#ifndef REVET_DB_POSTGRES_HPP
#define REVET_DB_POSTGRES_HPP

#pragma once
#ifdef REVET_DB_POSTGRES
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <format>
#include <libpq-fe.h>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <revet/task.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "common.hpp"
#include "pg_params.hpp"

namespace db::postgres {

using Row = std::vector<std::string>;
using Result = std::vector<Row>;

// For columns where NULL and '' mean different things. query() returns both as "".
using NullableRow = std::vector<std::optional<std::string>>;
using NullableResult = std::vector<NullableRow>;

inline std::string resolve_url(std::string_view url) {
    if (url.starts_with("env:")) {
        const char* val = std::getenv(std::string(url.substr(4)).c_str());
        if (!val) {
            throw std::runtime_error(std::format("env var not set: {}", url.substr(4)));
        }
        return val;
    }
    return std::string(url);
}

class Pool {
public:
    Pool(std::string_view url, int size) {
        const auto conn_str = resolve_url(url);
        conns_.reserve(size);
        try {
            std::generate_n(std::back_inserter(conns_), size, [&] {
                PGconn* c = PQconnectdb(conn_str.c_str());
                if (PQstatus(c) != CONNECTION_OK) {
                    std::string msg = PQerrorMessage(c);
                    PQfinish(c);
                    throw std::runtime_error("db::postgres: " + msg);
                }
                return c;
            });
        } catch (...) {
            // The destructor does not run when a constructor throws.
            close_all();
            throw;
        }
    }
    ~Pool() {
        close_all();
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    PGconn* acquire() {
        PGconn* c = nullptr;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [&] { return !conns_.empty(); });
            c = conns_.back();
            conns_.pop_back();
        }
        // A reset at release can fail while the server is down, so try again here.
        if (PQstatus(c) != CONNECTION_OK) {
            PQreset(c);
            if (PQstatus(c) != CONNECTION_OK) {
                std::string msg = PQerrorMessage(c);
                give_back(c);
                throw std::runtime_error("db::postgres: " + msg);
            }
        }
        return c;
    }
    void release(PGconn* c) {
        clean(c);
        give_back(c);
    }
    std::mutex& write_mutex() {
        return write_mu_;
    }

private:
    // A connection must not carry a broken socket or an open transaction over to
    // the next borrower.
    static void clean(PGconn* c) {
        if (PQstatus(c) != CONNECTION_OK) {
            PQreset(c);
            return;
        }
        const auto status = PQtransactionStatus(c);
        if (status == PQTRANS_INTRANS || status == PQTRANS_INERROR) {
            PQclear(PQexec(c, "ROLLBACK"));
            if (PQtransactionStatus(c) != PQTRANS_IDLE) {
                PQreset(c);
            }
        } else if (status != PQTRANS_IDLE) {
            PQreset(c);
        }
    }
    void give_back(PGconn* c) {
        {
            std::lock_guard lock(mu_);
            conns_.push_back(c);
        }
        cv_.notify_one();
    }
    void close_all() {
        for (auto c : conns_) {
            PQfinish(c);
        }
        conns_.clear();
    }

    std::vector<PGconn*> conns_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::mutex write_mu_;
};

// ── Handle<Intent> ────────────────────────────────────────────────────────────

template<typename Intent = Read>
class Handle {
public:
    Handle(Pool& pool) : pool_(pool), conn_(pool.acquire()) {
        if constexpr (std::is_same_v<Intent, Write>) {
            write_lock_.emplace(pool.write_mutex());
        }
    }
    ~Handle() {
        pool_.release(conn_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    // SQL with no arguments is sent verbatim: ? is not treated as a placeholder.
    Result query(std::string_view sql) {
        return extract<Row>(run(sql));
    }

    template<typename... Args>
    Result query(std::string_view sql, Args&&... args) {
        return extract<Row>(run(sql, std::forward<Args>(args)...));
    }

    NullableResult query_nullable(std::string_view sql) {
        return extract<NullableRow>(run(sql));
    }

    template<typename... Args>
    NullableResult query_nullable(std::string_view sql, Args&&... args) {
        return extract<NullableRow>(run(sql, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void exec(std::string_view sql, Args&&... args)
        requires std::is_same_v<Intent, Write>
    {
        query(sql, std::forward<Args>(args)...);
    }

    // Async variants — run on the I/O pool, free the calling coroutine
    template<typename... Args>
    auto async_query(std::string_view sql, Args&&... args) {
        return web::run_on_io(
            [this, s = std::string(sql), t = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() {
                return std::apply([this, &s](const auto&... a) { return query(s, a...); }, t);
            });
    }

    template<typename... Args>
    auto async_query_nullable(std::string_view sql, Args&&... args) {
        return web::run_on_io(
            [this, s = std::string(sql), t = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() {
                return std::apply([this, &s](const auto&... a) { return query_nullable(s, a...); }, t);
            });
    }

    template<typename... Args>
    auto async_exec(std::string_view sql, Args&&... args)
        requires std::is_same_v<Intent, Write>
    {
        return web::run_on_io(
            [this, s = std::string(sql), t = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() {
                std::apply([this, &s](const auto&... a) { exec(s, a...); }, t);
            });
    }

private:
    struct ResultDeleter {
        void operator()(PGresult* r) const {
            PQclear(r);
        }
    };
    using ResultPtr = std::unique_ptr<PGresult, ResultDeleter>;

    ResultPtr run(std::string_view sql) {
        // string_view does not guarantee a terminating NUL.
        return ResultPtr(PQexec(conn_, std::string(sql).c_str()));
    }

    template<typename... Args>
    ResultPtr run(std::string_view sql, Args&&... args) {
        const auto pg_sql = detail::positional_sql(sql);
        const std::vector<std::optional<std::string>> values{detail::to_nullable_pg_param(std::forward<Args>(args))...};
        std::vector<const char*> params;
        params.reserve(values.size());
        for (const auto& v : values) {
            params.push_back(v ? v->c_str() : nullptr);
        }
        return ResultPtr(PQexecParams(conn_, pg_sql.c_str(), static_cast<int>(params.size()), nullptr, params.data(),
                                      nullptr, nullptr, 0));
    }

    template<typename RowT>
    std::vector<RowT> extract(const ResultPtr& res) const {
        const auto status = PQresultStatus(res.get());
        if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
            // A lost connection can give a null result, which has no message of its own.
            std::string msg = res ? PQresultErrorMessage(res.get()) : "";
            if (msg.empty()) {
                msg = PQerrorMessage(conn_);
            }
            throw std::runtime_error(msg);
        }
        const int rows = PQntuples(res.get()), cols = PQnfields(res.get());
        std::vector<RowT> result;
        result.reserve(static_cast<size_t>(rows));
        for (auto r : std::views::iota(0, rows)) {
            RowT row;
            row.reserve(static_cast<size_t>(cols));
            for (auto c : std::views::iota(0, cols)) {
                if constexpr (std::is_same_v<RowT, NullableRow>) {
                    if (PQgetisnull(res.get(), r, c)) {
                        row.emplace_back(std::nullopt);
                        continue;
                    }
                }
                row.emplace_back(PQgetvalue(res.get(), r, c));
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    Pool& pool_;
    PGconn* conn_;
    std::optional<std::unique_lock<std::mutex>> write_lock_;
};

// ── Transaction ───────────────────────────────────────────────────────────────

// Rolls back on scope exit unless commit() was called. Must not outlive its Handle.
class Transaction {
public:
    explicit Transaction(Handle<Write>& handle) : handle_(handle) {
        handle_.exec("BEGIN");
    }
    ~Transaction() {
        if (open_) {
            try {
                handle_.exec("ROLLBACK");
            } catch (...) {
                // Connection lost: the server has already rolled back, and the
                // pool resets the connection on release.
            }
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        handle_.exec("COMMIT");
        open_ = false;
    }

private:
    Handle<Write>& handle_;
    bool open_ = true;
};

} // namespace db::postgres
#endif // REVET_DB_POSTGRES

#endif

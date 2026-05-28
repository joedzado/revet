#ifndef REVET_DB_POSTGRES_HPP
#define REVET_DB_POSTGRES_HPP

#pragma once
#ifdef REVET_DB_POSTGRES
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <format>
#include <libpq-fe.h>
#include <mutex>
#include <optional>
#include <ranges>
#include <revet/task.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "common.hpp"
#include "pg_params.hpp"

namespace db::postgres {

using Row = std::vector<std::string>;
using Result = std::vector<Row>;

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
        std::generate_n(std::back_inserter(conns_), size, [&] {
            PGconn* c = PQconnectdb(conn_str.c_str());
            if (PQstatus(c) != CONNECTION_OK) {
                std::string msg = PQerrorMessage(c);
                PQfinish(c);
                throw std::runtime_error("db::postgres: " + msg);
            }
            return c;
        });
    }
    ~Pool() {
        for (auto c : conns_) {
            PQfinish(c);
        }
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    PGconn* acquire() {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [&] { return !conns_.empty(); });
        const auto c = conns_.back();
        conns_.pop_back();
        return c;
    }
    void release(PGconn* c) {
        {
            std::lock_guard lock(mu_);
            conns_.push_back(c);
        }
        cv_.notify_one();
    }
    std::mutex& write_mutex() {
        return write_mu_;
    }

private:
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

    Result query(std::string_view sql) {
        return extract(PQexec(conn_, sql.data()));
    }

    template<typename... Args>
    Result query(std::string_view sql, Args&&... args) {
        const auto pg_sql = positional_sql(sql);
        const std::vector<std::string> strs{detail::to_pg_param(std::forward<Args>(args))...};
        std::vector<const char*> params;
        for (const auto& s : strs) {
            params.push_back(s.c_str());
        }
        return extract(PQexecParams(conn_, pg_sql.c_str(), static_cast<int>(params.size()), nullptr, params.data(),
                                    nullptr, nullptr, 0));
    }

    template<typename... Args>
    void exec(std::string_view sql, Args&&... args)
        requires std::is_same_v<Intent, Write>
    {
        query(sql, std::forward<Args>(args)...);
    }

    // Async variants — run on the I/O pool, free the calling coroutine
    template<typename... Args>
    auto async_query(std::string_view sql, Args&&... args) const {
        return web::run_on_io(
            [this, s = std::string(sql), t = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() {
                return std::apply([this, &s](const auto&... a) { return query(s, a...); }, t);
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
    // Translate ? placeholders to $1, $2, ... for PQexecParams
    static auto positional_sql(std::string_view sql) -> std::string {
        std::string result;
        result.reserve(sql.size() + 16);
        int n = 1;
        for (char c : sql) {
            if (c == '?') {
                result += std::format("${}", n++);
            } else {
                result += c;
            }
        }
        return result;
    }

    Result extract(PGresult* res) const {
        if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string msg = PQresultErrorMessage(res);
            PQclear(res);
            throw std::runtime_error(msg);
        }
        const int rows = PQntuples(res), cols = PQnfields(res);
        Result result;
        result.reserve(static_cast<size_t>(rows));
        for (auto r : std::views::iota(0, rows)) {
            Row row;
            row.reserve(static_cast<size_t>(cols));
            for (auto c : std::views::iota(0, cols)) {
                row.emplace_back(PQgetvalue(res, r, c));
            }
            result.push_back(std::move(row));
        }
        PQclear(res);
        return result;
    }

    Pool& pool_;
    PGconn* conn_;
    std::optional<std::unique_lock<std::mutex>> write_lock_;
};

} // namespace db::postgres
#endif // REVET_DB_POSTGRES

#endif

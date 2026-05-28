#ifndef REVET_DB_SQLITE_HPP
#define REVET_DB_SQLITE_HPP

#pragma once
#ifdef REVET_DB_SQLITE
#include <algorithm>
#include <condition_variable>
#include <format>
#include <mutex>
#include <optional>
#include <ranges>
#include <revet/task.hpp>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "common.hpp"

namespace db::sqlite {

using Row = std::vector<std::string>;
using Result = std::vector<Row>;

template<typename T>
inline void bind_arg(sqlite3_stmt* stmt, int idx, T&& arg) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_floating_point_v<U>) {
        sqlite3_bind_double(stmt, idx, static_cast<double>(arg));
    } else if constexpr (std::is_integral_v<U> && !std::is_same_v<U, bool>) {
        if constexpr (sizeof(U) <= 4) {
            sqlite3_bind_int(stmt, idx, static_cast<int>(arg));
        } else {
            sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(arg));
        }
    } else {
        std::string s{std::forward<T>(arg)};
        sqlite3_bind_text(stmt, idx, s.c_str(), -1, SQLITE_TRANSIENT);
    }
}

class Pool {
public:
    Pool(std::string_view path, int size) {
        conns_.reserve(size);
        std::string path_str(path); // Convert to null-terminated string
        std::generate_n(std::back_inserter(conns_), size, [&] {
            sqlite3* c = nullptr;
            int rc = sqlite3_open_v2(path_str.c_str(), &c, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
            if (rc != SQLITE_OK) {
                const char* errmsg = c ? sqlite3_errmsg(c) : "unknown";
                std::string full_error =
                    std::format("db::sqlite: cannot open {} (errno={}, msg={})", path_str, rc, errmsg);
                if (c) {
                    sqlite3_close(c);
                }
                throw std::runtime_error(full_error);
            }
            return c;
        });
    }
    ~Pool() {
        for (auto c : conns_) {
            sqlite3_close(c);
        }
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    sqlite3* acquire() {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [&] { return !conns_.empty(); });
        const auto c = conns_.back();
        conns_.pop_back();
        return c;
    }
    void release(sqlite3* c) {
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
    std::vector<sqlite3*> conns_;
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

    // Read — always available
    Result query(std::string_view sql) {
        return const_cast<const Handle*>(this)->query(sql);
    }
    Result query(std::string_view sql) const {
        Result result;
        char* err = nullptr;
        sqlite3_exec(
            conn_, sql.data(),
            [](void* data, int cols, char** vals, char**) -> int {
                auto& res = *static_cast<Result*>(data);
                Row row;
                for (auto i : std::views::iota(0, cols)) {
                    row.emplace_back(vals[i] ? vals[i] : "");
                }
                res.push_back(std::move(row));
                return 0;
            },
            &result, &err);
        if (err) {
            std::string msg(err);
            sqlite3_free(err);
            throw std::runtime_error(msg);
        }
        return result;
    }

    template<typename... Args>
    Result query(std::string_view sql, Args&&... args) {
        return const_cast<const Handle*>(this)->query(sql, std::forward<Args>(args)...);
    }
    template<typename... Args>
    Result query(std::string_view sql, Args&&... args) const {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(conn_, sql.data(), -1, &stmt, nullptr) != SQLITE_OK) {
            std::string err = sqlite3_errmsg(conn_);
            throw std::runtime_error("db::sqlite: prepare failed: " + err + " (SQL: " + std::string(sql) + ")");
        }
        int idx = 1;
        (bind_arg(stmt, idx++, std::forward<Args>(args)), ...);
        const int cols = sqlite3_column_count(stmt);
        Result result;
        int    rc = SQLITE_OK;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            Row row;
            row.reserve(static_cast<size_t>(cols));
            for (auto i : std::views::iota(0, cols)) {
                const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                row.emplace_back(txt ? txt : "");
            }
            result.push_back(std::move(row));
        }
        if (rc != SQLITE_DONE) {
            // Don't surface errmsg before finalize — errmsg is a pointer to
            // memory owned by the connection and finalize may invalidate it
            // on some sqlite builds. Snapshot the message first.
            std::string err = sqlite3_errmsg(conn_);
            sqlite3_finalize(stmt);
            throw std::runtime_error("db::sqlite: step failed: " + err + " (SQL: " + std::string(sql) + ")");
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Write — only available on Handle<Write>
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
                return std::apply([&](const auto&... a) { return query(s, a...); }, t);
            });
    }

    template<typename... Args>
    auto async_exec(std::string_view sql, Args&&... args)
        requires std::is_same_v<Intent, Write>
    {
        return web::run_on_io(
            [this, s = std::string(sql), t = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() {
                std::apply([&](const auto&... a) { exec(s, a...); }, t);
            });
    }

private:
    Pool& pool_;
    sqlite3* conn_;
    std::optional<std::unique_lock<std::mutex>> write_lock_;
};

} // namespace db::sqlite
#endif // REVET_DB_SQLITE

#endif

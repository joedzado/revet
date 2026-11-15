// Auto-generated — do not edit
#pragma once
#include <revet/db.hpp>
#include <revet/task.hpp>
#include <revet/decimal.hpp>
#include "models.hpp"
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace queries {

namespace items {

inline auto all_items(auto& db) -> web::Task<db::Result> {
    co_return co_await db.async_query("SELECT * FROM items; -- trailing :notparam 'it''s :x'");
}

inline auto sync_all_items(auto& db) -> db::Result {
    return db.query("SELECT * FROM items; -- trailing :notparam 'it''s :x'");
}

struct ByOwnerParams {
    std::string owner;
    int lim;
};

inline auto by_owner(auto& db, const ByOwnerParams& p) -> web::Task<std::vector<User>> {
    auto raw = co_await db.async_query("SELECT * FROM items WHERE owner = ? OR alt = ? LIMIT ?;", p.owner, p.owner, p.lim);
    std::vector<User> out;
    out.reserve(raw.size());
    for (auto& r : raw) out.push_back(revet::json::from_row<User>(r));
    co_return out;
}

inline auto sync_by_owner(auto& db, const ByOwnerParams& p) -> std::vector<User> {
    auto raw = db.query("SELECT * FROM items WHERE owner = ? OR alt = ? LIMIT ?;", p.owner, p.owner, p.lim);
    std::vector<User> out;
    out.reserve(raw.size());
    for (auto& r : raw) out.push_back(revet::json::from_row<User>(r));
    return out;
}

struct TouchParams {
    uint64_t id;
    std::optional<std::string> note;
};

inline auto touch(auto& db, const TouchParams& p) -> web::Task<void> {
    co_await db.async_exec("UPDATE items SET note = ? WHERE id = ?;", p.note, p.id);
}

inline auto sync_touch(auto& db, const TouchParams& p) -> void {
    db.exec("UPDATE items SET note = ? WHERE id = ?;", p.note, p.id);
}

} // namespace items

namespace users {

struct FindUserParams {
    int64_t id;
};

inline auto find_user(auto& db, const FindUserParams& p) -> web::Task<std::vector<std::tuple<int64_t, std::optional<std::string>, revet::Decimal>>> {
    auto raw = co_await db.async_query_nullable("SELECT id, nick, amt FROM users WHERE id = ?;", p.id);
    co_return db::typed<int64_t, std::optional<std::string>, revet::Decimal>(raw);
}

inline auto sync_find_user(auto& db, const FindUserParams& p) -> std::vector<std::tuple<int64_t, std::optional<std::string>, revet::Decimal>> {
    return db::typed<int64_t, std::optional<std::string>, revet::Decimal>(db.query_nullable("SELECT id, nick, amt FROM users WHERE id = ?;", p.id));
}

inline auto count_users(auto& db) -> web::Task<std::optional<int64_t>> {
    auto raw = co_await db.async_query_nullable("SELECT count(*) FROM users;");
    if (raw.empty() || raw[0].empty() || !raw[0][0]) co_return std::nullopt;
    co_return db::detail::convert<int64_t>(raw[0][0], 0);
}

inline auto sync_count_users(auto& db) -> std::optional<int64_t> {
    auto raw = db.query_nullable("SELECT count(*) FROM users;");
    if (raw.empty() || raw[0].empty() || !raw[0][0]) return std::nullopt;
    return db::detail::convert<int64_t>(raw[0][0], 0);
}

struct UpsertParams {
    std::optional<std::string> name;
};

inline auto upsert(auto& db, const UpsertParams& p) -> web::Task<void> {
    co_await db.async_exec("WITH x AS (SELECT 1) INSERT INTO users(name) VALUES (?);", p.name);
}

inline auto sync_upsert(auto& db, const UpsertParams& p) -> void {
    db.exec("WITH x AS (SELECT 1) INSERT INTO users(name) VALUES (?);", p.name);
}

inline auto cte_rows(auto& db) -> web::Task<std::vector<User>> {
    auto raw = co_await db.async_query("WITH a AS (SELECT 1) SELECT * FROM a;");
    std::vector<User> out;
    out.reserve(raw.size());
    for (auto& r : raw) out.push_back(revet::json::from_row<User>(r));
    co_return out;
}

inline auto sync_cte_rows(auto& db) -> std::vector<User> {
    auto raw = db.query("WITH a AS (SELECT 1) SELECT * FROM a;");
    std::vector<User> out;
    out.reserve(raw.size());
    for (auto& r : raw) out.push_back(revet::json::from_row<User>(r));
    return out;
}

} // namespace users

} // namespace queries

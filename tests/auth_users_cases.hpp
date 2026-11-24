// Test cases shared by the SQLite and Postgres auth user tests. The including file defines
// AuthDb (fixture with a `pool` and the schema created), W (the Write handle type), count(), and
// AUTH_REQUIRE_DB() (a SKIP when no database is available).
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <revet/auth/users.hpp>
#include <set>
#include <thread>
#include <vector>

namespace auth = web::auth;

TEST_CASE("auth users: create_schema can run twice", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    REQUIRE_NOTHROW(auth::create_schema(w));
}

TEST_CASE("auth users: first sight creates a user, second sight finds it", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto id = auth::find_or_create_user(w, "google", "sub-1");
    REQUIRE(id.size() == 32);
    REQUIRE(auth::user_exists(w, id));
    REQUIRE(auth::find_or_create_user(w, "google", "sub-1") == id);
    REQUIRE(auth::find_user(w, "google", "sub-1") == id);
    REQUIRE(count(w, "auth_users") == 1);
    REQUIRE(count(w, "auth_identities") == 1);
}

TEST_CASE("auth users: different identities are different users, ids are unique", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    std::set<std::string> ids;
    for (int i = 0; i < 50; ++i) {
        ids.insert(auth::find_or_create_user(w, "google", "sub-" + std::to_string(i)));
    }
    REQUIRE(ids.size() == 50);
    // Same subject under another provider is a different person.
    REQUIRE(auth::find_or_create_user(w, "apple", "sub-1") != auth::find_or_create_user(w, "google", "sub-1"));
}

TEST_CASE("auth users: unknown identity finds nothing", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    REQUIRE_FALSE(auth::find_user(w, "google", "nobody").has_value());
    REQUIRE_FALSE(auth::user_exists(w, "nobody"));
}

TEST_CASE("auth users: values with quotes and SQL are stored as data", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const std::string nasty = "x'); DROP TABLE auth_users; --";
    const auto id = auth::find_or_create_user(w, "p", nasty);
    REQUIRE(auth::find_user(w, "p", nasty) == id);
    REQUIRE(count(w, "auth_users") == 1);
}

TEST_CASE("auth users: linking a second identity", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto id = auth::find_or_create_user(w, "passkey", "cred-1");
    REQUIRE(auth::link_identity(w, id, "google", "g-1") == auth::LinkResult::linked);
    REQUIRE(auth::find_user(w, "google", "g-1") == id);
    REQUIRE(auth::identities_of(w, id).size() == 2);
}

TEST_CASE("auth users: link is idempotent and refuses an identity owned by someone else", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto alice = auth::find_or_create_user(w, "google", "alice");
    const auto bob = auth::find_or_create_user(w, "google", "bob");
    REQUIRE(auth::link_identity(w, alice, "google", "alice") == auth::LinkResult::already_linked);
    REQUIRE(auth::link_identity(w, alice, "google", "bob") == auth::LinkResult::taken);
    REQUIRE(auth::find_user(w, "google", "bob") == bob);
    REQUIRE(auth::link_identity(w, "no-such-user", "apple", "x") == auth::LinkResult::no_such_user);
    REQUIRE_FALSE(auth::find_user(w, "apple", "x").has_value());
}

TEST_CASE("auth users: unlink removes one identity but never the last", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto id = auth::find_or_create_user(w, "passkey", "cred-1");
    auth::link_identity(w, id, "google", "g-1");

    REQUIRE(auth::unlink_identity(w, id, "google", "g-1") == auth::UnlinkResult::unlinked);
    REQUIRE_FALSE(auth::find_user(w, "google", "g-1").has_value());
    REQUIRE(auth::unlink_identity(w, id, "passkey", "cred-1") == auth::UnlinkResult::last_identity);
    REQUIRE(auth::find_user(w, "passkey", "cred-1") == id);
}

TEST_CASE("auth users: unlink of a missing or someone else's identity is not_found", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto alice = auth::find_or_create_user(w, "google", "alice");
    auth::link_identity(w, alice, "apple", "a");
    auth::find_or_create_user(w, "google", "bob");
    REQUIRE(auth::unlink_identity(w, alice, "google", "bob") == auth::UnlinkResult::not_found);
    REQUIRE(auth::unlink_identity(w, alice, "google", "missing") == auth::UnlinkResult::not_found);
    REQUIRE(auth::find_user(w, "google", "bob").has_value());
}

TEST_CASE("auth users: a freed identity can be claimed by another user", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto alice = auth::find_or_create_user(w, "passkey", "a1");
    const auto bob = auth::find_or_create_user(w, "passkey", "b1");
    auth::link_identity(w, alice, "google", "shared");
    REQUIRE(auth::unlink_identity(w, alice, "google", "shared") == auth::UnlinkResult::unlinked);
    REQUIRE(auth::link_identity(w, bob, "google", "shared") == auth::LinkResult::linked);
}

TEST_CASE("auth users: deleting a user removes every identity and leaves others alone", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto alice = auth::find_or_create_user(w, "google", "alice");
    auth::link_identity(w, alice, "apple", "alice-apple");
    const auto bob = auth::find_or_create_user(w, "google", "bob");

    REQUIRE(auth::delete_user(w, alice));
    REQUIRE_FALSE(auth::user_exists(w, alice));
    REQUIRE_FALSE(auth::find_user(w, "google", "alice").has_value());
    REQUIRE_FALSE(auth::find_user(w, "apple", "alice-apple").has_value());
    REQUIRE(auth::user_exists(w, bob));
    REQUIRE(count(w, "auth_identities") == 1);
    REQUIRE_FALSE(auth::delete_user(w, alice));

    // The deleted identity starts over as a brand new user.
    REQUIRE(auth::find_or_create_user(w, "google", "alice") != alice);
}

TEST_CASE("auth users: deleting a user cascades to tables that reference it", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    w.exec("CREATE TABLE child (user_id TEXT NOT NULL REFERENCES auth_users(id) ON DELETE CASCADE)");
    const auto id = auth::find_or_create_user(w, "google", "alice");
    w.exec("INSERT INTO child (user_id) VALUES (?)", id);
    REQUIRE(auth::delete_user(w, id));
    REQUIRE(count(w, "child") == 0);
}

TEST_CASE("auth users: concurrent creation of one identity yields one user", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    std::vector<std::string> ids(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        threads.emplace_back([&, i] {
            W w(fx.pool);
            ids[i] = auth::find_or_create_user(w, "google", "same");
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    REQUIRE(std::set<std::string>(ids.begin(), ids.end()).size() == 1);
    W w(fx.pool);
    REQUIRE(count(w, "auth_users") == 1);
    REQUIRE(count(w, "auth_identities") == 1);
}

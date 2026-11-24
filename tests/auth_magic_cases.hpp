// Test cases shared by the SQLite and Postgres magic-link tests. The including file defines AuthDb (a
// fixture with a `pool` and the users, tokens, passkeys and magic-link schemas), W (Write handle),
// count(), and AUTH_REQUIRE_DB().
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <mutex>
#include <revet/auth/magic.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/auth/tokens.hpp>
#include <set>
#include <thread>
#include <vector>

namespace auth = web::auth;
namespace magic = web::auth::magic;

namespace {

constexpr long long T0 = 1'800'000'000;

struct Outbox {
    std::mutex mu;
    std::vector<web::mail::Message> sent;
};

// Sets the config for one test and captures what would be mailed.
auto capture(Outbox& out) {
    auto& c = magic::config();
    c.base_url = "https://app.example.com";
    c.path = "/auth/magic";
    c.app_name = "Example";
    c.ttl = std::chrono::seconds(900);
    c.max_per_address_per_hour = 5;
    c.send = [&out](web::mail::Message m) -> void {
        const std::scoped_lock lock(out.mu);
        out.sent.push_back(std::move(m));
    };
}

auto token_in(const web::mail::Message& m) -> std::string {
    const auto at = m.text.find("?token=");
    REQUIRE(at != std::string::npos);
    const auto start = at + 7;
    return m.text.substr(start, m.text.find_first_of("\r\n \"<", start) - start);
}

} // namespace

TEST_CASE("magic: a link is mailed and only its hash is stored", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);

    REQUIRE(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0) == magic::RequestResult::sent);
    REQUIRE(out.sent.size() == 1);
    const auto& m = out.sent[0];
    REQUIRE(m.to == "ann@example.com");
    REQUIRE(m.subject == "Your Example sign-in link");
    REQUIRE(m.text.find("https://app.example.com/auth/magic?token=") != std::string::npos);
    REQUIRE(m.text.find("15 minutes") != std::string::npos);
    REQUIRE(m.html.find("href=\"https://app.example.com/auth/magic?token=") != std::string::npos);

    const auto token = token_in(m);
    REQUIRE(token.size() == 43); // 32 random bytes, base64url
    const auto rows = w.query_as<std::string, std::string>("SELECT token_hash, email FROM auth_magic_links");
    REQUIRE(rows.size() == 1);
    REQUIRE(std::get<0>(rows[0]).size() == 64);
    REQUIRE(std::get<0>(rows[0]) != token);
    REQUIRE(std::get<0>(rows[0]).find(token) == std::string::npos);
    REQUIRE(std::get<1>(rows[0]) == "ann@example.com");
    // No copy of the token in any column.
    REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_magic_links WHERE email = ? OR user_id = ?", token, token)
                .value_or(-1) == 0);
}

TEST_CASE("magic: the link signs in once and creates the account", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    const auto token = token_in(out.sent[0]);

    const auto done = magic::complete(w, token, T0 + 60);
    REQUIRE(done.has_value());
    REQUIRE(done->new_user);
    REQUIRE_FALSE(done->linked);
    REQUIRE(done->email == "ann@example.com");
    REQUIRE(done->user_id.size() == 32);
    REQUIRE(auth::find_user(w, "email", "ann@example.com") == done->user_id);

    const auto again = magic::complete(w, token, T0 + 61);
    REQUIRE_FALSE(again.has_value());
    REQUIRE(again.error() == magic::CompleteError::invalid_link);
    REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_users").value_or(-1) == 1);
}

TEST_CASE("magic: a second link for the same address signs in the same user", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    const auto first = magic::complete(w, token_in(out.sent[0]), T0 + 10);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0 + 100);
    const auto second = magic::complete(w, token_in(out.sent[1]), T0 + 110);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(second->user_id == first->user_id);
    REQUIRE_FALSE(second->new_user);
}

TEST_CASE("magic: addresses are compared without case or surrounding spaces", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    magic::request(w, "  Ann@Example.COM ", magic::Purpose::sign_in, {}, T0 + 1);
    REQUIRE(out.sent.size() == 2);
    REQUIRE(out.sent[1].to == "ann@example.com");
    const auto a = magic::complete(w, token_in(out.sent[0]), T0 + 5);
    const auto b = magic::complete(w, token_in(out.sent[1]), T0 + 6);
    REQUIRE(a->user_id == b->user_id);
}

TEST_CASE("magic: an expired, unknown or malformed token is refused", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    const auto token = token_in(out.sent[0]);

    const auto late = magic::complete(w, token, T0 + 900); // exactly at expiry
    REQUIRE_FALSE(late.has_value());
    REQUIRE(late.error() == magic::CompleteError::invalid_link);
    REQUIRE(magic::complete(w, token, T0 + 899).has_value()); // an expired attempt did not spend it

    for (const std::string& bad : {std::string(), std::string("nope"), std::string(43, 'A'), std::string(500, 'x'),
                                   token + "x", token.substr(1), std::string("' OR 1=1 --")}) {
        INFO(bad);
        REQUIRE_FALSE(magic::complete(w, bad, T0 + 10).has_value());
    }
    REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_users").value_or(-1) == 1);
}

TEST_CASE("magic: addresses that cannot be mailed are refused without a trace", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    for (const std::string& bad :
         {std::string(), std::string("ann"), std::string("ann@example"), std::string("ann@example.com\r\nBcc: x@y.com"),
          std::string("a@b.c"), std::string("<ann@example.com>"), std::string(300, 'a')}) {
        INFO(bad);
        REQUIRE(magic::request(w, bad, magic::Purpose::sign_in, {}, T0) == magic::RequestResult::invalid_email);
    }
    REQUIRE(out.sent.empty());
    REQUIRE(count(w, "auth_magic_links") == 0);
}

TEST_CASE("magic: an address gets a bounded number of links per hour", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0 + i) ==
                magic::RequestResult::sent);
    }
    REQUIRE(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0 + 10) ==
            magic::RequestResult::throttled);
    REQUIRE(out.sent.size() == 5);
    REQUIRE(count(w, "auth_magic_links") == 5);
    // Other addresses are unaffected, and the allowance comes back.
    REQUIRE(magic::request(w, "bob@example.com", magic::Purpose::sign_in, {}, T0 + 11) == magic::RequestResult::sent);
    REQUIRE(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0 + 3601 + 4) ==
            magic::RequestResult::sent);
}

TEST_CASE("magic: a signed-in user adds an email address", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    const auto user = auth::find_or_create_user(w, "test", "someone");

    REQUIRE(magic::request(w, "ann@example.com", magic::Purpose::link, user, T0) == magic::RequestResult::sent);
    REQUIRE(out.sent[0].subject == "Confirm your email address for Example");
    const auto done = magic::complete(w, token_in(out.sent[0]), T0 + 5);
    REQUIRE(done.has_value());
    REQUIRE(done->linked);
    REQUIRE_FALSE(done->new_user);
    REQUIRE(done->user_id == user);
    REQUIRE(auth::find_user(w, "email", "ann@example.com") == user);
    REQUIRE(auth::identities_of(w, user).size() == 2);

    // From now on a sign-in link for that address signs in the same user.
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0 + 100);
    REQUIRE(magic::complete(w, token_in(out.sent[1]), T0 + 101)->user_id == user);
}

TEST_CASE("magic: adding an address that belongs to someone else is refused", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    const auto ann = magic::complete(w, token_in(out.sent[0]), T0 + 1);
    const auto other = auth::find_or_create_user(w, "test", "other");

    magic::request(w, "ann@example.com", magic::Purpose::link, other, T0 + 10);
    const auto done = magic::complete(w, token_in(out.sent[1]), T0 + 11);
    REQUIRE_FALSE(done.has_value());
    REQUIRE(done.error() == magic::CompleteError::email_taken);
    REQUIRE(auth::find_user(w, "email", "ann@example.com") == ann->user_id);
    REQUIRE(magic::complete(w, token_in(out.sent[1]), T0 + 12).error() == magic::CompleteError::invalid_link); // spent
}

TEST_CASE("magic: linking needs a user that exists", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    REQUIRE(magic::request(w, "ann@example.com", magic::Purpose::link, std::string(32, 'f'), T0) ==
            magic::RequestResult::no_such_user);
    REQUIRE(out.sent.empty());

    // The user is deleted between the request and the click.
    const auto user = auth::find_or_create_user(w, "test", "gone");
    magic::request(w, "ann@example.com", magic::Purpose::link, user, T0);
    REQUIRE(auth::delete_user(w, user));
    REQUIRE(magic::complete(w, token_in(out.sent[0]), T0 + 5).error() == magic::CompleteError::invalid_link);
    REQUIRE_FALSE(auth::find_user(w, "email", "ann@example.com").has_value());
}

TEST_CASE("magic: of many racing clicks on one link exactly one wins", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    std::string token;
    {
        W w(fx.pool);
        magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
        token = token_in(out.sent[0]);
    }
    std::vector<int> won(8, 0);
    std::vector<std::thread> threads;
    threads.reserve(won.size());
    for (int& i : won) {
        threads.emplace_back([&] -> void {
            W w(fx.pool);
            i = magic::complete(w, token, T0 + 5).has_value() ? 1 : 0;
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    int total = 0;
    for (const int v : won) {
        total += v;
    }
    REQUIRE(total == 1);
    W w(fx.pool);
    REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_users").value_or(-1) == 1);
}

TEST_CASE("magic: an email address lets a user back in after losing every passkey", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);

    // A passkey user adds an email address.
    auto& pk = auth::passkeys::config();
    pk.rp = {.rp_id = "app.example.com", .origins = {"https://app.example.com"}};
    const auto user = auth::find_or_create_user(w, "passkey", "credential-1");
    w.exec("INSERT INTO auth_passkeys (credential_id, user_id, public_key, sign_count, backup_eligible, backup_state, "
           "label, created_at, last_used_at)"
           " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
           std::string("credential-1"), user, std::string("AAAA"), 0LL, 0, 0, std::string("old phone"), T0, 0LL);
    magic::request(w, "ann@example.com", magic::Purpose::link, user, T0);
    REQUIRE(magic::complete(w, token_in(out.sent[0]), T0 + 1).has_value());

    // The only passkey can now be removed, because another way in exists...
    REQUIRE(auth::passkeys::remove_passkey(w, user, "credential-1") == auth::passkeys::RemoveResult::removed);
    // ...and a mailed link signs the same user in and can start a session.
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0 + 1000);
    const auto back = magic::complete(w, token_in(out.sent[1]), T0 + 1001);
    REQUIRE(back.has_value());
    REQUIRE(back->user_id == user);
    const auto pair = auth::tokens::issue(w, back->user_id, "new phone", T0 + 1001);
    REQUIRE(auth::tokens::verify_access(pair.access_token, T0 + 1002)->user_id == user);
}

TEST_CASE("magic: the link comes from configuration, never from the request", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);

    magic::config().base_url = "";
    REQUIRE_THROWS(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0));
    magic::config().base_url = "http://evil.example.com";
    REQUIRE_THROWS(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0));
    magic::config().base_url = "https://app.example.com\"><script>";
    REQUIRE_THROWS(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0));
    magic::config().base_url = "http://localhost:8080"; // development
    REQUIRE(magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0) == magic::RequestResult::sent);
    REQUIRE(out.sent[0].text.find("http://localhost:8080/auth/magic?token=") != std::string::npos);
    REQUIRE(count(w, "auth_magic_links") == 1); // the refused attempts left nothing behind
}

TEST_CASE("magic: the app name cannot inject markup into the mail", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    magic::config().app_name = "<b>Evil</b> & Co";
    W w(fx.pool);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    REQUIRE(out.sent[0].html.find("<b>Evil</b>") == std::string::npos);
    REQUIRE(out.sent[0].html.find("&lt;b&gt;Evil&lt;/b&gt; &amp; Co") != std::string::npos);
}

TEST_CASE("magic: old link records are cleared", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    REQUIRE(count(w, "auth_magic_links") == 1);
    magic::request(w, "bob@example.com", magic::Purpose::sign_in, {}, T0 + (3 * 86400));
    REQUIRE(count(w, "auth_magic_links") == 1);
}

TEST_CASE("magic: peeking shows what a link would do without spending it", "[auth][db][magic]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    Outbox out;
    capture(out);
    W w(fx.pool);
    const auto user = auth::find_or_create_user(w, "test", "someone");
    magic::request(w, "ann@example.com", magic::Purpose::sign_in, {}, T0);
    magic::request(w, "bob@example.com", magic::Purpose::link, user, T0);

    const auto a = magic::peek(w, token_in(out.sent[0]), T0 + 1);
    REQUIRE(a.has_value());
    REQUIRE(a->email == "ann@example.com");
    REQUIRE(a->purpose == magic::Purpose::sign_in);
    REQUIRE(magic::peek(w, token_in(out.sent[1]), T0 + 1)->purpose == magic::Purpose::link);

    REQUIRE(magic::peek(w, token_in(out.sent[0]), T0 + 1).has_value()); // still there
    REQUIRE(magic::complete(w, token_in(out.sent[0]), T0 + 2).has_value());
    REQUIRE_FALSE(magic::peek(w, token_in(out.sent[0]), T0 + 3).has_value());   // spent
    REQUIRE_FALSE(magic::peek(w, token_in(out.sent[1]), T0 + 900).has_value()); // expired
    REQUIRE_FALSE(magic::peek(w, "nope", T0).has_value());
    REQUIRE_FALSE(magic::peek(w, "", T0).has_value());
}

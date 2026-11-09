// Helpers added after sweeping the apps for code each one rewrote.
#include <catch2/catch_test_macros.hpp>
#include <revet/hash.hpp>
#include <revet/jobs.hpp>
#include <revet/json.hpp>
#include <revet/otp.hpp>
#include <revet/page.hpp>
#include <revet/svg.hpp>
#include <revet/time.hpp>
#include <revet/web.hpp>
#include <revet/db/common.hpp>

#include <array>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

// ── html / svg ────────────────────────────────────────────────────────────────

TEST_CASE("html::num rounds and trims", "[html]") {
    CHECK(html::num(12.5) == "12.5");
    CHECK(html::num(3.0) == "3");
    CHECK(html::num(0.1 + 0.2) == "0.3");
    CHECK(html::num(-0.0001) == "0");
    CHECK(html::num(1234567.0) == "1234567");
    CHECK(html::num(2.345, 1) == "2.3");
    CHECK(html::num(std::numeric_limits<double>::quiet_NaN()) == "0");
}

TEST_CASE("attributes take numbers", "[html]") {
    CHECK(html::attr.min(0).str == R"( min="0")");
    CHECK(html::attr.step(0.25).str == R"( step="0.25")");
    CHECK(html::attr("max", 10L).str == R"( max="10")");
    CHECK(html::attr.value(std::string("a\"b")).str == R"( value="a&quot;b")");
    CHECK(svg::attr.x(1.5).str == R"( x="1.5")");
}

TEST_CASE("aria, role and data attributes", "[html]") {
    CHECK(html::attr.aria_label("Close").str == R"( aria-label="Close")");
    CHECK(html::attr.aria("pressed", "true").str == R"( aria-pressed="true")");
    CHECK(html::attr.role("button").str == R"( role="button")");
    CHECK(html::attr.data("n", 3).str == R"( data-n="3")");
    CHECK(html::attr.inputmode("decimal").str == R"( inputmode="decimal")");
}

TEST_CASE("inline_style builds escaped custom properties", "[html]") {
    const html::Node n = html::inline_style{}.var("x", 12.345, "%").prop("top", 4, "px").var("c", "red\"");
    CHECK(n.str == R"( style="--x:12.35%;top:4px;--c:red&quot;")");
    CHECK(html::div{html::inline_style{}.var("y", 50.0, "%"), "a"}.str == R"(<div style="--y:50%">a</div>)");
}

TEST_CASE("svg::path_data and points", "[svg]") {
    svg::path_data d;
    d.move(0, 10).line(5.26, 3.333).curve(1, 2, 3, 4, 5, 6).close();
    CHECK(d.str() == "M0,10L5.3,3.3C1,2,3,4,5,6Z");
    CHECK(svg::path{svg::attr.d(d)}.str == R"(<path d="M0,10L5.3,3.3C1,2,3,4,5,6Z"/>)");

    const std::vector<std::pair<double, double>> pts{{0, 0}, {1.26, 2}, {3, 4.5}};
    CHECK(svg::path_data{2}.polyline(pts).str() == "M0,0L1.26,2L3,4.5");
    CHECK(svg::points(pts) == "0,0 1.3,2 3,4.5");
    CHECK(svg::path_data{}.polyline(std::vector<std::pair<double, double>>{}).empty());
    CHECK(svg::attr.viewbox(100, 40).str == R"( viewBox="0 0 100 40")");
}

TEST_CASE("html_doc sets lang; standard_head has the basics", "[html]") {
    const auto doc = html::html_doc{html::lang("en"), html::standard_head({.title = "A <b>", .theme_color = "#111"}), html::body{"x"}};
    CHECK(doc.str.starts_with(R"(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">)"));
    CHECK(doc.str.find("<title>A &lt;b&gt;</title>") != std::string::npos);
    CHECK(doc.str.find(R"(<meta name="theme-color" content="#111">)") != std::string::npos);
    CHECK(doc.str.find("/_revet/htmx.js") != std::string::npos);
    CHECK(doc.str.find("<style>") != std::string::npos);
    CHECK(doc.str.find("optimistic.js") == std::string::npos);
    CHECK(html::html_doc{html::head{}, html::body{}}.str.starts_with("<!DOCTYPE html><html><head>"));
}

// ── json ──────────────────────────────────────────────────────────────────────

TEST_CASE("json::Writer builds documents", "[json]") {
    revet::json::Writer w;
    w.object()
        .field("id", 7)
        .field("name", "a\"b\n")
        .field("ok", true)
        .field("ratio", 0.5)
        .field("none", std::optional<int>{})
        .key("tags")
        .array()
        .value("x")
        .value(2)
        .object()
        .end()
        .end()
        .end();
    CHECK(w.str() == R"({"id":7,"name":"a\"b\n","ok":true,"ratio":0.5,"none":null,"tags":["x",2,{}]})");
}

TEST_CASE("json::Writer rejects misuse", "[json]") {
    revet::json::Writer w;
    w.object();
    CHECK_THROWS(w.value(1)); // no key
    revet::json::Writer w2;
    CHECK_THROWS(w2.end());
    revet::json::Writer w3;
    w3.value(1);
    CHECK_THROWS(w3.value(2));
}

TEST_CASE("json::quote escapes control characters; number() has no NaN", "[json]") {
    CHECK(revet::json::quote(std::string("a\x01" "b\t")) == R"("a\u0001b\t")");
    CHECK(revet::json::number(std::numeric_limits<double>::infinity()) == "null");
    CHECK(revet::json::number(1.5) == "1.5");
}

TEST_CASE("json::Value::get with fallbacks", "[json]") {
    revet::json::Document doc(R"({"a":3,"b":"x","n":null,"f":2.5})");
    auto root = doc.root();
    CHECK(root.get<int64_t>("a", 0) == 3);
    CHECK(root.get<int64_t>("missing", 9) == 9);
    CHECK(root.get<int64_t>("n", 9) == 9);
    CHECK(root.get<int64_t>("b", 9) == 9); // wrong type
    CHECK(root.get("b", "") == "x");
    CHECK(root.get<double>("f", 0) == 2.5);
    CHECK_FALSE(root.get<bool>("missing"));
}

// ── request params and URLs ───────────────────────────────────────────────────

TEST_CASE("ParamMap typed get never throws", "[web]") {
    web::ParamMap m;
    m["page"] = "3";
    m["bad"] = "12abc";
    m["neg"] = "-4";
    m["f"] = " 2.5 ";
    m["empty"] = "";
    m["on"] = "on";
    m["huge"] = "99999999999999999999";
    CHECK(m.get("page", 1) == 3);
    CHECK(m.get("bad", 1) == 1);
    CHECK(m.get("missing", 1) == 1);
    CHECK(m.get("empty", 1) == 1);
    CHECK(m.get("neg", 0) == -4);
    CHECK(m.get("f", 0.0) == 2.5);
    CHECK(m.get("on", false));
    CHECK(m.get("huge", 5) == 5);
    CHECK_FALSE(m.as<int>("bad").has_value());
    CHECK(m.get("page", "x") == "3"); // the string overload still applies
    CHECK(m.has("empty"));
}

TEST_CASE("url_encode and url_decode_path", "[sanitize]") {
    CHECK(sanitize::url_encode("slow mornings/ä+&") == "slow%20mornings%2F%C3%A4%2B%26");
    CHECK(sanitize::url_encode("A-z_0.~") == "A-z_0.~");
    CHECK(sanitize::url_decode_path("slow%20mornings+x") == "slow mornings+x");
    CHECK(sanitize::url_decode_path("bad%2") == "bad%2");
    CHECK(sanitize::url_decode_path("%zz") == "%zz");
    CHECK(sanitize::url_decode_path(sanitize::url_encode("ä b/c")) == "ä b/c");
}

TEST_CASE("route params arrive decoded", "[web]") {
    web::ParamMap out;
    REQUIRE(web::match_route("/topic/:name", "/topic/slow%20mornings", out));
    CHECK(out.get("name") == "slow mornings");
}

// ── time ──────────────────────────────────────────────────────────────────────

TEST_CASE("time::ago", "[time]") {
    using revet::time::ago;
    const int64_t now = 1'800'000'000'000;
    CHECK(ago(now - 10'000, now) == "just now");
    CHECK(ago(now + 50'000, now) == "just now");
    CHECK(ago(now - 12 * 60'000, now) == "12 min ago");
    CHECK(ago(now - 5 * 3'600'000, now) == "5 h ago");
    CHECK(ago(now - 3 * 86'400'000LL, now) == "3 days ago");
    CHECK(ago(now - 120 * 86'400'000LL, now) == "4 months ago");
    CHECK(ago(now - 800 * 86'400'000LL, now) == "2 years ago");
}

TEST_CASE("time::parse_date and iso_date", "[time]") {
    using namespace std::chrono;
    const auto d = revet::time::parse_date("2026-11-08T10:00:00Z");
    REQUIRE(d);
    CHECK(revet::time::iso_date(*d) == "2026-11-08");
    CHECK_FALSE(revet::time::parse_date("2026-02-30"));
    CHECK_FALSE(revet::time::parse_date("2026-1-08"));
    CHECK_FALSE(revet::time::parse_date(""));
    CHECK(revet::time::days_between(*d, year{2026} / 12 / 8) == 30);
    CHECK(revet::time::ago(year{2026} / 11 / 7, year{2026} / 11 / 8) == "yesterday");
    CHECK(revet::time::ago(year{2026} / 11 / 8, year{2026} / 11 / 8) == "today");
    CHECK(revet::time::today_iso().size() == 10);
}

// ── error pages ───────────────────────────────────────────────────────────────

TEST_CASE("error_page without and with a renderer", "[web]") {
    web::Request req;
    auto plain = web::bad_request(req, "A transaction needs a name.");
    CHECK(plain.status.code == 400);
    CHECK(plain.body == "A transaction needs a name.");
    CHECK(web::not_found(req).body == "404 Not Found");

    web::on_error_page([](const web::Request&, web::Status s, std::string_view msg) {
        return web::Response{.body = std::format("<h1>{}</h1>{}", s.text, msg), .status = web::status::ok};
    });
    auto page = web::not_found(req, "No such account.");
    CHECK(page.status.code == 404); // forced, though the renderer said 200
    CHECK(page.body == "<h1>Not Found</h1>No such account.");
    auto handler = web::detail::error_handler_for(web::status::method_not_allowed);
    REQUIRE(handler);
    CHECK(handler(req).status.code == 405);
    web::on_error_page({});
    CHECK_FALSE(web::detail::error_handler_for(web::status::method_not_allowed));
}

// ── hashes and one-time codes ─────────────────────────────────────────────────

TEST_CASE("sha1, hmac_sha1, md5 known answers", "[hash]") {
    CHECK(web::hash::sha1_hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(web::hash::sha1_hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(web::hash::hex(web::hash::hmac_sha1("key", "The quick brown fox jumps over the lazy dog")) ==
          "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");
    CHECK(web::hash::md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(web::hash::md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
}

TEST_CASE("TOTP matches RFC 6238 and refuses replays", "[otp]") {
    const std::string secret = "12345678901234567890";
    CHECK(web::otp::code(secret, 59 / 30) == "287082");                // RFC 6238 T=59, last 6 digits
    CHECK(web::otp::code(secret, 1111111109 / 30) == "081804");       // T=1111111109
    CHECK(web::otp::base32_decode(web::otp::base32_encode(secret)) == secret);
    CHECK(web::otp::base32_encode(secret) == "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");

    web::otp::ReplayGuard guard;
    const uint64_t now = 1111111109 / 30;
    CHECK(guard.verify(secret, web::otp::code(secret, now), now));
    CHECK_FALSE(guard.verify(secret, web::otp::code(secret, now), now)); // same code again
    CHECK_FALSE(guard.verify(secret, "000000", now + 1));
    CHECK(guard.verify(secret, web::otp::code(secret, now + 1), now + 1));
}

TEST_CASE("otp::Throttle locks after N failures", "[otp]") {
    web::otp::Throttle t{2, std::chrono::seconds{60}};
    CHECK_FALSE(t.locked("u"));
    t.fail("u");
    CHECK_FALSE(t.locked("u"));
    t.fail("u");
    CHECK(t.locked("u"));
    CHECK_FALSE(t.locked("other"));
    t.reset("u");
    CHECK_FALSE(t.locked("u"));
}

// ── jobs ──────────────────────────────────────────────────────────────────────

TEST_CASE("jobs::SingleFlight runs one at a time and records the outcome", "[jobs]") {
    static web::jobs::SingleFlight job;
    std::atomic<bool> release{false};
    REQUIRE(job.start([&] {
        while (!release) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return std::string("synced 3");
    }));
    CHECK(job.status().running);
    CHECK_FALSE(job.start([] { return std::string("second"); }));
    release = true;
    for (int i = 0; i < 2000 && job.status().running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    auto s = job.status();
    CHECK_FALSE(s.running);
    CHECK(s.last_ok);
    CHECK(s.last_result == "synced 3");
    CHECK(s.finished_ms >= s.started_ms);

    CHECK(job.run([]() -> std::string { throw std::runtime_error("no network"); }));
    CHECK_FALSE(job.status().last_ok);
    CHECK(job.status().last_result == "failed: no network");
}

TEST_CASE("jobs::every repeats", "[jobs]") {
    static std::atomic<int> n{0};
    web::jobs::every(std::chrono::milliseconds{2}, [] { ++n; });
    for (int i = 0; i < 2000 && n < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(n >= 3);
}

// ── db typed conversion ───────────────────────────────────────────────────────

TEST_CASE("db::typed converts columns", "[db]") {
    using Opt = std::optional<std::string>;
    const std::vector<std::vector<Opt>> raw{{Opt{"a"}, Opt{"2.5"}, std::nullopt, Opt{"3.0"}, Opt{"t"}}};
    auto rows = db::typed<std::string, double, std::optional<int>, int64_t, bool>(raw);
    REQUIRE(rows.size() == 1);
    auto [name, x, maybe, n, flag] = rows[0];
    CHECK(name == "a");
    CHECK(x == 2.5);
    CHECK_FALSE(maybe);
    CHECK(n == 3);
    CHECK(flag);
    CHECK_THROWS(db::typed<int>(std::vector<std::vector<Opt>>{{std::nullopt}}));
    CHECK_THROWS(db::typed<int>(std::vector<std::vector<Opt>>{{Opt{"x1"}}}));
    CHECK_THROWS(db::typed<int, int>(std::vector<std::vector<Opt>>{{Opt{"1"}}}));
}

// ── config ────────────────────────────────────────────────────────────────────

TEST_CASE("data_dir and read_text", "[web]") {
    CHECK(web::data_dir() == "data");
    web::config::data_dir("/tmp");
    CHECK(web::data_dir() == "/tmp");
    web::config::data_dir("data");
    CHECK_FALSE(web::read_text("/nonexistent/revet").has_value());
}

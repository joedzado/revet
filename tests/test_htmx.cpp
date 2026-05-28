#include <catch2/catch_test_macros.hpp>
#include <revet/html.hpp>
#include <revet/htmx.hpp>
#include <revet/web.hpp>
#include <string>

// ── DSL Attribute Tests ───────────────────────────────────────────────────

TEST_CASE("htmx::attr::post — emits hx-post attribute", "[htmx]") {
    const auto n = htmx::attr::post("/api/submit");
    CHECK(n.str == " hx-post=\"/api/submit\"");
}

TEST_CASE("htmx::attr::get — emits hx-get attribute", "[htmx]") {
    const auto n = htmx::attr::get("/search");
    CHECK(n.str == " hx-get=\"/search\"");
}

TEST_CASE("htmx::attr::put — emits hx-put attribute", "[htmx]") {
    const auto n = htmx::attr::put("/api/item/1");
    CHECK(n.str == " hx-put=\"/api/item/1\"");
}

TEST_CASE("htmx::attr::del — emits hx-delete attribute", "[htmx]") {
    const auto n = htmx::attr::del("/api/item/1");
    CHECK(n.str == " hx-delete=\"/api/item/1\"");
}

TEST_CASE("htmx::attr::target — emits hx-target attribute", "[htmx]") {
    const auto n = htmx::attr::target("#results");
    CHECK(n.str == " hx-target=\"#results\"");
}

TEST_CASE("htmx::attr::swap — emits hx-swap attribute", "[htmx]") {
    const auto n = htmx::attr::swap("innerHTML");
    CHECK(n.str == " hx-swap=\"innerHTML\"");
}

TEST_CASE("htmx::attr::select — emits hx-select attribute", "[htmx]") {
    const auto n = htmx::attr::select("tr");
    CHECK(n.str == " hx-select=\"tr\"");
}

TEST_CASE("htmx::attr::trigger — emits hx-trigger attribute", "[htmx]") {
    const auto n = htmx::attr::trigger("keyup changed delay:500ms");
    CHECK(n.str == " hx-trigger=\"keyup changed delay:500ms\"");
}

TEST_CASE("htmx::attr::confirm — emits hx-confirm attribute", "[htmx]") {
    const auto n = htmx::attr::confirm("Are you sure?");
    CHECK(n.str == " hx-confirm=\"Are you sure?\"");
}

TEST_CASE("htmx::attr::push_url — emits hx-push-url attribute", "[htmx]") {
    const auto n = htmx::attr::push_url("/items/1");
    CHECK(n.str == " hx-push-url=\"/items/1\"");
}

TEST_CASE("htmx::attr::replace_url — emits hx-replace-url attribute", "[htmx]") {
    const auto n = htmx::attr::replace_url("/new-path");
    CHECK(n.str == " hx-replace-url=\"/new-path\"");
}

TEST_CASE("htmx::attr::boost — emits hx-boost attribute", "[htmx]") {
    const auto n = htmx::attr::boost();
    CHECK(n.str == " hx-boost=\"true\"");
}

TEST_CASE("htmx::attr::vals — emits hx-vals attribute", "[htmx]") {
    const auto n = htmx::attr::vals(R"({"key":"value"})");
    // JSON quotes are HTML-escaped to &quot; which is valid in HTML attributes
    CHECK(n.str == R"( hx-vals="{&quot;key&quot;:&quot;value&quot;}")");
}

TEST_CASE("htmx::attr::include — emits hx-include attribute", "[htmx]") {
    const auto n = htmx::attr::include("#form");
    CHECK(n.str == " hx-include=\"#form\"");
}

TEST_CASE("htmx::attr::indicator — emits hx-indicator attribute", "[htmx]") {
    const auto n = htmx::attr::indicator(".spinner");
    CHECK(n.str == " hx-indicator=\".spinner\"");
}

// ── HTML Element Integration Tests ────────────────────────────────────────

TEST_CASE("HTMX attributes in button element", "[htmx]") {
    const std::string s =
        std::string(html::button{htmx::attr::post("/api/submit"), htmx::attr::target("#result"), "Submit"});
    CHECK(s == R"(<button hx-post="/api/submit" hx-target="#result">Submit</button>)");
}

TEST_CASE("HTMX attributes in form element", "[htmx]") {
    const std::string s =
        std::string(html::form{htmx::attr::get("/search"), htmx::attr::target("#results"),
                               htmx::attr::swap("outerHTML"), htmx::attr::trigger("keyup changed delay:500ms"),
                               html::input{html::attr("type", "text"), html::attr("name", "q")}});
    CHECK(s.find("hx-get=\"/search\"") != std::string::npos);
    CHECK(s.find("hx-target=\"#results\"") != std::string::npos);
    CHECK(s.find("hx-swap=\"outerHTML\"") != std::string::npos);
    CHECK(s.find("hx-trigger=\"keyup changed delay:500ms\"") != std::string::npos);
}

TEST_CASE("HTMX attributes with HTML escaping", "[htmx]") {
    const std::string s = std::string(html::button{htmx::attr::post("/api/submit?q=<script>"), "Submit"});
    CHECK(s == R"(<button hx-post="/api/submit?q=&lt;script&gt;">Submit</button>)");
}

// ── Request Detection Tests ───────────────────────────────────────────────

TEST_CASE("htmx::is_request — detects HTMX request", "[htmx]") {
    web::Request req;
    req.headers.data["hx-request"] = "true";
    CHECK(htmx::is_request(req) == true);
}

TEST_CASE("htmx::is_request — returns false for non-HTMX request", "[htmx]") {
    web::Request req;
    CHECK(htmx::is_request(req) == false);
}

TEST_CASE("htmx::get_target — reads HX-Target header", "[htmx]") {
    web::Request req;
    req.headers.data["hx-target"] = "#results";
    CHECK(htmx::get_target(req) == "#results");
}

TEST_CASE("htmx::get_target — returns empty string if missing", "[htmx]") {
    web::Request req;
    CHECK(htmx::get_target(req) == "");
}

TEST_CASE("htmx::get_trigger — reads HX-Trigger header", "[htmx]") {
    web::Request req;
    req.headers.data["hx-trigger"] = "search-button";
    CHECK(htmx::get_trigger(req) == "search-button");
}

TEST_CASE("htmx::get_trigger_name — reads HX-Trigger-Name header", "[htmx]") {
    web::Request req;
    req.headers.data["hx-trigger-name"] = "email";
    CHECK(htmx::get_trigger_name(req) == "email");
}

TEST_CASE("htmx::get_current_url — reads HX-Current-URL header", "[htmx]") {
    web::Request req;
    req.headers.data["hx-current-url"] = "http://localhost:8080/page";
    CHECK(htmx::get_current_url(req) == "http://localhost:8080/page");
}

TEST_CASE("htmx::is_boosted — detects hx-boost", "[htmx]") {
    web::Request req;
    req.headers.data["hx-boosted"] = "true";
    CHECK(htmx::is_boosted(req) == true);
}

// ── Response Header Helpers Tests ─────────────────────────────────────────

TEST_CASE("htmx::redirect — sets HX-Redirect header", "[htmx]") {
    web::Response resp;
    htmx::redirect(resp, "/new-path");
    CHECK(resp.headers.get("hx-redirect") == "/new-path");
}

TEST_CASE("htmx::reswap — sets HX-Reswap header", "[htmx]") {
    web::Response resp;
    htmx::reswap(resp, "beforeend");
    CHECK(resp.headers.get("hx-reswap") == "beforeend");
}

TEST_CASE("htmx::retarget — sets HX-Retarget header", "[htmx]") {
    web::Response resp;
    htmx::retarget(resp, "#sidebar");
    CHECK(resp.headers.get("hx-retarget") == "#sidebar");
}

TEST_CASE("htmx::trigger — sets HX-Trigger header", "[htmx]") {
    web::Response resp;
    htmx::trigger(resp, "itemSaved");
    CHECK(resp.headers.get("hx-trigger") == "itemSaved");
}

TEST_CASE("htmx::trigger_with_data — sets HX-Trigger with JSON", "[htmx]") {
    web::Response resp;
    htmx::trigger_with_data(resp, R"({"id":123,"name":"Item"})");
    CHECK(resp.headers.get("hx-trigger") == R"({"id":123,"name":"Item"})");
}

TEST_CASE("htmx::refresh — sets HX-Refresh header", "[htmx]") {
    web::Response resp;
    htmx::refresh(resp);
    CHECK(resp.headers.get("hx-refresh") == "true");
}

TEST_CASE("htmx::push_url — sets HX-Push-Url header", "[htmx]") {
    web::Response resp;
    htmx::push_url(resp, "/items/1");
    CHECK(resp.headers.get("hx-push-url") == "/items/1");
}

TEST_CASE("htmx::replace_url — sets HX-Replace-Url header", "[htmx]") {
    web::Response resp;
    htmx::replace_url(resp, "/new-url");
    CHECK(resp.headers.get("hx-replace-url") == "/new-url");
}

TEST_CASE("HTMX response helpers are chainable", "[htmx]") {
    web::Response resp;
    auto& r = htmx::trigger(resp, "saved");
    CHECK(&r == &resp); // Should return reference to same response
    CHECK(resp.headers.get("hx-trigger") == "saved");
}

// ── Optimistic Update Tests ───────────────────────────────────────────────

TEST_CASE("htmx::js::increment<int> — calls revet.opt.incrInt", "[htmx][optimistic]") {
    static constexpr html::typed_id<int> k{"count"};
    const auto n = htmx::optimistic(k, +1);
    CHECK(n.str.find("revet.opt.incrInt") != std::string::npos);
    CHECK(n.str.find("count") != std::string::npos);
}

TEST_CASE("htmx::js::decrement<int> — calls revet.opt.decrInt", "[htmx][optimistic]") {
    static constexpr html::typed_id<int> k{"count"};
    const auto n = htmx::optimistic(k, -1);
    CHECK(n.str.find("revet.opt.decrInt") != std::string::npos);
    CHECK(n.str.find("count") != std::string::npos);
}

TEST_CASE("htmx::js::increment<double> — calls revet.opt.incrFloat", "[htmx][optimistic]") {
    static constexpr html::typed_id<double> k{"price"};
    const auto n = htmx::optimistic(k, +0.5);
    CHECK(n.str.find("revet.opt.incrFloat") != std::string::npos);
    CHECK(n.str.find("price") != std::string::npos);
}

TEST_CASE("htmx::js::decrement<double> — calls revet.opt.decrFloat", "[htmx][optimistic]") {
    static constexpr html::typed_id<double> k{"price"};
    const auto n = htmx::optimistic(k, -0.5);
    CHECK(n.str.find("revet.opt.decrFloat") != std::string::npos);
    CHECK(n.str.find("price") != std::string::npos);
}

TEST_CASE("htmx::optimistic delta=0 — routes to increment", "[htmx][optimistic]") {
    static constexpr html::typed_id<int> k{"val"};
    const auto n = htmx::optimistic(k, 0);
    CHECK(n.str.find("revet.opt.incrInt") != std::string::npos);
}

TEST_CASE("htmx::optimistic typed_id<bool> — calls revet.opt.toggleClass", "[htmx][optimistic]") {
    static constexpr html::typed_id<bool> k{"like-btn", "active"};
    const auto n = htmx::optimistic(k);
    CHECK(n.str.find("revet.opt.toggleClass") != std::string::npos);
    CHECK(n.str.find("active") != std::string::npos);
    CHECK(n.str.find("like-btn") != std::string::npos);
}

TEST_CASE("htmx::optimistic JsMappable custom op", "[htmx][optimistic]") {
    struct Clamp {
        int max;
        auto to_js(std::string_view id) const -> std::string {
            return std::format("revet.opt.clamp('{}',{})", id, max);
        }
    };
    static constexpr html::typed_id<int> k{"score"};
    const auto n = htmx::optimistic(k, Clamp{.max = 100});
    CHECK(n.str.find("revet.opt.clamp") != std::string::npos);
    CHECK(n.str.find("score") != std::string::npos);
}

TEST_CASE("htmx::optimistic untyped fallback — binds element to e", "[htmx][optimistic]") {
    static constexpr html::id k{"my-el"};
    const auto n = htmx::optimistic(k, "e.classList.toggle('active')");
    CHECK(n.str.find("getElementById") != std::string::npos);
    CHECK(n.str.find("my-el") != std::string::npos);
    CHECK(n.str.find("classList.toggle") != std::string::npos);
}

TEST_CASE("htmx::optimistic — emits hx-on:htmx:before-request attribute", "[htmx][optimistic]") {
    static constexpr html::typed_id<int> k{"counter"};
    const auto n = htmx::optimistic(k, +1);
    CHECK(n.str.find("hx-on:htmx:before-request") != std::string::npos);
}

TEST_CASE("htmx::js::set_text — calls revet.opt.setText", "[htmx][optimistic]") {
    const auto s = htmx::js::set_text{"done"}.to_js("status");
    CHECK(s.find("revet.opt.setText") != std::string::npos);
    CHECK(s.find("status") != std::string::npos);
    CHECK(s.find("done") != std::string::npos);
}

TEST_CASE("htmx::js::add_class / remove_class — call revet.opt.*", "[htmx][optimistic]") {
    CHECK(htmx::js::add_class{"hidden"}.to_js("el").find("revet.opt.addClass") != std::string::npos);
    CHECK(htmx::js::remove_class{"hidden"}.to_js("el").find("revet.opt.removeClass") != std::string::npos);
}

// ── Rollback Tests ────────────────────────────────────────────────────────

TEST_CASE("htmx::optimistic scalar — emits send-error rollback", "[htmx][rollback]") {
    static constexpr html::typed_id<int> k{"score"};
    const auto n = htmx::optimistic(k, +1);
    CHECK(n.str.find("htmx:send-error") != std::string::npos);
    CHECK(n.str.find("revet.opt.restoreText") != std::string::npos);
}

TEST_CASE("htmx::optimistic scalar — rollback calls restoreText", "[htmx][rollback]") {
    static constexpr html::typed_id<int> k{"score"};
    const auto n = htmx::optimistic(k, +1);
    CHECK(n.str.find("revet.opt.incrInt") != std::string::npos);
    CHECK(n.str.find("revet.opt.restoreText") != std::string::npos);
}

TEST_CASE("htmx::optimistic double — rollback calls restoreText", "[htmx][rollback]") {
    static constexpr html::typed_id<double> k{"price"};
    const auto n = htmx::optimistic(k, +1.5);
    CHECK(n.str.find("revet.opt.incrFloat") != std::string::npos);
    CHECK(n.str.find("revet.opt.restoreText") != std::string::npos);
    CHECK(n.str.find("htmx:send-error") != std::string::npos);
}

TEST_CASE("htmx::optimistic bool — rollback calls rollbackClass", "[htmx][rollback]") {
    static constexpr html::typed_id<bool> k{"like-btn", "active"};
    const auto n = htmx::optimistic(k);
    CHECK(n.str.find("htmx:send-error") != std::string::npos);
    CHECK(n.str.find("revet.opt.toggleClass") != std::string::npos);
    CHECK(n.str.find("revet.opt.rollbackClass") != std::string::npos);
}

TEST_CASE("htmx::js::toggle_class — rollback_js calls rollbackClass", "[htmx][rollback]") {
    const auto rb = htmx::js::toggle_class{"active"}.rollback_js("my-btn");
    CHECK(rb.find("revet.opt.rollbackClass") != std::string::npos);
    CHECK(rb.find("my-btn") != std::string::npos);
    CHECK(rb.find("active") != std::string::npos);
}

TEST_CASE("htmx::js::add_class — rollback_js calls removeClass", "[htmx][rollback]") {
    const auto rb = htmx::js::add_class{"selected"}.rollback_js("el");
    CHECK(rb.find("revet.opt.removeClass") != std::string::npos);
    CHECK(rb.find("selected") != std::string::npos);
}

TEST_CASE("htmx::js::remove_class — rollback_js calls addClass", "[htmx][rollback]") {
    const auto rb = htmx::js::remove_class{"hidden"}.rollback_js("el");
    CHECK(rb.find("revet.opt.addClass") != std::string::npos);
    CHECK(rb.find("hidden") != std::string::npos);
}

TEST_CASE("htmx::optimistic JsMappable without rollback_js — no send-error attr", "[htmx][rollback]") {
    struct NoRollback {
        auto to_js(std::string_view id) const -> std::string {
            return std::format("revet.opt.custom('{}')", id);
        }
    };
    static constexpr html::typed_id<int> k{"el"};
    const auto n = htmx::optimistic(k, NoRollback{});
    CHECK(n.str.find("htmx:send-error") == std::string::npos);
    CHECK(n.str.find("htmx:before-request") != std::string::npos);
}

// ── Container Optimistic Tests ────────────────────────────────────────────

TEST_CASE("htmx::optimistic_append — emits before-request and send-error", "[htmx][container]") {
    static constexpr html::id k_feed{"msg-feed"};
    const auto n = htmx::optimistic_append(k_feed, html::div{"Hello"});
    CHECK(n.str.find("htmx:before-request") != std::string::npos);
    CHECK(n.str.find("htmx:send-error") != std::string::npos);
}

TEST_CASE("htmx::optimistic_append — calls revet.opt.appendPending", "[htmx][container]") {
    static constexpr html::id k_feed{"msg-feed"};
    const auto n = htmx::optimistic_append(k_feed, html::div{"Hello"});
    CHECK(n.str.find("revet.opt.appendPending") != std::string::npos);
    CHECK(n.str.find("msg-feed") != std::string::npos);
}

TEST_CASE("htmx::optimistic_append — rollback calls revet.opt.removePending", "[htmx][container]") {
    static constexpr html::id k_feed{"msg-feed"};
    const auto n = htmx::optimistic_append(k_feed, html::div{"Hello"});
    CHECK(n.str.find("revet.opt.removePending") != std::string::npos);
    CHECK(n.str.find("htmx:send-error") != std::string::npos);
}

TEST_CASE("htmx::optimistic_prepend — calls revet.opt.prependPending", "[htmx][container]") {
    static constexpr html::id k_feed{"msg-feed"};
    const auto n = htmx::optimistic_prepend(k_feed, html::div{"Hello"});
    CHECK(n.str.find("revet.opt.prependPending") != std::string::npos);
    CHECK(n.str.find("msg-feed") != std::string::npos);
    CHECK(n.str.find("htmx:send-error") != std::string::npos);
}

#include <revet/web.hpp>
#include <chrono>
#include <string>
#include <thread>

static constexpr std::string_view ROW_STYLE =
    "display:flex;justify-content:flex-end;margin:.3rem 0;animation:fadeIn .2s";
static constexpr std::string_view BUBBLE_STYLE =
    "max-width:72%;background:var(--accent);color:var(--accent-fg);"
    "padding:.5rem .875rem;border-radius:1rem 1rem .25rem 1rem;"
    "font-size:.875rem;line-height:1.4";
static constexpr std::string_view STAMP_STYLE =
    "display:block;font-size:.6rem;margin-top:.25rem;opacity:.7";

static auto render_post(const auto& req) -> web::Response {
    const auto text = std::string(req.form.get("text"));
    if (text.empty()) return web::to_response(html::Node{});

    // Simulate server processing latency so optimistic update is visible
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    return web::to_response(html::div{
        html::attr.style(ROW_STYLE),
        html::div{
            html::attr.style(BUBBLE_STYLE),
            text,
            html::span{html::attr.style(STAMP_STYLE), "✓ sent"},
        },
    });
}

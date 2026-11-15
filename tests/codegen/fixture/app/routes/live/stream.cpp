#include <revet/web.hpp>
#include <chrono>
#include <format>
#include <random>
#include <string_view>
#include <thread>

static constexpr std::string_view SWATCH_WRAP =
    "animation:sw-in .3s;display:flex;flex-direction:column;align-items:center;gap:6px";
static constexpr std::string_view SWATCH_CHIP =
    ";width:72px;height:72px;border-radius:8px;border:1px solid rgba(128,128,128,.15)";
static constexpr std::string_view SWATCH_LABEL =
    "font-size:.6rem;color:var(--muted);font-family:monospace";

static void render_sse(const web::Request& /*req*/, web::SSEWriter& sse) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<unsigned int> dist(0, 0xFFFFFFU);

    while (sse.alive()) {
        unsigned int val = dist(rng);
        auto hex = std::format("#{:06X}", val);

        auto swatch = html::div{
            html::attr.style(SWATCH_WRAP),
            html::div{html::attr.style(std::format("background:{}{}", hex, SWATCH_CHIP))},
            html::code{html::attr.style(SWATCH_LABEL), hex},
        }.str;
        sse.send(swatch, "color");
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }
}

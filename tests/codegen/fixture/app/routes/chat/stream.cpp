#include <revet/web.hpp>
#include <chrono>
#include <random>
#include <string_view>
#include <thread>

static constexpr std::array FRAGMENTS = {
    "Lorem ipsum dolor sit amet.",
    "Consectetur adipiscing elit, sed do eiusmod.",
    "Ut labore et dolore magna aliqua.",
    "Duis aute irure dolor in reprehenderit.",
    "Excepteur sint occaecat cupidatat non proident.",
    "Sunt in culpa qui officia deserunt mollit.",
    "Voluptatem accusantium doloremque laudantium.",
    "Nemo enim ipsam voluptatem quia voluptas sit.",
    "Quis nostrum exercitationem ullam corporis suscipit.",
    "Ut perspiciatis unde omnis iste natus error sit.",
    "Nam libero tempore cum soluta nobis eligendi.",
    "Temporibus autem quibusdam et aut officiis.",
};

static constexpr std::string_view ROW_STYLE =
    "display:flex;align-items:flex-start;gap:.5rem;margin:.3rem 0;animation:fadeIn .2s";
static constexpr std::string_view AVATAR_STYLE =
    "display:flex;width:1.75rem;height:1.75rem;flex-shrink:0;"
    "justify-content:center;align-items:center;border-radius:50%;"
    "background:var(--muted);color:var(--surface);font-size:.7rem;font-weight:700";
static constexpr std::string_view BUBBLE_STYLE =
    "max-width:72%;padding:.5rem .875rem;border:1px solid var(--border);"
    "border-radius:.25rem 1rem 1rem 1rem;background:var(--surface);"
    "font-size:.875rem;line-height:1.4";

static void render_sse(const web::Request& /*req*/, web::SSEWriter& sse) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, FRAGMENTS.size() - 1);
    std::uniform_int_distribution<int>    delay_ms(2000, 4500);

    while (sse.alive()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms(rng)));
        if (!sse.alive()) break;

        auto text = std::string(FRAGMENTS[pick(rng)]);
        auto msg = html::div{
            html::attr.style(ROW_STYLE),
            html::div{html::attr.style(AVATAR_STYLE), "A"},
            html::div{html::attr.style(BUBBLE_STYLE), text},
        }.str;
        sse.send(msg, "chat");
    }
}

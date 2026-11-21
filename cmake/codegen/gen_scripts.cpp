// Embeds app/scripts/*.js into scripts.hpp. gen_routes serves each at a content-hashed URL.
// Called from gen_routes, which also serves each script (routes.cpp includes scripts.hpp).
//
// For app/scripts/chart.js the header has:
//   scripts::chart      — "/_app/chart.<hash>.js", safe to cache forever
//   scripts::chart_js   — the source
//   scripts::chart_tag()— <script src=... defer>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>

#include "common.hpp"

namespace revet::codegen {
namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-signed-bitwise,hicpp-signed-bitwise)
// Minimal SHA-256 (FIPS 180-4). Fixed-size blocks: every index is bounded by its loop. Only used for cache-busting
// URLs, so it must match the hash the Python generator produced: hashlib.sha256(src).hexdigest()[:10].
std::string sha256_hex(std::string_view data) {
    static constexpr std::array<std::uint32_t, 64> K = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    std::string msg(data);
    std::uint64_t bit_len = static_cast<std::uint64_t>(msg.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back('\0');
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xffU));
    }

    auto rotr = [](std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + off + (i * 4));
            w[i] = (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
                   (static_cast<std::uint32_t>(p[2]) << 8U) | p[3];
        }
        for (std::size_t i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3U);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = h;
        for (std::size_t i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            std::uint32_t t1 = v[7] + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            std::uint32_t t2 = S0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (std::size_t i = 0; i < 8; ++i) {
            h[i] += v[i];
        }
    }

    static constexpr char k_hex[] = "0123456789abcdef";
    std::string out;
    for (std::uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(k_hex[(word >> shift) & 0xfU]);
        }
    }
    return out;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-signed-bitwise,hicpp-signed-bitwise)

struct AppScript {
    std::string ident; // C++ identifier
    std::string url;   // /_app/<stem>.<hash>.js
    std::string src;
};

// Python \W → _, and a leading digit gets a leading underscore.
std::string ident_of(const std::string& stem) {
    std::string name = stem;
    for (char& c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) != 0) && c != '_') {
            c = '_';
        }
    }
    if (!name.empty() && (std::isdigit(static_cast<unsigned char>(name[0])) != 0)) {
        name = "_" + name;
    }
    return name;
}

std::vector<AppScript> scan_scripts(const Ctx& ctx) {
    std::vector<AppScript> out;
    fs::path dir = ctx.proj / "app" / "scripts";
    if (!fs::is_directory(dir)) {
        return out;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".js") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });

    for (const auto& f : files) {
        std::string src = read_text(f);
        std::string stem = f.stem().string();
        std::string digest = sha256_hex(src).substr(0, 10);
        out.push_back({ident_of(stem), cat("/_app/", stem, ".", digest, ".js"), src});
    }
    return out;
}

} // namespace

// Used by gen_routes to list app scripts as routes.
std::vector<std::pair<std::string, std::string>> app_script_routes(const Ctx& ctx) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& s : scan_scripts(ctx)) {
        out.emplace_back(s.url, s.ident);
    }
    return out;
}

int gen_scripts(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "scripts.hpp");
    Lines lines = file_header({"#include <revet/html.hpp>", "#include <string_view>"});
    lines.push_back("namespace scripts {");
    for (const auto& s : scan_scripts(ctx)) {
        lines.push_back("inline constexpr std::string_view " + s.ident + " = \"" + s.url + "\";");
        lines.push_back("inline constexpr std::string_view " + s.ident + "_js = \"" + escape_str(s.src) + "\";");
        lines.push_back("inline auto " + s.ident + "_tag() -> html::Node {");
        lines.push_back("    return html::script{html::attr.src(" + s.ident + "), html::attr(\"defer\", \"\")};");
        lines.push_back("}");
    }
    lines.push_back("} // namespace scripts");
    lines.push_back("");
    write_lines(out_path, lines);
    return 0;
}

} // namespace revet::codegen

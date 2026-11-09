#pragma once
// Hashes libsodium doesn't have, for interop only: SHA-1 (TOTP, legacy APIs) and MD5 (checksums,
// Rails/Postgres md5()). Neither is safe for passwords or signatures; use crypto.hpp for those.

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace web::hash {

using Sha1Digest = std::array<std::uint8_t, 20>;

inline auto sha1(std::string_view message) -> Sha1Digest {
    std::array<std::uint32_t, 5> h = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m(message);
    const std::uint64_t bits = static_cast<std::uint64_t>(message.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) {
        m += '\0';
    }
    for (int i = 7; i >= 0; --i) {
        m += static_cast<char>((bits >> (static_cast<unsigned>(i) * 8U)) & 0xFFU);
    }
    const auto rotl = [](std::uint32_t v, unsigned n) { return (v << n) | (v >> (32U - n)); };
    for (std::size_t chunk = 0; chunk < m.size(); chunk += 64) {
        std::array<std::uint32_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto byte = [&](std::size_t k) { return static_cast<std::uint32_t>(static_cast<std::uint8_t>(m[chunk + (i * 4) + k])); };
            w[i] = (byte(0) << 24U) | (byte(1) << 16U) | (byte(2) << 8U) | byte(3);
        }
        for (std::size_t i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        auto [a, b, c, d, e] = h;
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const std::uint32_t next = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = next;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<std::uint8_t, 20> out{};
    for (std::size_t i = 0; i < 5; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            out[(i * 4) + j] = static_cast<std::uint8_t>((h[i] >> (24U - (static_cast<unsigned>(j) * 8U))) & 0xFFU);
        }
    }
    return out;
}

inline auto hmac_sha1(std::string key, std::string_view message) -> Sha1Digest {
    if (key.size() > 64) {
        const auto hashed = sha1(key);
        key.assign(reinterpret_cast<const char*>(hashed.data()), hashed.size());
    }
    key.resize(64, '\0');
    std::string inner(64, '\0');
    std::string outer(64, '\0');
    for (std::size_t i = 0; i < 64; ++i) {
        inner[i] = static_cast<char>(key[i] ^ 0x36);
        outer[i] = static_cast<char>(key[i] ^ 0x5C);
    }
    const auto inner_hash = sha1(inner.append(message));
    return sha1(outer.append(reinterpret_cast<const char*>(inner_hash.data()), inner_hash.size()));
}

// Lowercase hex MD5, as Postgres md5() prints it.
inline auto md5_hex(std::string_view text) -> std::string {
    static constexpr std::array<std::uint32_t, 64> k = [] {
        std::array<std::uint32_t, 64> t{};
        constexpr std::array<std::uint32_t, 64> v = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122,
            0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6,
            0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60,
            0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
            0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        for (std::size_t i = 0; i < 64; ++i) {
            t[i] = v[i];
        }
        return t;
    }();
    static constexpr std::array<std::uint32_t, 16> shifts = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
    std::string msg(text);
    const std::uint64_t bits = static_cast<std::uint64_t>(msg.size()) * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56) {
        msg += '\0';
    }
    for (int i = 0; i < 8; ++i) {
        msg += static_cast<char>((bits >> (8U * static_cast<unsigned>(i))) & 0xFFU);
    }
    std::uint32_t a0 = 0x67452301;
    std::uint32_t b0 = 0xefcdab89;
    std::uint32_t c0 = 0x98badcfe;
    std::uint32_t d0 = 0x10325476;
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::array<std::uint32_t, 16> m{};
        for (std::size_t i = 0; i < 16; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                m[i] |= static_cast<std::uint32_t>(static_cast<unsigned char>(msg[off + (i * 4) + j])) << (8U * j);
            }
        }
        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;
        for (std::uint32_t i = 0; i < 64; ++i) {
            std::uint32_t f = 0;
            std::uint32_t g = 0;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = ((5 * i) + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = ((3 * i) + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            f = f + a + k[i] + m[g];
            a = d;
            d = c;
            c = b;
            const std::uint32_t s = shifts[((i / 16) * 4) + (i % 4)];
            b = b + ((f << s) | (f >> (32 - s)));
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }
    std::string out;
    for (const std::uint32_t v : {a0, b0, c0, d0}) {
        for (unsigned j = 0; j < 4; ++j) {
            out += std::format("{:02x}", (v >> (8U * j)) & 0xFFU);
        }
    }
    return out;
}

template<std::size_t N>
inline auto hex(const std::array<std::uint8_t, N>& bytes) -> std::string {
    std::string out;
    out.reserve(N * 2);
    for (const auto b : bytes) {
        out += std::format("{:02x}", b);
    }
    return out;
}

inline auto sha1_hex(std::string_view message) -> std::string {
    return hex(sha1(message));
}

} // namespace web::hash

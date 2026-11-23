#pragma once

// A small, strict CBOR (RFC 8949) decoder for the subset WebAuthn uses: integers, byte and text
// strings, arrays, maps, booleans and null, all definite length. Anything else (tags, floats,
// indefinite lengths, duplicate map keys, nesting deeper than max_depth, more than max_items entries
// in one array or map) is an error, never a crash.
//
//   auto v = web::cbor::decode(bytes);                 // whole input must be one item
//   auto [v, used] = web::cbor::decode_prefix(bytes);  // one item, then whatever follows
//   const auto* fmt = v->find("fmt");                  // map lookup by text key
//   const auto* x = key->find(-2);                     // ... or by integer key

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace web::cbor {

enum class Error {
    truncated,       // input ended inside an item
    trailing_bytes,  // decode(): bytes left over after the item
    unsupported,     // tags, floats, indefinite length, simple values other than false/true/null
    too_deep,        // nested more than max_depth levels
    too_large,       // an integer outside int64, a count larger than the input can hold, or above max_items
    duplicate_key,   // the same key twice in one map
    bad_text,        // text string that is not valid UTF-8
};

struct Bytes {
    std::string data;
};
struct Text {
    std::string data;
};
struct Null {};

struct Value;
struct Entry;

struct Value {
    // Integer, byte string, text string, array, map, bool, null.
    std::variant<std::int64_t, Bytes, Text, std::vector<Value>, std::vector<Entry>, bool, Null> v;

    [[nodiscard]] auto is_int() const -> bool { return std::holds_alternative<std::int64_t>(v); }
    [[nodiscard]] auto is_bytes() const -> bool { return std::holds_alternative<Bytes>(v); }
    [[nodiscard]] auto is_text() const -> bool { return std::holds_alternative<Text>(v); }
    [[nodiscard]] auto is_array() const -> bool { return std::holds_alternative<std::vector<Value>>(v); }
    [[nodiscard]] auto is_map() const -> bool { return std::holds_alternative<std::vector<Entry>>(v); }

    [[nodiscard]] auto as_int() const -> const std::int64_t* { return std::get_if<std::int64_t>(&v); }
    [[nodiscard]] auto as_bytes() const -> const std::string* {
        const auto* b = std::get_if<Bytes>(&v);
        return b != nullptr ? &b->data : nullptr;
    }
    [[nodiscard]] auto as_text() const -> const std::string* {
        const auto* t = std::get_if<Text>(&v);
        return t != nullptr ? &t->data : nullptr;
    }
    [[nodiscard]] auto as_array() const -> const std::vector<Value>* { return std::get_if<std::vector<Value>>(&v); }
    [[nodiscard]] auto as_map() const -> const std::vector<Entry>* { return std::get_if<std::vector<Entry>>(&v); }

    // Map lookups: the value under a text or integer key, or nullptr (also when this is not a map).
    [[nodiscard]] inline auto find(std::string_view key) const -> const Value*;
    [[nodiscard]] inline auto find(std::int64_t key) const -> const Value*;
};

struct Entry {
    Value key;
    Value value;
};

inline auto Value::find(std::string_view key) const -> const Value* {
    if (const auto* m = as_map()) {
        for (const auto& e : *m) {
            if (const auto* t = e.key.as_text(); t != nullptr && *t == key) return &e.value;
        }
    }
    return nullptr;
}

inline auto Value::find(std::int64_t key) const -> const Value* {
    if (const auto* m = as_map()) {
        for (const auto& e : *m) {
            if (const auto* i = e.key.as_int(); i != nullptr && *i == key) return &e.value;
        }
    }
    return nullptr;
}

inline constexpr int max_depth = 16;
// WebAuthn's arrays and maps hold a handful of entries. The cap keeps the duplicate-key check (quadratic
// in the map size) and the allocations small whatever an attacker sends.
inline constexpr std::uint64_t max_items = 256;

namespace detail {

inline auto same_key(const Value& a, const Value& b) -> bool {
    if (const auto* x = a.as_int()) {
        const auto* y = b.as_int();
        return y != nullptr && *x == *y;
    }
    if (const auto* x = a.as_text()) {
        const auto* y = b.as_text();
        return y != nullptr && *x == *y;
    }
    if (const auto* x = a.as_bytes()) {
        const auto* y = b.as_bytes();
        return y != nullptr && *x == *y;
    }
    return false; // arrays, maps and simple values as keys are never considered equal
}

inline auto valid_utf8(std::string_view s) -> bool {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1FU; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0FU; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07U; }
        else return false;
        if (i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0U) != 0x80U) return false;
            cp = (cp << 6) | (cc & 0x3FU);
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += len;
    }
    return true;
}

class Reader {
public:
    explicit Reader(std::string_view in) : in_(in) {}

    [[nodiscard]] auto consumed() const -> std::size_t { return pos_; }

    auto item(int depth) -> std::expected<Value, Error> {
        if (depth > max_depth) return std::unexpected(Error::too_deep);
        if (pos_ >= in_.size()) return std::unexpected(Error::truncated);
        const auto initial = static_cast<unsigned char>(in_[pos_++]);
        const unsigned major = initial >> 5U;
        const unsigned info = initial & 0x1FU;

        if (major == 7) {
            if (info == 20) return Value{false};
            if (info == 21) return Value{true};
            if (info == 22) return Value{Null{}};
            return std::unexpected(Error::unsupported);
        }
        if (major == 6) return std::unexpected(Error::unsupported); // tags

        auto arg = argument(info);
        if (!arg) return std::unexpected(arg.error());
        const std::uint64_t n = *arg;

        switch (major) {
        case 0:
            if (n > static_cast<std::uint64_t>(INT64_MAX)) return std::unexpected(Error::too_large);
            return Value{static_cast<std::int64_t>(n)};
        case 1:
            if (n > static_cast<std::uint64_t>(INT64_MAX)) return std::unexpected(Error::too_large);
            return Value{-1 - static_cast<std::int64_t>(n)};
        case 2:
        case 3: {
            if (n > in_.size() - pos_) return std::unexpected(Error::truncated);
            std::string data(in_.substr(pos_, static_cast<std::size_t>(n)));
            pos_ += static_cast<std::size_t>(n);
            if (major == 2) return Value{Bytes{std::move(data)}};
            if (!valid_utf8(data)) return std::unexpected(Error::bad_text);
            return Value{Text{std::move(data)}};
        }
        case 4: {
            if (n > max_items || n > in_.size() - pos_) return std::unexpected(Error::too_large); // every item takes a byte
            std::vector<Value> items;
            items.reserve(static_cast<std::size_t>(n));
            for (std::uint64_t i = 0; i < n; ++i) {
                auto v = item(depth + 1);
                if (!v) return std::unexpected(v.error());
                items.push_back(std::move(*v));
            }
            return Value{std::move(items)};
        }
        default: { // 5: map
            if (n > max_items || n > (in_.size() - pos_) / 2) return std::unexpected(Error::too_large); // a pair takes two bytes
            std::vector<Entry> entries;
            entries.reserve(static_cast<std::size_t>(n));
            for (std::uint64_t i = 0; i < n; ++i) {
                auto k = item(depth + 1);
                if (!k) return std::unexpected(k.error());
                auto v = item(depth + 1);
                if (!v) return std::unexpected(v.error());
                for (const auto& e : entries) {
                    if (same_key(e.key, *k)) return std::unexpected(Error::duplicate_key);
                }
                entries.push_back({std::move(*k), std::move(*v)});
            }
            return Value{std::move(entries)};
        }
        }
    }

private:
    auto argument(unsigned info) -> std::expected<std::uint64_t, Error> {
        if (info < 24) return info;
        std::size_t width = 0;
        if (info == 24) width = 1;
        else if (info == 25) width = 2;
        else if (info == 26) width = 4;
        else if (info == 27) width = 8;
        else return std::unexpected(Error::unsupported); // 28-30 reserved, 31 indefinite length
        if (width > in_.size() - pos_) return std::unexpected(Error::truncated);
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < width; ++i) {
            v = (v << 8U) | static_cast<unsigned char>(in_[pos_++]);
        }
        return v;
    }

    std::string_view in_;
    std::size_t pos_ = 0;
};

} // namespace detail

struct Decoded {
    Value value;
    std::size_t consumed = 0;
};

// One item from the front of `in`; reports how many bytes it used.
[[nodiscard]] inline auto decode_prefix(std::string_view in) -> std::expected<Decoded, Error> {
    detail::Reader r(in);
    auto v = r.item(0);
    if (!v) return std::unexpected(v.error());
    return Decoded{std::move(*v), r.consumed()};
}

// The whole input must be exactly one item.
[[nodiscard]] inline auto decode(std::string_view in) -> std::expected<Value, Error> {
    auto d = decode_prefix(in);
    if (!d) return std::unexpected(d.error());
    if (d->consumed != in.size()) return std::unexpected(Error::trailing_bytes);
    return std::move(d->value);
}

} // namespace web::cbor

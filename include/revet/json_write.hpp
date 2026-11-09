#pragma once
// Writing JSON. json::quote escapes one string; json::Writer builds a document and places the commas.
//   json::Writer w;
//   w.object().key("id").value(7).key("tags").array().value("a").value("b").end().end();
//   w.str() == R"({"id":7,"tags":["a","b"]})"

#include <cmath>
#include <concepts>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace revet::json {

inline void append_quoted(std::string& out, std::string_view s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

// "a\"b" -> "\"a\\\"b\"" (with the surrounding quotes)
inline auto quote(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 2);
    append_quoted(out, s);
    return out;
}

// Shortest round-trip text for a number; NaN and infinity become null (JSON has neither).
inline auto number(double v) -> std::string {
    return std::isfinite(v) ? std::format("{}", v) : "null";
}

class Writer {
public:
    auto object() -> Writer& {
        before_value();
        out_ += '{';
        stack_.push_back({.object = true});
        return *this;
    }
    auto array() -> Writer& {
        before_value();
        out_ += '[';
        stack_.push_back({.object = false});
        return *this;
    }
    // Closes the innermost object or array.
    auto end() -> Writer& {
        if (stack_.empty()) {
            throw std::logic_error("json::Writer: end() with nothing open");
        }
        out_ += stack_.back().object ? '}' : ']';
        stack_.pop_back();
        return *this;
    }
    auto key(std::string_view k) -> Writer& {
        if (stack_.empty() || !stack_.back().object || stack_.back().after_key) {
            throw std::logic_error("json::Writer: key() outside an object");
        }
        if (stack_.back().count++ > 0) {
            out_ += ',';
        }
        append_quoted(out_, k);
        out_ += ':';
        stack_.back().after_key = true;
        return *this;
    }

    auto value(std::string_view s) -> Writer& {
        before_value();
        append_quoted(out_, s);
        return *this;
    }
    auto value(const char* s) -> Writer& {
        return value(std::string_view(s));
    }
    auto value(const std::string& s) -> Writer& {
        return value(std::string_view(s));
    }
    auto value(bool b) -> Writer& {
        return raw(b ? "true" : "false");
    }
    template<std::integral T>
        requires(!std::is_same_v<T, bool>)
    auto value(T v) -> Writer& {
        return raw(std::format("{}", v));
    }
    template<std::floating_point T>
    auto value(T v) -> Writer& {
        return raw(number(static_cast<double>(v)));
    }
    auto value(std::nullopt_t) -> Writer& {
        return raw("null");
    }
    template<typename T>
    auto value(const std::optional<T>& v) -> Writer& {
        return v ? value(*v) : raw("null");
    }
    // Inserts already-serialized JSON as one value.
    auto raw(std::string_view json) -> Writer& {
        before_value();
        out_ += json;
        return *this;
    }
    // key(k).value(v) in one call.
    template<typename T>
    auto field(std::string_view k, const T& v) -> Writer& {
        return key(k).value(v);
    }

    [[nodiscard]] auto str() const& -> const std::string& {
        return out_;
    }
    [[nodiscard]] auto str() && -> std::string {
        return std::move(out_);
    }

private:
    struct Level {
        bool object;
        int count = 0;
        bool after_key = false;
    };

    void before_value() {
        if (stack_.empty()) {
            if (!out_.empty()) {
                throw std::logic_error("json::Writer: more than one top-level value");
            }
            return;
        }
        auto& top = stack_.back();
        if (top.object) {
            if (!top.after_key) {
                throw std::logic_error("json::Writer: value in an object needs a key() first");
            }
            top.after_key = false;
        } else if (top.count++ > 0) {
            out_ += ',';
        }
    }

    std::string out_;
    std::vector<Level> stack_;
};

} // namespace revet::json

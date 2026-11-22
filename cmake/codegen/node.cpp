#include "node.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

namespace revet::codegen {

namespace {

// First position at or after `from` whose character is not in `set`, or t.size().
auto skip_set(std::string_view t, size_t from, std::string_view set) -> size_t {
    return std::min(t.find_first_not_of(set, from), t.size());
}

// First position at or after `from` whose character is in `set`, or t.size().
auto skip_to(std::string_view t, size_t from, std::string_view set) -> size_t {
    return std::min(t.find_first_of(set, from), t.size());
}

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what);
}

// "\\u" plus four lowercase hex digits.
void append_u_escape(std::string& out, unsigned v) {
    out += std::format("\\u{:04x}", v & 0xFFFFU);
}

void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0U | (cp >> 6U));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0U | (cp >> 12U));
        out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    } else {
        out += static_cast<char>(0xF0U | (cp >> 18U));
        out += static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
        out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (cp & 0x3FU));
    }
}

// ── JSON parsing ────────────────────────────────────────────────────────────

class JsonParser {
public:
    explicit JsonParser(std::string_view t) : t_(t) {}

    auto document() -> Node {
        Node n = value();
        ws();
        if (p_ != t_.size()) {
            fail("json: extra data");
        }
        return n;
    }

private:
    std::string_view t_;
    size_t p_ = 0;

    void ws() {
        p_ = skip_set(t_, p_, " \t\n\r");
    }

    auto peek() -> char {
        if (p_ >= t_.size()) {
            fail("json: unexpected end");
        }
        return t_.at(p_);
    }

    void expect_word(std::string_view w) {
        if (t_.substr(p_, w.size()) != w) {
            fail("json: bad literal");
        }
        p_ += w.size();
    }

    auto hex4() -> unsigned {
        if (p_ + 4 > t_.size()) {
            fail("json: bad \\u escape");
        }
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_.at(p_++);
            v <<= 4U;
            if (c >= '0' && c <= '9') {
                v |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                fail("json: bad hex digit");
            }
        }
        return v;
    }

    auto string_body() -> std::string {
        // Opening quote already consumed.
        std::string out;
        while (true) {
            const char c = peek();
            ++p_;
            if (c == '"') {
                break;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            const char e = peek();
            ++p_;
            switch (e) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && t_.substr(p_, 2) == "\\u") {
                        p_ += 2;
                        const unsigned lo = hex4();
                        cp = 0x10000U + ((cp - 0xD800U) << 10U) + (lo - 0xDC00U);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    fail("json: bad escape");
            }
        }
        return out;
    }

    auto value() -> Node {
        ws();
        Node n;
        const char c = peek();
        if (c == '{') {
            ++p_;
            n.kind = Node::Kind::Table;
            ws();
            if (peek() == '}') {
                ++p_;
                return n;
            }
            while (true) {
                ws();
                if (peek() != '"') {
                    fail("json: expected key");
                }
                ++p_;
                std::string key = string_body();
                ws();
                if (peek() != ':') {
                    fail("json: expected ':'");
                }
                ++p_;
                Node v = value();
                n.keys.push_back(std::move(key));
                n.vals.push_back(std::move(v));
                ws();
                const char d = peek();
                ++p_;
                if (d == '}') {
                    break;
                }
                if (d != ',') {
                    fail("json: expected ',' or '}'");
                }
            }
        } else if (c == '[') {
            ++p_;
            n.kind = Node::Kind::Array;
            ws();
            if (peek() == ']') {
                ++p_;
                return n;
            }
            while (true) {
                n.items.push_back(value());
                ws();
                const char d = peek();
                ++p_;
                if (d == ']') {
                    break;
                }
                if (d != ',') {
                    fail("json: expected ',' or ']'");
                }
            }
        } else if (c == '"') {
            ++p_;
            n.kind = Node::Kind::String;
            n.s = string_body();
        } else if (c == 't') {
            expect_word("true");
            n.kind = Node::Kind::Bool;
            n.b = true;
        } else if (c == 'f') {
            expect_word("false");
            n.kind = Node::Kind::Bool;
        } else if (c == 'n') {
            expect_word("null");
        } else {
            const size_t start = p_;
            p_ = skip_set(t_, p_, "-+.eE0123456789");
            if (start == p_) {
                fail("json: unexpected character");
            }
            n.kind = Node::Kind::Number;
            n.s = std::string(t_.substr(start, p_ - start));
        }
        return n;
    }
};

// ── JSON output ─────────────────────────────────────────────────────────────

void dump_string(std::string& out, const std::string& s) {
    out += '"';
    size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s.at(i));
        unsigned cp = c;
        size_t len = 1;
        if (c >= 0x80) {
            if ((c & 0xE0U) == 0xC0) {
                len = 2;
                cp = c & 0x1FU;
            } else if ((c & 0xF0U) == 0xE0) {
                len = 3;
                cp = c & 0x0FU;
            } else {
                len = 4;
                cp = c & 0x07U;
            }
            for (size_t k = 1; k < len && i + k < s.size(); ++k) {
                cp = (cp << 6U) | (static_cast<unsigned char>(s.at(i + k)) & 0x3FU);
            }
        }
        i += len;
        switch (cp) {
            case '"':
                out += "\\\"";
                continue;
            case '\\':
                out += "\\\\";
                continue;
            case '\n':
                out += "\\n";
                continue;
            case '\r':
                out += "\\r";
                continue;
            case '\t':
                out += "\\t";
                continue;
            case '\b':
                out += "\\b";
                continue;
            case '\f':
                out += "\\f";
                continue;
            default:
                break;
        }
        if (cp >= 0x20 && cp <= 0x7E) {
            out += static_cast<char>(cp);
        } else if (cp >= 0x10000) {
            const unsigned v = cp - 0x10000;
            append_u_escape(out, 0xD800U + (v >> 10U));
            append_u_escape(out, 0xDC00U + (v & 0x3FFU));
        } else {
            append_u_escape(out, cp);
        }
    }
    out += '"';
}

// Dumped items separated by ",\n".
auto join_dumped(auto&& items) -> std::string {
    return std::ranges::fold_left(items, std::string(), [](std::string acc, const std::string& item) -> std::string {
        if (!acc.empty()) {
            acc += ",\n";
        }
        acc += item;
        return acc;
    });
}

void dump_value(std::string& out, const Node& n, int level) {
    const std::string pad(static_cast<size_t>(level + 1) * 2, ' ');
    const std::string close_pad(static_cast<size_t>(level) * 2, ' ');
    switch (n.kind) {
        case Node::Kind::Null:
            out += "null";
            break;
        case Node::Kind::Bool:
            out += n.b ? "true" : "false";
            break;
        case Node::Kind::Number:
            out += n.s;
            break;
        case Node::Kind::String:
            dump_string(out, n.s);
            break;
        case Node::Kind::Array:
            if (n.items.empty()) {
                out += "[]";
                break;
            }
            out += "[\n";
            out += join_dumped(n.items | std::views::transform([&](const Node& item) -> std::string {
                                   std::string s = pad;
                                   dump_value(s, item, level + 1);
                                   return s;
                               }));
            out += "\n" + close_pad + "]";
            break;
        case Node::Kind::Table:
            if (n.keys.empty()) {
                out += "{}";
                break;
            }
            out += "{\n";
            out +=
                join_dumped(std::views::zip(n.keys, n.vals) | std::views::transform([&](const auto& kv) -> std::string {
                                std::string s = pad;
                                dump_string(s, std::get<0>(kv));
                                s += ": ";
                                dump_value(s, std::get<1>(kv), level + 1);
                                return s;
                            }));
            out += "\n" + close_pad + "}";
            break;
    }
}

// ── TOML parsing (subset) ───────────────────────────────────────────────────

class TomlParser {
public:
    explicit TomlParser(std::string_view t) : t_(t) {}

    auto document() -> Node {
        Node root;
        root.kind = Node::Kind::Table;
        std::vector<std::string> section;
        while (true) {
            skip_blank();
            if (p_ >= t_.size()) {
                break;
            }
            if (t_.at(p_) == '[') {
                ++p_;
                if (peek() == '[') {
                    fail("toml: array-of-tables is not supported");
                }
                section = dotted_key();
                skip_inline_ws();
                expect(']');
                table_at(root, section);
            } else {
                std::vector<std::string> path = dotted_key();
                skip_inline_ws();
                expect('=');
                skip_inline_ws();
                Node v = value();
                const std::string last = path.back();
                path.pop_back();
                Node& owner = table_at(root, section);
                Node& target = table_at(owner, path);
                target.set(last, std::move(v));
            }
            skip_inline_ws();
            if (p_ < t_.size() && t_.at(p_) == '#') {
                skip_comment();
            }
            if (p_ < t_.size() && t_.at(p_) != '\n' && t_.at(p_) != '\r') {
                fail("toml: expected end of line");
            }
        }
        return root;
    }

private:
    std::string_view t_;
    size_t p_ = 0;

    [[noreturn]] void bad(const char* what) const {
        fail(std::string("toml: ") + what + " at byte " + std::to_string(p_));
    }

    auto peek() -> char {
        if (p_ >= t_.size()) {
            bad("unexpected end");
        }
        return t_.at(p_);
    }

    void expect(char c) {
        if (peek() != c) {
            bad("unexpected character");
        }
        ++p_;
    }

    void skip_inline_ws() {
        p_ = skip_set(t_, p_, " \t");
    }

    void skip_comment() {
        p_ = skip_to(t_, p_, "\n");
    }

    // Whitespace, newlines and comments between statements and inside arrays.
    void skip_blank() {
        for (p_ = skip_set(t_, p_, " \t\n\r"); p_ < t_.size() && t_.at(p_) == '#'; p_ = skip_set(t_, p_, " \t\n\r")) {
            skip_comment();
        }
    }

    static constexpr std::string_view k_bare_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

    auto simple_key() -> std::string {
        skip_inline_ws();
        if (p_ < t_.size() && (t_.at(p_) == '"' || t_.at(p_) == '\'')) {
            const Node n = string_value();
            return n.s;
        }
        const size_t start = p_;
        p_ = skip_set(t_, p_, k_bare_chars);
        if (start == p_) {
            bad("expected key");
        }
        return std::string(t_.substr(start, p_ - start));
    }

    auto dotted_key() -> std::vector<std::string> {
        std::vector<std::string> parts;
        parts.push_back(simple_key());
        skip_inline_ws();
        while (p_ < t_.size() && t_.at(p_) == '.') {
            ++p_;
            parts.push_back(simple_key());
            skip_inline_ws();
        }
        return parts;
    }

    static auto child_table(Node& t, const std::string& key) -> Node& {
        if (const auto it = std::ranges::find(t.keys, key); it != t.keys.end()) {
            Node& existing = t.vals.at(static_cast<size_t>(it - t.keys.begin()));
            if (existing.kind != Node::Kind::Table) {
                fail("toml: key is not a table: " + key);
            }
            return existing;
        }
        Node child;
        child.kind = Node::Kind::Table;
        t.keys.push_back(key);
        t.vals.push_back(std::move(child));
        return t.vals.back();
    }

    static auto table_at(Node& root, const std::vector<std::string>& path) -> Node& {
        return *std::ranges::fold_left(
            path, &root, [](Node* cur, const std::string& seg) -> Node* { return &child_table(*cur, seg); });
    }

    auto string_value() -> Node {
        Node n;
        n.kind = Node::Kind::String;
        const char q = t_.at(p_);
        if (t_.substr(p_, 3) == std::string(3, q)) {
            // Multi-line string: skip the first newline if present.
            p_ += 3;
            if (p_ < t_.size() && t_.at(p_) == '\r') {
                ++p_;
            }
            if (p_ < t_.size() && t_.at(p_) == '\n') {
                ++p_;
            }
            while (true) {
                if (p_ >= t_.size()) {
                    bad("unterminated multi-line string");
                }
                if (t_.substr(p_, 3) == std::string(3, q)) {
                    p_ += 3;
                    break;
                }
                const char c = t_.at(p_++);
                if (q == '"' && c == '\\') {
                    const char e = t_.at(p_++);
                    if (e == 'n') {
                        n.s += '\n';
                    } else if (e == 't') {
                        n.s += '\t';
                    } else if (e == '\n') {
                        while (p_ < t_.size() &&
                               (t_.at(p_) == ' ' || t_.at(p_) == '\t' || t_.at(p_) == '\n' || t_.at(p_) == '\r')) {
                            ++p_;
                        }
                    } else {
                        n.s += e;
                    }
                } else {
                    n.s += c;
                }
            }
            return n;
        }
        ++p_;
        while (true) {
            if (p_ >= t_.size() || t_.at(p_) == '\n') {
                bad("unterminated string");
            }
            const char c = t_.at(p_++);
            if (c == q) {
                break;
            }
            if (q == '"' && c == '\\') {
                const char e = t_.at(p_++);
                switch (e) {
                    case 'n':
                        n.s += '\n';
                        break;
                    case 't':
                        n.s += '\t';
                        break;
                    case 'r':
                        n.s += '\r';
                        break;
                    case 'b':
                        n.s += '\b';
                        break;
                    case 'f':
                        n.s += '\f';
                        break;
                    case '"':
                        n.s += '"';
                        break;
                    case '\\':
                        n.s += '\\';
                        break;
                    case 'u':
                    case 'U': {
                        const size_t digits = e == 'u' ? 4 : 8;
                        unsigned cp = 0;
                        for (size_t k = 0; k < digits; ++k) {
                            const char h = t_.at(p_++);
                            cp <<= 4U;
                            if (h >= '0' && h <= '9') {
                                cp |= static_cast<unsigned>(h - '0');
                            } else if (h >= 'a' && h <= 'f') {
                                cp |= static_cast<unsigned>(h - 'a' + 10);
                            } else if (h >= 'A' && h <= 'F') {
                                cp |= static_cast<unsigned>(h - 'A' + 10);
                            } else {
                                bad("bad unicode escape");
                            }
                        }
                        append_utf8(n.s, cp);
                        break;
                    }
                    default:
                        bad("bad escape");
                }
            } else {
                n.s += c;
            }
        }
        return n;
    }

    auto value() -> Node {
        const char c = peek();
        Node n;
        if (c == '"' || c == '\'') {
            return string_value();
        }
        if (c == '[') {
            ++p_;
            n.kind = Node::Kind::Array;
            while (true) {
                skip_blank();
                if (peek() == ']') {
                    ++p_;
                    break;
                }
                n.items.push_back(value());
                skip_blank();
                if (peek() == ',') {
                    ++p_;
                    continue;
                }
                expect(']');
                break;
            }
            return n;
        }
        if (c == '{') {
            ++p_;
            n.kind = Node::Kind::Table;
            skip_inline_ws();
            if (peek() == '}') {
                ++p_;
                return n;
            }
            while (true) {
                std::vector<std::string> path = dotted_key();
                skip_inline_ws();
                expect('=');
                skip_inline_ws();
                Node v = value();
                const std::string last = path.back();
                path.pop_back();
                table_at(n, path).set(last, std::move(v));
                skip_inline_ws();
                if (peek() == ',') {
                    ++p_;
                    skip_inline_ws();
                    continue;
                }
                expect('}');
                break;
            }
            return n;
        }
        const size_t start = p_;
        p_ = skip_to(t_, p_, " \t\n\r,]}#");
        const std::string tok(t_.substr(start, p_ - start));
        if (tok == "true" || tok == "false") {
            n.kind = Node::Kind::Bool;
            n.b = tok == "true";
            return n;
        }
        if (tok.empty() || tok.contains(':')) {
            bad("unsupported value");
        }
        n.kind = Node::Kind::Number;
        n.s = tok;
        return n;
    }
};

} // namespace

// ── Node ────────────────────────────────────────────────────────────────────

auto Node::find(std::string_view key) const -> const Node* {
    if (kind != Kind::Table) {
        return nullptr;
    }
    const auto it = std::ranges::find(keys, key);
    return it == keys.end() ? nullptr : &vals.at(static_cast<size_t>(it - keys.begin()));
}

void Node::set(const std::string& key, Node value) {
    if (const auto it = std::ranges::find(keys, key); it != keys.end()) {
        vals.at(static_cast<size_t>(it - keys.begin())) = std::move(value);
        return;
    }
    keys.push_back(key);
    vals.push_back(std::move(value));
}

auto parse_json(std::string_view text) -> Node {
    return JsonParser(text).document();
}

auto dump_json_indent2(const Node& n) -> std::string {
    std::string out;
    dump_value(out, n, 0);
    return out;
}

auto parse_toml(std::string_view text) -> Node {
    return TomlParser(text).document();
}

auto py_str(const Node& n) -> std::string {
    switch (n.kind) {
        case Node::Kind::String:
        case Node::Kind::Number:
            return n.s;
        case Node::Kind::Bool:
            return n.b ? "True" : "False";
        default:
            return "";
    }
}

auto py_int(const Node& n) -> long long {
    switch (n.kind) {
        case Node::Kind::Number:
        case Node::Kind::String:
            return std::stoll(n.s);
        case Node::Kind::Bool:
            return n.b ? 1 : 0;
        default:
            fail("int() argument must be a number or string");
    }
}

auto py_bool(const Node& n) -> bool {
    switch (n.kind) {
        case Node::Kind::Bool:
            return n.b;
        case Node::Kind::Number:
            return n.s != "0" && n.s != "0.0";
        case Node::Kind::String:
            return !n.s.empty();
        case Node::Kind::Array:
            return !n.items.empty();
        case Node::Kind::Table:
            return !n.keys.empty();
        case Node::Kind::Null:
            return false;
    }
    return false;
}

} // namespace revet::codegen

#include "node.hpp"

#include <cstdio>
#include <stdexcept>

namespace revet::codegen {

namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what);
}

void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// ── JSON parsing ────────────────────────────────────────────────────────────

class JsonParser {
public:
    explicit JsonParser(std::string_view t) : t_(t) {}

    Node document() {
        Node n = value();
        ws();
        if (p_ != t_.size()) fail("json: extra data");
        return n;
    }

private:
    std::string_view t_;
    size_t p_ = 0;

    void ws() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_;
    }

    char peek() {
        if (p_ >= t_.size()) fail("json: unexpected end");
        return t_[p_];
    }

    void expect_word(std::string_view w) {
        if (t_.substr(p_, w.size()) != w) fail("json: bad literal");
        p_ += w.size();
    }

    unsigned hex4() {
        if (p_ + 4 > t_.size()) fail("json: bad \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("json: bad hex digit");
        }
        return v;
    }

    std::string string_body() {
        // Opening quote already consumed.
        std::string out;
        while (true) {
            char c = peek();
            ++p_;
            if (c == '"') break;
            if (c != '\\') {
                out += c;
                continue;
            }
            char e = peek();
            ++p_;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && t_.substr(p_, 2) == "\\u") {
                        p_ += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("json: bad escape");
            }
        }
        return out;
    }

    Node value() {
        ws();
        Node n;
        char c = peek();
        if (c == '{') {
            ++p_;
            n.kind = Node::Kind::Table;
            ws();
            if (peek() == '}') { ++p_; return n; }
            while (true) {
                ws();
                if (peek() != '"') fail("json: expected key");
                ++p_;
                std::string key = string_body();
                ws();
                if (peek() != ':') fail("json: expected ':'");
                ++p_;
                Node v = value();
                n.keys.push_back(std::move(key));
                n.vals.push_back(std::move(v));
                ws();
                char d = peek();
                ++p_;
                if (d == '}') break;
                if (d != ',') fail("json: expected ',' or '}'");
            }
        } else if (c == '[') {
            ++p_;
            n.kind = Node::Kind::Array;
            ws();
            if (peek() == ']') { ++p_; return n; }
            while (true) {
                n.items.push_back(value());
                ws();
                char d = peek();
                ++p_;
                if (d == ']') break;
                if (d != ',') fail("json: expected ',' or ']'");
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
            size_t start = p_;
            while (p_ < t_.size() && (t_[p_] == '-' || t_[p_] == '+' || t_[p_] == '.' ||
                                      t_[p_] == 'e' || t_[p_] == 'E' ||
                                      (t_[p_] >= '0' && t_[p_] <= '9'))) {
                ++p_;
            }
            if (start == p_) fail("json: unexpected character");
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
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c;
        size_t len = 1;
        if (c >= 0x80) {
            if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
            else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
            else { len = 4; cp = c & 0x07; }
            for (size_t k = 1; k < len && i + k < s.size(); ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
            }
        }
        i += len;
        char buf[16];
        switch (cp) {
            case '"': out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            default: break;
        }
        if (cp >= 0x20 && cp <= 0x7E) {
            out += static_cast<char>(cp);
        } else if (cp >= 0x10000) {
            unsigned v = cp - 0x10000;
            std::snprintf(buf, sizeof buf, "\\u%04x", 0xD800 + (v >> 10));
            out += buf;
            std::snprintf(buf, sizeof buf, "\\u%04x", 0xDC00 + (v & 0x3FF));
            out += buf;
        } else {
            std::snprintf(buf, sizeof buf, "\\u%04x", cp);
            out += buf;
        }
    }
    out += '"';
}

void dump_value(std::string& out, const Node& n, int level) {
    const std::string pad(static_cast<size_t>(level + 1) * 2, ' ');
    const std::string close_pad(static_cast<size_t>(level) * 2, ' ');
    switch (n.kind) {
        case Node::Kind::Null: out += "null"; break;
        case Node::Kind::Bool: out += n.b ? "true" : "false"; break;
        case Node::Kind::Number: out += n.s; break;
        case Node::Kind::String: dump_string(out, n.s); break;
        case Node::Kind::Array:
            if (n.items.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < n.items.size(); ++i) {
                if (i) out += ",\n";
                out += pad;
                dump_value(out, n.items[i], level + 1);
            }
            out += "\n" + close_pad + "]";
            break;
        case Node::Kind::Table:
            if (n.keys.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < n.keys.size(); ++i) {
                if (i) out += ",\n";
                out += pad;
                dump_string(out, n.keys[i]);
                out += ": ";
                dump_value(out, n.vals[i], level + 1);
            }
            out += "\n" + close_pad + "}";
            break;
    }
}

// ── TOML parsing (subset) ───────────────────────────────────────────────────

class TomlParser {
public:
    explicit TomlParser(std::string_view t) : t_(t) {}

    Node document() {
        Node root;
        root.kind = Node::Kind::Table;
        std::vector<std::string> section;
        while (true) {
            skip_blank();
            if (p_ >= t_.size()) break;
            if (t_[p_] == '[') {
                ++p_;
                if (peek() == '[') fail("toml: array-of-tables is not supported");
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
                std::string last = path.back();
                path.pop_back();
                Node& owner = table_at(root, section);
                Node& target = table_at(owner, path);
                target.set(last, std::move(v));
            }
            skip_inline_ws();
            if (p_ < t_.size() && t_[p_] == '#') skip_comment();
            if (p_ < t_.size() && t_[p_] != '\n' && t_[p_] != '\r') fail("toml: expected end of line");
        }
        return root;
    }

private:
    std::string_view t_;
    size_t p_ = 0;

    [[noreturn]] void bad(const char* what) {
        fail(std::string("toml: ") + what + " at byte " + std::to_string(p_));
    }

    char peek() {
        if (p_ >= t_.size()) bad("unexpected end");
        return t_[p_];
    }

    void expect(char c) {
        if (peek() != c) bad("unexpected character");
        ++p_;
    }

    void skip_inline_ws() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t')) ++p_;
    }

    void skip_comment() {
        while (p_ < t_.size() && t_[p_] != '\n') ++p_;
    }

    // Whitespace, newlines and comments between statements and inside arrays.
    void skip_blank() {
        while (p_ < t_.size()) {
            char c = t_[p_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++p_;
            else if (c == '#') skip_comment();
            else break;
        }
    }

    static bool bare_char(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }

    std::string simple_key() {
        skip_inline_ws();
        if (p_ < t_.size() && (t_[p_] == '"' || t_[p_] == '\'')) {
            Node n = string_value();
            return n.s;
        }
        size_t start = p_;
        while (p_ < t_.size() && bare_char(t_[p_])) ++p_;
        if (start == p_) bad("expected key");
        return std::string(t_.substr(start, p_ - start));
    }

    std::vector<std::string> dotted_key() {
        std::vector<std::string> parts;
        parts.push_back(simple_key());
        skip_inline_ws();
        while (p_ < t_.size() && t_[p_] == '.') {
            ++p_;
            parts.push_back(simple_key());
            skip_inline_ws();
        }
        return parts;
    }

    static Node& child_table(Node& t, const std::string& key) {
        for (size_t i = 0; i < t.keys.size(); ++i) {
            if (t.keys[i] == key) {
                if (t.vals[i].kind != Node::Kind::Table) fail("toml: key is not a table: " + key);
                return t.vals[i];
            }
        }
        Node child;
        child.kind = Node::Kind::Table;
        t.keys.push_back(key);
        t.vals.push_back(std::move(child));
        return t.vals.back();
    }

    static Node& table_at(Node& root, const std::vector<std::string>& path) {
        Node* cur = &root;
        for (const auto& seg : path) cur = &child_table(*cur, seg);
        return *cur;
    }

    Node string_value() {
        Node n;
        n.kind = Node::Kind::String;
        char q = t_[p_];
        if (t_.substr(p_, 3) == std::string(3, q)) {
            // Multi-line string: skip the first newline if present.
            p_ += 3;
            if (p_ < t_.size() && t_[p_] == '\r') ++p_;
            if (p_ < t_.size() && t_[p_] == '\n') ++p_;
            while (true) {
                if (p_ >= t_.size()) bad("unterminated multi-line string");
                if (t_.substr(p_, 3) == std::string(3, q)) { p_ += 3; break; }
                char c = t_[p_++];
                if (q == '"' && c == '\\') {
                    char e = t_[p_++];
                    if (e == 'n') n.s += '\n';
                    else if (e == 't') n.s += '\t';
                    else if (e == '\n') { while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_; }
                    else n.s += e;
                } else {
                    n.s += c;
                }
            }
            return n;
        }
        ++p_;
        while (true) {
            if (p_ >= t_.size() || t_[p_] == '\n') bad("unterminated string");
            char c = t_[p_++];
            if (c == q) break;
            if (q == '"' && c == '\\') {
                char e = t_[p_++];
                switch (e) {
                    case 'n': n.s += '\n'; break;
                    case 't': n.s += '\t'; break;
                    case 'r': n.s += '\r'; break;
                    case 'b': n.s += '\b'; break;
                    case 'f': n.s += '\f'; break;
                    case '"': n.s += '"'; break;
                    case '\\': n.s += '\\'; break;
                    case 'u':
                    case 'U': {
                        size_t digits = e == 'u' ? 4 : 8;
                        unsigned cp = 0;
                        for (size_t k = 0; k < digits; ++k) {
                            char h = t_[p_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else bad("bad unicode escape");
                        }
                        append_utf8(n.s, cp);
                        break;
                    }
                    default: bad("bad escape");
                }
            } else {
                n.s += c;
            }
        }
        return n;
    }

    Node value() {
        char c = peek();
        Node n;
        if (c == '"' || c == '\'') return string_value();
        if (c == '[') {
            ++p_;
            n.kind = Node::Kind::Array;
            while (true) {
                skip_blank();
                if (peek() == ']') { ++p_; break; }
                n.items.push_back(value());
                skip_blank();
                if (peek() == ',') { ++p_; continue; }
                expect(']');
                break;
            }
            return n;
        }
        if (c == '{') {
            ++p_;
            n.kind = Node::Kind::Table;
            skip_inline_ws();
            if (peek() == '}') { ++p_; return n; }
            while (true) {
                std::vector<std::string> path = dotted_key();
                skip_inline_ws();
                expect('=');
                skip_inline_ws();
                Node v = value();
                std::string last = path.back();
                path.pop_back();
                table_at(n, path).set(last, std::move(v));
                skip_inline_ws();
                if (peek() == ',') { ++p_; skip_inline_ws(); continue; }
                expect('}');
                break;
            }
            return n;
        }
        size_t start = p_;
        while (p_ < t_.size() && t_[p_] != ' ' && t_[p_] != '\t' && t_[p_] != '\n' && t_[p_] != '\r' &&
               t_[p_] != ',' && t_[p_] != ']' && t_[p_] != '}' && t_[p_] != '#') {
            ++p_;
        }
        std::string tok(t_.substr(start, p_ - start));
        if (tok == "true" || tok == "false") {
            n.kind = Node::Kind::Bool;
            n.b = tok == "true";
            return n;
        }
        if (tok.empty() || tok.find(':') != std::string::npos) bad("unsupported value");
        n.kind = Node::Kind::Number;
        n.s = tok;
        return n;
    }
};

}  // namespace

// ── Node ────────────────────────────────────────────────────────────────────

const Node* Node::find(std::string_view key) const {
    if (kind != Kind::Table) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) return &vals[i];
    }
    return nullptr;
}

void Node::set(const std::string& key, Node value) {
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) {
            vals[i] = std::move(value);
            return;
        }
    }
    keys.push_back(key);
    vals.push_back(std::move(value));
}

Node parse_json(std::string_view text) {
    return JsonParser(text).document();
}

std::string dump_json_indent2(const Node& n) {
    std::string out;
    dump_value(out, n, 0);
    return out;
}

Node parse_toml(std::string_view text) {
    return TomlParser(text).document();
}

std::string py_str(const Node& n) {
    switch (n.kind) {
        case Node::Kind::String:
        case Node::Kind::Number: return n.s;
        case Node::Kind::Bool: return n.b ? "True" : "False";
        default: return "";
    }
}

long long py_int(const Node& n) {
    switch (n.kind) {
        case Node::Kind::Number:
        case Node::Kind::String: return std::stoll(n.s);
        case Node::Kind::Bool: return n.b ? 1 : 0;
        default: fail("int() argument must be a number or string");
    }
}

bool py_bool(const Node& n) {
    switch (n.kind) {
        case Node::Kind::Bool: return n.b;
        case Node::Kind::Number: return n.s != "0" && n.s != "0.0";
        case Node::Kind::String: return !n.s.empty();
        case Node::Kind::Array: return !n.items.empty();
        case Node::Kind::Table: return !n.keys.empty();
        case Node::Kind::Null: return false;
    }
    return false;
}

}  // namespace revet::codegen

#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace revet::codegen {

// ── Files ───────────────────────────────────────────────────────────────────

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw GenError("cannot read " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    // Universal newlines: \r\n and \r are read as \n.
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r') {
            out += '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
        } else {
            out += raw[i];
        }
    }
    return out;
}

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

void write_file(const fs::path& p, std::string_view content) {
    if (file_exists(p) && read_text(p) == content) return;
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw GenError("cannot write " + p.string());
    out << content;
}

void write_lines(const fs::path& p, const Lines& lines) {
    write_file(p, join(lines, "\n"));
}

fs::path setup(const Ctx& ctx, std::string_view filename) {
    fs::path out = ctx.proj / "build" / "generated" / std::string(filename);
    fs::create_directories(out.parent_path());
    return out;
}

std::vector<fs::path> glob(const fs::path& dir, std::string_view suffix, bool recursive) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    auto consider = [&](const fs::directory_entry& e) {
        if (!e.is_regular_file(ec)) return;
        std::string name = e.path().filename().string();
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            out.push_back(e.path());
        }
    };
    if (recursive) {
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) consider(e);
    } else {
        for (const auto& e : fs::directory_iterator(dir, ec)) consider(e);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool has_models(const fs::path& dir) {
    return !glob(dir, ".hpp").empty();
}

// ── Templates ───────────────────────────────────────────────────────────────

std::string tmpl_whole(const Ctx& ctx, std::string_view name) {
    return read_text(ctx.framework / "cmake" / "templates" / std::string(name));
}

std::string tmpl(const Ctx& ctx, std::string_view name) {
    std::string s = tmpl_whole(ctx, name);
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

std::string subst(std::string_view text, const Vars& vars) {
    auto lookup = [&](const std::string& key) -> const std::string& {
        for (const auto& [k, v] : vars) {
            if (k == key) return v;
        }
        throw GenError("template: no value for '" + key + "'");
    };
    auto ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '$') {
            out += text[i++];
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == '$') {
            out += '$';
            i += 2;
            continue;
        }
        std::string key;
        if (i + 1 < text.size() && text[i + 1] == '{') {
            size_t close = text.find('}', i + 2);
            if (close == std::string_view::npos) throw GenError("template: unterminated ${");
            key = std::string(text.substr(i + 2, close - i - 2));
            i = close + 1;
        } else {
            size_t j = i + 1;
            while (j < text.size() && ident_char(text[j])) ++j;
            if (j == i + 1) throw GenError("template: invalid placeholder");
            key = std::string(text.substr(i + 1, j - i - 1));
            i = j;
        }
        out += lookup(key);
    }
    return out;
}

// ── Strings ─────────────────────────────────────────────────────────────────

namespace {
bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
}  // namespace

std::string strip(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && is_space(s[b])) ++b;
    while (e > b && is_space(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string join(const Lines& parts, std::string_view sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

Lines splitlines(std::string_view s) {
    Lines out;
    std::string cur;
    bool pending = false;  // true when cur holds text not yet pushed
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\r' || c == '\n') {
            out.push_back(cur);
            cur.clear();
            pending = false;
            if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else {
            cur += c;
            pending = true;
        }
    }
    if (pending) out.push_back(cur);
    return out;
}

Lines split_args(std::string_view s) {
    Lines out;
    int depth = 0;
    std::string buf;
    auto flush = [&] {
        std::string item = strip(buf);
        if (!item.empty()) out.push_back(item);
        buf.clear();
    };
    for (char ch : s) {
        if (ch == '<' || ch == '(' || ch == '[') ++depth;
        else if (ch == '>' || ch == ')' || ch == ']') --depth;
        if (ch == ',' && depth == 0) {
            flush();
        } else {
            buf += ch;
        }
    }
    flush();
    return out;
}

std::string escape_str(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string collapse_ws(std::string_view s) {
    std::string out;
    bool in_ws = false;
    for (char c : s) {
        if (is_space(c)) {
            if (!in_ws) out += ' ';
            in_ws = true;
        } else {
            out += c;
            in_ws = false;
        }
    }
    return out;
}

std::size_t count_newlines(std::string_view s, std::size_t end) {
    return static_cast<std::size_t>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(end), '\n'));
}

std::string rel_include(const fs::path& hpp, const fs::path& proj) {
    return "#include \"../../" + fs::relative(hpp, proj).generic_string() + "\"";
}

Lines file_header(std::initializer_list<std::string> includes) {
    Lines out{"// Auto-generated — do not edit", "#pragma once"};
    for (const auto& inc : includes) out.push_back(inc);
    out.push_back("");
    return out;
}

// ── Regex ───────────────────────────────────────────────────────────────────

bool match_prefix(const std::string& s, Match& m, const Re& re) {
    return std::regex_search(s, m, re, std::regex_constants::match_continuous);
}

bool match_full(const std::string& s, Match& m, const Re& re) {
    return std::regex_match(s, m, re);
}

}  // namespace revet::codegen

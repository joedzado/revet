#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <sstream>

namespace revet::codegen {

// ── Files ───────────────────────────────────────────────────────────────────

auto read_text(const fs::path& p) -> std::string {
    const std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw GenError("cannot read " + p.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    // Universal newlines: \r\n and \r are read as \n.
    return replace_all<R"RE(\r\n?)RE">(ss.str(), "\n");
}

auto file_exists(const fs::path& p) -> bool {
    std::error_code ec;
    return fs::exists(p, ec);
}

void write_file(const fs::path& p, std::string_view content) {
    if (file_exists(p) && read_text(p) == content) {
        return;
    }
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw GenError("cannot write " + p.string());
    }
    out << content;
}

void write_lines(const fs::path& p, const Lines& lines) {
    write_file(p, join(lines, "\n"));
}

auto setup(const Ctx& ctx, std::string_view filename) -> fs::path {
    fs::path out = ctx.proj / "build" / "generated" / std::string(filename);
    fs::create_directories(out.parent_path());
    return out;
}

auto glob(const fs::path& dir, std::string_view suffix, bool recursive) -> std::vector<fs::path> {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return out;
    }
    const auto matching = [&](const auto& entries) -> std::vector<fs::path> {
        return entries | std::views::filter([&](const fs::directory_entry& e) -> bool {
                   return e.is_regular_file(ec) && e.path().filename().string().ends_with(suffix);
               }) |
               std::views::transform([](const fs::directory_entry& e) -> fs::path { return e.path(); }) |
               std::ranges::to<std::vector<fs::path>>();
    };
    out = recursive ? matching(fs::recursive_directory_iterator(dir, ec)) : matching(fs::directory_iterator(dir, ec));
    std::ranges::sort(out);
    return out;
}

auto has_models(const fs::path& dir) -> bool {
    return !glob(dir, ".hpp").empty();
}

// ── Templates ───────────────────────────────────────────────────────────────

auto tmpl_whole(const Ctx& ctx, std::string_view name) -> std::string {
    return read_text(ctx.framework / "cmake" / "templates" / std::string(name));
}

auto tmpl(const Ctx& ctx, std::string_view name) -> std::string {
    std::string s = tmpl_whole(ctx, name);
    s.erase(s.find_last_not_of('\n') + 1); // npos + 1 == 0: all newlines
    return s;
}

auto subst(std::string_view text, const Vars& vars) -> std::string {
    const auto lookup = [&](const std::string& key) -> const std::string& {
        const auto it = std::ranges::find(vars, key, &Vars::value_type::first);
        if (it == vars.end()) {
            throw GenError("template: no value for '" + key + "'");
        }
        return it->second;
    };
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text.at(i) != '$') {
            out += text.at(i++);
            continue;
        }
        if (i + 1 < text.size() && text.at(i + 1) == '$') {
            out += '$';
            i += 2;
            continue;
        }
        std::string key;
        if (i + 1 < text.size() && text.at(i + 1) == '{') {
            const size_t close = text.find('}', i + 2);
            if (close == std::string_view::npos) {
                throw GenError("template: unterminated ${");
            }
            key = std::string(text.substr(i + 2, close - i - 2));
            i = close + 1;
        } else {
            constexpr std::string_view k_ident = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
            const auto j = std::min(text.find_first_not_of(k_ident, i + 1), text.size());
            if (j == i + 1) {
                throw GenError("template: invalid placeholder");
            }
            key = std::string(text.substr(i + 1, j - i - 1));
            i = j;
        }
        out += lookup(key);
    }
    return out;
}

// ── Strings ─────────────────────────────────────────────────────────────────

auto strip(std::string_view s) -> std::string {
    constexpr std::string_view k_ws = " \t\n\r\f\v";
    const size_t b = s.find_first_not_of(k_ws);
    if (b == std::string_view::npos) {
        return {};
    }
    return std::string(s.substr(b, s.find_last_not_of(k_ws) - b + 1));
}

auto lower(std::string_view s) -> std::string {
    return s | std::views::transform([](char c) -> char {
               return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
           }) |
           std::ranges::to<std::string>();
}

auto starts_with(std::string_view s, std::string_view prefix) -> bool {
    return s.starts_with(prefix);
}

auto ends_with(std::string_view s, std::string_view suffix) -> bool {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

auto join(const Lines& parts, std::string_view sep) -> std::string {
    if (parts.empty()) {
        return {};
    }
    return std::ranges::fold_left(parts | std::views::drop(1), parts.front(),
                                  [sep](std::string acc, const std::string& p) -> std::string {
                                      acc += sep;
                                      acc += p;
                                      return acc;
                                  });
}

auto splitlines(std::string_view s) -> Lines {
    std::string text = replace_all<R"RE(\r\n?)RE">(s, "\n");
    if (text.empty()) {
        return {};
    }
    auto out = text | std::views::split('\n') |
               std::views::transform([](auto line) -> std::string { return std::string(std::string_view(line)); }) |
               std::ranges::to<Lines>();
    if (text.back() == '\n') {
        out.pop_back(); // split's empty piece after the final newline
    }
    return out;
}

auto split_args(std::string_view s) -> Lines {
    Lines out;
    int depth = 0;
    std::string buf;
    const auto flush = [&] -> void {
        const std::string item = strip(buf);
        if (!item.empty()) {
            out.push_back(item);
        }
        buf.clear();
    };
    for (const char ch : s) {
        if (ch == '<' || ch == '(' || ch == '[') {
            ++depth;
        } else if (ch == '>' || ch == ')' || ch == ']') {
            --depth;
        }
        if (ch == ',' && depth == 0) {
            flush();
        } else {
            buf += ch;
        }
    }
    flush();
    return out;
}

auto escape_str(std::string_view s) -> std::string {
    auto escaped = [](const char& c) -> std::string_view {
        switch (c) {
            case '\\':
                return "\\\\";
            case '"':
                return "\\\"";
            case '\r':
                return "\\r";
            case '\n':
                return "\\n";
            default:
                return {&c, 1};
        }
    };
    return s | std::views::transform(escaped) | std::views::join | std::ranges::to<std::string>();
}

auto escape_quotes(std::string_view s) -> std::string {
    auto escaped = [](const char& c) -> std::string_view {
        if (c == '\\') {
            return "\\\\";
        }
        if (c == '"') {
            return "\\\"";
        }
        return {&c, 1};
    };
    return s | std::views::transform(escaped) | std::views::join | std::ranges::to<std::string>();
}

auto collapse_ws(std::string_view s) -> std::string {
    return replace_all<R"RE(\s+)RE">(s, " ");
}

auto count_newlines(std::string_view s, std::size_t end) -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count(s.substr(0, end), '\n'));
}

auto rel_include(const fs::path& hpp, const fs::path& proj) -> std::string {
    return "#include \"../../" + fs::relative(hpp, proj).generic_string() + "\"";
}

auto file_header(std::initializer_list<std::string> includes) -> Lines {
    Lines out{"// Auto-generated — do not edit", "#pragma once"};
    out.insert(out.end(), includes);
    out.emplace_back("");
    return out;
}

} // namespace revet::codegen

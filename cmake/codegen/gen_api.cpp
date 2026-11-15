// Port of cmake/gen_api.py: [[revet::external("METHOD URL")]] declarations -> apis.hpp (inline HTTP coroutines).
#include "common.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace revet::codegen {
namespace {

struct Param {
    std::string type;
    std::string name;
};

struct External {
    std::string method;
    std::string url;
    std::string ns;  // "a::b" or empty
    std::string func_name;
    std::string return_type;
    std::string response_type;
    std::vector<Param> params;
};

const Re& attr_re() {
    static const Re re(R"RE(\[\[\s*revet::external\s*\(\s*"([^"]+)"\s*\)\s*\]\])RE");
    return re;
}

const Re& ns_open_re() {
    static const Re re(R"RE(\bnamespace\s+(\w+)\s*\{)RE");
    return re;
}

// Function declaration directly after the attribute (anchored at the start of the tail).
//   auto name(params) -> ReturnType;   |   ReturnType name(params);
const Re& decl_re() {
    static const Re re(R"RE((?:inline\s+)?(?:auto\s+(\w+)\s*\(([^)]*)\)\s*->\s*([^;]+?)|([\w:<>,\s]+?)\s+(\w+)\s*\(([^)]*)\))\s*;)RE");
    return re;
}

const Re& param_re() {
    static const Re re(R"RE(\s*(?:const\s+)?([\w:<>,\s]+?)\s+(\w+)\s*(?:=\s*(.+?))?\s*)RE");
    return re;
}

// Splits on commas outside <>, (), [] and drops empties (Python split_top_commas).
Lines split_top_commas(const std::string& s) {
    return split_args(s);
}

std::vector<Param> parse_params(const std::string& s) {
    std::vector<Param> params;
    for (const auto& raw : split_top_commas(s)) {
        Match m;
        if (!match_full(raw, m, param_re())) continue;
        params.push_back({collapse_ws(strip(m[1].str())), m[2].str()});
    }
    return params;
}

std::string task_inner(const std::string& ret) {
    static const Re task_re(R"RE(\s*web::Task\s*<\s*([\s\S]+)\s*>\s*)RE");
    std::string stripped = strip(ret);
    Match m;
    if (match_full(stripped, m, task_re)) return strip(m[1].str());
    return stripped;
}

// Mirrors Python _ns_at-free namespace tracking in scan_file: a brace walk over the whole text.
std::vector<External> scan_file(const fs::path& path) {
    std::string text = read_text(path);

    struct Ev {
        size_t pos;
        int kind;  // 0 = namespace open, 1 = closing brace, 2 = attribute
        std::string payload;  // namespace name, or attribute method+url
        size_t end;
    };
    std::vector<Ev> events;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), ns_open_re()); it != std::sregex_iterator(); ++it) {
        size_t pos = static_cast<size_t>(it->position(0));
        events.push_back({pos, 0, (*it)[1].str(), pos + static_cast<size_t>(it->length(0))});
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '}') events.push_back({i, 1, "", i + 1});
    }
    for (auto it = std::sregex_iterator(text.begin(), text.end(), attr_re()); it != std::sregex_iterator(); ++it) {
        size_t pos = static_cast<size_t>(it->position(0));
        events.push_back({pos, 2, (*it)[1].str(), pos + static_cast<size_t>(it->length(0))});
    }
    std::stable_sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) { return a.pos < b.pos; });

    std::vector<External> out;
    std::vector<std::string> ns_stack;
    std::vector<int> ns_depths;
    int depth = 0;
    size_t last = 0;

    auto consume_braces = [&](size_t upto) {
        for (size_t k = last; k < upto; ++k) {
            if (text[k] == '{') {
                ++depth;
            } else if (text[k] == '}') {
                --depth;
                while (!ns_depths.empty() && depth < ns_depths.back()) {
                    ns_depths.pop_back();
                    ns_stack.pop_back();
                }
            }
        }
    };

    for (const auto& ev : events) {
        consume_braces(ev.pos);
        if (ev.kind == 0) {
            ++depth;
            ns_stack.push_back(ev.payload);
            ns_depths.push_back(depth);
            last = ev.end;
        } else if (ev.kind == 1) {
            last = ev.pos;
        } else {
            static const Re method_re(R"RE(^(GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS)\s+(.+)$)RE");
            Match mu;
            std::string method_url = strip(ev.payload);
            if (!match_full(method_url, mu, method_re)) {
                last = ev.end;
                continue;
            }
            std::string method = mu[1].str();
            std::string url = strip(mu[2].str());

            std::string tail = text.substr(ev.end, 2048);
            std::string lead = tail.substr(std::min(tail.find_first_not_of(" \t\r\n\f\v"), tail.size()));
            Match dm;
            if (!match_prefix(lead, dm, decl_re())) {
                last = ev.end;
                continue;
            }
            bool alt1 = dm[1].matched;
            std::string ret = strip(alt1 ? dm[3].str() : dm[4].str());
            std::string name = alt1 ? dm[1].str() : dm[5].str();
            std::string params_s = alt1 ? dm[2].str() : dm[6].str();

            std::string ns;
            for (const auto& n : ns_stack) {
                if (!ns.empty()) ns += "::";
                ns += n;
            }
            out.push_back({method, url, ns, name, ret, task_inner(ret), parse_params(params_s)});
            last = ev.end;
        }
    }
    return out;
}

std::string to_string_expr(const std::string& ptype, const std::string& name) {
    if (lower(ptype).find("string") != std::string::npos) return cat("std::string(", name, ")");
    return cat("std::to_string(", name, ")");
}

std::string json_str_literal(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
}

// Builds the URL expression: path params substituted, remaining params appended as a query string.
std::string url_expr(const std::string& url, const std::vector<Param>& params) {
    std::set<std::string> path_names;
    static const Re path_re(R"RE(\{(\w+)\})RE");
    for (auto it = std::sregex_iterator(url.begin(), url.end(), path_re); it != std::sregex_iterator(); ++it) {
        path_names.insert((*it)[1].str());
    }

    std::string expr = cat("std::string(", json_str_literal(url), ")");
    for (const auto& p : params) {
        if (!path_names.count(p.name)) continue;
        std::string val = to_string_expr(p.type, p.name);
        std::string key = "{" + p.name + "}";
        expr = cat("[](std::string s, const std::string& v) { auto p = s.find(\"", key,
                   "\"); if (p != std::string::npos) s.replace(p, ", std::to_string(p.name.size() + 2),
                   ", v); return s; }(", expr, ", ", val, ")");
    }
    bool first_q = url.find('?') == std::string::npos;
    for (const auto& p : params) {
        if (path_names.count(p.name)) continue;
        std::string val = to_string_expr(p.type, p.name);
        std::string sep = first_q ? "?" : "&";
        first_q = false;
        expr = cat(expr, " + \"", sep, p.name, "=\" + ", val);
    }
    return expr;
}

std::string http_helper(const std::string& method) {
    std::string m = lower(method);
    return m == "delete" ? "delete_" : m;
}

Lines emit_definition(const External& e) {
    std::string sig;
    for (size_t i = 0; i < e.params.size(); ++i) {
        if (i) sig += ", ";
        sig += e.params[i].type + " " + e.params[i].name;
    }
    Lines lines;
    lines.push_back(cat("inline auto ", e.func_name, "(", sig, ") -> ", e.return_type, " {"));
    lines.push_back(cat("    auto url  = ", url_expr(e.url, e.params), ";"));
    lines.push_back(cat("    auto resp = co_await http::", http_helper(e.method), "(url);"));
    lines.push_back("    if (!resp.ok()) throw http::Error(resp.status_code, resp.body);");
    lines.push_back("    revet::json::Document doc(resp.body);");
    lines.push_back(cat("    co_return revet::json::from_json<", e.response_type, ">(doc);"));
    lines.push_back("}");
    return lines;
}

}  // namespace

int gen_api(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "apis.hpp");
    fs::path apis_dir = ctx.proj / "app" / "apis";

    std::vector<External> externs;
    for (const auto& f : glob(apis_dir, ".hpp")) {
        for (auto& e : scan_file(f)) externs.push_back(std::move(e));
    }

    Lines lines = file_header({"#include <coroutine>", "#include <revet/task.hpp>", "#include <revet/http.hpp>",
                               "#include <revet/json.hpp>", "#include \"models.hpp\"", "#include <string>"});
    for (const auto& hpp : glob(apis_dir, ".hpp")) lines.push_back(rel_include(hpp, ctx.proj));
    lines.push_back("");

    // Group by namespace, emitted in sorted namespace order.
    std::map<std::string, std::vector<External>> by_ns;
    for (auto& e : externs) by_ns[e.ns].push_back(std::move(e));
    for (const auto& [ns, group] : by_ns) {
        size_t depth = 0;
        if (!ns.empty()) {
            std::string opened;
            size_t start = 0;
            while (start <= ns.size()) {
                size_t end = ns.find("::", start);
                if (end == std::string::npos) end = ns.size();
                if (depth) opened += " ";
                opened += "namespace " + ns.substr(start, end - start) + " {";
                ++depth;
                start = end + 2;
            }
            lines.push_back(opened);
        }
        for (const auto& e : group) {
            for (const auto& l : emit_definition(e)) lines.push_back(l);
            lines.push_back("");
        }
        if (!ns.empty()) {
            lines.push_back(std::string(depth, '}') + " // namespace " + ns);
            lines.push_back("");
        }
    }

    write_file(out_path, join(lines, "\n"));
    return 0;
}

}  // namespace revet::codegen

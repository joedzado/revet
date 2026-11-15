// app/routes/**/*.cpp -> routes.hpp (urls::) and routes.cpp (route table).
#include "common.hpp"
#include "node.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <tuple>

namespace revet::codegen {
namespace {

// ── Regexes ─────────────────────────────────────────────────────────────────

const Re& model_attr_re() {
    static const Re re(R"RE((?:\[\[\s*revet::model[^\]]*\]\]\s*struct|struct\s*\[\[\s*revet::model[^\]]*\]\])\s+(\w+))RE");
    return re;
}
const Re& state_flash_re() {
    static const Re re(R"RE(web::(State|Flash)<(\w+)>)RE");
    return re;
}
const Re& toggle_re() {
    static const Re re(
        R"RE(\[\[[\s\S]*?revet::toggle\(\s*"([^"]+)"\s*,\s*(true|false)\s*\)[\s\S]*?\]\]\s*bool\s+(\w+))RE");
    return re;
}

// ── Path → route / namespace ────────────────────────────────────────────────

struct Entry {
    std::string route;
    std::string ns;
    fs::path file;
};

// rel is relative to app/routes.
std::pair<std::string, std::string> route_and_ns(const fs::path& rel) {
    std::vector<std::string> parts;
    for (const auto& e : rel) parts.push_back(e.string());
    std::string& last = parts.back();
    size_t dot = last.rfind('.');
    if (dot != std::string::npos && dot != 0) last = last.substr(0, dot);  // with_suffix("")
    if (parts.back() == "index") parts.pop_back();

    static const Re bracket_re(R"RE(\[(.+)\])RE");
    std::vector<std::string> route_parts;
    std::vector<std::string> ns_parts;
    for (const auto& part : parts) {
        Match m;
        if (match_full(part, m, bracket_re)) {
            route_parts.push_back(":" + m[1].str());
            ns_parts.push_back("_" + m[1].str() + "_");
        } else {
            route_parts.push_back(part);
            std::string p = part;
            std::replace(p.begin(), p.end(), '-', '_');
            ns_parts.push_back(p);
        }
    }
    std::string route = "/" + join(route_parts, "/");
    std::string ns = join(ns_parts, "_");
    std::replace(ns.begin(), ns.end(), '-', '_');
    if (ns.empty()) ns = "index";
    return {route, ns};
}

std::set<std::string> scan_model_names(const fs::path& models_dir) {
    std::set<std::string> names;
    for (const auto& f : glob(models_dir, ".hpp")) {
        std::string content = read_text(f);
        for (auto it = std::sregex_iterator(content.begin(), content.end(), model_attr_re()); it != std::sregex_iterator(); ++it) {
            names.insert((*it)[1].str());
        }
    }
    return names;
}

// ── Per-route emission helpers ──────────────────────────────────────────────

struct StateInfo {
    bool needed = false;
    std::vector<std::string> state_vars;
    std::vector<std::tuple<std::string, std::string, std::string>> flashes;  // (mode, type, var)
};

StateInfo extract_state_vars(const std::string& func_name, const std::string& src) {
    StateInfo info;
    std::string args;
    Match head;
    Re head_re(cat(func_name, R"(\s*\(([\s\S]*?)\))"));
    if (std::regex_search(src, head, head_re)) args = head[1].str();

    std::set<std::pair<std::string, std::string>> needed;
    for (auto it = std::sregex_iterator(args.begin(), args.end(), state_flash_re()); it != std::sregex_iterator(); ++it) {
        needed.insert({(*it)[1].str(), (*it)[2].str()});
    }
    info.needed = !needed.empty();
    for (const auto& [mode, type] : needed) {
        std::string var = "state_" + type;
        info.state_vars.push_back(var);
        info.flashes.emplace_back(mode, type, var);
    }
    return info;
}

std::string handler_args(const std::vector<std::string>& state_vars, const std::string& suffix = "") {
    std::string out = "req";
    for (const auto& v : state_vars) out += ", " + v;
    return out + suffix;
}

void emit_state_loading(Lines& out, const StateInfo& info) {
    for (const auto& [mode, type, var] : info.flashes) {
        out.push_back(cat("            web::", mode, "<", type, "> ", var, ";"));
        out.push_back(cat("            if (auto c = req.cookies.get(\"_cw_", type, "\"); !c.empty())"));
        out.push_back(cat("                ", var, ".data = web::states::unpack_", type, "(c);"));
        out.push_back(cat("            ctx.states[typeid(", type, ")] = ", var, ";"));
    }
}

struct ApiDetect {
    std::string param_type;   // empty when none
    std::string return_type;  // empty when none
    bool is_async = false;
};

ApiDetect api_detect(const std::string& func_name, const std::string& src, const std::set<std::string>& names) {
    ApiDetect d;
    Match pm;
    Re param_re(cat(func_name, R"(\s*\(\s*const\s+([\w:]+)\s*&)"));
    if (std::regex_search(src, pm, param_re)) {
        std::string raw = pm[1].str();
        size_t sep = raw.rfind("::");
        if (sep != std::string::npos) raw = raw.substr(sep + 2);
        if (names.count(raw)) d.param_type = raw;
    }

    Re async_re(cat(func_name, R"(\s*\([^)]*\)\s*->\s*(?:web::)?Task\s*<)"));
    d.is_async = std::regex_search(src, async_re);

    Match rm;
    Re ret_re(cat(func_name, R"(\s*\([^)]*\)\s*->\s*(?:(?:web::)?Task\s*<\s*)?(std::vector\s*<\s*(\w+)\s*>|(\w+)))"));
    if (std::regex_search(src, rm, ret_re)) {
        if (rm[2].matched && names.count(rm[2].str())) {
            d.return_type = "std::vector<" + rm[2].str() + ">";
        } else if (rm[3].matched && names.count(rm[3].str())) {
            d.return_type = rm[3].str();
        }
    }
    return d;
}

void emit_api_handler(Lines& out, const std::string& ns, const std::string& method, const std::string& field,
                      const std::string& src, const std::set<std::string>& names) {
    ApiDetect d = api_detect("render_" + method, src, names);

    std::string ret = d.is_async ? "co_return" : "return";
    std::string wait = d.is_async ? "co_await " : "";
    std::string handle = d.is_async ? "r.async_" + field : "r." + field;
    std::string sig = d.is_async ? "-> web::Task<web::Response>" : "-> web::Response";

    out.push_back(cat("        ", handle, " = [](const web::Request& req) ", sig, " {"));
    const std::string fn = cat("route_", ns, "::render_", method);
    if (!d.param_type.empty()) {
        out.push_back("            if (!std::string_view(req.headers.get(\"content-type\")).starts_with(\"application/json\"))");
        out.push_back(cat("                ", ret, " web::api::error(web::status::bad_request, \"Content-Type: application/json required\");"));
        out.push_back("            if (req.raw_body.empty())");
        out.push_back(cat("                ", ret, " web::api::error(web::status::bad_request, \"request body required\");"));
        out.push_back(cat("            ", d.param_type, " body{};"));
        out.push_back("            try {");
        out.push_back("                revet::json::Document doc(req.raw_body);");
        out.push_back(cat("                body = revet::json::from_json<", d.param_type, ">(doc);"));
        out.push_back("            } catch (const revet::json::ParseError& e) {");
        out.push_back(cat("                ", ret, " web::api::error(web::status::bad_request, e.what());"));
        out.push_back("            }");
        if (!d.return_type.empty()) {
            out.push_back(cat("            auto result = ", wait, fn, "(body);"));
            out.push_back(cat("            ", ret, " web::Response{.body = revet::json::to_json<", d.return_type,
                              ">(result), .content_type = \"application/json\"};"));
        } else {
            out.push_back(cat("            ", ret, " web::to_response(", wait, fn, "(body));"));
        }
    } else if (!d.return_type.empty()) {
        out.push_back(cat("            auto result = ", wait, fn, "(req);"));
        out.push_back(cat("            ", ret, " web::Response{.body = revet::json::to_json<", d.return_type,
                          ">(result), .content_type = \"application/json\"};"));
    } else {
        out.push_back(cat("            ", ret, " web::to_response(", wait, fn, "(req));"));
    }
    out.push_back("        };");
}

// ── compile_commands.json patch ─────────────────────────────────────────────

Node str_node(const std::string& s) {
    Node n;
    n.kind = Node::Kind::String;
    n.s = s;
    return n;
}

fs::path resolved(const fs::path& p) {
    std::error_code ec;
    fs::path r = fs::weakly_canonical(p, ec);
    return ec ? p : r;
}

void patch_compile_commands(const Ctx& ctx, const fs::path& out_c, const fs::path& site) {
    fs::path cc = ctx.proj / "build" / "compile_commands.json";
    if (!file_exists(cc)) return;

    Node db;
    try {
        db = parse_json(read_text(cc));
    } catch (const std::exception& e) {
        std::cerr << "gen_routes: warning: failed to parse " << cc.string() << ": " << e.what() << "\n";
        return;
    }
    if (db.kind != Node::Kind::Array) return;

    auto file_of = [](const Node& e) -> fs::path {
        const Node* f = e.find("file");
        return resolved(fs::path(f && f->kind == Node::Kind::String ? f->s : std::string()));
    };

    fs::path routes_cpp = resolved(out_c);
    const Node* ref = nullptr;
    for (const auto& e : db.items) {
        if (file_of(e) == routes_cpp) {
            ref = &e;
            break;
        }
    }
    if (!ref) return;
    Node ref_copy = *ref;

    fs::path gen_dir = resolved(ctx.proj / "build" / "generated");
    const Lines always_headers = {"routes.hpp", "components.hpp", "styles.hpp", "states.hpp",
                                  "toggles.hpp", "i18n.hpp",     "uploads.hpp"};
    const Lines optional_headers = {"queries.hpp", "resources.hpp", "models.hpp", "forms.hpp", "apis.hpp"};
    std::vector<fs::path> headers;
    for (const auto& h : always_headers) headers.push_back(gen_dir / h);
    for (const auto& h : optional_headers) {
        if (file_exists(gen_dir / h)) headers.push_back(gen_dir / h);
    }

    Lines include_pairs;  // arguments form: "-include", "<abs>"
    Lines include_flags;  // command form:    "-include <abs>"
    for (const auto& h : headers) {
        include_pairs.push_back("-include");
        include_pairs.push_back(h.string());
        include_flags.push_back("-include " + h.string());
    }

    auto make_entry = [&](const fs::path& src_file) {
        Node entry = ref_copy;
        fs::path src_abs = resolved(src_file);
        std::string stem = src_file.stem().string();
        entry.set("file", str_node(src_abs.string()));
        if (const Node* cmd_node = ref_copy.find("command")) {
            std::string cmd = std::regex_replace(cmd_node->s, Re(R"(-o\s+\S+)"), "-o " + stem + ".o");
            std::string extra = " " + join(include_flags, " ") + " -Wno-unused-function";
            size_t sp = cmd.rfind(' ');
            std::string prefix = sp == std::string::npos ? "" : cmd.substr(0, sp);
            entry.set("command", str_node(prefix + extra + " " + src_abs.string()));
        } else if (const Node* args_node = ref_copy.find("arguments")) {
            Node args;
            args.kind = Node::Kind::Array;
            for (const auto& a : args_node->items) args.items.push_back(a);
            for (size_t i = 0; i < args.items.size(); ++i) {
                if (args.items[i].kind == Node::Kind::String && args.items[i].s == "-o" && i + 1 < args.items.size()) {
                    args.items[i + 1] = str_node(stem + ".o");
                }
            }
            if (!args.items.empty()) args.items.back() = str_node(src_abs.string());
            for (const auto& p : include_pairs) args.items.push_back(str_node(p));
            args.items.push_back(str_node("-Wno-unused-function"));
            entry.set("arguments", args);
        }
        return entry;
    };

    std::set<fs::path> site_files;
    for (const auto& f : glob(site, ".cpp", true)) {
        if (f.filename() != "site_init.cpp") site_files.insert(resolved(f));
    }

    Node out;
    out.kind = Node::Kind::Array;
    for (const auto& e : db.items) {
        if (!site_files.count(file_of(e))) out.items.push_back(e);
    }
    for (const auto& f : site_files) out.items.push_back(make_entry(f));

    write_file(cc, dump_json_indent2(out));
}

}  // namespace

int gen_routes(const Ctx& ctx) {
    // routes.cpp includes these asset headers, so they must exist first.
    gen_htmx(ctx);
    gen_optimistic(ctx);
    gen_scripts(ctx);  // scripts.hpp, listed as a Routes output

    fs::path site = ctx.proj / "app" / "routes";
    fs::path models_dir = ctx.proj / "models";
    fs::path models_site = site / "models.hpp";
    fs::path apis_dir = ctx.proj / "app" / "apis";
    bool have_models = has_models(models_dir);
    bool have_apis = !glob(apis_dir, ".hpp").empty();
    std::set<std::string> model_names = scan_model_names(models_dir);

    fs::path out_h = setup(ctx, "routes.hpp");
    fs::path out_c = out_h.parent_path() / "routes.cpp";

    // Entries: sorted paths, then static routes before parameterized ones (stable).
    std::vector<Entry> entries;
    for (const auto& f : glob(site, ".cpp", true)) {
        auto [route, ns] = route_and_ns(fs::relative(f, site));
        entries.push_back({route, ns, f});
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        int ka = a.route.find(':') != std::string::npos ? 1 : 0;
        int kb = b.route.find(':') != std::string::npos ? 1 : 0;
        if (ka != kb) return ka < kb;
        return a.route < b.route;
    });

    // ── urls:: entries ──
    const std::string url_param_t = tmpl(ctx, "routes_url_param.tmpl");
    const std::string url_static_t = tmpl(ctx, "routes_url_static.tmpl");
    const std::string static_asset_t = tmpl(ctx, "routes_static_asset.tmpl");
    const std::string toggle_handler_t = tmpl(ctx, "routes_toggle_handler.tmpl");

    Lines hpp_url_entries;
    static const Re param_re(R"RE(:(\w+))RE");
    static const Re param_any_re(R"RE(:\w+)RE");
    for (const auto& e : entries) {
        Lines params;
        for (auto it = std::sregex_iterator(e.route.begin(), e.route.end(), param_re); it != std::sregex_iterator(); ++it) {
            params.push_back((*it)[1].str());
        }
        if (!params.empty()) {
            std::string param_args;
            for (size_t i = 0; i < params.size(); ++i) {
                if (i) param_args += ", ";
                param_args += "std::string_view " + params[i];
            }
            std::string fmt_route = std::regex_replace(e.route, param_any_re, "{}");
            // Python: args are url-encoded with sanitize::url_encode when building a URL.
            Lines wrapped;
            for (const auto& p : params) wrapped.push_back("sanitize::url_encode(" + p + ")");
            std::string encoded_args = join(wrapped, ", ");
            hpp_url_entries.push_back(subst(url_param_t, {{"ns", e.ns},
                                                          {"param_args", param_args},
                                                          {"fmt_route", fmt_route},
                                                          {"args_list", encoded_args}}));
        } else {
            hpp_url_entries.push_back(subst(url_static_t, {{"ns", e.ns}, {"route", e.route}}));
        }
    }

    // ── includes ──
    std::set<std::string> global_includes = {"#include \"routes.hpp\"", "#include \"styles.hpp\"", "#include <vector>"};
    for (const auto& hpp : glob(ctx.proj / "types", ".hpp")) global_includes.insert(rel_include(hpp, ctx.proj));
    if (have_models) global_includes.insert("#include \"models.hpp\"");
    if (have_apis) global_includes.insert("#include \"apis.hpp\"");
    if (file_exists(models_site)) global_includes.insert(rel_include(models_site, ctx.proj));

    // ── site bodies: includes hoisted, the rest kept (ordered map by namespace) ──
    std::vector<std::pair<std::string, std::string>> site_contents;
    for (const auto& e : entries) {
        std::string content = read_text(e.file);
        Lines stripped;
        for (const auto& line : splitlines(content)) {
            std::string s = strip(line);
            if (starts_with(s, "#include")) {
                if (s.find("models.hpp") != std::string::npos || s.find("json_models.hpp") != std::string::npos ||
                    s.find("web.hpp") != std::string::npos || s.find("routes.hpp") != std::string::npos ||
                    s.find("styles.hpp") != std::string::npos) {
                    continue;
                }
                global_includes.insert(s);
            } else {
                stripped.push_back(line);
            }
        }
        std::string body = join(stripped, "\n");
        bool replaced = false;
        for (auto& [k, v] : site_contents) {
            if (k == e.ns) {
                v = body;
                replaced = true;
                break;
            }
        }
        if (!replaced) site_contents.emplace_back(e.ns, body);
    }

    // ── state toggles: [[revet::toggle("/route", default)]] bool name ──
    std::vector<std::pair<std::string, std::string>> toggle_routes;  // (route, name)
    for (const auto& f : glob(ctx.proj / "app" / "states", ".hpp")) {
        std::string content = read_text(f);
        for (auto it = std::sregex_iterator(content.begin(), content.end(), toggle_re()); it != std::sregex_iterator(); ++it) {
            toggle_routes.emplace_back((*it)[1].str(), (*it)[3].str());
        }
    }
    for (const auto& [route, name] : toggle_routes) {
        hpp_url_entries.push_back(subst(url_static_t, {{"ns", name}, {"route", route}}));
    }

    // ── routes.hpp ──
    fs::path version_file = ctx.proj.parent_path() / "VERSION";
    std::string version_str = "unknown";
    if (file_exists(version_file)) {
        std::string raw = strip(read_text(version_file));
        version_str = raw;
    }
    Lines hpp = file_header({"#include <string_view>", "#include <format>", "#include <revet/web.hpp>",
                             "#include <revet/upload.hpp>", "#include \"toggles.hpp\"", "#include \"components.hpp\"",
                             "#include \"i18n.hpp\"", "#include \"uploads.hpp\"", "#include \"resources.hpp\""});
    hpp.push_back("namespace urls {");
    for (const auto& l : hpp_url_entries) hpp.push_back(l);
    hpp.push_back("}");
    for (const auto& l : Lines{"", "namespace revet {",
                               cat("inline constexpr std::string_view version = \"", version_str, "\";"),
                               "} // namespace revet"}) {
        hpp.push_back(l);
    }
    write_lines(out_h, hpp);

    // ── routes.cpp ──
    Lines cpp;
    for (const auto& inc : global_includes) cpp.push_back(inc);  // std::set is already sorted
    cpp.push_back("#include \"htmx_asset.hpp\"");
    cpp.push_back("#include \"optimistic_asset.hpp\"");
    cpp.push_back("#include \"scripts.hpp\"");
    cpp.push_back("#include \"server.hpp\"  // revet.toml max_body, applied at static init");
    cpp.push_back("");

    for (const auto& [ns, content] : site_contents) {
        cpp.push_back("namespace route_" + ns + " {");
        cpp.push_back(content);
        cpp.push_back("}");
        cpp.push_back("");
    }

    cpp.push_back("namespace routes {");
    cpp.push_back("void setup(std::vector<web::Route>& out) {");
    for (const auto& [path, symbol] : std::vector<std::pair<std::string, std::string>>{
             {"/_revet/htmx.js", "htmx_js"}, {"/_revet/optimistic.js", "optimistic_js"}}) {
        cpp.push_back(subst(static_asset_t, {{"path", path}, {"symbol", symbol}}));
    }
    // App scripts (app/scripts/*.js): the URL carries a content hash, so they can be cached for good.
    const std::string app_script_t = tmpl(ctx, "routes_app_script.tmpl");
    for (const auto& [url, ident] : app_script_routes(ctx)) {
        cpp.push_back(subst(app_script_t, {{"path", url}, {"symbol", ident}}));
    }
    for (const auto& [route, name] : toggle_routes) {
        cpp.push_back(subst(toggle_handler_t, {{"path", route}, {"name", name}}));
    }

    for (const auto& e : entries) {
        std::string src = read_text(e.file);
        bool is_api = starts_with(e.route, "/api/") || e.route == "/api";
        cpp.push_back("    {");
        cpp.push_back(cat("        web::Route r; r.path = \"", e.route, "\";"));

        static const std::vector<std::pair<std::string, std::string>> method_field = {
            {"get", "get"}, {"post", "post"}, {"put", "put"}, {"delete", "del"}, {"patch", "patch"}};
        for (const auto& [method, field] : method_field) {
            std::string func = "render_" + method;
            if (!std::regex_search(src, Re(func + R"(\s*\()"))) continue;

            if (is_api) {
                emit_api_handler(cpp, e.ns, method, field, src, model_names);
                continue;
            }

            StateInfo st = extract_state_vars(func, src);
            bool is_async =
                std::regex_search(src, Re(R"(Task\s*<\s*(?:web::)?Response\s*>\s+)" + func + R"(\s*\()")) ||
                std::regex_search(src, Re(func + R"(\s*\([^)]*\)\s*->\s*web::Task\s*<\s*(?:web::)?Response\s*>)"));

            if (is_async) {
                cpp.push_back(cat("        r.async_", field, " = [](const web::Request& req) -> web::Task<web::Response> {"));
                if (st.needed) cpp.push_back("            auto& ctx = web::Context::current();");
                emit_state_loading(cpp, st);
                cpp.push_back(cat("            return route_", e.ns, "::", func, "(", handler_args(st.state_vars), ");"));
                cpp.push_back("        };");
            } else {
                cpp.push_back(cat("        r.", field, " = [](const web::Request& req) -> web::Response {"));
                if (st.needed) cpp.push_back("            auto& ctx = web::Context::current();");
                emit_state_loading(cpp, st);
                cpp.push_back(cat("            auto resp = web::to_response(route_", e.ns, "::", func, "(",
                                  handler_args(st.state_vars), "));"));
                for (const auto& [mode, type, var] : st.flashes) {
                    (void)var;
                    if (mode == "Flash") {
                        cpp.push_back(cat("            if (req.cookies.get(\"_cw_", type, "\") != \"\") resp.cookies[\"_cw_",
                                          type, "\"] = \"\";"));
                    }
                }
                cpp.push_back("            return resp;");
                cpp.push_back("        };");
            }
        }

        if (std::regex_search(src, Re(R"(render_sse\s*\()"))) {
            cpp.push_back("        r.sse = [](const web::Request& req, web::SSEWriter& sse) {");
            StateInfo st = extract_state_vars("render_sse", src);
            if (st.needed || !st.flashes.empty()) cpp.push_back("            auto& ctx = web::Context::current();");
            emit_state_loading(cpp, st);
            cpp.push_back(cat("            route_", e.ns, "::render_sse(", handler_args(st.state_vars, ", sse"), ");"));
            cpp.push_back("        };");
        }

        // WebSocket handler: render_ws(req[, states...], conn) → void
        if (std::regex_search(src, Re(R"(render_ws\s*\()"))) {
            cpp.push_back("        r.ws = [](const web::Request& req, web::ws::Conn& conn) {");
            StateInfo st = extract_state_vars("render_ws", src);
            if (st.needed || !st.flashes.empty()) cpp.push_back("            auto& ctx = web::Context::current();");
            emit_state_loading(cpp, st);
            cpp.push_back(cat("            route_", e.ns, "::render_ws(", handler_args(st.state_vars, ", conn"), ");"));
            cpp.push_back("        };");
        }

        if (std::regex_search(src, Re(R"(\bapi_guards\s*\(\s*\))"))) {
            cpp.push_back(cat("        r.guards = route_", e.ns, "::api_guards();"));
        } else if (std::regex_search(src, Re(R"(\broute_guards\s*\(\s*\))"))) {
            cpp.push_back(cat("        r.guards = route_", e.ns, "::route_guards();"));
        }

        cpp.push_back("        out.push_back(r);");
        cpp.push_back("    }");
    }

    cpp.push_back("}");
    cpp.push_back("} // namespace routes");
    write_lines(out_c, cpp);

    patch_compile_commands(ctx, out_c, site);
    return 0;
}

}  // namespace revet::codegen

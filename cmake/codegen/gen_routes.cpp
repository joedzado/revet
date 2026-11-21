// app/routes/**/*.cpp -> routes.hpp (urls::) and routes.cpp (route table).
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <tuple>

#include "common.hpp"
#include "node.hpp"

namespace revet::codegen {
namespace {

// ── Path → route / namespace ────────────────────────────────────────────────

struct Entry {
    std::string route;
    std::string ns;
    fs::path file;
};

// rel is relative to app/routes.
auto route_and_ns(const fs::path& rel) -> std::pair<std::string, std::string> {
    std::vector<std::string> parts;
    for (const auto& e : rel) {
        parts.push_back(e.string());
    }
    std::string& last = parts.back();
    size_t dot = last.rfind('.');
    if (dot != std::string::npos && dot != 0) {
        last = last.substr(0, dot); // with_suffix("")
    }
    if (parts.back() == "index") {
        parts.pop_back();
    }

    std::vector<std::string> route_parts;
    std::vector<std::string> ns_parts;
    for (const auto& part : parts) {
        if (auto m = ctre::match<R"RE(\[([^\n]+)\])RE">(part)) { // [id].cpp -> :id
            route_parts.push_back(":" + m.get<1>().to_string());
            ns_parts.push_back("_" + m.get<1>().to_string() + "_");
        } else {
            route_parts.push_back(part);
            std::string p = part;
            std::ranges::replace(p, '-', '_');
            ns_parts.push_back(p);
        }
    }
    std::string route = "/" + join(route_parts, "/");
    std::string ns = join(ns_parts, "_");
    std::ranges::replace(ns, '-', '_');
    if (ns.empty()) {
        ns = "index";
    }
    return {route, ns};
}

auto scan_model_names(const fs::path& models_dir) -> std::set<std::string> {
    std::set<std::string> names;
    for (const auto& f : glob(models_dir, ".hpp")) {
        std::string content = read_text(f);
        for (auto m : ctre::search_all<
                 R"RE((?:\[\[\s*revet::model[^\]]*\]\]\s*struct|struct\s*\[\[\s*revet::model[^\]]*\]\])\s+(\w+))RE">(
                 std::string_view(content))) {
            names.insert(m.get<1>().to_string());
        }
    }
    return names;
}

// ── Per-route emission helpers ──────────────────────────────────────────────

struct StateInfo {
    bool needed = false;
    std::vector<std::string> state_vars;
    std::vector<std::tuple<std::string, std::string, std::string>> flashes; // (mode, type, var)
};

auto extract_state_vars(const std::string& func_name, const std::string& src) -> StateInfo {
    StateInfo info;
    std::string_view args;
    if (auto head = search_after<R"RE(\s*\(([\s\S]*?)\))RE">(src, func_name)) {
        args = head.get<1>().to_view();
    }

    std::set<std::pair<std::string, std::string>> needed;
    for (auto m : find_state_types(args)) {
        needed.insert({m.get<1>().to_string(), m.get<2>().to_string()});
    }
    info.needed = !needed.empty();
    for (const auto& [mode, type] : needed) {
        std::string var = "state_" + type;
        info.state_vars.push_back(var);
        info.flashes.emplace_back(mode, type, var);
    }
    return info;
}

auto handler_args(const std::vector<std::string>& state_vars, const std::string& suffix = "") -> std::string {
    std::string out = "req";
    for (const auto& v : state_vars) {
        out += ", " + v;
    }
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
    std::string param_type;  // empty when none
    std::string return_type; // empty when none
    bool is_async = false;
};

auto api_detect(const std::string& func_name, const std::string& src, const std::set<std::string>& names) -> ApiDetect {
    ApiDetect d;
    if (auto pm = search_after<R"RE(\s*\(\s*const\s+([\w:]+)\s*&)RE">(src, func_name)) {
        std::string raw = pm.get<1>().to_string();
        size_t sep = raw.rfind("::");
        if (sep != std::string::npos) {
            raw = raw.substr(sep + 2);
        }
        if (names.contains(raw)) {
            d.param_type = raw;
        }
    }

    d.is_async = static_cast<bool>(search_after<R"RE(\s*\([^)]*\)\s*->\s*(?:web::)?Task\s*<)RE">(src, func_name));

    if (auto rm =
            search_after<R"RE(\s*\([^)]*\)\s*->\s*(?:(?:web::)?Task\s*<\s*)?(std::vector\s*<\s*(\w+)\s*>|(\w+)))RE">(
                src, func_name)) {
        if (rm.get<2>() && names.contains(rm.get<2>().to_string())) {
            d.return_type = "std::vector<" + rm.get<2>().to_string() + ">";
        } else if (rm.get<3>() && names.contains(rm.get<3>().to_string())) {
            d.return_type = rm.get<3>().to_string();
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
        out.emplace_back(
            R"(            if (!std::string_view(req.headers.get("content-type")).starts_with("application/json")))");
        out.push_back(cat("                ", ret,
                          " web::api::error(web::status::bad_request, \"Content-Type: application/json required\");"));
        out.emplace_back("            if (req.raw_body.empty())");
        out.push_back(
            cat("                ", ret, " web::api::error(web::status::bad_request, \"request body required\");"));
        out.push_back(cat("            ", d.param_type, " body{};"));
        out.emplace_back("            try {");
        out.emplace_back("                revet::json::Document doc(req.raw_body);");
        out.push_back(cat("                body = revet::json::from_json<", d.param_type, ">(doc);"));
        out.emplace_back("            } catch (const revet::json::ParseError& e) {");
        out.push_back(cat("                ", ret, " web::api::error(web::status::bad_request, e.what());"));
        out.emplace_back("            }");
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
    out.emplace_back("        };");
}

// ── compile_commands.json patch ─────────────────────────────────────────────

auto str_node(const std::string& s) -> Node {
    Node n;
    n.kind = Node::Kind::String;
    n.s = s;
    return n;
}

auto resolved(const fs::path& p) -> fs::path {
    std::error_code ec;
    fs::path r = fs::weakly_canonical(p, ec);
    return ec ? p : r;
}

void patch_compile_commands(const Ctx& ctx, const fs::path& out_c, const fs::path& site) {
    fs::path cc = ctx.proj / "build" / "compile_commands.json";
    if (!file_exists(cc)) {
        return;
    }

    Node db;
    try {
        db = parse_json(read_text(cc));
    } catch (const std::exception& e) {
        std::cerr << "gen_routes: warning: failed to parse " << cc.string() << ": " << e.what() << "\n";
        return;
    }
    if (db.kind != Node::Kind::Array) {
        return;
    }

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
    if (ref == nullptr) {
        return;
    }
    Node ref_copy = *ref;

    fs::path gen_dir = resolved(ctx.proj / "build" / "generated");
    const Lines always_headers = {"routes.hpp",  "components.hpp", "styles.hpp", "states.hpp",
                                  "toggles.hpp", "i18n.hpp",       "uploads.hpp"};
    const Lines optional_headers = {"queries.hpp", "resources.hpp", "models.hpp", "forms.hpp", "apis.hpp"};
    std::vector<fs::path> headers;
    for (const auto& h : always_headers) {
        headers.push_back(gen_dir / h);
    }
    for (const auto& h : optional_headers) {
        if (file_exists(gen_dir / h)) {
            headers.push_back(gen_dir / h);
        }
    }

    Lines include_pairs; // arguments form: "-include", "<abs>"
    Lines include_flags; // command form:    "-include <abs>"
    for (const auto& h : headers) {
        include_pairs.emplace_back("-include");
        include_pairs.push_back(h.string());
        include_flags.push_back("-include " + h.string());
    }

    auto make_entry = [&](const fs::path& src_file) -> Node {
        Node entry = ref_copy;
        fs::path src_abs = resolved(src_file);
        std::string stem = src_file.stem().string();
        entry.set("file", str_node(src_abs.string()));
        if (const Node* cmd_node = ref_copy.find("command")) {
            std::string cmd = replace_all<R"RE(-o\s+\S+)RE">(cmd_node->s, "-o " + stem + ".o");
            std::string extra = " " + join(include_flags, " ") + " -Wno-unused-function";
            size_t sp = cmd.rfind(' ');
            std::string prefix = sp == std::string::npos ? "" : cmd.substr(0, sp);
            entry.set("command", str_node(prefix + extra + " " + src_abs.string()));
        } else if (const Node* args_node = ref_copy.find("arguments")) {
            Node args;
            args.kind = Node::Kind::Array;
            for (const auto& a : args_node->items) {
                args.items.push_back(a);
            }
            for (size_t i = 0; i < args.items.size(); ++i) {
                if (args.items[i].kind == Node::Kind::String && args.items[i].s == "-o" && i + 1 < args.items.size()) {
                    args.items[i + 1] = str_node(stem + ".o");
                }
            }
            if (!args.items.empty()) {
                args.items.back() = str_node(src_abs.string());
            }
            for (const auto& p : include_pairs) {
                args.items.push_back(str_node(p));
            }
            args.items.push_back(str_node("-Wno-unused-function"));
            entry.set("arguments", args);
        }
        return entry;
    };

    std::set<fs::path> site_files;
    for (const auto& f : glob(site, ".cpp", true)) {
        if (f.filename() != "site_init.cpp") {
            site_files.insert(resolved(f));
        }
    }

    Node out;
    out.kind = Node::Kind::Array;
    for (const auto& e : db.items) {
        if (static_cast<unsigned int>(site_files.contains(file_of(e))) == 0U) {
            out.items.push_back(e);
        }
    }
    for (const auto& f : site_files) {
        out.items.push_back(make_entry(f));
    }

    write_file(cc, dump_json_indent2(out));
}

} // namespace

auto gen_routes(const Ctx& ctx) -> int {
    // routes.cpp includes these asset headers, so they must exist first.
    gen_htmx(ctx);
    gen_optimistic(ctx);
    gen_scripts(ctx); // scripts.hpp, listed as a Routes output

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
        entries.push_back({.route = route, .ns = ns, .file = f});
    }
    std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) -> bool {
        int ka = a.route.contains(':') ? 1 : 0;
        int kb = b.route.contains(':') ? 1 : 0;
        if (ka != kb) {
            return ka < kb;
        }
        return a.route < b.route;
    });

    // ── urls:: entries ──
    const std::string url_param_t = tmpl(ctx, "routes_url_param.tmpl");
    const std::string url_static_t = tmpl(ctx, "routes_url_static.tmpl");
    const std::string static_asset_t = tmpl(ctx, "routes_static_asset.tmpl");
    const std::string toggle_handler_t = tmpl(ctx, "routes_toggle_handler.tmpl");

    Lines hpp_url_entries;
    for (const auto& e : entries) {
        Lines params;
        for (auto m : ctre::search_all<R"RE(:(\w+))RE">(std::string_view(e.route))) {
            params.push_back(m.get<1>().to_string());
        }
        if (!params.empty()) {
            std::string param_args;
            for (size_t i = 0; i < params.size(); ++i) {
                if (i != 0U) {
                    param_args += ", ";
                }
                param_args += "std::string_view " + params[i];
            }
            std::string fmt_route = replace_all<R"RE(:\w+)RE">(e.route, "{}");
            // Python: args are url-encoded with sanitize::url_encode when building a URL.
            Lines wrapped;
            for (const auto& p : params) {
                wrapped.push_back("sanitize::url_encode(" + p + ")");
            }
            std::string encoded_args = join(wrapped, ", ");
            hpp_url_entries.push_back(subst(
                url_param_t,
                {{"ns", e.ns}, {"param_args", param_args}, {"fmt_route", fmt_route}, {"args_list", encoded_args}}));
        } else {
            hpp_url_entries.push_back(subst(url_static_t, {{"ns", e.ns}, {"route", e.route}}));
        }
    }

    // ── includes ──
    std::set<std::string> global_includes = {"#include \"routes.hpp\"", "#include \"styles.hpp\"", "#include <vector>"};
    for (const auto& hpp : glob(ctx.proj / "types", ".hpp")) {
        global_includes.insert(rel_include(hpp, ctx.proj));
    }
    if (have_models) {
        global_includes.insert("#include \"models.hpp\"");
    }
    if (have_apis) {
        global_includes.insert("#include \"apis.hpp\"");
    }
    if (file_exists(models_site)) {
        global_includes.insert(rel_include(models_site, ctx.proj));
    }

    // ── site bodies: includes hoisted, the rest kept (ordered map by namespace) ──
    std::vector<std::pair<std::string, std::string>> site_contents;
    for (const auto& e : entries) {
        std::string content = read_text(e.file);
        Lines stripped;
        for (const auto& line : splitlines(content)) {
            std::string s = strip(line);
            if (starts_with(s, "#include")) {
                if (s.contains("models.hpp") || s.contains("json_models.hpp") || s.contains("web.hpp") ||
                    s.contains("routes.hpp") || s.contains("styles.hpp")) {
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
        if (!replaced) {
            site_contents.emplace_back(e.ns, body);
        }
    }

    // ── state toggles: [[revet::toggle("/route", default)]] bool name ──
    std::vector<std::pair<std::string, std::string>> toggle_routes; // (route, name)
    for (const auto& f : glob(ctx.proj / "app" / "states", ".hpp")) {
        std::string content = read_text(f);
        for (auto m : find_toggles(std::string_view(content))) {
            toggle_routes.emplace_back(m.get<1>().to_string(), m.get<3>().to_string());
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
    hpp.emplace_back("namespace urls {");
    for (const auto& l : hpp_url_entries) {
        hpp.push_back(l);
    }
    hpp.emplace_back("}");
    for (const auto& l :
         Lines{"", "namespace revet {", cat("inline constexpr std::string_view version = \"", version_str, "\";"),
               "} // namespace revet"}) {
        hpp.push_back(l);
    }
    write_lines(out_h, hpp);

    // ── routes.cpp ──
    Lines cpp;
    for (const auto& inc : global_includes) {
        cpp.push_back(inc); // std::set is already sorted
    }
    cpp.emplace_back("#include \"htmx_asset.hpp\"");
    cpp.emplace_back("#include \"optimistic_asset.hpp\"");
    cpp.emplace_back("#include \"scripts.hpp\"");
    cpp.emplace_back("#include \"server.hpp\"  // revet.toml max_body, applied at static init");
    cpp.emplace_back("");

    for (const auto& [ns, content] : site_contents) {
        cpp.push_back("namespace route_" + ns + " {");
        cpp.push_back(content);
        cpp.emplace_back("}");
        cpp.emplace_back("");
    }

    cpp.emplace_back("namespace routes {");
    cpp.emplace_back("void setup(std::vector<web::Route>& out) {");
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
        cpp.emplace_back("    {");
        cpp.push_back(cat("        web::Route r; r.path = \"", e.route, "\";"));

        static const std::vector<std::pair<std::string, std::string>> method_field = {
            {"get", "get"}, {"post", "post"}, {"put", "put"}, {"delete", "del"}, {"patch", "patch"}};
        for (const auto& [method, field] : method_field) {
            std::string func = "render_" + method;
            if (!search_after<R"RE(\s*\()RE">(src, func)) {
                continue;
            }

            if (is_api) {
                emit_api_handler(cpp, e.ns, method, field, src, model_names);
                continue;
            }

            StateInfo st = extract_state_vars(func, src);
            // `Task<Response> func(` or `func(...) -> web::Task<Response>`
            bool is_async = static_cast<bool>(
                search_after<R"RE(\s*\([^)]*\)\s*->\s*web::Task\s*<\s*(?:web::)?Response\s*>)RE">(src, func));
            for (auto m :
                 ctre::search_all<R"RE(Task\s*<\s*(?:web::)?Response\s*>\s+(\w+)\s*\()RE">(std::string_view(src))) {
                if (m.get<1>().to_view() == func) {
                    is_async = true;
                }
            }

            if (is_async) {
                cpp.push_back(
                    cat("        r.async_", field, " = [](const web::Request& req) -> web::Task<web::Response> {"));
                if (st.needed) {
                    cpp.emplace_back("            auto& ctx = web::Context::current();");
                }
                emit_state_loading(cpp, st);
                cpp.push_back(
                    cat("            return route_", e.ns, "::", func, "(", handler_args(st.state_vars), ");"));
                cpp.emplace_back("        };");
            } else {
                cpp.push_back(cat("        r.", field, " = [](const web::Request& req) -> web::Response {"));
                if (st.needed) {
                    cpp.emplace_back("            auto& ctx = web::Context::current();");
                }
                emit_state_loading(cpp, st);
                cpp.push_back(cat("            auto resp = web::to_response(route_", e.ns, "::", func, "(",
                                  handler_args(st.state_vars), "));"));
                for (const auto& [mode, type, var] : st.flashes) {
                    (void)var;
                    if (mode == "Flash") {
                        cpp.push_back(cat("            if (req.cookies.get(\"_cw_", type,
                                          R"(") != "") resp.cookies["_cw_)", type, R"("] = "";)"));
                    }
                }
                cpp.emplace_back("            return resp;");
                cpp.emplace_back("        };");
            }
        }

        if (ctre::search<R"RE(render_sse\s*\()RE">(src)) {
            cpp.emplace_back("        r.sse = [](const web::Request& req, web::SSEWriter& sse) {");
            StateInfo st = extract_state_vars("render_sse", src);
            if (st.needed || !st.flashes.empty()) {
                cpp.emplace_back("            auto& ctx = web::Context::current();");
            }
            emit_state_loading(cpp, st);
            cpp.push_back(cat("            route_", e.ns, "::render_sse(", handler_args(st.state_vars, ", sse"), ");"));
            cpp.emplace_back("        };");
        }

        // WebSocket handler: render_ws(req[, states...], conn) → void
        if (ctre::search<R"RE(render_ws\s*\()RE">(src)) {
            cpp.emplace_back("        r.ws = [](const web::Request& req, web::ws::Conn& conn) {");
            StateInfo st = extract_state_vars("render_ws", src);
            if (st.needed || !st.flashes.empty()) {
                cpp.emplace_back("            auto& ctx = web::Context::current();");
            }
            emit_state_loading(cpp, st);
            cpp.push_back(cat("            route_", e.ns, "::render_ws(", handler_args(st.state_vars, ", conn"), ");"));
            cpp.emplace_back("        };");
        }

        if (ctre::search<R"RE(\bapi_guards\s*\(\s*\))RE">(src)) {
            cpp.push_back(cat("        r.guards = route_", e.ns, "::api_guards();"));
        } else if (ctre::search<R"RE(\broute_guards\s*\(\s*\))RE">(src)) {
            cpp.push_back(cat("        r.guards = route_", e.ns, "::route_guards();"));
        }

        cpp.emplace_back("        out.push_back(r);");
        cpp.emplace_back("    }");
    }

    cpp.emplace_back("}");
    cpp.emplace_back("} // namespace routes");
    write_lines(out_c, cpp);

    patch_compile_commands(ctx, out_c, site);
    return 0;
}

} // namespace revet::codegen

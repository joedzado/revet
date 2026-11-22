// app/routes/**/*.cpp -> routes.hpp (urls::) and routes.cpp (route table).
#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <ranges>
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
    auto parts = rel | std::views::transform([](const fs::path& e) -> std::string { return e.string(); }) |
                 std::ranges::to<Lines>();
    std::string& last = parts.back();
    const size_t dot = last.rfind('.');
    if (dot != std::string::npos && dot != 0) {
        last = last.substr(0, dot); // with_suffix("")
    }
    if (parts.back() == "index") {
        parts.pop_back();
    }

    // [id].cpp -> :id in the route, _id_ in the namespace.
    auto param = [](const std::string& part) -> std::optional<std::string> {
        if (const auto m = ctre::match<R"RE(\[([^\n]+)\])RE">(part)) {
            return m.get<1>().to_string();
        }
        return std::nullopt;
    };
    const auto route_parts = parts | std::views::transform([&](const std::string& part) -> std::string {
                                 auto p = param(part);
                                 return p ? ":" + *p : part;
                             }) |
                             std::ranges::to<Lines>();
    const auto ns_parts = parts | std::views::transform([&](const std::string& part) -> std::string {
                              if (auto p = param(part)) {
                                  return "_" + *p + "_";
                              }
                              // Dots and other non-ident chars show up in real paths
                              // (/.well-known/assetlinks.json); C++ namespaces cannot hold them.
                              std::string ns;
                              ns.reserve(part.size());
                              for (char c : part) {
                                  ns.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_');
                              }
                              return ns;
                          }) |
                          std::ranges::to<Lines>();
    const std::string route = "/" + join(route_parts, "/");
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
        auto found = captures<
            R"RE((?:\[\[\s*revet::model[^\]]*\]\]\s*struct|struct\s*\[\[\s*revet::model[^\]]*\]\])\s+(\w+))RE">(
            read_text(f));
        names.insert(found.begin(), found.end());
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
    if (const auto head = search_after<R"RE(\s*\(([\s\S]*?)\))RE">(src, func_name)) {
        args = head.get<1>().to_view();
    }

    auto needed = find_state_types(args) |
                  std::views::transform([](const auto& m) -> std::pair<std::string, std::string> {
                      return {m.template get<1>().to_string(), m.template get<2>().to_string()};
                  }) |
                  std::ranges::to<std::set<std::pair<std::string, std::string>>>();
    info.needed = !needed.empty();
    info.state_vars = needed | std::views::values |
                      std::views::transform([](const std::string& type) -> std::string { return "state_" + type; }) |
                      std::ranges::to<Lines>();
    info.flashes = needed |
                   std::views::transform([](const auto& n) -> std::tuple<std::string, std::string, std::string> {
                       return {n.first, n.second, "state_" + n.second};
                   }) |
                   std::ranges::to<std::vector<std::tuple<std::string, std::string, std::string>>>();
    return info;
}

auto handler_args(const std::vector<std::string>& state_vars, const std::string& suffix = "") -> std::string {
    Lines args = {"req"};
    args.insert(args.end(), state_vars.begin(), state_vars.end());
    return join(args, ", ") + suffix;
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
    if (const auto pm = search_after<R"RE(\s*\(\s*const\s+([\w:]+)\s*&)RE">(src, func_name)) {
        std::string raw = pm.get<1>().to_string();
        const size_t sep = raw.rfind("::");
        if (sep != std::string::npos) {
            raw = raw.substr(sep + 2);
        }
        if (names.contains(raw)) {
            d.param_type = raw;
        }
    }

    d.is_async = static_cast<bool>(search_after<R"RE(\s*\([^)]*\)\s*->\s*(?:web::)?Task\s*<)RE">(src, func_name));

    if (const auto rm =
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
    const ApiDetect d = api_detect("render_" + method, src, names);

    const std::string ret = d.is_async ? "co_return" : "return";
    const std::string wait = d.is_async ? "co_await " : "";
    const std::string handle = d.is_async ? "r.async_" + field : "r." + field;
    const std::string sig = d.is_async ? "-> web::Task<web::Response>" : "-> web::Response";

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
    const fs::path r = fs::weakly_canonical(p, ec);
    return ec ? p : r;
}

void patch_compile_commands(const Ctx& ctx, const fs::path& out_c, const fs::path& site) {
    const fs::path cc = ctx.proj / "build" / "compile_commands.json";
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
    const auto ref = std::ranges::find_if(db.items, [&](const Node& e) -> bool { return file_of(e) == routes_cpp; });
    if (ref == db.items.end()) {
        return;
    }
    Node ref_copy = *ref;

    fs::path gen_dir = resolved(ctx.proj / "build" / "generated");
    const Lines always_headers = {"routes.hpp",  "components.hpp", "styles.hpp", "states.hpp",
                                  "toggles.hpp", "i18n.hpp",       "uploads.hpp"};
    const Lines optional_headers = {"queries.hpp", "resources.hpp", "models.hpp", "forms.hpp", "apis.hpp"};
    auto in_gen = [&](const std::string& h) -> fs::path { return gen_dir / h; };
    auto headers = always_headers | std::views::transform(in_gen) | std::ranges::to<std::vector<fs::path>>();
    std::ranges::copy(optional_headers | std::views::transform(in_gen) | std::views::filter(file_exists),
                      std::back_inserter(headers));
    // command form: "-include <abs>"
    auto include_flags =
        headers | std::views::transform([](const fs::path& h) -> std::string { return "-include " + h.string(); }) |
        std::ranges::to<Lines>();

    const auto make_entry = [&](const fs::path& src_file) -> Node {
        Node entry = ref_copy;
        const fs::path src_abs = resolved(src_file);
        const std::string stem = src_file.stem().string();
        entry.set("file", str_node(src_abs.string()));
        if (const Node* cmd_node = ref_copy.find("command")) {
            const std::string cmd = replace_all<R"RE(-o\s+\S+)RE">(cmd_node->s, "-o " + stem + ".o");
            const std::string extra = " " + join(include_flags, " ") + " -Wno-unused-function";
            const size_t sp = cmd.rfind(' ');
            const std::string prefix = sp == std::string::npos ? "" : cmd.substr(0, sp);
            entry.set("command", str_node(prefix + extra + " " + src_abs.string()));
        } else if (const Node* args_node = ref_copy.find("arguments")) {
            Node args = *args_node;
            const auto is_o = [](const Node& a) -> bool { return a.kind == Node::Kind::String && a.s == "-o"; };
            if (const auto o = std::ranges::find_if(args.items, is_o);
                o != args.items.end() && std::next(o) != args.items.end()) {
                *std::next(o) = str_node(stem + ".o");
            }
            if (!args.items.empty()) {
                args.items.back() = str_node(src_abs.string());
            }
            // arguments form: "-include", "<abs>"
            std::ranges::for_each(headers, [&](const fs::path& h) -> void {
                args.items.push_back(str_node("-include"));
                args.items.push_back(str_node(h.string()));
            });
            args.items.push_back(str_node("-Wno-unused-function"));
            entry.set("arguments", args);
        }
        return entry;
    };

    auto site_files = glob(site, ".cpp", true) |
                      std::views::filter([](const fs::path& f) -> bool { return f.filename() != "site_init.cpp"; }) |
                      std::views::transform(resolved) | std::ranges::to<std::set<fs::path>>();

    Node out;
    out.kind = Node::Kind::Array;
    std::ranges::copy_if(db.items, std::back_inserter(out.items),
                         [&](const Node& e) -> bool { return !site_files.contains(file_of(e)); });
    std::ranges::transform(site_files, std::back_inserter(out.items), make_entry);

    write_file(cc, dump_json_indent2(out));
}

} // namespace

auto gen_routes(const Ctx& ctx) -> int {
    // routes.cpp includes these asset headers, so they must exist first.
    gen_htmx(ctx);
    gen_optimistic(ctx);
    gen_scripts(ctx); // scripts.hpp, listed as a Routes output

    fs::path site = ctx.proj / "app" / "routes";
    const fs::path models_dir = ctx.proj / "models";
    const fs::path models_site = site / "models.hpp";
    const fs::path apis_dir = ctx.proj / "app" / "apis";
    const bool have_models = has_models(models_dir);
    const bool have_apis = !glob(apis_dir, ".hpp").empty();
    const std::set<std::string> model_names = scan_model_names(models_dir);

    const fs::path out_h = setup(ctx, "routes.hpp");
    const fs::path out_c = out_h.parent_path() / "routes.cpp";

    // Entries: sorted paths, then static routes before parameterized ones (stable).
    auto entries = glob(site, ".cpp", true) | std::views::transform([&](const fs::path& f) -> Entry {
                       auto [route, ns] = route_and_ns(fs::relative(f, site));
                       return {.route = route, .ns = ns, .file = f};
                   }) |
                   std::ranges::to<std::vector<Entry>>();
    std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) -> bool {
        const int ka = a.route.contains(':') ? 1 : 0;
        const int kb = b.route.contains(':') ? 1 : 0;
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

    auto prefixed = [](const Lines& items, std::string_view before, std::string_view after = "") -> Lines {
        return items |
               std::views::transform([&](const std::string& p) -> std::string { return cat(before, p, after); }) |
               std::ranges::to<Lines>();
    };
    auto url_entry = [&](const Entry& e) -> std::string {
        const Lines params = captures<R"RE(:(\w+))RE">(e.route);
        if (params.empty()) {
            return subst(url_static_t, {{"ns", e.ns}, {"route", e.route}});
        }
        // Args are url-encoded with sanitize::url_encode when building a URL.
        return subst(url_param_t, {{"ns", e.ns},
                                   {"param_args", join(prefixed(params, "std::string_view "), ", ")},
                                   {"fmt_route", replace_all<R"RE(:\w+)RE">(e.route, "{}")},
                                   {"args_list", join(prefixed(params, "sanitize::url_encode(", ")"), ", ")}});
    };
    auto hpp_url_entries = entries | std::views::transform(url_entry) | std::ranges::to<Lines>();

    // ── includes ──
    std::set<std::string> global_includes = {"#include \"routes.hpp\"", "#include \"styles.hpp\"", "#include <vector>"};
    std::ranges::transform(glob(ctx.proj / "types", ".hpp"), std::inserter(global_includes, global_includes.end()),
                           [&](const fs::path& hpp) -> std::string { return rel_include(hpp, ctx.proj); });
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
        Lines lines = splitlines(read_text(e.file));
        auto is_include = [](const std::string& line) -> bool { return strip(line).starts_with("#include"); };
        // Hoisted, except headers routes.cpp already has.
        auto hoisted = [](const std::string& inc) -> bool {
            constexpr auto k_provided = std::to_array<std::string_view>(
                {"models.hpp", "json_models.hpp", "web.hpp", "routes.hpp", "styles.hpp"});
            return std::ranges::none_of(k_provided, [&](std::string_view h) -> bool { return inc.contains(h); });
        };
        std::ranges::copy(lines | std::views::filter(is_include) | std::views::transform(strip) |
                              std::views::filter(hoisted),
                          std::inserter(global_includes, global_includes.end()));
        std::string body = join(lines | std::views::filter(std::not_fn(is_include)) | std::ranges::to<Lines>(), "\n");
        if (const auto it = std::ranges::find(site_contents, e.ns, &std::pair<std::string, std::string>::first);
            it != site_contents.end()) {
            it->second = body;
        } else {
            site_contents.emplace_back(e.ns, body);
        }
    }

    // ── state toggles: [[revet::toggle("/route", default)]] bool name ──
    std::vector<std::pair<std::string, std::string>> toggle_routes; // (route, name)
    for (const auto& f : glob(ctx.proj / "app" / "states", ".hpp")) {
        const std::string content = read_text(f);
        std::ranges::transform(find_toggles(std::string_view(content)), std::back_inserter(toggle_routes),
                               [](const auto& m) -> std::pair<std::string, std::string> {
                                   return {m.template get<1>().to_string(), m.template get<3>().to_string()};
                               });
    }
    std::ranges::transform(toggle_routes, std::back_inserter(hpp_url_entries), [&](const auto& t) -> std::string {
        return subst(url_static_t, {{"ns", t.second}, {"route", t.first}});
    });

    // ── routes.hpp ──
    const fs::path version_file = ctx.proj.parent_path() / "VERSION";
    std::string version_str = "unknown";
    if (file_exists(version_file)) {
        const std::string raw = strip(read_text(version_file));
        version_str = raw;
    }
    Lines hpp = file_header({"#include <string_view>", "#include <format>", "#include <revet/web.hpp>",
                             "#include <revet/upload.hpp>", "#include \"toggles.hpp\"", "#include \"components.hpp\"",
                             "#include \"i18n.hpp\"", "#include \"uploads.hpp\"", "#include \"resources.hpp\""});
    hpp.emplace_back("namespace urls {");
    hpp.insert(hpp.end(), hpp_url_entries.begin(), hpp_url_entries.end());
    hpp.insert(hpp.end(),
               {"}", "", "namespace revet {", cat("inline constexpr std::string_view version = \"", version_str, "\";"),
                "} // namespace revet"});
    write_lines(out_h, hpp);

    // ── routes.cpp ──
    Lines cpp(global_includes.begin(), global_includes.end()); // std::set is already sorted
    cpp.emplace_back("#include \"htmx_asset.hpp\"");
    cpp.emplace_back("#include \"optimistic_asset.hpp\"");
    cpp.emplace_back("#include \"scripts.hpp\"");
    cpp.emplace_back("#include \"server.hpp\"  // revet.toml max_body, applied at static init");
    cpp.emplace_back("");

    for (const auto& [ns, content] : site_contents) {
        cpp.push_back("namespace route_" + ns + " {");
        cpp.emplace_back("namespace log = web::log; // ::log is the math function on glibc");
        cpp.push_back(content);
        cpp.emplace_back("}");
        cpp.emplace_back("");
    }

    cpp.emplace_back("namespace routes {");
    cpp.emplace_back("void setup(std::vector<web::Route>& out) {");
    const auto emit = [&](const std::string& t, const char* path_key, const char* name_key) -> auto {
        return [&t, path_key, name_key](const std::pair<std::string, std::string>& r) -> std::string {
            return subst(t, {{path_key, r.first}, {name_key, r.second}});
        };
    };
    const std::vector<std::pair<std::string, std::string>> framework_assets = {
        {"/_revet/htmx.js", "htmx_js"}, {"/_revet/optimistic.js", "optimistic_js"}};
    std::ranges::transform(framework_assets, std::back_inserter(cpp), emit(static_asset_t, "path", "symbol"));
    // App scripts (app/scripts/*.js): the URL carries a content hash, so they can be cached for good.
    const std::string app_script_t = tmpl(ctx, "routes_app_script.tmpl");
    std::ranges::transform(app_script_routes(ctx), std::back_inserter(cpp), emit(app_script_t, "path", "symbol"));
    std::ranges::transform(toggle_routes, std::back_inserter(cpp), emit(toggle_handler_t, "path", "name"));

    for (const auto& e : entries) {
        std::string src = read_text(e.file);
        const bool is_api = starts_with(e.route, "/api/") || e.route == "/api";
        cpp.emplace_back("    {");
        cpp.push_back(cat("        web::Route r; r.path = \"", e.route, "\";"));

        static const std::vector<std::pair<std::string, std::string>> method_field = {
            {"get", "get"}, {"post", "post"}, {"put", "put"}, {"delete", "del"}, {"patch", "patch"}};
        for (const auto& [method, field] : method_field) {
            const std::string func = "render_" + method;
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
            is_async = is_async || std::ranges::contains(
                                       captures<R"RE(Task\s*<\s*(?:web::)?Response\s*>\s+(\w+)\s*\()RE">(src), func);

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
                std::ranges::transform(
                    st.flashes | std::views::filter([](const auto& f) -> bool { return std::get<0>(f) == "Flash"; }),
                    std::back_inserter(cpp), [](const auto& f) -> std::string {
                        const auto& type = std::get<1>(f);
                        return cat("            if (req.cookies.get(\"_cw_", type, R"(") != "") resp.cookies["_cw_)",
                                   type, R"("] = "";)");
                    });
                cpp.emplace_back("            return resp;");
                cpp.emplace_back("        };");
            }
        }

        if (ctre::search<R"RE(render_sse\s*\()RE">(src)) {
            cpp.emplace_back("        r.sse = [](const web::Request& req, web::SSEWriter& sse) {");
            const StateInfo st = extract_state_vars("render_sse", src);
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
            const StateInfo st = extract_state_vars("render_ws", src);
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

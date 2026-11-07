#!/usr/bin/env python3
import sys, re, json
from pathlib import Path
from codegen import tmpl as _tmpl, write_lines, has_models, rel_include, STATE_FLASH_RE

_TOGGLE_HANDLER  = _tmpl("routes_toggle_handler.tmpl")
_URL_PARAM       = _tmpl("routes_url_param.tmpl")
_URL_STATIC      = _tmpl("routes_url_static.tmpl")
_STATIC_ASSET    = _tmpl("routes_static_asset.tmpl")

# Regex to detect [[revet::model]] struct names in models/*.hpp
_MODEL_ATTR_RE = re.compile(
    r'(?:\[\[\s*revet::model[^\]]*\]\]\s*struct|struct\s*\[\[\s*revet::model[^\]]*\]\])\s+(\w+)'
)

def scan_model_names(models_dir: Path) -> set[str]:
    """Return set of [[revet::model]] struct names from models/*.hpp."""
    if not models_dir.exists():
        return set()
    names: set[str] = set()
    for f in models_dir.glob("*.hpp"):
        for m in _MODEL_ATTR_RE.finditer(f.read_text()):
            names.add(m.group(1))
    return names


def _emit_state_loading(out, flashes_meta):
    for mode, type_, var_name in flashes_meta:
        out.append(f"            web::{mode}<{type_}> {var_name};")
        out.append(f"            if (auto c = req.cookies.get(\"_cw_{type_}\"); !c.empty())")
        out.append(f"                {var_name}.data = web::states::unpack_{type_}(c);")
        out.append(f"            ctx.states[typeid({type_})] = {var_name};")


def _handler_args(state_vars, suffix=""):
    return "req" + "".join(f", {v}" for v in state_vars) + suffix


def main():
    # gen_htmx and gen_optimistic must run first — routes.cpp includes their asset headers
    import subprocess
    for script in ("gen_htmx.py", "gen_optimistic.py"):
        subprocess.run(
            [sys.executable, str(Path(__file__).parent / script), sys.argv[1]],
            check=True
        )

    PROJ = Path(sys.argv[1])
    SITE = PROJ / "app" / "routes"
    LAYOUTS = PROJ / "app" / "layouts"
    MODELS_DIR = PROJ / "models"

    HAS_MODELS = has_models(MODELS_DIR)
    MODEL_NAMES = scan_model_names(MODELS_DIR)
    MODELS_SITE = SITE / "models.hpp"
    APIS_DIR = PROJ / "app" / "apis"
    HAS_APIS = APIS_DIR.exists() and any(APIS_DIR.glob("*.hpp"))
    COMPONENTS_H = PROJ / "build" / "generated" / "components.hpp"
    OUT_H = PROJ / "build" / "generated" / "routes.hpp"
    OUT_C = PROJ / "build" / "generated" / "routes.cpp"
    COMPILE_COMMANDS = PROJ / "build" / "compile_commands.json"
    OUT_H.parent.mkdir(parents=True, exist_ok=True)

    def route_and_ns(p: Path):
        parts = list(p.with_suffix("").parts)
        if parts[-1] == "index": parts.pop()
        route_parts, ns_parts = [], []
        for part in parts:
            m = re.match(r'^\[(.+)\]$', part)
            if m:
                route_parts.append(f":{m.group(1)}")
                ns_parts.append(f"_{m.group(1)}_")
            else:
                route_parts.append(part)
                ns_parts.append(part.replace("-", "_"))
        route = "/" + "/".join(route_parts)
        ns = "_".join(ns_parts).replace("-", "_") or "index"
        return route, ns

    # 1. Gather component dependencies
    comp_deps = {}
    if COMPONENTS_H.exists():
        src = COMPONENTS_H.read_text()
        for m in re.finditer(r'struct (\w+)_traits \{[\s\S]*?// deps: (.*?)\n', src):
            name, deps_str = m.groups()
            items = [i.strip().strip('"') for i in deps_str.split(',') if i.strip()]
            deps = []
            for i in range(0, len(items), 2):
                deps.append((items[i], items[i+1]))
            comp_deps[name] = deps

    entries = []
    for f in sorted(SITE.glob("**/*.cpp")):
        rel = f.relative_to(SITE)
        route, ns = route_and_ns(rel)
        entries.append((route, ns, f))

    # Static routes before parameterized ones so /users/new beats /users/:id
    entries.sort(key=lambda e: (1 if ':' in e[0] else 0, e[0]))

    # routes.hpp url entries — written after toggle_routes is populated below
    hpp_header = [
        "// Auto-generated — do not edit",
        "#pragma once",
        "#include <string_view>",
        "#include <format>",
        "#include <revet/web.hpp>",
        "#include <revet/upload.hpp>",
        "#include \"toggles.hpp\"",
        "#include \"components.hpp\"",
        "#include \"i18n.hpp\"",
        "#include \"uploads.hpp\"",
        "#include \"resources.hpp\"",
        "",
    ]
    hpp_url_entries = []
    for route, ns, f in entries:
        params = re.findall(r':(\w+)', route)
        if params:
            param_args = ", ".join(f"std::string_view {p}" for p in params)
            fmt_route  = re.sub(r':\w+', '{}', route)
            args_list  = ", ".join(params)
            hpp_url_entries.append(_URL_PARAM.substitute(ns=ns, param_args=param_args, fmt_route=fmt_route, args_list=args_list))
        else:
            hpp_url_entries.append(_URL_STATIC.substitute(ns=ns, route=route))
    # toggle url entries added below after toggle_routes is scanned

    # Write routes.cpp
    global_includes = {
        "#include \"routes.hpp\"",
        "#include \"styles.hpp\"",
        "#include <vector>",
    }
    for hpp in (PROJ / "types").glob("*.hpp"):
        global_includes.add(rel_include(hpp, PROJ))
    if HAS_MODELS: global_includes.add("#include \"models.hpp\"")
    if HAS_APIS: global_includes.add("#include \"apis.hpp\"")
    if MODELS_SITE.exists():
        global_includes.add(rel_include(MODELS_SITE, PROJ))

    site_contents = {}
    for route, ns, f in entries:
        content = f.read_text()
        stripped = []
        for line in content.splitlines():
            if line.strip().startswith("#include"):
                inc = line.strip()
                if "models.hpp" in inc or "json_models.hpp" in inc or "web.hpp" in inc or "routes.hpp" in inc or "styles.hpp" in inc: continue  # hoisted to global_includes or implicit
                global_includes.add(inc)
            else:
                stripped.append(line)
        site_contents[ns] = "\n".join(stripped)

    # Scan states/ for standalone [[revet::toggle(route, default)]] bool defs
    STATES_DIR = PROJ / "app" / "states"
    TOGGLE_PATTERN = re.compile(
        r'\[\[.*?revet::toggle\(\s*"([^"]+)"\s*,\s*(true|false)\s*\).*?\]\]\s*bool\s+(\w+)',
        re.DOTALL,
    )
    toggle_routes = []  # (route, name)
    if STATES_DIR.exists():
        for f in sorted(STATES_DIR.glob("*.hpp")):
            for m in TOGGLE_PATTERN.finditer(f.read_text()):
                route, _default, name = m.groups()
                toggle_routes.append((route, name))

    for route, name in toggle_routes:
        hpp_url_entries.append(_URL_STATIC.substitute(ns=name, route=route))

    version_file = PROJ.parent / "VERSION"
    version_str  = version_file.read_text().strip() if version_file.exists() else "unknown"
    version_block = [
        "",
        "namespace revet {",
        f'inline constexpr std::string_view version = "{version_str}";',
        "} // namespace revet",
    ]
    write_lines(OUT_H, hpp_header + ["namespace urls {"] + hpp_url_entries + ["}"] + version_block)

    cpp = sorted(global_includes)
    cpp.append('#include "htmx_asset.hpp"')
    cpp.append('#include "optimistic_asset.hpp"')
    cpp.append('#include "server.hpp"  // revet.toml max_body, applied at static init')
    cpp.append("")

    for ns, content in site_contents.items():
        cpp.append(f"namespace route_{ns} {{")
        cpp.append(content)
        cpp.append("}")
        cpp.append("")

    cpp.append("namespace routes {")
    cpp.append("void setup(std::vector<web::Route>& out) {")

    # Built-in static assets served at /_revet/*
    for path, symbol in [
        ("/_revet/htmx.js",       "htmx_js"),
        ("/_revet/optimistic.js", "optimistic_js"),
    ]:
        cpp.append(_STATIC_ASSET.substitute(path=path, symbol=symbol))

    # Add automatic toggle routes
    for path, name in toggle_routes:
        cpp.append(_TOGGLE_HANDLER.substitute(path=path, name=name))

    def extract_state_vars(func_name, src):
        """Return (needed set, state_vars list, flashes_to_clear list) for a function."""
        needed = set()
        m_head = re.search(rf"{func_name}\s*\((.*?)\)", src, re.DOTALL)
        args_str = m_head.group(1) if m_head else ""
        for sm in STATE_FLASH_RE.finditer(args_str):
            needed.add((sm.group(1), sm.group(2)))
        state_vars, flashes_to_clear = [], []
        for mode, type_ in sorted(needed):
            var_name = f"state_{type_}"
            state_vars.append(var_name)
            flashes_to_clear.append((mode, type_, var_name))
        return needed, state_vars, flashes_to_clear

    # Regex helpers for API typed-param/return detection
    _PARAM_TYPE_RE_CACHE: dict[str, re.Pattern] = {}
    _RETURN_TYPE_RE_CACHE: dict[str, re.Pattern] = {}
    _ASYNC_RE_CACHE: dict[str, re.Pattern] = {}

    def _api_detect(func_name: str, src: str):
        """For API routes: detect model param type, return type, and async-ness.
        Returns (param_type | None, return_type | None, is_async).
        param_type is the C++ type name if it's a known [[revet::model]] (not web::Request/auto).
        return_type is the C++ type (possibly std::vector<Model>) if the return is a known model.
        is_async is True if the return is wrapped in web::Task<...>.
        """
        # Param: const TypeName& varname  — capture TypeName
        if func_name not in _PARAM_TYPE_RE_CACHE:
            _PARAM_TYPE_RE_CACHE[func_name] = re.compile(
                rf"{func_name}\s*\(\s*const\s+([\w:]+)\s*&"
            )
        pm = _PARAM_TYPE_RE_CACHE[func_name].search(src)
        param_type = None
        if pm:
            raw = pm.group(1).split("::")[-1]  # strip web:: etc.
            if raw in MODEL_NAMES:
                param_type = raw

        # Async: -> web::Task<...>  (or  -> Task<...>)
        if func_name not in _ASYNC_RE_CACHE:
            _ASYNC_RE_CACHE[func_name] = re.compile(
                rf"{func_name}\s*\([^)]*\)\s*->\s*(?:web::)?Task\s*<"
            )
        is_async = bool(_ASYNC_RE_CACHE[func_name].search(src))

        # Return: -> [web::Task<]TypeName | std::vector<TypeName>[>]
        # Match optionally through a Task<...> wrapper so async handlers can be detected.
        if func_name not in _RETURN_TYPE_RE_CACHE:
            _RETURN_TYPE_RE_CACHE[func_name] = re.compile(
                rf"{func_name}\s*\([^)]*\)\s*->"
                r"\s*(?:(?:web::)?Task\s*<\s*)?"
                r"(std::vector\s*<\s*(\w+)\s*>|(\w+))"
            )
        rm = _RETURN_TYPE_RE_CACHE[func_name].search(src)
        return_type = None
        if rm:
            vec_inner = rm.group(2)
            plain = rm.group(3)
            if vec_inner and vec_inner in MODEL_NAMES:
                return_type = f"std::vector<{vec_inner}>"
            elif plain and plain in MODEL_NAMES:
                return_type = plain

        return param_type, return_type, is_async

    def _emit_api_handler(cpp, ns, method, field, src):
        """Emit a JSON-aware API route handler wrapper.

        Handlers that return web::Task<T> are emitted as r.async_{field}
        so they can co_await outbound HTTP / DB calls. Sync handlers
        keep the existing r.{field} = [](req) -> Response form.
        """
        param_type, return_type, is_async = _api_detect(f"render_{method}", src)

        if is_async:
            ret    = "co_return"
            wait   = "co_await "
            handle = f"r.async_{field}"
            sig    = "-> web::Task<web::Response>"
        else:
            ret    = "return"
            wait   = ""
            handle = f"r.{field}"
            sig    = "-> web::Response"

        cpp.append(f"        {handle} = [](const web::Request& req) {sig} {{")

        if param_type:
            # Typed model param: validate + deserialize before calling handler
            cpp.append(f"            if (!std::string_view(req.headers.get(\"content-type\")).starts_with(\"application/json\"))")
            cpp.append(f"                {ret} web::api::error(web::status::bad_request, \"Content-Type: application/json required\");")
            cpp.append(f"            if (req.raw_body.empty())")
            cpp.append(f"                {ret} web::api::error(web::status::bad_request, \"request body required\");")
            cpp.append(f"            {param_type} body{{}};")
            cpp.append(f"            try {{")
            cpp.append(f"                revet::json::Document doc(req.raw_body);")
            cpp.append(f"                body = revet::json::from_json<{param_type}>(doc);")
            cpp.append(f"            }} catch (const revet::json::ParseError& e) {{")
            cpp.append(f"                {ret} web::api::error(web::status::bad_request, e.what());")
            cpp.append(f"            }}")
            if return_type:
                cpp.append(f"            auto result = {wait}route_{ns}::render_{method}(body);")
                cpp.append(f"            {ret} web::Response{{.body = revet::json::to_json<{return_type}>(result), .content_type = \"application/json\"}};")
            else:
                cpp.append(f"            {ret} web::to_response({wait}route_{ns}::render_{method}(body));")
        elif return_type:
            # No model param, but model return: auto-serialize
            cpp.append(f"            auto result = {wait}route_{ns}::render_{method}(req);")
            cpp.append(f"            {ret} web::Response{{.body = revet::json::to_json<{return_type}>(result), .content_type = \"application/json\"}};")
        else:
            # Full control: web::Request → web::Response, no wrapping
            cpp.append(f"            {ret} web::to_response({wait}route_{ns}::render_{method}(req));")

        cpp.append("        };")

    for route, ns, f in entries:
        src = f.read_text()
        is_api = route.startswith("/api/") or route == "/api"
        cpp.append("    {")
        cpp.append(f"        web::Route r; r.path = \"{route}\";")

        # Maps user-facing function name suffix → Route field name
        method_field = {"get": "get", "post": "post", "put": "put", "delete": "del", "patch": "patch"}
        for method, field in method_field.items():
            func_name = f"render_{method}"
            if not re.search(rf"{func_name}\s*\(", src):
                continue

            if is_api:
                _emit_api_handler(cpp, ns, method, field, src)
            else:
                needed, state_vars, flashes_meta = extract_state_vars(func_name, src)

                # Detect async handler: explicit or trailing-return Task<Response>
                # Matches both:
                #   web::Task<web::Response> render_get(...)
                #   auto render_get(...) -> web::Task<web::Response>
                is_async = bool(re.search(
                    rf"Task\s*<\s*(?:web::)?Response\s*>\s+{func_name}\s*\(", src)) or \
                           bool(re.search(
                    rf"{func_name}\s*\([^)]*\)\s*->\s*web::Task\s*<\s*(?:web::)?Response\s*>", src))

                if is_async:
                    cpp.append(f"        r.async_{field} = [](const web::Request& req) -> web::Task<web::Response> {{")
                    if needed:
                        cpp.append("            auto& ctx = web::Context::current();")
                    _emit_state_loading(cpp, flashes_meta)
                    cpp.append(f"            return route_{ns}::render_{method}({_handler_args(state_vars)});")
                    cpp.append("        };")
                else:
                    cpp.append(f"        r.{field} = [](const web::Request& req) -> web::Response {{")
                    if needed:
                        cpp.append("            auto& ctx = web::Context::current();")
                    _emit_state_loading(cpp, flashes_meta)
                    flashes_to_clear = [f"_cw_{type_}" for mode, type_, _ in flashes_meta if mode == "Flash"]
                    cpp.append(f"            auto resp = web::to_response(route_{ns}::render_{method}({_handler_args(state_vars)}));")
                    for cname in flashes_to_clear:
                        cpp.append(f"            if (req.cookies.get(\"{cname}\") != \"\") resp.cookies[\"{cname}\"] = \"\";")
                    cpp.append("            return resp;")
                    cpp.append("        };")

        # SSE handler: render_sse(req[, states...], sse) → void
        if re.search(r"render_sse\s*\(", src):
            cpp.append(f"        r.sse = [](const web::Request& req, web::SSEWriter& sse) {{")
            needed, state_vars, flashes_meta = extract_state_vars("render_sse", src)
            if needed:
                cpp.append("            auto& ctx = web::Context::current();")
            _emit_state_loading(cpp, flashes_meta)
            cpp.append(f"            route_{ns}::render_sse({_handler_args(state_vars, ', sse')});")
            cpp.append(f"        }};")

        if re.search(r"\bapi_guards\s*\(\s*\)", src):
            cpp.append(f"        r.guards = route_{ns}::api_guards();")
        elif re.search(r"\broute_guards\s*\(\s*\)", src):
            cpp.append(f"        r.guards = route_{ns}::route_guards();")

        cpp.append("        out.push_back(r);")
        cpp.append("    }")

    cpp.append("}")
    cpp.append("} // namespace routes")
    write_lines(OUT_C, cpp)

    # Emit compile_commands.json entries for site/*.cpp so LSPs (clangd, ccls)
    # can resolve symbols. These files are inlined into routes.cpp at codegen
    # time and never directly compiled, so CMake omits them from the DB.
    # We synthesize entries by cloning the routes.cpp entry and force-including
    # all generated headers that site files depend on (they are inlined into
    # routes.cpp at codegen time and never compiled standalone, so clangd needs
    # these to resolve symbols).
    def patch_compile_commands():
        if not COMPILE_COMMANDS.exists():
            return
        try:
            db = json.loads(COMPILE_COMMANDS.read_text())
        except json.JSONDecodeError as e:
            print(f"gen_routes: warning: failed to parse {COMPILE_COMMANDS}: {e}", file=sys.stderr)
            return

        routes_cpp_abs = str(OUT_C.resolve())
        ref = next((e for e in db if Path(e.get("file", "")).resolve() == Path(routes_cpp_abs)), None)
        if not ref:
            return

        gen_dir = (PROJ / "build" / "generated").resolve()

        # All headers that routes.cpp inlines — every site file needs these.
        always_headers = [
            "routes.hpp",
            "components.hpp",
            "styles.hpp",
            "states.hpp",
            "toggles.hpp",
            "i18n.hpp",
            "uploads.hpp",
        ]
        optional_headers = [
            "queries.hpp",
            "resources.hpp",
            "models.hpp",
            "forms.hpp",
            "apis.hpp",
        ]
        headers = [gen_dir / h for h in always_headers]
        headers += [gen_dir / h for h in optional_headers if (gen_dir / h).exists()]

        # "-include" must be two separate tokens in the arguments array form.
        # In the command string form a single "-include/path" token also works.
        include_pairs = []          # for "arguments" form: ["-include", "/abs/path"]
        include_flags = []          # for "command" form:   "-include /abs/path"
        for h in headers:
            include_pairs += ["-include", str(h)]
            include_flags.append(f"-include {h}")

        def make_entry(src_file):
            entry = dict(ref)
            entry["file"] = str(src_file.resolve())
            if "command" in entry:
                cmd = entry["command"]
                cmd = re.sub(r"-o\s+\S+", f"-o {src_file.stem}.o", cmd)
                parts = cmd.rsplit(" ", 1)
                parts[-1] = str(src_file.resolve())
                extra = " " + " ".join(include_flags) + " -Wno-unused-function"
                entry["command"] = parts[0] + extra + " " + parts[-1]
            elif "arguments" in entry:
                args = list(entry["arguments"])
                for i, a in enumerate(args):
                    if a == "-o" and i + 1 < len(args):
                        args[i + 1] = f"{src_file.stem}.o"
                if args:
                    args[-1] = str(src_file.resolve())
                args.extend(include_pairs)
                args.append("-Wno-unused-function")
                entry["arguments"] = args
            return entry

        # Targets: site/*.cpp except site_init.cpp (that one IS compiled directly).
        # Always regenerate synthesized entries so force-include changes take effect.
        site_files = {
            f.resolve()
            for f in SITE.glob("**/*.cpp")
            if f.name != "site_init.cpp"
        }
        db = [e for e in db if Path(e.get("file", "")).resolve() not in site_files]
        db.extend(make_entry(f) for f in sorted(site_files, key=lambda p: str(p)))

        COMPILE_COMMANDS.write_text(json.dumps(db, indent=2))

    patch_compile_commands()


if __name__ == "__main__":
    main()

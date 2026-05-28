#!/usr/bin/env python3
"""Scans app/apis/*.hpp for [[revet::external("METHOD URL")]] function
declarations and emits build/generated/apis.hpp with inline definitions
that call into http::* and JSON-deserialize the response.

User files own:
  - The response struct (marked [[revet::model]] for free JSON support)
  - A forward declaration with the [[revet::external]] attribute

Generated apis.hpp:
  - Re-includes every user header
  - Provides one inline definition per external function, placed in the
    same namespace the declaration appeared in.

Param classification by name & type:
  - Name appears as {name} in the URL  -> path param (URL substitution)
  - Otherwise primitive (string/int)    -> query-string param
  - (Body params for POST/PUT/PATCH not yet supported.)
"""
import re
from pathlib import Path
from dataclasses import dataclass, field
from codegen import setup, file_header, rel_include, write_file

PROJ, OUT = setup("apis.hpp")
APIS_DIR = PROJ / "app" / "apis"

# [[revet::external("METHOD URL")]]
ATTR_RE = re.compile(
    r'\[\[\s*revet::external\s*\(\s*"([^"]+)"\s*\)\s*\]\]',
)

# Function declaration following the attribute. Supports both styles:
#   auto name(params) -> ReturnType;
#   ReturnType name(params);
DECL_RE = re.compile(
    r"""
    (?:inline\s+)?
    (?:
        auto\s+(?P<name1>\w+)\s*\((?P<params1>[^)]*)\)\s*->\s*(?P<ret1>[^;]+?)
        |
        (?P<ret2>[\w:<>,\s]+?)\s+(?P<name2>\w+)\s*\((?P<params2>[^)]*)\)
    )
    \s*;
    """,
    re.VERBOSE,
)

PARAM_RE = re.compile(r"^\s*(?:const\s+)?([\w:<>,\s]+?)\s+(\w+)\s*(?:=\s*(.+?))?\s*$")


@dataclass
class External:
    method: str
    url: str
    namespace: str        # may be empty for global
    func_name: str
    return_type: str      # e.g. "web::Task<UserResponse>"
    response_type: str    # the inner T of web::Task<T>
    params: list = field(default_factory=list)   # [(type, name, default_or_None)]
    src_file: str = ""


def split_top_commas(s: str) -> list[str]:
    """Split on commas respecting <>, (), [] nesting."""
    out, depth, buf = [], 0, []
    for ch in s:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(buf).strip())
            buf = []
        else:
            buf.append(ch)
    if buf:
        out.append("".join(buf).strip())
    return [x for x in out if x]


def parse_params(s: str):
    """Parse a C++ parameter list. Returns list of (type, name, default)."""
    params = []
    for raw in split_top_commas(s):
        m = PARAM_RE.match(raw)
        if not m:
            continue
        ptype = re.sub(r"\s+", " ", m.group(1).strip())
        params.append((ptype, m.group(2), m.group(3)))
    return params


def task_inner(ret: str) -> str:
    """Return T from 'web::Task<T>'; otherwise the type itself."""
    m = re.match(r"^\s*web::Task\s*<\s*(.+)\s*>\s*$", ret.strip(), re.DOTALL)
    return m.group(1).strip() if m else ret.strip()


def scan_file(path: Path):
    """Yield External objects, tracking namespace depth/name as we scan."""
    text = path.read_text()
    ns_stack: list[str] = []
    pos = 0
    NS_OPEN = re.compile(r"\bnamespace\s+(\w+)\s*\{")
    BRACE = re.compile(r"[{}]")

    # We do a single pass: walk through namespace tokens AND attribute matches,
    # interleaved by source position.
    events = []
    for m in NS_OPEN.finditer(text):
        events.append((m.start(), "ns_open", m.group(1), m.end()))
    # Track every brace separately so closes pop the stack.
    for m in BRACE.finditer(text):
        if m.group() == "}":
            events.append((m.start(), "brace_close", None, m.end()))
        # opens are handled by ns_open or are non-ns (skip)
    for m in ATTR_RE.finditer(text):
        events.append((m.start(), "attr", m, m.end()))
    # Skip non-ns opening braces: they appear at positions not at the start of
    # an `ns_open`. Easiest: maintain a brace depth alongside ns_stack length;
    # when depth drops below ns_stack length we pop.
    events.sort(key=lambda e: e[0])

    depth = 0
    ns_depths: list[int] = []   # parallel to ns_stack: brace depth when each was pushed
    # Walk text positions to count braces — events alone miss non-namespace opens.
    # Strategy: between consecutive events, count {/} in the text slice.
    last_pos = 0

    def consume_braces(upto: int):
        nonlocal depth
        for ch in text[last_pos:upto]:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                # Pop any namespaces that just closed.
                while ns_depths and depth < ns_depths[-1]:
                    ns_depths.pop()
                    ns_stack.pop()

    for pos, kind, payload, end in events:
        consume_braces(pos)
        if kind == "ns_open":
            # The opening brace is at `end - 1`; record current depth+1.
            depth += 1
            ns_stack.append(payload)
            ns_depths.append(depth)
            last_pos = end
        elif kind == "brace_close":
            # already handled in consume_braces via subsequent iterations.
            last_pos = pos  # don't double-count
        elif kind == "attr":
            m = payload
            method_url = m.group(1).strip()
            mu = re.match(r"^(GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS)\s+(.+)$", method_url)
            if not mu:
                last_pos = end
                continue
            method, url = mu.group(1), mu.group(2).strip()
            # Match function decl immediately after attribute (allow whitespace).
            tail = text[end:end + 2048]
            dm = DECL_RE.match(tail.lstrip())
            if not dm:
                last_pos = end
                continue
            ret = (dm.group("ret1") or dm.group("ret2") or "").strip()
            name = dm.group("name1") or dm.group("name2")
            params_s = dm.group("params1") or dm.group("params2") or ""
            yield External(
                method=method,
                url=url,
                namespace="::".join(ns_stack),
                func_name=name,
                return_type=ret,
                response_type=task_inner(ret),
                params=parse_params(params_s),
                src_file=path.name,
            )
            last_pos = end
        else:
            last_pos = end


def scan_all():
    if not APIS_DIR.exists():
        return []
    out = []
    for f in sorted(APIS_DIR.glob("*.hpp")):
        out.extend(scan_file(f))
    return out


def is_string_like(t: str) -> bool:
    return t in ("std::string", "std::string_view", "const std::string", "const std::string_view")


def to_string_expr(ptype: str, name: str) -> str:
    """Express a param as std::string for URL construction."""
    if "string" in ptype.lower():
        return f"std::string({name})"
    return f"std::to_string({name})"


def emit_url_expr(url: str, params: list) -> tuple[str, set]:
    """Build a C++ expression that constructs the request URL.
    Returns (expr, set_of_path_param_names_consumed)."""
    path_names = set(re.findall(r"\{(\w+)\}", url))
    # Start with the URL template; replace each {name} via a small lambda chain
    expr = f'std::string({json_str_literal(url)})'
    for ptype, pname, _ in params:
        if pname not in path_names:
            continue
        val = to_string_expr(ptype, pname)
        expr = (
            f'[](std::string s, const std::string& v) {{ '
            f'auto p = s.find("{{{pname}}}"); '
            f'if (p != std::string::npos) s.replace(p, {len(pname) + 2}, v); '
            f'return s; '
            f'}}({expr}, {val})'
        )
    # Query string params: any param whose name is not in URL.
    has_q = "?" in url
    first_q = not has_q
    for ptype, pname, _ in params:
        if pname in path_names:
            continue
        val = to_string_expr(ptype, pname)
        sep = "?" if first_q else "&"
        first_q = False
        expr = f'{expr} + "{sep}{pname}=" + {val}'
    return expr, path_names


def json_str_literal(s: str) -> str:
    escaped = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def http_helper(method: str) -> str:
    m = method.lower()
    return "delete_" if m == "delete" else m


def emit_definition(ext: External) -> list[str]:
    lines = []
    sig_params = ", ".join(f"{t} {n}" for t, n, _ in ext.params)
    lines.append(f"inline auto {ext.func_name}({sig_params}) -> {ext.return_type} {{")
    url_expr, _ = emit_url_expr(ext.url, ext.params)
    lines.append(f"    auto url  = {url_expr};")
    lines.append(f"    auto resp = co_await http::{http_helper(ext.method)}(url);")
    lines.append(f"    if (!resp.ok()) throw http::Error(resp.status_code, resp.body);")
    lines.append(f"    revet::json::Document doc(resp.body);")
    lines.append(f"    co_return revet::json::from_json<{ext.response_type}>(doc);")
    lines.append("}")
    return lines


def generate(externs: list[External]) -> str:
    lines = file_header(
        "#include <coroutine>",
        "#include <revet/task.hpp>",
        "#include <revet/http.hpp>",
        "#include <revet/json.hpp>",
        '#include "models.hpp"',
        "#include <string>",
    )
    # Re-include every user header so callers only need apis.hpp.
    if APIS_DIR.exists():
        for hpp in sorted(APIS_DIR.glob("*.hpp")):
            lines.append(rel_include(hpp, PROJ))
    lines.append("")
    # Group by namespace to emit one block each.
    by_ns: dict[str, list[External]] = {}
    for e in externs:
        by_ns.setdefault(e.namespace, []).append(e)
    for ns in sorted(by_ns):
        if ns:
            # Split nested namespace: "a::b" -> "namespace a { namespace b {"
            parts = ns.split("::")
            lines.append(" ".join(f"namespace {p} {{" for p in parts))
        for e in by_ns[ns]:
            lines.extend(emit_definition(e))
            lines.append("")
        if ns:
            lines.append("}" * len(ns.split("::")) + f" // namespace {ns}")
            lines.append("")
    return "\n".join(lines)


def main():
    externs = scan_all()
    write_file(OUT, generate(externs))


if __name__ == "__main__":
    main()

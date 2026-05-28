#!/usr/bin/env python3
"""Scans models/*.hpp for [[revet::model]] structs and emits
build/generated/models.hpp with from_json/to_json helpers.

User-owned structs live in models/*.hpp. The generated aggregate
header re-includes every user file so any project code that does
`#include "models.hpp"` gets all model types plus serialization.
"""
import sys, re
from pathlib import Path
from codegen import tmpl as _tmpl, setup, static_tmpl, file_header, rel_include, write_file

_FROM_JSON       = _tmpl("json_from_json.tmpl")
_FROM_JSON_VALUE = _tmpl("json_from_json_value.tmpl")
_FROM_ROW        = _tmpl("json_from_row.tmpl")
_TO_JSON         = _tmpl("json_to_json.tmpl")
_PREAMBLE        = static_tmpl("json_models_preamble.tmpl")

PROJ, OUT = setup("models.hpp")
MODELS_DIR = PROJ / "models"
APIS_DIR = PROJ / "app" / "apis"


def _model_sources():
    """Files to scan for [[revet::model]] structs: user models + API headers."""
    files = []
    if MODELS_DIR.exists():
        files += sorted(MODELS_DIR.glob("*.hpp"))
    if APIS_DIR.exists():
        files += sorted(APIS_DIR.glob("*.hpp"))
    return files

ATTR_RE = re.compile(
    r"(?:\[\[\s*revet::model(?:\s*\(\s*([^)]*)\s*\))?\s*\]\]\s*"
    r"struct|struct\s*\[\[\s*revet::model(?:\s*\(\s*([^)]*)\s*\))?\s*\]\])"
    r"\s+(\w+)\s*\{([^}]*)\}\s*;",
    re.DOTALL,
)

FIELD_RE = re.compile(r"^\s*([\w:<>,\s]+?)\s+(\w+)\s*(?:=[^;]*)?;", re.MULTILINE)


def parse_fields(body: str):
    """Extract (type, name) pairs from a struct body. Skips constructors,
    member functions, nested types."""
    fields = []
    for line in body.split(";"):
        line = line.strip()
        if not line:
            continue
        # Skip anything that looks like a function (has parens), constructors,
        # or declarations with initializer that contains {}
        if "(" in line or "{" in line:
            continue
        # Strip default-value (= expr)
        line = re.sub(r"\s*=\s*.*$", "", line).strip()
        m = re.match(r"^([\w:<>,\s]+?)\s+(\w+)$", line)
        if m:
            ftype = re.sub(r"\s+", " ", m.group(1).strip())
            fields.append((ftype, m.group(2)))
    return fields


def parse_attrs(attr_body):
    """Parse [[revet::model(row, ...)]] attribute args."""
    if not attr_body:
        return set()
    return {a.strip() for a in attr_body.split(",") if a.strip()}


NS_OPEN_RE = re.compile(r"\bnamespace\s+([\w:]+)\s*\{")


def _ns_at(content: str, pos: int) -> str:
    """Return the C++ namespace surrounding `pos` (empty string for global).
    Walks the prefix counting brace depth and tracks open namespaces."""
    stack: list[tuple[str, int]] = []  # (ns_name, depth_when_opened)
    depth = 0
    i = 0
    while i < pos:
        m_ns = NS_OPEN_RE.match(content, i)
        if m_ns:
            depth += 1
            stack.append((m_ns.group(1), depth))
            i = m_ns.end()
            continue
        ch = content[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            while stack and stack[-1][1] > depth:
                stack.pop()
        i += 1
    # Flatten nested namespace names ("a::b" inside another "c" → "c::a::b").
    parts = []
    for name, _ in stack:
        parts.extend(name.split("::"))
    return "::".join(parts)


def scan_models():
    """Return list of (file, qualified_name, fields, attrs)."""
    out = []
    for f in _model_sources():
        content = f.read_text()
        for m in ATTR_RE.finditer(content):
            attr_body = m.group(1) or m.group(2)
            name, body = m.group(3), m.group(4)
            ns = _ns_at(content, m.start())
            qualified = f"{ns}::{name}" if ns else name
            fields = parse_fields(body)
            out.append((f, qualified, fields, parse_attrs(attr_body)))
    return out


def is_optional(t: str):
    return t.startswith("std::optional<")


def is_vector(t: str):
    return t.startswith("std::vector<")


def inner_type(t: str):
    return t[t.index("<") + 1 : t.rindex(">")].strip()


def deser_expr(ftype: str, value_expr: str) -> str:
    """Generate an expression that converts a revet::json::Value to ftype."""
    if is_optional(ftype):
        inner = inner_type(ftype)
        # value_expr is typically v.at("field") — rewrite to a has() guard so
        # absent optional fields become std::nullopt instead of throwing.
        key_match = re.search(r'\.at\("([^"]+)"\)$', value_expr)
        if key_match:
            key = key_match.group(1)
            base = value_expr[: value_expr.rfind('.at(')]
            return (f'{base}.has("{key}") ? '
                    f'std::optional<{inner}>(static_cast<{inner}>({base}.at("{key}"))) '
                    f': std::nullopt')
    if is_vector(ftype):
        inner = inner_type(ftype)
        return f"({value_expr}).operator std::vector<{inner}>()"
    return f"static_cast<{ftype}>({value_expr})"


def row_expr(ftype: str, cell_expr: str) -> str:
    """Convert a row cell (std::string) to the given C++ type."""
    if ftype in ("std::string", "std::string_view"):
        return cell_expr
    if ftype == "bool":
        return f'({cell_expr} == "1" || {cell_expr} == "true")'
    if ftype == "float":
        return f"std::stof({cell_expr})"
    if ftype == "double":
        return f"std::stod({cell_expr})"
    if ftype in ("int", "int32_t", "uint32_t", "short"):
        return f"static_cast<{ftype}>(std::stol({cell_expr}))"
    if ftype in ("int64_t", "long", "long long", "uint64_t", "size_t", "std::size_t"):
        return f"static_cast<{ftype}>(std::stoll({cell_expr}))"
    if is_optional(ftype):
        inner = inner_type(ftype)
        return (
            f"({cell_expr}.empty() ? std::optional<{inner}>{{}} : "
            f"std::optional<{inner}>({row_expr(inner, cell_expr)}))"
        )
    # Unknown / vector: fall back to default-constructed value.
    return f"{ftype}{{}}"


def ser_lines(ftype: str, expr: str, out_var: str) -> list[str]:
    """Emit C++ lines that append the JSON encoding of `expr` to `out_var`."""
    lines = []
    if is_optional(ftype):
        inner = inner_type(ftype)
        lines.append(f"    if ({expr}.has_value()) {{")
        for ln in ser_lines(inner, f"(*{expr})", out_var):
            lines.append("    " + ln)
        lines.append("    } else {")
        lines.append(f'        {out_var} += "null";')
        lines.append("    }")
        return lines
    if is_vector(ftype):
        inner = inner_type(ftype)
        lines.append(f'    {out_var} += "[";')
        lines.append(f"    {{")
        lines.append(f"        bool first = true;")
        lines.append(f"        for (auto const& _elem : {expr}) {{")
        lines.append(f'            if (!first) {out_var} += ",";')
        lines.append(f"            first = false;")
        for ln in ser_lines(inner, "_elem", out_var):
            lines.append("    " + ln)
        lines.append(f"        }}")
        lines.append(f"    }}")
        lines.append(f'    {out_var} += "]";')
        return lines
    if ftype in ("std::string", "std::string_view"):
        lines.append(f"    {out_var} += revet::json::detail::quote({expr});")
        return lines
    if ftype == "bool":
        lines.append(f'    {out_var} += ({expr}) ? "true" : "false";')
        return lines
    # numeric default
    lines.append(f"    {out_var} += std::to_string({expr});")
    return lines


def generate(models):
    lines = file_header(
        "#include <revet/json.hpp>",
        "#include <string>",
        "#include <string_view>",
        "#include <vector>",
        "#include <optional>",
        "#include <cstdio>",
    )
    for hpp in _model_sources():
        lines.append(rel_include(hpp, PROJ))

    lines += _PREAMBLE.splitlines()
    lines.append("")

    for src_file, name, fields, _attrs in models:
        fv_body = "\n".join(f"    out.{fn} = {deser_expr(ft, f'v.at(\"{fn}\")')};" for ft, fn in fields)
        lines.append(_FROM_JSON_VALUE.substitute(name=name, src_file=src_file.name, body=fv_body))
        lines.append("")
        lines.append(_FROM_JSON.substitute(name=name))
        lines.append("")
        fr_body = "\n".join(f"    out.{fn} = {row_expr(ft, f'r[{i}]')};" for i, (ft, fn) in enumerate(fields))
        lines.append(_FROM_ROW.substitute(name=name, body=fr_body))
        lines.append("")
        tj_lines = []
        for i, (ft, fn) in enumerate(fields):
            sep = ',' if i > 0 else ''
            tj_lines.append(f'    out += "{sep}\\"{fn}\\":";')
            tj_lines.extend(ser_lines(ft, f"val.{fn}", "out"))
        lines.append(_TO_JSON.substitute(name=name, body="\n".join(tj_lines)))
        lines.append("")

    lines.append("} // namespace revet::json")
    lines.append("")
    return "\n".join(lines)


def main():
    models = scan_models()
    write_file(OUT, generate(models))


if __name__ == "__main__":
    main()

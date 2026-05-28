#!/usr/bin/env python3
import sys, re
from pathlib import Path
from codegen import tmpl as _tmpl, setup, write_lines, write_file, has_models, rel_include, STATE_FLASH_RE, STATE_WRITE_RE, STATE_TEMPLATE_RE

_TRAITS           = _tmpl("states_traits.tmpl")
_PACK_ENC         = _tmpl("states_pack_enc.tmpl")
_UNPACK_ENC       = _tmpl("states_unpack_enc.tmpl")
_SPECIALIZATIONS  = _tmpl("states_specializations.tmpl")
_COOKIE_HELPER    = _tmpl("states_cookie_helper.tmpl")
_PACK_PLAIN_FN    = _tmpl("states_pack_plain_fn.tmpl")
_UNPACK_PLAIN_FN  = _tmpl("states_unpack_plain_fn.tmpl")

_STR_TYPES = frozenset({"std::string", "std::string_view"})
_NUM_TYPES = frozenset({"int", "int32_t", "int64_t", "int16_t",
                         "uint32_t", "uint64_t", "uint16_t",
                         "size_t", "float", "double"})


def _pack_field(ft, fn):
    if ft in _STR_TYPES:
        return f"    {{ uint32_t len = (uint32_t)s.{fn}.size(); out.append((char*)&len, 4); out.append(s.{fn}); }}"
    if ft in _NUM_TYPES:
        return f"    out.append((char*)&s.{fn}, sizeof(s.{fn}));"
    if ft == "bool":
        return f"    out.append((char*)&s.{fn}, 1);"


def _unpack_field(ft, fn):
    if ft in _STR_TYPES:
        return f"    if (p + 4 <= end) {{ uint32_t len; memcpy(&len, p, 4); p += 4; if (p + len <= end) {{ out.{fn} = std::string(p, len); p += len; }} }}"
    if ft in _NUM_TYPES:
        return f"    if (p + sizeof(out.{fn}) <= end) {{ memcpy(&out.{fn}, p, sizeof(out.{fn})); p += sizeof(out.{fn}); }}"
    if ft == "bool":
        return f"    if (p + 1 <= end) {{ memcpy(&out.{fn}, p, 1); p += 1; }}"


def main():
    PROJ, OUT = setup("states.hpp")
    SITE = PROJ / "app" / "routes"
    LAYOUTS = PROJ / "app" / "layouts"
    MODELS_DIR = PROJ / "models"

    MODELS_SITE = SITE / "models.hpp"

    types = set()
    files = list(SITE.glob("**/*.cpp")) + list(LAYOUTS.glob("**/*.cpp"))

    for f in files:
        content = f.read_text()
        for m in STATE_FLASH_RE.finditer(content):
            types.add(m.group(2))
        for m in STATE_WRITE_RE.finditer(content):
            types.add(m.group(1))
        for m in STATE_TEMPLATE_RE.finditer(content):
            types.add(m.group(1))

    if not types:
        write_file(OUT, "#pragma once\n\nnamespace web::states {\n    template<typename T> struct Traits;\n    template<typename T> std::string pack(const T& val);\n}\n")
        sys.exit(0)

    hpp = [
        "// Auto-generated — do not edit",
        "#pragma once",
        "#include <revet/web.hpp>",
        "#include <revet/sanitize.hpp>",
        "#include <revet/crypto.hpp>",
    ]

    if has_models(MODELS_DIR):
        hpp.append("#include \"models.hpp\"")
    if MODELS_SITE.exists():
        hpp.append(rel_include(MODELS_SITE, PROJ))

    hpp += [
        "#include <string>",
        "#include <cstring>",
        "",
        "namespace web::states {",
        "",
        "template<typename T> struct Traits;",
        "template<typename T> std::string pack(const T& val);",  # Encrypted (default)
        "template<typename T> T unpack(std::string_view);",  # Encrypted (default)
        "template<typename T> std::string pack_plain(const T& val);",  # Unencrypted
        "template<typename T> T unpack_plain(std::string_view);",  # Unencrypted
        ""
    ]

    struct_defs = {}
    model_hpps = sorted(MODELS_DIR.glob("*.hpp")) if MODELS_DIR.exists() else []
    search_defs = files + model_hpps + ([MODELS_SITE] if MODELS_SITE.exists() else [])

    for f in search_defs:
        content = f.read_text()
        for t in types:
            if t in struct_defs: continue
            match = re.search(rf"(?:struct|class)\s+(?:\[\[.*?\]\]\s*)?{t}\s*{{([^}}]+)}};", content)
            if match:
                fields = []
                body = match.group(1)
                for line in body.split(";"):
                    line = line.strip()
                    if not line: continue
                    # Remove default values (e.g., "= false") from the line
                    line = re.sub(r"\s*=\s*.*$", "", line)
                    m_field = re.search(r"([\w::<>]+)\s+(\w+)$", line)
                    if m_field:
                        fields.append((m_field.group(1), m_field.group(2)))
                struct_defs[t] = fields

    for t in sorted(types):
        if t not in struct_defs:
            sys.exit(f"gen_states: definition for '{t}' not found in any scanned header or source file")

        fields = struct_defs[t]
        hpp.append(_TRAITS.substitute(t=t))
        hpp.append("")

        pack_body = "\n".join(filter(None, (_pack_field(ft, fn) for ft, fn in fields)))
        hpp.append(_PACK_PLAIN_FN.substitute(t=t, body=pack_body))
        hpp.append("")

        # Encrypted pack (default)
        hpp.append(_PACK_ENC.substitute(t=t))
        hpp.append("")

        unpack_body = "\n".join(filter(None, (_unpack_field(ft, fn) for ft, fn in fields)))
        hpp.append(_UNPACK_PLAIN_FN.substitute(t=t, body=unpack_body))
        hpp.append("")

        # Encrypted unpack (default)
        hpp.append(_UNPACK_ENC.substitute(t=t))
        hpp.append("")

        # Plain variants (for unencrypted cookies if user opts out)
        hpp.append(_SPECIALIZATIONS.substitute(t=t))
        hpp.append("")

    hpp.append("")
    hpp.append("// Type-safe cookie helpers")
    for t in sorted(types):
        hpp.append(_COOKIE_HELPER.substitute(t=t))

    hpp.append("")
    hpp.append("// has<T>: check if a state cookie is present (for use in route_guards before state injection)")
    hpp.append("template<typename T>")
    hpp.append("[[nodiscard]] inline bool has(const web::ParamMap& cookies) {")
    hpp.append("    return !cookies.get(std::string(cookie_prefix).append(Traits<T>::name)).empty();")
    hpp.append("}")

    hpp.append("} // namespace web::states")
    write_lines(OUT, hpp)


if __name__ == "__main__":
    main()

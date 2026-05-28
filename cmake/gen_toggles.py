#!/usr/bin/env python3
"""Scans states/*.hpp for [[revet::toggle(route, default)]] bool defs and generates toggles.hpp."""
import sys, re
from pathlib import Path
from codegen import tmpl, tmpl_whole, setup, write_file

_HEADER     = tmpl_whole("toggles_header.tmpl")
_TOGGLE_FN  = tmpl("toggles_fn.tmpl")

PROJ, OUT = setup("toggles.hpp")
STATES    = PROJ / "app" / "states"

PATTERN = re.compile(
    r'\[\[.*?revet::toggle\(\s*"([^"]+)"\s*,\s*(true|false)\s*\).*?\]\]\s*bool\s+(\w+)',
    re.DOTALL,
)

toggles = []  # (name, cookie_key, default_bool)
if STATES.exists():
    for f in sorted(STATES.glob("*.hpp")):
        for m in PATTERN.finditer(f.read_text()):
            route, default, name = m.groups()
            toggles.append((name, route, default == "true"))

if not toggles:
    write_file(OUT, "#pragma once\n\nnamespace web::toggles {}\n")
    sys.exit(0)

functions = "\n\n".join(
    _TOGGLE_FN.substitute(name=name, default_str="true" if default else "false")
    for name, _route, default in toggles
)
write_file(OUT, _HEADER.substitute(functions=functions))

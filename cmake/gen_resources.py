#!/usr/bin/env python3
"""Parses revet.toml [resources.*] and generates src/resources.hpp + src/db_backends.cmake."""
import sys, tomllib
from pathlib import Path
from codegen import tmpl as _tmpl, setup, write_lines, write_file, static_tmpl

_POOL_FN     = _tmpl("resources_pool_fn.tmpl")
_HANDLE_FN   = _tmpl("resources_handle_fn.tmpl")
_DB_ALIAS    = _tmpl("resources_db_alias.tmpl")

PROJ, OUT    = setup("resources.hpp")
TOML         = PROJ / "revet.toml"
BACKENDS_OUT = PROJ / "build" / "generated" / "db_backends.cmake"

PATHS_BLOCK = static_tmpl("resources_paths_block.tmpl")


def _exit_empty(msg):
    write_file(OUT, f"// Auto-generated — do not edit\n#pragma once\n\n{PATHS_BLOCK}\n\n{msg}\n")
    write_file(BACKENDS_OUT, "set(REVET_DB_BACKENDS)\n")
    sys.exit(0)


if not TOML.exists():
    _exit_empty("// no revet.toml")

with TOML.open("rb") as f:
    data = tomllib.load(f)

thread_count = int(data.get("thread_count", 4))

# Parse resources
raw_resources = data.get("resources", {})
resources = {}
for name, r in raw_resources.items():
    resources[name] = {
        "type":    r.get("type", "sqlite"),
        "path":    r.get("path", r.get("url", f"app/data/{name}.db")),
        "pool":    int(r.get("pool_size", thread_count)),
        "default": bool(r.get("default", False)),
    }

if not resources:
    _exit_empty("// no [resources.*] in revet.toml")

NS = {"sqlite": "db::sqlite", "postgres": "db::postgres"}

default = next((n for n, r in resources.items() if r["default"]),
               next(iter(resources)))

out = ["// Auto-generated — do not edit", "#pragma once", '#include <revet/db.hpp>',
       "", PATHS_BLOCK,
       "", "namespace resources {", "namespace _pools {", ""]

pool_lines, handle_lines = [], []
for name, r in resources.items():
    ns_str = NS[r["type"]]
    pool_lines.append(_POOL_FN.substitute(ns=ns_str, name=name, path=r["path"], pool=r["pool"]))
    pool_lines.append("")
    handle_lines.append(_HANDLE_FN.substitute(ns=ns_str, name=name))

out += pool_lines + ["} // namespace _pools", ""] + handle_lines

if default != "db":
    out.append("")
    out.append(_DB_ALIAS.substitute(ns=NS[resources[default]["type"]], default=default))

out += ["", "} // namespace resources", ""]
write_lines(OUT, out)

backends = list({r["type"] for r in resources.values()})
write_file(BACKENDS_OUT, f"# Auto-generated — do not edit\nset(REVET_DB_BACKENDS {' '.join(backends)})\n")

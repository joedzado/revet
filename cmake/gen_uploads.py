#!/usr/bin/env python3
"""Parses revet.toml [uploads.*] and generates build/generated/uploads.hpp."""
import sys, tomllib
from pathlib import Path
from codegen import tmpl, file_header, setup, write_lines, write_file

_UPLOAD_NS = tmpl("uploads_namespace.tmpl")

PROJ, OUT = setup("uploads.hpp")
TOML = PROJ / "revet.toml"

EMPTY = "#pragma once\n// no [uploads.*] in revet.toml\nnamespace uploads {}\n"

if not TOML.exists():
    write_file(OUT, EMPTY)
    sys.exit(0)

with TOML.open("rb") as f:
    data = tomllib.load(f)

raw_dirs = data.get("uploads", {})
dirs = {
    name: {
        "incoming":  d.get("incoming",  f"app/uploads/{name}/incoming"),
        "validated": d.get("validated", f"app/uploads/{name}/validated"),
    }
    for name, d in raw_dirs.items()
}

if not dirs:
    write_file(OUT, EMPTY)
    sys.exit(0)

# Create dirs at configure time (incoming 0o700, validated 0o750)
import os
for name, d in dirs.items():
    for key, mode in (("incoming", 0o700), ("validated", 0o750)):
        p = PROJ / d[key]
        p.mkdir(parents=True, exist_ok=True)
        os.chmod(p, mode)

out = file_header("#include <filesystem>", "#include <string_view>")
out += ["namespace uploads {", ""]

for name, d in dirs.items():
    incoming  = d["incoming"]
    validated = d["validated"]
    out.append(_UPLOAD_NS.substitute(name=name, incoming=incoming, validated=validated))
    out.append("")

out += ["} // namespace uploads", ""]
write_lines(OUT, out)

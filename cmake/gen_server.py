#!/usr/bin/env python3
"""Reads server settings from revet.toml (top-level `max_body`) and generates
build/generated/server.hpp. The generated header sets the value during static
initialisation, so REVET_MAX_BODY and --max-body (read later by web::init) still win."""
import re, sys, tomllib
from codegen import setup, write_file

PROJ, OUT = setup("server.hpp")
TOML = PROJ / "revet.toml"

HEADER = "// Auto-generated from revet.toml — do not edit\n#pragma once\n#include <revet/web.hpp>\n"


def to_bytes(value):
    """Integer bytes, or a string like "64MB" / "512KiB" / "1g". Binary units."""
    if isinstance(value, bool):
        raise ValueError("max_body must be a size, not a boolean")
    if isinstance(value, int):
        n = value
    else:
        m = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*([kmg]?i?b?)\s*", str(value), re.I)
        if not m:
            raise ValueError(f'max_body = "{value}" is not a size (try "64MB")')
        unit = m.group(2).lower().rstrip("b").rstrip("i")
        n = int(float(m.group(1)) * {"": 1, "k": 1024, "m": 1024**2, "g": 1024**3}[unit])
    if n < 1:
        raise ValueError("max_body must be at least 1 byte")
    return n


max_body = None
data_dir = None
if TOML.exists():
    with TOML.open("rb") as f:
        data = tomllib.load(f)
    if isinstance(data.get("data_dir"), str):
        data_dir = data["data_dir"]
    if "max_body" in data:
        try:
            max_body = to_bytes(data["max_body"])
        except ValueError as e:
            sys.exit(f"revet.toml: {e}")

# Applied at static init, so before web::init: env vars and flags still win.
body = []
if max_body is None:
    body.append("// max_body not set: web::config::DEFAULT_MAX_BODY_BYTES applies")
else:
    body.append(f"inline const bool max_body_set = (web::config::max_body_bytes({max_body}UL), true);")
if data_dir is not None:
    escaped = data_dir.replace("\\", "\\\\").replace('"', '\\"')
    body.append(f'inline const bool data_dir_set = (web::config::data_dir("{escaped}"), true);')
write_file(OUT, HEADER + "namespace revet::generated {\n" + "\n".join(body) + "\n} // namespace revet::generated\n")

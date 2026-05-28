#!/usr/bin/env python3
"""Reads revet.toml for htmx version, embeds chosen vendor file into htmx_asset.hpp."""
import sys, re
from pathlib import Path
from codegen import setup, file_header, write_lines, escape_str

DEFAULT_VERSION = "2.0.9"

PROJ, OUT = setup("htmx_asset.hpp")

# Read version from revet.toml [htmx] version = "..."
version = DEFAULT_VERSION
toml = PROJ / "revet.toml"
if toml.exists():
    for line in toml.read_text().splitlines():
        m = re.match(r'^\s*version\s*=\s*"([^"]+)"', line)
        if m:
            # Only pick it up if we're inside [htmx] section
            pass  # handled below with section tracking

    in_htmx = False
    for line in toml.read_text().splitlines():
        if re.match(r'^\s*\[htmx\]', line):
            in_htmx = True
            continue
        if in_htmx:
            if line.strip().startswith("["):
                break
            m = re.match(r'^\s*version\s*=\s*"([^"]+)"', line)
            if m:
                version = m.group(1)
                break

FRAMEWORK_DIR = Path(__file__).resolve().parent.parent
vendor_file = FRAMEWORK_DIR / "vendor" / "htmx" / version / "htmx.min.js"

if not vendor_file.exists():
    sys.exit(f"gen_htmx: htmx {version} not vendored — run: python3 scripts/vendor_htmx.py")

js_content = vendor_file.read_text()
escaped = escape_str(js_content)

out = file_header("#include <string_view>")
out += [
    "namespace revet::detail {",
    f'inline constexpr std::string_view htmx_version = "{version}";',
    f'inline constexpr std::string_view htmx_js = "{escaped}";',
    "} // namespace revet::detail",
    "",
]

write_lines(OUT, out)

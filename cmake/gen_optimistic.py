#!/usr/bin/env python3
"""Compiles static/optimistic.ts with Bun and embeds into optimistic_asset.hpp."""
import sys, subprocess
from pathlib import Path
from codegen import setup, file_header, write_lines, escape_str

PROJ, OUT = setup("optimistic_asset.hpp")

FRAMEWORK_DIR = Path(__file__).resolve().parent.parent
TS_FILE = FRAMEWORK_DIR / "static" / "optimistic.ts"

if not TS_FILE.exists():
    sys.exit(f"gen_optimistic: {TS_FILE} not found")

import shutil, tempfile
bun = shutil.which("bun") or str(Path.home() / ".bun" / "bin" / "bun")

with tempfile.NamedTemporaryFile(suffix=".js", delete=False) as tmp:
    js_tmp = Path(tmp.name)

result = subprocess.run(
    [bun, "build", "--minify", str(TS_FILE), "--outfile", str(js_tmp)],
    capture_output=True, text=True,
)
if result.returncode != 0:
    js_tmp.unlink(missing_ok=True)
    sys.exit(f"gen_optimistic: bun build failed:\n{result.stderr}")

escaped = escape_str(js_tmp.read_text())
js_tmp.unlink(missing_ok=True)

out = file_header("#include <string_view>")
out += [
    "namespace revet::detail {",
    f'inline constexpr std::string_view optimistic_js = "{escaped}";',
    "} // namespace revet::detail",
    "",
]

write_lines(OUT, out)

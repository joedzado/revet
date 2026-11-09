import sys, re
from pathlib import Path
from string import Template

TMPL_DIR = Path(__file__).parent / "templates"

def setup(filename):
    proj = Path(sys.argv[1])
    out = proj / "build" / "generated" / filename
    out.parent.mkdir(parents=True, exist_ok=True)
    return proj, out

def write_file(path, content):
    if path.exists() and path.read_text() == content:
        return
    path.write_text(content)

def write_lines(path, lines):
    write_file(path, "\n".join(lines))

def static_tmpl(name):
    return (TMPL_DIR / name).read_text().rstrip('\n')

def tmpl(name):
    return Template((TMPL_DIR / name).read_text().rstrip('\n'))

def tmpl_whole(name):
    return Template((TMPL_DIR / name).read_text())

def file_header(*includes):
    return ["// Auto-generated — do not edit", "#pragma once", *includes, ""]

STATE_FLASH_RE      = re.compile(r"web::(State|Flash)<(\w+)>")
# Catches write-only state usage: .state(T{), .flash(T{), has<T>, clear<T>()
STATE_WRITE_RE      = re.compile(r"\.(?:state|flash)\((\w+)\s*\{")
STATE_TEMPLATE_RE   = re.compile(r"(?:has|clear)<(\w+)>")

def split_args(s: str) -> list[str]:
    """Split on commas respecting <>, (), [] nesting."""
    args, depth, buf = [], 0, []
    for ch in s:
        if ch in '<([': depth += 1
        elif ch in '>)]': depth -= 1
        if ch == ',' and depth == 0:
            args.append(''.join(buf).strip())
            buf = []
        else:
            buf.append(ch)
    if buf:
        args.append(''.join(buf).strip())
    return [a for a in args if a]

def has_models(models_dir) -> bool:
    return models_dir.exists() and any(models_dir.glob("*.hpp"))

def escape_str(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "\\r").replace("\n", "\\n")

def rel_include(hpp_path, proj) -> str:
    return f'#include "../../{hpp_path.relative_to(proj)}"'

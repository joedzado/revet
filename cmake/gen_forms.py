#!/usr/bin/env python3
"""Scans forms/*.hpp for [[revet::form]] structs and generates forms.hpp."""
import sys, re
from pathlib import Path
from codegen import tmpl, tmpl_whole, setup, rel_include, write_file

_HEADER             = tmpl_whole("forms_header.tmpl")
_POPULATE_FN        = tmpl("forms_populate_fn.tmpl")
_VALIDATE_FN        = tmpl("forms_validate_fn.tmpl")
_POPULATE_OPTIONAL  = tmpl("forms_populate_optional.tmpl")
_POPULATE_REQUIRED  = tmpl("forms_populate_required.tmpl")
_VALIDATE_OPTIONAL  = tmpl("forms_validate_optional.tmpl")
_VALIDATE_REQUIRED  = tmpl("forms_validate_required.tmpl")

PROJ, OUT = setup("forms.hpp")
FORMS_DIR = PROJ / "app" / "forms"

form_files = sorted(FORMS_DIR.glob("*.hpp")) if FORMS_DIR.exists() else []
if not form_files:
    write_file(OUT, "// no forms/*.hpp files found\n#pragma once\nnamespace forms {}\n")
    sys.exit(0)

# ── parse helpers ─────────────────────────────────────────────────────────────

def find_form_structs(src):
    """Yield (struct_name, body_str) for each [[revet::form]] struct."""
    pattern = re.compile(r'struct\s+\[\[revet::form\]\]\s+(\w+)\s*\{')
    for m in pattern.finditer(src):
        name  = m.group(1)
        start = m.end()
        depth = 1
        i     = start
        while i < len(src) and depth > 0:
            if   src[i] == '{': depth += 1
            elif src[i] == '}': depth -= 1
            i += 1
        yield name, src[start:i - 1]

def parse_members(body):
    """Return list of (member_name, is_optional) from a struct body."""
    members = []
    i = 0
    while i < len(body):
        # skip whitespace
        while i < len(body) and body[i] in ' \t\n\r':
            i += 1
        if i >= len(body):
            break
        # collect one statement (up to ';', respecting <> and () depth)
        depth = 0
        j = i
        while j < len(body):
            if body[j] in '<(':
                depth += 1
            elif body[j] in '>)':
                depth -= 1
            elif body[j] == ';' and depth == 0:
                break
            j += 1
        decl = body[i:j].strip()
        i = j + 1
        if not decl or 'revet::Field' not in decl:
            continue
        is_optional = decl.startswith('std::optional')
        # strip initializer {…} to isolate the declarator
        decl_no_init = re.sub(r'\{[^}]*\}', '', decl).strip()
        m = re.search(r'(\w+)\s*$', decl_no_init)
        if m:
            members.append((m.group(1), is_optional))
    return members

# ── emit ──────────────────────────────────────────────────────────────────────

includes = "\n".join(rel_include(hpp, PROJ) for hpp in form_files)

functions = []
for hpp in form_files:
    src = hpp.read_text()
    for struct_name, body in find_form_structs(src):
        members = parse_members(body)
        if not members:
            continue

        pop_lines = [
            (_POPULATE_OPTIONAL if opt else _POPULATE_REQUIRED).substitute(mname=m)
            for m, opt in members
        ]
        val_lines = [
            (_VALIDATE_OPTIONAL if opt else _VALIDATE_REQUIRED).substitute(mname=m)
            for m, opt in members
        ]
        functions.append(_POPULATE_FN.substitute(struct_name=struct_name, body="\n".join(pop_lines)))
        functions.append(_VALIDATE_FN.substitute(struct_name=struct_name, body="\n".join(val_lines)))

write_file(OUT, _HEADER.substitute(includes=includes, functions="\n\n".join(functions)) + "\n")

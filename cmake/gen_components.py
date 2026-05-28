#!/usr/bin/env python3
import sys, re
from pathlib import Path
from codegen import tmpl as _tmpl, setup, write_lines, write_file, split_args, has_models, rel_include

_EXPORT_DECL    = _tmpl("components_export.tmpl")
_TRAITS_STRUCT  = _tmpl("components_traits.tmpl")



PROJ, OUT = setup("components.hpp")

COMPONENTS_DIR = PROJ / "app" / "components"
LAYOUTS_DIR = PROJ / "app" / "layouts"
MODELS_DIR = PROJ / "models"

cpp_files = sorted([
    *COMPONENTS_DIR.glob("*.cpp"),
    *LAYOUTS_DIR.glob("*.cpp"),
])

if not cpp_files:
    write_file(OUT, "// no components/*.cpp or layouts/*.cpp files found\n#pragma once\n")
    sys.exit(0)

out = [
    "// Auto-generated — do not edit",
    "#pragma once",
    '#include <revet/web.hpp>',
    '#include "states.hpp"',
]

# Include user type headers so exported function signatures resolve.
# Convention: models/*.hpp + components/*.hpp + types/*.hpp.
# Paths are relative to the generated file location (build/generated/).
if has_models(MODELS_DIR):
    out.append('#include "models.hpp"')  # generated aggregate in build/generated/
for hpp in sorted(COMPONENTS_DIR.glob("*.hpp")):
    out.append(rel_include(hpp, PROJ))
for hpp in sorted((PROJ / "types").glob("*.hpp")):
    out.append(rel_include(hpp, PROJ))

out.append("")

comp_deps = {}

for cpp_file in cpp_files:
    content = cpp_file.read_text()
    
    for m_export in re.finditer(r'\[\[revet::export\]\]', content):
        start = m_export.end()
        paren_open = content.find('(', start)
        if paren_open == -1: continue
        
        head = content[start:paren_open].strip()
        head_parts = head.split()
        if not head_parts: continue
        name = head_parts[-1]
        ret_type = " ".join(head_parts[:-1])
        
        paren_close = content.find(')', paren_open)
        if paren_close == -1: continue
        
        args_str = content[paren_open+1:paren_close].strip()
        
        # We give the State/Flash arguments default values in the header
        # to allow calling the component without passing them explicitly.
        # layout_main(title, inner, web::Url path, std::string_view q, web::State<Theme> theme = web::State<Theme>::current())
        
        header_args = []
        deps = []
        for arg in split_args(args_str):
            arg = arg.strip()
            if not arg: continue

            # Remove any existing default value if present to avoid duplication
            arg_base = arg.split("=")[0].strip()

            if sm := re.search(r'web::(State|Flash)<(\w+)>', arg_base):
                mode, tname = sm.groups()
                header_args.append(f"{arg_base} = web::{mode}<{tname}>::current()")
                deps.append((mode, tname))
            else:
                header_args.append(arg)

        line_no = content.count("\n", 0, start) + 1
        out.append(_EXPORT_DECL.substitute(line_no=line_no, cpp_file=cpp_file,
                                           ret_type=ret_type, name=name,
                                           args_str=', '.join(header_args)))
        
        if deps:
            comp_deps[name] = deps

out.append("")
out.append("namespace web::components {")
for name, deps in comp_deps.items():
    dep_list = ", ".join(f'"{mode}", "{t}"' for mode, t in deps)
    out.append(_TRAITS_STRUCT.substitute(name=name, dep_list=dep_list))
out.append("}")

write_lines(OUT, out)

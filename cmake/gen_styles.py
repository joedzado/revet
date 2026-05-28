#!/usr/bin/env python3
"""Parses styles/*.css and generates src/styles.hpp."""
import sys, re
from pathlib import Path
from codegen import tmpl as _tmpl, file_header, setup, write_lines, escape_str

_NAMED_CLASS = _tmpl("styles_named_class.tmpl")
_ID          = _tmpl("styles_id.tmpl")

PROJ, OUT = setup("styles.hpp")

styles_dir = PROJ / "app" / "styles"
if not styles_dir.exists():
    sys.exit(f"gen_styles: styles directory not found: {styles_dir}")
css_files = sorted(styles_dir.glob("*.css"))

out = file_header('#include <revet/css.hpp>')
out += ["namespace styles {", ""]

all_css = []
defined_classes = set()
defined_ids     = {}  # cpp_name → css_name

for css_file in css_files:
    content = css_file.read_text()
    all_css.append(content)

    pending_selectors = []  # lines before a { that belong to the same block
    for i, line in enumerate(content.splitlines(), 1):
        stripped = line.strip()
        if not stripped:
            pending_selectors.clear()
            continue

        has_open  = "{" in stripped
        ends_comma = stripped.rstrip().endswith(",")

        if not has_open and not ends_comma:
            pending_selectors.clear()
            continue

        # Collect candidate lines: pending carry-overs + current
        candidates = pending_selectors + [(i, stripped)]

        if has_open:
            # Flush all accumulated selectors into this block
            for li, sel in candidates:
                for m in re.finditer(r'\.([A-Za-z][A-Za-z0-9_-]*)', sel):
                    css_name = m.group(1)
                    cpp_name = css_name.replace("-", "_")
                    if cpp_name not in defined_classes:
                        out.append(_NAMED_CLASS.substitute(line=li, file=css_file, cpp_name=cpp_name, css_name=css_name))
                        defined_classes.add(cpp_name)
                for m in re.finditer(r'#([A-Za-z][A-Za-z0-9_-]*)', sel):
                    css_name = m.group(1)
                    cpp_name = css_name.replace("-", "_")
                    if cpp_name not in defined_ids:
                        defined_ids[cpp_name] = css_name
            pending_selectors.clear()
        else:
            # Comma-ended selector line — carry forward
            pending_selectors = candidates

out.append("} // namespace styles")
out.append("")

if defined_ids:
    out.append("namespace ids {")
    out.append("")
    for cpp_name, css_name in sorted(defined_ids.items()):
        out.append(_ID.substitute(cpp_name=cpp_name, css_name=css_name))
    out.append("")
    out.append("} // namespace ids")
out.append("")
out.append("namespace html::detail {")
escaped = escape_str("\n".join(all_css))
out.append(f'inline const std::string _all_css_str = "{escaped}";')
out.append("inline const bool _all_css_init = ([]{{ all_css_block = _all_css_str; return true; }})();")
out.append("} // namespace html::detail")
out.append("")

write_lines(OUT, out)

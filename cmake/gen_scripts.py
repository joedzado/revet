#!/usr/bin/env python3
"""Embeds app/scripts/*.js into scripts.hpp; routes.cpp serves each at a content-hashed URL.

For app/scripts/chart.js the header has:
  scripts::chart      — "/_app/chart.<hash>.js", safe to cache forever
  scripts::chart_js   — the source
  scripts::chart_tag()— <script src=... defer>
"""
import hashlib, re, sys
from pathlib import Path
from codegen import setup, file_header, write_lines, escape_str

PROJ, OUT = setup("scripts.hpp")
SCRIPTS = PROJ / "app" / "scripts"


def ident(stem: str) -> str:
    name = re.sub(r"\W", "_", stem)
    return f"_{name}" if name[0].isdigit() else name


def scan() -> list[tuple[str, str, str]]:
    """(identifier, url, source) for each script, sorted by name."""
    if not SCRIPTS.exists():
        return []
    out = []
    for f in sorted(SCRIPTS.glob("*.js")):
        src = f.read_text().replace("\r\n", "\n")
        digest = hashlib.sha256(src.encode()).hexdigest()[:10]
        out.append((ident(f.stem), f"/_app/{f.stem}.{digest}.js", src))
    return out


def main() -> None:
    lines = file_header("#include <revet/html.hpp>", "#include <string_view>")
    lines.append("namespace scripts {")
    for name, url, src in scan():
        lines.append(f'inline constexpr std::string_view {name} = "{url}";')
        lines.append(f'inline constexpr std::string_view {name}_js = "{escape_str(src)}";')
        lines.append(f"inline auto {name}_tag() -> html::Node {{")
        lines.append(f'    return html::script{{html::attr.src({name}), html::attr("defer", "")}};')
        lines.append("}")
    lines += ["} // namespace scripts", ""]
    write_lines(OUT, lines)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Reads locale/strings.csv and generates build/generated/i18n.hpp."""
import sys, csv
from pathlib import Path
from codegen import tmpl, tmpl_whole, setup, escape_str, write_file

_STR_CONST = tmpl("i18n_str_const.tmpl")
_HEADER    = tmpl_whole("i18n_header.tmpl")


# ISO 639-1 two-letter codes (BCP 47 primary subtags).
ISO_639_1 = {
    "af","sq","am","ar","hy","az","eu","be","bn","bs","bg","ca","zh","co","hr",
    "cs","da","nl","en","eo","et","fi","fr","fy","gl","ka","de","el","gu","ht",
    "ha","he","hi","hu","is","ig","id","ga","it","ja","jv","kn","kk","km","ko",
    "ku","ky","lo","la","lv","lt","lb","mk","mg","ms","ml","mt","mi","mr","mn",
    "my","ne","no","ny","or","ps","fa","pl","pt","pa","ro","ru","sm","gd","sr",
    "st","sn","sd","si","sk","sl","so","es","su","sw","sv","tl","tg","ta","tt",
    "te","th","tr","tk","uk","ur","ug","uz","vi","cy","xh","yi","yo","zu",
}

PROJ, OUT = setup("i18n.hpp")
CSV  = PROJ / "app" / "locale" / "strings.csv"

if not CSV.exists():
    write_file(OUT, "#pragma once\n\nnamespace i18n {}\n")
    sys.exit(0)

rows = []
with CSV.open(newline="", encoding="utf-8") as f:
    reader = csv.DictReader(f)
    locales = [c for c in reader.fieldnames if c != "key"]
    for row in reader:
        rows.append(row)

# Validate locale codes against ISO 639-1.
errors = [l for l in locales if l not in ISO_639_1]
if errors:
    print(f"ERROR gen_i18n: unrecognized locale code(s): {', '.join(errors)}", file=sys.stderr)
    print(f"  Locale column headers must be ISO 639-1 two-letter codes (e.g. en, es, fr, de).", file=sys.stderr)
    sys.exit(1)

n = len(locales)

# Locale picker HTML — a small form that POSTs to /set_locale.
# Generated inline so it automatically reflects the project's locale list.
picker_buttons = "\n        ".join(
    f'html::button{{html::attr.type("submit"), html::attr.name("lang"), html::attr.value("{l}"), '
    f'styles::btn, detail::locale_idx == {i} ? html::Node(styles::btn_solid) : html::Node(styles::btn_ghost), '
    f'"{l.upper()}"}},'
    for i, l in enumerate(locales)
)

str_consts = "\n".join(
    _STR_CONST.substitute(n=n, key=row["key"], translations=", ".join(f'"{escape_str(row[l])}"' for l in locales))
    for row in rows
)

write_file(OUT, _HEADER.substitute(
    locale_enum=", ".join(locales),
    locale_names_arr=", ".join(f'"{l}"' for l in locales),
    n=n,
    picker_buttons=picker_buttons,
    str_consts=str_consts,
))

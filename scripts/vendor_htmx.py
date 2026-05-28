#!/usr/bin/env python3
"""Download and vendor htmx releases into vendor/htmx/{version}/htmx.min.js."""
import urllib.request, sys
from pathlib import Path

VERSIONS = [
    "2.0.4",
    "2.0.5",
    "2.0.6",
    "2.0.7",
    "2.0.8",
    "2.0.9",
    "4.0.0-beta1",
    "4.0.0-beta2",
    "4.0.0-beta3",
    "4.0.0-beta4",
]

REPO = Path(__file__).resolve().parent.parent
VENDOR = REPO / "vendor" / "htmx"

def download(version: str):
    url = f"https://unpkg.com/htmx.org@{version}/dist/htmx.min.js"
    dest = VENDOR / version / "htmx.min.js"
    if dest.exists():
        print(f"  {version}  already vendored, skipping")
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"  {version}  downloading...", end=" ", flush=True)
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:
            dest.write_bytes(resp.read())
        print(f"ok ({dest.stat().st_size // 1024}KB)")
    except Exception as e:
        print(f"FAILED: {e}")
        dest.unlink(missing_ok=True)
        sys.exit(1)

print(f"Vendoring htmx into {VENDOR}")
for v in VERSIONS:
    download(v)
print("Done.")

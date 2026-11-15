#!/bin/sh
# Download htmx releases into vendor/htmx/<version>/htmx.min.js.
set -eu

VERSIONS="2.0.4 2.0.5 2.0.6 2.0.7 2.0.8 2.0.9 4.0.0-beta1 4.0.0-beta2 4.0.0-beta3 4.0.0-beta4"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR="$REPO/vendor/htmx"

echo "Vendoring htmx into $VENDOR"
for v in $VERSIONS; do
    dest="$VENDOR/$v/htmx.min.js"
    if [ -f "$dest" ]; then
        echo "  $v  already vendored, skipping"
        continue
    fi
    mkdir -p "$VENDOR/$v"
    printf '  %s  downloading... ' "$v"
    tmp="$dest.part"
    if curl -fsSL --max-time 30 -o "$tmp" "https://unpkg.com/htmx.org@$v/dist/htmx.min.js"; then
        mv "$tmp" "$dest"
        echo "ok ($(( $(wc -c < "$dest") / 1024 ))KB)"
    else
        rm -f "$tmp"
        echo "FAILED"
        exit 1
    fi
done
echo "Done."

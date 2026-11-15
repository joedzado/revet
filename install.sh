#!/bin/bash
# revet installer — builds the framework from source on this machine.
# Usage: curl -fsSL https://raw.githubusercontent.com/joedzado/revet/main/install.sh | sh

set -e

REPO="https://github.com/joedzado/revet.git"
SRC_DIR="$HOME/.revet/src"
BUILD_DIR="$SRC_DIR/build"
PREFIX="$HOME/.revet/current"
BIN_LINK="/usr/local/bin/revet"

info()  { printf "\033[36m%s\033[0m\n" "$*"; }
ok()    { printf "\033[32m✔ %s\033[0m\n" "$*"; }
err()   { printf "\033[31m✗ %s\033[0m\n" "$*" >&2; exit 1; }
warn()  { printf "\033[33m⚠ %s\033[0m\n" "$*"; }

# ── preflight ──────────────────────────────────────────────────────────────────
command -v git >/dev/null 2>&1 || err "git is required. Install it and re-run."

OS=$(uname -s)
case "$OS" in Darwin|Linux) ;; *) err "Unsupported OS: $OS" ;; esac

# ── ensure ~/.revet is user-writable ──────────────────────────────────────────
REVET_HOME="$HOME/.revet"
if [ -e "$REVET_HOME" ] && [ ! -w "$REVET_HOME" ]; then
    warn "$REVET_HOME is not writable by $(whoami) (likely root-owned from a prior dev install). Fixing ownership..."
    sudo chown -R "$(whoami)" "$REVET_HOME" || err "Could not chown $REVET_HOME. Run: sudo chown -R \$(whoami) $REVET_HOME"
fi

# ── clone / update framework source ───────────────────────────────────────────
if [ -d "$SRC_DIR/.git" ]; then
    info "Updating revet source..."
    git -C "$SRC_DIR" pull --ff-only
else
    info "Cloning revet..."
    git clone --depth 1 "$REPO" "$SRC_DIR"
fi

# ── system tools (cmake, C++ compiler, ninja) ─────────────────────────────────
info "Checking system dependencies..."
bash "$SRC_DIR/scripts/install_deps.sh"

command -v cmake >/dev/null 2>&1 || err "cmake is required (>= 3.20)."
command -v clang++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || err "A C++23 compiler (clang++ >= 17 or g++ >= 13) is required."
command -v python3 >/dev/null 2>&1 || err "python3 is required by the revet CLI."

# ── build from source ─────────────────────────────────────────────────────────
info "Building revet..."
cd "$SRC_DIR"
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
cmake "${GEN[@]}" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

# ── install ───────────────────────────────────────────────────────────────────
info "Installing revet to $PREFIX..."
rm -rf "$PREFIX"
mkdir -p "$PREFIX"
for d in cmake src static template include vendor CMakeLists.txt conanfile.py LICENSE VERSION; do
    [ -e "$SRC_DIR/$d" ] || continue
    if [ -d "$SRC_DIR/$d" ]; then
        cp -R "$SRC_DIR/$d" "$PREFIX/$d"
    else
        cp "$SRC_DIR/$d" "$PREFIX/$d"
    fi
done
cp "$SRC_DIR/bin/revet" "$PREFIX/revet"
chmod +x "$PREFIX/revet"

if [ -w "$(dirname "$BIN_LINK")" ]; then
    ln -sf "$PREFIX/revet" "$BIN_LINK"
else
    sudo ln -sf "$PREFIX/revet" "$BIN_LINK"
fi

# ── done ──────────────────────────────────────────────────────────────────────
ok "revet installed!"
echo ""
echo "  Create a new project:"
echo "    revet new myapp"
echo "    cd myapp"
echo "    revet dev"
echo ""

if ! echo "$PATH" | grep -q "/usr/local/bin"; then
    warn "/usr/local/bin is not in your PATH."
    warn "Add this to your shell profile (~/.bashrc or ~/.zshrc):"
    echo '    export PATH="/usr/local/bin:$PATH"'
    echo ""
fi

echo "  Docs: https://github.com/joedzado/revet"

#!/bin/bash
# Builds and installs the revet checkout this script lives in (branch and uncommitted
# changes included) to ~/.revet/current, and links /usr/local/bin/revet.
#   scripts/install_local.sh
# install.sh calls this after cloning; run it directly to install a checkout or worktree.
set -e

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$SRC_DIR/build"
PREFIX="$HOME/.revet/current"
BIN_LINK="/usr/local/bin/revet"

info()  { printf "\033[36m%s\033[0m\n" "$*"; }
ok()    { printf "\033[32m✔ %s\033[0m\n" "$*"; }
err()   { printf "\033[31m✗ %s\033[0m\n" "$*" >&2; exit 1; }
warn()  { printf "\033[33m⚠ %s\033[0m\n" "$*"; }

OS=$(uname -s)
case "$OS" in Darwin|Linux) ;; *) err "Unsupported OS: $OS" ;; esac

# ── ensure ~/.revet is user-writable ──────────────────────────────────────────
REVET_HOME="$HOME/.revet"
if [ -e "$REVET_HOME" ] && [ ! -w "$REVET_HOME" ]; then
    warn "$REVET_HOME is not writable by $(whoami) (likely root-owned from a prior dev install). Fixing ownership..."
    sudo chown -R "$(whoami)" "$REVET_HOME" || err "Could not chown $REVET_HOME. Run: sudo chown -R \$(whoami) $REVET_HOME"
fi

# ── tools ─────────────────────────────────────────────────────────────────────
command -v cmake >/dev/null 2>&1 || err "cmake is required (>= 3.20). Run: bash $SRC_DIR/scripts/install_deps.sh"
command -v clang++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || err "A C++23 compiler (clang++ >= 17 or g++ >= 13) is required."
command -v conan >/dev/null 2>&1 || err "conan is required (C++ packages). Run: bash $SRC_DIR/scripts/install_deps.sh"

# ── build ─────────────────────────────────────────────────────────────────────
info "Building revet from $SRC_DIR ($(git -C "$SRC_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "no git"))..."
cd "$SRC_DIR"
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
# macOS: Apple's clang unless CC/CXX say otherwise (Homebrew LLVM lacks the SDK setup).
if [ "$OS" = Darwin ]; then
    export CC="${CC:-/usr/bin/clang}" CXX="${CXX:-/usr/bin/clang++}"
fi
cmake "${GEN[@]}" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

# ── install ───────────────────────────────────────────────────────────────────
info "Installing revet to $PREFIX..."
rm -rf "$PREFIX"
mkdir -p "$PREFIX"
for d in cmake cli src static template include vendor scripts CMakeLists.txt conanfile.txt .clang-format .clang-tidy LICENSE VERSION; do
    [ -e "$SRC_DIR/$d" ] || continue
    cp -R "$SRC_DIR/$d" "$PREFIX/$d"
done
# The revet CLI, the codegen tool and the vendored static libs, built once here and reused by every app build.
cmake --install "$BUILD_DIR" --prefix "$PREFIX/prebuilt" >/dev/null
ln -sf "$PREFIX/prebuilt/bin/revet" "$PREFIX/revet"

if [ -w "$(dirname "$BIN_LINK")" ]; then
    ln -sf "$PREFIX/revet" "$BIN_LINK"
else
    sudo ln -sf "$PREFIX/revet" "$BIN_LINK"
fi

# ── done ──────────────────────────────────────────────────────────────────────
ok "revet $(cat "$SRC_DIR/VERSION") installed!"
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

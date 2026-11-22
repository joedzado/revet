#!/bin/bash
# revet installer.
#
#   curl -fsSL https://raw.githubusercontent.com/joedzado/revet/main/install.sh | bash
#       installs system dependencies, clones GitHub main into ~/.revet/src, builds and installs it
#   ./install.sh            (from a checkout or worktree)
#       builds and installs that checkout: same as scripts/install_local.sh
#
# The build and install itself is scripts/install_local.sh.
set -e

REPO="https://github.com/joedzado/revet.git"
SRC_DIR="$HOME/.revet/src"

info()  { printf "\033[36m%s\033[0m\n" "$*"; }
err()   { printf "\033[31m✗ %s\033[0m\n" "$*" >&2; exit 1; }

# Run from a checkout: install it.
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [ -f "$HERE/scripts/install_local.sh" ]; then
        exec bash "$HERE/scripts/install_local.sh"
    fi
fi

# Piped from curl: fetch the source, install the tools, then build and install it.
command -v git >/dev/null 2>&1 || err "git is required. Install it and re-run."
if [ -d "$SRC_DIR/.git" ]; then
    info "Updating revet source..."
    git -C "$SRC_DIR" pull --ff-only
else
    info "Cloning revet..."
    git clone --depth 1 "$REPO" "$SRC_DIR"
fi

info "Checking system dependencies..."
bash "$SRC_DIR/scripts/install_deps.sh"

exec bash "$SRC_DIR/scripts/install_local.sh"

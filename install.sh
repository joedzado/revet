#!/bin/bash
# revet installer — https://github.com/joedzado/revet
# Usage: curl -fsSL https://raw.githubusercontent.com/joedzado/revet/main/install.sh | sh

set -e

REPO="https://github.com/joedzado/revet.git"
INSTALL_DIR="$HOME/.revet/src"

info()  { printf "\033[36m%s\033[0m\n" "$*"; }
ok()    { printf "\033[32m✔ %s\033[0m\n" "$*"; }
err()   { printf "\033[31m✗ %s\033[0m\n" "$*" >&2; exit 1; }
warn()  { printf "\033[33m⚠ %s\033[0m\n" "$*"; }

# ── preflight ──────────────────────────────────────────────────────────────────
command -v git    >/dev/null 2>&1 || err "git is required. Install it and re-run."
command -v python3 >/dev/null 2>&1 || err "python3 is required. Install it and re-run."

OS=$(uname -s)
case "$OS" in Darwin|Linux) ;; *) err "Unsupported OS: $OS" ;; esac

# ── ensure ~/.revet is user-writable ──────────────────────────────────────────
REVET_HOME="$HOME/.revet"
if [ -e "$REVET_HOME" ] && [ ! -w "$REVET_HOME" ]; then
    warn "$REVET_HOME exists but is not writable by $(whoami) (likely root-owned from a prior dev install)."
    warn "Fixing ownership..."
    sudo chown -R "$(whoami)" "$REVET_HOME" || err "Could not chown $REVET_HOME. Run: sudo chown -R \$(whoami) $REVET_HOME"
fi

# ── clone / update framework source ───────────────────────────────────────────
if [ -d "$INSTALL_DIR/.git" ]; then
    info "Updating existing revet source..."
    git -C "$INSTALL_DIR" pull --ff-only
else
    info "Cloning revet..."
    git clone --depth 1 "$REPO" "$INSTALL_DIR"
fi

# ── install system dependencies ────────────────────────────────────────────────
info "Installing system dependencies..."
bash "$INSTALL_DIR/scripts/install_deps.sh"

# ── install framework ─────────────────────────────────────────────────────────
info "Installing revet framework..."
cd "$INSTALL_DIR"

# revet-fw install writes to ~/.revet/current/ and symlinks to /usr/local/bin/revet
# The symlink requires write access to /usr/local/bin — use sudo if needed.
if [ -w "/usr/local/bin" ]; then
    python3 scripts/revet-fw install
else
    sudo python3 scripts/revet-fw install
fi

# ── done ──────────────────────────────────────────────────────────────────────
ok "revet installed!"
echo ""
echo "  Create a new project:"
echo "    revet new myapp"
echo "    cd myapp"
echo "    revet dev"
echo ""

# Warn if /usr/local/bin isn't in PATH (common on some Linux setups)
if ! echo "$PATH" | grep -q "/usr/local/bin"; then
    warn "/usr/local/bin is not in your PATH."
    warn "Add this to your shell profile (~/.bashrc or ~/.zshrc):"
    echo '    export PATH="/usr/local/bin:$PATH"'
    echo ""
fi

echo "  Docs: https://github.com/joedzado/revet"

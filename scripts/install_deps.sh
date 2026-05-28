#!/bin/bash
# Install revet dependencies. Auto-detects OS.

set -e

OS=$(uname -s)
ARCH=$(uname -m)

info() { echo -e "\033[36m$@\033[0m"; }
ok() { echo -e "\033[32m✔ $@\033[0m"; }
err() { echo -e "\033[31m✗ $@\033[0m" >&2; exit 1; }
warn() { echo -e "\033[33m⚠ $@\033[0m"; }

case "$OS" in
  Darwin)
    info "macOS detected"

    # Check if Homebrew installed
    if ! command -v brew &> /dev/null; then
      err "Homebrew not found. Install from https://brew.sh"
    fi

    info "Installing critical dependencies..."
    brew install cmake python@3.12 conan 2>/dev/null || true

    # Check for Xcode CLI tools
    if ! xcode-select -p &>/dev/null; then
      info "Installing Xcode Command Line Tools..."
      xcode-select --install || warn "Please install Xcode CLI tools manually"
    fi

    if ! command -v clang++ &> /dev/null; then
      err "clang++ not found. Install Xcode CLI tools with: xcode-select --install"
    fi

    info "Installing optional dependencies..."
    brew install ninja sqlite ccache samply lldb clang-tools 2>/dev/null || true

    info "Initializing Conan profile..."
    conan profile detect --force 2>/dev/null || true

    ok "Dependencies installed"
    ;;

  Linux)
    info "Linux detected"

    if command -v apt-get &> /dev/null; then
      info "Debian/Ubuntu detected"
      sudo apt-get update
      sudo apt-get install -y cmake python3 build-essential python3-pip 2>/dev/null || true
      pip3 install conan 2>/dev/null || true
      info "Installing optional dependencies..."
      sudo apt-get install -y ninja-build sqlite3 libsqlite3-dev ccache clang-tools lldb 2>/dev/null || true

    elif command -v dnf &> /dev/null; then
      info "Fedora/RHEL detected"
      sudo dnf install -y cmake python3 gcc-c++ python3-pip 2>/dev/null || true
      pip3 install conan 2>/dev/null || true
      info "Installing optional dependencies..."
      sudo dnf install -y ninja-build sqlite sqlite-devel ccache clang-tools-extra lldb 2>/dev/null || true

    elif command -v pacman &> /dev/null; then
      info "Arch Linux detected"
      sudo pacman -S --noconfirm cmake python base-devel python-pip 2>/dev/null || true
      pip3 install conan 2>/dev/null || true
      info "Installing optional dependencies..."
      sudo pacman -S --noconfirm ninja sqlite ccache clang lldb 2>/dev/null || true

    else
      warn "Unsupported package manager. Install manually:"
      echo "  - cmake 3.20+"
      echo "  - C++ compiler with C++23 support"
      echo "  - python3 + conan (pip3 install conan)"
      echo "  - Optional: ninja, sqlite3, ccache, clang-tidy, lldb"
      exit 1
    fi

    info "Initializing Conan profile..."
    conan profile detect --force 2>/dev/null || true

    ok "Dependencies installed"
    ;;

  *)
    err "Unsupported OS: $OS"
    ;;
esac

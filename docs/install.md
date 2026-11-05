# Installation

## What you need

| Dependency | Why | Min version |
|---|---|---|
| **C++23 compiler** | All framework code is C++23. | clang 18 / gcc 14 / Apple clang from Xcode 16 |
| **CMake** | Configure + build. | 3.20 |
| **Ninja** (optional) | Faster builds. revet auto-picks it if installed. | any |
| **Python 3** | Codegen + CLI. | 3.10 |
| **Conan 2** | C++ package manager (think `npm` / `cargo` for C++). Installs libsodium, libcurl, simdjson, ctre. | 2.0 |
| **libsodium-dev** | AEAD for cookies + CSRF. Linked against the system version. | 1.0.18 |
| **libcurl-dev** | Outbound HTTP client. | 7.x |
| **libsqlite3-dev** | SQLite resource backend. | 3.x |
| **libpq-dev** *(optional; macOS: `brew install libpq`)* | Postgres resource backend. Skip if not using postgres. | 14 |

## macOS

```bash
brew install cmake ninja python3 libsodium curl sqlite postgresql
pip3 install conan
conan profile detect
```

Xcode CLT must be installed (`xcode-select --install`). If you're on an older Xcode that ships a pre-C++23 clang, install LLVM 19 explicitly:

```bash
brew install llvm
export CC=$(brew --prefix llvm)/bin/clang
export CXX=$(brew --prefix llvm)/bin/clang++
```

## Debian / Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y \
    git curl ca-certificates \
    python3 python3-pip \
    cmake ninja-build \
    clang-19 libc++-19-dev libc++abi-19-dev \
    libsodium-dev libcurl4-openssl-dev \
    libsqlite3-dev libpq-dev
pip3 install --break-system-packages conan
conan profile detect

export CC=clang-19
export CXX=clang++-19
```

## Arch / Fedora / others

You're on your own for package names, but the dependencies are the same. The framework does not vendor or build any of them.

## Install the framework

```bash
curl -fsSL https://raw.githubusercontent.com/joedzado/revet/main/install.sh | sh
```

What `install.sh` does:

1. Clones the repo to `~/.revet/src/`.
2. Runs `scripts/install_deps.sh` to install missing dev dependencies for your OS.
3. Runs `scripts/revet-fw install`, which:
   - Copies the framework into `~/.revet/current/`.
   - Symlinks `/usr/local/bin/revet → ~/.revet/current/bin/revet` (needs sudo for the symlink).

Re-running `install.sh` upgrades to the latest `main`.

## Verify

```bash
revet --version
# → 0.1.5

revet new myapp
cd myapp
revet dev
```

If `revet dev` builds and serves at `http://localhost:8080`, you're set.

## Uninstall

```bash
sudo rm /usr/local/bin/revet
rm -rf ~/.revet
```

The framework owns nothing else outside those two paths. Conan's package cache (`~/.conan2/`) and your project build dirs are independent.

## Why is `~/.revet/current/` root-owned?

`install.sh` runs `scripts/revet-fw install` under `sudo` to drop the symlink into `/usr/local/bin/`, and the symlink + the install dir end up owned by root. This is fine for end users (read-only stable install, like Homebrew or Node).

If you're hacking on the framework itself, you'll want to point at your source repo instead — see [contributing.md](./contributing.md).

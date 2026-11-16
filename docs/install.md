# Installation

## What you need

| Dependency | Why | Min version |
|---|---|---|
| **C++23 compiler** | All framework code is C++23. | clang 18 / gcc 14 / Apple clang from Xcode 16 |
| **CMake** | Configure + build. | 3.20 |
| **Ninja** (optional) | Faster builds. revet auto-picks it if installed. | any |
| **Conan 2** | C++ package manager (think `npm` / `cargo` for C++). Installs libcurl, Catch2 and app dependencies; needs Python, which pip or Homebrew brings. libsodium, simdjson, CTRE and FTXUI are vendored. | 2.21 (libcurl recipes need it) |
| **libcurl-dev** | Outbound HTTP client. | 7.x |
| **libsqlite3-dev** | SQLite resource backend. | 3.x |
| **libpq-dev** *(optional; macOS: `brew install libpq`)* | Postgres resource backend. Skip if not using postgres. | 14 |

## macOS

```bash
brew install cmake ninja conan sqlite postgresql
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
    libcurl4-openssl-dev \
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
3. Builds the framework from source: the `revet` CLI, the codegen tool and the vendored libraries.
4. Copies the framework into `~/.revet/current/` and installs the build to `~/.revet/current/prebuilt/`.
5. Symlinks `/usr/local/bin/revet → ~/.revet/current/prebuilt/bin/revet` (needs sudo for the symlink).

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

`install.sh` runs `scripts/install.sh` under `sudo` to drop the symlink into `/usr/local/bin/`, and the symlink + the install dir end up owned by root. This is fine for end users (read-only stable install, like Homebrew or Node).

If you're hacking on the framework itself, you'll want to point at your source repo instead — see [contributing.md](./contributing.md).

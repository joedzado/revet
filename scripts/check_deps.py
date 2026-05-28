#!/usr/bin/env python3
"""Check dependencies for revet.

Detects: cmake, C++23 compiler, python3, conan, ninja, catch2, sqlite3, clang-tidy, ccache, samply, lldb.
Returns: dict of {name: (found: bool, version: str, why_needed: str)}.
"""
import subprocess
import sys
import re
from pathlib import Path

DEPS = {
    # (tool_name, check_cmd, version_pattern, why_needed, critical)
    "cmake": (["cmake", "--version"], r"cmake version ([\d.]+)", "Build system", True),
    "c++": (["clang++", "--version"], r"Apple clang version ([\d.]+)", "Compiler", True),
    "python3": (["python3", "--version"], r"Python ([\d.]+)", "Codegen scripts", True),
    "conan": (["conan", "--version"], r"Conan version ([\d.]+)", "Package manager (C++ deps)", True),
    "libsodium": (["pkg-config", "--modversion", "libsodium"], r"([\d.]+)", "Encrypted cookies (via Conan)", False),
    "ninja": (["ninja", "--version"], r"([\d.]+)", "Fast builds (fallback: make)", False),
    "catch2": (["pkg-config", "--modversion", "catch2"], r"([\d.]+)", "Unit tests (via Conan)", False),
    "sqlite3": (["sqlite3", "--version"], r"([\d.]+)", "Database (opt feature)", False),
    "clang-tidy": (["clang-tidy", "--version"], r"version ([\d.]+)", "Linting (run tidy)", False),
    "ccache": (["ccache", "--version"], r"ccache version ([\d.]+)", "Build cache", False),
    "samply": (["samply", "--version"], r"([\d.]+)", "Profiling (run with --profile)", False),
    "lldb": (["lldb", "--version"], r"lldb-([\d.]+)", "Debugger attach (run with --lldb)", False),
}

def check_tool(name, cmd, pattern, why, critical):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        text = result.stdout + result.stderr
        m = re.search(pattern, text)
        version = m.group(1) if m else "found"
        return {"found": True, "version": version, "why": why, "critical": critical}
    except Exception:
        return {"found": False, "version": None, "why": why, "critical": critical}

def check_cmake_version():
    """Verify cmake >= 3.20."""
    try:
        result = subprocess.run(["cmake", "--version"], capture_output=True, text=True, timeout=5)
        m = re.search(r"cmake version ([\d.]+)", result.stdout)
        if m:
            major, minor = map(int, m.group(1).split(".")[:2])
            if (major, minor) < (3, 20):
                return False
    except Exception:
        pass
    return True

def check_cpp20_support():
    """Verify compiler supports -std=c++23."""
    test_src = """
    #include <utility>
    auto foo(auto x) { return x; }
    int main() { return foo(42); }
    """
    try:
        import tempfile
        with tempfile.NamedTemporaryFile(mode='w', suffix='.cpp', delete=False) as f:
            f.write(test_src)
            temp_file = f.name
        result = subprocess.run(
            ["clang++", "-std=c++23", "-c", temp_file, "-o", "/dev/null"],
            capture_output=True, timeout=5
        )
        Path(temp_file).unlink()
        return result.returncode == 0
    except Exception:
        try:
            Path(temp_file).unlink()
        except Exception:
            pass
        return False

def check_all():
    deps = {}
    for name, (cmd, pattern, why, critical) in DEPS.items():
        deps[name] = check_tool(name, cmd, pattern, why, critical)

    # Special version checks
    if deps["cmake"]["found"] and not check_cmake_version():
        deps["cmake"]["found"] = False
        deps["cmake"]["version"] = "< 3.20"

    if deps["c++"]["found"] and not check_cpp20_support():
        deps["c++"]["found"] = False
        deps["c++"]["version"] = "no C++23"

    return deps

def print_report(deps, mode="verbose"):
    """Print dependency report. mode: verbose, terse, or missing_only."""
    critical_missing = [name for name, info in deps.items() if info["critical"] and not info["found"]]
    optional_missing = [name for name, info in deps.items() if not info["critical"] and not info["found"]]

    if mode == "missing_only":
        if critical_missing:
            print("CRITICAL missing:")
            for name in critical_missing:
                info = deps[name]
                print(f"  {name}: {info['why']}")
        if optional_missing:
            print("\nOPTIONAL missing:")
            for name in optional_missing:
                info = deps[name]
                print(f"  {name}: {info['why']}")
        return bool(critical_missing or optional_missing)

    if mode == "terse":
        missing = [name for name, info in deps.items() if not info["found"]]
        if missing:
            print(f"Missing: {', '.join(missing)}")
            return True
        print("✓ All dependencies OK")
        return False

    # verbose
    print("\n─── Dependency Check ───")
    for name in sorted(deps.keys()):
        info = deps[name]
        status = "✓" if info["found"] else "✗"
        version = f" v{info['version']}" if info["version"] else ""
        crit = " [CRITICAL]" if info["critical"] and not info["found"] else ""
        print(f"{status} {name:12} {info['why']:30} {version}{crit}")

    if critical_missing:
        print(f"\n⚠ {len(critical_missing)} critical missing:")
        for name in critical_missing:
            print(f"  - {name}")
        return True

    if optional_missing:
        print(f"\nℹ {len(optional_missing)} optional missing (dev/profiling only):")
        for name in optional_missing:
            print(f"  - {name}")

    return False

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "verbose"
    deps = check_all()
    has_missing = print_report(deps, mode)

    # Only fail on critical missing (not optional)
    critical_missing = any(info["critical"] and not info["found"] for info in deps.values())
    sys.exit(1 if critical_missing else 0)

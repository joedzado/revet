// revet — user-facing CLI for revet projects.
//
//   revet dev [--port N] [--asan] [--lldb] [--profile] [--debug] [--logging LEVEL]
//   revet build [--debug] [--target NAME|PROFILE]
//   revet new <name> [--empty|--blank|--clean]
//   revet packages | add <pkg>[@version] | remove <pkg> | lock
//   revet upgrade [--force] | tidy | format | clean | doctor
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <set>
#include <unistd.h>

#include "cli.hpp"

namespace revet::cli {
namespace {

struct Args {
    std::string command = "dev";
    bool command_given = false;
    std::optional<std::string> extra;
    std::optional<int> port;
    std::optional<std::string> logging;
    std::optional<std::string> target;
    bool lldb = false, profile = false, asan = false, debug = false;
    bool version = false, help = false, force = false, empty = false;
};

[[noreturn]] void usage_error(const std::string& msg) {
    err(msg);
    info("Run `revet --help` for usage.");
    std::exit(1);
}

Args parse(int argc, char** argv) {
    Args a;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::optional<std::string> inline_value;
        if (arg.size() > 2 && arg.starts_with("--")) {
            if (size_t eq = arg.find('='); eq != std::string::npos) {
                inline_value = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
            }
        }
        auto value = [&] -> std::string {
            if (inline_value) {
                return *inline_value;
            }
            if (i + 1 >= argc) {
                usage_error("argument " + arg + ": expected one argument");
            }
            return argv[++i];
        };
        auto flag = [&](bool& target) {
            if (inline_value) {
                usage_error("argument " + arg + ": ignored explicit argument '" + *inline_value + "'");
            }
            target = true;
        };

        if (arg == "--port") {
            std::string v = value();
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end != '\0' || n <= 0 || n > 65535) {
                usage_error("argument --port: invalid int value: '" + v + "'");
            }
            a.port = static_cast<int>(n);
        } else if (arg == "--logging") {
            std::string v = value();
            static const std::set<std::string> levels = {"debug", "info", "warn", "error"};
            if (!levels.contains(v)) {
                usage_error("argument --logging: invalid choice: '" + v + "' (choose from debug, info, warn, error)");
            }
            a.logging = v;
        } else if (arg == "--target") {
            a.target = value();
        } else if (arg == "--lldb") {
            flag(a.lldb);
        } else if (arg == "--profile") {
            flag(a.profile);
        } else if (arg == "--asan") {
            flag(a.asan);
        } else if (arg == "--debug") {
            flag(a.debug);
        } else if (arg == "--version") {
            flag(a.version);
        } else if (arg == "--help" || arg == "-h") {
            flag(a.help);
        } else if (arg == "--force") {
            flag(a.force);
        } else if (arg == "--empty" || arg == "--blank" || arg == "--clean") {
            flag(a.empty);
        } else if ((arg.starts_with('-') && arg != "-") || positional > 1) {
            usage_error("unrecognized arguments: " + arg);
        } else if (positional == 0) {
            a.command = arg;
            a.command_given = true;
            ++positional;
        } else {
            a.extra = arg;
            ++positional;
        }
    }
    return a;
}

void print_help(const std::string& command) {
    info("revet " + framework_version());
    info("");
    if (command == "new") {
        info("Usage: revet new <name> [--empty]");
        info("");
        info("Arguments:");
        info("  <name>         — project directory name");
        info("");
        info("Options:");
        info("  --empty        — bare project (minimal index route, no starter site)");
        info("  --blank        — alias for --empty");
        info("  --clean        — alias for --empty");
    } else if (command == "dev") {
        info("Usage: revet dev [options]");
        info("");
        info("Options:");
        info("  --port N       — listen on port N (default: 8080 or revet.toml)");
        info("  --logging LEVEL  — set log level: debug, info, warn, error");
        info("  --asan         — enable AddressSanitizer + UBSan");
        info("  --lldb         — launch under lldb");
        info("  --profile      — record with samply (flame graph)");
        info("  --debug        — build with debug symbols");
    } else {
        info("Usage: revet [command] [options]");
        info("");
        info("Commands:");
        info("  dev            — debug build with file watcher  [--port N] [--logging LEVEL] [--asan] [--lldb] "
             "[--profile] [--debug]");
        info("  build          — release build for deployment  [--debug] [--target linux-arm64]");
        info("  packages       — interactive package manager TUI");
        info("  add <pkg>      — add a Conan package dependency  [pkg@version]");
        info("  remove <pkg>   — remove a package dependency");
        info("  lock           — regenerate revet.lock from current deps");
        info("  new <name>     — scaffold a new project  [--empty|--blank|--clean]");
        info("  upgrade        — sync with installed framework version  [--force]");
        info("  tidy           — run clang-tidy --fix on project source");
        info("  format         — run clang-format -i on project source");
        info("  clean          — remove build artifacts");
        info("  doctor         — check build tools and optional extras");
    }
}

int main_impl(int argc, char** argv) {
#ifdef __APPLE__
    // Apple's clang for everything revet runs (CMake, Conan builds) unless CC/CXX say otherwise:
    // `cc` on PATH can be a Homebrew GCC or LLVM that lacks the macOS SDK setup.
    if (std::getenv("CC") == nullptr && access("/usr/bin/clang", X_OK) == 0) {
        setenv("CC", "/usr/bin/clang", 1);
    }
    if (std::getenv("CXX") == nullptr && access("/usr/bin/clang++", X_OK) == 0) {
        setenv("CXX", "/usr/bin/clang++", 1);
    }
#endif
    Args a = parse(argc, argv);
    if (a.version) {
        info(framework_version());
        return 0;
    }
    if (a.help) {
        print_help(a.command_given ? a.command : "");
        return 0;
    }

    std::error_code ec;
    (void)fs::current_path(ec);
    if (ec) {
        fail("Current directory no longer exists. cd into your project directory and retry.");
    }

    static const std::set<std::string> known = {"dev", "build",   "packages", "pkg",    "add",   "remove", "lock",
                                                "new", "upgrade", "tidy",     "format", "clean", "help",   "doctor"};
    const std::string& cmd = a.command;
    if (!known.contains(cmd)) {
        err("unknown command: " + cmd);
        info("");
        print_help("");
        return 1;
    }
    if (cmd != "new" && cmd != "upgrade" && cmd != "help" && cmd != "doctor" && !fs::exists("revet.toml")) {
        fail("no revet.toml found — not a revet project");
    }

    if (cmd == "add") {
        if (!a.extra) {
            fail("usage: revet add <package>[@version]");
        }
        return cmd_add(*a.extra);
    }
    if (cmd == "remove") {
        if (!a.extra) {
            fail("usage: revet remove <package>");
        }
        return cmd_remove(*a.extra);
    }
    if (cmd == "lock") {
        return cmd_lock();
    }
    if (cmd == "packages" || cmd == "pkg") {
        return cmd_packages();
    }
    if (cmd == "dev") {
        DevOptions o;
        o.port = a.port ? *a.port : toml_port(fs::current_path()).value_or(8080);
        o.lldb = a.lldb;
        o.profile = a.profile;
        o.asan = a.asan;
        o.debug = a.debug;
        o.logging = a.logging;
        return cmd_dev(o);
    }
    if (cmd == "build") {
        Result r = cmake_build(true, false, a.debug, a.target);
        (void)std::fwrite(r.out.data(), 1, r.out.size(), stdout);
        if (r.rc != 0) {
            (void)std::fflush(stdout);
            (void)std::fwrite(r.err.data(), 1, r.err.size(), stderr);
            return 1;
        }
        return 0;
    }
    if (cmd == "new") {
        if (!a.extra) {
            fail("usage: revet new <name> [--empty|--blank|--clean]");
        }
        return cmd_new(*a.extra, a.empty);
    }
    if (cmd == "upgrade") {
        return cmd_upgrade(a.force);
    }
    if (cmd == "tidy") {
        return cmd_tidy();
    }
    if (cmd == "format") {
        return cmd_format();
    }
    if (cmd == "clean") {
        return cmd_clean();
    }
    if (cmd == "doctor") {
        return cmd_doctor();
    }
    print_help("");
    return 0;
}

} // namespace
} // namespace revet::cli

int main(int argc, char** argv) {
    try {
        return revet::cli::main_impl(argc, argv);
    } catch (const std::exception& e) {
        revet::cli::err(e.what());
    } catch (...) {
        revet::cli::err("unexpected error");
    }
    return 1;
}

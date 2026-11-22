// `revet dev`: Debug build, run the server, rebuild on file changes, single-key commands.
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <poll.h>
#include <set>
#include <sys/stat.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include "cli.hpp"

namespace revet::cli {
namespace {

// ── Terminal ────────────────────────────────────────────────────────────────

termios g_saved_tty{};
bool g_have_tty = false;

void restore_tty() {
    if (g_have_tty) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &g_saved_tty);
    }
}

void raw_tty() {
    if (!g_have_tty) {
        return;
    }
    termios raw = g_saved_tty;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSADRAIN, &raw);
}

// ── Server process ──────────────────────────────────────────────────────────

std::atomic<pid_t> g_server{-1};
std::atomic<bool> g_server_own_group{false}; // false for lldb, which shares our terminal and group

void stop_server(bool wait) {
    pid_t pid = g_server.exchange(-1);
    if (pid <= 0) {
        return;
    }
    if (g_server_own_group) {
        killpg(pid, SIGTERM);
    } else {
        kill(pid, SIGTERM);
    }
    if (wait) {
        wait_child(pid);
    }
}

auto server_running() -> bool {
    pid_t pid = g_server;
    if (pid <= 0) {
        return false;
    }
    if (waitpid(pid, nullptr, WNOHANG) == 0) {
        return true;
    }
    // Exited and reaped (or not ours): forget the pid so nothing signals it after the OS reuses it.
    g_server.compare_exchange_strong(pid, -1);
    return false;
}

// Ctrl-C while the terminal is cooked (during a build) or a kill: take the server down too.
extern "C" void on_signal(int sig) {
    pid_t pid = g_server;
    if (pid > 0) {
        if (g_server_own_group) {
            killpg(pid, SIGTERM);
        } else {
            kill(pid, SIGTERM);
        }
    }
    if (g_have_tty) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &g_saved_tty);
    }
    _exit(128 + sig);
}

void write_pid(pid_t pid) {
    write_file(fs::path(k_build_dir) / ".pid", std::to_string(pid));
}

// ── File watcher ────────────────────────────────────────────────────────────

class Watcher {
public:
    Watcher() {
        scan(true);
    }

    // Polls every 250 ms; collects changed or new files until take() is called.
    void start() {
        std::thread([this] -> void {
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                scan(false);
            }
        }).detach();
    }

    auto take() -> std::vector<std::string> {
        std::scoped_lock lock(mu_);
        std::vector<std::string> out(pending_.begin(), pending_.end());
        pending_.clear();
        return out;
    }

private:
    static auto skipped_dir(const std::string& name) -> bool {
        return name == "build" || name == "profiles" || name == "__pycache__" ||
               name[0] == '.'; // includes .git, .cache; profiles/ is samply output
    }
    static auto skipped_file(const fs::path& p) -> bool {
        std::string name = p.filename().string();
        if (name.empty() || name[0] == '.') {
            return true;
        }
        std::string ext = p.extension().string();
        return ext == ".db" || ext == ".db-shm" || ext == ".db-wal";
    }

    void scan(bool initial) {
        std::error_code ec;
        std::vector<std::string> changed;
        for (auto it = fs::recursive_directory_iterator(".", fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            const fs::path& p = it->path();
            if (it->is_directory(ec)) {
                if (skipped_dir(p.filename().string())) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec) || skipped_file(p)) {
                continue;
            }
            auto t = fs::last_write_time(p, ec);
            if (ec) {
                continue;
            }
            std::string key = p.lexically_relative(".").string();
            auto found = mtimes_.find(key);
            if (initial) {
                mtimes_[key] = t;
            } else if (found == mtimes_.end() || found->second != t) {
                mtimes_[key] = t;
                changed.push_back(key);
            }
        }
        if (!changed.empty()) {
            std::scoped_lock lock(mu_);
            pending_.insert(changed.begin(), changed.end());
        }
    }

    std::map<std::string, fs::file_time_type> mtimes_; // touched only by the scanning thread
    std::mutex mu_;
    std::set<std::string> pending_;
};

// ── Rebuild and run ─────────────────────────────────────────────────────────

void header() {
    rule("revet");
}

void print_flags(const DevOptions& o) {
    std::string flags;
    for (auto [name, on] : {std::pair{"asan", o.asan}, {"ubsan", o.asan}, {"lldb", o.lldb}, {"profile", o.profile}}) {
        if (on) {
            flags += (flags.empty() ? "" : " ") + std::string(name);
        }
    }
    if (!flags.empty()) {
        dim("  flags: " + flags);
    }
}

void print_keys() {
    dim("  r=rebuild  t=routes  d=debug  q=quit");
}

// Copies the server's stderr to build/.server.log and to the terminal.
void tee_server_log(int fd, const fs::path& log_path) {
    // Only allocation failure can throw in here.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    std::thread([fd, log_path] -> void {
        std::ofstream log(log_path, std::ios::binary | std::ios::trunc);
        std::string pending;
        std::array<char, 4096> buf{};
        auto emit = [&](std::string_view line) -> void {
            log << line << std::flush;
            std::string out = "\r\033[K" + std::string(line);
            (void)std::fflush(stdout);
            (void)!write(STDERR_FILENO, out.data(), out.size());
        };
        ssize_t got = 0;
        while ((got = read(fd, buf.data(), buf.size())) > 0 || (got < 0 && errno == EINTR)) {
            pending.append(buf.data(), static_cast<size_t>(std::max<ssize_t>(got, 0)));
            for (size_t nl = pending.find('\n'); nl != std::string::npos; nl = pending.find('\n')) {
                emit(std::string_view(pending).substr(0, nl + 1));
                pending.erase(0, nl + 1);
            }
        }
        if (!pending.empty()) {
            emit(pending);
        }
        close(fd);
    }).detach();
}

auto chomp(std::string s) -> std::string {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

void rebuild(const DevOptions& o, const std::string& reason) {
    stop_server(true);
    clear_screen();
    header();
    print_flags(o);
    if (!reason.empty()) {
        dim(reason);
    }
    dim("building…");
    Result r = cmake_build(false, o.asan, o.debug, std::nullopt);
    for (const auto& line : split_lines_keepends(r.out)) {
        info(expand_tabs(chomp(line)));
    }
    if (r.rc != 0) {
        for (const auto& line : split_lines_keepends(r.err)) {
            err(expand_tabs(chomp(line)));
        }
        err("✖ build failed");
        return;
    }

    std::string port = std::to_string(o.port);
    std::string url = "http://localhost:" + port;
    std::string bin = binary_path().string();
    if (o.lldb) {
        Child c = spawn({"lldb", "--", bin, "--port", port});
        g_server_own_group = false;
        g_server = c.pid;
        ok("● lldb attached  " + url);
    } else if (o.profile) {
        std::error_code ec;
        fs::create_directories("profiles", ec);
        std::string out = "profiles/profile-" + std::to_string(std::time(nullptr)) + ".json.gz";
        // samply opens the profile with `open`; this one forces Chrome instead of the default browser.
        std::string dir = (fs::temp_directory_path() / "revet-samply-XXXXXX").string();
        if (mkdtemp(dir.data()) != nullptr) {
            write_file(fs::path(dir) / "open", "#!/bin/sh\nexec /usr/bin/open -a \"Google Chrome\" \"$@\"\n");
            fs::permissions(fs::path(dir) / "open",
                            fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read, ec);
        }
        SpawnOptions so;
        so.new_session = true;
        const char* path = std::getenv("PATH");
        so.env.push_back("PATH=" + dir + ":" + ((path != nullptr) ? path : ""));
        std::string samply = which("samply").value_or("samply").string();
        Child c = spawn({samply, "record", "-o", out, bin, "--port", port}, so);
        g_server_own_group = true;
        g_server = c.pid;
        if (c.pid > 0) {
            write_pid(c.pid);
        }
        ok("● profiling  " + url + "  → " + out);
    } else {
        SpawnOptions so;
        so.new_session = true;
        so.out = Io::Null;
        bool logging = std::getenv("LOG_LEVEL") != nullptr;
        so.err = logging ? Io::Pipe : Io::Null;
        Child c = spawn({bin, "--port", port}, so);
        g_server_own_group = true;
        g_server = c.pid;
        if (c.pid <= 0) {
            err("cannot start " + bin);
            return;
        }
        if (logging) {
            fs::path log_path = fs::path(k_build_dir) / ".server.log";
            tee_server_log(c.err_fd, log_path);
            dim("  logs → " + log_path.string());
        }
        write_pid(c.pid);
        ok(std::string(o.asan ? "● running [asan]" : "● running") + "  " + url);
    }
    print_keys();
}

auto changed_reason(const std::vector<std::string>& files) -> std::string {
    std::string names;
    for (const auto& f : files) {
        names += (names.empty() ? "" : ", ") + fs::path(f).filename().string();
    }
    return "↺ " + names;
}

} // namespace

auto cmd_dev(const DevOptions& o) -> int {
    if (o.logging) {
        setenv("LOG_LEVEL", o.logging->c_str(), 1);
    }

    auto missing = missing_critical_tools();
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) {
            list += (list.empty() ? "" : ", ") + m;
        }
        err("✗ Missing dependencies: " + list);
        info("Installing via: bash " + install_deps_script().string());
        if (run_inherit({"bash", install_deps_script().string()}) != 0) {
            fail("Installation failed. Install manually and retry.");
        }
        ok("✓ Dependencies installed. Retrying...");
        missing = missing_critical_tools();
        if (!missing.empty()) {
            list.clear();
            for (const auto& m : missing) {
                list += (list.empty() ? "" : ", ") + m;
            }
            fail("Still missing: " + list);
        }
    }

    if (isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &g_saved_tty) == 0) {
        g_have_tty = true;
    }
    (void)std::atexit(restore_tty);
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    Watcher watcher;
    watcher.start();
    rebuild(o, "");
    raw_tty();

    bool stdin_open = true;
    while (true) {
        if (auto changed = watcher.take(); !changed.empty()) {
            restore_tty();
            rebuild(o, changed_reason(changed));
            raw_tty();
        }

        if (!stdin_open) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        pollfd pfd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
        if (poll(&pfd, 1, 100) <= 0) {
            continue;
        }
        char key = 0;
        if (read(STDIN_FILENO, &key, 1) != 1) {
            stdin_open = false; // EOF: not interactive; keep watching
            continue;
        }
        restore_tty();
        if (key == 'r') {
            rebuild(o, "");
        } else if (key == 'd') {
            pid_t pid = g_server;
            if (server_running()) {
                run_inherit({"lldb", "-p", std::to_string(pid)});
                clear_screen();
                header();
                print_flags(o);
                if (server_running()) {
                    ok("● running  http://localhost:" + std::to_string(o.port));
                } else {
                    err("✖ server exited during debug session");
                }
                print_keys();
            } else {
                err("no running server to attach to");
            }
        } else if (key == 't') {
            clear_screen();
            header();
            print_routes();
            if (server_running()) {
                ok(std::string(o.profile ? "● profiling" : "● running") +
                   "  http://localhost:" + std::to_string(o.port));
            }
            print_keys();
        } else if (key == 'q' || key == '\x03') {
            if (o.profile && g_server > 0) {
                ok("● writing profile…");
            }
            stop_server(o.profile);
            return 0;
        }
        raw_tty();
    }
}

} // namespace revet::cli

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
#include <iterator>
#include <map>
#include <mutex>
#include <poll.h>
#include <ranges>
#include <revet/background.hpp>
#include <set>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include "cli.hpp"

namespace revet::cli {
namespace {

// ── Terminal ────────────────────────────────────────────────────────────────

// The terminal settings to restore on exit and from signal handlers.
struct Tty {
    termios saved{};
    bool have = false;
};
auto tty() -> Tty& {
    static Tty state;
    return state;
}

void restore_tty() {
    if (tty().have) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &tty().saved);
    }
}

void raw_tty() {
    if (!tty().have) {
        return;
    }
    termios raw = tty().saved;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSADRAIN, &raw);
}

// ── Server process ──────────────────────────────────────────────────────────

// The running app server; signal handlers stop it, so the first call happens before they are installed.
struct Server {
    std::atomic<pid_t> pid{-1};
    std::atomic<bool> own_group{false}; // false for lldb, which shares our terminal and group
};
auto server() -> Server& {
    static Server state;
    return state;
}

void stop_server(bool wait) {
    const pid_t pid = server().pid.exchange(-1);
    if (pid <= 0) {
        return;
    }
    if (server().own_group) {
        killpg(pid, SIGTERM);
    } else {
        kill(pid, SIGTERM);
    }
    if (wait) {
        wait_child(pid);
    }
}

auto server_running() -> bool {
    pid_t pid = server().pid;
    if (pid <= 0) {
        return false;
    }
    if (waitpid(pid, nullptr, WNOHANG) == 0) {
        return true;
    }
    // Exited and reaped (or not ours): forget the pid so nothing signals it after the OS reuses it.
    server().pid.compare_exchange_strong(pid, -1);
    return false;
}

// Ctrl-C while the terminal is cooked (during a build) or a kill: take the server down too.
extern "C" void on_signal(int sig) {
    const pid_t pid = server().pid;
    if (pid > 0) {
        if (server().own_group) {
            killpg(pid, SIGTERM);
        } else {
            kill(pid, SIGTERM);
        }
    }
    if (tty().have) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &tty().saved);
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
        thread_ = std::jthread([this](const std::stop_token& stop) -> void {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                scan(false);
            }
        });
    }

    auto take() -> std::vector<std::string> {
        const std::scoped_lock lock(mu_);
        std::vector<std::string> out(pending_.begin(), pending_.end());
        pending_.clear();
        return out;
    }

private:
    // Dot entries (.git, .cache), build output, samply profiles and SQLite files.
    static auto skipped(const fs::directory_entry& e) -> bool {
        std::error_code ec;
        const std::string name = e.path().filename().string();
        if (is_hidden(e)) {
            return true;
        }
        if (e.is_directory(ec)) {
            return name == "build" || name == "profiles" || name == "__pycache__";
        }
        const std::string ext = e.path().extension().string();
        return ext == ".db" || ext == ".db-shm" || ext == ".db-wal";
    }

    void scan(bool initial) {
        std::error_code ec;
        std::vector<std::string> changed;
        for (const auto& p : files_under(".", skipped)) {
            const auto t = fs::last_write_time(p, ec);
            if (ec) {
                continue;
            }
            const std::string key = p.lexically_relative(".").string();
            const auto found = mtimes_.find(key);
            if (initial) {
                mtimes_[key] = t;
            } else if (found == mtimes_.end() || found->second != t) {
                mtimes_[key] = t;
                changed.push_back(key);
            }
        }
        if (!changed.empty()) {
            const std::scoped_lock lock(mu_);
            pending_.insert(changed.begin(), changed.end());
        }
    }

    std::map<std::string, fs::file_time_type> mtimes_; // touched only by the scanning thread
    std::mutex mu_;
    std::set<std::string> pending_;
    std::jthread thread_; // last, so it stops and joins before the members it scans into go away
};

// ── Rebuild and run ─────────────────────────────────────────────────────────

void header() {
    rule("revet");
}

void print_flags(const DevOptions& o) {
    const auto all = std::to_array<std::pair<std::string, bool>>(
        {{"asan", o.asan}, {"ubsan", o.asan}, {"lldb", o.lldb}, {"profile", o.profile}});
    const std::string flags = join(all | std::views::filter([](const auto& f) -> bool { return f.second; }) |
                                       std::views::keys | std::ranges::to<std::vector<std::string>>(),
                                   " ");
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
    revet::background::spawn([fd, log_path](const std::stop_token&) -> void {
        std::ofstream log(log_path, std::ios::binary | std::ios::trunc);
        std::string pending;
        std::array<char, 4096> buf{};
        const auto emit = [&](std::string_view line) -> void {
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
    });
}

auto chomp(std::string s) -> std::string {
    s.erase(s.find_last_not_of("\r\n") + 1); // npos + 1 == 0: all newlines
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
    const Result r = cmake_build(false, o.asan, o.debug, std::nullopt);
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

    const std::string port = std::to_string(o.port);
    const std::string url = "http://localhost:" + port;
    const std::string bin = binary_path().string();
    if (o.lldb) {
        const Child c = spawn({"lldb", "--", bin, "--port", port});
        server().own_group = false;
        server().pid = c.pid;
        ok("● lldb attached  " + url);
    } else if (o.profile) {
        std::error_code ec;
        fs::create_directories("profiles", ec);
        const std::string out = "profiles/profile-" + std::to_string(std::time(nullptr)) + ".json.gz";
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
        const std::string samply = which("samply").value_or("samply").string();
        const Child c = spawn({samply, "record", "-o", out, bin, "--port", port}, so);
        server().own_group = true;
        server().pid = c.pid;
        if (c.pid > 0) {
            write_pid(c.pid);
        }
        ok("● profiling  " + url + "  → " + out);
    } else {
        SpawnOptions so;
        so.new_session = true;
        so.out = Io::Null;
        const bool logging = std::getenv("LOG_LEVEL") != nullptr;
        so.err = logging ? Io::Pipe : Io::Null;
        const Child c = spawn({bin, "--port", port}, so);
        server().own_group = true;
        server().pid = c.pid;
        if (c.pid <= 0) {
            err("cannot start " + bin);
            return;
        }
        if (logging) {
            const fs::path log_path = fs::path(k_build_dir) / ".server.log";
            tee_server_log(c.err_fd, log_path);
            dim("  logs → " + log_path.string());
        }
        write_pid(c.pid);
        ok(std::string(o.asan ? "● running [asan]" : "● running") + "  " + url);
    }
    print_keys();
}

auto changed_reason(const std::vector<std::string>& files) -> std::string {
    return "↺ " + join(files | std::views::transform([](const std::string& f) -> std::string {
                           return fs::path(f).filename().string();
                       }) | std::ranges::to<std::vector<std::string>>(),
                       ", ");
}

} // namespace

auto cmd_dev(const DevOptions& o) -> int {
    if (o.logging) {
        setenv("LOG_LEVEL", o.logging->c_str(), 1);
    }

    auto missing = missing_critical_tools();
    if (!missing.empty()) {
        err("✗ Missing dependencies: " + join(missing, ", "));
        info("Installing via: bash " + install_deps_script().string());
        if (run_inherit({"bash", install_deps_script().string()}) != 0) {
            fail("Installation failed. Install manually and retry.");
        }
        ok("✓ Dependencies installed. Retrying...");
        missing = missing_critical_tools();
        if (!missing.empty()) {
            fail("Still missing: " + join(missing, ", "));
        }
    }

    if (isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &tty().saved) == 0) {
        tty().have = true;
    }
    (void)std::atexit(restore_tty);
    // Construct the state the handler touches before it can run.
    (void)tty();
    (void)server();
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
        if (const auto changed = watcher.take(); !changed.empty()) {
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
            const pid_t pid = server().pid;
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
            if (o.profile && server().pid > 0) {
                ok("● writing profile…");
            }
            stop_server(o.profile);
            return 0;
        }
        raw_tty();
    }
}

} // namespace revet::cli

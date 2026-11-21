// Output, processes, files, hashes and framework location.
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "cli.hpp"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace revet::cli {

// ── Output ──────────────────────────────────────────────────────────────────

namespace {
void line_out(std::string_view a, std::string_view b = {}, std::string_view c = {}) {
    std::string s;
    s.reserve(a.size() + b.size() + c.size() + 1);
    s.append(a).append(b).append(c).push_back('\n');
    (void)std::fwrite(s.data(), 1, s.size(), stdout);
}
} // namespace

void rule(std::string_view s) {
    line_out(k_bold_cyan, "── " + std::string(s) + " ──", k_reset);
}
void info(std::string_view s) {
    line_out(s);
}
void dim(std::string_view s) {
    line_out(k_dim, s, k_reset);
}
void ok(std::string_view s) {
    line_out(k_green, "✔ " + std::string(s), k_reset);
}

void err(std::string_view s) {
    (void)std::fflush(stdout);
    std::string line = std::string(k_red) + "ERROR: " + std::string(s) + std::string(k_reset) + "\n";
    (void)std::fwrite(line.data(), 1, line.size(), stderr);
}

void fail(std::string_view s) {
    err(s);
    std::exit(1);
}

std::string expand_tabs(std::string_view s, int width) {
    std::string out;
    int col = 0;
    for (char c : s) {
        if (c == '\t') {
            int n = width - (col % width);
            out.append(static_cast<size_t>(n), ' ');
            col += n;
        } else {
            out.push_back(c);
            col = (c == '\n' || c == '\r') ? 0 : col + 1;
        }
    }
    return out;
}

void clear_screen() {
    (void)std::fputs("\033[H\033[2J\033[3J", stdout);
    (void)std::fflush(stdout);
}

// ── Processes ───────────────────────────────────────────────────────────────

namespace {

// The current environment with `overrides` ("KEY=VALUE") added or replacing entries.
std::vector<std::string> merged_env(const std::vector<std::string>& overrides) {
    std::vector<std::string> env;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
        std::string_view entry(*e);
        std::string_view key = entry.substr(0, entry.find('='));
        bool replaced = false;
        for (const auto& o : overrides) {
            if (std::string_view(o).substr(0, o.find('=')) == key) {
                replaced = true;
            }
        }
        if (!replaced) {
            env.emplace_back(entry);
        }
    }
    for (const auto& o : overrides) {
        env.push_back(o);
    }
    return env;
}

std::vector<char*> c_array(std::vector<std::string>& v) {
    std::vector<char*> out;
    out.reserve(v.size() + 1);
    for (auto& s : v) {
        out.push_back(s.data());
    }
    out.push_back(nullptr);
    return out;
}

} // namespace

Child spawn(const std::vector<std::string>& argv, const SpawnOptions& opts) {
    Child child;
    if (argv.empty()) {
        return child;
    }

    std::array<int, 2> out_pipe = {-1, -1};
    std::array<int, 2> err_pipe = {-1, -1};
    if (opts.out == Io::Pipe && pipe(out_pipe.data()) != 0) {
        return child;
    }
    if (opts.err == Io::Pipe && pipe(err_pipe.data()) != 0) {
        if (out_pipe[0] >= 0) {
            close(out_pipe[0]), close(out_pipe[1]);
        }
        return child;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    auto route = [&](Io io, int fd, const std::array<int, 2>& pipe_fds) {
        if (io == Io::Null) {
            posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", fd == 0 ? O_RDONLY : O_WRONLY, 0);
        } else if (io == Io::Pipe) {
            posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], fd);
            posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
            posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
        }
    };
    route(opts.in, 0, {-1, -1}); // stdin is never piped
    route(opts.out, 1, out_pipe);
    route(opts.err, 2, err_pipe);
    std::string cwd = opts.cwd.string();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" // macOS 26 renamed it; Linux has only the _np name
    if (!cwd.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    }
#pragma clang diagnostic pop

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // POSIX declares the spawn flags as int constants and the attribute as short.
    // NOLINTBEGIN(bugprone-signed-bitwise,hicpp-signed-bitwise)
    short flags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
    if (opts.new_session) {
        flags |= POSIX_SPAWN_SETSID;
    }
    // NOLINTEND(bugprone-signed-bitwise,hicpp-signed-bitwise)
    posix_spawnattr_setflags(&attr, flags);
    sigset_t none;
    sigset_t defaults;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &defaults);

    std::vector<std::string> args = argv;
    std::vector<std::string> env = merged_env(opts.env);
    auto c_args = c_array(args);
    auto c_env = c_array(env);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, c_args[0], &actions, &attr, c_args.data(), c_env.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    if (out_pipe[1] >= 0) {
        close(out_pipe[1]);
    }
    if (err_pipe[1] >= 0) {
        close(err_pipe[1]);
    }
    if (rc != 0) {
        if (out_pipe[0] >= 0) {
            close(out_pipe[0]);
        }
        if (err_pipe[0] >= 0) {
            close(err_pipe[0]);
        }
        return child;
    }
    child.pid = pid;
    child.out_fd = out_pipe[0];
    child.err_fd = err_pipe[0];
    return child;
}

int wait_child(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

Result run(const std::vector<std::string>& argv, const fs::path& cwd, int timeout_s) {
    Result r;
    SpawnOptions opts;
    opts.cwd = cwd;
    opts.in = Io::Null;
    opts.out = Io::Pipe;
    opts.err = Io::Pipe;
    Child c = spawn(argv, opts);
    if (c.pid < 0) {
        r.started = false;
        r.rc = -1;
        r.err = argv.empty() ? "" : argv[0] + ": cannot run";
        return r;
    }

    const auto deadline = std::time(nullptr) + timeout_s;
    std::array<pollfd, 2> fds = {{{c.out_fd, POLLIN, 0}, {c.err_fd, POLLIN, 0}}};
    std::array<std::string*, 2> sinks = {&r.out, &r.err};
    int open_fds = 2;
    bool timed_out = false;
    std::vector<char> buf(65536);
    while (open_fds > 0) {
        int wait_ms = -1;
        if (timeout_s > 0) {
            auto left = deadline - std::time(nullptr);
            if (left <= 0) {
                timed_out = true;
                break;
            }
            wait_ms = static_cast<int>(left * 1000);
        }
        int n = poll(fds.data(), fds.size(), wait_ms);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (size_t i = 0; i < fds.size(); ++i) {
            pollfd& f = fds.at(i);
            // NOLINTNEXTLINE(bugprone-signed-bitwise,hicpp-signed-bitwise): POSIX poll flags are int
            if (f.fd < 0 || (f.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            ssize_t got = read(f.fd, buf.data(), buf.size());
            if (got > 0) {
                sinks.at(i)->append(buf.data(), static_cast<size_t>(got));
            } else if (got == 0 || errno != EINTR) {
                close(f.fd);
                f.fd = -1;
                --open_fds;
            }
        }
    }
    if (timed_out) {
        kill(c.pid, SIGKILL);
    }
    for (auto& f : fds) {
        if (f.fd >= 0) {
            close(f.fd);
        }
    }
    r.rc = wait_child(c.pid);
    if (timed_out) {
        r.rc = -1;
    }
    return r;
}

int run_inherit(const std::vector<std::string>& argv, const fs::path& cwd) {
    SpawnOptions opts;
    opts.cwd = cwd;
    Child c = spawn(argv, opts);
    if (c.pid < 0) {
        return -1;
    }
    return wait_child(c.pid);
}

std::optional<fs::path> which(std::string_view name) {
    if (name.contains('/')) {
        if (access(std::string(name).c_str(), X_OK) == 0) {
            return fs::path(name);
        }
        return std::nullopt;
    }
    const char* path = std::getenv("PATH");
    if (path == nullptr) {
        return std::nullopt;
    }
    std::string_view rest(path);
    while (true) {
        size_t colon = rest.find(':');
        std::string_view dir = rest.substr(0, colon);
        fs::path candidate = fs::path(dir.empty() ? "." : std::string(dir)) / std::string(name);
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec) && access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
        if (colon == std::string_view::npos) {
            break;
        }
        rest = rest.substr(colon + 1);
    }
    return std::nullopt;
}

unsigned cpu_count() {
    unsigned n = std::thread::hardware_concurrency();
    return (n != 0U) ? n : 1;
}

// ── Files ───────────────────────────────────────────────────────────────────

std::optional<std::string> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const fs::path& p, std::string_view content) {
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail("cannot write " + p.string());
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::vector<std::string> split_lines_keepends(std::string_view s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, nl - start + 1));
        start = nl + 1;
    }
    return out;
}

std::string strip(std::string_view s) {
    size_t b = 0;
    size_t e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    while (b < e && ws(s[b])) {
        ++b;
    }
    while (e > b && ws(s[e - 1])) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

// ── Hashes ──────────────────────────────────────────────────────────────────

// Fixed-size digest blocks: every index is bounded by its loop.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-signed-bitwise,hicpp-signed-bitwise)

namespace {

std::string to_hex(const unsigned char* bytes, size_t n) {
    static constexpr char k_digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(k_digits[bytes[i] >> 4U]);
        out.push_back(k_digits[bytes[i] & 0xfU]);
    }
    return out;
}

constexpr std::uint32_t rotl(std::uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}

// Message padded to a multiple of 64 bytes with the bit length appended (little- or big-endian).
std::string padded(std::string_view data, bool big_endian) {
    std::string msg(data);
    std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back('\0');
    }
    for (int i = 0; i < 8; ++i) {
        int shift = big_endian ? (7 - i) * 8 : i * 8;
        msg.push_back(static_cast<char>((bits >> shift) & 0xffU));
    }
    return msg;
}

} // namespace

std::string md5_hex(std::string_view data) {
    static constexpr std::array<std::uint32_t, 64> K = [] {
        std::array<std::uint32_t, 64> k{};
        constexpr std::array<std::uint32_t, 64> table = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        for (size_t i = 0; i < 64; ++i) {
            k[i] = table[i];
        }
        return k;
    }();
    static constexpr std::array<unsigned, 64> S = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::uint32_t a0 = 0x67452301;
    std::uint32_t b0 = 0xefcdab89;
    std::uint32_t c0 = 0x98badcfe;
    std::uint32_t d0 = 0x10325476;
    std::string msg = padded(data, false);
    for (size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + off + (static_cast<size_t>(i) * 4));
            m[i] = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
                   (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
        }
        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;
        for (unsigned i = 0; i < 64; ++i) {
            std::uint32_t f;
            unsigned g;
            if (i < 16) {
                f = (b & c) | (~b & d), g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c), g = ((5 * i) + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d, g = ((3 * i) + 5) % 16;
            } else {
                f = c ^ (b | ~d), g = (7 * i) % 16;
            }
            f = f + a + K[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + rotl(f, S[i]);
        }
        a0 += a, b0 += b, c0 += c, d0 += d;
    }
    unsigned char out[16];
    std::uint32_t words[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[(i * 4) + j] = static_cast<unsigned char>((words[i] >> (8U * j)) & 0xffU);
        }
    }
    return to_hex(out, 16);
}

std::string sha1_hex(std::string_view data) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string msg = padded(data, true);
    for (size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + off + (static_cast<size_t>(i) * 4));
            w[i] = (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
                   (static_cast<std::uint32_t>(p[2]) << 8U) | p[3];
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f;
            std::uint32_t k;
            if (i < 20) {
                f = (b & c) | (~b & d), k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d, k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d, k = 0xCA62C1D6;
            }
            std::uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    unsigned char out[20];
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[(i * 4) + j] = static_cast<unsigned char>((h[i] >> (24U - (8 * j))) & 0xffU);
        }
    }
    return to_hex(out, 20);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-signed-bitwise,hicpp-signed-bitwise)

// ── Framework ───────────────────────────────────────────────────────────────

namespace {

fs::path executable_path() {
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        return {};
    }
    buf.resize(std::strlen(buf.c_str()));
    return fs::path(buf);
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

} // namespace

const fs::path& framework_dir() {
    static const fs::path dir = [] {
        if (const char* env = std::getenv("REVET_FRAMEWORK"); env && *env) {
            return fs::path(env);
        }
        std::error_code ec;
        fs::path exe = fs::canonical(executable_path(), ec);
        // Installed: <framework>/prebuilt/bin/revet. Built in a checkout: <repo>/build/tools/revet.
        for (fs::path d = exe.parent_path(); !d.empty(); d = d.parent_path()) {
            if (fs::is_regular_file(d / "template" / "CMakeLists.txt", ec)) {
                return d;
            }
            if (d == d.root_path()) {
                break;
            }
        }
#ifdef REVET_SOURCE_DIR
        // Built out of tree (build directory outside the checkout): the checkout it came from.
        return fs::path(REVET_SOURCE_DIR);
#else
        return exe.parent_path();
#endif
    }();
    return dir;
}

const std::string& framework_version() {
    static const std::string version = [] {
        for (fs::path d = framework_dir(); !d.empty(); d = d.parent_path()) {
            if (auto v = read_file(d / "VERSION")) {
                return strip(*v);
            }
            if (d == d.root_path()) {
                break;
            }
        }
        return std::string("unknown");
    }();
    return version;
}

} // namespace revet::cli

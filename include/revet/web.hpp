#pragma once

#include "states.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <typeindex>
#include <vector>

#include "background.hpp"
#include "c_api.hpp"
#include "crypto.hpp"
#include "css_utils.hpp"
#include "task.hpp"
#include "websocket.hpp"
// Network headers
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace web {

struct ServerConfig {
    int port = 8080;
    std::vector<std::string> binds; // IPv4 addresses. Empty → every interface.
    int request_threads = 0;        // 0 → hardware_concurrency()
    int io_threads = 0;             // 0 → 4 × hardware_concurrency()
};

namespace config {
// Whether Set-Cookie headers should include `Secure`. Off by default so
// localhost dev over plain HTTP works. Production deployments behind HTTPS
// should turn this on at startup (or set env REVET_FORCE_SECURE_COOKIES=1).
inline auto secure_cookies_flag() -> bool& {
    static bool flag = [] -> bool {
        const char* env = std::getenv("REVET_FORCE_SECURE_COOKIES");
        return env != nullptr && std::string_view(env) == "1";
    }();
    return flag;
}
inline auto secure_cookies() -> bool {
    return secure_cookies_flag();
}
inline void secure_cookies(bool enabled) {
    secure_cookies_flag() = enabled;
}

// Largest request body the server reads, in bytes. Bodies are buffered in memory, so this
// is a memory bound, not a feature limit. Default 64 MiB. Sources, each overriding the one
// before: revet.toml `max_body = "256MB"`, env REVET_MAX_BODY, --max-body; app code calling
// max_body_bytes(n) after web::init overrides all of them.
inline constexpr size_t DEFAULT_MAX_BODY_BYTES = 64UL * 1024UL * 1024UL;
inline auto max_body_bytes_ref() -> size_t& {
    static size_t bytes = DEFAULT_MAX_BODY_BYTES;
    return bytes;
}
inline auto max_body_bytes() -> size_t {
    return max_body_bytes_ref();
}
inline void max_body_bytes(size_t bytes) {
    max_body_bytes_ref() = bytes;
}

// Where the app keeps its files. Priority: --data-dir, REVET_DATA_DIR, revet.toml `data_dir`, "data".
inline auto data_dir_ref() -> std::filesystem::path& {
    static std::filesystem::path dir = "data";
    return dir;
}
inline void data_dir(std::filesystem::path dir) {
    data_dir_ref() = std::move(dir);
}
} // namespace config

// The app's data directory (see config::data_dir_ref), e.g. web::data_dir() / "snapshots".
inline auto data_dir() -> const std::filesystem::path& {
    return config::data_dir_ref();
}

// A whole file as text; nullopt if it can't be opened.
inline auto read_text(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(f), {});
}

namespace detail {
inline auto server_config() -> ServerConfig& {
    static ServerConfig cfg;
    return cfg;
}
} // namespace detail

namespace detail {
// "64MB", "512k", "1.5GiB", "1048576" → bytes. Units are binary (K = 1024). Empty on bad input.
inline auto parse_size(std::string_view text) -> std::optional<size_t> {
    text = ws::detail::trim(text);
    const size_t i = std::min(text.find_first_not_of("0123456789."), text.size());
    if (i == 0) {
        return std::nullopt;
    }
    double value = 0;
    try {
        value = std::stod(std::string(text.substr(0, i)));
    } catch (...) {
        return std::nullopt;
    }
    const std::string unit = ws::detail::lower(
        text.substr(i) | std::views::filter([](char c) -> bool { return c != ' '; }) | std::ranges::to<std::string>());
    double mult = 1;
    if (unit.empty() || unit == "b") {
        mult = 1;
    } else if (unit == "k" || unit == "kb" || unit == "kib") {
        mult = 1024.0;
    } else if (unit == "m" || unit == "mb" || unit == "mib") {
        mult = 1024.0 * 1024.0;
    } else if (unit == "g" || unit == "gb" || unit == "gib") {
        mult = 1024.0 * 1024.0 * 1024.0;
    } else {
        return std::nullopt;
    }
    const double bytes = value * mult;
    if (!(bytes >= 1) || bytes > 1e15) {
        return std::nullopt;
    }
    return static_cast<size_t>(bytes);
}

// "127.0.0.1,192.168.64.1" → each address.
inline auto parse_binds(std::string_view text) -> std::vector<std::string> {
    return text | std::views::split(',') | std::views::filter([](auto part) -> bool { return !part.empty(); }) |
           std::views::transform([](auto part) -> std::string { return std::string(std::string_view(part)); }) |
           std::ranges::to<std::vector<std::string>>();
}
} // namespace detail

// The server cannot start: a bad command-line value, a missing key, a port it cannot bind.
// main reports what() and exits with exit_code, so a supervisor sees the failure and retries.
struct StartupError : std::runtime_error {
    StartupError(const std::string& what, int code) : std::runtime_error(what), exit_code(code) {}
    int exit_code;
};

// Parses an integer CLI value in [lo, hi]; throws StartupError (exit code 2) on anything else.
inline auto parse_arg(std::string_view flag, std::string_view value, long long lo, long long hi) -> long long {
    long long out = 0;
    const char* end = std::to_address(value.end());
    const auto [ptr, ec] = std::from_chars(std::to_address(value.begin()), end, out);
    if (ec != std::errc{} || ptr != end || out < lo || out > hi) {
        throw StartupError(std::format("{}: expected an integer in [{}, {}], got '{}'", flag, lo, hi, value), 2);
    }
    return out;
}

inline void init(int argc, char* const* argv) {
    auto& cfg = detail::server_config();
    constexpr long long k_max_threads = 4096;
    constexpr long long k_max_port = 65535;
    if (const char* binds = std::getenv("REVET_BIND")) {
        cfg.binds = detail::parse_binds(binds);
    }
    if (const char* dir = std::getenv("REVET_DATA_DIR"); dir != nullptr && *dir != '\0') {
        config::data_dir(dir);
    }
    if (const char* body = std::getenv("REVET_MAX_BODY")) {
        if (auto n = detail::parse_size(body)) {
            config::max_body_bytes(*n);
        }
    }
    const auto args = std::span(argv, static_cast<size_t>(argc)) | std::ranges::to<std::vector<std::string_view>>();
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args.at(i);
        const bool has_value = i + 1 < args.size();
        if ((arg == "--port" || arg == "-p") && has_value) {
            cfg.port = static_cast<int>(parse_arg(arg, args.at(++i), 1, k_max_port));
        } else if (arg == "--bind" && has_value) {
            cfg.binds = detail::parse_binds(args.at(++i));
        } else if (arg == "--request-threads" && has_value) {
            cfg.request_threads = static_cast<int>(parse_arg(arg, args.at(++i), 0, k_max_threads));
        } else if (arg == "--io-threads" && has_value) {
            cfg.io_threads = static_cast<int>(parse_arg(arg, args.at(++i), 0, k_max_threads));
        } else if (arg == "--data-dir" && has_value) {
            config::data_dir(args.at(++i));
        } else if (arg == "--max-body" && has_value) {
            if (auto n = detail::parse_size(args.at(++i))) {
                config::max_body_bytes(*n);
            }
        }
    }
}

} // namespace web

namespace web {
struct Status {
    int code;
    std::string_view text;

    // NOLINTBEGIN(*-explicit-constructor): implicit on purpose, statuses and paths are used directly as values
    constexpr operator int() const {
        return code;
    }
    constexpr operator std::string_view() const {
        // NOLINTEND(*-explicit-constructor)
        return text;
    }
    [[nodiscard]] auto to_string() const {
        return std::format("{} {}", code, text);
    }
    constexpr auto operator==(const Status&) const -> bool = default;
    constexpr auto operator<(const Status& o) const -> bool {
        return code < o.code;
    }
};
} // namespace web

template<>
struct std::formatter<web::Status> : std::formatter<std::string> {
    auto format(const web::Status& s, auto& ctx) const {
        return std::formatter<std::string>::format(s.to_string(), ctx);
    }
};

namespace web::status {
// 2xx Success
constexpr Status ok = {.code = 200, .text = "OK"};
constexpr Status created = {.code = 201, .text = "Created"};
constexpr Status accepted = {.code = 202, .text = "Accepted"};
constexpr Status no_content = {.code = 204, .text = "No Content"};
constexpr Status partial_content = {.code = 206, .text = "Partial Content"};

// 3xx Redirection
constexpr Status moved_permanently = {.code = 301, .text = "Moved Permanently"};
constexpr Status found = {.code = 302, .text = "Found"};
constexpr Status see_other = {.code = 303, .text = "See Other"};
constexpr Status not_modified = {.code = 304, .text = "Not Modified"};
constexpr Status temporary_redirect = {.code = 307, .text = "Temporary Redirect"};
constexpr Status permanent_redirect = {.code = 308, .text = "Permanent Redirect"};

// 4xx Client Errors
constexpr Status bad_request = {.code = 400, .text = "Bad Request"};
constexpr Status unauthorized = {.code = 401, .text = "Unauthorized"};
constexpr Status payment_required = {.code = 402, .text = "Payment Required"};
constexpr Status forbidden = {.code = 403, .text = "Forbidden"};
constexpr Status not_found = {.code = 404, .text = "Not Found"};
constexpr Status method_not_allowed = {.code = 405, .text = "Method Not Allowed"};
constexpr Status not_acceptable = {.code = 406, .text = "Not Acceptable"};
constexpr Status conflict = {.code = 409, .text = "Conflict"};
constexpr Status gone = {.code = 410, .text = "Gone"};
constexpr Status unprocessable_entity = {.code = 422, .text = "Unprocessable Entity"};
constexpr Status payload_too_large = {.code = 413, .text = "Payload Too Large"};
constexpr Status too_many_requests = {.code = 429, .text = "Too Many Requests"};

// 5xx Server Errors
constexpr Status internal_error = {.code = 500, .text = "Internal Server Error"};
constexpr Status not_implemented = {.code = 501, .text = "Not Implemented"};
constexpr Status bad_gateway = {.code = 502, .text = "Bad Gateway"};
constexpr Status service_unavailable = {.code = 503, .text = "Service Unavailable"};
constexpr Status gateway_timeout = {.code = 504, .text = "Gateway Timeout"};
} // namespace web::status

namespace web {

struct ParamMap {
    std::flat_map<std::string, std::string, std::less<>> data;
    [[nodiscard]] auto get(std::string_view key, std::string_view fallback = "") const -> std::string {
        if (data.contains(key)) {
            return data.at(key);
        }
        return std::string(fallback);
    }
    // Typed read: the fallback's type picks the result, e.g. req.query.get("page", 1) -> int.
    // Missing, empty, or unparseable values (including trailing junk like "12abc") give the fallback;
    // nothing throws. Bools accept 1/true/on/yes and 0/false/off/no.
    template<typename T>
        requires std::is_arithmetic_v<T>
    [[nodiscard]] auto get(std::string_view key, T fallback) const -> T {
        return as<T>(key).value_or(fallback);
    }
    // The value parsed as T, or nullopt if missing or unparseable.
    template<typename T>
        requires std::is_arithmetic_v<T>
    [[nodiscard]] auto as(std::string_view key) const -> std::optional<T> {
        const auto it = data.find(key);
        if (it == data.end()) {
            return std::nullopt;
        }
        // NOLINTNEXTLINE(misc-const-correctness): the numeric branch strips a leading '+'
        std::string_view v = ws::detail::trim(it->second, " \t");
        if (v.empty()) {
            return std::nullopt;
        }
        if constexpr (std::is_same_v<T, bool>) {
            if (v == "1" || v == "true" || v == "on" || v == "yes") {
                return true;
            }
            if (v == "0" || v == "false" || v == "off" || v == "no") {
                return false;
            }
            return std::nullopt;
        } else {
            if (v.front() == '+') {
                v.remove_prefix(1);
            }
            T out{};
            const auto [ptr, ec] = std::from_chars(std::to_address(v.begin()), std::to_address(v.end()), out);
            if (ec != std::errc{} || ptr != std::to_address(v.end())) {
                return std::nullopt;
            }
            if constexpr (std::is_floating_point_v<T>) {
                if (!std::isfinite(out)) {
                    return std::nullopt;
                }
            }
            return out;
        }
    }
    [[nodiscard]] auto has(std::string_view key) const -> bool {
        return data.contains(key);
    }
    [[nodiscard]] auto is(std::string_view key, std::string_view val) const -> bool {
        return get(key) == val;
    }
    [[nodiscard]] auto is(std::string_view key) const -> bool {
        return !get(key).empty();
    }
    // NOTE: inserts a default-constructed string if key is absent
    auto operator[](std::string_view key) -> std::string& {
        return data[std::string(key)];
    }
};

struct CookieJar : ParamMap {
    static constexpr std::string_view prefix = web::states::cookie_prefix;

    template<typename T>
    auto operator+=(const T& val) -> CookieJar& {
        data[std::string(prefix).append(web::states::Traits<T>::name)] = web::states::pack(val);
        return *this;
    }

    template<typename T>
    auto clear() -> CookieJar& {
        data[std::string(prefix).append(web::states::Traits<T>::name)] = "";
        return *this;
    }
};

enum class Method : std::uint8_t { GET, POST, PUT, DEL, PATCH, HEAD, OPTIONS }; // DEL = DELETE (reserved keyword)

constexpr auto to_string(Method m) -> std::string_view {
    switch (m) {
        case Method::GET:
            return "GET";
        case Method::POST:
            return "POST";
        case Method::PUT:
            return "PUT";
        case Method::DEL:
            return "DELETE";
        case Method::PATCH:
            return "PATCH";
        case Method::HEAD:
            return "HEAD";
        case Method::OPTIONS:
            return "OPTIONS";
    }
    return "";
}

struct Url {
    std::string path;
    Url() = default;
    // NOLINTBEGIN(*-explicit-constructor): implicit on purpose, statuses and paths are used directly as values
    Url(const char* p) : path(p) {}
    Url(std::string p) : path(std::move(p)) {}
    operator std::string_view() const {
        // NOLINTEND(*-explicit-constructor)
        return path;
    }
};

enum class FileType : std::uint8_t { jpeg, png, gif, webp, pdf, zip, unknown };

struct UploadedFile {
    std::string field_name;
    std::string filename;     // sanitized basename
    std::string content_type; // from multipart header (untrusted)
    FileType detected_type = FileType::unknown;
    std::filesystem::path path;          // current location; updated by validation middleware
    std::filesystem::path validated_dir; // target dir; set by parser from upload config
};

struct Request {
    Method method = Method::GET;
    std::string http_version;
    Url path;
    ParamMap query;
    ParamMap form;
    ParamMap headers;
    ParamMap cookies;
    ParamMap params;
    std::vector<UploadedFile> files; // populated at parse time; use Context::current().files for post-guard paths
    bool body_too_large = false;
    std::string raw_body; // populated for Content-Type: application/json bodies (POST, PUT, PATCH, DELETE)
    std::string peer_ip;  // socket peer address (IPv4/IPv6 text); empty when unknown. Not the end client behind a proxy.
};

struct Response {
    std::string body;
    std::string location;
    CookieJar cookies;
    ParamMap headers;
    std::string content_type = "text/html";
    Status status = status::ok;

    template<typename T>
    auto state(const T& val) -> Response&;
    template<typename T>
    auto flash(const T& val) -> Response&;

    // Without this, cookies set by this response last until the browser closes.
    std::optional<long> cookie_max_age;
    auto keep_cookies_for(std::chrono::seconds age) -> Response& {
        cookie_max_age = age.count();
        return *this;
    }
};

struct Context {
    std::flat_map<std::type_index, std::any> states;
    std::vector<UploadedFile> files;
    std::filesystem::path upload_tmp;
    static auto current() -> Context& {
        thread_local Context ctx;
        return ctx;
    }
    // Drops everything a previous request left on this thread.
    void reset() {
        states.clear();
        if (!upload_tmp.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(upload_tmp, ec);
            upload_tmp.clear();
        }
        files.clear();
    }
};

template<typename T>
struct State {
    std::optional<T> data;
    explicit operator bool() const {
        return data.has_value();
    }
    auto operator->() const -> const T* {
        return &data.value();
    }
    auto operator*() const -> const T& {
        return data.value();
    }
    auto operator->() -> T* {
        return &data.value();
    }
    auto operator*() -> T& {
        return data.value();
    }

    static auto current() -> State<T> {
        auto& ctx = Context::current();
        if (ctx.states.contains(std::type_index(typeid(T)))) {
            return std::any_cast<State<T>>(ctx.states.at(std::type_index(typeid(T))));
        }
        return {std::nullopt};
    }
};

template<typename T>
struct Flash {
    std::optional<T> data;
    explicit operator bool() const {
        return data.has_value();
    }
    auto operator->() const -> const T* {
        return &data.value();
    }
    auto operator*() const -> const T& {
        return data.value();
    }
    auto operator->() -> T* {
        return &data.value();
    }
    auto operator*() -> T& {
        return data.value();
    }

    static auto current() -> Flash<T> {
        auto& ctx = Context::current();
        if (ctx.states.contains(std::type_index(typeid(T)))) {
            auto val = std::any_cast<Flash<T>>(ctx.states.at(std::type_index(typeid(T))));
            return val;
        }
        return {std::nullopt};
    }
};

// ── Static file serving ───────────────────────────────────────────────────────

namespace detail {
inline auto public_root() -> std::filesystem::path& {
    static std::filesystem::path root;
    return root;
}

inline auto mime_for(std::string_view path) -> std::string_view {
    const auto ext = std::filesystem::path(path).extension().string();
    if (ext == ".html") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".css") {
        return "text/css";
    }
    if (ext == ".js") {
        return "application/javascript";
    }
    if (ext == ".json") {
        return "application/json";
    }
    if (ext == ".svg") {
        return "image/svg+xml";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return "image/jpeg";
    }
    if (ext == ".gif") {
        return "image/gif";
    }
    if (ext == ".ico") {
        return "image/x-icon";
    }
    if (ext == ".woff") {
        return "font/woff";
    }
    if (ext == ".woff2") {
        return "font/woff2";
    }
    if (ext == ".ttf") {
        return "font/ttf";
    }
    if (ext == ".otf") {
        return "font/otf";
    }
    if (ext == ".pdf") {
        return "application/pdf";
    }
    if (ext == ".txt") {
        return "text/plain";
    }
    return "application/octet-stream";
}

inline auto resolve_path(std::string_view path) -> std::filesystem::path {
    const auto& root = public_root();
    return root.empty() ? std::filesystem::path(path) : root / path;
}

inline auto read_file(const std::filesystem::path& full_path, std::string_view content_type) -> Response {
    std::ifstream f(full_path, std::ios::binary);
    if (!f) {
        return {.body = "Not Found", .status = status::not_found};
    }
    std::string body((std::istreambuf_iterator<char>(f)), {});
    return {.body = std::move(body), .content_type = std::string(content_type)};
}
// ── Upload helpers ────────────────────────────────────────────────────────

inline auto read_magic(const std::filesystem::path& p) -> std::array<unsigned char, 12> {
    std::array<unsigned char, 12> buf{};
    if (std::ifstream f{p, std::ios::binary}; f) {
        f.read(c_api::chars(std::span<unsigned char>(buf)), static_cast<std::streamsize>(buf.size()));
    }
    return buf;
}

inline auto detect_type(const std::filesystem::path& p) -> FileType {
    const auto b = read_magic(p);
    if (b.at(0) == 0xFF && b.at(1) == 0xD8 && b.at(2) == 0xFF) {
        return FileType::jpeg;
    }
    if (b.at(0) == 0x89 && b.at(1) == 'P' && b.at(2) == 'N' && b.at(3) == 'G' && b.at(4) == 0x0D && b.at(5) == 0x0A &&
        b.at(6) == 0x1A && b.at(7) == 0x0A) {
        return FileType::png;
    }
    if (b.at(0) == 'G' && b.at(1) == 'I' && b.at(2) == 'F' && b.at(3) == '8') {
        return FileType::gif;
    }
    if (b.at(0) == 'R' && b.at(1) == 'I' && b.at(2) == 'F' && b.at(3) == 'F' && b.at(8) == 'W' && b.at(9) == 'E' &&
        b.at(10) == 'B' && b.at(11) == 'P') {
        return FileType::webp;
    }
    if (b.at(0) == '%' && b.at(1) == 'P' && b.at(2) == 'D' && b.at(3) == 'F') {
        return FileType::pdf;
    }
    if (b.at(0) == 'P' && b.at(1) == 'K' && b.at(2) == 0x03 && b.at(3) == 0x04) {
        return FileType::zip;
    }
    return FileType::unknown;
}

inline auto sanitize_filename(std::string_view name) -> std::string {
    if (const auto p = name.find_last_of("/\\"); p != std::string_view::npos) {
        name = name.substr(p + 1);
    }
    // Control characters dropped.
    std::string out = name | std::views::filter([](char c) -> bool {
                          const auto u = static_cast<unsigned char>(c);
                          return u >= 0x20 && u != 0x7F;
                      }) |
                      std::ranges::to<std::string>();
    const auto start = out.find_first_not_of('.');
    if (start == std::string::npos) {
        return "upload";
    }
    out.erase(0, start);
    return out.empty() ? "upload" : out;
}

inline auto make_upload_tmp() -> std::filesystem::path {
    auto tmpl = (std::filesystem::temp_directory_path() / "revet_XXXXXX").string();
    if (::mkdtemp(tmpl.data()) == nullptr) {
        throw std::runtime_error("failed to create upload tmp dir");
    }
    return {tmpl};
}

inline auto parse_multipart(std::string_view body, std::string_view boundary, const std::filesystem::path& incoming_dir,
                            ParamMap& form_out) -> std::vector<UploadedFile> {
    std::vector<UploadedFile> files;
    const std::string delim = "--" + std::string(boundary);
    const std::string part_sep = "\r\n" + delim;
    constexpr int MAX_PARTS = 32;
    int part_count = 0;

    size_t pos = body.find(delim);
    if (pos == std::string_view::npos) {
        return files;
    }
    pos += delim.size();

    while (part_count < MAX_PARTS) {
        if (pos + 2 > body.size()) {
            break;
        }
        if (body.substr(pos, 2) == "--") {
            break;
        }
        if (body.substr(pos, 2) != "\r\n") {
            break;
        }
        pos += 2;

        std::string field_name;
        std::string filename;
        std::string content_type;

        while (true) {
            const size_t eol = body.find("\r\n", pos);
            if (eol == std::string_view::npos) {
                return files;
            }
            if (eol == pos) {
                pos += 2;
                break;
            }

            const std::string_view hdr = body.substr(pos, eol - pos);
            pos = eol + 2;

            const auto colon = hdr.find(':');
            if (colon == std::string_view::npos) {
                continue;
            }

            std::string hname(hdr.substr(0, colon));
            std::string hval(hdr.substr(colon + 1));
            std::ranges::transform(hname, hname.begin(),
                                   [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
            if (const auto f = hval.find_first_not_of(" \t"); f != std::string::npos) {
                hval = hval.substr(f);
            }

            if (hname == "content-disposition") {
                if (auto n = hval.find("name=\""); n != std::string::npos) {
                    n += 6;
                    if (const auto e = hval.find('"', n); e != std::string::npos) {
                        field_name = hval.substr(n, e - n);
                    }
                }
                if (auto f = hval.find("filename=\""); f != std::string::npos) {
                    f += 10;
                    if (const auto e = hval.find('"', f); e != std::string::npos) {
                        filename = sanitize_filename(hval.substr(f, e - f));
                    }
                }
            } else if (hname == "content-type") {
                content_type = hval;
            }
        }

        const size_t data_end = body.find(part_sep, pos);
        if (data_end == std::string_view::npos) {
            break;
        }

        const std::string_view data = body.substr(pos, data_end - pos);
        pos = data_end + part_sep.size();
        ++part_count;

        if (!filename.empty()) {
            auto dest = incoming_dir / filename;
            if (std::filesystem::exists(dest)) {
                auto stem = dest.stem().string();
                auto ext = dest.extension().string();
                // The first free name_<n>.ext.
                auto candidates = std::views::iota(1) | std::views::transform([&](int i) -> std::filesystem::path {
                                      return incoming_dir / std::format("{}_{}{}", stem, i, ext);
                                  });
                dest = *std::ranges::find_if(
                    candidates, [](const std::filesystem::path& p) -> bool { return !std::filesystem::exists(p); });
            }

            {
                std::ofstream out(dest, std::ios::binary);
                if (!out) {
                    break;
                }
                out.write(data.data(), static_cast<std::streamsize>(data.size()));
            }

            UploadedFile f;
            f.field_name = field_name;
            f.filename = dest.filename().string();
            f.content_type = content_type;
            f.detected_type = detect_type(dest);
            f.path = dest;
            files.push_back(std::move(f));
        } else if (!field_name.empty()) {
            form_out.data[field_name] = std::string(data);
        }
    }

    return files;
}

} // namespace detail

inline void set_public_root(std::filesystem::path root) {
    detail::public_root() = std::move(root);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

inline auto serve_file(std::string_view path) -> Response {
    return detail::read_file(detail::resolve_path(path), detail::mime_for(path));
}

inline auto serve_file(std::string_view path, std::string_view content_type) -> Response {
    return detail::read_file(detail::resolve_path(path), content_type);
}

inline auto redirect(std::string_view loc, Status status = status::see_other) -> Response {
    return {.body = {}, .location = std::string(loc), .cookies = {}, .status = status};
}

inline auto redirect_safe(std::string_view loc) -> Response {
    if (loc.empty() || loc.at(0) != '/') {
        return redirect("/");
    }
    return redirect(loc);
}

inline auto back(const Request& req) -> Response {
    return redirect_safe(req.headers.get("referer", "/"));
}

inline auto to_response(std::string s) -> Response {
    return {.body = std::move(s), .location = {}, .cookies = {}, .status = status::ok};
}

inline auto to_response(html::Node n) -> Response {
    return to_response(std::string(std::move(n)));
}

template<typename T>
inline auto to_response(T&& val) -> Response {
    if constexpr (std::is_same_v<std::decay_t<T>, Response>) {
        return std::forward<T>(val);
    } else {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
        return to_response(std::string(std::forward<T>(val)));
    }
}

inline auto match_route(std::string_view pattern, std::string_view path, ParamMap& out) -> bool {
    const auto split = [](std::string_view s) -> std::vector<std::string_view> {
        return s | std::views::split('/') |
               std::views::transform([](auto part) -> std::string_view { return std::string_view(part); }) |
               std::ranges::to<std::vector<std::string_view>>();
    };
    auto pat = split(pattern);
    auto seg = split(path);
    if (pat.size() != seg.size()) {
        return false;
    }
    auto is_param = [](const auto& ps) -> bool { return std::get<0>(ps).starts_with(':'); };
    const auto pairs = std::views::zip(pat, seg);
    if (!std::ranges::all_of(
            pairs, [&](const auto& ps) -> bool { return is_param(ps) || std::get<0>(ps) == std::get<1>(ps); })) {
        return false;
    }
    ParamMap captured;
    for (const auto& [p, s] : pairs | std::views::filter(is_param)) {
        captured.data[std::string(p.substr(1))] = sanitize::url_decode_path(s);
    }
    out = std::move(captured);
    return true;
}

inline auto parse_params(std::string_view s, char pair_sep = '&', bool url_decode = true) -> ParamMap {
    ParamMap out;
    // key=value pairs; a later duplicate key wins.
    for (auto part : s | std::views::split(pair_sep)) {
        const std::string_view pair(part);
        if (const size_t sep = pair.find('='); sep != std::string_view::npos) {
            const auto key = ws::detail::trim(pair.substr(0, sep), " \t\r\n");
            const std::string val(ws::detail::trim(pair.substr(sep + 1), " \t\r\n"));
            if (!key.empty()) {
                out.data[std::string(key)] = url_decode ? sanitize::url_decode(val) : val;
            }
        }
    }
    return out;
}

namespace detail {
// Text form of the remote address of a connected socket; "" for non-IP sockets or on error.
inline auto peer_ip(int fd) -> std::string {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) {
        return {};
    }
    std::array<char, INET6_ADDRSTRLEN> buf{};
    if (ss.ss_family == AF_INET) {
        const auto* a = reinterpret_cast<const sockaddr_in*>(&ss);
        if (inet_ntop(AF_INET, &a->sin_addr, buf.data(), buf.size()) != nullptr) {
            return buf.data();
        }
    } else if (ss.ss_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(&ss);
        if (inet_ntop(AF_INET6, &a->sin6_addr, buf.data(), buf.size()) != nullptr) {
            return buf.data();
        }
    }
    return {};
}
} // namespace detail

// `leftover`, when given, receives any bytes read past the header section (WebSocket upgrades need them).
inline auto parse_request(int fd, std::string* leftover = nullptr) -> Request {
    // Read until full header section (\r\n\r\n) or 64 KB guard
    std::string raw;
    raw.reserve(4096);
    {
        std::array<char, 4096> hbuf{};
        while (!raw.contains("\r\n\r\n")) {
            const ssize_t n = recv(fd, hbuf.data(), hbuf.size(), 0);
            if (n <= 0) {
                break;
            }
            raw.append(hbuf.data(), static_cast<size_t>(n));
            if (raw.size() > 64UL * 1024UL) {
                break;
            }
        }
    }
    if (raw.empty() || !raw.contains("\r\n\r\n")) {
        return {};
    }
    if (leftover != nullptr) {
        if (const auto header_end = raw.find("\r\n\r\n"); header_end != std::string::npos) {
            leftover->assign(raw, header_end + 4);
        }
    }

    const std::string_view req_str(raw);
    const size_t first_line_end = req_str.find("\r\n");
    if (first_line_end == std::string_view::npos) {
        return {};
    }
    const std::string_view first_line = req_str.substr(0, first_line_end);

    Request req;
    const size_t m_end = first_line.find(' ');
    if (m_end == std::string_view::npos) {
        return {};
    }
    const std::string_view m_str = first_line.substr(0, m_end);

    if (m_str == "GET") {
        req.method = Method::GET;
    } else if (m_str == "POST") {
        req.method = Method::POST;
    } else if (m_str == "PUT") {
        req.method = Method::PUT;
    } else if (m_str == "DELETE") {
        req.method = Method::DEL;
    } else if (m_str == "PATCH") {
        req.method = Method::PATCH;
    } else if (m_str == "HEAD") {
        req.method = Method::HEAD;
    } else if (m_str == "OPTIONS") {
        req.method = Method::OPTIONS;
    }

    const size_t p_end = first_line.find(' ', m_end + 1);
    if (p_end == std::string_view::npos) {
        return {};
    }
    const std::string_view full_path = first_line.substr(m_end + 1, p_end - (m_end + 1));
    req.http_version = std::string(first_line.substr(p_end + 1));
    req.http_version.erase(req.http_version.find_last_not_of(" \t") + 1); // npos + 1 == 0: all blanks

    if (const size_t q_idx = full_path.find('?'); q_idx != std::string_view::npos) {
        req.path = Url(std::string(full_path.substr(0, q_idx)));
        req.query = parse_params(full_path.substr(q_idx + 1), '&');
    } else {
        req.path = Url(std::string(full_path));
    }

    // Parse Headers & Cookies
    size_t head_start = first_line_end + 2;
    size_t content_length = 0;
    int content_length_count = 0;
    bool content_length_bad = false;
    bool saw_transfer_encoding = false;
    bool duplicate_routing_header = false;
    const auto routing_header = [](std::string_view key) -> bool {
        return key == "host" || key == "origin" || key == "upgrade" || key == "connection" ||
               key == "sec-websocket-key" || key == "sec-websocket-version";
    };
    while (true) {
        const size_t line_end = req_str.find("\r\n", head_start);
        if (line_end == std::string_view::npos || line_end == head_start) {
            break;
        }

        const std::string_view line = req_str.substr(head_start, line_end - head_start);
        // A folded line can hide a second Host or Content-Length. This server does not unfold.
        if (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            return {};
        }
        if (const size_t sep = line.find(':'); sep != std::string_view::npos) {
            const std::string key(ws::detail::trim(line.substr(0, sep), " \t\r\n"));
            std::string val(ws::detail::trim(line.substr(sep + 1), " \t\r\n"));

            std::string lkey = key;
            std::ranges::transform(lkey, lkey.begin(),
                                   [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });

            if (routing_header(lkey) && req.headers.data.contains(lkey)) {
                duplicate_routing_header = true;
            }
            req.headers.data[lkey] = val;
            if (lkey == "cookie") {
                // std::println(stderr, "Cookies: {}", val);
                req.cookies = parse_params(val, ';', false);
            }
            if (lkey == "content-length") {
                ++content_length_count;
                unsigned long long n = 0;
                const char* const begin = val.data();
                const char* const end = std::to_address(val.end());
                const auto [ptr, ec] = std::from_chars(begin, end, n);
                // Trailing junk ("5abc", "5 0") is a length two parsers can disagree on.
                const bool too_big = ec == std::errc::result_out_of_range;
                if (ptr != end || (ec != std::errc{} && !too_big)) {
                    content_length_bad = true;
                } else if (too_big || n > config::max_body_bytes()) {
                    req.body_too_large = true;
                    content_length = 0;
                } else {
                    content_length = static_cast<size_t>(n);
                }
            } else if (lkey == "transfer-encoding") {
                saw_transfer_encoding = true;
            }
        }
        head_start = line_end + 2;
    }

    // Chunked bodies are not implemented. Parsing them as the raw body, or accepting two lengths, is smuggling.
    if (content_length_bad || content_length_count > 1 || saw_transfer_encoding || duplicate_routing_header) {
        return {};
    }
    if (req.body_too_large) {
        return req;
    }

    // Body for the methods that carry one
    if (req.method == Method::POST || req.method == Method::PUT || req.method == Method::PATCH ||
        req.method == Method::DEL) {
        const size_t header_end = req_str.find("\r\n\r\n");
        if (header_end != std::string_view::npos) {
            std::string body(req_str.substr(header_end + 4));
            {
                std::array<char, 4096> bbuf{};
                while (body.size() < content_length) {
                    const ssize_t n = recv(fd, bbuf.data(), bbuf.size(), 0);
                    if (n <= 0) {
                        break;
                    }
                    body.append(bbuf.data(), static_cast<size_t>(n));
                }
            }
            if (content_length_count > 0 && body.size() > content_length) {
                body.resize(content_length);
            }

            const auto ct = req.headers.get("content-type");
            const std::string_view mp_prefix = "multipart/form-data";
            if (ct.starts_with(mp_prefix)) {
                const std::string_view ctv(ct);
                const auto bp = ctv.find("boundary=");
                if (bp != std::string_view::npos) {
                    std::string_view boundary = ctv.substr(bp + 9);
                    if (const auto semi = boundary.find(';'); semi != std::string_view::npos) {
                        boundary = boundary.substr(0, semi);
                    }
                    auto& ctx = Context::current();
                    ctx.upload_tmp = detail::make_upload_tmp();
                    ctx.files = detail::parse_multipart(body, boundary, ctx.upload_tmp, req.form);
                    req.files = ctx.files;
                }
            } else if (ct.starts_with("application/json")) {
                req.raw_body = std::move(body);
            } else {
                req.form = parse_params(body);
            }
        }
    }

    return req;
}

inline void send_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const ssize_t sent = ::send(fd, data.data(), data.size(), 0);
        if (sent <= 0) {
            break;
        }
        data.remove_prefix(static_cast<size_t>(sent));
    }
}

inline void send_response(int fd, const Response& resp) {
    std::string head;
    head.reserve(200 + (resp.headers.data.size() * 64) + (resp.cookies.data.size() * 100));
    head += std::format("HTTP/1.1 {} {}\r\n"
                        "Content-Type: {}\r\n"
                        "Content-Length: {}\r\n"
                        "Connection: close\r\n",
                        resp.status.code, resp.status.text, resp.content_type, resp.body.size());

    if (!resp.location.empty()) {
        std::string loc = resp.location;
        std::erase_if(loc, [](char c) -> bool { return c == '\r' || c == '\n'; });
        head += std::format("Location: {}\r\n", loc);
    }
    for (const auto& [k, v] : resp.headers.data) {
        head += std::format("{}: {}\r\n", k, v);
    }
    {
        // Emit `Secure` when configured. Sources: process-wide opt-in via
        // web::config::secure_cookies(true), or env REVET_FORCE_SECURE_COOKIES.
        // Off by default so localhost dev over plain HTTP still receives the
        // cookie (browsers drop `Secure` cookies on http://).
        const bool secure = web::config::secure_cookies();
        const std::string_view suffix = secure ? "; Secure" : "";
        for (const auto& [k, v] : resp.cookies.data) {
            if (v.empty()) {
                head += std::format("Set-Cookie: {}=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0{}\r\n", k, suffix);
            } else {
                const std::string age = resp.cookie_max_age ? std::format("; Max-Age={}", *resp.cookie_max_age) : "";
                head += std::format("Set-Cookie: {}={}; Path=/; HttpOnly; SameSite=Lax{}{}\r\n", k, v, age, suffix);
            }
        }
    }
    head += "\r\n";
    send_all(fd, head);
    send_all(fd, resp.body);
}

struct SSEWriter {
    int fd;

    void send(std::string_view data, std::string_view event = {}, std::string_view id = {}) const {
        std::string msg;
        if (!id.empty()) {
            msg += std::format("id: {}\n", id);
        }
        if (!event.empty()) {
            msg += std::format("event: {}\n", event);
        }
        // One "data:" line per line of `data`.
        if (!data.empty()) {
            for (auto line : data | std::views::split('\n')) {
                msg += "data: ";
                msg += std::string_view(line);
                msg += '\n';
            }
            if (data.back() == '\n') {
                msg.erase(msg.size() - std::string_view("data: \n").size()); // no empty line after a trailing newline
            }
        }
        msg += '\n';
        ::send(fd, msg.data(), msg.size(), 0);
    }

    void comment(std::string_view text = {}) const {
        std::string msg = ": ";
        msg += text;
        msg += "\n\n";
        ::send(fd, msg.data(), msg.size(), 0);
    }

    [[nodiscard]] auto alive() const -> bool {
        char buf = 0;
        const ssize_t r = ::recv(fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
        return r > 0 || (r < 0 && errno == EAGAIN);
    }
};

using Handler = std::function<Response(const Request&)>;
using AsyncHandler = std::function<Task<Response>(const Request&)>;
using SSEHandler = std::function<void(const Request&, SSEWriter&)>;
using WSHandler = std::function<void(const Request&, ws::Conn&)>;
using Next = std::function<Response()>;
using Middleware = std::function<Response(const Request&, const Next&)>;

inline auto error_handlers() -> std::flat_map<int, Handler>& {
    static std::flat_map<int, Handler> handlers;
    return handlers;
}

inline void on_error(Status status, Handler h) {
    error_handlers()[status.code] = std::move(h);
}

// One renderer for every error page, so they share the app's layout:
//   web::on_error_page([](const web::Request& req, web::Status s, std::string_view msg) {
//       return layout_main(req, s.text, html::p{msg.empty() ? s.text : msg});
//   });
// It also serves 404/405/413/500 unless on_error() set a handler for that status.
// The status on the returned response is forced to `s`.
using ErrorPage = std::function<Response(const Request&, Status, std::string_view message)>;

inline auto error_page_renderer() -> ErrorPage& {
    static ErrorPage renderer;
    return renderer;
}

inline void on_error_page(ErrorPage f) {
    error_page_renderer() = std::move(f);
}

// An error page for `s` with an optional message for the user. Without a renderer: plain text.
inline auto error_page(const Request& req, Status s, std::string_view message = {}) -> Response {
    Response resp;
    if (const auto& render = error_page_renderer()) {
        resp = render(req, s, message);
    } else {
        resp.body = message.empty() ? std::format("{} {}", s.code, s.text) : std::string(message);
        resp.content_type = "text/plain; charset=utf-8";
    }
    resp.status = s;
    return resp;
}

inline auto bad_request(const Request& req, std::string_view message = {}) -> Response {
    return error_page(req, status::bad_request, message);
}

inline auto not_found(const Request& req, std::string_view message = {}) -> Response {
    return error_page(req, status::not_found, message);
}

inline auto forbidden(const Request& req, std::string_view message = {}) -> Response {
    return error_page(req, status::forbidden, message);
}

namespace detail {
// The on_error() handler for `s`, else one that uses the error page renderer, else empty.
inline auto error_handler_for(Status s) -> Handler {
    if (error_handlers().contains(s.code)) {
        return error_handlers().at(s.code);
    }
    if (error_page_renderer()) {
        return [s](const Request& req) -> Response { return error_page(req, s); };
    }
    return {};
}
} // namespace detail

inline auto middlewares() -> std::vector<Middleware>& {
    static std::vector<Middleware> mws;
    return mws;
}

inline void use(Middleware mw) {
    middlewares().push_back(std::move(mw));
}

inline void clear_middlewares() {
    middlewares().clear();
}

inline auto dispatch(const Request& req, const Handler& handler,
                     const std::vector<Middleware>& guards = {}) -> Response {
    const auto& global = middlewares();
    std::vector<const Middleware*> pipeline;
    pipeline.reserve(guards.size() + global.size());
    std::ranges::transform(guards, std::back_inserter(pipeline), [](const auto& g) -> auto { return &g; });
    std::ranges::transform(global, std::back_inserter(pipeline), [](const auto& mw) -> auto { return &mw; });

    size_t idx = 0;
    Next next = [&idx, &pipeline, &handler, &req, &next] -> Response {
        if (idx < pipeline.size()) {
            return (*pipeline.at(idx++))(req, next);
        }
        return handler(req);
    };
    return next();
}

namespace detail {

inline auto internal_error_response(const Request& req) -> Response {
    if (const auto handler = detail::error_handler_for(status::internal_error)) {
        return handler(req);
    }
    auto resp = to_response(std::string("500 Internal Server Error"));
    resp.status = status::internal_error;
    return resp;
}

// Parameters are held in the coroutine frame, so req outlives every suspension.
inline auto run_async_handler(std::shared_ptr<const Request> req, AsyncHandler handler,
                              Response from_pipeline) -> Task<Response> {
    Response resp;
    try {
        resp = co_await handler(*req);
    } catch (const std::exception& e) {
        std::println(stderr, "Error handling request: {}", e.what());
        resp = internal_error_response(*req);
    } catch (...) {
        std::println(stderr, "Error handling request: unknown exception");
        resp = internal_error_response(*req);
    }
    // Keep what middleware added on the way out, such as a CSRF cookie.
    // The handler wins where both set the same name.
    resp.cookies.data.insert(from_pipeline.cookies.data.begin(), from_pipeline.cookies.data.end());
    resp.headers.data.insert(from_pipeline.headers.data.begin(), from_pipeline.headers.data.end());
    co_return resp;
}

} // namespace detail

// Async counterpart of dispatch(). Route guards and global middleware run first,
// on the calling thread, and the handler only starts if all of them call next.
// Calls done exactly once, including when the handler throws.
inline void dispatch_async(std::shared_ptr<const Request> req, const AsyncHandler& handler,
                           const std::vector<Middleware>& guards, std::function<void(const Response&)> done) {
    bool reached = false;
    Response from_pipeline;
    try {
        const Handler sentinel = [&reached](const Request&) -> Response {
            reached = true;
            return {};
        };
        from_pipeline = dispatch(*req, sentinel, guards);
    } catch (const std::exception& e) {
        std::println(stderr, "Error handling request: {}", e.what());
        done(detail::internal_error_response(*req));
        return;
    }
    if (!reached) {
        done(from_pipeline);
        return;
    }
    detail::run_async_handler(std::move(req), handler, std::move(from_pipeline))
        .start([done = std::move(done)](const Response& resp) -> void { done(resp); });
}

struct Route {
    std::string path;
    Handler get;
    Handler post;
    Handler put;
    Handler del;
    Handler patch;
    SSEHandler sse;
    AsyncHandler async_get;
    AsyncHandler async_post;
    AsyncHandler async_put;
    AsyncHandler async_del;
    AsyncHandler async_patch;
    std::vector<Middleware> guards;
    WSHandler ws;
};

namespace detail {
// A listening socket, or the reason there isn't one. Empty address → every interface.
inline auto open_listener(const std::string& address, int port) -> std::expected<int, std::string> {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;
    if (!address.empty() && inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
        return std::unexpected(std::format("not an IPv4 address: {}", address));
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::unexpected("socket failed");
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(fd, c_api::sockaddr_of(addr), sizeof(addr)) < 0 || ::listen(fd, SOMAXCONN) < 0) {
        const std::string reason = std::strerror(errno);
        close(fd);
        return std::unexpected(std::format("can't listen on {}:{}: {}", address.empty() ? "*" : address, port, reason));
    }
    return fd;
}

// Runs global middleware and route guards. A response means do not take over the socket.
inline auto sse_gate(const Request& req, const std::vector<Middleware>& guards) -> std::optional<Response> {
    bool reached = false;
    const auto sentinel = [&reached](const Request&) -> Response {
        reached = true;
        return {};
    };
    Response refused = dispatch(req, sentinel, guards);
    if (reached) {
        return std::nullopt;
    }
    return refused;
}
} // namespace detail

inline auto ascii_lower(std::string_view s) -> std::string {
    std::string out(s);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    return out;
}

// RFC 6455 §4.2.1: Connection is a token list that must include "upgrade".
inline auto is_ws_upgrade(const Request& req) -> bool {
    if (req.method != Method::GET || ascii_lower(req.headers.get("upgrade")) != "websocket") {
        return false;
    }
    const std::string connection = ascii_lower(req.headers.get("connection"));
    const bool has_upgrade_token = std::ranges::any_of(connection | std::views::split(','), [](auto token) -> bool {
        return ws::detail::trim(std::string_view(token)) == "upgrade";
    });
    return has_upgrade_token && ws::valid_key(req.headers.get("sec-websocket-key"));
}

// Browsers send Origin on WebSocket upgrades; a cross-site page must not open one with the user's cookies.
// Origin's host must equal Host. Scheme and case are ignored: hosts are case-insensitive and TLS is terminated
// upstream. Proxies must preserve Host. Requests without Origin (devices, CLIs) pass; auth is the route's guard.
inline auto ws_origin_allowed(const Request& req) -> bool {
    const auto origin = req.headers.get("origin");
    if (origin.empty()) {
        return true;
    }
    const auto scheme_end = origin.find("://");
    if (scheme_end == std::string::npos) {
        return false;
    }
    const auto origin_host = ascii_lower(std::string_view(origin).substr(scheme_end + 3));
    const auto host = req.headers.get("host");
    return !host.empty() && origin_host == ascii_lower(host);
}

// One accepted connection. Tests call this with a socketpair; listen() is the poll loop.
inline void on_client(int client_fd, const std::vector<Route>& all_routes) {
    Context::current().reset();
    std::string leftover;
    auto req = parse_request(client_fd, &leftover);
    req.peer_ip = detail::peer_ip(client_fd);
    if (req.path.path.empty()) {
        close(client_fd);
        return;
    }

    if (req.body_too_large) {
        Response resp;
        if (const auto handler = detail::error_handler_for(status::payload_too_large)) {
            resp = handler(req);
        } else {
            resp = to_response(std::string("413 Payload Too Large"));
            resp.status = status::payload_too_large;
        }
        send_response(client_fd, resp);
        close(client_fd);
        return;
    }

    const auto it = std::ranges::find_if(
        all_routes, [&req](const Route& r) -> bool { return match_route(r.path, req.path.path, req.params); });

    if (it == all_routes.end()) {
        Response resp;
        if (const auto handler = detail::error_handler_for(status::not_found)) {
            resp = handler(req);
        } else {
            resp = to_response(std::string("404 Not Found"));
            resp.status = status::not_found;
        }
        send_response(client_fd, resp);
        close(client_fd);
        return;
    }

    // WebSocket is checked first: a route may declare both, and an upgrade must not reach SSE.
    // Guards and global middleware run before the 101, so a refused upgrade stays HTTP.
    if (it->ws && is_ws_upgrade(req)) {
        // RFC 6455 §4.1 requires HTTP/1.1 and Host. Content-Length on an upgrade is a pipelined request.
        if (req.http_version != "HTTP/1.1" || req.headers.get("host").empty() ||
            req.headers.data.contains("content-length")) {
            send_response(client_fd, Response{.body = "Bad Request", .status = status::bad_request});
            close(client_fd);
            return;
        }
        if (!ws_origin_allowed(req)) {
            send_response(client_fd, Response{.body = "Forbidden", .status = status::forbidden});
            close(client_fd);
            return;
        }
        if (req.headers.get("sec-websocket-version") != "13") {
            Response resp = to_response(std::string("Unsupported WebSocket version"));
            resp.status = Status{.code = 426, .text = "Upgrade Required"};
            resp.headers.data["Sec-WebSocket-Version"] = "13";
            send_response(client_fd, resp);
            close(client_fd);
            return;
        }
        // Cap is checked before any thread or guard work, so floods can't exhaust threads.
        auto slot = ws::Slot::acquire();
        if (!slot) {
            send_response(client_fd,
                          Response{.body = "Too many WebSocket connections", .status = status::service_unavailable});
            close(client_fd);
            return;
        }
        try {
            revet::background::spawn([client_fd, slot = std::move(*slot), leftover = std::move(leftover),
                                      ws_fn = it->ws, guards = it->guards,
                                      req](const std::stop_token& stop) mutable -> void {
                // At shutdown, unblock reads and writes so the handler returns and the thread can be joined.
                const std::stop_callback on_stop(stop, [client_fd] -> void { ::shutdown(client_fd, SHUT_RDWR); });
                std::optional<Response> refused;
                try {
                    refused = detail::sse_gate(req, guards);
                } catch (const std::exception& e) {
                    std::println(stderr, "WebSocket middleware error: {}", e.what());
                    refused = to_response(std::string("500 Internal Server Error"));
                    refused->status = status::internal_error;
                }
                if (refused) {
                    send_response(client_fd, *refused);
                    close(client_fd);
                    return;
                }
                // Conn owns fd from here: it closes when the last reference drops.
                const auto conn = ws::Conn::make(client_fd, std::move(slot));
                conn->prime(std::move(leftover));
                const auto deflate = ws::negotiate_deflate(req.headers.get("sec-websocket-extensions"));
                const std::string extensions =
                    deflate.enabled ? std::format("Sec-WebSocket-Extensions: {}\r\n", deflate.response) : "";
                const std::string handshake =
                    std::format("HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: {}\r\n"
                                "{}"
                                "\r\n",
                                ws::accept_key(req.headers.get("sec-websocket-key")), extensions);
                size_t sent = 0;
                while (sent < handshake.size()) {
                    const ssize_t n =
                        ::send(client_fd, std::string_view(handshake).substr(sent).data(), handshake.size() - sent, 0);
                    if (n < 0 && errno == EINTR) {
                        continue;
                    }
                    if (n <= 0) {
                        return;
                    }
                    sent += static_cast<size_t>(n);
                }
                conn->set_deflate(deflate);
                try {
                    ws_fn(req, *conn);
                } catch (const std::exception& e) {
                    std::println(stderr, "WebSocket handler error: {}", e.what());
                }
                conn->close();
            });
        } catch (const std::system_error& e) {
            std::println(stderr, "WebSocket thread: {}", e.what());
            close(client_fd);
        }
        return;
    }

    // SSE — its own thread, so the pool isn't held for the connection's duration
    if (req.method == Method::GET && it->sse) {
        revet::background::spawn(
            [client_fd, sse_fn = it->sse, guards = it->guards, req](const std::stop_token& stop) -> void {
                // At shutdown, unblock writes so the handler returns and the thread can be joined.
                const std::stop_callback on_stop(stop, [client_fd] -> void { ::shutdown(client_fd, SHUT_RDWR); });
                std::optional<Response> refused;
                try {
                    refused = detail::sse_gate(req, guards);
                } catch (const std::exception& e) {
                    std::println(stderr, "SSE middleware error: {}", e.what());
                    refused = to_response(std::string("500 Internal Server Error"));
                    refused->status = status::internal_error;
                }
                if (refused) {
                    send_response(client_fd, *refused);
                } else {
                    const std::string_view sse_head = "HTTP/1.1 200 OK\r\n"
                                                      "Content-Type: text/event-stream\r\n"
                                                      "Cache-Control: no-cache\r\n"
                                                      "Connection: keep-alive\r\n"
                                                      "X-Accel-Buffering: no\r\n"
                                                      "\r\n";
                    ::send(client_fd, sse_head.data(), sse_head.size(), 0);
                    SSEWriter writer{.fd = client_fd};
                    try {
                        sse_fn(req, writer);
                    } catch (const std::exception& e) {
                        std::println(stderr, "SSE handler error: {}", e.what());
                    }
                }
                close(client_fd);
            });
        return;
    }

    // Async handlers — pool thread is freed immediately after start()
    const AsyncHandler* async_h = nullptr;
    if (req.method == Method::GET && it->async_get) {
        async_h = &it->async_get;
    } else if (req.method == Method::POST && it->async_post) {
        async_h = &it->async_post;
    } else if (req.method == Method::PUT && it->async_put) {
        async_h = &it->async_put;
    } else if (req.method == Method::DEL && it->async_del) {
        async_h = &it->async_del;
    } else if (req.method == Method::PATCH && it->async_patch) {
        async_h = &it->async_patch;
    }

    if (async_h != nullptr) {
        dispatch_async(std::make_shared<const Request>(std::move(req)), *async_h, it->guards,
                       [client_fd](const Response& resp) -> void {
                           send_response(client_fd, resp);
                           close(client_fd);
                       });
        return;
    }

    // Sync handlers
    Response resp;
    try {
        if (req.method == Method::GET && it->get) {
            resp = dispatch(req, it->get, it->guards);
        } else if (req.method == Method::POST && it->post) {
            resp = dispatch(req, it->post, it->guards);
        } else if (req.method == Method::PUT && it->put) {
            resp = dispatch(req, it->put, it->guards);
        } else if (req.method == Method::DEL && it->del) {
            resp = dispatch(req, it->del, it->guards);
        } else if (req.method == Method::PATCH && it->patch) {
            resp = dispatch(req, it->patch, it->guards);
        } else {
            if (const auto handler = detail::error_handler_for(status::method_not_allowed)) {
                resp = handler(req);
            } else {
                resp = to_response(std::string("405 Method Not Allowed"));
                resp.status = status::method_not_allowed;
            }
        }
    } catch (const std::exception& e) {
        std::println(stderr, "Error handling request: {}", e.what());
        if (const auto handler = detail::error_handler_for(status::internal_error)) {
            resp = handler(req);
        } else {
            resp = to_response(std::string("500 Internal Server Error"));
            resp.status = status::internal_error;
        }
    }
    send_response(client_fd, resp);
    close(client_fd);
}

inline void listen(const std::vector<Route>& all_routes) {
    // A client that disconnects mid-response (closing a tab on an SSE stream,
    // say) would otherwise kill the server with SIGPIPE on its next write;
    // with it ignored, send() just returns EPIPE.
    (void)std::signal(SIGPIPE, SIG_IGN);

    const auto& cfg = detail::server_config();
    // Found now rather than on the first login.
    if (crypto::key_required() && std::getenv("REVET_COOKIE_KEY") == nullptr) {
        throw StartupError("REVET_COOKIE_KEY is not set, and REVET_REQUIRE_COOKIE_KEY=1 forbids the hostname fallback",
                           1);
    }
    const int rthreads =
        cfg.request_threads > 0 ? cfg.request_threads : static_cast<int>(std::thread::hardware_concurrency());
    const int ithreads =
        cfg.io_threads > 0 ? cfg.io_threads : 4 * static_cast<int>(std::thread::hardware_concurrency());

    detail::request_pool() = std::make_unique<WorkerPool>(rthreads);
    detail::io_pool() = std::make_unique<WorkerPool>(ithreads);

    auto listen_on = [&](const std::string& address) -> pollfd {
        const auto fd = detail::open_listener(address, cfg.port);
        if (!fd) {
            throw StartupError(fd.error(), 1);
        }
        return {.fd = *fd, .events = POLLIN, .revents = 0};
    };
    const auto binds = cfg.binds.empty() ? std::vector<std::string>{""} : cfg.binds;
    auto listeners = binds | std::views::transform(listen_on) | std::ranges::to<std::vector<pollfd>>();

    std::println("Server running on port {} at {} "
                 "(request-threads={}, io-threads={})",
                 cfg.port, cfg.binds.empty() ? std::string("every interface") : std::format("{}", cfg.binds), rthreads,
                 ithreads);

    while (true) {
        if (poll(listeners.data(), static_cast<nfds_t>(listeners.size()), -1) <= 0) {
            continue;
        }
        for (const auto& listener : listeners | std::views::filter([](const pollfd& l) -> bool {
                                        // POSIX poll flags are int.
                                        return (l.revents & POLLIN) != 0; // NOLINT(*-signed-bitwise)
                                    })) {
            const int client_fd = accept(listener.fd, nullptr, nullptr);
            if (client_fd < 0) {
                continue;
            }

            detail::request_pool()->submit([client_fd, &all_routes] -> void { on_client(client_fd, all_routes); });
        }
    }
}

// Compat overload: explicit port overrides config
inline void listen(const std::vector<Route>& routes, int port) {
    detail::server_config().port = port;
    listen(routes);
}

} // namespace web

namespace web {
template<typename T>
inline auto Response::state(const T& val) -> Response& {
    cookies += val;
    return *this;
}

template<typename T>
inline auto Response::flash(const T& val) -> Response& {
    return state(val);
}
} // namespace web

namespace web::api {
// JSON error response — {"error":"<msg>"} with the given status
inline auto error(Status s, std::string_view msg) -> Response {
    auto escaped = [](const char& c) -> std::string_view {
        if (c == '"') {
            return "\\\"";
        }
        if (c == '\\') {
            return "\\\\";
        }
        return {&c, 1};
    };
    std::string body = R"({"error":")" +
                       (msg | std::views::transform(escaped) | std::views::join | std::ranges::to<std::string>()) +
                       "\"}";
    return {.body = std::move(body), .content_type = "application/json", .status = s};
}

// Compare against a fixed token using constant-time equality. Use this
// instead of `tok == "secret"` in user code — `==` is a timing oracle.
//
//   r.guards = { web::api::bearer_static(std::getenv("API_KEY") ?: "") };
//
// An empty `expected` rejects every request, so a missing env var fails
// closed rather than silently accepting any caller.
inline auto bearer_static(std::string expected) -> Middleware;

// Bearer token guard — reads Authorization: Bearer <token>, calls verify(token).
// Use when you only need to validate; handler never sees the raw token.
// IMPORTANT: inside `verify`, compare secrets with `web::crypto::secure_eq`,
// not `==`, to avoid leaking the token byte-by-byte through timing.
inline auto bearer(std::function<bool(std::string_view)> verify) -> Middleware {
    return [v = std::move(verify)](const Request& req, const Next& next) -> Response {
        const auto auth = req.headers.get("authorization");
        if (!std::string_view(auth).starts_with("Bearer ")) {
            return api::error(status::unauthorized, "missing token");
        }
        if (!v(std::string_view(auth).substr(7))) {
            return api::error(status::unauthorized, "invalid token");
        }
        return next();
    };
}

// Typed bearer guard — resolves the token to a principal T and stores it in context.
// Return std::nullopt from resolve() to reject with 401.
// Retrieve in the handler with web::api::principal<T>().
template<typename T>
inline auto bearer(std::function<std::optional<T>(std::string_view)> resolve) -> Middleware {
    return [r = std::move(resolve)](const Request& req, const Next& next) -> Response {
        const auto auth = req.headers.get("authorization");
        if (!std::string_view(auth).starts_with("Bearer ")) {
            return api::error(status::unauthorized, "missing token");
        }
        if (auto result = r(std::string_view(auth).substr(7))) {
            Context::current().states[typeid(T)] = std::move(*result);
            return next();
        }
        return api::error(status::unauthorized, "invalid token");
    };
}

// Retrieve the principal stored by bearer<T>(). Call only from handlers
// protected by a bearer<T> guard — throws std::bad_any_cast otherwise.
template<typename T>
inline auto principal() -> const T& {
    return std::any_cast<const T&>(Context::current().states.at(typeid(T)));
}

inline auto bearer_static(std::string expected) -> Middleware {
    // Empty expected → fail closed.
    return bearer([e = std::move(expected)](std::string_view tok) -> bool {
        if (e.empty()) {
            return false;
        }
        return web::crypto::secure_eq(tok, e);
    });
}
} // namespace web::api

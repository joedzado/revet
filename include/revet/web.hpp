#pragma once

#include "states.hpp"

#include <memory>
#include <algorithm>
#include <any>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <flat_map>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include "crypto.hpp"
#include "css_utils.hpp"
#include "task.hpp"

// Network headers
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace web {

struct ServerConfig {
    int port = 8080;
    int request_threads = 0; // 0 → hardware_concurrency()
    int io_threads = 0;      // 0 → 4 × hardware_concurrency()
};

namespace config {
// Whether Set-Cookie headers should include `Secure`. Off by default so
// localhost dev over plain HTTP works. Production deployments behind HTTPS
// should turn this on at startup (or set env REVET_FORCE_SECURE_COOKIES=1).
inline bool& secure_cookies_flag() {
    static bool flag = []() {
        const char* env = std::getenv("REVET_FORCE_SECURE_COOKIES");
        return env != nullptr && std::string_view(env) == "1";
    }();
    return flag;
}
inline bool secure_cookies()             { return secure_cookies_flag(); }
inline void secure_cookies(bool enabled) { secure_cookies_flag() = enabled; }
} // namespace config

namespace detail {
inline auto server_config() -> ServerConfig& {
    static ServerConfig cfg;
    return cfg;
}
} // namespace detail

inline void init(int argc, char** argv) {
    auto& cfg = detail::server_config();
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            cfg.port = std::stoi(argv[++i]);
        } else if (arg == "--request-threads" && i + 1 < argc) {
            cfg.request_threads = std::stoi(argv[++i]);
        } else if (arg == "--io-threads" && i + 1 < argc) {
            cfg.io_threads = std::stoi(argv[++i]);
        }
    }
}

} // namespace web

namespace web {
struct Status {
    int code;
    std::string_view text;

    constexpr operator int() const {
        return code;
    }
    constexpr operator std::string_view() const {
        return text;
    }
    auto to_string() const {
        return std::format("{} {}", code, text);
    }
    constexpr bool operator==(const Status&) const = default;
    constexpr bool operator<(const Status& o) const {
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
constexpr Status ok = {200, "OK"};
constexpr Status created = {201, "Created"};
constexpr Status accepted = {202, "Accepted"};
constexpr Status no_content = {204, "No Content"};
constexpr Status partial_content = {206, "Partial Content"};

// 3xx Redirection
constexpr Status moved_permanently = {301, "Moved Permanently"};
constexpr Status found = {302, "Found"};
constexpr Status see_other = {303, "See Other"};
constexpr Status not_modified = {304, "Not Modified"};
constexpr Status temporary_redirect = {307, "Temporary Redirect"};
constexpr Status permanent_redirect = {308, "Permanent Redirect"};

// 4xx Client Errors
constexpr Status bad_request = {400, "Bad Request"};
constexpr Status unauthorized = {401, "Unauthorized"};
constexpr Status payment_required = {402, "Payment Required"};
constexpr Status forbidden = {403, "Forbidden"};
constexpr Status not_found = {404, "Not Found"};
constexpr Status method_not_allowed = {405, "Method Not Allowed"};
constexpr Status not_acceptable = {406, "Not Acceptable"};
constexpr Status conflict = {409, "Conflict"};
constexpr Status gone = {410, "Gone"};
constexpr Status unprocessable_entity = {422, "Unprocessable Entity"};
constexpr Status payload_too_large = {413, "Payload Too Large"};
constexpr Status too_many_requests = {429, "Too Many Requests"};

// 5xx Server Errors
constexpr Status internal_error = {500, "Internal Server Error"};
constexpr Status not_implemented = {501, "Not Implemented"};
constexpr Status bad_gateway = {502, "Bad Gateway"};
constexpr Status service_unavailable = {503, "Service Unavailable"};
constexpr Status gateway_timeout = {504, "Gateway Timeout"};
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

constexpr std::string_view to_string(Method m) {
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
    Url(const char* p) : path(p) {}
    Url(std::string p) : path(std::move(p)) {}
    operator std::string_view() const {
        return path;
    }
};

inline constexpr size_t MAX_BODY_BYTES = 2UL * 1024UL * 1024UL; // 2 MB

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
    Url path;
    ParamMap query;
    ParamMap form;
    ParamMap headers;
    ParamMap cookies;
    ParamMap params;
    std::vector<UploadedFile> files; // populated at parse time; use Context::current().files for post-guard paths
    bool body_too_large = false;
    std::string raw_body; // populated for Content-Type: application/json POST bodies
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
};

struct Context {
    std::flat_map<std::type_index, std::any> states;
    std::vector<UploadedFile> files;
    std::filesystem::path upload_tmp;
    static auto current() -> Context& {
        thread_local Context ctx;
        return ctx;
    }
};

template<typename T>
struct State {
    std::optional<T> data;
    explicit operator bool() const {
        return data.has_value();
    }
    const T* operator->() const {
        return &data.value();
    }
    const T& operator*() const {
        return data.value();
    }
    T* operator->() {
        return &data.value();
    }
    T& operator*() {
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
    const T* operator->() const {
        return &data.value();
    }
    const T& operator*() const {
        return data.value();
    }
    T* operator->() {
        return &data.value();
    }
    T& operator*() {
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
    auto& root = public_root();
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
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    }
    return buf;
}

inline auto detect_type(const std::filesystem::path& p) -> FileType {
    const auto b = read_magic(p);
    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) {
        return FileType::jpeg;
    }
    if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G' && b[4] == 0x0D && b[5] == 0x0A && b[6] == 0x1A &&
        b[7] == 0x0A) {
        return FileType::png;
    }
    if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8') {
        return FileType::gif;
    }
    if (b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' &&
        b[11] == 'P') {
        return FileType::webp;
    }
    if (b[0] == '%' && b[1] == 'P' && b[2] == 'D' && b[3] == 'F') {
        return FileType::pdf;
    }
    if (b[0] == 'P' && b[1] == 'K' && b[2] == 0x03 && b[3] == 0x04) {
        return FileType::zip;
    }
    return FileType::unknown;
}

inline auto sanitize_filename(std::string_view name) -> std::string {
    if (auto p = name.find_last_of("/\\"); p != std::string_view::npos) {
        name = name.substr(p + 1);
    }
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c == 0 || c < 0x20 || c == 0x7F) {
            continue;
        }
        out += static_cast<char>(c);
    }
    if (auto start = out.find_first_not_of('.'); start == std::string::npos) {
        return "upload";
    } else if (start > 0) {
        out = out.substr(start);
    }
    return out.empty() ? "upload" : out;
}

inline auto make_upload_tmp() -> std::filesystem::path {
    auto tmpl = (std::filesystem::temp_directory_path() / "revet_XXXXXX").string();
    if (::mkdtemp(tmpl.data()) == nullptr) {
        throw std::runtime_error("failed to create upload tmp dir");
    }
    return std::filesystem::path(tmpl);
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

        std::string field_name, filename, content_type;

        while (true) {
            size_t eol = body.find("\r\n", pos);
            if (eol == std::string_view::npos) {
                return files;
            }
            if (eol == pos) {
                pos += 2;
                break;
            }

            std::string_view hdr = body.substr(pos, eol - pos);
            pos = eol + 2;

            auto colon = hdr.find(':');
            if (colon == std::string_view::npos) {
                continue;
            }

            std::string hname(hdr.substr(0, colon));
            std::string hval(hdr.substr(colon + 1));
            std::ranges::transform(hname, hname.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (auto f = hval.find_first_not_of(" \t"); f != std::string::npos) {
                hval = hval.substr(f);
            }

            if (hname == "content-disposition") {
                if (auto n = hval.find("name=\""); n != std::string::npos) {
                    n += 6;
                    if (auto e = hval.find('"', n); e != std::string::npos) {
                        field_name = hval.substr(n, e - n);
                    }
                }
                if (auto f = hval.find("filename=\""); f != std::string::npos) {
                    f += 10;
                    if (auto e = hval.find('"', f); e != std::string::npos) {
                        filename = sanitize_filename(hval.substr(f, e - f));
                    }
                }
            } else if (hname == "content-type") {
                content_type = hval;
            }
        }

        size_t data_end = body.find(part_sep, pos);
        if (data_end == std::string_view::npos) {
            break;
        }

        std::string_view data = body.substr(pos, data_end - pos);
        pos = data_end + part_sep.size();
        ++part_count;

        if (!filename.empty()) {
            auto dest = incoming_dir / filename;
            if (std::filesystem::exists(dest)) {
                auto stem = dest.stem().string();
                auto ext = dest.extension().string();
                for (int i = 1; std::filesystem::exists(dest); ++i) {
                    std::string name = stem;
                    name += '_';
                    name += std::to_string(i);
                    name += ext;
                    dest = incoming_dir / name;
                }
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
        return to_response(std::string(std::forward<T>(val)));
    }
}

inline auto match_route(std::string_view pattern, std::string_view path, ParamMap& out) -> bool {
    auto split = [](std::string_view s) {
        std::vector<std::string_view> parts;
        size_t start = 0;
        while (start <= s.size()) {
            size_t end = s.find('/', start);
            parts.push_back(s.substr(start, end - start));
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return parts;
    };
    auto pat = split(pattern);
    auto seg = split(path);
    if (pat.size() != seg.size()) {
        return false;
    }
    ParamMap captured;
    for (size_t i = 0; i < pat.size(); ++i) {
        if (!pat[i].empty() && pat[i][0] == ':') {
            captured.data[std::string(pat[i].substr(1))] = std::string(seg[i]);
        } else if (pat[i] != seg[i]) {
            return false;
        }
    }
    out = std::move(captured);
    return true;
}

inline auto parse_params(std::string_view s, char pair_sep = '&', bool url_decode = true) -> ParamMap {
    ParamMap out;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(pair_sep, start);
        std::string_view const pair = s.substr(start, end - start);
        if (size_t const sep = pair.find('='); sep != std::string_view::npos) {
            std::string key(pair.substr(0, sep));
            std::string val(pair.substr(sep + 1));
            auto trim = [](std::string& str) {
                str.erase(str.find_last_not_of(" \t\r\n") + 1);
                str.erase(0, str.find_first_not_of(" \t\r\n"));
            };
            trim(key);
            trim(val);
            if (!key.empty()) {
                out.data[key] = url_decode ? sanitize::url_decode(val) : std::move(val);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

inline auto parse_request(int fd) -> Request {
    // Read until full header section (\r\n\r\n) or 64 KB guard
    std::string raw;
    raw.reserve(4096);
    {
        std::array<char, 4096> hbuf{};
        while (raw.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = recv(fd, hbuf.data(), hbuf.size(), 0);
            if (n <= 0) {
                break;
            }
            raw.append(hbuf.data(), static_cast<size_t>(n));
            if (raw.size() > 64UL * 1024UL) {
                break;
            }
        }
    }
    if (raw.empty()) {
        return {};
    }

    std::string_view const req_str(raw);
    size_t const first_line_end = req_str.find("\r\n");
    if (first_line_end == std::string_view::npos) {
        return {};
    }
    std::string_view const first_line = req_str.substr(0, first_line_end);

    Request req;
    size_t const m_end = first_line.find(' ');
    if (m_end == std::string_view::npos) {
        return {};
    }
    std::string_view const m_str = first_line.substr(0, m_end);

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

    size_t const p_end = first_line.find(' ', m_end + 1);
    if (p_end == std::string_view::npos) {
        return {};
    }
    std::string_view const full_path = first_line.substr(m_end + 1, p_end - (m_end + 1));

    if (size_t const q_idx = full_path.find('?'); q_idx != std::string_view::npos) {
        req.path = Url(std::string(full_path.substr(0, q_idx)));
        req.query = parse_params(full_path.substr(q_idx + 1), '&');
    } else {
        req.path = Url(std::string(full_path));
    }

    // Parse Headers & Cookies
    size_t head_start = first_line_end + 2;
    size_t content_length = 0;
    while (true) {
        size_t const line_end = req_str.find("\r\n", head_start);
        if (line_end == std::string_view::npos || line_end == head_start) {
            break;
        }

        std::string_view const line = req_str.substr(head_start, line_end - head_start);
        if (size_t const sep = line.find(':'); sep != std::string_view::npos) {
            std::string key(line.substr(0, sep));
            std::string val(line.substr(sep + 1));
            auto trim = [](std::string& str) {
                if (size_t last = str.find_last_not_of(" \t\r\n"); last != std::string::npos) {
                    str.erase(last + 1);
                }
                if (size_t first = str.find_first_not_of(" \t\r\n"); first != std::string::npos) {
                    str.erase(0, first);
                } else {
                    str.clear();
                }
            };
            trim(key);
            trim(val);

            std::string lkey = key;
            std::ranges::transform(lkey, lkey.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            req.headers.data[lkey] = val;
            if (lkey == "cookie") {
                // std::println(stderr, "Cookies: {}", val);
                req.cookies = parse_params(val, ';', false);
            }
            if (lkey == "content-length") {
                try {
                    content_length = std::stoul(val);
                } catch (...) {
                    content_length = 0;
                }
                if (content_length > MAX_BODY_BYTES) {
                    req.body_too_large = true;
                    content_length = 0;
                }
            }
        }
        head_start = line_end + 2;
    }

    // Body for POST
    if (req.method == Method::POST) {
        size_t const header_end = req_str.find("\r\n\r\n");
        if (header_end != std::string_view::npos) {
            std::string body(req_str.substr(header_end + 4));
            {
                std::array<char, 4096> bbuf{};
                while (body.size() < content_length) {
                    ssize_t n = recv(fd, bbuf.data(), bbuf.size(), 0);
                    if (n <= 0) {
                        break;
                    }
                    body.append(bbuf.data(), static_cast<size_t>(n));
                }
            }

            const auto ct = req.headers.get("content-type");
            const std::string_view mp_prefix = "multipart/form-data";
            if (ct.starts_with(mp_prefix)) {
                std::string_view ctv(ct);
                auto bp = ctv.find("boundary=");
                if (bp != std::string_view::npos) {
                    std::string_view boundary = ctv.substr(bp + 9);
                    if (auto semi = boundary.find(';'); semi != std::string_view::npos) {
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

inline void send_all(int fd, const char* data, size_t len) {
    while (len > 0) {
        ssize_t sent = ::send(fd, data, len, 0);
        if (sent <= 0) {
            break;
        }
        data += sent;
        len -= static_cast<size_t>(sent);
    }
}

inline void send_response(int fd, const Response& resp) {
    std::string head;
    head.reserve(200 + resp.headers.data.size() * 64 + resp.cookies.data.size() * 100);
    head += std::format("HTTP/1.1 {} {}\r\n"
                        "Content-Type: {}\r\n"
                        "Content-Length: {}\r\n"
                        "Connection: close\r\n",
                        resp.status.code, resp.status.text, resp.content_type, resp.body.size());

    if (!resp.location.empty()) {
        std::string loc = resp.location;
        std::erase_if(loc, [](char c) { return c == '\r' || c == '\n'; });
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
                head += std::format("Set-Cookie: {}={}; Path=/; HttpOnly; SameSite=Lax{}\r\n", k, v, suffix);
            }
        }
    }
    head += "\r\n";
    send_all(fd, head.data(), head.size());
    send_all(fd, resp.body.data(), resp.body.size());
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
        std::string_view rem = data;
        while (!rem.empty()) {
            auto nl = rem.find('\n');
            msg += "data: ";
            msg += rem.substr(0, nl);
            msg += '\n';
            if (nl == std::string_view::npos) {
                break;
            }
            rem = rem.substr(nl + 1);
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

    [[nodiscard]] bool alive() const {
        char buf;
        ssize_t r = ::recv(fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
        return r > 0 || (r < 0 && errno == EAGAIN);
    }
};

using Handler = std::function<Response(const Request&)>;
using AsyncHandler = std::function<Task<Response>(const Request&)>;
using SSEHandler = std::function<void(const Request&, SSEWriter&)>;
using Next = std::function<Response()>;
using Middleware = std::function<Response(const Request&, const Next&)>;

inline auto error_handlers() -> std::flat_map<int, Handler>& {
    static std::flat_map<int, Handler> handlers;
    return handlers;
}

inline void on_error(Status status, Handler h) {
    error_handlers()[status.code] = std::move(h);
}

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

inline auto dispatch(const Request& req, const Handler& handler, const std::vector<Middleware>& guards = {})
    -> Response {
    const auto& global = middlewares();
    std::vector<const Middleware*> pipeline;
    pipeline.reserve(guards.size() + global.size());
    std::ranges::transform(guards, std::back_inserter(pipeline), [](const auto& g) { return &g; });
    std::ranges::transform(global, std::back_inserter(pipeline), [](const auto& mw) { return &mw; });

    size_t idx = 0;
    Next next = [&idx, &pipeline, &handler, &req, &next]() -> Response {
        if (idx < pipeline.size()) {
            return (*pipeline[idx++])(req, next);
        }
        return handler(req);
    };
    return next();
}

namespace detail {

inline auto internal_error_response(const Request& req) -> Response {
    if (error_handlers().contains(status::internal_error)) {
        return error_handlers().at(status::internal_error)(req);
    }
    auto resp = to_response(std::string("500 Internal Server Error"));
    resp.status = status::internal_error;
    return resp;
}

// Parameters are held in the coroutine frame, so req outlives every suspension.
inline auto run_async_handler(std::shared_ptr<const Request> req, const AsyncHandler& handler, Response from_pipeline)
    -> Task<Response> {
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
    for (const auto& [key, value] : from_pipeline.cookies.data) {
        resp.cookies.data.try_emplace(key, value);
    }
    for (const auto& [key, value] : from_pipeline.headers.data) {
        resp.headers.data.try_emplace(key, value);
    }
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
        .start([done = std::move(done)](Response resp) { done(resp); });
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
};

inline void listen(const std::vector<Route>& all_routes) {
    const auto& cfg = detail::server_config();
    const int rthreads =
        cfg.request_threads > 0 ? cfg.request_threads : static_cast<int>(std::thread::hardware_concurrency());
    const int ithreads =
        cfg.io_threads > 0 ? cfg.io_threads : 4 * static_cast<int>(std::thread::hardware_concurrency());

    detail::request_pool() = std::make_unique<WorkerPool>(rthreads);
    detail::io_pool() = std::make_unique<WorkerPool>(ithreads);

    std::println("Server running on http://localhost:{} "
                 "(request-threads={}, io-threads={})",
                 cfg.port, rthreads, ithreads);

    int const server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::println(stderr, "socket failed");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg.port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::println(stderr, "bind failed");
        close(server_fd);
        return;
    }

    ::listen(server_fd, SOMAXCONN);

    while (true) {
        int const client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            continue;
        }

        detail::request_pool()->submit([client_fd, &all_routes]() {
            auto& ctx = Context::current();
            ctx.states.clear();
            if (!ctx.upload_tmp.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(ctx.upload_tmp, ec);
                ctx.upload_tmp.clear();
            }
            ctx.files.clear();
            auto req = parse_request(client_fd);
            if (req.path.path.empty()) {
                close(client_fd);
                return;
            }

            if (req.body_too_large) {
                Response resp;
                if (error_handlers().contains(status::payload_too_large)) {
                    resp = error_handlers().at(status::payload_too_large)(req);
                } else {
                    resp = to_response(std::string("413 Payload Too Large"));
                    resp.status = status::payload_too_large;
                }
                send_response(client_fd, resp);
                close(client_fd);
                return;
            }

            auto it = std::ranges::find_if(
                all_routes, [&req](const Route& r) { return match_route(r.path, req.path.path, req.params); });

            if (it == all_routes.end()) {
                Response resp;
                if (error_handlers().contains(status::not_found)) {
                    resp = error_handlers().at(status::not_found)(req);
                } else {
                    resp = to_response(std::string("404 Not Found"));
                    resp.status = status::not_found;
                }
                send_response(client_fd, resp);
                close(client_fd);
                return;
            }

            // SSE — detached thread so pool isn't held for connection duration
            if (req.method == Method::GET && it->sse) {
                std::thread([client_fd, sse_fn = it->sse, guards = it->guards, req]() {
                    bool ok = true;
                    Response guard_resp;
                    if (!guards.empty()) {
                        bool reached = false;
                        auto sentinel = [&reached](const Request&) -> Response {
                            reached = true;
                            return {};
                        };
                        guard_resp = dispatch(req, sentinel, guards);
                        ok = reached;
                    }
                    if (!ok) {
                        send_response(client_fd, guard_resp);
                    } else {
                        const std::string_view sse_head = "HTTP/1.1 200 OK\r\n"
                                                          "Content-Type: text/event-stream\r\n"
                                                          "Cache-Control: no-cache\r\n"
                                                          "Connection: keep-alive\r\n"
                                                          "X-Accel-Buffering: no\r\n"
                                                          "\r\n";
                        ::send(client_fd, sse_head.data(), sse_head.size(), 0);
                        SSEWriter writer{client_fd};
                        try {
                            sse_fn(req, writer);
                        } catch (const std::exception& e) {
                            std::println(stderr, "SSE handler error: {}", e.what());
                        }
                    }
                    close(client_fd);
                }).detach();
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

            if (async_h) {
                dispatch_async(std::make_shared<const Request>(std::move(req)), *async_h, it->guards,
                               [client_fd](const Response& resp) {
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
                    if (error_handlers().contains(status::method_not_allowed)) {
                        resp = error_handlers().at(status::method_not_allowed)(req);
                    } else {
                        resp = to_response(std::string("405 Method Not Allowed"));
                        resp.status = status::method_not_allowed;
                    }
                }
            } catch (const std::exception& e) {
                std::println(stderr, "Error handling request: {}", e.what());
                if (error_handlers().contains(status::internal_error)) {
                    resp = error_handlers().at(status::internal_error)(req);
                } else {
                    resp = to_response(std::string("500 Internal Server Error"));
                    resp.status = status::internal_error;
                }
            }
            send_response(client_fd, resp);
            close(client_fd);
        });
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
    std::string body;
    body.reserve(12 + msg.size());
    body += R"({"error":")";
    for (char c : msg) {
        if (c == '"') {
            body += "\\\"";
        } else if (c == '\\') {
            body += "\\\\";
        } else {
            body += c;
        }
    }
    body += "\"}";
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
        if (e.empty()) return false;
        return web::crypto::secure_eq(tok, e);
    });
}
} // namespace web::api

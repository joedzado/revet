#pragma once

// Outgoing mail over SMTP. Small on purpose: one message, plain text plus HTML, sent from a background
// thread so a slow mail server never holds up a request.
//
//   REVET_SMTP_URL    smtps://smtp.example.com:465 (TLS from the start) or smtp://smtp.example.com:587 (STARTTLS)
//   REVET_SMTP_USER   login, if the server wants one
//   REVET_SMTP_PASS
//   REVET_MAIL_FROM   "My App <login@example.com>" or a bare address
//   REVET_SMTP_CA_FILE  optional CA bundle for a relay whose certificate is not publicly trusted
//
//   web::mail::outbox().enqueue({.to = "ann@example.com", .subject = "Hello", .text = "Hi", .html = "<p>Hi</p>"});
//
// A message that fails is retried a few times with a growing pause, then dropped with a log line that names
// only the recipient's domain. Plain `smtp://` without TLS is refused unless the server is on this machine
// or REVET_SMTP_ALLOW_PLAINTEXT=1, because the login would cross the network in the clear.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <sodium.h>

#include "log.hpp"

namespace web::mail {

struct Message {
    std::string to;      // one bare address, already validated by the caller or rejected here
    std::string subject; // single line
    std::string text;
    std::string html;    // optional
};

struct Config {
    std::string smtp_url;
    std::string username;
    std::string password;
    std::string from;
    bool allow_plaintext = false;
    std::string ca_file; // REVET_SMTP_CA_FILE: trust this CA bundle for the server's certificate (a private relay)
    long timeout_seconds = 20;
};

// Mail may go out without TLS only to a server on this machine, or when the operator says so. The host is
// compared whole: "smtp://localhost.evil.example.com" is not this machine.
[[nodiscard]] inline auto plaintext_allowed(const Config& c) -> bool {
    if (c.allow_plaintext) return true;
    std::string_view u = c.smtp_url;
    if (!u.starts_with("smtp://")) return false;
    u.remove_prefix(7);
    if (u.starts_with("[")) { // [::1]:25
        const auto close = u.find(']');
        return close != std::string_view::npos && u.substr(0, close + 1) == "[::1]";
    }
    const auto host = u.substr(0, u.find_first_of(":/"));
    return host == "localhost" || host == "127.0.0.1";
}

inline auto config() -> Config& {
    static Config c = [] {
        Config cfg;
        const auto env = [](const char* name) { const char* v = std::getenv(name); return v != nullptr ? std::string(v) : std::string(); };
        cfg.smtp_url = env("REVET_SMTP_URL");
        cfg.username = env("REVET_SMTP_USER");
        cfg.password = env("REVET_SMTP_PASS");
        cfg.from = env("REVET_MAIL_FROM");
        cfg.allow_plaintext = env("REVET_SMTP_ALLOW_PLAINTEXT") == "1";
        cfg.ca_file = env("REVET_SMTP_CA_FILE");
        return cfg;
    }();
    return c;
}

// ── addresses ────────────────────────────────────────────────────────────────

// A plain ASCII address: dot-atom local part, dotted domain, no quotes, comments, display names or IP
// literals. Narrower than the RFC on purpose: everything it accepts is safe to put in a header and an SMTP
// command, and everything real people use passes.
[[nodiscard]] inline auto valid_address(std::string_view a) -> bool {
    if (a.empty() || a.size() > 254) return false;
    const auto at = a.rfind('@');
    if (at == std::string_view::npos || at == 0 || at == a.size() - 1) return false;
    const auto local = a.substr(0, at);
    const auto domain = a.substr(at + 1);
    if (local.size() > 64 || domain.size() > 253) return false;
    constexpr std::string_view atext = "!#$%&'*+-/=?^_`{|}~";
    const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string_view::npos) return false;
    if (!std::ranges::all_of(local, [&](char c) { return alnum(c) || c == '.' || atext.find(c) != std::string_view::npos; })) return false;
    if (std::ranges::count(a, '@') != 1) return false;
    if (domain.find('.') == std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= domain.size()) {
        const auto dot = domain.find('.', start);
        const auto label = domain.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        if (!std::ranges::all_of(label, [&](char c) { return alnum(c) || c == '-'; })) return false;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    const auto tld = domain.substr(domain.rfind('.') + 1);
    return tld.size() >= 2 && !std::ranges::all_of(tld, [](char c) { return c >= '0' && c <= '9'; });
}

// What a person typed, made comparable: surrounding spaces dropped, lower case, and valid or nothing.
[[nodiscard]] inline auto normalize_address(std::string_view raw) -> std::optional<std::string> {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
    std::string out(raw);
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (!valid_address(out)) return std::nullopt;
    return out;
}

// "My App <login@example.com>" or "login@example.com" -> "login@example.com"
[[nodiscard]] inline auto envelope_address(std::string_view from) -> std::optional<std::string> {
    const auto lt = from.rfind('<');
    const auto gt = from.rfind('>');
    if (lt != std::string_view::npos && gt != std::string_view::npos && gt > lt) from = from.substr(lt + 1, gt - lt - 1);
    return normalize_address(from);
}

// ── message ──────────────────────────────────────────────────────────────────

namespace detail {

inline auto base64(std::string_view bytes) -> std::string {
    std::string out(sodium_base64_ENCODED_LEN(bytes.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(),
                      sodium_base64_VARIANT_ORIGINAL);
    out.resize(out.find('\0'));
    return out;
}

inline auto wrapped_base64(std::string_view bytes) -> std::string {
    const auto b = base64(bytes);
    std::string out;
    for (std::size_t i = 0; i < b.size(); i += 76) {
        out += b.substr(i, 76);
        out += "\r\n";
    }
    return out;
}

inline auto has_line_break(std::string_view s) -> bool {
    return s.find_first_of("\r\n") != std::string_view::npos;
}

inline auto random_hex(std::size_t n) -> std::string {
    std::string raw(n, '\0');
    randombytes_buf(raw.data(), raw.size());
    std::string hex(n * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
    hex.pop_back();
    return hex;
}

inline auto http_date(std::chrono::system_clock::time_point tp) -> std::string {
    static constexpr const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d +0000", days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
                  tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

} // namespace detail

// The full RFC 5322 message, CRLF line ends, both bodies base64 so nothing in them can be mistaken for
// protocol (dots, long lines, 8-bit text). Error text when something would put a line break into a header.
[[nodiscard]] inline auto build_mime(const Config& cfg, const Message& m,
                                     std::chrono::system_clock::time_point when = std::chrono::system_clock::now())
    -> std::expected<std::string, std::string> {
    const auto sender = envelope_address(cfg.from);
    if (!sender) return std::unexpected("REVET_MAIL_FROM is not a valid address");
    if (!valid_address(m.to)) return std::unexpected("recipient is not a valid address");
    if (detail::has_line_break(cfg.from) || detail::has_line_break(m.subject)) return std::unexpected("line break in a header");
    if (m.subject.empty() || m.subject.size() > 200) return std::unexpected("subject must be 1 to 200 characters");
    if (sodium_init() < 0) return std::unexpected("libsodium failed to initialise");

    const bool ascii = std::ranges::all_of(m.subject, [](char c) { return static_cast<unsigned char>(c) < 0x80 && static_cast<unsigned char>(c) >= 0x20; });
    const auto subject = ascii ? m.subject : "=?UTF-8?B?" + detail::base64(m.subject) + "?=";
    const auto domain = sender->substr(sender->rfind('@') + 1);
    const auto boundary = "revet-" + detail::random_hex(12);

    std::string out;
    out += "From: " + cfg.from + "\r\n";
    out += "To: " + m.to + "\r\n";
    out += "Subject: " + subject + "\r\n";
    out += "Date: " + detail::http_date(when) + "\r\n";
    out += "Message-ID: <" + detail::random_hex(12) + "@" + domain + ">\r\n";
    out += "MIME-Version: 1.0\r\n";
    if (m.html.empty()) {
        out += "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n";
        out += detail::wrapped_base64(m.text);
    } else {
        out += "Content-Type: multipart/alternative; boundary=\"" + boundary + "\"\r\n\r\n";
        out += "--" + boundary + "\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
               detail::wrapped_base64(m.text);
        out += "--" + boundary + "\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
               detail::wrapped_base64(m.html);
        out += "--" + boundary + "--\r\n";
    }
    return out;
}

// Which TLS library this build of libcurl uses, e.g. "OpenSSL/3.3.2" or "SecureTransport". REVET_SMTP_CA_FILE only
// works with the OpenSSL family; with SecureTransport (the macOS default) trust the CA in the system keychain instead.
[[nodiscard]] auto tls_backend() -> std::string;

// One SMTP delivery through libcurl (src/mail.cpp). False and an explanation on failure.
[[nodiscard]] auto send_smtp(const Config& cfg, const Message& m, std::string& error) -> bool;

// ── background delivery ──────────────────────────────────────────────────────

using Transport = std::function<bool(const Message&, std::string& error)>;

class Outbox {
public:
    // `pause` is the wait before the first retry; it quadruples each time.
    explicit Outbox(Transport transport = nullptr, int attempts = 4, std::chrono::milliseconds pause = std::chrono::seconds(2))
        : transport_(transport ? std::move(transport) : Transport([](const Message& m, std::string& e) { return send_smtp(config(), m, e); })),
          attempts_(attempts),
          pause_(pause) {}

    ~Outbox() {
        {
            const std::scoped_lock lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    Outbox(const Outbox&) = delete;
    Outbox& operator=(const Outbox&) = delete;

    void enqueue(Message m) {
        {
            const std::scoped_lock lock(mu_);
            if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
            queue_.push_back({std::move(m), 0});
        }
        cv_.notify_all();
    }

    // Waits until everything queued has been delivered or given up on. For tests and clean shutdown.
    void drain() {
        std::unique_lock lock(mu_);
        idle_cv_.wait(lock, [this] { return queue_.empty() && !busy_; });
    }

private:
    struct Item {
        Message message;
        int attempts;
    };

    void run() {
        std::unique_lock lock(mu_);
        while (true) {
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stop_) return;
                continue;
            }
            auto item = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
            lock.unlock();

            std::string error;
            bool ok = false;
            try {
                ok = transport_(item.message, error);
            } catch (const std::exception& e) {
                error = e.what();
            }
            ++item.attempts;
            const auto at = item.message.to.rfind('@');
            const auto domain = at == std::string::npos ? std::string("?") : item.message.to.substr(at + 1);
            if (!ok) {
                if (item.attempts < attempts_) {
                    log::warn("mail to @{} failed ({}), will retry", domain, error);
                    auto wait = pause_;
                    for (int i = 1; i < item.attempts; ++i) wait *= 4;
                    lock.lock();
                    const bool stopped = cv_.wait_for(lock, wait, [this] { return stop_; });
                    if (!stopped) queue_.push_front(std::move(item));
                    busy_ = false;
                    idle_cv_.notify_all();
                    continue;
                }
                log::error("mail to @{} given up after {} attempts: {}", domain, item.attempts, error);
            }
            lock.lock();
            busy_ = false;
            idle_cv_.notify_all();
        }
    }

    Transport transport_;
    int attempts_;
    std::chrono::milliseconds pause_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    std::deque<Item> queue_;
    std::thread worker_;
    bool stop_ = false;
    bool busy_ = false;
};

// The process-wide outbox that apps use.
inline auto outbox() -> Outbox& {
    static Outbox box;
    return box;
}

} // namespace web::mail

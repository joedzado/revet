// A tiny SMTP server for tests: accepts connections on a local port, speaks just enough SMTP for
// libcurl, and records each message (envelope, body, and whether it arrived over TLS). Can refuse
// connections, require a login, and offer implicit TLS (smtps) or STARTTLS with a self-signed
// certificate for "localhost" that it writes to a file the client can be told to trust.
#pragma once

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <sodium.h>

namespace smtp_sink {

struct Received {
    std::string mail_from;
    std::vector<std::string> rcpt_to;
    std::string data; // the message as sent, dot-unstuffed, without the closing "."
    bool over_tls = false;
    std::string login; // "user:password" if the client authenticated
};

enum class Tls { none, implicit, starttls };

class Server {
public:
    explicit Server(Tls tls = Tls::none, bool require_login = false) : tls_(tls), require_login_(require_login) {
        if (tls_ != Tls::none) make_tls();
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listener_, 8);
        socklen_t len = sizeof(addr);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~Server() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        close(listener_);
        if (ctx_) SSL_CTX_free(ctx_);
        if (!ca_path_.empty()) std::remove(ca_path_.c_str());
    }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    [[nodiscard]] auto url() const -> std::string {
        if (tls_ == Tls::implicit) return "smtps://localhost:" + std::to_string(port_);
        return (tls_ == Tls::starttls ? "smtp://localhost:" : "smtp://127.0.0.1:") + std::to_string(port_);
    }
    [[nodiscard]] auto port() const -> int { return port_; }
    [[nodiscard]] auto ca_file() const -> const std::string& { return ca_path_; } // trust this to accept the server's certificate

    // The next `n` connections get "421 try later" instead of a greeting.
    void refuse_next(int n) { refuse_ = n; }

    [[nodiscard]] auto messages() -> std::vector<Received> {
        const std::scoped_lock lock(mu_);
        return received_;
    }
    [[nodiscard]] auto connections() const -> int { return connections_; }
    [[nodiscard]] auto tls_handshakes() const -> int { return handshakes_; }

private:
    struct Conn {
        int fd;
        SSL* ssl = nullptr;
        std::string buf;

        auto fill() -> bool {
            char tmp[4096];
            const auto n = ssl != nullptr ? SSL_read(ssl, tmp, sizeof(tmp)) : recv(fd, tmp, sizeof(tmp), 0);
            if (n <= 0) return false;
            buf.append(tmp, static_cast<std::size_t>(n));
            return true;
        }
        auto line(std::string& out) -> bool {
            while (buf.find("\r\n") == std::string::npos) {
                if (!fill()) return false;
            }
            const auto eol = buf.find("\r\n");
            out = buf.substr(0, eol);
            buf.erase(0, eol + 2);
            return true;
        }
        void say(const std::string& s) {
            if (ssl != nullptr) SSL_write(ssl, s.data(), static_cast<int>(s.size()));
            else send(fd, s.data(), s.size(), 0);
        }
    };

    void make_tls() {
        EVP_PKEY* key = EVP_EC_gen("P-256");
        X509* cert = X509_new();
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
        X509_gmtime_adj(X509_getm_notAfter(cert), 86400);
        X509_set_pubkey(cert, key);
        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
        X509_set_issuer_name(cert, name);
        X509V3_CTX v3;
        X509V3_set_ctx_nodb(&v3);
        X509V3_set_ctx(&v3, cert, cert, nullptr, nullptr, 0);
        for (const auto& [nid, value] : {std::pair<int, const char*>{NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1"},
                                         {NID_basic_constraints, "critical,CA:TRUE"}}) {
            X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &v3, nid, value);
            X509_add_ext(cert, ext, -1);
            X509_EXTENSION_free(ext);
        }
        X509_sign(cert, key, EVP_sha256());

        ca_path_ = "/tmp/revet_smtp_sink_" + std::to_string(getpid()) + "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".pem";
        FILE* f = std::fopen(ca_path_.c_str(), "w");
        PEM_write_X509(f, cert);
        std::fclose(f);

        ctx_ = SSL_CTX_new(TLS_server_method());
        SSL_CTX_use_certificate(ctx_, cert);
        SSL_CTX_use_PrivateKey(ctx_, key);
        X509_free(cert);
        EVP_PKEY_free(key);
    }

    auto upgrade(Conn& c) -> bool {
        c.ssl = SSL_new(ctx_);
        SSL_set_fd(c.ssl, c.fd);
        if (SSL_accept(c.ssl) != 1) {
            SSL_free(c.ssl);
            c.ssl = nullptr;
            return false;
        }
        ++handshakes_;
        return true;
    }

    void handle(int fd) {
        ++connections_;
        timeval tv{5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        Conn c{fd};
        if (tls_ == Tls::implicit && !upgrade(c)) return;
        if (refuse_ > 0) {
            --refuse_;
            c.say("421 try later\r\n");
            return;
        }
        c.say("220 sink ready\r\n");
        Received cur;
        bool logged_in = !require_login_;
        std::string login;
        std::string line;
        while (c.line(line)) {
            auto starts = [&](const char* p) { return line.size() >= std::strlen(p) && strncasecmp(line.c_str(), p, std::strlen(p)) == 0; };
            if (starts("EHLO") || starts("HELO")) {
                std::string r = "250-sink\r\n250-8BITMIME\r\n";
                if (tls_ == Tls::starttls && c.ssl == nullptr) r += "250-STARTTLS\r\n";
                r += require_login_ || true ? "250 AUTH PLAIN LOGIN\r\n" : "250 OK\r\n";
                c.say(r);
            } else if (starts("STARTTLS") && tls_ == Tls::starttls && c.ssl == nullptr) {
                c.say("220 go ahead\r\n");
                if (!upgrade(c)) return;
                c.buf.clear();
            } else if (starts("AUTH PLAIN")) {
                auto arg = line.size() > 11 ? line.substr(11) : std::string();
                if (arg.empty()) {
                    c.say("334 \r\n");
                    if (!c.line(arg)) return;
                }
                std::string raw(arg.size(), '\0');
                std::size_t n = 0;
                sodium_base642bin(reinterpret_cast<unsigned char*>(raw.data()), raw.size(), arg.data(), arg.size(), nullptr, &n, nullptr,
                                  sodium_base64_VARIANT_ORIGINAL);
                raw.resize(n); // \0user\0password
                const auto p = raw.find('\0', 1);
                login = p == std::string::npos ? "" : raw.substr(1, p - 1) + ":" + raw.substr(p + 1);
                logged_in = true;
                c.say("235 ok\r\n");
            } else if (starts("AUTH LOGIN")) {
                c.say("334 VXNlcm5hbWU6\r\n");
                std::string u, p;
                if (!c.line(u)) return;
                c.say("334 UGFzc3dvcmQ6\r\n");
                if (!c.line(p)) return;
                auto dec = [](const std::string& b) {
                    std::string raw(b.size(), '\0');
                    std::size_t n = 0;
                    sodium_base642bin(reinterpret_cast<unsigned char*>(raw.data()), raw.size(), b.data(), b.size(), nullptr, &n, nullptr,
                                      sodium_base64_VARIANT_ORIGINAL);
                    raw.resize(n);
                    return raw;
                };
                login = dec(u) + ":" + dec(p);
                logged_in = true;
                c.say("235 ok\r\n");
            } else if (starts("MAIL FROM:")) {
                if (!logged_in) { c.say("530 authentication required\r\n"); continue; }
                cur = {};
                cur.mail_from = line.substr(10);
                cur.over_tls = c.ssl != nullptr;
                cur.login = login;
                c.say("250 ok\r\n");
            } else if (starts("RCPT TO:")) {
                cur.rcpt_to.push_back(line.substr(8));
                c.say("250 ok\r\n");
            } else if (starts("DATA")) {
                c.say("354 go ahead\r\n");
                std::string body = c.buf;
                c.buf.clear();
                while (body.find("\r\n.\r\n") == std::string::npos) {
                    if (!c.fill()) return;
                    body += c.buf;
                    c.buf.clear();
                }
                body.resize(body.find("\r\n.\r\n") + 2);
                std::string plain;
                for (std::size_t i = 0; i < body.size(); ++i) {
                    if (body[i] == '.' && (i == 0 || body[i - 1] == '\n') && i + 1 < body.size() && body[i + 1] == '.') continue;
                    plain.push_back(body[i]);
                }
                cur.data = plain;
                {
                    const std::scoped_lock lock(mu_);
                    received_.push_back(cur);
                }
                c.say("250 queued\r\n");
            } else if (starts("QUIT")) {
                c.say("221 bye\r\n");
                break;
            } else if (starts("RSET") || starts("NOOP")) {
                c.say("250 ok\r\n");
            } else {
                c.say("502 not implemented\r\n");
            }
        }
        if (c.ssl != nullptr) {
            SSL_shutdown(c.ssl);
            SSL_free(c.ssl);
        }
    }

    void serve() {
        while (!stop_) {
            pollfd p{listener_, POLLIN, 0};
            if (poll(&p, 1, 100) <= 0) continue;
            const int fd = accept(listener_, nullptr, nullptr);
            if (fd < 0) continue;
            handle(fd);
            close(fd);
        }
    }

    Tls tls_;
    bool require_login_;
    SSL_CTX* ctx_ = nullptr;
    std::string ca_path_;
    int listener_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<int> refuse_{0};
    std::atomic<int> connections_{0};
    std::atomic<int> handshakes_{0};
    std::mutex mu_;
    std::vector<Received> received_;
};

} // namespace smtp_sink

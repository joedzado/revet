// Mail: address rules, message building, delivery through libcurl to a local SMTP sink, and the
// retrying outbox.
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <random>
#include <revet/mail.hpp>

#include "smtp_sink.hpp"

namespace mail = web::mail;
using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

// OpenSSL-backed libcurl can SIGPIPE on a closed SMTP socket; web::serve already ignores it.
const auto ignore_sigpipe = (std::signal(SIGPIPE, SIG_IGN), 0);

mail::Config cfg_for(const std::string& url) {
    mail::Config c;
    c.smtp_url = url;
    c.from = "My App <login@app.example.com>";
    c.timeout_seconds = 5;
    return c;
}

auto decode_b64(std::string text) -> std::string {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
    std::string out(text.size(), '\0');
    std::size_t len = 0;
    REQUIRE(sodium_base642bin(reinterpret_cast<unsigned char*>(out.data()), out.size(), text.data(), text.size(),
                              nullptr, &len, nullptr, sodium_base64_VARIANT_ORIGINAL) == 0);
    out.resize(len);
    return out;
}

} // namespace

TEST_CASE("mail: ordinary addresses are valid", "[mail]") {
    for (const char* a : {"ann@example.com", "a.b+tag@sub.example.co.uk", "x_y-z@ex-ample.org", "o'brien@example.ie",
                          "1234@example.net", "a@b.cd", "UPPER@Example.COM", "user.name@example-domain.com"}) {
        INFO(a);
        REQUIRE(mail::valid_address(a));
    }
}

TEST_CASE("mail: anything that could break a header or an SMTP command is invalid", "[mail]") {
    for (const std::string a : {""s,
                                "ann"s,
                                "@example.com"s,
                                "ann@"s,
                                "ann@example"s,
                                "ann@@example.com"s,
                                "a@b@example.com"s,
                                "ann@example.com\r\nBcc: evil@example.com"s,
                                "ann@example.com\nX: y"s,
                                "ann@exam ple.com"s,
                                " ann@example.com"s,
                                "ann@example.com "s,
                                "\"ann\"@example.com"s,
                                "<ann@example.com>"s,
                                "ann,bob@example.com"s,
                                "ann;bob@example.com"s,
                                "ann@example.com,bob@example.com"s,
                                ".ann@example.com"s,
                                "ann.@example.com"s,
                                "a..b@example.com"s,
                                "ann@-example.com"s,
                                "ann@example-.com"s,
                                "ann@example..com"s,
                                "ann@.example.com"s,
                                "ann@example.com."s,
                                "ann@exa_mple.com"s,
                                "ann@[127.0.0.1]"s,
                                "ann@127.0.0.1"s,
                                "ann@example.1"s,
                                "ann@example.12"s,
                                "ünï@example.com"s,
                                "ann@exämple.com"s,
                                "ann@example.com\0"s,
                                "ann\t@example.com"s,
                                "ann@example.c"s,
                                std::string(65, 'a') + "@example.com",
                                "ann@" + std::string(64, 'a') + ".com",
                                "a@" + std::string(250, 'b') + ".com"}) {
        INFO(a);
        REQUIRE_FALSE(mail::valid_address(a));
    }
}

TEST_CASE("mail: normalising what a person typed", "[mail]") {
    REQUIRE(mail::normalize_address("  Ann@Example.COM \t") == "ann@example.com");
    REQUIRE(mail::normalize_address("ann@example.com") == "ann@example.com");
    REQUIRE_FALSE(mail::normalize_address("").has_value());
    REQUIRE_FALSE(mail::normalize_address("   ").has_value());
    REQUIRE_FALSE(mail::normalize_address("ann").has_value());
    REQUIRE_FALSE(mail::normalize_address("ann@example.com\r\n").has_value()); // only spaces and tabs are trimmed
}

TEST_CASE("mail: the envelope sender is the address inside the angle brackets", "[mail]") {
    REQUIRE(mail::envelope_address("My App <Login@App.Example.com>") == "login@app.example.com");
    REQUIRE(mail::envelope_address("login@app.example.com") == "login@app.example.com");
    REQUIRE_FALSE(mail::envelope_address("My App").has_value());
    REQUIRE_FALSE(mail::envelope_address("<>").has_value());
}

TEST_CASE("mail: building a message", "[mail]") {
    const auto cfg = cfg_for("smtp://127.0.0.1:1");
    const mail::Message m{.to = "ann@example.com",
                          .subject = "Your sign-in link",
                          .text = "Line one\n.\nhttps://x.example/auth?t=abc",
                          .html = "<p>Hi \xc3\xa9</p>"};
    const auto when = std::chrono::system_clock::time_point{std::chrono::seconds(1'800'000'000)};
    const auto raw = mail::build_mime(cfg, m, when);
    REQUIRE(raw.has_value());
    const auto& s = *raw;

    REQUIRE(
        s.starts_with("From: My App <login@app.example.com>\r\nTo: ann@example.com\r\nSubject: Your sign-in link\r\n"));
    REQUIRE(s.find("Date: Fri, 15 Jan 2027 08:00:00 +0000\r\n") != std::string::npos);
    REQUIRE(s.find("Message-ID: <") != std::string::npos);
    REQUIRE(s.find("@app.example.com>\r\n") != std::string::npos);
    REQUIRE(s.find("MIME-Version: 1.0\r\n") != std::string::npos);
    REQUIRE(s.find("multipart/alternative") != std::string::npos);

    // No bare line feeds anywhere, and no line longer than the SMTP limit.
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            REQUIRE((i > 0 && s[i - 1] == '\r'));
        }
    }
    std::size_t start = 0;
    while (start < s.size()) {
        const auto end = s.find("\r\n", start);
        REQUIRE(end - start <= 998);
        start = end + 2;
    }
    // The bodies come back out of base64 unchanged, including the lone dot and the UTF-8.
    const auto text_at = s.find("charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n") +
                         std::string("charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n").size();
    REQUIRE(decode_b64(s.substr(text_at, s.find("--revet-", text_at) - text_at)) == m.text);
    const auto html_marker = std::string("text/html; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n");
    const auto html_at = s.find(html_marker) + html_marker.size();
    REQUIRE(decode_b64(s.substr(html_at, s.find("--revet-", html_at) - html_at)) == m.html);
}

TEST_CASE("mail: text only messages are single part", "[mail]") {
    const auto raw =
        mail::build_mime(cfg_for("smtp://127.0.0.1:1"), {.to = "ann@example.com", .subject = "Hi", .text = "plain"});
    REQUIRE(raw.has_value());
    REQUIRE(raw->find("multipart") == std::string::npos);
    REQUIRE(raw->find("Content-Type: text/plain; charset=UTF-8") != std::string::npos);
}

TEST_CASE("mail: non-ASCII subjects are encoded", "[mail]") {
    const auto raw = mail::build_mime(cfg_for("smtp://127.0.0.1:1"),
                                      {.to = "ann@example.com", .subject = "Caf\xc3\xa9", .text = "x"});
    REQUIRE(raw.has_value());
    REQUIRE(raw->find("Subject: =?UTF-8?B?Q2Fmw6k=?=\r\n") != std::string::npos);
}

TEST_CASE("mail: nothing that could inject a header is built", "[mail]") {
    const auto cfg = cfg_for("smtp://127.0.0.1:1");
    REQUIRE_FALSE(
        mail::build_mime(cfg, {.to = "ann@example.com\r\nBcc: x@y.com", .subject = "s", .text = "t"}).has_value());
    REQUIRE_FALSE(
        mail::build_mime(cfg, {.to = "ann@example.com", .subject = "s\r\nBcc: x@y.com", .text = "t"}).has_value());
    REQUIRE_FALSE(
        mail::build_mime(cfg, {.to = "ann@example.com", .subject = "s\nBcc: x@y.com", .text = "t"}).has_value());
    REQUIRE_FALSE(mail::build_mime(cfg, {.to = "ann@example.com", .subject = "", .text = "t"}).has_value());
    REQUIRE_FALSE(
        mail::build_mime(cfg, {.to = "ann@example.com", .subject = std::string(300, 'x'), .text = "t"}).has_value());
    auto bad_from = cfg;
    bad_from.from = "App <a@b.com>\r\nBcc: x@y.com";
    REQUIRE_FALSE(mail::build_mime(bad_from, {.to = "ann@example.com", .subject = "s", .text = "t"}).has_value());
    bad_from.from = "";
    REQUIRE_FALSE(mail::build_mime(bad_from, {.to = "ann@example.com", .subject = "s", .text = "t"}).has_value());
    // Whatever is in the body, it stays in the body.
    const auto raw = mail::build_mime(
        cfg, {.to = "ann@example.com", .subject = "s", .text = "x\r\nBcc: x@y.com\r\n\r\n.\r\nQUIT\r\n"});
    REQUIRE(raw.has_value());
    REQUIRE(raw->find("Bcc:") == std::string::npos);
    REQUIRE(raw->find("QUIT") == std::string::npos);
}

TEST_CASE("mail: delivery through libcurl to an SMTP server", "[mail]") {
    smtp_sink::Server sink;
    std::string error;
    REQUIRE(mail::send_smtp(
        cfg_for(sink.url()),
        {.to = "ann@example.com", .subject = "Your link", .text = "Go: https://x.example/a?t=1\n.\nend"}, error));
    REQUIRE(error.empty());
    const auto got = sink.messages();
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].mail_from == "<login@app.example.com>");
    REQUIRE(got[0].rcpt_to == std::vector<std::string>{"<ann@example.com>"});
    REQUIRE(got[0].data.find("To: ann@example.com\r\n") != std::string::npos);
    REQUIRE(got[0].data.find("Subject: Your link\r\n") != std::string::npos);
    const auto body_at = got[0].data.find("\r\n\r\n") + 4;
    REQUIRE(decode_b64(got[0].data.substr(body_at)) == "Go: https://x.example/a?t=1\n.\nend");
}

TEST_CASE("mail: delivery failures are reported, not thrown", "[mail]") {
    std::string error;
    REQUIRE_FALSE(mail::send_smtp(cfg_for("smtp://127.0.0.1:1"), {.to = "ann@example.com", .subject = "s", .text = "t"},
                                  error)); // nothing listens
    REQUIRE_FALSE(error.empty());

    mail::Config none;
    REQUIRE_FALSE(mail::send_smtp(none, {.to = "ann@example.com", .subject = "s", .text = "t"}, error));
    REQUIRE(error == "REVET_SMTP_URL is not set");

    auto wrong_scheme = cfg_for("http://127.0.0.1:1");
    REQUIRE_FALSE(mail::send_smtp(wrong_scheme, {.to = "ann@example.com", .subject = "s", .text = "t"}, error));
    REQUIRE(error.find("smtp://") != std::string::npos);

    smtp_sink::Server sink;
    sink.refuse_next(1);
    REQUIRE_FALSE(mail::send_smtp(cfg_for(sink.url()), {.to = "ann@example.com", .subject = "s", .text = "t"}, error));
    REQUIRE(sink.messages().empty());

    REQUIRE_FALSE(mail::send_smtp(cfg_for(sink.url()), {.to = "not an address", .subject = "s", .text = "t"}, error));
    REQUIRE(sink.messages().empty());
}

TEST_CASE("mail: the outbox delivers in order and drain waits for it", "[mail]") {
    std::vector<std::string> sent;
    std::mutex mu;
    mail::Outbox box([&](const mail::Message& m, std::string&) {
        const std::scoped_lock l(mu);
        sent.push_back(m.subject);
        return true;
    });
    for (int i = 0; i < 20; ++i) {
        box.enqueue({.to = "ann@example.com", .subject = "m" + std::to_string(i), .text = "t"});
    }
    box.drain();
    REQUIRE(sent.size() == 20);
    for (int i = 0; i < 20; ++i) {
        REQUIRE(sent[static_cast<std::size_t>(i)] == "m" + std::to_string(i));
    }
}

TEST_CASE("mail: a failing send is retried and then succeeds", "[mail]") {
    std::atomic<int> calls{0};
    mail::Outbox box(
        [&](const mail::Message&, std::string& e) {
            if (++calls < 3) {
                e = "boom";
                return false;
            }
            return true;
        },
        4, 1ms);
    box.enqueue({.to = "ann@example.com", .subject = "s", .text = "t"});
    box.drain();
    REQUIRE(calls == 3);
}

TEST_CASE("mail: a message that never sends is given up on", "[mail]") {
    std::atomic<int> calls{0};
    mail::Outbox box(
        [&](const mail::Message&, std::string& e) {
            ++calls;
            e = "down";
            return false;
        },
        3, 1ms);
    box.enqueue({.to = "ann@example.com", .subject = "s", .text = "t"});
    box.enqueue({.to = "bob@example.com", .subject = "s", .text = "t"});
    box.drain();
    REQUIRE(calls == 6); // 3 attempts each, and the second message was not stuck behind the first forever
}

TEST_CASE("mail: a transport that throws does not kill the worker", "[mail]") {
    std::atomic<int> calls{0};
    mail::Outbox box(
        [&](const mail::Message&, std::string&) -> bool {
            if (++calls == 1) {
                throw std::runtime_error("oops");
            }
            return true;
        },
        3, 1ms);
    box.enqueue({.to = "ann@example.com", .subject = "s", .text = "t"});
    box.drain();
    REQUIRE(calls == 2);
}

TEST_CASE("mail: the outbox retries through a real server that refuses at first", "[mail]") {
    smtp_sink::Server sink;
    sink.refuse_next(2);
    const auto cfg = cfg_for(sink.url());
    mail::Outbox box([&](const mail::Message& m, std::string& e) { return mail::send_smtp(cfg, m, e); }, 4, 5ms);
    box.enqueue({.to = "ann@example.com", .subject = "Hello", .text = "retry me"});
    box.drain();
    REQUIRE(sink.connections() == 3);
    REQUIRE(sink.messages().size() == 1);
}

TEST_CASE("mail: destroying an outbox with work pending does not hang", "[mail]") {
    {
        mail::Outbox box(
            [](const mail::Message&, std::string& e) {
                e = "down";
                return false;
            },
            10, 10s);
        box.enqueue({.to = "ann@example.com", .subject = "s", .text = "t"});
        std::this_thread::sleep_for(50ms);
    }
    SUCCEED();
}

TEST_CASE("mail: plain SMTP is allowed only to this machine or by choice", "[mail][tls]") {
    mail::Config c;
    c.smtp_url = "smtp://localhost:25";
    REQUIRE(mail::plaintext_allowed(c));
    c.smtp_url = "smtp://127.0.0.1:2525";
    REQUIRE(mail::plaintext_allowed(c));
    c.smtp_url = "smtp://[::1]:25";
    REQUIRE(mail::plaintext_allowed(c));
    for (const char* url : {"smtp://mail.example.com:587", "smtp://localhost.evil.example.com:25",
                            "smtp://127.0.0.1.evil.com:25", "smtp://10.0.0.5:25"}) {
        c.smtp_url = url;
        INFO(url);
        c.allow_plaintext = false;
        REQUIRE_FALSE(mail::plaintext_allowed(c));
        c.allow_plaintext = true;
        REQUIRE(mail::plaintext_allowed(c));
    }
}

TEST_CASE("mail: a server certificate that is not trusted is refused", "[mail][tls]") {
    // The same self-signed certificate over both kinds of TLS. With no CA configured, delivery must fail
    // and the message must not be sent in the clear instead.
    for (const auto mode : {smtp_sink::Tls::implicit, smtp_sink::Tls::starttls}) {
        smtp_sink::Server sink(mode);
        std::string error;
        auto c = cfg_for(sink.url());
        REQUIRE_FALSE(mail::send_smtp(
            c, {.to = "ann@example.com", .subject = "secret link", .text = "https://x.example/a?t=SECRET"}, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(sink.messages().empty());
    }
}

TEST_CASE("mail: delivery over TLS to a server with a trusted certificate", "[mail][tls]") {
    if (mail::tls_backend().find("SecureTransport") != std::string::npos) {
        SKIP("this libcurl uses SecureTransport, which ignores REVET_SMTP_CA_FILE and trusts only the system keychain");
    }
    for (const auto mode : {smtp_sink::Tls::implicit, smtp_sink::Tls::starttls}) {
        smtp_sink::Server sink(mode);
        auto c = cfg_for(sink.url());
        c.ca_file = sink.ca_file();
        std::string error;
        const bool ok = mail::send_smtp(c, {.to = "ann@example.com", .subject = "Hi", .text = "over tls"}, error);
        INFO(error);
        REQUIRE(ok);
        const auto got = sink.messages();
        REQUIRE(got.size() == 1);
        REQUIRE(got[0].over_tls);
        REQUIRE(sink.tls_handshakes() == 1);
    }
}

TEST_CASE("mail: the login reaches the server", "[mail]") {
    smtp_sink::Server sink(smtp_sink::Tls::none, /*require_login=*/true); // local, so plain text is allowed
    auto c = cfg_for(sink.url());
    c.username = "app-login";
    c.password = "s3cret:with:colons";
    std::string error;
    const bool ok = mail::send_smtp(c, {.to = "ann@example.com", .subject = "Hi", .text = "x"}, error);
    INFO(error);
    REQUIRE(ok);
    const auto got = sink.messages();
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].login == "app-login:s3cret:with:colons");

    // Without the login the server refuses the mail, and nothing is recorded.
    smtp_sink::Server strict(smtp_sink::Tls::none, true);
    REQUIRE_FALSE(
        mail::send_smtp(cfg_for(strict.url()), {.to = "ann@example.com", .subject = "Hi", .text = "x"}, error));
    REQUIRE(strict.messages().empty());
}

TEST_CASE("mail: random text never yields an address that could break a header", "[mail][fuzz]") {
    std::mt19937_64 rng(0xa11ce);
    const std::string alphabet = "abcXYZ019@.-_+'!#$%&*/=?^`{|}~ \t\r\n\0\"<>,;:()[]\\\x80\xff";
    int accepted = 0;
    for (int i = 0; i < 200000; ++i) {
        std::string s;
        const auto n = 1 + rng() % 40;
        for (std::size_t k = 0; k < n; ++k) {
            s.push_back(alphabet[rng() % alphabet.size()]);
        }
        if (const auto a = mail::normalize_address(s)) {
            ++accepted;
            REQUIRE(mail::valid_address(*a));
            REQUIRE(a->find_first_of("\r\n\0 \t\"<>,;:()[]\\") == std::string::npos);
            REQUIRE(std::count(a->begin(), a->end(), '@') == 1);
            REQUIRE(std::all_of(a->begin(), a->end(), [](unsigned char c) { return c < 0x80 && c > 0x20; }));
            const auto mime = mail::build_mime(cfg_for("smtp://127.0.0.1:1"), {.to = *a, .subject = "s", .text = "t"});
            REQUIRE(mime.has_value());
            // Exactly one To header, and no extra header lines.
            REQUIRE(mime->find("\r\nTo: " + *a + "\r\n") != std::string::npos);
        }
    }
    SUCCEED("accepted " << accepted);
}

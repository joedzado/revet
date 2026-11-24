// SMTP delivery through libcurl. The curl headers stay in this file.
#include <algorithm>
#include <array>
#include <cstring>
#include <curl/curl.h>
#include <revet/mail.hpp>

namespace web::mail {
namespace {

struct Upload {
    const std::string* data{};
    std::size_t sent = 0;
};

auto read_callback(char* buffer, std::size_t size, std::size_t nitems, void* userp) -> std::size_t {
    auto* up = static_cast<Upload*>(userp);
    const auto room = size * nitems;
    const auto n = std::min(room, up->data->size() - up->sent);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic,bugprone-not-null-terminated-result): curl buffer
    std::memcpy(buffer, up->data->data() + up->sent, n);
    up->sent += n;
    return n;
}

} // namespace

auto tls_backend() -> std::string {
    const auto* info = curl_version_info(CURLVERSION_NOW);
    return info != nullptr && info->ssl_version != nullptr ? info->ssl_version : "";
}

auto send_smtp(const Config& cfg, const Message& m, std::string& error) -> bool {
    if (cfg.smtp_url.empty()) {
        error = "REVET_SMTP_URL is not set";
        return false;
    }
    const bool implicit_tls = cfg.smtp_url.starts_with("smtps://");
    if (!implicit_tls && !cfg.smtp_url.starts_with("smtp://")) {
        error = "REVET_SMTP_URL must start with smtp:// or smtps://";
        return false;
    }
    const bool may_skip_tls = plaintext_allowed(cfg);

    const auto mime = build_mime(cfg, m);
    if (!mime) {
        error = mime.error();
        return false;
    }
    const auto sender = envelope_address(cfg.from);
    if (!sender) {
        error = "REVET_MAIL_FROM is not a valid address";
        return false;
    }

    static const bool curl_ready = [] -> bool { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; }();
    if (!curl_ready) {
        error = "curl failed to initialise";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }
    curl_slist* recipients = curl_slist_append(nullptr, ("<" + m.to + ">").c_str());
    std::array<char, CURL_ERROR_SIZE> errbuf{};
    Upload upload{.data = &*mime};
    const std::string from = "<" + *sender + ">";

    curl_easy_setopt(curl, CURLOPT_URL, cfg.smtp_url.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    if (!cfg.username.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg.password.c_str());
    }
    // smtp:// upgrades with STARTTLS and fails if the server will not; smtps:// is TLS from the first byte.
    // Certificates and host names are verified (curl's default). Plain text only for a local server, or by choice.
    curl_easy_setopt(curl, CURLOPT_USE_SSL,
                     may_skip_tls && !implicit_tls ? static_cast<long>(CURLUSESSL_TRY)
                                                   : static_cast<long>(CURLUSESSL_ALL));
    if (!cfg.ca_file.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, cfg.ca_file.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, cfg.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min<long>(cfg.timeout_seconds, 10));
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf.data());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        error = errbuf.front() != '\0' ? errbuf.data() : curl_easy_strerror(rc);
        return false;
    }
    return true;
}

} // namespace web::mail

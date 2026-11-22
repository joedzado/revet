#include <curl/curl.h>
#include <format>
#include <revet/http_client.hpp>
#include <string>

namespace http {

// ── CurlClient ───────────────────────────────────────────────────────────────

namespace { // NOLINT(google-readability-anonymous-namespace-comments)

auto write_callback(void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

void init_curl_global() {
    static bool initialized = false;
    if (!initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        initialized = true;
    }
}

struct CurlHandle {
    CurlHandle() = default;
    CurlHandle(const CurlHandle&) = delete;
    auto operator=(const CurlHandle&) -> CurlHandle& = delete;
    CurlHandle(CurlHandle&&) = delete;
    auto operator=(CurlHandle&&) -> CurlHandle& = delete;
    CURL* h = curl_easy_init();
    ~CurlHandle() {
        if (h != nullptr) {
            curl_easy_cleanup(h);
        }
    }
    explicit operator bool() const {
        return h != nullptr;
    }
};

struct CurlHeaders {
    CurlHeaders() = default;
    CurlHeaders(const CurlHeaders&) = delete;
    auto operator=(const CurlHeaders&) -> CurlHeaders& = delete;
    CurlHeaders(CurlHeaders&&) = delete;
    auto operator=(CurlHeaders&&) -> CurlHeaders& = delete;
    curl_slist* list = nullptr;
    void append(const std::string& header) {
        list = curl_slist_append(list, header.c_str());
    }
    ~CurlHeaders() {
        if (list != nullptr) {
            curl_slist_free_all(list);
        }
    }
};

} // namespace

auto CurlClient::execute(const std::string& method, const std::string& url,
                         const std::map<std::string, std::string>& headers, const std::string& body) -> Response {
    init_curl_global();

    const CurlHandle curl;
    if (!curl) {
        return Response{.status_code = 0, .body = "Failed to initialize curl", .headers = {}};
    }

    curl_easy_setopt(curl.h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.h, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl.h, CURLOPT_CONNECTTIMEOUT, static_cast<long>(options_.connect_timeout.count()));
    curl_easy_setopt(curl.h, CURLOPT_TIMEOUT, static_cast<long>(options_.total_timeout.count()));

    std::string response_body;
    curl_easy_setopt(curl.h, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.h, CURLOPT_WRITEDATA, &response_body);

    CurlHeaders header_list;
    header_list.append("Content-Type: application/json");
    for (const auto& [key, value] : headers) {
        header_list.append(std::format("{}: {}", key, value));
    }
    curl_easy_setopt(curl.h, CURLOPT_HTTPHEADER, header_list.list);

    if (!body.empty()) {
        curl_easy_setopt(curl.h, CURLOPT_POSTFIELDS, body.c_str());
    }

    const CURLcode res = curl_easy_perform(curl.h);
    long status_code = 0;
    curl_easy_getinfo(curl.h, CURLINFO_RESPONSE_CODE, &status_code);

    if (res != CURLE_OK) {
        // status_code 0 signals a transport failure (DNS, connect, timeout, …)
        // distinct from any real HTTP status the remote could send.
        return Response{
            .status_code = 0, .body = std::format("Curl error: {}", curl_easy_strerror(res)), .headers = {}};
    }

    return Response{.status_code = static_cast<int>(status_code), .body = response_body, .headers = {}};
}

// ── MockClient ───────────────────────────────────────────────────────────────

auto MockClient::execute(const std::string& method, const std::string& url,
                         const std::map<std::string, std::string>& headers, const std::string& body) -> Response {
    // Record call
    call_history_.push_back({.method = method, .url = url, .headers = headers, .body = body});

    // Look up stubbed response
    const std::string key = method + ":" + url;
    const auto it = stubs_.find(key);
    if (it != stubs_.end()) {
        return it->second;
    }

    // Not found
    return Response{.status_code = 404, .body = std::format("No stub for {}", key), .headers = {}};
}

// ── Global client management ──────────────────────────────────────────────────

namespace global {
namespace { // NOLINT(google-readability-anonymous-namespace-comments)
auto client_slot() -> std::shared_ptr<Client>& {
    static std::shared_ptr<Client> client;
    return client;
}
} // namespace

auto default_client() -> Client& {
    if (!client_slot()) {
        client_slot() = std::make_shared<CurlClient>();
    }
    return *client_slot();
}

void set_client(std::shared_ptr<Client> client) {
    client_slot() = std::move(client);
}
} // namespace global

} // namespace http

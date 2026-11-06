#include <curl/curl.h>
#include <format>
#include <revet/http_client.hpp>
#include <string>

namespace http {

// ── CurlClient ───────────────────────────────────────────────────────────────

namespace { // NOLINT(google-readability-anonymous-namespace-comments)

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
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
    CURL* h = curl_easy_init();
    ~CurlHandle() {
        if (h) {
            curl_easy_cleanup(h);
        }
    }
    operator CURL*() const {
        return h;
    }
    explicit operator bool() const {
        return h != nullptr;
    }
};

struct CurlHeaders {
    curl_slist* list = nullptr;
    void append(const std::string& header) {
        list = curl_slist_append(list, header.c_str());
    }
    ~CurlHeaders() {
        if (list) {
            curl_slist_free_all(list);
        }
    }
};

} // namespace

Response CurlClient::execute(const std::string& method, const std::string& url,
                             const std::map<std::string, std::string>& headers, const std::string& body) {
    init_curl_global();

    CurlHandle curl;
    if (!curl) {
        return Response{0, "Failed to initialize curl", {}};
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(options_.connect_timeout.count()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(options_.total_timeout.count()));

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    CurlHeaders header_list;
    header_list.append("Content-Type: application/json");
    for (const auto& [key, value] : headers) {
        header_list.append(std::format("{}: {}", key, value));
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list.list);

    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    if (res != CURLE_OK) {
        // status_code 0 signals a transport failure (DNS, connect, timeout, …)
        // distinct from any real HTTP status the remote could send.
        return Response{0, std::format("Curl error: {}", curl_easy_strerror(res)), {}};
    }

    return Response{static_cast<int>(status_code), response_body, {}};
}

// ── MockClient ───────────────────────────────────────────────────────────────

Response MockClient::execute(const std::string& method, const std::string& url,
                             const std::map<std::string, std::string>& headers, const std::string& body) {
    // Record call
    call_history_.push_back({method, url, headers, body});

    // Look up stubbed response
    const std::string key = method + ":" + url;
    const auto it = stubs_.find(key);
    if (it != stubs_.end()) {
        return it->second;
    }

    // Not found
    return Response{404, std::format("No stub for {}", key), {}};
}

// ── Global client management ──────────────────────────────────────────────────

namespace global {
namespace { // NOLINT(google-readability-anonymous-namespace-comments)
std::shared_ptr<Client> g_client;
} // namespace

Client& default_client() {
    if (!g_client) {
        g_client = std::make_shared<CurlClient>();
    }
    return *g_client;
}

void set_client(std::shared_ptr<Client> client) {
    g_client = std::move(client);
}
} // namespace global

} // namespace http

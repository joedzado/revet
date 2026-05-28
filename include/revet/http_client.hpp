#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace http {

// HTTP response. status_code == 0 means transport failure (DNS, connect,
// timeout, etc.) — distinct from any real HTTP status.
struct Response {
    int status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;

    // True iff the response is a successful HTTP 2xx. Transport failures
    // (status_code == 0) and 4xx/5xx are false.
    bool ok() const { return status_code >= 200 && status_code < 300; }
    explicit operator bool() const { return ok(); }
};

// Thrown by generated API client functions (apis.hpp) on non-2xx responses
// and transport failures. status_code == 0 means transport failure.
class Error : public std::runtime_error {
public:
    Error(int status, std::string body)
        : std::runtime_error(
              status == 0
                  ? ("http transport error: " + body)
                  : ("http error " + std::to_string(status) + ": " + body)),
          status_code(status),
          body(std::move(body)) {}

    int status_code;
    std::string body;
};

// Abstract HTTP client interface
class Client {
public:
    Client() = default;
    virtual ~Client() = default;

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    virtual Response execute(const std::string& method, const std::string& url,
                             const std::map<std::string, std::string>& headers, const std::string& body) = 0;
};

// Production HTTP client using libcurl
class CurlClient final : public Client {
public:
    Response execute(const std::string& method, const std::string& url,
                     const std::map<std::string, std::string>& headers, const std::string& body) override;
};

// Mock HTTP client for testing
class MockClient final : public Client {
public:
    // Register response for URL pattern
    void stub(const std::string& method, const std::string& url_pattern, const Response& response) {
        stubs_[method + ":" + url_pattern] = response;
    }

    // Clear all stubs
    void clear() {
        stubs_.clear();
        call_history_.clear();
    }

    // Get call history for assertions
    const auto& history() const {
        return call_history_;
    }

    Response execute(const std::string& method, const std::string& url,
                     const std::map<std::string, std::string>& headers, const std::string& body) override;

private:
    struct Call {
        std::string method;
        std::string url;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    std::map<std::string, Response> stubs_;
    std::vector<Call> call_history_;
};

// Global client management
namespace global {
Client& default_client();
void set_client(std::shared_ptr<Client> client);
} // namespace global

} // namespace http

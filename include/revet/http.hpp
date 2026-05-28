#pragma once

#include <revet/http_client.hpp>
#include <revet/thread_pool.hpp>
#include <string>

namespace http {

// Awaitable wrapper — executes HTTP call on the I/O pool, frees calling coroutine
template<typename ResultType = Response>
class Awaitable {
public:
    explicit Awaitable(const std::string& method, const std::string& url, Client* client = nullptr)
        : method_(method), url_(url), client_(client != nullptr ? client : &global::default_client()) {}

    bool await_ready() const {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) {
        web::detail::io_submit([this, h]() mutable {
            response_ = client_->execute(method_, url_, {}, "");
            h.resume();
        });
    }

    ResultType await_resume() {
        return response_;
    }

private:
    std::string method_;
    std::string url_;
    Client* client_;
    Response response_;
};

// Helper functions
inline Awaitable<Response> get(const std::string& url, Client* client = nullptr) {
    return Awaitable<Response>("GET", url, client);
}

inline Awaitable<Response> post(const std::string& url, Client* client = nullptr) {
    return Awaitable<Response>("POST", url, client);
}

inline Awaitable<Response> put(const std::string& url, Client* client = nullptr) {
    return Awaitable<Response>("PUT", url, client);
}

inline Awaitable<Response> delete_(const std::string& url, Client* client = nullptr) {
    return Awaitable<Response>("DELETE", url, client);
}

inline Awaitable<Response> patch(const std::string& url, Client* client = nullptr) {
    return Awaitable<Response>("PATCH", url, client);
}

inline Awaitable<Response> head(const std::string& url, Client* client = nullptr) {
    return Awaitable<Response>("HEAD", url, client);
}

inline Awaitable<Response> options(const std::string& url, Client* client = nullptr) {
    return Awaitable<Response>("OPTIONS", url, client);
}

} // namespace http

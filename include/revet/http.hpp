#pragma once

#include <revet/http_client.hpp>
#include <revet/thread_pool.hpp>
#include <string>
#include <utility>

namespace http {

// Awaitable wrapper — executes HTTP call on the I/O pool, frees calling coroutine
template<typename ResultType = Response>
class Awaitable {
public:
    explicit Awaitable(std::string method, std::string url, Client* client = nullptr)
        : method_(std::move(method)), url_(std::move(url)),
          client_(client != nullptr ? client : &global::default_client()) {}

    [[nodiscard]] auto await_ready() const -> bool {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) {
        web::detail::io_submit([this, h]() mutable -> auto {
            response_ = client_->execute(method_, url_, {}, "");
            h.resume();
        });
    }

    auto await_resume() -> ResultType {
        return response_;
    }

private:
    std::string method_;
    std::string url_;
    Client* client_;
    Response response_;
};

// Helper functions
inline auto get(const std::string& url, Client* client = nullptr) -> Awaitable<Response> {
    return Awaitable<Response>("GET", url, client);
}

inline auto post(const std::string& url, Client* client = nullptr) -> Awaitable<Response> {
    return Awaitable<Response>("POST", url, client);
}

inline auto put(const std::string& url, Client* client = nullptr) -> Awaitable<Response> {
    return Awaitable<Response>("PUT", url, client);
}

inline auto delete_(const std::string& url, Client* client = nullptr) -> Awaitable<Response> {
    return Awaitable<Response>("DELETE", url, client);
}

inline auto patch(const std::string& url, Client* client = nullptr) -> Awaitable<Response> {
    return Awaitable<Response>("PATCH", url, client);
}

inline auto head(const std::string& url, Client* client = nullptr) -> Awaitable<Response> {
    return Awaitable<Response>("HEAD", url, client);
}

inline auto options(const std::string& url, Client* client = nullptr) -> Awaitable<Response> {
    return Awaitable<Response>("OPTIONS", url, client);
}

} // namespace http

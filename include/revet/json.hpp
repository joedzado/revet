#pragma once

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "json_write.hpp"

namespace revet::json {

// Exception for JSON errors
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

// Forward declarations - simdjson types not exposed in public API
namespace simdjson_types {
struct ValueImpl;
struct DocumentImpl;
} // namespace simdjson_types

// Wrapper around simdjson::ondemand for clean API
class Value {
public:
    Value(const Value& other);
    auto operator=(const Value& other) -> Value&;
    Value(Value&& other) noexcept;
    auto operator=(Value&& other) noexcept -> Value&;
    ~Value();

    // Field access by key
    auto at(std::string_view key) -> Value;

    // Array access by index
    auto at(size_t index) -> Value;

    // Type conversions
    operator std::string();
    operator std::string_view();
    operator int32_t();
    operator int64_t();
    operator uint32_t();
    operator uint64_t();
    operator float();
    operator double();
    operator bool();

    // Optional conversions
    template<typename T>
    operator std::optional<T>() {
        try {
            if (is_null()) {
                return std::nullopt;
            }
            auto val = static_cast<T>(*this);
            return std::optional<T>(val);
        } catch (...) {
            return std::nullopt;
        }
    }

    // Array conversion
    template<typename T>
    operator std::vector<T>() {
        std::vector<T> result;
        auto elems = get_array_elements();
        std::ranges::transform(elems, std::back_inserter(result), [](auto& e) -> auto { return static_cast<T>(e); });
        return result;
    }

    // Check if field exists
    auto has(std::string_view key) -> bool;

    // The field converted to T, or `fallback` if it is missing, null, or the wrong type.
    template<typename T>
    auto get(std::string_view key, T fallback = T{}) -> T {
        try {
            if (!has(key)) {
                return fallback;
            }
            Value v = at(key);
            if (v.is_null()) {
                return fallback;
            }
            return static_cast<T>(v);
        } catch (...) {
            return fallback;
        }
    }
    auto get(std::string_view key, const char* fallback) -> std::string {
        return get<std::string>(key, std::string(fallback));
    }

    // Check if null
    auto is_null() -> bool;

private:
    friend class Document;

    std::shared_ptr<simdjson_types::ValueImpl> impl_;

    explicit Value(std::shared_ptr<simdjson_types::ValueImpl> impl);

    auto get_array_elements() -> std::vector<Value>;
};

// Parse JSON string — move-only (not copyable; the underlying parser is uniquely owned)
class Document {
public:
    explicit Document(std::string_view json_str);
    ~Document();
    Document(Document&&) noexcept = default;
    auto operator=(Document&&) noexcept -> Document& = default;
    Document(const Document&) = delete;
    auto operator=(const Document&) -> Document& = delete;

    auto root() -> Value;

    auto at(std::string_view key) -> Value;
    auto operator[](std::string_view key) -> Value;

private:
    std::unique_ptr<simdjson_types::DocumentImpl> impl_;
};

} // namespace revet::json

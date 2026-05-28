#pragma once

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
    Value& operator=(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(Value&& other) noexcept;
    ~Value();

    // Field access by key
    Value at(std::string_view key);

    // Array access by index
    Value at(size_t index);

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
            T val = static_cast<T>(*this);
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
        std::ranges::transform(elems, std::back_inserter(result), [](auto& e) { return static_cast<T>(e); });
        return result;
    }

    // Check if field exists
    bool has(std::string_view key);

    // Check if null
    bool is_null();

private:
    friend class Document;

    std::shared_ptr<simdjson_types::ValueImpl> impl_;

    explicit Value(std::shared_ptr<simdjson_types::ValueImpl> impl);

    std::vector<Value> get_array_elements();
};

// Parse JSON string — move-only (not copyable; the underlying parser is uniquely owned)
class Document {
public:
    explicit Document(std::string_view json_str);
    ~Document();
    Document(Document&&) noexcept = default;
    Document& operator=(Document&&) noexcept = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Value root();

    Value at(std::string_view key);
    Value operator[](std::string_view key);

private:
    std::unique_ptr<simdjson_types::DocumentImpl> impl_;
};

} // namespace revet::json

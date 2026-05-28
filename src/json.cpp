#include <format>
#include <memory>
#include <revet/json.hpp>
#include <simdjson.h>
#include <string>
#include <utility>

namespace revet::json {

namespace simdjson_types {

struct ValueImpl {
    simdjson::dom::element element;
    std::shared_ptr<simdjson::dom::parser> parser;

    ValueImpl(const simdjson::dom::element& elem, std::shared_ptr<simdjson::dom::parser> p)
        : element(elem), parser(std::move(p)) {}
};

struct DocumentImpl {
    std::shared_ptr<simdjson::dom::parser> parser;
    simdjson::padded_string buf; // must outlive root — element holds pointers into this
    simdjson::dom::element root;

    explicit DocumentImpl(std::string_view json_str)
        : parser(std::make_shared<simdjson::dom::parser>()), buf(json_str.data(), json_str.size()) {
        auto parse_result = parser->parse(buf);
        if (parse_result.error() != simdjson::error_code::SUCCESS) {
            throw ParseError("JSON parse error");
        }
        root = parse_result.value();
    }
};

} // namespace simdjson_types

// Value implementation
Value::Value(std::shared_ptr<simdjson_types::ValueImpl> impl) : impl_(std::move(impl)) {}

Value::Value(const Value& other) = default;

Value& Value::operator=(const Value& other) = default;

Value::Value(Value&& other) noexcept = default;

Value& Value::operator=(Value&& other) noexcept = default;

Value::~Value() = default;

Value Value::at(std::string_view key) {
    auto field = impl_->element.at_key(key);
    if (field.error() != 0U) {
        throw ParseError(std::format("Field not found: {}", key));
    }
    return Value(std::make_shared<simdjson_types::ValueImpl>(field.value(), impl_->parser));
}

Value Value::at(size_t index) {
    auto elem = impl_->element.at(index);
    if (elem.error() != 0U) {
        throw ParseError("Array index out of bounds");
    }
    return Value(std::make_shared<simdjson_types::ValueImpl>(elem.value(), impl_->parser));
}

Value::operator std::string() {
    auto str = impl_->element.get_string();
    if (str.error() != 0U) {
        throw ParseError("Expected string");
    }
    return std::string(str.value());
}

Value::operator std::string_view() {
    auto str = impl_->element.get_string();
    if (str.error() != 0U) {
        throw ParseError("Expected string");
    }
    return str.value();
}

Value::operator int32_t() {
    auto num = impl_->element.get_int64();
    if (num.error() != 0U) {
        throw ParseError("Expected integer");
    }
    return static_cast<int32_t>(num.value());
}

Value::operator int64_t() {
    auto num = impl_->element.get_int64();
    if (num.error() != 0U) {
        throw ParseError("Expected integer");
    }
    return num.value();
}

Value::operator uint32_t() {
    auto num = impl_->element.get_uint64();
    if (num.error() != 0U) {
        throw ParseError("Expected unsigned integer");
    }
    return static_cast<uint32_t>(num.value());
}

Value::operator uint64_t() {
    auto num = impl_->element.get_uint64();
    if (num.error() != 0U) {
        throw ParseError("Expected unsigned integer");
    }
    return num.value();
}

Value::operator float() {
    auto num = impl_->element.get_double();
    if (num.error() != 0U) {
        throw ParseError("Expected floating point");
    }
    return static_cast<float>(num.value());
}

Value::operator double() {
    auto num = impl_->element.get_double();
    if (num.error() != 0U) {
        throw ParseError("Expected floating point");
    }
    return num.value();
}

Value::operator bool() {
    auto b = impl_->element.get_bool();
    if (b.error() != 0U) {
        throw ParseError("Expected boolean");
    }
    return b.value();
}

bool Value::has(std::string_view key) {
    try {
        auto field = impl_->element.at_key(key);
        return field.error() == 0U;
    } catch (...) {
        return false;
    }
}

bool Value::is_null() {
    return impl_->element.is_null();
}

std::vector<Value> Value::get_array_elements() {
    std::vector<Value> result;
    auto arr = impl_->element.get_array();
    if (arr.error() != 0U) {
        throw ParseError("Expected array");
    }
    std::ranges::transform(arr.value(), std::back_inserter(result), [this](auto elem) {
        return Value(std::make_shared<simdjson_types::ValueImpl>(elem, impl_->parser));
    });
    return result;
}

// Document implementation
Document::Document(std::string_view json_str) : impl_(std::make_unique<simdjson_types::DocumentImpl>(json_str)) {}

Document::~Document() = default;

Value Document::root() {
    return Value(std::make_shared<simdjson_types::ValueImpl>(impl_->root, impl_->parser));
}

Value Document::at(std::string_view key) {
    return root().at(key);
}

Value Document::operator[](std::string_view key) {
    return at(key);
}

} // namespace revet::json

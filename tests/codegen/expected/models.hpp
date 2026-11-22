// Auto-generated — do not edit
#pragma once
#include <revet/json.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdio>

#include "../../models/create_user.hpp"
#include "../../models/nested.hpp"
#include "../../models/session.hpp"
#include "../../models/user.hpp"
#include "../../app/apis/usgs.hpp"

namespace revet::json {

namespace detail {
inline std::string quote(std::string_view s) { return ::revet::json::quote(s); }
} // namespace detail

template<typename T> T from_json_value(Value& v);
template<typename T> T from_json(Document& doc);
template<typename T> std::string to_json(const T& val);
template<typename T> T from_row(const std::vector<std::string>& r);

// CreateUser — from create_user.hpp
template<> inline CreateUser from_json_value<CreateUser>([[maybe_unused]] Value& v) {
    CreateUser out{};
    out.name = static_cast<std::string>(v.at("name"));
    out.email = static_cast<std::string>(v.at("email"));
    out.role = v.has("role") ? std::optional<std::string>(static_cast<std::string>(v.at("role"))) : std::nullopt;
    return out;
}

template<> inline CreateUser from_json<CreateUser>([[maybe_unused]] Document& doc) {
    auto root = doc.root();
    return from_json_value<CreateUser>(root);
}

template<> inline CreateUser from_row<CreateUser>([[maybe_unused]] const std::vector<std::string>& r) {
    CreateUser out{};
    out.name = r[0];
    out.email = r[1];
    out.role = (r[2].empty() ? std::optional<std::string>{} : std::optional<std::string>(r[2]));
    return out;
}

template<> inline std::string to_json<CreateUser>([[maybe_unused]] const CreateUser& val) {
    std::string out = "{";
    out += "\"name\":";
    out += revet::json::detail::quote(val.name);
    out += ",\"email\":";
    out += revet::json::detail::quote(val.email);
    out += ",\"role\":";
    if (val.role.has_value()) {
        out += revet::json::detail::quote((*val.role));
    } else {
        out += "null";
    }
    out += "}";
    return out;
}

// shop::catalog::Item — from nested.hpp
template<> inline shop::catalog::Item from_json_value<shop::catalog::Item>([[maybe_unused]] Value& v) {
    shop::catalog::Item out{};
    out.id = static_cast<int64_t>(v.at("id"));
    out.title = static_cast<std::string>(v.at("title"));
    out.price = v.has("price") ? std::optional<double>(static_cast<double>(v.at("price"))) : std::nullopt;
    out.tags = (v.at("tags")).operator std::vector<std::string>();
    return out;
}

template<> inline shop::catalog::Item from_json<shop::catalog::Item>([[maybe_unused]] Document& doc) {
    auto root = doc.root();
    return from_json_value<shop::catalog::Item>(root);
}

template<> inline shop::catalog::Item from_row<shop::catalog::Item>([[maybe_unused]] const std::vector<std::string>& r) {
    shop::catalog::Item out{};
    out.id = static_cast<int64_t>(std::stoll(r[0]));
    out.title = r[1];
    out.price = (r[2].empty() ? std::optional<double>{} : std::optional<double>(std::stod(r[2])));
    out.tags = std::vector<std::string>{};
    return out;
}

template<> inline std::string to_json<shop::catalog::Item>([[maybe_unused]] const shop::catalog::Item& val) {
    std::string out = "{";
    out += "\"id\":";
    out += std::to_string(val.id);
    out += ",\"title\":";
    out += revet::json::detail::quote(val.title);
    out += ",\"price\":";
    if (val.price.has_value()) {
        out += std::to_string((*val.price));
    } else {
        out += "null";
    }
    out += ",\"tags\":";
    out += "[";
    {
        bool first = true;
        for (auto const& _elem : val.tags) {
            if (!first) out += ",";
            first = false;
        out += revet::json::detail::quote(_elem);
        }
    }
    out += "]";
    out += "}";
    return out;
}

// shop::catalog::inner::LineItem — from nested.hpp
template<> inline shop::catalog::inner::LineItem from_json_value<shop::catalog::inner::LineItem>([[maybe_unused]] Value& v) {
    shop::catalog::inner::LineItem out{};
    out.item = static_cast<Item>(v.at("item"));
    out.qty = static_cast<int>(v.at("qty"));
    return out;
}

template<> inline shop::catalog::inner::LineItem from_json<shop::catalog::inner::LineItem>([[maybe_unused]] Document& doc) {
    auto root = doc.root();
    return from_json_value<shop::catalog::inner::LineItem>(root);
}

template<> inline shop::catalog::inner::LineItem from_row<shop::catalog::inner::LineItem>([[maybe_unused]] const std::vector<std::string>& r) {
    shop::catalog::inner::LineItem out{};
    out.item = Item{};
    out.qty = static_cast<int>(std::stol(r[1]));
    return out;
}

template<> inline std::string to_json<shop::catalog::inner::LineItem>([[maybe_unused]] const shop::catalog::inner::LineItem& val) {
    std::string out = "{";
    out += "\"item\":";
    out += std::to_string(val.item);
    out += ",\"qty\":";
    out += std::to_string(val.qty);
    out += "}";
    return out;
}

// User — from user.hpp
template<> inline User from_json_value<User>([[maybe_unused]] Value& v) {
    User out{};
    out.id = static_cast<std::string>(v.at("id"));
    out.name = static_cast<std::string>(v.at("name"));
    out.email = static_cast<std::string>(v.at("email"));
    out.role = v.has("role") ? std::optional<std::string>(static_cast<std::string>(v.at("role"))) : std::nullopt;
    return out;
}

template<> inline User from_json<User>([[maybe_unused]] Document& doc) {
    auto root = doc.root();
    return from_json_value<User>(root);
}

template<> inline User from_row<User>([[maybe_unused]] const std::vector<std::string>& r) {
    User out{};
    out.id = r[0];
    out.name = r[1];
    out.email = r[2];
    out.role = (r[3].empty() ? std::optional<std::string>{} : std::optional<std::string>(r[3]));
    return out;
}

template<> inline std::string to_json<User>([[maybe_unused]] const User& val) {
    std::string out = "{";
    out += "\"id\":";
    out += revet::json::detail::quote(val.id);
    out += ",\"name\":";
    out += revet::json::detail::quote(val.name);
    out += ",\"email\":";
    out += revet::json::detail::quote(val.email);
    out += ",\"role\":";
    if (val.role.has_value()) {
        out += revet::json::detail::quote((*val.role));
    } else {
        out += "null";
    }
    out += "}";
    return out;
}

// usgs_gov::CountResponse — from usgs.hpp
template<> inline usgs_gov::CountResponse from_json_value<usgs_gov::CountResponse>([[maybe_unused]] Value& v) {
    usgs_gov::CountResponse out{};
    out.count = static_cast<uint32_t>(v.at("count"));
    out.maxAllowed = static_cast<uint32_t>(v.at("maxAllowed"));
    return out;
}

template<> inline usgs_gov::CountResponse from_json<usgs_gov::CountResponse>([[maybe_unused]] Document& doc) {
    auto root = doc.root();
    return from_json_value<usgs_gov::CountResponse>(root);
}

template<> inline usgs_gov::CountResponse from_row<usgs_gov::CountResponse>([[maybe_unused]] const std::vector<std::string>& r) {
    usgs_gov::CountResponse out{};
    out.count = static_cast<uint32_t>(std::stol(r[0]));
    out.maxAllowed = static_cast<uint32_t>(std::stol(r[1]));
    return out;
}

template<> inline std::string to_json<usgs_gov::CountResponse>([[maybe_unused]] const usgs_gov::CountResponse& val) {
    std::string out = "{";
    out += "\"count\":";
    out += std::to_string(val.count);
    out += ",\"maxAllowed\":";
    out += std::to_string(val.maxAllowed);
    out += "}";
    return out;
}

} // namespace revet::json

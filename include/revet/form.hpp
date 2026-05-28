#pragma once
#include "html.hpp"

#include <optional>
#include <string>
#include <string_view>

#include "validate.hpp"

namespace revet {

static constexpr std::string_view k_error_color = "#e53e3e";

// ── Field type tags ───────────────────────────────────────────────────────────

struct Email {
    static constexpr const char* input_type = "email";
    static constexpr const char* inputmode = "email";
    static constexpr const char* autocomplete = "email";
    static constexpr const char* label = "Email";
    static constexpr ctll::fixed_string regex_pattern = "[^@]+@[^@]+\\.[a-zA-Z]+";
    static constexpr bool has_regex = true;
    static constexpr bool is_textarea = false;
};

struct PasswordNew {
    static constexpr const char* input_type = "password";
    static constexpr const char* inputmode = "";
    static constexpr const char* autocomplete = "new-password";
    static constexpr const char* label = "Password";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct PasswordCurrent {
    static constexpr const char* input_type = "password";
    static constexpr const char* inputmode = "";
    static constexpr const char* autocomplete = "current-password";
    static constexpr const char* label = "Password";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct Tel {
    static constexpr const char* input_type = "tel";
    static constexpr const char* inputmode = "tel";
    static constexpr const char* autocomplete = "tel";
    static constexpr const char* label = "Phone";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct WebUrl {
    static constexpr const char* input_type = "url";
    static constexpr const char* inputmode = "url";
    static constexpr const char* autocomplete = "url";
    static constexpr const char* label = "URL";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct GivenName {
    static constexpr const char* input_type = "text";
    static constexpr const char* inputmode = "text";
    static constexpr const char* autocomplete = "given-name";
    static constexpr const char* label = "First Name";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct FamilyName {
    static constexpr const char* input_type = "text";
    static constexpr const char* inputmode = "text";
    static constexpr const char* autocomplete = "family-name";
    static constexpr const char* label = "Last Name";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct FullName {
    static constexpr const char* input_type = "text";
    static constexpr const char* inputmode = "text";
    static constexpr const char* autocomplete = "name";
    static constexpr const char* label = "Full Name";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct Username {
    static constexpr const char* input_type = "text";
    static constexpr const char* inputmode = "text";
    static constexpr const char* autocomplete = "username";
    static constexpr const char* label = "Username";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct PostalCode {
    static constexpr const char* input_type = "text";
    static constexpr const char* inputmode = "numeric";
    static constexpr const char* autocomplete = "postal-code";
    static constexpr const char* label = "Postal Code";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct Integer {
    static constexpr const char* input_type = "number";
    static constexpr const char* inputmode = "numeric";
    static constexpr const char* autocomplete = "";
    static constexpr const char* label = "Number";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct Decimal {
    static constexpr const char* input_type = "number";
    static constexpr const char* inputmode = "decimal";
    static constexpr const char* autocomplete = "";
    static constexpr const char* label = "Number";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct Date {
    static constexpr const char* input_type = "date";
    static constexpr const char* inputmode = "";
    static constexpr const char* autocomplete = "";
    static constexpr const char* label = "Date";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct PlainText {
    static constexpr const char* input_type = "text";
    static constexpr const char* inputmode = "text";
    static constexpr const char* autocomplete = "";
    static constexpr const char* label = "Text";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

struct Textarea {
    static constexpr const char* input_type = "";
    static constexpr const char* inputmode = "text";
    static constexpr const char* autocomplete = "";
    static constexpr const char* label = "Message";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = true;
};

struct Search {
    static constexpr const char* input_type = "search";
    static constexpr const char* inputmode = "search";
    static constexpr const char* autocomplete = "";
    static constexpr const char* label = "Search";
    static constexpr bool has_regex = false;
    static constexpr bool is_textarea = false;
};

// ── Field<FieldType, Rules...> ────────────────────────────────────────────────
// FieldType controls HTML attributes and built-in regex.
// Rules... are validate:: rule types with static check()/msg().
// CSS class is passed at construction: Field<Email> email{styles::my_input};
// Required-ness is determined by codegen from whether the member is std::optional.
// name and error are set by the generated populate()/validate() functions.

template<typename FieldType, typename... Rules>
struct Field {
    using field_type = FieldType;

    std::string value;
    std::string name;
    std::string error;
    const html::css_class* _style = nullptr;

    Field() = default;
    explicit Field(const html::css_class& s) : _style(&s) {}

    void validate(bool required) {
        error.clear();
        if (required && value.empty()) {
            error = "required";
            return;
        }
        if (value.empty()) {
            return;
        }

        if constexpr (FieldType::has_regex) {
            if (!static_cast<bool>(ctre::match<FieldType::regex_pattern>(value))) {
                error = "invalid format";
                return;
            }
        }

        auto check_rule = [this]<typename R>() -> bool {
            if (!R::check(value)) {
                error = R::msg();
                return false;
            }
            return true;
        };
        (void)(check_rule.template operator()<Rules>() && ...);
    }

    operator html::Node() const {
        std::string class_attr;
        if (_style) {
            html::detail::style_registry[_style->name] = _style->rules;
            for (const auto& [sel, r] : _style->pseudo) {
                html::detail::style_registry[_style->name + sel] = r;
            }
            class_attr = std::format(" class=\"{}\"", _style->name);
        }

        std::string extra;
        if (!std::string_view(FieldType::inputmode).empty()) {
            extra += std::format(" inputmode=\"{}\"", FieldType::inputmode);
        }
        if (!std::string_view(FieldType::autocomplete).empty()) {
            extra += std::format(" autocomplete=\"{}\"", FieldType::autocomplete);
        }
        if (!error.empty()) {
            extra += std::format(" style=\"border-color:{}\"", k_error_color);
        }

        html::Node input_node;
        if constexpr (FieldType::is_textarea) {
            input_node = html::Node{html::raw_tag{}, "<textarea name=\"" + name + "\" rows=\"5\"" + class_attr + extra +
                                                         ">" + sanitize::html_escape(value) + "</textarea>"};
        } else {
            input_node = html::Node{html::raw_tag{}, std::format("<input type=\"{}\" name=\"{}\" value=\"{}\"{}{}>",
                                                                 FieldType::input_type, name,
                                                                 sanitize::html_escape(value), class_attr, extra)};
        }

        html::Node error_node;
        if (!error.empty()) {
            error_node =
                html::span{html::attr.style(std::format("color:{};font-size:.875rem;display:block", k_error_color)),
                           html::Node{html::raw_tag{}, sanitize::html_escape(error)}};
        }

        return html::label{html::attr.style("display:block;margin-bottom:1rem"),
                           html::span{html::attr.style("display:block;margin-bottom:.25rem;font-weight:500"),
                                      html::raw(FieldType::label)},
                           error_node, input_node};
    }
};

} // namespace revet

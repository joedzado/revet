#pragma once
#include <revet/form.hpp>

struct [[revet::form]] ContactForm {
    revet::Field<revet::Email,    validate::required_t, validate::max_len_t<254>> email;
    revet::Field<revet::PlainText, validate::required_t, validate::min_len_t<2>, validate::max_len_t<100>> name;
    revet::Field<revet::Textarea, validate::required_t, validate::min_len_t<10>, validate::max_len_t<1000>> message;
    std::optional<revet::Field<revet::Tel, validate::regex_t<"^[+]?[\\d\\s().\\-]{7,20}$", "use format: +1 555-555-5555">>> phone;
};

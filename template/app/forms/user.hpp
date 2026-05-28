#pragma once
#include <revet/form.hpp>

struct [[revet::form]] UserNewForm {
    revet::Field<revet::FullName, validate::required_t>                          name;
    revet::Field<revet::Email,    validate::required_t, validate::max_len_t<254>> email;
};

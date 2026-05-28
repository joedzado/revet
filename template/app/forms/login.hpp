#pragma once
#include <revet/form.hpp>

struct [[revet::form]] LoginForm {
    revet::Field<revet::Username,        validate::required_t> username;
    revet::Field<revet::PasswordCurrent, validate::required_t> password;
};

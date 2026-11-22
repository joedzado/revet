#pragma once
#include <optional>
#include <string>

struct [[revet::model]] CreateUser {
    std::string name;
    std::string email;
    std::optional<std::string> role;
};

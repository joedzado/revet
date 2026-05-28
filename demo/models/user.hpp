#pragma once
#include <optional>
#include <string>

struct [[revet::model]] User {
    std::string id;
    std::string name;
    std::string email;
    std::optional<std::string> role;
};

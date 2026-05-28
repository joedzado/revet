#pragma once
#include <string>

struct [[revet::model]] User {
    std::string id;
    std::string name;
    std::string email;
};

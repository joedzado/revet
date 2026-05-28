#pragma once
#include <string>

struct [[revet::model]] Toast {
    std::string message;
    std::string kind; // "success" | "error"
};

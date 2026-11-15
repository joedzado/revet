#pragma once
#include <string>
struct Cart {
    int count;
    std::string coupon;
};
struct [[maybe_unused]] Notice {
    std::string text;
    bool error;
};

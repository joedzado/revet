#pragma once
#include <cstdint>
#include <string>
struct Cart {
    int count;
    std::string coupon;
    std::int64_t expires;
    std::uint32_t version = 1;
};
struct [[maybe_unused]] Notice {
    std::string text;
    bool error;
};

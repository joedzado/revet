#pragma once
#include <optional>
#include <string>
#include <vector>
namespace shop::catalog {
[[revet::model]] struct Item {
    int64_t id;
    std::string title = "untitled";
    std::optional<double> price;
    std::vector<std::string> tags;
    bool active{true};
};
namespace inner {
struct [[revet::model(snake_case)]] LineItem {
    Item item;
    int qty;
};
}
}
struct NotAModel { int z; };

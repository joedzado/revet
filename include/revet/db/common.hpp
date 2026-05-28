#ifndef REVET_DB_COMMON_HPP
#define REVET_DB_COMMON_HPP

#pragma once
#include <string>
#include <vector>

namespace db {
struct Read {};  // read-only handle — no write lock acquired
struct Write {}; // write handle — acquires pool write lock on construction

using Row = std::vector<std::string>;
using Result = std::vector<Row>;
} // namespace db

#endif

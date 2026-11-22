#pragma once
// Minimal ordered document tree for the two formats codegen reads or writes:
// revet.toml (subset of TOML) and compile_commands.json.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace revet::codegen {

struct Node {
    enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Table };

    Kind kind = Kind::Null;
    bool b = false;
    std::string s;                 // String value, or raw text for Number
    std::vector<Node> items;       // Array
    std::vector<std::string> keys; // Table, insertion order
    std::vector<Node> vals;        // Table values, parallel to keys

    // Table lookup; nullptr when missing or not a table.
    [[nodiscard]] auto find(std::string_view key) const -> const Node*;
    // Table insert-or-replace, keeping the original position of an existing key.
    void set(const std::string& key, Node value);
};

// Parses JSON. Throws std::runtime_error on malformed input.
auto parse_json(std::string_view text) -> Node;
// Matches json.dumps(obj, indent=2) output (ensure_ascii, no trailing newline).
auto dump_json_indent2(const Node& n) -> std::string;

// Parses the TOML subset used by revet.toml: [a.b] tables, dotted keys,
// strings, integers, floats, booleans, arrays and inline tables.
// Throws std::runtime_error on anything else.
auto parse_toml(std::string_view text) -> Node;

// Conversions used by the generators.
auto py_str(const Node& n) -> std::string; // str(value)
auto py_int(const Node& n) -> long long;   // int(value)
auto py_bool(const Node& n) -> bool;       // bool(value)

} // namespace revet::codegen

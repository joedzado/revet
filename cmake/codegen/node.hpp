#pragma once
// Minimal ordered document tree for the two formats codegen reads or writes:
// revet.toml (subset of TOML) and compile_commands.json.

#include <string>
#include <string_view>
#include <vector>

namespace revet::codegen {

struct Node {
    enum class Kind { Null, Bool, Number, String, Array, Table };

    Kind kind = Kind::Null;
    bool b = false;
    std::string s;                  // String value, or raw text for Number
    std::vector<Node> items;        // Array
    std::vector<std::string> keys;  // Table, insertion order
    std::vector<Node> vals;         // Table values, parallel to keys

    // Table lookup; nullptr when missing or not a table.
    const Node* find(std::string_view key) const;
    // Table insert-or-replace, keeping the original position of an existing key.
    void set(const std::string& key, Node value);
};

// Parses JSON. Throws std::runtime_error on malformed input.
Node parse_json(std::string_view text);
// Matches Python json.dumps(obj, indent=2) (ensure_ascii, no trailing newline).
std::string dump_json_indent2(const Node& n);

// Parses the TOML subset used by revet.toml: [a.b] tables, dotted keys,
// strings, integers, floats, booleans, arrays and inline tables.
// Throws std::runtime_error on anything else.
Node parse_toml(std::string_view text);

// Python-style conversions used by the generators.
std::string py_str(const Node& n);   // str(value)
long long py_int(const Node& n);     // int(value)
bool py_bool(const Node& n);         // bool(value)

}  // namespace revet::codegen

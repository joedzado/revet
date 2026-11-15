// app/queries/*.sql -> queries.hpp (typed async/sync wrappers).
#include "common.hpp"

#include <cctype>
#include <map>
#include <set>

namespace revet::codegen {
namespace {

const std::set<std::string> exec_verbs = {"insert", "update", "delete", "create", "drop", "alter", "truncate"};
const std::set<std::string> valid_directives = {"name", "model", "raw", "param"};

const std::map<std::string, std::string> param_types = {
    {"int", "int"},     {"int32", "int32_t"}, {"int64", "int64_t"},   {"uint32", "uint32_t"},
    {"uint64", "uint64_t"}, {"double", "double"}, {"float", "float"}, {"string", "std::string"},
    {"bool", "bool"},
};

[[noreturn]] void die(const std::string& msg) {
    throw GenError("error: " + msg);
}

bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Scrub mode (params == nullptr) blanks literals and comments
// to spaces; sub mode rewrites :name → ? for declared params (outside literals/comments).
std::string process_sql(const std::string& sql, const std::set<std::string>* params) {
    std::string out;
    auto keep = [&](char c) { return params == nullptr ? ' ' : c; };
    size_t i = 0;
    while (i < sql.size()) {
        if (sql[i] == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            while (i < sql.size() && sql[i] != '\n') {
                out += keep(sql[i]);
                ++i;
            }
        } else if (sql[i] == '\'') {
            out += keep(sql[i]);
            ++i;
            while (i < sql.size() && sql[i] != '\'') {
                out += keep(sql[i]);
                ++i;
            }
            if (i < sql.size()) {
                out += keep(sql[i]);
                ++i;
            }
        } else if (params != nullptr && sql[i] == ':') {
            size_t j = i + 1;
            if (j < sql.size() && (std::isalpha(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) {
                size_t k = j;
                while (k < sql.size() && is_word(sql[k])) ++k;
                std::string name = sql.substr(j, k - j);
                if (params->count(name)) {
                    out += '?';
                    i = k;
                    continue;
                }
            }
            out += sql[i];
            ++i;
        } else {
            out += sql[i];
            ++i;
        }
    }
    return out;
}

std::string snake_to_camel(const std::string& s) {
    std::string out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('_', start);
        if (end == std::string::npos) end = s.size();
        std::string w = s.substr(start, end - start);
        // First letter upper, the rest lower.
        for (size_t k = 0; k < w.size(); ++k) {
            w[k] = static_cast<char>(k == 0 ? std::toupper(static_cast<unsigned char>(w[k]))
                                            : std::tolower(static_cast<unsigned char>(w[k])));
        }
        out += w;
        start = end + 1;
    }
    return out;
}

std::string infer_verb(const std::string& sql, const std::string& file, const std::string& name) {
    for (const auto& line : splitlines(sql)) {
        std::string stripped = strip(line);
        if (stripped.empty() || starts_with(stripped, "--")) continue;
        size_t end = 0;
        while (end < stripped.size() && !std::isspace(static_cast<unsigned char>(stripped[end]))) ++end;
        std::string token = lower(stripped.substr(0, end));
        if (exec_verbs.count(token)) return "exec";
        if (token == "select") return "query";
        die(file + ": block '" + name + "': unrecognised SQL verb '" + token +
            "' — expected SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, or TRUNCATE");
    }
    die(file + ": block '" + name + "': no SQL statement found");
}

Lines emit_pair(const std::string& fn, const std::string& opts, const std::string& async_ret,
                const std::string& sync_ret, const Lines& async_body, const Lines& sync_body) {
    Lines out;
    out.push_back(cat("inline auto ", fn, "(auto& db", opts, ") -> ", async_ret, " {"));
    for (const auto& l : async_body) out.push_back(l);
    out.push_back("}");
    out.push_back("");
    out.push_back(cat("inline auto sync_", fn, "(auto& db", opts, ") -> ", sync_ret, " {"));
    for (const auto& l : sync_body) out.push_back(l);
    out.push_back("}");
    out.push_back("");
    return out;
}

Lines emit_exec(const std::string& fn, const std::string& sql, const std::string& args_decl, const std::string& call_args) {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    return emit_pair(fn, opts, "web::Task<void>", "void",
                     {cat("    co_await db.async_exec(\"", sql, "\"", call_args, ");")},
                     {cat("    db.exec(\"", sql, "\"", call_args, ");")});
}

Lines emit_query_raw(const std::string& fn, const std::string& sql, const std::string& args_decl, const std::string& call_args) {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    return emit_pair(fn, opts, "web::Task<db::Result>", "db::Result",
                     {cat("    co_return co_await db.async_query(\"", sql, "\"", call_args, ");")},
                     {cat("    return db.query(\"", sql, "\"", call_args, ");")});
}

Lines emit_query_model(const std::string& fn, const std::string& sql, const std::string& args_decl,
                       const std::string& call_args, const std::string& mdl) {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    Lines map_rows = {
        cat("    std::vector<", mdl, "> out;"),
        "    out.reserve(raw.size());",
        cat("    for (auto& r : raw) out.push_back(revet::json::from_row<", mdl, ">(r));"),
    };
    Lines async_body = {cat("    auto raw = co_await db.async_query(\"", sql, "\"", call_args, ");")};
    for (const auto& l : map_rows) async_body.push_back(l);
    async_body.push_back("    co_return out;");
    Lines sync_body = {cat("    auto raw = db.query(\"", sql, "\"", call_args, ");")};
    for (const auto& l : map_rows) sync_body.push_back(l);
    sync_body.push_back("    return out;");
    return emit_pair(fn, opts, cat("web::Task<std::vector<", mdl, ">>"), cat("std::vector<", mdl, ">"), async_body, sync_body);
}

}  // namespace

int gen_queries(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "queries.hpp");
    fs::path queries_dir = ctx.proj / "app" / "queries";
    if (!file_exists(queries_dir)) throw GenError("gen_queries: queries directory not found: " + queries_dir.string());

    std::vector<fs::path> sql_files = glob(queries_dir, ".sql");
    if (sql_files.empty()) {
        write_file(out_path, "// no queries/*.sql files found\n#pragma once\n");
        return 0;
    }

    static const Re exec_re(R"RE(--\s*exec\s*)RE");
    static const Re columns_re(R"RE(--\s*columns\s*:)RE");
    static const Re directive_re(R"RE(--\s*@revet:(\w+))RE");
    static const Re split_re(R"RE(--\s*@revet:name:\s*)RE");
    static const Re model_re(R"RE(--\s*@revet:model:\s*(\w+))RE");
    static const Re raw_re(R"RE(--\s*@revet:raw\s*)RE");
    static const Re param_re(R"RE(--\s*@revet:param:\s*(\w+)\s+(\w+))RE");
    static const Re ref_re(R"RE(:([A-Za-z_]\w*))RE");

    Lines out = file_header({"#include <revet/db.hpp>", "#include <revet/task.hpp>", "#include \"models.hpp\"",
                             "#include <string>", "#include <utility>", "#include <vector>"});
    out.push_back("namespace queries {");
    out.push_back("");

    for (const auto& sql_file : sql_files) {
        std::string ns = sql_file.stem().string();
        std::string file = sql_file.filename().string();
        std::string content = read_text(sql_file);

        size_t lineno = 0;
        for (const auto& line : splitlines(content)) {
            ++lineno;
            std::string stripped = strip(line);
            Match m;
            std::string where = file + ":" + std::to_string(lineno) + ": ";
            if (match_full(stripped, m, exec_re)) {
                die(where + "'-- exec' is not a valid directive — exec/query is inferred from the SQL verb");
            }
            if (match_prefix(stripped, m, columns_re)) {
                die(where + "'-- columns:' is not a valid directive — use '-- @revet:model: <Type>'");
            }
            if (match_prefix(stripped, m, directive_re)) {
                std::string key = m[1].str();
                if (!valid_directives.count(key)) {
                    std::string valid;
                    for (const auto& d : valid_directives) {
                        if (!valid.empty()) valid += ", ";
                        valid += "@revet:" + d;
                    }
                    die(where + "unknown directive '@revet:" + key + "' — valid directives: " + valid);
                }
            }
        }

        // re.split(r"--\s*@revet:name:\s*", content)
        std::vector<std::string> blocks;
        size_t prev = 0;
        for (auto it = std::sregex_iterator(content.begin(), content.end(), split_re); it != std::sregex_iterator(); ++it) {
            size_t pos = static_cast<size_t>(it->position(0));
            blocks.push_back(content.substr(prev, pos - prev));
            prev = pos + static_cast<size_t>(it->length(0));
        }
        blocks.push_back(content.substr(prev));

        Lines ns_out;
        for (size_t bi = 1; bi < blocks.size(); ++bi) {
            Lines block_lines = splitlines(blocks[bi]);
            if (block_lines.empty()) die(file + ": empty @revet:name block");
            std::string name = strip(block_lines[0]);

            std::string model;
            bool is_raw = false;
            std::vector<std::pair<std::string, std::string>> params;  // ordered: (name, C++ type)
            Lines sql_lines;
            for (size_t li = 1; li < block_lines.size(); ++li) {
                const std::string& line = block_lines[li];
                std::string stripped = strip(line);
                Match m;
                if (match_prefix(stripped, m, model_re)) {
                    model = m[1].str();
                } else if (match_prefix(stripped, m, raw_re)) {
                    is_raw = true;
                } else if (match_prefix(stripped, m, param_re)) {
                    std::string pname = m[1].str();
                    std::string ptype = m[2].str();
                    auto type_it = param_types.find(ptype);
                    if (type_it == param_types.end()) {
                        die(file + ": block '" + name + "': unknown param type '" + ptype + "' — valid types: " +
                            "bool, double, float, int, int32, int64, string, uint32, uint64");
                    }
                    for (const auto& p : params) {
                        if (p.first == pname) die(file + ": block '" + name + "': duplicate param '" + pname + "'");
                    }
                    params.emplace_back(pname, type_it->second);
                } else {
                    sql_lines.push_back(line);
                }
            }

            std::string sql = strip(join(sql_lines, "\n"));

            // :name references outside literals and comments, in order (repeats kept).
            std::string scrubbed = process_sql(sql, nullptr);
            std::vector<std::string> sql_param_refs;
            std::set<std::string> sql_param_set;
            for (auto it = std::sregex_iterator(scrubbed.begin(), scrubbed.end(), ref_re); it != std::sregex_iterator(); ++it) {
                sql_param_refs.push_back((*it)[1].str());
                sql_param_set.insert((*it)[1].str());
            }

            std::string struct_name;
            std::string sql_escaped;
            std::string args_decl;
            std::string call_args;
            if (!params.empty()) {
                std::set<std::string> declared;
                for (const auto& p : params) declared.insert(p.first);
                for (const auto& p : params) {
                    if (!sql_param_set.count(p.first)) {
                        die(file + ": block '" + name + "': param '" + p.first + "' declared but not used in SQL");
                    }
                }
                for (const auto& ref : sql_param_set) {
                    if (!declared.count(ref)) {
                        die(file + ": block '" + name + "': SQL uses ':" + ref + "' but it is not declared with @revet:param");
                    }
                }
                sql_escaped = escape_str(process_sql(sql, &declared));
                struct_name = snake_to_camel(name) + "Params";

                Lines fields;
                for (const auto& p : params) fields.push_back(cat("    ", p.second, " ", p.first, ";"));
                ns_out.push_back(cat("struct ", struct_name, " {"));
                for (const auto& f : fields) ns_out.push_back(f);
                ns_out.push_back("};");
                ns_out.push_back("");

                args_decl = "const " + struct_name + "& p";
                for (const auto& ref : sql_param_refs) call_args += ", p." + ref;
                call_args = ", " + call_args.substr(2);
                if (sql_param_refs.empty()) call_args.clear();
            } else if (!sql_param_set.empty()) {
                std::string refs;
                for (const auto& r : sql_param_set) {
                    if (!refs.empty()) refs += ", ";
                    refs += ":" + r;
                }
                die(file + ": block '" + name + "': SQL uses named params (" + refs +
                    ") but no '@revet:param' directives are declared");
            } else {
                sql_escaped = escape_str(sql);
            }

            std::string verb = infer_verb(sql, file, name);

            if (model.empty() && !is_raw && verb == "query") {
                die(file + ": block '" + name +
                    "': SELECT query is missing '-- @revet:model: <Type>'.\n"
                    "  Add '-- @revet:model: <Type>' to map results to a struct, or '-- @revet:raw' to return vector<vector<string>>.");
            }

            Lines emitted;
            if (verb == "exec") {
                emitted = emit_exec(name, sql_escaped, args_decl, call_args);
            } else if (!model.empty()) {
                emitted = emit_query_model(name, sql_escaped, args_decl, call_args, model);
            } else {
                emitted = emit_query_raw(name, sql_escaped, args_decl, call_args);
            }
            for (const auto& l : emitted) ns_out.push_back(l);
        }

        if (!ns_out.empty()) {
            out.push_back("namespace " + ns + " {");
            out.push_back("");
            for (const auto& l : ns_out) out.push_back(l);
            out.push_back("} // namespace " + ns);
            out.push_back("");
        }
    }

    out.push_back("} // namespace queries");
    out.push_back("");
    write_lines(out_path, out);
    return 0;
}

}  // namespace revet::codegen

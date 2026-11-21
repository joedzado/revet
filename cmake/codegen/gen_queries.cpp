// app/queries/*.sql -> queries.hpp (typed async/sync wrappers).
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "common.hpp"

namespace revet::codegen {
namespace {

auto exec_verbs() -> const std::set<std::string>& {
    static const std::set<std::string> values = {"insert", "update", "delete", "create", "drop", "alter", "truncate"};
    return values;
}
auto valid_directives() -> const std::set<std::string>& {
    static const std::set<std::string> values = {"name", "model", "raw", "param", "returns", "scalar"};
    return values;
}

auto param_types() -> const std::map<std::string, std::string>& {
    static const std::map<std::string, std::string> values = {
        {"int", "int"},         {"int32", "int32_t"},          {"int64", "int64_t"}, {"uint32", "uint32_t"},
        {"uint64", "uint64_t"}, {"double", "double"},          {"float", "float"},   {"string", "std::string"},
        {"bool", "bool"},       {"decimal", "revet::Decimal"},
    };
    return values;
}

[[noreturn]] void die(const std::string& msg) {
    throw GenError("error: " + msg);
}

auto is_word(char c) -> bool {
    return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

// Scrub mode (params == nullptr) blanks literals and comments
// to spaces; sub mode rewrites :name → ? for declared params (outside literals/comments).
auto process_sql(const std::string& sql, const std::set<std::string>* params) -> std::string {
    std::string out;
    auto keep = [&](char c) -> char { return params == nullptr ? ' ' : c; };
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
            if (j < sql.size() && ((std::isalpha(static_cast<unsigned char>(sql[j])) != 0) || sql[j] == '_')) {
                size_t k = j;
                while (k < sql.size() && is_word(sql[k])) {
                    ++k;
                }
                std::string name = sql.substr(j, k - j);
                if (params->contains(name)) {
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

auto snake_to_camel(const std::string& s) -> std::string {
    std::string out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('_', start);
        if (end == std::string::npos) {
            end = s.size();
        }
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

// 'int64' -> 'int64_t', 'string?' -> 'std::optional<std::string>'.
auto cpp_type(const std::string& t, const std::string& file, const std::string& name) -> std::string {
    bool nullable = !t.empty() && t.back() == '?';
    std::string base = nullable ? t.substr(0, t.size() - 1) : t;
    auto it = param_types().find(base);
    if (it == param_types().end()) {
        std::string valid;
        for (const auto& [k, v] : param_types()) {
            (void)v;
            if (!valid.empty()) {
                valid += ", ";
            }
            valid += k;
        }
        die(cat(file, ": block '", name, "': unknown type '", t, "' — valid types: ", valid,
                " (add '?' for nullable)"));
    }
    return nullable ? "std::optional<" + it->second + ">" : it->second;
}

// For `WITH a AS (...), b AS (...) <stmt>`, the first verb of <stmt> outside parentheses.
auto main_verb_after_with(const std::string& sql) -> std::optional<std::string> {
    const std::string scrubbed = lower(process_sql(sql, nullptr));
    int depth = 0;
    for (auto m : ctre::search_all<R"RE([()]|\b(?:select|insert|update|delete)\b)RE">(std::string_view(scrubbed))) {
        std::string_view tok = m.to_view();
        if (tok == "(") {
            ++depth;
        } else if (tok == ")") {
            --depth;
        } else if (depth == 0) {
            return std::string(tok);
        }
    }
    return std::nullopt;
}

auto infer_verb(const std::string& sql, const std::string& file, const std::string& name) -> std::string_view {
    for (const auto& line : splitlines(sql)) {
        std::string stripped = strip(line);
        if (stripped.empty() || starts_with(stripped, "--")) {
            continue;
        }
        size_t end = 0;
        while (end < stripped.size() && (std::isspace(static_cast<unsigned char>(stripped[end])) == 0)) {
            ++end;
        }
        std::string token = lower(stripped.substr(0, end));
        if (exec_verbs().contains(token)) {
            return "exec";
        }
        if (token == "select" || token == "values" || token == "pragma") {
            return "query";
        }
        if (token == "with") {
            auto main = main_verb_after_with(sql);
            if (!main) {
                die(cat(file, ": block '", name, "': WITH has no statement after its CTEs"));
            }
            // A data-modifying CTE statement with RETURNING still yields rows.
            if (*main != "select" && !ctre::search<R"RE(\breturning\b)RE">(lower(process_sql(sql, nullptr)))) {
                return "exec";
            }
            return "query";
        }
        die(cat(file, ": block '", name, "': unrecognised SQL verb '", token,
                "' — expected SELECT, WITH, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, or TRUNCATE"));
    }
    die(cat(file, ": block '", name, "': no SQL statement found"));
}

auto emit_pair(const std::string& fn, const std::string& opts, const std::string& async_ret,
               const std::string& sync_ret, const Lines& async_body, const Lines& sync_body) -> Lines {
    Lines out;
    out.push_back(cat("inline auto ", fn, "(auto& db", opts, ") -> ", async_ret, " {"));
    for (const auto& l : async_body) {
        out.push_back(l);
    }
    out.emplace_back("}");
    out.emplace_back("");
    out.push_back(cat("inline auto sync_", fn, "(auto& db", opts, ") -> ", sync_ret, " {"));
    for (const auto& l : sync_body) {
        out.push_back(l);
    }
    out.emplace_back("}");
    out.emplace_back("");
    return out;
}

auto emit_exec(const std::string& fn, const std::string& sql, const std::string& args_decl,
               const std::string& call_args) -> Lines {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    return emit_pair(fn, opts, "web::Task<void>", "void",
                     {cat("    co_await db.async_exec(\"", sql, "\"", call_args, ");")},
                     {cat("    db.exec(\"", sql, "\"", call_args, ");")});
}

auto emit_query_raw(const std::string& fn, const std::string& sql, const std::string& args_decl,
                    const std::string& call_args) -> Lines {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    return emit_pair(fn, opts, "web::Task<db::Result>", "db::Result",
                     {cat("    co_return co_await db.async_query(\"", sql, "\"", call_args, ");")},
                     {cat("    return db.query(\"", sql, "\"", call_args, ");")});
}

auto emit_query_typed(const std::string& fn, const std::string& sql, const std::string& args_decl,
                      const std::string& call_args, const std::vector<std::string>& cols) -> Lines {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    std::string types = join(cols, ", ");
    std::string ret = "std::vector<std::tuple<" + types + ">>";
    return emit_pair(fn, opts, "web::Task<" + ret + ">", ret,
                     {cat("    auto raw = co_await db.async_query_nullable(\"", sql, "\"", call_args, ");"),
                      cat("    co_return db::typed<", types, ">(raw);")},
                     {cat("    return db::typed<", types, ">(db.query_nullable(\"", sql, "\"", call_args, "));")});
}

auto emit_query_scalar(const std::string& fn, const std::string& sql, const std::string& args_decl,
                       const std::string& call_args, const std::string& col) -> Lines {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    // A nullable scalar type is already optional; don't wrap it twice.
    const std::string prefix = "std::optional<";
    bool already = col.starts_with(prefix);
    std::string ret = already ? col : prefix + col + ">";
    std::string inner = already ? col.substr(prefix.size(), col.size() - prefix.size() - 1) : col;
    return emit_pair(fn, opts, "web::Task<" + ret + ">", ret,
                     {cat("    auto raw = co_await db.async_query_nullable(\"", sql, "\"", call_args, ");"),
                      "    if (raw.empty() || raw[0].empty() || !raw[0][0]) co_return std::nullopt;",
                      cat("    co_return db::detail::convert<", inner, ">(raw[0][0], 0);")},
                     {cat("    auto raw = db.query_nullable(\"", sql, "\"", call_args, ");"),
                      "    if (raw.empty() || raw[0].empty() || !raw[0][0]) return std::nullopt;",
                      cat("    return db::detail::convert<", inner, ">(raw[0][0], 0);")});
}

auto emit_query_model(const std::string& fn, const std::string& sql, const std::string& args_decl,
                      const std::string& call_args, const std::string& mdl) -> Lines {
    std::string opts = args_decl.empty() ? "" : ", " + args_decl;
    Lines map_rows = {
        cat("    std::vector<", mdl, "> out;"),
        "    out.reserve(raw.size());",
        cat("    for (auto& r : raw) out.push_back(revet::json::from_row<", mdl, ">(r));"),
    };
    Lines async_body = {cat("    auto raw = co_await db.async_query(\"", sql, "\"", call_args, ");")};
    for (const auto& l : map_rows) {
        async_body.push_back(l);
    }
    async_body.emplace_back("    co_return out;");
    Lines sync_body = {cat("    auto raw = db.query(\"", sql, "\"", call_args, ");")};
    for (const auto& l : map_rows) {
        sync_body.push_back(l);
    }
    sync_body.emplace_back("    return out;");
    return emit_pair(fn, opts, cat("web::Task<std::vector<", mdl, ">>"), cat("std::vector<", mdl, ">"), async_body,
                     sync_body);
}

} // namespace

auto gen_queries(const Ctx& ctx) -> int {
    fs::path out_path = setup(ctx, "queries.hpp");
    fs::path queries_dir = ctx.proj / "app" / "queries";

    std::vector<fs::path> sql_files = glob(queries_dir, ".sql");
    if (sql_files.empty()) {
        write_file(out_path, "// no queries/*.sql files found\n#pragma once\n");
        return 0;
    }

    Lines out = file_header({"#include <revet/db.hpp>", "#include <revet/task.hpp>", "#include <revet/decimal.hpp>",
                             "#include \"models.hpp\"", "#include <optional>", "#include <string>", "#include <tuple>",
                             "#include <utility>", "#include <vector>"});
    out.emplace_back("namespace queries {");
    out.emplace_back("");

    for (const auto& sql_file : sql_files) {
        std::string ns = sql_file.stem().string();
        std::string file = sql_file.filename().string();
        std::string content = read_text(sql_file);

        size_t lineno = 0;
        for (const auto& line : splitlines(content)) {
            ++lineno;
            std::string stripped = strip(line);
            std::string where = file + ":" + std::to_string(lineno) + ": ";
            if (ctre::match<R"RE(--\s*exec\s*)RE">(stripped)) {
                die(where + "'-- exec' is not a valid directive — exec/query is inferred from the SQL verb");
            }
            if (ctre::starts_with<R"RE(--\s*columns\s*:)RE">(stripped)) {
                die(where + "'-- columns:' is not a valid directive — use '-- @revet:model: <Type>'");
            }
            if (auto m = ctre::starts_with<R"RE(--\s*@revet:(\w+))RE">(stripped)) {
                std::string key = m.get<1>().to_string();
                if (!valid_directives().contains(key)) {
                    std::string valid;
                    for (const auto& d : valid_directives()) {
                        if (!valid.empty()) {
                            valid += ", ";
                        }
                        valid += "@revet:" + d;
                    }
                    die(cat(where, "unknown directive '@revet:", key, "' — valid directives: ", valid));
                }
            }
        }

        // re.split(r"--\s*@revet:name:\s*", content)
        std::vector<std::string> blocks;
        size_t prev = 0;
        for (auto m : ctre::search_all<R"RE(--\s*@revet:name:\s*)RE">(std::string_view(content))) {
            size_t pos = offset_in(content, m.to_view());
            blocks.push_back(content.substr(prev, pos - prev));
            prev = pos + m.size();
        }
        blocks.push_back(content.substr(prev));

        Lines ns_out;
        for (size_t bi = 1; bi < blocks.size(); ++bi) {
            Lines block_lines = splitlines(blocks[bi]);
            if (block_lines.empty()) {
                die(file + ": empty @revet:name block");
            }
            std::string name = strip(block_lines[0]);

            std::string model;
            bool is_raw = false;
            std::vector<std::string> returns;
            std::string scalar;
            std::vector<std::pair<std::string, std::string>> params; // ordered: (name, C++ type)
            Lines sql_lines;
            for (size_t li = 1; li < block_lines.size(); ++li) {
                const std::string& line = block_lines[li];
                std::string stripped = strip(line);
                if (auto m = ctre::starts_with<R"RE(--\s*@revet:model:\s*(\w+))RE">(stripped)) {
                    model = m.get<1>().to_string();
                } else if (ctre::match<R"RE(--\s*@revet:raw\s*)RE">(stripped)) {
                    is_raw = true;
                } else if (auto r = ctre::match<R"RE(--\s*@revet:returns:\s*([^\n]+))RE">(stripped)) {
                    for (const auto& t : split_args(r.get<1>().to_view())) {
                        std::string ts = strip(t);
                        if (!ts.empty()) {
                            returns.push_back(cpp_type(ts, file, name));
                        }
                    }
                } else if (auto sc = ctre::match<R"RE(--\s*@revet:scalar:\s*(\w+\??)\s*)RE">(stripped)) {
                    scalar = cpp_type(sc.get<1>().to_string(), file, name);
                } else if (auto pm = ctre::starts_with<R"RE(--\s*@revet:param:\s*(\w+)\s+(\w+\??))RE">(stripped)) {
                    std::string pname = pm.get<1>().to_string();
                    std::string ctype = cpp_type(pm.get<2>().to_string(), file, name);
                    for (const auto& p : params) {
                        if (p.first == pname) {
                            die(cat(file, ": block '", name, "': duplicate param '", pname, "'"));
                        }
                    }
                    params.emplace_back(pname, ctype);
                } else {
                    sql_lines.push_back(line);
                }
            }

            std::string sql = strip(join(sql_lines, "\n"));

            // :name references outside literals and comments, in order (repeats kept).
            std::string scrubbed = process_sql(sql, nullptr);
            std::vector<std::string> sql_param_refs;
            std::set<std::string> sql_param_set;
            for (auto m : ctre::search_all<R"RE(:([A-Za-z_]\w*))RE">(std::string_view(scrubbed))) {
                sql_param_refs.push_back(m.get<1>().to_string());
                sql_param_set.insert(m.get<1>().to_string());
            }

            std::string struct_name;
            std::string sql_escaped;
            std::string args_decl;
            std::string call_args;
            if (!params.empty()) {
                std::set<std::string> declared;
                for (const auto& p : params) {
                    declared.insert(p.first);
                }
                for (const auto& p : params) {
                    if (!sql_param_set.contains(p.first)) {
                        die(cat(file, ": block '", name, "': param '", p.first, "' declared but not used in SQL"));
                    }
                }
                for (const auto& ref : sql_param_set) {
                    if (!declared.contains(ref)) {
                        die(cat(file, ": block '", name, "': SQL uses ':", ref,
                                "' but it is not declared with @revet:param"));
                    }
                }
                sql_escaped = escape_str(process_sql(sql, &declared));
                struct_name = snake_to_camel(name) + "Params";

                Lines fields;
                for (const auto& p : params) {
                    fields.push_back(cat("    ", p.second, " ", p.first, ";"));
                }
                ns_out.push_back(cat("struct ", struct_name, " {"));
                for (const auto& f : fields) {
                    ns_out.push_back(f);
                }
                ns_out.emplace_back("};");
                ns_out.emplace_back("");

                args_decl = "const " + struct_name + "& p";
                for (const auto& ref : sql_param_refs) {
                    call_args += ", p." + ref;
                }
                call_args = ", " + call_args.substr(2);
                if (sql_param_refs.empty()) {
                    call_args.clear();
                }
            } else if (!sql_param_set.empty()) {
                std::string refs;
                for (const auto& r : sql_param_set) {
                    if (!refs.empty()) {
                        refs += ", ";
                    }
                    refs += ":" + r;
                }
                die(cat(file, ": block '", name, "': SQL uses named params (", refs,
                        ") but no '@revet:param' directives are declared"));
            } else {
                sql_escaped = escape_str(sql);
            }

            std::string verb(infer_verb(sql, file, name));

            std::vector<std::string> shapes;
            if (!model.empty()) {
                shapes.emplace_back("model");
            }
            if (is_raw) {
                shapes.emplace_back("raw");
            }
            if (!returns.empty()) {
                shapes.emplace_back("returns");
            }
            if (!scalar.empty()) {
                shapes.emplace_back("scalar");
            }
            if (shapes.size() > 1) {
                die(cat(file, ": block '", name,
                        "': pick one of @revet:model, @revet:raw, @revet:returns, @revet:scalar (got ",
                        join(shapes, ", "), ")"));
            }
            if (shapes.empty() && verb == "query") {
                die(cat(file, ": block '", name,
                        "': SELECT query is missing '-- @revet:model: <Type>'.\n"
                        "  Add '-- @revet:model: <Type>' to map results to a struct, '-- @revet:returns: <types>' for "
                        "typed tuples,\n"
                        "  '-- @revet:scalar: <type>' for one value, or '-- @revet:raw' to return "
                        "vector<vector<string>>."));
            }

            Lines emitted;
            if (verb == "exec") {
                emitted = emit_exec(name, sql_escaped, args_decl, call_args);
            } else if (!model.empty()) {
                emitted = emit_query_model(name, sql_escaped, args_decl, call_args, model);
            } else if (!returns.empty()) {
                emitted = emit_query_typed(name, sql_escaped, args_decl, call_args, returns);
            } else if (!scalar.empty()) {
                emitted = emit_query_scalar(name, sql_escaped, args_decl, call_args, scalar);
            } else {
                emitted = emit_query_raw(name, sql_escaped, args_decl, call_args);
            }
            for (const auto& l : emitted) {
                ns_out.push_back(l);
            }
        }

        if (!ns_out.empty()) {
            out.push_back("namespace " + ns + " {");
            out.emplace_back("");
            for (const auto& l : ns_out) {
                out.push_back(l);
            }
            out.push_back("} // namespace " + ns);
            out.emplace_back("");
        }
    }

    out.emplace_back("} // namespace queries");
    out.emplace_back("");
    write_lines(out_path, out);
    return 0;
}

} // namespace revet::codegen

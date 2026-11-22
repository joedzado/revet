// app/queries/*.sql -> queries.hpp (typed async/sync wrappers).
#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
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
    const size_t n = sql.size();
    // Copies sql[from, to), or blanks it in scrub mode.
    auto keep = [&](size_t from, size_t to) -> void {
        out += params == nullptr ? std::string(to - from, ' ') : sql.substr(from, to - from);
    };
    size_t i = 0;
    while (i < n) {
        if (sql.compare(i, 2, "--") == 0) {
            size_t end = std::min(sql.find('\n', i), n);
            keep(i, end);
            i = end;
        } else if (sql[i] == '\'') {
            size_t close = sql.find('\'', i + 1);
            size_t end = close == std::string::npos ? n : close + 1;
            keep(i, end);
            i = end;
        } else if (params != nullptr && sql[i] == ':') {
            size_t j = i + 1;
            if (j < n && ((std::isalpha(static_cast<unsigned char>(sql[j])) != 0) || sql[j] == '_')) {
                auto k = static_cast<size_t>(
                    std::find_if_not(sql.begin() + static_cast<std::ptrdiff_t>(j), sql.end(), is_word) - sql.begin());
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
    // Each word: first letter upper, the rest lower.
    auto capitalized = [](auto word) -> std::string {
        std::string w = lower(std::string_view(word));
        if (!w.empty()) {
            w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
        }
        return w;
    };
    return join(s | std::views::split('_') | std::views::transform(capitalized) | std::ranges::to<Lines>(), "");
}

// 'int64' -> 'int64_t', 'string?' -> 'std::optional<std::string>'.
auto cpp_type(const std::string& t, const std::string& file, const std::string& name) -> std::string {
    bool nullable = !t.empty() && t.back() == '?';
    std::string base = nullable ? t.substr(0, t.size() - 1) : t;
    auto it = param_types().find(base);
    if (it == param_types().end()) {
        std::string valid = join(param_types() | std::views::keys | std::ranges::to<Lines>(), ", ");
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
    auto lines = splitlines(sql) | std::views::transform(strip) | std::ranges::to<Lines>();
    auto first =
        std::ranges::find_if(lines, [](const std::string& l) -> bool { return !l.empty() && !starts_with(l, "--"); });
    if (first == lines.end()) {
        die(cat(file, ": block '", name, "': no SQL statement found"));
    }
    std::string token = lower(first->substr(0, first->find_first_of(" \t\n\r\f\v")));
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

auto emit_pair(const std::string& fn, const std::string& opts, const std::string& async_ret,
               const std::string& sync_ret, const Lines& async_body, const Lines& sync_body) -> Lines {
    Lines out;
    out.push_back(cat("inline auto ", fn, "(auto& db", opts, ") -> ", async_ret, " {"));
    append(out, async_body);
    out.emplace_back("}");
    out.emplace_back("");
    out.push_back(cat("inline auto sync_", fn, "(auto& db", opts, ") -> ", sync_ret, " {"));
    append(out, sync_body);
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
    append(async_body, map_rows);
    async_body.emplace_back("    co_return out;");
    Lines sync_body = {cat("    auto raw = db.query(\"", sql, "\"", call_args, ");")};
    append(sync_body, map_rows);
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

        for (const auto& [lineno, line] : std::views::zip(std::views::iota(1UZ), splitlines(content))) {
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
                    std::string valid =
                        join(valid_directives() | std::views::transform([](const std::string& d) -> std::string {
                                 return "@revet:" + d;
                             }) | std::ranges::to<Lines>(),
                             ", ");
                    die(cat(where, "unknown directive '@revet:", key, "' — valid directives: ", valid));
                }
            }
        }

        Lines ns_out;
        // The text before the first `-- @revet:name:` is not a block.
        for (const auto& block : split_at<R"RE(--\s*@revet:name:\s*)RE">(content) | std::views::drop(1)) {
            Lines block_lines = splitlines(block);
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
            for (const auto& line : block_lines | std::views::drop(1)) {
                std::string stripped = strip(line);
                if (auto m = ctre::starts_with<R"RE(--\s*@revet:model:\s*(\w+))RE">(stripped)) {
                    model = m.get<1>().to_string();
                } else if (ctre::match<R"RE(--\s*@revet:raw\s*)RE">(stripped)) {
                    is_raw = true;
                } else if (auto r = ctre::match<R"RE(--\s*@revet:returns:\s*([^\n]+))RE">(stripped)) {
                    std::ranges::transform(
                        split_args(r.get<1>().to_view()), std::back_inserter(returns),
                        [&](const std::string& t) -> std::string { return cpp_type(t, file, name); });
                } else if (auto sc = ctre::match<R"RE(--\s*@revet:scalar:\s*(\w+\??)\s*)RE">(stripped)) {
                    scalar = cpp_type(sc.get<1>().to_string(), file, name);
                } else if (auto pm = ctre::starts_with<R"RE(--\s*@revet:param:\s*(\w+)\s+(\w+\??))RE">(stripped)) {
                    std::string pname = pm.get<1>().to_string();
                    std::string ctype = cpp_type(pm.get<2>().to_string(), file, name);
                    if (std::ranges::contains(params, pname, &std::pair<std::string, std::string>::first)) {
                        die(cat(file, ": block '", name, "': duplicate param '", pname, "'"));
                    }
                    params.emplace_back(pname, ctype);
                } else {
                    sql_lines.push_back(line);
                }
            }

            std::string sql = strip(join(sql_lines, "\n"));

            // :name references outside literals and comments, in order (repeats kept).
            std::string scrubbed = process_sql(sql, nullptr);
            Lines sql_param_refs = captures<R"RE(:([A-Za-z_]\w*))RE">(scrubbed);
            std::set<std::string> sql_param_set(sql_param_refs.begin(), sql_param_refs.end());

            std::string struct_name;
            std::string sql_escaped;
            std::string args_decl;
            std::string call_args;
            if (!params.empty()) {
                auto declared = params | std::views::keys | std::ranges::to<std::set<std::string>>();
                if (auto unused = std::ranges::find_if(
                        params, [&](const std::string& p) -> bool { return !sql_param_set.contains(p); },
                        &std::pair<std::string, std::string>::first);
                    unused != params.end()) {
                    die(cat(file, ": block '", name, "': param '", unused->first, "' declared but not used in SQL"));
                }
                if (auto undeclared = std::ranges::find_if(
                        sql_param_set, [&](const std::string& r) -> bool { return !declared.contains(r); });
                    undeclared != sql_param_set.end()) {
                    die(cat(file, ": block '", name, "': SQL uses ':", *undeclared,
                            "' but it is not declared with @revet:param"));
                }
                sql_escaped = escape_str(process_sql(sql, &declared));
                struct_name = snake_to_camel(name) + "Params";

                ns_out.push_back(cat("struct ", struct_name, " {"));
                std::ranges::transform(params, std::back_inserter(ns_out), [](const auto& p) -> std::string {
                    return cat("    ", p.second, " ", p.first, ";");
                });
                ns_out.emplace_back("};");
                ns_out.emplace_back("");

                args_decl = "const " + struct_name + "& p";
                call_args = join(sql_param_refs | std::views::transform([](const std::string& r) -> std::string {
                                     return ", p." + r;
                                 }) | std::ranges::to<Lines>(),
                                 "");
            } else if (!sql_param_set.empty()) {
                std::string refs = join(sql_param_set | std::views::transform([](const std::string& r) -> std::string {
                                            return ":" + r;
                                        }) | std::ranges::to<Lines>(),
                                        ", ");
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
            append(ns_out, emitted);
        }

        if (!ns_out.empty()) {
            out.push_back("namespace " + ns + " {");
            out.emplace_back("");
            append(out, ns_out);
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

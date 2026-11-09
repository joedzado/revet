#!/usr/bin/env python3
"""Parses queries/*.sql and generates build/generated/queries.hpp.

Each `-- @revet:name: foo` block becomes `queries::<file>::foo(db, args...)`.

Directives (per block):
  -- @revet:name: <fn>           (required) function name
  -- @revet:model: <Type>        (required for SELECT) returns std::vector<Type> via from_row<T>
  -- @revet:raw                  (SELECT only) suppress model requirement; returns raw Result
  -- @revet:returns: <t>, <t>... (SELECT only) typed rows: std::vector<std::tuple<...>>
  -- @revet:scalar: <t>          (SELECT only) first column of the first row: std::optional<T>
  -- @revet:param: <name> <type> (repeatable) named parameter; SQL must use :name placeholders

A type ending in '?' is nullable: `string?` is std::optional<std::string>, bound as NULL when empty
(params) or read from a NULL column (returns/scalar). `decimal` is revet::Decimal.

exec vs query is inferred from the SQL verb:
  INSERT | UPDATE | DELETE | CREATE | DROP | ALTER | TRUNCATE  →  db.exec(), void
  SELECT                                                         →  db.query()
  WITH ... (CTE)                                                 →  by the statement after the CTEs

Named params: declare with @revet:param, use :name in SQL.
  Codegen generates a typed params struct and replaces :name → ? for binding.
  Supported types: int, int32, int64, uint32, uint64, double, float, string, bool

Plain -- comments are passed through to the SQL string unchanged.
"""
import sys, re
from pathlib import Path
from codegen import tmpl as _tmpl, file_header, setup, write_lines, write_file, escape_str

_PARAMS_STRUCT = _tmpl("queries_params_struct.tmpl")


def _process_sql(sql: str, params: set | None = None) -> str:
    """Scrub mode (params=None): blank string literals and -- comments (same length).
    Sub mode (params=set): replace :name → ? outside literals/comments."""
    result = []
    i = 0
    while i < len(sql):
        if sql[i] == '-' and i + 1 < len(sql) and sql[i + 1] == '-':
            while i < len(sql) and sql[i] != '\n':
                result.append(' ' if params is None else sql[i])
                i += 1
        elif sql[i] == "'":
            result.append(' ' if params is None else sql[i])
            i += 1
            while i < len(sql) and sql[i] != "'":
                result.append(' ' if params is None else sql[i])
                i += 1
            if i < len(sql):
                result.append(' ' if params is None else sql[i])
                i += 1
        elif params is not None and sql[i] == ':':
            m = re.match(r':([A-Za-z_]\w*)', sql[i:])
            if m and m.group(1) in params:
                result.append('?')
                i += len(m.group(0))
            else:
                result.append(sql[i])
                i += 1
        else:
            result.append(sql[i])
            i += 1
    return ''.join(result)

EXEC_VERBS = {"insert", "update", "delete", "create", "drop", "alter", "truncate"}
VALID_DIRECTIVES = {"name", "model", "raw", "param", "returns", "scalar"}

PARAM_TYPE_MAP = {
    "int":    "int",
    "int32":  "int32_t",
    "int64":  "int64_t",
    "uint32": "uint32_t",
    "uint64": "uint64_t",
    "double": "double",
    "float":  "float",
    "string": "std::string",
    "bool":   "bool",
    "decimal": "revet::Decimal",
}


def cpp_type(t: str, file: str, name: str) -> str:
    """'int64' -> 'int64_t', 'string?' -> 'std::optional<std::string>'."""
    nullable = t.endswith("?")
    base = t[:-1] if nullable else t
    if base not in PARAM_TYPE_MAP:
        die(f"{file}: block '{name}': unknown type '{t}' — valid types: {', '.join(sorted(PARAM_TYPE_MAP))} (add '?' for nullable)")
    ctype = PARAM_TYPE_MAP[base]
    return f"std::optional<{ctype}>" if nullable else ctype


def _main_verb_after_with(sql: str) -> str | None:
    """For `WITH a AS (...), b AS (...) <stmt>`, the first verb of <stmt> outside parentheses."""
    scrubbed = _process_sql(sql)
    depth = 0
    for m in re.finditer(r"[()]|\b(select|insert|update|delete)\b", scrubbed, re.IGNORECASE):
        tok = m.group(0)
        if tok == "(":
            depth += 1
        elif tok == ")":
            depth -= 1
        elif depth == 0:
            return tok.lower()
    return None

def snake_to_camel(s: str) -> str:
    return "".join(w.capitalize() for w in s.split("_"))

def die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)

def infer_verb(sql: str, file: str, name: str) -> str:
    for line in sql.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("--"):
            continue
        token = re.split(r"\s+", stripped)[0].lower()
        if token in EXEC_VERBS:
            return "exec"
        if token in ("select", "values", "pragma"):
            return "query"
        if token == "with":
            main = _main_verb_after_with(sql)
            if main is None:
                die(f"{file}: block '{name}': WITH has no statement after its CTEs")
            # A data-modifying CTE statement with RETURNING still yields rows.
            if main != "select" and not re.search(r"\breturning\b", _process_sql(sql), re.IGNORECASE):
                return "exec"
            return "query"
        die(f"{file}: block '{name}': unrecognised SQL verb '{token}' — expected SELECT, WITH, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, or TRUNCATE")
    die(f"{file}: block '{name}': no SQL statement found")

def _opt_args(args_decl: str) -> str:
    return (", " + args_decl) if args_decl else ""


def _emit_pair(fn, opts, async_ret, sync_ret, async_body, sync_body) -> list:
    return [
        f"inline auto {fn}(auto& db{opts}) -> {async_ret} {{",
        *async_body, "}", "",
        f"inline auto sync_{fn}(auto& db{opts}) -> {sync_ret} {{",
        *sync_body, "}", "",
    ]

def emit_exec(fn: str, db_call: str, args_decl: str, call_args: str) -> list:
    opts = _opt_args(args_decl)
    call = f'co_await db.async_exec("{db_call}"{call_args});'
    sync = f'db.exec("{db_call}"{call_args});'
    return _emit_pair(fn, opts, "web::Task<void>", "void", [f"    {call}"], [f"    {sync}"])

def emit_query_raw(fn: str, db_call: str, args_decl: str, call_args: str) -> list:
    opts = _opt_args(args_decl)
    call = f'co_await db.async_query("{db_call}"{call_args})'
    sync = f'db.query("{db_call}"{call_args})'
    return _emit_pair(fn, opts, "web::Task<db::Result>", "db::Result",
                      [f"    co_return {call};"], [f"    return {sync};"])

def emit_query_typed(fn: str, db_call: str, args_decl: str, call_args: str, cols: list) -> list:
    opts = _opt_args(args_decl)
    types = ", ".join(cols)
    ret = f"std::vector<std::tuple<{types}>>"
    return _emit_pair(fn, opts, f"web::Task<{ret}>", ret,
                      [f'    auto raw = co_await db.async_query_nullable("{db_call}"{call_args});',
                       f"    co_return db::typed<{types}>(raw);"],
                      [f'    return db::typed<{types}>(db.query_nullable("{db_call}"{call_args}));'])

def emit_query_scalar(fn: str, db_call: str, args_decl: str, call_args: str, col: str) -> list:
    opts = _opt_args(args_decl)
    # A nullable scalar type is already optional; don't wrap it twice.
    ret = col if col.startswith("std::optional<") else f"std::optional<{col}>"
    inner = col[len("std::optional<"):-1] if col.startswith("std::optional<") else col
    body = [f"    if (raw.empty() || raw[0].empty() || !raw[0][0]) return std::nullopt;",
            f"    return db::detail::convert<{inner}>(raw[0][0], 0);"]
    return _emit_pair(fn, opts, f"web::Task<{ret}>", ret,
                      [f'    auto raw = co_await db.async_query_nullable("{db_call}"{call_args});',
                       *[l.replace("return", "co_return") for l in body]],
                      [f'    auto raw = db.query_nullable("{db_call}"{call_args});', *body])

def emit_query_model(fn: str, db_call: str, args_decl: str, call_args: str, mdl: str) -> list:
    opts = _opt_args(args_decl)
    call = f'co_await db.async_query("{db_call}"{call_args})'
    sync = f'db.query("{db_call}"{call_args})'
    map_rows = [
        f"    std::vector<{mdl}> out;",
        f"    out.reserve(raw.size());",
        f"    for (auto& r : raw) out.push_back(revet::json::from_row<{mdl}>(r));",
    ]
    return _emit_pair(fn, opts, f"web::Task<std::vector<{mdl}>>", f"std::vector<{mdl}>",
                      [f"    auto raw = {call};", *map_rows, "    co_return out;"],
                      [f"    auto raw = {sync};", *map_rows, "    return out;"])


def main() -> None:
    PROJ, OUT = setup("queries.hpp")

    QUERIES_DIR = PROJ / "app" / "queries"
    sql_files = sorted(QUERIES_DIR.glob("*.sql")) if QUERIES_DIR.exists() else []
    if not sql_files:
        write_file(OUT, "// no queries/*.sql files found\n#pragma once\n")
        sys.exit(0)

    out = file_header('#include <revet/db.hpp>', '#include <revet/task.hpp>',
                      '#include <revet/decimal.hpp>', '#include "models.hpp"',
                      "#include <optional>", "#include <string>", "#include <tuple>",
                      "#include <utility>", "#include <vector>")
    out += ["namespace queries {", ""]

    for sql_file in sql_files:
        ns      = sql_file.stem
        content = sql_file.read_text()
        lines   = content.splitlines()

        for lineno, line in enumerate(lines, 1):
            stripped = line.strip()
            if re.match(r"--\s*exec\s*$", stripped):
                die(f"{sql_file.name}:{lineno}: '-- exec' is not a valid directive — exec/query is inferred from the SQL verb")
            if re.match(r"--\s*columns\s*:", stripped):
                die(f"{sql_file.name}:{lineno}: '-- columns:' is not a valid directive — use '-- @revet:model: <Type>'")
            m = re.match(r"--\s*@revet:(\w+)", stripped)
            if m:
                key = m.group(1)
                if key not in VALID_DIRECTIVES:
                    die(f"{sql_file.name}:{lineno}: unknown directive '@revet:{key}' — valid directives: {', '.join(f'@revet:{d}' for d in sorted(VALID_DIRECTIVES))}")

        blocks = re.split(r"--\s*@revet:name:\s*", content)

        ns_out = []
        for block in blocks[1:]:
            block_lines = block.splitlines()
            name      = block_lines[0].strip()
            rest_lines = block_lines[1:]

            model     = None
            is_raw    = False
            returns   = None
            scalar    = None
            params    = {}   # OrderedDict-like: insertion order preserved (Python 3.7+)
            sql_lines = []

            for line in rest_lines:
                stripped = line.strip()
                if m := re.match(r"--\s*@revet:model:\s*(\w+)", stripped):
                    model = m.group(1)
                elif re.match(r"--\s*@revet:raw\s*$", stripped):
                    is_raw = True
                elif m := re.match(r"--\s*@revet:returns:\s*(.+)$", stripped):
                    returns = [cpp_type(t.strip(), sql_file.name, name) for t in m.group(1).split(",") if t.strip()]
                elif m := re.match(r"--\s*@revet:scalar:\s*(\w+\??)\s*$", stripped):
                    scalar = cpp_type(m.group(1), sql_file.name, name)
                elif m := re.match(r"--\s*@revet:param:\s*(\w+)\s+(\w+\??)", stripped):
                    pname, ptype = m.group(1), m.group(2)
                    if pname in params:
                        die(f"{sql_file.name}: block '{name}': duplicate param '{pname}'")
                    params[pname] = cpp_type(ptype, sql_file.name, name)
                else:
                    sql_lines.append(line)

            sql = "\n".join(sql_lines).strip()

            # Detect :name refs in SQL outside string literals and -- comments
            sql_scrubbed   = _process_sql(sql)
            sql_param_refs = re.findall(r':([A-Za-z_]\w*)', sql_scrubbed)
            sql_param_set  = set(sql_param_refs)

            if params:
                # Validate declared params match SQL refs
                for pname in params:
                    if pname not in sql_param_set:
                        die(f"{sql_file.name}: block '{name}': param '{pname}' declared but not used in SQL")
                for ref in sql_param_set:
                    if ref not in params:
                        die(f"{sql_file.name}: block '{name}': SQL uses ':{ref}' but it is not declared with @revet:param")
                # Replace :name → ? only outside string literals and -- comments
                sql_positional = _process_sql(sql, set(params))
                sql_escaped = escape_str(sql_positional)
                # Arg list in order of SQL references (repeats = repeated arg)
                arg_list = ", ".join(f"p.{ref}" for ref in sql_param_refs)
                struct_name = f"{snake_to_camel(name)}Params"
                # Emit struct
                fields = "\n".join(f"    {ctype} {pname};" for pname, ctype in params.items())
                ns_out.append(_PARAMS_STRUCT.substitute(struct_name=struct_name, fields=fields))
                ns_out.append("")
            elif sql_param_set:
                # sql_param_set already comes from the scrubbed SQL (safe)
                die(
                    f"{sql_file.name}: block '{name}': SQL uses named params "
                    f"({', '.join(':'+r for r in sorted(sql_param_set))}) "
                    f"but no '@revet:param' directives are declared"
                )
            else:
                sql_escaped = escape_str(sql)

            verb = infer_verb(sql, sql_file.name, name)

            shapes = [x for x in (model and "model", is_raw and "raw", returns and "returns", scalar and "scalar") if x]
            if len(shapes) > 1:
                die(f"{sql_file.name}: block '{name}': pick one of @revet:model, @revet:raw, @revet:returns, @revet:scalar (got {', '.join(shapes)})")
            if not shapes and verb == "query":
                die(
                    f"{sql_file.name}: block '{name}': SELECT query is missing '-- @revet:model: <Type>'.\n"
                    f"  Add '-- @revet:model: <Type>' to map results to a struct, '-- @revet:returns: <types>' for typed tuples,\n"
                    f"  '-- @revet:scalar: <type>' for one value, or '-- @revet:raw' to return vector<vector<string>>."
                )

            # ── arg strings ──────────────────────────────────────────────────
            if params:
                args_decl = f"const {struct_name}& p"
                call_args = ", " + ", ".join(f"p.{ref}" for ref in sql_param_refs)
            else:
                args_decl = ""
                call_args = ""

            if verb == "exec":
                ns_out += emit_exec(name, sql_escaped, args_decl, call_args)
            elif model:
                ns_out += emit_query_model(name, sql_escaped, args_decl, call_args, model)
            elif returns:
                ns_out += emit_query_typed(name, sql_escaped, args_decl, call_args, returns)
            elif scalar:
                ns_out += emit_query_scalar(name, sql_escaped, args_decl, call_args, scalar)
            else:
                ns_out += emit_query_raw(name, sql_escaped, args_decl, call_args)

        if ns_out:
            out.append(f"namespace {ns} {{")
            out.append("")
            out += ns_out
            out += [f"}} // namespace {ns}", ""]

    out += ["} // namespace queries", ""]

    write_lines(OUT, out)


if __name__ == "__main__":
    main()

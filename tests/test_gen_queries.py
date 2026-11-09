#!/usr/bin/env python3
"""Unit tests for gen_queries.py codegen."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_PY = sys.executable

CMAKE_DIR = Path(__file__).parent.parent / "cmake"
GEN_QUERIES = CMAKE_DIR / "gen_queries.py"


def run_gen(project_dir: Path, *, expect_failure=False) -> tuple[str, str, int]:
    result = subprocess.run(
        [_PY, str(GEN_QUERIES), str(project_dir)],
        capture_output=True, text=True,
    )
    if not expect_failure and result.returncode != 0:
        raise AssertionError(f"gen_queries.py failed unexpectedly:\n{result.stderr}")
    return result.stdout, result.stderr, result.returncode


def scaffold(tmp: Path, sql: str) -> Path:
    (tmp / "app" / "queries").mkdir(parents=True)
    (tmp / "app" / "queries" / "q.sql").write_text(sql)
    return tmp


def output(tmp: Path, sql: str) -> str:
    scaffold(tmp, sql)
    out, _, _ = run_gen(tmp)
    return (tmp / "build" / "generated" / "queries.hpp").read_text()


def gen(sql: str) -> str:
    with tempfile.TemporaryDirectory() as td:
        return output(Path(td), sql)


def gen_err(sql: str) -> tuple[str, str, int]:
    with tempfile.TemporaryDirectory() as td:
        return run_gen(scaffold(Path(td), sql), expect_failure=True)


class TestVerbInference(unittest.TestCase):
    def test_insert_generates_exec(self):
        hpp = gen("-- @revet:name: create\nINSERT INTO t (a) VALUES (?);\n")
        self.assertIn("db.exec(", hpp)
        self.assertNotIn("db.query(", hpp)

    def test_delete_generates_exec(self):
        self.assertIn("db.exec(", gen("-- @revet:name: remove\nDELETE FROM t WHERE id = ?;\n"))

    def test_update_generates_exec(self):
        self.assertIn("db.exec(", gen("-- @revet:name: upd\nUPDATE t SET a = ? WHERE id = ?;\n"))

    def test_create_generates_exec(self):
        self.assertIn("db.exec(", gen("-- @revet:name: init\nCREATE TABLE IF NOT EXISTS t (id INTEGER);\n"))

    def test_drop_generates_exec(self):
        self.assertIn("db.exec(", gen("-- @revet:name: drop\nDROP TABLE IF EXISTS t;\n"))

    def test_alter_generates_exec(self):
        self.assertIn("db.exec(", gen("-- @revet:name: migrate\nALTER TABLE t ADD COLUMN x TEXT;\n"))

    def test_truncate_generates_exec(self):
        self.assertIn("db.exec(", gen("-- @revet:name: clear\nTRUNCATE TABLE t;\n"))

    def test_select_with_model_generates_query(self):
        hpp = gen("-- @revet:name: all\n-- @revet:model: Thing\nSELECT id FROM t;\n")
        self.assertIn("db.query(", hpp)
        self.assertNotIn("db.exec(", hpp)


class TestModelDirective(unittest.TestCase):
    def test_model_returns_typed_vector(self):
        hpp = gen("-- @revet:name: all\n-- @revet:model: User\nSELECT id, name FROM users;\n")
        self.assertIn("std::vector<User>", hpp)
        self.assertIn("revet::json::from_row<User>(r)", hpp)

    def test_model_emits_async_default(self):
        hpp = gen("-- @revet:name: find\n-- @revet:model: User\n-- @revet:param: id int\nSELECT id FROM t WHERE id = :id;\n")
        self.assertIn("-> web::Task<std::vector<User>>", hpp)
        self.assertIn("co_await db.async_query(", hpp)
        self.assertIn("co_return out;", hpp)
        self.assertIn("inline auto sync_find(", hpp)
        self.assertIn("-> std::vector<User>", hpp)


class TestRawDirective(unittest.TestCase):
    def test_raw_returns_result_no_error(self):
        hpp = gen("-- @revet:name: count\n-- @revet:raw\nSELECT COUNT(*) FROM t;\n")
        self.assertIn("db.query(", hpp)
        self.assertNotIn("std::vector<", hpp)

    def test_raw_emits_async_default(self):
        hpp = gen("-- @revet:name: dump\n-- @revet:raw\nSELECT * FROM t;\n")
        self.assertIn("-> web::Task<db::Result>", hpp)
        self.assertIn("co_await db.async_query(", hpp)
        self.assertIn("inline auto sync_dump(", hpp)
        self.assertIn("-> db::Result", hpp)


class TestValidationErrors(unittest.TestCase):
    def test_select_missing_model_and_raw_is_error(self):
        _, stderr, code = gen_err("-- @revet:name: all\nSELECT id FROM t;\n")
        self.assertNotEqual(code, 0)
        self.assertIn("@revet:model:", stderr)
        self.assertIn("@revet:raw", stderr)
        self.assertIn("all", stderr)

    def test_old_exec_directive_is_error(self):
        _, stderr, code = gen_err("-- @revet:name: init\n-- exec\nCREATE TABLE t (id INTEGER);\n")
        self.assertNotEqual(code, 0)
        self.assertIn("-- exec", stderr)

    def test_old_columns_directive_is_error(self):
        _, stderr, code = gen_err("-- @revet:name: all\n-- columns: id, name\nSELECT id, name FROM t;\n")
        self.assertNotEqual(code, 0)
        self.assertIn("columns", stderr)
        self.assertIn("@revet:model:", stderr)

    def test_unknown_revet_directive_is_error(self):
        _, stderr, code = gen_err("-- @revet:name: all\n-- @revet:execc\nSELECT id FROM t;\n")
        self.assertNotEqual(code, 0)
        self.assertIn("@revet:execc", stderr)
        self.assertIn("valid directives", stderr)

    def test_plain_comment_passes_through(self):
        hpp = gen("-- @revet:name: all\n-- @revet:raw\n-- just a regular comment\nSELECT id FROM t;\n")
        self.assertIn("just a regular comment", hpp)


class TestNamespace(unittest.TestCase):
    def test_namespace_derived_from_filename(self):
        with tempfile.TemporaryDirectory() as td:
            proj = Path(td)
            (proj / "app" / "queries").mkdir(parents=True)
            (proj / "app" / "queries" / "users.sql").write_text("-- @revet:name: all\n-- @revet:raw\nSELECT 1;\n")
            run_gen(proj)
            hpp = (proj / "build" / "generated" / "queries.hpp").read_text()
            self.assertIn("namespace users {", hpp)
            self.assertIn("inline auto all(", hpp)


class TestBoilerplate(unittest.TestCase):
    def test_required_includes(self):
        hpp = gen("-- @revet:name: noop\n-- @revet:raw\nSELECT 1;\n")
        for header in ("#include <utility>", '#include "models.hpp"'):
            with self.subTest(header=header):
                self.assertIn(header, hpp)


class TestNamedParams(unittest.TestCase):
    def test_param_generates_struct(self):
        hpp = gen(
            "-- @revet:name: find_user\n"
            "-- @revet:raw\n"
            "-- @revet:param: id int\n"
            "SELECT * FROM users WHERE id = :id;\n"
        )
        self.assertIn("struct FindUserParams {", hpp)
        self.assertIn("int id;", hpp)

    def test_param_function_takes_struct(self):
        hpp = gen(
            "-- @revet:name: find_user\n"
            "-- @revet:raw\n"
            "-- @revet:param: id int\n"
            "SELECT * FROM users WHERE id = :id;\n"
        )
        self.assertIn("inline auto find_user(auto& db, const FindUserParams& p)", hpp)

    def test_named_replaced_with_positional(self):
        hpp = gen(
            "-- @revet:name: find_user\n"
            "-- @revet:raw\n"
            "-- @revet:param: id int\n"
            "SELECT * FROM users WHERE id = :id;\n"
        )
        self.assertNotIn(":id", hpp)
        self.assertIn("?", hpp)

    def test_struct_name_camel_cased(self):
        hpp = gen(
            "-- @revet:name: get_by_email\n"
            "-- @revet:raw\n"
            "-- @revet:param: email string\n"
            "SELECT * FROM users WHERE email = :email;\n"
        )
        self.assertIn("struct GetByEmailParams {", hpp)

    def test_multiple_params(self):
        hpp = gen(
            "-- @revet:name: upsert\n"
            "-- @revet:param: name string\n"
            "-- @revet:param: age int\n"
            "INSERT INTO t (name, age) VALUES (:name, :age);\n"
        )
        self.assertIn("std::string name;", hpp)
        self.assertIn("int age;", hpp)
        self.assertIn("p.name, p.age", hpp)

    def test_repeated_param_in_sql(self):
        hpp = gen(
            "-- @revet:name: search\n"
            "-- @revet:raw\n"
            "-- @revet:param: q string\n"
            "SELECT * FROM t WHERE a = :q OR b = :q;\n"
        )
        # :q appears twice → p.q, p.q
        self.assertIn("p.q, p.q", hpp)

    def test_exec_with_params(self):
        hpp = gen(
            "-- @revet:name: delete_user\n"
            "-- @revet:param: id int\n"
            "DELETE FROM users WHERE id = :id;\n"
        )
        self.assertIn("db.exec(", hpp)
        self.assertIn("p.id", hpp)

    def test_select_with_model_and_params(self):
        hpp = gen(
            "-- @revet:name: find\n"
            "-- @revet:model: User\n"
            "-- @revet:param: id int\n"
            "SELECT * FROM users WHERE id = :id;\n"
        )
        self.assertIn("std::vector<User>", hpp)
        self.assertIn("p.id", hpp)

    def test_param_type_mapping(self):
        types = [
            ("int",    "int "),
            ("int64",  "int64_t "),
            ("string", "std::string "),
            ("double", "double "),
            ("bool",   "bool "),
        ]
        for sql_type, cpp_type in types:
            with self.subTest(sql_type=sql_type):
                hpp = gen(
                    f"-- @revet:name: q\n-- @revet:raw\n"
                    f"-- @revet:param: v {sql_type}\n"
                    f"SELECT * FROM t WHERE x = :v;\n"
                )
                self.assertIn(cpp_type, hpp)

    def test_sql_named_param_without_directive_is_error(self):
        _, stderr, code = gen_err(
            "-- @revet:name: q\n-- @revet:raw\nSELECT * FROM t WHERE id = :id;\n"
        )
        self.assertNotEqual(code, 0)
        self.assertIn(":id", stderr)
        self.assertIn("@revet:param", stderr)

    def test_param_declared_but_not_in_sql_is_error(self):
        _, stderr, code = gen_err(
            "-- @revet:name: q\n-- @revet:raw\n"
            "-- @revet:param: unused int\n"
            "SELECT * FROM t WHERE id = :id;\n"
        )
        self.assertNotEqual(code, 0)
        self.assertIn("unused", stderr)

    def test_duplicate_param_is_error(self):
        _, stderr, code = gen_err(
            "-- @revet:name: q\n-- @revet:raw\n"
            "-- @revet:param: id int\n"
            "-- @revet:param: id string\n"
            "SELECT * FROM t WHERE id = :id;\n"
        )
        self.assertNotEqual(code, 0)
        self.assertIn("duplicate", stderr)

    def test_unknown_param_type_is_error(self):
        _, stderr, code = gen_err(
            "-- @revet:name: q\n-- @revet:raw\n"
            "-- @revet:param: id uuid\n"
            "SELECT * FROM t WHERE id = :id;\n"
        )
        self.assertNotEqual(code, 0)
        self.assertIn("uuid", stderr)




class TestSweepAdditions(unittest.TestCase):
    """CTEs, nullable params, typed returns and scalars (gaps found in healthdb/ledger)."""

    def test_with_select_is_query(self):
        hpp = gen("-- @revet:name: totals\n-- @revet:raw\nWITH d AS (SELECT 1 AS x) SELECT x FROM d;\n")
        self.assertIn("db.query(", hpp)

    def test_with_insert_is_exec(self):
        hpp = gen("-- @revet:name: copy\nWITH src AS (SELECT a FROM t) INSERT INTO u (a) SELECT a FROM src;\n")
        self.assertIn("db.exec(", hpp)

    def test_with_nested_select_in_cte_then_update_is_exec(self):
        hpp = gen("-- @revet:name: bump\nWITH r(n) AS (SELECT count(*) FROM (SELECT 1)) UPDATE t SET n = (SELECT n FROM r);\n")
        self.assertIn("db.exec(", hpp)

    def test_with_without_statement_is_error(self):
        _, err, code = gen_err("-- @revet:name: bad\n-- @revet:raw\nWITH d AS (SELECT 1);\n")
        self.assertNotEqual(code, 0)
        self.assertIn("WITH has no statement", err)

    def test_nullable_param(self):
        hpp = gen("-- @revet:name: put\n-- @revet:param: note string?\n-- @revet:param: n int64\nUPDATE t SET note = :note WHERE n = :n;\n")
        self.assertIn("std::optional<std::string> note;", hpp)
        self.assertIn("int64_t n;", hpp)

    def test_returns_typed_tuples(self):
        hpp = gen("-- @revet:name: rows\n-- @revet:returns: string, double, int64?\nSELECT a, b, c FROM t;\n")
        self.assertIn("std::vector<std::tuple<std::string, double, std::optional<int64_t>>>", hpp)
        self.assertIn("db::typed<std::string, double, std::optional<int64_t>>", hpp)
        self.assertIn("async_query_nullable", hpp)

    def test_scalar(self):
        hpp = gen("-- @revet:name: count\n-- @revet:scalar: int64\nSELECT count(*) FROM t;\n")
        self.assertIn("-> std::optional<int64_t>", hpp)
        self.assertIn("db::detail::convert<int64_t>", hpp)

    def test_nullable_scalar_not_double_wrapped(self):
        hpp = gen("-- @revet:name: latest\n-- @revet:scalar: string?\nSELECT max(a) FROM t;\n")
        self.assertIn("-> std::optional<std::string>", hpp)
        self.assertNotIn("std::optional<std::optional", hpp)

    def test_decimal_type(self):
        hpp = gen("-- @revet:name: sums\n-- @revet:returns: decimal\nSELECT sum(a) FROM t;\n")
        self.assertIn("revet::Decimal", hpp)

    def test_two_shapes_is_error(self):
        _, err, code = gen_err("-- @revet:name: x\n-- @revet:raw\n-- @revet:scalar: int\nSELECT 1;\n")
        self.assertNotEqual(code, 0)
        self.assertIn("pick one of", err)

    def test_unknown_type_is_error(self):
        _, err, code = gen_err("-- @revet:name: x\n-- @revet:returns: money\nSELECT 1;\n")
        self.assertNotEqual(code, 0)
        self.assertIn("unknown type 'money'", err)

    def test_missing_queries_dir_is_fine(self):
        with tempfile.TemporaryDirectory() as td:
            _, _, code = run_gen(Path(td))
            self.assertEqual(code, 0)
            self.assertIn("no queries", (Path(td) / "build" / "generated" / "queries.hpp").read_text())


if __name__ == "__main__":
    unittest.main()

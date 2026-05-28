#!/usr/bin/env python3
"""Unit tests for gen_resources.py codegen."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_PY = sys.executable
CMAKE_DIR = Path(__file__).parent.parent / "cmake"
GEN_RESOURCES = CMAKE_DIR / "gen_resources.py"


def run_gen(project_dir: Path) -> tuple[str, str, int]:
    result = subprocess.run(
        [_PY, str(GEN_RESOURCES), str(project_dir)],
        capture_output=True, text=True,
    )
    return result.stdout, result.stderr, result.returncode


def gen(toml: str) -> str:
    with tempfile.TemporaryDirectory() as td:
        p = Path(td)
        (p / "revet.toml").write_text(toml)
        out, err, rc = run_gen(p)
        if rc != 0:
            raise AssertionError(f"gen_resources.py failed:\n{err}")
        return (p / "build" / "generated" / "resources.hpp").read_text()


class TestGenResources(unittest.TestCase):

    def test_no_toml_produces_empty_output(self):
        with tempfile.TemporaryDirectory() as td:
            out, _, rc = run_gen(Path(td))
            self.assertEqual(rc, 0)
            hpp = (Path(td) / "build" / "generated" / "resources.hpp").read_text()
            self.assertIn("no revet.toml", hpp)

    def test_sqlite_resource_basic(self):
        hpp = gen('[resources.mydb]\ntype = "sqlite"\n')
        self.assertIn("mydb", hpp)
        self.assertIn("db::sqlite", hpp)

    def test_sqlite_default_path(self):
        hpp = gen('[resources.mydb]\ntype = "sqlite"\n')
        self.assertIn("app/data/mydb.db", hpp)

    def test_explicit_path(self):
        hpp = gen('[resources.mydb]\ntype = "sqlite"\npath = "custom/path.db"\n')
        self.assertIn("custom/path.db", hpp)

    def test_pool_size(self):
        hpp = gen('[resources.mydb]\ntype = "sqlite"\npool_size = 8\n')
        self.assertIn("8", hpp)

    def test_thread_count_default_pool(self):
        hpp = gen('thread_count = 6\n[resources.mydb]\ntype = "sqlite"\n')
        self.assertIn("6", hpp)

    def test_postgres_resource(self):
        hpp = gen('[resources.pg]\ntype = "postgres"\nurl = "postgres://localhost/app"\n')
        self.assertIn("db::postgres", hpp)
        self.assertIn("postgres://localhost/app", hpp)

    def test_multiple_resources(self):
        hpp = gen(
            '[resources.cache]\ntype = "sqlite"\n\n'
            '[resources.main]\ntype = "sqlite"\n'
        )
        self.assertIn("cache", hpp)
        self.assertIn("main", hpp)

    def test_default_resource_generates_db_alias(self):
        hpp = gen('[resources.primary]\ntype = "sqlite"\ndefault = true\n')
        self.assertIn("db()", hpp)

    def test_no_resources_section(self):
        hpp = gen('thread_count = 4\n')
        self.assertIn("no [resources.*]", hpp)

    def test_inline_table_and_comments_dont_break_parse(self):
        # tomllib handles comments and inline tables correctly
        toml = (
            "# top-level comment\n"
            "thread_count = 2\n"
            "\n"
            "[resources.alpha]\n"
            'type = "sqlite"\n'
            "# another comment\n"
            "pool_size = 3\n"
        )
        hpp = gen(toml)
        self.assertIn("alpha", hpp)
        self.assertIn("3", hpp)

    def test_invalid_toml_fails_gracefully(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / "revet.toml").write_text("this is not valid toml ][")
            _, err, rc = run_gen(p)
            self.assertNotEqual(rc, 0)


class TestGenUploads(unittest.TestCase):

    def setUp(self):
        self.gen_uploads = CMAKE_DIR / "gen_uploads.py"

    def run_uploads(self, toml: str) -> str:
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / "revet.toml").write_text(toml)
            result = subprocess.run(
                [_PY, str(self.gen_uploads), str(p)],
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                raise AssertionError(f"gen_uploads.py failed:\n{result.stderr}")
            return (p / "build" / "generated" / "uploads.hpp").read_text()

    def test_no_toml_produces_empty_output(self):
        with tempfile.TemporaryDirectory() as td:
            result = subprocess.run(
                [_PY, str(self.gen_uploads), str(td)],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0)
            hpp = (Path(td) / "build" / "generated" / "uploads.hpp").read_text()
            self.assertIn("no [uploads.*]", hpp)

    def test_basic_upload_section(self):
        hpp = self.run_uploads("[uploads.photos]\n")
        self.assertIn("namespace photos", hpp)
        self.assertIn("app/uploads/photos/incoming", hpp)
        self.assertIn("app/uploads/photos/validated", hpp)

    def test_explicit_paths(self):
        hpp = self.run_uploads(
            '[uploads.docs]\n'
            'incoming = "tmp/docs/in"\n'
            'validated = "tmp/docs/ok"\n'
        )
        self.assertIn("tmp/docs/in", hpp)
        self.assertIn("tmp/docs/ok", hpp)

    def test_multiple_upload_sections(self):
        hpp = self.run_uploads("[uploads.images]\n\n[uploads.videos]\n")
        self.assertIn("namespace images", hpp)
        self.assertIn("namespace videos", hpp)

    def test_comment_in_toml_ignored(self):
        hpp = self.run_uploads("# comment\n[uploads.files]\n# another\n")
        self.assertIn("namespace files", hpp)

    def test_invalid_toml_fails(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / "revet.toml").write_text("[[invalid toml")
            result = subprocess.run(
                [_PY, str(self.gen_uploads), str(p)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Unit tests for gen_components.py — focuses on split_args."""

import sys
import subprocess
import tempfile
import unittest
from pathlib import Path

CMAKE_DIR = Path(__file__).parent.parent / "cmake"
sys.path.insert(0, str(CMAKE_DIR))

from codegen import split_args  # noqa: E402


class TestSplitArgs(unittest.TestCase):

    def test_empty(self):
        self.assertEqual(split_args(""), [])

    def test_single_arg(self):
        self.assertEqual(split_args("int x"), ["int x"])

    def test_two_simple_args(self):
        self.assertEqual(split_args("int x, std::string y"), ["int x", "std::string y"])

    def test_template_arg_not_split(self):
        self.assertEqual(
            split_args("std::map<std::string, int> m"),
            ["std::map<std::string, int> m"],
        )

    def test_template_arg_with_sibling(self):
        self.assertEqual(
            split_args("std::map<std::string, int> m, bool flag"),
            ["std::map<std::string, int> m", "bool flag"],
        )

    def test_nested_templates(self):
        self.assertEqual(
            split_args("std::map<std::string, std::vector<int>> m, int n"),
            ["std::map<std::string, std::vector<int>> m", "int n"],
        )

    def test_state_type(self):
        self.assertEqual(
            split_args("web::State<Theme> t, std::string_view title"),
            ["web::State<Theme> t", "std::string_view title"],
        )

    def test_whitespace_trimmed(self):
        args = split_args("  int x ,  float y  ")
        self.assertEqual(args, ["int x", "float y"])

    def test_no_spurious_empty_args(self):
        self.assertEqual(split_args("int x"), ["int x"])
        self.assertNotIn("", split_args("int x, int y"))


class TestGenComponentsIntegration(unittest.TestCase):
    """Run gen_components.py end-to-end with a template-type arg."""

    GEN = CMAKE_DIR / "gen_components.py"
    _PY = sys.executable

    def run_gen(self, proj: Path) -> str:
        result = subprocess.run(
            [self._PY, str(self.GEN), str(proj)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise AssertionError(f"gen_components.py failed:\n{result.stderr}")
        return (proj / "build" / "generated" / "components.hpp").read_text()

    def test_template_arg_preserved_in_forward_decl(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            comp_dir = p / "app" / "components"
            comp_dir.mkdir(parents=True)
            (comp_dir / "card.cpp").write_text(
                '[[revet::export]]\n'
                'html::Node render_card(std::map<std::string, int> data, std::string_view title) {\n'
                '    return {};\n'
                '}\n'
            )
            hpp = self.run_gen(p)
            # The forward decl must keep the template arg intact
            self.assertIn("std::map<std::string, int>", hpp)
            # Must not have been split into two args incorrectly
            self.assertNotIn("int> data", hpp.split("std::map<std::string,")[0]
                             if "std::map<std::string," in hpp else "")

    def test_simple_component_forward_decl(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            comp_dir = p / "app" / "components"
            comp_dir.mkdir(parents=True)
            (comp_dir / "btn.cpp").write_text(
                '[[revet::export]]\n'
                'html::Node render_btn(std::string_view label, bool disabled) {\n'
                '    return {};\n'
                '}\n'
            )
            hpp = self.run_gen(p)
            self.assertIn("render_btn", hpp)
            self.assertIn("std::string_view label", hpp)
            self.assertIn("bool disabled", hpp)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Tests for gen_scripts.py: app/scripts/*.js -> scripts.hpp, and their routes in routes.cpp."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CMAKE = Path(__file__).parent.parent / "cmake"


def run(files: dict[str, str]):
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        for name, src in files.items():
            (root / "app" / "scripts").mkdir(parents=True, exist_ok=True)
            (root / "app" / "scripts" / name).write_text(src)
        p = subprocess.run([sys.executable, str(CMAKE / "gen_scripts.py"), str(root)], capture_output=True, text=True)
        out = root / "build" / "generated" / "scripts.hpp"
        return p.returncode, (out.read_text() if out.exists() else ""), p.stderr


class TestGenScripts(unittest.TestCase):
    def test_no_scripts(self):
        code, hpp, _ = run({})
        self.assertEqual(code, 0)
        self.assertIn("namespace scripts {", hpp)

    def test_embeds_and_hashes(self):
        code, hpp, _ = run({"chart-hover.js": 'const s = "a\\\\b";\nconsole.log(s);\r\n'})
        self.assertEqual(code, 0)
        self.assertRegex(hpp, r'chart_hover = "/_app/chart-hover\.[0-9a-f]{10}\.js"')
        self.assertIn('chart_hover_js = "const s = \\"a\\\\\\\\b\\";\\nconsole.log(s);\\n"', hpp)
        self.assertIn("chart_hover_tag()", hpp)

    def test_hash_changes_with_content(self):
        _, a, _ = run({"x.js": "1"})
        _, b, _ = run({"x.js": "2"})
        self.assertNotEqual(a, b)

    def test_leading_digit_gets_valid_identifier(self):
        _, hpp, _ = run({"3d.js": "1"})
        self.assertIn("_3d =", hpp)


if __name__ == "__main__":
    unittest.main()

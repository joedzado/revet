#!/usr/bin/env python3
"""Tests for gen_server.py: revet.toml max_body → build/generated/server.hpp."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parent.parent / "cmake" / "gen_server.py"


def run(toml):
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        if toml is not None:
            (root / "revet.toml").write_text(toml)
        p = subprocess.run([sys.executable, str(SCRIPT), str(root)], capture_output=True, text=True)
        out = root / "build" / "generated" / "server.hpp"
        return p.returncode, (out.read_text() if out.exists() else ""), p.stderr


class TestGenServer(unittest.TestCase):
    def test_no_toml_uses_default(self):
        code, hpp, _ = run(None)
        self.assertEqual(code, 0)
        self.assertIn("DEFAULT_MAX_BODY_BYTES applies", hpp)
        self.assertNotIn("max_body_bytes(", hpp)

    def test_unset_uses_default(self):
        code, hpp, _ = run('name = "x"\nport = 8080\n')
        self.assertEqual(code, 0)
        self.assertNotIn("max_body_bytes(", hpp)

    def test_string_size(self):
        code, hpp, _ = run('name = "x"\nmax_body = "256MB"\n')
        self.assertEqual(code, 0)
        self.assertIn(f"max_body_bytes({256 * 1024 * 1024}UL)", hpp)

    def test_kib_and_gib(self):
        self.assertIn(f"max_body_bytes({512 * 1024}UL)", run('max_body = "512KiB"\n')[1])
        self.assertIn(f"max_body_bytes({1024 ** 3}UL)", run('max_body = "1g"\n')[1])

    def test_integer_bytes(self):
        self.assertIn("max_body_bytes(1048576UL)", run("max_body = 1048576\n")[1])

    def test_bad_value_fails_the_build(self):
        code, _, err = run('max_body = "huge"\n')
        self.assertNotEqual(code, 0)
        self.assertIn("not a size", err)

    def test_zero_rejected(self):
        code, _, err = run("max_body = 0\n")
        self.assertNotEqual(code, 0)


if __name__ == "__main__":
    unittest.main()

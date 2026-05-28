#!/usr/bin/env python3
"""Validates static/optimistic.ts — type-check via bun build and functional test via node."""
import shutil, subprocess, unittest
from pathlib import Path

FRAMEWORK = Path(__file__).parent.parent
TS_FILE   = FRAMEWORK / "static" / "optimistic.ts"
JS_FILE   = FRAMEWORK / "static" / "optimistic.js"
TEST_FILE = FRAMEWORK / "tests"  / "optimistic_test.js"

from pathlib import Path as _Path
BUN  = shutil.which("bun") or (str(_Path.home() / ".bun" / "bin" / "bun") if (_Path.home() / ".bun" / "bin" / "bun").exists() else None)
NODE = shutil.which("node")

EXPECTED_EXPORTS = [
    "incrInt", "decrInt", "incrFloat", "decrFloat",
    "setText", "restoreText",
    "toggleClass", "rollbackClass", "addClass", "removeClass",
    "appendPending", "prependPending", "removePending",
]


@unittest.skipUnless(BUN, "bun not installed — skipping optimistic.ts validation")
class TestOptimisticTs(unittest.TestCase):

    def test_ts_file_exists(self):
        self.assertTrue(TS_FILE.exists(), f"Missing: {TS_FILE}")

    def test_build(self):
        r = subprocess.run(
            [BUN, "build", "--minify", str(TS_FILE), "--outfile", str(JS_FILE)],
            capture_output=True, text=True,
        )
        self.assertEqual(r.returncode, 0, f"bun build failed:\n{r.stderr}")
        self.assertTrue(JS_FILE.exists(), "bun build produced no output")

    def test_all_exports_present(self):
        ts = TS_FILE.read_text()
        for fn in EXPECTED_EXPORTS:
            self.assertIn(fn, ts, f"Function missing: {fn}")


@unittest.skipUnless(NODE, "node not installed — skipping optimistic.js functional test")
class TestOptimisticJsFunctional(unittest.TestCase):

    def test_functional(self):
        self.assertTrue(JS_FILE.exists(), f"Missing compiled JS: {JS_FILE} (run cmake configure first)")
        r = subprocess.run(["node", str(TEST_FILE)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, f"optimistic_test.js failed:\n{r.stdout}\n{r.stderr}")


if __name__ == "__main__":
    unittest.main()

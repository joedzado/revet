#!/usr/bin/env python3
"""Unit tests for gen_dependencies.py — find_package name resolution from
Conan's CMakeDeps output when the CMake package name differs from the Conan name."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_PY = sys.executable
GEN_DEPS = Path(__file__).parent.parent / "cmake" / "gen_dependencies.py"


def gen(toml: str, conan_files: dict = None) -> str:
    """Run gen_dependencies.py on a scaffold; conan_files maps build/conan names to content."""
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        (tmp / "revet.toml").write_text(toml)
        if conan_files is not None:
            conan = tmp / "build" / "conan"
            conan.mkdir(parents=True)
            for name, content in conan_files.items():
                (conan / name).write_text(content)
        result = subprocess.run([_PY, str(GEN_DEPS), str(tmp)], capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(f"gen_dependencies.py failed:\n{result.stderr}")
        return (tmp / "build" / "generated" / "dependencies.cmake").read_text()


DEP_TOML = '[build.dependencies]\nexamplelib = "examplelib/1.0.0"\n'

# What Conan's CMakeDeps writes for a package whose CMake name is ExampleLib.
DATA_DIFFERENT_NAME = (
    "set(examplelib_PACKAGE_FOLDER_RELEASE \"/pkg\")\n"
    "list(APPEND examplelib_COMPONENT_NAMES ExampleLib::core ExampleLib::extra)\n"
)


class TestCMakeNameResolution(unittest.TestCase):

    def test_cmake_name_differs_from_conan_name(self):
        out = gen(DEP_TOML, {"ExampleLib-release-armv8-data.cmake": DATA_DIFFERENT_NAME})
        self.assertIn("find_package(ExampleLib REQUIRED)", out)
        self.assertNotIn("find_package(examplelib REQUIRED)", out)
        self.assertIn("target_link_libraries(revet_core INTERFACE examplelib::examplelib)", out)

    def test_components_forwarded_to_root_target(self):
        out = gen(DEP_TOML, {"ExampleLib-release-armv8-data.cmake": DATA_DIFFERENT_NAME})
        self.assertIn("if(DEFINED examplelib_COMPONENT_NAMES AND TARGET examplelib::examplelib)", out)
        self.assertIn("INTERFACE_LINK_LIBRARIES ${examplelib_COMPONENT_NAMES})", out)

    def test_module_files_ignored(self):
        out = gen(DEP_TOML, {
            "module-ExampleLib-release-armv8-data.cmake": "set(examplelib_PACKAGE_FOLDER_RELEASE x)\n",
            "ExampleLib-release-armv8-data.cmake": DATA_DIFFERENT_NAME,
        })
        self.assertIn("find_package(ExampleLib REQUIRED)", out)

    def test_same_name_unchanged(self):
        out = gen('[build.dependencies]\nfmt = "fmt/10.1.1"\n', {
            "fmt-release-armv8-data.cmake": "set(fmt_PACKAGE_FOLDER_RELEASE \"/pkg\")\n",
        })
        self.assertIn("find_package(fmt REQUIRED)", out)
        self.assertIn("INTERFACE fmt::fmt)", out)

    def test_no_conan_dir_falls_back_to_package_name(self):
        out = gen(DEP_TOML, None)
        self.assertIn("find_package(examplelib REQUIRED)", out)
        self.assertIn("INTERFACE examplelib::examplelib)", out)


if __name__ == "__main__":
    unittest.main()

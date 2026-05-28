#!/usr/bin/env python3
"""Unit tests for gen_states.py codegen."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_PY = sys.executable
CMAKE_DIR = Path(__file__).parent.parent / "cmake"
GEN_STATES = CMAKE_DIR / "gen_states.py"


def scaffold(tmp: Path, route_src: str, model_src: str | None = None) -> Path:
    routes = tmp / "app" / "routes"
    routes.mkdir(parents=True)
    (routes / "index.cpp").write_text(route_src)
    if model_src is not None:
        models = tmp / "models"
        models.mkdir(parents=True)
        (models / "models.hpp").write_text(model_src)
    return tmp


def gen(route_src: str, model_src: str | None = None) -> str:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        scaffold(tmp, route_src, model_src)
        result = subprocess.run(
            [_PY, str(GEN_STATES), str(tmp)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise AssertionError(f"gen_states.py failed:\n{result.stderr}")
        return (tmp / "build" / "generated" / "states.hpp").read_text()


class TestGenStates(unittest.TestCase):

    def test_no_state_usage_produces_minimal_output(self):
        hpp = gen("// no state usage here")
        self.assertIn("namespace web::states", hpp)

    def test_string_field_packed_and_unpacked(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Session> s) {}',
            'struct Session { std::string username; };'
        )
        self.assertIn("pack_plain_Session", hpp)
        self.assertIn("unpack_plain_Session", hpp)
        self.assertIn("username", hpp)

    def test_bool_field_packed(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Prefs> s) {}',
            'struct Prefs { bool dark_mode; };'
        )
        self.assertIn("dark_mode", hpp)

    def test_uint32_field_packed(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Counter> s) {}',
            'struct Counter { uint32_t count; };'
        )
        self.assertIn("count", hpp)

    def test_flash_type_recognized(self):
        hpp = gen(
            'void render_get(const web::Request&, web::Flash<Alert> a) {}',
            'struct Alert { std::string message; };'
        )
        self.assertIn("pack_plain_Alert", hpp)
        self.assertIn("message", hpp)

    def test_multiple_state_types(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<User> u, web::State<Theme> t) {}',
            'struct User { std::string name; };\nstruct Theme { bool dark; };'
        )
        self.assertIn("pack_plain_User", hpp)
        self.assertIn("pack_plain_Theme", hpp)

    def test_encrypted_pack_wraps_plain(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Session> s) {}',
            'struct Session { std::string token; };'
        )
        self.assertIn("crypto::encrypt", hpp)
        self.assertIn("crypto::decrypt", hpp)

    def test_int64_field_packed(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Session> s) {}',
            'struct Session { int64_t created_at; };'
        )
        pack_body = hpp.split("inline std::string pack_plain_Session")[1].split("}")[0]
        self.assertIn("created_at", pack_body)

    def test_float_field_packed(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Stats> s) {}',
            'struct Stats { float score; };'
        )
        pack_body = hpp.split("inline std::string pack_plain_Stats")[1].split("}")[0]
        self.assertIn("score", pack_body)

    def test_double_field_packed(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Stats> s) {}',
            'struct Stats { double value; };'
        )
        pack_body = hpp.split("inline std::string pack_plain_Stats")[1].split("}")[0]
        self.assertIn("value", pack_body)

    def test_int32_field_packed(self):
        hpp = gen(
            'void render_get(const web::Request&, web::State<Item> s) {}',
            'struct Item { int32_t id; };'
        )
        pack_body = hpp.split("inline std::string pack_plain_Item")[1].split("}")[0]
        self.assertIn("id", pack_body)


if __name__ == "__main__":
    unittest.main()

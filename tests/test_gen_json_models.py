#!/usr/bin/env python3
"""Unit tests for gen_json_models.py parser and codegen."""
import sys
import unittest
import tempfile
import shutil
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "cmake"))

import gen_json_models as gjm


def run_on(proj: Path):
    """Invoke the generator on a fake project dir and return generated text."""
    gjm.PROJ = proj
    gjm.MODELS_DIR = proj / "json_models"
    gjm.OUT = proj / "build" / "generated" / "json_models.hpp"
    models = gjm.scan_models()
    gjm.OUT.parent.mkdir(parents=True, exist_ok=True)
    gjm.OUT.write_text(gjm.generate(models))
    return gjm.OUT.read_text(), models


class ScanningTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        (self.tmp / "json_models").mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_detects_prefix_attribute(self):
        (self.tmp / "json_models" / "user.hpp").write_text(
            "[[revet::model]]\nstruct User { std::string name; int age; };\n"
        )
        _, models = run_on(self.tmp)
        self.assertEqual(len(models), 1)
        _, name, fields, _ = models[0]
        self.assertEqual(name, "User")
        self.assertEqual(fields, [("std::string", "name"), ("int", "age")])

    def test_detects_infix_attribute(self):
        (self.tmp / "json_models" / "user.hpp").write_text(
            "struct [[revet::model]] User { std::string name; };\n"
        )
        _, models = run_on(self.tmp)
        self.assertEqual(len(models), 1)
        self.assertEqual(models[0][1], "User")

    def test_attribute_args_parsed(self):
        (self.tmp / "json_models" / "u.hpp").write_text(
            "[[revet::model(row, pretty)]]\nstruct U { int x; };\n"
        )
        _, models = run_on(self.tmp)
        self.assertEqual(models[0][3], {"row", "pretty"})

    def test_struct_without_attribute_ignored(self):
        (self.tmp / "json_models" / "plain.hpp").write_text(
            "struct Plain { int x; };\n"
        )
        _, models = run_on(self.tmp)
        self.assertEqual(models, [])

    def test_field_parsing_skips_methods_and_defaults(self):
        (self.tmp / "json_models" / "m.hpp").write_text(
            "[[revet::model]]\n"
            "struct M {\n"
            "  int count = 0;\n"
            "  std::string name;\n"
            "  void do_something();\n"
            "};\n"
        )
        _, models = run_on(self.tmp)
        fields = models[0][2]
        self.assertEqual(fields, [("int", "count"), ("std::string", "name")])

    def test_no_models_dir_empty_output(self):
        shutil.rmtree(self.tmp / "json_models")
        text, models = run_on(self.tmp)
        self.assertEqual(models, [])
        self.assertIn("#pragma once", text)
        self.assertIn("namespace revet::json", text)


class CodegenTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        (self.tmp / "json_models").mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_emits_from_json_specialization(self):
        (self.tmp / "json_models" / "eq.hpp").write_text(
            "[[revet::model]]\n"
            "struct Earthquake { std::string id; double mag; int64_t time; };\n"
        )
        text, _ = run_on(self.tmp)
        self.assertIn("from_json_value<Earthquake>", text)
        self.assertIn("from_json<Earthquake>", text)
        self.assertIn('v.at("id")', text)
        self.assertIn('v.at("mag")', text)
        self.assertIn('static_cast<double>', text)

    def test_emits_to_json_specialization(self):
        (self.tmp / "json_models" / "p.hpp").write_text(
            "[[revet::model]]\nstruct P { std::string name; int age; bool alive; };\n"
        )
        text, _ = run_on(self.tmp)
        self.assertIn("to_json<P>", text)
        self.assertIn('\\"name\\":', text)
        self.assertIn('\\"alive\\":', text)
        self.assertIn('? "true" : "false"', text)

    def test_vector_field_deser(self):
        (self.tmp / "json_models" / "p.hpp").write_text(
            "[[revet::model]]\nstruct P { std::vector<std::string> tags; };\n"
        )
        text, _ = run_on(self.tmp)
        self.assertIn("operator std::vector<std::string>()", text)

    def test_optional_field_deser(self):
        (self.tmp / "json_models" / "p.hpp").write_text(
            "[[revet::model]]\nstruct P { std::optional<int32_t> age; };\n"
        )
        text, _ = run_on(self.tmp)
        self.assertIn('v.has("age") ? std::optional<int32_t>(static_cast<int32_t>(v.at("age"))) : std::nullopt', text)

    def test_optional_field_ser_writes_null(self):
        (self.tmp / "json_models" / "p.hpp").write_text(
            "[[revet::model]]\nstruct P { std::optional<std::string> bio; };\n"
        )
        text, _ = run_on(self.tmp)
        self.assertIn("has_value()", text)
        self.assertIn('out += "null"', text)

    def test_emits_from_row_specialization(self):
        (self.tmp / "json_models" / "eq.hpp").write_text(
            "[[revet::model]]\n"
            "struct Earthquake {\n"
            "  std::string id;\n"
            "  double magnitude;\n"
            "  int64_t timestamp;\n"
            "};\n"
        )
        text, _ = run_on(self.tmp)
        self.assertIn("from_row<Earthquake>", text)
        self.assertIn("out.id = r[0];", text)
        self.assertIn("std::stod(r[1])", text)
        self.assertIn("std::stoll(r[2])", text)

    def test_from_row_bool_and_optional(self):
        (self.tmp / "json_models" / "p.hpp").write_text(
            "[[revet::model]]\n"
            "struct P { bool active; std::optional<int32_t> age; };\n"
        )
        text, _ = run_on(self.tmp)
        self.assertIn('(r[0] == "1" || r[0] == "true")', text)
        self.assertIn("std::optional<int32_t>", text)
        self.assertIn(".empty()", text)

    def test_user_hpp_files_reincluded(self):
        (self.tmp / "json_models" / "city.hpp").write_text(
            "[[revet::model]]\nstruct City { std::string name; };\n"
        )
        (self.tmp / "json_models" / "passthrough.hpp").write_text(
            "struct Extra { int x; };\n"  # no attribute; still included
        )
        text, _ = run_on(self.tmp)
        self.assertIn('"../../json_models/city.hpp"', text)
        self.assertIn('"../../json_models/passthrough.hpp"', text)


if __name__ == "__main__":
    unittest.main()

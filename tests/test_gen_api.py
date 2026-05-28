#!/usr/bin/env python3
"""Unit tests for gen_api.py — scans app/apis/*.hpp for [[revet::external]]
function declarations and generates inline coroutine definitions."""

import sys
import unittest
import tempfile
import shutil
from pathlib import Path

# Make `cmake/` importable.
sys.path.insert(0, str(Path(__file__).parent.parent / "cmake"))


def _make_project(api_files: dict) -> Path:
    """Create a throwaway project with app/apis/*.hpp content from `api_files`."""
    root = Path(tempfile.mkdtemp(prefix="revet_gen_api_"))
    apis = root / "app" / "apis"
    apis.mkdir(parents=True)
    for name, content in api_files.items():
        (apis / name).write_text(content)
    (root / "build" / "generated").mkdir(parents=True)
    return root


def _run_gen(root: Path) -> str:
    """Re-import gen_api with sys.argv pointed at `root`, return generated text."""
    sys.argv = ["gen_api.py", str(root)]
    if "gen_api" in sys.modules:
        del sys.modules["gen_api"]
    if "codegen" in sys.modules:
        del sys.modules["codegen"]
    import gen_api  # noqa: F401
    gen_api.main()
    return (root / "build" / "generated" / "apis.hpp").read_text()


class TestExternalAttributeParsing(unittest.TestCase):
    def test_simple_get(self):
        proj = _make_project({"github.hpp": """
#pragma once
#include <revet/web.hpp>
namespace github_com {
struct [[revet::model]] UserResponse { uint32_t id; std::string login; };
[[revet::external("GET https://api.github.com/users/{login}")]]
auto user(std::string login) -> web::Task<UserResponse>;
}
"""})
        try:
            out = _run_gen(proj)
            self.assertIn("namespace github_com", out)
            self.assertIn("inline auto user(std::string login) -> web::Task<UserResponse>", out)
            self.assertIn("co_await http::get(url)", out)
            self.assertIn("revet::json::Document doc(resp.body)", out)
            self.assertIn("revet::json::from_json<UserResponse>(doc)", out)
            # URL template appears verbatim before substitution lambda kicks in.
            self.assertIn("https://api.github.com/users/{login}", out)
        finally:
            shutil.rmtree(proj)

    def test_throws_http_error_on_non_2xx(self):
        """Generated API code must surface non-2xx responses (and transport
        failures, status 0) as http::Error so callers can catch them, rather
        than silently letting JSON parsing crash on error bodies."""
        proj = _make_project({"x.hpp": """
namespace ns {
struct [[revet::model]] R { uint32_t id; };
[[revet::external("GET https://api.example.com/r")]]
auto fetch() -> web::Task<R>;
}
"""})
        try:
            out = _run_gen(proj)
            # The generated function must check resp.ok() before parsing JSON.
            self.assertIn("if (!resp.ok()) throw http::Error", out)
            # And it must propagate the upstream status/body verbatim.
            self.assertIn("resp.status_code", out)
            self.assertIn("resp.body", out)
            # Sanity: the throw is placed BEFORE the JSON parse, otherwise
            # we'd still try to parse error bodies as JSON.
            throw_idx = out.index("throw http::Error")
            parse_idx = out.index("revet::json::Document")
            self.assertLess(throw_idx, parse_idx,
                            "throw must precede JSON parse")
        finally:
            shutil.rmtree(proj)

    def test_method_mapping(self):
        cases = {
            "GET": "http::get",
            "POST": "http::post",
            "PUT": "http::put",
            "DELETE": "http::delete_",   # keyword collision → trailing underscore
            "PATCH": "http::patch",
        }
        for method, helper in cases.items():
            proj = _make_project({"x.hpp": f"""
namespace ns {{
struct [[revet::model]] R {{ uint32_t id; }};
[[revet::external("{method} https://api.example.com/things/{{id}}")]]
auto thing(uint32_t id) -> web::Task<R>;
}}
"""})
            try:
                out = _run_gen(proj)
                self.assertIn(helper, out, f"{method} should use {helper}")
            finally:
                shutil.rmtree(proj)


class TestParamClassification(unittest.TestCase):
    def test_path_params(self):
        proj = _make_project({"a.hpp": """
namespace ex {
struct [[revet::model]] R { std::string title; };
[[revet::external("GET https://api.example.com/repos/{owner}/{repo}")]]
auto repo(std::string owner, std::string repo) -> web::Task<R>;
}
"""})
        try:
            out = _run_gen(proj)
            # Both substitutions should appear with their placeholder names.
            self.assertIn('s.find("{owner}")', out)
            self.assertIn('s.find("{repo}")', out)
            # No query-string concatenation should be generated.
            self.assertNotIn('"?owner=', out)
            self.assertNotIn('"?repo=', out)
        finally:
            shutil.rmtree(proj)

    def test_query_params(self):
        proj = _make_project({"a.hpp": """
namespace ex {
struct [[revet::model]] R { std::string title; };
[[revet::external("GET https://api.example.com/search")]]
auto search(std::string q, uint32_t page) -> web::Task<R>;
}
"""})
        try:
            out = _run_gen(proj)
            # Primitives not in URL become query params; first uses '?', rest '&'.
            self.assertIn('"?q="', out)
            self.assertIn('"&page="', out)
            # Numeric params should be std::to_string'd.
            self.assertIn("std::to_string(page)", out)
        finally:
            shutil.rmtree(proj)

    def test_mixed_path_and_query(self):
        proj = _make_project({"a.hpp": """
namespace ex {
struct [[revet::model]] R { std::string body; };
[[revet::external("GET https://api.example.com/users/{id}/posts")]]
auto posts(uint32_t id, std::string filter) -> web::Task<R>;
}
"""})
        try:
            out = _run_gen(proj)
            self.assertIn('s.find("{id}")', out)
            self.assertIn('"?filter="', out)
            # `id` should NOT be appended as a query string.
            self.assertNotIn('"?id="', out)
            self.assertNotIn('"&id="', out)
        finally:
            shutil.rmtree(proj)


class TestNamespaceHandling(unittest.TestCase):
    def test_global_namespace(self):
        proj = _make_project({"a.hpp": """
struct [[revet::model]] R { uint32_t n; };
[[revet::external("GET https://api.example.com/n")]]
auto get_n() -> web::Task<R>;
"""})
        try:
            out = _run_gen(proj)
            # Function emitted at global scope — no wrapping namespace block.
            self.assertIn("inline auto get_n()", out)
            # No empty `namespace  {` would appear from the empty key.
            self.assertNotIn("namespace  {", out)
        finally:
            shutil.rmtree(proj)

    def test_multiple_namespaces_in_one_file(self):
        proj = _make_project({"both.hpp": """
namespace a {
struct [[revet::model]] R1 { uint32_t x; };
[[revet::external("GET https://api.a.com/x")]]
auto x() -> web::Task<R1>;
}
namespace b {
struct [[revet::model]] R2 { uint32_t y; };
[[revet::external("GET https://api.b.com/y")]]
auto y() -> web::Task<R2>;
}
"""})
        try:
            out = _run_gen(proj)
            self.assertIn("namespace a {", out)
            self.assertIn("namespace b {", out)
            # Each function in its own namespace block.
            a_idx = out.index("namespace a {")
            b_idx = out.index("namespace b {")
            self.assertLess(a_idx, b_idx)
            self.assertIn("inline auto x()", out[a_idx:b_idx])
            self.assertIn("inline auto y()", out[b_idx:])
        finally:
            shutil.rmtree(proj)

    def test_sibling_after_namespace_close(self):
        """A decl outside a namespace block must not inherit the closed namespace."""
        proj = _make_project({"x.hpp": """
namespace inside {
struct [[revet::model]] R1 { uint32_t a; };
[[revet::external("GET https://api.example.com/a")]]
auto a() -> web::Task<R1>;
}
struct [[revet::model]] R2 { uint32_t b; };
[[revet::external("GET https://api.example.com/b")]]
auto b() -> web::Task<R2>;
"""})
        try:
            out = _run_gen(proj)
            ns_open  = out.index("namespace inside {")
            ns_close = out.index("} // namespace inside")
            # `a` is inside the namespace block; `b` lives at global (outside).
            self.assertIn("inline auto a()", out[ns_open:ns_close])
            outside = out[:ns_open] + out[ns_close:]
            self.assertIn("inline auto b()", outside)
            self.assertNotIn("inline auto b()", out[ns_open:ns_close])
        finally:
            shutil.rmtree(proj)

    def test_struct_braces_dont_confuse_tracker(self):
        """A struct body's braces must not pop the namespace stack."""
        proj = _make_project({"x.hpp": """
namespace ex {
struct [[revet::model]] R {
    uint32_t id;
    std::string name;
};
[[revet::external("GET https://api.example.com/r/{id}")]]
auto r(uint32_t id) -> web::Task<R>;
}
"""})
        try:
            out = _run_gen(proj)
            ns_idx = out.index("namespace ex {")
            close_idx = out.index("} // namespace ex")
            # Function must be emitted INSIDE the namespace block.
            self.assertIn("inline auto r(", out[ns_idx:close_idx])
        finally:
            shutil.rmtree(proj)


class TestGeneratedOutput(unittest.TestCase):
    def test_re_includes_user_headers(self):
        proj = _make_project({"github.hpp": """
namespace gh {
struct [[revet::model]] R { uint32_t id; };
[[revet::external("GET https://api.github.com/x")]]
auto x() -> web::Task<R>;
}
"""})
        try:
            out = _run_gen(proj)
            self.assertIn("app/apis/github.hpp", out)
        finally:
            shutil.rmtree(proj)

    def test_required_includes(self):
        proj = _make_project({"a.hpp": """
namespace ns {
struct [[revet::model]] R { uint32_t id; };
[[revet::external("GET https://api.example.com/r")]]
auto r() -> web::Task<R>;
}
"""})
        try:
            out = _run_gen(proj)
            self.assertIn("#pragma once", out)
            self.assertIn("#include <revet/http.hpp>", out)
            self.assertIn("#include <revet/json.hpp>", out)
            self.assertIn("#include <revet/task.hpp>", out)
        finally:
            shutil.rmtree(proj)

    def test_empty_project_produces_valid_header(self):
        """Project with no app/apis dir at all should still produce a parseable header."""
        root = Path(tempfile.mkdtemp(prefix="revet_gen_api_empty_"))
        (root / "build" / "generated").mkdir(parents=True)
        try:
            out = _run_gen(root)
            self.assertIn("#pragma once", out)
        finally:
            shutil.rmtree(root)


class TestRealProjectFixtures(unittest.TestCase):
    """Smoke-test against the actual template/demo projects shipped in this repo."""

    def _gen_for(self, proj_name):
        return _run_gen(Path(__file__).parent.parent / proj_name)

    def test_template(self):
        out = self._gen_for("template")
        self.assertIn("namespace usgs_gov", out)
        self.assertIn("inline auto count(std::string starttime, std::string endtime)", out)
        self.assertIn("co_await http::get(url)", out)

    def test_demo(self):
        out = self._gen_for("demo")
        self.assertIn("namespace usgs_gov", out)
        self.assertIn("inline auto count(std::string starttime, std::string endtime)", out)


if __name__ == "__main__":
    unittest.main()

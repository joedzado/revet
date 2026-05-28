#!/usr/bin/env python3
"""Unit tests for gen_routes.py — focus on /api/* handler emission,
including the async (web::Task<T>) path."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_PY = sys.executable
CMAKE_DIR = Path(__file__).parent.parent / "cmake"
GEN_ROUTES = CMAKE_DIR / "gen_routes.py"


def scaffold(tmp: Path, *, route_path: str, route_src: str, model_src: str = "") -> Path:
    """Create app/routes/<route_path>.cpp with given content. route_path
    is relative to app/routes/ and uses '/' separators; '.cpp' is appended."""
    routes = tmp / "app" / "routes"
    (routes / Path(route_path).parent).mkdir(parents=True, exist_ok=True)
    (routes / (route_path + ".cpp")).write_text(route_src)
    if model_src:
        models = tmp / "models"
        models.mkdir(parents=True, exist_ok=True)
        (models / "models.hpp").write_text(model_src)
    return tmp


def _user_block(cpp: str, path: str) -> str:
    """Return just the `{ web::Route r; r.path = "<path>"; ... out.push_back(r); }`
    block, so assertions don't accidentally match framework-injected boilerplate
    routes (htmx.js, optimistic.js, etc.)."""
    marker = f'r.path = "{path}"'
    start  = cpp.index(marker)
    end    = cpp.index("out.push_back(r);", start)
    return cpp[start:end]


def gen(**scaffold_kwargs) -> str:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        scaffold(tmp, **scaffold_kwargs)
        result = subprocess.run(
            [_PY, str(GEN_ROUTES), str(tmp)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise AssertionError(f"gen_routes.py failed:\n{result.stderr}")
        return (tmp / "build" / "generated" / "routes.cpp").read_text()


class TestApiHandlerSync(unittest.TestCase):
    """Existing behavior: sync API handlers still emit r.get/post/... (not async_*)."""

    def test_sync_returns_model_no_body(self):
        cpp = gen(
            route_path="api/v1/users",
            route_src="""
                #include <revet/web.hpp>
                static auto render_get(const web::Request&) -> User {
                    return User{};
                }
            """,
            model_src='struct [[revet::model]] User { std::string id; };',
        )
        block = _user_block(cpp, "/api/v1/users")
        self.assertIn("r.get =", block)
        self.assertNotIn("r.async_get", block)
        self.assertIn("revet::json::to_json<User>", block)
        self.assertNotIn("co_await", block)
        self.assertNotIn("co_return", block)

    def test_sync_with_typed_body(self):
        cpp = gen(
            route_path="api/v1/users",
            route_src="""
                #include <revet/web.hpp>
                static auto render_post(const CreateUser& body) -> User {
                    return User{};
                }
            """,
            model_src=(
                'struct [[revet::model]] CreateUser { std::string name; };\n'
                'struct [[revet::model]] User { std::string id; };\n'
            ),
        )
        block = _user_block(cpp, "/api/v1/users")
        self.assertIn("r.post =", block)
        self.assertNotIn("r.async_post", block)
        self.assertIn("from_json<CreateUser>", block)
        self.assertIn("to_json<User>", block)
        self.assertNotIn("co_await", block)

    def test_sync_full_control(self):
        cpp = gen(
            route_path="api/v1/raw",
            route_src="""
                #include <revet/web.hpp>
                static auto render_get(const web::Request&) -> web::Response {
                    return web::Response{};
                }
            """,
        )
        block = _user_block(cpp, "/api/v1/raw")
        self.assertIn("r.get =", block)
        self.assertNotIn("r.async_get", block)
        self.assertIn("web::to_response", block)


class TestApiHandlerAsync(unittest.TestCase):
    """New behavior: API handlers returning web::Task<T> emit async_* wrappers."""

    def test_async_returns_task_of_model(self):
        cpp = gen(
            route_path="api/v1/users",
            route_src="""
                #include <revet/web.hpp>
                static auto render_get(const web::Request&) -> web::Task<User> {
                    co_return User{};
                }
            """,
            model_src='struct [[revet::model]] User { std::string id; };',
        )
        # User route emits async handler, not sync one.
        block = _user_block(cpp, "/api/v1/users")
        self.assertIn("r.async_get =", block)
        self.assertNotIn("r.get =", block)
        # Must be a coroutine returning Task<Response>.
        self.assertIn("-> web::Task<web::Response>", block)
        self.assertIn("co_await route_", block)
        self.assertIn("co_return web::Response", block)
        # JSON serialization preserved.
        self.assertIn("to_json<User>", block)

    def test_async_with_typed_body_returns_task_of_model(self):
        cpp = gen(
            route_path="api/v1/users",
            route_src="""
                #include <revet/web.hpp>
                static auto render_post(const CreateUser& body) -> web::Task<User> {
                    co_return User{};
                }
            """,
            model_src=(
                'struct [[revet::model]] CreateUser { std::string name; };\n'
                'struct [[revet::model]] User { std::string id; };\n'
            ),
        )
        block = _user_block(cpp, "/api/v1/users")
        self.assertIn("r.async_post =", block)
        self.assertNotIn("r.post =", block)
        # All error returns inside the coroutine must co_return, not return.
        self.assertIn("co_return web::api::error", block)
        # And the success path co_awaits then co_returns the JSON response.
        self.assertIn("co_await route_", block)
        self.assertIn("to_json<User>", block)

    def test_async_returns_task_of_vector_model(self):
        cpp = gen(
            route_path="api/v1/users",
            route_src="""
                #include <revet/web.hpp>
                static auto render_get(const web::Request&) -> web::Task<std::vector<User>> {
                    co_return std::vector<User>{};
                }
            """,
            model_src='struct [[revet::model]] User { std::string id; };',
        )
        block = _user_block(cpp, "/api/v1/users")
        self.assertIn("r.async_get =", block)
        self.assertIn("to_json<std::vector<User>>", block)
        self.assertIn("co_await route_", block)

    def test_async_full_control_task_of_response(self):
        cpp = gen(
            route_path="api/v1/raw",
            route_src="""
                #include <revet/web.hpp>
                static auto render_get(const web::Request&) -> web::Task<web::Response> {
                    co_return web::Response{};
                }
            """,
        )
        block = _user_block(cpp, "/api/v1/raw")
        self.assertIn("r.async_get =", block)
        self.assertNotIn("r.get =", block)
        self.assertIn("co_await route_", block)
        # No JSON wrapping for full-control async: it just passes through to_response.
        self.assertIn("web::to_response", block)


class TestNonApiRoutesUnchanged(unittest.TestCase):
    """The async API emission must not regress general route emission."""

    def test_non_api_async_route_still_uses_async_get(self):
        cpp = gen(
            route_path="quakes",
            route_src="""
                #include <revet/web.hpp>
                static auto render_get(const web::Request&) -> web::Task<web::Response> {
                    co_return web::Response{};
                }
            """,
        )
        block = _user_block(cpp, "/quakes")
        self.assertIn("r.async_get =", block)
        # Non-API routes do NOT use the api::error / from_json / to_json wrapping.
        self.assertNotIn("revet::json::to_json", block)
        self.assertNotIn("web::api::error", block)


if __name__ == "__main__":
    unittest.main()

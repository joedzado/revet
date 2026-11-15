from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMakeDeps


class CppWebConan(ConanFile):
    name = "revet"
    version = (Path(__file__).parent / "VERSION").read_text().strip()
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("catch2/3.8.0")
        self.requires("libcurl/8.4.0")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

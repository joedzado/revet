from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMakeDeps


class CppWebConan(ConanFile):
    name = "revet"
    version = (Path(__file__).parent / "VERSION").read_text().strip()
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("libsodium/1.0.20")
        self.requires("catch2/3.8.0")
        self.requires("libcurl/8.4.0")
        self.requires("simdjson/3.10.0")
        self.requires("ctre/3.8.1")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

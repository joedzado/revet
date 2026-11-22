// Resource, upload, dependency and toggle generators.
// Each reads revet.toml (or app/states) and writes one generated include or CMake fragment.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <ranges>
#include <set>
#include <sys/stat.h>

#include "common.hpp"
#include "node.hpp"

namespace revet::codegen {
namespace {

auto load_toml(const Ctx& ctx) -> Node {
    return parse_toml(read_text(ctx.proj / "revet.toml"));
}

auto table_or_empty(const Node& root, std::string_view key) -> const Node* {
    static const Node empty_table = [] -> Node {
        Node n;
        n.kind = Node::Kind::Table;
        return n;
    }();
    const Node* n = root.find(key);
    return (n != nullptr) ? n : &empty_table;
}

// ── stable ordered view of a table (insertion order) ───────────

struct Pair {
    std::string key;
    const Node* value;
};

auto items_of(const Node& table) -> std::vector<Pair> {
    return std::views::zip(table.keys, table.vals) | std::views::transform([](const auto& kv) -> Pair {
               return {.key = std::get<0>(kv), .value = &std::get<1>(kv)};
           }) |
           std::ranges::to<std::vector<Pair>>();
}

} // namespace

// ── resources ───────────────────────────────────────────────────────────────

auto gen_resources(const Ctx& ctx) -> int {
    fs::path out = setup(ctx, "resources.hpp");
    const fs::path toml = ctx.proj / "revet.toml";
    fs::path backends_out = ctx.proj / "build" / "generated" / "db_backends.cmake";
    const std::string paths_block = tmpl(ctx, "resources_paths_block.tmpl");

    const auto exit_empty = [&](const std::string& msg) -> int {
        write_file(out, "// Auto-generated — do not edit\n#pragma once\n\n" + paths_block + "\n\n" + msg + "\n");
        write_file(backends_out, "set(REVET_DB_BACKENDS)\n");
        return 0;
    };

    if (!file_exists(toml)) {
        return exit_empty("// no revet.toml");
    }

    const Node data = load_toml(ctx);
    long long thread_count = 4;
    if (const Node* tc = data.find("thread_count")) {
        thread_count = py_int(*tc);
    }

    struct Resource {
        std::string name;
        std::string type;
        std::string path;
        long long pool = 0;
        bool is_default = false;
    };
    auto resource_of = [&](const Pair& item) -> Resource {
        const auto& [name, rnode] = item;
        Resource r;
        r.name = name;
        const Node* type = rnode->find("type");
        r.type = (type != nullptr) ? py_str(*type) : "sqlite";
        if (const Node* p = rnode->find("path")) {
            r.path = py_str(*p);
        } else if (const Node* u = rnode->find("url")) {
            r.path = py_str(*u);
        } else {
            r.path = "app/data/" + name + ".db";
        }
        const Node* pool = rnode->find("pool_size");
        r.pool = (pool != nullptr) ? py_int(*pool) : thread_count;
        const Node* def = rnode->find("default");
        r.is_default = (def != nullptr) ? py_bool(*def) : false;
        return r;
    };
    auto resources = items_of(*table_or_empty(data, "resources")) | std::views::transform(resource_of) |
                     std::ranges::to<std::vector<Resource>>();

    if (resources.empty()) {
        return exit_empty("// no [resources.*] in revet.toml");
    }

    auto ns_for = [](const std::string& type) -> std::string {
        if (type == "sqlite") {
            return "db::sqlite";
        }
        if (type == "postgres") {
            return "db::postgres";
        }
        throw GenError("gen_resources: unknown resource type '" + type + "'");
    };

    // The first resource marked default, else the first one.
    const auto dflt_it = std::ranges::find_if(resources, &Resource::is_default);
    const Resource& dflt_res = dflt_it == resources.end() ? resources.front() : *dflt_it;
    std::string dflt = dflt_res.name;
    std::string dflt_ns = ns_for(dflt_res.type);

    const std::string pool_fn_t = tmpl(ctx, "resources_pool_fn.tmpl");
    const std::string handle_fn_t = tmpl(ctx, "resources_handle_fn.tmpl");
    const std::string db_alias_t = tmpl(ctx, "resources_db_alias.tmpl");

    Lines lines = {"// Auto-generated — do not edit",
                   "#pragma once",
                   "#include <revet/db.hpp>",
                   "",
                   paths_block,
                   "",
                   "namespace resources {",
                   "namespace _pools {",
                   ""};
    // Each pool function is followed by a blank line.
    std::ranges::transform(resources, std::back_inserter(lines), [&](const Resource& r) -> std::string {
        return subst(pool_fn_t,
                     {{"ns", ns_for(r.type)}, {"name", r.name}, {"path", r.path}, {"pool", std::to_string(r.pool)}}) +
               "\n";
    });
    lines.emplace_back("} // namespace _pools");
    lines.emplace_back("");
    std::ranges::transform(resources, std::back_inserter(lines), [&](const Resource& r) -> std::string {
        return subst(handle_fn_t, {{"ns", ns_for(r.type)}, {"name", r.name}});
    });
    auto backends = resources | std::views::transform(&Resource::type) | std::ranges::to<std::set<std::string>>();

    if (dflt != "db") {
        lines.emplace_back("");
        lines.push_back(subst(db_alias_t, {{"ns", dflt_ns}, {"default", dflt}}));
    }
    lines.emplace_back("");
    lines.emplace_back("} // namespace resources");
    lines.emplace_back("");
    write_lines(out, lines);

    const std::string list = join(backends | std::ranges::to<Lines>(), " ");
    write_file(backends_out, "# Auto-generated — do not edit\nset(REVET_DB_BACKENDS " + list + ")\n");
    return 0;
}

// ── uploads ─────────────────────────────────────────────────────────────────

auto gen_uploads(const Ctx& ctx) -> int {
    const fs::path out = setup(ctx, "uploads.hpp");
    const fs::path toml = ctx.proj / "revet.toml";
    const std::string empty = "#pragma once\n// no [uploads.*] in revet.toml\nnamespace uploads {}\n";

    if (!file_exists(toml)) {
        write_file(out, empty);
        return 0;
    }

    const Node data = load_toml(ctx);
    struct Bucket {
        std::string name;
        std::string incoming;
        std::string validated;
    };
    auto dirs = items_of(*table_or_empty(data, "uploads")) | std::views::transform([](const Pair& item) -> Bucket {
                    const auto& [name, d] = item;
                    const Node* inc = d->find("incoming");
                    const Node* val = d->find("validated");
                    return {.name = name,
                            .incoming = (inc != nullptr) ? py_str(*inc) : "app/uploads/" + name + "/incoming",
                            .validated = (val != nullptr) ? py_str(*val) : "app/uploads/" + name + "/validated"};
                }) |
                std::ranges::to<std::vector<Bucket>>();

    if (dirs.empty()) {
        write_file(out, empty);
        return 0;
    }

    // Created at configure time: incoming 0700 (not web-accessible), validated 0750.
    for (const auto& b : dirs) {
        for (const auto& [rel, mode] :
             std::vector<std::pair<std::string, mode_t>>{{b.incoming, 0700}, {b.validated, 0750}}) {
            const fs::path p = ctx.proj / rel;
            fs::create_directories(p);
            ::chmod(p.c_str(), mode);
        }
    }

    const std::string ns_t = tmpl(ctx, "uploads_namespace.tmpl");
    Lines lines = file_header({"#include <filesystem>", "#include <string_view>"});
    lines.emplace_back("namespace uploads {");
    lines.emplace_back("");
    // Each namespace is followed by a blank line.
    std::ranges::transform(dirs, std::back_inserter(lines), [&](const Bucket& b) -> std::string {
        return subst(ns_t, {{"name", b.name}, {"incoming", b.incoming}, {"validated", b.validated}}) + "\n";
    });
    lines.emplace_back("} // namespace uploads");
    lines.emplace_back("");
    write_lines(out, lines);
    return 0;
}

// ── dependencies ────────────────────────────────────────────────────────────

auto gen_dependencies(const Ctx& ctx) -> int {
    const fs::path toml = ctx.proj / "revet.toml";
    const fs::path out = setup(ctx, "dependencies.cmake");
    const std::string dep_t = tmpl(ctx, "dependencies_dep.tmpl");

    // Line-based on purpose: matches the original parser's behaviour exactly.
    std::vector<std::pair<std::string, std::string>> deps; // key -> conan ref (insertion order)
    Lines lines = file_exists(toml)
                      ? splitlines(read_text(toml)) | std::views::transform(strip) | std::ranges::to<Lines>()
                      : Lines{};
    const auto start = std::ranges::find_if(
        lines, [](const std::string& l) -> bool { return starts_with(l, "[build.dependencies]"); });
    if (start != lines.end()) {
        const auto section = std::ranges::subrange(std::next(start), lines.end()) |
                             std::views::take_while([](const std::string& l) -> bool { return !starts_with(l, "["); });
        for (const auto& stripped : section) {
            std::string key;
            std::string ref;
            if (const auto m = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=\s*"([^"]+)")RE">(stripped)) {
                key = m.get<1>().to_string();
                ref = m.get<2>().to_string();
            } else if (const auto b = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=)RE">(stripped)) {
                key = b.get<1>().to_string();
                ref = key;
            } else {
                continue;
            }
            if (const auto it = std::ranges::find(deps, key, &std::pair<std::string, std::string>::first);
                it != deps.end()) {
                it->second = ref;
            } else {
                deps.emplace_back(key, ref);
            }
        }
    }

    // Conan package name -> CMake package name, from the CMakeDeps files Conan wrote.
    // '<CMakeName>-release-<arch>-data.cmake' sets '<conan_name>_PACKAGE_FOLDER_RELEASE'.
    // Example: conan package 'libwebp' ships CMake package 'WebP'.
    std::map<std::string, std::string> cmake_names;
    const fs::path conan_dir = ctx.proj / "build" / "conan";
    if (fs::is_directory(conan_dir)) {
        for (const auto& f : glob(conan_dir, "-data.cmake")) {
            const std::string name = f.filename().string();
            if (starts_with(name, "module-") || !name.contains("-release-")) {
                continue;
            }
            std::string text = read_text(f);
            if (const auto m = ctre::search<R"RE(set\((\w+)_PACKAGE_FOLDER_RELEASE)RE">(text)) {
                cmake_names[m.get<1>().to_string()] = name.substr(0, name.find("-release-"));
            }
        }
    }

    Lines cmake = {"# Generated by revet-codegen — do not edit"};
    for (const auto& [key, ref] : deps) {
        (void)key;
        const size_t slash = ref.find('/');
        std::string pkg = slash == std::string::npos ? ref : ref.substr(0, slash);
        const auto it = cmake_names.find(pkg);
        std::string cmake_name = it == cmake_names.end() ? pkg : it->second;
        cmake.push_back(subst(dep_t, {{"pkg_name", pkg}, {"cmake_name", cmake_name}}));
        // Conan's root target can be empty with the libraries on its components; forward them when present.
        cmake.push_back(cat("if(DEFINED ", pkg, "_COMPONENT_NAMES AND TARGET ", pkg, "::", pkg, ")"));
        cmake.push_back(cat("    set_property(TARGET ", pkg, "::", pkg, " APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${",
                            pkg, "_COMPONENT_NAMES})"));
        cmake.emplace_back("endif()");
    }
    cmake.emplace_back("");
    write_file(out, join(cmake, "\n"));
    return 0;
}

// ── toggles ─────────────────────────────────────────────────────────────────

auto gen_toggles(const Ctx& ctx) -> int {
    const fs::path out = setup(ctx, "toggles.hpp");

    struct Toggle {
        std::string name;
        bool default_on;
    };
    std::vector<Toggle> toggles;
    for (const auto& f : glob(ctx.proj / "app" / "states", ".hpp")) {
        const std::string content = read_text(f);
        std::ranges::transform(
            find_toggles(std::string_view(content)), std::back_inserter(toggles), [](const auto& m) -> Toggle {
                return {.name = m.template get<3>().to_string(), .default_on = m.template get<2>().to_view() == "true"};
            });
    }

    if (toggles.empty()) {
        write_file(out, "#pragma once\n\nnamespace web::toggles {}\n");
        return 0;
    }

    const std::string fn_t = tmpl(ctx, "toggles_fn.tmpl");
    const auto fns = toggles | std::views::transform([&](const Toggle& t) -> std::string {
                         return subst(fn_t, {{"name", t.name}, {"default_str", t.default_on ? "true" : "false"}});
                     }) |
                     std::ranges::to<Lines>();
    const std::string header_t = tmpl_whole(ctx, "toggles_header.tmpl");
    write_file(out, subst(header_t, {{"functions", join(fns, "\n\n")}}));
    return 0;
}

} // namespace revet::codegen

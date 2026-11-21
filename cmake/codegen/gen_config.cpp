// Resource, upload, dependency and toggle generators.
// Each reads revet.toml (or app/states) and writes one generated include or CMake fragment.
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <sys/stat.h>

#include "common.hpp"
#include "node.hpp"

namespace revet::codegen {
namespace {

Node load_toml(const Ctx& ctx) {
    return parse_toml(read_text(ctx.proj / "revet.toml"));
}

const Node* table_or_empty(const Node& root, std::string_view key) {
    static const Node empty_table = [] {
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

std::vector<Pair> items_of(const Node& table) {
    std::vector<Pair> out;
    out.reserve(table.keys.size());
    for (size_t i = 0; i < table.keys.size(); ++i) {
        out.push_back({table.keys[i], &table.vals[i]});
    }
    return out;
}

} // namespace

// ── resources ───────────────────────────────────────────────────────────────

int gen_resources(const Ctx& ctx) {
    fs::path out = setup(ctx, "resources.hpp");
    fs::path toml = ctx.proj / "revet.toml";
    fs::path backends_out = ctx.proj / "build" / "generated" / "db_backends.cmake";
    const std::string paths_block = tmpl(ctx, "resources_paths_block.tmpl");

    auto exit_empty = [&](const std::string& msg) {
        write_file(out, "// Auto-generated — do not edit\n#pragma once\n\n" + paths_block + "\n\n" + msg + "\n");
        write_file(backends_out, "set(REVET_DB_BACKENDS)\n");
        return 0;
    };

    if (!file_exists(toml)) {
        return exit_empty("// no revet.toml");
    }

    Node data = load_toml(ctx);
    long long thread_count = 4;
    if (const Node* tc = data.find("thread_count")) {
        thread_count = py_int(*tc);
    }

    struct Resource {
        std::string name;
        std::string type;
        std::string path;
        long long pool;
        bool is_default;
    };
    std::vector<Resource> resources;
    for (const auto& [name, rnode] : items_of(*table_or_empty(data, "resources"))) {
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
        resources.push_back(std::move(r));
    }

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

    std::string dflt = resources.front().name;
    for (const auto& r : resources) {
        if (r.is_default) {
            dflt = r.name;
            break;
        }
    }
    std::string dflt_ns;
    for (const auto& r : resources) {
        if (r.name == dflt) {
            dflt_ns = ns_for(r.type);
        }
    }

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
    Lines pool_lines;
    Lines handle_lines;
    std::set<std::string> backends;
    for (const auto& r : resources) {
        std::string ns = ns_for(r.type);
        backends.insert(r.type);
        pool_lines.push_back(
            subst(pool_fn_t, {{"ns", ns}, {"name", r.name}, {"path", r.path}, {"pool", std::to_string(r.pool)}}));
        pool_lines.push_back("");
        handle_lines.push_back(subst(handle_fn_t, {{"ns", ns}, {"name", r.name}}));
    }
    for (const auto& l : pool_lines) {
        lines.push_back(l);
    }
    lines.push_back("} // namespace _pools");
    lines.push_back("");
    for (const auto& l : handle_lines) {
        lines.push_back(l);
    }

    if (dflt != "db") {
        lines.push_back("");
        lines.push_back(subst(db_alias_t, {{"ns", dflt_ns}, {"default", dflt}}));
    }
    lines.push_back("");
    lines.push_back("} // namespace resources");
    lines.push_back("");
    write_lines(out, lines);

    std::string list;
    for (const auto& b : backends) {
        if (!list.empty()) {
            list += ' ';
        }
        list += b;
    }
    write_file(backends_out, "# Auto-generated — do not edit\nset(REVET_DB_BACKENDS " + list + ")\n");
    return 0;
}

// ── uploads ─────────────────────────────────────────────────────────────────

int gen_uploads(const Ctx& ctx) {
    fs::path out = setup(ctx, "uploads.hpp");
    fs::path toml = ctx.proj / "revet.toml";
    const std::string empty = "#pragma once\n// no [uploads.*] in revet.toml\nnamespace uploads {}\n";

    if (!file_exists(toml)) {
        write_file(out, empty);
        return 0;
    }

    Node data = load_toml(ctx);
    struct Bucket {
        std::string name;
        std::string incoming;
        std::string validated;
    };
    std::vector<Bucket> dirs;
    for (const auto& [name, d] : items_of(*table_or_empty(data, "uploads"))) {
        Bucket b;
        b.name = name;
        const Node* inc = d->find("incoming");
        const Node* val = d->find("validated");
        b.incoming = (inc != nullptr) ? py_str(*inc) : "app/uploads/" + name + "/incoming";
        b.validated = (val != nullptr) ? py_str(*val) : "app/uploads/" + name + "/validated";
        dirs.push_back(std::move(b));
    }

    if (dirs.empty()) {
        write_file(out, empty);
        return 0;
    }

    // Created at configure time: incoming 0700 (not web-accessible), validated 0750.
    for (const auto& b : dirs) {
        for (const auto& [rel, mode] :
             std::vector<std::pair<std::string, mode_t>>{{b.incoming, 0700}, {b.validated, 0750}}) {
            fs::path p = ctx.proj / rel;
            fs::create_directories(p);
            ::chmod(p.c_str(), mode);
        }
    }

    const std::string ns_t = tmpl(ctx, "uploads_namespace.tmpl");
    Lines lines = file_header({"#include <filesystem>", "#include <string_view>"});
    lines.push_back("namespace uploads {");
    lines.push_back("");
    for (const auto& b : dirs) {
        lines.push_back(subst(ns_t, {{"name", b.name}, {"incoming", b.incoming}, {"validated", b.validated}}));
        lines.push_back("");
    }
    lines.push_back("} // namespace uploads");
    lines.push_back("");
    write_lines(out, lines);
    return 0;
}

// ── dependencies ────────────────────────────────────────────────────────────

int gen_dependencies(const Ctx& ctx) {
    fs::path toml = ctx.proj / "revet.toml";
    fs::path out = setup(ctx, "dependencies.cmake");
    const std::string dep_t = tmpl(ctx, "dependencies_dep.tmpl");

    // Line-based on purpose: matches the original parser's behaviour exactly.
    std::vector<std::pair<std::string, std::string>> deps; // key -> conan ref (insertion order)
    if (file_exists(toml)) {
        bool in_deps = false;
        for (const auto& line : splitlines(read_text(toml))) {
            std::string stripped = strip(line);
            if (starts_with(stripped, "[build.dependencies]")) {
                in_deps = true;
                continue;
            }
            if (!in_deps) {
                continue;
            }
            if (starts_with(stripped, "[")) {
                break;
            }
            std::string key;
            std::string ref;
            if (auto m = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=\s*"([^"]+)")RE">(stripped)) {
                key = m.get<1>().to_string();
                ref = m.get<2>().to_string();
            } else if (auto b = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=)RE">(stripped)) {
                key = b.get<1>().to_string();
                ref = key;
            } else {
                continue;
            }
            bool replaced = false;
            for (auto& d : deps) {
                if (d.first == key) {
                    d.second = ref;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                deps.emplace_back(key, ref);
            }
        }
    }

    // Conan package name -> CMake package name, from the CMakeDeps files Conan wrote.
    // '<CMakeName>-release-<arch>-data.cmake' sets '<conan_name>_PACKAGE_FOLDER_RELEASE'.
    // Example: conan package 'libwebp' ships CMake package 'WebP'.
    std::map<std::string, std::string> cmake_names;
    fs::path conan_dir = ctx.proj / "build" / "conan";
    if (fs::is_directory(conan_dir)) {
        for (const auto& f : glob(conan_dir, "-data.cmake")) {
            std::string name = f.filename().string();
            if (starts_with(name, "module-") || !name.contains("-release-")) {
                continue;
            }
            std::string text = read_text(f);
            if (auto m = ctre::search<R"RE(set\((\w+)_PACKAGE_FOLDER_RELEASE)RE">(text)) {
                cmake_names[m.get<1>().to_string()] = name.substr(0, name.find("-release-"));
            }
        }
    }

    Lines cmake = {"# Generated by revet-codegen — do not edit"};
    for (const auto& [key, ref] : deps) {
        (void)key;
        size_t slash = ref.find('/');
        std::string pkg = slash == std::string::npos ? ref : ref.substr(0, slash);
        auto it = cmake_names.find(pkg);
        std::string cmake_name = it == cmake_names.end() ? pkg : it->second;
        cmake.push_back(subst(dep_t, {{"pkg_name", pkg}, {"cmake_name", cmake_name}}));
        // Conan's root target can be empty with the libraries on its components; forward them when present.
        cmake.push_back(cat("if(DEFINED ", pkg, "_COMPONENT_NAMES AND TARGET ", pkg, "::", pkg, ")"));
        cmake.push_back(cat("    set_property(TARGET ", pkg, "::", pkg, " APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${",
                            pkg, "_COMPONENT_NAMES})"));
        cmake.push_back("endif()");
    }
    cmake.push_back("");
    write_file(out, join(cmake, "\n"));
    return 0;
}

// ── toggles ─────────────────────────────────────────────────────────────────

int gen_toggles(const Ctx& ctx) {
    fs::path out = setup(ctx, "toggles.hpp");

    struct Toggle {
        std::string name;
        bool default_on;
    };
    std::vector<Toggle> toggles;
    for (const auto& f : glob(ctx.proj / "app" / "states", ".hpp")) {
        std::string content = read_text(f);
        for (auto m : find_toggles(std::string_view(content))) {
            toggles.push_back({m.get<3>().to_string(), m.get<2>().to_view() == "true"});
        }
    }

    if (toggles.empty()) {
        write_file(out, "#pragma once\n\nnamespace web::toggles {}\n");
        return 0;
    }

    const std::string fn_t = tmpl(ctx, "toggles_fn.tmpl");
    Lines fns;
    for (const auto& t : toggles) {
        fns.push_back(subst(fn_t, {{"name", t.name}, {"default_str", t.default_on ? "true" : "false"}}));
    }
    const std::string header_t = tmpl_whole(ctx, "toggles_header.tmpl");
    write_file(out, subst(header_t, {{"functions", join(fns, "\n\n")}}));
    return 0;
}

} // namespace revet::codegen

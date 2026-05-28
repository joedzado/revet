#pragma once
#include <algorithm>
#include <filesystem>
#include <revet/web.hpp>

namespace uploads {

using FileValidator = std::function<bool(web::UploadedFile&)>;

namespace detail {

inline void remove_all_files(std::vector<web::UploadedFile>& files) {
    for (auto& f : files) {
        std::error_code ec;
        std::filesystem::remove(f.path, ec);
    }
}

inline auto atomic_move(const std::filesystem::path& from, const std::filesystem::path& to) -> bool {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    std::filesystem::remove(from, ec);
    return !ec;
}

// Returns false if dir is invalid: doesn't exist, not a directory,
// world-readable, or resolves outside project_root.
inline auto validate_dir(const std::filesystem::path& dir, const std::filesystem::path& project_root) -> bool {
    std::error_code ec;
    auto canon = std::filesystem::canonical(dir, ec);
    if (ec) {
        return false;
    }
    if (!std::filesystem::is_directory(canon)) {
        return false;
    }

    // Not world-readable
    auto perms = std::filesystem::status(canon, ec).permissions();
    if (ec) {
        return false;
    }
    if ((perms & std::filesystem::perms::others_read) != std::filesystem::perms::none) {
        return false;
    }

    // Jailed to project_root
    auto root = std::filesystem::canonical(project_root, ec);
    if (ec) {
        return false;
    }
    auto root_s = root.string();
    if (root_s.back() != '/') {
        root_s += '/';
    }
    auto canon_s = canon.string();
    if (canon_s != root_s.substr(0, root_s.size() - 1) && !canon_s.starts_with(root_s)) {
        return false;
    }

    return true;
}

} // namespace detail

// ── Built-in validators ──────────────────────────────────────────────────────

// Detects type from magic bytes, sets detected_type, passes if it matches allowed.
inline auto magic_bytes(std::vector<web::FileType> allowed) -> FileValidator {
    return [allowed = std::move(allowed)](web::UploadedFile& f) -> bool {
        f.detected_type = web::detail::detect_type(f.path);
        return std::ranges::contains(allowed, f.detected_type);
    };
}

// Passes if file size ≤ bytes.
inline auto max_size(std::uintmax_t bytes) -> FileValidator {
    return [bytes](web::UploadedFile& f) -> bool {
        std::error_code ec;
        return std::filesystem::file_size(f.path, ec) <= bytes && !ec;
    };
}

// ── Middleware factory ───────────────────────────────────────────────────────

struct AllowConfig {
    std::filesystem::path validated_dir;   // destination after guard pass; required
    std::filesystem::path project_root;    // jail boundary; defaults to current_path()
    std::vector<web::FileType> types;      // allowed types; empty = any
    std::vector<FileValidator> validators; // extra validators run after magic check
    bool check_magic = true;
};

// Route guard. On pass: moves each file tmp → validated_dir, updates path.
// On fail: removes all files from tmp, returns 422.
// Returns 500 if validated_dir fails directory validation.
inline auto allow(AllowConfig cfg) -> web::Middleware {
    return [cfg = std::move(cfg)](const web::Request&, const web::Next& next) -> web::Response {
        auto& files = web::Context::current().files;
        if (files.empty()) {
            return next();
        }

        const auto root = cfg.project_root.empty() ? std::filesystem::current_path() : cfg.project_root;

        if (!detail::validate_dir(cfg.validated_dir, root)) {
            return {.status = web::status::internal_error};
        }

        for (auto& file : files) {
            if (file.validated_dir.empty()) {
                file.validated_dir = cfg.validated_dir;
            }

            if (cfg.check_magic) {
                file.detected_type = web::detail::detect_type(file.path);
                if (!cfg.types.empty() && !std::ranges::contains(cfg.types, file.detected_type)) {
                    detail::remove_all_files(files);
                    return {.status = web::status::unprocessable_entity};
                }
            }

            for (const auto& v : cfg.validators) {
                if (!v(file)) {
                    detail::remove_all_files(files);
                    return {.status = web::status::unprocessable_entity};
                }
            }

            auto dest = file.validated_dir / file.path.filename();
            if (!detail::atomic_move(file.path, dest)) {
                detail::remove_all_files(files);
                return {.status = web::status::internal_error};
            }
            file.path = std::move(dest);
        }

        return next();
    };
}

} // namespace uploads

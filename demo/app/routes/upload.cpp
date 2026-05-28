#include "routes.hpp"
#include "styles.hpp"
#include <revet/csrf.hpp>
#include <revet/upload.hpp>
#include <filesystem>
#include <format>

static auto route_guards() -> std::vector<web::Middleware> {
    return {
        uploads::allow({
            .validated_dir = std::filesystem::path(uploads::files::validated_dir),
            .types         = { web::FileType::jpeg, web::FileType::png,
                               web::FileType::gif,  web::FileType::webp },
        })
    };
}

static constexpr auto type_label(web::FileType t) -> std::string_view {
    switch (t) {
        case web::FileType::jpeg:    return "JPEG";
        case web::FileType::png:     return "PNG";
        case web::FileType::gif:     return "GIF";
        case web::FileType::webp:    return "WebP";
        case web::FileType::pdf:     return "PDF";
        case web::FileType::zip:     return "ZIP";
        case web::FileType::unknown: return "unknown";
    }
    return "unknown";
}

static auto fmt_size(std::uintmax_t bytes) -> std::string {
    if (bytes < 1024UL)         return std::format("{} B", bytes);
    if (bytes < 1024UL * 1024UL) return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

// ── Snippets ──────────────────────────────────────────────────────────────────

static constexpr auto SNIPPET_TOML =
R"([uploads.files]
# incoming  = "app/uploads/files/incoming"   # auto-created, mode 0700
# validated = "app/uploads/files/validated"  # auto-created, mode 0750)";

static constexpr auto SNIPPET_ROUTE =
R"(#include <revet/upload.hpp>

// Validates uploads before render_get / render_post runs.
// On failure: deletes tmp files, returns 422. Never calls your handler.
static auto route_guards() -> std::vector<web::Middleware> {
    return {
        uploads::allow({
            .validated_dir = std::filesystem::path(uploads::files::validated_dir),
            .types         = { web::FileType::jpeg, web::FileType::png,
                               web::FileType::gif,  web::FileType::webp },
        })
    };
}

static auto render_post(const web::Request&) -> web::Response {
    auto& files = web::Context::current().files;
    if (files.empty()) return web::redirect(urls::upload);

    // files[0].path          — absolute path in validated_dir
    // files[0].filename      — sanitized original filename
    // files[0].detected_type — detected from magic bytes (not Content-Type header)
    std::error_code ec;
    auto bytes = std::filesystem::file_size(files[0].path, ec);
    // … process the file …
    return web::redirect(urls::upload);
})";

static constexpr auto SNIPPET_SIZE_GUARD =
R"(uploads::allow({
    .validated_dir = std::filesystem::path(uploads::files::validated_dir),
    .types         = { web::FileType::jpeg, web::FileType::png },
    .validators    = { uploads::max_size(5 * 1024 * 1024) },  // 5 MB cap
}))";

static constexpr auto SNIPPET_CUSTOM_VALIDATOR =
R"(// Any std::function<bool(web::UploadedFile&)> works as a validator.
// Receives a mutable file ref — inspect on disk, add metadata, etc.
auto square_only = [](web::UploadedFile& f) -> bool {
    // e.g. open f.path and check image dimensions
    return true;
};

uploads::allow({
    .validated_dir = std::filesystem::path(uploads::files::validated_dir),
    .validators    = { uploads::magic_bytes({web::FileType::png}), square_only },
    .check_magic   = false,  // magic_bytes validator handles type detection
}))";

// ── Render ────────────────────────────────────────────────────────────────────

static auto render_get(const web::Request& req) -> web::Response {
    const auto upload_form =
        html::div{styles::form_wrap,
            html::form{
                html::attr.action(urls::upload),
                html::attr.method("POST"),
                html::attr.enctype("multipart/form-data"),
                web::csrf::field(req),
                html::div{styles::fgroup,
                    html::label{html::attr.for_("file"), i18n::upload_file_label},
                    html::file_input{
                        html::attr.id("file"),
                        html::attr.name("file"),
                        html::attr.accept_images(),
                    },
                },
                html::submit{styles::btn, i18n::btn_upload},
            }
        };

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_upload_eyebrow},
            html::h1{i18n::page_upload_h1},
            html::p{i18n::page_upload_body},
        } +

        sec_label(i18n::section_upload_form) +
        html::p{i18n::upload_form_desc_a, html::code{"enctype=\"multipart/form-data\""},
                i18n::upload_form_desc_b, html::code{"route_guards()"},
                i18n::upload_form_desc_c, html::code{"validated_dir"},
                i18n::upload_form_desc_d} +
        upload_form +

        sec_label("revet.toml") +
        html::p{i18n::upload_toml_desc} +
        snippet_block("revet.toml", SNIPPET_TOML) +

        sec_label(i18n::section_route_handler) +
        html::p{html::code{"uploads::allow"}, i18n::upload_handler_desc_a,
                html::code{"Content-Type"}, i18n::upload_handler_desc_b} +
        snippet_block("site/upload.cpp", SNIPPET_ROUTE) +

        sec_label(i18n::section_size_guard) +
        html::p{html::code{"uploads::max_size(bytes)"}, i18n::upload_size_guard_desc,
                html::code{".validators"}, "."} +
        snippet_block("5 MB cap", SNIPPET_SIZE_GUARD) +

        sec_label(i18n::section_custom_validators) +
        html::p{i18n::upload_custom_val_desc_pre,
                html::code{"std::function<bool(web::UploadedFile&)>"},
                i18n::upload_custom_val_desc_suf} +
        snippet_block("Custom validator", SNIPPET_CUSTOM_VALIDATOR);

    return layout_page(req, i18n::page_upload_h1, urls::upload, content);
}

static auto render_post(const web::Request& req) -> web::Response {
    auto& files = web::Context::current().files;
    if (files.empty()) return web::redirect(urls::upload);

    const auto& f = files[0];
    std::error_code ec;
    const auto bytes    = std::filesystem::file_size(f.path, ec);
    const auto size_str = ec ? std::string{"(unknown)"} : fmt_size(bytes);

    const auto result =
        alert_ok(
            html::strong{i18n::upload_file_accepted} +
            html::div{styles::alert_detail,
                html::div{html::strong{i18n::upload_label_name},          f.filename},
                html::div{html::strong{i18n::upload_label_detected_type}, type_label(f.detected_type)},
                html::div{html::strong{i18n::upload_label_declared_type}, f.content_type},
                html::div{html::strong{i18n::upload_label_size},          size_str},
                html::div{html::strong{i18n::upload_label_stored_at},     f.path.string()},
            }
        );

    const auto content =
        html::div{styles::hero,
            html::h1{i18n::upload_accepted_h1},
        } +
        sec_label(i18n::section_upload_result) +
        result +
        html::div{css::make(css::mt::_6),
            html::a{html::attr.href(urls::upload), styles::btn, styles::btn_ghost, i18n::btn_upload_another},
        };

    return layout_page(req, i18n::page_upload_h1, urls::upload, content);
}

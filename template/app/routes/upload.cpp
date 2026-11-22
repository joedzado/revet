#include <revet/csrf.hpp>
#include <revet/log.hpp>
#include <revet/upload.hpp>

namespace {
const log::TuTag LOG(log::tags::ROUTES);

constexpr html::id F_FILE("file");

auto route_guards() -> std::vector<web::Middleware> {
    return {uploads::allow({
        .validated_dir = std::filesystem::path(uploads::files::validated_dir),
        .types = {web::FileType::jpeg, web::FileType::png, web::FileType::gif, web::FileType::webp},
        .check_magic = true,
    })};
}

auto render_get(const web::Request& req) -> web::Response {
    const auto content = html::div{
        styles::container, styles::page_content, html::h1{styles::page_title, "Upload"},
        html::div{styles::form_card,
                  html::form{html::attr.post(), html::attr.action(urls::upload), html::attr.multipart(),
                             web::csrf::field(req),
                             html::div{
                                 styles::field_group,
                                 html::label{styles::field_label, F_FILE.for_(), "Image (jpeg, png, gif, webp)"},
                                 html::file_input{html::attr.name("file"), F_FILE, html::attr.accept_images()},
                             },
                             html::div{
                                 styles::form_actions,
                                 html::submit{styles::btn, "Upload"},
                             }}}};
    return layout_main(req, "Upload — {{name}}", content);
}

auto render_post(const web::Request& /*req*/) -> web::Response {
    auto& files = web::Context::current().files;
    if (files.empty()) {
        return web::redirect(urls::upload);
    }

    log::info("Uploaded: {}", files.at(0).filename);
    // files[0].path points to uploads/files/validated/<filename>
    return web::redirect(urls::upload);
}
} // namespace

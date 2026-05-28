#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <revet/upload.hpp>
#include <revet/web.hpp>
#include <span>
#include <sys/stat.h>

// ── Helpers ───────────────────────────────────────────────────────────────────

struct TempDir {
    std::filesystem::path path;

    explicit TempDir(std::filesystem::perms p = std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                                std::filesystem::perms::group_exec) {
        auto tmpl = (std::filesystem::temp_directory_path() / "revet_test_XXXXXX").string();
        ::mkdtemp(tmpl.data());
        path = tmpl;
        std::filesystem::permissions(path, p);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

static void write_bytes(const std::filesystem::path& p, std::span<const unsigned char> data) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

static constexpr std::array<unsigned char, 3> JPEG_MAGIC = {0xFF, 0xD8, 0xFF};
static constexpr std::array<unsigned char, 8> PNG_MAGIC = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
static constexpr std::array<unsigned char, 4> TEXT_BYTES = {'h', 'e', 'l', 'l'};

static auto build_multipart(std::string_view boundary, std::string_view field, std::string_view filename,
                            std::string_view ct, std::span<const unsigned char> data) -> std::string {
    std::string body;
    body += "--";
    body += boundary;
    body += "\r\n";
    body += "Content-Disposition: form-data; name=\"";
    body += field;
    body += "\"; filename=\"";
    body += filename;
    body += "\"\r\n";
    body += "Content-Type: ";
    body += ct;
    body += "\r\n\r\n";
    body.append(reinterpret_cast<const char*>(data.data()), data.size());
    body += "\r\n--";
    body += boundary;
    body += "--\r\n";
    return body;
}

static auto build_text_part(std::string_view boundary, std::string_view field, std::string_view value) -> std::string {
    std::string body;
    body += "--";
    body += boundary;
    body += "\r\n";
    body += "Content-Disposition: form-data; name=\"";
    body += field;
    body += "\"\r\n\r\n";
    body += value;
    body += "\r\n--";
    body += boundary;
    body += "--\r\n";
    return body;
}

// ── parse_multipart ───────────────────────────────────────────────────────────

TEST_CASE("parse_multipart — single file part written to disk", "[uploads]") {
    TempDir incoming;
    web::ParamMap form;

    const auto body = build_multipart("boundary123", "avatar", "photo.jpg", "image/jpeg", JPEG_MAGIC);
    auto files = web::detail::parse_multipart(body, "boundary123", incoming.path, form);

    REQUIRE(files.size() == 1);
    CHECK(files[0].field_name == "avatar");
    CHECK(files[0].filename == "photo.jpg");
    CHECK(files[0].content_type == "image/jpeg");
    CHECK(std::filesystem::exists(files[0].path));
    CHECK(std::filesystem::file_size(files[0].path) == JPEG_MAGIC.size());
}

TEST_CASE("parse_multipart — magic bytes detected after write", "[uploads]") {
    TempDir incoming;
    web::ParamMap form;

    const auto body = build_multipart("b", "f", "img.png", "image/png", PNG_MAGIC);
    auto files = web::detail::parse_multipart(body, "b", incoming.path, form);

    REQUIRE(files.size() == 1);
    CHECK(files[0].detected_type == web::FileType::png);
}

TEST_CASE("parse_multipart — unknown magic type detected correctly", "[uploads]") {
    TempDir incoming;
    web::ParamMap form;

    const auto body = build_multipart("b", "f", "data.bin", "application/octet-stream", TEXT_BYTES);
    auto files = web::detail::parse_multipart(body, "b", incoming.path, form);

    REQUIRE(files.size() == 1);
    CHECK(files[0].detected_type == web::FileType::unknown);
}

TEST_CASE("parse_multipart — text field goes to form, not files", "[uploads]") {
    TempDir incoming;
    web::ParamMap form;

    const auto body = build_text_part("b", "description", "hello world");
    auto files = web::detail::parse_multipart(body, "b", incoming.path, form);

    CHECK(files.empty());
    CHECK(form.get("description") == "hello world");
}

TEST_CASE("parse_multipart — part count capped at 32", "[uploads]") {
    TempDir incoming;
    web::ParamMap form;

    std::string body;
    for (int i = 0; i < 40; ++i) {
        body += "--bound\r\n";
        body += "Content-Disposition: form-data; name=\"f";
        body += std::to_string(i);
        body += "\"; filename=\"f";
        body += std::to_string(i);
        body += ".jpg\"\r\n";
        body += "Content-Type: image/jpeg\r\n\r\n";
        body.append(reinterpret_cast<const char*>(JPEG_MAGIC.data()), JPEG_MAGIC.size());
        body += "\r\n";
    }
    body += "--bound--\r\n";

    auto files = web::detail::parse_multipart(body, "bound", incoming.path, form);
    CHECK(files.size() <= 32);
}

// ── sanitize_filename ─────────────────────────────────────────────────────────

TEST_CASE("sanitize_filename — strips path components", "[uploads]") {
    CHECK(web::detail::sanitize_filename("../../etc/passwd") == "passwd");
    CHECK(web::detail::sanitize_filename("/absolute/path/file.txt") == "file.txt");
    CHECK(web::detail::sanitize_filename("subdir/file.jpg") == "file.jpg");
}

TEST_CASE("sanitize_filename — strips leading dots", "[uploads]") {
    CHECK(web::detail::sanitize_filename("..hidden") == "hidden");
    CHECK(web::detail::sanitize_filename("...") == "upload");
}

TEST_CASE("sanitize_filename — strips null bytes and control chars", "[uploads]") {
    std::string name;
    name += "file";
    name += '\0';
    name += "name.jpg";
    CHECK(web::detail::sanitize_filename(name) == "filename.jpg");
}

TEST_CASE("sanitize_filename — empty result falls back to 'upload'", "[uploads]") {
    CHECK(web::detail::sanitize_filename("") == "upload");
    CHECK(web::detail::sanitize_filename("...") == "upload");
}

// ── uploads::allow guard ──────────────────────────────────────────────────────

static void set_context_file(const std::filesystem::path& p, std::string_view field = "f",
                             std::string_view fname = "file.jpg") {
    auto& ctx = web::Context::current();
    ctx.files.clear();
    web::UploadedFile f;
    f.field_name = field;
    f.filename = std::string(fname);
    f.path = p;
    ctx.files.push_back(std::move(f));
}

TEST_CASE("uploads::allow — valid jpeg passes and moves to validated_dir", "[uploads]") {
    TempDir incoming, validated(std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                std::filesystem::perms::group_exec);

    auto src = incoming.path / "img.jpg";
    write_bytes(src, JPEG_MAGIC);
    set_context_file(src, "avatar", "img.jpg");

    bool handler_called = false;
    auto guard = uploads::allow({
        .validated_dir = validated.path,
        .project_root = validated.path.parent_path(),
        .types = {web::FileType::jpeg},
        .check_magic = true,
    });

    auto resp = guard({}, [&]() -> web::Response {
        handler_called = true;
        return {};
    });

    CHECK(resp.status == web::status::ok);
    CHECK(handler_called);
    CHECK(std::filesystem::exists(validated.path / "img.jpg"));
    CHECK(!std::filesystem::exists(src));
}

TEST_CASE("uploads::allow — wrong type returns 422 and removes file", "[uploads]") {
    TempDir incoming, validated(std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                std::filesystem::perms::group_exec);

    auto src = incoming.path / "data.bin";
    write_bytes(src, TEXT_BYTES);
    set_context_file(src, "f", "data.bin");

    auto guard = uploads::allow({
        .validated_dir = validated.path,
        .project_root = validated.path.parent_path(),
        .types = {web::FileType::jpeg},
        .check_magic = true,
    });

    auto resp = guard({}, []() -> web::Response { return {}; });

    CHECK(resp.status == web::status::unprocessable_entity);
    CHECK(!std::filesystem::exists(src));
}

TEST_CASE("uploads::allow — max_size validator rejects oversized file", "[uploads]") {
    TempDir incoming, validated(std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                std::filesystem::perms::group_exec);

    auto src = incoming.path / "big.jpg";
    write_bytes(src, JPEG_MAGIC);
    set_context_file(src, "f", "big.jpg");

    auto guard = uploads::allow({
        .validated_dir = validated.path,
        .project_root = validated.path.parent_path(),
        .validators = {uploads::max_size(1)}, // 1 byte max; JPEG_MAGIC is 3 bytes
        .check_magic = false,
    });

    auto resp = guard({}, []() -> web::Response { return {}; });
    CHECK(resp.status == web::status::unprocessable_entity);
    CHECK(!std::filesystem::exists(src));
}

TEST_CASE("uploads::allow — custom validator receives mutable file ref", "[uploads]") {
    TempDir incoming, validated(std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                std::filesystem::perms::group_exec);

    auto src = incoming.path / "img.jpg";
    write_bytes(src, JPEG_MAGIC);
    set_context_file(src, "f", "img.jpg");

    web::FileType seen = web::FileType::unknown;
    auto guard = uploads::allow({
        .validated_dir = validated.path,
        .project_root = validated.path.parent_path(),
        .validators = {[&seen](web::UploadedFile& f) {
            seen = f.detected_type;
            return true;
        }},
    });

    guard({}, []() -> web::Response { return {}; });
    CHECK(seen == web::FileType::jpeg);
}

TEST_CASE("uploads::allow — jail escape rejected with 500", "[uploads]") {
    TempDir incoming;
    TempDir project_root; // project root is a separate temp dir

    // validated_dir is OUTSIDE project_root — should be rejected
    TempDir outside_dir(std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                        std::filesystem::perms::group_exec);

    auto src = incoming.path / "img.jpg";
    write_bytes(src, JPEG_MAGIC);
    set_context_file(src, "f", "img.jpg");

    auto guard = uploads::allow({
        .validated_dir = outside_dir.path,
        .project_root = project_root.path, // outside_dir is not under project_root
        .check_magic = false,
    });

    auto resp = guard({}, []() -> web::Response { return {}; });
    CHECK(resp.status == web::status::internal_error);
}

TEST_CASE("uploads::allow — world-readable validated_dir rejected with 500", "[uploads]") {
    TempDir incoming;
    TempDir world_readable(std::filesystem::perms::all); // 777

    auto src = incoming.path / "img.jpg";
    write_bytes(src, JPEG_MAGIC);
    set_context_file(src, "f", "img.jpg");

    auto guard = uploads::allow({
        .validated_dir = world_readable.path,
        .project_root = world_readable.path.parent_path(),
        .check_magic = false,
    });

    auto resp = guard({}, []() -> web::Response { return {}; });
    CHECK(resp.status == web::status::internal_error);
}

TEST_CASE("uploads::allow — no files skips validation and calls next", "[uploads]") {
    TempDir validated(std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                      std::filesystem::perms::group_exec);

    web::Context::current().files.clear();

    bool called = false;
    auto guard = uploads::allow({
        .validated_dir = validated.path,
        .project_root = validated.path.parent_path(),
    });

    auto resp = guard({}, [&]() -> web::Response {
        called = true;
        return {};
    });
    CHECK(called);
    CHECK(resp.status == web::status::ok);
}

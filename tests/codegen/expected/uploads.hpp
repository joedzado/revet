// Auto-generated — do not edit
#pragma once
#include <filesystem>
#include <string_view>

namespace uploads {

namespace files {
    struct IncomingPath  { std::filesystem::path value; };
    struct ValidatedPath { std::filesystem::path value; };

    inline constexpr std::string_view incoming_dir  = "app/uploads/files/incoming";
    inline constexpr std::string_view validated_dir = "app/uploads/files/validated";

    inline IncomingPath  incoming (std::string_view filename) { return { std::filesystem::path("app/uploads/files/incoming")  / filename }; }
    inline ValidatedPath validated(std::string_view filename) { return { std::filesystem::path("app/uploads/files/validated") / filename }; }
} // namespace files

namespace avatars {
    struct IncomingPath  { std::filesystem::path value; };
    struct ValidatedPath { std::filesystem::path value; };

    inline constexpr std::string_view incoming_dir  = "app/uploads/avatars/incoming";
    inline constexpr std::string_view validated_dir = "app/uploads/avatars/validated";

    inline IncomingPath  incoming (std::string_view filename) { return { std::filesystem::path("app/uploads/avatars/incoming")  / filename }; }
    inline ValidatedPath validated(std::string_view filename) { return { std::filesystem::path("app/uploads/avatars/validated") / filename }; }
} // namespace avatars

} // namespace uploads

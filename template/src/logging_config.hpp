#pragma once

// Auto-generated logging configuration from revet.toml
namespace log_config {
    constexpr bool ENABLED = true;
    inline void init() {
        if constexpr (ENABLED) {
            ::log::set_level(::log::Level::INFO);
        }
    }
}

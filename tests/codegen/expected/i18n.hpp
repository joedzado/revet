// Auto-generated — do not edit
#pragma once
#include <revet/i18n.hpp>
#include <revet/html.hpp>
#include "styles.hpp"

namespace i18n {

enum class Locale { en, es, fr };
inline constexpr std::string_view locale_names[] = { "en", "es", "fr" };
inline constexpr int locale_count = 3;

inline void set_locale(std::string_view name) {
    for (int i = 0; i < locale_count; ++i)
        if (locale_names[i] == name) { detail::locale_idx = i; return; }
    detail::locale_idx = 0;
}

inline auto current_locale() -> Locale {
    return static_cast<Locale>(detail::locale_idx);
}

// locale_picker() — renders a POST form with one button per locale.
// Active locale gets btn_solid styling; others get btn_ghost.
inline auto locale_picker() -> html::Node {
    return html::form{html::attr.method("get"), html::attr.action("/set_locale"),
        html::button{html::attr.type("submit"), html::attr.name("lang"), html::attr.value("en"), styles::btn, detail::locale_idx == 0 ? html::Node(styles::btn_solid) : html::Node(styles::btn_ghost), "EN"},
        html::button{html::attr.type("submit"), html::attr.name("lang"), html::attr.value("es"), styles::btn, detail::locale_idx == 1 ? html::Node(styles::btn_solid) : html::Node(styles::btn_ghost), "ES"},
        html::button{html::attr.type("submit"), html::attr.name("lang"), html::attr.value("fr"), styles::btn, detail::locale_idx == 2 ? html::Node(styles::btn_solid) : html::Node(styles::btn_ghost), "FR"},
    };
}

inline constexpr Str<3> nav_section_pages{{"Pages", "Páginas", "Pages"}};
inline constexpr Str<3> nav_home{{"Home", "Inicio", "Accueil"}};
inline constexpr Str<3> nav_users{{"Users", "Usuarios", "Utilisateurs"}};
inline constexpr Str<3> nav_contact{{"Contact", "Contacto", "Contact"}};
inline constexpr Str<3> nav_about{{"About", "Acerca", "À propos"}};
inline constexpr Str<3> nav_components{{"Components", "Componentes", "Composants"}};
inline constexpr Str<3> nav_nested{{"Nested", "Anidado", "Imbriqué"}};

} // namespace i18n

// Auto-generated — do not edit
#pragma once
#include <revet/web.hpp>
#include "states.hpp"
#include "models.hpp"
#include "../../types/shared.hpp"

#line 4 "<PROJ>/app/components/ui.cpp"
html::Node sec_label(std::string_view text);
#line 9 "<PROJ>/app/components/ui.cpp"
html::Node snippet_block(std::string_view label, std::string_view code);
#line 17 "<PROJ>/app/components/ui.cpp"
html::Node alert_err(html::Node content);
#line 22 "<PROJ>/app/components/ui.cpp"
html::Node alert_ok(html::Node content);
#line 27 "<PROJ>/app/components/ui.cpp"
html::Node card(std::string_view mono, std::string_view title, std::string_view body);
#line 36 "<PROJ>/app/components/ui.cpp"
html::Node card_grid(html::Node items);
#line 92 "<PROJ>/app/layouts/layout.cpp"
html::Node breadcrumb(std::string_view href, std::string_view label);
#line 98 "<PROJ>/app/layouts/layout.cpp"
web::Response layout_page(const web::Request& req, std::string_view title, std::string_view active, html::Node content);

namespace web::components {
}
#include <array>
#include <revet/ui.hpp>

static auto render_get(const auto& req) {
    auto make_card = [](std::string_view initials, std::string_view name) {
        return html::div{styles::team_card,
            html::div{styles::team_avatar, initials},
            html::div{styles::team_name,   name},
        };
    };

    auto cards =
        make_card("NP", "Nora Padding")  +
        make_card("RV", "Rex Vec")       +
        make_card("Z",  "nullptr");

    auto content =
        breadcrumb(urls::about, i18n::nav_about) +
        html::div{styles::hero,
            html::h1{i18n::page_team_h1},
            html::p{i18n::page_team_body},
        } +
        html::div{styles::stat_strip,
            html::div{styles::stat, html::span{styles::stat_n, "0"},   html::span{styles::stat_l, i18n::stat_runtime_deps}},
            html::div{styles::stat, html::span{styles::stat_n, "~2k"}, html::span{styles::stat_l, i18n::stat_lines_core}},
            html::div{styles::stat, html::span{styles::stat_n, "∞"},   html::span{styles::stat_l, i18n::stat_checks}},
        } +
        sec_label(i18n::section_contributors) +
        html::div{styles::team_grid, std::move(cards)} +
        html::h2{i18n::h2_getting_involved} +
        html::p{i18n::getting_involved_body} +
        ui::toggle("getting-started", i18n::toggle_dev_commands,
            html::pre{
R"sh(./scripts/revet-fw run      # start the server
./scripts/revet-fw test     # build with ASAN + run all tests
./scripts/revet-fw install  # deploy to ~/.revet/current/  (requires sudo))sh"
            }
        );

    return layout_page(req, i18n::page_team_h1, urls::about_team, content);
}

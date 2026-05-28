#include <revet/web.hpp>
#include <revet/csrf.hpp>
#include <revet/htmx.hpp>
#include <revet/log.hpp>
#include "components.hpp"
#include "resources.hpp"
#include "queries.hpp"

static const log::TuTag LOG(log::tags::ROUTES);

static constexpr html::typed_id<int> k_counter{"counter-value"};

// ── Private components ────────────────────────────────────────────────────────

static auto dash_header() -> html::Node {
    return html::div{styles::dash_header,
        html::h1{styles::page_title, "Dashboard"},
        html::p{styles::dash_subtitle,
            "A live demo of revet — file-based, server-rendered C++23 web framework."
        }
    };
}

static auto dash_stats(int user_count, int counter_val) -> html::Node {
    return html::div{styles::dash_stats,
        html::div{styles::dash_stat,
            html::div{styles::dash_stat_value, user_count},
            html::div{styles::dash_stat_label, "Users in database"}
        },
        html::div{styles::dash_stat,
            html::div{styles::dash_stat_value, counter_val},
            html::div{styles::dash_stat_label, "Counter (state cookie)"}
        },
        html::div{styles::dash_stat,
            html::div{styles::dash_stat_value, "C++23"},
            html::div{styles::dash_stat_label, "Language standard"}
        }
    };
}

static auto counter_card(const web::Request& req, int val) -> html::Node {
    return html::div{styles::card, styles::counter_box,
        html::h2{"State Cookie Counter"},
        html::p{"Encrypted counter stored in a cookie. State is hoisted into this handler by codegen."},
        html::div{k_counter, styles::counter_value, val},
        html::form{
            html::attr.post(),
            html::attr.action(urls::index),
            htmx::attr::post(urls::index),
            htmx::attr::target(k_counter),
            htmx::attr::swap_outer_html(),
            htmx::optimistic(k_counter, +1),
            web::csrf::field(req),
            html::submit{styles::btn, "Increment"}
        }
    };
}

static auto get_started_card() -> html::Node {
    return html::div{styles::card,
        html::h3{"Get Started"},
        html::div{styles::terminal,
            html::div{styles::terminal_header,
                html::div{styles::dot, styles::red},
                html::div{styles::dot, styles::yellow},
                html::div{styles::dot, styles::green}
            },
            html::div{styles::terminal_content,
                html::span{"$ "}, "revet new my-app", html::br{},
                html::span{"$ "}, "cd my-app && revet dev", html::br{},
                html::span{"● "}, "running  http://localhost:8080"
            }
        }
    };
}

static auto user_row(const User& u) -> html::Node {
    std::string initial = u.name.empty() ? "?" : std::string{u.name[0]};
    return html::a{styles::user_row, html::attr.href(urls::users__id_(u.id)),
        html::div{styles::user_avatar, initial},
        html::div{styles::user_info,
            html::div{styles::user_name,  u.name},
            html::div{styles::user_email, u.email}
        },
        html::span{styles::user_id,        "#" + u.id},
        html::span{styles::user_row_arrow, "→"}
    };
}

static auto recent_users_section(const std::vector<User>& users) -> html::Node {
    return html::div{
        html::div{styles::dash_section_header,
            html::div{
                html::h2{styles::dash_section_title, "Recent Users"},
                badge("live · sqlite")
            },
            html::a{styles::btn, styles::btn_secondary, styles::btn_sm,
                html::attr.href(urls::users), "View all →"}
        },
        html::div{styles::user_list,
            html::list(users | std::views::take(3), user_row)
        }
    };
}

// ── Route handlers ────────────────────────────────────────────────────────────

static auto render_get(const web::Request& req, web::State<Counter> counter) {
    int val = counter ? counter->count : 0;
    log::info("GET / - counter={}", val);

    const auto db    = resources::users_db();
    const auto users = queries::users::sync_all(db);

    const auto content = html::div{styles::container, styles::page_content, styles::animate,
        dash_header(),
        dash_stats(static_cast<int>(users.size()), val),
        html::div{styles::dash_grid,
            counter_card(req, val),
            get_started_card()
        },
        recent_users_section(users)
    };

    return layout_main(req, "Dashboard — {{name}}", content);
}

static auto render_post(const web::Request& req, web::State<Counter> counter) {
    int val = counter ? counter->count : 0;
    int next = val + 1;
    log::info("POST / - increment {} → {}", val, next);
    if (htmx::is_request(req)) {
        return web::to_response(html::div{k_counter, styles::counter_value, next})
            .state(Counter{next});
    }
    return web::redirect(urls::index).state(Counter{next});
}

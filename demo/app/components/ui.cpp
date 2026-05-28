#include "styles.hpp"
#include <revet/web.hpp>

[[revet::export]]
html::Node sec_label(std::string_view text) {
    return html::div{styles::sec_label, text};
}

[[revet::export]]
html::Node snippet_block(std::string_view label, std::string_view code) {
    return html::div{styles::snippet_block,
        html::div{styles::sec_label, label},
        html::pre{code},
    };
}

[[revet::export]]
html::Node alert_err(html::Node content) {
    return html::div{styles::alert, styles::alert_err, std::move(content)};
}

[[revet::export]]
html::Node alert_ok(html::Node content) {
    return html::div{styles::alert, styles::alert_ok, std::move(content)};
}

[[revet::export]]
html::Node card(std::string_view mono, std::string_view title, std::string_view body) {
    return html::div{styles::card,
        html::div{styles::card_mono, mono},
        html::h3{title},
        html::p{body},
    };
}

[[revet::export]]
html::Node card_grid(html::Node items) {
    return html::div{styles::cards, std::move(items)};
}

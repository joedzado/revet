// `revet packages`: browse, add, upgrade and remove Conan dependencies (FTXUI).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <map>
#include <thread>
#include <utility>

#include "cli.hpp"

namespace revet::cli {
namespace {

using namespace ftxui; // NOLINT(google-build-using-namespace): FTXUI is written as a DSL

enum class View : std::uint8_t { Installed, Search, Versions };

constexpr int k_name_w = 22;
constexpr int k_ver_w = 12;
constexpr int k_lat_w = 12;

struct State {
    Deps installed;
    std::map<std::string, std::string> latest; // name -> newest ref, filled in the background
    View view = View::Installed;

    int inst_cur = 0;

    std::string query;
    std::vector<std::pair<std::string, std::string>> results; // (name, newest ref)
    int srch_cur = 0;
    bool searching = false;
    unsigned query_gen = 0; // bumped per keystroke; a search runs when it settles

    std::string ver_pkg;
    std::vector<std::string> versions;
    int ver_cur = 0;
    bool ver_loading = false;
    View ver_origin = View::Installed;

    std::string status;
};

// Background work posts results back to the UI thread. The screen and state are never
// freed: threads still waiting on `conan list` may post after the loop has ended.
App* g_screen = nullptr;
State* g_st = nullptr;

std::atomic<bool> g_bg_failed{false};

// Background tasks. in_background catches everything, but clang-tidy does not see through its
// catch (...) and flags every task lambda.
// NOLINTBEGIN(bugprone-exception-escape)

// Runs `f` on a detached thread. An exception escaping it would call std::terminate.
template<class F>
void in_background(F f) {
    std::thread([f = std::move(f)] mutable noexcept -> auto {
        try {
            f();
        } catch (...) {
            g_bg_failed = true;
        }
    }).detach();
}

void post(std::function<void()> task) {
    g_screen->Post(std::move(task));
    g_screen->PostEvent(Event::Custom);
}

void bg_latest(std::vector<std::string> names) {
    in_background([names = std::move(names)] -> void {
        for (const auto& name : names) {
            if (auto ref = conan_latest(name, 12)) {
                post([name, ref = *ref] -> void { g_st->latest[name] = ref; });
            }
        }
    });
}

void bg_search(const std::string& q) {
    in_background([q] -> void {
        Result r = run({"conan", "list", q + "*", "-r", "conancenter"}, {}, 15);
        std::map<std::string, std::vector<std::string>> by_pkg;
        if (r.rc == 0) {
            for (auto& ref : conan_refs(r.out)) {
                by_pkg[ref.substr(0, ref.find('/'))].push_back(ref);
            }
        }
        std::vector<std::pair<std::string, std::string>> found;
        for (auto& [name, refs] : by_pkg) {
            sort_refs_newest_first(refs);
            found.emplace_back(name, refs.front());
            if (found.size() == 25) {
                break;
            }
        }
        post([q, found] -> void {
            State& st = *g_st;
            if (q != st.query) {
                return;
            }
            st.results.clear();
            for (const auto& f : found) {
                if (!find_dep(st.installed, f.first)) {
                    st.results.push_back(f);
                }
            }
            st.srch_cur = std::clamp(st.srch_cur, 0, std::max(0, static_cast<int>(st.results.size()) - 1));
            st.searching = false;
        });
    });
}

void bg_versions(const std::string& pkg) {
    in_background([pkg] -> void {
        Result r = run({"conan", "list", pkg + "/*", "-r", "conancenter"}, {}, 15);
        std::vector<std::string> refs = r.rc == 0 ? conan_refs(r.out, pkg) : std::vector<std::string>{};
        sort_refs_newest_first(refs);
        post([pkg, refs] -> void {
            State& st = *g_st;
            if (pkg != st.ver_pkg) {
                return;
            }
            st.versions = refs;
            st.ver_loading = false;
            const std::string* cur = find_dep(st.installed, pkg);
            auto it = cur ? std::ranges::find(refs, *cur) : refs.end();
            st.ver_cur = it == refs.end() ? 0 : static_cast<int>(it - refs.begin());
        });
    });
}

// NOLINTEND(bugprone-exception-escape)

// Next build re-runs Conan; the lockfile no longer matches.
void invalidate() {
    std::error_code ec;
    fs::remove(fs::path(k_build_dir) / "conan" / ".revet_deps_hash", ec);
    fs::remove("revet.lock", ec);
}

void set_dep(State& st, const std::string& name, const std::string& ref) {
    std::string content = upsert_dep(read_toml(), name, ref);
    write_toml(content);
    st.installed = deps_from_toml(content);
    invalidate();
}

void drop_installed_from_results(State& st) {
    std::erase_if(st.results, [&](const auto& r) -> auto { return find_dep(st.installed, r.first) != nullptr; });
    st.srch_cur = std::clamp(st.srch_cur, 0, std::max(0, static_cast<int>(st.results.size()) - 1));
}

void open_versions(State& st, const std::string& pkg, View origin) {
    st.ver_pkg = pkg;
    st.versions.clear();
    st.ver_cur = 0;
    st.ver_loading = true;
    st.ver_origin = origin;
    st.view = View::Versions;
    bg_versions(pkg);
}

// ── Rendering ───────────────────────────────────────────────────────────────

auto cell(const std::string& s, int width) -> Element {
    return text(s) | size(WIDTH, EQUAL, width);
}

auto tab(const std::string& label, bool active) -> Element {
    Element e = text(" " + label + " ");
    return active ? (e | bold | underlined) : (e | ftxui::dim);
}

auto render_installed(const State& st) -> Element {
    if (st.installed.empty()) {
        return text("    No packages installed.  Press [tab] to search.") | ftxui::dim;
    }
    Elements rows;
    rows.push_back(hbox({text("  "), cell("Package", k_name_w), text("  "), cell("Installed", k_ver_w), text("  "),
                         cell("Latest", k_lat_w)}) |
                   bold | ftxui::dim);
    for (size_t i = 0; i < st.installed.size(); ++i) {
        const auto& [name, ref] = st.installed[i];
        auto lat = st.latest.find(name);
        bool known = lat != st.latest.end();
        bool upgradeable = known && lat->second != ref;
        std::string lat_ver = known ? ref_version(lat->second) : "…";
        Element sym = text("  ");
        if (upgradeable) {
            sym = text(" ↑") | color(Color::Yellow);
        } else if (known) {
            sym = text(" ✔") | color(Color::Green);
        }
        Element row = hbox({text("  "), cell(name, k_name_w), text("  "), cell(ref_version(ref), k_ver_w), text("  "),
                            cell(lat_ver, k_lat_w), text("  "), sym});
        bool sel = std::cmp_equal(i, st.inst_cur);
        rows.push_back(sel ? (row | color(Color::Cyan) | inverted | focus) : row);
    }
    return vbox(std::move(rows)) | yframe;
}

auto render_search(const State& st) -> Element {
    Elements rows;
    rows.push_back(hbox({text("  Search ▸ ") | bold, text(st.query), text(" ") | inverted}));
    if (st.searching) {
        rows.push_back(text(""));
        rows.push_back(text("    searching…") | color(Color::Yellow));
    } else if (!st.results.empty()) {
        rows.push_back(hbox({text("    "), cell("Package", k_name_w), text("  "), cell("Latest", k_ver_w)}) | bold |
                       ftxui::dim);
        Elements list;
        for (size_t j = 0; j < st.results.size(); ++j) {
            bool sel = std::cmp_equal(j, st.srch_cur);
            Element row = hbox({text(sel ? "  ▶ " : "    "), cell(st.results[j].first, k_name_w), text("  "),
                                cell(ref_version(st.results[j].second), k_ver_w)});
            list.push_back(sel ? (row | color(Color::Cyan) | inverted | focus) : row);
        }
        rows.push_back(vbox(std::move(list)) | yframe | flex);
    } else if (!st.query.empty()) {
        rows.push_back(text(""));
        rows.push_back(text("    no results") | ftxui::dim);
    }
    return vbox(std::move(rows));
}

auto render_versions(const State& st) -> Element {
    Elements rows;
    if (st.ver_loading) {
        rows.push_back(text("  loading versions…") | color(Color::Yellow));
    } else if (st.versions.empty()) {
        rows.push_back(text("  no versions found") | ftxui::dim);
    } else {
        const std::string* cur = find_dep(st.installed, st.ver_pkg);
        for (size_t i = 0; i < st.versions.size(); ++i) {
            bool installed = cur != nullptr && *cur == st.versions[i];
            bool sel = std::cmp_equal(i, st.ver_cur);
            Element row = text(std::string("  ") + (installed ? " ✔ " : "   ") + ref_version(st.versions[i]));
            if (installed) {
                row = row | color(Color::Green);
            }
            rows.push_back(sel ? (row | inverted | focus) : row);
        }
    }
    Element body = vbox(std::move(rows)) | yframe | size(HEIGHT, LESS_THAN, 20) | size(WIDTH, EQUAL, 44);
    return window(text(" versions: " + st.ver_pkg + " ") | bold,
                  vbox({body, separator(), text(" [enter] select  [esc] back ") | ftxui::dim | center})) |
           clear_under | center;
}

auto render(const State& st) -> Element {
    View tab_view = st.view == View::Versions ? st.ver_origin : st.view;
    std::string hints;
    Element content;
    if (tab_view == View::Installed) {
        content = render_installed(st);
        hints = " [↑↓] navigate  [enter/v] pick version  [u] upgrade  [r] remove  [tab] search  [q] quit ";
    } else {
        content = render_search(st);
        hints = " [↑↓] navigate  [enter] add latest  [v] pick version  [esc] clear  [tab] installed ";
    }
    if (st.view == View::Versions) {
        hints = " [↑↓] navigate  [enter] select version  [esc] back  [q] quit ";
    }

    Element status = text("");
    if (!st.status.empty()) {
        status = text("  " + st.status) | color(starts_with(st.status, "✔") ? Color::Green : Color::Yellow);
    } else if (g_bg_failed) {
        status = text("  a conan lookup failed; results may be incomplete") | color(Color::Yellow);
    }
    Element page = vbox({
        text(" revet packages ") | bold | color(Color::Cyan) | center,
        separator() | color(Color::Cyan),
        hbox({text(" "), tab("Installed", tab_view == View::Installed), text(" "),
              tab("Search", tab_view == View::Search), filler(), text("[tab] switch ") | ftxui::dim}),
        text(""),
        content | flex,
        status,
        text(hints) | color(Color::Cyan) | ftxui::dim,
    });
    if (st.view == View::Versions) {
        return dbox({page, render_versions(st)});
    }
    return page;
}

// ── Keys ────────────────────────────────────────────────────────────────────

void schedule_search(State& st) {
    unsigned gen = ++st.query_gen;
    st.status.clear();
    in_background([gen] -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        post([gen] -> void {
            State& s = *g_st;
            if (gen != s.query_gen || s.view != View::Search) {
                return;
            }
            s.results.clear();
            s.searching = !s.query.empty();
            if (s.searching) {
                bg_search(s.query);
            }
        });
    });
}

auto on_installed(State& st, const Event& e) -> bool {
    auto n = static_cast<int>(st.installed.size());
    if (e == Event::Character('q') || e == Event::Escape) {
        g_screen->Exit();
    } else if (e == Event::Tab) {
        st.view = View::Search;
        st.status.clear();
    } else if (e == Event::ArrowUp) {
        st.inst_cur = std::max(0, st.inst_cur - 1);
    } else if (e == Event::ArrowDown) {
        st.inst_cur = (n != 0) ? std::min(n - 1, st.inst_cur + 1) : 0;
    } else if ((e == Event::Return || e == Event::Character('v')) && n != 0) {
        open_versions(st, st.installed[static_cast<size_t>(st.inst_cur)].first, View::Installed);
    } else if (e == Event::Character('u') && n != 0) {
        std::string name = st.installed[static_cast<size_t>(st.inst_cur)].first;
        auto lat = st.latest.find(name);
        if (lat == st.latest.end()) {
            st.status = "Still checking latest version for " + name + "…";
        } else if (lat->second != *find_dep(st.installed, name)) {
            std::string ref = lat->second;
            set_dep(st, name, ref);
            st.status = "✔ upgraded " + name + " → " + ref_version(ref) + "  (run revet build)";
        } else {
            st.status = name + " is already at latest (" + ref_version(lat->second) + ")";
        }
    } else if (e == Event::Character('r') && n != 0) {
        std::string name = st.installed[static_cast<size_t>(st.inst_cur)].first;
        std::string content = remove_dep(read_toml(), name).first;
        write_toml(content);
        st.installed = deps_from_toml(content);
        invalidate();
        st.inst_cur = std::clamp(st.inst_cur, 0, std::max(0, static_cast<int>(st.installed.size()) - 1));
        st.status = "✔ removed " + name + "  (run revet build)";
    } else {
        return false;
    }
    return true;
}

auto on_search(State& st, const Event& e) -> bool {
    auto n = static_cast<int>(st.results.size());
    if ((e == Event::Escape && st.query.empty()) || e == Event::Tab) {
        st.view = View::Installed;
        st.status.clear();
    } else if (e == Event::Escape) {
        st.query.clear(); // a second Esc goes back to Installed
        schedule_search(st);
    } else if (e == Event::Backspace) {
        if (!st.query.empty()) {
            st.query.pop_back();
        }
        schedule_search(st);
    } else if (e == Event::ArrowUp) {
        st.srch_cur = std::max(0, st.srch_cur - 1);
    } else if (e == Event::ArrowDown) {
        st.srch_cur = (n != 0) ? std::min(n - 1, st.srch_cur + 1) : 0;
    } else if (e == Event::Return && n != 0) {
        auto [name, ref] = st.results[static_cast<size_t>(st.srch_cur)];
        set_dep(st, name, ref);
        drop_installed_from_results(st);
        st.status = "✔ added " + ref + "  (run revet build to install)";
        bg_latest({name});
    } else if (e == Event::Character('v') && n != 0) {
        open_versions(st, st.results[static_cast<size_t>(st.srch_cur)].first, View::Search);
    } else if (e.is_character() && e.character().size() == 1 && e.character()[0] >= 32 && e.character()[0] <= 126) {
        st.query += e.character();
        schedule_search(st);
    } else {
        return false;
    }
    return true;
}

auto on_versions(State& st, const Event& e) -> bool {
    auto n = static_cast<int>(st.versions.size());
    if (e == Event::Escape || e == Event::Character('q')) {
        st.view = st.ver_origin;
        st.status.clear();
    } else if (e == Event::ArrowUp) {
        st.ver_cur = std::max(0, st.ver_cur - 1);
    } else if (e == Event::ArrowDown) {
        st.ver_cur = (n != 0) ? std::min(n - 1, st.ver_cur + 1) : 0;
    } else if (e == Event::Return && n != 0) {
        std::string ref = st.versions[static_cast<size_t>(st.ver_cur)];
        set_dep(st, st.ver_pkg, ref);
        if (st.ver_origin == View::Search) {
            drop_installed_from_results(st);
        }
        st.view = st.ver_origin;
        st.status = "✔ set " + st.ver_pkg + " → " + ref_version(ref) + "  (run revet build)";
    } else {
        return false;
    }
    return true;
}

} // namespace

auto cmd_packages() -> int {
    // Never freed: background threads may still post to them after the screen closes.
    g_screen = new App(App::Fullscreen()); // NOLINT(cppcoreguidelines-owning-memory)
    g_st = new State();                    // NOLINT(cppcoreguidelines-owning-memory)
    State& st = *g_st;
    st.installed = deps_from_toml(read_toml());
    if (!st.installed.empty()) {
        std::vector<std::string> names;
        for (const auto& d : st.installed) {
            names.push_back(d.first);
        }
        bg_latest(names);
    }

    auto ui = Renderer([] -> Element { return render(*g_st); }) | CatchEvent([](const Event& e) -> bool {
                  State& s = *g_st;
                  if (e == Event::Custom) {
                      return true;
                  }
                  switch (s.view) {
                      case View::Installed:
                          return on_installed(s, e);
                      case View::Search:
                          return on_search(s, e);
                      case View::Versions:
                          return on_versions(s, e);
                  }
                  return false;
              });
    g_screen->Loop(ui);
    return 0;
}

} // namespace revet::cli

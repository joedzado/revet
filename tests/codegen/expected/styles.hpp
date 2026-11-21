// Auto-generated — do not edit
#pragma once
#include <revet/css.hpp>

namespace styles {

#line 79 "<PROJ>/app/styles/layout.css"
inline constexpr html::named_class btn_icon{"btn-icon"};
#line 88 "<PROJ>/app/styles/layout.css"
inline constexpr html::named_class or_{"or"};
#line 88 "<PROJ>/app/styles/layout.css"
inline constexpr html::named_class not_{"not"};
#line 89 "<PROJ>/app/styles/layout.css"
inline constexpr html::named_class float_end{"float-end"};
#line 89 "<PROJ>/app/styles/layout.css"
inline constexpr html::named_class default_{"default"};
} // namespace styles

namespace ids {

inline constexpr html::id sb_cb{"sb-cb"};
inline constexpr html::id sidebar{"sidebar"};
inline constexpr html::id wrap{"wrap"};

} // namespace ids

namespace html::detail {
inline const std::string _all_css_str = "@font-face {\n  font-family: 'JetBrains Mono';\n  font-style: normal;\n  font-weight: 400;\n  font-display: block;\n  src: url('/fonts/JetBrainsMono-Regular.woff2') format('woff2');\n}\n\n@font-face {\n  font-family: 'JetBrains Mono';\n  font-style: italic;\n  font-weight: 400;\n  font-display: block;\n  src: url('/fonts/JetBrainsMono-Italic.woff2') format('woff2');\n}\n\n@font-face {\n  font-family: 'JetBrains Mono';\n  font-style: normal;\n  font-weight: 500;\n  font-display: block;\n  src: url('/fonts/JetBrainsMono-Medium.woff2') format('woff2');\n}\n\n@font-face {\n  font-family: 'JetBrains Mono';\n  font-style: italic;\n  font-weight: 500;\n  font-display: block;\n  src: url('/fonts/JetBrainsMono-MediumItalic.woff2') format('woff2');\n}\n\n@font-face {\n  font-family: 'IBM Plex Sans';\n  font-style: normal;\n  font-weight: 300 500;\n  font-display: block;\n  src: url('/fonts/ibm-plex-sans.woff2') format('woff2');\n}\n\n*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}\n\n:root {\n  --sw:         272px;\n  --hh:         56px;\n  --r:          6px;\n  --shadow-sm:  0 8px 24px rgba(0,0,0,.12);\n  --shadow-lg:  0 20px 60px rgba(0,0,0,.22);\n  --shadow-bd:  rgba(0,0,0,.5);\n  --mono:       'JetBrains Mono', monospace;\n}\n\n@media (prefers-color-scheme: dark) {\n  body {\n    --bg:         #060b14;\n    --surface:    #0c1220;\n    --surface2:   #111927;\n    --border:     #1c2a3a;\n    --text:       #e0e8f4;\n    --muted:      #7086a0;\n    --accent:     #38bdf8;\n    --accent-dim: #0ea5e9;\n    --accent-fg:  #001220;\n    --code-bg:    #040810;\n    --code-fg:    #7dd3fc;\n    --ok:         #4ade80;\n    --ok-bg:      #042011;\n    --ok-bd:      #166534;\n    --err:        #fca5a5;\n    --err-bg:     #3b0808;\n    --err-bd:     #991b1b;\n    --focus-ring: rgba(56,189,248,.15);\n  }\n}\n\nbody {\n  --bg:         #f4f1eb;\n  --surface:    #eae7e0;\n  --surface2:   #dedad2;\n  --border:     #cdc9bf;\n  --text:       #1b1916;\n  --muted:      #6c6860;\n  --accent:     #2563eb;\n  --accent-dim: #1d4ed8;\n  --accent-fg:  #fff;\n  --code-bg:    #e4e0d8;\n  --code-fg:    #1e293b;\n  --ok:         #166534;\n  --ok-bg:      #dcfce7;\n  --ok-bd:      #86efac;\n  --err:        #991b1b;\n  --err-bg:     #fee2e2;\n  --err-bd:     #fca5a5;\n  --focus-ring: rgba(37,99,235,.2);\n  \n  font-family: 'IBM Plex Sans', sans-serif;\n  background: var(--bg);\n  color: var(--text);\n  line-height: 1.65;\n  font-size: 16px;\n  -webkit-font-smoothing: antialiased;\n  color-scheme: light dark;\n}\n\n/* ── Sidebar ─────────────────────────────────────────────────────────────── */\n#sidebar {\n  position: fixed; top: 0; left: 0;\n  width: var(--sw); height: 100vh;\n  background: var(--surface);\n  border-right: 1px solid var(--border);\n  display: flex; flex-direction: column;\n  z-index: 200;\n  transform: translateX(calc(-1 * var(--sw)));\n  overflow: hidden;\n  transition: transform 0.2s ease-in-out;\n}\n\n\n.btn-icon {\n  background: none; border: none; padding: 4px 8px;\n  cursor: pointer; font-size: 1.1rem; color: var(--text);\n  display: flex; align-items: center; justify-content: center;\n  border-radius: var(--r);\n  transition: background 0.1s;\n}\n\n/* Class names that are C++ keywords or alternative tokens. */\n.or, .not { opacity: .6 }\n.float-end, .default { float: right }\n\n/* ── Sidebar state (pure CSS checkbox — no JS, no server round-trip) ─── */\n#sb-cb { position:absolute; opacity:0; width:0; height:0; pointer-events:none; }\nbody:has(#sb-cb:checked) #sidebar { transform: translateX(0); }\n@media (min-width: 769px) {\n  body:has(#sb-cb:checked) #wrap { margin-left: var(--sw); }\n}\n\n";
inline const bool _all_css_init = ([]{{ all_css_block = _all_css_str; return true; }})();
} // namespace html::detail

# Vendored third-party sources

| Library | Version | License | Source | Notes |
|---|---|---|---|---|
| CTRE | 3.11.0 | Apache-2.0 with LLVM exceptions (`ctre/LICENSE`) | hanickadot/compile-time-regular-expressions (include/ + LICENSE) | header-only |
| simdjson | 5.2.0 | Apache-2.0 (`simdjson/LICENSE`); upstream also offers MIT | simdjson/simdjson release (simdjson.h, simdjson.cpp, LICENSE) | amalgamation; embeds code under BSD-3-Clause, BSL-1.0 and MIT, notices inline |
| libsodium | 1.0.22 | ISC (`LICENSE` inside the tarball) | jedisct1/libsodium release tarball, sha256 adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349 | build with ./configure && make |
| FTXUI | 7.0.3 | MIT (`ftxui/LICENSE`) | ArthurSonzogni/FTXUI release (include/, src/, cmake/, LICENSE) | CMakeLists: examples/ and doc/ subdirs patched out |
| htmx | 2.0.4–2.0.11, 4.0.0-beta1–6, 4.0.0, 4.0.1 (default 2.0.11) | 0BSD (`htmx/LICENSE`) | bigskysoftware/htmx releases (htmx.min.js) | 0BSD needs no attribution |

All are compatible with revet's MIT license. Apps that ship binaries link libsodium and simdjson
(and serve htmx): their distributions should carry those licenses' notices.

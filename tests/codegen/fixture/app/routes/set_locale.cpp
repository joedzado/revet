// GET /set_locale?lang=es  — sets the locale cookie and redirects back.

static auto render_get(const web::Request& req) -> web::Response {
    auto lang = req.query.get("lang");
    // Validate against known locales; fall back to "en".
    bool valid = false;
    for (int i = 0; i < i18n::locale_count; ++i)
        if (i18n::locale_names[i] == lang) { valid = true; break; }
    if (!valid) lang = "en";

    auto ref = req.headers.get("referer", "/");
    auto resp = web::redirect(ref);
    resp.cookies.data["locale"] = std::string(lang);
    return resp;
}

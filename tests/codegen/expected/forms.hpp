// Auto-generated — do not edit
#pragma once
#include <revet/form.hpp>
#include <revet/web.hpp>

#include "../../app/forms/contact.hpp"

namespace forms {

inline void populate(ContactForm& f, const web::ParamMap& p) {
    f.email.name = "email";
    f.email.value = std::string(p.get("email"));
    f.name.name = "name";
    f.name.value = std::string(p.get("name"));
    f.message.name = "message";
    f.message.value = std::string(p.get("message"));
    if (auto v = p.get("phone"); !v.empty()) {
        if (!f.phone) f.phone.emplace();
        f.phone->name = "phone";
        f.phone->value = std::string(v);
    } else {
        f.phone.reset();
    }
}

inline validate::Result validate(ContactForm& f) {
    std::vector<validate::Error> errs;
    f.email.validate(true); if (!f.email.error.empty()) errs.push_back({"email", f.email.error});
    f.name.validate(true); if (!f.name.error.empty()) errs.push_back({"name", f.name.error});
    f.message.validate(true); if (!f.message.error.empty()) errs.push_back({"message", f.message.error});
    if (f.phone) { f.phone->validate(false); if (!f.phone->error.empty()) errs.push_back({"phone", f.phone->error}); }
    if (!errs.empty()) return std::unexpected(std::move(errs));
    return {};
}

} // namespace forms

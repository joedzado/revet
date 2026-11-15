// Auto-generated — do not edit
#pragma once
#include <revet/db.hpp>

namespace paths {
    constexpr auto public_dir = "app/public";
} // namespace paths

namespace resources {
namespace _pools {

inline db::sqlite::Pool& _pool_users_db() {
    static db::sqlite::Pool p{"app/data/edge.db", 2};
    return p;
}

inline db::postgres::Pool& _pool_analytics() {
    static db::postgres::Pool p{"postgres://localhost/edge", 4};
    return p;
}

} // namespace _pools

template<typename Intent = db::Read>
inline db::sqlite::Handle<Intent> users_db() { return db::sqlite::Handle<Intent>{_pools::_pool_users_db()}; }
template<typename Intent = db::Read>
inline db::postgres::Handle<Intent> analytics() { return db::postgres::Handle<Intent>{_pools::_pool_analytics()}; }

template<typename Intent = db::Read>
inline db::sqlite::Handle<Intent> db() { return users_db<Intent>(); }

} // namespace resources

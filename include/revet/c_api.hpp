#pragma once
// Pointer views for C APIs (libsodium, zlib, sqlite, BSD sockets) that take unsigned char or sockaddr.
// The only place revet reinterprets pointers. char and unsigned char may alias any object, and
// sockaddr_in/sockaddr_in6 are the layouts the socket API expects behind a sockaddr*.

#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>

namespace web::c_api {

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-const-cast)

inline auto uchars(std::string_view s) -> const unsigned char* {
    return reinterpret_cast<const unsigned char*>(s.data());
}

inline auto uchars(std::string& s) -> unsigned char* {
    return reinterpret_cast<unsigned char*>(s.data());
}

inline auto uchars(std::span<char> s) -> unsigned char* {
    return reinterpret_cast<unsigned char*>(s.data());
}

inline auto chars(std::span<const unsigned char> b) -> std::string_view {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

inline auto chars(std::span<unsigned char> b) -> char* {
    return reinterpret_cast<char*>(b.data());
}

// NUL-terminated text from a C API that returns unsigned char (sqlite3_column_text); null stays null.
inline auto c_str(const unsigned char* p) -> const char* {
    return reinterpret_cast<const char*>(p);
}

// zlib's next_in is non-const unless ZLIB_CONST is defined; inflate and deflate never write through it.
inline auto zlib_input(std::string_view s) -> unsigned char* {
    return const_cast<unsigned char*>(uchars(s));
}

template<typename SockAddr>
inline auto sockaddr_of(SockAddr& addr) -> sockaddr* {
    return reinterpret_cast<sockaddr*>(&addr);
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-const-cast)

} // namespace web::c_api

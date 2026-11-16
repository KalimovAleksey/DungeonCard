#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>

namespace dc {

inline std::string fmt(const char* f, ...) {
    char buf[320];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

}  // namespace dc

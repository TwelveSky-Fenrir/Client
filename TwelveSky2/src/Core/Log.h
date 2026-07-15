// Core/Log.h — journalisation minimale (header-only) pour le client réécrit.
#pragma once
#include <cstdio>
#include <cstdarg>

namespace ts2 {

inline void Logf(const char* level, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::printf("[%s] %s\n", level, buf);
    std::fflush(stdout);
}

} // namespace ts2

#define TS2_LOG(...)  ::ts2::Logf("INFO", __VA_ARGS__)
#define TS2_WARN(...) ::ts2::Logf("WARN", __VA_ARGS__)
#define TS2_ERR(...)  ::ts2::Logf("ERR ", __VA_ARGS__)

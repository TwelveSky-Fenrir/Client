// Core/Log.h — minimal (header-only) logging for the rewritten client.
// Writes to stdout AND to a "TwelveSky2.log" file (CWD): essential for diagnostics,
// since the client is a WinMain app (WINDOWS subsystem) whose stdout is NOT attached
// to the launching console — without the file, no runtime trace would be visible.
#pragma once
#include <cstdio>
#include <cstdarg>

namespace ts2 {

// Log file opened lazily on the 1st trace ("w" mode = fresh log per launch).
// Local static of an inline function => a single instance shared across all TUs.
inline std::FILE* LogFile() {
    static std::FILE* f = std::fopen("TwelveSky2.log", "w");
    return f;
}

inline void Logf(const char* level, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::printf("[%s] %s\n", level, buf);
    std::fflush(stdout);
    if (std::FILE* f = LogFile()) {
        std::fprintf(f, "[%s] %s\n", level, buf);
        std::fflush(f);
    }
}

} // namespace ts2

#define TS2_LOG(...)  ::ts2::Logf("INFO", __VA_ARGS__)
#define TS2_WARN(...) ::ts2::Logf("WARN", __VA_ARGS__)
#define TS2_ERR(...)  ::ts2::Logf("ERR ", __VA_ARGS__)

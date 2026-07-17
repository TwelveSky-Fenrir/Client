// Core/Log.h — journalisation minimale (header-only) pour le client réécrit.
// Écrit sur stdout ET dans un fichier "TwelveSky2.log" (CWD) : indispensable au diagnostic,
// car le client est une app WinMain (sous-système WINDOWS) dont le stdout n'est PAS rattaché
// à la console de lancement — sans le fichier, aucune trace runtime n'est visible.
#pragma once
#include <cstdio>
#include <cstdarg>

namespace ts2 {

// Fichier de log ouvert paresseusement à la 1re trace (mode "w" = journal frais par lancement).
// Static local d'une fonction inline => une seule instance partagée par toutes les TU.
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

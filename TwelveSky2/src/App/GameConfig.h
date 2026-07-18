// App/GameConfig.h — configuration parsed from the '/'-delimited command line.
// WinMain 0x4609C0: original launch invocation "TwelveSky2.exe /0/0/2/1024/768".
#pragma once
#include "Core/Types.h"

namespace ts2 {

struct GameConfig {
    int  buildVariant = 0;          // field 0 -> dword_166918C (build/server variant)
    int  useTRVariant = 0;          // field 1 -> g_UseTRVariant (dword_1669190, TR localization)
    int  windowMode   = 2;          // field 2 -> dword_1669180 (==2: centered bordered windowed; else fullscreen)
    int  width  = kRefWidth;        // field 3
    int  height = kRefHeight;       // field 4
    bool valid  = false;            // well-formed command line ('/'-prefixed, >=3 fields)

    bool Windowed() const { return windowMode == 2; }

    // Splits the '/'-delimited command line. Faithful: requires a '/' prefix.
    static GameConfig Parse(const char* cmdLine);
};

} // namespace ts2

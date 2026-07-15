// main.cpp — point d'entrée du client TwelveSky2 (réécriture fidèle).
// Équivaut à WinMain 0x4609C0 : délègue tout à ts2::App.
#include <windows.h>
#include "App/App.h"
#include "Tools/UiWindowSelfTest.h"
#include "Tools/WorldReflectionSelfTest.h"
#include <cstring>
#include <cstdlib>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrev*/, LPSTR lpCmdLine, int /*nShow*/) {
    if (lpCmdLine && std::strncmp(lpCmdLine, "-uiwindowtest", 13) == 0) {
        const char* args = lpCmdLine + 13;
        while (*args == ' ') ++args;
        char which[32] = {};
        int seconds = 8;
        int width = 0, height = 0; // 0 -> defaut kRefWidth/kRefHeight (RunUiWindowSelfTest)

        std::sscanf(args, "%31s %d %d %d", which, &seconds, &width, &height);
        return ts2::tools::RunUiWindowSelfTest(which, seconds, width, height);
    }
    if (lpCmdLine && std::strncmp(lpCmdLine, "-reflectiontest", 15) == 0) {
        const char* args = lpCmdLine + 15;
        while (*args == ' ') ++args;
        int seconds = 8, width = 0, height = 0;
        std::sscanf(args, "%d %d %d", &seconds, &width, &height);
        return ts2::tools::RunWorldReflectionSelfTest(seconds, width, height);
    }
    ts2::App app;
    return app.Run(hInstance, lpCmdLine);
}

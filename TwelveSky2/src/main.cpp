// main.cpp — point d'entrée du client TwelveSky2 (réécriture fidèle).
// Équivaut à WinMain 0x4609C0 : délègue tout à ts2::App.
#include <windows.h>
#include "App/App.h"
#include "Tools/UiWindowSelfTest.h"
#include "Tools/WorldReflectionSelfTest.h"
#include "Tools/CharSelectSelfTest.h"
#include "Tools/WorldSelfTest.h"
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
    // -charselecttest [seconds] [width] [height] : PREVIEW de l'ecran CharSelect/CreateChar
    // (scene 4, inatteignable hors serveur). Force la scene, injecte 2 persos par defaut, et
    // ouvre une fenetre INTERACTIVE (clic = CRÉER/fleches/selection). seconds<=0 => jusqu'a
    // fermeture. Sert a VOIR le rendu 3D perso + la creation sans serveur de login.
    if (lpCmdLine && std::strncmp(lpCmdLine, "-charselecttest", 15) == 0) {
        const char* args = lpCmdLine + 15;
        while (*args == ' ') ++args;
        int seconds = 0, width = 0, height = 0; // 0 s = jusqu'a fermeture de la fenetre
        std::sscanf(args, "%d %d %d", &seconds, &width, &height);
        return ts2::tools::RunCharSelectSelfTest(seconds, width, height);
    }
    // -worldtest [seconds] [zoneId] [selfX] [selfY] [selfZ] : VOIR le perso DANS le monde 3D.
    // Charge une vraie zone (terrain+objets) + injecte un self + force InGame + capture PNG.
    if (lpCmdLine && std::strncmp(lpCmdLine, "-worldtest", 10) == 0) {
        const char* args = lpCmdLine + 10;
        while (*args == ' ') ++args;
        int seconds = 0, zoneId = 1;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        std::sscanf(args, "%d %d %f %f %f", &seconds, &zoneId, &sx, &sy, &sz);
        return ts2::tools::RunWorldSelfTest(seconds, zoneId, sx, sy, sz, 0, 0);
    }
    ts2::App app;
    return app.Run(hInstance, lpCmdLine);
}

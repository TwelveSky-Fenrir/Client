// App/GameConfig.h — configuration issue de la ligne de commande '/'-délimitée.
// WinMain 0x4609C0 : lancement d'origine « TwelveSky2.exe /0/0/2/1024/768 ».
#pragma once
#include "Core/Types.h"

namespace ts2 {

struct GameConfig {
    int  buildVariant = 0;          // champ 0 -> dword_166918C (variante de build/serveur)
    int  useTRVariant = 0;          // champ 1 -> g_UseTRVariant (dword_1669190, localisation TR)
    int  windowMode   = 2;          // champ 2 -> dword_1669180 (==2 : fenêtré bordé centré ; sinon plein écran)
    int  width  = kRefWidth;        // champ 3
    int  height = kRefHeight;       // champ 4
    bool valid  = false;            // ligne de commande bien formée ('/'-préfixée, >=3 champs)

    bool Windowed() const { return windowMode == 2; }

    // Découpe la ligne de commande '/'-délimitée. Fidèle : exige un préfixe '/'.
    static GameConfig Parse(const char* cmdLine);
};

} // namespace ts2

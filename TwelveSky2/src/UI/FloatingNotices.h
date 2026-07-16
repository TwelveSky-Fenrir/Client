// UI/FloatingNotices.h — notices flottantes du HUD en jeu (13 slots typés, 10 s).
//
// Réécriture FIDÈLE de l'objet singleton `dword_1821D58` de TwelveSky2, dont les
// deux SEULES méthodes sont :
//   HUD_ShowFloatingMessage    0x5AEEC0  arme un slot (type 0..12), horodate, éteint
//                                        les slots concurrents, joue un son ;
//   HUD_RenderFloatingMessages 0x5AF4C0  dessine les slots actifs (durée 10 s, SANS
//                                        fondu : coupure nette).
// Appelées depuis UI_RenderAllDialogs 0x5AE2D0 : HUD_RenderFloatingMessages @0x5AE5A7
// (this = dword_1821D58) JUSTE AVANT UI_SysMsgList_Render 0x5AEC80 @0x5AE5B9
// (this = dword_1822350) — c'est cet ORDRE que ChatWindow::Render reproduit.
//
// ---------------------------------------------------------------------------------
// LAYOUT PROUVÉ de dword_1821D58 (offsets relevés dans les deux fonctions) :
//   +0     int   scratchX      sortie de UI_ProjectSpriteToScreen 0x50F5D0 (@0x5AF59A)
//   +4     int   scratchY      idem — scratch de frame, NON modélisé ici (variable locale)
//   +8     int   active[13]    slot i -> +8+4*i        (@0x5AEEE0 écrit 1 / @0x5AF52B lit)
//   +60    char  text[14][101] slot i -> +60+101*i     (@0x5AEF0B / @0x5AF5E3)
//                              le 14e (+1373) = 2e ligne du type 12 (@0x5AEF1F / @0x5AFCFF)
//   +1476  float ts[13]        slot i -> +1476+4*i     (@0x5AEF3A écrit / @0x5AF552 lit)
// Cohérence arithmétique vérifiée : 60 + 101*13 = 1373 ; 1476 + 4*12 = 1524.
//
// ⚠️ PIÈGE (corrigé) : `this+1524` N'EST PAS un champ distinct — c'est ts[12]
// lui-même (1476 + 4*12). HUD_ShowFloatingMessage écrit ts[type] = g_GameTimeSec
// (@0x5AEF3A) puis, pour le type 12 SEULEMENT, y ajoute dbl_7A7368 = 20.0 (@0x5AEF60,
// octets vérifiés `00 00 00 00 00 00 34 40`). L'horodatage du type 12 est donc
// POSTDATÉ de +20 s ; comme le rendu teste `now - ts <= 10.0`, la durée de vie réelle
// du type 12 est de 30 s, pas 10 s.
//
// ---------------------------------------------------------------------------------
// PÉRIMÈTRE / DÉVIATIONS ASSUMÉES :
//   - SON : HUD_ShowFloatingMessage sélectionne un son via `subType` (sous-switch
//     @0x5AEFF5/0x5AF06D/0x5AF117/0x5AF18B/0x5AF2CB/0x5AF351 -> Snd3D_PlayScaledVolume
//     0x4DA380). NON reproduit ici (aucune des 34 adresses `flt_14xxxxx` de banque de
//     sons n'est résolue en fichier) ; `subType` est accepté et conservé dans la
//     signature pour rester fidèle au contrat d'appel. NB : dans le binaire, le
//     `default: return;` de ces sous-switch n'annule RIEN de l'état du slot (les
//     écritures active/ts/texte/extinctions le précèdent toutes) — l'omission du son
//     est donc sans effet sur le rendu.
//   - Le binaire n'a AUCUN repli graphique : si le sprite n'est pas chargé, rien n'est
//     dessiné. On reste fidèle (pas de rect coloré de substitution) : texte seul.
//
// NB INCLUSION : header VOLONTAIREMENT LÉGER (aucun <d3d9.h>/<d3dx9.h>/<winsock2.h>)
// — il est inclus par UI/ChatWindow.h, dont le bandeau garantit cette propriété. Les
// ressources GPU sont donc derrière un PIMPL opaque (`struct Gpu`), même convention
// que Scene/SceneManager.h (scènes concrètes tenues par unique_ptr pour ne pas tirer
// d3dx9 chez les incluants).
#pragma once
#include <array>
#include <memory>
#include <string>

namespace ts2::gfx { class SpriteBatch; class Font; class GpuTexture; }

namespace ts2::ui {

class FloatingNotices {
public:
    // 13 slots typés (garde `type < 0 || type > 12` @0x5AEECD/@0x5AEED3 ; boucle
    // `for i in [0,13)` @0x5AF509).
    static constexpr int kSlotCount = 13;
    // Tampon de texte par slot : 101 o NUL inclus (Crt_Memset ..., 0x65 @0x5AEEF6).
    static constexpr int kTextLen = 101;
    // Durée de vie : `g_GameTimeSec - ts <= 10.0` @0x5AF552, sinon slot = 0 @0x5AF55A.
    static constexpr float kLifetimeSec = 10.0f;
    // Postdatage du type 12 : ts[12] += 20.0 @0x5AEF60 (dbl_7A7368) -> vie utile 30 s.
    static constexpr float kType12TimeBonus = 20.0f;
    // Sous-état de scène exigé par la garde @0x5AF4DA (g_SceneSubState 0x1676184 == 4
    // = MainTick, cf. Scene/SceneManager.h).
    static constexpr int kSubStateMainTick = 4;

    FloatingNotices();
    ~FloatingNotices();
    FloatingNotices(const FloatingNotices&)            = delete;
    FloatingNotices& operator=(const FloatingNotices&) = delete;

    // HUD_ShowFloatingMessage 0x5AEEC0. `type` 0..12 (hors bornes -> ignoré
    // silencieusement, @0x5AEED5). `subType` = sélecteur de SON uniquement
    // (non reproduit, cf. bandeau) ; conservé pour fidélité du contrat d'appel.
    // `text2` = 2e ligne, utilisée par le seul type 12 (@0x5AFCFF) ; RAZ
    // INCONDITIONNELLE à chaque appel (Crt_Memset @0x5AEEF6 précède le test de type).
    void Show(int type, int subType, const std::string& text,
              const std::string& text2 = std::string());

    // HUD_RenderFloatingMessages 0x5AF4C0. `nowSec` = g_GameTimeSec (flt_815180) ;
    // `screenW/screenH` = dimensions écran (champs +20/+24 de g_PlayerCmdController
    // 0x1669170 lus par UI_ProjectSpriteToScreen 0x50F5D0).
    void Render(gfx::SpriteBatch& sprites, gfx::Font& font, float nowSec,
                int screenW, int screenH);

private:
    struct Gpu; // PIMPL opaque (gfx::GpuTexture) — cf. « NB INCLUSION » ci-dessus

    // RAZ des 13 slots (branche `else` de la garde de scène, @0x5AF4FA).
    void ClearAll();

    // UI_ProjectSpriteToScreen 0x50F5D0 : ancre le CENTRE du sprite à la même
    // fraction d'écran que sa position de conception (le sprite lui-même n'est PAS
    // mis à l'échelle).
    static void Project(int designX, int designY, int spriteW, int spriteH,
                        int screenW, int screenH, int& outX, int& outY);

    // Charge paresseusement `001_%05d.IMG` (745 / 1028) ; nullptr si indisponible
    // (-> aucun sprite dessiné, fidèle : le binaire n'a pas de repli).
    // `which` : 0 = sprite idx 744 (types 0..11), 1 = sprite idx 1027 (type 12).
    gfx::GpuTexture* EnsureTexture(gfx::SpriteBatch& sprites, int which);

    std::array<int, kSlotCount>          active_{};  // +8+4*i
    std::array<float, kSlotCount>        ts_{};      // +1476+4*i
    std::array<std::string, kSlotCount>  text_{};    // +60+101*i
    std::string                          text2_;     // +1373 (2e ligne du type 12)

    std::unique_ptr<Gpu> gpu_;
};

} // namespace ts2::ui

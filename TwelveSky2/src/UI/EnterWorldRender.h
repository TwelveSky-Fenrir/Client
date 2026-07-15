// UI/EnterWorldRender.h — RENDU VISUEL de l'écran de transition EnterWorld (ts2::ui).
//
// AUDIT 2026-07-14 (re-vérifié) : Scene::EnterWorld N'AVAIT AUCUN CASE dans
// SceneManager::Render() (Scene/SceneManager.cpp, fichier interdit en écriture pour cette
// mission) — le switch tombait dans `default` ("None : écran effacé par
// Renderer::BeginFrame"). Pendant TOUTE la transition CharSelect->InGame (200+ frames de
// chargement de zone, potentiellement jusqu'à 5000 frames d'attente ACK serveur, cf.
// Game/EnterWorldFlow.h), le client réécrit affichait donc un écran VIDE (couleur de clear
// du device, pas même noir garanti), alors que le binaire d'origine affiche un VRAI écran
// de chargement.
//
// Ce module (déjà écrit) est maintenant INSTANCIÉ et câblé via LoginScene (fichier
// autorisé, MÊME pattern que IntroRender — cf. UI/LoginScene.h::enterWorldRender_ +
// UI/LoginScene.cpp::LoginScene::RenderEnterWorld) : device_ branché dans
// LoginScene::Init(), rendu appelé par LoginScene::RenderEnterWorld(state, zoneId).
// SEUL chaînon manquant, TOUJOURS à appliquer manuellement dans le fichier interdit
// Scene/SceneManager.cpp (rapport d'audit 2026-07-14, mission EnterWorldRender, pour le
// patch exact) : ajouter un `case Scene::EnterWorld:` dans SceneManager::Render() qui
// appelle `login_->RenderEnterWorld(enterWorldState_, game::g_World.zoneId);`.
//
// Réécriture fidèle de la GÉOMÉTRIE réelle de Scene_EnterWorldRender 0x52C260 (~930 o),
// décompilée via idaTs2 (serveur HTTP JSON-RPC http://127.0.0.1:13337/mcp, méthode
// "decompile" — le MCP `idaTs2` n'était pas joignable via l'outil MCP direct au moment de
// cet audit, fallback HTTP JSON-RPC utilisé, MÊME IDB, aucune donnée inventée).
//
// === GÉOMÉTRIE RÉELLE EXTRAITE (0x52C260) ===
//
//   if (*(this + 1) == 0)   // EnterWorldState::WaitBeforeUnload (état 0, ~30 frames)
//       // RIEN dessiné (juste Gfx_Begin2D/End2D/Present) — écran nu, MÊME motif que
//       // IntroRender::Render() pour subState==0 (cf. UI/IntroRender.h).
//   else {
//       // Fond plein écran = image de chargement PROPRE À LA ZONE CIBLE, centrée sur SA
//       // taille réelle (Sprite2D_GetWidth/Height), atlas SÉPARÉ (PAS unk_8E8B50/001) :
//       //   unk_A649B8 + 148 * previousZoneId  (this+15727, = zoneId-1)
//       // Résolu par audit AssetMgr_InitAllSlots 0x4DEB50 (catégorie de construction 7,
//       // kk=0..349) + Sprite2D_BuildPath 0x4D68E0 (case 7 -> dossier "008", fichier
//       // "008_%05d.IMG", index = a3+1) : le fichier réel chargé pour ce slot
//       // previousZoneId est donc "008_%05d.IMG" avec index = previousZoneId+1 = zoneId.
//       //   -> CHEMIN RÉEL CONFIRMÉ : G03_GDATA/D01_GIMAGE2D/008/008_%05d.IMG, index=zoneId.
//       baseX = nWidth/2  - bgW/2;  baseY = nHeight/2 - bgH/2;
//       Sprite2D_Draw(bg, baseX, baseY);
//
//       // Texte : StrTable003_Get(dword_84A6A8, zoneId) + StrTable005_Get(g_LangId, 69),
//       // 2 lots UI_DrawNumberValue (police numérique bitmap dédiée, PAS la police UI
//       // normale) centrés autour de (baseX+363, baseY+475). Reproduit ici par une SEULE
//       // ligne de texte via ctx.Text (police normale, pas le rendu bitmap numérique
//       // exact) — fidélité PARTIELLE assumée (TODO : reproduire UI_DrawNumberValue/
//       // UI_MeasureNumberText 0x53FCC0/0x53FCA0 si un jour nécessaire pixel-exact).
//
//       // Barre de progression : 21 sprites (atlas unk_8E8B50/001, MÊME atlas que
//       // Intro/ServerSelect), slot = 1140 + clamp(zoneResourceIndex, 0, 20), dessinée à
//       // (baseX+123, baseY+504). zoneResourceIndex = EnterWorldFlowState::
//       // zoneResourceIndex (this+15726), avance de 0 à 20 durant LoadZoneResources
//       // (10 frames par incrément, cf. Game/EnterWorldFlow.h) : c'est une ANIMATION DE
//       // PROGRESSION réelle (21 frames distinctes), pas un simple spinner statique.
//
//       UI_RenderAllDialogs();  // notices d'erreur (Failed) rendues PAR-DESSUS, comme
//                                // partout ailleurs (cf. GameHud/GameWindows) — délégué à
//                                // ClientRuntime::PromptState côté ClientSource, PAS
//                                // dupliqué ici (mêmes hooks que SceneManager.cpp les
//                                // branche déjà pour host.ShowErrorNotice).
//   }
//
// PÉRIMÈTRE : dessin uniquement, à partir de game::EnterWorldFlowState (Game/
// EnterWorldFlow.h, déjà écrit) en LECTURE SEULE + zoneId (fourni par l'appelant, comme
// EnterWorldFlow_Update). Aucune interaction souris/clavier (fidèle : ni Update ni Render
// d'origine ne testent d'entrée pendant cette transition).
#pragma once
#include "UI/UIManager.h"        // ts2::ui::UiContext
#include "Game/EnterWorldFlow.h" // ts2::game::EnterWorldFlowState/EnterWorldState
#include "Gfx/GpuTexture.h"      // gfx::GpuTexture (fond de zone + barre, atlas réels)
#include <unordered_map>

namespace ts2::ui {

namespace enterworld_layout {

// Dimensions NOMINALES du fond de repli (Sprite2D_GetWidth/Height du vrai fond quand
// chargé ; ces nominales ne servent QUE si le fichier 008_%05d.IMG est indisponible).
constexpr int kBgW = 1024;
constexpr int kBgH = 768;

// Offsets RÉELS relevés dans Scene_EnterWorldRender (relatifs à baseX/baseY = coin
// haut-gauche du fond, cf. commentaire d'en-tête ci-dessus).
constexpr int kTextOffsetX = 363; // EA 0x52c3fd/0x52c426 (centre du bloc "numéro/label")
constexpr int kTextOffsetY = 475;
constexpr int kBarOffsetX  = 123; // EA 0x52c5c8 (Sprite2D_Draw de la barre de progression)
constexpr int kBarOffsetY  = 504;

// Barre de progression : 21 frames (atlas unk_8E8B50/001, slots 1140..1160), pilotées par
// EnterWorldFlowState::zoneResourceIndex (0..20). EA 0x52c593/0x52c59d.
constexpr int kBarFrameBase = 1140;
constexpr int kBarFrameCap  = 1160;

inline int BarFrameSlot(int zoneResourceIndex) {
    const int v = kBarFrameBase + zoneResourceIndex;
    return v > kBarFrameCap ? kBarFrameCap : v;
}

} // namespace enterworld_layout

// ---------------------------------------------------------------------------
// EnterWorldRender — dessine l'écran de transition EnterWorld à partir d'un
// game::EnterWorldFlowState en lecture seule + le zoneId cible (même paramètre que
// EnterWorldFlow_Update). Aucune interaction souris/clavier.
// ---------------------------------------------------------------------------
class EnterWorldRender {
public:
    // Doit être appelé UNE FOIS après création du device D3D9 (même pattern que
    // IntroRender::SetDevice / ServerSelectRender::SetDevice) : sans device, GetBackground/
    // GetBarFrame ne peuvent charger aucune texture réelle et Render() repliera
    // systématiquement sur les aplats colorés.
    void SetDevice(IDirect3DDevice9* device) { device_ = device; }

    // Appelée deux fois par frame par le pilote de scène (une fois par UiPhase, comme
    // IntroRender::Render / ServerSelectRender::Render), ctx.FillRect/Text filtrant déjà
    // en interne sur ctx.phase. `zoneId` = même valeur que celle passée à
    // EnterWorldFlow_Update (dword_1675A9C d'origine, PAS relue depuis un global ici).
    void Render(const UiContext& ctx, const game::EnterWorldFlowState& state, int zoneId);

private:
    // Fond de zone (dossier "008", cf. commentaire d'en-tête) : cache paresseux par
    // zoneId (pas par previousZoneId — le calcul de l'index fichier ET l'index de slot
    // s'annulent, cf. audit ci-dessus : slot=zoneId-1, fichier=slot+1=zoneId).
    gfx::GpuTexture* GetBackground(int zoneId);

    // Barre de progression (atlas unk_8E8B50/"001", MÊME dossier que IntroRender/
    // ServerSelectRender, décalage +1 slot->fichier IDENTIQUE — cf. leurs commentaires).
    gfx::GpuTexture* GetBarFrame(int slotIndex);

    IDirect3DDevice9* device_ = nullptr;
    std::unordered_map<int, gfx::GpuTexture> bgCache_;  // zoneId -> texture fond (dossier 008)
    std::unordered_map<int, gfx::GpuTexture> barCache_; // slot -> texture barre (dossier 001)
};

} // namespace ts2::ui

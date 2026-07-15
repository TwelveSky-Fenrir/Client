// UI/IntroRender.h — RENDU VISUEL de l'écran Intro/logos (ts2::ui).
//
// Réécriture fidèle de la GÉOMÉTRIE réelle de Scene_IntroRender 0x518880 (~560 o),
// décompilée via idaTs2 (serveur HTTP JSON-RPC http://127.0.0.1:13337/mcp, méthode
// "decompile" — le MCP `idaTs2` n'étant pas exposé en outil différé dans cette session ;
// MÊME IDB, aucune donnée inventée).
//
// === DÉCOUVERTE CLÉ (corrige une spéculation de Game/IntroFlow.h) ===
// Scene_IntroRender NE LIT JAMAIS le tampon logoFade (this[3..152], 150 dwords) — il
// n'apparaît dans AUCUNE instruction du corps décompilé. La spéculation de
// IntroFlow.h ("pilote vraisemblablement un fondu/enchaînement de logos") est donc
// FAUSSE au sens fondu-alpha : il n'y a NI fondu NI lecture de logoFade dans le rendu.
// Le mécanisme réel est un DÉFILEMENT DISCRET DE SPRITES (un sprite plein-opacité par
// micro-état, PAS d'alpha-blend/cross-fade) :
//   if (this[1] == 0)                    -> RIEN dessiné (écran nu, sous-état Init/attente).
//   else {
//       v8 = this[1] + 797;              // EA 0x518A0B
//       if (v8 > 830) v8 = 830;          // EA 0x518A15-0x518A17 (plafond)
//       centré (nWidth/2 - w/2, nHeight/2 - h/2) sur unk_8E8B50 + 148*v8 (EA 0x518A8F)
//   }
// this[1] = subState (game::IntroState::subState, 0..34). Pour subState=1..33
// (kIntroLogoStepCount micro-états, IntroFlow.h), v8 parcourt EXACTEMENT 798..830 (33
// valeurs distinctes — confirme numériquement kIntroLogoStepCount=33, chaque micro-état
// de 3 frames = 0,1 s affiche un sprite DIFFÉRENT de l'atlas, sans transition visuelle
// entre eux). Pour subState=34 (maintien final, kIntroFinalHoldFrames=90 frames), v8=831
// plafonné à 830 -> le DERNIER logo (830, identique à subState=33) reste affiché tout le
// maintien final. subState=0 (attente initiale, kIntroWaitFrames=90 frames) -> écran
// NOIR, RIEN de dessiné par cette fonction : le fond noir vient du clear device
// (Gfx_BeginFrame -> g_GfxRenderer+1308 = 0, cf. IntroRender.cpp) et de l'INTRO.AVI qui
// précède, hors périmètre de Scene_IntroRender lui-même.
//
// PÉRIMÈTRE : dessin uniquement, à partir de game::IntroState (Game/IntroFlow.h, déjà
// écrit) en LECTURE SEULE.
//
// === CÂBLAGE RÉEL (mission "LOGO INTRO REEL", 2026-07-14, Docs/TS2_INTRO_LOGO_ASSETS.md) ===
// Le sprite atlas .IMG RÉEL est chargé : `path = "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG"
// % (slotIndex+1)` via asset::ImgFile::Load + gfx::GpuTexture::CreateFromImgFile (même
// pattern que UI/PanelSkin.h / UI/InventoryWindow.cpp), mis en cache paresseux par
// slotIndex dans `logoCache_` (au plus 33 entrées, une par sous-état 1..33 — subState 34
// réutilise le slot 830 déjà en cache). Dessiné via SpriteBatch::DrawSpriteScaled, centré
// sur SA taille réelle (668×229 sources, uploadées arrondies à la puissance de 2 par
// GpuTexture::CreateFromImgFile — fidèle à cTexture_LoadFromImgFile 0x457A20), EXACTEMENT
// comme Sprite2D_Draw d'origine sur `unk_8E8B50 + 148*v8`. ZÉRO repli : si le fichier est
// réellement absent/illisible au runtime, RIEN n'est dessiné (fidèle à Sprite2D_Draw qui
// échoue en silence) — plus aucun aplat coloré ni libellé de diagnostic. Vérifié par
// self-test GPU (Asset/AssetSelfTest.cpp) contre les 33 vrais fichiers
// 001_00799..001_00831.IMG : tous chargés.
#pragma once
#include "UI/UIManager.h"    // ts2::ui::UiContext
#include "Game/IntroFlow.h"  // ts2::game::IntroState
#include "Gfx/GpuTexture.h"  // gfx::GpuTexture (logo réel, atlas unk_8E8B50)
#include <unordered_map>

namespace ts2::ui {

namespace intro_layout {

// (kLogoW/kLogoH supprimés le 2026-07-15 : constantes mortes de l'ancien repli aplat, jamais
//  référencées. Le sprite réel est chargé depuis 001_%05d.IMG (668×229, DXT1) et centré sur
//  SA taille réelle — cf. IntroRender.cpp.)

// v8 = this[1] + 797, plafonné à 830 (EA 0x518A0B/0x518A15/0x518A17). Fidèle même pour
// subState==0 (bien que Render ne l'appelle jamais dans ce cas — cf. Render ci-dessous).
constexpr int kLogoSpriteBase = 798; // subState == 1 -> premier sprite de la séquence
constexpr int kLogoSpriteCap  = 830; // plafond (subState >= 33)

inline int LogoSpriteIndex(int subState) {
    const int v = subState + 797;
    return v > kLogoSpriteCap ? kLogoSpriteCap : v;
}

} // namespace intro_layout

// ---------------------------------------------------------------------------
// IntroRender — dessine l'écran Intro à partir d'un game::IntroState en lecture
// seule. Aucune interaction souris/clavier (fidèle : Scene_IntroRender/Update ne
// testent aucune entrée) -> pas de latch visuel, seulement un cache de textures
// paresseux (33 logos maximum, negligeable en mémoire).
// ---------------------------------------------------------------------------
class IntroRender {
public:
    // Appelée deux fois par frame par le pilote de scène (une fois par UiPhase, comme
    // Dialog::Render / MsgBoxDialog::Render), ctx.FillRect/Text filtrant déjà en interne
    // sur ctx.phase.
    void Render(const UiContext& ctx, const game::IntroState& state);

    // BUG DE FIDÉLITÉ CORRIGÉ (vérification runtime 2026-07-14, capture d'écran) :
    // `ctx.renderer` (UI/UIManager.h) n'est JAMAIS peuplé par LoginScene::RenderIntro()
    // (seul appelant) — GetLogoSprite() retombait donc TOUJOURS sur le repli aplat+libellé
    // ("Logo #NNN"), le vrai logo n'était JAMAIS affiché malgré un chargement réel validé
    // en isolation (Asset/AssetSelfTest.cpp, qui LUI peuple ctx.renderer). Même pattern que
    // le correctif déjà appliqué à ServerSelectRender::SetDevice() : device stocké en
    // interne, indépendant de ctx.renderer. L'appelant DOIT appeler SetDevice() une fois le
    // device créé (LoginScene::Init(), même endroit que serverSelectRender_.SetDevice()).
    void SetDevice(IDirect3DDevice9* device) { device_ = device; }

private:
    // Résout un slot de l'atlas unk_8E8B50 (AssetMgr_InitAllSlots 0x4deb50, catégorie 1 ->
    // "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG") vers sa texture GPU, cache paresseux.
    // DÉCALAGE +1 CONFIRMÉ par décompilation directe (Sprite2D_BuildPath 0x4d68e0 formate
    // le fichier avec `slot+1`) ET par le contenu réel : la séquence de logos (slots
    // 798..830) correspond EXACTEMENT aux 33 fichiers 001_00799..001_00831.IMG (668x229
    // DXT1 uniformes), pas 001_00798..001_00830.IMG.
    gfx::GpuTexture* GetLogoSprite(const UiContext& ctx, int slotIndex);

    IDirect3DDevice9* device_ = nullptr; // cf. SetDevice() — requis pour charger les .IMG réels
    std::unordered_map<int, gfx::GpuTexture> logoCache_; // slot -> texture (lazy, <=33 entrées)
};

} // namespace ts2::ui

// UI/UIManager.h — framework UI / gestionnaire de dialogues du client TwelveSky2.
//
// Réécriture FIDÈLE du framework UI relevé dans le désassemblage (idaTs2), voir
// Docs/TS2_CLIENT_SHELL.md §2.2. Le framework d'origine N'EST PAS un système objet
// polymorphe : c'est un registre statique d'environ 38 dialogues singletons dont les
// « méthodes » sont des fonctions libres __thiscall câblées à la main dans des chaînes
// de dispatch parallèles (une par phase du cycle de vie / type d'événement). On en donne
// ici une transposition C++ propre et PRAGMATIQUE (une classe de base `Dialog`, un
// registre `UIManager`) qui préserve les DEUX invariants clés :
//
//   1. « Premier consommateur gagne » (UI_RouteLButtonDown 0x5AC740,
//      UI_RouteLButtonUp 0x5AD0F0, UI_RouteKeyInput 0x5ADF50) : les événements sont
//      poussés à chaque dialogue en séquence ; dès qu'un handler renvoie 1 la chaîne
//      s'arrête et le monde 3D ne reçoit pas l'événement.
//   2. Rendu en ORDRE INVERSE du routage (UI_RenderAllDialogs 0x5AE2D0) : fond d'abord,
//      popups modaux en dernier (= dessinés au-dessus). Il n'existe AUCUN tick de
//      logique UI séparé : la logique vit dans _Render, les handlers réseau Pkt_* et
//      le routage clavier.
//
// S'appuie sur les briques Gfx : ts2::gfx::Renderer (device), ts2::gfx::SpriteBatch
// (lot 2D ID3DXSprite) et ts2::gfx::Font (texte ID3DXFont). Aucune de ces briques
// n'est redéfinie ici.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Core/Types.h"

namespace ts2::ui {

// Couleur ARGB compacte (helper local ; D3DCOLOR = 0xAARRGGBB).
inline constexpr D3DCOLOR Argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<D3DCOLOR>(a) << 24) | (static_cast<D3DCOLOR>(r) << 16)
         | (static_cast<D3DCOLOR>(g) << 8)  |  static_cast<D3DCOLOR>(b);
}

// Phase de rendu : le pass UI est découpé en deux sous-passes indépendantes car le
// lot sprite (panneaux) et le lot police (texte) sont deux ID3DXSprite distincts.
// FillRect/DrawFrame ne dessinent qu'en phase Panels ; Text qu'en phase Text. Chaque
// Dialog::Render est ainsi appelé deux fois par frame, mais l'écrit naturellement
// (panneaux + texte dans un seul corps de méthode).
//
// TODO [ancres 0x69E620 / 0x69E650 / 0x6A3080 / 0x69E750] — DIVERGENCE PROUVÉE (gap GX2D-01),
// correctif HORS DU PÉRIMÈTRE de ce front (il vit dans Gfx/Font.{h,cpp} + Gfx/SpriteBatch.{h,cpp}).
// Le binaire n'a QU'UN SEUL ID3DXSprite pour toute la passe 2D, et blits et texte y sont
// ENTRELACÉS dans un batch UNIQUE :
//   - Gfx_Begin2D 0x69E620 : SetRenderState(ZENABLE=7, FALSE) @0x69E630 puis
//     Sprite(+608)->Begin(D3DXSPRITE_ALPHABLEND=16) @0x69E644  — UN SEUL Begin par frame.
//   - Gfx_End2D   0x69E650 : Sprite(+608)->End() @0x69E65C puis SetRenderState(7, TRUE).
//   - UI_DrawSprite 0x6A3080 (blit) : Draw via `dword_800078` @0x6A30FC = g_GfxRenderer+608.
//   - UI_DrawText   0x69E750 (texte) : Font(+612)->DrawTextA(Sprite(+608), ...) @0x69E800.
//     => les deux consommateurs tapent le MÊME ID3DXSprite (0x800078).
//   - Entrelacement prouvé dans Scene_LoginRender 0x51B020, entre Gfx_Begin2D @0x51B189 et
//     Gfx_End2D @0x51B5A7 : blit @0x51B207, blit @0x51B28F, TEXTE @0x51B316, blit (caret)
//     @0x51B34F, TEXTE @0x51B40C, blits @0x51B445/0x51B4C7/0x51B54A.
// ZENABLE=FALSE + aucun tri => l'ORDRE DE SOUMISSION est l'ordre d'occlusion. Ici, les deux
// sous-passes ci-dessous (panneaux PUIS texte, sur deux ID3DXSprite distincts —
// SpriteBatch::sprite_ et Font::sprite_) font passer TOUT le texte au-dessus de TOUS les
// panneaux : l'ordre diverge dès que deux dialogues qui se recouvrent sont enregistrés.
// LATENT à ce jour : UIManager n'enregistre qu'un seul dialogue (msgBox_, cf. Init), et à
// un dialogue les deux ordres coïncident. Correctif : partager un unique ID3DXSprite entre
// Font et SpriteBatch (p. ex. Font::SetExternalSprite), puis effondrer UIManager::Render()
// en une passe et supprimer UiPhase. Impacte aussi ChatWindow/BuffStatusPanel/GameHud/
// GameWindows/WorldRenderer, qui utilisent le même idiome deux-passes.
enum class UiPhase { Panels, Text };

// ---------------------------------------------------------------------------
// UiContext — ressources graphiques partagées passées à chaque Dialog::Render.
// Fournit les primitives 2D minimales (rectangle plein, cadre, texte) au-dessus de
// SpriteBatch/Font. Le rectangle plein est un blit d'une texture blanche 1x1 mise à
// l'échelle et modulée par la couleur (seule vraie modulation du moteur, cf.
// SpriteBatch::DrawSpriteScaled), donc sans dépendre d'un asset .IMG.
struct UiContext {
    gfx::Renderer*     renderer = nullptr; // device D3D9 (ts2::gfx::Renderer)
    gfx::SpriteBatch*  sprites  = nullptr; // lot 2D partagé (panneaux)
    gfx::Font*         font     = nullptr; // police UI (texte)
    IDirect3DTexture9* whiteTex = nullptr; // texture blanche 1x1 (créée par UIManager)
    int                screenW  = ts2::kRefWidth;  // nWidth  (0x1669184)
    int                screenH  = ts2::kRefHeight; // nHeight (0x1669188)
    float              gameTimeSec = 0.0f;         // g_GameTimeSec (auto-close, animations)
    UiPhase            phase    = UiPhase::Panels;  // sous-passe courante

    // Rectangle plein coloré (phase Panels uniquement).
    void FillRect(int x, int y, int w, int h, D3DCOLOR color) const;
    // Cadre 1 px (phase Panels uniquement).
    void DrawFrame(int x, int y, int w, int h, D3DCOLOR color, int thickness = 1) const;
    // Texte mode normal via la police UI (phase Text uniquement).
    void Text(const char* s, int x, int y, D3DCOLOR color) const;
    // Largeur pixel d'un texte (pour centrage) ; 0 si police indisponible.
    int  MeasureText(const char* s) const;
};

// ---------------------------------------------------------------------------
// Dialog — classe de base d'un dialogue UI. Transpose la struct UIDialogBase du
// désassemblage : +0 x, +4 y, +8 bOpen, +0xC btnPressed[N]. Les handlers renvoient
// true si l'événement est CONSOMMÉ (règle « premier consommateur gagne »).
class Dialog {
public:
    virtual ~Dialog() = default;

    // Ouvre / ferme le dialogue (bascule le flag « visible »/bOpen).
    virtual void Open();   // pattern *_Open  : bOpen=1, reset des latches de boutons
    virtual void Close();  // bOpen=0

    // Clic-gauche enfoncé  (chaîne UI_RouteLButtonDown 0x5AC740).
    virtual bool OnMouseDown(int x, int y) { (void)x; (void)y; return false; }
    // Clic-gauche relâché = « clic » validé (chaîne UI_RouteLButtonUp 0x5AD0F0).
    virtual bool OnClick(int x, int y)     { (void)x; (void)y; return false; }
    // Saisie clavier / touche (chaîne UI_RouteKeyInput 0x5ADF50). `vk` = virtual-key.
    virtual bool OnKey(int vk)             { (void)vk; return false; }

    // --- Clic DROIT (gap UIFW-01) ------------------------------------------
    // Chaîne UI_RouteRButtonDown 0x5AD5D0 (1er slot appelé @0x5AD5E4) / UI_RouteRButtonUp
    // 0x5ADA90 (1er slot @0x5ADAA4) : 38 slots « premier consommateur gagne », strictement
    // le même motif `test eax,eax / jz / mov eax,1 / jmp` que les chaînes clic-gauche.
    // ⚠ Dans l'IDB, les slots de la chaîne 0x5AD5D0 portent l'étiquette HÉRITÉE et
    // TROMPEUSE `UI_Dlg_OnLButtonDblClk_*` : leur rôle réel est OnRButtonDown (cf.
    // commentaire de tête IDA de 0x5AD5D0). Handlers réels (non-stubs) : cGameHud_OnRButtonDown
    // 0x6318E0 (@0x5AD7E4), cQuickSlotWin_OnRButtonDown 0x6608D0 (@0x5AD804),
    // UI_NpcWin_OnRDown_Dispatch 0x5DDC50 (@0x5AD7A4), UI_OptionsWnd_OnRButtonDown 0x66A170,
    // UI_CharListWnd_OnRButtonDown 0x66E840, UI_RankWnd_OnRButtonDown 0x6747E0, etc.
    virtual bool OnRButtonDown(int x, int y) { (void)x; (void)y; return false; }
    virtual bool OnRButtonUp(int x, int y)   { (void)x; (void)y; return false; }

    // Rendu per-frame (appelé en ordre INVERSE du routage). Reçoit la position curseur
    // client pour le survol, comme UI_RenderAllDialogs qui pousse (x,y) à chaque draw.
    virtual void Render(const UiContext& ctx, int cursorX, int cursorY) {
        (void)ctx; (void)cursorX; (void)cursorY;
    }

    // --- Passe de SURVOL / infobulle (gap UIFW-03) -------------------------
    // Miroir de UI_RouteRButtonExamine 0x5AE5E0. ⚠ Malgré son nom IDA, cette fonction
    // n'a RIEN à voir avec le bouton droit : `xrefs_to 0x5AE5E0` renvoie EXACTEMENT 1
    // xref, @0x5AE5C9 — la DERNIÈRE instruction utile de UI_RenderAllDialogs 0x5AE2D0
    // (suivie de `mov esp,ebp / pop ebp / retn` @0x5AE5CE). C'est donc une passe de
    // SURVOL par frame, exécutée APRÈS les ~39 draws, avec le MÊME (x,y) curseur client
    // que les draws (var_C/var_10, issus de GetPhysicalCursorPos @0x5AE2DD).
    // Deux propriétés structurelles à préserver :
    //   (1) chaîne « premier consommateur gagne » -> UN SEUL tooltip par frame ;
    //   (2) exécutée après tous les Render -> le tooltip est TOUJOURS au-dessus.
    // Consommateurs réels : UI_Shop_ShowItemTooltip 0x5C9360 (@0x5AE6B1),
    // UI_Warehouse_ShowItemTooltip 0x5CB4A0 (@0x5AE702), UI_ItemListWin_OnMove 0x5D2510
    // (@0x5AE71D), UI_StorageWin_OnMove 0x5D7D20 (@0x5AE738), UI_NpcWin_OnMove_Dispatch
    // 0x5DE8C0 (@0x5AE76E), cGameHud_DrawTooltipDispatch 0x64EA30 (@0x5AE7A4),
    // cQuickSlotWin_DrawTooltip 0x6620E0 (@0x5AE7BF), UI_QuickBar_Handle 0x6869E0
    // (@0x5AE8CD), UI_ConsumableBar_OnRightClick 0x68E940 (@0x5AE8E8).
    // Renvoie true si CE dialogue a dessiné son infobulle (= consomme la passe).
    virtual bool OnHover(const UiContext& ctx, int cursorX, int cursorY) {
        (void)ctx; (void)cursorX; (void)cursorY; return false;
    }

    // Opt-out de UIManager::CloseAll (défaut public/documenté du binaire : fermé).
    // UI_CloseAllDialogs 0x5AC590 n'agit PAS sur toute l'UI : sa liste est FIGÉE (~27
    // cibles) et laisse VOLONTAIREMENT ouverts MsgBox (dword_1822438), NoticeDlg,
    // TextInput, ItemListWin, StorageWin, ClanWin, NpcWin, AutoPlay — alors que
    // UI_ResetAllDialogs 0x5AC3F0 (~42 cibles) les réinitialise, LUI (MsgBox : appel
    // UI_Dlg_OnReset_ClearFlag8_5C08A0(dword_1822438) @0x5AC430, sans contrepartie dans
    // 0x5AC590). Un dialogue absent de la liste 0x5AC590 doit renvoyer false ici.
    virtual bool ClosedByCloseAll() const { return true; }

    bool IsOpen() const { return bOpen_; }  // *(this+8)
    int  X() const { return x_; }           // *(this+0)
    int  Y() const { return y_; }           // *(this+4)

protected:
    static bool PointInRect(int px, int py, int rx, int ry, int rw, int rh) {
        return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
    }

    int  x_     = 0;      // +0x00 position écran (recentrée chaque frame)
    int  y_     = 0;      // +0x04
    bool bOpen_ = false;  // +0x08 drapeau visible
};

// ---------------------------------------------------------------------------
// MsgBoxDialog — dialogue modal OK/Annuler partagé (unk_1822438 dans l'original).
// Transposition fidèle du cycle MsgBox de §2.2 :
//   UI_MsgBox_Open        0x5C08C0  (bOpen=1, openTime, reset latches, contexte)
//   UI_MsgBox_OnLButtonDown 0x5C0980 (hit-test OK/Annuler, latch, modal -> return 1)
//   UI_MsgBox_OnLButtonUp 0x5C0A90  (valide selon le bouton pressé puis ferme)
//   UI_MsgBox_Render      0x5C3100  (centre écran, cadre, texte, boutons + auto-close)
// Simplifications pragmatiques : le texte est fourni en clair (au lieu de switch sur
// contextType -> StrTable005) et le fond est un panneau plein (au lieu du sprite .IMG).
class MsgBoxDialog : public Dialog {
public:
    static constexpr int kBtnOk     = 0;
    static constexpr int kBtnCancel = 1;

    // Callback de résultat : reçoit kBtnOk / kBtnCancel. Optionnel.
    using ResultFn = std::function<void(int button)>;

    // Ouvre la boîte avec un titre + corps ; `onResult` déclenché à la validation.
    // `withCancel=false` -> boîte 1 bouton (comportement NoticeDlg simplifié).
    void Open(const std::string& title, const std::string& body,
              ResultFn onResult = {}, bool withCancel = true);
    void Open() override { Dialog::Open(); } // ouverture nue (état par défaut)

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // UI_CloseAllDialogs 0x5AC590 ne touche PAS dword_1822438 (le MsgBox partagé) : la
    // liste @0x5AC5A2-0x5AC6D1 ne le cite jamais. Seul UI_ResetAllDialogs 0x5AC3F0 le
    // remet à plat (@0x5AC430). Ouvrir une fenêtre ne doit donc PAS avaler la boîte
    // modale en cours — d'où l'opt-out.
    bool ClosedByCloseAll() const override { return false; }

private:
    struct Rect { int x, y, w, h; };
    // Géométrie recalculée chaque frame à partir des dimensions écran (centrage).
    void Layout(int screenW, int screenH, Rect& box, Rect& ok, Rect& cancel) const;
    void Finish(int button); // invoque le callback + ferme

    std::string title_;
    std::string body_;
    ResultFn    onResult_;
    bool        withCancel_ = true;
    bool        btnPressed_[2] = {false, false}; // +0x0C latches (armés au down)
    float       openTime_ = 0.0f;                // +0x14 openTime (auto-close type 4)
    // Dims écran mémorisées au dernier Render : le hit-test (OnMouseDown/OnClick, routés
    // entre deux frames) doit s'aligner sur la géométrie effectivement dessinée, quelle
    // que soit la résolution. Fidèle à l'UI d'origine qui recentre chaque frame.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

// ---------------------------------------------------------------------------
// UIManager — registre de dialogues + routeurs d'événements + pass de rendu.
// Équivaut au bloc de fonctions libres UI_* + registre statique du binaire. Singleton
// (comme g_GxdRenderer::Instance()), car l'original manipule des singletons globaux.
class UIManager {
public:
    static UIManager& Instance();

    // Init : mémorise les briques Gfx, crée la texture blanche 1x1, enregistre les
    // dialogues intégrés (MsgBox). `hwnd` sert à convertir la position curseur écran
    // -> client (comme UI_RenderAllDialogs : GetPhysicalCursorPos + ScreenToClient).
    bool Init(gfx::Renderer* renderer, gfx::SpriteBatch* sprites, gfx::Font* font,
              HWND hwnd, int screenW, int screenH);
    void Shutdown(); // UI_DestroyAllDialogs 0x5AC270 (teardown, App_Shutdown)

    // Enregistre un dialogue (NON possédé). L'ordre = priorité de ROUTAGE : index 0 =
    // premier consommateur / popup le plus « au-dessus » ; le rendu itère à l'envers.
    void Register(Dialog* dlg);

    // --- Routeurs d'événements (première consommation gagne) ---
    bool RouteMouseDown(int x, int y); // UI_RouteLButtonDown 0x5AC740
    bool RouteMouseUp(int x, int y);   // UI_RouteLButtonUp   0x5AD0F0
    bool RouteKey(int vk);             // UI_RouteKeyInput    0x5ADF50
    // Clic DROIT (gap UIFW-01). Noms attendus TELS QUELS par le front App, qui a déjà
    // assigné ses hooks et les documente : App/App.cpp:633 et :651 citent
    // « ts2::ui::UIManager::RouteRButtonDown/Up ». NE PAS renommer sans re-câbler App.
    bool RouteRButtonDown(int x, int y); // UI_RouteRButtonDown 0x5AD5D0
    bool RouteRButtonUp(int x, int y);   // UI_RouteRButtonUp   0x5ADA90

    // Pass de rendu per-frame (ordre inverse du routage). À appeler depuis
    // Scene_*Render, une seule fois par frame. UI_RenderAllDialogs 0x5AE2D0.
    void Render();

    // --- Cycle de vie global ---
    // ⚠ 0x5AC3F0 et 0x5AC590 sont DEUX fonctions DISTINCTES, aux listes différentes —
    // ne pas les confondre (elles l'étaient ici jusqu'à la vague W9, cf. gap N-1).
    void ResetAll(); // UI_ResetAllDialogs 0x5AC3F0 (transitions de scène) : ~42 cibles,
                     // TOUT est remis à plat, MsgBox compris (@0x5AC430), focus EDIT
                     // relâché INCONDITIONNELLEMENT (UI_FocusEditBox(mgr,0) @0x5AC3FE).
    void CloseAll(); // UI_CloseAllDialogs 0x5AC590 (ouvrir une fenêtre ferme les autres) :
                     // liste FIGÉE de ~27 cibles, qui épargne MsgBox & co. Les dialogues
                     // hors liste s'excluent via Dialog::ClosedByCloseAll() -> false.

    // Accès au dialogue MsgBox intégré (partagé, comme unk_1822438).
    MsgBoxDialog& MsgBox() { return msgBox_; }

    // Dimensions écran (mises à jour sur redimensionnement).
    void SetScreenSize(int w, int h) { ctx_.screenW = w; ctx_.screenH = h; }

private:
    UIManager() = default;
    ~UIManager() { Shutdown(); }
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool CreateWhiteTexture(IDirect3DDevice9* dev);
    // Passe de survol « premier consommateur gagne » (UI_RouteRButtonExamine 0x5AE5E0,
    // appelée @0x5AE5C9 en fin de UI_RenderAllDialogs). Exécutée à la fin de CHAQUE
    // sous-passe de Render() — voir la justification détaillée sur la définition.
    void RunHoverChain(int cx, int cy);

    UiContext             ctx_;
    HWND                  hwnd_ = nullptr;
    std::vector<Dialog*>  dialogs_;   // registre en ordre de routage (0 = top)
    MsgBoxDialog          msgBox_;    // dialogue intégré (possédé)
    bool                  inited_ = false;
};

} // namespace ts2::ui

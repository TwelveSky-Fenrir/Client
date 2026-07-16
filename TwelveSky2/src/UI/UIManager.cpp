// UI/UIManager.cpp — implémentation du framework UI / gestionnaire de dialogues.
// Voir UIManager.h et Docs/TS2_CLIENT_SHELL.md §2.2.
#include "UI/UIManager.h"
#include "Core/Log.h"
#include <utility> // std::move

namespace ts2::ui {

// ===========================================================================
// UiContext — primitives 2D
// ===========================================================================
namespace {
// Rectangle source unité pour la texture blanche 1x1 (mis à l'échelle par FillRect).
const RECT kUnitSrc = {0, 0, 1, 1};
} // namespace

void UiContext::FillRect(int x, int y, int w, int h, D3DCOLOR color) const {
    if (phase != UiPhase::Panels) return;          // dessin sprite hors phase panneaux
    if (!sprites || !sprites->Ready() || !whiteTex) return;
    if (w <= 0 || h <= 0) return;
    // Blit d'une texture blanche 1x1 mise à l'échelle (w,h) et modulée par `color`.
    // compensatePos=true : la position finale à l'écran reste (x,y) malgré l'échelle
    // (cf. SpriteBatch::DrawSpriteScaled / UI_DrawSpriteScaledAlpha 0x457D70).
    sprites->DrawSpriteScaled(whiteTex, &kUnitSrc, x, y,
                              static_cast<float>(w), static_cast<float>(h),
                              color, /*compensatePos=*/true);
}

void UiContext::DrawFrame(int x, int y, int w, int h, D3DCOLOR color, int t) const {
    if (phase != UiPhase::Panels) return;
    if (w <= 0 || h <= 0 || t <= 0) return;
    FillRect(x,         y,         w, t,         color); // haut
    FillRect(x,         y + h - t, w, t,         color); // bas
    FillRect(x,         y,         t, h,         color); // gauche
    FillRect(x + w - t, y,         t, h,         color); // droite
}

void UiContext::Text(const char* s, int x, int y, D3DCOLOR color) const {
    if (phase != UiPhase::Text) return;            // dessin texte hors phase texte
    if (!s || !font || !font->Ready()) return;
    // Style ombré (kStyleShadow) : lisibilité par-dessus les panneaux, comme l'UI
    // d'origine qui pousse un liseré/ombre noir (Font_DrawTextStyled 0x405DC0).
    font->DrawTextStyled(s, x, y, color, gfx::kStyleShadow);
}

int UiContext::MeasureText(const char* s) const {
    if (!s || !font || !font->Ready()) return 0;
    return font->MeasureText(s);
}

// ===========================================================================
// Dialog — base
// ===========================================================================
void Dialog::Open()  { bOpen_ = true; }
void Dialog::Close() { bOpen_ = false; }

// ===========================================================================
// MsgBoxDialog — modal OK/Annuler (unk_1822438)
// ===========================================================================
namespace {
// Dimensions du cadre MsgBox (approximation du sprite de fond d'origine).
constexpr int kBoxW = 320;
constexpr int kBoxH = 160;
constexpr int kBtnW = 96;
constexpr int kBtnH = 28;

// Palette (couleurs plates, à défaut du sprite .IMG).
const D3DCOLOR kColBg       = Argb(230,  24,  28,  40);
const D3DCOLOR kColBorder   = Argb(255, 180, 150,  90);
const D3DCOLOR kColBtn      = Argb(255,  56,  64,  88);
const D3DCOLOR kColBtnHover = Argb(255,  84,  96, 128);
const D3DCOLOR kColBtnDown  = Argb(255, 150, 120,  70);
const D3DCOLOR kColText     = Argb(255, 240, 240, 240);
const D3DCOLOR kColTitle    = Argb(255, 255, 214, 140);
} // namespace

void MsgBoxDialog::Layout(int screenW, int screenH, Rect& box, Rect& ok, Rect& cancel) const {
    // Centrage écran (comme UI_MsgBox_Render : centre (nWidth/2, nHeight/2)).
    box.x = screenW / 2 - kBoxW / 2;
    box.y = screenH / 2 - kBoxH / 2;
    box.w = kBoxW;
    box.h = kBoxH;
    const int by = box.y + kBoxH - kBtnH - 16;
    if (withCancel_) {
        // Deux boutons : OK à gauche, Annuler à droite.
        ok     = { box.x + 40,                 by, kBtnW, kBtnH };
        cancel = { box.x + kBoxW - 40 - kBtnW, by, kBtnW, kBtnH };
    } else {
        // Un seul bouton centré.
        ok     = { box.x + kBoxW / 2 - kBtnW / 2, by, kBtnW, kBtnH };
        cancel = { 0, 0, 0, 0 };
    }
}

void MsgBoxDialog::Open(const std::string& title, const std::string& body,
                        ResultFn onResult, bool withCancel) {
    // UI_MsgBox_Open 0x5C08C0 : coupe la saisie, mémorise openTime, arme bOpen,
    // réinitialise les latches de boutons, fixe titre/corps.
    title_        = title;
    body_         = body;
    onResult_     = std::move(onResult);
    withCancel_   = withCancel;
    btnPressed_[0] = btnPressed_[1] = false;
    openTime_     = gfx::g_GameTimeSec;
    bOpen_        = true;
    TS2_LOG("MsgBox ouverte : \"%s\"", title_.c_str());
}

void MsgBoxDialog::Finish(int button) {
    bOpen_ = false;                    // ferme (bOpen=0)
    ResultFn fn = std::move(onResult_);
    onResult_ = {};
    btnPressed_[0] = btnPressed_[1] = false;
    if (fn) fn(button);
}

bool MsgBoxDialog::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    // UI_MsgBox_OnLButtonDown 0x5C0980 : hit-test des boutons -> latch btnPressed[i].
    // Utilise les dims écran mémorisées au dernier Render (géométrie effectivement dessinée).
    Rect box, ok, cancel;
    Layout(lastScreenW_, lastScreenH_, box, ok, cancel);
    if (PointInRect(x, y, ok.x, ok.y, ok.w, ok.h)) {
        btnPressed_[kBtnOk] = true;
    } else if (withCancel_ && PointInRect(x, y, cancel.x, cancel.y, cancel.w, cancel.h)) {
        btnPressed_[kBtnCancel] = true;
    }
    return true; // modal de fait : consomme tout clic tant que la boîte est ouverte
}

bool MsgBoxDialog::OnClick(int x, int y) {
    if (!bOpen_) return false;
    // UI_MsgBox_OnLButtonUp 0x5C0A90 : valide si relâché sur le bouton armé, sinon annule.
    Rect box, ok, cancel;
    Layout(lastScreenW_, lastScreenH_, box, ok, cancel);
    if (btnPressed_[kBtnOk] && PointInRect(x, y, ok.x, ok.y, ok.w, ok.h)) {
        Finish(kBtnOk);
    } else if (withCancel_ && btnPressed_[kBtnCancel] &&
               PointInRect(x, y, cancel.x, cancel.y, cancel.w, cancel.h)) {
        Finish(kBtnCancel);
    } else {
        btnPressed_[0] = btnPressed_[1] = false; // relâché hors bouton : désarme
    }
    return true; // modal
}

bool MsgBoxDialog::OnKey(int vk) {
    if (!bOpen_) return false;
    // Raccourcis modaux : Entrée -> OK, Échap -> Annuler (ferme la boîte).
    if (vk == VK_RETURN) { Finish(kBtnOk); return true; }
    if (vk == VK_ESCAPE) { Finish(withCancel_ ? kBtnCancel : kBtnOk); return true; }
    return true; // modal : avale toutes les touches tant qu'ouverte
}

void MsgBoxDialog::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Mémorise les dims écran courantes pour que le hit-test (routé entre deux frames)
    // s'aligne sur la géométrie dessinée. Fait dans les deux sous-passes.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;
    Rect box, ok, cancel;
    Layout(ctx.screenW, ctx.screenH, box, ok, cancel);

    // --- Phase panneaux : fond, cadre, boutons ---
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(box.x, box.y, box.w, box.h, kColBg);
        ctx.DrawFrame(box.x, box.y, box.w, box.h, kColBorder, 2);

        const bool okHover = PointInRect(cursorX, cursorY, ok.x, ok.y, ok.w, ok.h);
        D3DCOLOR okCol = btnPressed_[kBtnOk] ? kColBtnDown : (okHover ? kColBtnHover : kColBtn);
        ctx.FillRect(ok.x, ok.y, ok.w, ok.h, okCol);
        ctx.DrawFrame(ok.x, ok.y, ok.w, ok.h, kColBorder, 1);

        if (withCancel_) {
            const bool caHover = PointInRect(cursorX, cursorY, cancel.x, cancel.y, cancel.w, cancel.h);
            D3DCOLOR caCol = btnPressed_[kBtnCancel] ? kColBtnDown : (caHover ? kColBtnHover : kColBtn);
            ctx.FillRect(cancel.x, cancel.y, cancel.w, cancel.h, caCol);
            ctx.DrawFrame(cancel.x, cancel.y, cancel.w, cancel.h, kColBorder, 1);
        }
        return;
    }

    // --- Phase texte : titre, corps, libellés de boutons (centrés) ---
    // Titre centré en haut du cadre.
    const int titleW = ctx.MeasureText(title_.c_str());
    ctx.Text(title_.c_str(), box.x + (box.w - titleW) / 2, box.y + 14, kColTitle);
    // Corps (une ligne, centré) — le squelette ne gère pas le retour à la ligne.
    const int bodyW = ctx.MeasureText(body_.c_str());
    ctx.Text(body_.c_str(), box.x + (box.w - bodyW) / 2, box.y + 56, kColText);
    // Libellés des boutons.
    const char* okLbl = "OK";
    const int okLblW = ctx.MeasureText(okLbl);
    ctx.Text(okLbl, ok.x + (ok.w - okLblW) / 2, ok.y + 6, kColText);
    if (withCancel_) {
        const char* caLbl = "Annuler";
        const int caLblW = ctx.MeasureText(caLbl);
        ctx.Text(caLbl, cancel.x + (cancel.w - caLblW) / 2, cancel.y + 6, kColText);
    }
}

// ===========================================================================
// UIManager
// ===========================================================================
UIManager& UIManager::Instance() {
    static UIManager s_instance;
    return s_instance;
}

bool UIManager::CreateWhiteTexture(IDirect3DDevice9* dev) {
    if (!dev) return false;
    IDirect3DTexture9* tex = nullptr;
    // Pool MANAGED : restauré automatiquement après un Reset de device (pas d'OnLost*).
    HRESULT hr = dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        TS2_ERR("UIManager : CreateTexture(1x1 blanche) a echoue (0x%08lX)", hr);
        return false;
    }
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        *reinterpret_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu; // blanc opaque ARGB
        tex->UnlockRect(0);
    }
    ctx_.whiteTex = tex;
    return true;
}

bool UIManager::Init(gfx::Renderer* renderer, gfx::SpriteBatch* sprites,
                     gfx::Font* font, HWND hwnd, int screenW, int screenH) {
    ctx_.renderer = renderer;
    ctx_.sprites  = sprites;
    ctx_.font     = font;
    ctx_.screenW  = screenW;
    ctx_.screenH  = screenH;
    hwnd_         = hwnd;

    if (renderer && renderer->Device())
        CreateWhiteTexture(renderer->Device());

    // Enregistre les dialogues intégrés. Le MsgBox est un popup modal : il figure en
    // TÊTE du registre (premier consommateur / rendu au-dessus, comme unk_1822438).
    dialogs_.clear();
    Register(&msgBox_);

    inited_ = true;
    TS2_LOG("UIManager initialise (%d dialogues, %dx%d)",
            static_cast<int>(dialogs_.size()), screenW, screenH);
    return true;
}

void UIManager::Shutdown() {
    // UI_DestroyAllDialogs 0x5AC270 : teardown à la fermeture.
    dialogs_.clear();
    if (ctx_.whiteTex) { ctx_.whiteTex->Release(); ctx_.whiteTex = nullptr; }
    inited_ = false;
}

void UIManager::Register(Dialog* dlg) {
    if (dlg) dialogs_.push_back(dlg);
}

bool UIManager::RouteMouseDown(int x, int y) {
    // UI_RouteLButtonDown 0x5AC740 : chaîne « premier consommateur gagne ». (Le binaire
    // garde certaines zones spéciales — onglets de chat — derrière la porte d'état
    // g_SceneMgr==6 && g_SceneSubState==4 ; la chaîne de dialogues, elle, tourne
    // toujours, chaque dialogue testant son propre bOpen.)
    for (Dialog* d : dialogs_)
        if (d && d->OnMouseDown(x, y)) return true;
    return false;
}

bool UIManager::RouteMouseUp(int x, int y) {
    // UI_RouteLButtonUp 0x5AD0F0.
    for (Dialog* d : dialogs_)
        if (d && d->OnClick(x, y)) return true;
    return false;
}

bool UIManager::RouteKey(int vk) {
    // UI_RouteKeyInput 0x5ADF50.
    for (Dialog* d : dialogs_)
        if (d && d->OnKey(vk)) return true;
    return false;
}

// --- Clic DROIT (gap UIFW-01) ----------------------------------------------
// UI_RouteRButtonDown 0x5AD5D0 : 38 slots enchaînés, 1er qui renvoie 1 consomme
// (`test eax,eax / jz / mov eax,1 / jmp` — p. ex. @0x5AD5E4 -> @0x5AD5ED pour le slot 0,
// jusqu'au `return ...(a1,a2) != 0` @0x5ADA8A du dernier). Aucun bloc de tête (à la
// différence de UI_RouteLButtonDown 0x5AC740, dont le bloc chat 0x5AC75B-0x5ACC01 précède
// la chaîne) : le routeur attaque directement les dialogues, d'où la boucle nue ci-dessous.
//
// ⚠ La GARDE D'ÉTAT n'est PAS ici : elle vit dans Input_OnRButtonDown 0x50ADB0 @0x50AE17
// (g_SceneMgr==6 && g_SceneSubState==4 && g_SelfActionState[0] ∈ {11,12,33..37} => le clic
// droit est INTÉGRALEMENT avalé, ce routeur n'étant même pas appelé). Elle est déjà
// reproduite, à son juste étage, par App::RButtonGateOpen (App/App.cpp:1167).
bool UIManager::RouteRButtonDown(int x, int y) {
    for (Dialog* d : dialogs_)
        if (d && d->OnRButtonDown(x, y)) return true;
    return false;
}

bool UIManager::RouteRButtonUp(int x, int y) {
    // UI_RouteRButtonUp 0x5ADA90 : chaîne symétrique (1er slot @0x5ADAA4, motif
    // `test eax,eax / jz loc_5ADAB7 / mov eax,1 / jmp loc_5ADF4A`).
    for (Dialog* d : dialogs_)
        if (d && d->OnRButtonUp(x, y)) return true;
    return false;
}

void UIManager::Render() {
    if (!inited_) return;
    ctx_.gameTimeSec = gfx::g_GameTimeSec;

    // Position curseur -> coordonnées client (UI_RenderAllDialogs 0x5AE2D0 :
    // GetPhysicalCursorPos @0x5AE2DD puis ScreenToClient(hWndParent 0x815184) @0x5AE2EE).
    //
    // TT-10 — GetPhysicalCursorPos, PAS GetCursorPos (le commentaire citait déjà la bonne
    // API @0x5AE2DD alors que l'appel était GetCursorPos : divergence réelle). Les deux ne
    // coïncident que sans virtualisation DPI ; sous un client 32-bit non DPI-aware sur un
    // écran mis à l'échelle, GetCursorPos renvoie des coordonnées LOGIQUES et décale tout
    // le hit-test UI + l'ancrage des infobulles. Le binaire est physique de bout en bout.
    POINT p{};
    GetPhysicalCursorPos(&p);                                   // 0x5AE2DD
    if (hwnd_) ScreenToClient(hwnd_, &p);                       // 0x5AE2EE
    const int cx = p.x, cy = p.y;

    // Rendu en ORDRE INVERSE du registre : fond d'abord, popups modaux en dernier
    // (= au-dessus). Deux sous-passes indépendantes car le lot sprite (panneaux) et le
    // lot police (texte) sont deux ID3DXSprite distincts.
    //
    // TODO [ancres 0x69E620 / 0x69E650 / 0x6A3080 / 0x69E750] : le binaire fait UN SEUL
    // Begin/End (Gfx_Begin2D/Gfx_End2D) sur UN SEUL ID3DXSprite (g_GfxRenderer+608 =
    // dword_800078), blits et texte ENTRELACÉS en ordre de soumission — cf. le pavé de
    // preuve au-dessus de `enum class UiPhase` dans UIManager.h (gap GX2D-01). Les deux
    // sous-passes ci-dessous divergent de cet ordre, mais le correctif exige de fusionner
    // Font::sprite_ et SpriteBatch::sprite_, qui vivent dans Gfx/Font.{h,cpp} et
    // Gfx/SpriteBatch.{h,cpp} — hors du périmètre de ce front. NON CORRIGÉ ICI, à dessein :
    // divergence LATENTE tant qu'un seul dialogue est enregistré (cf. Init).

    // Passe 1 : panneaux (lot sprite).
    if (ctx_.sprites && ctx_.sprites->Ready()) {
        ctx_.phase = UiPhase::Panels;
        if (SUCCEEDED(ctx_.sprites->Begin(D3DXSPRITE_ALPHABLEND))) {
            for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
                if (*it) (*it)->Render(ctx_, cx, cy);
            RunHoverChain(cx, cy);   // 0x5AE5C9 — après TOUS les draws de la sous-passe
            ctx_.sprites->End();
        }
    }

    // Passe 2 : texte (lot police).
    if (ctx_.font && ctx_.font->Ready() && ctx_.font->BeginBatch(D3DXSPRITE_ALPHABLEND)) {
        ctx_.phase = UiPhase::Text;
        for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
            if (*it) (*it)->Render(ctx_, cx, cy);
        RunHoverChain(cx, cy);       // 0x5AE5C9 — idem, volet texte de l'infobulle
        ctx_.font->EndBatch();
    }
}

// UIFW-03 — passe de SURVOL, miroir de UI_RouteRButtonExamine 0x5AE5E0 (appelée
// @0x5AE5C9, DERNIER call de UI_RenderAllDialogs 0x5AE2D0, juste avant `mov esp,ebp /
// pop ebp / retn` @0x5AE5CE). Chaîne « premier consommateur gagne » : le 1er dialogue
// qui dessine son infobulle arrête la passe (un seul tooltip par frame, @0x5AE6B1 Shop,
// @0x5AE702 Warehouse, @0x5AE71D ItemListWin, @0x5AE738 StorageWin, @0x5AE76E NpcWin,
// @0x5AE7A4 cGameHud, @0x5AE7BF QuickSlot, @0x5AE8CD QuickBar, @0x5AE8E8 ConsumableBar).
//
// ORDRE : la chaîne d'origine suit l'ordre canonique du routage ; on itère donc dialogs_
// à l'endroit (comme RouteMouseDown), et NON à l'envers comme les draws.
//
// PLACEMENT — appelée à la fin de CHAQUE sous-passe, et non une 3e fois après les deux
// (ce que suggérait le dossier de gaps) : UiContext::FillRect ne dessine QU'EN phase
// Panels (UIManager.cpp:18) et UiContext::Text QU'EN phase Text (UIManager.cpp:39), donc
// une passe placée après les deux ne dessinerait RIEN. Le binaire, lui, n'a qu'UN SEUL
// batch 2D (Gfx_Begin2D @0x51B189 ... UI_RenderAllDialogs @0x51B59D ... Gfx_End2D
// @0x51B5A7 dans Scene_LoginRender 0x51B020), où le tooltip est simplement soumis en
// dernier. Curseur et états étant identiques dans les deux sous-passes, le gagnant est le
// MÊME dialogue de façon déterministe : l'infobulle est donc bien soumise en dernier dans
// chaque lot, donc au-dessus. C'est la transposition la plus fidèle possible tant que
// l'archi deux-passes tient (cf. TODO GX2D-01 au-dessus de `enum class UiPhase`).
void UIManager::RunHoverChain(int cx, int cy) {
    for (Dialog* d : dialogs_)
        if (d && d->OnHover(ctx_, cx, cy)) return;   // 1er consommateur gagne
}

void UIManager::ResetAll() {
    // UI_ResetAllDialogs 0x5AC3F0 : état neutre aux transitions de scène. ~42 cibles
    // (@0x5AC408-0x5AC589), SANS exception — MsgBox inclus (@0x5AC430) — plus un
    // UI_FocusEditBox(&g_UIEditBoxMgr, 0) INCONDITIONNEL en tête (@0x5AC3FE). Fermer
    // tous les dialogues enregistrés est donc bien le miroir fidèle ici.
    for (Dialog* d : dialogs_) if (d) d->Close();
}

void UIManager::CloseAll() {
    // N-1 — UI_CloseAllDialogs 0x5AC590 (ouvrir une fenêtre ferme les autres). Ce N'EST
    // PAS ResetAll : sa liste est FIGÉE (~27 cibles, @0x5AC5A2-0x5AC6D1) et laisse
    // délibérément intacts MsgBox (dword_1822438), NoticeDlg, TextInput, ItemListWin,
    // StorageWin, ClanWin, NpcWin et AutoPlay — tous absents de la liste. D'où le filtre
    // ClosedByCloseAll() : sans lui, ouvrir une fenêtre avalait la boîte modale en cours
    // (défaut réel, les 3 appelants actuels — ClanContextMenu.cpp:238,
    // StoragePwWindow.cpp:156, PartyWindow.cpp:135 — passent tous par ici).
    for (Dialog* d : dialogs_)
        if (d && d->ClosedByCloseAll()) d->Close();

    // Le focus EDIT n'est relâché que CONDITIONNELLEMENT (@0x5AC669) :
    //   if (g_UIEditBoxMgr == 20) UI_FocusEditBox(&g_UIEditBoxMgr, 0);   // @0x5AC672
    // — contrairement au relâchement inconditionnel de ResetAll (@0x5AC3FE). Sans état de
    // focus EDIT modélisé dans UIManager (les 21 EDIT natifs de UI_CreateEditBoxes 0x50E460
    // ne sont pas câblés, cf. UI/Win32EditBox.h), rien à faire ici.
    // TODO [ancre 0x5AC669] : à porter si/quand le focus EDIT natif est câblé.

    // TODO [ancre 0x5AC59B] : le 2e paramètre de UI_CloseAllDialogs(this, a2) gate
    //   `cDrawWin_Close(dword_1839290)` @0x5AC5A2 + `cGameHud_Hide(dword_1839568)` @0x5AC5AC.
    // Les 3 sites appelants réels du binaire passent TOUS a2=1 (UI_ClanWin_Open @0x5D8E50,
    // UI_StoragePwWnd_ProcNet @0x666F44, UI_MemberSelectWnd_ProcNet @0x6677C4) : le HUD
    // DEVRAIT donc être masqué ici. NON MODÉLISABLE dans UIManager, qui ne possède ni le
    // HUD ni la DrawWin (ils vivent dans SceneManager : hud_). Volontairement PAS de
    // paramètre `alsoHudAndDrawWin` ajouté : personne ne pourrait l'honorer, ce ne serait
    // qu'un bouton mort. À traiter côté front Scene (cf. rapport de front, wiringTodo).
}

} // namespace ts2::ui

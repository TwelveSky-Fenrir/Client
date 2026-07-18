// UI/LoginScene.cpp — login shell scene: lifecycle, dispatch, and Intro/EnterWorld rendering.
// ServerSelect/Login/CharSelect logic lives in the sibling LoginScene_*.cpp files (mechanical
// split of the original monolithic LoginScene.cpp; see LoginScene.h for the full class).
// Ground truth = Docs/TS2_CLIENT_SHELL.md §2.1/§4 + disassembly (idaTs2):
//   Scene_LoginUpdate 0x51A8D0, Scene_LoginRender 0x51B020,
//   Scene_LoginOnMouseDown 0x51B5D0, Scene_LoginOnMouseUp 0x51B780,
//   UI_NoticeDlg_Open 0x5C0280, UI_NoticeDlg_OnLButtonUp 0x5C03F0 (NoticeWait sub-state exit
//   mechanism — closes Docs/TS2_LOGINSCENE_AUDIT.md §3.6, decompiled 2026-07-14: see
//   LoginSub::NoticeWait in LoginScene.h).
//
// Widgets (EditBox/Button) are driven THROUGH THEIR PUBLIC INTERFACE:
//   - EditBox: SetMaxLength/SetPassword/SetText/Clear/SetFocused/Focused/Text/
//              OnMouseDown/OnChar/OnKey + SetOnSubmit/SetOnTab callbacks.
//   - Button:  SetLabel/Label + OnMouseDown/OnMouseUp/OnMouseMove events and the
//              SetOnClick callback (latch: armed on down, validated on up inside).
// Rendering is flat (SpriteBatch for fills + Font for text): widget geometry is read via
// X()/Y()/W()/H() and state via Focused()/Hovered()/Pressed().
#include "UI/LoginScene.h"
#include "UI/LoginScene_Internal.h" // kColText (shared by the split family)
#include "UI/UiProjection.h"       // ui::ProjectDesignAnchor (UI_ProjectSpriteToScreen 0x50F5D0)
#include "Config/GameOptions.h"    // ts2::config::Cfg_SaveLastServer (G02_GINFO\010.BIN, write-only)
#include "Net/Login.h"             // ConnectLoginServer / LoginRequest / ConnectGameServer
#include "Net/CharSelectPackets.h" // AccountKeepAlive/CreateCharacter/CharSlotAction/ReqEnterCharInfo/ReqCancelEnter
#include "Net/Rng.h"                // DefaultRng() — Rng_Next() % 360 for spawnRotationDeg (see GameState.h)
#include "Net/GameServerDomains.h"  // SelectGameServerHost / g_ServerMode (Net_SelectServerDomain 0x53FE90)
#include "Game/GameState.h"        // game::g_World.zoneId (consumed by EnterWorldFlow)
#include "Game/StringTables.h"     // game::g_Strings.bannedWords (001.DAT, 1432 banned words — creation filter)
#include "Game/ClientRuntime.h"    // game::Str(id) — real StrTable005 text for CharSelect notices
#include "Game/GameDatabase.h"     // game::GetItemInfo / WeaponClassFromTypeCode (entry motion 0x4CC870)
#include "Game/MiscManagers.h"     // game::Cursors() / kCursorDefault (scene-entry cursor reset, 0x4C1110)
#include "Asset/ImgFile.h"         // asset::ImgFile (.IMG loader, real ServerSelect/Login background)
#include "Gfx/Camera.h"            // gfx::Camera — application projection (Gfx_InitDevice 0x69BFC6)
#include "Core/Log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace ts2::ui {

namespace {

// kFieldW/kFieldH: nominal field-size constants from the original login-panel geometry block
// (Scene_LoginRender 0x51B020) — unused by any surviving function (kept for fidelity to the
// original declaration set; see LoginScene_Internal.h for kColText/kBtnW/kBtnH, and
// LoginScene_Login.cpp for kPanelW/kPanelH).
constexpr int kFieldW = 170, kFieldH = 22;

} // namespace

LoginScene::~LoginScene() { Shutdown(); }

bool LoginScene::Init(IDirect3DDevice9* device, net::NetSystem* net, HWND notifyWnd,
                      int screenW, int screenH, int serverModeFlag,
                      gfx::Renderer* renderer) {
    device_        = device;
    net_           = net;
    notifyWnd_     = notifyWnd;
    screenW_       = screenW;
    screenH_       = screenH;
    serverModeFlag_ = serverModeFlag;           // dword_166918C: consumed by BuildServerList()
    net::g_ServerMode = serverModeFlag;         // mirrors g_ServerModeFlag 0x166918C for
                                                // Net_SelectServerDomain 0x53FE90 (connect game-server)
    gfxRenderer_   = renderer;                  // CharSelect 3D preview (cf. LoginScene.h::Init)
    if (!device_) { TS2_ERR("LoginScene::Init : device nul"); return false; }

    // GXD font (Font_AddTtfResource 0x4C0E70 then D3DXCreateFontIndirect).
    gfx::Font::AddTtfResource(false);
    if (!font_.Init(device_, screenW_, screenH_))
        TS2_WARN("LoginScene : police indisponible (texte non rendu).");
    if (!sprites_.Create(device_))
        TS2_WARN("LoginScene : sprite batch indisponible (aplats non rendus).");
    gfx::SetActiveSprite(&sprites_);
    CreateWhiteTexture();

    // Input fields (EM_LIMITTEXT 0x7F; password masked with '*').
    idBox_.SetMaxLength(0x7F); idBox_.SetPassword(false); idBox_.SetTextColor(kColText);
    pwBox_.SetMaxLength(0x7F); pwBox_.SetPassword(true);  pwBox_.SetTextColor(kColText);

    // Button labels — ASSUMED PLACEHOLDER, NOT extracted from StrTable005 (re-verified by
    // direct decompilation of Scene_CharSelectRender 0x51CED0, session 2026-07-14, CharSelect
    // UI/flow audit: of the 40 StrTable005_Get calls in this function, NONE serves an action-
    // button label — all 40 are for the create-form VALUE TEXT, dynamic per job/faction/face,
    // cf. CharSelectFlow.h::CharCreateForm). No French string "Créer"/"Supprimer"/"Entrer"/
    // "Confirmer"/"Annuler"/"Oui"/"Non"/"Quitter" is hardcoded in the executable either.
    // The real List screen hit-tests its action buttons via Sprite2D_HitTest on atlas rects
    // (Scene_CharSelectOnMouseUp 0x522E50, Sprite2D_GetWidth/GetHeight/HitTest refs on
    // unk_94D7B4/unk_95B364/unk_95B3F8) — GRAPHICAL CAPTIONS (UI atlas texture), not drawn text
    // for these buttons in the binary. There is therefore no "correct" StrTable005 id to wire
    // here: these are neither faithful text nor wrong ids, but a TEMPORARY fallback until the
    // button sprite atlas is loaded (rendering TODO, cf. file header and LayoutCreateForm/
    // LayoutCharSelect below).
    okBtn_.SetLabel("OK");        exitBtn_.SetLabel("Quitter");  optBtn_.SetLabel("Ombres");
    enterBtn_.SetLabel("Entrer"); backBtn_.SetLabel("Retour");   // backBtn_ = slots 963/964/965
                                                                  // (unk_90B80C) = BACK button ->
                                                                  // ServerSelect, CONFIRMED by IDA
                                                                  // this session (Scene_CharSelect
                                                                  // OnMouseUp EA 0x525A1A-0x525A71:
                                                                  // Net_CloseSocket + scene=2). The
                                                                  // real QUIT is slot 25
                                                                  // (unk_8E99C4, EA 0x525AA5), hit-
                                                                  // tested separately in
                                                                  // CharSelectOnMouseUp. The TEXT
                                                                  // remains a placeholder (skinned
                                                                  // button, label not drawn).
    createBtn_.SetLabel("Creer");  deleteBtn_.SetLabel("Supprimer");
    restoreBtn_.SetLabel("Restaurer"); // placeholder text; real sprite slots 3086/3087/3088 (0x51E354)
    createConfirmBtn_.SetLabel("Confirmer"); createCancelBtn_.SetLabel("Annuler");
    // (Delete/exit Yes/No confirmations are now the sprite-driven MsgBox msgBox_, not a Button.)
    jobMinusBtn_.SetLabel("-");    jobPlusBtn_.SetLabel("+");
    factionMinusBtn_.SetLabel("-"); factionPlusBtn_.SetLabel("+");
    faceMinusBtn_.SetLabel("-");   facePlusBtn_.SetLabel("+");
    hairMinusBtn_.SetLabel("-");   hairPlusBtn_.SetLabel("+");
    variantMinusBtn_.SetLabel("-"); variantPlusBtn_.SetLabel("+");
    createNameBox_.SetMaxLength(12); createNameBox_.SetTextColor(kColText);

    // Login button actions (latch validated on release inside the button).
    okBtn_.SetOnClick([this] { loginSub_ = LoginSub::Trigger; });               // -> DoLogin
    exitBtn_.SetOnClick([this] {                                                 // -> ServerSelect
        pending_  = ts2::Scene::ServerSelect;
        loginSub_ = LoginSub::Init;
    });
    optBtn_.SetOnClick([this] { shadowsEnabled_ = !shadowsEnabled_; });          // toggle 0x84DEF8

    // Field keyboard navigation: Tab -> next field, Enter -> submit.
    idBox_.SetOnTab   ([this] { SetFocus(2); });                 // ID -> PW
    idBox_.SetOnSubmit([this] { SetFocus(2); });                 // Enter on ID -> PW
    pwBox_.SetOnTab   ([this] { SetFocus(1); });                 // PW -> ID (cycle)
    pwBox_.SetOnSubmit([this] { loginSub_ = LoginSub::Trigger; }); // Enter on PW -> login

    // CharSelect buttons: down-arm/up-validate, same as Login buttons (Button::SetOnClick).
    enterBtn_.SetOnClick([this] { game::OnEnterButtonClicked(charState_, charHost_); });
    // BACK (unk_90B80C = slots 963/964/965, column @v117+185): Scene_CharSelectOnMouseUp
    // EA 0x525A1A-0x525A71 (re-decompiled this session, front W4-F1) — this button's REAL
    // action is NOT "Quit" but a RETURN to ServerSelect: guard `this[+0xF598]==3`
    // (preview entry in progress -> ignore, EA 0x525A33), else Net_CloseSocket(&g_NetClient)
    // (0x463000, EA 0x525A46) then this[0]=2 (scene=ServerSelect, EA 0x525A51), this[1]=0
    // (sub-state Init, EA 0x525A5D), this[2]=0 (frame counter, EA 0x525A6A). The old wiring
    // to OnQuitButtonClicked was WRONG (the real Quit is slot 25, cf. CharSelectOnMouseUp).
    // [A5] Delegated to game::RequestBackToServerSelect, which ALREADY carries the exact
    // sequence (Entering guard EA 0x525A33; host.CloseConnection = Net_CloseSocket 0x463000 EA
    // 0x525A46; pendingTransition=ServerSelect EA 0x525A51; subState=Init EA 0x525A5D;
    // frameCounter=0 EA 0x525A6A) — the old code REPLICATED it by hand here, setting
    // `pending_` DIRECTLY instead of going through pendingTransition: the transition
    // short-circuited UpdateCharSelect (and thus its consumption at tick head), and the
    // `subState == Active` guard of RequestBackToServerSelect was not applied.
    // The transition is now consumed by CharSelectUpdate (ServerSelect branch).
    backBtn_.SetOnClick([this] { game::RequestBackToServerSelect(charState_, charHost_); });
    createBtn_.SetOnClick([this] { game::OnCreateButtonClicked(charState_, charHost_); });
    deleteBtn_.SetOnClick([this] { game::OnDeleteButtonClicked(charState_, charHost_); });
    // QUIT (slots 25/26/27 @ (x0, y0+222)): guard `this[+0xF598]==3` EA 0x525ABE then
    // Net_CloseSocket + g_QuitFlag=1 — carried by game::OnQuitButtonClicked.
    quitBtn_.SetOnClick([this] { game::OnQuitButtonClicked(charState_, charHost_); });
    createConfirmBtn_.SetOnClick([this] { game::ConfirmCreateCharacter(charState_, charHost_); });
    createCancelBtn_.SetOnClick([this]  { game::CancelCreateForm(charState_); });
    jobMinusBtn_.SetOnClick([this]     { game::SetCreateJob(charState_, -1); });
    jobPlusBtn_.SetOnClick([this]      { game::SetCreateJob(charState_, +1); });
    factionMinusBtn_.SetOnClick([this] { game::SetCreateFaction(charState_, -1); });
    factionPlusBtn_.SetOnClick([this]  { game::SetCreateFaction(charState_, +1); });
    faceMinusBtn_.SetOnClick([this]    { game::SetCreateFace(charState_, -1); });
    facePlusBtn_.SetOnClick([this]     { game::SetCreateFace(charState_, +1); });
    hairMinusBtn_.SetOnClick([this]    { game::SetCreateHairColor(charState_, -1); });
    hairPlusBtn_.SetOnClick([this]     { game::SetCreateHairColor(charState_, +1); });
    variantMinusBtn_.SetOnClick([this] { game::SetCreateVariant(charState_, -1); });
    variantPlusBtn_.SetOnClick([this]  { game::SetCreateVariant(charState_, +1); });
    // Yes/No confirmations (delete + exit): now carried by msgBox_ (ConfirmMsgBox), whose OK
    // action is supplied at open time (charHost_.ShowDeleteConfirm for delete type 2;
    // ServerSelectOnMouseUp for exit type 1). No more Yes/No Button::SetOnClick here.

    // Real "Confirm"/"Cancel" button textures (Docs/TS2_LOGIN_BUTTON_ASSETS.md §4): Login's
    // OK/Quit share the same generic sprite pair. Falls back to the colored rect
    // (ApplyConfirmCancelSkin) if the .IMG files are missing. Delete/exit Yes/No are no longer
    // a Button (the sprite-driven MsgBox draws slots 8-13 directly).
    ApplyConfirmCancelSkin(okBtn_, exitBtn_);

    // CharSelect List screen: each button in the main column has its OWN dedicated idle/hover/
    // pressed sprites in atlas 001 (Docs/TS2_CHARSELECT_RE.md §4/§7, slots confirmed EA by EA)
    // — NOT the generic Confirm/Cancel pair. SetNormal = idle state (these buttons DO have a
    // distinct idle sprite, unlike Login buttons which only have hover/pressed). restoreBtn_/
    // enterBtn_/createBtn_/deleteBtn_/backBtn_ are LATCHED (hit-tested in CharSelectOnMouseDown/
    // Up); RENAME/STORAGE/QUIT, with no Button member or hit-test, are drawn as direct idle
    // sprites in CharListRender().
    {
        auto skinColBtn = [this](Button& b, int idle, int hover, int pressed) {
            if (gfx::GpuTexture* t = GetAtlasSprite(idle))    b.SetNormal(WidgetSprite(t->Handle()));
            if (gfx::GpuTexture* t = GetAtlasSprite(hover))   b.SetHover(WidgetSprite(t->Handle()));
            if (gfx::GpuTexture* t = GetAtlasSprite(pressed)) b.SetPressed(WidgetSprite(t->Handle()));
        };
        skinColBtn(enterBtn_,   16,  17,  18); // ENTER
        skinColBtn(createBtn_,  19,  20,  21); // CREATE
        skinColBtn(deleteBtn_,  22,  23,  24); // DELETE
        skinColBtn(backBtn_,   963, 964, 965); // BACK
        skinColBtn(restoreBtn_, 3086, 3087, 3088); // RESTORE @ y=v117-37, EA 0x51E354/0x525B46
    }

    // Create-form screen: -/+ buttons = slot 41 unk_8EA304 ("-") and slot 42 unk_8EA398 ("+"),
    // Scene_CharSelectRender 0x51CED0.
    // 🔴 FIDELITY FIX (re-disassembled EA by EA this front): these sprites are a SINGLE state =
    // PRESSED, drawn ONLY if the this[3..12] latch is armed. The binary has only ONE blit site
    // per button, GUARDED by the latch:
    //   `mov ecx,[ebp+var_470]` @0x51E983 ; `cmp dword ptr [ecx+0Ch],0` @0x51E989 ;
    //   `jz short loc_51E9AA` @0x51E98D  -> latch NULL = SKIP THE DRAW.
    //   If armed only: `add edx,4Eh` (y=panelY+78) @0x51E995 ; `add eax,73h` (x=panelX+115)
    //   @0x51E99C ; `mov ecx, offset unk_8EA304` @0x51E9A0 ; `call Sprite2D_Draw` @0x51E9A5.
    // STRICTLY identical pattern for "+" (latch +0x10 @0x51E9B0, unk_8EA398 @0x51E9CE) and for
    // the other 8 (latches +0x14 @0x51E9D9 … +0x30 @0x51EAF9). NO hover branch, NO normal
    // branch. The idle "-"/"+" glyphs are therefore painted IN THE PANEL TEXTURE (slot 40); the
    // 41/42 slots are only PRESSED-state overlays. SetNormal would have drawn them PERMANENTLY
    // (Button::DrawSkin blits `normal_` unconditionally) = 10 spurious sprites from frame 1.
    // With SetPressed alone: unarmed -> skin=&normal_ invalid -> `if (skin->Valid())` false +
    // fallbackTex_ null -> NOTHING drawn; armed -> slot 41/42 blitted. Exactly the binary.
    // Confirm/Cancel, however, ARE tri-state (hit-test unk_8E9084 @0x51EB4F, hover @0x51EB70,
    // pressed unk_8E9118 @0x51EB90) -> generic pair ApplyConfirmCancelSkin.
    {
        Button* minusBtns[] = { &jobMinusBtn_, &factionMinusBtn_, &faceMinusBtn_, &hairMinusBtn_, &variantMinusBtn_ };
        Button* plusBtns[]  = { &jobPlusBtn_,  &factionPlusBtn_,  &facePlusBtn_,  &hairPlusBtn_,  &variantPlusBtn_ };
        // PRESSED overlay only — latch guard EA 0x51E989 (`cmp [ecx+0Ch],0 ; jz`).
        if (gfx::GpuTexture* t = GetAtlasSprite(41)) for (Button* b : minusBtns) b->SetPressed(WidgetSprite(t->Handle()));
        // Same for "+" — latch guard EA 0x51E9B0 (`cmp [ecx+10h],0 ; jz`).
        if (gfx::GpuTexture* t = GetAtlasSprite(42)) for (Button* b : plusBtns)  b->SetPressed(WidgetSprite(t->Handle()));
    }
    ApplyConfirmCancelSkin(createConfirmBtn_, createCancelBtn_);

    BuildCharSelectHost();

    // ServerSelect network/persistence callbacks (game::ServerSelectHost):
    //  - QueryServerStatus: wired to the REAL network layer (ts2::net::QueryServerStatusLive,
    //    ss-netconnect contract = Net_QueryServerStatus 0x519CC0 — TCP connect + recv 17 bytes).
    //    Translates LiveServerStatus -> game::ServerStatus (same 3 fields: maxPop/loadStep/curPop).
    //  - SaveLastServer: Cfg_SaveLastServer 0x519C40 (G02_GINFO\010.BIN persistence, server index).
    serverHost_.QueryServerStatus = [](const std::string& name, uint16_t port) {
        const ts2::net::LiveServerStatus r = ts2::net::QueryServerStatusLive(name, port);
        game::ServerStatus s;
        s.maxPopulation     = r.maxPopulation;
        s.loadStep          = r.loadStep;
        s.currentPopulation = r.currentPopulation;
        return s;
    };
    serverHost_.SaveLastServer = [](int32_t /*groupIndex*/, int32_t serverIndex) {
        ts2::config::Cfg_SaveLastServer(serverIndex);
    };

    BuildServerList();
    ResetLoginFields();
    loginSub_ = LoginSub::Init;

    // ServerSelect/Login background (Scene_ServerSelectUpdate 0x518B30, EA 0x518C29-0x518C40):
    // this[168] = 2380 or 2381, Rng_Next()%2 (50/50). Drawn once here (cf. LoginScene.h::
    // bgAtlasSlot_ note about the simplification vs. the original). Mirrored into
    // serverState_.backgroundImageId so ServerSelectRender reads the SAME draw (shared
    // ServerSelect<->Login scene memory, cf. GetAtlasSprite/DrawFullscreenBg).
    // SINGLE RNG STREAM (W5b): the binary has only ONE _holdrand state (Rng_Next 0x7603FD,
    // 678 code xrefs: Net_Send* builders, Snow_/Rain_ FX, HUD, and these background draws).
    // We therefore tap net::DefaultRng() and NOT std::rand(), which here was a SECOND
    // independent stream with no binary counterpart.
    // Anchor: Scene_ServerSelectUpdate 0x518B30, `call Rng_Next` EA 0x518C19 then
    // `and eax, 80000001h` (signed %2 idiom); `mov [eax+2A0h], 94Ch` EA 0x518C31 (=2380,
    // case 0) / `mov [ecx+2A0h], 94Dh` EA 0x518C40 (=2381, case != 0).
    bgAtlasSlot_ = (net::DefaultRng().NextMod(2)) ? 2381 : 2380;
    serverState_.backgroundImageId = bgAtlasSlot_;
    // [A4] The CharSelect background draw (this[15713] = 2383/2384/2385) is NO LONGER done
    // HERE. It lives in the Init block of Scene_CharSelectUpdate 0x51BD90 (`call Rng_Next` EA
    // 0x51C23A, `%3` EA 0x51C245, writes 0x51C261/0x51C270/0x51C27F) and is therefore RE-DRAWN
    // on EVERY entry into scene 4 — not once at boot. Carried by game::UpdateCharSelect
    // (RunInitBlock) into charState_.backgroundSlot, which the render reads. Cf. LoginScene.h.

    // --- CharSelect 3D preview (Char_RenderModel 0x527020): persistent resources ---
    // gfx::MeshRenderer::Init requires a gfx::Renderer& (of which it only reads Device()); without
    // it, charPreviewReady_ stays false and CharSelectRenderPreview3D() draws NOTHING — which is
    // the SAFE behavior (the binary, for its part, does draw: cf. wiring TODO CHARSELECT_3D).
    if (gfxRenderer_ && charMesh_.Init(*gfxRenderer_)) {
        // gameDataDir="." : the process CWD is already switched to gameDataDir by
        // App::ResolveGameDataDir() (App/App.cpp) well before scene 4, as early as App::Init.
        // SAME convention and SAME reason as Scene/WorldRenderer.cpp (Models()/Motions()).
        charModels_  = std::make_unique<gfx::ModelCache>(charMesh_, ".");
        charMotions_ = std::make_unique<gfx::MotionCache>(".");
        // D3DLIGHT9 0x18C5358 -> SetLight(0,&light) @0x51D226: diffuse/ambient 0.8 and
        // direction normalize(-1,-1,-1). Pushed ONCE (the values are binary literals, rewritten
        // identically every frame by Scene_CharSelectRender).
        gfx::CharPreview3D::ApplyLight(charMesh_);
        charPreviewReady_ = true;
    } else if (!gfxRenderer_) {
        TS2_WARN("LoginScene : aucun gfx::Renderer fourni -> apercu 3D CharSelect DESACTIVE "
                 "(cf. wiring TODO CHARSELECT_3D, Scene/SceneManager.cpp:337).");
    } else {
        TS2_WARN("LoginScene : MeshRenderer::Init a echoue -> apercu 3D CharSelect desactive.");
    }

    // Wiring of the ServerSelect screen's REAL sprites (panel/load bar/action button, cf.
    // UI/ServerSelectRender.h): requires the D3D9 device, unavailable before this point
    // (device_ was just assigned at the top of this function).
    serverSelectRender_.SetDevice(device_);
    // Same for the real Intro logo (cf. UI/IntroRender.h::SetDevice — fidelity bug fixed
    // 2026-07-14: ctx.renderer was never populated by RenderIntro(), without this SetDevice()
    // the real logo would NEVER show, only the "Logo #NNN" fallback).
    introRender_.SetDevice(device_);
    // Same for the real EnterWorld transition screen (cf. UI/EnterWorldRender.h): without this
    // SetDevice(), the zone background (008_%05d.IMG) and the progress bar (atlas 001, slots
    // 1140..1160) can never load a real texture and Render() always falls back to diagnostic
    // colored fills.
    enterWorldRender_.SetDevice(device_);
    return true;
}

void LoginScene::Shutdown() {
    // Fidelity/lifetime: Net_ServerStatusThread 0x518AB0 is detached in the binary; here we
    // JOIN the worker before releasing anything (it writes serverState_ under serverMutex_) —
    // an assumed fidelity deviation (join bounded by QueryServerStatusLive's timeout) to avoid
    // any use-after-free of LoginScene.
    if (statusThread_.joinable()) statusThread_.join();
    // W5b — lifetime: g_LoginNoticeHook is a GLOBAL (Net/Login.cpp:30) that captures `this`.
    // LoginScene is owned by a unique_ptr member of SceneManager (SceneManager.h): the scene
    // dies BEFORE the global, unlike charHost_.ShowNotice (a member, which dies with the
    // scene). So the hook is cleared here to never leave a dangling `this` — same discipline
    // as the join above.
    net::g_LoginNoticeHook = nullptr;
    if (gfx::ActiveSprite() == &sprites_) gfx::SetActiveSprite(nullptr);
    if (whiteTex_) { whiteTex_->Release(); whiteTex_ = nullptr; }
    atlasCache_.clear(); // release the GpuTextures before the device is destroyed
    // CharSelect 3D preview: same discipline as atlasCache_ — the cache's SkinnedModel VB/IB/
    // textures, then the vertex declaration and shaders of MeshRenderer, must be released
    // BEFORE the device is destroyed (owned by SceneManager, which destroys LoginScene first).
    // ORDER: models (reference the device) -> motions (100% CPU data, no device object) ->
    // MeshRenderer.
    charModels_.reset();
    charMotions_.reset();
    charMesh_.Shutdown();
    charPreviewReady_ = false;
    gfxRenderer_      = nullptr;
    // NOTE: no OnDeviceLost/OnDeviceReset hook is required for these resources: MeshRenderer
    // creates ALL its VB/IB/textures in D3DPOOL_MANAGED (Gfx/MeshRenderer.cpp:369/381/451),
    // which survives a Reset without re-upload — exactly why Scene/WorldRenderer.cpp doesn't
    // touch them either in its own OnDeviceLost/OnDeviceReset (it only handles its font there).
    // font_ / sprites_ release themselves via their destructors.
}

void LoginScene::OnDeviceLost()  { font_.OnDeviceLost();  sprites_.OnLostDevice();  }
void LoginScene::OnDeviceReset() { font_.OnDeviceReset(); sprites_.OnResetDevice(); }

void LoginScene::CreateWhiteTexture() {
    if (!device_) return;
    // 1x1 white texture, MANAGED pool (survives a device reset). Modulated by the Draw color,
    // it's the fill primitive (FillRect). After the 2026-07-15 zero-fallback purge, the
    // Init / ServerSelect / Login (Credential) screens use NO FillRect anymore (everything is
    // wired onto real atlas sprites). FillRect only remains for: (1) the CharSelect placeholder
    // (scene 4, 1:1 wiring deferred), (2) the notice modal panel (RenderNotice). whiteTex_ thus
    // remains necessary for these two cases.
    if (FAILED(device_->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                      D3DPOOL_MANAGED, &whiteTex_, nullptr))) {
        whiteTex_ = nullptr;
        return;
    }
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(whiteTex_->LockRect(0, &lr, nullptr, 0))) {
        *reinterpret_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
        whiteTex_->UnlockRect(0);
    }
}

// Colored fill: a blit of the 1x1 texture scaled to w×h at (x,y). We pass compensatePos=true
// (like UI_DrawSpriteScaledAlpha 0x457D70) so the scale matrix places the top-left corner
// exactly at (x,y). Call between sprites_.Begin() and sprites_.End().
void LoginScene::FillRect(int x, int y, int w, int h, D3DCOLOR color) {
    if (!whiteTex_ || !sprites_.Ready() || w <= 0 || h <= 0) return;
    sprites_.DrawSpriteScaled(whiteTex_, nullptr, x, y,
                              static_cast<float>(w), static_cast<float>(h),
                              color, /*compensatePos=*/true);
}

POINT LoginScene::CursorClient() const {
    POINT p{0, 0};
    if (GetCursorPos(&p) && notifyWnd_) ScreenToClient(notifyWnd_, &p);
    return p;
}

// Resolves a slot of the shared atlas unk_8E8B50 (AssetMgr_InitAllSlots 0x4deb50, category 1
// loop -> "G03_GDATA\D01_GIMAGE2D\001\") to its GpuTexture, with lazy caching.
// OFFSET CONFIRMED by direct decompilation (idaTs2, session 2026-07-14): Sprite2D_BuildPath
// 0x4d68e0 formats the filename with `slot+1` (vsnprintf "...%05d.IMG", a3+1) — the real file
// is NOT 001_<slot>.IMG but 001_<slot+1>.IMG. Verified against the decompressed assets: the
// Intro logo sequence (subState 1..33, slots 798..830) matches EXACTLY the 33 files
// 001_00799..001_00831.IMG (all 668x229 DXT1); the ServerSelect buttons (slot ButtonImageId(i),
// e.g. 1786) point to an UNRELATED asset (001_01786.IMG = 737x755 panel) while slot+1
// (001_01787.IMG) is indeed a consistent 153x23 DXT3 button.
gfx::GpuTexture* LoginScene::GetAtlasSprite(int slotIndex) {
    if (!device_) return nullptr;
    auto it = atlasCache_.find(slotIndex);
    if (it != atlasCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG", slotIndex + 1);
    asset::ImgFile img;
    if (img.Load(path))
        tex.CreateFromImgFile(device_, img);
    else
        TS2_WARN("LoginScene : atlas slot %d illisible (%s) — rien dessine (zero repli).", slotIndex, path);

    auto res = atlasCache_.emplace(slotIndex, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

// Fullscreen ServerSelect/Login background (Sprite2D_DrawScaled(unk_8E8B50 + 148*this[168], 0,
// 0, nWidth/1024, nHeight/768) — EA 0x519461 / 0x51B207, IDENTICAL in both scenes). Real
// texture scaled to the screen. NO fallback (2026-07-15): if the sprite fails to load, nothing
// is drawn — faithful to Sprite2D_DrawScaled/EnsureLoaded, which fails silently
// (Docs/TS2_SHELL_RENDER_TRUTH.md §2: "Never a FillRect").
// [A7/B1] FIDELITY FIX (2026-07-16): the divisors are the LITERAL CONSTANTS 1024.0f / 768.0f,
// NOT the texture dimensions. The binary NEVER reads the sprite size here — the old code
// (`screenW_/tex->Width()`) contradicted its own comment and produced a wrong scale whenever a
// background wasn't exactly 1024x768.
// FULL PROOF (direct disassembly, re-verified this front):
//   flt_1669178 / flt_166917C: `data_refs` = 4 refs = 1 WRITE + 3 reads.
//     SOLE write: WinMain 0x4609C0 -> `fld ds:flt_7A68C8 ; fstp ds:flt_1669178` @0x4609D3/
//     0x4609D9 then `fld ds:flt_7A68C4 ; fstp ds:flt_166917C` @0x4609DF/0x4609E5.
//     reads: Scene_ServerSelectRender @0x51942F, Scene_LoginRender @0x51B1D5,
//                Scene_CharSelectRender @0x51D279 — all `fdiv`.
//   values verified BYTE-EXACT (get_bytes): flt_7A68C8 = 00 00 80 44 = 1024.0f;
//                                             flt_7A68C4 = 00 00 40 44 =  768.0f.
// These are therefore NOT resolution variables: they are the design REFERENCE dimensions,
// frozen at startup and never rewritten.
//
// Sprite2D_DrawScaled 0x4D6BF0 argument order (4 args + this), pushes @0x51D257-0x51D2AB:
//   fild nHeight ; fdiv flt_166917C ; ... push ecx ; fstp [esp]   <- scaleY (pushed 1st)
//   fild nWidth  ; fdiv flt_1669178 ; ... push ecx ; fstp [esp]   <- scaleX
//   push 0                                                        <- y
//   push 0                                                        <- x
//   mov ecx, atlas + 148*this[15713] ; call Sprite2D_DrawScaled
// => Sprite2D_DrawScaled(spr, x=0, y=0, scaleX = nWidth/1024.0f, scaleY = nHeight/768.0f).
// The `push ecx ; fstp [esp]` idiom (slot reservation + FPU write) is what makes it look like
// 3 dwords are pushed: there are actually 4.
//
// nWidth = 0x1669184, nHeight = 0x1669188 (= screenW_/screenH_ on the C++ side).
// NO fallback: if the sprite fails to load, nothing is drawn — faithful to
// Sprite2D_DrawScaled/EnsureLoaded, which fails silently.
void LoginScene::DrawFullscreenBg(int slotIndex) {
    constexpr float kDesignW = 1024.0f; // flt_7A68C8 -> flt_1669178 (WinMain @0x4609D9)
    constexpr float kDesignH = 768.0f;  // flt_7A68C4 -> flt_166917C (WinMain @0x4609E5)
    gfx::GpuTexture* tex = GetAtlasSprite(slotIndex);
    if (tex && tex->Handle() && sprites_.Ready()) {
        const float sx = static_cast<float>(screenW_) / kDesignW;
        const float sy = static_cast<float>(screenH_) / kDesignH;
        sprites_.DrawSpriteScaled(tex->Handle(), nullptr, 0, 0, sx, sy,
                                  gfx::kSpriteWhite, /*compensatePos=*/true);
    }
}

// Slots 9/10 = "Confirm" hover/pressed (001_00010/00011.IMG), 12/13 = "Cancel"
// hover/pressed (001_00013/00014.IMG) — cf. Docs/TS2_LOGIN_BUTTON_ASSETS.md §4. These 4 files
// are confirmed present on disk (doc §4 "Verified on disk").
//
// REMOVED 2026-07-15 (user request: ZERO fallback): no more SetFallbackTexture/
// SetFallbackColors — if a button .IMG is missing at runtime, Button::DrawSkin draws NOTHING
// (instead of an invented colored fill). Only the REAL hover/pressed states are wired.
//
// FIXED (Login audit 2026-07-14, Docs/TS2_LOGINSCENE_AUDIT.md §4, full re-decompilation of
// Scene_LoginRender 0x51B020): slot 9/12 is wired as HOVER (SetHover), NOT as normal state
// (SetNormal) — proof EA by EA:
//   if ( *(this+3) )                                                   Sprite2D_Draw(pressed)
//   else if ( Sprite2D_HitTest(unk_8E9084,...) && *(this+1)==1 )       Sprite2D_Draw(unk_8E9084)
//   // else (neither pressed nor hovered): NOTHING is drawn for this button.
// The binary therefore NEVER draws unk_8E9084/unk_8E9240 in idle state (cursor elsewhere) —
// only hover and press are visible; the permanent "OK"/"Quitter" label, if it exists, is
// painted into the panel (unk_8E9368) itself, not added by this function. The old code wired
// this sprite via SetNormal(), making it display PERMANENTLY (Button::DrawSkin falls back to
// normal_ whenever hover_/pressed_ are absent) — an extra button fill visible outside
// hover/click, not present in the original screen. Fixed here.
void LoginScene::ApplyConfirmCancelSkin(Button& confirmBtn, Button& cancelBtn) {
    if (gfx::GpuTexture* t = GetAtlasSprite(9))  confirmBtn.SetHover(WidgetSprite(t->Handle()));
    if (gfx::GpuTexture* t = GetAtlasSprite(10)) confirmBtn.SetPressed(WidgetSprite(t->Handle()));
    if (gfx::GpuTexture* t = GetAtlasSprite(12)) cancelBtn.SetHover(WidgetSprite(t->Handle()));
    if (gfx::GpuTexture* t = GetAtlasSprite(13)) cancelBtn.SetPressed(WidgetSprite(t->Handle()));
}

void LoginScene::Update(ts2::Scene scene) {
    if (pending_ != ts2::Scene::None) return; // awaiting consumption by SceneManager
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectUpdate(); break;
    case ts2::Scene::Login:        LoginUpdate();        break;
    case ts2::Scene::CharSelect:   CharSelectUpdate();   break;
    default: break;
    }
}

void LoginScene::Render(ts2::Scene scene) {
    if (!device_) return;
    gfx::SetActiveSprite(&sprites_);
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectRender(); break;
    case ts2::Scene::Login:        LoginRender();        break;
    case ts2::Scene::CharSelect:   CharSelectRender();   break;
    default: break;
    }
    if (noticeOpen_) RenderNotice(); // popup on top (reverse of the dispatch order)
}

void LoginScene::OnMouseDown(ts2::Scene scene, int x, int y) {
    if (pending_ != ts2::Scene::None) return;
    if (noticeOpen_) return; // the notice closes on release (modal behavior)
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectOnMouseDown(x, y); break;
    case ts2::Scene::Login:        LoginOnMouseDown(x, y);        break;
    case ts2::Scene::CharSelect:   CharSelectOnMouseDown(x, y);   break;
    default: break;
    }
}

void LoginScene::OnMouseUp(ts2::Scene scene, int x, int y) {
    if (pending_ != ts2::Scene::None) return;
    if (noticeOpen_) {
        // Faithful to UI_NoticeDlg_OnLButtonUp 0x5C03F0: closes ONLY if the click lands on the
        // OK button (slot 8, at panel+203,+90 — same coords as RenderNotice). A click outside
        // the button leaves the notice open. (Enter/Escape also close it, cf. OnKeyDown.)
        gfx::GpuTexture* panel = GetAtlasSprite(7);
        gfx::GpuTexture* okT   = GetAtlasSprite(8);
        if (panel && panel->Valid() && okT && okT->Valid()) {
            const int pW = static_cast<int>(panel->Width()), pH = static_cast<int>(panel->Height());
            const int nx = screenW_ / 2 - pW / 2, ny = screenH_ / 2 - pH / 2;
            const int okX = nx + 203, okY = ny + 90;
            if (x >= okX && x < okX + static_cast<int>(okT->Width()) &&
                y >= okY && y < okY + static_cast<int>(okT->Height()))
                CloseNotice();
        } else {
            CloseNotice(); // assets unavailable: any click closes it (don't lock the screen)
        }
        return;
    }
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectOnMouseUp(x, y); break; // Scene_ServerSelectOnMouseUp 0x519AC0: confirms exit
    case ts2::Scene::Login:        LoginOnMouseUp(x, y);        break; // OK validates on "up"
    case ts2::Scene::CharSelect:   CharSelectOnMouseUp(x, y);   break; // down-arm/up-validate buttons
    default: break;
    }
}

void LoginScene::OnChar(char c) {
    if (noticeOpen_) return;
    const unsigned int ch = static_cast<unsigned char>(c);
    if (idBox_.Focused())            idBox_.OnChar(ch);
    else if (pwBox_.Focused())       pwBox_.OnChar(ch);
    else if (createNameBox_.Focused()) createNameBox_.OnChar(ch);
}

void LoginScene::OnKeyDown(int vk) {
    if (noticeOpen_) {
        if (vk == VK_RETURN || vk == VK_ESCAPE) CloseNotice();
        return;
    }
    // The focused widget handles its own editing (Backspace/Delete/arrows), Tab
    // (SetOnTab), and Enter (SetOnSubmit).
    if (idBox_.Focused())              idBox_.OnKey(vk);
    else if (pwBox_.Focused())         pwBox_.OnKey(vk);
    else if (createNameBox_.Focused()) createNameBox_.OnKey(vk);
}

// Intro (Scene::Intro) — render only, driven by SceneManager. game::IntroState and its state
// machine (Game/IntroFlow.h) remain owned by SceneManager; LoginScene just reuses the GPU
// resources already created by Init() (sprite batch/font/white texture), all through
// ts2::ui::IntroRender (REAL positions/dimensions extracted from Scene_IntroRender 0x518880,
// cf. UI/IntroRender.h).
void LoginScene::RenderIntro(const game::IntroState& state) {
    if (!device_) return;
    gfx::SetActiveSprite(&sprites_);

    UiContext ctx;
    ctx.sprites  = &sprites_;
    ctx.font     = &font_;
    ctx.whiteTex = whiteTex_;
    ctx.screenW  = screenW_;
    ctx.screenH  = screenH_;

    if (sprites_.Ready()) {
        sprites_.Begin();
        ctx.phase = UiPhase::Panels;
        introRender_.Render(ctx, state);
        sprites_.End();
    }
    if (font_.Ready()) {
        font_.BeginBatch();
        ctx.phase = UiPhase::Text;
        introRender_.Render(ctx, state);
        font_.EndBatch();
    }
}

// EnterWorld (Scene::EnterWorld) — render only, driven by SceneManager, SAME pattern as
// RenderIntro above. game::EnterWorldFlowState and its state machine (Game/EnterWorldFlow.h,
// Scene_EnterWorldUpdate 0x52BFF0) remain owned by SceneManager; LoginScene just reuses the
// GPU resources already created by Init() (sprite batch/font/white texture) via
// ts2::ui::EnterWorldRender (REAL positions/dimensions extracted from Scene_EnterWorldRender
// 0x52C260, cf. UI/EnterWorldRender.h).
void LoginScene::RenderEnterWorld(const game::EnterWorldFlowState& state, int zoneId) {
    if (!device_) return;
    gfx::SetActiveSprite(&sprites_);

    UiContext ctx;
    ctx.sprites  = &sprites_;
    ctx.font     = &font_;
    ctx.whiteTex = whiteTex_;
    ctx.screenW  = screenW_;
    ctx.screenH  = screenH_;

    if (sprites_.Ready()) {
        sprites_.Begin();
        ctx.phase = UiPhase::Panels;
        enterWorldRender_.Render(ctx, state, zoneId);
        sprites_.End();
    }
    if (font_.Ready()) {
        font_.BeginBatch();
        ctx.phase = UiPhase::Text;
        enterWorldRender_.Render(ctx, state, zoneId);
        font_.EndBatch();
    }
}

} // namespace ts2::ui

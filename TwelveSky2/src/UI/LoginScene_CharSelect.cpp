// UI/LoginScene_CharSelect.cpp — CharSelect scene (Scene_CharSelect* : 0x51BD90 / 0x51CED0 /
// 0x520F40 / 0x522E50). Mechanical split of LoginScene.cpp (see LoginScene.cpp for the class
// lifecycle/dispatch and LoginScene.h for the full class declaration).
#include "UI/LoginScene.h"
#include "UI/LoginScene_Internal.h" // kColText, kBtnW/kBtnH (shared by the split family)
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

// Scene 4 — CharSelect. Flow/decisions = Game/CharSelectFlow.h::UpdateCharSelect + the
// OnXxxButtonClicked/ConfirmXxx functions (wired in Init() via SetOnClick, same down-arm/
// up-validate pattern as the Login buttons). LoginScene now only routes mouse/keyboard events
// to this module and draws charState_.

// Scene_CharSelectUpdate 0x51BD90: sub-states Init(30f)->Active (keepalive/30f, entry-into-
// game sequence on the preview timer)->Locked. Fixed dt (30 FPS, cf. CLAUDE.md
// flt_815188=0.033333) — App_FrameTick calls Update() once per frame.
void LoginScene::CharSelectUpdate() {
    ++frame_;
    const game::CharSelectTransition t = game::UpdateCharSelect(charState_, charHost_, 1.0f / 30.0f);
    // [A5/A3] Transition to ServerSelect (scene 2) — TWO producers, both OUTSIDE the tick: the
    // BACK button (Scene_CharSelectOnMouseUp, `this[0]=2` EA 0x525A51) and the OK click of a
    // MODE-2 NoticeDlg (UI_NoticeDlg_OnLButtonUp 0x5C03F0 case 2, `g_SceneMgr=2` EA 0x5C04E4).
    // game::UpdateCharSelect surfaces them AT THE TOP (CharSelectFlow.cpp); without this
    // branch, BOTH were swallowed and the Locked state stayed a PERMANENT dead end. NO
    // notice 44/45: the binary opens none on this path.
    if (t == game::CharSelectTransition::ServerSelect) {
        pending_ = ts2::Scene::ServerSelect; // this[0] = 2 (EA 0x525A51 / 0x5C04E4)
        return;
    }
    if (t == game::CharSelectTransition::EnterWorld) {
        // AUTHENTIC zoneId = the one returned by Net_ReqEnterCharInfo (overrides any local
        // value, cf. CharSelectFlow.h EnterCharInfoResult).
        game::g_World.zoneId = charState_.enterWorldZoneId;
        // Name + local position of the chosen character (CharSelect sheet, cf. Net/
        // CharSelectPackets.h::CharSlotInfo): consumed by the tail72/name13 payload of
        // Net_SendPacket_Op12 (EnterWorld, cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md and
        // Game/GameState.h::SelfState::spawnX/Y/Z for the exact EA provenance).
        if (charState_.enterWorldSlot >= 0 &&
            static_cast<size_t>(charState_.enterWorldSlot) < charState_.slots.size()) {
            const auto& slot = charState_.slots[static_cast<size_t>(charState_.enterWorldSlot)];
            game::g_World.self.localPlayerName = slot.name;
            game::g_World.self.spawnX = slot.localPosX;
            game::g_World.self.spawnY = slot.localPosY;
            game::g_World.self.spawnZ = slot.localPosZ;
        }
        // [A18] Spawn angle: READ from the state, NOT re-drawn here. The `Rng_Next()%360` is
        // INSIDE the entry block of Scene_CharSelectUpdate (EA 0x51c7ed), i.e. BEFORE
        // Net_ReqEnterCharInfo (op22, EA 0x51c81d) and thus BEFORE the 4 op22 nonce draws then
        // the 4 op11 draws: re-drawing it HERE (after the fact) would SHIFT the shared PRNG
        // stream of net::DefaultRng() by one, which changes ALL subsequently emitted network
        // nonces. game::CharSelectState::enterWorldSpawnRotationDeg carries the draw at the
        // right point of the stream (wiring TODO SPAWN_ROT of Game/CharSelectFlow.h:499 —
        // CLOSED here). flt_1675AC4 AND flt_1675AC8 receive the SAME value (EA
        // 0x51c7ed/0x51c7f9): a single field is enough on the C++ side
        // (Net/CharSelectPackets.h::kTail72OffRotA/RotB duplicate it at serialization time).
        game::g_World.self.spawnRotationDeg = charState_.enterWorldSpawnRotationDeg;
        pending_ = ts2::Scene::EnterWorld; // scene_id = 5
    }
}

// [D2] CharSlotRect() was REMOVED (function + declaration): its last caller disappeared once
// the selection hit-test switched to AtlasHitTest(1657, ...), which tests the HIGHLIGHT sprite
// at the binary's exact coordinates (@0x522688) rather than a reconstructed sheet RECT. It also
// carried an INVENTED template (kNominalW=176 / kNominalH=44) used as a fallback — forbidden on
// a pixel-perfect mission. The anchors it documented now live in CharListRender() (origin X =
// nWidth-0xC2 @0x51D4EC, Y0 = 0x13 @0x51D4F5, pitch 44 = `imul .., 2Ch` @0x51D590, offset +0x22
// @0x51D599).

// Reads an int32 from the RAW sheet (net::g_CharRecords[slot] = &unk_1669380 + 0x2768*slot).
// Cf. LoginScene.h for why this exists: game::CharSlotInfo exposes neither +60 nor +5708 (the
// two discriminants of the tier cascades), nor the 11 detail-panel fields.
int32_t LoginScene::CharRecI32(int32_t slot, int byteOffset) {
    if (slot < 0 || slot >= net::kCharRecordCount) return 0;
    if (byteOffset < 0 || byteOffset + static_cast<int>(sizeof(int32_t)) > net::kCharRecordSize)
        return 0;
    int32_t v = 0;
    std::memcpy(&v, net::g_CharRecords[slot] + byteOffset, sizeof(v));
    return v;
}

// [C2] Column of 10 buttons — origin and pitch RE-PROVEN BYTE-EXACT this front:
//   x0 = nWidth - 0x8C (140) @0x51DF12 ; y0 = nHeight - 0x12D (301) @0x51DF20 (written ONCE,
//   @0x51DF17/@0x51DF26, never rewritten in the block).
//   y deltas decoded from bytes: `sub edx,25h` (83 EA 25) @0x51E347 = -37 (slot
//   3086); ENTER +0; `add eax,25h` (83 C0 25) @0x51DFDE = +37; `add eax,4Ah` (83 C0 4A)
//   @0x51E06C = +74; `add eax,6Fh` (83 C0 6F) @0x51E0FA = +111; `add eax,94h`
//   (05 94 00 00 00) @0x51E18C = +148; `add edx,0B9h` @0x51E215 = +185 (BACK);
//   `add ecx,0DEh` @0x51E29F = +222 (QUIT).
//
// ⚠ DIMENSIONS: the invented template kColBtnW=128 / kColBtnH=34 of the old code was REMOVED.
//   The real dimensions live in spr+108/+112, filled at .IMG load time (not determinable
//   statically — spec §13.1): we thus use the NATIVE size of the NORMAL sprite's texture,
//   which is the exact source, available at runtime like in the binary. Missing sprite ->
//   0x0 bounds -> the button can't be clicked, consistent with an unloaded sprite (Sprite2D_
//   HitTest would test a 0x0 rect).
// ⚠ The binary's hit-test targets the NORMAL sprite — hence slots 3086/16/19/22/963/25.
void LoginScene::LayoutCharSelect() {
    const int x0 = screenW_ - 0x8C;  // nWidth  - 140 @0x51DF12
    const int y0 = screenH_ - 0x12D; // nHeight - 301 @0x51DF20
    auto place = [this](Button& b, int slotNormal, int x, int y) {
        int w = 0, h = 0;
        if (gfx::GpuTexture* t = GetAtlasSprite(slotNormal)) {
            w = static_cast<int>(t->Width());
            h = static_cast<int>(t->Height());
        }
        b.SetBounds(x, y, w, h);
    };
    place(restoreBtn_, 3086, x0, y0 - 37);  // EA 0x51E34F (`sub edx,25h` @0x51E347)
    place(enterBtn_,     16, x0, y0);       // EA 0x51DF4E / hit-test 0x51DF53
    place(createBtn_,    19, x0, y0 + 37);  // EA 0x51DFE6 / hit-test 0x51DFEB
    place(deleteBtn_,    22, x0, y0 + 74);  // EA 0x51E074 / hit-test 0x51E079
    place(backBtn_,     963, x0, y0 + 185); // EA 0x51E220 / hit-test 0x51E225
    place(quitBtn_,      25, x0, y0 + 222); // EA 0x51E2AA / hit-test 0x51E2AF
}

// Create-form screen: 5 -/+ pairs (job/faction/face/hair color/variant), typed name,
// Confirm/Cancel — REAL anchors (Scene_CharSelectRender 0x51CED0, panel unk_8EA270 slot 40
// @ (nWidth-0x14F, 0x49) EA 0x51E4A1/0x51E4B0). The -/+ rows are aligned on the drawn VALUES
// (panelY+{78,102,126,150,174}, not +24), minus @panelX+115, plus @panelX+196; Confirm
// @panelX+47 / Cancel @panelX+149, y=panelY+203 (slots 9/12).
void LoginScene::LayoutCreateForm() {
    const int panelX = screenW_ - 335, panelY = 73; // EA 0x51E4A7 (sub 0x14F) / 0x51E4B0 (0x49)
    // -/+ hit-rect dims = NATIVE size of sprite slots 41/42 (unk_8EA304/unk_8EA398) if
    // loadable, else the nominal template -> the click matches the drawn sprite exactly.
    int mw = 20, mh = 20, pw = 20, ph = 20;
    if (gfx::GpuTexture* t = GetAtlasSprite(41)) { if (t->Width() > 0) { mw = static_cast<int>(t->Width()); mh = static_cast<int>(t->Height()); } }
    if (gfx::GpuTexture* t = GetAtlasSprite(42)) { if (t->Width() > 0) { pw = static_cast<int>(t->Width()); ph = static_cast<int>(t->Height()); } }
    const int rowY[5] = { panelY + 78, panelY + 102, panelY + 126, panelY + 150, panelY + 174 };
    auto layoutRow = [&](int row, Button& minus, Button& plus) {
        minus.SetBounds(panelX + 115, rowY[row], mw, mh);
        plus.SetBounds (panelX + 196, rowY[row], pw, ph);
    };
    layoutRow(0, jobMinusBtn_, jobPlusBtn_);
    layoutRow(1, factionMinusBtn_, factionPlusBtn_);
    layoutRow(2, faceMinusBtn_, facePlusBtn_);
    layoutRow(3, hairMinusBtn_, hairPlusBtn_);
    layoutRow(4, variantMinusBtn_, variantPlusBtn_);
    // Name input zone: value drawn at (panelX+0x7F, panelY+0x35) EA 0x51E4F4-0x51E507.
    createNameBox_.SetBounds(panelX + 127, panelY + 53, 150, 20);
    // Confirm (slot 9 unk_8E9084) @(panelX+47, panelY+203); Cancel (slot 12 unk_8E9240)
    // @(panelX+149, panelY+203).
    createConfirmBtn_.SetBounds(panelX + 47,  panelY + 203, kBtnW, kBtnH);
    createCancelBtn_.SetBounds (panelX + 149, panelY + 203, kBtnW, kBtnH);
}

// CharSelect AUDIT STATUS (updated 2026-07-16, front cs-render-2d) — of the 4 gaps the
// 2026-07-14 audit listed here, all are CLOSED except #1:
//  1) 3-BRANCH DISPATCH (`this[+0xF588]` @0x51D4CB: ==1 List 0x51D4E6, ==2 Form
//     0x51E4A1, else 3rd branch 0x51ECC5 = SECONDARY PASSWORD / PIN overlay), driven by
//     dword_16692A4 / this[15375]): STILL OPEN. game::CharSelectScreen only models
//     {List, CreateForm} and game::CharSelectPinState is an inert state — both live in
//     Game/CharSelectFlow.h, OUTSIDE this front. The 3rd branch is therefore unreachable on
//     the C++ side. TODO [0x51ECC5] (cf. CharSelectRenderUi2D).
//  2) 3D PREVIEW: CLOSED. The 4 Char_RenderModel calls (0x51D361/0x51D3CC/0x51D429/0x51D480)
//     are wired in CharSelectRenderPreview3D(), with the previously-missing persistent members
//     (charMesh_/charModels_/charMotions_, cf. LoginScene.h) and gfx::CharPreview3D's
//     appearance resolution. One wiring remains OUTSIDE this front: SceneManager must pass its
//     gfx::Renderer to Init() (wiring TODO CHARSELECT_3D).
//  3) LIST SPRITE SKINNING: CLOSED. Panel 2012, TWO 4-tier cascades (icon AND value),
//     highlight 1657, centered level and name — cf. CharListRender(). No more FillRect or
//     invented template anywhere in scene 4.
//  4) TITLE / "Account: %s": CLOSED — they never existed in the binary and were removed (the
//     function's 40 StrTable005_Get calls are ALL in 0x51E571-0x51FEF7, exclusively for the
//     create-form VALUES). Button labels are GRAPHICAL (atlas textures): no text is drawn over
//     them.
//
// [A1/A13/A16] PAINT ORDER — Scene_CharSelectRender 0x51CED0, EA by EA
// The binary opens and closes TWO distinct 2D batches, with the 3D pass BETWEEN THEM:
//
//   Gfx_Begin2D                                                            @0x51D22D
//     >>> if (this[1] == 0) { Gfx_End2D @0x51D243 ; Gfx_Present @0x51D24D ;
//                             jmp 0x520EC8 /* RETURN */ }                  <<< guard @0x51D238
//     Sprite2D_DrawScaled(atlas + 148*this[15713], 0, 0, nW/1024, nH/768)  @0x51D2AB
//   Gfx_End2D                                     <-- THE 2D CLOSES BEFORE THE 3D  @0x51D2B5
//   --- 3D PASS: 4x Char_RenderModel 0x527020 (0x51D361/0x51D3CC/0x51D429/0x51D480) ---
//   Gfx_Begin2D                                                            @0x51D48A
//     GetPhysicalCursorPos(&Point) @0x51D493 ; ScreenToClient(hWndParent) @0x51D4A4
//     var_1C = Point.x @0x51D4AD ; var_420 = Point.y @0x51D4B3   <- mouse for ALL HitTests
//     --- ALL 2D UI (screen dispatch @0x51D4CB) ---
//   UI_RenderAllDialogs @0x520EAF ; Gfx_End2D @0x520EB9 ; Gfx_Present @0x520EC3
//
// [A1] INIT GUARD (@0x51D238, `cmp dword ptr [edx+4], 0`): during the Init sub-state — the 30
// frames Scene_CharSelectUpdate counts before switching to Active (EA 0x51bde4
// `++this[2] >= 0x1Eu`) — the render draws NOTHING: End2D + Present + early return. This is a
// ONE-SECOND blank screen, to be reproduced as is. The old code showed the list immediately.
// ⚠ Locked sub-state (2): Update is inert BUT the render still draws EVERYTHING (a frozen
//   image) — the guard only tests 0, not `!= 1`. It's a frozen modal, not a dead screen.
void LoginScene::CharSelectRender() {
    // Guard @0x51D238: `[edx+4]` == this[1] == sub-state. ONLY 0 exits.
    if (charState_.subState == game::CharSelectSubState::Init) return; // blank frame (1 s)

    CharSelectRenderBg();        // Begin2D .. fullscreen bg .. End2D  (0x51D22D-0x51D2B5)
    CharSelectRenderPreview3D(); // BETWEEN the two 2D batches            (0x51D2C0-0x51D485)
    CharSelectRenderUi2D();      // Begin2D .. UI .. End2D               (0x51D48A-0x520EB9)
}

// Fullscreen background — Begin2D/End2D ON ITS OWN (0x51D22D .. 0x51D2B5).
// The slot is READ from the state (this[15713]), re-drawn on every Init by the flow: cf. [A4].
void LoginScene::CharSelectRenderBg() {
    if (!sprites_.Ready()) return;
    // FIX (invisible 3D preview): Gfx_Begin2D 0x69E620 @0x69E630 sets ZENABLE=FALSE BEFORE the
    // sprite -> the fullscreen background does NOT write Z. Without this, the background would
    // write Z=near everywhere and OCCLUDE the 3D character drawn right after (depth test).
    // ClientSource used to miss this 2D bracket (gap GX2D-01). Gfx_End2D 0x69E650 @0x69E672
    // restores ZENABLE=TRUE for the 3D pass.
    if (device_) device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE); // 0x69E630
    sprites_.Begin();
    DrawFullscreenBg(charState_.backgroundSlot); // 2383/2384/2385 (EA 0x51C261/70/7F)
    sprites_.End();
    if (device_) device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);  // 0x69E672 (depth ON for the 3D pass)
}

// [A14] The former kCharPreviewEye/kCharPreviewTarget constants were REMOVED: they duplicated
// a truth already proven byte-exact in gfx::CharPreview3D::CameraEye()/CameraTarget()/
// CameraUp() (Gfx/CharPreview3D.h §5 — flt_7EDA1C=5.0f, flt_7A9764=-28.0f, flt_7A8D74=10.0f,
// up=(0,1,0) @0x6A233A). They lost their last reader once CharSelectRenderPreview3D() switched
// to CharPreview3D::BuildViewMatrix(). The binary writes these 6 floats at scene Init on BOTH
// renderer singletons (g_GfxRenderer 0x800130..0x800144 @0x51BDED-0x51BE21, copied into
// g_GxdRenderer 0x18C51C0..0x18C51D4 @0x51BE2C-0x51BE65); on the C++ side these are constants
// consumed at the render point, so the "write on every Init" has no observable effect.
//
// [A16/E1/G1/G2] 3D PASS — the 4 Char_RenderModel 0x527020 calls, BETWEEN Gfx_End2D (0x51D2B5)
// and Gfx_Begin2D (0x51D48A). Screen dispatch @0x51D2C0 (`v108 = this[15714]`):
//   this[15714]==1 (List)    : guard `cmp [eax+0F58Ch], -1 ; jz 0x51D485` @0x51D2ED ->
//                               slot == -1 => BOTH calls SKIPPED (no model).
//                               (pass=1,isCreate=0) @0x51D361 ; (pass=2,isCreate=0) @0x51D3CC
//                               record = &unk_1669380 + 0x2768*this[15715]
//   this[15714]==2 (Create)  : (pass=1,isCreate=1) @0x51D429 ; (pass=2,isCreate=1) @0x51D480
//                               record = &dword_16709B8 (preview sheet)
//   else                      : jmp 0x51D485 (no 3D) — unreachable here, cf. UI2D.
//
// [E1] `pass ∈ {1,2}` MANDATORY (Model_Render @0x40EBD5: `dec eax ; cmp eax,1 ; ja ->
// exit`) and the WHOLE paperdoll is drawn in pass 1 THEN in pass 2 — never the two passes per
// piece. animState = 1 (Idle, `this[15718]=1` @0x51C363) or 3 (Entering, @0x52516F): NEVER 0.
// The ~500-case switch on a8 is DEAD here (a8 = 0 HARDCODED @0x52705F / @0x527544) -> not
// ported. Real scale = 1.0 (the 20.0f of flt_7ED9F8 is the frustum sphere diameter, not a
// scale — cf. Gfx/CharPreview3D.h §5).
//
// [G1] The old TODO "race/gender not yet exposed" was WRONG: the LIST's race is sheet+40
// (`mov eax,[edx+28h]` @0x527536), gender +44 — both are read by
// gfx::CharPreview3D::BuildFromRecord DIRECTLY from the raw sheet net::g_CharRecords[slot],
// so without depending on game::CharSlotInfo.
// [G2] The CREATE preview's weapon comes from the SCENE (this[15716] = variant,
// `mov edx,[ecx+0F590h]` @0x5271B8), NOT from record+216 (which is only written on
// confirmation) — hence `choices.variant = createForm.variant`.
void LoginScene::CharSelectRenderPreview3D() {
    if (!charPreviewReady_ || !charModels_ || !charMotions_) return; // cf. wiring CHARSELECT_3D

    // Pose = the last 5 arguments of Char_RenderModel (this[15717..15724]).
    gfx::CharPreviewPose pose;
    pose.motion    = charState_.previewMotionIndex;                   // this[15717] (+0xF594)
    pose.animState = static_cast<int32_t>(charState_.previewMotion);  // this[15718] (+0xF598)
    pose.animTime  = charState_.previewElapsed;                       // this[15719] (+0xF59C)
    pose.pos[0]    = charState_.previewPos[0];                        // this[15720..22]
    pose.pos[1]    = charState_.previewPos[1];
    pose.pos[2]    = charState_.previewPos[2];
    pose.yawDeg    = charState_.previewRot[1];                        // yaw = this[15724] (+0xF5B0)

    gfx::CharPreviewResult r;
    if (charState_.screen == game::CharSelectScreen::List) {
        // Guard @0x51D2ED — BEFORE any resolution: slot == -1 => nothing at all.
        const int32_t slot = charState_.selectedSlot;
        if (slot < 0 || slot >= net::kCharRecordCount) return;
        // record = &unk_1669380 + 0x2768*slot == net::g_CharRecords[slot] (RAW sheet).
        r = gfx::CharPreview3D::BuildFromRecord(*charModels_, *charMotions_,
                                                net::g_CharRecords[slot], pose);
    } else {
        // Preview sheet dword_16709B8: rebuilt from the form (same fields, cf.
        // Game/CharSelectFlow.h::CharCreateForm and the exhaustive inventory §6.2).
        gfx::CharPreviewChoices c;
        c.job       = charState_.createForm.job;       // +36 — RACE index on this branch (@0x527051)
        c.gender    = charState_.createForm.faction;   // +44 — gender (historical field name kept)
        c.face      = charState_.createForm.face;      // +48
        c.hairColor = charState_.createForm.hairColor; // +52
        c.variant   = charState_.createForm.variant;   // this[15716] (SCENE) — @0x5271B8
        r = gfx::CharPreview3D::BuildFromChoices(*charModels_, *charMotions_, c, pose);
    }
    if (!r.valid) return;

    // View = D3DXMatrixLookAtLH(eye(0,5,-28), target(0,10,0), up(0,1,0)) — the exact operation
    // of Gfx_BeginFrame @0x6A2352 on the camera block written at scene Init (0x800130..0x800144
    // @0x51BDED-0x51BE21, copied into g_GxdRenderer @0x51BE2C-0x51BE65).
    // [A14] Writing these 6 floats "on every Init, on BOTH singletons" is, on the C++ side,
    // without observable effect: these are CONSTANTS (byte-verified, cf. CharPreview3D.h §5)
    // and nothing else reads them. So we set them directly here, at the point of consumption.
    // ⚠ The PROJECTION is NOT touched by scene 4 (the only matrix memcpys of
    // Scene_CharSelectRender, @0x51CF32-0x51CF4D, copy the WORLD 0x800244 and the VIEW
    // 0x800154 into GXD — never the projection): we reuse the one, APPLICATION-side, set at
    // boot by Gfx_InitDevice 0x69B9B0 (D3DXMatrixPerspectiveFovLH @0x69BFC6). It is already
    // carried by gfx::Camera (fovY 45° = kFovDegDefault; near/far = g_GxdRenderer+60/+64) — we
    // instantiate it with DEFAULTS rather than copying literals here, so as not to duplicate a
    // truth that already lives in Gfx/Camera.h.
    D3DXMATRIX view, proj;
    gfx::CharPreview3D::BuildViewMatrix(view);
    const gfx::Camera appCam; // default values = application projection (cf. Gfx/Camera.h)
    appCam.BuildProjMatrix(proj, static_cast<float>(screenW_) / static_cast<float>(screenH_));
    charMesh_.SetCamera(view, proj);

    // The background batch CharSelectRenderBg() (2D sprite path, @0x51D22D-0x51D2B5) just ran
    // JUST BEFORE on the SAME shared device and rebound its own shaders/states there. charMesh_'s
    // LOCAL bind cache (currentPass_) doesn't see this change -> from frame 2 onward it would
    // stay at kPass_SkinnedLit and skip the rebind, drawing the skinned model with the stale 2D
    // shader (cf. banner in Gfx/MeshRenderer.h). Faithful to the binary, where everything goes
    // through the same g_CurrentShaderPass 0x194591C (coming from a 2D pass therefore forces a
    // rebind). We invalidate it.
    charMesh_.InvalidateShaderBindingCache();

    // (The Z-buffer is clean: CharSelectRenderBg just drew the background with ZENABLE=FALSE,
    // so the background did NOT write Z -> the character passes the depth test, cf. fix GX2D-01.)

    // TWO complete paperdoll renders: pass 1 then pass 2 (the binary's 4 call sites are two
    // adjacent pairs, never interleaved per piece).
    gfx::CharPreview3D::Render(charMesh_, r, pose, gfx::MeshRenderer::kDrawPass_Opaque); // pass=1
    gfx::CharPreview3D::Render(charMesh_, r, pose, gfx::MeshRenderer::kDrawPass_Blend);  // pass=2
}

// [A13] Second 2D batch (0x51D48A): hover is recomputed EVERY FRAME from the LIVE PHYSICAL
// cursor position, read AFTER Gfx_Begin2D and BEFORE the screen dispatch (@0x51D4CB). No hover
// index is cached in Update — any caching would diverge.
void LoginScene::CharSelectRenderUi2D() {
    if (charState_.screen == game::CharSelectScreen::CreateForm) CreateFormRender(); // @0x51E4A1
    else                                                          CharListRender();  // @0x51D4E6
    // 3rd dispatch branch (@0x51D4E1 -> 0x51ECC5) = SECONDARY PASSWORD / PIN overlay.
    // UNREACHABLE here: game::CharSelectScreen only models {List, CreateForm} and
    // game::CharSelectPinState is an inert state (opcodes 13/14/15 not wired).
    // TODO [0x51ECC5]: port the PIN assistant (this[15375]/15376/15377, permuted keypad
    // this[15385..15394], sprites unk_93EF4C) — out of scope for this front.
    RenderMsgBox(); // shared modal MsgBox (delete confirmation type 2) drawn last, cf. host.ShowDeleteConfirm
}

// [D1/D2] LIST SCREEN (this[15714] == 1) — Scene_CharSelectRender @0x51D4E6-0x51D7BF
// Origin, LITERAL: X = nWidth - 0xC2 (194) @0x51D4EC ; Y0 = 0x13 (19) @0x51D4F5.
// Background panel: Sprite2D_Draw(unk_931680, X, 19) @0x51D50F — slot 2012
//   ((0x931680 - 0x8E8B50)/148 = 2012, base+stride re-verified this front).
// `for (i=0;i<3;++i)` loop: `cmp [ebp+var_20], 3 ; jge` @0x51D526 -> kMaxCharSlots = 3.
//
// [D1] 🔴 EMPTY SLOT = NOTHING DRAWN. First line of the loop body:
//        `Crt_Strcmp(&unk_1669394 + 0x2768*i, "") ; test eax,eax ; jnz` @0x51D545/0x51D54F
//      -> if the name is empty, `jmp loc_51D51D` = CONTINUE: the `continue` skips the ENTIRE
//      body (no sprite, no text, no frame). Only panel 2012 stays visible behind it. Do NOT
//      "improve" this by drawing an empty-slot placeholder.
//
// Per OCCUPIED slot, FOUR elements (in this order):
//  1. Tier icon @ (X, 19 + 44*i + 0x22) = (X, 53 + 44*i)
//     (`imul eax, 2Ch` @0x51D590 then `lea edx, [ecx+eax+22h]` @0x51D599)
//  2. Selection highlight unk_924944 = slot 1657 @ (X + 0x19, 19 + 44*i + 0x32)
//     = (X+25, 69 + 44*i), ONLY if `i == this[15715]` (`cmp edx,[ecx+0F58Ch]` @0x51D618)
//  3. LEVEL, number CENTERED on x = X + 0x36 (54), y = 19 + 44*i + 0x33 (70+44*i) @0x51D715-0x51D756
//  4. NAME, text CENTERED on x = X + 0x76 (118), same y @0x51D779-0x51D7BA
//
// 🔴 TWO 4-TIER CASCADES — same predicate, DIFFERENT sprite and value. EXACT order (if/
//    else-if), disassembled this front:
//      icon @0x51D556-0x51D60A                     |  value @0x51D642-0x51D712
//      rec[+5708] >= 1 -> unk_985A1C (4343) @0x51D605 | -> "%d" rec[+5708]        @0x51D6FA
//      rec[+60]   >= 1 -> unk_94B504 (2729) @0x51D5E4 | -> "%d" rec[+60]          @0x51D6D4
//      rec[+56]   >= 113 -> unk_9319F8 (2018) @0x51D5C3 | -> "%d" rec[+56] - 112  @0x51D6B1
//      else           -> unk_931714 (2013) @0x51D5A2 | -> "%d" rec[+56]          @0x51D685
//    (113 = `cmp ..., 71h` @0x51D584 ; 112 = `sub edx, 70h` @0x51D6B1 — verified.)
//    ⚠ The old code only ported 2 of the 4 tiers (2018/2013) and rendered the SAME value at
//      both text positions: that was wrong on both counts. The 2nd text slot is NOT a second
//      level — it's the NAME (Crt_StringInit(&String, name) @0x51D771).
//
// ZERO-FALLBACK POLICY: a sprite that fails to load -> nothing drawn (faithful to
// Sprite2D_Draw/EnsureLoaded, which fails silently).
//
// ⚠ ASSUMED AND MEASURED STRUCTURAL DIVERGENCE (paint order): the binary has only ONE loop
// (init `mov [ebp+var_20],0` EA 0x51D514 ; exit `cmp [ebp+var_20],3 ; jge loc_51D7C4` EA
// 0x51D526/0x51D52A ; back edge `jmp loc_51D51D` EA 0x51D7BF) that INTERLEAVES sprites and text
// PER SHEET: icon_0, highlight_0, level_0, name_0, icon_1… Here it is split into two passes
// (sprite batch then font batch). The INTRA-sheet order (highlight_i before text_i) is
// PRESERVED — the only overlap possible at the 44px pitch — but the INTER-sheet order diverges
// (the binary paints text_0 BEFORE icon_1, here icon_1 is painted before text_0). Without
// consequence as long as sprites fit within the 44px pitch (icons y=53+44i, highlight y=69+44i,
// text y=70+44i); dimensions not being statically determinable (spec §13.1), the overlap is
// neither proven nor provable here. To be re-examined if a list sprite exceeds this pitch.
void LoginScene::CharListRender() {
    LayoutCharSelect();
    // [A13] LIVE PHYSICAL cursor position (GetPhysicalCursorPos @0x51D493 +
    // ScreenToClient(hWndParent) @0x51D4A4) — recomputed on EVERY render frame.
    const POINT mp = CharSelectCursorClient();
    createBtn_.OnMouseMove(mp.x, mp.y);
    deleteBtn_.OnMouseMove(mp.x, mp.y);
    enterBtn_.OnMouseMove(mp.x, mp.y);
    backBtn_.OnMouseMove(mp.x, mp.y);
    restoreBtn_.OnMouseMove(mp.x, mp.y);

    constexpr int kPanelTop = 19;         // var_418 = 0x13 @0x51D4F5
    const int panneauX = screenW_ - 194;  // nWidth - 0xC2 @0x51D4EC

    if (sprites_.Ready()) {
        sprites_.Begin();

        // List panel — slot 2012 (unk_931680) @ (X, 19), native blit @0x51D50F.
        if (gfx::GpuTexture* t = GetAtlasSprite(2012))
            sprites_.DrawSprite(t->Handle(), nullptr, panneauX, kPanelTop, gfx::kSpriteWhite);

        for (int i = 0; i < game::kMaxCharSlots; ++i) {
            const game::CharSlotInfo& slot = charState_.slots[static_cast<size_t>(i)];
            if (!slot.occupied) continue;          // [D1] Crt_Strcmp(name,"")==0 -> continue @0x51D54F

            const int yIcon = kPanelTop + 44 * i + 0x22; // 53 + 44*i  @0x51D599
            // 1. Tier icon — 4-tier cascade, EXACT order.
            const int32_t t5708 = CharRecI32(i, 5708); // dword_166A9CC[2768h*i] @0x51D55C
            const int32_t t60   = CharRecI32(i,   60); // dword_16693BC[2768h*i] @0x51D572
            int tierSlot;
            if      (t5708 >= 1)       tierSlot = 4343; // unk_985A1C @0x51D605
            else if (t60   >= 1)       tierSlot = 2729; // unk_94B504 @0x51D5E4
            else if (slot.power >= 113) tierSlot = 2018; // unk_9319F8 @0x51D5C3 (`cmp ..,71h` @0x51D584)
            else                        tierSlot = 2013; // unk_931714 @0x51D5A2
            if (gfx::GpuTexture* t = GetAtlasSprite(tierSlot))
                sprites_.DrawSprite(t->Handle(), nullptr, panneauX, yIcon, gfx::kSpriteWhite);

            // 2. Selection highlight — slot 1657 (unk_924944) @ (X+25, 69+44*i).
            if (i == charState_.selectedSlot) {                     // `cmp edx,[ecx+0F58Ch]` @0x51D618
                if (gfx::GpuTexture* t = GetAtlasSprite(1657))
                    sprites_.DrawSprite(t->Handle(), nullptr,
                                        panneauX + 0x19, kPanelTop + 44 * i + 0x32,
                                        gfx::kSpriteWhite);         // (+25, +50) @0x51D634/0x51D62C
            }
        }
        sprites_.End();
    }

    if (font_.Ready()) {
        font_.BeginBatch();
        for (int i = 0; i < game::kMaxCharSlots; ++i) {
            const game::CharSlotInfo& slot = charState_.slots[static_cast<size_t>(i)];
            if (!slot.occupied) continue;                 // same `continue` @0x51D54F

            const int yText = kPanelTop + 44 * i + 0x33;  // 70 + 44*i  @0x51D723

            // 3. LEVEL — SAME 4-tier cascade as the icon, but on the VALUE.
            const int32_t v5708 = CharRecI32(i, 5708); // @0x51D64B
            const int32_t v60   = CharRecI32(i,   60); // @0x51D661
            char num[16];
            if      (v5708 >= 1) std::snprintf(num, sizeof(num), "%d", v5708);          // @0x51D6FA
            else if (v60   >= 1) std::snprintf(num, sizeof(num), "%d", v60);            // @0x51D6D4
            else if (slot.power >= 113)                               // `cmp ..., 71h` @0x51D673
                std::snprintf(num, sizeof(num), "%d", slot.power - 112);   // `sub edx, 70h` @0x51D6B1
            else
                std::snprintf(num, sizeof(num), "%d", slot.power);         // @0x51D685
            DrawNumberCentered(num, panneauX + 0x36, yText);          // center x = X+54 @0x51D72B

            // 4. NAME — centered text (Crt_StringInit(&String, &unk_1669394+0x2768*i) @0x51D771).
            DrawNumberCentered(slot.name.c_str(), panneauX + 0x76, yText); // x = X+118 @0x51D78F
        }
        font_.EndBatch();
    }

    // LEFT detail panel, then the button column — BINARY ORDER
    // (0x51D7C4 -> 0x51DF0D -> 0x51E4A1): the detail panel is painted BEFORE the buttons.
    CharDetailPanelRender();
    CharButtonColumnRender();
}

// [B2] LEFT DETAIL PANEL — @0x51D7C4-0x51DF0D, ABSOLUTE origin (15, 19)
// 🔴 GUARD MISSING FROM THE CONSOLIDATED SPEC §8.3, re-proven here by disassembly:
//    `cmp dword ptr [ecx+0F58Ch], 0FFFFFFFFh ; jz loc_51DF0D` @0x51D7CA
//    -> NO slot selected => the WHOLE panel is skipped (jumps straight to the button
//       column). The panel is therefore NOT a permanent decoration.
// Origin: var_4 = 0Fh (15) @0x51D7D7 ; var_418 = 13h (19) @0x51D7DE.
// Indexed by this[15715] (`imul eax, 2768h` @0x51D7F4) — this is NOT the 3-slot list.
// NO hit-test in the whole block: purely decorative.
//
// Background: SAME 4-tier cascade as the list, DIFFERENT sprites (@0x51D7FA-0x51D88E):
//   rec[+5708] >= 1  -> unk_985988 = slot 4342 @0x51D889
//   rec[+60]   >= 1  -> unk_94B3DC = slot 2727 @0x51D872
//   rec[+56]   >= 113 -> unk_90E6E0 = slot 1044 @0x51D85B
//   else             -> unk_8E93FC = slot   15 @0x51D844
// Then, all centered via UI_MeasureNumberText/UI_DrawNumberValue:
//   NAME  @ x = 15 + 0x77 (134), y = 19 + 0x20 (51)   @0x51D8CA / 0x51D8C3
//   LEVEL (SAME 4-tier cascade) @ x = 15 + 0x75 (132), y = 19 + 0x35 (72) @0x51DA1B/0x51DA14
//   then 11 fields @ x = 132, y = 19 + 0x48 + 19*k = 91, 110, ..., 281 (pitch 19)
//     (1st field byte-verified: `add eax, 48h` @0x51DA80 ; `add esi, 75h` @0x51DA87 ;
//      source `mov ecx, ds:dword_1669390[eax]` @0x51DA5D = sheet +16.)
void LoginScene::CharDetailPanelRender() {
    // Guard @0x51D7CA — BEFORE any drawing.
    const int32_t sel = charState_.selectedSlot;
    if (sel < 0 || sel >= game::kMaxCharSlots) return;
    const game::CharSlotInfo& rec = charState_.slots[static_cast<size_t>(sel)];
    const int32_t d5708 = CharRecI32(sel, 5708); // dword_166A9CC[2768h*sel] @0x51D7FA
    const int32_t d60   = CharRecI32(sel,   60); // dword_16693BC[2768h*sel] @0x51D815

    constexpr int kX = 15; // var_4   = 0Fh @0x51D7D7
    constexpr int kY = 19; // var_418 = 13h @0x51D7DE

    if (sprites_.Ready()) {
        sprites_.Begin();
        int bgSlot;
        if      (d5708 >= 1)       bgSlot = 4342; // unk_985988 @0x51D889
        else if (d60   >= 1)       bgSlot = 2727; // unk_94B3DC @0x51D872
        else if (rec.power >= 113) bgSlot = 1044; // unk_90E6E0 @0x51D85B (`cmp ..,71h` @0x51D830)
        else                       bgSlot = 15;   // unk_8E93FC @0x51D844
        if (gfx::GpuTexture* t = GetAtlasSprite(bgSlot))
            sprites_.DrawSprite(t->Handle(), nullptr, kX, kY, gfx::kSpriteWhite);
        sprites_.End();
    }

    if (!font_.Ready()) return;
    font_.BeginBatch();

    // Selected character's NAME — centered @ (15+119, 19+32) = (134, 51).
    DrawNumberCentered(rec.name.c_str(), kX + 0x77, kY + 0x20); // @0x51D8CA / 0x51D8C3

    char buf[32];
    // LEVEL — SAME 4-tier cascade (@0x51D90C-0x51DA09), centered @ (15+117, 19+53).
    if      (d5708 >= 1)       std::snprintf(buf, sizeof(buf), "%d", d5708);
    else if (d60   >= 1)       std::snprintf(buf, sizeof(buf), "%d", d60);
    else if (rec.power >= 113) std::snprintf(buf, sizeof(buf), "%d", rec.power - 112);
    else                       std::snprintf(buf, sizeof(buf), "%d", rec.power);
    DrawNumberCentered(buf, kX + 0x75, kY + 0x35); // (132, 72) @0x51DA1B / 0x51DA14

    // The 11 numeric fields: SAME x (132), y = 91 + 19*k.
    // 🔴 UNPROVEN SEMANTICS for about half of them — the OFFSETS and POSITIONS ARE proven (1st
    // field byte-verified above; the rest follow the same pattern at the 19px pitch).
    // game::CharSlotInfo (Game/CharSelectFlow.h, OUTSIDE this front) exposes NONE of these
    // fields: they live in the raw sheet net::g_CharRecords[sel], read here directly — the same
    // source as the binary, which indexes these same globals flatly.
    // TODO [0x51DA5D..0x51DEFF]: name +5484/+5488 (written by Pkt_CharStatDelta 0x46712C/
    // 0x467101), +5432 (string, "clan name" = unproven INFERENCE), +5708 ("2nd rebirth tier" =
    // inference by symmetry). Do NOT invent a label: the binary draws none (they're painted
    // into the panel texture).
    if (sel >= net::kCharRecordCount) { font_.EndBatch(); return; }
    // Offsets in the sheet, in draw order (increasing y). -1 = special case handled in the
    // loop (string, or a packed field read twice).
    constexpr int kDetailFieldOffsets[11] = {
        16,   // y=91  — g_Currency 0x1673180 (currency/gold)   `mov ecx, ds:dword_1669390[eax]` @0x51DA5D
        5484, // y=110 — dword_16746DC, UNKNOWN semantics (written by Pkt_CharStatDelta 0x46712C)
        5488, // y=129 — dword_16746E0, UNKNOWN semantics (written by Pkt_CharStatDelta 0x467101)
        100,  // y=148 — g_SkillPointPool 0x16731D4
        -1,   // y=167 — +5432 (dword_16746A8): STRING ("clan/team name" = INFERENCE)
        88,   // y=186 — g_MeridianPts_RatingMin
        92,   // y=205 — g_MeridianPts_RatingMax
        5568, // y=224 — g_MeridianPts_ExtAtk
        5572, // y=243 — g_MeridianPts_Defense
        -1,   // y=262 — +9408 (g_MeridianHpMpPacked 0x1675630) / 1000
        -1,   // y=281 — +9408 % 1000
    };
    for (int k = 0; k < 11; ++k) {
        const int y = kY + 0x48 + 19 * k; // 91 + 19*k (`add eax, 48h` @0x51DA80 for k=0)
        if (k == 4) {                     // STRING: direct read from the raw sheet
            char s[64] = {0};
            std::memcpy(s, net::g_CharRecords[sel] + 5432, sizeof(s) - 1);
            DrawNumberCentered(s, kX + 0x75, y);
            continue;
        }
        int32_t v;
        if (k == 9)       v = CharRecI32(sel, 9408) / 1000; // packed HP/MP: quotient
        else if (k == 10) v = CharRecI32(sel, 9408) % 1000; // packed HP/MP: remainder
        else              v = CharRecI32(sel, kDetailFieldOffsets[k]);
        std::snprintf(buf, sizeof(buf), "%d", v);
        DrawNumberCentered(buf, kX + 0x75, y);
    }
    font_.EndBatch();
}

// [C2/C1] COLUMN OF 10 BUTTONS — @0x51DF0D-0x51E4A1
// Origin: x0 = nWidth - 0x8C (140) @0x51DF12 ; y0 = nHeight - 0x12D (301) @0x51DF20.
// var_4 and var_418 are written ONCE here (@0x51DF17 / @0x51DF26) and NEVER rewritten in the
// whole block: the first 8 buttons share x0 and only differ by a y0 delta; the LAST two are at
// ABSOLUTE positions.
//
// 🔴 TABLE RE-PROVEN BYTE-EXACT THIS FRONT (get_bytes on each `add/sub`) — the consolidated
//    spec §8.2 table is WRONG on several rows (it placed button 3086 at y0+259 and gave
//    incorrect guards). Actual decoding:
//   dy      | slots N/H/P    | latch    | server guard (this[15374], +0xF038) | sprite EA
//   --------+----------------+----------+--------------------------------------+----------
//   -37     | 3086/3087/3088 | this[10] | == 50 OR == 40 (cf. 🔴 below)         | 0x51E34F
//   (8th painted, AFTER slot 25)        |  (`sub edx, 25h` = 83 EA 25 @0x51E347)|
//     0     | 16/17/18       | this[3]  | —                                    | 0x51DF4E
//   +37     | 19/20/21       | this[4]  | != 60 AND != 50 (@0x51DFA5 / 0x51DFB8) | 0x51DFE6
//           |                |          |  (`add eax, 25h` = 83 C0 25 @0x51DFDE)|
//   +74     | 22/23/24       | this[5]  | != 60  (`cmp ...,3Ch ; jz` @0x51E046) | 0x51E074
//           |                |          |  (`add eax, 4Ah` = 83 C0 4A @0x51E06C)|
//   +111    | 1812/1813/1814 | this[6]  | != 60  (@0x51E0D4)                    | 0x51E102
//           |                |          |  (`add eax, 6Fh` = 83 C0 6F @0x51E0FA)|
//   +148    | 1925/1926/1927 | this[7]  | != 60  (@0x51E162)                    | 0x51E196
//           |                |          |  (`add eax, 94h` = 05 94 00 00 00)    |
//   +185    | 963/964/965    | this[8]  | —      (`cmp [edx+20h],0` @0x51E1FE)  | 0x51E220
//   +222    | 25/26/27       | this[9]  | —      (`cmp [ecx+24h],0` @0x51E288)  | 0x51E2AA
//   ABS(15,332) | 3166/3167/3168 | this[11] | != 60 AND != 40 (@0x51E3B5/0x51E3BE) | 0x51E3E5
//   ABS(15,404) | 3192/3193/3194 | this[12] | == 40 ONLY (@0x51E430)               | 0x51E457
//   (absolute positions byte-verified: `push 14Ch ; push 0Fh` @0x51E3DE/0x51E3E3 and
//    `push 194h ; push 0Fh` = 68 94 01 00 00 / 6A 0F @0x51E450 -> (15,332) and (15,404).)
//
// 🔴 DOUBLE-KILL OF BUTTON #9 (3192): only RENDERED if serverIdx == 40 (@0x51E430) while its
//    handler ABANDONS precisely when serverIdx == 40 (notice 110 @0x525D81) — and even forcing
//    it, the `var_434` guard (invariant 0, cf. Game/CharSelectFlow.h [H1]) leads to notice
//    2248. ENTIRELY DEAD CHAIN: it IS DRAWN (render fidelity) but NO click is wired on it. Same
//    for opcode 24.
//
// SERVER GUARD: this[15374] (+0xF038) is reified NOWHERE on the C++ side (the "flat" 40/50/60
// index is NOT game::ServerSelectState::selectedServer — never guess, cf. rule #8).
// host.GetServerIndex is therefore nullptr -> srv == 0 permanently, which is the SAME state as
// the binary at srv==0: the "!=" guards let 7 buttons through, the "==" guards block (3086
// guarded `==50 || ==40` and 3192 guarded `==40` are NEVER drawn — without consequence, their
// chains being dead). TODO [0x51c09d / 0x51c13f]: reify this[15374] by decompiling its write
// site, then assign charHost_.GetServerIndex.
void LoginScene::CharButtonColumnRender() {
    if (!sprites_.Ready()) return;

    const int x0 = screenW_ - 0x8C;  // nWidth  - 140 @0x51DF12
    const int y0 = screenH_ - 0x12D; // nHeight - 301 @0x51DF20
    const POINT mp = CharSelectCursorClient(); // live per-frame hover (@0x51D493)
    const int32_t srv = charHost_.GetServerIndex ? charHost_.GetServerIndex() : 0;

    sprites_.Begin();
    // dy = 0: ENTER (16/17/18), no guard.
    DrawTriStateSprite(16, x0, y0, enterBtn_.Pressed(), mp.x, mp.y);
    // dy = +37: CREATE (19/20/21), guard != 60 AND != 50.
    if (srv != 60 && srv != 50)
        DrawTriStateSprite(19, x0, y0 + 37, createBtn_.Pressed(), mp.x, mp.y);
    // dy = +74: DELETE (22/23/24), guard != 60.
    if (srv != 60)
        DrawTriStateSprite(22, x0, y0 + 74, deleteBtn_.Pressed(), mp.x, mp.y);
    // dy = +111: slots 1812/1813/1814, guard != 60. Latch this[6] not ported (no Button
    // member) -> drawn in normal/hover state only. TODO [0x525544]: action not ported
    // (RENAME, item ticket 1133 — cf. Net/CharSelectPackets.h::CharItemAction).
    if (srv != 60)
        DrawTriStateSprite(1812, x0, y0 + 111, /*latched=*/false, mp.x, mp.y);
    // dy = +148: slots 1925/1926/1927, guard != 60. Latch this[7] not ported.
    // TODO [0x52B730]: action not ported (Net_ReqStorageList, op25).
    if (srv != 60)
        DrawTriStateSprite(1925, x0, y0 + 148, /*latched=*/false, mp.x, mp.y);
    // dy = +185: BACK (963/964/965), no server guard.
    DrawTriStateSprite(963, x0, y0 + 185, backBtn_.Pressed(), mp.x, mp.y);
    // dy = +222: QUIT (25/26/27), no server guard.
    DrawTriStateSprite(25, x0, y0 + 222, quitBtn_.Pressed(), mp.x, mp.y);
    // dy = -37: RESTORE (3086/3087/3088) — 8th in the binary's paint order (blit EA 0x51E34F,
    // AFTER slot 25 EA 0x51E2AA), NOT 1st: the column is painted 16, 19, 22, 1812,
    // 1925, 963, 25, THEN 3086, 3166, 3192 (increasing addresses 0x51DF4E -> 0x51E457).
    // 🔴 GUARD RE-DISASSEMBLED THIS FRONT (the sense was INVERTED, and the ==40 disjunction lost):
    //   `cmp dword ptr [eax+0F038h], 32h` @0x51E312 ; `jz short loc_51E32A` @0x51E319  (==50 -> DRAW)
    //   `cmp dword ptr [ecx+0F038h], 28h` @0x51E321 ; `jnz short loc_51E3A9` @0x51E328 (!=40 -> SKIP)
    // => draw IFF (srv == 50 || srv == 40). GetServerIndex not being reified (srv==0, cf. TODO
    // above), the binary NEVER draws this button — the old `srv != 50` used to draw it every
    // frame.
    if (srv == 50 || srv == 40)
        DrawTriStateSprite(3086, x0, y0 - 37, restoreBtn_.Pressed(), mp.x, mp.y);
    // ABSOLUTE (15,332): slots 3166/3167/3168, guard != 60 AND != 40. Role UNPROVEN
    // (spec §13.13) -> no click wired. TODO [0x525CA4].
    if (srv != 60 && srv != 40)
        DrawTriStateSprite(3166, 15, 332, /*latched=*/false, mp.x, mp.y);
    // ABSOLUTE (15,404): slots 3192/3193/3194, rendered ONLY if serverIdx == 40.
    // 🔴 DEAD CHAIN (double-kill above): drawn, never clickable.
    if (srv == 40)
        DrawTriStateSprite(3192, 15, 404, /*latched=*/false, mp.x, mp.y);
    sprites_.End();
}

// [C1] Canonical 3-STATE button pattern — CONSECUTIVE slots (n, n+1, n+2), without exception
// across the 10 buttons. Reference EA (ENTER): latch @0x51DF32, hit-test @0x51DF53,
// hover @0x51DF83, normal @0x51DF6C, pressed @0x51DF9A.
// ⚠ THE HIT-TEST TARGETS THE NORMAL SPRITE (base), never the one actually painted.
// ⚠ SIDE EFFECT NOT TO OMIT: both Sprite2D_Draw AND Sprite2D_HitTest call
//   Sprite2D_EnsureLoaded and write `spr+144 = g_GameTimeSec` (@0x4D6B4F / @0x4D6C81) — even a
//   plain hover "touches" the sprite for the atlas LRU. On the C++ side, GetAtlasSprite does
//   exactly that: lazy-loads and keeps the entry resident in atlasCache_ (no eviction) — the
//   observable effect (the sprite stays loaded) is identical.
void LoginScene::DrawTriStateSprite(int slotNormal, int x, int y, bool latched,
                                    int mouseX, int mouseY) {
    int slot;
    if (latched)                                            slot = slotNormal + 2; // pressed
    else if (AtlasHitTest(slotNormal, x, y, mouseX, mouseY)) slot = slotNormal + 1; // hover
    else                                                     slot = slotNormal;     // normal
    if (gfx::GpuTexture* t = GetAtlasSprite(slot))
        sprites_.DrawSprite(t->Handle(), nullptr, x, y, gfx::kSpriteWhite);
}

// Sprite2D_HitTest 0x4D6C50: `ptX >= x && ptX < x + spr[+108] && ptY >= y && ptY < y + spr[+112]`
// (`a4 < *(_DWORD*)(this+108) + a2` @0x4D6CBC) — bounds >= left/top and < right/bottom.
// spr+108/+112 = width/height, filled by the .IMG load: NOT statically determinable
// (spec §13.1). We therefore use the REAL dimensions of the loaded texture — it's the only
// exact source, and it's available at runtime just like in the binary. A non-loadable sprite
// => no rect => false (the binary, for its part, would test 0x0: same result).
bool LoginScene::AtlasHitTest(int slotIndex, int x, int y, int mouseX, int mouseY) {
    gfx::GpuTexture* t = GetAtlasSprite(slotIndex);
    if (!t || t->Width() <= 0 || t->Height() <= 0) return false;
    const int w = static_cast<int>(t->Width());
    const int h = static_cast<int>(t->Height());
    return mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
}

// UI_MeasureNumberText 0x53FCA0 then UI_DrawNumberValue 0x53FCC0, on the bitmap font
// unk_1685740. Centering = idiom `movzx eax,ax ; cdq ; sub eax,edx ; sar eax,1 ; sub esi,eax`
// (@0x51D73F-0x51D747) = `x = centerX - width/2` (signed division by 2).
// TODO [0x53FCC0]: the last argument is LITERALLY 1 at every call site (certain), but its
// meaning is UNKNOWN (color? outline? font?) — not ported.
// TODO [0x1685740]: the bitmap font object is not identified; rendered via gfx::Font, whose
// metrics differ -> centering is NOT guaranteed pixel-exact (spec §13.10).
void LoginScene::DrawNumberCentered(const char* text, int centerX, int y) {
    if (!text || !*text || !font_.Ready()) return;
    font_.DrawTextAt(text, centerX - font_.MeasureText(text) / 2, y, kColText);
}

// GetPhysicalCursorPos(&Point) @0x51D493 — NOT GetCursorPos (which CursorClient() uses for the
// other scenes) — then ScreenToClient(hWndParent 0x815184, &Point) @0x51D4A4.
// GetPhysicalCursorPos is only exposed by the SDK headers under _WIN32_WINNT >= 0x0600: we
// resolve it dynamically from user32 so as not to depend on any solution setting, falling back
// to GetCursorPos (identical outside of virtualized DPI).
POINT LoginScene::CharSelectCursorClient() const {
    POINT p{0, 0};
    using PFN = BOOL (WINAPI*)(LPPOINT);
    static PFN pGetPhysical = []() -> PFN {
        if (HMODULE u = GetModuleHandleA("user32.dll"))
            return reinterpret_cast<PFN>(
                reinterpret_cast<void*>(GetProcAddress(u, "GetPhysicalCursorPos")));
        return nullptr;
    }();
    const BOOL ok = pGetPhysical ? pGetPhysical(&p) : GetCursorPos(&p);
    if (ok && notifyWnd_) ScreenToClient(notifyWnd_, &p); // hWndParent 0x815184
    return p;
}

// Create-form screen — 1:1 WIRING onto the real atlas sprites (front W4-F1,
// Scene_CharSelectRender 0x51CED0, screen==2 branch EA 0x51E4A1). In THIS function: no
// FillRect fill, no fabricated French title/label — the binary draws ONLY the panel (slot 40),
// the -/+ buttons (slots 41/42, PRESSED state only), Confirm/Cancel (slots 9/12), the caret
// (slot 43), and the 5 localized VALUES + the name. No "Create/Name/Class/…" string exists in
// the exe.
// (⚠ this file no longer draws ANY FillRect for the confirmations: the old
//  DeleteConfirmRender/ExitConfirmRender with an invented geometry are replaced by the
//  real-sprite MsgBox UI/ConfirmMsgBox.h.)
//
// 🔴 PAINT ORDER RE-ESTABLISHED THIS FRONT by sweeping the draw sites in range
// [0x51E4A1, 0x51ECC5): (1) panel slot 40 @0x51E4CA; (2) typed NAME @0x51E507;
// (3) caret @0x51E543; (4) the 5 localized values, the LAST @0x51E97E; (5) ONLY THEN the 18
// button blits, from 0x51E9A5 (1st "-") to 0x51ECC0 (right rotation). The buttons are
// therefore painted LAST, ON TOP of the text: a long localized value (verbose language) is
// COVERED by the button, never the other way around. The button batch is for this reason
// REOPENED AFTER the font batch, at the end of the function.
void LoginScene::CreateFormRender() {
    LayoutCreateForm();
    // [A13] Live PHYSICAL cursor (GetPhysicalCursorPos @0x51D493 + ScreenToClient @0x51D4A4),
    // read once per frame at the 2nd Begin2D and shared by ALL hit-tests of scene 4.
    const POINT mp = CharSelectCursorClient();
    Button* formBtns[] = {
        &jobMinusBtn_, &jobPlusBtn_, &factionMinusBtn_, &factionPlusBtn_,
        &faceMinusBtn_, &facePlusBtn_, &hairMinusBtn_, &hairPlusBtn_,
        &variantMinusBtn_, &variantPlusBtn_, &createConfirmBtn_, &createCancelBtn_,
    };
    for (Button* b : formBtns) b->OnMouseMove(mp.x, mp.y);

    // REAL anchors of the create panel (EA 0x51E4A1 sub 0x14F / 0x51E4B0 = 0x49).
    const int panelX = screenW_ - 335, panelY = 73;

    // [G1] The old TODO "3D PREVIEW ... BLOCKED HERE: missing owned 3D members" is REMOVED:
    // LoginScene.h belongs to this front, the persistent members exist
    // (charMesh_/charModels_/charMotions_) and the TWO Char_RenderModel calls of the Create
    // screen (EA 0x51D429 pass=1 / 0x51D480 pass=2, sheet &dword_16709B8) are wired in
    // CharSelectRenderPreview3D() — at the RIGHT point of the paint order, i.e. AFTER the
    // background and BEFORE this 2D batch, never here.
    // ⚠ The fullscreen background is NO LONGER drawn here: it has its OWN Begin2D/End2D
    // (CharSelectRenderBg, 0x51D22D-0x51D2B5), closed BEFORE the 3D pass. Redrawing it in this
    // batch would repaint it ON TOP of the 3D model.

    if (sprites_.Ready()) {
        sprites_.Begin();
        // Create panel (slot 40 unk_8EA270) @ (panelX, panelY) — native blit, EA 0x51E4C5.
        if (gfx::GpuTexture* t = GetAtlasSprite(40))
            sprites_.DrawSprite(t->Handle(), nullptr, panelX, panelY, gfx::kSpriteWhite);
        // Name-field caret (g_SprTextInputCaret = slot 43 unk_8EA42C) @ (panelX+nameWidth+0x80,
        // panelY+0x35), while the field is focused (EA 0x51E50C test g_UIEditBoxMgr==3,
        // Sprite2D_Draw 0x51E543). Text-caret fallback guaranteed by DrawFieldValue (font
        // batch) if the sprite is missing.
        if (createNameBox_.Focused()) {
            if (gfx::GpuTexture* caret = GetAtlasSprite(43)) {
                const std::string nm = createNameBox_.Text();
                const int w = nm.empty() ? 0 : font_.MeasureText(nm.c_str());
                sprites_.DrawSprite(caret->Handle(), nullptr, panelX + w + 128, panelY + 53, gfx::kSpriteWhite);
            }
        }
        sprites_.End();
    }

    if (font_.Ready()) {
        font_.BeginBatch();
        // 5 localized VALUES of the form (Scene_CharSelectRender, range 0x51E548-0x51E93E),
        // CENTERED at x=panelX+0xA3 (=+163) via UI_MeasureNumberText/UI_DrawNumberValue:
        //   Job     (dword_16709DC) : Str(CreateJobLabelStrId)     y=panelY+0x4F (=+79)  EA 0x51E571
        //   Faction (dword_16709E4) : Str(CreateFactionLabelStrId) y=panelY+0x67 (=+103) EA 0x51E60B
        //   Face    (dword_16709E8) : "%c %s" 'A'+face + Str(28)   y=panelY+0x7F (=+127) EA 0x51E690
        //   Hair    (dword_16709EC) : "%c %s" 'A'+hair + Str(28)   y=panelY+0x97 (=+151) EA 0x51E6FD
        //   Variant (this[15716])   : Str(CreateVariantLabelStrId) y=panelY+0xAF (=+175) EA 0x51E76C
        // (helpers CharSelectFlow.h, byte-exact mapping re-verified EA by EA). No more raw
        // "%d", no fabricated title/label French string (the binary draws none).
        const int cx = panelX + 163; // panelX + 0xA3, values' center
        const std::string faceHairWord = game::Str(game::kCreateFaceHairLabelWordStrId);
        auto drawCentered = [&](const std::string& s, int y) {
            font_.DrawTextAt(s.c_str(), cx - font_.MeasureText(s.c_str()) / 2, y, kColText);
        };
        char buf[64];
        drawCentered(game::Str(game::CreateJobLabelStrId(charState_.createForm.job)),        panelY + 79);
        drawCentered(game::Str(game::CreateFactionLabelStrId(charState_.createForm.faction)), panelY + 103);
        std::snprintf(buf, sizeof(buf), "%c %s",
                      game::CreateFaceHairLabelLetter(charState_.createForm.face), faceHairWord.c_str());
        drawCentered(buf, panelY + 127);
        std::snprintf(buf, sizeof(buf), "%c %s",
                      game::CreateFaceHairLabelLetter(charState_.createForm.hairColor), faceHairWord.c_str());
        drawCentered(buf, panelY + 151);
        drawCentered(game::Str(game::CreateVariantLabelStrId(
                         charState_.createForm.job, charState_.createForm.variant)), panelY + 175);
        // Typed name: GetWindowTextA(dword_1668FCC) -> UI_DrawNumberValue @(panelX+0x7F, panelY+0x35)
        // EA 0x51E4DB-0x51E507. Value left-aligned (not centered), (panelX+127, panelY+53).
        DrawFieldValue(createNameBox_, panelX + 127, panelY + 53);
        font_.EndBatch();
    }

    // Paint order: the binary's 18 button blits (EA 0x51E9A5..0x51ECC0) come AFTER the last
    // text (last UI_DrawNumberValue EA 0x51E97E) — they therefore cover any localized value
    // that overflows, never the other way around. Hence this SECOND sprite batch, reopened
    // after the font batch. The 10 -/+ buttons only carry their PRESSED sprite (SetPressed in
    // Init, cf. latch guard EA 0x51E989): at rest, DrawSkin draws NOTHING.
    if (sprites_.Ready()) {
        sprites_.Begin();
        for (Button* b : formBtns) b->DrawSkin(sprites_);
        // Preview ROTATION buttons (EA 0x51EC2F slot 44/45; EA 0x51EC88 slot 46/47), positioned
        // via UI_ProjectSpriteToScreen 0x50F5D0 at world coords (390,628)/(557,628). PRESSED
        // state (idle+1) if the latch is armed (this[15]/this[16]), else idle; NO hover (the
        // binary only tests the latch: guard `cmp [reg+3Ch],0 ; jz` @0x51EC3A / @0x51EC93). The
        // projection ALWAYS uses the IDLE sprite's dims.
        auto drawRot = [&](int idleSlot, int worldX, int worldY, bool latched) {
            gfx::GpuTexture* base = GetAtlasSprite(idleSlot);
            if (!base) return; // sprite not loadable -> nothing (same as AtlasHitTest)
            const POINT a = ui::ProjectDesignAnchor(static_cast<int>(base->Width()),
                                                    static_cast<int>(base->Height()),
                                                    worldX, worldY, screenW_, screenH_);
            gfx::GpuTexture* t = latched ? GetAtlasSprite(idleSlot + 1) : base; // +1 = pressed
            if (t) sprites_.DrawSprite(t->Handle(), nullptr, a.x, a.y, gfx::kSpriteWhite);
        };
        drawRot(44, 390, 628, rotLeftLatched_);  // this[15] @0x51EC2F
        drawRot(46, 557, 628, rotRightLatched_); // this[16] @0x51EC88
        sprites_.End();
    }
}

// Scene_CharSelectOnMouseDown 0x520F40: arms latches (buttons) / selects a slot on direct
// click (the binary also selects on down, cf. header CharSelectFlow.h). Button validation
// (network send) happens on "up" in CharSelectOnMouseUp, same pattern as Login.
void LoginScene::CharSelectOnMouseDown(int x, int y) {
    // [A2] SUB-STATE GUARD — VERY FIRST LINE, BEFORE the modal-confirmation test:
    // `mov eax,[...] ; cmp dword ptr [eax+4], 1 ; jnz -> return` @0x520F4D.
    // The binary returns IMMEDIATELY if this[1] != 1: the mouse is inert during Init (30
    // blank-screen frames) AND during Locked (frozen image). Placing this guard AFTER the
    // `msgBox_.IsOpen()` test would let the modal be clicked in a state where the binary
    // routes nothing.
    if (charState_.subState != game::CharSelectSubState::Active) return;

    if (msgBox_.IsOpen()) { // modal MsgBox (UI_MsgBox_OnLButtonDown 0x5C0980): consumes the click
        msgBox_.OnMouseDown([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    if (charState_.screen == game::CharSelectScreen::CreateForm) {
        LayoutCreateForm();
        createNameBox_.OnMouseDown(x, y);
        jobMinusBtn_.OnMouseDown(x, y);      jobPlusBtn_.OnMouseDown(x, y);
        factionMinusBtn_.OnMouseDown(x, y);  factionPlusBtn_.OnMouseDown(x, y);
        faceMinusBtn_.OnMouseDown(x, y);     facePlusBtn_.OnMouseDown(x, y);
        hairMinusBtn_.OnMouseDown(x, y);     hairPlusBtn_.OnMouseDown(x, y);
        variantMinusBtn_.OnMouseDown(x, y);  variantPlusBtn_.OnMouseDown(x, y);
        createConfirmBtn_.OnMouseDown(x, y); createCancelBtn_.OnMouseDown(x, y);
        // ROTATION buttons (STICKY latches this[15]/this[16], only ARMED — never cleared
        // outside Init): hit-test on the IDLE sprite projected (UI_ProjectSpriteToScreen
        // 0x50F5D0), EA 0x522DB1 (slot 44, world (390,628) -> this[15]=1 @0x522DE7) and EA
        // 0x522E09 (slot 46, world (557,628) -> this[16]=1 @0x522E3F).
        auto rotHit = [&](int idleSlot, int worldX, int worldY) -> bool {
            gfx::GpuTexture* t = GetAtlasSprite(idleSlot);
            if (!t || t->Width() <= 0 || t->Height() <= 0) return false;
            const int w = static_cast<int>(t->Width()), h = static_cast<int>(t->Height());
            const POINT a = ui::ProjectDesignAnchor(w, h, worldX, worldY, screenW_, screenH_);
            return x >= a.x && x < a.x + w && y >= a.y && y < a.y + h;
        };
        if (rotHit(44, 390, 628)) rotLeftLatched_  = true; // @0x522DE7
        if (rotHit(46, 557, 628)) rotRightLatched_ = true; // @0x522E3F
        return;
    }

    LayoutCharSelect();
    // Selection hit-test = the HIGHLIGHT sprite unk_924944 (slot 1657) @
    // (nWidth-194 + 25, 19 + 44*i + 50), NOT the whole sheet: Scene_CharSelectOnMouseDown
    // 0x520F40, EA 0x522688 — SAME coordinates as the CharListRender blit (@0x51D634 /
    // @0x51D62C). The invented fallback (64x28 "if the asset is missing") has been REMOVED:
    // the real dimensions are spr+108/+112, filled at .IMG load time, and a non-loadable
    // sprite must produce NO clickable area (cf. AtlasHitTest).
    for (int i = 0; i < game::kMaxCharSlots; ++i) {
        if (AtlasHitTest(1657, (screenW_ - 194) + 0x19, 19 + 44 * i + 0x32, x, y)) {
            game::SelectCharacterSlot(charState_, i);
            return;
        }
    }
    // RESTORE (slot 3086): the binary carries the SAME server guard BEFORE the hit-test —
    //   `cmp dword ptr [edx+0F038h], 32h` @0x522908 ; `jz short loc_52291D` @0x52290F (==50 -> tests)
    //   `cmp dword ptr [eax+0F038h], 28h` @0x522914 ; `jnz short loc_52295D` @0x52291B (!=40 -> skip)
    // then only `call Sprite2D_HitTest(unk_958368)` @0x522935 and arming the latch
    // `mov [edx+28h], 1` @0x522951. Without it, the button stayed clickable where the binary
    // ignores it entirely (and invisible: cf. the same guard at render time, EA
    // 0x51E312/0x51E321).
    const int32_t srvDown = charHost_.GetServerIndex ? charHost_.GetServerIndex() : 0;
    if (srvDown == 50 || srvDown == 40)
        restoreBtn_.OnMouseDown(x, y);
    createBtn_.OnMouseDown(x, y);
    deleteBtn_.OnMouseDown(x, y);
    enterBtn_.OnMouseDown(x, y);
    backBtn_.OnMouseDown(x, y);
    quitBtn_.OnMouseDown(x, y);    // QUIT (slot 25): latch this[9] (`[ecx+24h]` @0x51E288)
}

// Scene_CharSelectOnMouseUp 0x522E50: carries almost all of the real business logic
// (validation + network requests) — here delegated to the SetOnClick callbacks wired in
// Init() (game::OnXxxButtonClicked/ConfirmXxx).
void LoginScene::CharSelectOnMouseUp(int x, int y) {
    // [A2] SAME guard, SAME position: `cmp dword ptr [eax+4], 1 ; jnz -> return` @0x522E70.
    if (charState_.subState != game::CharSelectSubState::Active) return;

    if (msgBox_.IsOpen()) { // modal MsgBox (UI_MsgBox_OnLButtonUp 0x5C0A90): OK -> action, else close
        msgBox_.OnMouseUp([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    if (charState_.screen == game::CharSelectScreen::CreateForm) {
        jobMinusBtn_.OnMouseUp(x, y);      jobPlusBtn_.OnMouseUp(x, y);
        factionMinusBtn_.OnMouseUp(x, y);  factionPlusBtn_.OnMouseUp(x, y);
        faceMinusBtn_.OnMouseUp(x, y);     facePlusBtn_.OnMouseUp(x, y);
        hairMinusBtn_.OnMouseUp(x, y);     hairPlusBtn_.OnMouseUp(x, y);
        variantMinusBtn_.OnMouseUp(x, y);  variantPlusBtn_.OnMouseUp(x, y);
        createConfirmBtn_.OnMouseUp(x, y); createCancelBtn_.OnMouseUp(x, y);
        return;
    }
    // RESTORE: server guard RE-CHECKED on "up" too (it precedes the hit-test) —
    //   `cmp dword ptr [ecx+0F038h], 32h` @0x525AF5 ; `jz short loc_525B11` @0x525AFC
    //   `cmp dword ptr [edx+0F038h], 28h` @0x525B04 ; `jnz loc_525C3C` @0x525B0B
    // then `cmp dword ptr [eax+28h],0 ; jz loc_525C3C` @0x525B17 (latch required — carried by
    // Button::OnMouseUp, which only validates if armed_) and finally Sprite2D_HitTest
    // @0x525B46. The restoration action itself is outside the ported flow
    // (host.RestoreCharacter unassigned).
    const int32_t srvUp = charHost_.GetServerIndex ? charHost_.GetServerIndex() : 0;
    if (srvUp == 50 || srvUp == 40)
        restoreBtn_.OnMouseUp(x, y);
    createBtn_.OnMouseUp(x, y);
    deleteBtn_.OnMouseUp(x, y);
    enterBtn_.OnMouseUp(x, y);
    backBtn_.OnMouseUp(x, y);
    // QUIT (unk_8E99C4 = slot 25 @ (x0, y0+222)): hit-test EA 0x525AA5 -> if
    // this[+0xF598] != 3 (no entry in progress, guard EA 0x525ABE), UI_MsgBox_Open confirmation
    // (EA 0x525AE5). Now a real latched Button (cf. LoginScene.h): the manual rect hit-test has
    // been removed — it used to short-circuit the latch, so the PRESSED state (slot 27) was
    // never painted. The action remains game::OnQuitButtonClicked (which carries the Entering
    // guard) — wired in Init().
    quitBtn_.OnMouseUp(x, y);
}

// Builds charHost_: the network/UI integration point of Game/CharSelectFlow.h, wired onto the
// REAL builders (Net/CharSelectPackets.h) instead of the old implementation's
// gameHost_/gamePort_ placeholders, which were never populated (cf.
// Docs/TS2_CHARSELECT_AUDIT.md §2.5).
void LoginScene::BuildCharSelectHost() {
    // net::LoadCharacterSlotsFromRecords (Net/CharSelectPackets.h) parses the 3 raw sheets
    // persisted by Net/Login.cpp::LoginRequest (net::g_CharRecords) — wired since session
    // 2026-07-14 (cf. Docs/TS2_LOGINSCENE_AUDIT.md §3.9: the completeness gap that used to
    // leave this blob unused is now closed). Before any successful login, net::g_CharRecords
    // is all zero -> the 3 sheets are empty -> `slots` stays {} (occupied=false everywhere),
    // faithful to the "never a fake occupied slot" behavior the old stub already targeted.
    charHost_.LoadCharacterSlots = &net::LoadCharacterSlotsFromRecords;
    charHost_.AccountKeepAlive = [this]() -> int32_t {
        return net_ ? net::AccountKeepAlive(net_->Client()) : 0;
    };
    // Str_ValidateNameChars(slot name) 0x53FD70 — now wireable: the STORED character name is
    // available via charState_.slots[slot].name (populated by LoadCharacterSlots above).
    // Out-of-bounds -> true (faithful at a minimum, never artificially blocks the flow on an
    // invalid index).
    charHost_.IsCharacterNameValid = [this](int32_t slot) {
        if (slot < 0 || slot >= game::kMaxCharSlots) return true;
        return game::ValidateNameCharset(charState_.slots[static_cast<size_t>(slot)].name);
    };
    charHost_.HasGmAuthLevel       = [] { return net::g_GmAuthLevel >= 1; };
    // g_ServerModeFlag 0x166918C — ALREADY reified in serverModeFlag_ (Init, cf. above): the
    // data was there, and so was the consumer (CharSelectFlow.cpp:224), only the wire was
    // missing. Read by the binary @0x5252C1 (`cmp ds:g_ServerModeFlag, 0` — guard of notice
    // 2163 when the form opens) and @0x51c08d (restore-list entry count). Without this wiring,
    // RecomputeRestoreListCount always took the `else` branch.
    charHost_.IsServerModeFlag = [this] { return serverModeFlag_ != 0; };
    // this[15374] (+0xF038) = "flat" server index (= 5*group + button), written by
    // Scene_ServerSelectOnMouseDown 0x519A36 into the SAME cell as
    // serverState_.selectedServer (confirmed by decompilation). Closes TODO 0x51c09d/0x51c13f
    // without guessing: in SingleServer mode (/0/0/2/…, 1 entry) it's 0 -> the CharSelect
    // ==40/50/60 guards stay inert (faithful). The 40/50/60 divergence only appears with the
    // live multi-group list (separate ServerSelect front).
    charHost_.GetServerIndex = [this]() -> int32_t {
        std::lock_guard<std::mutex> lk(serverMutex_);
        return serverState_.selectedServer;
    };
    // Secondary-password / PIN assistant — reified on the Net/ side this pass (recvBuf+0x95=149
    // / +0x99=153, cf. Net/Login.cpp + Net/NetClient.h). dword_16692A4 != 0 (EA 0x51beae) =>
    // assistant required; Crt_Strcmp(unk_16692A8,"") != 0 (EA 0x51bf3d) => PIN already stored
    // (VERIFY mode). Unblocks the CharSelectFlow PIN flow (PRNG fidelity).
    charHost_.IsSecondaryPasswordRequired = [] { return net::g_SecondaryPwRequired != 0; };
    charHost_.HasStoredSecondaryPassword  = [] { return net::g_StoredSecondaryPw[0] != '\0'; };
    // UI_FocusEditBox 0x50F4A0 (g_UIEditBoxMgr 0x1668FC0) — decompiled: `if (idx < 0x16)
    // { *this = idx; return idx ? SetFocus(hwnd[idx]) : SetFocus(hWndParent); }` — it ONLY
    // does SetFocus + set the focus index. PROVEN indices:
    //   0  = parent window  — `push 0` @0x51BE77 -> call @0x51BE7E (scene Init) and @0x529365
    //   3  = create name    — `push 3` @0x5252FC -> call @0x525303, immediately followed by
    //        SetWindowTextA(dword_1668FCC, "") @0x525314: this is indeed the name EDIT, the
    //        SAME hWnd as the GetWindowTextA of the create-form render (@0x51E4E2).
    //        Independent cross-check: the caret is only painted if `g_UIEditBoxMgr == 3`
    //        (@0x51E50C).
    //   19 = delete by name  — `push 13h` @0x525FCC
    // No mapping is INVENTED for unproven indices: they are ignored.
    charHost_.FocusEditBox = [this](int32_t idx) {
        if (idx == 0) { SetFocus(0); createNameBox_.SetFocused(false); return; } // -> hWndParent
        if (idx == 3) { createNameBox_.SetFocused(true); return; }
        // TODO [anchor 0x525FCC]: index 19 = "delete by name" EDIT, widget not ported.
    };
    charHost_.ShowNotice = [this](int32_t strId) {
        // StrTable005_Get(g_LangId, strId) 0x4C1D20 — game::g_Strings.messages is loaded by
        // App::Init BEFORE any scene (cf. App.cpp::Init, LoadStringTables), so the real text is
        // already available here (no more "#id" fallback — cf. game::Str()).
        OpenNotice(game::Str(strId).c_str());
    };
    // [A3] ShowNoticeTyped — PREFERRED over ShowNotice (CharSelectFlow.h tries it first). The
    // 2nd argument of UI_NoticeDlg_Open 0x5C0280 decides the FATE of the Locked sub-state, via
    // UI_NoticeDlg_OnLButtonUp 0x5C03F0 (routed by UI_RouteLButtonUp 0x5AD0F0, sole xref EA
    // 0x5AD164 — never the scene handlers, which are gated `==1`):
    //   mode 1 (Close)      : plain close, the scene STAYS where it is (recoverable errors,
    //                         e.g. after a successful Net_ReqCancelEnter).
    //   mode 2 (Disconnect) : case 2 @0x5C04C9 -> Net_CloseSocket(&g_NetClient) @0x5C04DF;
    //                         g_SceneMgr=2 @0x5C04E4; g_SceneSubState=0 @0x5C04EE;
    //                         dword_1676188=0 @0x5C04F8.
    //   mode 3 (Quit)       : g_QuitFlag=1 — NEVER used by CharSelect.
    // Without this wiring, the TYPE was LOST and Locked became a PERMANENT DEAD END: Update is
    // inert in sub-state 2 and the scene's 4 mouse handlers refuse everything.
    // (wiring TODO NOTICEDLG_MODE2 of Game/CharSelectFlow.h:37 — CLOSED here.)
    charHost_.ShowNoticeTyped = [this](int32_t strId, game::NoticeType type) {
        noticeType_ = type;
        if (type == game::NoticeType::Disconnect) {
            OpenNotice(game::Str(strId).c_str(), [this] {
                // EXACT effects of case 2, in the binary's order. game::OnNoticeDlgMode2Ok sets
                // CloseConnection + pendingTransition=ServerSelect + subState=Init +
                // frameCounter=0; CharSelectUpdate consumes the transition (cf. [A5/A3]).
                game::OnNoticeDlgMode2Ok(charState_, charHost_);
            });
        } else {
            OpenNotice(game::Str(strId).c_str()); // mode 1: plain close, no effect
        }
    };
    // W5b — WIRING of the "post-login deltas" notice (Net/Login.h::g_LoginNoticeHook). Without
    // this assignment, the hook stayed null across the ENTIRE repo: the notice branch of
    // net::LoginRequest (Net/Login.cpp:277) was DEAD CODE and the notice could NEVER show. In
    // the binary it is UNCONDITIONAL once the delta>0 guard is passed — Net_LoginRequest
    // 0x51B8E0:
    //   StrTable005_Get(g_LangId, 1785) EA 0x51bd68
    //   -> UI_NoticeDlg_Open(byte_18225C8, 1, <text>, "") EA 0x51bd75.
    // Same pattern as charHost_.ShowNotice above (game::Str = faithful StrTable005_Get).
    net::g_LoginNoticeHook = [this](int32_t id) {
        OpenNotice(game::Str(id).c_str());
    };
    // UI_MsgBox_Open(dword_1822438, 2, StrTable005_Get(g_LangId,49), "") @0x5254C4-0x5254DD:
    // opens the shared modal MsgBox (type 2 = deletion). The OK action (UI_MsgBox_OnLButtonUp
    // case 2) = CharSelect_ReqDeleteChar 0x528FD0 -> game::ConfirmDeleteCharacter (opcode 18);
    // Cancel = plain close. The title is game::Str(49) (faithful, no French text in the exe).
    // RESIDUAL GAP [anchor 0x5C08D0/0x5C08DC]: the real UI_MsgBox_Open also DEFOCUSES the name
    // field (Util_SetClampedU8Field(dword_8E714C,0) + UI_FocusEditBox(&g_UIEditBoxMgr,0)); here
    // the field keeps focus under the modal (not reproduced — out of scope for this module).
    charHost_.ShowDeleteConfirm = [this] {
        msgBox_.Open(game::Str(49), 2,
                     [this] { game::ConfirmDeleteCharacter(charState_, charHost_); });
    };
    // Str_ValidateNameChars 0x53FD70, FAITHFUL reproduction: ValidateNameCharset()
    // (Game/CharSelectFlow.cpp) — encoding + length (12 useful characters max, via the
    // original WCHAR[13] buffer) + EXACT character set (0-9/A-Z/a-z/Thai).
    // The "empty name" case is handled SEPARATELY and UPSTREAM by
    // ConfirmCreateCharacter() (notice[38]), faithful to the binary's order.
    charHost_.ValidateNameChars = &game::ValidateNameCharset;
    // maybe_Dict001_MatchWord(g_BannedWordList, ...) 0x4C1410: real filter of the 1432 banned
    // words (001.DAT, G01_GFONT\001.DAT) loaded by App::Init. Safe and behaviorally-equivalent
    // implementation but NOT byte-exact against sub_4C1410 (original sliding window) — cf.
    // BannedWordDict::IsBanned (StringTables.h/.cpp) for the documented gap.
    charHost_.IsNameBanned = [](const std::string& n) {
        return game::g_Strings.bannedWords.IsBanned(n);
    };
    charHost_.GetEditedName = [this] { return createNameBox_.Text(); };
    // Random initial job — PRECISE anchor: Scene_CharSelectOnMouseUp 0x522E50,
    // `call Rng_Next` EA 0x52536F then `cdq ; mov ecx, 3 ; idiv ecx` then
    // `mov ds:dword_16709DC, edx` EA 0x52537C (job = Rng_Next() % 3), on the mouse-up
    // validation of the "Create" button. Single shared RNG stream (cf. bgAtlasSlot_ above):
    // net::DefaultRng(), not std::rand().
    charHost_.RandomInitialJob = [] { return net::DefaultRng().NextMod(3); };

    charHost_.CreateCharacter = [this](int32_t slot, const game::CharCreateForm& form,
                                       int32_t presetId) -> int32_t {
        return net_ ? net::CreateCharacter(net_->Client(), slot, form, presetId)
                    : net::kCharSelectErrSend;
    };
    charHost_.DeleteCharacter = [this](int32_t slot) -> int32_t {
        return net_ ? net::CharSlotAction(net_->Client(), slot, /*action=*/1, /*arg=*/0)
                    : net::kCharSelectErrSend;
    };
    charHost_.RequestEnterCharInfo = [this](int32_t slot) -> game::EnterCharInfoResult {
        if (!net_) {
            game::EnterCharInfoResult r; r.resultCode = net::kCharSelectErrSend; return r;
        }
        return net::ReqEnterCharInfo(net_->Client(), slot);
    };
    charHost_.ConnectToGameServer = [this](int32_t domainId, int32_t gamePort) -> int32_t {
        // Net_SelectServerDomain 0x53FE90 (call EA 0x51c850): translates the domainId received
        // from the server (op22 reply) into a hostname via the hardcoded table
        // (Net/GameServerDomains.h), THEN Net_ConnectGameServer 0x462A70 (EA 0x51c866). The
        // PORT comes from the op22 reply. Fallback (out-of-range index OR the guard OFF) = the
        // host of the selected login channel.
        if (!net_) return net::kNetErrSocketSend;
        std::string fallback = net::kLoginHostCom;
        {
            std::lock_guard<std::mutex> lk(serverMutex_);
            const int idx = serverState_.selectedServer;
            if (idx >= 0 && idx < static_cast<int>(serverState_.servers.size()))
                fallback = serverState_.servers[static_cast<size_t>(idx)].name; // name == host (cf. BuildServerList)
        }
        std::string host = net::SelectGameServerHost(domainId, fallback.c_str()); // 0x53FE90
        return net::ConnectGameServer(net_->Client(), host.c_str(), static_cast<uint16_t>(gamePort), notifyWnd_);
    };
    charHost_.CancelEnter = [this]() -> int32_t {
        return net_ ? net::ReqCancelEnter(net_->Client()) : net::kCharSelectErrSend;
    };
    charHost_.GetEnterPreviewDurationFrames = nullptr; // -> kDefaultEnterPreviewFrames (CharSelectFlow.cpp)
    charHost_.CloseConnectionAndQuit = [this] {
        if (net_) net::NetCloseSocket(net_->Client());
        PostQuitMessage(0); // equivalent to g_QuitFlag=1 (pattern already used, cf. App.cpp VK_ESCAPE)
    };
    // Net_CloseSocket(&g_NetClient) 0x463000 ALONE — the BACK button (EA 0x525A46) and the OK
    // click of the mode-2 NoticeDlg (EA 0x5C04DF). WITHOUT quitting the process (≠
    // CloseConnectionAndQuit).
    charHost_.CloseConnection = [this] {
        if (net_) net::NetCloseSocket(net_->Client());
    };

    // [A15] Clears the 150 latches this[3..152] (+12..+608) on EVERY Init — loop
    // `for (i=0;i<150;++i) this[i+3]=0` @0x51BE83-0x51BEA4 (`cmp var, 96h`). ⚠ THIS IS NOT
    // 10: Scene_CharSelectOnMouseDown arms latches up to this[92]. On the C++ side, latches are
    // carried by this file's Widget instances -> hook. Without it, a button left armed from a
    // previous visit would repaint its PRESSED state on scene re-entry. Only the REIFIED
    // buttons are cleared here (latches this[6]/this[7]/this[11]/this[12] have no widget: their
    // buttons are drawn without a latch, cf. CharButtonColumnRender).
    charHost_.ClearAllButtonLatches = [this] {
        Button* all[] = { &enterBtn_, &backBtn_, &createBtn_, &deleteBtn_, &restoreBtn_,
                          &quitBtn_, &jobMinusBtn_, &jobPlusBtn_, &factionMinusBtn_,
                          &factionPlusBtn_, &faceMinusBtn_, &facePlusBtn_, &hairMinusBtn_,
                          &hairPlusBtn_, &variantMinusBtn_, &variantPlusBtn_,
                          &createConfirmBtn_, &createCancelBtn_ };
        for (Button* b : all) b->Reset(); // armed_ = false ; hover_active_ = false (Widgets.h:223)
        // STICKY rotation latches (this[15]/this[16]) — cleared HERE only (the Init's 150-latch
        // loop @0x51BE83 covers +0x3C/+0x40), never during the Active state.
        rotLeftLatched_ = rotRightLatched_ = false;
    };

    // [A8] Create-preview rotation toggles: this[15] (`cmp [reg+3Ch],0` @0x51CDD0) and
    // this[16] (@0x51CDF1). The flow applies `yaw += 3.0` @0x51CDE8 / `yaw -= 3.0` @0x51CE09 on
    // this[15724], ONLY on the Create screen (already done on the game side,
    // CharSelectFlow.cpp:476-477). WIRED: UI_ProjectSpriteToScreen 0x50F5D0 is now ported
    // (UI/UiProjection.h), the 2 buttons (slots 44/45, 46/47 at world (390,628)/(557,628)) are
    // rendered (CreateFormRender) and hit-tested (CharSelectOnMouseDown), arming these STICKY
    // latches read here.
    charHost_.IsRotateLeftLatched  = [this] { return rotLeftLatched_; };
    charHost_.IsRotateRightLatched = [this] { return rotRightLatched_; };

    // PcModel_ResolveSlotAndApply 0x4E5A00 -> animation FRAME COUNT. The flow calls it with
    // different arguments depending on the screen (LIST: rec+40/rec+44 @0x51c555; CREATE:
    // dword_16709DC(+36)/dword_16709E4(+44) @0x51cd7a) — hence the generic signature. ⚠ SAME
    // MotionCache as the DRAW (cf. LoginScene.h::charMotions_): in the binary it's the SAME
    // g_ModelMotionArray 0x8E8B30 used by both (PcModel_ResolveEquipSlot @0x52705F/0x527544
    // and PcModel_ResolveSlotAndApply @0x51c555). Two caches would diverge on missing motions,
    // and the entry timer (`this[15719] >= duration` @0x51C649) would run against a phantom
    // duration.
    // Motion_BuildPathAndLoad 0x4D7390 case 1: "C%03d%03d%03d.MOTION" % (race+3*gender+1,
    // motion+1, animState+1) == MotionCache::GetForPlayer(race, gender, motion, animState).
    // ⚠ THE `return 0` BELOW DOES NOT TRIGGER ANY FALLBACK — the old comment "-> falls back to
    // kDefaultEnterPreviewFrames" was DOUBLY WRONG: (1) MotionFrameCount tests the PRESENCE of
    // the std::function, not its value, so a hook assigned that returns 0 never falls back to
    // GetEnterPreviewDurationFrames; (2) above all, 0 IS a LEGITIMATE return value of the
    // binary and must NOT be filtered — re-decompiled this front:
    //   PcModel_ResolveSlotAndApply 0x4E5A00 = `Motion_GetFrameCount(PcModel_ResolveEquipSlot(…), a9)`
    //   Motion_GetFrameCount 0x4D7830: `if (!*(this+152) && !Motion_Load(this,a2)) return 0;`
    //                                   (EA 0x4D784B -> 0x4D7854)
    // => on a MISSING/unreadable motion, the binary literally returns 0 and the upstream flow
    // handles it fine (cf. the ⚠ note in Game/CharSelectFlow.cpp::MotionFrameCount). Returning
    // 0 when charMotions_ is null or the palette invalid thus reproduces EXACTLY the binary's
    // "motion not found" case: this is the faithful degradation, not a gap.
    charHost_.GetMotionFrameCount = [this](int32_t race, int32_t gender,
                                           int32_t motion, int32_t animState) -> int32_t {
        if (!charMotions_) return 0; // = the binary's "motion not loadable" case (EA 0x4D7854)
        const gfx::MotionPalette* mp = charMotions_->GetForPlayer(race, gender, motion, animState);
        return (mp && mp->valid) ? mp->frameCount : 0;
    };

    // Weapon_ClassFromField112 0x4CC870 (call EA 0x525156): 1..3 weapon class of the slot's
    // STARTING weapon, hence the entry-animation motion index = 2*class (`shl eax,1`
    // @0x52515B, this[15717]=2*class @0x525163 — the ×2 stays game-side, CharSelectFlow.cpp:1016).
    // Source = item id at sheet+216 (= equip block +104 +112, a2+112 @0x4CC88D), resolved in the
    // item DB mITEM (game::GetItemInfo = MobDb_GetEntry 0x4C3C00) then switch on typeCode+188.
    // This hook used to be nullptr -> previewMotionIndex stayed 0 (motion 0 instead of 2/4/6).
    // Faithful degradation: DB not loaded or item not found -> nullptr -> 0 (like @0x4CC893).
    charHost_.GetEnterPreviewWeaponClass = [this](int32_t slot) -> int32_t {
        if (slot < 0 || slot >= net::kCharRecordCount) return 0;
        const uint32_t weaponId = static_cast<uint32_t>(CharRecI32(slot, 216)); // sheet+216
        const game::ItemInfo* rec = game::GetItemInfo(weaponId);                // 0x4C3C00
        if (!rec) return 0;                                                     // @0x4CC893
        return game::WeaponClassFromTypeCode(rec->typeCode);                    // 0x4CC8BE
    };

    // 🔴 PublishSelfFromSlot — mirrors the SINGLE memcpy @0x51C707:
    //   Crt_Memcpy(g_SelfCharInvBlock /*0x1673170*/, &unk_1669380 + 10088*slot, 0x2768u)
    // This memcpy sets BOTH the inventory block AND g_LocalElement (= block+0x24 =
    // sheet[+36] = the `job` field), which later goes out on the wire at bytes [137..140] of
    // Net_ConnectGameServer 0x462A70's op11 auth packet (EA 0x462D5D).
    // PROVEN ORDER: 0x51C707 (memcpy) ≺ 0x51C81D (op22) ≺ 0x51C850 ≺ op11.
    // This hook was UNWIRED across the ENTIRE repo (GAMEAUTH_Element_Zero defect documented by
    // Game/CharSelectFlow.h:400-405): bytes [137..140] of the handshake used to go out as 0.
    // g_LocalElementSecondary 0x1673198 = sheet +40 (the RACE, only ever filled by the server)
    // — also set, same memcpy.
    charHost_.PublishSelfFromSlot = [this](int32_t slot) {
        if (slot < 0 || slot >= game::kMaxCharSlots) return;
        const game::CharSlotInfo& s = charState_.slots[static_cast<size_t>(slot)];
        game::g_World.self.element          = s.job;  // 0x1673194 = block+0x24 = sheet +36
        game::g_World.self.elementSecondary = s.race; // 0x1673198 = block+0x28 = sheet +40
    };
}

} // namespace ts2::ui

// UI/NpcDialogWindow.cpp — page 0 of `cNpcWin`: the NPC service menu.
// See UI/NpcDialogWindow.h for the full anchoring and the proof that `this+2` is a
// game::NpcDefRecord*.
//
// Include order: Net/ FIRST (NetClient.h pulls <winsock2.h> before <windows.h>) —
// same rule as UI/GuildWindow.cpp.
#include "Net/SendPackets.h"      // net::Net_SendWarpRequest / Net_SendOp116 / Net_SendOp126
#include "Net/NetClient.h"        // net::GlobalNetClient() == &g_NetClient 0x8156A0
#include "UI/NpcDialogWindow.h"
#include "UI/TeleportWindow.h"    // page 76 opened by service code 76 (0x4C)
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h"   // game::g_Client (Var/VarF/msg), game::Str(id)
#include "Game/GameState.h"       // game::g_World.self (level/levelBonus/currency/element)
#include "Game/MapWarp.h"         // game::WarpAddr::*, game::FactionTownNpcId
#include "Game/SkillCombat.h"     // game::Combat_ReadLocalElementPairs (Char_GetPairedElement)
#include "Net/Rng.h"              // net::DefaultRng() (Rng_Next 0x7603FD)

#include <cstdio>                 // std::snprintf (name/label rendering)
#include <cstring>                // strnlen (table fields not guaranteed null-terminated)
#include <string>

namespace ts2::ui {

namespace {

// Panel background (best effort): the real background is the sprite `unk_8F7608` from
// the UI atlas (UI_NpcMenu_Draw 0x5DFC30 @0x5dfdb6 `Sprite2D_Draw(&unk_8F7608, *this, *(this+1))`)
// — atlas slot not resolved on the ClientSource side, cf. dims TODO in the .h. Falls back to kColBg.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_02463.IMG");

// g_SysMsgColor 0x84DFD8 — not modeled as its own field; long-tail via Var(), same
// convention as Game/SocialSystem.cpp:69.
constexpr uint32_t kSysMsgColorAddr = 0x84dfd8u;

// dword_16851B8 — global tested by UI_NpcWin_Open (0x5db946 / 0x5db9ea / 0x5dbc68 / 0x5dbacc):
// when it equals 40, services at slots n=24/28/29/38 are neutralized (code 0) WHILE STILL
// CONSUMING their slot (`++v9` executed on both branches). Exact role not elucidated
// (probably a zone/mode id). Long-tail via Var().
constexpr uint32_t kNpcMenuGateAddr = 0x16851B8u;
constexpr int32_t  kNpcMenuGateOff  = 40;

// `case n -> service code` table from UI_NpcWin_Open 0x5DB530 @0x5db6cc, EXHAUSTIVELY
// recorded from the decompiled switch (EA of each assignment in parentheses). Entries n
// absent from the switch (50, 51, 55, 59, 60, 61, 63, 64 and 76..99) fall through to
// `default: continue` — they consume NO slot. 0 = no service (never produced by this
// table; code 0 only appears via the kNpcMenuGate above).
int32_t ServiceCodeForFlagIndex(int n) {
    switch (n) {
    case  0: return  4; /*0x5db6d9*/  case  1: return  5; /*0x5db6f8*/
    case  2: return  6; /*0x5db717*/  case  3: return  7; /*0x5db736*/
    case  4: return  8; /*0x5db755*/  case  5: return  9; /*0x5db774*/
    case  6: return 10; /*0x5db793*/  case  7: return 11; /*0x5db7b2*/
    case  8: return 12; /*0x5db7d1*/  case  9: return 13; /*0x5db7f0*/
    case 10: return 46; /*0x5dbc29*/  case 11: return 22; /*0x5db907*/
    case 12: return 15; /*0x5db82e*/  case 13: return 16; /*0x5db84d*/
    case 14: return 17; /*0x5db86c*/  case 15: return 41; /*0x5dbb8e*/
    case 16: return 42; /*0x5dbbad*/  case 17: return 18; /*0x5db88b*/
    case 18: return 14; /*0x5db80f*/  case 19: return 20; /*0x5db8c9*/
    case 20: return 21; /*0x5db8e8*/  case 21: return 47; /*0x5dbc48*/
    case 22: return 23; /*0x5db926*/  case 23: return 19; /*0x5db8aa*/
    case 24: return 24; /*0x5db96d — kNpcMenuGate gate*/
    case 25: return 25; /*0x5db98c*/  case 26: return 26; /*0x5db9ab*/
    case 27: return 27; /*0x5db9ca*/
    case 28: return 28; /*0x5dba11 — kNpcMenuGate gate*/
    case 29: return 48; /*0x5dbc8f — kNpcMenuGate gate*/
    case 30: return 29; /*0x5dba30*/  case 31: return 30; /*0x5dba4f*/
    case 32: return 55; /*0x5dbd68*/  case 33: return 40; /*0x5dba6e*/
    case 34: return 54; /*0x5dbd49*/  case 35: return 36; /*0x5dbb12*/
    case 36: return 33; /*0x5dba8d*/  case 37: return 34; /*0x5dbaac*/
    case 38: return 35; /*0x5dbaf3 — kNpcMenuGate gate*/
    case 39: return 37; /*0x5dbb31*/  case 40: return 38; /*0x5dbb50*/
    case 41: return 39; /*0x5dbb6f*/  case 42: return 43; /*0x5dbbcc*/
    case 43: return 44; /*0x5dbbeb*/  case 44: return 45; /*0x5dbc0a*/
    case 45: return 49; /*0x5dbcae*/  case 46: return 50; /*0x5dbccd*/
    case 47: return 51; /*0x5dbcec*/  case 48: return 52; /*0x5dbd0b*/
    case 49: return 53; /*0x5dbd2a*/  case 52: return 58; /*0x5dbda6*/
    case 53: return 59; /*0x5dbdc5*/  case 54: return 56; /*0x5dbd87*/
    case 56: return 61; /*0x5dbde4*/  case 57: return 62; /*0x5dbe03*/
    case 58: return 63; /*0x5dbe22*/  case 62: return 64; /*0x5dbe41*/
    case 65: return 66; /*0x5dbe60*/  case 66: return 67; /*0x5dbe7f*/
    case 67: return 68; /*0x5dbe9e*/  case 68: return 69; /*0x5dbebd*/
    case 69: return 70; /*0x5dbedc*/  case 70: return 71; /*0x5dbefb*/
    case 71: return 72; /*0x5dbf1a*/  case 72: return 73; /*0x5dbf36*/
    case 73: return 75; /*0x5dbf52*/  case 74: return 76; /*0x5dbf6e*/
    case 75: return 77; /*0x5dbf8a*/
    default: return -1;               /*0x5dbf9b: default -> continue, no slot consumed*/
    }
}

// The 4 slots neutralized when dword_16851B8 == 40 (0x5db946/0x5db9ea/0x5dbc68/0x5dbacc).
bool FlagIndexIsGated(int n) { return n == 24 || n == 28 || n == 29 || n == 38; }

// "Arm the warp block then emit Op20" — body LITERALLY identical across the three
// warp handlers of page 0:
//   UI_NpcMenu_CastReturn 0x5E19E0 @0x5e1aa9-0x5e1b4c  (mode 10)
//   UI_ClanWarp_Commit    0x608B30 @0x608c46-0x608ce9  (mode  6)
//   UI_WarpFactionTown    0x608D40 @0x608db4-0x608e57  (mode  6)
// EXACT sequence (same globals, same order):
//   g_MorphInProgress=1 ; dword_1675A8C=mode ; dword_1675A90=0 ; g_TargetZoneId=zone ;
//   Crt_Memset(&dword_1675AA0,0,0x48) ; dword_1675AA0=0 ; dword_1675AA4=1 ; flt_1675AA8=0.0 ;
//   flt_1675AAC/AB0/AB4 = pos ; flt_1675AC4=flt_1675AC8=Rng_Next()%360 ;
//   Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C, zone).
//
// NOTE Crt_Memset(&dword_1675AA0, 0, 0x48) = 72 bytes from 0x1675AA0 (up to 0x1675AE8):
// the g_Client.Var escape hatch is an address->slot map, not a contiguous memory image —
// a range memset is not possible. Only the fields REWRITTEN right after by the binary are
// therefore set (they cover 0x1675AA0..0x1675AC8); the remainder 0x1675ACC..0x1675AE4 is
// modeled by NO WarpAddr field and has NO reader on the ClientSource side -> nothing to clear.
// Same tradeoff (and same rationale) as Game/MapWarp.cpp::ArmFullWarp.
void ArmWarpAndSendOp20(int32_t warpModeCode, int32_t zoneId, const float pos[3]) {
    using namespace ts2::game;
    g_Client.Var (WarpAddr::MorphInProgress) = 1;              // g_MorphInProgress = 1
    g_Client.Var (WarpAddr::WarpModeCode)    = warpModeCode;   // dword_1675A8C
    g_Client.Var (WarpAddr::WarpSub)         = 0;              // dword_1675A90
    g_Client.Var (WarpAddr::WarpTargetNpc)   = zoneId;         // g_TargetZoneId 0x1675A9C
    g_Client.Var (WarpAddr::WarpFlagA0)      = 0;              // dword_1675AA0
    g_Client.Var (WarpAddr::WarpFlagA4)      = 1;              // dword_1675AA4
    g_Client.VarF(WarpAddr::WarpDelay)       = 0.0f;           // flt_1675AA8
    g_Client.VarF(WarpAddr::WarpPosX)        = pos[0];         // flt_1675AAC
    g_Client.VarF(WarpAddr::WarpPosY)        = pos[1];         // flt_1675AB0
    g_Client.VarF(WarpAddr::WarpPosZ)        = pos[2];         // flt_1675AB4
    // flt_1675AC4 = flt_1675AC8 = (float)(Rng_Next() % 360) — a SINGLE draw, copied twice.
    const float facing = static_cast<float>(ts2::net::DefaultRng().NextMod(360));
    g_Client.VarF(WarpAddr::WarpFacingA)     = facing;         // flt_1675AC4
    g_Client.VarF(WarpAddr::WarpFacingB)     = facing;         // flt_1675AC8

    // Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C, zone) — UNCONDITIONAL once the
    // !g_MorphInProgress guard is passed. Emitted via the i32 ALIAS Net_SendWarpRequest and
    // NOT via Net_SendPacket_Op20(int8_t,int8_t): the zones on my paths are 140 (WarpFactionTown
    // element 3), 291 (ClanWarp) and 71/72/73 (CastReturn); 140 and 291 exceed 127 and would
    // be SIGN-extended by the int8_t builder (0x8C -> 0x8CFFFFFF). The binary pushes a2/a3
    // zero-extended over 32 bits. Cf. note Net/SendPackets.h:247-253.
    //
    // The singleton: the binary reads g_NetClient 0x8156A0 as a GLOBAL (Net_SendPacket_Op20
    // 0x4B5000 never receives a socket). On the C++ side it's net::GlobalNetClient(), SET by
    // ConnectLoginServer/ConnectGameServer (Net/Login.cpp:131/313) — the `if (client)` check is
    // a dereference safety net (null until a connection is established; this menu only opens
    // in-game, post-handshake), NOT a masking guard: the path is genuinely reached. Same
    // pattern as Game/MapWarp.cpp:86.
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendWarpRequest(*client, warpModeCode, zoneId);

    // TODO [anchor Motion_GetComboOffsetTable 0x5025E0 / GInfo2_GetVec3 0x4FD4C0]: `pos`
    // stays at {0,0,0} until a faction-town coordinate resolver is wired (NPC/motion .IMG
    // tables, out of scope — cf. game::IFactionTownCoordResolver in Game/MapWarp.h). No
    // effect on the EMISSION (Op20 only carries mode+zone), only on the local staging block.
    // RNG draw order preserved: facing is drawn BEFORE the 4 Op20 nonces.
}

} // namespace

NpcDialogWindow::NpcDialogWindow() {
    // Initial position = center of the reference screen; recomputed on every Recenter()
    // like the binary (UI_NpcMenu_Draw 0x5dfc54/0x5dfc7c).
    x_ = (ts2::kRefWidth  - kPanelW) / 2;
    y_ = (ts2::kRefHeight - kPanelH) / 2;
}

// ============================================================================
// Lifecycle — UI_NpcWin_Open 0x5DB530 / UI_NpcWin_CloseRestore 0x5DC1F0
// ============================================================================
void NpcDialogWindow::Open(const game::NpcDefRecord* npcDef) {
    // 0x5db540: UI_CloseAllDialogs(&dword_1821D4C, 1) — closes ALL other dialogs before
    // opening. TODO [anchor UI_CloseAllDialogs 0x5AC590]: no "close all" equivalent on
    // ts2::ui::UIManager (registry + routers only) and UIManager.h is not owned here.
    // Fidelity gap: another dialog already open stays open.

    Dialog::Open();          // 0x5db553: *(this+3) = 1

    npcDef_ = npcDef;        // 0x5db54d: *(this+2) = *(DWORD*)a2  (cf. proof in the .h)

    // 0x5db55d..0x5db58f: *(this+4..9) = -1 (6 "material being deposited" slots, re-read
    // by UI_NpcWin_CloseRestore 0x5DC1F0 to return items to the inventory). These slots
    // belong to the Craft/Refine pages (not page 0, which deposits nothing) — not modeled
    // here. TODO [anchor 0x5DC1F0]: to be ported by the owning pages' fronts.

    // 0x5db596: for(i<100) *(this+i+70) = 0 — the binary clears 100 latches; only the
    // first 10 are used by page 0 (loops i<10 elsewhere), the other 90 belong to the
    // button latches of other pages.
    for (int i = 0; i < kMaxServices; ++i) pressLatch_[i] = false;

    // 0x5db5c1: for(j<10) *(this+j+170) = 0
    for (int j = 0; j < kMaxServices; ++j) serviceCodes_[j] = 0;

    // 0x5db5ec: for(k<6){ *(this+k+10)=0 ; for(m<9) *(this+9*k+m+16)=0 } — 6x9 grid of the
    // Craft pages (outside page 0), not modeled here.

    serviceCodes_[0] = 1;    // 0x5db652: *(this+170) = 1  (greeting)
    serviceCodes_[1] = 2;    // 0x5db66c: *(this+171) = 2  (expertise)

    // 0x5db680: for(n<100) if (*(DWORD*)(def + 4*n + 1340) == 2 && v9 <= 8) -> switch(n).
    // The `v9 <= 8` guard reserves slot 9 for the "close" service set below.
    const int32_t gate = game::g_Client.VarGet(kNpcMenuGateAddr);
    int v9 = 2;              // 0x5db67d
    for (int n = 0; n < 100; ++n) {
        // fieldG[100] @+1340 == nMenu[n]; "offered" <=> value exactly 2 (0x5db6b5).
        if (!npcDef_ || npcDef_->fieldG[n] != 2 || v9 > 8) continue;
        const int32_t code = ServiceCodeForFlagIndex(n);
        if (code < 0) continue;                       // default -> continue, no slot consumed
        // dword_16851B8 == 40 gate: code forced to 0 BUT the slot is STILL CONSUMED
        // (`++v9` on BOTH branches — 0x5db95f/0x5dba03/0x5dbc81/0x5dbae5).
        serviceCodes_[v9++] = (FlagIndexIsGated(n) && gate == kNpcMenuGateOff) ? 0 : code;
    }

    serviceCodes_[9] = 3;    // 0x5dbfa6: *(this+179) = 3  (close)
    // 0x5dbfb3: *(this+180) = 0 — page 0. This class IS page 0: no `page` field.

    // 0x5dbfbd: for(ii<5) *(this+ii+181) = (ii >= *(DWORD*)(def+32))
    // -> greeting pages beyond fieldA (== nSpeechNum) are pre-marked "already used",
    // so they are never drawn by PickGreeting().
    const uint32_t speechCount = npcDef_ ? npcDef_->fieldA : 0u;
    for (int ii = 0; ii < kGreetingSlots; ++ii)
        greetingUsed_[ii] = (static_cast<uint32_t>(ii) >= speechCount);

    greetingIdx_ = -1;       // 0x5dc00c: *(this+186) = -1

    // 0x5dc01d..0x5dc0a8: tail "a2[3]=1 / a2[4]=0.0 / orient the NPC toward the player
    // (Math_AngleBetween2D, except kinds 63/113/213/313/7) / Fx_MeleeSwingUpdate". Mutates
    // the RENDER ENTITY g_NpcRenderArray, not the window -> belongs to the 3D-picking front
    // (cf. banner in the .h). Deliberately absent here.
}

void NpcDialogWindow::Close() {
    // UI_NpcWin_CloseRestore 0x5DC1F0 — EMITS NOTHING (verified: no Net_Send* in its body;
    // the "close" service (code 3) leads here via UI_NpcMenu_OnLUp_CloseNpcWin 0x5E1980,
    // which is a plain trampoline). Its real job = return up to 6 material slots to the
    // inventory (g_InvMain/g_InvGrid_*/g_InvAux), cGameHud_Hide, and possibly
    // UI_FocusEditBox. TODO [anchor 0x5DC1F0]: material restitution concerns the
    // *(this+4..9) slots of the Craft/Refine pages, not modeled by page 0 (cf. Open()).
    for (int i = 0; i < kMaxServices; ++i) pressLatch_[i] = false;
    Dialog::Close();
}

// ============================================================================
// Geometry — per-frame re-centering (0x5df574 / 0x5df654 / 0x5dfc54)
// ============================================================================
void NpcDialogWindow::Recenter(int screenW, int screenH) {
    // *this      = nWidth/2  - Sprite2D_GetWidth(&unk_8F7608)/2
    // *(this+1)  = nHeight/2 - Sprite2D_GetHeight(&unk_8F7608)/2
    // (sprite dims replaced by kPanelW/kPanelH — cf. TODO in the .h)
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

NpcDialogWindow::Rect NpcDialogWindow::ServiceRowRect(int row) const {
    // Sprite2D_HitTest(&unk_8F7730, *this + 12, *(this+1) + 22*row + 7, mx, my) — 0x5df6fb.
    return { x_ + kRowOffsetX, y_ + kRowPitchY * row + kRowOffsetY, kRowW, kRowH };
}

// ============================================================================
// System messages
// ============================================================================
void NpcDialogWindow::SysMsg(int strId) {
    // Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, strId), g_SysMsgColor).
    // Same convention as Game/SocialSystem.cpp:68-72 (g_SysMsgColor 0x84DFD8 long-tailed).
    const uint32_t sysColor = static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
    game::g_Client.msg.System(game::Str(strId), sysColor);
}

bool NpcDialogWindow::CheckNpcFaction() {
    // `*(DWORD*)(def+1312) - 2 == g_LocalElement` — UI_NpcMenu_CastReturn 0x5e19fe,
    // UI_ClanWarp_Commit 0x608b4e. fieldB == nTribe (cf. banner in the .h).
    // The binary dereferences `def` without a check; def==nullptr can only happen on the
    // C++ side -> explicit refusal (no emission), never a crash.
    if (!npcDef_) return false;
    if (static_cast<int32_t>(npcDef_->fieldB) - 2 == game::g_World.self.element) return true;
    SysMsg(143);   // StrTable005_Get(g_LangId, 143) — 0x5e1a10 / 0x608b60
    return false;
}

// ============================================================================
// Mouse / keyboard events
// ============================================================================
bool NpcDialogWindow::OnMouseDown(int x, int y) {
    // UI_NpcWin_OnLDown_Dispatch 0x5DCB10: `if (!*(this+3)) return 0;` then page 0 ->
    // UI_NpcMenu_OnLDown 0x5DF560. The window is effectively modal: the dispatcher ALWAYS
    // returns 1 while open (result = 1 on every branch, including default).
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x5df574: re-center BEFORE the hit-test

    for (int i = 0; i < kMaxServices; ++i) {
        // 0x5df5db: `if ((int)*(this+i+170) >= 1)` — slots at 0 (dword_16851B8==40 gate)
        // are ignored on down.
        if (serviceCodes_[i] < 1) continue;
        const Rect r = ServiceRowRect(i);
        if (!PointInRect(x, y, r.x, r.y, r.w, r.h)) continue;
        // 0x5df61c: Snd3D_PlayScaledVolume(flt_1487E3C, ..., 0, 100, 1) — menu click sound.
        // TODO [anchor 0x5DF61C] click sound not wired (no audio sink on this front).
        pressLatch_[i] = true;              // 0x5df627: *(this+i+70) = 1
        return true;                         // the binary RETURNS on the first hit (no continuation)
    }
    return true;   // modal: the dispatcher consumes the click even off-row (0x5dcb10 default)
}

bool NpcDialogWindow::OnClick(int x, int y) {
    // UI_NpcWin_OnLUp_Dispatch 0x5DD3B0 -> page 0: UI_NpcMenu_OnLUp 0x5DF640.
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x5df654: re-center BEFORE the hit-test

    for (int i = 0; i < kMaxServices; ++i) {
        // 0x5df6b7: `if (*(this+i+70))` — ONLY the latch matters on up (no `>= 1` test
        // here, unlike down: the latch can only be armed on code >= 1 anyway).
        if (!pressLatch_[i]) continue;
        pressLatch_[i] = false;             // 0x5df6c9: UNCONDITIONAL latch clear
        const Rect r = ServiceRowRect(i);
        // 0x5df6fb: the hit-test is redone on release; if it fails, the loop CONTINUES
        // (the latch stays cleared) — reproduced as-is.
        if (!PointInRect(x, y, r.x, r.y, r.w, r.h)) continue;
        DispatchService(serviceCodes_[i]);  // 0x5df72c: switch(*(this+i+170))
        return true;                         // 0x5df702: `return result` on the first hit
    }
    return true;   // modal (same reason as OnMouseDown)
}

bool NpcDialogWindow::OnKey(int vk) {
    // UI_NpcWin_OnKey 0x5DE030: EMITS NOTHING; adjusts g_Currency/g_InvWeight if
    // *(this+22683)==4 (page 4 only, outside page 0) then UI_NpcWin_CloseRestore. No
    // particular key is filtered on the binary side; Escape = port convention (cf.
    // other Dialog classes).
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { Close(); return true; }
    return false;
}

// ============================================================================
// Service dispatch — switch from UI_NpcMenu_OnLUp 0x5DF640 @0x5df72c
// ============================================================================
void NpcDialogWindow::DispatchService(int code) {
    switch (code) {
    case 1:     PickGreeting();          return;  // 0x5df736 UI_NpcMenu_PickGreeting 0x5DFF00
    case 3:     Close();                 return;  // 0x5df750 UI_NpcMenu_OnLUp_CloseNpcWin 0x5E1980
                                                  //          -> UI_NpcWin_CloseRestore, emits nothing
    case 4:     CastReturn();            return;  // 0x5df75d UI_NpcMenu_CastReturn      0x5E19E0
    case 0x31:  WarpFactionTown();       return;  // 0x5df99d UI_WarpFactionTown         0x608D40
    case 0x35:  ClassChangeValidate();   return;  // 0x5df9d1 UI_ClassChange_Validate    0x60A310
    case 0x37:  SendOp116AndClose();     return;  // 0x5df9eb UI_NpcMenu_..SendOp116AndClose 0x60FA60
    case 0x3E:  FactionAdvanceCommit();  return;  // 0x5dfa39 UI_FactionAdvance_Commit   0x612C20
    case 0x4C:  OpenTeleportPage();      return;  // 0x5dfad4 (case 76) cTeleportWin_Init 0x627BA0

    // ----------------------------------------------------------------------------------
    // Unported EMITTING services — the layout PROOF exists (cf. front report), but the
    // STATE their guards query is not modeled anywhere on the ClientSource side. Emitting
    // without the guard would be worse than not emitting (packet sent where the binary
    // refuses): we abstain explicitly rather than guess. NO builder is missing.
    // ----------------------------------------------------------------------------------
    case 9:
        // TODO [anchor UI_NpcMenu_RequestJoinFaction 0x5E5680]: 13 cascading guards,
        // including Char_GetElementAffinity / Char_ClassifyAffinityRankA /
        // Char_CompareSkillLoadout / byte_1686334[130*elem+13*i] / g_PartyRosterNames /
        // Char_GetMaxAffinityIndex — none of this state is modeled. Emits NOTHING
        // directly: opens MsgBox type 5 (0x5e5a05), and it's UI_MsgBox_OnLButtonUp
        // 0x5C0A90 case 5 (@0x5C0D99) that emits Net_SendOp37 (@0x5c0dc4) under
        // `g_MorphInProgress!=1 && !g_GmCmdCooldownLatch`. The MsgBox type does not
        // exist on the C++ side (MsgBoxDialog takes a callback), so this wiring belongs
        // to the MsgBox front. Builder Net_SendOp37: PRESENT (SendPackets.h:132).
        return;
    case 0x18:
        // DO NOT PORT — BLOCKED gap (unmodeled state), not "ready to wire". RE-PROVEN in
        // IDA (Warp_ProcessKeyword 0x5F54E0 decompiled): UI_NpcMenu_ExecuteWarp 0x5F5470
        // @0x5f548d = Warp_ProcessKeyword(dword_168618C[g_LocalElement]) ; switch @0x5f5513,
        // ONLY cases 0/2/6/9/12 act (default @0x5f5b2e -> 0). This is NOT a "keyword table"
        // (a prior misreading, now corrected) but a CLAN-WAR TIER LADDER: each branch
        // compares the local clan name dword_16746A8 (WARP-03) against T1 tiers
        // byte_16861C3[52*elem]/unk_16861B6/16861A9/168619C (WARP-01/02) and T2 unk_168626C;
        // success -> Warp_LookupDest 0x5F5B60 then LABEL_59 Warp_SendTeleport(g_LocalElement,
        // pos) @0x5f5aee -> zones {138,139,165,166}. The TRANSPORT is ported
        // (game::Warp_SendTeleport, Game/MapWarp.h:187); the BLOCKER is the STATE:
        // dword_16746A8 and T1/T2 have NO writer on the C++ side (Net/GameHandlers_PartyGuild.cpp
        // / Net/WorldEntityDispatch.cpp, outside my files). Porting now would read EMPTY tables.
        // PROVEN EMISSION RISK (reason for abstention) — case 2, branch @0x5f5740: if
        // T2[elem]=="" and dword_16746B8==0 (rank, never written),
        // Str_NameListContainsMismatch(byte_1822730, 5, dword_16746A8) @0x5f578c (3rd arg =
        // the LOCAL clan name, == "" when clanless) MATCHES as soon as a list slot is empty
        // -> emits Net_SendOp107 @0x5f57ce, a packet the binary does NOT emit in any real state.
        // WARP-09 (to fix on wiring day): Str_NameListContainsMismatch 0x5CCCF0 returns 1 on
        // MATCH (`!Crt_Strcmp(this+13*i+8, a3)` @0x5ccd3a; guard `count>7 -> 0` @0x5ccd07) —
        // the IDA name says the OPPOSITE of its actual behavior.
        return;
    case 0x30:
        // DO NOT PORT — BLOCKED gap (unmodeled clan-name buffers). RE-PROVEN in IDA
        // (UI_ClanWarp_Commit 0x608B30 decompiled): faction guard `*(*(this+2)+1312)-2 ==
        // g_LocalElement` @0x608b4e otherwise str005[143]; THEN clan guard
        // `!Crt_Strcmp(dword_16746A8, &String) || (Crt_Strcmp(byte_1686138,dword_16746A8) &&
        // Crt_Strcmp(byte_1686145, dword_16746A8))` @0x608bd1 otherwise str005[1474].
        // dword_16746A8/byte_1686138/byte_1686145 (clan names; byte_1686145 - byte_1686138 =
        // 13) have NO writer on the C++ side -> the "clan member/officer" guard is not
        // evaluable. Armed body: v8=291 ; Motion_GetComboOffsetTable(g_LocalElement, 291, v7)
        // @0x608c1e WITH fallback GInfo2_GetVec3(flt_1555D08, 291, v7) @0x608c34 (contrast:
        // the paid-teleport page 0x627D50 has NO fallback — case 291 genuinely depends on
        // byte_1686138 vs dword_16746A8, unlike mapIds 313/316/331/334 which resolve a
        // constant); mode 6; Op20(6, 291) @0x608ce9 (zone 291 >= 128 -> i32 alias); `return
        // UI_NpcWin_CloseRestore(this)` @0x608cf1 OUTSIDE the `if (!g_MorphInProgress)`
        // (unconditional close); NO g_GmCmdCooldownLatch latch. Builder Net_SendWarpRequest:
        // PRESENT.
        return;
    case 0x32:
        // TODO [anchor UI_ClanDisband_Commit 0x608EC0]: Net_SendOp79(&g_AutoPlayMgr, 14, v7)
        // (@0x608f5f) under guards `Crt_Strcmp(byte_1686138,dword_16746A8)!=0` -> str005[1492],
        // `dword_16746B8!=0` -> str005[1497], then `g_MorphInProgress!=1 && !g_GmCmdCooldownLatch`.
        // Same unmodeled clan buffers as case 0x30 -> the "I am the clan leader" guard is
        // IMPOSSIBLE to evaluate; emitting a clan disband without it would be an active bug.
        // FIDELITY TO REPRODUCE ON WIRING DAY: `v7` is a `_BYTE[108]` NEVER initialized ->
        // the binary emits 100 bytes of UNINITIALIZED STACK (an origin bug; builder
        // Net_SendOp79(nc, int8_t, const void* payload100) is PRESENT, SendPackets.h:195).
        return;

    default:
        // 0x5df72c: the 60+ other cases lead to the 77 OTHER cNpcWin pages (Appraise 2 /
        // SkillLearn 5 / Refine 6 / CreateClan 7 / Shop 8 / Craft 10 / Stall 12 /
        // Warehouse 0x15 / GemSocket 0x13 / Enchant 0x1E / Guild 0x0F-0x10 / ClanCreate
        // 0x2F / …), owned by other fronts. Code 0 (dword_16851B8==40 gate) also falls
        // here: the binary does `default: return result` doing nothing — identical
        // behavior.
        // TODO [anchor UI_NpcMenu_OnLUp 0x5DF640 @0x5df72c]: delegate each code to the
        // owning front's window once those pages are ported.
        return;
    }
}

// ============================================================================
// Code 76 (0x4C) — switches to page 76 (paid teleportation)
// UI_NpcMenu_OnLUp 0x5DF640 @0x5dfad4 (jumptable 005DF72C case 76) -> cTeleportWin_Init 0x627BA0
// ============================================================================
void NpcDialogWindow::OpenTeleportPage() {
    // The binary does NOT create a new object: cTeleportWin_Init(this) does
    // *(this+180)=76 (@0x627bac) on the SAME cNpcWin object; the OnLDown/OnLUp/Draw
    // dispatchers then route to the cTeleportWin_* handlers and page 0 (this menu) is no
    // longer drawn. On the C++ side, page 0 and page 76 are two separate classes -> we
    // OPEN the TeleportWindow (which clears its 4 latches, = cTeleportWin_Init) and CLOSE
    // this menu, which faithfully reproduces the page swap (the two pages are mutually
    // exclusive in the binary).
    // teleport_ is injected by the host (UI/GameWindows) via SetTeleportWindow; nullptr
    // until the wiring is done -> no-op (neither opening nor premature closing of the menu).
    if (teleport_) {
        teleport_->Open();
        Close();
    }
}

// ============================================================================
// Code 1 — UI_NpcMenu_PickGreeting 0x5DFF00 (LOCAL, emits nothing)
// ============================================================================
void NpcDialogWindow::PickGreeting() {
    // 0x5dff13: `if (*(this+186) != 5)` — once saturated, the function does NOTHING more.
    if (greetingIdx_ == kGreetingSlots) return;

    // 0x5dff1a: looks for the first UNUSED page; i == 5 <=> all consumed.
    int i = 0;
    for (; i < kGreetingSlots; ++i) {
        if (!greetingUsed_[i]) break;       // 0x5dff38
    }
    if (i == kGreetingSlots) {
        greetingIdx_ = kGreetingSlots;      // 0x5dff4f: *(this+186) = 5 (saturation)
        return;
    }
    // 0x5dff74: do { *(this+186) = Rng_Next() % 5; } while (*(this + *(this+186) + 181));
    // Rejection sampling — at least one free page exists (guaranteed by the loop above),
    // so the loop terminates. Reproduced as-is: the NUMBER of draws consumed from the
    // shared RNG depends on rejections, exactly like the binary.
    do {
        greetingIdx_ = static_cast<int32_t>(ts2::net::DefaultRng().NextMod(kGreetingSlots));
    } while (greetingUsed_[greetingIdx_]);
    greetingUsed_[greetingIdx_] = true;     // 0x5dff9c: *(this + *(this+186) + 181) = 1
}

// ============================================================================
// Code 4 — UI_NpcMenu_CastReturn 0x5E19E0 -> Op20(mode 10, zone 71/72/73)
// ============================================================================
void NpcDialogWindow::CastReturn() {
    // 0x5e19fe: faction guard (otherwise str005[143], NO emission).
    if (!CheckNpcFaction()) return;

    // 0x5e1a25..0x5e1a5b: v6 = 71/72/73 depending on g_LocalElement 0/1/2; element 3 -> v6 stays 0.
    int32_t v6 = 0;
    switch (game::g_World.self.element) {
    case 0: v6 = 71; break;   /*0x5e1a49*/
    case 1: v6 = 72; break;   /*0x5e1a52*/
    case 2: v6 = 73; break;   /*0x5e1a5b*/
    default: break;           // element 3: v6 = 0 -> 0x5e1a66 false -> NOTHING (not even CloseRestore)
    }
    if (!v6) return;          // 0x5e1a66: `if (v6)` — otherwise bare `return result`

    // 0x5e1a81: if (!Motion_GetComboOffsetTable(g_LocalElement, v6, v5)) GInfo2_GetVec3(...).
    // TODO [anchor 0x5025E0 / 0x4FD4C0]: tables not wired -> position {0,0,0} (cf. note in
    // ArmWarpAndSendOp20). Does NOT affect the EMISSION.
    float pos[3] = { 0.0f, 0.0f, 0.0f };

    // 0x5e1aa3: `if (!g_MorphInProgress)` — arming guard.
    if (!game::g_Client.VarGet(game::WarpAddr::MorphInProgress)) {
        ArmWarpAndSendOp20(/*warpModeCode=*/10, /*zoneId=*/v6, pos);  // 0x5e1ab3 / 0x5e1b4c
    }
    // 0x5e1b54: CloseRestore is called INSIDE the `if (v6)`, so it ALSO runs when morph
    // blocked the arming (not only after a successful emission). Reproduced as-is.
    Close();
}

// ============================================================================
// Code 0x31 — UI_WarpFactionTown 0x608D40 -> Op20(mode 6, zone 1/6/11/140)
// ============================================================================
void NpcDialogWindow::WarpFactionTown() {
    // WARNING: NO faction guard here: 0x608D40 attacks the switch DIRECTLY (unlike
    // CastReturn/ClanWarp). Not adding one "for consistency" would be an invention.
    // 0x608d61: v4 = 1/6/11/140 depending on g_LocalElement 0/1/2/3 — table IDENTICAL to
    // game::FactionTownNpcId (Game/MapWarp.h:142, switch of Map_BeginWarpToFactionTown
    // 0x55C510), reused as-is. DO NOT delegate to game::BeginWarpToFactionTown: that
    // function arms modes 3/7/11 and carries a "dead" guard, whereas 0x608D40 arms mode 6
    // with no guard — these are two distinct functions that merely share the table.
    const int32_t v4 = game::FactionTownNpcId(game::g_World.self.element);
    if (!v4) return;          // 0x608d8e: `if (v4)` — switch default -> bare `return result`

    // 0x608da2: GInfo2_GetVec3(flt_1555D08, v4, v3) ALONE (no Motion_GetComboOffsetTable
    // here, unlike CastReturn/ClanWarp). Same coordinate-resolution TODO.
    float pos[3] = { 0.0f, 0.0f, 0.0f };

    // 0x608dae: `if (!g_MorphInProgress)`.
    if (!game::g_Client.VarGet(game::WarpAddr::MorphInProgress)) {
        // zone 140 (element 3) >= 128: the i32 alias is MANDATORY (cf. ArmWarpAndSendOp20).
        ArmWarpAndSendOp20(/*warpModeCode=*/6, /*zoneId=*/v4, pos);   // 0x608dbe / 0x608e57
    }
    Close();                  // 0x608e5f: inside `if (v4)`, same remark as CastReturn
}

// ============================================================================
// Code 0x35 — UI_ClassChange_Validate 0x60A310 (local guards -> MsgBox type 46 -> Op79/15)
// ============================================================================
void NpcDialogWindow::ClassChangeValidate() {
    // Three cost/level tiers, tested in THIS order (0x60A310). `g_SelfLevelBonus` =
    // self.levelBonus (0x16731AC), `g_SelfLevel` = self.level (0x16731A8), `g_Currency` =
    // self.currency (0x1673180) — all modeled in game::g_World.self (GameState.h:308/309).
    const auto& self = game::g_World.self;

    // The binary's 3 tests are written in NEGATIVE form; they are restated here in the
    // equivalent positive form, term for term (nothing omitted):
    //   0x60a31e: if (g_SelfLevelBonus <= 0)            -> branch 1 = `levelBonus > 0`
    //   0x60a39b: if (lvl < 113 || lvl > 145 || g_SelfLevelBonus)
    //                                                     -> branch 2 = `113<=lvl<=145 && !levelBonus`
    //   0x60a40d: if (lvl < 100 || lvl > 112)            -> branch 3 = `100<=lvl<=112`
    // WARNING: the `&& levelBonus == 0` term of branch 2 is ESSENTIAL and is NOT
    // redundant with branch 1: `levelBonus < 0` (negative) passes branch 1 (`<= 0` true)
    // then fails branch 2 (`|| g_SelfLevelBonus` true) and thus falls through to branch 3.
    // Without this term, a negative levelBonus would wrongly apply the tier-50 cost. All
    // 3 refusals converge on LABEL_13 (0x60a418) -> str005[965] (VERIFIED: the three
    // `goto LABEL_13` at 0x60a327 / 0x60a3a4 / 0x60a416 share the same block).
    if (self.levelBonus > 0) {
        if (self.currency < 100) { SysMsg(965); return; }   // 0x60a327: early return, NO CloseRestore
        // TODO [anchor 0x60a375]: UI_MsgBox_Open(dword_1822438, 46, str005[1753], str005[1754]).
    } else if (self.level >= 113 && self.level <= 145 && self.levelBonus == 0) {
        if (self.currency < 50)  { SysMsg(965); return; }   // 0x60a3a4
        // TODO [anchor 0x60a3f3]: UI_MsgBox_Open(dword_1822438, 46, str005[1753], str005[1755]).
    } else if (self.level >= 100 && self.level <= 112) {
        if (self.currency < 20)  { SysMsg(965); return; }   // 0x60a416
        // TODO [anchor 0x60a462]: UI_MsgBox_Open(dword_1822438, 46, str005[1753], str005[1756]).
    } else {
        SysMsg(1801); return;                               // 0x60a479: early return, NO CloseRestore
    }

    // TODO [anchor UI_MsgBox_OnLButtonUp 0x5C0A90 case 46 @0x5C18AA]: CONFIRMATION emits
    // Net_SendOp79(&g_AutoPlayMgr, 15, var_70) (@0x5c18c4) WITH NO guard at all, where
    // var_70 is a `_BYTE[100]` ZEROED by Crt_Memset(v49,0,0x64) (@0x5c0acf) — unlike the
    // UNINITIALIZED payload of the Op79/14 in case 0x32. Builder Net_SendOp79: PRESENT
    // (SendPackets.h:195). The MsgBox `type` does not exist on the C++ side (MsgBoxDialog
    // = title/body/callback): this wiring belongs to the MsgBox front, my handler stops
    // at opening — matches the binary, which also emits nothing inside 0x60A310.

    // 0x60a493: all 3 branches fall through to `return UI_NpcWin_CloseRestore(this)`.
    Close();
}

// ============================================================================
// Code 0x37 — UI_NpcMenu_OnLUp_SendOp116AndClose 0x60FA60 -> Op116
// ============================================================================
void NpcDialogWindow::SendOp116AndClose() {
    // 0x60FA60: NO guard. Net_SendOp116(&g_AutoPlayMgr) then UI_NpcWin_CloseRestore.
    // UNCONDITIONAL emission via the global singleton (cf. note in ArmWarpAndSendOp20).
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendOp116(*client);        // 0x60fa6c
    Close();                                      // 0x60fa79
}

// ============================================================================
// Code 0x3E — UI_FactionAdvance_Commit 0x612C20 -> Op126(1)
// ============================================================================
void NpcDialogWindow::FactionAdvanceCommit() {
    // 0x612c29: switch(g_SelfMorphNpcId) — ONLY cases 1/6/11/37/140 exist, `default`
    // returns without doing anything. g_SelfMorphNpcId 0x1675A98 -> long-tailed
    // (WarpAddr::SelfMorphNpcId, same convention as everywhere else in ClientSource).
    const int32_t morphNpc = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId);
    const int32_t elem     = game::g_World.self.element;
    // Char_GetPairedElement(g_LocalPlayerSheet, k) == Combat_ReadLocalElementPairs().Paired(k)
    // (Game/SkillCombat.h:93-136 — snapshot of g_AlliancePairTable via g_Client.VarGet).
    const game::ElementPairTable pairs = game::Combat_ReadLocalElementPairs();

    bool allowed;
    switch (morphNpc) {
    case 1:   allowed = (elem == 0 || elem == pairs.Paired(0)); break; /*0x612c73: `!g_LocalElement ||`*/
    case 6:   allowed = (elem == 1 || elem == pairs.Paired(1)); break; /*0x612cbb*/
    case 11:  allowed = (elem == 2 || elem == pairs.Paired(2)); break; /*0x612d03*/
    case 37:  allowed = true;                                   break; /*0x612c51: unconditional goto LABEL_15*/
    case 140: allowed = (elem == 3 || elem == pairs.Paired(3)); break; /*0x612d47*/
    default:  return;                                                  /*0x612c51: default -> return result*/
    }
    if (!allowed) { SysMsg(143); return; }        // LABEL_14 @0x612d5a: str005[143]

    // LABEL_15 @0x612d79: `if (g_SelfLevel >= 113)`.
    if (game::g_World.self.level < 113) { SysMsg(879); return; }  // 0x612d8c: str005[879]

    // 0x612da5: Net_SendOp126(&g_AutoPlayMgr, 1) — the `1` is a LITERAL constant.
    // WARNING: NO CloseRestore in this function (unlike other handlers): the window
    // stays open after the send. Reproduced as-is.
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendOp126(*client, 1);
}

// ============================================================================
// Render — UI_NpcMenu_Draw 0x5DFC30
// ============================================================================
void NpcDialogWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;
    lastScreenW_ = ctx.screenW;             // memorized for cross-frame hit-test re-centering
    lastScreenH_ = ctx.screenH;
    Recenter(ctx.screenW, ctx.screenH);     // 0x5dfc54: Draw re-centers too

    const Rect panel = PanelRect();

    if (ctx.phase == UiPhase::Panels) {
        // 0x5dfdb6: Sprite2D_Draw(&unk_8F7608, *this, *(this+1)) — menu background.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);

        // 0x5dfca8: `if (*(this+186) != -1)` -> Sprite2D_Draw(&unk_8F7574, *this, *(this+1)-160)
        // = the "speech bubble" banner ABOVE the menu, shown ONLY once a greeting has
        // been drawn.
        if (greetingIdx_ != -1) {
            const int bubbleY = panel.y - 160;                    // 0x5dfccc
            const int bubbleH = panel.y - bubbleY;                // TODO [anchor 0x5DFCCC] real
            kPanelBg.Draw(ctx, panel.x, bubbleY, panel.w, bubbleH, kColBg); // dims of unk_8F7574
            ctx.DrawFrame(panel.x, bubbleY, panel.w, bubbleH, kColBorder, 1);
        }

        // 0x5dfdbb: for(i<10) if (*(this+i+170) >= 1) -> Sprite2D_Draw(&g_AssetMgr_UiAtlasSlots
        // + 148*v9, *this+12, *(this+1)+22*i+7), with v9 = UI_NpcMenu_ServiceToStrId(code) + {0
        // idle, +1 on hover (0x5dfe54), +2 if pressed (0x5dfe99)}. Slots with code 0
        // (dword_16851B8==40 gate) are NOT drawn.
        // TODO [anchor UI_NpcMenu_ServiceToStrId 0x5DF160 / g_AssetMgr_UiAtlasSlots 0x8E8B50]:
        // each service label is an ATLAS SPRITE (slot = ServiceToStrId(code)*148), not text
        // — mapping of the 78 codes to slot not ported. Meanwhile, the row is drawn flat
        // with its 3 states, and the numeric code is written in the text phase.
        for (int i = 0; i < kMaxServices; ++i) {
            if (serviceCodes_[i] < 1) continue;
            const Rect r = ServiceRowRect(i);
            const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
            const D3DCOLOR col = pressLatch_[i] ? kColPressed : (hover ? kColHover : kColRowBg);
            ctx.FillRect(r.x, r.y, r.w, r.h, col);
            ctx.DrawFrame(r.x, r.y, r.w, r.h, kColBorder, 1);
        }
        return;
    }

    // --- Text phase ---
    if (greetingIdx_ != -1 && npcDef_) {
        // 0x5dfcea: Crt_Vsnprintf(v7, "%s.....", *(this+2) + 4) then UI_DrawNumberValue(v7,
        // *this+22, *(this+1)-144, 3). `def+4` == NpcDefRecord::name. The "....." suffix is
        // LITERAL in the binary (string aS 0x7BA870).
        char nameBuf[64];
        const std::string npcName(npcDef_->name, strnlen(npcDef_->name, sizeof(npcDef_->name)));
        std::snprintf(nameBuf, sizeof(nameBuf), "%s.....", npcName.c_str());
        ctx.Text(nameBuf, panel.x + kNameOffsetX, panel.y + kNameOffsetY, kColTitle);

        // 0x5dfd2e: `if (*(this+186) != 5)` — once saturated, the name stays but NO
        // greeting line is drawn anymore.
        // 0x5dfd97: for(i<5) UI_DrawNumberValue(51*i + def + 255*(*(this+186)) + 36,
        //                                         *this+22, *(this+1) + 20*i - 121, 1)
        //          == def->textGrid[greetingIdx_][i]  (textGrid @+36, page=255 bytes, line=51 bytes).
        if (greetingIdx_ != kGreetingSlots) {
            for (int i = 0; i < 5; ++i) {
                const char* raw = npcDef_->textGrid[greetingIdx_][i];
                const std::string line(raw, strnlen(raw, sizeof(npcDef_->textGrid[0][0])));
                if (line.empty()) continue;
                ctx.Text(line.c_str(), panel.x + kNameOffsetX,
                         panel.y + kGreetOffsetY + kGreetPitchY * i, kColText);
            }
        }
    }

    // Numeric service code, pending the atlas mapping (cf. TODO of the Panels phase).
    for (int i = 0; i < kMaxServices; ++i) {
        if (serviceCodes_[i] < 1) continue;
        const Rect r = ServiceRowRect(i);
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "service %d", serviceCodes_[i]);
        ctx.Text(lbl, r.x + 4, r.y + 4, kColTextDim);
    }
}

} // namespace ts2::ui

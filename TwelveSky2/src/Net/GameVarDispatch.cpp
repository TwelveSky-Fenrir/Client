// Net/GameVarDispatch.cpp — implementation of Pkt_SetGameVar (opcode 0x16, EA 0x468370).
//
// Byte-exact translation of the original switch (v63 = selector, v66 = signed value).
// The binary reads:
//     Crt_Memcpy(&v63, &unk_8156C1, 4);   // selector = payload[0..3]
//     Crt_Memcpy(&v66, &unk_8156C5, 4);   // value    = payload[4..7]
// then `switch (v63)`. Each case is reproduced in order, keeping the original
// symbols/EA in comment. The modeled globals (currency, weight, attribute points,
// element) go into g_World.self / g_Client.inv; the whole long tail of dword_XXXX
// goes through g_Client.Var(address).
//
// Modules not written (stubbed locally, faithful to the SIGNATURE, body // TODO):
//   Char_CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0)  -> stat engine
//   Map_BeginWarpToFactionTown[Ex] (0x55C510/0x55C9A0)-> map warp
//   Net_ShopAction_4 (0x5C95C0)                       -> network action (shop)
//   Player_CheckStateDigit (0x511740)                 -> player controller
//   sub_5C9870 / UI_Confirm2Dlg_Init (0x5C9870/0x5C9800) -> UI dialogs
//
// WARNING: FALSE FRIEND FIXED (W11/GVAR-01) — `Crt_StringInit 0x75CAB0` is NOT a
// "custom std::string init" (which is what both this line AND the IDB comment claimed):
// it is a TWO-ARGUMENT `strcpy(dest, src)`. Proof in disassembly:
//   0x75CAB0 push edi ; 0x75CAB1 mov edi,[esp+4+arg_0]  <- edi = RAW dest
//   0x75CAB5 jmp short loc_75CB25                       <- jump into the tail of
//   0x75CAC0 Crt_Strcat (which, itself, first scans dest's NUL then `lea edi,[ecx-N]`)
//   0x75CB25 mov ecx,[esp+4+arg_4]                      <- ecx = src ; copy loop
// Alternate entry point, classic MSVC CRT pattern: strcpy == strcat MINUS the scan prologue.
// The 1-argument no-op stub that used to live here has therefore been REMOVED (it reduced
// case 98's day-of-week sub-switch to a no-op); call sites now use std::memcpy.
// The project already knew this elsewhere: GameHandlers_ChatSocial.cpp:279,
// GameHandlers_PartyGuild.cpp:321-322, WorldEntityDispatch.cpp:1218-1220.
#include "Net/GameVarDispatch.h"
#include "Net/NetClient.h"                   // net::g_GmAuthLevel (dword_1669294) — case 54
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/GameDatabase.h"
#include "Game/StatEngine.h"                 // StatEngine::CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0)
#include "Game/MapWarp.h"                    // ts2::game::BeginWarpToFactionTown (0x55C510/0x55C9A0)
#include "Game/MotionPoolsCoordResolver.h"  // g_CoordResolver (town coord resolver, 0x5025E0/0x4FD4C0)

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>

namespace ts2::game {
namespace {

// ---------------------------------------------------------------------------
// Original addresses of globals NOT modeled as a dedicated field (long tail).
// Stable key for g_Client.Var(addr). Binary's original name kept in comment.
// ---------------------------------------------------------------------------
constexpr uint32_t A_SysMsgColor      = 0x84DFD8;   // g_SysMsgColor (system line color)
// Attack rating (4 globals handled by the stat engine):
constexpr uint32_t A_RatingBaseMin    = 0x168736C;  // dword_168736C[0]  base min
constexpr uint32_t A_RatingCurMin     = 0x1687370;  // dword_1687370[0]  current min (clamped)
constexpr uint32_t A_RatingBaseMax    = 0x1687374;  // dword_1687374[0]  base max
constexpr uint32_t A_RatingCurMax     = 0x1687378;  // dword_1687378[0]  current max (clamped)

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

// System line color (g_SysMsgColor 0x84DFD8); defaults to white if unset.
uint32_t sysColor() {
    uint32_t c = static_cast<uint32_t>(g_Client.VarGet(A_SysMsgColor));
    return c ? c : 0xFFFFFFFFu;
}

// Localized system line: Msg_AppendSystemLine(StrTable005_Get(id), g_SysMsgColor).
void sysMsg(int strId) { g_Client.msg.System(Str(strId), sysColor()); }

// vsnprintf on a known LITERAL format (the only hardcoded formats in the binary).
std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap; va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// FmtFromStrTable(strId, ...): Crt_Vsnprintf(buf, StrTable005_Get(strId), args...) — the
// format IS the localized table entry (distinct from fmt(), which takes a literal format).
// Used when the binary passes StrTable005_Get(id) as the 1st arg of Crt_Vsnprintf (0x75CD5F).
std::string FmtFromStrTable(int strId, ...) {
    char buf[1024];
    va_list ap; va_start(ap, strId);
    std::vsnprintf(buf, sizeof(buf), Str(strId).c_str(), ap);   // format = table entry
    va_end(ap);
    return std::string(buf);
}

// Char_CalcAttackRatingMin 0x4CD970 — full aggregate via StatEngine (original this =
// g_EquipSnapshotScratch 0x8E719C, self's equipment snapshot; C++ equiv. = g_World.self/db).
int Char_CalcAttackRatingMin() { return StatEngine::CalcAttackRatingMin(g_World.self, g_World.db); }
// Char_CalcAttackRatingMax 0x4CE3F0 — same.
int Char_CalcAttackRatingMax() { return StatEngine::CalcAttackRatingMax(g_World.self, g_World.db); }

// "recalc min + clamp" pattern (cases 47, 77, 91):
//   base=CalcMin(); if (curMin > base) curMin = base;
void ratingRecalcMinClamp() {
    int mn = Char_CalcAttackRatingMin();
    g_Client.Var(A_RatingBaseMin) = mn;
    if (g_Client.VarGet(A_RatingCurMin) > mn) g_Client.Var(A_RatingCurMin) = mn;
}

// "recalc min AND max with clamp" pattern (cases 32, 40, 71):
void ratingRecalcBothClamp() {
    int mn = Char_CalcAttackRatingMin();
    g_Client.Var(A_RatingBaseMin) = mn;
    if (g_Client.VarGet(A_RatingCurMin) > mn) g_Client.Var(A_RatingCurMin) = mn;
    int mx = Char_CalcAttackRatingMax();
    g_Client.Var(A_RatingBaseMax) = mx;
    if (g_Client.VarGet(A_RatingCurMax) > mx) g_Client.Var(A_RatingCurMax) = mx;
}

// "recalc min and max WITHOUT clamp" pattern (cases 68, 95):
void ratingRecalcSet() {
    g_Client.Var(A_RatingBaseMin) = Char_CalcAttackRatingMin();
    g_Client.Var(A_RatingBaseMax) = Char_CalcAttackRatingMax();
}

// --- Stubs for subsystems not yet written (faithful signatures) -----------------
//
// [W10 / AUD-02] The no-op stub `Snd3D_PlayScaledVolume(int,int,int) {}` that used to
// live HERE was REMOVED: it was doubly misleading.
//   1) It hid the gap from local audits (the code compiled and "played a sound"
//      that didn't actually exist);
//   2) its SIGNATURE was wrong. IDA truth (Snd3D_PlayScaledVolume 0x4DA380,
//      `retn 0Ch` @0x4DA3E7 + `mov [ebp+var_4], ecx` @0x4DA386):
//        char __thiscall(Snd3D* this, int arg0, int percent, char arg8)
//      — the 1st parameter is an emitter POINTER, not an id. The
//      `__userpurge(a2@ebx, a3@edi)` shown by Hex-Rays is an ARTIFACT
//      (ebx/edi belong to Snd_Play3D, not to the prologue).
//   3) The 3rd argument is DEAD: `mov byte ptr [ebp+arg_8], 0` @0x4DA389 overwrites
//      the argument BEFORE any use, then `movzx ecx, [ebp+arg_8]` @0x4DA395 ->
//      Snd3D_EnsureLoaded(this, 0). The `push 1` at the 6 call sites has no effect.
//
// EMITTER OF THE 6 SITES — identity RESOLVED (wave W10). The 6 sites are
// byte-identical: `push 1 / push 64h / push 0 / mov ecx, offset flt_1490A7C /
// call Snd3D_PlayScaledVolume` (arg0=0, percent=100, dead arg8=1).
// flt_1490A7C is not a standalone global: it is slot 189 of the asset manager's
// type-4 bank —
//   AssetMgr_InitAllSlots 0x4DEB50 (sole caller App_Init, `mov ecx, offset
//   g_ModelMotionArray` @0x46224B, this = 0x8E8B30); bank-4 loop
//   @0x4E05C3..0x4E05F9: `for (i=0; i<0x19A; ++i) Snd3D_SetISNPath(this +
//   0xB9F18C + 0xC0*i, /*type=*/4, i, 0, 0, 0)` — 410 slots, stride 192.
//   base = 0x8E8B30 + 0xB9F18C = 0x1487CBC;
//   (0x1490A7C - 0x1487CBC) / 0xC0 = exactly 189.
// Snd3D_SetISNPath 0x4DA0C0 case 4 = "G03_GDATA\D06_GSOUND\004\E%03d001001.ISN"
// with (a3+1) => the file played is E190001001.ISN.
//
// BLOCKED: no Snd3D emitter bank exists on the C++ side. audio::Emitter
// (Audio/Sound3D.h:72, method PlayScaledVolume(int percent, float nowSec) —
// correct signature) is only instantiated by audio::SoundBank
// (World/WorldIntegration.cpp:417), which is the AMBIENT WSndBank: a
// distinct system, without Snd3D_SetISNPath. Building the bank belongs to Audio/* +
// Asset/* + Game/MotionPools — all out of scope for this front, and
// reimplementing it here would be duplicate work. The 6 calls are therefore replaced
// with anchored TODOs (see each case), and the dependency is escalated to
// the orchestrator. DO NOT put a no-op stub back in its place.
//
// Map_BeginWarpToFactionTown 0x55C510 — __thiscall(this=g_LocalPlayerSheet, mode); element =
// g_LocalElement 0x1673194 = g_World.self.element. Resolves + arms the warp globals
// (no network emission: nc stays at default nullptr, MapWarp.h convention).
void Map_BeginWarpToFactionTown(int mode) {
    BeginWarpToFactionTown(g_World.self.element, /*ex=*/false, mode, &g_CoordResolver);
}
// Map_BeginWarpToFactionTownEx 0x55C9A0.
void Map_BeginWarpToFactionTownEx(int mode) {
    BeginWarpToFactionTown(g_World.self.element, /*ex=*/true, mode, &g_CoordResolver);
}
// TODO(net) Net_ShopAction_4 0x5C95C0 (shop close/action).
void Net_ShopAction_4(int /*a*/) {}
// TODO(player) Player_CheckStateDigit 0x511740 (&g_PlayerCmdController).
void Player_CheckStateDigit() {}
// TODO(ui) sub_5C9870 / UI_Confirm2Dlg_Init 0x5C9870/0x5C9800 (confirmation dialog).
void UI_Confirm2Dlg_Init() {}
void UI_Confirm2Dlg_Cancel() {}
// (ex-stub `Crt_StringInit(uint32_t)` REMOVED — see the "FALSE FRIEND" block at the
//  top of the file: 0x75CAB0 is a 2-argument strcpy(dest, src), not a 1-argument init.
//  Its only caller was case 98, which now does the real copy.)

// Localized item name (MobDb_GetEntry(id) + 4 == ItemInfo::name field).
const char* itemName(uint32_t itemId) {
    const ItemInfo* it = GetItemInfo(itemId);
    return it ? it->name : "";
}

} // namespace

// ---------------------------------------------------------------------------
// Pkt_SetGameVar (EA 0x468370).
// ---------------------------------------------------------------------------
void ApplySetGameVar(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 8) return;

    int32_t selector;   // v63
    int32_t value;      // v66  (signed: compared to -1, <=0, etc.)
    std::memcpy(&selector, payload + 0, 4);   // unk_8156C1
    std::memcpy(&value,    payload + 4, 4);   // unk_8156C5

    switch (selector) {
    case 1: // System message 219
        sysMsg(219); // /*0x4683fd*/ StrTable005_Get(219)
        break;

    case 2: // g_SelfUnspentAttrPoints 0x16731D0
        g_World.self.unspentAttr = value; // /*0x468415*/
        break;

    case 3: // g_Currency 0x1673180 (+ mirror dword_1687254[0])
        g_World.self.currency = value;    // /*0x468422*/
        g_Client.inv.currency = value;
        g_Client.Var(0x1687254) = value;  // dword_1687254[0] /*0x46842b*/
        break;

    case 4:  g_Client.Var(0x16746F4) = value; break; // dword_16746F4 /*0x468439*/
    case 5:  g_Client.Var(0x16746F8) = value; break; // dword_16746F8 /*0x468446*/
    case 6:  g_Client.Var(0x16746A4) = value; break; // dword_16746A4 /*0x468454*/
    case 7:  g_Client.Var(0x16731B0) = value; break; // dword_16731B0 /*0x468462*/

    case 8: // dword_16746E4 + Msg 691
        g_Client.Var(0x16746E4) = value; // /*0x46846f*/
        sysMsg(691);                     // /*0x468486*/
        break;

    case 9: // g_SelfCharInvBlock 0x1673170 (head word) + mirror dword_168724C[0]
        g_Client.Var(0x1673170) = value; // /*0x46849e*/
        g_Client.Var(0x168724C) = value; // /*0x4684a6*/
        break;

    case 10: g_Client.Var(A_RatingCurMin) = value; break; // dword_1687370[0] /*0x4684b4*/
    case 11: g_Client.Var(A_RatingCurMax) = value; break; // dword_1687378[0] /*0x4684c2*/
    case 12: g_Client.Var(0x167325C) = value; break;      // dword_167325C /*0x4684cf*/
    case 13: g_Client.Var(0x16731B0) = value; break;      // dword_16731B0 /*0x4684dd*/

    case 14: // dword_1673260 + mirror dword_16872EC
        g_Client.Var(0x1673260) = value; // /*0x4684eb*/
        g_Client.Var(0x16872EC) = value; // /*0x4684f3*/
        break;

    case 15: // dword_16746E8 + sound + Msg 654
        g_Client.Var(0x16746E8) = value;      // /*0x468501*/
        // TODO(audio) [anchor 0x468512] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Blocked: no Snd3D emitter bank on
        //   the C++ side (see AUD-02 block at the top of the file).
        sysMsg(654);                          // /*0x468527*/
        break;

    case 16: // ++dword_1673178 ; dword_167317C = value ; ++dword_167478C
        ++g_Client.Var(0x1673178);       // /*0x468545*/
        g_Client.Var(0x167317C) = value; // /*0x46854e*/
        ++g_Client.Var(0x167478C);       // /*0x468559*/
        break;

    case 17: g_Client.Var(0x16746F0) = value; break; // dword_16746F0 /*0x468569*/

    case 18: // dword_1674700 ; if 0 -> warp to faction town
        g_Client.Var(0x1674700) = value;              // /*0x468577*/
        if (!value) Map_BeginWarpToFactionTown(0);    // /*0x46858d*/
        break;

    case 19: // g_InvWeight 0x16732AC
        g_Client.inv.weight = value; // /*0x46859a*/
        break;

    case 20: g_Client.Var(0x16746EC) = value; break; // dword_16746EC /*0x4685a7*/

    case 21: // dword_1674704 ; if 0 -> warp
        g_Client.Var(0x1674704) = value;              // /*0x4685b5*/
        if (!value) Map_BeginWarpToFactionTown(0);    // /*0x4685cb*/
        break;

    case 22: // dword_1674708 ; if 0 && dword_167588C<=0 -> warp
        g_Client.Var(0x1674708) = value;              // /*0x4685d8*/
        if (!value && g_Client.VarGet(0x167588C) <= 0)
            Map_BeginWarpToFactionTown(0);            // /*0x4685f6*/
        break;

    case 23: // (falls through with 45) g_InvWeight += value ; Msg "(%d)%s" (value, Str(634))
    case 45:
        g_Client.inv.weight += value; // /*0x468609*/
        g_Client.msg.System(fmt("(%d)%s", value, Str(634).c_str()), sysColor()); // /*0x46862f*/
        break;

    case 24: g_Client.Var(0x16731B4) = value; break; // dword_16731B4 /*0x468657*/

    case 25: // g_InvWeight += value ; Msg "(%d)%s" (value, Str(891))
        g_Client.inv.weight += value; // /*0x46866a*/
        g_Client.msg.System(fmt("(%d)%s", value, Str(891).c_str()), sysColor()); // /*0x468690*/
        break;

    case 26: // dword_1674764 ; if 0 && dword_1687310[0] -> Net_ShopAction_4
        g_Client.Var(0x1674764) = value; // /*0x4686b8*/
        if (!value && g_Client.VarGet(0x1687310))
            Net_ShopAction_4(0);         // /*0x4686d6*/
        break;

    case 27: g_Client.Var(0x1674770) = value; break; // dword_1674770 /*0x4686e3*/

    case 28: // dword_1674768 ; Msg "%s" (Str(988))
        g_Client.Var(0x1674768) = value; // /*0x4686f1*/
        g_Client.msg.System(fmt("%s", Str(988).c_str()), sysColor()); // /*0x468713*/
        break;

    case 29: // dword_167476C ; Msg "%s" (Str(989))
        g_Client.Var(0x167476C) = value; // /*0x46873b*/
        g_Client.msg.System(fmt("%s", Str(989).c_str()), sysColor()); // /*0x46875c*/
        break;

    case 30: g_Client.Var(0x1674778) = value; break; // dword_1674778 /*0x468784*/

    case 31: // dword_1674794 + sound + Msg 1185
        g_Client.Var(0x1674794) = value;   // /*0x468792*/
        // TODO(audio) [anchor 0x4687A3] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Blocked (see AUD-02 block at top of file).
        sysMsg(1185);                      // /*0x4687b8*/
        break;

    case 32: // dword_167479C ; if 0 -> recalc rating (min+max, clamp)
        g_Client.Var(0x167479C) = value;   // /*0x4687d0*/
        if (!value) ratingRecalcBothClamp(); // /*0x4687e9*/
        break;

    case 33: g_Client.Var(0x16747A0) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x468847*/
    case 34: g_Client.Var(0x16747A4) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x46886a*/
    case 35: g_Client.Var(0x16747A8) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x46888c*/
    case 36: g_Client.Var(0x16747AC) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x4688af*/
    case 37: g_Client.Var(0x16747B0) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x4688d2*/
    case 38: g_Client.Var(0x16747B4) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x4688f4*/
    case 39: g_Client.Var(0x16747B8) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x468917*/

    case 40: // dword_16747C8 ; if 0 -> recalc rating (min+max, clamp)
        g_Client.Var(0x16747C8) = value;     // /*0x46893a*/
        if (!value) ratingRecalcBothClamp(); // /*0x468952*/
        break;

    case 41: g_Client.Var(0x16747DC) = value; break; // dword_16747DC /*0x4689b0*/
    case 42: g_Client.Var(0x16747E0) = value; break; // dword_16747E0 /*0x4689be*/
    case 43: g_Client.Var(0x16747E4) = value; break; // dword_16747E4 /*0x4689cc*/
    case 44: g_Client.Var(0x16747EC) = value; break; // dword_16747EC /*0x4689d9*/

    case 46: g_Client.Var(0x1674A50) = value; break; // dword_1674A50 /*0x468a3b*/

    case 47: // dword_1674A54 ; recalc min (clamp)
        g_Client.Var(0x1674A54) = value; // /*0x468a49*/
        ratingRecalcMinClamp();          // /*0x468a59*/
        break;

    case 48: g_Client.Var(0x1674A58) = value; break; // dword_1674A58 /*0x468a87*/

    case 49: // dword_1675AFC ; Msg + floating banner "(%d)%s" (dword_1675AFC, Str(1479)) ; if <=0 warp
        g_Client.Var(0x1675AFC) = value; // /*0x468a94*/
        {
            std::string s = fmt("(%d)%s", g_Client.VarGet(0x1675AFC), Str(1479).c_str());
            g_Client.msg.Floating(1, 0, s);        // HUD_ShowFloatingMessage(1,0,..) /*0x468ada*/
            g_Client.msg.System(s, sysColor());    // /*0x468af2*/
        }
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x468b04*/
        break;

    case 50: // Msg "(mosterKill: %d)" (value)  [sic: original typo "moster"]
        g_Client.msg.System(fmt("(mosterKill: %d)", value), sysColor()); // /*0x468b38*/
        break;

    case 54: // dword_1674AAC ; if GM (g_GmAuthLevel>0) -> Msg "TimeEffectTime:%d"
        g_Client.Var(0x1674AAC) = value; // g_TimeEffectRemain /*0x468b45*/
        // (0x468b4b) `cmp ds:g_GmAuthLevel, 0 ; jle` — SIGNED comparison on the global
        // dword_1669294. Source of truth = net::g_GmAuthLevel (Net/NetClient.h:119),
        // FED by Net/Login.cpp:218 (memcpy from recvBuf+133) and read by
        // UI/GameHud.cpp:1228 / Scene/SceneManager.cpp:1417. The old read
        // `g_Client.VarGet(0x1669294)` targeted a 2nd parallel store that NOBODY writes
        // (exhaustive grep) -> the debug line never displayed, even for a GM.
        if (static_cast<int32_t>(net::g_GmAuthLevel) > 0) // /*0x468b4b, jle = signed*/
            g_Client.msg.System(fmt("TimeEffectTime:%d", g_Client.VarGet(0x1674AAC)), sysColor()); // /*0x468b66*/
        break;

    case 55: // if value==-1 : Msg per dword_1674AB4 (0..5) ; else dword_1674AB0=value + Msg 1706
        if (value == -1) { // /*0x468b8f*/
            switch (g_Client.VarGet(0x1674AB4)) { // dword_1674AB4
            case 0: sysMsg(1888); break; // /*0x468bcb*/
            case 1: sysMsg(1707); break; // /*0x468bf1*/
            case 2: sysMsg(1708); break; // /*0x468c17*/
            case 3: sysMsg(1709); break; // /*0x468c39*/
            case 4: sysMsg(1710); break; // /*0x468c5c*/
            case 5: sysMsg(2228); break; // /*0x468c7f*/
            default: return;             // /*default -> return*/
            }
        } else {
            g_Client.Var(0x1674AB0) = value; // /*0x468c94*/
            sysMsg(1706);                    // /*0x468caa*/
        }
        break;

    case 56: g_Client.Var(0x16760AC) = value; break; // dword_16760AC /*0x468cc2*/
    case 57: g_Client.Var(0x16760B0) = value; break; // dword_16760B0 /*0x468cd0*/
    case 58: g_Client.Var(0x16760B4) = value; break; // dword_16760B4 /*0x468cdd*/

    case 59: // dword_168744C = (value > 0)
        g_Client.Var(0x168744C) = (value > 0) ? 1 : 0; // /*0x468cec*/
        break;

    case 60: // dword_1674A20[value/1e8] = value % 1e8
        g_Client.Var(0x1674A20 + 4u * static_cast<uint32_t>(value / 100000000)) = value % 100000000; // /*0x468d21*/
        break;

    case 61: // g_AutoHuntFuelA 0x16755A4 ; if g_InvDirtyEnable==1 && !fuel -> warpEx
        g_Client.Var(0x16755A4) = value; // /*0x468d30*/
        if (g_Client.VarGet(0x16755AC) == 1 && !g_Client.VarGet(0x16755A4))
            Map_BeginWarpToFactionTownEx(0); // /*0x468d4f*/
        break;

    case 62: // g_AutoHuntFuelB 0x16755A8 ; if 0 -> warpEx
        g_Client.Var(0x16755A8) = value; // /*0x468d5c*/
        if (!value) Map_BeginWarpToFactionTownEx(0); // /*0x468d71*/
        break;

    case 63: // dword_1675638 + Msg 1931
        g_Client.Var(0x1675638) = value; // /*0x468d7e*/
        sysMsg(1931);                    // /*0x468d95*/
        break;

    case 64: g_Client.Var(0x1675634) = value; if (value <= 0) Map_BeginWarpToFactionTown(0); break; // /*0x468dad*/
    case 65: g_Client.Var(0x1675654) = value; if (value <= 0) Map_BeginWarpToFactionTown(0); break; // /*0x468dcc*/

    case 66: // dword_1675678 + mirror dword_168746C
        g_Client.Var(0x1675678) = value; // /*0x468dec*/
        g_Client.Var(0x168746C) = value; // /*0x468df5*/
        break;

    case 67: g_Client.Var(0x167567C) = value; break; // dword_167567C /*0x468e02*/

    case 68: // g_ElementMastery 0x1675680 (on/off) + recalc rating + Msg
        if (value) { // /*0x468e11*/
            g_Client.Var(0x1675680) = value;   // g_ElementMastery /*0x468e16*/
            ratingRecalcSet();                 // /*0x468e26*/
            sysMsg(2086);                      // /*0x468e4a*/
        } else {
            g_Client.Var(0x1675678) = 0;       // /*0x468e5c*/
            g_Client.Var(0x1675680) = 0;       // g_ElementMastery /*0x468e66*/
            g_Client.Var(0x168746C) = 0;       // /*0x468e70*/
            ratingRecalcSet();                 // /*0x468e84*/
        }
        break;

    case 69: g_Client.Var(0x1675684) = value; break; // dword_1675684 /*0x468ea0*/

    case 70: // g_AutoHuntFuelA (same as 61)
        g_Client.Var(0x16755A4) = value; // /*0x468eae*/
        if (g_Client.VarGet(0x16755AC) == 1 && !g_Client.VarGet(0x16755A4))
            Map_BeginWarpToFactionTownEx(0); // /*0x468ecd*/
        break;

    case 71: { // idx = g_TalismanSlot - 10 ; if 0<=idx<10 : dword_167568C[idx]=value + recalc
        int idx = g_Client.VarGet(0x1674760) - 10; // g_TalismanSlot /*0x468f05*/
        if (idx >= 0 && idx < 10) {
            g_Client.Var(0x167568C + 4u * static_cast<uint32_t>(idx)) = value; // /*0x468f1e*/
            ratingRecalcBothClamp(); // /*0x468f2f*/
        }
        break;
    }

    case 72: g_Client.Var(0x167565C) = value; break; // dword_167565C /*0x468eda*/
    case 73: g_Client.Var(0x1674A5C) = value; break; // g_AutoHuntSkillSlotUnlocks /*0x468ee7*/
    case 74: g_Client.Var(0x1674718) = value; break; // dword_1674718 /*0x468ef5*/
    case 75: g_Client.Var(0x16756DC) = value; break; // dword_16756DC /*0x468f8d*/
    case 76: g_Client.Var(0x16756E0) = value; break; // dword_16756E0 /*0x468f9a*/

    case 77: // dword_16756E4 ; recalc min (clamp)
        g_Client.Var(0x16756E4) = value; // /*0x468fa8*/
        ratingRecalcMinClamp();          // /*0x468fb8*/
        break;

    case 78: g_Client.Var(0x16756F0) = value; break; // dword_16756F0 /*0x468fe6*/

    case 79: // dword_16756F4 ; Msg 2153 if value else 2152
        g_Client.Var(0x16756F4) = value;     // /*0x468ff3*/
        sysMsg(value ? 2153 : 2152);         // /*0x469035 / 0x469013*/
        break;

    case 80: g_Client.Var(0x16756FC) = value; break; // dword_16756FC /*0x46904d*/

    case 81: // dword_1675704 ; if ==1 Msg 2265
        g_Client.Var(0x1675704) = value; // /*0x46905b*/
        if (value == 1) sysMsg(2265);    // /*0x469077*/
        break;

    case 82: // dword_1675700 ; flt_1687244 = (float)value
        g_Client.Var(0x1675700) = value;                       // /*0x46908f*/
        g_Client.VarF(0x1687244) = static_cast<float>(value);  // /*0x469098*/
        break;

    case 83: g_Client.Var(0x167570C) = value; break; // dword_167570C /*0x4690a6*/
    case 84: g_Client.Var(0x1675708) = value; break; // dword_1675708 /*0x4690b4*/
    case 85: g_Client.Var(0x16756E8) = value; break; // dword_16756E8 /*0x4690c1*/
    case 86: g_Client.Var(0x16756EC) = value; break; // dword_16756EC /*0x4690cf*/
    case 87: g_Client.Var(0x1675710) = value; break; // dword_1675710 /*0x4690dd*/
    case 88: g_Client.Var(0x16732A8) = value; break; // g_Inv_ExtraPageCount /*0x4690ea*/
    case 89: g_Client.Var(0x1673F34) = value; break; // dword_1673F34 /*0x4690f8*/
    case 90: g_Client.Var(0x16755A4) = value; break; // g_AutoHuntFuelA /*0x469106*/

    case 91: // dword_1675728 ; recalc min (clamp)
        g_Client.Var(0x1675728) = value; // /*0x469113*/
        ratingRecalcMinClamp();          // /*0x469123*/
        break;

    case 93: g_Client.Var(0x16760B8) = value; break; // dword_16760B8 /*0x469151*/
    case 94: g_Client.Var(0x16760BC) = value; break; // dword_16760BC /*0x46915f*/

    case 95: // dword_16760C0 ; recalc set (min+max, without clamp)
        g_Client.Var(0x16760C0) = value; // /*0x46916c*/
        ratingRecalcSet();               // /*0x46917c*/
        break;

    case 97: // warp + Msg 237
        Map_BeginWarpToFactionTown(0); // /*0x46919c*/
        sysMsg(237);                   // /*0x4691b2*/
        break;

    case 98: // dword_1676108 ; if >0 split into digit groups then init string
        g_Client.Var(0x1676108) = value; // /*0x4691ca*/
        if (value > 0) {
            int v = g_Client.VarGet(0x1676108);
            g_Client.Var(0x167610C) = v / 10000000 + 1;      // &unk_989680 == 0x989680 == 10,000,000 /*0x4691ec*/
            g_Client.Var(0x1676110) = v % 10000000 / 100000; // /*0x469208*/
            g_Client.Var(0x1676114) = v % 100000 / 1000;     // /*0x469224*/
            g_Client.Var(0x1676118) = v % 1000 / 10;         // /*0x469240*/

            // (0x46924B-0x469252) `mov ecx,0Ah ; idiv ; mov [ebp+var_418], edx` -> d = v % 10.
            // (0x469258-0x46926B) `cmp var_418,6 ; ja def_46926B ; jmp jpt_46926B[edx*4]`
            //   -> 7-case sub-switch (table @0x469C5C) + default. UNSIGNED comparison.
            // Each branch does `push <literal> ; push offset byte_167611C ;
            //   call Crt_StringInit ; add esp,8` == strcpy(byte_167611C, literal)
            //   (0x75CAB0 = strcpy(dest, src) — see the "FALSE FRIEND" block at top of file).
            // The old comment "both branches do the same Crt_StringInit" was
            // FACTUALLY WRONG: there are 8 branches, each with a DIFFERENT literal.
            static const char* const kDayNames[7] = {
                "Sunday",   // /*0x469272, aSunday    0x7A6D00*/
                "Monday",   // /*0x469289, aMonday    0x7A6CF8*/
                "Tuesday",  // /*0x46929d, aTuesday   0x7A6CF0*/
                "Wednesday",// /*0x4692b1, aWednesday 0x7A6CE4*/
                "Thursday", // /*0x4692c5, aThursday  0x7A6CD8*/
                "Friday",   // /*0x4692d9, aFriday    0x7A6CD0*/
                "Saturday", // /*0x4692ed, aSaturday  0x7A6CC4*/
            };
            const int d = v % 10;                                 // /*0x46924b*/
            const char* day = (d >= 0 && d <= 6) ? kDayNames[d]   // /*ja @0x46925f*/
                                                 : "Unknown";     // /*def_46926B @0x469301, d in 7..9*/
            // Destination = byte_167611C, single-use buffer: xrefs_to = 9 = these 8 writes
            // + 1 SINGLE read, UI_GameHud_Render @0x6868FA, which pushes it DIRECTLY as
            // a %s vararg of "NowTime : %d / %d %d:%d %s" -> it is indeed a raw char[].
            // Size 64 imposed by the 1st caller of Blob(): UI/GameHud.cpp:1237.
            // memcpy(len+1) reproduces strcpy: NUL included, rest of buffer left untouched.
            auto& dayBlob = g_Client.Blob(0x167611C, 64);         // /*0x469277 push offset byte_167611C*/
            std::memcpy(dayBlob.data(), day, std::strlen(day) + 1); // /*0x46927c call Crt_StringInit*/
        }
        break;

    case 99: // dword_1675730 + sound + Msg 2321
        g_Client.Var(0x1675730) = value;   // /*0x46931b*/
        // TODO(audio) [anchor 0x46932B] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Blocked (see AUD-02 block at top of file).
        sysMsg(2321);                      // /*0x469341*/
        break;

    case 100: g_Client.Var(0x16760C4) = value; break; // dword_16760C4 /*0x469359*/
    case 101: g_Client.Var(0x16760C8) = value; break; // dword_16760C8 /*0x469367*/
    case 102: g_Client.Var(0x16760CC) = value; break; // dword_16760CC /*0x469374*/

    case 104: // dword_1675738 + sound + Msg 2354
        g_Client.Var(0x1675738) = value;   // /*0x469382*/
        // TODO(audio) [anchor 0x469393] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Blocked (see AUD-02 block at top of file).
        sysMsg(2354);                      // /*0x4693a8*/
        break;

    case 105: g_Client.Var(0x16757A0) = value; break; // dword_16757A0 /*0x4693c0*/

    case 106: // dword_16757A8 ; if morph==310 && element != dword_1685E08 && !value -> warp
        g_Client.Var(0x16757A8) = value; // /*0x4693ce*/
        if (g_Client.VarGet(0x1675A98) == 310) { // g_SelfMorphNpcId
            if (g_World.self.element != g_Client.VarGet(0x1685E08) && !g_Client.VarGet(0x16757A8))
                Map_BeginWarpToFactionTown(0); // /*0x4693fd*/
        }
        break;

    case 107: // dword_1674780 ; opens/closes confirmation dialog
        g_Client.Var(0x1674780) = value; // /*0x46940a*/
        if (g_Client.VarGet(0x182242C) || g_Client.VarGet(0x1674780) != 1)
            UI_Confirm2Dlg_Cancel();     // sub_5C9870 /*0x469433*/
        else
            UI_Confirm2Dlg_Init();       // /*0x469427*/
        break;

    case 108: // dword_167587C ; if <=0 warp
        g_Client.Var(0x167587C) = value;              // /*0x469440*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x469453*/
        break;

    case 109: // if dword_1675878<1 && value>0 -> Msg 2295 ; dword_1675878 = value
        if (g_Client.VarGet(0x1675878) < 1 && value > 0)
            sysMsg(2295);                 // /*0x46947c*/
        g_Client.Var(0x1675878) = value;  // /*0x46948f*/
        break;

    case 110: // dword_16760D8 / dword_16760DC per value (0,1,2,3)
        if (value) { // /*0x46949e*/
            switch (value) { // /*0x4694b0*/
            case 1: g_Client.Var(0x16760DC) = 0;  break; // /*0x4694b2*/
            case 2: g_Client.Var(0x16760D8) = 15; break; // /*0x4694c4*/
            case 3: g_Client.Var(0x16760DC) = 15; break; // /*0x4694d6*/
            }
        } else {
            g_Client.Var(0x16760D8) = 0; // /*0x4694a0*/
        }
        break;

    case 111: g_Client.Var(0x1675884) = value; break; // dword_1675884 /*0x4694e8*/

    case 112: { // dword_1675804 ; if <1 : clean up auto-potion belt
        g_Client.Var(0x1675804) = value; // /*0x4694f6*/
        if (value < 1) {
            int slot = g_Client.VarGet(0x1675800); // dword_1675800
            uint32_t beltAddr = 0x16757B0 + 4u * static_cast<uint32_t>(slot); // g_AutoPotionBelt[slot]
            uint32_t cntAddr  = 0x16757D8 + 4u * static_cast<uint32_t>(slot); // dword_16757D8[slot]
            if (g_Client.VarGet(beltAddr) == 878) { // /*0x469515*/
                g_Client.Var(A_RatingBaseMin) = Char_CalcAttackRatingMin(); // /*0x46951c*/
            }
            if (g_Client.VarGet(cntAddr) < 1) { // /*0x469534*/
                g_Client.Var(beltAddr) = 0;     // /*0x46953b*/
            }
        }
        break;
    }

    case 113: // dword_167587C (same as 108)
        g_Client.Var(0x167587C) = value;              // /*0x46954e*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x469561*/
        break;

    case 114: // g_Currency += value ; Crt_Vsnprintf(buf, StrTable005_Get(1845), value) ; color 1
        g_World.self.currency += value; // g_Currency 0x1673180 /*0x469574*/
        g_Client.inv.currency += value;
        // Format = table entry 1845 (contains the %d) ; arg = value ; literal color 1.
        g_Client.msg.System(FmtFromStrTable(1845, value), 1); // /*0x469588..0x4695ab*/
        break;

    case 115: // dword_1675890 + sound + Msg 2528
        g_Client.Var(0x1675890) = value;   // /*0x4695b8*/
        // TODO(audio) [anchor 0x4695C8] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Blocked (see AUD-02 block at top of file).
        sysMsg(2528);                      // /*0x4695de*/
        break;

    case 116: g_Client.Var(0x167588C) = value; break; // dword_167588C /*0x4695f6*/

    case 117: // dword_167563C ; if <=0 warp
        g_Client.Var(0x167563C) = value;              // /*0x469604*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x469616*/
        break;

    case 118: // dword_1675894 + Player_CheckStateDigit
        g_Client.Var(0x1675894) = value; // /*0x469623*/
        Player_CheckStateDigit();        // /*0x46962e*/
        break;

    case 119: // dword_1675898 + Player_CheckStateDigit
        g_Client.Var(0x1675898) = value; // /*0x46963b*/
        Player_CheckStateDigit();        // /*0x469646*/
        break;

    case 120: // dword_1675640 + sound + Msg 2654
        g_Client.Var(0x1675640) = value;   // /*0x469653*/
        // TODO(audio) [anchor 0x469663] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Blocked (see AUD-02 block at top of file).
        sysMsg(2654);                      // /*0x469679*/
        break;

    case 121: // dword_1675644 + Msg 2684
        g_Client.Var(0x1675644) = value; // /*0x469691*/
        sysMsg(2684);                    // /*0x4696a7*/
        break;

    case 122: g_Client.Var(0x16760E4) = value; break; // dword_16760E4 /*0x4696bf*/
    case 123: g_Client.Var(0x16760E8) = value; break; // dword_16760E8 /*0x4696cd*/
    case 124: g_Client.Var(0x16760EC) = value; break; // dword_16760EC /*0x4696db*/
    case 125: g_Client.Var(0x16760F0) = value; break; // dword_16760F0 /*0x4696e8*/

    case 126: // (falls through with 133) Msg "%s %s %s" (Str(2532), Str(value+2685), Str(2667))
    case 133:
        g_Client.msg.System(
            fmt("%s %s %s", Str(2532).c_str(), Str(value + 2685).c_str(), Str(2667).c_str()),
            sysColor()); // /*0x469734*/
        break;

    // Cases 127/146/147/148 — COMMON pattern (re-verified instruction by instruction @0x469759):
    //   push <itemId> ; mov ecx, offset mITEM ; call MobDb_GetEntry
    //   mov ecx,[ebp+var_400] ; add ecx,4 ; push ecx      <- ARG = ItemInfo::name
    //   push 0BB3h (2995) ; call StrTable005_Get ; push eax <- FORMAT
    //   lea edx,[ebp+var_3F8] ; push edx ; call Crt_Vsnprintf ; add esp, 0Ch
    // `add esp,0Ch` = EXACTLY 3 arguments -> Crt_Vsnprintf(buf, Str(2995), itemName).
    // Str(2995) is therefore the FORMAT (it contains the %s), NOT a prefix to concatenate:
    // the old form `Str(2995) + " [" + itemName + "]"` left the %s LITERAL on
    // screen and invented a " [ ]" decoration absent from the binary. Pattern already
    // established and accepted at case 114 (FmtFromStrTable(1845, value)).
    case 127: // dword_1675648 ; Msg Str(2995) formatted with item name 1833 (0x729)
        g_Client.Var(0x1675648) = value; // /*0x46975c*/
        g_Client.msg.System(FmtFromStrTable(2995, itemName(1833)), sysColor()); // /*0x469797*/
        break;

    case 128: g_Client.Var(0x167564C) = value; break; // dword_167564C /*0x4697be*/
    case 129: g_Client.Var(0x16760F8) = value; break; // dword_16760F8 /*0x4697cc*/
    case 130: g_Client.Var(0x16760FC) = value; break; // dword_16760FC /*0x4697d9*/
    case 131: g_Client.Var(0x1676100) = value; break; // dword_1676100 /*0x4697e7*/
    case 132: g_Client.Var(0x1676104) = value; break; // dword_1676104 /*0x4697f5*/

    case 146: // dword_1674AA4 ; Msg Str(2995) formatted with item name 17210 (0x434A)
        g_Client.Var(0x1674AA4) = value; // /*0x469867*/
        // Pattern verified identical to case 127: `push 433Ah` @0x469864, `add esp,0Ch` @0x4698a8.
        g_Client.msg.System(FmtFromStrTable(2995, itemName(17210)), sysColor()); // /*0x4698a3*/
        break;

    case 147: // dword_1674AA0 ; Msg Str(2995) formatted with item name 1956 (0x7A4)
        g_Client.Var(0x1674AA0) = value; // /*0x4698cb*/
        g_Client.msg.System(FmtFromStrTable(2995, itemName(1956)), sysColor()); // /*0x469907*/
        break;

    case 148: // dword_167589C ; Msg Str(2995) formatted with item name 17576 (0x44A8)
        g_Client.Var(0x167589C) = value; // /*0x46992f*/
        g_Client.msg.System(FmtFromStrTable(2995, itemName(17576)), sysColor()); // /*0x46996a*/
        break;

    case 149: // dword_167589C ; if <=0 warp
        g_Client.Var(0x167589C) = value;              // /*0x46998e*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x4699a1*/
        break;

    case 150: g_Client.Var(0x1674790) = value; break; // dword_1674790 /*0x4699ab*/
    case 158: g_Client.Var(0x16758A4) = value; break; // dword_16758A4 /*0x4699b5*/

    default: // missing selectors (51-53, 92, 96, 103, 134-145, 151-157, > 158): no-op
        return;
    }
}

} // namespace ts2::game

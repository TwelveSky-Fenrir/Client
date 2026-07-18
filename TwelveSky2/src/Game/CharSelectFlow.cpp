// Game/CharSelectFlow.cpp — implementation. See CharSelectFlow.h for the original EAs,
// the state machine asymmetry, and the two documentation corrections [H1]/[A12].
// Split family: shared validation/preview/recompute helpers live in
// Game/CharSelectFlow_Internal.h; the CREATE form lives in CharSelectFlow_Create.cpp; the
// DELETE/RESTORE flows live in CharSelectFlow_Delete.cpp; slot selection/ENTER/BACK/QUIT
// live in CharSelectFlow_Nav.cpp. This file keeps the Init/Active state machine core:
// RunInitBlock, UpdateListScreen, UpdateCreateScreen, InitPinWizard, FireEnterSequence,
// RecoverToIdle, UpdateCharSelect.
#include "Game/CharSelectFlow.h"
#include "Game/CharSelectFlow_Internal.h"
#include <windows.h> // MultiByteToWideChar — ValidateNameCharset() (Str_ValidateNameChars 0x53FD70)

// Two already-reified client state globals, touched DIRECTLY (not via CharSelectHost)
// because they are the SAME globals the binary manipulates, and fidelity depends on it:
//   net::g_MorphInProgress = g_MorphInProgress 0x1675A88 — ANTI-REPLAY guard for the
//     entry sequence (EA 0x51c6bb). This is not a local flag: it is also read by
//     Camera_UpdateFromInput (0x50B857, cf. App/PlayerInputController) and written by the
//     world — so coming back to CharSelect with the flag still at 1 MUST block entry,
//     exactly like the binary.
//   net::DefaultRng() = Rng_Next 0x7603FD (shared _holdrand) — the draw ORDER is
//     observable (packet nonces), so every draw must go through this single stream.
// Established precedent: Game/ComboPickupTick.h and UI/LoginScene.cpp already do exactly this.
// These two headers are leaves (<cstdint> only): no cycle with
// Net/CharSelectPackets.h, which includes Game/CharSelectFlow.h.
#include "Net/ClientState.h"
#include "Net/Rng.h"

namespace ts2::game {

namespace {

constexpr int32_t kInitWaitFrames          = 30; // 0x1E — `++this[2] >= 0x1Eu` EA 0x51bde4
constexpr int32_t kKeepAliveIntervalFrames = 30; // 0x1E — EA 0x51c40b AND 0x51c46e

// StrTable005 ids EXACTLY read off the disassembly, with their NoticeDlg TYPE (2nd arg of
// UI_NoticeDlg_Open 0x5C0280) — the type decides the fate of the Locked state, cf. .h.
// (Split family: CREATE-only ids live in CharSelectFlow_Create.cpp, DELETE/RESTORE-only ids
// in CharSelectFlow_Delete.cpp, the ENTER-only id in CharSelectFlow_Nav.cpp, and the id
// shared between DELETE and ENTER in CharSelectFlow_Internal.h.)
constexpr int32_t kStrSessionExpired      = 20;   // type 2 (EA 0x51c42a / LABEL_92 0x51cb02)

// --- op22 (Net_ReqEnterCharInfo): switch EA 0x51c83c ---
constexpr int32_t kStrEnterInfoFail55   = 55;   // code 1, TYPE 1 -> back to Idle (EA 0x51cba4/0x51cbb1)
constexpr int32_t kStrEnterInfoFail1347 = 1347; // code 2, type 2 -> Locked (EA 0x51cbf9/0x51cc0e)
constexpr int32_t kStrEnterInfoFail1348 = 1348; // code 3, type 2 -> Locked (EA 0x51cc33/0x51cc48)
constexpr int32_t kStrEnterInfoFail229  = 229;  // code 4, type 2 -> Locked (EA 0x51cc6d/0x51cc82)
constexpr int32_t kStrEnterInfoFail56   = 56;   // code 101, type 2 -> Locked (EA 0x51cca4/0x51ccb9)
constexpr int32_t kStrEnterInfoFail57   = 57;   // code 102, type 2 -> Locked (EA 0x51ccdb/0x51ccf0)

// --- op11 (Net_ConnectGameServer): switch EA 0x51c87e ---
constexpr int32_t kStrConnectFail59 = 59; // code 1, type 2 -> Locked (EA 0x51c8b3/0x51c8c8)
constexpr int32_t kStrConnectFail60 = 60; // code 2, type 2 -> Locked (EA 0x51c8ea/0x51c8ff)
constexpr int32_t kStrConnectFail61 = 61; // code 3, TYPE 1 -> CancelEnter (EA 0x51c921/0x51c92e)
constexpr int32_t kStrConnectFail62 = 62; // code 4, TYPE 1 -> CancelEnter (EA 0x51c9ce/0x51c9db)
constexpr int32_t kStrConnectFail63 = 63; // code 5, TYPE 1 -> CancelEnter (EA 0x51ca7b/0x51ca88)
constexpr int32_t kStrConnectFail64 = 64; // code 6, type 2 -> Locked (EA 0x51cb31/0x51cb46)
constexpr int32_t kStrConnectFail65 = 65; // code 7, type 2 -> Locked (EA 0x51cb68/0x51cb7d)

// this[15705] (+0xF564) — WRITE-ONLY MIRROR (dead field, cf. .h). ⚠ NEVER READ IT.
//
// POLARITY RE-DERIVED FROM THE DISASSEMBLY (this function's older version computed
// the INVERSE and the field was called `allSlotsFull`). Loop EA 0x51bec4-0x51bf0a:
//   0x51bec4  mov [ebp+var_10], 0                       ; i = 0
//   0x51bed6  cmp [ebp+var_10], 3 ; jge short loc_51BF01 ; i >= 3 -> exit
//   0x51bef1  call Crt_Strcmp(&unk_1669394 + 0x2768*i, "")
//   0x51bef9  test eax, eax
//   0x51befb  jz short loc_51BEFF  -> 0x51beff jmp short loc_51BECD  ; EMPTY name  -> CONTINUE
//   0x51befd  jmp short loc_51BF01                                   ; NON-empty name -> EXIT
//   0x51bf01  cmp [ebp+var_10], 3 ; 0x51bf05 jl short loc_51BF14
//   0x51bf0a  mov dword ptr [eax+0F564h], 1
// => we advance over EMPTY slots, exit at the first OCCUPIED one, and the flag only flips to 1
// if ALL 3 SLOTS ARE EMPTY. Hence the rename allSlotsFull -> allSlotsEmpty (.h).
// COUNTER-PROOF: the "free slot?" loop of the CREATE button (EA 0x52524c-0x525289) has the
// INVERSE polarity (`jnz short loc_525287` EA 0x525283 = continue over OCCUPIED slots) —
// that is the one FindFirstFreeSlot() models, and only it.
void RecomputeAllSlotsEmptyMirror(CharSelectState& s) {
    int32_t i = 0;
    for (; i < kMaxCharSlots && !s.slots[static_cast<size_t>(i)].occupied; ++i) {} // EA 0x51befb
    if (i >= kMaxCharSlots) s.allSlotsEmpty = true; // EA 0x51bf01/0x51bf05 -> 0x51bf0a
}

// this[15602] (+0xF3C8) = restore-list entry COUNT — the ONLY field read
// from this block (EA 0x5242ac). EXACT formula (EA 0x51c08d-0x51c153):
//   if (g_ServerModeFlag)  count = (this[15374]==40) ? 1 : 7;   // EA 0x51c13f/0x51c144/0x51c153
//   else                   count = (this[15374]==40) ? 3 : 5;   // EA 0x51c09d/0x51c0a2/0x51c0b1
// The table this[15603..15611] written right after it is NEVER read -> not modeled.
void RecomputeRestoreListCount(CharSelectState& s, const CharSelectHost& host) {
    const bool    serverMode  = host.IsServerModeFlag && host.IsServerModeFlag();
    const int32_t serverIndex = host.GetServerIndex ? host.GetServerIndex() : 0;
    if (serverMode) s.restoreListCount = (serverIndex == 40) ? 1 : 7;
    else            s.restoreListCount = (serverIndex == 40) ? 3 : 5;
}

// PIN wizard — branch `if (dword_16692A4)` EA 0x51beb5. Cf. [A12] in the .h.
// ⚠ The keypad permutation CONSUMES THE SHARED PRNG STREAM (10 draws minimum, more
// on rejection): it must be reproduced so that the background draw (EA 0x51c247) that
// FOLLOWS it lands on the same value as the binary.
void InitPinWizard(CharSelectState& s, const CharSelectHost& host) {
    s.pin.panelOpen = true;   // this[15375] = 1   EA 0x51bf1c
    s.allSlotsEmpty = false;  // this[15705] = 0   EA 0x51bf29 (dead mirror)

    // `Crt_Strcmp(&unk_16692A8, "")` EA 0x51bf3d: non-empty stored PIN -> 2 (VERIFY),
    // empty -> 1 (SET).
    const bool hasStoredPin = host.HasStoredSecondaryPassword && host.HasStoredSecondaryPassword();
    s.pin.mode = hasStoredPin ? 2 : 1; // EA 0x51bf68 / 0x51bf4c
    s.pin.step = 0;                     // this[15377] = 0  EA 0x51bf59
    // TODO [anchor 0x51bf82]: this[15378..15380] = 0 (3 × Crt_StringInit EA 0x51bfb5/
    // 0x51bfcc/0x51bfe2, the 3 char[5] entry fields) — not modeled.

    for (auto& k : s.pin.keypad) k = -1;                 // EA 0x51c008
    for (int32_t i = 0; i < 10; ++i) {                    // EA 0x51c015
        int32_t v;
        do { v = net::DefaultRng().NextMod(10); }         // EA 0x51c043
        while (s.pin.keypad[static_cast<size_t>(v)] != -1); // EA 0x51c054
        s.pin.keypad[static_cast<size_t>(v)] = i;         // EA 0x51c05f
    }
    // TODO [anchor 0x51c06f]: this[15395] = 0 — not modeled.
}

// RECOVERABLE errors: `g_MorphInProgress = 0` THEN reset the preview to Idle.
// EA 0x51c955-0x51c97e, 0x51ca02-0x51ca2b, 0x51cab8-0x51cae1, 0x51cbb6-0x51cbdf.
// Order matters: without resetting the global to 0, the next attempt would fall into
// the anti-replay branch (EA 0x51c6bb) and never emit anything again.
void RecoverToIdle(CharSelectState& s) {
    net::g_MorphInProgress = 0; // 0x1675A88
    ResetPreviewToIdle(s);
}

// "Enter world" network sequence — the BLOCK EA 0x51c707-0x51cd01, in the EXACT ORDER.
// Triggered ONLY by the preview timer (never by the click), and ONLY if
// the g_MorphInProgress anti-replay guard lets it through (guard applied by the caller).
void FireEnterSequence(CharSelectState& s, const CharSelectHost& host) {
    const int32_t slot = s.selectedSlot;
    const CharSlotInfo& rec = s.slots[static_cast<size_t>(slot)];

    // (1) EA 0x51c707 — Crt_Memcpy(g_SelfCharInvBlock 0x1673170,
    //                              &unk_1669380 + 10088*slot, 0x2768u).
    // Sets BOTH the inventory block AND g_LocalElement (= block+0x24 = record[+36]),
    // which then goes out at bytes [137..140] of the op11 handshake (EA 0x462d5d).
    // PROVEN ORDER: 0x51c707 ≺ 0x51c81d (op22) ≺ 0x51c850 ≺ op11 — hence the call at THE TOP.
    if (host.PublishSelfFromSlot) host.PublishSelfFromSlot(slot);

    // (2) EA 0x51c70f — g_MorphInProgress = 1. THIS flag is what makes the sequence
    // non-replayable: on the next frame, the timer is still >= duration, but the
    // guard EA 0x51c6bb then routes to resetting to Idle instead of firing again.
    net::g_MorphInProgress = 1; // 0x1675A88

    // (3) EA 0x51c719-0x51c737 — dword_1675A8C=1, dword_1675A90=0, dword_1675A94=0,
    //     g_SelfMorphNpcId (0x1675A98) = 0.
    // TODO WIRING [anchors 0x51c719/0x51c723/0x51c72d/0x51c737]: these 4 "morph/self"
    // state globals are not reified in ClientSource (only g_MorphInProgress is).
    // No C++ consumer reads them to date -> left unset here rather than invented.

    // (4) EA 0x51c756 — g_TargetZoneId (0x1675A9C) = dword_166A8DC[2522*slot] (record +5468).
    // PRE-SEED: Net_ReqEnterCharInfo receives &g_TargetZoneId as an out-param and only
    // writes it if code==0 (guard `if (!v19)` EA 0x52b2b7). So on failure, the global keeps
    // its local value. No effect on the transition (which only happens on code 0, where the
    // server has already overwritten the value), hence modeling it as a plain starting value.
    int32_t targetZoneId = rec.localZoneId;

    // (5) EA 0x51c765-0x51c7d4 — Crt_Memset(&dword_1675AA0, 0, 0x48) then spawn
    // position at offsets +0x0C/+0x10/+0x14 of the "struct72" block.
    // This block is already modeled and consumed on the Net side (net::BuildEnterWorldTail72,
    // Net/CharSelectPackets.h) from g_World.self.spawnX/Y/Z, themselves populated
    // from slots[enterWorldSlot].localPosX/Y/Z by UI/LoginScene — nothing to set here.

    // (6) EA 0x51c7ed/0x51c7f9 — flt_1675AC4 = (float)(Rng_Next() % 360) ; flt_1675AC8 =
    // flt_1675AC4 (SAME value duplicated, not a 2nd angle).
    // ⚠ DRAW POSITION: HERE, BEFORE Net_ReqEnterCharInfo (EA 0x51c81d) — hence before
    // the op22's 4 nonce draws and the op11's 4. Drawing this angle AFTER the network
    // sequence (what UI/LoginScene.cpp:1194 used to do) yields a DIFFERENT value:
    // the PRNG stream is shared and its order is observable.
    s.enterWorldSpawnRotationDeg = static_cast<float>(net::DefaultRng().NextMod(360));

    // (7) EA 0x51c81d — Net_ReqEnterCharInfo(slot, &domainId, &port, &g_TargetZoneId, &code)
    EnterCharInfoResult info{};
    info.zoneId = targetZoneId; // pre-seed (4) — overwritten by the server if code==0
    if (host.RequestEnterCharInfo) info = host.RequestEnterCharInfo(slot);

    switch (info.resultCode) { // EA 0x51c83c
        case 0: {
            // EA 0x51c850 Net_SelectServerDomain + EA 0x51c866 Net_ConnectGameServer
            const int32_t connectResult = host.ConnectToGameServer
                ? host.ConnectToGameServer(info.domainId, info.gamePort)
                : 101;
            switch (connectResult) { // EA 0x51c87e
                case 0:
                    // EA 0x51c888/0x51c891/0x51c89b — this[0]=5, this[1]=0, this[2]=0.
                    s.pendingTransition = CharSelectTransition::EnterWorld;
                    s.enterWorldZoneId  = info.zoneId; // AUTHENTIC (server)
                    s.enterWorldSlot    = slot;
                    s.subState          = CharSelectSubState::Init; // this[1]=0 EA 0x51c891
                    s.frameCounter      = 0;                        // this[2]=0 EA 0x51c89b
                    return;
                case 1: Notice(host, kStrConnectFail59, NoticeType::Disconnect); Lock(s); return;
                case 2: Notice(host, kStrConnectFail60, NoticeType::Disconnect); Lock(s); return;
                case 3:
                case 4:
                case 5: {
                    // TYPE 1 notice (simple close) then Net_ReqCancelEnter.
                    const int32_t connStr = (connectResult == 3) ? kStrConnectFail61
                                          : (connectResult == 4) ? kStrConnectFail62
                                                                 : kStrConnectFail63;
                    Notice(host, connStr, NoticeType::Close);
                    const int32_t cancelResult = host.CancelEnter ? host.CancelEnter() : 0;
                    if (cancelResult != 0) { // EA 0x51c94b / 0x51c9f8 / 0x51caab
                        if (cancelResult == 101) {
                            // LABEL_92 EA 0x51cae9-0x51cb14: notice 20 type 2 + Locked.
                            Notice(host, kStrSessionExpired, NoticeType::Disconnect);
                            Lock(s);
                        }
                        // ORIGINAL QUIRK REPRODUCED AS-IS: cancelResult != 0 && != 101
                        // -> NO state change at all (the binary literally does
                        // nothing). The preview stays stuck in Entering and
                        // g_MorphInProgress stays at 1 => the anti-replay guard will reset
                        // the preview to Idle on the next frame. This is not a lockup:
                        // it is the exact consequence of the original code.
                    } else {
                        RecoverToIdle(s); // EA 0x51c955-0x51c97e (and analogous sites)
                    }
                    return;
                }
                case 6: Notice(host, kStrConnectFail64, NoticeType::Disconnect); Lock(s); return;
                case 7: Notice(host, kStrConnectFail65, NoticeType::Disconnect); Lock(s); return;
                default: return; // `default: return;` — faithful no-op
            }
        }
        case 1:
            // The ONLY SOFT failure in the whole sequence: TYPE 1 notice, back to Idle,
            // can be retried. EA 0x51cba4-0x51cbdf.
            Notice(host, kStrEnterInfoFail55, NoticeType::Close);
            RecoverToIdle(s);
            return;
        case 2:   Notice(host, kStrEnterInfoFail1347, NoticeType::Disconnect); Lock(s); return;
        case 3:   Notice(host, kStrEnterInfoFail1348, NoticeType::Disconnect); Lock(s); return;
        case 4:   Notice(host, kStrEnterInfoFail229,  NoticeType::Disconnect); Lock(s); return;
        case 101: Notice(host, kStrEnterInfoFail56,   NoticeType::Disconnect); Lock(s); return;
        case 102: Notice(host, kStrEnterInfoFail57,   NoticeType::Disconnect); Lock(s); return;
        default: return; // `default: return;` — faithful no-op
    }
}

// "LIST screen" branch of the Active substate — EA 0x51c488-0x51cd01.
//   if (this[15715] == -1) nothing at all           (guard EA 0x51c4aa)
//   this[15718]==1 (Idle)     -> timer that LOOPS (EA 0x51c4e7-0x51c5bb)
//   this[15718]==3 (Entering) -> timer, then on completion, clamp + anti-replay + sequence
void UpdateListScreen(CharSelectState& s, const CharSelectHost& host, float dt) {
    if (s.selectedSlot == -1) return;                     // EA 0x51c4aa
    if (s.selectedSlot < 0 || s.selectedSlot >= kMaxCharSlots) return; // porting guard

    if (s.previewMotion == PreviewMotion::Idle) {         // EA 0x51c4c1
        s.previewElapsed += dt * 30.0f;                   // EA 0x51c4e7
        const float dur = ListMotionFrameCount(s, host);  // EA 0x51c555
        // LOOP: subtraction, no clamp (EA 0x51c5bb). The binary calls
        // PcModel_ResolveSlotAndApply a 2nd time for the subtraction — same value.
        if (s.previewElapsed >= dur) s.previewElapsed -= dur;
        return;
    }

    if (s.previewMotion != PreviewMotion::Entering) return; // EA 0x51c4c7 (`else if (v18==3)`)

    s.previewElapsed += dt * 30.0f;                        // EA 0x51c5db
    const float dur = ListMotionFrameCount(s, host);       // EA 0x51c649
    if (s.previewElapsed < dur) return;

    // CLAMP to (duration - 1) — EA 0x51c6ae. Done BEFORE the anti-replay test.
    s.previewElapsed = dur - 1.0f;

    // ANTI-REPLAY — EA 0x51c6bb (`cmp g_MorphInProgress, 1`). If a morph is already
    // in progress, do NOT fire again: reset the preview to Idle (EA 0x51c6c0/CD/DC).
    // Faithful consequences: (a) after a fire, the next frame falls back in here and
    // properly resets the preview to Idle; (b) coming back to CharSelect while the global
    // is still at 1 makes the ENTER button inoperative — that is the binary's behavior.
    if (net::g_MorphInProgress == 1) {
        ResetPreviewToIdle(s);
        return;
    }

    FireEnterSequence(s, host); // EA 0x51c707-0x51cd01
}

// "CREATE screen" branch of the Active substate — EA 0x51cd25-0x51ce09.
// The timer LOOPS (no network sequence here), then the two toggles spin
// the preview. ⚠ Rotation exists ONLY on this screen: the list has none.
void UpdateCreateScreen(CharSelectState& s, const CharSelectHost& host, float dt) {
    s.previewElapsed += dt * 30.0f;                        // EA 0x51cd25
    const float dur = CreateMotionFrameCount(s, host);     // EA 0x51cd7a
    if (s.previewElapsed >= dur) s.previewElapsed -= dur;  // LOOP, EA 0x51cdc7

    // this[15724] (+0xF5B0) = YAW. `+= 3.0` if this[15] (EA 0x51cdd0/0x51cde8),
    // `-= 3.0` if this[16] (EA 0x51cdf1/0x51ce09). BOTH tests are independent
    // (no `else`): if both latches are armed, they cancel out exactly.
    if (host.IsRotateLeftLatched  && host.IsRotateLeftLatched())  s.previewRot[1] += 3.0f;
    if (host.IsRotateRightLatched && host.IsRotateRightLatched()) s.previewRot[1] -= 3.0f;
}

// Complete initialization block — EA 0x51bded-0x51c3c7, in the binary's order.
void RunInitBlock(CharSelectState& s, const CharSelectHost& host) {
    // (1) EA 0x51bded-0x51be65 — camera: eye (0,5,-28) / target (0,10,0), written to
    // BOTH singletons (g_GfxRenderer 0x800130..0x800144 THEN g_GxdRenderer
    // 0x18C51C0..0x18C51D4, identical copy EA 0x51be2c-0x51be65).
    // Out of scope for this module (Gfx/) -> set by UI/LoginScene (TODO A14 of the P2 front).

    // (2) EA 0x51be72 — Util_SetClampedU8Field(dword_8E714C, 0).
    // TODO [anchor 0x51be72]: unidentified global UI state, not modeled.

    // (3) EA 0x51be7e — UI_FocusEditBox(&g_UIEditBoxMgr, 0): gives focus back to the
    // parent window (index 0 = no active input field).
    if (host.FocusEditBox) host.FocusEditBox(0);

    // (4) EA 0x51be83-0x51bea4 — `for (i=0;i<150;++i) this[i+3] = 0`: the 150 latches
    // this[3..152] (+12..+608). ⚠ 150, NOT 10: OnMouseDown arms up to this[92].
    if (host.ClearAllButtonLatches) host.ClearAllButtonLatches();

    // (5) EA 0x51beb5 — `if (dword_16692A4)`: PIN wizard OR standard flow (EXCLUSIVE).
    // ⚠ The two branches do NOT consume the same number of PRNG draws: the PIN branch
    // permutes the keypad (>= 10 draws), the standard branch draws none. The background
    // draw (EA 0x51c247) that follows them depends on this.
    // TODO WIRING [anchor 0x51beb5]: as long as host.IsSecondaryPasswordRequired is nullptr,
    // the standard flow is ALWAYS taken — correct for an account without a secondary
    // password, WRONG for one that has one (cf. [A12] in the .h).
    if (host.IsSecondaryPasswordRequired && host.IsSecondaryPasswordRequired()) {
        InitPinWizard(s, host);          // EA 0x51bf1c-0x51c06f
    } else {
        s.pin.panelOpen = false;         // this[15375] = 0   EA 0x51beba
        RecomputeAllSlotsEmptyMirror(s); // this[15705]       EA 0x51bec4-0x51bf0a
    }

    // (6) EA 0x51c07c — this[15396] = 0.
    // TODO [anchor 0x51c07c]: Warehouse panel flag, not modeled.

    // (7) EA 0x51c08d-0x51c1c8 — this[15602] (count, READ) + this[15603..15611] (DEAD
    // table, not modeled).
    RecomputeRestoreListCount(s, host);

    // (8) EA 0x51c1d5-0x51c230 — various fields reset to their init value.
    // TODO [anchors 0x51c1d5/0x51c1ef/0x51c1fc/0x51c209/0x51c216]: this[15703] ("quick
    // class select" panel), this[15706] (Rename), this[15707]/this[15708]/
    // this[15709] (extended list) — subsystems not ported.
    s.restoreListIndex        = -1;    // this[15704] = -1  EA 0x51c1e2
    s.deleteByNamePanelOpen   = false; // this[15711] = 0   EA 0x51c223
    s.deleteByNameListFlag    = 0;     // this[15712] = 0   EA 0x51c230

    // (9) EA 0x51c247-0x51c27f — fullscreen background: `v20 = Rng_Next() % 3` then
    // 2383 / 2384 / 2385. DRAWN ON EVERY ENTRY INTO INIT (the block is immediately followed
    // by this[15714]=1 EA 0x51c28c), not once at scene startup.
    // ⚠ The `default` (EA 0x51c289) WRITES NOTHING — so on an unexpected value, this[15713]
    // KEEPS its previous value. No effect here since Rng_Next()%3 ∈ {0,1,2}, but the
    // structure is reproduced: no catch-all `else`.
    const int32_t bg = net::DefaultRng().NextMod(kCharBgSlotCount); // EA 0x51c247
    if (bg == 0)      s.backgroundSlot = kCharBgSlotFirst;       // 2383, EA 0x51c261
    else if (bg == 1) s.backgroundSlot = kCharBgSlotFirst + 1;   // 2384, EA 0x51c270
    else if (bg == 2) s.backgroundSlot = kCharBgSlotFirst + 2;   // 2385, EA 0x51c27f

    // (10) EA 0x51c28c/0x51c299 — List screen, no selection.
    s.screen       = CharSelectScreen::List; // this[15714] = 1
    s.selectedSlot = -1;                     // this[15715] = -1

    // (11) EA 0x51c2a6/0x51c2b3/0x51c2c0 — this[15398]=0, this[15399]=0, this[15401]=-1.
    // TODO [anchors 0x51c2a6/0x51c2b3/0x51c2c0]: Warehouse panel fields, not modeled.

    // (12) EA 0x51c2ca-0x51c34b — default selection.
    RecomputeDefaultSelection(s);

    // (13) EA 0x51c356-0x51c3b4 — 3D preview fully reset (motion, animState=1,
    // timer, position vec3, rotation vec3).
    ResetPreviewToIdle(s);
    s.previewPos[0] = s.previewPos[1] = s.previewPos[2] = 0.0f; // EA 0x51c37d/88/93
    s.previewRot[0] = s.previewRot[1] = s.previewRot[2] = 0.0f; // EA 0x51c39e/a9/b4

    // (14) EA 0x51c3bd/0x51c3c7 — switch to Active.
    s.subState     = CharSelectSubState::Active;
    s.frameCounter = 0;
}

} // namespace

// Str_ValidateNameChars 0x53FD70 — declared in CharSelectFlow.h (public API, called
// externally by UI/LoginScene_CharSelect.cpp); must have external linkage, hence defined
// here rather than inline in CharSelectFlow_Internal.h.
bool ValidateNameCharset(const std::string& name) {
    // FIXED capacity of 13 WCHAR (== 12 useful characters + NUL), EXACTLY like
    // the original: `MultiByteToWideChar(0, 0, name, -1, &buf, 13)`.
    wchar_t wbuf[13] = {};
    const int written = MultiByteToWideChar(CP_ACP, 0, name.c_str(), -1, wbuf, 13);
    if (written == 0) return false; // invalid encoding OR name > 12 useful characters

    for (const wchar_t* p = wbuf; *p; ++p) {
        const unsigned c = static_cast<unsigned>(*p);
        const bool digit = (c >= 0x30 && c <= 0x39);
        const bool upper = (c >= 0x41 && c <= 0x5A);
        const bool lower = (c >= 0x61 && c <= 0x7A);
        const bool thai  = (c >= 0x0E00 && c <= 0x0E7F);
        if (!digit && !upper && !lower && !thai) return false;
    }
    return true; // faithful: an EMPTY name passes through this loop and returns true here
}

CharSelectTransition UpdateCharSelect(CharSelectState& state, const CharSelectHost& host, float dt) {
    // --- Transitions armed OUTSIDE this function, surfaced FIRST ---
    // The binary writes `this[0]` DIRECTLY from off-tick handlers (BACK button
    // EA 0x525A51 in OnMouseUp; OK click of the mode-2 NoticeDlg EA 0x5C04E4, which isn't
    // even in the scene): the scene switch there is IMMEDIATE, no CharSelect tick runs in
    // between. Since these handlers ALSO set this[1]=0 (EA 0x525A5D / 0x5C04EE), a
    // consumption done further down would be swallowed by `case Init` — hence this test up
    // front, before any substate handling.
    if (state.pendingTransition != CharSelectTransition::None) {
        const CharSelectTransition t = state.pendingTransition;
        state.pendingTransition = CharSelectTransition::None;
        return t;
    }

    // EA 0x51bdac — `v21 = this[1]`: ONLY substates 0 and 1 are handled.
    // Substate 2 (Locked) matches NO case => Update does NOTHING.
    switch (state.subState) {
    case CharSelectSubState::Init: {
        // EA 0x51bde4 — `else if (++this[2] >= 0x1Eu)`. DURING these 30 frames, RENDER
        // draws nothing (guard EA 0x51D238): 1 second of black screen, faithful.
        ++state.frameCounter;
        if (state.frameCounter < kInitWaitFrames) return CharSelectTransition::None;

        // Character data is reread on EVERY Init (the binary directly reads
        // unk_1669394/16693A8/16693B8/166A8DC.. on every pass).
        if (host.LoadCharacterSlots) host.LoadCharacterSlots(state.slots);
        RunInitBlock(state, host);
        return CharSelectTransition::None;
    }
    case CharSelectSubState::Active: {
        // --- DOUBLE BEAT: BOTH `% 30` tests read the SAME already-incremented this[2],
        // so they fall on the SAME FRAME. Strict order (EA 0x51c40b then
        // 0x51c46e), and screen dispatch only comes AFTER.
        ++state.frameCounter; // EA 0x51c40b (`++*(this+2) % 0x1Eu`)

        // (1) Session keep-alive — Net_AccountKeepAlive 0x5298F0 (op12).
        // ⚠ this[2] is NOT reset to 0 by a successful beat: it runs freely and
        // the modulo re-fires every 30 frames.
        if (state.frameCounter % kKeepAliveIntervalFrames == 0) {
            const int32_t keepAlive = host.AccountKeepAlive ? host.AccountKeepAlive() : 0;
            if (keepAlive == 101) {
                // EA 0x51c41d/0x51c42a/0x51c432/0x51c43c — notice 20 of TYPE 2 then
                // Locked. Type 2 is what allows the NoticeDlg's OK click to
                // route back to ServerSelect (cf. OnNoticeDlgMode2Ok).
                Notice(host, kStrSessionExpired, NoticeType::Disconnect);
                Lock(state);
                return CharSelectTransition::None;
            }
        }

        // (2) Anticheat beat — SAME frame, SAME modulo (EA 0x51c46e).
        // `if (Ac_GameGuard_Heartbeat() != 1877) { g_QuitFlag = 1; return; }`
        // (`cmp eax, 755h` EA 0x51c469 -> `g_QuitFlag = 1` EA 0x51c470).
        // Anticheat is out of scope: hook absent => beat deemed successful.
        if (state.frameCounter % kKeepAliveIntervalFrames == 0) {
            const int32_t beat = host.GameGuardHeartbeat ? host.GameGuardHeartbeat()
                                                         : kGameGuardHeartbeatOk;
            if (beat != kGameGuardHeartbeatOk) {
                if (host.RequestQuit) host.RequestQuit(); // g_QuitFlag = 1
                return CharSelectTransition::None;         // no screen dispatch
            }
        }

        // (3) Screen dispatch — `v19 = this[15714]` EA 0x51c488.
        if (state.screen == CharSelectScreen::List)            UpdateListScreen(state, host, dt);
        else if (state.screen == CharSelectScreen::CreateForm) UpdateCreateScreen(state, host, dt);

        // EnterWorld armed during THIS tick (FireEnterSequence, EA 0x51c888 `this[0]=5`):
        // the binary switches scene right away, so it is surfaced WITHOUT waiting for the
        // next tick.
        if (state.pendingTransition != CharSelectTransition::None) {
            const CharSelectTransition t = state.pendingTransition;
            state.pendingTransition = CharSelectTransition::None;
            return t;
        }
        return CharSelectTransition::None;
    }
    case CharSelectSubState::Locked:
    default:
        // FROZEN MODAL (not a dead state): Update inert and mouse inert, but render
        // keeps drawing the full image (guard EA 0x51D238: only substate 0 skips
        // drawing). The ONLY exit is the OK click of a mode-2 NoticeDlg, which
        // arrives OUT OF SCENE -> OnNoticeDlgMode2Ok(); its transition is surfaced by the
        // test at the top of this function.
        return CharSelectTransition::None;
    }
}

// OK click of a MODE 2 NoticeDlg — UI_NoticeDlg_OnLButtonUp 0x5C03F0, case dlg==2
// (EA 0x5C04C9). ONLY exit from the Locked state, and it arrives via a route OUT OF THE
// SCENE (the scene's mouse handlers are gated `== 1` and would never see it):
//   UI_RouteLButtonUp 0x5AD0F0 -> UI_NoticeDlg_OnLButtonUp 0x5C03F0 (unique xref 0x5AD164).
void OnNoticeDlgMode2Ok(CharSelectState& state, const CharSelectHost& host) {
    if (host.CloseConnection) host.CloseConnection();             // EA 0x5C04DF
    state.pendingTransition = CharSelectTransition::ServerSelect; // g_SceneMgr = 2 EA 0x5C04E4
    state.subState          = CharSelectSubState::Init;           // g_SceneSubState = 0 EA 0x5C04EE
    state.frameCounter      = 0;                                  // dword_1676188 = 0 EA 0x5C04F8
}

} // namespace ts2::game

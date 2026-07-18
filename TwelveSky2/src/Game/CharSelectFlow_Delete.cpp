// Game/CharSelectFlow_Delete.cpp — implementation. See Game/CharSelectFlow.h for the full
// doc (original EAs, state machine, [H1]/[A12] corrections). Split family: shared
// validation/preview/recompute helpers live in Game/CharSelectFlow_Internal.h; the
// Init/Active state machine core lives in CharSelectFlow.cpp; the CREATE form lives in
// CharSelectFlow_Create.cpp; slot selection/ENTER/BACK/QUIT live in CharSelectFlow_Nav.cpp.
// This file covers DELETE/RESTORE: OnDeleteButtonClicked, ConfirmDeleteCharacter, the dead
// "delete by name entry" panel (opcode 24, cf. [H1]), ConfirmRestoreCharacter.
#include "Game/CharSelectFlow.h"
#include "Game/CharSelectFlow_Internal.h"

#include "Net/ClientState.h"
#include "Net/Rng.h"

#include <windows.h>

namespace ts2::game {

namespace {

// StrTable005 ids used only by DELETE/RESTORE. Cf. CharSelectFlow_Internal.h for
// kStrEnterNoSelection (shared with ENTER) and CharSelectFlow.cpp for the op11/op22 ids
// kept by the Init/Active core.
constexpr int32_t kStrDeleteSuccess       = 50;
constexpr int32_t kStrDeleteFail51        = 51; // -> Locked
constexpr int32_t kStrDeleteFail411       = 411;
constexpr int32_t kStrDeleteFail48        = 48;
constexpr int32_t kStrDeleteFail633       = 633;
constexpr int32_t kStrDeleteFail2091      = 2091;
constexpr int32_t kStrDeleteFail52        = 52; // -> Locked
constexpr int32_t kStrDeleteFail53        = 53; // -> Locked

// --- opcode 24: DEAD chain (cf. [H1]) — ids kept for the faithful port ---
constexpr int32_t kStrDeleteByNameEmpty   = 1463; // empty name, WITHOUT sending (EA 0x52928c)
constexpr int32_t kStrDeleteByNameOk      = 1464; // code 0   (EA 0x52930b)
constexpr int32_t kStrDeleteByNameFail1   = 1465; // code 1   (EA 0x5293cc)
constexpr int32_t kStrDeleteByNameFail2   = 1468; // code 2   (EA 0x5293f2)
constexpr int32_t kStrDeleteByNameFail3   = 1469; // code 3   (EA 0x529418)
constexpr int32_t kStrDeleteByNameFail4   = 1466; // code 4   (EA 0x52943e)
constexpr int32_t kStrDeleteByNameFail5   = 1470; // code 5   (EA 0x529464)
constexpr int32_t kStrDeleteByNameFail101 = 703;  // code 101 (EA 0x52948a) -> Lock, type 2
constexpr int32_t kStrDeleteByNameFail102 = 704;  // code 102 (EA 0x5294c7) -> Lock, type 2

// --- opcode 18 action=2: restore (CharSelect_ReqRestoreChar 0x5295D0) ---
constexpr int32_t kStrRestoreOk          = 1271; // code 0   (EA 0x52962b)
constexpr int32_t kStrRestoreSessLost1   = 51;   // code 1   (EA 0x52967d) -> Lock, type 2
constexpr int32_t kStrRestoreFail1272    = 1272; // code 2   (EA 0x5296b7)
constexpr int32_t kStrRestoreFail2091    = 2091; // code 5   (EA 0x5296dd)
constexpr int32_t kStrRestoreFail2541    = 2541; // code 11  (EA 0x52970e)
constexpr int32_t kStrRestoreFail2542    = 2542; // code 12  (EA 0x52973f)
constexpr int32_t kStrRestoreFail2545    = 2545; // code 13  (EA 0x529770)
constexpr int32_t kStrRestoreFail2543    = 2543; // code 14  (EA 0x5297a1)
constexpr int32_t kStrRestoreFail2544    = 2544; // code 15  (EA 0x5297d2)
constexpr int32_t kStrRestoreSessLost101 = 52;   // code 101 (EA 0x5297f2) -> Lock, type 2
constexpr int32_t kStrRestoreSessLost102 = 53;   // code 102 (EA 0x529826) -> Lock, type 2

} // namespace

bool OnDeleteButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false; // lock EA 0x525484
    // TODO fidelity [anchor 0x51E046]: the button isn't even DRAWN if
    // this[15374] == 60 — a rendering guard, out of scope for this module.

    if (state.selectedSlot == -1) {
        Notice(host, kStrEnterNoSelection, NoticeType::Close); // notice 47, shared
        return false;
    }

    if (host.ShowDeleteConfirm) host.ShowDeleteConfirm();
    return true;
}

void ConfirmDeleteCharacter(CharSelectState& state, const CharSelectHost& host) {
    if (state.selectedSlot == -1) return;
    const int32_t slot = state.selectedSlot;
    const int32_t code = host.DeleteCharacter ? host.DeleteCharacter(slot) : 101;

    switch (code) {
        case 0:
            Notice(host, kStrDeleteSuccess, NoticeType::Close);
            state.slots[static_cast<size_t>(slot)] = CharSlotInfo{};
            state.selectedSlot = -1;
            // ⚠ NO `allSlotsEmpty = ...` here: that would be an INVENTION. The binary
            // NEVER touches +0xF564 on this path — the 3 OnMouseUp sites write it
            // to *1* (EA 0x5230be/0x52335e/0x5237e9) and the only site that writes 0 is the
            // PIN branch (EA 0x51bf29). The flag therefore stays stuck at its last value,
            // even after a delete that frees up a slot. This is inconsequential (0 proven
            // reads) — and exactly why it must never gate anything.
            return;
        case 1:    Notice(host, kStrDeleteFail51,   NoticeType::Disconnect); Lock(state); return;
        case 2:    Notice(host, kStrDeleteFail411,  NoticeType::Close);      return;
        case 3:    Notice(host, kStrDeleteFail48,   NoticeType::Close);      return;
        case 4:    Notice(host, kStrDeleteFail633,  NoticeType::Close);      return;
        case 5:    Notice(host, kStrDeleteFail2091, NoticeType::Close);      return;
        case 0x65: Notice(host, kStrDeleteFail52,   NoticeType::Disconnect); Lock(state); return;
        case 0x66: Notice(host, kStrDeleteFail53,   NoticeType::Disconnect); Lock(state); return;
        default: return; // faithful no-op
    }
}

// ===========================================================================
// 🔴 "DELETE BY NAME ENTRY" PANEL (opcode 24) — DEAD CHAIN
// ===========================================================================
// These three functions are a FAITHFUL PORT of code UNREACHABLE in the binary (full
// proof at the top of CharSelectFlow.h, [H1]: `var_434` is invariant at 0 across the
// ENTIRE function => 0x52601D, the only write of 1 to +0xF57C, is never executed).
// They must STAY WITHOUT A CALLER — "a dead function in the binary stays dead
// in C++". Do not wire any click to them.
// ===========================================================================

void OpenDeleteByNamePanel(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.screen != CharSelectScreen::List) return;
    // The original guard (`var_430`/`var_434`, EA 0x525f62/0x525f91) is what makes this
    // block unreachable: var_434 == 0 is invariant => notice 2248 fires systematically.

    if (host.ClearDeleteByNameInput) host.ClearDeleteByNameInput(); // EA 0x525fe3
    if (host.FocusEditBox) host.FocusEditBox(19);                   // `push 13h` EA 0x525fcc

    state.deleteByNamePanelOpen = true; // +0xF57C = 1 (EA 0x52601d — NEVER EXECUTED)
    state.deleteByNameListFlag  = 1;    // +0xF580 = 1 (EA 0x52602d)
}

void CancelDeleteByNamePanel(CharSelectState& state) {
    state.deleteByNamePanelOpen = false; // +0xF57C = 0 (EA 0x524ff4)
    state.deleteByNameListFlag  = 0;     // +0xF580 = 0 (EA 0x525004)
}

void ConfirmDeleteCharByName(CharSelectState& state, const CharSelectHost& host) {
    const std::string name = host.GetDeleteByNameInput ? host.GetDeleteByNameInput()
                                                       : std::string{};
    if (name.empty()) {
        Notice(host, kStrDeleteByNameEmpty, NoticeType::Close); // 1463, WITHOUT sending (EA 0x52928c)
        return;
    }

    // slotEnc = *(_BYTE*)(this+62860) + 100 * *(_BYTE*)(this+62848) (EA 0x5292cd):
    // selectedSlot (+0xF58C) and listFlag (+0xF580) read as a BYTE (so -1 -> 255).
    const int32_t slotEnc = static_cast<uint8_t>(state.selectedSlot)
                          + 100 * static_cast<uint8_t>(state.deleteByNameListFlag);
    const int32_t code = host.VerifyCharName ? host.VerifyCharName(slotEnc, name) : 101;

    switch (code) { // EA 0x5292f5
        case 0:
            Notice(host, kStrDeleteByNameOk, NoticeType::Close); // 1464
            state.selectedSlot          = -1;    // EA 0x529348
            state.deleteByNamePanelOpen = false; // EA 0x52939e
            state.deleteByNameListFlag  = 0;     // EA 0x5293ae
            if (host.FocusEditBox) host.FocusEditBox(0); // EA 0x529365
            return;
        case 1: Notice(host, kStrDeleteByNameFail1, NoticeType::Close); return; // 1465
        case 2: Notice(host, kStrDeleteByNameFail2, NoticeType::Close); return; // 1468
        case 3: Notice(host, kStrDeleteByNameFail3, NoticeType::Close); return; // 1469
        case 4: Notice(host, kStrDeleteByNameFail4, NoticeType::Close); return; // 1466
        case 5: Notice(host, kStrDeleteByNameFail5, NoticeType::Close); return; // 1470
        case 101:
            Notice(host, kStrDeleteByNameFail101, NoticeType::Disconnect); // 703
            Lock(state); // EA 0x5294a2/0x5294af
            return;
        case 102:
            Notice(host, kStrDeleteByNameFail102, NoticeType::Disconnect); // 704
            Lock(state); // EA 0x5294df/0x5294ec
            return;
        default:
            // TODO [anchor 0x529503]: notice Crt_Vsnprintf("%s%d", StrTable005(2455), code)
            // — the hook carries neither the formatted string nor the numeric code.
            return;
    }
}

void ConfirmRestoreCharacter(CharSelectState& state, const CharSelectHost& host) {
    const int32_t code = host.RestoreCharacter
        ? host.RestoreCharacter(state.selectedSlot, state.restoreListIndex) // EA 0x5295f6
        : 101;

    switch (code) { // EA 0x529615
        case 0:
            Notice(host, kStrRestoreOk, NoticeType::Close); // 1271
            state.selectedSlot = -1;                        // EA 0x529662
            return;
        case 1:
            Notice(host, kStrRestoreSessLost1, NoticeType::Disconnect); // 51
            Lock(state); // EA 0x529692/0x52969c
            return;
        case 2: Notice(host, kStrRestoreFail1272, NoticeType::Close); return; // 1272
        case 5: Notice(host, kStrRestoreFail2091, NoticeType::Close); return; // 2091
        // Codes 11..15: UI_NoticeDlg_Open(_, 1, body, CAPTION=StrTable005(2546)) — the
        // 4th argument (title) is not carried by the hooks; only the body is rendered.
        case 11: Notice(host, kStrRestoreFail2541, NoticeType::Close); return;
        case 12: Notice(host, kStrRestoreFail2542, NoticeType::Close); return;
        case 13: Notice(host, kStrRestoreFail2545, NoticeType::Close); return;
        case 14: Notice(host, kStrRestoreFail2543, NoticeType::Close); return;
        case 15: Notice(host, kStrRestoreFail2544, NoticeType::Close); return;
        case 101:
            Notice(host, kStrRestoreSessLost101, NoticeType::Disconnect); // 52
            Lock(state); // EA 0x529807/0x529811
            return;
        case 102:
            Notice(host, kStrRestoreSessLost102, NoticeType::Disconnect); // 53
            Lock(state); // EA 0x52983b/0x529845
            return;
        default: return; // faithful no-op
    }
}

} // namespace ts2::game

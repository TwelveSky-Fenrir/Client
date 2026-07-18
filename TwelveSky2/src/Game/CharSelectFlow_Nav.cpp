// Game/CharSelectFlow_Nav.cpp — implementation. See Game/CharSelectFlow.h for the full
// doc (original EAs, state machine, [H1]/[A12] corrections). Split family: shared
// validation/preview/recompute helpers live in Game/CharSelectFlow_Internal.h; the
// Init/Active state machine core (and OnNoticeDlgMode2Ok) live in CharSelectFlow.cpp; the
// CREATE form lives in CharSelectFlow_Create.cpp; DELETE/RESTORE live in
// CharSelectFlow_Delete.cpp. This file covers slot selection and the ENTER/BACK/QUIT
// buttons: SelectCharacterSlot, OnEnterButtonClicked, RequestBackToServerSelect,
// OnQuitButtonClicked.
#include "Game/CharSelectFlow.h"
#include "Game/CharSelectFlow_Internal.h"

#include "Net/ClientState.h"
#include "Net/Rng.h"

#include <windows.h>

namespace ts2::game {

namespace {

// StrTable005 id used only by ENTER. Cf. CharSelectFlow_Internal.h for kStrEnterNoSelection
// (shared with DELETE) and CharSelectFlow.cpp for the op11/op22 ids kept by the
// Init/Active core.
constexpr int32_t kStrEnterInvalidName = 1856; // 0x740, type 1 (EA 0x525117/0x52512e)

} // namespace

void SelectCharacterSlot(CharSelectState& state, int32_t slotIndex) {
    if (state.subState != CharSelectSubState::Active) return; // guards EA 0x520F4D/0x522E70
    if (state.screen != CharSelectScreen::List) return;
    if (state.previewMotion == PreviewMotion::Entering) return; // universal lock
    if (slotIndex < 0 || slotIndex >= kMaxCharSlots) return;
    if (!state.slots[static_cast<size_t>(slotIndex)].occupied) return;
    if (slotIndex == state.selectedSlot) return;

    state.selectedSlot   = slotIndex;
    state.previewElapsed = 0.0f; // EA 0x5226b9
}

// ENTER button — "Time 1" (EA 0x525062-0x5251F0). EMITS NO PACKET: it only
// ARMS the preview. The network sequence fires from the timer (UpdateListScreen).
bool OnEnterButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false; // lock EA 0x5250A2

    if (state.selectedSlot == -1) {
        Notice(host, kStrEnterNoSelection, NoticeType::Close); // notice 47 type 1, EA 0x5250C4
        return false;
    }

    // `cmp g_GmAuthLevel, 1 ; jge` EA 0x5250E2: a GM (level >= 1) SKIPS name
    // validation ENTIRELY — the call to Str_ValidateNameChars isn't even executed.
    const bool gmAuth = host.HasGmAuthLevel && host.HasGmAuthLevel();
    if (!gmAuth) {
        // EA 0x525109 — Str_ValidateNameChars(unk_1685740, &unk_1669394 + 10088*slot).
        const bool nameOk = !host.IsCharacterNameValid // nullptr => true (documented fallback)
                          || host.IsCharacterNameValid(state.selectedSlot);
        if (!nameOk) {
            Notice(host, kStrEnterInvalidName, NoticeType::Close); // notice 1856, EA 0x525117
            return false;
        }
    }

    // EA 0x525156/0x52515B/0x525163 — Weapon_ClassFromField112(g_EquipSnapshotScratch,
    // &unk_16693E8 + 10088*slot) -> class 1..3, then `shl eax,1` and
    // `this[15717] = 2*classe`. unk_16693E8 = record +104 (equipment block); the class
    // is derived from the item ID at +112 of that block (= record +216, the starting weapon).
    state.previewMotionIndex = host.GetEnterPreviewWeaponClass
        ? 2 * host.GetEnterPreviewWeaponClass(state.selectedSlot)
        : 0; // TODO [anchor 0x4CC870]: without the item DB, motion stays 0 (instead of 2/4/6)

    state.previewMotion  = PreviewMotion::Entering; // this[15718] = 3  EA 0x52516F
    state.previewElapsed = 0.0f;                    // this[15719] = 0.0 EA 0x525181

    // EA 0x5251E4/0x5251EB — PcSnd_ResolveEquipSlot(g_ModelMotionArray, rec+40, rec+44,
    // this[15717], 3, 1,0,0,0, 100, 1) then Snd3D_PlayScaledVolume: the weapon sound for
    // entry. This is the ONLY audio call in all of Scene_CharSelectOnMouseUp.
    // Out of scope for this module (Audio/) -> played by UI/LoginScene on a true return.
    return true;
}

// BACK button — EA 0x525A33-0x525A6A. Destination = scene 2 (ServerSelect), NOT Login,
// and WITHOUT any notice.
bool RequestBackToServerSelect(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false; // guard EA 0x525A33

    if (host.CloseConnection) host.CloseConnection(); // Net_CloseSocket EA 0x525A46
    state.pendingTransition = CharSelectTransition::ServerSelect; // this[0]=2 EA 0x525A51
    state.subState          = CharSelectSubState::Init;           // this[1]=0 EA 0x525A5D
    state.frameCounter      = 0;                                  // this[2]=0 EA 0x525A6A
    return true;
}

void OnQuitButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.previewMotion == PreviewMotion::Entering) return; // guard EA 0x525ABE
    if (host.CloseConnectionAndQuit) host.CloseConnectionAndQuit();
}

} // namespace ts2::game

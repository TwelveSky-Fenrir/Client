// Game/CharSelectFlow_Internal.h — shared file-local helpers for the Scene_CharSelectUpdate/
// OnMouseUp split family (CharSelectFlow.cpp / CharSelectFlow_Create.cpp /
// CharSelectFlow_Delete.cpp / CharSelectFlow_Nav.cpp). Included only by that family; not
// part of the public Game/CharSelectFlow.h API. Everything below is inline or
// namespace-local (anonymous namespace => internal linkage per TU, so no ODR risk from
// being included by multiple .cpp files).
#pragma once
#include "Game/CharSelectFlow.h"

namespace ts2::game {

// Str_ValidateNameChars 0x53FD70 — declared in CharSelectFlow.h, DEFINED (non-inline, for
// external linkage) in CharSelectFlow.cpp since it's part of the public API called from
// UI/LoginScene_CharSelect.cpp. Not duplicated here.

namespace {

// ⚠ NO IDA ANCHOR — a porting artifact, NOT a value from the binary. Only reached if
// the host implements NO motion resolution at all (cf. MotionFrameCount). Never use it to
// "cover for" a zero duration: 0 is a legitimate value (anchor 0x4d7854).
constexpr float kDefaultEnterPreviewFrames = 30.0f; // fallback if NO duration hook

// StrTable005 id, SHARED between OnDeleteButtonClicked (CharSelectFlow_Delete.cpp) and
// OnEnterButtonClicked (CharSelectFlow_Nav.cpp) — both fire it on "no slot selected".
constexpr int32_t kStrEnterNoSelection = 47; // 0x2F, type 1 (EA 0x5250C4)

// UI_NoticeDlg_Open(byte_18225C8, type, StrTable005_Get(g_LangId, strId), "") 0x5C0280.
// The TYPE is structural (cf. NoticeType): the typed hook is preferred, falling back
// to the legacy hook only if there is nothing better — in which case the type is LOST.
inline void Notice(const CharSelectHost& host, int32_t strId, NoticeType type) {
    if (host.ShowNoticeTyped) { host.ShowNoticeTyped(strId, type); return; }
    if (host.ShowNotice)      host.ShowNotice(strId);
}

// `*(this+1) = 2 ; *(this+2) = 0` — the EXACT pattern of the 33 locking-error sites
// (13 in Update: 0x51c432, 0x51c8c8, 0x51c8ff, 0x51cb0a, 0x51cb46, 0x51cb7d, 0x51cc0e,
// 0x51cc48, 0x51cc82, 0x51ccb9, 0x51ccf0 … ; 20 in OnMouseUp).
inline void Lock(CharSelectState& s) {
    s.subState     = CharSelectSubState::Locked;
    s.frameCounter = 0;
}

// Resets the preview — pattern `motion=0 ; animState=1 ; timer=0.0` repeated as-is
// at EA 0x51c356/0x51c363/0x51c372 (Init), 0x51c6c0/CD/DC (anti-replay),
// 0x51c962/6F/7E, 0x51ca0f/1C/2B, 0x51cac5/D2/E1, 0x51cbc3/D0/DF (recoverable errors).
inline void ResetPreviewToIdle(CharSelectState& s) {
    s.previewMotionIndex = 0;                    // this[15717]
    s.previewMotion      = PreviewMotion::Idle;  // this[15718] = 1
    s.previewElapsed     = 0.0f;                 // this[15719] = 0.0
}

// PcModel_ResolveSlotAndApply 0x4E5A00 -> the animation's frame count.
// The last 4 arguments are CONSTANT across all 6 call sites: (…, 1, 0, 0, 1).
//
// ⚠ ZERO IS A LEGITIMATE RETURN VALUE OF THE BINARY — DO NOT FILTER IT OUT.
// Full chain: PcModel_ResolveSlotAndApply 0x4E5A00 = `Motion_GetFrameCount(
// PcModel_ResolveEquipSlot(...), a9)` (EA 0x4e5a2b/0x4e5a37), and Motion_GetFrameCount
// 0x4D7830 reads:
//     if (!*(this+152) && !Motion_Load(this, a2)) return 0;   // EA 0x4d784b -> 0x4d7854
//     *(this+108) = g_GameTimeSec; return *(this+140);         // EA 0x4d7861/0x4d786d
// => on an ABSENT/unreadable motion, the binary literally returns 0, and the upstream flow
// handles it as-is: the Idle timer `elapsed >= 0 -> elapsed -= 0` (a loop that no longer
// progresses, EA 0x51c5bb) and the Entering branch `elapsed < 0` false -> fires immediately
// with `elapsed = 0 - 1.0 = -1.0` (EA 0x51c6ae). A fallback of 30.0f here would OVERWRITE a
// PROVEN binary value with an UNANCHORED constant (kDefaultEnterPreviewFrames is just a
// porting artifact): that would be a fidelity regression, not a fix.
// Hence testing hook PRESENCE (not its value): the fallback exists only for
// hosts that do NOT implement motion resolution at all.
// 🔴 OUT-OF-SCOPE RESIDUAL [anchor 0x4d7854]: UI/LoginScene.cpp::GetMotionFrameCount
// returns 0 for TWO distinct reasons — "motion absent" (faithful, mirrors 0x4d7854)
// AND "MotionCache not built" (a porting-specific degradation, outside the binary's model).
// The two are indistinguishable here. The fix is to WIRE the cache (SceneManager/renderer
// default), definitely not to filter the 0 in this function.
inline float MotionFrameCount(const CharSelectHost& host, int32_t modelRace, int32_t modelGender,
                              int32_t motion, PreviewMotion animState, int32_t slotIndex) {
    if (host.GetMotionFrameCount) {
        return static_cast<float>(host.GetMotionFrameCount(
            modelRace, modelGender, motion, static_cast<int32_t>(animState)));
    }
    // Legacy fallback (UI/LoginScene.cpp:1807 still assigns this hook, to nullptr).
    if (host.GetEnterPreviewDurationFrames) return host.GetEnterPreviewDurationFrames(slotIndex);
    return kDefaultEnterPreviewFrames;
}

// Duration of the LIST preview animation: arguments (rec+40 /*race*/,
// rec+44 /*gender*/) — EA 0x51c555 (`dword_16693A8[2522*slot]`, `dword_16693AC[2522*slot]`).
// ⚠ +40, NOT +36: cf. the three-witness proof in CharSlotInfo::race (.h).
inline float ListMotionFrameCount(const CharSelectState& s, const CharSelectHost& host) {
    const CharSlotInfo& rec = s.slots[static_cast<size_t>(s.selectedSlot)];
    return MotionFrameCount(host, rec.race, rec.faction, s.previewMotionIndex,
                            s.previewMotion, s.selectedSlot);
}

// Duration of the CREATION preview animation: arguments (dword_16709DC /*+36*/,
// dword_16709E4 /*+44*/) — EA 0x51cd7a. The creation branch does read +36 (consistent with
// `mov edx,[ecx+24h]` @0x527051 in Char_RenderModel).
inline float CreateMotionFrameCount(const CharSelectState& s, const CharSelectHost& host) {
    return MotionFrameCount(host, s.createForm.job, s.createForm.faction,
                            s.previewMotionIndex, s.previewMotion, -1);
}

// Recomputes the default selection (EA 0x51c299 then loop 0x51c2ca-0x51c34b):
//   this[15715] = -1 ;
//   for (i=0;i<3;++i)
//     if (Crt_Strcmp(name_i, "")) {                       // occupied
//       if (this[15715] == -1) this[15715] = i;
//       else if (dword_16693B8[2522*i] > dword_16693B8[2522*this[15715]]) this[15715] = i;
//     }
// STRICTLY-greater comparison (`jle` EA 0x51c343) => on a tie, the FIRST one wins.
inline void RecomputeDefaultSelection(CharSelectState& s) {
    s.selectedSlot = -1; // EA 0x51c299
    for (int32_t i = 0; i < kMaxCharSlots; ++i) {
        const CharSlotInfo& cur = s.slots[static_cast<size_t>(i)];
        if (!cur.occupied) continue;
        if (s.selectedSlot == -1) {
            s.selectedSlot = i; // EA 0x51c317
        } else if (cur.power > s.slots[static_cast<size_t>(s.selectedSlot)].power) {
            s.selectedSlot = i; // EA 0x51c34b
        }
    }
}

} // namespace
} // namespace ts2::game

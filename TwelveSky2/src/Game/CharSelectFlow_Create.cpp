// Game/CharSelectFlow_Create.cpp — implementation. See Game/CharSelectFlow.h for the full
// doc (original EAs, state machine, [H1]/[A12] corrections). Split family: shared
// validation/preview/recompute helpers live in Game/CharSelectFlow_Internal.h; the
// Init/Active state machine core lives in CharSelectFlow.cpp; DELETE/RESTORE live in
// CharSelectFlow_Delete.cpp; slot selection/ENTER/BACK/QUIT live in CharSelectFlow_Nav.cpp.
// This file covers the CREATE form: OnCreateButtonClicked, the five Set/Create* arrow
// pairs, ConfirmCreateCharacter, CancelCreateForm.
#include "Game/CharSelectFlow.h"
#include "Game/CharSelectFlow_Internal.h"

#include "Net/ClientState.h"
#include "Net/Rng.h"

#include <windows.h>

namespace ts2::game {

namespace {

// StrTable005 ids used only by the CREATE form. Cf. CharSelectFlow_Internal.h for
// kStrEnterNoSelection (shared with DELETE/ENTER) and CharSelectFlow.cpp for the
// op11/op22 ids kept by the Init/Active core.
constexpr int32_t kStrCreateListFull      = 22;   // 0x16, type 1 (EA 0x525294/0x5252a1/0x5252a8)
// 0x873 — CREATION FORBIDDEN ON THIS REGION. type 1 (`push 1` EA 0x5252df), notice
// EA 0x5252e6 then RETURN EA 0x5252eb. Guard: this[15374]==40 AND !g_ServerModeFlag.
constexpr int32_t kStrCreateRegionBlocked = 2163;
constexpr int32_t kStrCreateEmptyName     = 38; // GetWindowTextA==0, EA 0x5265a5
constexpr int32_t kStrCreateInvalidChars  = 39; // !Str_ValidateNameChars, EA 0x5265db
constexpr int32_t kStrCreateBannedWord    = 40; // maybe_Dict001_MatchWord, EA 0x526611
constexpr int32_t kStrCreateSuccess       = 41;
constexpr int32_t kStrCreateFail42        = 42; // -> Locked
constexpr int32_t kStrCreateFail43        = 43;
constexpr int32_t kStrCreateFail701       = 701;
constexpr int32_t kStrCreateFail44        = 44; // -> Locked
constexpr int32_t kStrCreateFail45        = 45; // -> Locked

// EXACT table of the STARTING WEAPON id (record +216 = dword_1670A90), read off
// 0x5266CD-0x526760: outer switch on dword_16709DC (job) -> 3 blocks, each an inner
// switch on this[15716] (variant, `[this+0xF590]` EA 0x526671/0x5266C7/0x52671A).
// Written values: 5/6/7 (job 0, EA 0x52669A/0x5266A6/0x5266B2), 11/12/13 (job 1,
// EA 0x5266F0/0x5266FC/0x526708), 17/18/19 (job 2, EA 0x526743/0x52674F/0x52675B).
// Closed form: 6*job + variant + 5 (bounds 5..19).
// ⚠ THIS IS NOT a "lookPresetId": +216 is an ITEM ID, resolved in the item DB
// by MobDb_GetEntry(mITEM) (EA 0x5274A3), just like the 8 other equipment slots
// (+0x78, +0x88, +0xB8, +0xE8, +0xF8, +0x108, +0x118, +0x128).
int32_t ResolveStartingWeaponItemId(int32_t job, int32_t variant) {
    static constexpr int32_t kTable[3][3] = {
        { 5,  6,  7},
        {11, 12, 13},
        {17, 18, 19},
    };
    if (job < 0 || job > 2 || variant < 0 || variant > 2) return 0; // normally unreachable
    return kTable[job][variant];
}

int32_t FindFirstFreeSlot(const CharSelectState& s) {
    for (int32_t i = 0; i < kMaxCharSlots; ++i) {
        if (!s.slots[static_cast<size_t>(i)].occupied) return i;
    }
    return -1;
}

// The binary's arrows do not "clamp": they BAIL OUT DOING NOTHING at the bound
// (`if (field == 0) return` for −, `if (field == max) return` for +). Effect identical to
// a clamp for delta = ±1, but the shape is reproduced: no write happens at the bound.
inline bool StepField(int32_t& field, int32_t delta, int32_t lo, int32_t hi) {
    const int32_t next = field + delta;
    if (next < lo || next > hi) return false;
    field = next;
    return true;
}

} // namespace

void OnCreateButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.screen != CharSelectScreen::List) return;
    if (state.previewMotion == PreviewMotion::Entering) return; // lock EA 0x52523E

    // (1) EA 0x52524c-0x525289 — "is there a free slot left?". The loop advances over
    // OCCUPIED slots (`jnz short loc_525287` EA 0x525283) and exits at the first EMPTY one;
    // var_28==3 (EA 0x525289) = none free -> notice 22 type 1 (EA 0x525294/0x5252a1) then
    // RETURN (`jmp loc_526B7C` EA 0x5252ad). ⚠ INVERSE polarity of loop 0x51bec4 (cf.
    // RecomputeAllSlotsEmptyMirror): do not confuse the two.
    if (FindFirstFreeSlot(state) == -1) {
        Notice(host, kStrCreateListFull, NoticeType::Close);
        return;
    }

    // (2) BLOCKING REGIONAL GUARD — EA 0x5252b2-0x5252eb, RE-DISASSEMBLED:
    //   0x5252b8  cmp dword ptr [eax+0F038h], 28h  ; this[15374] == 40 ?
    //   0x5252bf  jnz short loc_5252F0             ; != 40 -> open the form
    //   0x5252c1  cmp ds:g_ServerModeFlag, 0       ; global 0x166918C
    //   0x5252c8  jnz short loc_5252F0             ; server mode -> open anyway
    //   0x5252cf  push 873h (=2163) ... 0x5252df push 1 ... 0x5252e6 UI_NoticeDlg_Open
    //   0x5252eb  jmp loc_526B7C                   ; RETURN
    // => blocks IFF (serverIndex == 40 && !g_ServerModeFlag).
    // ⚠ DO NOT CONFUSE with the guard cited by this block's older TODO
    // (EA 0x51dfa5 `== 60` / 0x51dfb8 `== 50`): that one is in Scene_CharSelectRender
    // 0x51CED0 and only HIDES THE DRAWING of the button — it's a rendering matter
    // (UI/LoginScene), not flow. The two guards coexist and are distinct.
    // Fallback of both hooks (nullptr): si=0, sm=false -> inert guard, which is
    // exactly the binary's behavior at this[15374]==0.
    const int32_t serverIndex = host.GetServerIndex ? host.GetServerIndex() : 0;
    const bool    serverMode  = host.IsServerModeFlag && host.IsServerModeFlag();
    if (serverIndex == 40 && !serverMode) {
        Notice(host, kStrCreateRegionBlocked, NoticeType::Close);
        return;
    }

    // (3) EA 0x5252f7 — Util_SetClampedU8Field(dword_8E714C, 0), the SAME call as Init
    // (EA 0x51be72). TODO [anchor 0x5252f7]: unidentified global UI state, not modeled.

    // (4) EA 0x525303 — UI_FocusEditBox(&g_UIEditBoxMgr, 3) (`push 3` EA 0x5252fc): the
    // form's NAME field gets keyboard focus on open.
    if (host.FocusEditBox) host.FocusEditBox(3);

    // (5) TODO [anchor 0x525314]: SetWindowTextA(dword_1668FCC, "") — clears the name-entry
    // WIDGET (the model itself is cleared in (7) by `CharCreateForm{}`). No host hook
    // exposes this SetWindowTextA (GetEditedName only READS): not manufacturing
    // a hook nobody would assign.

    // (6) EA 0x52531a-0x525346 — `for (i=0;i<150;++i) this[i+3] = 0` (`cmp var_28, 96h`
    // EA 0x52532c ; `mov [ecx+eax*4+0Ch], 0` EA 0x52533e): the CREATE button purges the 150
    // button latches, EXACTLY like Init (EA 0x51be83-0x51bea4).
    if (host.ClearAllButtonLatches) host.ClearAllButtonLatches();

    // (7) EA 0x52534e-0x525396 — switch to the creation screen then a fresh record, in
    // the binary's order: screen=2 (0x52534e) ; variant=0 (0x52535e) ; name="" (0x525368) ;
    // job=Rng_Next()%3 (0x52537c) ; gender=0 (0x525382) ; face=0 (0x52538c) ;
    // hair=0 (0x525396).
    state.screen     = CharSelectScreen::CreateForm;
    state.createForm = CharCreateForm{};
    state.createForm.job = host.RandomInitialJob ? (host.RandomInitialJob() % 3) : 0;

    // (8) EA 0x5253a6-0x52541c — 3D preview FULLY reset, exactly like the
    // Init block (EA 0x51c356-0x51c3b4): motion, animState=1, timer, THEN the position
    // vec3 and rotation vec3 (6 `fldz`/`fstp` that ResetPreviewToIdle does not cover).
    ResetPreviewToIdle(state);                                  // EA 0x5253a6/0x5253b6/0x5253c8
    state.previewPos[0] = state.previewPos[1] = state.previewPos[2] = 0.0f; // EA 0x5253d6/e4/f2
    state.previewRot[0] = state.previewRot[1] = state.previewRot[2] = 0.0f; // EA 0x525400/40e/41c
    // RETURN — `jmp loc_526B7C` EA 0x525422.
}

void SetCreateJob(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    // − : `if (dword_16709DC == 0) return;` EA 0x52609b ; `sub ecx,1` EA 0x5260b2.
    // + : `if (dword_16709DC == 2) return;` EA 0x526141 ; `add ecx,1` EA 0x526155.
    if (!StepField(state.createForm.job, delta, 0, 2)) return;

    // The job resets ALL of its sub-options — identical block on both arrows
    // (− : EA 0x5260b8-0x5260ee ; + : EA 0x526158-0x526182 and following):
    state.createForm.faction   = 0;    // dword_16709E4 = 0  EA 0x5260b8
    state.createForm.face      = 0;    // dword_16709E8 = 0  EA 0x5260c2
    state.createForm.hairColor = 0;    // dword_16709EC = 0  EA 0x5260cc
    state.createForm.variant   = 0;    // this[15716]  = 0  EA 0x5260dc / 0x526182
    state.previewElapsed       = 0.0f; // this[15719]  = 0.0 EA 0x5260ee
}

void SetCreateFaction(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    StepField(state.createForm.faction, delta, 0, 1); // EA 0x5261F8 / 0x526280
}

void SetCreateFace(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    StepField(state.createForm.face, delta, 0, 6); // EA 0x526305 / 0x52636B
}

void SetCreateHairColor(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    StepField(state.createForm.hairColor, delta, 0, 2); // EA 0x5263D1 / 0x52643A
}

void SetCreateVariant(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    // this[15716] — guards `cmp ...,0` EA 0x526490 (−) and `cmp ...,2` EA 0x52650B (+).
    StepField(state.createForm.variant, delta, 0, 2);
}

bool ConfirmCreateCharacter(CharSelectState& state, const CharSelectHost& host) {
    if (state.screen != CharSelectScreen::CreateForm) return false;

    // EXACT order of the 3 notices (EA 0x52658f-0x526623): empty -> 38 ; rejected
    // characters -> 39 ; banned word -> 40. The hard 12-character bound is part of
    // ValidateNameCharset (conversion failure on the 13-WCHAR buffer).
    const std::string name = host.GetEditedName ? host.GetEditedName() : std::string{};
    if (name.empty()) {
        Notice(host, kStrCreateEmptyName, NoticeType::Close); // notice 38
        return false;
    }
    if (host.ValidateNameChars && !host.ValidateNameChars(name)) {
        Notice(host, kStrCreateInvalidChars, NoticeType::Close); // notice 39
        return false;
    }
    if (host.IsNameBanned && host.IsNameBanned(name)) {
        Notice(host, kStrCreateBannedWord, NoticeType::Close); // notice 40, EA 0x5265FC
        return false;
    }

    state.createForm.name = name;
    const int32_t slot = FindFirstFreeSlot(state);
    if (slot == -1) {
        Notice(host, kStrCreateListFull, NoticeType::Close);
        return false;
    }

    // record +216 = starting weapon = 6*job + variant + 5 (EA 0x52669A..0x52675B).
    const int32_t weaponItemId = ResolveStartingWeaponItemId(state.createForm.job,
                                                             state.createForm.variant);
    const int32_t code = host.CreateCharacter
        ? host.CreateCharacter(slot, state.createForm, weaponItemId)
        : 101;

    switch (code) {
        case 0:
            Notice(host, kStrCreateSuccess, NoticeType::Close);
            // The net::g_CharRecords mirror is updated by net::CreateCharacter from
            // the op17 echo (EA 0x52a71e); the next Init will reread the records.
            state.slots[static_cast<size_t>(slot)].occupied = true;
            state.screen       = CharSelectScreen::List;
            state.selectedSlot = slot;
            ResetPreviewToIdle(state);
            return true;
        case 1:    Notice(host, kStrCreateFail42,  NoticeType::Disconnect); Lock(state); return false;
        case 2:    Notice(host, kStrCreateFail43,  NoticeType::Close);      return false;
        case 3:    Notice(host, kStrCreateFail701, NoticeType::Close);      return false;
        case 0x65: Notice(host, kStrCreateFail44,  NoticeType::Disconnect); Lock(state); return false;
        case 0x66: Notice(host, kStrCreateFail45,  NoticeType::Disconnect); Lock(state); return false;
        default: return false; // faithful no-op
    }
}

void CancelCreateForm(CharSelectState& state) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    state.screen = CharSelectScreen::List; // this[15715] (selection) UNCHANGED, faithful
    ResetPreviewToIdle(state);
}

} // namespace ts2::game

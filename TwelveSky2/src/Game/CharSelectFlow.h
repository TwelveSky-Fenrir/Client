// Game/CharSelectFlow.h — state flow for the CHARACTER SELECTION screen (scene 4).
//
// Faithful C++ rewrite of the FLOW/DECISION logic (list, selection, creation, deletion,
// enter world) of:
//   Scene_CharSelectUpdate      0x51BD90  (state machine — the CORE of this module)
//   Scene_CharSelectOnMouseDown 0x520F40  (button latches)
//   Scene_CharSelectOnMouseUp   0x522E50  (click validation + network requests)
//   Scene_CharSelectRender      0x51CED0  (pure D3D9 — ported by UI/LoginScene.cpp)
// Associated network functions: Net_CreateCharacter 0x52A4A0 (op17), Net_CharSlotAction
// 0x52A740 (op18), Net_ReqEnterCharInfo 0x52B070 (op22), Net_ReqCancelEnter 0x52B310
// (op23), Net_ReqVerifyCharName 0x52B4C0 (op24), Net_AccountKeepAlive 0x5298F0 (op12),
// Net_ConnectGameServer 0x462A70 (op11), Net_SelectServerDomain 0x53FE90.
//
// MEMORY IDENTITY (proven): the scene's `this` IS the globals block 0x1676180,
// so this[N] ≡ 0x1676180 + 4*N. this[0]=scene id, this[1]=substate, this[2]=
// frame counter.
//
// ===========================================================================
// STATE MACHINE (this[1]) — ASYMMETRY OF THE 4 ENTRY POINTS
// ===========================================================================
//   Substate 0 (Init)   : Update counts 30 frames then initializes everything (EA 0x51bde4:
//     `++this[2] >= 0x1Eu`) -> this[1]=1 (EA 0x51c3bd), this[2]=0 (EA 0x51c3c7).
//     DURING these 30 frames RENDER DRAWS NOTHING (guard EA 0x51D238: End2D +
//     Present + return) = 1 second of BLACK screen, and the mouse is inert.
//   Substate 1 (Active)  : everything is active (the only state where mouse handlers
//     pass — guards `cmp [eax+4],1` EA 0x520F4D and 0x522E70).
//   Substate 2 (Locked) : Update DOES NOTHING (EA 0x51bdac: only 0 and 1 are
//     handled) and BOTH mouse handlers return immediately — BUT render still
//     draws EVERYTHING (frozen image). This is a FROZEN MODAL, not a dead state.
//     OUT-OF-SCENE ESCAPE (proven): the NoticeDlg receives its clicks via an ALTERNATE
//     route from the scene — UI_RouteLButtonUp 0x5AD0F0 -> UI_NoticeDlg_OnLButtonUp
//     0x5C03F0 (unique xref EA 0x5AD164). On OK of a MODE 2 NoticeDlg (EA 0x5C04C9):
//       Net_CloseSocket(&g_NetClient) 0x5C04DF ; g_SceneMgr=2 (ServerSelect) 0x5C04E4 ;
//       g_SceneSubState=0 0x5C04EE ; dword_1676188=0 0x5C04F8.
//     -> Hence NoticeType (below): the notice's TYPE determines whether the Locked
//        state is a dead end (mode 1) or an exit to ServerSelect (mode 2).
//        UI/LoginScene is responsible for routing this click (cf. wiring TODO NOTICEDLG_MODE2).
//
// ===========================================================================
// TWO DOCUMENTATION CORRECTIONS (this file's older comments were WRONG)
// ===========================================================================
// [H1] "Delete by name entry" (opcode 24): this is NOT a "second delete mechanism
//   with double confirmation" — the chain is ENTIRELY DEAD.
//   PROOF (re-established over the ENTIRE function [0x522E50, 0x526B90), not a
//   sub-block): the stack slot `var_434` has only 3 references in the WHOLE function —
//   the frame prologue (0x522E50), ONE write `mov [ebp+var_434], 0` (0x525DFA)
//   and ONE `cmp [ebp+var_434], 0` (0x525F91). NO non-zero write, NO `lea`
//   (hence no pointer aliasing) => `var_434 == 0` is INVARIANT => the `jnz`
//   (0x525FC0) is NEVER taken => 0x52601D — the ONLY write of 1 to +0xF57C in the
//   whole game image (`search_text 0F57Ch` over [0x401000,0x6D7234) = 7 hits, 4 writes,
//   the other 3 to 0) — is UNREACHABLE. The panel therefore NEVER opens and
//   opcode 24 is NEVER emitted by this client.
//   => WIRE NO CLICK to OpenDeleteByNamePanel/ConfirmDeleteCharByName. The code
//      below is kept (faithful port, behaviorally correct since it has no
//      caller) — rule "a dead function in the binary stays dead in C++".
//
// [A12] The `dword_16692A4` wizard is NOT a "test/GM account flag".
//   PROOF: `data_refs(0x16692A4)` = 6 refs. The sole producer is Net_LoginRequest
//   0x51B8E0, EA 0x51BBE7: `Crt_Memcpy(&dword_16692A4, &g_NetClient.recvBuf + 0x95, 4)`.
//   The payload starts at recvBuf+1, so this is the field at OFFSET 148 OF THE LOGIN
//   RESPONSE. The other 4 refs are `mov ds:dword_16692A4, 0` in Net_AccountReq_op13/
//   14/15/16 (the SECONDARY PASSWORD opcodes). It is therefore a PIN/secondary password
//   screen MANDATORY for any account that has one set — not a GM overlay.
//   The state is modeled minimally below (CharSelectPinState); opcodes 13/14/15
//   are NOT wired (anchored TODO).
//
// SCOPE: pure flow/decision logic. No rendering, no D3D9. Every side effect
// (network, UI, keyboard focus, character data) goes through CharSelectHost —
// EXCEPT two client state globals already reified elsewhere and used directly by the
// .cpp, because they are the SAME globals the binary touches, and the fidelity of the
// PRNG flow and the anti-replay guard depend on it:
//   net::g_MorphInProgress (Net/ClientState.h) = g_MorphInProgress 0x1675A88
//   net::DefaultRng()      (Net/Rng.h)          = Rng_Next 0x7603FD (shared _holdrand)
// Established precedent: Game/ComboPickupTick.h and UI/LoginScene.cpp already do exactly this.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ts2::game {

// Number of character slots in the standard flow. CONFIRMED by THREE independent
// `i<3` loops: initial occupancy (EA 0x51bec4), default selection
// (EA 0x51c2ca), and the slot render loop (`cmp var_20, 3` EA 0x51D526).
inline constexpr int32_t kMaxCharSlots = 3;

// Size of the raw character record (0x2768). EA 0x51c707 (`Crt_Memcpy(..., 0x2768u)`).
inline constexpr int32_t kCharRecordSize = 10088;

// Internal substates of Scene_CharSelectUpdate (= this[1]). Cf. the asymmetry note above.
enum class CharSelectSubState : int32_t {
    Init   = 0, // Update counts 30 frames then initializes; RENDER = BLANK SCREEN; mouse inert
    Active = 1, // only interactive state
    Locked = 2, // Update inert + mouse inert, but RENDER STILL FULL (frozen image) = frozen modal.
                // Exit ONLY via the OK click of a mode-2 NoticeDlg (out of scene).
};

// NoticeDlg type — 2nd argument of UI_NoticeDlg_Open 0x5C0280. DETERMINES THE FLOW:
// UI_NoticeDlg_OnLButtonUp 0x5C03F0 is what applies the effect on OK.
enum class NoticeType : int32_t {
    Close      = 1, // simple close — the scene STAYS in its current state
    Disconnect = 2, // Net_CloseSocket + scene 2 (ServerSelect) + substate 0 (EA 0x5C04DF-0x5C04F8)
    Quit       = 3, // g_QuitFlag=1 — never used by CharSelect
};

// Active screen in the Active substate (this[15714], +0xF588, EXACT values 1/2).
enum class CharSelectScreen : int32_t {
    List       = 1, // list of 3 slots + column of 10 buttons
    CreateForm = 2, // creation wizard
};

// 3D preview animation state = this[15718] (+0xF598). NAME CAUTION: this field
// is the `animState` passed to PcModel_ResolveSlotAndApply/Char_RenderModel, NOT the motion
// index (that is this[15717], cf. CharSelectState::previewMotionIndex).
// CharSelect ONLY ever uses values 1 and 3 (this[15718]=1 at Init EA 0x51c363;
// =3 when ENTER is armed EA 0x52516F). Value 0 is NEVER used here.
enum class PreviewMotion : int32_t {
    Idle     = 1, // cosmetic loop (the timer LOOPS, EA 0x51c4e7-0x51c5bb)
    Entering = 3, // entry armed; on timer completion -> network sequence
};

// UNIVERSAL LOCK (this[15718] == 3): once entry is armed, ALL list buttons bail
// out before doing anything — ENTER 0x5250A2, CREATE 0x52523E, DELETE
// 0x525484, RENAME 0x525544, BACK 0x525A33, QUIT 0x525ABE, RESTORE 0x525B5A,
// button8 0x525CA4. The whole screen is frozen. Atomicity guard for the transition.

// Scene transition ready to be applied by the caller (consumed once).
enum class CharSelectTransition : int32_t {
    None,
    EnterWorld,   // this[0]=5 (EA 0x51c888); see enterWorld*
    ServerSelect, // this[0]=2 (EA 0x525A51) — BACK button; cf. RequestBackToServerSelect
};

// A character slot. Offsets are those of the raw 10088-byte record
// (base unk_1669380 + 10088*i), NAMED AFTER THE MEMCPY AT 0x51c707 which copies the
// selected slot's record into g_SelfCharInvBlock 0x1673170 (so record[+N] ≡ 0x1673170+N).
struct CharSlotInfo {
    // occupancy: `Crt_Strcmp(&unk_1669394 + 10088*i, "") != 0` — the SINGLE and
    // UNIVERSAL rule (EA 0x51bec4/0x51c2f7; `data_refs(0x1669394)` = 10 refs, all within
    // the 4 CharSelect functions; no other discriminant exists).
    bool    occupied = false;
    std::string name;         // +20  (unk_1669394) — 13 bytes max server-side

    // +56 (dword_16693B8) — default-selection criterion (STRICT `>` comparison,
    // EA 0x51c343). MISLEADING NAME KEPT to avoid breaking callers: the value
    // is actually the character's LEVEL (it feeds g_SelfLevel 0x16731A8).
    int32_t power = 0;

    // +36 — "job/class" field. This is the field EMITTED by the creation form
    // (dword_16709DC) and the SENTINEL tested `== 3` by the LIST branch of
    // Char_RenderModel (EA 0x52754A). This is NOT the race index used by the list render.
    int32_t job = 0;

    // +40 (dword_16693A8) — EFFECTIVE RACE FOR LIST RENDER AND SOUND.
    // ⚠ DISTINCT from `job` (+36). THREE-WITNESS CONVERGING PROOF:
    //   1. Char_RenderModel 0x527020 has TWO branches (`cmp [ebp+arg_4],0 ; jz` @0x52702F):
    //      the CREATION branch reads +36 (`mov edx,[ecx+24h]` @0x527051), the LIST branch reads
    //      +40 (`mov eax,[edx+28h]` @0x527536) — both pass +44 as gender.
    //   2. Scene_CharSelectUpdate calls PcModel_ResolveSlotAndApply with
    //      dword_16693A8[2522*slot] (=+40) and dword_16693AC[2522*slot] (=+44) — EA 0x51c555.
    //   3. The ENTER button's sound passes the SAME 16693A8/16693AC (EA 0x5251E4).
    // The LIST branch ALSO reads +36, but ONLY as the `==3` sentinel (0x52754A):
    // the two fields coexist with distinct roles — this is not a copy-paste.
    // +40 is NEVER written by the client (`data_refs(0x16709E0)` = 0 refs): the server
    // fills it in (op17 echo, EA 0x52A71E).
    // TO BE POPULATED by net::ParseCharRecord from offset 40 (cf. TODO K1 in the Net/ front).
    int32_t race = 0;

    int32_t faction = 0;      // +44 (dword_16693AC) — MISLEADING NAME KEPT: it is the
                              // GENDER (0..1). PcModel_ResolveEquipSlot 0x4E46A0 clamps
                              // a3>1 (EA 0x4E46CC). The emission offset is correct.
    int32_t face      = 0;    // +48 (0..6)
    int32_t hairColor = 0;    // +52 (0..2)

    // +104 (unk_16693E8) — 208-byte equipment block. Its +112 field (= record +216) is
    // the ITEM ID OF THE STARTING WEAPON, resolved in the item DB (`MobDb_GetEntry(mITEM)`
    // EA 0x5274A3). Consumed by Weapon_ClassFromField112 0x4CC870 (EA 0x525156) to
    // derive the weapon class -> the entry animation's motion index.
    // Not reified here: exposed via CharSelectHost::GetEnterPreviewWeaponClass.

    int32_t localZoneId = 0;  // +5468 (dword_166A8DC) — pre-seed for g_TargetZoneId 0x1675A9C
                              // (EA 0x51c756). On op22 code 0 the SERVER overwrites it
                              // (Net_ReqEnterCharInfo only writes its out-params
                              // under `if (!v19)`, EA 0x52b2b7-0x52b2ec).
    float   localPosX = 0.0f; // +5472 (dword_166A8E0) — int32 -> float via `fild` (EA 0x51c79e)
    float   localPosY = 0.0f; // +5476 (dword_166A8E4) — EA 0x51c7b9
    float   localPosZ = 0.0f; // +5480 (dword_166A8E8) — EA 0x51c7d4
};

// The creation form = the dword_16709B8 record (also a raw 10088-byte record).
// EXHAUSTIVE INVENTORY of the fields the binary writes (everything else stays ZERO):
//   name       byte_16709CC +20  <- GetWindowTextA(hwnd, buf, 13)  EA 0x526583/0x52658F
//   job/race   dword_16709DC +36 <- Rng_Next()%3 EA 0x52537C ; arrows 0x5260B2/0x526158
//   gender     dword_16709E4 +44 <- 0 EA 0x525382 ; arrows 0x5261F8/0x526280
//   face       dword_16709E8 +48 <- 0 EA 0x52538C ; arrows 0x526305/0x52636B
//   hair       dword_16709EC +52 <- 0 EA 0x525396 ; arrows 0x5263D1/0x52643A
//   equipment  unk_1670A20 +104 <- Crt_Memset(...,0,0xD0) EA 0x526634
//   weapon     dword_1670A90 +216 <- 6*job + variant + 5   EA 0x52669A..0x52675B
// +56 is NEVER written by the form. Neither is +40 (the server fills it in).
struct CharCreateForm {
    int32_t job       = 0; // dword_16709DC (+36), 0..2
    int32_t faction   = 0; // dword_16709E4 (+44), 0..1 — this is GENDER (name kept as-is)
    int32_t face      = 0; // dword_16709E8 (+48), 0..6
    int32_t hairColor = 0; // dword_16709EC (+52), 0..2

    // `variant` IS NOT IN THE RECORD: it is the SCENE field this[15716] (+0xF590),
    // bounds 0..2 (guards `cmp ...,0` EA 0x526490 and `cmp ...,2` EA 0x52650B). It is
    // kept here for modeling convenience, but its lifecycle is that of a scene
    // field: it only reaches the network THROUGH the weapon (+216 = 6*job+variant+5).
    // It is NOT reset to zero at scene Init (`search_text 0F590h` = 16 hits,
    // NONE in Scene_CharSelectUpdate) but IS reset by the CREATE button (EA 0x52535E)
    // AND by BOTH job arrows (EA 0x5260DC and 0x526182).
    int32_t variant   = 0;
    std::string name;      // read by host.GetEditedName()
};

// --- Form LABEL helpers (Scene_CharSelectRender 0x51CED0) ---
// All FIVE fields are LOCALIZED TEXT (no raw "%d" in the binary).
// job (0..2)     -> StrTable005_Get(g_LangId, 23/24/25).            EA 0x51e548-0x51e5c9
inline int32_t CreateJobLabelStrId(int32_t job) { return 23 + job; }
// faction (0..1) -> StrTable005_Get(g_LangId, 26/27).                EA 0x51e60b-0x51e64e
inline int32_t CreateFactionLabelStrId(int32_t faction) { return 26 + faction; }
// variant (0..2) -> 3x3 grid indexed by (job,variant): 29+3*job+variant. EA 0x51e76c-0x51e93e
// DISTINCT from the NETWORK id of the weapon preset (5..19): two independent grids.
inline int32_t CreateVariantLabelStrId(int32_t job, int32_t variant) { return 29 + 3 * job + variant; }
// face AND hairColor share `sprintf("%c %s", 'A'+value, StrTable005_Get(g_LangId,28))`
// EA face 0x51e690-0x51e6f8, EA hairColor 0x51e6fd-0x51e767 (SAME fixed word id 28).
inline constexpr int32_t kCreateFaceHairLabelWordStrId = 28;
inline char CreateFaceHairLabelLetter(int32_t value) { return static_cast<char>('A' + value); }

// Result of Net_ReqEnterCharInfo (opcode 22, 17-byte response = [1][code:4][domainId:4]
// [gamePort:4][zoneId:4]). ⚠ The 3 out-params are written ONLY if code==0 (guard
// `if (!v19)` EA 0x52b2b7) — on a non-zero code, domainId/gamePort/zoneId keep the
// caller's pre-existing value.
struct EnterCharInfoResult {
    int32_t resultCode = 101; // 0=ok ; 1=SOFT failure (back to Idle) ; 2/3/4/101/102=locking
    int32_t domainId   = 0;   // -> Net_SelectServerDomain 0x53FE90
    int32_t gamePort   = 0;   // -> Net_ConnectGameServer 0x462A70
    int32_t zoneId     = 0;   // AUTHENTIC (overwrites the local pre-seed, cf. CharSlotInfo)
};

// --- Integration points (network/UI/data), nullptr = documented safe fallback ---
struct CharSelectHost {
    // Populates `slots` on scene entry (mirrors the binary's direct reads of unk_1669394/
    // dword_16693A8/16693AC/16693B8/166A8DC..). Wired to
    // net::LoadCharacterSlotsFromRecords. nullptr => slots unchanged.
    std::function<void(std::array<CharSlotInfo, kMaxCharSlots>&)> LoadCharacterSlots;

    // Net_AccountKeepAlive 0x5298F0 (op12, /30 frames). 101 = session expired.
    // nullptr => treated as alive.
    std::function<int32_t()> AccountKeepAlive;

    // Ac_GameGuard_Heartbeat 0x6DE3F7, called every 30 frames IN THE SAME FRAME as the
    // keepalive (EA 0x51c46e). The binary compares the return value to 1877 (`cmp eax, 755h`
    // EA 0x51c469) and sets g_QuitFlag=1 otherwise (EA 0x51c470).
    // Anticheat is OUT OF SCOPE (CLAUDE.md): nullptr => kGameGuardHeartbeatOk,
    // i.e. "the heartbeat always succeeds". The flow's STRUCTURE is faithful,
    // only the verdict is neutralized.
    std::function<int32_t()> GameGuardHeartbeat;

    // g_QuitFlag = 1 (0x815590). Used by the GameGuard failure (EA 0x51c470) AND the
    // QUIT button. nullptr => no-op.
    std::function<void()> RequestQuit;

    // Str_ValidateNameChars 0x53FD70 on the slot's STORED name (EA 0x525109) —
    // ENTER precondition. nullptr => true.
    std::function<bool(int32_t slotIndex)> IsCharacterNameValid;

    // g_GmAuthLevel >= 1 (0x1669294) — `cmp g_GmAuthLevel, 1 ; jge` EA 0x5250E2: a GM
    // SKIPS name validation entirely. nullptr => false.
    std::function<bool()> HasGmAuthLevel;

    // UI_NoticeDlg_Open(byte_18225C8, type, StrTable005_Get(g_LangId,strId), "") 0x5C0280.
    // PREFERRED over ShowNotice: the TYPE decides whether Locked is a dead end
    // (mode 1) or an exit to ServerSelect (mode 2), cf. NoticeType.
    // If nullptr, falls back to ShowNotice (the type is then LOST -> the Locked state
    // becomes a dead end: fidelity gap, cf. wiring TODO NOTICEDLG_MODE2).
    std::function<void(int32_t strId, NoticeType type)> ShowNoticeTyped;

    // Legacy variant WITHOUT type. Kept for hosts already wired
    // (UI/LoginScene.cpp:1732). Used only if ShowNoticeTyped is absent.
    std::function<void(int32_t strId)> ShowNotice;

    // UI_MsgBox_Open(2, StrTable005_Get(g_LangId,49), "") : Yes/No delete confirmation.
    // The host MUST call back ConfirmDeleteCharacter() on "Yes" (the binary
    // routes this via UI_MsgBox_OnLButtonUp 0x5C0A90 -> CharSelect_ReqDeleteChar 0x528FD0).
    std::function<void()> ShowDeleteConfirm;

    // Str_ValidateNameChars 0x53FD70 on the name TYPED during creation. Faithful
    // implementation provided: ValidateNameCharset(). nullptr => true (not faithful, test hosts).
    std::function<bool(const std::string&)> ValidateNameChars;

    // maybe_Dict001_MatchWord(g_BannedWordList, typedName) 0x4C1410 (EA 0x5265FC) —
    // true = banned. nullptr => false (not faithful, test hosts).
    std::function<bool(const std::string&)> IsNameBanned;

    // GetWindowTextA(dword_1668FCC, buf, 13) — name typed into the creation form.
    std::function<std::string()> GetEditedName;

    // Rng_Next()%3 (EA 0x52537C) — random initial job when the form opens.
    // ⚠ CONSUMES THE SHARED PRNG STREAM: must draw from net::DefaultRng(). nullptr => 0
    // (and the PRNG stream SHIFTS by one draw relative to the binary).
    std::function<int32_t()> RandomInitialJob;

    // Clears the 150 button latches this[3..152] (+12..+608) on every Init —
    // loop `for (i=0;i<150;++i) this[i+3]=0` EA 0x51be83-0x51bea4 (`cmp var,96h`).
    // ⚠ THIS IS NOT 10: OnMouseDown arms latches up to this[92]. The latches are
    // UI/LoginScene Widgets, out of scope for this module => hook.
    // nullptr => no-op (latches stay armed across visits: fidelity gap).
    std::function<void()> ClearAllButtonLatches;

    // UI_FocusEditBox(&g_UIEditBoxMgr, index) 0x50F4A0. PROVEN indices: 0 at Init
    // (EA 0x51be7e), 19 when opening the dead panel (EA 0x525fcc), 0 on op24 success
    // (EA 0x529365). `UI_FocusEditBox` ONLY does SetFocus.
    std::function<void(int32_t editBoxIndex)> FocusEditBox;

    // PcModel_ResolveSlotAndApply 0x4E5A00 — returns the animation's FRAME COUNT.
    // The binary calls it with DIFFERENT arguments per screen (EA 0x51c555 for
    // the list, EA 0x51cd7a for creation):
    //   LIST    : (g_ModelMotionArray, rec+40 /*race*/, rec+44 /*gender*/, motion, animState, 1,0,0,1)
    //   CREATION : (g_ModelMotionArray, dword_16709DC /*+36*/, dword_16709E4 /*+44*/, motion, animState, 1,0,0,1)
    // Hence the generic signature below. The last 4 arguments are CONSTANT.
    // nullptr => falls back to GetEnterPreviewDurationFrames then kDefaultEnterPreviewFrames.
    std::function<int32_t(int32_t modelRace, int32_t modelGender, int32_t motion, int32_t animState)>
        GetMotionFrameCount;

    // OBSOLETE — kept because UI/LoginScene.cpp:1807 still assigns it (to nullptr).
    // Fallback of GetMotionFrameCount, used ONLY for the ENTER animation.
    std::function<float(int32_t slotIndex)> GetEnterPreviewDurationFrames;

    // Weapon_ClassFromField112(g_EquipSnapshotScratch, &unk_16693E8 + 10088*slot) 0x4CC870
    // (EA 0x525156) -> weapon class 1..3, read from the item ID at rec+104+112 (=rec+216).
    // The entry animation's motion index is derived from it: `shl eax,1` (EA 0x52515B)
    // then `this[15717] = 2*classe` (EA 0x525163).
    // nullptr => TODO [anchor 0x4CC870]: previewMotionIndex stays at 0 (the entry animation
    // then plays motion 0 instead of 2/4/6). Requires the item DB (Game/GameDatabase,
    // out of scope for this front).
    std::function<int32_t(int32_t slotIndex)> GetEnterPreviewWeaponClass;

    // Latches for the TWO creation-preview rotation toggles: this[15] (EA
    // 0x51cdd0) and this[16] (EA 0x51cdf1). LoginScene widgets => hooks.
    // nullptr => false (no rotation).
    std::function<bool()> IsRotateLeftLatched;
    std::function<bool()> IsRotateRightLatched;

    // `dword_16692A4 != 0` (EA 0x51beae) — is the SECONDARY PASSWORD wizard
    // required? Cf. [A12] above: this global comes from the LOGIN RESPONSE (offset 148,
    // `Crt_Memcpy(&dword_16692A4, &recvBuf + 0x95, 4)` EA 0x51BBE7) and is reset to 0 by
    // opcodes 13/14/15/16. It is NOT a GM/test-account flag.
    // TODO WIRING [anchor 0x51BBE7]: dword_16692A4 is not reified on the Net/ side (Login.cpp
    // does not retain this response field). nullptr => false = standard flow, which is
    // correct for an account WITHOUT a secondary password but WRONG for one that has one.
    std::function<bool()> IsSecondaryPasswordRequired;

    // `Crt_Strcmp(&unk_16692A8, "") != 0` (EA 0x51bf3d) — is a PIN ALREADY set on the
    // account? unk_16692A8 = 5 bytes copied from the login response (recvBuf+0x99 => payload
    // +152, EA 0x51bbfb). Decides the wizard's mode: true -> VERIFY (2), false ->
    // SET (1). nullptr => false.
    std::function<bool()> HasStoredSecondaryPassword;

    // g_ServerModeFlag (dword_166918C, = GameConfig::buildVariant) — EA 0x51c08d.
    // nullptr => false.
    std::function<bool()> IsServerModeFlag;

    // this[15374] (+0xF038) — flat server index (observed values 40/50/60).
    // EA 0x51c09d/0x51c13f. nullptr => 0.
    std::function<int32_t()> GetServerIndex;

    // Net_CreateCharacter(slot, record, &code) 0x52A4A0 (op17).
    std::function<int32_t(int32_t slotIndex, const CharCreateForm& form, int32_t lookPresetId)> CreateCharacter;

    // CharSelect_ReqDeleteChar 0x528FD0 -> Net_CharSlotAction(slot,1,0,&code) (op18).
    std::function<int32_t(int32_t slotIndex)> DeleteCharacter;

    // CharSelect_ReqRestoreChar 0x5295D0 -> Net_CharSlotAction(slot,2,listIndex,&code)
    // (op18, EA 0x5295f6). `listIndex` = this[15704] (+0xF560).
    // TODO WIRING [anchor 0x5295f6]: hook not wired; the restore list that
    // feeds restoreListIndex is not modeled (cf. CharSelectState).
    std::function<int32_t(int32_t slotIndex, int32_t listIndex)> RestoreCharacter;

    // Net_ReqVerifyCharName(slotEnc, name, &code) 0x52B4C0 (op24).
    // 🔴 DEAD CHAIN — NEVER WIRE (cf. [H1] above).
    std::function<int32_t(int32_t slotEnc, const std::string& name)> VerifyCharName;

    // GetWindowTextA(dword_166900C, String, 49) 0x529273 — EDIT field of the DEAD panel
    // (≠ GetEditedName, which reads dword_1668FCC over 13 bytes). 🔴 DEAD CHAIN.
    std::function<std::string()> GetDeleteByNameInput;

    // SetWindowTextA(dword_166900C, "") EA 0x525fe3. 🔴 DEAD CHAIN.
    std::function<void()> ClearDeleteByNameInput;

    // Publishes the chosen character's identity into world state BEFORE the game
    // server handshake. Mirrors the SINGLE memcpy at EA 0x51c707:
    //   Crt_Memcpy(g_SelfCharInvBlock /*0x1673170*/, &unk_1669380 + 10088*slot, 0x2768u)
    // This memcpy sets BOTH the inventory block AND g_LocalElement (= block+0x24 =
    // record[+36] = the `job` field), which then goes on the wire at bytes [137..140] of
    // Net_ConnectGameServer's op11 auth packet 0x462A70 (EA 0x462d5d).
    // PROVEN ORDER: 0x51c707 (memcpy) ≺ 0x51c81d (op22) ≺ 0x51c850 ≺ op11.
    // 🔴 TODO WIRING [anchor 0x51c707] — NOT WIRED to date (GAMEAUTH_Element_Zero gap):
    // UI/LoginScene.cpp::BuildCharSelectHost must set
    //   charHost_.PublishSelfFromSlot = [](int32_t slot) {
    //       game::g_World.self.element = charState_.slots[slot].job;  // record +36
    //       /* + copy net::g_CharRecords[slot] into g_World.self.charInvBlock */ };
    // As long as this hook is nullptr, the handshake's bytes [137..140] stay at 0.
    std::function<void(int32_t slotIndex)> PublishSelfFromSlot;

    // Net_ReqEnterCharInfo(slot,&domainId,&port,&zoneId,&code) 0x52B070 (op22).
    std::function<EnterCharInfoResult(int32_t slotIndex)> RequestEnterCharInfo;

    // Net_SelectServerDomain(domainId,&host) 0x53FE90 (EA 0x51c850) +
    // Net_ConnectGameServer(&g_NetClient,host,port,&code) 0x462A70 (EA 0x51c866), folded together.
    std::function<int32_t(int32_t domainId, int32_t gamePort)> ConnectToGameServer;

    // Net_ReqCancelEnter(&code) 0x52B310 (op23) — EA 0x51c93c/0x51c9e9/0x51ca96.
    std::function<int32_t()> CancelEnter;

    // Net_CloseSocket(&g_NetClient) 0x463000 + g_QuitFlag=1 — QUIT button.
    std::function<void()> CloseConnectionAndQuit;

    // Net_CloseSocket(&g_NetClient) 0x463000 ALONE — BACK button (EA 0x525A46), before
    // the transition to ServerSelect. nullptr => no-op.
    std::function<void()> CloseConnection;
};

// Success value of the GameGuard heartbeat (`cmp eax, 755h` EA 0x51c469).
inline constexpr int32_t kGameGuardHeartbeatOk = 1877;

// Atlas slots for the fullscreen background (EA 0x51c261/0x51c270/0x51c27f). Declared BEFORE
// CharSelectState: kCharBgSlotFirst serves as a default member initializer.
inline constexpr int32_t kCharBgSlotFirst = 2383;
inline constexpr int32_t kCharBgSlotCount = 3;

// --- PIN / secondary password wizard state (branch dword_16692A4 != 0) ---
// Cf. [A12] above: MANDATORY screen for any account with a secondary password
// set, NOT a GM overlay. Modeled MINIMALLY (the state, not the interactions).
struct CharSelectPinState {
    bool    panelOpen = false; // this[15375] (+0xF03C) — 1 = PIN wizard active (EA 0x51bf1c)
    // this[15376] (+0xF040) — 1 = SET a PIN (none stored), 2 = VERIFY.
    // EXACT discriminant: `Crt_Strcmp(&unk_16692A8, "")` (EA 0x51bf3d) where unk_16692A8 is
    // the 5-byte PIN copied from the login response (recvBuf+0x99 => payload +152,
    // EA 0x51bbfb). Non-empty -> 2 (EA 0x51bf68); empty -> 1 (EA 0x51bf4c).
    int32_t mode      = 0;
    int32_t step      = 0; // this[15377] (+0xF044) = 0 (EA 0x51bf59)
    // RANDOM permutation of the numeric keypad this[15385..15394] (+0xF064..+0xF088):
    // init to -1 (EA 0x51c008) then rejection-fill `do v=Rng_Next()%10 while(occupied)`
    // (EA 0x51c015-0x51c05f). ⚠ CONSUMES THE SHARED PRNG STREAM — reproduced as-is.
    std::array<int32_t, 10> keypad{};
    // TODO [anchors 0x529AA0 / 0x529D20 / 0x529FB0]: opcodes 13 (verify), 14
    // (change), 15 (set) are NOT wired — the wizard is an inert state.
    // TODO [anchor 0x51bf82]: this[15378..15380] (3 × char[5] at this+61524/61529/61534,
    // the 3 entry fields) and this[15395] (EA 0x51c06f) not modeled.
};

// --- Complete state of the CharSelect screen ---
struct CharSelectState {
    CharSelectSubState subState = CharSelectSubState::Init;
    int32_t frameCounter = 0; // this[2] — ⚠ NOT reset to 0 by a successful keepalive

    CharSelectScreen screen = CharSelectScreen::List; // this[15714] (+0xF588)

    std::array<CharSlotInfo, kMaxCharSlots> slots{};
    int32_t selectedSlot = -1; // this[15715] (+0xF58C), -1 = none

    // this[15705] (+0xF564) — 🔴 DEAD FIELD: `search_text 0F564h` over [0x401000,0x6D7234)
    // = 5 hits, 5 WRITES (0x51bf0a=1, 0x51bf29=0, 0x5230be=1, 0x52335e=1, 0x5237e9=1),
    // ZERO READS. Kept as a faithful WRITE-ONLY mirror.
    //
    // ⚠ NAME CORRECTED — it used to be called `allSlotsFull`: that was EXACTLY BACKWARDS.
    //   The loop EA 0x51bec4-0x51bf0a advances over EMPTY slots and EXITS at the first
    //   OCCUPIED one: `Crt_Strcmp(&unk_1669394 + 0x2768*i, "")` EA 0x51bef1 ; `test eax,eax`
    //   EA 0x51bef9 ; `jz short loc_51BEFF` EA 0x51befb -> `jmp short loc_51BECD` (i++,
    //   CONTINUE on EMPTY name) ; otherwise `jmp short loc_51BF01` EA 0x51befd (NON-empty
    //   name = OCCUPIED slot -> EXIT). So `cmp [ebp+var_10], 3` EA 0x51bf01 + `jl` EA 0x51bf05
    //   falls through to `mov [eax+0F564h], 1` EA 0x51bf0a ONLY if ALL 3 SLOTS ARE
    //   EMPTY. The flag means "no character on the account", not "list full".
    //   COUNTER-PROOF in the SAME image: the "is any slot still free?" loop of the
    //   CREATE button (EA 0x52524c-0x525289) is the EXACT MIRROR of polarity — its
    //   `jnz short loc_525287` EA 0x525283 CONTINUES over OCCUPIED slots and exits
    //   at the first EMPTY one. The two loops are therefore deliberately opposite: this is not
    //   a copy-paste, and neither can serve as a template for the other.
    // ⚠ NEVER READ IT nor let it gate anything (rule "dead stays dead"):
    //   the CREATE button tests for the presence of a free slot (EA 0x52524c), not this flag.
    bool allSlotsEmpty = false;

    // this[15713] (+0xF584) — atlas slot for the fullscreen BACKGROUND, DRAWN ON EVERY INIT:
    // `v20 = Rng_Next()%3` (EA 0x51c247) -> 2383 (0x51c261) / 2384 (0x51c270) / 2385
    // (0x51c27f). The block is INSIDE Init (immediately followed by
    // this[15714]=1 EA 0x51c28c and this[15715]=-1 EA 0x51c299).
    // ⚠ The `default` (EA 0x51c289) WRITES NOTHING — no effect since Rng_Next() >= 0, but
    // do NOT "fix" this by adding a default case.
    // The draw happens HERE (not in LoginScene::Init) for two reasons: temporal
    // fidelity AND synchronization of the shared PRNG stream.
    // -> UI/LoginScene must READ this field, not re-draw it (wiring TODO CHARBG_SLOT).
    int32_t backgroundSlot = kCharBgSlotFirst;

    // --- 3D preview ---
    // this[15717] (+0xF594) — MOTION INDEX. 0 at Init (EA 0x51c356); 2*weapon class
    // when ENTER is armed (EA 0x525163). ⚠ DISTINCT from previewMotion (= this[15718]).
    int32_t previewMotionIndex = 0;
    PreviewMotion previewMotion  = PreviewMotion::Idle; // this[15718] (+0xF598) = animState
    float         previewElapsed = 0.0f;                // this[15719] (+0xF59C), in frames
    // this[15720..15722] (+0xF5A0..A8) — preview position vec3, reset to 0 at Init.
    float previewPos[3] = {0.0f, 0.0f, 0.0f};
    // this[15723..15725] (+0xF5AC..B4) — rotation vec3. YAW is this[15724] (+0xF5B0)
    // (`fld dword ptr [ecx+4]` EA 0x527076 on the rotation block).
    float previewRot[3] = {0.0f, 0.0f, 0.0f};

    // Spawn angle (flt_1675AC4 AND flt_1675AC8 — SAME value duplicated, NOT two
    // angles). `Rng_Next()%360` DRAWN IN THE ENTRY BLOCK (EA 0x51c7ed), BEFORE
    // Net_ReqEnterCharInfo (EA 0x51c81d) — hence BEFORE the op22's 4 nonce draws
    // and the op11's 4. The PRNG stream order depends on it.
    // -> UI/LoginScene must READ this field, not re-draw it (wiring TODO SPAWN_ROT).
    float enterWorldSpawnRotationDeg = 0.0f;

    // --- PIN wizard (cf. [A12]) ---
    CharSelectPinState pin{};

    // --- "Delete by name entry" panel (opcode 24) ---
    // 🔴 UNREACHABLE (cf. [H1] above): these two fields stay at their init value.
    // Kept as a faithful port of fields +0xF57C / +0xF580 (EA 0x51c223 / 0x51c230).
    bool    deleteByNamePanelOpen = false; // this[15711] (+0xF57C)
    int32_t deleteByNameListFlag  = 0;     // this[15712] (+0xF580) — ×100 multiplier

    // this[15704] (+0xF560) — selection index in the restore list, sent
    // as the op18 action=2 `arg` (EA 0x5295f6). Init to -1 (EA 0x51c1e2).
    // TODO [anchors 0x524232-0x524250 / 0x5242ac-0x5242d8]: the LIST itself is not
    // modeled (prev/next arrows that clamp into [0, count-1]).
    int32_t restoreListIndex = -1;

    // this[15602] (+0xF3C8) — restore list entry COUNT. This is the ONLY
    // field READ from this block (EA 0x5242ac). The table this[15603..15611] that follows it
    // is written (EA 0x51c160-0x51c1c8 / 0x51c0be-0x51c126) but NEVER read: NOT modeled
    // (rule "dead stays dead"). EXACT formula, cf. RecomputeRestoreListCount().
    // ⚠ WRITE-ONLY MIRROR ON THE C++ SIDE: the binary reads it @0x5242ac (the
    //   RESTORE screen), which is NOT ported — the whole chain is dead here
    //   (host.RestoreCharacter never assigned, button 3086 not wired). Do not read this field
    //   until this chain is ported; its value also depends on two hooks that are
    //   currently nullptr (IsServerModeFlag/GetServerIndex), so it is not reliable.
    int32_t restoreListCount = 5;

    CharCreateForm createForm{};

    // Transition consumable once by UpdateCharSelect().
    CharSelectTransition pendingTransition = CharSelectTransition::None;
    int32_t enterWorldZoneId = 0;
    int32_t enterWorldSlot   = -1;
};

// Str_ValidateNameChars 0x53FD70 — FAITHFUL reproduction. The original:
//   1. MultiByteToWideChar(CP_ACP, 0, name, -1, buf, 13) — FIXED 13-WCHAR buffer
//      (12 useful chars + NUL): fails if the conversion fails, which INCLUDES "name
//      too long". There is NO other length guard in the binary — the buffer's
//      capacity serves as both the encoding AND the hard 12-character bound.
//   2. sub_760F03 = wcslen(buf).
//   3. EVERY wide character must fall in one of 4 EXACT ranges: '0'-'9'
//      (0x30-0x39), 'A'-'Z' (0x41-0x5A), 'a'-'z' (0x61-0x7A), or the Thai block
//      U+0E00-U+0E7F (present as-is in the EU binary — reproduced without interpretation).
// ASSUMED BEHAVIOR QUIRK (faithful, not a porting bug): an EMPTY name passes through the
// loop with zero iterations and returns TRUE. It is NOT this function that rejects empty:
// it is the caller (EA 0x52658f-0x5265b7, `GetWindowTextA(...) == 0` -> notice 38).
bool ValidateNameCharset(const std::string& name);

// Scene_CharSelectUpdate 0x51BD90. Call once per frame (30 FPS) while the scene is
// CharSelect. Returns the pending transition (None most of the time).
CharSelectTransition UpdateCharSelect(CharSelectState& state, const CharSelectHost& host, float dt);

// Click on an occupied list slot. No-op if empty/out of bounds/already selected.
void SelectCharacterSlot(CharSelectState& state, int32_t slotIndex);

// CREATE button (List screen). Opens the form if a slot is free
// (notice[22] otherwise); initial job drawn via host.RandomInitialJob.
void OnCreateButtonClicked(CharSelectState& state, const CharSelectHost& host);

// −/+ button pairs of the form. `delta` = -1 or +1. EXACT bounds [0,2]/[0,1]/
// [0,6]/[0,2]/[0,2] — the binary BAILS OUT DOING NOTHING at the bounds (`if (job==0) return`
// EA 0x52609b for −, `if (job==2) return` EA 0x526141 for +); it does not "clamp".
// ⚠ SetCreateJob resets gender/face/hair **AND `variant`** AND the timer.
void SetCreateJob(CharSelectState& state, int32_t delta);
void SetCreateFaction(CharSelectState& state, int32_t delta);
void SetCreateFace(CharSelectState& state, int32_t delta);
void SetCreateHairColor(CharSelectState& state, int32_t delta);
void SetCreateVariant(CharSelectState& state, int32_t delta);

// CONFIRM button of the form. EXACT order of the 3 notices (EA 0x52658f-0x526623):
// empty -> 38 ; rejected chars -> 39 ; banned word -> 40. Then the weapon id (job,variant)
// and Net_CreateCharacter on the first free slot.
bool ConfirmCreateCharacter(CharSelectState& state, const CharSelectHost& host);

// CANCEL button of the form. Back to List, selection UNCHANGED (faithful).
void CancelCreateForm(CharSelectState& state);

// DELETE button (List screen). Opens the confirmation via host.ShowDeleteConfirm().
bool OnDeleteButtonClicked(CharSelectState& state, const CharSelectHost& host);

// "Yes" click of the confirmation opened by OnDeleteButtonClicked().
void ConfirmDeleteCharacter(CharSelectState& state, const CharSelectHost& host);

// --- "Delete by name entry" panel (opcode 24) ---
// 🔴 DEAD CHAIN, UNREACHABLE IN THE BINARY (full proof above, [H1]).
// Faithful port KEPT but must STAY WITHOUT A CALLER. Do not wire any click to it.
void OpenDeleteByNamePanel(CharSelectState& state, const CharSelectHost& host);
void CancelDeleteByNamePanel(CharSelectState& state);
void ConfirmDeleteCharByName(CharSelectState& state, const CharSelectHost& host);

// "Yes" click of the RESTORE confirmation (CharSelect_ReqRestoreChar 0x5295D0).
// No caller until the restore list is ported.
void ConfirmRestoreCharacter(CharSelectState& state, const CharSelectHost& host);

// ENTER button (List screen). Precondition: a slot selected, not already entering,
// and (stored name valid OR GM). On success arms previewMotion=Entering: the network
// sequence fires from the TIMER (inside UpdateCharSelect), NEVER here — faithful (EA 0x52516F).
bool OnEnterButtonClicked(CharSelectState& state, const CharSelectHost& host);

// BACK button (List screen) -> scene 2 (ServerSelect), NOT Login(3), and WITHOUT a notice.
// Exit guard `this[15718]==3` (EA 0x525A33) then Net_CloseSocket (EA 0x525A46),
// this[0]=2 (EA 0x525A51), this[1]=0 (EA 0x525A5D), this[2]=0 (EA 0x525A6A).
// Returns true if the transition was armed (to consume via UpdateCharSelect).
bool RequestBackToServerSelect(CharSelectState& state, const CharSelectHost& host);

// QUIT button (List screen). No-op if entry is in progress (guard EA 0x525ABE).
void OnQuitButtonClicked(CharSelectState& state, const CharSelectHost& host);

// Applies the effect of the OK click of a MODE 2 NoticeDlg (UI_NoticeDlg_OnLButtonUp
// 0x5C03F0 case 2). This is the ONLY exit from the Locked state, and it arrives via an
// OUT-OF-SCENE route (UI_RouteLButtonUp 0x5AD0F0 -> 0x5C03F0, unique xref EA 0x5AD164):
// the scene's mouse handlers are gated `== 1` and would never see it.
// EXACT effects: Net_CloseSocket (0x5C04DF), scene 2 (0x5C04E4), substate 0 (0x5C04EE),
// counter 0 (0x5C04F8).
// -> UI/LoginScene must call this from its NoticeDlg routing (wiring TODO NOTICEDLG_MODE2).
void OnNoticeDlgMode2Ok(CharSelectState& state, const CharSelectHost& host);

} // namespace ts2::game

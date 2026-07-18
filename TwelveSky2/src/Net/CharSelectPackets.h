// Net/CharSelectPackets.h — BLOCKING network requests for the character-select
// screen (scene 4), on the LOGIN socket still active (before ConnectGameServer /
// WSAAsyncSelect — cf. Net/Login.cpp: the same blocking send()+recv() scheme is
// already used there for LoginRequest). Network integration point for
// Game/CharSelectFlow.h::CharSelectHost, called from UI/LoginScene.cpp.
//
// Original functions (renamed in the IDB `RE/TwelveSky2.exe.i64`, names confirmed
// via `lookup_funcs` — see also Docs/TS2_CHARSELECT_AUDIT.md and
// Game/CharSelectFlow.h, which already documented these same EAs):
//   Net_AccountKeepAlive 0x5298F0 (opcode 12)
//   Net_CreateCharacter  0x52A4A0 (opcode 17 / 0x11)
//   Net_CharSlotAction   0x52A740 (opcode 18 / 0x12), via CharSelect_ReqDeleteChar 0x528FD0
//   Net_ReqEnterCharInfo 0x52B070 (opcode 22 / 0x16)
//   Net_ReqCancelEnter   0x52B310 (opcode 23 / 0x17 — NOT 21, cf. RECONFIRMATION below)
//   Net_ReqVerifyCharName 0x52B4C0 (opcode 24 / 0x18) — ported but NEVER EMITTED (dead
//     chain proven, see the anchor on VerifyCharName)
//   Net_AccountReq_op27  0x52BD80 (opcode 27 / 0x1b) — emitted from
//     Scene_CharSelectOnMouseUp @0x523E07 (single xref). Do NOT confuse with
//     Net_SendPacket_Op27 0x4B5B90 (Net/SendPackets.h), unrelated.
//
// RE RECONFIRMATION (2026-07-14, direct idaTs2 access — HTTP JSON-RPC 127.0.0.1:13337,
// `decompile` tool, on the 5 EAs above + Scene_CharSelectOnMouseUp 0x522E50 for the
// creation form field layout). The previous session (without IDA access) had assumed
// these 5 functions were simple wrappers around the generic low-level builders
// `Net_SendPacket_Op12/17/18/21/22` (`RE/net_builders_decomp.json`).
// THAT WAS WRONG: each of the 5 functions builds its OWN inline frame (same
// duplicated pattern — nonces/header/XOR/send — but with an opcode and/or payload
// DIFFERENT from the generic same-named builders, which actually serve OTHER call
// sites in the game). Discrepancies fixed in this module:
//   - Net_AccountKeepAlive: NO payload (9 B, not 213), and above all NO RESPONSE
//     WAIT (the binary does NOT recv() — fire-and-forget heartbeat, *a1=0
//     immediately after a successful send). The old code wrongly waited for 5 B.
//   - Net_CreateCharacter: REAL payload of 10092 B (4 B slot + 10088 B character
//     record), NOT 61 B. REAL response of 10093 B ([1][code:4][record-echo:10088],
//     the server sends back the created record), NOT 5 B. Known offsets WITHIN
//     the record (relative to the start, confirmed by decompiling the caller
//     Scene_CharSelectOnMouseUp EA 0x526634-0x5267E4, consistent with the comments
//     already present in Game/CharSelectFlow.h::CharCreateForm):
//       [20..32] name (13 B) - [36] job - [44] gender (called "faction") - [48] face -
//       [52] hairColor - [216] startingWeaponItemId (ex-"lookPresetId", RENAMED —
//       it's an ITEM id resolved against the items DB, cf. the anchor on the constant).
//       Everything else is ZERO in every observed path (never written before send
//       in the binary) — in particular [40] (race), which ONLY the server fills,
//       and which comes back via opcode 17's echo.
//   - Net_CharSlotAction: REAL payload of 12 B (3 fields of 4B at 0/4/8), NOT 76 B.
//     Response [1][code:4] (5 B) unchanged/confirmed.
//   - Net_ReqEnterCharInfo: REAL payload of 4 B (ONLY the slot), NOT 2 fields/8B
//     (the old code sent a redundant byte that would misalign the frame by 4 B
//     against a real server). Response [1][code:4][domainId:4][gamePort:4][zoneId:4]
//     (17 B) CONFIRMED unchanged — this was already correct.
//   - Net_ReqCancelEnter: REAL opcode 23 (0x17), NOT 21 (0x15) — the generic opcode
//     21/Net_SendPacket_Op21 does exist in the binary (used elsewhere, e.g.
//     World_LoadMap after a Net_ConnectGameServer failure) but is NOT what this
//     function sends. Still NO payload (9 B). And above all NO RESPONSE WAIT (like
//     AccountKeepAlive) — the old code wrongly waited for 5 B.
// The generic FRAMING (9B header: nonces/sequence/opcode, then full XOR, sequence
// increment after a successful send) remains CONFIRMED unchanged — only the internal
// construction (exact opcode + payload content/size + presence or not of a recv())
// was fixed.
#pragma once
#include "Net/NetClient.h"
#include "Game/CharSelectFlow.h"
#include <array>
#include <cstdint>
#include <string>

namespace ts2::net {

// --- Internal layout of a 10088 B (0x2768) character record ---
// One record per slot (net::g_CharRecords[i], persisted by
// Net/Login.cpp::LoginRequest), the SAME structure as the payload sent by
// Net_CreateCharacter (opcode 17, cf. mapping above). Offsets RE-CONFIRMED by
// direct decompilation of Scene_CharSelectUpdate 0x51BD90 (session 2026-07-14,
// EA 0x51c2f7-0x51c7d4): the binary compares/reads `unk_1669394`/`dword_16693B8`/
// `dword_166A8DC`/`dword_166A8E0`/`E4`/`E8`, all FIXED OFFSETS relative to the base
// `unk_1669380` of the 1st record (stride 2522 dwords = 10088 B = kCharRecordSize) —
// address subtraction: name=+20, power=+56, zoneId=+5468, position=+5472/5476/5480.
inline constexpr int kCharRecFieldName    = 20;   // 13 B, C-string (character name)
// +36: "job/class" field WRITTEN BY THE CREATION FORM (dword_16709DC,
// 3 writes: 0x52537C random / 0x5260B2 minus arrow / 0x526158 plus arrow) and read
// by the CREATION preview as a race index. DISTINCT from kCharRecFieldRace (+40) —
// see the detailed anchor on it. In the LIST branch, +36 is used ONLY as a SENTINEL
// tested `== 3` (Char_RenderModel 0x527020, `cmp dword ptr [ecx+24h], 3` @0x52754A).
inline constexpr int kCharRecFieldJob     = 36;   // int32
// +40: LIST's EFFECTIVE RACE — index passed to PcModel_ResolveEquipSlot 0x4E46A0
// (a2) to resolve the 3D model, and to PcSnd_ResolveEquipSlot (@0x5251E4) for sound.
// DECISIVE ANCHOR — Char_RenderModel 0x527020 has TWO branches (`cmp [ebp+arg_4], 0 ;
// jz loc_527452` @0x52702F) that read DIFFERENT fields:
//   CREATION branch (arg_4 != 0): `mov edx, [ecx+24h]` @0x527051 -> a2 = record+36
//   LIST branch     (arg_4 == 0): `mov eax, [edx+28h]` @0x527536 -> a2 = record+40
// (BOTH pass record+44 as a3: `mov eax,[edx+2Ch]` @0x52704A / `mov ecx,[eax+2Ch]`
// @0x52752F). This is NOT a copy-paste mistake: the LIST branch ALSO reads +36, but
// only as a `== 3` sentinel @0x52754A -> the two fields COEXIST with distinct roles.
// RE-VERIFIED (direct disassembly, this session) + corroborated by data_refs:
//   data_refs(0x16709E0) = creation record +40 -> ZERO references: the client NEVER
//     writes +40; it's the SERVER that fills it (opcode 17 echo @0x52A71E).
//   data_refs(0x16693A8) = list record +40 -> 5 refs, ALL reads (0x51C52E,
//     0x51C598, 0x51C622, 0x51C691 in Scene_CharSelectUpdate; 0x5251D8 in
//     Scene_CharSelectOnMouseUp).
// => Rendering the LIST with +36 (= job) shows the WRONG 3D model. List rendering
// goes through gfx::CharPreview3D::BuildFromRecord (RAW record, LIST branch @0x527452);
// on the data side, +40 is carried by game::CharSlotInfo::race (populated by ParseCharRecord).
// WARNING NOT via ReadCharRecordListFields, which has no caller (cf. its banner below).
inline constexpr int kCharRecFieldRace    = 40;   // int32 — g_LocalElementSecondary 0x1673198
// +44: GENDER (0..1) — historical name "faction" kept so as not to break
// game::CharSlotInfo::faction (Game/CharSelectFlow.h, outside this front). PcModel_ResolveEquipSlot
// 0x4E46A0 clamps a2>2 and a3>1 (@0x4E46CC) => a2(race) in [0..2], a3(gender) in [0..1].
// The emission OFFSET is correct: this is a NAMING divergence only, do not touch it.
inline constexpr int kCharRecFieldFaction = 44;   // int32 — actually gender
inline constexpr int kCharRecFieldFace    = 48;   // int32
inline constexpr int kCharRecFieldHair    = 52;   // int32
// +56: the binary uses it as LEVEL (g_SelfLevel 0x16731A8: tier, level display,
// default selection). Name "power" kept — game::CharSlotInfo::power is outside this
// front. Naming divergence only, value and usage are correct.
inline constexpr int kCharRecFieldPower   = 56;   // int32 — dword_16693B8[2522*i] (unk_1669380+0x38)
// +216: STARTING WEAPON ITEM ID, resolved against the items DB — NOT a "lookPresetId".
// ANCHOR: Char_RenderModel LIST branch, `mov edx, [ecx+0D8h]` @0x527497 then
// `mov ecx, offset mITEM ; call MobDb_GetEntry` @0x52749E/0x5274A3 — EXACTLY the same
// pattern as the 8 other equipment slots (+0x78/+0x88/+0xB8/+0xE8/+0xF8/+0x108/+0x118/
// +0x128; e.g. +0xE8 -> MobDb_GetEntry @0x5274BA, 2 instructions further).
// The client-side resolution FORMULA remains `6*race + variant + 5` (bounds 5..19,
// cf. game::ResolveLookPresetId) — only the field NAME was wrong.
// WARNING +216 is written ONLY ON CONFIRMATION of the creation (0x52669A..0x52675B): the
// creation FORM's 3D preview reads its weapon from the SCENE (this[15716] = +0xF590,
// `mov edx,[ecx+0F590h]` @0x5271B8), NOT here. Do not wire +216 to the creation preview.
inline constexpr int kCharRecFieldStartingWeaponItemId = 216;  // int32
inline constexpr int kCharRecFieldZoneId  = 5468; // int32 — dword_166A8DC[2522*i] (+0x155C)
inline constexpr int kCharRecFieldPosX    = 5472; // int32 — dword_166A8E0[2522*i], cast to float at use time
inline constexpr int kCharRecFieldPosY    = 5476; // int32 — dword_166A8E4[2522*i]
inline constexpr int kCharRecFieldPosZ    = 5480; // int32 — dword_166A8E8[2522*i]

// SENTINEL value tested on +36 by Char_RenderModel's LIST branch:
// `cmp dword ptr [ecx+24h], 3 ; jnz loc_527669` @0x52754A/0x52754E. If +36 != 3, the
// 0x527554..0x527669 block is SKIPPED. Semantics of the "3" NOT PROVEN (we only know
// THAT the binary tests this value, not WHY).
// TODO [0x52754A]: figure out what the guarded block actually draws (descend into
// loc_527554..loc_527669) — needed to know what disappears when +36 != 3.
inline constexpr int32_t kCharRecJobSentinelValue = 3;

// --- Record fields consumed by LIST RENDERING (screen this[15714]==1) ---
// READ-ONLY view of the fields Char_RenderModel 0x527020 reads in its LIST
// branch (arg_4 == 0).
//
// WARNING API WITH NO CALLER AT ALL — DO NOT WIRE IT INTO RENDERING.
// The rationale that motivated this struct ("game::CharSlotInfo doesn't expose
// race +40") is STALE: CharSlotInfo::race EXISTS (Game/CharSelectFlow.h:168) and
// ParseCharRecord now fills it from +40 (cf. the anchor block on ParseCharRecord
// below, which closes the "TODO K1" cited by that same field). And list 3D
// rendering doesn't go through here: it reads the RAW record via
// gfx::CharPreview3D::BuildFromRecord (LIST branch @0x527452), with its own
// offset constants.
// => The 2 overloads below have ZERO callers anywhere in src/ (verified by grep).
// TODO [anchor 0x527536]: orchestrator decision — either REMOVE these 2 overloads +
// this struct (no consumer), or make Gfx/CharPreview3D.cpp consume the
// kCharRecField* constants from THIS header instead of its local kRecOff*
// constants, to have only ONE source of truth for offsets. Two sets of constants
// describing the SAME record will eventually drift. Wiring this API into
// rendering would create a 3rd path: do not do that.
struct CharRecordListFields {
    int32_t race                 = 0;     // +40 — a2 of PcModel_ResolveEquipSlot @0x527536
    int32_t gender               = 0;     // +44 — a3 of PcModel_ResolveEquipSlot @0x52752F
    int32_t startingWeaponItemId = 0;     // +216 — MobDb_GetEntry(mITEM) @0x5274A3
    int32_t job                  = 0;     // +36 — sentinel only in the list
    bool    jobSentinelIs3        = false; // (+36 == 3) @0x52754A
};

// Reads the above fields from a raw kCharRecordSize-byte record.
CharRecordListFields ReadCharRecordListFields(const uint8_t* rec);

// Same, from net::g_CharRecords[slot]. Returns false (and leaves `out` at its
// default) if `slot` is outside [0, kCharRecordCount) — PORT guard (the binary
// indexes without bounds).
bool ReadCharRecordListFields(int32_t slot, CharRecordListFields& out);

// Parses a raw kCharRecordSize (10088) byte record into a CharSlotInfo.
// `occupied` reproduces EXACTLY the binary's criterion (Crt_Strcmp(name,"") != 0,
// EA 0x51c2f7): an empty record (empty name) leaves every other field at its
// default value (the binary never uses them in that case — no zero byte is ever
// interpreted as job/faction/etc for a free slot).
//
// WARNING `out.job` carries +36 (correct: +36 IS the job field) and is NOT the
// race: that one is at +40 and is carried by `out.race` (populated from
// kCharRecFieldRace — cf. the kCharRecFieldRace anchor, 0x527536 vs 0x527051,
// for proof that the two fields coexist with distinct roles).
//
// `out.race` (+40) is FILLED BY THE SERVER only (opcode 17 echo @0x52A71E;
// data_refs(0x16709E0) = 0 refs on the creation-form side). Its TWO C++ consumers:
//   - Game/CharSelectFlow.cpp::ListMotionFrameCount -> MotionFrameCount(rec.race, …),
//     mirroring `mov ecx, ds:dword_16693A8[eax]` @0x51C52E ->
//     PcModel_ResolveSlotAndApply 0x4E5A00 @0x51C53A;
//   - UI/LoginScene.cpp::PublishSelfFromSlot -> g_World.self.elementSecondary, mirroring
//     the block+0x28 set by Crt_Memcpy(g_SelfCharInvBlock 0x1673170, record, 0x2768) @0x51C707
//     (= g_LocalElementSecondary 0x1673198).
void ParseCharRecord(const uint8_t* rec, game::CharSlotInfo& out);

// Populates `slots` from the 3 records persisted by Net_LoginRequest
// (net::g_CharRecords, cf. NetClient.h). Real integration point for
// CharSelectHost::LoadCharacterSlots (Game/CharSelectFlow.h) — wired from
// UI/LoginScene.cpp::BuildCharSelectHost.
void LoadCharacterSlotsFromRecords(std::array<game::CharSlotInfo, game::kMaxCharSlots>& slots);

// Generic transport codes (mirroring kLoginErrSend/kLoginErrRecv, Net/Login.h) —
// returned when the blocking send()/recv() fails before even getting a server
// response. Consumed by CharSelectFlow.cpp like any "unknown code" (`default:`
// branch — faithful no-op, cf. Game/CharSelectFlow.cpp). Values 101/102
// CONFIRMED by decompilation (Net_CloseSocket then *a=101 on send failure, *a=102
// on recv failure, in all 5 original functions).
inline constexpr int32_t kCharSelectErrSend = 101;
inline constexpr int32_t kCharSelectErrRecv = 102;

// Net_AccountKeepAlive 0x5298F0 (opcode 12). Session heartbeat (/30 frames in the
// CharSelect Active sub-state, cf. CharSelectFlow.h). NO payload, NO response wait
// (confirmed fire-and-forget): returns 0 as soon as the send succeeds, 101 otherwise.
int32_t AccountKeepAlive(NetClient& nc);

// Net_CreateCharacter 0x52A4A0 (opcode 17). `startingWeaponItemId` (ex-`lookPresetId`,
// RENAMED: it's an ITEM ID resolved against the items DB, cf. the anchor on
// kCharRecFieldStartingWeaponItemId / MobDb_GetEntry @0x5274A3) = id resolved
// client-side by CharSelectFlow (formula `6*race + variant + 5`, bounds 5..19, cf.
// ResolveLookPresetId — the FORMULA is unchanged, only the field name was wrong).
// Sends the 10088 B record (see offset mapping above); fully consumes the
// 10093 B response = [1][code:4][record-echo:10088].
// FRAME RE-VERIFIED BYTE-BY-BYTE (direct decompilation of 0x52A4A0, this session):
// len=10101 @0x52A582 (= 9 header + 4 slot @9 + 10088 record @13); recv of 10093
// @0x52A661; `Crt_Memcpy(v16 /*offset 9*/, &a1, 4u)` @0x52A562; `Crt_Memcpy(v17
// /*offset 13*/, a2, 0x2768u)` @0x52A57A. No residual byte (unlike op27).
// MIRROR (EA 0x52a71e, guard `if (!v18)` EA 0x52a700): on code 0, the echoed
// record (recvBuf+5, 10088 B) is copied into g_CharRecords[slot] — a faithful port
// of the binary's `unk_1669380 + 10088*slot` mirror, the SAME array Net_LoginRequest
// 0x51B8E0 fills at login. Without this copy, LoadCharacterSlotsFromRecords re-reads
// a zeroed record on the next Init sub-state and the created character disappears.
int32_t CreateCharacter(NetClient& nc, int32_t slot, const game::CharCreateForm& form,
                        int32_t startingWeaponItemId);

// Net_CharSlotAction 0x52A740 (opcode 18). Two PROVEN actions, two distinct callers
// (both reached from UI_MsgBox_OnLButtonUp 0x5C0A90):
//   action=1, arg=0        -> CharSelect_ReqDeleteChar   0x528FD0 (EA 0x528fee): deletion
//   action=2, arg=listIndex -> CharSelect_ReqRestoreChar 0x5295D0 (EA 0x5295f6): restore
// SEMANTICS OF `arg` PROVEN (it used to be noted "free / out of scope" here):
// it's field +0xF560 (= this[15704]) of the CharSelect scene = a SELECTION INDEX
// in the restore list, initialized to -1 (EA 0x51c1e2), driven by two clamped arrow
// buttons — previous `if (idx > 0) --idx` (EA 0x524232-0x524250) and next
// `if (idx < count-1) ++idx` with count = field +0xF3C8 (EA 0x5242ac-
// 0x5242d8) — reset to 0 at EA 0x525c2d, read back by Scene_CharSelectRender (EA
// 0x52030f/0x52044f). This is NEITHER a constant NOR a flag.
int32_t CharSlotAction(NetClient& nc, int32_t slot, int32_t action, int32_t arg);

// Net_ReqVerifyCharName 0x52B4C0 (opcode 24) — character deletion confirmed by TYPING
// THE NAME. Called by CharSelect_ReqDeleteCharByName 0x529230 (EA 0x5292cd), itself
// reached ONLY from UI_MsgBox_OnLButtonUp 0x5C0A90 case 41 (EA 0x5c1743).
//
// WARNING THIS OPCODE IS NEVER EMITTED BY THIS CLIENT — it is NOT a "second,
// double-confirmation deletion mechanism" (an INCORRECT claim in earlier versions of
// this comment, fixed here). The delete-by-name panel NEVER OPENS:
//   - `var_434` of Scene_CharSelectOnMouseUp, over the WHOLE FUNCTION [0x522E50, 0x526B90):
//     only 3 references — 0x522E50 (frame declaration), `mov [ebp+var_434], 0`
//     @0x525DFA, `cmp [ebp+var_434], 0` @0x525F91. NO non-zero write, NO `lea`
//     (hence no pointer aliasing) => `var_434 == 0` is an INVARIANT => the
//     `jnz 0x525FC0` is NEVER taken.
//   - consequence: `this[0xF57C] = 1` (opening the panel) is written ONLY at EA
//     0x52601D, in the dead block; the other 3 writes to 0xF57C are `= 0`.
// The chain 0x525FC0 -> 0x529230 -> 0x52B4C0 is therefore fully UNREACHABLE.
// => DO NOT WIRE ANY CLICK to VerifyCharName. The function stays ported (fidelity
// to the code present in the binary) but must remain DEAD, per the rule "a
// function dead in the binary stays dead in C++".
// 62 B frame = 9 B header + [slotEnc:i32@0][name:49@4] (53 B); 5 B response
// = [1][code:4]. Codes routed by the caller: 0/1/2/3/4/5/101/102/default.
//
// `slotEnc` is an ENCODED slot, NOT the raw slot — EA 0x5292cd:
//   Net_ReqVerifyCharName(*(_BYTE *)(this + 62860) + 100 * *(_BYTE *)(this + 62848), ...)
// i.e. `slot(+0xF58C) + 100 * flag(+0xF580)`, BOTH read as _BYTE. See
// game::ConfirmDeleteCharByName (Game/CharSelectFlow.h) for the reachability proof
// that fixes flag==1 on any real send.
int32_t VerifyCharName(NetClient& nc, int32_t slotEnc, const std::string& name);

// Net_ReqEnterCharInfo 0x52B070 (opcode 22). FULL result (resultCode/domainId/
// gamePort/zoneId), directly in the format expected by CharSelectHost::RequestEnterCharInfo.
game::EnterCharInfoResult ReqEnterCharInfo(NetClient& nc, int32_t slot);

// Net_ReqCancelEnter 0x52B310 (opcode 23 — cf. RECONFIRMATION above — NO
// payload). Cancels an entry after a recoverable connection failure (codes 3/4/5 of
// ConnectToGameServer, cf. CharSelectFlow.cpp). NO response wait (confirmed fire-
// and-forget, like AccountKeepAlive): returns 0 as soon as the send succeeds.
int32_t ReqCancelEnter(NetClient& nc);

// Net_AccountReq_op27 0x52BD80 (opcode 27). Emitted by Scene_CharSelectOnMouseUp
// (`call Net_AccountReq_op27` @0x523E07 — UNIQUE xref, cf. xrefs_to(0x52BD80) = 1 ref),
// so it is indeed within scene 4's emission surface.
//
// WARNING DO NOT CONFUSE with net::Net_SendPacket_Op27 (Net/SendPackets.h): that one
// is Net_SendPacket_Op27 0x4B5B90, opcode 0x1b, on the GAME socket (item upgrade).
// Unrelated — homonymous builder index only.
//
// `arg`: the binary passes this[+0xF0A4] (`mov eax, [edx+0F0A4h]` @0x523DFA), a
// SELECTION INDEX initialized to -1 (@0x51C2C0, Init block of Scene_CharSelectUpdate),
// written by Scene_CharSelectOnMouseDown (@0x521EB7), guarded `!= -1` right before
// emission (`cmp dword ptr [eax+0F0A4h], 0FFFFFFFFh` @0x523DC1), and reset to -1
// afterward (@0x523E3C, @0x524073). Emitted as 4 BYTES (`Crt_Memcpy(v15, &a1, 4u)`
// @0x52BE3E) — this is NOT a 1-byte arg, despite the IDB comment ("opcode-27
// (1-byte arg)") which is WRONG.
// TODO [0x521EB7]: exact semantics of this[+0xF0A4] (which list this panel
// selects) NOT PROVEN — only its protocol (index, -1 = none) is.
//
// WARNING TWO FAITHFUL ANOMALIES, do NOT "fix" them:
//  1. 14-BYTE FRAME WHOSE 14th BYTE IS UNINITIALIZED. `len = 0Eh` @0x52BE46 (14), but
//     only bytes 0..12 are written (9 B header + `Crt_Memcpy(payload@9, &a1, 4u)`
//     @0x52BE3E stops at byte 12). Byte 13 is RESIDUAL STACK: it is still XORed
//     (loop `i < len` covering 0..13) and SENT. The IDA frame layout confirms it:
//     `var_3EF` (_BYTE[995]) starts at frame offset 0x2D = byte 9, and nothing
//     writes its index [4]. 14 bytes MUST be emitted, otherwise the frame is 1
//     short and misaligns the server.
//     TODO [0x52BE46]: the VALUE of byte 13 is uninitialized stack — not
//     deterministic statically. We emit 0 (the only reproducible choice); a runtime
//     x32dbg dump would be needed to know the real observed value.
//  2. `dword_1675898 <- rx+5` is written UNCONDITIONALLY (`Crt_Memcpy(&dword_1675898,
//     &MEMORY[0x8156C5], 4u)` @0x52BFC4), BEFORE the code test (`*a2 = v16` @0x52BFD2) —
//     unlike EVERY other builder, which guards its effect with `if (!code)`
//     (cf. op17: `if (!v18)` @0x52A700). No guard here: the field is overwritten even
//     when the server returns an error.
// Response: 9 B recv (`j != 9` @0x52BF27) = [1][code:4][value:4]. Returns the code
// (rx+1, @0x52BFB0).
int32_t AccountReq_op27(NetClient& nc, int32_t arg);

// --- PIN / secondary password helper (opcodes 13/14/15) ---
// Blocking synchronous requests (send + immediate recv, login socket) emitted by
// CharSelect's PIN assistant (dword_16692A4 != 0 branch). PIN = 4 ASCII digits +
// NUL (5 B). On success (code 0) the server echoes back the new PIN and the client
// resets the "PIN required" flag (net::g_SecondaryPwRequired = dword_16692A4) and
// stores the PIN (net::g_StoredSecondaryPw = unk_16692A8). Layout and resets
// RE-VERIFIED against the disassembly.
//
// op13 SET (Net_AccountReq_op13 0x529AA0): payload PIN[5]@9 (len 14); recv 10 =
//   [1..4]=code, [5..9]=PIN echo. On code 0: g_SecondaryPwRequired=0 + g_StoredSecondaryPw
//   <- recv+5 (@0x529CEC).
int32_t SecondaryPasswordSet(NetClient& nc, const uint8_t pin5[5]);        // op13 0x529AA0
// op14 CHANGE (Net_AccountReq_op14 0x529D20): payload old[5]@9 + new[5]@14 (len 19);
//   recv 10 = [1..4]=code, [5..9]=PIN echo. On code 0: g_SecondaryPwRequired=0 +
//   g_StoredSecondaryPw <- recv+5 (new PIN, @0x529F7E).
int32_t SecondaryPasswordChange(NetClient& nc, const uint8_t oldPin5[5], const uint8_t newPin5[5]); // op14 0x529D20
// op15 VERIFY (Net_AccountReq_op15 0x529FB0): payload PIN[5]@9 (len 14); recv 5 =
//   [1..4]=code. On code 0: g_SecondaryPwRequired=0 ONLY (no PIN update, @0x52A1FC).
int32_t SecondaryPasswordVerify(NetClient& nc, const uint8_t pin5[5]);     // op15 0x529FB0

// --- CONFIRMED layout of the shared "struct72" (Op12.a4 / Op15.a2 / Op16.a2) ---
// RE-CONFIRMED by fresh decompilation (2026-07-14, direct idaTs2 access) of
// Scene_CharSelectUpdate 0x51BD90 (EA 0x51c765-0x51c7f9, writer of the record just
// before Net_ConnectGameServer) AND, independently, of Map_BeginWarpToFactionTown
// 0x55C510 (EA 0x55c6a9-0x55c65a, the SAME byte-for-byte pattern on the SAME global
// `dword_1675AA0`) — both sites confirm the same layout, which removes any doubt
// about its nature as a generic "teleport/spawn record":
//   Crt_Memset(&dword_1675AA0, 0, 0x48);   // 72 B zeroed
//   dword_1675AA0 = 0;                     // +0x00 i32  mode/type (0 in both EnterWorld/city-warp sites)
//   dword_1675AA4 = 0;                     // +0x04 i32  variant (0 or 1 depending on the caller site, 0 here)
//   flt_1675AA8   = 0.0f;                  // +0x08 f32  always 0.0 in every observed site
//   flt_1675AAC   = posX;                  // +0x0C f32  spawn position X (NOT +0x00!)
//   flt_1675AB0   = posY;                  // +0x10 f32  spawn position Y
//   flt_1675AB4   = posZ;                  // +0x14 f32  spawn position Z
//   // +0x18..+0x23 (12 B): never written by these 2 sites -> stays 0 (memset)
//   flt_1675AC4   = (Rng_Next() % 360);    // +0x24 f32  spawn rotation (0..359, freshly drawn)
//   flt_1675AC8   = flt_1675AC4;           // +0x28 f32  SAME value duplicated (not a 2nd angle)
//   // +0x2C..+0x47 (28 B): never written by these 2 sites -> stays 0 (memset)
// Discrepancy fixed vs. the old wiring (Scene/SceneManager.cpp, host.SendEnterWorldRequest,
// before this session): spawnX/Y/Z were wrongly serialized at offsets +0x00/+0x04/+0x08
// (with the mode/type field and float padding overwritten by the position) instead of
// +0x0C/+0x10/+0x14, and the rotation (+0x24/+0x28) wasn't sent at all (left at
// zero). Cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md for the full verification detail.
inline constexpr int kTail72OffMode  = 0x00; // i32, 0 for EnterWorld
inline constexpr int kTail72OffFlag  = 0x04; // i32, 0 for EnterWorld
inline constexpr int kTail72OffPad8  = 0x08; // f32, always 0.0
inline constexpr int kTail72OffPosX  = 0x0C; // f32
inline constexpr int kTail72OffPosY  = 0x10; // f32
inline constexpr int kTail72OffPosZ  = 0x14; // f32
inline constexpr int kTail72OffRotA  = 0x24; // f32, rotation (Rng_Next() % 360)
inline constexpr int kTail72OffRotB  = 0x28; // f32, duplicate of kTail72OffRotA

// Builds the EXACT struct72 block (72 B, zero-filled then the 5 fields above set
// at the confirmed offsets) for Net_SendPacket_Op12's tail72 payload
// (opcode 12, EnterWorld). `out` must point to 72 valid bytes.
void BuildEnterWorldTail72(float posX, float posY, float posZ, float rotationDeg,
                            uint8_t out[72]);

} // namespace ts2::net

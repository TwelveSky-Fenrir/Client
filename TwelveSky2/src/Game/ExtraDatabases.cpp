// Game/ExtraDatabases.cpp — loads the 2 extra .IMG tables, faithful to loaders
// 0x4C6BD0 ("SkillDefTbl_LoadImg", actually mNPC) and 0x4C8090 ("NpcTbl_LoadImg", actually mQUEST).
//
// Algorithm (identical to the 5 tables in GameDatabase.cpp, with one difference — see below):
//   1. read the file (raw CreateFileA/ReadFile in the original, here via ImgFile); decode
//      the [rawSize][packedSize][zlib] envelope -> payload (Asset_DecompressImg 0x53F5E0)
//   2. count = *(u32*)payload (^ magic if the table uses one)
//   3. integrity guard: count MUST equal the hardcoded expected constant
//   4. copy count*stride bytes starting at payload+4 (NO "header" offset with an embedded
//      name here, unlike the 5 tables — the 2 loaders memcpy from `hMem+4` directly)
//
// Difference between 005_00005 (mNPC) vs 005_00006 (mQUEST): the first XORs the counter with
// magic 0x1022 (`v9 ^ 0x1022 == 500`), the second compares the RAW counter with no magic
// (`v10 == 1000`) — represented here by magic=0 (XOR by 0 = identity).
#include "Game/ExtraDatabases.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>
#include <string>

namespace ts2::game {

namespace {

// Table descriptor (one line = one original loader).
struct ExtraTableSpec {
    const char* file;    // .IMG file name
    uint32_t    magic;   // counter XOR key (0 = none, see 005_00006)
    uint32_t    count;   // expected counter (hardcoded integrity guard)
    uint32_t    stride;  // size of one record
    const char* label;   // logical name (logging)
    DataTable ExtraDatabases::* member; // target member in g_ExtraDb
    // hasTR: does the loader test `cmp ds:g_UseTRVariant, 1` (global 0x1669190)?
    // BOTH loaders in this file do: SkillDefTbl_LoadImg @0x4C6BD9 (005_00005)
    // and NpcTbl_LoadImg @0x4C8099 (005_00006). TR strings 0x7A715C / 0x7A71E0.
    bool        hasTR;
    // Binary record validator (nullptr = not transcribed) — same signature as
    // the 5 tables in GameDatabase.cpp and QuestTbl_ValidateRecord (QuestSystem.cpp:74).
    bool (*validate)(const DataTable&, int);
};

// Raw record readers (local duplicate of those in GameDatabase.cpp — each file
// has its own anonymous namespace, like Join/kTablesDir already duplicated here).
// `memcpy` mandatory: strides 11736/8444 guarantee no alignment.
inline int32_t RecI32(const uint8_t* r, size_t off) {
    int32_t v; std::memcpy(&v, r + off, 4); return v;
}
inline uint32_t RecU32(const uint8_t* r, size_t off) {
    uint32_t v; std::memcpy(&v, r + off, 4); return v;
}
// Pattern `for (i = 0; i < N && rec[off+i]; ++i); if (i == N) return 0;`: fails if NO
// zero byte exists in [off, off+maxLen).
inline bool RecHasNul(const uint8_t* r, size_t off, size_t maxLen) {
    for (size_t i = 0; i < maxLen; ++i)
        if (r[off + i] == 0) return true;
    return false;
}

// Record validators — literal transcription of the binary's 2 validators.

// SkillDefTbl_ValidateRecord 0x4C65F0 (IDB misnomer: validates an NPC) — NpcDefRecord, 11736 bytes.
bool ValidateNpcDef(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // Early "empty slot" accept @0x4c6608: id == 0 => VALID (sparse table: 500 slots).
    if (RecU32(r, 0) == 0) return true;
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 500) return false;             /*0x4c6643*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c6664*/
    if (!RecHasNul(r, 4, 25)) return false;                               /*0x4c666d name[25]*/
    if (RecI32(r, 32) < 1 || RecI32(r, 32) > 5) return false;             /*0x4c66dd fieldA*/
    // 5x5 grid of 51-byte strings @+36: `51*k + m + 255*j + 36` @0x4c6716 -> textGrid[5][5][51].
    for (size_t j = 0; j < 5; ++j)
        for (size_t k = 0; k < 5; ++k)
            if (!RecHasNul(r, 36 + 255 * j + 51 * k, 51)) return false;
    if (RecI32(r, 1312) < 1 || RecI32(r, 1312) > 5) return false;         /*0x4c67a5 fieldB*/
    if (RecI32(r, 1316) < 1 || RecI32(r, 1316) > 17) return false;        /*0x4c67de fieldC*/
    if (RecI32(r, 1320) < 1 || RecI32(r, 1320) > 10000) return false;     /*0x4c681a fieldD*/
    // fieldE (+1324): the VALIDATOR bounds it to [1,10000] — the [1,66] bound cited in
    // ExtraDatabases.h comes from a DIFFERENT CONSUMER (Model_GetNpcMeshSlot). Here we
    // transcribe the validator, not the consumer.
    if (RecI32(r, 1324) < 1 || RecI32(r, 1324) > 10000) return false;     /*0x4c6856*/
    for (size_t n = 0; n < 3; ++n)                                        /*0x4c685f fieldF[3]*/
        if (RecI32(r, 1328 + 4 * n) < 1 || RecI32(r, 1328 + 4 * n) > 1000) return false;
    for (size_t i = 0; i < 100; ++i)                                      /*0x4c68bb fieldG[100]*/
        if (RecI32(r, 1340 + 4 * i) < 1 || RecI32(r, 1340 + 4 * i) > 2) return false;
    for (size_t j = 0; j < 3; ++j)                                        /*0x4c6914 shopItemIds[3][28]*/
        for (size_t k = 0; k < 28; ++k)
            if (RecU32(r, 1740 + 112 * j + 4 * k) >= 0x186A0u) return false;
    for (size_t j = 0; j < 3; ++j)                                        /*0x4c69a3 teachSkillIds[3][8]*/
        for (size_t k = 0; k < 8; ++k)
            if (RecU32(r, 2076 + 32 * j + 4 * k) > 0x12Cu) return false;
    for (size_t a = 0; a < 3; ++a)                                        /*0x4c6a32 skillMatrix[3][3][3][8]*/
        for (size_t b = 0; b < 3; ++b)
            for (size_t c = 0; c < 3; ++c)
                for (size_t d = 0; d < 8; ++d)
                    if (RecU32(r, 2172 + 288 * a + 96 * b + 32 * c + 4 * d) > 0x12Cu) return false;
    for (size_t l = 0; l < 145; ++l)                                      /*0x4c6b34 levelCostTable[145][15]*/
        for (size_t s = 0; s < 15; ++s)
            if (RecU32(r, 3036 + 60 * l + 4 * s) > 0x5F5E100u) return false;
    return true;                                                          /*0x4c6bc8*/
}

// NpcTbl_ValidateRecord 0x4C78C0 (IDB misnomer: validates a QUEST) — QuestDefRecord, 8444 bytes.
bool ValidateQuestDef(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // Early "empty slot" accept @0x4c78e2: `Crt_Strcmp(rec + 4, "") == 0 => return 1`.
    // The pushed string is String 0x7EC95F = ""; strcmp(x, "") == 0 <=> x[0] == '\0'.
    // DIFFERENT SEMANTICS from the 6 other tables: it's the EMPTY NAME that marks a free
    // slot, NOT id == 0 (see the ExtraDatabases.h banner and GetQuestDefRecord).
    if (r[4] == 0) return true;
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 1000) return false;            /*0x4c7923*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c7944*/
    if (!RecHasNul(r, 4, 51)) return false;                               /*0x4c794d name[51]*/
    if (RecI32(r, 56) < 1 || RecI32(r, 56) > 4) return false;             /*0x4c79bd fieldA*/
    if (RecI32(r, 60) < 1 || RecI32(r, 60) > 1000) return false;          /*0x4c79f3 fieldB*/
    if (RecI32(r, 64) < 1 || RecI32(r, 64) > 145) return false;           /*0x4c7a29 levelReq*/
    if (RecI32(r, 68) < 1 || RecI32(r, 68) > 2) return false;             /*0x4c7a5c fieldD*/
    if (RecI32(r, 72) < 1 || RecI32(r, 72) > 8) return false;             /*0x4c7a8f fieldE*/
    if (RecU32(r, 76) > 0xC8u) return false;                              /*0x4c7aac fieldF*/
    // NB: +80..+91 are covered by NO guard (see _unk80[12] in the header).
    if (RecI32(r, 92) < 1 || RecI32(r, 92) > 500) return false;           /*0x4c7afb fieldG*/
    for (size_t j = 0; j < 5; ++j)                                        /*0x4c7b04 fieldH[5]*/
        if (RecU32(r, 96 + 4 * j) > 0x1F4u) return false;
    if (RecI32(r, 116) < 1 || RecI32(r, 116) > 500) return false;         /*0x4c7b87 fieldI*/
    // NB: +120/+124 (objectiveTarget/objectiveRequired) and +128 are ALSO NOT validated.
    for (size_t k = 0; k < 3; ++k) {                                      /*0x4c7b90 rewards[3]*/
        if (RecI32(r, 136 + 8 * k) < 1 || RecI32(r, 136 + 8 * k) > 6) return false;
        if (RecU32(r, 140 + 8 * k) > 0x5F5E100u) return false;
    }
    if (RecU32(r, 160) > 0x3E8u) return false;                            /*0x4c7c49 fieldK*/
    // 10 dialogue blocks of 15 lines of 51 bytes. The binary unrolls 10 distinct loops at
    // bases 164, 992, 1820, 2648, 3476, 4304, 5132, 5960, 6788, 7616 (@0x4c7c6e .. @0x4c801c):
    // not a constant 828 bytes = 15*51 + 63, which confirms `_tail[63]` is not validated per block.
    for (size_t b = 0; b < 10; ++b)
        for (size_t i = 0; i < 15; ++i)
            if (!RecHasNul(r, 164 + 828 * b + 51 * i, 51)) return false;
    return true;                                                          /*0x4c8087*/
}

// header=4 for both tables (no embedded name, unlike the 5 tables in GameDatabase.cpp).
constexpr uint32_t kHeader = 4;

// Original EAs: SkillDefTbl_LoadImg 0x4C6BD0 (magic 0x1022, count 500, stride 11736) ->
// actually the "mNPC" manager (rec0 "Blacksmith Wu", [Error::mNPC.Init()]).
// NpcTbl_LoadImg 0x4C8090 (no magic, count 1000, stride 8444) -> actually the manager
// "mQUEST" (rec0 "[Intro] Banker Bai & Beggar Xiao", [Error::mQUEST.Init()]).
// Cross-check VeryOldClient (class names only, Docs/TS2_TABLES_ROSETTA.md §6/§7):
// mNPC = NPC class (CNPC.cpp), mQUEST = QUEST class (CQUEST.cpp). The NPC magic 0x1022 is the
// 6th family-D guard magic (split: 5 in GameDatabase.cpp, this 6th one here) — see GAP-1 §11.
const ExtraTableSpec kExtraTables[] = {
    // hasTR=true: cmp @0x4C6BD9 -> "G03_GDATA\D01_GIMAGE2D\005\TR\005_00005.IMG" (0x7A715C).
    { "005_00005.IMG", 0x1022,  500, 11736, "NPC_DEF (mNPC)",     &ExtraDatabases::npc,   true, &ValidateNpcDef   }, // ex-VeryOldClient: NPC/CNPC.cpp (CONFIRMED)
    // hasTR=true: cmp @0x4C8099 -> "G03_GDATA\D01_GIMAGE2D\005\TR\005_00006.IMG" (0x7A71E0).
    { "005_00006.IMG", 0x0000, 1000,  8444, "QUEST_DEF (mQUEST)", &ExtraDatabases::quest, true, &ValidateQuestDef }, // ex-VeryOldClient: QUEST/CQUEST.cpp (CONFIRMED); magic=0 (raw count, VeryOld agreement xorKey 0x0)
};

// Common subfolder (identical to GameDatabase.cpp — hardcoded path in the binary).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Loads a table into `out`. Returns false on failure.
// `useTR` = state of g_UseTRVariant 0x1669190; both tables in this file have a TR branch.
// NB: if the VALIDATION LOOP fails, `out` has already been populated — faithful to the
// binary, which publishes count/records BEFORE entering the loop (see @0x4c64ea/@0x4c64f5
// for the model loader). Only earlier failures leave `out` unchanged.
bool LoadOneExtraTable(const std::string& gameDataDir, const ExtraTableSpec& s, DataTable& out,
                       bool useTR) {
    // TR variant — modeled on SkillDefTbl_LoadImg 0x4C6BD0 (`cmp ds:g_UseTRVariant, 1` @0x4C6BD9)
    // and NpcTbl_LoadImg 0x4C8090 (@0x4C8099): path ...\005\TR\<file> if the flag is 1.
    const std::string file = (useTR && s.hasTR) ? std::string("TR\\") + s.file
                                                : std::string(s.file);
    const std::string path = Join(Join(gameDataDir, kTablesDir), file);

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("ExtraDB %s : .IMG illisible : %s", s.label, path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() < 4) {
        TS2_ERR("ExtraDB %s : payload trop court (%zu o)", s.label, payload.size());
        return false;
    }

    // count = first dword (^ magic if applicable; magic=0 for 005_00006 = no XOR).
    // GAP-1 (counter XOR-magic guard): guard in the right place (table loader, NOT
    // Asset/ImgFile). magic 0x1022 = NPC (VeryOld xorKey DIFFERENT, not transposed); magic 0 = QUEST
    // (raw count). DO NOT move/reimplement this pass. See Docs/TS2_TABLES_ROSETTA.md §11.
    uint32_t first = 0;
    std::memcpy(&first, payload.data(), 4);
    const uint32_t count = first ^ s.magic;

    // Integrity guard: the original loader fails if count != expected constant.
    if (count != s.count) {
        TS2_ERR("ExtraDB %s : compteur invalide (%u, attendu %u) — table alteree ?",
                s.label, count, s.count);
        return false;
    }

    const size_t need = static_cast<size_t>(kHeader)
                      + static_cast<size_t>(count) * s.stride;
    if (payload.size() < need) {
        TS2_ERR("ExtraDB %s : payload tronque (%zu < %zu o)", s.label, payload.size(), need);
        return false;
    }

    // Copy of the records ONLY (count*stride bytes from offset 4 — no embedded
    // name to skip here, see the header comment).
    const uint8_t* rec = payload.data() + kHeader;
    out.data.assign(rec, rec + static_cast<size_t>(count) * s.stride);
    out.count  = count;
    out.stride = s.stride;

    // Bulk validation loop — pattern @0x4C64F5..0x4C6527 from the model loader:
    //   `for (i = 0; i < *this; ++i) if (!ValidateRecord(this, i)) return 0;  return 1;`
    // Present in all 8 loaders in the binary, including the 2 here (-> 0x4C65F0 / 0x4C78C0).
    // A single invalid record rejects the table IN BULK and aborts App_Init 0x461C20
    // on "[Error::mNPC.Init()]" / "[Error::mQUEST.Init()]".
    // MANDATORY: after out.count/out.stride are assigned (the validators read record(i)).
    if (s.validate) {
        for (uint32_t i = 0; i < out.count; ++i) {
            if (!s.validate(out, static_cast<int>(i))) {
                TS2_ERR("ExtraDB %s : enregistrement %u invalide — table rejetee", s.label, i);
                return false;
            }
        }
    }

    TS2_LOG("ExtraDB %s : %u enregistrements x %u o", s.label, out.count, out.stride);
    return true;
}

} // namespace

bool LoadExtraDatabases(const std::string& gameDataDir, bool useTR) {
    bool allOk = true;
    for (const ExtraTableSpec& s : kExtraTables) {
        DataTable& t = g_ExtraDb.*(s.member);
        if (!LoadOneExtraTable(gameDataDir, s, t, useTR))
            allOk = false; // still attempt the other table
    }
    return allOk;
}

const NpcDefRecord* GetNpcDefRecord(uint32_t npcId) {
    if (npcId < 1) return nullptr;
    const uint8_t* r = g_ExtraDb.npc.record(npcId - 1);
    const NpcDefRecord* rec = reinterpret_cast<const NpcDefRecord*>(r);
    // Empty slot (id == 0), same semantics as NpcDefTbl_ValidateRecord 0x4C65F0.
    return (rec && rec->id != 0) ? rec : nullptr;
}

const QuestDefRecord* GetQuestDefRecord(uint32_t questId) {
    if (questId < 1) return nullptr;
    const uint8_t* r = g_ExtraDb.quest.record(questId - 1);
    const QuestDefRecord* rec = reinterpret_cast<const QuestDefRecord*>(r);
    // Empty slot (name == ""), DIFFERENT semantics from NpcDefRecord — see QuestDefTbl_ValidateRecord
    // 0x4C78C0, which tests `Crt_Strcmp(name, "") == 0` rather than id==0.
    return (rec && rec->name[0] != '\0') ? rec : nullptr;
}

// NpcTbl_FindByTypeAndId 0x4C8340 — literal transcription (see ExtraDatabases.h for the
// full proof). `this` = mQUEST 0x8E71E4 {count @+0, records @+4} == g_ExtraDb.quest.
//
// FIDELITY — points NOT to "improve":
//   * the order of the 3 conditions is the binary's (name, then +56, then +60);
//   * the `+1` applies to the ARGUMENT (`add edx, 1` @0x4C839E), never to the field;
//   * NO null/table-loaded guard: the binary has none. On an empty table
//     (count == 0) the loop `i < *this` @0x4C8361 doesn't run and the function returns 0
//     (`xor eax, eax` @0x4C83D2) — already the natural behavior here, no need to add
//     a test the original doesn't do.
const QuestDefRecord* FindQuestDefByElementAndId(int element0, int questId) {
    const DataTable& t = g_ExtraDb.quest;
    for (uint32_t i = 0; i < t.count; ++i) {                       // cmp edx,[ecx] @0x4C8361
        // imul eax, 20FCh @0x4C836D -> stride 8444 (== sizeof(QuestDefRecord), static_assert).
        const uint8_t* raw = t.record(i);
        if (!raw) break;
        const QuestDefRecord* rec = reinterpret_cast<const QuestDefRecord*>(raw);

        // (a) Crt_Strcmp(rec+4, "") != 0 @0x4C837E: name NOT empty. The pushed string
        //     @0x4C8365 is String 0x7EC95F = the 2nd NUL of "re.DAT\0\0" (0x7EC958), i.e. "".
        //     strcmp(x, "") != 0 <=> x[0] != '\0'.
        if (rec->name[0] == '\0') continue;                        // jnz/jmp @0x4C8388-0x4C838A

        // (b) rec[56] == element0 + 1 @0x4C83A1 (the +1 is on the argument @0x4C839E).
        if (rec->fieldA != static_cast<uint32_t>(element0 + 1)) continue;  // jnz @0x4C83A5

        // (c) rec[60] == questId @0x4C83BA.
        if (rec->fieldB != static_cast<uint32_t>(questId)) continue;       // jnz @0x4C83BD

        return rec;                                               // base + 8444*i @0x4C83CE
    }
    return nullptr;                                               // xor eax,eax @0x4C83D2
}

} // namespace ts2::game

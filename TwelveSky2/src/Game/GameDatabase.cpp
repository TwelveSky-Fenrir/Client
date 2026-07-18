// Game/GameDatabase.cpp — loads the .IMG data tables (faithful to loaders
// 0x4C2680/0x4C3930/0x4C4BC0/0x4C62A0/0x4C7390).
//
// Common algorithm (identical across the binary's 5 loaders):
//   1. pick the path: ...\005\<file> or ...\005\TR\<file> if g_UseTRVariant == 1
//      (0x1669190 — ONLY for the 3 `hasTR` tables here: ITEM/SKILL/MONSTER)
//   2. read the file; decode envelope [rawSize][packedSize][zlib] -> payload
//   3. count = *(u32*)payload ^ magic
//   4. integrity guard: count MUST equal the hardcoded expected constant (else fail)
//   5. copy count*stride bytes from payload+header into DataTable.data
//   6. validate EACH record (*_ValidateRecord); the first invalid one rejects the
//      table AS A WHOLE (@0x4C64F5..0x4C6527) -> App_Init 0x461C20 aborts with « [Error::mXXX.Init()] »
// The SOCKET table has ~9.4 KB of trailing padding in the payload: we only copy count*stride
// (the original loader allocates exactly 0xECCC = 3031*20 and ignores the rest).
#include "Game/GameDatabase.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>
#include <string>

namespace ts2::game {

namespace {

// Descriptor for one table (one row = one original loader).
struct TableSpec {
    const char* file;    // .IMG file name
    uint32_t    magic;   // XOR key for the counter
    uint32_t    count;   // expected count (hardcoded integrity guard)
    uint32_t    header;  // record offset within the payload
    uint32_t    stride;  // size of one record
    const char* label;   // logical / embedded name (logging + audit)
    DataTable GameDatabases::* member; // target member in g_World.db
    // hasTR: does the original loader test `cmp ds:g_UseTRVariant, 1` (global 0x1669190)?
    // data_refs(0x1669190) = 14 refs, of which EXACTLY 5 are table loaders: MobDb_LoadImg
    // @0x4C3939, SkillGrowthTbl_LoadImg @0x4C4BC9, ItemDefTbl_LoadImg @0x4C62A9,
    // SkillDefTbl_LoadImg @0x4C6BD9 (ExtraDatabases.cpp), NpcTbl_LoadImg @0x4C8099 (same).
    // LevelTable_LoadImg 0x4C2680 and AnchorTbl_LoadImg 0x4C7390 have NO TR branch at all:
    // the binary NEVER reads 005\TR\005_00001.IMG or 005\TR\005_00010.IMG, even if the
    // file exists on disk. Do NOT "improve" this (fidelity).
    bool        hasTR;
    // Original binary's record validator (nullptr = not ported). Signature modeled on
    // QuestTbl_ValidateRecord (Game/QuestSystem.cpp:74), the only validator already in place before W9.
    bool (*validate)(const DataTable&, int);
};

// ---------------------------------------------------------------------------
// Raw record readers.
// `memcpy` is mandatory: `data` is a byte-for-byte copy of the .IMG payload, with no
// alignment guarantee (strides 436/776/944 are not all multiples of 4).
// The I32/U32 distinction is NOT cosmetic: the original validators mix signed
// `cmp` (jl/jg) and unsigned (ja/jb) on the SAME fields — we transcribe the
// sign of each comparison exactly as it appears in the disassembly.
// ---------------------------------------------------------------------------
inline int32_t RecI32(const uint8_t* r, size_t off) {
    int32_t v; std::memcpy(&v, r + off, 4); return v;
}
inline uint32_t RecU32(const uint8_t* r, size_t off) {
    uint32_t v; std::memcpy(&v, r + off, 4); return v;
}
// Pattern `for (i = 0; i < N && rec[off+i]; ++i); if (i == N) return 0;` present in the 5
// text-field validators (ITEM/SKILL/MONSTER here, NPC/QUEST in ExtraDatabases.cpp;
// LEVEL and SOCKET have no strings): fails if there is NO nul byte in [off, off+maxLen).
inline bool RecHasNul(const uint8_t* r, size_t off, size_t maxLen) {
    for (size_t i = 0; i < maxLen; ++i)
        if (r[off + i] == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Record validators — literal transcription of the binary's validators.
// Each original loader loops over ALL its records and returns 0 on the first
// invalid one (cf. the loop @0x4C64F5..0x4C6527 referenced in LoadOneTable).
// ---------------------------------------------------------------------------

// LevelTable_ValidateEntry 0x4C2430 — LEVEL_INFO, 44 bytes = 11 dwords.
bool ValidateLevel(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // NOTE: this validator has NO early "empty slot" accept (unlike the other 6) —
    // it goes straight to the id bound check @0x4c2457. The LEVEL table is dense (145/145).
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 145) return false;             /*0x4c2457*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c2472*/
    if (RecU32(r, 4) > 0x77359400u) return false;                         /*0x4c2489*/
    if (RecI32(r, 8) < 1 || RecI32(r, 8) > 2000000000) return false;      /*0x4c24c6*/
    if (RecI32(r, 4) >= RecI32(r, 8)) return false;                       /*0x4c24e9*/
    // CROSS-RECORD guard @0x4c251b — the only one in the binary that reads the NEXT record:
    //   `if (a2 < 144 && *(this + 11*a2 + 2) != *(this + 11*a2 + 12) - 1) return 0;`
    // dword[11*a2+12] == dword[11*(a2+1)+1], i.e. offset +4 of record a2+1. In other words,
    // expNext[n] must equal expCumul[n+1] - 1.
    if (row0 < 144) {
        // The count guard (count == 145) guarantees record(row0+1) != nullptr here.
        const uint8_t* next = t.record(static_cast<uint32_t>(row0 + 1));
        if (next && RecI32(r, 8) != RecI32(next, 4) - 1) return false;
    }
    if (RecU32(r, 12) > 0x64u) return false;                              /*0x4c2532*/
    for (size_t off = 16; off <= 40; off += 4)                            /*0x4c2559..0x4c264c*/
        if (RecU32(r, off) > 0x2710u) return false;
    return true;                                                          /*0x4c264c*/
}

// MobDb_ValidateEntry 0x4C2C50 — ITEM_INFO, 436 bytes.
bool ValidateItem(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // EARLY "empty slot" accept @0x4c2c68: `if (!*(DWORD*)(436*a2 + records)) return 1;`
    // CRITICAL — the ITEM table declares 99999 records, the vast majority of which are
    // empty: without this accept, the validation loop would reject the table at the first
    // gap and LoadGameDatabases would return false on otherwise-sound data.
    if (RecU32(r, 0) == 0) return true;
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 99999) return false;           /*0x4c2ca3*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c2cc4*/
    if (!RecHasNul(r, 4, 25)) return false;                               /*0x4c2ccd name[25]*/
    for (size_t j = 0; j < 3; ++j)                                        /*0x4c2d13 3 x 51*/
        if (!RecHasNul(r, 29 + 51 * j, 51)) return false;
    if (RecI32(r, 184) < 1 || RecI32(r, 184) > 6) return false;           /*0x4c2da9*/
    if (RecI32(r, 188) < 1 || RecI32(r, 188) > 36) return false;          /*0x4c2de2*/
    if (RecI32(r, 192) < 1 || RecI32(r, 192) > 10000) return false;       /*0x4c2e1e*/
    if (RecU32(r, 196) > 0x2710u) return false;                           /*0x4c2e3e*/
    if (RecU32(r, 200) > 0x2710u) return false;                           /*0x4c2e7a*/
    if (RecI32(r, 204) < 1 || RecI32(r, 204) > 145) return false;         /*0x4c2ed2*/
    if (RecU32(r, 208) > 0xCu) return false;                              /*0x4c2ef2*/
    if (RecI32(r, 212) < 1 || RecI32(r, 212) > 4) return false;           /*0x4c2f44*/
    if (RecI32(r, 216) < 1 || RecI32(r, 216) > 14) return false;          /*0x4c2f7d*/
    if (RecI32(r, 220) < 1 || RecI32(r, 220) > 2000000000) return false;  /*0x4c2fb9*/
    if (RecU32(r, 224) > 0x77359400u) return false;                       /*0x4c2fd9*/
    if (RecU32(r, 228) > 0x77359400u) return false;                       /*0x4c3015*/
    if (RecI32(r, 232) < 1 || RecI32(r, 232) > 145) return false;         /*0x4c306d*/
    if (RecU32(r, 236) > 0xCu) return false;                              /*0x4c308d*/
    // 11 fields (+240..+280), each [1,2] — cf. ItemInfo::flags[11].
    for (size_t off = 240; off <= 280; off += 4)                          /*0x4c30df..0x4c3319*/
        if (RecI32(r, off) < 1 || RecI32(r, off) > 2) return false;
    if (RecI32(r, 284) < 1 || RecI32(r, 284) > 3) return false;           /*0x4c3352*/
    if (RecU32(r, 288) >= 0x16Eu) return false;                           /*0x4c3372 (< 366)*/
    for (size_t off = 292; off <= 308; off += 4)                          /*0x4c33ae..0x4c34bc*/
        if (RecU32(r, off) > 0x2710u) return false;
    if (RecU32(r, 312) > 0x4E20u) return false;                           /*0x4c34da*/
    if (RecU32(r, 316) > 0x2710u) return false;                           /*0x4c3516*/
    if (RecU32(r, 320) > 0x4E20u) return false;                           /*0x4c3552*/
    if (RecU32(r, 324) > 0x2710u) return false;                           /*0x4c358e*/
    if (RecU32(r, 328) > 0x2710u) return false;                           /*0x4c35ca*/
    if (RecU32(r, 332) > 0x2710u) return false;                           /*0x4c3606*/
    if (RecU32(r, 336) > 0x64u) return false;                             /*0x4c3642*/
    if (RecU32(r, 340) > 0x10u) return false;                             /*0x4c367b*/
    if (RecU32(r, 344) > 0x2710u) return false;                           /*0x4c36b4*/
    // CONDITIONAL guard @0x4c3722: field340 == 9 => field344 must be [1,3].
    if (RecU32(r, 340) == 9 && (RecI32(r, 344) < 1 || RecI32(r, 344) > 3)) return false;
    if (RecU32(r, 348) > 0x12Cu) return false;                            /*0x4c3742*/
    if (RecU32(r, 352) > 0x64u) return false;                             /*0x4c377e*/
    if (RecU32(r, 356) > 0x3E8u) return false;                            /*0x4c37b7*/
    if (RecU32(r, 360) > 0x64u) return false;                             /*0x4c37f3*/
    if (RecU32(r, 364) > 0x64u) return false;                             /*0x4c382c*/
    if (RecU32(r, 368) > 0x64u) return false;                             /*0x4c3865*/
    for (size_t m = 0; m < 8; ++m) {                                      /*0x4c3887 resist[8]*/
        if (RecU32(r, 372 + 8 * m) > 0x12Cu) return false;                /*0x4c38bd*/
        if (RecU32(r, 376 + 8 * m) > 0x64u) return false;                 /*0x4c38fc*/
    }
    return true;                                                          /*0x4c3928*/
}

// Bounds for the 2 blocks of 25 SKILL_INFO stats (+576 statMin / +676 statMax). Validator
// 0x4C4160 tests them via a `for (m = 0; m < 2; ++m)` loop over base `576 + 100*m`,
// with a DIFFERENT bound per stat index (read one by one from the decompile).
const uint32_t kSkillStatMax[25] = {
    0x2710, 0x2710, 0x2710, 0x64,   0x64,    // +576 +580 +584 +588 +592
    0x3E8,  0x3E8,  0x3E8,  0x3E8,  0x2710,  // +596 +600 +604 +608 +612
    0x3E8,  0x3E8,  0x3E8,  0x3E8,  0x3E8,   // +616 +620 +624 +628 +632
    0x3E8,  0x3E8,  0x3E8,  0x3E8,  0x3E8,   // +636 +640 +644 +648 +652
    0x3E8,  0x3E8,  0x2710, 0x3E8,  0x2710,  // +656 +660 +664 +668 +672
};

// SkillGrowthTbl_ValidateRecord 0x4C4160 — SKILL_INFO, 776 bytes.
bool ValidateSkill(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    if (RecU32(r, 0) == 0) return true;                                   // empty slot accept
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 300) return false;
    if (RecI32(r, 0) != row0 + 1) return false;
    if (!RecHasNul(r, 4, 25)) return false;                               // name[25]
    for (size_t j = 0; j < 10; ++j)                                       // description[10][51]
        if (!RecHasNul(r, 29 + 51 * j, 51)) return false;
    if (RecI32(r, 540) < 1 || RecI32(r, 540) > 4) return false;
    if (RecI32(r, 544) < 1 || RecI32(r, 544) > 5) return false;
    if (RecI32(r, 548) < 1 || RecI32(r, 548) > 10000) return false;
    if (RecI32(r, 552) < 1 || RecI32(r, 552) > 4) return false;
    if (RecI32(r, 556) < 1 || RecI32(r, 556) > 10) return false;
    if (RecI32(r, 560) < 1 || RecI32(r, 560) > 1000) return false;
    if (RecI32(r, 564) < 1 || RecI32(r, 564) > 1000) return false;
    if (RecU32(r, 568) > 0xAu) return false;
    if (RecU32(r, 572) > 0x3E8u) return false;
    for (size_t m = 0; m < 2; ++m)                                        // statMin then statMax
        for (size_t k = 0; k < 25; ++k)
            if (RecU32(r, 576 + 100 * m + 4 * k) > kSkillStatMax[k]) return false;
    return true;
}

// ItemDefTbl_ValidateRecord 0x4C5350 — MONSTER_INFO, 944 bytes (IDB misnomer: validates a MONSTER).
bool ValidateMonster(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    if (RecU32(r, 0) == 0) return true;                                   // empty slot accept
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 10000) return false;
    if (RecI32(r, 0) != row0 + 1) return false;
    if (!RecHasNul(r, 4, 25)) return false;                               // name[25]
    for (size_t j = 0; j < 2; ++j)                                        // textBlock[2][101]
        if (!RecHasNul(r, 29 + 101 * j, 101)) return false;
    if (RecI32(r, 232) < 1 || RecI32(r, 232) > 15) return false;
    if (RecI32(r, 236) < 1 || RecI32(r, 236) > 51) return false;
    if (RecI32(r, 240) < 1 || RecI32(r, 240) > 2) return false;
    if (RecI32(r, 244) < 1 || RecI32(r, 244) > 10000) return false;       // kindIndexP1
    for (size_t m = 0; m < 3; ++m)                                        // collDim[3]
        if (RecI32(r, 248 + 4 * m) < 1 || RecI32(r, 248 + 4 * m) > 1000) return false;
    if (RecU32(r, 260) > 0x3E8u) return false;
    if (RecI32(r, 264) < 1 || RecI32(r, 264) > 4) return false;
    if (RecI32(r, 268) < 1 || RecI32(r, 268) > 2) return false;
    for (size_t n = 0; n < 6; ++n)                                        // field272[6]
        if (RecI32(r, 272 + 4 * n) < 1 || RecI32(r, 272 + 4 * n) > 10000) return false;
    if (RecU32(r, 296) >= 4u) return false;
    for (size_t i = 0; i < 3; ++i)                                        // field300[3]
        if (RecU32(r, 300 + 4 * i) > 0x2710u) return false;
    if (RecU32(r, 312) >= 4u) return false;
    for (size_t i = 0; i < 3; ++i)                                        // field316[3]
        if (RecU32(r, 316 + 4 * i) > 0x2710u) return false;
    if (RecI32(r, 328) < 1 || RecI32(r, 328) > 10000) return false;
    if (RecI32(r, 332) < 1 || RecI32(r, 332) > 10000) return false;
    if (RecI32(r, 336) < 1 || RecI32(r, 336) > 1000000) return false;     // rewardMin
    if (RecI32(r, 340) < 1 || RecI32(r, 340) > 1000000) return false;     // rewardMax
    if (RecU32(r, 336) > RecU32(r, 340)) return false;                    // min <= max (unsigned)
    if (RecI32(r, 344) < 1 || RecI32(r, 344) > 145) return false;         // level
    if (RecU32(r, 348) > 0xCu) return false;
    if (RecI32(r, 352) < 1 || RecI32(r, 352) > 1000) return false;
    if (RecU32(r, 356) > 0x3E8u) return false;
    if (RecU32(r, 360) > 0x5F5E100u) return false;
    if (RecU32(r, 364) > 0x5F5E100u) return false;
    if (RecI32(r, 368) < 1 || RecI32(r, 368) > 2000000000) return false;  // hpMax
    if (RecI32(r, 372) < 1 || RecI32(r, 372) > 6) return false;
    if (RecU32(r, 376) > 0x2710u) return false;
    if (RecU32(r, 380) > 0x2710u) return false;
    if (RecU32(r, 376) > RecU32(r, 380)) return false;
    if (RecU32(r, 384) > 0x3E8u) return false;
    if (RecU32(r, 388) > 0x3E8u) return false;
    if (RecU32(r, 392) > 0x3E8u) return false;
    if (RecU32(r, 396) > 0x493E0u) return false;                          // <= 300000
    if (RecU32(r, 400) > 0x30D40u) return false;                          // <= 200000
    for (size_t i = 0; i < 4; ++i)                                        // field404[4]
        if (RecU32(r, 404 + 4 * i) > 0x186A0u) return false;
    if (RecU32(r, 420) > 0x64u) return false;
    if (RecU32(r, 424) > 0x64u) return false;
    if (RecU32(r, 428) > 0x64u) return false;
    if (RecU32(r, 424) > RecU32(r, 428)) return false;
    if (RecI32(r, 432) < 0) return false;
    // Composite binary guard: field432/100 must be 2, 3, 4, or 5 — UNLESS field432
    // is zero (the last term `&& *(...+432)` allows the value 0 outright).
    {
        const uint32_t v432 = RecU32(r, 432);
        const uint32_t q = v432 / 100u;
        if (q != 2u && q != 3u && q != 4u && q != 5u && v432 != 0u) return false;
        if (q > 15u) return false;
    }
    if (RecU32(r, 436) > 0xF4240u) return false;
    if (RecU32(r, 440) > 0x5F5E100u) return false;                        // goldMin
    if (RecU32(r, 444) > 0x5F5E100u) return false;                        // goldMax
    if (RecU32(r, 440) > RecU32(r, 444)) return false;
    for (size_t k = 0; k < 5; ++k) {                                      // dropA[5]
        if (RecU32(r, 448 + 8 * k) > 0xF4240u) return false;
        if (RecU32(r, 452 + 8 * k) >= 0x186A0u) return false;
    }
    for (size_t m = 0; m < 12; ++m)                                       // field488[12]
        if (RecU32(r, 488 + 4 * m) > 0xF4240u) return false;
    if (RecU32(r, 536) > 0xF4240u) return false;
    if (RecU32(r, 540) >= 0x186A0u) return false;
    for (size_t n = 0; n < 50; ++n) {                                     // dropB[50]
        if (RecU32(r, 544 + 8 * n) > 0xF4240u) return false;
        if (RecU32(r, 548 + 8 * n) >= 0x186A0u) return false;
    }
    return true;
}

// AnchorTbl_ValidateRecord 0x4C6F20 — SOCKET_INFO, 20 bytes {type,+4,attachId,offX,offY}.
bool ValidateSocket(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    if (RecU32(r, 8) == 0) return true;                                   /*0x4c6f33*/
    if (RecU32(r, 0) == 0) return true;                                   /*0x4c6f50*/
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 54) return false;              /*0x4c6f82*/
    // ORIGINAL BUG PRESERVED (fidelity rule): the binary tests `< 0 AND > 10000`, a
    // conjunction that can NEVER be true -> DEAD guard. Verified in the disassembly,
    // not just the decompile: `cmp [+4], 0 / jge 4C6FBB` @0x4c6f97-0x4c6f9c then
    // `cmp [+4], 2710h / jle 4C6FBB` @0x4c6faa-0x4c6fb2 (both exits lead to the same
    // success label). The author likely meant `||`: do NOT "fix" this.
    if (RecI32(r, 4) < 0 && RecI32(r, 4) > 10000) return false;           /*0x4c6fb2*/
    if (RecU32(r, 0) == 1) {                                              /*0x4c6fcb*/
        if (RecI32(r, 8) < 1 || RecI32(r, 8) > 33) return false;          /*0x4c6ff5*/
        if (RecU32(r, 12) > 0x190u) return false;                         /*0x4c700f*/
        if (RecU32(r, 16) > 0x190u) return false;                         /*0x4c703f*/
    } else if (RecI32(r, 0) <= 1 || RecI32(r, 0) >= 30) {                 /*0x4c7089*/
        if (RecI32(r, 0) >= 30 && RecI32(r, 0) <= 38) {                   /*0x4c7147*/
            if (RecI32(r, 8) < 1 || RecI32(r, 8) > 2) return false;       /*0x4c716d*/
            if (RecU32(r, 12) > 0x32u) return false;                      /*0x4c7187*/
            if (RecU32(r, 16) != 0) return false;                         /*0x4c71af*/
        } else if (RecI32(r, 0) >= 39 && RecI32(r, 0) <= 42) {            /*0x4c71e4*/
            if (RecI32(r, 8) < 1 || RecI32(r, 8) > 10) return false;      /*0x4c720a*/
            if (RecI32(r, 12) < 1) return false;                          /*0x4c7224*/
            if (RecU32(r, 16) != 0) return false;                         /*0x4c7239*/
        } else if (RecI32(r, 0) >= 43 && RecI32(r, 0) <= 46) {            /*0x4c726e*/
            if (RecI32(r, 8) < 1 || RecI32(r, 8) > 10) return false;      /*0x4c7294*/
            if (RecI32(r, 12) < 6) return false;                          /*0x4c72ae*/
            if (RecU32(r, 16) != 0) return false;                         /*0x4c72c3*/
        } else if (RecI32(r, 0) >= 47 && RecI32(r, 0) <= 54) {            /*0x4c72fc*/
            if (RecI32(r, 8) < 11 || RecI32(r, 8) > 45) return false;     /*0x4c7322*/
            if (RecI32(r, 12) < -5 || RecI32(r, 12) > 15) return false;   /*0x4c734c*/
            if (RecI32(r, 16) < -4 || RecI32(r, 16) > 65) return false;   /*0x4c7376*/
        }
        // No guard for types outside {1, 30..54}: the binary falls through to `return 1`.
    } else {                                                              // type in [2, 29]
        if (RecI32(r, 8) < 1 || RecI32(r, 8) > 100) return false;         /*0x4c70b3*/
        if (RecU32(r, 12) > 0x3E8u) return false;                         /*0x4c70cd*/
        if (RecU32(r, 16) > 0x3E8u) return false;                         /*0x4c70fd*/
    }
    return true;                                                          /*0x4c7381*/
}

// Order and constants extracted from the disassembly (Docs/TS2_IMG_FORMAT.md 4.1).
// magic/count/header/stride = IDA GROUND TRUTH (never transposed from VeryOld). Cross-checked
// against VeryOldClient classes (class names only, Docs/TS2_TABLES_ROSETTA.md §1); WARNING: IDB MISNOMERS:
// MobDb_LoadImg=ITEM, ItemDefTbl_LoadImg=MONSTER, AnchorTbl_LoadImg=SOCKET (off-by-one shift).
const TableSpec kTables[] = {
    // LevelTable_LoadImg 0x4C2680 -> mLEVEL 0x8E7208. ex-VeryOldClient: LEVEL/CLEVEL.cpp (CONFIRMED)
    // hasTR=false: NO TR branch (absent from the 14 data_refs of g_UseTRVariant 0x1669190).
    { "005_00001.IMG", 0x0E31,   145,  34,  44, "LEVEL_INFO",   &GameDatabases::level,   false, &ValidateLevel   },
    // MobDb_LoadImg 0x4C3930 (misnomer) -> mITEM 0x8E71EC. ex-VeryOldClient: ITEM/CITEM.cpp (CONFIRMED)
    // hasTR=true: `cmp ds:g_UseTRVariant, 1` @0x4C3939 -> ...\005\TR\005_00002.IMG (0x7A704C).
    { "005_00002.IMG", 0x1CB3, 99999,  67, 436, "ITEM_INFO",    &GameDatabases::item,    true,  &ValidateItem    },
    // SkillGrowthTbl_LoadImg 0x4C4BC0. ex-VeryOldClient: SKILL/CSKILL.cpp (CONFIRMED)
    // hasTR=true: cmp @0x4C4BC9 -> ...\005\TR\005_00003.IMG (0x7A70A4).
    { "005_00003.IMG", 0x0C7E,   300,  84, 776, "SKILL_INFO",   &GameDatabases::skill,   true,  &ValidateSkill   },
    // ItemDefTbl_LoadImg 0x4C62A0 (misnomer) -> mMONSTER 0x8E71FC. ex-VeryOldClient: MONSTER (CONFIRMED)
    // hasTR=true: cmp @0x4C62A9 -> ...\005\TR\005_00004.IMG (0x7A7104), EU branch @0x4c62f1.
    { "005_00004.IMG", 0x1583, 10000,  88, 944, "MONSTER_INFO", &GameDatabases::monster, true,  &ValidateMonster },
    // AnchorTbl_LoadImg 0x4C7390 (misnomer) -> mSOCKET 0x8E71D0. ex-VeryOldClient: GSOCKET/CSOCKET.cpp
    // (CONFIRMED table; WARNING: CONFLICT J-4 on the count: IDA=3031 vs VeryOld 2891 valid/3000 fixed — IDA wins).
    // hasTR=false: NO TR branch (cf. LEVEL) — 005\TR\005_00010.IMG is never read.
    { "005_00010.IMG", 0x0FDB,  3031, 103,  20, "SOCKET_INFO",  &GameDatabases::socketT, false, &ValidateSocket  },
};

// Subfolder common to all tables (hardcoded paths in the binary).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Loads a table into `out`. Returns false on failure.
// `useTR` = state of g_UseTRVariant 0x1669190 (cmdline field 1); only the `hasTR` tables
// switch to the localized subfolder.
// NOTE: if the VALIDATION LOOP fails, `out` has already been populated — this matches the
// binary, which publishes `*this = count` / `*(this+1) = records` (@0x4c64ea/@0x4c64f2) BEFORE
// entering the loop @0x4c64f5. Only earlier failures leave `out` unchanged.
bool LoadOneTable(const std::string& gameDataDir, const TableSpec& s, DataTable& out, bool useTR) {
    // TR variant: the 5 relevant loaders open an ENTIRELY separate path when
    // g_UseTRVariant == 1. Model: ItemDefTbl_LoadImg 0x4C62A0 —
    //   `if (g_UseTRVariant == 1)` @0x4c62a9/@0x4c62b0
    //     CreateFileA("G03_GDATA\D01_GIMAGE2D\005\TR\005_00004.IMG")  @0x4c62cf
    //   else
    //     CreateFileA("G03_GDATA\D01_GIMAGE2D\005\005_00004.IMG")     @0x4c62f1
    // The choice is PER TABLE (`s.hasTR`), not global: cf. TableSpec's hasTR field.
    const std::string file = (useTR && s.hasTR) ? std::string("TR\\") + s.file
                                                : std::string(s.file);
    const std::string path = Join(Join(gameDataDir, kTablesDir), file);

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("DB %s : .IMG illisible : %s", s.label, path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() < 4) {
        TS2_ERR("DB %s : payload trop court (%zu o)", s.label, payload.size());
        return false;
    }

    // count = first dword XOR magic (cf. Crt_Memcpy(&v, hMem, 4); v ^ magic).
    // GAP-1 (XOR-magic count guard, family D): this guard already lives in the right place —
    // in the table loader, NEVER in Asset/ImgFile (which stops at the envelope
    // [rawSize][packedSize][zlib]). 5 magics here (0xE31/0x1CB3/0xC7E/0x1583/0xFDB), the 6th (NPC
    // 0x1022) in ExtraDatabases.cpp. WARNING: VeryOld magics DIFFER (alt build) — do NOT
    // transpose them, do NOT move/reimplement this pass. Cf. Docs/TS2_TABLES_ROSETTA.md §11 GAP-1.
    uint32_t first = 0;
    std::memcpy(&first, payload.data(), 4);
    const uint32_t count = first ^ s.magic;

    // Integrity guard: the original loader fails if count != expected constant.
    if (count != s.count) {
        TS2_ERR("DB %s : compteur invalide (%u, attendu %u) — table alteree ?",
                s.label, count, s.count);
        return false;
    }

    const size_t need = static_cast<size_t>(s.header)
                      + static_cast<size_t>(count) * s.stride;
    if (payload.size() < need) {
        TS2_ERR("DB %s : payload tronque (%zu < %zu o)", s.label, payload.size(), need);
        return false;
    }

    // Copy of the records ONLY (count*stride bytes from offset header).
    const uint8_t* rec = payload.data() + s.header;
    out.data.assign(rec, rec + static_cast<size_t>(count) * s.stride);
    out.count  = count;
    out.stride = s.stride;

    // BULK validation loop — ItemDefTbl_LoadImg @0x4C64F5..0x4C6527:
    //   `for (i = 0; i < *this; ++i) if (!ItemDefTbl_ValidateRecord(this, i)) return 0;`
    //   `return 1;`  (fail @0x4c6523, success @0x4c6527)
    // Identical pattern in the binary's 8 loaders. A SINGLE invalid record fails the
    // entire loader -> App_Init 0x461C20 aborts with « [Error::mXXX.Init()] ».
    // The table is therefore rejected AS A WHOLE, never partially.
    // IMPORTANT: this loop comes AFTER the out.count/out.stride assignment above —
    // the validators index via out.record(i), which depends on both.
    if (s.validate) {
        for (uint32_t i = 0; i < out.count; ++i) {
            if (!s.validate(out, static_cast<int>(i))) {
                TS2_ERR("DB %s : enregistrement %u invalide — table rejetee", s.label, i);
                return false;
            }
        }
    }

    // IMG-TRUTH audit: the embedded name must START WITH the expected label.
    // PREFIX comparison, not equality: the .IMG files REPEAT the name to fill the
    // header, so header == 4 + strlen(label) * k for integer k — verified against the 5
    // hardcoded header constants of the loaders (kTables' `header` column):
    //   LEVEL_INFO 4+10*3=34 · ITEM_INFO 4+9*7=67 · SKILL_INFO 4+10*8=84
    //   MONSTER_INFO 4+12*7=88 · SOCKET_INFO 4+11*9=103   (5/5 exact divisions)
    // Asset/ImgFile.cpp:61 reads 30 raw bytes without wrapping on the period: TableName()
    // therefore equals e.g. "ITEM_INFOITEM_INFOITEM_INFOITE" or "MONSTER_INFOMONSTER_INFOMONSTE".
    // A strict equality was ALWAYS false -> systematic TS2_WARN on all 5 tables
    // (the audit caught nothing and just flooded the log). The prefix check holds on all 5.
    // NOTE: the binary does NO name audit at all — it skips the header via the hardcoded
    // constant (`v10 = 88` @0x4C647D then `Crt_Memcpy(v4, hMem + v10, 944 * v12)` @0x4C64C8).
    // This audit is a C++ addition; the header constants are NOT derived from the name, leave them alone.
    const std::string& embedded = img.TableName();
    if (!embedded.empty() && embedded.rfind(s.label, 0) != 0)
        TS2_WARN("DB %s : nom embarque different = '%s'", s.label, embedded.c_str());

    TS2_LOG("DB %s : %u enregistrements x %u o", s.label, out.count, out.stride);
    return true;
}

} // namespace

bool LoadGameDatabases(const std::string& gameDataDir, bool useTR) {
    bool allOk = true;
    for (const TableSpec& s : kTables) {
        DataTable& t = g_World.db.*(s.member);
        if (!LoadOneTable(gameDataDir, s, t, useTR))
            allOk = false; // still try the other tables
    }
    return allOk;
}

const LevelInfo* GetLevelInfo(int level) {
    if (level < 1) return nullptr;
    const uint8_t* r = g_World.db.level.record(static_cast<uint32_t>(level - 1));
    return reinterpret_cast<const LevelInfo*>(r); // nullptr if out of bounds
}

// ---------------------------------------------------------------------------
// GetRebirthExpSpan — rebirth EXP sub-table (mLEVEL 0x8E7208 + 0x18EC).
// See GameDatabase.h for the full proof (int32, not float: fidiv @0x67A64F).
//
// Table copied byte-for-byte from maybe_LevelTable_InitFloats 0x4C2380 (disasm re-read this
// mission), immediates of the 12 `mov dword ptr [reg+off], imm32`:
//   tier  off      imm32 (hex)   int32 value
//    1   0x18EC    39589228h      962105896   [EA 0x4c238a]
//    2   0x18F0    3BA3CB33h     1000590131   [EA 0x4c2397]
//    3   0x18F4    3E068168h     1040613736   [EA 0x4c23a4]
//    4   0x18F8    4081A54Dh     1082238285   [EA 0x4c23b1]
//    5   0x18FC    43163108h     1125527816   [EA 0x4c23be]
//    6   0x1900    45C528C0h     1170548928   [EA 0x4c23cb]
//    7   0x1904    488F9B05h     1217370885   [EA 0x4c23d8]
//    8   0x1908    4B76A138h     1266065720   [EA 0x4c23e5]
//    9   0x190C    4E7B5FFCh     1316708348   [EA 0x4c23f2]
//   10   0x1910    519F07A9h     1369376681   [EA 0x4c23ff]
//   11   0x1914    54E2D4C4h     1424151748   [EA 0x4c240c]
//   12   0x1918    58481079h     1481117817   [EA 0x4c2419]
// ---------------------------------------------------------------------------
int32_t GetRebirthExpSpan(int tier) {
    // Accessor guard 0x4C2BF0: outside [1..12] -> 0 (@0x4c2bf7 jl / @0x4c2c01 jle,
    // branch loc_4C2C03 `xor eax, eax`).
    if (tier < 1 || tier > 12) return 0;
    // 1-based index (the binary reads [this + 4*tier + 0x18E8], i.e. +0x18EC for tier=1).
    static const int32_t kRebirthExpSpan[13] = {
        0,           // [0] unused (the binary never accesses it: guard tier>=1)
        962105896,   // tier 1  — 39589228h
        1000590131,  // tier 2  — 3BA3CB33h
        1040613736,  // tier 3  — 3E068168h
        1082238285,  // tier 4  — 4081A54Dh
        1125527816,  // tier 5  — 43163108h
        1170548928,  // tier 6  — 45C528C0h
        1217370885,  // tier 7  — 488F9B05h
        1266065720,  // tier 8  — 4B76A138h
        1316708348,  // tier 9  — 4E7B5FFCh
        1369376681,  // tier 10 — 519F07A9h
        1424151748,  // tier 11 — 54E2D4C4h
        1481117817,  // tier 12 — 58481079h
    };
    return kRebirthExpSpan[tier];
}

const ItemInfo* GetItemInfo(uint32_t itemId) {
    if (itemId < 1) return nullptr;
    const uint8_t* r = g_World.db.item.record(itemId - 1);
    const ItemInfo* it = reinterpret_cast<const ItemInfo*>(r);
    // Empty slot (itemId == 0) => not found, like MobDb_GetEntry 0x4C3C00.
    return (it && it->itemId != 0) ? it : nullptr;
}

const SkillInfo* GetSkillInfo(uint32_t skillId) {
    if (skillId < 1) return nullptr;
    const uint8_t* r = g_World.db.skill.record(skillId - 1);
    const SkillInfo* sk = reinterpret_cast<const SkillInfo*>(r);
    // Empty slot (skillId == 0) => not found, like SkillGrowthTbl_GetRecord 0x4C4E90
    // (`if (*(base+776*(id-1))) return ... ; return 0;` — tests the record's 1st dword).
    return (sk && sk->skillId != 0) ? sk : nullptr;
}

const MonsterInfo* GetMonsterInfo(uint32_t monsterId) {
    if (monsterId < 1) return nullptr;                          // ItemDefTbl_GetRecord 0x4C6570: a2<1 => 0
    const uint8_t* r = g_World.db.monster.record(monsterId - 1); // base+944*(id-1); record() handles id>count
    const MonsterInfo* mi = reinterpret_cast<const MonsterInfo*>(r);
    // Empty slot (id==0) => not found, like the 1st-dword guard in 0x4C6570
    // (`if (*(base+944*(id-1))) return ... ; return 0;`).
    return (mi && mi->id != 0) ? mi : nullptr;
}

} // namespace ts2::game

// Game/ExtraDatabases.h — loader for 2 extra .IMG tables (005_00005/00006),
// distinct from the 5 tables in Game/GameDatabase.h (LEVEL/ITEM/SKILL/MONSTER/SOCKET).
//
// WARNING, MISLEADING IDA NAMES (verified via decompilation + neighboring ValidateRecord,
// same method as MobDb/ItemDefTbl documented elsewhere in this project):
//   005_00005.IMG -> IDA loader "SkillDefTbl_LoadImg" 0x4C6BD0 / validator "SkillDefTbl_ValidateRecord"
//                    0x4C65F0. THIS IS NOT a skill table: rec[0] = "Blacksmith Wu" + a
//                    large text/dialogue block, and the associated error is [Error::mNPC.Init()].
//                    -> It's the real NPC definition table, loaded into the "mNPC" manager.
//                    Renamed here NpcDefRecord / NpcDefTbl.
//   005_00006.IMG -> IDA loader "NpcTbl_LoadImg" 0x4C8090 / validator "NpcTbl_ValidateRecord" 0x4C78C0.
//                    THIS IS NOT an NPC table: rec[0] = "[Intro] Banker Bai & Beggar Xiao" +
//                    10 dialogue blocks, associated error [Error::mQUEST.Init()].
//                    -> It's the real QUEST definition table, loaded into the "mQUEST" manager.
//                    Renamed here QuestDefRecord / QuestDefTbl.
//
// Layouts deduced from the validators (same hardcoded bounds as the integrity guards of the 5
// tables in GameDatabase.h): offsets where a bound coincides with another known table's count
// (ITEM_INFO=99999, SKILL_INFO=300, LEVEL_INFO=145) are STRONG role hypotheses (bound
// correlation), not certainties proven by an observed accessor — flagged in comments.
//
// File envelope: [rawSize u32][packedSize u32][zlib] -> payload (see Asset_DecompressImg 0x53F5E0,
// same decoder as the 5 tables). Unlike the 5 tables in GameDatabase.h, there is NO embedded
// table name in the payload: records start directly at offset 4 (right after the counter),
// header=4 for both tables here.
#pragma once
#include "Game/GameState.h" // for ts2::game::DataTable
#include <cstdint>
#include <string>

namespace ts2::game {

// Typed records (byte-exact .IMG layout, deduced from the validation loops of
// NpcDefTbl_ValidateRecord 0x4C65F0 / QuestDefTbl_ValidateRecord 0x4C78C0).
#pragma pack(push, 1)

// NpcDefRecord — 11736 bytes. Table "mNPC" (005_00005.IMG). Empty slot if id == 0 (guard
// at the top of NpcDefTbl_ValidateRecord: `if (!*(DWORD*)rec) return 1;`).
// Cross-check VeryOldClient: NPC class (VeryOldClient/GameSystem/CNPC.cpp, singleton mNPC).
// Per-offset detail: Docs/TS2_TABLES_ROSETTA.md §6. NB: VeryOld nHeadImg[6] is a RUNTIME
// field (mNPC_nHeadImg overlay in CNPC::Init), ABSENT from the 11736-byte file record.
struct NpcDefRecord {
    uint32_t id;                 // +0     1..500, MUST equal index+1 (0 = empty slot); ex-VeryOldClient: nIndex (CONFIRMED)
    char     name[25];           // +4     NPC name (e.g. rec[0] = "Blacksmith Wu"), NUL within [0..24]; ex-VeryOldClient: nName (CONFIRMED)
    uint8_t  _pad29[3];          // +29    reserved (alignment)
    uint32_t fieldA;              // +32    (1..5) unknown role — precedes the text grid, possibly
                                   //        the number of active submenus/dialogues out of the 5 available.
                                   //        ex-VeryOldClient: nSpeechNum (PLAUSIBLE, resolves the guess)
    char     textGrid[5][5][51]; // +36    5x5 grid of strings (<=51 bytes, NUL-terminated) — NPC
                                   //        dialogue/menu text (5 "pages" x 5 lines, hypothesis).
                                   //        ex-VeryOldClient: nSpeech[5][5][51] (CONFIRMED, identical 5x5x51 structure)
    uint8_t  _pad1311[1];        // +1311  reserved (alignment)
    uint32_t fieldB;              // +1312  (1..5)  unknown role; ex-VeryOldClient: nTribe (PLAUSIBLE, tribes 1..4 + neutral?)
    uint32_t fieldC;              // +1316  (1..17) unknown role (17 ~ number of zones/maps?); ex-VeryOldClient: nType (PLAUSIBLE)
    uint32_t fieldD;              // +1320  (1..10000) unknown role (world coordinate? see ITEM_INFO fields 192/196/200, similar bounds)
                                   //        ex-VeryOldClient: nDataSortNumber2D (PLAUSIBLE, corrects the guess: 2D portrait image index/sort)
    // RESOLVED (Docs/TS2_NPC_MESH_DRAW.md §2-3, decompilation of Npc_DrawMesh 0x57FF00):
    // kindIndex+1 of the NPC's visual model. `Npc_DrawMesh` reads `*(DWORD*)(*this+1324) - 1` on
    // the runtime record pointed to by g_NpcRenderArray[i].ptr (resolved via
    // SkillDefTbl_GetRecord(mNPC, kindId) == GetNpcDefRecord() here) and uses it to index
    // g_NpcMeshCatalog (66 entries, stem "N%03d%03d001.SOBJECT") after checking Model_GetNpcMeshSlot
    // (hard bound 0x41=65, so fieldE must be [1,66]). ClientSource side: see
    // Gfx/ModelCache.h::GetForNpc, which computes kindIndex = fieldE - 1.
    uint32_t fieldE;              // +1324  kindIndex+1 of the NPC model (N*.SOBJECT), [1,66]; ex-VeryOldClient: nDataSortNumber3D (CONFIRMED, 3D model role proven)
    // fieldF[1] (+1332) RESOLVED (same doc, same function): NPC interaction/click range,
    // compared via Target_IsBeyondClickRange((float*)this+5, fieldF[1]) -- height/radius of the
    // camera anti-clipping guard, same role as ItemInfo.drawSize for Char_Draw. fieldF[0]/[2]
    // remain of unknown role.
    uint32_t fieldF[3];           // +1328  3x (1..1000) [1]=interaction range (RESOLVED), [0]/[2] unknown; ex-VeryOldClient: nSize[3] (PLAUSIBLE)
    uint32_t fieldG[100];         // +1340  100x (1..2) — probably boolean flags (state/availability); ex-VeryOldClient: nMenu[100] (PLAUSIBLE, 100 active/inactive menu entries)
    // <100000 per value: strong correlation with ITEM_INFO (integrity guard = 99999 items,
    // see GameDatabase.h). Hypothesis: item ids sold by this merchant NPC (3 categories x 28 slots).
    // ex-VeryOldClient: nShopInfo[3][28] (PLAUSIBLE, identical 3x28 structure + shop role).
    uint32_t shopItemIds[3][28];  // +1740
    // <=300 per value: strong correlation with SKILL_INFO (integrity guard = 300 skills,
    // see GameDatabase.h). Hypothesis: skill ids taught by this NPC (3x8).
    // ex-VeryOldClient: nSkillInfo1[3][8] (PLAUSIBLE, identical 3x8 structure).
    uint32_t teachSkillIds[3][8]; // +2076
    // <=300 per value: same bound as SKILL_INFO. Hypothesis: prerequisite/cost matrix for
    // skills (3 groups x 3 x 3 x 8 slots) — nested structure not fully elucidated.
    // ex-VeryOldClient: nSkillInfo2[3][3][3][8] (PLAUSIBLE, identical 3x3x3x8 structure).
    uint32_t skillMatrix[3][3][3][8]; // +2172
    // <=100000000 (1e8) per value, indexed by 145 (== LEVEL_INFO guard) x 15. Hypothesis: cost
    // table (gold?) per player level x 15 slots (e.g. skill training cost scaling with level).
    // ex-VeryOldClient: nGambleCostInfo[145][15] (PLAUSIBLE, gamble cost per level).
    uint32_t levelCostTable[145][15]; // +3036
};
static_assert(sizeof(NpcDefRecord) == 11736, "NpcDefRecord must be 11736 bytes");

// QuestDefRecord — 8444 bytes. Table "mQUEST" (005_00006.IMG). Empty slot if name == ""
// (guard at the top of QuestDefTbl_ValidateRecord: `if (!Crt_Strcmp(rec->name, "")) return 1;` —
// UNLIKE the other tables, it is NOT id==0 that marks an empty slot here).
// Cross-check VeryOldClient: QUEST class (VeryOldClient/GameSystem/CQUEST.cpp, singleton mQUEST).
// STRONG semantic cross-check (Docs/TS2_TABLES_ROSETTA.md §7): the STRING field (name) is the
// empty-slot marker in BOTH builds (VeryOld qSubject = "emptiness key").
struct QuestDefRecord {
    uint32_t id;                 // +0    1..1000, MUST equal index+1; ex-VeryOldClient: qIndex (CONFIRMED)
    char     name[51];           // +4    quest title (e.g. rec[0] = "[Intro] Banker Bai & Beggar Xiao"); ex-VeryOldClient: qSubject[51] (CONFIRMED, emptiness key)
    uint8_t  _pad55[1];          // +55   reserved (alignment)
    // +56/+60 = the table's COMPOSITE KEY (RESOLVED — NpcTbl_FindByTypeAndId 0x4C8340):
    // the binary NEVER looks up a quest by row index, it scans comparing
    // `rec[56] == element0 + 1` (the +1 is applied to the ARGUMENT, EA 0x4C839E; cmp 0x4C83A1)
    // AND `rec[60] == questId` (cmp 0x4C83BA). Proven by data (005_00006.IMG, 688
    // non-empty rows): histogram of +56 = {1:207, 2:207, 3:207, 4:67} (= the 4 elements)
    // and 678 of 688 rows have +60 != id — the row index is NOT the key.
    uint32_t fieldA;              // +56   (1..4)    element0 + 1 (player's element/faction, 0-based +1)
    uint32_t fieldB;              // +60   (1..1000) quest id WITHIN the element (NOT the row id)
    // +64 CONSUMED (RESOLVED): Quest_CheckObjectiveState 0x50FF10, "implied" branch
    // (EA 0x50FF80/0x50FF86) -> `cmp g_SelfLevel[base+0xA038], [rec+0x40]` then `jge`: LEVEL
    // gate for the NEXT quest. Intro data is consistent (1,1,1,1,2,4,6,8,10,12).
    uint32_t levelReq;            // +64   (1..145)  required level; ex-VeryOldClient: qLevel (CONFIRMED, consumed @0x50FF86)
    uint32_t fieldD;              // +68   (1..2)   unknown role — flag (repeatable?)
    // +72 RESOLVED: Pkt_SmithUpgradeResult 0x48E7D0 @0x48E929 reads `[rec+0x48]` and writes it
    // into g_QuestObjType 0x16745FC; Quest_CheckObjectiveState 0x50FF10 @0x50FFFC uses it as the
    // selector of its 8-case switch. Data: {1:227,2:57,3:38,4:69,5:127,6:7,7:88,8:75}.
    uint32_t fieldE;              // +72   (1..8)    objective type (selector of switch 0x50FFFC)
    uint32_t fieldF;              // +76   (0..200) unknown role
    uint8_t  _unk80[12];          // +80   12 bytes NOT covered by the validator (no guard observed)
    uint32_t fieldG;              // +92   (1..500) "variant A" result/id (v10[23], Quest_GetObjectiveResult 0x510520)
    uint32_t fieldH[5];           // +96   5x (0..500) [0] = "variant B" result/id (v10[24]); [1..4] unknown role
    uint32_t fieldI;              // +116  (1..500) "objective completed" result/id (v10[29])
    // +120/+124 RESOLVED — the comment "16 bytes NOT covered" was WRONG (not validated by the
    // validator != not consumed). Dual proof:
    //  (a) IDA: Pkt_SmithUpgradeResult @0x48E881/0x48E884 reads `[rec+0x78]` (=+120) and writes it
    //      as an ITEM ID into g_InvMain; Quest_CheckObjectiveState compares +120 (target) and
    //      +124 (required quantity, EA 0x51002A: `progress >= [v10+124]` -> state 3 "completed").
    //  (b) Data: "[Intro] Kill one Goblin!" +120=1 +124=1; "Kill 5 Dragon Priests!"
    //      +120=2 +124=5; "Kill 10 Killer Fish!" +120=3 +124=10. Perfect correlation with
    //      the switch: types 2/3/4/8 (never read +124) have +124==0; type 7 (never reads +120)
    //      has +120==0.
    uint32_t objectiveTarget;     // +120  objective target (mob/item/npc id depending on the +72 type)
    uint32_t objectiveRequired;   // +124  required quantity / 2nd target (phase 2 of type 6)
    uint8_t  _gap128[8];          // +128  NOT proven: 0 across the 688 non-empty rows, no reader observed
    // 3 pairs (category 1..6, value <=1e8): reward table (item/gold/exp x amount).
    // CONFIRMED by Quest_GetRewardItemId 0x510A10: loop `i<3` over `[rec + 8*i + 0x88]`
    // (=+136, category) and `[rec + 8*i + 0x8C]` (=+140, value); category==6 => value = item
    // id. ex-VeryOldClient: qReward[3][2] (type 1..6 / value) (CONFIRMED, same shape).
    struct { uint32_t category; uint32_t value; } rewards[3]; // +136
    uint32_t fieldK;              // +160  (0..1000) unknown role
    // 10 dialogue blocks (matches the disassembly comment "10 dialogue blocks"):
    // each block = 15 lines of text (<=51 bytes, NUL-terminated, VALIDATED) + 63 bytes of
    // block trailer NOT validated by NpcTbl_ValidateRecord (per-block metadata — speaker? flags? — unknown).
    struct {
        char    lines[15][51]; // dialogue text (up to 15 lines per block)
        uint8_t _tail[63];     // NOT validated by the disassembly — unknown role
    } dialogue[10];               // +164
};
static_assert(sizeof(QuestDefRecord) == 8444, "QuestDefRecord must be 8444 bytes");

#pragma pack(pop)

// API.

// State of the 2 extra tables (kept separate from game::GameDatabases so as not to
// modify GameState.h — see the standalone-module rule).
struct ExtraDatabases {
    DataTable npc;   // NpcDefRecord   (11736 bytes) — manager "mNPC",   005_00005.IMG
    DataTable quest; // QuestDefRecord (8444 bytes)  — manager "mQUEST", 005_00006.IMG
};

// Single global instance (mirrors g_World.db but outside GameState.h).
inline ExtraDatabases g_ExtraDb;

// Loads the 2 .IMG tables into g_ExtraDb. `gameDataDir` = "GameData" root (files
// live under <gameDataDir>\G03_GDATA\D01_GIMAGE2D\005\). Returns true if BOTH tables are
// loaded and validated (counter guard OK + *_ValidateRecord loop OK on every
// record, like the original which fails on the first invalid ValidateRecord).
//
// `useTR` = state of flag g_UseTRVariant 0x1669190 (field 1 of the cmdline, written @0x460C48).
// At 1, BOTH tables switch to ...\005\TR\: their loaders test the flag
// (`cmp ds:g_UseTRVariant, 1` @0x4C6BD9 for 005_00005, @0x4C8099 for 005_00006).
// Default `false` = historical EU behavior (test callers have nothing to change).
bool LoadExtraDatabases(const std::string& gameDataDir, bool useTR = false);

// Typed NpcDefRecord accessor. `npcId` 1-based (1..500); nullptr out of bounds OR empty slot (id==0).
const NpcDefRecord* GetNpcDefRecord(uint32_t npcId);

// Typed QuestDefRecord accessor BY ROW INDEX. `questId` 1-based (1..1000); nullptr out of
// bounds OR empty slot (empty name — see this table's different empty-check semantics).
// WARNING — THIS IS NOT the binary's access mode: no function in TwelveSky2.exe
// resolves a quest by row index. The binary's sole resolver is
// NpcTbl_FindByTypeAndId 0x4C8340 = a composite (element, id) scan -> see
// FindQuestDefByElementAndId below. Indexing by id is DISPROVEN by the asset itself: of
// the 688 non-empty rows in 005_00006.IMG, 678 have +60 != id (only the first 10, those
// of element 1, coincide). Kept for historical callers / debugging; all new code
// must use FindQuestDefByElementAndId.
const QuestDefRecord* GetQuestDefRecord(uint32_t questId);

// NpcTbl_FindByTypeAndId 0x4C8340 — THE resolver for the mQUEST table (the binary always
// calls it with `ecx = offset mQUEST 0x8E71E4`). Composite linear scan over g_ExtraDb.quest;
// keeps the FIRST row satisfying, IN THIS ORDER:
//   (a) name not empty       — Crt_Strcmp(rec+4, "") != 0   (push String 0x7EC95F @0x4C8365,
//                             call Crt_Strcmp 0x75CF20 @0x4C837E)
//   (b) rec[56] == element0 + 1                            (add edx,1 @0x4C839E; cmp @0x4C83A1)
//   (c) rec[60] == questId                                 (cmp @0x4C83BA; jnz @0x4C83BD)
// Returns `base + 8444*i` @0x4C83CE, or nullptr @0x4C83D4 (no row found).
// `element0` = local 0-based element (g_LocalElement 0x1673194 == g_World.self.element); the
// +1 is applied TO THE ARGUMENT, not to the field.
const QuestDefRecord* FindQuestDefByElementAndId(int element0, int questId);

} // namespace ts2::game

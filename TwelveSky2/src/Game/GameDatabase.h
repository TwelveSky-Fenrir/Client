// Game/GameDatabase.h — loader for the static data tables (.IMG 005_*) into g_World.db.
//
// Faithfully reproduces the client's 5 loaders (cluster 0x4C2680..0x4C7390):
//   005_00001.IMG -> LEVEL_INFO   (LevelTable_LoadImg 0x4C2680, magic 0xE31,   145 x 44 bytes)
//   005_00002.IMG -> ITEM_INFO    (MobDb_LoadImg      0x4C3930, magic 0x1CB3, 99999 x 436 bytes)
//   005_00003.IMG -> SKILL_INFO   (SkillGrowthTbl...  0x4C4BC0, magic 0xC7E,   300 x 776 bytes)
//   005_00004.IMG -> MONSTER_INFO (ItemDefTbl_LoadImg 0x4C62A0, magic 0x1583, 10000 x 944 bytes)
//   005_00010.IMG -> SOCKET_INFO  (AnchorTbl_LoadImg  0x4C7390, magic 0xFDB,  3031 x 20 bytes)
//
// Envelope [rawSize][packedSize][zlib] -> payload; payload[0]^magic == count (hardcoded
// integrity guard); records at offset `header`, fixed stride. Cf.
// Docs/TS2_IMG_FORMAT.md and Docs/TS2_GAMEPLAY_LOGIC.md (layouts proven by the validators).
#pragma once
#include "Game/GameState.h"
#include <cstdint>
#include <string>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Typed records (byte-exact layout of the .IMG files).
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

// LEVEL_INFO — 44 bytes = 11 dwords. Fields proven by LevelTable_ValidateEntry 0x4C2430.
// Base stats are read by LevelTable_GetStatK (Stat3=extAtk .. Stat9=ratingMax);
// there is NO MP base in this table (cf. TS2_GAMEPLAY_LOGIC.md 2.4).
// Cross-check against VeryOldClient: class LEVEL (VeryOldClient/GameSystem/CLEVEL.cpp). Header
// +0..+12 CONFIRMED; stats block +16..+40 = CONFLICT J-1 (block REORDERED between builds +
// no MP base in this build) — cf. Docs/TS2_TABLES_ROSETTA.md §2 + Journal J-1. IDA wins,
// VeryOld names are a semantic hint ONLY, never an offset/value transposition.
struct LevelInfo {
    uint32_t level;            // +0  1..145 (must equal index+1); ex-VeryOldClient: lIndex (CONFIRMED)
    int32_t  expCumul;         // +4  cumulative experience (< expNext ; == expCumul[level+1]-1); ex-VeryOldClient: lRangeInfo[0] (expMin) (CONFIRMED)
    int32_t  expNext;          // +8  next level threshold (1..2 000 000 000); ex-VeryOldClient: lRangeInfo[1] (expMax) (CONFIRMED)
    uint32_t meta;             // +12 meta field (<= 100); ex-VeryOldClient: lRangeInfo[2] (percent factor) (CONFIRMED, bound<=100 => %)
    // +16..+40: stats block, consumer-proven (Char_Calc* 0x4D0530/0x4D1830/0x4D2830/0x4D34B0/
    // 0x4D4ED0/0x4CD970/0x4CE3F0). VeryOld names this block in a DIFFERENT ORDER (CONFLICT J-1):
    // do NOT read the ex-VeryOldClient names below as the field's role — that name is
    // aligned by ORDINAL, contradicted by the IDA consumer (IDA wins).
    int32_t  baseExtAttack;    // +16 base external attack   (Stat3); ex-VeryOldClient: lAttackPower  (CONFLICT J-1 — IDA=EXT atk)
    int32_t  baseIntAttack;    // +20 base internal attack   (Stat4); ex-VeryOldClient: lDefensePower (CONFLICT J-1 — IDA=INT atk, opposite meaning)
    int32_t  baseExtDefense;   // +24 base external defense  (Stat5); ex-VeryOldClient: lAttackSuccess(CONFLICT J-1 — IDA=EXT def)
    int32_t  baseIntDefense;   // +28 base internal defense  (Stat6); ex-VeryOldClient: lAttackBlock  (CONFLICT J-1 — IDA=INT def)
    int32_t  baseMaxHp;        // +32 base max HP            (Stat7); ex-VeryOldClient: lElementAttack(CONFLICT J-1 — IDA=MAX HP)
    int32_t  baseAtkRatingMin; // +36 base min damage rating (Stat8); ex-VeryOldClient: lLife        (CONFLICT J-1 — real HP is +32)
    int32_t  baseAtkRatingMax; // +40 base max damage rating (Stat9); ex-VeryOldClient: lMana        (CONFLICT J-1 — lMana DOES NOT EXIST in this build)
};
static_assert(sizeof(LevelInfo) == 44, "LevelInfo must be 44 bytes");

// ITEM_INFO — 436 bytes. Fields proven by MobDb_ValidateEntry 0x4C2C50 + TS2_GAMEPLAY_LOGIC.md 2.4.
// `fieldNNN` fields are validated (known bounds) but their role remains to be documented.
// The stat offsets (+292..+432) are the ones consumed by the stat engine (Char_Calc*).
// Cross-check against VeryOldClient: class ITEM (VeryOldClient/GameSystem/CITEM.cpp, singleton mITEM;
// accessor ITEM::ReturnDataAddr == MobDb_GetEntry 0x4C3C00). WARNING: MODEL MISMATCH: VeryOld has
// 5 attrs (Str/Dex/Vit/Int/Luck) whereas IDA only has 4 contribution fields (+292/296/300/304) —
// do NOT map 1:1. Per-offset detail: Docs/TS2_TABLES_ROSETTA.md §3 + Journal J-A/J-B/J-C.
struct ItemInfo {
    uint32_t itemId;         // +0   1-based; 0 = empty slot. (== index+1); ex-VeryOldClient: iIndex (CONFIRMED)
    char     name[25];       // +4   name (NUL terminator within [0..24]); ex-VeryOldClient: iName (CONFIRMED)
    // +29: 3x51 layout PROVEN (436-byte record); the `model` LABEL is NOT proven by a reader.
    // ex-VeryOldClient: iDescription[3][51] (PLAUSIBLE, same dims — J-A: relabel model->description review recommended).
    char     model[3][51];   // +29  3 model names (51 bytes each)
    uint8_t  _pad182[2];     // +182 reserved
    // +184: SECONDARY classifier, proven (Item_MeetsEquipRequirement 0x64ECD0).
    // ex-VeryOldClient: iType/iSort (PLAUSIBLE, J-B — iType(1..8)/iSort(1..99) don't cover 1..6; values not transposed).
    uint32_t category;       // +184 category (1..6 ; 5=equip/weapon, 6=class4)
    // +188: MASTER classifier (Item_GetEquipCategory 0x54C940, Weapon_ClassFromEquip 0x4CC9F0,
    // Item_IsUpgradeableWeapon 0x4C9960 ==28=weapon). ex-VeryOldClient: iType (CONFIRMED role;
    // WARNING: ranges diverge IDA 1..36 vs VeryOld 1..8 — name aligned, values NEVER transposed).
    uint32_t typeCode;       // +188 type (1..36 ; 28=weapon, 29=elemental weapon, 30=mount/costume, 35/36=pet)
    // +192 IconID (1-based, do NOT confuse with itemId/+0): index into the icon pool
    // g_AssetMgr_ItemIconSlots (folder G03_GDATA\D01_GIMAGE2D\002\, 4000 slots), formatted
    // as "002_%05u.IMG". Confirmed by decompilation of cGameHud_Render 0x64A900
    // (`v133 = *(ITEM_INFO+192) - 1` = 0-based pool index; Sprite2D_BuildPath shows that
    // the file suffix = 0-based pool index + 1 = IconID as-is, so NO offset needs to be
    // applied between this field and the file number). See Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md.
    uint32_t iconId;         // +192 (1..10000); ex-VeryOldClient: iDataNumber2D (CONFIRMED, 0-based icon index)
    uint32_t field196;       // +196 (<= 10000); ex-VeryOldClient: iDataNumber3D (PLAUSIBLE, 2D adjacency)
    uint32_t field200;       // +200 (<= 10000); ex-VeryOldClient: iAddDataNumber3D (PLAUSIBLE)
    uint32_t itemLevel;      // +204 item level (1..145 ; scaling thresholds 45/100/113/146); ex-VeryOldClient: iLevel (CONFIRMED)
    uint32_t field208;       // +208 (<= 12)  ; ex-VeryOldClient: iMartialLevel ? (PLAUSIBLE, undecided in Rosetta)
    // +212: required FACTION. Eligibility guard @0x64ECF5 (Item_MeetsEquipRequirement
    // 0x64ECD0): `if (a2[53] != 1 && a2[53] - 2 != g_LocalElementSecondary) return 0` —
    // value 1 = "any faction" (wildcard), otherwise (value - 2) must equal
    // g_LocalElementSecondary 0x1673198 (= g_World.self.elementSecondary).
    uint32_t field212;       // +212 (1..4) required faction ; 1 = any [0x64ECF5]
    uint32_t subtype;        // +216 subtype (1..14 ; 2/4/5/6/7/9=class1, 11..14=class2 gems); ex-VeryOldClient: iEquipInfo[2] (CONFIRMED, equip slot code)
    // +220: purchase PRICE in the secondary currency, NOT in gold. Proven by
    // UI_NpcShop_OnRDown_Buy 0x5E5000: `mov ecx, ds:g_InvWeight / cmp ecx, [ebp+var_20] /
    // jge` @0x5e548e-0x5e5497 -> compares against g_InvWeight 0x16732AC (IDB misnomer: this
    // global is a CURRENCY, cf. Net/GameHandlers_Misc.cpp:314/361 which writes `p.money`
    // there and subtracts 100000000). Modifiers proven at the same site: x0.9 if
    // g_SelfMorphNpcId==291 (ftol(v32[55] * 0.9)), and x99 (quantity) if typeCode(+188)==2
    // (stackable). Failure -> StrTable005_Get(0xD6=214). ex-VeryOldClient: iBuyCost (CONFIRMED role = price).
    uint32_t field220;       // +220 (1..2 000 000 000) price in secondary currency [0x5e5497]
    uint32_t field224;       // +224 (<= 2 000 000 000)
    // +228: purchase PRICE IN GOLD (not a stat prerequisite — correction W7). Proven by
    // UI_NpcShop_OnRDown_Buy: `mov ecx, ds:g_Currency / cmp ecx, [eax+0E4h] / jge`
    // @0x5e54c2-0x5e54ce -> compares against g_Currency 0x1673180 (= g_World.self.currency,
    // displayed as « Or : %d » by UI/GameHud.cpp). No discount and no x99, unlike +220.
    // Failure -> StrTable005_Get(0x586=1414).
    uint32_t field228;       // +228 (<= 2 000 000 000) price in gold [0x5e54ce]
    // +232/+236: SUMMED equip requirement (the only one proven). Guard @0x64ED49:
    // `if (a2[59] + a2[58] > g_SelfLevelBonus + g_SelfLevel) return 0` — i.e.
    // field236 + field232 > self.levelBonus + self.level => refused. Both terms are
    // homogeneous with the two globals: +232 (1..145) = level, +236 (<=12) = rebirth
    // tier. NOTE: this is NOT itemLevel (+204), which is only used for stat scaling.
    uint32_t field232;       // +232 (1..145) required level (term 1/2) [0x64ED49]
    uint32_t field236;       // +236 (<= 12) required rebirth tier (term 2/2) [0x64ED49]
    uint32_t flags[11];      // +240 11 flags (each 1..2) ; ex-VeryOldClient: iCheck* family (~13 drop/sell/trade/... flags) (PLAUSIBLE, FAMILY correlation, 11 vs 13 counts diverge)
    uint32_t skillFlag;      // +284 1=normal, 2=skill, 3=upgrade (scalingMode ; Equip_ComputeGemSetBonus 0x54E420 ==2) ; no aligned VeryOld field (PLAUSIBLE, IDA role)
    uint32_t field288;       // +288 (< 366)
    // +292..+304: attrs block. WARNING: MODEL MISMATCH (J-C): 5 VeryOld attrs vs 4 IDA fields — no 1:1 mapping.
    int32_t  attrPrimaryA;   // +292 external Force base (ext atk/def) ; ex-VeryOldClient: iStrength (external Force / waegong) (PLAUSIBLE J-C)
    int32_t  attrPrimaryB;   // +296 internal Force base (int atk/def) ; ex-VeryOldClient: iIntelligent (internal Force / naegong) (PLAUSIBLE, same model divergence)
    int32_t  attrRatingMin;  // +300 min rating base / internal def (x0.9) ; secondary defensive attr (Char_SumGemStatC slot4) ; no VeryOld field (PLAUSIBLE)
    int32_t  attrRatingMax;  // +304 max rating base ; secondary offensive attr (Char_SumGemStatA slots 2,7) ; no VeryOld field (PLAUSIBLE)
    int32_t  critRate;       // +308 flat crit rate ; ex-VeryOldClient: iCritical (PLAUSIBLE, named by bounds, outside the reader-proven block 312..336)
    int32_t  extAttack;      // +312 flat external attack (Char_CalcExternalAttack) ; ex-VeryOldClient: iAttackPower (CONFIRMED, VeryOld has a SINGLE iAttackPower => aligned on external)
    int32_t  intAttack;      // +316 flat internal attack (Char_CalcInternalAttack) ; internal variant (PLAUSIBLE, VeryOld does not distinguish int/ext)
    int32_t  extDefense;     // +320 flat external defense (Char_CalcExternalDefense) ; ex-VeryOldClient: iDefensePower (CONFIRMED, aligned on external def)
    int32_t  intDefense;     // +324 flat internal defense (Char_CalcInternalDefense) ; no distinct VeryOld field (PLAUSIBLE)
    int32_t  maxHp;          // +328 flat max HP (reader-proven) ; absent from VeryOld (CONFIRMED IDA side, null VeryOld hint)
    int32_t  maxMp;          // +332 flat max MP (Char_CalcMaxMP) ; absent from VeryOld (PLAUSIBLE, named by bounds)
    int32_t  accuracy;       // +336 flat accuracy (reader-proven) ; ex-VeryOldClient: iAttackSucess (CONFIRMED, accuracy = attack success rate)
    uint32_t field340;       // +340 (<= 16)
    uint32_t field344;       // +344 (<= 10000)
    uint32_t field348;       // +348 (<= 300) ; acquired skill id (cat 5 = manual ; Pkt_ItemActionDispatch 0x46A320 G0) ; ex-VeryOldClient: iGainSkillNumber (CONFIRMED)
    uint32_t field352;       // +352 (<= 100)
    uint32_t field356;       // +356 (<= 1000)
    int32_t  regen;          // +360 regen / MP cost reduction % (Player_CastSkill 0x53BC40) ; no aligned VeryOld field (PLAUSIBLE)
    int32_t  evasion;        // +364 flat evasion ; ex-VeryOldClient: iAttackBlock (block/makgi) (PLAUSIBLE — block != evasion (hoepi), to be resolved dynamically, cf. GAP-4)
    int32_t  resistAll;      // +368 overall elemental resistance ; ex-VeryOldClient: iElementDefensePower (PLAUSIBLE, not proven by a named reader)
    // +372..+435 closes the 436-byte record. ex-VeryOldClient: composite tail (iPotionType[2], iLastAttackBonusInfo[2],
    // iCapeInfo[3], iBonusSkillInfo[N][2]) (PLAUSIBLE, STRUCTURAL correlation of key/value pairs, not field-by-field).
    struct { int32_t key; int32_t val; } resist[8]; // +372..+432 : 8 (key,value) pairs
};
static_assert(sizeof(ItemInfo) == 436, "ItemInfo must be 436 bytes");

// SKILL_INFO — 776 bytes. Table SKILL (005_00003.IMG), loaded raw into kTables[2]
// (g_World.db.skill). Layout PROVEN byte-by-byte by SkillGrowthTbl_ValidateRecord 0x4C4160
// (reads each offset directly), the interpolator SkillGrowthTbl_InterpStatByLevel 0x4C4EE0
// (statMin/statMax blocks + levelNorm denominator), and the accessor SkillGrowthTbl_GetRecord
// 0x4C4E90 (base + 776*(id-1)). Cross-check against VeryOldClient: class SKILL (VeryOldClient/
// GameSystem/CSKILL.cpp, singleton mSKILL ; SKILL::ReturnDataAddr == SkillGrowthTbl_GetRecord).
// Per-offset detail: Docs/TS2_TABLES_ROSETTA.md §4. `fieldNNN` fields carry the bound PROVEN
// by the validator ; +568/+572 are a GAP (dwords [142]/[143] unassigned).
struct SkillInfo {
    uint32_t skillId;             // +0   1..300, MUST equal index+1 (0 = empty slot) [SkillGrowthTbl_ValidateRecord 0x4C4160] ; ex-VeryOldClient: sIndex (CONFIRMED)
    char     name[25];            // +4   name (NUL-term within [0..24], rec0 "Mercy Healing") [validator: scans 25 bytes -> fails if no NUL] ; ex-VeryOldClient: sName[25] (CONFIRMED)
    char     description[10][51]; // +29  10 NUL-terminated 51-byte strings [validator: 10 x 51 loop] ; ex-VeryOldClient: sDescription[10][51] (CONFIRMED, identical dims)
    uint8_t  _pad539[1];          // +539 reserved (aligns dword[135] to +540)
    uint32_t field540;            // +540 (validator [1,4]) ; ex-VeryOldClient: element_flag ? (PLAUSIBLE, Rosetta §4 dword[135])
    uint32_t category;            // +544 (validator [1,5]) skill type (4/5 = posture/stance) [Player_CastSkill 0x53BC40] ; ex-VeryOldClient: sType (CONFIRMED role, Rosetta §4)
    uint32_t field548;            // +548 (validator [1,10000]) ; ex-VeryOldClient: subcategory / sAttackType (PLAUSIBLE, Rosetta §4 dword[137])
    uint32_t field552;            // +552 (validator [1,4]) ; ex-VeryOldClient: req_element/branch / sTribeInfo[2] (PLAUSIBLE, Rosetta §4 dword[138])
    uint32_t field556;            // +556 (validator [1,10]) ; req_weapon_type (1=any, else item.+188 - 11) (PLAUSIBLE, Rosetta §4 dword[139])
    uint32_t spCost;              // +560 (validator [1,1000]) skill-point cost (learning: dword_16731D4 -= rec[0x230]) ; ex-VeryOldClient: sLearnSkillPoint (CONFIRMED, Rosetta §4)
    uint32_t levelNorm;           // +564 (validator [1,1000]) interpolation denominator = level cap [SkillGrowthTbl_InterpStatByLevel 0x4C4EE0 : / (int)rec[141]] ; ex-VeryOldClient: sMaxUpgradePoint (CONFIRMED)
    uint32_t field568;            // +568 (validator [0,10])  GAP (dword[142] unassigned, Rosetta §4)
    uint32_t field572;            // +572 (validator [0,1000]) GAP (dword[143] unassigned, Rosetta §4)
    // +576 / +676: two blocks of 25 stats interpolated per level (v = statMin + level *
    // (statMax - statMin) / levelNorm, INTEGER division) [SkillGrowthTbl_InterpStatByLevel
    // 0x4C4EE0, cases 1..25: statMin = rec[144+(k-1)], statMax = rec[169+(k-1)]]. statIndex 1
    // = MP cost (Player_CastSkill / Skill_CastStoredAtTarget 0x53E740 : g_SelfMana -= cost).
    // ex-VeryOldClient: sGradeInfo[0]/[1] (min/max grade) (CONFIRMED ; sub-fields 22 vs 25 =
    // different build, values never transposed).
    int32_t  statMin[25];         // +576 (dword[144..168]) low bounds per tier
    int32_t  statMax[25];         // +676 (dword[169..193]) high bounds per tier (closes the 776-byte record)
};
static_assert(sizeof(SkillInfo) == 776, "SkillInfo must be 776 bytes");

// Drop table entry (8 bytes) — {u32<=1000000 ; u32<100000} per validator
// ItemDefTbl_ValidateRecord 0x4C5F60 (pairs @+448 dropA[5] and @+544 dropB[50], not 8 bytes).
struct DropEntry { uint32_t itemId; uint32_t chance; };
static_assert(sizeof(DropEntry) == 8, "DropEntry must be 8 bytes");

// MONSTER_INFO — 944 bytes. Table MONSTER (005_00004.IMG), loaded raw into kTables[3]
// (g_World.db.monster). IDB MISNOMER: the symbols "ItemDefTbl_*" actually load/validate/read
// the MONSTER_INFO file (embedded name "MONSTER_INFO", rec[0]="Goblin"), NOT ITEM_INFO. Layout
// PROVEN byte-by-byte by ItemDefTbl_ValidateRecord 0x4C5F60 (reads each offset directly);
// accessor STRICTLY 1-based ItemDefTbl_GetRecord 0x4C6570 (base+944*(id-1), rejects
// id<1||id>count, guards 1st dword!=0). Two fields are proven by a RENDER consumer,
// Char_Draw 0x5805C0: kindIndexP1 (+244, `*(def+244)-1` = 1-based visual model index) and
// hpMax (+368, damage-state divisor). Cf. Docs/TS2_MONSTER_NPC_MODEL.md.
struct MonsterInfo {
    uint32_t id;                 // +0    1-based, ==index+1 (validator: <1||>10000 ; ==a2+1)
    char     name[25];           // +4    name (scans 25 bytes, fails if no NUL) — rec0 "Goblin"
    // +29: 2 optional 101-byte strings (validator: for j<2, k<101 @ 101*j+k+29). ALWAYS
    // EMPTY on real mobs — this is NOT a "model name" field (cf. Gfx/ModelCache.h:
    // the model is resolved via printf from kindIndexP1, not stored here). Role not resolved.
    char     textBlock[2][101];  // +29   (2x101 = 202 bytes -> closes at +231)
    uint8_t  _pad231[1];         // +231  align (29+202=231 ; field232 at +232)
    int32_t  field232;           // +232  [1,15] type/class ; ==2 => has damage states (Char_Draw)
    int32_t  field236;           // +236  [1,51]
    int32_t  field240;           // +240  [1,2]
    int32_t  kindIndexP1;        // +244  [1,10000] 1-based MODEL INDEX (Char_Draw : *(def+244)-1) — CRITICAL
    int32_t  collDim[3];         // +248  3x [1,1000] : collision radius sqrt(collDim[2]^2+collDim[0]^2)*0.5 (Pkt_SpawnMonster 0x467B00)
    uint32_t field260;           // +260  <=1000
    int32_t  field264;           // +264  [1,4]
    int32_t  field268;           // +268  [1,2]
    int32_t  field272[6];        // +272  6x [1,10000] (272..292 -> closes at +296)
    uint32_t field296;           // +296  <4
    uint32_t field300[3];        // +300  3x <=10000 (300..308 -> closes at +312)
    uint32_t field312;           // +312  <4
    uint32_t field316[3];        // +316  3x <=10000 (316..324 -> closes at +328)
    int32_t  field328;           // +328  [1,10000]
    int32_t  field332;           // +332  [1,10000]
    int32_t  rewardMin;          // +336  [1,1000000], +336<=+340 (min/max pair, role unproven)
    int32_t  rewardMax;          // +340  [1,1000000]
    int32_t  level;              // +344  [1,145] level
    uint32_t field348;           // +348  <=12
    int32_t  field352;           // +352  [1,1000]
    uint32_t field356;           // +356  <=1000
    uint32_t field360;           // +360  <=100000000
    uint32_t field364;           // +364  <=100000000
    int32_t  hpMax;              // +368  [1,2000000000] MAX HP — damage-state divisor (Char_Draw def[+368]) — CRITICAL
    int32_t  field372;           // +372  [1,6]
    uint32_t field376;           // +376  <=10000, +376<=+380
    uint32_t field380;           // +380  <=10000
    uint32_t field384;           // +384  <=1000
    uint32_t field388;           // +388  <=1000
    uint32_t field392;           // +392  <=1000
    uint32_t field396;           // +396  <=300000
    uint32_t field400;           // +400  <=200000
    uint32_t field404[4];        // +404  4x <=100000 (404..416 -> closes at +420)
    uint32_t field420;           // +420  <=100
    uint32_t field424;           // +424  <=100, +424<=+428
    uint32_t field428;           // +428  <=100
    int32_t  field432;           // +432  /100 in {0,2,3,4,5}, /100<=15
    uint32_t field436;           // +436  <=1000000
    uint32_t goldMin;            // +440  <=100000000, +440<=+444 GOLD min/max pair
    uint32_t goldMax;            // +444  <=100000000
    DropEntry dropA[5];          // +448  5x {u32<=1000000 ; u32<100000}, not 8 bytes (448..487 -> closes at +488)
    uint32_t field488[12];       // +488  12x <=1000000 (488..535 -> closes at +536)
    uint32_t field536;           // +536  <=1000000
    uint32_t field540;           // +540  <100000
    DropEntry dropB[50];         // +544  50x {u32<=1000000 ; u32<100000}, not 8 bytes (544..943 -> closes at +944)
};
static_assert(sizeof(MonsterInfo) == 944, "MonsterInfo must be 944 bytes");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// API.
// ---------------------------------------------------------------------------

// Loads the 5 .IMG tables into g_World.db. `gameDataDir` = "GameData" root
// (files are under <gameDataDir>\G03_GDATA\D01_GIMAGE2D\005\).
// Returns true if ALL tables are loaded and validated (count guard OK AND
// *_ValidateRecord loop OK on every record, like the original loaders which
// return 0 at the first invalid record — cf. @0x4C64F5..0x4C6527).
//
// `useTR` = state of flag g_UseTRVariant 0x1669190 (cmdline field 1, written @0x460C48).
// When 1, ONLY the ITEM (005_00002), SKILL (005_00003) and MONSTER (005_00004) tables switch
// to the localized subfolder ...\005\TR\: these are the only ones whose loader tests the flag
// (`cmp ds:g_UseTRVariant, 1` @0x4C3939 / @0x4C4BC9 / @0x4C62A9). LEVEL (005_00001) and SOCKET
// (005_00010) have NO TR branch in the binary and stay EU even in TR mode, even though
// 005\TR\005_00001.IMG exists on disk — this is intentional (fidelity).
// Default `false` = historical EU behavior (test callers need no changes).
bool LoadGameDatabases(const std::string& gameDataDir, bool useTR = false);

// LEVEL_INFO accessor. `level` 1-based (1..145); nullptr out of bounds.
// (the client indexes record[level-1].)
const LevelInfo* GetLevelInfo(int level);

// REBIRTH EXP sub-table — 12 dwords appended to the mLEVEL 0x8E7208 manager, right after
// the 145 LEVEL_INFO records (145 x 44 = 6380 bytes = 0x18EC: the sub-table therefore starts
// exactly at mLEVEL+0x18EC, i.e. dword index 1595).
//
// These 12 values are NOT loaded from 005_00001.IMG: they are HARDCODED in the binary
// by maybe_LevelTable_InitFloats 0x4C2380 (12 `mov dword ptr [reg+0x18EC..0x1918],
// imm32`, EA 0x4c238a..0x4c2419), itself called only by the static CRT initializer
// CrtInit_LevelTableThunk 0x7A5260 (single xref). Hardcoding them here is therefore FAITHFUL.
//
// GOTCHA: the IDB comment "12 float scaling constants" is WRONG — these are int32s.
// Hard proof in UI_GameHud_Render: `fidiv [ebp+var_850]` @0x67A64F (bar) and @0x67A696
// (text); FIDIV only accepts an integer memory operand (m32int), and var_850 receives
// the accessor's return value directly (@0x67a624 call -> @0x67a629 mov [ebp+var_850], eax).
// Corroboration: the 12 values (962M..1481M) fall under the EXP ceiling 0x77359400
// = 2 000 000 000 used @0x67a5ea; read as float they would equal 2e-4 .. 8.8e14.
//
// `tier` = rebirth tier (g_SelfLevelBonus 0x16731AC). Reproduces accessor
// 0x4C2BF0: guard `tier >= 1 && tier <= 12` (@0x4c2bf7 jl / @0x4c2c01 jle) else 0;
// read `[this + 4*tier + 0x18E8]` (@0x4c2c0d) — hence tier=1 -> +0x18EC.
int32_t GetRebirthExpSpan(int tier);

// ITEM_INFO accessor. `itemId` 1-based; index = itemId-1 (cf. MobDb_GetEntry 0x4C3C00,
// `base+436*(id-1)`, guards id!=0). ex-VeryOldClient: ITEM::ReturnDataAddr (CONFIRMED, identical logic).
// Returns nullptr out of bounds OR if the slot is empty (itemId field == 0).
const ItemInfo* GetItemInfo(uint32_t itemId);

// Weapon class 1..3 (0 = none) from the `typeCode` (+188) of an ITEM_INFO record.
// EXACT switch of Weapon_ClassFromField112 0x4CC870 (@0x4cc8be): {0xD,0x10,0x13}->1,
// {0xE,0x11,0x14}->2, {0xF,0x12,0x15}->3, else 0 (@0x4cc904). Shared by
// PlayerCmdController::WeaponClass (self's equipped weapon) and the CharSelect entry
// motion (LoginScene::GetEnterPreviewWeaponClass). The CALL to the getter stays with the caller.
inline int32_t WeaponClassFromTypeCode(uint32_t typeCode) {
    switch (typeCode) {                       // 0x4cc8be — record+188
    case 0xD: case 0x10: case 0x13: return 1;
    case 0xE: case 0x11: case 0x14: return 2;
    case 0xF: case 0x12: case 0x15: return 3;
    default:                        return 0; // 0x4cc904
    }
}

// SKILL_INFO accessor. `skillId` 1-based (1..300); index = skillId-1 (cf.
// SkillGrowthTbl_GetRecord 0x4C4E90, `base+776*(id-1)`, guards id!=0 & record[0]!=0).
// ex-VeryOldClient: SKILL::ReturnDataAddr (CONFIRMED, identical logic). Returns nullptr
// out of bounds OR if the slot is empty (skillId field == 0).
const SkillInfo* GetSkillInfo(uint32_t skillId);

// MONSTER_INFO accessor. `monsterId` 1-based; index = monsterId-1 (cf.
// ItemDefTbl_GetRecord 0x4C6570, base+944*(id-1), rejects id<1||id>count, guards 1st dword!=0).
// Pkt_SpawnMonster 0x467B00 passes the RAW network id (1-based) to this getter -> the C++
// caller manipulates a 1-based id, this accessor does the -1. Returns nullptr out of bounds
// OR if the slot is empty (id field == 0). Table = g_World.db.monster.
const MonsterInfo* GetMonsterInfo(uint32_t monsterId);

} // namespace ts2::game

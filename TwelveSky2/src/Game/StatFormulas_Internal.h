// Game/StatFormulas_Internal.h — shared file-local helpers for the Char_Calc* stat-formula
// split family (StatFormulas.cpp / StatFormulas_Attack.cpp / StatFormulas_Defense.cpp).
// Included only by that family; not part of the public Game/StatFormulas.h API. Everything
// below is inline or namespace-local (anonymous namespace => internal linkage per TU, so
// no ODR risk from being included by multiple .cpp files).
#pragma once
#include "Game/StatFormulas.h"
#include "Game/StatBonusContributors.h"   // Item_SumGemStatBonus, Char_SumGemStat*, SkillTree_SumBonuses
                                          // (already includes ItemSystem.h: Item_GetElementalBonus, etc.)
#include "Game/ClientRuntime.h"          // g_Client.VarGet — see the g_EquipAux block below

namespace ts2::game {
namespace {

// Crt_ftol 0x760810: double->int conversion by truncation toward zero (fistp after setting
// the rounding mode to chop). Equivalent to a C++ (int) cast.
inline int ftol(double d) { return static_cast<int>(d); }

// MobDb_GetEntry 0x4C3C00 (reduced): 1-based ITEM_INFO record; nullptr if empty.
inline const ItemInfo* itemRec(const GameDatabases& db, uint32_t id) {
    if (id == 0) return nullptr;
    const uint8_t* p = db.item.record(id - 1);       // index = id-1
    if (!p) return nullptr;
    const ItemInfo* it = reinterpret_cast<const ItemInfo*>(p);
    if (it->itemId == 0) return nullptr;              // empty slot
    return it;
}

// LevelTable_GetStatK 0x4C29C0..: dword field `fieldIdx` (4..10 = Stat3..Stat9) of the
// LEVEL_INFO record for level `lvl`, with tier cap (lvl>145 -> 145; outside [1..157] -> 0).
// fieldIdx in LevelInfo: 4=baseExtAttack(+16) .. 10=baseAtkRatingMax(+40).
inline int levelStat(const GameDatabases& db, int lvl, int fieldIdx) {
    if (lvl < 1 || lvl > 157) return 0;              // guard 0x4C29D4
    int L = (lvl > 145) ? 145 : lvl;                 // tier fallback 0x4C29E1
    const uint8_t* p = db.level.record(L - 1);
    if (!p) return 0;
    return reinterpret_cast<const int32_t*>(p)[fieldIdx];
}

// Item_ClassifyRecord 0x5509A0: category 0..9 (or -1 if null) of an ITEM_INFO record.
inline int classifyRecord(const ItemInfo* it) {
    if (!it) return -1;
    if (it->category == 5) {                         // a1[46]==5
        uint32_t st = it->subtype;                   // a1[54]
        if (st == 2 || st == 4 || st == 5 || st == 6 || st == 7 || st == 9) return 1;
        if (st == 11 || st == 12 || st == 13 || st == 14) return 2;
        switch (it->typeCode) {                      // a1[47]
            case 31: return 5;
            case 32: return 6;
            case 8:  return 8;
            case 29: return 9;
        }
    } else {
        if (it->category == 6) return 4;             // a1[46]==6
        if ((it->itemId >= 201 && it->itemId <= 218) ||
            (it->itemId >= 2303 && it->itemId <= 2305)) return 3;
        if (it->typeCode == 28) return 7;
    }
    return 0;
}

// Item_ClassifyById 0x550800: same, via id lookup.
inline int classifyById(const GameDatabases& db, uint32_t id) {
    if (id == 0) return -1;
    const ItemInfo* it = itemRec(db, id);
    if (!it) return -1;
    return classifyRecord(it);
}

// Item_GetSlotGroup 0x54D700: 30 if typeCode==30, else 3 (0 if id unknown). All of its
// original call sites operate on g_SpecialItem (0x1687310, absent) -> never called here.
[[maybe_unused]] inline int slotGroup(const GameDatabases& db, uint32_t id) {
    const ItemInfo* it = itemRec(db, id);
    if (!it) return 0;
    return (it->typeCode == 30) ? 30 : 3;
}

// sub_545610/545640/545670 0x545610..: bytes 0/1/2 of the socket word (item attribute).
inline int attrByte0(uint32_t socket) { return  socket        & 0xFF; }
// byte1 (sub_545640) is only consumed by neutralized skill terms [hook].
[[maybe_unused]] inline int attrByte1(uint32_t socket) { return (socket >> 8)  & 0xFF; }
inline int attrByte2(uint32_t socket) { return (socket >> 16) & 0xFF; }

// Per-cultivation growth tables (Char_AttrGrowth_Field292/296/300/304
// 0x4CF030/0x4CFA90/0x4CEC20/0x4CF560). Indexed by growthIndex: tier bucket
// (thresholds 100/200/300/400) then step growthIndex%100 in [1..15]. Step 0 or outside
// [1..15] -> 0. Byte-exact reproduction of the original switches.
// [16]: index 0 unused.
const int G292_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G292_101_200[16] = {0,2,6,12,20,30,42,56,72,90,110,134,164,194,224,254};
const int G292_201_300[16] = {0,1,2,3,5,7,10,14,18,22,27,33,41,49,57,65};
const int G292_301_400[16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};
const int G292_gt400  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};

const int G296_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G296_101_200[16] = {0,1,2,3,5,7,10,14,18,22,27,33,41,49,57,65};
const int G296_201_300[16] = {0,3,8,15,25,37,52,70,90,112,137,167,205,243,281,319};
const int G296_gt300  [16] = {0,1,2,3,5,7,10,14,18,22,27,33,41,49,57,65};

const int G300_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G300_101_300[16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};
const int G300_301_400[16] = {0,2,6,12,20,30,42,56,72,90,110,134,164,194,224,254};
const int G300_gt400  [16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};

const int G304_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G304_101_200[16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G304_201_300[16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};
const int G304_301_400[16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G304_gt400  [16] = {0,2,6,12,20,30,42,56,72,90,110,134,164,194,224,254};

inline int pick(const int* t, int gi) { int k = gi % 100; return (k >= 1 && k <= 15) ? t[k] : 0; }

inline int growth292(int gi) {           // 0x4CF030
    if (gi > 400) return pick(G292_gt400, gi);
    if (gi > 300) return pick(G292_301_400, gi);
    if (gi > 200) return pick(G292_201_300, gi);
    if (gi > 100) return pick(G292_101_200, gi);
    return pick(G292_le100, gi);
}
inline int growth296(int gi) {           // 0x4CFA90 (bucket 101..200 == 201..300 merged? no: <=300/<=200/<=100)
    if (gi > 300) return pick(G296_gt300, gi);
    if (gi > 200) return pick(G296_201_300, gi);
    if (gi > 100) return pick(G296_101_200, gi);
    return pick(G296_le100, gi);
}
inline int growth300(int gi) {           // 0x4CEC20 (buckets <=400/<=300/<=100: 101..300 merged)
    if (gi > 400) return pick(G300_gt400, gi);
    if (gi > 300) return pick(G300_301_400, gi);
    if (gi > 100) return pick(G300_101_300, gi);
    return pick(G300_le100, gi);
}
inline int growth304(int gi) {           // 0x4CF560
    if (gi > 400) return pick(G304_gt400, gi);
    if (gi > 300) return pick(G304_301_400, gi);
    if (gi > 200) return pick(G304_201_300, gi);
    if (gi > 100) return pick(G304_101_200, gi);
    return pick(G304_le100, gi);
}

// Equipment snapshot (Char_BuildEquipSnapshot 0x4CC1C0). Resolves equip[i].itemId ->
// ITEM_INFO record. r[1] = weapon (== equip[1].itemId == g_LocalPlayerWeaponId). The
// costume/form override (branch g_SpecialFormActive 0x16760D4>0) is [runtime absent] ->
// not applied (see TODO below).
struct Snapshot {
    const ItemInfo* r[13];
    uint32_t        sock[13];   // equip[i].socket (word dword_16731E0 + 16*i)
    uint32_t        id[13];     // equip[i].itemId
};
Snapshot buildSnapshot(const SelfState& s, const GameDatabases& db) {
    Snapshot sn{};
    for (int i = 0; i < 13; ++i) {
        sn.id[i]   = s.equip[i].itemId;
        sn.sock[i] = s.equip[i].socket;
        sn.r[i]    = itemRec(db, sn.id[i]);
    }
    // TODO [runtime absent] 0x4CC310: if g_SpecialFormActive>0 && Npc_IsSpecialType(
    //   g_SelfMorphNpcId 0x1675A98)==1, slots 0..7 are replaced with costume ids (tables
    //   86673.. keyed by level+elementSecondary) — not modeled (morph state absent from
    //   SelfState). RE-VERIFIED (2026-07-14 audit): g_SpecialFormActive 0x16760D4 is NEVER
    //   written anywhere in ClientSource (negative grep) -> stays 0 -> this branch never
    //   triggers in the rewritten client, TODO unchanged (unlike g_SelfMorphNpcId 0x1675A98
    //   itself, read via g_Client.VarGet() in a dozen modules — but its gate
    //   g_SpecialFormActive remains absent).
    return sn;
}

// ENCHANT-DELTA LOOP — Item_GetEnchantStatDelta 0x553D50.
//
// The table IS implemented (Game/ItemSystem.cpp:323, prototype re-exposed by
// Game/StatBonusContributors.h:49 — already included here) and returns NON-ZERO values:
// the enchant level is byte3 of the socket word (Item_GetAttribByte3), read from the SAME
// array g_Slot0Socket 0x16731E0 (stride 16) as the gems, an array written by the network
// layer (Pkt_ItemActionDispatch @0x46A9E2 -> Net/ItemActionDispatch.cpp:232). The former
// "[hook]=0" neutralization and the "table not extracted" rationale from StatFormulas.h:32
// / StatEngine.h:24 were thus STALE.
//
// Two DISTINCT patterns coexist in the binary — do not conflate them. The xref count to
// 0x553D50 separates them formally: 17 calls across 9 functions = TWO calls for each of the
// 8 "pattern A" channels (one class-8 branch + one multi-slot branch) and ONE for
// Char_CalcAttackRatingMin (pattern B, no class-8 branch).

// PATTERN A — Char_CalcMaxHP 0x4D53EE (reference, key 50):
//   explicit i!=8 guard @0x4D5418; class 8 reserved for slot 1 @0x4D5437/0x4D543D,
//   add @0x4D5464 + floor 1 @0x4D546A; slots {0,2,3,4,5,7} x class {1,4}
//   @0x4D5479..0x4D54A7, add @0x4D54CA + floor 1 @0x4D54D0.
// The floor is INSIDE the class test: a piece of an ineligible class does not trigger it
// (0x4D5441 and 0x4D54A7 skip both the add AND the floor).
// Structure verified IDENTICAL (only the key changes) for:
//   Char_CalcMaxMP           0x4D5F6F (key 60, push 3Ch @0x4D5FC4)
//   Char_CalcExternalAttack  0x4D10BD (key 10, push 0Ah @0x4D1112)
//   Char_CalcInternalAttack  0x4D23D8 (key 20, push 14h @0x4D23D8)
//   Char_CalcExternalDefense 0x4D300E (key 30, push 1Eh @0x4D300E)
//   Char_CalcInternalDefense 0x4D3E03 (key 40, push 28h @0x4D3E03)
//   Char_CalcAccuracy        0x4D449E (key 70, push 46h @0x4D44F3)
//   Char_CalcEvasion         0x4D4A4D (key 80, push 50h @0x4D4AA2)
// The accumulator is the SAME one as the LEVEL_INFO base (var_1C at 0x4D4ED0).
inline void enchantLoopA(int& acc, const Snapshot& sn, int key) {
    for (int i = 0; i < 13; ++i) {                       // 0x4D5400 (i < 13)
        if (!sn.r[i]) continue;                          // 0x4D5410
        if (i == 8) continue;                            // 0x4D5418 (explicit guard)
        const int cls = classifyRecord(sn.r[i]);         // Item_ClassifyRecord 0x4D542F
        if (i == 1) {                                    // 0x4D5437
            if (cls == 8) {                              // 0x4D543D
                acc += Item_GetEnchantStatDelta(cls, i, sn.sock[i], key); // 0x4D545F
                if (acc < 1) acc = 1;                    // 0x4D546A
            }
        } else if (i == 0 || i == 2 || i == 3 || i == 4 || i == 5 || i == 7) { // 0x4D5479..0x4D549B
            if (cls == 1 || cls == 4) {                  // 0x4D549D/0x4D54A3
                acc += Item_GetEnchantStatDelta(cls, i, sn.sock[i], key); // 0x4D54C5
                if (acc < 1) acc = 1;                    // 0x4D54D0
            }
        }
    }
}

// PATTERN B lives next to its sole caller Char_CalcAttackRatingMin (0x4CDC63, key 100) in
// StatFormulas_Attack.cpp (enchantLoopRatingMin) — see that file for the four proven
// differences from pattern A.

// g_EquipAux 0x16750B8/dword_16750BC/dword_16750C0 — 13 slots, stride 0x0C (3 bit-packed
// dwords PER SLOT), block0/1/2 arguments of SkillTree_SumBonuses (see
// Game/StatBonusContributors.h). RE-DISCOVERED AVAILABLE (2026-07-14 formula audit):
// Net/ItemActionDispatch.cpp REALLY writes these 3 blocks via the g_Client.Var(kEquipAux0/1/2
// + slot*0x0C) escape hatch on every inventory->equipment move (mirroring g_InvAux, see its
// own header banner) — the state is therefore NO LONGER absent, even though EquipSlot still
// has NO dedicated field for it (it only carries extra0/extra1 = g_EquipDurability/
// g_EquipSerial, an UNRELATED pair). These 3 blocks are read directly off the g_Client
// singleton (as ItemActionDispatch.cpp already does), not off `s`/`sn`, to avoid duplicating
// a second source of truth in SelfState. Same addresses as Net/ItemActionDispatch.cpp
// (kEquipAux0/1/2), redeclared here with internal linkage (no export) for lack of a shared
// header for these constants.
constexpr uint32_t kEquipAux0 = 0x16750B8;
constexpr uint32_t kEquipAux1 = 0x16750BC;
constexpr uint32_t kEquipAux2 = 0x16750C0;

// Loop SkillTree_SumBonuses(category, g_EquipAux[3*i], dword_16750BC[3*i], dword_16750C0[3*i])
// over the 13 slots where sn.r[i] != nullptr — reproduced IDENTICALLY at the 7 confirmed call
// sites (re-decompiled): CalcMaxHP 0x4D57C9 cat=7, CalcMaxMP 0x4D6299 cat=8,
// CalcExternalAttack 0x4D151B cat=1, CalcInternalAttack 0x4D25F7 cat=2, CalcExternalDefense
// 0x4D330B cat=5, CalcInternalDefense 0x4D40FF cat=6, CalcAttackRatingMax 0x4CEAB1 cat=4.
// None of the 7 loops excludes slot 8 (unlike the main equip-sum loops) — confirmed by
// disassembly: the only guard is `sn.r[i] != nullptr`.
// CalcAttackRatingMin (0x4CE18F, cat=3) calls the same helper but MERGED into its own 0..13
// loop (wired separately in CalcAttackRatingMin, see StatFormulas_Attack.cpp) — commutative
// integer addition, same result whether aggregated here or inline.
inline int skillTreeEquipBonus(int category, const Snapshot& sn, const GameDatabases& db) {
    int total = 0;
    for (int i = 0; i < 13; ++i) {
        if (!sn.r[i]) continue;
        const uint32_t block0 = static_cast<uint32_t>(g_Client.VarGet(kEquipAux0 + i * 0x0C));
        const uint32_t block1 = static_cast<uint32_t>(g_Client.VarGet(kEquipAux1 + i * 0x0C));
        const uint32_t block2 = static_cast<uint32_t>(g_Client.VarGet(kEquipAux2 + i * 0x0C));
        total += SkillTree_SumBonuses(category, block0, block1, block2, db);
    }
    return total;
}

} // namespace
} // namespace ts2::game

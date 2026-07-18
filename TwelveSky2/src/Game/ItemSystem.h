// Game/ItemSystem.h — TS2 client item system: reads an ITEM_INFO record (436 o),
// decodes the socket/gem word bitfields, and computes bonuses (level scaling,
// sockets, gems, grade, enchant, element).
//
// CLEAN C++ rewrite, but NUMERICALLY FAITHFUL to the disassembly of
// TwelveSky2.exe (tables/constants are copied byte/float exact).
// Ground truth: Docs/TS2_GAMEPLAY_LOGIC.md §2 (item stats) + IDB idaTs2.
//
// Function <-> original address mapping:
//   Item_GetAttribByte0..3   0x545610 / 0x545640 / 0x545670 / 0x5456A0
//   Item_GetScaledStat       0x545980
//   Item_SocketBonusInt      0x4CA620      Item_SocketBonusFloat   0x4CAC30
//   Item_GetElementalBonus   0x54F590
//   Item_DecodeGemBonus      0x54D390
//   Item_GetGradeValue       0x54D750      Item_GetGradeMultiplier 0x54D9A0
//   Item_GetEnchantStatDelta 0x553D50
//   Item_ScaleStatByTypeA/B/C/D  0x4C91B0 / 0x4C95C0 / 0x4CB0D0 / 0x4CB3F0
//   ItemLookup (MobDb_GetEntry)  0x4C3C00
#pragma once
#include <cstdint>
#include <cstring>
#include "Game/GameState.h"
#include "Game/GameDatabase.h"  // ItemInfo (Item_MeetsEquipRequirement) — no cycle:
                                // GameDatabase.h only includes GameState.h.

namespace ts2::game {

// Read-only view onto an ITEM_INFO record (stride 436 o).
// Offsets come from the stats engine (Char_Calc*). Cf. doc §2.4.
namespace ItemOff {
    constexpr uint32_t kItemId       = 0;    // id (ranges 201-218/2303-2305 = element)
    constexpr uint32_t kCategory     = 184;  // idx46: 5=equip/weapon, 6=class4
    constexpr uint32_t kTypeCode     = 188;  // idx47: 28=weapon, 29=elemental weapon, 30=mount…
    constexpr uint32_t kItemLevel    = 204;  // scaling thresholds 45/100/113/146
    // WARNING: kItemLevel (+204) is NOT the equip requirement — it is only used for
    // stat scaling. The proven requirement is kReqLevel + kReqRebirth (guard @0x64ED49).
    constexpr uint32_t kFaction      = 212;  // idx53: required faction; 1 = any [0x64ECF5]
    constexpr uint32_t kSubtype      = 216;  // idx54
    constexpr uint32_t kPricePrimary = 220;  // idx55: price in secondary currency (g_InvWeight 0x16732AC) [0x5e5497]
    constexpr uint32_t kPriceGold    = 228;  // idx57: price in gold (g_Currency 0x1673180) [0x5e54ce]
    constexpr uint32_t kReqLevel     = 232;  // idx58: required level (term 1/2) [0x64ED49]
    constexpr uint32_t kReqRebirth   = 236;  // idx59: required rebirth tier (term 2/2) [0x64ED49]
    constexpr uint32_t kSkillFlag    = 284;  // idx71: 1=normal, 2=skill, 3=upgrade
    constexpr uint32_t kAttrPrimaryA = 292;  // External force (Field292)
    constexpr uint32_t kAttrPrimaryB = 296;  // Internal force (Field296)
    constexpr uint32_t kAttrRatingMin= 300;  // base min rating / internal def ×0.9 (Field300)
    constexpr uint32_t kAttrRatingMax= 304;  // base max rating (Field304)
    constexpr uint32_t kCritRate     = 308;
    constexpr uint32_t kExtAttack    = 312;
    constexpr uint32_t kIntAttack    = 316;
    constexpr uint32_t kExtDefense   = 320;
    constexpr uint32_t kIntDefense   = 324;
    constexpr uint32_t kMaxHp        = 328;
    constexpr uint32_t kMaxMp        = 332;
    constexpr uint32_t kAccuracy     = 336;
    constexpr uint32_t kRegen        = 360;
    constexpr uint32_t kEvasion      = 364;
    constexpr uint32_t kResistAll    = 368;
    constexpr uint32_t kResistPair0  = 372;  // 8 pairs (key,value): +372/+376 … +428/+432
}

struct ItemInfoView {
    const uint8_t* rec = nullptr;

    ItemInfoView() = default;
    explicit ItemInfoView(const uint8_t* r) : rec(r) {}
    bool valid() const { return rec != nullptr; }

    // Raw reads (memcpy = alignment-tolerant).
    int32_t i32(uint32_t off) const {
        int32_t v = 0; std::memcpy(&v, rec + off, 4); return v;
    }
    uint32_t u32(uint32_t off) const {
        uint32_t v = 0; std::memcpy(&v, rec + off, 4); return v;
    }

    // Named fields (read by the stats engines).
    uint32_t itemId()        const { return u32(ItemOff::kItemId); }
    uint32_t category()      const { return u32(ItemOff::kCategory); }
    uint32_t typeCode()      const { return u32(ItemOff::kTypeCode); }
    uint32_t itemLevel()     const { return u32(ItemOff::kItemLevel); }
    uint32_t faction()       const { return u32(ItemOff::kFaction); }
    uint32_t subtype()       const { return u32(ItemOff::kSubtype); }
    uint32_t pricePrimary()  const { return u32(ItemOff::kPricePrimary); }
    uint32_t priceGold()     const { return u32(ItemOff::kPriceGold); }
    uint32_t reqLevel()      const { return u32(ItemOff::kReqLevel); }
    uint32_t reqRebirth()    const { return u32(ItemOff::kReqRebirth); }
    uint32_t skillFlag()     const { return u32(ItemOff::kSkillFlag); }
    int32_t  attrPrimaryA()  const { return i32(ItemOff::kAttrPrimaryA); }
    int32_t  attrPrimaryB()  const { return i32(ItemOff::kAttrPrimaryB); }
    int32_t  attrRatingMin() const { return i32(ItemOff::kAttrRatingMin); }
    int32_t  attrRatingMax() const { return i32(ItemOff::kAttrRatingMax); }
    int32_t  critRate()      const { return i32(ItemOff::kCritRate); }
    int32_t  extAttack()     const { return i32(ItemOff::kExtAttack); }
    int32_t  intAttack()     const { return i32(ItemOff::kIntAttack); }
    int32_t  extDefense()    const { return i32(ItemOff::kExtDefense); }
    int32_t  intDefense()    const { return i32(ItemOff::kIntDefense); }
    int32_t  maxHp()         const { return i32(ItemOff::kMaxHp); }
    int32_t  maxMp()         const { return i32(ItemOff::kMaxMp); }
    int32_t  accuracy()      const { return i32(ItemOff::kAccuracy); }
    int32_t  regen()         const { return i32(ItemOff::kRegen); }
    int32_t  evasion()       const { return i32(ItemOff::kEvasion); }
    int32_t  resistAll()     const { return i32(ItemOff::kResistAll); }
    // Resistance pair p ∈ [0..7]: key + value.
    int32_t  resistKey(int p) const { return i32(ItemOff::kResistPair0 + 8 * p); }
    int32_t  resistVal(int p) const { return i32(ItemOff::kResistPair0 + 8 * p + 4); }
};

// Resolves an item id (1-based) into an ITEM_INFO record. Reproduces
// MobDb_GetEntry 0x4C3C00: bounds 1..count and rejects empty slots (id==0).
ItemInfoView ItemLookup(const DataTable& itemTbl, uint32_t itemId);

// Socket/gem word decoding, byte by byte (0x545610/40/70/A0).
// A 32-bit socket word: byte0=int socket category / refinement, byte1=element
// or gem grade, byte2=float socket category / gem count, byte3=enchant level.
// The original functions memcpy 4 bytes from the pushed argument,
// hence the direct extraction of byte N from the 32-bit word.
inline uint8_t Item_GetAttribByte0(uint32_t w) { return static_cast<uint8_t>(w & 0xFFu); }
inline uint8_t Item_GetAttribByte1(uint32_t w) { return static_cast<uint8_t>((w >> 8) & 0xFFu); }
inline uint8_t Item_GetAttribByte2(uint32_t w) { return static_cast<uint8_t>((w >> 16) & 0xFFu); }
inline uint8_t Item_GetAttribByte3(uint32_t w) { return static_cast<uint8_t>((w >> 24) & 0xFFu); }

// Level/type-scaled item bonus (Item_GetScaledStat 0x545980).
// statIdx ∈ 1..6 selects the stat; the result is truncated (ftol).
// Returns 0 if the type/level does not match any curve.
int Item_GetScaledStat(const ItemInfoView& item, int statIdx);

// Weapon socket bonus (type_code == 28).
//   weaponId    : weapon id (resolved in itemTbl)
//   socketWord  : weapon socket word (byte0 for int, byte2 for float)
//   category    : expected category (byte/10) — 1..6 (int) / 1..5 (float)
// Item_SocketBonusInt 0x4CA620 / Item_SocketBonusFloat 0x4CAC30.
int    Item_SocketBonusInt  (const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int category);
double Item_SocketBonusFloat(const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int category);

// Elemental weapon bonus (type_code == 29), byte1 of the socket
// word (Item_GetElementalBonus 0x54F590). key ∈ 1..8.
double Item_GetElementalBonus(const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int key);

// Gem bonus decoding from a decimal word (Item_DecodeGemBonus 0x54D390).
//   group 1: millions digits (a3/1000000)  -> attribute gem
//   group 2: thousands digits (a3%1e6/1000)  -> special gem (key 30/130)
//   group 3: unit digits   (a3%1000)      -> flat gem (×30/20/100/200…)
// key = expected prefix; returns 0 if not matching.
double Item_DecodeGemBonus(int group, int key, int gemWord);

// Grade (Item_GetGradeValue 0x54D750 / Item_GetGradeMultiplier 0x54D9A0).
//   GradeValue      : item id -> flat value 10/15/20/30 (0 otherwise)
//   GradeMultiplier : grade 1..10 -> multiplier 1.05..1.4 (1.0 otherwise)
int    Item_GetGradeValue(int itemId);
double Item_GetGradeMultiplier(int grade);

// Enchant stat delta (Item_GetEnchantStatDelta 0x553D50).
// Large table (class, slot index, enchant level 1..59, key) -> signed delta
// (hundredths for the main stats, units for accuracy/evasion).
//   itemClass  : item class — Item_ClassifyRecord (1, 4 -> weapons/armor;
//                8 -> special case, slot 1 only)
//   slot       : equipment slot index 0..12 (the engines loop i!=8;
//                slots {0,2,3,4,5}=armor, 7=weapon, 1=class-8 case)
//   socketWord : slot socket word (byte3 = enchant level 1..59)
//   key        : stat key (10=atkExt, 20=atkInt, 30=defExt, 40=defInt,
//                50=HP, 60=MP, 70=accuracy, 80=evasion, 90/100=rating…)
// Typical call: Item_GetEnchantStatDelta(class, i, g_Slot#Socket[i], key).
int Item_GetEnchantStatDelta(int itemClass, int slot, uint32_t socketWord, int key);

// Scales a stat toward a cap by weapon type (Item_ScaleStatByType*).
//   caps    : 4 weapon caps (mPAT @0x8E717C — dword_8E717C[4])
//   itemId  : weapon/special item id (selects caps[0..3])
//   value   : value to scale (compared to the cap)
//   flag    : upgrade counter (>0 required for A/B), otherwise returns 0
// A 0x4C91B0 (1000/2000) · B 0x4C95C0 (2000/4000) · C 0x4CB0D0 (2000/4000)
// · D 0x4CB3F0 (1800/3600). Returns 0 if the id is not eligible.
double Item_ScaleStatByTypeA(const int32_t caps[4], int itemId, int value, int flag);
double Item_ScaleStatByTypeB(const int32_t caps[4], int itemId, int value, int flag);
double Item_ScaleStatByTypeC(const int32_t caps[4], int itemId, int value);
double Item_ScaleStatByTypeD(const int32_t caps[4], int itemId, int value);

// Equipment/usage eligibility gate — Item_MeetsEquipRequirement 0x64ECD0.
// Returns false if the item CANNOT be equipped/used by the local player.
//
// 15 guards evaluated IN THE ORDER of the binary (faction, slot, summed level, then 12 gates
// by id/skillFlag/class backed by the rebirth tier dword_16747BC). Anchor details
// in ItemSystem.cpp.
//
// `equipSlot` is a SIGNED int, and -1 is the "no slot" sentinel: the Hex-Rays rendering
// `a3 <= 0xC` (a3 typed unsigned) is MISLEADING — the disasm does `cmp [ebp+arg_4], 0 / jl`
// @0x64ecfe-0x64ed02 then `cmp .., 0Ch / jg` @0x64ed04-0x64ed08, i.e. two SIGNED
// comparisons: the slot guard only applies for equipSlot ∈ [0..12]. 8 of the binary's 9
// callers pass -1 (icon hover: pure eligibility test, no target slot).
//
// State read (binary globals -> C++ equivalents):
//   g_LocalElementSecondary 0x1673198 -> g_World.self.elementSecondary
//   g_SelfLevel             0x16731A8 -> g_World.self.level
//   g_SelfLevelBonus        0x16731AC -> g_World.self.levelBonus
//   dword_16747BC (rebirth tier) -> g_Client.VarGet(0x16747BC)
bool Item_MeetsEquipRequirement(const ItemInfo& it, int equipSlot);

} // namespace ts2::game

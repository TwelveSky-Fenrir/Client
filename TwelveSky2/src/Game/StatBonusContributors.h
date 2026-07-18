// Game/StatBonusContributors.h — stat contributors currently NEUTRALIZED in
// StatFormulas.cpp (enchant bonus, gem/refine bonus, and skill tree bonus).
//
// Clean C++ rewrite, but NUMERICALLY FAITHFUL to the TwelveSky2.exe disassembly
// (tables/constants copied byte-for-byte). Source of truth: idaTs2 IDB (direct
// decompilation of the addresses below), cross-checked with Docs/TS2_GAMEPLAY_LOGIC.md.
//
// Function <-> original address mapping:
//   Item_GetEnchantStatDelta 0x553D50  — PROTOTYPE ONLY here (see note below).
//   Item_SumGemStatBonus     0x4C3CC0
//   Item_GemStatBonusLookup  0x4C3D90  (internal helper, not exposed in the header)
//   Char_SumGemStatA         0x54CB00
//   Char_SumGemStatB         0x54CB80
//   Char_SumGemStatC         0x54CC40
//   Char_SumGemStatD         0x54CC90
//   SkillTree_SumBonuses     0x54B700
//   SkillTree_GetNodeValue   0x54B830  (internal helper, not exposed in the header)
//   AnchorTbl_FindByKey      0x4C7630  (internal helper, operates on GameDatabases::socketT)
//
// IMPORTANT NOTE — Item_GetEnchantStatDelta is ALREADY implemented in ItemSystem.cpp/.h
// (verified byte-exact by re-decompiling 0x553D50: the case table (class, slot, key,
// enchant level) already written in ItemSystem.cpp EXACTLY matches the current
// disassembly). It is NOT a stub — contrary to what the "NEUTRALIZED TERMS" comment in
// StatFormulas.h might suggest: that comment doesn't mean the function is missing, only
// that StatFormulas.cpp doesn't CALL IT YET (the call site is neutralized in
// StatFormulas.cpp, not the function itself).
// This file therefore only RE-DECLARES its prototype (no redefinition — that would cause
// a duplicate definition at link time with ItemSystem.cpp) so this module stays
// self-describing. The prototype is copied identically from ItemSystem.h.
//
// ⚠️ HISTORICAL NOTE: earlier wiring notes mentioned an `ItemInfo` type conflict between
// GameDatabase.h and ItemSystem.h. That's no longer the case in the current tree:
// GameDatabase.h exports `ts2::game::ItemInfo` (byte-exact 436-byte POD) and ItemSystem.h
// only exports a distinct view `ItemInfoView`. No ODR conflict is therefore present for
// the current wiring of StatFormulas.cpp.
#pragma once
#include <cstdint>
#include "Game/GameState.h"
#include "Game/ItemSystem.h"   // Item_GetAttribByte0..3, DataTable — see conflict note above.

namespace ts2::game {

// Item_GetEnchantStatDelta 0x553D50 — PROTOTYPE ONLY (definition in ItemSystem.cpp).
// Identical signature to the one already declared in ItemSystem.h: do not redefine here.
int Item_GetEnchantStatDelta(int itemClass, int slot, uint32_t socketWord, int key);

// Item_SumGemStatBonus 0x4C3CC0 — gem bonus of an item WITH 4 gem sockets
// (accessories: rings/necklaces/earrings — call loop observed on slots 9..12).
//   key        : stat key (observed = 1 at the only decompiled call site; same keys
//                as Item_GemStatBonusLookup: 1,2,4,6,8 — 3/5/7 always 0).
//   socketWord : item's 32-bit socket word; its 4 bytes (Item_GetAttribByte0..3) are
//                EACH an independent "gem level" 0..100 (up to 4 gems/item).
//                Bytes 0..2 use group 1 (tiers 41-60/61-80/1-20/21-40/81-100),
//                byte 3 uses group 2 (tiers of 5 + special range for key 1).
// Returns the sum of the 4 lookups (0 if socketWord == 0).
int Item_SumGemStatBonus(int key, uint32_t socketWord);

// Char_SumGemStatA/B/C/D 0x54CB00/0x54CB80/0x54CC40/0x54CC90 — flat refine/gem bonus
// (5 points per tier, +5 bonus if tier == 25) read from byte2 (Item_GetAttribByte2
// = "socket float category / gem count") of the socket word of EACH relevant equipment
// slot. In the original these are PARAMETERLESS functions that read directly from the
// g_SlotNSocket globals (0x16731E0 + 16*N, N = slot index); here `s.equip[N].socket`
// (SelfState) is used instead to stay testable, like the rest of the stat engine
// (StatFormulas.h).
//   A (slots 7 [weapon] + 2)              — feeds Char_SumAttrField304 (attrRatingMax/+304)
//   B (slots 3 + 5 + 1)                   — feeds Char_SumAttrField296 (attrPrimaryB/+296)
//   C (slot 4)                             — feeds Char_SumAttrField300 (attrRatingMin/+300)
//   D (slot 0)                             — feeds Char_SumAttrField292 (attrPrimaryA/+292)
// (mapping inferred from the actual callers: Char_SumAttrField29X/30X → Char_SumGemStat{A..D}).
int Char_SumGemStatA(const SelfState& s);
int Char_SumGemStatB(const SelfState& s);
int Char_SumGemStatC(const SelfState& s);
int Char_SumGemStatD(const SelfState& s);

// SkillTree_SumBonuses 0x54B700 — sums up to 5 skill-tree node bonuses.
//   category      : category passed through as-is to SkillTree_GetNodeValue (original a1).
//                   8 confirmed call sites from re-decompilation ("formula audit" mission,
//                   2026-07-14), all in a 0..13 loop over occupied equip slots, with
//                   g_EquipAux/dword_16750BC/dword_16750C0[3*i] as blocks (STATFORMULAS.CPP
//                   wires them via g_Client.Var(), cf. StatFormulas.cpp::skillTreeEquipBonus()):
//                     CalcMaxHP 0x4D57C9 cat=7, CalcMaxMP 0x4D6299 cat=8,
//                     CalcExternalAttack 0x4D151B cat=1, CalcInternalAttack 0x4D25F7 cat=2,
//                     CalcExternalDefense 0x4D330B cat=5, CalcInternalDefense 0x4D40FF cat=6,
//                     CalcAttackRatingMin 0x4CE18F cat=3 (merged into an existing loop),
//                     CalcAttackRatingMax 0x4CEAB1 cat=4.
//   block0/1/2    : 3 bit-packed dwords (original a2/a3/a4 parameters — the real stdcall
//                   ABI passes 3 full dwords despite the decompiler's misleading `char`
//                   typing; Hex-Rays over-refines the types since only certain bytes are
//                   re-read).
//                   Exact layout (reproduced from the original Crt_Memcpy, little-endian):
//                     block0 byte1 = number of active pairs n (1..5, else returns 0);
//                     block0 byte2/3   = pair #0 (id,value)
//                     block1 byte0/1   = pair #1 (id,value)   block1 byte2/3 = pair #2
//                     block2 byte0/1   = pair #3 (id,value)   block2 byte2/3 = pair #4
//                   (each byte is a SIGNED `char` in the original — sign-extended identically).
//   db            : GameDatabases (access to db.socketT — 20-byte SOCKET_INFO table, required
//                   by the internal helper SkillTree_GetNodeValue/AnchorTbl_FindByKey).
// For each pair i < n whose id is nonzero, adds SkillTree_GetNodeValue(category, id, val).
int SkillTree_SumBonuses(int category, uint32_t block0, uint32_t block1, uint32_t block2,
                          const GameDatabases& db);

} // namespace ts2::game

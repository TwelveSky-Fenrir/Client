// Net/ItemActionDispatch.cpp — rewrite of Pkt_ItemActionDispatch (opcode 0x1a, EA 0x46A320).
//
// ===========================================================================
//  DISPATCHER MAP (source: idaTs2, EA 0x46A320)
// ===========================================================================
//
//  HEADER (0x46A320..0x46A40D) — the payload is 4 consecutive dwords, copied
//  from the receive buffer (unk_8156C1..) into locals:
//     payload[0] -> var_414 : action flag / server result code
//                             (0 = execute the action ; !=0 = just notify).
//     payload[1] -> var_42C : ROW    of the inventory cell.
//     payload[2] -> var_43C : COLUMN of the inventory cell.
//     payload[3] -> var_428 : destination index "D" (container/belt/rack).
//     g_GmCmdCooldownLatch (0x1675B08) is reset to 0.
//     Cell = g_InvMain[(row%100)*0x600 + (col%100)*0x18] -> itemId.
//     item = MobDb_GetEntry(mITEM, itemId) (ITEM_INFO record, 436 bytes); null -> return.
//
//  OUTER SWITCH (0x46A426) on item->typeCode (ITEM_INFO +0xBC = +188).
//  Routed via byte_487D28[typeCode-5] -> jpt_46A44F (5 blocks + default):
//   ---------------------------------------------------------------------------
//   typeCode | block               | EA        | role
//   ---------|---------------------|-----------|-------------------------------
//     5      | A SkillLearn        | 0x46A456  | skill book -> hotbar
//     6..22  | B EquipSwap         | 0x46A8A1  | equip (swap bag<->slot)
//     28,29  |   same              |           |
//     31,32  |   same              |           |
//     23,24  | C SpecialContainer  | 0x46AC7F  | store in special container
//     26     | D AutoPotionBelt    | 0x46AE24  | auto-potion belt
//     35,36  | E ThrowWeaponRack   | 0x46AF76  | throw-weapon rack / pet
//     25,27  | (cascade route 5)   | 0x46B10F  | consumable: effect mega-switch
//     30,33,34| (cascade route 5)   |           | (def_46A44F, NOT the epilogue 0x46B168)
//    outside 5..36 (direct cascade)| 0x46B10F  |
//   ---------------------------------------------------------------------------
//
//  TWO DISTINCT jump targets (fix H3, proven by IDA 0x46A426):
//    - def_46B168 (label 0x487CFE, code 0x46B168) = RETURN epilogue / no-op.
//      All blocks A-E (and their early flag!=0 exits) jump there: they
//      RETURN, they do NOT fall through into the cascade.
//    - def_46A44F (0x46B10F) = THE CASCADE: a HUGE sub-switch (>1000 cases, on
//      var_480 = item->itemId) that applies the consumable effect (potions, buffs,
//      teleports, transformations, etc.). It is reached ONLY by route 5 of the
//      outer switch (typeCodes 25,27,30,33,34) AND by typeCodes outside [5,36]
//      (46A435 cmp 0x1F ; 46A43C ja def_46A44F). Out of scope here -> see
//      Net/ItemEffectDispatch.cpp.
//
//  This module FAITHFULLY implements: the header, the outer routing, block A
//  (skill learning, complete), block B (equipment swap, with the FSM
//  state self.mode set at the tail = g_SelfActionState 0x1687328), and the
//  storage + cell clearing of blocks C/D/E. The deep tails (stat recalc,
//  localized message formatting, effect mega-switch) are left
//  as // TODO(@EA).
//
//  Mutated state: g_Client.inv (grid), g_World.self (equip/skill points), and the
//  long tail of SoA globals via g_Client.Var(originalAddress).
// ===========================================================================

#include "Net/ItemActionDispatch.h"
#include "Net/ItemEffectDispatch.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"

#include <cstdint>
#include <cstring>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Original addresses of SoA globals not modeled as a dedicated field. They are
// reproduced bit-for-bit via g_Client.Var(addr) (escape hatch provided by the base).
// ---------------------------------------------------------------------------
namespace {

// Learned skills bar: 40 slots, stride 8 = {+0 skillID, +4 spCost}.
constexpr uint32_t kLearnedSkills_Id   = 0x16742BC; // g_LearnedSkills
constexpr uint32_t kLearnedSkills_Cost = 0x16742C0; // dword_16742C0

// Main equipment: 13 slots, stride 0x10 = {+0 main, +4 durab, +8 socket, +C serial}.
constexpr uint32_t kEquipMain       = 0x16731D8;
constexpr uint32_t kEquipDurability = 0x16731DC;
constexpr uint32_t kEquipSocket     = 0x16731E0; // g_Slot0Socket
constexpr uint32_t kEquipSerial     = 0x16731E4;
// Auxiliary equipment: 13 slots, stride 0x0C = {+0, +4, +8}.
constexpr uint32_t kEquipAux0 = 0x16750B8;
constexpr uint32_t kEquipAux1 = 0x16750BC;
constexpr uint32_t kEquipAux2 = 0x16750C0;
// Equipment render mirror: 13 slots, stride 8 = {+0 main, +4 socket}.
constexpr uint32_t kEquipVisible_Main   = 0x16872A8;
constexpr uint32_t kEquipVisible_Socket = 0x16872AC;

// Auxiliary inventory (SoA parallel to the grid): row stride 0x300, column 0x0C.
constexpr uint32_t kInvAux0 = 0x1674AB8;
constexpr uint32_t kInvAux1 = 0x1674ABC;
constexpr uint32_t kInvAux2 = 0x1674AC0;

// Specialized containers.
constexpr uint32_t kSpecialContainer = 0x1675808; // cat 23/24 : [(type-23)*0x38 + D*4] = itemID
constexpr uint32_t kAutoPotionBelt   = 0x16757B0; // cat 26    : [D*4] = itemID
constexpr uint32_t kAutoPotionTimer  = 0x16757D8; // cat 26    : [D*4] = 30
constexpr uint32_t kThrowWeaponRack  = 0x16749FC; // cat 35/36 : [D*4] = itemID

constexpr uint32_t kGmCmdCooldownLatch = 0x1675B08;

// Localized string ids from 005.DAT (StrTable005_Get).
constexpr int kMsg_SkillLearned = 0x1A9; // "skill learned"
constexpr int kMsg_PetNotUsable = 0x74D; // pet already active / not usable

// Reads a dword at an offset into a raw record (SKILL_INFO record, 776 bytes, etc.).
inline uint32_t RecU32(const uint8_t* rec, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, rec + off, sizeof(v));
    return v;
}

// 1-based SKILL_INFO record (mSKILL / SkillGrowthTbl_GetRecord 0x4C4E90).
const uint8_t* GetSkillRecord(uint32_t skillId) {
    if (skillId == 0 || skillId > g_World.db.skill.count) return nullptr;
    return g_World.db.skill.record(skillId - 1);
}

// ---------------------------------------------------------------------------
// Block A — skill learning (typeCode 5). EA 0x46A456..0x46A89C.
// The object is a "book": it reads item->field348 (skillId), resolves the
// SKILL_INFO record, and depending on skillRec[+0x21C] (1..4) looks for a free
// slot in a range of the g_LearnedSkills bar, then "commits" (loc_46A727): spends
// points, writes the slot, clears the cell, shows a message, notes the quickslot.
// ---------------------------------------------------------------------------

// Finds the 1st free slot in [start,end) (free = skillID < 1). Returns `end`
// if none found (exact behavior of loops 0x46A4D6.., 0x46A531.., etc.).
int ScanFreeSkillSlot(int start, int end) {
    int i = start;
    while (i < end) {
        if (static_cast<int32_t>(g_Client.Var(kLearnedSkills_Id + i * 8)) < 1) break; // free
        ++i;
    }
    return i;
}

// Common commit (loc_46A727): writes the skill into slot `slot`,
// spends skillRec[+0x230] points, clears the cell (row,col), shows the
// 0x1A9 message, and (re)inits the quickslot window based on the slot's range.
void CommitSkillLearn(const uint8_t* skillRec, int slot, uint32_t row, uint32_t col) {
    // g_SkillPointPool -= skillRec[+0x230]   (0x46A72D)
    const uint32_t cost = RecU32(skillRec, 0x230);
    g_World.self.skillPoints -= static_cast<int>(cost);
    // g_LearnedSkills[slot] = {skillID, cost}   (0x46A74D / 0x46A766)
    g_Client.Var(kLearnedSkills_Id   + slot * 8) = static_cast<int32_t>(RecU32(skillRec, 0x0));
    g_Client.Var(kLearnedSkills_Cost + slot * 8) = static_cast<int32_t>(cost);
    // Full clear of the source cell   (0x46A76D..0x46A82D)
    g_Client.inv.ClearCell(row % 100, col % 100);
    // Msg_AppendSystemLine(StrTable005_Get(0x1A9), g_SysMsgColor)   (0x46A82D)
    g_Client.msg.System(Str(kMsg_SkillLearned));
    // cQuickSlotWin_Init(dword_18392C0, page) based on the slot's range   (0x46A84E..)
    //   slot<0x0A -> page 0 ; <0x14 -> page 1 ; <0x1E -> page 2.
    // TODO(@0x46A84E): quickslot window reinitialization (UI out of scope for the base).
}

// Returns true if the object was handled (learning attempted), false to fall
// through to the default doing nothing (failure/full).
void HandleSkillLearn(const ItemInfo* item, uint32_t row, uint32_t col) {
    // (0x46A456) var_414 must be 0 to act; else -> default.
    // (guard already filtered by the caller via `flag`)
    const uint32_t skillId = item->field348;               // itemRec[+0x15C]  (0x46A46A)
    const uint8_t* skillRec = GetSkillRecord(skillId);      // mSKILL[skillId]
    if (!skillRec) return;                                  // (0x46A488) null -> default

    const uint32_t skillType = RecU32(skillRec, 0x21C);     // switch 1..4  (0x46A495)
    int slot;
    switch (skillType) {
    case 1: // (0x46A4CA) range 0..9
        slot = ScanFreeSkillSlot(0, 0x0A);
        if (slot == 0x0A) return;                           // full -> default
        // [audio] Snd3D_PlayScaledVolume(flt_1494F7C)  (0x46A510)
        CommitSkillLearn(skillRec, slot, row, col);
        return;

    case 2: // (0x46A525) range 20..29
        slot = ScanFreeSkillSlot(0x14, 0x1E);
        if (slot == 0x1E) return;                           // full -> default
        // Sound sub-switch on skillRec[+0x220] (2..5), no state effect.
        //   (0x46A56B) case2->flt_149503C, case3->flt_14950FC,
        //              case4->flt_14951BC, case5->flt_149527C  [audio]
        CommitSkillLearn(skillRec, slot, row, col);
        return;

    case 3: // (0x46A5ED) range 10..19, else 30..39
    case 4: // (0x46A689) identical to case 3
        slot = ScanFreeSkillSlot(0x0A, 0x14);
        if (slot == 0x14) {                                 // 10..19 full
            slot = ScanFreeSkillSlot(0x1E, 0x28);           // try 30..39
            if (slot == 0x28) return;                       // full -> default
        }
        // [audio] Snd3D_PlayScaledVolume(flt_1494F7C)  (0x46A674 / 0x46A710)
        CommitSkillLearn(skillRec, slot, row, col);
        return;

    default: // (def_46A4C3, 0x46A722) -> default
        return;
    }
}

// ---------------------------------------------------------------------------
// Block B — equipment swap (typeCode 6..22, 28,29,31,32). EA 0x46A8A1.
// slot = item->subtype - 2 (itemRec[+0xD8]-2), must be in [0,12]. SWAPS
// the inventory cell (row,col) with the equipment slot: the object from the
// bag goes into the slot, the old equipment goes back down into the cell.
// ---------------------------------------------------------------------------
void HandleEquipSwap(const ItemInfo* item, uint32_t row, uint32_t col) {
    // (0x46A8A1) var_414 must be 0 (already filtered by the caller).
    const int slot = static_cast<int>(item->subtype) - 2;  // itemRec[+0xD8]-2  (0x46A8B5)
    if (slot < 0 || slot > 0x0C) return;                   // (0x46A8C4/CD) -> default

    InvCell& cell = g_Client.inv.At(row % 100, col % 100);
    EquipSlot& eq = g_World.self.equip[slot];

    // 1) Save the old equipment (var_410..var_3F8).   (0x46A8DD..0x46A961)
    const uint32_t oldMain    = eq.itemId;                       // g_EquipMain
    const uint32_t oldDurab   = eq.extra0;                       // g_EquipDurability
    const uint32_t oldSocket  = eq.socket;                       // g_Slot0Socket
    const uint32_t oldSerial  = eq.extra1;                       // g_EquipSerial
    const int32_t  oldAux0    = g_Client.Var(kEquipAux0 + slot * 0x0C);
    const int32_t  oldAux1    = g_Client.Var(kEquipAux1 + slot * 0x0C);
    const int32_t  oldAux2    = g_Client.Var(kEquipAux2 + slot * 0x0C);

    // 2) Inventory cell -> equipment slot.   (0x46A967..0x46AA8E)
    //   SoA mapping: InvMain->EquipMain, InvGrid_Count->EquipDurability,
    //   InvGrid_Durability->Slot0Socket, InvGrid_InstanceSerial->EquipSerial,
    //   InvAux[0..2]->EquipAux[0..2].
    eq.itemId = cell.itemId;        // InvMain            -> g_EquipMain
    eq.extra0 = cell.flag;          // InvGrid_Count      -> g_EquipDurability
    eq.socket = cell.color;         // InvGrid_Durability -> g_Slot0Socket
    eq.extra1 = cell.durability;    // InvGrid_InstanceSerial -> g_EquipSerial
    g_Client.Var(kEquipAux0 + slot * 0x0C) = static_cast<int32_t>(g_Client.Var(kInvAux0 + (row % 100) * 0x300 + (col % 100) * 0x0C));
    g_Client.Var(kEquipAux1 + slot * 0x0C) = static_cast<int32_t>(g_Client.Var(kInvAux1 + (row % 100) * 0x300 + (col % 100) * 0x0C));
    g_Client.Var(kEquipAux2 + slot * 0x0C) = static_cast<int32_t>(g_Client.Var(kInvAux2 + (row % 100) * 0x300 + (col % 100) * 0x0C));

    // Render mirror.   (0x46AA94..0x46AAC5)
    g_Client.Var(kEquipVisible_Main   + slot * 8) = static_cast<int32_t>(eq.itemId);
    g_Client.Var(kEquipVisible_Socket + slot * 8) = static_cast<int32_t>(eq.socket);

    // 3) Old equipment -> inventory cell.   (0x46AACC..0x46AB16 and the
    //    inferred symmetric continuation 0x46AB10..). GridX/GridY kept
    //    (equipment has no position in the grid).
    cell.itemId     = oldMain;      // var_410 -> g_InvMain
    cell.flag       = oldDurab;     // var_40C -> g_InvGrid_Count
    cell.color      = oldSocket;    // var_408 -> g_InvGrid_Durability
    cell.durability = oldSerial;    // var_404 -> g_InvGrid_InstanceSerial
    g_Client.Var(kInvAux0 + (row % 100) * 0x300 + (col % 100) * 0x0C) = oldAux0;
    g_Client.Var(kInvAux1 + (row % 100) * 0x300 + (col % 100) * 0x0C) = oldAux1;
    g_Client.Var(kInvAux2 + (row % 100) * 0x300 + (col % 100) * 0x0C) = oldAux2;

    // Block B tail (successful equip-swap): self's FSM state. (0x46abcb)
    //   mov ds:g_SelfActionState(0x1687328), 1  -- value 1, unconditional on the
    //   successful-swap path. self.mode ≡ g_SelfActionState (read by CombatResultApply
    //   when mode in {1,5,6,7}). Without this writer, SelfModeDeclenche() always reads 0.
    g_World.self.mode = 1;                                      // (0x46abcb) g_SelfActionState = 1

    // TODO(@0x46AB16..0x46B168): block B tail — derived stat recalc
    //   (Char_CalcAttackRatingMin/Max 0x4CD970/0x4CE3F0, g_EquipSnapshotScratch snapshot),
    //   sound effect and UI refresh, before falling through to the default.
}

// ---------------------------------------------------------------------------
// Block C — special container (typeCode 23,24). EA 0x46AC7F.
// If flag!=0: formats a localized message (0xBB8) with the object's name -> TODO.
// Else: stores the itemID in g_SpecialContainer[(type-23)*0x38 + D*4], clears the
// cell, plays a sound, prepares a message.
// ---------------------------------------------------------------------------
void HandleSpecialContainer(const ItemInfo* item, uint32_t flag, uint32_t row,
                            uint32_t col, uint32_t dstD) {
    if (flag != 0) {
        // (0x46AC88) memset(buf,0,0x3E8) ; StrTable005_Get(0xBB8) ; vsnprintf(buf, fmt, name)
        // TODO(@0x46AC88): formatting/display of localized message 0xBB8 (item->name).
        return;
    }
    // (0x46ACCC) idx = typeCode - 0x17 ; container[idx*0x38 + D*4] = itemID.
    const uint32_t idx = item->typeCode - 0x17;             // 0 or 1
    g_Client.Var(kSpecialContainer + idx * 0x38 + dstD * 4) = static_cast<int32_t>(item->itemId);
    // Clear the cell.   (0x46ACFF..0x46ADBF)
    g_Client.inv.ClearCell(row % 100, col % 100);
    // [audio] Snd3D_PlayScaledVolume(unk_1495ABC)   (0x46ADBF)
    // TODO(@0x46ADCF): second localized message (memset + StrTable005 + vsnprintf).
}

// ---------------------------------------------------------------------------
// Block D — auto-potion belt (typeCode 26). EA 0x46AE24.
// If flag!=0 -> default. Else: belt[D]=itemID, timer[D]=30, clears the cell.
// ---------------------------------------------------------------------------
void HandleAutoPotionBelt(const ItemInfo* item, uint32_t row, uint32_t col, uint32_t dstD) {
    // (0x46AE24) flag already filtered by the caller.
    g_Client.Var(kAutoPotionBelt + dstD * 4) = static_cast<int32_t>(item->itemId); // (0x46AE40)
    g_Client.Var(kAutoPotionTimer + dstD * 4) = 0x1E;                              // 30 (0x46AE4D)
    g_Client.inv.ClearCell(row % 100, col % 100);                                  // (0x46AE58..)
    // [audio] Snd3D_PlayScaledVolume(unk_1495ABC)   (0x46AF18)
    // TODO(@0x46AF28): localized message 0xBB7 (memset + StrTable005 + vsnprintf).
}

// ---------------------------------------------------------------------------
// Block E — throw-weapon rack / pet (typeCode 35,36). EA 0x46AF76.
// If flag!=0: system message 0x74D -> default. Else: rack[D]=itemID, clears the
// cell, recalculates the attack rating.
// ---------------------------------------------------------------------------
void HandleThrowWeaponRack(const ItemInfo* item, uint32_t flag, uint32_t row,
                           uint32_t col, uint32_t dstD) {
    if (flag != 0) {
        // (0x46AF7F) Msg_AppendSystemLine(StrTable005_Get(0x74D), g_SysMsgColor)
        g_Client.msg.System(Str(kMsg_PetNotUsable));
        return;
    }
    // (0x46AFA4) rack[D] = itemID ; clears the cell.
    g_Client.Var(kThrowWeaponRack + dstD * 4) = static_cast<int32_t>(item->itemId);
    g_Client.inv.ClearCell(row % 100, col % 100);
    // TODO(@0x46B079..0x46B168): min/max attack rating recalc
    //   (Char_CalcAttackRatingMin 0x4CD970, Char_CalcAttackRatingMax 0x4CE3F0 ;
    //    writes to dword_168736C/1687370/1687374) before falling through to the default.
}

// ---------------------------------------------------------------------------
// Outer routing table byte_487D28[typeCode-5] (32 bytes, exact value from the
// binary) -> block index: 0=A, 1=B, 2=C, 3=D, 4=E, 5=default.
// ---------------------------------------------------------------------------
const uint8_t kTypeRoute[32] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // typeCode 5..20
    1, 1, 2, 2, 5, 3, 5, 1, 1, 5, 1, 1, 5, 5, 4, 4, // typeCode 21..36
};

} // namespace

// ===========================================================================
//  Entry point.
// ===========================================================================
void ApplyItemActionDispatch(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 16) return;                       // the binary reads 16 fixed bytes

    // --- Header: 4 dwords LE (0x46A343..0x46A385). ---
    uint32_t flag, row, col, dstD;                          // var_414, var_42C, var_43C, var_428
    std::memcpy(&flag, payload + 0, 4);
    std::memcpy(&row,  payload + 4, 4);
    std::memcpy(&col,  payload + 8, 4);
    std::memcpy(&dstD, payload + 12, 4);

    // g_GmCmdCooldownLatch = 0   (0x46A3BD)
    g_Client.Var(kGmCmdCooldownLatch) = 0;

    // --- Inventory cell -> itemID -> ITEM_INFO record. (0x46A3C7..0x46A40D) ---
    const InvCell& cell = g_Client.inv.At(row % 100, col % 100);
    const ItemInfo* item = GetItemInfo(cell.itemId);        // MobDb_GetEntry(mITEM, itemId)
    if (!item) return;                                      // (0x46A40D) null -> return

    // --- Outer switch on item->typeCode (ITEM_INFO +0xBC). (0x46A426) ---
    // H3 (CRITICAL BUG fixed): the def_46A44F 0x46B10F cascade (effect mega-switch)
    // is reached ONLY by route 5 (typeCodes 25,27,30,33,34) and by
    // typeCodes outside [5,36]. Blocks A-E, on the other hand, RETURN (they jump to the
    // def_46B168 0x487CFE epilogue, DISTINCT from the cascade) — they must NOT fall
    // through into ApplyItemEffectDispatch.
    const uint32_t typeCode = item->typeCode;

    // Outside [5,36]: 46A435 cmp var_474,0x1F ; 46A43C ja def_46A44F -> direct cascade.
    if (typeCode < 5 || typeCode > 36) {
        ApplyItemEffectDispatch(item, flag, row, col, dstD);    // def_46A44F 0x46B10F
        return;
    }

    switch (kTypeRoute[typeCode - 5]) {                         // byte_487D28[typeCode-5] -> jpt_46A44F 0x487D10
    case 0: // A — skill learning. flag!=0 -> def_46B168 (epilogue) ; else handled -> def_46B168.
        if (flag == 0) HandleSkillLearn(item, row, col);        // 0x46A456 ; 0x46A45F jmp def_46B168
        return;                                                 // all block A exits -> def_46B168 (RETURN, not cascade)
    case 1: // B — equipment swap. flag!=0 -> def_46B168.
        if (flag == 0) HandleEquipSwap(item, row, col);         // 0x46A8A1
        return;
    case 2: // C — special container (handles flag internally). 0x46AC7F -> def_46B168.
        HandleSpecialContainer(item, flag, row, col, dstD);
        return;
    case 3: // D — auto-potion belt. flag!=0 -> def_46B168.
        if (flag == 0) HandleAutoPotionBelt(item, row, col, dstD); // 0x46AE24
        return;
    case 4: // E — throw-weapon rack / pet (handles flag internally). 0x46AF76 -> def_46B168.
        HandleThrowWeaponRack(item, flag, row, col, dstD);
        return;
    case 5: // default route of the outer switch = def_46A44F cascade (typeCodes 25,27,30,33,34).
    default:
        // =======================================================================
        //  CASCADE def_46A44F (0x46B10F) — consumable effect mega-switch.
        //  Switch key: var_480 = ITEM_INFO[+0] == item->itemId. Routed by
        //  ranges over item->itemId (sub-tables byte_487E90/488014/4882F8/4884B4/
        //  488584/4886F0), handlers 0x46B658.. . See Net/ItemEffectDispatch.cpp.
        ApplyItemEffectDispatch(item, flag, row, col, dstD);    // def_46A44F 0x46B10F
        return;
    }
}

} // namespace ts2::game

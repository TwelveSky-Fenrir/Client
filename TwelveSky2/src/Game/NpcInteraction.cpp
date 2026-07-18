// Game/NpcInteraction.cpp — see NpcInteraction.h for the EA -> function/method table and
// fidelity notes (NPC field provenance, Npc_/Monster_ ambiguity on def+96).
#include "Game/NpcInteraction.h"

namespace ts2::game {

// ===========================================================================
// Level_ToAggroValue 0x53F700 — full table, transcribed as-is.
// ===========================================================================
int Npc_LevelToAggroValue(int level) {
    if (level < 100) return level;
    switch (level) {
        case 100: return 102;  case 101: return 105;  case 102: return 108;
        case 103: return 111;  case 104: return 114;  case 105: return 117;
        case 106: return 120;  case 107: return 123;  case 108: return 126;
        case 109: return 129;  case 110: return 132;  case 111: return 135;
        case 112: return 138;  case 113: return 143;  case 114: return 149;
        case 115: return 155;  case 116: return 161;  case 117: return 167;
        case 118: return 173;  case 119: return 179;  case 120: return 185;
        case 121: return 191;  case 122: return 197;  case 123: return 203;
        case 124: return 209;  case 125: return 215;  case 126: return 221;
        case 127: return 227;  case 128: return 233;  case 129: return 239;
        case 130: return 245;  case 131: return 251;  case 132: return 257;
        case 133: return 263;  case 134: return 269;  case 135: return 275;
        case 136: return 281;  case 137: return 287;  case 138: return 293;
        case 139: return 299;  case 140: return 305;  case 141: return 311;
        case 142: return 317;  case 143: return 323;  case 144: return 329;
        case 145: return 335;  case 146: return 355;  case 147: return 375;
        case 148: return 395;  case 149: return 415;  case 150: return 455;
        case 151: return 495;  case 152: return 535;  case 153: return 575;
        case 154: return 635;  case 155: return 695;  case 156: return 755;
        case 157: return 815;
        default:  return 1;
    }
}

// ===========================================================================
// Npc_IsSpecialType 0x54EE60 — fixed list.
// ===========================================================================
bool Npc_IsSpecialType(int typeOrCode) {
    switch (typeOrCode) {
        case 19: case 20: case 21: case 34: case 49: case 120: case 154:
        case 175: case 176: case 177: case 190: case 191: case 192: case 193:
            return true;
        default:
            return false;
    }
}

// ===========================================================================
// Npc_IsQuestTarget 0x540340.
// ===========================================================================
bool Npc_IsQuestTarget(const void* def, const NpcQuestContext& ctx) {
    if (!def) return false;
    const int elt = ctx.localElement;

    switch (NpcDefReadI32(def, kNpcDefOffQuestCatA)) {
    case 1:
        switch (NpcDefReadI32(def, kNpcDefOffQuestCatB)) {
            case 0x0B: return ctx.elementLoadout[0] == elt || ctx.elementLoadout[0] == ctx.GetPaired(elt);
            case 0x0C: return ctx.elementLoadout[1] == elt || ctx.elementLoadout[1] == ctx.GetPaired(elt);
            case 0x0D: return ctx.elementLoadout[2] == elt || ctx.elementLoadout[2] == ctx.GetPaired(elt);
            case 0x0F: case 0x12: return false;
            case 0x15: return elt == 0;
            case 0x16: return elt == 1;
            case 0x17: return elt == 2;
            case 0x1C: return ctx.elementLoadout[3] == elt || ctx.elementLoadout[3] == ctx.GetPaired(elt);
            case 0x1D: return elt == 3;
            case 0x1F: case 0x23: return elt == 0 || elt == ctx.GetPaired(0);
            case 0x20: case 0x24: return elt == 1 || elt == ctx.GetPaired(1);
            case 0x21: case 0x25: return elt == 2 || elt == ctx.GetPaired(2);
            case 0x22: case 0x26: return elt == 3 || elt == ctx.GetPaired(3);
            case 0x2F: return ctx.factionFlag == 1;
            case 0x30: return ctx.factionFlag == 2;
            default:   return false;
        }
    case 6: return elt == 0 || elt == ctx.GetPaired(0);
    case 7: return elt == 1 || elt == ctx.GetPaired(1);
    case 8: return elt == 2 || elt == ctx.GetPaired(2);
    case 9: return elt == 3 || elt == ctx.GetPaired(3);
    case 0x0E: return ctx.factionFlag == 1;
    case 0x0F: return ctx.factionFlag == 2;
    default:   return false;
    }
}

// ===========================================================================
// Npc_GetNameplateColor 0x540790.
// ===========================================================================
int Npc_GetNameplateColor(const void* def, const NpcQuestContext& ctx, int selfLevel, int selfLevelBonus) {
    if (!def) return 2; // no record -> treated as "hostile"/unknown (safe choice, not present in the binary which assumes def is valid)
    const int elt = ctx.localElement;

    auto pairedOrEqual = [&](int loadoutVal) { return loadoutVal == elt || loadoutVal == ctx.GetPaired(elt); };

    switch (NpcDefReadI32(def, kNpcDefOffQuestCatA)) {
    case 1:
        switch (NpcDefReadI32(def, kNpcDefOffQuestCatB)) {
            case 0x0B: return pairedOrEqual(ctx.elementLoadout[0]) ? 10 : 2;
            case 0x0C: return pairedOrEqual(ctx.elementLoadout[1]) ? 10 : 2;
            case 0x0D: return pairedOrEqual(ctx.elementLoadout[2]) ? 10 : 2;
            case 0x0F: case 0x12: return 2;
            case 0x15: return (elt == 0) ? 10 : 2;
            case 0x16: return (elt == 1) ? 10 : 2;
            case 0x17: return (elt == 2) ? 10 : 2;
            case 0x1C: return pairedOrEqual(ctx.elementLoadout[3]) ? 10 : 2;
            case 0x1D: return (elt == 3) ? 10 : 2;
            case 0x1F: case 0x23: return (elt != 0 && elt != ctx.GetPaired(0)) ? 2 : 10;
            case 0x20: case 0x24: return (elt == 1 || elt == ctx.GetPaired(1)) ? 10 : 2;
            case 0x21: case 0x25: return (elt == 2 || elt == ctx.GetPaired(2)) ? 10 : 2;
            case 0x22: case 0x26: return (elt == 3 || elt == ctx.GetPaired(3)) ? 10 : 2;
            case 0x2F: return (ctx.factionFlag == 1) ? 10 : 2;
            case 0x30: return (ctx.factionFlag == 2) ? 10 : 2;
            default: break; // -> default branch (aggro/level)
        }
        break;
    case 6: return (elt != 0 && elt != ctx.GetPaired(0)) ? 2 : 10;
    case 7: return (elt == 1 || elt == ctx.GetPaired(1)) ? 10 : 2;
    case 8: return (elt == 2 || elt == ctx.GetPaired(2)) ? 10 : 2;
    case 9: return (elt == 3 || elt == ctx.GetPaired(3)) ? 10 : 2;
    case 0x0E: return (ctx.factionFlag == 1) ? 10 : 2;
    case 0x0F: return (ctx.factionFlag == 2) ? 10 : 2;
    default: break; // -> default branch (aggro/level)
    }

    // Default branch (original LABEL_60/LABEL_76): compares the local player's "power"
    // (Level_ToAggroValue(level+bonus)) against the target's threshold (def+352).
    const int selfPower = Npc_LevelToAggroValue(selfLevelBonus + selfLevel);
    const int targetThreshold = NpcDefReadI32(def, kNpcDefOffAggroLevel);
    if (selfPower - targetThreshold < 10) {
        return (targetThreshold - selfPower > 10) ? 2 : 33;
    }
    return 22;
}

// ===========================================================================
// NpcInteractionSystem — state + 4 action functions.
// ===========================================================================
NpcInteractionExt& NpcInteractionSystem::Ext(std::size_t npcIndex) {
    if (npcIndex >= ext_.size()) ext_.resize(npcIndex + 1);
    return ext_[npcIndex];
}
const NpcInteractionExt* NpcInteractionSystem::TryExt(std::size_t npcIndex) const {
    return npcIndex < ext_.size() ? &ext_[npcIndex] : nullptr;
}

bool NpcInteractionSystem::ShouldRefresh(const NpcEntity& npc) const {
    return host.ShouldRefreshTarget ? host.ShouldRefreshTarget(npc) : true;
}

int NpcInteractionSystem::FindNpcIndexById(EntityId id) const {
    const auto& npcs = g_World.npcs;
    for (std::size_t i = 0; i < npcs.size(); ++i) {
        if (npcs[i].active && npcs[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

NpcInteractionSystem::RewardArgs NpcInteractionSystem::BuildRewardArgs(const NpcEntity& npc,
                                                                        const NpcInteractionExt& ext) const {
    RewardArgs a;
    a.idHi = static_cast<int>(npc.id.hi);
    a.idLo = static_cast<int>(npc.id.lo);
    a.p0 = 0; // literal 0, always (3rd argument of Net_SendVaultReq_201 in both branches)

    if (NpcDefReadI32(npc.def, kNpcDefOffKind) == 1) {
        // "Vendor"/direct vault branch — no bag placement, but weight guard
        // (Util_SumExceeds2Billion 0x53F660, cf. Quest_SumExceeds2Billion reused as-is).
        if (Quest_SumExceeds2Billion(g_Client.inv.weight, ext.offerWeight)) {
            a.blockedWeight = true;
            return a;
        }
        a.outSlot = a.outB = a.outC = a.outD = 0;
        a.ok = true;
        return a;
    }

    // cGameHud_PlaceItemIntoBag 0x650470 branch — faithful output order (v9,v11,v10,v14).
    int slot = -1, b = 0, c = 0, d = 0;
    if (host.TryPlaceItemIntoBag) host.TryPlaceItemIntoBag(ext.offerItemId, ext.offerWeight, slot, b, c, d);
    if (slot == -1) {
        a.blockedBag = true;
        return a;
    }
    a.outSlot = slot; a.outB = b; a.outC = c; a.outD = d;
    a.ok = true;
    return a;
}

void NpcInteractionSystem::SendReward(const RewardArgs& args, float gameTimeSec) {
    if (!args.ok) return;
    if (morphInProgress || pendingLatch_) return; // faithful: silent, no error
    if (host.SendVaultReq201) {
        host.SendVaultReq201(args.idHi, args.idLo, args.p0, args.outSlot, args.outB, args.outC, args.outD);
    }
    pendingLatch_ = true;
    pendingLatchTimeSec_ = gameTimeSec;
}

// ---------------------------------------------------------------------------
// Npc_Interact 0x53A660.
// ---------------------------------------------------------------------------
void NpcInteractionSystem::Interact(EntityId targetId, float gameTimeSec) {
    const int idx = FindNpcIndexById(targetId);
    if (idx < 0) return; // "i == dword_1687228" -> absent, silent

    const NpcEntity& npc = g_World.npcs[static_cast<std::size_t>(idx)];
    if (!ShouldRefresh(npc)) {
        // Fidelity TODO: original color g_SysMsgColor 0x84DFD8 not modeled ; MessageLog
        // uses its default white color.
        g_Client.msg.System(Str(115));
        return;
    }

    const NpcInteractionExt& ext = Ext(static_cast<std::size_t>(idx));
    const PlayerEntity& self = g_World.Self();

    if (Npc_DistanceXZ(ext.x, ext.z, self.x, self.z) <= kNpcInteractRange) {
        RewardArgs a = BuildRewardArgs(npc, ext);
        if (a.blockedWeight) { g_Client.msg.System(Str(116)); return; }
        if (a.blockedBag)    { g_Client.msg.System(Str(117)); return; }
        SendReward(a, gameTimeSec);
    } else {
        // "Out of range" branch -> approach on foot (out of scope, cf. host.ApproachNpc).
        if (host.ApproachNpc) host.ApproachNpc(ext.x, ext.y, ext.z);
    }
}

// ---------------------------------------------------------------------------
// Npc_AutoInteract 0x53A980.
// ---------------------------------------------------------------------------
int NpcInteractionSystem::AutoInteractCurrentTarget(EntityId currentAttackOrderTarget, float gameTimeSec) {
    const int idx = FindNpcIndexById(currentAttackOrderTarget);
    if (idx < 0) return 0;

    const NpcEntity& npc = g_World.npcs[static_cast<std::size_t>(idx)];
    if (!ShouldRefresh(npc)) return 0;

    const NpcInteractionExt& ext = Ext(static_cast<std::size_t>(idx));
    const PlayerEntity& self = g_World.Self();

    // Faithful: unlike Interact(), AutoInteract triggers NO approach if out of range —
    // simply returns 1 (not an error on the caller's side).
    if (Npc_DistanceXZ(ext.x, ext.z, self.x, self.z) > kNpcInteractRange) return 1;

    RewardArgs a = BuildRewardArgs(npc, ext);
    if (!a.ok) return 0; // weight or bag blocked -> silent failure (no message here, faithful)

    if (morphInProgress || pendingLatch_) return 0;
    SendReward(a, gameTimeSec);
    return 1;
}

// ---------------------------------------------------------------------------
// Npc_AutoSelectNearest 0x53ABC0 — 6 decreasing-priority passes.
// ---------------------------------------------------------------------------
void NpcInteractionSystem::AutoSelectNearestInteractable(float gameTimeSec) {
    const auto& npcs = g_World.npcs;
    const PlayerEntity& self = g_World.Self();

    bool sawWeightBlocked = false; // v10
    bool sawBagBlocked = false;    // v11

    // Pass 1: "vendor"/direct vault NPC (kind==1), the closest exploitable one (the binary
    // takes the FIRST one found in array order, not the strictly nearest one — faithful:
    // no distance sort here).
    for (std::size_t i = 0; i < npcs.size(); ++i) {
        const NpcEntity& npc = npcs[i];
        if (!npc.active) continue;
        const NpcInteractionExt& ext = Ext(i);
        if (Npc_Distance3D(ext.x, ext.y, ext.z, self.x, self.y, self.z) > kNpcInteractRange) continue;
        if (!ShouldRefresh(npc)) continue;
        if (NpcDefReadI32(npc.def, kNpcDefOffKind) != 1) continue;

        if (Quest_SumExceeds2Billion(g_Client.inv.weight, ext.offerWeight)) { sawWeightBlocked = true; continue; }

        RewardArgs a; a.idHi = static_cast<int>(npc.id.hi); a.idLo = static_cast<int>(npc.id.lo);
        a.p0 = 0; a.outSlot = a.outB = a.outC = a.outD = 0; a.ok = true;
        SendReward(a, gameTimeSec);
        return;
    }

    // Passes 2..6: non-vendor NPC (kind!=1), decreasing categories {5,6} then 4,3,2,1.
    const std::array<std::array<int, 2>, 5> kCategoryPasses{{
        {5, 6}, {4, -1}, {3, -1}, {2, -1}, {1, -1}
    }};
    for (const auto& categories : kCategoryPasses) {
        for (std::size_t i = 0; i < npcs.size(); ++i) {
            const NpcEntity& npc = npcs[i];
            if (!npc.active) continue;
            const NpcInteractionExt& ext = Ext(i);
            if (Npc_Distance3D(ext.x, ext.y, ext.z, self.x, self.y, self.z) > kNpcInteractRange) continue;
            if (!ShouldRefresh(npc)) continue;
            if (NpcDefReadI32(npc.def, kNpcDefOffKind) == 1) continue;
            const int cat = NpcDefReadI32(npc.def, kNpcDefOffFaction);
            if (cat != categories[0] && cat != categories[1]) continue;

            int slot = -1, b = 0, c = 0, d = 0;
            if (host.TryPlaceItemIntoBag) host.TryPlaceItemIntoBag(ext.offerItemId, ext.offerWeight, slot, b, c, d);
            if (slot == -1) { sawBagBlocked = true; continue; }

            RewardArgs a; a.idHi = static_cast<int>(npc.id.hi); a.idLo = static_cast<int>(npc.id.lo);
            a.p0 = 0; a.outSlot = slot; a.outB = b; a.outC = c; a.outD = d; a.ok = true;
            SendReward(a, gameTimeSec);
            return;
        }
    }

    // No exploitable NPC found -> message (weight > bag > "nothing nearby").
    if (sawWeightBlocked) {
        g_Client.msg.System(Str(116));
    } else if (sawBagBlocked) {
        g_Client.msg.System(Str(117));
    } else {
        g_Client.msg.System(Str(632));
    }
}

// ---------------------------------------------------------------------------
// Npc_AutoInteractForPet 0x53B5F0.
// ---------------------------------------------------------------------------
void NpcInteractionSystem::AutoInteractForPet(uint32_t selectedItemId, float gameTimeSec) {
    if (selectedItemId < 1) return;

    const bool gateOpen = host.IsPetCommandItemReady ? host.IsPetCommandItemReady(selectedItemId) : false;
    if (!gateOpen) return;

    const auto& npcs = g_World.npcs;
    const PlayerEntity& self = g_World.Self();

    for (std::size_t i = 0; i < npcs.size(); ++i) {
        const NpcEntity& npc = npcs[i];
        if (!npc.active) continue;
        const NpcInteractionExt& ext = Ext(i);
        if (Npc_Distance3D(ext.x, ext.y, ext.z, self.x, self.y, self.z) > kNpcInteractRange) continue;
        if (!ShouldRefresh(npc)) continue;

        if (NpcDefReadI32(npc.def, kNpcDefOffKind) == 1) {
            if (Quest_SumExceeds2Billion(g_Client.inv.weight, ext.offerWeight)) continue;

            RewardArgs a; a.idHi = static_cast<int>(npc.id.hi); a.idLo = static_cast<int>(npc.id.lo);
            a.p0 = 0; a.outSlot = a.outB = a.outC = a.outD = 0; a.ok = true;
            if (!morphInProgress && !pendingLatch_) SendReward(a, gameTimeSec);
            return;
        }

        // Faithful: the binary compares the record id 3 times (1401, 2132, 1401 — the 3rd
        // test is a dead duplicate from the original binary, reproduced as-is, not "fixed").
        const int recordId0 = NpcDefReadI32(npc.def, 0);
        if (recordId0 == 1401 || recordId0 == 2132 || recordId0 == 1401) continue;

        const int cat = NpcDefReadI32(npc.def, kNpcDefOffFaction);
        const int equipCat = host.GetEquipCategory ? host.GetEquipCategory(selectedItemId) : -1;

        const bool matchA = (cat == 3 || cat == 4 || cat == 5) && equipCat == 1;
        const bool matchB = (cat == 4 || cat == 5) && equipCat == 2;
        if (!matchA && !matchB) continue;

        int slot = -1, b = 0, c = 0, d = 0;
        if (host.TryPlaceItemIntoBag) host.TryPlaceItemIntoBag(ext.offerItemId, ext.offerWeight, slot, b, c, d);
        if (slot == -1) continue;

        RewardArgs a; a.idHi = static_cast<int>(npc.id.hi); a.idLo = static_cast<int>(npc.id.lo);
        a.p0 = 0; a.outSlot = slot; a.outB = b; a.outC = c; a.outD = d; a.ok = true;
        if (!morphInProgress && !pendingLatch_) SendReward(a, gameTimeSec);
        return;
    }
    // No match -> silent (no message, faithful to the original).
}

} // namespace ts2::game

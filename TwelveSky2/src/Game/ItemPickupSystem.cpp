// Game/ItemPickupSystem.cpp — see ItemPickupSystem.h for detailed EA references.
#include "Game/ItemPickupSystem.h"
#include <cmath>
#include <cstddef>

namespace ts2::game {

namespace {

// 3D Euclidean distance (equivalent to Math_Dist3D, EA 0x53faa0: sqrt(dx^2+dy^2+dz^2)).
float Dist3D(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

GroundPickupTarget FindNearestGroundItem(GameWorld& world) {
    GroundPickupTarget best;

    // Local player position, equivalent to flt_1687330 (the "self" block of entity
    // index 0 of the player array, cf. GameState.h PlayerEntity::x/y/z).
    PlayerEntity& self = world.Self();

    for (std::size_t i = 0; i < world.groundItems.size(); ++i) {
        GroundItem& candidate = world.groundItems[i];
        if (!candidate.active)
            continue;

        const float d = Dist3D(candidate.x, candidate.y, candidate.z, self.x, self.y, self.z);
        if (best.item == nullptr || d < best.distance) {
            best.index    = static_cast<int>(i);
            best.item     = &candidate;
            best.distance = d;
        }
    }
    return best;
}

bool IsWithinPickupRange(const GameWorld& world, const GroundItem& target) {
    if (!target.active)
        return false;

    // world.Self() is not const (may insert a default player); accessed here read-only
    // via a local non-const reference, consistent with this system's usage
    // (called after FindNearestGroundItem, which already guarantees a player is present).
    GameWorld& mutableWorld = const_cast<GameWorld&>(world);
    const PlayerEntity& self = mutableWorld.Self();

    // EA 0x539eef (Item_PickupTarget) / EA 0x539def (Item_InteractGround):
    // if (Math_Dist3D(target, player) <= 20.0)
    const float d = Dist3D(target.x, target.y, target.z, self.x, self.y, self.z);
    return d <= kPickupRange;
}

bool WouldExceedWeightCapacity(int64_t currentWeight, int64_t addedItemWeight) {
    // EA 0x53f660 (Util_SumExceeds2Billion): return a2 + (__int64)a1 > 2000000000;
    return (currentWeight + addedItemWeight) > kWeightOverflowGuard;
}

PickupOutcome EvaluatePickup(GameWorld& world, GroundPickupTarget& outTarget,
                              int64_t itemWeight) {
    outTarget = FindNearestGroundItem(world);
    if (outTarget.item == nullptr)
        return PickupOutcome::NoTarget;

    if (!IsWithinPickupRange(world, *outTarget.item))
        return PickupOutcome::OutOfRange;

    if (WouldExceedWeightCapacity(g_Client.inv.weight, itemWeight))
        return PickupOutcome::WouldExceedWeight;

    return PickupOutcome::Ok;
    // TODO (EA 0x5db530): emission of the pickup confirmation network request —
    // not identified in Item_PickupTarget/Item_InteractGround (see .h comment).
}

int32_t ValidateSplitQuantity(int32_t maxDisponible, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur) {
    // EA 0x5b1713/0x5b1731 (case 1, identical pattern in every other switch case):
    //   if (this[19] >= 1) v = Crt_Atoi(this+20); else v = maxDisponible;
    int32_t quantite = aSaisieUtilisateur ? quantiteDemandee : maxDisponible;

    // EA 0x5b1767/0x5b1785: if (v > maxDisponible) v = maxDisponible;
    if (quantite > maxDisponible)
        quantite = maxDisponible;

    // EA 0x5b178c: if (v >= 1) { ... } else { sub_5B02D0(this) /* cancel */ }
    if (quantite < 1)
        return 0;

    return quantite;
}

int32_t ValidateSplitQuantity(const InvCell& cellule, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur) {
    // case this[4]==1: g_InvGrid_Count[384*row+6*col] -> InvCell::flag (cf. ClientRuntime.h).
    return ValidateSplitQuantity(static_cast<int32_t>(cellule.flag), quantiteDemandee,
                                  aSaisieUtilisateur);
}

int32_t ValidateSplitQuantity(const GroundPickupSlot& cellule, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur) {
    // case this[4]==5: dword_1674400[42*conteneur+3*slot] -> GroundPickupSlot::count.
    return ValidateSplitQuantity(static_cast<int32_t>(cellule.count), quantiteDemandee,
                                  aSaisieUtilisateur);
}

} // namespace ts2::game

// Game/GroundAuraWorldObjectTick.cpp — voir GroundAuraWorldObjectTick.h pour les EAs
// d'origine, les offsets exacts et les écarts de nommage documentés.
#include "Game/GroundAuraWorldObjectTick.h"
#include "Game/ClientRuntime.h" // g_Client.Var (échappatoire longue traîne, GetFxAuraCount)
#include <cmath>

namespace ts2::game {

namespace {

constexpr float kFrameRate30      = 30.0f; // Fx_MeleeSwingTick_Loop/Once @0x58043E/0x5804DE : a3*30.0
constexpr float kFarCullDistance  = 400.0f; // Fx_MeleeSwingTick_Loop @0x580483
constexpr uint32_t kFxAuraCountAddr = 0x168722Cu; // g_FxAuraCount d'origine

// Redimensionne un vecteur d'extension pour couvrir `index` (croissance paresseuse — même
// idiome que Game/EntityLifecycleTick.cpp::EnsureCapacity).
template <class T>
bool EnsureCapacity(std::vector<T>& ext, int index) {
    if (index < 0) return false;
    if (static_cast<size_t>(index) >= ext.size()) ext.resize(static_cast<size_t>(index) + 1);
    return true;
}

inline float Dist3D(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Fx_MeleeSwingTick_Loop 0x580400.
void TickLoop(GroundItem& item, GroundItemTickExt& ext, float dt,
              const GroundAuraWorldObjectTickHost& host, const PlayerEntity& self) {
    const int frameCount = host.GetWeaponEffectFrameCount
                                ? host.GetWeaponEffectFrameCount(ext.effectDefHandle, ext.mode)
                                : -1; // oracle nul -> ne boucle jamais (dégradation propre)
    ext.frame += dt * kFrameRate30; // @0x58043E
    if (frameCount > 0 && ext.frame >= static_cast<float>(frameCount))
        ext.frame -= static_cast<float>(frameCount); // @0x58045F

    // @0x580483 : Math_Dist3D((float*)this+5, flt_1687330) > 400.0 -> this+11 = this+20.
    // flt_1687330 == position du joueur local (game::GameWorld::Self(), même convention que
    // Game/ItemPickupSystem.h/NpcInteraction.h/AutoPlaySystem.h).
    if (Dist3D(item.x, item.y, item.z, self.x, self.y, self.z) > kFarCullDistance)
        ext.farField44 = ext.farSrcField80; // @0x58048E (sémantique non déterminée)
}

// Fx_MeleeSwingTick_Once 0x5804A0.
void TickOnce(GroundItemTickExt& ext, float dt, const GroundAuraWorldObjectTickHost& host) {
    const int frameCount = host.GetWeaponEffectFrameCount
                                ? host.GetWeaponEffectFrameCount(ext.effectDefHandle, ext.mode)
                                : -1;
    ext.frame += dt * kFrameRate30; // @0x5804DE
    if (frameCount > 0 && ext.frame >= static_cast<float>(frameCount)) {
        ext.mode  = 0; // @0x5804F8
        ext.frame = 0.0f; // @0x580504
    }
}

} // namespace

// ===========================================================================
// 1. Fx_MeleeSwingTick 0x5803A0
// ===========================================================================

void ResetGroundItemTickExt(int groundItemIndex) {
    if (!EnsureCapacity(g_GroundItemTickExt, groundItemIndex)) return;
    g_GroundItemTickExt[static_cast<size_t>(groundItemIndex)] = GroundItemTickExt{};
}

void TickGroundItemEffect(GameWorld& world, int groundItemIndex, float dt,
                           const GroundAuraWorldObjectTickHost& host) {
    if (groundItemIndex < 0 || static_cast<size_t>(groundItemIndex) >= world.groundItems.size())
        return;
    GroundItem& item = world.groundItems[static_cast<size_t>(groundItemIndex)];
    if (!item.active) return; // garde de tête `*(this+1)` @0x5803AC (défensive, déjà filtré
                               // par l'appelant InGameTickFlow.cpp)

    if (!EnsureCapacity(g_GroundItemTickExt, groundItemIndex)) return;
    GroundItemTickExt& ext = g_GroundItemTickExt[static_cast<size_t>(groundItemIndex)];

    switch (ext.mode) { // *(this+3) @0x5803BA
    case 0: TickLoop(item, ext, dt, host, world.Self()); break; // @0x5803D9
    case 1: TickOnce(ext, dt, host); break;                     // @0x5803EE
    default: break;                                             // no-op fidèle (autre valeur)
    }
}

// ===========================================================================
// 2. Pool de projectiles d'attaque (g_FxAuraCount / dword_17D06F4)
// ===========================================================================

int GetFxAuraCount() {
    return g_Client.VarGet(kFxAuraCountAddr);
}

bool IsFxAuraActive(int) {
    return false; // pool SoA non modélisé côté ClientSource — repli sûr documenté (cf. .h)
}

void UpdateHomingProjectile(int, float) {
    // Fx_HomingProjectileUpdate 0x5862D0 — HORS PÉRIMÈTRE (cf. .h). No-op intentionnel.
}

// ===========================================================================
// 3. Objets de zone / nœuds de ressource (g_World.zoneObjects)
// ===========================================================================

int GetWorldObjectCount(const GameWorld& world) {
    return static_cast<int>(world.zoneObjects.size());
}

bool IsWorldObjectActive(const GameWorld& world, int index) {
    if (index < 0 || static_cast<size_t>(index) >= world.zoneObjects.size()) return false;
    return world.zoneObjects[static_cast<size_t>(index)].active;
}

void TickWorldObject(float) {
    // sub_584170 0x584170 : stub vide dans le binaire d'origine. Reproduit fidèlement.
}

} // namespace ts2::game

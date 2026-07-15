// Game/EntityLifecycleTick.cpp — voir EntityLifecycleTick.h pour les EAs d'origine, les
// offsets exacts et les écarts documentés (nommage sub_580550, ambiguïté this+24, etc.).
#include "Game/EntityLifecycleTick.h"

namespace ts2::game {

namespace {

constexpr float kGibGravity  = -300.0f; // Fx_GibUpdate @0x583d1e : a3 * -300.0
constexpr float kFallGravity = -300.0f; // Char_Update   @0x582197 : a3 * -300.0
constexpr float kFrameRate30 = 30.0f;   // conversion dt(s) -> frames @30 FPS (a3*30.0)
constexpr float kGroundProbeOffset = 20.0f; // Char_Update @0x58221b / Fx_GibUpdate @0x583db1 : pos.y + 20.0

// Redimensionne un vecteur d'extension pour couvrir `index` (croissance paresseuse, comme
// GameState.cpp::FindOrAdd agrandit ses tableaux). Retourne false si index < 0.
template <class T>
bool EnsureCapacity(std::vector<T>& ext, int index) {
    if (index < 0) return false;
    if (static_cast<size_t>(index) >= ext.size()) ext.resize(static_cast<size_t>(index) + 1);
    return true;
}

} // namespace

void ResetMonsterTickExt(int monsterIndex) {
    if (!EnsureCapacity(g_MonsterTickExt, monsterIndex)) return;
    g_MonsterTickExt[static_cast<size_t>(monsterIndex)] = MonsterTickExt{};
}

void ResetNpcTickExt(int npcIndex) {
    if (!EnsureCapacity(g_NpcTickExt, npcIndex)) return;
    g_NpcTickExt[static_cast<size_t>(npcIndex)] = NpcTickExt{};
}

// ===========================================================================
// Les 3 fonctions "désactivation de slot" (sub_55D720 / sub_580550 / sub_583390) —
// toutes trois décompilent en `*(_DWORD*)this = 0; return this;`, cf. écart documenté
// dans EntityLifecycleTick.h. Primitive commune :
// ===========================================================================
namespace {
template <class Vec>
void DeactivateSlot(Vec& v, int index) {
    if (index < 0 || static_cast<size_t>(index) >= v.size()) return;
    v[static_cast<size_t>(index)].active = false; // *(_DWORD*)this = 0
}
} // namespace

void DespawnStalePlayer(GameWorld& world, int playerIndex) {              // sub_55D720
    DeactivateSlot(world.players, playerIndex);
}

void RespawnMonsterAfterKnockback(GameWorld& world, int monsterIndex) {   // sub_580550
    DeactivateSlot(world.monsters, monsterIndex);
}

void CleanupStaleNpcEntity(GameWorld& world, int npcIndex) {              // sub_583390
    DeactivateSlot(world.npcs, npcIndex);
}

// ===========================================================================
// Char_Update 0x581E10 — tick monstre.
// ===========================================================================
void UpdateMonster(GameWorld& world, int monsterIndex, float dt, const EntityLifecycleTickHost& host) {
    if (monsterIndex < 0 || static_cast<size_t>(monsterIndex) >= world.monsters.size()) return;
    MonsterEntity& mon = world.monsters[static_cast<size_t>(monsterIndex)];
    if (!mon.active) return; // garde de tête `if (*(_DWORD*)this)` @0x581e1c

    if (!EnsureCapacity(g_MonsterTickExt, monsterIndex)) return;
    MonsterTickExt& ext = g_MonsterTickExt[static_cast<size_t>(monsterIndex)];

    // --- 3 timers de swap de modèle (this+64/65, 66/67, 68/69) @0x581e29..0x581f24 ---
    for (int slot = 0; slot < 3; ++slot) {
        if (!ext.auraActive[slot]) continue;
        ext.auraFrame[slot] += dt * kFrameRate30;
        const float duration = host.GetAuraSwapDuration ? host.GetAuraSwapDuration(slot) : 0.0f;
        if (ext.auraFrame[slot] >= duration) ext.auraActive[slot] = false;
    }

    // --- Fenêtre de coup (états 5=AttackA / 7=AttackB uniquement) @0x581f34..0x582145 ---
    if (ext.motionState == 5 && ext.attackWindupMode == 1) {
        bool justArmed = false;
        const bool inWindow = host.IsFrameInHitListA && host.IsFrameInHitListA(ext.weaponOrSkillAnimId, ext.animFrame);
        if (ext.hitLatched) {
            if (!inWindow) ext.hitLatched = false; // @0x581fbe
        } else if (inWindow) {
            ext.hitLatched = true;                 // @0x581f8c
            justArmed = true;                       // v12=1 @0x581f93
        }
        if (justArmed) {
            const bool bypass = host.IsAttackTargetBypassActive && host.IsAttackTargetBypassActive(); // @0x581ff2
            bool targetExists = false;
            if (!bypass) {
                // Scan g_EntityArray (joueurs) pour une entité active dont l'id == attackTargetId.
                // @0x581ff4..0x582060
                for (const PlayerEntity& p : world.players) {
                    if (p.active && p.id == ext.attackTargetId) { targetExists = true; break; }
                }
            }
            if (bypass || targetExists) { // LABEL_32 @0x58206a
                if (ext.hitActionKind == 1) {
                    if (ext.hitActionSub == 1 && host.SendMeleeHit1) host.SendMeleeHit1(monsterIndex);      // @0x582095
                    else if (ext.hitActionSub == 2 && host.SendMeleeHit2) host.SendMeleeHit2(monsterIndex); // @0x58209f
                } else if (ext.hitActionKind == 2 && host.SpawnAttackProjectile) {
                    host.SpawnAttackProjectile(monsterIndex); // @0x5820a9
                }
            }
        }
    } else if (ext.motionState == 7 && ext.attackWindupMode == 1) {
        bool justArmed = false;
        const bool inWindow = host.IsFrameInHitListB && host.IsFrameInHitListB(ext.weaponOrSkillAnimId, ext.animFrame);
        if (ext.hitLatched) {
            if (!inWindow) ext.hitLatched = false; // @0x582125
        } else if (inWindow) {
            ext.hitLatched = true;  // @0x5820f3
            justArmed = true;        // @0x5820fa
        }
        if (justArmed && host.SpawnAttackProjectileAlt) host.SpawnAttackProjectileAlt(monsterIndex); // @0x58213d
    }

    // --- Chute/knockback (overlay ABSOLU, ne touche PAS mon.x/y/z) @0x582145..0x5822b6 ---
    // CORRIGÉ (audit étapes 5-8, 2026-07-14) : re-vérification par désassemblage brut
    // (pas seulement le pseudocode Hex-Rays) de 0x582145..0x582289 -- la sonde de sol
    // ET le son d'atterrissage utilisent fallOffX/fallOffZ COURANTS (this+0xF0/+0xF8,
    // copiés en argument à 0x582206..0x582236, PAS this+... de MonsterEntity.x/z) et
    // NON mon.x/mon.z. La version précédente de ce fichier passait par erreur mon.x/mon.z
    // (position réelle statique du monstre, jamais mise à jour par ce bloc) au lieu de la
    // position de vol suivie par fallOffX/Y/Z -- avec MonsterTickExt zéro-initialisé tant
    // qu'aucun spawn ne l'amorce (cf. TODO ResetMonsterTickExt plus haut), la sonde
    // originale visait donc systématiquement (0,0,~20) au lieu du point de chute réel.
    // Second écart corrigé : la hauteur d'origine de la sonde utilise fallOffY D'AVANT
    // la mise à jour de ce tick (snapshot @0x582152..0x58217f, lu à 0x582212 AVANT le
    // recalcul de 0x5821d3), pas la valeur déjà intégrée -- capturée ici dans
    // oldFallOffY avant les += ci-dessous.
    if (ext.fallActive && ext.fallLandCounter < ext.fallLandThreshold) {
        const float oldFallOffY = ext.fallOffY; // snapshot PRÉ-update @0x582152-0x58217f
        ext.fallVelY += dt * kFallGravity;             // @0x582197 (gravité)
        ext.fallOffX += ext.fallVelX * dt;              // @0x5821b5
        ext.fallOffY += ext.fallVelY * dt;              // @0x5821d3
        ext.fallOffZ += ext.fallVelZ * dt;              // @0x5821f1
        float groundY = 0.0f;
        const bool found = host.GetGroundHeight &&
            host.GetGroundHeight(ext.fallOffX, ext.fallOffZ, oldFallOffY + kGroundProbeOffset, groundY); // @0x58223e
        if (found && ext.fallOffY < groundY - ext.fallGroundClearance) {             // @0x582263
            ++ext.fallLandCounter;                                                   // @0x582274
            ext.fallOffY = groundY - ext.fallGroundClearance;                        // @0x582289
            if (ext.fallLandCounter == 1 && host.PlayLandingSound) {                 // @0x582299
                host.PlayLandingSound(ext.fallOffX, ext.fallOffY, ext.fallOffZ);      // @0x5822b1 (position = fallOff courant)
            }
        }
    }

    // --- Dispatch FSM finale (switch this+6) — HORS PÉRIMÈTRE, cf. header. @0x5822d3 ---
    if (host.DispatchMotionTick) host.DispatchMotionTick(monsterIndex, dt);
}

// ===========================================================================
// Fx_GibUpdate 0x583CD0 — tick NPC/effet (gravité + collision sol).
// ===========================================================================
void TickNpcEffect(GameWorld& world, int npcIndex, float dt, const EntityLifecycleTickHost& host) {
    if (npcIndex < 0 || static_cast<size_t>(npcIndex) >= world.npcs.size()) return;
    NpcEntity& npc = world.npcs[static_cast<size_t>(npcIndex)];
    if (!npc.active) return; // garde `if (*(_DWORD*)this)` @0x583cdc

    if (!EnsureCapacity(g_NpcTickExt, npcIndex)) return;
    NpcTickExt& ext = g_NpcTickExt[static_cast<size_t>(npcIndex)];
    if (!ext.gibActive) return; // garde `if (*((_DWORD*)this+21))` @0x583ce9

    // CORRIGÉ (audit étapes 5-8, 2026-07-14) : re-vérification par désassemblage brut de
    // 0x583ce9..0x583db1 -- la hauteur d'origine de la sonde utilise posY D'AVANT la mise
    // à jour de ce tick (snapshot @0x583cf4..0x583d09, lu à 0x583da8 AVANT le recalcul de
    // 0x583d51), pas ext.posY déjà intégré (écart mineur : jusqu'à velY*dt ~ plusieurs
    // dizaines d'unités en fin de chute rapide). x/z restent corrects (posX/posZ COURANTS,
    // déjà fidèles dans la version précédente).
    const float oldPosY = ext.posY; // snapshot PRÉ-update @0x583cf4-0x583d09
    ext.velY += dt * kGibGravity;      // @0x583d1e (gravité)
    ext.posX += ext.velX * dt;         // @0x583d36
    ext.posY += ext.velY * dt;         // @0x583d51
    ext.posZ += ext.velZ * dt;         // @0x583d6c
    ext.extraValue += ext.extraRate * dt; // @0x583d87

    float groundY = 0.0f;
    const bool found = host.GetGroundHeight &&
        host.GetGroundHeight(ext.posX, ext.posZ, oldPosY + kGroundProbeOffset, groundY); // @0x583dd4
    if (found && groundY >= ext.posY) { // @0x583df2
        ext.gibActive = false;           // @0x583df9
        ext.posY = groundY;              // @0x583e06
    }
}

} // namespace ts2::game

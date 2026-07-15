// Game/AutoPlaySystem.cpp — voir AutoPlaySystem.h pour la table EA -> méthode complète.
#include "Game/AutoPlaySystem.h"
#include <cmath>
#include <cstring>
#include <utility>

namespace ts2::game {

namespace {

// ---------------------------------------------------------------------------
// Lecture LE brute dans un enregistrement de table .IMG (MONSTER_INFO/NPC — non
// modélisés en struct typée, cf. Docs/TS2_IMG_FORMAT.md §7) ou dans le corps brut
// d'un paquet (NpcEntity::body). Même convention que ResolveMobDef/RdU32 côté
// EntityManager.cpp (non exportées, réimplantées ici localement).
// ---------------------------------------------------------------------------
int32_t ReadI32(const void* base, std::size_t offset) {
    int32_t v = 0;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}
uint32_t ReadU32(const void* base, std::size_t offset) {
    uint32_t v = 0;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}
float ReadF32(const void* base, std::size_t offset) {
    float v = 0.0f;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}

// Offsets dans le record MONSTER_INFO/NPC_INFO (944 o, table 005_00004.IMG, stride
// partagé monstres+NPC dans cette réécriture — cf. Game/EntityManager.cpp ResolveMobDef).
// Suit la convention prouvée sur ITEM_INFO (nom en clair à +4, cf. GameDatabase.h) :
// on suppose la même convention pour le nom ici (utilisé par IsFriend/IsEnemy ci-dessous).
constexpr std::size_t kDefOffName       = 4;   // nom / propriétaire (chaîne, convention ITEM_INFO)
constexpr std::size_t kDefOffFaction    = 184; // dword_17AB598[i]+184 (AutoPlay_FindNpcTarget)
constexpr std::size_t kDefOffNpcKind    = 188; // dword_17AB598[i]+188 == 1 (NPC "vendeur/interactable")
constexpr std::size_t kDefOffMonCat232  = 232; // v13+232 <= 3 (AutoPlay_Build/SelectTarget/Count)
constexpr std::size_t kDefOffMonCat236  = 236; // v13+236 <= 1

// Offsets dans NpcEntity::body (84 o, wire payload+8..91) — cf. commentaire RecvPackets.h
// "body[0..3] = id modele mob-db". Les champs suivants sont déduits de l'arithmétique de
// pointeur d'AutoPlay_FindNpcTarget/MoveToNpc sur le tableau runtime d'origine (base
// dword_17AB534, body démarrant à +16 dans ce tableau = payload+8) :
constexpr std::size_t kNpcBodyOffDefId  = 0;  // dword_17AB544[i] (= body[0..3], déjà résolu via NpcEntity::def)
constexpr std::size_t kNpcBodyOffOffer  = 4;  // dword_17AB548[i] : poids/quantité de l'offre de ramassage
constexpr std::size_t kNpcBodyOffPosX   = 16; // unk_17AB554 (position monde du NPC, absente de NpcEntity)
constexpr std::size_t kNpcBodyOffPosY   = 20;
constexpr std::size_t kNpcBodyOffPosZ   = 24;

constexpr float kFixedDt = 0.033f; // littéral utilisé par TOUS les appels MapColl_SlideMoveGround
                                    // du cluster (distinct de flt_815188=0.033333 du tick 30 FPS).
constexpr float kNpcInteractRange = 50.0f; // seuil de distance NPC en mode "portée de compétence"
constexpr float kRebuildIntervalSec = 1.0f;      // 0x3E8 ms
constexpr float kNpcInteractCooldownSec = 0.05f; // 0x32 ms
constexpr float kInitialElapsedSec = 1000.0f;    // >> aux deux seuils ci-dessus : action immédiate au 1er tick

// Stat_UnpackCombined 0x54CE40 — utilitaire pur, traduit tel quel (nécessaire à
// CheckTownScroll pour la lecture du talisman).
void UnpackCombined(int32_t v, int32_t& hi, int32_t& lo) {
    if (v < 0) { hi = 0; lo = 0; return; }
    hi = v / 1000000;
    lo = v % 1000000;
    if (hi > 100) hi = 0;
    if (lo > 100000) lo = 0;
}

} // namespace (anonyme)

// ===========================================================================
// AutoPlay_Construct 0x457EA0
// ===========================================================================
AutoPlaySystem::AutoPlaySystem() {
    // targets_ / targetCount_ / currentTargetIndex_ sont déjà à leurs valeurs d'origine
    // via les initialiseurs par défaut (id=-1, count=0, cible=-1 — cf. AutoPlayTargetSlot
    // et les membres de classe). Les timers démarrent "expirés" pour déclencher une
    // construction de liste / interaction NPC dès le premier appel, comme le binaire où
    // GetTickCount() initial excède toujours les seuils de 1000 ms / 50 ms depuis 0.
    rebuildTimerSec_ = kInitialElapsedSec;
    npcInteractCooldownSec_ = kInitialElapsedSec;
    // friendNames/enemyNames restent vides (List_Construct(this+296)/(this+324) d'origine).
    // NB : les offsets 0..19 et 264..275 remis à zéro par Crt_Memset dans le binaire
    // appartiennent à l'état de panneau UI autoplay (hors périmètre de ce cluster,
    // aucune des ~19 fonctions étudiées ne les lit) — non reproduits ici.
}

// ===========================================================================
// AutoPlay_DistanceToPlayer 0x4589E0
// ===========================================================================
float AutoPlaySystem::DistanceToPlayer(float x, float y, float z) {
    const PlayerEntity& self = g_World.Self();
    const float dx = x - self.x;
    const float dz = z - self.z;
    (void)y; // la composante Y (0x458a1c/0x458a5a) est calculée dans le binaire d'origine
             // puis JAMAIS additionnée au total (v6 = v8 + v5, sans le terme Y) : distance
             // strictement 2D (X,Z) — particularité fidèlement reproduite, pas une omission.
    return std::sqrt(dx * dx + dz * dz);
}

// ===========================================================================
// AutoPlay_InsertTargetSorted 0x458870 — insertion triée par distance croissante,
// capacité 15, l'éventuel élément le plus faible expulsé en fin de liste pleine est
// abandonné (fidèle : le binaire ne l'append nulle part si count==15).
// ===========================================================================
void AutoPlaySystem::InsertTargetSorted(int32_t monsterIndex, float distance, bool available) {
    for (uint16_t i = 0; i < targetCount_; ++i) {
        if (distance < targets_[i].distance) {
            std::swap(monsterIndex, targets_[i].monsterIndex);
            std::swap(distance, targets_[i].distance);
            std::swap(available, targets_[i].available);
        }
    }
    if (targetCount_ < 15) {
        targets_[targetCount_].monsterIndex = monsterIndex;
        targets_[targetCount_].distance = distance;
        targets_[targetCount_].available = available;
        ++targetCount_;
    }
}

// ===========================================================================
// AutoPlay_IsTargetLocked 0x458B80
// ===========================================================================
bool AutoPlaySystem::IsTargetLocked(int32_t monsterIndex) const {
    // Garde fidèle : le binaire refuse tout verrouillage dès que la liste est pleine
    // (>= 15), même si l'id recherché y figure — particularité conservée telle quelle.
    if (targetCount_ >= 15) return false;
    for (uint16_t i = 0; i < targetCount_; ++i) {
        if (targets_[i].available && targets_[i].monsterIndex == monsterIndex)
            return true;
    }
    return false;
}

// ===========================================================================
// AutoPlay_ClearTargetSlot 0x4587E0 — libère le slot dont l'id == cible courante, puis
// remet la cible courante à "aucune". Le binaire écrit un NaN flottant sur ce champ (type
// confondu par le décompilateur, cf. cast float* du paramètre) ; les 4 autres sites qui
// touchent ce même champ (+220) utilisent tous le sentinel entier -1 (Construct,
// RemoveTargetByMonsterIndex, SelectTarget, BuildTargetList) — normalisé à -1 ici, même
// rôle, pas une supposition sur la valeur.
// ===========================================================================
void AutoPlaySystem::ClearTargetSlot() {
    for (uint16_t i = 0; i < 15; ++i) {
        if (targets_[i].monsterIndex == currentTargetIndex_) {
            targets_[i] = AutoPlayTargetSlot{};
            currentTargetIndex_ = -1;
            return;
        }
    }
}

// ===========================================================================
// AutoPlay_ResetTargetList 0x458AB0 — si la cible courante n'est PAS verrouillée, on la
// libère (et on réinitialise currentTargetIndex_ à -1) avant de vider toute la liste ;
// si elle EST verrouillée, elle survit au reset (ClearTargetSlot non appelée), seule la
// liste des 15 slots candidats est vidée.
// ===========================================================================
void AutoPlaySystem::ResetTargetList() {
    if (!IsTargetLocked(currentTargetIndex_))
        ClearTargetSlot();
    targetCount_ = 0;
    targets_.fill(AutoPlayTargetSlot{});
}

// ===========================================================================
// AutoPlay_RemoveTargetById 0x458E00 — scanne les 15 slots (pas seulement les occupés,
// comme le binaire) ; supprime le premier slot dont l'id correspond et réinitialise
// TOUJOURS la cible courante à -1 dès qu'une suppression a lieu (même si ce n'était pas
// elle) — particularité fidèlement conservée.
// ===========================================================================
void AutoPlaySystem::RemoveTargetByMonsterIndex(int32_t monsterIndex) {
    for (uint16_t i = 0; i < 15; ++i) {
        if (targets_[i].monsterIndex == monsterIndex) {
            targets_[i] = AutoPlayTargetSlot{};
            currentTargetIndex_ = -1;
            return;
        }
    }
}

// ===========================================================================
// AutoPlay_BuildTargetList 0x458280
// ===========================================================================
bool AutoPlaySystem::BuildTargetList() {
    if (!externalState.worldReady) return false; // dword_14A88E8

    ResetTargetList();

    const PlayerEntity& self = g_World.Self();
    const float speed = host.GetSelfMoveSpeed ? host.GetSelfMoveSpeed() : 1.0f;

    for (std::size_t i = 0; i < g_World.monsters.size(); ++i) {
        const MonsterEntity& mon = g_World.monsters[i];

        // Gating fidèle : def résolu (v13), actif (dword_1766F74), état valide et != mort
        // (dword_1766F8C), catégories 232<=3 et 236<=1.
        if (!mon.def || !mon.active) continue;
        const MonsterAutoplayExt& ext = Ext(i);
        if (ext.state == 0 || ext.state == 12) continue;
        if (ReadI32(mon.def, kDefOffMonCat232) > 3) continue;
        if (ReadI32(mon.def, kDefOffMonCat236) > 1) continue;

        const float dist = DistanceToPlayer(mon.x, mon.y, mon.z);
        bool inRange;

        if (config.mode != 1) {
            // Branche "accessibilité" : le monstre doit être atteignable par glissement de
            // collision depuis la position du joueur (aucun obstacle sur la trajectoire).
            float outX = mon.x, outY = self.y, outZ = mon.z; // défaut si host non branché : atteignable
            if (host.SlideMove)
                host.SlideMove(self.x, self.y, self.z, mon.x, self.y, mon.z, speed, kFixedDt, outX, outY, outZ);
            inRange = (outX == mon.x && outZ == mon.z);
        } else {
            // Branche "portée de compétence" (0x4583d9..0x458489) : seuil = coût de la
            // compétence active (simple si en posture 2 ET configurée, sinon AoE) +
            // portée d'engagement de la cible COURANTE (this+220) — fidèle : c'est bien
            // la cible déjà verrouillée qui sert de référence, PAS le monstre i examiné.
            uint32_t skillId = config.skillAoE;
            if (PlayerIsInStance(2) && config.skillSingle != 0) skillId = config.skillSingle;
            const int32_t cost = Skill_CostById(static_cast<int>(skillId), g_World.self, g_World.db.item);
            float engageRange = 0.0f;
            if (currentTargetIndex_ >= 0 && static_cast<std::size_t>(currentTargetIndex_) < g_World.monsters.size())
                engageRange = Ext(static_cast<std::size_t>(currentTargetIndex_)).engageRange;
            inRange = dist <= static_cast<float>(cost) + engageRange;
        }

        if (inRange)
            InsertTargetSorted(static_cast<int32_t>(i), dist, ext.aggroOwner != 1);
    }

    return targetCount_ != 0;
}

// ===========================================================================
// AutoPlay_SelectTarget 0x4585E0
// ===========================================================================
int32_t AutoPlaySystem::SelectTarget() {
    if (!externalState.worldReady) return 0; // fidèle : retourne 0 (pas -1) si le monde n'est pas prêt

    const PlayerEntity& self = g_World.Self();
    const float speed = host.GetSelfMoveSpeed ? host.GetSelfMoveSpeed() : 1.0f;

    for (uint16_t i = 0; i < targetCount_; ++i) {
        if (!targets_[i].available) continue; // seuls les slots "libres" sont candidats
        currentTargetIndex_ = targets_[i].monsterIndex;
        if (currentTargetIndex_ < 0) continue;

        bool ok = false;
        const std::size_t idx = static_cast<std::size_t>(currentTargetIndex_);
        if (idx < g_World.monsters.size()) {
            const MonsterEntity& mon = g_World.monsters[idx];
            if (mon.def) {
                const MonsterAutoplayExt& ext = Ext(idx);
                if (ext.state != 0 && ext.state != 12 && mon.active
                    && ReadI32(mon.def, kDefOffMonCat232) <= 3
                    && ReadI32(mon.def, kDefOffMonCat236) <= 1) {
                    float outX = mon.x, outY = self.y, outZ = mon.z;
                    if (host.SlideMove)
                        host.SlideMove(self.x, self.y, self.z, mon.x, self.y, mon.z, speed, kFixedDt, outX, outY, outZ);
                    ok = (outX == mon.x && outZ == mon.z);
                }
            }
        }

        if (ok) return currentTargetIndex_;
        ClearTargetSlot();
    }
    return -1;
}

// ===========================================================================
// AutoPlay_CountTargetsInRange 0x458C10
// ===========================================================================
bool AutoPlaySystem::CountTargetsInRangeAtLeastThreshold() {
    int32_t withinRange = 0;
    const int32_t aoeCost = Skill_CostById(static_cast<int>(config.skillAoE), g_World.self, g_World.db.item);

    for (uint16_t i = 0; i < targetCount_; ++i) {
        const int32_t monsterIndex = targets_[i].monsterIndex;
        if (monsterIndex < 0 || static_cast<std::size_t>(monsterIndex) >= g_World.monsters.size())
            continue;
        const std::size_t idx = static_cast<std::size_t>(monsterIndex);
        const MonsterEntity& mon = g_World.monsters[idx];
        const MonsterAutoplayExt& ext = Ext(idx);
        const bool valid = mon.def && mon.active && ext.state != 0 && ext.state != 12
                            && ReadI32(mon.def, kDefOffMonCat232) <= 3
                            && ReadI32(mon.def, kDefOffMonCat236) <= 1;
        if (!valid) {
            RemoveTargetByMonsterIndex(monsterIndex);
            continue;
        }

        const float dist = DistanceToPlayer(mon.x, mon.y, mon.z);
        if (dist <= static_cast<float>(aoeCost) + ext.engageRange)
            ++withinRange;
        if (withinRange >= config.aoeThreshold)
            return true;
    }
    return false;
}

// ===========================================================================
// AutoPlay_FindWalkableAdjacent 0x4580C0 — sonde les 8 directions cardinales/diagonales
// (ordre exact du switch d'origine) à 10 unités, retourne la première atteignable.
// ===========================================================================
bool AutoPlaySystem::FindWalkableAdjacent(float& outX, float& outY, float& outZ) const {
    if (!externalState.worldReady) return false;

    static constexpr float kDirs[8][2] = {
        {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {-1.0f, 1.0f},
        {-1.0f, 0.0f}, {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f},
    };

    const PlayerEntity& self = g_World.Self();
    const float speed = host.GetSelfMoveSpeed ? host.GetSelfMoveSpeed() : 1.0f;

    for (const auto& d : kDirs) {
        const float tx = d[0] * 10.0f + self.x;
        const float ty = self.y;
        const float tz = d[1] * 10.0f + self.z;
        float rx = tx, ry = ty, rz = tz;
        if (host.SlideMove)
            host.SlideMove(self.x, self.y, self.z, tx, ty, tz, speed, kFixedDt, rx, ry, rz);
        if (rx == tx && rz == tz) {
            outX = tx; outY = ty; outZ = tz;
            return true;
        }
    }
    return false;
}

// ===========================================================================
// AutoPlay_UpdateTargeting 0x45D080
// ===========================================================================
bool AutoPlaySystem::UpdateTargeting(float dt) {
    rebuildTimerSec_ += dt;

    const bool locked = IsTargetLocked(currentTargetIndex_);
    if (rebuildTimerSec_ <= kRebuildIntervalSec || locked) {
        if (locked || SelectTarget() >= 0) {
            // Ordre d'attaque en attente : fidèle à dword_1687354/1687358/1687350 ET leur
            // mirroir dword_1675B28/1675B2C/1675B24 — écrits UNIQUEMENT si l'anim self est
            // au tout début (0 <= frame < 1), comme dans le binaire. dword_1766F7C/1766F78
            // d'origine = EntityId {lo,hi} du monstre ciblé -> exposés via MonsterEntity::id.
            if (g_Client.VarF(0x168732C) >= 0.0f && g_Client.VarF(0x168732C) < 1.0f) {
                const std::size_t idx = static_cast<std::size_t>(currentTargetIndex_);
                if (idx < g_World.monsters.size()) {
                    const EntityId& tid = g_World.monsters[idx].id;
                    g_Client.Var(0x1687354) = static_cast<int32_t>(tid.hi);
                    g_Client.Var(0x1675B28) = static_cast<int32_t>(tid.hi);
                    g_Client.Var(0x1687358) = static_cast<int32_t>(tid.lo);
                    g_Client.Var(0x1675B2C) = static_cast<int32_t>(tid.lo);
                    g_Client.Var(0x1687350) = 5;
                    g_Client.Var(0x1675B24) = 5;
                }
            }
            return true;
        }
        g_Client.Var(0x1687354) = 0;
        g_Client.Var(0x1675B28) = 0;
        g_Client.Var(0x1687358) = -1;
        g_Client.Var(0x1675B2C) = -1;
        g_Client.Var(0x1687350) = 5;
        g_Client.Var(0x1675B24) = 5;
        ResetTargetList();
        return false;
    }

    if (BuildTargetList())
        rebuildTimerSec_ = 0.0f; // fidèle : le timestamp n'est mis à jour qu'en cas de succès
    return false;
}

// ===========================================================================
// AutoPlay_FindNpcTarget 0x458E90
// ===========================================================================
int32_t AutoPlaySystem::FindNpcTarget() const {
    int32_t result = -1;

    for (std::size_t i = 0; i < g_World.npcs.size(); ++i) {
        const NpcEntity& npc = g_World.npcs[i];
        if (!npc.active) continue;
        if (host.ShouldRefreshNpc && !host.ShouldRefreshNpc(npc)) continue;
        if (!npc.def) continue;
        if (ReadI32(npc.def, kDefOffNpcKind) != 1) continue;

        const uint32_t offerWeight = ReadU32(npc.body.data(), kNpcBodyOffOffer);
        if (static_cast<int64_t>(offerWeight) + g_Client.inv.weight > 2000000000LL) {
            result = -1;
            break; // fidèle : abandonne la 1re passe (pas toute la recherche, cf. plus bas)
        }
        if (config.mode != 1) return static_cast<int32_t>(i);

        const float nx = ReadF32(npc.body.data(), kNpcBodyOffPosX);
        const float ny = ReadF32(npc.body.data(), kNpcBodyOffPosY);
        const float nz = ReadF32(npc.body.data(), kNpcBodyOffPosZ);
        if (DistanceToPlayer(nx, ny, nz) < kNpcInteractRange) return static_cast<int32_t>(i);
    }

    if (result != -1) return result;

    // 2e passe : NPC ami (par nom, def+4) ou NPC de faction ciblée par le masque PK
    // (def+184 -> bit) et non ennemi.
    for (std::size_t j = 0; j < g_World.npcs.size(); ++j) {
        const NpcEntity& npc = g_World.npcs[j];
        if (!npc.active) continue;
        if (host.ShouldRefreshNpc && !host.ShouldRefreshNpc(npc)) continue;
        if (!npc.def) continue;

        const char* ownerName = reinterpret_cast<const char*>(npc.def) + kDefOffName;
        const float nx = ReadF32(npc.body.data(), kNpcBodyOffPosX);
        const float ny = ReadF32(npc.body.data(), kNpcBodyOffPosY);
        const float nz = ReadF32(npc.body.data(), kNpcBodyOffPosZ);

        if (IsFriendName(ownerName)) {
            if (config.mode != 1) return static_cast<int32_t>(j);
            if (DistanceToPlayer(nx, ny, nz) < kNpcInteractRange) return static_cast<int32_t>(j);
            continue;
        }

        const int32_t faction = ReadI32(npc.def, kDefOffFaction);
        int32_t bit = 0;
        switch (faction) {
            case 1: bit = 1; break;
            case 2: bit = 2; break;
            case 3: bit = 4; break;
            case 4: bit = 8; break;
            default: bit = 0; break;
        }
        if (!bit) return -1; // fidèle : faction inconnue -> abandon immédiat de TOUTE la recherche

        if ((config.pkFactionMask & static_cast<uint32_t>(bit)) == static_cast<uint32_t>(bit)
            && !IsEnemyName(ownerName)) {
            if (config.mode != 1) return static_cast<int32_t>(j);
            if (DistanceToPlayer(nx, ny, nz) < kNpcInteractRange) return static_cast<int32_t>(j);
        }
    }

    return result;
}

// ===========================================================================
// AutoPlay_MoveToNpc 0x45C5C0
// ===========================================================================
bool AutoPlaySystem::MoveToNpc() {
    const int32_t npcIdx = FindNpcTarget();
    if (npcIdx < 0) return false;

    if (npcInteractCooldownSec_ < kNpcInteractCooldownSec) return true;
    if (externalState.sceneTransitionBlocking) return true; // sub_53B9E0

    const NpcEntity& npc = g_World.npcs[static_cast<std::size_t>(npcIdx)];
    if (!npc.def) return false;

    if (ReadI32(npc.def, kDefOffNpcKind) == 1) {
        if (host.InteractNpc) host.InteractNpc(npc.id);
        return true;
    }

    // NPC "offre de ramassage" : tente de placer l'objet/quantité en sac avant d'interagir.
    const uint32_t offerItemId = ReadU32(npc.body.data(), kNpcBodyOffDefId);
    const uint32_t offerWeight = ReadU32(npc.body.data(), kNpcBodyOffOffer);
    const int32_t placedRow = host.TryPlaceItemIntoBag ? host.TryPlaceItemIntoBag(offerItemId, offerWeight) : -1;
    if (placedRow >= 0) {
        if (host.InteractNpc) host.InteractNpc(npc.id);
        npcInteractCooldownSec_ = 0.0f;
        return true;
    }

    if (config.warpOnStuck) {
        const bool attacking = host.IsSelfAttacking ? host.IsSelfAttacking() : false;
        if (externalState.warpSuppressed || !attacking) return true;
        if (host.WarpToFactionTown) host.WarpToFactionTown();
        // fidèle : pas de retour ici -> tombe jusqu'au `return false` final.
    }
    return false;
}

// ===========================================================================
// AutoPlay_IsMobOfFaction 0x45BE80
// ===========================================================================
bool AutoPlaySystem::IsMobOfFaction(bool secondTier, int32_t monsterDefId) const {
    if (secondTier) {
        static constexpr int32_t kPairs[9][2] = {
            {10, 112}, {14, 113}, {18, 114}, {29, 115}, {33, 116},
            {37, 117}, {48, 118}, {52, 119}, {56, 120},
        };
        for (int32_t c = 0; c < 9; ++c)
            if (PlayerIsCharClass(c) && (monsterDefId == kPairs[c][0] || monsterDefId == kPairs[c][1]))
                return true;
        return false;
    }
    static constexpr int32_t kOctets[9][8] = {
        {8, 9, 58, 59, 85, 86, 121, 122},
        {12, 13, 60, 61, 87, 88, 123, 124},
        {16, 17, 62, 63, 89, 90, 125, 126},
        {27, 28, 64, 65, 91, 92, 127, 128},
        {31, 32, 66, 67, 93, 94, 129, 130},
        {35, 36, 68, 69, 95, 96, 131, 132},
        {46, 47, 70, 71, 97, 98, 133, 134},
        {50, 51, 72, 73, 99, 100, 135, 136},
        {54, 55, 74, 75, 101, 102, 137, 138},
    };
    for (int32_t c = 0; c < 9; ++c) {
        if (!PlayerIsCharClass(c)) continue;
        for (int32_t k = 0; k < 8; ++k)
            if (monsterDefId == kOctets[c][k]) return true;
    }
    return false;
}

// ===========================================================================
// AutoPlay_IsMobCategory2 0x45C2F0
// ===========================================================================
bool AutoPlaySystem::IsMobCategory2(int32_t classId, int32_t monsterDefId) const {
    switch (classId) {
        case 0:
        case 1: {
            if (PlayerIsElementalAffinity(0) && monsterDefId == 7) return true;
            if (PlayerIsElementalAffinity(1) && monsterDefId == 26) return true;
            if (PlayerIsElementalAffinity(2) && monsterDefId == 45) return true;
            static constexpr int32_t kClassIds[9] = {11, 15, 19, 30, 34, 38, 49, 53, 57};
            for (int32_t c = 0; c < 9; ++c)
                if (PlayerIsCharClass(c) && monsterDefId == kClassIds[c]) return true;
            return false;
        }
        case 2: case 3: case 4: case 5: case 6: case 7:
            return monsterDefId == 82 || monsterDefId == 83 || monsterDefId == 84
                || monsterDefId == 103 || monsterDefId == 104 || monsterDefId == 105;
        default:
            return false;
    }
}

// ===========================================================================
// Player_IsCharClass 0x45C550 / Player_IsInStance 0x45C480 / sub_45C590 0x45C590 —
// tous les trois lisent l'ITEM_INFO de externalState.classItemId (dword_1673248 dans le
// binaire), champ typeCode (+188, cf. GameDatabase.h::ItemInfo).
// ===========================================================================
bool AutoPlaySystem::PlayerIsCharClass(int32_t classIdx) const {
    const ItemInfo* info = GetItemInfo(externalState.classItemId);
    return info && info->typeCode == static_cast<uint32_t>(classIdx + 13);
}

bool AutoPlaySystem::PlayerIsInStance(int32_t stance) const {
    const ItemInfo* info = GetItemInfo(externalState.classItemId);
    if (!info) return false;
    const uint32_t t = info->typeCode;
    if (stance == 0) return t == 13 || t == 16 || t == 19;
    if (stance == 1) return t == 14 || t == 17 || t == 20;
    if (stance == 2) return t == 15 || t == 18 || t == 21;
    return false;
}

bool AutoPlaySystem::PlayerIsElementalAffinity(int32_t elementIdx) const {
    return g_World.self.element == elementIdx || g_World.self.elementSecondary == elementIdx;
}

// ===========================================================================
// AutoPlay_IsFriend 0x45FAA0 / AutoPlay_IsEnemy 0x45FBE0 — recherche linéaire par égalité
// de chaîne (Crt_Strcmp) dans les listes chaînées this+296/this+324 de l'original,
// reproduites en std::vector<std::string>.
// ===========================================================================
bool AutoPlaySystem::IsFriendName(const char* name) const {
    if (!name) return false;
    for (const auto& n : friendNames)
        if (n == name) return true;
    return false;
}
bool AutoPlaySystem::IsEnemyName(const char* name) const {
    if (!name) return false;
    for (const auto& n : enemyNames)
        if (n == name) return true;
    return false;
}

// ===========================================================================
// AutoPlay_HasRequiredItems 0x45CC10 — cherche dans la grille de ramassage (3 conteneurs
// x 14 slots, g_EntityManager.PickupSlot) PUIS dans l'inventaire principal (g_Client.inv)
// deux catégories de matériaux ; s'arrête dès que les deux sont trouvées.
// ===========================================================================
namespace {
void ClassifyMaterial(uint32_t itemId, bool& hasA, bool& hasB) {
    switch (itemId) {
        case 32: case 33: case 34: hasA = true; hasB = true; break;
        case 26: case 27: case 28: case 29: case 30: case 31: hasB = true; break;
        case 2: case 3: case 4: case 23: case 24: case 25: hasA = true; break;
        default: break;
    }
}
} // namespace

bool AutoPlaySystem::HasRequiredItems() const {
    bool hasA = false, hasB = false;

    for (uint32_t container = 0; container < 3 && !(hasA && hasB); ++container) {
        for (uint32_t slot = 0; slot < EntityManager::kSlotsPerContainer && !(hasA && hasB); ++slot) {
            const GroundPickupSlot* s = g_EntityManager.PickupSlot(container, slot);
            if (s && s->aux == 3)
                ClassifyMaterial(s->itemId, hasA, hasB);
        }
    }
    if (hasA && hasB) return true;

    const int32_t pages = (externalState.invExtraPageCount > 0) ? 2 : 1;
    for (int32_t page = 0; page < pages && !(hasA && hasB); ++page) {
        for (uint32_t col = 0; col < InventoryState::kCols && !(hasA && hasB); ++col) {
            ClassifyMaterial(g_Client.inv.cells[static_cast<uint32_t>(page) * InventoryState::kCols + col].itemId,
                              hasA, hasB);
        }
    }
    return hasA && hasB;
}

// ===========================================================================
// Corps commun à AutoPlay_CheckReturnScroll 0x45C750 / AutoPlay_CheckTownScroll 0x45C9B0.
// ===========================================================================
bool AutoPlaySystem::CheckConsumableScroll(uint32_t itemId, int strTableId, bool& enabledToggle) {
    // 1) grille de ramassage rapide : 3 conteneurs x 14 slots (dword_1674404==3 ==
    //    slot occupé -> GroundPickupSlot::aux==3, cf. EntityManager.h).
    for (uint32_t container = 0; container < 3; ++container) {
        for (uint32_t slot = 0; slot < EntityManager::kSlotsPerContainer; ++slot) {
            const GroundPickupSlot* s = g_EntityManager.PickupSlot(container, slot);
            if (!s || s->aux != 3 || s->itemId != itemId || s->count == 0) continue;

            if (externalState.morphInProgress) return true;      // dword_1675A88 == 1
            if (pendingItemUseLatch_) return true;                // dword_1675B08 déjà armé

            pendingItemUseContainer_ = static_cast<int32_t>(container);
            pendingItemUseSlot_ = static_cast<int32_t>(slot);
            if (host.SendUseGroundPickupItem && host.SendUseGroundPickupItem(pendingItemUseContainer_, pendingItemUseSlot_)) {
                pendingItemUseLatch_ = true;
                pendingItemUseTimeSec_ = g_World.gameTimeSec;
            }
            return true;
        }
    }

    // 2) absent de la grille de ramassage : déjà dans l'inventaire principal ?
    const int32_t pages = (externalState.invExtraPageCount > 0) ? 2 : 1;
    for (int32_t page = 0; page < pages; ++page) {
        for (uint32_t col = 0; col < InventoryState::kCols; ++col) {
            if (g_Client.inv.cells[static_cast<uint32_t>(page) * InventoryState::kCols + col].itemId == itemId)
                return false; // présent -> rien à faire cette frame
        }
    }

    // 3) introuvable partout : message système, désactive le toggle, notifie le serveur.
    g_Client.msg.System(Str(strTableId));
    enabledToggle = false;
    if (host.NotifyInventoryDirty) host.NotifyInventoryDirty(externalState.invDirtyEnable);
    return false;
}

// ===========================================================================
// AutoPlay_CheckReturnScroll 0x45C750 (item 1001)
// ===========================================================================
bool AutoPlaySystem::CheckReturnScroll() {
    const ItemInfo* selInfo = GetItemInfo(externalState.selectedInvItemId);
    if (!selInfo) return false;
    if (selInfo->typeCode == 28 || selInfo->typeCode == 31 || selInfo->typeCode == 32) return false;

    if (!config.useReturnScroll) return false;
    if (static_cast<int32_t>(externalState.selectedInvItemId) <= 0) return false;
    if (externalState.selectedInvCounter >= 50) return false;

    return CheckConsumableScroll(1001, /*StrTable005*/ 1793, config.useReturnScroll);
}

// ===========================================================================
// AutoPlay_CheckTownScroll 0x45C9B0 (item 563)
// ===========================================================================
bool AutoPlaySystem::CheckTownScroll() {
    if (!config.useTownItem) return false;
    if (externalState.talismanSlot <= 9 || externalState.talismanSlot >= 20) return false;

    int32_t hi = 0, lo = 0;
    const std::size_t slot = static_cast<std::size_t>(externalState.talismanSlot);
    UnpackCombined(slot < externalState.talismanPacked.size() ? externalState.talismanPacked[slot] : 0, hi, lo);
    if (hi > 50) return false;

    return CheckConsumableScroll(563, /*StrTable005*/ 2185, config.useTownItem);
}

// ===========================================================================
// Ext() — extension par-monstre, redimensionnée à la volée.
// ===========================================================================
MonsterAutoplayExt& AutoPlaySystem::Ext(std::size_t monsterIndex) {
    if (monsterIndex >= ext_.size()) ext_.resize(monsterIndex + 1);
    return ext_[monsterIndex];
}

// ===========================================================================
// Update(dt) — orchestration absente du cluster d'origine (les fonctions ci-dessus sont
// appelées séparément par la boucle de jeu d'origine) : enchaîne un pas de farming
// complet par frame, dans l'ordre logique ciblage -> consommables auto.
// ===========================================================================
void AutoPlaySystem::Update(float dt) {
    npcInteractCooldownSec_ += dt;
    UpdateTargeting(dt);
    CheckReturnScroll();
    CheckTownScroll();
}

} // namespace ts2::game

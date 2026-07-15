// Game/AutoPlaySystem.h — Système d'autoplay (farming automatique) de TwelveSky2.
//
// Réécriture C++ fidèle du cluster IDA "AutoPlay_*" (EA 0x457EA0..0x45D080), qui est la
// MACHINE D'ÉTAT de ciblage/farming automatique du client d'origine (objet "this" alloué
// quelque part dans le contexte joueur, jamais nommé explicitement dans l'IDB — seule sa
// forme est connue via AutoPlay_Construct). Elle gère :
//   - une liste de 15 cibles-monstres triée par distance (AutoPlay_*TargetList/Sorted/Slot),
//   - la sélection/verrouillage de la cible courante et l'émission de l'ordre d'attaque,
//   - la recherche/interaction avec un NPC (ramassage de récompense, vendeur, ami/ennemi PK),
//   - l'utilisation automatique de parchemins retour/ville,
//   - la classification faction/catégorie d'un monstre pour le PK automatique.
//
// Fonctions d'origine couvertes (EA -> méthode) :
//   AutoPlay_Construct              0x457EA0 -> AutoPlaySystem::AutoPlaySystem()
//   AutoPlay_BuildTargetList        0x458280 -> BuildTargetList()
//   AutoPlay_SelectTarget           0x4585E0 -> SelectTarget()
//   AutoPlay_InsertTargetSorted     0x458870 -> InsertTargetSorted()
//   AutoPlay_DistanceToPlayer       0x4589E0 -> DistanceToPlayer()
//   AutoPlay_IsTargetLocked         0x458B80 -> IsTargetLocked()
//   AutoPlay_CountTargetsInRange    0x458C10 -> CountTargetsInRangeAtLeastThreshold()
//   AutoPlay_RemoveTargetById       0x458E00 -> RemoveTargetByMonsterIndex()
//   AutoPlay_FindNpcTarget          0x458E90 -> FindNpcTarget()
//   AutoPlay_FindWalkableAdjacent   0x4580C0 -> FindWalkableAdjacent()
//   AutoPlay_IsMobOfFaction         0x45BE80 -> IsMobOfFaction()
//   AutoPlay_IsMobCategory2         0x45C2F0 -> IsMobCategory2()
//   AutoPlay_MoveToNpc              0x45C5C0 -> MoveToNpc()
//   AutoPlay_CheckReturnScroll      0x45C750 -> CheckReturnScroll()
//   AutoPlay_CheckTownScroll        0x45C9B0 -> CheckTownScroll()
//   AutoPlay_HasRequiredItems       0x45CC10 -> HasRequiredItems()
//   AutoPlay_UpdateTargeting        0x45D080 -> UpdateTargeting()
//   AutoPlay_ClearTargetSlot        0x4587E0 -> ClearTargetSlot()
//   AutoPlay_ResetTargetList        0x458AB0 -> ResetTargetList()
// + petits utilitaires appelés par le cluster (traduits car indispensables) :
//   Player_IsCharClass  0x45C550, Player_IsInStance 0x45C480, sub_45C590 (affinité élément),
//   AutoPlay_IsFriend 0x45FAA0, AutoPlay_IsEnemy 0x45FBE0, Stat_UnpackCombined 0x54CE40.
//
// PÉRIMÈTRE : la machine d'état de ciblage/farming, PAS le rendu des panneaux UI autoplay
// (hors périmètre, cf. TODO ci-dessous). Les sous-systèmes tiers (collision de déplacement,
// réseau, placement d'objet en sac, listes ami/ennemi de la vraie UI sociale, classe/stance
// du joueur local au sens large) ne sont PAS modélisés dans Game/GameState.h ni les autres
// en-têtes partagés : ils sont exposés via des points d'intégration explicites (host/
// externalState ci-dessous), documentés avec leur EA d'origine, à brancher par l'appelant.
//
// Autonomie : ce module n'inclut QUE les en-têtes partagés listés dans la mission
// (GameState.h, GameDatabase.h, ClientRuntime.h, SkillSystem.h, EntityManager.h) + la STL.
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <string>
#include <functional>

#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/ClientRuntime.h"
#include "Game/SkillSystem.h"
#include "Game/EntityManager.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Réglages utilisateur du farming auto (globals g_AutoHunt* de l'original).
// ---------------------------------------------------------------------------
struct AutoPlayConfig {
    // g_AutoHuntMode 0x16755F4. 1 = mode "portée de compétence" : BuildTargetList élargit
    // la liste autour de la cible DÉJÀ verrouillée via le coût de compétence au lieu de
    // tester l'accessibilité par glissement de collision (cf. BuildTargetList()). Toute
    // autre valeur (0 observé par défaut, 2 utilisé ailleurs dans le binaire) suit la
    // branche "accessibilité". Piloté aussi par FindNpcTarget/MoveToNpc (filtre 50 unités).
    int32_t  mode = 0;
    uint32_t skillSingle = 0;   // g_AutoHuntSkillSingle    0x16755F8 (0 = non configuré)
    uint32_t skillAoE    = 0;   // g_AutoHuntSkillAoE       0x1675600
    int32_t  aoeThreshold = 0;  // g_AutoHuntAoEThreshold   0x1675608 (nb cibles pour lancer l'AoE)
    bool     useReturnScroll = false; // g_AutoHuntUseReturnScroll 0x1675618
    bool     useTownItem     = false; // g_AutoHuntUseTownItem     0x167561C
    uint32_t pkFactionMask   = 0;     // g_AutoHuntPkFactionMask   0x167560C (bits 1/2/4/8 = factions 1..4)
    // g_AutoHuntBagFullReturn 0x1675610 (nom réel confirmé : xrefs AutoPlay_DrawSettingsPanel
    // 0x4593C0 / AutoPlay_OnClickSettings 0x45A0D0, panneau de réglages AutoPlay ; accédé aussi
    // via `(&g_AutoHuntMode)[7]` dans MoveToNpc). Sémantique réelle CONFIRMÉE par le panneau de
    // réglages d'origine : « retour ville si sac plein » — actif quand TryPlaceItemIntoBag
    // échoue (ramassage NPC bloqué par manque de place) ET que le joueur attaque, warp vers la
    // ville de faction. Le nom warpOnStuck ci-dessous décrit l'EFFET (warp), g_AutoHuntBagFullReturn
    // décrit la CAUSE réelle telle qu'affichée dans le panneau (case à cocher dédiée, 2 sprites
    // toggle à x+131/x+205,y+257) — conservé tel quel pour ne pas casser les appelants existants.
    bool     warpOnStuck = false;
};

// ---------------------------------------------------------------------------
// État/globals externes non modélisés dans les en-têtes partagés (longue traîne). Valeurs
// par défaut choisies pour que le module reste fonctionnel sans branchement complet ;
// à synchroniser par l'appelant (World/Net/UI) quand ces systèmes existeront.
// ---------------------------------------------------------------------------
struct AutoPlayExternalState {
    bool worldReady = true;               // dword_14A88E8 (monde/collision prêt)
    bool sceneTransitionBlocking = false; // sub_53B9E0 0x53B9E0 : true ssi dword_1822390==1 ET dword_1822388==1
    bool warpSuppressed = false;          // dword_1675B00
    bool morphInProgress = false;         // dword_1675A88 (nom IDB : g_MorphInProgress)
    bool invDirtyEnable = true;           // g_InvDirtyEnable 0x16755AC (argument de Net_SendOp99)

    uint32_t selectedInvItemId = 0;   // g_SelectedInvItemId 0x1673258 (sélection UI inventaire)
    int32_t  selectedInvCounter = 0;  // dword_167325C (compteur associé, sémantique non prouvée ici)
    uint32_t classItemId = 0;         // dword_1673248 : item "cœur de classe" équipé, lu par
                                       // Player_IsCharClass/Player_IsInStance (0x45C550/0x45C480).
    int32_t  invExtraPageCount = 0;   // g_Inv_ExtraPageCount 0x16732A8 (0 => 1 page scannée, >0 => 2)

    int32_t talismanSlot = 0;                    // g_TalismanSlot 0x1674760 (cf. neutralisation StatFormulas.h)
    std::array<int32_t, 20> talismanPacked{};    // dword_1675664[0..19] (valeurs combinées packées)
};

// ---------------------------------------------------------------------------
// Un slot de la liste de cibles (15 max, offsets +36..+215 de l'objet d'origine).
// monsterIndex référence g_World.monsters (index, PAS l'identité réseau).
// ---------------------------------------------------------------------------
struct AutoPlayTargetSlot {
    int32_t monsterIndex = -1; // -1 = libre (le binaire écrit NaN/-1 selon le site — normalisé ici)
    float   distance = 0.0f;
    bool    available = false; // dword_176703C[idx] != 1 au moment de l'insertion (cf. MonsterAutoplayExt)
};

// ---------------------------------------------------------------------------
// Champs par-monstre du tableau runtime d'origine (stride 280 o, base dword_1766F74) qui
// NE SONT PAS couverts par MonsterEntity (Game/GameState.h) : ils sont peuplés ailleurs
// dans le binaire (hors du cluster AutoPlay_* étudié ici, provenance non identifiée dans
// ces ~19 fonctions) — probablement par un handler d'état/aggro monstre non porté dans
// EntityManager. Indexé comme g_World.monsters (même index). Défauts choisis pour ne
// jamais bloquer le ciblage tant que rien ne les alimente ; setters au fil de l'eau via
// Ext() pour un branchement futur.
// ---------------------------------------------------------------------------
struct MonsterAutoplayExt {
    int32_t state = 1;          // dword_1766F8C d'origine : 0 = invalide, 12 = mort -> exclu du ciblage.
    int32_t aggroOwner = 0;     // dword_176703C d'origine : ==1 => déjà engagé par un tiers.
    float   engageRange = 0.0f; // unk_1766FD8 d'origine : allonge la portée de sélection/AoE du monstre.
};

// ---------------------------------------------------------------------------
// Points d'intégration délibérément hors périmètre de ce cluster : collision de
// déplacement, réseau, UI d'inventaire, rafraîchissement NPC, état d'action du joueur.
// Callbacks optionnels (nullptr = comportement par défaut documenté au site d'appel).
// EA d'origine citées pour le branchement réel.
// ---------------------------------------------------------------------------
struct AutoPlayHost {
    // MapColl_SlideMoveGround 0x697330 : tente un déplacement glissé de (x,y,z) vers
    // (toX,toY,toZ) à `speed` sur `dt` secondes ; écrit la position réellement atteinte
    // dans (outX,outY,outZ). L'appelant compare lui-même au point visé (== -> non bloqué).
    std::function<void(float x, float y, float z, float toX, float toY, float toZ,
                        float speed, float dt, float& outX, float& outY, float& outZ)> SlideMove;

    // Char_CalcAttackSpeed 0x4CCAB0 (&g_EquipSnapshotScratch dans le binaire — dépend du
    // moteur de stats StatFormulas.h, volontairement NON inclus ici pour garder ce module
    // autonome dans les en-têtes listés par la mission). Vitesse utilisée comme paramètre
    // `speed` de SlideMove. Défaut si non branché : 1.0f.
    std::function<float()> GetSelfMoveSpeed;

    // Npc_Interact 0x53A660 (idHi, idLo du NPC).
    std::function<void(EntityId npcId)> InteractNpc;

    // cGameHud_PlaceItemIntoBag 0x650470 : vérifie/effectue le placement de `itemId`
    // (quantité/poids = weight) dans le sac. Retour >= 0 = slot obtenu (succès) ; -1 = échec.
    std::function<int(uint32_t itemId, uint32_t weight)> TryPlaceItemIntoBag;

    // Net_SendPacket_Op22 0x4B5300 : demande d'utilisation d'un objet de la grille de
    // ramassage rapide (conteneur, slot). Retour = succès d'émission du paquet.
    std::function<bool(int container, int slot)> SendUseGroundPickupItem;

    // Net_SendOp99 0x4BD140 (argument g_InvDirtyEnable) : notifie le serveur d'un
    // rafraîchissement d'inventaire après échec définitif d'un parchemin auto.
    std::function<void(bool invDirtyEnable)> NotifyInventoryDirty;

    // Map_BeginWarpToFactionTownEx 0x55C9A0 (argument 0).
    std::function<void()> WarpToFactionTown;

    // Char_IsAttackAction 0x558A50 (&g_LocalPlayerSheet) : vrai si l'animation en cours
    // du joueur local est une action d'attaque.
    std::function<bool()> IsSelfAttacking;

    // maybe_Npc_ShouldRefreshTarget 0x583E20 : éligibilité de rafraîchissement du NPC
    // (probable anti-spam/minuterie, hors périmètre du cluster AutoPlay_*). Défaut (non
    // branché) : toujours éligible.
    std::function<bool(const NpcEntity&)> ShouldRefreshNpc;
};

// ---------------------------------------------------------------------------
// AutoPlaySystem — machine d'état de farming automatique (mirroir de l'objet AutoPlay_*
// d'origine, ~332 o). Une instance par joueur local (usage typique : un singleton, comme
// le reste de ts2::game — laissé au soin de l'appelant, pas de global imposé ici).
// ---------------------------------------------------------------------------
class AutoPlaySystem {
public:
    AutoPlaySystem(); // AutoPlay_Construct 0x457EA0

    AutoPlayConfig         config;
    AutoPlayExternalState  externalState;
    AutoPlayHost           host;

    // Listes de noms ami/ennemi (List_Construct(this+296)/(this+324) de l'original —
    // AutoPlay_IsFriend 0x45FAA0 / AutoPlay_IsEnemy 0x45FBE0 en faisaient une recherche
    // linéaire par égalité de chaîne ; std::vector<std::string> reproduit fidèlement).
    std::vector<std::string> friendNames;
    std::vector<std::string> enemyNames;
    bool IsFriendName(const char* name) const; // AutoPlay_IsFriend  0x45FAA0
    bool IsEnemyName(const char* name)  const; // AutoPlay_IsEnemy   0x45FBE0

    // ---- Machine de ciblage --------------------------------------------------
    bool    BuildTargetList();                                    // 0x458280
    int32_t SelectTarget();                                       // 0x4585E0
    void    InsertTargetSorted(int32_t monsterIndex, float distance, bool available); // 0x458870
    bool    IsTargetLocked(int32_t monsterIndex) const;            // 0x458B80
    bool    CountTargetsInRangeAtLeastThreshold();                 // 0x458C10
    void    RemoveTargetByMonsterIndex(int32_t monsterIndex);      // 0x458E00
    void    ClearTargetSlot();                                     // 0x4587E0
    void    ResetTargetList();                                     // 0x458AB0
    bool    UpdateTargeting(float dt);                             // 0x45D080 (dt cumulé, cf. .cpp)

    // ---- Géométrie / déplacement ----------------------------------------------
    // Distance JOUEUR<->point. NB fidèle : la composante Y est calculée dans le binaire
    // d'origine (0x458a5a) mais jamais additionnée au total — distance 2D (X,Z) reproduite
    // telle quelle (bug/particularité de l'original, pas une approximation de notre part).
    static float DistanceToPlayer(float x, float y, float z);      // 0x4589E0
    bool FindWalkableAdjacent(float& outX, float& outY, float& outZ) const; // 0x4580C0

    // ---- NPC --------------------------------------------------------------------
    int32_t FindNpcTarget() const; // 0x458E90
    bool    MoveToNpc();           // 0x45C5C0

    // ---- Classification de monstre (PK auto) -----------------------------------
    // `secondTier` = paramètre a1 d'origine (false/0 -> table des ids "1er palier",
    // true/1 -> table "2e palier"). `monsterDefId` = champ d'identification du monstre
    // (dword_1766F84 / equivalent — PAS l'index de tableau).
    bool IsMobOfFaction(bool secondTier, int32_t monsterDefId) const;  // 0x45BE80
    bool IsMobCategory2(int32_t classId, int32_t monsterDefId) const;  // 0x45C2F0

    // ---- Objets consommables auto -------------------------------------------------
    bool CheckReturnScroll();     // 0x45C750 (item 1001)
    bool CheckTownScroll();       // 0x45C9B0 (item 563)
    bool HasRequiredItems() const; // 0x45CC10

    // ---- Orchestration --------------------------------------------------------
    // Absent du cluster d'origine tel quel : enchaîne un pas de farming complet par
    // frame (ciblage puis auto-consommables), comme le ferait la boucle de jeu appelant
    // ces fonctions séparément. dt en secondes (remplace GetTickCount() par un
    // accumulateur — mêmes seuils : 1000 ms -> 1.0f, 50 ms -> 0.05f, cf. .cpp).
    void Update(float dt);

    // Accès lecture (diagnostic / UI — le rendu du panneau autoplay reste hors périmètre,
    // TODO EA du renderer non identifiée dans ce cluster).
    const std::array<AutoPlayTargetSlot, 15>& Targets() const { return targets_; }
    uint16_t TargetCount() const { return targetCount_; }
    int32_t  CurrentTargetIndex() const { return currentTargetIndex_; }

    // Extension par-monstre (cf. MonsterAutoplayExt) — accès pour un futur handler d'état.
    MonsterAutoplayExt& Ext(std::size_t monsterIndex);

private:
    std::array<AutoPlayTargetSlot, 15> targets_{};
    uint16_t targetCount_ = 0;          // +216
    int32_t  currentTargetIndex_ = -1;  // +220, -1 = aucune cible verrouillée
    std::vector<MonsterAutoplayExt> ext_;

    // Timers dt-driven (remplacent GetTickCount() — cf. commentaire Update()).
    float rebuildTimerSec_ = 0.0f;          // +228 (rebuild toutes les 1000 ms)
    float npcInteractCooldownSec_ = 0.0f;   // +240 (cooldown interaction NPC 50 ms)

    // État partagé entre CheckReturnScroll et CheckTownScroll (dword_1675B08/1675B1C/
    // 1675B20/flt_1675B0C de l'original — UN SEUL parchemin en vol à la fois, quel que
    // soit son type, exactement comme le binaire).
    bool     pendingItemUseLatch_ = false;
    int32_t  pendingItemUseContainer_ = -1;
    int32_t  pendingItemUseSlot_ = -1;
    float    pendingItemUseTimeSec_ = 0.0f;

    // Utilitaires internes traduits car indispensables au cluster (EA en commentaire).
    bool PlayerIsCharClass(int32_t classIdx) const;      // Player_IsCharClass 0x45C550
    bool PlayerIsInStance(int32_t stance) const;         // Player_IsInStance  0x45C480
    bool PlayerIsElementalAffinity(int32_t elementIdx) const; // sub_45C590    0x45C590

    // Factorise le corps commun à CheckReturnScroll/CheckTownScroll (même structure dans
    // le binaire, seuls l'id d'objet, le message et le drapeau à désactiver diffèrent).
    // `itemId` = 1001 ou 563 ; `strTableId` = id StrTable005 du message "aucun parchemin"
    // (1793 / 2185) ; `enabledToggle` = config.useReturnScroll ou config.useTownItem.
    bool CheckConsumableScroll(uint32_t itemId, int strTableId, bool& enabledToggle);
};

} // namespace ts2::game

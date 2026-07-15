// Game/NpcInteraction.h — Système d'interaction PNJ de TwelveSky2 (ts2::game).
//
// Réécriture C++ fidèle (traduction réelle du désassemblage, pas d'invention) du petit
// cluster "Npc_*" qui gère : la sélection du PNJ interactible le plus proche, l'auto-
// interaction (portée/cooldown), et la classification PNJ (spécial / cible de quête /
// couleur de plaque de nom). Alimente les hooks NPC actuellement stubés de
// Game/AutoPlaySystem.h (host.InteractNpc -> Npc_Interact, host.ShouldRefreshNpc ->
// équivalent de maybe_Npc_ShouldRefreshTarget) et la résolution de cible de quête de
// Game/QuestSystem.h (IsQuestTarget peut nourrir la définition d'un objectif "aller
// parler à/tuer une entité de telle catégorie").
//
// Fonctions d'origine traduites (EA -> fonction/méthode) :
//   Npc_Interact             0x53A660 -> NpcInteractionSystem::Interact()
//   Npc_AutoInteract         0x53A980 -> NpcInteractionSystem::AutoInteractCurrentTarget()
//   Npc_AutoSelectNearest    0x53ABC0 -> NpcInteractionSystem::AutoSelectNearestInteractable()
//   Npc_AutoInteractForPet   0x53B5F0 -> NpcInteractionSystem::AutoInteractForPet()
//   Npc_IsQuestTarget        0x540340 -> Npc_IsQuestTarget()          (fonction pure)
//   Npc_GetNameplateColor    0x540790 -> Npc_GetNameplateColor()      (fonction pure)
//   Npc_IsSpecialType        0x54EE60 -> Npc_IsSpecialType()          (fonction pure)
// Callees indispensables à la fidélité, traduits en interne (pas de dépendance externe) :
//   Math_Dist2D_XZ           0x53FA40 -> DistanceXZ()   (distance PNJ<->joueur plan XZ)
//   Math_Dist3D              0x53FAA0 -> Distance3D()   (distance PNJ<->joueur 3D)
//   Level_ToAggroValue       0x53F700 -> Npc_LevelToAggroValue() (table complète 100..157)
//
// ---------------------------------------------------------------------------------------
// PROVENANCE DES CHAMPS PNJ (important) : les 4 fonctions d'action lisent un tableau
// RUNTIME (base dword_17AB534, pas dans l'IDB tel quel — stride 38 dwords/152 o), DIFFÉRENT
// du payload réseau brut modélisé par NpcEntity::body (84 o, cf. Game/GameState.h). Champs
// identifiés par arithmétique d'adresse entre les symboles voisins de ce tableau :
//   +0   actif           (dword_17AB534[38*i])
//   +4   EntityId.hi      (dword_17AB538[38*i])
//   +8   EntityId.lo      (dword_17AB53C[38*i])
//   +16  itemId "offre"   (dword_17AB544[38*i])  — objet géré/remis par le PNJ
//   +20  poids "offre"    (dword_17AB548[38*i])
//   +100 ptr définition   (dword_17AB598[38*i])  — champs +184/+188 lus par Npc_Interact et
//        consorts ; MÊME convention que Game/AutoPlaySystem.cpp (kDefOffFaction=184,
//        kDefOffNpcKind=188 sur NpcEntity::def) → on réutilise donc NpcEntity::def pour ce
//        pointeur, cohérent avec le reste du code déjà écrit.
//   +128 position (x,y,z) (flt_17AB5B4[38*i])
// Ces 3 derniers groupes (itemId/poids d'offre, position) sont ABSENTS de NpcEntity (ni body
// wire ni def ne les portent) → modélisés ici via NpcInteractionExt (tableau parallèle à
// g_World.npcs, même index, à l'image de AutoPlaySystem::MonsterAutoplayExt). NOTE fidélité :
// ceci diffère du choix (non prouvé, cf. commentaires "unk_17AB554"/"déduits") fait dans
// AutoPlaySystem.cpp de lire la position depuis NpcEntity::body+16 — l'arithmétique
// d'adresse directe sur Npc_Interact ci-dessus est la source la plus fiable dont on dispose
// ici ; à réconcilier lors d'un futur portage du vrai spawn NPC (TODO intégration).
//
// Npc_IsQuestTarget/Npc_GetNameplateColor lisent un pointeur "a1+96" avec des champs à
// +232/+236 (sélection de catégorie) et, pour la couleur, +252/+260/+352 (hauteur de modèle,
// seuil d'aggro/niveau). NOTE FIDÉLITÉ IMPORTANTE : l'offset +96 correspond EXACTEMENT à
// MonsterEntity::def (documenté "+0x60" = 96 dans Game/GameState.h), alors que NpcEntity::def
// est théoriquement à +100 (cf. ci-dessus) — ces 2 fonctions opèrent donc très probablement
// sur le tableau MONSTRE d'origine malgré leur nom IDB "Npc_*" (précédent déjà rencontré dans
// QuestSystem.h : "Pkt_SmithUpgradeResult... mal nommé"). Conformément à la mission ("Opère
// sur game::g_World.npcs"), on les expose ici de façon GÉNÉRIQUE sur un pointeur de record
// (compatible NpcEntity::def ET MonsterEntity::def, tous deux "const void*" résolus) plutôt
// que de figer un type d'entité — l'appelant choisit la source.
//
// RÈGLE : ce fichier n'édite AUCUN header existant. Inclut Game/GameState.h (NpcEntity,
// EntityId, GameWorld), Game/ClientRuntime.h (g_Client.msg/inv, Str()) et Game/QuestSystem.h
// (Quest_SumExceeds2Billion, réutilisé tel quel — même formule que Util_SumExceeds2Billion
// 0x53F660 utilisée par TOUTES les fonctions de ce fichier).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <array>
#include <vector>
#include <functional>

#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/QuestSystem.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Constantes fidèles (littéraux du binaire).
// ---------------------------------------------------------------------------
constexpr float kNpcInteractRange = 50.0f; // seuil <= utilisé par les 4 fonctions d'action
                                            // (Math_Dist2D_XZ/Math_Dist3D <= 50.0)

// Offsets dans le record pointé par NpcEntity::def (cf. bandeau ci-dessus). Réutilise EXACTEMENT
// les noms/valeurs déjà établis dans Game/AutoPlaySystem.cpp (kDefOffFaction/kDefOffNpcKind)
// pour rester cohérent avec le reste du portage ; +252/+260/+352 sont propres à ce fichier
// (Npc_GetNameplateColor / Char_DrawOverheadName 0x581440).
constexpr std::size_t kNpcDefOffFaction     = 184; // dword_17AB598[i]+184 (== AutoPlaySystem kDefOffFaction)
constexpr std::size_t kNpcDefOffKind        = 188; // dword_17AB598[i]+188 (== AutoPlaySystem kDefOffNpcKind ; ==1 -> "vendeur"/vault direct)
constexpr std::size_t kNpcDefOffQuestCatA   = 232; // *(def+232) — catégorie objectif (Npc_IsQuestTarget/GetNameplateColor)
constexpr std::size_t kNpcDefOffQuestCatB   = 236; // *(def+236) — sous-catégorie (branche categorie==1)
constexpr std::size_t kNpcDefOffAggroLevel  = 352; // *(def+352) — comparé à Level_ToAggroValue(niveau local)

// ---------------------------------------------------------------------------
// Lecture LE brute d'un dword dans un record opaque (même convention que les helpers
// anonymes de AutoPlaySystem.cpp — redéclarés ici localement, non exportés là-bas).
// ---------------------------------------------------------------------------
inline uint32_t NpcDefReadU32(const void* def, std::size_t offset) {
    if (!def) return 0;
    uint32_t v = 0;
    std::memcpy(&v, static_cast<const uint8_t*>(def) + offset, sizeof(v));
    return v;
}
inline int32_t NpcDefReadI32(const void* def, std::size_t offset) {
    return static_cast<int32_t>(NpcDefReadU32(def, offset));
}

// ---------------------------------------------------------------------------
// Champs runtime absents de NpcEntity (cf. bandeau ci-dessus) — tableau parallèle à
// g_World.npcs, même index (à l'image de AutoPlaySystem::MonsterAutoplayExt). À peupler par
// le (futur) portage du spawn/update NPC ; défauts sûrs (0) tant que rien ne les alimente.
// ---------------------------------------------------------------------------
struct NpcInteractionExt {
    float    x = 0.0f, y = 0.0f, z = 0.0f; // flt_17AB5B4[38*i] (+128 origine) : position monde
    uint32_t offerItemId = 0;              // dword_17AB544[38*i] (+16) : objet géré/remis par le PNJ
    uint32_t offerWeight = 0;              // dword_17AB548[38*i] (+20) : poids/quantité associée
};

// ---------------------------------------------------------------------------
// Contexte "élément local" nécessaire à Npc_IsQuestTarget/Npc_GetNameplateColor (reflète des
// blocs de g_LocalPlayerSheet 0x1685748 non couverts par SelfState, cf. Game/GameState.h :
// g_ElementLoadout 0x1685E14..+0x1C = loadout[0..3], et Char_GetPairedElement 0x557C00 qui
// cherche `element` dans les paires alliance[2] du même g_LocalPlayerSheet, +0x71C/+0x728).
// Données pures (pas de comportement caché) : l'appelant les peuple depuis le futur portage
// de g_LocalPlayerSheet ; défauts (tout à 0, pairedElement absent -> -1) = "aucun élément",
// fidèle au fallback d'origine.
// ---------------------------------------------------------------------------
struct NpcQuestContext {
    int localElement = 0;                 // = g_World.self.element (g_LocalElement 0x1673194)
    std::array<int, 4> elementLoadout{};   // g_ElementLoadout..+0x1C (loadout[0..3] ; loadout[4] jamais lu par ces 2 fonctions)
    int factionFlag = 0;                   // dword_1687320[0] (indicateur faction/camp, sens exact non prouvé ici)

    // Char_GetPairedElement 0x557C00 : élément "jumelé" (autre membre d'une paire alliance du
    // loadout), -1 si absent. Fonction pure injectable (dépend de g_LocalPlayerSheet, hors
    // périmètre des headers partagés de cette mission) ; nullptr -> -1 constant (fidèle au
    // fallback "return -1" de fin de fonction d'origine).
    std::function<int(int element)> pairedElement;
    int GetPaired(int element) const { return pairedElement ? pairedElement(element) : -1; }
};

// ---------------------------------------------------------------------------
// Distances fidèles (Math_Dist2D_XZ 0x53FA40 / Math_Dist3D 0x53FAA0 — sqrt(dx²+dz²) et
// sqrt(dx²+dy²+dz²), aucune approximation).
// ---------------------------------------------------------------------------
inline float Npc_DistanceXZ(float x1, float z1, float x2, float z2) {
    const float dx = x1 - x2, dz = z1 - z2;
    return std::sqrt(dx * dx + dz * dz);
}
inline float Npc_Distance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    const float dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// Level_ToAggroValue 0x53F700 — table complète (identité sous 100, courbe codée en dur
// 100..157, 1 par défaut au-delà de 157). Utilisée par Npc_GetNameplateColor (branche par
// défaut : comparaison de "puissance" joueur vs seuil du PNJ/monstre).
// ---------------------------------------------------------------------------
int Npc_LevelToAggroValue(int level);

// ===========================================================================
// Classification pure (aucun état, aucun réseau) — Npc_IsSpecialType 0x54EE60.
// ===========================================================================
// Vrai pour la liste figée {19,20,21,34,49,120,154,175,176,177,190,191,192,193} (table de
// type/code "spécial" réutilisée à plusieurs endroits du binaire — cf. xrefs Char_CalcAttackRating*,
// Char_BuildEquipSnapshot, UI_MainInventory : PAS spécifique aux PNJ malgré le nom IDB).
bool Npc_IsSpecialType(int typeOrCode);

// ===========================================================================
// Npc_IsQuestTarget 0x540340 / Npc_GetNameplateColor 0x540790 — fonctions PURES opérant sur
// un pointeur de record "def" générique (NpcEntity::def OU MonsterEntity::def, cf. bandeau de
// tête). `def` peut être nullptr (résultat "non trouvé"/couleur par défaut, sûr).
// ===========================================================================

// Vrai si l'entité correspond à l'objectif de quête/élément/faction actif du joueur local
// (branches fidèles : catégorie 1 -> sous-catégorie 232/236 ; catégories 6/7/8/9/0xE/0xF ->
// élément/faction direct ; tout le reste -> false).
bool Npc_IsQuestTarget(const void* def, const NpcQuestContext& ctx);

// Code couleur de plaque de nom (10 = allié/élément apparié, 2 = hostile/mismatch, 22/33 =
// branche par défaut selon l'écart de "puissance" Level_ToAggroValue(niveau local) vs
// *(def+352)). `selfLevel`/`selfLevelBonus` = g_World.self.level / g_World.self.levelBonus.
int Npc_GetNameplateColor(const void* def, const NpcQuestContext& ctx, int selfLevel, int selfLevelBonus);

// ---------------------------------------------------------------------------
// Points d'intégration hors périmètre (réseau/UI/item/stat non modélisés dans les headers
// partagés de cette mission). Callbacks optionnels ; comportement par défaut documenté au
// site d'appel. EA d'origine citées pour le branchement réel — AUCUN envoi réseau direct
// n'est fait par ce module en dehors de SendVaultReq201.
// ---------------------------------------------------------------------------
struct NpcInteractionHost {
    // maybe_Npc_ShouldRefreshTarget 0x583E20 : éligibilité de rafraîchissement/verrouillage
    // du PNJ (comparaisons de noms alliance/chat + minuteries — hors périmètre, dépend de
    // l'UI sociale). Signature alignée sur AutoPlaySystem::AutoPlayHost::ShouldRefreshNpc pour
    // pouvoir brancher le MÊME callback des deux côtés. Défaut (non branché) : toujours éligible.
    std::function<bool(const NpcEntity&)> ShouldRefreshTarget;

    // cGameHud_PlaceItemIntoBag 0x650470 : tente de placer (itemId, weight) dans le sac.
    // outSlot=-1 => échec. Ordre des 3 autres sorties fidèle à l'appel d'origine
    // (&v9,&v11,&v10,&v14 — v9=outSlot ; v11/v10/v14 réordonnés tels quels, sémantique non
    // prouvée, réinjectés tels quels dans Net_SendVaultReq_201).
    std::function<void(uint32_t itemId, uint32_t weight, int& outSlot, int& outB, int& outC, int& outD)> TryPlaceItemIntoBag;

    // Net_SendVaultReq_201 0x5901C0 : émission réseau de la demande d'interaction/récompense
    // PNJ. Seul point d'envoi réseau de ce module ; TODO PRÉCIS : brancher sur le vrai
    // builder Net_Send* une fois la couche sortante disponible.
    std::function<void(int idHi, int idLo, int p0, int outSlot, int outB, int outC, int outD)> SendVaultReq201;

    // Branche "hors de portée" de Npc_Interact (0x53a73a) — approche à pied. Enchaîne dans le
    // binaire : World_IsPointBlocked 0x540DA0 (teste la position du JOUEUR, pas celle du PNJ —
    // fidèle, pas une erreur de notre part) -> Char_CalcAttackSpeed 0x4CCAB0 (vitesse) ->
    // Skill_TraceProjectilePath 0x5419F0 (calcule un point d'approche) -> Net_QueueRunTo
    // 0x511B00 (enqueue le déplacement). Hors périmètre (collision + pathing + réseau) : TODO
    // PRÉCIS, cf. EAs ci-dessus. Appelée avec la position du PNJ cible ; no-op par défaut.
    std::function<void(float npcX, float npcY, float npcZ)> ApproachNpc;

    // Item_GetEquipCategory 0x54C940 : catégorie d'équipement de l'item sélectionné (1/2 dans
    // AutoInteractForPet). Hors périmètre (Item/Stat, headers non inclus ici). -1 par défaut
    // (ne correspond jamais à 1 ni 2), fidèle au cas "aucune correspondance".
    std::function<int(uint32_t itemId)> GetEquipCategory;

    // Gate complète de Npc_AutoInteractForPet (0x53b600..0x53b661) : MobDb_GetEntry(&mITEM,
    // selectedItemId) 0x4C3C00 doit réussir ET (typeCode!=22 OU (Item_NormalizeStatByType
    // 0x4C8FF0 >= 100 ET compteur associé >= 1)). Hors périmètre (Item/Stat). false par défaut
    // (fonction inerte tant que non branchée — conservateur : on ne peut pas prouver la
    // disponibilité du pet sans le système Item réel).
    std::function<bool(uint32_t selectedItemId)> IsPetCommandItemReady;
};

// ---------------------------------------------------------------------------
// NpcInteractionSystem — porte l'état partagé entre les 4 fonctions d'action (verrou
// "requête en vol" dword_1675B08/flt_1675B0C — MÊME globals que
// AutoPlaySystem::pendingItemUseLatch_/pendingItemUseTimeSec_ documentés dans
// AutoPlaySystem.h : dans le binaire d'origine c'est UN SEUL verrou partagé entre l'usage de
// parchemin ET l'interaction PNJ. Ce module porte SA PROPRE copie ; TODO d'intégration :
// unifier avec AutoPlaySystem si les deux tournent en même temps dans la boucle de jeu) +
// les hooks hors périmètre (host) + l'extension par-PNJ (ext_).
// ---------------------------------------------------------------------------
class NpcInteractionSystem {
public:
    NpcInteractionHost host;

    // g_MorphInProgress 0x1675A88 : bloque l'émission de la requête PNJ (toutes fonctions
    // d'action). Piloté par l'appelant (même global que AutoPlayExternalState::morphInProgress).
    bool morphInProgress = false;

    // Extension par-PNJ (position + item/poids d'offre) — accès pour peuplement par le futur
    // portage du spawn NPC. Redimensionne automatiquement sur g_World.npcs.
    NpcInteractionExt& Ext(std::size_t npcIndex);
    const NpcInteractionExt* TryExt(std::size_t npcIndex) const;

    // Npc_Interact 0x53A660 — recherche le PNJ actif d'identité `targetId`, approche ou
    // interagit selon la portée (50.0). `gameTimeSec` = g_World.gameTimeSec (horodatage du
    // verrou, fidèle à flt_1675B0C = g_GameTimeSec).
    void Interact(EntityId targetId, float gameTimeSec);

    // Npc_AutoInteract 0x53A980 — interagit avec la cible d'ordre d'attaque courante
    // (`currentAttackOrderTarget` = g_SelfAttackOrder_GridX/Y 0x1687354/58, PAS de
    // déplacement si hors de portée — fidèle, contrairement à Interact()). Renvoie le code de
    // retour d'origine (0 = échec/absent, 1 = succès ou "hors de portée mais pas une erreur").
    int AutoInteractCurrentTarget(EntityId currentAttackOrderTarget, float gameTimeSec);

    // Npc_AutoSelectNearest 0x53ABC0 — scanne g_World.npcs par 6 passes de priorité
    // décroissante (vendeur direct, puis catégorie {5,6}, {4}, {3}, {2}, {1}) et interagit
    // avec le premier PNJ exploitable trouvé ; message d'échec (poids/sac/aucun PNJ) sinon.
    void AutoSelectNearestInteractable(float gameTimeSec);

    // Npc_AutoInteractForPet 0x53B5F0 — commande auto liée à l'item sélectionné
    // (`selectedItemId` = g_SelectedInvItemId 0x1673258). Gate via host.IsPetCommandItemReady.
    void AutoInteractForPet(uint32_t selectedItemId, float gameTimeSec);

private:
    std::vector<NpcInteractionExt> ext_;

    bool  pendingLatch_ = false;        // dword_1675B08 (g_GmCmdCooldownLatch)
    float pendingLatchTimeSec_ = 0.0f;  // flt_1675B0C

    // Résultat commun aux 4 fonctions d'action (args de Net_SendVaultReq_201 0x5901C0).
    struct RewardArgs {
        int idHi = 0, idLo = 0, p0 = 0, outSlot = 0, outB = 0, outC = 0, outD = 0;
        bool ok = false;        // false => échec (ne rien envoyer)
        bool blockedWeight = false; // true => bloqué par Util_SumExceeds2Billion (poids)
        bool blockedBag    = false; // true => bloqué par échec PlaceItemIntoBag
    };

    // Corps commun (typeCode188==1 -> vault direct + garde de poids ; sinon PlaceItemIntoBag).
    RewardArgs BuildRewardArgs(const NpcEntity& npc, const NpcInteractionExt& ext) const;
    // Envoie la requête si ni morph ni verrou en cours (fidèle : sinon silencieux, PAS d'erreur).
    void SendReward(const RewardArgs& args, float gameTimeSec);

    bool ShouldRefresh(const NpcEntity& npc) const;
    int  FindNpcIndexById(EntityId id) const;
};

} // namespace ts2::game

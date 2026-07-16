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
//   AutoPlay_Start                  0x45D580 -> Start()  (reset+armement+chargement des listes)
//   AutoPlay_LoadFriendList         0x45D730 -> LoadFriendList()  (G02_GINFO\011.BIN)
//   AutoPlay_LoadEnemyList          0x45DAF0 -> LoadEnemyList()   (G02_GINFO\012.BIN)
//   AutoPlay_SaveFriendList         0x45DE50 -> SaveFriendList()
//   AutoPlay_SaveEnemyList          0x45E140 -> SaveEnemyList()
//   AutoPlay_Update                 0x45E770 -> Update()  (tick principal : spine loot/liste)
// + petits utilitaires appelés par le cluster (traduits car indispensables) :
//   Player_IsCharClass  0x45C550, Player_IsInStance 0x45C480, sub_45C590 (affinité élément),
//   AutoPlay_IsFriend 0x45FAA0, AutoPlay_IsEnemy 0x45FBE0, Stat_UnpackCombined 0x54CE40.
//
// PÉRIMÈTRE : la machine d'état de ciblage/farming, PAS le rendu des panneaux UI autoplay
// (hors périmètre, cf. TODO ci-dessous). Les sous-systèmes tiers (collision de déplacement,
// réseau, placement d'objet en sac, classe/stance du joueur local au sens large) ne sont PAS
// modélisés dans Game/GameState.h ni les autres
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
    // = !Game_IsSceneNotReady() 0x53B9E0 : VRAI ssi dword_1822390==1 ET dword_1822388==1 (scène
    // PRÊTE). ATTENTION POLARITÉ : Game_IsSceneNotReady() renvoie l'INVERSE (vrai = scène NON
    // prête, `dword_1822390!=1 || dword_1822388!=1`) ; ce champ stocke SA NÉGATION. Lu tel quel
    // par MoveToNpc 0x45C608 (`if (!Game_IsSceneNotReady()) return 1;`, cf. .cpp:453) ET par
    // host.CanAutoInteractNpc (SceneManager.cpp). PRODUCTEUR À CÂBLER (hors périmètre) : un tick
    // doit écrire `sceneTransitionBlocking = !Game_IsSceneNotReady()` — aujourd'hui JAMAIS écrit
    // (=false à vie), donc MoveToNpc traite la scène comme jamais prête (branche interaction
    // toujours prise). Cf. rapport : câblage requis dès que MoveToNpc devient atteint par Update.
    bool sceneTransitionBlocking = false;
    bool warpSuppressed = false;          // dword_1675B00
    bool morphInProgress = false;         // dword_1675A88 (nom IDB : g_MorphInProgress)
    bool invDirtyEnable = true;           // g_InvDirtyEnable 0x16755AC (argument de Net_SendOp99).
    // NB : ré-armé (=1) en permanence par les op. d'inventaire (Inv_Add/RemoveItemQuantity, 82
    // xrefs sur 0x16755AC) ; requis =true pour que la garde d'entrée d'AutoPlay_Update @0x45e792
    // laisse passer. Le latch de Start le remet à 0 une fois (flush serveur) — cf. Update().

    // ---- CARBURANT D'AUTO-HUNT : AUCUN champ ici — le stockage est g_Client.Var (AP-01/AP-07) ----
    // g_AutoHuntFuelA/B 0x16755A4 / 0x16755A8 = crédit/temps de chasse restant. Garde d'entrée
    // d'AutoPlay_Update 0x45E770 @0x45e782 (`cmp g_AutoHuntFuelA,0 ; jg`) / @0x45e78b (idem B) :
    // le tick sort à la 1re ligne (`jmp loc_45ED71` @0x45e794) tant que les DEUX sont <= 0.
    //
    // CORRECTIF AP-07 (2026-07-16) — l'ancien commentaire attribuait l'armement à l'UI
    // (« Mis > 0 par AutoPlay_OnMouseUpMain 0x45A980 ») : c'est RÉFUTÉ par les xrefs. 0x45A980
    // est ABSENT des 19 xrefs de 0x16755A4 et des 15 xrefs de 0x16755A8 — il n'écrit que
    // g_InvDirtyEnable 0x16755AC. Ce carburant est 100 % SERVEUR ; le commentaire de l'IDB le
    // dit lui-même : « Crédit/temps de chasse restant A (serveur) ». Écrivains RÉELS (disasm) :
    //   Pkt_SetGameVar 0x468370        case 61 @0x468d30 / case 62 @0x468d5c / case 90 @0x469106
    //                                   (+ @0x468eae)
    //   Pkt_ItemActionDispatch 0x46A320 @0x478b4d / @0x481782 / @0x48317b
    //   Net_OnConfirmPromptOpen_Dlg10 0x490EE0 @0x490f93 / @0x490f9c
    //   Char_TickDeathRespawn 0x576CB0  @0x577ac3 / @0x577acc
    // LECTEURS SEULEMENT (à ne pas confondre) : AutoPlay_IsActionAllowed 0x45D470, AutoPlay_Update
    // 0x45E770, AutoPlay_OnMouseDown 0x45EE30, UI_GameHud_Render 0x67A3C0.
    //
    // CORRECTIF AP-01 (2026-07-16) — deux champs `autoHuntFuelA/B` vivaient ICI et étaient lus par
    // Update() : ils n'avaient AUCUN écrivain dans tout src/ (split-brain), donc le tick sortait à
    // sa 1re ligne À VIE. SUPPRIMÉS. Source de vérité UNIQUE côté ClientSource = le chemin serveur
    // g_Client.Var(0x16755A4) / g_Client.Var(0x16755A8), écrit par Net/GameVarDispatch.cpp:389
    // (case 61), :395 (case 62), :430, :481 (case 90) — lu directement par Update() (cf. .cpp,
    // ancre @0x45e782). Ne PAS réintroduire de champ miroir ici.

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
    // this+44 de l'objet d'origine (@0x458548 chemin 1er slot, @0x458596 chemin InsertTargetSorted) :
    // `dword_176703C[70*i] != 1`. Champ CONSERVÉ (il existe dans la struct d'origine et SelectTarget
    // le lit @0x4585e0), mais sa valeur est PROUVÉE toujours vraie au point d'insertion — cf. la
    // démonstration du verrouillage de phase dans .cpp::BuildTargetList (ancres 0x5816cf/0x581939).
    bool    available = false;
};

// ---------------------------------------------------------------------------
// (SUPPRIMÉ 2026-07-16) struct MonsterAutoplayExt — ses 3 champs étaient des FANTÔMES : déclarés,
// lus, mais JAMAIS écrits par personne dans src/ (aucun `Ext()` n'était un setter). Chacun a été
// re-ancré sur la donnée réelle, déjà disponible et déjà peuplée :
//   - `state` (AP-06)       = dword_1766F8C = base+0x18 = MonsterEntity::body+8. Peuplé par les DEUX
//     chemins d'EntityManager::OnSpawnMonster (:523 refresh / :540 spawn), miroir du memcpy 0x50
//     @0x467cef/@0x467e23 de Pkt_SpawnMonster 0x467B00. Lu via AutoTarget_MonsterActionState()
//     (Game/AutoTargetCombatGate.h:113) qui lit EXACTEMENT body+8. L'ancien défaut `state = 1`
//     rendait morts les 3 filtres « monstre mort/invalide » (0x45831a, 0x4586a9, 0x458d12).
//   - `engageRange` (AP-05) = unk_1766FD8 = base+0x64 = MonsterEntity::radius (GameState.h:212),
//     DÉJÀ calculé par EntityManager.cpp:558 avec la formule exacte du binaire
//     (sqrt(def[+256]^2+def[+248]^2)*0.5, @0x467d4f-0x467d87) et jusqu'ici JAMAIS lu.
//   - `aggroOwner`          = dword_176703C = base+0xC8. Son écrivain est INVISIBLE aux xrefs (accès
//     par pointeur) : Char_UpdateMotionState 0x5816A0 — reset inconditionnel `[eax+0C8h]=0` @0x5816cf
//     puis `[ecx+0C8h]=1` @0x581939 dans le SEUL case 12. Ne PAS lui inventer d'écrivain : la valeur
//     lue par le binaire est prouvée constante au point d'usage (cf. .cpp::BuildTargetList).
// Ne PAS réintroduire cette struct : elle réinstallerait 3 stockages sans écrivain.
// ---------------------------------------------------------------------------

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

    // Listes ami/ennemi = FILTRES DE BUTIN PAR NOM D'OBJET (ce ne sont PAS des noms de joueurs,
    // PAS l'UI sociale). Chargées de G02_GINFO\011.BIN (amis) / 012.BIN (ennemis) — this+296 /
    // this+324 de l'original —, chaque nom VALIDÉ à la lecture contre la table ITEM (MobDb_FindByName
    // mITEM 0x4C3C50 : stride 436, nom @+4 = ItemInfo::name) ; un nom inexistant est silencieusement
    // rejeté. Interrogées par AutoPlay_FindNpcTarget 0x458E90 sur le nom (def+4) de l'entrée de
    // butin/NPC :
    //   - friendNames (IsFriendName @0x458FEB) = LISTE BLANCHE : objet TOUJOURS sélectionné,
    //     court-circuitant le masque de catégorie config.pkFactionMask.
    //   - enemyNames  (IsEnemyName  @0x4590D1) = LISTE NOIRE  : objet JAMAIS sélectionné par la
    //     voie catégorie, même si sa catégorie est cochée.
    // AutoPlay_IsFriend 0x45FAA0 / AutoPlay_IsEnemy 0x45FBE0 = recherche linéaire par égalité de
    // chaîne (Crt_Strcmp) — std::vector<std::string> reproduit fidèlement.
    std::vector<std::string> friendNames;
    std::vector<std::string> enemyNames;
    bool IsFriendName(const char* name) const; // AutoPlay_IsFriend  0x45FAA0
    bool IsEnemyName(const char* name)  const; // AutoPlay_IsEnemy   0x45FBE0

    // ---- Chargement/sauvegarde des listes (G02_GINFO\011.BIN / 012.BIN) ---------------------
    // Fichier = 1200 o = 48 x 25 (sans en-tête), bourrage '@' (0x40). Load exige EXACTEMENT
    // 1200 o lus (sinon vide la liste + renvoie false, comme les 4 chemins d'échec du binaire) ;
    // le nom d'un slot = ses octets jusqu'au 1er '@' ou NUL. Save : garde taille (Friend : > 48 =>
    // vide + false SANS écrire ; Enemy : > 48 => vide PUIS écrit — asymétrie fidèle 0x45DEC0 /
    // 0x45E1A3), buffer pré-rempli '@', écrit 1200 o. Save requis par l'UI d'édition
    // (AutoPlay_OnMouseUpNameList 0x45B000, front UI voisin) — exposé même si pas encore câblé.
    bool LoadFriendList(); // AutoPlay_LoadFriendList 0x45D730
    bool LoadEnemyList();  // AutoPlay_LoadEnemyList  0x45DAF0
    bool SaveFriendList(); // AutoPlay_SaveFriendList 0x45DE50
    bool SaveEnemyList();  // AutoPlay_SaveEnemyList  0x45E140

    // Reset des cibles + armement de l'auto-hunt + CHARGEMENT des listes ami/ennemi. Miroir
    // d'AutoPlay_Start 0x45D580, dont l'UNIQUE appelant binaire est UI_InitAllDialogs 0x5ABF50
    // @0x5AC193 (`if (!AutoPlay_Start(g_AutoPlayBot)) return 0;` — 28e des ~37 slots d'init de
    // dialogues, APRÈS UI_FactionTitleWnd_Init 0x184BF90 et AVANT UI_GameHud_Init @0x5ac1a8).
    //
    // ⚠ WIRING TODO (hors de mes fichiers — front CharSelect-fix / GameWindows) : Start() n'a
    // AUJOURD'HUI AUCUN appelant côté ClientSource. Le miroir ClientSource de UI_InitAllDialogs est
    // GameWindows::Init (UI/GameWindows.h:127, qui POSSÈDE déjà autoPlaySystem_) — PAS
    // SceneManager.cpp (ancienne prescription erronée). Site fidèle : dans GameWindows::Init,
    // AVANT l'init du HUD, `if (!autoPlaySystem_.Start()) return false;` (Start() renvoie toujours
    // true @0x45d729, la garde ne se déclenche donc jamais — mais la FORME reste `if (!...)` pour
    // respecter 0x5ac193 ≺ 0x5ac1a8). SANS cet appel, LoadFriendList/LoadEnemyList ne tournent
    // jamais, friendNames/enemyNames restent vides, ET huntArmed_ reste false -> la chaîne OR
    // d'Update() est désarmée (défaut D1). Câblage à assigner par l'orchestrateur.
    bool Start(); // AutoPlay_Start 0x45D580

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

    // ---- Tick principal -------------------------------------------------------
    // Portage fidèle d'AutoPlay_Update 0x45E770 (la « spine » atteignable : gardes -> latch
    // inv-dirty -> throttle matériaux -> chaîne OR loot/NPC/consommables dont MoveToNpc ->
    // ciblage monstre). C'EST ce tick qui interroge les listes ami/ennemi (via MoveToNpc ->
    // FindNpcTarget), correctif du défaut « listes chargées mais jamais consultées ». Le tail
    // de combat (Player_CastSkill/Net_QueueRunTo) est DÉFÉRÉ (cf. .cpp, TODO 0x45E91D). dt en
    // secondes (remplace GetTickCount() par des accumulateurs — seuils 2000 ms / 50 ms, cf. .cpp).
    // Déjà câblé chaque frame InGame : SceneManager.cpp:1497 -> GameWindows::UpdateAutoPlay
    // (UI/GameWindows.h:142) -> Update(dt). C'est ce chemin VIVANT qui rend actionnables les
    // correctifs AP-01/AP-05/AP-06 : la garde de carburant franchie (g_Client.Var 0x16755A4/A8),
    // le tick atteint UpdateTargeting -> BuildTargetList/SelectTarget/CountTargets.
    void Update(float dt);

    // Accès lecture (diagnostic / UI — le rendu du panneau autoplay reste hors périmètre,
    // TODO EA du renderer non identifiée dans ce cluster).
    const std::array<AutoPlayTargetSlot, 15>& Targets() const { return targets_; }
    uint16_t TargetCount() const { return targetCount_; }
    int32_t  CurrentTargetIndex() const { return currentTargetIndex_; }

    // (SUPPRIMÉ 2026-07-16) Ext(std::size_t) / MonsterAutoplayExt — cf. le bandeau ci-dessus : les
    // 3 champs par-monstre étaient des fantômes sans écrivain, re-ancrés sur MonsterEntity::body+8
    // et MonsterEntity::radius, déjà peuplés par EntityManager.

private:
    std::array<AutoPlayTargetSlot, 15> targets_{};
    uint16_t targetCount_ = 0;          // +216
    int32_t  currentTargetIndex_ = -1;  // +220, -1 = aucune cible verrouillée

    // Timers dt-driven (remplacent GetTickCount() — cf. commentaire Update()). npcInteractCooldownSec_
    // modélise this[60] (byte +240) : timestamp partagé « dernière action » lu à la fois pour le
    // cooldown 50 ms (MoveToNpc 0x45C5F7) et le throttle 2000 ms (Update 0x45E827).
    float rebuildTimerSec_ = 0.0f;          // +228 (rebuild toutes les 1000 ms)
    float npcInteractCooldownSec_ = 0.0f;   // +240 (cooldown NPC 50 ms / throttle matériaux 2000 ms)

    // this+288 : armé (=1) par Start 0x45D641 ; garde la chaîne OR d'auto-hunt d'AutoPlay_Update
    // (rien ne l'efface dans le cluster étudié -> reste vrai après l'init HUD, le carburant
    // g_AutoHuntFuelA/B faisant office de gate d'activité réelle).
    bool huntArmed_ = false;
    // this+284 : armé (=1) par Start 0x45D634 ; latch one-shot -> flush inv-dirty au serveur
    // (Net_SendOp99) au 1er tick armé puis désarmé, cf. Update() @0x45e79e.
    bool invDirtyStartLatch_ = false;

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

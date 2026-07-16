// Game/ItemPickupSystem.h — ramassage d'objets au sol + validation de quantite (split de pile).
//
// Reecriture C++ PROPRE de :
//   Item_PickupTarget            EA 0x539ec0 — confirmation de ramassage a l'arrivee (garde de portee)
//   Item_InteractGround          EA 0x539dc0 — approche + confirmation (meme garde de portee, cheminement)
//   Item_QtyDialog_OnLButtonUp   EA 0x5b1650 — clic OK du dialogue de quantite (bornage de la saisie)
//
// /!\ BANDEAU CORRIGE — Passe 4 / vague W7, front "npc-array-unify". CE QUI SUIVAIT ICI ETAIT
// FAUX ET S'EST REVELE INVERSE. L'ancien texte affirmait que les noms IDA du bloc 0x1764D14
// ("g_NpcRenderArray", "g_NpcCount", "Scene_RayHitNpcBox") etaient "mal etiquetes par une passe
// de renommage automatique" et que ce tableau portait les objets au sol. RE-PROUVE dans IDA
// (W7) : c'est L'INVERSE — les noms IDA sont CORRECTS et c'est l'hypothese "objets au sol" de
// ce module qui etait erronee.
//
// Verite terrain (re-prouvee par decompilation fraiche, cf. Game/GameState.h::NpcRenderEntry
// pour la table d'offsets complete) :
//   - 0x1764D14 (stride 88, borne g_NpcCount 0x1687220 = 100) = tableau de RENDU/CIBLAGE DES
//     PNJ. ECRIVAIN UNIQUE : cGameData_LoadZoneNpcInfo 0x5578E0, qui y copie la table de PNJ
//     STATIQUES par zone mZONENPCINFO 0x14AA930 (def mNPC, position, angle). AUCUN objet au sol
//     n'y est jamais ecrit -- aucun handler reseau d'objet au sol ne le touche.
//   - TOUS ses lecteurs le traitent en PNJ : Npc_DrawMesh 0x57FF00, Npc_RenderSlotTick 0x5803A0,
//     Scene_RayHitNpcBox 0x541680, World_PickEntityAtCursor 0x538AB0 (boucle j), et la
//     categorie de clic 4 route vers Npc_ApproachAndInteract @0x53723F (0x539DC0) puis
//     UI_NpcWin_Open 0x5DB530 -- une fenetre de SERVICE PNJ, ce qui est coherent avec un PNJ,
//     pas avec un ramassage d'objet.
//   - Les sacs de butin sont AILLEURS : categorie de clic 6 -> Npc_Interact @0x536AB9 sur
//     dword_17AB534 (stride 152, borne dword_1687228), via Scene_RayHitItemModel 0x5418B0
//     @0x539129.
//
// CONSEQUENCE — MODULE SUSPECT, A REEVALUER (signale a l'orchestrateur, NON traite ici) : la
// premisse de `FindNearestGroundItem` (chercher un objet au sol dans le pool 0x1764D14) est
// FAUSSE : ce pool ne contient que des PNJ. Par consigne de fidelite ("pas de refactor
// opportuniste", "un stub du binaire reste un stub"), W7 ne fait que CORRIGER LE TYPAGE et ce
// bandeau ; la semantique du module est laissee INCHANGEE. Les EA 0x539EC0/0x539DC0/0x5B1650
// cites ci-dessous restent valides en eux-memes -- c'est leur RATTACHEMENT a une notion
// d'"objet au sol" qui est a rejuger.
// TODO [ancre 0x538AB0 / 0x539DC0] : re-decompiler Item_PickupTarget/Item_InteractGround pour
// trancher s'ils sont en realite les fonctions d'APPROCHE/INTERACTION PNJ (leurs noms IDA
// "Item_*" seraient alors les faux amis, symetriquement a Npc_ApproachAndInteract 0x539DC0 qui
// porte le MEME EA que "Item_InteractGround" ci-dessus -- indice fort d'un doublon de nommage).
//
// Voir Docs/TS2_GAMEPLAY_LOGIC.md, memoires ts2-entity-model / ts2-gameplay-logic.
#pragma once
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/EntityManager.h"
#include <cstdint>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Ramassage au sol.
// ---------------------------------------------------------------------------

// Seuil de portee fidele au binaire : Math_Dist3D(cible, joueur) <= 20.0, constante en dur
// identique dans les deux fonctions sources :
//   Item_PickupTarget   EA 0x539eef : if (Math_Dist3D(...) <= 20.0)
//   Item_InteractGround EA 0x539def : if (Math_Dist3D(...) <= 20.0)
inline constexpr float kPickupRange = 20.0f;

// Garde-fou anti-depassement (Util_SumExceeds2Billion, EA 0x53f660) :
//   return a2 + (__int64)a1 > 2000000000;
// utilise dans ce sous-systeme pour valider un transfert de poids AVANT de l'appliquer
// (motif verifie dans Npc_AutoInteract EA 0x53aa87 : "if (Util_SumExceeds2Billion(g_InvWeight,
// poidsObjet)) return 0;", et motif voisin dans AutoPlay_ScanGroundItems EA 0x45e5d8 avec un
// seuil en dur de 1 900 000 000 applique a g_InvWeight lors du filtrage des objets auto-loot).
//
// IMPORTANT (fidelite) : NI Item_PickupTarget (EA 0x539ec0) NI Item_InteractGround
// (EA 0x539dc0) ne contiennent la moindre verification de poids/capacite dans leur
// desassemblage — la garde de portee (20.0) est la SEULE validation « avant ramassage »
// presente dans ces deux fonctions precises. La garde de poids ci-dessous est reproduite a
// partir de la logique de transfert d'objet verifiee la plus proche dans le binaire (citee
// ci-dessus) pour satisfaire l'exigence de validation de poids de cette mission.
// TODO (EA 0x539ec0 / 0x539dc0) : confirmer dynamiquement si un plafond de poids distinct
// est applique cote serveur au moment du ramassage reel (aucun indice cote client dans ces
// deux fonctions).
inline constexpr int64_t kWeightOverflowGuard = 2000000000; // EA 0x53f660

// Cible de ramassage resolue.
struct GroundPickupTarget {
    // Indice dans g_World.npcRenderEntries (== a1 de Item_InteractGround). C'est un index de
    // SLOT du pool 0x1764D14, stable et aligne sur mZONENPCINFO[i] depuis W7 (le pool a 100
    // slots fixes, plus de compaction) -- c'est exactement l'index que le binaire envoie au
    // serveur : Net_QueueRunTo(..., 4, index, ...) @0x539E78, index resolu par
    // World_PickEntityAtCursor (`*a4 = j` @0x538E8F).
    int             index    = -1;
    // nullptr si aucun slot actif trouve. Retype par W7 : GroundItem -> NpcRenderEntry
    // (Game/GameState.h) -- meme type, nom corrige (cf. bandeau de tete : ce pool porte des
    // PNJ, pas des objets au sol).
    NpcRenderEntry* item     = nullptr;
    float           distance = 0.0f; // distance 3D au joueur local (Math_Dist3D, EA 0x53faa0)
};

// Trouve l'objet au sol actif le plus proche du joueur local (g_World.Self()).
//
// Reecriture PROPRE (pas byte-exacte) : le binaire ne "cherche" pas la cible dans
// Item_PickupTarget/Item_InteractGround — ces fonctions consultent un index deja resolu par
// le clic souris (Game_OnWorldLeftClick EA 0x536690 -> World_PickEntityAtCursor EA 0x538ab0)
// et memorise cote self (dword_1687354, "g_SelfAttackOrder_GridX" — reutilise ici comme
// indice de cible de ramassage, cf. Char_TickLootPickupState EA 0x57ca50). On modelise ici
// une resolution explicite par plus-proche-voisin afin de permettre un appel autonome
// (ramassage automatique / raccourci clavier) sans dependre de l'etat de clic UI, qui est
// hors perimetre de cette mission (rendu/curseur/raycast ecran, EA 0x538ab0).
GroundPickupTarget FindNearestGroundItem(GameWorld& world);

// Reproduit fidelement la garde de portee d'Item_PickupTarget/Item_InteractGround :
// distance 3D au joueur local <= 20.0 (EA 0x539eef / 0x539def, kPickupRange). Renvoie false
// si `target` n'est pas active ou si la distance depasse le seuil.
bool IsWithinPickupRange(const GameWorld& world, const NpcRenderEntry& target);

// Reproduit le motif de garde anti-depassement (Util_SumExceeds2Billion, EA 0x53f660)
// applique a l'ajout d'un poids d'objet au poids d'inventaire courant. Renvoie true si le
// ramassage DOIT etre refuse (currentWeight + addedItemWeight > kWeightOverflowGuard).
bool WouldExceedWeightCapacity(int64_t currentWeight, int64_t addedItemWeight);

// Issue de l'evaluation d'une tentative de ramassage.
enum class PickupOutcome {
    Ok,                 // cible en portee et poids ok : pret a emettre la requete reseau
    NoTarget,            // aucun slot actif a proximite (aucune correspondance dans g_World.npcRenderEntries)
    OutOfRange,          // hors du rayon de 20.0 (EA 0x539eef / 0x539def)
    WouldExceedWeight,   // depasserait la garde anti-overflow (EA 0x53f660)
};

// Point d'entree haut niveau : resout la cible de ramassage la plus proche puis valide la
// portee (EA 0x539eef/0x539def) et le poids (EA 0x53f660, cf. WouldExceedWeightCapacity).
// Remplit `outTarget` avec la cible resolue (meme en cas d'echec, pour diagnostic UI).
//
// PAS D'ENVOI RESEAU ICI. Dans le binaire, un ramassage confirme (portee ok) declenche
// uniquement Net_QueueMoveResume (EA 0x511870, arret de la file de deplacement) puis
// UI_NpcWin_Open (EA 0x5db530, ouverture d'une fenetre — construit un menu de service a
// partir de la cible ; AUCUN Net_SendPacket_Op* n'est appele dans Item_PickupTarget ni
// Item_InteractGround). La requete reseau de confirmation de ramassage est vraisemblablement
// emise par le callback de selection/OK de cette fenetre (rendu/UI, hors perimetre de cette
// mission — TODO EA 0x5db530 : tracer les callbacks de la fenetre ouverte par UI_NpcWin_Open
// pour identifier le builder Net_Send* exact du ramassage). Voir aussi
// Inv_RemoveItemQuantity (EA 0x5b0340, appele par le dialogue de quantite ci-dessous) : cette
// fonction ne fait QUE de la comptabilite locale (decrement des tableaux d'inventaire) et
// n'emet elle non plus AUCUN paquet reseau.
PickupOutcome EvaluatePickup(GameWorld& world, GroundPickupTarget& outTarget,
                              int64_t itemWeight = 0);

// ---------------------------------------------------------------------------
// Dialogue de quantite (split de pile) — Item_QtyDialog_OnLButtonUp, EA 0x5b1650.
// ---------------------------------------------------------------------------
//
// Le binaire repete EXACTEMENT le meme algorithme de bornage pour chaque categorie de
// conteneur (switch sur this[4] : cas 1, 5, 6, 7, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x15,
// 0x17..0x1A) — seule change la source du "maximum disponible" (grille d'inventaire
// principale, grille de ramassage au sol / g_Container5, carquois, poids, entrepot,
// etal...). L'algorithme commun, illustre par le cas 1 (grille d'inventaire principale,
// EA 0x5b1705-0x5b179b) est :
//
//   quantite = aSaisieUtilisateur ? quantiteDemandee : maxDisponible;  // EA 0x5b1713/0x5b1731
//                                                                       // (this[19]>=1 ? atoi(buf) : maxDisponible)
//   if (quantite > maxDisponible) quantite = maxDisponible;            // EA 0x5b1767/0x5b1785
//   if (quantite < 1) -> annulation (equiv. sub_5B02D0, EA 0x5b1791),  // EA 0x5b178c
//       le dialogue se ferme sans appeler Inv_RemoveItemQuantity ;
//   sinon -> quantite validee, stockee puis Inv_RemoveItemQuantity(this) est appele.
//
// Ce meme motif se retrouve a l'identique dans les 14 autres cas du switch (ex. cas 5,
// grille de ramassage au sol : EA 0x5b17ff-0x5b188a, source = dword_1674400[42*row+3*col],
// EXACTEMENT EntityManager::GroundPickupSlot::count ; cas 7 et 0x15 : plafond fixe 99,
// EA 0x5b194d-0x5b198a et EA 0x5b1d4c-0x5b1d89).
//
// Renvoie la quantite bornee dans [1, maxDisponible], ou 0 si la demande est invalide
// (maxDisponible <= 0, ou quantite resolue < 1) — dans le binaire ce cas ferme le dialogue
// via sub_5B02D0(this) au lieu d'appeler Inv_RemoveItemQuantity.
//
// `aSaisieUtilisateur` reproduit la garde `this[19] >= 1` (longueur du champ de saisie du
// dialogue, EA 0x5b1713) : true = une quantite a ete tapee par le joueur (quantiteDemandee
// est alors la valeur deja convertie via Crt_Atoi, EA 0x7603a6) ; false = aucune saisie,
// le binaire utilise alors directement `maxDisponible` comme valeur de depart (comportement
// par defaut du dialogue quand il s'ouvre : quantite pre-remplie au maximum). La saisie
// clavier/texte elle-meme (rendu du slider/dialogue) est hors perimetre de cette mission.
int32_t ValidateSplitQuantity(int32_t maxDisponible, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur = true);

// Variante pratique pour une cellule de la grille d'inventaire principale (cas this[4]==1 du
// binaire, EA 0x5b1705-0x5b179b : g_InvGrid_Count[384*row+6*col] ; le compteur est stocke
// dans InvCell::flag, cf. ClientRuntime.h InventoryState::Set()).
int32_t ValidateSplitQuantity(const InvCell& cellule, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur = true);

// Variante pratique pour un slot de la grille de ramassage au sol (cas this[4]==5 du binaire,
// EA 0x5b17ff-0x5b188a : dword_1674400[42*conteneur+3*slot], EXACTEMENT
// EntityManager::GroundPickupSlot::count, cf. EntityManager.h / g_Container5_ItemId).
int32_t ValidateSplitQuantity(const GroundPickupSlot& cellule, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur = true);

} // namespace ts2::game

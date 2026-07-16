// Game/EntityManager.h — gestionnaire d'entites du monde (spawn / despawn / update).
//
// Peuple et rafraichit les tableaux d'entites de g_World (joueurs / monstres / NPC)
// a partir des paquets ENTRANTS decodes dans Net/RecvPackets.h. Une methode par
// famille de paquet. Reecriture C++ PROPRE (pas byte-exact) des handlers d'origine :
//   Pkt_EnterWorld           0x464160  (op 0x0c) -> OnEnterWorld
//   Pkt_SpawnCharacter       0x4646c0  (op 0x0f) -> OnSpawnCharacter
//   Pkt_CharStateUpdate      0x464c10  (op 0x10) -> OnCharStateUpdate
//   Pkt_CharStatDelta        0x465d90  (op 0x11) -> OnCharStatDelta
//   Pkt_SpawnMonster         0x467b00  (op 0x12) -> OnSpawnMonster
//   Pkt_SpawnNpc             0x467ec0  (op 0x13) -> OnSpawnNpc
//   Pkt_GroundItemRemove     0x46a200  (op 0x19) -> OnGroundItemRemove
//   Net_OnPartyMemberPosition 0x4ab9f0 (op 0x91) -> OnPartyMemberPosition
//
// L'etat partage vit dans Game/GameState.h (g_World) ; seuls les types PROPRES a ce
// sous-systeme (grille de ramassage) sont declares ici. Voir RE/net_handler_notes.md
// et Docs/TS2_PROTOCOL_SPEC.md pour la semantique d'origine.
#pragma once
#include <cstddef>   // size_t (kPMoveState/kPMoveStateLen ci-dessous)
#include <cstdint>
#include <vector>
#include "Game/GameState.h"
#include "Net/RecvPackets.h"

namespace ts2::game {

// Offsets du bloc « move-state » du JOUEUR, relatifs au body (600 o) du slot.
// EXPOSES (etaient fichier-local dans EntityManager.cpp) pour la couche d'intention
// Game/PlayerCmdController.*, qui lit/ecrit ce meme bloc : dans le binaire c'est le
// global g_SelfMoveStateBlock 0x1687324, et g_EntityArray 0x1687234 + 0x18 (body) + 216
// = 0x1687324 — c'est donc DEJA ce bloc-ci, pas un etat separe. Recoupe par body+228 =
// 0x1687330 = flt_1687330 (position monde du joueur local).
// Layout : {animSlot@+0, actionState@+4, animFrame@+8, pos@+12, dest@+24, facing@+36,
//           targetFacing@+40, targetKind@+44, ...} — cf. TS2_MoveStateBlock (IDB) et
//           Game/PlayerCmdController.h::MoveCmdBlock.
inline constexpr size_t kPMoveState    = 216;  // bloc move-state (72 o) -> g_SelfMoveStateBlock
inline constexpr size_t kPMoveStateLen = 72;

// Grille de ramassage au sol (dword_1674400 / g_Container5_ItemId de l'original).
// ATTENTION : ce n'est PAS le tableau d'objets-monde GameWorld::groundItems
// (dword_1764D14) — c'est la petite grille "radar" de ramassage adressee par
// (conteneur, slot), cible du seul handler Pkt_GroundItemRemove. Chaque slot =
// triplet {itemId, count, aux}. Conteneur = 14 slots (stride 42 dwords d'origine),
// slot = 3 dwords. Modelisee ici en propre car absente de GameState.
struct GroundPickupSlot {
    uint32_t itemId = 0;
    uint32_t count  = 0;
    uint32_t aux    = 0;
    bool empty() const { return itemId == 0 && count == 0; }
};

// EntityManager : branche les paquets entrants sur les tableaux d'entites de g_World.
class EntityManager {
public:
    // op 0x0c — reinitialise TOUS les tableaux d'entites puis copie les blocs
    // self/zone recus (perso+inventaire 10088 o, etat de zone 288 o).
    void OnEnterWorld(const net::EnterWorld& p);

    // op 0x0f — creation/mise a jour d'un personnage (index 0 = joueur local).
    // Retourne le slot cree/rafraichi.
    PlayerEntity* OnSpawnCharacter(const net::SpawnCharacter& p);

    // op 0x12 — creation/mise a jour d'un monstre. Retourne le slot, ou nullptr si
    // l'id de definition est invalide alors que la base MONSTER_INFO est chargee.
    MonsterEntity* OnSpawnMonster(const net::SpawnMonster& p);

    // op 0x13 — creation / rafraichissement / despawn (action==3) d'un NPC.
    // Retourne le slot vivant, ou nullptr en cas de despawn / rejet. Renseigne
    // aussi x/y/z (body+16/20/24, cf. Game/EntityManager.cpp).
    NpcEntity* OnSpawnNpc(const net::SpawnNpc& p);

    // op 0x10 — mise a jour des bitfields d'etat d'un personnage (36 etats).
    void OnCharStateUpdate(const net::CharStateUpdate& p);

    // op 0x11 — deltas stat/PV/PM/niveau (dispatcher 36 cas ; sous-ensemble entite).
    void OnCharStatDelta(const net::CharStatDelta& p);

    // op 0x91 — position monde d'un membre de groupe (resolu par identite reseau).
    void OnPartyMemberPosition(const net::PartyMemberPosition& p);

    // op 0x19 — decrement/retrait d'une pile de la grille de ramassage.
    void OnGroundItemRemove(const net::GroundItemRemove& p);

    // Acces borne a la grille de ramassage (etat possede par le manager).
    // Renvoie nullptr si (conteneur,slot) sort des limites.
    GroundPickupSlot* PickupSlot(uint32_t containerIndex, uint32_t slotIndex);

    // Vrai si l'entite est le joueur local (index 0 du tableau joueurs).
    bool IsSelf(const PlayerEntity* e) const;

    // Grille de ramassage brute (lecture seule pour l'UI/tests).
    const std::vector<GroundPickupSlot>& GroundPickup() const { return groundPickup_; }

    // Bornes de la grille de ramassage.
    static constexpr uint32_t kSlotsPerContainer = 14;
    static constexpr uint32_t kMaxContainers     = 256;

private:
    std::vector<GroundPickupSlot> groundPickup_;
};

// Instance globale unique (a l'image des handlers d'origine operant sur des globals).
inline EntityManager g_EntityManager;

} // namespace ts2::game

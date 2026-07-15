// Net/ItemActionDispatch.h — mega-dispatcher de l'opcode reseau 0x1a.
//
// Reecriture fidele de Pkt_ItemActionDispatch (EA 0x46A320, ~121 Ko, un des plus
// gros handlers du binaire). Ce paquet est envoye par le serveur en reponse a une
// action « utiliser/equiper/ranger un objet de l'inventaire » : le serveur echo la
// position (ligne/colonne) de la cellule d'inventaire concernee, le client relit
// l'objet dans sa propre grille, resout sa fiche ITEM_INFO et aiguille selon le
// TYPE de l'objet (ITEM_INFO.typeCode, offset +188 = champ 0xBC).
//
// L'opcode 0x1a n'a pas de struct dans RecvPackets.h : le payload est un simple
// bloc de 4 dwords (voir .cpp pour la carte). C'est un trou de couverture comble ici.
//
// SOURCE DE VERITE : desassemblage idaTs2. Les EA d'origine sont citees en commentaire.
#pragma once
#include <cstdint>

namespace ts2::game {

// Applique un paquet 0x1a. `payload` pointe sur le bloc recu (equivalent de
// unk_8156C1 dans le binaire) ; `len` sa taille (le binaire lit 16 octets fixes).
void ApplyItemActionDispatch(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

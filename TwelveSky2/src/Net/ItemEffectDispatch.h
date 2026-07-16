// Net/ItemEffectDispatch.h — mega-switch d'effets consommables (def_46B168, EA 0x46B168).
//
// C'est le DEFAUT du dispatcher d'action d'objet Pkt_ItemActionDispatch (0x46A320) :
// quand aucun des blocs A..E (apprentissage / equip / conteneur / ceinture / rack) ne
// s'applique, l'execution retombe sur def_46B168 (0x46B168), un enorme sous-switch
// (~120 Ko, >1000 cas) aiguille sur var_480 = ITEM_INFO[+0] (= item->itemId).
//
// PROUVE (def_46A44F 0x46B10F) :
//     mov edx, [ebp+var_438]   ; var_438 = record ITEM_INFO
//     mov eax, [edx]           ; *record = ITEM_INFO[+0] = template id
//     mov [ebp+var_480], eax   ; var_480 = template id (== item->itemId == cell.itemId)
// Le brief supposait un « id d'effet » d'un champ dedie : FAUX. Aucun bloc n'ecrit
// var_480 ailleurs — le switch aiguille directement sur item->itemId.
//
// SOURCE DE VERITE : desassemblage idaTs2. EA d'origine citees en commentaire (Regle #0).
#pragma once
#include <cstdint>

namespace ts2::game {

struct ItemInfo; // fwd (Game/GameDatabase.h)

// Applique l'effet consommable keye sur item->itemId (var_480 = ITEM_INFO[+0]).
//   flag = var_414 (payload+0 : drapeau/code resultat serveur ; 0 = executer).
//   row  = var_42C (ligne de la cellule d'inventaire, brut).
//   col  = var_43C (colonne de la cellule d'inventaire, brut).
//   dstD = var_428 (index destination « D » / itemId cible d'une transformation).
void ApplyItemEffectDispatch(const ItemInfo* item, uint32_t flag,
                             uint32_t row, uint32_t col, uint32_t dstD);

} // namespace ts2::game

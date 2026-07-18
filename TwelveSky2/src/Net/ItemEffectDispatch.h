// Net/ItemEffectDispatch.h — consumable effect mega-switch (def_46B168, EA 0x46B168).
//
// This is the DEFAULT of the Pkt_ItemActionDispatch (0x46A320) object-action dispatcher:
// when none of the blocks A..E (learn / equip / container / belt / rack) applies,
// execution falls through to def_46B168 (0x46B168), a huge sub-switch
// (~120 KB, >1000 cases) keyed on var_480 = ITEM_INFO[+0] (= item->itemId).
//
// PROVEN (def_46A44F 0x46B10F):
//     mov edx, [ebp+var_438]   ; var_438 = ITEM_INFO record
//     mov eax, [edx]           ; *record = ITEM_INFO[+0] = template id
//     mov [ebp+var_480], eax   ; var_480 = template id (== item->itemId == cell.itemId)
// The brief assumed an "effect id" from a dedicated field: WRONG. No block writes
// var_480 anywhere else — the switch is keyed directly on item->itemId.
//
// SOURCE OF TRUTH: idaTs2 disassembly. Original EAs cited in comment (Rule #0).
#pragma once
#include <cstdint>

namespace ts2::game {

struct ItemInfo; // fwd (Game/GameDatabase.h)

// Applies the consumable effect keyed on item->itemId (var_480 = ITEM_INFO[+0]).
//   flag = var_414 (payload+0: flag/server result code; 0 = execute).
//   row  = var_42C (inventory cell row, raw).
//   col  = var_43C (inventory cell column, raw).
//   dstD = var_428 ("D" destination index / target itemId of a transformation).
void ApplyItemEffectDispatch(const ItemInfo* item, uint32_t flag,
                             uint32_t row, uint32_t col, uint32_t dstD);

} // namespace ts2::game

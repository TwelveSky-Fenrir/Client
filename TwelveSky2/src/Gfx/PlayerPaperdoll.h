// Gfx/PlayerPaperdoll.h — player paperdoll resolver, modeled on Char_RenderModel 0x527020.
//
// ROLE: produce, for a player, (a) the SHARED animated bone palette and (b) the ordered
// list of model pieces to draw with the SAME transform — replacing the current inline
// 2-piece body in Scene/WorldRenderer.cpp.
//
// IDA PROOF (Char_RenderModel 0x527020, per-piece drawing on the CharSelect screen; in-game
// player body drawing is not statically located -> in-game usage is an honest extrapolation
// of this function):
//   - v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, race, gender, weaponPose, animState,1,0,0)
//     = ONE bone palette shared by ALL pieces.
//   - 2-piece base body: SObject_DrawEx(&flt_F5B21C[..], .., v37, 1) (SLOT1)
//                      + SObject_DrawEx(&flt_F59A7C[..], .., v37, 1) (SLOT0).
//   - Weapon (v33, a4[62]): SObject_DrawEx(&flt_100EA3C[.. + 36*(*(v33+196))], .., v37, 1)
//     -> SAME palette v37, SAME transform a8(pos)/a7(animTime) as the body: the weapon is
//        SKINNED to the hand bone via the body's palette, NOT positioned by an offset. This is
//        the "hand" attachment requested (Char_RenderModel 0x527bfe).
//
// FLEET C / FRONT C3 WIRING (integration, 2026-07-17) — see PlayerPaperdoll.cpp for the detail
// and the anchors. Summary:
//   * B3 Gfx/PlayerMotionSlotResolver.h: the v37 palette now goes through ResolvePlayerMotionSlot
//     0x4E46A0 (with its Motion_IsValidWeaponPose 0x4E3A30 guard). Since Char_RenderModel calls
//     PcModel_ResolveEquipSlot with a8=0 AND a6=1, the slot is ALWAYS category 1 "C" -> routed via the
//     PUBLIC MotionCache::GetForPlayer (identical "C..." stem) without touching the cache API. Net effect:
//     an invalid (pose=0, state) pair falls onto the proven idle C001001128 instead of an out-of-table stem.
//   * B2 Gfx/EquipModelResolver.h: the 2-piece base body goes through BuildArmorBodyStem(Base0/Base1)
//     -> ModelCache::Get (public), a stem BIT-FOR-BIT identical to the old GetForPlayerBody.
//
// OUT OF SCOPE (TODO anchor): the 12 other equipment slots (helmet/armor/...) — B2 now provides
// BuildArmorBodyStem for slots 14..21 ("C..." stem indexed by *(itemRec+196)), BUT the DATA is
// missing: Char_RenderModel is only called from CharSelect (a4[30..74]), the IN-GAME layout of
// equipped item ids is not proven -> only the weapon is reversed (self=equip[7], remote=body+148, see
// GameState.h); helmet/torso/etc. of remote players have no proven offset. The weapon itself
// remains on the GetForItem fallback (the real flt_100EA3C catalog indexed by field196 is not traced).
// The paperdoll = 2-piece body + weapon; the rest stays a TODO anchor (no stem/offset invention).
#pragma once
#include "Gfx/ModelCache.h"
#include "Gfx/MotionCache.h"
#include "Game/GameState.h"
#include <vector>

namespace ts2::gfx {

// Result of a paperdoll resolution. `palette` (animated, shared) applies to ALL
// pieces (Char_RenderModel v37). `pieces` in order SLOT0, SLOT1, [weapon].
struct PaperdollResult {
    gfx::BonePalette                        palette{};       // Char_RenderModel 0x527020: shared v37
    std::vector<const gfx::SkinnedModel*>   pieces;          // SLOT0 (flt_F59A7C), SLOT1 (flt_F5B21C), [weapon]
    bool                                    valid = false;
};

// STATELESS resolver (refs passed at call time) — avoids adding a member to WorldRenderer.h
// (NOT editable). Modeled on Char_RenderModel 0x527020.
//
// LIVE ANIMATION (front F_PLAYERANIM, 2026-07-17) — params added:
//   animState     = FSM state entity+244 = CharAnimState::state (populated from the network,
//                   Game/EntityManager.cpp:390 = body+220). Clip SELECTOR: passed AS-IS to
//                   PcModel_ResolveEquipSlot 0x4E46A0 (base + 156*state), replacing the old fixed 0.
//   animCursor    = cursor entity+248 = CharAnimState::animFrame, advanced by
//                   game::Player_AdvanceAnimCursor (Game/PlayerAnimCursorTick.h) in the UPDATE phase.
//   hasAnimCursor = game::Player_AnimCursorTickIsWired(): true ONLY if the cursor is actually
//                   advancing (anti-regression guard, see .cpp) -> SampleByCursor; otherwise
//                   falls back to SampleByGameTime (correct clip via animState, global clock rate).
// Same exact pattern as monsters/NPCs (Scene/WorldRenderer.cpp renderOne: animType always
// passed, only SampleByCursor vs SampleByGameTime is gated by hasAnimCursor).
class PlayerPaperdoll {
public:
    // weaponPose (G5, DEEP IDA render) = a4 of PcModel_ResolveEquipSlot 0x4E46A0 @0x4e578e =
    // animSlot (entity+240 = body+216 = 2*weaponClass). Selects the WEAPON POSE MOTION set
    // (base + 19968*weaponPose). 0 = unarmed pose (fallback). MUST match the value passed to
    // WorldPlayerMotionFrameCount (otherwise the cursor wrap and the drawn clip diverge).
    // torsoItemId/legsItemId (G3, DEEP IDA #5 equip-slot-layout): ITEM_INFO item id of the BODY
    // armor pieces drawn in-game by Char_DrawWeaponTrailEffect 0x55E9D0 (body block 0x5616EC):
    //   torso = equip[2] = body+92+8*2 = body+108 (SObject token 003, catalog unk_F5BC3C @0x561bd1)
    //   legs  = equip[5] = body+92+8*5 = body+132 (SObject token 004, catalog unk_F7147C @0x561e47)
    // mesh variant = ITEM_INFO(item)+196 (field196, WITHOUT -1 for body armor); 0 = nothing equipped
    // (catalog[0]). 0 -> base body. Without them, the player stays in the base body (nude).
    static PaperdollResult Resolve(ModelCache& models, MotionCache& motions,
                                   int race, int gender, int costume0, int costume1,
                                   uint32_t weaponItemId, uint32_t torsoItemId, uint32_t legsItemId,
                                   int weaponPose,
                                   int animState, float animCursor, bool hasAnimCursor,
                                   float gameTimeSec);
};

} // namespace ts2::gfx

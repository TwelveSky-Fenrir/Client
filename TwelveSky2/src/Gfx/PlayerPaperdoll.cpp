// Gfx/PlayerPaperdoll.cpp — implementation. See PlayerPaperdoll.h for the IDA proof
// (Char_RenderModel 0x527020: v37 shared palette, 2-piece body + hand-bone-skinned weapon).
//
// ///// FLEET C / FRONT C3 — WIRING B2 + B3 (integration, 2026-07-17) /////
// This file CONNECTS two FLEET B modules that were previously unwired at the render site:
//   B2  Gfx/EquipModelResolver.h  — BuildArmorBodyStem (SObject "C..." stem for body pieces,
//        faithful port of SObject_BuildPath 0x4D89C0 case 1) fed to ModelCache::Get (public, EXISTING).
//   B3  Gfx/PlayerMotionSlotResolver.h — ResolvePlayerMotionSlot (meta-switch + guard
//        Motion_IsValidWeaponPose 0x4E3A30 of PcModel_ResolveEquipSlot 0x4E46A0) routes the bone
//        palette through MotionCache::GetForPlayer (public, EXISTING).
// No cache API is modified (FLEET C disjointness constraint).
#include "Gfx/PlayerPaperdoll.h"
#include "Gfx/EquipModelResolver.h"        // B2: BuildArmorBodyStem (SObject stem for body pieces)
#include "Gfx/PlayerMotionSlotResolver.h"  // B3: ResolvePlayerMotionSlot (guard + player MOTION slot)
#include "Game/GameDatabase.h"             // game::GetItemInfo (weapon model resolution via ITEM_INFO)
#include <cstdio>                          // snprintf (base body torso/legs stems)

namespace ts2::gfx {

PaperdollResult PlayerPaperdoll::Resolve(ModelCache& models, MotionCache& motions,
                                         int race, int gender, int costume0, int costume1,
                                         uint32_t weaponItemId, uint32_t torsoItemId, uint32_t legsItemId,
                                         int weaponPose,
                                         int animState, float animCursor, bool hasAnimCursor,
                                         float gameTimeSec) {
    PaperdollResult r;

    // ===========================================================================
    // 1) SHARED bone palette — B3: ResolvePlayerMotionSlot 0x4E46A0.
    // ===========================================================================
    // Char_RenderModel 0x527020 resolves v37 via:
    //     v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, a4[9], a4[11], a5, a6, 1, 0, 0);  /*0x52705f/0x527544*/
    // -> ARGUMENT ANCHORS (re-checked in IDA this session): a2=race, a3=gender, a4(pose)=Char_RenderModel::a5,
    //    a5(state)=Char_RenderModel::a6, a6=1, a7=0, a8(itemOrSkillId)=0. a8 IS LITERALLY 0 at BOTH
    //    Char_RenderModel call sites -> the weapon-id meta-switch (0x4e46ef..0x4e5708) ALWAYS falls
    //    through to LABEL_152 (category 1, "C" body), never a cat.6 "X" family. Also
    //    a6=1 (<=112) forces ResolveDefaultL152 onto its 1st branch (MakeParam, base 2624960): the
    //    resolved slot is therefore ALWAYS category BodyC (folder 001), variant=pose(=0), state=animState.
    //
    // WIRING CONSEQUENCE (fidelity + "do not change the MotionCache API" constraint): since the
    // slot is ALWAYS category 1 "C", the stem "C%03d%03d%03d" produced by ResolvePlayerMotionSlot
    // (PlayerMotionSlot::BuildStem, folder motionFolder()="001") is BIT-FOR-BIT the one built by
    // MotionCache::GetForPlayer 0x4E46A0 (BuildPlayerMotionStem: "C%03d%03d%03d" % (race+3*gender+1,
    // weaponType+1, animState+1)). We therefore route through the PUBLIC GetForPlayer, passing it the
    // decomposed slot indices (weaponIndex/stateIndex) -- MotionCache::Get(stem, folder) is PRIVATE, we
    // cannot call it directly and are not allowed to open up its API.
    //
    // WHAT B3 ADDS relative to the old `GetForPlayer(race, gender, 0, animState)`: the
    // Motion_IsValidWeaponPose 0x4E3A30 GUARD (0x4e46cc). For an INVALID (pose=0, animState) pair (e.g.
    // the attack states 4..9/42..46/... that require a non-zero weapon pose), 0x4E46A0 returns the
    // ABSOLUTE fallback idle slot `this + 2644772` = "C001001128" (race0/gen0/wp0/state127, 0x4e46dd) instead
    // of building an out-of-table "C{kind}001{state+1}" stem. For common LOCOMOTION states
    // (0,1,2,3,10,11,12..) the guard passes and the result is STRICTLY IDENTICAL to the old code
    // (MakeParam, wp=0). B3 is therefore a faithful superset: identical when valid, proven idle when
    // invalid (never an out-of-table stem). weaponPose=0 assumed (the actual in-game a4=pose comes from
    // the a8 switch, not ported) -- SAME fallback as the old weaponType=0, not an invention.
    const gfx::PlayerMotionSlot ms = gfx::ResolvePlayerMotionSlot(
        race, gender, weaponPose, animState, /*ctxA6=*/1, /*ctxA7=*/0, /*itemOrSkillId=*/0);

    if (ms.category == gfx::PlayerMotionCategory::BodyC) {
        // Category 1 "C" (the only reachable case here, a8=0) -> public GetForPlayer, identical stem.
        if (const gfx::MotionPalette* mp =
                motions.GetForPlayer(ms.race, ms.gender, ms.weaponIndex, ms.stateIndex)) {
            // REAL per-entity cursor (entity+248) ONLY if it is actually advancing
            // (game::Player_AnimCursorTickIsWired via hasAnimCursor): SampleByCursor reproduces the
            // faithful draw path (frame = ftol(animTime), wraps by subtraction — Char_TickMoveState
            // 0x574830 @0x574922). Otherwise ASSUMED fallback to the global clock (SampleByGameTime): without
            // the cursor advancing, wiring SampleByCursor(0) would freeze ALL players at frame 0 (MANDATORY
            // anti-regression guard, see header).
            r.palette = hasAnimCursor
                ? MotionCache::SampleByCursor(*mp, animCursor)      // entity+248 cursor (0x574922)
                : MotionCache::SampleByGameTime(*mp, gameTimeSec);  // global-clock fallback (not wired)
        }
        // else r.palette stays invalid (identity on the MeshRenderer side) — honest degradation, no invention.
    }
    // else: category WeaponSkillX (folder 006, "X..." stem) -> UNREACHABLE at this site (a8=0). No PUBLIC
    // MotionCache accessor produces an "X" stem/folder 006 (only GetForPlayer exists, folder
    // 001 "C"), and MotionCache::Get(stem, folder) is PRIVATE. TODO-anchor [0x4E46A0 cat.6]: wiring the
    // "X" weapon clips would require a new public MotionCache entry point (out of C3 scope:
    // forbidden to modify the cache APIs) -> identity palette fallback, never an invented stem.

    // ===========================================================================
    // 2) 2-piece base body — B2: BuildArmorBodyStem -> ModelCache::Get (public).
    // ===========================================================================
    // Char_RenderModel 0x527020 (branch a3==0) draws the base body via:
    //     SObject_DrawEx(&flt_F59A7C[504*race + 252*gender + 36*a4[12]], .., v37, 1);  /*0x5277b0*/  (SLOT0)
    //     SObject_DrawEx(&flt_F5B21C[216*race + 108*gender + 36*a4[13]], .., v37, 1);  /*0x52780b*/  (SLOT1)
    // flt_F59A7C / flt_F5B21C are pre-generated by AssetMgr_InitAllSlots 0x4DEB50 by calling
    // SObject_BuildPath 0x4D89C0 case 1: SLOT0 = "C%03d001%03d", SLOT1 = "C%03d002%03d" (kind+1,
    // variant+1). This is EXACTLY BuildArmorBodyStem(race, gender, Base0/Base1, costumeN) (B2). The
    // resulting stem is BIT-FOR-BIT the one from ModelCache::BuildPlayerBodyStem (the old
    // GetForPlayerBody path) -> now routed through B2 without changing the render output.
    //
    // Variant bounds PRESERVED from the old GetForPlayerBody (costume0 in [0,7) flt_F59A7C catalog,
    // costume1 in [0,3) flt_F5B21C catalog, see Gfx/ModelCache.h::BuildPlayerBodyStem): these are
    // DEFENSIVE ClientSource guards (the binary does not bound the variant) — this existing behavior
    // is preserved. Out of bounds -> piece not resolved (as before), never a garbage stem.
    if (costume0 >= 0 && costume0 < 7) {
        const std::string s0 = gfx::BuildArmorBodyStem(race, gender, gfx::EquipBodySlot::Base0, costume0);
        if (const gfx::SkinnedModel* m = models.Get(s0))       // ModelCache::Get (public, EXISTING)
            if (!m->Empty()) r.pieces.push_back(m);            // SLOT0 (flt_F59A7C)
    }
    if (costume1 >= 0 && costume1 < 3) {
        const std::string s1 = gfx::BuildArmorBodyStem(race, gender, gfx::EquipBodySlot::Base1, costume1);
        if (const gfx::SkinnedModel* m = models.Get(s1))
            if (!m->Empty()) r.pieces.push_back(m);            // SLOT1 (flt_F5B21C)
    }

    // ===========================================================================
    // 2-bis) BASE body: TORSO (token 003) + LEGS (token 004) — FIX for the "IN-WORLD
    //        head-only character" bug. Without this block, PlayerPaperdoll only produced
    //        SLOT0(001=face) + SLOT1(002=hair) = a FLOATING HEAD (proven at runtime via -worldtest:
    //        only C001001001/C001002001 loaded). Char_RenderModel 0x527020 (branch a3==0) AND
    //        CharPreview3D::BuildFromRecord ALSO draw the body via EquipA(003)/EquipB(004) at
    //        entry 0 when nothing is equipped -> stems "C%03d003000" / "C%03d004000" (kind =
    //        race+3*gender+1, SObject_BuildPath 0x4D89C0 case 1). SAME shared v37 palette + SAME
    //        transform as the rest. (TODO G3: entry = ITEM_INFO+196 of the equipped item instead of 0,
    //        via equip[k]=body+92+8*k — DEEP IDA render/agent 6.)
    // ===========================================================================
    if (race >= 0 && race < 3 && gender >= 0 && gender < 2) {
        // G3 (DEEP IDA #5): the TORSO (equip[2], token 003) and LEGS (equip[5], token 004) are
        // drawn from catalog[variantEff] where variantEff = ITEM_INFO(item)+196 (WITHOUT -1 for body
        // armor, @0x561bd1/@0x561e47), or 0 if nothing equipped. SObject_BuildPath 0x4d8ba7 names the file
        // "C%03d{token}%03d" % (kind, variantEff+1) -> suffix = variantEff+1 (base=001, NOT 000; the
        // AssetMgr catalog[0] entry is file ...001). kind = race+3*gender+1 (art selector body+64 !=3,
        // common case; ==3 -> kind+7 = TODO). Item not equipped -> base body (variantEff 0 -> suffix 001).
        const int kind = race + 3 * gender + 1;
        auto bodyArmorVariant = [](uint32_t itemId) -> int {
            if (itemId == 0) return 0;                                   // catalog[0] = base body
            if (const game::ItemInfo* it = game::GetItemInfo(itemId))
                return static_cast<int>(it->field196);                   // variantEff = field196 (no -1)
            return 0;
        };
        char sTorso[16] = {}, sLegs[16] = {};
        std::snprintf(sTorso, sizeof(sTorso), "C%03d003%03d", kind, bodyArmorVariant(torsoItemId) + 1); // token 003
        std::snprintf(sLegs,  sizeof(sLegs),  "C%03d004%03d", kind, bodyArmorVariant(legsItemId)  + 1); // token 004
        if (const gfx::SkinnedModel* m = models.Get(sTorso)) if (!m->Empty()) r.pieces.push_back(m);
        if (const gfx::SkinnedModel* m = models.Get(sLegs))  if (!m->Empty()) r.pieces.push_back(m);
    }

    // ===========================================================================
    // 3) Weapon — fallback to current behavior (GetForItem), catalog not traced.
    // ===========================================================================
    // Char_RenderModel 0x527bfe draws the weapon via catalog flt_100EA3C indexed by
    // *(itemRec+196) (= ITEM_INFO.field196, the item-id->modelIndex link):
    //     SObject_DrawEx(&flt_100EA3C[1440*race - 36 + 720*gender + 36*(*(v33+196))], .., v37, 1);
    // flt_100EA3C is a pre-generated cat.6 "W..." SObject catalog whose population law
    // (AssetMgr_InitAllSlots, cat.6 loop) IS NOT TRACED -> field196 cannot be inverted back to the
    // (type, subType, level) triplet that BuildWeaponStem (B2) expects. DOCUMENTED FALLBACK: keep the
    // current resolution via ITEM_INFO.model[0] (+29) through ModelCache::GetForItem(item, 0), SAME
    // transform + SAME v37 palette as the body (weapon skinned to the hand bone). No stem invention.
    // TODO-anchor [0x527bfe / flt_100EA3C / 0x4DEB50 cat.6]: trace the weapon catalog's population
    // law to resolve exactly like the binary (field196 -> flt_100EA3C entry).
    if (weaponItemId != 0) {
        if (const game::ItemInfo* it = game::GetItemInfo(weaponItemId))
            if (const gfx::SkinnedModel* w = models.GetForItem(*it, /*slot=*/0))
                if (!w->Empty()) r.pieces.push_back(w);
    }

    // ===========================================================================
    // TODO-anchor [Char_RenderModel 0x527020, body slots 2 / 14..21 + accessories]:
    // The equipment pieces (helmet a4[34]->flt_F6685C=Slot14, torso a4[46]->flt_F7C09C=Slot15, ...
    // slots 14..21 via BuildArmorBodyStem(race, gender, Slot14..21, *(itemRec+196))) are NOW
    // resolvable on the STEM side (B2 provides BuildArmorBodyStem for 14..21) -- but the DATA is
    // missing:
    //   * Char_RenderModel operates on the CHARSELECT structure (a4[30..74]); the IDB proves it is
    //     called ONLY from Scene_CharSelectRender, NEVER in-game (in-game player body drawing is
    //     not statically located). The IN-GAME layout of equipped item ids is therefore NOT
    //     proven: on the ClientSource side, only the WEAPON is reversed (self=SelfState::equip[7],
    //     remote= PlayerEntity::body+148, see GameState.h); the helmet/torso/etc. slots of REMOTE
    //     players have no proven offset in the network body (600 B), and the SelfState::equip[k] ->
    //     paperdoll slot (14..21) mapping is not traced either.
    // -> Resolving 14..21 would require INVENTING either those offsets or that mapping: out of
    //    scope (the "IDA is the sole source of truth" rule). The paperdoll is therefore left as
    //    2-piece body + weapon, matching current behavior. The B2 pipeline is ready (BuildArmorBodyStem
    //    handles 14..21): wiring these slots will only need the data source (per-slot item id) once reversed.
    // ===========================================================================

    r.valid = !r.pieces.empty();
    return r;
}

} // namespace ts2::gfx

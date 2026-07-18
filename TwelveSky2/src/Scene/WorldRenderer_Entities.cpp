// Scene/WorldRenderer_Entities.cpp — per-entity draw (body + weapon trail) and the
// screen-projection helpers it shares with the nameplate pass, split from
// WorldRenderer.cpp (see WorldRenderer.h for the exact scope of the wiring).
#include "Scene/WorldRenderer.h"
#include "Gfx/Renderer.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/NameplateLogic.h"
#include "Game/NpcInteraction.h"  // game::Npc_GetNameplateColor (0x540790) — MONSTER nameplate color (W9)
#include "Game/ClientRuntime.h"   // game::Str (StrTable005_Get 0x4C1D10) — NameplateHost::ResolveString (W9)
#include "Game/ExtraDatabases.h"  // game::NpcDefRecord (NPC label: name@+4, fieldF[1]@+1332) — W9
// world::World_PickEntityAtCursor (0x538AB0) + world::BuildScreenPickCamera — W9.
// LINK DEPENDENCY FOR ORCHESTRATOR: src\World\TerrainPicker.cpp/.h exist but aren't listed in
// TwelveSky2.vcxproj/.vcxproj.filters (0 "TerrainPicker" hits among 138 explicit <ClCompile>,
// unlike World\WorldMap.cpp/World\WorldIntegration.cpp) -> until added, drawNameplatePass() won't
// link (unresolved external: world::World_PickEntityAtCursor, world::BuildScreenPickCamera).
// Out of .vcxproj scope here; reimplementing hit-test would duplicate the 5 ported Scene_RayHit*
// (0x5415E0/0x541680/0x541780/0x5418B0/0x541920) or invent picking (forbidden).
#include "World/TerrainPicker.h"
#include "World/WorldIntegration.h" // F_ENTITY3D (B8): world::WorldAssets + collision::GroundPlane (shadow ground plane)
#include "Config/GameOptions.h"   // config::g_Options (g_Opt_ShowHitMarkers/ShowNameplates 0x84DED0/D4) — W9
#include "Game/StaticNpcLoader.h" // decor NPCs (mission "PNJ DECOR VISIBLES A L'ECRAN", cf. Render())
#include "Game/AnimationTick.h"       // ZoneNpc_AnimTickIsWired() / Monster_MotionTickIsWired() / IMotionFrameCountOracle — W7
#include "Game/PlayerAnimCursorTick.h" // Player_AnimCursorTickIsWired() (player cursor) — front F_PLAYERANIM
#include "Game/EntityLifecycleTick.h" // g_MonsterTickExt (per-monster motionState/animFrame) — W7
#include "Gfx/MotionCache.h"      // animated bone palette (mirror of g_ModelMotionArray 0x8E8B30) — W3-F1
#include "Gfx/PlayerPaperdoll.h"  // player paperdoll (Char_RenderModel 0x527020 layer) — W3-F1
#include "Game/WeaponTrailResolver.h" // weapon trail: id->v6 switch + stems (front F_WEAPONTRAIL)
#include "Core/Log.h"
#include <cstring>

#include "Scene/WorldRenderer_Internal.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2 {

// ===========================================================================
//  Nameplates (screen projection + gfx::Font)
// ===========================================================================

bool WorldRenderer::worldToScreen(const D3DXVECTOR3& world, const D3DXMATRIX& viewProj,
                                  int& sx, int& sy) const {
    D3DXVECTOR4 clip;
    D3DXVec3Transform(&clip, &world, &viewProj);
    if (clip.w <= 0.001f) return false; // behind the camera (or at eye level)

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    // Generous margin: allow a bit of off-screen overshoot rather than clipping too early a
    // name whose only half would exceed the frame.
    if (ndcX < -1.5f || ndcX > 1.5f || ndcY < -1.5f || ndcY > 1.5f) return false;

    sx = static_cast<int>((ndcX * 0.5f + 0.5f) * static_cast<float>(screenW_));
    sy = static_cast<int>((1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(screenH_));
    return true;
}

void WorldRenderer::drawEntityLabel(const std::string& text, const D3DXVECTOR3& worldPos,
                                    D3DCOLOR color, const D3DXMATRIX& viewProj) {
    if (text.empty() || !font_.Ready()) return;
    int sx = 0, sy = 0;
    if (!worldToScreen(worldPos, viewProj, sx, sy)) return;
    const int w = font_.MeasureText(text.c_str());
    font_.DrawTextStyled(text.c_str(), sx - w / 2, sy, color, gfx::kStyleOutline);
}

// ===========================================================================
//  Weapon trail — shared opaque/shadow resolution (front F_WEAPONTRAIL)
//  Char_DrawWeaponEffectVariantB 0x56BF90 (id->v6 switch @0x56c001, state gate @0x56c411) /
//  Char_DrawWeaponTrailEffect 0x55E9D0 (same switch, opaque primitive). Cf. WorldRenderer.h.
// ===========================================================================

bool WorldRenderer::resolveWeaponTrail(const DrawableEntity& ent,
                                       const gfx::SkinnedModel*& outModel,
                                       gfx::BonePalette& outPalette) {
    outModel   = nullptr;
    outPalette = gfx::BonePalette{}; // identity fallback (invalid) by default

    // (1) Trail = PLAYER effect only (0x55E9D0/0x56BF90 loop over g_EntityArray).
    if (!ent.hasBody || !modelCache_) return false;
    // (2) Master gate @0x56c01b: weaponAnimSlot != 0 AND !altWeaponSet (this+55 / this+144).
    if (ent.weaponAnimSlot == 0 || ent.altWeaponSet) return false;
    // (3) Anim-id switch -> effect index v6 ∈ [0,41] (transcribed from 0x56BF90).
    const int v6 = game::ResolveWeaponTrailIndex(ent.weaponAnimSlot);
    if (v6 < 0) return false;
    // (4) Action-state gate (this+61 = entity+244 = CharAnimState::state, carried in animType)
    //     -> motion sub-block 0/1/2, or -1 (no drawing).
    const int motionSub = game::ResolveWeaponTrailMotionSub(ent.animType);
    if (motionSub < 0) return false;

    // Motion F/cat.5 of the effect (unk_F54DB4/E50/EEC + 468*v6). Sub-block 2 is drawn ONLY if
    // frameCount>=1 (Motion_GetFrameCount @0x56c43e); sub-blocks 0/1 are unconditional.
    const gfx::MotionPalette* mp = Motions().GetForWeaponTrail(v6, motionSub);
    if (game::WeaponTrailMotionSubIsFrameGated(motionSub) && (!mp || mp->frameCount < 1))
        return false;

    // (5) Effect SObject (stem "Y%03d001" cat.9, resolved via ModelCache::Get). No model on disk
    //     -> no trail (SObject_Load would fail the same way in the binary).
    const gfx::SkinnedModel* model = modelCache_->Get(game::BuildWeaponTrailStem(v6));
    if (!model || model->Empty()) return false;

    // Bone palette: same cursor (entity+248 = this+62 = animCursor) as the body. Identity fallback
    // if no motion resolves (sub-blocks 0/1) — SampleByCursor/SampleByGameTime yield an invalid
    // BonePalette in that case, so DrawModel falls back to identityPalette_.
    if (mp) {
        outPalette = ent.hasAnimCursor
                        ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)
                        : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    }
    outModel = model;
    return true;
}

// ===========================================================================
//  Rendering one entity (EntityDrawLogic decides, NameplateLogic names)
// ===========================================================================

void WorldRenderer::renderOne(const DrawableEntity& ent, const game::DrawCullContext& cull,
                              const D3DXMATRIX& view, const D3DXMATRIX& proj,
                              const D3DXMATRIX& viewProj) {
    // Char_Draw 0x5805C0: a2=1 (main pass, cf. Scene_InGameRender). renderOne draws ONLY the body:
    // the planar shadow (ex-showReflection) is now a DEDICATED pass, drawn BEFORE the bodies
    // (renderPlanarShadows, front F_ENTITY3D / B8); the label lives in drawNameplatePass.
    // showShadow (volume 0x580CE0) remains dead code, never drawn.
    const game::EntityDrawFlags flags = game::ComputeEntityDrawFlags(ent.renderState, cull, /*drawPass=*/1);
    if (!flags.showBody) return; // inactive or outside the near-cull guard (IsBeyondCameraNearCull)

    const game::BodyMeshPlacement placement = game::ComputeBodyMeshPlacement(ent.renderState);
    const D3DXVECTOR3 pos(placement.pos.x, placement.pos.y, placement.pos.z);
    const float scale = (placement.scale > 0.0f) ? placement.scale : 1.0f; // placeholder fallback

    // Body: monster -> real model if resolved (replaces the cube); player -> real base
    // body (SLOT0+SLOT1, race/gender/costume, cf. WorldRenderer.h "PLAYERS" banner) if
    // resolved (replaces the cube); DECOR NPC (ent.npcDef non-null, cf. WorldRenderer.h
    // §"NPC" banner) -> real model via ResolveNpcModel if resolved (replaces the cube);
    // GAMEPLAY NPC (ent.npcDef null) -> no known body model -> cube every time.
    // monsterDefId and npcDef are never both set at once (cf. DrawableEntity), monsterDefId
    // takes priority by construction here.
    // bodyModel: MONSTER / DECOR NPC only (players go through the paperdoll below). Also
    // kept for the reflection pass further down (Char_DrawReflection 0x581090, monsters
    // only). For a player (monsterDefId==0 && npcDef==null) -> nullptr.
    const gfx::SkinnedModel* bodyModel = (ent.monsterDefId != 0)
        ? ResolveMonsterModel(ent.monsterDefId)
        : ResolveNpcModel(ent.npcDef);

    const D3DXVECTOR3 rotDeg(0.0f, placement.angle, 0.0f);
    const D3DXVECTOR3 scaleVec(scale, scale, scale);

    // ANIMATED bone palette — Char_Draw 0x5805C0 / Npc_DrawMesh 0x57FF00 -> SObject_DrawEx 0x4D9330
    // (Motion_GetData 0x4D78C0 = motionSlot+136) -> Model_Render 0x40EBB0 (frame = ftol(animTime),
    // clamped 0..frameCount-1). Per-entity animType/cursor (Pass 4 / wave W7, front motion-anim,
    // gaps as-motion-01/02): animType = monster slot+24 (@0x580770, Model_GetNpcMotionSlot arg 3) /
    // NPC slot+12 (@0x57ffa0, Model_GetNpcMeshSlot arg 3); animCursor = monster slot+28 (@0x580828) /
    // NPC slot+16 (@0x57fff1), accumulated by the entity's own tick (`+= dt*30`), never by
    // g_GameTimeSec. Scene_InGameUpdate 0x52C600 proves 4 disjoint families: @0x52c96d/@0x52c9fd
    // players (Char_UpdateAnimationFrame 0x571880), @0x52ca4c decor NPC (Npc_RenderSlotTick
    // 0x5803A0), @0x52cad6 monsters (Char_Update 0x581E10). Ports: Game/AnimationTick.h §5
    // (monsters) / §6 (NPC).
    //
    // hasAnimCursor=false (PLAYERS) -> SampleByGameTime fallback, unchanged: their real cursor
    // depends on the unported 0x5727BF terminal switch (55 handlers); wiring it would freeze it at 0.
    gfx::BonePalette palette; // identity fallback if no MOTION resolves
    if (ent.monsterDefId != 0) {
        // Model_GetNpcMotionSlot 0x4E5960 (monster, stride 3276) — arg 3 = per-entity animType.
        if (const gfx::MotionPalette* mp = Motions().GetForMonster(ent.monsterDefId, ent.animType))
            palette = ent.hasAnimCursor
                        ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)      // @0x580828
                        : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    } else if (ent.npcDef) {
        // Model_GetNpcMeshSlot 0x4E5910 (decor NPC, stride 468) — arg 3 = per-entity animType.
        if (const gfx::MotionPalette* mp = Motions().GetForNpc(*ent.npcDef, ent.animType))
            palette = ent.hasAnimCursor
                        ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)      // @0x57fff1
                        : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    }
    // TODO [anchor Char_Draw 0x5805C0 @0x580776]: case `*((_DWORD*)this + 53)` (slot+212 =
    // MonsterTickExt::fallActive) NOT implemented — when a monster is falling/knocked back, the
    // binary draws with animTime = 0.0 (frozen pose) and reads pos/rot from slot+240/+252
    // (fallOffX/Y/Z, @0x5807d1) instead of slot+32/+56. Outside this front's 2 gaps, and the
    // knockback physics itself isn't ported (cf. Game/AnimationTick.cpp, TODO knockback).

    // GAMEPLAY NPC: bodyMeshEligible=false -> NO body, NO cube (the original never draws a mesh
    // for dword_17AB534, cf. DrawableEntity::bodyMeshEligible). They also have NO label in the
    // original client (Char_DrawNameTag 0x583470 = dead code, click category 6 = no drawing — cf.
    // drawNameplatePass, case 6): this loop therefore emits nothing at all for them.

    // WEAPON TRAIL — OPAQUE pass (Char_DrawWeaponTrailEffect 0x55E9D0 -> SObject_DrawEx 0x4D9330 ->
    // Model_Render 0x40EBB0), drawn BEFORE the body: in 0x55E9D0 the trail block (switch @0x55EAxx)
    // precedes the body drawing (flt_F59A7C/F5B21C @0x561750/0x561949) — same order as in 0x56BF90
    // (trail first). Same transform as the body (pos=this+63, cap=this+69=rotDeg.y, scaleVec;
    // animTime=this+62=animCursor via the palette). resolveWeaponTrail() self-gates on hasBody ->
    // no-op for monsters/NPCs; strict gate (weaponAnimSlot/altWeaponSet/v6/state) => no permanent
    // trail. Independent of the body resolution (emitted even if the body falls back to the cube).
    // Warning: weaponAnimSlot isn't fed from the network yet -> no trail in practice (cf. header).
    {
        const gfx::SkinnedModel* trailModel = nullptr;
        gfx::BonePalette trailPalette;
        if (resolveWeaponTrail(ent, trailModel, trailPalette))
            meshRenderer_.DrawModel(*trailModel, pos, rotDeg, scaleVec, trailPalette);
    }

    if (ent.bodyMeshEligible) {
        if (ent.hasBody) {
            // PLAYER — PlayerPaperdoll (Char_RenderModel 0x527020 layer): ONE SHARED animated
            // bone palette (PcModel_ResolveEquipSlot 0x4E46A0) + an ordered piece list (body SLOT0
            // flt_F59A7C / SLOT1 flt_F5B21C + weapon). Replaces the old inline 2-piece body AND the
            // old weapon hack (wpos = pos.y + scale*0.6). The weapon is now a piece drawn at the
            // SAME transform + SAME palette as the body (Char_RenderModel 0x527bfe: weapon skinned
            // to the hand bone via v37), not an offset.
            // Front F_PLAYERANIM: animType (entity+244 FSM state) + cursor (entity+248, guarded by
            // hasAnimCursor) -> the player's REAL clip AND cadence (cf. PlayerPaperdoll header).
            // Also passed to the planar shadow pass (flattened silhouette = SAME anim as the body).
            gfx::PaperdollResult pd = gfx::PlayerPaperdoll::Resolve(
                *modelCache_, Motions(), ent.bodyRace, ent.bodyGender,
                ent.bodyCostumeSlot0, ent.bodyCostumeSlot1, ent.weaponItemId,
                ent.torsoItemId, ent.legsItemId, ent.animSlot,
                ent.animType, ent.animCursor, ent.hasAnimCursor,
                game::g_World.gameTimeSec);
            if (pd.valid) {
                for (const gfx::SkinnedModel* piece : pd.pieces)
                    meshRenderer_.DrawModel(*piece, pos, rotDeg, scaleVec, pd.palette);
            } else {
                // Fallback: neither body nor weapon resolved -> debug net (cf. drawPlaceholderCube).
                drawPlaceholderCube(pos, scale, ent.placeholderColor, rotDeg.y, view, proj);
            }
        } else if (bodyModel && !bodyModel->Empty()) {
            // MONSTER / DECOR NPC — real model + animated palette resolved above.
            meshRenderer_.DrawModel(*bodyModel, pos, rotDeg, scaleVec, palette);
        } else {
            // Visual traceability even without the real model (cf. WorldRenderer.h): last-resort
            // net when the monster/NPC model didn't resolve. NEVER reached by a gameplay NPC
            // (bodyMeshEligible=false above).
            drawPlaceholderCube(pos, scale, ent.placeholderColor, rotDeg.y, view, proj);
        }
    }

    // FIDELITY NOTE: monster = best-anchored path (Char_Draw 0x5805C0 IS the in-game monster
    // draw). Player = extrapolated from Char_RenderModel 0x527020 (in-game player body draw not
    // statically located) — animated palette applied as an honest choice, better than identity.
    // The old separate weapon overlay (pos.y+0.6 offset, no reversed bone) is REMOVED in favor of
    // the skinned hand attachment (paperdoll).

    // PLANAR SHADOW — MOVED out of renderOne (front F_ENTITY3D / B8). The old "reflection"
    // approximation (drawReflectionOverlay, monsters only, body redrawn without flattening or a
    // bracket) is REPLACED by the real projected shadow: Model_RenderPlanarShadow 0x40F720
    // (D3DXMatrixShadow flattening @0x40FB28, PASS 5 = VS09 + PS NULL), drawn for PLAYERS +
    // MONSTERS + DECOR NPCS in a DEDICATED pass (renderPlanarShadows), bracketed ONCE by the shadow
    // states (GXD_Setup/EndStencilShadowState 0x404F20/0x4050D0) and executed BEFORE the opaque
    // bodies — exactly like the 0x52D9DC..0x52DB15 bracket in Scene_InGameRender (shadows BEFORE
    // opaque). renderOne() therefore now draws ONLY the body (faithful: Char_Draw 0x5805C0 draws
    // neither shadow nor label). ent.reflectionEligible is no longer read here (the showReflection
    // visibility gate is re-evaluated in the shadow pass).

    // REMOVED — Pass 4 / wave W9, front nameplate-entity (gaps HUD-NP-01/02/05): this site used to
    // emit `ComputeNameplateInfo(actor, /*drawMode=*/1, ent.notSelf, vctx, host)` for EVERY
    // player/monster/NPC EVERY frame — a dead binary path (xrefs_to(0x56EF40) = 4 sites, only the
    // a2=1 one @0x52FC02 gated by dword_1668F64 ∈ {1,2}, a global NEVER written; the 3 LIVE sites
    // @0x531052/@0x5310A5/@0x5310F8 pass a2=2 on the single hovered entity) that additionally never
    // drew anything (vctx only had selfX/Y/Z -> optShowHitMarkers=false -> the @0x56F679 gate was
    // FALSE -> mainLine.text empty -> drawEntityLabel() never called), and routed players, monsters,
    // gameplay NPCs AND decor NPCs through the SAME player-flavored ComputeNameplateInfo instead of
    // the binary's 4 disjoint label functions. Now moved to drawNameplatePass() (cf. WorldRenderer.h
    // banner): Char_Draw 0x5805C0 (the body, this function) draws NO label — labels live in the 2D
    // block (Gfx_Begin2D @0x52FB89) of Scene_InGameRender, not in the body dispatcher.
    (void)viewProj; // kept in the signature: renderOne remains the single point for all 4 loops
}

} // namespace ts2

// Scene/WorldRenderer_Shadows.cpp — planar projected shadow pass, split from
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

namespace {

// F_ENTITY3D (B8) — model height (a2 of Model_RenderPlanarShadow 0x40F720) used for the ground-
// plane pick segment (start = pos.y + h @0x40F995) and its reach (maxDist = h·2.5 @0x40FA39).
// ASSUMED GAP, NOT a proven constant: the real height (the SObject's bound) isn't ported in
// ClientSource (EntityRenderInfo::drawSize stays 0 — cf. WorldRenderer.h §LOD). The PLANE found
// (hence the D3DXMatrixShadow projection) does NOT depend on h; h only bounds the search: this
// value (× entity scale) is enough to find the ground under a grounded entity, without inventing
// any geometry. Cf. front report.
constexpr float kShadowModelHeight = 12.0f;

} // namespace

// ===========================================================================
//  Planar projected shadow — Wave B / branch B8 (front F_ENTITY3D)
//  Bracket Scene_InGameRender 0x52D0B0 (0x52D9DC..0x52DB15) + Model_RenderPlanarShadow 0x40F720.
// ===========================================================================
//
// REPLACES the old drawReflectionOverlay() approximation (monsters only, body redrawn at the same
// transform WITHOUT flattening or a state bracket) with the REAL shadow: the ground plane is
// queried (collisionSource_->GetGroundPlaneForShadow 0x40F720), then the skinned body is flattened
// via meshRenderer_.DrawModelPlanarShadow (D3DXMatrixShadow @0x40FB28, PASS 5 = VS09 + PS NULL),
// for the 3 categories the binary shadows inside the bracket: PLAYERS (Char_DrawWeaponEffectVariantB
// @0x52DA41), MONSTERS (Char_DrawReflection @0x52DB09) and DECOR NPCS (Npc_DrawMeshGlow @0x52DAA2).
// Gameplay NPCs (bodyMeshEligible=false) have no body -> no shadow, same as the original.

// GXD_SetupStencilShadowState 0x404F20 — D3D states at the START of the bracket (byte-exact,
// disasm re-checked this session: device=[esi+20Ch]=+524, SetRenderState=+228,
// SetTextureStageState=+268).
void WorldRenderer::beginPlanarShadowBracket() {
    if (!device_) return;
    device_->SetRenderState(D3DRS_LIGHTING, FALSE);                    // (137,0) @0x404F7D
    device_->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_FLAT);           // (9,1)   @0x404F92
    device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);               // (14,0)  @0x404FA7
    device_->SetRenderState(D3DRS_STENCILENABLE, TRUE);              // (52,1)  @0x404FBC
    device_->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);        // (56,3)  @0x404FD1
    device_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);   // (55,7)  @0x404FE6 anti-double-blend mask
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);          // (27,1)  @0x404FFB
    device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);     // (19,5)  @0x405010
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); // (20,6)  @0x405025
    // TEXTUREFACTOR (60): RGB=0, A = (u8)((diffuse.r+diffuse.g+diffuse.b)/3 · 128) << 24 — color =
    // light diffuse (this+1124/1128/1132); /3.0 (dbl_7EDA38) · 128.0 (dbl_7EDA88) @0x405053..0x40507F.
    // -> semi-transparent BLACK shadow. STENCILREF is never set by 0x404F20 -> inherited (not invented).
    const D3DXVECTOR3& d = meshRenderer_.LightDiffuse();
    int a = static_cast<int>(((d.x + d.y + d.z) / 3.0f) * 128.0f);
    if (a < 0)   a = 0;
    if (a > 255) a = 255;
    device_->SetRenderState(D3DRS_TEXTUREFACTOR, static_cast<D3DCOLOR>(a) << 24); // (60,A<<24) @0x40507F
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);        // (0,1,2)  @0x405096
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);            // (0,2,3)  @0x4050AD
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);            // (0,5,3)  @0x4050C6
}

// GXD_EndStencilShadowState 0x4050D0 — restore (byte-exact, SAME order, Docs §5).
void WorldRenderer::endPlanarShadowBracket() {
    if (!device_) return;
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);   // (0,5,2) @0x4050E8
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);   // (0,2,2) @0x4050FF
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE); // (0,1,4) @0x405116
    device_->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);            // (60,-1) @0x40512B
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);             // (20,1)  @0x405140
    device_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_ONE);              // (19,2)  @0x405155
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);            // (27,0)  @0x40516A
    device_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);      // (55,1)  @0x40517F
    device_->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);          // (56,8)  @0x405194
    device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);              // (52,0)  @0x4051A9
    device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);                // (14,1)  @0x4051BE
    device_->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);         // (9,2)   @0x4051D3
    device_->SetRenderState(D3DRS_LIGHTING, TRUE);                     // (137,1) @0x4051ED
}

void WorldRenderer::renderPlanarShadows(const std::vector<DrawableEntity>& drawables,
                                        const game::DrawCullContext& cull) {
    // Prerequisite: ground-plane source set (SetCollisionSource, by MAIN) + device. Clean fallback
    // otherwise — NO drawing, NO invented y=constant plane (rule: "IDA is the sole source of truth").
    if (!collisionSource_ || !device_ || !modelCache_) return;

    // Shadow projection direction = the one DrawModelPlanarShadow injects (negated) into
    // D3DXMatrixShadow; the SAME direction is used for the ground-plane pick -> pick/projection
    // consistency.
    const D3DXVECTOR3& shadowDir = meshRenderer_.ShadowLightDir();
    const float lightDir[3] = { shadowDir.x, shadowDir.y, shadowDir.z };

    // State bracket opened ONCE around all entities (0x52D9DC).
    beginPlanarShadowBracket();

    for (const DrawableEntity& ent : drawables) {
        if (!ent.bodyMeshEligible) continue; // gameplay NPC: no body -> no shadow

        // Visibility = Char_DrawReflection/Char_DrawShadow gate (showReflection = active &&
        // dist(pos, self) <= 300 && camera near-cull) — PROVEN for the MONSTER shadow; reused for
        // players/NPCs (their exact gate lives in the undecompiled this+0xDC mega-switch, cf.
        // WorldRenderer.h §Shadow — documented approximation, not an invented threshold).
        const game::EntityDrawFlags flags =
            game::ComputeEntityDrawFlags(ent.renderState, cull, /*drawPass=*/1);
        if (!flags.showReflection) continue;

        const game::BodyMeshPlacement placement = game::ComputeBodyMeshPlacement(ent.renderState);
        const D3DXVECTOR3 pos(placement.pos.x, placement.pos.y, placement.pos.z);
        const float scale = (placement.scale > 0.0f) ? placement.scale : 1.0f;
        const D3DXVECTOR3 rotDeg(0.0f, placement.angle, 0.0f);
        const D3DXVECTOR3 scaleVec(scale, scale, scale);

        // Ground plane via segment [feet+h -> +shadowDir] (Model_RenderPlanarShadow 0x40F720:
        // Collision_SegPickA 0x420D60 + maxDist = h·2.5 @0x40FA39). h = kShadowModelHeight·scale
        // (GAP documented at the top of this file). Clean fallback if the ground doesn't resolve.
        const float posArr[3]     = { pos.x, pos.y, pos.z };
        const float modelHeight   = kShadowModelHeight * scale;
        world::collision::GroundPlane gp;
        if (!collisionSource_->GetGroundPlaneForShadow(posArr, modelHeight, lightDir,
                                                       modelHeight * 2.5f, gp) || !gp.valid)
            continue;

        // WEAPON TRAIL — PLANAR SHADOW pass (Char_DrawWeaponEffectVariantB 0x56BF90 ->
        // SObject_DrawAnimated2 0x4D91C0 -> Model_RenderPlanarShadow 0x40F720). Drawn BEFORE the
        // body, same as the trail block at the head of 0x56BF90 precedes the body pieces. Same
        // SObject/palette/transform as the opaque trail in renderOne -> consistent flattened
        // silhouette. resolveWeaponTrail() self-gates on hasBody (no-op for monsters/NPCs) and is
        // independent of the body resolution below (the paperdoll's `continue` does not cancel it).
        {
            const gfx::SkinnedModel* trailModel = nullptr;
            gfx::BonePalette trailPalette;
            if (resolveWeaponTrail(ent, trailModel, trailPalette))
                meshRenderer_.DrawModelPlanarShadow(*trailModel, pos, rotDeg, scaleVec,
                                                    trailPalette, gp.shadowPlane);
        }

        // Model(s) + palette resolution — SAME source as renderOne (the body and its shadow share
        // BOTH geometry and transform), so the flattened silhouette matches the body.
        if (ent.hasBody) {
            // PLAYER: paperdoll (body + weapon pieces, shared bone palette). Each piece is
            // flattened separately -> the shadow is the composite (cf. Model_RenderPlanarShadow:
            // one SObject_DrawAnimated2 per piece, each call one silhouette).
            // Front F_PLAYERANIM: animType (entity+244 FSM state) + cursor (entity+248, guarded by
            // hasAnimCursor) -> the player's REAL clip AND cadence (cf. PlayerPaperdoll header).
            // Also passed to the planar shadow pass (flattened silhouette = SAME anim as the body).
            gfx::PaperdollResult pd = gfx::PlayerPaperdoll::Resolve(
                *modelCache_, Motions(), ent.bodyRace, ent.bodyGender,
                ent.bodyCostumeSlot0, ent.bodyCostumeSlot1, ent.weaponItemId,
                ent.torsoItemId, ent.legsItemId, ent.animSlot,
                ent.animType, ent.animCursor, ent.hasAnimCursor,
                game::g_World.gameTimeSec);
            if (!pd.valid) continue; // body not resolved -> no shadow (no invented shadow cube)
            for (const gfx::SkinnedModel* piece : pd.pieces)
                meshRenderer_.DrawModelPlanarShadow(*piece, pos, rotDeg, scaleVec,
                                                    pd.palette, gp.shadowPlane);
        } else {
            // MONSTER / DECOR NPC: one bodyModel + its animated palette (same resolution as
            // renderOne: ResolveMonsterModel/ResolveNpcModel + Motions().GetFor*).
            const gfx::SkinnedModel* bodyModel = (ent.monsterDefId != 0)
                ? ResolveMonsterModel(ent.monsterDefId)
                : ResolveNpcModel(ent.npcDef);
            if (!bodyModel || bodyModel->Empty()) continue; // model not resolved -> no shadow

            gfx::BonePalette palette; // identity fallback if no MOTION resolves (same as renderOne)
            if (ent.monsterDefId != 0) {
                if (const gfx::MotionPalette* mp = Motions().GetForMonster(ent.monsterDefId, ent.animType))
                    palette = ent.hasAnimCursor
                                ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)
                                : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
            } else if (ent.npcDef) {
                if (const gfx::MotionPalette* mp = Motions().GetForNpc(*ent.npcDef, ent.animType))
                    palette = ent.hasAnimCursor
                                ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)
                                : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
            }
            meshRenderer_.DrawModelPlanarShadow(*bodyModel, pos, rotDeg, scaleVec,
                                                palette, gp.shadowPlane);
        }
    }

    // Close the bracket: restore the states (0x52DB15).
    endPlanarShadowBracket();
}

} // namespace ts2

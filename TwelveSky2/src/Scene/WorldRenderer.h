// Scene/WorldRenderer.h — REAL drawing of world entities (milestone 4->5 wiring).
//
// This file reverses NO new function: it WIRES TOGETHER three already-written modules
// that were not connected to each other until now:
//   Gfx/MeshRenderer.h    — knows how to draw a gfx::SkinnedModel (GPU-skinned VB/IB).
//   Gfx/Camera.h          — orbit camera view/projection matrices.
//   Game/EntityDrawLogic.h — decision (draw/not) + placement, NO D3D rendering.
//   Game/NameplateLogic.h  — text + color of the name above the head.
//   Game/GameState.h::g_World — entity source (players/monsters/npcs, all with x/y/z
//                                since this milestone — cf. NpcEntity).
//
// SCOPE OF THIS MISSION (wiring, not new RE):
//   - Entity body: real model via ResolveMonsterModel()/ResolveWeaponModel() (wired to
//     Gfx/ModelCache, cf. banner below) when resolvable; OTHERWISE a colored placeholder
//     cube (D3DXCreateBox + fixed pipeline) — never a blank screen.
//
// ModelCache WIRING (mission "wire ResolveModel() to Gfx/ModelCache", 2026-07-14, cf.
// Gfx/ModelCache.h for the exact signatures) — TODO(model_cache) below RESOLVED for two
// of the three cases:
//   - MONSTERS: 100% resolved. MonsterEntity::body[0] = mob id -> MONSTER_INFO, SAME
//     convention as Game/EntityManager.cpp::ResolveMobDef (raw id, no -1, passed as-is
//     to g_World.db.monster.record()) -> ModelCache::GetForMonster(monsterDefId)
//     resolves kindIndex=MONSTER_INFO.field244-1 internally and returns the real
//     gfx::SkinnedModel (M*.SOBJECT). ENTIRELY replaces the cube when it resolves.
//   - PLAYERS: RESOLVED (mission "base player body wiring", 2026-07-14, cf.
//     Gfx/ModelCache.h::GetForPlayerBody + Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5):
//     PlayerEntity::body+68/+72/+76/+80 (race/gender/costumeSlot0/costumeSlot1) ->
//     ModelCache::GetForPlayerBody(race, gender, costumeSlot0, costumeSlot1) returns the
//     2 pieces (SLOT0+SLOT1) actually drawn by the original player pipeline
//     (Char_DrawWeaponTrailEffect, NOT Char_Draw). These 4 offsets are valid for ANY
//     player in the array (self AND remotes) WITHOUT distinction: unlike the weapon
//     (which has a dedicated, more reactive self global dword_1673248), race/gender/
//     costume have NO separate self global known -- on the contrary, doc §4 shows that
//     Pkt_ItemActionDispatch rewrites gender/costumeSlot0/costumeSlot1 DIRECTLY in
//     entity[0]+96/100/104, hence in this SAME `body` (entity and body share the same
//     memory, body = entity+0x18) -- p.body is therefore already the most up-to-date
//     source for the local player too. ENTIRELY replaces the body cube once at least one
//     of the 2 pieces resolves; the cube remains a last-resort marker if BOTH stems fail
//     (file not found/out of bounds). What remains resolvable in addition to the body:
//     the equipped weapon, wired for BOTH THE LOCAL PLAYER *AND* REMOTE PLAYERS (mission
//     "remote player weapon wiring", 2026-07-14):
//       * Local player (index 0): SelfState::equip[7].itemId (slot 7 = weapon,
//         dword_1673248, cf. Docs/TS2_GAMEPLAY_LOGIC.md §"13 slots ... Slot 7 = weapon"
//         and Game/SkillSystem.cpp:145 "self.equip[7] = dword_1673248") -> GetItemInfo(itemId)
//         -> ModelCache::GetForItem(item, 0). Source already continuously up to date
//         (local equipment updates), preferred over re-reading the network body for this case.
//       * REMOTE players (i!=0): offset RESOLVED in PlayerEntity::body (600 bytes, payload
//         Pkt_SpawnCharacter 0x0f 0x4646c0) -> body+148 (LE u32) = equipped weapon item id,
//         valid for ANY entity in the array (self included, at index 0, but SelfState is
//         kept for that case — cf. above). DECOMPILATION EVIDENCE (twin function pair
//         found in the IDB, MCP idaTs2):
//           - `Weapon_ClassFromEquip` 0x4cc9f0 (SELF ONLY):
//             `*(this+7) = MobDb_GetEntry(mITEM, dword_1673248)` — dword_1673248 IS the
//             self weapon id (same global as Game/SkillSystem.cpp:145).
//           - `Weapon_ClassFromField56` 0x4cc930 (GENERIC, self OR remote):
//             `*(this+7) = MobDb_GetEntry(mITEM, *(a2+56))`, called as
//             `Weapon_ClassFromField56(g_EquipSnapshotScratch, entity+116)` (seen plainly
//             in `Char_AnimEndToIdle_5761A0` 0x57629b, and documented for EVERY active
//             entity via `Char_UpdateAnimationFrame` 0x571880 in RE/gameplay_findings.json:
//             "WeaponClass = Weapon_ClassFromField56 (dword_8E719C, entity+116)") ->
//             a2=entity+116, so `*(a2+56)` = `*(entity+172)`. Identical body (same
//             MobDb_GetEntry + same switch on typeCode@+188) as Weapon_ClassFromEquip =>
//             `entity+172` carries SEMANTICALLY the same "weapon id" field as
//             dword_1673248, but read from the ENTITY itself (hence valid for a remote
//             index, never wired to a self global). `entity+172` = `body+148` since the
//             body starts at `entity+0x18` (24 bytes): `Pkt_SpawnCharacter` 0x4646c0 does
//             `Crt_Memcpy(&dword_168724C[227*i], v8, 600)` with `dword_168724C` =
//             `dword_1687234`(=base)`+0x18`. Hence `entity+172 - 24 = 148`.
//         PlayerEntity::body otherwise contains raw appearance/equipment data at other
//         offsets (name @+16, weaponTypeId @+64, race/gender/costumeSlot0/costumeSlot1
//         @+68/72/76/80 -- RESOLVED, cf. PLAYERS paragraph above --, modelC/D @+84/88
//         still NOT resolved, "equipSnapshot" base @+92 — cf. RE/gameplay_findings.json
//         struct CharEntity) but ONLY the weapon (@+148) and the base body
//         (@+68/72/76/80) have a known resolver on the ClientSource side to date (cf.
//         ModelCache.h); modelC/D and the rest of the equipSnapshot remain out of scope
//         (no invention). GetItemInfo(itemId) -> ModelCache::GetForItem(item, 0), the
//         EXACT same path as for the local player. The real weapon model, when resolved,
//         is drawn as an OVERLAY on the body (real model if resolved, otherwise cube):
//         there's no reversed hand attachment point, so no real skeleton-relative
//         transform -- a simple fixed vertical offset (cf.
//         WorldRenderer_Entities.cpp::renderOne) serves as a visual marker rather than
//         leaving a floating object with no link to the entity.
//   - NPC: TWO DISTINCT ARRAYS, TWO DIFFERENT RESOLUTION STATES (updated for mission
//     "PNJ DECOR VISIBLES A L'ÉCRAN", 2026-07-14):
//       * DECOR NPC (`game::ZoneNpcs()`, Game/StaticNpcLoader.h — client-source EQUIVALENT
//         of `g_NpcRenderArray` 0x1764D14, repopulated locally from the static table
//         `mZONENPCINFO`, cf. StaticNpcLoader.h header banner for the full evidence):
//         RESOLVED. Every `StaticNpcSlot::def` carries a non-null `NpcDefRecord*` (guarded
//         at the source, cf. StaticNpcLoader.cpp) → `ResolveNpcModel()` →
//         `ModelCache::GetForNpc(*def)` (RESOLVED in an earlier mission: `npc.fieldE`
//         +1324 = kindIndex+1 of the visual model N*.SOBJECT, cf. Docs/TS2_NPC_MESH_DRAW.md
//         §2-3 and Gfx/ModelCache.cpp) — a real model replaces the cube as soon as `fieldE`
//         resolves a file on disk. THIS IS THE REAL SOURCE of the decor NPCs (merchants,
//         guards...) visible in-game in the original binary: `Npc_DrawMesh 0x57FF00` reads
//         ONLY `g_NpcRenderArray`.
//       * GAMEPLAY NPC (`game::g_World.npcs`, fed by `Pkt_SpawnNpc` opcode 0x13): ALWAYS
//         unresolved, intentionally. `game::NpcEntity` (Game/GameState.h) carries no usable
//         `NpcDefRecord*`/kindId (`def` stays an untyped `const void*`, no function of the
//         render call graph calls `Char_Draw`/`Npc_DrawMesh` on this array, cf.
//         Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §2) — wiring `ResolveNpcModel` here would
//         require inventing a network-id → `NpcDefRecord` mapping, out of scope (no
//         invention). YELLOW cube kept as-is for this loop, unchanged.
//     Both `Render()` loops share the exact same `renderOne()` pipeline (hence the same
//     YELLOW cube fallback if resolution fails) — only the data source differs.
//   - FIDELITY AUDIT 2026-07-14 (full re-decompilation of Char_Draw 0x5805C0 and
//     Npc_DrawMesh 0x57FF00, cf. Docs/TS2_NPC_MESH_DRAW.md): gap found and FIXED in
//     WorldRenderer.cpp::Render() (NPC loop). Npc_DrawMesh contains a hard far-cull
//     specific to NPCs -- `Math_Dist3D(pos_npc, flt_1687330 /* LOCAL PLAYER position */)
//     > 1000.0 -> immediate return`, BEFORE even the camera near-cull -- which does NOT
//     exist in Char_Draw (verified: no Math_Dist3D call in its disassembly/decompile).
//     game::ComputeEntityDrawFlags only models the Char_Draw pipeline (camera near-cull
//     only, never a far-cull) and is reused as-is for players/monsters/npcs: without an
//     extra guard, an NPC >1000 units from the local player would have been drawn
//     (placeholder cube) even though the original client never does. Fixed by adding this
//     guard BEFORE renderOne in the NPC loop (`kNpcFarCullDistanceSq` constant, ts2
//     anonymous namespace) -- does not touch EntityDrawLogic.cpp (out of this mission's
//     edit scope, so the guard stays localized at the call site rather than in the shared
//     pure function). The camera near-cull (IsBeyondCameraNearCull) and the absence of a
//     far-cull for players/monsters are CONFIRMED FAITHFUL as-is (no change).
//   - DISTANCE CULLING / LOD AUDIT 2026-07-14 (session 2, mission "CULLING DE DISTANCE ET
//     LOD"): re-verification that the camera near-cull (above) and the NPC far-cull
//     (above) do apply to ALL entities, not just the ones near the camera.
//       * Camera near-cull (`IsBeyondCameraNearCull`): CONFIRMED applied UNIFORMLY to the
//         3 loops (players -- self included --, monsters, NPC) via the common
//         `game::ComputeEntityDrawFlags()` call in `renderOne()`: none of `Render()`'s 3
//         loops bypasses it. Faithful to both `Char_Draw` (players/monsters) AND
//         `Npc_DrawMesh` (NPC), which both apply this guard.
//       * 1000-unit far-cull (NPC only): CONFIRMED still correctly SCOPED to the NPC loop
//         alone (`kNpcFarCullDistanceSq`) and absent from the player/monster loops -- this
//         is the correct fidelity (`Char_Draw` has no `Math_Dist3D`, cf. above), NOT a
//         gap: extending this far-cull to players/monsters would be a regression.
//       * MINOR FIDELITY GAP FOUND (not fixed, low impact): `EntityRenderState::info` is
//         NEVER populated by `WorldRenderer::Render()` (`DrawableEntity` has no `info`
//         field) -> `IsBeyondCameraNearCull` always receives `radius=0.0` instead of the
//         real per-entity `info.drawSize`. The guard is still applied identically to ALL
//         entities (so no self/remote/monster/NPC bias), but the original formula's
//         `pos.y + radius*0.5` vertical offset degenerates to `pos.y` for everyone -- the
//         10-unit threshold is slightly less precise vertically. Not fixed here (would
//         require wiring `info.drawSize` per entity type from the `MONSTER_INFO`/
//         `ITEM_INFO`/player-body tables, out of scope for this verification mission).
//       * REAL LOD SYSTEM FOUND IN THE BINARY, BEYOND SIMPLE CULLING (cf.
//         Docs/TS2_GXD_ENGINE.md §2.6/§2.7/§3, EAs already noted in an earlier RE
//         session) -- ENTIRELY NOT WIRED in ClientSource to date:
//           - `Model_Render 0x40EBB0` (called by `ModelObj_Draw 0x4D71B0` for placed
//             skinned models/objects, DOWNSTREAM of the `Char_Draw` dispatcher) does a
//             spherical frustum-cull THEN an LOD "fade" driven by distance/fog, before
//             choosing the sub-mesh to draw (`Model_DrawSkinnedSubset 0x40CA40`) -- this
//             is a RUNTIME LOD system separate from and complementary to the entity
//             near/far-cull already documented above.
//           - The SOBJECT format REALLY carries several geometric detail levels (1 to 4),
//             generated offline by `cMesh_BuildProgressiveLOD 0x43BB00` (D3DXCleanMesh ->
//             WeldVertices -> ValidMesh -> GeneratePMesh -> SetNumFaces, classic D3DX
//             "progressive mesh" pipeline) and serialized by `cMesh_SaveToFileWithLOD
//             0x43AC10`. So these are not invented cubes/LOD: a model's `.SOBJECT` file
//             really embeds several meshes of decreasing detail.
//           - On the ClientSource side, `Gfx/MeshRenderer.h::SkinnedModel/SkinnedMesh`
//             already carries the data structure (`std::vector<SkinnedLod> lods`, one
//             level per "subset" of the `Mesh_ReadFromFile` parser) and
//             `MeshRenderer::DrawModel()` accepts an `int lod` parameter -- BUT **no
//             caller supplies it**. `WorldRenderer_Entities.cpp::renderOne()`
//             systematically calls
//             `meshRenderer_.DrawModel(*model, pos, rotDeg, scaleVec, palette)` WITHOUT a
//             5th argument -> `lod` is always 0 (the MOST detailed level), regardless of
//             the entity's camera/player distance (local, remote, monster or NPC).
//             Concretely: geometry NEVER simplifies with distance in ClientSource, unlike
//             the original binary.
//           - NOT wired intentionally in this mission (verification/documentation, not
//             new RE): the exact formula of `Model_Render`'s LOD "fade" (per-tier
//             distance thresholds, possible dependency on fog/`g_Opt_GfxDetailShadows`)
//             could NOT be re-decompiled live this session (the `idaTs2` MCP server was
//             unavailable/saturated -- concurrent access by other agents of the same
//             wave). Wiring a distance-based LOD selection without this exact formula
//             would be an INVENTED threshold, contrary to the "IDA is the sole source of
//             truth" rule -- left as an explicit TODO for a session with IDA access
//             available: decompile `Model_Render 0x40EBB0` to extract the
//             threshold/fade formula, then compute `lod` in `renderOne()` from
//             `Distance3D(pos, cull.cameraPos)` before the `DrawModel()` call.
//           - Distinct: TEXTURE LOD (`g_TexLodLevel` / `dword_18C4EFC`, 0..3, mip skip in
//             `Tex_ReadPacked 0x417740`) is a GLOBAL quality setting (graphics menu), not
//             a per-distance/per-entity switch -- out of scope for this mission.
//   - Nameplate: real call to game::ComputeNameplateInfo, drawn via gfx::Font
//     (a font owned by WorldRenderer, same pattern as UI/GameHud.h::font_).
//   - Shadow/reflection (EntityDrawLogic::ComputeEntityDrawFlags.showShadow/showReflection).
//
//     ///// REWRITTEN — Pass 4 / wave W5, front shadow-wiring (2026-07-16) /////
//     The previous analysis stopped ONE LEVEL TOO EARLY and concluded "no cast shadow is
//     actually drawn by the original client". This is WRONG, and is fixed here. IDA
//     (re-checked end to end this session) proves TWO twin chains of VOLUME 0x40DC70, one
//     dead, one live:
//
//     PRECISION (Pass 4 / W5b, front shadow-fidelity): "two chains" only counts the twins
//     of volume 0x40DC70 below. The binary contains AT LEAST two OTHER shadow
//     implementations, also DEAD and not counted here:
//         cSObject_RenderWithShadow     0x43D530  (0xA21 bytes, 0 xref)
//         cSObject_RenderWithShadow_Alt 0x43DF60  (0xA21 bytes, 0 xref)
//     — both readers of the direction cache flt_18C53C0/C4/C8 (@0x43D9E0/@0x43DC83 and
//     @0x43E410/@0x43E6B3). Twins of each other (identical sizes), with no caller at all:
//     remnants of an abandoned 3rd/4th variant. Wire nothing from them either.
//
//     [A] STENCIL SHADOW VOLUME — DEAD, UNREACHABLE IN THE BINARY.
//         Char_DrawShadow 0x580CE0 / Npc_DrawMeshShadow 0x5800E0 /
//         Char_DrawWeaponEffectVariantA 0x568FE0 -> SObject_DrawAnimated 0x4D9050 ->
//         Model_RenderWithShadow 0x40EEE0 -> Model_BuildShadowVolume 0x40DC70.
//         All 3 heads have 0 xref EACH, and `find_bytes` of their addresses in
//         little-endian yields 0 occurrences across the WHOLE image (hence no indirect
//         call via vtable/pointer table). Detailed evidence: Gfx/MeshRenderer.cpp, banner
//         above DrawModelShadow(). -> never drawn: CORRECT to not wire it
//         (gfx::MeshRenderer::DrawModelShadow stays without a caller, on purpose).
//
//     [B] PROJECTED PLANAR SHADOW — LIVE, and it's the game's real shadow.
//         Model_RenderPlanarShadow 0x40F720 flattens the model onto the ground plane via
//         j_D3DXMatrixShadow @0x40FB28 (the IDB's comment on the function itself says
//         "render projected ground shadow via D3DXMatrixShadow"), in PASS 5 = VS09
//         (g_GxdSh09_VS) + PS NULL. Ground plane (a,b,c,d) = floats +124/+128/+132/+136 of
//         `a8[40] + 156*hitIdx`, coming from Collision_SegPickA 0x420D60.
//         `reaches(Scene_InGameRender 0x52D0B0 -> 0x40F720)` = true, depth 3.
//         The devs DUPLICATED chain [A] then switched to the planar one, orphaning the
//         volume (twin functions, pairwise identical sizes):
//              DEAD [A]                                LIVE [B]                       size
//              Char_DrawShadow 0x580CE0                Char_DrawReflection 0x581090   0x3A4
//              Npc_DrawMeshShadow 0x5800E0             Npc_DrawMeshGlow 0x5801D0      0xE2
//              Char_DrawWeaponEffectVariantA 0x568FE0  Char_DrawWeaponEffectVariantB 0x56BF90  0x2AFF
//         SObject_DrawAnimated 0x4D9050 and SObject_DrawAnimated2 0x4D91C0 are themselves
//         twins (0x16F each) and differ ONLY by Model_RenderWithShadow vs
//         Model_RenderPlanarShadow (decompilation verified: 0x4D91C0 calls ONLY 0x40F720).
//         => NAMING CONSEQUENCE: `Char_DrawReflection` / `Npc_DrawMeshGlow` are MISNAMED
//         in the IDB. They are neither a reflection nor a glow: they are the (planar)
//         SHADOW DRAWS of the monster and the NPC respectively. (IDB rename not done: IDA
//         is read-only for this front.)
//
//     SCENE BRACKET (Scene_InGameRender 0x52D0B0, disassembly re-read line by line) — the
//     shadow pass is an explicit bracket, BEFORE the opaque render:
//         0x52D9DC  GXD_SetupStencilShadowState(g_GxdRenderer)   <-- START shadow pass
//           loop i<g_EntityCount  : Char_DrawWeaponEffectVariantB(&g_EntityArray[908*i]) @0x52DA41
//           loop i<g_NpcCount     : Npc_DrawMeshGlow(&g_NpcRenderArray[88*i])            @0x52DAA2
//           loop i<g_MonsterCount : Char_DrawReflection(&dword_1766F74[280*i])           @0x52DB09
//         0x52DB15  GXD_EndStencilShadowState(g_GxdRenderer)     <-- END, then the opaque pass
//           (Char_DrawWeaponTrailEffect @0x52DB7C, Npc_DrawMesh @0x52DBDF, ...)
//     So: planar shadows for PLAYERS **and** NPC **and** MONSTERS — not just monsters as
//     the previous writeup assumed.
//
//     CORE OF GXD_SetupStencilShadowState 0x404F20 — SHADOW DIRECTION DERIVATION.
//     (Pass 4 / W5b, front shadow-fidelity: the banner claimed "decompiled, verified" but
//      only transcribed its SetRenderState calls, skipping its 6 FIRST lines, which are
//      precisely the essential part — they COMPUTE flt_18C53C0/C4/C8 every frame.)
//     this = ecx = g_GxdRenderer 0x18C4EF8 (set by `mov ecx, offset g_GxdRenderer` @0x52D9D7,
//     single caller @0x52D9DC) -> esi+4C8h = 0x18C53C0 / +4CCh = 0x18C53C4 / +4D0h = 0x18C53C8.
//       0x404F26  fld  [esi+4A0h]  -> 0x404F2D  fstp [esi+4C8h]   ; x := light.Direction.x
//       0x404F39  fldz             -> 0x404F3C  fstp [esi+4CCh]   ; y := 0.0
//       0x404F43  fld  [esi+4A8h]  -> 0x404F49  fstp [esi+4D0h]   ; z := light.Direction.z
//       0x404F4F  call Vec3_Normalize (0x6BB60C = jmp [0x7A64C8] = D3DXVec3Normalize, in=out=edi)
//       0x404F54  fld  ds:flt_7EDA10 (= 0xBF800000 = -1.0, bytes re-read: 00 00 80 bf)
//                                  -> 0x404F5B  fstp [esi+4CCh]   ; y := -1.0
//       0x404F62  call Vec3_Normalize
//     I.e.:  shadowLightDir = normalize( normalize(L.x, 0, L.z) then .y := -1 )
//     Since the 1st normalization makes the horizontal component unit-length, the norm
//     before the 2nd is ALWAYS sqrt(2) -> y ≡ -1/sqrt(2) ≈ -0.7071: the shadow always falls
//     at 45° regardless of time of day.
//     SOURCE (esi+4A0h): the renderer's light is a D3DLIGHT9 at esi+460h (= +1120, cf.
//     Gfx/MeshRenderer.h); D3DLIGHT9 layout -> Direction@+64, so 0x18C5358+64 = 0x18C5398
//     = esi+4A0h (Direction.x) and esi+4A8h = 0x18C53A0 (Direction.z). Only the light's
//     HORIZONTAL PLANE is reused (y is overwritten): the sun's azimuth orients the shadow,
//     not its elevation. Writer of this light: Env_UpdateSunLight 0x412210
//     (@0x412339/@0x412343: light.Direction = -normalize(sunDir), from
//     cAtmosphere_GetSunDirectionA 0x7904D0), itself also recomputed every frame.
//     WHY xrefs_to(0x18C53C0) SHOWS NO WRITER (a trap not to fall back into): its 7 xrefs
//     are ALL reads (0x40F585, 0x40F9A4, 0x40FB00, 0x43D9E0, 0x43DC83, 0x43E410, 0x43E6B3)
//     because the write is register-relative to esi -> INVISIBLE in an absolute xref.
//     flt_18C53C0/C4/C8 is therefore a recomputed CACHE, not a frozen constant.
//
//     Real states set NEXT by GXD_SetupStencilShadowState 0x404F20 (decompiled, verified):
//       LIGHTING(137)=0, SHADEMODE(9)=FLAT, ZWRITEENABLE(14)=0, STENCILENABLE(52)=1,
//       STENCILFUNC(56)=EQUAL(3), STENCILPASS(55)=INCR(7) (anti-double-blend mask),
//       ALPHABLENDENABLE(27)=1, SRCBLEND(19)=SRCALPHA(5), DESTBLEND(20)=INVSRCALPHA(6),
//       TEXTUREFACTOR(60)=(diffuse average ×128)<<24, TSS0: COLOROP=SELECTARG1,
//       COLORARG1=TFACTOR, ALPHAARG1=TFACTOR.
//     (Correction in passing: the previous writeup gave "DESTBLEND=INVSRCCOLOR" — it's
//      actually INVSRCALPHA(6). And 0x40F720's v37 fade, with fogNear=999999/fogFar=1000000,
//      ALWAYS saturates to 1.0 -> systematic LOD 0.)
//
//     WIRING STATE — UPDATE for front F_ENTITY3D / branch B8 (REAL planar shadow):
//       * showShadow -> STILL no stencil volume: [A] is dead, cf. above.
//       * PLANAR SHADOW [B] -> NOW IMPLEMENTED (this front). The ground-plane source now
//         exists (Wave B4: WorldAssets::GetGroundPlaneForShadow 0x40F720 -> collision::
//         GroundPlane, via Collision_SegPickA 0x420D60). The rendering lives in a
//         DEDICATED pass WorldRenderer::renderPlanarShadows(): state bracket opened once
//         (Begin/End = GXD_Setup/EndStencilShadowState 0x404F20/0x4050D0), ground plane
//         queried per entity, flattening + VS09 in
//         gfx::MeshRenderer::DrawModelPlanarShadow (D3DXMatrixShadow @0x40FB28, PASS 5, PS
//         NULL). EXTENDED TO THE 3 CATEGORIES of the bracket: PLAYERS (@0x52DA41,
//         paperdoll flattened piece by piece), MONSTERS (@0x52DB09), DECOR NPC (@0x52DAA2,
//         via game::ZoneNpcs() = client-source equivalent of g_NpcRenderArray). The old
//         drawReflectionOverlay() approximation (monsters only, no flattening or bracket)
//         is REMOVED. Clean fallback if the plane doesn't resolve OR if
//         SetCollisionSource() hasn't been set (no drawing, no invented y=constant plane).
//         `reflectionEligible` is no longer read (the showReflection visibility gate is
//         re-evaluated in the shadow pass). MAIN WIRING REQUIRED:
//         WorldRenderer::SetCollisionSource(worldAssets_) from Scene/SceneManager.cpp (cf.
//         front report). Without it, the pass is a clean no-op.
//       * B7 — distance-based shadow LOD: NOT implemented BY DESIGN. 0x40F720's v37 fade
//         saturates at 1.0 (fogNear/fogFar = 999999/1000000) -> systematic LOD 0; lod=0 is
//         ALREADY faithful (cf. §LOD above). Enabling it would be unfaithful.
//       * [NOT VERIFIED / DOCUMENTED APPROXIMATION] PLAYER/NPC shadow visibility gate:
//         showReflection is reused (active && dist(self)<=300 && near-cull), PROVEN for
//         the MONSTER shadow (Char_DrawReflection). The exact player (field +0xDC,
//         undecompiled 11 KB mega-switch of Char_DrawWeaponEffectVariantB 0x56BF90) and
//         NPC shadow gates aren't reversed -> an assumed approximation, not an invented
//         threshold.
//       * [NOT VERIFIED] Model height (a2): EntityRenderInfo::drawSize stays 0 (not
//         ported) -> kShadowModelHeight fallback (GAP documented in
//         WorldRenderer_Shadows.cpp). Only used for the pick segment/maxDist; the plane
//         found (hence the projection) doesn't depend on it.
//       * [NOT VERIFIED] STENCILREF during the bracket: never set by 0x404F20 ->
//         inherited (likely 0, the D3D9 default) — NOT invented (not set). Anti-double-
//         blend via STENCILFUNC=EQUAL/STENCILPASS=INCR: inert if the depth-stencil has no
//         stencil bits (clean degradation: slight over-darkening at overlaps, no crash).
//
// TODO(fidelity): PlayerEntity/MonsterEntity (Game/GameState.h) don't yet carry the real
// name/level/scale/angle (network payload not fully decoded on the GameState side) —
// NameplateActor/EntityRenderState are populated with only the available fields
// (position, HP) + fallback values documented at the call site (the relevant
// WorldRenderer*.cpp file). No invented value is ever presented as certain: the visual
// markers (placeholder color/text) stay identifiable as such.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

#include "Gfx/MeshRenderer.h"
#include "Gfx/Camera.h"
#include "Gfx/Font.h"
#include "Gfx/ModelCache.h"
#include "Game/EntityDrawLogic.h"
#include "Game/ExtraDatabases.h" // game::NpcDefRecord (decor NPC, cf. DrawableEntity::npcDef)
#include "Game/AnimationTick.h"  // game::IMotionFrameCountOracle (oracle exposed below) — W7

namespace ts2 {

namespace gfx { class Renderer; }
// F_ENTITY3D (branch B8): source of the planar shadow's GROUND PLANE. Forward-declared
// here (non-owning pointer, collisionSource_ member); the .cpp includes World/WorldIntegration.h.
namespace world { class WorldAssets; }

// Animation frame-count oracle (Pass 4 / wave W7, front motion-anim) — mirrors
// Model_GetMotionFrameCount 0x4E5A70 (monster) / Model_GetWeaponEffectFrameCount 0x4E5A40
// (decor NPC). Both resolve the SAME slot as the draw, so the count must come from the
// SAME cache as the drawn palette: this cache (gfx::MotionCache) is a file-static of the
// WorldRenderer.cpp split family (WorldRenderer.h couldn't receive a member at the time
// this was introduced, cf. Scene/WorldRenderer_Internal.h::Motions()) -- hence this free
// accessor, the only legitimate entry point for the ticks of Game/AnimationTick.h which
// live outside the gfx layer.
// Reference to a process-lifetime singleton: never store it beyond that.
// TO BE WIRED (outside my files): Scene/SceneManager.cpp must pass it to
// game::Monster_DispatchMotionTick and game::ZoneNpc_TickAnim -- without which the
// cursors advance without ever wrapping (clean degradation, cf. Game/AnimationTick.h).
const game::IMotionFrameCountOracle& WorldMotionFrameCountOracle();

// Frame count of a PLAYER's CURRENT CLIP (front F_PLAYERANIM, 2026-07-17) — mirrors
// Motion_GetFrameCount 0x4D7830 returned by PcModel_ResolveSlotAndApply 0x4E5A00, which
// bounds the cursor wrap (Char_TickMoveState 0x574830 @0x574922). Backed by the SAME
// MotionCache (Motions()) as the draw: the wrap's frameCount == the sampled palette's
// frameCount (fidelity — otherwise wrap and render would diverge). weaponType is left at
// 0 by the caller (a8 = an unreversed ~500-case switch, TODO anchor 0x4E46A0). Returns 0
// if the slot doesn't resolve (file absent / bound exceeded) -> game::Player_AdvanceAnimCursor
// then advances without wrapping (unknown duration, clean degradation). There is NO
// "player" method on IMotionFrameCountOracle (interface not owned): hence this dedicated
// free accessor, the sole entry point for the player cursor advance of
// Game/PlayerAnimCursorTick.h which lives outside the gfx layer.
// TO BE WIRED (outside my files): Scene/SceneManager.cpp calls it in UPDATE to supply the
// frameCount to game::Player_AdvanceAnimCursor (cf. front report, integrationForMain).
int WorldPlayerMotionFrameCount(int race, int gender, int weaponType, int animState);

// WorldRenderer — draws the content of game::g_World every InGame frame (players +
// monsters + npcs, cf. banner): body (model or placeholder) + nameplate. A single
// MeshRenderer/Font is shared across every entity of the frame (same pattern as
// ts2::ui::GameHud: one font per component, no global font singleton).
class WorldRenderer {
public:
    ~WorldRenderer() { Shutdown(); }
    WorldRenderer() = default;
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Builds the MeshRenderer (vertex decl + skinned shaders), the placeholder cube
    // (D3DXCreateBox) and the nameplate font. Returns false if the device is null or if
    // MeshRenderer::Init fails (the placeholder cube/font stay best-effort: their failure
    // degrades rendering but doesn't block the overall init).
    bool Init(gfx::Renderer& renderer, int screenW, int screenH);
    void Shutdown();

    void OnDeviceLost();
    void OnDeviceReset();

    // Draws every active entity of game::g_World (players then monsters then npcs) +
    // their nameplates, using `camera`'s matrices. Does nothing if not ready.
    void Render(const gfx::Camera& camera);

    // (GXD_RenderPostBlur 0x4053E0) The bloom/post-blur reads pixel shaders PS12/PS14 of
    // the GXDEffect npk (this+527404/+527468). This ShaderSet — the 12 shaders, loaded by
    // Init() and attached to the MeshRenderer via AttachShaderSet — is its only source on
    // the ClientSource side. MAIN passes it to GxdRenderer::RenderPostBlur from
    // SceneManager (SINGLE original call site @0x52FB53 in Scene_InGameRender 0x52D0B0,
    // after all the 3D, before Gfx_Begin2D @0x52FB89). NON-owning reference: valid as long
    // as this WorldRenderer lives (InGame scene lifetime).
    const gfx::ShaderSet& BloomShaderSet() const { return shaderSet_; }

    // F_ENTITY3D (branch B8) — source of the GROUND PLANE for the projected planar shadow
    // (WorldAssets::GetGroundPlaneForShadow 0x40F720). NON-owning reference, valid as long
    // as the InGame scene's WorldAssets lives (same lifetime as this WorldRenderer). As
    // long as it's NOT set (nullptr), the planar shadow pass is a clean no-op (no drawing,
    // no invented plane). SAME pattern as host.GetGroundHeight: to be wired from
    // Scene/SceneManager.cpp (worldAssets_). Cf. the front report for the exact site.
    void SetCollisionSource(const world::WorldAssets* src) { collisionSource_ = src; }

private:
    // -----------------------------------------------------------------------
    // Real model extension point — WIRED to Gfx/ModelCache (mission "wire
    // ResolveModel()", 2026-07-14, cf. header banner for the full per-entity-type
    // detail). The old single signature ResolveModel(modelCategoryId, motionIndex)
    // matched NO real ModelCache API (which resolves by ITEM_INFO itemId for an
    // item, or by monsterDefId/MONSTER_INFO for a monster — never by a generic
    // category/motion pair): replaced by two entry points aligned on the cache's
    // REAL signatures (cf. Gfx/ModelCache.h::GetForItem/GetForMonster).
    // -----------------------------------------------------------------------
    const gfx::SkinnedModel* ResolveMonsterModel(uint32_t monsterDefId); // monster body
    const gfx::SkinnedModel* ResolveWeaponModel(uint32_t weaponItemId);  // player weapon (self + remote, cf. banner)
    // DECOR NPC body (mission "PNJ DECOR VISIBLES A L'ECRAN", 2026-07-14, cf. header
    // banner §"NPC" for the distinction from the gameplay array): delegates to
    // Gfx/ModelCache.h::GetForNpc(NpcDefRecord&), RESOLVED in an earlier mission
    // (npc.fieldE +1324 = kindIndex+1, cf. ModelCache.cpp). nullptr if npcDef is null or if
    // fieldE is out of bounds [1,66] (cube fallback in renderOne, never an exception).
    const gfx::SkinnedModel* ResolveNpcModel(const game::NpcDefRecord* npcDef);
    // Base player body (SLOT0+SLOT1, cf. "PLAYERS" banner + Gfx/ModelCache.h::
    // GetForPlayerBody). race/gender/costumeSlot0/costumeSlot1 = PlayerEntity::body+68/72/76/80,
    // valid for self AND remotes without distinction (cf. banner).
    gfx::PlayerBodyModel ResolvePlayerBodyModel(int race, int gender, int costumeSlot0, int costumeSlot1);

    bool buildPlaceholderCube(IDirect3DDevice9* dev);
    // rotYDeg (mission ROTATION/ORIENTATION, 2026-07-14): horizontal heading in degrees,
    // SAME convention as MeshRenderer::DrawModel (rotationDeg.y, matrix S*Rz*Ry*Rx*T) --
    // applied even to the placeholder cube so the visual marker stays orientation-
    // consistent with the real model that may later replace it (no notable visual effect
    // on a Y-symmetric cube, but avoids a world-matrix mismatch between the two draw paths).
    void drawPlaceholderCube(const D3DXVECTOR3& pos, float scale, D3DCOLOR color,
                             float rotYDeg, const D3DXMATRIX& view, const D3DXMATRIX& proj);

    bool worldToScreen(const D3DXVECTOR3& world, const D3DXMATRIX& viewProj,
                       int& sx, int& sy) const;
    void drawEntityLabel(const std::string& text, const D3DXVECTOR3& worldPos,
                         D3DCOLOR color, const D3DXMATRIX& viewProj);

    // =======================================================================
    // "Hovered entity label" pass — Pass 4 / wave W9, front nameplate-entity.
    //
    // REPLACES the old nameplate block in renderOne() (one plate PER ENTITY and PER
    // FRAME, drawMode=1), which reproduced a DEAD PATH of the binary and, on top of that,
    // drew NOTHING (empty NameplateViewerContext -> false @0x56F679 gate -> empty
    // mainLine). Cf. Game/NameplateLogic.h's §DRAWMODE for the full evidence.
    //
    // FAITHFUL STRUCTURE: in Scene_InGameRender 0x52D0B0, the body (Char_Draw 0x5805C0)
    // and the label (Char_DrawNameplate 0x56EF40 / Char_DrawOverheadName 0x581440 /
    // Fx_MeleeSwingDrawMarker 0x5802C0 / Obj_DrawNameLabel 0x5840B0) are drawn by
    // DIFFERENT functions, at DIFFERENT places: bodies in the 3D passes, labels in the 2D
    // block opened by Gfx_Begin2D @0x52FB89. renderOne() therefore now draws ONLY the
    // body, and this single pass (called after Render()'s 4 loops) reproduces the 2D
    // block @0x52FB58..0x53120B:
    //     GetPhysicalCursorPos(&pt) @0x52FB5C ; ScreenToClient(hWndParent, &pt) @0x52FB6C
    //     World_PickEntityAtCursor(..., g_Opt_ShowHitMarkers ? 1 : 0) @0x530F7E / @0x530FA6
    //     switch (kind) 8 cases @0x530FC7 -> AT MOST ONE label per frame
    // =======================================================================
    void drawNameplatePass(const gfx::Camera& camera, const game::DrawCullContext& cull,
                           const D3DXMATRIX& viewProj);

    // An entity already normalized for the render loop (players/monsters/npcs all
    // converge here, cf. WorldRenderer.cpp).
    struct DrawableEntity {
        game::EntityRenderState renderState; // EntityDrawLogic view (pos/hp/scaleY/...)
        bool        notSelf = true;          // original a3 (loop index != 0)
        std::string name;                    // REAL name for players (PlayerEntity::name,
                                              // cf. WorldRenderer.cpp::Render); placeholder
                                              // "Monster#i"/"Npc#i" for monsters/NPC (real
                                              // names out of scope, cf. banner)
        D3DCOLOR    placeholderColor = 0xFFFFFFFFu;

        // Real model resolution inputs (mission ModelCache, cf. header banner): 0 = not
        // resolved -> placeholder cube for the corresponding part. A monster with
        // monsterDefId!=0 ENTIRELY replaces the cube; a player with hasBody=true replaces
        // the body-cube with the resolved SLOT0/SLOT1 pieces (cf. "PLAYERS" banner);
        // weaponItemId!=0 ADDS the weapon on top of the body (real model or cube, cf.
        // renderOne). Never both monsterDefId AND hasBody in practice (a monster has no
        // race/gender/costume, a player has no monsterDefId). weaponItemId is populated
        // for EVERY active player (self AND remote, cf. "remote player weapon wiring"
        // banner -> PlayerEntity::body+148 for remotes).
        uint32_t    monsterDefId = 0;
        uint32_t    weaponItemId = 0;
        // G3 (DEEP IDA #5) — equipped BODY armor (players): ITEM_INFO item id of the torso
        // (equip[2]=body+108, token 003) and legs (equip[5]=body+132, token 004). 0 = base
        // body. PlayerPaperdoll::Resolve resolves the variant via ITEM_INFO+196.
        uint32_t    torsoItemId  = 0;
        uint32_t    legsItemId   = 0;

        // WEAPON TRAIL (front F_WEAPONTRAIL, 2026-07-17) — skinned swoosh/glow effect drawn
        // during a cast, PLAYERS ONLY (Char_DrawWeaponTrailEffect 0x55E9D0 opaque /
        // Char_DrawWeaponEffectVariantB 0x56BF90 shadow, both on g_EntityArray). Master
        // gate @0x56c01b: drawn only if weaponAnimSlot != 0 AND !altWeaponSet.
        //   weaponAnimSlot = CharAnimState::weaponAnimSlot (entity+220 = this+55), active
        //                    skill/weapon anim id -> switch game::ResolveWeaponTrailIndex
        //                    -> v6 ∈ [0,41].
        //   altWeaponSet   = CharAnimState::altWeaponSet   (entity+576 = this+144).
        // Populated for players (hasBody); 0/false for monsters/NPC (no trail).
        // Warning: TO DATE weaponAnimSlot/altWeaponSet are NOT fed from the network on the
        // ClientSource side (cf. front report / integrationForMain) -> weaponAnimSlot is 0
        // -> the gate fails -> NO trail is ever emitted (clean degradation, no crash). The
        // wiring becomes effective as soon as MAIN populates these fields (EntityManager,
        // body+196/+552).
        int         weaponAnimSlot = 0;
        bool        altWeaponSet   = false;

        // DECOR NPC (mission "PNJ DECOR VISIBLES A L'ÉCRAN", 2026-07-14, cf. header banner
        // §"NPC"): non-null ONLY for entries of `game::ZoneNpcs()` (StaticNpcLoader) —
        // never for players/monsters/gameplay NPC. Consumed by `ResolveNpcModel()` in
        // `renderOne()`, IN PLACE of `monsterDefId` (the two are never both set at once):
        // `monsterDefId != 0` takes priority, otherwise `npcDef` is tried, otherwise cube
        // fallback.
        const game::NpcDefRecord* npcDef = nullptr;

        // Base player body (mission "base player body wiring", 2026-07-14, cf. "PLAYERS"
        // banner): hasBody=true for every active PlayerEntity (self AND remotes, never for
        // monsters/NPC); race/gender/costumeSlot0/costumeSlot1 = raw read of
        // PlayerEntity::body+68/72/76/80 (cf. WorldRenderer.cpp::Render).
        bool hasBody         = false;
        int  bodyRace        = 0;
        int  bodyGender      = 0;
        int  bodyCostumeSlot0 = 0;
        int  bodyCostumeSlot1 = 0;

        // PER-ENTITY ANIMATION (Pass 4 / wave W7, front motion-anim — gaps as-motion-01
        // "animType frozen at idle" and as-motion-02 "per-entity cursor never consumed").
        // Before: `GetForMonster(defId, /*idle*/0)` + a shared GLOBAL clock -> no entity
        // ever changed animation, and all were animated IN PHASE.
        //   animType   = monster slot +24 (Char_Draw 0x5805C0 @0x580770, arg 3 of
        //                Model_GetNpcMotionSlot) / NPC slot +12 (Npc_DrawMesh 0x57FF00
        //                @0x57ffa0, arg 3 of Model_GetNpcMeshSlot).
        //   animCursor = monster slot +28 (Char_Draw @0x580828, SObject_DrawEx animTime) /
        //                NPC slot +16 (Npc_DrawMesh @0x57fff1, same).
        // hasAnimCursor=false -> SampleByGameTime fallback (correct clip via animType, but
        // a global clock cadence). C++ sources per entity family:
        //   MONSTERS  : game::g_MonsterTickExt[i] (.motionState = slot+24 / .animFrame = slot+28);
        //   DECOR NPC : g_World.npcRenderEntries[i] (.mode = slot+12 / .frameAcc = slot+16,
        //               unified W7 pool, cf. Game/AnimationTick.h §6);
        //   PLAYERS   : CharAnimState p.anim.state (entity+244, clip selector, network) /
        //               p.anim.animFrame (entity+248, cursor advanced by
        //               game::Player_AdvanceAnimCursor — front F_PLAYERANIM, Game/PlayerAnimCursorTick.h).
        // hasAnimCursor = game::Monster_MotionTickIsWired / ZoneNpc_AnimTickIsWired /
        // Player_AnimCursorTickIsWired depending on the family (anti-regression guard:
        // only enables SampleByCursor if the matching cursor tick has actually run).
        int   animType      = 0;
        // Weapon pose (G5, DEEP IDA render): entity+240 = body+216 = move-state+0 = 2*weaponClass.
        // 3rd param (weaponType) of MotionCache::GetForPlayer / PcModel_ResolveEquipSlot 0x4E46A0
        // (base + 19968*animSlot). 0 for monsters/NPC. Armed player -> weapon-pose clip.
        int   animSlot      = 0;
        float animCursor    = 0.0f;
        bool  hasAnimCursor = false;

        // Reflection eligibility (mission "SHADOW/REFLECTION EXTENSION", 2026-07-14, cf.
        // header banner § Shadow/reflection): true ONLY for monsters. `Char_DrawReflection`
        // 0x581090 has, in the entire binary, a single caller (`xrefs_to` confirmed),
        // itself in the MONSTER loop of `Scene_InGameRender` -- never on the player or NPC
        // array. Setting this flag to true for a player or NPC would be an invention (no
        // matching call in the disassembly): DO NOT extend it without new contrary
        // decompilation evidence.
        bool reflectionEligible = false;

        // 3D BODY eligibility (mission "PNJ GAMEPLAY SANS CORPS", RE 2026-07-15): true by
        // default. Set to false ONLY for GAMEPLAY NPC (g_World.npcs, network array
        // dword_17AB534) — PROVEN by RE idaTs2: their `def` field (dword_17AB598 =
        // ITEM_INFO record) is read by NO rendering function (data_refs 0x17AB598 =
        // interaction/autoplay only), and the 3 network NPC loops of Scene_InGameRender
        // 0x52D0B0 (0x52dc84/0x52ec5b/0x52fcae) only call Char_DrawAura / Fx_DrawZoneAura /
        // ModelObj_Draw(quest marker) / Char_DrawNameTag — NEVER SObject_DrawEx or
        // Char_Draw (no mesh body). The 3D body of NPCs is EXCLUSIVELY drawn by
        // Npc_DrawMesh 0x57FF00 on the SEPARATE array g_NpcRenderArray 0x1764D14 (populated
        // by Pkt_EnterWorld from zone decor NPCs => ZoneNpcs loop below). Drawing a body
        // (let alone a cube) for a gameplay NPC would therefore be an INFIDELITY: when this
        // flag is false, renderOne() emits neither model nor cube (nor reflection), only
        // the nameplate.
        bool bodyMeshEligible = true;
    };
    void renderOne(const DrawableEntity& ent, const game::DrawCullContext& cull,
                  const D3DXMATRIX& view, const D3DXMATRIX& proj, const D3DXMATRIX& viewProj);

    // WEAPON TRAIL (front F_WEAPONTRAIL) — resolution SHARED between the opaque pass
    // (renderOne, Char_DrawWeaponTrailEffect 0x55E9D0 -> DrawModel) and the planar shadow
    // pass (renderPlanarShadows, Char_DrawWeaponEffectVariantB 0x56BF90 ->
    // DrawModelPlanarShadow), so the flattened silhouette exactly matches the opaque trail
    // (same SObject, same palette, same transform as the body). Applies the binary's full
    // gate:
    //   1. player (hasBody) + modelCache_ ready,
    //   2. weaponAnimSlot != 0 && !altWeaponSet (@0x56c01b),
    //   3. v6 = ResolveWeaponTrailIndex(weaponAnimSlot) != -1,
    //   4. motionSub = ResolveWeaponTrailMotionSub(animType=state) != -1,
    //   5. sub-block 2: guarded by frameCount>=1 (Motion_GetFrameCount @0x56c43e).
    // Returns true + (outModel, outPalette) ready to draw; false = no trail (clean skip).
    // outPalette: SampleByCursor(animCursor) if hasAnimCursor, otherwise SampleByGameTime
    // fallback — same cursor (entity+248 = this+62) as the body.
    bool resolveWeaponTrail(const DrawableEntity& ent,
                            const gfx::SkinnedModel*& outModel,
                            gfx::BonePalette& outPalette);

    // =======================================================================
    // PROJECTED PLANAR SHADOW pass — Wave B / branch B8 (front F_ENTITY3D).
    // Reproduces the shadow bracket of Scene_InGameRender 0x52D0B0 (0x52D9DC..0x52DB15):
    // opens the shadow states once, queries the ground plane per entity (collisionSource_->
    // GetGroundPlaneForShadow 0x40F720) and, if the plane resolves, flattens the skinned
    // body via meshRenderer_.DrawModelPlanarShadow; restores the states afterward.
    // Extended to the 3 categories shadowed by the binary (PLAYERS @0x52DA41 / MONSTERS
    // @0x52DB09 / DECOR NPC @0x52DAA2) — gameplay NPC (bodyMeshEligible=false) have no body
    // -> no shadow. Replaces the old drawReflectionOverlay approximation (monsters only,
    // no flattening or bracket).
    // Clean no-op if collisionSource_==nullptr (fallback: no drawing, no invented plane).
    void renderPlanarShadows(const std::vector<DrawableEntity>& drawables,
                             const game::DrawCullContext& cull);
    // GXD_SetupStencilShadowState 0x404F20 / GXD_EndStencilShadowState 0x4050D0 — D3D
    // states of the bracket (byte-exact, cf. Docs/TS2_EXTRACT_PLANAR_SHADOW.md §3.b/§5).
    // Set/restored ONCE around the shadow loop. STENCILREF not set by 0x404F20 -> inherited
    // (not invented).
    void beginPlanarShadowBracket();
    void endPlanarShadowBracket();

    // FRONT FX-F4 (M1): REAL shader slots of the GXDEffect npk (Shader03 VS03_SkinnedLit
    // 0x409AB0 + Shader04 PS04_Tex 0x409CC0 + VS15 shadow volume 0x40ACB0), loaded from
    // "./GXDEFFECT/GXDEffect.npk" (XTEA key {1,4,4,1}, cf. Shader_LoadVS03 0x409AB0:
    // Npk_OpenFile + Npk_FindEntryByName("Shader03.fx") + j_D3DXCompileShader "Main"/"vs_2_0"
    // + GetConstantByName mKeyMatrix/mWorldViewProjMatrix/mLightDirection/mLightAmbient/
    // mLightDiffuse). OWNED here (lifetime = WorldRenderer);
    // meshRenderer_.AttachShaderSet(&shaderSet_) only takes a NON-owning reference to it.
    // Without this wiring, DrawSkinnedSubset falls back to the reconstructed HLSL
    // (fallback) and the real npk Shader03/04 are never used (cf. MeshRenderer.cpp:510).
    // DECLARED BEFORE meshRenderer_: since members are destroyed in reverse declaration
    // order, this guarantees ~MeshRenderer (which releases its reference) runs BEFORE
    // ~ShaderSet (which frees VS/PS/CT/decl) even without an explicit Shutdown() call.
    gfx::ShaderSet    shaderSet_;
    gfx::MeshRenderer meshRenderer_;
    gfx::Font         font_;
    std::unique_ptr<gfx::ModelCache> modelCache_; // cf. Init() for the gameDataDir="." choice
    // F_ENTITY3D (B8) — source of the planar shadow's ground plane (SetCollisionSource,
    // set by MAIN from SceneManager). NON-owning. nullptr -> planar shadow pass disabled
    // (clean fallback).
    const world::WorldAssets* collisionSource_ = nullptr;
    IDirect3DDevice9* device_    = nullptr;
    // hWndParent 0x815184 (main window) — needed by drawNameplatePass's ScreenToClient
    // (@0x52FB6C). NO new Init() parameter nor hook for the orchestrator to set: the
    // window is retrieved from the device itself
    // (IDirect3DDevice9::GetCreationParameters().hFocusWindow == the HWND passed to
    // CreateDevice by gfx::Renderer::Init), so this front stays self-sufficient. nullptr
    // -> ScreenToClient is skipped (same screen coordinates), like UI/UIManager.cpp:294.
    HWND              hwnd_      = nullptr;
    ID3DXMesh*        cubeMesh_  = nullptr; // placeholder (D3DXCreateBox 1x1x1)
    int  screenW_ = 0, screenH_ = 0;
    bool ready_   = false;
};

} // namespace ts2

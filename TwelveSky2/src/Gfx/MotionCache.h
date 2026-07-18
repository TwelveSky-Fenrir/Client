// Gfx/MotionCache.h — CPU mirror of g_ModelMotionArray 0x8E8B30 (animated bone palettes).
//
// PROBLEM solved here: Scene/WorldRenderer.cpp used to draw every entity with an
// IDENTITY gfx::BonePalette (no animation). The real client keeps in memory, at boot,
// a huge array of Motion objects (g_ModelMotionArray 0x8E8B30, 156-byte entries each)
// indexed by (kind, anim). Each frame the renderer fetches the object's bone palette via
// Motion_GetData 0x4D78C0 (returns motionSlot+136) and samples the current frame. This
// cache fills this gap on the pure CPU SIDE (no D3D device required): key = MOTION stem
// with no folder or extension (e.g. "M001001001"), value = decoded matrices + a gfx::MotionPalette
// (valid/frameCount/bonesPerFrame/base) identical to Motion_GetData's return value.
//
// PROVEN DRAWING CHAIN (IDA, single source of truth):
//   Char_Draw 0x5805C0 (in-game monster body)
//     -> SObject_DrawEx 0x4D9330(desc, pass, animTime, pos, rotY, scale, motionSlot, 1)
//       -> Motion_GetData 0x4D78C0(motionSlot) = motionSlot+136 (MotionPalette)
//       -> Model_Render 0x40EBB0(mesh, scale, scaleVec, pos, rotVec, pass, palette, animTime,..)
//         -> Model_DrawSkinnedSubset 0x40CA40 (base + ((frame*bonesPerFrame)<<6), count=bonesPerFrame)
//
// MotionPalette STRUCTURE (Motion_GetData return = motionSlot+136), confirmed by Model_Render
// (a7[0]=valid, a7[1]=frameCount, see 0x40ebde/0x40ebfd) and Model_DrawSkinnedSubset
// (*(a4+8)=bonesPerFrame, *(a4+12)=base): +0 valid, +4 frameCount, +8 bonesPerFrame, +12 base
// (ptr -> 64-byte matrices, frame-major). This is EXACTLY gfx::MotionPalette from MeshRenderer.h.
//
// .MOTION FORMAT -> matrices (Motion_ReadFrames 0x40B1A0 + Motion_QuatToMatrix 0x6BB684 =
// thunk D3DXMatrixRotationQuaternion): the ts2::asset::Motion parser (Asset/Motion.h) already
// supplies keyframes in disk order = frame-major, At(frame,bone)=frame*BoneCount()+bone.
// FIDELITY (CLAUDE.md): .MOTION = PURE zlib via the existing parser -- NEVER carry over
// XTEA/GXCW (reserved for .npk).
//
// PER-SLOT ACCESSORS (mirroring the 3 requested accessors):
//   - Monster: Model_GetNpcMotionSlot 0x4E5960 (kind<=0x14C=332, anim in {0,1,3,4,5,7,8,12,19}).
//   - NPC    : Model_GetNpcMeshSlot   0x4E5910 (kind<=0x41=65, anim<2).
//   - Player : PcModel_ResolveEquipSlot 0x4E46A0 (race in [0,3), gender in [0,2)).
//
// .MOTION PATHS (Motion_BuildPathAndLoad 0x4D7390, exact printfs, verified on disk 001..006):
//   Player  001\C%03d%03d%03d.MOTION % (race+3*gender+1, weaponType+1, animState+1)
//   NPC     002\N%03d001%03d.MOTION  % (kindIndex+1, animType+1)
//   Monster 003\M%03d001%03d.MOTION  % (kindIndex+1, animType+1)
#pragma once
#include "Gfx/MeshRenderer.h"      // gfx::MotionPalette / gfx::BonePalette (bone palette types)
#include "Asset/Motion.h"          // ts2::asset::Motion (pure zlib parser)
#include "Game/GameDatabase.h"     // game::GetMonsterInfo / MonsterInfo::kindIndexP1
#include "Game/ExtraDatabases.h"   // game::NpcDefRecord::fieldE
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  MotionCache — key = MOTION stem (name WITHOUT folder or extension, e.g. "M001001001").
// ---------------------------------------------------------------------------
class MotionCache {
public:
    // gameDataDir = "GameData" root (same "." convention as ModelCache, see
    // WorldRenderer::Init). NO D3D device required: 100% CPU data.
    explicit MotionCache(std::string gameDataDir);

    MotionCache(const MotionCache&) = delete;
    MotionCache& operator=(const MotionCache&) = delete;

    // Animation palette of a monster (Char_Draw 0x5805C0 + Model_GetNpcMotionSlot 0x4E5960):
    // kindIndex = GetMonsterInfo(monsterDefId)->kindIndexP1 - 1; guards 1<=kindIndexP1<=333 and
    // animType in {0,1,3,4,5,7,8,12,19}. nullptr if out of bounds / file missing / parsing failed.
    const gfx::MotionPalette* GetForMonster(uint32_t monsterDefId, int animType);

    // Animation palette of an NPC (Model_GetNpcMeshSlot 0x4E5910): kindIndex = npc.fieldE - 1;
    // guards 1<=fieldE<=66 and animType in {0,1}. nullptr otherwise.
    const gfx::MotionPalette* GetForNpc(const game::NpcDefRecord& npc, int animType);

    // Animation palette of a player (PcModel_ResolveEquipSlot 0x4E46A0): guards race in [0,3),
    // gender in [0,2). weaponType/animState left at idle default (0,0) by the caller, see
    // PlayerPaperdoll. nullptr otherwise.
    const gfx::MotionPalette* GetForPlayer(int race, int gender, int weaponType, int animState);

    // Animation palette of the WEAPON TRAIL (front F_WEAPONTRAIL, 2026-07-17) — motion cat. 5
    // "F%03d001%03d.MOTION" % (trailIndex+1, motionSub+1), folder 005 (Motion_BuildPathAndLoad
    // 0x4D7390 case 5, see Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §3). trailIndex = v6 in [0,42) from
    // the switch in Game/WeaponTrailResolver.h; motionSub in [0,3) = sub-block chosen by the action
    // state (ResolveWeaponTrailMotionSub) and indexing unk_F54DB4/E50/EEC in the binary. This is the
    // SAME cache as the body (fidelity: in 0x56BF90 the trail and the body share
    // g_ModelMotionArray via Motion_GetData). nullptr if trailIndex/motionSub out of bounds, file
    // missing or parsing failed. The palette's frameCount (MotionPalette+4) feeds the
    // Motion_GetFrameCount>=1 gate of sub-block 2 (see Scene/WorldRenderer.cpp::resolveWeaponTrail).
    const gfx::MotionPalette* GetForWeaponTrail(int trailIndex, int motionSub);

    // Samples the bone slice for the current frame from g_World.gameTimeSec.
    //
    // ///// MISATTRIBUTION FIXED — Passe 4 / wave W7, motion-anim front (2026-07-16) /////
    // The previous write-up presented `ftol(g_GameTimeSec*30.0) % count` as "the binary's
    // clock idiom" for body drawing, citing "Char_RenderModel 0x528d38". That is WRONG and is
    // fixed here. This idiom does exist, but in ONE place only, for ONE object only: the
    // floating QUEST MARKER above an NPC -- Npc_DrawMesh 0x57FF00
    // @0x58005b (`Crt_ftol(g_GameTimeSec * 30.0)` then `v9 = v5 % SubObjectCount`, fed into
    // ModelObj_Draw(unk_B60AB8 + 148*153)). The BODY itself is NEVER sampled by a
    // global clock: neither Char_Draw 0x5805C0 (@0x580828: animTime = monster slot +28) nor
    // Npc_DrawMesh (@0x57fff1: animTime = NPC slot +16) read g_GameTimeSec for the body.
    // => This function reproduces NO body-drawing path of the binary. The faithful path
    // is SampleByCursor below. SampleByGameTime is KEPT because it is still
    // consumed by Gfx/PlayerPaperdoll.cpp:23 (PLAYERS): the real player cursor lives in
    // CharAnimState::animFrame and only advances via the terminal switch of
    // Char_UpdateAnimationFrame 0x5727BF (55 handlers), NOT PORTED as of now (`stateHandler`
    // null, see Scene/SceneManager.cpp:802) -> wiring the player cursor now would freeze it
    // at 0. ASSUMED and documented fallback pending that port, NOT a faithful reproduction.
    //
    // count = bonesPerFrame RAW, NO clamp -- faithful to Model_DrawSkinnedSubset 0x40CA40, which
    // passes Count = *(a4+8) as-is to SetMatrixArray (+88): @0x40d4e8 (Sh03), @0x40dac2 (Sh05),
    // @0x40d7c0 (Sh07), with no cmp/min or constant 40. The bone bound belongs to the SHADER
    // (D3DX writes min(Count, mKeyMatrix.Elements)) and is applied by MeshRenderer::
    // DrawSkinnedSubset (boneArraySize_; kMaxBones = HLSL fallback only). Identical to
    // MotionPalette::FrameSlice. Invalid BonePalette (matrices=nullptr) if mp is invalid.
    static gfx::BonePalette SampleByGameTime(const gfx::MotionPalette& mp, float gameTimeSec);

    // Samples the bone slice by the ENTITY'S OWN CURSOR — the binary's REAL drawing path
    // for monster/NPC bodies (Passe 4 / W7, motion-anim front; fixes the gap
    // as-motion-02 "global clock instead of per-entity cursor").
    //   Char_Draw 0x5805C0 @0x580828: SObject_DrawEx(..., animTime = *((float*)this + 7)
    //     = monster slot +28, ...) -- PER-MONSTER cursor, accumulated by the 9
    //     Char_MotionTick_* handlers (0x582D40..0x5832E0) dispatched by Char_Update 0x581E10 @0x5822D3.
    //   Npc_DrawMesh 0x57FF00 @0x57fff1: SObject_DrawEx(..., animTime = *(this + 4)
    //     = NPC slot +16, ...) -- PER-NPC cursor, accumulated by Npc_RenderSlotTick_Loop
    //     0x580400 @0x58043e / Npc_RenderSlotTick_Once 0x5804A0 @0x5804de.
    // SObject_DrawEx 0x4D9330 @0x4d946d passes a3 AS-IS as the 8th argument of Model_Render
    // 0x40EBB0 (= animTime) -> frame = ftol(animTime) @0x40ebea, bound [0, frameCount-1].
    //
    // NO MODULO HERE, AND THIS IS ESSENTIAL (unlike SampleByGameTime): the cursor
    // ALREADY arrives wrapped by the tick, whose wrap is a SINGLE SUBTRACTION (@0x582e10
    // Loop, @0x58045f NPC), never a modulo -- and the Death state (Char_MotionTick_Death 0x5832E0
    // @0x583345) deliberately FREEZES the cursor at frameCount-1: a modulo would restart it at
    // 0 and visually resurrect the corpse. The clamp [0, frameCount-1] reproduces
    // exactly Model_Render's bound, nothing more.
    // Same absence of bone clamp as SampleByGameTime (see its note: count = bonesPerFrame RAW,
    // the only legitimate bound belongs to the shader via MeshRenderer::DrawSkinnedSubset).
    static gfx::BonePalette SampleByCursor(const gfx::MotionPalette& mp, float cursor);

    // Number of frames of the (kind, animType) anim — exact mirror of Model_GetMotionFrameCount
    // 0x4E5A70 (monster) / Model_GetWeaponEffectFrameCount 0x4E5A40 (NPC), which resolve the SAME
    // slot as the drawing then return Motion_GetFrameCount 0x4D7830. VERIFIED: 0x4D7830 ALWAYS
    // returns *(slot+140) (@0x4d786d) regardless of its 2nd argument (1 on the monster side, 0 on
    // the NPC side — this flag only drives the lazy Motion_Load, not the returned value) => the
    // tick's frameCount is BIT-FOR-BIT that of the drawing palette. 0 if not resolved (bound
    // exceeded / file missing) -> the tick degrades cleanly (see Game/AnimationTick.h).
    int GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType);
    int GetNpcMotionFrameCount(const game::NpcDefRecord& npc, int animType);

private:
    struct Entry {
        std::vector<D3DXMATRIX> matrices;      // frameCount*boneCount, frame-major order
        gfx::MotionPalette      palette{};     // points into `matrices` (stable: persistent cache)
        bool                    loadFailed = false;
    };

    // Lazy-load + cache (both success AND failure). folder = "001"/"002"/"003" (3 bytes + NUL).
    const gfx::MotionPalette* Get(const std::string& stem, const char* folder);

    // Stems (Motion_BuildPathAndLoad 0x4D7390) — empty string if a parameter is out of bounds.
    static std::string BuildMonsterMotionStem(int kindIndex, int animType); // "M%03d001%03d"
    static std::string BuildNpcMotionStem(int kindIndex, int animType);     // "N%03d001%03d"
    static std::string BuildPlayerMotionStem(int race, int gender, int weaponType, int animState); // "C%03d%03d%03d"
    static std::string BuildWeaponTrailMotionStem(int trailIndex, int motionSub); // "F%03d001%03d" (cat.5, folder 005)

    // Full path <gameDataDir>\G03_GDATA\D03_GMOTION\NNN\<stem>.MOTION (0x4D7390).
    std::string BuildMotionPath(const std::string& stem, const char* folder) const;

    std::string                            gameDataDir_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace ts2::gfx

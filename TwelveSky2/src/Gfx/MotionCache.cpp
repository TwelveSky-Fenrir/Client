// Gfx/MotionCache.cpp — implementation. See MotionCache.h for the proven drawing chain
// (Char_Draw 0x5805C0 -> SObject_DrawEx 0x4D9330 -> Motion_GetData 0x4D78C0 -> Model_Render
// 0x40EBB0) and the EAs of the slot accessors / .MOTION paths.
#include "Gfx/MotionCache.h"
#include "Core/Log.h"
#include <cstdio>

namespace ts2::gfx {

namespace {

// Same join convention as Gfx/ModelCache.cpp::JoinPath / Game/GameDatabase.cpp::Join.
std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Model_GetNpcMotionSlot 0x4E5960: switch of valid monster anims {0,1,3,4,5,7,8,0xC,0x13}
// (the others -> off_547298 fallback). 0xC=12, 0x13=19.
bool IsValidMonsterAnim(int animType) {
    switch (animType) {
        case 0: case 1: case 3: case 4: case 5: case 7: case 8: case 12: case 19:
            return true;
        default:
            return false;
    }
}

} // namespace

MotionCache::MotionCache(std::string gameDataDir)
    : gameDataDir_(std::move(gameDataDir)) {}

// Motion_BuildPathAndLoad 0x4D7390 case 3: "M%03d001%03d.MOTION" % (kindIndex+1, animType+1).
std::string MotionCache::BuildMonsterMotionStem(int kindIndex, int animType) {
    if (kindIndex < 0 || kindIndex >= 333) return {};   // bound Model_GetNpcMotionSlot 0x4E5960 (a2>0x14C)
    if (!IsValidMonsterAnim(animType)) return {};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "M%03d001%03d", kindIndex + 1, animType + 1);
    return buf;
}

// Motion_BuildPathAndLoad 0x4D7390 case 2: "N%03d001%03d.MOTION" % (kindIndex+1, animType+1).
std::string MotionCache::BuildNpcMotionStem(int kindIndex, int animType) {
    if (kindIndex < 0 || kindIndex >= 66) return {};    // bound Model_GetNpcMeshSlot 0x4E5910 (a2<=0x41)
    if (animType < 0 || animType >= 2) return {};       // Model_GetNpcMeshSlot: a3<2
    char buf[16];
    std::snprintf(buf, sizeof(buf), "N%03d001%03d", kindIndex + 1, animType + 1);
    return buf;
}

// Motion_BuildPathAndLoad 0x4D7390 case 1: "C%03d%03d%03d.MOTION" %
// (race+3*gender+1, weaponType+1, animState+1).
std::string MotionCache::BuildPlayerMotionStem(int race, int gender, int weaponType, int animState) {
    if (race < 0 || race >= 3) return {};               // PcModel_ResolveEquipSlot 0x4E46A0: race in [0,3)
    if (gender < 0 || gender >= 2) return {};           //                                     gender in [0,2)
    if (weaponType < 0 || animState < 0) return {};
    const int kindIndex = race + 3 * gender;            // 0-based (same formula as body, ModelCache)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "C%03d%03d%03d", kindIndex + 1, weaponType + 1, animState + 1);
    return buf;
}

// Motion_BuildPathAndLoad 0x4D7390 case 5: "F%03d001%03d.MOTION" % (trailIndex+1, motionSub+1),
// folder 005 (front F_WEAPONTRAIL). trailIndex = v6 of the trail (loop i23<42 of AssetMgr_
// InitAllSlots 0x4DEB50); motionSub in [0,3) = sub-block (inner loop i24<3) chosen by the action
// state (Game/WeaponTrailResolver::ResolveWeaponTrailMotionSub).
std::string MotionCache::BuildWeaponTrailMotionStem(int trailIndex, int motionSub) {
    if (trailIndex < 0 || trailIndex >= 42) return {};  // 42 entries (loop i23<42)
    if (motionSub  < 0 || motionSub  >= 3)  return {};  // 3 sub-blocks F54DB4/E50/EEC (loop i24<3)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "F%03d001%03d", trailIndex + 1, motionSub + 1);
    return buf;
}

// Motion_BuildPathAndLoad 0x4D7390: folder G03_GDATA\D03_GMOTION\NNN\<stem>.MOTION.
std::string MotionCache::BuildMotionPath(const std::string& stem, const char* folder) const {
    if (stem.empty()) return {};
    std::string path = JoinPath(gameDataDir_, "G03_GDATA\\D03_GMOTION");
    path = JoinPath(path, folder);
    path = JoinPath(path, stem + ".MOTION");
    return path;
}

const gfx::MotionPalette* MotionCache::Get(const std::string& stem, const char* folder) {
    if (stem.empty()) return nullptr;

    auto found = entries_.find(stem);
    if (found != entries_.end())
        return found->second.loadFailed ? nullptr : &found->second.palette;

    Entry entry;
    const std::string path = BuildMotionPath(stem, folder);

    asset::Motion m;
    if (path.empty() || !m.Load(path)) {
        TS2_WARN("MotionCache: echec chargement MOTION '%s' (stem '%s')",
                 path.empty() ? "<chemin vide>" : path.c_str(), stem.c_str());
        entry.loadFailed = true;
    } else {
        // Bit-faithful conversion (Motion_ReadFrames 0x40B1A0 + Motion_QuatToMatrix 0x6BB684 =
        // thunk D3DXMatrixRotationQuaternion): per keyframe (28 bytes on disk), quaternion rotation
        // then translation written at +48/+52/+56 of the matrix (= m._41/_42/_43, see 0x40b26d..).
        // FIDELITY: .MOTION = pure zlib (existing parser), NO XTEA/GXCW (see CLAUDE.md).
        const uint32_t F = m.FrameCount(); // count_A (Motion_ReadFrames @off4)
        const uint32_t B = m.BoneCount();  // count_B (Motion_ReadFrames @off8)
        const std::vector<asset::MotionKeyframe>& kf = m.Keyframes(); // disk order = frame-major
        entry.matrices.resize(static_cast<size_t>(F) * B);
        const size_t n = (entry.matrices.size() < kf.size()) ? entry.matrices.size() : kf.size();
        for (size_t i = 0; i < n; ++i) {
            D3DXQUATERNION q(kf[i].qx, kf[i].qy, kf[i].qz, kf[i].qw);
            D3DXMATRIX mtx;
            D3DXMatrixRotationQuaternion(&mtx, &q); // Motion_QuatToMatrix 0x6BB684
            mtx._41 = kf[i].tx;                     // translation @+48
            mtx._42 = kf[i].ty;                     // translation @+52
            mtx._43 = kf[i].tz;                     // translation @+56
            entry.matrices[i] = mtx;
        }
        // gfx::MotionPalette = motionSlot+136 (Motion_GetData 0x4D78C0): +0 valid, +4 frameCount,
        // +8 bonesPerFrame, +12 base. base set below after insertion (cache vector's stable pointer).
        entry.palette.valid         = entry.matrices.empty() ? 0 : 1;
        entry.palette.frameCount    = static_cast<int>(F);
        entry.palette.bonesPerFrame = static_cast<int>(B);
        entry.palette.base          = nullptr; // rewired below onto the inserted entry
    }

    auto ins = entries_.emplace(stem, std::move(entry)).first;
    if (!ins->second.loadFailed) {
        // The vector's buffer was transferred by the move (stable address): we rewire `base`
        // onto the PERSISTENT cache instance so it never points at a stack/temporary.
        ins->second.palette.base = ins->second.matrices.empty() ? nullptr
                                                                 : ins->second.matrices.data();
        return &ins->second.palette;
    }
    return nullptr;
}

const gfx::MotionPalette* MotionCache::GetForMonster(uint32_t monsterDefId, int animType) {
    // Char_Draw 0x5805C0: kindIndex = *(def+244) - 1 = MonsterInfo::kindIndexP1 - 1.
    const game::MonsterInfo* mi = game::GetMonsterInfo(monsterDefId);
    if (!mi) return nullptr;
    const int k1 = mi->kindIndexP1;                 // 1-based as stored (+244)
    if (k1 < 1 || k1 > 333) return nullptr;          // Model_GetNpcMotionSlot 0x4E5960: a2>0x14C -> fallback
    const std::string stem = BuildMonsterMotionStem(k1 - 1, animType);
    return Get(stem, "003");
}

const gfx::MotionPalette* MotionCache::GetForNpc(const game::NpcDefRecord& npc, int animType) {
    // Npc_DrawMesh 0x57FF00 / Model_GetNpcMeshSlot 0x4E5910: kindIndex = fieldE - 1, bound [1,66].
    const int f = static_cast<int>(npc.fieldE);      // +1324, kindIndex+1
    if (f < 1 || f > 66) return nullptr;
    const std::string stem = BuildNpcMotionStem(f - 1, animType);
    return Get(stem, "002");
}

const gfx::MotionPalette* MotionCache::GetForPlayer(int race, int gender, int weaponType, int animState) {
    // PcModel_ResolveEquipSlot 0x4E46A0 (default path): player slot by (race,gender,weaponPose,animState).
    const std::string stem = BuildPlayerMotionStem(race, gender, weaponType, animState);
    return Get(stem, "001");
}

const gfx::MotionPalette* MotionCache::GetForWeaponTrail(int trailIndex, int motionSub) {
    // Weapon trail effect motion (unk_F54DB4/E50/EEC + 468*v6 in 0x56BF90): cat. 5, folder 005.
    const std::string stem = BuildWeaponTrailMotionStem(trailIndex, motionSub);
    return Get(stem, "005");
}

gfx::BonePalette MotionCache::SampleByGameTime(const gfx::MotionPalette& mp, float gameTimeSec) {
    if (!mp.valid || mp.frameCount <= 0 || mp.bonesPerFrame <= 0 || !mp.base)
        return {}; // identity fallback on caller side (invalid BonePalette)

    // Idiom `ftol(g_GameTimeSec*30.0) % count` — Npc_DrawMesh 0x57FF00 @0x58005b. MISATTRIBUTION
    // FIXED (Passe 4 / W7): this idiom does NOT draw a body, it ONLY animates the floating quest
    // marker (ModelObj_Draw(unk_B60AB8 + 148*153)); the old citation "Char_RenderModel
    // 0x528d38" for a body was wrong. This function is therefore a FALLBACK (players only, see
    // MotionCache.h), not a faithful path -- the faithful body path is SampleByCursor.
    int frame = static_cast<int>(gameTimeSec * 30.0f) % mp.frameCount;
    if (frame < 0) frame += mp.frameCount;

    // NO BONE CLAMP -- fixes the residual "40-bone clamp vs unclamped FrameSlice" (Passe 4/W5).
    // Binary truth: Model_DrawSkinnedSubset 0x40CA40 passes Count = *(a4+8) = bonesPerFrame RAW to
    // ID3DXConstantTable::SetMatrixArray (method +88) -- no cmp/min, no constant 40 on this
    // path: Sh03 @0x40d4e8 (g_GxdSh03_hKeyMatrix, Count=*(a4+8)), Sh05 @0x40dac2 and Sh07 @0x40d7c0
    // (Count=*(a10+8)). Model_Render 0x40EBB0, its sole caller, clamps no further (its only
    // bound is on the FRAME, not the bones). The ONLY real bound is the mKeyMatrix[] array size
    // declared by the shader -- D3DX only writes min(Count, desc.Elements) -- and it is
    // already applied in the right place by MeshRenderer::DrawSkinnedSubset (boneArraySize_ =
    // GetConstantDesc(mKeyMatrix).Elements on the real npk shader; kMaxBones clamp reserved for the
    // HLSL fallback, whose mKeyMatrix[40] IS a real array constraint).
    // The old clamp citing 0x40CA40 was BACKWARDS: it truncated here, upstream and IRREVERSIBLY,
    // short-circuiting boneArraySize_ (bones >= 40 lost with no recourse on a shader declaring more),
    // and diverged from MotionPalette::FrameSlice (MeshRenderer.cpp:151), which is unclamped. kMaxBones=40
    // remains an UNPROVEN rewrite HYPOTHESIS in IDA (Docs/TS2_GXD_ROSETTA.md, TODO P-8): do not
    // let it leak outside the fallback that alone justifies it. No overflow is possible without this
    // clamp: the source slice contains exactly bonesPerFrame matrices (never over-read).
    //
    // Model_Render: slice = base + ((frame*bonesPerFrame)<<6) bytes = base[frame*bonesPerFrame]
    // in steps of 64-byte matrices.
    gfx::BonePalette bp;
    bp.matrices = mp.base + static_cast<size_t>(frame) * mp.bonesPerFrame;
    bp.count    = static_cast<UINT>(mp.bonesPerFrame); // RAW, like FrameSlice and like the binary
    return bp;
}

// REAL drawing path for monster/NPC bodies (see MotionCache.h for the full anchor
// chain). Char_Draw 0x5805C0 @0x580828 (animTime = monster slot +28) / Npc_DrawMesh 0x57FF00
// @0x57fff1 (animTime = NPC slot +16) -> SObject_DrawEx 0x4D9330 @0x4d946d (a3 passed as-is)
// -> Model_Render 0x40EBB0 @0x40ebea (frame = ftol(animTime), bound [0, frameCount-1]).
gfx::BonePalette MotionCache::SampleByCursor(const gfx::MotionPalette& mp, float cursor) {
    if (!mp.valid || mp.frameCount <= 0 || mp.bonesPerFrame <= 0 || !mp.base)
        return {}; // identity fallback on caller side (invalid BonePalette)

    // Model_Render 0x40ebea: frame = ftol(animTime) then bound [0, frameCount-1]. NO modulo
    // (see MotionCache.h): the cursor already arrives wrapped by the tick (wrap via a single
    // subtraction), and the Death state 0x5832E0 @0x583345 deliberately freezes it at frameCount-1 -- a modulo
    // would break this freeze. The lower clamp additionally guards against a negative cursor (never produced by the
    // ported handlers, pure defensive guard).
    int frame = static_cast<int>(cursor); // Crt_ftol = truncation toward zero
    if (frame < 0) frame = 0;
    if (frame > mp.frameCount - 1) frame = mp.frameCount - 1;

    // NO BONE CLAMP: count = bonesPerFrame RAW, strictly like SampleByGameTime above
    // and like Model_DrawSkinnedSubset 0x40CA40 (@0x40d4e8/@0x40dac2/@0x40d7c0 pass Count as-is
    // to SetMatrixArray). The bone bound stays that of the shader (MeshRenderer::DrawSkinnedSubset
    // / boneArraySize_) -- real-shader-path <-> HLSL-fallback consistency preserved (rule W5).
    gfx::BonePalette bp;
    bp.matrices = mp.base + static_cast<size_t>(frame) * mp.bonesPerFrame;
    bp.count    = static_cast<UINT>(mp.bonesPerFrame);
    return bp;
}

// Model_GetMotionFrameCount 0x4E5A70 = Motion_GetFrameCount(Model_GetNpcMotionSlot(this,
// kindIdx, animType), 1) — SAME slot as the drawing (Char_Draw @0x580770), hence same palette:
// we simply re-read its frameCount. Motion_GetFrameCount 0x4D7830 returns *(slot+140)
// @0x4d786d regardless of its 2nd argument (see MotionCache.h) -> exact equivalence.
int MotionCache::GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType) {
    const gfx::MotionPalette* mp = GetForMonster(monsterDefId, animType);
    return mp ? mp->frameCount : 0; // 0 = slot not resolved (Motion_GetFrameCount also returns 0)
}

// Model_GetWeaponEffectFrameCount 0x4E5A40 = Motion_GetFrameCount(Model_GetNpcMeshSlot(this,
// kindIdx, animType), 0) — SAME slot as the drawing (Npc_DrawMesh @0x57ffa0), same reasoning.
int MotionCache::GetNpcMotionFrameCount(const game::NpcDefRecord& npc, int animType) {
    const gfx::MotionPalette* mp = GetForNpc(npc, animType);
    return mp ? mp->frameCount : 0;
}

} // namespace ts2::gfx

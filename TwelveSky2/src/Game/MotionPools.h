// Game/MotionPools.h — ASSET/MOTION pools initialized by App_Init: mGDATA,
// mZONEMAININFO, mZONENPCINFO, mZONEMOVEINFO (see CLAUDE.md, App_Init 0x461C20,
// "[Error::mXXX.Init()]" sequence).
//
// EXACT mapping found in App_Init (0x461C20, disassembly):
//   if ( AssetMgr_InitAllSlots(&g_ModelMotionArray) )        // "[Error::mGDATA.Init()]"        0x4DEB50
//     if ( Motion_InitFrameTable(&g_MotionFrameRangeTable) ) // "[Error::mZONEMAININFO.Init()]"  0x4F1380
//       if ( Motion_LoadGInfo002Bin(&dword_14AA930) )        // "[Error::mZONENPCINFO.Init()]"   0x4FCFD0
//         if ( Motion_LoadGInfo003Bin(&flt_1555D08) )        // "[Error::mZONEMOVEINFO.Init()]"  0x4FD420
//
// WARNING misleading naming: despite the "GInfo002/003" name, the manager triggered by
// Motion_LoadGInfo003Bin's failure is indeed mZONEMOVEINFO (NOT mZONENPCINFO — that one
// corresponds to Motion_LoadGInfo002Bin). Verified by re-reading the 4 consecutive
// MessageBoxA("[Error::mXXX.Init()]") blocks in App_Init.
//
// 1) AssetMgr_InitAllSlots 0x4DEB50 (mGDATA) — NOT a file loader: it
//    initializes a HUGE in-memory array (g_ModelMotionArray, offsets
//    observed up to ~0xBC07B4 ≈ 12.3 MB) that pre-builds, for EVERY slot
//    (2D sprite, model, motion, static/water sobject, 3D sound track) of EVERY
//    category/subcategory, the associated file path — via 6 separate
//    "BuildPath" functions, each backed by its own folder-name tables
//    in .rdata (out of scope for this mission — these functions belong to the
//    render/asset "Sprite2D/ModelObj/Motion/SObject/Snd3D" subsystems):
//      Sprite2D_BuildPath      0x4D68E0  (slot stride 148 B, categories 1..7)
//      ModelObj_BuildPath      0x4D6E20  (slot stride 148 B, categories 1..5)
//      Motion_BuildPathAndLoad 0x4D7390  (slot stride 156 B, categories 1..6)
//      SObject_BuildPath       0x4D89C0  (slot stride 144 B, categories 1..21 + 11..14)
//      SObject_BuildPathW      0x4D96A0  (slot stride 144 B, category 6, "wide" variant)
//      Snd3D_SetISNPath        0x4DA0C0  (slot stride 192 B, categories 1..6)
//    Then: WSndMgr_Reset 0x4DAFC0 and 5x cThread_Start 0x78FBF0 (starts 5 asynchronous
//    loading threads that consume these paths later). The function always
//    returns 1 (no failure possible in the disassembly).
//    This module does NOT implement the 6 BuildPath functions (out of scope: file
//    naming formats owned by another subsystem); it documents and EXPOSES the exact
//    geometry of the pool (per-category counts/strides, extracted from the loop bounds
//    in the disassembly) so the owning subsystem can wire it faithfully later.
//    InitModelMotionPool() below faithfully reproduces the ONLY part with no
//    external dependency: triggering WSndMgr_Reset/cThread_Start is OUT OF
//    SCOPE for this module's network/thread concerns (TODO, see .cpp); the function
//    therefore simply returns `true` like the original (no failure possible).
//
// 2) Motion_InitFrameTable 0x4F1380 (mZONEMAININFO) — PURELY DATA: a
//    350-case `switch(i)` (338 explicit + 12 default) that fills a 350-row
//    table {start,end,type,flag} with NO file reads at all. Original memory
//    layout (g_MotionFrameRangeTable, DWORD* named `this`): two parallel
//    blocks of 350 entries interleaved 2 by 2 —
//      this[2*i]      = start   (start frame, 1-based, 0 for default case)
//      this[2*i+1]    = end     (end frame)
//      this[700+2*i]  = type    (animation category, -1 for default case)
//      this[701+2*i]  = flag    (0/1)
//    i.e. this[0..699] and this[700..1399] (1400 DWORD = 5600 B). Extracted HERE as a
//    struct-of-350 array (MotionFrameRange[350]) — same values, memory layout
//    reorganized for readability (no other module of the rewritten client references the
//    raw buffer by pointer, so the reorganization loses no functional
//    fidelity). All values were extracted automatically from the Hex-Rays
//    pseudocode of Motion_InitFrameTable (350/350 lines covered, including the 12
//    default cases {0,0,-1,0} from "default: continue;").
//
// 3) Motion_LoadGInfo002Bin 0x4FCFD0 (mZONENPCINFO) — REAL file loader:
//    reads "G02_GINFO\002.BIN" (under the GameData root) entirely RAW (no
//    compression/wrapper, plain CreateFileA+ReadFile) into dword_14AA930, exactly
//    701400 B = 350 rows x 501 DWORD. Fails if the read size differs from 701400.
//
//    ⚠️ FIX 2026-07-14 (Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md): row 1..350
//    is NOT a "weapon/effect attach-point table per motion frame" as
//    previously documented here (interpretation error) — it is the PER-ZONE
//    STATIC NPC PLACEMENT table (350 zones, i = zoneId-1), consumed directly by
//    `cGameData_LoadZoneNpcInfo` 0x5578E0 to populate `g_NpcRenderArray` on (re)load
//    of the current zone (see Docs/TS2_NPC_RENDER_ARRAY_WRITER.md). Row layout for
//    row i (0-based, zoneId = i+1):
//      row[0]        = n, number of static NPCs placed in this zone (0..100)
//      row[1..100]   = kindId (u32, 1-based, index into the `mNPC` table) of the n NPCs
//      row[101..400] = n float position triplets (x,y,z) at offset 101+3*j
//                       (j = 0-based NPC index, paired with row[1+j])
//      row[401..500] = n display-angle floats, at offset 401+j (paired with row[1+j])
//    The IDA function names "GInfo_FindMotionByFrameId"/"GInfo_CalcLeftMargin"/
//    "GInfo_CalcRightMargin"/"Motion_GetAABB" are MISLEADING (legacy of an earlier
//    naming pass that wrongly assumed a "motion/animation" semantic):
//    confirmed by decompilation, their 3 ONLY callers (xrefs_to = 1 site each) are
//    in `Quest_DrawTracker` 0x510FC0 (quest-tracking HUD), which uses them to
//    find WHICH ZONE a given quest NPC is in (linear search of
//    `kindId` in row[1..n] across the 350 zones) then compute an on-screen margin
//    relative to THAT ZONE's bounds (not a motion — `Motion_GetAABB` 0x4F6F60 is actually
//    a static table of 350 world-bound rectangles per zoneId, values
//    on the order of ±10000 units, incompatible with a character animation
//    bounding box). Not renamed in the IDB out of caution (risk of collision with a
//    future "Quest_" pass), but their real role is: "find the zone containing an NPC of
//    a given kindId, compute the displayed position relative to that zone's bounds".
//
//    Original consumer functions (all 0x4FDxxx, contiguous to
//    Motion_LoadGInfo002Bin):
//      GInfo_FindMotionByFrameId 0x4FD070 (this, kindId) -> 1-based zoneId or 0
//        (scans row[1..n] looking for kindId) — REIMPLEMENTED here (standalone,
//        name kept for compat with existing code despite the corrected semantics).
//      GInfo_CalcLeftMargin  0x4FD1D0 (this, kindId) -> round(pos.x - ZoneBounds(zone).minX)
//      GInfo_CalcRightMargin 0x4FD2F0 (this, kindId) -> round(ZoneBounds(zone).maxX - pos.z)
//        These last two depend on Motion_GetAABB 0x4F6F60 (13.9 KB, hardcoded table of 350
//        zone-bound rectangles — OUT OF SCOPE, quest/UI subsystem).
//        This module exposes GetRawAttachPoint() which returns the raw (x,y,z) triplet
//        NOT corrected by the zone bounds — the calling layer that owns the bounds
//        table can do the subtraction itself (see TODO in MotionPools.cpp).
//    For ZONE NPC PLACEMENT (the main use case, cGameData_LoadZoneNpcInfo),
//    use the direct accessors ZoneNpcCount()/ZoneNpcKindId()/ZoneNpcPosition()/
//    ZoneNpcAngle() below instead (direct indexing by zoneId, no search) — see
//    Game/StaticNpcLoader.h for the high-level loader equivalent to
//    cGameData_LoadZoneNpcInfo.
//
// 4) Motion_LoadGInfo003Bin 0x4FD420 (mZONEMOVEINFO) — REAL file loader:
//    reads "G02_GINFO\003.BIN" entirely RAW into flt_1555D08, exactly
//    1127000 B = 350 rows x 805 FLOAT (3220 B/row). Fails if the read size differs.
//    Row layout, motion = i+1 (1-based), deduced from GInfo2_GetVec3 0x4FD4C0:
//      row[0..2] = position (x,y,z) of the motion (the ONLY fields consumed in the
//                  disassembly reviewed — the remaining 802 floats per row are not
//                  referenced by any known caller, role not elucidated).
//    This is the table that Game/MapWarp.h uses via IFactionTownCoordResolver as the
//    GInfo2_GetVec3(npcId) -> vec3 fallback (see comment at top of MapWarp.h).
//    GetVec3() below is the faithful reimplementation of GInfo2_GetVec3.
//
// File formats discovered: G02_GINFO\002.BIN and G02_GINFO\003.BIN are both
// RAW C-ARRAY DUMPS (no header, no compression, fixed size known
// in advance) — not to be confused with the [rawSize][packedSize][zlib] wrapper
// of .IMG files (see Asset/ImgFile.h): here it's a direct ReadFile() into the final buffer.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::game {

// 1) mGDATA — geometry of the g_ModelMotionArray pool (AssetMgr_InitAllSlots 0x4DEB50).
//    Purely documentary (constants extracted from the disassembly's loop
//    bounds); the actual pool (per-slot file paths) is built by
//    the 6 BuildPath functions listed above, out of scope for this module.
namespace ModelMotionPoolLayout {
    // Record strides per slot family (bytes, taken from the step of the
    // `this + STRIDE * i + ...` loops in the disassembly).
    inline constexpr int kSprite2DSlotStride = 148;
    inline constexpr int kModelObjSlotStride = 148;
    inline constexpr int kMotionSlotStride   = 156;
    inline constexpr int kSObjectSlotStride  = 144;
    inline constexpr int kSoundSlotStride    = 192;

    // Slot counts per Sprite2D_BuildPath(this, category, ...) category — bounds
    // of the `for (i=0; i<N; ++i)` loops in AssetMgr_InitAllSlots, in code order.
    inline constexpr int kSprite2DCat1Count = 4500;
    inline constexpr int kSprite2DCat2Count = 4000;
    inline constexpr int kSprite2DCat3Count = 760;
    inline constexpr int kSprite2DCat4Count = 150;
    inline constexpr int kSprite2DCat5Count = 999;
    inline constexpr int kSprite2DCat6Count = 3 * 35;   // 3x35 nested loop
    inline constexpr int kSprite2DCat7Count = 350;

    // ModelObj_BuildPath: 3x2x508 (cat.1), 3x2x35 (cat.2), 320 (cat.3), 246 (cat.4),
    // 3x2x508 (cat.5).
    inline constexpr int kModelObjCat1Count = 3 * 2 * 508;
    inline constexpr int kModelObjCat2Count = 3 * 2 * 35;
    inline constexpr int kModelObjCat3Count = 320;
    inline constexpr int kModelObjCat4Count = 246;
    inline constexpr int kModelObjCat5Count = 3 * 2 * 508;

    // Motion_BuildPathAndLoad: 3x3x8x128 (cat.1 and cat.6), 66x3 (cat.2), 333x21 (cat.3),
    // 59x6x2 (cat.4), 3x2x2 (cat.1 variant 2 sub-args), 42x3 (cat.5).
    inline constexpr int kMotionCat1Count = 3 * 3 * 8 * 128;
    inline constexpr int kMotionCat2Count = 66 * 3;
    inline constexpr int kMotionCat3Count = 333 * 21;
    inline constexpr int kMotionCat4Count = 59 * 6 * 2;
    inline constexpr int kMotionCat5Count = 42 * 3;
    inline constexpr int kMotionCat6Count = 3 * 3 * 8 * 128;

    // Number of asynchronous loading threads started by cThread_Start at the end of
    // AssetMgr_InitAllSlots (unk_BC0604/BC0670/BC06DC/BC0748/BC07B4 + this).
    inline constexpr int kAsyncLoaderThreadCount = 5;
} // namespace ModelMotionPoolLayout

// Initializes the mGDATA pool. FAITHFUL reimplementation of the part of
// AssetMgr_InitAllSlots 0x4DEB50 that doesn't depend on any other subsystem: it does NOT
// build the file paths (delegated to the 6 BuildPath functions, out of
// scope — see header comment) and does NOT start the asynchronous threads
// (delegated to Thread/Sound, out of scope). Always returns true, like the original
// (no failure path in the disassembly).
bool InitModelMotionPool();

// 2) mZONEMAININFO — 350-row table {start,end,type,flag} (Motion_InitFrameTable
//    0x4F1380). Purely hardcoded constants in the binary, no file.
struct MotionFrameRange {
    int32_t start = 0;  // this[2*i]     — start frame (1-based), 0 if uncovered
    int32_t end   = 0;  // this[2*i+1]   — end frame
    int32_t type  = -1; // this[700+2*i] — animation category, -1 if uncovered
    int32_t flag  = 0;  // this[701+2*i] — 0/1 indicator
};
inline constexpr int kMotionFrameTableCount = 350;

// Fills the g_MotionFrameRangeTable equivalent (350 entries, values extracted from the
// Hex-Rays pseudocode of Motion_InitFrameTable, 350/350 cases covered). Always
// returns true (faithful: no failure path in the original).
bool InitFrameTable();

// Accessor — motionIndex 1-based (1..350), like the GInfo2_GetVec3/
// GInfo_FindMotionByFrameId accessors. Returns nullptr if out of bounds or if
// InitFrameTable() hasn't been called yet.
const MotionFrameRange* GetFrameRange(int motionIndex1Based);

// 3) mZONENPCINFO — 350x501 DWORD attach table (Motion_LoadGInfo002Bin 0x4FCFD0,
//    file "G02_GINFO\002.BIN" under the GameData root, exactly 701400 B, RAW).
inline constexpr int kAttachTableRowCount   = 350;   // motions 1..350
inline constexpr int kAttachTableRowStride  = 501;   // DWORD per row
inline constexpr size_t kGInfo002BinSize    = 701400; // 350 * 501 * 4

// Loads "G02_GINFO\002.BIN" from <gameDataDir>\G02_GINFO\002.BIN. Fails (false,
// buffer unchanged) if the file is unreadable OR its size differs from
// kGInfo002BinSize (strict guard, faithful to the original `NumberOfBytesRead == 701400`).
bool LoadGInfo002Bin(const std::string& gameDataDir);

// Raw pointer to the loaded table (350*501 DWORD), or nullptr if not loaded.
const uint32_t* LoadedAttachTable();

// Raw row (501 DWORD) for a 1-based motion, or nullptr if out of bounds / not loaded.
const uint32_t* AttachTableRow(int motionIndex1Based);

// GInfo_FindMotionByFrameId 0x4FD070 — faithful, standalone reimplementation (does not
// depend on Motion_GetAABB). Scans the 350 rows, returns the motion index (1-based)
// of the FIRST row whose frame id (row[1..row[0]]) == frameId, or 0 if none found.
int FindMotionByFrameId(int frameId);

// RAW attach point (x,y,z) — combines the internal search of GInfo_CalcLeftMargin/
// GInfo_CalcRightMargin (same i/j indices) but WITHOUT the Motion_GetAABB
// correction (out of scope, see header comment). A caller with the motion's
// bounding box can reproduce exactly:
//   leftMargin  = round(x - aabb.minX)   (GInfo_CalcLeftMargin  0x4FD1D0)
//   rightMargin = round(aabb.maxX - z)   (GInfo_CalcRightMargin 0x4FD2F0)
// Returns false if frameId isn't found in any row (x/y/z left at 0).
bool GetRawAttachPoint(int frameId, float& x, float& y, float& z, int* outMotionIndex1Based = nullptr);

// --- Direct "zone NPC" accessors (indexed by zoneId, NO search) ---
// Added 2026-07-14 after semantic correction (see header comment §3):
// reproduce exactly the read done by `cGameData_LoadZoneNpcInfo` 0x5578E0,
// which indexes DIRECTLY via `row = AttachTableRow(zoneId)` (no search by value).
// npcIndex0Based must be < ZoneNpcCount(zoneId1Based) (otherwise returns false/values 0).
// See Game/StaticNpcLoader.h for the high-level loader that chains these.

// row[0] — number of static NPCs placed in the zone (0 if zoneId out of bounds or table
// not loaded). Hard bound from the original file: 100 max (row[1..100]).
int ZoneNpcCount(int zoneId1Based);

// row[1+npcIndex] — 1-based kindId (index into the mNPC table, see SkillDefTbl_GetRecord
// in cGameData_LoadZoneNpcInfo). Returns 0 if out of bounds.
uint32_t ZoneNpcKindId(int zoneId1Based, int npcIndex0Based);

// row[101+3*npcIndex .. +2] — NPC position (x,y,z). Returns false if out of bounds
// (x/y/z left at 0).
bool ZoneNpcPosition(int zoneId1Based, int npcIndex0Based, float& x, float& y, float& z);

// row[401+npcIndex] — NPC's initial display angle (radians, copied as-is into
// the "+80" baseline by cGameData_LoadZoneNpcInfo). Returns 0.0f if out of bounds.
float ZoneNpcAngle(int zoneId1Based, int npcIndex0Based);

// 4) mZONEMOVEINFO — 350x805 FLOAT coordinate table (Motion_LoadGInfo003Bin
//    0x4FD420, file "G02_GINFO\003.BIN" under the GameData root, exactly 1127000 B,
//    RAW). Used by Game/MapWarp.h (IFactionTownCoordResolver, GInfo2_GetVec3
//    fallback) — wire up LoadedCoordTable()/GetVec3() from there.
inline constexpr int kCoordTableRowCount  = 350;  // motions/NPC 1..350
inline constexpr int kCoordTableRowStride = 805;  // FLOAT per row (3220 B)
inline constexpr size_t kGInfo003BinSize  = 1127000; // 350 * 805 * 4

// Loads "G02_GINFO\003.BIN" from <gameDataDir>\G02_GINFO\003.BIN. Fails (false,
// buffer unchanged) if unreadable or if the size differs from kGInfo003BinSize.
bool LoadGInfo003Bin(const std::string& gameDataDir);

// Raw pointer to the loaded table (350*805 FLOAT), or nullptr if not loaded.
// This is the accessor needed to wire up Game/MapWarp.h: layout = row i (0-based,
// motion=i+1) of 805 floats, the first 3 = position (x,y,z); the rest (802 floats)
// aren't consumed by any known caller in the disassembly reviewed.
const float* LoadedCoordTable();

// GInfo2_GetVec3 0x4FD4C0 — faithful reimplementation: motionIndex1Based outside [1,350]
// => (0,0,0) and returns false; otherwise reads row[0..2] and returns true.
bool GetVec3(int motionIndex1Based, float& x, float& y, float& z);

} // namespace ts2::game

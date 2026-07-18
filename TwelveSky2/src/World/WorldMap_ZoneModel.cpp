// World/WorldMap_ZoneModel.cpp — zone-model path resolution and zone resource dispatch,
// split out of WorldMap.cpp: ZoneModelPathWM/WJ, CurrentZoneModelPath, LoadZoneResource,
// LoadCurrentZoneModel. See WorldMap.h for source EAs.
#include "WorldMap.h"
#include "Asset/WorldChunk.h" // typed terrain views (CollisionMesh/Face/QuadNode/TerrainVertex) — Gaps G4/G5/G7

#include <algorithm> // std::min/std::max — AABB bounds for sweeps (namespace collision, WG-02/WG-03)
#include <cmath>   // sqrt/fabs — collision queries (namespace collision, Gaps G02/G03/G04)
#include <cstdint>
#include <cstdio>

namespace ts2::world {

namespace {

// snprintf "Z%03d" into a std::string (reproduces Crt_Vsnprintf 0x75cd5f / sub_75CDF8 0x75cdf8).
std::string FormatZone(const char* fmt, int a) {
    char buf[268] = {0}; // CHAR v7[268] in the original functions
    std::snprintf(buf, sizeof(buf), fmt, a);
    return std::string(buf);
}
std::string FormatZone2(const char* fmt, int a, int b) {
    char buf[268] = {0};
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    return std::string(buf);
}

} // namespace

// ===========================================================================
// Main .WM path (World_LoadZoneResource case 6, first switch 0x4dcd9b).
// ===========================================================================
std::string WorldMap::ZoneModelPathWM(int fileId, int z291Variant) {
    switch (fileId) {
        case 34:  return "G03_GDATA\\D07_GWORLD\\Z034_1.WM";
        case 49:  return "G03_GDATA\\D07_GWORLD\\Z049_1.WM";
        case 51:  return "G03_GDATA\\D07_GWORLD\\Z051_1.WM";
        case 53:  return "G03_GDATA\\D07_GWORLD\\Z053_1.WM";
        case 54:  return "G03_GDATA\\D07_GWORLD\\Z054_1.WM";
        case 138: case 139: case 165: case 166:
                  return "G03_GDATA\\D07_GWORLD\\Z138_1.WM";
        case 154: return "G03_GDATA\\D07_GWORLD\\Z154_1.WM";
        case 155: return "G03_GDATA\\D07_GWORLD\\Z155_1.WM";
        case 156: return "G03_GDATA\\D07_GWORLD\\Z156_1.WM";
        case 175: return "G03_GDATA\\D07_GWORLD\\Z175_01.WM";
        case 194: return "G03_GDATA\\D07_GWORLD\\Z194_1.WM";
        // 50/52/170: the primary is Z170_1.WM (the secondary Z170_2.WM loads separately).
        case 50: case 52: case 170:
                  return "G03_GDATA\\D07_GWORLD\\Z170_1.WM";
        case 267: return "G03_GDATA\\D07_GWORLD\\Z267_1.WM";
        case 270: return "G03_GDATA\\D07_GWORLD\\Z270_1.WM";
        case 291: return z291Variant == 0 ? "G03_GDATA\\D07_GWORLD\\Z291_1.WM"   // dword_1686134==0
                                           : "G03_GDATA\\D07_GWORLD\\Z291_2.WM";
        case 297: return "G03_GDATA\\D07_GWORLD\\Z297_1.WM"; // fileId 297 covers zones 200/298/299
        case 319: return "G03_GDATA\\D07_GWORLD\\Z319_1.WM";
        default:  return FormatZone(kFmtWM, fileId);         // "Z%03d.WM"
    }
}

// ===========================================================================
// Secondary .WJ path (World_LoadZoneResource case 6, second switch 0x4dd0b4).
// CONFLICT C-01 (Docs/TS2_WORLD_ROSETTA.md §2): the .WJ layer is ABSENT from VeryOldClient
// (WORLD_FOR_GXD = .WM only); introduced by the target — IDA WINS (anchor: second switch
// 0x4dd0b4 -> MapColl_LoadFaces this+0x150). Do NOT backport the absence of WJ.
// ===========================================================================
std::string WorldMap::ZoneModelPathWJ(int fileId) {
    switch (fileId) {
        case 34:  return "G03_GDATA\\D07_GWORLD\\Z034_1.WJ";
        case 49:  return "G03_GDATA\\D07_GWORLD\\Z049_1.WJ";
        case 51:  return "G03_GDATA\\D07_GWORLD\\Z051_1.WJ";
        case 53:  return "G03_GDATA\\D07_GWORLD\\Z053_1.WJ";
        case 154: return "G03_GDATA\\D07_GWORLD\\Z154_1.WJ";
        case 155: return "G03_GDATA\\D07_GWORLD\\Z155_1.WJ";
        case 156: return "G03_GDATA\\D07_GWORLD\\Z156_1.WJ";
        case 175: return "G03_GDATA\\D07_GWORLD\\Z175_01.WJ";
        case 194: return "G03_GDATA\\D07_GWORLD\\Z194_1.WJ";
        case 267: return "G03_GDATA\\D07_GWORLD\\Z267_1.WJ";
        case 270: return "G03_GDATA\\D07_GWORLD\\Z270_1.WJ";
        default:  return FormatZone(kFmtWJ, fileId);         // "Z%03d.WJ"
    }
}

// ===========================================================================
// Current-layer .WM path (World_LoadCurrentZoneModel 0x4dd6e0).
// Selection by fileId + mode (a2). Empty string = no reload.
// ===========================================================================
std::string WorldMap::CurrentZoneModelPath(int fileId, int mode) {
    switch (fileId) {
        case 34:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z034_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z034_2.WM";
        case 49:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z049_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z049_2.WM";
        case 51:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z051_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z051_2.WM";
        case 53:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z053_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z053_2.WM";
        case 54:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z054_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z054_2.WM";
        case 88:
            switch (mode) {
                case 1: return "G03_GDATA\\D07_GWORLD\\Z088_1.WM";
                case 2: return "G03_GDATA\\D07_GWORLD\\Z088_2.WM";
                case 3: return "G03_GDATA\\D07_GWORLD\\Z088_3.WM";
                case 4: return "G03_GDATA\\D07_GWORLD\\Z088_4.WM";
                case 5: return "G03_GDATA\\D07_GWORLD\\Z088_5.WM";
                case 6: return "G03_GDATA\\D07_GWORLD\\Z088.WM";
                default: return std::string(); // "" -> skip
            }
        case 138: case 139: case 165: case 166:
            switch (mode) {
                case 1: return "G03_GDATA\\D07_GWORLD\\Z138_1.WM";
                case 2: return "G03_GDATA\\D07_GWORLD\\Z138_2.WM";
                case 3: return "G03_GDATA\\D07_GWORLD\\Z138_3.WM";
                case 4: return "G03_GDATA\\D07_GWORLD\\Z138_4.WM";
                default: return "G03_GDATA\\D07_GWORLD\\Z138_5.WM";
            }
        case 154: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z154_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z154_2.WM";
        case 155: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z155_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z155_2.WM";
        case 156: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z156_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z156_2.WM";
        case 175:
            // mode 1..11 -> Z175_01..Z175_11.WM; otherwise "" (skip).
            switch (mode) {
                case 1:  return "G03_GDATA\\D07_GWORLD\\Z175_01.WM";
                case 2:  return "G03_GDATA\\D07_GWORLD\\Z175_02.WM";
                case 3:  return "G03_GDATA\\D07_GWORLD\\Z175_03.WM";
                case 4:  return "G03_GDATA\\D07_GWORLD\\Z175_04.WM";
                case 5:  return "G03_GDATA\\D07_GWORLD\\Z175_05.WM";
                case 6:  return "G03_GDATA\\D07_GWORLD\\Z175_06.WM";
                case 7:  return "G03_GDATA\\D07_GWORLD\\Z175_07.WM";
                case 8:  return "G03_GDATA\\D07_GWORLD\\Z175_08.WM";
                case 9:  return "G03_GDATA\\D07_GWORLD\\Z175_09.WM";
                case 10: return "G03_GDATA\\D07_GWORLD\\Z175_10.WM";
                case 11: return "G03_GDATA\\D07_GWORLD\\Z175_11.WM";
                default: return std::string();
            }
        case 194: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z194_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z194_2.WM";
        case 267: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z267_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z267_2.WM";
        case 270: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z270_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z270_2.WM";
        case 291: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z291_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z291_2.WM";
        case 297:
            if (mode == 1) return "G03_GDATA\\D07_GWORLD\\Z297_1.WM";
            if (mode == 2) return "G03_GDATA\\D07_GWORLD\\Z297_2.WM";
            return "G03_GDATA\\D07_GWORLD\\Z297_3.WM";
        case 319: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z319_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z319_2.WM";
        case 324: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z324.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z324_2.WM";
        case 342: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z342.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z342_1.WM";
        default:  return std::string(); // all other zones: "" -> no reload
    }
}

// ===========================================================================
// World_LoadZoneResource 0x4dcb60 — dispatch by resource type.
// Returns the original return byte (LOBYTE(v3)).
// ===========================================================================
unsigned char WorldMap::LoadZoneResource(int zoneId, ResourceKind kind) {
    // LOBYTE(v3) = a3 (kind) on entry; kept for cases that don't overwrite it.
    int v3 = static_cast<int>(kind);

    switch (kind) {
        case ResourceKind::FreeSound: // case 1
            v3 = hooks_.freeZoneSound ? (hooks_.freeZoneSound(hooks_.user) ? 1 : 0) : 0;
            return static_cast<unsigned char>(v3);

        case ResourceKind::MapFileWG: { // case 2  .WG
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtWG, fileId);
                v3 = hooks_.loadMapFileWG ? (hooks_.loadMapFileWG(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::ObjectsWO: { // case 3  .WO
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtWO, fileId);
                v3 = hooks_.loadObjectsWO ? (hooks_.loadObjectsWO(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::ObjectsWP: { // case 4  .WP
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtWP, fileId);
                v3 = hooks_.loadObjectsWP ? (hooks_.loadObjectsWP(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::ShadowTex: { // case 5  .SHADOW
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtShadow, fileId);
                v3 = hooks_.loadShadowTexture ? (hooks_.loadShadowTexture(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::WorldModel: { // case 6  .WM + .WJ
            int fileId = ZoneIdToFileId(zoneId);
            if (fileId == -1) return static_cast<unsigned char>(fileId); // 0xFF

            // Main .WM path.
            std::string wm = ZoneModelPathWM(fileId, flagZ291Variant);
            // Zones 50/52/170: double load (secondary Z170_2.WM into the Secondary slot).
            if (fileId == 170 || fileId == 50 || fileId == 52) {
                if (hooks_.loadFaces)
                    hooks_.loadFaces(hooks_.user, CollisionSlot::Secondary,
                                     "G03_GDATA\\D07_GWORLD\\Z170_2.WM");
                // wm stays Z170_1.WM (already returned by ZoneModelPathWM).
            }
            // MapColl_LoadFaces(this+0xA8, wm) — main collision.
            // CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWM (WM1->mRANGE1). IDA anchor:
            // MapColl_LoadFaces 0x694510. NB: the loaded data stays an opaque buffer
            // (CollisionMesh.raw), never decoded or queried here -> see the ground-height TODO in
            // WorldIntegration.cpp::LoadFaces (gaps G01/G02, TS2_WORLD_ROSETTA.md §3).
            if (hooks_.loadFaces) hooks_.loadFaces(hooks_.user, CollisionSlot::Main, wm.c_str());

            // Gap G02: bind the decoded mesh of the main layer (this+0xA8) for
            // ground/collision queries (resolves the SetCollisionMesh TODO). The runtime MapColl
            // carries faces+quadtree in place; here we point at the data already decoded by G01.
            if (hooks_.queryCollisionMesh)
                collisionMesh_ = hooks_.queryCollisionMesh(hooks_.user, CollisionSlot::Main);

            // Secondary .WJ path -> MapColl_LoadFaces(this+0x150, wj) — CONFLICT C-01 (WJ
            // absent from VeryOldClient, IDA wins; cf. ZoneModelPathWJ 0x4dd0b4 above).
            std::string wj = ZoneModelPathWJ(fileId);
            v3 = hooks_.loadFaces ? (hooks_.loadFaces(hooks_.user, CollisionSlot::WJ, wj.c_str()) ? 1 : 0) : 0;
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Atmosphere: { // case 7  .ATM
            // a2 == -1 -> skip (returns v3 = kind on entry).
            if (zoneId == -1) return static_cast<unsigned char>(v3);
            // if (atmosphereLoaded || World_LoadMap(...)) { load .ATM }
            // Byte-exact structure (World_LoadZoneResource 0x4dcb60 case 7: `byte_18C67C8 ||
            // World_LoadMap 0x4116b0`), guard read @0x4DD202 / skip @0x4DD217.
            // EW-02 (W11): `atmosphereLoaded_` stays ALWAYS false (LoadMap no longer arms it, see
            // WorldMap.cpp) -> `proceed` is false -> World_LoadMap replays on EVERY zone, exactly
            // like the binary (where World_UnloadMap 0x411A80 clears the flag at step 1 before step
            // 7). The `||` short-circuit thus reproduces its net effect without an atmosphere
            // object to free.
            bool proceed = atmosphereLoaded_;
            if (!proceed) {
                bool ok = LoadMap(kAtmosphereResourceDir); // -> dword_18C67C4, device g_GfxRenderer_pDevice
                v3 = ok ? 1 : 0;
                proceed = ok;
            }
            if (proceed) {
                // ATM uses the RAW zoneId (a2), not the fileId.
                std::string p = FormatZone(kFmtAtm, zoneId);
                v3 = hooks_.loadDataFile ? (hooks_.loadDataFile(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Minimap01: { // case 8
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtMinimap1, fileId);
                v3 = hooks_.loadMinimap ? (hooks_.loadMinimap(hooks_.user, 1, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Minimap02: { // case 9
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtMinimap2, fileId);
                v3 = hooks_.loadMinimap ? (hooks_.loadMinimap(hooks_.user, 2, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Minimap03: { // case 10
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtMinimap3, fileId);
                v3 = hooks_.loadMinimap ? (hooks_.loadMinimap(hooks_.user, 3, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::WorldSound: { // case 11  .WSOUND (Z%03d\Z%03d.WSOUND)
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone2(kFmtWSound, fileId, fileId);
                v3 = hooks_.loadWorldSound ? (hooks_.loadWorldSound(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::WorldBgm: { // case 12  .BGM
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtBgm, fileId);
                v3 = hooks_.loadWorldBgm ? (hooks_.loadWorldBgm(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        default: // switch default: returns v3 unchanged (= kind).
            return static_cast<unsigned char>(v3);
    }
}

// ===========================================================================
// World_LoadCurrentZoneModel 0x4dd6e0 — reloads the main collision (this+0xA8)
// with the .WM model of the current zone's `mode` layer (g_SelfMorphNpcId).
// ===========================================================================
int WorldMap::LoadCurrentZoneModel(int mode) {
    int fileId = ZoneIdToFileId(currentZoneId_); // World_ZoneIdToFileId(dword_1675A98)
    if (fileId == -1) return fileId;             // -1

    std::string path = CurrentZoneModelPath(fileId, mode);

    // result = Crt_Strcmp(path, ""): 0 if path is empty -> no reload (returns 0).
    if (path.empty()) return 0;

    // MapColl_Free(this+0xA8) then MapColl_LoadFaces(this+0xA8, path).
    if (hooks_.freeFaces) hooks_.freeFaces(hooks_.user, CollisionSlot::Main);
    // The free above may have invalidated the previously bound mesh: unbind then
    // rebind after the reload (Gap G02, consistent with the current Main layer).
    collisionMesh_ = nullptr;
    const int rc = hooks_.loadFaces
                       ? (hooks_.loadFaces(hooks_.user, CollisionSlot::Main, path.c_str()) ? 1 : 0)
                       : 0;
    if (hooks_.queryCollisionMesh)
        collisionMesh_ = hooks_.queryCollisionMesh(hooks_.user, CollisionSlot::Main);
    return rc;
}

} // namespace ts2::world

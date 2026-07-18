// Gfx/FxBillboard.cpp — implementation of the combat/skill FX "Object A" LEAF billboard.
//
// IDA ground truth: TwelveSky2.exe (imagebase 0x400000). Every block cites its anchor (name + 0xADDR).
// See FxBillboard.h for the overview and Docs/TS2_EXTRACT_FX_COMBAT.md for the spec.
//
// Reuse (no reinvention):
//   - Object A engine: ZoneFx_Init/UpdateEmit/RenderBillboards/Free (Gfx/ZoneFxEmitter.*) = the
//     IDA functions Particle_Init 0x6A7020 / Particle_UpdateEmit 0x6A7530 /
//     Particle_RenderBillboards 0x6A70B0 / Particle_Free 0x6A6FF0. Templates/pools = same structs.
//   - Decompression: asset::Zlib (GXDCompress.dll, GXD_DecompressEntity 0x6A1A30).
//   - Bounded reading: asset::ByteReader; file reading: asset::ReadWholeFile.
//   - Texture/track framing = same as Asset/WorldChunk.cpp::ReadTextureBlock / ReadAnimTrack
//     (Tex_LoadCompressedFromHandle 0x6A9CF0 / Anim_LoadQuatTrackFromHandle 0x6AAE20). Those
//     helpers are file-local there (not exported); we replicate their proven framing HERE
//     (bit-for-bit identical), because a standalone .PARTICLE is NOT a full .WP chunk but a bare node.
#include "Gfx/FxBillboard.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <d3dx9.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {
namespace {

// -------------------------------------------------------------------------------------------
// Module state.
// -------------------------------------------------------------------------------------------
IDirect3DDevice9* s_device   = nullptr;                 // g_GfxRenderer device 0x7FFE18
std::string       s_dataRoot = "GameData";              // GameData root (prefix of the .PARTICLE path)

// CPU scratch for billboard vertices — the binary uses a SINGLE shared VB (dword_800080) for
// all Object A emitters; we therefore keep one buffer reused for every render.
std::vector<Billboard_Vertex> s_scratch;

// One template-table slot (SObject_EnsureLoadedK 0x4D9EB0: {loaded@+0, path@+4, node@+104}).
// Data is kept in C++ members here (memory layout unconstrained — see FxBillboard.h).
struct FxTemplateSlot {
    bool              loaded = false;   // flag @+0  (0x4D9EBA: if(*this) return 1)
    FxEmitterTemplate node{};           // node @+104 (232 B) — filled by the .PARTICLE loader
};
FxTemplateSlot s_templates[kFxParticleTemplateCount];

// -------------------------------------------------------------------------------------------
// Texture framing — mirrors Asset/WorldChunk.cpp::ReadTextureBlock (Tex_LoadCompressedFromHandle
// 0x6A9CF0). [imageSize u32]; if 0 -> no texture (4 B consumed, empty dds). Otherwise
// [rawSize u32][packedSize u32][zlib packed] -> inflate = image(imageSize)+trailer(8 B);
// return the first `imageSize` bytes (the decoded DDS/BMP). Throws AssetError on bad framing.
// -------------------------------------------------------------------------------------------
std::vector<uint8_t> ReadFxTextureBlock(asset::ByteReader& r) {
    std::vector<uint8_t> dds;
    const uint32_t imageSize = r.U32();                 // [imageSize]
    if (imageSize == 0) return dds;                     // absent (present=false)
    const uint32_t rawSize = r.U32();                   // [rawSize]
    const uint32_t packed  = r.U32();                   // [packedSize]
    if (r.Remaining() < packed)
        throw asset::AssetError("FX .PARTICLE: texture packedSize dépasse le flux");
    std::vector<uint8_t> out = asset::Zlib::Instance().InflateTo(r.Ptr(), packed, rawSize);
    r.Skip(packed);
    // trailer 8 B (cf. ReadTextureBlock @WorldChunk): rawSize == imageSize + 8. Overflow-SAFE check
    // (audit fix: `imageSize + 8` could overflow a uint32 on a malformed .PARTICLE, letting
    // a huge imageSize through and causing an OOB at `out.begin() + imageSize`).
    if (rawSize < 8 || rawSize - 8 != imageSize || out.size() < imageSize)
        throw asset::AssetError("FX .PARTICLE: texture rawSize != imageSize+8 (ou flux tronqué)");
    dds.assign(out.begin(), out.begin() + imageSize);   // decoded image = first imageSize bytes
    return dds;
}

// -------------------------------------------------------------------------------------------
// Quaternion-track framing — mirrors Asset/WorldChunk.cpp::ReadAnimTrack
// (Anim_LoadQuatTrackFromHandle 0x6AAE20). [present u32]; if !=0 -> GXD block
// [rawSize u32][packedSize u32][zlib packed]. The block is CONSUMED (skipped): the keyframe
// path (template+56/frameMatrices) is NOT ported (identity else-branch of UpdateEmit @0x6A78C3,
// cf. ZoneFxEmitter). The cursor still needs to advance to reach the 144 B of fields.
// -------------------------------------------------------------------------------------------
void SkipFxAnimTrack(asset::ByteReader& r) {
    const uint32_t present = r.U32();                   // [present]
    if (present == 0) return;
    /*rawSize*/ (void)r.U32();                          // [rawSize] (unused: skipped)
    const uint32_t packed = r.U32();                    // [packedSize]
    if (r.Remaining() < packed)
        throw asset::AssetError("FX .PARTICLE: piste quat packedSize dépasse le flux");
    r.Skip(packed);                                     // skip the GXD block
}

// -------------------------------------------------------------------------------------------
// Creates a GPU texture from an in-memory DDS/BMP (D3DPOOL_MANAGED). This is what
// Tex_LoadCompressedFromHandle 0x6A9CF0 does internally once the stream is inflated
// (D3DXCreateTextureFromFileInMemory*). Identical to WorldGeometryRenderer::createTextureFromBlock
// (@0x6A3040). Empty dds OR null device => nullptr (SetTexture(0,nullptr) at render time, faithful
// to "no texture").
// -------------------------------------------------------------------------------------------
IDirect3DTexture9* CreateFxTexture(IDirect3DDevice9* dev, const std::vector<uint8_t>& dds) {
    if (!dev || dds.empty()) return nullptr;
    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, dds.data(), static_cast<UINT>(dds.size()),
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("FxBillboard: creation texture .PARTICLE echouee (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

// -------------------------------------------------------------------------------------------
// LOADER for a standalone .PARTICLE file into a 232 B template — mirrors
// Fx_NodeLoadFromFile 0x6A6680. Disk layout = [texture block][quat track][144 B of fields]
// (NO present-flag up front, unlike a .WP node from ReadFxNode). STRICT order:
//   1) Tex_LoadCompressedFromHandle 0x6A9CF0  (this+1  = texture)      @0x6A66E1
//   2) Anim_LoadQuatTrackFromHandle 0x6AAE20  (this+14 = quat track)   @0x6A6715
//   3) 18 ReadFile -> [runtime +72, +216) = 144 B of fields             @0x6A6760..@0x6A69B4
//   4) *this = 1 (enabled)                                             @0x6A69DA
// Failure (missing file / bad framing) -> node left disabled (WorldObjectB_Free @0x6A6767).
// Returns true if the template is ready (enabled=1).
// -------------------------------------------------------------------------------------------
bool LoadParticleFile(int index, FxEmitterTemplate& node) {
    // FxParticle_BuildPath 0x4D9E60 + GameData root.
    char rel[128];
    FxBillboard_BuildPath(rel, sizeof(rel), index);
    const std::string full = s_dataRoot + "\\" + rel;

    // CreateFileA(...) — failure => *this=0, node not loaded (@0x6A66B1).
    std::vector<uint8_t> file;
    if (!asset::ReadWholeFile(full, file)) {
        TS2_WARN("FxBillboard: .PARTICLE introuvable (%s)", full.c_str());
        return false;
    }

    try {
        asset::ByteReader r(file);
        std::vector<uint8_t> dds = ReadFxTextureBlock(r); // 1) texture (0x6A66E1)
        SkipFxAnimTrack(r);                               // 2) quat track (0x6A6715; keyframe not ported)
        uint8_t fields[kFxTemplateDiskSize];              // 3) 144 B [runtime +72,+216)
        r.Read(fields, kFxTemplateDiskSize);              //    (18 ReadFile calls aggregated)

        IDirect3DTexture9* gpuTex = CreateFxTexture(s_device, dds);
        // ZoneFx_BuildTemplate = Fx_NodeLoadFromFile @0x6A69DA (enabled=1) + copies 144 B to (tmpl+72)
        // + GPU texture at (tmpl+52). colorRate NOT derived here (done by ZoneFx_Init/ComputeGradients).
        ZoneFx_BuildTemplate(&node, fields, kFxTemplateDiskSize, gpuTex);
        return true;
    } catch (const asset::AssetError& e) {
        // Corrupted framing / truncated file -> ReadFile failure (@0x6A6760 -> WorldObjectB_Free).
        TS2_WARN("FxBillboard: parse .PARTICLE %03d echoue (%s)", index + 1, e.what());
        node.enabled = 0;
        return false;
    }
}

} // namespace

// =============================================================================================
//  CONFIGURATION
// =============================================================================================
void FxBillboard_SetDevice(IDirect3DDevice9* device) { s_device = device; }

void FxBillboard_SetDataRoot(const char* root) {
    if (root && *root) s_dataRoot = root;
}

// =============================================================================================
//  LOADER / TEMPLATE TABLE
// =============================================================================================

// FxParticle_BuildPath 0x4D9E60: Crt_Vsnprintf(dst, "G03_GDATA\\D05_GPARTICLE\\%03d.PARTICLE", index+1).
void FxBillboard_BuildPath(char* dst, size_t dstSize, int index) {
    if (!dst || dstSize == 0) return;
    std::snprintf(dst, dstSize, "G03_GDATA\\D05_GPARTICLE\\%03d.PARTICLE", index + 1);
}

// SObject_EnsureLoadedK 0x4D9EB0: returns the already-loaded template, else loads it on first call.
FxEmitterTemplate* FxBillboard_GetTemplate(int index) {
    if (index < 0 || index >= kFxParticleTemplateCount) return nullptr; // outside the 52 slots
    FxTemplateSlot& slot = s_templates[index];
    if (slot.loaded)                                       // if(*this) return 1  (@0x4D9EBA)
        return slot.node.enabled ? &slot.node : nullptr;
    if (!LoadParticleFile(index, slot.node))               // Fx_NodeLoadFromFile 0x6A6680 (@0x4D9ED7)
        return nullptr;                                    // failure -> loaded stays false (retry possible)
    slot.loaded = true;                                    // *this = 1  (@0x4D9EE7)
    return slot.node.enabled ? &slot.node : nullptr;
}

bool FxBillboard_IsTemplateLoaded(int index) {
    if (index < 0 || index >= kFxParticleTemplateCount) return false;
    return s_templates[index].loaded;
}

void FxBillboard_FreeAllTemplates() {
    for (FxTemplateSlot& slot : s_templates) {
        if (slot.node.texture) {                           // balanced release of GPU textures
            slot.node.texture->Release();
            slot.node.texture = nullptr;
        }
        slot.node.enabled = 0;
        slot.loaded = false;
    }
}

// =============================================================================================
//  LIFECYCLE OF A 48 B POBJECT POOL
// =============================================================================================

// SObject_UpdateK 0x4D9F00: ensure-loaded, then Particle_Init(pool, template) 0x6A7020.
void FxBillboard_PoolInit(FxParticlePool* pool, int index) {
    if (!pool) return;
    FxEmitterTemplate* t = FxBillboard_GetTemplate(index);  // SObject_EnsureLoadedK (@0x4D9F19)
    if (!t) return;
    ZoneFx_Init(pool, t);                                   // Particle_Init(a2, this+104) (@0x4D9F30)
}

// Particle_EnsureLoadedThenUpdateEmit 0x4D9F40: ensure-loaded, then Particle_UpdateEmit 0x6A7530.
void FxBillboard_PoolUpdate(FxParticlePool* pool, int index, float dt,
                            const float pos[3], const float rot[3], FxFrustumFn frustum) {
    if (!pool) return;
    FxEmitterTemplate* t = FxBillboard_GetTemplate(index);  // ensure-loaded (@0x4D9F59)
    if (!t) return;
    ZoneFx_UpdateEmit(pool, dt, pos, rot, frustum);         // Particle_UpdateEmit(a2,a3,a4,a5) (@0x4D9F78)
}

// Binary's update wiring: "if(pool.flag) UpdateEmit; else Init" (mirrors WorldGeometryRenderer::
// updateFx / loop 2 MapColl_UpdateObjectAnim @0x694AF0, and the combat path SObject_UpdateK vs
// Particle_EnsureLoadedThenUpdateEmit). 1st frame -> Init only (allocation, no emission).
void FxBillboard_PoolTick(FxParticlePool* pool, int index, float dt,
                          const float pos[3], const float rot[3], FxFrustumFn frustum) {
    if (!pool) return;
    if (pool->flag)                                         // if(*(pool+0)) (@0x694AF6)
        FxBillboard_PoolUpdate(pool, index, dt, pos, rot, frustum);
    else
        FxBillboard_PoolInit(pool, index);                  // template = this+104[index] (@0x694B13)
}

// Particle_EnsureLoadedThenRender 0x4D9F90: ensure-loaded, then Particle_RenderBillboards 0x6A70B0.
int FxBillboard_PoolRender(FxParticlePool* pool, int index, IDirect3DDevice9* device,
                           const float right[3], const float up[3], int maxQuads, FxFrustumFn frustum) {
    if (!pool) return 0;
    if (!FxBillboard_GetTemplate(index)) return 0;          // ensure-loaded (@0x4D9FA2)
    // Frame parameters for the shared billboard render (Particle_RenderBillboards 0x6A70B0). Internal
    // CPU scratch (the binary has a single VB dword_800080). pool->flag==0 -> ZoneFx_RenderBillboards
    // returns 0 (nothing to draw until the pool is initialized), faithful.
    ZoneFxFrameParams params;
    params.device = device;
    if (right) { params.right[0] = right[0]; params.right[1] = right[1]; params.right[2] = right[2]; }
    if (up)    { params.up[0]    = up[0];    params.up[1]    = up[1];    params.up[2]    = up[2];    }
    params.maxQuads = maxQuads;
    params.scratch  = &s_scratch;
    params.frustum  = frustum;
    return ZoneFx_RenderBillboards(pool, params);           // Particle_RenderBillboards(a2) (@0x4D9FBE)
}

// Particle_Free 0x6A6FF0: frees the pool's particle array (balanced HeapFree).
void FxBillboard_PoolFree(FxParticlePool* pool) {
    ZoneFx_Free(pool);
}

} // namespace ts2::gfx

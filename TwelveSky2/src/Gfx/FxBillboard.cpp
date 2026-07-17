// Gfx/FxBillboard.cpp — implémentation du LEAF billboard « Object A » des FX de combat/skill.
//
// Vérité IDA : TwelveSky2.exe (imagebase 0x400000). Chaque bloc cite son ancre (nom + 0xADDR).
// Voir FxBillboard.h pour la carte d'ensemble et Docs/TS2_EXTRACT_FX_COMBAT.md pour la spec.
//
// Réutilisations (aucune réinvention) :
//   - Moteur Object A : ZoneFx_Init/UpdateEmit/RenderBillboards/Free (Gfx/ZoneFxEmitter.*) = les
//     fonctions IDA Particle_Init 0x6A7020 / Particle_UpdateEmit 0x6A7530 /
//     Particle_RenderBillboards 0x6A70B0 / Particle_Free 0x6A6FF0. Templates/pools = mêmes structs.
//   - Décompression : asset::Zlib (GXDCompress.dll, GXD_DecompressEntity 0x6A1A30).
//   - Lecture bornée : asset::ByteReader ; lecture fichier : asset::ReadWholeFile.
//   - Framing texture/piste = celui de Asset/WorldChunk.cpp::ReadTextureBlock / ReadAnimTrack
//     (Tex_LoadCompressedFromHandle 0x6A9CF0 / Anim_LoadQuatTrackFromHandle 0x6AAE20). Ces
//     helpers y sont file-local (non exportés) ; on réplique ICI leur framing prouvé (identique
//     bit-à-bit), car le .PARTICLE autonome n'est PAS un chunk .WP complet mais un nœud nu.
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
// État de module.
// -------------------------------------------------------------------------------------------
IDirect3DDevice9* s_device   = nullptr;                 // g_GfxRenderer device 0x7FFE18
std::string       s_dataRoot = "GameData";              // racine GameData (préfixe du chemin .PARTICLE)

// Scratch CPU des sommets billboard — le binaire utilise UN seul VB partagé (dword_800080) pour
// tous les émetteurs Object A ; on possède donc un unique buffer réutilisé à chaque rendu.
std::vector<Billboard_Vertex> s_scratch;

// Un slot de table de template (SObject_EnsureLoadedK 0x4D9EB0 : {loaded@+0, path@+4, node@+104}).
// On garde la donnée en membres C++ (agencement mémoire non contraint — cf. FxBillboard.h).
struct FxTemplateSlot {
    bool              loaded = false;   // flag @+0  (0x4D9EBA : if(*this) return 1)
    FxEmitterTemplate node{};           // node @+104 (232o) — rempli par le loader .PARTICLE
};
FxTemplateSlot s_templates[kFxParticleTemplateCount];

// -------------------------------------------------------------------------------------------
// Framing texture — miroir de Asset/WorldChunk.cpp::ReadTextureBlock (Tex_LoadCompressedFromHandle
// 0x6A9CF0). [imageSize u32] ; si 0 -> texture absente (4o consommés, dds vide). Sinon
// [rawSize u32][packedSize u32][zlib packed] -> inflate = image(imageSize)+trailer(8o) ;
// on renvoie les `imageSize` premiers octets (le DDS/BMP décodé). Lève AssetError si framing KO.
// -------------------------------------------------------------------------------------------
std::vector<uint8_t> ReadFxTextureBlock(asset::ByteReader& r) {
    std::vector<uint8_t> dds;
    const uint32_t imageSize = r.U32();                 // [imageSize]
    if (imageSize == 0) return dds;                     // absente (present=false)
    const uint32_t rawSize = r.U32();                   // [rawSize]
    const uint32_t packed  = r.U32();                   // [packedSize]
    if (r.Remaining() < packed)
        throw asset::AssetError("FX .PARTICLE : texture packedSize dépasse le flux");
    std::vector<uint8_t> out = asset::Zlib::Instance().InflateTo(r.Ptr(), packed, rawSize);
    r.Skip(packed);
    if (rawSize != imageSize + 8)                       // trailer 8o (cf. ReadTextureBlock @WorldChunk)
        throw asset::AssetError("FX .PARTICLE : texture rawSize != imageSize+8");
    dds.assign(out.begin(), out.begin() + imageSize);   // image décodée = imageSize premiers octets
    return dds;
}

// -------------------------------------------------------------------------------------------
// Framing piste de quaternions — miroir de Asset/WorldChunk.cpp::ReadAnimTrack
// (Anim_LoadQuatTrackFromHandle 0x6AAE20). [present u32] ; si !=0 -> bloc GXD
// [rawSize u32][packedSize u32][zlib packed]. On CONSOMME (skip) le bloc : le chemin keyframe
// (template+56/frameMatrices) n'est PAS porté (else-branch identité de UpdateEmit @0x6A78C3,
// cf. ZoneFxEmitter). Il faut néanmoins avancer le curseur pour atteindre les 144o de champs.
// -------------------------------------------------------------------------------------------
void SkipFxAnimTrack(asset::ByteReader& r) {
    const uint32_t present = r.U32();                   // [present]
    if (present == 0) return;
    /*rawSize*/ (void)r.U32();                          // [rawSize] (non utilisé : on saute)
    const uint32_t packed = r.U32();                    // [packedSize]
    if (r.Remaining() < packed)
        throw asset::AssetError("FX .PARTICLE : piste quat packedSize dépasse le flux");
    r.Skip(packed);                                     // saut du bloc GXD
}

// -------------------------------------------------------------------------------------------
// Crée une texture GPU depuis un DDS/BMP en mémoire (D3DPOOL_MANAGED). C'est ce que fait
// Tex_LoadCompressedFromHandle 0x6A9CF0 en interne une fois le flux inflaté (D3DXCreateTexture-
// FromFileInMemory*). Identique à WorldGeometryRenderer::createTextureFromBlock (@0x6A3040).
// dds vide OU device nul => nullptr (SetTexture(0,nullptr) au rendu, fidèle au « pas de texture »).
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
// LOADER d'un fichier .PARTICLE autonome dans un template 232o — miroir de
// Fx_NodeLoadFromFile 0x6A6680. Layout disque = [texture block][piste quat][144o de champs]
// (PAS de present-flag en tête, contrairement au nœud .WP de ReadFxNode). Ordre STRICT :
//   1) Tex_LoadCompressedFromHandle 0x6A9CF0  (this+1  = texture)      @0x6A66E1
//   2) Anim_LoadQuatTrackFromHandle 0x6AAE20  (this+14 = piste quat)   @0x6A6715
//   3) 18 ReadFile -> [runtime +72, +216) = 144o de champs             @0x6A6760..@0x6A69B4
//   4) *this = 1 (enabled)                                             @0x6A69DA
// Échec (fichier absent / framing KO) -> node laissé désactivé (WorldObjectB_Free @0x6A6767).
// Renvoie true si le template est prêt (enabled=1).
// -------------------------------------------------------------------------------------------
bool LoadParticleFile(int index, FxEmitterTemplate& node) {
    // FxParticle_BuildPath 0x4D9E60 + racine GameData.
    char rel[128];
    FxBillboard_BuildPath(rel, sizeof(rel), index);
    const std::string full = s_dataRoot + "\\" + rel;

    // CreateFileA(...) — échec => *this=0, node non chargé (@0x6A66B1).
    std::vector<uint8_t> file;
    if (!asset::ReadWholeFile(full, file)) {
        TS2_WARN("FxBillboard: .PARTICLE introuvable (%s)", full.c_str());
        return false;
    }

    try {
        asset::ByteReader r(file);
        std::vector<uint8_t> dds = ReadFxTextureBlock(r); // 1) texture (0x6A66E1)
        SkipFxAnimTrack(r);                               // 2) piste quat (0x6A6715 ; keyframe non porté)
        uint8_t fields[kFxTemplateDiskSize];              // 3) 144o [runtime +72,+216)
        r.Read(fields, kFxTemplateDiskSize);              //    (18 ReadFile agrégés)

        IDirect3DTexture9* gpuTex = CreateFxTexture(s_device, dds);
        // ZoneFx_BuildTemplate = Fx_NodeLoadFromFile @0x6A69DA (enabled=1) + recopie 144o à (tmpl+72)
        // + texture GPU à (tmpl+52). colorRate NON dérivé ici (fait par ZoneFx_Init/ComputeGradients).
        ZoneFx_BuildTemplate(&node, fields, kFxTemplateDiskSize, gpuTex);
        return true;
    } catch (const asset::AssetError& e) {
        // Framing corrompu / fichier tronqué -> échec du ReadFile (@0x6A6760 -> WorldObjectB_Free).
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
//  LOADER / TABLE DE TEMPLATES
// =============================================================================================

// FxParticle_BuildPath 0x4D9E60 : Crt_Vsnprintf(dst, "G03_GDATA\\D05_GPARTICLE\\%03d.PARTICLE", index+1).
void FxBillboard_BuildPath(char* dst, size_t dstSize, int index) {
    if (!dst || dstSize == 0) return;
    std::snprintf(dst, dstSize, "G03_GDATA\\D05_GPARTICLE\\%03d.PARTICLE", index + 1);
}

// SObject_EnsureLoadedK 0x4D9EB0 : renvoie le template déjà chargé, sinon le charge au 1er appel.
FxEmitterTemplate* FxBillboard_GetTemplate(int index) {
    if (index < 0 || index >= kFxParticleTemplateCount) return nullptr; // hors 52 slots
    FxTemplateSlot& slot = s_templates[index];
    if (slot.loaded)                                       // if(*this) return 1  (@0x4D9EBA)
        return slot.node.enabled ? &slot.node : nullptr;
    if (!LoadParticleFile(index, slot.node))               // Fx_NodeLoadFromFile 0x6A6680 (@0x4D9ED7)
        return nullptr;                                    // échec -> loaded reste false (retry possible)
    slot.loaded = true;                                    // *this = 1  (@0x4D9EE7)
    return slot.node.enabled ? &slot.node : nullptr;
}

bool FxBillboard_IsTemplateLoaded(int index) {
    if (index < 0 || index >= kFxParticleTemplateCount) return false;
    return s_templates[index].loaded;
}

void FxBillboard_FreeAllTemplates() {
    for (FxTemplateSlot& slot : s_templates) {
        if (slot.node.texture) {                           // Release équilibré des textures GPU
            slot.node.texture->Release();
            slot.node.texture = nullptr;
        }
        slot.node.enabled = 0;
        slot.loaded = false;
    }
}

// =============================================================================================
//  CYCLE DE VIE D'UN POOL POBJECT 48o
// =============================================================================================

// SObject_UpdateK 0x4D9F00 : ensure-loaded, puis Particle_Init(pool, template) 0x6A7020.
void FxBillboard_PoolInit(FxParticlePool* pool, int index) {
    if (!pool) return;
    FxEmitterTemplate* t = FxBillboard_GetTemplate(index);  // SObject_EnsureLoadedK (@0x4D9F19)
    if (!t) return;
    ZoneFx_Init(pool, t);                                   // Particle_Init(a2, this+104) (@0x4D9F30)
}

// Particle_EnsureLoadedThenUpdateEmit 0x4D9F40 : ensure-loaded, puis Particle_UpdateEmit 0x6A7530.
void FxBillboard_PoolUpdate(FxParticlePool* pool, int index, float dt,
                            const float pos[3], const float rot[3], FxFrustumFn frustum) {
    if (!pool) return;
    FxEmitterTemplate* t = FxBillboard_GetTemplate(index);  // ensure-loaded (@0x4D9F59)
    if (!t) return;
    ZoneFx_UpdateEmit(pool, dt, pos, rot, frustum);         // Particle_UpdateEmit(a2,a3,a4,a5) (@0x4D9F78)
}

// Câblage update du binaire : « if(pool.flag) UpdateEmit ; else Init » (miroir WorldGeometryRenderer::
// updateFx / boucle 2 MapColl_UpdateObjectAnim @0x694AF0, et chemin combat SObject_UpdateK vs
// Particle_EnsureLoadedThenUpdateEmit). 1re frame -> Init seul (allocation, pas d'émission).
void FxBillboard_PoolTick(FxParticlePool* pool, int index, float dt,
                          const float pos[3], const float rot[3], FxFrustumFn frustum) {
    if (!pool) return;
    if (pool->flag)                                         // if(*(pool+0)) (@0x694AF6)
        FxBillboard_PoolUpdate(pool, index, dt, pos, rot, frustum);
    else
        FxBillboard_PoolInit(pool, index);                  // template = this+104[index] (@0x694B13)
}

// Particle_EnsureLoadedThenRender 0x4D9F90 : ensure-loaded, puis Particle_RenderBillboards 0x6A70B0.
int FxBillboard_PoolRender(FxParticlePool* pool, int index, IDirect3DDevice9* device,
                           const float right[3], const float up[3], int maxQuads, FxFrustumFn frustum) {
    if (!pool) return 0;
    if (!FxBillboard_GetTemplate(index)) return 0;          // ensure-loaded (@0x4D9FA2)
    // Paramètres de frame du rendu billboard partagé (Particle_RenderBillboards 0x6A70B0). Scratch
    // CPU interne (le binaire a un VB unique dword_800080). pool->flag==0 -> ZoneFx_RenderBillboards
    // renvoie 0 (rien à dessiner tant que le pool n'est pas initialisé), fidèle.
    ZoneFxFrameParams params;
    params.device = device;
    if (right) { params.right[0] = right[0]; params.right[1] = right[1]; params.right[2] = right[2]; }
    if (up)    { params.up[0]    = up[0];    params.up[1]    = up[1];    params.up[2]    = up[2];    }
    params.maxQuads = maxQuads;
    params.scratch  = &s_scratch;
    params.frustum  = frustum;
    return ZoneFx_RenderBillboards(pool, params);           // Particle_RenderBillboards(a2) (@0x4D9FBE)
}

// Particle_Free 0x6A6FF0 : libère le tableau de particules du pool (HeapFree équilibré).
void FxBillboard_PoolFree(FxParticlePool* pool) {
    ZoneFx_Free(pool);
}

} // namespace ts2::gfx

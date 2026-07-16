// Asset/Texture.h — lecteur des textures diverses de TwelveSky2.
// Traduction fidèle de RE/asset_parsers/textures.py (validé 203/205).
// ex-VeryOldClient: TEXTURE_FOR_GXD (LoadFromTGA/LoadFromDDS) — noms/logique de lecture ; côté
// cible = zlib seul, aucune substitution GXCW (LoadGXCW/Load2 = artefact VeryOld, CONFLICT §4.B).
//
// Trois familles chargées par le client (loaders IDA entre []) :
//   (TGA)    .tga brut, en-tête TGA 18 o standard. Le client n'accepte au
//            converter DXT [Tex_LoadTgaConvert 0x417180] que le type 2
//            (true-color 24/32 bpp, dimensions puissance de 2) ; les autres
//            (gris 8 bpp, colormap) sont chargés directement par D3DX.
//   (SHADOW) .SHADOW = DDS standard ("DDS " + header 124 o), FourCC DXT1/3/5.
//            [Tex_LoadFromFile 0x6A9910] n'accepte QUE DXT1/DXT3/DXT5.
//   (IMG)    enveloppe GXD [rawSize:u32][packedSize:u32][flux zlib] -> après inflate,
//            en-tête GXD 36 o + fichier DDS Microsoft embarqué (famille T). Matérialisée
//            ici via LoadFromImgFile / LoadFromImgTexturePayload (parse l'en-tête GXD puis
//            délègue le DDS embarqué à LoadFromDdsMemory) ; la famille D (table de données)
//            reste hors périmètre. cTexture_LoadFromImgFile 0x457A20 (UI 2D) /
//            Tex_LoadCompressedDDS 0x6A2E80 (minimaps) — format PROUVÉ identique.
//
// Ce lecteur va plus loin que le parseur Python : il MATÉRIALISE les pixels.
//   - TGA  -> décodé en RGBA8 top-down (32 bpp), prêt à l'upload.
//   - DDS  -> conserve les blocs DXT compressés tels quels (le GPU/D3DX décode) ;
//             fourCC + mip count + dimensions exposés.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

class ImgFile; // Asset/ImgFile.h — source du payload GXD décompressé (LoadFromImgFile)

// FourCC DXT acceptés par les loaders de texture (Tex_LoadFromFile 0x6A9910).
constexpr uint32_t kFourCC_DXT1 = 0x31545844u; // "DXT1"
constexpr uint32_t kFourCC_DXT3 = 0x33545844u; // "DXT3"
constexpr uint32_t kFourCC_DXT5 = 0x35545844u; // "DXT5"

// Famille de fichier détectée (cf. sniff() de textures.py).
enum class TextureFamily {
    Unknown,
    Tga,      // TGA brut décodé en RGBA8
    Dds,      // DDS/.SHADOW : blocs DXT (ou données brutes RGB/A) conservés
    ImgZip,   // enveloppe "PK\x03\x04" (GLS.IMG, launcher) — jamais décodée : ImgFile::Load()
              // refuse aussi ce cas (hors périmètre du client de jeu, aucun décodeur ZIP ici)
    ImgGxd,   // enveloppe [rawSize][packedSize][zlib] — LoadFile la MATÉRIALISE désormais en
              // texture (famille T) via ImgFile + LoadFromImgFile ; famille D (table) -> pixels vides
};

// Interprétation des octets du champ `pixels`.
enum class PixelFormat {
    Unknown,
    RGBA8,     // TGA décodé : 4 o/pixel, ordre R,G,B,A, top-down
    DxtBlocks, // DDS DXT1/3/5 : blocs compressés (payload après header 128 o)
    DdsRaw,    // DDS non-DXT (pixelformat RGB/A) : octets bruts après le header
};

struct Texture {
    TextureFamily family = TextureFamily::Unknown;
    uint32_t width  = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::Unknown;

    // ----- DDS ------------------------------------------------------------
    std::string fourCC;              // "DXT1"/"DXT3"/"DXT5" (nuls retirés) ; vide si N/A
    uint32_t    fourCCValue = 0;     // FourCC en u32 LE
    uint32_t    mipCount    = 0;     // dwMipMapCount (0 => 1 niveau implicite)
    bool        acceptedByLoader = false; // true si DXT1/3/5 (Tex_LoadFromFile)

    // ----- TGA ------------------------------------------------------------
    uint8_t tgaImageType = 0;        // 1 colormap, 2 truecolor, 3 gray, 9/10/11 RLE
    uint8_t bpp = 0;                 // bits/pixel source (8/16/24/32)
    bool    topOrigin   = false;     // descriptor bit5 : origine haut-gauche
    bool    rightOrigin = false;     // descriptor bit4 : origine à droite
    bool    convertibleDxt = false;  // éligible au converter DXT (type2 24/32 pow2)

    // ----- IMG (enveloppe + en-tête GXD famille T) -----------------------
    uint32_t imgRawSize    = 0;      // ImgGxd : rawSize annoncé (enveloppe)
    uint32_t imgPackedSize = 0;      // ImgGxd : packedSize annoncé (enveloppe)
    // Dimensions LOGIQUES du sprite (en-tête GXD +0/+4), DISTINCTES de la surface
    // physique pow2 du DDS embarqué (celle-ci -> width/height ci-dessus). 0 hors .IMG
    // famille T. cTexture_LoadFromImgFile 0x457A20 (qmemcpy en-tête brut 0x1C o @0x457B67,
    // AVANT tout NextPow2) ; getters Sprite2D_GetWidth 0x4D6CD0 / Sprite2D_GetHeight 0x4D6D20.
    // Ex. 001_00001.IMG : logique 261x90, DDS embarqué 512x128.
    uint32_t imgLogicalWidth  = 0;
    uint32_t imgLogicalHeight = 0;

    // Données pixel/bloc (cf. PixelFormat). Vide pour ImgZip/ImgGxd.
    std::vector<uint8_t> pixels;

    bool Empty() const { return pixels.empty(); }
    void Clear();

    // Point d'entrée universel : dispatche par magic (puis extension implicite).
    // Renvoie true pour TGA/DDS décodés, et aussi pour ImgZip/ImgGxd détectés
    // (dans ce cas pixels reste vide : passer par ImgFile pour décompresser).
    bool LoadFile(const std::string& path);

    // Chargements explicites par famille.
    bool LoadTGA(const std::string& path);
    bool LoadDDS(const std::string& path);

    // Décode un DDS Microsoft standard déjà en mémoire (magic "DDS ", header 124 o).
    // Sources : .SHADOW extrait d'une NPK, OU le fichier DDS embarqué dans un .IMG
    // famille T à l'offset +36 (cf. LoadFromImgTexturePayload ci-dessous).
    bool LoadFromDdsMemory(const uint8_t* data, size_t size);

    // ---- Matérialisation .IMG texture GXD (famille T) --------------------------------
    // cTexture_LoadFromImgFile 0x457A20 (UI 2D) / Tex_LoadCompressedDDS 0x6A2E80 (minimaps)
    // — format PROUVÉ identique (extraction réelle : 001/002/007 + Z*_MINIMAP). `img` doit
    // être un ImgFile de kind==TextureDxt (payload déjà décompressé & classé). Parse
    // l'en-tête GXD 36 o (width@+0 / height@+4 / FourCC@+28 / dataSize@+32) puis matérialise
    // le fichier DDS embarqué (+36) en blocs DXT via LoadFromDdsMemory. Résultat :
    //   width/height          = dimensions PHYSIQUES pow2 du DDS (surface D3D9),
    //   imgLogicalWidth/Height = dimensions LOGIQUES du sprite (en-tête GXD).
    // Couche CPU pure : AUCUNE dépendance D3D9 ici — l'upload GPU reste gfx::GpuTexture
    // (CreateFromTexture = blocs DXT via LockRect ; CreateFromImgFile = réplique D3DX de
    // 0x457A20). Voir la note détaillée dans Texture.cpp.
    bool LoadFromImgFile(const ImgFile& img);
    bool LoadFromImgTexturePayload(const uint8_t* payload, size_t size);

    // Décode un TGA déjà en mémoire.
    bool LoadFromTgaMemory(const uint8_t* data, size_t size);

    // Détecte la famille d'un buffer sans le décoder (cf. sniff() du parseur).
    static TextureFamily Sniff(const uint8_t* data, size_t size);
};

} // namespace ts2::asset

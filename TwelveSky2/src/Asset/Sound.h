// Asset/Sound.h — lecteur audio TwelveSky2.
//   .WSOUND  = banque de sons d'ambiance par zone (conteneur de METADONNEES).
//   .ISN     = effets (D06_GSOUND)      -> Ogg Vorbis brut.
//   .BGM     = musique (D10_WORLDBGM)   -> Ogg Vorbis brut.
//   *.OGG    = sons extraits du banc    -> Ogg Vorbis brut.
//
// Fidèle à RE/asset_parsers/wsound.py (validé 75/75).
//
// === Sources IDA (vérité unique) ===
//   WSndBank_LoadFile         0x4DA790  -> parse le conteneur .WSOUND
//   WSndBank_UpdatePositional 0x4DAC30  -> logique 3D : layout du record 20 o
//   Snd_LoadOggToBuffers      0x6A8120  -> décode un Ogg (ov_open, exige stéréo/44100)
//   World_LoadZoneResource    0x4DCB60  -> chemin "G03_GDATA\D09_WSOUND\Z%03d\Z%03d.WSOUND"
//   OGG externes (Crt_Vsnprintf)        -> "<base>_%04d.OGG", index 1-base
//
// === IMPORTANT ===
//   Le conteneur .WSOUND NE contient PAS les données Ogg. Chaque son est chargé
//   depuis un fichier externe "<base>_%04d.OGG" (i=1..count). Le record 100 o
//   porte le nom source .ogg + 36 o de champs runtime périmés (l'outil de build
//   a sérialisé de la mémoire vive : adresses pile/ntdll). Le chemin de lecture
//   ne s'appuie que sur l'index -> ces champs sont ignorés au chargement.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// Signature de page Ogg ("OggS").
inline constexpr char kOggMagic[4] = {'O', 'g', 'g', 'S'};

// Vrai si le tampon commence par la signature de page Ogg "OggS".
bool IsOgg(const uint8_t* data, size_t size);
inline bool IsOgg(const std::vector<uint8_t>& v) { return IsOgg(v.data(), v.size()); }

// Charge un Ogg Vorbis brut (.ISN / .BGM / .OGG) tel quel en mémoire.
// Renvoie false si ouverture impossible ou si l'entête n'est pas "OggS".
bool ReadOggFile(const std::string& path, std::vector<uint8_t>& out);

// Métadonnées d'un son (record 100 o à this+8). Seul le nom est exploité par le
// jeu ; les 36 o restants sont des champs runtime périmés (cf. entête).
struct WSoundEntry {
    std::string name;              // nom source ".ogg" (buffer 64 o, null-terminé)
    std::vector<uint8_t> oggData;  // vide tant que LoadExternalOggs() n'a pas été appelé
                                   // (le conteneur .WSOUND n'embarque PAS l'audio)
};

// Émetteur positionnel (record 20 o à this+20, exploité par WSndBank_UpdatePositional).
struct WSoundEmitter {
    uint32_t soundIndex = 0;  // 0-base, == index du record 100 o / OGG
    float    x = 0.0f;
    float    y = 0.0f;
    float    z = 0.0f;        // Math_Dist3D(rec+4, listener)
    float    radius = 0.0f;   // portée ; si dist < radius -> joue, volume ~ (radius-d)/radius
};

// Conteneur .WSOUND.
//
// Layout disque :
//   [u32 count]                nb de sons (> 0)                 (this+8)
//   count  * { record 100 o }  métadonnées par son
//   [u32 count2]               nb d'émetteurs positionnels      (this+16)
//   count2 * { record 20  o }  placements 3D                    (this+20)
//   Taille attendue == 4 + 100*count + 4 + 20*count2
class WSound {
public:
    // Parse le conteneur .WSOUND (métadonnées + émetteurs). Ne charge PAS l'audio.
    // Renvoie false si la structure est incohérente (bornes/tailles).
    bool Load(const std::string& path);

    // Charge les OGG externes "<base>_%04d.OGG" (i=1..count) et remplit
    // WSoundEntry::oggData. Renvoie le nombre d'OGG "OggS" valides chargés.
    // Ne fait rien tant que Load() n'a pas réussi.
    size_t LoadExternalOggs();

    // Chemin de l'OGG externe pour un index 1-base (ex. "Z001.WSOUND_0001.OGG").
    std::string OggPathFor(size_t oneBasedIndex) const;

    const std::string&               Path()     const { return path_; }
    const std::vector<WSoundEntry>&  Entries()  const { return entries_; }
    const std::vector<WSoundEmitter>& Emitters() const { return emitters_; }

    uint32_t Count()  const { return count_; }   // nb de sons
    uint32_t Count2() const { return count2_; }  // nb d'émetteurs

    // Validation (miroir du validateur Python).
    bool   SizeOk()        const { return sizeOk_; }        // taille disque == attendue
    size_t ExpectedSize()  const { return expectedSize_; }
    size_t ActualSize()    const { return actualSize_; }
    size_t Trailing()      const { return trailing_; }      // octets après le dernier record
    size_t BadIndexCount() const { return badIndex_; }      // émetteurs à soundIndex hors [0,count)

private:
    std::string                path_;
    std::vector<WSoundEntry>   entries_;
    std::vector<WSoundEmitter> emitters_;
    uint32_t count_  = 0;
    uint32_t count2_ = 0;
    bool     sizeOk_ = false;
    size_t   expectedSize_ = 0;
    size_t   actualSize_   = 0;
    size_t   trailing_     = 0;
    size_t   badIndex_     = 0;
};

} // namespace ts2::asset

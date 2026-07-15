// Audio/Sound3D.h — émetteur sonore positionnel « Snd3D » + banque d'ambiance WSndBank.
//
// === Sources IDA (idaTs2 — vérité unique) ===
//   Snd3D_Construct         0x4DA010  ctor : SoundObj + section critique
//   Snd3D_SetISNPath        0x4DA0C0  construit le chemin .ISN selon le type (1..6)
//   Snd3D_EnsureLoaded      0x4DA270  charge sync (verrou) OU enfile sur le thread loader
//   Snd3D_PlayScaledVolume  0x4DA380  joue à (master * pourcent / 100)
//   Snd3D_PlayFullVolume    0x4DA3F0  joue à volume maître plein
//   Snd3D_PlayPositional    0x4DA450  atténuation linéaire sur 300 unités
//   Snd3D_UnloadIfExpired    0x4DA340  décharge si inactif au-delà du TTL
//   Snd3D_Unload            0x4DA230  Release sous verrou
//   WSndBank_UpdatePositional 0x4DAC30 banque : plus proche émetteur -> Play/Update/Stop
//
// Layout d'origine du Snd3D (octets) :
//   +0    drapeau « chargé »
//   +4    chemin source .ISN (buffer chaîne)
//   +104  SoundObj embarqué (60 o)
//   +164  horodatage dernière lecture (float, secondes de jeu)
//   +168  RTL_CRITICAL_SECTION
//
// Module autonome (leaf). Namespace ts2::audio.
#pragma once
#include "Audio/AudioSystem.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ts2::audio {

// Vecteur 3D minimal (le binaire lit 3 floats consécutifs : x,y,z).
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Portée d'atténuation de Snd3D_PlayPositional 0x4DA450 (constante 300.0 en dur).
inline constexpr float kPositionalFalloff = 300.0f;

// Construit un chemin source .ISN — miroir EXACT de Snd3D_SetISNPath 0x4DA0C0.
//   type 1 : "G03_GDATA\D06_GSOUND\001\C%03d%03d%03d.ISN"  (a+3*b+1, c+1, d+1)
//   type 2 : "...\002\N%03d001%03d.ISN"                    (a+1, b+1)
//   type 3 : "...\003\M%03d001%03d.ISN"                    (a+1, b+1)
//   type 4 : "...\004\E%03d001001.ISN"                     (a+1)
//   type 5 : "...\005\H%03d%03d%03d.ISN"                   (a+3*b+1, c+1, d+1)
//   type 6 : "...\006\X%03d%03d%03d.ISN"                   (a+3*b+1, c+1, d+1)
//   autre  : chaîne vide
std::string BuildIsnPath(int type, int a = 0, int b = 0, int c = 0, int d = 0);

// ---------------------------------------------------------------------------
// Emitter — objet « Snd3D » : un SoundBuffer en mode Pool + chemin source + TTL.
// Thread-safe (verrou interne), car chargeable depuis le thread loader.
// ---------------------------------------------------------------------------
class Emitter {
public:
    Emitter() = default;

    // Snd3D_SetISNPath : fixe le chemin source (aussi utilisable avec un chemin direct).
    void SetSource(int type, int a = 0, int b = 0, int c = 0, int d = 0);
    void SetSourcePath(std::string path) {
        std::lock_guard<std::mutex> lk(mutex_);
        path_ = std::move(path);
    }
    const std::string& SourcePath() const { return path_; }

    // Snd3D_EnsureLoaded 0x4DA270.
    //   sync=true  : charge immédiatement (sous verrou) et rafraîchit l'horodatage.
    //   sync=false : enfile la demande sur le thread loader (retour immédiat).
    // `nowSec` = temps de jeu courant (g_GameTimeSec / g_FrameAccumSec) pour le TTL.
    bool EnsureLoaded(bool sync, float nowSec);

    // Snd3D_PlayScaledVolume 0x4DA380 : joue à (master * percent / 100). percent 0..100.
    bool PlayScaledVolume(int percent, float nowSec);
    // Snd3D_PlayFullVolume 0x4DA3F0 : joue à volume maître plein.
    bool PlayFullVolume(float nowSec);
    // Snd3D_PlayPositional 0x4DA450 : volume = (300-dist)/300 * master, muet au-delà de 300.
    bool PlayPositional(const Vec3& listener, const Vec3& source, float nowSec);

    // Snd3D_UnloadIfExpired 0x4DA340 : décharge si (nowSec - dernierPlay) > ttl.
    void UnloadIfExpired(float nowSec, float ttl);
    // Snd3D_Unload 0x4DA230 : Release sous verrou.
    void Unload();

    bool Loaded() const;
    float LastPlaySec() const { return lastPlaySec_; }

    // Nb de buffers dupliqués du pool (Snd_LoadOggToBuffers appelé avec count=2 pour les .ISN).
    void SetPoolCount(int n) { poolCount_ = n < 1 ? 1 : n; }

private:
    bool LoadLocked(float nowSec);   // corps commun (verrou déjà pris)

    mutable std::mutex mutex_;                 // Snd3D+168 (RTL_CRITICAL_SECTION)
    std::string path_;                          // Snd3D+4
    SoundBuffer buffer_;                        // Snd3D+104 (SoundObj)
    float lastPlaySec_ = 0.0f;                  // Snd3D+164
    int   poolCount_ = 2;                       // .ISN chargés en pool de 2 (Snd3D_EnsureLoaded)
};

// ---------------------------------------------------------------------------
// SoundBank — miroir runtime de WSndBank (ambiance de zone .WSOUND).
// N sons bouclés + M émetteurs ; à chaque frame on cherche l'émetteur le plus
// proche de l'auditeur par son, et on ajuste volume/lecture par distance.
//   WSndBank_UpdatePositional 0x4DAC30.
// Le conteneur .WSOUND est parsé ailleurs (Asset/Sound : ts2::asset::WSound) ;
// on reçoit ici la liste de sons (PCM/chemins) et d'émetteurs.
// ---------------------------------------------------------------------------
class SoundBank {
public:
    // Émetteur positionnel = record 20 o de WSndBank (soundIndex + x,y,z + radius).
    struct BankEmitter {
        uint32_t soundIndex = 0;
        Vec3     pos;
        float    radius = 0.0f;
    };

    // Charge N sons bouclés depuis leurs chemins (via le callback PCM) + les émetteurs.
    // Chaque son est un SoundBuffer en mode Loop (kind 2 comme WSndBank_LoadFile).
    bool Load(const std::vector<std::string>& soundPaths,
              const std::vector<BankEmitter>& emitters);

    // WSndBank_UpdatePositional : `enable`=false coupe tout ; sinon, pour chaque son,
    // cherche l'émetteur le plus proche ; si dist < radius joue/ajuste, sinon coupe.
    //   `enableScale` (a3 du binaire) module l'intensité (0..100), typiquement 100.
    void UpdatePositional(const Vec3& listener, bool enable, int enableScale = 100);

    void Clear();
    size_t SoundCount()   const { return sounds_.size(); }
    size_t EmitterCount() const { return emitters_.size(); }

private:
    std::vector<std::unique_ptr<SoundBuffer>> sounds_;  // WSndBank+1 (SoundObj[N])
    std::vector<uint8_t> playing_;                      // WSndBank+3 (drapeaux « en lecture »)
    std::vector<BankEmitter> emitters_;                 // WSndBank+5 (records 20 o)
};

} // namespace ts2::audio

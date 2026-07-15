// Audio/Sound3D.h — émetteur sonore positionnel « Snd3D » + banque d'ambiance WSndBank.
//
// === Sources IDA (idaTs2 — vérité unique) ===
//   Snd3D_Construct         0x4DA010  ctor : SoundObj + section critique
//                                     // ex-VeryOldClient: GSOUND::GSOUND (mDATA.Init + InitializeCriticalSection)
//   Snd3D_SetISNPath        0x4DA0C0  construit le chemin .ISN selon le type (1..6)
//                                     // ex-VeryOldClient: GSOUND::Init (switch identique — CONFIRMED)
//   Snd3D_EnsureLoaded      0x4DA270  charge sync (verrou) OU enfile sur le thread loader
//                                     // ex-VeryOldClient: GSOUND::Load (sync vs CSoundLoaderThread__PushJob)
//   Snd3D_PlayScaledVolume  0x4DA380  joue à (master * pourcent / 100)
//                                     // ex-VeryOldClient: GSOUND::Play (mDATA.Play(loop, tVolumeSize*mSoundOption[1]*0.01))
//   Snd3D_PlayFullVolume    0x4DA3F0  joue à volume maître plein
//                                     // ex-VeryOldClient: GSOUND::Play2 (gate mSoundOption[1]==0 ; Play(FALSE, mSoundOption[1], 0))
//   Snd3D_PlayPositional    0x4DA450  atténuation linéaire sur 300 unités
//                                     // ex-VeryOldClient: GSOUND::Play3 (falloff 300.0f, (300-len)/300*mSoundOption[1])
//   Snd3D_UnloadIfExpired    0x4DA340  décharge si inactif au-delà du TTL
//                                     // ex-VeryOldClient: GSOUND::ProcessForMemory (if (present-mLastUsedTime)>len Free)
//   Snd3D_Unload            0x4DA230  Release sous verrou
//                                     // ex-VeryOldClient: GSOUND::Free (EnterCriticalSection + mDATA.Free)
//   WSndBank_UpdatePositional 0x4DAC30 banque : plus proche émetteur -> Play/Update/Stop
//                                     // ex-VeryOldClient: WSOUND_FOR_GXD::Play (S03_GWSound.cpp — CONFIRMED)
//
// === Indicateur VeryOldClient (build différent/altéré — NOMS/idiomes seulement) ===
//   Niveau émetteur = classe GSOUND (S03_GSound.cpp) ; banque de zone = WSOUND_FOR_GXD +
//   SOUNDINFO_FOR_GXD (S03_GWSound.cpp / H03_GData.h). Corrélation CONFIRMED contre l'IDA
//   (§E du Docs/TS2_AUDIO_ROSETTA.md). Aucune valeur/adresse transposée depuis VeryOldClient.
//
// Layout d'origine du Snd3D (octets) — ex-VeryOldClient: classe GSOUND (S03_GSound.cpp) :
//   +0    drapeau « chargé »                          // mCheckValidState
//   +4    chemin source .ISN (buffer chaîne)          // mFileName
//   +104  SoundObj embarqué (60 o)                    // mDATA (SOUNDDATA_FOR_GXD)
//   +164  horodatage dernière lecture (float, secondes de jeu) // mLastUsedTime
//   +168  RTL_CRITICAL_SECTION                        // mLock
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
// ex-VeryOldClient: GSOUND::Play3 — `if (tLength > 300.0f) return;`. CONFIRMED (constante IDA).
inline constexpr float kPositionalFalloff = 300.0f;

// Construit un chemin source .ISN — miroir EXACT de Snd3D_SetISNPath 0x4DA0C0.
// ex-VeryOldClient: GSOUND::Init — `switch(tValue01)` byte-identique (mêmes 6 gabarits sprintf,
//   mêmes index a+3*b+1 / c+1 / d+1). CONFIRMED — indicateur de nom/idiome le plus fort du front.
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

    mutable std::mutex mutex_;                 // Snd3D+168 (RTL_CRITICAL_SECTION) — ex-VeryOldClient: GSOUND::mLock
    std::string path_;                          // Snd3D+4   — ex-VeryOldClient: GSOUND::mFileName
    SoundBuffer buffer_;                        // Snd3D+104 (SoundObj) — ex-VeryOldClient: GSOUND::mDATA (SOUNDDATA_FOR_GXD)
    float lastPlaySec_ = 0.0f;                  // Snd3D+164 — ex-VeryOldClient: GSOUND::mLastUsedTime
    int   poolCount_ = 2;                       // .ISN chargés en pool de 2 (Snd3D_EnsureLoaded 0x4DA270 — valeur prouvée IDA)
                                                // ex-VeryOldClient: GSOUND::Load -> mDATA.LoadFromOGG(mFileName, 3, 4, 1) : sort=3 CONFIRMED,
                                                //   mais dup=4 = valeur du build VeryOld, DIVERGE du 2 prouvé IDA -> NON transposée (IDA gagne).
};

// ---------------------------------------------------------------------------
// SoundBank — miroir runtime de WSndBank (ambiance de zone .WSOUND).
// N sons bouclés + M émetteurs ; à chaque frame on cherche l'émetteur le plus
// proche de l'auditeur par son, et on ajuste volume/lecture par distance.
//   WSndBank_UpdatePositional 0x4DAC30.
// ex-VeryOldClient: classe WSOUND_FOR_GXD (S03_GWSound.cpp / H03_GData.h) — Load()/Play(). CONFIRMED (§E).
// Le conteneur .WSOUND est parsé ailleurs (Asset/Sound : ts2::asset::WSound) ;
// on reçoit ici la liste de sons (PCM/chemins) et d'émetteurs.
// ---------------------------------------------------------------------------
class SoundBank {
public:
    // Émetteur positionnel = record 20 o de WSndBank (soundIndex + x,y,z + radius).
    // ex-VeryOldClient: SOUNDINFO_FOR_GXD (H03_GData.h) — { int mIndex; float mCenter[3]; float mRadius; }. CONFIRMED.
    struct BankEmitter {
        uint32_t soundIndex = 0;   // ex-VeryOldClient: mIndex
        Vec3     pos;              // ex-VeryOldClient: mCenter[3]
        float    radius = 0.0f;    // ex-VeryOldClient: mRadius
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
    std::vector<std::unique_ptr<SoundBuffer>> sounds_;  // WSndBank+1 (SoundObj[N]) — ex-VeryOldClient: WSOUND_FOR_GXD::mSound (SOUNDDATA_FOR_GXD[mSoundNum])
    std::vector<uint8_t> playing_;                      // WSndBank+3 (drapeaux « en lecture ») — ex-VeryOldClient: WSOUND_FOR_GXD::mCheckPlaySound
    std::vector<BankEmitter> emitters_;                 // WSndBank+5 (records 20 o) — ex-VeryOldClient: WSOUND_FOR_GXD::mSoundInfo (SOUNDINFO_FOR_GXD[mSoundInfoNum])
};

} // namespace ts2::audio

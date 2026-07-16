// Audio/AudioSystem.h — enveloppe DirectSound8 FIDELE du client TwelveSky2.
//
// === Sources IDA (idaTs2 — vérité unique) ===
//   Gfx_ZeroInitRenderer   0x69B980  init : DirectSoundCreate8(NULL,&g_pDirectSound8,NULL)
//                                     -> écrit renderer+1448 (dispo) et renderer+1452 (IDS8*)
//                                     // ex-VeryOldClient: GXD::InitForSound (CONFIRMED)
//   Snd_LoadOggToBuffers   0x6A8120  décode l'Ogg puis CreateSoundBuffer + Lock/memcpy/Unlock
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG (CONFIRMED)
//   Snd_Play3D             0x6A85C0  SetVolume(mB)+SetPan+Play  (démarrage)
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play (CONFIRMED)
//   Snd_Play2D             0x6A8880  SetVolume(mB)+SetPan       (mise à jour d'un buffer qui joue)
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::ChangeVolumeAndPan (CONFIRMED)
//   Snd_Stop               0x6A87B0  Stop()
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Stop (PLAUSIBLE — VeryOld rembobine
//                                     //   via SetCurrentPosition(0), absent de la vtable relevée en IDA)
//   Snd_ReleaseBuffers     0x6A80D0  Release() des 10 slots + free du PCM
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Free (CONFIRMED)
//   SoundObj_InitBuffers   0x6A8070  zéro-init de l'objet son (SoundObj, 60 o)
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Init (CONFIRMED)
//   WSndBank_LoadFile      0x4DA790  banque .WSOUND : N sons bouclés + M émetteurs
//                                     // ex-VeryOldClient: WSOUND_FOR_GXD::Load (CONFIRMED)
//   WSndBank_UpdatePositional 0x4DAC30  attênuation par distance -> Play2D/Play3D/Stop
//                                     // ex-VeryOldClient: WSOUND_FOR_GXD::Play (CONFIRMED)
//
// === Indicateur VeryOldClient (build différent/altéré — NOMS/idiomes seulement) ===
//   Buffer/décodage = Core/GXD/SOUNDDATA_FOR_GXD.cpp (classe SOUNDDATA_FOR_GXD, GXD.h:695).
//   Corrélation arbitrée IDA + statuts : Docs/TS2_AUDIO_ROSETTA.md. Aucune valeur/adresse
//   transposée depuis VeryOldClient ; les ancres 0x… ci-dessus sont l'adresse de la cible.
//
// === Découvertes importantes (relevées dans le binaire) ===
//   * L'objet global g_DirectSoundAvailable (0x8003C0) et g_pDirectSound8 (0x8003C4)
//     sont en réalité les champs +1448 / +1452 du singleton renderer g_GfxRenderer (0x7FFE18).
//   * Le binaire crée le device avec DirectSoundCreate8(NULL,..) et NE pose PAS de
//     SetCooperativeLevel explicite dans le chemin d'init observé. Pour un wrapper
//     CORRECT (CreateSoundBuffer fiable) on pose DSSCL_PRIORITY par défaut — paramétrable.
//   * Les buffers ne sont PAS des IDirectSound3DBuffer8 : DSBUFFERDESC.dwFlags = 0xC2 =
//     DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_STATIC. La spatialisation « 3D » est faite
//     en logiciel (distance -> volume/pan), cf. Snd3D_PlayPositional 0x4DA450.
//   * Format WAVEFORMATEX imposé en dur : PCM stéréo 16-bit 44100 Hz (blockAlign 4).
//   * Volume : millibels = tronc(2000*log10(v)) borné à [-10000,0]  (v = pourcent/100).
//     Le binaire calcule -2000*log10(1/v) via FYL2X — strictement équivalent.
//
// Le conteneur .WSOUND/.ISN/.BGM et le décodage Ogg sont gérés ailleurs (Asset/Sound).
// Ce module reçoit le PCM déjà décodé, soit directement, soit via un callback de chargement.
//
// Module autonome : ne dépend d'aucun autre .cpp du projet (leaf). Namespace ts2::audio.
#pragma once

#ifndef DIRECTSOUND_VERSION
#define DIRECTSOUND_VERSION 0x0800
#endif

#include <windows.h>
#include <mmreg.h>
#include <dsound.h>

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace ts2::audio {

// --- Format PCM imposé par Snd_LoadOggToBuffers 0x6A8120 (stéréo 16-bit 44100). ---
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG assemble une WAVEFORMATEX identique
//   (wFormatTag=1, nChannels=2, nSamplesPerSec=44100, nAvgBytesPerSec=176400, nBlockAlign=4,
//   wBitsPerSample=16). CONFIRMED — valeurs bit-exactes prouvées dans l'IDB (v32..v35).
struct PcmFormat {
    uint16_t channels      = 2;      // v32 = 0x00020001 -> nChannels = 2
    uint32_t samplesPerSec = 44100;  // v33 = 44100  (exigé : le chargeur rejette tout autre)
    uint16_t bitsPerSample = 16;     // v35 = 0x00100004 -> wBitsPerSample = 16, nBlockAlign = 4
    WAVEFORMATEX ToWaveFormat() const;
};

// --- Mode de lecture = champ « kind » du SoundObj (SoundObj+12 / idx 3). ---
//   1 = one-shot (Play flags 0)           -> Snd_Play3D kind==1
//   2 = boucle   (Play flags DSBPLAY_LOOPING) -> Snd_Play3D kind==2 (ambiance/BGM)
//   3 = pool     (N buffers dupliqués, joue le premier libre) -> Snd_Play3D kind==3
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::mLoadSort — `switch(tLoadSort)` 1/2/3 dans LoadFromOGG
//   et Play/Stop/ChangeVolumeAndPan (mêmes 3 modes). CONFIRMED.
enum class PlayMode : int { OneShot = 1, Loop = 2, Pool = 3 };

// Nb max de buffers par objet son : Snd_ReleaseBuffers 0x6A80D0 boucle sur 10 slots (idx 5..14).
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::mSoundData[10] (IDirectSoundBuffer*[10], cap fixe 10). CONFIRMED.
inline constexpr int kMaxBuffers = 10;

// DSBUFFERDESC.dwFlags relevé dans Snd_LoadOggToBuffers (v40 = 194 = 0xC2).
// ex-VeryOldClient: LoadFromOGG dsbd.dwFlags = DSBCAPS_STATIC|DSBCAPS_CTRLPAN|DSBCAPS_CTRLVOLUME
//   (0xC2, guid3DAlgorithm = GUID_NULL). CONFIRMED — pas d'IDirectSound3DBuffer, spatialisation logicielle.
inline constexpr DWORD kBufferFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_STATIC;

// Callback de chargement fourni par la couche Asset/Sound : décode `path`
// (.ISN/.BGM/.OGG) et remplit `outPcm` avec du PCM entrelacé 16-bit stéréo 44100.
// Renvoie true en cas de succès. Miroir du décodage interne de Snd_LoadOggToBuffers.
using PcmLoadCallback = std::function<bool(const std::string& path, std::vector<uint8_t>& outPcm)>;

// Conversion volume linéaire -> millibels DirectSound (formule FYL2X du binaire).
//   v <= 0   -> DSBVOLUME_MIN (-10000)
//   v >= 1   -> 0 (DSBVOLUME_MAX)
//   sinon    -> tronc(2000*log10(v))
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play / ChangeVolumeAndPan — courbe volume
//   `v>0 ? (v<1 ? log10(1/v)*-2000 : 0) : -10000`. CONFIRMED (strictement équivalent : log10(1/v)*-2000 == 2000*log10(v)).
LONG LinearToMillibel(double linear);

// Idem à partir d'un pourcentage 0..100 (comme l'argument de Snd_Play3D/Snd_Play2D,
// qui fait `vol * 0.01` en interne).
inline LONG PercentToMillibel(int percent) { return LinearToMillibel(percent * 0.01); }

// ---------------------------------------------------------------------------
// SoundBuffer — miroir du « SoundObj » (60 o) manipulé par Snd_LoadOggToBuffers /
// Snd_Play3D / Snd_Play2D / Snd_Stop / Snd_ReleaseBuffers.
//
//   Layout d'origine (DWORD*, idx) — ex-VeryOldClient: classe SOUNDDATA_FOR_GXD (GXD.h:695),
//   correspondance champ-à-champ CONFIRMED :
//     +0  taille fichier brut (inutile ensuite)        // mFileDataSize
//     +1  ptr tas des octets bruts (libéré au Release) // mFileData
//     +2  drapeau « chargé »                           // mCheckValidState
//     +3  kind (PlayMode)                              // mLoadSort
//     +4  nb de buffers actifs                         // mDuplicateNum
//     +5..+14  IDirectSoundBuffer*[10]                 // mSoundData[10]
// ---------------------------------------------------------------------------
class SoundBuffer {
public:
    SoundBuffer() = default;
    ~SoundBuffer();
    SoundBuffer(const SoundBuffer&) = delete;
    SoundBuffer& operator=(const SoundBuffer&) = delete;

    // Crée les buffers DirectSound à partir de PCM déjà décodé (Snd_LoadOggToBuffers, queue).
    //   mode==Pool -> `poolCount` buffers identiques (borné à kMaxBuffers), sinon 1.
    // Renvoie false si device indisponible, PCM vide, ou CreateSoundBuffer/Lock échoue.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG (partie upload buffers). CONFIRMED.
    bool LoadPcm(const uint8_t* pcm, size_t bytes, PlayMode mode, int poolCount = 1,
                 const PcmFormat& fmt = PcmFormat{});
    bool LoadPcm(const std::vector<uint8_t>& pcm, PlayMode mode, int poolCount = 1,
                 const PcmFormat& fmt = PcmFormat{}) {
        return LoadPcm(pcm.data(), pcm.size(), mode, poolCount, fmt);
    }

    // Charge via le callback PCM enregistré dans AudioSystem (résout un chemin).
    bool LoadFromPath(const std::string& path, PlayMode mode, int poolCount = 1);

    // Démarre la lecture (Snd_Play3D 0x6A85C0). volumePercent 0..100, pan brut (SetPan = 100*pan).
    //   OneShot/Loop : (re)joue le buffer unique. Pool : joue le premier buffer non-actif.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play (dispatch `switch(mLoadSort)` 1/2/3). CONFIRMED.
    bool Play(int volumePercent, int pan = 0);

    // Met à jour volume/pan des buffers DEJA en lecture, sans (re)démarrer (Snd_Play2D 0x6A8880).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::ChangeVolumeAndPan (garde « only-if-playing »). CONFIRMED.
    bool UpdatePlaying(int volumePercent, int pan = 0);

    // Arrête toute lecture (Snd_Stop 0x6A87B0).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Stop (PLAUSIBLE — VeryOld ajoute SetCurrentPosition(0), absent IDA).
    void Stop();

    // Libère les buffers et le PCM (Snd_ReleaseBuffers 0x6A80D0).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Free (release des 10 slots + mem::release(mFileData)). CONFIRMED.
    void Release();

    bool     Loaded() const { return loaded_; }
    PlayMode Mode()   const { return mode_; }
    int      Count()  const { return count_; }
    // Vrai si au moins un buffer est en cours de lecture (DSBSTATUS_PLAYING).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::IsPlaying (PLAUSIBLE — pas de primitive IDA isolée ;
    //   VeryOld se limite à mSoundData[0] sur sort 1/2, ClientSource généralise à tous les buffers).
    bool IsPlaying() const;

private:
    bool loaded_ = false;                    // SoundObj+2      — ex-VeryOldClient: mCheckValidState
    PlayMode mode_ = PlayMode::OneShot;      // SoundObj+3      — ex-VeryOldClient: mLoadSort
    int count_ = 0;                          // SoundObj+4      — ex-VeryOldClient: mDuplicateNum
    IDirectSoundBuffer* buffers_[kMaxBuffers] = {}; // SoundObj+5..+14 — ex-VeryOldClient: mSoundData[10]
    std::vector<uint8_t> pcm_;               // équivalent du tas SoundObj+1 — ex-VeryOldClient: mFileData
    PcmFormat fmt_{};
};

// ---------------------------------------------------------------------------
// BgmChannel — « slot BGM » de scène : possède UN SoundBuffer et encapsule le
// cycle prouvé release -> load -> (si option) play, tel que le binaire le fait
// pour la musique de fond de zone/menu. C'est le pendant du sous-objet SoundObj
// que cSceneMgr embarque à +612 (this+153 en DWORD).
//
// === Cible IDA (idaTs2 — vérité) ===
//   Init/reinit + release du slot BGM de cSceneMgr :
//     cSceneMgr_ReinitBgm          0x517A80  SoundObj_InitBuffers(+612)+SndMgr_InitBgmSlot(+612) (zéro-init)
//     SndMgr_InitBgmSlot           0x6A80A0  met à 0 les 14 premiers DWORD du SoundObj (+612)
//     SceneMgr_ReleaseSoundBuffers 0x517B60  Snd_ReleaseBuffers(+612) au destructeur cSceneMgr / App_Shutdown 0x462480
//   Le zéro-init est assuré ici par le ctor par défaut de SoundBuffer (loaded_=0,
//   buffers_[]=nullptr) — équivalent observable de SndMgr_InitBgmSlot 0x6A80A0.
//
//   Chargement + lecture prouvés (mêmes primitives Snd_* aux DEUX sites BGM) :
//     Scene_ServerSelectUpdate 0x518B30 (slot cSceneMgr +612, BGM de menu "Z000.BGM") :
//       0x518bde  Snd_ReleaseBuffers(slot)
//       0x518bf7  Snd_LoadOggToBuffers(slot, "G03_GDATA\\D10_WORLDBGM\\Z000.BGM", kind=3, voices=1, a5=1)
//       0x518c03  if (g_BgmEnabled==1)
//       0x518c14    Snd_Play3D(slot, ..., vol=100, pan=0, a8=0)
//     World_LoadZoneResource 0x4DCB60 case 12 (slot MONDE g_GameWorld+2236, BGM de zone "Z%03d.BGM") :
//       0x4dd41d  chemin "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM" (Z = World_ZoneIdToFileId(zoneId) 0x4db0f0)
//       0x4dd43e  Snd_LoadOggToBuffers(slot, path, kind=1, voices=1, a5=1)
//     Player_ResetCombatState 0x50F6xx (joue le slot MONDE à l'entrée en jeu) :
//       0x50f761  if (g_BgmEnabled==1)
//       0x50f76e    Snd_Play3D(g_GameWorld+2236, ..., vol=100, pan=0, a8=v4)
//
// === Boucle DSBPLAY_LOOPING : arbitrage (IDA lu DIRECTEMENT, Snd_Play3D 0x6A85C0) ===
//   Le drapeau de boucle dépend du kind ET de l'arg a8 :
//     - kind==1 (OneShot) : Play(...,flags) avec flags=(a8!=0)?1:0   (branche a8 @0x6a8785)
//     - kind==2 (Loop)    : Play(...,1) — DSBPLAY_LOOPING TOUJOURS    (@0x6a8742)
//     - kind==3 (Pool)    : Play(...,flags) avec flags=(a8!=0)?1:0    (branche a8 @0x6a86c2)
//   Les DEUX sites BGM chargent kind=1 (zone) / kind=3 (menu), PAS kind=2. Le a8 du play
//   de zone (Player_ResetCombatState, a8=v4) est un local NON initialisé -> indéterminable
//   statiquement ; le play de menu passe a8=0. La continuité du BGM de zone est en outre
//   assurée par une RELANCE toutes les 900 s dans Player_UpdateLocalAnim 0x5321EC (hook
//   AnimationTick::PlayAmbientBgm — HORS de ce module).
//   -> CHOIX documenté : ce slot autonome charge en PlayMode::Loop (kind=2) pour garantir
//      un BGM CONTINU (exigence « load+loop » + convention déjà en place dans
//      World/WorldMap case 12 -> WorldIntegration::LoadWorldBgm + Rosetta §F TODO #1).
//      Équivalent OBSERVABLE (ambiance continue), PAS une repro bit-exacte du kind.
//   TODO(fidélité) : si on modélise un jour le slot MONDE (g_GameWorld+2236) séparément,
//      réconcilier kind exact (1/3) + relance 900 s de Player_UpdateLocalAnim.
// ---------------------------------------------------------------------------
class BgmChannel {
public:
    // Cycle release -> load -> (si bgmEnabled) play. `path` doit être résoluble par le
    // PcmLoadCallback de l'AudioSystem (OggVorbisLoadCallback). volumePercent : le binaire
    // passe 100 en dur aux DEUX sites de play (0x518c14 / 0x50f76e).
    // Renvoie true si le .BGM a été DÉCODÉ+chargé (que bgmEnabled soit vrai ou non) ;
    // false si device audio indispo / .BGM absent / décodeur absent -> silencieux, AUCUN
    // crash (garde exigée « Guard si fichier BGM absent »).
    bool LoadAndPlay(const std::string& path, bool bgmEnabled, int volumePercent = 100);

    // Snd_Stop 0x6A87B0 — arrête la lecture sans libérer les buffers.
    void Stop();

    // Snd_ReleaseBuffers 0x6A80D0 (SceneMgr_ReleaseSoundBuffers 0x517B60) — libère tout.
    void Release();

    bool Loaded()    const { return buf_.Loaded(); }
    bool IsPlaying() const { return buf_.IsPlaying(); }

private:
    // SoundObj (60 o) du slot BGM — pendant de cSceneMgr +612. ctor SoundBuffer = zéro-init
    // (équivalent SndMgr_InitBgmSlot 0x6A80A0).
    SoundBuffer buf_;
};

// ---------------------------------------------------------------------------
// AudioSystem — possède le device IDirectSound8 (g_pDirectSound8), le volume
// SFX maître (g_SfxMasterVolume 0x84DEEC, 0..100), le callback PCM et le thread
// de préchargement asynchrone (SndLoader1_Run 0x4E6D60).
// Singleton d'accès (le binaire n'a qu'un device global).
// ---------------------------------------------------------------------------
class AudioSystem {
public:
    static AudioSystem& Instance();

    // DirectSoundCreate8 + SetCooperativeLevel(coopLevel). hwnd requis pour le coop level.
    // Sur échec, Available() reste false (comportement de Gfx_ZeroInitRenderer 0x69B980 :
    // le drapeau dispo est remis à 0 si le create échoue) — le jeu tourne alors muet.
    // ex-VeryOldClient: GXD::InitForSound (`mCheckValidStateForSound=TRUE; DirectSoundCreate8(NULL,&mDirectSound,NULL)`).
    //   CONFIRMED pour le create ; le SetCooperativeLevel(DSSCL_PRIORITY) est un CONFLICT (cf. Rosetta §J1,
    //   VeryOld le pose dans GXD::PrimaryBuffer, absent du chemin d'init IDA) — ajout délibéré, non transposé.
    bool Init(HWND hwnd, DWORD coopLevel = DSSCL_PRIORITY);
    void Shutdown();

    bool           Available() const { return available_; }      // g_DirectSoundAvailable
    IDirectSound8* Device()    const { return device_; }         // g_pDirectSound8

    // Volume SFX maître 0..100 (g_SfxMasterVolume). Consommé par les émetteurs Snd3D.
    void SetMasterVolume(int percent) { masterVolume_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent); }
    int  MasterVolume() const { return masterVolume_; }

    // Callback de décodage PCM (branché par Asset/Sound). Utilisé par LoadFromPath / EnsureLoaded.
    void SetLoadCallback(PcmLoadCallback cb) { loadCb_ = std::move(cb); }
    bool LoadPcm(const std::string& path, std::vector<uint8_t>& out) const;
    bool HasLoadCallback() const { return static_cast<bool>(loadCb_); }

    // --- Thread de préchargement (miroir de SndLoader1 : file + worker + Sleep(1)). ---
    // Chaque tâche renvoie true si le chargement a réussi ; sinon elle est ré-enfilée
    // (comportement de SndLoader1_Run 0x4E6D60 qui ré-insère le nœud en cas d'échec).
    // ex-VeryOldClient: CSoundLoaderThread (H099_LoaderThread) — GSOUND::Load(0) enfile via
    //   CSoundLoaderThread__PushJob (chargement asynchrone). PLAUSIBLE (indice de nom/idiome, hors Rosetta).
    using LoadTask = std::function<bool()>;
    void StartLoaderThread();
    void StopLoaderThread();
    void EnqueueLoad(LoadTask task);
    size_t PendingLoads() const;

private:
    AudioSystem() = default;
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    void LoaderLoop();

    IDirectSound8* device_ = nullptr;   // renderer+1452 — ex-VeryOldClient: GXD::mDirectSound
    bool  available_ = false;           // renderer+1448 — ex-VeryOldClient: GXD::mCheckValidStateForSound
    int   masterVolume_ = 100;          // 0x84DEEC      — ex-VeryOldClient: mGAMEOPTION->mSoundOption[1]

    PcmLoadCallback loadCb_;

    std::thread loader_;
    std::atomic<bool> loaderRunning_{false};   // SndLoader+104 — ex-VeryOldClient: CSoundLoaderThread (état worker)
    mutable std::mutex queueMutex_;            // SndLoader+80 (crit-section)
    std::deque<LoadTask> queue_;
};

// Accès court.
inline AudioSystem& Audio() { return AudioSystem::Instance(); }

} // namespace ts2::audio

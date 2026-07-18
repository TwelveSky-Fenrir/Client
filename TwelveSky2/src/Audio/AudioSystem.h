// Audio/AudioSystem.h — FAITHFUL DirectSound8 wrapper for the TwelveSky2 client.
//
// === IDA sources (idaTs2 — sole source of truth) ===
//   Gfx_ZeroInitRenderer   0x69B980  init: DirectSoundCreate8(NULL,&g_pDirectSound8,NULL)
//                                     -> writes renderer+1448 (available) and renderer+1452 (IDS8*)
//                                     // ex-VeryOldClient: GXD::InitForSound (CONFIRMED)
//   Snd_LoadOggToBuffers   0x6A8120  decodes the Ogg then CreateSoundBuffer + Lock/memcpy/Unlock
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG (CONFIRMED)
//   Snd_Play3D             0x6A85C0  SetVolume(mB)+SetPan+Play  (start)
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play (CONFIRMED)
//   Snd_Play2D             0x6A8880  SetVolume(mB)+SetPan       (update of a buffer that is playing)
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::ChangeVolumeAndPan (CONFIRMED)
//   Snd_Stop               0x6A87B0  Stop()
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Stop (PLAUSIBLE — VeryOld rewinds
//                                     //   via SetCurrentPosition(0), absent from the vtable found in IDA)
//   Snd_ReleaseBuffers     0x6A80D0  Release() of the 10 slots + free the PCM
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Free (CONFIRMED)
//   SoundObj_InitBuffers   0x6A8070  zero-init of the sound object (SoundObj, 60 bytes)
//                                     // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Init (CONFIRMED)
//   WSndBank_LoadFile      0x4DA790  .WSOUND bank: N looped sounds + M emitters
//                                     // ex-VeryOldClient: WSOUND_FOR_GXD::Load (CONFIRMED)
//   WSndBank_UpdatePositional 0x4DAC30  distance attenuation -> Play2D/Play3D/Stop
//                                     // ex-VeryOldClient: WSOUND_FOR_GXD::Play (CONFIRMED)
//
// === VeryOldClient indicator (different/altered build — NAMES/idioms only) ===
//   Buffer/decoding = Core/GXD/SOUNDDATA_FOR_GXD.cpp (class SOUNDDATA_FOR_GXD, GXD.h:695).
//   IDA-arbitrated correlation + statuses: Docs/TS2_AUDIO_ROSETTA.md. No value/address
//   transposed from VeryOldClient; the 0x… anchors above are the address of the target.
//
// === Key findings (identified in the binary) ===
//   * The global object g_DirectSoundAvailable (0x8003C0) and g_pDirectSound8 (0x8003C4)
//     are actually fields +1448 / +1452 of the renderer singleton g_GfxRenderer (0x7FFE18).
//   * The binary creates the device with DirectSoundCreate8(NULL,..) and does NOT set
//     an explicit SetCooperativeLevel in the observed init path. For a wrapper that is
//     CORRECT (reliable CreateSoundBuffer) we set DSSCL_PRIORITY by default — configurable.
//   * The buffers are NOT IDirectSound3DBuffer8: DSBUFFERDESC.dwFlags = 0xC2 =
//     DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_STATIC. "3D" spatialization is done
//     in software (distance -> volume/pan), see Snd3D_PlayPositional 0x4DA450.
//   * WAVEFORMATEX format hardcoded: 16-bit stereo PCM 44100 Hz (blockAlign 4).
//   * Volume: millibels = trunc(2000*log10(v)) clamped to [-10000,0]  (v = percent/100).
//     The binary computes -2000*log10(1/v) via FYL2X — strictly equivalent.
//
// The .WSOUND/.ISN/.BGM container and Ogg decoding are handled elsewhere (Asset/Sound).
// This module receives already-decoded PCM, either directly or via a load callback.
//
// Dependencies: the HEADER stays self-contained; AudioSystem.cpp includes Config/GameOptions.h to
//   read g_Options.SoundVolume (= g_SfxMasterVolume 0x84DEEC, the SAME memory word in the
//   binary — see MasterVolume()). Config/GameOptions.h only includes <cstddef>/<cstdint>:
//   no cycle. Namespace ts2::audio.
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

// --- PCM format enforced by Snd_LoadOggToBuffers 0x6A8120 (16-bit stereo 44100). ---
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG builds an identical WAVEFORMATEX
//   (wFormatTag=1, nChannels=2, nSamplesPerSec=44100, nAvgBytesPerSec=176400, nBlockAlign=4,
//   wBitsPerSample=16). CONFIRMED — bit-exact values proven in the IDB (v32..v35).
struct PcmFormat {
    uint16_t channels      = 2;      // v32 = 0x00020001 -> nChannels = 2
    uint32_t samplesPerSec = 44100;  // v33 = 44100  (required: the loader rejects anything else)
    uint16_t bitsPerSample = 16;     // v35 = 0x00100004 -> wBitsPerSample = 16, nBlockAlign = 4
    WAVEFORMATEX ToWaveFormat() const;
};

// --- Playback mode = SoundObj's "kind" field (SoundObj+12 / idx 3). ---
//   1 = one-shot (Play flags 0)               -> Snd_Play3D kind==1
//   2 = loop     (Play flags DSBPLAY_LOOPING)  -> Snd_Play3D kind==2 (ambiance/BGM)
//   3 = pool     (N duplicated buffers, plays the first free one) -> Snd_Play3D kind==3
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::mLoadSort — `switch(tLoadSort)` 1/2/3 in LoadFromOGG
//   and Play/Stop/ChangeVolumeAndPan (same 3 modes). CONFIRMED.
enum class PlayMode : int { OneShot = 1, Loop = 2, Pool = 3 };

// Max buffers per sound object: Snd_ReleaseBuffers 0x6A80D0 loops over 10 slots (idx 5..14).
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::mSoundData[10] (IDirectSoundBuffer*[10], fixed cap 10). CONFIRMED.
inline constexpr int kMaxBuffers = 10;

// DSBUFFERDESC.dwFlags found in Snd_LoadOggToBuffers (v40 = 194 = 0xC2).
// ex-VeryOldClient: LoadFromOGG dsbd.dwFlags = DSBCAPS_STATIC|DSBCAPS_CTRLPAN|DSBCAPS_CTRLVOLUME
//   (0xC2, guid3DAlgorithm = GUID_NULL). CONFIRMED — no IDirectSound3DBuffer, software spatialization.
inline constexpr DWORD kBufferFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_STATIC;

// Load callback provided by the Asset/Sound layer: decodes `path`
// (.ISN/.BGM/.OGG) and fills `outPcm` with interleaved 16-bit stereo 44100 PCM.
// Returns true on success. Mirrors the internal decode of Snd_LoadOggToBuffers.
using PcmLoadCallback = std::function<bool(const std::string& path, std::vector<uint8_t>& outPcm)>;

// Linear volume -> DirectSound millibel conversion (binary's FYL2X formula).
//   v <= 0   -> DSBVOLUME_MIN (-10000)
//   v >= 1   -> 0 (DSBVOLUME_MAX)
//   else     -> trunc(2000*log10(v))
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play / ChangeVolumeAndPan — volume curve
//   `v>0 ? (v<1 ? log10(1/v)*-2000 : 0) : -10000`. CONFIRMED (strictly equivalent: log10(1/v)*-2000 == 2000*log10(v)).
LONG LinearToMillibel(double linear);

// Same, from a 0..100 percentage (like the argument of Snd_Play3D/Snd_Play2D,
// which does `vol * 0.01` internally).
inline LONG PercentToMillibel(int percent) { return LinearToMillibel(percent * 0.01); }

// SoundBuffer — mirror of the "SoundObj" (60 bytes) manipulated by Snd_LoadOggToBuffers /
// Snd_Play3D / Snd_Play2D / Snd_Stop / Snd_ReleaseBuffers.
//
//   Original layout (DWORD*, idx) — ex-VeryOldClient: class SOUNDDATA_FOR_GXD (GXD.h:695),
//   field-to-field correspondence CONFIRMED:
//     +0  raw file size (unused afterward)              // mFileDataSize
//     +1  heap ptr to raw bytes (freed on Release)       // mFileData
//     +2  "loaded" flag                                  // mCheckValidState
//     +3  kind (PlayMode)                                 // mLoadSort
//     +4  active buffer count                             // mDuplicateNum
//     +5..+14  IDirectSoundBuffer*[10]                    // mSoundData[10]
class SoundBuffer {
public:
    SoundBuffer() = default;
    ~SoundBuffer();
    SoundBuffer(const SoundBuffer&) = delete;
    SoundBuffer& operator=(const SoundBuffer&) = delete;

    // Creates the DirectSound buffers from already-decoded PCM (Snd_LoadOggToBuffers, queued).
    //   mode==Pool -> `poolCount` identical buffers (capped at kMaxBuffers), else 1.
    // Returns false if device unavailable, PCM empty, or CreateSoundBuffer/Lock fails.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG (buffer-upload part). CONFIRMED.
    bool LoadPcm(const uint8_t* pcm, size_t bytes, PlayMode mode, int poolCount = 1,
                 const PcmFormat& fmt = PcmFormat{});
    bool LoadPcm(const std::vector<uint8_t>& pcm, PlayMode mode, int poolCount = 1,
                 const PcmFormat& fmt = PcmFormat{}) {
        return LoadPcm(pcm.data(), pcm.size(), mode, poolCount, fmt);
    }

    // Loads via the PCM callback registered in AudioSystem (resolves a path).
    bool LoadFromPath(const std::string& path, PlayMode mode, int poolCount = 1);

    // Starts playback (Snd_Play3D 0x6A85C0). volumePercent 0..100, raw pan (SetPan = 100*pan).
    //   OneShot/Loop: (re)plays the single buffer. Pool: plays the first inactive buffer.
    //   `loop` = arg_0 of Snd_Play3D: nonzero -> DSBPLAY_LOOPING on OneShot/Pool modes
    //   (branch @0x6A877C..0x6A8799). Loop mode ALWAYS loops, regardless of `loop`
    //   (literal `push 1` @0x6A873B/0x6A86E7): `loop` has no effect there.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play (dispatch `switch(mLoadSort)` 1/2/3). CONFIRMED.
    bool Play(int volumePercent, int pan = 0, int loop = 0);

    // Updates volume/pan of buffers ALREADY playing, without (re)starting (Snd_Play2D 0x6A8880).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::ChangeVolumeAndPan ("only-if-playing" guard). CONFIRMED.
    bool UpdatePlaying(int volumePercent, int pan = 0);

    // Stops all playback (Snd_Stop 0x6A87B0).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Stop (PLAUSIBLE — VeryOld adds SetCurrentPosition(0), absent from IDA).
    void Stop();

    // Frees the buffers and the PCM (Snd_ReleaseBuffers 0x6A80D0).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Free (release of the 10 slots + mem::release(mFileData)). CONFIRMED.
    void Release();

    bool     Loaded() const { return loaded_; }
    PlayMode Mode()   const { return mode_; }
    int      Count()  const { return count_; }
    // True if at least one buffer is currently playing (DSBSTATUS_PLAYING).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::IsPlaying (PLAUSIBLE — no isolated IDA primitive;
    //   VeryOld limits itself to mSoundData[0] on sort 1/2, ClientSource generalizes to all buffers).
    bool IsPlaying() const;

private:
    bool loaded_ = false;                    // SoundObj+2      — ex-VeryOldClient: mCheckValidState
    PlayMode mode_ = PlayMode::OneShot;      // SoundObj+3      — ex-VeryOldClient: mLoadSort
    int count_ = 0;                          // SoundObj+4      — ex-VeryOldClient: mDuplicateNum
    IDirectSoundBuffer* buffers_[kMaxBuffers] = {}; // SoundObj+5..+14 — ex-VeryOldClient: mSoundData[10]
    std::vector<uint8_t> pcm_;               // equivalent of the SoundObj+1 heap — ex-VeryOldClient: mFileData
    PcmFormat fmt_{};
};

// BgmChannel — scene "BGM slot": owns ONE SoundBuffer and encapsulates the
// proven release -> load -> (if enabled) play cycle, as the binary does
// for zone/menu background music. This is the counterpart of the SoundObj
// sub-object that cSceneMgr embeds at +612 (this+153 as DWORD).
//
// === IDA target (idaTs2 — truth) ===
//   Init/reinit + release of cSceneMgr's BGM slot:
//     cSceneMgr_ReinitBgm          0x517A80  SoundObj_InitBuffers(+612)+SndMgr_InitBgmSlot(+612) (zero-init)
//     SndMgr_InitBgmSlot           0x6A80A0  zeroes the first 14 DWORDs of the SoundObj (+612)
//     SceneMgr_ReleaseSoundBuffers 0x517B60  Snd_ReleaseBuffers(+612) in cSceneMgr's destructor / App_Shutdown 0x462480
//   The zero-init is guaranteed here by SoundBuffer's default ctor (loaded_=0,
//   buffers_[]=nullptr) — observable equivalent of SndMgr_InitBgmSlot 0x6A80A0.
//
//   Load + play proven (same Snd_* primitives at BOTH BGM sites):
//     Scene_ServerSelectUpdate 0x518B30 (cSceneMgr slot +612, menu BGM "Z000.BGM"):
//       0x518bde  Snd_ReleaseBuffers(slot)
//       0x518bf7  Snd_LoadOggToBuffers(slot, "G03_GDATA\\D10_WORLDBGM\\Z000.BGM", kind=3, voices=1, a5=1)
//       0x518c03  if (g_BgmEnabled==1)
//       0x518c14    Snd_Play3D(slot, ..., vol=100, pan=0)
//     World_LoadZoneResource 0x4DCB60 case 12 (WORLD slot g_GameWorld+2236, zone BGM "Z%03d.BGM"):
//       0x4dd41d  path "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM" (Z = World_ZoneIdToFileId(zoneId) 0x4db0f0)
//       0x4dd43e  Snd_LoadOggToBuffers(slot, path, kind=1, voices=1, a5=1)
//     Player_ResetCombatState 0x50F6xx (plays the WORLD slot on entering the game):
//       0x50f761  if (g_BgmEnabled==1)
//       0x50f76e    Snd_Play3D(g_GameWorld+2236, ..., vol=100, pan=0, a8=v4)
//
// === DSBPLAY_LOOPING loop: arbitration (IDA read DIRECTLY, Snd_Play3D 0x6A85C0) ===
//   CORRECTION (wave W9): there is NO 4th argument "a8". Snd_Play3D does `retn 0Ch`
//   on all SIX exits (@0x6A86E2, 0x6A86F5, 0x6A8714, 0x6A8748, 0x6A8796, 0x6A87A8) = 3
//   stack arguments, final answer: Snd_Play3D(SoundObj*@ecx, int loop, int volPercent, int pan).
//   The pseudo-code's "a8" is an ARTIFACT: Hex-Rays' esp model is broken by two
//   mistyped `call dword ptr [ecx+3Ch/40h]` (announced delta 4 instead of 0x10 @0x6A86B8 and
//   0xC @0x6A877C); after recomputation, `[esp+4+arg_8]` @0x6A877C actually designates arg_0 = loop.
//   The loop flag thus depends on BOTH kind AND `loop` (arg_0):
//     - kind==1 (OneShot): Play(...,flags) with flags=(loop!=0)?1:0  (branch @0x6A8785)
//     - kind==2 (Loop)   : Play(...,1) — DSBPLAY_LOOPING ALWAYS      (`push 1` @0x6A873B)
//     - kind==3 (Pool)   : Play(...,flags) with flags=(loop!=0)?1:0  (branch @0x6A86C2)
//   BOTH BGM sites load kind=1 (zone) / kind=3 (menu), NOT kind=2. The zone play's `loop`
//   (Player_ResetCombatState) is an UNINITIALIZED local -> statically undeterminable;
//   the menu play passes loop=0. Zone BGM continuity is also
//   guaranteed by a RESTART every 900 s in Player_UpdateLocalAnim 0x5321EC (hook
//   AnimationTick::PlayAmbientBgm — OUTSIDE this module).
//   -> Documented CHOICE: this standalone slot loads with PlayMode::Loop (kind=2) to guarantee
//      CONTINUOUS BGM (the "load+loop" requirement + the convention already used in
//      World/WorldMap case 12 -> WorldIntegration::LoadWorldBgm + Rosetta §F TODO #1).
//      OBSERVABLE equivalent (continuous ambiance), NOT a bit-exact repro of kind.
//   TODO(fidelity): if the WORLD slot (g_GameWorld+2236) is ever modeled separately,
//      reconcile the exact kind (1/3) + the 900 s restart from Player_UpdateLocalAnim.
class BgmChannel {
public:
    // Cycle release -> load -> (if bgmEnabled) play. `path` must be resolvable by the
    // AudioSystem's PcmLoadCallback (OggVorbisLoadCallback). volumePercent: the binary
    // hardcodes 100 at BOTH play sites (0x518c14 / 0x50f76e).
    // Returns true if the .BGM was DECODED+loaded (whether bgmEnabled is true or not);
    // false if audio device unavailable / .BGM missing / decoder absent -> silent, NO
    // crash (required guard "Guard if BGM file missing").
    bool LoadAndPlay(const std::string& path, bool bgmEnabled, int volumePercent = 100);

    // Snd_Stop 0x6A87B0 — stops playback without freeing the buffers.
    void Stop();

    // Snd_ReleaseBuffers 0x6A80D0 (SceneMgr_ReleaseSoundBuffers 0x517B60) — frees everything.
    void Release();

    bool Loaded()    const { return buf_.Loaded(); }
    bool IsPlaying() const { return buf_.IsPlaying(); }

private:
    // SoundObj (60 bytes) of the BGM slot — counterpart of cSceneMgr +612. SoundBuffer ctor = zero-init
    // (equivalent of SndMgr_InitBgmSlot 0x6A80A0).
    SoundBuffer buf_;
};

// AudioSystem — owns the IDirectSound8 device (g_pDirectSound8), the master
// SFX volume (g_SfxMasterVolume 0x84DEEC, 0..100), the PCM callback, and the
// asynchronous preload thread (SndLoader1_Run 0x4E6D60).
// Access singleton (the binary has only one global device).
class AudioSystem {
public:
    static AudioSystem& Instance();

    // DirectSoundCreate8 + SetCooperativeLevel(coopLevel). hwnd required for the coop level.
    // On failure, Available() stays false (behavior of Gfx_ZeroInitRenderer 0x69B980:
    // the available flag is reset to 0 if create fails) — the game then runs muted.
    // ex-VeryOldClient: GXD::InitForSound (`mCheckValidStateForSound=TRUE; DirectSoundCreate8(NULL,&mDirectSound,NULL)`).
    //   CONFIRMED for the create; SetCooperativeLevel(DSSCL_PRIORITY) is a CONFLICT (see Rosetta §J1,
    //   VeryOld sets it in GXD::PrimaryBuffer, absent from the IDA init path) — deliberate addition, not ported.
    bool Init(HWND hwnd, DWORD coopLevel = DSSCL_PRIORITY);
    void Shutdown();

    bool           Available() const { return available_; }      // g_DirectSoundAvailable
    IDirectSound8* Device()    const { return device_; }         // g_pDirectSound8

    // Master SFX volume 0..100 (g_SfxMasterVolume 0x84DEEC). Consumed by the Snd3D emitters.
    //
    // NO COPY: 0x84DEEC IS the idx11 option field itself (offset 0x2C of g_Options
    // 0x84DEC0, see Config/GameOptions.h). The 3 play functions RE-READ it on every call
    // (@0x4DA3BC `imul eax, ds:g_SfxMasterVolume`, @0x4DA3FB `cmp`, @0x4DA432 `mov`,
    //  @0x4DA45D `cmp`, @0x4DA524 `fimul`) -> in the binary, moving the slider takes effect
    // INSTANTLY on all following sounds. A member cached at boot (the former
    // `masterVolume_`) made the options slider ineffective until relaunch.
    // Defined out-of-line in AudioSystem.cpp (access to config::g_Options without a header dependency).
    void SetMasterVolume(int percent);
    int  MasterVolume() const;

    // PCM decode callback (wired by Asset/Sound). Used by LoadFromPath / EnsureLoaded.
    void SetLoadCallback(PcmLoadCallback cb) { loadCb_ = std::move(cb); }
    bool LoadPcm(const std::string& path, std::vector<uint8_t>& out) const;
    bool HasLoadCallback() const { return static_cast<bool>(loadCb_); }

    // --- Preload thread (mirror of SndLoader1: queue + worker + Sleep(1)). ---
    // Each task returns true if the load succeeded; otherwise it is re-enqueued
    // (behavior of SndLoader1_Run 0x4E6D60, which re-inserts the node on failure).
    // ex-VeryOldClient: CSoundLoaderThread (H099_LoaderThread) — GSOUND::Load(0) enqueues via
    //   CSoundLoaderThread__PushJob (asynchronous load). PLAUSIBLE (name/idiom clue, outside Rosetta).
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
    // NO `masterVolume_`: the SFX volume lives in config::g_Options.SoundVolume, which IS
    // g_SfxMasterVolume 0x84DEEC (see MasterVolume()). — ex-VeryOldClient: mGAMEOPTION->mSoundOption[1]

    PcmLoadCallback loadCb_;

    std::thread loader_;
    std::atomic<bool> loaderRunning_{false};   // SndLoader+104 — ex-VeryOldClient: CSoundLoaderThread (worker state)
    mutable std::mutex queueMutex_;            // SndLoader+80 (crit-section)
    std::deque<LoadTask> queue_;
};

// Short accessor.
inline AudioSystem& Audio() { return AudioSystem::Instance(); }

} // namespace ts2::audio

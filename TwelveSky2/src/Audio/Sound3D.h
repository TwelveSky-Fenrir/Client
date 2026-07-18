// Audio/Sound3D.h — "Snd3D" positional sound emitter + WSndBank ambiance bank.
//
// === IDA sources (idaTs2 — sole source of truth) ===
//   Snd3D_Construct         0x4DA010  ctor: SoundObj + critical section
//                                     // ex-VeryOldClient: GSOUND::GSOUND (mDATA.Init + InitializeCriticalSection)
//   Snd3D_SetISNPath        0x4DA0C0  builds the .ISN path based on type (1..6)
//                                     // ex-VeryOldClient: GSOUND::Init (identical switch — CONFIRMED)
//   Snd3D_EnsureLoaded      0x4DA270  loads sync (locked) OR enqueues on the loader thread
//                                     // ex-VeryOldClient: GSOUND::Load (sync vs CSoundLoaderThread__PushJob)
//   Snd3D_PlayScaledVolume  0x4DA380  plays at (master * percent / 100)
//                                     // ex-VeryOldClient: GSOUND::Play (mDATA.Play(loop, tVolumeSize*mSoundOption[1]*0.01))
//   Snd3D_PlayFullVolume    0x4DA3F0  plays at full master volume
//                                     // ex-VeryOldClient: GSOUND::Play2 (gate mSoundOption[1]==0 ; Play(FALSE, mSoundOption[1], 0))
//   Snd3D_PlayPositional    0x4DA450  linear attenuation over 300 units
//                                     // ex-VeryOldClient: GSOUND::Play3 (falloff 300.0f, (300-len)/300*mSoundOption[1])
//   Snd3D_UnloadIfExpired    0x4DA340  unloads if inactive beyond the TTL
//                                     // ex-VeryOldClient: GSOUND::ProcessForMemory (if (present-mLastUsedTime)>len Free)
//   Snd3D_Unload            0x4DA230  Release under lock
//                                     // ex-VeryOldClient: GSOUND::Free (EnterCriticalSection + mDATA.Free)
//   WSndBank_UpdatePositional 0x4DAC30 bank: nearest emitter -> Play/Update/Stop
//                                     // ex-VeryOldClient: WSOUND_FOR_GXD::Play (S03_GWSound.cpp — CONFIRMED)
//   AssetMgr_InitAllSlots   0x4DEB50  seeds the 410 slots of the SFX bank (type 4, index i)
//   AssetMgr_UpdateUnloadExpired 0x4E2050  periodic GC: 6 Snd3D_UnloadIfExpired sweeps
//   App_FrameTick           0x4625D0  @0x4626AE 60 s guard -> @0x4626D7 GC call (TTL 300 s)
//
// === VeryOldClient indicator (different/altered build — NAMES/idioms only) ===
//   Emitter level = class GSOUND (S03_GSound.cpp); zone bank = WSOUND_FOR_GXD +
//   SOUNDINFO_FOR_GXD (S03_GWSound.cpp / H03_GData.h). Correlation CONFIRMED against IDA
//   (§E of Docs/TS2_AUDIO_ROSETTA.md). No value/address transposed from VeryOldClient.
//
// Original Snd3D layout (bytes) — ex-VeryOldClient: class GSOUND (S03_GSound.cpp):
//   +0    "loaded" flag                                // mCheckValidState
//   +4    .ISN source path (string buffer)              // mFileName
//   +104  embedded SoundObj (60 bytes)                   // mDATA (SOUNDDATA_FOR_GXD)
//   +164  last-play timestamp (float, game seconds)       // mLastUsedTime
//   +168  RTL_CRITICAL_SECTION                            // mLock
//
// Standalone module (leaf). Namespace ts2::audio.
#pragma once
#include "Audio/AudioSystem.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ts2::audio {

// Minimal 3D vector (the binary reads 3 consecutive floats: x,y,z).
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Attenuation range of Snd3D_PlayPositional 0x4DA450 (hardcoded 300.0 constant).
// ex-VeryOldClient: GSOUND::Play3 — `if (tLength > 300.0f) return;`. CONFIRMED (IDA constant).
inline constexpr float kPositionalFalloff = 300.0f;

// Builds an .ISN source path — EXACT mirror of Snd3D_SetISNPath 0x4DA0C0.
// ex-VeryOldClient: GSOUND::Init — `switch(tValue01)` byte-identical (same 6 sprintf templates,
//   same a+3*b+1 / c+1 / d+1 indices). CONFIRMED — strongest name/idiom indicator in this front.
//   type 1 : "G03_GDATA\D06_GSOUND\001\C%03d%03d%03d.ISN"  (a+3*b+1, c+1, d+1)
//   type 2 : "...\002\N%03d001%03d.ISN"                    (a+1, b+1)
//   type 3 : "...\003\M%03d001%03d.ISN"                    (a+1, b+1)
//   type 4 : "...\004\E%03d001001.ISN"                     (a+1)
//   type 5 : "...\005\H%03d%03d%03d.ISN"                   (a+3*b+1, c+1, d+1)
//   type 6 : "...\006\X%03d%03d%03d.ISN"                   (a+3*b+1, c+1, d+1)
//   other  : empty string
std::string BuildIsnPath(int type, int a = 0, int b = 0, int c = 0, int d = 0);

// Emitter — "Snd3D" object: a SoundBuffer in Pool mode + source path + TTL.
// Thread-safe (internal lock), since loadable from the loader thread.
class Emitter {
public:
    Emitter() = default;

    // Snd3D_SetISNPath: sets the source path (also usable with a direct path).
    void SetSource(int type, int a = 0, int b = 0, int c = 0, int d = 0);
    void SetSourcePath(std::string path) {
        std::lock_guard<std::mutex> lk(mutex_);
        path_ = std::move(path);
    }
    const std::string& SourcePath() const { return path_; }

    // Snd3D_EnsureLoaded 0x4DA270.
    //   sync=true  : loads immediately (under lock) and refreshes the timestamp.
    //   sync=false : enqueues the request on the loader thread (immediate return).
    // `nowSec` = current game time (g_GameTimeSec / g_FrameAccumSec) for the TTL.
    bool EnsureLoaded(bool sync, float nowSec);

    // Snd3D_PlayScaledVolume 0x4DA380: plays at (master * percent / 100). percent 0..100.
    // `loop` = the binary's arg_0, RELAYED as-is to Snd_Play3D (@0x4DA3D5 `mov ecx,[ebp+arg_0]`
    //   / @0x4DA3D8 `push ecx`). All identified sites pass 0. Do NOT confuse with the
    //   3rd "sync" argument (arg_8), which is DEAD: `mov [ebp+arg_8], 0` @0x4DA389 overwrites it
    //   BEFORE any use -> Snd3D_EnsureLoaded(this, 0) = always asynchronous.
    bool PlayScaledVolume(int loop, int percent, float nowSec);
    // Snd3D_PlayFullVolume 0x4DA3F0: plays at full master volume.
    //   loop not configurable: literal `push 0` @0x4DA438.
    bool PlayFullVolume(float nowSec);
    // Snd3D_PlayPositional 0x4DA450: volume = (300-dist)/300 * master, silent beyond 300.
    //   loop not configurable: literal `push 0` @0x4DA538.
    bool PlayPositional(const Vec3& listener, const Vec3& source, float nowSec);

    // Snd3D_UnloadIfExpired 0x4DA340: unloads if (nowSec - lastPlay) > ttl.
    void UnloadIfExpired(float nowSec, float ttl);
    // Snd3D_Unload 0x4DA230: Release under lock.
    void Unload();

    bool Loaded() const;
    float LastPlaySec() const { return lastPlaySec_; }

    // Number of duplicated pool buffers (Snd_LoadOggToBuffers called with count=2 for .ISN files).
    void SetPoolCount(int n) { poolCount_ = n < 1 ? 1 : n; }

private:
    bool LoadLocked(float nowSec);   // common body (lock already held)
    // Snd3D_EnsureLoaded(this, 0) 0x4DA270 @0x4DA27E -> SndLoader1_Enqueue 0x4E6FB0: enqueues the
    // request on the loader thread and RETURNS CONTROL. "Lock already held" variant (calling
    // EnsureLoaded(false,…) from a play would re-acquire mutex_ -> deadlock).
    void EnqueueAsyncLoadLocked(float nowSec);

    mutable std::mutex mutex_;                 // Snd3D+168 (RTL_CRITICAL_SECTION) — ex-VeryOldClient: GSOUND::mLock
    std::string path_;                          // Snd3D+4   — ex-VeryOldClient: GSOUND::mFileName
    SoundBuffer buffer_;                        // Snd3D+104 (SoundObj) — ex-VeryOldClient: GSOUND::mDATA (SOUNDDATA_FOR_GXD)
    float lastPlaySec_ = 0.0f;                  // Snd3D+164 — ex-VeryOldClient: GSOUND::mLastUsedTime
    // Enqueue dedup: SndLoader1_Enqueue 0x4E6FB0 @0x4E705A checks membership in the
    // "in-flight" queue (SndLoader1Q_IterNotEqual) and REFUSES to insert the same Snd3D twice.
    // Without this flag, a sound triggered every frame would stack one task per frame.
    bool  pendingLoad_ = false;
    int   poolCount_ = 2;                       // .ISN files loaded in a pool of 2 (Snd3D_EnsureLoaded 0x4DA270 — value proven in IDA)
                                                // ex-VeryOldClient: GSOUND::Load -> mDATA.LoadFromOGG(mFileName, 3, 4, 1): sort=3 CONFIRMED,
                                                //   but dup=4 = VeryOld build's value, DIVERGES from the 2 proven in IDA -> NOT ported (IDA wins).
};

// SoundBank — runtime mirror of WSndBank (.WSOUND zone ambiance).
// N looped sounds + M emitters; every frame we look for the nearest emitter
// to the listener per sound, and adjust volume/playback by distance.
//   WSndBank_UpdatePositional 0x4DAC30.
// ex-VeryOldClient: class WSOUND_FOR_GXD (S03_GWSound.cpp / H03_GData.h) — Load()/Play(). CONFIRMED (§E).
// The .WSOUND container is parsed elsewhere (Asset/Sound: ts2::asset::WSound);
// here we receive the list of sounds (PCM/paths) and emitters.
class SoundBank {
public:
    // Positional emitter = 20-byte WSndBank record (soundIndex + x,y,z + radius).
    // ex-VeryOldClient: SOUNDINFO_FOR_GXD (H03_GData.h) — { int mIndex; float mCenter[3]; float mRadius; }. CONFIRMED.
    struct BankEmitter {
        uint32_t soundIndex = 0;   // ex-VeryOldClient: mIndex
        Vec3     pos;              // ex-VeryOldClient: mCenter[3]
        float    radius = 0.0f;    // ex-VeryOldClient: mRadius
    };

    // Loads N looped sounds from their paths (via the PCM callback) + the emitters.
    // Each sound is a SoundBuffer in Loop mode (kind 2 like WSndBank_LoadFile).
    bool Load(const std::vector<std::string>& soundPaths,
              const std::vector<BankEmitter>& emitters);

    // WSndBank_UpdatePositional: `enable`=false cuts everything; else, for each sound,
    // finds the nearest emitter; if dist < radius plays/adjusts, else cuts.
    //   `enableScale` (the binary's a3) scales intensity (0..100), typically 100.
    void UpdatePositional(const Vec3& listener, bool enable, int enableScale = 100);

    void Clear();
    size_t SoundCount()   const { return sounds_.size(); }
    size_t EmitterCount() const { return emitters_.size(); }

private:
    std::vector<std::unique_ptr<SoundBuffer>> sounds_;  // WSndBank+1 (SoundObj[N]) — ex-VeryOldClient: WSOUND_FOR_GXD::mSound (SOUNDDATA_FOR_GXD[mSoundNum])
    std::vector<uint8_t> playing_;                      // WSndBank+3 ("playing" flags) — ex-VeryOldClient: WSOUND_FOR_GXD::mCheckPlaySound
    std::vector<BankEmitter> emitters_;                 // WSndBank+5 (20-byte records) — ex-VeryOldClient: WSOUND_FOR_GXD::mSoundInfo (SOUNDINFO_FOR_GXD[mSoundInfoNum])
};

// Emitter registry — addressed by the ABSOLUTE address of the original Snd3D
//
// In the binary, Snd3D objects are NOT allocated: they are STATIC arrays in .data,
// all relative to the asset manager `g_ModelMotionArray` 0x8E8B30 (single `this`, set
// by App_Init @0x46224B `mov ecx, offset g_ModelMotionArray`). The ~1700 call sites
// thus carry a HARDCODED address (`mov ecx, offset flt_1495ABC`).
//
// This registry reproduces that addressing: each emitter is identified by the absolute
// address of the original Snd3D, which lets a TODO site be wired up by literally copying the
// IDA symbol it cites — e.g. `audio::PlayScaledVolume(0x1495ABC, 0, 100, nowSec)`.
//
// The 6 Snd3D bands swept by AssetMgr_UpdateUnloadExpired 0x4E2050 (base = 0x8E8B30 + off):
//   @0x4E378A  off 0x86D5CC -> 0x11560FC  [3][2][8][116]   (192 bytes/slot)
//   @0x4E37F9  off 0xA775CC -> 0x13600FC  [66][3]
//   @0x4E3861  off 0xA80A4C -> 0x136957C  [291][21]
//   @0x4E38A6  off 0xB9F18C -> 0x1487CBC  [410]            <- master SFX bank
//   @0x4E3957  off 0xBB250C -> 0x149B03C  [3][2][4][12]
//   @0x4E3A14  off 0x9725CC -> 0x125B0FC  [3][2][8][116]
inline constexpr uint32_t kSnd3DStride = 192u;   // proven: `imul edx, 0C0h` @0x4E05E4

// Master SFX bank — the emitter for nearly all sites (Snd3D_PlayScaledVolume
// 0x4DA380 totals 1624 code xrefs). Base = 0x8E8B30 + 0xB9F18C.
// Count of 410 PROVEN twice:  `cmp [ebp+var_10], 19Ah` @0x4E05CC (AssetMgr_InitAllSlots)
//   and `i88 < 410` @0x4E386A (AssetMgr_UpdateUnloadExpired); contiguity verified:
//   0x1487CBC + 410*192 = 0x149B03C = exact base of the next band (byte_BB250C).
inline constexpr uint32_t kSfxBankBase  = 0x1487CBCu;
inline constexpr int      kSfxBankCount = 410;

// GC TTL and period — AssetMgr_UpdateUnloadExpired called by App_FrameTick 0x4625D0:
//   @0x4626AE guards `g_GameTimeSec - flt_81518C >= 60.0` (dbl_7EDA50 = 0x404E000000000000)
//   @0x4626D7 call with a3 = flt_7A6C9C = 0x43960000 = 300.0f  (TTL)
inline constexpr float kEmitterTtlSec      = 300.0f;
inline constexpr float kEmitterGcPeriodSec = 60.0f;

// Registry emitter for the absolute address `addr` (created lazily on 1st access).
// If `addr` lands on an exact SFX bank slot, its .ISN source is SEEDED
// automatically — AssetMgr_InitAllSlots 0x4DEB50 @0x4E05CC..0x4E05F4:
//   `for (i=0; i<0x19A; ++i) Snd3D_SetISNPath(base + 0xC0*i, /*type=*/4, i, 0, 0, 0)`
// i.e. BuildIsnPath(4, i) = "G03_GDATA\D06_GSOUND\004\E%03d001001.ISN" with (i+1).
// Outside the SFX bank (the 5 other bands above, seeded by OTHER AssetMgr_InitAllSlots
// loops with other types — NOT yet identified), the source stays EMPTY:
// plays are then silent no-ops until SetEmitterSource has been called.
// Safe fallback: no crash, never a guessed path.
//   TODO [anchor 0x4DEB50]: identify the types/indices of the 5 other bands to seed them too.
//
// WARNING — verified pitfall: `flt_1687330` is NOT an emitter. It is the PLAYER
// POSITIONS array (g_PlayerArray 0x1687234 + 0xFC, stride 0x38C = 908), passed to the two
// `Vec3*` parameters of Snd3D_PlayPositional, never to `ecx`. Witness site Pkt_CharStatDelta
// @0x465F1A..0x465F34: `push offset flt_1687330` (self) / `imul edx,38Ch` + `add edx, offset
// flt_1687330` (player idx) / `mov ecx, offset flt_14890FC` <- the REAL emitter, index 27 of the
// SFX bank. Do not turn a position into an emitter.
Emitter& EmitterAt(uint32_t addr);

// Emitter at index `index` within the band based at `tableAddr` (proven stride 192).
inline Emitter& EmitterInTable(uint32_t tableAddr, int index) {
    return EmitterAt(tableAddr + kSnd3DStride * static_cast<uint32_t>(index));
}

// Snd3D_SetISNPath 0x4DA0C0 on the registry entry — for emitters OUTSIDE the SFX bank,
// whose source is not seeded by AssetMgr_InitAllSlots.
void SetEmitterSource(uint32_t addr, int type, int a = 0, int b = 0, int c = 0, int d = 0);

// --- Free-standing entries: 1:1 mirror of the binary's 3 functions (`this` becomes `addr`). ---
// All 3 are NON-BLOCKING: if the .ISN is not yet loaded, they enqueue it on the
// loader thread and return false WITHOUT playing (binary design, see Sound3D.cpp).
bool PlayScaledVolume(uint32_t addr, int loop, int percent, float nowSec);   // 0x4DA380
bool PlayFullVolume(uint32_t addr, float nowSec);                            // 0x4DA3F0
bool PlayPositional(uint32_t addr, const Vec3& listener, const Vec3& source, // 0x4DA450
                    float nowSec);

// Sweeps the ENTIRE registry -> Snd3D_UnloadIfExpired 0x4DA340 (mirror of the 6 loops of
// AssetMgr_UpdateUnloadExpired 0x4E2050, @0x4E378A..0x4E3A14).
void UnloadExpiredAll(float nowSec, float ttl = kEmitterTtlSec);

// Ready-to-wire emitter GC: encapsulates the 60 s guard AND the 300 s TTL.
// Exact mirror of App_FrameTick 0x4625D0 @0x4626AE..0x4626D7 — call EVERY frame
// with the game time (g_GameTimeSec 0x815180); the internal guard does the rest.
void TickEmitterGc(float nowSec);

// Number of materialized emitters (diagnostic / tests).
size_t EmitterCount();

} // namespace ts2::audio

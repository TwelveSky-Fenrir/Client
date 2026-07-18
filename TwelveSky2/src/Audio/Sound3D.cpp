// Audio/Sound3D.cpp — Snd3D emitter + WSndBank bank + registry (see Sound3D.h).
#include "Audio/Sound3D.h"

#include <cmath>
#include <cstdio>
#include <map>

namespace ts2::audio {

// .ISN path — Snd3D_SetISNPath 0x4DA0C0

std::string BuildIsnPath(int type, int a, int b, int c, int d) {
    // ex-VeryOldClient: GSOUND::Init (S03_GSound.cpp) — `switch(tValue01)` byte-identical
    //   (6 sprintf templates "G03_GDATA\D06_GSOUND\…", same indices). CONFIRMED against Snd3D_SetISNPath 0x4DA0C0.
    char buf[128] = {};
    switch (type) {
        case 1:
            std::snprintf(buf, sizeof(buf),
                          "G03_GDATA\\D06_GSOUND\\001\\C%03d%03d%03d.ISN",
                          a + 3 * b + 1, c + 1, d + 1);
            break;
        case 2:
            std::snprintf(buf, sizeof(buf),
                          "G03_GDATA\\D06_GSOUND\\002\\N%03d001%03d.ISN",
                          a + 1, b + 1);
            break;
        case 3:
            std::snprintf(buf, sizeof(buf),
                          "G03_GDATA\\D06_GSOUND\\003\\M%03d001%03d.ISN",
                          a + 1, b + 1);
            break;
        case 4:
            std::snprintf(buf, sizeof(buf),
                          "G03_GDATA\\D06_GSOUND\\004\\E%03d001001.ISN",
                          a + 1);
            break;
        case 5:
            std::snprintf(buf, sizeof(buf),
                          "G03_GDATA\\D06_GSOUND\\005\\H%03d%03d%03d.ISN",
                          a + 3 * b + 1, c + 1, d + 1);
            break;
        case 6:
            std::snprintf(buf, sizeof(buf),
                          "G03_GDATA\\D06_GSOUND\\006\\X%03d%03d%03d.ISN",
                          a + 3 * b + 1, c + 1, d + 1);
            break;
        default:
            buf[0] = '\0';   // sub_75CAB0: empty string
            break;
    }
    return std::string(buf);
}

// Emitter (Snd3D)

void Emitter::SetSource(int type, int a, int b, int c, int d) {
    std::string p = BuildIsnPath(type, a, b, c, d);
    std::lock_guard<std::mutex> lk(mutex_);
    path_ = std::move(p);
}

bool Emitter::Loaded() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return buffer_.Loaded();
}

bool Emitter::LoadLocked(float nowSec) {
    // Snd3D_EnsureLoaded (sync branch): already loaded -> refreshes the timestamp; else loads.
    // ex-VeryOldClient: GSOUND::Load — `if (mCheckValidState) mLastUsedTime=... ; else if (mDATA.LoadFromOGG(...)) ...`.
    if (buffer_.Loaded()) {
        lastPlaySec_ = nowSec;
        return true;
    }
    if (path_.empty()) return false;
    // .ISN files are loaded in a pool of 2 buffers (Snd_LoadOggToBuffers(...,3,2,1)).
    // ex-VeryOldClient: GSOUND::Load calls mDATA.LoadFromOGG(mFileName, 3, 4, 1): sort=3 CONFIRMED;
    //   VeryOld's dup=4 DIVERGES from the 2 proven in IDA -> value not ported (IDA wins).
    if (!buffer_.LoadFromPath(path_, PlayMode::Pool, poolCount_)) return false;
    lastPlaySec_ = nowSec;
    return true;
}

void Emitter::EnqueueAsyncLoadLocked(float nowSec) {
    // Snd3D_EnsureLoaded(this, 0) 0x4DA270: `if (a2 != 1) return SndLoader1_Enqueue(&unk_14A91A0,
    //   this);` @0x4DA27E/@0x4DA328 — enqueues and returns control WITHOUT loading.
    // Dedup: SndLoader1_Enqueue 0x4E6FB0 @0x4E705A refuses to insert a Snd3D already in the queue.
    if (pendingLoad_) return;
    if (path_.empty()) return;
    if (!AudioSystem::Instance().HasLoadCallback()) return;
    pendingLoad_ = true;
    // `this` is stable: registry emitters are NEVER destroyed (see Registry()).
    AudioSystem::Instance().EnqueueLoad([this, nowSec]() -> bool {
        std::lock_guard<std::mutex> lk(mutex_);
        const bool ok = LoadLocked(nowSec);
        if (ok) pendingLoad_ = false;   // failure -> LoaderLoop re-enqueues the SAME task
        return ok;
    });
}

bool Emitter::EnsureLoaded(bool sync, float nowSec) {
    if (!AudioSystem::Instance().HasLoadCallback()) {
        return false;
    }
    if (!sync) {
        // Snd3D_EnsureLoaded(this,0) @0x4DA27E -> `return SndLoader1_Enqueue(&unk_14A91A0, this)`.
        // FAITHFUL return value of SndLoader1_Enqueue 0x4E6FB0: 1 ONLY if the object is
        // already loaded (`if (*a2 == 1) return 1;` @0x4E6FDB), 0 in all other cases
        // (@0x4E7076 / @0x4E7174) — including when the queue insertion succeeds.
        // ex-VeryOldClient: GSOUND::Load(0) -> CSoundLoaderThread__PushJob(this) (branch `!tLoadSort`).
        std::lock_guard<std::mutex> lk(mutex_);
        if (buffer_.Loaded()) return true;
        EnqueueAsyncLoadLocked(nowSec);
        return false;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    return LoadLocked(nowSec);
}

bool Emitter::PlayScaledVolume(int loop, int percent, float nowSec) {
    // Snd3D_PlayScaledVolume 0x4DA380: enqueues if not loaded, timestamps, plays at master*percent/100.
    // ex-VeryOldClient: GSOUND::Play — `mDATA.Play(tCheckLoop, (int)((float)(tVolumeSize*mGAMEOPTION->mSoundOption[1])*0.01f), 0)`. CONFIRMED.
    // NO `master==0` gate here (unlike FullVolume/Positional): consistent with 0x4DA380,
    //   which never tests g_SfxMasterVolume before playing.
    std::lock_guard<std::mutex> lk(mutex_);
    // @0x4DA390 `cmp dword ptr [eax], 0` -> if not loaded, Snd3D_EnsureLoaded(this, 0) @0x4DA39D.
    // That call enqueues (async) and returns SndLoader1_Enqueue(...), which is 0 as long as
    // *this == 0 (@0x4E6FDB only returns 1 if already loaded) -> `test eax,eax` @0x4DA3A2 then
    // `jmp loc_4DA3E4` @0x4DA3A6 = DRY EXIT. lastPlaySec_ is NOT written on this path:
    // @0x4DA3B1 `fstp [edx+0A4h]` is INSIDE the guarded block loc_4DA3A8.
    // => the 1st trigger of a not-yet-loaded sound is SILENT by design (non-blocking).
    if (!buffer_.Loaded()) {
        EnqueueAsyncLoadLocked(nowSec);
        return false;
    }
    lastPlaySec_ = nowSec;                                     // @0x4DA3AB/0x4DA3B1
    const int master = AudioSystem::Instance().MasterVolume(); // @0x4DA3BC imul ds:g_SfxMasterVolume — ex-VeryOldClient: mGAMEOPTION->mSoundOption[1]
    // v5 = ftol(master * percent * 0.01) — Snd_Play3D's "percent" argument (dbl_7EDB40 = 0.01).
    const int vol = static_cast<int>(static_cast<double>(master * percent) * 0.01);
    // @0x4DA3B7 `push 0` = LITERAL pan; @0x4DA3D5/0x4DA3D8 relay arg_0 (loop).
    return buffer_.Play(vol, 0, loop);
}

bool Emitter::PlayFullVolume(float nowSec) {
    // Snd3D_PlayFullVolume 0x4DA3F0: only plays if master != 0, at full master volume.
    // ex-VeryOldClient: GSOUND::Play2 — `if (mSoundOption[1]==0) return; ... mDATA.Play(FALSE, mSoundOption[1], 0)`. CONFIRMED.
    std::lock_guard<std::mutex> lk(mutex_);
    const int master = AudioSystem::Instance().MasterVolume();
    if (master == 0) return false;               // @0x4DA3FB cmp ds:g_SfxMasterVolume, 0
    // Same dry exit as 0x4DA380: @0x4DA41D `jnz`/@0x4DA41F `jmp loc_4DA445`.
    if (!buffer_.Loaded()) {
        EnqueueAsyncLoadLocked(nowSec);
        return false;
    }
    lastPlaySec_ = nowSec;                       // @0x4DA424/0x4DA42A (guarded block loc_4DA421)
    // @0x4DA430 `push 0` = pan; @0x4DA438 `push 0` = LITERAL loop (not configurable).
    return buffer_.Play(master, 0, 0);
}

bool Emitter::PlayPositional(const Vec3& listener, const Vec3& source, float nowSec) {
    // Snd3D_PlayPositional 0x4DA450: Euclidean distance; if <= 300, volume = (300-d)/300 * master.
    // ex-VeryOldClient: GSOUND::Play3 — `tLength=sqrtf(...); if (tLength>300.0f) return;
    //   tSoundVolumeSize=(int)(((300.0f-tLength)/300.0f)*mSoundOption[1]); mDATA.Play(FALSE, tSoundVolumeSize, 0)`. CONFIRMED.
    std::lock_guard<std::mutex> lk(mutex_);
    const int master = AudioSystem::Instance().MasterVolume();
    if (master == 0) return false;               // @0x4DA45D cmp ds:g_SfxMasterVolume, 0
    // Same dry exit as 0x4DA380: @0x4DA482 `jnz`/@0x4DA484 `jmp loc_4DA545`.
    if (!buffer_.Loaded()) {
        EnqueueAsyncLoadLocked(nowSec);
        return false;
    }
    // @0x4DA48C/0x4DA492: the timestamp precedes the distance test -> it is refreshed EVEN
    // when the source is out of range (the emitter stays "warm" with respect to the TTL).
    lastPlaySec_ = nowSec;

    const float dx = listener.x - source.x;
    const float dy = listener.y - source.y;
    const float dz = listener.z - source.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);   // @0x4DA498..0x4DA4F2 (Math_Sqrt_0)
    // @0x4DA506 `fcomp ds:dbl_7A7CB0` (= 300.0) + `test ah, 41h` / `jnz loc_4DA515`: we play
    // if dist < 300 (C0) or dist == 300 (C3) -> STRICTLY silent beyond 300.
    if (dist > kPositionalFalloff) return false;

    // v8 = ftol((300-d)/300 * master) — Snd_Play3D's "percent" argument (@0x4DA518..0x4DA52A).
    const int vol = static_cast<int>((kPositionalFalloff - dist) / kPositionalFalloff *
                                     static_cast<float>(master));
    // @0x4DA532 `push 0` = pan; @0x4DA538 `push 0` = LITERAL loop (not configurable).
    return buffer_.Play(vol, 0, 0);
}

void Emitter::UnloadIfExpired(float nowSec, float ttl) {
    // Snd3D_UnloadIfExpired 0x4DA340: if(loaded && ttl < now - lastPlay) Unload().
    // ex-VeryOldClient: GSOUND::ProcessForMemory — `if (!mCheckValidState) return; if ((tPresentTime-mLastUsedTime)>tValidTimeLength) Free()`. CONFIRMED.
    std::lock_guard<std::mutex> lk(mutex_);
    if (!buffer_.Loaded()) return;
    if (ttl < nowSec - lastPlaySec_) {
        buffer_.Release();
    }
}

void Emitter::Unload() {
    // Snd3D_Unload 0x4DA230 — ex-VeryOldClient: GSOUND::Free (EnterCriticalSection + mDATA.Free + LeaveCriticalSection). CONFIRMED.
    std::lock_guard<std::mutex> lk(mutex_);
    buffer_.Release();
}

// SoundBank (WSndBank)

bool SoundBank::Load(const std::vector<std::string>& soundPaths,
                     const std::vector<BankEmitter>& emitters) {
    Clear();
    // WSndBank_LoadFile: each sound is loaded looped (kind 2, 1 buffer).
    // ex-VeryOldClient: WSOUND_FOR_GXD::Load — reads mSoundNum, then for each sound
    //   `mSound[i-1].LoadFromOGG("%s_%04d.OGG", 2 /*sort=loop*/, 1, 1)`, then reads mSoundInfoNum + mSoundInfo[]. CONFIRMED (sort=2).
    sounds_.reserve(soundPaths.size());
    size_t loadedCount = 0;
    for (const std::string& p : soundPaths) {
        auto sb = std::make_unique<SoundBuffer>();
        if (sb->LoadFromPath(p, PlayMode::Loop, 1))
            ++loadedCount;
        sounds_.push_back(std::move(sb));
    }
    playing_.assign(sounds_.size(), 0);
    emitters_ = emitters;
    return loadedCount != 0;
}

void SoundBank::UpdatePositional(const Vec3& listener, bool enable, int enableScale) {
    // WSndBank_UpdatePositional 0x4DAC30.
    // ex-VeryOldClient: WSOUND_FOR_GXD::Play(tPostCoord, tVolumeRatio) — CONFIRMED (§E):
    //   tVolumeRatio==0 -> Stop on all active mCheckPlaySound; else, per sound, nearest
    //   emitter (mUTIL->ReturnLengthXYZ); if mRadius>dist -> ChangeVolumeAndPan (already playing) / Play (1st activation); else Stop.
    if (sounds_.empty() || emitters_.empty()) return;

    if (!enable) {
        // a3==0 branch: cuts all sounds currently playing.
        // ex-VeryOldClient: `if (!tVolumeRatio) { for k: if (mCheckPlaySound[k]) { mCheckPlaySound[k]=0; mSound[k].Stop(); } return; }`.
        for (size_t i = 0; i < sounds_.size(); ++i) {
            if (playing_[i]) {
                playing_[i] = 0;
                if (sounds_[i]) sounds_[i]->Stop();
            }
        }
        return;
    }

    // For each sound i: find the nearest emitter that references it.
    for (size_t i = 0; i < sounds_.size(); ++i) {
        int   nearest = -1;
        float bestDist = 0.0f;
        for (size_t j = 0; j < emitters_.size(); ++j) {
            if (emitters_[j].soundIndex != i) continue;
            const float dx = emitters_[j].pos.x - listener.x;
            const float dy = emitters_[j].pos.y - listener.y;
            const float dz = emitters_[j].pos.z - listener.z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);   // Math_Dist3D
            if (nearest == -1 || d < bestDist) {
                nearest = static_cast<int>(j);
                bestDist = d;
            }
        }
        if (nearest == -1) continue;

        const float radius = emitters_[nearest].radius;
        if (radius > bestDist) {
            // Proportional volume: (radius - dist)/radius * 100, rescaled by a3.
            // ex-VeryOldClient: `tVolume = int((tVolumeRatio * ((mRadius - v6)/mRadius * 100.0f)) * 0.01f)`. CONFIRMED (double scaling).
            const int v4 = enableScale *
                           static_cast<int>((radius - bestDist) / radius * 100.0f);
            const int vol = static_cast<int>(static_cast<double>(v4) * 0.01);
            SoundBuffer* sb = sounds_[i].get();
            if (!sb) continue;
            if (playing_[i]) {
                sb->UpdatePlaying(vol, 0);        // already playing -> Snd_Play2D (adjust) — ex-VeryOldClient: mSound[i].ChangeVolumeAndPan(tVolume, 0)
            } else {
                playing_[i] = 1;
                sb->Play(vol, 0);                 // start -> Snd_Play3D — ex-VeryOldClient: mSound[i].Play(1, tVolume, 0)
            }
        } else if (playing_[i]) {
            playing_[i] = 0;
            if (sounds_[i]) sounds_[i]->Stop();   // out of range -> cut
        }
    }
}

void SoundBank::Clear() {
    // ex-VeryOldClient: WSOUND_FOR_GXD::Free — `for i: mSound[i].Free(); mem::release(mSound/mSoundFileName/mCheckPlaySound/mSoundInfo)`. CONFIRMED.
    for (auto& s : sounds_) if (s) s->Release();
    sounds_.clear();
    playing_.clear();
    emitters_.clear();
}

// Emitter registry (see Sound3D.h for anchors and reasoning)

namespace {

struct EmitterRegistry {
    std::mutex mutex;
    std::map<uint32_t, std::unique_ptr<Emitter>> map;   // key = absolute address of the original Snd3D
};

EmitterRegistry& Registry() {
    // NEVER destroyed — deliberate. In the binary, Snd3D objects are GLOBAL .data
    // (byte_B9F18C -> 0x1487CBC, byte_86D5CC, byte_A775CC, …): they survive until the end of the
    // process. A destructible static would risk being destroyed BEFORE the loader
    // thread stops (SndLoader1_Run 0x4E6D60), which captures `this` on the emitters -> UAF at exit.
    // The leak is thus the FAITHFUL behavior (mirror of the original .data), not an oversight.
    static EmitterRegistry* reg = new EmitterRegistry();
    return *reg;
}

// AssetMgr_InitAllSlots 0x4DEB50, bank-4 loop @0x4E05CC..0x4E05F4:
//   for (i = 0; i < 0x19A /*410*/; ++i)
//       Snd3D_SetISNPath(this + 0xB9F18C + 0xC0*i, /*type=*/4, /*a=*/i, 0, 0, 0);
// with this = g_ModelMotionArray 0x8E8B30 (App_Init @0x46224B) -> base 0x1487CBC.
// Snd3D_SetISNPath 0x4DA0C0 case 4 -> "G03_GDATA\D06_GSOUND\004\E%03d001001.ISN" with (a+1).
void SeedBankSourceForAddr(Emitter& e, uint32_t addr) {
    const uint32_t span = static_cast<uint32_t>(kSfxBankCount) * kSnd3DStride;
    if (addr < kSfxBankBase || addr >= kSfxBankBase + span) return;   // outside the SFX bank
    const uint32_t off = addr - kSfxBankBase;
    if (off % kSnd3DStride != 0) return;                              // not a slot start
    e.SetSource(4, static_cast<int>(off / kSnd3DStride));
}

} // namespace

Emitter& EmitterAt(uint32_t addr) {
    EmitterRegistry& reg = Registry();
    std::lock_guard<std::mutex> lk(reg.mutex);
    std::unique_ptr<Emitter>& slot = reg.map[addr];
    if (!slot) {
        slot.reset(new Emitter());
        // Seeds the .ISN source for SFX bank slots (see above). Takes the
        // emitter's OWN lock (Emitter::SetSource), distinct from reg.mutex: no cycle.
        SeedBankSourceForAddr(*slot, addr);
    }
    return *slot;
}

void SetEmitterSource(uint32_t addr, int type, int a, int b, int c, int d) {
    // Snd3D_SetISNPath 0x4DA0C0.
    EmitterAt(addr).SetSource(type, a, b, c, d);
}

bool PlayScaledVolume(uint32_t addr, int loop, int percent, float nowSec) {
    return EmitterAt(addr).PlayScaledVolume(loop, percent, nowSec);   // 0x4DA380
}

bool PlayFullVolume(uint32_t addr, float nowSec) {
    return EmitterAt(addr).PlayFullVolume(nowSec);                    // 0x4DA3F0
}

bool PlayPositional(uint32_t addr, const Vec3& listener, const Vec3& source, float nowSec) {
    return EmitterAt(addr).PlayPositional(listener, source, nowSec);  // 0x4DA450
}

void UnloadExpiredAll(float nowSec, float ttl) {
    // Mirror of the 6 Snd3D_UnloadIfExpired 0x4DA340 loops of AssetMgr_UpdateUnloadExpired
    // 0x4E2050 (@0x4E378A, 0x4E37F9, 0x4E3861, 0x4E38A6, 0x4E3957, 0x4E3A14). The binary
    // sweeps complete STATIC bands; here we sweep MATERIALIZED emitters, which is
    // observably equivalent (a slot never played is never loaded, so there is nothing to
    // unload: Snd3D_UnloadIfExpired exits immediately on `if (*(_DWORD*)this)` @0x4DA34A).
    EmitterRegistry& reg = Registry();
    std::lock_guard<std::mutex> lk(reg.mutex);
    for (auto& kv : reg.map) {
        if (kv.second) kv.second->UnloadIfExpired(nowSec, ttl);
    }
}

void TickEmitterGc(float nowSec) {
    // App_FrameTick 0x4625D0:
    //   @0x4626AE  `g_GameTimeSec - flt_81518C` vs dbl_7EDA50 (60.0) — `test ah,5` / `jp`:
    //              only proceeds if the gap is >= 60 s (C0 clear).
    //   @0x4626B8  flt_81518C = g_GameTimeSec  (rearms the period)
    //   @0x4626D7  AssetMgr_UpdateUnloadExpired(g_ModelMotionArray, g_GameTimeSec,
    //              flt_7A6C9C = 300.0f)
    static float lastGcSec = 0.0f;   // flt_81518C 0x81518C (initialized to 0 like the .data)
    if (nowSec - lastGcSec < kEmitterGcPeriodSec) return;
    lastGcSec = nowSec;
    UnloadExpiredAll(nowSec, kEmitterTtlSec);
}

size_t EmitterCount() {
    EmitterRegistry& reg = Registry();
    std::lock_guard<std::mutex> lk(reg.mutex);
    return reg.map.size();
}

} // namespace ts2::audio

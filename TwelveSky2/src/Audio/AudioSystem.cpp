// Audio/AudioSystem.cpp — faithful DirectSound8 wrapper (see AudioSystem.h).
#include "Audio/AudioSystem.h"
#include "Audio/OggVorbisDecoder.h"   // OggVorbisLoadCallback (Ogg decode -> 16-bit stereo 44100 PCM)
#include "Config/GameOptions.h"       // g_Options.SoundVolume == g_SfxMasterVolume 0x84DEEC (idx11, offset 0x2C)

#include <cmath>
#include <cstring>

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")

namespace ts2::audio {

// PcmFormat / conversions

WAVEFORMATEX PcmFormat::ToWaveFormat() const {
    // Exact reconstruction of the WAVEFORMATEX built in Snd_LoadOggToBuffers 0x6A8120:
    //   wFormatTag = WAVE_FORMAT_PCM, nBlockAlign = channels*bits/8, nAvgBytes = rate*blockAlign.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG fills `pwf` with these same 6 fields
    //   (PCM/2ch/44100/176400/blockAlign 4/16b) before CreateSoundBuffer. CONFIRMED.
    WAVEFORMATEX wf = {};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = channels;
    wf.nSamplesPerSec  = samplesPerSec;
    wf.wBitsPerSample  = bitsPerSample;
    wf.nBlockAlign     = static_cast<WORD>(channels * bitsPerSample / 8);
    wf.nAvgBytesPerSec = samplesPerSec * wf.nBlockAlign;
    wf.cbSize          = 0;
    return wf;
}

LONG LinearToMillibel(double linear) {
    // Binary (Snd_Play3D/Snd_Play2D):
    //   v <= 0      -> -10000
    //   v >= 1      -> 0
    //   else        -> Dbl2Uint( FYL2X(1/v, log10(2)) * -2000 ) = trunc(2000*log10(v))
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play / ChangeVolumeAndPan — `v8 = tVolume*0.01f ;
    //   v8>0 ? (v8<1 ? LONG(log10(1.0f/v8)*-2000.0f) : 0) : -10000`. CONFIRMED (equivalent).
    if (linear <= 0.0) return DSBVOLUME_MIN;   // -10000
    if (linear >= 1.0) return 0;               // DSBVOLUME_MAX
    double mb = 2000.0 * std::log10(linear);   // negative in (0,1)
    LONG v = static_cast<LONG>(mb);            // truncation toward zero (Crt_ftol/Dbl2Uint)
    if (v < DSBVOLUME_MIN) v = DSBVOLUME_MIN;
    if (v > 0) v = 0;
    return v;
}

// SoundBuffer

SoundBuffer::~SoundBuffer() { Release(); }

bool SoundBuffer::LoadFromPath(const std::string& path, PlayMode mode, int poolCount) {
    std::vector<uint8_t> pcm;
    if (!AudioSystem::Instance().LoadPcm(path, pcm)) return false;
    return LoadPcm(pcm, mode, poolCount);
}

bool SoundBuffer::LoadPcm(const uint8_t* pcm, size_t bytes, PlayMode mode, int poolCount,
                          const PcmFormat& fmt) {
    // Snd_LoadOggToBuffers 0x6A8120 (queue): rejects if device missing or already loaded.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG — identical entry guard
    //   `if (!mGXD->mCheckValidStateForSound || mCheckValidState) return FALSE`. CONFIRMED.
    AudioSystem& sys = AudioSystem::Instance();
    if (!sys.Available() || !sys.Device()) return false;
    if (loaded_) return false;
    if (!pcm || bytes == 0) return false;

    Release();               // starts fresh from a clean state
    mode_ = mode;
    fmt_  = fmt;
    pcm_.assign(pcm, pcm + bytes);

    // Buffer count: Pool -> poolCount (capped at 10), else 1  (SoundObj+4).
    // ex-VeryOldClient: LoadFromOGG `switch(tLoadSort){1:mDuplicateNum=1; 2:mDuplicateNum=1; 3:mDuplicateNum=tDuplicateNum}`.
    int n = (mode == PlayMode::Pool) ? poolCount : 1;
    if (n < 1) n = 1;
    if (n > kMaxBuffers) n = kMaxBuffers;

    WAVEFORMATEX wf = fmt_.ToWaveFormat();

    DSBUFFERDESC desc = {};
    desc.dwSize          = sizeof(DSBUFFERDESC);   // v39 = 36
    desc.dwFlags         = kBufferFlags;           // v40 = 0xC2
    desc.dwBufferBytes   = static_cast<DWORD>(bytes); // v41 = PCM size
    desc.dwReserved      = 0;
    desc.lpwfxFormat     = &wf;                     // v43 -> WAVEFORMATEX
    // guid3DAlgorithm stays GUID_NULL (already zero-initialized): no hardware 3D.

    // Creates n identical buffers and copies the PCM into them (CreateSoundBuffer + Lock/memcpy/Unlock).
    // ex-VeryOldClient: LoadFromOGG loops `for i in [0,mDuplicateNum)`:
    //   CreateSoundBuffer -> Lock(0,0,...,2 /*ENTIREBUFFER*/) -> CopyMemory -> Unlock,
    //   any failure -> GORET -> Free(). CONFIRMED (same sequence, same DSBLOCK_ENTIREBUFFER=2 flag).
    for (int i = 0; i < n; ++i) {
        IDirectSoundBuffer* buf = nullptr;
        if (FAILED(sys.Device()->CreateSoundBuffer(&desc, &buf, nullptr)) || !buf) {
            Release();
            return false;
        }
        void* p1 = nullptr; DWORD b1 = 0;
        void* p2 = nullptr; DWORD b2 = 0;
        // Lock the entire buffer (offset 0, DSBLOCK_ENTIREBUFFER=2 -> the binary passes flag 2).
        if (FAILED(buf->Lock(0, 0, &p1, &b1, &p2, &b2, DSBLOCK_ENTIREBUFFER))) {
            buf->Release();
            Release();
            return false;
        }
        DWORD n1 = (b1 < bytes) ? b1 : static_cast<DWORD>(bytes);
        std::memcpy(p1, pcm, n1);
        if (p2 && b2 && bytes > n1) std::memcpy(p2, pcm + n1, bytes - n1);
        buf->Unlock(p1, b1, p2, b2);
        buffers_[i] = buf;
    }

    count_  = n;
    loaded_ = true;
    return true;
}

void SoundBuffer::Release() {
    // Snd_ReleaseBuffers 0x6A80D0: loaded=0 then Release() on the 10 slots.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Free — `mCheckValidState=0; mem::release(&mFileData);
    //   for i in [0,10) SAFE_RELEASE(mSoundData[i])`. CONFIRMED.
    loaded_ = false;
    count_  = 0;
    for (int i = 0; i < kMaxBuffers; ++i) {
        if (buffers_[i]) {
            buffers_[i]->Release();
            buffers_[i] = nullptr;
        }
    }
    pcm_.clear();
}

void SoundBuffer::Stop() {
    // Snd_Stop 0x6A87B0: GetStatus, if buffer lost -> Release, else Stop() each buffer.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Stop (`switch(mLoadSort)`: sort1/2 = mSoundData[0],
    //   sort3 = all voices; `status & 2 /*BUFFERLOST*/` -> Free()). PLAUSIBLE — VeryOld
    //   adds `SetCurrentPosition(0)` (rewind) after Stop(), absent from the vtable found in IDA (not ported).
    if (!loaded_) return;
    for (int i = 0; i < count_; ++i) {
        IDirectSoundBuffer* b = buffers_[i];
        if (!b) continue;
        DWORD status = 0;
        if (FAILED(b->GetStatus(&status)) || (status & DSBSTATUS_BUFFERLOST)) {
            Release();
            return;
        }
        b->Stop();
    }
}

bool SoundBuffer::IsPlaying() const {
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::IsPlaying — `status & 1 /*PLAYING*/` (PLAUSIBLE:
    //   VeryOld limited to mSoundData[0] on sort 1/2; here we iterate all buffers).
    for (int i = 0; i < count_; ++i) {
        if (!buffers_[i]) continue;
        DWORD status = 0;
        if (SUCCEEDED(buffers_[i]->GetStatus(&status)) && (status & DSBSTATUS_PLAYING))
            return true;
    }
    return false;
}

bool SoundBuffer::Play(int volumePercent, int pan, int loop) {
    // Snd_Play3D 0x6A85C0: SetVolume(mB) + SetPan(100*pan) + Play(flags).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play — `switch(mLoadSort)`: sort1/2 = mSoundData[0]
    //   (bit1 `status&2`=BUFFERLOST -> Free); sort3 = first free voice (`(status&1)==0`) after
    //   discarding a silent volume (`tVolumea != -10000`). CONFIRMED (identical dispatch).
    AudioSystem& sys = AudioSystem::Instance();
    if (!sys.Available() || !loaded_) return false;

    const LONG mb  = PercentToMillibel(volumePercent);
    const LONG dpan = static_cast<LONG>(100 * pan);
    // kind==2 (Loop): `push 1` LITERAL @0x6A873B -> always loops, `loop` ignored.
    // kind==1/3      : flags = (loop != 0) ? DSBPLAY_LOOPING : 0  (@0x6A877C..0x6A8799).
    const DWORD playFlags = (mode_ == PlayMode::Loop || loop != 0) ? DSBPLAY_LOOPING : 0u;

    if (mode_ == PlayMode::OneShot || mode_ == PlayMode::Loop) {
        IDirectSoundBuffer* b = buffers_[0];
        if (!b) return false;
        DWORD status = 0;
        if (FAILED(b->GetStatus(&status)) || (status & DSBSTATUS_BUFFERLOST)) {
            Release();
            return false;
        }
        b->SetVolume(mb);
        b->SetPan(dpan);
        return SUCCEEDED(b->Play(0, 0, playFlags));
    }

    // Pool (kind 3): plays the first buffer NOT currently playing (Snd_Play3D branches on kind==3).
    if (mode_ == PlayMode::Pool) {
        if (mb == DSBVOLUME_MIN) return false;   // the binary ignores a silent volume
        for (int i = 0; i < count_; ++i) {
            IDirectSoundBuffer* b = buffers_[i];
            if (!b) continue;
            DWORD status = 0;
            if (FAILED(b->GetStatus(&status)) || (status & DSBSTATUS_BUFFERLOST)) {
                Release();
                return false;
            }
            if (status & DSBSTATUS_PLAYING) continue;   // busy -> next
            b->SetVolume(mb);
            b->SetPan(dpan);
            // kind==3: flags = (loop != 0) ? DSBPLAY_LOOPING : 0 (@0x6A86C2), NOT hardcoded 0.
            return SUCCEEDED(b->Play(0, 0, playFlags));
        }
        return false; // all busy
    }
    return false;
}

bool SoundBuffer::UpdatePlaying(int volumePercent, int pan) {
    // Snd_Play2D 0x6A8880: updates volume/pan of buffers that are PLAYING, without (re)starting.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::ChangeVolumeAndPan — same mB/pan conversion, only acts
    //   `if status & 1` (PLAYING), never restarts (misleading IDA name "Play2D"). CONFIRMED.
    AudioSystem& sys = AudioSystem::Instance();
    if (!sys.Available() || !loaded_) return false;

    const LONG mb  = PercentToMillibel(volumePercent);
    const LONG dpan = static_cast<LONG>(100 * pan);

    if (mode_ == PlayMode::OneShot || mode_ == PlayMode::Loop) {
        IDirectSoundBuffer* b = buffers_[0];
        if (!b) return false;
        DWORD status = 0;
        if (FAILED(b->GetStatus(&status)) || (status & DSBSTATUS_BUFFERLOST)) {
            Release();
            return false;
        }
        if (status & DSBSTATUS_PLAYING) {
            b->SetVolume(mb);
            b->SetPan(dpan);
        }
        return true;
    }

    if (mode_ == PlayMode::Pool) {
        for (int i = 0; i < count_; ++i) {
            IDirectSoundBuffer* b = buffers_[i];
            if (!b) continue;
            DWORD status = 0;
            if (FAILED(b->GetStatus(&status)) || (status & DSBSTATUS_BUFFERLOST)) {
                Release();
                return false;
            }
            if (status & DSBSTATUS_PLAYING) {
                b->SetVolume(mb);
                b->SetPan(dpan);
            }
        }
        return true;
    }
    return false;
}

// BgmChannel — scene BGM slot (see AudioSystem.h for IDA anchors)

bool BgmChannel::LoadAndPlay(const std::string& path, bool bgmEnabled, int volumePercent) {
    // 0x518bde Snd_ReleaseBuffers(slot): starts from a CLEAN slot — release BEFORE reload,
    //   exactly like Scene_ServerSelectUpdate right before Snd_LoadOggToBuffers.
    buf_.Release();
    // 0x518bf7 / 0x4dd43e Snd_LoadOggToBuffers(slot, path, kind, voices=1, a5=1).
    //   PlayMode::Loop (kind=2) -> DSBPLAY_LOOPING forced (Snd_Play3D 0x6a8742): CONTINUOUS
    //   ambiance. See the header note (observable-equivalent choice, TODO kind fidelity).
    //   Failure (device missing / .BGM not found / Ogg decoder absent) -> false, silent.
    if (!buf_.LoadFromPath(path, PlayMode::Loop, 1))
        return false;
    // 0x518c03 / 0x50f761: play is GATED by the g_BgmEnabled==1 option (0x84DEF0).
    // 0x518c14 / 0x50f76e Snd_Play3D(slot, ..., vol=100, pan=0).
    if (bgmEnabled)
        buf_.Play(volumePercent, 0);
    return true;
}

void BgmChannel::Stop()    { buf_.Stop(); }      // Snd_Stop 0x6A87B0
void BgmChannel::Release() { buf_.Release(); }   // Snd_ReleaseBuffers 0x6A80D0

// AudioSystem

AudioSystem& AudioSystem::Instance() {
    static AudioSystem inst;
    return inst;
}

int AudioSystem::MasterVolume() const {
    // g_SfxMasterVolume 0x84DEEC = g_Options(0x84DEC0) + 0x2C = idx11 SoundVolume field.
    // DIRECT and FRESH read, like the binary's 3 play sites (@0x4DA3BC / 0x4DA3FB / 0x4DA432
    // / 0x4DA45D / 0x4DA524): the options slider takes effect on the very next sound.
    // No clamping on read — the binary consumes the raw word; the [0,100] range is
    // guaranteed upstream by UI_OptionsWnd_OnClick 0x66D140 / GameOptions::Normalize().
    return config::g_Options.SoundVolume;
}

void AudioSystem::SetMasterVolume(int percent) {
    // Writes the option field ITSELF (no local copy) — [0,100] clamping preserved, like
    // the per-field clamp in UI_OptionsWnd_OnClick 0x66D140.
    config::g_Options.SoundVolume = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

AudioSystem::~AudioSystem() { Shutdown(); }

bool AudioSystem::Init(HWND hwnd, DWORD coopLevel) {
    if (device_) return available_;

    // Wires up the default Ogg Vorbis decoder (mirrors the internal decode of
    // Snd_LoadOggToBuffers 0x6A8120) if no callback was set before Init.
    // The `!HasLoadCallback()` guard preserves a test callback installed beforehand.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG decodes inline (no callback); splitting
    //   it into PcmLoadCallback is a ClientSource choice. PLAUSIBLE — dsound buffer fed identically.
    if (!HasLoadCallback())
        SetLoadCallback(&OggVorbisLoadCallback);

    // Gfx_ZeroInitRenderer 0x69B980: DirectSoundCreate8(NULL, &g_pDirectSound8, NULL).
    HRESULT hr = DirectSoundCreate8(nullptr, &device_, nullptr);
    if (FAILED(hr) || !device_) {
        device_ = nullptr;
        available_ = false;   // renderer+1448 reset to 0 on failure
        return false;
    }

    // The observed init path does not set a coop level; we set it here to make
    // CreateSoundBuffer reliable (DSSCL_PRIORITY by default, like a typical fullscreen D3D game).
    if (hwnd) {
        hr = device_->SetCooperativeLevel(hwnd, coopLevel);
        if (FAILED(hr)) {
            // Non-fatal: falls back to NORMAL, which suffices for secondary buffers.
            device_->SetCooperativeLevel(hwnd, DSSCL_NORMAL);
        }
    }

    available_ = true;
    StartLoaderThread();
    return true;
}

void AudioSystem::Shutdown() {
    StopLoaderThread();
    available_ = false;
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

bool AudioSystem::LoadPcm(const std::string& path, std::vector<uint8_t>& out) const {
    if (!loadCb_) return false;
    out.clear();
    return loadCb_(path, out);
}

// --- Preload thread (SndLoader1_Run 0x4E6D60). ---

void AudioSystem::StartLoaderThread() {
    if (loaderRunning_.exchange(true)) return;     // already running
    loader_ = std::thread(&AudioSystem::LoaderLoop, this);
}

void AudioSystem::StopLoaderThread() {
    if (!loaderRunning_.exchange(false)) return;
    if (loader_.joinable()) loader_.join();
    std::lock_guard<std::mutex> lk(queueMutex_);
    queue_.clear();
}

void AudioSystem::EnqueueLoad(LoadTask task) {
    if (!task) return;
    std::lock_guard<std::mutex> lk(queueMutex_);
    queue_.push_back(std::move(task));
}

size_t AudioSystem::PendingLoads() const {
    std::lock_guard<std::mutex> lk(queueMutex_);
    return queue_.size();
}

void AudioSystem::LoaderLoop() {
    // Worker loop: pops a task, processes it outside the lock, re-enqueues on failure,
    // then Sleep(1) — exactly the structure of SndLoader1_Run 0x4E6D60.
    // ex-VeryOldClient: CSoundLoaderThread (H099_LoaderThread) — GSOUND::Load(0) pushes a job
    //   via CSoundLoaderThread__PushJob; the worker reloads as a background task. PLAUSIBLE (name/idiom).
    while (loaderRunning_.load()) {
        LoadTask task;
        {
            std::lock_guard<std::mutex> lk(queueMutex_);
            if (!queue_.empty()) {
                task = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (task) {
            bool ok = task();
            if (!ok) {                              // re-insert the node (load failure)
                std::lock_guard<std::mutex> lk(queueMutex_);
                queue_.push_back(std::move(task));
            }
        }
        Sleep(1);
    }
}

} // namespace ts2::audio

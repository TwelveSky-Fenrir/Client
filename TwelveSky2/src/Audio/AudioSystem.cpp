// Audio/AudioSystem.cpp — enveloppe DirectSound8 fidèle (voir AudioSystem.h).
#include "Audio/AudioSystem.h"
#include "Audio/OggVorbisDecoder.h"   // OggVorbisLoadCallback (décodage Ogg -> PCM 16-bit stéréo 44100)
#include "Config/GameOptions.h"       // g_Options.SoundVolume == g_SfxMasterVolume 0x84DEEC (idx11, off 0x2C)

#include <cmath>
#include <cstring>

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")

namespace ts2::audio {

// ===========================================================================
// PcmFormat / conversions
// ===========================================================================

WAVEFORMATEX PcmFormat::ToWaveFormat() const {
    // Reconstruit exactement la WAVEFORMATEX assemblée dans Snd_LoadOggToBuffers 0x6A8120 :
    //   wFormatTag = WAVE_FORMAT_PCM, nBlockAlign = channels*bits/8, nAvgBytes = rate*blockAlign.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG remplit `pwf` avec ces mêmes 6 champs
    //   (PCM/2ch/44100/176400/blockAlign 4/16b) avant CreateSoundBuffer. CONFIRMED.
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
    // Binaire (Snd_Play3D/Snd_Play2D) :
    //   v <= 0      -> -10000
    //   v >= 1      -> 0
    //   sinon       -> Dbl2Uint( FYL2X(1/v, log10(2)) * -2000 ) = tronc(2000*log10(v))
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play / ChangeVolumeAndPan — `v8 = tVolume*0.01f ;
    //   v8>0 ? (v8<1 ? LONG(log10(1.0f/v8)*-2000.0f) : 0) : -10000`. CONFIRMED (équivalent).
    if (linear <= 0.0) return DSBVOLUME_MIN;   // -10000
    if (linear >= 1.0) return 0;               // DSBVOLUME_MAX
    double mb = 2000.0 * std::log10(linear);   // négatif dans (0,1)
    LONG v = static_cast<LONG>(mb);            // troncature vers zéro (Crt_ftol/Dbl2Uint)
    if (v < DSBVOLUME_MIN) v = DSBVOLUME_MIN;
    if (v > 0) v = 0;
    return v;
}

// ===========================================================================
// SoundBuffer
// ===========================================================================

SoundBuffer::~SoundBuffer() { Release(); }

bool SoundBuffer::LoadFromPath(const std::string& path, PlayMode mode, int poolCount) {
    std::vector<uint8_t> pcm;
    if (!AudioSystem::Instance().LoadPcm(path, pcm)) return false;
    return LoadPcm(pcm, mode, poolCount);
}

bool SoundBuffer::LoadPcm(const uint8_t* pcm, size_t bytes, PlayMode mode, int poolCount,
                          const PcmFormat& fmt) {
    // Snd_LoadOggToBuffers 0x6A8120 (queue) : refuse si device absent ou déjà chargé.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG — garde d'entrée identique
    //   `if (!mGXD->mCheckValidStateForSound || mCheckValidState) return FALSE`. CONFIRMED.
    AudioSystem& sys = AudioSystem::Instance();
    if (!sys.Available() || !sys.Device()) return false;
    if (loaded_) return false;
    if (!pcm || bytes == 0) return false;

    Release();               // repart d'un état propre
    mode_ = mode;
    fmt_  = fmt;
    pcm_.assign(pcm, pcm + bytes);

    // Nb de buffers : Pool -> poolCount (borné à 10), sinon 1  (SoundObj+4).
    // ex-VeryOldClient: LoadFromOGG `switch(tLoadSort){1:mDuplicateNum=1; 2:mDuplicateNum=1; 3:mDuplicateNum=tDuplicateNum}`.
    int n = (mode == PlayMode::Pool) ? poolCount : 1;
    if (n < 1) n = 1;
    if (n > kMaxBuffers) n = kMaxBuffers;

    WAVEFORMATEX wf = fmt_.ToWaveFormat();

    DSBUFFERDESC desc = {};
    desc.dwSize          = sizeof(DSBUFFERDESC);   // v39 = 36
    desc.dwFlags         = kBufferFlags;           // v40 = 0xC2
    desc.dwBufferBytes   = static_cast<DWORD>(bytes); // v41 = taille PCM
    desc.dwReserved      = 0;
    desc.lpwfxFormat     = &wf;                     // v43 -> WAVEFORMATEX
    // guid3DAlgorithm reste GUID_NULL (déjà zéro-initialisé) : pas de 3D matériel.

    // Crée n buffers identiques et y copie le PCM (CreateSoundBuffer + Lock/memcpy/Unlock).
    // ex-VeryOldClient: LoadFromOGG boucle `for i in [0,mDuplicateNum)` :
    //   CreateSoundBuffer -> Lock(0,0,...,2 /*ENTIREBUFFER*/) -> CopyMemory -> Unlock,
    //   tout échec -> GORET -> Free(). CONFIRMED (même séquence, même flag DSBLOCK_ENTIREBUFFER=2).
    for (int i = 0; i < n; ++i) {
        IDirectSoundBuffer* buf = nullptr;
        if (FAILED(sys.Device()->CreateSoundBuffer(&desc, &buf, nullptr)) || !buf) {
            Release();
            return false;
        }
        void* p1 = nullptr; DWORD b1 = 0;
        void* p2 = nullptr; DWORD b2 = 0;
        // Lock du buffer entier (offset 0, DSBLOCK_ENTIREBUFFER=2 -> le binaire passe flag 2).
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
    // Snd_ReleaseBuffers 0x6A80D0 : loaded=0 puis Release() des 10 slots.
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
    // Snd_Stop 0x6A87B0 : GetStatus, si buffer perdu -> Release, sinon Stop() chaque buffer.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Stop (`switch(mLoadSort)` : sort1/2 = mSoundData[0],
    //   sort3 = toutes les voix ; `status & 2 /*BUFFERLOST*/` -> Free()). PLAUSIBLE — VeryOld
    //   ajoute `SetCurrentPosition(0)` (rewind) après Stop(), absent de la vtable relevée en IDA (non transposé).
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
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::IsPlaying — `status & 1 /*PLAYING*/` (PLAUSIBLE :
    //   VeryOld limité à mSoundData[0] sur sort 1/2 ; ici on itère tous les buffers).
    for (int i = 0; i < count_; ++i) {
        if (!buffers_[i]) continue;
        DWORD status = 0;
        if (SUCCEEDED(buffers_[i]->GetStatus(&status)) && (status & DSBSTATUS_PLAYING))
            return true;
    }
    return false;
}

bool SoundBuffer::Play(int volumePercent, int pan, int loop) {
    // Snd_Play3D 0x6A85C0 : SetVolume(mB) + SetPan(100*pan) + Play(flags).
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::Play — `switch(mLoadSort)` : sort1/2 = mSoundData[0]
    //   (bit1 `status&2`=BUFFERLOST -> Free) ; sort3 = 1re voix libre (`(status&1)==0`) après
    //   avoir écarté un volume silencieux (`tVolumea != -10000`). CONFIRMED (dispatch identique).
    AudioSystem& sys = AudioSystem::Instance();
    if (!sys.Available() || !loaded_) return false;

    const LONG mb  = PercentToMillibel(volumePercent);
    const LONG dpan = static_cast<LONG>(100 * pan);
    // kind==2 (Loop) : `push 1` LITTÉRAL @0x6A873B -> boucle toujours, `loop` ignoré.
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

    // Pool (kind 3) : joue le premier buffer NON en lecture (Snd_Play3D branche kind==3).
    if (mode_ == PlayMode::Pool) {
        if (mb == DSBVOLUME_MIN) return false;   // le binaire ignore un volume silencieux
        for (int i = 0; i < count_; ++i) {
            IDirectSoundBuffer* b = buffers_[i];
            if (!b) continue;
            DWORD status = 0;
            if (FAILED(b->GetStatus(&status)) || (status & DSBSTATUS_BUFFERLOST)) {
                Release();
                return false;
            }
            if (status & DSBSTATUS_PLAYING) continue;   // occupé -> suivant
            b->SetVolume(mb);
            b->SetPan(dpan);
            // kind==3 : flags = (loop != 0) ? DSBPLAY_LOOPING : 0 (@0x6A86C2), PAS 0 en dur.
            return SUCCEEDED(b->Play(0, 0, playFlags));
        }
        return false; // tous occupés
    }
    return false;
}

bool SoundBuffer::UpdatePlaying(int volumePercent, int pan) {
    // Snd_Play2D 0x6A8880 : met à jour volume/pan des buffers qui JOUENT, sans (re)démarrer.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::ChangeVolumeAndPan — même conversion mB/pan, n'agit
    //   que si `status & 1` (PLAYING), jamais de restart (nom IDA « Play2D » trompeur). CONFIRMED.
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

// ===========================================================================
// BgmChannel — slot BGM de scène (voir AudioSystem.h pour les ancres IDA)
// ===========================================================================

bool BgmChannel::LoadAndPlay(const std::string& path, bool bgmEnabled, int volumePercent) {
    // 0x518bde Snd_ReleaseBuffers(slot) : repart d'un slot PROPRE — release AVANT reload,
    //   exactement comme Scene_ServerSelectUpdate juste avant Snd_LoadOggToBuffers.
    buf_.Release();
    // 0x518bf7 / 0x4dd43e Snd_LoadOggToBuffers(slot, path, kind, voices=1, a5=1).
    //   PlayMode::Loop (kind=2) -> DSBPLAY_LOOPING forcé (Snd_Play3D 0x6a8742) : ambiance
    //   CONTINUE. Voir la note d'en-tête (choix observable-équivalent, TODO fidélité kind).
    //   Échec (device absent / .BGM introuvable / décodeur Ogg absent) -> false, silencieux.
    if (!buf_.LoadFromPath(path, PlayMode::Loop, 1))
        return false;
    // 0x518c03 / 0x50f761 : le play est GATÉ par l'option g_BgmEnabled==1 (0x84DEF0).
    // 0x518c14 / 0x50f76e Snd_Play3D(slot, ..., vol=100, pan=0).
    if (bgmEnabled)
        buf_.Play(volumePercent, 0);
    return true;
}

void BgmChannel::Stop()    { buf_.Stop(); }      // Snd_Stop 0x6A87B0
void BgmChannel::Release() { buf_.Release(); }   // Snd_ReleaseBuffers 0x6A80D0

// ===========================================================================
// AudioSystem
// ===========================================================================

AudioSystem& AudioSystem::Instance() {
    static AudioSystem inst;
    return inst;
}

int AudioSystem::MasterVolume() const {
    // g_SfxMasterVolume 0x84DEEC = g_Options(0x84DEC0) + 0x2C = champ idx11 SoundVolume.
    // Lecture DIRECTE et FRAÎCHE, comme les 3 play du binaire (@0x4DA3BC / 0x4DA3FB / 0x4DA432
    // / 0x4DA45D / 0x4DA524) : le slider d'options agit dès le son suivant.
    // Pas de bornage à la lecture — le binaire consomme le mot brut ; la plage [0,100] est
    // garantie en amont par UI_OptionsWnd_OnClick 0x66D140 / GameOptions::Normalize().
    return config::g_Options.SoundVolume;
}

void AudioSystem::SetMasterVolume(int percent) {
    // Écrit le champ d'option LUI-MÊME (aucune copie locale) — bornage [0,100] conservé, comme
    // le clamp par champ de UI_OptionsWnd_OnClick 0x66D140.
    config::g_Options.SoundVolume = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

AudioSystem::~AudioSystem() { Shutdown(); }

bool AudioSystem::Init(HWND hwnd, DWORD coopLevel) {
    if (device_) return available_;

    // Branche le décodeur Ogg Vorbis par défaut (miroir du décodage interne de
    // Snd_LoadOggToBuffers 0x6A8120) si aucun callback n'a été posé avant Init.
    // La garde `!HasLoadCallback()` préserve un callback de test installé en amont.
    // ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG décode inline (pas de callback) ; le
    //   découpage en PcmLoadCallback est un choix ClientSource. PLAUSIBLE — buffer dsound identiquement alimenté.
    if (!HasLoadCallback())
        SetLoadCallback(&OggVorbisLoadCallback);

    // Gfx_ZeroInitRenderer 0x69B980 : DirectSoundCreate8(NULL, &g_pDirectSound8, NULL).
    HRESULT hr = DirectSoundCreate8(nullptr, &device_, nullptr);
    if (FAILED(hr) || !device_) {
        device_ = nullptr;
        available_ = false;   // renderer+1448 remis à 0 en cas d'échec
        return false;
    }

    // Le chemin d'init observé ne pose pas de coop level ; on le pose ici pour fiabiliser
    // CreateSoundBuffer (DSSCL_PRIORITY par défaut, comme un jeu D3D plein écran typique).
    if (hwnd) {
        hr = device_->SetCooperativeLevel(hwnd, coopLevel);
        if (FAILED(hr)) {
            // Non fatal : on retombe sur NORMAL, qui suffit pour des buffers secondaires.
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

// --- Thread de préchargement (SndLoader1_Run 0x4E6D60). ---

void AudioSystem::StartLoaderThread() {
    if (loaderRunning_.exchange(true)) return;     // déjà lancé
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
    // Boucle worker : dépile une tâche, la traite hors verrou, ré-enfile en cas d'échec,
    // puis Sleep(1) — exactement la structure de SndLoader1_Run 0x4E6D60.
    // ex-VeryOldClient: CSoundLoaderThread (H099_LoaderThread) — GSOUND::Load(0) pousse un job
    //   via CSoundLoaderThread__PushJob ; le worker recharge en tâche de fond. PLAUSIBLE (nom/idiome).
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
            if (!ok) {                              // ré-insertion du nœud (échec de chargement)
                std::lock_guard<std::mutex> lk(queueMutex_);
                queue_.push_back(std::move(task));
            }
        }
        Sleep(1);
    }
}

} // namespace ts2::audio

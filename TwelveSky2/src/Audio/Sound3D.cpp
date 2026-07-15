// Audio/Sound3D.cpp — émetteur Snd3D + banque WSndBank (voir Sound3D.h).
#include "Audio/Sound3D.h"

#include <cmath>
#include <cstdio>

namespace ts2::audio {

// ===========================================================================
// Chemin .ISN — Snd3D_SetISNPath 0x4DA0C0
// ===========================================================================

std::string BuildIsnPath(int type, int a, int b, int c, int d) {
    // ex-VeryOldClient: GSOUND::Init (S03_GSound.cpp) — `switch(tValue01)` byte-identique
    //   (6 gabarits sprintf « G03_GDATA\D06_GSOUND\… », mêmes index). CONFIRMED contre Snd3D_SetISNPath 0x4DA0C0.
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
            buf[0] = '\0';   // sub_75CAB0 : chaîne vide
            break;
    }
    return std::string(buf);
}

// ===========================================================================
// Emitter (Snd3D)
// ===========================================================================

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
    // Snd3D_EnsureLoaded (branche sync) : déjà chargé -> rafraîchit l'horodatage ; sinon charge.
    // ex-VeryOldClient: GSOUND::Load — `if (mCheckValidState) mLastUsedTime=... ; else if (mDATA.LoadFromOGG(...)) ...`.
    if (buffer_.Loaded()) {
        lastPlaySec_ = nowSec;
        return true;
    }
    if (path_.empty()) return false;
    // Les .ISN sont chargés en pool de 2 buffers (Snd_LoadOggToBuffers(...,3,2,1)).
    // ex-VeryOldClient: GSOUND::Load appelle mDATA.LoadFromOGG(mFileName, 3, 4, 1) : sort=3 CONFIRMED ;
    //   le dup=4 de VeryOld DIVERGE du 2 prouvé en IDA -> valeur non transposée (IDA gagne).
    if (!buffer_.LoadFromPath(path_, PlayMode::Pool, poolCount_)) return false;
    lastPlaySec_ = nowSec;
    return true;
}

bool Emitter::EnsureLoaded(bool sync, float nowSec) {
    if (!AudioSystem::Instance().HasLoadCallback()) {
        return false;
    }
    if (!sync) {
        // Snd3D_EnsureLoaded(this,0) -> SndLoader1_Enqueue : chargement asynchrone.
        // On capture `this` ; la tâche renvoie true si le chargement a abouti (sinon ré-enfilée).
        // ex-VeryOldClient: GSOUND::Load(0) -> CSoundLoaderThread__PushJob(this) (branche `!tLoadSort`).
        //   ClientSource capture une lambda (mutex + LoadLocked). Différence de mécanique de file (nom/idiome).
        AudioSystem::Instance().EnqueueLoad([this, nowSec]() -> bool {
            std::lock_guard<std::mutex> lk(mutex_);
            return LoadLocked(nowSec);
        });
        return true;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    return LoadLocked(nowSec);
}

bool Emitter::PlayScaledVolume(int percent, float nowSec) {
    // Snd3D_PlayScaledVolume 0x4DA380 : charge si besoin, horodate, joue à master*percent/100.
    // ex-VeryOldClient: GSOUND::Play — `mDATA.Play(tCheckLoop, (int)((float)(tVolumeSize*mGAMEOPTION->mSoundOption[1])*0.01f), 0)`. CONFIRMED.
    std::lock_guard<std::mutex> lk(mutex_);
    if (!buffer_.Loaded() && !LoadLocked(nowSec)) return false;
    lastPlaySec_ = nowSec;
    const int master = AudioSystem::Instance().MasterVolume(); // 0..100  — ex-VeryOldClient: mGAMEOPTION->mSoundOption[1]
    // v5 = ftol(master * percent * 0.01) — argument « pourcent » de Snd_Play3D.
    const int vol = static_cast<int>(static_cast<double>(master * percent) * 0.01);
    return buffer_.Play(vol, 0);
}

bool Emitter::PlayFullVolume(float nowSec) {
    // Snd3D_PlayFullVolume 0x4DA3F0 : ne joue que si master != 0, à volume maître plein.
    // ex-VeryOldClient: GSOUND::Play2 — `if (mSoundOption[1]==0) return; ... mDATA.Play(FALSE, mSoundOption[1], 0)`. CONFIRMED.
    std::lock_guard<std::mutex> lk(mutex_);
    const int master = AudioSystem::Instance().MasterVolume();
    if (master == 0) return false;
    if (!buffer_.Loaded() && !LoadLocked(nowSec)) return false;
    lastPlaySec_ = nowSec;
    return buffer_.Play(master, 0);
}

bool Emitter::PlayPositional(const Vec3& listener, const Vec3& source, float nowSec) {
    // Snd3D_PlayPositional 0x4DA450 : dist euclidienne ; si <= 300, volume = (300-d)/300 * master.
    // ex-VeryOldClient: GSOUND::Play3 — `tLength=sqrtf(...); if (tLength>300.0f) return;
    //   tSoundVolumeSize=(int)(((300.0f-tLength)/300.0f)*mSoundOption[1]); mDATA.Play(FALSE, tSoundVolumeSize, 0)`. CONFIRMED.
    std::lock_guard<std::mutex> lk(mutex_);
    const int master = AudioSystem::Instance().MasterVolume();
    if (master == 0) return false;
    if (!buffer_.Loaded() && !LoadLocked(nowSec)) return false;
    lastPlaySec_ = nowSec;

    const float dx = listener.x - source.x;
    const float dy = listener.y - source.y;
    const float dz = listener.z - source.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist > kPositionalFalloff) return false;   // muet au-delà de 300

    // v8 = ftol((300-d)/300 * master) — argument « pourcent » de Snd_Play3D.
    const int vol = static_cast<int>((kPositionalFalloff - dist) / kPositionalFalloff *
                                     static_cast<float>(master));
    return buffer_.Play(vol, 0);
}

void Emitter::UnloadIfExpired(float nowSec, float ttl) {
    // Snd3D_UnloadIfExpired 0x4DA340 : if(loaded && ttl < now - lastPlay) Unload().
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

// ===========================================================================
// SoundBank (WSndBank)
// ===========================================================================

bool SoundBank::Load(const std::vector<std::string>& soundPaths,
                     const std::vector<BankEmitter>& emitters) {
    Clear();
    // WSndBank_LoadFile : chaque son est chargé en boucle (kind 2, 1 buffer).
    // ex-VeryOldClient: WSOUND_FOR_GXD::Load — lit mSoundNum, puis pour chaque son
    //   `mSound[i-1].LoadFromOGG("%s_%04d.OGG", 2 /*sort=loop*/, 1, 1)`, puis lit mSoundInfoNum + mSoundInfo[]. CONFIRMED (sort=2).
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
    // ex-VeryOldClient: WSOUND_FOR_GXD::Play(tPostCoord, tVolumeRatio) — CONFIRMED (§E) :
    //   tVolumeRatio==0 -> Stop de tous les mCheckPlaySound actifs ; sinon, par son, plus proche
    //   émetteur (mUTIL->ReturnLengthXYZ) ; si mRadius>dist -> ChangeVolumeAndPan (déjà en lecture) / Play (1re activation) ; sinon Stop.
    if (sounds_.empty() || emitters_.empty()) return;

    if (!enable) {
        // Branche a3==0 : coupe tout son en cours.
        // ex-VeryOldClient: `if (!tVolumeRatio) { for k: if (mCheckPlaySound[k]) { mCheckPlaySound[k]=0; mSound[k].Stop(); } return; }`.
        for (size_t i = 0; i < sounds_.size(); ++i) {
            if (playing_[i]) {
                playing_[i] = 0;
                if (sounds_[i]) sounds_[i]->Stop();
            }
        }
        return;
    }

    // Pour chaque son i : trouver l'émetteur le plus proche qui le référence.
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
            // Volume proportionnel : (radius - dist)/radius * 100, remis à l'échelle par a3.
            // ex-VeryOldClient: `tVolume = int((tVolumeRatio * ((mRadius - v6)/mRadius * 100.0f)) * 0.01f)`. CONFIRMED (double échelle).
            const int v4 = enableScale *
                           static_cast<int>((radius - bestDist) / radius * 100.0f);
            const int vol = static_cast<int>(static_cast<double>(v4) * 0.01);
            SoundBuffer* sb = sounds_[i].get();
            if (!sb) continue;
            if (playing_[i]) {
                sb->UpdatePlaying(vol, 0);        // déjà en lecture -> Snd_Play2D (ajuste) — ex-VeryOldClient: mSound[i].ChangeVolumeAndPan(tVolume, 0)
            } else {
                playing_[i] = 1;
                sb->Play(vol, 0);                 // démarrage -> Snd_Play3D — ex-VeryOldClient: mSound[i].Play(1, tVolume, 0)
            }
        } else if (playing_[i]) {
            playing_[i] = 0;
            if (sounds_[i]) sounds_[i]->Stop();   // hors de portée -> coupe
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

} // namespace ts2::audio

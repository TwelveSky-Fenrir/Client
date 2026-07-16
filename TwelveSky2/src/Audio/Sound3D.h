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
//   AssetMgr_InitAllSlots   0x4DEB50  amorce les 410 slots de la banque SFX (type 4, index i)
//   AssetMgr_UpdateUnloadExpired 0x4E2050  GC périodique : 6 balayages Snd3D_UnloadIfExpired
//   App_FrameTick           0x4625D0  @0x4626AE garde 60 s -> @0x4626D7 appel du GC (TTL 300 s)
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
#include <cstdint>
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
    // `loop` = arg_0 du binaire, RELAYÉ tel quel à Snd_Play3D (@0x4DA3D5 `mov ecx,[ebp+arg_0]`
    //   / @0x4DA3D8 `push ecx`). Tous les sites relevés passent 0. NE PAS confondre avec le
    //   3e argument « sync » (arg_8), qui est MORT : `mov [ebp+arg_8], 0` @0x4DA389 l'écrase
    //   AVANT tout usage -> Snd3D_EnsureLoaded(this, 0) = toujours asynchrone.
    bool PlayScaledVolume(int loop, int percent, float nowSec);
    // Snd3D_PlayFullVolume 0x4DA3F0 : joue à volume maître plein.
    //   loop non paramétrable : `push 0` littéral @0x4DA438.
    bool PlayFullVolume(float nowSec);
    // Snd3D_PlayPositional 0x4DA450 : volume = (300-dist)/300 * master, muet au-delà de 300.
    //   loop non paramétrable : `push 0` littéral @0x4DA538.
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
    // Snd3D_EnsureLoaded(this, 0) 0x4DA270 @0x4DA27E -> SndLoader1_Enqueue 0x4E6FB0 : enfile la
    // demande sur le thread loader et REND LA MAIN. Variante « verrou déjà pris » (appeler
    // EnsureLoaded(false,…) depuis un play reprendrait mutex_ -> interblocage).
    void EnqueueAsyncLoadLocked(float nowSec);

    mutable std::mutex mutex_;                 // Snd3D+168 (RTL_CRITICAL_SECTION) — ex-VeryOldClient: GSOUND::mLock
    std::string path_;                          // Snd3D+4   — ex-VeryOldClient: GSOUND::mFileName
    SoundBuffer buffer_;                        // Snd3D+104 (SoundObj) — ex-VeryOldClient: GSOUND::mDATA (SOUNDDATA_FOR_GXD)
    float lastPlaySec_ = 0.0f;                  // Snd3D+164 — ex-VeryOldClient: GSOUND::mLastUsedTime
    // Dédup d'enfilage : SndLoader1_Enqueue 0x4E6FB0 @0x4E705A teste l'appartenance à la file
    // « en cours » (SndLoader1Q_IterNotEqual) et REFUSE d'insérer deux fois le même Snd3D.
    // Sans ce drapeau, un son déclenché à chaque frame empilerait une tâche par frame.
    bool  pendingLoad_ = false;
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

// ===========================================================================
// Registre d'émetteurs — adressage par l'adresse ABSOLUE du Snd3D d'origine
// ===========================================================================
//
// Dans le binaire, les Snd3D ne sont PAS alloués : ce sont des tableaux STATIQUES en .data,
// tous relatifs au gestionnaire d'assets `g_ModelMotionArray` 0x8E8B30 (`this` unique, posé
// par App_Init @0x46224B `mov ecx, offset g_ModelMotionArray`). Les ~1700 sites d'appel
// portent donc une adresse EN DUR (`mov ecx, offset flt_1495ABC`).
//
// Ce registre reproduit cet adressage : chaque émetteur est identifié par l'adresse absolue
// du Snd3D d'origine, ce qui permet de câbler un site TODO en recopiant littéralement le
// symbole IDA qu'il cite —  ex. `audio::PlayScaledVolume(0x1495ABC, 0, 100, nowSec)`.
//
// Les 6 bandes Snd3D balayées par AssetMgr_UpdateUnloadExpired 0x4E2050 (base = 0x8E8B30 + off) :
//   @0x4E378A  off 0x86D5CC -> 0x11560FC  [3][2][8][116]   (192 o/slot)
//   @0x4E37F9  off 0xA775CC -> 0x13600FC  [66][3]
//   @0x4E3861  off 0xA80A4C -> 0x136957C  [291][21]
//   @0x4E38A6  off 0xB9F18C -> 0x1487CBC  [410]            <- banque SFX maîtresse
//   @0x4E3957  off 0xBB250C -> 0x149B03C  [3][2][4][12]
//   @0x4E3A14  off 0x9725CC -> 0x125B0FC  [3][2][8][116]
inline constexpr uint32_t kSnd3DStride = 192u;   // prouvé : `imul edx, 0C0h` @0x4E05E4

// Banque SFX maîtresse — l'émetteur de la quasi-totalité des sites (Snd3D_PlayScaledVolume
// 0x4DA380 totalise 1624 xrefs code). Base = 0x8E8B30 + 0xB9F18C.
// Compte 410 PROUVÉ deux fois :  `cmp [ebp+var_10], 19Ah` @0x4E05CC (AssetMgr_InitAllSlots)
//   et `i88 < 410` @0x4E386A (AssetMgr_UpdateUnloadExpired) ; contiguïté vérifiée :
//   0x1487CBC + 410*192 = 0x149B03C = base exacte de la bande suivante (byte_BB250C).
inline constexpr uint32_t kSfxBankBase  = 0x1487CBCu;
inline constexpr int      kSfxBankCount = 410;

// TTL et période du GC — AssetMgr_UpdateUnloadExpired appelé par App_FrameTick 0x4625D0 :
//   @0x4626AE garde `g_GameTimeSec - flt_81518C >= 60.0` (dbl_7EDA50 = 0x404E000000000000)
//   @0x4626D7 appel avec a3 = flt_7A6C9C = 0x43960000 = 300.0f  (TTL)
inline constexpr float kEmitterTtlSec      = 300.0f;
inline constexpr float kEmitterGcPeriodSec = 60.0f;

// Émetteur du registre pour l'adresse absolue `addr` (créé à la volée au 1er accès).
// Si `addr` tombe sur un slot exact de la banque SFX, sa source .ISN est AMORCÉE
// automatiquement — AssetMgr_InitAllSlots 0x4DEB50 @0x4E05CC..0x4E05F4 :
//   `for (i=0; i<0x19A; ++i) Snd3D_SetISNPath(base + 0xC0*i, /*type=*/4, i, 0, 0, 0)`
// soit BuildIsnPath(4, i) = "G03_GDATA\D06_GSOUND\004\E%03d001001.ISN" avec (i+1).
// Hors banque SFX (les 5 autres bandes ci-dessus, amorcées par d'AUTRES boucles de
// AssetMgr_InitAllSlots avec d'autres types — NON relevées à ce jour), la source reste VIDE :
// les play sont alors des no-op silencieux tant que SetEmitterSource n'a pas été appelé.
// Repli sûr : aucun crash, jamais de chemin deviné.
//   TODO [ancre 0x4DEB50] : relever les types/index des 5 autres bandes pour les amorcer aussi.
//
// ATTENTION — piège vérifié : `flt_1687330` n'est PAS un émetteur. C'est le tableau des
// POSITIONS joueur (g_PlayerArray 0x1687234 + 0xFC, stride 0x38C = 908), passé aux deux
// paramètres `Vec3*` de Snd3D_PlayPositional, jamais à `ecx`. Site témoin Pkt_CharStatDelta
// @0x465F1A..0x465F34 : `push offset flt_1687330` (self) / `imul edx,38Ch` + `add edx, offset
// flt_1687330` (joueur idx) / `mov ecx, offset flt_14890FC` <- l'émetteur RÉEL, index 27 de la
// banque SFX. Ne pas transformer une position en émetteur.
Emitter& EmitterAt(uint32_t addr);

// Émetteur d'indice `index` dans la bande basée en `tableAddr` (stride 192 prouvé).
inline Emitter& EmitterInTable(uint32_t tableAddr, int index) {
    return EmitterAt(tableAddr + kSnd3DStride * static_cast<uint32_t>(index));
}

// Snd3D_SetISNPath 0x4DA0C0 sur l'entrée du registre — pour les émetteurs HORS banque SFX,
// dont la source n'est pas amorcée par AssetMgr_InitAllSlots.
void SetEmitterSource(uint32_t addr, int type, int a = 0, int b = 0, int c = 0, int d = 0);

// --- Entrées libres : miroir 1:1 des 3 fonctions du binaire (le `this` devient `addr`). ---
// Les 3 sont NON BLOQUANTES : si le .ISN n'est pas encore chargé, elles l'enfilent sur le
// thread loader et renvoient false SANS jouer (conception du binaire, cf. Sound3D.cpp).
bool PlayScaledVolume(uint32_t addr, int loop, int percent, float nowSec);   // 0x4DA380
bool PlayFullVolume(uint32_t addr, float nowSec);                            // 0x4DA3F0
bool PlayPositional(uint32_t addr, const Vec3& listener, const Vec3& source, // 0x4DA450
                    float nowSec);

// Balaie TOUT le registre -> Snd3D_UnloadIfExpired 0x4DA340 (miroir des 6 boucles de
// AssetMgr_UpdateUnloadExpired 0x4E2050, @0x4E378A..0x4E3A14).
void UnloadExpiredAll(float nowSec, float ttl = kEmitterTtlSec);

// GC d'émetteurs prêt à câbler : encapsule la garde de 60 s ET le TTL de 300 s.
// Miroir exact d'App_FrameTick 0x4625D0 @0x4626AE..0x4626D7 — à appeler à CHAQUE frame
// avec le temps de jeu (g_GameTimeSec 0x815180) ; la garde interne fait le reste.
void TickEmitterGc(float nowSec);

// Nb d'émetteurs matérialisés (diagnostic / tests).
size_t EmitterCount();

} // namespace ts2::audio

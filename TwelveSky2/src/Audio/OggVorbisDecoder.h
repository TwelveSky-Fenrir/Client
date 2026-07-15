// Audio/OggVorbisDecoder.h — décodeur Ogg Vorbis -> PCM 16-bit stéréo 44100.
//
// === Source IDA (idaTs2 — vérité unique) : Snd_LoadOggToBuffers 0x6A8120 ===
//   Le binaire décode l'Ogg via libvorbisfile puis remplit les buffers DirectSound.
//   Le chemin de décodage exact répliqué ici :
//     * OggVF_Open 0x6BFB60   (ov_open) -> on utilise ov_open_callbacks sur mémoire.
//     * Garde de format : rejette tout ce qui n'est PAS  vi->channels == 2  ET
//       vi->rate == 44100  (le binaire fait `goto LABEL_44` -> échec).
//     * OggVF_PcmTotal 0x6BD7C0 (ov_pcm_total) : pré-dimensionne  frames * 4  octets
//       (stéréo x 16-bit = 4 o/frame). Le binaire échoue si le total vaut 0.
//     * OggVF_Read 0x6BEAF0 (ov_read) : buffer 4096, bigendianp=0, word=2 (16-bit),
//       sgned=1. n==0 -> EOF (succès) ; n<0 -> échec (le binaire abandonne).
//     * OggVF_Clear 0x6BD4C0 (ov_clear) : libère le décodeur.
//
// Le PCM produit (entrelacé 16-bit stéréo 44100) alimente ensuite AudioSystem via le
// callback PcmLoadCallback. Namespace ts2::audio.
//
// === Indicateur VeryOldClient (build différent/altéré — NOMS/idiomes seulement) ===
//   ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG (Core/GXD/SOUNDDATA_FOR_GXD.cpp) décode
//   l'OGG inline (ov_open/ov_info/ov_pcm_total/ov_read/ov_clear) dans la MÊME méthode que
//   l'upload buffer. ClientSource scinde décode/upload en deux unités (choix structurel, PLAUSIBLE).
//   Différence d'IO assumée : VeryOld ouvre un FILE* (fopen "rb" + _setmode _O_BINARY(0x8000) + ov_open) ;
//   ClientSource lit en mémoire (ov_open_callbacks). Détail d'IO non transposé, PCM identique.
//   Statuts détaillés : Docs/TS2_AUDIO_ROSETTA.md §B.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::audio {

// Métadonnées du flux décodé (renseignées après un décodage réussi).
struct OggDecodeInfo {
    int      channels    = 0;   // vi->channels (toujours 2 après la garde de format)
    long     sampleRate  = 0;   // vi->rate     (toujours 44100 après la garde de format)
    uint64_t totalFrames = 0;   // ov_pcm_total(-1) : nb de frames stéréo
};

// Décode un Ogg Vorbis EN MÉMOIRE en PCM 16-bit stéréo 44100 (miroir de
// Snd_LoadOggToBuffers 0x6A8120). Rejette tout flux qui n'est pas 2 canaux / 44100 Hz.
// `out` reçoit le PCM entrelacé. Renvoie false si l'ouverture, la garde de format ou
// un ov_read échoue. `info` (optionnel) reçoit les métadonnées du flux.
bool DecodeOggVorbisToPcm16(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& out, OggDecodeInfo* info = nullptr);

// Surcharge vecteur.
bool DecodeOggVorbisToPcm16(const std::vector<uint8_t>& data,
                            std::vector<uint8_t>& out, OggDecodeInfo* info = nullptr);

// Callback de chargement PCM compatible ts2::audio::PcmLoadCallback :
//   bool(const std::string& path, std::vector<uint8_t>& outPcm).
// Lit le fichier via ts2::asset::ReadOggFile (vérifie « OggS ») puis décode.
// À brancher dans AudioSystem::SetLoadCallback.
bool OggVorbisLoadCallback(const std::string& path, std::vector<uint8_t>& out);

} // namespace ts2::audio

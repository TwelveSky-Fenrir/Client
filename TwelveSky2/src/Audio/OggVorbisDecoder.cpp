// Audio/OggVorbisDecoder.cpp — décodage Ogg Vorbis fidèle à Snd_LoadOggToBuffers 0x6A8120.
//
// Le binaire décode via un FILE* (ov_open sur un descripteur CRT). On garde la MÊME
// logique de décodage mais on lit depuis un tampon mémoire (ov_open_callbacks), ce qui
// permet de réutiliser ts2::asset::ReadOggFile (lecture disque + vérif « OggS »).
#include "Audio/OggVorbisDecoder.h"

#include "Asset/Sound.h"   // ts2::asset::ReadOggFile
#include "Core/Log.h"

#include <cstring>

#include <vorbis/vorbisfile.h>   // ov_open_callbacks / ov_info / ov_pcm_total / ov_read / ov_clear

// libvorbisfile dépend de libvorbis, qui dépend de libogg (voir external/oggvorbis/lib).
#pragma comment(lib, "vorbisfile_static.lib")
#pragma comment(lib, "vorbis_static.lib")
#pragma comment(lib, "ogg_static.lib")

namespace ts2::audio {

namespace {

// ---------------------------------------------------------------------------
// Flux mémoire : équivalent du FILE* utilisé par le binaire, mais sur un tampon
// déjà résident. `pos` est l'offset courant de lecture (0..size).
// ---------------------------------------------------------------------------
struct MemStream {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;
};

// read_func — sémantique fread : lit au plus `size*nmemb` octets, renvoie le nombre
// d'éléments complets de `size` octets effectivement lus.
size_t MemRead(void* ptr, size_t size, size_t nmemb, void* datasource) {
    MemStream* s = static_cast<MemStream*>(datasource);
    if (!s || !ptr || size == 0 || nmemb == 0) return 0;
    size_t remaining = (s->pos < s->size) ? (s->size - s->pos) : 0;
    size_t wanted    = size * nmemb;
    size_t bytes     = (wanted < remaining) ? wanted : remaining;
    if (bytes) {
        std::memcpy(ptr, s->data + s->pos, bytes);
        s->pos += bytes;
    }
    return bytes / size;   // nb d'éléments complets (size==1 dans vorbisfile -> == bytes)
}

// seek_func — renvoie 0 en cas de succès, -1 si hors bornes (flux toujours seekable ici).
int MemSeek(void* datasource, ogg_int64_t offset, int whence) {
    MemStream* s = static_cast<MemStream*>(datasource);
    if (!s) return -1;
    ogg_int64_t newpos;
    switch (whence) {
        case SEEK_SET: newpos = offset;                                   break;
        case SEEK_CUR: newpos = static_cast<ogg_int64_t>(s->pos)  + offset; break;
        case SEEK_END: newpos = static_cast<ogg_int64_t>(s->size) + offset; break;
        default:       return -1;
    }
    if (newpos < 0 || newpos > static_cast<ogg_int64_t>(s->size)) return -1;
    s->pos = static_cast<size_t>(newpos);
    return 0;
}

// close_func — rien à fermer (tampon possédé par l'appelant).
int MemClose(void* /*datasource*/) { return 0; }

// tell_func — offset courant.
long MemTell(void* datasource) {
    MemStream* s = static_cast<MemStream*>(datasource);
    return s ? static_cast<long>(s->pos) : -1;
}

} // namespace

// ===========================================================================
// DecodeOggVorbisToPcm16 — réplique du décodage de Snd_LoadOggToBuffers 0x6A8120.
// ===========================================================================
bool DecodeOggVorbisToPcm16(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& out, OggDecodeInfo* info) {
    out.clear();
    if (info) { info->channels = 0; info->sampleRate = 0; info->totalFrames = 0; }
    if (!data || size == 0) return false;

    MemStream stream{ data, size, 0 };

    ov_callbacks cb;
    cb.read_func  = &MemRead;
    cb.seek_func  = &MemSeek;
    cb.close_func = &MemClose;
    cb.tell_func  = &MemTell;

    // OggVF_Open 0x6BFB60 (ov_open) -> ov_open_callbacks sur mémoire.
    OggVorbis_File vf;
    int rc = ov_open_callbacks(&stream, &vf, nullptr, 0, cb);
    if (rc < 0) {
        TS2_ERR("OGG : ov_open_callbacks a echoue (%d)", rc);
        return false;
    }

    // Garde de format stricte : le binaire exige vi->channels == 2 ET vi->rate == 44100,
    // sinon `goto LABEL_44` -> échec (Snd_LoadOggToBuffers 0x6A8120).
    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi || vi->channels != 2 || vi->rate != 44100) {
        if (vi)
            TS2_WARN("OGG : format rejete (channels=%d rate=%ld, exige 2/44100)",
                     vi->channels, vi->rate);
        ov_clear(&vf);
        return false;
    }

    // Pré-dimensionnement : ov_pcm_total(-1) frames * 4 octets (2 canaux x 2 o).
    // Le binaire échoue si le total vaut 0 (`if (!(4*v14)) goto LABEL_44`).
    ogg_int64_t frames = ov_pcm_total(&vf, -1);
    if (frames <= 0) {
        TS2_WARN("OGG : ov_pcm_total nul ou invalide (%lld)",
                 static_cast<long long>(frames));
        ov_clear(&vf);
        return false;
    }
    if (info) {
        info->channels    = vi->channels;
        info->sampleRate  = vi->rate;
        info->totalFrames = static_cast<uint64_t>(frames);
    }
    out.reserve(static_cast<size_t>(frames) * 4);

    // Boucle OggVF_Read 0x6BEAF0 (ov_read) : buffer 4096, bigendianp=0, word=2, sgned=1.
    //   n == 0 -> EOF (succès) ; n < 0 -> échec (le binaire abandonne sur négatif).
    char scratch[4096];
    int  bitstream = 0;
    for (;;) {
        long n = ov_read(&vf, scratch, static_cast<int>(sizeof(scratch)),
                         0 /*bigendianp*/, 2 /*word=16-bit*/, 1 /*sgned*/, &bitstream);
        if (n == 0) break;   // EOF
        if (n < 0) {         // erreur de flux -> échec fidèle
            TS2_WARN("OGG : ov_read erreur (%ld)", n);
            ov_clear(&vf);
            out.clear();
            return false;
        }
        const uint8_t* p = reinterpret_cast<const uint8_t*>(scratch);
        out.insert(out.end(), p, p + n);
    }

    // OggVF_Clear 0x6BD4C0 (ov_clear).
    ov_clear(&vf);
    return !out.empty();
}

bool DecodeOggVorbisToPcm16(const std::vector<uint8_t>& data,
                            std::vector<uint8_t>& out, OggDecodeInfo* info) {
    return DecodeOggVorbisToPcm16(data.data(), data.size(), out, info);
}

// ===========================================================================
// OggVorbisLoadCallback — signature ts2::audio::PcmLoadCallback.
// ===========================================================================
bool OggVorbisLoadCallback(const std::string& path, std::vector<uint8_t>& out) {
    // Réutilise le lecteur Asset : lit le fichier brut + vérifie la signature « OggS ».
    std::vector<uint8_t> raw;
    if (!ts2::asset::ReadOggFile(path, raw)) {
        out.clear();
        return false;
    }
    return DecodeOggVorbisToPcm16(raw, out, nullptr);
}

} // namespace ts2::audio

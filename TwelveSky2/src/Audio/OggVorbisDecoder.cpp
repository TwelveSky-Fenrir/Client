// Audio/OggVorbisDecoder.cpp — Ogg Vorbis decoding faithful to Snd_LoadOggToBuffers 0x6A8120.
//
// The binary decodes via a FILE* (ov_open on a CRT descriptor). We keep the SAME
// decode logic but read from a memory buffer (ov_open_callbacks), which lets us
// reuse ts2::asset::ReadOggFile (disk read + "OggS" signature check).
#include "Audio/OggVorbisDecoder.h"

#include "Asset/Sound.h"   // ts2::asset::ReadOggFile
#include "Core/Log.h"

#include <cstring>

#include <vorbis/vorbisfile.h>   // ov_open_callbacks / ov_info / ov_pcm_total / ov_read / ov_clear

// libvorbisfile depends on libvorbis, which depends on libogg (see external/oggvorbis/lib).
#pragma comment(lib, "vorbisfile_static.lib")
#pragma comment(lib, "vorbis_static.lib")
#pragma comment(lib, "ogg_static.lib")

namespace ts2::audio {

namespace {

// Memory stream: equivalent of the FILE* used by the binary, but over an
// already-resident buffer. `pos` is the current read offset (0..size).
struct MemStream {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;
};

// read_func — fread semantics: reads at most `size*nmemb` bytes, returns the number
// of complete `size`-byte elements actually read.
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
    return bytes / size;   // number of complete elements (size==1 in vorbisfile -> == bytes)
}

// seek_func — returns 0 on success, -1 if out of bounds (stream is always seekable here).
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

// close_func — nothing to close (buffer owned by the caller).
int MemClose(void* /*datasource*/) { return 0; }

// tell_func — current offset.
long MemTell(void* datasource) {
    MemStream* s = static_cast<MemStream*>(datasource);
    return s ? static_cast<long>(s->pos) : -1;
}

} // namespace

// DecodeOggVorbisToPcm16 — replica of Snd_LoadOggToBuffers 0x6A8120's decoding.
// ex-VeryOldClient: SOUNDDATA_FOR_GXD::LoadFromOGG — recovered decode steps:
//   fopen("rb")+_setmode(_O_BINARY)+ov_open -> ov_info (guards 2ch/44100) -> tFileSize=4*ov_pcm_total(-1)
//   -> loop ov_read(scratch,4096,0,2,1) accumulated via memcpy -> ov_clear. CONFIRMED (see §B).
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

    // OggVF_Open 0x6BFB60 (ov_open) -> ov_open_callbacks over memory.
    // ex-VeryOldClient: LoadFromOGG — `fp=fopen(tFileName,"rb"); _setmode(_fileno(fp),0x8000); ov_open(fp,&vorbis,0,0)`
    //   (ov_open takes ownership of the FILE*). PLAUSIBLE — IO divergence (FILE* vs memory), identical decode semantics.
    OggVorbis_File vf;
    int rc = ov_open_callbacks(&stream, &vf, nullptr, 0, cb);
    if (rc < 0) {
        TS2_ERR("OGG : ov_open_callbacks a echoue (%d)", rc);
        return false;
    }

    // Strict format guard: the binary requires vi->channels == 2 AND vi->rate == 44100,
    // else `goto LABEL_44` -> failure (Snd_LoadOggToBuffers 0x6A8120).
    // ex-VeryOldClient: LoadFromOGG `if (!info || info->channels != 2 || info->rate != 44100) GORET();`. CONFIRMED (bit-exact guard).
    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi || vi->channels != 2 || vi->rate != 44100) {
        if (vi)
            TS2_WARN("OGG : format rejete (channels=%d rate=%ld, exige 2/44100)",
                     vi->channels, vi->rate);
        ov_clear(&vf);
        return false;
    }

    // Pre-sizing: ov_pcm_total(-1) frames * 4 bytes (2 channels x 2 bytes).
    // The binary fails if the total is 0 (`if (!(4*v14)) goto LABEL_44`).
    // ex-VeryOldClient: LoadFromOGG `tFileSize = 4 * ov_pcm_total(&vorbis, -1); tFileData = mem::alloc(tFileSize);`
    //   (4 = nBlockAlign); NULL/zero total -> GORET. CONFIRMED (same calc, same failure on zero total).
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

    // OggVF_Read 0x6BEAF0 loop (ov_read): buffer 4096, bigendianp=0, word=2, sgned=1.
    //   n == 0 -> EOF (success); n < 0 -> failure (the binary bails out on negative).
    // ex-VeryOldClient: LoadFromOGG scratch = `mGXD->mSoundOutBufferForPCM[4096]` (GXD singleton's
    //   SHARED decode buffer). PLAUSIBLE — here a local stack buffer (reentrant/thread-safe),
    //   functionally equivalent with no observable effect.
    char scratch[4096];
    int  bitstream = 0;
    for (;;) {
        // ex-VeryOldClient: `Size = ov_read(&vorbis, mGXD->mSoundOutBufferForPCM, 4096, 0, 2, 1, &bitstream)`. CONFIRMED (byte-identical params).
        long n = ov_read(&vf, scratch, static_cast<int>(sizeof(scratch)),
                         0 /*bigendianp*/, 2 /*word=16-bit*/, 1 /*sgned*/, &bitstream);
        // ex-VeryOldClient: `(Size & 0x80000000) != 0` -> ov_read_error break; `!Size` -> break EOF;
        //   else `memcpy(&tFileData[i], scratch, Size); i += Size`. CONFIRMED (negative = failure, zero = success).
        //   (VeryOld has an overflow guard `Size + i > tFileSize` -> failure; not proven in IDA, not ported: here `vector` grows dynamically.)
        if (n == 0) break;   // EOF
        if (n < 0) {         // stream error -> faithful failure
            TS2_WARN("OGG : ov_read erreur (%ld)", n);
            ov_clear(&vf);
            out.clear();
            return false;
        }
        const uint8_t* p = reinterpret_cast<const uint8_t*>(scratch);
        out.insert(out.end(), p, p + n);
    }

    // OggVF_Clear 0x6BD4C0 (ov_clear).
    // ex-VeryOldClient: `ov_clear(&vorbis)` (also closes the owned FILE*) then `mem::release(&tFileData)`. CONFIRMED.
    ov_clear(&vf);
    return !out.empty();
}

bool DecodeOggVorbisToPcm16(const std::vector<uint8_t>& data,
                            std::vector<uint8_t>& out, OggDecodeInfo* info) {
    return DecodeOggVorbisToPcm16(data.data(), data.size(), out, info);
}

// OggVorbisLoadCallback — signature ts2::audio::PcmLoadCallback.
bool OggVorbisLoadCallback(const std::string& path, std::vector<uint8_t>& out) {
    // ex-VeryOldClient: equivalent of the IO part of SOUNDDATA_FOR_GXD::LoadFromOGG (VeryOld fopens the
    //   FILE* right inside LoadFromOGG); here split into Asset read + decode. PLAUSIBLE (ClientSource split).
    // Reuses the Asset reader: reads the raw file + checks the "OggS" signature.
    std::vector<uint8_t> raw;
    if (!ts2::asset::ReadOggFile(path, raw)) {
        out.clear();
        return false;
    }
    return DecodeOggVorbisToPcm16(raw, out, nullptr);
}

} // namespace ts2::audio

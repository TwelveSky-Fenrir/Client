// Asset/NpkArchive.cpp — faithful translation of RE/asset_parsers/npk.py (validated).
#include "Asset/NpkArchive.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cctype>
#include <cstring>

namespace ts2::asset {

// ResMgr_HashName 0x708A2C — Adler-like hash (mod 65521) folded into a bucket index (mod 257).
// The "A..Z -> +32" fold (@0x708A4E) is equivalent to lowercasing: the hash is therefore
// case-insensitive in ASCII, consistent with Find's Crt_Stricmp 0x76668B (@0x6FD65B).
// FIDELITY NOTE (SIGNED): the binary loads each byte into a SIGNED `char` (`mov al,[esi]`
// @0x708A31). The range bounds are therefore compared as SIGNED (`cmp al,41h`/`jl` @0x708A43,
// `cmp al,5Ah`/`jg` @0x708A47) and the added value is SIGN-EXTENDED (`movsx eax,al`
// @0x708A4B and @0x708A54) — hence NEGATIVE for any byte >= 0x80 — while the modulo
// stays UNSIGNED (`div` @0x708A5D/@0x708A68/@0x708A82). Iterating over `unsigned char`
// would diverge on any non-ASCII name. Explicit int8_t/int32_t: the result does not depend
// on the platform's default `char` signedness (/J).
uint32_t NpkArchive::HashName(const std::string& name) {
    uint32_t a = 1, b = 0; // ecx = 1 @0x708A36 ; edi = 0 @0x708A37
    for (char ch : name) {
        if (ch == '\0') break; // `test al,al` @0x708A39/@0x708A6F: the binary stops at NUL
        const int32_t  sc = static_cast<int32_t>(static_cast<int8_t>(ch)); // movsx eax, al
        const uint32_t v  = ((sc >= 65 && sc <= 90) ? (a + static_cast<uint32_t>(sc) + 32u)  // @0x708A4E
                                                    : (a + static_cast<uint32_t>(sc)))       // @0x708A57
                            % 0xFFF1u;                                                       // div @0x708A5D
        a = v;
        b = (b + v) % 0xFFF1u; // div @0x708A68
    }
    return (a | (b << 16)) % 0x101u; // shl/or/div @0x708A76-@0x708A82
}

bool NpkArchive::Open(const std::string& path, const XteaKey& key) {
    key_ = key;
    entries_.clear();
    buckets_.assign(kBucketCount, std::vector<uint32_t>());
    useBuckets_ = false;
    if (!ReadWholeFile(path, data_)) {
        TS2_ERR("NPK: cannot open: %s", path.c_str());
        return false;
    }
    try {
        ByteReader r(data_);
        if (!(r.PeekMagic("NPK!", 4) || r.PeekMagic("NPAK", 4)))
            throw AssetError("invalid NPK magic");
        r.Skip(4);
        version_    = r.U32();
        entryCount_ = r.U32();
        dirOff_     = r.U32();
        dataOff_    = r.U32();
        if (version_ < 21) throw AssetError("unsupported NPK version"); // Err_SetLast(-255) @0x6FD11C
        if (version_ >= 23) r.U32(); // hdrExtra (unvalidated) — @0x6FD163

        // Npk_ParseDirectory 0x6FD04C @0x6FD178: below version 24 the binary does NOT read a
        // single contiguous directory — it seeks to dirOff+base (@0x6FD2F7) then loops entry
        // by entry (28-byte Pak_ReadAndDecrypt with a9=0/a10=0, so the no-tail XTEA variant,
        // then reads the name @0x6FD3E5 + Pak_StrDup @0x6FD410); and below version 22 the
        // record differs further still (FILETIME converted via Filetime_ToUnixSeconds 0x70879E
        // @0x6FD384). No shipped archive is affected (GXDEffect.npk = v27): we reject outright
        // rather than silently mis-parsing via the contiguous path.
        // TODO [Npk_ParseDirectory 0x6FD04C @0x6FD2F7-0x6FD429]: the per-entry path is not
        // implemented — no asset can prove it, so it is not guessed.
        if (version_ < 24)
            throw AssetError("NPK version < 24: per-entry directory not implemented");

        // Directory table, XTEA-decrypted. Variant selection = a9/a10 arguments of
        // Pak_ReadAndDecrypt @0x6FD1FC: tail = version >= 25, variant = !(version >= 26).
        const bool tail    = version_ >= 25;
        const bool variant = !(version_ >= 26); // >=26 => standard XTEA (BlockStd)
        size_t start = dirOff_;

        // The directory-length THRESHOLD is 27, NOT 24 (@0x6FD181):
        //   version >= 27: guards fileSize < dataOff -> Err_SetLast(-52) (@0x6FD188), then
        //                   length = dataOff - dirOff (@0x6FD191) — layout
        //                   [header][directory][data];
        //   24 <= version < 27: lseek(dirOff + base) then length = fileSize - dirOff
        //                   (@0x6FD1A8) — layout [header][data][directory].
        // (`base` = archive+1116: null for a file-backed archive; only Npk_OpenFromMemory
        //  0x6FD453 would set it, and it has 0 xrefs — dead code in the binary.)
        if (size_t(dirOff_) > data_.size())
            throw AssetError("dirOff out of file bounds"); // negative length -> Crt_Malloc fails -> Err_SetLast(-100) @0x6FD1C7
        size_t length;
        if (version_ >= 27) {
            if (size_t(dataOff_) > data_.size())
                throw AssetError("dataOff out of file bounds"); // Err_SetLast(-52) @0x6FD188
            if (dataOff_ < dirOff_)
                throw AssetError("dataOff before dirOff"); // same: negative v21 -> Err_SetLast(-100) @0x6FD1C7
            length = size_t(dataOff_) - dirOff_;
        } else {
            length = data_.size() - dirOff_;
        }
        if (start + length > data_.size()) throw AssetError("directory table out of file bounds");

        std::vector<uint8_t> dir(data_.begin() + start, data_.begin() + start + length);
        XteaDecryptBuffer(dir.data(), dir.size(), key_, tail, variant);

        ByteReader dr(dir);
        entries_.reserve(entryCount_);
        for (uint32_t i = 0; i < entryCount_; ++i) {
            NpkEntry e;
            e.offset   = dr.U32();
            // Npk_ParseDirectory 0x6FD04C @0x6FD259: for version < 27 the data PRECEDES
            // the directory, so any entry pointing into/past it is corrupt
            // -> LABEL_46 @0x6FD434 Err_SetLast(-50). Guard deliberately DISABLED at v27, where
            // the layout flips (GXDEffect.npk: dirOff=0x18 < dataOff=0x1EC).
            if (version_ < 27 && e.offset >= dirOff_)
                throw AssetError("entry offset inside the directory");
            e.stored   = dr.U32();
            e.raw      = dr.U32();
            e.flags    = dr.U32();
            e.filetime = dr.U64();
            e.nameLen  = dr.U16();
            e.reserved = dr.U16();
            e.name     = dr.Str(e.nameLen);
            entries_.push_back(std::move(e));

            // ResMgr_AddEntry 0x708C87: the binary hashes EVERY inserted entry (@0x708CD5) and
            // does so on the STORED name AS-IS (v5 = a2[8] @0x708CC6) — never normalized —
            // then chains it at the tail of the bucket (@0x708CFC/@0x708D05), hence the insertion order.
            buckets_[HashName(entries_.back().name)].push_back(
                static_cast<uint32_t>(entries_.size() - 1));
            // a1[277] = 1 once there are 257+ entries (@0x708D17): this flag is what switches
            // Npk_FindEntryByName from buckets to a linear scan.
            if (entries_.size() >= kBucketCount) useBuckets_ = true;
        }
        return true;
    } catch (const std::exception& ex) {
        TS2_ERR("NPK: parse failed (%s): %s", ex.what(), path.c_str());
        entries_.clear();
        buckets_.assign(kBucketCount, std::vector<uint32_t>());
        useBuckets_ = false;
        return false;
    }
}

std::vector<uint8_t> NpkArchive::Read(const NpkEntry& e) const {
    if (size_t(e.offset) + e.stored > data_.size())
        throw AssetError("NPK blob out of file bounds: " + e.name);
    std::vector<uint8_t> blob(data_.begin() + e.offset, data_.begin() + e.offset + e.stored);
    const bool tail = version_ >= 25;

    // 1) XTEA variant BEFORE decompression if (0x100 && 0x100000)
    if ((e.flags & kNpkX2) && (e.flags & kNpkX2Pre))
        XteaDecryptBuffer(blob.data(), blob.size(), key_, tail, /*variant*/ true);
    // 2) standard XTEA if 0x10
    if (e.flags & kNpkXtea)
        XteaDecryptBuffer(blob.data(), blob.size(), key_, tail, /*variant*/ false);
    // 3) zlib decompression if 0x1000 (threshold rawSize >= 0x100)
    if (e.flags & kNpkZlib) {
        if (e.raw >= 0x100) {
            blob = Zlib::Instance().InflateTo(blob.data(), blob.size(), e.raw);
        }
        // else: direct copy (already in blob)
    }
    // 4) XTEA variant AFTER decompression if (0x100 && !0x100000)
    // Npk_ReadEntryData 0x6FD746 @0x6FD8CE: the length passed is a1[2] = rawSize (NOT the
    // stored size), and the tail flag is hardcoded 0. After the inflate above,
    // blob.size() == e.raw: the two match. Without kNpkZlib however, the binary decrypts
    // `raw` bytes of a raw-sized dest buffer while only `stored` bytes were actually
    // read — a case where raw == stored in practice (nothing to decompress). We therefore
    // clamp to the actual buffer to never overrun.
    // TODO [Npk_ReadEntryData 0x6FD746 @0x6FD8CE]: the raw > stored case WITHOUT kNpkZlib is
    // not exercised by any shipped asset (the 12 entries of GXDEffect.npk are flags=0x100000 only);
    // absent proof of the expected content beyond `stored`, it is not reproduced.
    if ((e.flags & kNpkX2) && !(e.flags & kNpkX2Pre)) {
        const size_t n = (size_t(e.raw) < blob.size()) ? size_t(e.raw) : blob.size();
        XteaDecryptBuffer(blob.data(), n, key_, /*tail*/ false, /*variant*/ true);
    }

    return blob;
}

const NpkEntry* NpkArchive::Find(const std::string& name) const {
    // Npk_FindEntryByName 0x6FD5E1: the binary first NORMALIZES the REQUESTED name into a
    // 512-byte buffer — Path_NormalizeSlashes(a2, v5, 0x200) @0x6FD623 — which rejects (-20)
    // any name of length >= 512 (Crt_Strlen >= a3 @0x708A97) and replaces "\" (92) with
    // "/" (47) (@0x708AC2/@0x708AC4).
    // WARNING: the STORED name is NEVER normalized: ResMgr_AddEntry 0x708C87 hashes the raw name
    // (@0x708CC6) and the comparison is Crt_Stricmp(stored_raw, requested_normalized)
    // (@0x6FD65B/@0x6FD694). Normalizing the stored name would make this function find entries
    // the binary itself cannot find — so we don't.
    if (name.size() >= 512) return nullptr; // Err_SetLast(-20) @0x708A97
    std::string q = name;
    for (char& c : q)
        if (c == '\\') c = '/'; // @0x708AC2/@0x708AC4

    // Case-insensitive comparison (Crt_Stricmp 0x76668B).
    const auto stricmpEq = [](const std::string& lhs, const std::string& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (size_t i = 0; i < rhs.size(); ++i)
            if (std::tolower((unsigned char)lhs[i]) != std::tolower((unsigned char)rhs[i]))
                return false;
        return true;
    };

    if (useBuckets_) {
        // Bucket path (a1[277] != 0): v3 = a1[ResMgr_HashName(v5) + 20] @0x6FD63B, then
        // walks the bucket chain (i = *v3 ; i = *(i+56)) @0x6FD644.
        for (uint32_t idx : buckets_[HashName(q)])
            if (stricmpEq(entries_[idx].name, q)) return &entries_[idx]; // @0x6FD65B
    } else {
        // Linear path (a1[277] == 0): walks the global insertion list
        // (i = a1[11] ; i = *(i+48)) @0x6FD67C. This is the path the binary takes for
        // GXDEffect.npk (12 entries < 257, cf. @0x708D17).
        for (const auto& e : entries_)
            if (stricmpEq(e.name, q)) return &e; // @0x6FD694
    }
    return nullptr; // Err_SetLast(-10) @0x6FD666
}

std::vector<uint8_t> NpkArchive::Read(const std::string& name) const {
    const NpkEntry* e = Find(name);
    if (!e) throw AssetError("missing NPK entry: " + name);
    return Read(*e);
}

} // namespace ts2::asset

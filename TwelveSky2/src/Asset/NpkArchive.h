// Asset/NpkArchive.h — NPK (NPacK) archive reader. Faithful to Npk_ParseDirectory
// 0x6FD04C / Npk_ReadEntryData 0x6FD746. Validated byte-exact on GXDEffect.npk.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Asset/Xtea.h"

namespace ts2::asset {

// Bits of an entry's flags field (interpreted by Npk_ReadEntryData).
enum NpkFlags : uint32_t {
    kNpkXtea     = 0x00010u,  // blob encrypted with standard XTEA
    kNpkX2       = 0x00100u,  // blob encrypted with XTEA variant
    kNpkZlib     = 0x01000u,  // blob compressed with zlib (if rawSize >= 0x100)
    kNpkX2Pre    = 0x100000u, // + kNpkX2 => variant BEFORE decompression; alone => "stored as-is"
};

struct NpkEntry {
    uint32_t offset = 0;     // absolute offset of the blob in the file
    uint32_t stored = 0;     // size on disk
    uint32_t raw    = 0;     // decompressed size
    uint32_t flags  = 0;
    // Timestamp. WARNING: the SEMANTICS switch at version 22 (Npk_ParseDirectory 0x6FD04C
    // @0x6FD337): for version >= 22 the 28 bytes of the record are copied verbatim and the field
    // is ALREADY in unix seconds; for version < 22 the disk carries a Windows FILETIME that
    // the binary converts via Filetime_ToUnixSeconds 0x70879E (@0x6FD384). Only the >= 22
    // regime is reachable here: Open rejects version < 24 (cf. NpkArchive.cpp).
    uint64_t filetime = 0;
    uint16_t nameLen = 0;
    uint16_t reserved = 0;
    std::string name;
};

class NpkArchive {
public:
    // Loads and parses the archive. `key` = XTEA key (default {1,4,4,1}).
    bool Open(const std::string& path, const XteaKey& key = kNpkKey);

    uint32_t Version() const { return version_; }
    const std::vector<NpkEntry>& Entries() const { return entries_; }

    // Looks up an entry by name (case-insensitive). nullptr if absent.
    const NpkEntry* Find(const std::string& name) const;

    // Extracts/decrypts/decompresses an entry into memory (Npk_ReadEntryData).
    std::vector<uint8_t> Read(const NpkEntry& e) const;
    std::vector<uint8_t> Read(const std::string& name) const;

    // Bucket index of a name (ResMgr_HashName 0x708A2C). CALLED on every insertion by
    // Open (faithful: ResMgr_AddEntry 0x708C87 @0x708CD5 hashes EVERY inserted entry,
    // regardless of the switch flag's state), then by Find once the buckets are armed.
    static uint32_t HashName(const std::string& name);

    // Number of buckets = final hash modulo (0x101 = 257) — ResMgr_HashName @0x708A7D.
    static constexpr size_t kBucketCount = 0x101u;

private:
    std::vector<uint8_t> data_;
    std::vector<NpkEntry> entries_;
    // Buckets from ResMgr_AddEntry 0x708C87: a1[hash + 20] @0x708CD5, chained via entry+56.
    // We store INDEXES into entries_ (order = insertion order, like the binary's tail
    // chaining @0x708CFC/@0x708D05), since pointers get invalidated by push_back.
    std::vector<std::vector<uint32_t>> buckets_;
    // a1[277] @0x708D17: bucket lookup only ARMS once there are 257+ entries; below that
    // Npk_FindEntryByName 0x6FD5E1 scans the insertion list itself (@0x6FD67C).
    bool     useBuckets_ = false;
    XteaKey  key_ = kNpkKey;
    uint32_t version_ = 0;
    uint32_t entryCount_ = 0;
    uint32_t dirOff_ = 0;
    uint32_t dataOff_ = 0;
};

} // namespace ts2::asset

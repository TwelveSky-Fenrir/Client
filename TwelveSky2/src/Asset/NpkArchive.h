// Asset/NpkArchive.h — lecteur d'archive NPK (NPacK). Fidèle à Npk_ParseDirectory
// 0x6FD04C / Npk_ReadEntryData 0x6FD746. Validé byte-exact sur GXDEffect.npk.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Asset/Xtea.h"

namespace ts2::asset {

// Bits du champ flags d'une entrée (interprétés par Npk_ReadEntryData).
enum NpkFlags : uint32_t {
    kNpkXtea     = 0x00010u,  // blob chiffré XTEA standard
    kNpkX2       = 0x00100u,  // blob chiffré XTEA variante
    kNpkZlib     = 0x01000u,  // blob compressé zlib (si rawSize >= 0x100)
    kNpkX2Pre    = 0x100000u, // + kNpkX2 => variante AVANT décompression ; seul => « stocké tel quel »
};

struct NpkEntry {
    uint32_t offset = 0;     // offset absolu du blob dans le fichier
    uint32_t stored = 0;     // taille sur disque
    uint32_t raw    = 0;     // taille décompressée
    uint32_t flags  = 0;
    uint64_t filetime = 0;   // FILETIME Windows
    uint16_t nameLen = 0;
    uint16_t reserved = 0;
    std::string name;
};

class NpkArchive {
public:
    // Charge et parse l'archive. `key` = clé XTEA (défaut {1,4,4,1}).
    bool Open(const std::string& path, const XteaKey& key = kNpkKey);

    uint32_t Version() const { return version_; }
    const std::vector<NpkEntry>& Entries() const { return entries_; }

    // Cherche une entrée par nom (insensible à la casse). nullptr si absente.
    const NpkEntry* Find(const std::string& name) const;

    // Extrait/déchiffre/décompresse une entrée en mémoire (Npk_ReadEntryData).
    std::vector<uint8_t> Read(const NpkEntry& e) const;
    std::vector<uint8_t> Read(const std::string& name) const;

    // Hash de nom (ResMgr_HashName 0x708A2C) — pour info/lookup à buckets.
    static uint32_t HashName(const std::string& name);

private:
    std::vector<uint8_t> data_;
    std::vector<NpkEntry> entries_;
    XteaKey  key_ = kNpkKey;
    uint32_t version_ = 0;
    uint32_t entryCount_ = 0;
    uint32_t dirOff_ = 0;
    uint32_t dataOff_ = 0;
};

} // namespace ts2::asset

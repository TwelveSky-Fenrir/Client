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
    // Horodatage. ⚠ La SÉMANTIQUE bascule à la version 22 (Npk_ParseDirectory 0x6FD04C
    // @0x6FD337) : pour version >= 22 les 28 o du record sont recopiés verbatim et le champ
    // est DÉJÀ en secondes unix ; pour version < 22 le disque porte un FILETIME Windows que
    // le binaire convertit via Filetime_ToUnixSeconds 0x70879E (@0x6FD384). Seul le régime
    // >= 22 est atteignable ici : Open rejette version < 24 (cf. NpkArchive.cpp).
    uint64_t filetime = 0;
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

    // Index de seau d'un nom (ResMgr_HashName 0x708A2C). APPELÉE à chaque insertion par
    // Open (fidèle : ResMgr_AddEntry 0x708C87 @0x708CD5 hache TOUTE entrée, quel que soit
    // l'état du drapeau de bascule), puis par Find quand les seaux sont armés.
    static uint32_t HashName(const std::string& name);

    // Nombre de seaux = modulo final du hash (0x101 = 257) — ResMgr_HashName @0x708A7D.
    static constexpr size_t kBucketCount = 0x101u;

private:
    std::vector<uint8_t> data_;
    std::vector<NpkEntry> entries_;
    // Seaux de ResMgr_AddEntry 0x708C87 : a1[hash + 20] @0x708CD5, chaîne par entrée+56.
    // On stocke des INDEX dans entries_ (l'ordre = ordre d'insertion, comme le chaînage en
    // queue du binaire @0x708CFC/@0x708D05), les pointeurs étant invalidés par push_back.
    std::vector<std::vector<uint32_t>> buckets_;
    // a1[277] @0x708D17 : le lookup à seaux ne s'ARME qu'à partir de 257 entrées ; en dessous
    // Npk_FindEntryByName 0x6FD5E1 balaye lui-même la liste d'insertion (@0x6FD67C).
    bool     useBuckets_ = false;
    XteaKey  key_ = kNpkKey;
    uint32_t version_ = 0;
    uint32_t entryCount_ = 0;
    uint32_t dirOff_ = 0;
    uint32_t dataOff_ = 0;
};

} // namespace ts2::asset

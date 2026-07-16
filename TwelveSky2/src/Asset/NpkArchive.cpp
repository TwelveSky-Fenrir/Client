// Asset/NpkArchive.cpp — traduction fidèle de RE/asset_parsers/npk.py (validé).
#include "Asset/NpkArchive.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cctype>
#include <cstring>

namespace ts2::asset {

// ResMgr_HashName 0x708A2C — hash Adler-like (mod 65521) replié en index de seau (mod 257).
// Le repli « A..Z -> +32 » (@0x708A4E) équivaut à un passage en minuscule : le hash est donc
// insensible à la casse en ASCII, cohérent avec le Crt_Stricmp 0x76668B de Find (@0x6FD65B).
// ⚠ FIDÉLITÉ SIGNÉE : le binaire charge chaque octet dans un `char` SIGNÉ (`mov al,[esi]`
// @0x708A31). Les bornes de plage sont donc comparées en SIGNÉ (`cmp al,41h`/`jl` @0x708A43,
// `cmp al,5Ah`/`jg` @0x708A47) et la valeur ajoutée est SIGN-ÉTENDUE (`movsx eax,al`
// @0x708A4B et @0x708A54) — donc NÉGATIVE pour tout octet >= 0x80 — tandis que le modulo
// reste NON SIGNÉ (`div` @0x708A5D/@0x708A68/@0x708A82). Itérer sur `unsigned char`
// divergerait sur tout nom non-ASCII. int8_t/int32_t explicites : le résultat ne dépend pas
// du signe par défaut de `char` sur la plateforme (/J).
uint32_t NpkArchive::HashName(const std::string& name) {
    uint32_t a = 1, b = 0; // ecx = 1 @0x708A36 ; edi = 0 @0x708A37
    for (char ch : name) {
        if (ch == '\0') break; // `test al,al` @0x708A39/@0x708A6F : le binaire s'arrête au NUL
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
        TS2_ERR("NPK : ouverture impossible : %s", path.c_str());
        return false;
    }
    try {
        ByteReader r(data_);
        if (!(r.PeekMagic("NPK!", 4) || r.PeekMagic("NPAK", 4)))
            throw AssetError("magic NPK invalide");
        r.Skip(4);
        version_    = r.U32();
        entryCount_ = r.U32();
        dirOff_     = r.U32();
        dataOff_    = r.U32();
        if (version_ < 21) throw AssetError("version NPK non supportee"); // Err_SetLast(-255) @0x6FD11C
        if (version_ >= 23) r.U32(); // hdrExtra (non validé) — @0x6FD163

        // Npk_ParseDirectory 0x6FD04C @0x6FD178 : sous la version 24 le binaire ne lit AUCUN
        // répertoire contigu — il se place à dirOff+base (@0x6FD2F7) puis boucle entrée par
        // entrée (Pak_ReadAndDecrypt de 28 o avec a9=0/a10=0, donc variante XTEA sans queue,
        // puis lecture du nom @0x6FD3E5 + Pak_StrDup @0x6FD410) ; et sous la version 22 le
        // record diffère encore (FILETIME converti par Filetime_ToUnixSeconds 0x70879E
        // @0x6FD384). Aucune archive livrée n'est concernée (GXDEffect.npk = v27) : on rejette
        // franchement plutôt que de mal parser en silence avec le chemin contigu.
        // TODO [Npk_ParseDirectory 0x6FD04C @0x6FD2F7-0x6FD429] : chemin par-entrée non
        // implémenté — aucun asset ne permet de le prouver, donc non deviné.
        if (version_ < 24)
            throw AssetError("version NPK < 24 : repertoire par-entree non implemente");

        // Table de répertoire, déchiffrée XTEA. Sélection des variantes = arguments a9/a10 de
        // Pak_ReadAndDecrypt @0x6FD1FC : tail = version >= 25, variant = !(version >= 26).
        const bool tail    = version_ >= 25;
        const bool variant = !(version_ >= 26); // >=26 => XTEA standard (BlockStd)
        size_t start = dirOff_;

        // Le SEUIL de la longueur du répertoire est 27, PAS 24 (@0x6FD181) :
        //   version >= 27 : garde fileSize < dataOff -> Err_SetLast(-52) (@0x6FD188), puis
        //                   longueur = dataOff - dirOff (@0x6FD191) — disposition
        //                   [en-tête][répertoire][données] ;
        //   24 <= version < 27 : lseek(dirOff + base) puis longueur = fileSize - dirOff
        //                   (@0x6FD1A8) — disposition [en-tête][données][répertoire].
        // (`base` = archive+1116 : nul pour une archive sur fichier ; seul Npk_OpenFromMemory
        //  0x6FD453 le renseignerait, et il a 0 xref — code mort dans le binaire.)
        if (size_t(dirOff_) > data_.size())
            throw AssetError("dirOff hors fichier"); // longueur négative -> Crt_Malloc échoue -> Err_SetLast(-100) @0x6FD1C7
        size_t length;
        if (version_ >= 27) {
            if (size_t(dataOff_) > data_.size())
                throw AssetError("dataOff hors fichier"); // Err_SetLast(-52) @0x6FD188
            if (dataOff_ < dirOff_)
                throw AssetError("dataOff avant dirOff"); // idem : v21 négatif -> Err_SetLast(-100) @0x6FD1C7
            length = size_t(dataOff_) - dirOff_;
        } else {
            length = data_.size() - dirOff_;
        }
        if (start + length > data_.size()) throw AssetError("table repertoire hors fichier");

        std::vector<uint8_t> dir(data_.begin() + start, data_.begin() + start + length);
        XteaDecryptBuffer(dir.data(), dir.size(), key_, tail, variant);

        ByteReader dr(dir);
        entries_.reserve(entryCount_);
        for (uint32_t i = 0; i < entryCount_; ++i) {
            NpkEntry e;
            e.offset   = dr.U32();
            // Npk_ParseDirectory 0x6FD04C @0x6FD259 : pour version < 27 les données PRÉCÈDENT
            // le répertoire, donc toute entrée pointant dans/au-delà de celui-ci est corrompue
            // -> LABEL_46 @0x6FD434 Err_SetLast(-50). Garde délibérément DÉSACTIVÉ en v27, où
            // la disposition s'inverse (GXDEffect.npk : dirOff=0x18 < dataOff=0x1EC).
            if (version_ < 27 && e.offset >= dirOff_)
                throw AssetError("offset d'entree dans le repertoire");
            e.stored   = dr.U32();
            e.raw      = dr.U32();
            e.flags    = dr.U32();
            e.filetime = dr.U64();
            e.nameLen  = dr.U16();
            e.reserved = dr.U16();
            e.name     = dr.Str(e.nameLen);
            entries_.push_back(std::move(e));

            // ResMgr_AddEntry 0x708C87 : le binaire hache TOUTE entrée insérée (@0x708CD5) et
            // le fait sur le nom STOCKÉ TEL QUEL (v5 = a2[8] @0x708CC6) — jamais normalisé —
            // puis chaîne en queue du seau (@0x708CFC/@0x708D05), d'où l'ordre d'insertion.
            buckets_[HashName(entries_.back().name)].push_back(
                static_cast<uint32_t>(entries_.size() - 1));
            // a1[277] = 1 dès 257 entrées (@0x708D17) : c'est ce drapeau qui bascule
            // Npk_FindEntryByName des seaux au balayage linéaire.
            if (entries_.size() >= kBucketCount) useBuckets_ = true;
        }
        return true;
    } catch (const std::exception& ex) {
        TS2_ERR("NPK : parse echoue (%s) : %s", ex.what(), path.c_str());
        entries_.clear();
        buckets_.assign(kBucketCount, std::vector<uint32_t>());
        useBuckets_ = false;
        return false;
    }
}

std::vector<uint8_t> NpkArchive::Read(const NpkEntry& e) const {
    if (size_t(e.offset) + e.stored > data_.size())
        throw AssetError("blob NPK hors fichier : " + e.name);
    std::vector<uint8_t> blob(data_.begin() + e.offset, data_.begin() + e.offset + e.stored);
    const bool tail = version_ >= 25;

    // 1) variante XTEA AVANT décompression si (0x100 && 0x100000)
    if ((e.flags & kNpkX2) && (e.flags & kNpkX2Pre))
        XteaDecryptBuffer(blob.data(), blob.size(), key_, tail, /*variant*/ true);
    // 2) XTEA standard si 0x10
    if (e.flags & kNpkXtea)
        XteaDecryptBuffer(blob.data(), blob.size(), key_, tail, /*variant*/ false);
    // 3) décompression zlib si 0x1000 (seuil rawSize >= 0x100)
    if (e.flags & kNpkZlib) {
        if (e.raw >= 0x100) {
            blob = Zlib::Instance().InflateTo(blob.data(), blob.size(), e.raw);
        }
        // sinon : copie directe (déjà dans blob)
    }
    // 4) variante XTEA APRÈS décompression si (0x100 && !0x100000)
    // Npk_ReadEntryData 0x6FD746 @0x6FD8CE : la longueur passée est a1[2] = rawSize (et NON la
    // taille stockée), et le drapeau de queue est 0 en dur. Après l'inflate ci-dessus
    // blob.size() == e.raw : les deux coïncident. Sans kNpkZlib en revanche, le binaire
    // déchiffre `raw` octets d'un tampon dest raw-sized alors que seuls `stored` octets ont été
    // lus — cas où raw == stored en pratique (rien à décompresser). On borne donc au tampon
    // réel pour ne jamais déborder.
    // TODO [Npk_ReadEntryData 0x6FD746 @0x6FD8CE] : le cas raw > stored SANS kNpkZlib n'est
    // porté par aucun asset livré (les 12 entrées de GXDEffect.npk sont flags=0x100000 seul) ;
    // faute de preuve du contenu attendu au-delà de `stored`, non reproduit.
    if ((e.flags & kNpkX2) && !(e.flags & kNpkX2Pre)) {
        const size_t n = (size_t(e.raw) < blob.size()) ? size_t(e.raw) : blob.size();
        XteaDecryptBuffer(blob.data(), n, key_, /*tail*/ false, /*variant*/ true);
    }

    return blob;
}

const NpkEntry* NpkArchive::Find(const std::string& name) const {
    // Npk_FindEntryByName 0x6FD5E1 : le binaire NORMALISE d'abord le nom DEMANDÉ dans un
    // tampon de 512 o — Path_NormalizeSlashes(a2, v5, 0x200) @0x6FD623 — qui rejette (-20)
    // tout nom de longueur >= 512 (Crt_Strlen >= a3 @0x708A97) et remplace « \ » (92) par
    // « / » (47) (@0x708AC2/@0x708AC4).
    // ⚠ Le nom STOCKÉ n'est JAMAIS normalisé : ResMgr_AddEntry 0x708C87 hache le nom brut
    // (@0x708CC6) et la comparaison est Crt_Stricmp(stocké_brut, demandé_normalisé)
    // (@0x6FD65B/@0x6FD694). Normaliser le stocké ferait trouver ici des entrées que le
    // binaire est incapable de trouver — donc on ne le fait pas.
    if (name.size() >= 512) return nullptr; // Err_SetLast(-20) @0x708A97
    std::string q = name;
    for (char& c : q)
        if (c == '\\') c = '/'; // @0x708AC2/@0x708AC4

    // Comparaison insensible à la casse (Crt_Stricmp 0x76668B).
    const auto stricmpEq = [](const std::string& lhs, const std::string& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (size_t i = 0; i < rhs.size(); ++i)
            if (std::tolower((unsigned char)lhs[i]) != std::tolower((unsigned char)rhs[i]))
                return false;
        return true;
    };

    if (useBuckets_) {
        // Chemin à seaux (a1[277] != 0) : v3 = a1[ResMgr_HashName(v5) + 20] @0x6FD63B, puis
        // parcours de la chaîne du seau (i = *v3 ; i = *(i+56)) @0x6FD644.
        for (uint32_t idx : buckets_[HashName(q)])
            if (stricmpEq(entries_[idx].name, q)) return &entries_[idx]; // @0x6FD65B
    } else {
        // Chemin linéaire (a1[277] == 0) : parcours de la liste d'insertion globale
        // (i = a1[11] ; i = *(i+48)) @0x6FD67C. C'est le chemin que prend le binaire pour
        // GXDEffect.npk (12 entrées < 257, cf. @0x708D17).
        for (const auto& e : entries_)
            if (stricmpEq(e.name, q)) return &e; // @0x6FD694
    }
    return nullptr; // Err_SetLast(-10) @0x6FD666
}

std::vector<uint8_t> NpkArchive::Read(const std::string& name) const {
    const NpkEntry* e = Find(name);
    if (!e) throw AssetError("entree NPK absente : " + name);
    return Read(*e);
}

} // namespace ts2::asset

// Game/GameDatabase.cpp — chargement des tables de donnees .IMG (fidele aux chargeurs
// 0x4C2680/0x4C3930/0x4C4BC0/0x4C62A0/0x4C7390).
//
// Algorithme commun (identique dans les 5 chargeurs du binaire) :
//   1. lire le fichier ; decoder l'enveloppe [rawSize][packedSize][zlib] -> payload
//   2. count = *(u32*)payload ^ magic
//   3. garde d'integrite : count DOIT valoir la constante attendue en dur (sinon echec)
//   4. copier count*stride octets a partir de payload+header dans DataTable.data
// La table SOCKET a ~9.4 Ko de padding en fin de payload : on ne copie que count*stride
// (le chargeur d'origine alloue exactement 0xECCC = 3031*20 et ignore le reste).
#include "Game/GameDatabase.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>
#include <string>

namespace ts2::game {

namespace {

// Descripteur d'une table (une ligne = un chargeur d'origine).
struct TableSpec {
    const char* file;    // nom du .IMG
    uint32_t    magic;   // cle XOR du compteur
    uint32_t    count;   // compteur attendu (garde d'integrite en dur)
    uint32_t    header;  // offset des enregistrements dans le payload
    uint32_t    stride;  // taille d'un enregistrement
    const char* label;   // nom logique / embarque (journalisation + audit)
    DataTable GameDatabases::* member; // membre cible dans g_World.db
};

// Ordre et constantes preleves dans le desassemblage (Docs/TS2_IMG_FORMAT.md 4.1).
// magic/count/header/stride = VERITE IDA (jamais transposes de VeryOld). Cross-check classes
// VeryOldClient (noms de classe seulement, Docs/TS2_TABLES_ROSETTA.md §1) ; ATTENTION: MISNOMERS IDB :
// MobDb_LoadImg=ITEM, ItemDefTbl_LoadImg=MONSTER, AnchorTbl_LoadImg=SOCKET (decalage d'un cran).
const TableSpec kTables[] = {
    // LevelTable_LoadImg 0x4C2680 -> mLEVEL 0x8E7208. ex-VeryOldClient: LEVEL/CLEVEL.cpp (CONFIRMED)
    { "005_00001.IMG", 0x0E31,   145,  34,  44, "LEVEL_INFO",   &GameDatabases::level   },
    // MobDb_LoadImg 0x4C3930 (misnomer) -> mITEM 0x8E71EC. ex-VeryOldClient: ITEM/CITEM.cpp (CONFIRMED)
    { "005_00002.IMG", 0x1CB3, 99999,  67, 436, "ITEM_INFO",    &GameDatabases::item    },
    // SkillGrowthTbl_LoadImg 0x4C4BC0. ex-VeryOldClient: SKILL/CSKILL.cpp (CONFIRMED)
    { "005_00003.IMG", 0x0C7E,   300,  84, 776, "SKILL_INFO",   &GameDatabases::skill   },
    // ItemDefTbl_LoadImg 0x4C62A0 (misnomer) -> mMONSTER 0x8E71FC. ex-VeryOldClient: MONSTER (CONFIRMED)
    { "005_00004.IMG", 0x1583, 10000,  88, 944, "MONSTER_INFO", &GameDatabases::monster },
    // AnchorTbl_LoadImg 0x4C7390 (misnomer) -> mSOCKET 0x8E71D0. ex-VeryOldClient: GSOCKET/CSOCKET.cpp
    // (CONFIRMED table ; ATTENTION: CONFLICT J-4 sur le count : IDA=3031 vs VeryOld 2891 valides/3000 fixes — IDA gagne).
    { "005_00010.IMG", 0x0FDB,  3031, 103,  20, "SOCKET_INFO",  &GameDatabases::socketT },
};

// Sous-dossier commun a toutes les tables (chemins en dur dans le binaire).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Charge une table dans `out`. Renvoie false et laisse `out` inchange en cas d'echec.
bool LoadOneTable(const std::string& gameDataDir, const TableSpec& s, DataTable& out) {
    const std::string path = Join(Join(gameDataDir, kTablesDir), s.file);

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("DB %s : .IMG illisible : %s", s.label, path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() < 4) {
        TS2_ERR("DB %s : payload trop court (%zu o)", s.label, payload.size());
        return false;
    }

    // count = premier dword XOR magic (cf. Crt_Memcpy(&v, hMem, 4); v ^ magic).
    // GAP-1 (XOR-magic garde du compteur, famille D) : cette garde vit DEJA au bon endroit —
    // dans le chargeur de table, JAMAIS dans Asset/ImgFile (qui s'arrete a l'enveloppe
    // [rawSize][packedSize][zlib]). 5 magics ici (0xE31/0x1CB3/0xC7E/0x1583/0xFDB), le 6e (NPC
    // 0x1022) dans ExtraDatabases.cpp. ATTENTION: magics VeryOld DIFFERENTS (build alt.) — NE PAS
    // transposer, NE PAS deplacer/reimplementer cette passe. Cf. Docs/TS2_TABLES_ROSETTA.md §11 GAP-1.
    uint32_t first = 0;
    std::memcpy(&first, payload.data(), 4);
    const uint32_t count = first ^ s.magic;

    // Garde d'integrite : le chargeur d'origine echoue si count != constante attendue.
    if (count != s.count) {
        TS2_ERR("DB %s : compteur invalide (%u, attendu %u) — table alteree ?",
                s.label, count, s.count);
        return false;
    }

    const size_t need = static_cast<size_t>(s.header)
                      + static_cast<size_t>(count) * s.stride;
    if (payload.size() < need) {
        TS2_ERR("DB %s : payload tronque (%zu < %zu o)", s.label, payload.size(), need);
        return false;
    }

    // Copie des enregistrements SEULS (count*stride octets depuis l'offset header).
    const uint8_t* rec = payload.data() + s.header;
    out.data.assign(rec, rec + static_cast<size_t>(count) * s.stride);
    out.count  = count;
    out.stride = s.stride;

    // Audit IMG-TRUTH : le nom embarque doit correspondre au label attendu.
    const std::string& embedded = img.TableName();
    if (!embedded.empty() && embedded != s.label)
        TS2_WARN("DB %s : nom embarque different = '%s'", s.label, embedded.c_str());

    TS2_LOG("DB %s : %u enregistrements x %u o", s.label, out.count, out.stride);
    return true;
}

} // namespace

bool LoadGameDatabases(const std::string& gameDataDir) {
    bool allOk = true;
    for (const TableSpec& s : kTables) {
        DataTable& t = g_World.db.*(s.member);
        if (!LoadOneTable(gameDataDir, s, t))
            allOk = false; // on tente quand meme les autres tables
    }
    return allOk;
}

const LevelInfo* GetLevelInfo(int level) {
    if (level < 1) return nullptr;
    const uint8_t* r = g_World.db.level.record(static_cast<uint32_t>(level - 1));
    return reinterpret_cast<const LevelInfo*>(r); // nullptr si hors bornes
}

// ---------------------------------------------------------------------------
// GetRebirthExpSpan — sous-table EXP de renaissance (mLEVEL 0x8E7208 + 0x18EC).
// Voir GameDatabase.h pour la preuve complete (int32 et non float : fidiv @0x67A64F).
//
// Table recopiee a l'octet depuis maybe_LevelTable_InitFloats 0x4C2380 (disasm relu cette
// mission), immediats des 12 `mov dword ptr [reg+off], imm32` :
//   tier  off      imm32 (hex)   valeur int32
//    1   0x18EC    39589228h      962105896   [EA 0x4c238a]
//    2   0x18F0    3BA3CB33h     1000590131   [EA 0x4c2397]
//    3   0x18F4    3E068168h     1040613736   [EA 0x4c23a4]
//    4   0x18F8    4081A54Dh     1082238285   [EA 0x4c23b1]
//    5   0x18FC    43163108h     1125527816   [EA 0x4c23be]
//    6   0x1900    45C528C0h     1170548928   [EA 0x4c23cb]
//    7   0x1904    488F9B05h     1217370885   [EA 0x4c23d8]
//    8   0x1908    4B76A138h     1266065720   [EA 0x4c23e5]
//    9   0x190C    4E7B5FFCh     1316708348   [EA 0x4c23f2]
//   10   0x1910    519F07A9h     1369376681   [EA 0x4c23ff]
//   11   0x1914    54E2D4C4h     1424151748   [EA 0x4c240c]
//   12   0x1918    58481079h     1481117817   [EA 0x4c2419]
// ---------------------------------------------------------------------------
int32_t GetRebirthExpSpan(int tier) {
    // Garde de l'accesseur 0x4C2BF0 : hors [1..12] -> 0 (@0x4c2bf7 jl / @0x4c2c01 jle,
    // branche loc_4C2C03 `xor eax, eax`).
    if (tier < 1 || tier > 12) return 0;
    // Indice 1-based (le binaire lit [this + 4*tier + 0x18E8], soit +0x18EC pour tier=1).
    static const int32_t kRebirthExpSpan[13] = {
        0,           // [0] inutilise (le binaire n'y accede jamais : garde tier>=1)
        962105896,   // tier 1  — 39589228h
        1000590131,  // tier 2  — 3BA3CB33h
        1040613736,  // tier 3  — 3E068168h
        1082238285,  // tier 4  — 4081A54Dh
        1125527816,  // tier 5  — 43163108h
        1170548928,  // tier 6  — 45C528C0h
        1217370885,  // tier 7  — 488F9B05h
        1266065720,  // tier 8  — 4B76A138h
        1316708348,  // tier 9  — 4E7B5FFCh
        1369376681,  // tier 10 — 519F07A9h
        1424151748,  // tier 11 — 54E2D4C4h
        1481117817,  // tier 12 — 58481079h
    };
    return kRebirthExpSpan[tier];
}

const ItemInfo* GetItemInfo(uint32_t itemId) {
    if (itemId < 1) return nullptr;
    const uint8_t* r = g_World.db.item.record(itemId - 1);
    const ItemInfo* it = reinterpret_cast<const ItemInfo*>(r);
    // Slot vide (itemId == 0) => introuvable, comme MobDb_GetEntry 0x4C3C00.
    return (it && it->itemId != 0) ? it : nullptr;
}

const SkillInfo* GetSkillInfo(uint32_t skillId) {
    if (skillId < 1) return nullptr;
    const uint8_t* r = g_World.db.skill.record(skillId - 1);
    const SkillInfo* sk = reinterpret_cast<const SkillInfo*>(r);
    // Slot vide (skillId == 0) => introuvable, comme SkillGrowthTbl_GetRecord 0x4C4E90
    // (`if (*(base+776*(id-1))) return ... ; return 0;` — teste le 1er dword du record).
    return (sk && sk->skillId != 0) ? sk : nullptr;
}

const MonsterInfo* GetMonsterInfo(uint32_t monsterId) {
    if (monsterId < 1) return nullptr;                          // ItemDefTbl_GetRecord 0x4C6570 : a2<1 => 0
    const uint8_t* r = g_World.db.monster.record(monsterId - 1); // base+944*(id-1) ; record() gere id>count
    const MonsterInfo* mi = reinterpret_cast<const MonsterInfo*>(r);
    // Slot vide (id==0) => introuvable, comme la garde 1er dword de 0x4C6570
    // (`if (*(base+944*(id-1))) return ... ; return 0;`).
    return (mi && mi->id != 0) ? mi : nullptr;
}

} // namespace ts2::game

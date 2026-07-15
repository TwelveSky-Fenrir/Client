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
const TableSpec kTables[] = {
    { "005_00001.IMG", 0x0E31,   145,  34,  44, "LEVEL_INFO",   &GameDatabases::level   },
    { "005_00002.IMG", 0x1CB3, 99999,  67, 436, "ITEM_INFO",    &GameDatabases::item    },
    { "005_00003.IMG", 0x0C7E,   300,  84, 776, "SKILL_INFO",   &GameDatabases::skill   },
    { "005_00004.IMG", 0x1583, 10000,  88, 944, "MONSTER_INFO", &GameDatabases::monster },
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

const ItemInfo* GetItemInfo(uint32_t itemId) {
    if (itemId < 1) return nullptr;
    const uint8_t* r = g_World.db.item.record(itemId - 1);
    const ItemInfo* it = reinterpret_cast<const ItemInfo*>(r);
    // Slot vide (itemId == 0) => introuvable, comme MobDb_GetEntry 0x4C3C00.
    return (it && it->itemId != 0) ? it : nullptr;
}

} // namespace ts2::game

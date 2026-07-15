// Game/ExtraDatabases.cpp — chargement des 2 tables .IMG supplementaires, fidele aux chargeurs
// 0x4C6BD0 ("SkillDefTbl_LoadImg", en verite mNPC) et 0x4C8090 ("NpcTbl_LoadImg", en verite mQUEST).
//
// Algorithme (identique aux 5 tables de GameDatabase.cpp, a une difference pres — cf. ci-dessous) :
//   1. lire le fichier (CreateFileA/ReadFile bruts dans l'original, ici via ImgFile) ; decoder
//      l'enveloppe [rawSize][packedSize][zlib] -> payload (Asset_DecompressImg 0x53F5E0)
//   2. count = *(u32*)payload (^ magic si la table en utilise un)
//   3. garde d'integrite : count DOIT valoir la constante attendue en dur
//   4. copier count*stride octets a partir de payload+4 (PAS d'offset "header" avec nom embarque
//      ici, contrairement aux 5 tables — les 2 chargeurs memcpy des `hMem+4` directement)
//
// Difference avec 005_00005 (mNPC) vs 005_00006 (mQUEST) : la premiere XOR le compteur avec le
// magic 0x1022 (`v9 ^ 0x1022 == 500`), la seconde compare le compteur BRUT sans magic
// (`v10 == 1000`) — represente ici par magic=0 (XOR par 0 = identite).
#include "Game/ExtraDatabases.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>
#include <string>

namespace ts2::game {

namespace {

// Descripteur d'une table (une ligne = un chargeur d'origine).
struct ExtraTableSpec {
    const char* file;    // nom du .IMG
    uint32_t    magic;   // cle XOR du compteur (0 = aucune, cf. 005_00006)
    uint32_t    count;   // compteur attendu (garde d'integrite en dur)
    uint32_t    stride;  // taille d'un enregistrement
    const char* label;   // nom logique (journalisation)
    DataTable ExtraDatabases::* member; // membre cible dans g_ExtraDb
};

// header=4 pour les 2 tables (pas de nom embarque, contrairement aux 5 tables de GameDatabase.cpp).
constexpr uint32_t kHeader = 4;

// EAs d'origine : SkillDefTbl_LoadImg 0x4C6BD0 (magic 0x1022, count 500, stride 11736) ->
// en verite le manager "mNPC" (rec0 "Blacksmith Wu", [Error::mNPC.Init()]).
// NpcTbl_LoadImg 0x4C8090 (pas de magic, count 1000, stride 8444) -> en verite le manager
// "mQUEST" (rec0 "[Intro] Banker Bai & Beggar Xiao", [Error::mQUEST.Init()]).
const ExtraTableSpec kExtraTables[] = {
    { "005_00005.IMG", 0x1022,  500, 11736, "NPC_DEF (mNPC)",     &ExtraDatabases::npc   },
    { "005_00006.IMG", 0x0000, 1000,  8444, "QUEST_DEF (mQUEST)", &ExtraDatabases::quest },
};

// Sous-dossier commun (identique a GameDatabase.cpp — chemin en dur dans le binaire).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Charge une table dans `out`. Renvoie false et laisse `out` inchange en cas d'echec.
bool LoadOneExtraTable(const std::string& gameDataDir, const ExtraTableSpec& s, DataTable& out) {
    const std::string path = Join(Join(gameDataDir, kTablesDir), s.file);

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("ExtraDB %s : .IMG illisible : %s", s.label, path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() < 4) {
        TS2_ERR("ExtraDB %s : payload trop court (%zu o)", s.label, payload.size());
        return false;
    }

    // count = premier dword (^ magic si applicable ; magic=0 pour 005_00006 = pas de XOR).
    uint32_t first = 0;
    std::memcpy(&first, payload.data(), 4);
    const uint32_t count = first ^ s.magic;

    // Garde d'integrite : le chargeur d'origine echoue si count != constante attendue.
    if (count != s.count) {
        TS2_ERR("ExtraDB %s : compteur invalide (%u, attendu %u) — table alteree ?",
                s.label, count, s.count);
        return false;
    }

    const size_t need = static_cast<size_t>(kHeader)
                      + static_cast<size_t>(count) * s.stride;
    if (payload.size() < need) {
        TS2_ERR("ExtraDB %s : payload tronque (%zu < %zu o)", s.label, payload.size(), need);
        return false;
    }

    // Copie des enregistrements SEULS (count*stride octets depuis l'offset 4 — pas de nom
    // embarque a sauter ici, cf. commentaire d'en-tete).
    const uint8_t* rec = payload.data() + kHeader;
    out.data.assign(rec, rec + static_cast<size_t>(count) * s.stride);
    out.count  = count;
    out.stride = s.stride;

    TS2_LOG("ExtraDB %s : %u enregistrements x %u o", s.label, out.count, out.stride);
    return true;
}

} // namespace

bool LoadExtraDatabases(const std::string& gameDataDir) {
    bool allOk = true;
    for (const ExtraTableSpec& s : kExtraTables) {
        DataTable& t = g_ExtraDb.*(s.member);
        if (!LoadOneExtraTable(gameDataDir, s, t))
            allOk = false; // on tente quand meme l'autre table
    }
    return allOk;
}

const NpcDefRecord* GetNpcDefRecord(uint32_t npcId) {
    if (npcId < 1) return nullptr;
    const uint8_t* r = g_ExtraDb.npc.record(npcId - 1);
    const NpcDefRecord* rec = reinterpret_cast<const NpcDefRecord*>(r);
    // Slot vide (id == 0), meme semantique que NpcDefTbl_ValidateRecord 0x4C65F0.
    return (rec && rec->id != 0) ? rec : nullptr;
}

const QuestDefRecord* GetQuestDefRecord(uint32_t questId) {
    if (questId < 1) return nullptr;
    const uint8_t* r = g_ExtraDb.quest.record(questId - 1);
    const QuestDefRecord* rec = reinterpret_cast<const QuestDefRecord*>(r);
    // Slot vide (name == ""), semantique DIFFERENTE de NpcDefRecord — cf. QuestDefTbl_ValidateRecord
    // 0x4C78C0 qui teste `Crt_Strcmp(name, "") == 0` plutot que id==0.
    return (rec && rec->name[0] != '\0') ? rec : nullptr;
}

} // namespace ts2::game

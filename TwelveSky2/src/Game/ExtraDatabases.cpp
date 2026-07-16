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
// Cross-check VeryOldClient (noms de classe seulement, Docs/TS2_TABLES_ROSETTA.md §6/§7) :
// mNPC = classe NPC (CNPC.cpp), mQUEST = classe QUEST (CQUEST.cpp). Le magic NPC 0x1022 est le
// 6e magic de garde famille D (split : 5 dans GameDatabase.cpp, ce 6e ici) — cf. GAP-1 §11.
const ExtraTableSpec kExtraTables[] = {
    { "005_00005.IMG", 0x1022,  500, 11736, "NPC_DEF (mNPC)",     &ExtraDatabases::npc   }, // ex-VeryOldClient: NPC/CNPC.cpp (CONFIRMED)
    { "005_00006.IMG", 0x0000, 1000,  8444, "QUEST_DEF (mQUEST)", &ExtraDatabases::quest }, // ex-VeryOldClient: QUEST/CQUEST.cpp (CONFIRMED) ; magic=0 (count brut, accord VeryOld xorKey 0x0)
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
    // GAP-1 (XOR-magic garde du compteur) : garde au bon endroit (chargeur de table, PAS
    // Asset/ImgFile). magic 0x1022 = NPC (VeryOld xorKey DIFFERENT, non transpose) ; magic 0 = QUEST
    // (count brut). NE PAS deplacer/reimplementer cette passe. Cf. Docs/TS2_TABLES_ROSETTA.md §11.
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

// NpcTbl_FindByTypeAndId 0x4C8340 — transcription litterale (cf. ExtraDatabases.h pour la
// preuve complete). `this` = mQUEST 0x8E71E4 {count @+0, records @+4} == g_ExtraDb.quest.
//
// FIDELITE — points a NE PAS « ameliorer » :
//   * l'ordre des 3 conditions est celui du binaire (nom, puis +56, puis +60) ;
//   * le `+1` porte sur l'ARGUMENT (`add edx, 1` @0x4C839E), jamais sur le champ ;
//   * AUCUNE garde de nullite/table-chargee : le binaire n'en a pas. Sur une table vide
//     (count == 0) la boucle `i < *this` @0x4C8361 ne s'execute pas et la fonction renvoie 0
//     (`xor eax, eax` @0x4C83D2) — c'est deja le comportement naturel ici, inutile d'ajouter
//     un test que l'original ne fait pas.
const QuestDefRecord* FindQuestDefByElementAndId(int element0, int questId) {
    const DataTable& t = g_ExtraDb.quest;
    for (uint32_t i = 0; i < t.count; ++i) {                       // cmp edx,[ecx] @0x4C8361
        // imul eax, 20FCh @0x4C836D -> stride 8444 (== sizeof(QuestDefRecord), static_assert).
        const uint8_t* raw = t.record(i);
        if (!raw) break;
        const QuestDefRecord* rec = reinterpret_cast<const QuestDefRecord*>(raw);

        // (a) Crt_Strcmp(rec+4, "") != 0 @0x4C837E : nom NON vide. La chaine poussee
        //     @0x4C8365 est String 0x7EC95F = le 2e NUL de "re.DAT\0\0" (0x7EC958), donc "".
        //     strcmp(x, "") != 0 <=> x[0] != '\0'.
        if (rec->name[0] == '\0') continue;                        // jnz/jmp @0x4C8388-0x4C838A

        // (b) rec[56] == element0 + 1 @0x4C83A1 (le +1 est sur l'argument @0x4C839E).
        if (rec->fieldA != static_cast<uint32_t>(element0 + 1)) continue;  // jnz @0x4C83A5

        // (c) rec[60] == questId @0x4C83BA.
        if (rec->fieldB != static_cast<uint32_t>(questId)) continue;       // jnz @0x4C83BD

        return rec;                                               // base + 8444*i @0x4C83CE
    }
    return nullptr;                                               // xor eax,eax @0x4C83D2
}

} // namespace ts2::game

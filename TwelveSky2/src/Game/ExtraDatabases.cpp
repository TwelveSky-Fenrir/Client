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
    // hasTR : le chargeur teste-t-il `cmp ds:g_UseTRVariant, 1` (global 0x1669190) ?
    // Les DEUX chargeurs de ce fichier le font : SkillDefTbl_LoadImg @0x4C6BD9 (005_00005)
    // et NpcTbl_LoadImg @0x4C8099 (005_00006). Chaines TR 0x7A715C / 0x7A71E0.
    bool        hasTR;
    // Validateur d'enregistrement du binaire (nullptr = non transcrit) — meme signature que
    // celle des 5 tables de GameDatabase.cpp et de QuestTbl_ValidateRecord (QuestSystem.cpp:74).
    bool (*validate)(const DataTable&, int);
};

// ---------------------------------------------------------------------------
// Lecteurs bruts d'enregistrement (duplicat local de ceux de GameDatabase.cpp — chaque
// fichier a son propre namespace anonyme, comme Join/kTablesDir deja dupliques ici).
// `memcpy` obligatoire : les strides 11736/8444 ne garantissent aucun alignement.
// ---------------------------------------------------------------------------
inline int32_t RecI32(const uint8_t* r, size_t off) {
    int32_t v; std::memcpy(&v, r + off, 4); return v;
}
inline uint32_t RecU32(const uint8_t* r, size_t off) {
    uint32_t v; std::memcpy(&v, r + off, 4); return v;
}
// Motif `for (i = 0; i < N && rec[off+i]; ++i); if (i == N) return 0;` : echoue si AUCUN
// octet nul dans [off, off+maxLen).
inline bool RecHasNul(const uint8_t* r, size_t off, size_t maxLen) {
    for (size_t i = 0; i < maxLen; ++i)
        if (r[off + i] == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Validateurs d'enregistrement — transcription littérale des 2 validateurs du binaire.
// ---------------------------------------------------------------------------

// SkillDefTbl_ValidateRecord 0x4C65F0 (misnomer IDB : valide un PNJ) — NpcDefRecord, 11736 o.
bool ValidateNpcDef(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // Accept anticipe « slot vide » @0x4c6608 : id == 0 => VALIDE (table creuse : 500 slots).
    if (RecU32(r, 0) == 0) return true;
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 500) return false;             /*0x4c6643*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c6664*/
    if (!RecHasNul(r, 4, 25)) return false;                               /*0x4c666d name[25]*/
    if (RecI32(r, 32) < 1 || RecI32(r, 32) > 5) return false;             /*0x4c66dd fieldA*/
    // Grille 5x5 de chaines de 51 o @+36 : `51*k + m + 255*j + 36` @0x4c6716 -> textGrid[5][5][51].
    for (size_t j = 0; j < 5; ++j)
        for (size_t k = 0; k < 5; ++k)
            if (!RecHasNul(r, 36 + 255 * j + 51 * k, 51)) return false;
    if (RecI32(r, 1312) < 1 || RecI32(r, 1312) > 5) return false;         /*0x4c67a5 fieldB*/
    if (RecI32(r, 1316) < 1 || RecI32(r, 1316) > 17) return false;        /*0x4c67de fieldC*/
    if (RecI32(r, 1320) < 1 || RecI32(r, 1320) > 10000) return false;     /*0x4c681a fieldD*/
    // fieldE (+1324) : le VALIDATEUR borne a [1,10000] — la borne [1,66] citee dans
    // ExtraDatabases.h vient d'un CONSOMMATEUR different (Model_GetNpcMeshSlot). On
    // transcrit ici le validateur, pas le consommateur.
    if (RecI32(r, 1324) < 1 || RecI32(r, 1324) > 10000) return false;     /*0x4c6856*/
    for (size_t n = 0; n < 3; ++n)                                        /*0x4c685f fieldF[3]*/
        if (RecI32(r, 1328 + 4 * n) < 1 || RecI32(r, 1328 + 4 * n) > 1000) return false;
    for (size_t i = 0; i < 100; ++i)                                      /*0x4c68bb fieldG[100]*/
        if (RecI32(r, 1340 + 4 * i) < 1 || RecI32(r, 1340 + 4 * i) > 2) return false;
    for (size_t j = 0; j < 3; ++j)                                        /*0x4c6914 shopItemIds[3][28]*/
        for (size_t k = 0; k < 28; ++k)
            if (RecU32(r, 1740 + 112 * j + 4 * k) >= 0x186A0u) return false;
    for (size_t j = 0; j < 3; ++j)                                        /*0x4c69a3 teachSkillIds[3][8]*/
        for (size_t k = 0; k < 8; ++k)
            if (RecU32(r, 2076 + 32 * j + 4 * k) > 0x12Cu) return false;
    for (size_t a = 0; a < 3; ++a)                                        /*0x4c6a32 skillMatrix[3][3][3][8]*/
        for (size_t b = 0; b < 3; ++b)
            for (size_t c = 0; c < 3; ++c)
                for (size_t d = 0; d < 8; ++d)
                    if (RecU32(r, 2172 + 288 * a + 96 * b + 32 * c + 4 * d) > 0x12Cu) return false;
    for (size_t l = 0; l < 145; ++l)                                      /*0x4c6b34 levelCostTable[145][15]*/
        for (size_t s = 0; s < 15; ++s)
            if (RecU32(r, 3036 + 60 * l + 4 * s) > 0x5F5E100u) return false;
    return true;                                                          /*0x4c6bc8*/
}

// NpcTbl_ValidateRecord 0x4C78C0 (misnomer IDB : valide une QUETE) — QuestDefRecord, 8444 o.
bool ValidateQuestDef(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // Accept anticipe « slot vide » @0x4c78e2 : `Crt_Strcmp(rec + 4, "") == 0 => return 1`.
    // La chaine poussee est String 0x7EC95F = "" ; strcmp(x, "") == 0 <=> x[0] == '\0'.
    // SEMANTIQUE DIFFERENTE des 6 autres tables : c'est le NOM VIDE qui marque le slot libre,
    // PAS id == 0 (cf. le bandeau de ExtraDatabases.h et GetQuestDefRecord).
    if (r[4] == 0) return true;
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 1000) return false;            /*0x4c7923*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c7944*/
    if (!RecHasNul(r, 4, 51)) return false;                               /*0x4c794d name[51]*/
    if (RecI32(r, 56) < 1 || RecI32(r, 56) > 4) return false;             /*0x4c79bd fieldA*/
    if (RecI32(r, 60) < 1 || RecI32(r, 60) > 1000) return false;          /*0x4c79f3 fieldB*/
    if (RecI32(r, 64) < 1 || RecI32(r, 64) > 145) return false;           /*0x4c7a29 levelReq*/
    if (RecI32(r, 68) < 1 || RecI32(r, 68) > 2) return false;             /*0x4c7a5c fieldD*/
    if (RecI32(r, 72) < 1 || RecI32(r, 72) > 8) return false;             /*0x4c7a8f fieldE*/
    if (RecU32(r, 76) > 0xC8u) return false;                              /*0x4c7aac fieldF*/
    // NB : +80..+91 ne sont couverts par AUCUNE garde (cf. _unk80[12] dans le header).
    if (RecI32(r, 92) < 1 || RecI32(r, 92) > 500) return false;           /*0x4c7afb fieldG*/
    for (size_t j = 0; j < 5; ++j)                                        /*0x4c7b04 fieldH[5]*/
        if (RecU32(r, 96 + 4 * j) > 0x1F4u) return false;
    if (RecI32(r, 116) < 1 || RecI32(r, 116) > 500) return false;         /*0x4c7b87 fieldI*/
    // NB : +120/+124 (objectiveTarget/objectiveRequired) et +128 ne sont PAS valides non plus.
    for (size_t k = 0; k < 3; ++k) {                                      /*0x4c7b90 rewards[3]*/
        if (RecI32(r, 136 + 8 * k) < 1 || RecI32(r, 136 + 8 * k) > 6) return false;
        if (RecU32(r, 140 + 8 * k) > 0x5F5E100u) return false;
    }
    if (RecU32(r, 160) > 0x3E8u) return false;                            /*0x4c7c49 fieldK*/
    // 10 blocs de dialogue de 15 lignes de 51 o. Le binaire deroule 10 boucles distinctes aux
    // bases 164, 992, 1820, 2648, 3476, 4304, 5132, 5960, 6788, 7616 (@0x4c7c6e .. @0x4c801c) :
    // pas constant de 828 o = 15*51 + 63, ce qui confirme le `_tail[63]` non valide par bloc.
    for (size_t b = 0; b < 10; ++b)
        for (size_t i = 0; i < 15; ++i)
            if (!RecHasNul(r, 164 + 828 * b + 51 * i, 51)) return false;
    return true;                                                          /*0x4c8087*/
}

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
    // hasTR=true : cmp @0x4C6BD9 -> "G03_GDATA\D01_GIMAGE2D\005\TR\005_00005.IMG" (0x7A715C).
    { "005_00005.IMG", 0x1022,  500, 11736, "NPC_DEF (mNPC)",     &ExtraDatabases::npc,   true, &ValidateNpcDef   }, // ex-VeryOldClient: NPC/CNPC.cpp (CONFIRMED)
    // hasTR=true : cmp @0x4C8099 -> "G03_GDATA\D01_GIMAGE2D\005\TR\005_00006.IMG" (0x7A71E0).
    { "005_00006.IMG", 0x0000, 1000,  8444, "QUEST_DEF (mQUEST)", &ExtraDatabases::quest, true, &ValidateQuestDef }, // ex-VeryOldClient: QUEST/CQUEST.cpp (CONFIRMED) ; magic=0 (count brut, accord VeryOld xorKey 0x0)
};

// Sous-dossier commun (identique a GameDatabase.cpp — chemin en dur dans le binaire).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Charge une table dans `out`. Renvoie false en cas d'echec.
// `useTR` = etat de g_UseTRVariant 0x1669190 ; les 2 tables de ce fichier ont une branche TR.
// NB : en cas d'echec de la BOUCLE DE VALIDATION, `out` a deja ete peuple — fidele au binaire,
// qui publie count/records AVANT d'entrer dans la boucle (cf. @0x4c64ea/@0x4c64f5 pour le
// chargeur modele). Seuls les echecs anterieurs laissent `out` inchange.
bool LoadOneExtraTable(const std::string& gameDataDir, const ExtraTableSpec& s, DataTable& out,
                       bool useTR) {
    // Variante TR — modele SkillDefTbl_LoadImg 0x4C6BD0 (`cmp ds:g_UseTRVariant, 1` @0x4C6BD9)
    // et NpcTbl_LoadImg 0x4C8090 (@0x4C8099) : chemin ...\005\TR\<file> si le flag vaut 1.
    const std::string file = (useTR && s.hasTR) ? std::string("TR\\") + s.file
                                                : std::string(s.file);
    const std::string path = Join(Join(gameDataDir, kTablesDir), file);

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

    // Boucle de validation EN BLOC — motif @0x4C64F5..0x4C6527 du chargeur modele :
    //   `for (i = 0; i < *this; ++i) if (!ValidateRecord(this, i)) return 0;  return 1;`
    // Present dans les 8 chargeurs du binaire, dont les 2 d'ici (-> 0x4C65F0 / 0x4C78C0).
    // Un seul enregistrement invalide rejette la table EN BLOC et avorte App_Init 0x461C20
    // sur « [Error::mNPC.Init()] » / « [Error::mQUEST.Init()] ».
    // IMPERATIF : apres l'affectation de out.count/out.stride (les validateurs lisent record(i)).
    if (s.validate) {
        for (uint32_t i = 0; i < out.count; ++i) {
            if (!s.validate(out, static_cast<int>(i))) {
                TS2_ERR("ExtraDB %s : enregistrement %u invalide — table rejetee", s.label, i);
                return false;
            }
        }
    }

    TS2_LOG("ExtraDB %s : %u enregistrements x %u o", s.label, out.count, out.stride);
    return true;
}

} // namespace

bool LoadExtraDatabases(const std::string& gameDataDir, bool useTR) {
    bool allOk = true;
    for (const ExtraTableSpec& s : kExtraTables) {
        DataTable& t = g_ExtraDb.*(s.member);
        if (!LoadOneExtraTable(gameDataDir, s, t, useTR))
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

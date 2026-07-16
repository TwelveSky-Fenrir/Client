// Game/QuestSystem.cpp — implementation du systeme de quetes (ts2::game).
// Transcription fidele du desassemblage de TwelveSky2.exe. Cf. QuestSystem.h pour les EAs.
#include "Game/QuestSystem.h"
#include "Game/ExtraDatabases.h" // g_ExtraDb.quest + FindQuestDefByElementAndId (NpcTbl_FindByTypeAndId 0x4C8340)
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>
#include <vector>

namespace ts2::game {
namespace {

// Sous-dossier commun aux tables .IMG 005_* (meme convention que GameDatabase.cpp).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";
const char kQuestFile[] = "005_00007.IMG";

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Comparaison de chaines C insensible a la casse, bornee (mirroir Crt_Stricmp 0x76668B
// applique a des buffers non necessairement termines dans les 13 o du champ QuestInfo).
bool StriEqualBounded(const char* a, const char* b, size_t maxLen) {
    size_t i = 0;
    for (; i < maxLen; ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 'a' + 'A');
        if (ca != cb) return false;
        if (ca == 0) return true; // les deux se terminent ici
    }
    return true;
}

// Override de test uniquement (nullptr = resolution directe sur la vraie table — cf. le
// bandeau (B) de QuestSystem.h : le binaire n'a AUCUNE indirection ici).
QuestStepLookup g_stepLookup = nullptr;
const QuestStepRecord* g_curStepRecord = nullptr;

// Cache de projection QuestDefRecord (8444 o) -> QuestStepRecord (vue des seuls champs
// consommes). UNE entree par ligne de g_ExtraDb.quest, indexee par le MEME index de ligne
// que le binaire : c'est le miroir de `base + 8444*i` @0x4C83CE, donc un pointeur STABLE et
// UNIQUE par ligne. Indispensable : Quest_GetRewardItemId/IsRewardItemActive re-resolvent
// pendant qu'ApplyQuestInteractResultState tient deja un `npc` — un buffer statique partage
// les ferait aliaser.
std::vector<QuestStepRecord> g_stepCache;

// Projette une ligne mQUEST sur la vue QuestStepRecord. Offsets prouves par decompilation
// croisee (Quest_CheckObjectiveState 0x50FF10, Quest_GetObjectiveResult 0x510520,
// Quest_GetRewardItemId 0x510A10, Pkt_SmithUpgradeResult 0x48E7D0).
void ProjectQuestStep(const QuestDefRecord& def, QuestStepRecord& out) {
    out.field64  = def.levelReq;           // +64  porte de niveau (@0x50FF86 `cmp g_SelfLevel, [rec+0x40]`)
    out.category = def.fieldE;             // +72  type d'objectif 1..8 (@0x50FFFC switch ; @0x48E929 -> g_QuestObjType)
    out.field92  = def.fieldG;             // +92  v10[23] (@0x510520)
    out.field96  = def.fieldH[0];          // +96  v10[24] (@0x510520)
    out.field116 = def.fieldI;             // +116 v10[29] (@0x510520)
    out.targetId = def.objectiveTarget;    // +120 (@0x510012 etc. ; @0x48E881 -> item id g_InvMain)
    out.required = def.objectiveRequired;  // +124 (@0x51002A `progress >= [v10+124]`)
    for (int i = 0; i < 3; ++i) {          // +136 + 8*i / +140 + 8*i (@0x510A62 / @0x510A72)
        out.reward[i].type  = def.rewards[i].category;
        out.reward[i].value = def.rewards[i].value;
    }
}

} // namespace

// ===========================================================================
// (A) QuestTbl_* — table QUEST_INFO (005_00007.IMG).
// ===========================================================================

bool QuestTbl_ValidateRecord(const DataTable& table, int row0) {
    const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(row0));
    if (!r) return false; // ligne hors bornes (l'original ne boucle jamais hors [0,count))

    if (r->id == 0) return true; // slot vide -> valide

    if (r->id < 1 || r->id > 999) return false;
    if (r->id != static_cast<uint32_t>(row0 + 1)) return false;

    for (int i = 0; i < 5; ++i) {
        int j = 0;
        for (; j < 13 && r->name[i][j] != '\0'; ++j) {}
        if (j == 13) return false; // pas de NUL trouve dans les 13 o
    }

    if (r->group < 1 || r->group > 4) return false;
    if (r->stage < 1 || r->stage > 145) return false;
    return (r->value >= 1 && r->value <= 999);
}

bool LoadQuestTable(const std::string& gameDataDir) {
    const std::string path = JoinPath(JoinPath(gameDataDir, kTablesDir), kQuestFile);

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("Quest : .IMG illisible : %s", path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() < 4) {
        TS2_ERR("Quest : payload trop court (%zu o)", payload.size());
        return false;
    }

    // count = premier dword, SANS XOR (a la difference des 5 tables de GameDatabase.cpp).
    uint32_t count = 0;
    std::memcpy(&count, payload.data(), 4);

    // Garde d'integrite en dur (QuestTbl_LoadImg 0x4C8630 : `if (v10 == 999)`).
    if (count != 999) {
        TS2_ERR("Quest : compteur invalide (%u, attendu 999)", count);
        return false;
    }

    constexpr uint32_t kStride = 84;
    constexpr uint32_t kHeader = 4; // les enregistrements suivent directement le compteur
    const size_t need = static_cast<size_t>(kHeader) + static_cast<size_t>(count) * kStride;
    if (payload.size() < need) {
        TS2_ERR("Quest : payload tronque (%zu < %zu o)", payload.size(), need);
        return false;
    }

    const uint8_t* rec = payload.data() + kHeader;
    g_QuestTable.data.assign(rec, rec + static_cast<size_t>(count) * kStride);
    g_QuestTable.count  = count;
    g_QuestTable.stride = kStride;

    TS2_LOG("Quest : %u enregistrements x %u o", g_QuestTable.count, g_QuestTable.stride);

    // Validation ligne par ligne (fidele : arret des la premiere ligne invalide, table
    // laissee peuplee comme l'original — pas de rollback).
    for (uint32_t i = 0; i < g_QuestTable.count; ++i) {
        if (!QuestTbl_ValidateRecord(g_QuestTable, static_cast<int>(i))) {
            TS2_ERR("Quest : ligne %u invalide", i);
            return false;
        }
    }
    return true;
}

const QuestInfo* QuestTbl_GetRecord(const DataTable& table, int id1based) {
    if (id1based < 1 || static_cast<uint32_t>(id1based) > table.count) return nullptr;
    const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(id1based - 1));
    if (!r || r->id == 0) return nullptr;
    return r;
}

const QuestInfo* QuestTbl_FindByGroupAndName(const DataTable& table, int group0, const char* name) {
    if (!name) return nullptr;
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (!r || r->id == 0) continue;
        if (static_cast<int>(r->group) - 1 != group0) continue;
        for (int j = 0; j < 5; ++j) {
            if (StriEqualBounded(r->name[j], name, 13)) return r;
        }
    }
    return nullptr;
}

int QuestTbl_FindFirstByGroup(const DataTable& table, int group0, int flagA3, int flagA4) {
    if (flagA3 != 1 || flagA4 != 0) return 0;
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (!r || r->id == 0) continue;
        if (static_cast<int>(r->group) - 1 != group0) continue;
        if (r->stage != 1) continue;
        return static_cast<int>(r->id);
    }
    return 0;
}

int QuestTbl_FindByGroupAndStage(const DataTable& table, int group0, int stage) {
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (!r || r->id == 0) continue;
        if (static_cast<int>(r->group) - 1 != group0) continue;
        if (static_cast<int>(r->stage) != stage) continue;
        return static_cast<int>(r->id);
    }
    return 0;
}

int QuestTbl_CountByGroup(const DataTable& table, int group0) {
    int n = 0;
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0) ++n;
    }
    return n;
}

int QuestTbl_CountByGroupUpTo(const DataTable& table, int group0, int rowLimit) {
    int n = 0;
    for (int i = 0; i < rowLimit; ++i) {
        const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(i));
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0) ++n;
    }
    return n;
}

int QuestTbl_FindFirstOfGroup(const DataTable& table, int group0) {
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0)
            return static_cast<int>(r->id);
    }
    return 0;
}

int QuestTbl_FindPrevOfGroup(const DataTable& table, int group0, int fromId1based) {
    for (int i = fromId1based - 2; i >= 0; --i) {
        const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(i));
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0)
            return static_cast<int>(r->id);
    }
    return fromId1based; // fidele : renvoie l'argument inchange si rien trouve
}

int QuestTbl_FindNextOfGroup(const DataTable& table, int group0, int fromId1based) {
    // Fidele : le binaire boucle `for (i = fromId1based; i < count; ++i)` sans garder
    // contre un fromId1based negatif (UB memoire en C). On clamp a 0 par securite ; en
    // usage normal fromId1based est toujours >= 1 (id ou -1 jamais passe ici).
    for (uint32_t i = static_cast<uint32_t>(fromId1based < 0 ? 0 : fromId1based); i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0)
            return static_cast<int>(r->id);
    }
    return fromId1based; // fidele : renvoie l'argument inchange si rien trouve
}

// ===========================================================================
// (B) Etape de quete courante.
// ===========================================================================

void SetQuestStepLookup(QuestStepLookup fn) { g_stepLookup = fn; }

// LookupQuestStep — miroir de `NpcTbl_FindByTypeAndId(mQUEST, element0, questId)` 0x4C8340,
// appele EN DUR par le binaire (EA 0x50FF65, 0x50FFCA, 0x510A37, 0x510AB7, 0x664A67,
// 0x510E40, 0x510ECC). `zoneId` = element local 0-based (nom historique trompeur, cf.
// QuestSystem.h). Aucune garde de table-chargee : sur une table vide le scan de
// FindQuestDefByElementAndId ne trouve rien et renvoie nullptr — exactement le `xor eax,eax`
// @0x4C83D2 du binaire.
const QuestStepRecord* LookupQuestStep(int zoneId, int npcQuestId) {
    if (g_stepLookup) return g_stepLookup(zoneId, npcQuestId); // override de test

    const QuestDefRecord* def = FindQuestDefByElementAndId(zoneId, npcQuestId);
    if (!def) return nullptr;

    // Index de ligne == celui du binaire : (rec - base) / 8444 (imul stride @0x4C836D).
    const DataTable& t = g_ExtraDb.quest;
    const uint8_t* base = t.data.data();
    const size_t row = static_cast<size_t>(reinterpret_cast<const uint8_t*>(def) - base)
                     / static_cast<size_t>(t.stride);

    if (g_stepCache.size() != t.count) g_stepCache.assign(t.count, QuestStepRecord{});
    if (row >= g_stepCache.size()) return nullptr; // inatteignable (def vient de la table)

    ProjectQuestStep(*def, g_stepCache[row]);
    return &g_stepCache[row];
}

// CurrentQuestStepRecord — miroir de g_pCurQuestStepRecord 0x18231B4 (record de l'etape de
// quete COURANTE), RESOLU EN DIRECT. Le binaire LIT ce global (Pkt_SmithUpgradeResult 0x48E7D0 :
// 20 xrefs @0x18231B4, TOUTES des lectures, aucun store) mais ne l'ecrit jamais statiquement.
// Le jeu TOURNE, donc a l'execution ce pointeur n'est PAS nul quand les case 1/2/3/4 le
// deferencent : sa valeur est le record de l'etape courante. Equivalence prouvee
// g_pCurQuestStepRecord == NpcTbl_FindByTypeAndId(mQUEST, g_LocalElement, g_CurQuestId) : dans le
// case 2, la boucle de recompenses lit g_pCurQuestStepRecord+8*i+136 (@0x48EB0E) et
// Quest_GetRewardItemId 0x510A10 relit Find(element, g_CurQuestId) @0x510A37 pour la MEME
// recompense ecrite en inventaire -> les deux designent forcement le meme record. On resout donc
// via LookupQuestStep (comme les 7 autres consommateurs) au lieu de renvoyer g_curStepRecord
// qu'AUCUN code n'ecrit (renvoyait nullptr a vie => etat des case 1/2/3/4 d'ApplyQuestInteract-
// ResultState ET garde de visibilite du QuestTracker UI/QuestTrackerWindow.cpp:48 = CODE MORT).
// Ce n'est PAS fabriquer un ecrivain a g_curStepRecord : resolution a la demande, fidele a la
// lecture du pointeur. element = g_World.self.element (== g_LocalElement 0x1673194) ; questId =
// g_QuestProgress.npcQuestId (== g_CurQuestId 0x16745F4). npcQuestId==0 (aucune quete) =>
// Find(element,0) => nullptr => consommateurs masques (gating correct).
const QuestStepRecord* CurrentQuestStepRecord() {
    if (g_curStepRecord) return g_curStepRecord; // override de test (prioritaire)
    return LookupQuestStep(static_cast<int>(g_World.self.element), g_QuestProgress.npcQuestId);
}
void SetCurrentQuestStepRecord(const QuestStepRecord* record) { g_curStepRecord = record; }

namespace {
// Mirroir des boucles `for (i<2) for (j<64)` de Quest_CheckObjectiveState (EA 0x510058/
// 0x510070 et jumelles) : `*(this + 384*i + 6*j + 10320) == cible`. L'octet 10320*4 = 0xA140
// depuis g_PlayerCmdController 0x1669170 donne 0x16732B0 = g_InvMain -> c'est la GRILLE
// D'INVENTAIRE (row 0 = sac, row 1 = page bonus), pas une table de suivi dediee. Le binaire
// lit un global ; on lit donc g_Client.inv, son miroir etabli (ClientRuntime.h).
bool InvRows01Contains(uint32_t value) {
    for (uint32_t row = 0; row < 2; ++row)                       // for (i = 0; i < 2; ++i)
        for (uint32_t col = 0; col < InventoryState::kCols; ++col) // for (j = 0; j < 64; ++j)
            if (g_Client.inv.At(row, col).itemId == value) return true;
    return false;
}
} // namespace

int Quest_CheckObjectiveState(const QuestProgressState& s) {
    if (s.objectiveMode == 0 && s.objectiveType == 0 && s.objectiveTarget == 0 && s.objectiveProgress == 0) {
        // Branche « implicite » = PORTE DE NIVEAU sur la quete SUIVANTE (npcQuestId + 1).
        // EA 0x50FF6A : Find(mQUEST, element, questId+1) ; EA 0x50FF80/0x50FF86 :
        // `mov ecx,[edx+0xA038]` (= g_SelfLevel 0x16731A8) puis `cmp ecx,[eax+0x40]` / `jge`.
        const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId + 1);
        return (npc && g_World.self.level >= static_cast<int>(npc->field64)) ? 1 : 0;
    }
    if (s.objectiveMode != 1) return 0;

    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return 0;

    switch (s.objectiveType) {
        case 1:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= static_cast<int>(npc->required)) ? 3 : 2;
        case 2:
        case 3:
        case 4:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return InvRows01Contains(npc->targetId) ? 3 : 2;
        case 5:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1) ? 3 : 2;
        case 6:
            if (s.objectiveTarget == 1) {
                if (s.objectiveProgress != static_cast<int>(npc->targetId)) return 0;
                return InvRows01Contains(npc->targetId) ? 3 : 2;
            }
            if (s.objectiveTarget == 2) {
                if (s.objectiveProgress != static_cast<int>(npc->required)) return 0;
                return InvRows01Contains(npc->required) ? 5 : 4;
            }
            return 0;
        case 7:
            return (s.objectiveTarget == static_cast<int>(npc->targetId)) ? 2 : 0;
        case 8:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1) ? 3 : 2;
        default:
            return 0;
    }
}

bool Quest_IsObjectiveComplete(const QuestProgressState& s) {
    switch (s.objectiveType) {
        case 1: case 2: case 3: case 4: case 5: case 8:
            return Quest_CheckObjectiveState(s) == 3;
        case 6:
            return Quest_CheckObjectiveState(s) == 5;
        case 7:
            return Quest_CheckObjectiveState(s) == 2;
        default:
            return false;
    }
}

int Quest_GetObjectiveResult(const QuestProgressState& s) {
    if (s.objectiveMode == 0 && s.objectiveType == 0 && s.objectiveTarget == 0 && s.objectiveProgress == 0) {
        // Meme porte de niveau que Quest_CheckObjectiveState (g_SelfLevel vs rec+64).
        const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId + 1);
        if (!npc) return 0;
        return (g_World.self.level >= static_cast<int>(npc->field64)) ? static_cast<int>(npc->field92) : 0;
    }
    if (s.objectiveMode != 1) return 0;

    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return 0;

    switch (s.objectiveType) {
        case 1:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= static_cast<int>(npc->required))
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 2:
        case 3:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return InvRows01Contains(npc->targetId)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 4:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return InvRows01Contains(npc->targetId)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field96);
        case 5:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 6:
            if (s.objectiveTarget == 1) {
                if (s.objectiveProgress != static_cast<int>(npc->targetId)) return 0;
                return InvRows01Contains(npc->targetId)
                           ? static_cast<int>(npc->field96) : static_cast<int>(npc->field92);
            }
            if (s.objectiveTarget == 2) {
                if (s.objectiveProgress != static_cast<int>(npc->required)) return 0;
                return InvRows01Contains(npc->required)
                           ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
            }
            return 0;
        case 7:
            return (s.objectiveTarget == static_cast<int>(npc->targetId)) ? static_cast<int>(npc->field116) : 0;
        case 8:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        default:
            return 0;
    }
}

int Quest_GetRewardItemId(const QuestProgressState& s) {
    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return 0;
    for (const auto& r : npc->reward)
        if (r.type == 6) return static_cast<int>(r.value);
    return 0;
}

bool Quest_IsRewardItemActive(const QuestProgressState& s) {
    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return false;
    for (const auto& r : npc->reward) {
        if (r.type != 6) continue;
        const ItemInfo* item = GetItemInfo(r.value);
        return item != nullptr && item->typeCode == 2;
    }
    return false;
}

bool Quest_IsItemAllowed(int containerIndex, int itemId) {
    if (containerIndex == 1 && g_Client.VarGet(0x1687494) != 1) return false;
    for (int i = 0; i < 14; ++i) {
        const uint32_t addr = 0x1675808u + 4u * (14u * static_cast<uint32_t>(containerIndex) + static_cast<uint32_t>(i));
        if (g_Client.VarGet(addr) == itemId) return true;
    }
    return false;
}

// ===========================================================================
// Callees directes de Pkt_SmithUpgradeResult — grille d'inventaire (g_InvMain 0x16732B0),
// lignes 0..1 (sac principal + page bonus).
// ===========================================================================

// Inventory_RemoveItem 0x510C40.
int Quest_RemoveTrackedItem(InventoryState& inv, uint32_t itemId) {
    for (int i = 0; i < 2; ++i) {                                   // for (i = 0; i < 2; ++i) @0x510C49
        for (uint32_t j = 0; j < InventoryState::kCols; ++j) {      // for (j = 0; j < 64; ++j) @0x510C65
            InvCell& c = inv.At(static_cast<uint32_t>(i), j);
            if (c.itemId == itemId) {                               // @0x510CA0
                c = InvCell{}; // les 6 dwords 10320..10325 remis a 0 (@0x510CBF..0x510D63)
                return i;                                           // @0x510D6E
            }
        }
    }
    return -1;                                                      // @0x510D7D
}

// Inventory_ReplaceItem 0x510B40.
int Quest_ReplaceTrackedItem(InventoryState& inv, uint32_t oldItemId, uint32_t newItemId) {
    for (int i = 0; i < 2; ++i) {                                   // @0x510B49
        for (uint32_t j = 0; j < InventoryState::kCols; ++j) {      // @0x510B65
            InvCell& c = inv.At(static_cast<uint32_t>(i), j);
            if (c.itemId == oldItemId) {                            // @0x510BA0
                c.itemId = newItemId;                               // +0  @0x510BC2
                // Fidele : l'original n'ecrit que 10323/10324/10325 ; gridX/gridY
                // (10321/10322) restent INCHANGES.
                c.flag = 0;                                         // +12 @0x510BDE
                c.color = 0;                                        // +16 @0x510BFF
                c.durability = 0;                                   // +20 @0x510C20
                return i;                                           // @0x510C2B
            }
        }
    }
    return -1;                                                      // @0x510C3A
}

// ===========================================================================
// Paquet 0x27 — partie ETAT.
// ===========================================================================

void ApplyQuestInteractResultState(const QuestInteractResultPacket& p,
                                    QuestProgressState& progress,
                                    InventoryState& inv) {
    const QuestStepRecord* npc = CurrentQuestStepRecord();

    // Le binaire lit l'element depuis le global g_LocalElement 0x1673194 (= base+0xA024) a
    // CHAQUE resolution ; notre `progress.zoneId` en est le miroir. On le resynchronise ici
    // sur sa source unique (g_World.self.element == g_LocalElement, cf. Net/NetClient.h:111)
    // : sans ca, il resterait 0 a vie et le resolveur composite renverrait la ligne de
    // l'element 1 pour 3 joueurs sur 4.
    progress.zoneId = static_cast<int>(g_World.self.element);

    switch (p.resultCode) {
    case 1: {
        g_Client.Var(0x1675B08) = 0; // g_GmCmdCooldownLatch
        if (p.invRow != -1 && npc) {
            inv.Set(static_cast<uint32_t>(p.invRow), p.invSlot, npc->targetId, p.gridX, p.gridY,
                    /*count*/0, /*durability*/0, /*serial*/0);
        }
        // Etat de quete : ecrit dans progress.* (= la MEME memoire que les globals
        // 0x16745F4..0x1674604 du binaire, cf. QuestProgressState). L'ancienne version
        // ecrivait dans g_Client.Var(0x16745F4..) — une 2e representation que PERSONNE ne
        // relisait (les consommateurs lisent tous g_QuestProgress) : l'etat etait ecrit puis
        // perdu. Une seule representation desormais.
        progress.npcQuestId += 1;    // g_CurQuestId 0x16745F4 @0x48E911/0x48E914
        progress.objectiveMode = 1;  // g_QuestObjMode 0x16745F8 @0x48E91A
        if (npc) {
            progress.objectiveType = static_cast<int>(npc->category); // g_QuestObjType 0x16745FC @0x48E929
            // @0x48E938 / @0x48E95E : deux tests `cmp [rec+0x48], 6` independants.
            if (npc->category == 6) {
                progress.objectiveTarget = 1;                                   // @0x48E94E
                progress.objectiveProgress = static_cast<int>(npc->targetId);   // @0x48E975/0x48E978
            } else {
                progress.objectiveTarget = static_cast<int>(npc->targetId);     // @0x48E943/0x48E946
                progress.objectiveProgress = 0;                                 // @0x48E964
            }
        }
        // TODO(state) hors perimetre : Snd3D_PlayScaledVolume 0x4DA380, UI_EventNoticeWnd_Open(1) 0x6649F0.
        break;
    }
    case 2: {
        g_Client.Var(0x1675B08) = 0;
        if (p.invRow != -1) {
            const int rewardItemId = Quest_GetRewardItemId(progress);
            const bool active = Quest_IsRewardItemActive(progress);
            inv.Set(static_cast<uint32_t>(p.invRow), p.invSlot, static_cast<uint32_t>(rewardItemId),
                    p.gridX, p.gridY, active ? 1u : 0u, /*durability*/0, /*serial*/0);
            g_Client.Var(0x18398F8) = p.invRow; // dword_18398F8
            // TODO(state) hors perimetre : cGameHud_ResetUiState 0x62AFB0.
        }
        if (npc) {
            for (const auto& r : npc->reward) {
                switch (r.type) {
                case 2: // poids (g_InvWeight)
                    if (!Quest_SumExceeds2Billion(inv.weight, r.value)) {
                        inv.weight += r.value;
                        g_Client.msg.System(Str(428));
                    }
                    break;
                case 3: // monnaie (g_Currency)
                    inv.currency += r.value;
                    g_Client.msg.System(Str(429));
                    break;
                case 4:
                    g_Client.msg.System(Str(430));
                    break;
                case 5: // dword_16746A4 (longue traine)
                    g_Client.Var(0x16746A4) += static_cast<int32_t>(r.value);
                    g_Client.msg.System(Str(431));
                    break;
                case 6: // item — deja ecrit dans la grille ci-dessus
                    g_Client.msg.System(Str(427));
                    break;
                default:
                    break;
                }
            }
            switch (progress.objectiveType) { // g_QuestObjType (persiste depuis un case 1 precedent)
            case 2: case 3: case 4:
                Quest_RemoveTrackedItem(inv, npc->targetId);
                break;
            case 6:
                Quest_RemoveTrackedItem(inv, npc->required);
                break;
            default:
                break;
            }
        }
        progress.objectiveMode = 0;
        progress.objectiveType = 0;
        progress.objectiveTarget = 0;
        progress.objectiveProgress = 0;
        g_Client.Var(0x1675AE8) = 0;
        g_Client.VarF(0x1675AEC) = g_World.gameTimeSec - 590.0f;
        break;
    }
    case 3: {
        g_Client.Var(0x1675B08) = 0;
        // Fidele : PAS de garde invRow!=-1 ici dans le binaire (a la difference des cases
        // 1/2/4) — ecriture inconditionnelle. On clamp via InventoryState::At (evite l'UB
        // memoire si invRow==-1, le binaire aurait lu/ecrit hors-tableau).
        if (npc) {
            inv.Set(static_cast<uint32_t>(p.invRow), p.invSlot, npc->targetId, p.gridX, p.gridY,
                    0, 0, 0);
        }
        g_Client.Var(0x18398F8) = p.invRow;
        // TODO(state) hors perimetre : cGameHud_ResetUiState 0x62AFB0.
        break;
    }
    case 4: {
        g_Client.Var(0x1675B08) = 0;
        int32_t replaceResult = -1;
        if (npc) replaceResult = static_cast<int32_t>(Quest_ReplaceTrackedItem(inv, npc->targetId, npc->required));
        if (!replaceResult || (replaceResult == 1 && g_Client.VarGet(0x16732A8) > 0)) { // g_Inv_ExtraPageCount
            g_Client.Var(0x18398F8) = replaceResult;
            // TODO(state) hors perimetre : cGameHud_ResetUiState 0x62AFB0.
        }
        progress.objectiveTarget = 2;                                              // g_QuestObjParam1 0x1674600
        if (npc) progress.objectiveProgress = static_cast<int>(npc->required);     // g_QuestObjParam2 0x1674604
        break;
    }
    case 5: {
        // TODO(state) hors perimetre : UI_EventNoticeWnd_Open(2) 0x6649F0.
        progress.objectiveMode = 0;
        progress.objectiveType = 0;
        progress.objectiveTarget = 0;
        progress.objectiveProgress = 0;
        g_Client.Var(0x1675AE8) = 0;
        g_Client.VarF(0x1675AEC) = g_World.gameTimeSec - 590.0f;
        break;
    }
    case 6:
    case 8:
    case 9:
        progress.objectiveProgress += 1; // g_QuestObjParam2 0x1674604
        break;
    case 7:
    default:
        break; // rien a faire (case 7 : message uniquement, deja gere ailleurs)
    }
}

} // namespace ts2::game

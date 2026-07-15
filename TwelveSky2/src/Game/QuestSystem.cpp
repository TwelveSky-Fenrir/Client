// Game/QuestSystem.cpp — implementation du systeme de quetes (ts2::game).
// Transcription fidele du desassemblage de TwelveSky2.exe. Cf. QuestSystem.h pour les EAs.
#include "Game/QuestSystem.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>

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

// Resolveur d'etape courant injectable (cf. QuestSystem.h — hors perimetre : vraie table
// NPC mQUEST non modelisee).
QuestStepLookup g_stepLookup = nullptr;
const QuestStepRecord* g_curStepRecord = nullptr;

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
const QuestStepRecord* LookupQuestStep(int zoneId, int npcQuestId) {
    return g_stepLookup ? g_stepLookup(zoneId, npcQuestId) : nullptr;
}

const QuestStepRecord* CurrentQuestStepRecord() { return g_curStepRecord; }
void SetCurrentQuestStepRecord(const QuestStepRecord* record) { g_curStepRecord = record; }

namespace {
// Scan des 2x64 slots de suivi (mirroir des boucles i<2 / j<64 du binaire).
bool KillTrackContains(const QuestProgressState& s, uint32_t value) {
    for (const auto& block : s.killTrack)
        for (const auto& slot : block)
            if (slot.id == value) return true;
    return false;
}
} // namespace

int Quest_CheckObjectiveState(const QuestProgressState& s) {
    if (s.objectiveMode == 0 && s.objectiveType == 0 && s.objectiveTarget == 0 && s.objectiveProgress == 0) {
        const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId + 1);
        return (npc && s.totalKillCount >= static_cast<int>(npc->field64)) ? 1 : 0;
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
            return KillTrackContains(s, npc->targetId) ? 3 : 2;
        case 5:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1) ? 3 : 2;
        case 6:
            if (s.objectiveTarget == 1) {
                if (s.objectiveProgress != static_cast<int>(npc->targetId)) return 0;
                return KillTrackContains(s, npc->targetId) ? 3 : 2;
            }
            if (s.objectiveTarget == 2) {
                if (s.objectiveProgress != static_cast<int>(npc->required)) return 0;
                return KillTrackContains(s, npc->required) ? 5 : 4;
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
        const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId + 1);
        if (!npc) return 0;
        return (s.totalKillCount >= static_cast<int>(npc->field64)) ? static_cast<int>(npc->field92) : 0;
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
            return KillTrackContains(s, npc->targetId)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 4:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return KillTrackContains(s, npc->targetId)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field96);
        case 5:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 6:
            if (s.objectiveTarget == 1) {
                if (s.objectiveProgress != static_cast<int>(npc->targetId)) return 0;
                return KillTrackContains(s, npc->targetId)
                           ? static_cast<int>(npc->field96) : static_cast<int>(npc->field92);
            }
            if (s.objectiveTarget == 2) {
                if (s.objectiveProgress != static_cast<int>(npc->required)) return 0;
                return KillTrackContains(s, npc->required)
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
// Callees directes de Pkt_SmithUpgradeResult (killTrack).
// ===========================================================================

int Quest_RemoveTrackedItem(QuestProgressState& s, uint32_t itemId) {
    for (int i = 0; i < 2; ++i) {
        auto& block = s.killTrack[static_cast<size_t>(i)];
        for (auto& slot : block) {
            if (slot.id == itemId) {
                slot = QuestKillTrackSlot{};
                return i;
            }
        }
    }
    return -1;
}

int Quest_ReplaceTrackedItem(QuestProgressState& s, uint32_t oldItemId, uint32_t newItemId) {
    for (int i = 0; i < 2; ++i) {
        auto& block = s.killTrack[static_cast<size_t>(i)];
        for (auto& slot : block) {
            if (slot.id == oldItemId) {
                slot.id = newItemId;
                slot.aux3 = 0; slot.aux4 = 0; slot.aux5 = 0; // fidele : +12/+16/+20 remis a 0 (aux1/aux2 = +4/+8 non touches)
                return i;
            }
        }
    }
    return -1;
}

// ===========================================================================
// Paquet 0x27 — partie ETAT.
// ===========================================================================

void ApplyQuestInteractResultState(const QuestInteractResultPacket& p,
                                    QuestProgressState& progress,
                                    InventoryState& inv) {
    const QuestStepRecord* npc = CurrentQuestStepRecord();

    switch (p.resultCode) {
    case 1: {
        g_Client.Var(0x1675B08) = 0; // g_GmCmdCooldownLatch
        if (p.invRow != -1 && npc) {
            inv.Set(static_cast<uint32_t>(p.invRow), p.invSlot, npc->targetId, p.gridX, p.gridY,
                    /*count*/0, /*durability*/0, /*serial*/0);
        }
        g_Client.Var(0x16745F4) += 1;      // g_CurQuestId
        g_Client.Var(0x16745F8) = 1;       // g_QuestObjMode
        if (npc) {
            g_Client.Var(0x16745FC) = static_cast<int32_t>(npc->category); // g_QuestObjType
            if (npc->category == 6) {
                g_Client.Var(0x1674600) = 1;                                   // g_QuestObjParam1
                g_Client.Var(0x1674604) = static_cast<int32_t>(npc->targetId); // g_QuestObjParam2
            } else {
                g_Client.Var(0x1674600) = static_cast<int32_t>(npc->targetId);
                g_Client.Var(0x1674604) = 0;
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
            switch (g_Client.VarGet(0x16745FC)) { // g_QuestObjType (persiste depuis un case 1 precedent)
            case 2: case 3: case 4:
                Quest_RemoveTrackedItem(progress, npc->targetId);
                break;
            case 6:
                Quest_RemoveTrackedItem(progress, npc->required);
                break;
            default:
                break;
            }
        }
        g_Client.Var(0x16745F8) = 0;
        g_Client.Var(0x16745FC) = 0;
        g_Client.Var(0x1674600) = 0;
        g_Client.Var(0x1674604) = 0;
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
        if (npc) replaceResult = static_cast<int32_t>(Quest_ReplaceTrackedItem(progress, npc->targetId, npc->required));
        if (!replaceResult || (replaceResult == 1 && g_Client.VarGet(0x16732A8) > 0)) { // g_Inv_ExtraPageCount
            g_Client.Var(0x18398F8) = replaceResult;
            // TODO(state) hors perimetre : cGameHud_ResetUiState 0x62AFB0.
        }
        g_Client.Var(0x1674600) = 2;
        if (npc) g_Client.Var(0x1674604) = static_cast<int32_t>(npc->required);
        break;
    }
    case 5: {
        // TODO(state) hors perimetre : UI_EventNoticeWnd_Open(2) 0x6649F0.
        g_Client.Var(0x16745F8) = 0;
        g_Client.Var(0x16745FC) = 0;
        g_Client.Var(0x1674600) = 0;
        g_Client.Var(0x1674604) = 0;
        g_Client.Var(0x1675AE8) = 0;
        g_Client.VarF(0x1675AEC) = g_World.gameTimeSec - 590.0f;
        break;
    }
    case 6:
    case 8:
    case 9:
        g_Client.Var(0x1674604) += 1;
        break;
    case 7:
    default:
        break; // rien a faire (case 7 : message uniquement, deja gere ailleurs)
    }
}

} // namespace ts2::game

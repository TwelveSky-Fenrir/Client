// Gfx/ModelCache.cpp — implémentation. Voir ModelCache.h pour le pattern de chemin et
// les EAs d'origine (SObject_BuildPath 0x4D89C0, Model_LoadFile 0x40E700 via
// asset::SObject::Load, upload GPU via MeshRenderer::Upload).
#include "Gfx/ModelCache.h"
#include "Core/Log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ts2::gfx {

namespace {

// Même convention de jointure que Game/GameDatabase.cpp::Join (séparateur '\').
std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

} // namespace

int ResolveSObjectFolder(char prefix) {
    for (const SObjectCategory& c : kSObjectCategories)
        if (c.prefix == prefix) return c.folder;
    return -1;
}

std::string BuildSObjectPath(const std::string& gameDataDir, const std::string& stem) {
    if (stem.empty()) return {};
    const int folder = ResolveSObjectFolder(stem.front());
    if (folder < 0) return {};

    char folderName[8];
    std::snprintf(folderName, sizeof(folderName), "%03d", folder);

    std::string path = JoinPath(gameDataDir, "G03_GDATA\\D04_GSOBJECT");
    path = JoinPath(path, folderName);
    path = JoinPath(path, stem + ".SOBJECT");
    return path;
}

std::string BuildMonsterStem(int kindIndex, int variant, int sub) {
    if (kindIndex < 0 || kindIndex >= 333) return {};
    if (variant < 0 || variant >= 3) return {};
    const int subBound = (variant == 1) ? 4 : 1; // cf. bandeau ModelCache.h : variant 0/2 -> 1 seul sub.
    if (sub < 0 || sub >= subBound) return {};

    char buf[16];
    std::snprintf(buf, sizeof(buf), "M%03d%03d%03d", kindIndex + 1, variant + 1, sub + 1);
    return buf;
}

std::string BuildNpcStem(int kindIndex, int variant) {
    if (kindIndex < 0 || kindIndex >= 66) return {};
    if (variant < 0) return {};

    char buf[16];
    std::snprintf(buf, sizeof(buf), "N%03d%03d001", kindIndex + 1, variant + 1);
    return buf;
}

std::string BuildPlayerBodyStem(int race, int gender, int slot, int variant) {
    if (race < 0 || race >= 3) return {};
    if (gender < 0 || gender >= 2) return {};
    if (slot != 0 && slot != 1) return {};
    const int variantBound = (slot == 0) ? 7 : 3; // cf. ModelCache.h : SLOT0=7 var., SLOT1=3 var.
    if (variant < 0 || variant >= variantBound) return {};

    const int kindIndex = race + 3 * gender; // 0-based, ∈[0,6) -- formule prouvée §3ter du doc.
    const int slotToken = (slot == 0) ? 1 : 2; // constante de catalogue, PAS une donnee reseau.

    char buf[16];
    std::snprintf(buf, sizeof(buf), "C%03d%03d%03d", kindIndex + 1, slotToken, variant + 1);
    return buf;
}

ModelCache::ModelCache(MeshRenderer& renderer, std::string gameDataDir, size_t maxResident)
    : renderer_(renderer), gameDataDir_(std::move(gameDataDir)), maxResident_(maxResident) {}

const SkinnedModel* ModelCache::Get(const std::string& stem) {
    if (stem.empty()) return nullptr;

    auto found = entries_.find(stem);
    if (found != entries_.end()) {
        found->second.lastUseTick = ++tick_;
        return found->second.loadFailed ? nullptr : &found->second.model;
    }

    // Premier accès pour ce stem : résout le chemin, charge, uploade GPU.
    Entry entry;
    entry.lastUseTick = ++tick_;

    const std::string path = BuildSObjectPath(gameDataDir_, stem);
    if (path.empty()) {
        TS2_WARN("ModelCache: prefixe de modele inconnu pour '%s' (stem vide ou lettre "
                 "hors table kSObjectCategories)", stem.c_str());
        entry.loadFailed = true;
    } else {
        asset::SObject src;
        if (!src.Load(path)) {
            TS2_WARN("ModelCache: echec chargement '%s' (%s)", path.c_str(), src.error().c_str());
            entry.loadFailed = true;
        } else if (!renderer_.Upload(src, entry.model)) {
            TS2_WARN("ModelCache: echec upload GPU pour '%s'", path.c_str());
            entry.loadFailed = true;
        } else {
            TS2_LOG("ModelCache: '%s' charge et uploade (%zu mesh(es), resident=%zu/%zu)",
                    stem.c_str(), entry.model.meshes.size(), entries_.size() + 1, maxResident_);
        }
    }

    auto [ins, inserted] = entries_.emplace(stem, std::move(entry));
    (void)inserted; // toujours vrai ici (stem absent, verifie par find() ci-dessus)

    EvictIfNeeded();
    return ins->second.loadFailed ? nullptr : &ins->second.model;
}

const SkinnedModel* ModelCache::GetForItem(const game::ItemInfo& item, int slot) {
    if (slot < 0 || slot >= 3) return nullptr;

    const char* raw = item.model[slot];
    const size_t len = ::strnlen(raw, sizeof(item.model[slot]));
    if (len == 0) return nullptr; // slot de variante inutilise pour cet item

    return Get(std::string(raw, len));
}

const SkinnedModel* ModelCache::GetForMonsterKind(int kindIndex, int variant, int sub) {
    const std::string stem = BuildMonsterStem(kindIndex, variant, sub);
    if (stem.empty()) return nullptr;
    return Get(stem);
}

const SkinnedModel* ModelCache::GetForNpcKind(int kindIndex, int variant) {
    const std::string stem = BuildNpcStem(kindIndex, variant);
    if (stem.empty()) return nullptr;
    return Get(stem);
}

PlayerBodyModel ModelCache::GetForPlayerBody(int race, int gender, int costumeSlot0, int costumeSlot1) {
    PlayerBodyModel result;

    const std::string stem0 = BuildPlayerBodyStem(race, gender, 0, costumeSlot0);
    if (!stem0.empty()) result.slot0 = Get(stem0);

    const std::string stem1 = BuildPlayerBodyStem(race, gender, 1, costumeSlot1);
    if (!stem1.empty()) result.slot1 = Get(stem1);

    return result;
}

const SkinnedModel* ModelCache::GetForMonster(uint32_t monsterDefId) {
    // RÉSOLU (Docs/TS2_MONSTER_NPC_MODEL.md §2/§4) : kindIndex = MONSTER_INFO.field244 - 1.
    // Preuve : Char_Draw 0x5805C0 lit `*(DWORD*)(*(this+24) + 244) - 1` et le passe tel quel à
    // Model_GetNpcMotionSlot 0x4E5960, qui borne `a2` à [0, 0x14C]=[0,332] -- exactement les 333
    // modèles M*.SOBJECT catalogués par AssetMgr_InitAllSlots 0x4DEB50 (preuve arithmétique
    // d'adresse flt_FBBF3C/flt_FF67CC, §4 du doc). `this+24` == MONSTER_INFO* est prouvé par
    // Pkt_SpawnMonster 0x467B00 (ItemDefTbl_GetRecord stocké à l'offset dword 24 de l'entité).
    //
    // Indexation table : la mission demande explicitement `g_World.db.monster.record(monsterDefId)`
    // -- SANS -1 -- ce qui est cohérent avec Game/EntityManager.cpp::ResolveMobDef (même table,
    // même appel `t.record(id)` sans conversion, id = RdU32(body,0) reçu du réseau). On reproduit
    // cette même convention ici pour rester cohérent avec le reste de ClientSource (les deux
    // fonctions doivent accepter le même `monsterDefId` brut pour un même monstre).
    const game::DataTable& table = game::g_World.db.monster;
    const uint8_t* rec = table.record(monsterDefId);
    if (!rec) return nullptr; // table non chargee OU monsterDefId hors bornes [0, count).

    uint32_t field244 = 0;
    std::memcpy(&field244, rec + 244, sizeof(field244)); // dword @+244, LE (hote == fichier).

    // Bornes prouvées §2 du doc : field244 (1-based, tel que stocké dans le fichier .IMG) doit
    // être dans [1, 333] pour désigner un modèle réel ; au-delà, Model_GetNpcMotionSlot retombe
    // silencieusement sur le fallback générique côté binaire d'origine -- ici on renvoie nullptr
    // (mis en cache comme un échec normal par GetForMonsterKind -> Get()).
    if (field244 < 1 || field244 > 333) {
        static bool warnedOutOfRange = false;
        if (!warnedOutOfRange) {
            warnedOutOfRange = true;
            TS2_WARN("ModelCache::GetForMonster: field244=%u hors bornes [1,333] (monsterDefId=%u) "
                     "-- fallback nullptr (comportement d'origine, cf. Model_GetNpcMotionSlot 0x4E5960)",
                     field244, monsterDefId);
        }
        return nullptr;
    }

    return GetForMonsterKind(static_cast<int>(field244) - 1, 0, 0);
}

const SkinnedModel* ModelCache::GetForNpc(const game::NpcDefRecord& npc) {
    // RESOLU 2026-07-14 (cf. ModelCache.h) : npc.fieldE (+1324) = kindIndex+1, meme champ que
    // celui lu par Npc_DrawMesh 0x57FF00 sur l'enregistrement mNPC. Borne dure [1,66]
    // (Model_GetNpcMeshSlot 0x4E5910, a2<=0x41) -- hors bornes -> nullptr (fallback d'origine).
    if (npc.fieldE < 1 || npc.fieldE > 66) return nullptr;
    return GetForNpcKind(static_cast<int>(npc.fieldE) - 1, 0);
}

void ModelCache::Clear() {
    entries_.clear(); // ~Entry -> ~SkinnedModel -> Release() (VB/IB/textures liberes)
}

void ModelCache::EvictIfNeeded() {
    if (maxResident_ == 0) return; // illimite, pas d'eviction
    // Eviction LRU simple (scan lineaire, cf. mission "eviction simple") : retire
    // l'entree de plus faible lastUseTick jusqu'a repasser sous la limite. L'entree
    // qui vient d'etre inseree porte tick_ (le plus recent), elle n'est donc jamais
    // choisie ici sauf si le cache entier ne contient qu'elle (maxResident_ == 0 deja
    // exclu ci-dessus).
    while (entries_.size() > maxResident_) {
        auto worst = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.lastUseTick < worst->second.lastUseTick) worst = it;
        }
        entries_.erase(worst);
    }
}

} // namespace ts2::gfx

// Gfx/ModelCache.cpp — implementation. See ModelCache.h for the path pattern and
// original EAs (SObject_BuildPath 0x4D89C0, Model_LoadFile 0x40E700 via
// asset::SObject::Load, GPU upload via MeshRenderer::Upload).
#include "Gfx/ModelCache.h"
#include "Core/Log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ts2::gfx {

namespace {

// Same join convention as Game/GameDatabase.cpp::Join (separator '\').
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
    const int subBound = (variant == 1) ? 4 : 1; // cf. ModelCache.h banner: variant 0/2 -> only 1 sub.
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
    const int variantBound = (slot == 0) ? 7 : 3; // cf. ModelCache.h: SLOT0=7 variants, SLOT1=3 variants.
    if (variant < 0 || variant >= variantBound) return {};

    const int kindIndex = race + 3 * gender; // 0-based, ∈[0,6) -- formula proven in doc §3ter.
    const int slotToken = (slot == 0) ? 1 : 2; // catalog constant, NOT network data.

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

    // First access for this stem: resolve path, load, GPU upload.
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
    (void)inserted; // always true here (stem absent, verified by find() above)

    EvictIfNeeded();
    return ins->second.loadFailed ? nullptr : &ins->second.model;
}

const SkinnedModel* ModelCache::GetForItem(const game::ItemInfo& item, int slot) {
    if (slot < 0 || slot >= 3) return nullptr;

    const char* raw = item.model[slot];
    const size_t len = ::strnlen(raw, sizeof(item.model[slot]));
    if (len == 0) return nullptr; // unused variant slot for this item

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
    // RESOLVED (Docs/TS2_MONSTER_NPC_MODEL.md §2/§4): kindIndex = MONSTER_INFO.kindIndexP1 - 1.
    // Proof: Char_Draw 0x5805C0 reads `*(DWORD*)(*(this+24) + 244) - 1` and passes it as-is to
    // Model_GetNpcMotionSlot 0x4E5960, which bounds `a2` to [0, 0x14C]=[0,332] -- exactly the 333
    // M*.SOBJECT models catalogued by AssetMgr_InitAllSlots 0x4DEB50 (address arithmetic proof
    // flt_FBBF3C/flt_FF67CC, doc §4). `this+24` == MONSTER_INFO* is proven by
    // Pkt_SpawnMonster 0x467B00 (ItemDefTbl_GetRecord stored at dword offset 24 of the entity).
    //
    // OFF-BY-ONE FIX: the MONSTER getter 0x4C6570 is STRICTLY 1-based
    // (base+944*(id-1)). Pkt_SpawnMonster 0x467B00 passes the RAW network id (1-based) -> resolve
    // via game::GetMonsterInfo (which applies the -1), instead of the old table.record(id) WITHOUT -1.
    const game::MonsterInfo* mi = game::GetMonsterInfo(monsterDefId);
    if (!mi) return nullptr; // table not loaded, id out of bounds, or empty slot (id==0).

    const uint32_t k1 = mi->kindIndexP1; // +244, proven by Char_Draw 0x5805C0 (*(def+244)-1)

    // Bounds proven in doc §2: kindIndexP1 (1-based, as stored in the .IMG file) must
    // be in [1, 333] to designate a real model; beyond that, Model_GetNpcMotionSlot silently
    // falls back to the generic fallback on the original binary side -- here we return nullptr
    // (cached as a normal failure by GetForMonsterKind -> Get()).
    if (k1 < 1 || k1 > 333) {
        static bool warnedOutOfRange = false;
        if (!warnedOutOfRange) {
            warnedOutOfRange = true;
            TS2_WARN("ModelCache::GetForMonster: kindIndexP1=%u hors bornes [1,333] (monsterDefId=%u) "
                     "-- fallback nullptr (comportement d'origine, cf. Model_GetNpcMotionSlot 0x4E5960)",
                     k1, monsterDefId);
        }
        return nullptr;
    }

    return GetForMonsterKind(static_cast<int>(k1) - 1, 0, 0);
}

const SkinnedModel* ModelCache::GetForMonsterDamaged(uint32_t monsterDefId, int hpCurrent) {
    // Damage-state variant (Char_Draw 0x5805C0): family "M{k+1}002{sub+1}" (variant=1,
    // 4 sub-models). Original gate: def[+232]==2 & g_Opt_GfxDetailShadows==1 (graphics option
    // decided by the W3 render layer; we port the universal field232==2 guard here).
    // sub = 3 - ftol(hpCurrent*100.0/def[+368]) / 30. hpCurrent = monster record offset 92
    // (body+76) = *((int*)this+23) in Char_Draw. def[+368] = definition hpMax.
    const game::MonsterInfo* mi = game::GetMonsterInfo(monsterDefId); // 1-based (ItemDefTbl_GetRecord 0x4C6570)
    if (!mi) return nullptr;
    if (mi->field232 != 2) return nullptr; // Char_Draw: field232!=2 => no damage states
    const uint32_t k1 = mi->kindIndexP1;
    if (k1 < 1 || k1 > 333) return nullptr;
    if (mi->hpMax < 1) return nullptr;     // divisor def[+368] (the validator guarantees >=1, defensive guard)

    const int q = static_cast<int>(static_cast<double>(hpCurrent) * 100.0 / static_cast<double>(mi->hpMax)); // Crt_ftol
    int sub = 3 - q / 30;
    if (sub < 0) sub = 0; else if (sub > 3) sub = 3; // defensive bound (BuildMonsterStem variant1 => sub in [0,4))
    return GetForMonsterKind(static_cast<int>(k1) - 1, 1, sub);
}

const SkinnedModel* ModelCache::GetForNpc(const game::NpcDefRecord& npc) {
    // RESOLVED 2026-07-14 (cf. ModelCache.h): npc.fieldE (+1324) = kindIndex+1, same field
    // read by Npc_DrawMesh 0x57FF00 on the mNPC record. Hard bound [1,66]
    // (Model_GetNpcMeshSlot 0x4E5910, a2<=0x41) -- out of bounds -> nullptr (original fallback).
    if (npc.fieldE < 1 || npc.fieldE > 66) return nullptr;
    return GetForNpcKind(static_cast<int>(npc.fieldE) - 1, 0);
}

void ModelCache::Clear() {
    entries_.clear(); // ~Entry -> ~SkinnedModel -> Release() (VB/IB/textures freed)
}

void ModelCache::EvictIfNeeded() {
    if (maxResident_ == 0) return; // unlimited, no eviction
    // Simple LRU eviction (linear scan, cf. "simple eviction" mission): removes
    // the entry with the lowest lastUseTick until back under the limit. The entry
    // that was just inserted carries tick_ (the most recent), so it is never
    // picked here unless the cache contains only it (maxResident_ == 0 already
    // excluded above).
    while (entries_.size() > maxResident_) {
        auto worst = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.lastUseTick < worst->second.lastUseTick) worst = it;
        }
        entries_.erase(worst);
    }
}

} // namespace ts2::gfx

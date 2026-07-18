// Gfx/ModelCache.h — lazy cache of skinned models (.SOBJECT) resident on GPU.
//
// PROBLEM solved here: Gfx/MeshRenderer.h (ALREADY WRITTEN, milestone 4) can upload/draw
// an asset::SObject once PARSED IN MEMORY (MeshRenderer::Upload), but nothing in
// ClientSource fetches the .SOBJECT file from disk for a given entity, parses it
// (asset::SObject::Load), uploads it, or keeps the result in GPU memory across
// frames. ModelCache fills this gap: key = model name WITHOUT extension
// ("stem", e.g. "C001001001"), value = ts2::gfx::SkinnedModel already resident on GPU.
//
// PATH PATTERN — determined by actual repo exploration (NOT a guess):
//   GameData/G03_GDATA/D04_GSOBJECT/NNN/<stem>.SOBJECT
// where NNN = category folder, and stem[0] = prefix letter 100% consistent with
// the files actually present on disk (2280+66+915+6+... files surveyed) and with
// `SObject_BuildPath 0x4D89C0` (Docs/TS2_GXD_ENGINE.md §2.7: "by category 1=C
// player, 2=N NPC, 3=M monster, 4=P, 5/10=L, 6=W weapon, 7=H, 9=Y, 10=A accessory"):
//   001/C*.SOBJECT (2280, player)   002/N*.SOBJECT (66, NPC)   003/M*.SOBJECT (915, monster)
//   004/P*.SOBJECT                 005/L*.SOBJECT             006/W*.SOBJECT (816, weapon)
//   007/H*.SOBJECT                 009/Y*.SOBJECT              010/A*.SOBJECT
// (008 does not exist — gap confirmed by disk scan, not a hole in this table).
// See also Game/MotionPools.h (§1, header comment), which documents that
// SObject_BuildPath belongs to the mGDATA pool and was, until now, explicitly OUT
// OF SCOPE for any module already written — this file finally implements it (BuildSObjectPath).
//
// CORRELATION WITH THE DATABASES (mission):
//   - ITEM_INFO (Game/GameDatabase.h, ALREADY WRITTEN): the `model[3][51]` field IS
//     directly the "stem" expected here (original comment: "3 model names (51 bytes
//     each)") — see GetForItem() below, wired and functional.
//
//   - MONSTER_INFO / NpcDefRecord (mission "monster/NPC model resolution", supplementary RE
//     conducted for this file, live idaTs2 MCP decompilation — NOT the EAs suggested by the
//     mission, which are WRONG, see IDB naming trap below):
//     IDB NAMING TRAP WARNING (same family as the NPC/Quest swap documented at the top of
//     Game/ExtraDatabases.h): the mission cites `MobDb_LoadImg 0x4C3930 / MobDb_ValidateEntry
//     0x4C2C50` for monsters — THESE TWO EAs ACTUALLY LOAD/VALIDATE ITEM_INFO
//     (005_00002.IMG), NOT MONSTER_INFO. The real MONSTER_INFO EAs (005_00004.IMG, embedded
//     name "MONSTER_INFO", rec[0]="Goblin") are `ItemDefTbl_LoadImg 0x4C62A0` (misleading IDB
//     name, actually loads monsters) / validator `ItemDefTbl_ValidateRecord 0x4C5350` — decompiled
//     here for confirmation, see Docs/TS2_IMG_FORMAT.md §4.2 (already correct) and
//     Game/GameDatabase.cpp::kTables (stride 944, header 88, count 10000 — already accurate).
//     Layout confirmed by decompiling validator 0x4C5350: id@+0 (1..10000, ==index+1),
//     name@+4 (25-byte cstring), THEN TWO OPTIONAL 101-byte STRINGS at +29 and +130 (loop
//     `for(j<2) for(k<101 && rec[101*j+k+29])` — ALWAYS EMPTY on the first 12 real records
//     of the live file, per ad hoc Python dump: neither "Goblin"(id1) nor "Dragon Priest"(id2)
//     has text there). SO THIS IS NOT a "model name" field like ITEM_INFO — role not
//     elucidated (maybe boss subtitle/text, never populated on the common mobs tested).
//     Then numeric fields including collision dims @+248/+252/+256 (Game/EntityManager.cpp::
//     kDefDimA/kDefDimB, formula sqrt(a²+b²)*0.5 confirmed), drop table A ×5 pairs @+448 (8 bytes
//     each) and drop table B ×50 pairs @+544 — NO field among the ~230 dwords of the 944-byte
//     record has a validation bound compatible with a "model index" (see below).
//
//     REAL MODEL RESOLUTION (found via `AssetMgr_InitAllSlots 0x4DEB50`, which pre-generates
//     ALL SObject paths at boot by calling `SObject_BuildPath 0x4D89C0` per category):
//     the file name is NOT a field stored in MONSTER_INFO / NpcDefRecord — it is
//     **computed by a printf** from a "kindIndex" (visual MODEL index,
//     NOT the definition id):
//       Category 3 (monster, folder 003): "M%03d%03d%03d.SOBJECT" %
//                                              (kindIndex+1, variant+1, sub+1)
//         kindIndex 0-based in [0,333) (AssetMgr loop `for(i59<333)`); variant 0-based in [0,3):
//         0="base pose" (1 file, sub fixed 0), 1="variant" (sub 0..3, 4 files),
//         2="secondary variant" (1 file, sub fixed 0).
//       Category 2 (NPC, folder 002): "N%03d%03d001.SOBJECT" % (kindIndex+1, variant+1)
//         kindIndex 0-based in [0,66) (AssetMgr loop `for(i57<66)`); variant ALWAYS observed 0
//         in the binary (inner loop `for(i58<1)`) but the printf accepts a 2nd index —
//         exposed anyway for full format fidelity (see BuildNpcStem below).
//     These counts (333 monster models, 66 NPC models) EXACTLY match the real file counts
//     cited by the banner above (003/M* and 002/N*) — the existing kSObjectCategories table
//     (prefix 'M'->3, 'N'->2) is therefore ALREADY correct for these stems.
//
//     ✅ MONSTER RESOLVED (supplementary mission "monster/NPC model resolution", RE done
//     2026-07-14, complete proof verified in Docs/TS2_MONSTER_NPC_MODEL.md §1-4): the field
//     that gives kindIndex IS indeed in MONSTER_INFO — dword `+244` (named
//     `kindIndexP1` in the doc), 1..10000 per the validator but bounded in practice to [1,333]
//     by the consumer (Model_GetNpcMotionSlot 0x4E5960, `a2 > 0x14C` => fallback):
//       kindIndex (0-based, [0,333)) = MONSTER_INFO.field244 - 1
//     Direct proof: `Char_Draw 0x5805C0` reads `*(DWORD*)(*(this+24)+244) - 1` and passes it
//     as-is to `Model_GetNpcMotionSlot`/`SObject_DrawEx`; `this+24` == MONSTER_INFO* is proven by
//     `Pkt_SpawnMonster 0x467B00` (stores `ItemDefTbl_GetRecord(id)` at dword offset 24 of the
//     monster entity). Cross-checked arithmetic proof (doc §4): the static 333-entry catalog
//     pre-generated by `AssetMgr_InitAllSlots 0x4DEB50` (`flt_FBBF3C`/`flt_FF67CC`) lands
//     EXACTLY on the same addresses read by `Char_Draw`/`Char_UpdateMotionState`.
//     The 1263 populated MONSTER_INFO records (max id=1511) thus share these 333 kindIndex
//     values N-to-1 (multiple definitions = same visual model, e.g. rank variants).
//     `GetForMonster()` below is wired to this formula (see Gfx/ModelCache.cpp).
//
//     ✅ NPC RESOLVED (mission "NPC mesh rendering", 2026-07-14, see Docs/TS2_NPC_MESH_DRAW.md §2-3 +
//     Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md — this banner REPLACES the old "NPC NOT RESOLVED" note,
//     now obsolete): the NPC body IS drawn in-game by `Npc_DrawMesh 0x57FF00` (on a separate
//     RENDER array `g_NpcRenderArray` 0x1764D14, not `dword_17AB534` — hence no `Char_Draw`
//     call on NPCs). `Npc_DrawMesh` reads the visual model's kindIndex+1 at offset `+1324`
//     of the resolved `mNPC` record (`SkillDefTbl_GetRecord`) = field
//     `NpcDefRecord::fieldE` (Game/ExtraDatabases.h). Hard bound [1,66] via `Model_GetNpcMeshSlot
//     0x4E5910` (`a2 <= 0x41`). `GetForNpc()` below calls `GetForNpcKind(fieldE - 1, 0)`
//     within this bound, nullptr otherwise (original out-of-catalog fallback).
//
//   - EntityRenderInfo::modelCategoryId (Game/EntityDrawLogic.h) is an INTEGER (costume/model
//     id), NOT a file name: resolving it to a stem goes through a dedicated table
//     (likely PcModel_ResolveEquipSlot 0x4E46A0, Docs/TS2_GXD_ENGINE.md),
//     also out of scope for this mission (cache only).
#pragma once
#include "Gfx/MeshRenderer.h"
#include "Game/GameDatabase.h"
#include "Game/ExtraDatabases.h" // for game::NpcDefRecord (GetForNpc)
#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  Path resolution (see header banner for proof of the pattern).
// ---------------------------------------------------------------------------
struct SObjectCategory { char prefix; int folder; };

// D04_GSOBJECT category table verified by disk scan (see header banner).
inline constexpr SObjectCategory kSObjectCategories[] = {
    {'C', 1}, {'N', 2}, {'M', 3}, {'P', 4}, {'L', 5}, {'W', 6}, {'H', 7}, {'Y', 9}, {'A', 10},
};

// D04_GSOBJECT/NNN folder number for a given stem prefix ('C','N','M',...).
// -1 if unknown prefix.
int ResolveSObjectFolder(char prefix);

// Full path: <gameDataDir>\G03_GDATA\D04_GSOBJECT\NNN\<stem>.SOBJECT.
// Empty string if `stem` is empty or has an unknown prefix (no exception thrown).
std::string BuildSObjectPath(const std::string& gameDataDir, const std::string& stem);

// ---------------------------------------------------------------------------
//  Monster/NPC stems — format confirmed by decompiling SObject_BuildPath 0x4D89C0
//  (category 3 and 2 cases), see header banner for the full proof and real counts.
// ---------------------------------------------------------------------------

// Stem "M{kindIndex+1:03d}{variant+1:03d}{sub+1:03d}" (category 3, folder 003, prefix 'M').
// kindIndex 0-based in [0,333) — THIS IS NOT the `id` field of a MONSTER_INFO record
// (1..10000/1263 populated, see banner): it's the visual MODEL index (only 333 distinct
// values). variant 0-based in [0,3), sub bounded by variant (0/2 -> sub must be 0;
// 1 -> sub in [0,4)), faithful to the 3 inner loops of AssetMgr_InitAllSlots 0x4DEB50. Empty
// string if a parameter is out of bounds (no exception thrown).
std::string BuildMonsterStem(int kindIndex, int variant = 0, int sub = 0);

// Stem "N{kindIndex+1:03d}{variant+1:03d}001" (category 2, folder 002, prefix 'N').
// kindIndex 0-based in [0,66) — THIS IS NOT the `id` field of an NpcDefRecord (1..500/131
// populated, see ExtraDatabases.h + banner): it's the visual MODEL index (only 66 distinct
// values). variant 0-based, no known bound on the binary side (the only call observed
// in AssetMgr_InitAllSlots always uses variant=0) — exposed for fidelity to SObject_BuildPath's
// printf (case 2), which accepts any a4. Empty string if kindIndex or variant is negative
// or if kindIndex is out of bounds.
std::string BuildNpcStem(int kindIndex, int variant = 0);

// Stem "C{kindIndex+1:03d}{slotToken:03d}{variant+1:03d}" (category 1, folder 001, prefix
// 'C') — PLAYER BASE BODY, RESOLVED by direct decompilation (mission "player base body model
// resolution", 2026-07-14, see Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5: proof at 3
// call sites that re-read entity+92/+96 (=body+68/+72) on the g_EntityArray runtime array
// itself, both self AND remote — NOT a mere cardinality coincidence). kindIndex 0-based =
// race + 3*gender (race in [0,3), gender in [0,2) -> 6 combinations). `slot` selects the body
// PIECE (0 = SLOT0, catalog flt_F59A7C, 7 variants, path token "001"; 1 = SLOT1,
// catalog flt_F5B21C, 3 variants, token "002") — this token is a catalog CONSTANT
// wired by the original caller, NEVER network data (see doc §0). `variant` = raw value
// read from PlayerEntity::body+76 (slot 0) or body+80 (slot 1). Empty string if race/
// gender/slot/variant is out of bounds (no exception thrown): race in [0,3), gender in [0,2),
// slot in {0,1}, variant in [0,7) for slot 0 or [0,3) for slot 1.
std::string BuildPlayerBodyStem(int race, int gender, int slot, int variant);

// The 2 base body pieces ACTUALLY drawn together by the original player pipeline
// (Char_DrawWeaponTrailEffect EA 0x561750-0x561786 for SLOT0 / 0x561949-0x561993 for SLOT1,
// NOT Char_Draw — see Docs/TS2_PLAYER_BODY_MODEL.md §3ter-2: Char_Draw never draws a
// player). Each pointer is independently nullptr if its stem is out of bounds, not found
// on disk, or fails parsing/upload — no exception, see ModelCache::Get().
struct PlayerBodyModel {
    const SkinnedModel* slot0 = nullptr; // C{kindIndex+1}001{costumeSlot0+1} (7 possible variants)
    const SkinnedModel* slot1 = nullptr; // C{kindIndex+1}002{costumeSlot1+1} (3 possible variants)
};

// ---------------------------------------------------------------------------
//  ModelCache — key = stem (model name WITHOUT path or extension, e.g. "C001001001").
// ---------------------------------------------------------------------------
class ModelCache {
public:
    // renderer must already be Init()'d (D3D9 device ready); gameDataDir = "GameData"
    // root (same convention as Game::LoadGameDatabases). maxResident = number of GPU
    // models kept resident simultaneously before simple LRU eviction; 0 = unlimited
    // (not recommended — no GPU memory bound).
    explicit ModelCache(MeshRenderer& renderer, std::string gameDataDir, size_t maxResident = 256);

    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;

    // Lazy-load: returns the resident model for `stem`, loading/uploading it on first
    // access. nullptr if stem is empty, unknown prefix, file not found, invalid SOBJECT
    // parsing, or failed GPU upload — a failure is CACHED (does not retry loading the
    // same missing stem on every call, see TS2_WARN logged once on first failure). A
    // valid but 0-mesh SOBJECT is NOT a failure: it is cached and returned normally
    // (SkinnedModel::Empty() == true on the caller side).
    const SkinnedModel* Get(const std::string& stem);

    // Convenience: resolves ITEM_INFO::model[slot] (Game/GameDatabase.h, ALREADY WRITTEN)
    // and calls Get() on it. slot 0 = main model, 1/2 = secondary variants depending on
    // the item (exact role of slots 1/2 not disambiguated by this module — see the header
    // banner of Game/GameDatabase.h). Returns nullptr if slot is out of bounds [0,3) or if
    // the corresponding field is an empty string (item with no variant at that slot).
    const SkinnedModel* GetForItem(const game::ItemInfo& item, int slot = 0);

    // Direct lazy-load by monster MODEL index (see BuildMonsterStem above for the full
    // semantics of kindIndex/variant/sub — THIS IS NOT a MONSTER_INFO id). This is currently
    // the ONLY reliably usable monster API: see header banner for the proof that no known
    // MONSTER_INFO field lets you compute kindIndex from a definition id. nullptr if
    // kindIndex/variant/sub is out of bounds (delegated to BuildMonsterStem).
    const SkinnedModel* GetForMonsterKind(int kindIndex, int variant = 0, int sub = 0);

    // Same for an NPC MODEL index (see BuildNpcStem above).
    const SkinnedModel* GetForNpcKind(int kindIndex, int variant = 0);

    // RESOLVED (mission "player base body wiring", 2026-07-14, see header banner +
    // Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5): direct lazy-load of the 2 player base body
    // pieces (SLOT0+SLOT1) for a given race/gender/costume tuple — see BuildPlayerBodyStem
    // above for the exact formula and bounds. `race`/`gender` come from
    // PlayerEntity::body+68/+72, `costumeSlot0`/`costumeSlot1` from body+76/+80 (same offsets
    // valid for both the local player AND remote players, see doc §1: entity[0]+96/100/104
    // are mutated IN PLACE by equipment, so already up to date in `body` with no separate
    // global — unlike the weapon, which has a dedicated self global dword_1673248 wired
    // elsewhere). Each piece is resolved independently (one can succeed while the other
    // fails/is out of bounds) — see PlayerBodyModel above.
    PlayerBodyModel GetForPlayerBody(int race, int gender, int costumeSlot0, int costumeSlot1);

    // RESOLVED (mission "monster/NPC model resolution in ModelCache", 2026-07-14, see header
    // banner + Docs/TS2_MONSTER_NPC_MODEL.md §2/§4/§7): resolves via
    // `game::GetMonsterInfo(monsterDefId)` (1-based accessor, see ItemDefTbl_GetRecord 0x4C6570
    // base+944*(id-1)), reads the typed field `MonsterInfo::kindIndexP1` (+244, 1-based as
    // stored in the file) and calls `GetForMonsterKind(kindIndexP1 - 1, 0, 0)` if
    // `1 <= kindIndexP1 <= 333`. nullptr if the monster table is not loaded, if
    // `monsterDefId` is out of bounds / slot empty, or if kindIndexP1 is outside the
    // 333-model known catalog (original fallback, see Model_GetNpcMotionSlot 0x4E5960).
    // OFF-BY-ONE FIX (this milestone): the old code indexed `g_World.db.monster.record(id)`
    // WITHOUT -1 -- WRONG, since Pkt_SpawnMonster 0x467B00 passes the RAW network id (1-based)
    // to the 1-based getter 0x4C6570. `monsterDefId` remains the 1-based id (same convention
    // as ResolveMobDef, itself now corrected). The typed struct `game::MonsterInfo` now EXISTS
    // (Game/GameDatabase.h); the old note "const MonsterInfo& does NOT exist" is obsolete, but
    // the signature stays `uint32_t monsterDefId` for consistency with ResolveMobDef.
    const SkinnedModel* GetForMonster(uint32_t monsterDefId);

    // Damage-state variant (Char_Draw 0x5805C0). `hpCurrent` = entity runtime hp
    // (monster record offset 92 = body+76 = `*((int*)this+23)` in Char_Draw). Returns nullptr
    // if `mi->field232 != 2` (the monster has no damage states), if the id/kindIndexP1 is
    // out of catalog, or if the table is not loaded. Family "M{k+1}002{sub+1}" (variant 1, 4
    // sub-models): sub = 3 - ftol(hpCurrent*100/hpMax)/30 (bounded [0,3]). Original graphics
    // gate (g_Opt_GfxDetailShadows 0x84DEF8 == 1) left to the rendering caller (W3), which
    // chooses whether to call this variant vs GetForMonster (base pose) based on the option.
    const SkinnedModel* GetForMonsterDamaged(uint32_t monsterDefId, int hpCurrent);

    // RESOLVED (mission "NPC mesh rendering", 2026-07-14, see Docs/TS2_NPC_MESH_DRAW.md §2-3 +
    // Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md): `npc.fieldE` (+1324) IS the kindIndex+1 of the
    // NPC visual model (direct proof by decompiling `Npc_DrawMesh` 0x57FF00, which reads this
    // same offset on the `mNPC` record resolved by `SkillDefTbl_GetRecord`). Calls
    // `GetForNpcKind(fieldE - 1, 0)` if `1 <= fieldE <= 66` (hard bound `Model_GetNpcMeshSlot`
    // 0x4E5910, `a2 <= 0x41`); nullptr otherwise (original out-of-catalog fallback).
    const SkinnedModel* GetForNpc(const game::NpcDefRecord& npc);

    // Purges the whole cache (immediately releases all GPU VB/IB/textures).
    void Clear();

    size_t Resident()    const { return entries_.size(); }
    size_t MaxResident()  const { return maxResident_; }

private:
    struct Entry {
        SkinnedModel model;
        uint64_t     lastUseTick = 0;
        bool         loadFailed  = false;
    };

    void EvictIfNeeded();

    MeshRenderer&                          renderer_;
    std::string                            gameDataDir_;
    size_t                                 maxResident_;
    uint64_t                               tick_ = 0;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace ts2::gfx

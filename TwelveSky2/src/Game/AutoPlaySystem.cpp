// Game/AutoPlaySystem.cpp — see AutoPlaySystem.h for the full EA -> method table.
#include "Game/AutoPlaySystem.h"
// AutoTarget_MonsterActionState(): reads dword_1766F8C (= MonsterEntity::body+8), the monster's
// raw action state, exactly like the binary (see AP-06 and the banner in AutoPlaySystem.h).
// REUSED as-is rather than duplicated locally. No include cycle: this header only pulls in
// GameState.h / ClientRuntime.h / ComboPickupTick.h, none of which include AutoPlaySystem.h.
// NB Winsock order: AutoTargetCombatGate.h transitively pulls in Net/NetClient.h, which includes
// <winsock2.h> BEFORE <windows.h> (NetClient.h:19-20); no header included here brings in
// <windows.h> upstream, so there is no ordering constraint to respect in this file (unlike
// World/TerrainPicker.cpp:6-13, which includes Gfx/Camera.h).
#include "Game/AutoTargetCombatGate.h"
#include <cmath>
#include <cstring>
#include <utility>
#include <fstream>   // LoadFriendList/LoadEnemyList/Save*: binary I/O for the 011/012.BIN files
#include <algorithm> // std::min (name clamped to the 25-byte slot)

namespace ts2::game {

namespace {

// Raw LE reads into an .IMG table record (MONSTER_INFO/NPC — not modeled as a typed
// struct, see Docs/TS2_IMG_FORMAT.md §7) or into a packet's raw body (NpcEntity::body).
// Same convention as ResolveMobDef/RdU32 on the EntityManager.cpp side (not exported,
// reimplemented here locally).
int32_t ReadI32(const void* base, std::size_t offset) {
    int32_t v = 0;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}
uint32_t ReadU32(const void* base, std::size_t offset) {
    uint32_t v = 0;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}
float ReadF32(const void* base, std::size_t offset) {
    float v = 0.0f;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(v));
    return v;
}

// Offsets in the record pointed to by dword_17AB598[i] for loot/NPC ENTRIES: it is an
// ITEM_INFO (436 bytes, NOT a MONSTER_INFO 944 bytes — corrected), see MobDb_FindByName
// 0x4C3C50 (stride 436) and the IDB doc for Pkt_SpawnNpc 0x467EC0 ("the dword_17AB534 array
// mixes interactive NPCs and ground loot bags, def-ptr ITEM_INFO*"). Identifier names kept
// stable (referenced by Game/NpcInteraction.h:88-89) — only the DESCRIPTIONS are corrected here:
constexpr std::size_t kDefOffName       = 4;   // ItemInfo::name (@+4, see GameDatabase.h:60)
constexpr std::size_t kDefOffFaction    = 184; // ItemInfo::category (@+184, 1..4 -> bits 1/2/4/8) [AutoPlay_FindNpcTarget @0x459053]
constexpr std::size_t kDefOffNpcKind    = 188; // ItemInfo::typeCode (@+188; ==1 -> "vendor/direct interactable" NPC)
// The MONSTER records (BuildTargetList/SelectTarget/Count, base dword_1766F74) are, meanwhile,
// MONSTER_INFO; these two offsets apply to mon.def (ITEM_INFO resolved by ResolveNpcDef, see
// EntityManager) via ReadI32 — bounds 232<=3 / 236<=1 proven v13+232/+236:
constexpr std::size_t kDefOffMonCat232  = 232; // mon.def+232 <= 3 (AutoPlay_Build/SelectTarget/Count)
constexpr std::size_t kDefOffMonCat236  = 236; // mon.def+236 <= 1

// Offsets in NpcEntity::body (84 bytes, wire payload+8..91) — see the RecvPackets.h comment
// "body[0..3] = mob-db model id". The following fields are deduced from the pointer
// arithmetic of AutoPlay_FindNpcTarget/MoveToNpc on the original runtime array (base
// dword_17AB534, body starting at +16 in that array = payload+8):
constexpr std::size_t kNpcBodyOffDefId  = 0;  // dword_17AB544[i] (= body[0..3], already resolved via NpcEntity::def)
constexpr std::size_t kNpcBodyOffOffer  = 4;  // dword_17AB548[i]: pickup offer weight/quantity
constexpr std::size_t kNpcBodyOffPosX   = 16; // unk_17AB554 (NPC world position, absent from NpcEntity)
constexpr std::size_t kNpcBodyOffPosY   = 20;
constexpr std::size_t kNpcBodyOffPosZ   = 24;

constexpr float kFixedDt = 0.033f; // literal used by ALL MapColl_SlideMoveGround calls in the
                                    // cluster (distinct from flt_815188=0.033333, the 30 FPS tick).
constexpr float kNpcInteractRange = 50.0f; // NPC distance threshold in "skill range" mode
constexpr float kRebuildIntervalSec = 1.0f;      // 0x3E8 ms
constexpr float kNpcInteractCooldownSec = 0.05f; // 0x32 ms
constexpr float kInitialElapsedSec = 1000.0f;    // >> both thresholds above: immediate action on the 1st tick

// Format of the G02_GINFO\011.BIN (friends) / 012.BIN (enemies) list files — proven on
// AutoPlay_LoadFriendList 0x45D730 / AutoPlay_SaveFriendList 0x45DE50: 48 entries x 25 bytes,
// no header, '@' padding (0x40 @0x45DE9F). Exact size 1200 bytes (0x4B0) required on read.
constexpr std::size_t kNameSlotSize  = 25;    // slot width (== ItemInfo::name[25])
constexpr std::size_t kNameSlotCount = 48;    // 0x30 slots (Save size guard @0x45DEC0)
constexpr std::size_t kListFileSize  = 1200;  // 0x4B0 = 48 * 25 (exact ReadFile/WriteFile)
constexpr char        kNamePadChar   = '@';   // 0x40: inter-name padding (Crt_Memset 64 @0x45DE9F)
constexpr const char* kFriendListPath = "G02_GINFO\\011.BIN"; // @0x7A66E8
constexpr const char* kEnemyListPath  = "G02_GINFO\\012.BIN"; // @0x7A66FC

// MobDb_FindByName 0x4C3C50 — scans the ITEM table (g_World.db.item, stride 436) and compares
// the name @+4 (ItemInfo::name) by exact string equality (Crt_Strcmp, case-sensitive). Returns
// true on the 1st match. Reproduced as-is: list-name validation on read.
bool ItemNameInItemDb(const char* name) {
    const DataTable& db = g_World.db.item;
    for (uint32_t i = 0; i < db.count; ++i) {
        const uint8_t* rec = db.record(i);
        if (rec && std::strcmp(reinterpret_cast<const char*>(rec + kDefOffName), name) == 0)
            return true;
    }
    return false;
}

// Extracts a 25-byte slot's name: bytes up to the 1st '@' or NUL, clamped to the slot. Faithful
// note: the binary does Crt_Strlen(&Buffer[25*i]) (which OVERRUNS the slot, since the file is
// padded with non-null '@') THEN Str_Find('@')+Str_Erase to retruncate — the OBSERVABLE result
// is identical to this clamping (the 1st '@' of a name < 25 bytes falls within its own slot).
// Crt_Strlen's overrun UB is NOT reproduced (safe clamping), per instructions.
std::string ExtractSlotName(const std::uint8_t* buf, std::size_t slotIndex) {
    const char* slot = reinterpret_cast<const char*>(buf + kNameSlotSize * slotIndex);
    std::size_t len = 0;
    while (len < kNameSlotSize && slot[len] != kNamePadChar && slot[len] != '\0')
        ++len;
    return std::string(slot, len);
}

// Body shared by the two readers (AutoPlay_LoadFriendList 0x45D730 / AutoPlay_LoadEnemyList
// 0x45DAF0: strictly identical, only the path and target list differ). Faithful: the binary
// only calls List_Clear on the failure paths (missing file / size != 1200); the SUCCESS path
// appends WITHOUT clearing first (List_PushBackNode alone). Reproduced as-is.
bool LoadNameListFile(const char* path, std::vector<std::string>& outList) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { outList.clear(); return false; } // CreateFileA == -1 @0x45D7D7 -> List_Clear + return 0
    std::uint8_t buf[kListFileSize] = {0};     // Crt_Memset(Buffer, 0, 0x4B0) @0x45D7A5
    f.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(kListFileSize));
    if (static_cast<std::size_t>(f.gcount()) != kListFileSize) {
        outList.clear(); return false;         // NumberOfBytesRead != 1200 @0x45D88E -> List_Clear + return 0
    }
    // Success @0x45D926: append, WITHOUT clearing beforehand (faithful).
    for (std::size_t i = 0; i < kNameSlotCount; ++i) { // for (i=0; i<48; ++i)
        std::string name = ExtractSlotName(buf, i);
        if (name.empty()) continue;              // if (v26) @0x45D9E9: empty name ignored
        if (ItemNameInItemDb(name.c_str()))      // MobDb_FindByName(mITEM, ...) @0x45DA1F
            outList.push_back(std::move(name));  // List_PushBackNode @0x45DA99
    }
    return true; // @0x45DAA3
}

// Body shared by the two writers. `clampMode`: Friend -> clear + false WITHOUT writing if > 48
// (@0x45DEC0); Enemy -> clear THEN writes if > 48 (@0x45E1A3, no return). Faithful asymmetry.
enum class SaveClamp { FriendReturnOnOverflow, EnemyClearAndContinue };
bool SaveNameListFile(const char* path, std::vector<std::string>& list, SaveClamp clampMode) {
    std::uint8_t buf[kListFileSize];
    std::memset(buf, kNamePadChar, kListFileSize); // Crt_Memset(Buffer, 64, 0x4B0) @0x45DE9F / @0x45E182
    if (list.size() > kNameSlotCount) {
        list.clear(); // List_Clear @0x45DECE / @0x45E1B1
        if (clampMode == SaveClamp::FriendReturnOnOverflow)
            return false; // Friend: return 0 WITHOUT writing @0x45DED3
        // Enemy: continues and writes an all-'@' file (list now empty).
    }
    std::size_t i = 0;
    for (const std::string& name : list) { // iterates the list (<= 48 entries) — @0x45DFC1 / @0x45E294
        if (i >= kNameSlotCount) break;
        const std::size_t n = std::min<std::size_t>(name.size(), kNameSlotSize);
        std::memcpy(buf + kNameSlotSize * i, name.data(), n); // rest of the slot left as '@'
        ++i;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc); // CreateFileA CREATE_ALWAYS @0x45E0AD
    if (!out) return false;                                       // hFile == -1 @0x45E0BA -> return 0
    out.write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(kListFileSize));
    return static_cast<bool>(out); // WriteFile OK && written==1200 @0x45E101 -> return CloseHandle
}

// Stat_UnpackCombined 0x54CE40 — pure utility, translated as-is (required by
// CheckTownScroll to read the talisman).
void UnpackCombined(int32_t v, int32_t& hi, int32_t& lo) {
    if (v < 0) { hi = 0; lo = 0; return; }
    hi = v / 1000000;
    lo = v % 1000000;
    if (hi > 100) hi = 0;
    if (lo > 100000) lo = 0;
}

} // namespace (anonymous)

// AutoPlay_Construct 0x457EA0
AutoPlaySystem::AutoPlaySystem() {
    // targets_ / targetCount_ / currentTargetIndex_ are already at their original values via
    // the default initializers (id=-1, count=0, target=-1 — see AutoPlayTargetSlot and the
    // class members). The timers start "expired" to trigger a list build / NPC interaction on
    // the very first call, like the binary where the initial GetTickCount() always exceeds the
    // 1000 ms / 50 ms thresholds measured from 0.
    rebuildTimerSec_ = kInitialElapsedSec;
    npcInteractCooldownSec_ = kInitialElapsedSec;
    // friendNames/enemyNames stay empty (original List_Construct(this+296)/(this+324)).
    // NB: the offsets 0..19 and 264..275 zeroed by Crt_Memset in the binary belong to the
    // autoplay UI panel state (out of scope for this cluster, none of the ~19 functions
    // studied read them) — not reproduced here.
}

// AutoPlay_DistanceToPlayer 0x4589E0
float AutoPlaySystem::DistanceToPlayer(float x, float y, float z) {
    const PlayerEntity& self = g_World.Self();
    const float dx = x - self.x;
    const float dz = z - self.z;
    (void)y; // the Y component (0x458a1c/0x458a5a) is computed in the original binary then
             // NEVER added to the total (v6 = v8 + v5, without the Y term): strictly 2D (X,Z)
             // distance — a quirk faithfully reproduced, not an omission.
    return std::sqrt(dx * dx + dz * dz);
}

// AutoPlay_InsertTargetSorted 0x458870 — sorted insertion by ascending distance, 15-slot
// capacity; the weakest element potentially bumped off the end of a full list is dropped
// (faithful: the binary does not append it anywhere if count==15).
void AutoPlaySystem::InsertTargetSorted(int32_t monsterIndex, float distance, bool available) {
    for (uint16_t i = 0; i < targetCount_; ++i) {
        if (distance < targets_[i].distance) {
            std::swap(monsterIndex, targets_[i].monsterIndex);
            std::swap(distance, targets_[i].distance);
            std::swap(available, targets_[i].available);
        }
    }
    if (targetCount_ < 15) {
        targets_[targetCount_].monsterIndex = monsterIndex;
        targets_[targetCount_].distance = distance;
        targets_[targetCount_].available = available;
        ++targetCount_;
    }
}

// AutoPlay_IsTargetLocked 0x458B80
bool AutoPlaySystem::IsTargetLocked(int32_t monsterIndex) const {
    // Faithful guard: the binary refuses any locking once the list is full (>= 15), even if
    // the searched id is present — quirk kept as-is.
    if (targetCount_ >= 15) return false;
    for (uint16_t i = 0; i < targetCount_; ++i) {
        if (targets_[i].available && targets_[i].monsterIndex == monsterIndex)
            return true;
    }
    return false;
}

// AutoPlay_ClearTargetSlot 0x4587E0 — frees the slot whose id == current target, then resets
// the current target to "none". The binary writes a float NaN into this field (type confused
// by the decompiler, see the parameter's float* cast); the 4 other sites touching this same
// field (+220) all use the integer sentinel -1 (Construct, RemoveTargetByMonsterIndex,
// SelectTarget, BuildTargetList) — normalized to -1 here, same role, not a guess at the value.
void AutoPlaySystem::ClearTargetSlot() {
    for (uint16_t i = 0; i < 15; ++i) {
        if (targets_[i].monsterIndex == currentTargetIndex_) {
            targets_[i] = AutoPlayTargetSlot{};
            currentTargetIndex_ = -1;
            return;
        }
    }
}

// AutoPlay_ResetTargetList 0x458AB0 — if the current target is NOT locked, it is freed (and
// currentTargetIndex_ reset to -1) before the whole list is cleared; if it IS locked, it
// survives the reset (ClearTargetSlot not called), only the 15-slot candidate list is cleared.
void AutoPlaySystem::ResetTargetList() {
    if (!IsTargetLocked(currentTargetIndex_))
        ClearTargetSlot();
    targetCount_ = 0;
    targets_.fill(AutoPlayTargetSlot{});
}

// AutoPlay_RemoveTargetById 0x458E00 — scans all 15 slots (not just the occupied ones, like
// the binary); removes the first slot whose id matches and ALWAYS resets the current target
// to -1 once a removal happens (even if it wasn't the current target) — quirk faithfully kept.
void AutoPlaySystem::RemoveTargetByMonsterIndex(int32_t monsterIndex) {
    for (uint16_t i = 0; i < 15; ++i) {
        if (targets_[i].monsterIndex == monsterIndex) {
            targets_[i] = AutoPlayTargetSlot{};
            currentTargetIndex_ = -1;
            return;
        }
    }
}

// AutoPlay_BuildTargetList 0x458280
bool AutoPlaySystem::BuildTargetList() {
    if (!externalState.worldReady) return false; // dword_14A88E8

    ResetTargetList();

    const PlayerEntity& self = g_World.Self();
    const float speed = host.GetSelfMoveSpeed ? host.GetSelfMoveSpeed() : 1.0f;

    for (std::size_t i = 0; i < g_World.monsters.size(); ++i) {
        const MonsterEntity& mon = g_World.monsters[i];

        // Faithful gating: def resolved (v13), active (dword_1766F74), valid state and != dead
        // (dword_1766F8C), categories 232<=3 and 236<=1.
        if (!mon.def || !mon.active) continue;
        // (0x45831a / 0x45832d) `!dword_1766F8C[70*i] || dword_1766F8C[70*i] == 12` — raw action
        // state = base+0x18 = body+8, read via AutoTarget_MonsterActionState (AP-06: this filter
        // used to read the ghost field MonsterAutoplayExt::state, frozen at 1, so it NEVER activated).
        const int32_t monState = AutoTarget_MonsterActionState(mon);
        if (monState == 0 || monState == 12) continue;
        if (ReadI32(mon.def, kDefOffMonCat232) > 3) continue;
        if (ReadI32(mon.def, kDefOffMonCat236) > 1) continue;

        const float dist = DistanceToPlayer(mon.x, mon.y, mon.z);
        bool inRange;

        if (config.mode != 1) {
            // "Accessibility" branch: the monster must be reachable via collision sliding from
            // the player's position (no obstacle on the path).
            float outX = mon.x, outY = self.y, outZ = mon.z; // default if host not wired: reachable
            if (host.SlideMove)
                host.SlideMove(self.x, self.y, self.z, mon.x, self.y, mon.z, speed, kFixedDt, outX, outY, outZ);
            inRange = (outX == mon.x && outZ == mon.z);
        } else {
            // "Skill range" branch (0x4583d9..0x458489): threshold = active skill cost (single
            // if in stance 2 AND configured, otherwise AoE) + engage range of the CURRENT target
            // (this+220) — faithful: it is indeed the already-locked target that serves as the
            // reference, NOT the monster i under examination.
            uint32_t skillId = config.skillAoE;
            if (PlayerIsInStance(2) && config.skillSingle != 0) skillId = config.skillSingle;
            const int32_t cost = Skill_CostById(static_cast<int>(skillId), g_World.self, g_World.db.item);
            // (0x458417 / 0x458479) `+ *((float*)&unk_1766FD8 + 70 * *(this+220))`: unk_1766FD8 =
            // base+0x64 = MonsterEntity::radius (AP-05: this site used to read the ghost field
            // MonsterAutoplayExt::engageRange, frozen at 0.0f, whereas radius is ALREADY computed
            // with the binary's exact formula by EntityManager.cpp:558, anchor 0x467d87).
            // The binary indexes by this+220 = the ALREADY-locked target, NOT the monster i examined.
            float engageRange = 0.0f;
            if (currentTargetIndex_ >= 0 && static_cast<std::size_t>(currentTargetIndex_) < g_World.monsters.size())
                engageRange = g_World.monsters[static_cast<std::size_t>(currentTargetIndex_)].radius;
            inRange = dist <= static_cast<float>(cost) + engageRange;
        }

        // (0x458548 / 0x458596) `available = dword_176703C[70*i] != 1`, with dword_176703C = base+0xC8.
        // PROVEN ALWAYS TRUE HERE — phase locking between +0xC8 and the +0x18 state:
        //   * +0xC8 is ONLY written by Char_UpdateMotionState 0x5816A0: unconditional reset
        //     `[eax+0C8h]=0` @0x5816cf, then `[ecx+0C8h]=1` @0x581939 in the SOLE case 12 of the
        //     switch on [ecx+18h] (= the state). `find_bytes` confirms 0x581939 as the ONLY site
        //     in the binary writing 1 to this offset.
        //   * Char_UpdateMotionState has only 2 callers, both in Pkt_SpawnMonster 0x467B00, and
        //     each IMMEDIATELY follows a state write: spawn @0x467cef -> @0x467dc4; update
        //     mode==1 @0x467e88 -> @0x467ea9. On update mode!=1, the 0x48-byte state block is
        //     saved @0x467dff / restored @0x467e44: neither +0x18 nor +0xC8 move.
        //   => invariant: dword_176703C[i] == 1  <=>  dword_1766F8C[i] == 12.
        // But the `continue` on monState == 12 above executes BEFORE this point: the state
        // therefore cannot be 12 here, so +0xC8 cannot be 1, so the expression is ALWAYS true.
        // This is a property of the BINARY (not of our port): it will remain true even if
        // Char_UpdateMotionState is ported someday. Do NOT "fix" this by inventing an aggro
        // writer: the original comment ("==1 => already engaged by a third party") was WRONG —
        // +0xC8 is a DEATH motion latch, not a target reservation.
        if (inRange)
            InsertTargetSorted(static_cast<int32_t>(i), dist, /*available=*/true);
    }

    return targetCount_ != 0;
}

// AutoPlay_SelectTarget 0x4585E0
int32_t AutoPlaySystem::SelectTarget() {
    if (!externalState.worldReady) return 0; // faithful: returns 0 (not -1) if the world isn't ready

    const PlayerEntity& self = g_World.Self();
    const float speed = host.GetSelfMoveSpeed ? host.GetSelfMoveSpeed() : 1.0f;

    for (uint16_t i = 0; i < targetCount_; ++i) {
        if (!targets_[i].available) continue; // only "free" slots are candidates
        currentTargetIndex_ = targets_[i].monsterIndex;
        if (currentTargetIndex_ < 0) continue;

        bool ok = false;
        const std::size_t idx = static_cast<std::size_t>(currentTargetIndex_);
        if (idx < g_World.monsters.size()) {
            const MonsterEntity& mon = g_World.monsters[idx];
            if (mon.def) {
                // (0x4586a9 / 0x4586c1) same state filter as BuildTargetList — see AP-06.
                const int32_t monState = AutoTarget_MonsterActionState(mon);
                if (monState != 0 && monState != 12 && mon.active
                    && ReadI32(mon.def, kDefOffMonCat232) <= 3
                    && ReadI32(mon.def, kDefOffMonCat236) <= 1) {
                    float outX = mon.x, outY = self.y, outZ = mon.z;
                    if (host.SlideMove)
                        host.SlideMove(self.x, self.y, self.z, mon.x, self.y, mon.z, speed, kFixedDt, outX, outY, outZ);
                    ok = (outX == mon.x && outZ == mon.z);
                }
            }
        }

        if (ok) return currentTargetIndex_;
        ClearTargetSlot();
    }
    return -1;
}

// AutoPlay_CountTargetsInRange 0x458C10
bool AutoPlaySystem::CountTargetsInRangeAtLeastThreshold() {
    int32_t withinRange = 0;
    const int32_t aoeCost = Skill_CostById(static_cast<int>(config.skillAoE), g_World.self, g_World.db.item);

    for (uint16_t i = 0; i < targetCount_; ++i) {
        const int32_t monsterIndex = targets_[i].monsterIndex;
        if (monsterIndex < 0 || static_cast<std::size_t>(monsterIndex) >= g_World.monsters.size())
            continue;
        const std::size_t idx = static_cast<std::size_t>(monsterIndex);
        const MonsterEntity& mon = g_World.monsters[idx];
        // (0x458d12 / 0x458d25) same state filter as BuildTargetList — see AP-06.
        const int32_t monState = AutoTarget_MonsterActionState(mon);
        const bool valid = mon.def && mon.active && monState != 0 && monState != 12
                            && ReadI32(mon.def, kDefOffMonCat232) <= 3
                            && ReadI32(mon.def, kDefOffMonCat236) <= 1;
        if (!valid) {
            RemoveTargetByMonsterIndex(monsterIndex);
            continue;
        }

        const float dist = DistanceToPlayer(mon.x, mon.y, mon.z);
        // (0x458dd1) `+ *((float*)&unk_1766FD8 + 70*idx)` = MonsterEntity::radius — see AP-05. NB:
        // here the binary indexes the EXAMINED monster (idx), not the locked target (unlike
        // BuildTargetList @0x458417 which indexes this+220).
        if (dist <= static_cast<float>(aoeCost) + mon.radius)
            ++withinRange;
        if (withinRange >= config.aoeThreshold)
            return true;
    }
    return false;
}

// AutoPlay_FindWalkableAdjacent 0x4580C0 — probes the 8 cardinal/diagonal directions
// (exact order of the original switch) at 10 units, returns the first reachable one.
bool AutoPlaySystem::FindWalkableAdjacent(float& outX, float& outY, float& outZ) const {
    if (!externalState.worldReady) return false;

    static constexpr float kDirs[8][2] = {
        {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {-1.0f, 1.0f},
        {-1.0f, 0.0f}, {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f},
    };

    const PlayerEntity& self = g_World.Self();
    const float speed = host.GetSelfMoveSpeed ? host.GetSelfMoveSpeed() : 1.0f;

    for (const auto& d : kDirs) {
        const float tx = d[0] * 10.0f + self.x;
        const float ty = self.y;
        const float tz = d[1] * 10.0f + self.z;
        float rx = tx, ry = ty, rz = tz;
        if (host.SlideMove)
            host.SlideMove(self.x, self.y, self.z, tx, ty, tz, speed, kFixedDt, rx, ry, rz);
        if (rx == tx && rz == tz) {
            outX = tx; outY = ty; outZ = tz;
            return true;
        }
    }
    return false;
}

// AutoPlay_UpdateTargeting 0x45D080
bool AutoPlaySystem::UpdateTargeting(float dt) {
    rebuildTimerSec_ += dt;

    const bool locked = IsTargetLocked(currentTargetIndex_);
    if (rebuildTimerSec_ <= kRebuildIntervalSec || locked) {
        if (locked || SelectTarget() >= 0) {
            // Pending attack order: faithful to dword_1687354/1687358/1687350 AND their mirror
            // dword_1675B28/1675B2C/1675B24 — written ONLY if the self anim is at the very
            // beginning (0 <= frame < 1), as in the binary. Original
            // dword_1766F7C/1766F78 = EntityId {lo,hi} of the targeted monster -> exposed via
            // MonsterEntity::id.
            if (g_Client.VarF(0x168732C) >= 0.0f && g_Client.VarF(0x168732C) < 1.0f) {
                const std::size_t idx = static_cast<std::size_t>(currentTargetIndex_);
                if (idx < g_World.monsters.size()) {
                    const EntityId& tid = g_World.monsters[idx].id;
                    g_Client.Var(0x1687354) = static_cast<int32_t>(tid.hi);
                    g_Client.Var(0x1675B28) = static_cast<int32_t>(tid.hi);
                    g_Client.Var(0x1687358) = static_cast<int32_t>(tid.lo);
                    g_Client.Var(0x1675B2C) = static_cast<int32_t>(tid.lo);
                    g_Client.Var(0x1687350) = 5;
                    g_Client.Var(0x1675B24) = 5;
                }
            }
            return true;
        }
        g_Client.Var(0x1687354) = 0;
        g_Client.Var(0x1675B28) = 0;
        g_Client.Var(0x1687358) = -1;
        g_Client.Var(0x1675B2C) = -1;
        g_Client.Var(0x1687350) = 5;
        g_Client.Var(0x1675B24) = 5;
        ResetTargetList();
        return false;
    }

    if (BuildTargetList())
        rebuildTimerSec_ = 0.0f; // faithful: the timestamp is only updated on success
    return false;
}

// AutoPlay_FindNpcTarget 0x458E90
int32_t AutoPlaySystem::FindNpcTarget() const {
    int32_t result = -1;

    for (std::size_t i = 0; i < g_World.npcs.size(); ++i) {
        const NpcEntity& npc = g_World.npcs[i];
        if (!npc.active) continue;
        if (host.ShouldRefreshNpc && !host.ShouldRefreshNpc(npc)) continue;
        if (!npc.def) continue;
        if (ReadI32(npc.def, kDefOffNpcKind) != 1) continue;

        const uint32_t offerWeight = ReadU32(npc.body.data(), kNpcBodyOffOffer);
        if (static_cast<int64_t>(offerWeight) + g_Client.inv.weight > 2000000000LL) {
            result = -1;
            break; // faithful: abandons the 1st pass (not the whole search, see below)
        }
        if (config.mode != 1) return static_cast<int32_t>(i);

        const float nx = ReadF32(npc.body.data(), kNpcBodyOffPosX);
        const float ny = ReadF32(npc.body.data(), kNpcBodyOffPosY);
        const float nz = ReadF32(npc.body.data(), kNpcBodyOffPosZ);
        if (DistanceToPlayer(nx, ny, nz) < kNpcInteractRange) return static_cast<int32_t>(i);
    }

    if (result != -1) return result;

    // 2nd pass: friend NPC (by name, def+4) or NPC of a faction targeted by the PK mask
    // (def+184 -> bit) and not an enemy.
    for (std::size_t j = 0; j < g_World.npcs.size(); ++j) {
        const NpcEntity& npc = g_World.npcs[j];
        if (!npc.active) continue;
        if (host.ShouldRefreshNpc && !host.ShouldRefreshNpc(npc)) continue;
        if (!npc.def) continue;

        const char* ownerName = reinterpret_cast<const char*>(npc.def) + kDefOffName;
        const float nx = ReadF32(npc.body.data(), kNpcBodyOffPosX);
        const float ny = ReadF32(npc.body.data(), kNpcBodyOffPosY);
        const float nz = ReadF32(npc.body.data(), kNpcBodyOffPosZ);

        if (IsFriendName(ownerName)) {
            if (config.mode != 1) return static_cast<int32_t>(j);
            if (DistanceToPlayer(nx, ny, nz) < kNpcInteractRange) return static_cast<int32_t>(j);
            continue;
        }

        const int32_t faction = ReadI32(npc.def, kDefOffFaction);
        int32_t bit = 0;
        switch (faction) {
            case 1: bit = 1; break;
            case 2: bit = 2; break;
            case 3: bit = 4; break;
            case 4: bit = 8; break;
            default: bit = 0; break;
        }
        if (!bit) return -1; // faithful: unknown faction -> immediate abandonment of the WHOLE search

        if ((config.pkFactionMask & static_cast<uint32_t>(bit)) == static_cast<uint32_t>(bit)
            && !IsEnemyName(ownerName)) {
            if (config.mode != 1) return static_cast<int32_t>(j);
            if (DistanceToPlayer(nx, ny, nz) < kNpcInteractRange) return static_cast<int32_t>(j);
        }
    }

    return result;
}

// AutoPlay_MoveToNpc 0x45C5C0
bool AutoPlaySystem::MoveToNpc() {
    const int32_t npcIdx = FindNpcTarget();
    if (npcIdx < 0) return false;

    if (npcInteractCooldownSec_ < kNpcInteractCooldownSec) return true;
    if (externalState.sceneTransitionBlocking) return true; // sub_53B9E0

    const NpcEntity& npc = g_World.npcs[static_cast<std::size_t>(npcIdx)];
    if (!npc.def) return false;

    if (ReadI32(npc.def, kDefOffNpcKind) == 1) {
        if (host.InteractNpc) host.InteractNpc(npc.id);
        return true;
    }

    // "Pickup offer" NPC: attempts to place the item/quantity in the bag before interacting.
    const uint32_t offerItemId = ReadU32(npc.body.data(), kNpcBodyOffDefId);
    const uint32_t offerWeight = ReadU32(npc.body.data(), kNpcBodyOffOffer);
    const int32_t placedRow = host.TryPlaceItemIntoBag ? host.TryPlaceItemIntoBag(offerItemId, offerWeight) : -1;
    if (placedRow >= 0) {
        if (host.InteractNpc) host.InteractNpc(npc.id);
        npcInteractCooldownSec_ = 0.0f;
        return true;
    }

    if (config.warpOnStuck) {
        const bool attacking = host.IsSelfAttacking ? host.IsSelfAttacking() : false;
        if (externalState.warpSuppressed || !attacking) return true;
        if (host.WarpToFactionTown) host.WarpToFactionTown();
        // faithful: no return here -> falls through to the final `return false`.
    }
    return false;
}

// AutoPlay_IsMobOfFaction 0x45BE80
bool AutoPlaySystem::IsMobOfFaction(bool secondTier, int32_t monsterDefId) const {
    if (secondTier) {
        static constexpr int32_t kPairs[9][2] = {
            {10, 112}, {14, 113}, {18, 114}, {29, 115}, {33, 116},
            {37, 117}, {48, 118}, {52, 119}, {56, 120},
        };
        for (int32_t c = 0; c < 9; ++c)
            if (PlayerIsCharClass(c) && (monsterDefId == kPairs[c][0] || monsterDefId == kPairs[c][1]))
                return true;
        return false;
    }
    static constexpr int32_t kOctets[9][8] = {
        {8, 9, 58, 59, 85, 86, 121, 122},
        {12, 13, 60, 61, 87, 88, 123, 124},
        {16, 17, 62, 63, 89, 90, 125, 126},
        {27, 28, 64, 65, 91, 92, 127, 128},
        {31, 32, 66, 67, 93, 94, 129, 130},
        {35, 36, 68, 69, 95, 96, 131, 132},
        {46, 47, 70, 71, 97, 98, 133, 134},
        {50, 51, 72, 73, 99, 100, 135, 136},
        {54, 55, 74, 75, 101, 102, 137, 138},
    };
    for (int32_t c = 0; c < 9; ++c) {
        if (!PlayerIsCharClass(c)) continue;
        for (int32_t k = 0; k < 8; ++k)
            if (monsterDefId == kOctets[c][k]) return true;
    }
    return false;
}

// AutoPlay_IsMobCategory2 0x45C2F0
bool AutoPlaySystem::IsMobCategory2(int32_t classId, int32_t monsterDefId) const {
    switch (classId) {
        case 0:
        case 1: {
            if (PlayerIsElementalAffinity(0) && monsterDefId == 7) return true;
            if (PlayerIsElementalAffinity(1) && monsterDefId == 26) return true;
            if (PlayerIsElementalAffinity(2) && monsterDefId == 45) return true;
            static constexpr int32_t kClassIds[9] = {11, 15, 19, 30, 34, 38, 49, 53, 57};
            for (int32_t c = 0; c < 9; ++c)
                if (PlayerIsCharClass(c) && monsterDefId == kClassIds[c]) return true;
            return false;
        }
        case 2: case 3: case 4: case 5: case 6: case 7:
            return monsterDefId == 82 || monsterDefId == 83 || monsterDefId == 84
                || monsterDefId == 103 || monsterDefId == 104 || monsterDefId == 105;
        default:
            return false;
    }
}

// Player_IsCharClass 0x45C550 / Player_IsInStance 0x45C480 / sub_45C590 0x45C590 —
// all three read the ITEM_INFO of externalState.classItemId (dword_1673248 in the binary),
// typeCode field (+188, see GameDatabase.h::ItemInfo).
bool AutoPlaySystem::PlayerIsCharClass(int32_t classIdx) const {
    const ItemInfo* info = GetItemInfo(externalState.classItemId);
    return info && info->typeCode == static_cast<uint32_t>(classIdx + 13);
}

bool AutoPlaySystem::PlayerIsInStance(int32_t stance) const {
    const ItemInfo* info = GetItemInfo(externalState.classItemId);
    if (!info) return false;
    const uint32_t t = info->typeCode;
    if (stance == 0) return t == 13 || t == 16 || t == 19;
    if (stance == 1) return t == 14 || t == 17 || t == 20;
    if (stance == 2) return t == 15 || t == 18 || t == 21;
    return false;
}

bool AutoPlaySystem::PlayerIsElementalAffinity(int32_t elementIdx) const {
    return g_World.self.element == elementIdx || g_World.self.elementSecondary == elementIdx;
}

// AutoPlay_IsFriend 0x45FAA0 / AutoPlay_IsEnemy 0x45FBE0 — linear search by string equality
// (Crt_Strcmp) in the original's this+296/this+324 linked lists, reproduced as
// std::vector<std::string>.
bool AutoPlaySystem::IsFriendName(const char* name) const {
    if (!name) return false;
    for (const auto& n : friendNames)
        if (n == name) return true;
    return false;
}
bool AutoPlaySystem::IsEnemyName(const char* name) const {
    if (!name) return false;
    for (const auto& n : enemyNames)
        if (n == name) return true;
    return false;
}

// AutoPlay_LoadFriendList 0x45D730 / AutoPlay_LoadEnemyList 0x45DAF0 — read + validation.
// (shared body LoadNameListFile above, anchored slot by slot).
bool AutoPlaySystem::LoadFriendList() {
    return LoadNameListFile(kFriendListPath, friendNames); // this+296
}
bool AutoPlaySystem::LoadEnemyList() {
    return LoadNameListFile(kEnemyListPath, enemyNames);   // this+324
}

// AutoPlay_SaveFriendList 0x45DE50 / AutoPlay_SaveEnemyList 0x45E140 — writes 1200 bytes.
bool AutoPlaySystem::SaveFriendList() {
    // this+320 = size: > 48 => List_Clear + return 0 WITHOUT writing (@0x45DEC0).
    return SaveNameListFile(kFriendListPath, friendNames, SaveClamp::FriendReturnOnOverflow);
}
bool AutoPlaySystem::SaveEnemyList() {
    // this+348 = size: > 48 => List_Clear THEN writes (@0x45E1A3, no return — faithful asymmetry).
    return SaveNameListFile(kEnemyListPath, enemyNames, SaveClamp::EnemyClearAndContinue);
}

// AutoPlay_Start 0x45D580 — resets the targets, arms auto-hunt, LOADS the lists.
// SOLE binary caller: UI_InitAllDialogs 0x5ABF50 @0x5AC193 (= GameWindows::Init on the
// ClientSource side) -> to be wired to HUD init (see AutoPlaySystem.h::Start, front report).
bool AutoPlaySystem::Start() {
    targetCount_ = 0;                 // this+216 = 0            @0x45d58c
    currentTargetIndex_ = -1;         // this+220 = -1           @0x45d596
    rebuildTimerSec_ = 0.0f;          // this+228 = GetTickCount @0x45d5d3 (dt accumulator -> 0)
    npcInteractCooldownSec_ = 0.0f;   // this+240 = GetTickCount @0x45d600 (same: delta ~0 at start)
    // this+24/28/32/224 + timers this+232/236/244/248 (wander/action state + 4 clocks)
    // @0x45d5a5-0x45d61e: wander state/combat tail NOT modeled (see Update TODO).
    huntArmed_ = true;                // this+288 = 1            @0x45d641 (arms the OR chain)
    invDirtyStartLatch_ = true;       // this+284 = 1            @0x45d634 (flush inv-dirty latch)
    // this+280 = 1 @0x45d627 (actionStateLatch, feeds the deferred combat tail); this+292(w)=0
    // / this+294(w)=1 @0x45d650/@0x45d65f: UI/wander state not modeled.
    ResetTargetList();                //                          @0x45d669
    // this+252/256/260 = player pos snapshot (flt_1687330/34/38) + memset this+264..275 (wander
    // target) @0x45d677-0x45d6b3: wander state, not modeled (UpdateWander 0x45D200 not ported,
    // see Update TODO).
    config.aoeThreshold = 3;          // g_AutoHuntAoEThreshold = 3   @0x45d6bd
    // g_AutoHuntSettingsDirty = 1 @0x45d6c7: "settings changed" UI flag (autoplay panel), no
    // consumer in this scope — not modeled.
    if (!config.pkFactionMask)        //                              @0x45d6d8
        config.pkFactionMask = 2;     // g_AutoHuntPkFactionMask = 2  @0x45d6da (default category)
    config.useTownItem = false;       // g_AutoHuntUseTownItem = 0    @0x45d6e4
    LoadFriendList();                 // AutoPlay_LoadFriendList      @0x45d6f1  <-- POPULATION (D1)
    LoadEnemyList();                  // AutoPlay_LoadEnemyList       @0x45d6f9  <-- POPULATION (D1)
    // cQuickSlotWin_Close + focus edit-box @0x45d703-0x45d71f: UI (out of scope).
    return true;                      //                              @0x45d729
}

// AutoPlay_HasRequiredItems 0x45CC10 — searches the pickup grid (3 containers x 14 slots,
// g_EntityManager.PickupSlot) THEN the main inventory (g_Client.inv) for two material
// categories; stops as soon as both are found.
namespace {
void ClassifyMaterial(uint32_t itemId, bool& hasA, bool& hasB) {
    switch (itemId) {
        case 32: case 33: case 34: hasA = true; hasB = true; break;
        case 26: case 27: case 28: case 29: case 30: case 31: hasB = true; break;
        case 2: case 3: case 4: case 23: case 24: case 25: hasA = true; break;
        default: break;
    }
}
} // namespace

bool AutoPlaySystem::HasRequiredItems() const {
    bool hasA = false, hasB = false;

    for (uint32_t container = 0; container < 3 && !(hasA && hasB); ++container) {
        for (uint32_t slot = 0; slot < EntityManager::kSlotsPerContainer && !(hasA && hasB); ++slot) {
            const GroundPickupSlot* s = g_EntityManager.PickupSlot(container, slot);
            if (s && s->aux == 3)
                ClassifyMaterial(s->itemId, hasA, hasB);
        }
    }
    if (hasA && hasB) return true;

    const int32_t pages = (externalState.invExtraPageCount > 0) ? 2 : 1;
    for (int32_t page = 0; page < pages && !(hasA && hasB); ++page) {
        for (uint32_t col = 0; col < InventoryState::kCols && !(hasA && hasB); ++col) {
            ClassifyMaterial(g_Client.inv.cells[static_cast<uint32_t>(page) * InventoryState::kCols + col].itemId,
                              hasA, hasB);
        }
    }
    return hasA && hasB;
}

// Body shared by AutoPlay_CheckReturnScroll 0x45C750 / AutoPlay_CheckTownScroll 0x45C9B0.
bool AutoPlaySystem::CheckConsumableScroll(uint32_t itemId, int strTableId, bool& enabledToggle) {
    // 1) quick pickup grid: 3 containers x 14 slots (dword_1674404==3 == occupied slot ->
    //    GroundPickupSlot::aux==3, see EntityManager.h).
    for (uint32_t container = 0; container < 3; ++container) {
        for (uint32_t slot = 0; slot < EntityManager::kSlotsPerContainer; ++slot) {
            const GroundPickupSlot* s = g_EntityManager.PickupSlot(container, slot);
            if (!s || s->aux != 3 || s->itemId != itemId || s->count == 0) continue;

            if (externalState.morphInProgress) return true;      // dword_1675A88 == 1
            if (pendingItemUseLatch_) return true;                // dword_1675B08 already armed

            pendingItemUseContainer_ = static_cast<int32_t>(container);
            pendingItemUseSlot_ = static_cast<int32_t>(slot);
            if (host.SendUseGroundPickupItem && host.SendUseGroundPickupItem(pendingItemUseContainer_, pendingItemUseSlot_)) {
                pendingItemUseLatch_ = true;
                pendingItemUseTimeSec_ = g_World.gameTimeSec;
            }
            return true;
        }
    }

    // 2) absent from the pickup grid: already in the main inventory?
    const int32_t pages = (externalState.invExtraPageCount > 0) ? 2 : 1;
    for (int32_t page = 0; page < pages; ++page) {
        for (uint32_t col = 0; col < InventoryState::kCols; ++col) {
            if (g_Client.inv.cells[static_cast<uint32_t>(page) * InventoryState::kCols + col].itemId == itemId)
                return false; // present -> nothing to do this frame
        }
    }

    // 3) not found anywhere: system message, disables the toggle, notifies the server.
    g_Client.msg.System(Str(strTableId));
    enabledToggle = false;
    if (host.NotifyInventoryDirty) host.NotifyInventoryDirty(externalState.invDirtyEnable);
    return false;
}

// AutoPlay_CheckReturnScroll 0x45C750 (item 1001)
bool AutoPlaySystem::CheckReturnScroll() {
    const ItemInfo* selInfo = GetItemInfo(externalState.selectedInvItemId);
    if (!selInfo) return false;
    if (selInfo->typeCode == 28 || selInfo->typeCode == 31 || selInfo->typeCode == 32) return false;

    if (!config.useReturnScroll) return false;
    if (static_cast<int32_t>(externalState.selectedInvItemId) <= 0) return false;
    if (externalState.selectedInvCounter >= 50) return false;

    return CheckConsumableScroll(1001, /*StrTable005*/ 1793, config.useReturnScroll);
}

// AutoPlay_CheckTownScroll 0x45C9B0 (item 563)
bool AutoPlaySystem::CheckTownScroll() {
    if (!config.useTownItem) return false;
    if (externalState.talismanSlot <= 9 || externalState.talismanSlot >= 20) return false;

    int32_t hi = 0, lo = 0;
    const std::size_t slot = static_cast<std::size_t>(externalState.talismanSlot);
    UnpackCombined(slot < externalState.talismanPacked.size() ? externalState.talismanPacked[slot] : 0, hi, lo);
    if (hi > 50) return false;

    return CheckConsumableScroll(563, /*StrTable005*/ 2185, config.useTownItem);
}

// (REMOVED 2026-07-16) AutoPlaySystem::Ext() — the per-monster extension MonsterAutoplayExt had
// no writer: its 3 fields are now read from the real, already-populated data
// (MonsterEntity::body+8 via AutoTarget_MonsterActionState, MonsterEntity::radius), and
// `available` is proven constant. See the banner in AutoPlaySystem.h (AP-05/AP-06).

// AutoPlay_Update 0x45E770 — main auto-hunt tick (replaces the old fabricated orchestration:
// that was the root cause of defect D2, where MoveToNpc was never called so the friend/enemy
// lists were never queried). This port covers the reachable BACKBONE: entry guards -> inv-dirty
// latch -> materials throttle -> OR chain (loot/NPC/consumables, including MoveToNpc which
// queries the lists) -> monster targeting (UpdateTargeting). The "combat tail"
// (Player_CastSkill / Net_QueueRunTo), not transcribed in ClientSource, is DEFERRED.
void AutoPlaySystem::Update(float dt) {
    // this[60] (byte +240) is a GetTickCount timestamp in the binary; modeled by a dt
    // accumulator advanced every frame (same wall-clock delta since the last reset).
    npcInteractCooldownSec_ += dt;

    // Entry guard, exact mirror of 0x45e779-0x45e794:
    //     cmp g_InvDirtyEnable(0x16755AC), 1 ; jnz loc_45E794      -> exit if != 1
    //     cmp g_AutoHuntFuelA(0x16755A4), 0  ; jg  loc_45E79B      -> pass if A > 0
    //     cmp g_AutoHuntFuelB(0x16755A8), 0  ; jg  loc_45E79B      -> pass if B > 0
    //     loc_45E794: jmp loc_45ED71                                -> exit
    // FIX AP-01: the fuel used to be read from externalState.autoHuntFuelA/B, two fields that
    // NOBODY wrote in src/ (split-brain) -> the guard exited on the 1st line FOREVER and the
    // entire autoplay tick (including UpdateTargeting, the sole emitter of the attack order)
    // was dead. The fuel is 100% SERVER-SIDE: we now read the storage ACTUALLY written by the
    // network path, g_Client.Var(0x16755A4/A8) <- Pkt_SetGameVar 0x468370 cases 61/62/90
    // (Net/GameVarDispatch.cpp:389/395/430/481). See the banner in AutoPlaySystem.h for the
    // xref proof that the UI (AutoPlay_OnMouseUpMain 0x45A980) does NOT touch it.
    // invDirtyEnable (0x16755AC) stays on externalState: it's the only one of the two C++
    // storages with a real writer (UI/AutoPlayWindow.cpp:234/243). The mirror
    // g_Client.Var(0x16755AC), meanwhile, has readers only — a SYMMETRIC and INVERSE
    // split-brain, out of scope for this front (I don't own the writer file), flagged in the
    // report.
    if (!externalState.invDirtyEnable
        || (g_Client.VarGet(0x16755A4) <= 0 && g_Client.VarGet(0x16755A8) <= 0))
        return;

    // One-shot latch armed by Start (this+284) @0x45e79e: flushes inv-dirty to the server then
    // disarms the tracking (g_InvDirtyEnable=0 + Net_SendOp99(&g_AutoPlayMgr, 0)). Re-arming
    // invDirtyEnable=true is the inventory subsystem's responsibility (out of scope), otherwise
    // the rest never runs again — faithful to the binary (see front report).
    if (invDirtyStartLatch_) {
        externalState.invDirtyEnable = false;                            // g_InvDirtyEnable = 0 @0x45e7a7
        if (host.NotifyInventoryDirty) host.NotifyInventoryDirty(false); // Net_SendOp99(...,0) @0x45e7bd
        invDirtyStartLatch_ = false;                                     // this+284 = 0 @0x45e7c5
        return;                                                          // @0x45e7cf
    }

    // Purge of quick-skills 2..7 if slots are not unlocked @0x45e7db-0x45e806
    // (g_AutoHuntSkillSlotUnlocks / g_AutoHuntQuickSkills / dword_16755B8): quick-slot UI state
    // NOT modeled in ClientSource.
    // TODO [ancre AutoPlay_Update 0x45E7DB]: purge of auto skill shortcuts (UI front).

    // 2000 ms throttle @0x45e827 (GetTickCount() - this[60] > 0x7D0): beyond it, requires the
    // needed materials; failing that, warps to the faction town and exits the tick.
    if (npcInteractCooldownSec_ > 2.0f) {
        // this[62] = GetTickCount() @0x45e832: secondary clock with no consumer -> not modeled.
        if (!HasRequiredItems()) {                                       // AutoPlay_HasRequiredItems @0x45e83b
            if (host.WarpToFactionTown) host.WarpToFactionTown();        // Map_BeginWarpToFactionTownEx @0x45e84b
            return;                                                      // @0x45e850
        }
    }

    // OR chain @0x45e8b5 (guarded by huntArmed_ = this+288). MoveToNpc calls FindNpcTarget
    // 0x458E90, which QUERIES friendNames/enemyNames (IsFriendName @0x458FEB / IsEnemyName
    // @0x4590D1): this is THE runtime point that makes the 011/012.BIN lists actually consulted
    // on every armed tick (fix for defect D2). Faithful || short-circuit.
    // Two members of the binary chain are NOT ported (sub-features out of scope for the
    // "autoplay-lists" front; their non-triggering == "absent" defect == false, neutral in the ||):
    //   - AutoPlay_ScanGroundItems 0x45E4E0 (auto-loot inventory grid + Net_SendPacket_Op23),
    //     between MoveToNpc and CheckReturnScroll in the binary.  TODO [ancre 0x45E4E0].
    //   - AutoPlay_ValidateChatName 0x45E670 (guild name validation + Net_SendOp71), same.
    //     TODO [ancre 0x45E670].
    if (huntArmed_ && (MoveToNpc()          // 0x45C5C0 -> FindNpcTarget -> IsFriendName/IsEnemyName
                       || CheckReturnScroll()  // 0x45C750
                       || CheckTownScroll()))  // 0x45C9B0
        return;                                 // @0x45e8bc

    // Monster targeting @0x45e8c9 (mode==1) / @0x45ea95 (mode!=1). BOTH branches converge on
    // UpdateTargeting 0x45D080 (ported: writes the attack order dword_1687354/58, read by the
    // combat tick). In mode!=1 only, AutoPlay_UpdateWander 0x45D200 (wandering, NOT ported)
    // precedes it and can short-circuit (`if (UpdateWander() || !UpdateTargeting()) return;`):
    // treated here as "absent" (false) -> reduces to UpdateTargeting alone, same as mode==1.
    // TODO [ancre AutoPlay_UpdateWander 0x45D200]: random wandering (movement front).
    if (!UpdateTargeting(dt))                   // @0x45e913 (mode==1) / @0x45ead7 (mode!=1)
        return;

    // TODO [ancre AutoPlay_Update 0x45E91D-0x45ED54]: "combat tail" — CountTargetsInRange
    // 0x458C10 + Player_CastSkill 0x53BC40 (AoE param 1 / single param 2) + Net_QueueRunTo
    // 0x511B00 (approach). OUT OF SCOPE for the "autoplay-lists" front: Player_CastSkill /
    // Net_QueueRunTo are not transcribed in ClientSource (see SkillCombat.h:388,
    // "Player_CastSkill written but called by no one") — porting them here would be dead code.
    // Belongs to the combat/skill front. The g_SelfActionState[0] latch (this+280/236
    // @0x45e8db/@0x45ea95) that feeds this tail is therefore not reproduced either.
}

} // namespace ts2::game

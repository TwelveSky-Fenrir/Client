// Game/AutoPlaySystem.h — TwelveSky2 autoplay (automatic farming) system.
//
// Faithful C++ rewrite of the IDA "AutoPlay_*" cluster (EA 0x457EA0..0x45D080), which is
// the original client's automatic targeting/farming STATE MACHINE ("this" object allocated
// somewhere in the player context, never explicitly named in the IDB — only its shape is
// known via AutoPlay_Construct). It handles:
//   - a 15-slot monster target list sorted by distance (AutoPlay_*TargetList/Sorted/Slot),
//   - selecting/locking the current target and firing the attack order,
//   - finding/interacting with an NPC (reward pickup, vendor, PK friend/enemy),
//   - automatic use of return/town scrolls,
//   - faction/category classification of a monster for automatic PK.
//
// Original functions covered (EA -> method):
//   AutoPlay_Construct              0x457EA0 -> AutoPlaySystem::AutoPlaySystem()
//   AutoPlay_BuildTargetList        0x458280 -> BuildTargetList()
//   AutoPlay_SelectTarget           0x4585E0 -> SelectTarget()
//   AutoPlay_InsertTargetSorted     0x458870 -> InsertTargetSorted()
//   AutoPlay_DistanceToPlayer       0x4589E0 -> DistanceToPlayer()
//   AutoPlay_IsTargetLocked         0x458B80 -> IsTargetLocked()
//   AutoPlay_CountTargetsInRange    0x458C10 -> CountTargetsInRangeAtLeastThreshold()
//   AutoPlay_RemoveTargetById       0x458E00 -> RemoveTargetByMonsterIndex()
//   AutoPlay_FindNpcTarget          0x458E90 -> FindNpcTarget()
//   AutoPlay_FindWalkableAdjacent   0x4580C0 -> FindWalkableAdjacent()
//   AutoPlay_IsMobOfFaction         0x45BE80 -> IsMobOfFaction()
//   AutoPlay_IsMobCategory2         0x45C2F0 -> IsMobCategory2()
//   AutoPlay_MoveToNpc              0x45C5C0 -> MoveToNpc()
//   AutoPlay_CheckReturnScroll      0x45C750 -> CheckReturnScroll()
//   AutoPlay_CheckTownScroll        0x45C9B0 -> CheckTownScroll()
//   AutoPlay_HasRequiredItems       0x45CC10 -> HasRequiredItems()
//   AutoPlay_UpdateTargeting        0x45D080 -> UpdateTargeting()
//   AutoPlay_ClearTargetSlot        0x4587E0 -> ClearTargetSlot()
//   AutoPlay_ResetTargetList        0x458AB0 -> ResetTargetList()
//   AutoPlay_Start                  0x45D580 -> Start()  (reset+arming+list loading)
//   AutoPlay_LoadFriendList         0x45D730 -> LoadFriendList()  (G02_GINFO\011.BIN)
//   AutoPlay_LoadEnemyList          0x45DAF0 -> LoadEnemyList()   (G02_GINFO\012.BIN)
//   AutoPlay_SaveFriendList         0x45DE50 -> SaveFriendList()
//   AutoPlay_SaveEnemyList          0x45E140 -> SaveEnemyList()
//   AutoPlay_Update                 0x45E770 -> Update()  (main tick: loot/list spine)
// + small utilities called by the cluster (translated because they're required):
//   Player_IsCharClass  0x45C550, Player_IsInStance 0x45C480, sub_45C590 (element affinity),
//   AutoPlay_IsFriend 0x45FAA0, AutoPlay_IsEnemy 0x45FBE0, Stat_UnpackCombined 0x54CE40.
//
// SCOPE: the targeting/farming state machine, NOT the autoplay UI panel rendering (out of
// scope, see TODO below). Third-party subsystems (movement collision, networking, bag item
// placement, local player class/stance in the broad sense) are NOT modeled in
// Game/GameState.h or the other shared headers: they are exposed via explicit integration
// points (host/externalState below), documented with their original EA, to be wired by the
// caller.
//
// Autonomy: this module includes ONLY the shared headers listed in the mission
// (GameState.h, GameDatabase.h, ClientRuntime.h, SkillSystem.h, EntityManager.h) + the STL.
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <string>
#include <functional>

#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/ClientRuntime.h"
#include "Game/SkillSystem.h"
#include "Game/EntityManager.h"

namespace ts2::game {

// User settings for auto-farming (original g_AutoHunt* globals).
struct AutoPlayConfig {
    // g_AutoHuntMode 0x16755F4. 1 = "skill range" mode: BuildTargetList widens the list
    // around the ALREADY-locked target using the skill cost instead of testing
    // accessibility via collision sliding (see BuildTargetList()). Any other value (0
    // observed by default, 2 used elsewhere in the binary) follows the "accessibility"
    // branch. Also driven by FindNpcTarget/MoveToNpc (50-unit filter).
    int32_t  mode = 0;
    uint32_t skillSingle = 0;   // g_AutoHuntSkillSingle    0x16755F8 (0 = not configured)
    uint32_t skillAoE    = 0;   // g_AutoHuntSkillAoE       0x1675600
    int32_t  aoeThreshold = 0;  // g_AutoHuntAoEThreshold   0x1675608 (target count to cast the AoE)
    bool     useReturnScroll = false; // g_AutoHuntUseReturnScroll 0x1675618
    bool     useTownItem     = false; // g_AutoHuntUseTownItem     0x167561C
    uint32_t pkFactionMask   = 0;     // g_AutoHuntPkFactionMask   0x167560C (bits 1/2/4/8 = factions 1..4)
    // g_AutoHuntBagFullReturn 0x1675610 (real name confirmed: xrefs AutoPlay_DrawSettingsPanel
    // 0x4593C0 / AutoPlay_OnClickSettings 0x45A0D0, AutoPlay settings panel; also accessed via
    // `(&g_AutoHuntMode)[7]` in MoveToNpc). Real semantics CONFIRMED by the original settings
    // panel: "return to town if bag full" — active when TryPlaceItemIntoBag fails (NPC pickup
    // blocked by lack of space) AND the player is attacking, warps to the faction town. The
    // name warpOnStuck below describes the EFFECT (warp), g_AutoHuntBagFullReturn describes
    // the real CAUSE as shown in the panel (dedicated checkbox, 2 toggle sprites at
    // x+131/x+205,y+257) — kept as-is to avoid breaking existing callers.
    bool     warpOnStuck = false;
};

// External state/globals not modeled in the shared headers (long tail). Default values
// chosen so the module stays functional without full wiring; to be synced by the caller
// (World/Net/UI) once those systems exist.
struct AutoPlayExternalState {
    bool worldReady = true;               // dword_14A88E8 (world/collision ready)
    // = !Game_IsSceneNotReady() 0x53B9E0: TRUE iff dword_1822390==1 AND dword_1822388==1 (scene
    // READY). POLARITY WARNING: Game_IsSceneNotReady() returns the INVERSE (true = scene NOT
    // ready, `dword_1822390!=1 || dword_1822388!=1`); this field stores ITS NEGATION. Read as-is
    // by MoveToNpc 0x45C608 (`if (!Game_IsSceneNotReady()) return 1;`, see .cpp:453) AND by
    // host.CanAutoInteractNpc (SceneManager.cpp). PRODUCER TO WIRE (out of scope): a tick must
    // write `sceneTransitionBlocking = !Game_IsSceneNotReady()` — currently NEVER written
    // (=false forever), so MoveToNpc treats the scene as never-ready (interaction branch always
    // taken). See report: wiring required once MoveToNpc becomes reachable from Update.
    bool sceneTransitionBlocking = false;
    bool warpSuppressed = false;          // dword_1675B00
    bool morphInProgress = false;         // dword_1675A88 (IDB name: g_MorphInProgress)
    bool invDirtyEnable = true;           // g_InvDirtyEnable 0x16755AC (argument of Net_SendOp99).
    // NB: continuously re-armed (=1) by inventory ops (Inv_Add/RemoveItemQuantity, 82 xrefs on
    // 0x16755AC); required =true for AutoPlay_Update's entry guard @0x45e792 to let it through.
    // Start's latch resets it to 0 once (server flush) — see Update().

    // ---- AUTO-HUNT FUEL: NO field here — storage is g_Client.Var (AP-01/AP-07) ----
    // g_AutoHuntFuelA/B 0x16755A4 / 0x16755A8 = remaining hunt credit/time. AutoPlay_Update
    // 0x45E770's entry guard @0x45e782 (`cmp g_AutoHuntFuelA,0 ; jg`) / @0x45e78b (same for B):
    // the tick exits on the 1st line (`jmp loc_45ED71` @0x45e794) while BOTH are <= 0.
    //
    // FIX AP-07 (2026-07-16) — the previous comment attributed arming to the UI ("Set > 0 by
    // AutoPlay_OnMouseUpMain 0x45A980"): that is REFUTED by the xrefs. 0x45A980 is ABSENT from
    // the 19 xrefs of 0x16755A4 and the 15 xrefs of 0x16755A8 — it only writes g_InvDirtyEnable
    // 0x16755AC. This fuel is 100% SERVER-SIDE; the IDB comment itself says so: "Remaining hunt
    // credit/time A (server)". REAL writers (disasm):
    //   Pkt_SetGameVar 0x468370        case 61 @0x468d30 / case 62 @0x468d5c / case 90 @0x469106
    //                                   (+ @0x468eae)
    //   Pkt_ItemActionDispatch 0x46A320 @0x478b4d / @0x481782 / @0x48317b
    //   Net_OnConfirmPromptOpen_Dlg10 0x490EE0 @0x490f93 / @0x490f9c
    //   Char_TickDeathRespawn 0x576CB0  @0x577ac3 / @0x577acc
    // READERS ONLY (do not confuse with writers): AutoPlay_IsActionAllowed 0x45D470, AutoPlay_Update
    // 0x45E770, AutoPlay_OnMouseDown 0x45EE30, UI_GameHud_Render 0x67A3C0.
    //
    // FIX AP-01 (2026-07-16) — two fields `autoHuntFuelA/B` used to live HERE and were read by
    // Update(): they had NO writer anywhere in src/ (split-brain), so the tick exited on its 1st
    // line FOREVER. REMOVED. The SINGLE source of truth on the ClientSource side is the server
    // path g_Client.Var(0x16755A4) / g_Client.Var(0x16755A8), written by
    // Net/GameVarDispatch.cpp:389 (case 61), :395 (case 62), :430, :481 (case 90) — read directly
    // by Update() (see .cpp, anchor @0x45e782). Do NOT reintroduce a mirror field here.

    uint32_t selectedInvItemId = 0;   // g_SelectedInvItemId 0x1673258 (inventory UI selection)
    int32_t  selectedInvCounter = 0;  // dword_167325C (associated counter, semantics not proven here)
    uint32_t classItemId = 0;         // dword_1673248: equipped "class core" item, read by
                                       // Player_IsCharClass/Player_IsInStance (0x45C550/0x45C480).
    int32_t  invExtraPageCount = 0;   // g_Inv_ExtraPageCount 0x16732A8 (0 => 1 page scanned, >0 => 2)

    int32_t talismanSlot = 0;                    // g_TalismanSlot 0x1674760 (see StatFormulas.h neutralization)
    std::array<int32_t, 20> talismanPacked{};    // dword_1675664[0..19] (packed combined values)
};

// A slot in the target list (15 max, offsets +36..+215 of the original object).
// monsterIndex references g_World.monsters (index, NOT the network identity).
struct AutoPlayTargetSlot {
    int32_t monsterIndex = -1; // -1 = free (the binary writes NaN/-1 depending on the site — normalized here)
    float   distance = 0.0f;
    // this+44 of the original object (@0x458548 1st-slot path, @0x458596 InsertTargetSorted path):
    // `dword_176703C[70*i] != 1`. Field KEPT (it exists in the original struct and SelectTarget
    // reads it @0x4585e0), but its value is PROVEN always true at the insertion point — see the
    // phase-locking demonstration in .cpp::BuildTargetList (anchors 0x5816cf/0x581939).
    bool    available = false;
};

// (REMOVED 2026-07-16) struct MonsterAutoplayExt — its 3 fields were GHOSTS: declared, read,
// but NEVER written by anyone in src/ (no `Ext()` was a setter). Each was re-anchored on the
// real, already-available and already-populated data:
//   - `state` (AP-06)       = dword_1766F8C = base+0x18 = MonsterEntity::body+8. Populated by
//     BOTH paths of EntityManager::OnSpawnMonster (:523 refresh / :540 spawn), mirroring the
//     memcpy 0x50 @0x467cef/@0x467e23 of Pkt_SpawnMonster 0x467B00. Read via
//     AutoTarget_MonsterActionState() (Game/AutoTargetCombatGate.h:113), which reads EXACTLY
//     body+8. The old `state = 1` default deadened the 3 "dead/invalid monster" filters
//     (0x45831a, 0x4586a9, 0x458d12).
//   - `engageRange` (AP-05) = unk_1766FD8 = base+0x64 = MonsterEntity::radius (GameState.h:212),
//     ALREADY computed by EntityManager.cpp:558 with the binary's exact formula
//     (sqrt(def[+256]^2+def[+248]^2)*0.5, @0x467d4f-0x467d87) and never read until now.
//   - `aggroOwner`          = dword_176703C = base+0xC8. Its writer is INVISIBLE to xrefs (accessed
//     via pointer): Char_UpdateMotionState 0x5816A0 — unconditional reset `[eax+0C8h]=0` @0x5816cf
//     then `[ecx+0C8h]=1` @0x581939 in the SOLE case 12. Do NOT invent a writer for it: the value
//     read by the binary is proven constant at the usage point (see .cpp::BuildTargetList).
// Do NOT reintroduce this struct: it would reinstate 3 storages with no writer.

// Integration points deliberately out of scope for this cluster: movement collision,
// networking, inventory UI, NPC refresh, player action state. Optional callbacks
// (nullptr = default behavior documented at the call site). Original EA cited for real
// wiring.
struct AutoPlayHost {
    // MapColl_SlideMoveGround 0x697330: attempts a sliding move from (x,y,z) to
    // (toX,toY,toZ) at `speed` over `dt` seconds; writes the position actually reached
    // into (outX,outY,outZ). The caller itself compares against the intended point
    // (== -> not blocked).
    std::function<void(float x, float y, float z, float toX, float toY, float toZ,
                        float speed, float dt, float& outX, float& outY, float& outZ)> SlideMove;

    // Char_CalcAttackSpeed 0x4CCAB0 (&g_EquipSnapshotScratch in the binary — depends on
    // the stat engine StatFormulas.h, deliberately NOT included here to keep this module
    // self-contained within the headers listed by the mission). Speed used as SlideMove's
    // `speed` parameter. Default if not wired: 1.0f.
    std::function<float()> GetSelfMoveSpeed;

    // Npc_Interact 0x53A660 (idHi, idLo of the NPC).
    std::function<void(EntityId npcId)> InteractNpc;

    // cGameHud_PlaceItemIntoBag 0x650470: checks/performs placing `itemId` (quantity/weight
    // = weight) into the bag. Return >= 0 = slot obtained (success); -1 = failure.
    std::function<int(uint32_t itemId, uint32_t weight)> TryPlaceItemIntoBag;

    // Net_SendPacket_Op22 0x4B5300: requests using an item from the quick pickup grid
    // (container, slot). Return = packet emission success.
    std::function<bool(int container, int slot)> SendUseGroundPickupItem;

    // Net_SendOp99 0x4BD140 (argument g_InvDirtyEnable): notifies the server of an
    // inventory refresh after a definitive auto-scroll failure.
    std::function<void(bool invDirtyEnable)> NotifyInventoryDirty;

    // Map_BeginWarpToFactionTownEx 0x55C9A0 (argument 0).
    std::function<void()> WarpToFactionTown;

    // Char_IsAttackAction 0x558A50 (&g_LocalPlayerSheet): true if the local player's
    // current animation is an attack action.
    std::function<bool()> IsSelfAttacking;

    // maybe_Npc_ShouldRefreshTarget 0x583E20: NPC refresh eligibility (probable
    // anti-spam/timer, out of scope for the AutoPlay_* cluster). Default (not wired):
    // always eligible.
    std::function<bool(const NpcEntity&)> ShouldRefreshNpc;
};

// AutoPlaySystem — automatic farming state machine (mirrors the original AutoPlay_*
// object, ~332 bytes). One instance per local player (typical usage: a singleton, like
// the rest of ts2::game — left to the caller, no global imposed here).
class AutoPlaySystem {
public:
    AutoPlaySystem(); // AutoPlay_Construct 0x457EA0

    AutoPlayConfig         config;
    AutoPlayExternalState  externalState;
    AutoPlayHost           host;

    // Friend/enemy lists = LOOT FILTERS BY OBJECT NAME (these are NOT player names, NOT the
    // social UI). Loaded from G02_GINFO\011.BIN (friends) / 012.BIN (enemies) — this+296 /
    // this+324 in the original —, each name VALIDATED on read against the ITEM table
    // (MobDb_FindByName mITEM 0x4C3C50: stride 436, name @+4 = ItemInfo::name); a nonexistent
    // name is silently rejected. Queried by AutoPlay_FindNpcTarget 0x458E90 against the name
    // (def+4) of the loot/NPC entry:
    //   - friendNames (IsFriendName @0x458FEB) = WHITELIST: object ALWAYS selected, bypassing
    //     the config.pkFactionMask category mask.
    //   - enemyNames  (IsEnemyName  @0x4590D1) = BLACKLIST: object NEVER selected through the
    //     category path, even if its category is checked.
    // AutoPlay_IsFriend 0x45FAA0 / AutoPlay_IsEnemy 0x45FBE0 = linear search by string equality
    // (Crt_Strcmp) — faithfully reproduced by std::vector<std::string>.
    std::vector<std::string> friendNames;
    std::vector<std::string> enemyNames;
    bool IsFriendName(const char* name) const; // AutoPlay_IsFriend  0x45FAA0
    bool IsEnemyName(const char* name)  const; // AutoPlay_IsEnemy   0x45FBE0

    // ---- Load/save the lists (G02_GINFO\011.BIN / 012.BIN) ---------------------------------
    // File = 1200 bytes = 48 x 25 (no header), '@' padding (0x40). Load requires EXACTLY 1200
    // bytes read (otherwise clears the list + returns false, like the binary's 4 failure paths);
    // a slot's name = its bytes up to the 1st '@' or NUL. Save: size guard (Friend: > 48 =>
    // clear + false WITHOUT writing; Enemy: > 48 => clear THEN writes — faithful asymmetry
    // 0x45DEC0 / 0x45E1A3), buffer pre-filled with '@', writes 1200 bytes. Save is required by
    // the editing UI (AutoPlay_OnMouseUpNameList 0x45B000, neighboring UI front) — exposed even
    // though not yet wired.
    bool LoadFriendList(); // AutoPlay_LoadFriendList 0x45D730
    bool LoadEnemyList();  // AutoPlay_LoadEnemyList  0x45DAF0
    bool SaveFriendList(); // AutoPlay_SaveFriendList 0x45DE50
    bool SaveEnemyList();  // AutoPlay_SaveEnemyList  0x45E140

    // Resets the targets + arms auto-hunt + LOADS the friend/enemy lists. Mirrors
    // AutoPlay_Start 0x45D580, whose SOLE binary caller is UI_InitAllDialogs 0x5ABF50
    // @0x5AC193 (`if (!AutoPlay_Start(g_AutoPlayBot)) return 0;` — 28th of ~37 dialog-init
    // slots, AFTER UI_FactionTitleWnd_Init 0x184BF90 and BEFORE UI_GameHud_Init @0x5ac1a8).
    //
    // WARNING: WIRING TODO (outside my files — CharSelect-fix / GameWindows front): Start()
    // CURRENTLY HAS NO caller on the ClientSource side. The ClientSource mirror of
    // UI_InitAllDialogs is GameWindows::Init (UI/GameWindows.h:127, which ALREADY OWNS
    // autoPlaySystem_) — NOT SceneManager.cpp (previous, mistaken prescription). Faithful site:
    // in GameWindows::Init, BEFORE HUD init, `if (!autoPlaySystem_.Start()) return false;`
    // (Start() always returns true @0x45d729, so the guard never fires — but the FORM stays
    // `if (!...)` to respect 0x5ac193 ≺ 0x5ac1a8). WITHOUT this call, LoadFriendList/LoadEnemyList
    // never run, friendNames/enemyNames stay empty, AND huntArmed_ stays false -> Update()'s OR
    // chain is disarmed (defect D1). Wiring to be assigned to the orchestrator.
    bool Start(); // AutoPlay_Start 0x45D580

    // ---- Targeting machine --------------------------------------------------
    bool    BuildTargetList();                                    // 0x458280
    int32_t SelectTarget();                                       // 0x4585E0
    void    InsertTargetSorted(int32_t monsterIndex, float distance, bool available); // 0x458870
    bool    IsTargetLocked(int32_t monsterIndex) const;            // 0x458B80
    bool    CountTargetsInRangeAtLeastThreshold();                 // 0x458C10
    void    RemoveTargetByMonsterIndex(int32_t monsterIndex);      // 0x458E00
    void    ClearTargetSlot();                                     // 0x4587E0
    void    ResetTargetList();                                     // 0x458AB0
    bool    UpdateTargeting(float dt);                             // 0x45D080 (accumulated dt, see .cpp)

    // ---- Geometry / movement ----------------------------------------------
    // PLAYER<->point distance. Faithful note: the Y component is computed in the original
    // binary (0x458a5a) but never added to the total — 2D (X,Z) distance faithfully
    // reproduced (an original bug/quirk, not an approximation on our part).
    static float DistanceToPlayer(float x, float y, float z);      // 0x4589E0
    bool FindWalkableAdjacent(float& outX, float& outY, float& outZ) const; // 0x4580C0

    // ---- NPC --------------------------------------------------------------------
    int32_t FindNpcTarget() const; // 0x458E90
    bool    MoveToNpc();           // 0x45C5C0

    // ---- Monster classification (auto PK) -----------------------------------
    // `secondTier` = original parameter a1 (false/0 -> "1st tier" id table, true/1 -> "2nd
    // tier" table). `monsterDefId` = monster identification field (dword_1766F84 / equivalent
    // — NOT the array index).
    bool IsMobOfFaction(bool secondTier, int32_t monsterDefId) const;  // 0x45BE80
    bool IsMobCategory2(int32_t classId, int32_t monsterDefId) const;  // 0x45C2F0

    // ---- Auto consumable items -------------------------------------------------
    bool CheckReturnScroll();     // 0x45C750 (item 1001)
    bool CheckTownScroll();       // 0x45C9B0 (item 563)
    bool HasRequiredItems() const; // 0x45CC10

    // ---- Main tick -------------------------------------------------------
    // Faithful port of AutoPlay_Update 0x45E770 (the reachable "spine": guards -> inv-dirty
    // latch -> materials throttle -> loot/NPC/consumables OR chain including MoveToNpc ->
    // monster targeting). THIS is the tick that queries the friend/enemy lists (via
    // MoveToNpc -> FindNpcTarget), fixing the "lists loaded but never consulted" defect. The
    // combat tail (Player_CastSkill/Net_QueueRunTo) is DEFERRED (see .cpp, TODO 0x45E91D). dt
    // in seconds (replaces GetTickCount() with accumulators — 2000 ms / 50 ms thresholds, see
    // .cpp). Already wired every InGame frame: SceneManager.cpp:1497 ->
    // GameWindows::UpdateAutoPlay (UI/GameWindows.h:142) -> Update(dt). It's this LIVE path
    // that makes fixes AP-01/AP-05/AP-06 actionable: once the fuel guard passes (g_Client.Var
    // 0x16755A4/A8), the tick reaches UpdateTargeting -> BuildTargetList/SelectTarget/CountTargets.
    void Update(float dt);

    // Read access (diagnostics / UI — rendering the autoplay panel remains out of scope, the
    // renderer's EA TODO is not identified in this cluster).
    const std::array<AutoPlayTargetSlot, 15>& Targets() const { return targets_; }
    uint16_t TargetCount() const { return targetCount_; }
    int32_t  CurrentTargetIndex() const { return currentTargetIndex_; }

    // (REMOVED 2026-07-16) Ext(std::size_t) / MonsterAutoplayExt — see the banner above: the 3
    // per-monster fields were writer-less ghosts, re-anchored on MonsterEntity::body+8 and
    // MonsterEntity::radius, already populated by EntityManager.

private:
    std::array<AutoPlayTargetSlot, 15> targets_{};
    uint16_t targetCount_ = 0;          // +216
    int32_t  currentTargetIndex_ = -1;  // +220, -1 = no target locked

    // dt-driven timers (replace GetTickCount() — see Update() comment). npcInteractCooldownSec_
    // models this[60] (byte +240): shared "last action" timestamp read both for the 50 ms
    // cooldown (MoveToNpc 0x45C5F7) and the 2000 ms throttle (Update 0x45E827).
    float rebuildTimerSec_ = 0.0f;          // +228 (rebuild every 1000 ms)
    float npcInteractCooldownSec_ = 0.0f;   // +240 (NPC cooldown 50 ms / materials throttle 2000 ms)

    // this+288: armed (=1) by Start 0x45D641; guards AutoPlay_Update's auto-hunt OR chain
    // (nothing clears it in the studied cluster -> stays true after HUD init, with
    // g_AutoHuntFuelA/B acting as the real activity gate).
    bool huntArmed_ = false;
    // this+284: armed (=1) by Start 0x45D634; one-shot latch -> flushes inv-dirty to the server
    // (Net_SendOp99) on the 1st armed tick then disarms, see Update() @0x45e79e.
    bool invDirtyStartLatch_ = false;

    // State shared between CheckReturnScroll and CheckTownScroll (original
    // dword_1675B08/1675B1C/1675B20/flt_1675B0C — ONLY ONE scroll in flight at a time,
    // regardless of type, exactly like the binary).
    bool     pendingItemUseLatch_ = false;
    int32_t  pendingItemUseContainer_ = -1;
    int32_t  pendingItemUseSlot_ = -1;
    float    pendingItemUseTimeSec_ = 0.0f;

    // Internal utilities translated because they're required by the cluster (EA in comment).
    bool PlayerIsCharClass(int32_t classIdx) const;      // Player_IsCharClass 0x45C550
    bool PlayerIsInStance(int32_t stance) const;         // Player_IsInStance  0x45C480
    bool PlayerIsElementalAffinity(int32_t elementIdx) const; // sub_45C590    0x45C590

    // Factors out the body shared by CheckReturnScroll/CheckTownScroll (same structure in
    // the binary, only the item id, message, and flag to disable differ). `itemId` = 1001 or
    // 563; `strTableId` = StrTable005 id of the "no scroll" message (1793 / 2185);
    // `enabledToggle` = config.useReturnScroll or config.useTownItem.
    bool CheckConsumableScroll(uint32_t itemId, int strTableId, bool& enabledToggle);
};

} // namespace ts2::game

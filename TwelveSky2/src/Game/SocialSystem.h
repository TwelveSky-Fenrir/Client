// Game/SocialSystem.h — Achievements + social lists (friends/blacklist) for the TwelveSky2 client.
//
// Two DISTINCT and unrelated systems in the binary, grouped here because the
// mission treats them as "lightweight annexes":
//
//  1) TRIBE ACHIEVEMENTS — Net_OnAchievementDataLoad (0x4ac920) loads a 96-byte
//     table (24 int32) into dword_184C218 ; Net_OnAchievementNotice (0x4ac950)
//     builds a "<label> <name> <suffix>" message from this table, indexed by
//     TribeSkill_SkillIdToIndex (0x692e00, 12 slots 0..11).
//
//  2) AUTOPLAY FRIEND/ENEMY LISTS (farm bot) — AutoPlay_LoadFriendList (0x45d730) /
//     AutoPlay_SaveFriendList (0x45de50) / AutoPlay_IsFriend (0x45faa0) and their
//     Enemy counterparts (0x45daf0/0x45e140/0x45fbe0), plus the exclusive
//     add/remove logic AutoPlay_OnMouseUpNameList (0x45b000). This is the ONLY
//     "friend list / blacklist" structure actually proven by the disassembly:
//     local files G02_GINFO\011.BIN (friends) / 012.BIN (enemies), 48 names max,
//     25 bytes/slot, mutually exclusive. It serves the auto-combat bot's targeting
//     (don't attack a friend / prioritize an enemy), NOT a "presence" system (no
//     "online" field exists in this structure).
//
// WHAT IS NOT HERE (out of scope, already covered, or unproven):
//   - The REAL server-driven "friend online / friend added" system (opcodes 0x7e
//     FriendStatusNotice EA 0x4aa050 and 0x90 FriendListEvent EA 0x4ab040) is already
//     wired in Net/GameHandlers_ChatSocial.cpp. WARNING: these two handlers ONLY
//     build text and display it (HUD_ShowFloatingMessage / Msg_AppendSystemLine) —
//     they never read or write ANY array. Nowhere in the binary is there a "server
//     friend list with online status" array: the disassembly only proves the notice.
//     The real proven storage is the local AutoPlay list (above).
//   - Net_OnSocialListRemove (0x4a9450, opcode 0x79, already handled as a plain
//     notice in GameHandlers_ChatSocial.cpp) does NOT touch the AutoPlay friend/enemy
//     lists: it manipulates three totally different name grids (unk_16869C0/
//     1686AC4/1686BC8, also read by Skill_CheckBuffState 0x4fc950 and UI_Macro_Init
//     0x5cb800). These grids look like a faction/element roster per macro slot,
//     NOT a friend/blacklist — despite the existing IDA name "Net_OnSocialListRemove",
//     the content shows it's a different subsystem (out of scope for this mission;
//     not modeled here, per the "don't invent" rule).
//
#pragma once
#include "Game/ClientRuntime.h"   // game::Str(), g_Client (message log)
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::game {

// =============================================================================
// 1) TRIBE ACHIEVEMENTS
// =============================================================================

// Achievement table — dword_184C218 (EA 0x184c218), filled by
// Net_OnAchievementDataLoad (EA 0x4ac920): direct memcpy of 96 bytes received over
// the network. 96 / 4 = 24 int32 slots; only indices 0..11 are addressed by the
// notice handler via TribeSkillIdToIndex (the remaining 12 aren't read by any
// cross-referenced function in this cluster — probably a margin/second tribe set
// not exercised by this path; kept as-is for fidelity).
struct AchievementState {
    static constexpr size_t kFlagCount = 24;   // 96 bytes / 4

    std::array<int32_t, kFlagCount> flags{};

    // EA 0x4ac931/0x4ac94c : Crt_Memcpy(v1, &unk_8156C1, 96); Crt_Memcpy(dword_184C218, v1, 96);
    void LoadFromPayload(const void* payload96);
};

// TribeSkill_SkillIdToIndex (EA 0x692e00) — maps a tribe skill id to a slot
// 0..11 (12 recognized skills); -1 if unrecognized. Exact reproduction of the
// original switch (no closed formula, the table is irregular: 2,3,4,7,8,9,
// 12,13,14,141,142,143).
int TribeSkillIdToIndex(int skillId);

// Result of Net_OnAchievementNotice (EA 0x4ac950): either nothing (unrecognized
// skill id, EA 0x4aca3a), or ready-to-post text.
struct AchievementNoticeResult {
    bool        shown = false;
    std::string text;
};

// Rebuilds the Net_OnAchievementNotice message WITHOUT posting it to the log
// (pure, testable function). `tribeSkillOrMorphId` matches exactly the argument
// passed to TribeSkill_SkillIdToIndex in the original (EA 0x4ac9b5): the binary
// passes g_SelfMorphNpcId (dword 0x1675a98), a "morph npc" global reused here as
// a tribe skill selector — that's what the disassembly shows as-is, without
// interpretation; this global isn't modeled in GameState.h, so the caller must
// supply it. `achieverName13` matches the 13-byte name field copied at the head
// of the payload (EA 0x4ac9a1, &unk_8156C1..+13), unprocessed (no '@' trim here,
// unlike the AutoPlay lists: the original doesn't trim this field at all).
AchievementNoticeResult BuildAchievementNotice(const AchievementState& state,
                                                int tribeSkillOrMorphId,
                                                const std::string& achieverName13);

// Single global instance (mirror of dword_184C218) — fed by
// Net_OnAchievementDataLoad (Net/GameHandlers_BossWorld.cpp, opcode 0x98) and read
// by UI/SocialWindow.h (Achievements tab), same pattern as game::g_Warehouse/g_World.
inline AchievementState g_Achievements;

// Convenience variant: builds AND posts (floating banner + system line), like
// EA 0x4aca20 (HUD_ShowFloatingMessage(0,0,&v7,&String)) and 0x4aca35
// (Msg_AppendSystemLine(&v7, g_SysMsgColor)). g_SysMsgColor (EA 0x84dfd8) isn't
// modeled as its own field -> read via g_Client.Var() per the ClientRuntime hub convention.
bool PostAchievementNotice(const AchievementState& state, int tribeSkillOrMorphId,
                            const std::string& achieverName13);

// =============================================================================
// 2) AUTOPLAY FRIEND / ENEMY LISTS (only proven "social list" structure)
// =============================================================================

// One of the AutoPlay's two lists (friends OR enemies). Disk layout and capacity
// rules recorded identically in AutoPlay_Load/SaveFriendList (EA 0x45d730/
// 0x45de50) and AutoPlay_Load/SaveEnemyList (EA 0x45daf0/0x45e140) — both pairs
// are bit-for-bit identical except for the file name and field offsets in the
// original AutoPlay class.
struct SocialNameList {
    static constexpr size_t kCapacity  = 48;   // EA 0x45dec0 (`*(this+320) <= 0x30`), 0x45e1a3
    static constexpr size_t kSlotBytes = 25;   // on-disk slot stride
    static constexpr size_t kFileBytes = kCapacity * kSlotBytes;   // 1200 bytes (ReadFile/WriteFile 0x4B0 EA 0x45d7ca/0x45e0ad)

    std::vector<std::string> names;   // order = insertion order (std list in the binary)

    bool Full() const { return names.size() >= kCapacity; }

    // Linear Crt_Strcmp scan over the whole list — EA 0x45fbb3/0x45fba0 (IsFriend),
    // 0x45fcf3/0x45fce0 (IsEnemy).
    bool Contains(const std::string& name) const;

    // RAW add (no exclusivity check against the other list — see
    // AutoPlaySocialLists::AddFriend/AddToBlacklist for the full logic as
    // exercised by AutoPlay_OnMouseUpNameList, EA 0x45b6b0-0x45b90a).
    // FIDELITY NOTE: the original also validates the name via MobDb_FindByName (EA
    // 0x4c3c50) before any add (both loading AND UI entry) — this base isn't
    // exposed by the provided headers (GameDatabase.h only covers ITEM_INFO/
    // LEVEL_INFO); name-existence validation is therefore left to the caller
    // (TODO EA 0x45d9e9/0x45da26, 0x45b63f).
    bool Add(const std::string& name);

    // Removal by linear search, first occurrence — EA 0x45bb95-0x45bc21 (friend),
    // 0x45bd3e-0x45be34 (enemy).
    bool Remove(const std::string& name);

    // Serializes to the exact on-disk format: 48 slots x 25 bytes, padded with
    // 0x40 ('@') (Crt_Memset(Buffer, 64, 1200) EA 0x45de9f/0x45e182), name
    // truncated to 24 useful characters (the 25th byte remains '@' padding).
    std::array<uint8_t, kFileBytes> Serialize() const;

    // Reloads from `bufBytes` bytes in the on-disk format: for each 25-byte slot,
    // truncates at the first '@' encountered (EA 0x45d992/0x45d9af Str_Find('@'),
    // 0x45d9d1 Str_Erase), empty slot ignored. Overwrites the in-memory list
    // (implicit List_Clear, EA 0x45d7e5). If bufBytes < kFileBytes, the list is
    // simply cleared (like a missing file).
    void Deserialize(const uint8_t* buf, size_t bufBytes);
};

// Loads/saves the AutoPlay's exact local file (faithful paths and sizes:
// EA 0x45d7ca "G02_GINFO\\011.BIN" and 0x45db81 "G02_GINFO\\012.BIN", fixed 1200
// bytes). Uses the standard C API (fopen/fread/fwrite): the original binary uses
// Win32 CreateFileA/ReadFile/WriteFile; the semantics (fixed-block read/write,
// silent failure -> cleared list) are preserved.
bool LoadSocialNameListFile(SocialNameList& list, const char* path);
bool SaveSocialNameListFile(const SocialNameList& list, const char* path);

bool LoadFriendListFile(SocialNameList& list);          // EA 0x45d730 -> "G02_GINFO\011.BIN"
bool SaveFriendListFile(const SocialNameList& list);     // EA 0x45de50
bool LoadBlacklistFile(SocialNameList& list);            // EA 0x45daf0 -> "G02_GINFO\012.BIN"
bool SaveBlacklistFile(const SocialNameList& list);      // EA 0x45e140

// Add result code — reflects the 3 distinct outcomes of
// AutoPlay_OnMouseUpNameList (str1947 "already in a list", str1980 "list full",
// silent success + immediate save).
enum class SocialListOp { Added, AlreadyListed, ListFull };

// AutoPlay friend+enemy bundle with the EXACT mutual-exclusivity logic of
// AutoPlay_OnMouseUpNameList (EA 0x45b000): a name can only be present in one
// of the two lists; adding to one fails if the name is already in the OTHER
// (same error message str1947 as for a duplicate in the same list — the binary
// doesn't distinguish the two cases, cf. EA 0x45b679/0x45b848).
struct AutoPlaySocialLists {
    SocialNameList friends;    // this+296/+316/+320 — EA 0x45b7 (add), 0x45bb (remove)
    SocialNameList blacklist;  // this+324/+344/+348 — EA 0x45b8 (add), 0x45bd (remove)

    // mode==0 branch of AutoPlay_OnMouseUpNameList (add on the "friends" side).
    // Faithful test order: capacity full (str1980, EA 0x45b6f1/0x45b811) BEFORE
    // checking presence in the other list (str1947, EA 0x45b723/0x45b850).
    SocialListOp AddFriend(const std::string& name);

    // mode==1 branch (add on the "enemies / blacklist" side), symmetric — EA 0x45b6b0.
    SocialListOp AddToBlacklist(const std::string& name);

    // Removal — EA 0x45bb95-0x45bc21 (friends) / 0x45bd3e-0x45be34 (enemies):
    // linear search, first match, immediate save if found (otherwise str1981,
    // EA 0x45ba5b, not modeled here: the returned bool carries this information).
    bool RemoveFriend(const std::string& name);
    bool RemoveFromBlacklist(const std::string& name);

    bool IsFriend(const std::string& name) const { return friends.Contains(name); }     // EA 0x45faa0
    bool IsEnemy(const std::string& name)  const { return blacklist.Contains(name); }   // EA 0x45fbe0
    // AutoPlay_IsNameListed (EA 0x45f820): selects ONLY ONE of the two lists based
    // on the original active UI tab (this+292 == 0 -> friends, ==1 -> enemies);
    // this UI-dependent behavior isn't reproduced here (out of scope for data).
    // IsListed() below is the union of both, useful for the exclusivity logic above.
    bool IsListed(const std::string& name) const { return IsFriend(name) || IsEnemy(name); }

    bool LoadAll();
    bool SaveAll() const;
};

} // namespace ts2::game

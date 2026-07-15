// Game/SocialSystem.cpp — implémentation achievements + listes ami/ennemi AutoPlay (ts2::game).
// Fidèle à Net_OnAchievementDataLoad (0x4ac920), Net_OnAchievementNotice (0x4ac950),
// TribeSkill_SkillIdToIndex (0x692e00), AutoPlay_Load/SaveFriendList (0x45d730/0x45de50),
// AutoPlay_Load/SaveEnemyList (0x45daf0/0x45e140), AutoPlay_Is Friend/Enemy (0x45faa0/
// 0x45fbe0) et AutoPlay_OnMouseUpNameList (0x45b000). Voir SocialSystem.h pour le détail
// EA par champ/branche et pour ce qui est explicitement HORS périmètre.
#include "Game/SocialSystem.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ts2::game {

// =============================================================================
// 1) ACHIEVEMENTS DE TRIBU
// =============================================================================

void AchievementState::LoadFromPayload(const void* payload96) {
    // EA 0x4ac931 : Crt_Memcpy(v1, &unk_8156C1, 96) — copie directe, aucune validation.
    std::memcpy(flags.data(), payload96, sizeof(flags));
}

int TribeSkillIdToIndex(int skillId) {
    // EA 0x692e2b : switch irrégulier, reproduit tel quel (pas de formule fermée).
    switch (skillId) {
        case 2:   return 0;
        case 3:   return 1;
        case 4:   return 2;
        case 7:   return 3;
        case 8:   return 4;
        case 9:   return 5;
        case 12:  return 6;
        case 13:  return 7;
        case 14:  return 8;
        case 141: return 9;
        case 142: return 10;
        case 143: return 11;
        default:  return -1;   // EA 0x692e83
    }
}

AchievementNoticeResult BuildAchievementNotice(const AchievementState& state,
                                                int tribeSkillOrMorphId,
                                                const std::string& achieverName13) {
    AchievementNoticeResult r;

    const int slot = TribeSkillIdToIndex(tribeSkillOrMorphId);   // EA 0x4ac9b5
    if (slot == -1)                                              // EA 0x4ac9c1 -> EA 0x4aca3a (aucun message)
        return r;

    // EA 0x4ac9f2-0x4ac9f7 : StrTable005_Get(dword_184C218[v9] % 100 + 2249)
    const std::string label  = Str(state.flags[static_cast<size_t>(slot)] % 100 + 2249);
    const std::string suffix = Str(2305);                        // EA 0x4ac9d4

    // EA 0x4aca06 : Crt_Vsnprintf(&v7, "%s %s %s", v1=label, &v3=achieverName13, v2=suffix)
    r.text  = label + " " + achieverName13 + " " + suffix;
    r.shown = true;
    return r;
}

bool PostAchievementNotice(const AchievementState& state, int tribeSkillOrMorphId,
                            const std::string& achieverName13) {
    const AchievementNoticeResult r = BuildAchievementNotice(state, tribeSkillOrMorphId, achieverName13);
    if (!r.shown)
        return false;

    // g_SysMsgColor (EA 0x84dfd8) non modélisé en champ propre -> longue traîne via Var().
    const uint32_t sysColor = static_cast<uint32_t>(g_Client.VarGet(0x84dfd8));

    g_Client.msg.Floating(0, 0, r.text);      // EA 0x4aca20 : HUD_ShowFloatingMessage(0, 0, &v7, &String)
    g_Client.msg.System(r.text, sysColor);    // EA 0x4aca35 : Msg_AppendSystemLine(&v7, g_SysMsgColor)
    return true;
}

// =============================================================================
// 2) LISTES AMI / ENNEMI DE L'AUTOPLAY
// =============================================================================

bool SocialNameList::Contains(const std::string& name) const {
    // EA 0x45fbb3/0x45fba0 (IsFriend) / 0x45fcf3/0x45fce0 (IsEnemy) : Crt_Strcmp linéaire.
    for (const std::string& n : names)
        if (n == name)
            return true;
    return false;
}

bool SocialNameList::Add(const std::string& name) {
    if (Full() || Contains(name))
        return false;
    names.push_back(name);   // EA 0x45b7ce/0x45b8ec : List_PushBackNode
    return true;
}

bool SocialNameList::Remove(const std::string& name) {
    // EA 0x45bb95-0x45bc21 / 0x45bd3e-0x45be34 : recherche linéaire, 1re occurrence.
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            names.erase(names.begin() + static_cast<std::ptrdiff_t>(i));   // List_EraseNode
            return true;
        }
    }
    return false;
}

std::array<uint8_t, SocialNameList::kFileBytes> SocialNameList::Serialize() const {
    std::array<uint8_t, kFileBytes> buf{};
    buf.fill(0x40);   // EA 0x45de9f/0x45e182 : Crt_Memset(Buffer, 64 /*'@'*/, 1200)
    for (size_t i = 0; i < names.size() && i < kCapacity; ++i) {
        const std::string& n = names[i];
        const size_t len = n.size() < (kSlotBytes - 1) ? n.size() : (kSlotBytes - 1);
        std::memcpy(&buf[i * kSlotBytes], n.data(), len);
    }
    return buf;
}

void SocialNameList::Deserialize(const uint8_t* buf, size_t bufBytes) {
    names.clear();   // EA 0x45d7e5 : List_Clear (cas fichier absent/taille invalide -> vide)
    if (bufBytes < kFileBytes)
        return;
    for (size_t i = 0; i < kCapacity; ++i) {
        const char* raw = reinterpret_cast<const char*>(buf + i * kSlotBytes);
        std::string s(raw, strnlen(raw, kSlotBytes));
        const size_t at = s.find('@');   // EA 0x45d992/0x45d9af : Str_Find('@')
        if (at != std::string::npos)
            s.erase(at);                  // EA 0x45d9d1 : Str_Erase(from '@' to end)
        if (!s.empty())
            names.push_back(s);
        // NOTE fidélité : l'original ne pousse le nom en liste que si MobDb_FindByName()
        // le reconnaît (EA 0x45d9e9-0x45da26) — base non exposée ici, voir SocialSystem.h.
    }
}

bool LoadSocialNameListFile(SocialNameList& list, const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { list.names.clear(); return false; }                  // EA 0x45d7d7/0x45d7e5
    std::array<uint8_t, SocialNameList::kFileBytes> buf{};
    const size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (got != buf.size()) { list.names.clear(); return false; }    // EA 0x45d88e (taille != 1200)
    list.Deserialize(buf.data(), buf.size());
    return true;
}

bool SaveSocialNameListFile(const SocialNameList& list, const char* path) {
    if (list.names.size() > SocialNameList::kCapacity)              // EA 0x45dec0/0x45e1a3
        return false;
    const auto buf = list.Serialize();
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;                                            // EA 0x45e0ba
    const size_t put = std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return put == buf.size();                                        // EA 0x45e101/0x45e123
}

bool LoadFriendListFile(SocialNameList& list)      { return LoadSocialNameListFile(list, "G02_GINFO\\011.BIN"); }
bool SaveFriendListFile(const SocialNameList& list) { return SaveSocialNameListFile(list, "G02_GINFO\\011.BIN"); }
bool LoadBlacklistFile(SocialNameList& list)        { return LoadSocialNameListFile(list, "G02_GINFO\\012.BIN"); }
bool SaveBlacklistFile(const SocialNameList& list)  { return SaveSocialNameListFile(list, "G02_GINFO\\012.BIN"); }

SocialListOp AutoPlaySocialLists::AddFriend(const std::string& name) {
    if (friends.Full())
        return SocialListOp::ListFull;                    // EA 0x45b6f1 -> LABEL_62 (str1980)
    if (blacklist.Contains(name) || friends.Contains(name))
        return SocialListOp::AlreadyListed;                 // EA 0x45b723 (IsEnemy) -> str1947
    friends.Add(name);                                      // EA 0x45b758-0x45b7ce
    SaveFriendListFile(friends);                             // EA 0x45b7ec
    return SocialListOp::Added;
}

SocialListOp AutoPlaySocialLists::AddToBlacklist(const std::string& name) {
    if (blacklist.Full())
        return SocialListOp::ListFull;                    // EA 0x45b80f -> LABEL_62 (str1980)
    if (friends.Contains(name) || blacklist.Contains(name))
        return SocialListOp::AlreadyListed;                 // EA 0x45b848 (IsFriend) -> str1947
    blacklist.Add(name);                                    // EA 0x45b877-0x45b8ec
    SaveBlacklistFile(blacklist);                            // EA 0x45b90a
    return SocialListOp::Added;
}

bool AutoPlaySocialLists::RemoveFriend(const std::string& name) {
    if (!friends.Remove(name))
        return false;                                       // EA 0x45ba5b (str1981, non modélisé ici)
    SaveFriendListFile(friends);                             // EA 0x45bc1c
    return true;
}

bool AutoPlaySocialLists::RemoveFromBlacklist(const std::string& name) {
    if (!blacklist.Remove(name))
        return false;
    SaveBlacklistFile(blacklist);                            // EA 0x45be2f
    return true;
}

bool AutoPlaySocialLists::LoadAll() {
    const bool a = LoadFriendListFile(friends);
    const bool b = LoadBlacklistFile(blacklist);
    return a && b;
}

bool AutoPlaySocialLists::SaveAll() const {
    const bool a = SaveFriendListFile(friends);
    const bool b = SaveBlacklistFile(blacklist);
    return a && b;
}

} // namespace ts2::game

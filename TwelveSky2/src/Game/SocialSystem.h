// Game/SocialSystem.h — Achievements + listes sociales (amis/liste noire) du client TwelveSky2.
//
// Deux systèmes DISTINCTS et sans rapport l'un avec l'autre dans le binaire, regroupés
// ici parce que la mission les traite comme des « annexes légères » :
//
//  1) ACHIEVEMENTS (haut-fait de tribu) — Net_OnAchievementDataLoad (0x4ac920) charge une
//     table de 96 o (24 int32) dans dword_184C218 ; Net_OnAchievementNotice (0x4ac950)
//     construit un message "<label> <nom> <suffixe>" à partir de cette table, indexée par
//     TribeSkill_SkillIdToIndex (0x692e00, 12 slots 0..11).
//
//  2) LISTES AMI/ENNEMI DE L'AUTOPLAY (bot de farm) — AutoPlay_LoadFriendList (0x45d730) /
//     AutoPlay_SaveFriendList (0x45de50) / AutoPlay_IsFriend (0x45faa0) et leurs pendants
//     Enemy (0x45daf0/0x45e140/0x45fbe0), plus la logique d'ajout/retrait exclusive
//     AutoPlay_OnMouseUpNameList (0x45b000). C'est la SEULE structure de liste « amis /
//     liste noire » réellement prouvée par le désassemblage : fichiers locaux
//     G02_GINFO\011.BIN (amis) / 012.BIN (ennemis), 48 noms max, 25 o/slot, mutuellement
//     exclusives. Elle sert au ciblage du bot d'auto-combat (ne pas attaquer un ami /
//     prioriser un ennemi), PAS à un système de « présence » (aucun champ « online » n'existe
//     dans cette structure).
//
// CE QUI N'EST PAS ICI (hors périmètre, déjà couvert ou non prouvé) :
//   - Le VRAI système « ami en ligne / ami ajouté » piloté serveur (opcodes 0x7e
//     FriendStatusNotice EA 0x4aa050 et 0x90 FriendListEvent EA 0x4ab040) est déjà câblé
//     dans Net/GameHandlers_ChatSocial.cpp. ATTENTION : ces deux handlers ne font QUE
//     construire un texte et l'afficher (HUD_ShowFloatingMessage / Msg_AppendSystemLine) —
//     ils ne lisent ni n'écrivent AUCUN tableau. Il n'existe nulle part dans le binaire de
//     tableau de « liste d'amis serveur avec statut en ligne » : le désassemblage ne prouve
//     que la notice. Le stockage réel prouvé est la liste locale de l'AutoPlay (ci-dessus).
//   - Net_OnSocialListRemove (0x4a9450, opcode 0x79, déjà géré comme simple notice dans
//     GameHandlers_ChatSocial.cpp) NE TOUCHE PAS aux listes ami/ennemi de l'AutoPlay : elle
//     manipule trois grilles de noms totalement différentes (unk_16869C0/1686AC4/1686BC8,
//     aussi lues par Skill_CheckBuffState 0x4fc950 et UI_Macro_Init 0x5cb800). Ces grilles
//     ressemblent à un roster de faction/élément par slot de macro, PAS à une liste
//     d'amis/blacklist — malgré le nom IDA existant « Net_OnSocialListRemove », le contenu
//     démontre qu'il s'agit d'un autre sous-système (hors périmètre de cette mission ;
//     non modélisé ici, conformément à la règle « ne pas inventer »).
//
#pragma once
#include "Game/ClientRuntime.h"   // game::Str(), g_Client (journal de messages)
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::game {

// =============================================================================
// 1) ACHIEVEMENTS DE TRIBU
// =============================================================================

// Table de haut-faits — dword_184C218 (EA 0x184c218), remplie par
// Net_OnAchievementDataLoad (EA 0x4ac920) : memcpy direct de 96 o reçus réseau.
// 96 / 4 = 24 emplacements int32 ; seuls les indices 0..11 sont adressés par le
// handler de notice via TribeSkillIdToIndex (les 12 restants ne sont lus par
// aucune fonction croisée dans ce cluster — probablement une marge/second jeu de
// tribu non exercé par ce chemin ; on les conserve tels quels par fidélité).
struct AchievementState {
    static constexpr size_t kFlagCount = 24;   // 96 o / 4

    std::array<int32_t, kFlagCount> flags{};

    // EA 0x4ac931/0x4ac94c : Crt_Memcpy(v1, &unk_8156C1, 96); Crt_Memcpy(dword_184C218, v1, 96);
    void LoadFromPayload(const void* payload96);
};

// TribeSkill_SkillIdToIndex (EA 0x692e00) — mappe un id de compétence de tribu vers
// un slot 0..11 (12 compétences reconnues) ; -1 si non reconnu. Reproduction exacte
// du switch d'origine (pas de formule fermée, la table est irrégulière : 2,3,4,7,8,9,
// 12,13,14,141,142,143).
int TribeSkillIdToIndex(int skillId);

// Résultat de Net_OnAchievementNotice (EA 0x4ac950) : soit rien (id de compétence non
// reconnu, EA 0x4aca3a), soit un texte prêt à poster.
struct AchievementNoticeResult {
    bool        shown = false;
    std::string text;
};

// Reconstruit le message de Net_OnAchievementNotice SANS l'envoyer au journal (fonction
// pure, testable). `tribeSkillOrMorphId` correspond exactement à l'argument passé à
// TribeSkill_SkillIdToIndex dans l'original (EA 0x4ac9b5) : le binaire y passe
// g_SelfMorphNpcId (dword 0x1675a98), un global de « npc de morph » réutilisé ici comme
// sélecteur de compétence de tribu — c'est ce que montre le désassemblage tel quel, sans
// interprétation ; ce global n'est pas modélisé dans GameState.h, donc l'appelant doit le
// fournir. `achieverName13` correspond au champ nom de 13 o copié en tête de payload
// (EA 0x4ac9a1, &unk_8156C1..+13), non retraité (pas de trim '@' ici, contrairement aux
// listes AutoPlay : l'original ne trim rien sur ce champ).
AchievementNoticeResult BuildAchievementNotice(const AchievementState& state,
                                                int tribeSkillOrMorphId,
                                                const std::string& achieverName13);

// Instance globale unique (miroir de dword_184C218) — alimentée par
// Net_OnAchievementDataLoad (Net/GameHandlers_BossWorld.cpp, opcode 0x98) et lue par
// UI/SocialWindow.h (onglet Succès), même pattern que game::g_Warehouse/g_World.
inline AchievementState g_Achievements;

// Variante pratique : construit ET poste (bannière flottante + ligne système), comme
// EA 0x4aca20 (HUD_ShowFloatingMessage(0,0,&v7,&String)) et 0x4aca35
// (Msg_AppendSystemLine(&v7, g_SysMsgColor)). g_SysMsgColor (EA 0x84dfd8) n'est pas
// modélisé en champ propre -> lu via g_Client.Var() par convention du hub ClientRuntime.
bool PostAchievementNotice(const AchievementState& state, int tribeSkillOrMorphId,
                            const std::string& achieverName13);

// =============================================================================
// 2) LISTES AMI / ENNEMI DE L'AUTOPLAY (seule structure « social list » prouvée)
// =============================================================================

// Une des deux listes (amis OU ennemis) de l'AutoPlay. Layout disque et règles de
// capacité relevés à l'identique dans AutoPlay_Load/SaveFriendList (EA 0x45d730/
// 0x45de50) et AutoPlay_Load/SaveEnemyList (EA 0x45daf0/0x45e140) — les deux paires
// sont bit-à-bit identiques hormis le nom de fichier et les offsets de champ dans la
// classe AutoPlay d'origine.
struct SocialNameList {
    static constexpr size_t kCapacity  = 48;   // EA 0x45dec0 (`*(this+320) <= 0x30`), 0x45e1a3
    static constexpr size_t kSlotBytes = 25;   // stride slot sur disque
    static constexpr size_t kFileBytes = kCapacity * kSlotBytes;   // 1200 o (ReadFile/WriteFile 0x4B0 EA 0x45d7ca/0x45e0ad)

    std::vector<std::string> names;   // ordre = ordre d'insertion (list std du binaire)

    bool Full() const { return names.size() >= kCapacity; }

    // Crt_Strcmp linéaire sur toute la liste — EA 0x45fbb3/0x45fba0 (IsFriend),
    // 0x45fcf3/0x45fce0 (IsEnemy).
    bool Contains(const std::string& name) const;

    // Ajout BRUT (sans vérification d'exclusivité avec l'autre liste — voir
    // AutoPlaySocialLists::AddFriend/AddToBlacklist pour la logique complète telle
    // qu'exercée par AutoPlay_OnMouseUpNameList, EA 0x45b6b0-0x45b90a).
    // NOTE fidélité : l'original valide aussi le nom via MobDb_FindByName (EA 0x4c3c50)
    // avant tout ajout (chargement ET saisie UI) — cette base n'est pas exposée par les
    // headers fournis (GameDatabase.h ne couvre qu'ITEM_INFO/LEVEL_INFO) ; la validation
    // d'existence du nom reste donc à la charge de l'appelant (TODO EA 0x45d9e9/0x45da26,
    // 0x45b63f).
    bool Add(const std::string& name);

    // Retrait par recherche linéaire, 1re occurrence — EA 0x45bb95-0x45bc21 (friend),
    // 0x45bd3e-0x45be34 (enemy).
    bool Remove(const std::string& name);

    // Sérialise au format disque exact : 48 slots x 25 o, remplis de 0x40 ('@')
    // (Crt_Memset(Buffer, 64, 1200) EA 0x45de9f/0x45e182), nom tronqué à 24 caractères
    // utiles (le 25e octet reste du padding '@').
    std::array<uint8_t, kFileBytes> Serialize() const;

    // Recharge depuis `bufBytes` o au format disque : pour chaque slot de 25 o, tronque
    // au premier '@' rencontré (EA 0x45d992/0x45d9af Str_Find('@'), 0x45d9d1 Str_Erase),
    // slot vide ignoré. Écrase la liste en mémoire (List_Clear implicite, EA 0x45d7e5).
    // Si bufBytes < kFileBytes, la liste est simplement vidée (comme un fichier absent).
    void Deserialize(const uint8_t* buf, size_t bufBytes);
};

// Charge/sauvegarde le fichier local exact de l'AutoPlay (chemins et tailles fidèles :
// EA 0x45d7ca "G02_GINFO\\011.BIN" et 0x45db81 "G02_GINFO\\012.BIN", 1200 o fixes).
// Utilise l'API C standard (fopen/fread/fwrite) : le binaire d'origine utilise
// CreateFileA/ReadFile/WriteFile Win32, la sémantique (lecture/écriture bloc fixe,
// échec silencieux -> liste vidée) est préservée.
bool LoadSocialNameListFile(SocialNameList& list, const char* path);
bool SaveSocialNameListFile(const SocialNameList& list, const char* path);

bool LoadFriendListFile(SocialNameList& list);          // EA 0x45d730 -> "G02_GINFO\011.BIN"
bool SaveFriendListFile(const SocialNameList& list);     // EA 0x45de50
bool LoadBlacklistFile(SocialNameList& list);            // EA 0x45daf0 -> "G02_GINFO\012.BIN"
bool SaveBlacklistFile(const SocialNameList& list);      // EA 0x45e140

// Code de résultat d'ajout — reflète les 3 issues distinctes de
// AutoPlay_OnMouseUpNameList (str1947 « déjà dans une liste », str1980 « liste pleine »,
// succès silencieux + sauvegarde immédiate).
enum class SocialListOp { Added, AlreadyListed, ListFull };

// Bundle ami+ennemi de l'AutoPlay avec la logique d'exclusivité mutuelle EXACTE de
// AutoPlay_OnMouseUpNameList (EA 0x45b000) : un nom ne peut être présent que dans une
// seule des deux listes ; l'ajout à l'une échoue si le nom figure déjà dans l'AUTRE
// (même message d'erreur str1947 que pour un doublon dans la même liste — le binaire ne
// distingue pas les deux cas, cf. EA 0x45b679/0x45b848).
struct AutoPlaySocialLists {
    SocialNameList friends;    // this+296/+316/+320 — EA 0x45b7 (ajout), 0x45bb (retrait)
    SocialNameList blacklist;  // this+324/+344/+348 — EA 0x45b8 (ajout), 0x45bd (retrait)

    // Branche mode==0 de AutoPlay_OnMouseUpNameList (ajout côté « amis »).
    // Ordre des tests fidèle : capacité pleine (str1980, EA 0x45b6f1/0x45b811) AVANT
    // test de présence dans l'autre liste (str1947, EA 0x45b723/0x45b850).
    SocialListOp AddFriend(const std::string& name);

    // Branche mode==1 (ajout côté « ennemis / liste noire »), symétrique — EA 0x45b6b0.
    SocialListOp AddToBlacklist(const std::string& name);

    // Retrait — EA 0x45bb95-0x45bc21 (amis) / 0x45bd3e-0x45be34 (ennemis) : recherche
    // linéaire, 1re correspondance, sauvegarde immédiate si trouvé (sinon str1981,
    // EA 0x45ba5b, non modélisé ici : le bool de retour porte cette information).
    bool RemoveFriend(const std::string& name);
    bool RemoveFromBlacklist(const std::string& name);

    bool IsFriend(const std::string& name) const { return friends.Contains(name); }     // EA 0x45faa0
    bool IsEnemy(const std::string& name)  const { return blacklist.Contains(name); }   // EA 0x45fbe0
    // AutoPlay_IsNameListed (EA 0x45f820) : sélectionne UNE SEULE des deux listes selon
    // l'onglet actif d'origine (this+292 == 0 -> amis, ==1 -> ennemis) ; ce comportement
    // dépendant de l'UI n'est pas reproduit ici (hors périmètre données). IsListed()
    // ci-dessous est l'union des deux, utile pour la logique d'exclusivité ci-dessus.
    bool IsListed(const std::string& name) const { return IsFriend(name) || IsEnemy(name); }

    bool LoadAll();
    bool SaveAll() const;
};

} // namespace ts2::game

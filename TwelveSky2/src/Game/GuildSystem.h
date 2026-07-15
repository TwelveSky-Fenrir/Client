// Game/GuildSystem.h — Système de guilde : roster interne (ts2::game).
//
// Réécriture PROPRE (pas byte-exact) du roster de guilde relevé dans le désassemblage
// de TwelveSky2.exe (imagebase 0x400000). Vérité = le DÉSASM (MCP idaTs2) +
// RE/net_handler_notes.md (§ Net_OnGuildRosterUpdate 0x4f / Net_OnGuildRosterReset 0x4a /
// Net_OnTeamSlotAssign 0x56 / TeamFormationDispatch 0x53). Objet d'origine : g_Guild
// @ 0x1839968 (struct globale SANS vtable — champs accédés en arithmétique de pointeur
// brute `*(this+N)` dans le décompilé Hex-Rays, pas de type IDA appliqué).
//
// Fonctions d'origine reproduites ici :
//   Guild_CountMembers          0x66BBC0  -> GuildRoster::CountMembers
//   Guild_SelectNextMember      0x66BC30  -> GuildRoster::SelectNextMember
//   Guild_AddMemberFromInput    0x66BCD0  -> GuildRoster::AddMember (partie état) ;
//                                            envoi réseau (Net_SendOp76) câblé côté
//                                            UI/GuildWindow.cpp::ConfirmAdd
//   Net_OnTeamFormationDispatch 0x491E70 case 8  (@0x492874-0x492923, kick)
//                                            -> GuildRoster::RemoveMember(index)
//   Net_OnTeamFormationDispatch 0x491E70 case 17 (@0x492ded-0x492e2a, self-leave scan)
//                                            -> GuildRoster::RemoveMember(name)
//   UI_GuildMgrWnd_Open         0x667E20  (@0x667E29-0x667EB6, init balayage)
//                                            -> GuildRoster::Reset
//
// LAYOUT DÉDUIT de g_Guild (0x1839968) — décompilation croisée de Guild_CountMembers
// (`*(this+429)`, `(char*)this+13*i+67`), Guild_SelectNextMember (`*(this+399)`,
// `*(this+430)`, Net_SendOp78), Net_OnTeamSlotAssign (`dword_1839EDC[dword_183A020]`,
// 0x493090) et UI_GuildMgrWnd_Open (`*(this+349..430)`, 0x667E20). Seuls les champs
// requis par ces fonctions sont modélisés ; le reste (0..66, leader/co-leader @+28/+41/+54,
// grille de sélection UI dword_183A014/018 utilisée par kick/promote dans
// TeamFormationDispatch, bloc 5o/membre unk_1839D00 @+920) est HORS PÉRIMÈTRE (TODO) :
//
//   +0x43  (0x18399AB) unk_18399AB    : char name[50][13]  — nom NUL-terminé, "" = case vide
//                                        (comparé à &String, string vide @0x7EC95F)
//   +0x2D0 (0x1839C38) dword_1839C38  : int32 rank[50]     — rang par membre (promote/kick/
//                                        leave, cf. TeamFormationDispatch case 8/9/17),
//                                        indexé comme name[] (10*row+col == index membre)
//   +0x574 (0x1839EDC) dword_1839EDC  : int32 slotValue[50]— valeur reçue séquentiellement
//                                        via Net_OnTeamSlotAssign (op 0x56), écrite à
//                                        l'index = curseur courant (même i que name[i]) ;
//                                        sentinelle -2 = "pas encore demandée" (init Open)
//   +0x63C (0x1839FA4) *(this+399)    : bool active        — "balayage roster actif",
//                                        posé à 1 par UI_GuildMgrWnd_Open, gate
//                                        SelectNextMember (si faux -> aucune requête)
//   +0x6B4 (0x183A01C) *(this+429)    : int32 memberCount  — écrit par Guild_CountMembers
//   +0x6B8 (0x183A020) *(this+430)    : int32 cursor       — curseur de balayage séquentiel,
//                                        -1 = avant le premier ; Net_OnTeamSlotAssign
//                                        l'utilise tel quel comme index dans slotValue[]
//
// Roster DISTINCT (hors périmètre, cité pour éviter toute confusion) : g_AllianceRosterNames
// @ 0x16749B8 (5 slots : leader + 4 membres, stride 13 : 16749B8/C5/D2/DF/EC, plus
// g_LocalGuildName 0x168740C annexe) manipulé par Net_OnGuildRosterReset (op 0x4A, 0x4911D0),
// Net_OnGuildRosterUpdate (op 0x4F, 0x4918D0), GuildMemberJoin/Leave/Kick (0x4B/0x4D/0x4E).
// AUCUN rapport avec le roster de 50 membres modélisé ici — ne pas fusionner les deux.
// Désormais RÉELLEMENT modélisé côté ClientSource : Game/GameState.h::AllianceRoster
// (game::g_World.allianceRoster), câblé par Net/GameHandlers_PartyGuild.cpp (mission
// "CABLAGE ROSTER ALLIANCE/GUILDE", 2026-07-14, cf. Docs/TS2_ALLIANCE_PARTY_ROSTER.md §3).
#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Un membre du roster interne de guilde (slot fixe, index 0..49).
// ---------------------------------------------------------------------------
struct GuildMember {
    std::string name;              // unk_18399AB + 13*i ("" = case vide)
    int32_t     rank      = 0;     // dword_1839C38[i]
    int32_t     slotValue = -2;    // dword_1839EDC[i] (-2 = pas encore reçu, cf. UI Open)

    bool Empty() const { return name.empty(); }
};

// ---------------------------------------------------------------------------
// GuildRoster — miroir de l'objet global g_Guild (0x1839968). Instance unique :
// ts2::game::g_Guild (déclarée en bas de ce fichier).
// ---------------------------------------------------------------------------
class GuildRoster {
public:
    static constexpr int kMaxMembers = 50;   // borne des 3 boucles `i < 50` du binaire
    static constexpr int kNameStride = 13;   // (char*)this + 13*i + 67

    std::array<GuildMember, kMaxMembers> members{};

    bool    active      = false; // *(this+399)
    int32_t memberCount = 0;     // *(this+429)
    int32_t cursor      = -1;    // *(this+430)

    // Guild_CountMembers 0x66BBC0 — recompte les slots dont le nom est non vide
    // (Crt_Strcmp(name_i, "") != 0), écrit memberCount. Retourne la nouvelle valeur.
    int CountMembers();

    // Guild_SelectNextMember 0x66BC30 — si !active retourne false sans rien faire.
    // Sinon balaie de cursor+1 à 49 le premier slot non vide : si trouvé, pose
    // cursor=i et retourne true (déclenche l'envoi de la requête d'info membre côté
    // binaire) ; si aucun trouvé, clampe cursor=49 (kMaxMembers-1) et retourne false.
    // Envoi réseau (si retour true -> Net_SendOp78(&unk_846C08, members[cursor].name)) :
    // hors périmètre GuildSystem (module d'état pur, sans dépendance réseau) — câblé
    // côté appelant dans Net/GameHandlers_PartyGuild.cpp (le handler TeamSlotAssign
    // 0x56 enchaîne sur g_Guild.SelectNextMember() après avoir consommé la valeur
    // reçue dans dword_1839EDC[cursor], puis envoie Net_SendOp78(members[cursor].name)
    // si le balayage a trouvé un membre).
    bool SelectNextMember();

    // Guild_AddMemberFromInput 0x66BCD0 — partie état/logique uniquement : le binaire
    // lit une edit-box (GetWindowTextA, UI hors périmètre), vide la boîte
    // (SetWindowTextA), puis teste `name` contre le dictionnaire de mots bannis
    // (maybe_Dict001_MatchWord 0x4C1410, hors périmètre) : si banni -> message système
    // StrTable005(112) (pas d'envoi) ; sinon -> Net_SendOp76(name) (envoi de la demande
    // d'ajout). Ici : `banned` est calculé par l'appelant (UI + dico de mots bannis
    // non modélisés dans ce module) ; ne modifie PAS `members` (le roster n'est mis à
    // jour qu'à la réponse serveur, absente des 3 fonctions cibles). Retourne true si
    // la requête doit être envoyée (name non vide et !banned).
    // Si retour true -> l'appelant envoie Net_SendOp76(&unk_846C08, name)
    // (câblé côté UI/GuildWindow.cpp::ConfirmAdd, cf. Docs/TS2_SENDPACKETS_USAGE_AUDIT.md).
    bool AddMember(const std::string& name, bool banned) const;

    // Dérivé de Net_OnTeamFormationDispatch (0x491E70) case 8 [kick, @0x492874-0x492923] :
    // vide name[index] et remet rank[index]=0 (le binaire efface aussi un champ 5o
    // annexe unk_1839D00, hors périmètre), puis recompte (CountMembers). slotValue[index]
    // n'est PAS touché par l'original (dword_1839EDC n'apparaît pas dans ce chemin).
    // No-op si index hors [0, kMaxMembers).
    void RemoveMember(int index);

    // Variante par nom, dérivée de Net_OnTeamFormationDispatch case 17 (self-leave,
    // @0x492ded-0x492e2a) : balayage linéaire `i=0..49` du premier name[i]==name.
    // NOTE DE FIDÉLITÉ : l'original ne teste PAS si la recherche a échoué et écrit
    // sans garde dword_1839C38[50] quand name est absent du roster (dépassement de
    // tableau de 1 élément, probablement inoffensif car il retombe dans unk_1839D00
    // adjacent) — ce comportement N'EST PAS reproduit ici : si `name` est introuvable,
    // cette fonction ne fait rien (garde de sécurité volontaire, écart documenté).
    void RemoveMember(const std::string& name);

    // Dérivé de UI_GuildMgrWnd_Open (0x667E20 @0x667E29-0x667EB6) : réinitialise
    // l'état de balayage — slotValue[i]=-2 pour i=0..49, active=true, cursor=-1.
    // NE touche PAS name[]/rank[] (remplis ailleurs par la copie du "blob" guilde de
    // 1388 o reçu via TeamFormationDispatch avant l'appel d'origine) et N'appelle PAS
    // CountMembers()/SelectNextMember() (l'original les enchaîne juste après, à faire
    // depuis l'appelant comme dans UI_GuildMgrWnd_Open).
    void Reset();
};

// Instance globale unique (miroir de g_Guild 0x1839968).
inline GuildRoster g_Guild;

} // namespace ts2::game

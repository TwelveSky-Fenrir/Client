// Net/GameHandlers_PartyGuild.cpp — routage des paquets GROUPE / GUILDE / ALLIANCE / ÉQUIPE.
//
// Domaine « party_guild » (RE/handler_domains.json) : invitations (groupe/alliance),
// roster (membres, ajout/retrait/kick), chat de guilde/faction, valeurs/positions/HP
// des membres. Sémantique d'origine : RE/net_handler_notes.md.
//   0x2e PartyInvitePrompt   0x2f PartyInviteDecline  0x30 PartyJoinResult
//   0x34 AllyInvitePrompt    0x35 AllyInviteDecline   0x36 AllyJoinResult
//   0x37 GuildMemberInfo     0x38 GuildInfoUpdate     0x3d PartyResultDialog
//   0x3e PartyMemberNameSet  0x3f PartyMemberValueSet  0x40 PartyMemberClear
//   0x4a GuildRosterReset    0x4b GuildMemberJoin     0x4c GuildChatMessage
//   0x4d GuildMemberLeave    0x4e GuildMemberKick     0x4f GuildRosterUpdate
//   0x53 TeamFormationDispatch 0x54 GuildNoticeChat   0x56 TeamSlotAssign
//   0x5c GuildActionResult   0x5d PartyInviteResult   0x7b PartyMemberTargetSet
//   0x7f PartyMemberHpSet    0x80 PartyMemberUpdate   0x81 PartyItemResult
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // net::g_GmCmdCooldownLatch
#include "Net/SendPackets.h"
#include "Config/GameOptions.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"    // game::g_World (résolution d'entité par identité réseau)
#include "Game/GuildSystem.h"  // game::g_Guild (Guild_SelectNextMember)
#include "Game/MapWarp.h"      // game::BeginWarpToFactionTown (résolution warp, pas l'envoi)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (coordonnées réelles 003.BIN)
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

namespace {

// Couleurs de canal de chat (placeholders fidèles pour g_ChatColor_Guild 0x84DFE8 /
// g_ChatColor_Faction 0x84DFEC du binaire ; valeurs D3DCOLOR exactes à réextraire).
// Vérifié par decompilation+xrefs idaTs2 (2026-07-14, cf. GameHandlers_ChatSocial.cpp) :
// ces globals sont à 0 dans .data et n'ont aucun site d'écriture dans le binaire —
// valeur réelle posée hors binaire statique (config/skin runtime), pas un oubli.
constexpr uint32_t kChatColorGuild   = 0xFF66FF66u; // g_ChatColor_Guild
constexpr uint32_t kChatColorFaction = 0xFFFFCC33u; // g_ChatColor_Faction

// Lit un champ char[N] (13 o) terminé NUL en std::string propre.
template <size_t N>
inline std::string Name(const char (&s)[N]) {
    return std::string(s, ::strnlen(s, N));
}

// Résout l'index d'une entité joueur par identité réseau (idHi,idLo) SANS l'ajouter
// (le handler d'origine ne fait qu'un scan linéaire : dword_1687238/168723C[227*e]).
// index 0 = self. Retourne -1 si absent.
int FindPlayerIndex(uint32_t hi, uint32_t lo) {
    auto& players = ts2::game::g_World.players;
    for (size_t i = 0; i < players.size(); ++i)
        if (players[i].active && players[i].id.hi == hi && players[i].id.lo == lo)
            return static_cast<int>(i);
    return -1;
}

// Ferme la notice modale (registre dword_18225D0/18225D8 d'origine, distinct du
// msgbox g_Client.prompt = dword_1822440/1822450). Modélisée via Var pour fidélité.
void CloseNotice(int type) {
    auto& c = ts2::game::g_Client;
    if (c.VarGet(0x18225D0) && c.VarGet(0x18225D8) == type)
        c.Var(0x18225D0) = 0;
}

} // namespace

void RegisterPartyGuildHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, g_World, Str()

    // 0x2e PartyInvitePrompt — invitation de groupe : ouvre la boîte oui/non (type 8) si le
    // filtre est actif ; sinon refus automatique (Net_SendOp45(2), fidèle à Pkt_PartyInvitePrompt).
    OnPacket<PartyInvitePrompt>(sys, 0x2e, [&sys](const PartyInvitePrompt& p) {
        const std::string nm = Name(p.inviterName);
        if (config::g_Options.FilterPartyInvite) {
            g_Client.prompt.Open(8, "[" + nm + "]" + Str(p.flag == 1 ? 305 : 426), Str(306));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp45(sys.Client(), 2);
            g_Client.msg.System(Str(304));
        }
    });

    // 0x2f PartyInviteDecline — invitation de groupe refusée : ferme le prompt 8 + str307.
    OnTrigger(sys, 0x2f, []() {
        g_Client.prompt.CloseIf(8);
        g_Client.msg.System(Str(307));
    });

    // 0x30 PartyJoinResult — résultat d'adhésion au groupe : ferme la notice 4, str308..313.
    OnPacket<PartyJoinResult>(sys, 0x30, [&sys](const PartyJoinResult& p) {
        CloseNotice(4);
        g_Client.msg.System(Str(308 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0)  // adhésion confirmée -> demande d'infos de groupe (opcode 0x2E, sans payload).
            Net_SendOp46(sys.Client());
    });

    // 0x34 AllyInvitePrompt — invitation d'alliance : mémorise l'invitant + prompt (type 9)
    // si le filtre est actif ; sinon refus automatique (Net_SendOp49(2)).
    OnPacket<AllyInvitePrompt>(sys, 0x34, [&sys](const AllyInvitePrompt& p) {
        const std::string nm = Name(p.name);
        if (config::g_Options.FilterAllyInvite) {
            g_Client.Var(0x1822838) = static_cast<int32_t>(p.inviterId); // dword_1822838 = inviterId
            g_Client.prompt.Open(9, "[" + nm + "]" + Str(325), Str(326));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp49(sys.Client(), 2);
            g_Client.msg.System(Str(324));
        }
    });

    // 0x35 AllyInviteDecline — invitation d'alliance refusée : ferme le prompt 9 + str327.
    OnTrigger(sys, 0x35, []() {
        g_Client.prompt.CloseIf(9);
        g_Client.msg.System(Str(327));
    });

    // 0x36 AllyJoinResult — résultat d'adhésion alliance : ferme la notice 5, str328..333/2237.
    OnPacket<AllyJoinResult>(sys, 0x36, [&sys](const AllyJoinResult& p) {
        CloseNotice(5);
        const int code = static_cast<int>(p.resultCode);
        g_Client.msg.System(code <= 5 ? Str(328 + code) : Str(2237));
        if (code == 0) {
            // Réinit chaîne unk_1822828 (vide) + dword_1822838 = somme de 3 compteurs de staging
            // d'alliance (scalaires longue traîne, adressables via g_Client.Var comme le reste du
            // module) — sans impact sur l'envoi lui-même (Net_SendOp50 ne prend aucun paramètre).
            g_Client.Blob(0x1822828, 1).assign(1, 0); // unk_1822828 (CString vide -> 1er octet NUL)
            g_Client.Var(0x1822838) = g_Client.VarGet(0x1822984) + g_Client.VarGet(0x1822980) +
                                       g_Client.VarGet(0x182297C);
            Net_SendOp50(sys.Client());  // demande du roster d'alliance (opcode 0x32, sans payload).
        }
    });

    // 0x37 GuildMemberInfo — bloc d'infos/liste de membres de guilde : str334.
    OnPacket<GuildMemberInfo>(sys, 0x37, [](const GuildMemberInfo&) {
        g_Client.msg.System(Str(334));
        // TODO(ui): UI_ItemListWin_Open(field0, blockA[128], blockB[96], field228)
        //   — décoder noms/stats des membres.
    });

    // 0x38 GuildInfoUpdate — maj info + roster guilde : header/footer + 8 membres {id,val1,val2}.
    OnPacket<GuildInfoUpdate>(sys, 0x38, [](const GuildInfoUpdate& p) {
        g_Client.Var(0x1822848) = static_cast<int32_t>(p.header); // dword_1822848
        g_Client.Var(0x1822934) = static_cast<int32_t>(p.footer); // dword_1822934
        for (int i = 0; i < 8; ++i) {                             // 8 membres x 12 o
            uint32_t id = 0, v1 = 0, v2 = 0;
            std::memcpy(&id, p.members + 12 * i + 0, 4);
            std::memcpy(&v1, p.members + 12 * i + 4, 4);
            std::memcpy(&v2, p.members + 12 * i + 8, 4);
            g_Client.Var(0x18228CC + 12 * i) = static_cast<int32_t>(id); // dword_18228CC[3*i]
            g_Client.Var(0x18228D0 + 12 * i) = static_cast<int32_t>(v1); // dword_18228D0[3*i]
            g_Client.Var(0x18228D4 + 12 * i) = static_cast<int32_t>(v2); // dword_18228D4[3*i]
        }
        // TODO(state): block128 (nom/notice de guilde) -> dword_182284C (chaîne non modélisée).
    });

    // 0x3d PartyResultDialog — résultat d'action de groupe : ferme la notice 6, str492..497.
    OnPacket<PartyResultDialog>(sys, 0x3d, [&sys](const PartyResultDialog& p) {
        CloseNotice(6);
        g_Client.msg.System(Str(492 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0) {
            // Premier slot non vide de g_PartyRosterNames -> demande ses infos (Net_SendOp56),
            // cf. RE/net_handler_notes.md (PartyResultDialog) + Docs/TS2_ALLIANCE_PARTY_ROSTER.md §2.
            auto& names = g_World.partyRoster.names;
            for (size_t i = 0; i < names.size(); ++i) {
                if (!names[i].empty()) {
                    Net_SendOp56(sys.Client(), static_cast<int8_t>(i));
                    break;
                }
            }
        }
    });

    // 0x3e PartyMemberNameSet (Net_OnPartyMemberNameSet 0x4909A0) — pose le nom d'un membre
    // dans un slot du roster de groupe (g_PartyRosterNames, cf. Game/GameState.h::PartyRoster).
    OnPacket<PartyMemberNameSet>(sys, 0x3e, [](const PartyMemberNameSet& p) {
        const std::string nm = Name(p.name);
        if (p.slotIndex < g_World.partyRoster.names.size())
            g_World.partyRoster.names[p.slotIndex] = nm;
        // TODO(ui): UI_MemberSelectWnd_Open (fenêtre de sélection de membre à inviter).
        //   Aucun message système côté handler d'origine.
    });

    // 0x3f PartyMemberValueSet (Net_OnPartyMemberValueSet 0x490A10) — fixe une valeur de
    // membre si le groupe est actif, puis rescanne le roster de noms pour paginer.
    OnPacket<PartyMemberValueSet>(sys, 0x3f, [&sys](const PartyMemberValueSet& p) {
        if (g_Client.VarGet(0x184BE40)) { // dword_184BE40 = groupe actif
            g_Client.Var(0x184BE50 + 4 * static_cast<int>(p.index)) = static_cast<int32_t>(p.value);
            // Balaye g_PartyRosterNames[index+1..9] ; au premier nom non vide, redemande le
            // suivant (Net_SendOp57) — pagination du roster (RE/net_handler_notes.md).
            auto& names = g_World.partyRoster.names;
            for (size_t i = static_cast<size_t>(p.index) + 1; i < names.size(); ++i) {
                if (!names[i].empty()) {
                    Net_SendOp57(sys.Client(), static_cast<int8_t>(i));
                    break;
                }
            }
        }
    });

    // 0x40 PartyMemberClear (Net_OnPartyMemberClear 0x490AB0) — vide le nom d'un slot du
    // roster de groupe (Crt_StringInit 1 argument sur le binaire = vrai clear-à-vide).
    OnPacket<PartyMemberClear>(sys, 0x40, [](const PartyMemberClear& p) {
        if (p.slotIndex < g_World.partyRoster.names.size())
            g_World.partyRoster.names[p.slotIndex].clear();
    });

    // 0x4a GuildRosterReset (Net_OnGuildRosterReset 0x4911D0) — réinitialise le roster
    // d'alliance complet (5 noms + g_LocalGuildName) ; les 5 noms lus dans le payload sont
    // ignorés par le handler d'origine (reset inconditionnel), cf. Docs/
    // TS2_ALLIANCE_PARTY_ROSTER.md §3.
    OnPacket<GuildRosterReset>(sys, 0x4a, [](const GuildRosterReset& p) {
        g_World.allianceRoster.Reset();
        if (p.mode == 1)      g_Client.msg.System(Str(349));
        else if (p.mode == 2) g_Client.msg.System(Str(881));
    });

    // 0x4b GuildMemberJoin (Net_OnGuildMemberJoin 0x491330) — nouveau membre : inscrit dans
    // la 1re case libre du roster alliance (slots 1..4, slot 0 = leader jamais réattribué),
    // puis annonce "<str350> [nom]<str351>".
    OnPacket<GuildMemberJoin>(sys, 0x4b, [](const GuildMemberJoin& p) {
        const std::string nm = Name(p.name);
        g_World.allianceRoster.AddMember(nm); // no-op silencieux si les 4 slots sont pleins
        g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(351));
    });

    // 0x4c GuildChatMessage — chat de guilde : "<str350> [expéditeur] message".
    OnPacket<GuildChatMessage>(sys, 0x4c, [](const GuildChatMessage& p) {
        const std::string sender = Name(p.senderName);
        const std::string body   = std::string(p.message, ::strnlen(p.message, sizeof p.message));
        // Original : posté seulement si g_ChatShow_Guild==1 (flag non modélisé -> toujours posté).
        g_Client.msg.Chat(Str(350) + " [" + sender + "] " + body, kChatColorGuild, sender.c_str());
    });

    // 0x4d GuildMemberLeave (Net_OnGuildMemberLeave 0x4914D0) — un membre quitte l'alliance :
    // si `nm` == nom local -> vide tout le roster (départ de MOI-même) + Str(882) ; sinon
    // retire `nm` du roster et compacte (cf. AllianceRoster::RemoveMember ci-dessus, note de
    // fidélité sur l'algorithme de compactage). SelfState::localPlayerName vide par défaut
    // (non peuplé par aucun handler à ce jour) -> tant qu'il ne l'est pas, cette branche est
    // simplement jamais prise (dégradation honnête, cf. commentaire GameState.h).
    OnPacket<GuildMemberLeave>(sys, 0x4d, [](const GuildMemberLeave& p) {
        const std::string nm = Name(p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName) {
            g_World.allianceRoster.Reset();
            g_Client.msg.System(Str(882));
        } else {
            g_World.allianceRoster.RemoveMember(nm);
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(352));
        }
    });

    // 0x4e GuildMemberKick (Net_OnGuildMemberKick 0x4916D0) — un membre est expulsé : même
    // logique que GuildMemberLeave ci-dessus (self -> reset + Str(883) ; sinon retire+compacte).
    OnPacket<GuildMemberKick>(sys, 0x4e, [](const GuildMemberKick& p) {
        const std::string nm = Name(p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName) {
            g_World.allianceRoster.Reset();
            g_Client.msg.System(Str(883));
        } else {
            g_World.allianceRoster.RemoveMember(nm);
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(353));
        }
    });

    // 0x4f GuildRosterUpdate (Net_OnGuildRosterUpdate 0x4918D0) — dispatcher à 3 cas
    // (cf. Docs/TS2_ALLIANCE_PARTY_ROSTER.md §3) :
    //   cas 1 : reset complet du roster (dissolution/expulsion soi-même), str354.
    //   cas 2 : message informatif seul (str884), AUCUNE mutation du roster.
    //   cas 3 : si allianceRoster.IsLeader(nom local) (JE suis le leader, slot 0 == mon nom)
    //     -> reset complet, str700 (dissolution volontaire) ; sinon -> même traitement que
    //     cas 1 (reset complet, str354). Les DEUX branches de cas 3 réinitialisent le roster,
    //     seul le message diffère.
    OnPacket<GuildRosterUpdate>(sys, 0x4f, [](const GuildRosterUpdate& p) {
        switch (p.code) {
            case 1: // dissolution : réinitialise tout le roster
                g_World.allianceRoster.Reset();
                g_Client.msg.System(Str(350) + " " + Str(354));
                break;
            case 2:
                g_Client.msg.System(Str(350) + " " + Str(884));
                break;
            case 3: {
                const bool leader = g_World.allianceRoster.IsLeader(g_World.self.localPlayerName);
                g_World.allianceRoster.Reset();
                g_Client.msg.System(Str(350) + " " + Str(leader ? 700 : 354));
                break;
            }
            default:
                break;
        }
    });

    // 0x53 TeamFormationDispatch — MÉGA-DISPATCHER guilde (17 sous-opcodes). statusCode :
    // 0 = succès, >0 = codes d'erreur. Chaque sous-opcode affiche une ligne système et
    // ouvre/ferme une UI guilde (UI_GuildCreate_Open / UI_GuildMgrWnd_Open).
    OnPacket<TeamFormationDispatch>(sys, 0x53, [](const TeamFormationDispatch& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.subOpcode) {
            case 1:            break; // création
            case 2:                   // upgrade
                // TODO(state)+TODO(send) : logique EXACTE (décompilée, RE/net_batches/recv_4.json::
                //   Net_OnTeamFormationDispatch case 2) : si statusCode==0, sous-dispatch sur
                //   dword_1675B10 (1=ouvre juste UI_GuildMgrWnd ; 2=upgrade réel ; 3=simple resync) ;
                //   pour 2 : copier guildBlob[1388] -> unk_1839970, Guild_CountMembers, puis SI
                //   memberCount >= 10*dword_1839980 (dword_1839980 = PALIER DE GUILDE courant, TODO :
                //   champ absent de GuildRoster) -> sous-switch sur ce palier (1..4) avec SEUILS EXACTS
                //   {niveau>=50,gold>=0x1312D00=20000000}/{70,30000000}/{90,40000000}/{113,50000000} ->
                //   si seuils atteints, Net_SendGuarded_7(sys.Client()) (garde anti-spam déjà dans le
                //   builder) ; sinon message d'erreur (str568..577 selon palier/seuil). NE PAS FORCER :
                //   dword_1839980 (palier de guilde) n'est pas un champ de GuildRoster (Game/
                //   GuildSystem.h) — l'ajouter + le peupler correctement (paquet d'origine du palier
                //   non identifié dans ce module) est un travail de modélisation à part entière, pas
                //   une simple mise en relation de builder existant.
                break;
            case 4: case 7:    break; // invitation
            case 8:            break; // exclusion
            case 9: case 10:   break; // candidature / acceptation
            case 5: case 6:    break; // dissolution
            case 17:           break; // promotion
            default:           break;
        }
        // TODO(msg): lignes système par (subOpcode, statusCode) — ids StrTable005 à réextraire.
    });

    // 0x54 GuildNoticeChat — message de chat de faction/guilde : "<str543> [nom] message".
    OnPacket<GuildNoticeChat>(sys, 0x54, [](const GuildNoticeChat& p) {
        const std::string nm   = Name(p.name);
        const std::string body = std::string(p.message, ::strnlen(p.message, sizeof p.message));
        g_Client.msg.Faction(Str(543) + " [" + nm + "] " + body, kChatColorFaction, nm.c_str());
    });

    // 0x56 TeamSlotAssign — écrit une valeur dans le slot d'équipe courant (curseur global) puis
    // enchaîne le balayage du roster (Guild_SelectNextMember) ; un balayage réussi (prochain slot
    // non vide trouvé) envoie Net_SendOp78(name[cursor]) — câblé ici (cf. TODO(send) résolu dans
    // Game/GuildSystem.h : « à brancher depuis Net/GameHandlers_PartyGuild.cpp »).
    OnPacket<TeamSlotAssign>(sys, 0x56, [&sys](const TeamSlotAssign& p) {
        const int idx = g_Client.VarGet(0x183A020);                     // dword_183A020 = curseur
        g_Client.Var(0x1839EDC + 4 * idx) = static_cast<int32_t>(p.value); // dword_1839EDC[idx]
        if (g_Guild.SelectNextMember()) {
            char name13[13] = {};
            const std::string& nm = g_Guild.members[g_Guild.cursor].name;
            const size_t n = nm.size() < sizeof name13 ? nm.size() : sizeof name13;
            std::memcpy(name13, nm.data(), n);
            Net_SendOp78(sys.Client(), name13);
        }
    });

    // 0x5c GuildActionResult — résultat d'action de guilde (3 cas ; extra lu mais inutilisé).
    OnPacket<GuildActionResult>(sys, 0x5c, [](const GuildActionResult& p) {
        (void)p.extra;
        switch (p.action) {
            case 1: if (p.flag == 0) g_Client.msg.System(Str(758)); break;
            case 2: if (p.flag == 0) g_Client.msg.System(Str(759)); break;
            case 3:
                if (p.flag == 0)      g_Client.msg.System(Str(760));
                else if (p.flag == 1) g_Client.msg.System(Str(761));
                break;
            default: break;
        }
    });

    // 0x5d PartyInviteResult — résultat d'invitation de groupe (5 cas ; certains avec "[param]").
    OnPacket<PartyInviteResult>(sys, 0x5d, [](const PartyInviteResult& p) {
        const std::string param = std::to_string(p.param);
        switch (p.code) {
            case 1: g_Client.msg.System(Str(372)); break;
            case 2: g_Client.msg.System("[" + param + "]" + Str(373)); break; // couleur 3
            case 3: g_Client.msg.System(Str(374)); break;
            case 4: g_Client.msg.System("[" + param + "]" + Str(375)); break; // couleur 2
            case 5: g_Client.msg.System(Str(376)); break;
            default: break;
        }
    });

    // 0x7b PartyMemberTargetSet — fixe la cible d'un membre (résolu par identité réseau).
    OnPacket<PartyMemberTargetSet>(sys, 0x7b, [](const PartyMemberTargetSet& p) {
        const int e = FindPlayerIndex(p.idHi, p.idLo);
        if (e >= 0) // word_1687454[454*e] = (u16)targetVal ; stride entité = 908 o
            g_Client.Var(0x1687454 + 908 * e) =
                static_cast<int32_t>(static_cast<uint16_t>(p.targetVal));
    });

    // 0x7f PartyMemberHpSet — pose HP/MP courant+max d'un membre (kind 1/2), message si self.
    OnPacket<PartyMemberHpSet>(sys, 0x7f, [](const PartyMemberHpSet& p) {
        const int e = FindPlayerIndex(p.entityIdHi, p.entityIdLo);
        if (e >= 0 && (p.kind == 1 || p.kind == 2)) {
            g_Client.Var(0x1687458 + 908 * e) = p.curValue; // dword_1687458[227*e]
            g_Client.Var(0x168745C + 908 * e) = p.maxValue; // dword_168745C[227*e]
            if (e == 0) {                                   // self : message + action
                g_Client.msg.System(p.kind == 1 ? Str(2012) : Str(2013));
                // TODO(send): Player_QueueAction_op91 (EA 0x517490) enfile une action sur
                //   g_PlayerCmdController (dword_1669170, gros struct de commandes joueur — throttling/
                //   coalescing par tick) qui finit par émettre Net_SendOp91 (opcode 0x5B, sans payload).
                //   NE PAS FORCER un appel direct à Net_SendOp91 ici : g_PlayerCmdController n'est PAS
                //   modélisé dans ce module (cf. Net/CombatResultApply.cpp lignes 108/196/240/321/347 et
                //   Game/MapWarp.cpp — mêmes TODO(net) non résolus, tous en attente de ce même système
                //   de file de commandes joueur ; précédent délibérément respecté pour rester cohérent).
            }
        }
    });

    // 0x80 PartyMemberUpdate — maj d'un champ de membre + gestion de départ (selector 4 = self).
    OnPacket<PartyMemberUpdate>(sys, 0x80, [](const PartyMemberUpdate& p) {
        const int e = FindPlayerIndex(p.idHi, p.idLo);
        if (e < 0) return;
        switch (p.selector) {
            case 1: case 2: case 3:
                g_Client.Var(0x1687458 + 908 * e) = static_cast<int32_t>(p.value1);
                g_Client.Var(0x168745C + 908 * e) = static_cast<int32_t>(p.value2);
                break;
            case 4:
                if (e == 0) { // self : sortie de groupe -> reset HP/MP + warp ville
                    g_Client.Var(0x1687458) = 0;
                    g_Client.Var(0x168745C) = 0;
                    g_Client.msg.System(Str(117));
                    // Résout la cible de warp (garde + coords) ; l'envoi réseau reste un
                    // TODO(send) interne à MapWarp.cpp (Net_SendPacket_Op20, EA 0x55c66f).
                    BeginWarpToFactionTown(static_cast<int32_t>(net::g_LocalElement), false, 0, &g_CoordResolver);
                }
                break;
            default: break;
        }
    });

    // 0x81 PartyItemResult — pose une cellule d'inventaire (status 1) ou quitte le groupe (2).
    OnPacket<PartyItemResult>(sys, 0x81, [](const PartyItemResult& p) {
        if (p.status == 1) {
            g_Client.inv.Set(p.invRow, p.invCol, p.itemId,
                             p.gridPos % 8, p.gridPos / 8, 0, 0, 0);
            g_Client.msg.System(Str(1977));
        } else if (p.status == 2) {
            g_Client.msg.System(Str(117));
            g_Client.Var(0x1687458) = 0; // dword_1687458[0] (self)
            g_Client.Var(0x168745C) = 0; // dword_168745C[0]
            BeginWarpToFactionTown(static_cast<int32_t>(net::g_LocalElement));
        }
    });
}

} // namespace ts2::net

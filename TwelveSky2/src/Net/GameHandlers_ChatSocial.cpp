// Net/GameHandlers_ChatSocial.cpp — routage du domaine « chat_social ».
//
// Domaine « chat_social » (RE/handler_domains.json) : chat/chuchotement/faction/amis,
// notices, prompts de confirmation (Dlg10/14/19/20) et dialogues de résultat. On
// traduit fidèlement la logique de mise à jour d'état des handlers d'origine
// (RE/net_handler_notes.md) vers le hub game::g_Client (journal de messages, prompts,
// registre de notice, globals scalaires via Var()).
//
//   0x14 ChatNotice            0x29 WhisperReceive        0x2a PartyChatOrInvite
//   0x2b ShoutMessage          0x3a FactionBoardSync      0x3b ConfirmPromptOpenDlg19
//   0x3c ConfirmPromptClose19  0x41 ConfirmPromptOpen20   0x42 ConfirmPromptClose20
//   0x43 TradeResultDialog     0x44 RequestTargetNameSet  0x45 RequestCancelClear
//   0x46 RequestStateSet       0x47 ConfirmPromptOpen10   0x48 ConfirmPromptClose10
//   0x49 ResultDialog340       0x50 ConfirmPromptOpen14   0x51 ConfirmPromptClose14
//   0x52 ResultDialog399       0x55 FactionChatMessage    0x57 SelfFactionChat
//   0x59 WhisperMessage        0x5a TradeChatMessage      0x79 SocialListRemove
//   0x7e FriendStatusNotice    0x8b TradeChatMsg          0x90 FriendListEvent
//   0x9f NpcDialogEvent
#include "Net/GameHandlers.h"
#include "Game/ClientRuntime.h"
#include "Net/SendPackets.h"
#include "Config/GameOptions.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {
namespace {

// Lit un champ char[N] à taille fixe (potentiellement non terminé) en std::string.
inline std::string Fixed(const char* s, size_t n) { return std::string(s, strnlen(s, n)); }

// Couleurs de canal de chat : globals g_ChatColor_* du binaire (0x84DFDC..0x84DFF4),
// NON modélisés ici. La LOGIQUE de sélection (quel canal / GM vs normal) est préservée ;
// la valeur ARGB exacte (D3DCOLOR) n'est pas connue -> placeholders stables et distincts.
// Vérifié par decompilation+xrefs idaTs2 (2026-07-14) : ces 7 dwords sont initialisés à 0
// dans .data et n'ont AUCUN site d'écriture dans tout le binaire (seulement des lectures,
// depuis Pkt_WhisperReceive/Pkt_ShoutMessage/Pkt_PartyChatOrInvite/Net_OnGuild*/
// Net_On*FactionChat/Net_OnTradeChatMessage/Char_DrawNameplate/UI_GameHud_Render) — la
// vraie valeur ARGB est donc posée par un mécanisme hors binaire statique (config/skin
// chargé au runtime, non capturé par cette passe), pas juste "pas encore cherché".
constexpr uint32_t kChatColGM      = 0xFFFF3030u; // g_ChatColor_GM
constexpr uint32_t kChatColShout   = 0xFFFFC030u; // g_ChatColor_Shout
constexpr uint32_t kChatColFaction = 0xFF3080FFu; // g_ChatColor_Faction
constexpr uint32_t kChatColParty   = 0xFF30FF30u; // g_ChatColor_Party
constexpr uint32_t kChatColTrade   = 0xFFFFA030u; // g_ChatColor_Trade

// Registre de notice modale d'origine : dword_18225D0 (actif) / dword_18225D8 (type).
// Distinct du prompt MsgBox (dword_1822440/1822450 = game::PromptState). Modélisé
// fidèlement via Var() : plusieurs dialogues de résultat ferment cette notice.
inline void CloseNoticeIf(int type) {
    if (game::g_Client.Var(0x18225D0) != 0 && game::g_Client.Var(0x18225D8) == type)
        game::g_Client.Var(0x18225D0) = 0;
}

} // namespace

void RegisterChatSocialHandlers(NetSystem& sys) {
    using namespace game;  // g_Client, Str()

    // 0x14 ChatNotice — notice texte : bannière flottante + journal système (g_SysMsgColor).
    OnPacket<ChatNotice>(sys, 0x14, [](const ChatNotice& p) {
        std::string t = Fixed(p.text, sizeof p.text);
        g_Client.msg.Floating(0, 0, t);
        g_Client.msg.System(t);
    });

    // 0x29 WhisperReceive — chuchotement reçu : ligne "[expéditeur] message" au canal whisper.
    OnPacket<WhisperReceive>(sys, 0x29, [](const WhisperReceive& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        // Original : couleur g_ChatColor_GM si l'émetteur commence par "[GM]", sinon whisper ;
        // gate g_ChatShow_Whisper==1 (option d'affichage non modélisée -> toujours posté).
        g_Client.msg.Whisper("[" + who + "] " + msg, who.c_str());
        // Arme le flag « bulle de chat » de l'entité joueur homonyme (RE/net_handler_notes.md :
        // dword_1687520[227*i]=1, horodatage unk_1687524=g_GameTimeSec ; stride 908o = 227*4,
        // même convention que PartyGuild::FindPlayerIndex). PlayerEntity::name (Game/GameState.h)
        // permet la recherche par nom, contrairement au TODO d'origine qui la disait non modélisée.
        for (size_t i = 0; i < g_World.players.size(); ++i) {
            if (g_World.players[i].active && g_World.players[i].name == who) {
                g_Client.Var(0x1687520 + 908u * static_cast<uint32_t>(i)) = 1;
                g_Client.VarF(0x1687524 + 908u * static_cast<uint32_t>(i)) = g_World.gameTimeSec;
                // TODO(state): réinit de la chaîne bulle unk_1687528 (texte affiché dans la
                //   bulle) — pas de stockage per-entité pour ce texte dans PlayerEntity ;
                //   hors périmètre de cette passe (nécessiterait d'étendre GameState.h).
                break;
            }
        }
    });

    // 0x2a PartyChatOrInvite — notifs de groupe (rejoint/quitté) ou message de chat groupe.
    OnPacket<PartyChatOrInvite>(sys, 0x2a, [](const PartyChatOrInvite& p) {
        std::string name = Fixed(p.name, sizeof p.name);
        std::string msg  = Fixed(p.message, sizeof p.message);
        switch (p.selector) {
        case 0:  // a rejoint : ligne système + ligne de chat groupe.
            g_Client.msg.System("[" + name + "]" + Str(299));
            g_Client.msg.Chat("[" + name + "]" + Str(302), kChatColParty, name.c_str());
            break;
        case 1: g_Client.msg.System(Str(300)); break;
        case 2: g_Client.msg.System(Str(301)); break;
        case 3:  // message de chat groupe : affiché si filterFlag>=1 (OU g_Opt_FilterPartyChat non modélisé).
            if (p.filterFlag >= 1)
                g_Client.msg.Chat("[" + name + "] " + msg, kChatColParty, name.c_str());
            else
                g_Client.msg.System(Str(370));
            break;
        default: break;
        }
    });

    // 0x2b ShoutMessage — cri/diffusion : "<str114> [émetteur] message", couleur GM ou shout.
    OnPacket<ShoutMessage>(sys, 0x2b, [](const ShoutMessage& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        bool gm = (std::strncmp(p.senderName, "[GM]", 4) == 0);
        // gate g_ChatShow_Shout==1 (option d'affichage non modélisée -> toujours posté).
        g_Client.msg.Chat(Str(114) + " [" + who + "] " + msg,
                          gm ? kChatColGM : kChatColShout, who.c_str());
    });

    // 0x3a FactionBoardSync — resync carquois/faction (le payload ne porte que le code).
    OnPacket<FactionBoardSync>(sys, 0x3a, [](const FactionBoardSync& p) {
        if (p.code == 0) {
            // TODO(state): recopier le staging carquois/faction (dword_1822848/182284C/1822934,
            //   g_QuiverMain/Count/Socket/Serial/Aux...) vers l'état live — tableaux non modélisés.
            g_Client.Var(0x18398F4) = 3;
            g_Client.msg.System(Str(335));
        } else if (p.code == 1) {
            g_Client.msg.System(Str(327));
        }
        // (fermeture UI ItemListWin / cGameHud_Hide : hors périmètre état.)
    });

    // 0x3b ConfirmPromptOpenDlg19 — ouvre la boîte de confirmation 19 si le filtre est actif ;
    // sinon refus automatique (Net_SendOp55(2), fidèle à Net_OnConfirmPromptOpen_Dlg19).
    OnPacket<ConfirmPromptOpenDlg19>(sys, 0x3b, [&sys](const ConfirmPromptOpenDlg19& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (config::g_Options.FilterPrompt19) {
            g_Client.prompt.Open(19, "[" + nm + "]" + Str(499), Str(500));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp55(sys.Client(), 2);
            g_Client.msg.System(Str(498));
        }
    });

    // 0x3c ConfirmPromptClose_Dlg19 — ferme le dialogue 19 s'il est actif + str501.
    OnTrigger(sys, 0x3c, [] {
        g_Client.prompt.CloseIf(19);
        g_Client.msg.System(Str(501));
    });

    // 0x41 ConfirmPromptOpen_Dlg20 — ouvre la boîte de confirmation 20 si le filtre est actif ;
    // sinon refus automatique (Net_SendOp61(2), fidèle à Net_OnConfirmPromptOpen_Dlg20).
    OnPacket<ConfirmPromptOpen_Dlg20>(sys, 0x41, [&sys](const ConfirmPromptOpen_Dlg20& p) {
        std::string nm = Fixed(p.nameText, sizeof p.nameText);
        if (config::g_Options.FilterPrompt20) {
            g_Client.prompt.Open(20, "[" + nm + "]" + Str(508), Str(509));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp61(sys.Client(), 2);
            g_Client.msg.System(Str(507));
        }
    });

    // 0x42 ConfirmPromptCloseDlg20 — ferme le dialogue 20 s'il est actif + str510.
    OnTrigger(sys, 0x42, [] {
        g_Client.prompt.CloseIf(20);
        g_Client.msg.System(Str(510));
    });

    // 0x43 TradeResultDialog — ferme la notice d'échange (type 9) puis message str511..518.
    OnPacket<TradeResultDialog>(sys, 0x43, [&sys](const TradeResultDialog& p) {
        CloseNoticeIf(9);
        if (p.resultCode <= 7)
            g_Client.msg.System(Str(511 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0)  // accord confirmé -> Net_SendOp62 (opcode 0x3E, sans payload).
            Net_SendOp62(sys.Client());
    });

    // 0x44 RequestTargetNameSet — mémorise la cible d'une requête ; arme l'état requête (=1).
    // Réinitialise (vide) le buffer de nom cible correspondant (unk_1674697 subop1 /
    // unk_167468A subop2) — reset fidèle (Crt_StringInit sans argument = chaîne vide),
    // stocké via g_Client.Blob (13 o, même convention que les autres noms de 13 o du
    // protocole). Rien ne le relit encore côté C++ réécrit (aucune UI de saisie de cible
    // câblée dans ce module), mais l'écriture d'état elle-même est fidèle.
    OnPacket<RequestTargetNameSet>(sys, 0x44, [](const RequestTargetNameSet& p) {
        if (p.subop == 1) {
            g_Client.Var(0x1675B14) = 1;
            g_Client.Blob(0x1674697, 13).assign(13, 0);
        } else if (p.subop == 2) {
            g_Client.Var(0x1675B14) = 1;
            g_Client.Blob(0x167468A, 13).assign(13, 0);
        }
    });

    // 0x45 RequestCancelClear — annulation de requête : remet l'état à 0 + str534.
    OnTrigger(sys, 0x45, [] {
        g_Client.Var(0x1675B14) = 0;  // dword_1675B14
        g_Client.Blob(0x167468A, 13).assign(13, 0);
        g_Client.Blob(0x1674697, 13).assign(13, 0);
        g_Client.msg.System(Str(534));
    });

    // 0x46 RequestStateSet — fixe l'état UI de requête : state 0 -> 1, state 1 -> 2.
    OnPacket<RequestStateSet>(sys, 0x46, [](const RequestStateSet& p) {
        if (p.state == 0)      g_Client.Var(0x1675B14) = 1;
        else if (p.state == 1) g_Client.Var(0x1675B14) = 2;
    });

    // 0x47 ConfirmPromptOpen_Dlg10 — ouvre la boîte de confirmation 10 si le filtre est actif ;
    // sinon refus automatique (Net_SendOp67(2), fidèle à Net_OnConfirmPromptOpen_Dlg10).
    OnPacket<ConfirmPromptOpen_Dlg10>(sys, 0x47, [&sys](const ConfirmPromptOpen_Dlg10& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (config::g_Options.FilterPrompt10) {
            g_Client.prompt.Open(10, "[" + nm + "]" + Str(337), Str(338));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp67(sys.Client(), 2);
            g_Client.msg.System(Str(336));
        }
        // (Auto-hunt + g_InvDirtyEnable : clic auto sur la boîte — hors périmètre état.)
    });

    // 0x48 ConfirmPromptClose_Dlg10 — ferme le dialogue 10 s'il est actif + str339.
    OnTrigger(sys, 0x48, [] {
        g_Client.prompt.CloseIf(10);
        g_Client.msg.System(Str(339));
    });

    // 0x49 ResultDialog340 — ferme la notice (type 7) puis message str340..345.
    OnPacket<ResultDialog340>(sys, 0x49, [](const ResultDialog340& p) {
        CloseNoticeIf(7);
        if (p.status <= 5)
            g_Client.msg.System(Str(340 + static_cast<int>(p.status)));
    });

    // 0x50 ConfirmPromptOpenDlg14 — ouvre la boîte de confirmation 14 si le filtre est actif ;
    // sinon refus automatique (Net_SendOp74(2), fidèle à Net_OnConfirmPromptOpen_Dlg14).
    OnPacket<ConfirmPromptOpenDlg14>(sys, 0x50, [&sys](const ConfirmPromptOpenDlg14& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (config::g_Options.FilterPrompt14) {
            g_Client.prompt.Open(14, "[" + nm + "]" + Str(408), Str(409));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp74(sys.Client(), 2);
            g_Client.msg.System(Str(407));
        }
    });

    // 0x51 ConfirmPromptClose_Dlg14 — ferme le dialogue 14 s'il est actif + str410.
    OnTrigger(sys, 0x51, [] {
        g_Client.prompt.CloseIf(14);
        g_Client.msg.System(Str(410));
    });

    // 0x52 ResultDialog399 — ferme la notice (type 8) puis message str399..404.
    OnPacket<ResultDialog399>(sys, 0x52, [&sys](const ResultDialog399& p) {
        CloseNoticeIf(8);
        if (p.resultCode <= 5)
            g_Client.msg.System(Str(399 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0)  // requête GM confirmée -> Net_SendGuarded_3
            Net_SendGuarded_3(sys.Client());  // (garde anti-spam morph/cooldown déjà intégrée au builder).
    });

    // 0x55 FactionChatMessage — chat de faction : "<str416> [émetteur] message".
    OnPacket<FactionChatMessage>(sys, 0x55, [](const FactionChatMessage& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        // gate g_ChatShow_Faction==1 (option d'affichage non modélisée -> toujours posté).
        g_Client.msg.Faction(Str(416) + " [" + who + "] " + msg, kChatColFaction, who.c_str());
    });

    // 0x57 SelfFactionChat — poste un chat de faction pour un nom donné.
    OnPacket<SelfFactionChat>(sys, 0x57, [](const SelfFactionChat& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        // Original : ignoré si nm == nom du joueur local (byte_1673184) ; non accessible ici -> posté.
        g_Client.msg.Faction(Str(416) + " [" + nm + "]" + Str(417), kChatColFaction, nm.c_str());
    });

    // 0x59 WhisperMessage — MP reçu (subop1) ou écho envoyé (subop2) : "<préfixe> [interlocuteur] msg".
    OnPacket<WhisperMessage>(sys, 0x59, [](const WhisperMessage& p) {
        std::string who = Fixed(p.sender, sizeof p.sender);
        std::string msg = Fixed(p.msg, sizeof p.msg);
        if (p.subop == 1) {  // reçu : bannière flottante + ligne de chat (canal 12, préfixe str840).
            g_Client.msg.Floating(0, 0, "[" + who + "] " + msg);
            g_Client.msg.Whisper(Str(840) + " [" + who + "] " + msg, who.c_str());
        } else if (p.subop == 2) {  // écho envoyé : ligne de chat (canal 27, préfixe str841).
            g_Client.msg.Whisper(Str(841) + " [" + who + "] " + msg, who.c_str());
        }
    });

    // 0x5a TradeChatMessage — chat commerce : "<str113> [émetteur] message".
    OnPacket<TradeChatMessage>(sys, 0x5a, [](const TradeChatMessage& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        // gate g_ChatShow_Trade==1 (option d'affichage non modélisée -> toujours posté).
        g_Client.msg.Chat(Str(113) + " [" + who + "] " + msg, kChatColTrade, who.c_str());
    });

    // 0x79 SocialListRemove — retrait d'un nom des listes sociales (sous-ops 297/298/299).
    OnPacket<SocialListRemove>(sys, 0x79, [](const SocialListRemove& p) {
        // TODO(state): effacer le slot (name+category) dans les tableaux de noms sociaux
        //   non modélisés : listOp 297 -> unk_16869C0 (3x5), 298 -> unk_1686AC4 (4x5),
        //   299 -> unk_1686BC8 (4x5), stride 13 o. NE PAS FORCER : Game/SocialSystem.h §« CE
        //   QUI N'EST PAS ICI » documente explicitement que ces 3 grilles sont un sous-système
        //   DISTINCT de l'AutoPlay ami/ennemi (roster de faction/élément par slot de macro,
        //   probable), non prouvé/non modélisé — décision de périmètre délibérée, pas un oubli.
        std::string nm = Fixed(p.name, sizeof p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName)
            g_Client.msg.System(Str(1906));
    });

    // 0x7e FriendStatusNotice — ami en ligne (subop1) / hors ligne (subop2).
    OnPacket<FriendStatusNotice>(sys, 0x7e, [](const FriendStatusNotice& p) {
        std::string nm  = Fixed(p.name, sizeof p.name);
        std::string cls = Str(static_cast<int>(p.classId) + 75);  // StrTable005(classId+75)
        if (p.subop == 1) {  // en ligne
            g_Client.Var(0x1675DC8) = 1;      // dword_1675DC8
            g_Client.VarF(0x1675DD0) = 0.0f;  // flt_1675DD0
            std::string buf = Str(243) + "[" + nm + "]" + cls;
            g_Client.msg.Floating(0, 0, buf);
            g_Client.msg.System(buf, 1u);
            g_Client.msg.System("[5]" + Str(245), 1u);
        } else if (p.subop == 2) {  // hors ligne
            std::string buf = Str(244) + "[" + nm + "]" + cls;
            g_Client.msg.Floating(0, 0, buf);
            g_Client.msg.System(buf, 1u);
        }
    });

    // 0x8b TradeChatMsg — poste "[nom] message" au canal de chat 24 (f0 ignoré).
    OnPacket<TradeChatMsg>(sys, 0x8b, [](const TradeChatMsg& p) {
        std::string nm  = Fixed(p.name, sizeof p.name);
        std::string msg = Fixed(p.message, sizeof p.message);
        g_Client.msg.Chat("[" + nm + "] " + msg, 24u, nm.c_str());
    });

    // 0x90 FriendListEvent — notice d'ajout/retrait d'ami (4 cas) : flottant + ligne système.
    OnPacket<FriendListEvent>(sys, 0x90, [](const FriendListEvent& p) {
        std::string nm  = Fixed(p.name, sizeof p.name);
        // Str_GetClassLabel 0x557A98 -- transcription EXACTE (verifiee par decompilation
        // directe idaTs2, Net_OnFriendListEvent 0x4ab040) : Str(75+id) pour id dans [0,3],
        // chaine vide sinon (repli &String d'origine) -- meme logique que
        // WorldEntityDispatch.cpp::ClassLabel(). Anciennement un simple Str(param), qui
        // omettait le decalage +75 et le garde-fou hors-plage : corrige ici.
        const int32_t classIdRaw = static_cast<int32_t>(p.param);
        std::string cls = (classIdRaw >= 0 && classIdRaw <= 3) ? Str(75 + classIdRaw) : std::string();
        std::string buf;
        switch (p.code) {
        case 0: buf = "[" + cls + "] [" + nm + "] " + Str(243); break;
        case 1: buf = Str(244); break;
        case 2: buf = std::to_string(p.param) + Str(245); break;
        case 3: buf = "[" + cls + "-" + nm + "] " + Str(246); break;
        default: return;
        }
        g_Client.msg.Floating(0, 0, buf);
        g_Client.msg.System(buf, 1u);
    });

    // 0x9f NpcDialogEvent — résultat de dialogue PNJ : "<nom(StrTable003)> <texte>".
    OnPacket<NpcDialogEvent>(sys, 0x9f, [](const NpcDialogEvent& p) {
        int bodyId;
        switch (p.subOpcode) {
        case 0: bodyId = 2340; break;
        case 1: bodyId = 2342; break;
        case 2: bodyId = 2343; break;
        case 3: bodyId = 2344; break;
        case 4: bodyId = 2345; break;
        case 5: bodyId = 2346; break;
        default: return;
        }
        // StrTable003_Get(dword_84A6A8, p.nameStringId) dans l'original (Net_OnNpcDialogEvent
        // 0x4ad300, verifie par decompilation directe idaTs2) : table 003.DAT distincte de
        // StrTable005, non chargee/indexee cote ClientSource (meme lacune reelle que
        // WorldEntityDispatch.cpp::SkillName -- decision de perimetre File/Asset, pas Net).
        // Str() (StrTable005, texte stable "#<id>") sert de repere en attendant.
        std::string nm  = Str(static_cast<int>(p.nameStringId));
        std::string buf = nm + " " + Str(bodyId);
        g_Client.msg.Floating(2, 1, buf);
        g_Client.msg.System(buf);
        if (p.subOpcode == 0 || p.subOpcode == 2)  // 2e ligne
            g_Client.msg.System(Str(2341));
    });
}

} // namespace ts2::net

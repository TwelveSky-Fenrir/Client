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

// ---------------------------------------------------------------------------
// GATES D'AFFICHAGE DE CANAL (g_ChatShow_*) — délibérément NON implémentés ici.
//
// Les handlers 0x29/0x2b/0x55/0x5a n'émettent leur ligne que si le flag de canal vaut
// exactement 1 : g_ChatShow_Whisper (0x184C634) 0x48f278, g_ChatShow_Shout (0x184C644)
// 0x48f6b5, g_ChatShow_Faction (0x184C63C) 0x493058, g_ChatShow_Trade (0x184C640) 0x494465.
//
// Ce que la Passe 4 a établi (et qui interdit de les câbler depuis Net) :
//  - Ce sont des CHAMPS de l'objet g_ChatManager 0x184C3C8 (+0x26C whisper, +0x274 faction,
//    +0x278 trade, +0x27C shout), d'où l'absence d'écriture visible en xrefs absolus.
//  - Leur défaut RUNTIME est 1, posé par UI_GameHud_Init 0x675140 (`[this+26Ch]=1` 0x67519e,
//    +0x274 0x6751b8, +0x278 0x6751c5, +0x27C 0x6751d2) ; et `this == g_ChatManager` est
//    prouvé par le site d'appel `mov ecx, offset g_ChatManager` 0x5AC1A3 / `call
//    UI_GameHud_Init` 0x5AC1A8 (UI_InitAllDialogs). Les octets STATIQUES à 0x184C634..0x184C644
//    sont tous NULS (get_bytes) : l'init est donc indispensable.
//  - Les seuls écrivains ensuite sont des bascules UI pures (UI_RouteLButtonDown 0x5AC740,
//    `cmp ==1 -> 0 sinon 1`, ex. 0x5acb18-0x5acb2d) = les boutons d'onglet de canal du HUD.
//
// Conséquence : poser ces gates ici via Var() (défaut 0) ferait DISPARAÎTRE tout le chat —
// on échangerait un défaut mineur contre une régression grave. Les amorcer à 1 depuis un
// enregistrement de handlers réseau serait une init HUD égarée, et sans écrivain (UI, hors
// périmètre de ce front) le gate resterait constant à 1 = zéro changement observable.
// -> Câblage à poser côté UI : init à 1 dans l'équivalent d'UI_GameHud_Init + bascule dans
//    l'équivalent d'UI_RouteLButtonDown ; les gates seront alors branchés ici.
// ---------------------------------------------------------------------------

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
        // Pkt_WhisperReceive 0x48f210. Format "[%s] %s"(name, message) — 0x48f269 : fidèle.
        //
        // DÉFAUT CONNU (câblage inter-vagues requis, NON corrigeable depuis ce fichier) :
        // le binaire copie les 4 premiers octets du nom dans un buffer pré-zéroté (0x48f2a9)
        // puis `if (Crt_Strcmp(&v0, "[GM]")) -> g_ChatColor_Whisper (0x48f304) else
        // -> g_ChatColor_GM (0x48f2e3)` — soit, en clair, couleur GM ssi le nom commence par
        // "[GM]" (équivalent exact de strncmp(name,"[GM]",4)==0, cf. handler 0x2b infra qui
        // implémente bien ce test). Ici la ligne part TOUJOURS en couleur whisper, parce que
        // MessageLog::Whisper(t, who) (Game/ClientRuntime.h:44) n'a PAS de paramètre couleur et
        // code 0xFFFF80FF en dur (ClientRuntime.cpp:20) ; Game/ClientRuntime.* appartient à une
        // autre vague. Basculer sur msg.Chat(txt, color, who) échangerait un défaut contre un
        // autre : MsgKind::Whisper pilote le badge « non lu » de l'onglet Whisper
        // (UI/ChatWindow.cpp:157-162), qui serait perdu.
        // -> Câblage à poser : surcharge `Whisper(const std::string&, const char*, uint32_t)`
        //    conservant MsgKind::Whisper, puis `gm ? kChatColGM : <couleur whisper>` ici.
        //
        // Gate g_ChatShow_Whisper==1 (0x48f278) : voir la note « gates de canal » en tête de
        // fichier — délibérément NON implémenté ici (le poser sans son initialiseur ferait
        // disparaître tout le chat). La boucle de bulle infra est HORS gate dans le binaire
        // (0x48f309) : sa position ici est donc correcte.
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
    // Transcription des 4 Crt_Vsnprintf de Pkt_PartyChatOrInvite 0x48f3c0 (formats relus en
    // désassemblage : l'ordre des varargs cdecl est empilé droite->gauche).
    OnPacket<PartyChatOrInvite>(sys, 0x2a, [](const PartyChatOrInvite& p) {
        std::string name = Fixed(p.name, sizeof p.name);
        std::string msg  = Fixed(p.message, sizeof p.message);
        switch (p.selector) {
        case 0:  // a rejoint : ligne système + ligne de chat groupe.
            // "[%s]%s"(name, Str(299)) -> Msg_AppendSystemLine — 0x48f484 / 0x48f49e.
            g_Client.msg.System("[" + name + "]" + Str(299));
            // "%s %s"(Str(302), message) -> Msg_AppendChatLine(g_ChatColor_Party, &String) —
            // 0x48f4c6 / 0x48f4e6. Le 4e argument (locuteur) est le global `String` 0x7ec95f,
            // dont l'octet 0 vaut 0x00 (vérifié get_bytes) = chaîne VIDE, PAS le nom : la
            // ligne de bienvenue n'est attribuée à personne. Le message du payload (v9) est
            // bien le 2e vararg — il était perdu par la forme précédente "[nom]Str302".
            g_Client.msg.Chat(Str(302) + " " + msg, kChatColParty, "");
            break;
        // "[%s]%s"(name, Str(300)) — 0x48f513 ; "[%s]%s"(name, Str(301)) — 0x48f55b.
        // Le nom (v11) est le PREMIER vararg : sans lui le joueur ne voit pas QUI part/rejoint.
        case 1: g_Client.msg.System("[" + name + "]" + Str(300)); break;
        case 2: g_Client.msg.System("[" + name + "]" + Str(301)); break;
        case 3:  // message de chat groupe.
            // Garde 0x48f57f-0x48f58f : `cmp [ebp+var_448], 1 / jge` puis
            // `cmp g_Opt_FilterPartyChat, 0 / jnz` = `if (v8 >= 1 || g_Opt_FilterPartyChat)`.
            // `jge` = comparaison SIGNÉE -> filterFlag est relu en int32_t (un uint32_t ferait
            // basculer 0x80000000..0xFFFFFFFF du mauvais côté de la garde).
            // g_Opt_FilterPartyChat 0x84DEFC EST modélisé (Config/GameOptions.h:114, défaut 1
            // @GameOptions.cpp:42) : l'ancien commentaire « non modélisé » était périmé et sa
            // disjonction manquante faisait afficher Str(370) dès que le serveur envoyait 0.
            if (static_cast<int32_t>(p.filterFlag) >= 1 || config::g_Options.FilterPartyChat != 0)
                // TODO(audio) [ancre 0x48f5bf] : Snd3D_PlayScaledVolume(flt_1490B3C, 0, 100, 1)
                //   — module audio non câblé depuis Net (même convention que
                //   GameHandlers_Misc.cpp:452).
                // "%s [%s] %s"(Str(302), name, message), locuteur = v11 = name — 0x48f5ee / 0x48f610.
                g_Client.msg.Chat(Str(302) + " [" + name + "] " + msg, kChatColParty, name.c_str());
            else
                g_Client.msg.System(Str(370));  // 0x48f5a2 / 0x48f5ad
            break;
        default: break;
        }
    });

    // 0x2b ShoutMessage — cri/diffusion : "<str114> [émetteur] message", couleur GM ou shout.
    OnPacket<ShoutMessage>(sys, 0x2b, [](const ShoutMessage& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        bool gm = (std::strncmp(p.senderName, "[GM]", 4) == 0);
        // Gate g_ChatShow_Shout==1 (Pkt_ShoutMessage 0x48f640 @0x48f6b5) : non implémenté,
        // cf. note « GATES D'AFFICHAGE DE CANAL » en tête de fichier.
        g_Client.msg.Chat(Str(114) + " [" + who + "] " + msg,
                          gm ? kChatColGM : kChatColShout, who.c_str());
    });

    // 0x3a FactionBoardSync — resync carquois/faction (le payload ne porte que le code).
    // Net_OnFactionBoardSync 0x490560. Le CŒUR du handler est le commit staging -> live : le
    // staging est produit par le handler 0x38 GuildInfoUpdate (GameHandlers_PartyGuild.cpp:188-217,
    // qui documente explicitement le passage de relais vers ce fichier) et n'est OBSERVABLE que
    // par ce commit. Les 9 destinations live ne sont pas modélisées en champ propre : on emploie
    // l'échappatoire Var()/VarGet() de ClientRuntime.h:163 (même convention que le staging).
    // Strides re-dérivés des `refs` IDA (indices sur `int`) : g_QuiverMain 0x1673EB4 /
    // g_QuiverCount 0x1673EB8 / g_QuiverSocket 0x1673EBC / g_QuiverSerial 0x1673EC0 sont
    // indexés [4*i] -> stride 16 o ; g_QuiverAux 0x1675154 / 0x1675158 / 0x167515C sont
    // indexés [3*i] -> stride 12 o.
    OnPacket<FactionBoardSync>(sys, 0x3a, [](const FactionBoardSync& p) {
        if (p.code == 0) {
            g_Client.Var(0x1673EB0) = g_Client.VarGet(0x1822848);  // 0x49059a
            g_Client.Var(0x1675624) = g_Client.VarGet(0x1822934);  // 0x4905a5
            for (int i = 0; i < 8; ++i) {                          // boucle i<8 — 0x4905aa
                g_Client.Var(0x1673EB4 + 16 * i) = g_Client.VarGet(0x182284C + 16 * i); // g_QuiverMain[4*i]   0x4905d8
                g_Client.Var(0x1673EB8 + 16 * i) = g_Client.VarGet(0x1822850 + 16 * i); // g_QuiverCount[4*i]  0x4905f0
                g_Client.Var(0x1673EBC + 16 * i) = g_Client.VarGet(0x1822854 + 16 * i); // g_QuiverSocket[4*i] 0x490608
                g_Client.Var(0x1673EC0 + 16 * i) = g_Client.VarGet(0x1822858 + 16 * i); // g_QuiverSerial[4*i] 0x490620
                g_Client.Var(0x1675154 + 12 * i) = g_Client.VarGet(0x18228CC + 12 * i); // g_QuiverAux[3*i]    0x490638
                g_Client.Var(0x1675158 + 12 * i) = g_Client.VarGet(0x18228D0 + 12 * i); // dword_1675158[3*i]  0x490650
                g_Client.Var(0x167515C + 12 * i) = g_Client.VarGet(0x18228D4 + 12 * i); // dword_167515C[3*i]  0x490668
            }
            g_Client.Var(0x18398F4) = 3;    // 0x490673
            // TODO(ui) [ancre 0x490684] : UI_ItemListWin_Close(dword_1822820, 0) (0x5D1820) —
            //   la fenêtre ItemListWin n'est pas modélisée côté C++ (cf. TODO(ui) jumeau
            //   GameHandlers_PartyGuild.cpp:182) ; Net n'inclut jamais UI/.
            g_Client.msg.System(Str(335));  // 0x49069a / 0x4906a5
        } else if (p.code == 1) {
            // TODO(ui) [ancres 0x4906b1 / 0x4906bd] : cGameHud_Hide(dword_1839568) (0x62B050)
            //   = fermeture du classeur HUD, puis UI_ItemListWin_Close(dword_1822820, 0).
            //   Actions UI pures, non atteignables depuis Net (aucun fichier Net/ n'inclut UI/).
            g_Client.msg.System(Str(327));  // 0x4906d2 / 0x4906dd
        }
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
    // Net_OnRequestTargetNameSet 0x490DA0. COPIE le nom de cible reçu dans le buffer
    // correspondant (g_PendingReqTargetName_Sub1 0x1674697 pour subop 1,
    // g_PendingReqTargetName_Sub2 0x167468A pour subop 2) — ce n'est PAS un clear, malgré
    // l'apparence du pseudocode Hex-Rays qui MASQUE le 2e argument. Désassemblage :
    //   0x490DEA lea eax,[ebp+var_14] / push eax          ; arg2 = src = nom du payload
    //   0x490DEE push offset g_PendingReqTargetName_Sub1  ; arg1 = dest
    //   0x490DF3 call Crt_StringInit                      ; (idem 0x490E07-0x490E10 pour Sub2)
    // Crt_StringInit 0x75CAB0 == strcpy(dest, src) : `push edi / mov edi,[esp+4+arg_0]` (dest)
    // puis `jmp loc_75CB25` qui tombe dans la boucle de copie de Crt_Strcat 0x75CB25
    // (`mov ecx,[esp+4+arg_4]` = src, arrêt au 1er NUL, retour = dest). D'où la sémantique
    // strcpy ci-dessous et NON un memcpy aveugle de 13 o.
    // Ces deux blobs ONT des consommateurs bien réels côté C++, qui lisent tous jusqu'au 1er
    // NUL (mêmes conventions que la copie ci-dessous) :
    //   - Scene/SceneManager.cpp:785-792 (readReqName -> game::HasPendingTargetRequest), qui
    //     alimente le poll Net_SendOp64 (Game/InGameTickFlow.cpp:28-30) ;
    //   - UI/GameHud.cpp:1035 (ReadTargetName) -> ResolveTargetPlate(0x167468A / 0x1674697),
    //     qui AFFICHE les plaques de nom de cible (GameHud.cpp:1138-1139/1213-1214/1350-1351).
    // L'effacement précédent (.assign(13,0)) laissait donc en permanence les deux buffers
    // vides : plaques de cible muettes ET poll de requête jamais émis.
    // Le vrai clear, lui, est le handler 0x45 (Net_OnRequestCancelClear 0x490E30), infra :
    // son src est `offset String` 0x7EC95F, dont l'octet 0 vaut 0x00 -> chaîne vide.
    OnPacket<RequestTargetNameSet>(sys, 0x44, [](const RequestTargetNameSet& p) {
        // dest = 13 o exactement (0x1674697 - 0x167468A = 13).
        auto setName = [&p](uint32_t addr) {
            auto& b = g_Client.Blob(addr, 13);
            size_t n = 0;
            while (n < sizeof p.name && p.name[n] != 0) ++n;  // strcpy : arrêt au 1er NUL
            b.assign(13, 0);                                  // reste zero-fill -> NUL-terminé
            std::memcpy(b.data(), p.name, n);
        };
        if (p.subop == 1) {          // 0x490DDC (cmp 1) .. 0x490DFB
            setName(0x1674697);      // strcpy(g_PendingReqTargetName_Sub1, payloadName)
            g_Client.Var(0x1675B14) = 1;
        } else if (p.subop == 2) {   // 0x490DE2 (cmp 2) .. 0x490E18
            setName(0x167468A);      // strcpy(g_PendingReqTargetName_Sub2, payloadName)
            g_Client.Var(0x1675B14) = 1;
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
        // RÉSIDUEL CONNU (hors périmètre — dépendances non modélisées) : après
        // UI_MsgBox_Open (0x490F8E), Net_OnConfirmPromptOpen_Dlg10 0x490EE0 teste
        // `if ((g_AutoHuntFuelA > 0 || g_AutoHuntFuelB > 0) && g_InvDirtyEnable == 1)`
        // (0x490FAC ; globals 0x16755A4 / 0x16755A8 / 0x16755AC), pose dword_1822444 = 1
        // (0x490FFA) puis appelle UI_MsgBox_OnLButtonUp(dword_1822438, v8+170,
        // v5-Height/2+95) (0x49101F) = un clic gauche SYNTHÉTIQUE sur la boîte, donc une
        // ACCEPTATION AUTOMATIQUE du prompt quand l'auto-chasse tourne. Non réimplémenté :
        // les coordonnées dépendent de nWidth/nHeight (0x1669184 / 0x1669188) et de
        // Sprite2D_GetWidth/GetHeight(&unk_8E8F5C), non modélisés, et le clic est une action
        // UI (Net n'inclut jamais UI/). Divergence observable uniquement auto-chasse active :
        // l'original accepte seul, la réécriture laisse la boîte ouverte.
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
        // Gate g_ChatShow_Faction==1 (Net_OnFactionChatMessage 0x492fe0 @0x493058) : non
        // implémenté, cf. note « GATES D'AFFICHAGE DE CANAL » en tête de fichier.
        g_Client.msg.Faction(Str(416) + " [" + who + "] " + msg, kChatColFaction, who.c_str());
    });

    // 0x57 SelfFactionChat — poste un chat de faction pour un nom donné.
    // Net_OnSelfFactionChat 0x4930d0 : `result = Crt_Strcmp(byte_1673184, v3); if (result) {…}`
    // (0x493105 / 0x49310f) -> la ligne n'est postée QUE si le nom reçu DIFFÈRE du nom du
    // joueur local (gate anti-écho). byte_1673184 EST modélisé : Game/GameState.h:427
    // SelfState::localPlayerName, peuplé par UI/LoginScene.cpp:1116 — l'ancien commentaire
    // « non accessible ici » était périmé (ce même champ est déjà lu au handler 0x79 infra
    // et dans GameHandlers_PartyGuild.cpp:329). Politique `.empty()` documentée
    // GameState.h:425 : nom local vide -> aucun faux « c'est moi », on poste.
    OnPacket<SelfFactionChat>(sys, 0x57, [](const SelfFactionChat& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName)
            return;  // Crt_Strcmp == 0 (noms égaux) -> 0x49316d, rien n'est posté
        // "%s [%s]%s"(Str(416), name, Str(417)), locuteur = v3 = name — 0x493146 / 0x493168.
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
        // Gate g_ChatShow_Trade==1 (Net_OnTradeChatMessage 0x4943f0 @0x494465) : non
        // implémenté, cf. note « GATES D'AFFICHAGE DE CANAL » en tête de fichier.
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
        //
        // Gate de sous-opcode : le `switch (v3)` 0x4a94c2 de Net_OnSocialListRemove 0x4a9450
        // ne comporte QUE les cas 297/298/299 ; son `default:` fait `return result` dès
        // 0x4a94d8, donc SANS jamais atteindre le Crt_Strcmp(byte_1673184, v4) (0x4a967d) ni
        // le Str(1906) (0x4a969a), tous deux placés APRÈS le switch. Sans ce filtre, Str(1906)
        // était émis pour n'importe quel listOp.
        if (p.listOp != 297 && p.listOp != 298 && p.listOp != 299)
            return;  // default: -> 0x4a94d8
        std::string nm = Fixed(p.name, sizeof p.name);
        // `result = Crt_Strcmp(byte_1673184, v4); if (!result)` (0x4a9687) -> posté quand les
        // noms sont ÉGAUX (c'est bien MOI qu'on retire de la liste).
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName)
            g_Client.msg.System(Str(1906));
    });

    // 0x7e FriendStatusNotice — ami en ligne (subop1) / hors ligne (subop2).
    // Net_OnFriendStatusNotice 0x4aa050. Format "%s[%s]%s" : l'empilement cdecl (droite->gauche)
    // relu en désassemblage 0x4aa113-0x4aa138 donne les varargs (classe, nom, Str243) — soit
    // "classe[nom]Str243", et NON "Str243[nom]classe" comme le construisait la forme précédente.
    // Str(classId+75) est appelé SANS clamp ici (0x4aa129/0x4aa1ba) — fidèle : contrairement au
    // handler 0x90 qui, lui, passe par Str_GetClassLabel (garde-fou [0,3]).
    OnPacket<FriendStatusNotice>(sys, 0x7e, [](const FriendStatusNotice& p) {
        std::string nm  = Fixed(p.name, sizeof p.name);
        std::string cls = Str(static_cast<int>(p.classId) + 75);  // StrTable005(classId+75)
        if (p.subop == 1) {  // en ligne — 0x4aa0e3
            g_Client.Var(0x1675DC8) = 1;      // dword_1675DC8  0x4aa0f7
            g_Client.VarF(0x1675DD0) = 0.0f;  // flt_1675DD0    0x4aa103
            std::string buf = cls + "[" + nm + "]" + Str(243);  // 0x4aa138
            // Ordre du binaire : Msg_AppendSystemLine (0x4aa14b) PUIS HUD_ShowFloatingMessage
            // (0x4aa162) — l'inverse de la forme précédente.
            g_Client.msg.System(buf, 1u);
            g_Client.msg.Floating(0, 0, buf);  // (this, 0, 0, buf, &String) -> floatType=0, flag=0
            g_Client.msg.System("[5]" + Str(245), 1u);  // 0x4aa180 / 0x4aa193
        } else if (p.subop == 2) {  // hors ligne — 0x4aa0ec
            std::string buf = cls + "[" + nm + "]" + Str(244);  // 0x4aa1c9
            g_Client.msg.System(buf, 1u);      // 0x4aa1dc
            g_Client.msg.Floating(0, 0, buf);  // 0x4aa1f3
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
        // HUD_ShowFloatingMessage(dword_1821D58, 1u, 0, v10, &String) — 0x4ab1a5 : le slot-type
        // poussé est 1 (`push 0 / push 1` juste avant, args droite->gauche), PAS 0 -> le
        // flottant s'affichait au mauvais slot visuel. Mapping confirmé par les voisins fidèles
        // (0x9a/0x9d -> Floating(2,1) ; 0x99 -> Floating(0,0)). Ordre Floating (0x4ab1a5) puis
        // System (0x4ab1cd) : conforme au binaire ici (contrairement à 0x7e, inverse).
        g_Client.msg.Floating(1, 0, buf);
        g_Client.msg.System(buf, 1u);  // Msg_AppendSystemLine(..., 1) — 0x4ab1cd
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

// Net/GameHandlers_Misc.cpp — routage des paquets DIVERS vers l'état runtime
// (game::g_Client). Domaine « misc » (RE/handler_domains.json).
//
// Regroupe le méga-dispatcher de variables de jeu, la connexion au game server,
// les déclencheurs de script (anticheat, ignoré), timers/compte à rebours, la
// synchro des raccourcis (quickslots), le dispatcher de talisman/pet, cooldowns
// et auras de compétence, la cultivation (attributs/growth/buffs), l'invocation
// (summon), les effets de buff, la réanimation, le tableau PvP et les notices
// (système / achievement / serveur). Traduction fidèle de la sémantique décrite
// dans RE/net_handler_notes.md ; les envois automatiques (Net_SendOp*) et le
// rendu UI exact restent en `// TODO(send)` / `// TODO(state)`. Les globals
// scalaires de la longue traîne (dword_XXXX) passent par g_Client.Var(addr).
//
//   0x0e SystemMessageBox        0x16 SetGameVar           0x18 GameServerConnectResult
//   0x27 QuestInteractResult     0x28 ToggleObserver       0x39 PvpTallyUpdate
//   0x58 CultivationDispatch     0x5b QuickslotSync        0x61 ServerNameNotice
//   0x62 Sub_4A55E0 (*)          0x63 ScriptTrigger         0x66 PetSlotDispatch
//   0x6f SkillCooldownSet        0x71 Sub_4A7150 (*)        0x72 RevivePrompt
//   0x73 CountdownTimerStart     0x76 MinigameStateLoad     0x7d SkillAuraSync
//   0x82 Sub_4AAB60 (*)          0x84 SummonSpawn           0x85 SystemNotice
//   0x8f Sub_4AB020 (*)          0x99 AchievementNotice     0xae BuffEffectDispatch
//   0xb1 Sub_4B33C0 (*)
// (*) trous de couverture comblés — absents de RE/handler_domains.json et de tout
//     Register*Handlers avant cet ajout (cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md).
//
// RÈGLE : ce module N'ÉDITE PAS l'état partagé (ClientRuntime.h) — il l'utilise.
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // ts2::net::g_GmCmdCooldownLatch, g_MorphInProgress (0x1675A88)
#include "Net/Login.h"         // net::ConnectGameServer 0x462A70, net::kLoginHostCom, codes kNet*
#include "Net/SendPackets.h"   // net::Net_SendPacket_Op21 0x4B5190
#include "Game/ClientRuntime.h"
#include "Game/MapWarp.h"      // game::BeginWarpToMap37/BeginWarpToFactionTown (0x62/0x8f)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (résolution coords warp)
#include "Game/SocialSystem.h" // game::g_Achievements/PostAchievementNotice (AchievementNotice 0x99)

#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

// Adresses d'origine (globals non modélisés en champ propre, via g_Client.Var) et
// helpers locaux. Les adresses « 0xE00000NN » sont des clés SYNTHÉTIQUES stables
// pour des scalaires dont l'adresse d'origine n'est pas relevée dans les notes
// (hors plage image réelle -> pas de collision avec un dword_XXXX authentique).
namespace {
// g_TalismanSlot 0x1674760 — adresse RÉELLE (confirmée par Game/AutoPlaySystem.h:101 et
// Game/StatFormulas.h:37/StatFormulas.cpp:276, indépendamment de ce module) ; PAS une clé
// synthétique, contrairement au commentaire générique ci-dessus — partagée avec
// AutoPlaySystem (dword_1675664[slot] pour slot<10 dans Game/StatFormulas.cpp:276).
constexpr uint32_t kTalismanSlot     = 0x1674760u;  // g_TalismanSlot (index talisman, -1 = aucun)
constexpr uint32_t kPendingStopReq   = 0xE0000072u; // g_PendingStopRequest (invite réanimation)
// (ex-kMorphInProgress 0xE0000018 supprimé : clé synthétique morte — le VRAI global
//  morph est net::g_MorphInProgress = g_MorphInProgress 0x1675A88, ClientState.h:18,
//  celui que lisent les builders Net_Send* ; désormais écrit directement, cf. 0x18.)
// Base synthétique pour le fond du switch SetGameVar (une clé par varId).
constexpr uint32_t kGameVarBase      = 0xE0160000u; // dword sélectionné par varId

// Lecture non alignée d'un u32 depuis un tampon d'octets (comme le memcpy d'origine).
inline uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, sizeof v); return v; }
inline float    RdF32(const uint8_t* p) { float v; std::memcpy(&v, p, sizeof v); return v; }

// Recalcul des bornes de rating d'attaque (dword_168736C/1687370/1687374/1687378 = base
// min/cur min/base max/cur max), motif identique à Net/GameVarDispatch.cpp::
// ratingRecalcBothClamp (cases 32/40/71 du même moteur de stats, majoritaire dans ce
// dispatcher : 3 occurrences sur 5). Original : Char_CalcAttackRatingMin/Max (0x4CD970/
// 0x4CE3F0) sur g_EquipSnapshotScratch (snapshot d'équipement dédié, absent de ce module) ;
// on approx par g_World.self.atkRatingMin/Max déjà calculées par StatEngine (même
// approximation assumée que GameVarDispatch.cpp, pour rester cohérent — PAS un nouvel
// écart introduit ici). Utilisé par PetSlotDispatch (0x66) cases 6/7/8 (RE/
// net_handler_notes.md : « recalcule bornes d'attaque -> dword_168736C/1687370/1687374/
// 1687378 »).
void RecalcTalismanAttackRating() {
    const int32_t mn = game::g_World.self.atkRatingMin;
    game::g_Client.Var(0x168736C) = mn;                                          // base min
    if (game::g_Client.VarGet(0x1687370) > mn) game::g_Client.Var(0x1687370) = mn; // cur min (clamp)
    const int32_t mx = game::g_World.self.atkRatingMax;
    game::g_Client.Var(0x1687374) = mx;                                          // base max
    if (game::g_Client.VarGet(0x1687378) > mx) game::g_Client.Var(0x1687378) = mx; // cur max (clamp)
}
} // namespace

// hWndParent 0x815184 — fenêtre destinataire du WSAAsyncSelect(WM_USER+1) que pose
// Net_ConnectGameServer 0x462A70. Dans le binaire, App_WinMain 0x4609C0 renseigne ce
// global ; le layer réseau réécrit le reçoit en PARAMÈTRE (net::ConnectGameServer
// notifyWnd, Net/Login.h:59). Le seul appelant C++ existant (LoginScene, flux
// char-select) passe son propre notifyWnd_ ; le handler 0x18 (reconnexion en cours de
// jeu) n'a pas ce handle. On l'expose ici, à charge pour App de l'assigner.
// TODO [ancre 0x815184] : App (front NON possédé par ce module) doit poser
//   ts2::net::g_GameSocketNotifyWnd = hwnd_ à l'init (App::Init/HandleMessage), via une
//   déclaration extern dans un header App. Tant qu'il reste nullptr, le handler 0x18
//   dégrade honnêtement (traite la connexion comme un échec WSAAsyncSelect -> Str107)
//   au lieu d'appeler ConnectGameServer avec une fenêtre nulle (qui annulerait le recv).
HWND g_GameSocketNotifyWnd = nullptr;

void RegisterMiscHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, g_World, Str()

    // 0x0e ServerBillboardImage (ex-SystemMessageBox, mal nommé) — valide/affiche une image serveur.
    // Original : Billboard_ValidateImageViaTempFile(param, image[1000]). Aucune màj
    // d'état d'entité ; l'image est un nom/chemin de fichier (chaîne C).
    OnPacket<ServerBillboardImage>(sys, 0x0e, [](const ServerBillboardImage& p) {
        std::string image(p.image, strnlen(p.image, sizeof p.image));
        (void)image; (void)p.param;
        // TODO(state): Billboard_ValidateImageViaTempFile(p.param, image) — écrit
        //   l'image en fichier temp, la valide et l'affiche dans la boîte système.
    });

    // 0x16 SetGameVar — méga-dispatcher (~158 cas) : écrit 'value' dans le global
    // sélectionné par varId, souvent + ligne système/son, et warp si value<=0.
    OnPacket<SetGameVar>(sys, 0x16, [](const SetGameVar& p) {
        switch (p.varId) {
        case 2:  // g_SelfUnspentAttrPoints (0x16731D0)
            g_Client.Var(0x16731D0) = p.value;
            break;
        case 3:  // g_Currency (0x1673180) + miroir dword_1687254[0]
            g_Client.inv.currency = p.value;
            break;
        case 19: case 23: case 25:  // g_InvWeight
            g_Client.inv.weight = p.value;
            break;
        default:
            // Longue traîne : ~150 cas non reversés, chacun ciblant un global
            // distinct. On stocke fidèlement la valeur dans un slot dédié au varId.
            g_Client.Var(kGameVarBase + static_cast<uint32_t>(p.varId) * 4u) = p.value;
            break;
        }
        // NOTE : la ligne système/son/warp (Map_BeginWarpToFactionTown[Ex]) par varId, ainsi
        // que la couverture COMPLÈTE des ~158 cas du switch, sont implémentées par le module
        // dédié Net/GameVarDispatch.cpp (game::ApplySetGameVar), enregistré EN DERNIER dans
        // InstallGameHandlers via RegisterCoreOverrideHandlers (Net/GameHandlers_Core.cpp) —
        // il REMPLACE ce handler simplifié pour l'opcode 0x16 (un seul slot par opcode, le
        // dernier enregistré gagne). Le code ci-dessus reste la version « domaine » de
        // référence pour les 4 cas les plus fréquents, mais n'est PLUS celui exécuté à
        // runtime pour 0x16 — cf. Net/GameHandlers_Core.cpp lignes 1-18.
    });

    // 0x18 GameServerConnectResult — Pkt_GameServerConnectResult 0x469CF0. Résultat de
    // sélection/connexion (reconnexion/relais serveur EN COURS DE JEU) au serveur de jeu.
    // Décompilé (IDA) : v38=resultCode (memcpy@0x8156C1), v39=serverId (0x8156C5),
    // v36=port (0x8156C9). Capture [&sys] pour atteindre sys.Client() (NetClient& global,
    // persistant via InstallGameHandlers — pas de dangling).
    OnPacket<GameServerConnectResult>(sys, 0x18, [&sys](const GameServerConnectResult& p) {
        switch (p.resultCode) {                                    // switch(v38) — Pkt_GameServerConnectResult 0x469CF0
        case 0: {
            // Net_SelectServerDomain 0x53FE90 = stub VIDE dans l'image déballée (table
            // d'hôtes geniusorc.com non reconstructible) -> réutilise net::kLoginHostCom,
            // comme LoginScene (host callback CharSelect). serverId consommé nominalement.
            const char* host = net::kLoginHostCom;                 // (void)p.serverId
            // Net_ConnectGameServer 0x462A70 : nouvelle socket + bannière 5o + clé XOR@+4 /
            // seq@+5 + auth 141o + WSAAsyncSelect(WM_USER+1). Requiert hWndParent 0x815184.
            const HWND wnd = ts2::net::g_GameSocketNotifyWnd;      // == hWndParent 0x815184
            const int v40 = wnd
                ? net::ConnectGameServer(sys.Client(), host,
                                         static_cast<uint16_t>(p.port), wnd)
                : net::kNetErrAsyncSelect;  // pas de fenêtre -> code 6 -> Str107 (dégradé honnête, cf. §hWndParent)
            switch (v40) {                                         // switch(v40) — sous-résultat Net_ConnectGameServer
            case 0:
                // Original : g_SceneMgr=5 (0x1676180) / g_SceneSubState=0 (0x1676184) /
                // dword_1676188=0 -> bascule scène EnterWorld pour RECHARGER la zone.
                // SceneManager NON édité (possédé par W2-F1) : on arme le flag partagé
                // consommé par SceneManager (game::g_World.sceneReloadPending, GameState.h:544).
                // 0x469CF0 n'écrit PAS pendingWarpZoneId (aucun zoneId dans ce paquet) ->
                // laissé à sa valeur amont (-1 = recharge zone courante ; le nouveau serveur
                // renverra Pkt_EnterWorld 0x0c avec la vraie zone).
                game::g_World.sceneReloadPending = true;
                break;
            case 1: g_Client.prompt.Open(2, Str(102)); break;      // UI_NoticeDlg_Open(_,2,Str102,"")
            case 2: g_Client.prompt.Open(2, Str(103)); break;      // UI_NoticeDlg_Open(_,2,Str103,"")
            case 3:
            case 4:
            case 5: {
                const int sid = (v40 == 3) ? 104 : (v40 == 4) ? 105 : 106;
                g_Client.msg.System(Str(sid));                     // Msg_AppendSystemLine(g_ChatManager, Str104/105/106, g_SysMsgColor)
                net::Net_SendPacket_Op21(sys.Client());            // Net_SendPacket_Op21(&g_AutoPlayMgr) 0x4B5190
                // ÉCART FIDÉLITÉ [ancre 0x469CF0, LABEL_12] : l'original branche sur le retour
                // d'envoi (0=échec -> UI_NoticeDlg_Open(_,2,Str20,"") ; sinon g_MorphInProgress=0).
                // net::Net_SendPacket_Op21 retourne void (SendPackets.h:42, NON possédé par ce
                // front) -> on prend la branche succès (cas socket saine), pas de notice Str20.
                g_MorphInProgress = 0;                             // ts2::net::g_MorphInProgress = g_MorphInProgress 0x1675A88
                break;
            }
            case 6: g_Client.prompt.Open(2, Str(107)); break;      // UI_NoticeDlg_Open(_,2,Str107,"")
            case 7: g_Client.prompt.Open(2, Str(108)); break;      // UI_NoticeDlg_Open(_,2,Str108,"")
            default: break;                                        // default: return (no-op)
            }
            break;
        }
        // resultCode 1..12 : ligne système (StrTable005) + fin du morph (g_MorphInProgress=0).
        // Ids EXACTS décompilés (0x469CF0, cases 1..12).
        case 1:  g_Client.msg.System(Str(100));  g_MorphInProgress = 0; break;
        case 2:  g_Client.msg.System(Str(1221)); g_MorphInProgress = 0; break;
        case 3:  g_Client.msg.System(Str(1347)); g_MorphInProgress = 0; break;
        case 4:  g_Client.msg.System(Str(1928)); g_MorphInProgress = 0; break;
        case 5:  g_Client.msg.System(Str(1554)); g_MorphInProgress = 0; break;
        case 6:  g_Client.msg.System(Str(1951)); g_MorphInProgress = 0; break;
        case 7:  g_Client.msg.System(Str(2237)); g_MorphInProgress = 0; break;
        case 8:  g_Client.msg.System(Str(1213)); g_MorphInProgress = 0; break;
        case 9:  g_Client.msg.System(Str(2308)); g_MorphInProgress = 0; break;
        case 10: g_Client.msg.System(Str(2689)); g_MorphInProgress = 0; break;
        case 11: g_Client.msg.System(Str(2330)); g_MorphInProgress = 0; break;
        case 12: g_Client.msg.System(Str(2821)); g_MorphInProgress = 0; break;
        default: break;                                            // default: return (no-op)
        }
    });

    // 0x27 QuestInteractResult — résultat d'interaction de quête (mal nommé). Le
    // contenu objet vient de g_pCurQuestStepRecord ; invRow==-1 => pas d'écriture inv.
    OnPacket<QuestInteractResult>(sys, 0x27, [](const QuestInteractResult& p) {
        switch (p.resultCode) {
        case 1: g_Client.msg.System(Str(109)); break;  // avance étape + donne item
        case 2: g_Client.msg.System(Str(432)); break;  // récompense finale (3 récompenses)
        case 3: g_Client.msg.System(Str(433)); break;  // remplace item
        case 4: g_Client.msg.System(Str(434)); break;  // Inventory_ReplaceItem
        case 5: g_Client.msg.System(Str(435)); break;  // échec
        case 6: g_Client.msg.System(Str(436)); break;  // incrément compteur
        case 7: g_Client.msg.System(Str(438)); break;
        case 8: g_Client.msg.System(Str(437)); break;  // incrément compteur
        case 9: g_Client.msg.System(Str(439)); break;  // incrément compteur
        default: break;
        }
        // NOTE : l'écriture de cellule inventaire ([invRow][invSlot] si invRow!=-1) et les
        // compteurs de quête (killTrack/objective*) sont appliqués par
        // game::ApplyQuestInteractResultState (Game/QuestSystem.h/.cpp), câblée pour cet
        // opcode 0x27 dans Net/GameHandlers_Core.cpp (enregistré après ce module — complète
        // fidèlement les messages haut-niveau ci-dessus sans les dupliquer).
        (void)p.invRow; (void)p.invSlot; (void)p.gridX; (void)p.gridY;
    });

    // 0x28 ToggleObserver — bascule le mode observateur (élément 3) + retour ville.
    OnPacket<ToggleObserver>(sys, 0x28, [](const ToggleObserver& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;
            if (g_LocalElement == 3) {
                // Déjà observateur -> revenir à l'élément secondaire.
                g_LocalElement = static_cast<uint32_t>(g_Client.Var(0x1673198)); // g_LocalElementSecondary
                --g_Client.Var(0x16747E8);   // dword_16747E8
                g_Client.msg.System(Str(1443));
                // TODO(state): recalcul quête (NpcTbl_FindIdByType — table NPC .IMG absente
                //   du modèle client, cf. Game/QuestSystem.h) + reset objectif (mêmes limites
                //   que le TODO symétrique ci-dessous).
            } else {
                g_LocalElement = 3;          // devenir observateur
                g_Client.msg.System(Str(260));
                // TODO(state): reset objectif de quête — RE/net_handler_notes.md ne précise
                //   pas quels champs de QuestProgressState (Game/QuestSystem.h) sont remis à
                //   zéro (juste « reset objectif de quête ») ; ne pas deviner sans confirmation
                //   IDA (Pkt_ToggleObserver 0x462... — décompilation détaillée non disponible
                //   dans cette passe, serveur IDA inaccessible).
            }
            // Retour ville de faction (résolution + globals d'armement ; l'envoi réseau réel
            // reste un TODO(send) interne à Game/MapWarp.cpp, cf. en-tête d'inclusion).
            BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
        } else {  // resultCode==1 : refus
            g_Client.msg.System(Str(966));
        }
    });

    // 0x39 PvpTallyUpdate — incrémente les compteurs victoire/défaite.
    OnPacket<PvpTallyUpdate>(sys, 0x39, [](const PvpTallyUpdate& p) {
        switch (p.code) {
        case 0: ++g_Client.Var(0x182292C); break;  // défaites (dword_182292C)
        case 1: ++g_Client.Var(0x1822930); break;  // victoires (dword_1822930)
        case 2: g_Client.msg.System(Str(2237)); break;
        default: break;
        }
    });

    // 0x58 CultivationDispatch — méga-dispatcher cultivation (attributs/growth/buffs).
    OnPacket<CultivationDispatch>(sys, 0x58, [](const CultivationDispatch& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.subOpcode) {
        case 1:  // reset attributs -> redistribue vers g_SelfUnspentAttrPoints, str601
            g_Client.msg.System(Str(601));
            // TODO(state): remettre g_SelfBaseAttr292/296/300/304 (0x16731BC/C4/B8/C0)
            //   à 0 et cumuler dans g_SelfUnspentAttrPoints (0x16731D0).
            break;
        case 6:  // croissance/growth : body[0..3] = u32 ; coût selon g_GrowthIndex%100
            g_Client.Var(0x1674774) = static_cast<int32_t>(Rd32(p.body)); // g_GrowthIndex
            // TODO(state): appliquer le coût et recalculer les bornes d'attaque (AR).
            break;
        case 7:  // core attr +/- : coût 100 argent + 1M poids
            g_Client.inv.currency -= 100;
            g_Client.inv.weight   -= 1000000;
            // TODO(state): poser l'attribut de cœur puis recalcul AR.
            break;
        case 12: g_Client.Var(0x16747D4) = p.value; break;  // toggle dword_16747D4
        case 13: g_Client.Var(0x16747D8) = p.value; break;  // toggle dword_16747D8
        case 19:
        case 20:  // applique 11 u32 de buffs d'attributs depuis body[0..43]
            for (int i = 0; i < 11; ++i)
                g_Client.Var(0x16758BC + static_cast<uint32_t>(i) * 4u) =
                    static_cast<int32_t>(Rd32(p.body + 4 * i));
            if (p.subOpcode == 20)
                g_Client.inv.currency -= 1000;
            // TODO(state): mapper g_AttrBuffActive/300/304/292/296 + recalcul AR min/max.
            break;
        default:
            // TODO(state): sous-op de cultivation non reversé (value=p.value).
            break;
        }
    });

    // 0x5b QuickslotSync — mode 1 = charge les raccourcis ; mode 2 = argent/poids.
    OnPacket<QuickslotSync>(sys, 0x5b, [](const QuickslotSync& p) {
        if (p.flag == 0) {
            if (p.mode == 1) {  // copie quickslot[0..49] -> dword_184C0F8[]
                for (int i = 0; i < 50; ++i)
                    g_Client.Var(0x184C0F8 + static_cast<uint32_t>(i) * 4u) =
                        static_cast<int32_t>(p.quickslot[i]);
            } else if (p.mode == 2) {  // synchro argent/poids
                g_GmCmdCooldownLatch = 0;
                g_Client.inv.weight = static_cast<int64_t>(p.money);  // g_InvWeight
                g_Client.msg.System(Str(640));
            }
        }
    });

    // 0x61 ServerNameNotice — subop1 = message par id (StrTable003) ; subop2 = 3 floats.
    OnPacket<ServerNameNotice>(sys, 0x61, [](const ServerNameNotice& p) {
        if (p.subop == 1) {
            uint32_t id = Rd32(p.data);           // data[0..3] = id string
            g_Client.msg.System(Str(static_cast<int>(id)));
        } else if (p.subop == 2) {
            for (int i = 0; i < 3; ++i)           // data[0..11] = 3 floats -> flt_1687330
                g_Client.VarF(0x1687330 + static_cast<uint32_t>(i) * 4u) = RdF32(p.data + 4 * i);
        }
    });

    // 0x62 Sub_4A55E0 (ea=0x4A55E0) — opcode/payload vide (1 o) : déclenche un warp fixe
    // vers la carte 37 (byte_1685748 = joueur local). Trou de couverture comblé — la
    // fonction game::BeginWarpToMap37() existait déjà (Game/MapWarp.h/.cpp), câblée ici.
    OnTrigger(sys, 0x62, []() {
        BeginWarpToMap37();
        // TODO(net) : Net_SendPacket_Op20 (confirmation warp) émis par ArmFullWarp — pas
        //   de NetClient dans ce module, cf. TODO(net) équivalents de Game/MapWarp.cpp.
    });

    // 0x63 ScriptTrigger — challenge/verify GameGuard (ANTICHEAT). Ignoré par contrat :
    // aucune donnée de gameplay, aucun effet d'état à reproduire côté client réécrit.
    OnPacket<ScriptTrigger>(sys, 0x63, [](const ScriptTrigger&) {
        // Anticheat volontairement ignoré (Ac_GuardClient_MakeVerifyData).
    });

    // 0x66 PetSlotDispatch — dispatcher de slot talisman/pet (sous-op 1..8).
    OnPacket<PetSlotDispatch>(sys, 0x66, [](const PetSlotDispatch& p) {
        int32_t& slot = g_Client.Var(kTalismanSlot);  // g_TalismanSlot
        switch (p.subop) {
        case 1: slot = static_cast<int32_t>(p.value); break;
        case 2: slot = -1; break;
        case 3: slot += 10; break;
        case 4: slot -= 10; break;
        case 5:  // vide le slot courant
            if (slot >= 0) {
                g_Client.Var(0x1674738 + static_cast<uint32_t>(slot) * 4u) = 0; // dword_1674738[]
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = 0; // dword_167568C[]
                g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = 0; // dword_16756B4[]
            }
            slot = -1;
            g_Client.inv.weight -= 100000000;  // g_InvWeight -= 1e8
            g_Client.msg.System(Str(1511));
            break;
        case 6:  // active un attribut de talisman (recalcul AR)
            if (p.value != 0 && slot >= 0) {
                // TODO(state): « pose l'attribut » exact — Stat_UnpackCombined(v,hi,lo) sur
                //   dword_1675664[slot] (slot<10) ou dword_167568C[slot] (slot>=10) puis
                //   écriture d'un champ d'attribut cible non identifié précisément dans
                //   RE/net_handler_notes.md (juste « pose l'attribut », pas le champ exact ni
                //   la formule hi/lo -> attribut) : NE PAS FORCER une correspondance devinée.
                //   Game/BitPacking.h fournit Stat_UnpackCombined/PackCombined (prêtes à
                //   l'emploi dès que le champ cible sera confirmé).
                RecalcTalismanAttackRating();
                g_Client.msg.System(Str(2165));
            } else {
                g_Client.msg.System(Str(2166));
            }
            break;
        case 7:  // pose une valeur dans le slot (recalcul AR)
            if (slot >= 0) {
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) =
                    static_cast<int32_t>(p.value);
            }
            RecalcTalismanAttackRating();
            g_Client.msg.System(Str(2213));
            break;
        case 8:  // idem 7, message différent
            if (slot >= 0) {
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) =
                    static_cast<int32_t>(p.value);
            }
            RecalcTalismanAttackRating();
            g_Client.msg.System(Str(2181));
            break;
        default: break;
        }
    });

    // 0x6f SkillCooldownSet — fixe le cooldown d'une compétence (table dword_18217D0).
    OnPacket<SkillCooldownSet>(sys, 0x6f, [](const SkillCooldownSet& p) {
        if (p.skillId >= 1 && p.skillId <= 351)
            g_Client.Var(0x18217D0 + p.skillId * 4u) = static_cast<int32_t>(p.value);
    });

    // 0x71 Sub_4A7150 (ea=0x4A7150, 5 o) — trou de couverture comblé. Stub confirmé sans
    // effet observable dans le binaire (Crt_Memcpy dans un local jeté) : payload lu puis
    // ignoré. Enregistré explicitement (plutôt que laissé sans handler) par fidélité de
    // couverture du dispatch — cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md.
    OnTrigger(sys, 0x71, []() {});

    // 0x72 RevivePrompt — flag==0 : PV self à 0 + arme l'invite de réanimation.
    OnPacket<RevivePrompt>(sys, 0x72, [](const RevivePrompt& p) {
        if (p.flag == 0) {
            g_Client.Var(0x1687378) = 0;       // dword_1687378[0] (PV self)
            g_World.self.hp = 0;               // miroir modélisé
            g_Client.Var(kPendingStopReq) = 1; // g_PendingStopRequest=1
        }
    });

    // 0x73 CountdownTimerStart — démarre un compte à rebours (base timeGetTime).
    OnPacket<CountdownTimerStart>(sys, 0x73, [](const CountdownTimerStart& p) {
        g_Client.Var(0x183914C) = static_cast<int32_t>(p.mode);  // mode
        g_Client.Var(0x1839150) = static_cast<int32_t>(p.f1);
        g_Client.Var(0x1839154) = static_cast<int32_t>(p.f2);
        g_Client.Var(0x1839134) = 4;   // timer actif
        // TODO(state): dword_1839158 = timeGetTime() (horodatage de départ, ms depuis le
        //   boot Windows). NE PAS FORCER : aucune abstraction d'horloge murale (Win32
        //   timeGetTime/GetTickCount) n'existe dans les couches Game/Net — le seul temps
        //   disponible ici est g_World.gameTimeSec (flt_815180, temps de JEU à 30 FPS fixe,
        //   sémantique DIFFÉRENTE d'un horodatage temps réel) ; substituer l'un à l'autre
        //   romprait la fidélité plutôt que la préserver. Le compte à rebours réel (App/
        //   boucle 30 FPS) doit fournir cet horodatage au moment de l'intégration UI.
        // TODO(audio): Snd3D_PlayScaledVolume si mode==-1 ou mode==1 (module audio non câblé
        //   dans ce dispatcher réseau, cf. stub identique Net/GameVarDispatch.cpp).
    });

    // 0x76 MinigameStateLoad — charge 4 dwords d'état de mini-jeu.
    OnPacket<MinigameStateLoad>(sys, 0x76, [](const MinigameStateLoad& p) {
        g_Client.Var(0x1675D20) = static_cast<int32_t>(p.a);
        g_Client.Var(0x1675D24) = static_cast<int32_t>(p.b);
        g_Client.Var(0x1675D28) = static_cast<int32_t>(p.c);
        g_Client.Var(0x1675D2C) = static_cast<int32_t>(p.d);
    });

    // 0x7d SkillAuraSync — mini-dispatcher d'états toggle compétence/aura.
    OnPacket<SkillAuraSync>(sys, 0x7d, [](const SkillAuraSync& p) {
        switch (p.subOpcode) {
        case 0: {  // décode value en base-10 vers dword_1675DB8/BC/C0/C4
            int32_t v = p.value;
            g_Client.Var(0x1675DB8) = v / 1000000;
            g_Client.Var(0x1675DBC) = (v % 1000000) / 10000;
            g_Client.Var(0x1675DC0) = (v % 10000) / 100;
            g_Client.Var(0x1675DC4) = v % 100;
            // TODO(state): charge/décharge le modèle de zone selon dword_1675DB8[g_LocalElement].
            break;
        }
        case 2:  // toggle indexé (value 0..4)
            if (p.value >= 0 && p.value <= 4) {
                uint32_t idx = static_cast<uint32_t>(p.value);
                g_Client.Var(0x1675D98 + idx * 4u) = 1;
                g_Client.Var(0x1675DB8 + idx * 4u) = 1;
                g_Client.VarF(0x1675DA8 + idx * 4u) = 0.0f;
            }
            break;
        case 5:  // message flottant
            g_Client.msg.Floating(1, 0, Str(245) + " " + std::to_string(p.value));
            break;
        case 6:
            g_Client.Var(0x1675DCC) = 1;
            g_Client.msg.System(Str(246));
            break;
        case 7:
        case 9:
            g_Client.msg.System(Str(237));
            BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
            break;
        case 8:
            if (static_cast<uint32_t>(p.value) == g_LocalElement) {
                g_Client.msg.System(Str(1919));
                BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
            }
            break;
        default: break;
        }
    });

    // 0x82 Sub_4AAB60 (ea=0x4AAB60, 61 o) — trou de couverture comblé. Copie brute de 60
    // octets (15 floats) dans le bloc global flt_1676130 ; aucun parsing/branchement dans
    // le binaire d'origine (Docs/TS2_PROTOCOL_SPEC.md #0x82).
    OnPacket<RawFloatBlob15>(sys, 0x82, [](const RawFloatBlob15& p) {
        for (int i = 0; i < 15; ++i)
            g_Client.VarF(0x1676130 + static_cast<uint32_t>(i) * 4u) = p.values[i];
    });

    // 0x84 SummonSpawn — téléportation d'invocation vers une position fixe.
    OnPacket<SummonSpawn>(sys, 0x84, [](const SummonSpawn& p) {
        if (p.slot < 4u && p.status == 0) {
            // Position d'invocation codée en dur dans le handler d'origine.
            const float pos[3] = { -14.0f, 0.0f, -4242.0f };
            (void)pos; (void)p.slot;
            // TODO(send): Warp_SendTeleport (EA 0x5F5CE0, RE/naming_results.json : « send
            //   teleport-to-map request (sub_4B5000 op6) ») — enveloppe sub_4B5000 avec un sous-opcode
            //   interne 6. NE PAS FORCER : ni Warp_SendTeleport ni sub_4B5000 ne figurent parmi les 234
            //   builders Net_SendOpNN/Net_SendPacket_OpNN déjà portés dans Net/SendPackets.h — ce
            //   dispatcher-là n'a pas encore été reversé/porté côté C++, donc aucun appel existant à
            //   câbler ici sans d'abord ajouter ce builder (hors périmètre de ce passage de câblage).
        }
    });

    // 0x85 SystemNotice — messages de notice système (3 variantes).
    OnPacket<SystemNotice>(sys, 0x85, [](const SystemNotice& p) {
        switch (p.subOpcode) {
        case 0:  // "[value]str1479"
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1479));
            break;
        case 1:  // "[value]str843"
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(843));
            break;
        case 2:  // value 0..7 -> str1996..2003 en message flottant + ligne système
            if (p.value <= 7) {
                std::string t = Str(1996 + static_cast<int>(p.value));
                g_Client.msg.Floating(1, 0, t);
                g_Client.msg.System(t);
            }
            break;
        default: break;
        }
    });

    // 0x8f Sub_4AB020 (ea=0x4AB020) — opcode/payload vide (1 o) : déclenche un warp vers
    // la ville de la faction du joueur local. Trou de couverture comblé — réutilise
    // game::BeginWarpToFactionTown (mode « forcé »/non-Ex), déjà câblée ailleurs
    // (GameHandlers_BossWorld.cpp/GameHandlers_PartyGuild.cpp) pour la même sémantique.
    OnTrigger(sys, 0x8f, []() {
        BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
    });

    // 0x99 AchievementNotice — notification d'exploit (nom + état local dword_184C218).
    // Délègue à game::PostAchievementNotice (Game/SocialSystem.h/.cpp), qui reproduit
    // fidèlement idx=TribeSkill_SkillIdToIndex(g_SelfMorphNpcId) puis
    // "<str(dword_184C218[idx]%100+2249)> <name> <str2305>" — g_Achievements est alimenté
    // par AchievementDataLoad (opcode 0x98, GameHandlers_BossWorld.cpp). Si idx est invalide
    // (skill/morph non reconnu), l'original n'affiche RIEN (EA 0x4aca3a) — fidèlement
    // reproduit ici (silence, pas de fallback approximatif).
    OnPacket<AchievementNotice>(sys, 0x99, [](const AchievementNotice& p) {
        std::string name(p.name, strnlen(p.name, sizeof p.name));
        const int32_t morph = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
        PostAchievementNotice(g_Achievements, morph, name);
    });

    // 0xae BuffEffectDispatch — dispatcher de buff/stat + maj cellule inventaire.
    OnPacket<BuffEffectDispatch>(sys, 0xae, [](const BuffEffectDispatch& p) {
        switch (p.subOpcode) {
        case -1:
        case 5:  // pose deux valeurs de stat
            g_Client.Var(0x1675894) = p.param5;
            g_Client.Var(0x1675898) = p.param6;
            // TODO(state): Player_CheckStateDigit (rafraîchit l'état dérivé).
            break;
        case 1: {  // pose un objet dans la grille d'inventaire
            uint32_t count = (p.param1 == 12101) ? 99u : 0u;
            g_Client.inv.Set(p.param2, p.param3, static_cast<uint32_t>(p.param1),
                             p.param4 % 8u, p.param4 / 8u, count, 0, 0);
            // TODO(state): mise à jour de l'état dérivé après pose.
            break;
        }
        case 2: g_Client.msg.System(Str(598)); break;
        case 4: g_Client.msg.System(Str(1257)); break;
        case 3: {  // sous-dispatcher d'effets temporels
            int v26 = p.param1 / 1000;
            int v31 = -(p.param1 % 1000);
            (void)v26; (void)v31;
            // TODO(state): 2e switch v26 (-6..-17) ajustant les compteurs de buff
            //   (dword_167587C, 16746E4/E8, 1674700/708/794, 1674AA0) + messages
            //   formatés avec le nom d'objet (MobDb_GetEntry).
            break;
        }
        default: break;
        }
    });

    // 0xb1 Sub_4B33C0 (ea=0x4B33C0, 5 o) — trou de couverture comblé. Setter trivial :
    // copie value dans dword_16874A0 et efface le verrou occupé (Docs/TS2_PROTOCOL_SPEC.md #0xb1).
    OnPacket<RawU32Setter>(sys, 0xb1, [](const RawU32Setter& p) {
        g_GmCmdCooldownLatch = 0;                              // dword_1675B08 -> 0
        g_Client.Var(0x16874A0) = static_cast<int32_t>(p.value);
    });
}

} // namespace ts2::net

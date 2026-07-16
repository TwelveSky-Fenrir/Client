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
#include "Game/StatEngine.h"   // game::StatEngine::CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0) — M3
#include "Game/BitPacking.h"   // game::Stat_UnpackCombined/PackCombined (0x54CE40/0x54CEB0) — M4
#include "Game/MapWarp.h"      // game::BeginWarpToMap37/BeginWarpToFactionTown (0x62/0x8f) + Warp_SendTeleport (0x84)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (résolution coords warp)
#include "Game/SocialSystem.h" // game::g_Achievements/PostAchievementNotice (AchievementNotice 0x99)
#include "Game/GameDatabase.h" // game::GetItemInfo — MobDb_GetEntry(mITEM, id) 0x4C3C00 (0xae cas 3)
#include "Game/StringTables.h" // game::g_Strings.zoneNames = StrTable003 (0x4C1AD0) — 0x61 sous-op 1

#include <cstdarg>
#include <cstdint>
#include <cstdio>
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
// dispatcher : 3 occurrences sur 5). M3 : recalcul LIVE fidèle — le binaire (0x4A5790,
// cases 6/7/8) appelle Char_CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0) sur
// g_EquipSnapshotScratch à CHAQUE appel ; on reproduit via StatEngine::CalcAttackRatingMin/
// Max(g_World.self, g_World.db) (mêmes EA 0x4CD970/0x4CE3F0), comme GameVarDispatch.cpp.
// Utilisé par PetSlotDispatch (0x66) cases 6/7/8.
void RecalcTalismanAttackRating() {
    // 0x4A59BF.. : dword_168736C = Char_CalcAttackRatingMin(...) ; clamp dword_1687370.
    const int32_t mn = game::StatEngine::CalcAttackRatingMin(game::g_World.self, game::g_World.db); // 0x4CD970
    game::g_Client.Var(0x168736C) = mn;                                          // dword_168736C base min
    if (game::g_Client.VarGet(0x1687370) > mn) game::g_Client.Var(0x1687370) = mn; // dword_1687370 cur min (clamp)
    const int32_t mx = game::StatEngine::CalcAttackRatingMax(game::g_World.self, game::g_World.db); // 0x4CE3F0
    game::g_Client.Var(0x1687374) = mx;                                          // dword_1687374 base max
    if (game::g_Client.VarGet(0x1687378) > mx) game::g_Client.Var(0x1687378) = mx; // dword_1687378 cur max (clamp)
}

// Recalcul AR SANS clamp — motif DISTINCT de RecalcTalismanAttackRating() ci-dessus :
// le binaire écrit ici les 4 globals en AFFECTATION SÈCHE (aucun test `if (cur > n)`).
// Ancre : Net_OnCultivationDispatch 0x493180, cases 19/20 — EA 0x493FD7 (dword_1687370=Min),
// 0x493FE6 (dword_168736C=Min), 0x493FF5 (dword_1687378=Max), 0x494004 (dword_1687374=Max)
// [idem case 20 : EA 0x494114/0x494123/0x494132/0x494141]. Le binaire appelle Min et Max
// DEUX fois chacun (fonctions pures, sans effet de bord) -> factorisé en un appel.
void RecalcAttackRatingSetAll() {
    const int32_t mn = game::StatEngine::CalcAttackRatingMin(game::g_World.self, game::g_World.db); // 0x4CD970
    game::g_Client.Var(0x1687370) = mn;   // 0x493FD7 — PAS de clamp (≠ cases 9/10)
    game::g_Client.Var(0x168736C) = mn;   // 0x493FE6
    const int32_t mx = game::StatEngine::CalcAttackRatingMax(game::g_World.self, game::g_World.db); // 0x4CE3F0
    game::g_Client.Var(0x1687378) = mx;   // 0x493FF5
    game::g_Client.Var(0x1687374) = mx;   // 0x494004
}

// Recalcul AR de la case 6 : SEULEMENT dword_168736C et dword_1687374, sans clamp.
// Ancre : Net_OnCultivationDispatch 0x493180, case 6 — EA 0x4935F8 / 0x493607. Le binaire
// ne touche NI dword_1687370 NI dword_1687378 ici (contrairement aux cases 9/10/19/20).
void RecalcAttackRatingBaseOnly() {
    game::g_Client.Var(0x168736C) =
        game::StatEngine::CalcAttackRatingMin(game::g_World.self, game::g_World.db); // 0x4935F8
    game::g_Client.Var(0x1687374) =
        game::StatEngine::CalcAttackRatingMax(game::g_World.self, game::g_World.db); // 0x493607
}

// StrTable003_Get(dword_84A6A8, id) 0x4C1AD0 — table des NOMS DE ZONE (003.DAT / mZONENAME),
// DISTINCTE de StrTable005_Get(g_LangId, id) 0x4C1D10 (005.DAT / mMESSAGE) qu'expose game::Str().
// Index 1-based, chaîne vide hors bornes (comme &String dans l'original).
// Ancre : Net_OnServerNameNotice 0x4A5540, EA 0x4A55AC.
const char* Str3(int id) { return game::g_Strings.zoneNames.Get(id); }

// Crt_Vsnprintf(buf, StrTable005_Get(g_LangId, strId), ...) 0x75CD5F — le FORMAT est
// l'entrée de table localisée elle-même (et non un littéral). Buffer 1000 o comme
// l'original (v28/v29, [ebp-7F8h]/[ebp-410h] de Net_OnBuffEffectDispatch 0x4A88D0).
std::string FmtFromStrTable(int strId, ...) {
    char buf[1000];
    va_list ap; va_start(ap, strId);
    std::vsnprintf(buf, sizeof buf, game::Str(strId).c_str(), ap);
    va_end(ap);
    return std::string(buf);
}

// vsnprintf sur un format LITTÉRAL du binaire ("%s %d", "%s%s", "%s%s %d%s", "[%d]%s").
std::string Fmt(const char* f, ...) {
    char buf[1000];
    va_list ap; va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

// MobDb_GetEntry(mITEM, id) 0x4C3C00 ; le champ +4 de l'entrée == ItemInfo::name
// (GameDatabase.h:60). Renvoie nullptr si l'entrée n'existe pas — l'appelant DOIT
// tester, car le binaire s'en sert pour brancher (cf. cas 3 de 0xae).
const char* ItemNameOrNull(uint32_t itemId) {
    const game::ItemInfo* it = game::GetItemInfo(itemId);
    return it ? it->name : nullptr;
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
                // ÉCART FIDÉLITÉ [ancre 0x469CF0, EA 0x469E20 -> LABEL_12 0x469EE5] : l'original
                // fait `call Net_SendPacket_Op21 / test eax,eax / jnz` — le builder 0x4B5190
                // retourne bien un int (0 après Net_CloseSocket, 1 en succès) et, sur échec,
                // saute à LABEL_12 = UI_NoticeDlg_Open(byte_18225C8, 2, Str20, "") AU LIEU de
                // poser g_MorphInProgress = 0. Le correctif exige de changer la signature de
                // net::Net_SendPacket_Op21 (void -> bool) dans Net/SendPackets.h:42, fichier NON
                // POSSÉDÉ par ce front -> écart conservé et remonté à l'orchestrateur. Effet
                // observable limité au cas d'une socket déjà rompue.
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
    // Ancre : Net_OnCultivationDispatch 0x493180. Décodage : v72=value @0x8156C1 (memcpy
    // 0x49319E), v67=subOpcode @0x8156C5 (0x4931B4), v73..[100] = body @0x8156C9 (0x4931C7).
    //
    // NOTE DE MODÉLISATION (currency) : le binaire tient DEUX globals — g_Currency 0x1673180
    //   et son miroir dword_1687254 — décrémentés ENSEMBLE dans les cases 4/6/7/16/17/20.
    //   Le C++ les fusionne en un seul champ (g_Client.inv.currency, ClientRuntime.h:82,
    //   « g_Currency 0x1673180 (+ miroir dword_1687254[0]) ») ; la clé Var(0x1687254) n'a
    //   aucun lecteur. La case 15 fait exception : elle décrémente g_Currency SEUL (EA
    //   0x493C6B/9A/B3, sans toucher 0x1687254) — asymétrie NON représentable par le repli à
    //   un champ ; on applique la décrémentation à inv.currency (comportement observable).
    //
    // ⚠ ÉCART CROISÉ (hors périmètre de ce front, à arbitrer par l'orchestrateur) : le C++ a
    //   DEUX représentations concurrentes de g_Currency 0x1673180 — `g_Client.inv.currency`
    //   (qu'écrivent ce module et Net/GameHandlers_InvDispatch.cpp / InvCells.cpp) et
    //   `g_World.self.currency` (qu'écrivent Net/GameVarDispatch.cpp:163/556 et
    //   Net/CharStatDeltaDispatch.cpp:397, et que LIT le HUD — UI/GameHud.cpp:1166). Les coûts
    //   posés ici sur inv.currency ne sont donc pas affichés. Défaut PRÉ-EXISTANT et
    //   TRANSVERSAL (≈15 sites, 4 fichiers non possédés) : on conserve la convention en place
    //   dans ce fichier (cf. 0x16 case 3 ci-dessus) plutôt que d'introduire ici une écriture
    //   double qui doublerait les comptes face à GameVarDispatch pour le même opcode.
    OnPacket<CultivationDispatch>(sys, 0x58, [](const CultivationDispatch& p) {
        // g_GmCmdCooldownLatch = 0 : le binaire le pose PAR-CASE, en 1re instruction de
        // chacune des cases 1..20 (EA 0x493212 case 1 … 0x49402F case 20), et JAMAIS dans
        // le `default` (def_49320B 0x4941C7 = épilogue `return` sec). Le poser en tête du
        // lambda libérerait le verrou sur un sous-opcode inconnu — écart réel.
        if (p.subOpcode >= 1 && p.subOpcode <= 20) g_GmCmdCooldownLatch = 0;

        switch (p.subOpcode) {
        case 1:  // reset d'attributs — TOUT est gaté par v72==0 (EA 0x49322C)
            if (p.value != 0) break;
            // 0x493255 : unspent += (300 + 292 + 304 + 296) - 4 ; puis les 4 attributs à 1
            // (EA 0x49325A/64/6E/78) — à 1, PAS à 0 (d'où le `-4` du cumul).
            // Champs modélisés (GameState.h:311-315) et NON g_Client.Var(0x16731B8..) : ce sont
            // eux que lisent le moteur de stats (StatFormulas.cpp:440/809/1021/1134) et l'UI
            // (CharacterStatsWindow.cpp:72-75/145) — les clés Var() n'ont aucun lecteur.
            g_World.self.unspentAttr += g_World.self.attrDefensive   // 0x16731B8
                                     +  g_World.self.attrExtForce    // 0x16731BC
                                     +  g_World.self.attrOffensive   // 0x16731C0
                                     +  g_World.self.attrIntForce    // 0x16731C4
                                     -  4;
            g_World.self.attrDefensive  = 1;   // 0x49325A
            g_World.self.attrExtForce   = 1;   // 0x493264
            g_World.self.attrOffensive  = 1;   // 0x49326E
            g_World.self.attrIntForce   = 1;   // 0x493278
            g_Client.Var(0x1687370) = 1;       // 0x493282
            g_Client.Var(0x1687378) = 0;       // 0x49328C
            g_Client.msg.System(Str(601));     // 0x4932A7
            // TODO(ui) [ancre 0x4932BC] : cDrawWin_Init(dword_1839290) 0x628E40 — popup de
            //   tirage/gamble (this+2 actif, efface 9 flags). Aucune abstraction de ce popup
            //   n'existe côté C++ ; ne pas simuler.
            break;

        case 2:  // messages purs (EA 0x4932D0)
            switch (p.value) {
            case 0: g_Client.msg.System(Str(775)); break;  // 0x49330E
            case 1: g_Client.msg.System(Str(776)); break;  // 0x493334
            case 2: g_Client.msg.System(Str(778)); break;  // 0x493359
            case 3: g_Client.msg.System(Str(779)); break;  // 0x49337F
            default: break;                                // def_4932F6 : return
            }
            break;

        case 3:  // message pur gaté v72==0 (EA 0x49339E / gate 0x4933B8)
            if (p.value == 0) g_Client.msg.System(Str(777));  // 0x4933CC
            break;

        case 4:  // coût 500 (EA 0x4933EB)
            if (p.value == 0) {
                g_Client.inv.currency -= 500;              // 0x49341E g_Currency / 0x49342E miroir
                g_Client.msg.System(Str(783));             // 0x493444
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));             // LABEL_111 0x493ED3
            }
            break;

        case 5:  // latch seul, aucun autre effet (EA 0x493489-0x493493)
            break;

        case 6: {  // croissance/growth : coût tabulé par g_GrowthIndex%100
            if (p.value != 0) break;                       // gate 0x4934B2
            // switch (g_GrowthIndex % 100) -> coût v68 (EA 0x4934DF..0x49356D) ; toute autre
            // valeur = `default: return` (def_4934DF 0x493579) SANS le moindre effet.
            // g_World.self.growthIndex = g_GrowthIndex 0x1674774 (GameState.h:317) : c'est le
            // champ que lit le moteur de stats (StatFormulas.cpp:310/437/596/724/807/1020) ;
            // l'ancienne écriture g_Client.Var(0x1674774) n'avait AUCUN lecteur (clé morte).
            int32_t cost;
            switch (g_World.self.growthIndex % 100) {      // 0x4934C4 (idiv 100), 0x4934DF
            case 0:  cost =   800; break;  // 0x4934E6
            case 1:  cost =  1700; break;  // 0x4934F5
            case 2:  cost =  2500; break;  // 0x493501
            case 3:  cost =  3400; break;  // 0x49350D
            case 4:  cost =  4200; break;  // 0x493519
            case 5:  cost =  5100; break;  // 0x493525
            case 6:  cost =  5900; break;  // 0x493531
            case 7:  cost =  6800; break;  // 0x49353D
            case 8:  cost =  7600; break;  // 0x493549
            case 9:  cost =  8500; break;  // 0x493555
            case 10: cost =  9300; break;  // 0x493561
            case 11: cost = 10000; break;  // 0x49356D
            default: return;               // 0x493579 : return, aucun effet
            }
            const int32_t v71 = static_cast<int32_t>(Rd32(p.body));  // memcpy 0x49357E : v71 = body+0
            g_Client.inv.currency -= cost;                 // 0x49359C g_Currency / 0x4935AD miroir
            // Mise à jour EXACTE (0x4935B2-0x4935E4) : le binaire n'assigne JAMAIS
            // g_GrowthIndex = v71 — v71 est un PALIER, pas un index.
            if (g_World.self.growthIndex != 0 || v71 <= 1)
                ++g_World.self.growthIndex;                // 0x4935DE
            else
                g_World.self.growthIndex = 100 * (v71 - 1) + 1;  // 0x4935CD
            g_Client.Var(0x1687314) = g_World.self.growthIndex;   // 0x4935E9
            RecalcAttackRatingBaseOnly();                  // 0x4935F8 / 0x493607 (2 globals, sans clamp)
            g_Client.msg.System(Str(939));                 // 0x49361D (push 3ABh)
            break;
        }

        case 7:  // attribut de cœur +/- : v72 >= 3 ne fait RIEN (0x493656/5F/6C)
            if (p.value > 2) break;
            // Coûts communs aux 3 branches v72==0/1/2 (EA 0x49367F/0x4936FF/0x493763).
            g_Client.inv.currency -= 100;                   // g_Currency + miroir dword_1687254
            g_Client.inv.weight   -= 1000000;               // g_InvWeight 0x16732AC
            if (p.value == 0) {
                ++g_Client.Var(0x167477C);                  // 0x4936AD g_CoreAttr (lu par StatFormulas.cpp:1021/1134)
                ++g_Client.Var(0x1687318);                  // 0x4936BB
                g_Client.msg.System(Str(1143));             // 0x4936D2
                // TODO(audio) [ancre 0x4936ED] : Snd3D_PlayScaledVolume(flt_14980FC, 0, 100, 1).
            } else if (p.value == 1) {
                g_Client.msg.System(Str(1144));             // 0x493735
                // TODO(audio) [ancre 0x493750] : Snd3D_PlayScaledVolume(flt_14981BC, 0, 100, 1).
            } else {  // p.value == 2
                --g_Client.Var(0x167477C);                  // 0x493790
                --g_Client.Var(0x1687318);                  // 0x49379F
                g_Client.msg.System(Str(1144));             // 0x4937B5
                g_Client.msg.System(Str(1145));             // 0x4937D6
                // TODO(audio) [ancre 0x4937F1] : Snd3D_PlayScaledVolume(flt_149827C, 0, 100, 1).
            }
            break;

        case 8:  // (EA 0x493800)
            if (p.value == 0)      g_Client.Var(0x1674780) = 0;   // 0x493827 — SANS message
            else if (p.value == 2) g_Client.msg.System(Str(117)); // 0x493843
            break;

        case 9:  // toggle dword_1674798 ON + clamp AR (EA 0x493862)
            if (p.value != 0) break;                       // gate 0x49387C
            g_Client.Var(0x1674798) = 1;                   // 0x493880
            RecalcTalismanAttackRating();                  // 0x493894-0x4938E5 (clamp identique)
            break;

        case 10:  // toggle dword_1674798 OFF + même clamp (EA 0x4938F9) — symétrique de 9
            if (p.value != 0) break;                       // gate 0x493913
            g_Client.Var(0x1674798) = 0;                   // 0x493917
            RecalcTalismanAttackRating();                  // 0x49392B-0x49397C
            break;

        case 11:  // latch seul, aucun autre effet (EA 0x493990-0x4939AA)
            break;

        case 12:  // (EA 0x4939B4) — sous-switch sur v72
            switch (p.value) {
            case 0:
                // Le binaire DÉCRÉMENTE dword_16747D4 (il ne l'assigne pas) puis pose
                // dword_16747D8 = body[0..3] (et non p.value).
                --g_Client.Var(0x16747D4);                          // 0x4939FC
                g_Client.Var(0x16747D8) = static_cast<int32_t>(Rd32(p.body)); // memcpy 0x4939EB / 0x493A05
                g_Client.msg.System(Str(1341));                     // 0x493A1B
                break;
            case 1: g_Client.msg.System(Str(1342)); break;  // LABEL_68 0x493A41
            case 2: g_Client.msg.System(Str(1343)); break;  // 0x493A66
            case 3: g_Client.msg.System(Str(1344)); break;  // 0x493A8C
            case 4: g_Client.msg.System(Str(1336)); break;  // 0x493AB2
            default: break;                                 // return, aucun effet
            }
            break;

        case 13:  // (EA 0x493AD1) — le binaire MET À ZÉRO (il n'assigne pas p.value)
            if (p.value == 0) {
                g_Client.Var(0x16747D8) = 0;                // 0x493AF8
                g_Client.msg.System(Str(1339));             // 0x493B13
            } else if (p.value == 1) {
                g_Client.msg.System(Str(1340));             // 0x493B39
            }
            break;

        case 14:  // messages purs (EA 0x493B58)
            if (p.value == 0)      g_Client.msg.System(Str(1493));  // 0x493B99
            else if (p.value == 1) g_Client.msg.System(Str(1494));  // 0x493BBF
            else if (p.value == 2) g_Client.msg.System(Str(1495));  // 0x493BE4
            break;

        case 15:  // (EA 0x493C03) — message D'ABORD, puis coût selon le niveau
            switch (p.value) {
            case 0:
                g_Client.msg.System(Str(1768));             // 0x493C40 — AVANT le coût (0x493C4B)
                // Coût conditionnel (0x493C60-0x493CB3) : g_SelfLevel 0x16731A8 et
                // g_SelfLevelBonus 0x16731AC == g_World.self.level / .levelBonus (GameState.h:308-309).
                // Cf. NOTE DE MODÉLISATION : ici le binaire ne touche PAS le miroir 0x1687254.
                if (g_World.self.level >= 100 && g_World.self.level <= 112) {
                    g_Client.inv.currency -= 20;            // 0x493C6B
                } else if (g_World.self.level >= 113 && g_World.self.level <= 145 &&
                           g_World.self.levelBonus == 0) {
                    g_Client.inv.currency -= 50;            // 0x493C9A
                } else if (g_World.self.levelBonus > 0) {
                    g_Client.inv.currency -= 100;           // 0x493CB3
                }
                break;
            case 1: g_Client.msg.System(Str(1342)); break;  // LABEL_68 0x493A41 (partagé avec case 12/1)
            case 2: g_Client.msg.System(Str(1803)); break;  // 0x493CF4
            case 3: g_Client.msg.System(Str(1868)); break;  // 0x493D19
            default: break;                                 // return, aucun effet
            }
            break;

        case 16:  // coût 1 (EA 0x493D33)
            if (p.value == 0) {
                --g_Client.inv.currency;                    // 0x493D63 g_Currency / 0x493D71 miroir
                g_Client.msg.System(Str(783));              // 0x493D87
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));              // LABEL_111 0x493ED3
            }
            break;

        case 17:  // coût 10 (EA 0x493DCC)
            if (p.value == 0) {
                g_Client.inv.currency -= 10;                // 0x493DFC g_Currency / 0x493E0B miroir
                g_Client.msg.System(Str(783));              // 0x493E21
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));              // LABEL_111 0x493ED3
            }
            break;

        case 18:  // coût en POIDS (EA 0x493E66)
            if (p.value == 0) {
                g_Client.inv.weight -= 500000000;           // 0x493E97 g_InvWeight
                g_Client.msg.System(Str(783));              // 0x493EAD
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));              // LABEL_111 0x493ED3
            }
            break;

        case 19:
        case 20: {
            // 11 memcpy de 4 o depuis les lvars CONTIGUËS v73..v83 ([ebp-70h]..[ebp-48h]),
            // toutes remplies par l'unique `Crt_Memcpy(v73, MEMORY[0x8156C9], 0x64)` (0x4931C7)
            // => v73 = body+0 … v83 = body+40. Base RÉELLE = g_AttrBuffActive 0x16758A8.
            // ATTENTION : le binaire SAUTE 0x16758C4 (il enchaîne 0x16758C0 -> 0x16758C8) —
            // trou volontaire, consommé ailleurs ; une boucle contiguë le corromprait.
            // EA case 19 : 0x493F07..0x493FC5 ; case 20 : 0x494044..0x494102 (identiques).
            g_Client.Var(0x16758A8) = static_cast<int32_t>(Rd32(p.body +  0)); // g_AttrBuffActive 0x493F07
            g_Client.Var(0x16758AC) = static_cast<int32_t>(Rd32(p.body +  4)); // g_AttrBuff300    0x493F1A
            g_Client.Var(0x16758B0) = static_cast<int32_t>(Rd32(p.body +  8)); // g_AttrBuff304    0x493F2D
            g_Client.Var(0x16758B4) = static_cast<int32_t>(Rd32(p.body + 12)); // g_AttrBuff292    0x493F40
            g_Client.Var(0x16758B8) = static_cast<int32_t>(Rd32(p.body + 16)); // g_AttrBuff296    0x493F53
            g_Client.Var(0x16758BC) = static_cast<int32_t>(Rd32(p.body + 20)); //                  0x493F66
            g_Client.Var(0x16758C0) = static_cast<int32_t>(Rd32(p.body + 24)); //                  0x493F79
            g_Client.Var(0x16758C8) = static_cast<int32_t>(Rd32(p.body + 28)); // NB : C4 sauté    0x493F8C
            g_Client.Var(0x16758CC) = static_cast<int32_t>(Rd32(p.body + 32)); //                  0x493F9F
            g_Client.Var(0x16758D0) = static_cast<int32_t>(Rd32(p.body + 36)); //                  0x493FB2
            g_Client.Var(0x16758D4) = static_cast<int32_t>(Rd32(p.body + 40)); //                  0x493FC5
            RecalcAttackRatingSetAll();   // 4 globals en affectation sèche, SANS clamp
            if (p.subOpcode == 19) {
                g_Client.msg.System(Str(2945));            // 0x49401A (push B79h)
            } else {  // subOpcode == 20
                g_Client.inv.currency -= 1000;             // 0x494152 g_Currency / 0x494162 miroir
                if (p.value == 0)      g_Client.msg.System(Str(2943));  // 0x494195 (push B7Fh)
                else if (p.value == 1) g_Client.msg.System(Str(2944));  // 0x4941B7 (push B80h)
            }
            break;
        }

        default:
            break;   // def_49320B 0x4941C7 : `return` sec — n'écrit RIEN (pas même le latch).
        }
    });

    // 0x5b QuickslotSync — mode 1 = charge les raccourcis ; mode 2 = argent/poids.
    // Ancre : Net_OnQuickslotSync 0x4944A0. L'imbrication suit le binaire : le switch
    // EXTERNE porte sur v3=mode (0x494517/0x494520), le gate `if (!v6)` est INTERNE à
    // chaque mode (0x49452E pour mode 1, 0x494588 pour mode 2).
    OnPacket<QuickslotSync>(sys, 0x5b, [](const QuickslotSync& p) {
        if (p.mode == 1) {                       // 0x494517
            if (p.flag == 0) {                   // 0x49452E
                for (int i = 0; i < 50; ++i)     // copie quickslot[0..49] -> dword_184C0F8[] (0x49456C)
                    g_Client.Var(0x184C0F8 + static_cast<uint32_t>(i) * 4u) =
                        static_cast<int32_t>(p.quickslot[i]);
            }
        } else if (p.mode == 2) {                // 0x494520
            // g_GmCmdCooldownLatch = 0 dès mode==2 (0x494577), INDÉPENDAMMENT de v6 :
            // le binaire libère le verrou AVANT le test `if (!v6)` (0x494588). L'imbriquer
            // sous flag==0 laisserait le verrou armé sur un échec de synchro d'argent.
            g_GmCmdCooldownLatch = 0;
            if (p.flag == 0) {                   // 0x494588
                g_Client.inv.weight = static_cast<int64_t>(p.money);  // g_InvWeight 0x494592
                g_Client.msg.System(Str(640));   // 0x4945A8
            }
        }
    });

    // 0x61 ServerNameNotice — subop1 = message par id (StrTable003) ; subop2 = 3 floats.
    // Ancre : Net_OnServerNameNotice 0x4A5540.
    OnPacket<ServerNameNotice>(sys, 0x61, [](const ServerNameNotice& p) {
        if (p.subop == 1) {
            uint32_t id = Rd32(p.data);           // memcpy 0x4A5594 : data[0..3] = id string
            // 0x4A55AC : StrTable003_Get(dword_84A6A8, v4) — table des NOMS DE ZONE (003.DAT),
            // et NON StrTable005_Get(g_LangId, …) qu'expose game::Str() (005.DAT / mMESSAGE).
            // Tables distinctes (fichiers + strides différents) : le même index rend un texte
            // différent. Conforme au commentaire de champ RecvPackets.h:731.
            g_Client.msg.System(Str3(static_cast<int>(id)));
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
        case 6:  // active un attribut de talisman (recalcul AR). 0x4A58AE
            if (p.value != 0) {                                   // 0x4A58AE : if (v13)
                if (slot >= 0) {                                  // 0x4A58DD
                    int32_t hi, lo, packed;
                    if (slot >= 10) {                             // 0x4A58F9
                        // Unpack dword_1675664[slot], force lo=0, repack, écrit v12.
                        game::Stat_UnpackCombined(g_Client.VarGet(0x1675664 + static_cast<uint32_t>(slot) * 4u), hi, lo); // 0x54CE40
                        game::Stat_PackCombined(hi, 0, packed);   // v15=0 ; 0x54CEB0
                        g_Client.Var(0x1675664 + static_cast<uint32_t>(slot) * 4u) = packed;              // 0x4A599E
                        g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A59AE
                    } else {
                        game::Stat_UnpackCombined(g_Client.VarGet(0x167568C + static_cast<uint32_t>(slot) * 4u), hi, lo); // 0x4A5916
                        game::Stat_PackCombined(hi, 0, packed);   // 0x4A5933
                        g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = packed;              // 0x4A5940
                        g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5950
                    }
                    RecalcTalismanAttackRating();                 // 0x4A59BF..
                    g_Client.msg.System(Str(2165));               // 0x4A5A25
                }
                // slot<0 : no-op (aucun message) — fidèle à 0x4A58D1 "return result"
            } else {
                g_Client.msg.System(Str(2166));                  // 0x4A58C1 : value==0 UNIQUEMENT
            }
            break;
        case 7:  // pose une valeur dans le slot (recalcul AR). 0x4A5A3C
            if (slot < 0) break;                                  // 0x4A5A41 : no-op
            if (slot >= 10) {
                if (slot >= 20) break;                            // 0x4A5A7E : no-op
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5A88
            } else {
                g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5A65
            }
            RecalcTalismanAttackRating();                         // 0x4A5AA0..
            g_Client.msg.System(Str(2213));                      // 0x4A5B07
            break;
        case 8:  // idem 7, message différent. 0x4A5B1E
            if (slot < 0) break;                                  // 0x4A5B23 : no-op
            if (slot >= 10) {
                if (slot >= 20) break;                            // 0x4A5B5F : no-op
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5B6A
            } else {
                g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5B46
            }
            RecalcTalismanAttackRating();                         // 0x4A5B82..
            g_Client.msg.System(Str(2181));                      // 0x4A5BE9
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
            // Branche `else if (!dword_1675DB8[g_LocalElement])` (0x4A9E70) : outre le
            // chargement de modèle, le binaire remet DEUX états à zéro (0x4A9E8B / 0x4A9E9E).
            // Ces resets sont de l'état pur (aucune dépendance à l'asset .IMG) — le périmètre
            // invoqué par le TODO(state) ci-dessous ne les couvre pas.
            if (g_Client.VarGet(0x1675DB8 + g_LocalElement * 4u) == 0) {
                g_Client.Var(0x1675D98 + g_LocalElement * 4u)  = 0;      // 0x4A9E8B
                g_Client.VarF(0x1675DA8 + g_LocalElement * 4u) = 0.0f;   // 0x4A9E9E
            }
            // TODO(state) [ancre 0x4A9E41 / 0x4A9E81] : World_LoadCurrentZoneModel(g_GameWorld,
            //   g_LocalElement+1) si dword_1675DB8[elem]==1, sinon (…, 6) — plus, dans la branche
            //   ==1, flt_1675DA8[elem] = ModelObj_GetSubObjectCount(&unk_B68CCC, 0) - 1 (0x4A9E61),
            //   qui DÉPEND du modèle chargé : non calculable sans l'asset. Hors périmètre réseau.
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
        case 5: {  // ligne système + message flottant, MÊME tampon
            // 0x4A9EF2-0x4A9F05 : Crt_Vsnprintf(v6, "[%d]%s", v7, StrTable005_Get(245))
            // -> ordre gauche-à-droite = "[value]Str245" (et non "Str245 value").
            const std::string buf = Fmt("[%d]%s", p.value, Str(245).c_str());
            g_Client.msg.System(buf, 1);         // 0x4A9F18 : Msg_AppendSystemLine(_, v6, 1)
            g_Client.msg.Floating(0, 0, buf);    // 0x4A9F2F : HUD_ShowFloatingMessage(_, 0, 0, v6, &String)
            break;
        }
        case 6:
            g_Client.Var(0x1675DCC)  = 1;               // 0x4A9F39
            g_Client.VarF(0x1675DD4) = 0.0f;            // 0x4A9F45
            g_Client.msg.Floating(0, 0, Str(246));      // 0x4A9F69 : HUD_ShowFloatingMessage(_, 0, 0, Str246, &String)
            g_Client.msg.System(Str(246), 1);           // 0x4A9F85 : Msg_AppendSystemLine(_, Str246, 1)
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
    // Ancre : Net_OnSummonSpawn 0x4AA810 — v3=status @0x8156C1 (0x4AA846), v1=slot @0x8156C5
    // (0x4AA859), garde `if (v1 < 4 && !v3)` (0x4AA879), puis `return Warp_SendTeleport(v1, v2)`
    // (0x4AA88B) avec v2 = {-14.0f, 0.0f, -4242.0f} (0x4AA82A/2F/38).
    OnPacket<SummonSpawn>(sys, 0x84, [](const SummonSpawn& p) {
        if (p.slot < 4u && p.status == 0) {
            // Position d'invocation codée en dur dans le handler d'origine.
            const float pos[3] = { -14.0f, 0.0f, -4242.0f };
            // game::Warp_SendTeleport 0x5F5CE0 (MapWarp.h:187, corps MapWarp.cpp:281) : arme le
            // warp (mode 6, zones {138,139,165,166}) ET émet Op20 (EA 0x5F5DD6). Le paramètre
            // `nc` reste au défaut nullptr -> ArmFullWarp le résout vers net::GlobalNetClient()
            // (MapWarp.cpp:86-87) : l'envoi est RÉEL, conforme au binaire qui adresse g_NetClient
            // en global. La garde interne (a1<=3 && !g_MorphInProgress, EA 0x5F5D1A) est portée.
            game::Warp_SendTeleport(static_cast<uint16_t>(p.slot), pos);
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
    // Ancre : Net_OnBuffEffectDispatch 0x4A88D0 (7 memcpy, EA 0x4A891F..0x4A89BE = 28 o utiles).
    OnPacket<BuffEffectDispatch>(sys, 0xae, [](const BuffEffectDispatch& p) {
        switch (p.subOpcode) {
        case -1:  // (EA 0x4A91EF) — pose deux valeurs de stat PUIS émet str2659
            g_Client.Var(0x1675894) = p.param5;   // 0x4A91EF
            g_Client.Var(0x1675898) = p.param6;   // 0x4A91F8
            // TODO(player) [ancre 0x4A9202] : Player_CheckStateDigit(&g_PlayerCmdController)
            //   0x511740 — le seul portage C++ est un stub VIDE local au TU GameVarDispatch.cpp
            //   (l.127), non déclaré en header : l'appeler serait un no-op trompeur.
            g_Client.msg.System(Str(2659));       // 0x4A9218 — le cas 5 n'a PAS ce message
            break;
        case 5:  // (EA 0x4A91D2) — mêmes deux Var, mais AUCUN message (≠ cas -1)
            g_Client.Var(0x1675894) = p.param5;   // 0x4A91D2
            g_Client.Var(0x1675898) = p.param6;   // 0x4A91DA
            // TODO(player) [ancre 0x4A91EA] : Player_CheckStateDigit (cf. cas -1).
            break;
        case 1: {  // pose un objet dans la grille d'inventaire (EA 0x4A89E2)
            uint32_t count = (p.param1 == 12101) ? 99u : 0u;   // 0x4A8A38 / 0x4A8A49 / 0x4A8A65
            g_Client.inv.Set(p.param2, p.param3, static_cast<uint32_t>(p.param1),
                             p.param4 % 8u, p.param4 / 8u, count, 0, 0);
            g_Client.Var(0x1675894) = p.param5;   // 0x4A8AA7 — manquant jusqu'ici
            g_Client.Var(0x1675898) = p.param6;   // 0x4A8AB0
            // TODO(player) [ancre 0x4A8ABA] : Player_CheckStateDigit (cf. cas -1).
            g_Client.msg.System(Str(2901));       // 0x4A8AD0 — manquant jusqu'ici
            break;
        }
        case 2: g_Client.msg.System(Str(598));  break;  // 0x4A8AF6
        case 4: g_Client.msg.System(Str(1257)); break;  // 0x4A91BD
        case 3: {  // sous-dispatcher d'effets temporels (EA 0x4A8B0E)
            g_Client.Var(0x1675894) = p.param5;   // 0x4A8B0E
            g_Client.Var(0x1675898) = p.param6;   // 0x4A8B16
            // TODO(player) [ancre 0x4A8B21] : Player_CheckStateDigit (cf. cas -1).
            const int v26 = p.param1 / 1000;      // 0x4A8B31
            // 0x4A8B45 : v31 = -v35 % 1000. En division tronquée (C/C++ comme x86 idiv),
            // (-a)%b et -(a%b) sont IDENTIQUES — la forme ci-dessous est équivalente.
            const int v31 = -(p.param1 % 1000);

            // 1er switch (0x4A8B87) : v26 -> v27 (id d'ITEM) ou v30 (id de CHAÎNE).
            // v27/v30 initialisés à 0 (EA 0x4A8B48 / 0x4A8B4F) ; ni -14 ni -5 n'ont de cas.
            int v27 = 0;  // id item (MobDb_GetEntry)
            int v30 = 0;  // id chaîne (StrTable005)
            switch (v26) {
            case -16: v27 =  1894; break;  // 0x4A8C1E
            case -15: v27 =  1097; break;  // 0x4A8C12
            case -13: v27 =  1166; break;  // 0x4A8C06
            case -12: v27 =  1124; break;  // 0x4A8BFA
            case -11: v27 =  1103; break;  // 0x4A8BEE
            case -10: v27 =  1108; break;  // 0x4A8BE2
            case  -9: v27 = 12105; break;  // 0x4A8BD6
            case  -8: v27 =   869; break;  // 0x4A8BCA
            case  -7: v30 =  2318; break;  // 0x4A8BC1
            case  -6: v30 =   918; break;  // 0x4A8BB8
            case  -4: v30 =  2647; break;  // 0x4A8BAF
            case  -3: v30 =  2646; break;  // 0x4A8BA6
            case  -2: v30 =  2645; break;  // 0x4A8B9A
            case  -1: v30 =  2644; break;  // 0x4A8B8E
            default: break;
            }

            // 2e switch (0x4A8C2F) : compteurs en `+=` (DELTA) — sémantique DISTINCTE du
            // `= value` que Net/GameVarDispatch.cpp applique aux MÊMES globals sous 0x16.
            //
            // ÉCART ASSUMÉ (UB d'origine) : quand MobDb_GetEntry rend 0, l'original émet
            // quand même la ligne avec v28 NON INITIALISÉ (1000 o de pile — EA 0x4A8CB9,
            // 0x4A8DF1, 0x4A8E74, 0x4A8EF7, 0x4A8F7D, 0x4A8FFF, 0x4A9084). Comportement
            // indéfini, non reproductible : on émet une chaîne VIDE. En pratique inatteignable
            // (v27 sont des ids codés en dur, présents en base d'items).
            const char* nm = nullptr;
            switch (v26) {
            case -6:   // aucun compteur ; le format EST Str(v30), sans argument
                g_Client.msg.System(FmtFromStrTable(v30));                  // 0x4A8C3A/47/0x4A8DB1
                break;
            case -7:   // aucun compteur ; Str(v30) formaté avec 180
                g_Client.msg.System(FmtFromStrTable(v30, 180));             // 0x4A8C6B/78/80
                break;
            case -8:
                g_Client.Var(0x167587C) += v31;                             // 0x4A8C96
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8CAC
                g_Client.msg.System(nm ? FmtFromStrTable(2825, nm, v31) : std::string()); // LABEL_53 0x4A9001/26
                break;
            case -9: {  // aucun compteur
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8D28
                std::string s;
                if (nm) s = (v31 == 1) ? FmtFromStrTable(2823, nm, 1)       // 0x4A8D55/62
                                       : FmtFromStrTable(2824, nm, v31);    // 0x4A8D84/91
                g_Client.msg.System(s);                                     // 0x4A8D6A
                break;
            }
            case -10:
                g_Client.Var(0x16746E4) += v31;                             // 0x4A8DCD
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8DE4
                g_Client.msg.System(nm ? FmtFromStrTable(2826, nm) : std::string()); // LABEL_49 0x4A8F7F/A0
                break;
            case -11:
                g_Client.Var(0x16746E8) += v31;                             // 0x4A8E50
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8E67
                g_Client.msg.System(nm ? FmtFromStrTable(2826, nm) : std::string()); // LABEL_49
                break;
            case -12:
                g_Client.Var(0x1674708) += v31;                             // 0x4A8ED3
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8EEA
                g_Client.msg.System(nm ? FmtFromStrTable(2825, nm, v31) : std::string()); // LABEL_53
                break;
            case -13:
                g_Client.Var(0x1674794) += v31;                             // 0x4A8F59
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8F70
                g_Client.msg.System(nm ? FmtFromStrTable(2826, nm) : std::string()); // LABEL_49
                break;
            case -15:
                g_Client.Var(0x1674700) += v31;                             // 0x4A8FDB
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8FF2
                g_Client.msg.System(nm ? FmtFromStrTable(2825, nm, v31) : std::string()); // LABEL_53
                break;
            case -16:
                g_Client.Var(0x1674AA0) += v31;                             // 0x4A9061
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A9077
                g_Client.msg.System(nm ? FmtFromStrTable(2999, nm) : std::string()); // 0x4A909A/A7
                break;
            case -17: {  // aucun compteur ; double formatage littéral
                const std::string a = Fmt("%s %d", Str(2293).c_str(), v31); // 0x4A90E8/FA
                g_Client.msg.System(Fmt("%s%s", a.c_str(), Str(2648).c_str())); // 0x4A910C/25/40
                break;
            }
            default:  // 0x4A915B — couvre v26 = -14, -5..-1 et hors plage
                g_Client.msg.System(Fmt("%s%s %d%s", Str(2532).c_str(), Str(v30).c_str(),
                                        v31, Str(2648).c_str()));           // 0x4A9179/8B/A5
                break;
            }
            break;
        }
        default: break;   // cas 0 et hors plage : `return` sec (0x4A89C4)
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

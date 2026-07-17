// Scene/SceneManager.cpp — machine de scènes : Intro -> ServerSelect/Login/CharSelect (LoginScene)
// -> EnterWorld -> InGame (GameHud). Dispatch fidèle à cSceneMgr_Update/_Render.
#include "UI/LoginScene.h"      // tire Net/NetSystem.h (winsock2) en premier
#include "UI/GameHud.h"
#include "UI/GameWindows.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Net/NetSystem.h"
#include "Scene/SceneManager.h"
#include "Scene/WorldRenderer.h"
#include "Gfx/WorldGeometryRenderer.h" // géométrie statique .WO (distinct de WorldRenderer=entités)
#include "Gfx/GxdRenderer.h"           // GXD_RenderPostBlur 0x4053E0 (bloom, câblé en fin de rendu InGame)
#include "Game/PlayerAnimCursorTick.h" // Player_AdvanceAnimCursor (avance curseur anim joueur, switch 0x5727BF)
#include "Gfx/FxSetters.h"             // FxPool_* + FxSlot (pool FX combat dword_17D06F4, Vague D)
#include "Gfx/FxBillboard.h"           // FxBillboard_PoolTick/SetDevice (leaf Object A .PARTICLE, Vague D)
#include "Gfx/ModelObjectRenderer.h"   // renderer mesh model-object (ModelObj_Draw 0x4D71B0, FX combat mesh, Vague F)
#include <cstring>                     // std::memcpy (lecture race/genre depuis PlayerEntity::body)
#include "World/WorldIntegration.h"    // world::WorldAssets (charge réellement Z%03d.WO)
#include "World/WorldMap.h"            // world::WorldMap::LoadZoneResource / ZoneIdToFileId
#include "Audio/AudioSystem.h"        // audio::BgmChannel (slot BGM de scène, cSceneMgr +612)
#include "Config/GameOptions.h"       // config::g_Options.BgmEnabled (g_BgmEnabled 0x84DEF0)
#include "Gfx/SpriteBatch.h"    // gfx::g_GameTimeSec
#include "Game/GameState.h"     // game::g_World (zoneId)
#include "Game/ClientRuntime.h" // game::Str (messages d'erreur EnterWorld)
#include "Game/MapWarp.h"       // game::kSelfActionStateOffset (porte de gating InGame étape 12)
#include "Net/SendPackets.h"    // Net_SendPacket_Op13 (keepalive) / Net_SendOp64 (poll requête clan/faction)
#include "Net/CharSelectPackets.h" // net::BuildEnterWorldTail72 (bloc tail72 confirmé)
// 4 systèmes d'appoint du tick InGame (mission de câblage 2026-07-14, cf. rapports des
// agents dédiés) : anim/collision, cycle de vie d'entité, caméra/warp/potion/guilde,
// combo/pickup/quête. Câblés ci-dessous dans case Scene::InGame (RunMainTick).
#include "Game/AnimationTick.h"
#include "Game/EntityLifecycleTick.h"
#include "Game/CameraWarpTick.h"
#include "Game/ComboPickupTick.h"
#include "Game/MotionPools.h"    // game::LoadedCoordTable / kCoordTableRow* — table GINFO-003
                                  // (mZONEMOVEINFO 350x805 FLOAT, base d'origine flt_1555D08)
#include "Game/NpcInteraction.h" // NpcInteractionSystem::AutoInteractForPet (déjà porté, réutilisé)
// 3 systèmes supplémentaires câblés dans ce même bloc (mission de câblage 2026-07-14,
// suite des 4 ci-dessus) : effets au sol/auras/objets de zone, gate auto-cible/combat,
// pont caméra 3e personne.
#include "Game/GroundAuraWorldObjectTick.h"
#include "Game/AutoTargetCombatGate.h"
#include "Gfx/CameraThirdPersonBridge.h"
#include "Net/NetClient.h"       // net::GlobalNetClient / NetCloseSocket (SCN-01 : action OK de notice)
#include "Core/Log.h"
#include <windows.h>
#include <cstring>
#include <cstdio>   // std::snprintf (chemin BGM "Z%03d.BGM")
#include <cstdint>
#include <vector>   // std::vector (candidats de combo GINFO-003, cf. ComboCandidateLookup)

// hWndParent 0x815184 — fenêtre destinataire du WSAAsyncSelect(WM_USER+1) posé par
// Net_ConnectGameServer 0x462A70. Défini par Net/GameHandlers_Misc.cpp:158, qui laisse le
// TODO explicite « App doit poser ts2::net::g_GameSocketNotifyWnd à l'init, via une
// déclaration extern » (GameHandlers_Misc.cpp:153-157) — SANS assignation, le handler du
// paquet 0x18 (Pkt_GameServerConnectResult 0x469CF0, reconnexion/relais EN COURS DE JEU)
// dégrade en échec WSAAsyncSelect (Str107) et la reconnexion échoue TOUJOURS.
// Aucun header ne le déclare à ce jour -> déclaration extern locale (le linker résout sur
// la définition de ts2::net). Assigné dans SceneManager::Init : notifyHwnd_ EST hWndParent
// (App_Init @0x461C51 `mov ds:hWndParent, ecx` avec ecx = le HWND créé par WinMain 0x4609C0 ;
// côté C++, App::Init passe ce même hwnd_ à SceneManager::Init), et SceneManager::Init est
// appelée DEPUIS App::Init — donc au même instant que l'assignation d'origine. // 0x815184
namespace ts2::net { extern HWND g_GameSocketNotifyWnd; }

// TODO journalisé UNE SEULE FOIS par point d'intégration (évite le flood de logs pour les
// hooks appelés à 30 Hz depuis InGameTickFlow_Update — cf. case Scene::InGame ci-dessous).
#define TS2_INGAME_TODO_ONCE(...) do { \
        static bool s_ts2IngameTodoWarned = false; \
        if (!s_ts2IngameTodoWarned) { s_ts2IngameTodoWarned = true; TS2_LOG(__VA_ARGS__); } \
    } while (0)

namespace ts2 {

// Définition du miroir de g_SceneSubState 0x1676184 (champ +4 de cSceneMgr 0x1676180),
// déclaré dans SceneManager.h. Tenu à jour aux 4 points de synchro marqués « 0x1676184 »
// ci-dessous ; consommé par App/PlayerInputController.cpp (garde Camera_UpdateFromInput
// @0x50B7EC). Valeur initiale 0 = sous-état d'entrée de toute scène. // 0x1676184
int g_SceneSubState = 0;

// ===========================================================================
// GINFO-003 — sous-tables A/B de mZONEMOVEINFO (« G02_GINFO\003.BIN », 350x805 FLOAT,
// base d'origine flt_1555D08, chargée par Motion_LoadGInfo003Bin 0x4FD420 et exposée ici
// par game::LoadedCoordTable(), cf. Game/MotionPools.h).
//
// Layout de ligne RE-DÉRIVÉ des TROIS seuls lecteurs de cette table dans le binaire :
//   GInfo2_GetVec3           0x4FD4C0 : row[0..2]    = position (x,y,z)      [déjà porté]
//   Combo_FindNearbyFollowup 0x501270 : row[3]       = countA        (DWORD) @0x501290
//                                       row[4+3i]    = vec3 candidat A (i < countA)
//                                       row[304+i]   = idA           (DWORD) @0x501337
//   GInfo2_FindVec3ByKey     0x4FD540 : row[404]     = countB        (DWORD) @0x4FD583
//                                       row[705+i]   = clé B         (DWORD) (i < countB)
//                                       row[405+3i]  = vec3 B                @0x4FD5FF/620/642
// Somme 3+1+300+100+1+300+100 = 805 -> la ligne est INTÉGRALEMENT expliquée (le gap
// GINFO-003-SUBTABLES-UNWIRED portait sur ces 802 float jamais lus).
//
// `row` 0-based = 805*(motionId-1) : le binaire indexe en 1-based via `805*a2 - 805`.
// ATTENTION : les champs de comptage/clé/id sont lus en DWORD SUR la table FLOAT (motif
// `*((_DWORD *)this + ...)` du binaire) -> RÉINTERPRÉTATION binaire des bits, surtout PAS
// une conversion float->int.
// ===========================================================================
namespace {

// Réinterprète en int32 le mot d'index `idx` de la table FLOAT (motif `*((_DWORD *)this + idx)`
// de Combo_FindNearbyFollowup 0x501270 / GInfo2_FindVec3ByKey 0x4FD540).
int32_t GInfo2AsI32(const float* table, std::size_t idx) {
    int32_t v = 0;
    std::memcpy(&v, table + idx, sizeof(v));
    return v;
}

std::size_t GInfo2TableSize() {
    return static_cast<std::size_t>(game::kCoordTableRowCount) *
           static_cast<std::size_t>(game::kCoordTableRowStride);
}

// Sous-table A — candidats de combo de suivi de `motionId` (Combo_FindNearbyFollowup 0x501270 :
// boucle `i < row[3]`, position `row[4+3i]`, id `row[304+i]`). La sélection finale (distance
// < 30.0 puis Combo_CheckTransition == 1) reste faite par game::Combo_FindNearbyFollowup.
std::vector<game::ComboMotionCandidate> GInfo2ComboCandidates(int motionId) {
    std::vector<game::ComboMotionCandidate> out;
    const float* table = game::LoadedCoordTable(); // flt_1555D08
    if (!table) return out;                        // table non chargée -> aucun candidat
    if (motionId < 1 || motionId > game::kCoordTableRowCount) return out; // garde @0x501286

    const std::size_t row = static_cast<std::size_t>(game::kCoordTableRowStride) *
                            static_cast<std::size_t>(motionId - 1);       // `805*a2 - 805`
    const std::size_t tableSize = GInfo2TableSize();

    const int32_t countA = GInfo2AsI32(table, row + 3);                   // row[3] @0x501290
    if (countA > 0 && countA <= 100) out.reserve(static_cast<std::size_t>(countA));

    for (int32_t i = 0; i < countA; ++i) {                                // @0x501290
        const std::size_t posIdx = row + 4 + 3 * static_cast<std::size_t>(i);  // row[4+3i]
        const std::size_t idIdx  = row + 304 + static_cast<std::size_t>(i);    // row[304+i]
        // Le binaire ne borne PAS la boucle à 100 (il lit `countA` tel quel dans un tableau
        // global plat) : cette garde est une sûreté MÉMOIRE sur le std::vector, pas un
        // changement de comportement — pour toute ligne bien formée (countA <= 100) les deux
        // lectures restent dans la ligne et le résultat est identique au binaire.
        if (posIdx + 2 >= tableSize || idIdx >= tableSize) break;
        game::ComboMotionCandidate c;
        c.id = GInfo2AsI32(table, idIdx);   // @0x501337
        c.x  = table[posIdx + 0];
        c.y  = table[posIdx + 1];
        c.z  = table[posIdx + 2];
        out.push_back(c);
    }
    return out;
}

// Sous-table B — GInfo2_FindVec3ByKey 0x4FD540 : cherche `originKey` dans la liste de clés de
// `followupMotionId` (row[705+i], i < row[404]) et renvoie le vec3 associé (row[405+3i]).
// Non trouvé / hors bornes -> {0,0,0} : le binaire zéro le vec3 de sortie AVANT la recherche
// (@0x4FD54E/555/55D) et le laisse tel quel si la boucle s'achève sans correspondance.
void GInfo2ComboOrigin(int followupMotionId, int originKey, float outPos[3]) {
    outPos[0] = 0.0f; // @0x4FD54E
    outPos[1] = 0.0f; // @0x4FD555
    outPos[2] = 0.0f; // @0x4FD55D

    const float* table = game::LoadedCoordTable(); // flt_1555D08
    if (!table) return;
    if (followupMotionId < 1 || followupMotionId > game::kCoordTableRowCount) return; // @0x4FD56D

    const std::size_t row = static_cast<std::size_t>(game::kCoordTableRowStride) *
                            static_cast<std::size_t>(followupMotionId - 1);
    const std::size_t tableSize = GInfo2TableSize();

    const int32_t countB = GInfo2AsI32(table, row + 404);                 // row[404] @0x4FD583
    for (int32_t i = 0; i < countB; ++i) {
        const std::size_t keyIdx = row + 705 + static_cast<std::size_t>(i);   // row[705+i]
        if (keyIdx >= tableSize) return;                                       // sûreté mémoire
        if (GInfo2AsI32(table, keyIdx) != originKey) continue;
        const std::size_t posIdx = row + 405 + 3 * static_cast<std::size_t>(i); // row[405+3i]
        if (posIdx + 2 >= tableSize) return;                                    // sûreté mémoire
        outPos[0] = table[posIdx + 0]; // @0x4FD5FF
        outPos[1] = table[posIdx + 1]; // @0x4FD620
        outPos[2] = table[posIdx + 2]; // @0x4FD642
        return;
    }
    // Boucle achevée sans correspondance (`i == countB` @0x4FD5DC) -> vec3 laissé à {0,0,0}.
}

// ===========================================================================
// gx-fx-01 — Reconstitution des paramètres de spawn de projectile d'attaque monstre.
// Fx_SpawnAttackProjectile 0x582530 (et sa variante Alt 0x582A10) lisent leur `this` = un
// enregistrement MONSTRE (dword_1766F74, stride 280). Ce câblage (le producteur documenté du
// pool, cf. Game/GroundAuraWorldObjectTick.h:204 « Char_Update -> spawn, mission séparée »)
// remplit game::FxProjectileSpawnParams depuis game::g_World.monsters[idx], son MONSTER_INFO
// résolu (m.def, +0x60) et g_MonsterTickExt[idx] (cible d'attaque). Chaque champ porte l'offset
// binaire RE-PROUVÉ par décompilation directe de 0x582530 cette mission (EA en commentaire) ;
// this+96 == m.def (déréférencé sans garde @0x5825DF -> un monstre actif a toujours un def
// résolu, Pkt_SpawnMonster 0x467B00 rejette sinon, cf. EntityManager.cpp:410).
// Retourne false (aucun spawn) si l'index est invalide ou m.def nul : simple filet de sûreté
// mémoire, jamais franchi pour un monstre actif bien formé.
bool BuildMonsterProjectileParams(int idx, game::FxProjectileSpawnParams& p) {
    if (idx < 0 || static_cast<std::size_t>(idx) >= game::g_World.monsters.size()) return false;
    const game::MonsterEntity& m = game::g_World.monsters[static_cast<std::size_t>(idx)];
    if (!m.def) return false;                                   // *(this+96) @0x5825DF (actif => def!=null)
    const uint8_t* def = reinterpret_cast<const uint8_t*>(m.def);

    p.owner     = m.id;                                          // *(this+4)/*(this+8)  @0x5825B5/0x5825C7
    p.startX    = m.x;                                           // *(float*)(this+32)   @0x58281F
    p.startYRaw = m.y;                                           // *(float*)(this+36)   @0x58283D (avant + heightOffset)
    p.startZ    = m.z;                                           // *(float*)(this+40)   @0x58284F
    p.heading   = m.heading;                                     // *(float*)(this+56)   @0x58286F
    // targetX/Y/Z = *(float*)(this+44/48/52) = m.body[28/32/36] (body démarre à record+16).
    std::memcpy(&p.targetX, m.body.data() + 28, sizeof(p.targetX)); // @0x5828D7
    std::memcpy(&p.targetY, m.body.data() + 32, sizeof(p.targetY)); // @0x5828E9
    std::memcpy(&p.targetZ, m.body.data() + 36, sizeof(p.targetZ)); // @0x5828FB
    // Champs MONSTER_INFO (int32 aux offsets OCTET du record def ; record de 944 o, bornes sûres).
    std::memcpy(&p.weaponId,      def + 244, sizeof(p.weaponId));      // *(*(this+96)+244) @0x5825DF
    std::memcpy(&p.weaponSubtype, def + 236, sizeof(p.weaponSubtype)); // *(*(this+96)+236) @0x58268F
    std::memcpy(&p.heightOffset,  def + 328, sizeof(p.heightOffset));  // *(*(this+96)+328) @0x58283D
    std::memcpy(&p.speed,         def + 332, sizeof(p.speed));         // *(*(this+96)+332) @0x582913
    // cible = *(this+68)/*(this+72) = MonsterTickExt::attackTargetId (peuplée par l'IA de combat
    // réseau, HORS de ce front — d'où la latence documentée au site de câblage). g_MonsterTickExt
    // est dimensionné par UpdateMonster (EnsureCapacity) avant l'appel du hook ; garde défensive.
    if (static_cast<std::size_t>(idx) < game::g_MonsterTickExt.size())
        p.target = game::g_MonsterTickExt[static_cast<std::size_t>(idx)].attackTargetId; // @0x582601/0x582613
    return true;
}

// ===========================================================================
// SCN-01 — Action du bouton OK du dialogue de notice (UI_NoticeDlg_OnLButtonUp 0x5C03F0,
// switch (*(this+4)) @0x5C04C9).
//
// ⚠ LE GAP TEL QUE LIBELLÉ DANS LE DOSSIER EST PÉRIMÉ — NE PAS Y APPLIQUER SON CORRECTIF.
// Le dossier prescrit d'écrire un `NoticeDialog : public ui::Dialog` (rendu 0x5C0630 +
// hit-test 0x5C03F0) au motif que game::g_Client.prompt aurait « 12 écrivains, 0 lecteur ».
// C'était vrai à l'extraction ; ça ne l'est PLUS : UI/GameWindows.cpp:186 `SyncPrompt()`
// (front UI, MÊME vague) reflète désormais game::g_Client.prompt dans le MsgBoxDialog
// partagé, et il est RÉELLEMENT atteint (GameWindows::Render:237 -> appelé par
// SceneManager::Render `windows_->Render()`). Enregistrer ICI un second dialogue sur le
// MÊME état produirait :
//   (1) un DOUBLE rendu (MsgBox + NoticeDialog dessinant tous deux le prompt) ;
//   (2) du code MORT en entrée — UIManager::Init (UIManager.cpp:243) enregistre msgBox_ en
//       index 0, or Register (UIManager.cpp:258) ne fait qu'un push_back : le MsgBox routerait
//       toujours le clic en premier et le consommerait, notre OnClick ne serait jamais atteint.
// UI/GameWindows.cpp:211-216 laisse d'ailleurs explicitement CE trou-ci à ce front :
//   « TODO [ancre 0x5C04DF] : pour le type 2 (UI_NoticeDlg), l'OK d'origine exécute
//     Net_CloseSocket(&g_NetClient) + g_SceneMgr=2 + g_SceneSubState=0 [...] C'est le
//     périmètre des gaps SCN-03/04 : Scene/SceneManager.* n'est PAS détenu par ce front ».
// On comble donc EXACTEMENT ce trou (l'ACTION, pas le rendu), sans collision.
//
// ⚠ CORRECTION DU DOSSIER (re-prouvée en IDA cette mission) : PromptState FUSIONNE deux
// registres DISTINCTS du binaire. UI_NoticeDlg_OnLButtonUp ne traite que type ∈ [1,9] ; les
// Open(8/9/10/14/19/20) des handlers party/guilde/social appartiennent à un AUTRE dialogue
// (Oui/Non), déjà traité par GameWindows::SyncPrompt (IsNetConfirmType -> Net_SendOp45/49/
// 67/74/55/61). Le switch ci-dessous ne couvre donc QUE les types 1..9 exclus de cette
// liste — sans quoi le type 8 (invitation de groupe) déclencherait À LA FOIS Op45 (bon) et
// Net_SendOp73 (faux, case 8 de la table NOTICE). Les deux tables se CHEVAUCHENT en valeur
// numérique mais pas en sémantique : c'est le piège central de ce gap.
// ===========================================================================

// Retour à la sélection de serveur demandé par `case 2` (@0x5C04E4 `g_SceneMgr = 2`).
// Armé par Notice_DispatchOkAction, consommé par SceneManager::Update (case InGame), sur le
// MÊME motif que game::g_World.sceneEnterWorldPending / sceneReloadPending : on ne change pas
// de scène au milieu du routage d'un clic (l'UI en cours d'itération serait détruite).
// Statique de fichier plutôt que champ de game::GameWorld : le bouton OK en est le SEUL
// écrivain -> aucune dépendance inter-fronts à créer. // 0x5C04E4
bool g_noticeReturnToServerSelectPending = false;
// Fin anormale demandée par `case 3` (@0x5C051B `g_QuitFlag = 1`). Même mécanique.
bool g_noticeAbnormalEndPending = false;

} // namespace

// SCN-01 — cf. le bandeau ci-dessus. Reproduit `switch (*(this+4))` @0x5C04C9 de
// UI_NoticeDlg_OnLButtonUp 0x5C03F0, appelé APRÈS UI_NoticeDlg_Close (@0x5C04A5).
// Le client réseau est résolu via net::GlobalNetClient() (= &g_NetClient 0x8156A0, le
// binaire tape lui aussi le global), MÊME idiome que UI/GameWindows.cpp:152 SendPromptReply.
void Notice_DispatchOkAction(int type) {
    // Types réseau Oui/Non (8/9/10/14/19/20) : NE PAS traiter ici — ils appartiennent au
    // registre MsgBox et sont déjà émis par GameWindows::SyncPrompt (Op45/49/67/74/55/61).
    // Cf. l'avertissement « les deux tables se chevauchent » du bandeau.
    if (type == 8 || type == 9 || type == 10 || type == 14 || type == 19 || type == 20) return;

    net::NetClient* nc = net::GlobalNetClient(); // &g_NetClient 0x8156A0
    switch (type) {                              // @0x5C04C9
    case 1:
        break;                                   // `result = 1;` @0x5C04D0 — aucune action
    case 2:
        // Net_CloseSocket(&g_NetClient) @0x5C04DF PUIS g_SceneMgr=2 @0x5C04E4 /
        // g_SceneSubState=0 @0x5C04EE / dword_1676188=0 @0x5C04F8. ORDRE DU BINAIRE respecté :
        // la socket est fermée AVANT le changement de scène.
        if (nc) net::NetCloseSocket(*nc);                    // 0x463000 @0x5C04DF
        g_noticeReturnToServerSelectPending = true;          // g_SceneMgr = 2 @0x5C04E4
        break;
    case 3:
        // Log_WriteLine("[ABNORMAL_END] ( 3 )") @0x5C0516 puis g_QuitFlag = 1 @0x5C051B.
        TS2_LOG("[ABNORMAL_END] ( 3 )");                     // 0x53F2D0 @0x5C0516
        g_noticeAbnormalEndPending = true;                   // g_QuitFlag 0x815590 @0x5C051B
        break;
    // Les 6 envois de la table NOTICE. Le `this` d'origine est &g_AutoPlayMgr 0x846C08 : ces
    // builders ne lisent aucun champ de cet objet dans le port C++ (signature
    // `void Net_SendOpNN(NetClient&)`, Net/SendPackets.h) -> l'argument disparaît, comme
    // pour tous les autres appels de ce fichier.
    case 4: if (nc) net::Net_SendOp44(*nc); break;           // 0x4B7DC0 @0x5C0531
    case 5: if (nc) net::Net_SendOp48(*nc); break;           // 0x4B83A0 @0x5C0542
    case 6: if (nc) net::Net_SendOp54(*nc); break;           // 0x4B8C60 @0x5C0553
    case 7: if (nc) net::Net_SendOp66(*nc); break;           // 0x4B9E10 @0x5C0564
    case 8: if (nc) net::Net_SendOp73(*nc); break;           // 0x4BA860 @0x5C0575 (inatteignable : filtré ci-dessus)
    case 9: if (nc) net::Net_SendOp60(*nc); break;           // 0x4B9550 @0x5C0586
    default: break;                                          // `result = 1;` @0x5C0592
    }
}

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() { Shutdown(); }

static const char* SceneName(Scene s) {
    switch (s) {
    case Scene::Intro:        return "Intro";
    case Scene::ServerSelect: return "ServerSelect";
    case Scene::Login:        return "Login";
    case Scene::CharSelect:   return "CharSelect";
    case Scene::EnterWorld:   return "EnterWorld";
    case Scene::InGame:       return "InGame";
    default:                  return "None";
    }
}

void SceneManager::Init(gfx::Renderer& renderer, net::NetSystem& net, void* notifyHwnd,
                        int screenW, int screenH, const std::string& gameDataDir,
                        int serverModeFlag) {
    renderer_    = &renderer;
    net_         = &net;
    notifyHwnd_  = notifyHwnd;
    screenW_     = screenW;
    screenH_     = screenH;
    gameDataDir_ = gameDataDir;
    // hWndParent 0x815184 (App_Init @0x461C51) — cf. le bandeau de déclaration en tête de
    // fichier. Seul écrivain de ts2::net::g_GameSocketNotifyWnd, que le handler du paquet
    // 0x18 (Net/GameHandlers_Misc.cpp:216, reconnexion/relais serveur EN JEU) lit pour son
    // WSAAsyncSelect : nul, il dégradait en échec (Str107) et la reconnexion échouait
    // TOUJOURS. `notifyHwnd` EST le HWND créé par WinMain 0x4609C0, relayé par App::Init.
    ts2::net::g_GameSocketNotifyWnd = static_cast<HWND>(notifyHwnd); // 0x815184
    scene_    = Scene::None;
    subState_ = 0;
    frameCount_ = 0;
    g_SceneSubState = 0;   // miroir du champ +4 de cSceneMgr (état neuf) // 0x1676184

    // Scènes shell de connexion (ServerSelect/Login/CharSelect).
    //
    // 7e argument `&renderer` — APERÇU 3D DU PERSONNAGE EN CHARSELECT (scène 4).
    // Char_RenderModel 0x527020 est appelé EXACTEMENT 4 fois dans tout le binaire (xrefs
    // re-vérifiées cette session : 4/4), toutes depuis Scene_CharSelectRender 0x51CED0, en
    // DEUX paires (passe 1 puis passe 2) :
    //   écran LISTE    (this[15714]==1) : @0x51D361 (pass=1) · @0x51D3CC (pass=2)
    //   écran CRÉATION (this[15714]==2) : @0x51D429 (pass=1) · @0x51D480 (pass=2)
    // gfx::MeshRenderer::Init() exige un `gfx::Renderer&` (il n'en lit que Device(), mais
    // on ne peut pas fabriquer un Renderer depuis un IDirect3DDevice9* — Renderer::Init
    // crée son PROPRE device). SANS ce 7e argument, LoginScene::gfxRenderer_ reste nul
    // (LoginScene.cpp:98) -> la garde `if (gfxRenderer_ && charMesh_.Init(*gfxRenderer_))`
    // (LoginScene.cpp:312) échoue -> charModels_/charMotions_ ne sont jamais créés et
    // charPreviewReady_ reste false -> CharSelectRenderPreview3D() sort à sa 1re ligne
    // (LoginScene.cpp:1458) : les 4 Char_RenderModel ne dessinent RIEN et l'écran
    // CharSelect n'affiche AUCUN personnage, alors que le binaire en dessine un.
    // `renderer` est déjà en portée ici (renderer.Device() est passé en 1er argument).
    login_ = std::make_unique<ui::LoginScene>();
    if (!login_->Init(renderer.Device(), &net, static_cast<HWND>(notifyHwnd), screenW, screenH,
                      serverModeFlag, &renderer))
        TS2_WARN("LoginScene::Init a echoue (rendu login indisponible).");

    // HUD en jeu : construit à la volée en entrant en scène InGame.
    hud_ = std::make_unique<ui::GameHud>();
    hudReady_ = false;

    // Fenêtres de jeu (Entrepôt/Guilde/Quête/Compétences/Options/Social/AutoPlay/
    // Marchand/Groupe/Échange/Personnage) : même cycle de vie que le HUD.
    windows_ = std::make_unique<ui::GameWindows>();
    windowsReady_ = false;

    // Rendu 3D des entités (players/monsters) : même cycle de vie que le HUD.
    world_ = std::make_unique<WorldRenderer>();
    worldReady_ = false;

    // Géométrie de monde statique (.WO, cf. Gfx/WorldGeometryRenderer.h) : le rendu GPU
    // (worldGeom_->Init/Build) reste construit paresseusement à l'entrée en InGame (cf.
    // Change()), car il a besoin d'un device D3D "chaud" et de zoneId. En revanche
    // worldAssets_/worldMap_ (données de zone CPU, PAS le rendu) sont désormais construits
    // ICI, PAS à l'entrée InGame comme avant ce câblage : Scene::EnterWorld en a besoin
    // AVANT InGame pour son sous-état LoadZoneResources (cf. Update(), case EnterWorld,
    // host.LoadZoneResource) — voir Docs/TS2_ENTERWORLD_WIRING_TODO.md pour l'audit complet.
    worldGeom_ = std::make_unique<gfx::WorldGeometryRenderer>();
    worldGeomReady_ = false;

    // Slot BGM de scène (cSceneMgr +612) : ctor par défaut = zéro-init, équivalent de
    // cSceneMgr_ReinitBgm 0x517A80 -> SndMgr_InitBgmSlot 0x6A80A0 (SoundObj remis à 0).
    // Le device audio (DirectSoundCreate8) est créé ailleurs (AudioSystem::Init, cf. App) ;
    // ici on ne fait qu'allouer le slot. LoadZoneBgm charge le .BGM à l'entrée en jeu.
    bgm_ = std::make_unique<audio::BgmChannel>();

    if (!gameDataDir_.empty()) {
        worldAssets_ = std::make_unique<world::WorldAssets>(gameDataDir_);
        worldMap_    = std::make_unique<world::WorldMap>(worldAssets_->MakeHooks());
        worldMap_->SetDevice(renderer.Device());
    } else {
        TS2_WARN("SceneManager: gameDataDir vide - WorldMap indisponible "
                 "(EnterWorld/InGame degrades : chargement de zone impossible).");
    }

    TS2_LOG("SceneManager initialise (%dx%d).", screenW, screenH);
}

void SceneManager::Shutdown() {
    if (login_)   { login_->Shutdown();   login_.reset(); }
    if (windows_) { windows_->Shutdown(); windows_.reset(); }
    if (hud_)     { hud_->Shutdown();     hud_.reset(); }
    if (world_)   { world_->Shutdown();   world_.reset(); }
    if (worldGeom_) { worldGeom_->Shutdown(); worldGeom_.reset(); }
    if (modelObjRenderer_) { modelObjRenderer_->Shutdown(); modelObjRenderer_.reset(); } // (Vague F)
    // SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers(cSceneMgr+153) 0x6A80D0 :
    //   libère le slot BGM au destructeur de cSceneMgr (App_Shutdown 0x462480).
    if (bgm_) { bgm_->Release(); bgm_.reset(); }
    worldMap_.reset();
    worldAssets_.reset();
    renderer_ = nullptr;
    net_ = nullptr;
}

void SceneManager::StartIntro() { Change(Scene::Intro); }

void SceneManager::Change(Scene s) {
    const Scene prev = scene_;   // sert au release du slot BGM en QUITTANT InGame (voir bas)
    TS2_LOG("Scene : %s -> %s", SceneName(scene_), SceneName(s));
    scene_ = s;
    subState_ = 0;
    frameCount_ = 0;
    // Tout changement de scène repart du sous-état 0 dans le binaire : le champ +4 de
    // cSceneMgr est remis à 0 par les writers de g_SceneMgr (ex. op 0x18
    // Pkt_GameServerConnectResult 0x469CF0 : g_SceneMgr=5 @0x469d95 PUIS g_SceneSubState=0
    // @0x469d9f), et Scene_EnterWorldUpdate le repose lui-même à 1 après son case 0
    // (@0x52C0B0). Sans ce reset, la garde @0x50B7EC verrait un sous-état périmé de la
    // scène précédente. // 0x1676184
    g_SceneSubState = 0;

    // M6 — chaque ENTREE en scene EnterWorld repart de subState 0 dans le binaire : op 0x18
    // Pkt_GameServerConnectResult 0x469CF0 (@0x469d9f, g_SceneSubState=0) et le flux normal
    // posent g_SceneSubState 0x1676184 = 0, et Scene_EnterWorldUpdate 0x52BFF0 lit son etat
    // courant dans *(this+1)==g_SceneSubState. enterWorldState_ modelise ce champ pour cette
    // scene -> reset symetrique du reset deja present dans ReloadZone. Sans lui, une 2e entree
    // EnterWorld reprend d'un etat perime (WaitServerAck/Failed du cycle precedent -> machine
    // gelee). // 0x1676184 / 0x52BFF0
    if (s == Scene::EnterWorld)
        enterWorldState_ = game::EnterWorldFlowState{};

    // W5b — reset SYMÉTRIQUE pour la scène InGame, même raison que le bloc EnterWorld ci-dessus.
    // Pkt_EnterWorld 0x464160 est le SEUL writer de g_SceneMgr 0x1676180 = 6 — prouvé par balayage
    // d'octets sur l'image entière : find_bytes "C7 05 80 61 67 01 06 00 00 00" -> 1 SEULE occurrence
    // (@0x464304), et aucun store par registre (A3/89 05/89 0D/… sur 0x1676180 = 0 match). Or ce même
    // writer repose AUSSI, dans la foulée, g_SceneSubState 0x1676184 = 0 @0x46430E et
    // dword_1676188 = 0 @0x464318 : l'automate de Scene_InGameUpdate 0x52C600 repart donc de
    // case 0 (Setup) à CHAQUE entrée en scène 6, jamais d'un état hérité.
    // Le `this` de Scene_InGameUpdate EST bien cSceneMgr 0x1676180 : xrefs_to 0x52C600 -> 1 seul
    // appelant (cSceneMgr_Update 0x517BF0 @0x517c79), lui-même appelé avec
    // `mov ecx, offset g_SceneMgr` @0x462636 -> *(this+4) = g_SceneSubState (le switch @0x52C61F)
    // et *(this+8) = dword_1676188 (le compteur). game::InGameTickFlowState{} = {Setup=0,
    // frameCounter=0} en est le miroir 1:1.
    // Sans ce reset, un warp InGame->EnterWorld->InGame (op 0x18 -> op 0x0c) laisse inGameTickState_
    // à MainTick : le `g_SceneSubState = 0` posé ligne 160 est écrasé dès la frame suivante par
    // `g_SceneSubState = (int)inGameTickState_.state` = 4 (ligne ~945), Setup ne rejoue pas, et
    // surtout InitCamera (Cam_SetLookAt @0x52C759 / Camera_SetEyeTarget @0x52C7CF) ne recadre
    // JAMAIS la caméra sur la nouvelle zone. // 0x464304 / 0x46430E / 0x464318 / 0x52C600
    if (s == Scene::InGame)
        inGameTickState_ = game::InGameTickFlowState{};

    // (Vague D — FX combat) Reset du pool de slots FX (dword_17D06F4) à chaque entrée en jeu :
    // miroir de Pkt_EnterWorld 0x464160 @0x4642A4 (for i<g_FxAuraCount Fx_AttachSlotClear(&slot[i])).
    if (s == Scene::InGame)
        gfx::FxPool_Reset();

    // Entrée en jeu : initialise le HUD et les fenêtres de jeu une seule fois (device stable).
    if (s == Scene::InGame && hud_ && !hudReady_ && renderer_) {
        hudReady_ = hud_->Init(*renderer_, screenW_, screenH_);
        if (!hudReady_) TS2_WARN("GameHud::Init a echoue (HUD indisponible).");
    }
    // GAP-APPLIFE-02 — câble le client réseau sur la fenêtre de chat. SANS ce Bind,
    // ChatWindow::SendOnChannel sort immédiatement sur `if (!net_) return;`
    // (UI/ChatWindow.cpp:267) : la saisie s'afficherait en écho local mais AUCUN message ne
    // partirait jamais au serveur. Bind n'avait aucun appelant (grep) — UI/GameHud.h:159-161
    // le réclamait nommément (« requiert que SceneManager possède/expose un net::NetClient& »).
    // Les 7 builders concernés sont ceux de UI_Chat_SubmitInput 0x68B330 (Net_SendOp39/38/
    // 68/77/81/40/80 selon le canal). Ré-appliqué à chaque entrée InGame : le pointeur reste
    // valide (net_ est un membre de App), et une reconnexion réutilise le même NetClient.
    if (s == Scene::InGame && hud_ && hudReady_ && net_)
        hud_->Chat().Bind(&net_->Client());                              // 0x68B330
    if (s == Scene::InGame && windows_ && !windowsReady_ && renderer_) {
        windowsReady_ = windows_->Init(*renderer_, notifyHwnd_, screenW_, screenH_);
        if (!windowsReady_) TS2_WARN("GameWindows::Init a echoue (fenetres indisponibles).");
    }
    if (s == Scene::InGame && world_ && !worldReady_ && renderer_) {
        worldReady_ = world_->Init(*renderer_, screenW_, screenH_);
        if (!worldReady_) TS2_WARN("WorldRenderer::Init a echoue (rendu monde indisponible).");
        // (F_ENTITY3D B8) Source du plan-sol de l'ombre planaire (Model_RenderPlanarShadow 0x40F720,
        // plan récupéré via Collision_SegPickA 0x420D60) : WorldRenderer interroge
        // worldAssets_->GetGroundPlaneForShadow par entité. Même membre/durée de vie que le host
        // GetGroundHeight (worldAssets_ construit une seule fois en Init L388, détruit au Shutdown
        // AVEC world_ L403/409) -> aucun dangling. Sans ce câblage, la passe d'ombre est un no-op propre.
        if (worldReady_ && worldAssets_) world_->SetCollisionSource(worldAssets_.get());
        // (Vague D — FX combat) Amorçage UNE FOIS du leaf Object A + dispatch : device physique
        // (g_GfxRenderer 0x7FFE18), racine des assets (contient G03_GDATA\D05_GPARTICLE), et câblage
        // du hook de rendu particule s_particleRender -> FxBillboard_PoolRender (= lazy-load .PARTICLE
        // + Particle_RenderBillboards 0x6A70B0). s_meshDraw reste nul (ModelObj_Draw 0x4D71B0, système
        // modèle non porté -> les FX mesh block/parry/deflect restent invisibles à ce jalon).
        gfx::FxBillboard_SetDevice(renderer_->Device());
        gfx::FxBillboard_SetDataRoot(gameDataDir_.c_str());
        gfx::Fx_WireLeafHooks();
        // (Vague F) Renderer mesh model-object : enregistre l'instance qui reçoit le hook s_meshDraw
        // (câblé par Fx_WireLeafHooks ci-dessus ; le shim est no-op tant que Init() n'a pas enregistré
        // l'instance). Banque MiscC (unk_B60AB8) pour les FX de combat mesh (block/parry/deflect).
        // ModelObj_Draw 0x4D71B0 ; réutilise le parseur asset::MObject + l'upload GPU du monde.
        if (!modelObjRenderer_) modelObjRenderer_ = std::make_unique<gfx::ModelObjectRenderer>();
        if (!modelObjRenderer_->Init(*renderer_, gameDataDir_))
            TS2_WARN("ModelObjectRenderer::Init a echoue (FX combat mesh indisponibles).");
    }
    // Géométrie de monde statique (.WO) : chargement UNE SEULE FOIS à l'entrée en InGame,
    // pour le zoneId courant (game::g_World.zoneId). Pas de rechargement au changement de
    // zone en cours de partie (TODO futur : accrocher World_LoadZoneResource(ObjectsWO) au
    // flux de warp/MapWarp, cf. Game/MapWarp.h — hors périmètre de ce câblage initial, comme
    // host.LoadZoneResource déjà en TODO dans le case EnterWorld ci-dessous).
    if (s == Scene::InGame && worldGeom_ && !worldGeomReady_ && renderer_) {
        worldGeomReady_ = worldGeom_->Init(*renderer_);
        if (!worldGeomReady_) {
            TS2_WARN("WorldGeometryRenderer::Init a echoue (geometrie statique indisponible).");
        } else if (!worldMap_ || !worldAssets_) {
            // worldMap_/worldAssets_ sont désormais construits UNE FOIS dans Init() (cf.
            // Docs/TS2_ENTERWORLD_WIRING_TODO.md §2), pas ici, pour être déjà disponibles
            // pendant Scene::EnterWorld. nullptr ici => gameDataDir_ était vide à Init()
            // (déjà averti à ce moment-là).
            TS2_WARN("SceneManager: WorldMap indisponible (gameDataDir vide a Init) - "
                     "chargement .WO impossible.");
        } else {
            const int zoneId = game::g_World.zoneId;
            // Pose la cle de zone courante AVANT tout chargement de couche : SetCurrentZoneId
            // n'etait appele nulle part ailleurs (grep) -> World_LoadCurrentZoneModel 0x4DD6E0
            // (qui lit g_SelfMorphNpcId 0x1675A98) travaillait sur zone 0. // 0x4DD6E0
            if (worldMap_) worldMap_->SetCurrentZoneId(zoneId); // g_SelfMorphNpcId 0x1675A98
            // Redondant avec le chargement idx=3 (ResourceKind::ObjectsWO) déjà effectué
            // pendant Scene::EnterWorld (LoadZoneResources, cf. host.LoadZoneResource dans
            // Update()) - WorldMap::LoadZoneResource est idempotent (recharge le même
            // fichier). Gardé ici par sécurité pour les chemins qui forcent
            // Change(Scene::InGame) directement SANS passer par EnterWorld
            // (Scene/SceneAudit.cpp, Tools/UiWindowSelfTest.cpp).
            const unsigned char ok = worldMap_->LoadZoneResource(zoneId, world::ResourceKind::ObjectsWO);
            if (ok) {
                worldGeom_->Build(*worldAssets_);
                TS2_LOG("SceneManager: geometrie .WO zone %d chargee (%zu parts GPU, %zu ignorees A>1).",
                        zoneId, worldGeom_->UploadedPartCount(), worldGeom_->SkippedMultiAnchorCount());
            } else {
                TS2_WARN("SceneManager: World_LoadZoneResource(ObjectsWO, zone=%d) a echoue "
                         "(zoneId->fileId inconnu ou fichier Z%%03d.WO absent).", zoneId);
            }
        }
    }

    // --- Slot BGM de scène (cSceneMgr +612) : câblage enter-world / exit ---
    // Entrée en jeu = "enter-world" : charge+joue en boucle le .BGM de la zone courante,
    //   comme World_LoadZoneResource 0x4DCB60 case 12 (chemin "G03_GDATA\D10_WORLDBGM\Z%03d.BGM")
    //   puis le play gaté g_BgmEnabled (Player_ResetCombatState 0x50f761/0x50f76e ; MÊME cycle
    //   release->load->play que Scene_ServerSelectUpdate 0x518B30 sur le slot cSceneMgr +612).
    //   Hors des gardes du bloc géométrie ci-dessus : le BGM doit se charger même si le rendu
    //   .WO échoue. Filet de sécurité robuste pour les chemins qui forcent Change(InGame)
    //   directement ; le flux EnterWorld (host.LoadZoneResource idx=12) ne charge, lui, qu'un
    //   SoundBuffer throwaway côté WorldAssets. LoadZoneBgm fait Release AVANT reload (0x518bde).
    // TODO(zone-change en cours de partie) : un warp/MapWarp (Game/MapWarp.h) ne repasse PAS
    //   par Change(InGame) aujourd'hui (même limite que la géométrie .WO, cf. lignes ci-dessus).
    //   Quand le flux de warp sera câblé, il devra rappeler LoadZoneBgm(nouveauZoneId) pour
    //   recharger l'ambiance (World_LoadZoneResource case 12 est ré-appelée par zone dans le binaire).
    if (s == Scene::InGame) {
        LoadZoneBgm(game::g_World.zoneId);
    } else if (prev == Scene::InGame) {
        // Sortie du jeu (retour menu / déconnexion) : coupe l'ambiance de zone.
        //   SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers 0x6A80D0.
        ReleaseBgm();
    }
}

// --- Slot BGM de scène : chargement (enter-world/zone-change) + release (exit) ---
// Voir Audio/AudioSystem.h (BgmChannel) pour l'arbitrage complet des ancres IDA.
void SceneManager::LoadZoneBgm(int zoneId) {
    if (!bgm_) return;
    // World_LoadZoneResource 0x4DCB60 case 12 : Z = World_ZoneIdToFileId(zoneId) 0x4db0f0.
    //   fileId == -1 -> la zone n'a pas de BGM (le binaire saute le chargement, `if (v3 != -1)`
    //   @0x4dd406) : on coupe l'éventuel BGM précédent et on sort.
    const int fileId = world::WorldMap::ZoneIdToFileId(zoneId);
    if (fileId < 0) {
        TS2_LOG("SceneManager: zone %d sans fileId -> pas de BGM.", zoneId);
        ReleaseBgm();
        return;
    }
    // 0x4dd41d : chaîne .rdata "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM" (aG03GdataD10Wor_0 @0x7a7cc8).
    //   Le décodeur (OggVorbisLoadCallback via asset::ReadOggFile) attend un chemin résoluble
    //   -> on préfixe la racine GameData, comme World/WorldIntegration::LoadWorldBgm.
    char rel[64];
    std::snprintf(rel, sizeof(rel), "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM", fileId); // 0x4dd41d
    const std::string full = gameDataDir_.empty() ? std::string(rel)
                                                   : (gameDataDir_ + "\\" + rel);
    // g_BgmEnabled 0x84DEF0 (option f12) — gate du play (0x518c03 / 0x50f761). vol=100 en dur
    //   aux deux sites de play (0x518c14 / 0x50f76e) ; MusicVolume (option idx10) s'applique
    //   ailleurs (sons positionnels / UI), pas au play du slot BGM.
    const bool enabled = (config::g_Options.BgmEnabled == 1);
    if (bgm_->LoadAndPlay(full, enabled, 100)) {
        TS2_LOG("SceneManager: BGM zone %d (Z%03d.BGM) chargee%s.", zoneId, fileId,
                enabled ? " et jouee (boucle)" : " (option BGM off : silencieuse)");
    } else {
        // Guard exigée : .BGM absent / device audio indispo / décodeur Ogg absent -> muet,
        //   AUCUN crash (client silencieux pour cette zone, comme un DirectSound non dispo).
        TS2_WARN("SceneManager: BGM zone %d (Z%03d.BGM) indisponible "
                 "(fichier absent, audio non initialise ou decodeur Ogg absent).", zoneId, fileId);
    }
}

void SceneManager::ReleaseBgm() {
    // SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers(cSceneMgr+153) 0x6A80D0.
    if (bgm_) bgm_->Release();
}

// Rechargement RE-ENTRANT de zone (warp) — cf. declaration SceneManager.h. Rejoue la case 1
// de Scene_EnterWorldUpdate 0x52BFF0 (World_LoadZoneResource 0x4DCB60 idx 1..12) + rebuild
// GPU .WO + BGM, en LEVANT les gardes one-shot que Change() ne rejoue jamais en re-entree.
void SceneManager::ReloadZone(int zoneId) {
    // 1. Cle de zone courante : g_SelfMorphNpcId = g_TargetZoneId (Scene_EnterWorldUpdate
    //    0x52C173). Lue par World_LoadCurrentZoneModel 0x4DD6E0 (SetCurrentZoneId) ET
    //    cGameData_LoadZoneNpcInfo 0x5578E0 (via g_World.zoneId -> LoadZoneNpcs, spawn self).
    game::g_World.zoneId = zoneId;                        // g_SelfMorphNpcId 0x1675A98
    if (worldMap_) worldMap_->SetCurrentZoneId(zoneId);   // World_LoadCurrentZoneModel 0x4DD6E0

    // 2. Re-execute World_LoadZoneResource(zoneId, kind) pour kinds 1..12, fidele a la
    //    boucle case 1 (0x52C0F8 : idx 0..19, seuls 1..12 chargent, le reste no-op).
    //    Idempotent (WorldMap::LoadZoneResource recharge le meme fichier). // 0x4DCB60
    if (worldMap_) {
        for (int idx = 1; idx <= 12; ++idx)
            worldMap_->LoadZoneResource(zoneId, static_cast<world::ResourceKind>(idx)); // 0x4DCB60 case idx
    }

    // 3. Reconstruit la geometrie .WO GPU pour la nouvelle zone : LEVE la garde one-shot
    //    worldGeomReady_ (dans Change() le bloc rebuild est gate !worldGeomReady_ -> jamais
    //    rejoue en re-entree). ObjectsWO (kind 3) vient d'etre recharge dans la boucle ci-dessus
    //    -> worldAssets_ a jour. // 0x4DCB60 case 3 (ObjectsWO) + worldGeom_->Build
    if (worldGeomReady_ && worldGeom_ && worldAssets_) {
        worldGeom_->Build(*worldAssets_);
        TS2_LOG("SceneManager: rechargement zone %d -> geometrie .WO reconstruite "
                "(%zu parts GPU).", zoneId, worldGeom_->UploadedPartCount());
    }

    // 4. Recharge l'ambiance BGM de la nouvelle zone (LEVE la garde one-shot ; meme cycle
    //    release->load->play que World_LoadZoneResource 0x4DCB60 case 12). // 0x4DCB60 case 12
    LoadZoneBgm(zoneId);
}

void SceneManager::ConsumePending() {
    if (!login_) return;
    const Scene p = login_->PendingScene();
    if (p != Scene::None) {
        login_->ClearPending();
        Change(p);
    }
}

void SceneManager::Update(double dt, gfx::Camera& camera) {
    ++frameCount_;
    // H2 — op 0x18 Pkt_GameServerConnectResult 0x469CF0 pose g_SceneMgr=5 (@0x469d95, case 0 /
    // sous-resultat 0 ; confirme IDA : g_SceneSubState=0 @0x469d9f, dword_1676188=0 @0x469da9)
    // PENDANT InGame (reconnexion/relais serveur). Le handler reseau (Net/GameHandlers_Misc.cpp
    // op 0x18) arme game::g_World.sceneReloadPending, mais ce flag n'est lu QUE par la
    // case Scene::EnterWorld ci-dessous -> jamais atteint depuis InGame (reload mort). On
    // reproduit ICI le basculement scene 6->5 : le switch dispatchera alors sur case EnterWorld
    // LA MEME frame (comme le binaire ou g_SceneMgr==5 avant cSceneMgr_Update), laquelle
    // consommera le flag via sa branche sceneReloadPending existante (ReloadZone). NE PAS clear
    // le flag ici (la case EnterWorld le fera). // 0x469d95 / g_SceneMgr 0x1676180=5
    if (scene_ == Scene::InGame && game::g_World.sceneReloadPending) {
        Change(Scene::EnterWorld);
    }
    // SCN-01 — consommation des deux actions différées du bouton OK de la notice
    // (Notice_DispatchOkAction ci-dessus). Le binaire écrit g_SceneMgr/g_QuitFlag DIRECTEMENT
    // depuis UI_NoticeDlg_OnLButtonUp ; ici on diffère d'une frame pour ne pas changer de
    // scène pendant le routage du clic (UIManager itère son registre de dialogues à ce
    // moment-là). MÊME motif que sceneReloadPending ci-dessus / sceneEnterWorldPending.
    // La socket a DÉJÀ été fermée par Notice_DispatchOkAction, dans l'ordre du binaire
    // (Net_CloseSocket @0x5C04DF AVANT g_SceneMgr=2 @0x5C04E4).
    if (g_noticeReturnToServerSelectPending) {
        g_noticeReturnToServerSelectPending = false;
        Change(Scene::ServerSelect);   // g_SceneMgr = 2 @0x5C04E4 (+ g_SceneSubState=0 @0x5C04EE,
                                       // posé par Change) — dword_1676188=0 @0x5C04F8 = frameCount_
    }
    if (g_noticeAbnormalEndPending) {
        g_noticeAbnormalEndPending = false;
        // g_QuitFlag 0x815590 = 1 @0x5C051B. ClientSource ne réifie pas ce global : la sortie
        // passe par la boucle de messages (App/App.cpp WM_CLOSE/WM_DESTROY ->
        // PostQuitMessage @0x461BC3), qui est l'AUTRE sortie du binaire. Écart assumé et
        // documenté : même effet observable (arrêt du client), chemin différent.
        // TODO [ancre 0x815590] : réifier g_QuitFlag (miroir App::quit_) et le poser ICI.
        TS2_WARN("SceneManager: fin anormale demandee par la notice (type 3) -> PostQuitMessage.");
        ::PostQuitMessage(0);
    }
    switch (scene_) {
    case Scene::Intro:
        // Automate fidèle Scene_IntroUpdate 0x517FE0 (Game/IntroFlow.h) : 90 + 33×3 + 90
        // = 279 frames (9,3 s @ 30 FPS), PAS 90 frames comme l'ancien placeholder.
        if (game::UpdateIntro(introState_, 0.0f)) Change(Scene::ServerSelect);
        break;
    case Scene::ServerSelect:
        // UIFW-09 — Scene_ServerSelectUpdate 0x518B30 : le 2e des DEUX seuls sites de
        // UI_ResetAllDialogs 0x5AC3F0 (xrefs_to -> EXACTEMENT 2 : @0x518B79 ici et @0x52C038
        // côté EnterWorld, déjà câblé plus bas). Il n'était pas câblé : les dialogues restés
        // ouverts en quittant le jeu (ou après un échec de login) survivaient à l'arrivée sur
        // la sélection de serveur.
        // Structure d'origine, re-prouvée au désassemblage cette mission :
        //   518B3C `mov ecx,[eax+4]`          -> sous-état (champ +4 de cSceneMgr)
        //   518B42 sous-état==0 -> bloc Init ; ==1 -> boucle d'attente ; sinon sortie
        //   518B5D `[this+8] += 1`            -> compteur de frames (champ +8)
        //   518B69 `cmp [edx+8], 1Eh` / jnb   -> 30 frames avant d'exécuter le bloc
        //   518B79 call UI_ResetAllDialogs    <- LE SITE
        //   5191F0 `mov [ecx+4], 1` / 5191FA `mov [edx+8], 0` -> sous-état=1, compteur=0
        // subState_/frameCount_ SONT les miroirs de ces champs +4/+8 (SceneManager.h) ; ils
        // étaient jusqu'ici des FANTÔMES (écrits, jamais lus — grep) : ce bloc leur donne
        // enfin leur rôle d'origine pour la scène 2. frameCount_ est DÉJÀ incrémenté en tête
        // d'Update() (`++frameCount_`), miroir de @0x518B5D -> ne pas ré-incrémenter ici.
        // Sûr même registre vide (ResetAll itère dialogs_, cf. le commentaire du site EnterWorld).
        // Le RESTE du bloc d'origine est déjà couvert ailleurs et n'est PAS redupliqué ici :
        // Z000.BGM (@0x518BF7) + liste de serveurs + thread de statut -> UI/LoginScene.cpp:597-604
        // et LoginScene::ServerSelectUpdate ; UI_FocusEditBox(0) (@0x518BA5) -> LoginScene.
        // Restent non branchés, faute d'équivalent réifié (mêmes arbitrages que le bloc
        // EnterWorld plus bas) : WSndMgr_Free (@0x518B83), Gfx_ApplyOverlayBlendMode
        // (@0x518B8D), Util_SetClampedU8Field(mPOINTER,0) (@0x518B99), scratch 150 dw (@0x518BAA).
        // TODO [ancres 0x518B83 / 0x518B8D / 0x518B99 / 0x518BAA].
        if (subState_ == 0 && frameCount_ >= 30) {                       // @0x518B42 / @0x518B69 (1Eh)
            ui::UIManager::Instance().ResetAll();                        // 0x5AC3F0 @0x518B79
            subState_       = 1;                                         // `mov [ecx+4], 1` @0x5191F0
            frameCount_     = 0;                                         // `mov [edx+8], 0` @0x5191FA
            g_SceneSubState = 1;   // miroir du champ +4 // 0x1676184
        }
        if (login_) { login_->Update(scene_); ConsumePending(); }
        break;
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->Update(scene_); ConsumePending(); }
        break;
    case Scene::EnterWorld: {
        // (W2-F1) RECHARGEMENT RE-ENTRANT (warp / op 0x18 Pkt_GameServerConnectResult 0x469CF0,
        // seul writer de g_SceneMgr=5 @0x469d95 ; il a deja fait Change(Scene::EnterWorld) et
        // arme ces 2 champs). Teste EN PREMIER. g_TargetZoneId 0x1675A9C = pendingWarpZoneId.
        if (game::g_World.sceneReloadPending) {
            game::g_World.sceneReloadPending = false;
            const int warpZone = (game::g_World.pendingWarpZoneId >= 0)
                                     ? game::g_World.pendingWarpZoneId
                                     : game::g_World.zoneId;
            game::g_World.pendingWarpZoneId = -1;
            // Re-arme la machine d'etat visuelle (case 0..3) pour la NOUVELLE zone : sans ce
            // reset, enterWorldState_ resterait bloque sur WaitServerAck/Failed du cycle
            // precedent. Scene_EnterWorldUpdate repart de subState 0 (0x52C00F). // 0x52BFF0
            enterWorldState_ = game::EnterWorldFlowState{};
            subState_ = 0; frameCount_ = 0;
            g_SceneSubState = 0;   // miroir du champ +4 : rechargement = sous-état 0 // 0x1676184
            // Ecrit g_World.zoneId + SetCurrentZoneId (equivalent g_SelfMorphNpcId=g_TargetZoneId
            // @0x52C173) et rejoue LoadZoneResource(1..12) + rebuild geo/BGM. // 0x52BFF0 / 0x4DCB60
            ReloadZone(warpZone);
            break; // laisse le flux visuel se derouler des la frame suivante
        }
        // Bascule PRIORITAIRE et RÉELLE InGame : armée par EntityManager::OnEnterWorld
        // (Game/EntityManager.cpp, réception op 0x0c) via game::g_World.
        // sceneEnterWorldPending, fidèle à dword_1676180=6 écrit DIRECTEMENT par
        // Pkt_EnterWorld dans le binaire (cf. GameState.h et
        // Docs/TS2_ENTERWORLD_WIRING_TODO.md). Testé EN PREMIER, avant
        // EnterWorldFlow_Update ci-dessous (qui ne gère plus qu'un timeout de secours).
        if (game::g_World.sceneEnterWorldPending) {
            game::g_World.sceneEnterWorldPending = false;
            Change(Scene::InGame);
            break;
        }
        // Automate fidèle Scene_EnterWorldUpdate 0x52BFF0 (Game/EnterWorldFlow.h) :
        // attente(30) -> 20 ressources de zone espacées de 10 frames (~200 frames,
        // ~6,7s) -> attente(30) -> envoi requête -> attente ACK serveur (jusqu'à
        // 5000 frames). La bascule InGame réelle est déclenchée par la RÉCEPTION du
        // paquet serveur EnterWorld (op 0x0c, cf. ci-dessus) — ce flux ne sert plus
        // que de PROGRESSION VISUELLE (chargement des ressources de zone) + timeout
        // de secours si le serveur ne répond jamais.
        game::EnterWorldFlowHost host;
        host.ResetUiAndAudio = [this] {
            // Scene_EnterWorldUpdate 0x52BFF0, case 0 (WaitBeforeUnload), bloc gardé par
            // `if ((unsigned)*(this+2) >= 0x1E)` @0x52C02C (30 frames) : purge de l'UI et de
            // l'audio résiduels de CharSelect. L'ORDRE CI-DESSOUS EST CELUI DU BINAIRE
            // (dialogs -> curseur -> focus -> scratch -> son), vérifié au désassemblage
            // (0x52C033..0x52C089).

            // (1) UI_ResetAllDialogs(&dword_1821D4C) @0x52C038 — réel, câblé sur le même
            // UIManager que le reste du shell. Sûr même si windows_ n'est pas encore
            // construit (InGame pas encore atteint) : la liste de dialogues enregistrés est
            // alors vide (UIManager::ResetAll() itère dialogs_, vector par défaut vide).
            ui::UIManager::Instance().ResetAll();                          // 0x52C038

            // (2) Util_SetClampedU8Field(&dword_8E714C, 0) @0x52C044 — NON BRANCHÉ.
            // RECTIFICATION D'ÉTIQUETTE (prouvée dans l'IDB cette mission) : ce n'est PAS un
            // « reset tooltip », contrairement à ce qu'annoncent Game/EnterWorldFlow.h:91 et
            // Game/InGameTickFlow.h:128 (fichiers hors de ce front — à corriger ailleurs).
            // dword_8E714C EST mPOINTER, le jeu de curseurs souris. Chaîne de preuve :
            //   - App_Init @0x461F8B : `mov ecx, offset dword_8E714C` puis
            //     `call CursorSet_LoadResources 0x4C0FA0` @0x461F90 ; sur échec, le MessageBox
            //     empile la chaîne "[Error::mPOINTER.Init()]" (aErrorMpointerI @0x7A6BC0,
            //     push @0x461FA3) -> ce manager EST mPOINTER ;
            //   - CursorSet_LoadResources 0x4C0FA0 : `*this = 0` @0x4C0FAC (+0 = index actif),
            //     puis +1..+9 = 9 HCURSOR (LoadCursorA, ids 0x66..0x6C puis 0x75 et 0x77) ;
            //   - Cursor_AnimateTick 0x4C1140 : `SetCursor(*(this + *this + 1))` @0x4C115A
            //     -> *this EST bien l'index du curseur actif ;
            //   - Util_SetClampedU8Field 0x4C1110 : `if (a2 <= 8) *this = a2;` (borne ≤ 8 =
            //     exactement 9 slots). Le nom IDA est générique (157 appelants) ; appliqué à
            //     0x8E714C il n'a qu'un sens : l'index de curseur.
            // Donc @0x52C044 = « remettre la forme du curseur souris au slot 0 ».
            // L'équivalent fidèle EXISTE déjà : game::CursorSet::SetActiveSlot(0)
            // (Game/MiscManagers.cpp:88, miroir exact de 0x4C1110). Il n'est PAS appelable
            // ici : l'instance est App::cursors_, membre PRIVÉ de App (App/App.h:43), et
            // App.h/App.cpp ne sont pas possédés par ce front. L'appel serait de toute façon
            // un no-op prouvé aujourd'hui (seuls LoadResources/DestroyAll écrivent
            // CursorSet::state, tous deux à 0 ; SetActiveSlot n'a aucun appelant) — même
            // arbitrage que UI/LoginScene.cpp:809-813 pour ce même appel (EA 0x51A8FD).
            // TODO [ancre 0x52C044 / 0x4C1110] : exposer App::cursors_ (décision
            // orchestrateur, fichier non possédé) puis appeler CursorSet::SetActiveSlot(0) ICI.

            // (3) UI_FocusEditBox(&g_UIEditBoxMgr, 0) @0x52C050. Avec a2 = 0, l'original
            // (0x50F4A0) fait, sous `if (a2 < 0x16)` (22 slots) : `*this = 0` (index de
            // focus ; 0 = jeu, 1..21 = saisie active — cf. commentaire IDA sur
            // g_UIEditBoxMgr 0x1668FC0) PUIS, la branche `if (*this)` étant fausse,
            // `SetFocus(hWndParent)` @0x50F4CB : retirer le focus clavier de tout EDIT natif
            // et le rendre à la fenêtre de jeu.
            // ClientSource n'a aujourd'hui AUCUN EDIT natif vivant (ui::Win32EditBox existe
            // mais aucun fichier ne l'inclut) : la saisie texte in-game est le widget
            // custom-dessiné ChatWindow (flag focused_). Les deux volets sont donc rendus par :
            //   - index de focus = 0   -> Chat().Unfocus() (UI/ChatWindow.h:189) ;
            //   - SetFocus(hWndParent) -> notifyHwnd_ EST hWndParent 0x815184, PROUVÉ cette
            //     mission : App_Init @0x461C51 fait `mov ds:hWndParent, ecx` avec ecx = arg_4
            //     = le HWND créé par WinMain 0x4609C0 ; côté C++, App::Init passe ce même
            //     hwnd_ à SceneManager::Init (App/App.cpp:489) -> notifyHwnd_.
            //     Sans effet observable tant qu'aucun EDIT natif ne prend le focus, mais
            //     fidèle et immédiatement correct dès que Win32EditBox sera câblé.
            if (hud_) hud_->Chat().Unfocus();                              // 0x52C050 / 0x50F4BB
            if (notifyHwnd_) ::SetFocus(static_cast<HWND>(notifyHwnd_));   // 0x50F4CB

            // (4) scratch 150 dw @0x52C055 (`for (i=0;i<150;++i) *(this+i+3)=0;`, soit
            // +0xC..+0x260) — NON MODÉLISÉ, donc RIEN à remettre à zéro. SceneManager.h:42
            // documente bien « +12 tampon 150 dw » mais aucun membre ne le réifie, et ces
            // 150 dwords restent non identifiés dans le binaire (UI/LoginScene.cpp:811-814
            // n'en nomme que 3 — a1[3..5] = ses boutons ok/exit/opt — qui appartiennent à
            // LoginScene, PAS à la scène EnterWorld). Ne pas inventer de champ fantôme.
            // TODO [ancre 0x52C055] : identifier le tampon 150 dw de cSceneMgr +12.

            // (5) Snd_ReleaseBuffers(this + 153) @0x52C089 : `add ecx, 264h` -> slot
            // +0x264 = +612 = LE SLOT BGM DE SCÈNE, exactement celui de
            // SceneMgr_ReleaseSoundBuffers 0x517B60 (`return Snd_ReleaseBuffers(this + 153);`),
            // déjà réifié ici par bgm_ / ReleaseBgm(). Le binaire coupe le son AVANT de
            // spouler la zone (case 1).
            // RECTIFICATION du commentaire qui occupait ce bloc : il annonçait comme « reste
            // TODO » le release de la BANQUE DE SONS DE MONDE (WSndMgr_Free 0x4DB060) ; c'est
            // une confusion de slots — 0x52C089 vise +612 (bgm_), et WSndMgr_Free porte sur un
            // slot DISTINCT tenu par g_GameWorld, qui n'est pas appelé à cette EA.
            ReleaseBgm();                                                  // 0x52C089 / 0x6A80D0

            // ÉCART CONNU (hors mission, non introduit ici) : dans le binaire, la case 1
            // rechargera le BGM via World_LoadZoneResource 0x4DCB60 case 12 (~120 frames plus
            // tard). Côté C++, host.LoadZoneResource(idx=12) ne charge qu'un SoundBuffer
            // throwaway côté WorldAssets ; le vrai rechargement n'a lieu qu'à l'entrée InGame
            // (Change() -> LoadZoneBgm). Le silence dure donc un peu plus longtemps que dans
            // l'original, sans autre conséquence.
            // TODO [ancre 0x4DCB60 case 12] : faire appeler LoadZoneBgm(zoneId) par
            // host.LoadZoneResource quand idx==12 pour aligner exactement la fenêtre de silence.
        };
        host.LoadZoneResource = [this](int zoneId, int idx) {
            // World_LoadZoneResource 0x4DCB60 : idx EST directement world::ResourceKind
            // (cf. EnterWorldFlow.h, audit idx∈[1,12] réel, 0/[13,19] no-op fidèles —
            // le switch `default` d'origine ne fait rien pour ces valeurs).
            // worldMap_ est désormais construit UNE FOIS dans Init() (cf. plus haut),
            // PAS paresseusement à l'entrée InGame comme avant ce câblage, précisément
            // pour être déjà disponible ici (EnterWorld précède InGame dans le flux).
            if (!worldMap_) return; // gameDataDir_ vide à Init() — dégradation déjà journalisée
            if (idx < 1 || idx > 12) return; // no-op fidèle (switch `default` d'origine)
            const auto kind = static_cast<world::ResourceKind>(idx);
            const unsigned char ok = worldMap_->LoadZoneResource(zoneId, kind);
            if (!ok) {
                TS2_WARN("EnterWorld: World_LoadZoneResource(kind=%d, zone=%d) a echoue.", idx, zoneId);
            }
        };
        host.SendEnterWorldRequest = [this] {
            // Net_SendPacket_Op12 0x4B43C0 (opcode 12, 222 o) : bloc1 128 o (compte),
            // bloc2 13 o (nom du personnage), bloc3 72 o = record de spawn/téléport
            // confirmé (Net/CharSelectPackets.h::BuildEnterWorldTail72).
            uint8_t name13[13] = {};
            {
                const std::string& nm = game::g_World.self.localPlayerName;
                const size_t n = nm.size() < sizeof(name13) ? nm.size() : sizeof(name13);
                std::memcpy(name13, nm.data(), n);
            }
            uint8_t tail72[72] = {};
            net::BuildEnterWorldTail72(game::g_World.self.spawnX,
                                       game::g_World.self.spawnY,
                                       game::g_World.self.spawnZ,
                                       game::g_World.self.spawnRotationDeg,
                                       tail72);
            net::Net_SendPacket_Op12(net_->Client(), net::g_AccountName, name13, tail72);
            // NetSend() interne est fire-and-forget (void, cf. tous les autres builders
            // Net_Send* de ce fichier) : "true" est une fidélité partielle assumée, comme
            // host.SendKeepAlive plus bas dans le case Scene::InGame.
            return true;
        };
        host.ShowErrorNotice = [](int strId) {
            // UI_NoticeDlg_Open(byte_18225C8, 2, StrTable005_Get(g_LangId, strId), &String) :
            // strId 67 = echec emission Op12 (0x52C1A2), 68 = timeout ACK serveur (0x52C213).
            // Meme modele que host.ShowSpawnTimeoutNotice (InGame). // 0x5C0280
            game::g_Client.prompt.Open(2, game::Str(strId));
        };
        const int zoneId = game::g_World.zoneId;
        if (!game::EnterWorldFlow_Update(enterWorldState_, host, zoneId)) {
            // Etat Failed (timeout ACK 5000f @0x52C203 ou echec emission @0x52C194) : la notice
            // Str 67/68 a deja ete emise par host.ShowErrorNotice. NE PAS forcer InGame : le
            // binaire reste en scene 5 / etat 4 (default 0x52C232 = no-op). Le seul chemin
            // legitime vers InGame reste sceneEnterWorldPending (op 0x0c) teste en tete de ce case.
        }
        // Miroir du champ +4 de cSceneMgr pendant la scène EnterWorld : dans le binaire,
        // g_SceneSubState est LE MÊME champ (*(this+1)) pour Scene_EnterWorldUpdate 0x52BFF0
        // et Scene_InGameUpdate 0x52C600 — il porte donc ici l'état EnterWorld (0..4).
        // Non requis par la garde @0x50B7EC (elle court-circuite sur g_SceneMgr != 6), mais
        // maintenu pour que le miroir reste exact quelle que soit la scène. // 0x1676184
        g_SceneSubState = static_cast<int>(enterWorldState_.state);
        break;
    }
    case Scene::InGame: {
        // cSceneMgr_Update 0x517BF0 (case 6) : Scene_InGameUpdate() PUIS
        // AutoPlay_Update(g_AutoPlayBot) — dans cet ORDRE, à chaque frame InGame
        // (confirmé décompilation directe). Scene_InGameUpdate = InGameTickFlow_Update
        // (Game/InGameTickFlow.h, câblé ci-dessous) ; AutoPlay reste juste après, inchangé.
        //
        // Hooks câblés sur du code EXISTANT (réels, pas des TODO) :
        //   SendKeepAlive/SendPendingTargetPoll -> Net_SendPacket_Op13/Net_SendOp64 (Net/SendPackets.h)
        //   AppendKeepAliveFailedMessage    -> game::g_Client.msg.System(Str(70))
        //   ShowSpawnTimeoutNotice          -> game::g_Client.prompt.Open(2, Str(71)) (UI_NoticeDlg_Open)
        //   GetSelfActionState              -> g_World.players[0].body @kSelfActionStateOffset (Game/MapWarp.h)
        //   IsGm                            -> net::g_GmAuthLevel != 0 (Net/NetClient.h)
        //   IsExchangeWindowOpen            -> windows_->PlayerTrade().IsOpen() (Dialog::IsOpen)
        //   CanAutoInteractNpc/IsInventoryDirty/IsMorphInProgress
        //                                    -> windows_->AutoPlaySys().externalState.*
        // Hooks câblés sur les 4 systèmes Game/AnimationTick.h, Game/EntityLifecycleTick.h,
        // Game/CameraWarpTick.h, Game/ComboPickupTick.h (mission de câblage 2026-07-14, cf.
        // commentaires locaux à chaque hook ci-dessous pour le détail et les écarts/TODO
        // documentés) : TickWarpSuppressionTimeout, AutoUsePotion, UpdateLocalPlayerAnim,
        // UpdateEntityAnimFrame, DespawnStalePlayer, UpdateMonster/RespawnMonsterAfterKnockback,
        // TickNpcEffect/CleanupStaleNpcEffect, AutoInteractNpcForPet, UpdateQuestMarkerTimer,
        // FindComboFollowupTarget/BeginComboMorph, TickNearbyPickupSlots, RotateTipText.
        // Hooks câblés sur les 3 systèmes supplémentaires Game/GroundAuraWorldObjectTick.h,
        // Game/AutoTargetCombatGate.h, Gfx/CameraThirdPersonBridge.h (2e vague de câblage,
        // même date) : TickGroundItemEffect, GetFxAuraCount/IsFxAuraActive/
        // UpdateHomingProjectile, GetWorldObjectCount/IsWorldObjectActive/TickWorldObject,
        // ValidateAutoTarget, IsCombatAllowedOnMap ; InitCamera/UpdateCameraCollision
        // passent désormais par gfx::TickThirdPersonCamera (appelée juste après
        // InGameTickFlow_Update ci-dessous, hors host — cf. son commentaire local) grâce à
        // Update(dt, camera) qui reçoit enfin une gfx::Camera MUTABLE (SceneManager.h/
        // App.cpp étendus par cette même mission).
        // Reste TODO (aucune donnée/instance disponible dans ClientSource pour la nourrir) :
        // UpdateMapObjectAnim (aucun objet de collision de map animé modélisé).
        game::InGameTickFlowHost host;

        // --- Setup (case 0), one-shot ---------------------------------------------------
        // Scene_InGameUpdate 0x52C600 case 0 (jumptable @0x52C61F -> loc_52C626) : quadruplet
        // prouvé instruction par instruction, dans cet ordre exact.
        // ÉCART PROUVÉ vs Scene_EnterWorldUpdate 0x52BFF0 case 0 : la case 0 InGame ne contient
        // NI UI_ResetAllDialogs (@0x52C038, propre à EnterWorld) NI Snd_ReleaseBuffers(this+153)
        // (@0x52C089, idem). Ne PAS les ajouter ici : l'entrée en jeu ne coupe pas le BGM et ne
        // rabat pas les dialogues.
        host.ResetUiAndScratch = [this] {
            // (1) Gfx_ApplyOverlayBlendMode_SetState() @0x52C62B — NON BRANCHÉ.
            // L'original (0x53F630) fait exactement deux choses :
            //   Gfx_SetTextureBlendMode(g_GfxRenderer, 3, dword_7FFF78, 2) @0x53F646
            //   dword_8002C8 = 3 @0x53F64B  (état global de mode de blend d'overlay)
            // et 0x69DCA0 écrit les champs renderer +331/+332/+333 (+332 clampé sur +88) puis
            // enchaîne 4x SetTextureStageState (vtbl+276, stage 0, états 5/6/10/7).
            // NOTE (relevée cette mission) : le `mov ecx, offset unk_1685740` @0x52C626 qui
            // précède l'appel est DEAD — 0x53F630 est sans paramètre et n'utilise pas ecx (il
            // travaille sur g_GfxRenderer 0x7FFE18). Le commentaire Game/InGameTickFlow.h:128
            // (« sub_53F630(&unk_1685740) ») prête donc à cette fonction un argument qui
            // n'existe pas — fichier hors de ce front, signalé seulement.
            // Rien à appeler ici : aucun équivalent réifié dans ClientSource (ni dword_8002C8,
            // ni les champs renderer +331..+333), et Gfx/Renderer.h n'est pas possédé par ce
            // front. Ne pas confondre avec MeshRenderer::applyBlendMode (0x69DCA0 est un
            // chemin d'état d'overlay distinct, pas le blend par mesh).
            // TODO [ancre 0x52C62B / 0x53F630 / 0x69DCA0] : réifier l'état de blend d'overlay
            // (dword_8002C8 + renderer +331..+333) puis l'appeler ICI.

            // (2) Util_SetClampedU8Field(&dword_8E714C, 0) @0x52C637 — NON BRANCHÉ.
            // Strictement le même appel qu'@0x52C044 (case 0 d'EnterWorld) : voir la chaîne de
            // preuve complète plus haut dans ce fichier (dword_8E714C EST mPOINTER, le jeu de
            // curseurs ; 0x4C1110 = `if (a2 <= 8) *this = a2;` soit 9 slots) -> « remettre la
            // forme du curseur souris au slot 0 ». L'équivalent fidèle existe
            // (game::CursorSet::SetActiveSlot(0), Game/MiscManagers.cpp:88) mais l'instance est
            // App::cursors_, membre PRIVÉ de App (App/App.h:43), fichier non possédé -> même
            // arbitrage qu'@0x52C044.
            // TODO [ancre 0x52C637 / 0x4C1110] : exposer App::cursors_ (décision orchestrateur)
            // puis appeler CursorSet::SetActiveSlot(0) ICI.

            // (3) UI_FocusEditBox(&g_UIEditBoxMgr, 0) @0x52C643 — CÂBLÉ.
            // Appel STRICTEMENT identique à @0x52C050 (case 0 d'EnterWorld) : même fonction,
            // même a2 = 0 (`push 0` @0x52C63E). Re-vérifié cette mission sur 0x50F4A0 : sous
            // `if (a2 < 0x16)`, `*this = 0` @0x50F4BB puis, la branche `if (*this)` étant
            // fausse, `SetFocus(hWndParent)` @0x50F4CB. On réutilise donc le motif déjà prouvé
            // et câblé plus haut (index de focus -> Chat().Unfocus() ; SetFocus(hWndParent) ->
            // notifyHwnd_, qui EST hWndParent 0x815184).
            if (hud_) hud_->Chat().Unfocus();                              // 0x52C643 / 0x50F4BB
            if (notifyHwnd_) ::SetFocus(static_cast<HWND>(notifyHwnd_));   // 0x50F4CB

            // (4) scratch 150 dw @0x52C648 — NON MODÉLISÉ, donc RIEN à remettre à zéro.
            // `for (i=0; i<150; ++i) *(this+i+3) = 0;` (`cmp [ebp+var_8], 96h` @0x52C65A ;
            // `mov dword ptr [edx+ecx*4+0Ch], 0` @0x52C669), soit +0xC..+0x260 de cSceneMgr —
            // exactement le même tampon qu'@0x52C055 côté EnterWorld. SceneManager.h:56 le
            // documente mais aucun membre ne le réifie, et ces 150 dwords restent non
            // identifiés dans le binaire. Ne pas inventer de champ fantôme.
            // TODO [ancre 0x52C648] : identifier le tampon 150 dw de cSceneMgr +12.
        };

        // --- WaitFirstSpawn (case 1), timeout 5000 frames --------------------------------
        host.ShowSpawnTimeoutNotice = [] {
            // UI_NoticeDlg_Open(2, StrTable005_Get(g_LangId,71), "") 0x5C0280 — réel via
            // ClientRuntime::PromptState (même modèle que les autres prompts modaux).
            game::g_Client.prompt.Open(2, game::Str(71));
        };

        // --- InitCamera (case 3), one-shot -------------------------------------------------
        // Cam_SetLookAt/Camera_SetEyeTarget désormais câblés RÉELLEMENT via
        // gfx::TickThirdPersonCamera (Gfx/CameraThirdPersonBridge.h), appelée UNE FOIS par
        // frame juste après InGameTickFlow_Update ci-dessous (hors host : elle gère elle-même
        // le cadrage one-shot ET le suivi/collision chaque frame à partir du même flag
        // `justEnteredInGame`, cf. plus bas) — ce hook reste no-op pour éviter un double appel.
        host.InitCamera = [](float, float, float) {};

        // --- MainTick étape 1 : keepalive /300 frames + poll requête clan/faction ---------
        host.SendKeepAlive = [this]() -> bool {
            // Net_SendPacket_Op13(client, g_LocalElement) 0x4B4570. NetSend() intérieur est
            // best-effort (fire-and-forget) : les builders Net_Send* ne remontent PAS le
            // succès d'émission (void), donc "true" ici est une fidélité partielle assumée
            // (TODO : faire remonter le bool NetSend jusqu'ici si un jour nécessaire).
            net::Net_SendPacket_Op13(net_->Client(), static_cast<int8_t>(net::g_LocalElement));
            return true;
        };
        host.AppendKeepAliveFailedMessage = [] {
            game::g_Client.msg.System(game::Str(70)); // StrTable005 id 70
        };
        host.HasPendingTargetRequest = [] {
            const auto readReqName = [](uint32_t addr) {
                const auto& blob = game::g_Client.Blob(addr, 13);
                size_t len = 0;
                while (len < blob.size() && blob[len] != 0) ++len;
                return std::string(reinterpret_cast<const char*>(blob.data()), len);
            };
            return game::HasPendingTargetRequest(readReqName(0x167468A), readReqName(0x1674697));
        };
        host.SendPendingTargetPoll = [this] {
            net::Net_SendOp64(net_->Client()); // 0x4B9B20 — poll de requête clan/faction.
        };

        // --- MainTick étape 2 : timeout 10 s du flag "warp supprimé" ----------------------
        // Warp_TickSuppressionTimeout (Game/CameraWarpTick.h). Le hook reçoit directement
        // g_GameTimeSec depuis RunMainTick, puis synchronise ce latch avec
        // AutoPlayExternalState::warpSuppressed (MÊME global dword_1675B00, cf. tête de
        // Game/CameraWarpTick.h) : le site d'ARMEMENT réel (dword_1675B00=1) reste ailleurs
        // dans AutoPlaySystem (hors périmètre de ce câblage) — capturé ici au vol dès qu'il
        // est détecté, pour que l'auto-clear à 10 s (0x52C91F) reste fidèle.
        static game::WarpSuppressionState s_warpSuppression;
        host.TickWarpSuppressionTimeout = [this](float /*dt*/) {
            const float gameTimeSec = game::g_World.gameTimeSec;
            if (windowsReady_ && windows_) {
                auto& ext = windows_->AutoPlaySys().externalState;
                if (ext.warpSuppressed && !s_warpSuppression.suppressed) {
                    game::Warp_SetSuppressed(s_warpSuppression, gameTimeSec);
                }
                game::Warp_TickSuppressionTimeout(s_warpSuppression, gameTimeSec);
                ext.warpSuppressed = s_warpSuppression.suppressed; // repropage l'auto-clear
            } else {
                game::Warp_TickSuppressionTimeout(s_warpSuppression, gameTimeSec);
            }
        };

        // --- MainTick étapes 3-5 : anim/collision inconditionnelles chaque frame ----------
        // Game_AutoUsePotion (Game/CameraWarpTick.h). Câblage RÉEL des jauges/seuils/état
        // d'action (déjà modélisés dans GameState.h) ; la ceinture auto-play (3x14) et les
        // réglages de seuil UI n'existent encore nulle part dans ClientSource -> hooks
        // laissés nuls (dégradation propre : IsAutoPotionSystemEnabled==false par défaut,
        // donc la fonction ne consomme jamais de potion tant qu'un futur InventorySystem/
        // réglage AutoPlay ne les branche pas — cf. AutoPotionHost dans le header pour
        // chaque EA d'origine manquante).
        host.AutoUsePotion = [this](float /*dt*/) {
            game::AutoPotionHost potionHost;
            potionHost.GetHpGauge = [] { return static_cast<float>(game::g_World.self.hp); };
            potionHost.GetMpGauge = [] { return static_cast<float>(game::g_World.self.mp); };
            // ÉCART DOCUMENTÉ (cf. Game/CameraWarpTick.h::AutoPotionHost) : le binaire compare
            // bien HP/MP aux agrégats Char_CalcAttackRatingMin/Max, PAS à une capacité max
            // HP/MP — reproduit tel quel par fidélité.
            potionHost.GetHpThresholdMetric = [] { return static_cast<float>(game::g_World.self.atkRatingMin); };
            potionHost.GetMpThresholdMetric = [] { return static_cast<float>(game::g_World.self.atkRatingMax); };
            potionHost.GetSelfActionState = [] {
                if (game::g_World.players.empty()) return 0;
                const game::PlayerEntity& self0 = game::g_World.players[0];
                int32_t raw = 0;
                if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw)) {
                    std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
                }
                return static_cast<int>(raw);
            };
            if (windowsReady_ && windows_) {
                potionHost.IsMorphInProgress = [this] { return windows_->AutoPlaySys().externalState.morphInProgress; };
            }
            game::Game_AutoUsePotion(potionHost);
        };
        // MapColl_UpdateObjectAnim (Game/AnimationTick.h) : le "this" d'origine (objet de
        // collision de zone animé, MapCollisionObjectAnimState) n'a AUCUNE instance dans
        // ClientSource à ce jour — ni World/WorldMap.h ni Gfx/WorldGeometryRenderer.h
        // n'exposent de tableau de sous-objets/particules par objet de map. Câblage réel
        // impossible sans étendre ce système (hors périmètre de ce câblage) : TODO conservé.
        host.UpdateMapObjectAnim = [](float) {
            TS2_INGAME_TODO_ONCE("InGame: MapColl_UpdateObjectAnim non branche - aucune instance "
                                   "de MapCollisionObjectAnimState nulle part dans ClientSource "
                                   "(World/WorldMap.h n'expose pas d'objets de collision animes) "
                                   "(TODO EA 0x694A00).");
        };
        // Player_UpdateLocalAnim (Game/AnimationTick.h) : opère sur game::g_World (self),
        // reconstruit les ~80 timers de morph aux adresses d'origine via g_Client.Var/VarF.
        // LoadCurrentZoneModel câblé sur world::WorldMap::LoadCurrentZoneModel (déjà écrit,
        // instance possédée par SceneManager) ; IsPointOnGround/musique d'ambiance laissés
        // nuls (terrain/audio hors périmètre de ce câblage).
        host.UpdateLocalPlayerAnim = [this](float dt) {
            game::LocalAnimTickHost localHost;
            localHost.LoadCurrentZoneModel = [this](int reason) {
                if (worldMap_) worldMap_->LoadCurrentZoneModel(reason);
            };
            game::Player_UpdateLocalAnim(game::g_World, dt, nullptr, localHost);
        };
        // Char_UpdateAnimationFrame (Game/AnimationTick.h) : appelé pour l'entité 0 (soi,
        // étape 5) ET pour chaque joueur distant (étape 6, cf. Game/InGameTickFlow.cpp) via
        // le MÊME hook. isSelf/isLocalSimulation = (idx==0). GetPendingStopRequest/
        // ClearPendingStopRequest câblés sur g_PendingStopRequest (0xE0000072, MÊME variable
        // que Net/GameHandlers_Misc.cpp::kPendingStopReq) ; SendAutoPlayStopAck câblé sur
        // Net_SendOp95(pos_self, 2) (déjà déclaré, Net/SendPackets.h). Contact/interruption
        // de cast délégués à ActionFsm (déjà écrit) ; le switch terminal 55-handlers
        // (asset-driven, 0x5727BF) reste hors périmètre (stateHandler nul -> FSM gelée sur
        // son état courant au-delà de contact/interrupt/FX/rotation). Si un coup/compétence
        // instantané est validé pour SOI (contactFiredThisTick), le résultat est sérialisé et
        // envoyé via Net_SendPacket_Op18 (76o), complétant fidèlement Combat_QueueMeleeAttack/
        // Combat_QueueSkillAction (Game/CombatSystem.h, déjà écrit — réutilisé, pas dupliqué).
        host.UpdateEntityAnimFrame = [this](int idx, float dt) {
            if (idx < 0 || static_cast<size_t>(idx) >= game::g_World.players.size()) return;
            game::PlayerEntity& p = game::g_World.players[static_cast<size_t>(idx)];
            if (!p.active) return;
            const bool isSelf = (idx == 0);

            game::CombatActorState actor;
            actor.selfId = p.id;
            actor.x = p.x; actor.y = p.y; actor.z = p.z;
            actor.facing = p.anim.state; // entity+244, même offset que CharAnimState::state

            game::CharAnimTickHost animHost;
            animHost.GetPendingStopRequest = [] { return game::g_Client.Var(0xE0000072u) != 0; };
            animHost.ClearPendingStopRequest = [] { game::g_Client.Var(0xE0000072u) = 0; };
            animHost.SendAutoPlayStopAck = [this] {
                const game::PlayerEntity& self = game::g_World.Self();
                float pos[3] = { self.x, self.y, self.z };
                net::Net_SendOp95(net_->Client(), pos, 2);
            };

            game::CharAnimTickResult result;
            game::Char_UpdateAnimationFrame(p.anim, actor, game::g_World, nullptr,
                                             isSelf, isSelf,
                                             false /* pendingCastInterrupt: TODO g_AutoHuntFuelA/B non traces */,
                                             dt, nullptr, nullptr, animHost, result);

            // front F_PLAYERANIM : avance du curseur d'animation joueur (entity+248, field 62) =
            // idiome UNIVERSEL du switch terminal 0x5727BF (frame += dt*30 puis wrap par SOUSTRACTION,
            // Char_TickMoveState 0x574922), que la version C++ de Char_UpdateAnimationFrame ne fait pas
            // (stateHandler nul). frameCount du clip courant via l'oracle adossé au MÊME MotionCache que
            // le dessin ; race/genre = body+68/+72 (MÊME source que WorldRenderer), weaponType 0 (a8 de
            // PcModel_ResolveEquipSlot 0x4E46A0, switch ~500 cas non reversé). Player_AdvanceAnimCursor
            // arme lui-même le latch IsWired -> pas de figement (dégradation propre si non appelé).
            int animRace = 0, animGender = 0;
            std::memcpy(&animRace,   p.body.data() + 68, sizeof(animRace));
            std::memcpy(&animGender, p.body.data() + 72, sizeof(animGender));
            const int animFrameCount = ts2::WorldPlayerMotionFrameCount(animRace, animGender, 0, p.anim.state);
            game::Player_AdvanceAnimCursor(p.anim, dt, animFrameCount);

            if (isSelf && result.contactFiredThisTick) {
                uint8_t payload[76] = {};
                result.lastAction.Serialize(payload);
                net::Net_SendPacket_Op18(net_->Client(), payload);
            }
        };
        // Camera_UpdateCollision (Game/AnimationTick.h) : câblée RÉELLEMENT via
        // gfx::TickThirdPersonCamera, MÊME appel unique que host.InitCamera ci-dessus (cf. son
        // commentaire) — ce hook reste no-op pour éviter un double appel de
        // Camera_UpdateCollision sur la même frame.
        host.UpdateCameraCollision = [] {};

        // --- MainTick étape 6 : joueurs distants, péremption 7,5 s ------------------------
        // sub_55D720 (Game/EntityLifecycleTick.h) : désactive le slot périmé.
        host.DespawnStalePlayer = [](int idx, float) {
            game::DespawnStalePlayer(game::g_World, idx);
        };

        // --- MainTick étape 7 : tableau 88 o (GroundItem au sens GameState.h) -------------
        // Fx_MeleeSwingTick 0x5803A0 (Game/GroundAuraWorldObjectTick.h). GetWeaponEffectFrameCount
        // (Model_GetWeaponEffectFrameCount 0x4E5A40) laissé nul : aucune table de modèles/assets
        // d'effet d'arme câblée côté ClientSource à ce jour -> dégradation propre documentée
        // (le timer de frame avance mais ne boucle/complète jamais, cf. commentaire du header).
        host.TickGroundItemEffect = [](int index, float dt) {
            static const game::GroundAuraWorldObjectTickHost s_groundFxHost{}; // GetWeaponEffectFrameCount nul
            game::TickGroundItemEffect(game::g_World, index, dt, s_groundFxHost);
        };

        // --- MainTick étape 8 : monstres, péremption 7,5 s --------------------------------
        // Char_Update / sub_580550 (Game/EntityLifecycleTick.h). EntityLifecycleTickHost
        // partagé avec l'étape 9 ci-dessous. DispatchMotionTick + SpawnAttackProjectile(Alt)
        // sont désormais câblés ci-dessous (gx-fx-01, cette mission) ; les sous-hooks ENCORE
        // nuls (tables de fenêtre de coup Anim_IsFrameInHitListA/B 0x559F80/0x55A000, envoi
        // réseau melee Combat_SendMeleeHit1/2 0x5823E0/0x582480, hauteur de sol
        // MapColl_GetGroundHeight 0x697130, son d'impact Snd3D_PlayPositional 0x4DA450) restent
        // hors périmètre de ce front : dégradation propre documentée en tête de
        // Game/EntityLifecycleTick.h. NB : IsFrameInHitListA/B étant nuls, la fenêtre de coup
        // ne s'arme jamais (inWindow=false) — 2e verrou de latence du spawn projectile, en sus
        // de l'absence de producteur de motionState=5/7 (cf. câblage SpawnAttackProjectile).
        static game::EntityLifecycleTickHost s_lifecycleHost;

        // DispatchMotionTick — Char_Update 0x581E10, switch terminal @0x5822D3 (les 9 handlers
        // Char_MotionTick_* 0x582D40..0x5832E0). CE HOOK ÉTAIT LA CAUSE RACINE du gap
        // « s_lifecycleHost n'a aucun hook assigné » : game::Monster_DispatchMotionTick
        // (Game/AnimationTick.h §5) porte DÉJÀ fidèlement les 9 handlers et est appelé par
        // UpdateMonster (EntityLifecycleTick.cpp:153) via ce hook... qui n'était assigné NULLE
        // PART -> la FSM entière était du code mort et MonsterTickExt::motionState/animFrame ne
        // bougeaient jamais. Ce câblage la rend réellement atteinte (30 Hz, un appel par monstre
        // actif), ce que Scene/WorldRenderer.cpp détecte via game::Monster_MotionTickIsWired()
        // pour ne consommer le curseur par-entité (`ent.hasAnimCursor`) qu'une fois alimenté.
        // oracle = ts2::WorldMotionFrameCountOracle() (Scene/WorldRenderer.h, seul détenteur du
        // MotionCache) -> Model_GetMotionFrameCount 0x4E5A70, MÊME slot que le dessin.
        // StepTowardTarget (MapColl_StepTowardTarget 0x6974C0, états Move 3/4) laissé nul :
        // dégradation EXPLICITEMENT prescrite par AnimationTick.h — on saute le déplacement et
        // la transition « arrivé », on NE traite SURTOUT PAS « hook absent » comme
        // « échec -> state=1 » (le monstre ne marcherait jamais) ; le wrap de frame s'applique.
        // TODO [ancre MapColl_StepTowardTarget 0x6974C0] — exige la géométrie de collision de
        // carte + MONSTER_INFO+384/388, hors périmètre de ce front.
        s_lifecycleHost.DispatchMotionTick = [](int idx, float dt) {
            static const game::MonsterMotionTickHost s_motionHost{}; // StepTowardTarget nul (cf. supra)
            game::Monster_DispatchMotionTick(game::g_World, idx, dt,
                                              &WorldMotionFrameCountOracle(), s_motionHost);
        };

        // gx-fx-01 — SpawnAttackProjectile / SpawnAttackProjectileAlt : les DEUX DERNIERS slots
        // non assignés de s_lifecycleHost. Char_Update 0x581E10 les appelle (@0x5820A9 état 5 /
        // @0x58213D état 7) pour PEUPLER le pool de projectiles FX dword_17D06F4 via
        // Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10 — c'est LE cœur fonctionnel du gap
        // gx-fx-01 (« pool jamais peuplé »). Le port réel du pool + du spawn existe déjà
        // (game::Fx_SpawnAttackProjectile, Game/GroundAuraWorldObjectTick.cpp) mais n'avait
        // AUCUN appelant = code mort ; ce câblage lui donne son producteur documenté
        // (GroundAuraWorldObjectTick.h:204). Params reconstitués par BuildMonsterProjectileParams
        // (namespace anonyme en tête, offsets binaires re-prouvés en IDA cette mission).
        //
        // RÉSERVE HONNÊTE (re-prouvée en IDA + grep exhaustif cette mission) : le site d'appel est
        // gardé par `ext.motionState == 5/7 && ext.attackWindupMode == 1` puis `hitActionKind == 2`
        // (EntityLifecycleTick.cpp:80/103/108/117). Or AUCUN chemin de ClientSource ne produit
        // motionState=5/7, ni attackWindupMode=1, ni hitActionKind=2 : Monster_DispatchMotionTick
        // (câblé juste au-dessus) n'écrit QUE motionState=Loop(1) et attackWindupMode=0
        // (AnimationTick.cpp:703/744), et le vrai producteur est le handler réseau d'ordre
        // d'attaque monstre (HORS de ce front). Le spawn reste donc LATENT (jamais atteint
        // aujourd'hui) — mais fidèle et immédiatement fonctionnel dès qu'un producteur de
        // motionState=5/7 sera câblé. Même politique que host.GetFxAuraCount/UpdateHomingProjectile
        // plus bas (« câblage réel malgré tout », dégradation propre et sûre).
        s_lifecycleHost.SpawnAttackProjectile = [](int idx) {
            game::FxProjectileSpawnParams p;
            if (BuildMonsterProjectileParams(idx, p)) game::Fx_SpawnAttackProjectile(p);    // 0x582530 (état 5)
        };
        s_lifecycleHost.SpawnAttackProjectileAlt = [](int idx) {
            game::FxProjectileSpawnParams p;
            if (BuildMonsterProjectileParams(idx, p)) game::Fx_SpawnAttackProjectileAlt(p); // 0x582A10 (état 7)
        };

        // W9 — MapColl_GetGroundHeight 0x697130 : hauteur de sol sous (x,z). Consommé par
        // UpdateMonster (@0x58223E) et TickNpcEffect (@0x582263) via
        // Game/EntityLifecycleTick.cpp:141-145 pour reposer l'entité au sol après une chute /
        // un recul (knockback). Hook laissé NUL jusqu'ici -> AUCUN monstre ne retrouvait
        // jamais le sol après un knockback (chute infinie / altitude figée).
        // Le fournisseur EXISTE et est prêt : world::WorldAssets::GetGroundHeight
        // (World/WorldIntegration.h:133, portage byte-fidèle de 0x697130 sur la couche .WM),
        // dont le header prescrit NOMMÉMENT ce câblage (WorldIntegration.h:126-131 : « prêts à
        // câbler aux hooks consommateurs hors périmètre (host.GetGroundHeight
        // Game/EntityLifecycleTick.h:199) »). Signatures identiques :
        //   hook      : bool(float x, float z, float probeY,        float& outGroundY)
        //   fournisseur: bool(float x, float z, float probeCeilingY, float& outGroundY) const
        // worldAssets_ est un membre de SceneManager (déjà passé à gfx::TickThirdPersonCamera).
        // Build-safe : GetGroundHeight renvoie false si la couche .WM n'est pas chargée —
        // exactement la dégradation attendue par l'appelant (`if (!GetGroundHeight(...))`).
        // Capture `this` : s_lifecycleHost est STATIQUE mais SceneManager vit toute la session
        // (membre de App) et ce bloc est ré-exécuté à chaque frame InGame -> le lambda est
        // réassigné avec un `this` toujours valide.
        if (worldAssets_) {
            s_lifecycleHost.GetGroundHeight = [this](float x, float z, float probeY,
                                                     float& outGroundY) {
                return worldAssets_->GetGroundHeight(x, z, probeY, outGroundY); // 0x697130
            };
        }
        // Sous-hooks ENCORE nuls de s_lifecycleHost, faute de code appelable dans ClientSource
        // (vérifié par grep exhaustif cette mission — seules des MENTIONS en commentaire
        // existent) : Anim_IsFrameInHitListA/B 0x559F80/0x55A000 (fenêtre de coup),
        // Combat_SendMeleeHit1/2 0x5823E0/0x582480 (envoi melee), GetAuraSwapDuration,
        // IsAttackTargetBypassActive. Non câblables sans les porter d'abord (domaine
        // Game/SkillCombat, hors de ce front). TODO [ancres 0x559F80 / 0x55A000 / 0x5823E0 /
        // 0x582480].
        // Snd3D_PlayPositional 0x4DA450 (@0x5822B1, son du 1er atterrissage) : Audio/Sound3D.h:95
        // ::PlayPositional existe, mais l'objet son / slot .ISN visé par cette EA n'est identifié
        // NULLE PART -> pas de devinette (règle : aucun son plutôt qu'un son faux).
        // TODO [ancre 0x4DA450 / 0x5822B1] : identifier le slot .ISN d'atterrissage.

        host.UpdateMonster = [](int idx, float dt) {
            game::UpdateMonster(game::g_World, idx, dt, s_lifecycleHost);
        };
        host.RespawnMonsterAfterKnockback = [](int idx) {
            game::RespawnMonsterAfterKnockback(game::g_World, idx);
        };

        // --- MainTick étape 9 : tableau 152 o (NpcEntity au sens GameState.h) ------------
        // Fx_GibUpdate / sub_583390 (Game/EntityLifecycleTick.h), même host que ci-dessus.
        host.TickNpcEffect = [](int idx, float dt) {
            game::TickNpcEffect(game::g_World, idx, dt, s_lifecycleHost);
        };
        host.CleanupStaleNpcEffect = [](int idx) {
            game::CleanupStaleNpcEntity(game::g_World, idx);
        };

        // --- MainTick étape 10 : auras/homing (PAS dans GameState.h) ----------------------
        // g_FxAuraCount/dword_17D06F4 (Game/GroundAuraWorldObjectTick.h) : pool SoA de
        // projectiles d'attaque non modélisé côté ClientSource (cf. commentaire du header) ;
        // GetFxAuraCount lit le vrai slot via g_Client.Var (0 aujourd'hui, personne ne le
        // peuple encore) -> IsFxAuraActive/UpdateHomingProjectile jamais atteints en pratique,
        // dégradation propre et sûre (pas un stub figé, câblage réel malgré tout).
        host.GetFxAuraCount = [] { return game::GetFxAuraCount(); };
        host.IsFxAuraActive = [](int index) { return game::IsFxAuraActive(index); };
        host.UpdateHomingProjectile = [](int index, float dt) { game::UpdateHomingProjectile(index, dt); };

        // --- MainTick étape 11 : objets de monde (PAS dans GameState.h) -------------------
        // dword_1687230/dword_180EEF4 (Game/GroundAuraWorldObjectTick.h) : câblés sur
        // game::g_World.zoneObjects (déjà dimensionné à 500 par GameData_InitPools). TickWorldObject
        // (sub_584170) est un stub __stdcall VIDE confirmé par décompilation -> ne fait rien,
        // reproduit fidèlement (pas une supposition).
        host.GetWorldObjectCount = [] { return game::GetWorldObjectCount(game::g_World); };
        host.IsWorldObjectActive = [](int index) { return game::IsWorldObjectActive(game::g_World, index); };
        host.TickWorldObject = [](float dt) { game::TickWorldObject(dt); };

        // --- MainTick étape 12, porte de gating -------------------------------------------
        host.GetSelfActionState = [] {
            // g_SelfActionState[0] == g_World.players[0].body @+220 (== entity+244, cf.
            // Game/MapWarp.h::kSelfActionStateOffset, dérivation vérifiée depuis g_LocalPlayerSheet).
            if (game::g_World.players.empty()) return 0;
            const game::PlayerEntity& self0 = game::g_World.players[0];
            int32_t raw = 0;
            if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw)) {
                std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
            }
            return static_cast<int>(raw);
        };
        if (windowsReady_ && windows_) {
            host.IsExchangeWindowOpen = [this] { return windows_->PlayerTrade().IsOpen(); };
            // sub_53B9E0 : AutoPlayExternalState::sceneTransitionBlocking stocke déjà la
            // valeur BRUTE de sub_53B9E0 (cf. Game/AutoPlaySystem.h) — ce hook veut "true
            // quand sub_53B9E0 renvoie vrai", donc AUCUNE inversion ici (cf. avertissement
            // explicite dans Game/InGameTickFlow.h).
            host.CanAutoInteractNpc = [this] { return windows_->AutoPlaySys().externalState.sceneTransitionBlocking; };
            host.IsInventoryDirty   = [this] { return windows_->AutoPlaySys().externalState.invDirtyEnable; };
            host.IsMorphInProgress  = [this] { return windows_->AutoPlaySys().externalState.morphInProgress; };
        } else {
            host.IsExchangeWindowOpen = [] { return false; };
            host.CanAutoInteractNpc   = [] { return false; };
            host.IsInventoryDirty     = [] { return false; };
            host.IsMorphInProgress    = [] { return false; };
        }
        // Npc_AutoInteractForPet 0x53B5F0 : DÉJÀ porté par NpcInteractionSystem::
        // AutoInteractForPet() (Game/NpcInteraction.h, réutilisé tel quel, cf. rapport de
        // mission combo_pickup_quest). `selectedItemId` (g_SelectedInvItemId 0x1673258) n'est
        // tracé nulle part dans ClientSource à ce jour -> 0 fixe (garde interne
        // `if (selectedItemId < 1) return;` -> no-op fidèle et sûr, TODO le vrai câblage
        // le jour où la sélection d'item d'inventaire existe côté UI).
        static game::NpcInteractionSystem s_npcInteract;
        host.AutoInteractNpcForPet = [this] {
            if (windowsReady_ && windows_) {
                s_npcInteract.morphInProgress = windows_->AutoPlaySys().externalState.morphInProgress;
            }
            s_npcInteract.AutoInteractForPet(0 /* TODO g_SelectedInvItemId non trace */,
                                              game::g_World.gameTimeSec);
        };

        // --- MainTick étape 12a ------------------------------------------------------------
        // ValidateAutoTarget (Game/AutoTargetCombatGate.h) : câblé sur game::g_World, oracle de
        // portée par défaut (rangedLookup nul -> AutoTarget_DefaultRangeLookup) : mode 7 =
        // g_World.zoneObjects ; mode 4 = g_World.groundItems (même tableau que
        // g_NpcRenderArray/dword_1764D14, cf. commentaire du header AutoTargetCombatGate.h) —
        // les deux modes résolvent désormais une vraie position, plus de repli systématique.
        host.ValidateAutoTarget = [] { game::ValidateAutoTarget(game::g_World); };

        // --- MainTick étape 12b ------------------------------------------------------------
        // Quest_UpdateMarkerTimer (Game/ComboPickupTick.h), réutilise game::g_QuestProgress
        // (déjà porté, Game/QuestSystem.h). isArenaZone (Map_IsArenaZone 0x54B690) non
        // modélisé -> false fixe (TODO) ; fenêtre entrepôt/son de marqueur laissés nuls (UI/
        // audio hors périmètre de ce câblage).
        static game::QuestMarkerState s_questMarker;
        host.UpdateQuestMarkerTimer = [] {
            game::Quest_UpdateMarkerTimer(s_questMarker, game::g_QuestProgress,
                                           game::g_World.gameTimeSec,
                                           false /* isArenaZone: TODO Map_IsArenaZone non modelise */,
                                           nullptr, nullptr);
        };

        // --- MainTick étape 12c -------------------------------------------------------------
        // Combo_FindNearbyFollowup (Game/ComboPickupTick.h), site d'appel @0x52CEA9 :
        //   `mov ecx, offset flt_1555D08`   -> this = table GINFO-003 (game::LoadedCoordTable())
        //   `mov edx, ds:g_SelfMorphNpcId`  -> a2   = motionId courant          @0x52CE9D
        //   `push offset flt_1687330`       -> a3   = position du joueur local  @0x52CE98
        // GINFO-003-SUBTABLES : `candidates` lit désormais RÉELLEMENT la sous-table A de la
        // ligne (row[3]/row[4+3i]/row[304+i]) — ce lambda EST atteint : InGameTickFlow étape 12c
        // l'appelle toutes les 30 frames (garde `% 30` @0x52CE8E), et motionId est maintenant la
        // vraie valeur de g_SelfMorphNpcId (le « TODO non tracé » précédent était PÉRIMÉ :
        // l'adresse est tracée par game::WarpAddr::SelfMorphNpcId et déjà lue par une dizaine
        // de consommateurs via g_Client.VarGet).
        // `transitionCheck` reste nul : Combo_CheckTransition 0x4FD650 (validateur géant, ~250
        // paires + ~25 globals non tracés + g_MotionFrameRangeTable/SkillLevelTable_GetMin/Max)
        // appartient au domaine SkillCombat et n'est porté nulle part dans ClientSource -> 0
        // constant, donc aucun candidat ne franchit la garde `== 1` @0x501337 et le résultat
        // reste -1. La sous-table A est bien LUE (gap levé) mais la SÉLECTION finale reste
        // latente. TODO [ancre Combo_CheckTransition 0x4FD650] — hors périmètre de ce front.
        host.FindComboFollowupTarget = [] {
            const game::PlayerEntity& self = game::g_World.Self();
            const int motionId = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // @0x52CE9D
            return game::Combo_FindNearbyFollowup(motionId, self.x, self.y, self.z,
                                                   GInfo2ComboCandidates, nullptr);
        };
        // host.IsMorphInProgress est DÉJÀ câblé réellement plus haut (porte de gating étape 12,
        // même champ réutilisé ici par le binaire pour la gate combo étape 12c) — pas de
        // réaffectation ici.
        // BeginComboMorph (Game/ComboPickupTick.h) : port fidèle complet (phase=4, reset des
        // 72 o, rotation aléatoire via net::DefaultRng(), Net_SendPacket_Op20).
        // GINFO-003-SUBTABLES : `originLookup` lit désormais RÉELLEMENT la sous-table B
        // (row[404]/row[705+i]/row[405+3i]) via GInfo2_FindVec3ByKey 0x4FD540, appelée au site
        // @0x52CF30 avec a2 = followupMotionId (var_4) et a3 = g_SelfMorphNpcId (@0x52CF20) —
        // d'où `currentMotionId` = g_SelfMorphNpcId (l'ancien « TODO non tracé » était PÉRIMÉ,
        // cf. étape 12c ci-dessus).
        // RÉSERVE HONNÊTE : ce lambda n'est atteint que si FindComboFollowupTarget renvoie
        // != -1, ce qui exige Combo_CheckTransition 0x4FD650 (non porté, cf. étape 12c) —
        // la sous-table B est donc câblée fidèlement mais reste LATENTE tant que 0x4FD650
        // n'est pas porté. TODO [ancre Combo_CheckTransition 0x4FD650].
        static game::ComboMorphState s_comboMorph;
        host.BeginComboMorph = [this](int followupTargetId) {
            const int currentMotionId = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // @0x52CF20
            game::BeginComboMorph(s_comboMorph, followupTargetId, currentMotionId,
                                   net_->Client(), GInfo2ComboOrigin);
            if (windowsReady_ && windows_) {
                windows_->AutoPlaySys().externalState.morphInProgress = s_comboMorph.inProgress;
            }
        };

        // --- MainTick étape 12d -------------------------------------------------------------
        // Combat_IsElementAllowedOnMap (Game/AutoTargetCombatGate.h::IsCombatAllowedOnMapForSelf,
        // wrapper direct, déjà porté par Game/ComboPickupTick.h) : câblé sur world.self.element
        // (g_LocalElement) + g_SelfMorphNpcId (g_Client.VarGet). ElementPairTable
        // (g_LocalPlayerSheet+455..458) reste non modélisée dans ClientSource -> repli {} (4x -1,
        // "aucune paire enregistrée", PAS un raccourci "toujours faux" : le résultat dépend
        // toujours réellement de selfMorphNpcId, cf. commentaire du header).
        host.IsCombatAllowedOnMap = [] { return game::IsCombatAllowedOnMapForSelf(game::g_World); };
        host.IsGm = [] { return net::g_GmAuthLevel != 0; };
        // TickNearbyPickupSlots (Game/ComboPickupTick.h) : les 5 emplacements flt_1676130 sont
        // déjà alimentés par le handler du paquet 0x82 (Net/GameHandlers_Misc.cpp) via
        // g_Client.VarF — ce câblage réutilise le MÊME stockage (aucune duplication). Ne
        // s'exécute que si IsCombatAllowedOnMap ci-dessus renvoie un jour true (fidèle).
        host.TickNearbyPickupSlots = [this] {
            const game::PlayerEntity& self = game::g_World.Self();
            game::TickNearbyPickupSlots(self.x, self.y, self.z, net_->Client());
        };

        // --- MainTick étape 12e -------------------------------------------------------------
        // Tips_RotateUpdate (Game/ComboPickupTick.h) : réutilise game::g_Strings.notices
        // (TipsTable déjà porté, Game/StringTables.h) — le timer/index (600 s) y est déjà
        // tenu fidèlement ; ce câblage n'ajoute que l'appel manquant à l'append chat.
        host.RotateTipText = [] {
            game::Tips_RotateUpdate(game::g_Strings.notices, game::g_World.gameTimeSec);
        };

        // Détection "entrée en InGame" pour gfx::TickThirdPersonCamera ci-dessous : capturée
        // AVANT InGameTickFlow_Update, car l'état de la machine (inGameTickState_) transite
        // DURANT cet appel (InitCamera -> MainTick, one-shot, cf. Game/InGameTickFlow.cpp) —
        // c'est exactement la même frame où le binaire d'origine exécute son case 3
        // (Cam_SetLookAt/Camera_SetEyeTarget, EA 0x52C6EF).
        const bool justEnteredInGame = (inGameTickState_.state == game::InGameTickState::InitCamera);

        game::InGameTickFlow_Update(inGameTickState_, host, static_cast<float>(dt));

        // Miroir du champ +4 de cSceneMgr (g_SceneSubState 0x1676184) pour la scène InGame :
        // dans le binaire, Scene_InGameUpdate 0x52C600 écrit ce champ LUI-MÊME au fil de ses
        // transitions (ex. sub=4 posé @0x52C7F1 en fin de case 3 InitCamera, sub=3 posé
        // directement par Pkt_SpawnCharacter @0x464901). inGameTickState_.state EN EST le
        // modèle 1:1 (game::InGameTickState, valeurs alignées sur les cases d'origine :
        // Setup=0/WaitFirstSpawn=1/Failed=2/InitCamera=3/MainTick=4) -> on le recopie juste
        // APRÈS l'appel, à l'instant où le binaire a fini d'écrire le champ pour cette frame.
        // Consommé par la garde de Camera_UpdateFromInput @0x50B7EC (App/PlayerInputController).
        // Ordonnancement : App::FrameTick appelle g_playerInput.Update (App.cpp:643) AVANT la
        // boucle scene_.Update (App.cpp:656) — exactement comme le binaire appelle
        // Camera_UpdateFromInput @0x462619 avant cSceneMgr_Update @0x46263B. Le contrôleur lit
        // donc le sous-état produit par la frame PRÉCÉDENTE : même décalage d'une frame que
        // l'original. // 0x1676184
        g_SceneSubState = static_cast<int>(inGameTickState_.state);

        // M2 — MapColl_UpdateObjectAnim 0x694A00, appelee 1x/frame depuis Scene_InGameUpdate
        // 0x52C600 @0x52c94b (xref UNIQUE confirmee IDA). Cote ClientSource, WorldGeometryRenderer::
        // TickWorldAnim est l'equivalent documente (WorldGeometryRenderer.h:271-277, kAnimFps=15.0) :
        // seul writer de wavePhase_ (matrice bump-env eau) + phase de flipbook de sway par instance
        // .WO. Jamais appele avant ce cablage -> eau/sway figes. Garde one-shot worldGeomReady_
        // (device chaud requis, meme garde que le rendu .WO). // 0x694A00
        if (worldGeomReady_ && worldGeom_)
            worldGeom_->TickWorldAnim(static_cast<float>(dt));

        // (Vague D — FX combat) Tick des slots particule (types 5/6/7 = muzzle/hitspark/hitburst) :
        // miroir de la boucle d'update de Scene_InGameUpdate 0x52C600 (Particle_EnsureLoadedThenUpdateEmit
        // 0x4D9F40 -> Particle_UpdateEmit 0x6A7530). POSITION d'émission : le binaire suit l'os via
        // slot[30]=flt_FABB5C (système d'os non porté) ; approximation FIDÈLE = position de l'entité
        // source (elle suit l'entité frame par frame comme l'os), résolue par l'id réseau stocké dans le
        // slot (idHi@+0xC / idLo@+0x10, écrits par les setters Fx_Attach*, cf. Gfx/FxSetters.cpp). Si
        // l'entité a disparu -> pas d'émission (aucune particule à l'origine du monde) : dégradation honnête.
        for (int i = 0; i < gfx::FxPool_Count(); ++i) {
            gfx::FxSlot& fx = gfx::FxPool_Slots()[i];
            if (!fx.state || (fx.type != 5 && fx.type != 6 && fx.type != 7)) continue;
            const uint32_t* rawId = reinterpret_cast<const uint32_t*>(&fx);
            const uint32_t idHi = rawId[3], idLo = rawId[4]; // slot[3]=+0xC, slot[4]=+0x10
            float epos[3] = { 0.0f, 0.0f, 0.0f };
            bool found = false;
            for (size_t k = 0; !found && k < game::g_World.players.size(); ++k) {
                const game::PlayerEntity& p = game::g_World.players[k];
                if (p.active && p.id.hi == idHi && p.id.lo == idLo) { epos[0]=p.x; epos[1]=p.y; epos[2]=p.z; found=true; }
            }
            for (size_t k = 0; !found && k < game::g_World.monsters.size(); ++k) {
                const game::MonsterEntity& m = game::g_World.monsters[k];
                if (m.active && m.id.hi == idHi && m.id.lo == idLo) { epos[0]=m.x; epos[1]=m.y; epos[2]=m.z; found=true; }
            }
            if (!found) continue;
            const float erot[3] = { 0.0f, 0.0f, 0.0f };
            gfx::FxBillboard_PoolTick(reinterpret_cast<gfx::FxParticlePool*>(fx.ptclPool),
                                      fx.ptclDefIndex, static_cast<float>(dt), epos, erot, nullptr);
        }

        // (Vague F — FX combat mesh) Tick des slots MESH (types 8/9/10 = block/parry/deflect ; 0xC/0xD
        // = 12/13 routés AUSSI vers la banque MiscC par Fx_EmitterDraw 0x585E30). Trois rôles :
        //  (a) POSITION : le binaire place le mesh sur l'os d'arme (Model_GetAttachTransform 0x40FDC0,
        //      non porté) ; approximation FIDÈLE = centre de l'entité source (résolu par idHi/idLo, MÊME
        //      pattern que le tick particule) ; slot.orient (+0x50) laissé à 0 (aucune transformée d'os inventée).
        //  (b) FLIPBOOK : drawParam (+0x40) = index de frame (prouvé Vague F, 30 fps) -> += dt*30.
        //  (c) RECYCLAGE (correctif audit Vague F) : quand le flipbook est terminé (frame >= frameCount du
        //      .MOBJECT), libérer le slot (Fx_AttachSlotClear 0x584220) -> SANS ça, câbler s_meshDraw
        //      laisserait le mesh affiché EN PERMANENCE (régression) + fuite de pool. Reconstruction fidèle
        //      d'un effet one-shot ; TODO(ancre) confirmer la condition exacte (hold/loop/clear) par dump dynamique.
        if (modelObjRenderer_) {
            for (int i = 0; i < gfx::FxPool_Count(); ++i) {
                gfx::FxSlot& mfx = gfx::FxPool_Slots()[i];
                if (!mfx.state) continue;
                if (mfx.type != 8 && mfx.type != 9 && mfx.type != 10 && mfx.type != 12 && mfx.type != 13) continue;
                const uint32_t* mid = reinterpret_cast<const uint32_t*>(&mfx);
                const uint32_t idHi = mid[3], idLo = mid[4]; // slot[3]=+0xC, slot[4]=+0x10
                bool mfound = false;
                for (size_t k = 0; !mfound && k < game::g_World.players.size(); ++k) {
                    const game::PlayerEntity& p = game::g_World.players[k];
                    if (p.active && p.id.hi==idHi && p.id.lo==idLo) { mfx.position[0]=p.x; mfx.position[1]=p.y; mfx.position[2]=p.z; mfound=true; }
                }
                for (size_t k = 0; !mfound && k < game::g_World.monsters.size(); ++k) {
                    const game::MonsterEntity& m = game::g_World.monsters[k];
                    if (m.active && m.id.hi==idHi && m.id.lo==idLo) { mfx.position[0]=m.x; mfx.position[1]=m.y; mfx.position[2]=m.z; mfound=true; }
                }
                mfx.drawParam += static_cast<float>(dt) * 30.0f;                 // avance flipbook
                const uint32_t fc = modelObjRenderer_->FrameCount(mfx.meshIdxC); // idxC banque MiscC
                if (fc > 0 && mfx.drawParam >= static_cast<float>(fc))
                    gfx::Fx_AttachSlotClear(&mfx);                               // one-shot terminé -> recycle
            }
        }

        // W9 — Npc_RenderSlotTick 0x5803A0 : anim des PNJ de DÉCOR (mZONENPCINFO). Appelée
        // 1x/frame et par slot actif depuis Scene_InGameUpdate 0x52C600 @0x52CA4C (xref
        // UNIQUE, confirmée IDA cette mission). Boucle d'origine @0x52CA19 : `i < g_NpcCount
        // (0x1687220)`, stride 88 (`imul edx, 58h` @0x52CA27), garde `slot+4 != 0` (actif,
        // @0x52CA2A) sur g_NpcRenderArray 0x1764D14 — game::ZoneNpc_TickAnim
        // (Game/AnimationTick.cpp:859) porte fidèlement cette boucle ENTIÈRE (garde + dispatch
        // mode 0/1), d'où l'appel unique ici plutôt qu'une boucle recopiée.
        // Ce hook n'avait AUCUN appelant -> frameAcc restait 0 et TOUS les PNJ de décor étaient
        // figés sur le repli horloge globale : Scene/WorldRenderer.cpp:809
        // (`ent.hasAnimCursor = game::ZoneNpc_AnimTickIsWired()`) le détecte et ne consomme le
        // curseur par-entité qu'une fois ce tick réellement atteint. Site prescrit nommément
        // par Game/AnimationTick.h:456-460.
        // ORDRE : le binaire exécute MapColl_UpdateObjectAnim @0x52C94B AVANT Npc_RenderSlotTick
        // @0x52CA4C -> cet appel reste APRÈS TickWorldAnim ci-dessus.
        // oracle = ts2::WorldMotionFrameCountOracle() (Scene/WorldRenderer.h, seul détenteur du
        // MotionCache -> Model_GetMotionFrameCount 0x4E5A70), MÊME instance que le dispatch de
        // motion des monstres plus haut. // 0x52CA4C / 0x5803A0
        game::ZoneNpc_TickAnim(static_cast<float>(dt), &WorldMotionFrameCountOracle());

        // InGame_InitCamera (one-shot, si justEnteredInGame) + Camera_UpdateCollision (chaque
        // frame) : câblage RÉEL via gfx::TickThirdPersonCamera (Gfx/CameraThirdPersonBridge.h),
        // APRÈS la mise à jour de la position du joueur local pour cette frame (host.
        // UpdateEntityAnimFrame(0,...) déjà exécuté par InGameTickFlow_Update ci-dessus) —
        // remplace host.InitCamera/host.UpdateCameraCollision (laissés no-op plus haut).
        gfx::TickThirdPersonCamera(camera, game::g_World, static_cast<float>(dt), justEnteredInGame,
                                   worldAssets_.get()); // WG-02 : oracle collision terrain réel (0x69a1f0/0x540da0)

        // AutoPlay_Update(g_AutoPlayBot) — TOUJOURS après Scene_InGameUpdate, cf. commentaire
        // en tête de bloc et Game/InGameTickFlow.h (note d'intégration en bas de fichier).
        if (windowsReady_ && windows_) windows_->UpdateAutoPlay(static_cast<float>(dt));
        break;
    }
    default: break;
    }
}

void SceneManager::Render(IDirect3DDevice9* /*device*/, const gfx::Camera& camera) {
    switch (scene_) {
    case Scene::Intro:
        // Scene_IntroRender 0x518880 (UI/IntroRender.h) : logos défilés depuis les VRAIS
        // fichiers 001_00799..831.IMG (atlas UI), centrés sur leur taille réelle. Délégué à
        // LoginScene qui réutilise ses ressources GPU déjà créées (cf. LoginScene::RenderIntro) —
        // introState_ reste intégralement piloté ici (Update, cas Scene::Intro).
        if (login_) login_->RenderIntro(introState_);
        break;
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) login_->Render(scene_);
        break;
    case Scene::EnterWorld:
        // Scene_EnterWorldRender 0x52C260 (UI/EnterWorldRender.h) : écran de transition
        // CharSelect->InGame (fond de zone 008_%05d.IMG + barre de progression). Délégué à
        // LoginScene (mêmes ressources GPU que Scene::Intro ci-dessus). Sans ce case, la
        // scène EnterWorld tombait dans `default:` -> écran noir pendant tout le chargement.
        // enterWorldState_ reste piloté par SceneManager::Update() (case Scene::EnterWorld) ;
        // zoneId = game::g_World.zoneId (même valeur qu'EnterWorldFlow_Update).
        if (login_) login_->RenderEnterWorld(enterWorldState_, game::g_World.zoneId);
        break;
    case Scene::InGame:
        // ORDRE CORRIGÉ : la couche SilverLining minimale est appelée à deux moments,
        // comme le binaire d'origine :
        //   1) avant le décor/terrain (Env_UpdateFrame -> cAtmosphere_RenderFrame),
        //   2) après les entités (Env_StepTimeOfDay -> Atmosphere_DrawFrame).
        // Ici on l'applique autour du rendu monde: ciel -> décor .WO -> entités -> ciel
        // de fin -> HUD -> fenêtres.
        if (worldGeomReady_ && worldGeom_) worldGeom_->RenderSky(screenW_, screenH_);
        if (worldGeomReady_ && worldGeom_) worldGeom_->Render(camera, screenW_, screenH_);
        if (worldReady_ && world_) world_->Render(camera);
        if (worldGeomReady_ && worldGeom_) worldGeom_->RenderSky(screenW_, screenH_);
        // (GXD_RenderPostBlur 0x4053E0) Bloom/post-blur : appel UNIQUE et INCONDITIONNEL du
        // binaire @0x52FB53 (Scene_InGameRender 0x52D0B0), APRÈS tout le rendu 3D (terrain +
        // entités + ciel) et AVANT Gfx_Begin2D @0x52FB89. Le flag bloomEnabled (g_GxdRenderer+24,
        // @0x4053ED) vaut 1 par défaut (GXD_InitGlobalState 0x401320) -> bloom actif. RenderPostBlur
        // s'auto-garde (device/PS12/PS14/handles nuls -> no-op) et gère lui-même son EndScene/
        // BeginScene, laissant la scène OUVERTE pour le HUD 2D ci-dessous. Les PS12/PS14 du npk
        // GXDEffect proviennent du ShaderSet du WorldRenderer (chargé quand worldReady_). Le device
        // du singleton GxdRenderer est attaché à App_Init (App.cpp:374, GXD_DeviceReinit 0x4023F0).
        // (Vague D — FX combat) Rendu des slots FX : ancre de frame (droite/haut caméra depuis la
        // matrice vue) puis 3 passes Fx_EmitterDraw (miroir Scene_InGameRender 0x52D0B0 : passes 1/2 =
        // meshes block/parry/deflect via ModelObjectRenderer (Vague F, s_meshDraw câblé), passe 3 =
        // particules -> leaf Object A Particle_RenderBillboards
        // 0x6A70B0). AVANT le bloom pour que muzzle/étincelles participent au post-blur, comme dans le
        // binaire (sites 0x52DD14/0x52ECEB/0x52FAD8 précèdent GXD_RenderPostBlur @0x52FB53).
        if (worldReady_ && world_) {
            IDirect3DDevice9* fxDev = renderer_->Device();
            D3DXMATRIX fxView; camera.BuildViewMatrix(fxView);
            D3DXMATRIX fxProj; camera.BuildProjMatrix(fxProj,
                screenH_ ? static_cast<float>(screenW_) / static_cast<float>(screenH_) : 1.0f);
            const float fxRight[3] = { fxView._11, fxView._21, fxView._31 }; // droite caméra en monde
            const float fxUp[3]    = { fxView._12, fxView._22, fxView._32 }; // haut caméra en monde
            // (Gfx_BeginUnlitPass 0x69E470) État pipeline billboard OBLIGATOIRE avant Fx_EmitterDraw :
            // le binaire le pose @0x52FA77 juste avant la passe 3 (Fx_EmitterDraw 0x585E30 ne pose AUCUN
            // render-state, il ne fait que SetTexture+DrawPrimitiveUP). Décompilé byte-exact de 0x69E470 :
            // LIGHTING(137)=0, ZWRITEENABLE(14)=0, ALPHABLENDENABLE(27)=1, TSS0 ALPHAOP(4)=MODULATE,
            // ALPHAARG2(6)=DIFFUSE, SetFVF(0x142 XYZ|DIFFUSE|TEX1), SetTransform(WORLD, identité).
            // SANS ce bracket, le FVF hérité de RenderSky (XYZRHW pré-transformé écran) réinterprète les
            // sommets billboard 24o (coords MONDE) -> positions aberrantes, muzzle/hit invisibles.
            // Ajouts défensifs C++ (le binaire les hérite d'un état permanent qu'on ne garantit pas ici) :
            // VS/PS null (MeshRenderer laisse ses shaders skinnés bindés), VIEW/PROJ caméra (MeshRenderer
            // passe par des shaders, pas par SetTransform), SRCBLEND/DESTBLEND alpha standard.
            D3DXMATRIX fxIdent; D3DXMatrixIdentity(&fxIdent);
            fxDev->SetVertexShader(nullptr);
            fxDev->SetPixelShader(nullptr);
            fxDev->SetRenderState(D3DRS_LIGHTING, FALSE);              // 137,0
            fxDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);         // 14,0
            fxDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);      // 27,1
            fxDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);      // défensif (binaire hérite)
            fxDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);  // défensif (binaire hérite)
            fxDev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE); // 4,4
            fxDev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);   // 6,0
            fxDev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1); // 322 = 0x142
            fxDev->SetTransform(D3DTS_WORLD, &fxIdent);              // 256, identité
            fxDev->SetTransform(D3DTS_VIEW, &fxView);
            fxDev->SetTransform(D3DTS_PROJECTION, &fxProj);
            gfx::Fx_SetParticleFrame(fxDev, fxRight, fxUp, 0 /*maxQuads: pas de plafond*/, nullptr);
            // (Vague F) Plans de frustum pour le cull par-part des meshes FX. La POSITION et le
            // RECYCLAGE (flipbook one-shot) des slots MESH se font en phase UPDATE (tick ci-dessus) ;
            // le rendu ne fait que consommer slot.position/drawParam déjà résolus.
            if (modelObjRenderer_) modelObjRenderer_->SetFrame(fxView, fxProj);
            for (int pass = 1; pass <= 3; ++pass)
                for (int i = 0; i < gfx::FxPool_Count(); ++i)
                    gfx::Fx_EmitterDraw(&gfx::FxPool_Slots()[i], pass);
        }
        if (worldReady_ && world_)
            gfx::GxdRenderer::Instance().RenderPostBlur(world_->BloomShaderSet());
        if (hudReady_ && hud_) hud_->Render();
        if (windowsReady_ && windows_) windows_->Render();
        break;
    default:
        // None : écran effacé par Renderer::BeginFrame.
        break;
    }
}

// UIFW-08 — porte d'état d'action des entrées souris, cf. SceneManager.h.
bool SceneManager::InputSwallowedByActionState() const {
    if (scene_ != Scene::InGame || g_SceneSubState != 4) return false;   // `g_SceneMgr != 6 ||
                                                                         //  g_SceneSubState != 4` @0x50ACF7
    // g_SelfActionState[0] 0x1687328 == g_World.players[0].body @+220 (== entity+244, cf.
    // game::kSelfActionStateOffset, Game/MapWarp.h:83). MÊME lecture que host.GetSelfActionState
    // (étape 12 du tick InGame ci-dessus) — factorisée ici plutôt que dupliquée.
    if (game::g_World.players.empty()) return false;
    const game::PlayerEntity& self0 = game::g_World.players[0];
    int32_t raw = 0;
    if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw))
        std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
    switch (raw) {   // `!= 11 && != 12 && != 33 && != 34 && != 35 && != 36 && != 37` @0x50ACF7
    case 11: case 12: case 33: case 34: case 35: case 36: case 37:
        return true;   // clic TOTALEMENT avalé : ni UI_RouteLButton*, ni cSceneMgr_OnLButton*
    default:
        return false;
    }
}

void SceneManager::OnLButtonDown(int x, int y) {
    switch (scene_) {
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->OnMouseDown(scene_, x, y); ConsumePending(); }
        break;
    case Scene::InGame:
        // UIFW-08 — Input_OnLButtonDown 0x50AC90 @0x50ACF7 : dans certains états d'action de
        // soi (11/12/33..37), le clic n'atteint NI l'UI NI le monde. Testé AVANT le routage UI,
        // comme le binaire (la garde englobe l'appel à UI_RouteLButtonDown @0x50AD0F).
        if (InputSwallowedByActionState()) break;                        // 0x50ACF7
        // Les fenêtres (dialogues) interceptent le clic en premier (règle « premier
        // consommateur gagne » de UIManager) ; sinon il tombe vers le HUD.
        if (windowsReady_ && ui::UIManager::Instance().RouteMouseDown(x, y)) break;
        if (hudReady_ && hud_) hud_->OnMouseDown(x, y);
        break;
    default: break;
    }
}

void SceneManager::OnLButtonUp(int x, int y) {
    switch (scene_) {
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->OnMouseUp(scene_, x, y); ConsumePending(); }
        break;
    case Scene::InGame:
        // UIFW-08 — MÊME garde, entrée Input_OnLButtonUp 0x50AD20 @0x50AD87 (motif identique
        // à @0x50ACF7, vérifié au désassemblage : les 4 entrées souris la portent).
        if (InputSwallowedByActionState()) break;                        // 0x50AD87
        if (windowsReady_) ui::UIManager::Instance().RouteMouseUp(x, y);
        break;
    default: break;
    }
}

// GAP-APPLIFE-02 — UI_Chat_FocusInput 0x68B200 (xref UNIQUE : App_WndProc @0x461B5E).
bool SceneManager::FocusChatInput() {
    // Garde d'entrée `if (g_SceneMgr == 6 && g_SceneSubState == 4)` @0x68B217 : hors de cet
    // état, l'original ne prend AUCUN focus (le chat ne s'ouvre pas pendant le chargement
    // de zone ni dans le shell login).
    if (scene_ != Scene::InGame || g_SceneSubState != 4) return false;   // @0x68B217
    if (!hudReady_ || !hud_) return false;
    // Le binaire choisit ici entre deux EDIT : `if (dword_18225C0 || dword_1822724)` @0x68B239
    // -> UI_FocusEditBox(id 16 = index 15, boîte « dire ») ; sinon id 5 = index 4 = chat
    // principal (@0x68B22B / @0x68B250 ; l'id passé vaut index+1 — g_hEditChatMain 0x1668FD4
    // est à (0x1668FD4-0x1668FC4)/4 = index 4 du tableau g_hEditLoginId 0x1668FC4).
    // ClientSource n'a QU'UNE boîte de saisie in-game (ui::ChatWindow) : les deux branches
    // convergent donc sur le même widget. Les deux discriminants ne sont tracés nulle part.
    // TODO [ancre 0x68B239 / dword_18225C0 / dword_1822724] : distinguer la boîte « dire »
    // du chat principal le jour où elle sera modélisée.
    hud_->Chat().Focus();                                                // 0x50F4A0 (id 5)
    return true;
}

// GAP-APPLIFE-02 — arbitrage clavier de la saisie texte, cf. SceneManager.h.
bool SceneManager::RouteTextInputKey(int vk) {
    if (scene_ != Scene::InGame || !hudReady_ || !hud_) return false;
    // (1) Un champ de saisie FOCALISÉ mange la touche : c'est UI_EditBoxWndProc 0x50E070 qui
    // la reçoit dans le binaire (l'EDIT natif a le focus Win32), pas la fenêtre principale.
    // ChatWindow::OnKey couvre le même jeu de touches que la sous-classe d'origine :
    // Entrée -> submit (UI_Chat_SubmitInput 0x68B330 @0x50E1D6), Échap -> annule, Retour
    // arrière/flèches/Tab (@0x50E070 case 4).
    if (hud_->Chat().Focused()) return hud_->Chat().OnKey(vk);           // 0x50E070
    // (2) Aucun EDIT focalisé -> la fenêtre principale reçoit WM_KEYDOWN, dont le SEUL
    // traitement clavier est `if (a3 == 13) UI_Chat_FocusInput();` (App_WndProc @0x461B55/
    // @0x461B5E). VK_RETURN == 13 == kVK_RETURN (UI/ChatWindow).
    if (vk == VK_RETURN) return FocusChatInput();                        // 0x461B5E -> 0x68B200
    return false;
}

void SceneManager::OnChar(char c) {
    if (scene_ == Scene::Login && login_) { login_->OnChar(c); ConsumePending(); }
    // GAP-APPLIFE-02 — saisie de chat in-game. ChatWindow::OnChar filtre lui-même
    // (`if (!focused_) return false;` UI/ChatWindow.cpp:429), donc l'appel est inoffensif
    // hors saisie. Source : App/App.cpp WM_CHAR -> scene_.OnChar (inconditionnel, toutes
    // scènes). Le binaire n'a NI WM_CHAR NI EDIT custom : il confie la saisie aux 21 EDIT
    // Win32 natifs de mEDITBOX (UI_CreateEditBoxes 0x50E460) qui consomment WM_CHAR
    // eux-mêmes — déviation compensatoire déjà assumée et documentée par App/App.cpp:1067.
    else if (scene_ == Scene::InGame && hudReady_ && hud_) hud_->Chat().OnChar(c);
}

void SceneManager::OnKeyDown(int vk) {
    if ((scene_ == Scene::Login || scene_ == Scene::CharSelect) && login_) {
        login_->OnKeyDown(vk);
        ConsumePending();
    } else if (scene_ == Scene::InGame && windowsReady_ && windows_) {
        // NB : en scène InGame, App/App.cpp:1094 restreint DÉLIBÉRÉMENT ce point d'entrée au
        // chemin DirectInput (`if (scene_.Current() != Scene::InGame)` sur le WM_KEYDOWN) :
        // `vk` porte donc ici un SCANCODE DIK, pas un virtual-key. C'est fidèle — le clavier
        // in-game du binaire est 100 % DIK (Game_OnHotkey 0x537330 relit le tampon
        // DirectInput). La saisie de chat, elle, passe par RouteTextInputKey (VK, chemin
        // Win32) — cf. cSceneMgr_OnKeyDown 0x517F80, qui est __thiscall SANS paramètre de
        // touche et se contente de `switch(*this)` -> case 6 -> Game_OnHotkey.
        // Un dialogue OUVERT (Échap/Entrée...) intercepte avant les raccourcis
        // globaux d'ouverture (I/C/K/G/O/...), comme UI_RouteKeyInput d'origine.
        if (ui::UIManager::Instance().RouteKey(vk)) return;
        windows_->HandleHotkey(vk);
    }
}

void SceneManager::OnDeviceLost() {
    if (login_)    login_->OnDeviceLost();
    if (hud_)      hud_->OnDeviceLost();
    if (windows_)  windows_->OnDeviceLost();
    if (world_)    world_->OnDeviceLost();
    if (worldGeom_) worldGeom_->OnDeviceLost();
    if (modelObjRenderer_) modelObjRenderer_->OnDeviceLost(); // (Vague F) no-op : ressources MANAGED survivent au Reset
}

void SceneManager::OnDeviceReset() {
    if (login_)    login_->OnDeviceReset();
    if (hud_)      hud_->OnDeviceReset();
    if (windows_)  windows_->OnDeviceReset();
    if (world_)    world_->OnDeviceReset();
    if (worldGeom_) worldGeom_->OnDeviceReset();
    if (modelObjRenderer_) modelObjRenderer_->OnDeviceReset(); // (Vague F)
}

} // namespace ts2

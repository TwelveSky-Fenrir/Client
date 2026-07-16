// UI/NpcDialogWindow.cpp — page 0 de `cNpcWin` : le menu de services PNJ.
// Voir UI/NpcDialogWindow.h pour l'ancrage complet et la preuve que `this+2` est un
// game::NpcDefRecord*.
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant <windows.h>) —
// même règle que UI/GuildWindow.cpp.
#include "Net/SendPackets.h"      // net::Net_SendWarpRequest / Net_SendOp116 / Net_SendOp126
#include "Net/NetClient.h"        // net::GlobalNetClient() == &g_NetClient 0x8156A0
#include "UI/NpcDialogWindow.h"
#include "UI/TeleportWindow.h"    // page 76 ouverte par le service code 76 (0x4C)
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h"   // game::g_Client (Var/VarF/msg), game::Str(id)
#include "Game/GameState.h"       // game::g_World.self (level/levelBonus/currency/element)
#include "Game/MapWarp.h"         // game::WarpAddr::*, game::FactionTownNpcId
#include "Game/SkillCombat.h"     // game::Combat_ReadLocalElementPairs (Char_GetPairedElement)
#include "Net/Rng.h"              // net::DefaultRng() (Rng_Next 0x7603FD)

#include <cstdio>                 // std::snprintf (rendu du nom/des libellés)
#include <cstring>                // strnlen (champs de table non garantis null-terminés)
#include <string>

namespace ts2::ui {

namespace {

// Fond de panneau (best effort) : le vrai fond est le sprite `unk_8F7608` de l'atlas UI
// (UI_NpcMenu_Draw 0x5DFC30 @0x5dfdb6 `Sprite2D_Draw(&unk_8F7608, *this, *(this+1))`) —
// slot d'atlas non résolu côté ClientSource, cf. TODO des dims dans le .h. Repli kColBg.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_02463.IMG");

// g_SysMsgColor 0x84DFD8 — non modélisé en champ propre ; longue traîne via Var(), même
// convention que Game/SocialSystem.cpp:69.
constexpr uint32_t kSysMsgColorAddr = 0x84dfd8u;

// dword_16851B8 — global testé par UI_NpcWin_Open (0x5db946 / 0x5db9ea / 0x5dbc68 / 0x5dbacc) :
// quand il vaut 40, les services des slots n=24/28/29/38 sont neutralisés (code 0) TOUT EN
// CONSOMMANT leur emplacement (`++v9` exécuté dans les deux branches). Rôle exact non élucidé
// (probablement un id de zone/mode courant). Longue traîne via Var().
constexpr uint32_t kNpcMenuGateAddr = 0x16851B8u;
constexpr int32_t  kNpcMenuGateOff  = 40;

// Table `case n -> code de service` de UI_NpcWin_Open 0x5DB530 @0x5db6cc, relevée EXHAUSTIVEMENT
// sur le switch décompilé (EA de chaque affectation entre parenthèses). Les entrées n absentes
// du switch (50, 51, 55, 59, 60, 61, 63, 64 et 76..99) tombent sur `default: continue` — elles
// ne consomment AUCUN emplacement. 0 = pas de service (jamais produit par cette table ; le code 0
// n'apparaît que via la porte kNpcMenuGate ci-dessus).
int32_t ServiceCodeForFlagIndex(int n) {
    switch (n) {
    case  0: return  4; /*0x5db6d9*/  case  1: return  5; /*0x5db6f8*/
    case  2: return  6; /*0x5db717*/  case  3: return  7; /*0x5db736*/
    case  4: return  8; /*0x5db755*/  case  5: return  9; /*0x5db774*/
    case  6: return 10; /*0x5db793*/  case  7: return 11; /*0x5db7b2*/
    case  8: return 12; /*0x5db7d1*/  case  9: return 13; /*0x5db7f0*/
    case 10: return 46; /*0x5dbc29*/  case 11: return 22; /*0x5db907*/
    case 12: return 15; /*0x5db82e*/  case 13: return 16; /*0x5db84d*/
    case 14: return 17; /*0x5db86c*/  case 15: return 41; /*0x5dbb8e*/
    case 16: return 42; /*0x5dbbad*/  case 17: return 18; /*0x5db88b*/
    case 18: return 14; /*0x5db80f*/  case 19: return 20; /*0x5db8c9*/
    case 20: return 21; /*0x5db8e8*/  case 21: return 47; /*0x5dbc48*/
    case 22: return 23; /*0x5db926*/  case 23: return 19; /*0x5db8aa*/
    case 24: return 24; /*0x5db96d — porte kNpcMenuGate*/
    case 25: return 25; /*0x5db98c*/  case 26: return 26; /*0x5db9ab*/
    case 27: return 27; /*0x5db9ca*/
    case 28: return 28; /*0x5dba11 — porte kNpcMenuGate*/
    case 29: return 48; /*0x5dbc8f — porte kNpcMenuGate*/
    case 30: return 29; /*0x5dba30*/  case 31: return 30; /*0x5dba4f*/
    case 32: return 55; /*0x5dbd68*/  case 33: return 40; /*0x5dba6e*/
    case 34: return 54; /*0x5dbd49*/  case 35: return 36; /*0x5dbb12*/
    case 36: return 33; /*0x5dba8d*/  case 37: return 34; /*0x5dbaac*/
    case 38: return 35; /*0x5dbaf3 — porte kNpcMenuGate*/
    case 39: return 37; /*0x5dbb31*/  case 40: return 38; /*0x5dbb50*/
    case 41: return 39; /*0x5dbb6f*/  case 42: return 43; /*0x5dbbcc*/
    case 43: return 44; /*0x5dbbeb*/  case 44: return 45; /*0x5dbc0a*/
    case 45: return 49; /*0x5dbcae*/  case 46: return 50; /*0x5dbccd*/
    case 47: return 51; /*0x5dbcec*/  case 48: return 52; /*0x5dbd0b*/
    case 49: return 53; /*0x5dbd2a*/  case 52: return 58; /*0x5dbda6*/
    case 53: return 59; /*0x5dbdc5*/  case 54: return 56; /*0x5dbd87*/
    case 56: return 61; /*0x5dbde4*/  case 57: return 62; /*0x5dbe03*/
    case 58: return 63; /*0x5dbe22*/  case 62: return 64; /*0x5dbe41*/
    case 65: return 66; /*0x5dbe60*/  case 66: return 67; /*0x5dbe7f*/
    case 67: return 68; /*0x5dbe9e*/  case 68: return 69; /*0x5dbebd*/
    case 69: return 70; /*0x5dbedc*/  case 70: return 71; /*0x5dbefb*/
    case 71: return 72; /*0x5dbf1a*/  case 72: return 73; /*0x5dbf36*/
    case 73: return 75; /*0x5dbf52*/  case 74: return 76; /*0x5dbf6e*/
    case 75: return 77; /*0x5dbf8a*/
    default: return -1;               /*0x5dbf9b : default -> continue, pas de slot consommé*/
    }
}

// Les 4 slots neutralisés quand dword_16851B8 == 40 (0x5db946/0x5db9ea/0x5dbc68/0x5dbacc).
bool FlagIndexIsGated(int n) { return n == 24 || n == 28 || n == 29 || n == 38; }

// ---------------------------------------------------------------------------
// « Arme le bloc de warp puis émet Op20 » — corps LITTÉRALEMENT identique dans les trois
// handlers de warp de la page 0 :
//   UI_NpcMenu_CastReturn 0x5E19E0 @0x5e1aa9-0x5e1b4c  (mode 10)
//   UI_ClanWarp_Commit    0x608B30 @0x608c46-0x608ce9  (mode  6)
//   UI_WarpFactionTown    0x608D40 @0x608db4-0x608e57  (mode  6)
// Séquence EXACTE (mêmes globals, même ordre) :
//   g_MorphInProgress=1 ; dword_1675A8C=mode ; dword_1675A90=0 ; g_TargetZoneId=zone ;
//   Crt_Memset(&dword_1675AA0,0,0x48) ; dword_1675AA0=0 ; dword_1675AA4=1 ; flt_1675AA8=0.0 ;
//   flt_1675AAC/AB0/AB4 = pos ; flt_1675AC4=flt_1675AC8=Rng_Next()%360 ;
//   Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C, zone).
//
// NB Crt_Memset(&dword_1675AA0, 0, 0x48) = 72 o à partir de 0x1675AA0 (jusqu'à 0x1675AE8) :
// l'échappatoire g_Client.Var est une map adresse->slot, pas une image mémoire contiguë — on
// ne peut pas memset une plage. Seuls les champs RÉÉCRITS juste après par le binaire sont donc
// posés (ils couvrent 0x1675AA0..0x1675AC8) ; le reliquat 0x1675ACC..0x1675AE4 n'est modélisé
// par AUCUN champ WarpAddr et n'a AUCUN lecteur côté ClientSource -> rien à effacer.
// Même compromis (et même justification) que Game/MapWarp.cpp::ArmFullWarp.
void ArmWarpAndSendOp20(int32_t warpModeCode, int32_t zoneId, const float pos[3]) {
    using namespace ts2::game;
    g_Client.Var (WarpAddr::MorphInProgress) = 1;              // g_MorphInProgress = 1
    g_Client.Var (WarpAddr::WarpModeCode)    = warpModeCode;   // dword_1675A8C
    g_Client.Var (WarpAddr::WarpSub)         = 0;              // dword_1675A90
    g_Client.Var (WarpAddr::WarpTargetNpc)   = zoneId;         // g_TargetZoneId 0x1675A9C
    g_Client.Var (WarpAddr::WarpFlagA0)      = 0;              // dword_1675AA0
    g_Client.Var (WarpAddr::WarpFlagA4)      = 1;              // dword_1675AA4
    g_Client.VarF(WarpAddr::WarpDelay)       = 0.0f;           // flt_1675AA8
    g_Client.VarF(WarpAddr::WarpPosX)        = pos[0];         // flt_1675AAC
    g_Client.VarF(WarpAddr::WarpPosY)        = pos[1];         // flt_1675AB0
    g_Client.VarF(WarpAddr::WarpPosZ)        = pos[2];         // flt_1675AB4
    // flt_1675AC4 = flt_1675AC8 = (float)(Rng_Next() % 360) — un SEUL tirage, recopié.
    const float facing = static_cast<float>(ts2::net::DefaultRng().NextMod(360));
    g_Client.VarF(WarpAddr::WarpFacingA)     = facing;         // flt_1675AC4
    g_Client.VarF(WarpAddr::WarpFacingB)     = facing;         // flt_1675AC8

    // Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C, zone) — INCONDITIONNEL une fois la
    // garde !g_MorphInProgress franchie. Émis via l'ALIAS i32 Net_SendWarpRequest et NON via
    // Net_SendPacket_Op20(int8_t,int8_t) : les zones de mes chemins valent 140 (WarpFactionTown
    // élément 3), 291 (ClanWarp) et 71/72/73 (CastReturn) ; 140 et 291 dépassent 127 et
    // seraient SIGN-étendues par le builder int8_t (0x8C -> 0x8CFFFFFF). Le binaire pousse a2/a3
    // zéro-étendus sur 32 bits. Cf. note Net/SendPackets.h:247-253.
    //
    // Le singleton : le binaire lit g_NetClient 0x8156A0 en GLOBAL (Net_SendPacket_Op20 0x4B5000
    // ne reçoit jamais de socket). Côté C++ c'est net::GlobalNetClient(), RENSEIGNÉ par
    // ConnectLoginServer/ConnectGameServer (Net/Login.cpp:131/313) — le test `if (client)` est une
    // sécurité de déréférencement (nul tant qu'aucune connexion n'est amorcée ; ce menu ne s'ouvre
    // qu'en jeu, post-handshake), PAS une garde masquante : le chemin est réellement atteint.
    // Même pattern que Game/MapWarp.cpp:86.
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendWarpRequest(*client, warpModeCode, zoneId);

    // TODO [ancre Motion_GetComboOffsetTable 0x5025E0 / GInfo2_GetVec3 0x4FD4C0] : `pos` arrive
    // à {0,0,0} tant qu'aucun résolveur de coordonnées de ville n'est branché (tables NPC/motion
    // .IMG, hors périmètre — cf. game::IFactionTownCoordResolver dans Game/MapWarp.h). Sans effet
    // sur l'ÉMISSION (Op20 ne transporte que mode+zone), seulement sur le bloc local de mise en
    // scène. Ordre des tirages RNG préservé : le facing est tiré AVANT les 4 nonces d'Op20.
}

} // namespace

NpcDialogWindow::NpcDialogWindow() {
    // Position initiale = centre écran de référence ; recalculée à chaque Recenter() comme le
    // binaire (UI_NpcMenu_Draw 0x5dfc54/0x5dfc7c).
    x_ = (ts2::kRefWidth  - kPanelW) / 2;
    y_ = (ts2::kRefHeight - kPanelH) / 2;
}

// ============================================================================
// Cycle de vie — UI_NpcWin_Open 0x5DB530 / UI_NpcWin_CloseRestore 0x5DC1F0
// ============================================================================
void NpcDialogWindow::Open(const game::NpcDefRecord* npcDef) {
    // 0x5db540 : UI_CloseAllDialogs(&dword_1821D4C, 1) — ferme TOUS les autres dialogues avant
    // d'ouvrir. TODO [ancre UI_CloseAllDialogs 0x5AC590] : pas d'équivalent « fermer tout » sur
    // ts2::ui::UIManager (registre + routeurs seulement) et UIManager.h ne m'appartient pas.
    // Conséquence fidèle manquante : un autre dialogue déjà ouvert le reste.

    Dialog::Open();          // 0x5db553 : *(this+3) = 1

    npcDef_ = npcDef;        // 0x5db54d : *(this+2) = *(DWORD*)a2  (cf. preuve dans le .h)

    // 0x5db55d..0x5db58f : *(this+4..9) = -1 (6 slots de matériaux « en cours de dépôt »,
    // relus par UI_NpcWin_CloseRestore 0x5DC1F0 pour rendre les objets à l'inventaire). Ces
    // slots appartiennent aux pages Craft/Refine (pas à la page 0, qui ne dépose rien) — non
    // modélisés ici. TODO [ancre 0x5DC1F0] : à porter par les fronts des pages concernées.

    // 0x5db596 : for(i<100) *(this+i+70) = 0 — le binaire purge 100 verrous ; seuls les 10
    // premiers sont utilisés par la page 0 (boucles i<10 partout ailleurs), les 90 autres
    // appartiennent aux verrous de boutons des autres pages.
    for (int i = 0; i < kMaxServices; ++i) pressLatch_[i] = false;

    // 0x5db5c1 : for(j<10) *(this+j+170) = 0
    for (int j = 0; j < kMaxServices; ++j) serviceCodes_[j] = 0;

    // 0x5db5ec : for(k<6){ *(this+k+10)=0 ; for(m<9) *(this+9*k+m+16)=0 } — grille 6x9 des
    // pages Craft (hors page 0), non modélisée ici.

    serviceCodes_[0] = 1;    // 0x5db652 : *(this+170) = 1  (salutation)
    serviceCodes_[1] = 2;    // 0x5db66c : *(this+171) = 2  (expertise)

    // 0x5db680 : for(n<100) if (*(DWORD*)(def + 4*n + 1340) == 2 && v9 <= 8) -> switch(n).
    // La garde `v9 <= 8` réserve l'emplacement 9 au service « fermer » posé plus bas.
    const int32_t gate = game::g_Client.VarGet(kNpcMenuGateAddr);
    int v9 = 2;              // 0x5db67d
    for (int n = 0; n < 100; ++n) {
        // fieldG[100] @+1340 == nMenu[n] ; « proposé » <=> valeur exactement 2 (0x5db6b5).
        if (!npcDef_ || npcDef_->fieldG[n] != 2 || v9 > 8) continue;
        const int32_t code = ServiceCodeForFlagIndex(n);
        if (code < 0) continue;                       // default -> continue, aucun slot consommé
        // Porte dword_16851B8 == 40 : code forcé à 0 MAIS l'emplacement est CONSOMMÉ quand même
        // (`++v9` dans les DEUX branches — 0x5db95f/0x5dba03/0x5dbc81/0x5dbae5).
        serviceCodes_[v9++] = (FlagIndexIsGated(n) && gate == kNpcMenuGateOff) ? 0 : code;
    }

    serviceCodes_[9] = 3;    // 0x5dbfa6 : *(this+179) = 3  (fermer)
    // 0x5dbfb3 : *(this+180) = 0 — page 0. Cette classe EST la page 0 : pas de champ `page`.

    // 0x5dbfbd : for(ii<5) *(this+ii+181) = (ii >= *(DWORD*)(def+32))
    // -> les pages de salutation au-delà de fieldA (== nSpeechNum) sont pré-marquées
    // « déjà utilisées », donc jamais tirées par PickGreeting().
    const uint32_t speechCount = npcDef_ ? npcDef_->fieldA : 0u;
    for (int ii = 0; ii < kGreetingSlots; ++ii)
        greetingUsed_[ii] = (static_cast<uint32_t>(ii) >= speechCount);

    greetingIdx_ = -1;       // 0x5dc00c : *(this+186) = -1

    // 0x5dc01d..0x5dc0a8 : queue « a2[3]=1 / a2[4]=0.0 / orientation du PNJ vers le joueur
    // (Math_AngleBetween2D, sauf kinds 63/113/213/313/7) / Fx_MeleeSwingUpdate ». Mute
    // l'ENTITÉ DE RENDU g_NpcRenderArray, pas la fenêtre -> appartient au front picking 3D
    // (cf. bandeau du .h). Volontairement absent ici.
}

void NpcDialogWindow::Close() {
    // UI_NpcWin_CloseRestore 0x5DC1F0 — N'ÉMET RIEN (vérifié : aucun Net_Send* dans son corps ;
    // le service « fermer » (code 3) y mène via UI_NpcMenu_OnLUp_CloseNpcWin 0x5E1980, qui est
    // un simple trampoline). Son travail réel = restituer jusqu'à 6 slots de matériaux vers
    // l'inventaire (g_InvMain/g_InvGrid_*/g_InvAux), cGameHud_Hide, et éventuellement
    // UI_FocusEditBox. TODO [ancre 0x5DC1F0] : la restitution des matériaux concerne les slots
    // *(this+4..9) des pages Craft/Refine, non modélisés par la page 0 (cf. Open()).
    for (int i = 0; i < kMaxServices; ++i) pressLatch_[i] = false;
    Dialog::Close();
}

// ============================================================================
// Géométrie — recentrage par frame (0x5df574 / 0x5df654 / 0x5dfc54)
// ============================================================================
void NpcDialogWindow::Recenter(int screenW, int screenH) {
    // *this      = nWidth/2  - Sprite2D_GetWidth(&unk_8F7608)/2
    // *(this+1)  = nHeight/2 - Sprite2D_GetHeight(&unk_8F7608)/2
    // (dims du sprite remplacées par kPanelW/kPanelH — cf. TODO du .h)
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

NpcDialogWindow::Rect NpcDialogWindow::ServiceRowRect(int row) const {
    // Sprite2D_HitTest(&unk_8F7730, *this + 12, *(this+1) + 22*row + 7, mx, my) — 0x5df6fb.
    return { x_ + kRowOffsetX, y_ + kRowPitchY * row + kRowOffsetY, kRowW, kRowH };
}

// ============================================================================
// Messages système
// ============================================================================
void NpcDialogWindow::SysMsg(int strId) {
    // Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, strId), g_SysMsgColor).
    // Même convention que Game/SocialSystem.cpp:68-72 (g_SysMsgColor 0x84DFD8 en longue traîne).
    const uint32_t sysColor = static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
    game::g_Client.msg.System(game::Str(strId), sysColor);
}

bool NpcDialogWindow::CheckNpcFaction() {
    // `*(DWORD*)(def+1312) - 2 == g_LocalElement` — UI_NpcMenu_CastReturn 0x5e19fe,
    // UI_ClanWarp_Commit 0x608b4e. fieldB == nTribe (cf. bandeau du .h).
    // Le binaire déréférence `def` sans test ; def==nullptr ne peut arriver que côté C++ ->
    // refus explicite (pas d'émission), jamais de crash.
    if (!npcDef_) return false;
    if (static_cast<int32_t>(npcDef_->fieldB) - 2 == game::g_World.self.element) return true;
    SysMsg(143);   // StrTable005_Get(g_LangId, 143) — 0x5e1a10 / 0x608b60
    return false;
}

// ============================================================================
// Événements souris / clavier
// ============================================================================
bool NpcDialogWindow::OnMouseDown(int x, int y) {
    // UI_NpcWin_OnLDown_Dispatch 0x5DCB10 : `if (!*(this+3)) return 0;` puis page 0 ->
    // UI_NpcMenu_OnLDown 0x5DF560. La fenêtre est modale de fait : le dispatcher renvoie
    // TOUJOURS 1 quand elle est ouverte (result = 1 dans toutes les branches, y compris default).
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x5df574 : recentrage AVANT le hit-test

    for (int i = 0; i < kMaxServices; ++i) {
        // 0x5df5db : `if ((int)*(this+i+170) >= 1)` — les emplacements à 0 (porte
        // dword_16851B8==40) sont ignorés au down.
        if (serviceCodes_[i] < 1) continue;
        const Rect r = ServiceRowRect(i);
        if (!PointInRect(x, y, r.x, r.y, r.w, r.h)) continue;
        // 0x5df61c : Snd3D_PlayScaledVolume(flt_1487E3C, ..., 0, 100, 1) — clic de menu.
        // TODO [ancre 0x5DF61C] son de clic non branché (pas de sink audio sur ce front).
        pressLatch_[i] = true;              // 0x5df627 : *(this+i+70) = 1
        return true;                         // le binaire RETOURNE au premier hit (pas de suite)
    }
    return true;   // modal : le dispatcher consomme le clic même hors ligne (0x5dcb10 default)
}

bool NpcDialogWindow::OnClick(int x, int y) {
    // UI_NpcWin_OnLUp_Dispatch 0x5DD3B0 -> page 0 : UI_NpcMenu_OnLUp 0x5DF640.
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x5df654 : recentrage AVANT le hit-test

    for (int i = 0; i < kMaxServices; ++i) {
        // 0x5df6b7 : `if (*(this+i+70))` — SEUL le verrou compte au up (pas de test `>= 1` ici,
        // contrairement au down : le verrou ne peut de toute façon être armé que sur code >= 1).
        if (!pressLatch_[i]) continue;
        pressLatch_[i] = false;             // 0x5df6c9 : purge INCONDITIONNELLE du verrou
        const Rect r = ServiceRowRect(i);
        // 0x5df6fb : le hit-test est refait au relâchement ; s'il échoue, la boucle CONTINUE
        // (le verrou reste purgé) — reproduit tel quel.
        if (!PointInRect(x, y, r.x, r.y, r.w, r.h)) continue;
        DispatchService(serviceCodes_[i]);  // 0x5df72c : switch(*(this+i+170))
        return true;                         // 0x5df702 : `return result` au premier hit
    }
    return true;   // modal (même raison qu'OnMouseDown)
}

bool NpcDialogWindow::OnKey(int vk) {
    // UI_NpcWin_OnKey 0x5DE030 : n'émet RIEN ; ajuste g_Currency/g_InvWeight si *(this+22683)==4
    // (page 4 uniquement, hors page 0) puis UI_NpcWin_CloseRestore. Aucune touche particulière
    // n'est filtrée côté binaire ; Échap = convention du portage (cf. autres Dialog).
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { Close(); return true; }
    return false;
}

// ============================================================================
// Dispatch des services — switch de UI_NpcMenu_OnLUp 0x5DF640 @0x5df72c
// ============================================================================
void NpcDialogWindow::DispatchService(int code) {
    switch (code) {
    case 1:     PickGreeting();          return;  // 0x5df736 UI_NpcMenu_PickGreeting 0x5DFF00
    case 3:     Close();                 return;  // 0x5df750 UI_NpcMenu_OnLUp_CloseNpcWin 0x5E1980
                                                  //          -> UI_NpcWin_CloseRestore, n'émet rien
    case 4:     CastReturn();            return;  // 0x5df75d UI_NpcMenu_CastReturn      0x5E19E0
    case 0x31:  WarpFactionTown();       return;  // 0x5df99d UI_WarpFactionTown         0x608D40
    case 0x35:  ClassChangeValidate();   return;  // 0x5df9d1 UI_ClassChange_Validate    0x60A310
    case 0x37:  SendOp116AndClose();     return;  // 0x5df9eb UI_NpcMenu_..SendOp116AndClose 0x60FA60
    case 0x3E:  FactionAdvanceCommit();  return;  // 0x5dfa39 UI_FactionAdvance_Commit   0x612C20
    case 0x4C:  OpenTeleportPage();      return;  // 0x5dfad4 (case 76) cTeleportWin_Init 0x627BA0

    // ----------------------------------------------------------------------------------
    // Services ÉMETTEURS non portés — la PREUVE du layout existe (cf. rapport de front),
    // mais l'ÉTAT que leurs gardes interrogent n'est modélisé nulle part côté ClientSource.
    // Émettre sans la garde serait pire que ne pas émettre (paquet envoyé là où le binaire
    // refuse) : on s'abstient explicitement plutôt que de deviner. AUCUN builder ne manque.
    // ----------------------------------------------------------------------------------
    case 9:
        // TODO [ancre UI_NpcMenu_RequestJoinFaction 0x5E5680] : 13 gardes en cascade, dont
        // Char_GetElementAffinity / Char_ClassifyAffinityRankA / Char_CompareSkillLoadout /
        // byte_1686334[130*elem+13*i] / g_PartyRosterNames / Char_GetMaxAffinityIndex —
        // aucun de ces états n'est modélisé. N'émet RIEN directement : ouvre MsgBox type 5
        // (0x5e5a05), et c'est UI_MsgBox_OnLButtonUp 0x5C0A90 case 5 (@0x5C0D99) qui émet
        // Net_SendOp37 (@0x5c0dc4) sous `g_MorphInProgress!=1 && !g_GmCmdCooldownLatch`.
        // Le type de MsgBox n'existe pas côté C++ (MsgBoxDialog prend un callback), donc ce
        // câblage appartient au front MsgBox. Builder Net_SendOp37 : PRÉSENT (SendPackets.h:132).
        return;
    case 0x18:
        // NE PAS PORTER — gap BLOQUÉ (état non modélisé), pas « prêt à câbler ». RE-PROUVÉ dans
        // IDA (Warp_ProcessKeyword 0x5F54E0 décompilée) : UI_NpcMenu_ExecuteWarp 0x5F5470
        // @0x5f548d = Warp_ProcessKeyword(dword_168618C[g_LocalElement]) ; switch @0x5f5513, SEULS
        // cases 0/2/6/9/12 agissent (default @0x5f5b2e -> 0). Ce n'est PAS une « table de
        // mots-clés » (lecture erronée corrigée) mais une ÉCHELLE DE GUERRE DE CLAN : chaque
        // branche compare le nom de clan local dword_16746A8 (WARP-03) aux crans T1
        // byte_16861C3[52*elem]/unk_16861B6/16861A9/168619C (WARP-01/02) et T2 unk_168626C ;
        // succès -> Warp_LookupDest 0x5F5B60 puis LABEL_59 Warp_SendTeleport(g_LocalElement, pos)
        // @0x5f5aee -> zones {138,139,165,166}. Le TRANSPORT est porté (game::Warp_SendTeleport,
        // Game/MapWarp.h:187) ; le BLOCAGE est l'ÉTAT : dword_16746A8 et T1/T2 n'ont AUCUN
        // écrivain côté C++ (Net/GameHandlers_PartyGuild.cpp / Net/WorldEntityDispatch.cpp, hors
        // de mes fichiers). Porter maintenant lirait des tables VIDES.
        // ⚠️ RISQUE D'ÉMISSION prouvé (motif de l'abstention) — case 2, branche @0x5f5740 : si
        // T2[elem]=="" et dword_16746B8==0 (rang, non écrit), Str_NameListContainsMismatch(
        // byte_1822730, 5, dword_16746A8) @0x5f578c (3e arg = le nom de clan LOCAL, == "" hors
        // clan) MATCHE dès qu'un slot de la liste est vide -> émet
        // Net_SendOp107 @0x5f57ce, un paquet que le binaire N'ÉMET PAS dans un état réel.
        // ⚠️ WARP-09 (à corriger le jour du câblage) : Str_NameListContainsMismatch 0x5CCCF0
        // renvoie 1 sur CORRESPONDANCE (`!Crt_Strcmp(this+13*i+8, a3)` @0x5ccd3a ; garde
        // `count>7 -> 0` @0x5ccd07) — le nom IDA dit l'INVERSE de son comportement.
        return;
    case 0x30:
        // NE PAS PORTER — gap BLOQUÉ (buffers de nom de clan non modélisés). RE-PROUVÉ dans IDA
        // (UI_ClanWarp_Commit 0x608B30 décompilée) : garde faction `*(*(this+2)+1312)-2 ==
        // g_LocalElement` @0x608b4e sinon str005[143] ; PUIS garde clan `!Crt_Strcmp(dword_16746A8,
        // &String) || (Crt_Strcmp(byte_1686138,dword_16746A8) && Crt_Strcmp(byte_1686145,
        // dword_16746A8))` @0x608bd1 sinon str005[1474]. dword_16746A8/byte_1686138/byte_1686145
        // (noms de clan ; byte_1686145 - byte_1686138 = 13) n'ont AUCUN écrivain côté C++ -> garde
        // « membre/officier du clan » inévaluable. Corps armé : v8=291 ; Motion_GetComboOffsetTable(
        // g_LocalElement, 291, v7) @0x608c1e AVEC repli GInfo2_GetVec3(flt_1555D08, 291, v7)
        // @0x608c34 (contraste : la page téléport payante 0x627D50, elle, n'a AUCUN repli — le
        // case 291 dépend RÉELLEMENT de byte_1686138 vs dword_16746A8, à la différence des mapIds
        // 313/316/331/334 qui résolvent une constante) ; mode 6 ; Op20(6, 291) @0x608ce9 (zone 291
        // >= 128 -> alias i32) ; `return UI_NpcWin_CloseRestore(this)` @0x608cf1 HORS du
        // `if (!g_MorphInProgress)` (fermeture inconditionnelle) ; AUCUN latch g_GmCmdCooldownLatch.
        // Builder Net_SendWarpRequest : PRÉSENT.
        return;
    case 0x32:
        // TODO [ancre UI_ClanDisband_Commit 0x608EC0] : Net_SendOp79(&g_AutoPlayMgr, 14, v7)
        // (@0x608f5f) sous gardes `Crt_Strcmp(byte_1686138,dword_16746A8)!=0` -> str005[1492],
        // `dword_16746B8!=0` -> str005[1497], puis `g_MorphInProgress!=1 && !g_GmCmdCooldownLatch`.
        // Mêmes buffers de clan non modélisés que le case 0x30 -> garde « je suis le chef de
        // clan » IMPOSSIBLE à évaluer ; émettre une dissolution de clan sans elle serait un bug
        // actif. ⚠️ FIDÉLITÉ À REPRODUIRE LE JOUR DU CÂBLAGE : `v7` est un `_BYTE[108]` JAMAIS
        // initialisé -> le binaire émet 100 octets de PILE NON INITIALISÉE (bug d'origine ; le
        // builder Net_SendOp79(nc, int8_t, const void* payload100) est PRÉSENT, SendPackets.h:195).
        return;

    default:
        // 0x5df72c : les 60+ autres cases mènent aux 77 AUTRES PAGES de cNpcWin (Appraise 2 /
        // SkillLearn 5 / Refine 6 / CreateClan 7 / Shop 8 / Craft 10 / Stall 12 / Warehouse 0x15 /
        // GemSocket 0x13 / Enchant 0x1E / Guild 0x0F-0x10 / ClanCreate 0x2F / …), possédées par
        // d'autres fronts. Le code 0 (porte dword_16851B8==40) tombe ici aussi : le binaire fait
        // `default: return result` sans rien faire — comportement identique.
        // TODO [ancre UI_NpcMenu_OnLUp 0x5DF640 @0x5df72c] : déléguer chaque code à la fenêtre
        // du front propriétaire quand ces pages seront portées.
        return;
    }
}

// ============================================================================
// Code 76 (0x4C) — bascule vers la page 76 (téléportation payante)
// UI_NpcMenu_OnLUp 0x5DF640 @0x5dfad4 (jumptable 005DF72C case 76) -> cTeleportWin_Init 0x627BA0
// ============================================================================
void NpcDialogWindow::OpenTeleportPage() {
    // Le binaire NE crée pas de nouvel objet : cTeleportWin_Init(this) fait *(this+180)=76
    // (@0x627bac) sur le MÊME objet cNpcWin ; les dispatchers OnLDown/OnLUp/Draw routent alors
    // vers les handlers cTeleportWin_* et la page 0 (ce menu) n'est plus dessinée. Côté C++,
    // page 0 et page 76 sont deux classes distinctes -> on OUVRE la TeleportWindow (qui purge
    // ses 4 verrous, = cTeleportWin_Init) et on FERME ce menu, ce qui reproduit fidèlement le
    // remplacement de page (les deux pages sont mutuellement exclusives dans le binaire).
    // teleport_ est injecté par l'hôte (UI/GameWindows) via SetTeleportWindow ; nullptr tant que
    // le câblage n'est pas fait -> no-op (ni ouverture, ni fermeture prématurée du menu).
    if (teleport_) {
        teleport_->Open();
        Close();
    }
}

// ============================================================================
// Code 1 — UI_NpcMenu_PickGreeting 0x5DFF00 (LOCAL, n'émet rien)
// ============================================================================
void NpcDialogWindow::PickGreeting() {
    // 0x5dff13 : `if (*(this+186) != 5)` — une fois saturé, la fonction ne fait plus RIEN.
    if (greetingIdx_ == kGreetingSlots) return;

    // 0x5dff1a : cherche la première page NON utilisée ; i == 5 <=> toutes consommées.
    int i = 0;
    for (; i < kGreetingSlots; ++i) {
        if (!greetingUsed_[i]) break;       // 0x5dff38
    }
    if (i == kGreetingSlots) {
        greetingIdx_ = kGreetingSlots;      // 0x5dff4f : *(this+186) = 5 (saturation)
        return;
    }
    // 0x5dff74 : do { *(this+186) = Rng_Next() % 5; } while (*(this + *(this+186) + 181));
    // Tirage à rejet — au moins une page libre existe (garanti par la boucle ci-dessus), donc
    // la boucle termine. Reproduit tel quel : le NOMBRE de tirages consommés sur le RNG partagé
    // dépend des rejets, exactement comme le binaire.
    do {
        greetingIdx_ = static_cast<int32_t>(ts2::net::DefaultRng().NextMod(kGreetingSlots));
    } while (greetingUsed_[greetingIdx_]);
    greetingUsed_[greetingIdx_] = true;     // 0x5dff9c : *(this + *(this+186) + 181) = 1
}

// ============================================================================
// Code 4 — UI_NpcMenu_CastReturn 0x5E19E0 -> Op20(mode 10, zone 71/72/73)
// ============================================================================
void NpcDialogWindow::CastReturn() {
    // 0x5e19fe : garde faction (sinon str005[143], AUCUNE émission).
    if (!CheckNpcFaction()) return;

    // 0x5e1a25..0x5e1a5b : v6 = 71/72/73 selon g_LocalElement 0/1/2 ; élément 3 -> v6 reste 0.
    int32_t v6 = 0;
    switch (game::g_World.self.element) {
    case 0: v6 = 71; break;   /*0x5e1a49*/
    case 1: v6 = 72; break;   /*0x5e1a52*/
    case 2: v6 = 73; break;   /*0x5e1a5b*/
    default: break;           // élément 3 : v6 = 0 -> 0x5e1a66 faux -> RIEN (pas même CloseRestore)
    }
    if (!v6) return;          // 0x5e1a66 : `if (v6)` — sinon `return result` nu

    // 0x5e1a81 : if (!Motion_GetComboOffsetTable(g_LocalElement, v6, v5)) GInfo2_GetVec3(...).
    // TODO [ancre 0x5025E0 / 0x4FD4C0] : tables non branchées -> position {0,0,0} (cf. note dans
    // ArmWarpAndSendOp20). N'affecte PAS l'émission.
    float pos[3] = { 0.0f, 0.0f, 0.0f };

    // 0x5e1aa3 : `if (!g_MorphInProgress)` — garde d'armement.
    if (!game::g_Client.VarGet(game::WarpAddr::MorphInProgress)) {
        ArmWarpAndSendOp20(/*warpModeCode=*/10, /*zoneId=*/v6, pos);  // 0x5e1ab3 / 0x5e1b4c
    }
    // 0x5e1b54 : CloseRestore est appelé DANS le `if (v6)`, donc AUSSI quand le morph bloquait
    // l'armement (pas seulement après une émission réussie). Reproduit tel quel.
    Close();
}

// ============================================================================
// Code 0x31 — UI_WarpFactionTown 0x608D40 -> Op20(mode 6, zone 1/6/11/140)
// ============================================================================
void NpcDialogWindow::WarpFactionTown() {
    // ⚠️ AUCUNE garde de faction ici : 0x608D40 attaque DIRECTEMENT le switch (contrairement à
    // CastReturn/ClanWarp). Ne pas en ajouter « par cohérence » serait une invention.
    // 0x608d61 : v4 = 1/6/11/140 selon g_LocalElement 0/1/2/3 — table IDENTIQUE à
    // game::FactionTownNpcId (Game/MapWarp.h:142, switch de Map_BeginWarpToFactionTown 0x55C510),
    // réutilisée telle quelle. ⚠️ NE PAS déléguer à game::BeginWarpToFactionTown : cette
    // fonction-là arme les modes 3/7/11 et porte une garde « mort », alors que 0x608D40 arme le
    // mode 6 sans garde — ce sont deux fonctions distinctes qui partagent seulement la table.
    const int32_t v4 = game::FactionTownNpcId(game::g_World.self.element);
    if (!v4) return;          // 0x608d8e : `if (v4)` — default du switch -> `return result` nu

    // 0x608da2 : GInfo2_GetVec3(flt_1555D08, v4, v3) SEUL (pas de Motion_GetComboOffsetTable ici,
    // contrairement à CastReturn/ClanWarp). Même TODO de résolution de coordonnées.
    float pos[3] = { 0.0f, 0.0f, 0.0f };

    // 0x608dae : `if (!g_MorphInProgress)`.
    if (!game::g_Client.VarGet(game::WarpAddr::MorphInProgress)) {
        // zone 140 (élément 3) >= 128 : l'alias i32 est OBLIGATOIRE (cf. ArmWarpAndSendOp20).
        ArmWarpAndSendOp20(/*warpModeCode=*/6, /*zoneId=*/v4, pos);   // 0x608dbe / 0x608e57
    }
    Close();                  // 0x608e5f : dans le `if (v4)`, même remarque que CastReturn
}

// ============================================================================
// Code 0x35 — UI_ClassChange_Validate 0x60A310 (gardes locales -> MsgBox type 46 -> Op79/15)
// ============================================================================
void NpcDialogWindow::ClassChangeValidate() {
    // Trois barèmes coût/niveau, testés dans CET ordre (0x60A310). `g_SelfLevelBonus` =
    // self.levelBonus (0x16731AC), `g_SelfLevel` = self.level (0x16731A8), `g_Currency` =
    // self.currency (0x1673180) — tous modélisés dans game::g_World.self (GameState.h:308/309).
    const auto& self = game::g_World.self;

    // Les 3 tests du binaire sont écrits en forme NÉGATIVE ; ils sont repris ici en forme
    // positive équivalente, terme pour terme (aucun terme omis) :
    //   0x60a31e : if (g_SelfLevelBonus <= 0)            -> branche 1 = `levelBonus > 0`
    //   0x60a39b : if (lvl < 113 || lvl > 145 || g_SelfLevelBonus)
    //                                                     -> branche 2 = `113<=lvl<=145 && !levelBonus`
    //   0x60a40d : if (lvl < 100 || lvl > 112)            -> branche 3 = `100<=lvl<=112`
    // ⚠️ Le terme `&& levelBonus == 0` de la branche 2 est INDISPENSABLE et n'est PAS
    // redondant avec la branche 1 : `levelBonus < 0` (négatif) passe la branche 1
    // (`<= 0` vrai) puis échoue la branche 2 (`|| g_SelfLevelBonus` vrai) et tombe donc sur
    // la branche 3. Sans ce terme, un levelBonus négatif appliquerait à tort le barème 50.
    // Les 3 refus de solde convergent tous sur LABEL_13 (0x60a418) -> str005[965] (VÉRIFIÉ :
    // les trois `goto LABEL_13` à 0x60a327 / 0x60a3a4 / 0x60a416 partagent le même bloc).
    if (self.levelBonus > 0) {
        if (self.currency < 100) { SysMsg(965); return; }   // 0x60a327 : early return, PAS de CloseRestore
        // TODO [ancre 0x60a375] : UI_MsgBox_Open(dword_1822438, 46, str005[1753], str005[1754]).
    } else if (self.level >= 113 && self.level <= 145 && self.levelBonus == 0) {
        if (self.currency < 50)  { SysMsg(965); return; }   // 0x60a3a4
        // TODO [ancre 0x60a3f3] : UI_MsgBox_Open(dword_1822438, 46, str005[1753], str005[1755]).
    } else if (self.level >= 100 && self.level <= 112) {
        if (self.currency < 20)  { SysMsg(965); return; }   // 0x60a416
        // TODO [ancre 0x60a462] : UI_MsgBox_Open(dword_1822438, 46, str005[1753], str005[1756]).
    } else {
        SysMsg(1801); return;                               // 0x60a479 : early return, PAS de CloseRestore
    }

    // TODO [ancre UI_MsgBox_OnLButtonUp 0x5C0A90 case 46 @0x5C18AA] : la CONFIRMATION émet
    // Net_SendOp79(&g_AutoPlayMgr, 15, var_70) (@0x5c18c4) SANS AUCUNE garde, où var_70 est un
    // `_BYTE[100]` MIS À ZÉRO par Crt_Memset(v49,0,0x64) (@0x5c0acf) — contrairement au payload
    // NON initialisé de l'Op79/14 du case 0x32. Builder Net_SendOp79 : PRÉSENT (SendPackets.h:195).
    // Le `type` de MsgBox n'existe pas côté C++ (MsgBoxDialog = titre/corps/callback) : ce
    // câblage appartient au front MsgBox, mon handler s'arrête à l'ouverture — conforme au
    // binaire, qui n'émet rien non plus dans 0x60A310.

    // 0x60a493 : les 3 branches MsgBox retombent sur `return UI_NpcWin_CloseRestore(this)`.
    Close();
}

// ============================================================================
// Code 0x37 — UI_NpcMenu_OnLUp_SendOp116AndClose 0x60FA60 -> Op116
// ============================================================================
void NpcDialogWindow::SendOp116AndClose() {
    // 0x60FA60 : AUCUNE garde. Net_SendOp116(&g_AutoPlayMgr) puis UI_NpcWin_CloseRestore.
    // Émission INCONDITIONNELLE via le singleton global (cf. note dans ArmWarpAndSendOp20).
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendOp116(*client);        // 0x60fa6c
    Close();                                      // 0x60fa79
}

// ============================================================================
// Code 0x3E — UI_FactionAdvance_Commit 0x612C20 -> Op126(1)
// ============================================================================
void NpcDialogWindow::FactionAdvanceCommit() {
    // 0x612c29 : switch(g_SelfMorphNpcId) — SEULS les cas 1/6/11/37/140 existent, `default`
    // renvoie sans rien faire. g_SelfMorphNpcId 0x1675A98 -> longue traîne (WarpAddr::SelfMorphNpcId,
    // même convention que partout ailleurs dans ClientSource).
    const int32_t morphNpc = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId);
    const int32_t elem     = game::g_World.self.element;
    // Char_GetPairedElement(g_LocalPlayerSheet, k) == Combat_ReadLocalElementPairs().Paired(k)
    // (Game/SkillCombat.h:93-136 — instantané de g_AlliancePairTable via g_Client.VarGet).
    const game::ElementPairTable pairs = game::Combat_ReadLocalElementPairs();

    bool allowed;
    switch (morphNpc) {
    case 1:   allowed = (elem == 0 || elem == pairs.Paired(0)); break; /*0x612c73 : `!g_LocalElement ||`*/
    case 6:   allowed = (elem == 1 || elem == pairs.Paired(1)); break; /*0x612cbb*/
    case 11:  allowed = (elem == 2 || elem == pairs.Paired(2)); break; /*0x612d03*/
    case 37:  allowed = true;                                   break; /*0x612c51 : goto LABEL_15 inconditionnel*/
    case 140: allowed = (elem == 3 || elem == pairs.Paired(3)); break; /*0x612d47*/
    default:  return;                                                  /*0x612c51 : default -> return result*/
    }
    if (!allowed) { SysMsg(143); return; }        // LABEL_14 @0x612d5a : str005[143]

    // LABEL_15 @0x612d79 : `if (g_SelfLevel >= 113)`.
    if (game::g_World.self.level < 113) { SysMsg(879); return; }  // 0x612d8c : str005[879]

    // 0x612da5 : Net_SendOp126(&g_AutoPlayMgr, 1) — le `1` est une CONSTANTE littérale.
    // ⚠️ PAS de CloseRestore dans cette fonction (contrairement aux autres handlers) : la
    // fenêtre reste ouverte après l'envoi. Reproduit tel quel.
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendOp126(*client, 1);
}

// ============================================================================
// Rendu — UI_NpcMenu_Draw 0x5DFC30
// ============================================================================
void NpcDialogWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;
    lastScreenW_ = ctx.screenW;             // mémorisés pour le recentrage des hit-tests
    lastScreenH_ = ctx.screenH;
    Recenter(ctx.screenW, ctx.screenH);     // 0x5dfc54 : le Draw recentre lui aussi

    const Rect panel = PanelRect();

    if (ctx.phase == UiPhase::Panels) {
        // 0x5dfdb6 : Sprite2D_Draw(&unk_8F7608, *this, *(this+1)) — fond du menu.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);

        // 0x5dfca8 : `if (*(this+186) != -1)` -> Sprite2D_Draw(&unk_8F7574, *this, *(this+1)-160)
        // = le bandeau « bulle de dialogue » AU-DESSUS du menu, affiché SEULEMENT une fois une
        // salutation tirée.
        if (greetingIdx_ != -1) {
            const int bubbleY = panel.y - 160;                    // 0x5dfccc
            const int bubbleH = panel.y - bubbleY;                // TODO [ancre 0x5DFCCC] dims
            kPanelBg.Draw(ctx, panel.x, bubbleY, panel.w, bubbleH, kColBg); // réelles de unk_8F7574
            ctx.DrawFrame(panel.x, bubbleY, panel.w, bubbleH, kColBorder, 1);
        }

        // 0x5dfdbb : for(i<10) if (*(this+i+170) >= 1) -> Sprite2D_Draw(&g_AssetMgr_UiAtlasSlots
        // + 148*v9, *this+12, *(this+1)+22*i+7), avec v9 = UI_NpcMenu_ServiceToStrId(code) + {0
        // au repos, +1 au survol (0x5dfe54), +2 si enfoncé (0x5dfe99)}. Les emplacements de code
        // 0 (porte dword_16851B8==40) ne sont PAS dessinés.
        // TODO [ancre UI_NpcMenu_ServiceToStrId 0x5DF160 / g_AssetMgr_UiAtlasSlots 0x8E8B50] :
        // le libellé de chaque service est un SPRITE d'atlas (slot = ServiceToStrId(code)*148),
        // pas du texte — mapping des 78 codes -> slot non porté. En attendant, la ligne est
        // dessinée en aplat avec ses 3 états, et le code numérique est écrit en phase texte.
        for (int i = 0; i < kMaxServices; ++i) {
            if (serviceCodes_[i] < 1) continue;
            const Rect r = ServiceRowRect(i);
            const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
            const D3DCOLOR col = pressLatch_[i] ? kColPressed : (hover ? kColHover : kColRowBg);
            ctx.FillRect(r.x, r.y, r.w, r.h, col);
            ctx.DrawFrame(r.x, r.y, r.w, r.h, kColBorder, 1);
        }
        return;
    }

    // --- Phase texte ---
    if (greetingIdx_ != -1 && npcDef_) {
        // 0x5dfcea : Crt_Vsnprintf(v7, "%s.....", *(this+2) + 4) puis UI_DrawNumberValue(v7,
        // *this+22, *(this+1)-144, 3). `def+4` == NpcDefRecord::name — le suffixe "....." est
        // LITTÉRAL dans le binaire (chaîne aS 0x7BA870).
        char nameBuf[64];
        const std::string npcName(npcDef_->name, strnlen(npcDef_->name, sizeof(npcDef_->name)));
        std::snprintf(nameBuf, sizeof(nameBuf), "%s.....", npcName.c_str());
        ctx.Text(nameBuf, panel.x + kNameOffsetX, panel.y + kNameOffsetY, kColTitle);

        // 0x5dfd2e : `if (*(this+186) != 5)` — à saturation, le nom reste mais PLUS AUCUNE
        // ligne de salutation n'est dessinée.
        // 0x5dfd97 : for(i<5) UI_DrawNumberValue(51*i + def + 255*(*(this+186)) + 36,
        //                                         *this+22, *(this+1) + 20*i - 121, 1)
        //          == def->textGrid[greetingIdx_][i]  (textGrid @+36, page=255 o, ligne=51 o).
        if (greetingIdx_ != kGreetingSlots) {
            for (int i = 0; i < 5; ++i) {
                const char* raw = npcDef_->textGrid[greetingIdx_][i];
                const std::string line(raw, strnlen(raw, sizeof(npcDef_->textGrid[0][0])));
                if (line.empty()) continue;
                ctx.Text(line.c_str(), panel.x + kNameOffsetX,
                         panel.y + kGreetOffsetY + kGreetPitchY * i, kColText);
            }
        }
    }

    // Code numérique du service, en attendant le mapping d'atlas (cf. TODO de la phase Panels).
    for (int i = 0; i < kMaxServices; ++i) {
        if (serviceCodes_[i] < 1) continue;
        const Rect r = ServiceRowRect(i);
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "service %d", serviceCodes_[i]);
        ctx.Text(lbl, r.x + 4, r.y + 4, kColTextDim);
    }
}

} // namespace ts2::ui

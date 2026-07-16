// Net/GameVarDispatch.cpp — implémentation de Pkt_SetGameVar (opcode 0x16, EA 0x468370).
//
// Traduction byte-exacte du switch d'origine (v63 = sélecteur, v66 = valeur signée).
// Le binaire lit :
//     Crt_Memcpy(&v63, &unk_8156C1, 4);   // sélecteur = payload[0..3]
//     Crt_Memcpy(&v66, &unk_8156C5, 4);   // valeur    = payload[4..7]
// puis `switch (v63)`. On reproduit chaque cas dans l'ordre, en conservant les
// symboles/EA d'origine en commentaire. Les globals modélisés (monnaie, poids,
// points d'attributs, élément) vont dans g_World.self / g_Client.inv ; toute la
// longue traîne de dword_XXXX passe par g_Client.Var(adresse).
//
// Modules non écrits (stubés localement, fidèles à la SIGNATURE, corps // TODO) :
//   Char_CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0)  -> moteur de stats
//   Map_BeginWarpToFactionTown[Ex] (0x55C510/0x55C9A0)-> warp de carte
//   Net_ShopAction_4 (0x5C95C0)                       -> action réseau (boutique)
//   Player_CheckStateDigit (0x511740)                 -> contrôleur joueur
//   sub_5C9870 / UI_Confirm2Dlg_Init (0x5C9870/0x5C9800) -> dialogues UI
//   Crt_StringInit (0x75CAB0)                          -> init de std::string maison
#include "Net/GameVarDispatch.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/GameDatabase.h"
#include "Game/StatEngine.h"                 // StatEngine::CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0)
#include "Game/MapWarp.h"                    // ts2::game::BeginWarpToFactionTown (0x55C510/0x55C9A0)
#include "Game/MotionPoolsCoordResolver.h"  // g_CoordResolver (résolveur de coords ville, 0x5025E0/0x4FD4C0)

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>

namespace ts2::game {
namespace {

// ---------------------------------------------------------------------------
// Adresses d'origine des globals NON modélisés en champ propre (longue traîne).
// Clé stable pour g_Client.Var(addr). On garde le nom du binaire en commentaire.
// ---------------------------------------------------------------------------
constexpr uint32_t A_SysMsgColor      = 0x84DFD8;   // g_SysMsgColor (couleur ligne système)
// Rating d'attaque (4 globals travaillés par le moteur de stats) :
constexpr uint32_t A_RatingBaseMin    = 0x168736C;  // dword_168736C[0]  base min
constexpr uint32_t A_RatingCurMin     = 0x1687370;  // dword_1687370[0]  courant min (clampé)
constexpr uint32_t A_RatingBaseMax    = 0x1687374;  // dword_1687374[0]  base max
constexpr uint32_t A_RatingCurMax     = 0x1687378;  // dword_1687378[0]  courant max (clampé)

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

// Couleur des lignes système (g_SysMsgColor 0x84DFD8) ; blanc par défaut si non fixée.
uint32_t sysColor() {
    uint32_t c = static_cast<uint32_t>(g_Client.VarGet(A_SysMsgColor));
    return c ? c : 0xFFFFFFFFu;
}

// Ligne système localisée : Msg_AppendSystemLine(StrTable005_Get(id), g_SysMsgColor).
void sysMsg(int strId) { g_Client.msg.System(Str(strId), sysColor()); }

// vsnprintf sur format LITTÉRAL connu (les seuls formats codés en dur du binaire).
std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap; va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// FmtFromStrTable(strId, ...) : Crt_Vsnprintf(buf, StrTable005_Get(strId), args...) — le
// format EST l'entrée de table localisée (distinct de fmt() qui prend un format littéral).
// Utilisé quand le binaire passe StrTable005_Get(id) comme 1er arg de Crt_Vsnprintf (0x75CD5F).
std::string FmtFromStrTable(int strId, ...) {
    char buf[1024];
    va_list ap; va_start(ap, strId);
    std::vsnprintf(buf, sizeof(buf), Str(strId).c_str(), ap);   // format = entrée de table
    va_end(ap);
    return std::string(buf);
}

// Char_CalcAttackRatingMin 0x4CD970 — agrégat complet via StatEngine (le this d'origine =
// g_EquipSnapshotScratch 0x8E719C, snapshot d'équipement du self ; équiv. C++ = g_World.self/db).
int Char_CalcAttackRatingMin() { return StatEngine::CalcAttackRatingMin(g_World.self, g_World.db); }
// Char_CalcAttackRatingMax 0x4CE3F0 — idem.
int Char_CalcAttackRatingMax() { return StatEngine::CalcAttackRatingMax(g_World.self, g_World.db); }

// Motif « recalc min + clamp » (cases 47, 77, 91) :
//   base=CalcMin(); if (curMin > base) curMin = base;
void ratingRecalcMinClamp() {
    int mn = Char_CalcAttackRatingMin();
    g_Client.Var(A_RatingBaseMin) = mn;
    if (g_Client.VarGet(A_RatingCurMin) > mn) g_Client.Var(A_RatingCurMin) = mn;
}

// Motif « recalc min ET max avec clamp » (cases 32, 40, 71) :
void ratingRecalcBothClamp() {
    int mn = Char_CalcAttackRatingMin();
    g_Client.Var(A_RatingBaseMin) = mn;
    if (g_Client.VarGet(A_RatingCurMin) > mn) g_Client.Var(A_RatingCurMin) = mn;
    int mx = Char_CalcAttackRatingMax();
    g_Client.Var(A_RatingBaseMax) = mx;
    if (g_Client.VarGet(A_RatingCurMax) > mx) g_Client.Var(A_RatingCurMax) = mx;
}

// Motif « recalc min et max SANS clamp » (cases 68, 95) :
void ratingRecalcSet() {
    g_Client.Var(A_RatingBaseMin) = Char_CalcAttackRatingMin();
    g_Client.Var(A_RatingBaseMax) = Char_CalcAttackRatingMax();
}

// --- Stubs de sous-systèmes non écrits (signatures fidèles) -----------------
//
// [W10 / AUD-02] Le stub no-op `Snd3D_PlayScaledVolume(int,int,int) {}` qui vivait
// ICI a été SUPPRIMÉ : il était doublement trompeur.
//   1) Il masquait le trou aux audits locaux (le code compilait et « appelait un
//      son » qui n'existait pas) ;
//   2) sa SIGNATURE était fausse. Vérité IDA (Snd3D_PlayScaledVolume 0x4DA380,
//      `retn 0Ch` @0x4DA3E7 + `mov [ebp+var_4], ecx` @0x4DA386) :
//        char __thiscall(Snd3D* this, int arg0, int percent, char arg8)
//      — le 1er paramètre est un POINTEUR d'émetteur, pas un id. Le
//      `__userpurge(a2@ebx, a3@edi)` affiché par Hex-Rays est un ARTEFACT
//      (ebx/edi appartiennent à Snd_Play3D, pas au prologue).
//   3) Le 3e argument est MORT : `mov byte ptr [ebp+arg_8], 0` @0x4DA389 écrase
//      l'argument AVANT tout usage, puis `movzx ecx, [ebp+arg_8]` @0x4DA395 ->
//      Snd3D_EnsureLoaded(this, 0). Le `push 1` des 6 sites n'a aucun effet.
//
// ÉMETTEUR DES 6 SITES — identité RÉSOLUE (vague W10). Les 6 sites sont
// byte-identiques : `push 1 / push 64h / push 0 / mov ecx, offset flt_1490A7C /
// call Snd3D_PlayScaledVolume` (arg0=0, percent=100, arg8=1 mort).
// flt_1490A7C n'est pas un global isolé : c'est le slot 189 de la banque de type
// 4 du gestionnaire d'assets —
//   AssetMgr_InitAllSlots 0x4DEB50 (appelant unique App_Init, `mov ecx, offset
//   g_ModelMotionArray` @0x46224B, this = 0x8E8B30) ; boucle banque 4
//   @0x4E05C3..0x4E05F9 : `for (i=0; i<0x19A; ++i) Snd3D_SetISNPath(this +
//   0xB9F18C + 0xC0*i, /*type=*/4, i, 0, 0, 0)` — 410 slots, stride 192.
//   base = 0x8E8B30 + 0xB9F18C = 0x1487CBC ;
//   (0x1490A7C - 0x1487CBC) / 0xC0 = 189 exactement.
// Snd3D_SetISNPath 0x4DA0C0 case 4 = "G03_GDATA\D06_GSOUND\004\E%03d001001.ISN"
// avec (a3+1) => le fichier joué est E190001001.ISN.
//
// BLOQUÉ : aucune banque d'émetteurs Snd3D n'existe côté C++. audio::Emitter
// (Audio/Sound3D.h:72, méthode PlayScaledVolume(int percent, float nowSec) —
// signature correcte) n'est instanciée que par audio::SoundBank
// (World/WorldIntegration.cpp:417), qui est le WSndBank d'AMBIANCE : système
// distinct, sans Snd3D_SetISNPath. Construire la banque relève de Audio/* +
// Asset/* + Game/MotionPools — tous hors périmètre de ce front, et la
// réimplémenter ici ferait double emploi. Les 6 appels sont donc remplacés par
// des TODO ancrés (voir chaque case), et la dépendance est remontée à
// l'orchestrateur. NE PAS re-poser un stub no-op à la place.
//
// Map_BeginWarpToFactionTown 0x55C510 — __thiscall(this=g_LocalPlayerSheet, mode) ; élément =
// g_LocalElement 0x1673194 = g_World.self.element. Résolution + armement des globals de warp
// (pas d'émission réseau : nc reste au défaut nullptr, convention MapWarp.h).
void Map_BeginWarpToFactionTown(int mode) {
    BeginWarpToFactionTown(g_World.self.element, /*ex=*/false, mode, &g_CoordResolver);
}
// Map_BeginWarpToFactionTownEx 0x55C9A0.
void Map_BeginWarpToFactionTownEx(int mode) {
    BeginWarpToFactionTown(g_World.self.element, /*ex=*/true, mode, &g_CoordResolver);
}
// TODO(net) Net_ShopAction_4 0x5C95C0 (fermeture/action boutique).
void Net_ShopAction_4(int /*a*/) {}
// TODO(player) Player_CheckStateDigit 0x511740 (&g_PlayerCmdController).
void Player_CheckStateDigit() {}
// TODO(ui) sub_5C9870 / UI_Confirm2Dlg_Init 0x5C9870/0x5C9800 (dialogue de confirmation).
void UI_Confirm2Dlg_Init() {}
void UI_Confirm2Dlg_Cancel() {}
// TODO(ui) Crt_StringInit 0x75CAB0 (réinit d'un buffer std::string maison).
void Crt_StringInit(uint32_t /*addrByte167611C*/) {}

// Nom localisé d'un item (MobDb_GetEntry(id) + 4 == champ ItemInfo::name).
const char* itemName(uint32_t itemId) {
    const ItemInfo* it = GetItemInfo(itemId);
    return it ? it->name : "";
}

} // namespace

// ---------------------------------------------------------------------------
// Pkt_SetGameVar (EA 0x468370).
// ---------------------------------------------------------------------------
void ApplySetGameVar(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 8) return;

    int32_t selector;   // v63
    int32_t value;      // v66  (signé : comparé à -1, <=0, etc.)
    std::memcpy(&selector, payload + 0, 4);   // unk_8156C1
    std::memcpy(&value,    payload + 4, 4);   // unk_8156C5

    switch (selector) {
    case 1: // Msg système 219
        sysMsg(219); // /*0x4683fd*/ StrTable005_Get(219)
        break;

    case 2: // g_SelfUnspentAttrPoints 0x16731D0
        g_World.self.unspentAttr = value; // /*0x468415*/
        break;

    case 3: // g_Currency 0x1673180 (+ miroir dword_1687254[0])
        g_World.self.currency = value;    // /*0x468422*/
        g_Client.inv.currency = value;
        g_Client.Var(0x1687254) = value;  // dword_1687254[0] /*0x46842b*/
        break;

    case 4:  g_Client.Var(0x16746F4) = value; break; // dword_16746F4 /*0x468439*/
    case 5:  g_Client.Var(0x16746F8) = value; break; // dword_16746F8 /*0x468446*/
    case 6:  g_Client.Var(0x16746A4) = value; break; // dword_16746A4 /*0x468454*/
    case 7:  g_Client.Var(0x16731B0) = value; break; // dword_16731B0 /*0x468462*/

    case 8: // dword_16746E4 + Msg 691
        g_Client.Var(0x16746E4) = value; // /*0x46846f*/
        sysMsg(691);                     // /*0x468486*/
        break;

    case 9: // g_SelfCharInvBlock 0x1673170 (mot de tête) + miroir dword_168724C[0]
        g_Client.Var(0x1673170) = value; // /*0x46849e*/
        g_Client.Var(0x168724C) = value; // /*0x4684a6*/
        break;

    case 10: g_Client.Var(A_RatingCurMin) = value; break; // dword_1687370[0] /*0x4684b4*/
    case 11: g_Client.Var(A_RatingCurMax) = value; break; // dword_1687378[0] /*0x4684c2*/
    case 12: g_Client.Var(0x167325C) = value; break;      // dword_167325C /*0x4684cf*/
    case 13: g_Client.Var(0x16731B0) = value; break;      // dword_16731B0 /*0x4684dd*/

    case 14: // dword_1673260 + miroir dword_16872EC
        g_Client.Var(0x1673260) = value; // /*0x4684eb*/
        g_Client.Var(0x16872EC) = value; // /*0x4684f3*/
        break;

    case 15: // dword_16746E8 + son + Msg 654
        g_Client.Var(0x16746E8) = value;      // /*0x468501*/
        // TODO(audio) [ancre 0x468512] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Bloqué : aucune banque d'émetteurs
        //   Snd3D côté C++ (cf. bloc AUD-02 en tête de fichier).
        sysMsg(654);                          // /*0x468527*/
        break;

    case 16: // ++dword_1673178 ; dword_167317C = value ; ++dword_167478C
        ++g_Client.Var(0x1673178);       // /*0x468545*/
        g_Client.Var(0x167317C) = value; // /*0x46854e*/
        ++g_Client.Var(0x167478C);       // /*0x468559*/
        break;

    case 17: g_Client.Var(0x16746F0) = value; break; // dword_16746F0 /*0x468569*/

    case 18: // dword_1674700 ; si 0 -> warp ville faction
        g_Client.Var(0x1674700) = value;              // /*0x468577*/
        if (!value) Map_BeginWarpToFactionTown(0);    // /*0x46858d*/
        break;

    case 19: // g_InvWeight 0x16732AC
        g_Client.inv.weight = value; // /*0x46859a*/
        break;

    case 20: g_Client.Var(0x16746EC) = value; break; // dword_16746EC /*0x4685a7*/

    case 21: // dword_1674704 ; si 0 -> warp
        g_Client.Var(0x1674704) = value;              // /*0x4685b5*/
        if (!value) Map_BeginWarpToFactionTown(0);    // /*0x4685cb*/
        break;

    case 22: // dword_1674708 ; si 0 && dword_167588C<=0 -> warp
        g_Client.Var(0x1674708) = value;              // /*0x4685d8*/
        if (!value && g_Client.VarGet(0x167588C) <= 0)
            Map_BeginWarpToFactionTown(0);            // /*0x4685f6*/
        break;

    case 23: // (fall-through avec 45) g_InvWeight += value ; Msg "(%d)%s" (value, Str(634))
    case 45:
        g_Client.inv.weight += value; // /*0x468609*/
        g_Client.msg.System(fmt("(%d)%s", value, Str(634).c_str()), sysColor()); // /*0x46862f*/
        break;

    case 24: g_Client.Var(0x16731B4) = value; break; // dword_16731B4 /*0x468657*/

    case 25: // g_InvWeight += value ; Msg "(%d)%s" (value, Str(891))
        g_Client.inv.weight += value; // /*0x46866a*/
        g_Client.msg.System(fmt("(%d)%s", value, Str(891).c_str()), sysColor()); // /*0x468690*/
        break;

    case 26: // dword_1674764 ; si 0 && dword_1687310[0] -> Net_ShopAction_4
        g_Client.Var(0x1674764) = value; // /*0x4686b8*/
        if (!value && g_Client.VarGet(0x1687310))
            Net_ShopAction_4(0);         // /*0x4686d6*/
        break;

    case 27: g_Client.Var(0x1674770) = value; break; // dword_1674770 /*0x4686e3*/

    case 28: // dword_1674768 ; Msg "%s" (Str(988))
        g_Client.Var(0x1674768) = value; // /*0x4686f1*/
        g_Client.msg.System(fmt("%s", Str(988).c_str()), sysColor()); // /*0x468713*/
        break;

    case 29: // dword_167476C ; Msg "%s" (Str(989))
        g_Client.Var(0x167476C) = value; // /*0x46873b*/
        g_Client.msg.System(fmt("%s", Str(989).c_str()), sysColor()); // /*0x46875c*/
        break;

    case 30: g_Client.Var(0x1674778) = value; break; // dword_1674778 /*0x468784*/

    case 31: // dword_1674794 + son + Msg 1185
        g_Client.Var(0x1674794) = value;   // /*0x468792*/
        // TODO(audio) [ancre 0x4687A3] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Bloqué (cf. bloc AUD-02 en tête de fichier).
        sysMsg(1185);                      // /*0x4687b8*/
        break;

    case 32: // dword_167479C ; si 0 -> recalc rating (min+max, clamp)
        g_Client.Var(0x167479C) = value;   // /*0x4687d0*/
        if (!value) ratingRecalcBothClamp(); // /*0x4687e9*/
        break;

    case 33: g_Client.Var(0x16747A0) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x468847*/
    case 34: g_Client.Var(0x16747A4) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x46886a*/
    case 35: g_Client.Var(0x16747A8) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x46888c*/
    case 36: g_Client.Var(0x16747AC) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x4688af*/
    case 37: g_Client.Var(0x16747B0) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x4688d2*/
    case 38: g_Client.Var(0x16747B4) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x4688f4*/
    case 39: g_Client.Var(0x16747B8) = value; if (!value) Map_BeginWarpToFactionTown(0); break; // /*0x468917*/

    case 40: // dword_16747C8 ; si 0 -> recalc rating (min+max, clamp)
        g_Client.Var(0x16747C8) = value;     // /*0x46893a*/
        if (!value) ratingRecalcBothClamp(); // /*0x468952*/
        break;

    case 41: g_Client.Var(0x16747DC) = value; break; // dword_16747DC /*0x4689b0*/
    case 42: g_Client.Var(0x16747E0) = value; break; // dword_16747E0 /*0x4689be*/
    case 43: g_Client.Var(0x16747E4) = value; break; // dword_16747E4 /*0x4689cc*/
    case 44: g_Client.Var(0x16747EC) = value; break; // dword_16747EC /*0x4689d9*/

    case 46: g_Client.Var(0x1674A50) = value; break; // dword_1674A50 /*0x468a3b*/

    case 47: // dword_1674A54 ; recalc min (clamp)
        g_Client.Var(0x1674A54) = value; // /*0x468a49*/
        ratingRecalcMinClamp();          // /*0x468a59*/
        break;

    case 48: g_Client.Var(0x1674A58) = value; break; // dword_1674A58 /*0x468a87*/

    case 49: // dword_1675AFC ; Msg + bannière flottante "(%d)%s" (dword_1675AFC, Str(1479)) ; si <=0 warp
        g_Client.Var(0x1675AFC) = value; // /*0x468a94*/
        {
            std::string s = fmt("(%d)%s", g_Client.VarGet(0x1675AFC), Str(1479).c_str());
            g_Client.msg.Floating(1, 0, s);        // HUD_ShowFloatingMessage(1,0,..) /*0x468ada*/
            g_Client.msg.System(s, sysColor());    // /*0x468af2*/
        }
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x468b04*/
        break;

    case 50: // Msg "(mosterKill: %d)" (value)  [sic: faute d'origine « moster »]
        g_Client.msg.System(fmt("(mosterKill: %d)", value), sysColor()); // /*0x468b38*/
        break;

    case 54: // dword_1674AAC ; si GM (g_GmAuthLevel>0) -> Msg "TimeEffectTime:%d"
        g_Client.Var(0x1674AAC) = value; // /*0x468b45*/
        if (g_Client.VarGet(0x1669294) > 0) // g_GmAuthLevel
            g_Client.msg.System(fmt("TimeEffectTime:%d", g_Client.VarGet(0x1674AAC)), sysColor()); // /*0x468b66*/
        break;

    case 55: // si value==-1 : Msg selon dword_1674AB4 (0..5) ; sinon dword_1674AB0=value + Msg 1706
        if (value == -1) { // /*0x468b8f*/
            switch (g_Client.VarGet(0x1674AB4)) { // dword_1674AB4
            case 0: sysMsg(1888); break; // /*0x468bcb*/
            case 1: sysMsg(1707); break; // /*0x468bf1*/
            case 2: sysMsg(1708); break; // /*0x468c17*/
            case 3: sysMsg(1709); break; // /*0x468c39*/
            case 4: sysMsg(1710); break; // /*0x468c5c*/
            case 5: sysMsg(2228); break; // /*0x468c7f*/
            default: return;             // /*default -> return*/
            }
        } else {
            g_Client.Var(0x1674AB0) = value; // /*0x468c94*/
            sysMsg(1706);                    // /*0x468caa*/
        }
        break;

    case 56: g_Client.Var(0x16760AC) = value; break; // dword_16760AC /*0x468cc2*/
    case 57: g_Client.Var(0x16760B0) = value; break; // dword_16760B0 /*0x468cd0*/
    case 58: g_Client.Var(0x16760B4) = value; break; // dword_16760B4 /*0x468cdd*/

    case 59: // dword_168744C = (value > 0)
        g_Client.Var(0x168744C) = (value > 0) ? 1 : 0; // /*0x468cec*/
        break;

    case 60: // dword_1674A20[value/1e8] = value % 1e8
        g_Client.Var(0x1674A20 + 4u * static_cast<uint32_t>(value / 100000000)) = value % 100000000; // /*0x468d21*/
        break;

    case 61: // g_AutoHuntFuelA 0x16755A4 ; si g_InvDirtyEnable==1 && !fuel -> warpEx
        g_Client.Var(0x16755A4) = value; // /*0x468d30*/
        if (g_Client.VarGet(0x16755AC) == 1 && !g_Client.VarGet(0x16755A4))
            Map_BeginWarpToFactionTownEx(0); // /*0x468d4f*/
        break;

    case 62: // g_AutoHuntFuelB 0x16755A8 ; si 0 -> warpEx
        g_Client.Var(0x16755A8) = value; // /*0x468d5c*/
        if (!value) Map_BeginWarpToFactionTownEx(0); // /*0x468d71*/
        break;

    case 63: // dword_1675638 + Msg 1931
        g_Client.Var(0x1675638) = value; // /*0x468d7e*/
        sysMsg(1931);                    // /*0x468d95*/
        break;

    case 64: g_Client.Var(0x1675634) = value; if (value <= 0) Map_BeginWarpToFactionTown(0); break; // /*0x468dad*/
    case 65: g_Client.Var(0x1675654) = value; if (value <= 0) Map_BeginWarpToFactionTown(0); break; // /*0x468dcc*/

    case 66: // dword_1675678 + miroir dword_168746C
        g_Client.Var(0x1675678) = value; // /*0x468dec*/
        g_Client.Var(0x168746C) = value; // /*0x468df5*/
        break;

    case 67: g_Client.Var(0x167567C) = value; break; // dword_167567C /*0x468e02*/

    case 68: // g_ElementMastery 0x1675680 (on/off) + recalc rating + Msg
        if (value) { // /*0x468e11*/
            g_Client.Var(0x1675680) = value;   // g_ElementMastery /*0x468e16*/
            ratingRecalcSet();                 // /*0x468e26*/
            sysMsg(2086);                      // /*0x468e4a*/
        } else {
            g_Client.Var(0x1675678) = 0;       // /*0x468e5c*/
            g_Client.Var(0x1675680) = 0;       // g_ElementMastery /*0x468e66*/
            g_Client.Var(0x168746C) = 0;       // /*0x468e70*/
            ratingRecalcSet();                 // /*0x468e84*/
        }
        break;

    case 69: g_Client.Var(0x1675684) = value; break; // dword_1675684 /*0x468ea0*/

    case 70: // g_AutoHuntFuelA (idem 61)
        g_Client.Var(0x16755A4) = value; // /*0x468eae*/
        if (g_Client.VarGet(0x16755AC) == 1 && !g_Client.VarGet(0x16755A4))
            Map_BeginWarpToFactionTownEx(0); // /*0x468ecd*/
        break;

    case 71: { // idx = g_TalismanSlot - 10 ; si 0<=idx<10 : dword_167568C[idx]=value + recalc
        int idx = g_Client.VarGet(0x1674760) - 10; // g_TalismanSlot /*0x468f05*/
        if (idx >= 0 && idx < 10) {
            g_Client.Var(0x167568C + 4u * static_cast<uint32_t>(idx)) = value; // /*0x468f1e*/
            ratingRecalcBothClamp(); // /*0x468f2f*/
        }
        break;
    }

    case 72: g_Client.Var(0x167565C) = value; break; // dword_167565C /*0x468eda*/
    case 73: g_Client.Var(0x1674A5C) = value; break; // g_AutoHuntSkillSlotUnlocks /*0x468ee7*/
    case 74: g_Client.Var(0x1674718) = value; break; // dword_1674718 /*0x468ef5*/
    case 75: g_Client.Var(0x16756DC) = value; break; // dword_16756DC /*0x468f8d*/
    case 76: g_Client.Var(0x16756E0) = value; break; // dword_16756E0 /*0x468f9a*/

    case 77: // dword_16756E4 ; recalc min (clamp)
        g_Client.Var(0x16756E4) = value; // /*0x468fa8*/
        ratingRecalcMinClamp();          // /*0x468fb8*/
        break;

    case 78: g_Client.Var(0x16756F0) = value; break; // dword_16756F0 /*0x468fe6*/

    case 79: // dword_16756F4 ; Msg 2153 si value sinon 2152
        g_Client.Var(0x16756F4) = value;     // /*0x468ff3*/
        sysMsg(value ? 2153 : 2152);         // /*0x469035 / 0x469013*/
        break;

    case 80: g_Client.Var(0x16756FC) = value; break; // dword_16756FC /*0x46904d*/

    case 81: // dword_1675704 ; si ==1 Msg 2265
        g_Client.Var(0x1675704) = value; // /*0x46905b*/
        if (value == 1) sysMsg(2265);    // /*0x469077*/
        break;

    case 82: // dword_1675700 ; flt_1687244 = (float)value
        g_Client.Var(0x1675700) = value;                       // /*0x46908f*/
        g_Client.VarF(0x1687244) = static_cast<float>(value);  // /*0x469098*/
        break;

    case 83: g_Client.Var(0x167570C) = value; break; // dword_167570C /*0x4690a6*/
    case 84: g_Client.Var(0x1675708) = value; break; // dword_1675708 /*0x4690b4*/
    case 85: g_Client.Var(0x16756E8) = value; break; // dword_16756E8 /*0x4690c1*/
    case 86: g_Client.Var(0x16756EC) = value; break; // dword_16756EC /*0x4690cf*/
    case 87: g_Client.Var(0x1675710) = value; break; // dword_1675710 /*0x4690dd*/
    case 88: g_Client.Var(0x16732A8) = value; break; // g_Inv_ExtraPageCount /*0x4690ea*/
    case 89: g_Client.Var(0x1673F34) = value; break; // dword_1673F34 /*0x4690f8*/
    case 90: g_Client.Var(0x16755A4) = value; break; // g_AutoHuntFuelA /*0x469106*/

    case 91: // dword_1675728 ; recalc min (clamp)
        g_Client.Var(0x1675728) = value; // /*0x469113*/
        ratingRecalcMinClamp();          // /*0x469123*/
        break;

    case 93: g_Client.Var(0x16760B8) = value; break; // dword_16760B8 /*0x469151*/
    case 94: g_Client.Var(0x16760BC) = value; break; // dword_16760BC /*0x46915f*/

    case 95: // dword_16760C0 ; recalc set (min+max, sans clamp)
        g_Client.Var(0x16760C0) = value; // /*0x46916c*/
        ratingRecalcSet();               // /*0x46917c*/
        break;

    case 97: // warp + Msg 237
        Map_BeginWarpToFactionTown(0); // /*0x46919c*/
        sysMsg(237);                   // /*0x4691b2*/
        break;

    case 98: // dword_1676108 ; si >0 découpe en groupes de chiffres puis init string
        g_Client.Var(0x1676108) = value; // /*0x4691ca*/
        if (value > 0) {
            int v = g_Client.VarGet(0x1676108);
            g_Client.Var(0x167610C) = v / 10000000 + 1;      // &unk_989680 == 0x989680 == 10 000 000 /*0x4691ec*/
            g_Client.Var(0x1676110) = v % 10000000 / 100000; // /*0x469208*/
            g_Client.Var(0x1676114) = v % 100000 / 1000;     // /*0x469224*/
            g_Client.Var(0x1676118) = v % 1000 / 10;         // /*0x469240*/
            // switch(v % 10) : les deux branches font le même Crt_StringInit(&byte_167611C)
            Crt_StringInit(0x167611C); // /*0x46927c / 0x46930b*/
        }
        break;

    case 99: // dword_1675730 + son + Msg 2321
        g_Client.Var(0x1675730) = value;   // /*0x46931b*/
        // TODO(audio) [ancre 0x46932B] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Bloqué (cf. bloc AUD-02 en tête de fichier).
        sysMsg(2321);                      // /*0x469341*/
        break;

    case 100: g_Client.Var(0x16760C4) = value; break; // dword_16760C4 /*0x469359*/
    case 101: g_Client.Var(0x16760C8) = value; break; // dword_16760C8 /*0x469367*/
    case 102: g_Client.Var(0x16760CC) = value; break; // dword_16760CC /*0x469374*/

    case 104: // dword_1675738 + son + Msg 2354
        g_Client.Var(0x1675738) = value;   // /*0x469382*/
        // TODO(audio) [ancre 0x469393] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Bloqué (cf. bloc AUD-02 en tête de fichier).
        sysMsg(2354);                      // /*0x4693a8*/
        break;

    case 105: g_Client.Var(0x16757A0) = value; break; // dword_16757A0 /*0x4693c0*/

    case 106: // dword_16757A8 ; si morph==310 && element != dword_1685E08 && !value -> warp
        g_Client.Var(0x16757A8) = value; // /*0x4693ce*/
        if (g_Client.VarGet(0x1675A98) == 310) { // g_SelfMorphNpcId
            if (g_World.self.element != g_Client.VarGet(0x1685E08) && !g_Client.VarGet(0x16757A8))
                Map_BeginWarpToFactionTown(0); // /*0x4693fd*/
        }
        break;

    case 107: // dword_1674780 ; ouvre/ferme dialogue de confirmation
        g_Client.Var(0x1674780) = value; // /*0x46940a*/
        if (g_Client.VarGet(0x182242C) || g_Client.VarGet(0x1674780) != 1)
            UI_Confirm2Dlg_Cancel();     // sub_5C9870 /*0x469433*/
        else
            UI_Confirm2Dlg_Init();       // /*0x469427*/
        break;

    case 108: // dword_167587C ; si <=0 warp
        g_Client.Var(0x167587C) = value;              // /*0x469440*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x469453*/
        break;

    case 109: // si dword_1675878<1 && value>0 -> Msg 2295 ; dword_1675878 = value
        if (g_Client.VarGet(0x1675878) < 1 && value > 0)
            sysMsg(2295);                 // /*0x46947c*/
        g_Client.Var(0x1675878) = value;  // /*0x46948f*/
        break;

    case 110: // dword_16760D8 / dword_16760DC selon value (0,1,2,3)
        if (value) { // /*0x46949e*/
            switch (value) { // /*0x4694b0*/
            case 1: g_Client.Var(0x16760DC) = 0;  break; // /*0x4694b2*/
            case 2: g_Client.Var(0x16760D8) = 15; break; // /*0x4694c4*/
            case 3: g_Client.Var(0x16760DC) = 15; break; // /*0x4694d6*/
            }
        } else {
            g_Client.Var(0x16760D8) = 0; // /*0x4694a0*/
        }
        break;

    case 111: g_Client.Var(0x1675884) = value; break; // dword_1675884 /*0x4694e8*/

    case 112: { // dword_1675804 ; si <1 : nettoyage ceinture d'auto-potion
        g_Client.Var(0x1675804) = value; // /*0x4694f6*/
        if (value < 1) {
            int slot = g_Client.VarGet(0x1675800); // dword_1675800
            uint32_t beltAddr = 0x16757B0 + 4u * static_cast<uint32_t>(slot); // g_AutoPotionBelt[slot]
            uint32_t cntAddr  = 0x16757D8 + 4u * static_cast<uint32_t>(slot); // dword_16757D8[slot]
            if (g_Client.VarGet(beltAddr) == 878) { // /*0x469515*/
                g_Client.Var(A_RatingBaseMin) = Char_CalcAttackRatingMin(); // /*0x46951c*/
            }
            if (g_Client.VarGet(cntAddr) < 1) { // /*0x469534*/
                g_Client.Var(beltAddr) = 0;     // /*0x46953b*/
            }
        }
        break;
    }

    case 113: // dword_167587C (idem 108)
        g_Client.Var(0x167587C) = value;              // /*0x46954e*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x469561*/
        break;

    case 114: // g_Currency += value ; Crt_Vsnprintf(buf, StrTable005_Get(1845), value) ; couleur 1
        g_World.self.currency += value; // g_Currency 0x1673180 /*0x469574*/
        g_Client.inv.currency += value;
        // Format = entrée de table 1845 (contient le %d) ; arg = value ; couleur littérale 1.
        g_Client.msg.System(FmtFromStrTable(1845, value), 1); // /*0x469588..0x4695ab*/
        break;

    case 115: // dword_1675890 + son + Msg 2528
        g_Client.Var(0x1675890) = value;   // /*0x4695b8*/
        // TODO(audio) [ancre 0x4695C8] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Bloqué (cf. bloc AUD-02 en tête de fichier).
        sysMsg(2528);                      // /*0x4695de*/
        break;

    case 116: g_Client.Var(0x167588C) = value; break; // dword_167588C /*0x4695f6*/

    case 117: // dword_167563C ; si <=0 warp
        g_Client.Var(0x167563C) = value;              // /*0x469604*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x469616*/
        break;

    case 118: // dword_1675894 + Player_CheckStateDigit
        g_Client.Var(0x1675894) = value; // /*0x469623*/
        Player_CheckStateDigit();        // /*0x46962e*/
        break;

    case 119: // dword_1675898 + Player_CheckStateDigit
        g_Client.Var(0x1675898) = value; // /*0x46963b*/
        Player_CheckStateDigit();        // /*0x469646*/
        break;

    case 120: // dword_1675640 + son + Msg 2654
        g_Client.Var(0x1675640) = value;   // /*0x469653*/
        // TODO(audio) [ancre 0x469663] Snd3D_PlayScaledVolume(flt_1490A7C, arg0=0,
        //   percent=100) -> E190001001.ISN. Bloqué (cf. bloc AUD-02 en tête de fichier).
        sysMsg(2654);                      // /*0x469679*/
        break;

    case 121: // dword_1675644 + Msg 2684
        g_Client.Var(0x1675644) = value; // /*0x469691*/
        sysMsg(2684);                    // /*0x4696a7*/
        break;

    case 122: g_Client.Var(0x16760E4) = value; break; // dword_16760E4 /*0x4696bf*/
    case 123: g_Client.Var(0x16760E8) = value; break; // dword_16760E8 /*0x4696cd*/
    case 124: g_Client.Var(0x16760EC) = value; break; // dword_16760EC /*0x4696db*/
    case 125: g_Client.Var(0x16760F0) = value; break; // dword_16760F0 /*0x4696e8*/

    case 126: // (fall-through avec 133) Msg "%s %s %s" (Str(2532), Str(value+2685), Str(2667))
    case 133:
        g_Client.msg.System(
            fmt("%s %s %s", Str(2532).c_str(), Str(value + 2685).c_str(), Str(2667).c_str()),
            sysColor()); // /*0x469734*/
        break;

    case 127: // dword_1675648 ; Msg Str(2995) avec nom item 1833
        g_Client.Var(0x1675648) = value; // /*0x46975c*/
        g_Client.msg.System(Str(2995) + " [" + itemName(1833) + "]", sysColor()); // /*0x469797*/
        break;

    case 128: g_Client.Var(0x167564C) = value; break; // dword_167564C /*0x4697be*/
    case 129: g_Client.Var(0x16760F8) = value; break; // dword_16760F8 /*0x4697cc*/
    case 130: g_Client.Var(0x16760FC) = value; break; // dword_16760FC /*0x4697d9*/
    case 131: g_Client.Var(0x1676100) = value; break; // dword_1676100 /*0x4697e7*/
    case 132: g_Client.Var(0x1676104) = value; break; // dword_1676104 /*0x4697f5*/

    case 146: // dword_1674AA4 ; Msg Str(2995) avec nom item 17210
        g_Client.Var(0x1674AA4) = value; // /*0x469867*/
        g_Client.msg.System(Str(2995) + " [" + itemName(17210) + "]", sysColor()); // /*0x4698a3*/
        break;

    case 147: // dword_1674AA0 ; Msg Str(2995) avec nom item 1956
        g_Client.Var(0x1674AA0) = value; // /*0x4698cb*/
        g_Client.msg.System(Str(2995) + " [" + itemName(1956) + "]", sysColor()); // /*0x469907*/
        break;

    case 148: // dword_167589C ; Msg Str(2995) avec nom item 17576
        g_Client.Var(0x167589C) = value; // /*0x46992f*/
        g_Client.msg.System(Str(2995) + " [" + itemName(17576) + "]", sysColor()); // /*0x46996a*/
        break;

    case 149: // dword_167589C ; si <=0 warp
        g_Client.Var(0x167589C) = value;              // /*0x46998e*/
        if (value <= 0) Map_BeginWarpToFactionTown(0); // /*0x4699a1*/
        break;

    case 150: g_Client.Var(0x1674790) = value; break; // dword_1674790 /*0x4699ab*/
    case 158: g_Client.Var(0x16758A4) = value; break; // dword_16758A4 /*0x4699b5*/

    default: // sélecteurs absents (51-53, 92, 96, 103, 134-145, 151-157, > 158) : no-op
        return;
    }
}

} // namespace ts2::game

// UI/ClanContextMenu.cpp — menu contextuel joueur (UI_ClanWin dword_1822938). Gap SGP-1.
// Voir UI/ClanContextMenu.h pour le layout prouvé, le cycle d'origine et le câblage manquant.
//
// TOUTES les EA citées dans ce fichier ont été extraites du désassemblage (2026-07-16, balayage
// des call sites de UI_ClanWin_OnLDown 0x5D8EF0 / _OnLUp 0x5D92A0 / _Draw 0x5DA210). Elles ne
// sont PAS interpolées. NB : le dossier de gaps situe Net_SendOp65 @0x5D9BDC — la vraie EA du
// `call` est 0x5D9BE8 (0x5D9BDC est le montage des arguments).
//
// Ordre d'inclusion : Net/ EN PREMIER (Net/NetClient.h tire <winsock2.h> avant <windows.h>,
// qu'UI/ClanContextMenu.h tire transitivement via UIManager.h -> <d3d9.h>) — même convention
// que UI/PartyWindow.cpp:6-10 / UI/GuildWindow.cpp / UI/ChatWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 puis windows (ordre sûr)
#include "Net/NetClient.h"     // net::GlobalNetClient() (singleton g_NetClient 0x8156A0)
#include "UI/ClanContextMenu.h"
#include "Game/GameState.h"    // game::g_World (players / partyRoster / allianceRoster / self)
#include "Game/ClientRuntime.h"// game::g_Client (msg, Var/VarGet/Blob), game::Str
#include "Game/SkillCombat.h"  // game::Combat_ReadLocalElementPairs (Char_GetPairedElement)

#include <cstring>             // std::memcpy / std::memset (gate dword_168724C = body[0])
#include <cstdlib>             // std::abs (Math_AbsInt 0x761369)
#include <cstdint>             // uint32_t / int8_t
#include <vector>              // std::vector<uint8_t> (ClientRuntime::Blob)

namespace ts2::ui {

namespace {

// ===========================================================================
// Globals d'origine — conventions DÉJÀ établies dans le dépôt (aucune inventée)
// ===========================================================================
constexpr uint32_t kVarSelfMorphNpcId = 0x1675A98u; // g_SelfMorphNpcId
constexpr uint32_t kVarSysMsgColor    = 0x84DFD8u;  // g_SysMsgColor
constexpr uint32_t kVarNpcMenuGate    = 0x16851B8u; // dword_16851B8 (== 40 -> bouton [6] masqué)
constexpr uint32_t kVarTradePartnerLo = 0x168741Cu; // g_TradePartnerIdLo[0]
constexpr uint32_t kVarGuildTag       = 0x16746A8u; // dword_16746A8 (chaîne, cf. FireOp72)
constexpr uint32_t kVarGuildRank      = 0x16746B8u; // dword_16746B8
constexpr uint32_t kBlobPendingSub1   = 0x1674697u; // g_PendingReqTargetName_Sub1 (13 o)
constexpr uint32_t kBlobPendingSub2   = 0x167468Au; // g_PendingReqTargetName_Sub2 (13 o)

// Registre NoticeDlg (g_NoticeDlg 0x18225C8) : +8 = visible, +16 = type. MÊME modélisation
// que le helper CloseNotice() de Net/GameHandlers_PartyGuild.cpp:86-90 (0x18225C8+8 =
// 0x18225D0 ; +16 = 0x18225D8) — c'est ce qui referme la boucle : Op47 pose la notice 5, que
// le handler 0x36 (AllyJoinResult) referme ; Op53 pose la 6, refermée par le handler 0x3d ;
// Op43 pose la 4, refermée par le handler 0x30.
constexpr uint32_t kVarNoticeVisible = 0x18225D0u;
constexpr uint32_t kVarNoticeType    = 0x18225D8u;

// Ids StrTable005 relevés au désassemblage (l'id est le DERNIER push avant chaque
// `call StrTable005_Get` ; EA citée à chaque site d'emploi ci-dessous).
constexpr int kStrArenaRefused   = 1352; // 0x54B690 -> Open (0x5D8E37)
constexpr int kStrTargetNotFound = 361;  // 0x169 — « cible introuvable » (6 sites, cf. infra)
constexpr int kStrWrongElement   = 366;  // 0x16E — « mauvais élément » (LABEL_136)
constexpr int kStrNotAllowed     = 384;  // 0x180 — refus générique (LABEL_74)

// Map_IsArenaZone 0x54B690 : `return g_SelfMorphNpcId >= 270 && g_SelfMorphNpcId <= 274;`
// RE-VÉRIFIÉ au désassemblage (2026-07-16) avant duplication, car un faux positif bloquerait
// TOUT le menu (l'Open y retourne sans ouvrir) : 0x54B699 `mov eax, g_SelfMorphNpcId` ;
// 0x54B6A1 `cmp var_8, 10Eh` (270) / `jl` -> 0 ; 0x54B6AA `cmp var_8, 112h` (274) / `jle`
// -> 1. Bornes INCLUSIVES des deux côtés.
// DUPLIQUÉ depuis UI/PartyWindow.cpp:39-42 — ce front ne possède pas d'en-tête partagé où
// le mutualiser ; la duplication est celle que PartyWindow.cpp documente déjà
// (« à mutualiser dans une vague ultérieure qui possédera Game/MapWarp.h »).
bool Map_IsArenaZone() {
    const int32_t morphNpcId = game::g_Client.VarGet(kVarSelfMorphNpcId);
    return morphNpcId >= 270 && morphNpcId <= 274;
}

// g_SysMsgColor 0x84DFD8 (longue traîne, cf. UI/PartyWindow.cpp:46-49).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(kVarSysMsgColor));
}

// Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id), g_SysMsgColor).
void SysMsg(int strId) {
    game::g_Client.msg.System(game::Str(strId), SysMsgColor());
}

// UI_NoticeDlg_Open(byte_18225C8, type, StrTable005_Get(g_LangId, strId), "") 0x5C0280.
// Seuls les DEUX champs modélisés (visible/type) sont posés : le texte et le RENDU de la
// notice relèvent du gap SGP-2 (chaîne de confirmation NoticeDlg -> Op44/48/54/60/66/73),
// HORS PÉRIMÈTRE de ce front.
// TODO [ancres 0x5C0280 (_Open) / 0x5C03F0 (_OnLButtonUp) / 0x5C0630 (_Render)] SGP-2 :
//   porter le texte + le rendu + le bouton OK (types 4..9 -> Op44/48/54/66/73/60).
void NoticeOpen(int type, int strId) {
    (void)strId; // le texte n'est pas modélisé (cf. TODO SGP-2 ci-dessus)
    game::g_Client.Var(kVarNoticeVisible) = 1;
    game::g_Client.Var(kVarNoticeType)    = type;
}

// `Crt_Strcmp(s, "") != 0` <=> s[0] != 0 (String 0x7EC95F = chaîne vide, 1er octet NUL).
bool BlobNonEmpty(uint32_t addr) {
    const std::vector<uint8_t>& b = game::g_Client.Blob(addr, 13);
    return !b.empty() && b[0] != 0;
}

// Clan_FindMemberByName 0x5DA830 : scan des entités JOUEUR actives et « visibles » dont le
// nom == this+52. Bornage par players.size() (et non g_EntityCount 0x168721C) : convention
// déjà en place, cf. Net/GameHandlers_PartyGuild.cpp::FindPlayerIndex.
//   g_EntityArray[227*i]  -> players[i].active
//   dword_168724C[227*i]  -> entity+24 = body[0..3] réinterprété en int32 (gate de visibilité ;
//                            sémantique fine non établie, offset PROUVÉ — même idiome que
//                            Game/GroundAuraWorldObjectTick.cpp:229-241)
//   byte_168727C[908*i]   -> entity+72 = players[i].name
int FindClanMemberIndex(const std::string& name) {
    const auto& players = game::g_World.players;
    for (size_t i = 0; i < players.size(); ++i) {
        const game::PlayerEntity& e = players[i];
        if (!e.active) continue;                                  // g_EntityArray[227*i]
        uint32_t visGate = 0;                                     // dword_168724C[227*i]
        std::memcpy(&visGate, e.body.data(), sizeof(visGate));
        if (!visGate) continue;
        if (e.name == name) return static_cast<int>(i);           // Crt_Strcmp(...) == 0
    }
    return -1;
}

bool Clan_FindMemberByName(const std::string& name) {
    return FindClanMemberIndex(name) >= 0;                        // `return i < g_EntityCount;`
}

// Chaîne NUL-terminée logée à `bodyOff` dans le corps de 600 o de l'entité : non vide ?
bool EntityStringNonEmpty(const game::PlayerEntity& e, size_t bodyOff) {
    return bodyOff < e.body.size() && e.body[bodyOff] != 0;
}

// Clan_MemberHasFlagA 0x5DA8C0 : membre trouvé ET `Crt_Strcmp(&g_LocalGuildName + 227*i, "") != 0`.
// L'arithmétique du binaire porte sur un `unsigned int*` => pas 227 octets mais 227*4 = 908
// octets par entité. Cible réelle = g_LocalGuildName(0x168740C) + 908*i, soit entity_i + 0x1D8
// (472) ; le corps modélisé démarre à entity+24 => body[472-24] = body[448].
bool Clan_MemberHasFlagA(const std::string& name) {
    const int i = FindClanMemberIndex(name);
    if (i < 0) return false;
    return EntityStringNonEmpty(game::g_World.players[static_cast<size_t>(i)], 448);
}

// Clan_MemberHasFlagB 0x5DA980 : idem avec `byte_168725C[908*i]` = entity_i + 0x28 (40)
// => body[40-24] = body[16].
bool Clan_MemberHasFlagB(const std::string& name) {
    const int i = FindClanMemberIndex(name);
    if (i < 0) return false;
    return EntityStringNonEmpty(game::g_World.players[static_cast<size_t>(i)], 16);
}

// `g_LocalElement == elem || g_LocalElement == Char_GetPairedElement(g_LocalPlayerSheet, elem)`
// — Char_GetPairedElement 0x557C00 est modélisée par game::ElementPairTable::Paired, alimentée
// par game::Combat_ReadLocalElementPairs() (instantané de g_AlliancePairTable, cf.
// Game/SkillCombat.h:126-151 : le `this` du binaire ne sert QU'À résoudre les 4 paires).
bool ElementMatches(int elem) {
    const int self = game::g_World.self.element;                  // g_LocalElement 0x1673194
    if (self == elem) return true;
    return self == game::Combat_ReadLocalElementPairs().Paired(elem);
}

// Les 3 morphs qui court-circuitent le contrôle d'élément (37 / 119 / 124).
bool MorphBypassesElement() {
    const int32_t m = game::g_Client.VarGet(kVarSelfMorphNpcId);
    return m == 37 || m == 119 || m == 124;
}

// Charge le nom cible en payload 13 o (this+52 côté binaire : buffer de 13 o NUL-terminé).
void PackName13(const std::string& s, char out[13]) {
    std::memset(out, 0, 13);
    const size_t n = s.size() < 12 ? s.size() : 12; // 12 caractères + NUL
    if (n) std::memcpy(out, s.data(), n);
}

// --- Géométrie (panneau plat ; les sprites .IMG ne donnent pas leurs dims en statique) ---
// TODO [ancres 0x5DA239/0x5DA25E (GetWidth/GetHeight unk_8F7608) et 0x5DA6C1/0x5DA6E6
//   (unk_941AA8)] : les dimensions réelles des deux fonds sont lues au RUNTIME et ne sont PAS
//   connues statiquement. Les OFFSETS de boutons, eux, sont PROUVÉS (x+12, y+28+26*i ;
//   x+165/x+241, y+90). Mêmes réserves que UI/PartyWindow.h:110-116.
constexpr int kMenuW = 160;  // largeur déduite (boutons à x+12, largeur 136 => 12+136+12)
constexpr int kMenuH = 220;  // hauteur déduite (dernier bouton à y+184, hauteur 22 => 184+22+14)
constexpr int kMenuBtnX     = 12; // PROUVÉ (*this + 12, les 7 hit-tests)
constexpr int kMenuBtnY     = 28; // PROUVÉ (*(this+1) + 28)
constexpr int kMenuBtnPitch = 26; // PROUVÉ (28/54/80/106/132/158/184)
constexpr int kMenuBtnW = 136; // déduit
constexpr int kMenuBtnH = 22;  // déduit (< pitch 26)

constexpr int kConfW = 340;  // déduit (boutons à x+165 et x+241 => >= 241+68)
constexpr int kConfH = 140;  // déduit (boutons à y+90, hauteur 24 => 90+24+26)
constexpr int kConfBtnTwoX = 165; // PROUVÉ (*this + 165)
constexpr int kConfBtnOneX = 241; // PROUVÉ (*this + 241)
constexpr int kConfBtnY    = 90;  // PROUVÉ (*(this+1) + 90)
constexpr int kConfBtnW = 68;  // déduit (écart 241-165 = 76 => 68 + 8 de gouttière)
constexpr int kConfBtnH = 24;  // déduit

// Palette (couleurs plates, à défaut des sprites .IMG) — alignée sur MsgBoxDialog
// (UI/UIManager.cpp:68-75) pour que les deux popups se ressemblent.
const D3DCOLOR kColBg       = Argb(230,  24,  28,  40);
const D3DCOLOR kColBorder   = Argb(255, 180, 150,  90);
const D3DCOLOR kColBtn      = Argb(255,  56,  64,  88);
const D3DCOLOR kColBtnHover = Argb(255,  84,  96, 128);
const D3DCOLOR kColBtnDown  = Argb(255, 150, 120,  70);
const D3DCOLOR kColText     = Argb(255, 240, 240, 240);
const D3DCOLOR kColTitle    = Argb(255, 255, 214, 140);

// Libellés des 7 entrées du mode 1. UI_ClanWin_Draw 0x5DA210 n'écrit AUCUN texte : les libellés
// sont PEINTS DANS LES SPRITES .IMG eux-mêmes (unk_8F9134, unk_8FB634, …) et n'existent donc
// nulle part sous forme de chaîne — aucune ancre StrTable ne peut être citée ici.
// Conséquence assumée : on n'affiche QUE ce qui est PROUVÉ, et on nomme l'opcode sinon.
//   - « Inviter groupe (Op53) » : PROUVÉ par les gardes de la branche (roster de groupe plein
//     -> Str 490 @0x5D95BB ; cible déjà dans le roster -> Str 531 @0x5D9627), cf. FireOp53.
//   - « Fermer » : PROUVÉ (la branche [9] n'appelle que UI_ClanWin_Close @0x5D9DEC).
//   - « Echange (Op43) » : Op43 = op 0x2B, seul sortant du domaine échange (UI/PlayerTrade
//     Window.h:18-23) ; le bouton [3] bascule vers la page qui l'émet.
//   - Op47/Op59/Op65/Op72 : Net/Opcodes.h:234/246/252/259 ne les documente que comme
//     « requete relation/clan par nom cible (UI_ClanWin) », TOUS marqués (PLAUSIBLE) — la
//     nature exacte de la relation (ami / disciple / alliance / guilde) N'EST PAS établie.
// TODO [ancres 0x5DA2FF (unk_8F9134) / 0x5DA38C (unk_8FB634) / 0x5DA419 (unk_92DC1C) /
//   0x5DA4B1 (unk_923880) / 0x5DA546 (unk_8F8A44) / 0x5DA5DD (unk_8F8C00)] : extraire les
//   libellés réels des sprites .IMG de l'atlas 001 (ou établir la nature des 4 relations
//   côté serveur) avant de figer un texte lisible.
const char* const kMenuLabels[7] = {
    "Echange (Op43)...",      // [3]  -> bascule mode 2
    "Op47",                   // [4]
    "Inviter groupe (Op53)",  // [5]  (prouvé par les gardes de roster)
    "Op59",                   // [6]
    "Op65",                   // [7]
    "Op72",                   // [8]
    "Fermer",                 // [9]  (prouvé : Close seul)
};

} // namespace

// ===========================================================================
// Cycle de vie
// ===========================================================================

// UI_ClanWin_Open 0x5D8E10.
void ClanContextMenu::OpenForPlayer(const std::string& targetName, int level, int levelBonus,
                                    int field19, int element) {
    // Garde arène : message + RETOUR SANS OUVRIR (0x5D8E1E -> 0x5D8E42).
    if (Map_IsArenaZone()) {
        SysMsg(kStrArenaRefused);          // 0x5D8E37 (StrTable005_Get(g_LangId, 1352))
        return;                            // le binaire `return` : ni [2], ni [12], ni nom.
    }
    UIManager::Instance().CloseAll();      // 0x5D8E50 UI_CloseAllDialogs(&dword_1821D4C, 1)
    bOpen_ = true;                         // 0x5D8E58 : *(this+2) = 1
    for (int i = 0; i < kLatchCount; ++i)  // 0x5D8E5F : for (i=0; i<9; ++i) *(this+i+3) = 0
        latch_[i] = false;
    mode_       = kModeMenu;               // 0x5D8E8A : *(this+12) = 1
    targetName_ = targetName;              // 0x5D8E9C : Crt_StringInit (strcpy vers this+52)
    level_      = level;                   // 0x5D8EAA : *(this+17) = a3
    levelBonus_ = levelBonus;              // 0x5D8EB3 : *(this+18) = a4
    field19_    = field19;                 // 0x5D8EBC : *(this+19) = a5
    element_    = element;                 // 0x5D8EC5 : *(this+20) = a6
}

// UI_ClanWin_Close 0x5D8ED0 : *(this+2) = 0 (rien d'autre — ni mode, ni nom, ni latches).
void ClanContextMenu::Close() { bOpen_ = false; }

// ===========================================================================
// Géométrie
// ===========================================================================
void ClanContextMenu::LayoutMenu(int screenW, int screenH, Rect& panel, Rect btns[7]) const {
    // x = nWidth/2 - W(unk_8F7608)/2 ; y = nHeight/2 - H(unk_8F7608)/2.
    // Recalculé à l'identique par _Draw (0x5DA239/0x5DA25E), _OnLDown (0x5D8F39/0x5D8F5E)
    // et _OnLUp — les trois refont le même centrage depuis nWidth/nHeight.
    panel.x = screenW / 2 - kMenuW / 2;
    panel.y = screenH / 2 - kMenuH / 2;
    panel.w = kMenuW;
    panel.h = kMenuH;
    for (int i = 0; i < 7; ++i) {
        btns[i].x = panel.x + kMenuBtnX;                       // *this + 12
        btns[i].y = panel.y + kMenuBtnY + kMenuBtnPitch * i;   // *(this+1) + 28 + 26*i
        btns[i].w = kMenuBtnW;
        btns[i].h = kMenuBtnH;
    }
}

void ClanContextMenu::LayoutConfirm(int screenW, int screenH, Rect& panel,
                                    Rect& btnTwo, Rect& btnOne) const {
    // x = nWidth/2 - W(unk_941AA8)/2 ; y = nHeight/2 - H(unk_941AA8)/2.
    // _Draw 0x5DA6C1/0x5DA6E6 ; _OnLDown 0x5D91B1/0x5D91D6.
    panel.x = screenW / 2 - kConfW / 2;
    panel.y = screenH / 2 - kConfH / 2;
    panel.w = kConfW;
    panel.h = kConfH;
    btnTwo = { panel.x + kConfBtnTwoX, panel.y + kConfBtnY, kConfBtnW, kConfBtnH }; // [10]
    btnOne = { panel.x + kConfBtnOneX, panel.y + kConfBtnY, kConfBtnW, kConfBtnH }; // [11]
}

// ===========================================================================
// UI_ClanWin_OnLDown 0x5D8EF0 — hit-test -> son + latch, return 1.
// ===========================================================================
bool ClanContextMenu::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;                 // `if (!*(this+2)) return 0;`

    // TODO(audio) [ancres 0x5D8FA7 / 0x5D8FF4 / 0x5D9041 / 0x5D908E / 0x5D90DD / 0x5D912D /
    //   0x5D917D (mode 1) et 0x5D9222 / 0x5D926F (mode 2)] : chaque hit-test armé joue
    //   Snd3D_PlayScaledVolume(flt_1487E3C, .., 0, 100, 1) (0x4DA380). L'émetteur flt_1487E3C
    //   n'est pas modélisé côté C++ et Audio/* n'est pas possédé par ce front (gap AUD-02,
    //   même famille). Le latch, lui, est posé fidèlement ci-dessous.
    if (mode_ == kModeMenu) {
        Rect panel, btns[7];
        LayoutMenu(lastScreenW_, lastScreenH_, panel, btns);
        // Ordre des 7 hit-tests successifs : 0x5D8F93 [3] / 0x5D8FE0 [4] / 0x5D902D [5] /
        // 0x5D907A [6] / 0x5D90C9 [7] / 0x5D9119 [8] / 0x5D9169 [9].
        for (int i = 0; i < 7; ++i) {
            if (PointInRect(x, y, btns[i].x, btns[i].y, btns[i].w, btns[i].h)) {
                latch_[i] = true;              // *(this + 3 + i) = 1
                return true;
            }
        }
        return true;                           // return 1 même sans hit
    }
    if (mode_ == kModeConfirm) {
        Rect panel, btnTwo, btnOne;
        LayoutConfirm(lastScreenW_, lastScreenH_, panel, btnTwo, btnOne);
        if (PointInRect(x, y, btnTwo.x, btnTwo.y, btnTwo.w, btnTwo.h)) {
            latch_[kLatchOp43Two] = true;      // 0x5D920E -> *(this+10) = 1
        } else if (PointInRect(x, y, btnOne.x, btnOne.y, btnOne.w, btnOne.h)) {
            latch_[kLatchOp43One] = true;      // 0x5D925B -> *(this+11) = 1
        }
        return true;
    }
    return true;                               // mode inconnu : `return 1`
}

// ===========================================================================
// UI_ClanWin_OnLUp 0x5D92A0 — Close() PUIS gardes PUIS émission.
// Chaque branche appelle UI_ClanWin_Close AVANT ses gardes : la fenêtre se ferme même quand
// l'action est refusée. Fidèle, contre-intuitif, conservé tel quel.
// ===========================================================================
bool ClanContextMenu::OnClick(int x, int y) {
    if (!bOpen_) return false;                 // `if (!*(this+2)) return 0;`

    if (mode_ == kModeMenu) {
        Rect panel, btns[7];
        LayoutMenu(lastScreenW_, lastScreenH_, panel, btns);
        auto hit = [&](int i) {
            return PointInRect(x, y, btns[i].x, btns[i].y, btns[i].w, btns[i].h);
        };

        // [3] -> bascule en mode 2. SEULE branche qui ne ferme PAS la fenêtre (hit-test
        // 0x5D9356 : `if (Sprite2D_HitTest(unk_8F9134, ...)) *(this+12) = 2;`).
        if (latch_[kLatchToConfirm]) {
            latch_[kLatchToConfirm] = false;
            if (hit(kLatchToConfirm)) mode_ = kModeConfirm;
            return true;
        }
        if (latch_[kLatchOp47]) {
            latch_[kLatchOp47] = false;
            if (!hit(kLatchOp47)) return true; // hit-test 0x5D93B4
            Close();                           // 0x5D93CA
            FireOp47();
            return true;
        }
        if (latch_[kLatchOp53]) {
            latch_[kLatchOp53] = false;
            if (!hit(kLatchOp53)) return true; // hit-test 0x5D9518
            Close();                           // 0x5D952E
            FireOp53();
            return true;
        }
        if (latch_[kLatchOp59]) {
            latch_[kLatchOp59] = false;
            if (!hit(kLatchOp59)) return true; // hit-test 0x5D96EC
            Close();                           // 0x5D9702
            FireOp59();
            return true;
        }
        if (latch_[kLatchOp65]) {
            latch_[kLatchOp65] = false;
            if (!hit(kLatchOp65)) return true; // hit-test 0x5D994F
            Close();                           // 0x5D9965
            FireOp65();
            return true;
        }
        if (latch_[kLatchOp72]) {
            latch_[kLatchOp72] = false;
            if (!hit(kLatchOp72)) return true; // hit-test 0x5D9C51
            Close();                           // 0x5D9C67
            FireOp72();
            return true;
        }
        if (latch_[kLatchCloseMenu]) {
            latch_[kLatchCloseMenu] = false;
            if (hit(kLatchCloseMenu)) Close(); // hit-test 0x5D9DD6 -> Close 0x5D9DEC
            return true;                       // aucune émission
        }
        return true;                           // aucun latch armé : `return 1`
    }

    if (mode_ == kModeConfirm) {
        Rect panel, btnTwo, btnOne;
        LayoutConfirm(lastScreenW_, lastScreenH_, panel, btnTwo, btnOne);
        if (latch_[kLatchOp43Two]) {
            latch_[kLatchOp43Two] = false;
            if (!PointInRect(x, y, btnTwo.x, btnTwo.y, btnTwo.w, btnTwo.h)) return true;
            Close();                           // hit-test 0x5D9E89 -> Close 0x5D9E9F
            FireOp43(2);                       // 0x5D9F8A : Net_SendOp43(.., this+52, 2)
            return true;
        }
        if (latch_[kLatchOp43One]) {
            latch_[kLatchOp43One] = false;
            if (!PointInRect(x, y, btnOne.x, btnOne.y, btnOne.w, btnOne.h)) return true;
            Close();                           // hit-test 0x5D9FF4 -> Close 0x5DA00A
            FireOp43(1);                       // 0x5DA0F1 : Net_SendOp43(.., this+52, 1)
            return true;
        }
        return true;
    }
    return true;                               // `if (v55 != 2) return 1;`
}

// ===========================================================================
// Émissions — gardes transcrites branche par branche (EA réelles du désassemblage)
// ===========================================================================

// [4] -> Net_SendOp47 @0x5D94B1.
void ClanContextMenu::FireOp47() {
    // Clan_FindMemberByName 0x5D93D2 ; échec -> Str(361) @0x5D93EC.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    // morph 37/119/124 OU élément identique OU élément jumelé (Char_GetPairedElement 0x5D943A).
    if (!(MorphBypassesElement() || ElementMatches(element_))) {
        SysMsg(kStrWrongElement);            // 0x5D9457 (0x16E = 366)
        return;
    }
    if (game::g_World.self.level < 10) {     // g_SelfLevel >= 10
        SysMsg(1135);                        // 0x5D948B (0x46F)
        return;
    }
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return;
    char name13[13];
    PackName13(targetName_, name13);
    net::Net_SendOp47(*nc, name13);          // 0x5D94B1
    NoticeOpen(5, 357);                      // 0x5D94C5 (0x165) + 0x5D94D2 (_Open type 5)
}

// [5] -> Net_SendOp53 @0x5D9685 — INVITATION DE GROUPE (le builder le plus attendu du gap).
void ClanContextMenu::FireOp53() {
    // Clan_FindMemberByName 0x5D9536 ; échec -> Str(361) @0x5D9550.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }

    // 1er slot VIDE de g_PartyRosterNames (la boucle CONTINUE tant que le nom n'est PAS
    // vide) ; i == 10 -> roster plein -> Str(490) @0x5D95BB (0x1EA).
    const auto& roster = game::g_World.partyRoster.names;
    size_t i = 0;
    for (; i < roster.size() && !roster[i].empty(); ++i) {}
    if (i == roster.size()) {
        SysMsg(490);
        return;
    }
    // La cible est-elle DÉJÀ dans le roster ? (boucle sur strcmp(names[j], this+52))
    // j < 10 -> déjà présente -> Str(531) @0x5D9627 (0x213).
    size_t j = 0;
    for (; j < roster.size() && roster[j] != targetName_; ++j) {}
    if (j < roster.size()) {
        SysMsg(531);
        return;
    }
    // ICI le binaire ne teste QUE l'égalité stricte d'élément — PAS de morph, PAS d'élément
    // jumelé (contrairement à Op47/Op43 : aucun call Char_GetPairedElement dans cette
    // branche). Différence réelle, conservée. Refus -> Str(366) @0x5D965F (0x16E).
    if (game::g_World.self.element != element_) {
        SysMsg(kStrWrongElement);
        return;
    }
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return;
    char name13[13];
    PackName13(targetName_, name13);
    net::Net_SendOp53(*nc, name13);          // 0x5D9685
    NoticeOpen(6, 491);                      // 0x5D9699 (0x1EB) + 0x5D96A6 (_Open type 6)
}

// [6] -> Net_SendOp59 @0x5D98E6.
void ClanContextMenu::FireOp59() {
    // dword_16851B8 == 40 -> refus AVANT même la recherche du membre : Str(110) @0x5D971E (0x6E).
    if (game::g_Client.VarGet(kVarNpcMenuGate) == 40) { SysMsg(110); return; }
    // Clan_FindMemberByName 0x5D973B ; échec -> Str(361) @0x5D9755.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    if (game::g_World.self.level < 113) { SysMsg(502); return; }   // 0x5D9788 (0x1F6)
    // Une requête de confirmation est-elle DÉJÀ en cours ?
    // (`Crt_Strcmp(g_PendingReqTargetName_SubN, "") != 0` = buffer NON vide -> refus)
    if (BlobNonEmpty(kBlobPendingSub2)) { SysMsg(503); return; }   // 0x5D97C9 (0x1F7)
    if (BlobNonEmpty(kBlobPendingSub1)) { SysMsg(504); return; }   // 0x5D980A (0x1F8)
    // Égalité STRICTE d'élément (comme Op53) -> refus Str(366) @0x5D9843.
    if (game::g_World.self.element != element_) { SysMsg(kStrWrongElement); return; }
    const int selfBonus = game::g_World.self.levelBonus;           // g_SelfLevelBonus
    if (selfBonus >= 1) {
        if (levelBonus_ >= selfBonus) { SysMsg(1432); return; }    // 0x5D98C0 (0x598)
    } else if (level_ >= game::g_World.self.level) {
        SysMsg(1433);                                              // 0x5D9885 (0x599)
        return;
    }
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return;
    char name13[13];
    PackName13(targetName_, name13);
    net::Net_SendOp59(*nc, name13);          // 0x5D98E6
    NoticeOpen(9, 506);                      // 0x5D98FA (0x1FA) + 0x5D9907 (_Open type 9)
}

// [7] -> Net_SendOp65 @0x5D9BE8 (le dossier annonçait 0x5D9BDC = montage des arguments).
void ClanContextMenu::FireOp65() {
    // Clan_FindMemberByName 0x5D996D ; échec -> Str(361) @0x5D9986.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    if (game::g_Client.VarGet(kVarSelfMorphNpcId) == 324) { SysMsg(372); return; } // 0x5D99BD (0x174)
    const auto& ar = game::g_World.allianceRoster;
    // `if (Crt_Strcmp(g_AllianceRosterNames, "") != 0)` = slot 0 NON vide (alliance existante).
    if (!ar.memberNames[0].empty()) {
        // slot 0 != mon nom -> je ne suis PAS le chef -> LABEL_74 Str(384) @0x5D9A18 (0x180).
        if (!ar.IsLeader(game::g_World.self.localPlayerName)) { SysMsg(kStrNotAllowed); return; }
        // 1er slot VIDE parmi 1..4 ; k == 5 -> alliance pleine -> Str(367) @0x5D9A83 (0x16F).
        int k = 1;
        for (; k < game::AllianceRoster::kMaxSlots && !ar.memberNames[static_cast<size_t>(k)].empty(); ++k) {}
        if (k == game::AllianceRoster::kMaxSlots) { SysMsg(367); return; }
    }
    // Clan_MemberHasFlagA 0x5D9AA0 -> Str(347) @0x5D9AB9 (0x15B).
    if (Clan_MemberHasFlagA(targetName_)) { SysMsg(347); return; }
    // Char_GetPairedElement 0x5D9AED ; refus -> Str(366) @0x5D9B0B.
    if (!ElementMatches(element_)) { SysMsg(kStrWrongElement); return; }
    const int selfBonus = game::g_World.self.levelBonus;
    bool allowed;
    if (levelBonus_ >= 1) {
        allowed = (selfBonus >= 1);
    } else {
        // selfBonus <= 0 && |g_SelfLevel - this[17]| <= 9 (Math_AbsInt 0x5D9B6E).
        allowed = (selfBonus <= 0 && std::abs(game::g_World.self.level - level_) <= 9);
    }
    // Str(348) est dupliqué par le compilateur sur 3 sites : 0x5D9B48 / 0x5D9B8C / 0x5D9BC2.
    if (!allowed) { SysMsg(348); return; }
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return;
    char name13[13];
    PackName13(targetName_, name13);
    net::Net_SendOp65(*nc, name13);          // 0x5D9BE8
    NoticeOpen(7, 359);                      // 0x5D9BFC (0x167) + 0x5D9C09 (_Open type 7)
}

// [8] -> Net_SendOp72 @0x5D9D71.
//
// DÉGRADATION HONNÊTE (signalée au rapport) : la garde d'entrée exige
// `Crt_Strcmp(dword_16746A8, "") != 0`, c.-à-d. « j'appartiens à une guilde ».
// dword_16746A8 est un BUFFER DE CHAÎNE qu'AUCUN site C++ n'écrit à ce jour : les sites
// d'écriture du binaire sont les `Crt_StringInit` du méga-dispatcher 0x53 (cas 1/5/…), déjà
// tracés en TODO(state) dans Net/GameHandlers_PartyGuild.cpp (l'OFFSET de la chaîne DANS le
// blob 0x56C n'est pas établi -> le poser serait une invention, cf. règle « ne jamais
// deviner »). Conséquence : cette branche affichera Str(384) tant que dword_16746A8 n'est pas
// alimenté. C'est la TRANSCRIPTION FIDÈLE de la garde, pas un contournement — les 5 autres
// émissions ne sont pas concernées.
void ClanContextMenu::FireOp72() {
    // Clan_FindMemberByName 0x5D9C6F ; échec -> Str(361) @0x5D9C88.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    // Guilde non vide ET rang < 2 (comparaison NON SIGNÉE) ; sinon LABEL_74 Str(384) @0x5D9CDB.
    const bool hasGuild = BlobNonEmpty(kVarGuildTag);
    const uint32_t rank = static_cast<uint32_t>(game::g_Client.VarGet(kVarGuildRank));
    if (!(hasGuild && rank < 2u)) { SysMsg(kStrNotAllowed); return; }
    // Clan_MemberHasFlagB 0x5D9CF8 -> Str(405) @0x5D9D12 (0x195).
    if (Clan_MemberHasFlagB(targetName_)) { SysMsg(405); return; }
    // Égalité STRICTE d'élément, et message 406 (PAS 366) — spécifique à Op72 : 0x5D9D4B (0x196).
    if (game::g_World.self.element != element_) { SysMsg(406); return; }
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return;
    char name13[13];
    PackName13(targetName_, name13);
    net::Net_SendOp72(*nc, name13);          // 0x5D9D71
    NoticeOpen(8, 397);                      // 0x5D9D85 (0x18D) + 0x5D9D92 (_Open type 8)
}

// [10]/[11] -> Net_SendOp43(nom, flag) @0x5D9F8A (flag 2) / @0x5DA0F1 (flag 1).
// Les deux branches sont des COPIES littérales l'une de l'autre (mêmes gardes, mêmes ids),
// seul le flag diffère — d'où le paramètre plutôt qu'une duplication.
void ClanContextMenu::FireOp43(int8_t flag) {
    // Clan_FindMemberByName 0x5D9EA7 / 0x5DA012 ; échec -> Str(361) @0x5D9EC1 / @0x5DA02C.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    // Un échange est déjà en cours -> LABEL_117 Str(489) @0x5D9EF5 / @0x5DA05F (0x1E9).
    if (game::g_Client.VarGet(kVarTradePartnerLo)) { SysMsg(489); return; }
    // morph 37/119/124 OU élément identique OU jumelé (Char_GetPairedElement 0x5D9F44 /
    // 0x5DA0AE) ; refus -> Str(366) @0x5D9F62 / @0x5DA0CC.
    if (!(MorphBypassesElement() || ElementMatches(element_))) {
        SysMsg(kStrWrongElement);
        return;
    }
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return;
    char name13[13];
    PackName13(targetName_, name13);
    net::Net_SendOp43(*nc, name13, flag);
    NoticeOpen(4, 356);                      // 0x5D9F9E/0x5DA105 (0x164) + 0x5D9FAB/0x5DA112
}

// ===========================================================================
// UI_ClanWin_Draw 0x5DA210
// ===========================================================================
void ClanContextMenu::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;                     // `if (*(this+2))`

    // TODO [ancre 0x5DA27A] : Util_SetClampedU8Field(dword_8E714C, 0) — le binaire force le
    //   curseur au slot 0 quand ce panneau est visible. Gap UTIL-01 (CursorSet::SetActiveSlot
    //   sans appelant) : `cursors_` est membre PRIVÉ de App (App/App.h:43), fichier non
    //   possédé par ce front -> non câblé ici, tracé au rapport.

    Rect panel, btns[7];
    LayoutMenu(ctx.screenW, ctx.screenH, panel, btns);

    // Le panneau du MODE 1 est dessiné DÈS QUE la fenêtre est visible — y compris en mode 2,
    // où le panneau de confirmation est peint PAR-DESSUS (le test `[12] == 2` vient APRÈS les
    // 7 boutons du mode 1, cf. l'ordre des Sprite2D_Draw 0x5DA291..0x5DA697 puis 0x5DA70D).
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(panel.x, panel.y, panel.w, panel.h, kColBg);        // fond 0x5DA291
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);
        for (int i = 0; i < 7; ++i) {
            // Le bouton [6] (Op59) n'est PAS dessiné du tout quand dword_16851B8 == 40 —
            // sauf s'il est déjà pressé : le binaire teste `*(this+6)` d'abord (0x5DA4D0),
            // puis `dword_16851B8 != 40` avant les états survol/normal (0x5DA471/0x5DA4B1).
            if (i == kLatchOp59 && !latch_[i] &&
                game::g_Client.VarGet(kVarNpcMenuGate) == 40) continue;
            const bool hover = PointInRect(cursorX, cursorY, btns[i].x, btns[i].y,
                                           btns[i].w, btns[i].h);
            const D3DCOLOR c = latch_[i] ? kColBtnDown : (hover ? kColBtnHover : kColBtn);
            ctx.FillRect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, c);
            ctx.DrawFrame(btns[i].x, btns[i].y, btns[i].w, btns[i].h, kColBorder, 1);
        }
    } else {
        // Nom de la cible en tête du panneau. Le binaire ne l'écrit PAS (aucun appel texte
        // dans _Draw) : ajout assumé de lisibilité pour le panneau plat, sans ancre.
        const int nameW = ctx.MeasureText(targetName_.c_str());
        ctx.Text(targetName_.c_str(), panel.x + (panel.w - nameW) / 2, panel.y + 8, kColTitle);
        for (int i = 0; i < 7; ++i) {
            if (i == kLatchOp59 && !latch_[i] &&
                game::g_Client.VarGet(kVarNpcMenuGate) == 40) continue;
            ctx.Text(kMenuLabels[i], btns[i].x + 8, btns[i].y + 4, kColText);
        }
    }

    if (mode_ != kModeConfirm) return;       // `result = *(this+12); if (result == 2)`

    Rect cpanel, btnTwo, btnOne;
    LayoutConfirm(ctx.screenW, ctx.screenH, cpanel, btnTwo, btnOne);
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(cpanel.x, cpanel.y, cpanel.w, cpanel.h, kColBg);    // fond 0x5DA70D
        ctx.DrawFrame(cpanel.x, cpanel.y, cpanel.w, cpanel.h, kColBorder, 2);
        // NB fidélité : dans le binaire, les 2 boutons du mode 2 ne sont dessinés QUE
        // pressés (0x5DA783/0x5DA7F9) ou survolés (0x5DA762/0x5DA7D8) — il n'y a PAS d'état
        // « normal » (les boutons au repos sont peints dans le fond unk_941AA8). Le panneau
        // plat n'ayant pas ce fond, on dessine les 3 états pour rester utilisable.
        const bool hTwo = PointInRect(cursorX, cursorY, btnTwo.x, btnTwo.y, btnTwo.w, btnTwo.h);
        const bool hOne = PointInRect(cursorX, cursorY, btnOne.x, btnOne.y, btnOne.w, btnOne.h);
        ctx.FillRect(btnTwo.x, btnTwo.y, btnTwo.w, btnTwo.h,
                     latch_[kLatchOp43Two] ? kColBtnDown : (hTwo ? kColBtnHover : kColBtn));
        ctx.DrawFrame(btnTwo.x, btnTwo.y, btnTwo.w, btnTwo.h, kColBorder, 1);
        ctx.FillRect(btnOne.x, btnOne.y, btnOne.w, btnOne.h,
                     latch_[kLatchOp43One] ? kColBtnDown : (hOne ? kColBtnHover : kColBtn));
        ctx.DrawFrame(btnOne.x, btnOne.y, btnOne.w, btnOne.h, kColBorder, 1);
    } else {
        const int nameW = ctx.MeasureText(targetName_.c_str());
        ctx.Text(targetName_.c_str(), cpanel.x + (cpanel.w - nameW) / 2, cpanel.y + 30, kColTitle);
        // Libellés NEUTRES et DÉLIBÉRÉS : les DEUX boutons émettent Op43 (seul le flag change,
        // 2 à gauche @0x5D9F8A / 1 à droite @0x5DA0F1) — ce n'est donc PAS un oui/non. La
        // sémantique du flag n'est établie NULLE PART (UI/PlayerTradeWindow.h:20-21 constate
        // les 2 émissions sans la trancher, et le binaire n'a aucune autre xref d'Op43).
        // Les nommer « Oui/Non » serait une invention.
        // TODO [ancres 0x5D9F8A (flag 2) / 0x5DA0F1 (flag 1)] : établir la sémantique du flag
        //   côté serveur, puis poser les vrais libellés.
        ctx.Text("Op43 (2)", btnTwo.x + 6, btnTwo.y + 4, kColText);
        ctx.Text("Op43 (1)", btnOne.x + 6, btnOne.y + 4, kColText);
    }
}

} // namespace ts2::ui

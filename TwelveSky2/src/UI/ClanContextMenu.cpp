// UI/ClanContextMenu.cpp — player context menu (UI_ClanWin dword_1822938). Gap SGP-1.
// See UI/ClanContextMenu.h for the proven layout, the original lifecycle, and the missing wiring.
//
// All EAs cited in this file were extracted from the disassembly (2026-07-16, scan of
// UI_ClanWin_OnLDown 0x5D8EF0 / _OnLUp 0x5D92A0 / _Draw 0x5DA210 call sites). They are
// NOT interpolated. NB: the gap tracker lists Net_SendOp65 @0x5D9BDC — the actual `call`
// EA is 0x5D9BE8 (0x5D9BDC is the argument setup).
//
// Include order: Net/ FIRST (Net/NetClient.h pulls <winsock2.h> before <windows.h>,
// which UI/ClanContextMenu.h pulls transitively via UIManager.h -> <d3d9.h>) — same
// convention as UI/PartyWindow.cpp:6-10 / UI/GuildWindow.cpp / UI/ChatWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 then windows (safe order)
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

// Original globals — conventions ALREADY established in the repo (none invented)
constexpr uint32_t kVarSelfMorphNpcId = 0x1675A98u; // g_SelfMorphNpcId
constexpr uint32_t kVarSysMsgColor    = 0x84DFD8u;  // g_SysMsgColor
constexpr uint32_t kVarNpcMenuGate    = 0x16851B8u; // dword_16851B8 (== 40 -> button [6] hidden)
constexpr uint32_t kVarTradePartnerLo = 0x168741Cu; // g_TradePartnerIdLo[0]
constexpr uint32_t kVarGuildTag       = 0x16746A8u; // dword_16746A8 (string, cf. FireOp72)
constexpr uint32_t kVarGuildRank      = 0x16746B8u; // dword_16746B8
constexpr uint32_t kBlobPendingSub1   = 0x1674697u; // g_PendingReqTargetName_Sub1 (13 bytes)
constexpr uint32_t kBlobPendingSub2   = 0x167468Au; // g_PendingReqTargetName_Sub2 (13 bytes)

// NoticeDlg registry (g_NoticeDlg 0x18225C8): +8 = visible, +16 = type. SAME model as the
// CloseNotice() helper in Net/GameHandlers_PartyGuild.cpp:86-90 (0x18225C8+8 = 0x18225D0;
// +16 = 0x18225D8) — this is what closes the loop: Op47 raises notice 5, closed by handler
// 0x36 (AllyJoinResult); Op53 raises 6, closed by handler 0x3d; Op43 raises 4, closed by
// handler 0x30.
constexpr uint32_t kVarNoticeVisible = 0x18225D0u;
constexpr uint32_t kVarNoticeType    = 0x18225D8u;

// StrTable005 ids captured from the disassembly (the id is the LAST push before each
// `call StrTable005_Get`; EA cited at each usage site below).
constexpr int kStrArenaRefused   = 1352; // 0x54B690 -> Open (0x5D8E37)
constexpr int kStrTargetNotFound = 361;  // 0x169 — "target not found" (6 sites, see below)
constexpr int kStrWrongElement   = 366;  // 0x16E — "wrong element" (LABEL_136)
constexpr int kStrNotAllowed     = 384;  // 0x180 — generic refusal (LABEL_74)

// Map_IsArenaZone 0x54B690: `return g_SelfMorphNpcId >= 270 && g_SelfMorphNpcId <= 274;`
// RE-VERIFIED against the disassembly (2026-07-16) before duplicating, since a false positive
// would block the ENTIRE menu (Open would return without opening): 0x54B699 `mov eax,
// g_SelfMorphNpcId`; 0x54B6A1 `cmp var_8, 10Eh` (270) / `jl` -> 0; 0x54B6AA `cmp var_8, 112h`
// (274) / `jle` -> 1. Bounds INCLUSIVE on both ends.
// DUPLICATED from UI/PartyWindow.cpp:39-42 — this front has no shared header to host it in;
// the duplication is the one already documented in PartyWindow.cpp ("to be shared in a later
// wave that owns Game/MapWarp.h").
bool Map_IsArenaZone() {
    const int32_t morphNpcId = game::g_Client.VarGet(kVarSelfMorphNpcId);
    return morphNpcId >= 270 && morphNpcId <= 274;
}

// g_SysMsgColor 0x84DFD8 (long tail, cf. UI/PartyWindow.cpp:46-49).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(kVarSysMsgColor));
}

// Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id), g_SysMsgColor).
void SysMsg(int strId) {
    game::g_Client.msg.System(game::Str(strId), SysMsgColor());
}

// UI_NoticeDlg_Open(byte_18225C8, type, StrTable005_Get(g_LangId, strId), "") 0x5C0280.
// Only the TWO modeled fields (visible/type) are set: the text and RENDERING of the notice
// belong to gap SGP-2 (NoticeDlg confirmation chain -> Op44/48/54/60/66/73), OUT OF SCOPE
// for this front.
// TODO [anchors 0x5C0280 (_Open) / 0x5C03F0 (_OnLButtonUp) / 0x5C0630 (_Render)] SGP-2:
//   port the text + the rendering + the OK button (types 4..9 -> Op44/48/54/66/73/60).
void NoticeOpen(int type, int strId) {
    (void)strId; // text not modeled (cf. TODO SGP-2 above)
    game::g_Client.Var(kVarNoticeVisible) = 1;
    game::g_Client.Var(kVarNoticeType)    = type;
}

// `Crt_Strcmp(s, "") != 0` <=> s[0] != 0 (String 0x7EC95F = empty string, 1st byte NUL).
bool BlobNonEmpty(uint32_t addr) {
    const std::vector<uint8_t>& b = game::g_Client.Blob(addr, 13);
    return !b.empty() && b[0] != 0;
}

// Clan_FindMemberByName 0x5DA830: scans active, "visible" PLAYER entities whose name ==
// this+52. Bounded by players.size() (not g_EntityCount 0x168721C): convention already in
// place, cf. Net/GameHandlers_PartyGuild.cpp::FindPlayerIndex.
//   g_EntityArray[227*i]  -> players[i].active
//   dword_168724C[227*i]  -> entity+24 = body[0..3] reinterpreted as int32 (visibility gate;
//                            exact semantics not established, offset PROVEN — same idiom as
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

// NUL-terminated string stored at `bodyOff` in the entity's 600-byte body: non-empty?
bool EntityStringNonEmpty(const game::PlayerEntity& e, size_t bodyOff) {
    return bodyOff < e.body.size() && e.body[bodyOff] != 0;
}

// Clan_MemberHasFlagA 0x5DA8C0: member found AND `Crt_Strcmp(&g_LocalGuildName + 227*i, "") != 0`.
// The binary's arithmetic is on an `unsigned int*` => not 227 bytes but 227*4 = 908 bytes per
// entity. Real target = g_LocalGuildName(0x168740C) + 908*i, i.e. entity_i + 0x1D8 (472); the
// modeled body starts at entity+24 => body[472-24] = body[448].
bool Clan_MemberHasFlagA(const std::string& name) {
    const int i = FindClanMemberIndex(name);
    if (i < 0) return false;
    return EntityStringNonEmpty(game::g_World.players[static_cast<size_t>(i)], 448);
}

// Clan_MemberHasFlagB 0x5DA980: same, with `byte_168725C[908*i]` = entity_i + 0x28 (40)
// => body[40-24] = body[16].
bool Clan_MemberHasFlagB(const std::string& name) {
    const int i = FindClanMemberIndex(name);
    if (i < 0) return false;
    return EntityStringNonEmpty(game::g_World.players[static_cast<size_t>(i)], 16);
}

// `g_LocalElement == elem || g_LocalElement == Char_GetPairedElement(g_LocalPlayerSheet, elem)`
// — Char_GetPairedElement 0x557C00 is modeled by game::ElementPairTable::Paired, fed by
// game::Combat_ReadLocalElementPairs() (snapshot of g_AlliancePairTable, cf.
// Game/SkillCombat.h:126-151: the binary's `this` is used ONLY to resolve the 4 pairs).
bool ElementMatches(int elem) {
    const int self = game::g_World.self.element;                  // g_LocalElement 0x1673194
    if (self == elem) return true;
    return self == game::Combat_ReadLocalElementPairs().Paired(elem);
}

// The 3 morphs that bypass the element check (37 / 119 / 124).
bool MorphBypassesElement() {
    const int32_t m = game::g_Client.VarGet(kVarSelfMorphNpcId);
    return m == 37 || m == 119 || m == 124;
}

// Loads the target name into the 13-byte payload (this+52 in the binary: NUL-terminated
// 13-byte buffer).
void PackName13(const std::string& s, char out[13]) {
    std::memset(out, 0, 13);
    const size_t n = s.size() < 12 ? s.size() : 12; // 12 chars + NUL
    if (n) std::memcpy(out, s.data(), n);
}

// --- Geometry (flat panel; .IMG sprites don't expose their dims statically) ---
// TODO [anchors 0x5DA239/0x5DA25E (GetWidth/GetHeight unk_8F7608) and 0x5DA6C1/0x5DA6E6
//   (unk_941AA8)]: the actual dimensions of both backgrounds are read at RUNTIME and are NOT
//   known statically. The button OFFSETS, however, are PROVEN (x+12, y+28+26*i;
//   x+165/x+241, y+90). Same caveats as UI/PartyWindow.h:110-116.
constexpr int kMenuW = 160;  // derived width (buttons at x+12, width 136 => 12+136+12)
constexpr int kMenuH = 220;  // derived height (last button at y+184, height 22 => 184+22+14)
constexpr int kMenuBtnX     = 12; // PROVEN (*this + 12, all 7 hit-tests)
constexpr int kMenuBtnY     = 28; // PROVEN (*(this+1) + 28)
constexpr int kMenuBtnPitch = 26; // PROVEN (28/54/80/106/132/158/184)
constexpr int kMenuBtnW = 136; // derived
constexpr int kMenuBtnH = 22;  // derived (< pitch 26)

constexpr int kConfW = 340;  // derived (buttons at x+165 and x+241 => >= 241+68)
constexpr int kConfH = 140;  // derived (buttons at y+90, height 24 => 90+24+26)
constexpr int kConfBtnTwoX = 165; // PROVEN (*this + 165)
constexpr int kConfBtnOneX = 241; // PROVEN (*this + 241)
constexpr int kConfBtnY    = 90;  // PROVEN (*(this+1) + 90)
constexpr int kConfBtnW = 68;  // derived (gap 241-165 = 76 => 68 + 8 gutter)
constexpr int kConfBtnH = 24;  // derived

// Palette (flat colors, standing in for .IMG sprites) — aligned with MsgBoxDialog
// (UI/UIManager.cpp:68-75) so the two popups look alike.
const D3DCOLOR kColBg       = Argb(230,  24,  28,  40);
const D3DCOLOR kColBorder   = Argb(255, 180, 150,  90);
const D3DCOLOR kColBtn      = Argb(255,  56,  64,  88);
const D3DCOLOR kColBtnHover = Argb(255,  84,  96, 128);
const D3DCOLOR kColBtnDown  = Argb(255, 150, 120,  70);
const D3DCOLOR kColText     = Argb(255, 240, 240, 240);
const D3DCOLOR kColTitle    = Argb(255, 255, 214, 140);

// Labels for the 7 mode-1 entries. UI_ClanWin_Draw 0x5DA210 writes NO text at all: the
// labels are PAINTED INTO the .IMG SPRITES themselves (unk_8F9134, unk_8FB634, …) and thus
// don't exist anywhere as a string — no StrTable anchor can be cited here.
// Accepted consequence: we display ONLY what is PROVEN, and name the opcode otherwise.
//   - "Invite to group (Op53)": PROVEN by the branch's guards (party roster full ->
//     Str 490 @0x5D95BB; target already in roster -> Str 531 @0x5D9627), cf. FireOp53.
//   - "Close": PROVEN (branch [9] only calls UI_ClanWin_Close @0x5D9DEC).
//   - "Trade (Op43)": Op43 = op 0x2B, the only outbound in the trade domain
//     (UI/PlayerTradeWindow.h:18-23); button [3] switches to the page that emits it.
//   - Op47/Op59/Op65/Op72: Net/Opcodes.h:234/246/252/259 documents them only as
//     "name-targeted relation/clan request (UI_ClanWin)", ALL marked (PLAUSIBLE) — the exact
//     nature of the relation (friend / disciple / alliance / guild) is NOT established.
// TODO [anchors 0x5DA2FF (unk_8F9134) / 0x5DA38C (unk_8FB634) / 0x5DA419 (unk_92DC1C) /
//   0x5DA4B1 (unk_923880) / 0x5DA546 (unk_8F8A44) / 0x5DA5DD (unk_8F8C00)]: extract the real
//   labels from the 001 atlas .IMG sprites (or establish the nature of the 4 relations
//   server-side) before locking in readable text.
const char* const kMenuLabels[7] = {
    "Trade (Op43)...",        // [3]  -> switches to mode 2
    "Op47",                   // [4]
    "Invite to group (Op53)", // [5]  (proven by the roster guards)
    "Op59",                   // [6]
    "Op65",                   // [7]
    "Op72",                   // [8]
    "Close",                  // [9]  (proven: Close only)
};

} // namespace

// Lifecycle

// UI_ClanWin_Open 0x5D8E10.
void ClanContextMenu::OpenForPlayer(const std::string& targetName, int level, int levelBonus,
                                    int field19, int element) {
    // Arena guard: message + RETURN WITHOUT OPENING (0x5D8E1E -> 0x5D8E42).
    if (Map_IsArenaZone()) {
        SysMsg(kStrArenaRefused);          // 0x5D8E37 (StrTable005_Get(g_LangId, 1352))
        return;                            // the binary `return`s: no [2], no [12], no name.
    }
    UIManager::Instance().CloseAll();      // 0x5D8E50 UI_CloseAllDialogs(&dword_1821D4C, 1)
    bOpen_ = true;                         // 0x5D8E58 : *(this+2) = 1
    for (int i = 0; i < kLatchCount; ++i)  // 0x5D8E5F : for (i=0; i<9; ++i) *(this+i+3) = 0
        latch_[i] = false;
    mode_       = kModeMenu;               // 0x5D8E8A : *(this+12) = 1
    targetName_ = targetName;              // 0x5D8E9C : Crt_StringInit (strcpy to this+52)
    level_      = level;                   // 0x5D8EAA : *(this+17) = a3
    levelBonus_ = levelBonus;              // 0x5D8EB3 : *(this+18) = a4
    field19_    = field19;                 // 0x5D8EBC : *(this+19) = a5
    element_    = element;                 // 0x5D8EC5 : *(this+20) = a6
}

// UI_ClanWin_Close 0x5D8ED0 : *(this+2) = 0 (nothing else — no mode, no name, no latches).
void ClanContextMenu::Close() { bOpen_ = false; }

// Geometry
void ClanContextMenu::LayoutMenu(int screenW, int screenH, Rect& panel, Rect btns[7]) const {
    // x = nWidth/2 - W(unk_8F7608)/2 ; y = nHeight/2 - H(unk_8F7608)/2.
    // Recomputed identically by _Draw (0x5DA239/0x5DA25E), _OnLDown (0x5D8F39/0x5D8F5E)
    // and _OnLUp — all three redo the same centering from nWidth/nHeight.
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

// UI_ClanWin_OnLDown 0x5D8EF0 — hit-test -> sound + latch, return 1.
bool ClanContextMenu::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;                 // `if (!*(this+2)) return 0;`

    // TODO(audio) [anchors 0x5D8FA7 / 0x5D8FF4 / 0x5D9041 / 0x5D908E / 0x5D90DD / 0x5D912D /
    //   0x5D917D (mode 1) and 0x5D9222 / 0x5D926F (mode 2)]: each armed hit-test plays
    //   Snd3D_PlayScaledVolume(flt_1487E3C, .., 0, 100, 1) (0x4DA380). The emitter
    //   flt_1487E3C isn't modeled in C++ and Audio/* isn't owned by this front (gap AUD-02,
    //   same family). The latch itself is set faithfully below.
    if (mode_ == kModeMenu) {
        Rect panel, btns[7];
        LayoutMenu(lastScreenW_, lastScreenH_, panel, btns);
        // Order of the 7 successive hit-tests: 0x5D8F93 [3] / 0x5D8FE0 [4] / 0x5D902D [5] /
        // 0x5D907A [6] / 0x5D90C9 [7] / 0x5D9119 [8] / 0x5D9169 [9].
        for (int i = 0; i < 7; ++i) {
            if (PointInRect(x, y, btns[i].x, btns[i].y, btns[i].w, btns[i].h)) {
                latch_[i] = true;              // *(this + 3 + i) = 1
                return true;
            }
        }
        return true;                           // return 1 even on no hit
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
    return true;                               // unknown mode: `return 1`
}

// UI_ClanWin_OnLUp 0x5D92A0 — Close() THEN guards THEN emission.
// Every branch calls UI_ClanWin_Close BEFORE its guards: the window closes even when the
// action is refused. Faithful, counter-intuitive, kept as-is.
bool ClanContextMenu::OnClick(int x, int y) {
    if (!bOpen_) return false;                 // `if (!*(this+2)) return 0;`

    if (mode_ == kModeMenu) {
        Rect panel, btns[7];
        LayoutMenu(lastScreenW_, lastScreenH_, panel, btns);
        auto hit = [&](int i) {
            return PointInRect(x, y, btns[i].x, btns[i].y, btns[i].w, btns[i].h);
        };

        // [3] -> switches to mode 2. The ONLY branch that does NOT close the window (hit-test
        // 0x5D9356: `if (Sprite2D_HitTest(unk_8F9134, ...)) *(this+12) = 2;`).
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
            return true;                       // no emission
        }
        return true;                           // no latch armed: `return 1`
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

// Emissions — guards transcribed branch by branch (real EAs from the disassembly)

// [4] -> Net_SendOp47 @0x5D94B1.
void ClanContextMenu::FireOp47() {
    // Clan_FindMemberByName 0x5D93D2 ; failure -> Str(361) @0x5D93EC.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    // morph 37/119/124 OR identical element OR paired element (Char_GetPairedElement 0x5D943A).
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

// [5] -> Net_SendOp53 @0x5D9685 — GROUP INVITE (the most-awaited builder of this gap).
void ClanContextMenu::FireOp53() {
    // Clan_FindMemberByName 0x5D9536 ; failure -> Str(361) @0x5D9550.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }

    // 1st EMPTY slot of g_PartyRosterNames (the loop CONTINUES while the name is NOT
    // empty); i == 10 -> roster full -> Str(490) @0x5D95BB (0x1EA).
    const auto& roster = game::g_World.partyRoster.names;
    size_t i = 0;
    for (; i < roster.size() && !roster[i].empty(); ++i) {}
    if (i == roster.size()) {
        SysMsg(490);
        return;
    }
    // Is the target ALREADY in the roster? (loop over strcmp(names[j], this+52))
    // j < 10 -> already present -> Str(531) @0x5D9627 (0x213).
    size_t j = 0;
    for (; j < roster.size() && roster[j] != targetName_; ++j) {}
    if (j < roster.size()) {
        SysMsg(531);
        return;
    }
    // HERE the binary tests ONLY strict element equality — NO morph, NO paired element
    // (unlike Op47/Op43: no Char_GetPairedElement call in this branch). Real difference,
    // preserved. Refusal -> Str(366) @0x5D965F (0x16E).
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
    // dword_16851B8 == 40 -> refusal BEFORE even searching for the member: Str(110) @0x5D971E (0x6E).
    if (game::g_Client.VarGet(kVarNpcMenuGate) == 40) { SysMsg(110); return; }
    // Clan_FindMemberByName 0x5D973B ; failure -> Str(361) @0x5D9755.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    if (game::g_World.self.level < 113) { SysMsg(502); return; }   // 0x5D9788 (0x1F6)
    // Is a confirmation request ALREADY in progress?
    // (`Crt_Strcmp(g_PendingReqTargetName_SubN, "") != 0` = buffer NOT empty -> refusal)
    if (BlobNonEmpty(kBlobPendingSub2)) { SysMsg(503); return; }   // 0x5D97C9 (0x1F7)
    if (BlobNonEmpty(kBlobPendingSub1)) { SysMsg(504); return; }   // 0x5D980A (0x1F8)
    // STRICT element equality (like Op53) -> refusal Str(366) @0x5D9843.
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

// [7] -> Net_SendOp65 @0x5D9BE8 (the gap tracker listed 0x5D9BDC = argument setup).
void ClanContextMenu::FireOp65() {
    // Clan_FindMemberByName 0x5D996D ; failure -> Str(361) @0x5D9986.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    if (game::g_Client.VarGet(kVarSelfMorphNpcId) == 324) { SysMsg(372); return; } // 0x5D99BD (0x174)
    const auto& ar = game::g_World.allianceRoster;
    // `if (Crt_Strcmp(g_AllianceRosterNames, "") != 0)` = slot 0 NOT empty (alliance exists).
    if (!ar.memberNames[0].empty()) {
        // slot 0 != my name -> I am NOT the leader -> LABEL_74 Str(384) @0x5D9A18 (0x180).
        if (!ar.IsLeader(game::g_World.self.localPlayerName)) { SysMsg(kStrNotAllowed); return; }
        // 1st EMPTY slot among 1..4 ; k == 5 -> alliance full -> Str(367) @0x5D9A83 (0x16F).
        int k = 1;
        for (; k < game::AllianceRoster::kMaxSlots && !ar.memberNames[static_cast<size_t>(k)].empty(); ++k) {}
        if (k == game::AllianceRoster::kMaxSlots) { SysMsg(367); return; }
    }
    // Clan_MemberHasFlagA 0x5D9AA0 -> Str(347) @0x5D9AB9 (0x15B).
    if (Clan_MemberHasFlagA(targetName_)) { SysMsg(347); return; }
    // Char_GetPairedElement 0x5D9AED ; refusal -> Str(366) @0x5D9B0B.
    if (!ElementMatches(element_)) { SysMsg(kStrWrongElement); return; }
    const int selfBonus = game::g_World.self.levelBonus;
    bool allowed;
    if (levelBonus_ >= 1) {
        allowed = (selfBonus >= 1);
    } else {
        // selfBonus <= 0 && |g_SelfLevel - this[17]| <= 9 (Math_AbsInt 0x5D9B6E).
        allowed = (selfBonus <= 0 && std::abs(game::g_World.self.level - level_) <= 9);
    }
    // Str(348) is duplicated by the compiler at 3 sites: 0x5D9B48 / 0x5D9B8C / 0x5D9BC2.
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
// HONEST DEGRADATION (flagged in the report): the entry guard requires
// `Crt_Strcmp(dword_16746A8, "") != 0`, i.e. "I belong to a guild".
// dword_16746A8 is a STRING BUFFER that NO C++ site writes to as of today: the binary's
// write sites are the `Crt_StringInit` calls in the mega-dispatcher 0x53 (case 1/5/…),
// already tracked as TODO(state) in Net/GameHandlers_PartyGuild.cpp (the string's OFFSET
// WITHIN the 0x56C blob is not established -> setting it would be an invention, cf. the
// "never guess" rule). Consequence: this branch will display Str(384) until dword_16746A8
// is populated. This is a FAITHFUL TRANSCRIPTION of the guard, not a workaround — the other
// 5 emissions are not affected.
void ClanContextMenu::FireOp72() {
    // Clan_FindMemberByName 0x5D9C6F ; failure -> Str(361) @0x5D9C88.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    // Guild non-empty AND rank < 2 (UNSIGNED comparison); otherwise LABEL_74 Str(384) @0x5D9CDB.
    const bool hasGuild = BlobNonEmpty(kVarGuildTag);
    const uint32_t rank = static_cast<uint32_t>(game::g_Client.VarGet(kVarGuildRank));
    if (!(hasGuild && rank < 2u)) { SysMsg(kStrNotAllowed); return; }
    // Clan_MemberHasFlagB 0x5D9CF8 -> Str(405) @0x5D9D12 (0x195).
    if (Clan_MemberHasFlagB(targetName_)) { SysMsg(405); return; }
    // STRICT element equality, and message 406 (NOT 366) — specific to Op72: 0x5D9D4B (0x196).
    if (game::g_World.self.element != element_) { SysMsg(406); return; }
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return;
    char name13[13];
    PackName13(targetName_, name13);
    net::Net_SendOp72(*nc, name13);          // 0x5D9D71
    NoticeOpen(8, 397);                      // 0x5D9D85 (0x18D) + 0x5D9D92 (_Open type 8)
}

// [10]/[11] -> Net_SendOp43(name, flag) @0x5D9F8A (flag 2) / @0x5DA0F1 (flag 1).
// Both branches are LITERAL copies of each other (same guards, same ids), only the flag
// differs — hence the parameter instead of duplication.
void ClanContextMenu::FireOp43(int8_t flag) {
    // Clan_FindMemberByName 0x5D9EA7 / 0x5DA012 ; failure -> Str(361) @0x5D9EC1 / @0x5DA02C.
    if (!Clan_FindMemberByName(targetName_)) { SysMsg(kStrTargetNotFound); return; }
    // A trade is already in progress -> LABEL_117 Str(489) @0x5D9EF5 / @0x5DA05F (0x1E9).
    if (game::g_Client.VarGet(kVarTradePartnerLo)) { SysMsg(489); return; }
    // morph 37/119/124 OR identical or paired element (Char_GetPairedElement 0x5D9F44 /
    // 0x5DA0AE); refusal -> Str(366) @0x5D9F62 / @0x5DA0CC.
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

// UI_ClanWin_Draw 0x5DA210
void ClanContextMenu::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;                     // `if (*(this+2))`

    // TODO [anchor 0x5DA27A]: Util_SetClampedU8Field(dword_8E714C, 0) — the binary forces the
    //   cursor to slot 0 while this panel is visible. Gap UTIL-01 (CursorSet::SetActiveSlot
    //   with no caller): `cursors_` is a PRIVATE member of App (App/App.h:43), a file not
    //   owned by this front -> not wired here, flagged in the report.

    Rect panel, btns[7];
    LayoutMenu(ctx.screenW, ctx.screenH, panel, btns);

    // The MODE-1 panel is drawn AS SOON AS the window is visible — even in mode 2, where the
    // confirmation panel is painted ON TOP (the `[12] == 2` test comes AFTER the mode-1's 7
    // buttons, cf. the Sprite2D_Draw order 0x5DA291..0x5DA697 then 0x5DA70D).
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(panel.x, panel.y, panel.w, panel.h, kColBg);        // background 0x5DA291
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);
        for (int i = 0; i < 7; ++i) {
            // Button [6] (Op59) is NOT drawn at all when dword_16851B8 == 40 — unless it's
            // already pressed: the binary tests `*(this+6)` first (0x5DA4D0), then
            // `dword_16851B8 != 40` before the hover/normal states (0x5DA471/0x5DA4B1).
            if (i == kLatchOp59 && !latch_[i] &&
                game::g_Client.VarGet(kVarNpcMenuGate) == 40) continue;
            const bool hover = PointInRect(cursorX, cursorY, btns[i].x, btns[i].y,
                                           btns[i].w, btns[i].h);
            const D3DCOLOR c = latch_[i] ? kColBtnDown : (hover ? kColBtnHover : kColBtn);
            ctx.FillRect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, c);
            ctx.DrawFrame(btns[i].x, btns[i].y, btns[i].w, btns[i].h, kColBorder, 1);
        }
    } else {
        // Target name at the top of the panel. The binary does NOT write it (no text call in
        // _Draw): a readability addition for the flat panel, with no anchor.
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
        ctx.FillRect(cpanel.x, cpanel.y, cpanel.w, cpanel.h, kColBg);    // background 0x5DA70D
        ctx.DrawFrame(cpanel.x, cpanel.y, cpanel.w, cpanel.h, kColBorder, 2);
        // Fidelity note: in the binary, the 2 mode-2 buttons are drawn ONLY pressed
        // (0x5DA783/0x5DA7F9) or hovered (0x5DA762/0x5DA7D8) — there is NO "normal" state
        // (idle buttons are painted into the unk_941AA8 background). Since the flat panel
        // has no such background, we draw all 3 states to keep it usable.
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
        // NEUTRAL, DELIBERATE labels: BOTH buttons emit Op43 (only the flag changes,
        // 2 on the left @0x5D9F8A / 1 on the right @0x5DA0F1) — so this is NOT a yes/no.
        // The flag's semantics are established NOWHERE (UI/PlayerTradeWindow.h:20-21 notes
        // the 2 emissions without resolving it, and the binary has no other Op43 xref).
        // Naming them "Yes/No" would be an invention.
        // TODO [anchors 0x5D9F8A (flag 2) / 0x5DA0F1 (flag 1)]: establish the flag's
        //   server-side semantics, then set the real labels.
        ctx.Text("Op43 (2)", btnTwo.x + 6, btnTwo.y + 4, kColText);
        ctx.Text("Op43 (1)", btnOne.x + 6, btnOne.y + 4, kColText);
    }
}

} // namespace ts2::ui

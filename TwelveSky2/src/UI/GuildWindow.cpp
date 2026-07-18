// UI/GuildWindow.cpp — "Guild" window implementation. See UI/GuildWindow.h.
//
// Include order: Net/ FIRST (NetClient.h pulls <winsock2.h> before
// <windows.h>, which UI/GuildWindow.h pulls transitively via <d3d9.h>) — same
// convention as UI/ChatWindow.cpp / UI/WarehouseWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h: winsock2 then windows (order matters)
#include "Net/NetClient.h"     // net::GlobalNetClient() — g_NetClient 0x8156A0 singleton
#include "UI/GuildWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameState.h"     // game::g_World.self.localPlayerName / allianceRoster.guildName
#include "Game/ClientRuntime.h" // game::g_Client.msg.System + game::Str (StrTable005_Get)

#include <algorithm>
#include <cstring>

namespace ts2::ui {

namespace {
// Real panel background (best effort): (446,440) template from UI atlas folder
// G03_GDATA/D01_GIMAGE2D/001 — UNCONFIRMED candidate by IDA, chosen by
// aspect-ratio proximity to the Guild panel (300x284, cf. detailed methodology
// in UI/PanelSkin.h). Falls back automatically to kColBg if absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01165.IMG");

// Palette (cf. mission contract — D3DCOLOR = 0xAARRGGBB).
constexpr D3DCOLOR kColBg        = Argb(224,  32,  32,  40);
constexpr D3DCOLOR kColBorder    = Argb(255, 128, 128, 128);
constexpr D3DCOLOR kColHeaderBg  = Argb(255,  40,  40,  56);
constexpr D3DCOLOR kColText      = Argb(255, 255, 255, 255);
constexpr D3DCOLOR kColTitle     = Argb(255, 255, 221, 102);
constexpr D3DCOLOR kColSubtle    = Argb(200, 200, 200, 200);
constexpr D3DCOLOR kColBtn       = Argb(255,  56,  64,  88);
constexpr D3DCOLOR kColHover     = Argb(255,  64,  96, 160);
constexpr D3DCOLOR kColRowAlt    = Argb( 40, 255, 255, 255);
constexpr D3DCOLOR kColRowSel    = Argb(110,  64,  96, 160);
constexpr D3DCOLOR kColError     = Argb(255, 255,  96,  96);
constexpr D3DCOLOR kColErrorHov  = Argb(255, 255, 140, 140);
constexpr D3DCOLOR kColSuccess   = Argb(255,  96, 255,  96);
constexpr D3DCOLOR kColSuccessHv = Argb(255, 140, 255, 140);
constexpr D3DCOLOR kColEditBg    = Argb(255,  20,  20,  28);
constexpr D3DCOLOR kColEditFocus = Argb(255,  40,  40,  64);

// g_Guild+28 (0x1839984) — GUILD MASTER's name.
// UI_GuildMgrWnd_OnClick 0x668B70 gates ALL master actions via
// `Crt_Strcmp((unsigned int*)this + 7, byte_1673184)` — i.e. `*(g_Guild+28) vs
// g_SelfName` — at EA 0x668DEF (invite), 0x668EB9 (announcement), 0x6690EE (alliance),
// 0x66935E (leave), 0x669426 (rank), 0x669583 (kick), 0x669706 (dissolve),
// 0x66984C (action 56). Refusal = StrTable005 #467 (or #466/#566 depending on the path).
//
// This field is explicitly "OUT OF SCOPE" for game::GuildRoster (cf. the header of
// Game/GuildSystem.h: "leader/co-leader @+28/+41/+54"), and this front does not own
// Game/GuildSystem.h (rule #5) -> file-local mirror in the meantime.
//
// TODO [anchor 0x1839984 / Net_OnTeamFormationDispatch 0x491E70]: NOTHING populates this
// mirror today. In the binary, +28 arrives with the 1388-byte guild "blob"
// copied by TeamFormationDispatch before the window opens (the same blob that
// fills name[]/rank[]) — this path is not modeled on the ClientSource side. As long as it's
// empty, IsSelfGuildMaster() returns false and master actions REFUSE (message #467)
// instead of emitting: an HONEST and deliberate degradation, see below.
std::string g_guildMasterName; // g_Guild+28 (0x1839984)
} // namespace

// `*(g_Guild+28) == g_SelfName` — Crt_Strcmp(...) == 0 in the binary.
//
// ASSUMED FIDELITY GAP (documented): the binary does not test for emptiness. Two
// empty strings give Crt_Strcmp == 0 there, so "I am the master" = TRUE. Here both
// sources can be empty (g_guildMasterName is never populated, cf. its TODO;
// localPlayerName is, by UI/LoginScene.cpp:1116 at character selection), and a false
// "it's me" would let a non-master perform master actions. So we require BOTH to be
// non-empty — the SAME "honest degradation" policy already written for
// SelfState::localPlayerName (Game/GameState.h:340-347: "any comparison
// == localPlayerName fails cleanly (never a false 'it's me'")) and for
// AllianceRoster::IsLeader (Game/GameState.h:465-467, `!name.empty() && ...`).
bool GuildWindow::IsSelfGuildMaster() const {
    const std::string& self = game::g_World.self.localPlayerName; // byte_1673184
    return !self.empty() && !g_guildMasterName.empty() && g_guildMasterName == self;
}

// `*(g_Guild+428)` = selected row (-1 = none); the name lives at
// `this + 130*page + 13*row + 67` (UI_GuildMgrWnd_OnClick 0x668B70 @0x669180/0x6694B9/
// 0x669615). Here the page/row indexing is replaced by the absolute slot 0..49
// (assumed layout deviation, cf. UI/GuildWindow.h) — the SENT name remains the same.
std::string GuildWindow::SelectedMemberName() const {
    if (selectedIdx_ < 0 || selectedIdx_ >= game::GuildRoster::kMaxMembers) return std::string();
    return game::g_Guild.members[static_cast<size_t>(selectedIdx_)].name;
}

void GuildWindow::SetFeedback(const std::string& text, D3DCOLOR color) {
    feedback_      = text;
    feedbackColor_ = color;
    feedbackUntil_ = lastGameTimeSec_ + kFeedbackDurationSec;
}

GuildWindow::GuildWindow() {
    // Max name length = kNameStride - 1 (NUL-terminated, cf. GuildSystem.h).
    nameEdit_.SetMaxLength(static_cast<size_t>(game::GuildRoster::kNameStride - 1));
    nameEdit_.SetTextColor(kColText);
    nameEdit_.SetCaretColor(kColText);
}

// Lifecycle
void GuildWindow::Open() {
    Dialog::Open();
    mode_           = InputMode::None;
    scrollOffset_   = 0;
    selectedIdx_    = -1;   // `*(this+428) = -1` (UI_GuildMgrWnd_Open 0x667E20)
    pressedBtn_     = PressedBtn::None;
    pressedKickIdx_ = -1;
    pressedRowIdx_  = -1;
    feedback_.clear();
    feedbackUntil_  = -1.0f;
    nameEdit_.Clear();
    nameEdit_.SetFocused(false);

    // TODO [anchor UI_GuildCreate_Open 0x667DA0 @0x667e17] builder PRESENT but the
    // guards are not modeled: the binary requests the roster from the server via
    // Net_SendGuarded_2(1) (Op75 sub-op 2) AFTER two guards this front cannot
    // reproduce —
    //   (a) `Map_IsArenaZone() 0x54B690` -> refusal StrTable005 #1352: no equivalent
    //       helper exists on the ClientSource side ("arena zone" state is PASSED as a
    //       parameter where it's used, cf. Game/ComboPickupTick.cpp:152/155);
    //   (b) `Crt_Strcmp(dword_16746A8, "")` -> refusal #462: dword_16746A8 is a global
    //       DISTINCT from g_LocalGuildName 0x168740C (= game::g_World.allianceRoster
    //       .guildName) — do NOT conflate them, its exact role remains disputed (cf.
    //       Game/NameplateLogic.h:43-50, Game/ChatCommands.cpp:104-105).
    // Emitting without these guards would be less faithful than not emitting: we don't.
    // To be wired once (a) and (b) are modeled.
}

void GuildWindow::Close() {
    Dialog::Close();
    mode_ = InputMode::None;
    nameEdit_.SetFocused(false);
}

// Geometry
// RE-VERIFIED by fresh decompilation (window-position audit, 2026-07-14):
// UI_ClanWin_Draw 0x5DA210 (the real original Guild/Clan window) also re-centers
// ITS position EVERY frame with exactly this formula:
//   *this     = nWidth/2  - Sprite2D_GetWidth(bg)/2   (dword_1669184 = nWidth)
//   *(this+1) = nHeight/2 - Sprite2D_GetHeight(bg)/2  (dword_1669188 = nHeight)
// where nWidth/nHeight are the CURRENT SCREEN resolution (not a fixed reference
// resolution of 1024x768) -> NO stray scale/offset factor: the
// `screenW/2 - kPanelW/2` formula below is therefore bit-exact to the binary. Only the
// kPanelW/kPanelH dimensions remain an approximation (the real background sprite's
// width/height are not static constants: they are read at runtime at offsets +108/+112
// of a Sprite2D lazily loaded from an .IMG absent from the disassembled IDB,
// cf. Sprite2D_GetWidth/GetHeight 0x4D6CD0/0x4D6D20).
// RE-CONFIRMED (W6, 2026-07-16): UI_GuildMgrWnd_OnClick 0x668B70 re-centers identically
// on EVERY page case (@0x668bc0/0x668bd9 page 1, 0x66996a page 2, 0x669a95 page 3,
// 0x669da0 page 4, 0x669f30 page 5) — `nWidth/2 - Sprite2D_GetWidth(...)/2`. The
// centering therefore remains the only part of this geometry proven bit-exact.
// The layout (paging vs. scroll, columns, 7-icon-button strip) remains an
// assumed reinvention — cf. the header of UI/GuildWindow.h.
GuildWindow::Geom GuildWindow::ComputeGeometry(int screenW, int screenH) const {
    Geom g;
    g.panel = { screenW / 2 - kPanelW / 2, screenH / 2 - kPanelH / 2, kPanelW, kPanelH };
    g.header = { g.panel.x, g.panel.y, kPanelW, kHeaderH };
    g.closeBtn = { g.panel.x + kPanelW - kCloseBtnSize - 6,
                   g.panel.y + (kHeaderH - kCloseBtnSize) / 2,
                   kCloseBtnSize, kCloseBtnSize };

    g.listArea = { g.panel.x + kMargin, g.panel.y + kHeaderH + kListGap,
                   kPanelW - 2 * kMargin - kScrollBtnSize - 4, kVisibleRows * kRowH };
    g.scrollUp   = { g.listArea.x + g.listArea.w + 4, g.listArea.y,
                     kScrollBtnSize, kScrollBtnSize };
    g.scrollDown = { g.listArea.x + g.listArea.w + 4,
                     g.listArea.y + g.listArea.h - kScrollBtnSize,
                     kScrollBtnSize, kScrollBtnSize };

    g.actionRow = { g.panel.x + kMargin, g.listArea.y + g.listArea.h + kListGap,
                    kPanelW - 2 * kMargin, kActionH };

    if (mode_ == InputMode::None) {
        // 4 action buttons: 66 + 4 + 54 + 4 + 66 + 4 + 86 == 284 == actionRow.w.
        g.addBtn      = { g.actionRow.x,       g.actionRow.y, 66, kActionH };
        g.rankBtn     = { g.actionRow.x + 70,  g.actionRow.y, 54, kActionH };
        g.leaveBtn    = { g.actionRow.x + 128, g.actionRow.y, 66, kActionH };
        g.dissolveBtn = { g.actionRow.x + 198, g.actionRow.y, 86, kActionH };
        g.editBox = g.confirmBtn = g.cancelBtn = Rect{ 0, 0, 0, 0 };
    } else {
        g.editBox    = { g.actionRow.x, g.actionRow.y, 130, kActionH };
        g.confirmBtn = { g.editBox.x + g.editBox.w + 6, g.actionRow.y, 64, kActionH };
        g.cancelBtn  = { g.confirmBtn.x + g.confirmBtn.w + 6, g.actionRow.y, 56, kActionH };
        g.addBtn = g.rankBtn = g.leaveBtn = g.dissolveBtn = Rect{ 0, 0, 0, 0 };
    }

    g.feedbackArea = { g.panel.x + kMargin, g.actionRow.y + kActionH + 4,
                       kPanelW - 2 * kMargin, kFeedbackH };
    return g;
}

GuildWindow::Rect GuildWindow::RowRect(const Geom& g, int rowOnScreen) const {
    return { g.listArea.x, g.listArea.y + rowOnScreen * kRowH, g.listArea.w, kRowH };
}

GuildWindow::Rect GuildWindow::KickRect(const Geom& g, int rowOnScreen) const {
    const Rect r = RowRect(g, rowOnScreen);
    return { r.x + r.w - kKickBtnSize - 2, r.y + (kRowH - kKickBtnSize) / 2,
             kKickBtnSize, kKickBtnSize };
}

std::vector<int> GuildWindow::VisibleMemberIndices() const {
    std::vector<int> out;
    out.reserve(game::GuildRoster::kMaxMembers);
    for (int i = 0; i < game::GuildRoster::kMaxMembers; ++i)
        if (!game::g_Guild.members[static_cast<size_t>(i)].Empty())
            out.push_back(i);
    return out;
}

int GuildWindow::MaxScroll() const {
    const int n = static_cast<int>(VisibleMemberIndices().size());
    return std::max(0, n - kVisibleRows);
}

// Network actions
// ALL emissions below read net::GlobalNetClient() (Net/NetClient.h:67-68)
// = restoring the g_NetClient 0x8156A0 singleton that the binary's builders address
// globally without receiving it as a parameter. The pointer is set by
// ConnectLoginServer/ConnectGameServer (Net/Login.cpp:131/313): it is NON-NULL as soon
// as a session has started — unlike the former net_ member (never Bind()ed,
// so always null = dead code), removed by this wave.
//
// FIDELITY GAP COMMON TO KICK/LEAVE/DISSOLVE (documented, not hidden): the binary
// does NOT emit on click. It opens a confirmation MsgBox (UI_MsgBox_Open 0x5C08C0 on
// dword_1822438) and it's its release (UI_MsgBox_OnLButtonUp 0x5C1170, jump table
// 005C0BE5) that emits — case 16 -> Guarded_6 (@0x5c1181), case 17 -> Guarded_8
// (@0x5c119a), case 18 -> Guarded_4 (@0x5c11ae). This window is not wired to
// that MsgBox registry (game::g_Client.prompt models the state, not the routing of the 60+
// jump-table cases): we emit directly on click, AFTER reproducing the same
// guards. The PACKET ON THE WIRE is identical; only the confirmation step is missing.

// Row "X" -> kick. Anchor: UI_GuildMgrWnd_OnClick 0x668B70, branch
// `*(this+409)` @0x669527 (button unk_904878, x+213 y+69) -> MsgBox 17 (StrTable005
// #473) -> UI_MsgBox_OnLButtonUp @0x5c1190-0x5c119a: Net_SendGuarded_8(byte_183A0F4).
void GuildWindow::DoKick() {
    // Guard 1 @0x669583: must be the master, else LABEL_70 -> StrTable005 #467.
    if (!IsSelfGuildMaster()) { game::g_Client.msg.System(game::Str(467)); return; }
    // Guard 2 @0x6695C4: a row must be selected (`*(this+428) != -1`), else
    // LABEL_93 -> #472. In the binary "Kick" is a PAGE button (unk_904878,
    // x+213 y+69) acting on the selected row, hence this check. Here the "X"
    // is PER ROW (layout invention, cf. header of the .h): the targeted row is
    // implicit, so this guard is structurally satisfied. Kept anyway
    // — it remains the binary's control, and covers an inconsistent latch.
    if (pressedKickIdx_ < 0) { game::g_Client.msg.System(game::Str(472)); return; }

    const std::string target =
        game::g_Guild.members[static_cast<size_t>(pressedKickIdx_)].name;
    // Guard 3 @0x669615/0x669626: Crt_Strcmp(member, g_SelfName) == 0 (the target is
    // self) -> refusal #474; otherwise -> MsgBox 17 then send.
    if (target.empty() || target == game::g_World.self.localPlayerName) {
        game::g_Client.msg.System(game::Str(474));
        return;
    }

    net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
    if (!nc) return;                              // no session: nothing to emit

    // Net_SendGuarded_8 0x593420 (Net/SendPackets.h:185): Op75 sub-op 8, 13-byte payload.
    // TODO [anchor 0x183A0F4]: the binary sends byte_183A0F4 (= g_Guild+1932), NOT
    // `members[row]` directly. All 9 xrefs of byte_183A0F4 are READS
    // (Net_OnTeamFormationDispatch 0x491E70 @0x49286A/0x492892/0x492D4D/0x492D5F/
    // 0x492D87; UI_MsgBox_OnLButtonUp @0x5C1190/0x5C1224/0x5C123F/0x5C1D99) — NO
    // write site located statically (UI_GuildMgrWnd_OnMouseDown 0x667F10 only sets
    // `*(this+428)=i`, the index, without copying the name; write likely via a
    // computed address, to be observed dynamically via x32dbg). Sending the name of the
    // selected row is an INFERENCE — supported by guard 3 above, which validates
    // precisely the SELECTED ROW's name right before opening MsgBox 17 (it would be
    // pointless if +1932 held something else) — but it remains UNPROVEN. To confirm.
    char name13[13] = {};
    const size_t n = std::min(target.size(), sizeof(name13) - 1);
    std::memcpy(name13, target.data(), n);
    net::Net_SendGuarded_8(*nc, name13);

    // NO local mutation: the binary only removes the member on the SERVER RESPONSE
    // (Net_OnTeamFormationDispatch 0x491E70 case 8 @0x492874-0x492923 -> RemoveMember).
    // The former `game::g_Guild.RemoveMember(...)` call here from Pass 3 was an optimistic
    // update the binary does NOT do — removed.
    SetFeedback("Expulsion demandee : " + target, kColError);
}

// "Leave" -> UI_GuildMgrWnd_OnClick 0x668B70, branch `*(this+407)` @0x669305
// (button unk_904D18, x+101 y+69) -> MsgBox 18 (StrTable005 #477) ->
// UI_MsgBox_OnLButtonUp @0x5c11a9-0x5c11ae: Net_SendGuarded_4().
void GuildWindow::DoLeave() {
    // Guard @0x66935E — INVERTED compared to Dissolve: Crt_Strcmp(master, self) == 0
    // (I AM the master) -> refusal StrTable005 #466; a master doesn't "leave", they
    // dissolve. The refusal only triggers when IsSelfGuildMaster() is reliable: as long
    // as the g_guildMasterName mirror is empty (cf. its TODO), it returns false and this
    // path EMITS — this is the behavior of a non-master, hence the majority case.
    if (IsSelfGuildMaster()) { game::g_Client.msg.System(game::Str(466)); return; }

    net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
    if (!nc) return;

    // Net_SendGuarded_4 0x593210 (Net/SendPackets.h:68): Op75 sub-op 4, no payload.
    net::Net_SendGuarded_4(*nc);
    SetFeedback("Depart de la guilde demande.", kColError);
}

// "Dissolve" -> UI_GuildMgrWnd_OnClick 0x668B70, branch `*(this+410)` @0x6696AA
// (button unk_904E40, x+269 y+69) -> MsgBox 16 (StrTable005 #469) ->
// UI_MsgBox_OnLButtonUp @0x5c117c-0x5c1181: Net_SendGuarded_6().
void GuildWindow::DoDissolve() {
    // Guard @0x669706: Crt_Strcmp(master, self) != 0 (I am NOT the master)
    // -> refusal StrTable005 #467.
    if (!IsSelfGuildMaster()) { game::g_Client.msg.System(game::Str(467)); return; }

    net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
    if (!nc) return;

    // Net_SendGuarded_6 0x593320 (Net/SendPackets.h:127): Op75 sub-op 6, no payload.
    net::Net_SendGuarded_6(*nc);
    SetFeedback("Dissolution demandee.", kColError);
}

// Input validation (mode_ == AddMember | SetRank)
void GuildWindow::Confirm() {
    if (mode_ == InputMode::AddMember) {
        // Anchor: Guild_AddMemberFromInput 0x66BCD0 (sole original caller of
        // Net_SendOp76, unique xref @0x66bd5b), triggered by UI_GuildMgrWnd_OnClick
        // 0x668B70 page 2, branch `*(this+411)` @0x6699fc (button unk_906E0C,
        // x+272 y+71).
        const std::string name = nameEdit_.Text();

        // @0x66bcfc-0x66bd04: GetWindowTextA == 0 (empty input) -> immediate return,
        // NO message and NO send. Stays in input mode.
        if (name.empty()) return;

        // @0x66bd14: SetWindowTextA(dword_1668FE0, "") — the box is cleared BEFORE
        // the dictionary check.
        nameEdit_.Clear();

        // @0x66bd26: maybe_Dict001_MatchWord(g_BannedWordList, name) -> banned word ->
        // StrTable005 #112 and NO send. The dictionary (0x4C1410) is out of scope
        // for Game/GuildSystem.h -> `banned` always false here (cf. GuildRoster::AddMember,
        // "banned is computed by the caller"), so this refusal never triggers
        // yet. GuildRoster::AddMember is a const PREDICATE (it does NOT mutate members[]):
        // it returns false iff the name is empty or banned — emptiness already handled
        // above, so a false here means "banned".
        const bool banned = false;
        if (!game::g_Guild.AddMember(name, banned)) {
            game::g_Client.msg.System(game::Str(112));
            return;
        }

        net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
        if (!nc) return;

        // @0x66bd5b: Net_SendOp76(&g_AutoPlayMgr, String) — opcode 0x4C, raw 61-byte
        // payload (Net/SendPackets.h:108). The name read from the edit box is copied as-is
        // (NUL-padded), with no additional structure.
        char name61[61] = {};
        const size_t n = std::min(name.size(), sizeof(name61) - 1);
        std::memcpy(name61, name.data(), n);
        net::Net_SendOp76(*nc, name61);

        // The roster is only updated on the server response -> no local mutation.
        // Guild_AddMemberFromInput does NOT touch `*(this+426)`: we STAY on the
        // "invite" page after sending (only the Cancel button `*(this+412)` @0x669a5d
        // returns to page 1). Faithful.
        SetFeedback("Demande d'ajout envoyee : " + name, kColSuccess);
        return;
    }

    if (mode_ == InputMode::SetRank) {
        // Anchor: UI_GuildMgrWnd_OnClick 0x668B70 page 4, branch `*(this+415)` @0x669DE4
        // (button unk_902C24, x+71 y+39).
        //   @0x669e43: GetWindowTextA(dword_1668FF4, this+1945, 5)
        //   @0x669e78: Net_SendGuarded_10(this + 130*page + 13*row + 67, this+1945)
        //   @0x669e80: *(this+426) = 1  (back to roster page)
        // NO guard at this point: the checks (master / row / target != self) are
        // done when ENTERING page 4 (@0x6693CB, cf. OnClick below). The binary does
        // NOT require a non-empty rank — it sends the 5 bytes as-is. Reproduced.
        const std::string target = SelectedMemberName();
        net::NetClient* nc = net::GlobalNetClient();  // &g_NetClient 0x8156A0
        if (nc && !target.empty()) {
            // Net_SendGuarded_10 0x593550 (Net/SendPackets.h:243): Op75 sub-op 10,
            // 13-byte (name) + 5-byte (rank) CONTIGUOUS payload — both Crt_Memcpy (13 then
            // 5) happen BEFORE the anti-replay guard, cf. decompilation at 0x593580/0x593595.
            // The rank is a 5-byte STRING (buffer this+1945), NOT an integer.
            char name13[13] = {};
            const size_t n = std::min(target.size(), sizeof(name13) - 1);
            std::memcpy(name13, target.data(), n);

            char rank5[5] = {};
            const std::string rankText = nameEdit_.Text();
            const size_t r = std::min(rankText.size(), sizeof(rank5) - 1);
            std::memcpy(rank5, rankText.data(), r);

            net::Net_SendGuarded_10(*nc, name13, rank5);
            SetFeedback("Rang envoye pour " + target + " : " + rankText, kColSuccess);
        }
        // @0x669e80: back to roster page, whether or not the send happened.
        CancelInput();
        return;
    }
}

void GuildWindow::CancelInput() {
    // Original Cancel buttons: page 2 `*(this+412)` @0x669a5d, page 4 `*(this+416)`
    // @0x669ef8 — both simply do `*(this+426) = 1` (back to roster page) and
    // release the edit box's focus (UI_FocusEditBox(&g_UIEditBoxMgr, 0)).
    mode_ = InputMode::None;
    nameEdit_.Clear();
    nameEdit_.SetFocused(false);
    nameEdit_.SetMaxLength(static_cast<size_t>(game::GuildRoster::kNameStride - 1));
}

// Mouse events (latch armed on down, validated on release — MsgBoxDialog pattern)
bool GuildWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    const Geom g = ComputeGeometry(lastScreenW_, lastScreenH_);
    pressedBtn_     = PressedBtn::None;
    pressedKickIdx_ = -1;
    pressedRowIdx_  = -1;

    if (PointInRect(x, y, g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h)) {
        pressedBtn_ = PressedBtn::Close;
    } else if (mode_ != InputMode::None) {
        if (PointInRect(x, y, g.editBox.x, g.editBox.y, g.editBox.w, g.editBox.h)) {
            nameEdit_.SetFocused(true);
        } else if (PointInRect(x, y, g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h)) {
            pressedBtn_ = PressedBtn::Confirm;
        } else if (PointInRect(x, y, g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h)) {
            pressedBtn_ = PressedBtn::Cancel;
        } else {
            nameEdit_.SetFocused(false);
        }
    } else if (PointInRect(x, y, g.addBtn.x, g.addBtn.y, g.addBtn.w, g.addBtn.h)) {
        pressedBtn_ = PressedBtn::Add;
    } else if (PointInRect(x, y, g.rankBtn.x, g.rankBtn.y, g.rankBtn.w, g.rankBtn.h)) {
        pressedBtn_ = PressedBtn::Rank;
    } else if (PointInRect(x, y, g.leaveBtn.x, g.leaveBtn.y, g.leaveBtn.w, g.leaveBtn.h)) {
        pressedBtn_ = PressedBtn::Leave;
    } else if (PointInRect(x, y, g.dissolveBtn.x, g.dissolveBtn.y, g.dissolveBtn.w, g.dissolveBtn.h)) {
        pressedBtn_ = PressedBtn::Dissolve;
    } else if (PointInRect(x, y, g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h)) {
        pressedBtn_ = PressedBtn::ScrollUp;
    } else if (PointInRect(x, y, g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h)) {
        pressedBtn_ = PressedBtn::ScrollDown;
    } else {
        const std::vector<int> visible = VisibleMemberIndices();
        const int rows = std::min(kVisibleRows,
                                   static_cast<int>(visible.size()) - scrollOffset_);
        for (int i = 0; i < rows; ++i) {
            const Rect kr = KickRect(g, i);
            if (PointInRect(x, y, kr.x, kr.y, kr.w, kr.h)) {
                pressedBtn_     = PressedBtn::Kick;
                pressedKickIdx_ = visible[static_cast<size_t>(scrollOffset_ + i)];
                break;
            }
            // Row selection = `*(this+428) = i` (UI_GuildMgrWnd_OnMouseDown
            // 0x667F10); the original selector is highlighted at (x+17, y+20*i+190).
            const Rect r = RowRect(g, i);
            if (PointInRect(x, y, r.x, r.y, r.w, r.h)) {
                pressedBtn_    = PressedBtn::Row;
                pressedRowIdx_ = visible[static_cast<size_t>(scrollOffset_ + i)];
                break;
            }
        }
    }

    return PointInRect(x, y, g.panel.x, g.panel.y, g.panel.w, g.panel.h);
}

bool GuildWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    const Geom g = ComputeGeometry(lastScreenW_, lastScreenH_);
    const bool consumed = PointInRect(x, y, g.panel.x, g.panel.y, g.panel.w, g.panel.h);
    const PressedBtn armed = pressedBtn_;

    // Released before any processing: Confirm()/DoKick() can change the mode and thus
    // the geometry, and no path below should keep a stale latch.
    pressedBtn_ = PressedBtn::None;

    switch (armed) {
        case PressedBtn::Close:
            if (PointInRect(x, y, g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h))
                Close();
            break;

        case PressedBtn::Add:
            if (PointInRect(x, y, g.addBtn.x, g.addBtn.y, g.addBtn.w, g.addBtn.h)) {
                // Entering page 2 — UI_GuildMgrWnd_OnClick 0x668B70, branch `*(this+404)`
                // @0x668D94 (button unk_9043D8, x+157 y+49). Guard @0x668DEF: only the
                // MASTER can open the "invite" page, else StrTable005 #467. This
                // guard was MISSING in Pass 3 (the button was open to everyone).
                if (!IsSelfGuildMaster()) {
                    game::g_Client.msg.System(game::Str(467));
                    break;
                }
                // @0x668E29: *(this+426) = 2, focus edit box 8, SetWindowTextA(…, "").
                mode_ = InputMode::AddMember;
                nameEdit_.SetMaxLength(static_cast<size_t>(game::GuildRoster::kNameStride - 1));
                nameEdit_.Clear();
                nameEdit_.SetFocused(true);
                feedback_.clear();
                feedbackUntil_ = -1.0f;
            }
            break;

        case PressedBtn::Rank:
            if (PointInRect(x, y, g.rankBtn.x, g.rankBtn.y, g.rankBtn.w, g.rankBtn.h)) {
                // Entering page 4 — UI_GuildMgrWnd_OnClick 0x668B70, branch `*(this+408)`
                // @0x6693CB (button unk_904750, x+157 y+69).
                // Guard 1 @0x669426: master, else LABEL_70 -> #467.
                if (!IsSelfGuildMaster()) {
                    game::g_Client.msg.System(game::Str(467));
                    break;
                }
                // Guard 2 @0x669467: selected row, else LABEL_93 -> #472.
                if (selectedIdx_ < 0) {
                    game::g_Client.msg.System(game::Str(472));
                    break;
                }
                // Guard 3 @0x6694B9/0x6694CB: Crt_Strcmp(member, g_SelfName) == 0 (the
                // target is self) -> refusal #556 (NOT #546, which is page 5's
                // symmetric "alliance" refusal @0x669191).
                const std::string target = SelectedMemberName();
                if (target.empty() || target == game::g_World.self.localPlayerName) {
                    game::g_Client.msg.System(game::Str(556));
                    break;
                }
                // @0x6694F3: *(this+426) = 4, focus edit box 13, SetWindowTextA(…, "").
                mode_ = InputMode::SetRank;
                nameEdit_.SetMaxLength(static_cast<size_t>(kRankMaxChars));
                nameEdit_.Clear();
                nameEdit_.SetFocused(true);
                feedback_.clear();
                feedbackUntil_ = -1.0f;
            }
            break;

        case PressedBtn::Leave:
            if (PointInRect(x, y, g.leaveBtn.x, g.leaveBtn.y, g.leaveBtn.w, g.leaveBtn.h))
                DoLeave();
            break;

        case PressedBtn::Dissolve:
            if (PointInRect(x, y, g.dissolveBtn.x, g.dissolveBtn.y, g.dissolveBtn.w, g.dissolveBtn.h))
                DoDissolve();
            break;

        case PressedBtn::ScrollUp:
            if (PointInRect(x, y, g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h)) {
                scrollOffset_ = std::max(0, scrollOffset_ - 1);
                // `*(this+428) = -1` accompanies every original page change
                // (@0x668CE6 previous page, @0x668D7D next page): scrolling here is the
                // local analog of paging -> also deselect.
                selectedIdx_ = -1;
            }
            break;

        case PressedBtn::ScrollDown:
            if (PointInRect(x, y, g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h)) {
                scrollOffset_ = std::min(MaxScroll(), scrollOffset_ + 1);
                selectedIdx_  = -1; // same as @0x668D7D
            }
            break;

        case PressedBtn::Confirm:
            if (PointInRect(x, y, g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h))
                Confirm();
            break;

        case PressedBtn::Cancel:
            if (PointInRect(x, y, g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h))
                CancelInput();
            break;

        case PressedBtn::Row: {
            const std::vector<int> visible = VisibleMemberIndices();
            const int rows = std::min(kVisibleRows,
                                       static_cast<int>(visible.size()) - scrollOffset_);
            for (int i = 0; i < rows; ++i) {
                if (visible[static_cast<size_t>(scrollOffset_ + i)] != pressedRowIdx_) continue;
                const Rect r = RowRect(g, i);
                if (PointInRect(x, y, r.x, r.y, r.w, r.h))
                    selectedIdx_ = pressedRowIdx_; // `*(this+428) = i` (0x667F10)
                break;
            }
            break;
        }

        case PressedBtn::Kick: {
            const std::vector<int> visible = VisibleMemberIndices();
            const int rows = std::min(kVisibleRows,
                                       static_cast<int>(visible.size()) - scrollOffset_);
            for (int i = 0; i < rows; ++i) {
                if (visible[static_cast<size_t>(scrollOffset_ + i)] != pressedKickIdx_) continue;
                const Rect kr = KickRect(g, i);
                if (PointInRect(x, y, kr.x, kr.y, kr.w, kr.h))
                    DoKick();
                break;
            }
            break;
        }

        default: break;
    }

    pressedKickIdx_ = -1;
    pressedRowIdx_  = -1;
    return consumed;
}

// Keyboard — see the limitation documented at the top of UI/GuildWindow.h (no
// WM_CHAR routed by the UIManager, only OnKey(vk) = WM_KEYDOWN).
bool GuildWindow::OnKey(int vk) {
    if (!bOpen_) return false;

    if (mode_ != InputMode::None && nameEdit_.Focused()) {
        if (vk == VK_RETURN) { Confirm();     return true; }
        if (vk == VK_ESCAPE) { CancelInput(); return true; }
        if (vk == VK_BACK || vk == VK_DELETE || vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_HOME || vk == VK_END) {
            nameEdit_.OnKey(vk);
            return true;
        }
        // VK_0..VK_9 (0x30-0x39) and VK_A..VK_Z (0x41-0x5A) coincide with their
        // ASCII codes on Win32 -> basic uppercase/digit/space input only.
        // In SetRank mode the binary reads a 5-byte string via GetWindowTextA without
        // filtering characters: we therefore do NOT add a digit-only restriction.
        if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || vk == VK_SPACE) {
            nameEdit_.OnChar(static_cast<unsigned int>(vk));
            return true;
        }
        return true; // modal input: absorbs the rest of the keyboard with no effect
    }

    if (vk == VK_ESCAPE) { Close(); return true; }
    return false;
}

// Rendering
void GuildWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_     = ctx.screenW;
    lastScreenH_     = ctx.screenH;
    lastGameTimeSec_ = ctx.gameTimeSec;

    if (feedbackUntil_ > 0.0f && ctx.gameTimeSec >= feedbackUntil_) {
        feedback_.clear();
        feedbackUntil_ = -1.0f;
    }
    if (!bOpen_) return;

    const Geom g = ComputeGeometry(ctx.screenW, ctx.screenH);
    const std::vector<int> visible = VisibleMemberIndices();
    const int rows = std::min(kVisibleRows, static_cast<int>(visible.size()) - scrollOffset_);

    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, g.panel.x, g.panel.y, g.panel.w, g.panel.h, kColBg);
        ctx.DrawFrame(g.panel.x, g.panel.y, g.panel.w, g.panel.h, kColBorder, 2);
        ctx.FillRect(g.header.x, g.header.y, g.header.w, g.header.h, kColHeaderBg);

        const bool closeHover = PointInRect(cursorX, cursorY, g.closeBtn.x, g.closeBtn.y,
                                             g.closeBtn.w, g.closeBtn.h);
        ctx.FillRect(g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h,
                     closeHover ? kColHover : kColBtn);
        ctx.DrawFrame(g.closeBtn.x, g.closeBtn.y, g.closeBtn.w, g.closeBtn.h, kColBorder, 1);

        ctx.DrawFrame(g.listArea.x, g.listArea.y, g.listArea.w, g.listArea.h, kColBorder, 1);

        for (int i = 0; i < rows; ++i) {
            const Rect r = RowRect(g, i);
            const int memberIdx = visible[static_cast<size_t>(scrollOffset_ + i)];
            const bool hovered = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
            if (memberIdx == selectedIdx_) ctx.FillRect(r.x, r.y, r.w, r.h, kColRowSel);
            else if (hovered) ctx.FillRect(r.x, r.y, r.w, r.h, kColHover);
            else if ((i % 2) == 1) ctx.FillRect(r.x, r.y, r.w, r.h, kColRowAlt);

            const Rect kr = KickRect(g, i);
            const bool kickHover = PointInRect(cursorX, cursorY, kr.x, kr.y, kr.w, kr.h);
            ctx.FillRect(kr.x, kr.y, kr.w, kr.h, kickHover ? kColErrorHov : kColError);
            ctx.DrawFrame(kr.x, kr.y, kr.w, kr.h, kColBorder, 1);
        }

        const bool upHover = PointInRect(cursorX, cursorY, g.scrollUp.x, g.scrollUp.y,
                                          g.scrollUp.w, g.scrollUp.h);
        ctx.FillRect(g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h,
                     upHover ? kColHover : kColBtn);
        ctx.DrawFrame(g.scrollUp.x, g.scrollUp.y, g.scrollUp.w, g.scrollUp.h, kColBorder, 1);

        const bool downHover = PointInRect(cursorX, cursorY, g.scrollDown.x, g.scrollDown.y,
                                            g.scrollDown.w, g.scrollDown.h);
        ctx.FillRect(g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h,
                     downHover ? kColHover : kColBtn);
        ctx.DrawFrame(g.scrollDown.x, g.scrollDown.y, g.scrollDown.w, g.scrollDown.h, kColBorder, 1);

        if (mode_ != InputMode::None) {
            ctx.FillRect(g.editBox.x, g.editBox.y, g.editBox.w, g.editBox.h,
                         nameEdit_.Focused() ? kColEditFocus : kColEditBg);
            ctx.DrawFrame(g.editBox.x, g.editBox.y, g.editBox.w, g.editBox.h, kColBorder, 1);

            const bool confirmHover = PointInRect(cursorX, cursorY, g.confirmBtn.x, g.confirmBtn.y,
                                                   g.confirmBtn.w, g.confirmBtn.h);
            ctx.FillRect(g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h,
                         confirmHover ? kColSuccessHv : kColSuccess);
            ctx.DrawFrame(g.confirmBtn.x, g.confirmBtn.y, g.confirmBtn.w, g.confirmBtn.h, kColBorder, 1);

            const bool cancelHover = PointInRect(cursorX, cursorY, g.cancelBtn.x, g.cancelBtn.y,
                                                  g.cancelBtn.w, g.cancelBtn.h);
            ctx.FillRect(g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h,
                         cancelHover ? kColHover : kColBtn);
            ctx.DrawFrame(g.cancelBtn.x, g.cancelBtn.y, g.cancelBtn.w, g.cancelBtn.h, kColBorder, 1);
        } else {
            const Rect* btns[4]  = { &g.addBtn, &g.rankBtn, &g.leaveBtn, &g.dissolveBtn };
            for (int i = 0; i < 4; ++i) {
                const Rect& b = *btns[i];
                const bool hov = PointInRect(cursorX, cursorY, b.x, b.y, b.w, b.h);
                ctx.FillRect(b.x, b.y, b.w, b.h, hov ? kColHover : kColBtn);
                ctx.DrawFrame(b.x, b.y, b.w, b.h, kColBorder, 1);
            }
        }
        return;
    }

    // --- Text phase ---
    // Title: REAL name of the active guild (game::g_World.allianceRoster.guildName ==
    // g_LocalGuildName 0x168740C, cf. Game/GameState.h::AllianceRoster and Docs/
    // TS2_ALLIANCE_PARTY_ROSTER.md §3) — DISTINCT from the internal 50-member roster
    // (g_Guild) managed by this window. "Guilde" alone as long as no handler has yet
    // populated the name (before Net_OnGuildRosterReset/Update, or when guildless) —
    // honest degradation, not invention.
    const std::string& liveGuildName = game::g_World.allianceRoster.guildName;
    const std::string title = liveGuildName.empty() ? std::string("Guilde")
                                                      : ("Guilde : " + liveGuildName);
    ctx.Text(title.c_str(), g.panel.x + 10, g.panel.y + 7, kColTitle);

    const std::string count = std::to_string(game::g_Guild.CountMembers()) + "/" +
                               std::to_string(game::GuildRoster::kMaxMembers) + " membres";
    const int countW = ctx.MeasureText(count.c_str());
    ctx.Text(count.c_str(), g.closeBtn.x - countW - 10, g.panel.y + 7, kColText);
    ctx.Text("X", g.closeBtn.x + 5, g.closeBtn.y + 2, kColText);

    if (rows == 0) {
        ctx.Text("(aucun membre)", g.listArea.x + 4, g.listArea.y + 4, kColSubtle);
    }
    for (int i = 0; i < rows; ++i) {
        const Rect r = RowRect(g, i);
        const int memberIdx = visible[static_cast<size_t>(scrollOffset_ + i)];
        const game::GuildMember& m = game::g_Guild.members[static_cast<size_t>(memberIdx)];

        ctx.Text(m.name.c_str(), r.x + 4, r.y + 2, kColText);

        const std::string rankStr = "Rang " + std::to_string(m.rank);
        const int rankW = ctx.MeasureText(rankStr.c_str());
        ctx.Text(rankStr.c_str(), r.x + r.w - rankW - kKickBtnSize - 10, r.y + 2, kColSubtle);

        const Rect kr = KickRect(g, i);
        ctx.Text("X", kr.x + 4, kr.y + 1, kColText);
    }

    ctx.Text(scrollOffset_ > 0 ? "^" : "-", g.scrollUp.x + 5, g.scrollUp.y + 1, kColText);
    ctx.Text(scrollOffset_ < MaxScroll() ? "v" : "-", g.scrollDown.x + 5, g.scrollDown.y + 1, kColText);

    if (mode_ != InputMode::None) {
        ctx.Text(nameEdit_.Text().c_str(), g.editBox.x + 4, g.editBox.y + 6, kColText);
        // Simplified blinking caret: pinned at the end of the text (no tracking of a
        // mid-string position, EditBox::caret_ being private — cf. limitation documented
        // in the header).
        if (nameEdit_.Focused() && (static_cast<int>(ctx.gameTimeSec * 2.0f) & 1) == 0) {
            const int caretX = g.editBox.x + 4 + ctx.MeasureText(nameEdit_.Text().c_str());
            ctx.Text("|", caretX, g.editBox.y + 6, kColText);
        }
        ctx.Text("Confirmer", g.confirmBtn.x + 4, g.confirmBtn.y + 6, kColText);
        ctx.Text("Annuler", g.cancelBtn.x + 6, g.cancelBtn.y + 6, kColText);
    } else {
        ctx.Text("Ajouter",   g.addBtn.x + 6,      g.addBtn.y + 6,      kColText);
        ctx.Text("Rang",      g.rankBtn.x + 8,     g.rankBtn.y + 6,     kColText);
        ctx.Text("Quitter",   g.leaveBtn.x + 6,    g.leaveBtn.y + 6,    kColText);
        ctx.Text("Dissoudre", g.dissolveBtn.x + 8, g.dissolveBtn.y + 6, kColText);
    }

    if (!feedback_.empty())
        ctx.Text(feedback_.c_str(), g.feedbackArea.x, g.feedbackArea.y, feedbackColor_);
}

// UNPORTED GUILD EMISSIONS — builders ALL PRESENT in Net/SendPackets.h; what's
// missing is the STATE or the UI, never the packet. No `missingBuilder` on this front.
// 1) ANNOUNCEMENT — Net_SendGuarded_5 0x593290 (Net/SendPackets.h:98), Op75 sub-op 5,
//    204-byte payload = 4 CONTIGUOUS 51-byte fields (g_Guild+1724/1775/1826/1877).
//    Anchor: UI_GuildMgrWnd_OnClick 0x668B70 page 3, `*(this+413)` @0x669AD9 ->
//    4x GetWindowTextA(dword_1668FE4/E8/EC/F0, this+1724+51*i, 51) @0x669b3e-0x669b87,
//    then 8 guards (4x maybe_Dict001_MatchWord + 4x Str_ContainsForbiddenToken 0x556370)
//    -> refusal #112, else Net_SendGuarded_5(this+1724) @0x669ca9.
//    NOT PORTED: requires 4 free-text fields of 50 characters. The UIManager does not
//    route WM_CHAR (cf. header of the .h) -> input limited to uppercase/digits/space,
//    unusable for an announcement. Str_ContainsForbiddenToken 0x556370 is also not
//    modeled on the ClientSource side.
//    NB: the READ fields (+1724..+1927) are DISTINCT from the fields displayed on
//    entering the page (+1170/+1221/+1272/+1323, @0x668f60-0x66907f) — two buffers, do
//    not merge them.
//
// 2) ALLIANCE / WAR (page 5) — Net_SendGuarded_14 0x593770 (Net/SendPackets.h:128),
//    Op75 sub-op 14, `char` argument emitted as 4 LE BYTES (Crt_Memcpy(v2, &a1, 4) @0x5937a0).
//    Anchors: page entry `*(this+417)` @0x66977A -> Net_SendGuarded_2(3) @0x6697c3 +
//    `*(this+426)=5` + `*(this+18739) = dword_1687450` @0x6697dd; 5-choice selector
//    `*(this+420+i)` @0x66A08C -> `*(this+18739) = i` @0x66a11e; validation
//    `*(this+418)` @0x669F74 -> guard `dword_16746B8 <= 1` (else #1718) ->
//    Net_SendGuarded_14(*(this+18739)) @0x669fe7.
//    NOT PORTED: `*(g_Guild+74956)` (choice) and `dword_1687450` / `dword_16746B8` are
//    not modeled (dword_16746B8 also serves as a guard for UI_Guild_InviteRequest 0x5ED540
//    @0x5ed5c3, exact role not established).
//
// 3) ALLIANCE A/B — Net_SendGuarded_9 0x5934B0 (Net/SendPackets.h:214), Op75 sub-op 9,
//    13-byte payload (name) + i32 @13. Anchor: `*(this+406)` @0x669092 -> master guard
//    (#467) / row guard (#472) / target != self (#546) / if `*(this + 10*page + row + 180) == 2`
//    AND `+41 != "" AND +54 != ""` -> refusal #547; then MsgBox 24 (`*(this+482)=1`) or 25
//    (`*(this+482)=2`) -> UI_MsgBox_OnLButtonUp @0x5c1222-0x5c122e / @0x5c123d-0x5c1249:
//    Net_SendGuarded_9(byte_183A0F4, 1 or 2) — the VALUE comes from the MsgBox case
//    number, not from `*(this+482)`.
//    NOT PORTED: the per-member status `*(this + 10*page + row + 180)` and the
//    co-leader fields +41/+54 are "OUT OF SCOPE" for game::GuildRoster (cf. GuildSystem.h).
//
// 4) ACTION 56 — Net_SendGuarded_17 0x593800 (Net/SendPackets.h:157), Op75 sub-op 17,
//    13-byte + 13-byte + i32 @26 payload (3rd arg `char` -> Crt_Memcpy(v6, &a3, 4) @0x59385a).
//    Anchor: `*(this+425)` @0x6697F7 -> master guard (#566, NOT #467) / row guard (#472) /
//    `g_Guild+1932 != g_SelfName` (else #2133) -> MsgBox 56 (#2107) ->
//    UI_MsgBox_OnLButtonUp @0x5c1d8d-0x5c1da3: Net_SendGuarded_17(byte_183A0F4,
//    byte_1673184, dword_1839ED8) — dword_1839ED8 = g_Guild+1392, pushed BY VALUE.
//    NOT PORTED: g_Guild+1392 is not modeled and this action's exact role remains
//    undetermined (no usable string) — naming/emitting it would be guessing.
//
// 5) GUILD CREATION (Net_SendMenu_1 0x5938C0, Op79 sub-op 1) and INVITE
//    REQUEST (Net_SendGuarded_2(2)) — OUT OF THIS FRONT'S SCOPE: their triggers are
//    NPC window methods (UI_Guild_CreateRequest 0x5ED460 and
//    UI_Guild_InviteRequest 0x5ED540 call UI_NpcWin_CloseRestore 0x5DC1F0 on
//    `this`), not the Guild window. They open MsgBox 27 / 26 and the send happens
//    in UI_MsgBox_OnLButtonUp @0x5c1273 / @0x5c125f. To be wired from the NPC window.
//    CORRECTION to the upstream EXTRACT report, which gave 0x5ED460's guard as
//    "g_SelfLevel >= 40": the decompilation says the OPPOSITE — `if (g_SelfLevel <= 39)`
//    @0x5ed4a7 OPENS the confirmation (MsgBox 27, #600), and level >= 40 gives the
//    REFUSAL message #599 @0x5ed4ba. (Consistent with the IDA name "guild/mentor create":
//    a low-level character requests a mentor.) Do not reintroduce the inversion.

} // namespace ts2::ui

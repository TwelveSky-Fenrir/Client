// UI/ChatWindow.h - TS2 client chat window & system message list.
//
// PRAGMATIC rewrite (functional skeleton) of TwelveSky2's chat subsystem,
// based on Docs/TS2_CLIENT_SHELL.md #2.5 and the disassembly:
//   UI_Chat_SubmitInput     0x68B330  parses prefix (!/#/@/~) + routes by channel
//   Chat_SubmitTypedMessage 0x5C3CF0  simple "say" box -> Op80
//   Msg_AppendChatLine      0x68DB50  300-line chat ring (+ sender)
//   Msg_AppendSystemLine    0x68D9D0  300-line system ring (60s expiry)
//   UI_SysMsgList_Render    0x5AEC80  system list + 60s countdown
//   UI_GameHud_Render 0x67A3C0 §13    chat & system message window (bottom-left),
//     fully decompiled in Docs/TS2_UI_GAMEHUD_RENDER.md:
//       §13b (EA 0x683B71-0x684019) channel tabs (switch this+644, 1-5) +
//            per-channel "unread" badges (this+620..636, 5 booleans, shared
//            icon unk_924944 at the same x as the tabs) + per-channel
//            colored input preview;
//       §13c (EA 0x6840AC-0x68472B) BOTH rings (system this+672 / chat
//            this+38488, 122 bytes/entry, per-entry color);
//       §13d (EA 0x684736-0x6848DB) 8 stacked notification badges (this+128..
//            +160, not 4), x increasing by 23 px.
//
// The original object is the g_ChatManager 0x184C3C8 singleton (~80 KB: two
// ring logs, channel mode +644, whisper target +648). Here we do NOT
// reproduce the byte-exact layout: we keep the SEMANTICS (two rings,
// channels, prefixes, word filter, clickable tabs, notification badges) with
// clean C++ containers.
//
// Accepted pragmatic deviations:
//   - input doesn't use Win32 EDIT controls (UI_CreateEditBoxes 0x50E460) but an
//     internal buffer driven by OnKey/OnChar;
//   - localized labels (StrTable005_Get) and the banned word filter
//     (maybe_Dict001_MatchWord 0x4C1410) are provided via default values /
//     an injectable hook;
//   - local echo of sent messages (the server normally echoes them back) so
//     the UI stays visible offline;
//   - the 17 tab/badge .IMG icons (unk_91A358.., unk_924944,
//     unk_8EA960..8EAE00) not being resolved (no string/skin table loaded
//     in this pass), tabs/badges are drawn as tinted solid rects + text
//     label (accepted fallback, cf. Docs/TS2_UI_GAMEHUD_RENDER.md
//     §19) rather than blocking the render;
//   - the game::g_Client.msg stream (Game/ClientRuntime.h) is optionally
//     rewired via SyncFromMessageLog(): System/Chat/Whisper/Faction lines
//     already pushed by network/gameplay handlers feed the same rings as
//     AddLine/AddSystemLine.
//
// FLOATING NOTICES (wired 2026-07-16): MsgKind::Floating is NO LONGER a sink.
// Floating banners are a SEPARATE object in the binary (dword_1821D58, neither
// g_ChatManager 0x184C3C8 nor the system list dword_1822350): they are therefore
// carried by a standalone component UI/FloatingNotices.h (13 typed slots, 10s,
// no fade), which ChatWindow simply FEEDS (SyncFromMessageLog -> notices_.Show,
// HUD_ShowFloatingMessage 0x5AEEC0) and RENDERS (Render -> notices_.Render,
// HUD_RenderFloatingMessages 0x5AF4C0). This port works because ChatWindow is the
// only already-wired consumer of game::g_Client.msg (UI/GameHud.cpp L1283-1285) and
// the original chains both renders in UI_RenderAllDialogs 0x5AE2D0:
// HUD_RenderFloatingMessages @0x5AE5A7 THEN UI_SysMsgList_Render @0x5AE5B9 — order
// reproduced identically by Render() (notices first).
//
// NB inclusion: this header includes neither <d3d9.h> nor <winsock2.h> (forward
// decls), so .cpp files remain free to include Net/ (winsock2 first) before
// Gfx/ (windows.h) with no ordering conflict.
#pragma once
#include <cstdint>
#include <string>
#include <deque>
#include <array>

// HUD floating notices (dword_1821D58) — header LIGHT by construction (no
// d3d9/d3dx9: its GPU resources are behind a PIMPL), so including it here doesn't
// break the "NB inclusion" note above.
#include "UI/FloatingNotices.h"

// D3D9 COM interface (optional background): forward-declared, never dereferenced here.
struct IDirect3DTexture9;

namespace ts2::gfx { class SpriteBatch; class Font; }
namespace ts2::net { struct NetClient; }
namespace ts2::game { class MessageLog; }

namespace ts2::ui {

// ARGB color. Compatible with D3DCOLOR (== DWORD) at the Font/SpriteBatch call site.
using Color = uint32_t;

// Chat channels. Values 0..5 match the channelMode field (this+644) of
// cChatManager as read by UI_Chat_SubmitInput 0x68B330. Normal = local "say"
// (Op80, Chat_SubmitTypedMessage 0x5C3CF0), with no original channelMode.
enum class ChatChannel : int {
    Whisper  = 0, // -> Net_SendOp39(target13, msg61)   (whisper)
    Party    = 1, // -> Net_SendOp38(msg61)            (party)
    Alliance = 2, // -> Net_SendOp68(msg61)            (alliance)
    Guild    = 3, // -> Net_SendOp77(msg61)            (guild)
    Trade    = 4, // -> Net_SendOp81(msg61)            (trade)
    Faction  = 5, // -> Net_SendOp40(msg61)            (faction)
    Normal   = 6, // -> Net_SendOp80(msg61)            (local say, default channel)
};

// Channel colors: the original resolves at display time the 8-index
// mFONTCOLOR table (ColorTable_InitPalette 0x4C1D60; +184 = 0x84DFD8..0x84DFF4 =
// system/whisper/party/shout/guild/faction/trade/gm) via ColorTable_GetColor
// 0x4C1FE0. ChatWindow::ChannelColor() (see ChatWindow.cpp) NOW reads these
// REAL colors from game::g_Strings.colors (Game/StringTables.h), instead of the
// previously invented values (whisper/party/guild/faction/trade removed).
//
// Two channels have NO proven index in the table (0x4C1D60 only defines 8)
// and therefore keep a documented LOCAL fallback:
//   - Normal: the local "say" (Op80, Chat_SubmitTypedMessage 0x5C3CF0) is NOT
//     "shout" (Shout = idx39, Pkt_ShoutMessage 0x48F640 = opcode 0x43) -> no index.
//   - Alliance: none of the 8 indices of 0x4C1D60 correspond to alliance.
// kColorSystem remains a LOCAL fallback for this widget's purely cosmetic system
// lines (e.g. "Message blocked"); the REAL system lines already arrive colored
// (g_SysMsgColor 0x84DFD8 idx15) via game::g_Client.msg -> SyncFromMessageLog.
inline constexpr Color kColorSystem   = 0xFFC8C8C8u; // local fallback (widget's cosmetic system lines)
inline constexpr Color kColorNormal   = 0xFFFFFFFFu; // fallback: local Op80 "say", no proven palette index (0x4C1D60)
inline constexpr Color kColorAlliance = 0xFF80FFFFu; // fallback: no proven alliance channel index (0x4C1D60)

// -----------------------------------------------------------------------------
// ChatWindow: two ring logs (chat + system), a current channel, an input
// buffer, and 2D rendering (background via SpriteBatch, text via Font).
class ChatWindow {
public:
    // Capacity of each ring (cChatManager: 300 lines).
    static constexpr int   kMaxLines     = 300;
    // Number of chat lines shown (pageMode this+640: 10 expanded / 5 collapsed).
    static constexpr int   kVisibleLines = 10;
    static constexpr int   kCollapsedLines = 5;
    // Input limit (EM_LIMITTEXT chat field = 0x3C).
    static constexpr int   kInputLimit   = 60;
    // Useful body of a network message (payload 61 bytes, NUL included).
    static constexpr int   kMsgLen       = 61;
    // Length of a name/target (13 bytes, NUL included).
    static constexpr int   kNameLen      = 13;
    // Display TTL of a system line out of focus (UI_SysMsgList_Render: 60s).
    static constexpr float kSysLineTtl   = 60.0f;

    ChatWindow();
    ~ChatWindow();
    ChatWindow(const ChatWindow&)            = delete;
    ChatWindow& operator=(const ChatWindow&) = delete;

    // Wires the network client: while null, sends are ignored
    // (local echo stays active for rendering).
    void Bind(net::NetClient* nc) { net_ = nc; }

    // Optional background: UI sprite already uploaded as a GPU texture
    // (asset::ImgFile -> gfx::GpuTexture). Blitted as-is at (x,y) via
    // SpriteBatch before the text.
    void SetBackground(IDirect3DTexture9* tex, int x, int y) { bgTex_ = tex; bgX_ = x; bgY_ = y; }

    // Screen dimensions (to anchor the window bottom-left). Default 1024x768.
    void SetScreenSize(int w, int h) { screenW_ = w; screenH_ = h; }

    // Updates the game clock (g_GameTimeSec) for timestamping / expiry.
    void Tick(float nowSec) { now_ = nowSec; }

    // --- Adding lines -----------------------------------------------------
    // Msg_AppendChatLine 0x68DB50: pushes a line into the chat ring.
    void AddLine(const std::string& text, Color color, const std::string& sender = {});
    // Shortcut: channel line (channel color + sender).
    void AddChannelLine(ChatChannel ch, const std::string& sender, const std::string& body);
    // Msg_AppendSystemLine 0x68D9D0: pushes a line into the system ring.
    void AddSystemLine(const std::string& text, Color color = kColorSystem);

    // --- Render (SpriteBatch for background, Font for text) ----------------
    void Render(gfx::SpriteBatch& sprites, gfx::Font& font);

    // --- Mouse (clickable channel tabs, §13b) ---------------------------
    // Hit-test of the 5 channel tabs (Party/Whisper/Guild/Faction/Trade;
    // Alliance/Say remain reachable via prefix or Tab, cf. CycleChannel).
    // Selects the tab and clears its "unread" badge. Returns true if
    // consumed ("first consumer wins" rule, same convention as
    // UI/UIManager.h::Dialog::OnMouseDown).
    bool OnMouseDown(int x, int y);

    // --- Generic notification badges (§13d, 8 slots this+128..160)
    // idx outside [0,8) silently ignored. Placeholder rect (no icon
    // resolved): cf. header banner.
    void SetNotificationBadge(int idx, bool on);

    // --- Feed from game::g_Client.msg (Game/ClientRuntime.h) --------
    // Republishes into the chat_/sys_ rings the lines added to `log` since the
    // last call (tracks the identity of the last consumed line to survive
    // FIFO eviction on the source side, kMaxLines=256). MsgKind::System ->
    // system ring; Chat/Whisper/Faction -> chat ring (+ "unread" badge if
    // the channel isn't the active tab); Floating -> notices_.Show
    // (HUD_ShowFloatingMessage 0x5AEEC0), cf. header banner.
    void SyncFromMessageLog(const game::MessageLog& log);

    // --- Input --------------------------------------------------------------
    // WM_KEYDOWN: Enter (focus/submit), Escape (cancel), Backspace,
    // arrows (caret/scroll), Tab (change channel). Returns true if
    // the event is consumed (the 3D world must not receive it).
    bool OnKey(int vk);
    // WM_CHAR: inserts a printable character into the input (if focused).
    bool OnChar(char c);

    // UI_Chat_SubmitInput 0x68B330: parses the prefix (!,#,@,~) then routes
    // per the current channel; word filter before sending.
    void Submit();
    // Chat_SubmitTypedMessage 0x5C3CF0: simple "say" box -> Op80.
    void SubmitSay();

    // --- State ----------------------------------------------------------------
    void        SetChannel(ChatChannel ch) { channel_ = ch; }
    ChatChannel Channel() const { return channel_; }
    void        CycleChannel();
    // UI_Chat_SetWhisperMode 0x68B260: sets the target and switches to whisper channel.
    void        SetWhisperTarget(const std::string& name);
    const std::string& WhisperTarget() const { return whisperTarget_; }

    bool Focused() const { return focused_; }
    void Focus()   { focused_ = true; }              // UI_Chat_FocusInput 0x68B200
    void Unfocus() { focused_ = false; caret_ = 0; }
    const std::string& Input() const { return input_; }

    // Collapsed/expanded log state (pageMode this+640).
    void SetExpanded(bool v) { expanded_ = v; }
    bool Expanded() const { return expanded_; }

    // Banned word filter hook (maybe_Dict001_MatchWord 0x4C1410). Returns true
    // if the text contains a banned word -> message rejected. nullptr = no filter.
    using WordFilter = bool (*)(const char* text);
    void SetWordFilter(WordFilter f) { wordFilter_ = f; }

private:
    struct ChatLine { std::string text; Color color; std::string sender; float t; };
    struct SysLine  { std::string text; Color color; float t; };

    void        PushChat(ChatLine&& l);
    void        PushSys(SysLine&& l);
    bool        Banned(const std::string& s) const;
    void        SendOnChannel(ChatChannel ch, const std::string& body);
    static Color       ChannelColor(ChatChannel ch);
    static const char* ChannelPrefix(ChatChannel ch);

    // --- Channel tabs (§13b): 5 slots, x = EA 0x683B71 (+3,+80,+157,
    // +234,+311). Alliance/Say have no dedicated tab in the original HUD
    // (reachable only via ~ prefix / Tab, cf. doc §13b header banner:
    // the preview color table only cites Party/Whisper/Guild/
    // Faction/Trade/Shout, not Alliance).
    struct TabInfo { ChatChannel channel; const char* label; int xOffset; };
    static constexpr int kTabCount  = 5;
    static constexpr int kTabWidth  = 74;
    static constexpr int kTabHeight = 16;
    static const TabInfo kTabs[kTabCount];

    int  TabIndexForChannel(ChatChannel ch) const;
    void SelectTab(int idx);

    // Solid-rect primitives (lazy 1x1 white texture, same technique as
    // UI/GameHud.cpp::CreateWhiteTexture, but created via ID3DXSprite::GetDevice
    // to avoid depending on gfx::Renderer in this widget).
    IDirect3DTexture9* EnsureWhiteTexture(gfx::SpriteBatch& sprites);
    void DrawFilledRect(gfx::SpriteBatch& sprites, int x, int y, int w, int h, Color color);
    void RenderTabPanels(gfx::SpriteBatch& sprites, int tabsY, int badgesY);
    void RenderTabLabels(gfx::Font& font, int tabsY);

    std::deque<ChatLine> chat_;
    std::deque<SysLine>  sys_;
    int   scroll_ = 0;   // scroll offset (lines from the bottom)

    // HUD floating notices (binary object dword_1821D58, DISTINCT from
    // g_ChatManager 0x184C3C8): fed by SyncFromMessageLog (MsgKind::Floating)
    // and rendered first in Render(). Cf. header banner.
    FloatingNotices notices_;

    std::array<bool, kTabCount> unread_{};   // "unread" badges per tab (this+620..636)
    std::array<bool, 8>         notif_{};    // generic notification badges (§13d)
    IDirect3DTexture9*          whiteTex_ = nullptr;

    // Identity of the last game::g_Client.msg line already republished
    // (SyncFromMessageLog): allows pushing only new lines without
    // depending on a cumulative counter absent from MessageLog.
    bool        syncedAny_  = false;
    std::string syncedText_;
    std::string syncedWho_;
    int         syncedKind_ = -1;

    std::string input_;
    int   caret_   = 0;
    bool  focused_ = false;
    bool  expanded_ = true;

    ChatChannel channel_ = ChatChannel::Normal;
    std::string whisperTarget_;

    net::NetClient* net_ = nullptr;
    WordFilter      wordFilter_ = nullptr;

    IDirect3DTexture9* bgTex_ = nullptr;
    int   bgX_ = 8, bgY_ = 0;

    int   screenW_ = 1024;
    int   screenH_ = 768;
    float now_ = 0.0f;

    int   originX_ = 8;
    int   lineH_   = 14;
};

} // namespace ts2::ui

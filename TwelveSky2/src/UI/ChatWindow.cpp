// UI/ChatWindow.cpp - implementation of chat & the system message list.
//
// Include order: Net/ FIRST (NetClient.h pulls <winsock2.h> before
// <windows.h>), then Gfx/ (which pulls <windows.h>/<d3d9.h>/<d3dx9.h>), finally
// the module header. Follows the project's Winsock convention.
#include "Net/SendPackets.h"   // -> Net/NetClient.h: winsock2 then windows (safe order)
#include "Net/NetClient.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "UI/ChatWindow.h"
#include "Game/ClientRuntime.h"   // game::MessageLog/MsgKind (SyncFromMessageLog): no windows.h, safe to include here
#include "Game/StringTables.h"    // game::g_Strings.colors (mFONTCOLOR palette): std lib only, safe after Net/

#include <algorithm>
#include <cstring>

namespace ts2::ui {

// Channel tabs (§13b, EA 0x683B71): x = +3,+80,+157,+234,+311.
const ChatWindow::TabInfo ChatWindow::kTabs[ChatWindow::kTabCount] = {
    { ChatChannel::Party,   "Groupe",    3   },
    { ChatChannel::Whisper, "Chuchoter", 80  },
    { ChatChannel::Guild,   "Guilde",    157 },
    { ChatChannel::Faction, "Faction",   234 },
    { ChatChannel::Trade,   "Commerce",  311 },
};

ChatWindow::ChatWindow() = default;

ChatWindow::~ChatWindow() {
    if (whiteTex_) { whiteTex_->Release(); whiteTex_ = nullptr; }
}

// -----------------------------------------------------------------------------
// Channel -> color mapping. Reads the REAL mFONTCOLOR palette
// (ColorTable_InitPalette 0x4C1D60) via game::g_Strings.colors: the binary stores
// the channel index in the chat line (Msg_AppendChatLine 0x68DB50) and resolves it
// at DRAW time via ColorTable_GetColor 0x4C1FE0; here we resolve it at emission time
// (same pixel result). Normal/Alliance have no proven index in 0x4C1D60 -> documented
// local fallback.
Color ChatWindow::ChannelColor(ChatChannel ch) {
    const game::ColorPalette& pal = game::g_Strings.colors; // mFONTCOLOR instance 0x84DF20
    switch (ch) {
        case ChatChannel::Whisper:  return pal.WhisperColor(); // g_ChatColor_Whisper 0x84DFDC idx1  (0x4C1FE0)
        case ChatChannel::Party:    return pal.PartyColor();   // g_ChatColor_Party   0x84DFE0 idx40 (0x4C1FE0)
        case ChatChannel::Guild:    return pal.GuildColor();   // g_ChatColor_Guild   0x84DFE8 idx36 (0x4C1FE0)
        case ChatChannel::Faction:  return pal.FactionColor(); // g_ChatColor_Faction 0x84DFEC idx37 (0x4C1FE0)
        case ChatChannel::Trade:    return pal.TradeColor();   // g_ChatColor_Trade   0x84DFF0 idx38 (0x4C1FE0)
        case ChatChannel::Alliance: return kColorAlliance;     // fallback: no proven index (0x4C1D60)
        case ChatChannel::Normal:   return kColorNormal;       // fallback: local Op80 "say" with no index (0x4C1D60)
    }
    return kColorNormal;
}

const char* ChatWindow::ChannelPrefix(ChatChannel ch) {
    switch (ch) {
        case ChatChannel::Whisper:  return "[Chuchoter]";
        case ChatChannel::Party:    return "[Groupe]";
        case ChatChannel::Alliance: return "[Alliance]";
        case ChatChannel::Guild:    return "[Guilde]";
        case ChatChannel::Trade:    return "[Commerce]";
        case ChatChannel::Faction:  return "[Faction]";
        case ChatChannel::Normal:   return "[Dire]";
    }
    return "[Dire]";
}

// -----------------------------------------------------------------------------
// Rings (Msg_AppendChatLine / Msg_AppendSystemLine: capacity 300, FIFO eviction).
void ChatWindow::PushChat(ChatLine&& l) {
    chat_.push_back(std::move(l));
    if (static_cast<int>(chat_.size()) > kMaxLines) chat_.pop_front();
}

void ChatWindow::PushSys(SysLine&& l) {
    sys_.push_back(std::move(l));
    if (static_cast<int>(sys_.size()) > kMaxLines) sys_.pop_front();
}

void ChatWindow::AddLine(const std::string& text, Color color, const std::string& sender) {
    PushChat(ChatLine{ text, color, sender, now_ });
}

void ChatWindow::AddChannelLine(ChatChannel ch, const std::string& sender, const std::string& body) {
    AddLine(body, ChannelColor(ch), sender);
    // §13b: "unread" badge (this+620..636) if the message arrives on a tab
    // that isn't the currently active one.
    const int idx = TabIndexForChannel(ch);
    if (idx >= 0 && ch != channel_) unread_[static_cast<size_t>(idx)] = true;
}

void ChatWindow::AddSystemLine(const std::string& text, Color color) {
    PushSys(SysLine{ text, color, now_ });
}

// -----------------------------------------------------------------------------
// Channel tabs (§13b): channel -> tab index mapping, selection.
int ChatWindow::TabIndexForChannel(ChatChannel ch) const {
    for (int i = 0; i < kTabCount; ++i) {
        if (kTabs[i].channel == ch) return i;
    }
    return -1;
}

void ChatWindow::SelectTab(int idx) {
    if (idx < 0 || idx >= kTabCount) return;
    channel_ = kTabs[idx].channel;
    unread_[static_cast<size_t>(idx)] = false;
}

bool ChatWindow::OnMouseDown(int x, int y) {
    const int tabsY = screenH_ - 40; // must stay in sync with Render()
    for (int i = 0; i < kTabCount; ++i) {
        const int tx = originX_ + kTabs[i].xOffset;
        if (x >= tx && x < tx + kTabWidth && y >= tabsY && y < tabsY + kTabHeight) {
            SelectTab(i);
            return true;
        }
    }
    return false;
}

// §13d: 8 generic notification badges (this+128..+160, not 4).
void ChatWindow::SetNotificationBadge(int idx, bool on) {
    if (idx < 0 || idx >= static_cast<int>(notif_.size())) return;
    notif_[static_cast<size_t>(idx)] = on;
}

// -----------------------------------------------------------------------------
// Feed from game::g_Client.msg (real Msg_AppendSystemLine/Msg_AppendChatLine,
// already pushed by network/gameplay handlers -- cf. Game/ClientRuntime.h
// and SceneManager.cpp::AppendKeepAliveFailedMessage). Only republishes lines
// added since the last call: searches for the last already-consumed line
// starting from the end (survives FIFO eviction on the source side, kMaxLines=256,
// which may differ from the local kMaxLines=300).
void ChatWindow::SyncFromMessageLog(const game::MessageLog& log) {
    const auto& lines = log.Lines();
    if (lines.empty()) return;

    size_t startIdx = 0;
    if (syncedAny_) {
        bool found = false;
        for (size_t i = lines.size(); i-- > 0; ) {
            const auto& l = lines[i];
            if (l.text == syncedText_ && l.who == syncedWho_ &&
                static_cast<int>(l.kind) == syncedKind_) {
                startIdx = i + 1;
                found = true;
                break;
            }
        }
        if (!found) startIdx = 0; // tracked line evicted -> republish everything (best effort)
    }

    for (size_t i = startIdx; i < lines.size(); ++i) {
        const auto& l = lines[i];
        switch (l.kind) {
            case game::MsgKind::System:
                AddSystemLine(l.text, l.color);
                break;
            case game::MsgKind::Chat:
                AddLine(l.text, l.color, l.who);
                break;
            case game::MsgKind::Whisper:
                AddLine(l.text, l.color, l.who.empty() ? std::string("?") : l.who);
                if (channel_ != ChatChannel::Whisper) {
                    const int idx = TabIndexForChannel(ChatChannel::Whisper);
                    if (idx >= 0) unread_[static_cast<size_t>(idx)] = true;
                }
                break;
            case game::MsgKind::Faction:
                AddLine(l.text, l.color, l.who);
                if (channel_ != ChatChannel::Faction) {
                    const int idx = TabIndexForChannel(ChatChannel::Faction);
                    if (idx >= 0) unread_[static_cast<size_t>(idx)] = true;
                }
                break;
            case game::MsgKind::Floating:
                // HUD_ShowFloatingMessage 0x5AEEC0: arms the `floatType` slot (0..12;
                // signed guard @0x5AEECD/@0x5AEED3 replayed by Show, so an out-of-range
                // value is silently ignored just like the original @0x5AEED5).
                // Live producers emit 0,1,2,3,5,10 (Net/WorldEntityDispatch.cpp,
                // Net/GameVarDispatch.cpp, Net/GameHandlers_*.cpp).
                //
                // TODO [anchor 0x5AEF7A]: `subType` (arg_4) is passed as 0. It ONLY
                // selects the sound (sub-switch @0x5AEFF5..) — no effect on rendering —
                // and game::MessageLine doesn't carry it: MessageLog::Floating
                // (Game/ClientRuntime.cpp L36) already discards its `flag` parameter via
                // `int /*flag*/`. Rewire once the sound is ported.
                // TODO [anchor 0x5AEF26]: `text2` (2nd line, read only by type 12
                // @0x5AFCFF) is absent from game::MessageLine. No observable effect: no
                // type-12 producer on the ClientSource side.
                notices_.Show(l.floatType, 0, l.text);
                break;
            default:
                break;
        }
    }

    const auto& last = lines.back();
    syncedText_ = last.text;
    syncedWho_  = last.who;
    syncedKind_ = static_cast<int>(last.kind);
    syncedAny_  = true;
}

// -----------------------------------------------------------------------------
// Solid rect (lazy 1x1 white texture, same mechanics as
// UI/GameHud.cpp::CreateWhiteTexture, without depending on gfx::Renderer here).
IDirect3DTexture9* ChatWindow::EnsureWhiteTexture(gfx::SpriteBatch& sprites) {
    if (whiteTex_) return whiteTex_;
    ID3DXSprite* spr = sprites.Get();
    if (!spr) return nullptr;

    IDirect3DDevice9* dev = nullptr;
    if (FAILED(spr->GetDevice(&dev)) || !dev) return nullptr;

    IDirect3DTexture9* tex = nullptr;
    if (SUCCEEDED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                     D3DPOOL_MANAGED, &tex, nullptr)) && tex) {
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits) {
            *static_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
            tex->UnlockRect(0);
        }
    }
    dev->Release();
    whiteTex_ = tex;
    return whiteTex_;
}

void ChatWindow::DrawFilledRect(gfx::SpriteBatch& sprites, int x, int y, int w, int h, Color color) {
    if (!whiteTex_ || w <= 0 || h <= 0) return;
    RECT src{ 0, 0, 1, 1 };
    sprites.DrawSpriteScaled(whiteTex_, &src, x, y, static_cast<float>(w), static_cast<float>(h),
                             color, /*compensatePos=*/true);
}

// "Panels" pass (inside sprites.Begin()/End()): tab backgrounds + badges.
void ChatWindow::RenderTabPanels(gfx::SpriteBatch& sprites, int tabsY, int badgesY) {
    if (!EnsureWhiteTexture(sprites)) return;

    // §13d: 8 generic notification badges, stacked horizontally.
    for (int i = 0; i < static_cast<int>(notif_.size()); ++i) {
        const int bx = originX_ + i * 14;
        const Color c = notif_[static_cast<size_t>(i)] ? 0xFFFFC020u : 0x40606060u;
        DrawFilledRect(sprites, bx, badgesY, 10, 10, c);
    }

    // §13b: backgrounds of the 5 channel tabs + "unread" dot.
    for (int i = 0; i < kTabCount; ++i) {
        const TabInfo& t = kTabs[i];
        const int tx = originX_ + t.xOffset;
        const bool active = (channel_ == t.channel);
        DrawFilledRect(sprites, tx, tabsY, kTabWidth, kTabHeight,
                       active ? 0xFF3A5878u : 0xB0202028u);
        if (unread_[static_cast<size_t>(i)] && !active) {
            DrawFilledRect(sprites, tx + kTabWidth - 8, tabsY - 2, 6, 6, 0xFFFF4040u);
        }
    }
}

// "Text" pass (inside font.BeginBatch()/EndBatch()): tab labels.
void ChatWindow::RenderTabLabels(gfx::Font& font, int tabsY) {
    for (int i = 0; i < kTabCount; ++i) {
        const TabInfo& t = kTabs[i];
        const int tx = originX_ + t.xOffset;
        const bool active = (channel_ == t.channel);
        font.DrawTextStyled(t.label, tx + 4, tabsY + 2,
                            active ? kColorNormal : kColorSystem, gfx::kStyleOutline);
    }
}

// -----------------------------------------------------------------------------
// Banned word filter (maybe_Dict001_MatchWord 0x4C1410) via injectable hook.
bool ChatWindow::Banned(const std::string& s) const {
    return wordFilter_ && wordFilter_(s.c_str());
}

// -----------------------------------------------------------------------------
// Emission: builds a 61-byte payload (NUL-padded) and routes to the right
// Net_Send* builder (see Net/SendPackets.h). Whisper adds a 13-byte target.
void ChatWindow::SendOnChannel(ChatChannel ch, const std::string& body) {
    if (!net_) return;

    char msg[kMsgLen] = {};
    const size_t n = std::min(body.size(), static_cast<size_t>(kMsgLen - 1));
    std::memcpy(msg, body.data(), n);

    switch (ch) {
        case ChatChannel::Whisper: {
            char target[kNameLen] = {};
            const size_t tn = std::min(whisperTarget_.size(), static_cast<size_t>(kNameLen - 1));
            std::memcpy(target, whisperTarget_.data(), tn);
            net::Net_SendOp39(*net_, target, msg);   // 0x27 name + message
            break;
        }
        case ChatChannel::Party:    net::Net_SendOp38(*net_, msg); break; // 0x26
        case ChatChannel::Alliance: net::Net_SendOp68(*net_, msg); break; // 0x44
        case ChatChannel::Guild:    net::Net_SendOp77(*net_, msg); break; // 0x4d
        case ChatChannel::Trade:    net::Net_SendOp81(*net_, msg); break; // 0x51
        case ChatChannel::Faction:  net::Net_SendOp40(*net_, msg); break; // 0x28
        case ChatChannel::Normal:   net::Net_SendOp80(*net_, msg); break; // 0x50 (say)
    }
}

// -----------------------------------------------------------------------------
// UI_Chat_SubmitInput 0x68B330: prefix then routing by channel.
void ChatWindow::Submit() {
    if (input_.empty()) return;

    const std::string text = input_;
    input_.clear();
    caret_ = 0;

    // Channel prefix detection (! guild / # faction / @ trade / ~ alliance).
    ChatChannel prefixCh = channel_;
    bool hasPrefix = true;
    switch (text[0]) {
        case '!': prefixCh = ChatChannel::Guild;    break;
        case '#': prefixCh = ChatChannel::Faction;  break;
        case '@': prefixCh = ChatChannel::Trade;    break;
        case '~': prefixCh = ChatChannel::Alliance; break;
        default:  hasPrefix = false;                break;
    }

    const std::string body = hasPrefix ? text.substr(1) : text;
    if (hasPrefix && body.empty()) return; // "!" alone: nothing to send (v36[0]==0)

    // Banned word filter -> system line (StrTable005 id 112 in the original).
    if (Banned(body)) {
        AddSystemLine("Message bloque (mot filtre).", kColorSystem);
        return;
    }

    const ChatChannel ch = hasPrefix ? prefixCh : channel_;

    if (ch == ChatChannel::Whisper && whisperTarget_.empty()) {
        AddSystemLine("Aucune cible de chuchotement.", kColorSystem);
        return;
    }

    SendOnChannel(ch, body);
    // Local echo (the server normally echoes the line back; useful offline).
    AddChannelLine(ch, "moi", body);
}

// -----------------------------------------------------------------------------
// Chat_SubmitTypedMessage 0x5C3CF0: simple "say" box -> Op80.
void ChatWindow::SubmitSay() {
    if (input_.empty()) return;
    const std::string body = input_;
    input_.clear();
    caret_ = 0;

    if (Banned(body)) {
        AddSystemLine("Message bloque (mot filtre).", kColorSystem);
        return;
    }
    SendOnChannel(ChatChannel::Normal, body);
    AddChannelLine(ChatChannel::Normal, "moi", body);
}

// -----------------------------------------------------------------------------
// Channel / target.
void ChatWindow::CycleChannel() {
    static const ChatChannel kOrder[] = {
        ChatChannel::Normal, ChatChannel::Party, ChatChannel::Guild,
        ChatChannel::Faction, ChatChannel::Trade, ChatChannel::Alliance,
        ChatChannel::Whisper,
    };
    const int n = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        if (kOrder[i] == channel_) { idx = i; break; }
    }
    channel_ = kOrder[(idx + 1) % n];
}

void ChatWindow::SetWhisperTarget(const std::string& name) {
    whisperTarget_ = name.substr(0, static_cast<size_t>(kNameLen - 1));
    channel_ = ChatChannel::Whisper;
}

// -----------------------------------------------------------------------------
// Keyboard input. Win32 VK codes (avoids including windows.h for these constants).
namespace {
constexpr int kVK_BACK   = 0x08;
constexpr int kVK_TAB    = 0x09;
constexpr int kVK_RETURN = 0x0D;
constexpr int kVK_ESCAPE = 0x1B;
constexpr int kVK_PRIOR  = 0x21; // Page Up
constexpr int kVK_NEXT   = 0x22; // Page Down
constexpr int kVK_END    = 0x23;
constexpr int kVK_HOME   = 0x24;
constexpr int kVK_LEFT   = 0x25;
constexpr int kVK_RIGHT  = 0x27;
constexpr int kVK_DELETE = 0x2E;
} // namespace

// TODO [anchor 0x461930 / SceneManager.cpp L904/L908]: InGame keyboard routing NOT
// wired (mission W4-F2). OnChar/OnKey below are complete and faithful (Enter ->
// UI_Chat_FocusInput 0x68B200 / UI_Chat_SubmitInput 0x68B330, Escape/Backspace/arrows/
// Tab/PageUp-Down, caret insertion) but NOTHING calls them in the world scene:
// Scene/SceneManager.cpp::OnChar (L904) and OnKeyDown (L908) only dispatch to login_
// (Login/CharSelect scene). Missing link (file NOT owned by this front-end): add,
// under scene_==Scene::World, hud_->Chat().OnChar(c) / hud_->Chat().OnKey(vk) (accessor
// GameHud::Chat() already exposed, GameHud.h L166). WM_CHAR/WM_KEYDOWN source = App_WndProc
// 0x461930 -> App.cpp -> SceneManager (not owned). Same for Chat().Bind(net::NetClient*)
// (ChatWindow.h L120) to be called from SceneManager to enable network sending.
bool ChatWindow::OnKey(int vk) {
    // Enter: opens the input (App_WndProc WM_KEYDOWN VK_RETURN -> UI_Chat_FocusInput),
    // or submits then closes.
    if (vk == kVK_RETURN) {
        if (!focused_) { Focus(); }
        else { Submit(); Unfocus(); }
        return true;
    }

    if (!focused_) {
        // Out of input mode: only log scrolling is captured.
        if (vk == kVK_PRIOR) { ++scroll_; return true; }
        if (vk == kVK_NEXT)  { if (scroll_ > 0) --scroll_; return true; }
        return false; // everything else goes to the game / DirectInput
    }

    // While typing: capture everything (indicator "text in progress", focusIndex != 0).
    const int len = static_cast<int>(input_.size());
    switch (vk) {
        case kVK_ESCAPE: input_.clear(); Unfocus(); break;
        case kVK_BACK:   if (caret_ > 0) { input_.erase(static_cast<size_t>(--caret_), 1); } break;
        case kVK_DELETE: if (caret_ < len) { input_.erase(static_cast<size_t>(caret_), 1); } break;
        case kVK_LEFT:   if (caret_ > 0) --caret_; break;
        case kVK_RIGHT:  if (caret_ < len) ++caret_; break;
        case kVK_HOME:   caret_ = 0; break;
        case kVK_END:    caret_ = len; break;
        case kVK_TAB:    CycleChannel(); break;
        case kVK_PRIOR:  ++scroll_; break;
        case kVK_NEXT:   if (scroll_ > 0) --scroll_; break;
        default: break; // key captured with no effect
    }
    return true;
}

bool ChatWindow::OnChar(char c) {
    if (!focused_) return false;
    // Control characters (Enter/Backspace/Escape) handled by OnKey.
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 32 || uc == 127) return true;
    if (static_cast<int>(input_.size()) >= kInputLimit) return true;
    input_.insert(static_cast<size_t>(caret_), 1, c);
    ++caret_;
    return true;
}

// -----------------------------------------------------------------------------
// Render: background (SpriteBatch) + tabs/badges, then system list + log +
// tabs(text) + input (Font). Bottom-of-screen vertical stacking (§13):
//   ... log (stacked upward, bottomY=screenH-64) ...
//   badgesY = screenH-54  (§13d, 8 notification badges)
//   tabsY   = screenH-40  (§13b, 5 channel tabs)
//   inputY  = screenH-20  (input)
void ChatWindow::Render(gfx::SpriteBatch& sprites, gfx::Font& font) {
    // HUD floating notices (HUD_RenderFloatingMessages 0x5AF4C0) BEFORE everything
    // else: order faithful to UI_RenderAllDialogs 0x5AE2D0, which calls
    // HUD_RenderFloatingMessages @0x5AE5A7 (this = dword_1821D58) THEN
    // UI_SysMsgList_Render 0x5AEC80 @0x5AE5B9 (this = dword_1822350, = the system list
    // drawn below). Standalone component: manages its own sprite/font passes,
    // hence the call outside the batches opened below.
    notices_.Render(sprites, font, now_, screenW_, screenH_);

    const int total   = static_cast<int>(chat_.size());
    const int visible = expanded_ ? kVisibleLines : kCollapsedLines;
    const int maxScroll = std::max(0, total - visible);
    scroll_ = std::min(std::max(scroll_, 0), maxScroll);

    const int badgesY = screenH_ - 54;
    const int tabsY   = screenH_ - 40;
    const int inputY  = screenH_ - 20;

    // "Panels" pass: optional background (UI sprite already uploaded) + tabs +
    // badges (solid rects as long as no .IMG icon is resolved).
    if (sprites.Ready()) {
        sprites.Begin();
        if (bgTex_) sprites.DrawSprite(bgTex_, nullptr, bgX_, bgY_);
        RenderTabPanels(sprites, tabsY, badgesY);
        sprites.End();
    }

    if (!font.Ready()) return;
    font.BeginBatch();

    // System message list (top-left): most recent first, 60s expiry
    // out of focus (UI_SysMsgList_Render 0x5AEC80).
    int sy = 64;
    int shownSys = 0;
    for (auto it = sys_.rbegin(); it != sys_.rend() && shownSys < 8; ++it) {
        if (!focused_ && (now_ - it->t) > kSysLineTtl) break;
        font.DrawTextStyled(it->text.c_str(), originX_, sy, it->color, gfx::kStyleOutline);
        sy += lineH_;
        ++shownSys;
    }

    // Chat log (bottom-left), stacked upward. scroll_ = lines scrolled back.
    const int bottomY = screenH_ - 64;
    const int end   = total - scroll_;              // exclusive bottom index
    const int start = std::max(0, end - visible);
    int y = bottomY - lineH_;
    for (int i = end - 1; i >= start; --i) {
        const ChatLine& l = chat_[static_cast<size_t>(i)];
        const std::string s = l.sender.empty() ? l.text
                                               : ("[" + l.sender + "] " + l.text);
        font.DrawTextStyled(s.c_str(), originX_, y, l.color, gfx::kStyleOutline);
        y -= lineH_;
    }

    // Channel tabs (labels; backgrounds/badges were already drawn in the
    // panels phase above).
    RenderTabLabels(font, tabsY);

    // Input line (if focused): channel prefix + text + blinking caret.
    if (focused_) {
        const int iy = inputY;
        const char* prompt = ChannelPrefix(channel_);
        const Color pc = ChannelColor(channel_);
        font.DrawTextStyled(prompt, originX_, iy, pc, gfx::kStyleOutline);
        const int px = originX_ + font.MeasureText(prompt) + 6;
        font.DrawTextStyled(input_.c_str(), px, iy, kColorNormal, gfx::kStyleOutline);
        // Caret: "_" at the cursor position, blinks at 2 Hz.
        if ((static_cast<int>(now_ * 2.0f) & 1) == 0) {
            const std::string left = input_.substr(0, static_cast<size_t>(caret_));
            const int cx = px + font.MeasureText(left.c_str());
            font.DrawTextStyled("_", cx, iy, kColorNormal, gfx::kStyleNormal);
        }
    }

    font.EndBatch();
}

} // namespace ts2::ui

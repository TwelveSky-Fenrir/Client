// UI/ChatWindow.cpp - implementation du chat & de la liste systeme.
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant
// <windows.h>), puis Gfx/ (qui tire <windows.h>/<d3d9.h>/<d3dx9.h>), enfin le
// header du module. Respecte la convention Winsock du projet.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 puis windows (ordre sur)
#include "Net/NetClient.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "UI/ChatWindow.h"
#include "Game/ClientRuntime.h"   // game::MessageLog/MsgKind (SyncFromMessageLog) : pas de windows.h, inclusion sure ici
#include "Game/StringTables.h"    // game::g_Strings.colors (palette mFONTCOLOR) : std lib seulement, sur apres Net/

#include <algorithm>
#include <cstring>

namespace ts2::ui {

// Onglets de canal (§13b, EA 0x683B71) : x = +3,+80,+157,+234,+311.
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
// Correspondances canal -> couleur. Lit la palette REELLE mFONTCOLOR
// (ColorTable_InitPalette 0x4C1D60) via game::g_Strings.colors : le binaire stocke
// l'index de canal dans la ligne de chat (Msg_AppendChatLine 0x68DB50) et le resout
// au DESSIN via ColorTable_GetColor 0x4C1FE0 ; on resout ici a l'emission (meme pixel).
// Normal/Alliance n'ont aucun index prouve dans 0x4C1D60 -> repli local documente.
Color ChatWindow::ChannelColor(ChatChannel ch) {
    const game::ColorPalette& pal = game::g_Strings.colors; // instance mFONTCOLOR 0x84DF20
    switch (ch) {
        case ChatChannel::Whisper:  return pal.WhisperColor(); // g_ChatColor_Whisper 0x84DFDC idx1  (0x4C1FE0)
        case ChatChannel::Party:    return pal.PartyColor();   // g_ChatColor_Party   0x84DFE0 idx40 (0x4C1FE0)
        case ChatChannel::Guild:    return pal.GuildColor();   // g_ChatColor_Guild   0x84DFE8 idx36 (0x4C1FE0)
        case ChatChannel::Faction:  return pal.FactionColor(); // g_ChatColor_Faction 0x84DFEC idx37 (0x4C1FE0)
        case ChatChannel::Trade:    return pal.TradeColor();   // g_ChatColor_Trade   0x84DFF0 idx38 (0x4C1FE0)
        case ChatChannel::Alliance: return kColorAlliance;     // repli : aucun index prouve (0x4C1D60)
        case ChatChannel::Normal:   return kColorNormal;       // repli : Op80 "dire" local sans index (0x4C1D60)
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
// Anneaux (Msg_AppendChatLine / Msg_AppendSystemLine : capacite 300, purge FIFO).
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
    // §13b : badge "non lu" (this+620..636) si le message arrive sur un onglet
    // qui n'est pas l'onglet actif courant.
    const int idx = TabIndexForChannel(ch);
    if (idx >= 0 && ch != channel_) unread_[static_cast<size_t>(idx)] = true;
}

void ChatWindow::AddSystemLine(const std::string& text, Color color) {
    PushSys(SysLine{ text, color, now_ });
}

// -----------------------------------------------------------------------------
// Onglets de canal (§13b) : correspondance canal -> index d'onglet, selection.
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
    const int tabsY = screenH_ - 40; // doit rester en phase avec Render()
    for (int i = 0; i < kTabCount; ++i) {
        const int tx = originX_ + kTabs[i].xOffset;
        if (x >= tx && x < tx + kTabWidth && y >= tabsY && y < tabsY + kTabHeight) {
            SelectTab(i);
            return true;
        }
    }
    return false;
}

// §13d : 8 badges de notification generiques (this+128..+160, pas 4).
void ChatWindow::SetNotificationBadge(int idx, bool on) {
    if (idx < 0 || idx >= static_cast<int>(notif_.size())) return;
    notif_[static_cast<size_t>(idx)] = on;
}

// -----------------------------------------------------------------------------
// Alimentation depuis game::g_Client.msg (Msg_AppendSystemLine/Msg_AppendChatLine
// reels, deja pousses par les handlers reseau/gameplay -- cf. Game/ClientRuntime.h
// et SceneManager.cpp::AppendKeepAliveFailedMessage). Ne republie que les lignes
// ajoutees depuis le dernier appel : recherche la derniere ligne deja consommee
// en repartant de la fin (survit a l'eviction FIFO cote source, kMaxLines=256,
// qui peut differer du kMaxLines=300 local).
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
        if (!found) startIdx = 0; // ligne reperee evincee -> republie tout (best effort)
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
            default:
                break; // bannieres HUD flottantes : hors perimetre fenetre de chat
        }
    }

    const auto& last = lines.back();
    syncedText_ = last.text;
    syncedWho_  = last.who;
    syncedKind_ = static_cast<int>(last.kind);
    syncedAny_  = true;
}

// -----------------------------------------------------------------------------
// Rect plein (texture blanche 1x1 paresseuse, memes mecaniques que
// UI/GameHud.cpp::CreateWhiteTexture, sans dependre de gfx::Renderer ici).
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

// Passe "panneaux" (dans sprites.Begin()/End()) : fonds d'onglets + badges.
void ChatWindow::RenderTabPanels(gfx::SpriteBatch& sprites, int tabsY, int badgesY) {
    if (!EnsureWhiteTexture(sprites)) return;

    // §13d : 8 badges de notification generiques, empiles horizontalement.
    for (int i = 0; i < static_cast<int>(notif_.size()); ++i) {
        const int bx = originX_ + i * 14;
        const Color c = notif_[static_cast<size_t>(i)] ? 0xFFFFC020u : 0x40606060u;
        DrawFilledRect(sprites, bx, badgesY, 10, 10, c);
    }

    // §13b : fonds des 5 onglets de canal + pastille "non lu".
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

// Passe "texte" (dans font.BeginBatch()/EndBatch()) : libelles des onglets.
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
// Filtre de mots bannis (maybe_Dict001_MatchWord 0x4C1410) via hook injectable.
bool ChatWindow::Banned(const std::string& s) const {
    return wordFilter_ && wordFilter_(s.c_str());
}

// -----------------------------------------------------------------------------
// Emission : construit un payload 61 o (NUL-rempli) et route vers le bon builder
// Net_Send* (voir Net/SendPackets.h). Whisper ajoute une cible 13 o.
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
            net::Net_SendOp39(*net_, target, msg);   // 0x27 nom + message
            break;
        }
        case ChatChannel::Party:    net::Net_SendOp38(*net_, msg); break; // 0x26
        case ChatChannel::Alliance: net::Net_SendOp68(*net_, msg); break; // 0x44
        case ChatChannel::Guild:    net::Net_SendOp77(*net_, msg); break; // 0x4d
        case ChatChannel::Trade:    net::Net_SendOp81(*net_, msg); break; // 0x51
        case ChatChannel::Faction:  net::Net_SendOp40(*net_, msg); break; // 0x28
        case ChatChannel::Normal:   net::Net_SendOp80(*net_, msg); break; // 0x50 (dire)
    }
}

// -----------------------------------------------------------------------------
// UI_Chat_SubmitInput 0x68B330 : prefixe puis routage par canal.
void ChatWindow::Submit() {
    if (input_.empty()) return;

    const std::string text = input_;
    input_.clear();
    caret_ = 0;

    // Detection du prefixe de canal (! guilde / # faction / @ commerce / ~ alliance).
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
    if (hasPrefix && body.empty()) return; // "!" seul : rien a envoyer (v36[0]==0)

    // Filtre de mots bannis -> ligne systeme (StrTable005 id 112 dans l'original).
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
    // Echo local (le serveur renvoie normalement la ligne ; utile hors-ligne).
    AddChannelLine(ch, "moi", body);
}

// -----------------------------------------------------------------------------
// Chat_SubmitTypedMessage 0x5C3CF0 : boite " dire " simple -> Op80.
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
// Canal / cible.
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
// Saisie clavier. Codes VK Win32 (evite d'inclure windows.h pour ces constantes).
namespace {
constexpr int kVK_BACK   = 0x08;
constexpr int kVK_TAB    = 0x09;
constexpr int kVK_RETURN = 0x0D;
constexpr int kVK_ESCAPE = 0x1B;
constexpr int kVK_PRIOR  = 0x21; // Page precedente
constexpr int kVK_NEXT   = 0x22; // Page suivante
constexpr int kVK_END    = 0x23;
constexpr int kVK_HOME   = 0x24;
constexpr int kVK_LEFT   = 0x25;
constexpr int kVK_RIGHT  = 0x27;
constexpr int kVK_DELETE = 0x2E;
} // namespace

// TODO [ancre 0x461930 / SceneManager.cpp L904/L908] : routage clavier InGame NON
// cable (mission W4-F2). OnChar/OnKey ci-dessous sont complets et fideles (Entree ->
// UI_Chat_FocusInput 0x68B200 / UI_Chat_SubmitInput 0x68B330, Echap/Backspace/fleches/
// Tab/PageUp-Down, insertion caret) mais RIEN ne les appelle en scene monde :
// Scene/SceneManager.cpp::OnChar (L904) et OnKeyDown (L908) ne dispatchent qu'a login_
// (scene Login/CharSelect). Maillon manquant (fichier NON possede par ce front) : ajouter,
// sous scene_==Scene::World, hud_->Chat().OnChar(c) / hud_->Chat().OnKey(vk) (accessor
// GameHud::Chat() deja expose, GameHud.h L166). Source WM_CHAR/WM_KEYDOWN = App_WndProc
// 0x461930 -> App.cpp -> SceneManager (non possedes). Idem Chat().Bind(net::NetClient*)
// (ChatWindow.h L120) a appeler depuis SceneManager pour activer l'envoi reseau.
bool ChatWindow::OnKey(int vk) {
    // Entree : ouvre la saisie (App_WndProc WM_KEYDOWN VK_RETURN -> UI_Chat_FocusInput),
    // ou soumet puis referme.
    if (vk == kVK_RETURN) {
        if (!focused_) { Focus(); }
        else { Submit(); Unfocus(); }
        return true;
    }

    if (!focused_) {
        // Hors saisie : seul le defilement du journal est capture.
        if (vk == kVK_PRIOR) { ++scroll_; return true; }
        if (vk == kVK_NEXT)  { if (scroll_ > 0) --scroll_; return true; }
        return false; // le reste part au jeu / DirectInput
    }

    // En saisie : on capture tout (indicateur " texte en cours ", focusIndex != 0).
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
        default: break; // touche capturee sans effet
    }
    return true;
}

bool ChatWindow::OnChar(char c) {
    if (!focused_) return false;
    // Caracteres de controle (Entree/Retour/Echap) geres par OnKey.
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 32 || uc == 127) return true;
    if (static_cast<int>(input_.size()) >= kInputLimit) return true;
    input_.insert(static_cast<size_t>(caret_), 1, c);
    ++caret_;
    return true;
}

// -----------------------------------------------------------------------------
// Rendu : fond (SpriteBatch) + onglets/badges, puis liste systeme + journal +
// onglets(texte) + saisie (Font). Empilement vertical bas d'ecran (§13) :
//   ... journal (empile vers le haut, bottomY=screenH-64) ...
//   badgesY = screenH-54  (§13d, 8 badges de notification)
//   tabsY   = screenH-40  (§13b, 5 onglets de canal)
//   inputY  = screenH-20  (saisie)
void ChatWindow::Render(gfx::SpriteBatch& sprites, gfx::Font& font) {
    const int total   = static_cast<int>(chat_.size());
    const int visible = expanded_ ? kVisibleLines : kCollapsedLines;
    const int maxScroll = std::max(0, total - visible);
    scroll_ = std::min(std::max(scroll_, 0), maxScroll);

    const int badgesY = screenH_ - 54;
    const int tabsY   = screenH_ - 40;
    const int inputY  = screenH_ - 20;

    // Passe "panneaux" : fond optionnel (sprite d'UI deja uploade) + onglets +
    // badges (rects pleins tant qu'aucune icone .IMG n'est resolue).
    if (sprites.Ready()) {
        sprites.Begin();
        if (bgTex_) sprites.DrawSprite(bgTex_, nullptr, bgX_, bgY_);
        RenderTabPanels(sprites, tabsY, badgesY);
        sprites.End();
    }

    if (!font.Ready()) return;
    font.BeginBatch();

    // Liste des messages systeme (haut-gauche) : recentes d'abord, expiration 60 s
    // hors focus (UI_SysMsgList_Render 0x5AEC80).
    int sy = 64;
    int shownSys = 0;
    for (auto it = sys_.rbegin(); it != sys_.rend() && shownSys < 8; ++it) {
        if (!focused_ && (now_ - it->t) > kSysLineTtl) break;
        font.DrawTextStyled(it->text.c_str(), originX_, sy, it->color, gfx::kStyleOutline);
        sy += lineH_;
        ++shownSys;
    }

    // Journal de chat (bas-gauche), empile vers le haut. scroll_ = lignes remontees.
    const int bottomY = screenH_ - 64;
    const int end   = total - scroll_;              // index exclusif du bas
    const int start = std::max(0, end - visible);
    int y = bottomY - lineH_;
    for (int i = end - 1; i >= start; --i) {
        const ChatLine& l = chat_[static_cast<size_t>(i)];
        const std::string s = l.sender.empty() ? l.text
                                               : ("[" + l.sender + "] " + l.text);
        font.DrawTextStyled(s.c_str(), originX_, y, l.color, gfx::kStyleOutline);
        y -= lineH_;
    }

    // Onglets de canal (libelles ; les fonds/badges sont deja passes en phase
    // panneaux ci-dessus).
    RenderTabLabels(font, tabsY);

    // Ligne de saisie (si focus) : prefixe de canal + texte + caret clignotant.
    if (focused_) {
        const int iy = inputY;
        const char* prompt = ChannelPrefix(channel_);
        const Color pc = ChannelColor(channel_);
        font.DrawTextStyled(prompt, originX_, iy, pc, gfx::kStyleOutline);
        const int px = originX_ + font.MeasureText(prompt) + 6;
        font.DrawTextStyled(input_.c_str(), px, iy, kColorNormal, gfx::kStyleOutline);
        // Caret : " _ " a la position du curseur, clignote a 2 Hz.
        if ((static_cast<int>(now_ * 2.0f) & 1) == 0) {
            const std::string left = input_.substr(0, static_cast<size_t>(caret_));
            const int cx = px + font.MeasureText(left.c_str());
            font.DrawTextStyled("_", cx, iy, kColorNormal, gfx::kStyleNormal);
        }
    }

    font.EndBatch();
}

} // namespace ts2::ui

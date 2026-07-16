// UI/ChatWindow.h - fenetre de chat & liste de messages systeme du client TS2.
//
// Reecriture PRAGMATIQUE (squelette fonctionnel) du sous-systeme chat de
// TwelveSky2, d'apres Docs/TS2_CLIENT_SHELL.md #2.5 et le desassemblage :
//   UI_Chat_SubmitInput     0x68B330  parse prefixe (!/#/@/~) + route par canal
//   Chat_SubmitTypedMessage 0x5C3CF0  boite " dire " simple -> Op80
//   Msg_AppendChatLine      0x68DB50  anneau de 300 lignes de chat (+ expediteur)
//   Msg_AppendSystemLine    0x68D9D0  anneau de 300 lignes systeme (expiration 60 s)
//   UI_SysMsgList_Render    0x5AEC80  liste systeme + compte a rebours 60 s
//   UI_GameHud_Render 0x67A3C0 §13    fenetre de chat & messages systeme (bas-gauche),
//     decompilee integralement dans Docs/TS2_UI_GAMEHUD_RENDER.md :
//       §13b (EA 0x683B71-0x684019) onglets de canal (switch this+644, 1-5) +
//            badges "non lu" par canal (this+620..636, 5 booleens, icone commune
//            unk_924944 aux memes x que les onglets) + previsualisation de saisie
//            coloree par canal ;
//       §13c (EA 0x6840AC-0x68472B) les DEUX anneaux (systeme this+672 / chat
//            this+38488, 122 o/entree, couleur par entree) ;
//       §13d (EA 0x684736-0x6848DB) 8 badges de notification empiles (this+128..
//            +160, pas 4), x croissant de 23 px.
//
// L'objet d'origine est le singleton g_ChatManager 0x184C3C8 (~80 Ko : deux
// journaux en anneau, mode de canal +644, cible de chuchotement +648). Ici on ne
// reproduit PAS la disposition octet-exacte : on garde la SEMANTIQUE (deux
// anneaux, canaux, prefixes, filtre de mots, onglets cliquables, badges de
// notification) avec des conteneurs C++ propres.
//
// Deviations pragmatiques assumees :
//   - la saisie n'utilise pas les EDIT Win32 (UI_CreateEditBoxes 0x50E460) mais un
//     tampon interne pilote par OnKey/OnChar ;
//   - les libelles localises (StrTable005_Get) et le filtre de mots bannis
//     (maybe_Dict001_MatchWord 0x4C1410) sont fournis via des valeurs par defaut /
//     un hook injectable ;
//   - echo local des messages emis (le serveur les renvoie normalement) pour que
//     l'UI soit visible hors-ligne ;
//   - les 17 icones .IMG des onglets/badges (unk_91A358.., unk_924944,
//     unk_8EA960..8EAE00) n'etant pas resolues (pas de string/skin table chargee
//     dans cette passe), les onglets/badges sont dessines en rects pleins
//     teintes + libelle texte (repli assume, cf. Docs/TS2_UI_GAMEHUD_RENDER.md
//     §19) plutot que de bloquer le rendu ;
//   - le flux game::g_Client.msg (Game/ClientRuntime.h) est optionnellement
//     rebranche via SyncFromMessageLog() : les lignes System/Chat/Whisper/Faction
//     deja poussees par les handlers reseau/gameplay alimentent les memes
//     anneaux que AddLine/AddSystemLine (MsgKind::Floating hors perimetre, ce
//     sont des bannieres HUD separees, non rendues ici — cf. HUD_RenderFloatingMessages
//     0x5AF4C0 : verifie sans animation, show/hide net par flag+timer 10s, voir
//     Game/ClientRuntime.h::MessageLog::Floating pour le detail).
//
// NB inclusion : ce header n'inclut ni <d3d9.h> ni <winsock2.h> (forward decls),
// afin que les .cpp gardent la liberte d'inclure Net/ (winsock2 en premier) avant
// Gfx/ (windows.h) sans conflit d'ordre.
#pragma once
#include <cstdint>
#include <string>
#include <deque>
#include <array>

// Interface COM D3D9 (fond optionnel) : forward-declaree, jamais dereferencee ici.
struct IDirect3DTexture9;

namespace ts2::gfx { class SpriteBatch; class Font; }
namespace ts2::net { struct NetClient; }
namespace ts2::game { class MessageLog; }

namespace ts2::ui {

// Couleur ARGB. Compatible D3DCOLOR (== DWORD) au site d'appel Font/SpriteBatch.
using Color = uint32_t;

// Canaux de chat. Les valeurs 0..5 correspondent au champ channelMode (this+644)
// de cChatManager tel que lu par UI_Chat_SubmitInput 0x68B330. Normal = " dire "
// local (Op80, Chat_SubmitTypedMessage 0x5C3CF0), sans channelMode d'origine.
enum class ChatChannel : int {
    Whisper  = 0, // -> Net_SendOp39(cible13, msg61)   (chuchotement)
    Party    = 1, // -> Net_SendOp38(msg61)            (groupe)
    Alliance = 2, // -> Net_SendOp68(msg61)            (alliance)
    Guild    = 3, // -> Net_SendOp77(msg61)            (guilde)
    Trade    = 4, // -> Net_SendOp81(msg61)            (commerce)
    Faction  = 5, // -> Net_SendOp40(msg61)            (faction)
    Normal   = 6, // -> Net_SendOp80(msg61)            (dire local, canal par defaut)
};

// Couleurs de canal : l'original resout a l'affichage la table de 8 index
// mFONTCOLOR (ColorTable_InitPalette 0x4C1D60 ; +184 = 0x84DFD8..0x84DFF4 =
// system/whisper/party/shout/guild/faction/trade/gm) via ColorTable_GetColor
// 0x4C1FE0. ChatWindow::ChannelColor() (voir ChatWindow.cpp) lit DESORMAIS ces
// couleurs REELLES depuis game::g_Strings.colors (Game/StringTables.h), au lieu des
// valeurs inventees precedentes (whisper/party/guild/faction/trade supprimees).
//
// Deux canaux N'ONT PAS d'index prouve dans la table (0x4C1D60 n'en definit que 8)
// et gardent donc un repli LOCAL documente :
//   - Normal : le "dire" local (Op80, Chat_SubmitTypedMessage 0x5C3CF0) n'est PAS le
//     "cri" (Shout = idx39, Pkt_ShoutMessage 0x48F640 = opcode 0x43) -> pas d'index.
//   - Alliance : aucun des 8 index de 0x4C1D60 ne correspond a l'alliance.
// kColorSystem reste un repli LOCAL pour les lignes systeme purement cosmetiques de ce
// widget (ex. "Message bloque") ; les VRAIES lignes systeme arrivent deja colorees
// (g_SysMsgColor 0x84DFD8 idx15) via game::g_Client.msg -> SyncFromMessageLog.
inline constexpr Color kColorSystem   = 0xFFC8C8C8u; // repli local (lignes systeme cosmetiques du widget)
inline constexpr Color kColorNormal   = 0xFFFFFFFFu; // repli : Op80 "dire" local, aucun index de palette prouve (0x4C1D60)
inline constexpr Color kColorAlliance = 0xFF80FFFFu; // repli : aucun index de canal alliance prouve (0x4C1D60)

// -----------------------------------------------------------------------------
// ChatWindow : deux journaux en anneau (chat + systeme), un canal courant, une
// saisie, et le rendu 2D (fond via SpriteBatch, texte via Font).
class ChatWindow {
public:
    // Capacite de chaque anneau (cChatManager : 300 lignes).
    static constexpr int   kMaxLines     = 300;
    // Nb de lignes de chat affichees (pageMode this+640 : 10 etendu / 5 replie).
    static constexpr int   kVisibleLines = 10;
    static constexpr int   kCollapsedLines = 5;
    // Limite de saisie (EM_LIMITTEXT canal chat = 0x3C).
    static constexpr int   kInputLimit   = 60;
    // Corps utile d'un message reseau (payload 61 o, NUL inclus).
    static constexpr int   kMsgLen       = 61;
    // Longueur d'un nom/cible (13 o, NUL inclus).
    static constexpr int   kNameLen      = 13;
    // TTL d'affichage d'une ligne systeme hors focus (UI_SysMsgList_Render : 60 s).
    static constexpr float kSysLineTtl   = 60.0f;

    ChatWindow();
    ~ChatWindow();
    ChatWindow(const ChatWindow&)            = delete;
    ChatWindow& operator=(const ChatWindow&) = delete;

    // Cable le client reseau : tant qu'il est nul, les envois sont ignores
    // (l'echo local reste actif pour le rendu).
    void Bind(net::NetClient* nc) { net_ = nc; }

    // Fond optionnel : sprite d'UI deja uploade en texture GPU (asset::ImgFile ->
    // gfx::GpuTexture). Blitte tel quel a (x,y) via SpriteBatch avant le texte.
    void SetBackground(IDirect3DTexture9* tex, int x, int y) { bgTex_ = tex; bgX_ = x; bgY_ = y; }

    // Dimensions ecran (pour ancrer la fenetre en bas-gauche). Defaut 1024x768.
    void SetScreenSize(int w, int h) { screenW_ = w; screenH_ = h; }

    // Met a jour l'horloge de jeu (g_GameTimeSec) pour estampiller / expirer.
    void Tick(float nowSec) { now_ = nowSec; }

    // --- Ajout de lignes -----------------------------------------------------
    // Msg_AppendChatLine 0x68DB50 : pousse une ligne dans l'anneau de chat.
    void AddLine(const std::string& text, Color color, const std::string& sender = {});
    // Raccourci : ligne de canal (couleur du canal + expediteur).
    void AddChannelLine(ChatChannel ch, const std::string& sender, const std::string& body);
    // Msg_AppendSystemLine 0x68D9D0 : pousse une ligne dans l'anneau systeme.
    void AddSystemLine(const std::string& text, Color color = kColorSystem);

    // --- Rendu (SpriteBatch pour le fond, Font pour le texte) ----------------
    void Render(gfx::SpriteBatch& sprites, gfx::Font& font);

    // --- Souris (onglets de canal cliquables, §13b) ---------------------------
    // Hit-test des 5 onglets de canal (Groupe/Chuchoter/Guilde/Faction/Commerce ;
    // Alliance/Dire restent accessibles par prefixe ou Tab, cf. CycleChannel).
    // Selectionne l'onglet et efface son badge "non lu". Renvoie true si
    // consomme ("premier consommateur gagne", meme convention que
    // UI/UIManager.h::Dialog::OnMouseDown).
    bool OnMouseDown(int x, int y);

    // --- Badges de notification generiques (§13d, 8 emplacements this+128..160)
    // idx hors [0,8) ignore silencieusement. Placeholder rect (pas d'icone
    // resolue) : cf. bandeau de tete.
    void SetNotificationBadge(int idx, bool on);

    // --- Alimentation depuis game::g_Client.msg (Game/ClientRuntime.h) --------
    // Republie dans les anneaux chat_/sys_ les lignes ajoutees a `log` depuis le
    // dernier appel (suit l'identite de la derniere ligne consommee pour
    // survivre a l'eviction FIFO cote source, kMaxLines=256). MsgKind::System ->
    // anneau systeme ; Chat/Whisper/Faction -> anneau chat (+ badge "non lu" si
    // le canal n'est pas l'onglet actif) ; Floating ignore (bannieres HUD
    // separees, hors perimetre fenetre de chat).
    void SyncFromMessageLog(const game::MessageLog& log);

    // --- Saisie --------------------------------------------------------------
    // WM_KEYDOWN : Entree (focus/soumission), Echap (annule), Retour arriere,
    // fleches (curseur/defilement), Tab (change de canal). Renvoie true si
    // l'evenement est consomme (le monde 3D ne doit pas le recevoir).
    bool OnKey(int vk);
    // WM_CHAR : insere un caractere imprimable dans la saisie (si focus).
    bool OnChar(char c);

    // UI_Chat_SubmitInput 0x68B330 : parse le prefixe (!,#,@,~) puis route selon
    // le canal courant ; filtre de mots bannis avant envoi.
    void Submit();
    // Chat_SubmitTypedMessage 0x5C3CF0 : boite " dire " simple -> Op80.
    void SubmitSay();

    // --- Etat ----------------------------------------------------------------
    void        SetChannel(ChatChannel ch) { channel_ = ch; }
    ChatChannel Channel() const { return channel_; }
    void        CycleChannel();
    // UI_Chat_SetWhisperMode 0x68B260 : fixe la cible et passe en canal chuchotement.
    void        SetWhisperTarget(const std::string& name);
    const std::string& WhisperTarget() const { return whisperTarget_; }

    bool Focused() const { return focused_; }
    void Focus()   { focused_ = true; }              // UI_Chat_FocusInput 0x68B200
    void Unfocus() { focused_ = false; caret_ = 0; }
    const std::string& Input() const { return input_; }

    // Etat replie/etendu du journal (pageMode this+640).
    void SetExpanded(bool v) { expanded_ = v; }
    bool Expanded() const { return expanded_; }

    // Hook filtre de mots bannis (maybe_Dict001_MatchWord 0x4C1410). Renvoie true
    // si le texte contient un mot banni -> message rejete. nullptr = pas de filtre.
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

    // --- Onglets de canal (§13b) : 5 emplacements, x = EA 0x683B71 (+3,+80,+157,
    // +234,+311). Alliance/Dire n'ont pas d'onglet dedie dans le HUD d'origine
    // (accessibles par prefixe ~ / Tab uniquement, cf. bandeau de tete doc §13b :
    // la table de couleurs de previsualisation ne cite que Party/Whisper/Guild/
    // Faction/Trade/Shout, pas Alliance).
    struct TabInfo { ChatChannel channel; const char* label; int xOffset; };
    static constexpr int kTabCount  = 5;
    static constexpr int kTabWidth  = 74;
    static constexpr int kTabHeight = 16;
    static const TabInfo kTabs[kTabCount];

    int  TabIndexForChannel(ChatChannel ch) const;
    void SelectTab(int idx);

    // Primitives rect plein (texture blanche 1x1 paresseuse, meme technique que
    // UI/GameHud.cpp::CreateWhiteTexture, mais creee via ID3DXSprite::GetDevice
    // pour ne pas dependre de gfx::Renderer dans ce widget).
    IDirect3DTexture9* EnsureWhiteTexture(gfx::SpriteBatch& sprites);
    void DrawFilledRect(gfx::SpriteBatch& sprites, int x, int y, int w, int h, Color color);
    void RenderTabPanels(gfx::SpriteBatch& sprites, int tabsY, int badgesY);
    void RenderTabLabels(gfx::Font& font, int tabsY);

    std::deque<ChatLine> chat_;
    std::deque<SysLine>  sys_;
    int   scroll_ = 0;   // decalage de defilement (lignes depuis le bas)

    std::array<bool, kTabCount> unread_{};   // badges "non lu" par onglet (this+620..636)
    std::array<bool, 8>         notif_{};    // badges de notification generiques (§13d)
    IDirect3DTexture9*          whiteTex_ = nullptr;

    // Identite de la derniere ligne de game::g_Client.msg deja republiee
    // (SyncFromMessageLog) : permet de ne pousser que les lignes nouvelles sans
    // dependre d'un compteur cumulatif absent de MessageLog.
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

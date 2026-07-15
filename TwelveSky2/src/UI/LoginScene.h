// UI/LoginScene.h — scènes shell de connexion du client TwelveSky2 (ts2::ui).
//
// Regroupe les trois scènes de l'amorçage réseau, câblées sur la même machine
// d'états que le binaire (cSceneMgr 0x1676180) :
//   - ServerSelect (id 2) : Scene_ServerSelect* (0x518B30 / 0x519250 / 0x519780)
//   - Login        (id 3) : Scene_Login*        (0x51A8D0 / 0x51B020 / 0x51B5D0 / 0x51B780)
//   - CharSelect   (id 4) : Scene_CharSelect*   (0x51BD90 / 0x51CED0 / 0x520F40 / 0x522E50)
//
// Flux de connexion (Docs/TS2_CLIENT_SHELL.md §4) :
//   ServerSelect --clic--> Login --OK--> ConnectLoginServer + LoginRequest(op0x0B,
//   ver 106) --succès--> CharSelect --Entrer--> ConnectGameServer(op0x0B,
//   WSAAsyncSelect 0x401) --code 0--> EnterWorld.
//
// Réécriture PRAGMATIQUE : un squelette fonctionnel qui compile et DESSINE
// (aplats colorés via SpriteBatch + libellés via Font) et pilote le vrai flux
// réseau (Net/Login.h) au-dessus de net::NetSystem, sans réimplémenter les 573
// fonctions UI ni le chargement des sprites .IMG (atlas UI unk_8E8B50) — les
// vrais assets se brancheront via gfx::SetSpriteTextureLoader.
//
// Les champs/boutons sont des widgets ts2::ui (EditBox/Button) manipulés
// EXCLUSIVEMENT par leur interface publique (setters/getters + événements).
//
// Convention Winsock du projet : Net/NetSystem.h -> Net/NetClient.h met
// <winsock2.h> AVANT <windows.h> ; on l'inclut EN TÊTE pour garantir l'ordre.
#pragma once
#include "Net/NetSystem.h"      // net::NetSystem / net::NetClient (winsock2 avant windows)
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <functional>
#include <mutex>                 // std::mutex — protège l'état serveur partagé avec le thread de statut
#include <string>
#include <thread>                // std::thread — worker de statut serveur (Net_ServerStatusThread 0x518AB0)
#include <vector>

#include "UI/Widgets.h"         // ts2::ui::EditBox / ts2::ui::Button
#include "UI/ServerSelectRender.h" // ts2::ui::ServerSelectRender (rendu géométrie réelle ServerSelect)
#include "UI/IntroRender.h"        // ts2::ui::IntroRender (rendu géométrie réelle Intro)
#include "UI/EnterWorldRender.h"   // ts2::ui::EnterWorldRender (rendu géométrie réelle EnterWorld)
#include "Game/IntroFlow.h"        // ts2::game::IntroState (paramètre de RenderIntro)
#include "Game/EnterWorldFlow.h"   // ts2::game::EnterWorldFlowState (paramètre de RenderEnterWorld)
#include "Game/CharSelectFlow.h"   // ts2::game::CharSelectState/Host — flux CharSelect fidèle
#include "Game/ServerSelectFlow.h" // ts2::game::ServerSelectState/Host — flux ServerSelect fidèle (liste RÉELLE + statut)
#include "Net/ServerStatusQuery.h" // ts2::net::QueryServerStatusLive — interrogation live population (contrat ss-netconnect)
#include "Audio/AudioSystem.h"     // audio::SoundBuffer — BGM front-end Z000.BGM (Scene_ServerSelectUpdate 0x518BF7)
#include "Gfx/SpriteBatch.h"    // gfx::SpriteBatch, gfx::SetActiveSprite
#include "Gfx/Font.h"           // gfx::Font
#include "Gfx/GpuTexture.h"     // gfx::GpuTexture (fond réel ServerSelect/Login, atlas unk_8E8B50)
#include "Scene/SceneManager.h" // ts2::Scene
#include <unordered_map>

namespace ts2::ui {

// Sous-états de la scène Login (this[1] de cSceneMgr — Scene_LoginUpdate 0x51A8D0).
enum class LoginSub {
    Init       = 0, // reset champs + focus ID
    Idle       = 1, // idle + heartbeat GameGuard (/30 frames)
    Trigger    = 2, // OK cliqué -> passe en DoLogin
    DoLogin    = 3, // lit ID/PW, ConnectLoginServer + LoginRequest
    // NoticeWait = 4 : AUCUN case 4 dans le vrai switch (tombe dans le
    // `default: return result;` de Scene_LoginUpdate — no-op à chaque frame).
    // La sortie réelle ne vient PAS de cette fonction : elle est pilotée par
    // UI_NoticeDlg_OnLButtonUp 0x5C03F0 (clic OK sur la notice), qui pour
    // TOUTES les notices ouvertes par Scene_LoginUpdate (this[4]=kind=2,
    // confirmé par désassemblage — `push 2` avant CHAQUE appel
    // UI_NoticeDlg_Open sur le singleton &byte_18225C8, sans exception, y
    // compris les notices ID/PW vides) exécute inconditionnellement, au clic
    // OK : Net_CloseSocket(&g_NetClient) + g_SceneMgr=2 (retour ServerSelect)
    // + g_SceneSubState=0 (EA 0x5C04DF-0x5C0502). Autrement dit : fermer
    // N'IMPORTE QUELLE notice affichée depuis l'écran Login renvoie TOUJOURS
    // à ServerSelect et ferme la socket — même pour un simple champ vide.
    // Reproduit fidèlement via le callback de fermeture de OpenNotice() (cf.
    // DoLogin()) plutôt que par un auto-retour vers Idle (ancien comportement
    // inventé, cf. Docs/TS2_LOGINSCENE_AUDIT.md §3.6 — écart fermé).
    NoticeWait = 4, // no-op par frame ; sortie pilotée par le clic OK de la notice (cf. ci-dessus)
};

// La liste de serveurs et le statut/population de chaque entrée sont désormais portés par
// le module fidèle Game/ServerSelectFlow.h (game::ServerSelectState/ServerEntry) : source
// de vérité du FLUX (BuildServerList/PollServerStatuses/UpdateServerSelect/OnServerClicked,
// mirroir de Scene_ServerSelect* 0x518B30/0x519250/0x519780 + Net_ServerStatusThread
// 0x518AB0). LoginScene ne redéfinit plus de ServerEntry local (l'ancien struct ts2::ui
// dupliquait ces champs) — cf. membre serverState_ ci-dessous.

// ---------------------------------------------------------------------------
// LoginScene — machine des scènes 2/3/4. À brancher dans SceneManager : sur
// Update/Render/OnLButton*, déléguer aux méthodes ci-dessous puis appliquer
// PendingScene() (Change + ClearPending).
class LoginScene {
public:
    LoginScene() = default;
    ~LoginScene();
    LoginScene(const LoginScene&) = delete;
    LoginScene& operator=(const LoginScene&) = delete;

    // Crée la police + le sprite batch depuis le device, construit la liste des
    // serveurs et réinitialise l'écran de login. `net` = système réseau partagé
    // (socket dword_8156A0 + dispatcher) ; `notifyWnd` = fenêtre des
    // notifications socket asynchrones (WM_USER+1).
    //
    // `serverModeFlag` = dword_166918C (g_ServerModeFlag) = GameConfig::buildVariant
    // (1er jeton `/N/...` de la ligne de commande, parsé par WinMain EA 0x4609F1/
    // 0x460BAE). Pilote la construction de la table serveurs (BuildServerList, keyée
    // sur ce flag, fidèle à Scene_ServerSelectUpdate 0x518B30). DÉFAUT 0 = mode
    // SingleServer, le SEUL mode actif pour la commande documentée `/0/0/2/1024/768`
    // — SceneManager peut passer cfg.buildVariant pour couvrir les builds non nuls
    // (cf. « Points d'attention » de la doc de session).
    bool Init(IDirect3DDevice9* device, net::NetSystem* net, HWND notifyWnd,
              int screenW, int screenH, int serverModeFlag = 0);
    void Shutdown();

    void SetScreenSize(int w, int h) { screenW_ = w; screenH_ = h; }
    void OnDeviceLost();   // autour de IDirect3DDevice9::Reset
    void OnDeviceReset();

    // --- Dispatch par scène (branché dans les cases 2/3/4 de SceneManager) ---
    void Update(ts2::Scene scene);
    void Render(ts2::Scene scene);

    // Rendu de l'écran Intro (Scene::Intro), câblé par SceneManager (ts2::game::IntroState
    // et son flux restent détenus par SceneManager, cf. Game/IntroFlow.h — LoginScene ne
    // pilote AUCUN état ici). Réutilise simplement les ressources GPU déjà créées par
    // Init() (sprite batch + police + texture blanche) : LoginScene est initialisée avant
    // l'Intro dans le bootstrap (SceneManager::Init crée+Init() login_ en premier), donc
    // ces ressources sont déjà prêtes quand l'Intro joue.
    void RenderIntro(const game::IntroState& state);

    // Rendu de l'écran de transition EnterWorld (Scene::EnterWorld), câblé par
    // SceneManager (ts2::game::EnterWorldFlowState reste détenu par SceneManager, cf.
    // Game/EnterWorldFlow.h — même découplage que RenderIntro ci-dessus). `zoneId` = même
    // valeur que celle passée à game::EnterWorldFlow_Update (dword_1675A9C d'origine).
    // Réutilise les ressources GPU déjà créées par Init() (sprite batch + police + texture
    // blanche) — voir UI/EnterWorldRender.h pour la géométrie réelle reproduite
    // (Scene_EnterWorldRender 0x52C260).
    void RenderEnterWorld(const game::EnterWorldFlowState& state, int zoneId);

    void OnMouseDown(ts2::Scene scene, int x, int y);
    void OnMouseUp(ts2::Scene scene, int x, int y);

    // Saisie clavier (à relayer depuis App_WndProc : WM_CHAR -> OnChar,
    // WM_KEYDOWN -> OnKeyDown). N'a de sens qu'en scène Login.
    void OnChar(char c);
    void OnKeyDown(int vk);

    // Transition demandée par la machine (None = rester). Le SceneManager doit la
    // consommer après Update/OnMouse* : Change(PendingScene()) puis ClearPending().
    ts2::Scene PendingScene() const { return pending_; }
    void       ClearPending()       { pending_ = ts2::Scene::None; }

private:
    // --- ServerSelect (scène 2) — flux fidèle : Game/ServerSelectFlow.h pilote la LISTE
    // RÉELLE (BuildServerList) et l'interrogation de population (PollServerStatuses via un
    // thread worker, cf. LaunchServerStatusThread) ; LoginScene route les entrées et dessine.
    void ServerSelectUpdate();
    void ServerSelectRender();
    void ServerSelectOnMouseDown(int x, int y);
    RECT ServerRowRect(int i) const;
    // Lance le worker de statut serveur (fidèle à CreateThread(Net_ServerStatusThread
    // 0x518AB0) au passage sous-état Init->Idle) : interroge en TCP bloquant borné
    // (ts2::net::QueryServerStatusLive) HORS boucle de rendu, publie les populations sous
    // verrou (serverMutex_). Le rendu lit ces int32 « au fur et à mesure » (curPop==-1 = en cours).
    void LaunchServerStatusThread();

    // --- Login (scène 3) ---
    void LoginUpdate();          // Scene_LoginUpdate 0x51A8D0
    void LoginRender();          // Scene_LoginRender 0x51B020
    void LoginOnMouseDown(int x, int y); // Scene_LoginOnMouseDown 0x51B5D0
    void LoginOnMouseUp(int x, int y);   // Scene_LoginOnMouseUp   0x51B780
    void DoLogin();              // sous-état 3
    // Action kind=2 de UI_NoticeDlg_OnLButtonUp 0x5C03F0 (EA 0x5C04DF-0x5C0502) : ferme la
    // socket et revient à ServerSelect. Passée comme `onClose` à OpenNotice() par DoLogin()
    // pour les 4 notices qu'elle ouvre (cf. LoginSub::NoticeWait).
    void AbortLoginToServerSelect();
    void LayoutLogin(int px, int py);
    void DrawFieldValue(const EditBox& box, int tx, int ty);
    // Caret réel du champ de saisie (Sprite2D_Draw(unk_8EA42C = slot 43 de l'atlas UI) à
    // panneau+largeurTexte+127, y — EA 0x51B34F (ID) / 0x51B445 (PW)). Dessiné dans le batch
    // SPRITE quand le champ est focalisé ; repli caret texte « | » (batch Font, DrawFieldValue)
    // uniquement si le sprite slot 43 est indisponible (cf. CaretSpriteReady).
    void DrawFieldCaretSprite(const EditBox& box, int tx, int ty);
    bool CaretSpriteReady();     // true si le sprite caret réel (slot 43) est chargeable
    void ResetLoginFields();
    void SetFocus(int field);    // 0=aucun, 1=ID, 2=PW (dword_1668FC0)

    // --- CharSelect (scène 4) — flux fidèle : Game/CharSelectFlow.h::CharSelectState/
    // Host pilotent la logique (sous-états/création/suppression/entrée) ; LoginScene ne
    // fait plus que router les entrées et dessiner l'état courant (cf. Docs/TS2_CHARSELECT_AUDIT.md).
    void CharSelectUpdate();
    void CharSelectRender();
    void CharSelectOnMouseDown(int x, int y);
    void CharSelectOnMouseUp(int x, int y);
    void BuildCharSelectHost();  // construit charHost_ (appelé une fois depuis Init())
    void LayoutCharSelect();     // écran Liste (this[15714]==1)
    void LayoutCreateForm();     // écran Formulaire de création (this[15714]==2)
    void CharListRender();
    void CreateFormRender();
    void DeleteConfirmRender();  // confirmation Oui/Non (host.ShowDeleteConfirm)
    RECT CharSlotRect(int i) const;

    // --- Notice modale (NoticeDlg 0x5C0280/0x5C03F0 simplifiée) ---
    // `onClose` reproduit l'action déclenchée par UI_NoticeDlg_OnLButtonUp au clic OK,
    // indexée par this[4] (« kind »). Les notices ouvertes par Scene_LoginUpdate ont
    // TOUTES kind=2 (cf. commentaire de LoginSub::NoticeWait) -> onClose doit fermer la
    // socket + revenir à ServerSelect ; nullptr (défaut) = fermeture simple sans effet de
    // bord (comportement des notices CharSelect, kind non vérifié ici, hors périmètre).
    void OpenNotice(const std::string& text, std::function<void()> onClose = nullptr);
    void CloseNotice(); // ferme la notice et exécute onClose (clic OK / Entrée / Échap)
    void RenderNotice();

    // --- Helpers de rendu / entrée ---
    void  FillRect(int x, int y, int w, int h, D3DCOLOR color); // via texture blanche 1x1
    POINT CursorClient() const;
    void  CreateWhiteTexture();
    // Construit la table de serveurs RÉELLE via game::BuildServerList(serverState_, mode)
    // (mode dérivé de serverModeFlag_). Remplace l'ancienne construction locale + AddServer().
    void  BuildServerList();
    static std::string ConnectErrText(int code);   // codes kNet* -> StrTable005 réel (game::Str)
    static std::string LoginErrText(int result);   // code serveur -> StrTable005 réel (game::Str)

    // --- Fond réel ServerSelect/Login (atlas unk_8E8B50, cf. LoginScene.cpp) ---
    // Scene_ServerSelectUpdate 0x518B30 (EA 0x518C29-0x518C40) : this[168] = 2380 ou 2381
    // (Rng_Next()%2, 50/50) UNE SEULE FOIS par entrée dans ServerSelect ; Scene_LoginRender
    // 0x51B020 (EA 0x51B207) RELIT LE MÊME this[168] (mémoire de scène partagée, non reset
    // entre ServerSelect et Login) -> Login affiche TOUJOURS le même fond que le tirage
    // ServerSelect qui a précédé. Reproduit ici par bgAtlasSlot_ (tiré une fois dans Init(),
    // simplification : l'original re-tire à chaque (ré)entrée dans ServerSelect, non modélisé
    // par ce squelette qui n'a pas de sous-état ServerSelect « Init » distinct).
    gfx::GpuTexture* GetAtlasSprite(int slotIndex); // lazy-load + cache (nullptr si échec)
    void DrawFullscreenBg(int slotIndex); // texture réelle plein écran (rien si non chargée — zéro repli)

    // Câble la paire générique de sprites "Confirm"/"Cancel" (slots 9/10 et 12/13 du pool
    // partagé unk_8E8B50 — Docs/TS2_LOGIN_BUTTON_ASSETS.md §4) sur un couple de boutons,
    // avec repli sur le rect coloré existant (SetFallbackColors/SetFallbackTexture) si les
    // fichiers .IMG sont réellement absents/illisibles au runtime. Réutilisée pour Login
    // (OK/Quitter), CharSelect liste (Entrer/Quitter) et la confirmation de suppression
    // (Oui/Non) — même paire de sprites que le binaire réutilise dans
    // Scene_CharSelectRender et les dialogues modales (doc §5, xrefs confirmées).
    void ApplyConfirmCancelSkin(Button& confirmBtn, Button& cancelBtn);

    // --- Dépendances ---
    IDirect3DDevice9* device_    = nullptr;
    net::NetSystem*   net_       = nullptr;         // socket + dispatcher partagés
    HWND              notifyWnd_ = nullptr;
    int               screenW_   = 1024;
    int               screenH_   = 768;

    gfx::SpriteBatch  sprites_;                 // batch 2D (aplats colorés + sprites)
    gfx::Font         font_;                    // police GXD (texte)
    IDirect3DTexture9* whiteTex_ = nullptr;     // 1x1 blanc (modulé -> aplats)
    ts2::ui::ServerSelectRender serverSelectRender_; // rendu réel ServerSelect (positions/dimensions binaire, cf. UI/ServerSelectRender.h)
    ts2::ui::IntroRender        introRender_;        // rendu réel Intro (positions/dimensions binaire, cf. UI/IntroRender.h)
    ts2::ui::EnterWorldRender   enterWorldRender_;   // rendu réel EnterWorld (positions/dimensions binaire, cf. UI/EnterWorldRender.h)

    // --- État Login ---
    LoginSub loginSub_ = LoginSub::Init;
    int      frame_    = 0;                     // this[2] (compteur sous-état)
    int      focusField_ = 0;                   // dword_1668FC0
    EditBox  idBox_, pwBox_;                    // dword_1668FC4 / dword_1668FC8
    Button   okBtn_, exitBtn_, optBtn_;         // this[3] / this[4] / this[5]
    bool     shadowsEnabled_ = false;           // g_Opt_GfxDetailShadows 0x84DEF8
    std::string loggedUser_;                    // ID validé (affiché en CharSelect)

    // --- Serveurs (flux fidèle, cf. Game/ServerSelectFlow.h) ---
    // serverState_ = table serveurs + populations + sélection (mirroir de la portion
    // ServerSelect de g_SceneMgr 0x1676180). serverHost_ = callbacks réseau/persistance :
    // QueryServerStatus branché sur ts2::net::QueryServerStatusLive (contrat ss-netconnect,
    // Net_QueryServerStatus 0x519CC0), SaveLastServer sur config::Cfg_SaveLastServer.
    game::ServerSelectState serverState_;
    game::ServerSelectHost  serverHost_;
    int  serverModeFlag_ = 0;                   // dword_166918C (g_ServerModeFlag) = buildVariant :
                                                // pilote BuildServerList (0 = SingleServer .com actif ;
                                                // 1/2 = SingleServer autre hôte ; sinon MultiChannel)

    // Worker de statut serveur (fidèle : CreateThread(Net_ServerStatusThread 0x518AB0)).
    // Interroge les populations en TCP bloquant borné HORS boucle de rendu. serverMutex_
    // protège l'accès concurrent aux champs population (int32) de serverState_.servers.
    std::thread statusThread_;
    std::mutex  serverMutex_;
    bool        statusThreadLaunched_ = false;  // lancement unique (au passage Init->Idle)

    // BGM du front-end (Scene_ServerSelectUpdate 0x518BF7 : Snd_LoadOggToBuffers(
    // "G03_GDATA\D10_WORLDBGM\Z000.BGM", boucle)). Chargée+jouée une seule fois au même
    // passage Init->Idle que le worker de statut ; loop continu couvrant ServerSelect ET
    // Login (front-end partagé). Décodage Ogg->PCM via le callback branché dans
    // AudioSystem::Init (Audio/OggVorbisDecoder).
    audio::SoundBuffer bgm_;

    // --- Fond réel ServerSelect/Login (atlas unk_8E8B50) ---
    int bgAtlasSlot_ = 2380;                     // this[168] (2380/2381, cf. GetAtlasSprite)
    // Fond réel CharSelect (this[15713] = slot atlas aléatoire 2383/2384/2385 — cf.
    // Docs/TS2_CHARSELECT_RE.md §4). Tiré une fois dans Init() (simplification comme bgAtlasSlot_).
    int charBgSlot_ = 2383;
    std::unordered_map<int, gfx::GpuTexture> atlasCache_; // slot -> texture (lazy)

    // --- CharSelect (flux fidèle, cf. Game/CharSelectFlow.h) ---
    game::CharSelectState charState_;  // sous-état/écran/slots/formulaire/aperçu
    game::CharSelectHost  charHost_;   // callbacks réseau/UI (construits par BuildCharSelectHost)

    // Écran Liste : slots (clic = sélection), Créer/Supprimer/Entrer/Quitter.
    Button enterBtn_, backBtn_;        // backBtn_ = "Quitter" (host.CloseConnectionAndQuit, fidèle)
    Button createBtn_, deleteBtn_;

    // Écran Formulaire de création : 5 paires +/- (job/faction/visage/couleur/variant),
    // nom saisi (EditBox réutilisé de Widgets.h), Confirmer/Annuler.
    Button  jobMinusBtn_, jobPlusBtn_, factionMinusBtn_, factionPlusBtn_;
    Button  faceMinusBtn_, facePlusBtn_, hairMinusBtn_, hairPlusBtn_;
    Button  variantMinusBtn_, variantPlusBtn_;
    Button  createConfirmBtn_, createCancelBtn_;
    EditBox createNameBox_;

    // Confirmation de suppression (Oui/Non), ouverte par host.ShowDeleteConfirm().
    bool   deleteConfirmOpen_ = false;
    Button deleteYesBtn_, deleteNoBtn_;

    // --- Notice ---
    bool        noticeOpen_ = false;
    std::string noticeText_;
    std::function<void()> noticeOnClose_; // action kind=2 (cf. OpenNotice) ; vide = no-op

    // --- Transition demandée ---
    ts2::Scene pending_ = ts2::Scene::None;
};

} // namespace ts2::ui

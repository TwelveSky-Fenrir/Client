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
#include "Gfx/Renderer.h"       // gfx::Renderer — requis par gfx::MeshRenderer::Init (aperçu 3D CharSelect)
#include "Gfx/CharPreview3D.h"  // gfx::CharPreview3D + MeshRenderer/ModelCache/MotionCache (Char_RenderModel 0x527020)
#include "Scene/SceneManager.h" // ts2::Scene
#include <memory>               // std::unique_ptr — ModelCache/MotionCache (non copiables, ctor à arguments)
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
    //
    // `renderer` (OPTIONNEL, défaut nullptr) = le gfx::Renderer possédé par SceneManager.
    // Requis UNIQUEMENT par l'aperçu 3D de CharSelect : gfx::MeshRenderer::Init() prend un
    // `gfx::Renderer&` (dont il ne lit que Device(), cf. Gfx/MeshRenderer.cpp) et il n'existe
    // aucun moyen de fabriquer un Renderer depuis un IDirect3DDevice9* (Renderer::Init crée
    // son PROPRE device). Paramètre DÉFAUTÉ pour ne pas casser l'unique appelant existant
    // (Scene/SceneManager.cpp:337), qui n'est pas un fichier de ce front.
    // ⚠ TANT QUE SceneManager NE PASSE PAS `&renderer` ICI, charPreviewReady_ reste false et
    //   les 4 Char_RenderModel (0x51D361/0x51D3CC/0x51D429/0x51D480) ne dessinent RIEN.
    //   -> wiring TODO CHARSELECT_3D (cf. rapport de front cs-render-2d).
    bool Init(IDirect3DDevice9* device, net::NetSystem* net, HWND notifyWnd,
              int screenW, int screenH, int serverModeFlag = 0,
              gfx::Renderer* renderer = nullptr);
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
    void ServerSelectOnMouseUp(int x, int y);   // Scene_ServerSelectOnMouseUp 0x519AC0 (confirme la sortie)
    void ExitConfirmRender();                    // overlay Oui/Non de sortie (UI_MsgBox dword_1822438, EA 0x519B3E)
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

    // --- CharSelect : ordre de peintre EXACT (Scene_CharSelectRender 0x51CED0) ---
    // fond (Begin2D..End2D) -> PASSE 3D -> UI 2D (Begin2D..End2D). Les trois helpers
    // ci-dessous découpent la fonction d'origine dans CET ordre ; CharSelectRender() les
    // enchaîne. Empiler fond+UI dans un seul Begin2D puis dessiner la 3D CASSE l'ordre.
    void CharSelectRenderBg();       // Sprite2D_DrawScaled(atlas+148*this[15713],0,0,nW/1024,nH/768) @0x51D2AB
    void CharSelectRenderPreview3D();// 4x Char_RenderModel entre End2D 0x51D2B5 et Begin2D 0x51D48A
    void CharSelectRenderUi2D();     // dispatch d'écran 2D (0x51D4CB) : Liste / Formulaire

    // Panneau de détail GAUCHE de l'écran Liste, origine ABSOLUE (15,19).
    // GARDE : `cmp [ecx+0F58Ch], -1 ; jz loc_51DF0D` @0x51D7CA -> RIEN si aucun slot
    // sélectionné (garde ABSENTE de la spec consolidée §8.3, re-prouvée par désassemblage).
    void CharDetailPanelRender();
    // Colonne des 10 boutons (origine (nWidth-140, nHeight-301), pas 37 ; #8/#9 ABSOLUS).
    void CharButtonColumnRender();

    // Motif canonique du bouton 3 ÉTATS (slots CONSÉCUTIFS n/n+1/n+2), EA de référence
    // 0x51DF32-0x51DF9A (ENTRER) — identique pour les 10 boutons, sans exception :
    //   if (latch)                          Draw(base+2 /*pressé*/, x, y);
    //   else if (HitTest(base /*normal*/))  Draw(base+1 /*survol*/, x, y);
    //   else                                Draw(base   /*normal*/, x, y);
    // ⚠ Le hit-test porte TOUJOURS sur le sprite NORMAL, même quand un autre état est peint.
    void DrawTriStateSprite(int slotNormal, int x, int y, bool latched, int mouseX, int mouseY);
    // Sprite2D_HitTest 0x4D6C50 sur un slot d'atlas : bornes >= gauche/haut et < droite/bas.
    // Renvoie false si le sprite n'est pas chargeable (dimensions inconnues -> aucun rect).
    bool AtlasHitTest(int slotIndex, int x, int y, int mouseX, int mouseY);

    // Lit un int32 dans la fiche BRUTE de 10088 o du slot `slot` (net::g_CharRecords[slot]
    // == &unk_1669380 + 0x2768*slot). Nécessaire parce que game::CharSlotInfo
    // (Game/CharSelectFlow.h, HORS de ce front) n'expose PAS les champs que le rendu lit :
    // +60 (dword_16693BC, bonus de niveau) et +5708 (dword_166A9CC, 2e palier de
    // renaissance) pilotent les DEUX cascades de tier à 4 paliers de l'écran Liste
    // (@0x51D55C/0x51D572) et du panneau de détail (@0x51D7FA/0x51D815), et les 11 champs du
    // panneau de détail (+16/+88/+92/+100/+5432/+5484/+5488/+5568/+5572/+9408) n'y sont pas
    // non plus. C'est EXACTEMENT la source du binaire, qui indexe ces mêmes globals à plat.
    // Renvoie 0 si `slot` est hors [0, net::kCharRecordCount).
    static int32_t CharRecI32(int32_t slot, int byteOffset);

    // UI_MeasureNumberText 0x53FCA0 + UI_DrawNumberValue 0x53FCC0 (police bitmap
    // unk_1685740) : `x = cx - largeur/2` (idiome `movzx/cdq/sub/sar 1` @0x51D73F-0x51D747).
    void DrawNumberCentered(const char* text, int centerX, int y);

    // GetPhysicalCursorPos(&pt) @0x51D493 puis ScreenToClient(hWndParent 0x815184, &pt)
    // @0x51D4A4 — DISTINCT de CursorClient() (GetCursorPos), utilisé par les autres scènes.
    // Le survol de CharSelect est recalculé PAR FRAME dans le rendu depuis cette position
    // physique live ; AUCUN index de survol n'est mis en cache dans Update.
    POINT CharSelectCursorClient() const;

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
    // [A4] Fond CharSelect : l'ancien membre `charBgSlot_`, tiré UNE FOIS dans Init(), a été
    // SUPPRIMÉ. Le tirage `Rng_Next()%3` -> 2383/2384/2385 est À L'INTÉRIEUR du bloc Init de
    // Scene_CharSelectUpdate (EA 0x51C23A `call Rng_Next` ; 0x51C261/70/7F les 3 écritures ;
    // immédiatement suivi de this[15714]=1 @0x51C28C et this[15715]=-1 @0x51C299) : il est
    // donc RE-TIRÉ À CHAQUE ENTRÉE en scène, pas une seule fois au boot. Ce tirage est
    // désormais porté par game::CharSelectState::backgroundSlot (RunInitBlock) ; LoginScene
    // le LIT (wiring TODO CHARBG_SLOT de Game/CharSelectFlow.h:480 — FERMÉ ici).
    // Tirer une 2e fois ici décalerait AUSSI le flux PRNG partagé (net::DefaultRng), dont
    // dépendent les nonces réseau : double défaut de fidélité.
    std::unordered_map<int, gfx::GpuTexture> atlasCache_; // slot -> texture (lazy)

    // --- Aperçu 3D CharSelect (Char_RenderModel 0x527020, 4 sites) ---
    // Ces membres sont PERSISTANTS (caches + device objects) : c'est précisément ce que les
    // TODO d'aperçu 3D des passes précédentes déclaraient « bloqué, exige des membres
    // possédés ». LoginScene.h appartenant à ce front, le blocage est levé.
    gfx::Renderer*   gfxRenderer_ = nullptr;   // fourni par Init() (cf. wiring TODO CHARSELECT_3D)
    gfx::MeshRenderer            charMesh_;    // decl 76 o + shaders skinnés
    std::unique_ptr<gfx::ModelCache> charModels_;  // C%03d%03d%03d.SOBJECT (ctor : MeshRenderer&)
    // ⚠ MÊME MotionCache pour la PALETTE DESSINÉE et pour le NOMBRE DE FRAMES rendu à
    // host.GetMotionFrameCount : dans le binaire c'est le MÊME g_ModelMotionArray 0x8E8B30
    // qui sert aux deux (PcModel_ResolveEquipSlot @0x52705F/0x527544 et
    // PcModel_ResolveSlotAndApply @0x51c555). Deux caches divergeraient sur les motions
    // absentes. Même discipline que Scene/WorldRenderer.cpp::Motions().
    std::unique_ptr<gfx::MotionCache> charMotions_;
    bool charPreviewReady_ = false;            // charMesh_.Init() a réussi ET renderer fourni

    // --- CharSelect (flux fidèle, cf. Game/CharSelectFlow.h) ---
    game::CharSelectState charState_;  // sous-état/écran/slots/formulaire/aperçu
    game::CharSelectHost  charHost_;   // callbacks réseau/UI (construits par BuildCharSelectHost)

    // Écran Liste : slots (clic = sélection), Créer/Supprimer/Entrer/Quitter.
    Button enterBtn_, backBtn_;        // enterBtn_ = ENTRER (16/17/18) ; backBtn_ = RETOUR (963/964/965)
    Button createBtn_, deleteBtn_;     // CRÉER (19/20/21) ; SUPPRIMER (22/23/24)
    Button restoreBtn_;                // slots 3086/3087/3088 @ (x0, y0-37), EA 0x51E34F
    // QUITTER (slots 25/26/27 @ (x0, y0+222), EA 0x51E2AA). Était hit-testé « à la main » sur
    // le rect du sprite dans CharSelectOnMouseUp, sans latch : le binaire, lui, le latche
    // comme les autres (`cmp dword ptr [ecx+24h], 0` = this[9] @0x51E288) et peint donc bien
    // son état PRESSÉ (slot 27). Widget dédié -> motif 3 états identique aux 9 autres.
    Button quitBtn_;

    // Écran Formulaire de création : 5 paires +/- (job/faction/visage/couleur/variant),
    // nom saisi (EditBox réutilisé de Widgets.h), Confirmer/Annuler.
    Button  jobMinusBtn_, jobPlusBtn_, factionMinusBtn_, factionPlusBtn_;
    Button  faceMinusBtn_, facePlusBtn_, hairMinusBtn_, hairPlusBtn_;
    Button  variantMinusBtn_, variantPlusBtn_;
    Button  createConfirmBtn_, createCancelBtn_;
    EditBox createNameBox_;

    // Boutons de ROTATION de l'aperçu 3D de création (slots 44/45 et 46/47, projetés par
    // UI_ProjectSpriteToScreen 0x50F5D0 aux mondes (390,628)/(557,628)). Latches this[15]
    // (+0x3C) / this[16] (+0x40) : COLLANTS — jamais remis à 0 pendant l'état Actif (aucun
    // clear dans Update/OnMouseDown/OnMouseUp), UNIQUEMENT par l'Init de scène (boucle
    // 150-latch @0x51BE83) = charHost_.ClearAllButtonLatches. Un clic gauche -> rotation
    // continue +3°/frame (@0x51CDE8) jusqu'à re-entrée en scène ; un clic droit ajoute -3°
    // (@0x51CE09) -> +3-3=0 = arrêt net. NE PAS modéliser via Button (armed_ s'auto-efface).
    bool rotLeftLatched_  = false; // this[15]
    bool rotRightLatched_ = false; // this[16]

    // Confirmation de suppression (Oui/Non), ouverte par host.ShowDeleteConfirm().
    bool   deleteConfirmOpen_ = false;
    Button deleteYesBtn_, deleteNoBtn_;

    // Confirmation Oui/Non de SORTIE du jeu (ServerSelect, bouton d'action slot 4). Mirroir
    // de UI_MsgBox_Open(dword_1822438, 1, StrTable005[1], ...) ouvert par
    // Scene_ServerSelectOnMouseUp 0x519B3E ; "Oui" -> Log "[ABNORMAL_END] ( 4 )" + g_QuitFlag=1
    // (UI_MsgBox_OnLButtonUp case 1, EA 0x5C0BEC-0x5C0BFB) ; "Non" referme (UI_ConfirmPrompt_Close).
    bool   exitConfirmOpen_ = false;
    Button exitConfirmYesBtn_, exitConfirmNoBtn_;

    // --- Notice ---
    bool        noticeOpen_ = false;
    std::string noticeText_;
    std::function<void()> noticeOnClose_; // action kind=2 (cf. OpenNotice) ; vide = no-op
    // [A3] TYPE de la notice CharSelect (2e argument de UI_NoticeDlg_Open 0x5C0280).
    // C'est LUI qui décide si le sous-état Verrouillé est un cul-de-sac (mode 1 = Close :
    // la scène reste où elle est) ou une SORTIE vers ServerSelect (mode 2 = Disconnect :
    // UI_NoticeDlg_OnLButtonUp 0x5C03F0 case 2 -> Net_CloseSocket 0x5C04DF, g_SceneMgr=2
    // 0x5C04E4, g_SceneSubState=0 0x5C04EE, dword_1676188=0 0x5C04F8).
    // Sans ce champ, Verrouillé était un CUL-DE-SAC DÉFINITIF (les 4 handlers de scène sont
    // gatés `==1` et ne voient jamais ce clic — il arrive par UI_RouteLButtonUp 0x5AD0F0,
    // xref unique EA 0x5AD164). Renseigné par charHost_.ShowNoticeTyped.
    game::NoticeType noticeType_ = game::NoticeType::Close;

    // --- Transition demandée ---
    ts2::Scene pending_ = ts2::Scene::None;
};

} // namespace ts2::ui

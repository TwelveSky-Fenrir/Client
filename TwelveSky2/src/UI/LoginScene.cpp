// UI/LoginScene.cpp — implémentation des scènes shell de connexion.
// Vérité = Docs/TS2_CLIENT_SHELL.md §2.1/§4 + désassemblage (idaTs2) :
//   Scene_LoginUpdate 0x51A8D0, Scene_LoginRender 0x51B020,
//   Scene_LoginOnMouseDown 0x51B5D0, Scene_LoginOnMouseUp 0x51B780,
//   UI_NoticeDlg_Open 0x5C0280, UI_NoticeDlg_OnLButtonUp 0x5C03F0 (mécanisme
//   de sortie du sous-état NoticeWait — fermeture de Docs/TS2_LOGINSCENE_AUDIT.md
//   §3.6, décompilé le 2026-07-14 : voir LoginSub::NoticeWait dans LoginScene.h).
//
// Les widgets (EditBox/Button) sont pilotés PAR LEUR INTERFACE PUBLIQUE :
//   - EditBox : SetMaxLength/SetPassword/SetText/Clear/SetFocused/Focused/Text/
//               OnMouseDown/OnChar/OnKey + callbacks SetOnSubmit/SetOnTab.
//   - Button  : SetLabel/Label + événements OnMouseDown/OnMouseUp/OnMouseMove et
//               callback SetOnClick (latch : armé au down, validé au up dedans).
// Le rendu est fait à plat (SpriteBatch pour les aplats + Font pour le texte) :
// on lit la géométrie des widgets via X()/Y()/W()/H() et leur état via
// Focused()/Hovered()/Pressed().
#include "UI/LoginScene.h"
#include "UI/UiProjection.h"       // ui::ProjectDesignAnchor (UI_ProjectSpriteToScreen 0x50F5D0)
#include "Config/GameOptions.h"    // ts2::config::Cfg_SaveLastServer (G02_GINFO\010.BIN, écriture seule)
#include "Net/Login.h"             // ConnectLoginServer / LoginRequest / ConnectGameServer
#include "Net/CharSelectPackets.h" // AccountKeepAlive/CreateCharacter/CharSlotAction/ReqEnterCharInfo/ReqCancelEnter
#include "Net/Rng.h"                // DefaultRng() — Rng_Next() % 360 pour spawnRotationDeg (cf. GameState.h)
#include "Net/GameServerDomains.h"  // SelectGameServerHost / g_ServerMode (Net_SelectServerDomain 0x53FE90)
#include "Game/GameState.h"        // game::g_World.zoneId (consommé par EnterWorldFlow)
#include "Game/StringTables.h"     // game::g_Strings.bannedWords (001.DAT, 1432 mots — filtre de creation)
#include "Game/ClientRuntime.h"    // game::Str(id) — texte reel StrTable005 pour les notices CharSelect
#include "Game/GameDatabase.h"     // game::GetItemInfo / WeaponClassFromTypeCode (motion d'entree 0x4CC870)
#include "Game/MiscManagers.h"     // game::Cursors() / kCursorDefault (reset curseur d'entree de scene, 0x4C1110)
#include "Asset/ImgFile.h"         // asset::ImgFile (chargeur .IMG, fond réel ServerSelect/Login)
#include "Gfx/Camera.h"            // gfx::Camera — projection applicative (Gfx_InitDevice 0x69BFC6)
#include "Core/Log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace ts2::ui {

namespace {

// Dimensions/ancrages fidèles au panneau login (unk_8E9368) et à ses offsets
// hardcodés (Scene_LoginRender 0x51B020) : champs à +126,+60 / +126,+95 ;
// bouton OK à +298,+126 ; bouton Quitter à +374,+126 ; case ombres à +21,+130.
// Les tailles du panneau/boutons sont NOMINALES (les vraies viennent des sprites
// de l'atlas UI, non chargés ici).
constexpr int kPanelW = 470, kPanelH = 210;
constexpr int kFieldW = 170, kFieldH = 22;
constexpr int kBtnW   = 72,  kBtnH   = 24;

// Palette (ARGB) — restreinte aux SEULES couleurs encore employées après le câblage 1:1
// du formulaire de création CharSelect sur ses vrais sprites d'atlas (2026-07-16, front
// W4-F1). Les anciens placeholders CharSelect (kColBackdrop/kColField/kColFieldFocus/
// kColSel/kColTitle/kColLabel/kColBtn + BtnColor) ont été RETIRÉS : le formulaire est câblé
// sur les vrais sprites (panneau slot 40 unk_8EA270 @0x51E4C5, +/- slots 41/42 en état
// PRESSÉ seul, Confirmer/Annuler slots 9/12, caret slot 43 g_SprTextInputCaret @0x51E53E)
// — cf. CreateFormRender(). kColText = couleur des VALEURS réelles que le binaire dessine
// (champs Login, niveaux de perso, valeurs du formulaire de création, titre du MsgBox).
//
// NOTE : les ex-couleurs de SUBSTITUTION kColPanel/kColPanelEdge ont été RETIRÉES avec les
// ex-DeleteConfirmRender/ExitConfirmRender — les confirmations Oui/Non sont désormais le
// MsgBox à sprites réels (UI/ConfirmMsgBox.h, dword_1822438, UI_MsgBox_* 0x5C08C0) : plus
// aucun aplat inventé.
constexpr D3DCOLOR kColText      = 0xFFE8ECF4; // texte principal (VALEURS réelles + titre MsgBox)

inline RECT MakeRect(int x, int y, int w, int h) {
    RECT r; r.left = x; r.top = y; r.right = x + w; r.bottom = y + h; return r;
}

// Test point-dans-rectangle (le binaire n'exposait pas ce helper ; on le fournit
// localement pour le hit-test des lignes serveur/slot perso).
inline bool RectContains(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// [A14] Les anciens kCharPreviewEye/kCharPreviewTarget ont été SUPPRIMÉS : ils dupliquaient
// une vérité qui vit déjà, prouvée à l'octet, dans gfx::CharPreview3D::CameraEye()/
// CameraTarget()/CameraUp() (Gfx/CharPreview3D.h §5 — flt_7EDA1C=5.0f, flt_7A9764=-28.0f,
// flt_7A8D74=10.0f, up=(0,1,0) @0x6A233A). Ils n'avaient plus aucun lecteur depuis que
// CharSelectRenderPreview3D() passe par CharPreview3D::BuildViewMatrix().
// Le binaire écrit ces 6 floats à l'Init de la scène sur les DEUX singletons renderer
// (g_GfxRenderer 0x800130..0x800144 @0x51BDED-0x51BE21, recopiés dans g_GxdRenderer
// 0x18C51C0..0x18C51D4 @0x51BE2C-0x51BE65) ; côté C++ ce sont des constantes consommées au
// point de rendu, donc l'écriture « à chaque Init » est sans effet observable.

} // namespace

// ===========================================================================
// Cycle de vie
// ===========================================================================
LoginScene::~LoginScene() { Shutdown(); }

bool LoginScene::Init(IDirect3DDevice9* device, net::NetSystem* net, HWND notifyWnd,
                      int screenW, int screenH, int serverModeFlag,
                      gfx::Renderer* renderer) {
    device_        = device;
    net_           = net;
    notifyWnd_     = notifyWnd;
    screenW_       = screenW;
    screenH_       = screenH;
    serverModeFlag_ = serverModeFlag;           // dword_166918C : consommé par BuildServerList()
    net::g_ServerMode = serverModeFlag;         // miroir de g_ServerModeFlag 0x166918C pour
                                                // Net_SelectServerDomain 0x53FE90 (connect game-server)
    gfxRenderer_   = renderer;                  // aperçu 3D CharSelect (cf. LoginScene.h::Init)
    if (!device_) { TS2_ERR("LoginScene::Init : device nul"); return false; }

    // Police GXD (Font_AddTtfResource 0x4C0E70 puis D3DXCreateFontIndirect).
    gfx::Font::AddTtfResource(false);
    if (!font_.Init(device_, screenW_, screenH_))
        TS2_WARN("LoginScene : police indisponible (texte non rendu).");
    if (!sprites_.Create(device_))
        TS2_WARN("LoginScene : sprite batch indisponible (aplats non rendus).");
    gfx::SetActiveSprite(&sprites_);
    CreateWhiteTexture();

    // Champs de saisie (EM_LIMITTEXT 0x7F ; PW masqué par '*').
    idBox_.SetMaxLength(0x7F); idBox_.SetPassword(false); idBox_.SetTextColor(kColText);
    pwBox_.SetMaxLength(0x7F); pwBox_.SetPassword(true);  pwBox_.SetTextColor(kColText);

    // Libellés des boutons — FABRICATION ASSUMÉE, PAS extraite de StrTable005 (re-vérifié
    // par décompilation directe de Scene_CharSelectRender 0x51CED0, session 2026-07-14,
    // audit UI/flux CharSelect : sur les 40 appels StrTable005_Get de cette fonction,
    // AUCUN ne sert un libellé de bouton d'action — tous les 40 sont pour le TEXTE DE
    // VALEUR du formulaire de création, dynamique selon job/faction/visage, cf.
    // CharSelectFlow.h::CharCreateForm). Aucune chaîne française "Créer"/"Supprimer"/
    // "Entrer"/"Confirmer"/"Annuler"/"Oui"/"Non"/"Quitter" n'existe non plus en dur dans
    // l'exécutable. Le vrai écran Liste hit-teste ses boutons via Sprite2D_HitTest sur
    // des rects d'atlas (Scene_CharSelectOnMouseUp 0x522E50, refs Sprite2D_GetWidth/
    // GetHeight/HitTest sur unk_94D7B4/unk_95B364/unk_95B3F8) — CAPTIONS GRAPHIQUES
    // (texture d'atlas UI), PAS de texte dessiné à l'écran pour ces boutons dans le
    // binaire. Il n'existe donc PAS d'id StrTable005 "correct" à brancher ici : ce ne
    // sont ni des textes fidèles ni des ids erronés, mais un texte de repli TEMPORAIRE
    // tant que l'atlas de sprites boutons n'est pas chargé (TODO rendu, cf. commentaire
    // de tête de fichier et LayoutCreateForm/LayoutCharSelect ci-dessous).
    okBtn_.SetLabel("OK");        exitBtn_.SetLabel("Quitter");  optBtn_.SetLabel("Ombres");
    enterBtn_.SetLabel("Entrer"); backBtn_.SetLabel("Retour");   // backBtn_ = slots 963/964/965
                                                                  // (unk_90B80C) = bouton RETOUR ->
                                                                  // ServerSelect, CONFIRMÉ par IDA
                                                                  // cette session (Scene_CharSelect
                                                                  // OnMouseUp EA 0x525A1A-0x525A71 :
                                                                  // Net_CloseSocket + scene=2). Le
                                                                  // vrai QUITTER est le slot 25
                                                                  // (unk_8E99C4, EA 0x525AA5), hit-
                                                                  // testé séparément dans
                                                                  // CharSelectOnMouseUp. Le TEXTE
                                                                  // reste un placeholder (bouton
                                                                  // skinné, label non dessiné).
    createBtn_.SetLabel("Creer");  deleteBtn_.SetLabel("Supprimer");
    restoreBtn_.SetLabel("Restaurer"); // texte placeholder ; sprite réel slots 3086/3087/3088 (0x51E354)
    createConfirmBtn_.SetLabel("Confirmer"); createCancelBtn_.SetLabel("Annuler");
    // (Les confirmations Oui/Non delete/exit sont désormais le MsgBox à sprites msgBox_, pas des Button.)
    jobMinusBtn_.SetLabel("-");    jobPlusBtn_.SetLabel("+");
    factionMinusBtn_.SetLabel("-"); factionPlusBtn_.SetLabel("+");
    faceMinusBtn_.SetLabel("-");   facePlusBtn_.SetLabel("+");
    hairMinusBtn_.SetLabel("-");   hairPlusBtn_.SetLabel("+");
    variantMinusBtn_.SetLabel("-"); variantPlusBtn_.SetLabel("+");
    createNameBox_.SetMaxLength(12); createNameBox_.SetTextColor(kColText);

    // Actions des boutons login (latch validé au relâchement dans le bouton).
    okBtn_.SetOnClick([this] { loginSub_ = LoginSub::Trigger; });               // -> DoLogin
    exitBtn_.SetOnClick([this] {                                                 // -> ServerSelect
        pending_  = ts2::Scene::ServerSelect;
        loginSub_ = LoginSub::Init;
    });
    optBtn_.SetOnClick([this] { shadowsEnabled_ = !shadowsEnabled_; });          // toggle 0x84DEF8

    // Navigation clavier des champs : Tab -> champ suivant, Entrée -> soumission.
    idBox_.SetOnTab   ([this] { SetFocus(2); });                 // ID -> PW
    idBox_.SetOnSubmit([this] { SetFocus(2); });                 // Entrée sur ID -> PW
    pwBox_.SetOnTab   ([this] { SetFocus(1); });                 // PW -> ID (cycle)
    pwBox_.SetOnSubmit([this] { loginSub_ = LoginSub::Trigger; }); // Entrée sur PW -> login

    // Boutons CharSelect : down-arme/up-valide comme les boutons Login (Button::SetOnClick).
    enterBtn_.SetOnClick([this] { game::OnEnterButtonClicked(charState_, charHost_); });
    // RETOUR (unk_90B80C = slots 963/964/965, colonne @v117+185) : Scene_CharSelectOnMouseUp
    // EA 0x525A1A-0x525A71 (re-décompilé cette session, front W4-F1) — l'action RÉELLE de ce
    // bouton n'est PAS "Quitter" mais un RETOUR à ServerSelect : garde `this[+0xF598]==3`
    // (aperçu en cours d'entrée -> ignore, EA 0x525A33), sinon Net_CloseSocket(&g_NetClient)
    // (0x463000, EA 0x525A46) puis this[0]=2 (scene=ServerSelect, EA 0x525A51), this[1]=0
    // (sous-état Init, EA 0x525A5D), this[2]=0 (compteur de frames, EA 0x525A6A). L'ancien
    // câblage sur OnQuitButtonClicked était FAUX (le Quit réel est le slot 25, cf.
    // CharSelectOnMouseUp).
    // [A5] Délégué à game::RequestBackToServerSelect, qui porte DÉJÀ la séquence exacte
    // (garde Entering EA 0x525A33 ; host.CloseConnection = Net_CloseSocket 0x463000 EA
    // 0x525A46 ; pendingTransition=ServerSelect EA 0x525A51 ; subState=Init EA 0x525A5D ;
    // frameCounter=0 EA 0x525A6A) — l'ancien code la RÉPLIQUAIT à la main ici, en posant
    // `pending_` DIRECTEMENT au lieu de passer par pendingTransition : la transition
    // court-circuitait UpdateCharSelect (et donc sa consommation en tête de tick), et la
    // garde `subState == Active` de RequestBackToServerSelect n'était pas appliquée.
    // La transition est désormais consommée par CharSelectUpdate (branche ServerSelect).
    backBtn_.SetOnClick([this] { game::RequestBackToServerSelect(charState_, charHost_); });
    createBtn_.SetOnClick([this] { game::OnCreateButtonClicked(charState_, charHost_); });
    deleteBtn_.SetOnClick([this] { game::OnDeleteButtonClicked(charState_, charHost_); });
    // QUITTER (slots 25/26/27 @ (x0, y0+222)) : garde `this[+0xF598]==3` EA 0x525ABE puis
    // Net_CloseSocket + g_QuitFlag=1 — portés par game::OnQuitButtonClicked.
    quitBtn_.SetOnClick([this] { game::OnQuitButtonClicked(charState_, charHost_); });
    createConfirmBtn_.SetOnClick([this] { game::ConfirmCreateCharacter(charState_, charHost_); });
    createCancelBtn_.SetOnClick([this]  { game::CancelCreateForm(charState_); });
    jobMinusBtn_.SetOnClick([this]     { game::SetCreateJob(charState_, -1); });
    jobPlusBtn_.SetOnClick([this]      { game::SetCreateJob(charState_, +1); });
    factionMinusBtn_.SetOnClick([this] { game::SetCreateFaction(charState_, -1); });
    factionPlusBtn_.SetOnClick([this]  { game::SetCreateFaction(charState_, +1); });
    faceMinusBtn_.SetOnClick([this]    { game::SetCreateFace(charState_, -1); });
    facePlusBtn_.SetOnClick([this]     { game::SetCreateFace(charState_, +1); });
    hairMinusBtn_.SetOnClick([this]    { game::SetCreateHairColor(charState_, -1); });
    hairPlusBtn_.SetOnClick([this]     { game::SetCreateHairColor(charState_, +1); });
    variantMinusBtn_.SetOnClick([this] { game::SetCreateVariant(charState_, -1); });
    variantPlusBtn_.SetOnClick([this]  { game::SetCreateVariant(charState_, +1); });
    // Confirmations Oui/Non (suppression + sortie) : désormais portées par msgBox_ (ConfirmMsgBox),
    // dont l'action OK est fournie à l'ouverture (charHost_.ShowDeleteConfirm pour delete type 2 ;
    // ServerSelectOnMouseUp pour l'exit type 1). Plus de SetOnClick de boutons Yes/No ici.

    // Textures réelles des boutons "Confirm"/"Cancel" (Docs/TS2_LOGIN_BUTTON_ASSETS.md §4) :
    // OK/Quitter du Login — même paire de sprites génériques. Repli sur le rect coloré
    // (ApplyConfirmCancelSkin) si les .IMG sont absents. Les Oui/Non delete/exit ne sont plus
    // des Button (le MsgBox à sprites msgBox_ dessine slots 8-13 directement).
    ApplyConfirmCancelSkin(okBtn_, exitBtn_);

    // CharSelect écran Liste : chaque bouton de la colonne principale a ses PROPRES sprites
    // idle/hover/pressé dédiés dans l'atlas 001 (Docs/TS2_CHARSELECT_RE.md §4/§7, slots
    // confirmés EA par EA) — PAS la paire générique Confirm/Cancel. SetNormal = état idle
    // (ces boutons ONT un sprite idle distinct, contrairement aux boutons Login qui ne
    // possèdent que survol/pressé). restoreBtn_/enterBtn_/createBtn_/deleteBtn_/backBtn_ sont
    // LATCHÉS (hit-testés dans CharSelectOnMouseDown/Up) ; RENOMMER/ENTREPÔT/QUITTER, sans
    // membre Button ni hit-test, sont dessinés en sprites directs idle dans CharListRender().
    {
        auto skinColBtn = [this](Button& b, int idle, int hover, int pressed) {
            if (gfx::GpuTexture* t = GetAtlasSprite(idle))    b.SetNormal(WidgetSprite(t->Handle()));
            if (gfx::GpuTexture* t = GetAtlasSprite(hover))   b.SetHover(WidgetSprite(t->Handle()));
            if (gfx::GpuTexture* t = GetAtlasSprite(pressed)) b.SetPressed(WidgetSprite(t->Handle()));
        };
        skinColBtn(enterBtn_,   16,  17,  18); // ENTRER
        skinColBtn(createBtn_,  19,  20,  21); // CRÉER
        skinColBtn(deleteBtn_,  22,  23,  24); // SUPPRIMER
        skinColBtn(backBtn_,   963, 964, 965); // RETOUR
        skinColBtn(restoreBtn_, 3086, 3087, 3088); // RESTAURER @ y=v117-37, EA 0x51E354/0x525B46
    }

    // Formulaire de création (écran CreateForm) : boutons -/+ = slot 41 unk_8EA304 ("-") et
    // slot 42 unk_8EA398 ("+"), Scene_CharSelectRender 0x51CED0.
    // 🔴 CORRECTION DE FIDÉLITÉ (re-désassemblée EA par EA ce front) : ces sprites sont un
    // SEUL état = l'état PRESSÉ, dessiné UNIQUEMENT si le latch this[3..12] est armé. Le
    // binaire n'a QU'UN site de blit par bouton, et il est GARDÉ par le latch :
    //   `mov ecx,[ebp+var_470]` @0x51E983 ; `cmp dword ptr [ecx+0Ch],0` @0x51E989 ;
    //   `jz short loc_51E9AA` @0x51E98D  -> latch NUL = ON SAUTE LE DESSIN.
    //   Si armé seulement : `add edx,4Eh` (y=panelY+78) @0x51E995 ; `add eax,73h` (x=panelX+115)
    //   @0x51E99C ; `mov ecx, offset unk_8EA304` @0x51E9A0 ; `call Sprite2D_Draw` @0x51E9A5.
    // Motif STRICTEMENT identique pour le « + » (latch +0x10 @0x51E9B0, unk_8EA398 @0x51E9CE)
    // et pour les 8 autres (latches +0x14 @0x51E9D9 … +0x30 @0x51EAF9). AUCUNE branche survol,
    // AUCUNE branche normale. Les glyphes « - »/« + » AU REPOS sont donc peints DANS LA
    // TEXTURE DU PANNEAU (slot 40) ; les slots 41/42 ne sont que des SURIMPRESSIONS d'état
    // pressé. SetNormal les aurait dessinés EN PERMANENCE (Button::DrawSkin blitte `normal_`
    // sans condition) = 10 sprites parasites dès la 1re frame. Avec SetPressed seul :
    // non armé -> skin=&normal_ invalide -> `if (skin->Valid())` faux + fallbackTex_ nul ->
    // RIEN dessiné ; armé -> slot 41/42 blitté. Exactement le binaire.
    // Confirmer/Annuler sont, EUX, bien tri-état (hit-test unk_8E9084 @0x51EB4F, survol
    // @0x51EB70, pressé unk_8E9118 @0x51EB90) -> paire générique ApplyConfirmCancelSkin.
    {
        Button* minusBtns[] = { &jobMinusBtn_, &factionMinusBtn_, &faceMinusBtn_, &hairMinusBtn_, &variantMinusBtn_ };
        Button* plusBtns[]  = { &jobPlusBtn_,  &factionPlusBtn_,  &facePlusBtn_,  &hairPlusBtn_,  &variantPlusBtn_ };
        // Surimpression PRESSÉE seule — garde latch EA 0x51E989 (`cmp [ecx+0Ch],0 ; jz`).
        if (gfx::GpuTexture* t = GetAtlasSprite(41)) for (Button* b : minusBtns) b->SetPressed(WidgetSprite(t->Handle()));
        // Idem « + » — garde latch EA 0x51E9B0 (`cmp [ecx+10h],0 ; jz`).
        if (gfx::GpuTexture* t = GetAtlasSprite(42)) for (Button* b : plusBtns)  b->SetPressed(WidgetSprite(t->Handle()));
    }
    ApplyConfirmCancelSkin(createConfirmBtn_, createCancelBtn_);

    BuildCharSelectHost();

    // Callbacks réseau/persistance du flux ServerSelect (game::ServerSelectHost) :
    //  - QueryServerStatus : branché sur la couche réseau RÉELLE (ts2::net::QueryServerStatusLive,
    //    contrat ss-netconnect = Net_QueryServerStatus 0x519CC0 — connect TCP + recv 17 o).
    //    Traduit LiveServerStatus -> game::ServerStatus (mêmes 3 champs : maxPop/loadStep/curPop).
    //  - SaveLastServer : Cfg_SaveLastServer 0x519C40 (persistance G02_GINFO\010.BIN, index serveur).
    serverHost_.QueryServerStatus = [](const std::string& name, uint16_t port) {
        const ts2::net::LiveServerStatus r = ts2::net::QueryServerStatusLive(name, port);
        game::ServerStatus s;
        s.maxPopulation     = r.maxPopulation;
        s.loadStep          = r.loadStep;
        s.currentPopulation = r.currentPopulation;
        return s;
    };
    serverHost_.SaveLastServer = [](int32_t /*groupIndex*/, int32_t serverIndex) {
        ts2::config::Cfg_SaveLastServer(serverIndex);
    };

    BuildServerList();
    ResetLoginFields();
    loginSub_ = LoginSub::Init;

    // Fond ServerSelect/Login (Scene_ServerSelectUpdate 0x518B30, EA 0x518C29-0x518C40) :
    // this[168] = 2380 ou 2381, Rng_Next()%2 (50/50). Tiré une fois ici (cf. note de
    // LoginScene.h::bgAtlasSlot_ pour la simplification par rapport à l'original). Reporté
    // dans serverState_.backgroundImageId pour que ServerSelectRender lise le MÊME tirage
    // (mémoire de scène partagée ServerSelect<->Login, cf. GetAtlasSprite/DrawFullscreenBg).
    // FLUX RNG UNIQUE (W5b) : le binaire n'a qu'UN état _holdrand (Rng_Next 0x7603FD,
    // 678 xrefs code : builders Net_Send*, FX Snow_/Rain_, HUD, et ces tirages de fond).
    // On tape donc sur net::DefaultRng() et NON sur std::rand(), qui constituait ici un
    // SECOND flux indépendant sans contrepartie binaire.
    // Ancre : Scene_ServerSelectUpdate 0x518B30, `call Rng_Next` EA 0x518C19 puis
    // `and eax, 80000001h` (idiome %2 signé) ; `mov [eax+2A0h], 94Ch` EA 0x518C31 (=2380,
    // cas 0) / `mov [ecx+2A0h], 94Dh` EA 0x518C40 (=2381, cas != 0).
    bgAtlasSlot_ = (net::DefaultRng().NextMod(2)) ? 2381 : 2380;
    serverState_.backgroundImageId = bgAtlasSlot_;
    // [A4] Le tirage du fond CharSelect (this[15713] = 2383/2384/2385) N'EST PLUS FAIT ICI.
    // Il vit dans le bloc Init de Scene_CharSelectUpdate 0x51BD90 (`call Rng_Next` EA
    // 0x51C23A, `%3` EA 0x51C245, écritures 0x51C261/0x51C270/0x51C27F) et est donc RE-TIRÉ
    // à CHAQUE entrée en scène 4 — pas une fois au boot. Porté par game::UpdateCharSelect
    // (RunInitBlock) dans charState_.backgroundSlot, que le rendu LIT. Cf. LoginScene.h.

    // --- Aperçu 3D CharSelect (Char_RenderModel 0x527020) : ressources persistantes ---
    // gfx::MeshRenderer::Init exige un gfx::Renderer& (dont il ne lit que Device()) ; sans
    // lui, charPreviewReady_ reste false et CharSelectRenderPreview3D() ne dessine RIEN —
    // ce qui est le comportement SÛR (le binaire, lui, dessine : cf. wiring TODO CHARSELECT_3D).
    if (gfxRenderer_ && charMesh_.Init(*gfxRenderer_)) {
        // gameDataDir="." : le CWD du process est déjà basculé sur gameDataDir par
        // App::ResolveGameDataDir() (App/App.cpp) dès App::Init, donc bien avant la scène 4.
        // MÊME convention et MÊME raison que Scene/WorldRenderer.cpp (Models()/Motions()).
        charModels_  = std::make_unique<gfx::ModelCache>(charMesh_, ".");
        charMotions_ = std::make_unique<gfx::MotionCache>(".");
        // D3DLIGHT9 0x18C5358 -> SetLight(0,&light) @0x51D226 : diffuse/ambient 0.8 et
        // direction normalize(-1,-1,-1). Poussé UNE FOIS (les valeurs sont des littéraux du
        // binaire, réécrits à l'identique à chaque frame par Scene_CharSelectRender).
        gfx::CharPreview3D::ApplyLight(charMesh_);
        charPreviewReady_ = true;
    } else if (!gfxRenderer_) {
        TS2_WARN("LoginScene : aucun gfx::Renderer fourni -> apercu 3D CharSelect DESACTIVE "
                 "(cf. wiring TODO CHARSELECT_3D, Scene/SceneManager.cpp:337).");
    } else {
        TS2_WARN("LoginScene : MeshRenderer::Init a echoue -> apercu 3D CharSelect desactive.");
    }

    // Câblage des sprites RÉELS de l'écran ServerSelect (panneau/barre de charge/bouton
    // d'action, cf. UI/ServerSelectRender.h) : nécessite le device D3D9, indisponible
    // avant ce point (device_ vient d'être assigné en tête de cette fonction).
    serverSelectRender_.SetDevice(device_);
    // Idem pour le logo Intro réel (cf. UI/IntroRender.h::SetDevice — bug de fidélité
    // corrigé le 2026-07-14 : ctx.renderer n'est jamais peuplé par RenderIntro(), sans ce
    // SetDevice() le vrai logo n'est JAMAIS affiché, seulement le repli "Logo #NNN").
    introRender_.SetDevice(device_);
    // Idem pour l'écran de transition EnterWorld réel (cf. UI/EnterWorldRender.h) : sans
    // ce SetDevice(), le fond de zone (008_%05d.IMG) et la barre de progression (atlas
    // 001, slots 1140..1160) ne peuvent jamais charger de texture réelle et Render()
    // replie systématiquement sur les aplats colorés diagnostiques.
    enterWorldRender_.SetDevice(device_);
    return true;
}

void LoginScene::Shutdown() {
    // Fidélité/lifetime : Net_ServerStatusThread 0x518AB0 est détaché dans le binaire ;
    // ici on JOINT le worker avant de libérer quoi que ce soit (il écrit serverState_ sous
    // serverMutex_) — écart de fidélité assumé (join borné par le timeout de
    // QueryServerStatusLive) pour éviter tout accès après libération de LoginScene.
    if (statusThread_.joinable()) statusThread_.join();
    // W5b — lifetime : g_LoginNoticeHook est une GLOBALE (Net/Login.cpp:30) qui capture
    // `this`. LoginScene est détenue par un unique_ptr membre de SceneManager
    // (SceneManager.h) : la scène meurt AVANT le global, contrairement à charHost_.ShowNotice
    // (membre, qui meurt avec la scène). On dépose donc le hook ici pour ne jamais laisser
    // un `this` pendouillant — même discipline que le join ci-dessus.
    net::g_LoginNoticeHook = nullptr;
    if (gfx::ActiveSprite() == &sprites_) gfx::SetActiveSprite(nullptr);
    if (whiteTex_) { whiteTex_->Release(); whiteTex_ = nullptr; }
    atlasCache_.clear(); // libère les GpuTexture avant que le device ne soit détruit
    // Aperçu 3D CharSelect : même discipline que atlasCache_ — les VB/IB/textures des
    // SkinnedModel du cache, puis la déclaration de sommets et les shaders du MeshRenderer,
    // doivent être relâchés AVANT la destruction du device (possédé par SceneManager, qui
    // détruit LoginScene en premier). ORDRE : modèles (référencent le device) -> motions
    // (données 100 % CPU, aucun objet device) -> MeshRenderer.
    charModels_.reset();
    charMotions_.reset();
    charMesh_.Shutdown();
    charPreviewReady_ = false;
    gfxRenderer_      = nullptr;
    // ⚠ AUCUN hook OnDeviceLost/OnDeviceReset n'est requis pour ces ressources : MeshRenderer
    // crée TOUS ses VB/IB/textures en D3DPOOL_MANAGED (Gfx/MeshRenderer.cpp:369/381/451), qui
    // survit à un Reset sans ré-upload — c'est exactement pourquoi Scene/WorldRenderer.cpp
    // n'y touche pas non plus dans ses propres OnDeviceLost/OnDeviceReset (il n'y traite que
    // sa police). font_ / sprites_ se libèrent via leurs destructeurs.
}

void LoginScene::OnDeviceLost()  { font_.OnDeviceLost();  sprites_.OnLostDevice();  }
void LoginScene::OnDeviceReset() { font_.OnDeviceReset(); sprites_.OnResetDevice(); }

void LoginScene::CreateWhiteTexture() {
    if (!device_) return;
    // Texture 1x1 blanche, pool MANAGED (survit à un reset de device). Modulée par la
    // couleur de Draw, c'est la primitive d'aplat (FillRect). Après la purge zéro-fallback
    // du 2026-07-15, les écrans Init / ServerSelect / Login (Credential) n'utilisent PLUS
    // AUCUN FillRect (tout est câblé sur les vrais sprites de l'atlas). FillRect ne subsiste
    // que pour : (1) le placeholder CharSelect (scène 4, câblage 1:1 reporté), (2) le panneau
    // de la notice modale (RenderNotice). whiteTex_ reste donc nécessaire pour ces deux cas.
    if (FAILED(device_->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                      D3DPOOL_MANAGED, &whiteTex_, nullptr))) {
        whiteTex_ = nullptr;
        return;
    }
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(whiteTex_->LockRect(0, &lr, nullptr, 0))) {
        *reinterpret_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
        whiteTex_->UnlockRect(0);
    }
}

// Aplat coloré : un blit de la texture 1x1 mis à l'échelle w×h à (x,y). On passe
// compensatePos=true (comme UI_DrawSpriteScaledAlpha 0x457D70) pour que la matrice
// d'échelle place le coin haut-gauche exactement en (x,y). À appeler entre
// sprites_.Begin() et sprites_.End().
void LoginScene::FillRect(int x, int y, int w, int h, D3DCOLOR color) {
    if (!whiteTex_ || !sprites_.Ready() || w <= 0 || h <= 0) return;
    sprites_.DrawSpriteScaled(whiteTex_, nullptr, x, y,
                              static_cast<float>(w), static_cast<float>(h),
                              color, /*compensatePos=*/true);
}

POINT LoginScene::CursorClient() const {
    POINT p{0, 0};
    if (GetCursorPos(&p) && notifyWnd_) ScreenToClient(notifyWnd_, &p);
    return p;
}

// Résout un slot de l'atlas partagé unk_8E8B50 (AssetMgr_InitAllSlots 0x4deb50, boucle
// catégorie 1 -> "G03_GDATA\D01_GIMAGE2D\001\") vers son GpuTexture, avec cache paresseux.
// DÉCALAGE CONFIRMÉ par décompilation directe (idaTs2, session du 2026-07-14) :
// Sprite2D_BuildPath 0x4d68e0 formate le nom de fichier avec `slot+1` (vsnprintf
// "...%05d.IMG", a3+1) — le fichier réel N'EST PAS 001_<slot>.IMG mais 001_<slot+1>.IMG.
// Vérifié sur les assets décompressés : la séquence de logos Intro (subState 1..33,
// slots 798..830) correspond EXACTEMENT aux 33 fichiers 001_00799..001_00831.IMG (tous
// 668x229 DXT1) ; les boutons ServerSelect (slot ButtonImageId(i), ex. 1786) pointent vers
// un asset SANS RAPPORT (001_01786.IMG = panneau 737x755) alors que slot+1 (001_01787.IMG)
// est bien un bouton 153x23 DXT3 cohérent.
gfx::GpuTexture* LoginScene::GetAtlasSprite(int slotIndex) {
    if (!device_) return nullptr;
    auto it = atlasCache_.find(slotIndex);
    if (it != atlasCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG", slotIndex + 1);
    asset::ImgFile img;
    if (img.Load(path))
        tex.CreateFromImgFile(device_, img);
    else
        TS2_WARN("LoginScene : atlas slot %d illisible (%s) — rien dessine (zero repli).", slotIndex, path);

    auto res = atlasCache_.emplace(slotIndex, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

// Fond plein écran ServerSelect/Login (Sprite2D_DrawScaled(unk_8E8B50 + 148*this[168], 0,
// 0, nWidth/1024, nHeight/768) — EA 0x519461 / 0x51B207, IDENTIQUE dans les deux scènes).
// Texture réelle mise à l'échelle de l'écran. AUCUN repli (2026-07-15) : si le sprite ne
// charge pas, on ne dessine RIEN — fidèle à Sprite2D_DrawScaled/EnsureLoaded qui échoue en
// silence (Docs/TS2_SHELL_RENDER_TRUTH.md §2 : « Jamais de FillRect »).
// [A7/B1] CORRECTION DE FIDÉLITÉ (2026-07-16) : les diviseurs sont les CONSTANTES LITTÉRALES
// 1024.0f / 768.0f, PAS les dimensions de la texture. Le binaire NE LIT JAMAIS la taille du
// sprite ici — l'ancien code (`screenW_/tex->Width()`) contredisait son propre commentaire et
// produisait une mise à l'échelle fausse dès qu'un fond n'était pas exactement 1024x768.
// PREUVE COMPLÈTE (désassemblage direct, re-vérifié ce front) :
//   flt_1669178 / flt_166917C : `data_refs` = 4 réfs = 1 SEULE écriture + 3 lectures.
//     écriture UNIQUE : WinMain 0x4609C0 -> `fld ds:flt_7A68C8 ; fstp ds:flt_1669178` @0x4609D3/
//     0x4609D9 puis `fld ds:flt_7A68C4 ; fstp ds:flt_166917C` @0x4609DF/0x4609E5.
//     lectures : Scene_ServerSelectRender @0x51942F, Scene_LoginRender @0x51B1D5,
//                Scene_CharSelectRender @0x51D279 — toutes des `fdiv`.
//   valeurs vérifiées À L'OCTET (get_bytes) : flt_7A68C8 = 00 00 80 44 = 1024.0f ;
//                                             flt_7A68C4 = 00 00 40 44 =  768.0f.
// Ce ne sont donc PAS des variables de résolution : ce sont les dimensions de RÉFÉRENCE du
// design, figées au démarrage et jamais réécrites.
//
// Ordre des arguments de Sprite2D_DrawScaled 0x4D6BF0 (4 args + this), pushs @0x51D257-0x51D2AB :
//   fild nHeight ; fdiv flt_166917C ; ... push ecx ; fstp [esp]   <- scaleY (poussé en 1er)
//   fild nWidth  ; fdiv flt_1669178 ; ... push ecx ; fstp [esp]   <- scaleX
//   push 0                                                        <- y
//   push 0                                                        <- x
//   mov ecx, atlas + 148*this[15713] ; call Sprite2D_DrawScaled
// => Sprite2D_DrawScaled(spr, x=0, y=0, scaleX = nWidth/1024.0f, scaleY = nHeight/768.0f).
// L'idiome `push ecx ; fstp [esp]` (réservation de slot + écriture FPU) est ce qui fait
// croire à 3 dwords empilés : il y en a bien 4.
//
// nWidth = 0x1669184, nHeight = 0x1669188 (= screenW_/screenH_ côté C++).
// AUCUN repli : si le sprite ne charge pas, on ne dessine RIEN — fidèle à
// Sprite2D_DrawScaled/EnsureLoaded qui échoue en silence.
void LoginScene::DrawFullscreenBg(int slotIndex) {
    constexpr float kDesignW = 1024.0f; // flt_7A68C8 -> flt_1669178 (WinMain @0x4609D9)
    constexpr float kDesignH = 768.0f;  // flt_7A68C4 -> flt_166917C (WinMain @0x4609E5)
    gfx::GpuTexture* tex = GetAtlasSprite(slotIndex);
    if (tex && tex->Handle() && sprites_.Ready()) {
        const float sx = static_cast<float>(screenW_) / kDesignW;
        const float sy = static_cast<float>(screenH_) / kDesignH;
        sprites_.DrawSpriteScaled(tex->Handle(), nullptr, 0, 0, sx, sy,
                                  gfx::kSpriteWhite, /*compensatePos=*/true);
    }
}

// Slots 9/10 = "Confirm" survol/pressé (001_00010/00011.IMG), 12/13 = "Cancel"
// survol/pressé (001_00013/00014.IMG) — cf. Docs/TS2_LOGIN_BUTTON_ASSETS.md §4. Ces 4
// fichiers sont confirmés présents sur disque (doc §4 « Vérifié sur disque »).
//
// SUPPRIMÉ le 2026-07-15 (demande utilisateur : ZÉRO fallback) : plus de SetFallbackTexture/
// SetFallbackColors — si un .IMG bouton manque au runtime, Button::DrawSkin ne dessine RIEN
// (au lieu d'un aplat coloré inventé). On ne câble que les états survol/pressé RÉELS.
//
// CORRIGÉ (audit Login du 2026-07-14, Docs/TS2_LOGINSCENE_AUDIT.md §4, re-décompilation
// intégrale de Scene_LoginRender 0x51B020) : le slot 9/12 est câblé en SURVOL (SetHover),
// PAS en état normal (SetNormal) — preuve EA par EA :
//   if ( *(this+3) )                                                   Sprite2D_Draw(pressé)
//   else if ( Sprite2D_HitTest(unk_8E9084,...) && *(this+1)==1 )       Sprite2D_Draw(unk_8E9084)
//   // sinon (ni pressé, ni survolé) : RIEN n'est dessiné pour ce bouton.
// Le binaire ne dessine donc JAMAIS unk_8E9084/unk_8E9240 en état idle (souris ailleurs) —
// seuls le survol et l'appui sont visibles ; le libellé "OK"/"Quitter" permanent, s'il
// existe, est peint dans le panneau (unk_8E9368) lui-même, pas ajouté par cette fonction.
// L'ancien code câblait ce sprite en SetNormal(), ce qui le rendait affiché EN PERMANENCE
// (Button::DrawSkin retombe sur normal_ dès que hover_/pressed_ sont absents) — un aplat
// bouton visible en trop, non présent dans l'écran d'origine hors survol/clic. Corrigé ici.
void LoginScene::ApplyConfirmCancelSkin(Button& confirmBtn, Button& cancelBtn) {
    if (gfx::GpuTexture* t = GetAtlasSprite(9))  confirmBtn.SetHover(WidgetSprite(t->Handle()));
    if (gfx::GpuTexture* t = GetAtlasSprite(10)) confirmBtn.SetPressed(WidgetSprite(t->Handle()));
    if (gfx::GpuTexture* t = GetAtlasSprite(12)) cancelBtn.SetHover(WidgetSprite(t->Handle()));
    if (gfx::GpuTexture* t = GetAtlasSprite(13)) cancelBtn.SetPressed(WidgetSprite(t->Handle()));
}

// ===========================================================================
// Dispatch par scène
// ===========================================================================
void LoginScene::Update(ts2::Scene scene) {
    if (pending_ != ts2::Scene::None) return; // en attente de consommation par SceneManager
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectUpdate(); break;
    case ts2::Scene::Login:        LoginUpdate();        break;
    case ts2::Scene::CharSelect:   CharSelectUpdate();   break;
    default: break;
    }
}

void LoginScene::Render(ts2::Scene scene) {
    if (!device_) return;
    gfx::SetActiveSprite(&sprites_);
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectRender(); break;
    case ts2::Scene::Login:        LoginRender();        break;
    case ts2::Scene::CharSelect:   CharSelectRender();   break;
    default: break;
    }
    if (noticeOpen_) RenderNotice(); // popup au-dessus (ordre inverse du routage)
}

void LoginScene::OnMouseDown(ts2::Scene scene, int x, int y) {
    if (pending_ != ts2::Scene::None) return;
    if (noticeOpen_) return; // la notice se ferme au relâchement (comportement modal)
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectOnMouseDown(x, y); break;
    case ts2::Scene::Login:        LoginOnMouseDown(x, y);        break;
    case ts2::Scene::CharSelect:   CharSelectOnMouseDown(x, y);   break;
    default: break;
    }
}

void LoginScene::OnMouseUp(ts2::Scene scene, int x, int y) {
    if (pending_ != ts2::Scene::None) return;
    if (noticeOpen_) {
        // Fidèle UI_NoticeDlg_OnLButtonUp 0x5C03F0 : ferme UNIQUEMENT si le clic tombe sur le
        // bouton OK (slot 8, à panneau+203,+90 — mêmes coords que RenderNotice). Un clic hors
        // du bouton laisse la notice ouverte. (Entrée/Échap ferment aussi, cf. OnKeyDown.)
        gfx::GpuTexture* panel = GetAtlasSprite(7);
        gfx::GpuTexture* okT   = GetAtlasSprite(8);
        if (panel && panel->Valid() && okT && okT->Valid()) {
            const int pW = static_cast<int>(panel->Width()), pH = static_cast<int>(panel->Height());
            const int nx = screenW_ / 2 - pW / 2, ny = screenH_ / 2 - pH / 2;
            const int okX = nx + 203, okY = ny + 90;
            if (x >= okX && x < okX + static_cast<int>(okT->Width()) &&
                y >= okY && y < okY + static_cast<int>(okT->Height()))
                CloseNotice();
        } else {
            CloseNotice(); // assets indisponibles : tout clic ferme (ne pas bloquer l'écran)
        }
        return;
    }
    switch (scene) {
    case ts2::Scene::ServerSelect: ServerSelectOnMouseUp(x, y); break; // Scene_ServerSelectOnMouseUp 0x519AC0 : confirme la sortie
    case ts2::Scene::Login:        LoginOnMouseUp(x, y);        break; // OK se valide au « up »
    case ts2::Scene::CharSelect:   CharSelectOnMouseUp(x, y);   break; // boutons down-arme/up-valide
    default: break;
    }
}

void LoginScene::OnChar(char c) {
    if (noticeOpen_) return;
    const unsigned int ch = static_cast<unsigned char>(c);
    if (idBox_.Focused())            idBox_.OnChar(ch);
    else if (pwBox_.Focused())       pwBox_.OnChar(ch);
    else if (createNameBox_.Focused()) createNameBox_.OnChar(ch);
}

void LoginScene::OnKeyDown(int vk) {
    if (noticeOpen_) {
        if (vk == VK_RETURN || vk == VK_ESCAPE) CloseNotice();
        return;
    }
    // Le widget focalisé gère lui-même édition (Backspace/Suppr/flèches), Tab
    // (SetOnTab) et Entrée (SetOnSubmit).
    if (idBox_.Focused())              idBox_.OnKey(vk);
    else if (pwBox_.Focused())         pwBox_.OnKey(vk);
    else if (createNameBox_.Focused()) createNameBox_.OnKey(vk);
}

// ===========================================================================
// Intro (Scene::Intro) — rendu seul, câblé par SceneManager. game::IntroState et son
// automate (Game/IntroFlow.h) restent détenus par SceneManager ; LoginScene se contente
// de réutiliser ses ressources GPU déjà créées par Init() (sprite batch/police/texture
// blanche), le tout via ts2::ui::IntroRender (positions/dimensions RÉELLES extraites de
// Scene_IntroRender 0x518880, cf. UI/IntroRender.h).
// ===========================================================================
void LoginScene::RenderIntro(const game::IntroState& state) {
    if (!device_) return;
    gfx::SetActiveSprite(&sprites_);

    UiContext ctx;
    ctx.sprites  = &sprites_;
    ctx.font     = &font_;
    ctx.whiteTex = whiteTex_;
    ctx.screenW  = screenW_;
    ctx.screenH  = screenH_;

    if (sprites_.Ready()) {
        sprites_.Begin();
        ctx.phase = UiPhase::Panels;
        introRender_.Render(ctx, state);
        sprites_.End();
    }
    if (font_.Ready()) {
        font_.BeginBatch();
        ctx.phase = UiPhase::Text;
        introRender_.Render(ctx, state);
        font_.EndBatch();
    }
}

// ===========================================================================
// EnterWorld (Scene::EnterWorld) — rendu seul, câblé par SceneManager, MÊME pattern que
// RenderIntro ci-dessus. game::EnterWorldFlowState et son automate (Game/EnterWorldFlow.h,
// Scene_EnterWorldUpdate 0x52BFF0) restent détenus par SceneManager ; LoginScene se
// contente de réutiliser ses ressources GPU déjà créées par Init() (sprite batch/police/
// texture blanche) via ts2::ui::EnterWorldRender (positions/dimensions RÉELLES extraites
// de Scene_EnterWorldRender 0x52C260, cf. UI/EnterWorldRender.h).
// ===========================================================================
void LoginScene::RenderEnterWorld(const game::EnterWorldFlowState& state, int zoneId) {
    if (!device_) return;
    gfx::SetActiveSprite(&sprites_);

    UiContext ctx;
    ctx.sprites  = &sprites_;
    ctx.font     = &font_;
    ctx.whiteTex = whiteTex_;
    ctx.screenW  = screenW_;
    ctx.screenH  = screenH_;

    if (sprites_.Ready()) {
        sprites_.Begin();
        ctx.phase = UiPhase::Panels;
        enterWorldRender_.Render(ctx, state, zoneId);
        sprites_.End();
    }
    if (font_.Ready()) {
        font_.BeginBatch();
        ctx.phase = UiPhase::Text;
        enterWorldRender_.Render(ctx, state, zoneId);
        font_.EndBatch();
    }
}

// ===========================================================================
// Scène 2 — ServerSelect (Scene_ServerSelect* : 0x518B30 / 0x519250 / 0x519780)
// ===========================================================================
void LoginScene::ServerSelectUpdate() {
    // Flux fidèle (Scene_ServerSelectUpdate 0x518B30, machine à 2 sous-états) délégué à
    // game::UpdateServerSelect : sous-état Init compte 30 frames (0x1E) puis passe en Idle.
    // La liste est déjà construite dans Init() (serverState_.listBuilt=true) -> pas de
    // reconstruction ici, seul le compteur de frames avance. dt fixe (30 FPS, flt_815188).
    game::UpdateServerSelect(serverState_, 1.0f / 30.0f);

    // Au passage Init->Idle, lancement UNIQUE du worker de statut serveur — fidèle à
    // CreateThread(Net_ServerStatusThread 0x518AB0) déclenché en fin de sous-état 0 (à
    // 30 frames). L'interrogation TCP (bloquante bornée) se fait sur ce thread, JAMAIS dans
    // ce tick 30 FPS (qui figerait l'UI).
    if (serverState_.subState == game::ServerSelectSubState::Idle && !statusThreadLaunched_) {
        LaunchServerStatusThread();
        // BGM du front-end (Scene_ServerSelectUpdate 0x518BF7 : Snd_LoadOggToBuffers(
        // "G03_GDATA\D10_WORLDBGM\Z000.BGM", boucle)). Chargée+jouée UNE FOIS ici (même
        // passage Init->Idle, statusThreadLaunched_ garantit l'unicité) ; loop continu couvrant
        // ServerSelect ET Login. Décodage Ogg->PCM via le callback ogg d'AudioSystem::Init.
        if (audio::Audio().Available() &&
            bgm_.LoadFromPath("G03_GDATA/D10_WORLDBGM/Z000.BGM", audio::PlayMode::Loop)) {
            bgm_.Play(audio::Audio().MasterVolume());
            TS2_LOG("LoginScene : BGM front-end Z000.BGM chargee et jouee (boucle).");
        }
    }
    // La transition vers Login (scene_id=3) est déclenchée immédiatement au clic serveur
    // (ServerSelectOnMouseDown -> game::OnServerClicked), comme le binaire qui écrit
    // g_SceneMgr[0]=3 depuis OnMouseDown — pas besoin de consommer ici le retour de
    // UpdateServerSelect (Update() cesse d'appeler cette fonction dès pending_ != None).
}

// Worker de statut serveur — reproduit Net_ServerStatusThread 0x518AB0 : `for (i =
// dword_16851B0; i <= dword_16851B4; ++i)` (= plage [selectedGroupBtnLo..selectedGroupBtnHi])
// interroge chaque serveur (nom, port) et écrit maxPopulation/loadStep/currentPopulation.
// L'interrogation (ts2::net::QueryServerStatusLive = Net_QueryServerStatus 0x519CC0, connect
// TCP + recv 17 o, BLOQUANTE bornée par timeout) tourne HORS boucle de rendu ; les résultats
// sont publiés SOUS verrou (serverMutex_) — le rendu (ServerSelectRender) lit une copie
// protégée « au fur et à mesure » (currentPopulation == -1 = interrogation en cours).
//
// Équivalent à game::PollServerStatuses (même plage, même sémantique de parsing/écriture),
// déroulé ici à la main pour publier CHAQUE serveur dès sa réponse (incrémental) tout en
// gardant chaque écriture atomique vis-à-vis du rendu.
void LoginScene::LaunchServerStatusThread() {
    if (statusThreadLaunched_) return;
    statusThreadLaunched_ = true;
    if (!serverHost_.QueryServerStatus) return;

    // Snapshot des cibles (index, nom, port) de la plage active, sous verrou. Le vecteur
    // serverState_.servers est FIGÉ après BuildServerList() (aucune réallocation ensuite),
    // donc les index restent valides pendant toute la durée du worker.
    struct Target { int32_t index; std::string name; uint16_t port; };
    std::vector<Target> targets;
    {
        std::lock_guard<std::mutex> lk(serverMutex_);
        const int32_t lo   = serverState_.selectedGroupBtnLo;
        const int32_t hi   = serverState_.selectedGroupBtnHi;
        const int32_t last = static_cast<int32_t>(serverState_.servers.size()) - 1;
        for (int32_t i = (lo > 0 ? lo : 0); i <= hi && i <= last; ++i) {
            targets.push_back({ i, serverState_.servers[static_cast<size_t>(i)].name,
                                serverState_.servers[static_cast<size_t>(i)].port });
        }
        serverState_.statusThreadActive = true;
    }
    if (targets.empty()) return;

    statusThread_ = std::thread([this, targets] {
        for (const Target& t : targets) {
            const game::ServerStatus st = serverHost_.QueryServerStatus(t.name, t.port); // bloquant borné, hors verrou
            std::lock_guard<std::mutex> lk(serverMutex_);
            if (t.index < 0 || t.index >= static_cast<int32_t>(serverState_.servers.size())) continue;
            game::ServerEntry& s = serverState_.servers[static_cast<size_t>(t.index)];
            if (st.currentPopulation >= 0) {   // record 17 o reçu -> maj max/load/cur (fidèle Net_QueryServerStatus)
                s.maxPopulation     = st.maxPopulation;
                s.loadStep          = st.loadStep;
                s.currentPopulation = st.currentPopulation;
            } else {                           // échec/déconnexion -> -1 (max/load inchangés, fidèle)
                s.currentPopulation = -1;
            }
        }
    });
}

// ALIGNÉ sur la géométrie RÉELLEMENT dessinée par ts2::ui::ServerSelectRender
// (UI/ServerSelectRender.cpp::Render, branche boucle EA 0x5194DD, mode SingleServer
// RÉELLEMENT ACTIF cf. Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md) : même origine panneau
// (centrage sur les dimensions RÉELLES du sprite 001_01786.IMG une fois chargé) + mêmes
// offsets par index (serverselect_layout::ButtonOffsetX/Y, extraits de
// ServerSelect_GetButtonX/Y 0x519F40/0x51A0A0) + même dimension de hit-test
// (kButtonW/kButtonH — la texture réelle 153x23 est plus petite, cf. note de fidélité
// dans ServerSelectRender.cpp, mais le binaire hit-teste bien sur ces dimensions
// nominales de bouton, pas sur la texture). Le hit-test utilise volontairement le
// centrage NOMINAL kPanelW/kPanelH (au lieu des dimensions réelles de la texture
// utilisées par le rendu) : ServerRowRect() est appelée AVANT que ServerSelectRender()
// n'ait pu déclencher le premier chargement de la texture panneau dans certains
// enchaînements (ex. clic sur la toute première frame) — écart de fidélité mineur
// documenté, sans conséquence observable une fois le panneau chargé (mêmes dimensions).
//
// PORTÉE RÉELLE i=0 UNIQUEMENT (mode SingleServer, cf. commentaire de tête de
// ServerSelectRender()/BuildServerList() ci-dessous) : `i` au-delà de 0 n'est ni dessiné
// ni cliquable dans le binaire d'origine pour la commande de lancement documentée
// (`/0/0/2/1024/768` -> g_ServerModeFlag=0 -> UNE SEULE entrée serveur construite).
RECT LoginScene::ServerRowRect(int i) const {
    const int baseX = screenW_ / 2 - serverselect_layout::kPanelW / 2;
    const int baseY = screenH_ / 2 - serverselect_layout::kPanelH / 2;
    return MakeRect(baseX + serverselect_layout::ButtonOffsetX(i),
                     baseY + serverselect_layout::ButtonOffsetY(i),
                     serverselect_layout::kButtonW, serverselect_layout::kButtonH);
}

// Délègue le dessin à ts2::ui::ServerSelectRender (positions/dimensions RÉELLES
// extraites de Scene_ServerSelectRender 0x519250, cf. UI/ServerSelectRender.h) — le
// FLUX/ÉTAT vient désormais du module fidèle game::ServerSelectState (serverState_),
// alimenté par le worker de statut (populations RÉELLES). Le renderer ne modifie jamais
// l'état passé. Hit-test (ServerRowRect) ET dessin partagent la MÊME géométrie
// (serverselect_layout::ButtonOffsetX/Y) — plus de décalage entre le clic et la grille.
void LoginScene::ServerSelectRender() {
    const POINT mp = CursorClient();

    // Copie protégée de l'état serveur : le worker de statut écrit les populations
    // (int32) de serverState_.servers en tâche de fond ; on prend un instantané sous
    // verrou puis on rend depuis cette copie (pas de course, pas de blocage du worker
    // pendant les appels GPU). Les bornes/sélection/fond viennent directement du module
    // (game::BuildServerList : selectedGroupBtnLo/Hi, backgroundImageId, maxPopulation
    // alimentée par le serveur -> barre de charge RÉELLE).
    game::ServerSelectState state;
    {
        std::lock_guard<std::mutex> lk(serverMutex_);
        state = serverState_;
    }

    UiContext ctx;
    ctx.sprites  = &sprites_;
    ctx.font     = &font_;
    ctx.whiteTex = whiteTex_;
    ctx.screenW  = screenW_;
    ctx.screenH  = screenH_;

    // Scene_ServerSelectRender fait `if (g_ServerModeFlag) { <gros nombre> } else {
    // <boucle boutons> }` (EA 0x5194CB) — le choix de branche RENDU est keyé UNIQUEMENT
    // sur le flag, PAS sur le nombre d'entrées. On le reproduit fidèlement via
    // singleServerMode = (serverModeFlag_ != 0) :
    //   - flag == 0 (SEUL mode actif pour `/0/0/2/1024/768`) -> branche `else`/boucle
    //     (singleServerMode=false) ; BuildServerList() a construit 1 entrée -> 1 bouton.
    //   - flag != 0 (1/2/MultiChannel) -> branche « gros nombre » (singleServerMode=true),
    //     population de l'entrée 0 en gros chiffres (UI_DrawNumberValue), fidèle au binaire
    //     qui ne dessine PAS de grille dans ce cas même quand la table a 6 canaux.
    const bool singleServerMode = (serverModeFlag_ != 0);
    if (sprites_.Ready()) {
        sprites_.Begin();
        ctx.phase = UiPhase::Panels;
        serverSelectRender_.Render(ctx, state, mp.x, mp.y, singleServerMode);
        sprites_.End();
    }
    if (font_.Ready()) {
        font_.BeginBatch();
        ctx.phase = UiPhase::Text;
        serverSelectRender_.Render(ctx, state, mp.x, mp.y, singleServerMode);
        font_.EndBatch();
    }

    // MsgBox modal partagé (dword_1822438) en QUEUE de scène (UI_RenderAllDialogs 0x5AE2D0) —
    // dessiné PAR-DESSUS l'écran ServerSelect. Ne dessine rien s'il n'est pas ouvert.
    RenderMsgBox();
}

// Scene_ServerSelectOnMouseDown 0x519780 : la boucle de hit-test serveur réelle est
// `for (i = this[15372]; i <= this[15373]; ++i)` (EA 0x519974). Bornée ici sur la plage
// active [selectedGroupBtnLo..selectedGroupBtnHi] du module (mode SingleServer -> [0,0]).
//
// FLUX FIDÈLE : la sélection est déléguée à game::OnServerClicked (EA 0x5199a2-0x519a3f),
// qui n'accepte le clic QUE si la population est connue (currentPopulation >= 0) ET
// strictement < maxPopulation (this[12371], alimentée par le serveur via le worker de
// statut). Tant que le serveur n'a pas répondu (maxPopulation == 0 / curPop == -1), le
// clic est IGNORÉ, EXACTEMENT comme le binaire — pas de transition vers Login sur un
// serveur plein ou dont le statut n'est pas encore arrivé. OnServerClicked persiste aussi
// le choix (host.SaveLastServer -> Cfg_SaveLastServer) et écrit selectedServer (this[15374]).
void LoginScene::ServerSelectOnMouseDown(int x, int y) {
    // MsgBox modal prioritaire (UI_MsgBox_OnLButtonDown 0x5C0980) : consomme le clic.
    if (msgBox_.IsOpen()) {
        msgBox_.OnMouseDown([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(serverMutex_); // OnServerClicked lit les populations écrites par le worker
        const int hi = static_cast<int>(serverState_.servers.size()) - 1;
        const int lo = serverState_.selectedGroupBtnLo > 0 ? serverState_.selectedGroupBtnLo : 0;
        for (int i = lo; i <= hi && i <= serverState_.selectedGroupBtnHi; ++i) {
            if (RectContains(ServerRowRect(i), x, y)) {
                if (game::OnServerClicked(serverState_, serverHost_, i)) {
                    loginSub_ = LoginSub::Init;      // ré-init de l'écran login
                    pending_  = ts2::Scene::Login;   // scene_id = 3 (le binaire écrit g_SceneMgr[0]=3 ici)
                }
                return; // clic consommé (accepté ou refusé pour cause de population)
            }
        }
    }
    // Aucun serveur touché : armer le latch du bouton d'action/sortie (slot 4, ancre 891,701).
    // Scene_ServerSelectOnMouseDown 0x519A79-0x519AAF : UI_ProjectSpriteToScreen(slot 4,891,701)
    // puis Sprite2D_HitTest(unk_8E8DA0) -> this[3]=1 si le curseur est sur le sprite.
    UiContext ctx;
    ctx.screenW = screenW_;
    ctx.screenH = screenH_;
    serverSelectRender_.OnActionButtonMouseDown(x, y, ctx);
}

// Scene_ServerSelectOnMouseUp 0x519AC0 : au relâchement, si le bouton d'action était armé
// (this[3]) ET que le curseur est toujours dessus, ouvre la confirmation de sortie
// (UI_MsgBox_Open dword_1822438, action_id=1, corps = StrTable005_Get(g_LangId,1) EA
// 0x519B31, appel EA 0x519B3E). Sinon rien. Quand l'overlay est déjà ouvert, route le clic
// vers ses boutons Oui/Non.
void LoginScene::ServerSelectOnMouseUp(int x, int y) {
    // MsgBox modal prioritaire (UI_MsgBox_OnLButtonUp 0x5C0A90) : OK -> action de sortie, sinon ferme.
    if (msgBox_.IsOpen()) {
        msgBox_.OnMouseUp([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    UiContext ctx;
    ctx.screenW = screenW_;
    ctx.screenH = screenH_;
    if (serverSelectRender_.OnActionButtonMouseUp(x, y, ctx)) {
        // UI_MsgBox_Open(dword_1822438, 1, StrTable005_Get(g_LangId,1), "") @0x519B3E. Action OK
        // (type 1) = UI_MsgBox_OnLButtonUp case 1 (EA 0x5C0BEC-0x5C0BFB) : journalise
        // "[ABNORMAL_END] ( 4 )" (0x7BA830) puis g_QuitFlag=1 -> PostQuitMessage(0) (idiome projet).
        msgBox_.Open(game::Str(1), 1, [] {
            TS2_LOG("[ABNORMAL_END] ( 4 )");
            PostQuitMessage(0);
        });
    }
}

// ===========================================================================
// Scène 3 — Login (Scene_Login* : 0x51A8D0 / 0x51B020 / 0x51B5D0 / 0x51B780)
// ===========================================================================
void LoginScene::SetFocus(int field) {
    focusField_ = field;                    // dword_1668FC0
    idBox_.SetFocused(field == 1);
    pwBox_.SetFocused(field == 2);
}

// Ordre littéral ALIGNÉ sur le réel (EA 0x51A946 puis 0x51A954 puis 0x51A965/
// 0x51A976 — Docs/TS2_LOGINSCENE_AUDIT.md §3.8, fermé) : le binaire remet le
// compteur de frames à 0 et refocalise le champ ID AVANT de vider les EDIT
// Win32 (`SetWindowTextA(ID,"")` puis `SetWindowTextA(PW,"")`), pas après.
// Aucune différence de comportement à la frame suivante (Clear()/SetFocus()
// sont indépendants), mais la séquence d'appels reproduit maintenant l'ordre
// exact du désassemblage.
void LoginScene::ResetLoginFields() {
    frame_ = 0;                             // EA 0x51A946 (a1[2] = 0)
    SetFocus(1);                            // EA 0x51A954 — focus EDIT ID (UI_FocusEditBox(1))
    idBox_.Clear();                         // EA 0x51A965 — SetWindowTextA(ID, "")
    pwBox_.Clear();                         // EA 0x51A976 — SetWindowTextA(PW, "")
}

// Action kind=2 de UI_NoticeDlg_OnLButtonUp (cf. déclaration). Fermer la socket avant le
// changement de scène est fidèle (Net_CloseSocket EA 0x5C04DF, avant *a1=2/a1[1]=0 EA
// 0x5C04E4-0x5C04F8) ; loginSub_=Init prépare un réaffichage propre si l'utilisateur
// revient plus tard sur Login (même motif que exitBtn_.SetOnClick, cf. Init()).
void LoginScene::AbortLoginToServerSelect() {
    if (net_) net::NetCloseSocket(net_->Client()); // EA 0x5C04DF
    pending_  = ts2::Scene::ServerSelect;           // EA 0x5C04E4 (g_SceneMgr = 2)
    loginSub_ = LoginSub::Init;                     // EA 0x5C04EE (g_SceneSubState = 0)
    frame_    = 0;                                  // EA 0x5C04F8 (dword_1676188 = 0) — 3e
    // champ RAZ par UI_NoticeDlg_OnLButtonUp kind=2, manquant ici jusqu'à cette passe
    // (décompilation fraîche 0x5C03F0, 2026-07-14) : frame_ est le même compteur partagé
    // (this[2]) que ServerSelectUpdate()/LoginUpdate() incrémentent ; sans cette RAZ il
    // repartait avec la valeur laissée par l'écran précédent au lieu de 0.
}

// (LoginPanelOrigin() supprimé le 2026-07-15 : fonction morte, non déclarée dans la classe et
//  jamais appelée. LoginRender / LoginOnMouseDown / LoginOnMouseUp recalculent l'origine du
//  panneau en ligne à partir de la taille RÉELLE du sprite slot 14, cf. LoginRender.)

// Offsets hardcodés du panneau login. Distinction EXACTE (corrigée) entre la ZONE du champ
// (contrôle EDIT, UI_CreateEditBoxes 0x50E460 : ID @+(118,54), PW @+(118,90), 319x21) et la
// position du TEXTE dessiné (Scene_LoginRender 0x51B020 : UI_DrawNumberValue à +(126,60)/(126,95),
// soit +8/+6 dans le champ). Le hit-test/focus du champ utilise la vraie zone 319x21 ; le texte
// (DrawFieldValue) reste dessiné à +(126,60)/(126,95). Boutons : OK +(298,126), Quitter +(374,126),
// case ombres +(21,130).
void LoginScene::LayoutLogin(int px, int py) {
    idBox_.SetBounds(px + 118, py + 54, 319, 21); // g_hEditLoginId — zone EDIT réelle (319x21), Scene_LoginOnMouseDown 0x51B658 refs dword_166901C..28
    pwBox_.SetBounds(px + 118, py + 90, 319, 21); // g_hEditLoginPw — zone EDIT réelle (319x21), Scene_LoginOnMouseDown 0x51B695 refs dword_166902C..38
    okBtn_.SetBounds(px + 298, py + 126, kBtnW,  kBtnH);    // unk_8E9084, render/hit EA 0x51B48D/0x51B80E
    exitBtn_.SetBounds(px + 374, py + 126, kBtnW, kBtnH);   // unk_8E9240, render/hit EA 0x51B50F/0x51B85C
    optBtn_.SetBounds(px + 21,  py + 130, 16, 16);          // unk_9555BC : rendu EA 0x51B571 ; hit IDA à y+131 EA 0x51B74D/0x51B8B6
}

// Scene_LoginUpdate 0x51A8D0 : machine à sous-états (this[1]).
void LoginScene::LoginUpdate() {
    switch (loginSub_) {
    case LoginSub::Init:                    // case 0 — ordre littéral EA 0x51A8FD-0x51A976
        // EA 0x51A8FD : Util_SetClampedU8Field(&dword_8E714C, 0) — remet la forme du curseur
        // au slot 0 (flèche) à l'entrée Login. CÂBLÉ (C-cursor) : game::Cursors() est le singleton
        // UNIQUE (mPOINTER) désormais tické par App -> SetActiveSlot(0) prend effet (même triplet
        // ResetAllDialogs->curseur->focus que les autres entrées de scène, cf. SceneManager @0x52C044).
        game::Cursors().SetActiveSlot(game::kCursorDefault); // 0x51A8FD / 0x4C1110
        SetFocus(0);                        // EA 0x51A909 — défocus générique avant la RAZ
        okBtn_.Reset(); exitBtn_.Reset(); optBtn_.Reset(); // EA 0x51A90E-0x51A92F (a1[3..5],
        // sous-ensemble identifié des 150 dwords RAZ ; le reste (147 dw) appartient à des
        // champs partagés de cSceneMgr non modélisés dans LoginScene, cf. audit §3.3).
        loginSub_ = LoginSub::Idle;         // EA 0x51A93C (a1[1] = 1)
        frame_    = 0;                      // EA 0x51A946 (a1[2] = 0)
        ResetLoginFields();                 // EA 0x51A954-0x51A976 : focus ID + vide ID/PW —
        // ordre interne (focus AVANT clear) désormais conforme, cf. corps de
        // ResetLoginFields() (Docs/TS2_LOGINSCENE_AUDIT.md §3.8, clos).
        break;
    case LoginSub::Idle:                     // case 1
        ++frame_;                            // this[2]++
        // Fidèle : toutes les 30 frames, Ac_GameGuard_Heartbeat 0x6DE3F7 ; si
        // != 1877 -> g_QuitFlag = 1. GameGuard hors périmètre du shell UI.
        break;
    case LoginSub::Trigger:                  // case 2
        loginSub_ = LoginSub::DoLogin;       // this[1] = 3
        frame_    = 0;
        break;
    case LoginSub::DoLogin:                  // case 3
        DoLogin();
        break;
    case LoginSub::NoticeWait:               // case 4 — AUCUN case 4 réel (default: no-op).
        // Vérifié par désassemblage (cf. LoginSub::NoticeWait) : la sortie est pilotée par
        // le clic OK de la notice (OpenNotice(..., AbortLoginToServerSelect) dans DoLogin()),
        // PAS par cette fonction. Ne rien faire ici est la reproduction fidèle.
        break;
    }
}

// Sous-état 3 : lit ID/PW, ConnectLoginServer puis LoginRequest (op 0x0B, extra =
// net::kLoginExtra = 90218 = 0x1606A). CORRECTION (W5b) : ce commentaire annonçait
// « ver 106 » — FAUX, 106 = 0x6A n'est que l'octet BAS de la vraie valeur. Preuve :
// `push 1606Ah` EA 0x51ab0e devant `call Net_LoginRequest` EA 0x51ab20 dans
// Scene_LoginUpdate 0x51A8D0 (appelant UNIQUE de Net_LoginRequest 0x51B8E0).
// Ne pas « re-corriger » vers 106 : cf. Net/Login.h:39-41 qui documente la faute d'origine.
//
// Les 4 notices ci-dessous ont TOUTES kind=2 dans le binaire (EA 0x51AA3D, 0x51AA92,
// 0x51AF09 et tout le reste du switch, `push 2` confirmé par désassemblage devant CHAQUE
// appel UI_NoticeDlg_Open — cf. LoginSub::NoticeWait) : le clic OK sur N'IMPORTE LAQUELLE
// d'entre elles ferme la socket et renvoie à ServerSelect (AbortLoginToServerSelect), même
// pour les deux premières (champ vide) où le sous-état repasse pourtant IMMÉDIATEMENT à
// Idle — les deux effets sont réels et non exclusifs : Idle reprend la main tout de suite
// (le joueur peut continuer de taper pendant que la notice reste affichée, fidèle à
// l'absence de gating de l'état 1 sur noticeOpen_), puis le clic OK, quand il survient,
// écrase tout et repart en ServerSelect.
// Pré-zérotage EA 0x51A9F1/0x51AA05 (Docs/TS2_LOGINSCENE_AUDIT.md §3.7, clos SANS
// portage de code — non applicable) : le binaire fait `Crt_Memset(g_AccountName, 0,
// 128)` / `Crt_Memset(byte_1669214, 0, 128)` AVANT chaque `GetWindowTextA`, car ce
// sont des buffers C fixes réutilisés d'une frame sur l'autre — sans ce memset, un
// `GetWindowTextA` qui échoue laisserait les octets de la frame précédente (fuite
// d'un ancien mot de passe résiduel en mémoire). `idBox_.Text()`/`pwBox_.Text()`
// (std::string) n'ont pas cette classe de bug : chaque appel reflète EXACTEMENT le
// contenu actuel du widget, il n'existe pas d'état résiduel à effacer. Répliquer un
// "memset" ici n'aurait ni équivalent syntaxique ni effet observable — ce n'est pas
// un écart de comportement, c'est un détail d'implémentation obsolète avec
// l'abstraction std::string. Laissé tel quel intentionnellement.
void LoginScene::DoLogin() {
    // Le binaire : GetWindowTextA(ID)/GetWindowTextA(PW) ; vide -> NoticeDlg
    // (StrTable005 2905/2906) et retour au sous-état idle.
    if (idBox_.Text().empty()) {
        OpenNotice(game::Str(2905),                                   // ID vide -> StrTable005 str2905 (réel)
                   [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::Idle; frame_ = 0; return;
    }
    if (pwBox_.Text().empty()) {
        OpenNotice(game::Str(2906),                                   // PW vide -> StrTable005 str2906 (réel)
                   [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::Idle; frame_ = 0; return;
    }
    if (!net_ || serverState_.servers.empty()) {
        // Garde défensive (hors binaire : le serveur est codé en dur) -> message générique str6.
        OpenNotice(game::Str(6), [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::NoticeWait; frame_ = 0; return;
    }

    // Hôte/port du serveur sélectionné (serverState_ = table du module fidèle). L'entrée
    // game::ServerEntry porte le nom d'hôte dans `name` (SingleServer : hôte login RÉEL
    // "12sky2-login.geniusorc.com", cf. game::BuildServerList / BuildServerList()). Lecture
    // sous verrou (le worker de statut écrit d'autres champs de ces entrées en parallèle) ;
    // on copie hôte+port en local avant l'appel réseau bloquant.
    std::string svHost;
    uint16_t    svPort = 0;
    {
        std::lock_guard<std::mutex> lk(serverMutex_);
        int idx = serverState_.selectedServer;               // this[15374] (écrit par OnServerClicked)
        if (idx < 0 || idx >= static_cast<int>(serverState_.servers.size())) idx = 0; // garde (flux normal : >= 0)
        const game::ServerEntry& sv = serverState_.servers[static_cast<size_t>(idx)];
        svHost = sv.name;
        svPort = sv.port;
    }

    // Handshake login synchrone/bloquant (bannière 17o -> clé XOR). Opère sur la
    // socket du système réseau partagé (net_->Client()).
    const int rc = net::ConnectLoginServer(net_->Client(), svHost.c_str(), svPort);
    if (rc != net::kNetOk) {
        OpenNotice(ConnectErrText(rc), [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::NoticeWait; frame_ = 0; return;
    }

    // Requête id/pw (op 0x0B). En cas de succès, LoginRequest copie le jeton de
    // compte dans net::g_AccountName (réutilisé par ConnectGameServer) + le
    // niveau GM dans net::g_GmAuthLevel.
    int result = 0;
    net::LoginRequest(net_->Client(), idBox_.Text().c_str(), pwBox_.Text().c_str(),
                      net::kLoginExtra, result);
    if (result == 0) {
        loggedUser_ = idBox_.Text();
        // Ré-initialise le flux CharSelect (sous-état Init -> recalcul de l'occupation
        // des emplacements 30 frames plus tard, cf. Game/CharSelectFlow.h). Le serveur
        // de jeu réel (hôte/port) vient de Net_ReqEnterCharInfo (opcode 22), PAS d'un
        // placeholder mémorisé ici (cf. Docs/TS2_CHARSELECT_AUDIT.md §2.5).
        charState_ = game::CharSelectState{};
        pending_  = ts2::Scene::CharSelect; // scene_id = 4
        loginSub_ = LoginSub::Init;         // ré-init si retour ultérieur
    } else {
        OpenNotice(LoginErrText(result), [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::NoticeWait;   // this[1] = 4
        frame_    = 0;
    }
}

// Scene_LoginRender 0x51B020 : panneau centré, champs (texte à +126,+60/+95 avec
// caret), boutons OK/Quitter et case « ombres ».
void LoginScene::LoginRender() {
    // Panneau réel (unk_8E9368, slot 14 -> 001_00015.IMG, 470x167 logiques) : dimensions
    // RÉELLES de la texture si chargée (centrage plus fidèle que l'ancien gabarit nominal
    // kPanelW/kPanelH), repli sur ce même gabarit nominal sinon (fichier absent/illisible).
    gfx::GpuTexture* panelTex = GetAtlasSprite(14);
    const bool panelValid = panelTex && panelTex->Valid() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    const int px = screenW_ / 2 - panelW / 2;
    const int py = screenH_ / 2 - panelH / 2;
    LayoutLogin(px, py);

    const POINT mp = CursorClient();
    // Met à jour l'état de survol interne des boutons (Hovered()).
    okBtn_.OnMouseMove(mp.x, mp.y);
    exitBtn_.OnMouseMove(mp.x, mp.y);
    optBtn_.OnMouseMove(mp.x, mp.y);

    if (sprites_.Ready()) {
        sprites_.Begin();
        // Fond plein écran : sprite atlas[bgIndex] unk_8E8B50 réel (Scene_LoginRender
        // 0x51B020, EA 0x51B207 — MÊME this[168] que ServerSelect, cf. GetAtlasSprite).
        // ZÉRO fallback : si la texture n'est pas chargée, rien n'est dessiné.
        DrawFullscreenBg(bgAtlasSlot_);
        // Panneau (unk_8E9368 = slot 14 -> 001_00015.IMG) : texture réelle, blit non étiré à
        // sa taille native. ZÉRO fallback (fidèle Sprite2D_Draw) : rien si non chargé.
        if (panelValid)
            sprites_.DrawSprite(panelTex->Handle(), nullptr, px, py, gfx::kSpriteWhite);
        // Champs ID / PW : Scene_LoginRender 0x51B020 ne dessine JAMAIS de rectangle de fond
        // pour ces champs (l'encadré est gravé dans la texture du panneau). Aucun aplat.
        // Boutons OK/Quitter : sprite réel "Confirm"/"Cancel" survol/pressé (slots 9/10 et
        // 12/13, cf. ApplyConfirmCancelSkin) — invisibles à l'état idle, comme le binaire
        // (aucun Sprite2D_Draw hors survol/appui, EA 0x51B48D/0x51B50F). Aucun aplat de repli.
        okBtn_.DrawSkin(sprites_);
        exitBtn_.DrawSkin(sprites_);
        // Case option « ombres » (unk_9555BC = slot 3007 -> 001_03008.IMG) : le binaire ne
        // dessine ce sprite RÉEL que si g_Opt_GfxDetailShadows==1 (EA 0x51B556). Sprite réel,
        // aucun aplat.
        if (shadowsEnabled_) {
            if (gfx::GpuTexture* t = GetAtlasSprite(3007))
                sprites_.DrawSprite(t->Handle(), nullptr, optBtn_.X(), optBtn_.Y(), gfx::kSpriteWhite);
        }
        // Caret réel des champs (Sprite2D_Draw(unk_8EA42C = slot 43) à panneau+largeurTexte
        // +127, y — EA 0x51B34F (ID) / 0x51B445 (PW)) : dessiné dans le batch SPRITE quand
        // le champ est focalisé (test g_UIEditBoxMgr==1/2), EN CONTINU (le binaire ne fait
        // AUCUN test de clignotement). Repli caret texte « | » (batch Font) si le sprite est
        // indisponible. Mêmes offsets que DrawFieldValue (panneau+126,+60 / +126,+95).
        DrawFieldCaretSprite(idBox_, px + 126, py + 60);
        DrawFieldCaretSprite(pwBox_, px + 126, py + 95);
        sprites_.End();
    }

    if (font_.Ready()) {
        font_.BeginBatch();
        // Scene_LoginRender 0x51B020 ne contient STRICTEMENT AUCUN appel de dessin de texte
        // hors UI_DrawNumberValue (valeur des champs ID/PW) : ni titre, ni libellés de champ,
        // ni légende de bouton/case (0 appel StrTable005_Get et 0 appel Font_*/UI_Text* dans
        // toute la fonction — ces textes sont gravés dans la texture du panneau unk_8E9368).
        // On ne dessine donc QUE la valeur réelle des champs. Aucun texte de repli inventé.
        DrawFieldValue(idBox_, px + 126, py + 60); // texte à panneau+126,+60
        DrawFieldValue(pwBox_, px + 126, py + 95); //          panneau+126,+95 (masqué '*')
        font_.EndBatch();
    }
}

// Affiche la VALEUR d'un champ (masque '*' pour un mot de passe) + un caret texte « | »
// de REPLI. Le caret RÉEL est un sprite (slot 43, cf. DrawFieldCaretSprite, dessiné dans le
// batch sprite) : ici on ne trace le « | » que si ce sprite est indisponible. Fidèle :
// le binaire dessine le caret EN CONTINU quand le champ est focalisé (Sprite2D_Draw
// unk_8EA42C sous test g_UIEditBoxMgr==1/2, EA 0x51B322/0x51B418 — SANS clignotement) ;
// l'ancien clignotement (frame_/15) était une invention, retiré.
void LoginScene::DrawFieldValue(const EditBox& box, int tx, int ty) {
    std::string disp = box.Text();
    if (box.IsPassword())
        disp.assign(disp.size(), kPasswordMaskChar); // Crt_Memset(buf, 42, len)
    if (!disp.empty())
        font_.DrawTextAt(disp.c_str(), tx, ty, kColText);
    if (box.Focused() && !CaretSpriteReady()) {
        const int cx = tx + (disp.empty() ? 0 : font_.MeasureText(disp.c_str()));
        font_.DrawTextAt("|", cx + 1, ty, kColText);
    }
}

// true si le sprite caret réel (slot 43 de l'atlas UI = unk_8EA42C) est chargeable.
// Détermine si le caret RÉEL sera dessiné (batch sprite) ou son repli texte « | »
// (batch font, DrawFieldValue) — les deux batches sont cohérents via ce même test.
bool LoginScene::CaretSpriteReady() {
    gfx::GpuTexture* t = GetAtlasSprite(43);
    return t && t->Handle();
}

// Caret réel : Sprite2D_Draw(unk_8EA42C = slot 43) à panneau+largeurTexte+127, y
// (EA 0x51B34F ID / 0x51B445 PW). Affiché EN CONTINU quand le champ est focalisé (fidèle,
// pas de clignotement). tx/ty = ancre du texte de valeur (panneau+126,+60 / +126,+95) ;
// l'origine caret binaire est +127 (soit tx+1) après la largeur du texte. No-op si le
// sprite est indisponible (repli texte assuré par DrawFieldValue).
void LoginScene::DrawFieldCaretSprite(const EditBox& box, int tx, int ty) {
    if (!box.Focused() || !sprites_.Ready()) return;
    gfx::GpuTexture* caret = GetAtlasSprite(43);
    if (!caret || !caret->Handle()) return;
    std::string disp = box.Text();
    if (box.IsPassword())
        disp.assign(disp.size(), kPasswordMaskChar);
    const int w = disp.empty() ? 0 : font_.MeasureText(disp.c_str());
    sprites_.DrawSprite(caret->Handle(), nullptr, tx + w + 1, ty, gfx::kSpriteWhite);
}

// Scene_LoginOnMouseDown 0x51B5D0 : gardé par this[1]==1 (idle).
void LoginScene::LoginOnMouseDown(int x, int y) {
    if (loginSub_ != LoginSub::Idle) return;
    // Origine du panneau = taille RÉELLE du sprite 14, comme LoginRender/IDA
    // (Scene_LoginOnMouseDown 0x51B5D0 : Sprite2D_GetWidth/Height unk_8E9368 aux EA 0x51B608/0x51B62B).
    gfx::GpuTexture* panelTex = GetAtlasSprite(14);
    const bool panelValid = panelTex && panelTex->Valid() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    LayoutLogin(screenW_ / 2 - panelW / 2, screenH_ / 2 - panelH / 2);

    // Chaque EditBox se focalise si le clic tombe dedans, se défocalise sinon.
    idBox_.OnMouseDown(x, y);
    pwBox_.OnMouseDown(x, y);
    focusField_ = idBox_.Focused() ? 1 : (pwBox_.Focused() ? 2 : 0);

    // Boutons : armement du latch (l'action se déclenchera au relâchement dedans).
    okBtn_.OnMouseDown(x, y);   // this[3]=1
    exitBtn_.OnMouseDown(x, y); // this[4]=1
    optBtn_.OnMouseDown(x, y);  // this[5]=1
}

// Scene_LoginOnMouseUp 0x51B780 : validation OK/Quitter/Ombres si toujours survolé.
// Les callbacks SetOnClick (Init) déclenchent Trigger / retour ServerSelect / toggle.
void LoginScene::LoginOnMouseUp(int x, int y) {
    if (loginSub_ != LoginSub::Idle) return;
    // Même origine réelle que le rendu : Scene_LoginOnMouseUp 0x51B780,
    // Sprite2D_GetWidth/Height unk_8E9368 aux EA 0x51B7B8/0x51B7D7.
    gfx::GpuTexture* panelTex = GetAtlasSprite(14);
    const bool panelValid = panelTex && panelTex->Valid() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    LayoutLogin(screenW_ / 2 - panelW / 2, screenH_ / 2 - panelH / 2);

    okBtn_.OnMouseUp(x, y);   // -> onClick : loginSub_ = Trigger (sous-état 2)
    exitBtn_.OnMouseUp(x, y); // -> onClick : pending_ = ServerSelect (scene_id = 2)
    optBtn_.OnMouseUp(x, y);  // -> onClick : toggle shadowsEnabled_ (0x84DEF8)
}

// ===========================================================================
// Scène 4 — CharSelect. Flux/décision = Game/CharSelectFlow.h::UpdateCharSelect
// + les fonctions OnXxxButtonClicked/ConfirmXxx (câblées en Init() via SetOnClick,
// motif down-arme/up-valide identique aux boutons Login). LoginScene ne fait plus
// que router les événements souris/clavier vers ce module et dessiner charState_.
// ===========================================================================

// Scene_CharSelectUpdate 0x51BD90 : sous-états Init(30f)->Actif (keepalive/30f,
// séquence d'entrée en jeu au minuteur d'aperçu)->Verrouillé. dt fixe (30 FPS,
// cf. CLAUDE.md flt_815188=0.033333) — App_FrameTick appelle Update() 1x/frame.
void LoginScene::CharSelectUpdate() {
    ++frame_;
    const game::CharSelectTransition t = game::UpdateCharSelect(charState_, charHost_, 1.0f / 30.0f);
    // [A5/A3] Transition vers ServerSelect (scène 2) — DEUX producteurs, tous deux HORS du
    // tick : le bouton RETOUR (Scene_CharSelectOnMouseUp, `this[0]=2` EA 0x525A51) et le clic
    // OK d'un NoticeDlg de MODE 2 (UI_NoticeDlg_OnLButtonUp 0x5C03F0 case 2, `g_SceneMgr=2`
    // EA 0x5C04E4). game::UpdateCharSelect les remonte EN TÊTE (CharSelectFlow.cpp) ; sans
    // cette branche, les DEUX étaient avalés et l'état Verrouillé restait un cul-de-sac
    // définitif. PAS de notice 44/45 : le binaire n'en ouvre aucune sur ce chemin.
    if (t == game::CharSelectTransition::ServerSelect) {
        pending_ = ts2::Scene::ServerSelect; // this[0] = 2 (EA 0x525A51 / 0x5C04E4)
        return;
    }
    if (t == game::CharSelectTransition::EnterWorld) {
        // zoneId AUTHENTIQUE = celui renvoyé par Net_ReqEnterCharInfo (écrase toute
        // valeur locale, cf. CharSelectFlow.h EnterCharInfoResult).
        game::g_World.zoneId = charState_.enterWorldZoneId;
        // Nom + position locale du personnage choisi (fiche CharSelect, cf. Net/
        // CharSelectPackets.h::CharSlotInfo) : consommés par le payload tail72/name13 de
        // Net_SendPacket_Op12 (EnterWorld, cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md et
        // Game/GameState.h::SelfState::spawnX/Y/Z pour la provenance EA exacte).
        if (charState_.enterWorldSlot >= 0 &&
            static_cast<size_t>(charState_.enterWorldSlot) < charState_.slots.size()) {
            const auto& slot = charState_.slots[static_cast<size_t>(charState_.enterWorldSlot)];
            game::g_World.self.localPlayerName = slot.name;
            game::g_World.self.spawnX = slot.localPosX;
            game::g_World.self.spawnY = slot.localPosY;
            game::g_World.self.spawnZ = slot.localPosZ;
        }
        // [A18] Angle de spawn : LU depuis l'état, PAS re-tiré ici. Le `Rng_Next()%360` est
        // DANS le bloc d'entrée de Scene_CharSelectUpdate (EA 0x51c7ed), c'est-à-dire AVANT
        // Net_ReqEnterCharInfo (op22, EA 0x51c81d) et donc AVANT les 4 tirages de nonce de
        // l'op22 puis les 4 de l'op11 : re-tirer ICI (après coup) DÉCALAIT le flux PRNG
        // partagé de net::DefaultRng() d'un cran par rapport au binaire, ce qui change TOUS
        // les nonces réseau émis ensuite. game::CharSelectState::enterWorldSpawnRotationDeg
        // porte le tirage au bon point du flux (wiring TODO SPAWN_ROT de
        // Game/CharSelectFlow.h:499 — FERMÉ ici). flt_1675AC4 ET flt_1675AC8 reçoivent la
        // MÊME valeur (EA 0x51c7ed/0x51c7f9) : un seul champ suffit côté C++
        // (Net/CharSelectPackets.h::kTail72OffRotA/RotB la dupliquent à la sérialisation).
        game::g_World.self.spawnRotationDeg = charState_.enterWorldSpawnRotationDeg;
        pending_ = ts2::Scene::EnterWorld; // scene_id = 5
    }
}

// [D2] CharSlotRect() a été SUPPRIMÉ (fonction + déclaration) : son dernier appelant a
// disparu quand le hit-test de sélection est passé sur AtlasHitTest(1657, ...), qui teste le
// sprite de SURBRILLANCE aux coordonnées exactes du binaire (@0x522688) plutôt qu'un RECT de
// fiche reconstruit. Il portait en outre un gabarit INVENTÉ (kNominalW=176 / kNominalH=44)
// utilisé en repli — proscrit en mission pixel-perfect. Les ancres qu'il documentait sont
// désormais dans CharListRender() (origine X = nWidth-0xC2 @0x51D4EC, Y0 = 0x13 @0x51D4F5,
// pas 44 = `imul .., 2Ch` @0x51D590, décalage +0x22 @0x51D599).

// Lit un int32 dans la fiche BRUTE (net::g_CharRecords[slot] = &unk_1669380 + 0x2768*slot).
// Cf. LoginScene.h pour la raison d'être : game::CharSlotInfo n'expose ni +60 ni +5708 (les
// deux discriminants des cascades de tier), ni les 11 champs du panneau de détail.
int32_t LoginScene::CharRecI32(int32_t slot, int byteOffset) {
    if (slot < 0 || slot >= net::kCharRecordCount) return 0;
    if (byteOffset < 0 || byteOffset + static_cast<int>(sizeof(int32_t)) > net::kCharRecordSize)
        return 0;
    int32_t v = 0;
    std::memcpy(&v, net::g_CharRecords[slot] + byteOffset, sizeof(v));
    return v;
}

// [C2] Colonne des 10 boutons — origine et pas RE-PROUVÉS À L'OCTET ce front :
//   x0 = nWidth - 0x8C (140) @0x51DF12 ; y0 = nHeight - 0x12D (301) @0x51DF20 (écrits UNE
//   fois, @0x51DF17/@0x51DF26, jamais réécrits dans le bloc).
//   deltas y décodés depuis les octets : `sub edx,25h` (83 EA 25) @0x51E347 = -37 (slots
//   3086) ; ENTRER +0 ; `add eax,25h` (83 C0 25) @0x51DFDE = +37 ; `add eax,4Ah` (83 C0 4A)
//   @0x51E06C = +74 ; `add eax,6Fh` (83 C0 6F) @0x51E0FA = +111 ; `add eax,94h`
//   (05 94 00 00 00) @0x51E18C = +148 ; `add edx,0B9h` @0x51E215 = +185 (RETOUR) ;
//   `add ecx,0DEh` @0x51E29F = +222 (QUITTER).
//
// ⚠ DIMENSIONS : le gabarit inventé kColBtnW=128 / kColBtnH=34 de l'ancien code a été
//   SUPPRIMÉ. Les vraies dimensions vivent dans spr+108/+112, remplis au chargement du .IMG
//   (non déterminables en statique — spec §13.1) : on prend donc la taille NATIVE de la
//   texture du sprite NORMAL, qui est la source exacte, disponible au runtime comme dans le
//   binaire. Sprite absent -> bounds 0x0 -> le bouton ne peut pas être cliqué, ce qui est
//   cohérent avec un sprite non chargé (Sprite2D_HitTest testerait un rect 0x0).
// ⚠ Le hit-test du binaire porte sur le sprite NORMAL — d'où les slots 3086/16/19/22/963/25.
void LoginScene::LayoutCharSelect() {
    const int x0 = screenW_ - 0x8C;  // nWidth  - 140 @0x51DF12
    const int y0 = screenH_ - 0x12D; // nHeight - 301 @0x51DF20
    auto place = [this](Button& b, int slotNormal, int x, int y) {
        int w = 0, h = 0;
        if (gfx::GpuTexture* t = GetAtlasSprite(slotNormal)) {
            w = static_cast<int>(t->Width());
            h = static_cast<int>(t->Height());
        }
        b.SetBounds(x, y, w, h);
    };
    place(restoreBtn_, 3086, x0, y0 - 37);  // EA 0x51E34F (`sub edx,25h` @0x51E347)
    place(enterBtn_,     16, x0, y0);       // EA 0x51DF4E / hit-test 0x51DF53
    place(createBtn_,    19, x0, y0 + 37);  // EA 0x51DFE6 / hit-test 0x51DFEB
    place(deleteBtn_,    22, x0, y0 + 74);  // EA 0x51E074 / hit-test 0x51E079
    place(backBtn_,     963, x0, y0 + 185); // EA 0x51E220 / hit-test 0x51E225
    place(quitBtn_,      25, x0, y0 + 222); // EA 0x51E2AA / hit-test 0x51E2AF
}

// Écran Formulaire de création : 5 paires -/+ (job/faction/visage/couleur/variant),
// nom saisi, Confirmer/Annuler — ANCRES RÉELLES (Scene_CharSelectRender 0x51CED0, panneau
// unk_8EA270 slot 40 @ (nWidth-0x14F, 0x49) EA 0x51E4A1/0x51E4B0). Les rangées -/+ sont
// alignées sur les VALEURS dessinées (panelY+{78,102,126,150,174}, pas +24), minus @panelX+115,
// plus @panelX+196 ; Confirmer @panelX+47 / Annuler @panelX+149, y=panelY+203 (slots 9/12).
void LoginScene::LayoutCreateForm() {
    const int panelX = screenW_ - 335, panelY = 73; // EA 0x51E4A7 (sub 0x14F) / 0x51E4B0 (0x49)
    // Dims des hit-rects -/+ = taille NATIVE des sprites slots 41/42 (unk_8EA304/unk_8EA398)
    // si chargeables, sinon gabarit nominal -> le clic colle exactement au sprite dessiné.
    int mw = 20, mh = 20, pw = 20, ph = 20;
    if (gfx::GpuTexture* t = GetAtlasSprite(41)) { if (t->Width() > 0) { mw = static_cast<int>(t->Width()); mh = static_cast<int>(t->Height()); } }
    if (gfx::GpuTexture* t = GetAtlasSprite(42)) { if (t->Width() > 0) { pw = static_cast<int>(t->Width()); ph = static_cast<int>(t->Height()); } }
    const int rowY[5] = { panelY + 78, panelY + 102, panelY + 126, panelY + 150, panelY + 174 };
    auto layoutRow = [&](int row, Button& minus, Button& plus) {
        minus.SetBounds(panelX + 115, rowY[row], mw, mh);
        plus.SetBounds (panelX + 196, rowY[row], pw, ph);
    };
    layoutRow(0, jobMinusBtn_, jobPlusBtn_);
    layoutRow(1, factionMinusBtn_, factionPlusBtn_);
    layoutRow(2, faceMinusBtn_, facePlusBtn_);
    layoutRow(3, hairMinusBtn_, hairPlusBtn_);
    layoutRow(4, variantMinusBtn_, variantPlusBtn_);
    // Zone de saisie du nom : valeur dessinée à (panelX+0x7F, panelY+0x35) EA 0x51E4F4-0x51E507.
    createNameBox_.SetBounds(panelX + 127, panelY + 53, 150, 20);
    // Confirmer (slot 9 unk_8E9084) @(panelX+47, panelY+203) ; Annuler (slot 12 unk_8E9240)
    // @(panelX+149, panelY+203).
    createConfirmBtn_.SetBounds(panelX + 47,  panelY + 203, kBtnW, kBtnH);
    createCancelBtn_.SetBounds (panelX + 149, panelY + 203, kBtnW, kBtnH);
}

// ÉTAT DE L'AUDIT CharSelect (mis à jour le 2026-07-16, front cs-render-2d) — les 4 écarts
// que l'audit du 2026-07-14 listait ici sont CLOS, sauf le n°1 :
//  1) DISPATCH À 3 BRANCHES (`this[+0xF588]` @0x51D4CB : ==1 Liste 0x51D4E6, ==2 Formulaire
//     0x51E4A1, sinon 3e branche 0x51ECC5 = overlay MOT DE PASSE SECONDAIRE / PIN, piloté
//     par dword_16692A4 / this[15375]) : TOUJOURS OUVERT. game::CharSelectScreen ne modélise
//     que {List, CreateForm} et game::CharSelectPinState est un état inerte — les deux
//     vivent dans Game/CharSelectFlow.h, HORS de ce front. La 3e branche est donc
//     inatteignable côté C++. TODO [0x51ECC5] (cf. CharSelectRenderUi2D).
//  2) APERÇU 3D : CLOS. Les 4 Char_RenderModel (0x51D361/0x51D3CC/0x51D429/0x51D480) sont
//     câblés dans CharSelectRenderPreview3D(), avec les membres persistants qui manquaient
//     (charMesh_/charModels_/charMotions_, cf. LoginScene.h) et la résolution d'apparence de
//     gfx::CharPreview3D. Reste UN câblage hors de ce front : SceneManager doit passer son
//     gfx::Renderer à Init() (wiring TODO CHARSELECT_3D).
//  3) HABILLAGE SPRITE DE LA LISTE : CLOS. Panneau 2012, DEUX cascades de tier à 4 paliers
//     (icône ET valeur), surbrillance 1657, niveau et nom centrés — cf. CharListRender().
//     Plus aucun FillRect ni gabarit inventé dans la scène 4.
//  4) TITRE / "Compte : %s" : CLOS — ils n'existaient pas dans le binaire et ont été retirés
//     (les 40 StrTable005_Get de la fonction sont TOUS dans 0x51E571-0x51FEF7, exclusivement
//     pour les VALEURS du formulaire de création). Les libellés des boutons sont GRAPHIQUES
//     (textures d'atlas) : aucun texte n'est dessiné par-dessus.
// ===========================================================================
// [A1/A13/A16] ORDRE DE PEINTRE — Scene_CharSelectRender 0x51CED0, EA par EA
// ===========================================================================
// Le binaire ouvre et ferme DEUX batches 2D distincts, avec la passe 3D ENTRE LES DEUX :
//
//   Gfx_Begin2D                                                            @0x51D22D
//     >>> if (this[1] == 0) { Gfx_End2D @0x51D243 ; Gfx_Present @0x51D24D ;
//                             jmp 0x520EC8 /* RETURN */ }                  <<< garde @0x51D238
//     Sprite2D_DrawScaled(atlas + 148*this[15713], 0, 0, nW/1024, nH/768)  @0x51D2AB
//   Gfx_End2D                                     <-- LE 2D SE FERME AVANT LA 3D  @0x51D2B5
//   --- PASSE 3D : 4x Char_RenderModel 0x527020 (0x51D361/0x51D3CC/0x51D429/0x51D480) ---
//   Gfx_Begin2D                                                            @0x51D48A
//     GetPhysicalCursorPos(&Point) @0x51D493 ; ScreenToClient(hWndParent) @0x51D4A4
//     var_1C = Point.x @0x51D4AD ; var_420 = Point.y @0x51D4B3   <- mouse de TOUS les HitTest
//     --- TOUT LE 2D UI (dispatch d'écran @0x51D4CB) ---
//   UI_RenderAllDialogs @0x520EAF ; Gfx_End2D @0x520EB9 ; Gfx_Present @0x520EC3
//
// [A1] GARDE D'INIT (@0x51D238, `cmp dword ptr [edx+4], 0`) : pendant le sous-état Init —
// soit les 30 frames que compte Scene_CharSelectUpdate avant de passer Actif (EA 0x51bde4
// `++this[2] >= 0x1Eu`) — le rendu ne dessine RIEN : End2D + Present + return sec. C'est
// UNE SECONDE d'écran VIDE, à reproduire telle quelle. L'ancien code affichait la liste
// immédiatement.
// ⚠ Sous-état Verrouillé (2) : Update est inerte MAIS le rendu dessine TOUT (image figée) —
//   la garde ne teste QUE 0, pas `!= 1`. C'est un modal gelé, pas un écran mort.
void LoginScene::CharSelectRender() {
    // Garde @0x51D238 : `[edx+4]` == this[1] == sous-état. SEUL 0 sort.
    if (charState_.subState == game::CharSelectSubState::Init) return; // frame vide (1 s)

    CharSelectRenderBg();        // Begin2D .. fond plein écran .. End2D  (0x51D22D-0x51D2B5)
    CharSelectRenderPreview3D(); // ENTRE les deux batches 2D            (0x51D2C0-0x51D485)
    CharSelectRenderUi2D();      // Begin2D .. UI .. End2D               (0x51D48A-0x520EB9)
}

// Fond plein écran — Begin2D/End2D À LUI SEUL (0x51D22D .. 0x51D2B5).
// Le slot est LU dans l'état (this[15713]), re-tiré à chaque Init par le flux : cf. [A4].
void LoginScene::CharSelectRenderBg() {
    if (!sprites_.Ready()) return;
    sprites_.Begin();
    DrawFullscreenBg(charState_.backgroundSlot); // 2383/2384/2385 (EA 0x51C261/70/7F)
    sprites_.End();
}

// [A16/E1/G1/G2] PASSE 3D — les 4 Char_RenderModel 0x527020, ENTRE Gfx_End2D (0x51D2B5) et
// Gfx_Begin2D (0x51D48A). Dispatch d'écran @0x51D2C0 (`v108 = this[15714]`) :
//   this[15714]==1 (Liste)    : garde `cmp [eax+0F58Ch], -1 ; jz 0x51D485` @0x51D2ED ->
//                               slot == -1 => LES DEUX APPELS SAUTÉS (aucun modèle).
//                               (pass=1,isCreate=0) @0x51D361 ; (pass=2,isCreate=0) @0x51D3CC
//                               record = &unk_1669380 + 0x2768*this[15715]
//   this[15714]==2 (Création) : (pass=1,isCreate=1) @0x51D429 ; (pass=2,isCreate=1) @0x51D480
//                               record = &dword_16709B8 (fiche d'aperçu)
//   sinon                     : jmp 0x51D485 (aucune 3D) — inatteignable ici, cf. UI2D.
//
// [E1] `pass ∈ {1,2}` OBLIGATOIRE (Model_Render @0x40EBD5 : `dec eax ; cmp eax,1 ; ja ->
// sortie`) et le paperdoll ENTIER est dessiné en passe 1 PUIS en passe 2 — surtout pas les
// deux passes par pièce. animState = 1 (Idle, `this[15718]=1` @0x51C363) ou 3 (Entering,
// @0x52516F) : JAMAIS 0. Le switch ~500 cas sur a8 est MORT ici (a8 = 0 EN DUR @0x52705F /
// @0x527544) -> non porté. Échelle réelle = 1.0 (le 20.0f de flt_7ED9F8 est le diamètre de
// la sphère de frustum, pas une échelle — cf. Gfx/CharPreview3D.h §5).
//
// [G1] Le TODO « race/gender pas encore exposés » de l'ancien code était FAUX : la race de
// la LISTE est la fiche +40 (`mov eax,[edx+28h]` @0x527536), le genre +44 — les deux sont
// lus par gfx::CharPreview3D::BuildFromRecord DIRECTEMENT dans la fiche brute
// net::g_CharRecords[slot], donc sans dépendre de game::CharSlotInfo.
// [G2] L'arme de l'aperçu de CRÉATION vient de la SCÈNE (this[15716] = variant,
// `mov edx,[ecx+0F590h]` @0x5271B8), PAS de record+216 (qui n'est écrit qu'à la
// confirmation) : d'où `choices.variant = createForm.variant`.
void LoginScene::CharSelectRenderPreview3D() {
    if (!charPreviewReady_ || !charModels_ || !charMotions_) return; // cf. wiring CHARSELECT_3D

    // Pose = les 5 derniers arguments de Char_RenderModel (this[15717..15724]).
    gfx::CharPreviewPose pose;
    pose.motion    = charState_.previewMotionIndex;                   // this[15717] (+0xF594)
    pose.animState = static_cast<int32_t>(charState_.previewMotion);  // this[15718] (+0xF598)
    pose.animTime  = charState_.previewElapsed;                       // this[15719] (+0xF59C)
    pose.pos[0]    = charState_.previewPos[0];                        // this[15720..22]
    pose.pos[1]    = charState_.previewPos[1];
    pose.pos[2]    = charState_.previewPos[2];
    pose.yawDeg    = charState_.previewRot[1];                        // yaw = this[15724] (+0xF5B0)

    gfx::CharPreviewResult r;
    if (charState_.screen == game::CharSelectScreen::List) {
        // Garde @0x51D2ED — AVANT toute résolution : slot == -1 => rien du tout.
        const int32_t slot = charState_.selectedSlot;
        if (slot < 0 || slot >= net::kCharRecordCount) return;
        // record = &unk_1669380 + 0x2768*slot == net::g_CharRecords[slot] (fiche BRUTE).
        r = gfx::CharPreview3D::BuildFromRecord(*charModels_, *charMotions_,
                                                net::g_CharRecords[slot], pose);
    } else {
        // Fiche d'aperçu dword_16709B8 : reconstruite depuis le formulaire (mêmes champs,
        // cf. Game/CharSelectFlow.h::CharCreateForm et l'inventaire exhaustif §6.2).
        gfx::CharPreviewChoices c;
        c.job       = charState_.createForm.job;       // +36 — index de RACE de cette branche (@0x527051)
        c.gender    = charState_.createForm.faction;   // +44 — genre (nom historique conservé)
        c.face      = charState_.createForm.face;      // +48
        c.hairColor = charState_.createForm.hairColor; // +52
        c.variant   = charState_.createForm.variant;   // this[15716] (SCÈNE) — @0x5271B8
        r = gfx::CharPreview3D::BuildFromChoices(*charModels_, *charMotions_, c, pose);
    }
    if (!r.valid) return;

    // Vue = D3DXMatrixLookAtLH(eye(0,5,-28), target(0,10,0), up(0,1,0)) — l'opération exacte
    // de Gfx_BeginFrame @0x6A2352 sur le bloc caméra écrit à l'Init de la scène
    // (0x800130..0x800144 @0x51BDED-0x51BE21, recopié dans g_GxdRenderer @0x51BE2C-0x51BE65).
    // [A14] Écrire ces 6 floats « à chaque Init sur les DEUX singletons » est, côté C++, sans
    // effet observable : ce sont des CONSTANTES (vérifiées à l'octet, cf. CharPreview3D.h §5)
    // et rien d'autre ne les lit. On les pose donc directement ici, au point de consommation.
    // ⚠ La PROJECTION n'est PAS touchée par la scène 4 (les seuls memcpy de matrices de
    // Scene_CharSelectRender, @0x51CF32-0x51CF4D, copient le WORLD 0x800244 et la VUE
    // 0x800154 vers le GXD — jamais la projection) : on réutilise celle, APPLICATIVE, posée
    // au boot par Gfx_InitDevice 0x69B9B0 (D3DXMatrixPerspectiveFovLH @0x69BFC6). Elle est
    // déjà portée par gfx::Camera (fovY 45° = kFovDegDefault ; near/far = g_GxdRenderer+60/
    // +64) — on l'instancie par DÉFAUT plutôt que de recopier des littéraux ici, pour ne pas
    // dupliquer une vérité qui vit déjà dans Gfx/Camera.h.
    D3DXMATRIX view, proj;
    gfx::CharPreview3D::BuildViewMatrix(view);
    const gfx::Camera appCam; // valeurs par défaut = projection applicative (cf. Gfx/Camera.h)
    appCam.BuildProjMatrix(proj, static_cast<float>(screenW_) / static_cast<float>(screenH_));
    charMesh_.SetCamera(view, proj);

    // Le batch de fond CharSelectRenderBg() (chemin 2D sprite, @0x51D22D-0x51D2B5) a tourné
    // JUSTE AVANT sur le MÊME device partagé et y a rebindé ses propres shaders/états. Le cache
    // de bind LOCAL de charMesh_ (currentPass_) ne voit pas ce changement -> dès la 2e frame il
    // resterait à kPass_SkinnedLit et sauterait le rebind, dessinant le skinné avec le shader 2D
    // périmé (cf. bandeau Gfx/MeshRenderer.h). Fidèle au binaire, où tout passe par le même
    // g_CurrentShaderPass 0x194591C (venir d'une passe 2D force donc le rebind). On invalide.
    charMesh_.InvalidateShaderBindingCache();

    // DEUX rendus COMPLETS du paperdoll : passe 1 puis passe 2 (les 4 sites d'appel du
    // binaire sont deux paires adjacentes, jamais entrelacées par pièce).
    gfx::CharPreview3D::Render(charMesh_, r, pose, gfx::MeshRenderer::kDrawPass_Opaque); // pass=1
    gfx::CharPreview3D::Render(charMesh_, r, pose, gfx::MeshRenderer::kDrawPass_Blend);  // pass=2
}

// [A13] Second batch 2D (0x51D48A) : le survol est recalculé PAR FRAME depuis la position
// PHYSIQUE LIVE du curseur, lue APRÈS Gfx_Begin2D et AVANT le dispatch d'écran (@0x51D4CB).
// Aucun index de survol n'est mis en cache dans Update — toute mise en cache diverge.
void LoginScene::CharSelectRenderUi2D() {
    if (charState_.screen == game::CharSelectScreen::CreateForm) CreateFormRender(); // @0x51E4A1
    else                                                          CharListRender();  // @0x51D4E6
    // 3e branche du dispatch (@0x51D4E1 -> 0x51ECC5) = overlay MOT DE PASSE SECONDAIRE / PIN.
    // INATTEIGNABLE ici : game::CharSelectScreen ne modélise que {List, CreateForm} et
    // game::CharSelectPinState est un état inerte (opcodes 13/14/15 non câblés).
    // TODO [0x51ECC5] : porter l'assistant PIN (this[15375]/15376/15377, pavé permuté
    // this[15385..15394], sprites unk_93EF4C) — hors périmètre de ce front.
    RenderMsgBox(); // MsgBox modal partagé (suppression type 2) en queue, cf. host.ShowDeleteConfirm
}

// ===========================================================================
// [D1/D2] ÉCRAN LISTE (this[15714] == 1) — Scene_CharSelectRender @0x51D4E6-0x51D7BF
// ===========================================================================
// Origine, LITTÉRALE : X = nWidth - 0xC2 (194) @0x51D4EC ; Y0 = 0x13 (19) @0x51D4F5.
// Panneau de fond : Sprite2D_Draw(unk_931680, X, 19) @0x51D50F — slot 2012
//   ((0x931680 - 0x8E8B50)/148 = 2012, base+stride re-vérifiés ce front).
// Boucle `for (i=0;i<3;++i)` : `cmp [ebp+var_20], 3 ; jge` @0x51D526 -> kMaxCharSlots = 3.
//
// [D1] 🔴 SLOT VIDE = RIEN DESSINÉ. Première ligne du corps de boucle :
//        `Crt_Strcmp(&unk_1669394 + 0x2768*i, "") ; test eax,eax ; jnz` @0x51D545/0x51D54F
//      -> si le nom est vide, `jmp loc_51D51D` = CONTINUE : le `continue` saute TOUT le
//      corps (aucun sprite, aucun texte, aucun cadre). Seul le panneau 2012 reste visible
//      derrière. NE PAS « améliorer » en dessinant un emplacement vide.
//
// Par slot OCCUPÉ, QUATRE éléments (dans cet ordre) :
//  1. Icône de tier @ (X, 19 + 44*i + 0x22) = (X, 53 + 44*i)
//     (`imul eax, 2Ch` @0x51D590 puis `lea edx, [ecx+eax+22h]` @0x51D599)
//  2. Surbrillance de sélection unk_924944 = slot 1657 @ (X + 0x19, 19 + 44*i + 0x32)
//     = (X+25, 69 + 44*i), UNIQUEMENT si `i == this[15715]` (`cmp edx,[ecx+0F58Ch]` @0x51D618)
//  3. NIVEAU, nombre CENTRÉ sur x = X + 0x36 (54), y = 19 + 44*i + 0x33 (70+44*i) @0x51D715-0x51D756
//  4. NOM, texte CENTRÉ sur x = X + 0x76 (118), même y @0x51D779-0x51D7BA
//
// 🔴 DEUX CASCADES DE TIER À 4 PALIERS — même prédicat, SPRITE et VALEUR différents.
//    ORDRE EXACT (si / sinon-si), désassemblé ce front :
//      icône @0x51D556-0x51D60A                     |  valeur @0x51D642-0x51D712
//      rec[+5708] >= 1 -> unk_985A1C (4343) @0x51D605 | -> "%d" rec[+5708]        @0x51D6FA
//      rec[+60]   >= 1 -> unk_94B504 (2729) @0x51D5E4 | -> "%d" rec[+60]          @0x51D6D4
//      rec[+56]   >= 113 -> unk_9319F8 (2018) @0x51D5C3 | -> "%d" rec[+56] - 112  @0x51D6B1
//      sinon           -> unk_931714 (2013) @0x51D5A2 | -> "%d" rec[+56]          @0x51D685
//    (113 = `cmp ..., 71h` @0x51D584 ; 112 = `sub edx, 70h` @0x51D6B1 — vérifiés.)
//    ⚠ L'ancien code ne portait que 2 des 4 paliers (2018/2013) et rendait la MÊME valeur
//      aux deux emplacements de texte : c'était faux sur les deux plans. Le 2e emplacement
//      n'est PAS un second niveau — c'est le NOM (Crt_StringInit(&String, name) @0x51D771).
//
// POLITIQUE ZÉRO REPLI : un sprite qui ne charge pas -> rien dessiné (fidèle à
// Sprite2D_Draw/EnsureLoaded, qui échoue en silence).
//
// ⚠ DIVERGENCE STRUCTURELLE ASSUMÉE ET MESURÉE (ordre de peintre) : le binaire n'a qu'UNE
// boucle (init `mov [ebp+var_20],0` EA 0x51D514 ; sortie `cmp [ebp+var_20],3 ; jge
// loc_51D7C4` EA 0x51D526/0x51D52A ; arête arrière `jmp loc_51D51D` EA 0x51D7BF) qui
// ENTRELACE sprites et texte PAR FICHE : icone_0, surbrillance_0, niveau_0, nom_0, icone_1…
// On la scinde ici en deux passes (batch sprite puis batch font). L'ordre INTRA-fiche
// (surbrillance_i avant texte_i) est PRÉSERVÉ — c'est le seul recouvrement possible au pas
// de 44 px — mais l'ordre INTER-fiches diverge (le binaire peint texte_0 AVANT icone_1, on
// peint icone_1 avant texte_0). Sans conséquence tant que les sprites tiennent dans le pas
// de 44 px (icônes y=53+44i, surbrillance y=69+44i, texte y=70+44i) ; les dimensions n'étant
// pas déterminables en statique (spec §13.1), le recouvrement n'est ni prouvé ni prouvable
// ici. À re-trancher si un sprite de liste dépasse ce pas.
void LoginScene::CharListRender() {
    LayoutCharSelect();
    // [A13] Position PHYSIQUE LIVE du curseur (GetPhysicalCursorPos @0x51D493 +
    // ScreenToClient(hWndParent) @0x51D4A4) — recalculée à CHAQUE frame de rendu.
    const POINT mp = CharSelectCursorClient();
    createBtn_.OnMouseMove(mp.x, mp.y);
    deleteBtn_.OnMouseMove(mp.x, mp.y);
    enterBtn_.OnMouseMove(mp.x, mp.y);
    backBtn_.OnMouseMove(mp.x, mp.y);
    restoreBtn_.OnMouseMove(mp.x, mp.y);

    constexpr int kPanelTop = 19;         // var_418 = 0x13 @0x51D4F5
    const int panneauX = screenW_ - 194;  // nWidth - 0xC2 @0x51D4EC

    if (sprites_.Ready()) {
        sprites_.Begin();

        // Panneau de la liste — slot 2012 (unk_931680) @ (X, 19), blit natif @0x51D50F.
        if (gfx::GpuTexture* t = GetAtlasSprite(2012))
            sprites_.DrawSprite(t->Handle(), nullptr, panneauX, kPanelTop, gfx::kSpriteWhite);

        for (int i = 0; i < game::kMaxCharSlots; ++i) {
            const game::CharSlotInfo& slot = charState_.slots[static_cast<size_t>(i)];
            if (!slot.occupied) continue;          // [D1] Crt_Strcmp(name,"")==0 -> continue @0x51D54F

            const int yIcon = kPanelTop + 44 * i + 0x22; // 53 + 44*i  @0x51D599
            // 1. Icône de tier — cascade à 4 paliers, ORDRE EXACT.
            const int32_t t5708 = CharRecI32(i, 5708); // dword_166A9CC[2768h*i] @0x51D55C
            const int32_t t60   = CharRecI32(i,   60); // dword_16693BC[2768h*i] @0x51D572
            int tierSlot;
            if      (t5708 >= 1)       tierSlot = 4343; // unk_985A1C @0x51D605
            else if (t60   >= 1)       tierSlot = 2729; // unk_94B504 @0x51D5E4
            else if (slot.power >= 113) tierSlot = 2018; // unk_9319F8 @0x51D5C3 (`cmp ..,71h` @0x51D584)
            else                        tierSlot = 2013; // unk_931714 @0x51D5A2
            if (gfx::GpuTexture* t = GetAtlasSprite(tierSlot))
                sprites_.DrawSprite(t->Handle(), nullptr, panneauX, yIcon, gfx::kSpriteWhite);

            // 2. Surbrillance de sélection — slot 1657 (unk_924944) @ (X+25, 69+44*i).
            if (i == charState_.selectedSlot) {                     // `cmp edx,[ecx+0F58Ch]` @0x51D618
                if (gfx::GpuTexture* t = GetAtlasSprite(1657))
                    sprites_.DrawSprite(t->Handle(), nullptr,
                                        panneauX + 0x19, kPanelTop + 44 * i + 0x32,
                                        gfx::kSpriteWhite);         // (+25, +50) @0x51D634/0x51D62C
            }
        }
        sprites_.End();
    }

    if (font_.Ready()) {
        font_.BeginBatch();
        for (int i = 0; i < game::kMaxCharSlots; ++i) {
            const game::CharSlotInfo& slot = charState_.slots[static_cast<size_t>(i)];
            if (!slot.occupied) continue;                 // même `continue` @0x51D54F

            const int yText = kPanelTop + 44 * i + 0x33;  // 70 + 44*i  @0x51D723

            // 3. NIVEAU — MÊME cascade à 4 paliers que l'icône, mais sur la VALEUR.
            const int32_t v5708 = CharRecI32(i, 5708); // @0x51D64B
            const int32_t v60   = CharRecI32(i,   60); // @0x51D661
            char num[16];
            if      (v5708 >= 1) std::snprintf(num, sizeof(num), "%d", v5708);          // @0x51D6FA
            else if (v60   >= 1) std::snprintf(num, sizeof(num), "%d", v60);            // @0x51D6D4
            else if (slot.power >= 113)                               // `cmp ..., 71h` @0x51D673
                std::snprintf(num, sizeof(num), "%d", slot.power - 112);   // `sub edx, 70h` @0x51D6B1
            else
                std::snprintf(num, sizeof(num), "%d", slot.power);         // @0x51D685
            DrawNumberCentered(num, panneauX + 0x36, yText);          // centre x = X+54 @0x51D72B

            // 4. NOM — texte centré (Crt_StringInit(&String, &unk_1669394+0x2768*i) @0x51D771).
            DrawNumberCentered(slot.name.c_str(), panneauX + 0x76, yText); // x = X+118 @0x51D78F
        }
        font_.EndBatch();
    }

    // Panneau de détail GAUCHE, puis colonne de boutons — ORDRE DU BINAIRE
    // (0x51D7C4 -> 0x51DF0D -> 0x51E4A1) : le détail est peint AVANT les boutons.
    CharDetailPanelRender();
    CharButtonColumnRender();
}

// ===========================================================================
// [B2] PANNEAU DE DÉTAIL GAUCHE — @0x51D7C4-0x51DF0D, origine ABSOLUE (15, 19)
// ===========================================================================
// 🔴 GARDE ABSENTE DE LA SPEC CONSOLIDÉE §8.3, re-prouvée ici par désassemblage :
//    `cmp dword ptr [ecx+0F58Ch], 0FFFFFFFFh ; jz loc_51DF0D` @0x51D7CA
//    -> AUCUN slot sélectionné => le panneau ENTIER est sauté (on saute directement à la
//       colonne de boutons). Le panneau n'est donc PAS un décor permanent.
// Origine : var_4 = 0Fh (15) @0x51D7D7 ; var_418 = 13h (19) @0x51D7DE.
// Indexé par this[15715] (`imul eax, 2768h` @0x51D7F4) — ce n'est PAS la liste des 3 slots.
// AUCUN hit-test dans tout le bloc : purement décoratif.
//
// Fond : MÊME cascade à 4 paliers que la liste, sprites DIFFÉRENTS (@0x51D7FA-0x51D88E) :
//   rec[+5708] >= 1  -> unk_985988 = slot 4342 @0x51D889
//   rec[+60]   >= 1  -> unk_94B3DC = slot 2727 @0x51D872
//   rec[+56]   >= 113 -> unk_90E6E0 = slot 1044 @0x51D85B
//   sinon            -> unk_8E93FC = slot   15 @0x51D844
// Puis, tous centrés via UI_MeasureNumberText/UI_DrawNumberValue :
//   NOM   @ x = 15 + 0x77 (134), y = 19 + 0x20 (51)   @0x51D8CA / 0x51D8C3
//   NIVEAU (MÊME cascade 4 paliers) @ x = 15 + 0x75 (132), y = 19 + 0x35 (72) @0x51DA1B/0x51DA14
//   puis 11 champs @ x = 132, y = 19 + 0x48 + 19*k = 91, 110, ..., 281 (pas 19)
//     (1er champ vérifié à l'octet : `add eax, 48h` @0x51DA80 ; `add esi, 75h` @0x51DA87 ;
//      source `mov ecx, ds:dword_1669390[eax]` @0x51DA5D = fiche +16.)
void LoginScene::CharDetailPanelRender() {
    // Garde @0x51D7CA — AVANT tout dessin.
    const int32_t sel = charState_.selectedSlot;
    if (sel < 0 || sel >= game::kMaxCharSlots) return;
    const game::CharSlotInfo& rec = charState_.slots[static_cast<size_t>(sel)];
    const int32_t d5708 = CharRecI32(sel, 5708); // dword_166A9CC[2768h*sel] @0x51D7FA
    const int32_t d60   = CharRecI32(sel,   60); // dword_16693BC[2768h*sel] @0x51D815

    constexpr int kX = 15; // var_4   = 0Fh @0x51D7D7
    constexpr int kY = 19; // var_418 = 13h @0x51D7DE

    if (sprites_.Ready()) {
        sprites_.Begin();
        int bgSlot;
        if      (d5708 >= 1)       bgSlot = 4342; // unk_985988 @0x51D889
        else if (d60   >= 1)       bgSlot = 2727; // unk_94B3DC @0x51D872
        else if (rec.power >= 113) bgSlot = 1044; // unk_90E6E0 @0x51D85B (`cmp ..,71h` @0x51D830)
        else                       bgSlot = 15;   // unk_8E93FC @0x51D844
        if (gfx::GpuTexture* t = GetAtlasSprite(bgSlot))
            sprites_.DrawSprite(t->Handle(), nullptr, kX, kY, gfx::kSpriteWhite);
        sprites_.End();
    }

    if (!font_.Ready()) return;
    font_.BeginBatch();

    // NOM du personnage sélectionné — centré @ (15+119, 19+32) = (134, 51).
    DrawNumberCentered(rec.name.c_str(), kX + 0x77, kY + 0x20); // @0x51D8CA / 0x51D8C3

    char buf[32];
    // NIVEAU — MÊME cascade à 4 paliers (@0x51D90C-0x51DA09), centré @ (15+117, 19+53).
    if      (d5708 >= 1)       std::snprintf(buf, sizeof(buf), "%d", d5708);
    else if (d60   >= 1)       std::snprintf(buf, sizeof(buf), "%d", d60);
    else if (rec.power >= 113) std::snprintf(buf, sizeof(buf), "%d", rec.power - 112);
    else                       std::snprintf(buf, sizeof(buf), "%d", rec.power);
    DrawNumberCentered(buf, kX + 0x75, kY + 0x35); // (132, 72) @0x51DA1B / 0x51DA14

    // Les 11 champs numériques : MÊME x (132), y = 91 + 19*k.
    // 🔴 SÉMANTIQUE NON PROUVÉE pour la moitié d'entre eux — les OFFSETS et les POSITIONS
    // le sont (1er champ vérifié à l'octet ci-dessus ; le reste suit le même motif au pas
    // de 19). game::CharSlotInfo (Game/CharSelectFlow.h, HORS de ce front) n'expose AUCUN
    // de ces champs : ils vivent dans la fiche brute net::g_CharRecords[sel], qu'on relit
    // directement — même source que le binaire (dword_1669390[2768h*slot] & co).
    // TODO [0x51DA5D..0x51DEFF] : nommer +5484/+5488 (écrits par Pkt_CharStatDelta 0x46712C/
    // 0x467101), +5432 (chaîne, « nom de clan » = INFÉRENCE non prouvée), +5708 (« 2e palier
    // de renaissance » = inférence par symétrie). Ne PAS inventer de libellé : le binaire
    // n'en dessine aucun (ils sont peints dans la texture du panneau).
    if (sel >= net::kCharRecordCount) { font_.EndBatch(); return; }
    // Offsets dans la fiche, dans l'ordre de dessin (y croissant). -1 = cas particulier
    // traité dans la boucle (chaîne, ou champ packé lu deux fois).
    constexpr int kDetailFieldOffsets[11] = {
        16,   // y=91  — g_Currency 0x1673180 (monnaie/or)   `mov ecx, ds:dword_1669390[eax]` @0x51DA5D
        5484, // y=110 — dword_16746DC, sémantique INCONNUE (écrit par Pkt_CharStatDelta 0x46712C)
        5488, // y=129 — dword_16746E0, sémantique INCONNUE (écrit par Pkt_CharStatDelta 0x467101)
        100,  // y=148 — g_SkillPointPool 0x16731D4
        -1,   // y=167 — +5432 (dword_16746A8) : CHAÎNE (« nom de clan/équipe » = INFÉRENCE)
        88,   // y=186 — g_MeridianPts_RatingMin
        92,   // y=205 — g_MeridianPts_RatingMax
        5568, // y=224 — g_MeridianPts_ExtAtk
        5572, // y=243 — g_MeridianPts_Defense
        -1,   // y=262 — +9408 (g_MeridianHpMpPacked 0x1675630) / 1000
        -1,   // y=281 — +9408 % 1000
    };
    for (int k = 0; k < 11; ++k) {
        const int y = kY + 0x48 + 19 * k; // 91 + 19*k (`add eax, 48h` @0x51DA80 pour k=0)
        if (k == 4) {                     // CHAÎNE : lecture directe dans la fiche brute
            char s[64] = {0};
            std::memcpy(s, net::g_CharRecords[sel] + 5432, sizeof(s) - 1);
            DrawNumberCentered(s, kX + 0x75, y);
            continue;
        }
        int32_t v;
        if (k == 9)       v = CharRecI32(sel, 9408) / 1000; // HP/MP packé : quotient
        else if (k == 10) v = CharRecI32(sel, 9408) % 1000; // HP/MP packé : reste
        else              v = CharRecI32(sel, kDetailFieldOffsets[k]);
        std::snprintf(buf, sizeof(buf), "%d", v);
        DrawNumberCentered(buf, kX + 0x75, y);
    }
    font_.EndBatch();
}

// ===========================================================================
// [C2/C1] COLONNE DES 10 BOUTONS — @0x51DF0D-0x51E4A1
// ===========================================================================
// Origine : x0 = nWidth - 0x8C (140) @0x51DF12 ; y0 = nHeight - 0x12D (301) @0x51DF20.
// var_4 et var_418 sont écrits UNE SEULE FOIS ici (@0x51DF17 / @0x51DF26) et JAMAIS
// réécrits dans tout le bloc : les 8 premiers boutons partagent x0 et ne diffèrent que par
// un delta sur y0 ; les DEUX derniers sont à des positions ABSOLUES.
//
// 🔴 TABLE RE-PROUVÉE À L'OCTET CE FRONT (get_bytes sur chaque `add/sub`) — la table §8.2 de
//    la spec consolidée est FAUSSE sur plusieurs lignes (elle plaçait le bouton 3086 à
//    y0+259 et donnait des gardes erronées). Décodage effectif :
//   dy      | slots N/H/P    | latch    | garde serveur (this[15374], +0xF038) | EA sprite
//   --------+----------------+----------+--------------------------------------+----------
//   -37     | 3086/3087/3088 | this[10] | == 50 OU == 40 (cf. 🔴 ci-dessous)   | 0x51E34F
//   (8e peint, APRÈS le slot 25)        |  (`sub edx, 25h` = 83 EA 25 @0x51E347)|
//     0     | 16/17/18       | this[3]  | —                                    | 0x51DF4E
//   +37     | 19/20/21       | this[4]  | != 60 ET != 50 (@0x51DFA5 / 0x51DFB8) | 0x51DFE6
//           |                |          |  (`add eax, 25h` = 83 C0 25 @0x51DFDE)|
//   +74     | 22/23/24       | this[5]  | != 60  (`cmp ...,3Ch ; jz` @0x51E046) | 0x51E074
//           |                |          |  (`add eax, 4Ah` = 83 C0 4A @0x51E06C)|
//   +111    | 1812/1813/1814 | this[6]  | != 60  (@0x51E0D4)                    | 0x51E102
//           |                |          |  (`add eax, 6Fh` = 83 C0 6F @0x51E0FA)|
//   +148    | 1925/1926/1927 | this[7]  | != 60  (@0x51E162)                    | 0x51E196
//           |                |          |  (`add eax, 94h` = 05 94 00 00 00)    |
//   +185    | 963/964/965    | this[8]  | —      (`cmp [edx+20h],0` @0x51E1FE)  | 0x51E220
//   +222    | 25/26/27       | this[9]  | —      (`cmp [ecx+24h],0` @0x51E288)  | 0x51E2AA
//   ABS(15,332) | 3166/3167/3168 | this[11] | != 60 ET != 40 (@0x51E3B5/0x51E3BE) | 0x51E3E5
//   ABS(15,404) | 3192/3193/3194 | this[12] | == 40 SEULEMENT (@0x51E430)         | 0x51E457
//   (positions absolues vérifiées à l'octet : `push 14Ch ; push 0Fh` @0x51E3DE/0x51E3E3 et
//    `push 194h ; push 0Fh` = 68 94 01 00 00 / 6A 0F @0x51E450 -> (15,332) et (15,404).)
//
// 🔴 DOUBLE-KILL DU BOUTON #9 (3192) : il n'est RENDU que si serverIdx == 40 (@0x51E430)
//    alors que son handler ABANDONNE justement si serverIdx == 40 (notice 110 @0x525D81) —
//    et même en forçant, la garde `var_434` (invariant 0, cf. Game/CharSelectFlow.h [H1])
//    mène à la notice 2248. CHAÎNE ENTIÈREMENT MORTE : on le DESSINE (fidélité du rendu)
//    mais on ne câble AUCUN clic dessus. Idem pour l'opcode 24.
//
// GARDE SERVEUR : this[15374] (+0xF038) n'est réifié NULLE PART côté C++ (l'index « à plat »
// 40/50/60 n'est PAS game::ServerSelectState::selectedServer — ne rien deviner, cf. règle #8).
// host.GetServerIndex est donc nullptr -> srv == 0 en permanence, et c'est le MÊME état que
// le binaire à srv==0 : les gardes « != » laissent passer (7 boutons dessinés), les gardes
// « == » bloquent (3086 gardé `==50 || ==40` et 3192 gardé `==40` ne sont JAMAIS dessinés —
// sans conséquence, leurs chaînes étant mortes). TODO [0x51c09d / 0x51c13f] : réifier
// this[15374] en décompilant son écriture, puis assigner charHost_.GetServerIndex.
void LoginScene::CharButtonColumnRender() {
    if (!sprites_.Ready()) return;

    const int x0 = screenW_ - 0x8C;  // nWidth  - 140 @0x51DF12
    const int y0 = screenH_ - 0x12D; // nHeight - 301 @0x51DF20
    const POINT mp = CharSelectCursorClient(); // survol live, par frame (@0x51D493)
    const int32_t srv = charHost_.GetServerIndex ? charHost_.GetServerIndex() : 0;

    sprites_.Begin();
    // dy = 0 : ENTRER (16/17/18), aucune garde.
    DrawTriStateSprite(16, x0, y0, enterBtn_.Pressed(), mp.x, mp.y);
    // dy = +37 : CRÉER (19/20/21), garde != 60 ET != 50.
    if (srv != 60 && srv != 50)
        DrawTriStateSprite(19, x0, y0 + 37, createBtn_.Pressed(), mp.x, mp.y);
    // dy = +74 : SUPPRIMER (22/23/24), garde != 60.
    if (srv != 60)
        DrawTriStateSprite(22, x0, y0 + 74, deleteBtn_.Pressed(), mp.x, mp.y);
    // dy = +111 : slots 1812/1813/1814, garde != 60. Latch this[6] non porté (aucun membre
    // Button) -> dessiné à l'état normal/survol uniquement. TODO [0x525544] : action non
    // portée (RENOMMER, ticket item 1133 — cf. Net/CharSelectPackets.h::CharItemAction).
    if (srv != 60)
        DrawTriStateSprite(1812, x0, y0 + 111, /*latched=*/false, mp.x, mp.y);
    // dy = +148 : slots 1925/1926/1927, garde != 60. Latch this[7] non porté.
    // TODO [0x52B730] : action non portée (Net_ReqStorageList, op25).
    if (srv != 60)
        DrawTriStateSprite(1925, x0, y0 + 148, /*latched=*/false, mp.x, mp.y);
    // dy = +185 : RETOUR (963/964/965), aucune garde serveur.
    DrawTriStateSprite(963, x0, y0 + 185, backBtn_.Pressed(), mp.x, mp.y);
    // dy = +222 : QUITTER (25/26/27), aucune garde serveur.
    DrawTriStateSprite(25, x0, y0 + 222, quitBtn_.Pressed(), mp.x, mp.y);
    // dy = -37 : RESTAURER (3086/3087/3088) — 8e dans l'ordre du binaire (blit EA 0x51E34F,
    // APRÈS le slot 25 EA 0x51E2AA), et NON 1er : la colonne est peinte 16, 19, 22, 1812,
    // 1925, 963, 25, PUIS 3086, 3166, 3192 (adresses croissantes 0x51DF4E -> 0x51E457).
    // 🔴 GARDE RE-DÉSASSEMBLÉE CE FRONT (le sens était INVERSÉ, et la disjonction ==40 perdue) :
    //   `cmp dword ptr [eax+0F038h], 32h` @0x51E312 ; `jz short loc_51E32A` @0x51E319  (==50 -> DESSINE)
    //   `cmp dword ptr [ecx+0F038h], 28h` @0x51E321 ; `jnz short loc_51E3A9` @0x51E328 (!=40 -> SAUTE)
    // => dessiner SSI (srv == 50 || srv == 40). GetServerIndex n'étant pas réifié (srv==0,
    // cf. TODO ci-dessus), le binaire ne dessine JAMAIS ce bouton — l'ancien `srv != 50` le
    // peignait à chaque frame.
    if (srv == 50 || srv == 40)
        DrawTriStateSprite(3086, x0, y0 - 37, restoreBtn_.Pressed(), mp.x, mp.y);
    // ABSOLU (15,332) : slots 3166/3167/3168, garde != 60 ET != 40. Rôle NON PROUVÉ
    // (spec §13.13) -> aucun clic câblé. TODO [0x525CA4].
    if (srv != 60 && srv != 40)
        DrawTriStateSprite(3166, 15, 332, /*latched=*/false, mp.x, mp.y);
    // ABSOLU (15,404) : slots 3192/3193/3194, rendu SEULEMENT si serverIdx == 40.
    // 🔴 CHAÎNE MORTE (double-kill ci-dessus) : dessiné, jamais cliquable.
    if (srv == 40)
        DrawTriStateSprite(3192, 15, 404, /*latched=*/false, mp.x, mp.y);
    sprites_.End();
}

// [C1] Motif canonique du bouton 3 ÉTATS — slots CONSÉCUTIFS (n, n+1, n+2), sans exception
// sur les 10 boutons. EA de référence (ENTRER) : latch @0x51DF32, hit-test @0x51DF53,
// survol @0x51DF83, normal @0x51DF6C, pressé @0x51DF9A.
// ⚠ LE HIT-TEST PORTE SUR LE SPRITE NORMAL (base), jamais sur celui qui est peint.
// ⚠ EFFET DE BORD À NE PAS OMETTRE : Sprite2D_Draw ET Sprite2D_HitTest appellent tous deux
//   Sprite2D_EnsureLoaded et écrivent `spr+144 = g_GameTimeSec` (@0x4D6B4F / @0x4D6C81) —
//   même un simple survol « touche » le sprite pour le LRU d'atlas. Côté C++, GetAtlasSprite
//   fait exactement cela : il charge paresseusement et garde l'entrée résidente dans
//   atlasCache_ (pas d'éviction) — l'effet observable (le sprite reste chargé) est identique.
void LoginScene::DrawTriStateSprite(int slotNormal, int x, int y, bool latched,
                                    int mouseX, int mouseY) {
    int slot;
    if (latched)                                            slot = slotNormal + 2; // pressé
    else if (AtlasHitTest(slotNormal, x, y, mouseX, mouseY)) slot = slotNormal + 1; // survol
    else                                                     slot = slotNormal;     // normal
    if (gfx::GpuTexture* t = GetAtlasSprite(slot))
        sprites_.DrawSprite(t->Handle(), nullptr, x, y, gfx::kSpriteWhite);
}

// Sprite2D_HitTest 0x4D6C50 : `ptX >= x && ptX < x + spr[+108] && ptY >= y && ptY < y + spr[+112]`
// (`a4 < *(_DWORD*)(this+108) + a2` @0x4D6CBC) — bornes >= gauche/haut et < droite/bas.
// spr+108/+112 = width/height, remplis par le chargement du .IMG : NON déterminables en
// statique (spec §13.1). On utilise donc les dimensions RÉELLES de la texture chargée —
// c'est la seule source exacte, et elle est disponible au runtime comme dans le binaire.
// Sprite non chargeable => aucun rect => false (le binaire, lui, testerait 0x0 : idem).
bool LoginScene::AtlasHitTest(int slotIndex, int x, int y, int mouseX, int mouseY) {
    gfx::GpuTexture* t = GetAtlasSprite(slotIndex);
    if (!t || t->Width() <= 0 || t->Height() <= 0) return false;
    const int w = static_cast<int>(t->Width());
    const int h = static_cast<int>(t->Height());
    return mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;
}

// UI_MeasureNumberText 0x53FCA0 puis UI_DrawNumberValue 0x53FCC0, sur la police bitmap
// unk_1685740. Centrage = idiome `movzx eax,ax ; cdq ; sub eax,edx ; sar eax,1 ; sub esi,eax`
// (@0x51D73F-0x51D747) = `x = centerX - largeur/2` (division signée par 2).
// TODO [0x53FCC0] : le dernier argument vaut LITTÉRALEMENT 1 sur tous les sites (certain),
// mais sa signification est INCONNUE (couleur ? contour ? police ?) — non porté.
// TODO [0x1685740] : l'objet police bitmap n'est pas identifié ; on rend via gfx::Font, dont
// les métriques diffèrent -> le centrage n'est PAS garanti au pixel (spec §13.10).
void LoginScene::DrawNumberCentered(const char* text, int centerX, int y) {
    if (!text || !*text || !font_.Ready()) return;
    font_.DrawTextAt(text, centerX - font_.MeasureText(text) / 2, y, kColText);
}

// GetPhysicalCursorPos(&Point) @0x51D493 — PAS GetCursorPos (que CursorClient() utilise pour
// les autres scènes) — puis ScreenToClient(hWndParent 0x815184, &Point) @0x51D4A4.
// GetPhysicalCursorPos n'est exposé par les en-têtes du SDK que sous _WIN32_WINNT >= 0x0600 :
// on le résout dynamiquement dans user32 pour ne dépendre d'aucun réglage de la solution,
// avec repli sur GetCursorPos (identique hors DPI virtualisé).
POINT LoginScene::CharSelectCursorClient() const {
    POINT p{0, 0};
    using PFN = BOOL (WINAPI*)(LPPOINT);
    static PFN pGetPhysical = []() -> PFN {
        if (HMODULE u = GetModuleHandleA("user32.dll"))
            return reinterpret_cast<PFN>(
                reinterpret_cast<void*>(GetProcAddress(u, "GetPhysicalCursorPos")));
        return nullptr;
    }();
    const BOOL ok = pGetPhysical ? pGetPhysical(&p) : GetCursorPos(&p);
    if (ok && notifyWnd_) ScreenToClient(notifyWnd_, &p); // hWndParent 0x815184
    return p;
}

// Écran Formulaire de création — CÂBLAGE 1:1 sur les vrais sprites d'atlas (front W4-F1,
// Scene_CharSelectRender 0x51CED0, branche screen==2 EA 0x51E4A1). Dans CETTE fonction :
// aucun aplat FillRect, aucun titre/libellé FR fabriqué — le binaire ne dessine QUE le
// panneau (slot 40), les boutons -/+ (slots 41/42, état PRESSÉ uniquement), Confirmer/Annuler
// (slots 9/12), le caret (slot 43) et les 5 VALEURS localisées + le nom. Aucune chaîne
// "Créer/Nom/Classe/…" n'existe dans l'exe.
// (⚠ ce fichier ne dessine PLUS AUCUN aplat FillRect pour les confirmations : les ex-
//  DeleteConfirmRender/ExitConfirmRender à géométrie inventée sont remplacés par le MsgBox à
//  sprites réels UI/ConfirmMsgBox.h.)
//
// 🔴 ORDRE DE PEINTRE RE-ÉTABLI CE FRONT en balayant les sites de dessin de la plage
// [0x51E4A1, 0x51ECC5) : (1) panneau slot 40 @0x51E4CA ; (2) NOM saisi @0x51E507 ;
// (3) caret @0x51E543 ; (4) les 5 valeurs localisées, la DERNIÈRE @0x51E97E ; (5) PUIS
// SEULEMENT les 18 blits de boutons, de 0x51E9A5 (1er « - ») à 0x51ECC0 (rotation droite).
// Les boutons sont donc peints EN DERNIER, PAR-DESSUS le texte : une valeur localisée longue
// (langue verbeuse) est RECOUVERTE par le bouton, jamais l'inverse. Le batch des boutons est
// pour cela ROUVERT APRÈS le batch font, en fin de fonction.
void LoginScene::CreateFormRender() {
    LayoutCreateForm();
    // [A13] Curseur PHYSIQUE live (GetPhysicalCursorPos @0x51D493 + ScreenToClient @0x51D4A4),
    // lu une fois par frame au 2e Begin2D et partagé par TOUS les hit-tests de la scène 4.
    const POINT mp = CharSelectCursorClient();
    Button* formBtns[] = {
        &jobMinusBtn_, &jobPlusBtn_, &factionMinusBtn_, &factionPlusBtn_,
        &faceMinusBtn_, &facePlusBtn_, &hairMinusBtn_, &hairPlusBtn_,
        &variantMinusBtn_, &variantPlusBtn_, &createConfirmBtn_, &createCancelBtn_,
    };
    for (Button* b : formBtns) b->OnMouseMove(mp.x, mp.y);

    // Ancres RÉELLES du panneau de création (EA 0x51E4A1 sub 0x14F / 0x51E4B0 = 0x49).
    const int panelX = screenW_ - 335, panelY = 73;

    // [G1] L'ancien TODO « APERÇU 3D ... BLOQUÉ ICI : membres 3D possédés manquants » est
    // RETIRÉ : LoginScene.h appartient à ce front, les membres persistants existent
    // (charMesh_/charModels_/charMotions_) et les DEUX Char_RenderModel de l'écran Création
    // (EA 0x51D429 pass=1 / 0x51D480 pass=2, fiche &dword_16709B8) sont câblés dans
    // CharSelectRenderPreview3D() — au BON endroit de l'ordre de peintre, c'est-à-dire AVANT
    // ce batch 2D et APRÈS le fond, jamais ici.
    // ⚠ Le fond plein écran N'EST PLUS dessiné ici : il a son PROPRE Begin2D/End2D
    // (CharSelectRenderBg, 0x51D22D-0x51D2B5), fermé AVANT la passe 3D. Le redessiner dans
    // ce batch-ci le repeindrait PAR-DESSUS le modèle 3D.

    if (sprites_.Ready()) {
        sprites_.Begin();
        // Panneau de création (slot 40 unk_8EA270) @ (panelX, panelY) — blit natif, EA 0x51E4C5.
        if (gfx::GpuTexture* t = GetAtlasSprite(40))
            sprites_.DrawSprite(t->Handle(), nullptr, panelX, panelY, gfx::kSpriteWhite);
        // Caret du champ nom (g_SprTextInputCaret = slot 43 unk_8EA42C) @ (panelX+largeurNom+0x80,
        // panelY+0x35), quand le champ est focalisé (EA 0x51E50C test g_UIEditBoxMgr==3, Sprite2D_Draw
        // 0x51E543). Repli caret texte assuré par DrawFieldValue (batch font) si le sprite manque.
        if (createNameBox_.Focused()) {
            if (gfx::GpuTexture* caret = GetAtlasSprite(43)) {
                const std::string nm = createNameBox_.Text();
                const int w = nm.empty() ? 0 : font_.MeasureText(nm.c_str());
                sprites_.DrawSprite(caret->Handle(), nullptr, panelX + w + 128, panelY + 53, gfx::kSpriteWhite);
            }
        }
        sprites_.End();
    }

    if (font_.Ready()) {
        font_.BeginBatch();
        // 5 VALEURS localisées du formulaire (Scene_CharSelectRender, plage 0x51E548-0x51E93E),
        // CENTRÉES à x=panelX+0xA3 (=+163) via UI_MeasureNumberText/UI_DrawNumberValue :
        //   Job     (dword_16709DC) : Str(CreateJobLabelStrId)     y=panelY+0x4F (=+79)  EA 0x51E571
        //   Faction (dword_16709E4) : Str(CreateFactionLabelStrId) y=panelY+0x67 (=+103) EA 0x51E60B
        //   Face    (dword_16709E8) : "%c %s" 'A'+face + Str(28)   y=panelY+0x7F (=+127) EA 0x51E690
        //   Hair    (dword_16709EC) : "%c %s" 'A'+hair + Str(28)   y=panelY+0x97 (=+151) EA 0x51E6FD
        //   Variant (this[15716])   : Str(CreateVariantLabelStrId) y=panelY+0xAF (=+175) EA 0x51E76C
        // (helpers CharSelectFlow.h, mapping bit-exact re-vérifié EA par EA). Plus AUCUN "%d" brut,
        // AUCUN libellé/titre FR fabriqué (le binaire n'en dessine aucun).
        const int cx = panelX + 163; // panelX + 0xA3, centre des valeurs
        const std::string faceHairWord = game::Str(game::kCreateFaceHairLabelWordStrId);
        auto drawCentered = [&](const std::string& s, int y) {
            font_.DrawTextAt(s.c_str(), cx - font_.MeasureText(s.c_str()) / 2, y, kColText);
        };
        char buf[64];
        drawCentered(game::Str(game::CreateJobLabelStrId(charState_.createForm.job)),        panelY + 79);
        drawCentered(game::Str(game::CreateFactionLabelStrId(charState_.createForm.faction)), panelY + 103);
        std::snprintf(buf, sizeof(buf), "%c %s",
                      game::CreateFaceHairLabelLetter(charState_.createForm.face), faceHairWord.c_str());
        drawCentered(buf, panelY + 127);
        std::snprintf(buf, sizeof(buf), "%c %s",
                      game::CreateFaceHairLabelLetter(charState_.createForm.hairColor), faceHairWord.c_str());
        drawCentered(buf, panelY + 151);
        drawCentered(game::Str(game::CreateVariantLabelStrId(
                         charState_.createForm.job, charState_.createForm.variant)), panelY + 175);
        // Nom saisi : GetWindowTextA(dword_1668FCC) -> UI_DrawNumberValue @(panelX+0x7F, panelY+0x35)
        // EA 0x51E4DB-0x51E507. Valeur à gauche (non centrée), (panelX+127, panelY+53).
        DrawFieldValue(createNameBox_, panelX + 127, panelY + 53);
        font_.EndBatch();
    }

    // Ordre de peintre : les 18 blits de boutons du binaire (EA 0x51E9A5..0x51ECC0) viennent
    // APRÈS le dernier texte (dernier UI_DrawNumberValue EA 0x51E97E) — ils recouvrent donc
    // les valeurs localisées qui débordent, jamais l'inverse. D'où ce SECOND batch sprite,
    // rouvert après le batch font. Les 10 boutons -/+ ne portent QUE leur sprite pressé
    // (SetPressed en Init, cf. garde latch EA 0x51E989) : au repos, DrawSkin ne dessine RIEN.
    if (sprites_.Ready()) {
        sprites_.Begin();
        for (Button* b : formBtns) b->DrawSkin(sprites_);
        // Boutons de ROTATION de l'aperçu (EA 0x51EC2F slot 44/45 ; EA 0x51EC88 slot 46/47),
        // positionnés par UI_ProjectSpriteToScreen 0x50F5D0 aux mondes (390,628)/(557,628).
        // État PRESSÉ (slot idle+1) si le latch est armé (this[15]/this[16]), sinon idle ;
        // AUCUN survol (le binaire ne teste que le latch : garde `cmp [reg+3Ch],0 ; jz`
        // @0x51EC3A / @0x51EC93). La projection utilise TOUJOURS les dims du sprite IDLE.
        auto drawRot = [&](int idleSlot, int worldX, int worldY, bool latched) {
            gfx::GpuTexture* base = GetAtlasSprite(idleSlot);
            if (!base) return; // sprite non chargeable -> rien (comme AtlasHitTest)
            const POINT a = ui::ProjectDesignAnchor(static_cast<int>(base->Width()),
                                                    static_cast<int>(base->Height()),
                                                    worldX, worldY, screenW_, screenH_);
            gfx::GpuTexture* t = latched ? GetAtlasSprite(idleSlot + 1) : base; // +1 = pressé
            if (t) sprites_.DrawSprite(t->Handle(), nullptr, a.x, a.y, gfx::kSpriteWhite);
        };
        drawRot(44, 390, 628, rotLeftLatched_);  // this[15] @0x51EC2F
        drawRot(46, 557, 628, rotRightLatched_); // this[16] @0x51EC88
        sprites_.End();
    }
}

// Peint le MsgBox modal partagé (msgBox_ = dword_1822438) en QUEUE de la scène active
// (UI_RenderAllDialogs 0x5AE2D0). REMPLACE les ex-DeleteConfirmRender/ExitConfirmRender à
// géométrie INVENTÉE (voile modal + 2 aplats panneau/liseré + boutons Button 64x24, aucune
// ancre) : le binaire ne dessine AUCUN aplat, seulement les sprites d'atlas via UI_MsgBox_Render
// 0x5C3100 (panneau slot 7 centré sur sa taille native ; OK 8/9/10 @ +165,+90 ; Annuler
// 11/12/13 @ +241,+90 ; titre centré +234,+42 corps vide). Curseur de survol = position
// PHYSIQUE (CharSelectCursorClient = GetPhysicalCursorPos+ScreenToClient, comme le pipeline de
// dialogues @0x5AE2DD). No-op si msgBox_ est fermé -> appelable en queue de N'IMPORTE quelle scène.
void LoginScene::RenderMsgBox() {
    if (!msgBox_.IsOpen()) return;
    const POINT cur = CharSelectCursorClient(); // curseur physique (pipeline dialogues 0x5AE2DD)
    msgBox_.Render([this](int s) { return GetAtlasSprite(s); },
                   sprites_, font_, screenW_, screenH_, cur, kColText);
}

// Scene_CharSelectOnMouseDown 0x520F40 : arme les latches (boutons) / sélectionne
// un emplacement au clic direct (le binaire sélectionne aussi au down, cf. header
// CharSelectFlow.h). La validation des boutons (envoi réseau) a lieu au « up »
// dans CharSelectOnMouseUp, motif identique à Login.
void LoginScene::CharSelectOnMouseDown(int x, int y) {
    // [A2] GARDE DE SOUS-ÉTAT — TOUTE PREMIÈRE LIGNE, AVANT le test de la confirmation
    // modale : `mov eax,[...] ; cmp dword ptr [eax+4], 1 ; jnz -> return` @0x520F4D.
    // Le binaire sort IMMÉDIATEMENT si this[1] != 1 : la souris est inerte pendant l'Init
    // (30 frames d'écran vide) ET pendant le Verrouillé (image figée). Placer cette garde
    // APRÈS le test `msgBox_.IsOpen()` laisserait cliquer la modale dans un état où le binaire
    // ne route rien.
    if (charState_.subState != game::CharSelectSubState::Active) return;

    if (msgBox_.IsOpen()) { // MsgBox modal (UI_MsgBox_OnLButtonDown 0x5C0980) : consomme le clic
        msgBox_.OnMouseDown([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    if (charState_.screen == game::CharSelectScreen::CreateForm) {
        LayoutCreateForm();
        createNameBox_.OnMouseDown(x, y);
        jobMinusBtn_.OnMouseDown(x, y);      jobPlusBtn_.OnMouseDown(x, y);
        factionMinusBtn_.OnMouseDown(x, y);  factionPlusBtn_.OnMouseDown(x, y);
        faceMinusBtn_.OnMouseDown(x, y);     facePlusBtn_.OnMouseDown(x, y);
        hairMinusBtn_.OnMouseDown(x, y);     hairPlusBtn_.OnMouseDown(x, y);
        variantMinusBtn_.OnMouseDown(x, y);  variantPlusBtn_.OnMouseDown(x, y);
        createConfirmBtn_.OnMouseDown(x, y); createCancelBtn_.OnMouseDown(x, y);
        // Boutons de ROTATION (latches COLLANTS this[15]/this[16], ARMÉS seulement — jamais
        // désarmés hors Init) : hit-test sur le sprite IDLE projeté (UI_ProjectSpriteToScreen
        // 0x50F5D0), EA 0x522DB1 (slot 44, monde (390,628) -> this[15]=1 @0x522DE7) et EA
        // 0x522E09 (slot 46, monde (557,628) -> this[16]=1 @0x522E3F).
        auto rotHit = [&](int idleSlot, int worldX, int worldY) -> bool {
            gfx::GpuTexture* t = GetAtlasSprite(idleSlot);
            if (!t || t->Width() <= 0 || t->Height() <= 0) return false;
            const int w = static_cast<int>(t->Width()), h = static_cast<int>(t->Height());
            const POINT a = ui::ProjectDesignAnchor(w, h, worldX, worldY, screenW_, screenH_);
            return x >= a.x && x < a.x + w && y >= a.y && y < a.y + h;
        };
        if (rotHit(44, 390, 628)) rotLeftLatched_  = true; // @0x522DE7
        if (rotHit(46, 557, 628)) rotRightLatched_ = true; // @0x522E3F
        return;
    }

    LayoutCharSelect();
    // Hit-test de sélection = le sprite de SURBRILLANCE unk_924944 (slot 1657) @
    // (nWidth-194 + 25, 19 + 44*i + 50), PAS toute la fiche : Scene_CharSelectOnMouseDown
    // 0x520F40, EA 0x522688 — MÊMES coordonnées que le blit de CharListRender (@0x51D634 /
    // @0x51D62C). Le repli inventé (64x28 « si l'asset manque ») a été RETIRÉ : les vraies
    // dimensions sont spr+108/+112, remplies au chargement du .IMG, et un sprite non
    // chargeable ne doit produire AUCUNE zone cliquable (cf. AtlasHitTest).
    for (int i = 0; i < game::kMaxCharSlots; ++i) {
        if (AtlasHitTest(1657, (screenW_ - 194) + 0x19, 19 + 44 * i + 0x32, x, y)) {
            game::SelectCharacterSlot(charState_, i);
            return;
        }
    }
    // RESTAURER (slot 3086) : le binaire porte la MÊME garde serveur AVANT le hit-test —
    //   `cmp dword ptr [edx+0F038h], 32h` @0x522908 ; `jz short loc_52291D` @0x52290F (==50 -> teste)
    //   `cmp dword ptr [eax+0F038h], 28h` @0x522914 ; `jnz short loc_52295D` @0x52291B (!=40 -> saute)
    // puis seulement `call Sprite2D_HitTest(unk_958368)` @0x522935 et l'armement du latch
    // `mov [edx+28h], 1` @0x522951. Sans elle, le bouton restait cliquable là où le binaire
    // l'ignore totalement (et invisible : cf. la même garde au rendu, EA 0x51E312/0x51E321).
    const int32_t srvDown = charHost_.GetServerIndex ? charHost_.GetServerIndex() : 0;
    if (srvDown == 50 || srvDown == 40)
        restoreBtn_.OnMouseDown(x, y);
    createBtn_.OnMouseDown(x, y);
    deleteBtn_.OnMouseDown(x, y);
    enterBtn_.OnMouseDown(x, y);
    backBtn_.OnMouseDown(x, y);
    quitBtn_.OnMouseDown(x, y);    // QUITTER (slot 25) : latch this[9] (`[ecx+24h]` @0x51E288)
}

// Scene_CharSelectOnMouseUp 0x522E50 : porte la quasi-totalité de la logique
// métier réelle (validation + requêtes réseau) — ici déléguée aux callbacks
// SetOnClick câblés en Init() (game::OnXxxButtonClicked/ConfirmXxx).
void LoginScene::CharSelectOnMouseUp(int x, int y) {
    // [A2] MÊME garde, MÊME position : `cmp dword ptr [eax+4], 1 ; jnz -> return` @0x522E70.
    if (charState_.subState != game::CharSelectSubState::Active) return;

    if (msgBox_.IsOpen()) { // MsgBox modal (UI_MsgBox_OnLButtonUp 0x5C0A90) : OK -> action, sinon ferme
        msgBox_.OnMouseUp([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    if (charState_.screen == game::CharSelectScreen::CreateForm) {
        jobMinusBtn_.OnMouseUp(x, y);      jobPlusBtn_.OnMouseUp(x, y);
        factionMinusBtn_.OnMouseUp(x, y);  factionPlusBtn_.OnMouseUp(x, y);
        faceMinusBtn_.OnMouseUp(x, y);     facePlusBtn_.OnMouseUp(x, y);
        hairMinusBtn_.OnMouseUp(x, y);     hairPlusBtn_.OnMouseUp(x, y);
        variantMinusBtn_.OnMouseUp(x, y);  variantPlusBtn_.OnMouseUp(x, y);
        createConfirmBtn_.OnMouseUp(x, y); createCancelBtn_.OnMouseUp(x, y);
        return;
    }
    // RESTAURER : garde serveur RE-VÉRIFIÉE au « up » aussi (elle précède le hit-test) —
    //   `cmp dword ptr [ecx+0F038h], 32h` @0x525AF5 ; `jz short loc_525B11` @0x525AFC
    //   `cmp dword ptr [edx+0F038h], 28h` @0x525B04 ; `jnz loc_525C3C` @0x525B0B
    // puis `cmp dword ptr [eax+28h],0 ; jz loc_525C3C` @0x525B17 (latch requis — porté par
    // Button::OnMouseUp, qui ne valide que si armed_) et enfin Sprite2D_HitTest @0x525B46.
    // Action de restauration elle-même hors flux porté (host.RestoreCharacter non assigné).
    const int32_t srvUp = charHost_.GetServerIndex ? charHost_.GetServerIndex() : 0;
    if (srvUp == 50 || srvUp == 40)
        restoreBtn_.OnMouseUp(x, y);
    createBtn_.OnMouseUp(x, y);
    deleteBtn_.OnMouseUp(x, y);
    enterBtn_.OnMouseUp(x, y);
    backBtn_.OnMouseUp(x, y);
    // QUITTER (unk_8E99C4 = slot 25 @ (x0, y0+222)) : hit-test EA 0x525AA5 -> si
    // this[+0xF598] != 3 (pas d'entrée en cours, garde EA 0x525ABE), UI_MsgBox_Open de
    // confirmation (EA 0x525AE5). Désormais un vrai Button latché (cf. LoginScene.h) : le
    // hit-test « à la main » sur le rect du sprite a été retiré — il court-circuitait le
    // latch, donc l'état PRESSÉ (slot 27) n'était jamais peint. L'action reste
    // game::OnQuitButtonClicked (qui porte la garde Entering) — câblée en Init().
    quitBtn_.OnMouseUp(x, y);
}

// Construit charHost_ : point d'intégration réseau/UI de Game/CharSelectFlow.h,
// branché sur les VRAIS builders (Net/CharSelectPackets.h) au lieu des placeholders
// gameHost_/gamePort_ jamais renseignés de l'ancienne implémentation
// (cf. Docs/TS2_CHARSELECT_AUDIT.md §2.5).
void LoginScene::BuildCharSelectHost() {
    // net::LoadCharacterSlotsFromRecords (Net/CharSelectPackets.h) parse les 3 fiches
    // brutes persistées par Net/Login.cpp::LoginRequest (net::g_CharRecords) — câblé
    // depuis la session 2026-07-14 (cf. Docs/TS2_LOGINSCENE_AUDIT.md §3.9 : l'écart de
    // complétude qui laissait ce blob perdu est désormais fermé). Avant tout login
    // réussi, net::g_CharRecords est à zéro -> les 3 fiches sont vides -> `slots` reste
    // {} (occupied=false partout), fidèle au cas "jamais de faux occupé" déjà visé par
    // l'ancien stub.
    charHost_.LoadCharacterSlots = &net::LoadCharacterSlotsFromRecords;
    charHost_.AccountKeepAlive = [this]() -> int32_t {
        return net_ ? net::AccountKeepAlive(net_->Client()) : 0;
    };
    // Str_ValidateNameChars(nom du slot) 0x53FD70 — désormais câblable : le nom du
    // personnage STOCKÉ est disponible via charState_.slots[slot].name (peuplé par
    // LoadCharacterSlots ci-dessus). hors-bornes -> true (fidèle a minima, ne bloque
    // jamais artificiellement le flux sur un index invalide).
    charHost_.IsCharacterNameValid = [this](int32_t slot) {
        if (slot < 0 || slot >= game::kMaxCharSlots) return true;
        return game::ValidateNameCharset(charState_.slots[static_cast<size_t>(slot)].name);
    };
    charHost_.HasGmAuthLevel       = [] { return net::g_GmAuthLevel >= 1; };
    // g_ServerModeFlag 0x166918C — DÉJÀ réifié dans serverModeFlag_ (Init, cf. plus haut) :
    // la donnée était là, le consommateur aussi (CharSelectFlow.cpp:224), seul le fil
    // manquait. Lu par le binaire @0x5252C1 (`cmp ds:g_ServerModeFlag, 0` — garde de la
    // notice 2163 à l'ouverture du formulaire) et @0x51c08d (nombre d'entrées de la liste
    // de restauration). Sans ce câblage, RecomputeRestoreListCount prenait TOUJOURS la
    // branche `else`.
    charHost_.IsServerModeFlag = [this] { return serverModeFlag_ != 0; };
    // this[15374] (+0xF038) = index serveur « à plat » (= 5*groupe + bouton), écrit par
    // Scene_ServerSelectOnMouseDown 0x519A36 sur la MÊME cellule que serverState_.selectedServer
    // (confirmé décompilation). Ferme le TODO 0x51c09d/0x51c13f sans deviner : en mode
    // SingleServer (/0/0/2/…, 1 entrée) il vaut 0 -> les gardes CharSelect ==40/50/60 restent
    // inertes (fidèle). La divergence 40/50/60 n'apparaît qu'avec la liste multi-groupes live
    // (front ServerSelect distinct).
    charHost_.GetServerIndex = [this]() -> int32_t {
        std::lock_guard<std::mutex> lk(serverMutex_);
        return serverState_.selectedServer;
    };
    // Assistant PIN / mot de passe secondaire — réifié côté Net/ cette passe (offsets
    // recvBuf+0x95=149 / +0x99=153, cf. Net/Login.cpp + Net/NetClient.h). dword_16692A4 != 0
    // (EA 0x51beae) => assistant requis ; Crt_Strcmp(unk_16692A8,"") != 0 (EA 0x51bf3d) => PIN
    // déjà stocké (mode VÉRIFIER). Débloque le flux PIN de CharSelectFlow (fidélité PRNG).
    charHost_.IsSecondaryPasswordRequired = [] { return net::g_SecondaryPwRequired != 0; };
    charHost_.HasStoredSecondaryPassword  = [] { return net::g_StoredSecondaryPw[0] != '\0'; };
    // UI_FocusEditBox 0x50F4A0 (g_UIEditBoxMgr 0x1668FC0) — décompilée : `if (idx < 0x16)
    // { *this = idx; return idx ? SetFocus(hwnd[idx]) : SetFocus(hWndParent); }` — elle ne
    // fait QUE SetFocus + poser l'index de focus. Indices PROUVÉS :
    //   0  = fenêtre parente  — `push 0` @0x51BE77 -> call @0x51BE7E (Init de la scène) et @0x529365
    //   3  = nom de création  — `push 3` @0x5252FC -> call @0x525303, immédiatement suivi de
    //        SetWindowTextA(dword_1668FCC, "") @0x525314 : c'est bien l'EDIT du nom, le MÊME
    //        hWnd que le GetWindowTextA du rendu du formulaire (@0x51E4E2). Recoupement
    //        indépendant : le caret n'est peint que si `g_UIEditBoxMgr == 3` (@0x51E50C).
    //   19 = suppression par nom — `push 13h` @0x525FCC
    // Aucune correspondance n'est INVENTÉE pour les indices non prouvés : ils sont ignorés.
    charHost_.FocusEditBox = [this](int32_t idx) {
        if (idx == 0) { SetFocus(0); createNameBox_.SetFocused(false); return; } // -> hWndParent
        if (idx == 3) { createNameBox_.SetFocused(true); return; }
        // TODO [ancre 0x525FCC] : index 19 = EDIT « supprimer par nom », widget non porté.
    };
    charHost_.ShowNotice = [this](int32_t strId) {
        // StrTable005_Get(g_LangId, strId) 0x4C1D20 — g_Strings.messages est charge par
        // App::Init AVANT toute scene (cf. App.cpp::Init, LoadStringTables), donc le texte
        // reel est deja disponible ici (plus de repli "#id" — cf. game::Str()).
        OpenNotice(game::Str(strId).c_str());
    };
    // [A3] ShowNoticeTyped — PRÉFÉRÉ à ShowNotice (CharSelectFlow.h l'essaie en premier).
    // Le 2e argument de UI_NoticeDlg_Open 0x5C0280 est ce qui décide du DEVENIR de l'état
    // Verrouillé, via UI_NoticeDlg_OnLButtonUp 0x5C03F0 (route UI_RouteLButtonUp 0x5AD0F0,
    // xref unique EA 0x5AD164 — jamais les handlers de scène, qui sont gatés `==1`) :
    //   mode 1 (Close)      : simple fermeture, la scène RESTE où elle est (erreurs
    //                         récupérables, ex. après un Net_ReqCancelEnter réussi).
    //   mode 2 (Disconnect) : case 2 @0x5C04C9 -> Net_CloseSocket(&g_NetClient) @0x5C04DF ;
    //                         g_SceneMgr=2 @0x5C04E4 ; g_SceneSubState=0 @0x5C04EE ;
    //                         dword_1676188=0 @0x5C04F8.
    //   mode 3 (Quit)       : g_QuitFlag=1 — JAMAIS utilisé par CharSelect.
    // Sans ce câblage, le type était PERDU et Verrouillé devenait un CUL-DE-SAC DÉFINITIF :
    // Update est inerte en sous-état 2 et les 4 handlers souris de la scène refusent tout.
    // (wiring TODO NOTICEDLG_MODE2 de Game/CharSelectFlow.h:37 — FERMÉ ici.)
    charHost_.ShowNoticeTyped = [this](int32_t strId, game::NoticeType type) {
        noticeType_ = type;
        if (type == game::NoticeType::Disconnect) {
            OpenNotice(game::Str(strId).c_str(), [this] {
                // Effets EXACTS du case 2, dans l'ordre du binaire. game::OnNoticeDlgMode2Ok
                // pose CloseConnection + pendingTransition=ServerSelect + subState=Init +
                // frameCounter=0 ; CharSelectUpdate consomme la transition (cf. [A5/A3]).
                game::OnNoticeDlgMode2Ok(charState_, charHost_);
            });
        } else {
            OpenNotice(game::Str(strId).c_str()); // mode 1 : fermeture sèche, aucun effet
        }
    };
    // W5b — CÂBLAGE de la notice « deltas post-login » (Net/Login.h::g_LoginNoticeHook).
    // Sans cette pose, le hook restait nul dans TOUT le dépôt : la branche notice de
    // net::LoginRequest (Net/Login.cpp:277) était du CODE MORT et la notice ne pouvait
    // jamais s'afficher. Dans le binaire elle est INCONDITIONNELLE une fois la garde
    // delta>0 franchie — Net_LoginRequest 0x51B8E0 :
    //   StrTable005_Get(g_LangId, 1785) EA 0x51bd68
    //   -> UI_NoticeDlg_Open(byte_18225C8, 1, <texte>, "") EA 0x51bd75.
    // Même motif que charHost_.ShowNotice ci-dessus (game::Str = StrTable005_Get fidèle).
    net::g_LoginNoticeHook = [this](int32_t id) {
        OpenNotice(game::Str(id).c_str());
    };
    // UI_MsgBox_Open(dword_1822438, 2, StrTable005_Get(g_LangId,49), "") @0x5254C4-0x5254DD :
    // ouvre le MsgBox modal partagé (type 2 = suppression). L'action OK (UI_MsgBox_OnLButtonUp
    // case 2) = CharSelect_ReqDeleteChar 0x528FD0 -> game::ConfirmDeleteCharacter (opcode 18) ;
    // Annuler = fermeture sèche. Le titre est game::Str(49) (fidèle, aucun texte FR dans l'exe).
    // ÉCART RÉSIDUEL [ancre 0x5C08D0/0x5C08DC] : le vrai UI_MsgBox_Open DÉFOCALISE aussi le champ
    // de nom (Util_SetClampedU8Field(dword_8E714C,0) + UI_FocusEditBox(&g_UIEditBoxMgr,0)) ; ici
    // le champ garde le focus sous la modale (non reproduit — hors périmètre de ce module).
    charHost_.ShowDeleteConfirm = [this] {
        msgBox_.Open(game::Str(49), 2,
                     [this] { game::ConfirmDeleteCharacter(charState_, charHost_); });
    };
    // Str_ValidateNameChars 0x53FD70, reproduction FIDELE : ValidateNameCharset()
    // (Game/CharSelectFlow.cpp) — encodage + longueur (12 caracteres utiles max, via
    // le buffer WCHAR[13] d'origine) + jeu de caracteres EXACT (0-9/A-Z/a-z/thai).
    // Le cas "nom vide" est gere SEPAREMENT et EN AMONT par
    // ConfirmCreateCharacter() (notice[38]), fidele a l'ordre du binaire.
    charHost_.ValidateNameChars = &game::ValidateNameCharset;
    // maybe_Dict001_MatchWord(g_BannedWordList, ...) 0x4C1410 : filtre reel des 1432
    // mots bannis (001.DAT, G01_GFONT\001.DAT) charges par App::Init. Implementation
    // sure et equivalente en intention mais pas bit-exacte vis-a-vis de sub_4C1410
    // (fenetre glissante d'origine) — cf. BannedWordDict::IsBanned (StringTables.h/.cpp)
    // pour l'ecart documente.
    charHost_.IsNameBanned = [](const std::string& n) {
        return game::g_Strings.bannedWords.IsBanned(n);
    };
    charHost_.GetEditedName = [this] { return createNameBox_.Text(); };
    // Job initial aléatoire — ancre PRÉCISE : Scene_CharSelectOnMouseUp 0x522E50,
    // `call Rng_Next` EA 0x52536F puis `cdq ; mov ecx, 3 ; idiv ecx` puis
    // `mov ds:dword_16709DC, edx` EA 0x52537C (job = Rng_Next() % 3), au mouse-up de
    // validation du bouton « Créer ». Flux RNG unique (cf. bgAtlasSlot_ ci-dessus) :
    // net::DefaultRng() et non std::rand().
    charHost_.RandomInitialJob = [] { return net::DefaultRng().NextMod(3); };

    charHost_.CreateCharacter = [this](int32_t slot, const game::CharCreateForm& form,
                                       int32_t presetId) -> int32_t {
        return net_ ? net::CreateCharacter(net_->Client(), slot, form, presetId)
                    : net::kCharSelectErrSend;
    };
    charHost_.DeleteCharacter = [this](int32_t slot) -> int32_t {
        return net_ ? net::CharSlotAction(net_->Client(), slot, /*action=*/1, /*arg=*/0)
                    : net::kCharSelectErrSend;
    };
    charHost_.RequestEnterCharInfo = [this](int32_t slot) -> game::EnterCharInfoResult {
        if (!net_) {
            game::EnterCharInfoResult r; r.resultCode = net::kCharSelectErrSend; return r;
        }
        return net::ReqEnterCharInfo(net_->Client(), slot);
    };
    charHost_.ConnectToGameServer = [this](int32_t domainId, int32_t gamePort) -> int32_t {
        // Net_SelectServerDomain 0x53FE90 (EA d'appel 0x51c850) : traduit le domainId reçu du
        // serveur (réponse op22) en hostname via la table codée en dur (Net/GameServerDomains.h),
        // PUIS Net_ConnectGameServer 0x462A70 (EA 0x51c866). Le PORT vient de la réponse op22.
        // Fallback (index hors-plage OU garde OFF) = hôte du canal login sélectionné.
        if (!net_) return net::kNetErrSocketSend;
        std::string fallback = net::kLoginHostCom;
        {
            std::lock_guard<std::mutex> lk(serverMutex_);
            const int idx = serverState_.selectedServer;
            if (idx >= 0 && idx < static_cast<int>(serverState_.servers.size()))
                fallback = serverState_.servers[static_cast<size_t>(idx)].name; // name == host (cf. BuildServerList)
        }
        std::string host = net::SelectGameServerHost(domainId, fallback.c_str()); // 0x53FE90
        return net::ConnectGameServer(net_->Client(), host.c_str(), static_cast<uint16_t>(gamePort), notifyWnd_);
    };
    charHost_.CancelEnter = [this]() -> int32_t {
        return net_ ? net::ReqCancelEnter(net_->Client()) : net::kCharSelectErrSend;
    };
    charHost_.GetEnterPreviewDurationFrames = nullptr; // -> kDefaultEnterPreviewFrames (CharSelectFlow.cpp)
    charHost_.CloseConnectionAndQuit = [this] {
        if (net_) net::NetCloseSocket(net_->Client());
        PostQuitMessage(0); // équivalent g_QuitFlag=1 (motif déjà utilisé, cf. App.cpp VK_ESCAPE)
    };
    // Net_CloseSocket(&g_NetClient) 0x463000 SEUL — bouton RETOUR (EA 0x525A46) et clic OK
    // du NoticeDlg mode 2 (EA 0x5C04DF). SANS quitter le process (≠ CloseConnectionAndQuit).
    charHost_.CloseConnection = [this] {
        if (net_) net::NetCloseSocket(net_->Client());
    };

    // [A15] Efface les 150 latches this[3..152] (+12..+608) à CHAQUE Init — boucle
    // `for (i=0;i<150;++i) this[i+3]=0` @0x51BE83-0x51BEA4 (`cmp var, 96h`). ⚠ CE N'EST PAS
    // 10 : Scene_CharSelectOnMouseDown arme des latches jusqu'à this[92]. Côté C++ les
    // latches sont portés par les Widget de ce fichier -> hook. Sans lui, un bouton resté
    // armé d'une visite à l'autre repeindrait son état PRESSÉ à la ré-entrée en scène.
    // Seuls les boutons RÉIFIÉS sont désarmés ici (les latches this[6]/this[7]/this[11]/
    // this[12] n'ont pas de widget : leurs boutons sont dessinés sans latch, cf.
    // CharButtonColumnRender).
    charHost_.ClearAllButtonLatches = [this] {
        Button* all[] = { &enterBtn_, &backBtn_, &createBtn_, &deleteBtn_, &restoreBtn_,
                          &quitBtn_, &jobMinusBtn_, &jobPlusBtn_, &factionMinusBtn_,
                          &factionPlusBtn_, &faceMinusBtn_, &facePlusBtn_, &hairMinusBtn_,
                          &hairPlusBtn_, &variantMinusBtn_, &variantPlusBtn_,
                          &createConfirmBtn_, &createCancelBtn_ };
        for (Button* b : all) b->Reset(); // armed_ = false ; hover_active_ = false (Widgets.h:223)
        // Latches COLLANTS de rotation (this[15]/this[16]) — effacés ICI seulement (boucle
        // 150-latch de l'Init @0x51BE83 couvre +0x3C/+0x40), jamais pendant l'état Actif.
        rotLeftLatched_ = rotRightLatched_ = false;
    };

    // [A8] Bascules de rotation de l'aperçu de CRÉATION : this[15] (`cmp [reg+3Ch],0`
    // @0x51CDD0) et this[16] (@0x51CDF1). Le flux applique `yaw += 3.0` @0x51CDE8 /
    // `yaw -= 3.0` @0x51CE09 sur this[15724], UNIQUEMENT en écran Création (game-side déjà
    // fait, CharSelectFlow.cpp:476-477). CÂBLÉ : UI_ProjectSpriteToScreen 0x50F5D0 est
    // désormais portée (UI/UiProjection.h), les 2 boutons (slots 44/45, 46/47 aux mondes
    // (390,628)/(557,628)) sont rendus (CreateFormRender) et hit-testés (CharSelectOnMouseDown),
    // armant ces latches COLLANTS lus ici.
    charHost_.IsRotateLeftLatched  = [this] { return rotLeftLatched_; };
    charHost_.IsRotateRightLatched = [this] { return rotRightLatched_; };

    // PcModel_ResolveSlotAndApply 0x4E5A00 -> NOMBRE DE FRAMES de l'animation. Le flux
    // l'appelle avec des arguments différents selon l'écran (LISTE : rec+40/rec+44 @0x51c555 ;
    // CRÉATION : dword_16709DC(+36)/dword_16709E4(+44) @0x51cd7a) — d'où la signature
    // générique. ⚠ MÊME MotionCache que le DESSIN (cf. LoginScene.h::charMotions_) : dans le
    // binaire c'est le MÊME g_ModelMotionArray 0x8E8B30 qui sert à résoudre la palette et à
    // compter les frames. Deux caches divergeraient sur les motions absentes, et le minuteur
    // d'entrée (`this[15719] >= durée` @0x51C649) partirait sur une durée fantôme.
    // Motion_BuildPathAndLoad 0x4D7390 cas 1 : "C%03d%03d%03d.MOTION" % (race+3*gender+1,
    // motion+1, animState+1) == MotionCache::GetForPlayer(race, gender, motion, animState).
    // ⚠ LE `return 0` CI-DESSOUS NE DÉCLENCHE AUCUN REPLI — l'ancien commentaire « -> repli
    // kDefaultEnterPreviewFrames » était FAUX à double titre : (1) MotionFrameCount teste la
    // PRÉSENCE du std::function, pas sa valeur, donc un hook assigné qui renvoie 0 ne retombe
    // jamais sur GetEnterPreviewDurationFrames ; (2) surtout, 0 EST une valeur de retour
    // LÉGITIME du binaire et ne DOIT PAS être filtrée — re-décompilé ce front :
    //   PcModel_ResolveSlotAndApply 0x4E5A00 = `Motion_GetFrameCount(PcModel_ResolveEquipSlot(…), a9)`
    //   Motion_GetFrameCount 0x4D7830 : `if (!*(this+152) && !Motion_Load(this,a2)) return 0;`
    //                                   (EA 0x4D784B -> 0x4D7854)
    // => sur motion ABSENTE/illisible, le binaire renvoie littéralement 0 et le flux amont
    // s'en accommode (cf. le bloc ⚠ de Game/CharSelectFlow.cpp::MotionFrameCount). Renvoyer 0
    // quand charMotions_ est nul ou la palette invalide reproduit donc EXACTEMENT le cas
    // « motion introuvable » du binaire : c'est la dégradation fidèle, pas un trou.
    charHost_.GetMotionFrameCount = [this](int32_t race, int32_t gender,
                                           int32_t motion, int32_t animState) -> int32_t {
        if (!charMotions_) return 0; // = cas « motion non chargeable » du binaire (EA 0x4D7854)
        const gfx::MotionPalette* mp = charMotions_->GetForPlayer(race, gender, motion, animState);
        return (mp && mp->valid) ? mp->frameCount : 0;
    };

    // Weapon_ClassFromField112 0x4CC870 (EA d'appel 0x525156) : classe d'arme 1..3 de l'arme
    // de DÉPART du slot, d'où l'index de motion de l'animation d'entrée = 2*classe (`shl eax,1`
    // @0x52515B, this[15717]=2*classe @0x525163 — le ×2 reste game-side, CharSelectFlow.cpp:1016).
    // Source = ID d'objet à fiche+216 (= bloc équip +104 +112, a2+112 @0x4CC88D), résolu dans la
    // DB items mITEM (game::GetItemInfo = MobDb_GetEntry 0x4C3C00) puis switch typeCode+188.
    // Ce hook était nullptr -> previewMotionIndex restait 0 (motion 0 au lieu de 2/4/6).
    // Dégradation fidèle : DB non chargée ou item introuvable -> nullptr -> 0 (comme @0x4CC893).
    charHost_.GetEnterPreviewWeaponClass = [this](int32_t slot) -> int32_t {
        if (slot < 0 || slot >= net::kCharRecordCount) return 0;
        const uint32_t weaponId = static_cast<uint32_t>(CharRecI32(slot, 216)); // fiche+216
        const game::ItemInfo* rec = game::GetItemInfo(weaponId);                // 0x4C3C00
        if (!rec) return 0;                                                     // @0x4CC893
        return game::WeaponClassFromTypeCode(rec->typeCode);                    // 0x4CC8BE
    };

    // 🔴 PublishSelfFromSlot — miroir du memcpy UNIQUE @0x51C707 :
    //   Crt_Memcpy(g_SelfCharInvBlock /*0x1673170*/, &unk_1669380 + 10088*slot, 0x2768u)
    // Ce memcpy pose À LA FOIS le bloc d'inventaire ET g_LocalElement (= bloc+0x24 =
    // fiche[+36] = le champ `job`), qui part ensuite sur le fil aux octets [137..140] du
    // paquet d'auth op11 de Net_ConnectGameServer 0x462A70 (EA 0x462D5D).
    // ORDRE PROUVÉ : 0x51C707 (memcpy) ≺ 0x51C81D (op22) ≺ 0x51C850 ≺ op11.
    // Ce hook était NON BRANCHÉ dans tout le dépôt (défaut GAMEAUTH_Element_Zero documenté
    // par Game/CharSelectFlow.h:400-405) : les octets [137..140] du handshake partaient à 0.
    // g_LocalElementSecondary 0x1673198 = fiche +40 (la RACE, que seul le serveur remplit) —
    // posé aussi, même memcpy.
    charHost_.PublishSelfFromSlot = [this](int32_t slot) {
        if (slot < 0 || slot >= game::kMaxCharSlots) return;
        const game::CharSlotInfo& s = charState_.slots[static_cast<size_t>(slot)];
        game::g_World.self.element          = s.job;  // 0x1673194 = bloc+0x24 = fiche +36
        game::g_World.self.elementSecondary = s.race; // 0x1673198 = bloc+0x28 = fiche +40
    };
}

// ===========================================================================
// Notice modale (UI_NoticeDlg_Open 0x5C0280 / UI_NoticeDlg_OnLButtonUp 0x5C03F0 simplifiés)
// ===========================================================================
void LoginScene::OpenNotice(const std::string& text, std::function<void()> onClose) {
    noticeOpen_    = true;
    noticeText_    = text;
    noticeOnClose_ = std::move(onClose);
}

// Ferme la notice et exécute l'action de son « kind » (cf. OpenNotice). Point d'entrée
// unique du clic OK (OnMouseUp) et d'Entrée/Échap (OnKeyDown) — fidèle : le binaire ferme
// aussi la notice puis exécute l'action AVANT de rendre la main (UI_NoticeDlg_Close appelé
// avant le switch(this[4]) dans UI_NoticeDlg_OnLButtonUp, EA 0x5C04A5).
void LoginScene::CloseNotice() {
    noticeOpen_ = false;
    if (noticeOnClose_) {
        std::function<void()> cb = std::move(noticeOnClose_);
        noticeOnClose_ = nullptr;
        cb();
    }
}

// Pas d'animation d'ouverture/fermeture : confirmé par décompilation de
// UI_NoticeDlg_Render 0x5C0630 (dessin à position fixe, pleine opacité, tant que le
// flag actif est vrai) et UI_NoticeDlg_OnLButtonUp 0x5C03F0 (ferme + exécute l'action
// au même frame, sans délai). Voir Game/ClientRuntime.h::PromptState pour le détail.
// Le tracé statique ci-dessous est donc fidèle tel quel — ne pas ajouter de fondu.
void LoginScene::RenderNotice() {
    // Vrai dialogue de notice (UI_NoticeDlg_Render 0x5C0630) : panneau unk_8E8F5C = slot 7 ->
    // 001_00008.IMG, centré sur SA taille RÉELLE ; bouton OK unk_8E8FF0/unk_8E9084/unk_8E9118
    // = slots 8/9/10 (idle/survol/pressé) à (panneau+203, +90) ; texte centré sur panneau+234
    // à panneau+42 (1 ligne) via UI_DrawNumberValue (mode contour). ZÉRO aplat, ZÉRO voile
    // modal (le binaire n'en dessine aucun) : sans la vraie texture, RIEN (fidèle Sprite2D_Draw).
    gfx::GpuTexture* panel = GetAtlasSprite(7);
    if (!panel || !panel->Valid() || panel->Width() == 0 || panel->Height() == 0) return;
    const int panelW = static_cast<int>(panel->Width());
    const int panelH = static_cast<int>(panel->Height());
    const int px = screenW_ / 2 - panelW / 2;
    const int py = screenH_ / 2 - panelH / 2;
    const int okX = px + 203, okY = py + 90;

    // Survol du bouton OK (Sprite2D_HitTest sur la taille native du sprite idle slot 8).
    const POINT mp = CursorClient();
    gfx::GpuTexture* okIdle = GetAtlasSprite(8);
    bool okHover = false;
    if (okIdle && okIdle->Valid()) {
        okHover = mp.x >= okX && mp.x < okX + static_cast<int>(okIdle->Width()) &&
                  mp.y >= okY && mp.y < okY + static_cast<int>(okIdle->Height());
    }

    if (sprites_.Ready()) {
        sprites_.Begin();
        sprites_.DrawSprite(panel->Handle(), nullptr, px, py, gfx::kSpriteWhite);
        // OK : slot 8 (idle) ou 9 (survol). Le clic sur SA zone ferme la notice (cf. OnMouseUp).
        if (gfx::GpuTexture* okT = GetAtlasSprite(okHover ? 9 : 8))
            sprites_.DrawSprite(okT->Handle(), nullptr, okX, okY, gfx::kSpriteWhite);
        sprites_.End();
    }
    if (font_.Ready() && !noticeText_.empty()) {
        font_.BeginBatch();
        const int tw = font_.MeasureText(noticeText_.c_str());
        font_.DrawTextStyled(noticeText_.c_str(), px + 234 - tw / 2, py + 42, kColText, gfx::kStyleOutline);
        font_.EndBatch();
    }
}

// ===========================================================================
// Données serveurs & libellés d'erreur
// ===========================================================================
// Construction de la table serveurs — reproduction FIDÈLE de Scene_ServerSelectUpdate
// 0x518B30 (branche EA 0x518CF6 `if (g_ServerModeFlag)`), keyée sur serverModeFlag_
// (= dword_166918C = GameConfig::buildVariant, 1er jeton `/N/...` de la ligne de
// commande, parsé par WinMain EA 0x4609F1/0x460BAE via Crt_Atoi ; cf. LoginScene.h::Init).
//
// RE-VÉRIFIÉ par décompilation + désassemblage directs (idaTs2, 2026-07-15) — hôtes/ports
// relevés octet par octet ci-dessous. L'IDB PRIME : ces valeurs CORRIGENT
// Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md §2.4, qui listait à tort `141.95.12.155` comme
// 1er canal MultiChannel — c'est en réalité l'hôte SingleServer du mode 2 ; le vrai 1er
// canal MultiChannel est `test_ts2_login.co.kr` (EA 0x518F92).
//
// Le binaire écrit le HOSTNAME dans a1[371] (g_ServerNameTable) et le réutilise TEL QUEL
// comme cible de connexion (Net_ConnectLoginServer host arg, Scene_LoginUpdate EA
// 0x51AAEB) : il n'existe PAS de « nom d'affichage » distinct. On reproduit ce modèle
// (name == host == hostname) au lieu d'un libellé fabriqué (« TwelveSky2 »/« Canal N »).
//
// serverModeFlag_ vaut 0 par défaut (Init param défauté) = le SEUL mode atteint par la
// commande documentée `/0/0/2/1024/768`. Les modes 1/2/MultiChannel ne sont construits
// que si SceneManager câble un buildVariant non nul (cf. « Points d'attention » de la doc
// de session) — reproduits ici pour fidélité, PAS parce qu'ils sont actifs. Fidélité
// notable : modes 0/1/2 = UN SEUL serveur (a1[370]=1, port 8088) ; seul le mode « autre »
// (≠0,1,2) construit 6 canaux — donc « sinon multi-canal » signifie flag ∉ {0,1,2}.
void LoginScene::BuildServerList() {
    // STRUCTURE/FLUX via le module fidèle game::BuildServerList (count, ports, bornes de
    // boutons selectedGroupBtnLo/Hi, selectedServer=-1, background) : mode SingleServer pour
    // flag ∈ {0,1,2} (1 serveur, port 8088), MultiChannel sinon (6 canaux, ports EXACTS).
    const game::ServerListMode mode =
        (serverModeFlag_ == 0 || serverModeFlag_ == 1 || serverModeFlag_ == 2)
            ? game::ServerListMode::SingleServer
            : game::ServerListMode::MultiChannel;
    game::BuildServerList(serverState_, mode);

    // Injection des HÔTES RÉELS (relevés octet par octet, idaTs2) dans .name : le binaire
    // écrit le hostname dans a1[371] (g_ServerNameTable) et le réutilise TEL QUEL comme
    // cible de connexion (Net_ConnectLoginServer, Scene_LoginUpdate EA 0x51AAEB) ET
    // d'interrogation de statut (Net_QueryServerStatus 0x519CC0) — name == host == hostname,
    // pas de libellé d'affichage distinct. game::BuildServerList place un nom générique
    // ("12sky2-login..." / "Channel N") ; on le remplace par le hostname EXACT par mode.
    auto setName = [this](size_t i, const char* host) {
        if (i < serverState_.servers.size()) serverState_.servers[i].name = host;
    };
    switch (serverModeFlag_) {
    case 0: setName(0, net::kLoginHostCom); break; // EA 0x518D77 (.com) — déjà posé par le module
    case 1: setName(0, net::kLoginHostOrg); break; // EA 0x518E2F (.org, variante « EUTest »)
    case 2: setName(0, "141.95.12.155");   break;  // EA 0x518EE7 (IP littérale)
    default:                                        // MultiChannel — EA 0x518F6E-0x519198
        setName(0, "test_ts2_login.co.kr"); // EA 0x518F92 / port 10005
        setName(1, "192.168.0.93");         // EA 0x518FEA / port 10205
        setName(2, "192.168.0.93");         // EA 0x519042 / port 10305
        setName(3, "125.61.95.145");        // EA 0x51909A / port 11096
        setName(4, "192.168.0.91");         // EA 0x5190F2 / port 11095
        setName(5, "192.168.0.201");        // EA 0x51914A / port 11092
        break;
    }
    // selectedServer laissé à -1 par le module (this[15374]) ; OnServerClicked y écrira
    // l'index validé, DoLogin() garde contre -1 (flux normal : sélection avant login).
}
// Codes kNet* (Net/Login.h) -> message FR (le binaire mappe vers StrTable005).
// CORRIGÉ (Docs/TS2_LOGINSCENE_AUDIT.md §3.5) : le vrai switch(v35) de
// Scene_LoginUpdate 0x51A8D0 (EA 0x51AB03-0x51AF03) ne distingue QUE 1..4 ; TOUT
// le reste (5/6/7/12/défaut) retombe sur le message générique str6 — y compris
// kNetErrHost(12), qui avait ici (à tort) un message dédié « DNS » jamais
// affiché par le client réel depuis cet écran.
std::string LoginScene::ConnectErrText(int code) {
    // Messages RÉELS StrTable005 (game::Str = StrTable005_Get(g_LangId, id)) — mapping EXACT
    // de Scene_LoginUpdate 0x51A8D0 switch(v35) : 1->str2, 2->str3, 3->str4, 4->str5,
    // défaut->str6. Plus aucune chaîne FR inventée (l'asset 005.DAT porte le vrai texte localisé).
    switch (code) {
    case net::kNetErrState:      return game::Str(2);
    case net::kNetErrSocketSend: return game::Str(3);
    case net::kNetErrConnect:    return game::Str(4);
    case net::kNetErrRecv:       return game::Str(5);
    default:                     return game::Str(6);
    }
}

// Code résultat serveur (Net_LoginRequest) -> message FR. Identifiants StrTable005
// CORRIGÉS (Docs/TS2_LOGINSCENE_AUDIT.md §3.4) contre le vrai switch(v36) de
// Scene_LoginUpdate (EA 0x51AB3F-0x51AE83) : 20 issues distinctes (1..18+101+102),
// alors que seuls 8 cas étaient couverts ici auparavant (6/7/8/9/10/13/14/15/16/
// 17/18 retombaient à tort sur le message générique). Le cas 11 est un double-
// message d'origine (str369 + str616 en paramètre) — non représentable par ce
// const char* unique : on renvoie le message principal (str369) avec une note.
std::string LoginScene::LoginErrText(int result) {
    // Messages RÉELS StrTable005 (game::Str) — mapping EXACT de Scene_LoginUpdate 0x51A8D0
    // switch(v36). (case 11 = double message str369 + str616 en TITRE côté binaire ; ce retour
    // unique renvoie le message principal str369.) Plus AUCUNE chaîne FR inventée.
    switch (result) {
    case 1:   return game::Str(7);
    case 2:   return game::Str(8);
    case 3:   return game::Str(9);
    case 4:   return game::Str(10);
    case 5:   return game::Str(11);
    case 6:   return game::Str(12);
    case 7:   return game::Str(13);
    case 8:   return game::Str(14);
    case 9:   return game::Str(15);
    case 10:  return game::Str(16);
    case 11:  return game::Str(369);
    case 12:  return game::Str(813);
    case 13:  return game::Str(817);
    case 14:  return game::Str(1347);
    case 15:  return game::Str(1349);
    case 16:  return game::Str(229);
    case 17:  return game::Str(1840);
    case 18:  return game::Str(2453);
    case 101: return game::Str(17);
    case 102: return game::Str(18);
    default:  return game::Str(19);
    }
}

} // namespace ts2::ui

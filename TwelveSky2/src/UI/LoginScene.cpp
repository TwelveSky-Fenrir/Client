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
#include "Config/GameOptions.h"    // ts2::config::Cfg_SaveLastServer (G02_GINFO\010.BIN, écriture seule)
#include "Net/Login.h"             // ConnectLoginServer / LoginRequest / ConnectGameServer
#include "Net/CharSelectPackets.h" // AccountKeepAlive/CreateCharacter/CharSlotAction/ReqEnterCharInfo/ReqCancelEnter
#include "Net/Rng.h"                // DefaultRng() — Rng_Next() % 360 pour spawnRotationDeg (cf. GameState.h)
#include "Game/GameState.h"        // game::g_World.zoneId (consommé par EnterWorldFlow)
#include "Game/StringTables.h"     // game::g_Strings.bannedWords (001.DAT, 1432 mots — filtre de creation)
#include "Game/ClientRuntime.h"    // game::Str(id) — texte reel StrTable005 pour les notices CharSelect
#include "Asset/ImgFile.h"         // asset::ImgFile (chargeur .IMG, fond réel ServerSelect/Login)
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
// W4-F1). Ne subsistent que : kColPanel/kColPanelEdge = panneau + liseré de la
// confirmation modale de suppression (DeleteConfirmRender — overlay Oui/Non sans asset
// dédié identifié dans IDA) ; kColText = couleur des VALEURS réelles que le binaire dessine
// (valeur des champs Login, niveaux de perso, valeurs du formulaire de création). Aucune
// couleur d'aplat n'est plus présentée à la place d'un sprite .IMG réel. Les anciens
// placeholders CharSelect (kColBackdrop/kColField/kColFieldFocus/kColSel/kColTitle/
// kColLabel/kColBtn + BtnColor) ont été RETIRÉS : le formulaire est désormais câblé sur les
// vrais sprites (panneau slot 40 unk_8EA270 @0x51E4C5, +/- slots 41/42, Confirmer/Annuler
// slots 9/12, caret slot 43 g_SprTextInputCaret @0x51E53E) — cf. CreateFormRender().
constexpr D3DCOLOR kColPanel     = 0xF01E2A44; // panneau (confirmation de suppression modale)
constexpr D3DCOLOR kColPanelEdge = 0xFF3C5580; // liseré  (confirmation de suppression modale)
constexpr D3DCOLOR kColText      = 0xFFE8ECF4; // texte principal (VALEURS réelles)

inline RECT MakeRect(int x, int y, int w, int h) {
    RECT r; r.left = x; r.top = y; r.right = x + w; r.bottom = y + h; return r;
}

// Test point-dans-rectangle (le binaire n'exposait pas ce helper ; on le fournit
// localement pour le hit-test des lignes serveur/slot perso).
inline bool RectContains(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// Caméra d'aperçu 3D CharSelect — état de scène possédé ici (rule 6), extrait de
// Scene_CharSelectUpdate (sous-état Init, EA 0x51BDED -> 0x51BE65) : le binaire écrit le
// bloc g_CameraPos 0x800130..0x800144 (6 floats = œil + cible) puis le recopie dans
// g_GxdRenderer. Valeurs lues dans l'IDB : 0x800130=0.0 / 0x800134=flt_7EDA1C=5.0 /
// 0x800138=flt_7A9764=-28.0 (œil), 0x80013C=0.0 / 0x800140=flt_7A8D74=10.0 / 0x800144=0.0
// (cible), up=(0,1,0). Consommées par le bloc TODO d'aperçu 3D de CharListRender/
// CreateFormRender (câblage 3D bloqué : membres possédés par LoginScene.h, non éditable).
constexpr float kCharPreviewEye[3]    = { 0.0f, 5.0f, -28.0f }; // 0x800130/34/38 (0x51BDED)
constexpr float kCharPreviewTarget[3] = { 0.0f, 10.0f, 0.0f };  // 0x80013C/40/44 (0x51BE65)

} // namespace

// ===========================================================================
// Cycle de vie
// ===========================================================================
LoginScene::~LoginScene() { Shutdown(); }

bool LoginScene::Init(IDirect3DDevice9* device, net::NetSystem* net, HWND notifyWnd,
                      int screenW, int screenH, int serverModeFlag) {
    device_        = device;
    net_           = net;
    notifyWnd_     = notifyWnd;
    screenW_       = screenW;
    screenH_       = screenH;
    serverModeFlag_ = serverModeFlag;           // dword_166918C : consommé par BuildServerList()
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
    deleteYesBtn_.SetLabel("Oui"); deleteNoBtn_.SetLabel("Non");
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
    backBtn_.SetOnClick([this] {
        if (charState_.previewMotion == game::PreviewMotion::Entering) return; // EA 0x525A33
        if (net_) net::NetCloseSocket(net_->Client());                          // EA 0x525A46
        pending_                = ts2::Scene::ServerSelect;                      // EA 0x525A51 (this[0]=2)
        charState_.subState     = game::CharSelectSubState::Init;               // EA 0x525A5D (this[1]=0)
        charState_.frameCounter = 0;                                            // EA 0x525A6A (this[2]=0)
    });
    createBtn_.SetOnClick([this] { game::OnCreateButtonClicked(charState_, charHost_); });
    deleteBtn_.SetOnClick([this] { game::OnDeleteButtonClicked(charState_, charHost_); });
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
    // Confirmation de suppression (Oui/Non) : Oui -> ConfirmDeleteCharacter (opcode 18),
    // Non -> ferme simplement la confirmation (le binaire ne renvoie aucune requête).
    deleteYesBtn_.SetOnClick([this] {
        deleteConfirmOpen_ = false;
        game::ConfirmDeleteCharacter(charState_, charHost_);
    });
    deleteNoBtn_.SetOnClick([this] { deleteConfirmOpen_ = false; });

    // Textures réelles des boutons "Confirm"/"Cancel" (Docs/TS2_LOGIN_BUTTON_ASSETS.md §4) :
    // OK/Quitter du Login et Oui/Non de la confirmation de suppression — même paire de
    // sprites génériques réutilisée (doc §5). Repli sur le rect coloré déjà en place
    // (ApplyConfirmCancelSkin) si les fichiers sont réellement absents/illisibles.
    ApplyConfirmCancelSkin(okBtn_, exitBtn_);
    ApplyConfirmCancelSkin(deleteYesBtn_, deleteNoBtn_);

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

    // Formulaire de création (écran CreateForm) : boutons -/+ = sprites UNIQUES d'atlas baked
    // (slot 41 unk_8EA304 = "-", slot 42 unk_8EA398 = "+", Scene_CharSelectRender 0x51CED0) —
    // un seul état, dessiné en continu (SetNormal). Confirmer/Annuler = paire générique
    // Confirm/Cancel (slots 9/12, survol/pressé via ApplyConfirmCancelSkin, cf. EA 0x51E4A1).
    {
        Button* minusBtns[] = { &jobMinusBtn_, &factionMinusBtn_, &faceMinusBtn_, &hairMinusBtn_, &variantMinusBtn_ };
        Button* plusBtns[]  = { &jobPlusBtn_,  &factionPlusBtn_,  &facePlusBtn_,  &hairPlusBtn_,  &variantPlusBtn_ };
        if (gfx::GpuTexture* t = GetAtlasSprite(41)) for (Button* b : minusBtns) b->SetNormal(WidgetSprite(t->Handle()));
        if (gfx::GpuTexture* t = GetAtlasSprite(42)) for (Button* b : plusBtns)  b->SetNormal(WidgetSprite(t->Handle()));
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
    // Fond CharSelect (this[15713]) : slot atlas aléatoire 2383/2384/2385 (Scene_CharSelectUpdate
    // 0x51BD90, init — cf. Docs/TS2_CHARSELECT_RE.md §4). Tiré une fois ici.
    // Ancre : `call Rng_Next` EA 0x51C23A puis `cdq ; mov ecx, 3 ; idiv ecx` ;
    // `mov [edx+0F584h], 94Fh` EA 0x51C261 (=2383) / `950h` EA 0x51C270 (=2384) /
    // `951h` EA 0x51C27F (=2385). `2383 + Rng_Next()%3` est exactement équivalent :
    // Rng_Next() ∈ 0..0x7FFF (positif) => reste toujours 0..2, la branche `default`
    // (jmp 0x51C289, aucune écriture) est morte.
    charBgSlot_ = 2383 + net::DefaultRng().NextMod(3);

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
    // font_ / sprites_ se libèrent via leurs destructeurs.
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
void LoginScene::DrawFullscreenBg(int slotIndex) {
    gfx::GpuTexture* tex = GetAtlasSprite(slotIndex);
    if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0 && sprites_.Ready()) {
        const float sx = static_cast<float>(screenW_) / static_cast<float>(tex->Width());
        const float sy = static_cast<float>(screenH_) / static_cast<float>(tex->Height());
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
    case ts2::Scene::Login:      LoginOnMouseUp(x, y);      break; // OK se valide au « up »
    case ts2::Scene::CharSelect: CharSelectOnMouseUp(x, y); break; // boutons down-arme/up-valide
    default: break; // ServerSelect agit au « down »
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
        // EA 0x51A8FD : sub_4C1110(&unk_8E714C, 0) — remet à 0 CursorSet::state (index de
        // curseur actif, cf. Game/MiscManagers.h ; identifié par recoupement avec
        // Game/IntroFlow.h qui documente le même appel). Manager détenu par App::cursors_,
        // hors état LoginScene ; actuellement un no-op sans effet observable dans ce
        // squelette (rien n'écrit encore CursorSet::state ailleurs que sa valeur initiale
        // 0 posée à App_Init) — TODO concret si un jour un vrai changement de curseur est
        // câblé : brancher un callback équivalent à IntroHost::OnLogoSequenceBegin.
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
        // Angle de spawn : tiré fraîchement ICI (même point du flux que Scene_CharSelectUpdate
        // EA 0x51c7ed, juste avant Net_ConnectGameServer) via le MÊME PRNG que le binaire
        // (Net/Rng.h::DefaultRng, LCG identique à Rng_Next()). Cf. GameState.h::SelfState::
        // spawnRotationDeg et Net/CharSelectPackets.h::kTail72OffRotA/RotB pour la destination.
        game::g_World.self.spawnRotationDeg =
            static_cast<float>(net::DefaultRng().NextMod(360));
        pending_ = ts2::Scene::EnterWorld; // scene_id = 5
    }
}

// CORRIGÉ (audit CharSelect, session 2026-07-14, décompilation + désassemblage directs de
// Scene_CharSelectRender 0x51CED0, branche substate==1 « Liste », EA 0x51D4E6-0x51D7BF) :
// l'ancien gabarit (rowW=300, centré à l'écran, pas=50) était une INVENTION pure — le
// binaire réel ANCRE la colonne des 3 emplacements en HAUT-DROITE de l'écran, pas au
// centre. Origine du panneau/fond (Sprite2D_Draw sur unk_931680) : (nWidth-194, 19)
// (EA 0x51D4E6 `ecx=nWidth; sub ecx,0C2h` puis EA 0x51D4F5 `var_418=13h`). Pas vertical
// RÉEL de 44px par slot (`imul ..., 2Ch`, EA 0x51D590/0x51D5F3/0x51D623/0x51D65B) —
// PAS 50. Dans cette même boucle (i=0..2, EA 0x51D526 `cmp var_20,3` — CONFIRME
// game::kMaxCharSlots=3) le binaire dessine, par slot, une icône de tier/job
// (unk_931714/9319F8/94B504/985A1C, choisie selon dword_166A9CC/16693BC/16693B8 —
// stride 0x2768) à (nWidth-194, 19+34+i*44), un surlignage de sélection (unk_924944,
// UNIQUEMENT sur le slot == this[+0xF58C]) à (+25, +50+i*44), et un NIVEAU numérique
// (police bitmap unk_1685740, UI_MeasureNumberText/UI_DrawNumberValue, centré) vers
// (+54, +51+i*44) — rien de tout cela n'est câblé ici (cf. TODO au-dessus de
// CharListRender). NOTE HONNÊTE (re-vérifié idaTs2 2026-07-15) : Sprite2D_Draw (EA
// 0x51D50F pour le panneau, 0x51D5A7 pour l'icône de tier) NE PREND PAS de w/h explicite
// — les dimensions viennent de la texture .IMG chargée (panneau unk_931680 / icône de
// tier unk_931714…), donc NON prouvables statiquement sans charger l'asset. Seuls
// L'ORIGINE (x,y) et LE PAS (44px) sont extraits du binaire (fidèles) ; kNominalW/kNominalH
// ci-dessous ne servent QUE de repli de hit-test si le sprite de fiche (slot 2013) n'est pas
// chargeable — sinon la taille NATIVE de ce sprite est utilisée (cf. corps ci-dessous).
RECT LoginScene::CharSlotRect(int i) const {
    constexpr int kRightMargin = 194; // nWidth - 0xC2, EA 0x51D4EC
    constexpr int kTop         = 19;  // var_418 = 0x13, EA 0x51D4F5
    constexpr int kRowOffset   = 34;  // +0x22, EA 0x51D599/0x51D62C/0x51D5DB/0x51D6DA
    constexpr int kRowStep     = 44;  // imul ..., 2Ch, EA 0x51D590
    constexpr int kNominalW = 176, kNominalH = 44; // gabarit de repli (dims réelles = celles
                                                    // du sprite .IMG, non extractibles statiquement)
    const int x = screenW_ - kRightMargin;              // panneauX (= x du blit CharListRender)
    const int y = kTop + kRowOffset + i * kRowStep;     // y_i = 53 / 97 / 141
    // Dimensions = taille NATIVE du sprite de fiche (slot 2013) quand il est chargeable, pour
    // que le hit-test colle EXACTEMENT au blit de CharListRender ; sinon gabarit nominal.
    // GetAtlasSprite alimente un cache membre (paresseux) -> const_cast (le RECT reste une
    // valeur pure). Les 4 tiers partagent la même géométrie de fiche -> 2013 suffit ici.
    int w = kNominalW, h = kNominalH;
    if (gfx::GpuTexture* t = const_cast<LoginScene*>(this)->GetAtlasSprite(2013)) {
        if (t->Width() > 0 && t->Height() > 0) {
            w = static_cast<int>(t->Width());
            h = static_cast<int>(t->Height());
        }
    }
    return MakeRect(x, y, w, h);
}

// Écran Liste : colonne principale de boutons (Docs/TS2_CHARSELECT_RE.md §7). Origine
// v126 = nWidth-140, v117 = nHeight-301 ; pas vertical de 37 px. Les boutons de cette
// colonne sont rendus/hit-testés aux mêmes offsets que Scene_CharSelectRender/
// OnMouseDown/Up : RESTAURER v117-37 (EA 0x51E354/0x525B46), ENTRER v117 (0x51DF53/
// 0x52508E), CRÉER v117+37 (0x51DFEB/0x52522A), SUPPRIMER v117+74 (0x51E079/
// 0x52545C), RETOUR v117+185 (0x51E225/0x525A1F). RENOMMER (v117+111), ENTREPÔT
// (v117+148) et QUITTER (v117+222) restent dessinés en sprites directs.
void LoginScene::LayoutCharSelect() {
    // Hit rect = gabarit nominal (dims réelles = celles du sprite .IMG, non extractibles
    // statiquement) ; borné sous le pas de 37 px pour éviter le recouvrement des zones.
    constexpr int kColBtnW = 128, kColBtnH = 34;
    const int v126 = screenW_ - 140;
    const int v117 = screenH_ - 301;
    restoreBtn_.SetBounds(v126, v117 - 37,  kColBtnW, kColBtnH); // RESTAURER (3086/3087/3088), EA 0x51E354
    enterBtn_.SetBounds  (v126, v117,       kColBtnW, kColBtnH); // ENTRER    (16/17/18),       EA 0x51DF53
    createBtn_.SetBounds (v126, v117 + 37,  kColBtnW, kColBtnH); // CRÉER     (19/20/21),       EA 0x51DFEB
    deleteBtn_.SetBounds (v126, v117 + 74,  kColBtnW, kColBtnH); // SUPPRIMER (22/23/24),       EA 0x51E079
    backBtn_.SetBounds   (v126, v117 + 185, kColBtnW, kColBtnH); // RETOUR    (963/964/965),    EA 0x51E225
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

// AUDIT (2026-07-14, décompilation + désassemblage directs de Scene_CharSelectRender
// 0x51CED0 — 473 blocs, 151 appels Sprite2D_Draw/DrawScaled, 40 StrTable005_Get, EA
// 0x51CED0-0x520F40) — écarts CONFIRMÉS entre le binaire et cette implémentation :
//
// 1) DISPATCH RÉEL À 3 BRANCHES, pas 2 : le binaire teste `this[+0xF588]` (EA 0x51D4CB)
//    ==1 -> Liste (0x51D4E6), ==2 -> Formulaire (0x51E4A1), SINON -> 3e branche (0x51ECC5).
//    3e branche ÉLUCIDÉE (front W4-F1) : `screen∉{1,2}` -> `if (this[+0xF03C]==0) goto
//    0x51F895` (popups communs de fin) `else` overlay MOT DE PASSE SECONDAIRE / PIN — le
//    compte-annexe/GM piloté par dword_16692A4 / this[15375] (this[+0xF03C]). Sous-états
//    this[+0xF040] 1/2/3 (0x51ED12 / 0x51F0DB / 0x51F3DD), masque '*' de longueur
//    this[+0xF048], pavé PIN, sprites unk_93EF4C. CharSelectFlow.h déclare CE système
//    EXPLICITEMENT NON REPRODUIT (hors périmètre) et game::CharSelectScreen ne modélise que
//    {List, CreateForm} : l'état PIN (this[15375]/15376/longueur saisie) n'existe PAS dans
//    game::CharSelectState. -> TODO ancre (nécessiterait d'ajouter l'état PIN à
//    CharSelectState, fichier possédé par un autre front) — PAS de code ici. CharSelectRender()
//    ci-dessous ne modélise donc que 2 écrans (CreateForm/Liste) + l'overlay deleteConfirmOpen_.
//
// 2) APERÇU 3D DU PERSONNAGE : le binaire appelle `Char_RenderModel` (0x527020 —
//    « assemblage du paperdoll joueur pièce par pièce via SObject_DrawEx ... appelé
//    UNIQUEMENT depuis Scene_CharSelectRender (4x) ») QUATRE fois avant le 1er Gfx_Begin2D
//    (0x51D48A) : Liste EA 0x51D361/0x51D3CC (fiche &unk_1669380+0x2768*selectedSlot),
//    CreateForm EA 0x51D429/0x51D480 (fiche d'aperçu dword_16709B8) — en mode 3D, PAS en
//    sprite 2D. La CAMÉRA de preview est désormais modélisée (kCharPreviewEye/Target,
//    0x51BDED) et la RECETTE 3D complète est posée en TODO ancre au début de CharListRender()
//    et CreateFormRender() (résolution paperdoll disponible en lecture seule via
//    gfx::PlayerPaperdoll::Resolve, W3). Le CÂBLAGE reste BLOQUÉ : il exige des membres 3D
//    persistants (MeshRenderer/ModelCache/MotionCache/Camera + OnDeviceLost/Reset) et une
//    entrée begin-frame (Gfx_BeginFrame 0x6A2280) vivant dans LoginScene.h, NON possédé par
//    ce front (W4-F1) ; Scene/WorldRenderer est interdit d'édition. -> front possédant ces
//    membres 3D.
//
// 3) Habillage sprite de la Liste (panneau + icône de tier + surlignage + niveau
//    numérique par slot) confirmé et documenté EA par EA dans CharSlotRect() ci-dessous —
//    non câblé (repli FillRect aplat uniquement).
//
// 4) Titre "Selection du personnage" / ligne "Compte : %s" : AUCUN appel StrTable005_Get
//    ni aucun Font_Draw identifié pour un titre de la Liste dans la plage EA 0x51D2A0-
//    0x51D7C4 inspectée (les 40 appels StrTable005_Get de la fonction sont TOUS dans la
//    plage 0x51E571-0x51FEF7, exclusivement pour les VALEURS du formulaire de création,
//    cf. commentaire de CreateFormRender()). Le panneau réel (unk_931680) porte
//    vraisemblablement son propre titre peint dans la texture (même motif que le panneau
//    Login "Connexion", cf. LoginRender()) — ce texte libre ICI est donc, comme les
//    libellés de boutons (cf. Init()), un texte de repli TEMPORAIRE probablement en trop
//    une fois le vrai sprite panneau câblé (superposition potentielle avec un titre déjà
//    peint dans l'image) : à retirer/repositionner dès que unk_931680 (ou l'asset .IMG
//    correspondant) sera chargé.
void LoginScene::CharSelectRender() {
    if (charState_.screen == game::CharSelectScreen::CreateForm) CreateFormRender();
    else                                                          CharListRender();
    if (deleteConfirmOpen_) DeleteConfirmRender(); // overlay, cf. host.ShowDeleteConfirm
}

// Écran Liste — CÂBLAGE 1:1 depuis les vraies assets .IMG de l'atlas 001
// (Docs/TS2_CHARSELECT_RE.md §4/§5.1/§7). Plus AUCUN aplat FillRect ni libellé inventé :
//   - panneau liste (slot 2012) @ (nWidth-194, 19), blit natif non étiré ;
//   - fiches des emplacements OCCUPÉS (slot vide -> rien) : fond de fiche par tier
//     (renaissance niveau>=113 -> 2018, sinon normal -> 2013) @ (panneauX, y_i), avec
//     surbrillance de sélection (slot 1657) @ (panneauX+25, y_i+16) sur selectedSlot, et
//     la VALEUR de niveau centrée en texte à (panneauX+54, y_i+17) et (panneauX+118, y_i+17) ;
//   - colonne de boutons (slots dédiés câblés en Init()) @ (v126, v117 + offset).
// POLITIQUE ZÉRO REPLI (comme LoginRender) : un sprite qui ne charge pas -> rien dessiné.
void LoginScene::CharListRender() {
    LayoutCharSelect();
    const POINT mp = CursorClient();
    createBtn_.OnMouseMove(mp.x, mp.y);
    deleteBtn_.OnMouseMove(mp.x, mp.y);
    enterBtn_.OnMouseMove(mp.x, mp.y);
    backBtn_.OnMouseMove(mp.x, mp.y);
    restoreBtn_.OnMouseMove(mp.x, mp.y);

    // Ancres réelles (doc §5.1 / §7).
    constexpr int kPanelTop = 19;         // panneau liste unk_931680 @ (nWidth-194, 19)
    const int panneauX = screenW_ - 194;
    const int v126     = screenW_ - 140;  // origine colonne boutons
    const int v117     = screenH_ - 301;

    // TODO(ancre) APERÇU 3D — Scene_CharSelectRender 0x51CED0, 2x Char_RenderModel 0x527020
    // AVANT le 1er Gfx_Begin2D (0x51D48A), écran Liste : EA 0x51D361 (pass=1,isCreate=0) et
    // 0x51D3CC (pass=2,isCreate=0), sur la fiche sélectionnée &unk_1669380 + 0x2768*this[15715]
    // (charState_.selectedSlot). Caméra : œil kCharPreviewEye / cible kCharPreviewTarget
    // (Scene_CharSelectUpdate EA 0x51BDED). Chaque pièce -> SObject_DrawEx(desc, pass, animTime,
    // pos, rotY, 20.0, palette, 1) avec palette d'os partagée v37 =
    // PcModel_ResolveEquipSlot 0x4E46A0 (== PcModel_ResolveSlotAndApply 0x4E5A00), animTime
    // échantillonné par g_GameTimeSec (idiome ftol(t*30)%frameCount, Char_RenderModel 0x528D38).
    // Résolution DISPONIBLE en lecture seule : gfx::PlayerPaperdoll::Resolve (Gfx/PlayerPaperdoll.h,
    // W3) -> {palette, pièces SLOT0/SLOT1/[arme]} depuis (race, gender, costume0, costume1,
    // weaponItemId, gameTimeSec). Paramètres de la fiche = charState_.slots[selectedSlot]
    // (job@36/faction@44/face@48/hairColor@52 exposés ; race/gender/costume/arme PAS encore
    // exposés par LoadCharacterSlots -> état possédé par un autre front).
    // BLOQUÉ ICI : le câblage 3D exige des membres PERSISTANTS possédés (gfx::MeshRenderer +
    // gfx::ModelCache + gfx::MotionCache + gfx::Camera + hooks OnDeviceLost/Reset) et une entrée
    // begin-frame 3D (Gfx_BeginFrame 0x6A2280) qui vivent dans LoginScene.h (NON possédé par ce
    // front) ; Scene/WorldRenderer est interdit d'édition. -> à câbler par le front possédant
    // ces membres 3D. Ne pas toucher Scene/WorldRenderer.

    if (sprites_.Ready()) {
        sprites_.Begin();
        DrawFullscreenBg(charBgSlot_); // fond RÉEL CharSelect (slot 2383/2384/2385) — zéro aplat

        // Panneau de la liste (slot 2012) : blit non étiré à sa taille native.
        if (gfx::GpuTexture* t = GetAtlasSprite(2012))
            sprites_.DrawSprite(t->Handle(), nullptr, panneauX, kPanelTop, gfx::kSpriteWhite);

        // 3 fiches perso : uniquement les emplacements occupés.
        for (int i = 0; i < game::kMaxCharSlots; ++i) {
            const game::CharSlotInfo& slot = charState_.slots[static_cast<size_t>(i)];
            if (!slot.occupied) continue;            // slot vide -> pas de fiche dessinée
            const RECT r = CharSlotRect(i);          // fiche @ (panneauX, y_i), EA 0x51D5A7 ; hit sélection = r+(25,16)
            // Fond de fiche selon le tier (doc §5.1). Décidables ici : normal (2013) et
            // renaissance (niveau>=113 -> 2018). Les tiers intermédiaire (2729, rec+0x3C) et
            // max (4343, rec+0x164C) dépendent de champs de fiche NON exposés par
            // game::CharSlotInfo -> TODO (câbler dès que LoadCharacterSlots exposera ces offsets).
            const int ficheSlot = (slot.power >= 113) ? 2018 : 2013;
            if (gfx::GpuTexture* t = GetAtlasSprite(ficheSlot))
                sprites_.DrawSprite(t->Handle(), nullptr, r.left, r.top, gfx::kSpriteWhite);
            // Surbrillance de sélection (slot 1657) @ (panneauX+25, y_i+16).
            if (i == charState_.selectedSlot) {
                if (gfx::GpuTexture* t = GetAtlasSprite(1657))
                    sprites_.DrawSprite(t->Handle(), nullptr, r.left + 25, r.top + 16, gfx::kSpriteWhite);
            }
        }

        // Boutons LATCHÉS de la colonne principale : sprites idle/hover/pressé dédiés câblés
        // en Init() ; DrawSkin dessine l'état courant à la taille native du sprite.
        restoreBtn_.DrawSkin(sprites_); // RESTAURER @ (v126, v117-37), Scene_CharSelectRender 0x51E370/0x51E38A/0x51E3A4
        enterBtn_.DrawSkin(sprites_);
        createBtn_.DrawSkin(sprites_);
        deleteBtn_.DrawSkin(sprites_);
        backBtn_.DrawSkin(sprites_);

        // RENOMMER / ENTREPÔT / QUITTER : pas de membre Button -> dessinés en sprites DIRECTS
        // à l'état idle (doc §7). QUITTER (slot 25) est désormais HIT-TESTÉ dans
        // CharSelectOnMouseUp (rect direct sur ce sprite -> game::OnQuitButtonClicked, EA
        // 0x525AA5) ; RENOMMER (this[15706], ticket item 1133) / ENTREPÔT (this[15396],
        // Net_ReqStorageList) restent hors périmètre (latch/action non portés — TODO).
        auto drawIdleSprite = [&](int slot, int px, int py) {
            if (gfx::GpuTexture* t = GetAtlasSprite(slot))
                sprites_.DrawSprite(t->Handle(), nullptr, px, py, gfx::kSpriteWhite);
        };
        drawIdleSprite(1812, v126, v117 + 111); // RENOMMER (idle, hors périmètre)
        drawIdleSprite(1925, v126, v117 + 148); // ENTREPÔT (idle, hors périmètre)
        drawIdleSprite(25,   v126, v117 + 222); // QUITTER  (idle ; hit-test en OnMouseUp)
        sprites_.End();
    }

    if (font_.Ready()) {
        font_.BeginBatch();
        // VALEUR de niveau par fiche occupée (doc §5.1) : deux nombres CENTRÉS à
        // (panneauX+54, y_i+17) et (panneauX+118, y_i+17). Valeur = niveau brut, sauf tier
        // renaissance (niveau>=113 affiché niveau-112). Seul game::CharSlotInfo.power (fiche
        // +0x38 = dword_16693B8 = niveau, doc §5.2) est exposé -> la même valeur est rendue
        // aux deux emplacements (le binaire y dessine deux valeurs via UI_DrawNumberValue ;
        // la seconde n'est pas décidable statiquement -> TODO). Centrage via MeasureText
        // (repli du UI_MeasureNumberText/police bitmap unk_1685740 d'origine).
        for (int i = 0; i < game::kMaxCharSlots; ++i) {
            const game::CharSlotInfo& slot = charState_.slots[static_cast<size_t>(i)];
            if (!slot.occupied) continue;
            const RECT r = CharSlotRect(i);
            const int lvl = (slot.power >= 113) ? (slot.power - 112) : slot.power;
            char num[16];
            std::snprintf(num, sizeof(num), "%d", lvl);
            const int halfW = font_.MeasureText(num) / 2;
            font_.DrawTextAt(num, r.left + 54  - halfW, r.top + 17, kColText);
            font_.DrawTextAt(num, r.left + 118 - halfW, r.top + 17, kColText);
        }
        font_.EndBatch();
    }
}

// Écran Formulaire de création — CÂBLAGE 1:1 sur les vrais sprites d'atlas (front W4-F1,
// Scene_CharSelectRender 0x51CED0, branche screen==2 EA 0x51E4A1). Plus AUCUN aplat FillRect,
// AUCUN titre/libellé FR fabriqué : le binaire ne dessine QUE le panneau (slot 40), les
// boutons -/+ (slots 41/42) baked, Confirmer/Annuler (slots 9/12), le caret (slot 43) et les
// 5 VALEURS localisées + le nom. Aucune chaîne "Créer/Nom/Classe/…" n'existe dans l'exe.
void LoginScene::CreateFormRender() {
    LayoutCreateForm();
    const POINT mp = CursorClient();
    Button* formBtns[] = {
        &jobMinusBtn_, &jobPlusBtn_, &factionMinusBtn_, &factionPlusBtn_,
        &faceMinusBtn_, &facePlusBtn_, &hairMinusBtn_, &hairPlusBtn_,
        &variantMinusBtn_, &variantPlusBtn_, &createConfirmBtn_, &createCancelBtn_,
    };
    for (Button* b : formBtns) b->OnMouseMove(mp.x, mp.y);

    // Ancres RÉELLES du panneau de création (EA 0x51E4A1 sub 0x14F / 0x51E4B0 = 0x49).
    const int panelX = screenW_ - 335, panelY = 73;

    // TODO(ancre) APERÇU 3D — Scene_CharSelectRender 0x51CED0, 2x Char_RenderModel 0x527020
    // AVANT le 1er Gfx_Begin2D (0x51D48A), écran CreateForm : EA 0x51D429 (pass=1,isCreate=1)
    // et 0x51D480 (pass=2,isCreate=1), sur la fiche d'aperçu de création &dword_16709B8.
    // Caméra œil kCharPreviewEye / cible kCharPreviewTarget (Scene_CharSelectUpdate 0x51BDED) ;
    // palette d'os partagée v37 = PcModel_ResolveEquipSlot 0x4E46A0 (== PcModel_ResolveSlotAndApply
    // 0x4E5A00), animTime par g_GameTimeSec (idiome ftol(t*30)%frameCount). Résolution en lecture
    // seule : gfx::PlayerPaperdoll::Resolve (Gfx/PlayerPaperdoll.h, W3). Source d'apparence = la
    // fiche d'aperçu dword_16709B8 (construite depuis charState_.createForm) — NON répliquée dans
    // game::CharCreateForm (état possédé par un autre front).
    // BLOQUÉ ICI : mêmes membres 3D possédés manquants (MeshRenderer/ModelCache/MotionCache/
    // Camera + OnDeviceLost/Reset, entrée begin-frame Gfx_BeginFrame 0x6A2280) dans LoginScene.h
    // (NON possédé par ce front). Ne pas toucher Scene/WorldRenderer.

    if (sprites_.Ready()) {
        sprites_.Begin();
        DrawFullscreenBg(charBgSlot_); // fond RÉEL CharSelect (slot 2383/2384/2385) — zéro aplat
        // Panneau de création (slot 40 unk_8EA270) @ (panelX, panelY) — blit natif, EA 0x51E4C5.
        if (gfx::GpuTexture* t = GetAtlasSprite(40))
            sprites_.DrawSprite(t->Handle(), nullptr, panelX, panelY, gfx::kSpriteWhite);
        // Boutons -/+ (slots 41/42 skinnés en Init) + Confirmer/Annuler (slots 9/12) : DrawSkin
        // dessine l'état courant à la taille native du sprite.
        for (Button* b : formBtns) b->DrawSkin(sprites_);
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
}

// Confirmation Oui/Non de suppression (host.ShowDeleteConfirm), overlay au-dessus
// de l'écran Liste. "Oui" -> ConfirmDeleteCharacter (opcode 18) ; "Non" referme.
void LoginScene::DeleteConfirmRender() {
    const int bw = 300, bh = 110;
    const int bx = screenW_ / 2 - bw / 2, by = screenH_ / 2 - bh / 2;
    deleteYesBtn_.SetBounds(bx + 40,          by + bh - 40, 64, 24);
    deleteNoBtn_.SetBounds(bx + bw - 104,     by + bh - 40, 64, 24);
    const POINT mp = CursorClient();
    deleteYesBtn_.OnMouseMove(mp.x, mp.y);
    deleteNoBtn_.OnMouseMove(mp.x, mp.y);

    if (sprites_.Ready()) {
        sprites_.Begin();
        FillRect(0, 0, screenW_, screenH_, 0x88000000);       // voile modal
        FillRect(bx - 2, by - 2, bw + 4, bh + 4, kColPanelEdge);
        FillRect(bx, by, bw, bh, kColPanel);
        // Oui/Non : sprites réels "Confirm"/"Cancel" réutilisés (cf. ApplyConfirmCancelSkin) —
        // même paire générique que Login/CharSelect, mapping sémantique Oui=Confirm/Non=Cancel.
        deleteYesBtn_.DrawSkin(sprites_);
        deleteNoBtn_.DrawSkin(sprites_);
        sprites_.End();
    }
    if (font_.Ready()) {
        font_.BeginBatch();
        font_.DrawTextAt("Supprimer ce personnage ?", bx + 24, by + 30, kColText);
        if (!deleteYesBtn_.HasAnySkin())
            font_.DrawTextAt(deleteYesBtn_.Label().c_str(), deleteYesBtn_.X() + 20, deleteYesBtn_.Y() + 5, kColText);
        if (!deleteNoBtn_.HasAnySkin())
            font_.DrawTextAt(deleteNoBtn_.Label().c_str(), deleteNoBtn_.X() + 20, deleteNoBtn_.Y() + 5, kColText);
        font_.EndBatch();
    }
}

// Scene_CharSelectOnMouseDown 0x520F40 : arme les latches (boutons) / sélectionne
// un emplacement au clic direct (le binaire sélectionne aussi au down, cf. header
// CharSelectFlow.h). La validation des boutons (envoi réseau) a lieu au « up »
// dans CharSelectOnMouseUp, motif identique à Login.
void LoginScene::CharSelectOnMouseDown(int x, int y) {
    if (deleteConfirmOpen_) {
        deleteYesBtn_.OnMouseDown(x, y);
        deleteNoBtn_.OnMouseDown(x, y);
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
        return;
    }

    LayoutCharSelect();
    for (int i = 0; i < game::kMaxCharSlots; ++i) {
        const RECT row = CharSlotRect(i);
        int hw = 64, hh = 28; // repli : zone de surbrillance si l'asset manque
        if (gfx::GpuTexture* t = GetAtlasSprite(1657)) {
            if (t->Width() > 0 && t->Height() > 0) {
                hw = static_cast<int>(t->Width());
                hh = static_cast<int>(t->Height());
            }
        }
        // Hit-test de sélection = sprite unk_924944 (slot 1657) @ (panneauX+25, 19+50+i*44),
        // pas toute la fiche : Scene_CharSelectOnMouseDown 0x520F40, EA 0x522688.
        const RECT hit = MakeRect(row.left + 25, row.top + 16, hw, hh);
        if (RectContains(hit, x, y)) {
            game::SelectCharacterSlot(charState_, i);
            return;
        }
    }
    restoreBtn_.OnMouseDown(x, y); // RESTAURER : Scene_CharSelectOnMouseDown EA 0x522935 (slot 3086)
    createBtn_.OnMouseDown(x, y);
    deleteBtn_.OnMouseDown(x, y);
    enterBtn_.OnMouseDown(x, y);
    backBtn_.OnMouseDown(x, y);
}

// Scene_CharSelectOnMouseUp 0x522E50 : porte la quasi-totalité de la logique
// métier réelle (validation + requêtes réseau) — ici déléguée aux callbacks
// SetOnClick câblés en Init() (game::OnXxxButtonClicked/ConfirmXxx).
void LoginScene::CharSelectOnMouseUp(int x, int y) {
    if (deleteConfirmOpen_) {
        deleteYesBtn_.OnMouseUp(x, y);
        deleteNoBtn_.OnMouseUp(x, y);
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
    restoreBtn_.OnMouseUp(x, y); // RESTAURER : Scene_CharSelectOnMouseUp EA 0x525B46 (action restauration hors flux porté)
    createBtn_.OnMouseUp(x, y);
    deleteBtn_.OnMouseUp(x, y);
    enterBtn_.OnMouseUp(x, y);
    backBtn_.OnMouseUp(x, y);

    // QUITTER (unk_8E99C4 = slot 25, colonne @v117+222) : Scene_CharSelectOnMouseUp EA
    // 0x525AA5 hit-test -> si this[+0xF598]!=3 (pas en cours d'entrée), UI_MsgBox_Open de
    // confirmation de quit (EA 0x525AE5). Contrairement à RETOUR/ENTRER/CRÉER/SUPPRIMER, ce
    // bouton n'a PAS de membre Button (LoginScene.h non possédé par ce front) -> hit-test
    // DIRECT du rect du sprite (mêmes ancres que le drawIdleSprite de CharListRender).
    // game::OnQuitButtonClicked porte déjà la garde PreviewMotion::Entering et fait
    // Net_CloseSocket + g_QuitFlag=1 (simplification assumée vs la MsgBox du binaire).
    if (gfx::GpuTexture* t = GetAtlasSprite(25)) {
        const RECT r = MakeRect(screenW_ - 140, (screenH_ - 301) + 222,
                                static_cast<int>(t->Width()), static_cast<int>(t->Height()));
        if (RectContains(r, x, y)) { game::OnQuitButtonClicked(charState_, charHost_); return; }
    }
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
    charHost_.ShowNotice = [this](int32_t strId) {
        // StrTable005_Get(g_LangId, strId) 0x4C1D20 — g_Strings.messages est charge par
        // App::Init AVANT toute scene (cf. App.cpp::Init, LoadStringTables), donc le texte
        // reel est deja disponible ici (plus de repli "#id" — cf. game::Str()).
        OpenNotice(game::Str(strId).c_str());
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
    charHost_.ShowDeleteConfirm = [this] { deleteConfirmOpen_ = true; };
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
        (void)domainId; // Net_SelectServerDomain : table de rotation d'hôtes non
                        // reconstruite ici (même TODO que Net/GameHandlers_Misc.cpp,
                        // opcode 0x18) — on réutilise l'hôte du canal login sélectionné ;
                        // le PORT, lui, vient bien de la réponse serveur réelle (opcode
                        // 22), plus du placeholder jamais renseigné de l'ancien code
                        // (cf. audit §2.5).
        if (!net_) return net::kNetErrSocketSend;
        std::string host = net::kLoginHostCom;
        {
            std::lock_guard<std::mutex> lk(serverMutex_);
            const int idx = serverState_.selectedServer;
            if (idx >= 0 && idx < static_cast<int>(serverState_.servers.size()))
                host = serverState_.servers[static_cast<size_t>(idx)].name; // name == host (cf. BuildServerList)
        }
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

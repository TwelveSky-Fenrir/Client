// UI/Widgets.h — widgets 2D réutilisables du shell client TwelveSky2 (ts2::ui).
//
// Réimplémentation PRAGMATIQUE du socle « framework UI » de TwelveSky2, dessiné
// par-dessus la couche Gfx (SpriteBatch + Font). Voir Docs/TS2_CLIENT_SHELL.md §2.2.
//
// Le binaire d'origine n'est PAS un système objet : c'est un registre statique de
// ~38 dialogues singletons pilotés par des fonctions libres __thiscall, et la
// saisie texte passe par 21 EDIT Win32 natifs sous-classés sur
// UI_EditBoxWndProc (0x50E070). On reconstruit ici des widgets AUTONOMES qui
// se dessinent eux-mêmes (pas d'EDIT natif), tout en restant fidèles aux
// comportements observés :
//   - masque mot de passe = caractère '*' (0x2A / 42), cf. Scene_LoginRender
//     0x51B020 : Crt_Memset(String, 42, len) avant dessin.
//   - caret dessiné uniquement quand le champ a le focus (sprite unk_8EA42C).
//     L'original NE clignote PAS ; on ajoute un clignotement optionnel (demandé)
//     désactivable via SetCaretBlink(false) pour rester fidèle au binaire.
//   - navigation Tab -> champ suivant, Entrée -> soumission contextuelle
//     (UI_EditBoxWndProc : Tab=UI_FocusEditBox, Entrée=UI_Chat_SubmitInput...).
//   - boutons : latch armé au clic-enfoncé, validé au relâchement (pattern
//     btnPressed[] des dialogues, cf. UI_MsgBox_OnLButtonDown/Up).
//
// Contrat de rendu : Draw(SpriteBatch&, Font&) suppose que les DEUX lots sont
// déjà OUVERTS par l'appelant (SpriteBatch::Begin() et Font::BeginBatch()), car
// ID3DXSprite et ID3DXFont portent chacun leur propre batch. Les widgets ne
// gèrent pas l'ouverture/fermeture — ils émettent seulement leurs primitives.
#pragma once
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"

#include <windows.h>   // RECT, VK_*
#include <d3d9.h>      // IDirect3DTexture9, D3DCOLOR
#include <cstdint>
#include <string>
#include <functional>

namespace ts2::ui {

// Couleurs par défaut (ARGB) — blanc opaque et gris « désactivé ».
inline constexpr D3DCOLOR kTextWhite    = 0xFFFFFFFFu;
inline constexpr D3DCOLOR kTextDisabled = 0xFF808080u;

// Caractère de masquage des mots de passe (fidèle : Crt_Memset(buf, 42, len)).
inline constexpr char kPasswordMaskChar = '*'; // 0x2A / 42

// Période de clignotement du caret (secondes) : 0.5 s allumé / 0.5 s éteint.
inline constexpr float kCaretBlinkPeriodSec = 1.0f;

// Alignement horizontal du texte d'un Label.
enum class Align { Left, Center, Right };

// ---------------------------------------------------------------------------
// WidgetSprite — référence NON-possédante vers une texture GPU + sous-rectangle
// optionnel. La texture appartient à l'atlas/skin chargé ailleurs (ex.
// gfx::GpuTexture depuis un .IMG G03_GDATA) ; on ne stocke que le handle brut.
//   Usage : ui::WidgetSprite{ gpuTex.Handle() }               // texture entière
//           ui::WidgetSprite{ gpuTex.Handle(), RECT{l,t,r,b} } // sous-région
struct WidgetSprite {
    IDirect3DTexture9* tex    = nullptr; // handle non-possédé (GpuTexture::Handle())
    RECT               src    {};        // sous-rectangle source (si useSrc)
    bool               useSrc = false;   // false -> blit de la texture entière

    WidgetSprite() = default;
    WidgetSprite(IDirect3DTexture9* t) : tex(t) {}                 // texture entière
    WidgetSprite(IDirect3DTexture9* t, const RECT& r)             // sous-région
        : tex(t), src(r), useSrc(true) {}

    bool          Valid() const { return tex != nullptr; }
    const RECT*   SrcPtr() const { return useSrc ? &src : nullptr; }
};

// ---------------------------------------------------------------------------
// Widget — base commune : rectangle écran, visibilité, activation, hit-test.
// (Le binaire garde x/y/bOpen en tête de chaque struct de dialogue ; on formalise.)
class Widget {
public:
    virtual ~Widget() = default;

    void SetBounds(int x, int y, int w, int h) { x_ = x; y_ = y; w_ = w; h_ = h; }
    void SetPos(int x, int y)                  { x_ = x; y_ = y; }
    void SetSize(int w, int h)                 { w_ = w; h_ = h; }

    int  X() const { return x_; }
    int  Y() const { return y_; }
    int  W() const { return w_; }
    int  H() const { return h_; }

    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const    { return visible_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const    { return enabled_; }

    // Point dans le rectangle du widget (visible uniquement). Fidèle à
    // Sprite2D_HitTest 0x4D6C50 : test position + dimensions du sprite.
    bool HitTest(int mx, int my) const {
        return visible_ && mx >= x_ && mx < x_ + w_ && my >= y_ && my < y_ + h_;
    }

protected:
    int  x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    bool visible_ = true;
    bool enabled_ = true;
};

// ---------------------------------------------------------------------------
// Label — texte statique dessiné via Font (style normal/ombre/contour).
// Aligné dans le rectangle du widget (Left/Center/Right) ; centré verticalement
// si une hauteur est fournie. Correspond aux libellés StrTable005 des dialogues.
class Label : public Widget {
public:
    Label() = default;
    explicit Label(std::string text) : text_(std::move(text)) {}

    void SetText(std::string t)        { text_ = std::move(t); }
    const std::string& Text() const    { return text_; }

    void SetColor(D3DCOLOR c)          { color_ = c; }
    void SetStyle(int mode)            { style_ = mode; } // gfx::kStyle*
    void SetAlign(Align a)             { align_ = a; }
    void SetTextHeight(int h)          { textH_ = h; }

    // sb : inutilisé (le Label ne dessine que du texte) — présent pour l'uniformité
    // du contrat Draw(SpriteBatch&, Font&).
    void Draw(gfx::SpriteBatch& sb, gfx::Font& font);

private:
    std::string text_;
    D3DCOLOR    color_ = kTextWhite;
    int         style_ = gfx::kStyleNormal;
    Align       align_ = Align::Left;
    int         textH_ = 12; // hauteur de police par défaut (App_Init : Height=12)
};

// ---------------------------------------------------------------------------
// Button — bouton sprité 3 états (normal/survol/pressé) + libellé optionnel.
// Modèle d'interaction fidèle aux dialogues : latch armé sur clic-enfoncé
// (OnMouseDown), action déclenchée au relâchement DANS le bouton (OnMouseUp).
class Button : public Widget {
public:
    void SetSkin(const WidgetSprite& normal,
                 const WidgetSprite& hover,
                 const WidgetSprite& pressed) {
        normal_ = normal; hover_ = hover; pressed_ = pressed;
    }
    void SetNormal(const WidgetSprite& s)  { normal_ = s; }
    void SetHover(const WidgetSprite& s)   { hover_ = s; }
    void SetPressed(const WidgetSprite& s) { pressed_ = s; }

    void SetLabel(std::string t)        { label_ = std::move(t); }
    const std::string& Label() const    { return label_; }
    void SetLabelColor(D3DCOLOR c)      { labelColor_ = c; }
    void SetLabelStyle(int mode)        { labelStyle_ = mode; }
    void SetTextHeight(int h)           { textH_ = h; }

    // Callback déclenché quand le bouton est validé (clic complet à l'intérieur).
    void SetOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }

    // Événements souris. Renvoient true si l'événement concerne le bouton.
    void OnMouseMove(int mx, int my);
    bool OnMouseDown(int mx, int my); // arme le latch si dans le bouton
    bool OnMouseUp(int mx, int my);   // valide (fire onClick) si relâché dedans

    bool Hovered() const { return hover_active_; }
    bool Pressed() const { return armed_; }

    // Indique si CE BOUTON a été câblé avec AU MOINS UNE texture réelle (normal/survol/
    // pressée), quel que soit l'état visuel COURANT. Distinct d'un ancien test « l'état
    // affiché maintenant a-t-il une texture » (renommé/recorrigé, audit Login du
    // 2026-07-14, Docs/TS2_LOGINSCENE_AUDIT.md §4) : depuis la correction de fidélité de
    // Scene_LoginRender 0x51B020 (EA 0x51B48D/0x51B50F — le sprite unk_8E9084/unk_8E9240
    // n'est dessiné QUE si la souris survole le bouton ; il n'existe PAS de sprite "normal"
    // idle, cf. ApplyConfirmCancelSkin qui ne câble plus que Hover+Pressed), okBtn_/
    // exitBtn_ n'ont plus AUCUNE texture tant qu'ils ne sont ni survolés ni enfoncés —
    // tester l'état courant aurait fait réapparaître à tort le libellé de repli
    // ("OK"/"Quitter") dès que la souris quitte le bouton, alors que le binaire n'affiche
    // ni sprite ni texte dans ce cas (le libellé réel, s'il existe, est peint dans le
    // panneau lui-même). Le libellé de repli ne doit s'afficher QUE quand AUCUNE des 3
    // textures n'a pu être chargée (fichier .IMG réellement absent/illisible).
    bool HasAnySkin() const {
        return normal_.Valid() || hover_.Valid() || pressed_.Valid();
    }

    // Efface le latch enfoncé + le survol (fidèle au RAZ générique de champs de
    // cSceneMgr fait par les scènes à l'entrée d'un sous-état Init, ex. Scene_LoginUpdate
    // 0x51A8D0 case 0 : `for(i=0;i<150;++i) a1[i+3]=0;` couvre entre autres les 3 latches
    // de boutons this[3..5]). À appeler explicitement par la scène propriétaire du bouton
    // au moment de son propre reset ; ce widget ne se réinitialise jamais tout seul.
    void Reset() { armed_ = false; hover_active_ = false; }

    // ---- Repli couleur (rect plein), utilisé UNIQUEMENT quand le skin (normal_/hover_/
    // pressed_) sélectionné pour l'état courant est invalide (aucune texture fournie via
    // SetNormal/SetHover/SetPressed, ou fichier .IMG réellement absent/illisible au
    // runtime). Comportement par défaut (fallbackTex_ == nullptr) : STRICTEMENT identique
    // à l'ancien Draw() (aucun rect dessiné si le skin est invalide) — n'affecte AUCUN
    // appelant existant tant qu'il n'appelle pas explicitement ces setters. Cf.
    // Docs/TS2_LOGIN_BUTTON_ASSETS.md (LoginScene::okBtn_/exitBtn_, textures réelles avec
    // repli sur le rect coloré déjà en place).
    void SetFallbackColors(D3DCOLOR normalCol, D3DCOLOR hoverCol, D3DCOLOR pressedCol) {
        fallbackNormal_ = normalCol; fallbackHover_ = hoverCol; fallbackPressed_ = pressedCol;
    }
    // Texture 1x1 blanche servant à dessiner le repli (le Button ne possède pas sa propre
    // texture : l'appelant fournit celle déjà créée pour son propre usage, ex.
    // LoginScene::whiteTex_ / InventoryWindow::whiteTex_ — même technique que FillRect).
    void SetFallbackTexture(IDirect3DTexture9* whiteTex) { fallbackTex_ = whiteTex; }

    // Dessine UNIQUEMENT le sprite/rect (aucun texte) — pour les appelants qui séparent déjà
    // rendu sprite (phase Panels, sprites_.Begin()/End()) et rendu texte (phase Text,
    // font.BeginBatch()/EndBatch()), comme LoginScene. N'ouvre/ferme AUCUN lot : à appeler
    // entre un SpriteBatch::Begin() et son End().
    void DrawSkin(gfx::SpriteBatch& sb) const;
    // Dessine UNIQUEMENT le libellé — à appeler dans la même passe texte que le reste
    // (entre Font::BeginBatch()/EndBatch()).
    void DrawLabel(gfx::Font& font) const;

    // Combiné : DrawSkin() puis DrawLabel(). Contrat de rendu : suppose que les DEUX lots
    // sont déjà OUVERTS par l'appelant (SpriteBatch::Begin() ET Font::BeginBatch()), car
    // ID3DXSprite et ID3DXFont portent chacun leur propre batch (cf. commentaire de tête de
    // fichier) — les appelants à passes séparées (ex. LoginScene) doivent utiliser
    // DrawSkin()/DrawLabel() individuellement plutôt que cette variante combinée.
    void Draw(gfx::SpriteBatch& sb, gfx::Font& font);

private:
    WidgetSprite normal_, hover_, pressed_;
    std::string  label_;
    D3DCOLOR     labelColor_ = kTextWhite;
    int          labelStyle_ = gfx::kStyleNormal;
    int          textH_      = 12;
    bool         hover_active_ = false; // survol courant
    bool         armed_        = false; // enfoncé (latch)
    std::function<void()> onClick_;

    // Repli rect coloré (cf. SetFallbackColors/SetFallbackTexture ci-dessus).
    D3DCOLOR            fallbackNormal_  = 0;
    D3DCOLOR            fallbackHover_   = 0;
    D3DCOLOR            fallbackPressed_ = 0;
    IDirect3DTexture9*  fallbackTex_     = nullptr; // non possédé
};

// ---------------------------------------------------------------------------
// EditBox — champ de saisie autonome (pas d'EDIT Win32). Gère : insertion de
// caractères, curseur déplaçable, backspace/suppr, home/fin, masque mot de
// passe ('*'), caret clignotant, soumission (Entrée) et champ suivant (Tab).
// Fidèle à UI_EditBoxWndProc 0x50E070 (Tab/Entrée) et Scene_LoginRender 0x51B020
// (masque '*', caret après le texte quand focus).
class EditBox : public Widget {
public:
    void SetBackground(const WidgetSprite& bg) { bg_ = bg; }

    void SetText(const std::string& t);
    const std::string& Text() const { return text_; }
    void Clear();

    // Longueur max (fidèle : EM_LIMITTEXT — 0x7F login/pw, 0xC montants, 0x3C chat).
    void SetMaxLength(size_t n) { maxLen_ = n; }
    size_t MaxLength() const    { return maxLen_; }

    void SetPassword(bool p)    { password_ = p; }
    bool IsPassword() const     { return password_; }

    void SetFocused(bool f);
    bool Focused() const        { return focused_; }

    void SetTextColor(D3DCOLOR c)  { textColor_ = c; }
    void SetCaretColor(D3DCOLOR c) { caretColor_ = c; }
    void SetTextStyle(int mode)    { textStyle_ = mode; }
    void SetCaretBlink(bool b)     { caretBlink_ = b; }
    void SetPadding(int px)        { padX_ = px; }
    void SetTextHeight(int h)      { textH_ = h; }

    // Callbacks : Entrée (soumission) et Tab (champ suivant).
    void SetOnSubmit(std::function<void()> cb) { onSubmit_ = std::move(cb); }
    void SetOnTab(std::function<void()> cb)    { onTab_ = std::move(cb); }

    // Souris : focalise le champ au clic dedans (défocalise sinon via SetFocused).
    bool OnMouseDown(int mx, int my);

    // Clavier. OnChar : caractère imprimable (WM_CHAR). OnKey : touche virtuelle
    // (WM_KEYDOWN) — édition/navigation/soumission. Renvoient true si consommé.
    bool OnChar(unsigned int ch);
    bool OnKey(int vk);

    void Draw(gfx::SpriteBatch& sb, gfx::Font& font);

private:
    // Chaîne affichée : masquée par '*' si mot de passe, texte brut sinon.
    std::string DisplayString() const;

    WidgetSprite bg_;
    std::string  text_;
    size_t       maxLen_    = 0x7F;  // 127 (login/pw)
    size_t       caret_     = 0;     // index d'insertion [0..text_.size()]
    bool         password_  = false;
    bool         focused_   = false;
    bool         caretBlink_= true;
    D3DCOLOR     textColor_ = kTextWhite;
    D3DCOLOR     caretColor_= kTextWhite;
    int          textStyle_ = gfx::kStyleNormal;
    int          padX_      = 4;
    int          textH_     = 12;
    std::function<void()> onSubmit_;
    std::function<void()> onTab_;
};

} // namespace ts2::ui

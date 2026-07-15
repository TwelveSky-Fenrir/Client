// Gfx/Font.h — police bitmap/vectorielle du moteur GXD (ID3DXFont + ID3DXSprite).
//
// Réimplémentation FIDÈLE du sous-système texte de TwelveSky2, d'après le
// désassemblage (serveur idaTs2) :
//   - Gfx_InitDevice 0x69B9B0        : D3DXCreateSprite + D3DXCreateFontIndirectA
//                                      (sprite @+152 dword, font @+153 dword).
//   - App_Init 0x461C96              : construction du D3DXFONT_DESCA passé à
//                                      D3DXCreateFontIndirectA (j_ 0x6BB64E).
//   - Font_AddTtfResource 0x4C0E70   : enregistrement de la police TTF empaquetée
//                                      (G01_GFONT\...), pilotée par g_UseTRVariant
//                                      (0x1669190).
//   - Font_DrawTextStyled 0x405DC0   : texte normal / ombre / contour 8 directions.
//   - UI_DrawText 0x69E750           : même logique, sur l'objet UI.
//   - Font_MeasureTextWidth 0x405CE0 : largeur via DrawText(DT_CALCRECT).
//   - UI_MeasureText 0x69E680        : idem sur l'objet UI.
//
// Le SDK DirectX June 2010 étant câblé, on s'appuie sur ID3DXFont/ID3DXSprite
// (contrairement au reste de Gfx/ qui n'utilise que le Direct3D9 du Windows SDK).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

namespace ts2::gfx {

// Modes de style du dessin de texte (arg `mode` de Font_DrawTextStyled 0x405DC0).
enum StyleMode {
    kStyleNormal  = 0, // un seul passage, à (x, y)
    kStyleShadow  = 1, // ombre noire à (x-1, y-1) puis texte à (x, y)
    kStyleOutline = 2, // contour noir 8 directions puis texte à (x, y)
};

// Couleur du liseré/ombre : noir opaque (0xFF000000 == -16777216, cf. 0x405E7C).
constexpr D3DCOLOR kFontOutlineColor = 0xFF000000u;

class Font {
public:
    Font() = default;
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // Enregistre la police TTF empaquetée du client (G01_GFONT\...).
    // Fidèle à Font_AddTtfResource 0x4C0E70 : vérifie PathFileExistsA puis, ici,
    // AddFontResourceExA(FR_PRIVATE) — la police n'est visible que du process.
    //   trVariant=false -> "G01_GFONT\GIGASSOFT_12.TTF"
    //   trVariant=true  -> "G01_GFONT\TR\PTSans-Regular.TTF"
    // (L'original choisit via le global g_UseTRVariant 0x1669190 et appelait
    //  AddFontResourceA ; on préfère la variante Ex privée, comme demandé.)
    static bool AddTtfResource(bool trVariant = false);

    // Désenregistre la police TTF (App_Shutdown 0x462480, étape 33/33 — TOUT
    // DERNIER appel de la séquence). Fidèle à Font_RemoveTtfResource 0x4C0F10 :
    // contrepartie exacte d'AddTtfResource ci-dessus (même sélection de chemin
    // TR/EU). L'original garde le chemin dans un champ d'INSTANCE et ne rappelle
    // RemoveFontResourceA que si ce champ est non vide ; comme AddTtfResource
    // ci-dessus est déjà STATIQUE et SANS ÉTAT (aucun champ n'enregistre le
    // dernier chemin ajouté), cette fonction est elle aussi statique et sans
    // état : elle retire inconditionnellement la ressource pour `trVariant`
    // donné (RemoveFontResourceExA, contrepartie d'AddFontResourceExA/FR_PRIVATE
    // utilisé par AddTtfResource) — même déviation assumée que AddTtfResource.
    static bool RemoveTtfResource(bool trVariant = false);

    // Remplit le D3DXFONT_DESCA par défaut du client (valeurs de App_Init 0x461C96) :
    // Height=12, Width=6, Weight=0, MipLevels=1, Italic=0, CharSet=DEFAULT_CHARSET,
    // OutputPrecision=OUT_DEFAULT_PRECIS, Quality=CLEARTYPE_QUALITY, Pitch=DEFAULT_PITCH,
    // FaceName="GIGASSOFT_12" (ou "PT Sans" en variante TR).
    static D3DXFONT_DESCA MakeDefaultDesc(bool trVariant = false);

    // Crée l'ID3DXSprite + l'ID3DXFont avec le descripteur par défaut.
    // clipW/clipH = dimensions écran (le rect de clip = {x, y, clipW-1, clipH-1},
    // comme le champ largeur/hauteur du renderer, cf. 0x405DCF/0x405DD3).
    bool Init(IDirect3DDevice9* device, int clipW, int clipH, bool trVariant = false);

    // Variante avec descripteur explicite (pour reproduire un autre corps de police).
    bool InitWithDesc(IDirect3DDevice9* device, const D3DXFONT_DESCA& desc,
                      int clipW, int clipH);

    void Shutdown();

    // Perte/restauration du device D3D9 (à appeler autour de Reset()).
    void OnDeviceLost();  // ID3DXFont::OnLostDevice + ID3DXSprite::OnLostDevice
    void OnDeviceReset(); // ID3DXFont::OnResetDevice + ID3DXSprite::OnResetDevice

    // Met à jour le rectangle de clip (dims écran) après un redimensionnement.
    void SetClipRect(int clipW, int clipH) { clipW_ = clipW; clipH_ = clipH; }

    // Ouvre/ferme le lot de sprites. Dans l'original, le dessin de texte se fait
    // à l'intérieur du batch sprite global de la frame UI ; ID3DXFont::DrawText
    // exige que le sprite passé soit entre Begin()/End(). Appeler DrawText* entre
    // BeginBatch() et EndBatch().
    bool BeginBatch(DWORD flags = D3DXSPRITE_ALPHABLEND);
    void EndBatch();

    // Mesure la largeur en pixels du texte (les espaces sont comptés).
    // Fidèle à Font_MeasureTextWidth 0x405CE0 / UI_MeasureText 0x69E680 :
    // les ' ' sont remplacés par '_' dans une copie, puis DrawText(DT_CALCRECT),
    // et l'on renvoie rect.right.
    int MeasureText(const char* text) const;

    // Dessine le texte avec le style demandé (kStyleNormal/Shadow/Outline).
    // Fidèle à Font_DrawTextStyled 0x405DC0 / UI_DrawText 0x69E750.
    //   text  : chaîne ANSI NUL-terminée
    //   x, y  : coin haut-gauche (rect.left / rect.top)
    //   color : couleur D3DCOLOR (ARGB) du passage principal
    void DrawTextStyled(const char* text, int x, int y, D3DCOLOR color, int mode);

    // Raccourci mode normal.
    void DrawTextAt(const char* text, int x, int y, D3DCOLOR color) {
        DrawTextStyled(text, x, y, color, kStyleNormal);
    }

    ID3DXFont*   Handle() const { return font_; }
    ID3DXSprite* Sprite() const { return sprite_; }
    bool         Ready()  const { return font_ != nullptr; }

private:
    // Un passage DrawTextA unique : rect = {x, y, clipW_-1, clipH_-1}, format 0.
    // C'est l'appel vtable+56 (ID3DXFont::DrawTextA) répété par 0x405DC0.
    void DrawRun(const char* text, int x, int y, D3DCOLOR color) const;

    ID3DXSprite* sprite_ = nullptr; // ID3DXSprite (renderer @+152 dword / +608 o)
    ID3DXFont*   font_   = nullptr; // ID3DXFont   (renderer @+153 dword / +612 o)
    int          clipW_  = 0;       // largeur écran (renderer @+30 dword / +120 o)
    int          clipH_  = 0;       // hauteur écran (renderer @+31 dword / +124 o)
};

} // namespace ts2::gfx

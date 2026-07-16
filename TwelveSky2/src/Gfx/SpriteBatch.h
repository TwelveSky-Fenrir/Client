// Gfx/SpriteBatch.h — pass de rendu 2D du moteur GXD (ID3DXSprite).
//
// Réécriture FIDÈLE des routines sprite 2D de TwelveSky2.exe. Le DirectX SDK
// June 2010 étant câblé, on s'appuie sur d3dx9 (ID3DXSprite, D3DXCreateSprite,
// D3DXMatrix*) exactement comme le binaire d'origine.
//
// Ancres reversées (voir Docs/TS2_GXD_ENGINE.md) :
//   GXD_SetupLitSpritePass   0x4051F0  états alpha-blend du pass « lit sprite »
//   Sprite2D_EnsureLoaded    0x4D6A90  lazy-load de la texture au 1er blit
//   Sprite2D_Draw            0x4D6B20  blit plein-cadre (couleur blanche)
//   Sprite2D_DrawColored     0x4D6B80  blit d'une SOUS-RÉGION (voir note couleur)
//   UI_DrawSprite            0x6A3080  Draw(tex, srcRect, pos, 0xFFFFFFFF)
//   AutoPlay_DrawSpriteClip  0x457C10  variante clip via sprite du renderer
//   UI_DrawSpriteColored     0x6A3130  Draw mis à l'échelle (couleur blanche)
//   UI_DrawSpriteScaled      0x457CA0  Draw + matrice d'échelle (pos non compensée)
//   UI_DrawSpriteScaledAlpha 0x457D70  Draw + échelle + modulation ALPHA (pos compensée)
//   j_D3DXCreateSprite       0x6BB654  thunk vers D3DXCreateSprite
//
// NOTE COULEUR (importante) : dans le binaire, TOUS les blits sprite poussent la
// couleur 0xFFFFFFFF (-1) SAUF UI_DrawSpriteScaledAlpha qui module uniquement
// l'octet alpha (0x00FFFFFF | (alpha<<24)). Les noms « *Colored » issus de
// l'auto-analyse sont trompeurs : ils ne teintent PAS le RGB — « DrawColored »
// est en réalité un blit de sous-rectangle. On reste fidèle à ce comportement,
// tout en exposant un paramètre `color` sur les primitives SpriteBatch pour le
// cas alpha (seule vraie modulation du moteur).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>

namespace ts2::gfx {

// Couleur par défaut du blit : UI_DrawSprite/Draw poussent -1 = blanc opaque.
inline constexpr D3DCOLOR kSpriteWhite = 0xFFFFFFFFu;

// Modulation alpha seule (UI_DrawSpriteScaledAlpha 0x457D70) : RGB reste blanc,
// seul l'octet de poids fort (alpha) varie -> 0x00FFFFFF | (a<<24).
inline D3DCOLOR SpriteAlphaWhite(uint8_t a) {
    return 0x00FFFFFFu | (static_cast<D3DCOLOR>(a) << 24);
}

// ---------------------------------------------------------------------------
// GXD_SetupLitSpritePass 0x4051F0
// Configure le device pour le pass sprite « éclairé » (billboards du terrain,
// appelé par cWorldMesh_RenderTerrain 0x44F4A0). Reproduit les appels device :
//   SetRenderState(ZWRITEENABLE, FALSE)          (228 : 14,0)
//   SetRenderState(ALPHABLENDENABLE, TRUE)       (228 : 27,1)
//   SetRenderState(SRCBLEND, SRCALPHA)           (228 : 19,5)
//   SetRenderState(DESTBLEND, INVSRCALPHA)       (228 : 20,6)
//   SetTextureStageState(0, ALPHAOP, MODULATE)   (268 : 0,4,4)
//   [GXD_SetDirectionalLight(rndr,1,..) 0x403980 : LightEnable(0,1)+SetLight —
//    dépend de l'état caméra du renderer, cf. TODO dans le .cpp]
//   SetVertexShader(NULL)                        (368 : 0)
//   SetPixelShader(NULL)                         (428 : 0)
//   SetFVF(XYZ|DIFFUSE|TEX1 = 0x142)             (356 : 322)
//   SetTransform(D3DTS_WORLD, identité)          (176 : 256, world)
void GXD_SetupLitSpritePass(IDirect3DDevice9* dev);

// ---------------------------------------------------------------------------
// SpriteBatch — enveloppe de ID3DXSprite.
// Le renderer d'origine range ce sprite à g_GxdRenderer+528 (0x18C5108 /
// g_GxdRenderer_pSprite = pSprite @+528 d'Object B) et en cache un alias plat à
// dword_800078 (= pSprite @+608 d'Object A / g_GfxRenderer). Ici on
// encapsule l'interface ; l'alias « actif » est géré par SetActiveSprite().
// ex-VeryOldClient: GXD::mGraphicSprite (v1 / Object A @+608). CONFIRMED §1.1/§1.5.
class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch() { Destroy(); }
    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    // GXD_DeviceCreate 0x401610 : D3DXCreateSprite(device, &pSprite).
    bool    Create(IDirect3DDevice9* dev);
    void    Destroy();

    void    OnLostDevice();   // ID3DXSprite::OnLostDevice  (vtbl+48)
    void    OnResetDevice();  // ID3DXSprite::OnResetDevice (vtbl+52)

    HRESULT Begin(DWORD flags = D3DXSPRITE_ALPHABLEND); // (vtbl+32)
    HRESULT Flush();                                    // (vtbl+40)
    HRESULT End();                                      // (vtbl+44)

    // Primitive centrale : Draw(tex, srcRect, NULL, pos={x,y,0}, color).
    // srcRect == nullptr -> sprite entier (comportement ID3DXSprite). (vtbl+36)
    HRESULT DrawSprite(IDirect3DTexture9* tex, const RECT* srcRect,
                       int x, int y, D3DCOLOR color = kSpriteWhite);

    // Blit avec matrice d'échelle : GetTransform(save) / SetTransform(scale) /
    // Draw / SetTransform(save) — cf. UI_DrawSpriteScaled 0x457CA0.
    //   compensatePos=false : pos = {x, y}       (UI_DrawSpriteScaled : la pos
    //                                             subit l'échelle)
    //   compensatePos=true  : pos = {x/sx, y/sy} (UI_DrawSpriteScaledAlpha : pos
    //                                             pré-divisée pour rester à l'écran)
    HRESULT DrawSpriteScaled(IDirect3DTexture9* tex, const RECT* srcRect,
                             int x, int y, float scaleX, float scaleY,
                             D3DCOLOR color = kSpriteWhite,
                             bool compensatePos = false);

    ID3DXSprite* Get()   const { return sprite_; }
    bool         Ready() const { return sprite_ != nullptr; }

private:
    ID3DXSprite* sprite_ = nullptr;
};

// ---------------------------------------------------------------------------
// Sprite « actif » du renderer : mirroir des globales dword_800078 /
// g_GxdRenderer_pSprite lues par les helpers UI_DrawSprite*. Le code appelant
// installe le SpriteBatch courant une fois le device créé.
void         SetActiveSprite(SpriteBatch* batch);
SpriteBatch* ActiveSprite();

// Horloge de jeu (flt_815180 / g_GameTimeSec) écrite dans Texture+0x28 à chaque
// blit de Sprite2D. Fournie par l'App ; défaut 0.
extern float g_GameTimeSec;

// ---------------------------------------------------------------------------
// SpriteTexture — texture embarquée dans Sprite2D à l'offset +0x68, remplie par
// Tex_LoadCompressedDDS 0x6A2E80 (conteneur DDS-GXD zlib -> D3DXCreateTexture...).
// ex-VeryOldClient: GImage / TEXTURE_FOR_GXD (40 o) — layout CONFIRMED §1.5.
// Disposition exacte (indices Tex[i] du désassemblage) :
struct SpriteTexture {
    int                valid;        // +0x00  Tex[0] : 0 tant que non chargée, 1 sinon
    uint32_t           width;        // +0x04  Tex[1] : largeur (pixels)
    uint32_t           height;       // +0x08  Tex[2] : hauteur (pixels)
    uint32_t           header[5];    // +0x0C..+0x1C  reste de l'en-tête (qmemcpy 0x1C o)
    uint32_t           format;       // +0x20  Tex[8] : D3DFORMAT / drapeau format
    IDirect3DTexture9* d3dTex;       // +0x24  Tex[9] : texture GPU
    float              lastDrawTime; // +0x28  Tex[10]: g_GameTimeSec du dernier blit
};

// Chargeur bas niveau (Tex_LoadCompressedDDS 0x6A2E80) fourni par le sous-système
// asset : décompresse le conteneur DDS-GXD `filename` et crée out->d3dTex, en
// remplissant valid/width/height. Renvoie true en cas de succès. Hook injectable
// pour découpler le pass 2D du loader de fichiers (nullptr par défaut => échec).
using SpriteTextureLoader = bool (*)(SpriteTexture* out, const char* filename);
void SetSpriteTextureLoader(SpriteTextureLoader loader);

// ---------------------------------------------------------------------------
// Helpers de blit fidèles (opèrent sur le SpriteBatch actif).
//
// UI_DrawSprite 0x6A3080 : blit d'une SpriteTexture. Si useSrcRect, découpe le
// rectangle {left, top, left+w, top+h} ; sinon sprite entier {0,0,W,H}. Couleur
// TOUJOURS blanche (comportement binaire). Renvoie 0 si la texture n'est pas
// valide, sinon le HRESULT du Draw.
int UI_DrawSprite(SpriteTexture* tex, int x, int y, bool useSrcRect = false,
                  int srcLeft = 0, int srcTop = 0, int srcW = 0, int srcH = 0);

// AutoPlay_DrawSpriteClip 0x457C10 : identique à UI_DrawSprite mais via le sprite
// du renderer (g_GxdRenderer_pSprite). Fournie pour complétude.
int AutoPlay_DrawSpriteClip(SpriteTexture* tex, int x, int y, bool useSrcRect,
                            int srcLeft, int srcTop, int srcW, int srcH);

// UI_DrawSpriteColored 0x6A3130 : malgré son nom, c'est un blit plein-cadre MIS À
// L'ÉCHELLE (couleur blanche codée en dur). scaleX/scaleY = facteurs d'échelle.
int UI_DrawSpriteColored(SpriteTexture* tex, int x, int y,
                         float scaleX, float scaleY);

// UI_DrawSpriteScaled 0x457CA0 : blit plein-cadre avec matrice d'échelle ; la
// position N'EST PAS compensée (le sprite se déplace avec l'échelle).
int UI_DrawSpriteScaled(SpriteTexture* tex, int x, int y,
                        float scaleX, float scaleY);

// UI_DrawSpriteScaledAlpha 0x457D70 : blit plein-cadre à l'échelle + modulation
// ALPHA (RGB blanc, alpha = `alpha`). Ne fait rien si scale nul. Position
// compensée (pos = {x/sx, y/sy}). Seule vraie modulation de couleur du moteur.
void UI_DrawSpriteScaledAlpha(SpriteTexture* tex, int x, int y,
                              float scaleX, float scaleY, uint8_t alpha);

// ---------------------------------------------------------------------------
// Sprite2D — objet sprite 2D du client (cf. Sprite2D_Draw 0x4D6B20).
// Disposition exacte : loaded@+0, filename@+4 (0x64 o), texture@+0x68.
struct Sprite2D {
    int           loaded;         // +0x00  drapeau « chargé »
    char          filename[0x64]; // +0x04  chemin DDS-GXD (jusqu'à +0x68)
    SpriteTexture tex;            // +0x68  texture embarquée

    // Sprite2D_EnsureLoaded 0x4D6A90 : charge la texture au 1er usage.
    bool EnsureLoaded();

    // Sprite2D_Draw 0x4D6B20 : garantit le chargement, mémorise l'horloge de
    // jeu dans tex.lastDrawTime, puis blit plein-cadre (blanc).
    int Draw(int x, int y);

    // Sprite2D_DrawColored 0x4D6B80 : idem mais blit d'une SOUS-RÉGION
    // {left, top, w, h}. (Le nom d'origine évoque une couleur : en réalité ce
    // sont les coords du rectangle source ; la couleur reste blanche.)
    int DrawRegion(int x, int y, uint16_t left, uint16_t top,
                   uint16_t w, uint16_t h);
};

} // namespace ts2::gfx

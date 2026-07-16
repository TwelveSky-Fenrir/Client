// Gfx/SpriteBatch.cpp — implémentation du pass de rendu 2D GXD (ID3DXSprite).
// Traduction fidèle des ancres listées dans SpriteBatch.h.
#include "Gfx/SpriteBatch.h"
#include "Core/Log.h"

#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

// --- Globales de module (mirroir des globales du binaire) -------------------
float g_GameTimeSec = 0.0f;                       // flt_815180 / g_GameTimeSec

static SpriteBatch*        s_activeSprite = nullptr; // dword_800078 / g_GxdRenderer_pSprite
static SpriteTextureLoader s_texLoader    = nullptr; // Tex_LoadCompressedDDS 0x6A2E80 (hook)

void         SetActiveSprite(SpriteBatch* batch) { s_activeSprite = batch; }
SpriteBatch* ActiveSprite()                      { return s_activeSprite; }
void SetSpriteTextureLoader(SpriteTextureLoader loader) { s_texLoader = loader; }

// ===========================================================================
// GXD_SetupLitSpritePass 0x4051F0
// ===========================================================================
void GXD_SetupLitSpritePass(IDirect3DDevice9* dev) {
    if (!dev) return;

    // États de fusion alpha (offset device vtbl 228 = SetRenderState).
    dev->SetRenderState(D3DRS_ZWRITEENABLE,     FALSE);                // (14, 0)
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);                 // (27, 1)
    dev->SetRenderState(D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);    // (19, 5)
    dev->SetRenderState(D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA); // (20, 6)

    // Étage de texture 0 : ALPHAOP = MODULATE (offset 268 = SetTextureStageState).
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);     // (0, 4, 4)

    // GXD_SetDirectionalLight(rndr, 1, 0,0,0,0) 0x403980 :
    //   LightEnable(0, TRUE)  (vtbl+212) puis SetLight(0, &D3DLIGHT9)  (vtbl+204),
    //   avec une lumière directionnelle dont la diffuse dérive de la position
    //   caméra du renderer (rndr+1124..). Non reproductible ici sans l'objet
    //   renderer -> à brancher via le Renderer.
    //   Idem le drapeau *(rndr+526884) = 0 (champ interne du renderer).
    // TODO(renderer) : GXD_SetDirectionalLight + flag rndr+0x80A24.

    dev->SetVertexShader(nullptr); // (vtbl+368) SetVertexShader(NULL)
    dev->SetPixelShader(nullptr);  // (vtbl+428) SetPixelShader(NULL)

    // FVF 0x142 = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 (offset 356 = SetFVF).
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    // World = identité (les 16 floats rndr+988..rndr+1048), puis SetTransform.
    D3DXMATRIX world;
    D3DXMatrixIdentity(&world);
    dev->SetTransform(D3DTS_WORLD, &world); // (vtbl+176 : D3DTS_WORLD = 256)
}

// ===========================================================================
// SpriteBatch
// ===========================================================================
bool SpriteBatch::Create(IDirect3DDevice9* dev) {
    Destroy();
    if (!dev) { TS2_ERR("SpriteBatch::Create : device nul"); return false; }
    // GXD_DeviceCreate 0x401610 : D3DXCreateSprite(device, &pSprite) ; erreur -> code 8.
    // ex-VeryOldClient: GXD::mGraphicSprite (Object A @+608 / dword_800078 ; Object B pSprite @+528).
    HRESULT hr = D3DXCreateSprite(dev, &sprite_);
    if (FAILED(hr)) {
        TS2_ERR("D3DXCreateSprite a echoue (0x%08lX)", hr);
        sprite_ = nullptr;
        return false;
    }
    return true;
}

void SpriteBatch::Destroy() {
    if (s_activeSprite == this) s_activeSprite = nullptr;
    if (sprite_) { sprite_->Release(); sprite_ = nullptr; }
}

void SpriteBatch::OnLostDevice()  { if (sprite_) sprite_->OnLostDevice();  } // (vtbl+48)
void SpriteBatch::OnResetDevice() { if (sprite_) sprite_->OnResetDevice(); } // (vtbl+52)

HRESULT SpriteBatch::Begin(DWORD flags) { return sprite_ ? sprite_->Begin(flags) : E_FAIL; } // (vtbl+32)
HRESULT SpriteBatch::Flush()            { return sprite_ ? sprite_->Flush()      : E_FAIL; } // (vtbl+40)
HRESULT SpriteBatch::End()              { return sprite_ ? sprite_->End()        : E_FAIL; } // (vtbl+44)

HRESULT SpriteBatch::DrawSprite(IDirect3DTexture9* tex, const RECT* srcRect,
                                int x, int y, D3DCOLOR color) {
    if (!sprite_) return E_FAIL;
    // UI_DrawSprite : pos = { (float)x, (float)y, 0 } ; centre = NULL.
    D3DXVECTOR3 pos(static_cast<float>(x), static_cast<float>(y), 0.0f);
    return sprite_->Draw(tex, srcRect, nullptr, &pos, color); // (vtbl+36)
}

HRESULT SpriteBatch::DrawSpriteScaled(IDirect3DTexture9* tex, const RECT* srcRect,
                                      int x, int y, float scaleX, float scaleY,
                                      D3DCOLOR color, bool compensatePos) {
    if (!sprite_) return E_FAIL;
    // Sauvegarde de la transform courante du sprite (vtbl+16 = GetTransform).
    D3DXMATRIX saved, scaling;
    sprite_->GetTransform(&saved);
    // GXD_MatrixScaling 0x6BB66C = D3DXMatrixScaling(&m, sx, sy, 1.0f).
    D3DXMatrixScaling(&scaling, scaleX, scaleY, 1.0f);
    sprite_->SetTransform(&scaling); // (vtbl+20)

    // UI_DrawSpriteScaled : pos brute ; UI_DrawSpriteScaledAlpha : pos pré-divisée.
    const float px = compensatePos ? static_cast<float>(x) / scaleX : static_cast<float>(x);
    const float py = compensatePos ? static_cast<float>(y) / scaleY : static_cast<float>(y);
    D3DXVECTOR3 pos(px, py, 0.0f);

    HRESULT hr = sprite_->Draw(tex, srcRect, nullptr, &pos, color); // (vtbl+36)
    sprite_->SetTransform(&saved);   // restauration (vtbl+20)
    return hr;
}

// ===========================================================================
// Helpers de blit fidèles
// ===========================================================================
namespace {
// Construit le rectangle source comme UI_DrawSprite / AutoPlay_DrawSpriteClip.
inline RECT MakeSrcRect(const SpriteTexture* t, bool useSrcRect,
                        int left, int top, int w, int h) {
    RECT rc;
    if (useSrcRect) {                 // rectangle {left, top, left+w, top+h}
        rc.left   = left;
        rc.top    = top;
        rc.right  = left + w;
        rc.bottom = top + h;
    } else {                          // sprite entier {0, 0, W, H}
        rc.left   = 0;
        rc.top    = 0;
        rc.right  = static_cast<LONG>(t->width);
        rc.bottom = static_cast<LONG>(t->height);
    }
    return rc;
}
} // namespace

int UI_DrawSprite(SpriteTexture* tex, int x, int y, bool useSrcRect,
                  int srcLeft, int srcTop, int srcW, int srcH) {
    // if (!this->valid) return this->valid;  (renvoie 0, rien dessiné)
    if (!tex || !tex->valid) return 0;
    SpriteBatch* sb = ActiveSprite();
    if (!sb || !sb->Ready()) return 0;
    RECT rc = MakeSrcRect(tex, useSrcRect, srcLeft, srcTop, srcW, srcH);
    // Couleur codée en dur à 0xFFFFFFFF dans le binaire.
    return static_cast<int>(sb->DrawSprite(tex->d3dTex, &rc, x, y, kSpriteWhite));
}

int AutoPlay_DrawSpriteClip(SpriteTexture* tex, int x, int y, bool useSrcRect,
                            int srcLeft, int srcTop, int srcW, int srcH) {
    // 0x457C10 : même logique que UI_DrawSprite mais via le sprite du renderer.
    return UI_DrawSprite(tex, x, y, useSrcRect, srcLeft, srcTop, srcW, srcH);
}

int UI_DrawSpriteColored(SpriteTexture* tex, int x, int y,
                         float scaleX, float scaleY) {
    // 0x6A3130 : blit plein-cadre mis à l'échelle, couleur blanche, pos NON compensée.
    if (!tex || !tex->valid) return 0;
    SpriteBatch* sb = ActiveSprite();
    if (!sb || !sb->Ready()) return 0;
    RECT rc = MakeSrcRect(tex, false, 0, 0, 0, 0);
    return static_cast<int>(sb->DrawSpriteScaled(tex->d3dTex, &rc, x, y,
                                                 scaleX, scaleY, kSpriteWhite, false));
}

int UI_DrawSpriteScaled(SpriteTexture* tex, int x, int y,
                        float scaleX, float scaleY) {
    // 0x457CA0 : blit plein-cadre à l'échelle, couleur blanche, pos NON compensée.
    if (!tex || !tex->valid) return 0;
    SpriteBatch* sb = ActiveSprite();
    if (!sb || !sb->Ready()) return 0;
    RECT rc = MakeSrcRect(tex, false, 0, 0, 0, 0);
    return static_cast<int>(sb->DrawSpriteScaled(tex->d3dTex, &rc, x, y,
                                                 scaleX, scaleY, kSpriteWhite, false));
}

void UI_DrawSpriteScaledAlpha(SpriteTexture* tex, int x, int y,
                              float scaleX, float scaleY, uint8_t alpha) {
    // 0x457D70 : ne fait rien si texture invalide ou échelle nulle.
    if (!tex || !tex->valid || scaleX == 0.0f || scaleY == 0.0f) return;
    SpriteBatch* sb = ActiveSprite();
    if (!sb || !sb->Ready()) return;
    RECT rc = MakeSrcRect(tex, false, 0, 0, 0, 0);
    // Couleur = 0x00FFFFFF | (alpha<<24) ; position compensée (pos = {x/sx, y/sy}).
    sb->DrawSpriteScaled(tex->d3dTex, &rc, x, y, scaleX, scaleY,
                         SpriteAlphaWhite(alpha), /*compensatePos=*/true);
}

// ===========================================================================
// Sprite2D
// ===========================================================================
bool Sprite2D::EnsureLoaded() {
    // Sprite2D_EnsureLoaded 0x4D6A90
    if (loaded) return true;                    // if (*this) return 1;
    // Tex_LoadCompressedDDS(&tex /*+0x68*/, filename /*+4*/) 0x6A2E80
    if (!s_texLoader || !s_texLoader(&tex, filename)) return false; // -> 0 si échec
    loaded = 1;
    return true;
}

int Sprite2D::Draw(int x, int y) {
    // Sprite2D_Draw 0x4D6B20
    if (!loaded) {                              // if (!*this)
        if (!EnsureLoaded()) return 0;          //   if (!EnsureLoaded()) return 0;
        loaded = 1;                             //   *this = 1;
    }
    tex.lastDrawTime = g_GameTimeSec;           // *(this+0x90) = g_GameTimeSec
    return UI_DrawSprite(&tex, x, y, false, 0, 0, 0, 0);
}

int Sprite2D::DrawRegion(int x, int y, uint16_t left, uint16_t top,
                         uint16_t w, uint16_t h) {
    // Sprite2D_DrawColored 0x4D6B80 : useSrcRect=1, rectangle {left,top,w,h}.
    if (!loaded) {
        if (!EnsureLoaded()) return 0;
        loaded = 1;
    }
    tex.lastDrawTime = g_GameTimeSec;
    return UI_DrawSprite(&tex, x, y, true, left, top, w, h);
}

} // namespace ts2::gfx

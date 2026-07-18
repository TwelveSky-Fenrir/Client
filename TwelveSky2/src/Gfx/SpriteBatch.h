// Gfx/SpriteBatch.h — 2D render pass of the GXD engine (ID3DXSprite).
//
// FAITHFUL rewrite of TwelveSky2.exe's 2D sprite routines. Since the DirectX SDK
// June 2010 is wired in, we rely on d3dx9 (ID3DXSprite, D3DXCreateSprite,
// D3DXMatrix*) exactly like the original binary.
//
// Reversed anchors (see Docs/TS2_GXD_ENGINE.md):
//   GXD_SetupLitSpritePass   0x4051F0  alpha-blend states of the "lit sprite" pass
//   Sprite2D_EnsureLoaded    0x4D6A90  lazy texture load on 1st blit
//   Sprite2D_Draw            0x4D6B20  full-frame blit (white color)
//   Sprite2D_DrawColored     0x4D6B80  blit of a SUB-REGION (see color note)
//   UI_DrawSprite            0x6A3080  Draw(tex, srcRect, pos, 0xFFFFFFFF)
//   AutoPlay_DrawSpriteClip  0x457C10  clip variant via the renderer's sprite
//   UI_DrawSpriteColored     0x6A3130  scaled Draw (white color)
//   UI_DrawSpriteScaled      0x457CA0  Draw + scale matrix (pos not compensated)
//   UI_DrawSpriteScaledAlpha 0x457D70  Draw + scale + ALPHA modulation (pos compensated)
//   j_D3DXCreateSprite       0x6BB654  thunk to D3DXCreateSprite
//
// COLOR NOTE (important): in the binary, ALL sprite blits push color
// 0xFFFFFFFF (-1) EXCEPT UI_DrawSpriteScaledAlpha, which only modulates the
// alpha byte (0x00FFFFFF | (alpha<<24)). The "*Colored" names produced by
// auto-analysis are misleading: they do NOT tint RGB — "DrawColored" is
// actually a sub-rectangle blit. We stay faithful to this behavior, while
// exposing a `color` parameter on the SpriteBatch primitives for the alpha
// case (the engine's only true modulation).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>

namespace ts2::gfx {

// Default blit color: UI_DrawSprite/Draw push -1 = opaque white.
inline constexpr D3DCOLOR kSpriteWhite = 0xFFFFFFFFu;

// Alpha-only modulation (UI_DrawSpriteScaledAlpha 0x457D70): RGB stays white,
// only the high-order (alpha) byte varies -> 0x00FFFFFF | (a<<24).
inline D3DCOLOR SpriteAlphaWhite(uint8_t a) {
    return 0x00FFFFFFu | (static_cast<D3DCOLOR>(a) << 24);
}

// ---------------------------------------------------------------------------
// GXD_SetupLitSpritePass 0x4051F0
// Configures the device for the "lit sprite" pass (terrain billboards, called
// by cWorldMesh_RenderTerrain 0x44F4A0). Reproduces the device calls:
//   SetRenderState(ZWRITEENABLE, FALSE)          (228 : 14,0)
//   SetRenderState(ALPHABLENDENABLE, TRUE)       (228 : 27,1)
//   SetRenderState(SRCBLEND, SRCALPHA)           (228 : 19,5)
//   SetRenderState(DESTBLEND, INVSRCALPHA)       (228 : 20,6)
//   SetTextureStageState(0, ALPHAOP, MODULATE)   (268 : 0,4,4)
//   [GXD_SetDirectionalLight(rndr,1,..) 0x403980 : LightEnable(0,1)+SetLight —
//    depends on the renderer's camera state, cf. TODO in the .cpp]
//   SetVertexShader(NULL)                        (368 : 0)
//   SetPixelShader(NULL)                         (428 : 0)
//   SetFVF(XYZ|DIFFUSE|TEX1 = 0x142)             (356 : 322)
//   SetTransform(D3DTS_WORLD, identity)          (176 : 256, world)
void GXD_SetupLitSpritePass(IDirect3DDevice9* dev);

// ---------------------------------------------------------------------------
// SpriteBatch — wrapper around ID3DXSprite.
// The original renderer stores this sprite at g_GxdRenderer+528 (0x18C5108 /
// g_GxdRenderer_pSprite = pSprite @+528 of Object B) and caches a flat alias at
// dword_800078 (= pSprite @+608 of Object A / g_GfxRenderer). Here we
// encapsulate the interface; the "active" alias is managed by SetActiveSprite().
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

    // Core primitive: Draw(tex, srcRect, NULL, pos={x,y,0}, color).
    // srcRect == nullptr -> full sprite (ID3DXSprite behavior). (vtbl+36)
    HRESULT DrawSprite(IDirect3DTexture9* tex, const RECT* srcRect,
                       int x, int y, D3DCOLOR color = kSpriteWhite);

    // Blit with a scale matrix: GetTransform(save) / SetTransform(scale) /
    // Draw / SetTransform(save) — cf. UI_DrawSpriteScaled 0x457CA0.
    //   compensatePos=false : pos = {x, y}       (UI_DrawSpriteScaled : pos
    //                                             gets scaled too)
    //   compensatePos=true  : pos = {x/sx, y/sy} (UI_DrawSpriteScaledAlpha : pos
    //                                             pre-divided to stay on screen)
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
// Renderer's "active" sprite: mirror of the dword_800078 /
// g_GxdRenderer_pSprite globals read by the UI_DrawSprite* helpers. Calling
// code installs the current SpriteBatch once the device is created.
void         SetActiveSprite(SpriteBatch* batch);
SpriteBatch* ActiveSprite();

// Game clock (flt_815180 / g_GameTimeSec) written into Texture+0x28 on every
// Sprite2D blit. Supplied by App; defaults to 0.
extern float g_GameTimeSec;

// ---------------------------------------------------------------------------
// SpriteTexture — texture embedded in Sprite2D at offset +0x68, filled by
// Tex_LoadCompressedDDS 0x6A2E80 (zlib DDS-GXD container -> D3DXCreateTexture...).
// ex-VeryOldClient: GImage / TEXTURE_FOR_GXD (40 bytes) — layout CONFIRMED §1.5.
// Exact layout (Tex[i] indices from the disassembly):
struct SpriteTexture {
    int                valid;        // +0x00  Tex[0] : 0 until loaded, 1 otherwise
    uint32_t           width;        // +0x04  Tex[1] : width (pixels)
    uint32_t           height;       // +0x08  Tex[2] : height (pixels)
    uint32_t           header[5];    // +0x0C..+0x1C  rest of the header (qmemcpy 0x1C bytes)
    uint32_t           format;       // +0x20  Tex[8] : D3DFORMAT / format flag
    IDirect3DTexture9* d3dTex;       // +0x24  Tex[9] : GPU texture
    float              lastDrawTime; // +0x28  Tex[10]: g_GameTimeSec of the last blit
};

// Low-level loader (Tex_LoadCompressedDDS 0x6A2E80) supplied by the asset
// subsystem: decompresses the DDS-GXD container `filename` and creates
// out->d3dTex, filling valid/width/height. Returns true on success. Injectable
// hook to decouple the 2D pass from the file loader (nullptr by default => failure).
using SpriteTextureLoader = bool (*)(SpriteTexture* out, const char* filename);
void SetSpriteTextureLoader(SpriteTextureLoader loader);

// ---------------------------------------------------------------------------
// Faithful blit helpers (operate on the active SpriteBatch).
//
// UI_DrawSprite 0x6A3080 : blits a SpriteTexture. If useSrcRect, crops the
// rectangle {left, top, left+w, top+h}; otherwise full sprite {0,0,W,H}. Color
// ALWAYS white (binary behavior). Returns 0 if the texture is not valid,
// otherwise the HRESULT of the Draw.
int UI_DrawSprite(SpriteTexture* tex, int x, int y, bool useSrcRect = false,
                  int srcLeft = 0, int srcTop = 0, int srcW = 0, int srcH = 0);

// AutoPlay_DrawSpriteClip 0x457C10 : identical to UI_DrawSprite but via the
// renderer's sprite (g_GxdRenderer_pSprite). Provided for completeness.
int AutoPlay_DrawSpriteClip(SpriteTexture* tex, int x, int y, bool useSrcRect,
                            int srcLeft, int srcTop, int srcW, int srcH);

// UI_DrawSpriteColored 0x6A3130 : despite its name, this is a full-frame blit
// SCALED (hardcoded white color). scaleX/scaleY = scale factors.
int UI_DrawSpriteColored(SpriteTexture* tex, int x, int y,
                         float scaleX, float scaleY);

// UI_DrawSpriteScaled 0x457CA0 : full-frame blit with a scale matrix; the
// position is NOT compensated (the sprite moves along with the scale).
int UI_DrawSpriteScaled(SpriteTexture* tex, int x, int y,
                        float scaleX, float scaleY);

// UI_DrawSpriteScaledAlpha 0x457D70 : full-frame scaled blit + ALPHA
// modulation (white RGB, alpha = `alpha`). Does nothing if scale is zero.
// Compensated position (pos = {x/sx, y/sy}). The engine's only true color modulation.
void UI_DrawSpriteScaledAlpha(SpriteTexture* tex, int x, int y,
                              float scaleX, float scaleY, uint8_t alpha);

// ---------------------------------------------------------------------------
// Sprite2D — the client's 2D sprite object (cf. Sprite2D_Draw 0x4D6B20).
// Exact layout: loaded@+0, filename@+4 (0x64 bytes), texture@+0x68.
struct Sprite2D {
    int           loaded;         // +0x00  "loaded" flag
    char          filename[0x64]; // +0x04  DDS-GXD path (up to +0x68)
    SpriteTexture tex;            // +0x68  embedded texture

    // Sprite2D_EnsureLoaded 0x4D6A90 : loads the texture on first use.
    bool EnsureLoaded();

    // Sprite2D_Draw 0x4D6B20 : ensures loading, records the game clock into
    // tex.lastDrawTime, then blits the full frame (white).
    int Draw(int x, int y);

    // Sprite2D_DrawColored 0x4D6B80 : same but blits a SUB-REGION
    // {left, top, w, h}. (The original name suggests color: it's actually the
    // source rectangle coords; the color stays white.)
    int DrawRegion(int x, int y, uint16_t left, uint16_t top,
                   uint16_t w, uint16_t h);
};

} // namespace ts2::gfx

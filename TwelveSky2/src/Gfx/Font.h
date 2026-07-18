// Gfx/Font.h — GXD engine bitmap/vector font (ID3DXFont + ID3DXSprite).
//
// FAITHFUL reimplementation of TwelveSky2's text subsystem, based on the
// disassembly (idaTs2 server):
//   - Gfx_InitDevice 0x69B9B0        : D3DXCreateSprite + D3DXCreateFontIndirectA
//                                      (sprite @+152 dword, font @+153 dword).
//   - App_Init 0x461C96              : builds the D3DXFONT_DESCA passed to
//                                      D3DXCreateFontIndirectA (j_ 0x6BB64E).
//   - Font_AddTtfResource 0x4C0E70   : registers the bundled TTF font
//                                      (G01_GFONT\...), driven by g_UseTRVariant
//                                      (0x1669190).
//   - Font_DrawTextStyled 0x405DC0   : normal / shadow / 8-direction outline text.
//   - UI_DrawText 0x69E750           : same logic, on the UI object.
//   - Font_MeasureTextWidth 0x405CE0 : width via DrawText(DT_CALCRECT).
//   - UI_MeasureText 0x69E680        : same, on the UI object.
//
// Since the DirectX June 2010 SDK is wired in, this relies on ID3DXFont/ID3DXSprite
// (unlike the rest of Gfx/, which uses only the Windows SDK's Direct3D9).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

namespace ts2::gfx {

// Text-drawing style modes (`mode` arg of Font_DrawTextStyled 0x405DC0).
enum StyleMode {
    kStyleNormal  = 0, // single pass, at (x, y)
    kStyleShadow  = 1, // black shadow at (x-1, y-1) then text at (x, y)
    kStyleOutline = 2, // black 8-direction outline then text at (x, y)
};

// Outline/shadow color: opaque black (0xFF000000 == -16777216, cf. 0x405E7C).
constexpr D3DCOLOR kFontOutlineColor = 0xFF000000u;

class Font {
public:
    Font() = default;
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // Registers the client's bundled TTF font (G01_GFONT\...).
    // Faithful to Font_AddTtfResource 0x4C0E70: checks PathFileExistsA then, here,
    // AddFontResourceExA(FR_PRIVATE) — the font is visible only to this process.
    //   trVariant=false -> "G01_GFONT\GIGASSOFT_12.TTF"
    //   trVariant=true  -> "G01_GFONT\TR\PTSans-Regular.TTF"
    // (The original picks via global g_UseTRVariant 0x1669190 and called
    //  AddFontResourceA; we prefer the private Ex variant, as requested.)
    static bool AddTtfResource(bool trVariant = false);

    // Unregisters the TTF font (App_Shutdown 0x462480, step 33/33 — VERY LAST
    // call of the sequence). Faithful to Font_RemoveTtfResource 0x4C0F10:
    // exact counterpart of AddTtfResource above (same TR/EU path selection).
    // The original keeps the path in an INSTANCE field and only calls back
    // RemoveFontResourceA if that field is non-empty; since AddTtfResource
    // above is already STATIC and STATELESS (no field records the
    // last-added path), this function is likewise static and stateless:
    // it unconditionally removes the resource for the given `trVariant`
    // (RemoveFontResourceExA, counterpart of AddFontResourceExA/FR_PRIVATE
    // used by AddTtfResource) — same accepted deviation as AddTtfResource.
    static bool RemoveTtfResource(bool trVariant = false);

    // Fills the client's default D3DXFONT_DESCA (values from App_Init 0x461C96):
    // Height=12, Width=6, Weight=0, MipLevels=1, Italic=0, CharSet=DEFAULT_CHARSET,
    // OutputPrecision=OUT_DEFAULT_PRECIS, Quality=CLEARTYPE_QUALITY, Pitch=DEFAULT_PITCH,
    // FaceName="GIGASSOFT_12" (or "PT Sans" in the TR variant).
    static D3DXFONT_DESCA MakeDefaultDesc(bool trVariant = false);

    // Creates the ID3DXSprite + ID3DXFont with the default descriptor.
    // clipW/clipH = screen dimensions (clip rect = {x, y, clipW-1, clipH-1},
    // same as the renderer's width/height field, cf. 0x405DCF/0x405DD3).
    bool Init(IDirect3DDevice9* device, int clipW, int clipH, bool trVariant = false);

    // Variant with an explicit descriptor (to reproduce a different font face).
    bool InitWithDesc(IDirect3DDevice9* device, const D3DXFONT_DESCA& desc,
                      int clipW, int clipH);

    void Shutdown();

    // D3D9 device loss/restore (call around Reset()).
    void OnDeviceLost();  // ID3DXFont::OnLostDevice + ID3DXSprite::OnLostDevice
    void OnDeviceReset(); // ID3DXFont::OnResetDevice + ID3DXSprite::OnResetDevice

    // Updates the clip rect (screen dims) after a resize.
    void SetClipRect(int clipW, int clipH) { clipW_ = clipW; clipH_ = clipH; }

    // Opens/closes the sprite batch. In the original, text drawing happens
    // inside the UI frame's global sprite batch; ID3DXFont::DrawText requires
    // the passed sprite to be between Begin()/End(). Call DrawText* between
    // BeginBatch() and EndBatch().
    bool BeginBatch(DWORD flags = D3DXSPRITE_ALPHABLEND);
    void EndBatch();

    // Measures the text width in pixels (spaces are counted).
    // Faithful to Font_MeasureTextWidth 0x405CE0 / UI_MeasureText 0x69E680:
    // ' ' are replaced with '_' in a copy, then DrawText(DT_CALCRECT),
    // and rect.right is returned.
    int MeasureText(const char* text) const;

    // Draws text with the requested style (kStyleNormal/Shadow/Outline).
    // Faithful to Font_DrawTextStyled 0x405DC0 / UI_DrawText 0x69E750.
    //   text  : NUL-terminated ANSI string
    //   x, y  : top-left corner (rect.left / rect.top)
    //   color : D3DCOLOR (ARGB) of the main pass
    void DrawTextStyled(const char* text, int x, int y, D3DCOLOR color, int mode);

    // Shortcut for normal mode.
    void DrawTextAt(const char* text, int x, int y, D3DCOLOR color) {
        DrawTextStyled(text, x, y, color, kStyleNormal);
    }

    ID3DXFont*   Handle() const { return font_; }
    ID3DXSprite* Sprite() const { return sprite_; }
    bool         Ready()  const { return font_ != nullptr; }

private:
    // A single DrawTextA pass: rect = {x, y, clipW_-1, clipH_-1}, format 0.
    // This is the vtable+56 call (ID3DXFont::DrawTextA) repeated by 0x405DC0.
    void DrawRun(const char* text, int x, int y, D3DCOLOR color) const;

    // ex-VeryOldClient: GXD::mGraphicSprite / GXD::mGraphicFont (v1 / Object A). CONFIRMED §1.5.
    // Object A (g_GfxRenderer 0x7FFE18): pSprite @+608 (0x800078), pFont @+612 (0x80007C).
    // Object B (g_GxdRenderer 0x18C4EF8): pSprite @+528 (0x18C5108), pFont @+532 (0x18C510C) —
    //   this is the font/sprite ACTUALLY used by Font_DrawTextStyled 0x405DC0 (this class's
    //   main anchor). NB CONFLICT (D-6, out of apply pass) : Font MERGES the 2 A/B instances;
    //   anchoring mixes A offsets (+608/+612) with B's routine (0x405DC0) — to be settled in the log.
    ID3DXSprite* sprite_ = nullptr; // ID3DXSprite (renderer A @+152 dword / +608 B; B @+528) — ex-VeryOldClient: mGraphicSprite
    ID3DXFont*   font_   = nullptr; // ID3DXFont   (renderer A @+153 dword / +612 B; B @+532) — ex-VeryOldClient: mGraphicFont
    // NB CONFLICT (D-7, out of apply pass): +120/+124 NOT PROVEN; Object B reads screen W/H at +48/+52.
    int          clipW_  = 0;       // screen width (renderer @+30 dword / +120 B)
    int          clipH_  = 0;       // screen height (renderer @+31 dword / +124 B)
};

} // namespace ts2::gfx

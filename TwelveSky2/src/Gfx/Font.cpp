// Gfx/Font.cpp — GXD engine font & styled text.
// See Gfx/Font.h for the disassembly anchor table.
#include "Gfx/Font.h"
#include "Core/Log.h"

#include <shlwapi.h> // PathFileExistsA (cf. off_7A6340 in Font_AddTtfResource)
#include <cstring>

#pragma comment(lib, "d3dx9.lib")   // ID3DXFont / ID3DXSprite / D3DXCreate*
#pragma comment(lib, "gdi32.lib")   // AddFontResourceExA
#pragma comment(lib, "shlwapi.lib") // PathFileExistsA

namespace ts2::gfx {

// ------------------------------------------------------------------ TTF paths
// Font_AddTtfResource 0x4C0E70: "G01_GFONT\\GIGASSOFT_12.TTF" (0x7A6F04)
//                       or TR: "G01_GFONT\\TR\\PTSans-Regular.TTF" (0x7A6F20).
static const char* kTtfPathDefault = "G01_GFONT\\GIGASSOFT_12.TTF";
static const char* kTtfPathTr      = "G01_GFONT\\TR\\PTSans-Regular.TTF";

// Font name (FaceName of D3DXFONT_DESCA): App_Init 0x461CED / 0x461D03.
static const char* kFaceDefault = "GIGASSOFT_12";
static const char* kFaceTr      = "PT Sans";

Font::~Font() { Shutdown(); }

// ---------------------------------------------------------------- AddTtfResource
bool Font::AddTtfResource(bool trVariant) {
    const char* path = trVariant ? kTtfPathTr : kTtfPathDefault;

    // Faithful: the original returns 0 (mFONTDATA.Init failure) if the file is missing.
    if (!PathFileExistsA(path)) {
        TS2_ERR("Font: TTF introuvable: %s", path);
        return false;
    }
    // The original called AddFontResourceA; we use the process-private variant
    // (FR_PRIVATE) to avoid polluting the Windows session.
    if (AddFontResourceExA(path, FR_PRIVATE, nullptr) == 0) {
        TS2_ERR("Font: AddFontResourceExA a echoue: %s", path);
        return false;
    }
    TS2_LOG("Font: TTF enregistree: %s", path);
    return true;
}

// ---------------------------------------------------------------- RemoveTtfResource
bool Font::RemoveTtfResource(bool trVariant) {
    const char* path = trVariant ? kTtfPathTr : kTtfPathDefault;
    if (RemoveFontResourceExA(path, FR_PRIVATE, nullptr) == 0) {
        TS2_WARN("Font: RemoveFontResourceExA a echoue (deja retiree ?): %s", path);
        return false;
    }
    TS2_LOG("Font: TTF desenregistree: %s", path);
    return true;
}

// ---------------------------------------------------------------- MakeDefaultDesc
// ex-VeryOldClient: GXD::mFontInfo (type D3DXFONT_DESC, 56 B) — CONFIRMED §1.5. VeryOld gives the
//   TYPE but no values; the values (Height 12, Width 6, CLEARTYPE...) are 100% IDA (App_Init 0x461C96).
D3DXFONT_DESCA Font::MakeDefaultDesc(bool trVariant) {
    D3DXFONT_DESCA d = {}; // Crt_Memset(&v4, 0, 56) — sizeof(D3DXFONT_DESCA) == 56
    d.Height          = 12;                        // var_428 = 0x0C
    d.Width           = 6;                          // var_424 = 6
    d.Weight          = 0;                          // var_420 = FW_DONTCARE
    d.MipLevels       = 1;                          // var_41C = 1
    d.Italic          = FALSE;                      // var_418 = 0
    d.CharSet         = DEFAULT_CHARSET;            // var_414 = 1
    d.OutputPrecision = OUT_DEFAULT_PRECIS;        // var_413 = 0
    d.Quality         = CLEARTYPE_QUALITY;         // var_412 = 5
    d.PitchAndFamily  = DEFAULT_PITCH | FF_DONTCARE; // var_411 = 0
    strcpy_s(d.FaceName, trVariant ? kFaceTr : kFaceDefault); // var_410 (32 B)
    return d;
}

// ---------------------------------------------------------------------- Init
bool Font::Init(IDirect3DDevice9* device, int clipW, int clipH, bool trVariant) {
    return InitWithDesc(device, MakeDefaultDesc(trVariant), clipW, clipH);
}

bool Font::InitWithDesc(IDirect3DDevice9* device, const D3DXFONT_DESCA& desc,
                        int clipW, int clipH) {
    if (!device) { TS2_ERR("Font: device nul"); return false; }
    Shutdown();

    clipW_ = clipW;
    clipH_ = clipH;

    // j_D3DXCreateSprite 0x6BB654: renderer A @+608 (dword idx 152). ex-VeryOldClient: mGraphicSprite.
    HRESULT hr = D3DXCreateSprite(device, &sprite_);
    if (FAILED(hr)) { TS2_ERR("Font: D3DXCreateSprite (0x%08lX)", hr); Shutdown(); return false; }

    // j_D3DXCreateFontIndirectA 0x6BB64E: renderer A @+612 (dword idx 153). ex-VeryOldClient: mGraphicFont.
    hr = D3DXCreateFontIndirectA(device, &desc, &font_);
    if (FAILED(hr)) { TS2_ERR("Font: D3DXCreateFontIndirectA (0x%08lX)", hr); Shutdown(); return false; }

    TS2_LOG("Font: creee '%s' h=%d w=%d q=%d clip=%dx%d",
            desc.FaceName, desc.Height, desc.Width, desc.Quality, clipW_, clipH_);
    return true;
}

void Font::Shutdown() {
    if (font_)   { font_->Release();   font_   = nullptr; }
    if (sprite_) { sprite_->Release(); sprite_ = nullptr; }
}

void Font::OnDeviceLost() {
    if (font_)   font_->OnLostDevice();
    if (sprite_) sprite_->OnLostDevice();
}

void Font::OnDeviceReset() {
    if (sprite_) sprite_->OnResetDevice();
    if (font_)   font_->OnResetDevice();
}

bool Font::BeginBatch(DWORD flags) {
    if (!sprite_) return false;
    return SUCCEEDED(sprite_->Begin(flags));
}

void Font::EndBatch() {
    if (sprite_) sprite_->End();
}

// -------------------------------------------------------------------- MeasureText
int Font::MeasureText(const char* text) const {
    if (!font_ || !text) return 0;

    // Bounded copy (Crt_StrcpyS(v10, 1000, a2) / strcpy into char[1000]).
    char buf[1000];
    strncpy_s(buf, sizeof(buf), text, _TRUNCATE);

    // Space -> '_' so DT_CALCRECT also counts whitespace (0x405D2A).
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] == ' ') buf[i] = '_';
    }

    // initial rect {0, 0, clipW_-1, clipH_-1} (v7[0]=0, v7[1]=0, v8=w-1, v9=h-1).
    RECT r = { 0, 0, clipW_ - 1, clipH_ - 1 };
    // ID3DXFont::DrawTextA(sprite, text, -1, &rect, DT_CALCRECT(0x400), 0).
    font_->DrawTextA(sprite_, buf, -1, &r, DT_CALCRECT, 0);
    return r.right; // computed width (0x405D95 returns v8 = rect.right)
}

// -------------------------------------------------------------------- DrawRun
void Font::DrawRun(const char* text, int x, int y, D3DCOLOR color) const {
    // rect = {x, y, clipW_-1, clipH_-1}, format 0 (no flags) — identical to 0x405DC0.
    RECT r = { x, y, clipW_ - 1, clipH_ - 1 };
    font_->DrawTextA(sprite_, text, -1, &r, 0, color);
}

// ------------------------------------------------------------------ DrawTextStyled
void Font::DrawTextStyled(const char* text, int x, int y, D3DCOLOR color, int mode) {
    if (!font_ || !text) return;

    switch (mode) {
    case kStyleNormal: // 0x405DCA case 0
        DrawRun(text, x, y, color);
        break;

    case kStyleShadow: // 0x405E1B case 1: black shadow (x-1,y-1) then colored (x,y)
        DrawRun(text, x - 1, y - 1, kFontOutlineColor);
        DrawRun(text, x,     y,     color);
        break;

    case kStyleOutline: // 0x405E3F case 2: black 8-direction outline then colored (x,y)
        DrawRun(text, x - 1, y - 1, kFontOutlineColor);
        DrawRun(text, x,     y - 1, kFontOutlineColor);
        DrawRun(text, x + 1, y - 1, kFontOutlineColor);
        DrawRun(text, x - 1, y,     kFontOutlineColor);
        DrawRun(text, x + 1, y,     kFontOutlineColor);
        DrawRun(text, x - 1, y + 1, kFontOutlineColor);
        DrawRun(text, x,     y + 1, kFontOutlineColor);
        DrawRun(text, x + 1, y + 1, kFontOutlineColor);
        DrawRun(text, x,     y,     color);
        break;

    default: // 0x405E36: unknown mode -> no draw (returns result as-is)
        break;
    }
}

} // namespace ts2::gfx

// UI/FloatingNotices.cpp — HUD floating notices implementation.
// See UI/FloatingNotices.h for the proven layout of dword_1821D58, scope, and
// assumed deviations.
#include "Gfx/SpriteBatch.h"     // gfx::SpriteBatch / kSpriteWhite (Sprite2D_Draw 0x4D6B20)
#include "Gfx/Font.h"            // gfx::Font (UI_MeasureText 0x69E680 / UI_DrawText 0x69E750)
#include "Gfx/GpuTexture.h"      // gfx::GpuTexture (definition of FloatingNotices::Gpu)
#include "Asset/ImgFile.h"       // asset::ImgFile (001_%05d.IMG)
#include "UI/FloatingNotices.h"
#include "Core/Types.h"          // ts2::kRefWidth / kRefHeight (1024x768 reference)
#include "Scene/SceneManager.h"  // ts2::g_SceneSubState 0x1676184 (guard @0x5AF4DA)
#include "Game/StringTables.h"   // game::g_Strings.colors (ColorTable_GetColor 0x4C1FE0)

#include <cstdio>

namespace ts2::ui {

namespace {

// Path template of the SHARED Sprite2D table, category 1 (base
// g_AssetMgr_UiAtlasSlots 0x8E8B50, stride 148 bytes, filled by AssetMgr_InitAllSlots
// 0x4DEB50). Identical to UI/BuffStatusPanel.cpp::GxdCategory1Path (file-local there,
// hence this copy rather than a cross-dependency between two standalone widgets).
std::string GxdCategory1Path(int fileNo) {
    if (fileNo <= 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\001\\001_%05d.IMG", fileNo);
    return std::string(buf);
}

// The two anchor sprites. Warning: the 744 / 1027 literals in HUD_RenderFloatingMessages
// are NOT coordinates: they are the sprite INDICES passed as a2 to
// UI_ProjectSpriteToScreen 0x50F5D0, which reads its dimensions via
// Sprite2D_GetWidth/GetHeight(g_AssetMgr_UiAtlasSlots + 148*a2). Arithmetic proof
// (EXACT division, established method — cf. banner of UI/BuffStatusPanel.h):
//   0x8E8B50 + 148*744  = 0x903970 = unk_903970 drawn @0x5AF5B4 (types 0..11)
//   0x8E8B50 + 148*1027 = 0x90DD0C = unk_90DD0C drawn @0x5AFC83 (type 12)
// So the anchor sprite IS the drawn sprite. File = "001_%05d.IMG", %05d = idx+1.
constexpr int kSpriteFileNo[2] = { 745, 1028 };

// Per-type geometry — read case by case from the switch @0x5AF577 of
// HUD_RenderFloatingMessages 0x5AF4C0.
//   tex      : 0 = sprite idx 744, 1 = sprite idx 1027
//   designX/Y: a3/a4 arguments of UI_ProjectSpriteToScreen
//   spriteDy : dy of Sprite2D_Draw
//   textDy   : dy of UI_DrawNumberValue
//   textCx   : horizontal centering -> x + textCx - textWidth/2
struct TypeLayout { int tex; int designX; int designY; int spriteDy; int textDy; int textCx; };
constexpr TypeLayout kLayout[FloatingNotices::kSlotCount] = {
    /* 0 */ { 0, 287, 56, 22, 26, 225 }, // @0x5AF59A / 0x5AF5B4 / 0x5AF5C4 / 0x5AF5CA
    /* 1 */ { 0, 287, 56,  0,  4, 225 }, // @0x5AF62C / 0x5AF643 / 0x5AF653 / 0x5AF659
    /* 2 */ { 0, 287, 56, 22, 26, 225 }, // same as case 0 (case 0/2 grouped @0x5AF577)
    /* 3 */ { 0, 287, 56, 44, 48, 225 }, // @0x5AF74D / 0x5AF767 / 0x5AF777 / 0x5AF77D
    /* 4 */ { 0, 287, 56, 66, 70, 225 }, // @0x5AF7DF / 0x5AF7F9 / 0x5AF809 / 0x5AF80F
    /* 5 */ { 0, 287, 56, 66, 70, 225 }, // same as case 4 (case 4/5 grouped)
    /* 6 */ { 0, 287, 56, 88, 92, 225 }, // @0x5AFAB9 / 0x5AFAD3 / 0x5AFAE3 / 0x5AFAE9
    /* 7 */ { 0, 287, 56, 88, 92, 225 }, // same as case 6 (case 6/7/8/9 grouped)
    /* 8 */ { 0, 287, 56, 88, 92, 225 },
    /* 9 */ { 0, 287, 56, 88, 92, 225 },
    /*10 */ { 0, 287, 56, 44, 48, 225 }, // same as case 3 (case 3/10 grouped)
    /*11 */ { 0, 287, 56,  0,  4, 225 }, // same as case 1 (case 1/11 grouped)
    /*12 */ { 1, 287, 78,  0, 16, 222 }, // @0x5AFC6C / 0x5AFC83 / 0x5AFC93 / 0x5AFC99
};

// Type 12 ONLY: 2nd line (text2_) @0x5AFCE5 (dy) / 0x5AFCEB (cx) / 0x5AFCFF
// (measure) / 0x5AFD1E (draw).
constexpr int kType12Line2Dy = 38;
constexpr int kType12Line2Cx = 222;

// Literal color index `1` passed to UI_DrawNumberValue 0x53FCC0 @0x5AF606.
// Warning: this is a palette INDEX, not a color: UI_DrawNumberValue does
// `ColorTable_GetColor(dword_84DF20, 1)` @0x53FCD2. ColorTable_InitPalette 0x4C1D60
// sets `this[1] = 0xFFFFFFFF` @0x4C1D73 -> opaque white. Resolved here via the SAME
// accessor (game::ColorPalette::Get == ColorTable_GetColor) rather than hardcoded.
constexpr int kNoticeColorIndex = 1;

// Mutual extinctions — switch @0x5AEF7A of HUD_ShowFloatingMessage. Each Show
// turns off competing slots. Written-offset -> slot conversion: (offset - 8) / 4.
// Without this table, contradictory notices would coexist on screen.
struct Extinction { int count; int slot[3]; };
constexpr Extinction kExtinct[FloatingNotices::kSlotCount] = {
    /* 0 */ { 2, {  2, 12, -1 } }, // +16 @0x5AEF84 ; +56 @0x5AEF8E
    /* 1 */ { 1, { 11, -1, -1 } }, // +52 @0x5AEFAD
    /* 2 */ { 2, {  0, 12, -1 } }, // +8  @0x5AEFCC ; +56 @0x5AEFD6
    /* 3 */ { 1, { 10, -1, -1 } }, // +48 @0x5AF04A
    /* 4 */ { 1, {  5, -1, -1 } }, // +28 @0x5AF0F8
    /* 5 */ { 1, {  4, -1, -1 } }, // +24 @0x5AF16C
    /* 6 */ { 3, {  7,  8,  9 } }, // +36 @0x5AF1E0 ; +40 @0x5AF1EA ; +44 @0x5AF1F4
    /* 7 */ { 3, {  6,  8,  9 } }, // +32 @0x5AF213 ; +40 @0x5AF21D ; +44 @0x5AF227
    /* 8 */ { 3, {  6,  7,  9 } }, // +32 @0x5AF246 ; +36 @0x5AF250 ; +44 @0x5AF25A
    /* 9 */ { 3, {  6,  7,  8 } }, // +32 @0x5AF279 ; +36 @0x5AF283 ; +40 @0x5AF28D
    /*10 */ { 1, {  3, -1, -1 } }, // +20 @0x5AF2AC
    /*11 */ { 1, {  1, -1, -1 } }, // +12 @0x5AF332
    /*12 */ { 2, {  0,  2, -1 } }, // +8  @0x5AF3A3 ; +16 @0x5AF3AD
};

} // namespace

// PIMPL: lazy cache of the two anchor/draw sprites (cf. "INCLUSION NOTE"
// in the header — keeps <d3dx9.h> out of UI/ChatWindow.h).
struct FloatingNotices::Gpu {
    gfx::GpuTexture tex[2];
    bool            tried[2] = { false, false };
};

FloatingNotices::FloatingNotices() : gpu_(new Gpu()) {}
FloatingNotices::~FloatingNotices() = default;

// `else` branch of the scene guard: clears the 13 slots @0x5AF4FA.
void FloatingNotices::ClearAll() {
    active_.fill(0);
}

// HUD_ShowFloatingMessage 0x5AEEC0.
void FloatingNotices::Show(int type, int subType, const std::string& text,
                           const std::string& text2) {
    // SIGNED guard @0x5AEECD (`jl`) / @0x5AEED3 (`jle 0Ch`): outside [0,12] -> jumps to
    // the switch's `default`, i.e. returns without doing anything (@0x5AEED5).
    if (type < 0 || type >= kSlotCount) return;

    (void)subType; // SOUND selector only (sub-switch @0x5AEFF5..) — not reproduced, cf. banner of the .h

    active_[static_cast<size_t>(type)] = 1;              // *(this + 4*type + 8) = 1  @0x5AEEE0

    // Crt_Memset(this + 1373, 0, 0x65) @0x5AEEF6 then Crt_StringInit(this + 1373, arg_C)
    // @0x5AEF26: the 2nd line is UNCONDITIONALLY reset on EVERY Show
    // (not only for type 12) — the reset happens before the type switch.
    text2_ = text2.substr(0, static_cast<size_t>(kTextLen - 1));

    // Crt_StringInit(this + 101*type + 60, arg_8) @0x5AEF0B/@0x5AEF10: 101-byte
    // buffer NUL included -> 100 usable characters.
    text_[static_cast<size_t>(type)] = text.substr(0, static_cast<size_t>(kTextLen - 1));

    // *(float *)(this + 4*type + 1476) = g_GameTimeSec @0x5AEF3A. The original reads the
    // GLOBAL flt_815180 (not a parameter): mirrored here via its C++ counterpart
    // gfx::g_GameTimeSec (Gfx/SpriteBatch.cpp L11 "flt_815180 / g_GameTimeSec").
    // CONSISTENCY WITH Render() VERIFIED: App.cpp L764-765 writes `gfx::g_GameTimeSec` and
    // `game::g_World.gameTimeSec` from THE SAME `gameClockSec_` on two adjacent lines
    // -> the `nowSec` received by Render() (from ChatWindow::Tick(
    // game::g_World.gameTimeSec), UI/GameHud.cpp L1283) carries the same value. No
    // possible drift between the timestamp and the lifetime test.
    ts_[static_cast<size_t>(type)] = gfx::g_GameTimeSec;

    // Type 12 only: ts[12] += 20.0 (dbl_7A7368) @0x5AEF45/@0x5AEF60. `this+1524` IS
    // ts[12] (1476 + 4*12) -> POSTDATED timestamp, hence 30 s useful lifetime, not 10 s.
    if (type == 12) ts_[12] += kType12TimeBonus;

    // Mutual extinctions from the switch @0x5AEF7A.
    const Extinction& ex = kExtinct[static_cast<size_t>(type)];
    for (int k = 0; k < ex.count; ++k) {
        active_[static_cast<size_t>(ex.slot[k])] = 0;
    }
}

// UI_ProjectSpriteToScreen 0x50F5D0 :
//   x = Crt_ftol((double)(screenW * (designX + W/2)) / refW) - W/2   @0x50F5FC..0x50F634
//   y = Crt_ftol((double)(screenH * (designY + H/2)) / refH) - H/2   @0x50F645..0x50F68B
// where W/H = Sprite2D_GetWidth/GetHeight(g_AssetMgr_UiAtlasSlots + 148*idx), and refW/refH
// = fields +8/+12 of g_PlayerCmdController 0x1669170 = 1024.0f/768.0f (ts2::kRefWidth/
// kRefHeight). Crt_ftol truncates toward zero, like static_cast<int> from a double.
// Anchors the sprite's CENTER at the same screen fraction as its design position;
// the sprite itself is NOT scaled. At 1024x768 this reduces to (designX, designY).
void FloatingNotices::Project(int designX, int designY, int spriteW, int spriteH,
                              int screenW, int screenH, int& outX, int& outY) {
    const int halfW = spriteW / 2;
    const int halfH = spriteH / 2;
    outX = static_cast<int>(static_cast<double>(screenW * (designX + halfW)) /
                            static_cast<double>(ts2::kRefWidth)) - halfW;
    outY = static_cast<int>(static_cast<double>(screenH * (designY + halfH)) /
                            static_cast<double>(ts2::kRefHeight)) - halfH;
}

// Lazy loading of one of the two sprites. The device is obtained via
// ID3DXSprite::GetDevice (same technique as ChatWindow::EnsureWhiteTexture, which avoids
// making the widget depend on gfx::Renderer).
gfx::GpuTexture* FloatingNotices::EnsureTexture(gfx::SpriteBatch& sprites, int which) {
    if (!gpu_ || which < 0 || which > 1) return nullptr;
    if (gpu_->tried[which])
        return gpu_->tex[which].Valid() ? &gpu_->tex[which] : nullptr;

    ID3DXSprite* spr = sprites.Get();
    if (!spr) return nullptr;                        // sprite not created yet -> retry next frame
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(spr->GetDevice(&dev)) || !dev) return nullptr;

    gpu_->tried[which] = true;                       // REAL attempt made: don't keep retrying every frame
    asset::ImgFile img;
    const std::string path = GxdCategory1Path(kSpriteFileNo[which]);
    if (!path.empty() && img.Load(path))
        gpu_->tex[which].CreateFromImgFile(dev, img);
    dev->Release();

    return gpu_->tex[which].Valid() ? &gpu_->tex[which] : nullptr;
}

// HUD_RenderFloatingMessages 0x5AF4C0.
//
// ASSUMED DEVIATION: the binary runs ONE loop that alternates Sprite2D_Draw and
// UI_DrawNumberValue per slot; here rendering is split into a sprite pass
// (SpriteBatch::Begin/End) then a text pass (Font::BeginBatch/EndBatch), a constraint
// imposed by the batch API — same split as ChatWindow (RenderTabPanels /
// RenderTabLabels). Pixel-identical result: a slot's sprites and its text never
// overlap (text is drawn BELOW the sprite, text dy > sprite dy).
void FloatingNotices::Render(gfx::SpriteBatch& sprites, gfx::Font& font, float nowSec,
                             int screenW, int screenH) {
    // Guard @0x5AF4DA: `if (g_SceneMgr == 6 && g_SceneSubState == 4)`, else clears the
    // 13 slots @0x5AF4FA. The `g_SceneMgr == 6` (InGame) side is STRUCTURALLY
    // satisfied by the call site: this method is only reached via
    // ChatWindow::Render <- GameHud::Render, itself called under the SOLE
    // `case Scene::InGame` of SceneManager::Render (Scene/SceneManager.cpp L1300).
    // ClientSource does not expose a global mirror of g_SceneMgr 0x1676180 (only
    // ts2::g_SceneSubState 0x1676184 is, cf. Scene/SceneManager.h L54) — testing the
    // sub-state alone here is therefore EXACTLY equivalent to the original guard.
    if (ts2::g_SceneSubState != kSubStateMainTick) {
        ClearAll();
        return;
    }

    // Expiration: `g_GameTimeSec - ts <= 10.0` @0x5AF552, else slot = 0 @0x5AF55A.
    // HARD cutoff (no fade/lerp/alpha ramp in the original — cf. the observation already
    // recorded in Game/ClientRuntime.h::MessageLog::Floating). Done BEFORE drawing and
    // independently of sprite/font availability, so slots still expire even if
    // rendering is unavailable.
    for (int i = 0; i < kSlotCount; ++i) {
        if (!active_[static_cast<size_t>(i)]) continue;
        if (nowSec - ts_[static_cast<size_t>(i)] > kLifetimeSec)
            active_[static_cast<size_t>(i)] = 0;
    }

    // Resolve the two sprites BEFORE opening the batch (ID3DXSprite::Begin saves the
    // device state: avoid creating a texture mid-batch) and only ONCE for both passes.
    // Loaded only if an active slot requires it — the type 12 sprite (001_01028.IMG)
    // is therefore never loaded as long as no notice of that type is displayed.
    gfx::GpuTexture* tex[2] = { nullptr, nullptr };
    bool needed[2] = { false, false };
    for (int i = 0; i < kSlotCount; ++i) {
        if (active_[static_cast<size_t>(i)])
            needed[kLayout[static_cast<size_t>(i)].tex] = true;
    }
    for (int t = 0; t < 2; ++t) {
        if (needed[t]) tex[t] = EnsureTexture(sprites, t);
    }

    // --- Sprite pass (Sprite2D_Draw 0x4D6B20: full-frame white blit, unscaled).
    if (sprites.Ready()) {
        sprites.Begin();
        for (int i = 0; i < kSlotCount; ++i) {
            if (!active_[static_cast<size_t>(i)]) continue;
            const TypeLayout& L = kLayout[static_cast<size_t>(i)];
            gfx::GpuTexture* t = tex[L.tex];
            if (!t || !t->Handle()) continue; // faithful: the binary has no graphical fallback
            int x = 0, y = 0;
            Project(L.designX, L.designY, static_cast<int>(t->Width()),
                    static_cast<int>(t->Height()), screenW, screenH, x, y);
            sprites.DrawSprite(t->Handle(), nullptr, x, y + L.spriteDy, gfx::kSpriteWhite);
        }
        sprites.End();
    }

    // --- Text pass (UI_DrawNumberValue 0x53FCC0 -> UI_DrawText 0x69E750 mode 2 =
    // 8-direction black outline == gfx::kStyleOutline).
    if (!font.Ready()) return;
    const D3DCOLOR color = game::g_Strings.colors.Get(kNoticeColorIndex); // ColorTable_GetColor 0x4C1FE0

    font.BeginBatch();
    for (int i = 0; i < kSlotCount; ++i) {
        if (!active_[static_cast<size_t>(i)]) continue;
        const TypeLayout& L = kLayout[static_cast<size_t>(i)];

        // The text is anchored on the SAME projected coordinates as the sprite: if the
        // texture failed to load, W/H are 0 and the projection degenerates to
        // (designX*screenW/1024, designY*screenH/768) — the text stays readable rather
        // than disappearing with the sprite (only fallback kept, no colored rect).
        const gfx::GpuTexture* t = tex[L.tex];
        const int tw = t ? static_cast<int>(t->Width())  : 0;
        const int th = t ? static_cast<int>(t->Height()) : 0;
        int x = 0, y = 0;
        Project(L.designX, L.designY, tw, th, screenW, screenH, x, y);

        // x + textCx - UI_MeasureNumberText(txt)/2  (@0x5AF5CA/0x5AF5E3/0x5AF606).
        // UI_MeasureNumberText 0x53FCA0 == UI_MeasureText(g_GfxRenderer, txt) 0x69E680
        // == gfx::Font::MeasureText.
        const char* txt = text_[static_cast<size_t>(i)].c_str();
        const int   w   = font.MeasureText(txt);
        font.DrawTextStyled(txt, x + L.textCx - w / 2, y + L.textDy, color, gfx::kStyleOutline);

        // Type 12 only: 2nd line (this+1373) @0x5AFCE5..0x5AFD1E.
        if (i == 12) {
            const char* txt2 = text2_.c_str();
            const int   w2   = font.MeasureText(txt2);
            font.DrawTextStyled(txt2, x + kType12Line2Cx - w2 / 2, y + kType12Line2Dy,
                                color, gfx::kStyleOutline);
        }
    }
    font.EndBatch();
}

} // namespace ts2::ui

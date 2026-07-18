// UI/MinimapWidget.h — in-game HUD minimap (§12 of Docs/TS2_UI_GAMEHUD_RENDER.md).
//
// Binary anchor: minimap block of UI_GameHud_Render 0x67A3C0, EA 0x681230-0x683934
// (size-toggle button 0x6815BD-0x6816E6, visible window/clamp 0x6818CD-0x681D70,
// markers 0x681D70-0x683876). Not a ts2::ui::Dialog (always shown, outside
// UIManager's routing chain) — same status as GameHud's quickslot bar.
//
// Widget with NO GPU resources of its own: it draws through the SpriteBatch/Font/
// white texture already owned by the caller (GameHud), exactly like
// GameHud::DrawVitalsFrame()/DrawQuickSlotFrames() draw into the same batch —
// the pattern requested by the mission ("Layout struct + DrawFilledRect/
// DrawBarFill/GpuTexture", transposed here without duplicating any device/texture).
//
// Fidelity vs accepted simplifications (static RE available but runtime
// dimensions/tables not statically readable, see GameHud.cpp banner):
//   - §12a Two sizes (this+612): small/large -> bigMode_ below.
//   - §12b Three visible-window modes (this+616, switch 0/1/2): modeled by
//     MinimapWindowMode {Full, ClampedCenter, Free}.
//
// BEW-01 (2026-07-16) — THE MAP BACKGROUND IS NOW ACTUALLY BLITTED. Two claims from
// the old banner were FALSE and are corrected here (re-proven in IDA):
//   (1) "per-mode scales (dword_14A906C/dword_14A9070)": FALSE. @0x681560 `mov ecx,
//       ds:dword_14A906C[eax]` / @0x68157B `mov ecx, ds:dword_14A9070[eax]` with eax = 0x28*mode.
//       But dword_14A906C == unk_14A9068+4 and dword_14A9070 == unk_14A9068+8: these are the
//       +4/+8 fields of the TEXTURE OBJECT at index `mode` (stride 40), i.e. the LOGICAL WIDTH
//       and HEIGHT of the zone image (`qmemcpy(this+1, header, 0x1C)` @0x6A2FFE from
//       Tex_LoadCompressedDDS 0x6A2E80). No scale table exists.
//   (2) "world bounds NOT loaded in this model": FALSE since GX-ICON-01 —
//       WorldAssets::MinimapWorldBounds() (World/WorldIntegration.h) supplies exactly
//       dword_14A88C8 (@0x681513-0x68154B, with its TWO fchs on Z).
// Both now arrive via SetSourceProvider (see MinimapSource below), and the
// empirical constant kWorldViewRadius=4000 is GONE. Widget still has NO GPU resource of
// its own: it BORROWS the world's texture (in the target it lives at world+2092+40*mode, not in the HUD).
//   - §12c Markers: players (game::g_World.players, excluding self) and monsters
//     (game::g_World.monsters) projected and clamped/omitted based on the current mode.
//     NPC (§12c "NPC"): the doc confirms the original reads `dword_1764D18`/
//     `g_NpcRenderArray` — NOT the network gameplay array `dword_17AB534`
//     (`game::NpcEntity`/`g_World.npcs`, now positioned via body+16/20/24
//     but used ONLY for interaction/targeting, see the NpcEntity comment
//     in Game/GameState.h and Docs/TS2_NPC_MESH_DRAW.md). The client-source
//     equivalent of `g_NpcRenderArray` is `game::ZoneNpcs()` (Game/
//     StaticNpcLoader.h, already populated with real x/y/z and wired to
//     OnSpawnCharacter(self)) -> now projected below. Per-type icons
//     (§12c: 5 variants based on +1312) not modeled, for lack of confirmed
//     semantics for `NpcDefRecord::fieldB` -> a single-color dot
//     (kNpcDotColor), the same simplification as remote monsters/players.
//     Party/alliance: no alliance roster on the GameState side
//     (original's g_AllianceRosterNames not reproduced) -> not projected (TODO,
//     clean fallback: the rest of the HUD keeps working).
//   - Quest-highlight blink (§9/§12c, original formula
//     `Crt_ftol(g_GameTimeSec*2)%2==1`): mechanism wired faithfully
//     (SetQuestHighlightMonster/ClearQuestHighlight) but INERT by default,
//     for lack of a quest system feeding Game/GameState.h in this pass.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <functional>
#include <utility>

#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Game/GameState.h"
#include "Core/Types.h"

namespace ts2::ui {

// Current zone's map background, as read by UI_GameHud_Render's minimap block.
// A single provider (the world) fills it in; the widget owns NOTHING here.
//   tex             <- unk_14A9068 + 0x28*mode, field +36 (D3D9 surface)          @0x681AAB/@0x6A3040
//   imgW / imgH     <- fields +4 / +8 of the same object = LOGICAL dims of the image @0x681560/@0x68157B
//   minX / maxX     <- dword_14A88C8[+0] / [+0Ch]                                 @0x681519/@0x681527
//   negMaxZ/negMinZ <- -dword_14A88C8[+14h] / -dword_14A88C8[+8]  (fchs!)        @0x681535/@0x681546
struct MinimapSource {
    IDirect3DTexture9* tex = nullptr;
    int   imgW = 0, imgH = 0;
    float minX = 0.0f, maxX = 0.0f;
    float negMaxZ = 0.0f, negMinZ = 0.0f;
};

// Returns false if the zone has no map background (yet) -> falls back to the flat fill.
// `mode` = this+616 (0/1/2): it's what indexes the 3 textures in the target.
using MinimapSourceProvider = std::function<bool(int mode, MinimapSource& out)>;

// Simple screen rect (same shape as GameHud::HudRect, duplicated here to avoid
// coupling MinimapWidget.h to GameHud.h — see banner above).
struct MmRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Minimap's visible-window mode (this+616 in the binary, §12b). This field is ALSO
// the index into the 3 zone textures (`imul ecx, 28h ; add ecx, offset unk_14A9068` @0x681AA8): the
// "modes" are actually 3 IMAGES of decreasing resolution (= zoom levels), hence the
// -/+ buttons 0x6773DE/0x67748E. Blit behavior, re-proven byte by byte:
//   Full          (0) @0x6818FB: scrolling 145x128 crop, srcX/srcY clamped to the image edges.
//   ClampedCenter (1) @0x681ABB: IDENTICAL to case 0 (doc §12b: "redundant/refactor leftover").
//   Free          (2) @0x681C7D: srcX=srcY=0 and UI_DrawSprite a4=0 -> FULL source rect
//                                 (0,0,imgW,imgH), whole image at its natural size; and the
//                                 NPC loop is entirely skipped (@0x681DF3).
enum class MinimapWindowMode : uint8_t { Full = 0, ClampedCenter = 1, Free = 2 };

class MinimapWidget {
public:
    MinimapWidget() = default;

    // Computes the layout for these screen dimensions (cGameHud has no dynamic
    // resize hook, called once from GameHud::Init — same limitation as the
    // rest of the HUD).
    void Init(int screenW, int screenH);
    void OnScreenResize(int screenW, int screenH) { Init(screenW, screenH); }

    // BEW-01 — wires up the zone's map background. WITHOUT this provider, DrawPanels falls
    // back to the kViewportBg flat fill (the binary, by contrast, NEVER draws a flat fill: it
    // always blits unk_14A9068[mode]). To be set from Scene/SceneManager.cpp (the only owner
    // of both hud_ AND worldAssets_) — see the front report for the exact line.
    void SetSourceProvider(MinimapSourceProvider p) { sourceProvider_ = std::move(p); }
    bool HasSourceProvider() const { return static_cast<bool>(sourceProvider_); }

    // Toggle small/large (§12a button). SetBigMode allows external wiring
    // (e.g. a hotkey) in addition to OnMouseDown's internal hit-test.
    void ToggleSize();
    void SetBigMode(bool big);
    bool BigMode() const { return bigMode_; }

    // Visible window mode (§12b). Default: Full (=0), the value written by UI_GameHud_Init
    // 0x675184 (`mov [ecx+268h], 0`). WARNING [0x6773DE zoom− / 0x67748E zoom+]: this+616
    // is actually a ZOOM LEVEL bounded to [0,2] (2 −/+ buttons in large mode:
    // this[0x268]-=1 / +=1), NOT a freely-settable 3-state mode. SetWindowMode() therefore has
    // NO binary counterpart (an arbitrary setter); kept for lack of the 2 zoom buttons.
    // NB: the "scale table dword_14A906C/14A9070 not dumped" excuse previously invoked here
    // was FALSE (they are the texture's +4/+8 fields = its logical dims — see header
    // banner, BEW-01); the only remaining work is the 2 buttons -> TODO out of scope.
    void SetWindowMode(MinimapWindowMode mode) { windowMode_ = mode; }
    MinimapWindowMode WindowMode() const { return windowMode_; }

    // Attachment point for a future quest system (§9/§12c): the designated
    // monster blinks on the minimap per the original formula. No caller
    // wires this up yet (TODO, see header banner).
    void SetQuestHighlightMonster(game::EntityId id) { questHighlightMonster_ = id; }
    void ClearQuestHighlight() { questHighlightMonster_ = game::EntityId{}; }

    // Hit-test + size toggle ("first consumer wins", called by
    // GameHud::OnMouseDown before the generic HUD click). Returns true if
    // the event was consumed (toggle button OR click inside the panel).
    bool OnMouseDown(int x, int y);

    // "Panels" pass: background + viewport + dots (self/players/monsters).
    // Must be called INSIDE a sprite.Begin()/End() already opened by the caller (see
    // GameHud::Render — same sprite_/white_ as the rest of the HUD).
    void DrawPanels(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex);

    // Text pass: toggle button label + (large mode) zone name and
    // player coordinates. Must be called INSIDE a font.BeginBatch()/EndBatch().
    void DrawText(gfx::Font& font);

private:
    struct Layout {
        MmRect frame;     // full panel (§12a background)
        MmRect viewport;  // marker projection area ("map")
        MmRect toggleBtn; // size toggle button (§12a)
    };

    void RecomputeLayout();

    static void DrawFilledRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                               const MmRect& r, D3DCOLOR color);
    static void DrawBorderRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                               const MmRect& r, int thickness, D3DCOLOR color);
    static void DrawDot(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                        int cx, int cy, int size, D3DCOLOR color);

    // --- PROVEN projection (replaces the old fixed-radius "radar" ProjectToViewport) -----
    // World -> pixel WITHIN THE zone IMAGE. @0x681915-0x68193C (X) / @0x681942-0x681969 (Y):
    //   px = ftol( imgW * ( wx - minX)     / (maxX    - minX) )
    //   py = ftol( imgH * (-wz - negMaxZ)  / (negMinZ - negMaxZ) )
    // (the -wz comes from `fchs` @0x68190D; Crt_ftol 0x760810 truncates toward zero.)
    static void WorldToImagePixel(const MinimapSource& s, float wx, float wz, int& px, int& py);

    // Scrolling 145x128 crop centered on self, clamped to the image edges.
    // @0x68198F-0x6819FC (X) / @0x681A02-0x681A54 (Y):
    //   srcX = clamp(selfPx - 0x48, 0, imgW - 0x91) ; srcY = clamp(selfPy - 0x40, 0, imgH - 0x80)
    static void ComputeCrop(const MinimapSource& s, int selfPx, int selfPy, int& srcX, int& srcY);

    // Image pixel -> screen pixel, accounting for the current crop. @0x681E7E-0x681F05:
    //   sx = frame.x + px - srcX + 4   ;   sy = frame.y + py - srcY + 0x2A
    // Returns false if OUT of bounds -> the marker is OMITTED (jmp loc_68229E @0x681F05),
    // NEVER clamped to the edge: the old "radar" model (clamp in Full/ClampedCenter, omission
    // in Free) was inverted and wrong in both directions.
    //   kept iff  frame.x+4 <= sx <= frame.x+0x96   AND   frame.y+0x2A <= sy <= frame.y+0xA8
    bool MarkerScreenPos(int px, int py, int srcX, int srcY, int& sx, int& sy) const;

    MinimapSourceProvider sourceProvider_; // BEW-01 — map background supplied by the world

    int  screenW_ = ts2::kRefWidth;
    int  screenH_ = ts2::kRefHeight;
    // Defaults corrected from UI_GameHud_Init 0x675140 (cross-checked in IDA):
    //   this+612 (0x264) <- 1 @0x675177: the minimap starts in LARGE mode (previous
    //     default false = unfaithful).
    //   this+616 (0x268) <- 0 @0x675184: level this+616 = 0 (see WindowMode banner above).
    bool bigMode_ = true;                                               // this+612 (default 1 @0x675177)
    MinimapWindowMode windowMode_ = MinimapWindowMode::Full;            // this+616 (default 0 @0x675184)

    game::EntityId questHighlightMonster_{}; // blink mechanism (inert)

    Layout layout_{};
};

} // namespace ts2::ui

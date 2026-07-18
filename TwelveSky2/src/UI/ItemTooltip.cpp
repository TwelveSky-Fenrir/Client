// UI/ItemTooltip.cpp — item tooltip implementation (Item_DrawTooltip
// 0x652AD0). See UI/ItemTooltip.h for the premise correction and the wiring
// TODO (gap TT-01).
#include "UI/ItemTooltip.h"
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h"   // game::Str (StrTable005_Get 0x4C1D10)
#include "Game/StringTables.h"    // game::g_Strings.colors (mFONTCOLOR 0x84DF20)

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ts2::ui {
namespace {

// --- Constants pulled from the stack frame / operands of 0x652AD0 -----
constexpr int kLineStride = 101; // `imul …, 65h` — stride of a line (var_3380)
constexpr int kMaxLines   = 100; // var_8D8 = _DWORD[101], var_3380 = char[10104]
constexpr int kTileW      = 13;  // `imul eax, 0Dh` @0x65e2f9
constexpr int kTileH      = 15;  // `imul edx, 0Fh` @0x65e2ec
constexpr int kTextTopPad = 16;  // `add ecx, 10h`  @0x65e333
constexpr int kLineAdv    = 15;  // `add eax, 0Fh`  @0x65e584

// Background tile: Sprite2D unk_9404B0 = slot 2424 of UI atlas category 1
// (`mov ecx, offset unk_9404B0 / call Sprite2D_Draw` @0x65e300-0x65e305).
// (0x9404B0 - 0x8E8B50) / 148 = 2424, remainder 0 -> "001_02425.IMG". The tile is
// EXACTLY 13x15 (the decoded file's GXD header gives dword[0]=13, dword[1]=15),
// which matches the disassembly's 0x0D/0x0F strides.
constexpr int kTooltipBgSlot = 2424;

// Fallback if the atlas is missing/unreadable: the binary would draw nothing at
// all, but a neutral background keeps the text readable instead of drowning it
// in the 3D scene. The ONLY deliberate divergence in this file, confined to the
// error path.
constexpr D3DCOLOR kColBgFallback = 0xE0202028u;

// --- One built line: text + color INDEX (var_3380[] / var_8D8[]) ---
// The binary NEVER stores a D3DCOLOR here: var_8D8[i] is an index resolved at
// draw time by ColorTable_GetColor(dword_84DF20, idx) in UI_DrawNumberValue
// 0x53FCC0 (@0x53fcd2).
struct TooltipLine {
    char text[kLineStride];
    int  colorIdx;
};

class LineList {
public:
    // Equivalent to `Crt_Vsnprintf(&var_3380[n*0x65], fmt, …) ; var_8D8[n] = idx ; ++n`.
    void Add(int colorIdx, const char* fmt, ...) {
        if (lines_.size() >= static_cast<size_t>(kMaxLines)) return; // var_3380/var_8D8 bound
        TooltipLine l{};
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(l.text, sizeof(l.text), fmt, ap);
        va_end(ap);
        l.colorIdx = colorIdx;
        lines_.push_back(l);
    }
    int Count() const { return static_cast<int>(lines_.size()); }
    const TooltipLine& operator[](int i) const { return lines_[static_cast<size_t>(i)]; }

private:
    std::vector<TooltipLine> lines_;
};

// --- Content construction (main switch) --------------------------------
// Item_DrawTooltip branches on rec[0xBC] (= ItemInfo::typeCode, +188) via a
// 36-case jumptable: `sub edx, 1 / cmp ecx, 23h / ja def_652BE3 /
// jmp jpt_652BE3[ecx*4]` @0x652bc0-0x652be3 (table 0x65EA1C, index byte_65EA5C;
// cases 25/27/33/34 fall through to the default 0x654AA8).
void BuildLines(const game::ItemInfo* info, LineList& lines) {
    const char* name = info->name; // rec+4 (`add edx, 4` @0x652bf0)

    switch (info->typeCode) {
    // Case 1 (@0x652bea) and 2 (@0x652c37): name only, color index 1.
    //   Crt_Vsnprintf(line, "%s", rec+4) ; var_8D8[n] = 1 ; ++n
    case 1:
    case 2:
        lines.Add(1, "%s", name);                                  // @0x652c18 / 0x652c65
        break;

    // Case 3 (@0x652c84): 6-case sub-switch on rec[0xB8] (= category, +184) —
    // `sub edx, 1 / cmp …, 5 / ja def_652CB8 / jmp jpt_652CB8[eax*4]`
    // @0x652c9c-0x652cb8 (table 0x65EA80). Each case prefixes the name with a
    // 005.DAT label: Crt_Vsnprintf(line, "%s%s", StrTable005_Get(id), rec+4), color 1.
    // The 6 ids are read directly from the `push imm32` operands:
    //   cat 1 -> 0x3E9 @0x652cbf   cat 2 -> 0x3EA @0x652d1c
    //   cat 3 -> 0x3EB @0x652d79   cat 4 -> 0x3EC @0x652dd6
    //   cat 5 -> 0xA00 @0x652e33   cat 6 -> 0xA00 @0x652e8d  (the SAME id, not a typo)
    case 3: {
        int labelId = 0;
        switch (info->category) {
        case 1: labelId = 0x3E9; break;
        case 2: labelId = 0x3EA; break;
        case 3: labelId = 0x3EB; break;
        case 4: labelId = 0x3EC; break;
        case 5: labelId = 0xA00; break;
        case 6: labelId = 0xA00; break;
        default:
            // TODO [anchor 0x652EE5] — category sub-switch default not
            // transposed (body not surveyed). No line emitted rather than
            // inventing one.
            break;
        }
        if (labelId != 0)
            lines.Add(1, "%s%s", game::Str(labelId).c_str(), name);  // "%s%s" 0x7ED578, color @0x652cfd
        break;
    }

    default:
        // TODO [anchor 0x652BE3, jumptable 0x65EA1C / index byte_65EA5C] — the 33
        // other typeCode cases (4..24, 26, 28..32, 35, 36; 25/27/33/34 = default
        // 0x654AA8) each build their own lines via Crt_Vsnprintf with formats
        // "%s", "%s%s", "%s : %d%%" (0x7BAD70), "%s : %s%s" (0x7BAD2C),
        // "%s : %s%s[%s(%d)]" (0x7BAD18) and 005.DAT ids surveyed case by case
        // (e.g. 0x422 @0x65bd67, 0x463 @0x65bdcf), with their own color indices
        // (1, 0x18, 0x1B...). ~143 KB of pseudocode NOT transposed here: each
        // case requires line-by-line proof. Emitting NOTHING is faithful to
        // this file's silence on these cases — an accepted gap, not a guess.
        break;
    }

    // TODO [anchor 0x65E0E0] — PRICE LINE not emitted (gap TT-04, price part).
    // The BODY is proven and transposable as-is:
    //     Str_FormatThousands(rec[0xE0] /*= ItemInfo::field224, +224*/, &buf, &colorIdx)  @0x65e107
    //     Crt_Vsnprintf(line, "%s : %s%s" /*0x7BAD2C*/,
    //                   StrTable005_Get(0x425) /*@0x65e128*/, buf,
    //                   StrTable005_Get(0x24D) /*@0x65e111*/)                             @0x65e149
    //     ++var_3384                                                                       @0x65e157
    // Str_FormatThousands 0x546390 is also fully surveyed (a cascade of 10
    // tiers; the color index is an OUTPUT PARAMETER, not a constant:
    // 1 under a million, then 28 @0x5466f6 / 24 @0x54663a / 3 @0x546568 / 25
    // @0x546480, and EMPTY string + index 2 if the value is negative @0x546985).
    // NOT transposed here: without the guard below it would have no caller.
    // The GUARD leading to this block is NOT established — 0x65E0E0 is not a
    // fallthrough of the preceding "%s : %s%s[%s(%d)]" block (that one ends
    // @0x65e0ce and joins label loc_65E0D1); it's an alternate branch whose
    // condition remains to be proven. Emitting it unconditionally would add a
    // price line to items that don't have one. To complete: decompile block
    // 0x65DF00-0x65E0E0, then transpose both functions together.

    // TODO [anchor 0x65E598] — SECOND PANEL not emitted (gap TT-08a). Proven guard
    // `cmp [ecx+0BCh], 21h / jnz loc_65EA08` @0x65e5a8-0x65e5af (typeCode == 0x21);
    // builds a SEPARATE line block (var_5EF8, same stride 101) with its own
    // color index 0x18 (@0x65e60b/0x65e659/0x65e6a7), 005.DAT labels
    // 0xAF5+ (@0x65e5d7/0x65e625/0x65e673/0x65e6c1) in format "%s : " (0x7BAD10),
    // anchored at var_10 = x - cols*13 (@0x65e5b5-0x65e5ce, SAME formula as the
    // main panel => sits to its LEFT) and var_4 = y (@0x65e5d1). Not transposed:
    // the exact content of the lines (beyond the first 4 ids) was not surveyed.

    // TODO [anchor 0x65BE45-0x65C72F] — GEM SUB-PANEL not emitted (gap TT-08b).
    // 10-slot grid (var_740 0..9 @0x65beac), step 0x37=55, centered on sprite
    // unk_94B87C (slot 2735), cells +0x13/+0x29 (@0x65bed0/0x65bef1). Real
    // decoding of dword_167568C[i] (@0x65bf43-0x65bfc6) — WARNING, the formula
    // is unusual and does NOT match a simple decimal split:
    //     g1 = (a / 0x989680 /*1e7*/) * 10                       -> var_6388
    //     g2 = ((a % 0xF4240 /*1e6*/) / 0x2710 /*1e4*/) * 10     -> var_6380
    //     g3 = ((a % 0x3E8  /*1e3*/) / 0xA) * 10                 -> var_6374
    //     switch (g1 - 0xA)  ; 71 cases ; `cmp …, 0x46`  (jpt_65BFE0 0x65ED1C)
    // then Item_DecodeGemBonus (@0x65c07d, 0x65c0fc, 0x65c17b...). The source
    // table (equivalent of dword_167568C) also has no C++ counterpart.
}

} // namespace

void DrawItemTooltip(const UiContext& ctx, int x, int y, uint32_t itemId,
                     uint32_t socketWord) {
    // MobDb_GetEntry(mITEM 0x8E71EC, arg_8) @0x652afa; nullptr -> `jmp loc_65EA08`
    // @0x652b0e (immediate exit, nothing drawn).
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info) return;

    // TODO [anchors 0x652B1C / 0x652B2D / 0x652B4B / 0x652B7C] — DECODE PROLOGUE
    // not transposed (gap TT-07). The binary decodes `socketWord` (arg_10) across
    // FOUR bytes before the switch:
    //     byte0 -> var_C    (Item_GetAttribByte0 0x545610 @0x652b1c, stored @0x652b21)
    //     byte1 -> var_3388 (Item_GetAttribByte1 0x545640 @0x652b2d, stored @0x652b32)
    //     byte2 -> var_594  (Item_GetAttribByte2 0x545670 @0x652b4b), with the
    //               dedicated test `cmp var_594, 19h / mov var_73C, 1` @0x652b60-0x652b69
    //     byte3 -> var_59C  (Item_GetAttribByte3 0x5456A0 @0x652b7c, stored @0x652b81)
    // The 4 accessors already exist on the C++ side (Game/ItemSystem.h, Item_GetAttribByte0..3).
    // They are NOT called here because their ONLY readers are the main-switch
    // cases left as TODO in BuildLines(): decoding them now would produce four
    // variables no one consumes. To transpose AT THE SAME TIME as the cases that read them.
    (void)socketWord;

    // TODO [anchor 0x652B93] — `Item_ClassifyRecord(unk_1685740, rec)` -> var_744
    // (category 0..9) is NOT called: the function is indeed transposed on the C++
    // side but stays INTERNAL to Game/StatFormulas.cpp (l.40; no declaration in
    // Game/StatFormulas.h), and this front does not own those files. Wiring to be
    // done by their owner: expose `int Item_ClassifyRecord(const ItemInfo*);`
    // in Game/StatFormulas.h, then call it here before the switch. Same note as
    // above: var_744 is only read by cases not yet transposed.

    // --- Content ---------------------------------------------------------------
    LineList lines;
    BuildLines(info, lines);
    const int lineCount = lines.Count();
    if (lineCount <= 0) return; // nothing to draw (untransposed typeCode case)

    // --- Geometry (EA 0x65E160-0x65E336) --------------------------------------
    // WARNING: var_5A8 is a length in CHARACTERS (Crt_Strlen @0x65e1a4 /
    // @0x65e1c5), NOT a pixel width — the whole panel geometry is derived from
    // the character count of the longest line.
    int maxLen = 0; // var_5A8, reset to 0 @0x65e160
    for (int i = 0; i < lineCount; ++i) {
        const int len = static_cast<int>(std::strlen(lines[i].text));
        if (len > maxLen) maxLen = len; // `cmp eax, var_5A8 / jle` @0x65e1ac
    }

    // CONDITIONAL clamp to 33 characters (@0x65E1D5-0x65E247):
    //   category (+184) ∈ {3,4,5,6}  AND  (typeCode (+188) ∈ [7,0x15]  OR  == 0x1D)
    // -> maxLen = max(maxLen, 0x21). No floor otherwise.
    {
        const uint32_t cat = info->category;
        const uint32_t tc  = info->typeCode;
        const bool catHit = (cat == 3 || cat == 4 || cat == 5 || cat == 6); // @0x65e1db-0x65e20f
        const bool tcHit  = ((tc >= 7 && tc <= 0x15) || tc == 0x1D);        // @0x65e217-0x65e23c
        if (catHit && tcHit && maxLen < 0x21) maxLen = 0x21;                // @0x65e23e-0x65e247
    }

    // cols = (maxLen - 1) / 2 + 1  (SIGNED division: `sub eax,1 / cdq / sub eax,edx
    // / sar eax,1 / add eax,1` @0x65e251-0x65e25f); rows = lineCount + 2 (@0x65e273).
    const int cols = (maxLen - 1) / 2 + 1;
    const int rows = lineCount + 2;

    // Anchoring: the panel sits TO THE LEFT of the cursor (FULL width
    // subtracted, not half) and is CENTERED VERTICALLY on it. No screen clamp
    // in the binary — the tooltip can overflow, we don't "fix" it.
    const int tx = x - cols * kTileW;              // `imul eax,0Dh / sub edx,eax` @0x65e262-0x65e26a
    const int ty = y - (rows * kTileH) / 2;        // `imul eax,0Fh / cdq / sar / sub ecx,eax` @0x65e276-0x65e283

    // --- Panels phase: background tiling (EA 0x65E286-0x65E30A) ------------------
    if (ctx.phase == UiPhase::Panels) {
        // The binary NEVER draws a flat rectangle here: a nested loop of
        // un-stretched blits of the SAME 13x15 tile.
        static const PanelSkin s_bg(PanelSkin::Cat1Slot{kTooltipBgSlot});
        if (s_bg.TexW(ctx) != 0) {
            for (int r = 0; r < rows; ++r)                                   // @0x65e2a1-0x65e2b0
                for (int c = 0; c < cols; ++c)                               // @0x65e2cd-0x65e2e4
                    s_bg.Draw(ctx, tx + c * kTileW, ty + r * kTileH);        // @0x65e2e6-0x65e305
        } else {
            // Fallback outside the binary (atlas missing): a single rect at the
            // exact size the mosaic would have covered.
            ctx.FillRect(tx, ty, cols * kTileW, rows * kTileH, kColBgFallback);
        }
        return;
    }

    // --- Text phase: CENTERED lines (EA 0x65E30E-0x65E3C5, advance @0x65E584) ---
    // centerX = tx + (cols*13)/2  (`imul 0Dh / cdq / sar / add arg_0` @0x65e30e-0x65e32a)
    const int centerX = tx + (cols * kTileW) / 2;
    int lineY = ty + kTextTopPad; // var_3778 = arg_4 + 0x10 @0x65e330-0x65e336

    for (int i = 0; i < lineCount; ++i) {
        const TooltipLine& l = lines[i];

        // x = centerX - width/2 (@0x65e394-0x65e3a7). The binary measures with
        // the numeric bitmap font (UI_MeasureNumberText 0x53FCA0, this = unk_1685740)
        // and draws via UI_DrawNumberValue 0x53FCC0 -> UI_DrawText(g_GfxRenderer, s,
        // x, y, color, 2). Here we go through ctx.MeasureText/ctx.Text (ID3DXFont):
        // FONT and style divergence (2 = shadowed/outlined vs normal mode) already
        // documented for the whole shell, not specific to this file.
        const int lx = centerX - ctx.MeasureText(l.text) / 2;

        // Color = ColorTable_GetColor(dword_84DF20, idx) @0x53fcd2 — i.e. the
        // mFONTCOLOR palette (45 entries, 1-based index, 0xFF000000 out of bounds).
        const D3DCOLOR col = static_cast<D3DCOLOR>(game::g_Strings.colors.Get(l.colorIdx));

        ctx.Text(l.text, lx, lineY, col);
        lineY += kLineAdv; // `add eax, 0Fh` @0x65e584
    }
}

void DrawItemTooltip(const UiContext& ctx, int x, int y, const game::InvCell& cell) {
    if (cell.empty()) return;
    // InvCell::color IS the attribute word (see Net/ItemActionDispatch.cpp:
    // `eq.socket = cell.color;`).
    DrawItemTooltip(ctx, x, y, cell.itemId, cell.color);
}

} // namespace ts2::ui

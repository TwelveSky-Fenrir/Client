// UI/BuffStatusPanel.cpp — implementation of the buff grid (§9) and the
// bottom-right status panel (§16). See UI/BuffStatusPanel.h for scope, the
// assumed simplifications, and the icon resolution method.
#include "UI/BuffStatusPanel.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h" // game::g_Client.VarGet (mission "CABLAGE GRILLE DE BUFFS" [buff grid wiring], 2026-07-14)
#include "Asset/ImgFile.h"
#include "Core/Types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring> // std::memcpy (reads the 8-byte entries of the §9.10 bank)

namespace ts2::ui {

namespace {

// --- §9.10, timed-debuff bank (EA 0x67D560-0x67D77F) -------------------
// See the CollectZoneStateBuffs banner for proof of each value.
constexpr size_t kZoneStateBytes = 288; // 36 * 8 == sizeof(RecvPackets.h::zoneStateBlock)
constexpr int    kZoneBankCount  = 36;  // `cmp var_438, 24h / jge` @0x67D578
constexpr int    kZoneSkipLo     = 0x13; // 19 — `cmp var_438, 13h / jl`  @0x67D597
constexpr int    kZoneSkipHi     = 0x1C; // 28 — `cmp var_438, 1Ch / jg`  @0x67D5A0

// Same path template as used by GameHud.cpp (kVitalsFrameImgPath):
// category 1 of the shared Sprite2D table (base unk_8E8B50, cf. GameHud.cpp's
// header banner for the full derivation method). %05d = file number
// (table index + 1).
std::string GxdCategory1Path(int fileNo) {
    if (fileNo <= 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\001\\001_%05d.IMG", fileNo);
    return std::string(buf);
}

// Table of icons ACTUALLY resolved for the §9 grid (see the header's top
// banner for the method). Indexed by BuffIconId. Each `fileNo` was computed as
// `(unk_address - 0x8E8B50) / 148 + 1`, division verified exact for the 33 addresses
// below (33, not 34: kBuffKnownIconCount also counts the enum's final sentinel).
struct KnownIconDef { int fileNo; const char* symbol; const char* note; };
constexpr KnownIconDef kKnownIcons[kBuffKnownIconCount] = {
    { 3735, "unk_96FA08", "combo elementaire A" },          // kBuffComboA
    { 3737, "unk_96FB30", "combo elementaire B" },          // kBuffComboB
    { 3739, "unk_96FC58", "combo elementaire C" },          // kBuffComboC
    { 2820, "unk_94E90C", "etat elementaire 1" },           // kBuffElemState1
    { 2821, "unk_94E9A0", "etat elementaire 2" },           // kBuffElemState2
    { 2822, "unk_94EA34", "etat elementaire 3" },           // kBuffElemState3
    { 2823, "unk_94EAC8", "etat elementaire 4" },           // kBuffElemState4
    {  870, "unk_9081B4", "synergie paire elementaire" },   // kBuffElemPair
    { 3990, "unk_978D74", "loadout competences favorable" },// kBuffLoadoutGood
    { 3992, "unk_978E9C", "loadout competences defavorable" }, // kBuffLoadoutBad
    { 4165, "unk_97F2A0", "bonus rang (defaut)" },          // kBuffRankDefault
    { 4166, "unk_97F334", "bonus rang 1" },                 // kBuffRank1
    { 4167, "unk_97F3C8", "bonus rang 2" },                 // kBuffRank2
    { 4168, "unk_97F45C", "bonus rang 3" },                 // kBuffRank3
    { 4192, "unk_98023C", "gemme arme palier 1" },          // kBuffGem1
    { 4193, "unk_9802D0", "gemme arme palier 2" },          // kBuffGem2
    { 4194, "unk_980364", "gemme arme palier 3" },          // kBuffGem3
    { 4195, "unk_9803F8", "gemme arme palier 4" },          // kBuffGem4
    { 4231, "unk_9818C8", "drapeau simple A (dword_16756E0)" }, // kBuffFlagA
    { 4232, "unk_98195C", "drapeau simple B (dword_16756E4)" }, // kBuffFlagB
    { 4452, "unk_98988C", "drapeau simple C (dword_16758C4)" }, // kBuffFlagC
    {  871, "unk_908248", "harmonie elementaire" },         // kBuffHarmony
    {  872, "unk_9082DC", "mesentente elementaire" },       // kBuffMismatch
    { 3750, "unk_9702B4", "bonus divers 1 (dword_16862BC)" }, // kBuffMisc1
    {  874, "unk_908404", "bonus divers 2 (flt_1686070)" }, // kBuffMisc2
    {  875, "unk_908498", "bonus divers 3 (flt_1686080)" }, // kBuffMisc3
    {  876, "unk_90852C", "bonus divers 4 (flt_1686090)" }, // kBuffMisc4
    {  877, "unk_9085C0", "bonus divers 5 (dword_16860A0)" }, // kBuffMisc5
    { 3652, "unk_96CA0C", "bonus temporel serveur (==360)" }, // kBuffServerBonusMax
    { 3602, "unk_96AD24", "drapeau additionnel 1 (dword_1675704)" }, // kBuffFlagAdd1
    { 3225, "unk_95D330", "drapeau additionnel 2 (dword_16747BC)" }, // kBuffFlagAdd2
    { 3226, "unk_95D3C4", "drapeau additionnel 2 cumul (>6)" },      // kBuffFlagAdd3
    { 3840, "unk_9736BC", "drapeau additionnel 3 (dword_16757A4)" }, // kBuffFlagAdd4
    { 3929, "unk_976A30", "drapeau additionnel 4 (dword_1675878)" }, // kBuffFlagAdd5

    // --- Additions from mission "CABLAGE GRILLE DE BUFFS" [buff grid wiring] (2026-07-14):
    // DYNAMIC icons, fileNo = level/minute + constant -- same shared Sprite2D table as
    // above, formula verified by the same method (exact division by 148). See
    // CollectWiredConditionBuffs below for reading the corresponding globals.
    // Elemental mastery (§9.13): frame = unk_8E8B50 + 148*g_ElementMastery + 521996,
    // i.e. fileNo = g_ElementMastery + 3528 (521996/148 = 3527 EXACT). Range 1..7
    // confirmed by IDA (data_refs 0x1675680: original IDB comment "mastered element
    // 1..7 -> +1000 corresponding stat", NOT an estimate from the §1 /3000 bar).
    { 3529, "unk_8E8B50+521996(niv1)", "maitrise elementaire niveau 1" }, // kBuffElemMastery1
    { 3530, "unk_8E8B50+521996(niv2)", "maitrise elementaire niveau 2" }, // kBuffElemMastery2
    { 3531, "unk_8E8B50+521996(niv3)", "maitrise elementaire niveau 3" }, // kBuffElemMastery3
    { 3532, "unk_8E8B50+521996(niv4)", "maitrise elementaire niveau 4" }, // kBuffElemMastery4
    { 3533, "unk_8E8B50+521996(niv5)", "maitrise elementaire niveau 5" }, // kBuffElemMastery5
    { 3534, "unk_8E8B50+521996(niv6)", "maitrise elementaire niveau 6" }, // kBuffElemMastery6
    { 3535, "unk_8E8B50+521996(niv7)", "maitrise elementaire niveau 7" }, // kBuffElemMastery7
    // Server time bonus per whole minute (§9.11, case >=120 && !=360):
    // frame = unk_8E8B50 + 148*(dword_1674AB0/60) + 483516, i.e.
    // fileNo = (dword_1674AB0/60) + 3268 (483516/148 = 3267 EXACT). 360s (max tier,
    // 6 min) remains covered by kBuffServerBonusMax above (a distinct FIXED icon in
    // the binary, NOT this per-minute indexed table).
    { 3270, "unk_8E8B50+483516(2m)", "bonus temporel serveur 2 min" }, // kBuffServerBonusMin2
    { 3271, "unk_8E8B50+483516(3m)", "bonus temporel serveur 3 min" }, // kBuffServerBonusMin3
    { 3272, "unk_8E8B50+483516(4m)", "bonus temporel serveur 4 min" }, // kBuffServerBonusMin4
    { 3273, "unk_8E8B50+483516(5m)", "bonus temporel serveur 5 min" }, // kBuffServerBonusMin5
};
static_assert(sizeof(kKnownIcons) / sizeof(kKnownIcons[0]) == kBuffKnownIconCount,
              "kKnownIcons must cover BuffIconId exactly");

// Icons of the §16 bottom-right panel (off/on state of the 4 internal icons). Same
// resolution method as above.
struct StatusIconDef { int fileOff, fileOn; };
constexpr StatusIconDef kStatusIcons[4] = {
    { 2004, 2003 }, // this+176 : unk_93114C (off) / unk_9310B8 (on)
    { 2074, 2073 }, // this+180 : unk_9339C4 (off) / unk_933930 (on)
    { 2077, 2076 }, // this+184 : unk_933B80 (off) / unk_933AEC (on)
    { 2080, 2079 }, // this+188 : unk_933D3C (off) / unk_933CA8 (on)
};

// Fallback palette ("colored pills") for grid icons outside the known table
// (id < 0 or >= kBuffKnownIconCount — notably the 36 §9.10 timed debuffs).
// Pastel/saturated hues chosen to stay legible at 24x24 on a dark background,
// in the spirit of the "pill" requested by the mission (same principle as the
// colored `ctx.FillRect` fallback of WarehouseWindow::Render for its cells).
constexpr D3DCOLOR kPillPalette[] = {
    0xFFE05656u, 0xFF56A0E0u, 0xFF56D080u, 0xFFE0C256u, 0xFFB066E0u,
    0xFFE0863Cu, 0xFF3CC8C8u, 0xFFE056A8u, 0xFF9AD046u, 0xFF6E7CE0u,
};
constexpr size_t kPillPaletteCount = sizeof(kPillPalette) / sizeof(kPillPalette[0]);

D3DCOLOR PillColorForId(int buffId) {
    // Simple deterministic derivation (no real per-id semantics, just a
    // stable hue to visually distinguish buffs from each other).
    const unsigned h = static_cast<unsigned>(buffId) * 2654435761u; // Knuth multiplicative hash
    return kPillPalette[h % kPillPaletteCount];
}

// Creates an opaque white 1x1 texture (D3DPOOL_MANAGED: survives device reset).
// Deliberate duplication of the same helper as GameHud.cpp — pattern already
// established in this project (no convenient common header for such a small
// helper, cf. the equivalent comment in InventoryWindow.cpp about ResolveItemIconPath).
IDirect3DTexture9* CreateWhiteTexture(IDirect3DDevice9* dev) {
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)) || !tex) {
        return nullptr;
    }
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits) {
        *static_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
        tex->UnlockRect(0);
    }
    return tex;
}

} // namespace

// Init / Shutdown
bool BuffStatusPanel::Init(gfx::Renderer& renderer, gfx::Font* font) {
    Shutdown();
    device_ = renderer.Device();
    font_   = font;
    if (!device_) return false;
    if (!sprite_.Create(device_)) { device_ = nullptr; return false; }

    white_ = CreateWhiteTexture(device_);
    if (!white_) { Shutdown(); return false; }

    // §16 frame (best-effort; falls back to a colored rect in RenderStatusPanel if missing).
    {
        asset::ImgFile img;
        const std::string path = GxdCategory1Path(kStatusFrameFile);
        if (img.Load(path) && img.Kind() == asset::ImgKind::TextureDxt)
            statusFrameTex_.CreateFromImgFile(device_, img);
    }

    SetScreenSize(ts2::kRefWidth, ts2::kRefHeight);
    return true;
}

void BuffStatusPanel::Shutdown() {
    gridIconCache_.clear();
    panelIconCache_.clear();
    statusFrameTex_.Release();
    if (white_) { white_->Release(); white_ = nullptr; }
    sprite_.Destroy();
    device_ = nullptr;
    font_   = nullptr;
}

void BuffStatusPanel::SetScreenSize(int width, int height) {
    screenW_ = (width  > 0) ? width  : ts2::kRefWidth;
    screenH_ = (height > 0) ? height : ts2::kRefHeight;
}

// Primitives
void BuffStatusPanel::DrawFilledRect(int x, int y, int w, int h, D3DCOLOR color) {
    if (!white_ || w <= 0 || h <= 0) return;
    RECT src{ 0, 0, 1, 1 };
    sprite_.DrawSpriteScaled(white_, &src, x, y, static_cast<float>(w), static_cast<float>(h),
                             color, /*compensatePos=*/true);
}

void BuffStatusPanel::DrawBorder(int x, int y, int w, int h, int t, D3DCOLOR color) {
    if (t <= 0) return;
    DrawFilledRect(x,         y,         w, t, color);
    DrawFilledRect(x,         y + h - t, w, t, color);
    DrawFilledRect(x,         y,         t, h, color);
    DrawFilledRect(x + w - t, y,         t, h, color);
}

// Icons
gfx::GpuTexture* BuffStatusPanel::GetGridIconTex(int buffId) {
    if (buffId < 0 || buffId >= kBuffKnownIconCount) return nullptr; // outside table -> pill

    auto it = gridIconCache_.find(buffId);
    if (it != gridIconCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    if (device_) {
        const std::string path = GxdCategory1Path(kKnownIcons[buffId].fileNo);
        asset::ImgFile img;
        if (!path.empty() && img.Load(path))
            tex.CreateFromImgFile(device_, img);
    }
    auto res = gridIconCache_.emplace(buffId, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

gfx::GpuTexture* BuffStatusPanel::GetPanelIconTex(int fileNo) {
    if (fileNo <= 0) return nullptr;

    auto it = panelIconCache_.find(fileNo);
    if (it != panelIconCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    if (device_) {
        const std::string path = GxdCategory1Path(fileNo);
        asset::ImgFile img;
        if (img.Load(path))
            tex.CreateFromImgFile(device_, img);
    }
    auto res = panelIconCache_.emplace(fileNo, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

// Icon for a §9.10 bank slot (G07). The binary blits the Sprite2D entry
// `unk_A60D04 + 0x143C * g_LocalElementSecondary + 0x94 * i`
// (EA 0x67D613-0x67D62B, duplicated at 0x67D6EE-0x67D706 and 0x67D74C-0x67D764):
// entry stride 0x94 = 148 bytes, per-element block stride 0x143C = 5180 = 35*148.
// unk_A60D04 is the base of CATEGORY 6 of AssetMgr_InitAllSlots 0x4DEB50
// (@0x4DECEA: `Sprite2D_BuildPath(this + 5180*ii + 148*jj + 1540564, 6, ii, jj)`,
// this = 0x8E8B30 -> 0x8E8B30 + 1540564 = 0xA60D04, EXACT division/sum), hence
// template `Sprite2D_BuildPath 0x4D68E0` case 6 (@0x4D6A0C):
//   "G03_GDATA\D01_GIMAGE2D\007\007_%03d%03d.IMG", (a3 + 1, a4 + 1) = (element+1, index+1).
// WARNING: BINARY QUIRK reproduced as-is: the cat.6 table only has 35 entries per
// element (`for (jj = 0; jj < 35; ++jj)`) while the render loop goes up to
// index 35 (`cmp var_438, 24h` = 36 slots) -> in the binary, index 35
// overruns into the next element's block. Here file 036 doesn't exist for that
// element: the load fails -> pill fallback. Benign, accepted divergence
// (we don't display another element's icon), NOT a fix of the binary.
gfx::GpuTexture* BuffStatusPanel::GetBankIconTex(int element, int bankIndex) {
    if (element < 0 || bankIndex < 0) return nullptr;
    const int key = element * 100 + bankIndex; // bankIndex <= 35 -> no collision

    auto it = bankIconCache_.find(key);
    if (it != bankIconCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    if (device_) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\007\\007_%03d%03d.IMG",
                      element + 1, bankIndex + 1);
        asset::ImgFile img;
        if (img.Load(std::string(buf)))
            tex.CreateFromImgFile(device_, img);
    }
    auto res = bankIconCache_.emplace(key, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

void BuffStatusPanel::DrawGridIcon(int buffId, int x, int y, int size) {
    gfx::GpuTexture* tex = GetGridIconTex(buffId);
    if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0) {
        const float sx = static_cast<float>(size) / static_cast<float>(tex->Width());
        const float sy = static_cast<float>(size) / static_cast<float>(tex->Height());
        sprite_.DrawSpriteScaled(tex->Handle(), nullptr, x, y, sx, sy, gfx::kSpriteWhite, true);
        return;
    }
    // "Colored pill" fallback (.IMG icon not resolved for this id).
    DrawFilledRect(x, y, size, size, PillColorForId(buffId));
    DrawBorder(x, y, size, size, 1, 0xFF000000u);
}

void BuffStatusPanel::DrawEntryIcon(const GridEntry& e, int x, int y, int size) {
    if (e.bankIndex < 0) {          // §9 "catalog" condition -> cat.1 table
        DrawGridIcon(e.catalogId, x, y, size);
        return;
    }

    // §9.10 bank entry -> cat.6 table, indexed by (local element, index).
    // TODO [g_LocalElementSecondary 0x1673198]: the ELEMENT axis is inert on the C++
    // side -- `game::g_World.self.elementSecondary` (Game/GameState.h:408, the model
    // this codebase settled on, cf. Net/GameHandlers_VendorTrade.cpp:333) has NO
    // writer (exhaustive grep: only comparisons), and the competing escape hatch
    // `g_Client.Var(0x1673198)` isn't written anywhere either (only READ by
    // Net/GameHandlers_Misc.cpp:304). The element is therefore permanently 0 -> we
    // always load the element-1 variant ("007_001NNN.IMG"). This isn't a FALSE
    // signal (0 is a legitimate element value) but the variant may be wrong for
    // a player of a different element. To fix, wire up a writer for 0x1673198
    // (outside this front-end).
    const int element = game::g_World.self.elementSecondary;
    gfx::GpuTexture* tex = GetBankIconTex(element, e.bankIndex);
    if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0) {
        const float sx = static_cast<float>(size) / static_cast<float>(tex->Width());
        const float sy = static_cast<float>(size) / static_cast<float>(tex->Height());
        sprite_.DrawSpriteScaled(tex->Handle(), nullptr, x, y, sx, sy, gfx::kSpriteWhite, true);
        return;
    }
    // Pill fallback: id derived from the bank index, shifted out of the catalog id
    // space so PillColorForId produces a distinct, stable hue.
    DrawFilledRect(x, y, size, size, PillColorForId(kBuffKnownIconCount + e.bankIndex));
    DrawBorder(x, y, size, size, 1, 0xFF000000u);
}

// Data sources ACTUALLY modeled for the §9 grid (mission "CABLAGE
// GRILLE DE BUFFS" [buff grid wiring], 2026-07-14)
// The doc (Docs/TS2_UI_GAMEHUD_RENDER.md §9) catalogs ~50 distinct trigger
// conditions (points 1-14). Each reads one or more `dword_XXXXXXX` globals. This
// pass verified, BY ADDRESS, which of these globals has a real writer on the
// ClientSource side (i.e. a value that can actually become non-zero in-game):
//
//   METHOD: grep each address cited by the doc across ClientSource/, then for
//   ambiguous cases (address reused by several systems), cross-check directly
//   against the IDB via decompile()/data_refs() (idaTs2 MCP, http://127.0.0.1:13337).
//
//   WIRED below (confirmed source, identified writer, NO semantic conflict
//   with another already-modeled system):
//     §9.5  dword_167564C  -- Net/GameVarDispatch.cpp case 128 (direct write,
//           no other writer). Original condition: `%10000 > 0`, icon per
//           `/10000` (0=default, 1/2/3=tiers).
//     §9.7  dword_16756E0, dword_16756E4 -- GameVarDispatch.cpp cases 76/77. Simple
//           `> 0` flag.
//     §9.11 dword_1674AB0 -- GameVarDispatch.cpp case 55 (`value != -1` branch).
//           `==360` -> kBuffServerBonusMax; `>=120` (and !=360) -> per-minute tier.
//     §9.13 g_ElementMastery (0x1675680) -- GameVarDispatch.cpp case 68 (writes the
//           1..7 value as-is) + reset in case 68/WE_ResetElementMastery. Confirmed
//           by IDA (data_refs): original IDB comment "mastered element 1..7".
//     §9.14 dword_1675704 -- GameVarDispatch.cpp case 81. dword_16747BC -- written by
//           CharStatDeltaDispatch.cpp (`SV(0x16747BC)`, alias of g_Client.Var) AND
//           documented as `rebirthTier` in Game/SkillCombat.h. dword_1675878 --
//           GameVarDispatch.cpp case 109.
//     §9.3  EQUALITY-ONLY branch of "g_LocalElement == dword_1685E08 or paired via
//           Char_GetPairedElement" -- BOTH operands are modeled:
//           `game::g_World.self.element` (g_LocalElement, SelfState) and dword_1685E08
//           (`g_Client.VarGet`, written "unconditionally" by sub-case 38 of
//           Net/WorldEntityDispatch.cpp as "received class"). PRECEDENT IN THIS SAME
//           CODEBASE: Net/GameVarDispatch.cpp case 106 ALREADY does exactly this
//           comparison (`g_World.self.element != g_Client.VarGet(0x1685E08)`) --
//           independent confirmation that this read is an established pattern, not an
//           invention of this mission. The `Char_GetPairedElement` branch (function
//           NOT ported) is NOT covered -> this wiring is a STRICTLY MORE CONSERVATIVE
//           subset than the original (may miss a rare true positive, can never
//           fabricate a false one). WARNING: dword_1685E08 is ALSO the target
//           of a large 1324-byte memcpy (ZoneChangeInfo::block1, Net/GameHandlers_BossWorld.cpp)
//           in the original binary -- NO CONSEQUENCE here: this file writes via
//           `g_Client.Blob(0x1685E08, ...)`, an `unordered_map` table SEPARATE from
//           `g_Client.Var(0x1685E08)`'s (cf. Game/ClientRuntime.h) -- no collision
//           possible on the ClientSource side, unlike the binary where it was the same
//           physical memory.
//
//   DISCARDED (address WITHOUT a confirmed writer, or in proven semantic conflict with
//   another system ALREADY modeled under a different, better-established interpretation --
//   wiring it anyway would have fabricated a misleading signal, which this project forbids):
//     §9.1  dword_184C218 (elemental combos) -- verified via IDA data_refs(): this
//           SAME array is already modeled TWICE elsewhere under different, better-
//           established semantics (Game/SocialSystem.h::AchievementState, 24 exploit
//           flags written by Net_OnAchievementDataLoad/Notice; Net/
//           WorldEntityDispatch.cpp::kRankTable1, [0..11] title/rank table written
//           by sub-dispatch 752/753) -- the HUD doc's "combo/100, combo%100" reading
//           is only an unconfirmed hypothesis (the doc says so itself). Wiring a 3rd,
//           competing interpretation on the same address would have shown a signal
//           inconsistent with the two models already written.
//     §9.2  dword_16860B0[elem] (local elemental state) -- WorldEntityDispatch.cpp
//           (sub-case 45) only RESETS it to 0; no sub-case writes it a non-zero
//           value in what has been reversed so far -> the condition can never
//           trigger with the currently modeled data.
//     §9.4, §9.6, §9.8, §9.9 -- depend on unported functions
//           (Char_CompareSkillLoadout, Item_ClassifyById + weapon sockets,
//           Char_GetElementHarmonyBonus/MismatchPenalty) or on globals with no writer
//           found (dword_16862BC, flt_1686070/80/90, dword_16860A0) -- none has a
//           data source on the ClientSource side. (§9.3 has a branch partially
//           wired above -- only the Char_GetPairedElement-dependent part,
//           NOT ported, remains out of scope.)
//     §9.10 bank of 36 timed debuffs (dword_16758D8) -- the address IS modeled,
//           but under a DIFFERENT form (SelfState::zoneState, raw 288-byte blob from
//           Pkt_EnterWorld, never decoded field by field) -- not a usable vector of
//           36 {id,duration} entries as-is without decoding this blob, out of scope here.
//     §9.12 morph food buff (dword_168744C==1 && dword_1687450<=4) --
//           data_refs()/re-reading of Net/WorldEntityDispatch.cpp shows that
//           dword_1687450 (`kDuelOpponentVal`) is actually written by the
//           DUEL/CHALLENGE subsystem (sub-cases 102/107/112/114, "opponent code/result")
//           -- a semantics confirmed by line-by-line RE, far more reliable than the
//           HUD doc's "morph food buff" label (an unconfirmed hypothesis, made
//           without access to the string tables). Wiring the doc's `<=4` read would
//           have shown a "food buff" icon during a duel -- a FALSE signal.
//     dword_16758C4 (kBuffFlagC), dword_16757A4 (kBuffFlagAdd4) -- grepped across all of
//           ClientSource/, NO writer found (only cited in this file): would stay
//           at 0 permanently, nothing to wire.
void BuffStatusPanel::CollectWiredConditionBuffs(std::vector<game::ActiveBuff>& out) const {
    using game::g_Client;

    // §9.3 elemental pair synergy -- equality-only branch (cf. banner above:
    // Char_GetPairedElement not ported, conservative subset). Comes with a
    // fixed "10%" text (NOT a read value -- a binary literal, cf. doc §9.3),
    // handled as a special case in RenderGrid (ActiveBuff has no caption field).
    // `elemLastClass != 0` GUARD REQUIRED: `game::g_Client.VarGet` AND
    // `SelfState::element` both default to 0 (before any packet is received) --
    // without this guard, a freshly started client would FALSELY show the icon
    // (0==0) before receiving a single elemental class packet. This guard
    // introduces a false negative for the legitimate class 0 (indistinguishable
    // from "never received" with the currently modeled data) but NEVER a false
    // positive -- consistent with this file's "conservative subset" policy.
    {
        const int32_t elemLastClass = g_Client.VarGet(0x1685E08);
        if (elemLastClass != 0 && game::g_World.self.element == elemLastClass)
            out.push_back({ kBuffElemPair, 0.0f });
    }

    // §9.5 (EA ~0x67C6xx, doc §9 point 5): `dword_167564C % 10000 > 0` -> icon per
    // the `/10000` quotient (0=default, 1/2/3=tiers, >=4 clamped to tier 3, no
    // 4th icon in the disassembly).
    {
        const int32_t v = g_Client.VarGet(0x167564C);
        if (v % 10000 > 0) {
            const int tier = v / 10000;
            int id = kBuffRankDefault;
            if (tier == 1) id = kBuffRank1;
            else if (tier == 2) id = kBuffRank2;
            else if (tier >= 3) id = kBuffRank3;
            out.push_back({ id, 0.0f });
        }
    }

    // §9.7 simple flags (icon only if `> 0`, NO kBuffFlagC: dword_16758C4
    // has no writer, cf. banner above).
    if (g_Client.VarGet(0x16756E0) > 0) out.push_back({ kBuffFlagA, 0.0f });
    if (g_Client.VarGet(0x16756E4) > 0) out.push_back({ kBuffFlagB, 0.0f });

    // §9.11 server time bonus: `==360` -> fixed max icon; `>=120` (and !=360) ->
    // icon per whole minute tier (`dword_1674AB0/60`, tiers 2..5 -- 6 min
    // equals 360 so already covered by the ==360 case above).
    {
        const int32_t v = g_Client.VarGet(0x1674AB0);
        if (v == 360) {
            out.push_back({ kBuffServerBonusMax, 0.0f });
        } else if (v >= 120) {
            const int minutes = std::clamp(v / 60, 2, 5);
            out.push_back({ kBuffServerBonusMin2 + (minutes - 2), 0.0f });
        }
    }

    // §9.13 elemental mastery: `g_ElementMastery > 0` -> tier icon (real range
    // 1..7, confirmed by IDA -- cf. banner above). Any out-of-range value
    // (shouldn't happen, but defensively handled) is clamped rather than ignored.
    {
        const int32_t mastery = g_Client.VarGet(0x1675680); // g_ElementMastery
        if (mastery > 0) {
            const int lvl = std::clamp(mastery, 1, 7);
            out.push_back({ kBuffElemMastery1 + (lvl - 1), 0.0f });
        }
    }

    // §9.14 additional flags: dword_1675704==1; dword_16747BC in (0,13), with a
    // STACKED icon (an additional 2nd icon, not a replacement -- cf. doc: "2 stacked
    // icons") if it's also >6; dword_1675878>0. NO kBuffFlagAdd4
    // (dword_16757A4 has no writer, cf. banner above).
    if (g_Client.VarGet(0x1675704) == 1) out.push_back({ kBuffFlagAdd1, 0.0f });
    {
        // Same original address as Game/SkillCombat.h::CombatMorphState::rebirthTier
        // (separate local struct, NOT synced with g_Client.Var) -- here we read the
        // Var directly, the only instance actually written by the network handler
        // (Net/CharStatDeltaDispatch.cpp::SV, alias of g_Client.Var).
        const int32_t rebirth = g_Client.VarGet(0x16747BC);
        if (rebirth > 0 && rebirth < 13) {
            out.push_back({ kBuffFlagAdd2, 0.0f });
            if (rebirth > 6) out.push_back({ kBuffFlagAdd3, 0.0f });
        }
    }
    if (g_Client.VarGet(0x1675878) > 0) out.push_back({ kBuffFlagAdd5, 0.0f });
}

// §9.10 — Bank of 36 timed debuffs (EA 0x67D560-0x67D77F)
// PROVEN LAYOUT (disasm, not pseudocode): array of 36 8-byte entries at
// 0x16758D8. The stride of 8 is established by the SIB `dword_16758D8[ecx*8]` (@0x67D58B) —
// an x86 scale ∈ {1,2,4,8}: {id:u32 @+0, duration:u32 @+4}. 36 * 8 = 288 = the exact
// size of `zoneStateBlock[288]` (Net/RecvPackets.h:16), copied into dword_16758D8
// by Pkt_EnterWorld (@0x4641CE `push offset dword_16758D8`). A SEPARATE array of 36
// start timestamps follows, CONTIGUOUS: flt_16759F8 = 0x16758D8 + 288.
//
// DATA SOURCES ALREADY WIRED on the C++ side (nothing to write, only to consume):
//   - Game/EntityManager.cpp:300  (OnEnterWorld): `self.zoneState.assign(p.zoneStateBlock, ...)`
//                                                  <-> memcpy 0x120 bytes @0x4641CE
//   - Game/EntityManager.cpp:614-616 (OnCharStateUpdate, SELF branch, flag==1):
//       WrZoneU32(8*i, val)                      <-> dword_16758D8[2*i] @0x46530C
//       WrZoneU32(8*i+4, extra)                  <-> dword_16758DC[2*i] @0x465326
//       g_Client.VarF(0x16759F8+4*i) = gameTimeSec <-> flt_16759F8[i]   @0x465339
// Packet 0x10 (Pkt_CharStateUpdate 0x464C10) keeps the bank alive: it ALREADY
// carries the duration, which C++ captures correctly — only the CONSUMER
// was missing (G03).
//
// GUARDS (in the binary's exact order):
//   1. `cmp var_438, 24h / jge` @0x67D578          -> 36 slots
//   2. `cmp dword_16758D8[ecx*8], 1 / jge` @0x67D58B -> present iff (int32)id >= 1
//      (jge = SIGNED comparison)
//   3. `cmp var_438,13h / jl` + `cmp var_438,1Ch / jg` @0x67D597-0x67D5A9 ->
//      indices 19..28 (0x13..0x1C) INCLUSIVE are skipped: neither drawn nor counted.
// Remainder: `fild dword_16758DC[edx*8]` (`fild` = SIGNED integer load) then
// `fld g_GameTimeSec / fsub flt_16759F8[eax*4] / fsubp` @0x67D5B1-0x67D5CD, i.e.
//   remaining = (float)(int32)duration - (g_GameTimeSec - startTimestamp)
//
// NO PURGE (G11): the binary's grid has NO expiry condition — an entry with a
// negative `remaining` keeps being drawn (<= 10 branch -> perpetual blinking)
// as long as `id >= 1`. The ONLY reset is packet 0x10's flag==2
// (@0x465D48-0x465D5F, which zeroes id and duration WITHOUT touching the timestamp).
// The local purge that used to exist here (`buffs.erase(remove_if(... now >= expiryTime))`)
// was therefore REMOVED: it permanently destroyed an entry the server had not
// removed.
void BuffStatusPanel::CollectZoneStateBuffs(std::vector<GridEntry>& out) const {
    const std::vector<uint8_t>& zs = game::g_World.self.zoneState;
    // The binary always addresses the 288-byte BSS; on the C++ side the vector is
    // empty until an EnterWorld/CharStateUpdate has occurred.
    if (zs.size() < kZoneStateBytes) return;

    const float now = game::g_World.gameTimeSec;

    for (int i = 0; i < kZoneBankCount; ++i) {              // guard 1 @0x67D578
        int32_t id = 0, dur = 0;
        std::memcpy(&id,  zs.data() + 8 * i,     4);        // dword_16758D8[i*8]
        std::memcpy(&dur, zs.data() + 8 * i + 4, 4);        // dword_16758DC[i*8]

        if (id < 1) continue;                               // guard 2 @0x67D58B (jge, signed)
        if (i >= kZoneSkipLo && i <= kZoneSkipHi) continue; // guard 3 @0x67D597-0x67D5A9

        // Start timestamp: flt_16759F8[i]. `VarF` isn't const -> we re-read the
        // SAME 4 bytes via VarGet (the slot is an int32 reinterpreted as float, cf.
        // Game/ClientRuntime.h::VarF) and reinterpret them here.
        const int32_t bits = game::g_Client.VarGet(0x16759F8u + 4u * static_cast<uint32_t>(i));
        float start = 0.0f;
        std::memcpy(&start, &bits, 4);

        GridEntry e;
        e.bankIndex = i;                                    // var_438 (BANK index)
        e.remaining = static_cast<float>(dur) - (now - start); // @0x67D5B1-0x67D5CD
        out.push_back(e);
        // Grid POSITION (var_424) is NOT stored here: it's the index into
        // `out`, incremented simply by having been pushed -> see BuildGridEntries.
    }
}

std::vector<BuffStatusPanel::GridEntry> BuffStatusPanel::BuildGridEntries() const {
    std::vector<GridEntry> entries;
    const float now = game::g_World.gameTimeSec;

    // 1. `self.buffs` — generic model (Game/GameState.h) kept as an anchor point
    //    for future sources. WARNING: it still has NO writer in
    //    src/: this is INTENTIONAL here (cf. G03) — the §9.10 bank below does NOT go
    //    through it, it reads `self.zoneState` directly, which is the data actually
    //    fed by the network.
    // NB: `buffs` is carried by PlayerEntity (Game/GameState.h:200) -> accessed via
    // Self(), whereas `zoneState`/`elementSecondary` are carried by SelfState
    // (GameState.h:474/408) -> accessed via `.self`. Two different structures.
    for (const game::ActiveBuff& b : game::g_World.Self().buffs) {
        GridEntry e;
        e.catalogId = b.id;
        e.remaining = (b.expiryTime > 0.0f) ? (b.expiryTime - now) : -1.0f;
        entries.push_back(e);
    }

    // 2. Wired §9 conditions (§9.3/9.5/9.7/9.11/9.13/9.14) — FIXED icons, no
    //    duration or blinking (the binary only draws a `> 0` icon for these).
    {
        std::vector<game::ActiveBuff> wired;
        CollectWiredConditionBuffs(wired);
        for (const game::ActiveBuff& b : wired) {
            GridEntry e;
            e.catalogId = b.id;
            entries.push_back(e);
        }
    }

    // 3. §9.10 — bank of 36 timed debuffs.
    //    ACCEPTED (and unavoidable) ABSOLUTE POSITION MISMATCH: in the binary, §9.10
    //    sits between §9.9 and §9.11 and inherits a var_424 already advanced by the
    //    ~50 §9.1-9.9 conditions. Only 8 of these ~50 conditions have a modeled
    //    data source (cf. CollectWiredConditionBuffs banner): the cursor therefore
    //    CANNOT match the binary's, whatever order is chosen here. What IS
    //    faithfully reproduced, and what G06 is about, is the DISSOCIATION between
    //    bank index (var_438, the icon's source) and grid position (var_424), and
    //    the cursor advancement rule.
    CollectZoneStateBuffs(entries);

    return entries;
}

// §9 — Buff/debuff grid (7 columns)
// Position: `(var_4 + 28*(var_424 % 7), 5 + 28*(var_424 / 7))` — EA 0x67D5E9-0x67D612
// (`idiv 7`; quotient*0x1C+5 = y, remainder*0x1C + var_4 = x). var_4 == kGridX == 220.
//
// CURSOR RULE (G06), proven by the jump graph and NOT by pseudocode:
// `loc_67D770` (`var_424 += 1`) is reached via TWO paths —
//   - 0x67D695 `jmp loc_67D770`  (after normal drawing), AND
//   - 0x67D6B6 `jnz loc_67D770`  (blink branch that HIDES the icon).
// The cursor thus advances for EVERY entry passing the guards, drawn OR NOT: blinking
// hides the blit WITHOUT shifting the following icons. (The gaps file claimed the
// opposite — "var_424 is only incremented on a path that actually draws" — which
// would have made the whole grid jitter at 1 Hz.) This rule is treated as
// structural here: appearing in `entries` = occupying a position.
void BuffStatusPanel::RenderGrid() {
    const float now = game::g_World.gameTimeSec;

    const std::vector<GridEntry> entries = BuildGridEntries();
    const int count = std::min<int>(static_cast<int>(entries.size()), kGridMaxIcons);

    for (int j = 0; j < count; ++j) {
        const GridEntry& e = entries[static_cast<size_t>(j)];
        const int x = kGridX + (j % kGridCols) * kIconPitch;
        const int y = kGridY + (j / kGridCols) * kIconPitch;

        bool draw = true;
        if (e.bankIndex >= 0) {
            // Only the §9.10 bank blinks (block 0x67D69A is INTERNAL to its loop).
            // `Crt_ftol(remaining)` @0x67D5D3 then `cmp eax, 0Ah / jle loc_67D69A`
            // @0x67D5D8: blinking applies as soon as the TRUNCATED remaining time is
            // <= 10 — with NO lower bound (a negative remaining blinks forever,
            // cf. G05/G11). Crt_ftol truncates toward zero == static_cast<int>.
            if (static_cast<int>(e.remaining) <= 10) {
                // @0x67D69A-0x67D6B6: `fld g_GameTimeSec / fadd st,st / Crt_ftol /
                // and eax,80000001h / (negative modulo fixup) / cmp eax,1 / jnz loc_67D770`
                // -> the binary DRAWS iff ftol(2*t) % 2 == 1 (ODD half-period).
                // The old C++ code did `skipDraw = ... % 2 == 1`: polarity
                // EXACTLY INVERTED (icon visible when the original hides it). G05.
                const int half = static_cast<int>(now * 2.0f);
                draw = (half % 2 == 1);
            }
        }

        if (draw)
            DrawEntryIcon(e, x, y, kIconSize);

        // NO TEXT (G12): the exhaustive disassembly of loop 0x67D560-0x67D856
        // contains ONLY Sprite2D_HitTest (0x67D632, 0x67D70D) and Sprite2D_Draw
        // (0x67D690, 0x67D76B, 0x67D7F1, 0x67D842) — no font call, no
        // UI_DrawNumberValue. The numeric countdown that used to be drawn here was
        // fabricated: the remaining duration is ONLY used by the blink guard above.
    }
}

// §16 — Bottom-right status panel (4 icons + cast indicator)
void BuffStatusPanel::RenderStatusPanel() {
    const bool hasFrame = statusFrameTex_.Valid();
    const int frameW = hasFrame ? static_cast<int>(statusFrameTex_.Width())  : kStatusFallbackW;
    const int frameH = hasFrame ? static_cast<int>(statusFrameTex_.Height()) : kStatusFallbackH;

    const int fx = screenW_ - frameW;
    const int fy = screenH_ - frameH;

    if (hasFrame) {
        sprite_.DrawSprite(statusFrameTex_.Handle(), nullptr, fx, fy);
    } else {
        DrawFilledRect(fx, fy, frameW, frameH, 0xC0181820u);
        DrawBorder(fx, fy, frameW, frameH, 1, 0xFF3A3A48u);
    }

    for (int i = 0; i < 4; ++i) {
        const int ix = fx + kStatusOffsets[i].dx;
        const int iy = fy + kStatusOffsets[i].dy;
        const int fileNo = statusFlags_[i] ? kStatusIcons[i].fileOn : kStatusIcons[i].fileOff;
        gfx::GpuTexture* tex = GetPanelIconTex(fileNo);
        if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0) {
            const float sx = static_cast<float>(kStatusIconSize) / static_cast<float>(tex->Width());
            const float sy = static_cast<float>(kStatusIconSize) / static_cast<float>(tex->Height());
            sprite_.DrawSpriteScaled(tex->Handle(), nullptr, ix, iy, sx, sy, gfx::kSpriteWhite, true);
        } else {
            DrawFilledRect(ix, iy, kStatusIconSize, kStatusIconSize,
                           statusFlags_[i] ? 0xFF4C8C4Cu : 0xFF3A3A46u);
            DrawBorder(ix, iy, kStatusIconSize, kStatusIconSize, 1, 0xFF000000u);
        }
    }

    // Cast indicator (§16): overlaid on the 4th position. Icon sequence not
    // identified in the disassembly (only the trigger formula and frame
    // cycle are known) -> pulsing pill + frame number as text
    // (PERMANENT fallback, cf. the header's top banner).
    if (casting_) {
        const int ix = fx + kStatusOffsets[3].dx;
        const int iy = fy + kStatusOffsets[3].dy;
        const int frame = static_cast<int>(game::g_World.gameTimeSec * 16.0f) % 8; // Crt_ftol(t*16)%8
        const uint8_t alpha = static_cast<uint8_t>(128 + 127 * frame / 7); // pulse 0..7 -> 128..255
        const D3DCOLOR castColor = (static_cast<D3DCOLOR>(alpha) << 24) | 0x00FFD060u;
        DrawFilledRect(ix, iy, kStatusIconSize, kStatusIconSize, castColor);
        DrawBorder(ix, iy, kStatusIconSize, kStatusIconSize, 1, 0xFFFFFFFFu);
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%d", frame);
        pendingText_.push_back({ ix + kStatusIconSize / 2 - 3, iy + kStatusIconSize / 2 - 6,
                                  buf, 0xFF000000u });
    }
}

// Render
void BuffStatusPanel::Render() {
    if (!device_ || !sprite_.Ready()) return;

    pendingText_.clear();
    if (SUCCEEDED(sprite_.Begin(D3DXSPRITE_ALPHABLEND))) {
        RenderGrid();
        RenderStatusPanel();
        sprite_.End();
    }

    if (font_ && font_->Ready() && !pendingText_.empty()) {
        font_->BeginBatch();
        for (const TextItem& t : pendingText_)
            font_->DrawTextStyled(t.text.c_str(), t.x, t.y, t.color, gfx::kStyleShadow);
        font_->EndBatch();
    }
}

// Hit-test
bool BuffStatusPanel::OnMouseDown(int x, int y) {
    // §9 grid: rectangular zone encompassing the actually populated rows
    // (doc: "all grid icons are clickable" -> opens a generic tooltip, not
    // modeled here; only event consumption is reproduced).
    // Same source as RenderGrid (BuildGridEntries): the number of clickable
    // icons stays aligned with the number of displayed icons, §9.10 bank included.
    const int count = std::min<int>(static_cast<int>(BuildGridEntries().size()), kGridMaxIcons);
    if (count > 0) {
        const int rows = (count + kGridCols - 1) / kGridCols;
        const int gridW = kGridCols * kIconPitch;
        const int gridH = rows * kIconPitch;
        if (x >= kGridX && x < kGridX + gridW && y >= kGridY && y < kGridY + gridH)
            return true;
    }

    // §16 bottom-right panel.
    const bool hasFrame = statusFrameTex_.Valid();
    const int frameW = hasFrame ? static_cast<int>(statusFrameTex_.Width())  : kStatusFallbackW;
    const int frameH = hasFrame ? static_cast<int>(statusFrameTex_.Height()) : kStatusFallbackH;
    const int fx = screenW_ - frameW;
    const int fy = screenH_ - frameH;
    if (x >= fx && x < fx + frameW && y >= fy && y < fy + frameH)
        return true;

    return false;
}

// §16 hooks
void BuffStatusPanel::SetStatusFlag(int index, bool active) {
    if (index < 0 || index >= 4) return;
    statusFlags_[static_cast<size_t>(index)] = active;
}

// Device lost/reset
void BuffStatusPanel::OnDeviceLost() {
    sprite_.OnLostDevice();
    // white_/statusFrameTex_ are D3DPOOL_MANAGED: nothing to do.
}

void BuffStatusPanel::OnDeviceReset() {
    sprite_.OnResetDevice();
}

} // namespace ts2::ui

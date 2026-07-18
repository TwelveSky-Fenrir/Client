// UI/EnchantWindow.cpp — implementation of the "Enchant" window.
// See UI/EnchantWindow.h for the contract, the class/slot mapping hypothesis, and
// the RE references (Game/ItemSystem.h, Game/GameState.h).
#include "UI/EnchantWindow.h"
#include "Game/GameDatabase.h"
#include "Asset/ImgFile.h"

#include <cstdio>
#include <cstddef>

namespace ts2::ui {
namespace {

// Item icon resolver — IDENTICAL to ResolveItemIconPath in UI/InventoryWindow.cpp
// (the mission's reference pattern, duplicated for lack of a shared header, without
// touching the existing architecture). CONFIRMED by disassembly (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md):
// the file index is NOT itemId (old hypothesis, FALSE) but the SEPARATE field
// ITEM_INFO+192 ("IconID", game::ItemInfo::iconId, 1-based), read via game::GetItemInfo().
// (scaled via DrawSpriteScaled like the other windows, kSlotSize=48 here.)
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}

// Palette (ARGB, D3DCOLOR = 0xAARRGGBB) — same shades as the other modal
// windows of the shell (e.g. CharacterStatsWindow.cpp), cf. UI contract.
constexpr D3DCOLOR kColBg        = Argb(0xE0, 0x20, 0x20, 0x28); // panel background
constexpr D3DCOLOR kColTitleBg   = Argb(0xF0, 0x18, 0x18, 0x20); // title bar
constexpr D3DCOLOR kColFrame     = Argb(0xFF, 0x80, 0x80, 0x80); // frame
constexpr D3DCOLOR kColText      = Argb(0xFF, 0xFF, 0xFF, 0xFF); // normal text
constexpr D3DCOLOR kColTitle     = Argb(0xFF, 0xFF, 0xDD, 0x66); // title
constexpr D3DCOLOR kColLabel     = Argb(0xFF, 0xC0, 0xC0, 0xC8); // labels (light gray)
constexpr D3DCOLOR kColHover     = Argb(0xFF, 0x40, 0x60, 0xA0); // hover
constexpr D3DCOLOR kColSelected  = Argb(0xFF, 0xFF, 0xDD, 0x66); // selected slot frame
constexpr D3DCOLOR kColBtn       = Argb(0xFF, 0x38, 0x40, 0x50); // normal button
constexpr D3DCOLOR kColBtnDown   = Argb(0xFF, 0x58, 0x84, 0xC8); // pressed button
constexpr D3DCOLOR kColBtnOff    = Argb(0xFF, 0x30, 0x30, 0x34); // disabled button
constexpr D3DCOLOR kColSlotEmpty = Argb(0xFF, 0x2A, 0x2A, 0x30); // empty slot
constexpr D3DCOLOR kColSuccess   = Argb(0xFF, 0x60, 0xFF, 0x60); // positive delta
constexpr D3DCOLOR kColError     = Argb(0xFF, 0xFF, 0x60, 0x60); // negative delta / error
constexpr D3DCOLOR kColDivider   = Argb(0xFF, 0x50, 0x50, 0x58); // divider
constexpr D3DCOLOR kColDim       = Argb(0xFF, 0x70, 0x70, 0x78); // dimmed text (zero delta)

// --- Geometry constants ---
constexpr int kBoxW      = 580;
constexpr int kBoxH      = 440;
constexpr int kTitleH    = 28;
constexpr int kCloseSize = 18;

constexpr int kGridCols  = 5;
constexpr int kSlotSize  = 48;
constexpr int kSlotGap   = 10;
constexpr int kGridOffX  = 24;              // from box.x
constexpr int kGridOffY  = kTitleH + 24;    // from box.y
constexpr int kGridW     = kGridCols * kSlotSize + (kGridCols - 1) * kSlotGap;

constexpr int kPanelGapX        = 24; // gap between the grid and the info panel
constexpr int kPanelRightMargin = 24;
constexpr int kBtnBottomMargin  = 70; // height reserved at the bottom for the button

constexpr int kEnchantBtnW = 180;
constexpr int kEnchantBtnH = 34;

// Table of the 13 equipment type ids matching the slot order, taken
// VERBATIM from UI/InventoryWindow.cpp (kEquipDelta / cGameHud_InitLayout
// 0x62A5B0, order slotIds[13] = {2,3,4,5,6,7,99,9,10,11,12,13,14}). Used
// ONLY as a reference label (type id): no human-readable slot name is
// confirmed by the disassembly for this window, so none is invented.
constexpr int kEquipSlotIds[kEnchantSlotCount] = {
    2, 3, 4, 5, 6, 7, 99, 9, 10, 11, 12, 13, 14,
};

// Class/slot mapping DOCUMENTED (cf. header comment of UI/EnchantWindow.h
// and Game/ItemSystem.h lines 155-161, comment on Item_GetEnchantStatDelta).
// Sole source of truth for this mapping in this file (the private member
// EnchantWindow::GuessItemClass delegates here).
int ClassifySlotForEnchant(int slot) {
    if (slot == 1) return 8;                                   // special case (slot 1 only)
    if (slot == 0 || slot == 2 || slot == 3 || slot == 4 || slot == 5) return 4; // armor
    if (slot == 7) return 1;                                    // weapon
    return -1;                                                  // not covered by the known table
}

const char* ClassLabelFor(int itemClass) {
    switch (itemClass) {
        case 1: return "Arme";
        case 4: return "Armure";
        case 8: return "Spécial";
        default: return "Non pris en charge";
    }
}

D3DCOLOR SlotColorFor(int slot) {
    // Fixed palette of 13 distinct shades — FALLBACK used when the real .IMG icon of
    // the equipped item (GetIconTex/ResolveItemIconPath above) failed to load.
    static constexpr D3DCOLOR kPalette[kEnchantSlotCount] = {
        Argb(0xFF, 0xC0, 0x50, 0x50), Argb(0xFF, 0xC0, 0x80, 0x40),
        Argb(0xFF, 0xC0, 0xB0, 0x40), Argb(0xFF, 0x90, 0xB0, 0x40),
        Argb(0xFF, 0x50, 0xA0, 0x50), Argb(0xFF, 0x40, 0xA0, 0x90),
        Argb(0xFF, 0x40, 0x80, 0xB0), Argb(0xFF, 0x40, 0x60, 0xC0),
        Argb(0xFF, 0x70, 0x50, 0xC0), Argb(0xFF, 0xA0, 0x40, 0xB0),
        Argb(0xFF, 0xC0, 0x40, 0x80), Argb(0xFF, 0x90, 0x70, 0x50),
        Argb(0xFF, 0x60, 0x60, 0x60),
    };
    if (slot < 0 || slot >= kEnchantSlotCount) return kColSlotEmpty;
    return kPalette[slot];
}

const char* SlotLabelFor(int slot) {
    static char buf[kEnchantSlotCount][24];
    if (slot < 0 || slot >= kEnchantSlotCount) return "?";
    std::snprintf(buf[slot], sizeof(buf[slot]), "Slot %d (id %d)", slot, kEquipSlotIds[slot]);
    return buf[slot];
}

// Display keys/labels for the enchant delta (cf. UI/EnchantWindow.h).
const char* StatKeyLabel(EnchantStatKey key) {
    switch (key) {
        case EnchantStatKey::AtkExt:    return "Attaque Externe";
        case EnchantStatKey::AtkInt:    return "Attaque Interne";
        case EnchantStatKey::DefExt:    return "Défense Externe";
        case EnchantStatKey::DefInt:    return "Défense Interne";
        case EnchantStatKey::MaxHp:     return "PV Max";
        case EnchantStatKey::MaxMp:     return "PM Max";
        case EnchantStatKey::Precision: return "Précision";
        case EnchantStatKey::Evasion:   return "Esquive";
        case EnchantStatKey::Rating:    return "Rating";
    }
    return "?";
}

// Keys 10/20/30/40 are in HUNDREDTHS (converted /100 for display), the
// others in UNITS — faithful to the Item_GetEnchantStatDelta comment.
bool IsHundredthsKey(EnchantStatKey key) {
    return key == EnchantStatKey::AtkExt || key == EnchantStatKey::AtkInt ||
           key == EnchantStatKey::DefExt || key == EnchantStatKey::DefInt;
}

void FormatDelta(char* buf, size_t n, EnchantStatKey key, int rawDelta) {
    if (IsHundredthsKey(key)) {
        const double v = static_cast<double>(rawDelta) / 100.0;
        std::snprintf(buf, n, "%s%.2f", (v > 0.0 ? "+" : ""), v);
    } else {
        std::snprintf(buf, n, "%s%d", (rawDelta > 0 ? "+" : ""), rawDelta);
    }
}

// Enchant delta for `slot`, at level `previewLevel` (replaces byte3 of
// the socket word). Depends only on byte3 (cf. Game/ItemSystem.cpp — the only
// read of socketWord in Item_GetEnchantStatDelta), so a minimal word suffices.
int PreviewDeltaFor(int itemClass, int slot, int previewLevel, EnchantStatKey key) {
    if (itemClass < 0) return 0;
    int lvl = previewLevel;
    if (lvl < 1) lvl = 1;
    if (lvl > 59) lvl = 59;
    const uint32_t previewSocket = static_cast<uint32_t>(lvl) << 24;
    return game::Item_GetEnchantStatDelta(itemClass, slot, previewSocket, static_cast<int>(key));
}

// Enchant state derived from the selected slot (current item + current/next
// level + resolved class). Centralizes the logic used by both the button
// hit-test AND the rendering (single source of truth).
struct EnchantState {
    bool     valid     = false; // slot in [0..12] with an equipped item
    uint32_t itemId    = 0;
    int      itemClass = -1;    // -1 = not covered by the known table
    int      curLvl    = 0;     // current enchant level (byte3 of the socket word)
    int      nextLvl   = 1;     // previewed level (curLvl+1, capped at 59)
    bool     atMax     = false; // curLvl >= 59 (no further progression possible)
};

EnchantState ComputeState(int slot) {
    EnchantState st;
    if (slot < 0 || slot >= kEnchantSlotCount) return st;
    const game::EquipSlot& e = game::g_World.self.equip[static_cast<size_t>(slot)];
    if (e.itemId == 0) return st;
    st.valid     = true;
    st.itemId    = e.itemId;
    st.itemClass = ClassifySlotForEnchant(slot);
    st.curLvl    = static_cast<int>(game::Item_GetAttribByte3(e.socket));
    st.nextLvl   = st.curLvl + 1;
    if (st.nextLvl > 59) st.nextLvl = 59;
    st.atMax     = st.curLvl >= 59;
    return st;
}

} // namespace

// Delegation of static private members to the centralized logic above
// (keeps a single source of truth for the class/slot mapping and the palette).
int EnchantWindow::GuessItemClass(int slot) { return ClassifySlotForEnchant(slot); }
const char* EnchantWindow::ItemClassLabel(int itemClass) { return ClassLabelFor(itemClass); }
const char* EnchantWindow::SlotLabel(int slot) { return SlotLabelFor(slot); }
D3DCOLOR EnchantWindow::SlotColor(int slot) { return SlotColorFor(slot); }

int EnchantWindow::PreviewDelta(int slot, int previewLevel, EnchantStatKey key) const {
    if (slot < 0 || slot >= kEnchantSlotCount) return 0;
    const int itemClass = ClassifySlotForEnchant(slot);
    return PreviewDeltaFor(itemClass, slot, previewLevel, key);
}

// Icons (same lazy+cache pattern as InventoryWindow::GetIconTex)
gfx::GpuTexture* EnchantWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t itemId) {
    // Cache SHARED by file path (cf. SetIconCache/ActiveIconCache): an icon
    // already loaded by InventoryWindow/WarehouseWindow/VendorShopWindow is reused without
    // re-decoding/re-uploading to VRAM (same .IMG file, same ITEM_INFO::iconId).
    const std::string path = ResolveItemIconPath(itemId);
    return ActiveIconCache().GetOrLoad(dev, path);
}

// Layout — centered on the current screen (recomputed every frame, like MsgBoxDialog
// / CharacterStatsWindow).
void EnchantWindow::ComputeLayout(int screenW, int screenH, Layout& L) const {
    L.box.w = kBoxW;
    L.box.h = kBoxH;
    L.box.x = screenW / 2 - kBoxW / 2;
    L.box.y = screenH / 2 - kBoxH / 2;

    L.titleBar = Rect{ L.box.x, L.box.y, L.box.w, kTitleH };

    L.closeBtn = Rect{ L.box.x + L.box.w - kCloseSize - 6, L.box.y + 5,
                        kCloseSize, kCloseSize };

    const int gridX = L.box.x + kGridOffX;
    const int gridY = L.box.y + kGridOffY;
    for (int i = 0; i < kEnchantSlotCount; ++i) {
        const int col = i % kGridCols;
        const int row = i / kGridCols;
        L.slot[i] = Rect{ gridX + col * (kSlotSize + kSlotGap),
                           gridY + row * (kSlotSize + kSlotGap),
                           kSlotSize, kSlotSize };
    }

    const int panelX = gridX + kGridW + kPanelGapX;
    const int panelY = gridY;
    const int panelW = (L.box.x + L.box.w) - kPanelRightMargin - panelX;
    const int panelH = (L.box.y + L.box.h) - kBtnBottomMargin - panelY;
    L.panel = Rect{ panelX, panelY, panelW, panelH };

    L.enchantBtn = Rect{ L.box.x + L.box.w / 2 - kEnchantBtnW / 2,
                          L.box.y + L.box.h - kBtnBottomMargin + 18,
                          kEnchantBtnW, kEnchantBtnH };
}

// Lifecycle
void EnchantWindow::Open() {
    Dialog::Open();
    closeArmed_   = false;
    enchantArmed_ = false;
    for (bool& b : slotArmed_) b = false;
    selectedSlot_ = -1;
}

// Mouse
bool EnchantWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) {
        closeArmed_ = true;
        return true;
    }

    for (int i = 0; i < kEnchantSlotCount; ++i) {
        const Rect& r = L.slot[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h)) {
            slotArmed_[i] = true;
            return true;
        }
    }

    const EnchantState st = ComputeState(selectedSlot_);
    if (st.valid && st.itemClass >= 0 && !st.atMax &&
        PointInRect(x, y, L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h)) {
        enchantArmed_ = true;
        return true;
    }

    // Click anywhere else in the panel: consumed (prevents the click from
    // "passing through" to the 3D world behind the window) but arms nothing.
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

bool EnchantWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    if (closeArmed_) {
        closeArmed_ = false;
        if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) {
            Close();
            return true;
        }
    }

    for (int i = 0; i < kEnchantSlotCount; ++i) {
        if (!slotArmed_[i]) continue;
        slotArmed_[i] = false;
        const Rect& r = L.slot[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h)) {
            selectedSlot_ = i; // selects the item to enchant (even if the slot is empty
                                // or outside the table -> the panel will show the corresponding status)
            return true;
        }
    }

    if (enchantArmed_) {
        enchantArmed_ = false;
        if (PointInRect(x, y, L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h)) {
            const EnchantState st = ComputeState(selectedSlot_);
            if (st.valid && st.itemClass >= 0 && !st.atMax) {
                // TODO(send): enchant request to the server.
                // PROVEN SLOT MANIPULATION (re-audit W4-F3, UI_Enchant_Press 0x5FB770):
                // placing/removing the item to enchant in the window goes through
                // Item_BeginDragTransaction(g_DragCtx, /*type=*/8|9, ...)  // 0x5AFDF0
                // (LOCAL pickup/drop, container types 8/9), NOT a send. The
                // validation (actual send) is in UI_Enchant_OnLUp (Op19 family,
                // sub-op not statically isolated). No dedicated Net_Send* builder for
                // enchanting has been identified in Net/SendPackets.h to date
                // (no "Net_SendEnchant*"). The
                // most likely candidate is the generic action/inventory OUTBOUND
                // dispatcher opcode 0x13 (Outgoing::Op19, Net/Opcodes.h,
                // "sub-op 0..255, vault 201..250"), the mirror of the INBOUND
                // dispatcher Pkt_ItemActionDispatch (opcode 0x1a, EA 0x46A320,
                // Net/ItemActionDispatch.h) which applies the server result
                // (success/failure/break) to the equipment/bag cells. Exact
                // builder to call: Net_SendPacket_Op19(NetClient&, uint8_t
                // subCmd, const void* payload) — Net/SendPackets.h line 216. The
                // precise sub-op "cast an enchant on slot N" is NOT
                // isolated in RE/opcode_table.json nor RE/outbound_results.json:
                // DO NOT guess its value; capture it dynamically (breakpoint
                // on Net_SendPacket_Op19 during an in-game "Enchant" click),
                // then call here:
                //   Net_SendPacket_Op19(nc, /*subCmd=*/<captured>, /*payload=*/&req);
                // where `req` would encode at minimum {slot=selectedSlot_, itemId=st.itemId}.

                // Optimistic LOCAL update (state visible immediately, as requested
                // by the mission, even without an actual network send): advances the
                // displayed enchant level (byte3 of the socket word) of the selected
                // equipment. The server will overwrite this value with the REAL result
                // (success, failure, or item break) upon receiving its response via
                // Pkt_ItemActionDispatch — this preview is NOT guaranteed.
                game::EquipSlot& e = game::g_World.self.equip[static_cast<size_t>(selectedSlot_)];
                e.socket = (e.socket & 0x00FFFFFFu) | (static_cast<uint32_t>(st.nextLvl) << 24);
            }
            return true;
        }
    }

    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

bool EnchantWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// Rendering
void EnchantWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Stores the current screen dims so the hit-test (routed between two
    // frames) aligns with the geometry actually drawn. Done in both
    // sub-passes (Panels then Text), like MsgBoxDialog/CharacterStatsWindow.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Layout L;
    ComputeLayout(ctx.screenW, ctx.screenH, L);

    const auto& equip = game::g_World.self.equip;
    const EnchantState st = ComputeState(selectedSlot_);
    const bool canEnchant = st.valid && st.itemClass >= 0 && !st.atMax;

    char buf[128];

    if (ctx.phase == UiPhase::Panels) {
        // --- Background + frame + title bar ---
        ctx.FillRect(L.box.x, L.box.y, L.box.w, L.box.h, kColBg);
        ctx.FillRect(L.titleBar.x, L.titleBar.y, L.titleBar.w, L.titleBar.h, kColTitleBg);
        ctx.DrawFrame(L.box.x, L.box.y, L.box.w, L.box.h, kColFrame, 2);
        ctx.FillRect(L.box.x, L.box.y + kTitleH, L.box.w, 1, kColDivider);

        // --- Close button ---
        const bool closeHover = PointInRect(cursorX, cursorY, L.closeBtn.x, L.closeBtn.y,
                                             L.closeBtn.w, L.closeBtn.h);
        const D3DCOLOR closeCol = closeArmed_ ? kColBtnDown : (closeHover ? kColHover : kColBtn);
        ctx.FillRect(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, closeCol);
        ctx.DrawFrame(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, kColFrame, 1);

        // --- 13 equipment slots: real .IMG icon if resolved, otherwise fallback to
        // the generic per-slot colored square (SlotColor) — original behavior unchanged.
        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;
        for (int i = 0; i < kEnchantSlotCount; ++i) {
            const Rect& r = L.slot[i];
            const uint32_t itemId = equip[static_cast<size_t>(i)].itemId;
            const bool occupied = itemId != 0;
            const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);

            gfx::GpuTexture* icon = occupied ? GetIconTex(dev, itemId) : nullptr;
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColSlotEmpty); // neutral background under the icon
                const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                               gfx::kSpriteWhite, /*compensatePos=*/true);
            } else {
                ctx.FillRect(r.x, r.y, r.w, r.h, occupied ? SlotColor(i) : kColSlotEmpty);
            }

            D3DCOLOR frameCol = kColFrame;
            int thickness = 1;
            if (selectedSlot_ == i) { frameCol = kColSelected; thickness = 2; }
            else if (hover)         { frameCol = kColHover;    thickness = 2; }
            ctx.DrawFrame(r.x, r.y, r.w, r.h, frameCol, thickness);
        }

        // --- Info / preview panel (right side) ---
        ctx.FillRect(L.panel.x, L.panel.y, L.panel.w, L.panel.h, kColBg);
        ctx.DrawFrame(L.panel.x, L.panel.y, L.panel.w, L.panel.h, kColFrame, 1);

        // --- Enchant button ---
        const bool enchantHover = PointInRect(cursorX, cursorY, L.enchantBtn.x, L.enchantBtn.y,
                                               L.enchantBtn.w, L.enchantBtn.h);
        D3DCOLOR btnCol = kColBtnOff;
        if (canEnchant)
            btnCol = enchantArmed_ ? kColBtnDown : (enchantHover ? kColHover : kColBtn);
        ctx.FillRect(L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h, btnCol);
        ctx.DrawFrame(L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h, kColFrame, 1);
        return;
    }

    // --- Text phase -----------------------------------------------------
    const int titleW = ctx.MeasureText("Enchantement");
    ctx.Text("Enchantement", L.box.x + (L.box.w - titleW) / 2, L.titleBar.y + 6, kColTitle);
    ctx.Text("X", L.closeBtn.x + 5, L.closeBtn.y + 2, kColText);

    // Reference index inside each slot (number + type id, cf. kEquipSlotIds).
    for (int i = 0; i < kEnchantSlotCount; ++i) {
        const Rect& r = L.slot[i];
        std::snprintf(buf, sizeof(buf), "%d", i);
        ctx.Text(buf, r.x + 3, r.y + 2, kColText);
        if (equip[static_cast<size_t>(i)].itemId != 0) {
            const int lvl = static_cast<int>(game::Item_GetAttribByte3(
                equip[static_cast<size_t>(i)].socket));
            if (lvl > 0) {
                std::snprintf(buf, sizeof(buf), "+%d", lvl);
                ctx.Text(buf, r.x + 3, r.y + r.h - 14, kColTitle);
            }
        }
    }

    // --- Info panel ---
    int ty = L.panel.y + 10;
    const int tx = L.panel.x + 12;
    const int lineH = 16;

    if (!st.valid) {
        ctx.Text(selectedSlot_ < 0 ? "Sélectionnez un emplacement" : "Emplacement vide",
                  tx, ty, kColLabel);
        ty += lineH;
        ctx.Text("d'équipement à enchanter.", tx, ty, kColLabel);
    } else {
        const game::ItemInfo* info = game::GetItemInfo(st.itemId);
        std::snprintf(buf, sizeof(buf), "%s", info ? info->name : SlotLabel(selectedSlot_));
        ctx.Text(buf, tx, ty, kColTitle);
        ty += lineH + 4;

        std::snprintf(buf, sizeof(buf), "Classe : %s", ItemClassLabel(st.itemClass));
        ctx.Text(buf, tx, ty, kColLabel);
        ty += lineH;

        std::snprintf(buf, sizeof(buf), "Niveau d'enchant : +%d", st.curLvl);
        ctx.Text(buf, tx, ty, kColText);
        ty += lineH + 6;

        ctx.FillRect(L.panel.x + 8, ty, L.panel.w - 16, 1, kColDivider);
        ty += 10;

        if (st.itemClass < 0) {
            ctx.Text("Non pris en charge par la table", tx, ty, kColError);
            ty += lineH;
            ctx.Text("d'enchantement (mapping classe/slot", tx, ty, kColError);
            ty += lineH;
            ctx.Text("non confirmé pour ce slot).", tx, ty, kColError);
        } else if (st.atMax) {
            ctx.Text("Niveau maximum atteint (+59).", tx, ty, kColLabel);
        } else {
            std::snprintf(buf, sizeof(buf), "Aperçu au niveau +%d :", st.nextLvl);
            ctx.Text(buf, tx, ty, kColLabel);
            ty += lineH + 2;

            bool anyNonZero = false;
            for (int k = 0; k < kEnchantStatKeyCount; ++k) {
                const EnchantStatKey key = kEnchantStatKeys[k];
                const int delta = PreviewDeltaFor(st.itemClass, selectedSlot_, st.nextLvl, key);
                if (delta == 0) continue;
                anyNonZero = true;

                std::snprintf(buf, sizeof(buf), "%s :", StatKeyLabel(key));
                ctx.Text(buf, tx, ty, kColLabel);

                char dbuf[32];
                FormatDelta(dbuf, sizeof(dbuf), key, delta);
                const int dw = ctx.MeasureText(dbuf);
                ctx.Text(dbuf, L.panel.x + L.panel.w - 12 - dw, ty,
                          delta > 0 ? kColSuccess : kColError);
                ty += lineH;
            }
            if (!anyNonZero) {
                ctx.Text("(aucun bonus à ce palier précis)", tx, ty, kColDim);
                ty += lineH;
            }
        }
    }

    // "Enchanter" button label (dimmed when disabled).
    const int btnTextW = ctx.MeasureText("Enchanter");
    ctx.Text("Enchanter",
              L.enchantBtn.x + (L.enchantBtn.w - btnTextW) / 2, L.enchantBtn.y + 9,
              canEnchant ? kColText : kColDim);
}

} // namespace ts2::ui

// UI/NpcDialogWindow.h — NPC service menu, PAGE 0 of the `cNpcWin` mega-window (ts2::ui).
//
// ANCHOR (Pass 4 / wave W6, front `quest-npcdialog`) — THIS FILE WAS RE-ANCHORED:
// the previous version modeled an "Accept / Talk / Close" box that does NOT EXIST
// in the binary, and hung "Accept" off `Npc_Interact 0x53A660`, which is actually
// LOOT PICKUP (op201), not NPC dialogue. Both were removed.
//
// The real object is a 78-page mega-window (`this+180` = page id) routed by:
//   UI_NpcWin_Open           0x5DB530   builds the menu + state
//   UI_NpcWin_CloseRestore   0x5DC1F0   close (restores materials to inventory)
//   UI_NpcWin_OnLDown_Dispatch 0x5DCB10 -> page 0 : UI_NpcMenu_OnLDown 0x5DF560
//   UI_NpcWin_OnLUp_Dispatch   0x5DD3B0 -> page 0 : UI_NpcMenu_OnLUp   0x5DF640
//   UI_NpcWin_Draw_Dispatch    0x5DE180 -> page 0 : UI_NpcMenu_Draw    0x5DFC30
//   UI_NpcWin_OnKey            0x5DE030
// THIS CLASS ONLY CARRIES PAGE 0 (the service menu). The other 77 pages (Shop,
// Warehouse, Craft, Enchant, …) are other windows/fronts and are NOT here: the
// service codes leading to them leave a delegation TODO in DispatchService().
//
// `this+2` IS AN `NpcDefRecord*` (table "mNPC" 005_00005.IMG, Game/ExtraDatabases.h) — PROVEN:
//   - UI_NpcWin_Open 0x5DB530 @0x5db54d: `*(this+2) = *(DWORD*)a2` where
//     a2 = &g_NpcRenderArray[22*idx] (0x1764D14, stride 88) — cf. Npc_ApproachAndInteract
//     0x539DC0 @0x539eb4: `UI_NpcWin_Open(dword_1822EC8, (float*)&g_NpcRenderArray[22*a1])`.
//     So `this+2` = field 0 of the NPC render record = the mNPC record pointer.
//   - UI_NpcMenu_Draw 0x5DFC30 @0x5dfcea: `Crt_Vsnprintf(v7, "%s.....", *(this+2) + 4)`
//     -> `def+4` == NpcDefRecord::name. DIRECT CONFIRMATION of the type.
//   - @0x5dfd97: greeting line read at `*(this+2) + 36 + 255*(*(this+186)) + 51*i`
//     -> `def+36 + 255*page + 51*line` == NpcDefRecord::textGrid[page][line]
//     (textGrid @+36, page = 5*51 = 255 bytes, line = 51 bytes). PROVES the greeting
//     index selects a textGrid PAGE — the old version arbitrarily used textGrid[0].
//
// These three offsets RESOLVE three previously "unknown role" NpcDefRecord fields:
//   +32   fieldA      = count of active greeting pages (init of `greetingUsed_`,
//                        0x5dbffc: `this[ii+181] = (ii >= *(DWORD*)(def+32))`) — confirms
//                        the "nSpeechNum" hypothesis from ExtraDatabases.h.
//   +1312 fieldB      = NPC faction/tribe (guard `def->fieldB - 2 == g_LocalElement`,
//                        0x5e19fe / 0x608b4e) — confirms the "nTribe" hypothesis.
//   +1340 fieldG[100] = menu flags; a service is offered when the value is 2
//                        (0x5db6b5: `*(DWORD*)(def + 4*n + 1340) == 2`) — confirms "nMenu[100]".
// (These fields stay named fieldA/fieldB/fieldG: ExtraDatabases.h is not owned by this file.)
//
// DATA SCOPE: this window receives ONLY the `NpcDefRecord*` — exactly what the binary
// keeps at `this+2`, and everything page 0 needs (services +1340, greetings +32/+36,
// faction +1312). The rest of `UI_NpcWin_Open` (function tail, 0x5dc01d..0x5dc0a8:
// `a2[3]=1`, `a2[4]=0.0`, orienting the NPC toward the player via Math_AngleBetween2D,
// Fx_MeleeSwingUpdate) mutates the RENDER ENTITY, not the window: that belongs to the
// front owning 3D picking / g_NpcRenderArray, not this file.
// TODO [anchor UI_NpcWin_Open 0x5DB530 @0x5dc01d-0x5dc0a8]: "orientation + FX" tail to
// be ported by the 3D-picking front once it calls Open().
//
// WARNING: MISSING WIRING (outside my files, blocking — flagged to the orchestrator):
// `NpcDialogWindow::Open()` is called NOWHERE. Until the front owning 3D-world click
// routing calls `windows.NpcDialog().Open(def)` from the equivalent of
// Npc_ApproachAndInteract 0x539DC0 (dist<=20) / Item_PickupTarget 0x539EC0, this whole
// window remains unreachable. The binary, by contrast, opens it unconditionally.
#pragma once
#include "UI/UIManager.h"
#include "Game/ExtraDatabases.h"   // game::NpcDefRecord (table "mNPC")

namespace ts2::ui {

// Page 76 (paid teleportation) — separate cNpcWin object on the C++ side (cf. UI/TeleportWindow.h).
// Opened by DispatchService(76); the instance is injected by the host (GameWindows).
class TeleportWindow;

class NpcDialogWindow : public Dialog {
public:
    NpcDialogWindow();

    // Number of menu service rows (loops `i < 10` in UI_NpcWin_Open 0x5db5c1,
    // UI_NpcMenu_OnLDown 0x5df5b5, UI_NpcMenu_OnLUp 0x5df695, UI_NpcMenu_Draw 0x5dfdbb).
    static constexpr int kMaxServices = 10;
    // Number of greeting pages (loops `ii < 5` in UI_NpcWin_Open 0x5dbfbd and
    // UI_NpcMenu_PickGreeting 0x5dff1a) == first dimension of NpcDefRecord::textGrid.
    static constexpr int kGreetingSlots = 5;

    // UI_NpcWin_Open 0x5DB530. `npcDef` == `this+2` (mNQC record resolved by the caller
    // via game::GetNpcDefRecord(kindId), same as Npc_DrawMesh 0x57FF00 does for
    // g_NpcRenderArray). nullptr tolerated (no service, no greeting): the binary
    // dereferences without a check, but a record that can't be found cannot happen on
    // the C++ side without crashing — explicit fallback.
    void Open(const game::NpcDefRecord* npcDef);
    void Open() override { Dialog::Open(); } // bare open (Dialog contract default)
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Introspection (tests / wiring front). `row` in [0, kMaxServices).
    int  ServiceCode(int row) const {
        return (row >= 0 && row < kMaxServices) ? serviceCodes_[row] : 0;
    }
    // -1 = no greeting drawn yet (initial state 0x5dc00c); 5 = all consumed
    // (saturation 0x5dff4f); 0..4 = displayed NpcDefRecord::textGrid page.
    int  GreetingIndex() const { return greetingIdx_; }

    // Injects page 76 (teleportation) that service code 76 must open. In the binary
    // the "page" is just a field of the SAME cNpcWin object (*(this+180)); on the C++
    // side it is a separate class, hence this link set by the host (UI/GameWindows).
    // nullptr tolerated (service code 76 becomes a no-op until the link is wired).
    void SetTeleportWindow(TeleportWindow* w) { teleport_ = w; }

private:
    struct Rect { int x, y, w, h; };

    // Geometry re-centered on EVERY call like the binary (nWidth/2 - spriteW/2,
    // nHeight/2 - spriteH/2 — UI_NpcMenu_OnLDown 0x5df574, OnLUp 0x5df654, Draw 0x5dfc54).
    void   Recenter(int screenW, int screenH);
    Rect   PanelRect() const { return { x_, y_, kPanelW, kPanelH }; }
    // Service row `row`: Sprite2D_HitTest(&unk_8F7730, *this+12, *(this+1)+22*row+7, ...)
    // — UI_NpcMenu_OnLDown 0x5df606, OnLUp 0x5df6fb, Draw 0x5dfe2c. Same 3 EAs, same formula.
    Rect   ServiceRowRect(int row) const;

    // switch(*(this+i+170)) from UI_NpcMenu_OnLUp 0x5DF640 @0x5df72c.
    void   DispatchService(int code);
    // UI_NpcMenu_PickGreeting 0x5DFF00 (code 1) — purely local, emits nothing.
    void   PickGreeting();
    // Code 76 (@0x5dfad4 case 76 -> cTeleportWin_Init 0x627BA0): switches to page 76.
    void   OpenTeleportPage();

    // --- Ported service handlers (one per switch case, EA at the head of the implementation) ---
    void   CastReturn();            // code 4    UI_NpcMenu_CastReturn                0x5E19E0 -> Op20
    void   WarpFactionTown();       // code 0x31 UI_WarpFactionTown                   0x608D40 -> Op20
    void   ClassChangeValidate();   // code 0x35 UI_ClassChange_Validate              0x60A310 -> MsgBox(46)
    void   SendOp116AndClose();     // code 0x37 UI_NpcMenu_OnLUp_SendOp116AndClose   0x60FA60 -> Op116
    void   FactionAdvanceCommit();  // code 0x3E UI_FactionAdvance_Commit             0x612C20 -> Op126

    // Shared guard "NPC is my faction": `*(DWORD*)(def+1312) - 2 == g_LocalElement`,
    // otherwise Msg_AppendSystemLine(StrTable005_Get(143)) and NO emission.
    // EA: UI_NpcMenu_CastReturn 0x5e19fe, UI_ClanWarp_Commit 0x608b4e.
    bool   CheckNpcFaction();
    // Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id), g_SysMsgColor).
    static void SysMsg(int strId);

    // Page 76 to open on service code 76 (set by the host via SetTeleportWindow; the
    // binary does *(this+180)=76 on the SAME object — here, two separate classes).
    // nullptr => no-op.
    TeleportWindow* teleport_ = nullptr;

    const game::NpcDefRecord* npcDef_ = nullptr;      // this+2   (0x5db54d)
    int32_t serviceCodes_[kMaxServices] = {};          // this+170..179 (0x5db652/66c/fa6)
    bool    pressLatch_[kMaxServices]   = {};          // this+70..79   (armed 0x5df627, read/cleared 0x5df6b7/6c9)
    bool    greetingUsed_[kGreetingSlots] = {};        // this+181..185 (0x5dbffc)
    int32_t greetingIdx_ = -1;                         // this+186      (0x5dc00c)

    // Screen dims from the last Render: the hit-test (OnMouseDown/OnClick) is routed
    // across two frames and must line up with the drawn geometry (same pattern as
    // MsgBoxDialog). The binary re-centers indifferently in OnLDown/OnLUp/Draw, hence
    // the same re-centering here.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // TODO [anchor UI_NpcMenu_Draw 0x5DFC30 @0x5dfc70/0x5dfc98] REAL panel dimensions =
    // Sprite2D_GetWidth/Height(&unk_8F7608) — UI atlas sprite not resolved on the
    // ClientSource side. Placeholders sized to fit the PROVEN 10-row grid (pitch 22, y+7).
    static constexpr int kPanelW = 240;
    static constexpr int kPanelH = 236;   // 7 + 10*22 + bottom margin

    // Service row grid — PROVEN values (0x5df606 / 0x5df6fb / 0x5dfe2c).
    static constexpr int kRowOffsetX = 12;
    static constexpr int kRowOffsetY = 7;
    static constexpr int kRowPitchY  = 22;
    // TODO [anchor 0x5DF6FB] real row width/height = dims of sprite unk_8F7730
    // (Sprite2D_HitTest tests the sprite's rectangle) — unresolved, placeholders
    // consistent with the proven pitch.
    static constexpr int kRowW = kPanelW - 2 * kRowOffsetX;
    static constexpr int kRowH = 20;

    // "Name + greeting" banner — PROVEN (UI_NpcMenu_Draw 0x5DFC30): drawn ABOVE the
    // panel (negative offsets), and only if greetingIdx_ != -1 (0x5dfca8).
    static constexpr int kNameOffsetX     = 22;    // *this+22            (0x5dfd1c)
    static constexpr int kNameOffsetY     = -144;  // *(this+1)-144       (0x5dfd1c)
    static constexpr int kGreetOffsetY    = -121;  // *(this+1)+20*i-121  (0x5dfd97)
    static constexpr int kGreetPitchY     = 20;    // 20*i                (0x5dfd97)

    static constexpr D3DCOLOR kColBg       = 0xE0202028u; // panel background
    static constexpr D3DCOLOR kColBorder   = 0xFF808080u; // frame
    static constexpr D3DCOLOR kColTitle    = 0xFFFFDD66u; // NPC name (binary style 3)
    static constexpr D3DCOLOR kColText     = 0xFFFFFFFFu; // greeting line (style 1)
    static constexpr D3DCOLOR kColTextDim  = 0xFFAAAAAAu; // unported service (delegation TODO)
    static constexpr D3DCOLOR kColHover    = 0xFF4060A0u; // hover   (atlas state +1)
    static constexpr D3DCOLOR kColPressed  = 0xFF20304Cu; // pressed (atlas state +2)
    static constexpr D3DCOLOR kColRowBg    = 0xFF3A3A46u; // idle row (state +0)
};

} // namespace ts2::ui

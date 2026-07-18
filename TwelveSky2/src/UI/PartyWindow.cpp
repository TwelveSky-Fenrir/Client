// UI/PartyWindow.cpp — member selector (UI_MemberSelectWnd) + "Party" HUD panel.
// See UI/PartyWindow.h for the contract, the proven layout of g_MemberSelectWnd
// 0x184BE38, and the "missing wiring" banner (the 3 triggers live in files not
// owned by this wave).
//
// Include order: Net/ FIRST (NetClient.h pulls <winsock2.h> before <windows.h>,
// which UI/PartyWindow.h pulls transitively via UIManager.h -> <d3d9.h>)
// — same convention as UI/GuildWindow.cpp / UI/ChatWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h: winsock2 before windows (safe order)
#include "Net/NetClient.h"     // net::GlobalNetClient() (singleton g_NetClient 0x8156A0)
#include "UI/PartyWindow.h"
#include "UI/PanelSkin.h"

#include <cstdio>

namespace ts2::ui {

namespace {
// Real panel background (best effort): narrow/tall (252,440) template, the MOST
// repeated (63 non-consecutive occurrences) in the UI atlas folder
// G03_GDATA/D01_GIMAGE2D/001 — candidate NOT CONFIRMED by IDA, picked by default
// for this narrow HUD panel (210 px wide, height dynamic on member count; cf.
// detailed methodology in UI/PanelSkin.h). Automatic fallback to kColBg if absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00472.IMG");

// Formatting without excessive dynamic allocation (snprintf -> std::string).
std::string Fmt(const char* fmt, size_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, v);
    return std::string(buf);
}

// Map_IsArenaZone 0x54B690: `return g_SelfMorphNpcId >= 270 && g_SelfMorphNpcId <= 274;`
// (global 0x1675A98 is g_SelfMorphNpcId despite the IDB comment's "map id" label).
// Ported here rather than in a shared header: this wave only owns
// PartyWindow/SocialWindow. The binary's other callers (Scene/SceneManager.cpp:934,
// Game/WarehouseSystem.h) still carry a "Map_IsArenaZone not modeled" TODO —
// to be consolidated in a later wave that owns Game/MapWarp.h.
bool Map_IsArenaZone() {
    const int32_t morphNpcId = game::g_Client.VarGet(0x1675A98u); // g_SelfMorphNpcId
    return morphNpcId >= 270 && morphNpcId <= 274;               // EA 0x54B6BE
}

// g_SysMsgColor 0x84DFD8 — not modeled as its own field, long-tailed via Var()
// (same convention as Game/SocialSystem.cpp:68).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(0x84DFD8u));
}
} // namespace

// ===========================================================================
// Member selector — UI_MemberSelectWnd (the real "Party" window)
// ===========================================================================

bool PartyWindow::MemberSelectOpen() const {
    // *(this+2) — field +8 of g_MemberSelectWnd 0x184BE38 (guard 0x66737D / 0x66758B).
    return game::g_Client.VarGet(kVarMemberSelectOpen) != 0;
}

// UI_MemberSelectWnd_Open 0x667260.
void PartyWindow::OpenMemberSelect() {
    // --- Arena guard: TOTAL refusal, system message, no emission (0x66726E) ---
    if (Map_IsArenaZone()) {
        // EA 0x667287: StrTable005_Get(g_LangId, 1352) ; 0x667292: Msg_AppendSystemLine.
        game::g_Client.msg.System(game::Str(kStrArenaRefused), SysMsgColor());
        return; // the binary RETURNS here: no bOpen, no reset, no Op57.
    }

    // --- bOpen = 1 (0x66729F): this write is what resurrects the guard of
    // handler 0x3f (Net/GameHandlers_PartyGuild.cpp:186), never written until now. ---
    game::g_Client.Var(kVarMemberSelectOpen) = 1;

    // Button latches reset to 0 (0x6672C4 : loop i<2 over *(this+3..4)).
    msCloseLatch_   = false;
    msConfirmLatch_ = false;

    // Selection = -1 (0x6672D1 : *(this+5) = -1).
    game::g_Client.Var(kVarSelectedSlot) = kSlotUnset;

    // Values of the 10 slots = -2 (0x6672F6 : loop j<10, *(this+j+6) = -2).
    for (int j = 0; j < kRosterSlots; ++j)
        game::g_Client.Var(kVarSlotValuesBase + 4u * static_cast<uint32_t>(j)) = kValueUnset;

    // --- First NON-EMPTY roster slot -> Net_SendOp57(slot) (0x667300..0x667344) ---
    // The binary does `return Net_SendOp57(&g_AutoPlayMgr, k)` INSIDE the loop: a single
    // send, on the FIRST non-empty slot (Crt_Strcmp(g_PartyRosterNames + 13*k, "") != 0).
    // The server replies with 0x3f (the slot's value), whose handler relaunches Op57 on
    // the next slot: one-request-per-response pagination.
    //
    // NB fidelity: `&g_AutoPlayMgr` (0x846C08) is the emission BUFFER, NOT the network
    // client — Net_SendOp57 0x4B90D0 reads the XOR key (0x8156A4), the sequence
    // (0x8156A5) and the socket (0x8156AC) DIRECTLY from g_NetClient 0x8156A0. On the
    // C++ side the buffer is internal to PacketWriter and the singleton is
    // net::GlobalNetClient().
    const auto& names = game::g_World.partyRoster.names;
    for (int k = 0; k < kRosterSlots && k < static_cast<int>(names.size()); ++k) {
        if (names[k].empty()) continue;             // strcmp(..., "") == 0 -> empty slot
        net::NetClient* nc = net::GlobalNetClient();
        // The nc != nullptr check is NOT a courtesy guard: on the real path it is
        // ALWAYS true (the roster is only populated by handler 0x3e, hence only once a
        // game session is established). It only covers the theoretical call outside a
        // session, where the binary would have nothing to emit anyway.
        if (nc)
            net::Net_SendOp57(*nc, static_cast<int8_t>(k)); // opcode 0x39 — EA 0x667344
        break;                                              // `return` inside the loop
    }
}

// UI_MemberSelectWnd_Close 0x667350: *(this+2) = 0, nothing else.
void PartyWindow::CloseMemberSelect() {
    game::g_Client.Var(kVarMemberSelectOpen) = 0; // EA 0x667365
}

void PartyWindow::Close() {
    // UIManager::CloseAll/ResetAll (UI_CloseAllDialogs 0x5AC590) must close the
    // selector too: faithful, 0x6677C4 closes all dialogs before opening it.
    CloseMemberSelect();
    Dialog::Close();
}

// UI_MemberSelectWnd_ProcNet 0x667730 (UI file code 33): toggle.
void PartyWindow::Toggle() {
    if (MemberSelectOpen()) {
        CloseMemberSelect();  // EA 0x66783C
        return;
    }
    // Opening conditioned on scene state (EA 0x667756: g_SceneMgr == 6 &&
    // g_SceneSubState == 4 = in-game). The binary first closes all dialogs
    // (UI_CloseAllDialogs(dword_1821D4C, 1), EA 0x6677C4) then opens (EA 0x6677CC).
    // TODO [anchor 0x667756] g_SceneMgr 0x1676180 / g_SceneSubState 0x1676184 are not
    // modeled as Var() by the files owned here: the scene guard is therefore NOT
    // ported. The caller to be wired (toolbar / shortcut) is already, in the binary,
    // reachable only in-game — the guard is redundant there but should be restored
    // if Toggle() ever gets called from another scene.
    UIManager::Instance().CloseAll();
    OpenMemberSelect();
}

PartyWindow::MsLayout PartyWindow::ComputeMsLayout(int screenW, int screenH) const {
    // Screen centering, recomputed on every event/frame like the binary:
    //   *this      = nWidth/2  - Sprite2D_GetWidth(unk_90265C)/2   (EA 0x6673AD)
    //   *(this+1)  = nHeight/2 - Sprite2D_GetHeight(unk_90265C)/2  (EA 0x6673D2)
    // TODO [anchor 0x6673AD] kMsW/kMsH are DERIVED: the real dimensions come from the
    // .IMG sprite unk_90265C resolved at runtime, unknown statically.
    MsLayout m;
    m.w = kMsW;
    m.h = kMsH;
    m.x = screenW / 2 - kMsW / 2;
    m.y = screenH / 2 - kMsH / 2;
    return m;
}

void PartyWindow::MsSlotRect(const MsLayout& m, int i, int& rx, int& ry, int& rw, int& rh) const {
    // EA 0x667438 : Sprite2D_HitTest(unk_9026F0, *this + 17, *(this+1) + 20*i + 81, ...)
    rx = m.x + kMsSlotDX;
    ry = m.y + kMsSlotDY0 + kMsSlotStep * i;
    rw = kMsSlotW; rh = kMsSlotH; // TODO [anchor 0x667438] derived dims (sprite unk_9026F0)
}

void PartyWindow::MsCloseRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const {
    // EA 0x6674D2 : Sprite2D_HitTest(unk_8F3798, *this + 252, *(this+1) + 24, ...)
    rx = m.x + kMsCloseDX;
    ry = m.y + kMsCloseDY;
    rw = kMsCloseW; rh = kMsCloseH; // TODO [anchor 0x6674D2] derived dims (sprite unk_8F3798)
}

void PartyWindow::MsConfirmRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const {
    // EA 0x667521 : Sprite2D_HitTest(unk_902A68, *this + 214, *(this+1) + 298, ...)
    rx = m.x + kMsOkDX;
    ry = m.y + kMsOkDY;
    rw = kMsOkW; rh = kMsOkH; // TODO [anchor 0x667521] derived dims (sprite unk_902A68)
}

// ===========================================================================
// "Raid frame" HUD panel (kept invention, cf. .h banner)
// ===========================================================================

PartyWindow::Layout PartyWindow::BuildLayout(int screenW, int screenH) const {
    Layout L;
    (void)screenH;

    auto& client = game::g_Client;
    auto& world  = game::g_World;

    // Visibility guard: presence of at least one known player entity.
    //
    // CORRECTION (wave W6): the `Var(0x184BE40) != 0` guard that used to be here has
    // been REMOVED. It relied on a misidentification ("active party flag"):
    // 0x184BE40 is actually the bOpen field of g_MemberSelectWnd (cf. .h banner), it
    // has nothing to do with "the party is active". Since no one wrote it, this
    // guard was ALWAYS false and made this whole panel dead. The contract this
    // panel gives itself ("visible while at least one member is resolved") is
    // already ensured by the `src.empty()` test below — that's the only legitimate
    // guard.
    if (world.players.empty()) return L; // L.visible stays false

    struct RowSrc {
        std::string name;
        int hp = 0, hpMax = 0;
        int mp = 0, mpMax = 0;
        bool hasMp = false;
    };
    std::vector<RowSrc> src;
    src.reserve(kMaxRows);

    // --- Self (always first if present, real source SelfState -> StatEngine) ---
    if (world.players[0].active) {
        RowSrc r;
        r.name   = "Moi";
        r.hp     = world.self.hp;
        r.hpMax  = world.self.maxHp;
        r.mp     = world.self.mp;
        r.mpMax  = world.self.maxMp;
        r.hasMp  = true;
        src.push_back(std::move(r));
    }

    // --- Other members: a slot of the player array is treated as a "resolved
    // party member" if PartyMemberHpSet/PartyMemberUpdate (opcodes 0x7f/0x80) has
    // already written a non-zero max HP for that slot. These two opcodes are
    // emitted by the server ONLY for real party members (unlike world.players[],
    // which contains ANY nearby visible player) — so this is the best available
    // signal to avoid showing strangers. ---
    for (size_t i = 1; i < world.players.size() && src.size() < static_cast<size_t>(kMaxRows); ++i) {
        if (!world.players[i].active) continue;
        const uint32_t addr = static_cast<uint32_t>(kMemberStride * i);
        const int hpMax = client.VarGet(kVarMemberHpMaxBase + addr);
        if (hpMax <= 0) continue; // no party data received for this slot

        RowSrc r;
        // Real name: g_PartyRosterNames (game::g_World.partyRoster.names), populated by
        // Net_OnPartyMemberNameSet/_Clear (opcodes 0x3e/0x40, Net/GameHandlers_PartyGuild.cpp).
        // NOTE (best-effort, cf. Game/GameState.h::PartyRoster): `i` here is an ENTITY
        // index (world.players, resolved by network identity via
        // PartyMemberHpSet/Update), while the name roster is indexed by a
        // SERVER-ASSIGNED slot with no proven link to the entity index. We still read
        // names[i] as the most likely fallback (no known join key in the
        // disassembly); if this slot is empty/not yet received, we fall back to a
        // generic label rather than inventing a name.
        // NB: this dubious join does NOT contaminate the network path — Op57/Op58
        // carry a ROSTER index, read directly from partyRoster.names.
        const std::string& rosterName =
            (i < world.partyRoster.names.size()) ? world.partyRoster.names[i] : std::string();
        r.name = !rosterName.empty() ? rosterName : "Membre";
        r.hp    = client.VarGet(kVarMemberHpBase + addr);
        r.hpMax = hpMax;
        r.hasMp = false; // PartyMemberHpSet writes the SAME address for kind==1 (HP)
                          // and kind==2 (MP): no distinct address for teammates' MP
                          // in the handler's current state.
        src.push_back(std::move(r));
    }

    if (src.empty()) return L; // no resolved member -> hidden

    // --- Geometry (top-left anchored, independent of screen resolution) ---
    L.visible = true;
    L.x = kMarginX;
    L.y = kMarginY;
    L.w = kPanelW;

    const int barW  = kPanelW - 2 * kPadX;
    const int rowH  = kNameH + kBarGapY + kBarH + kBarGapY + kBarH + kRowGapY;
    int ty = kPadY + kTitleH + 4;

    for (const auto& s : src) {
        RowLayout rl;
        rl.name   = s.name;
        rl.nameY  = L.y + ty;
        rl.hp     = s.hp;
        rl.hpMax  = s.hpMax;
        rl.mp     = s.mp;
        rl.mpMax  = s.mpMax;
        rl.hasMp  = s.hasMp;

        rl.hpX = L.x + kPadX; rl.hpY = L.y + ty + kNameH + kBarGapY; rl.hpW = barW; rl.hpH = kBarH;
        rl.mpX = rl.hpX;      rl.mpY = rl.hpY + kBarH + kBarGapY;    rl.mpW = barW; rl.mpH = kBarH;

        L.rows.push_back(std::move(rl));
        ty += rowH;
    }

    L.h = ty - kRowGapY + kPadY;
    (void)screenW;
    return L;
}

// ===========================================================================
// Routing
// ===========================================================================

// UI_MemberSelectWnd_OnMouseDown 0x667370.
bool PartyWindow::OnMouseDown(int x, int y) {
    if (MemberSelectOpen()) {
        // Re-center BEFORE the hit-test, like the binary (0x667394..0x6673D2).
        const MsLayout m = ComputeMsLayout(lastScreenW_, lastScreenH_);

        const auto& names = game::g_World.partyRoster.names;

        // --- 10 name slots: only NON-EMPTY slots are clickable (0x667438) ---
        for (int i = 0; i < kRosterSlots && i < static_cast<int>(names.size()); ++i) {
            if (names[i].empty()) continue;
            int rx, ry, rw, rh;
            MsSlotRect(m, i, rx, ry, rw, rh);
            if (!PointInRect(x, y, rx, ry, rw, rh)) continue;

            // Already selected -> consumed, no effect (0x66744A).
            if (i == game::g_Client.VarGet(kVarSelectedSlot)) return true;

            // 0x667461: Snd3D_PlayScaledVolume — sound not ported (audio layer).
            game::g_Client.Var(kVarSelectedSlot) = i; // 0x66746C

            // 0x66747A: if the slot's value is > 0 -> g_WhisperPresetSlot = 0 (0x66747C)
            // then Crt_StringInit() (0x667498: clears the associated whisper string —
            // buffer not modeled here, only the flag is).
            if (game::g_Client.VarGet(kVarSlotValuesBase + 4u * static_cast<uint32_t>(i)) > 0)
                game::g_Client.Var(kVarWhisperPreset) = 0;
            return true; // 0x667577
        }

        // --- Buttons (reached only if NO slot was hit, i.e. i >= 10) ---
        int bx, by, bw, bh;
        MsCloseRect(m, bx, by, bw, bh);
        if (PointInRect(x, y, bx, by, bw, bh)) {
            msCloseLatch_ = true;  // *(this+3) = 1 — 0x6674EE
            return true;
        }
        MsConfirmRect(m, bx, by, bw, bh);
        if (PointInRect(x, y, bx, by, bw, bh)) {
            msConfirmLatch_ = true; // *(this+4) = 1 — 0x66753D
            return true;
        }
        // Click elsewhere: consumed if inside the panel (0x66756C), otherwise not.
        if (PointInRect(x, y, m.x, m.y, m.w, m.h)) return true;
    }

    // --- Raid frame: consumes only clicks landing ON the drawn panel (avoids
    // click-through into the 3D world). Pure information panel. ---
    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

// UI_MemberSelectWnd_OnClick 0x667580.
bool PartyWindow::OnClick(int x, int y) {
    if (MemberSelectOpen()) {
        // Re-center BEFORE the hit-test (0x6675A2..0x6675E0).
        const MsLayout m = ComputeMsLayout(lastScreenW_, lastScreenH_);

        // --- "Close" button (latch *(this+3)) — 0x6675E6 ---
        if (msCloseLatch_) {
            msCloseLatch_ = false;                    // 0x6675EF
            int bx, by, bw, bh;
            MsCloseRect(m, bx, by, bw, bh);
            if (PointInRect(x, y, bx, by, bw, bh))
                CloseMemberSelect();                  // 0x66762F
            return true;                              // consumed even if the hit fails (0x667622)
        }

        // --- "Confirm" button (latch *(this+4)) — 0x667641 ---
        if (msConfirmLatch_) {
            msConfirmLatch_ = false;                  // 0x66764E
            int bx, by, bw, bh;
            MsConfirmRect(m, bx, by, bw, bh);
            if (!PointInRect(x, y, bx, by, bw, bh)) return true; // 0x667683

            if (game::g_Client.VarGet(kVarSelectedSlot) == kSlotUnset) {
                // No member selected: system message, NO emission (0x667691).
                game::g_Client.msg.System(game::Str(kStrNoSelection), SysMsgColor()); // 0x6676AF
            } else {
                // UI_MsgBox_Open(dword_1822438, 21, StrTable005_Get(g_LangId, 530), "")
                // — EA 0x6676D7. The binary's contextType 21 (this+24) is replaced
                // on the C++ side by the result callback below.
                UIManager::Instance().MsgBox().Open(
                    game::Str(kStrConfirmBody), std::string(),
                    [](int button) {
                        // UI_MsgBox_OnLButtonUp 0x5C0A90.
                        // OK -> jpt_5C0BE5 case 21 = 0x5C11E9.
                        // Cancel -> jpt_5C2DC3: case 21 falls into def_5C2DC3
                        // ("default case, cases 4-7,11-13,15-18,21-36") = NO
                        // emission, no effect — verified in disassembly.
                        if (button != MsgBoxDialog::kBtnOk) return;

                        const int32_t slot = game::g_Client.VarGet(kVarSelectedSlot); // 0x5C11E9
                        net::NetClient* nc = net::GlobalNetClient();
                        // Not a courtesy guard: this box can only open from a selector
                        // opened during a game session, where nc is established.
                        if (nc)
                            net::Net_SendOp58(*nc, static_cast<int8_t>(slot)); // opcode 0x3A — 0x5C11F5
                        // Reset AFTER the send, unconditional (exact order 0x5C11F5 -> 0x5C11FA).
                        game::g_Client.Var(kVarSelectedSlot) = kSlotUnset;
                    },
                    true /* withCancel: box 21 does have a Cancel branch (no-op) */);
            }
            return true; // 0x6676B4
        }

        // No latch armed: NOT consumed (0x6676E3) — the binary returns 0 here.
    }

    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

// ===========================================================================
// Rendering
// ===========================================================================

void PartyWindow::RenderMemberSelect(const UiContext& ctx, int cursorX, int cursorY) {
    (void)cursorX; (void)cursorY;

    // UI_MemberSelectWnd_Render 0x667860: screen re-centering every frame.
    const MsLayout m = ComputeMsLayout(ctx.screenW, ctx.screenH);

    const auto& names = game::g_World.partyRoster.names;
    const int32_t selected = game::g_Client.VarGet(kVarSelectedSlot);

    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(m.x, m.y, m.w, m.h, kColBg);
        ctx.DrawFrame(m.x, m.y, m.w, m.h, kColBorder, 1);

        for (int i = 0; i < kRosterSlots && i < static_cast<int>(names.size()); ++i) {
            if (names[i].empty()) continue; // empty slots not clickable (0x667438)
            int rx, ry, rw, rh;
            MsSlotRect(m, i, rx, ry, rw, rh);
            ctx.FillRect(rx, ry, rw, rh, (i == selected) ? kColSelBg : kColSlotBg);
            ctx.DrawFrame(rx, ry, rw, rh, kColBorder, 1);
        }

        int bx, by, bw, bh;
        MsCloseRect(m, bx, by, bw, bh);
        ctx.FillRect(bx, by, bw, bh, kColBtnBg);
        ctx.DrawFrame(bx, by, bw, bh, kColBorder, 1);

        MsConfirmRect(m, bx, by, bw, bh);
        ctx.FillRect(bx, by, bw, bh, kColBtnBg);
        ctx.DrawFrame(bx, by, bw, bh, kColBorder, 1);
        return;
    }

    // Text phase. Labels: the binary pulls them from StrTable005 via ids not
    // recorded for this panel — game::Str() renders a stable "#<id>" placeholder
    // until the table is deciphered, so we stick to neutral labels.
    // TODO [anchor 0x667860] StrTable005 ids for the title and buttons not recorded.
    const char* title = "Groupe";
    ctx.Text(title, m.x + (m.w - ctx.MeasureText(title)) / 2, m.y + 24, kColTitle);

    for (int i = 0; i < kRosterSlots && i < static_cast<int>(names.size()); ++i) {
        if (names[i].empty()) continue;
        int rx, ry, rw, rh;
        MsSlotRect(m, i, rx, ry, rw, rh);
        ctx.Text(names[i].c_str(), rx + 4, ry + 2, kColText);
    }

    int bx, by, bw, bh;
    MsCloseRect(m, bx, by, bw, bh);
    ctx.Text("X", bx + 6, by + 3, kColText);

    MsConfirmRect(m, bx, by, bw, bh);
    ctx.Text("OK", bx + 18, by + 4, kColText);
}

void PartyWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    (void)cursorX; (void)cursorY;

    const Layout L = BuildLayout(ctx.screenW, ctx.screenH);

    // bOpen_/x_/y_ (Dialog's protected fields) reflect the auto-hidden state,
    // recomputed on EVERY Render (both phases give the same result within the
    // same frame). The modal selector also counts as "open": IsOpen() must be
    // true while it's shown (UIManager doesn't open/close anything on its own,
    // but external callers test IsOpen()).
    const bool msOpen = MemberSelectOpen();
    bOpen_ = L.visible || msOpen;
    x_ = L.x;
    y_ = L.y;

    lastVisible_ = L.visible;
    lastX_ = L.x; lastY_ = L.y; lastW_ = L.w; lastH_ = L.h;

    // Screen dims for the selector's cross-frame hit-test (cf. .h): the binary
    // re-centers on every event from the current nWidth/nHeight.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;

    auto BarFill = [](int cur, int max) -> float {
        if (max <= 0) return 0.0f;
        float t = static_cast<float>(cur) / static_cast<float>(max);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return t;
    };

    if (L.visible) {
        if (ctx.phase == UiPhase::Panels) {
            kPanelBg.Draw(ctx, L.x, L.y, L.w, L.h, kColBg);
            ctx.DrawFrame(L.x, L.y, L.w, L.h, kColBorder, 1);

            for (const auto& r : L.rows) {
                // HP bar (always real data for the displayed rows).
                ctx.FillRect(r.hpX, r.hpY, r.hpW, r.hpH, kColHpBg);
                const int hpFillW = static_cast<int>(r.hpW * BarFill(r.hp, r.hpMax));
                if (hpFillW > 0) ctx.FillRect(r.hpX, r.hpY, hpFillW, r.hpH, kColHpFill);
                ctx.DrawFrame(r.hpX, r.hpY, r.hpW, r.hpH, kColBorder, 1);

                // MP bar: greyed out/empty if no real data (cf. .h banner).
                if (r.hasMp) {
                    ctx.FillRect(r.mpX, r.mpY, r.mpW, r.mpH, kColMpBg);
                    const int mpFillW = static_cast<int>(r.mpW * BarFill(r.mp, r.mpMax));
                    if (mpFillW > 0) ctx.FillRect(r.mpX, r.mpY, mpFillW, r.mpH, kColMpFill);
                } else {
                    ctx.FillRect(r.mpX, r.mpY, r.mpW, r.mpH, kColNoData);
                }
                ctx.DrawFrame(r.mpX, r.mpY, r.mpW, r.mpH, kColBorder, 1);
            }
        } else {
            const char* title = "Groupe";
            ctx.Text(title, L.x + (L.w - ctx.MeasureText(title)) / 2, L.y + kPadY, kColTitle);

            for (const auto& r : L.rows) {
                ctx.Text(r.name.c_str(), L.x + kPadX, r.nameY, kColText);
            }
        }
    }

    // The modal selector draws OVER the raid frame (same dialog, hence same
    // render rank). Cf. .h banner: since party_ is registered at the end of the
    // list (UI/GameWindows.cpp:59), this modal renders below other windows — to
    // be arbitrated by the orchestrator (file not owned here).
    if (msOpen) RenderMemberSelect(ctx, cursorX, cursorY);
}

} // namespace ts2::ui

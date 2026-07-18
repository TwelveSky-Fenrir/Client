// UI/GameHud.cpp — implementation of the in-game HUD (cGameHud), functional skeleton.
// Anchors: cGameHud_InitLayout 0x62A5B0, cGameHud_Render 0x64A900,
//          cGameHud_OnMouseDown 0x62B080. See Docs/TS2_CLIENT_SHELL.md §2.3.
//
// This .cpp holds Init/Shutdown/InitLayout, the drawing primitives
// (DrawFilledRect/DrawBorder/DrawBarFill/DrawBarFillQuantized/DrawAtlasFrame/
// DrawAtlasBar) and OnDeviceLost/OnDeviceReset. The other GameHud sub-passes live
// in GameHud_Vitals.cpp / GameHud_Alliance.cpp / GameHud_Text.cpp /
// GameHud_Render.cpp (same class, split for size only — see those files' own
// banners for their EA anchors). Constants/helpers shared across more than one of
// these translation units live in GameHud_Internal.h.
//
// SCOPE CORRECTION (decompiled 2026-07-14 via idaTs2): `cGameHud_Render 0x64A900`
// (16465 bytes) actually renders the big "character" WINDOW (equipment/quiver/8x8
// bag/skill tree/elements/warehouse-loot tabs, switched on this[226]=1..10, cf.
// Docs/TS2_CLIENT_SHELL.md L.286/L.349) — it draws NO HP/MP bars and NO quickslot
// bar; it only fires when this[175] (window open) is true.
// The REAL always-on HP/MP bars + action bar (what this file models as `GameHud`)
// are rendered by a different, larger, previously undocumented function:
// `UI_GameHud_Render 0x67A3C0` (thiscall, ~50.6 KB decompiled, guarded by
// `g_SceneMgr==6 && g_SceneSubState==4`, IDA comment "render full in-game HUD
// (bars, buttons, chat, minimap)"). All constants below were extracted from THIS
// function; each cites its EA. The two were likely conflated when this skeleton
// was first written (same "main HUD" concept, two distinct UI singletons: cGameHud
// 0x1839568 vs. the always-visible HUD).
//
// REAL VITALS-FRAME TEXTURE (session 2026-07-14, idaTs2 direct JSON-RPC) — verified
// static RE, not a guess:
//  1. decompile(0x67A3C0) confirms `Sprite2D_Draw((int)&unk_8EC114, 0, 0)` @0x67A43D.
//  2. decompile(Sprite2D_Draw 0x4D6B20) -> Sprite2D_EnsureLoaded(this) -> Tex_LoadCompressedDDS
//     with ecx=this+0x68 and a single stack arg = this+4 -> Sprite2D layout confirmed:
//     loaded@0, filename@4 (0x64 bytes), SpriteTexture@0x68 (44 bytes), struct size 148 bytes.
//     Matches gfx::Sprite2D (Gfx/SpriteBatch.h) exactly.
//  3. xrefs_to(0x8EC114): only Render/OnMouseDown read it, no direct writers -> filename is
//     filled at runtime, not a static literal.
//  4. xrefs_to(0x8E8B50): 100+ xrefs across UI/Scene -> base of a shared Sprite2D array used
//     by the whole UI. (0x8EC114 - 0x8E8B50) / 148 = 93 exactly -> unk_8EC114 = element #93.
//  5. find_regex("GIMAGE2D") -> path template "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG" @0x7A7540.
//  6. xrefs_to(0x7A7540) -> single caller Sprite2D_BuildPath 0x4D68E0: `case 1: Crt_Vsnprintf(
//     this+4, "...\001\001_%05d.IMG", a3+1)` -> category 1 = folder 001, file index = a3+1.
//  7. xrefs_to(Sprite2D_BuildPath) -> single caller AssetMgr_InitAllSlots 0x4DEB50:
//     `for (i = 0; i < 4500; ++i) Sprite2D_BuildPath(this + 148*i + 32, 1, i, 0);` -> element i
//     lives at `this + 148*i + 32`. Solving for element(93) == 0x8EC114 gives this = 0x8E8B30
//     (consistent: this+32 = 0x8E8B50, matching step 4).
//  8. File number = a3+1 = 94 -> path `G03_GDATA\D01_GIMAGE2D\001\001_00094.IMG`, present on
//     disk (5453 bytes), decoded with RE/img_extract.py: DXT3, 214x69 embedded dims — consistent
//     with the estimated `layout_.frame` rect { 230, 80 }. Header verified byte-for-byte against
//     Gfx/GpuTexture.h (width@+0, height@+4, FourCC@+28="DXT3", imageSize@+32, "DDS "@+36):
//     GpuTexture::CreateFromImgFile applies with no adaptation.
//
// Loaded in GameHud::Init (below): asset::ImgFile::Load(path) -> gfx::GpuTexture::CreateFromImgFile.
// On failure (missing file/unexpected format) vitalsFrameTex_ stays invalid -> DrawVitalsFrame()
// falls back to the old flat colored rect (kFrameBg/kFrameBorder), unchanged behavior.
//
// COMPLETENESS AUDIT vs Docs/TS2_UI_GAMEHUD_RENDER.md (mission 2026-07-14): full
// section-by-section comparison of the doc's ~17 sections against this file's prior
// state (see mission report for the full table). Two findings drove the changes below:
//  1. UI/ChatWindow.h (§13, bottom-left chat/system window) was a COMPLETE widget
//     (two rings, tabs, input, SyncFromMessageLog) but instantiated NOWHERE in
//     ClientSource — its data pipeline (game::g_Client.msg, fed by ~20 network
//     handlers) ran empty. Wired below (render + sync + tab mouse routing) — keyboard
//     input remains unwired (needs a change in SceneManager.cpp, out of scope here).
//  2. UI/ConsumableBarWindow.h (§14, pixel counterpart of Game/ConsumableBarLogic.h)
//     was likewise complete (two-pass UiContext render, "missing item" tint, owned-
//     quantity badge via game::InventoryCount) but never instantiated either:
//     DrawQuickSlotFrames() below only drew placeholder rects. Wired below as a
//     replacement (DrawQuickSlotFrames kept as fallback if allocation fails, cf. Render()).
//
// Doc items already covered ELSEWHERE (confirmed during the audit, no change here):
//   §9  (buff grid)              -> UI/BuffStatusPanel.h (earlier mission)
//   §12 (minimap)                -> UI/MinimapWidget.h (earlier mission)
//   §16 (bottom-right status)    -> UI/BuffStatusPanel.h (earlier mission)
//   §17 (permanent tracker)      -> UI/QuestTrackerWindow.h via UI/GameWindows.h
//                                    (the transient CALLOUT Quest_DrawTracker 0x510FC0 is
//                                    DISTINCT and was wired THIS mission, see "QUEST MARKER
//                                    CALLOUT WIRING" banner below)
//
// Doc items with NO wiring this pass, for lack of modeled DATA in Game/GameState.h or
// Game/ClientRuntime.h (not a rendering gap, a missing source-of-truth — see mission
// report for detail):
//   §1  EXP bar + elemental mastery bar (HP/MP frame unchanged)
//   §3  selected-item stats panel / §4 talisman panel (no "inventory selection" concept
//       exposed globally)
//   §5  special buff badge / §6 second-row currency bonus badge (globals not modeled)
//   §7  x2 locked-target plates (no "resolved current target" exposed: Game/CombatSystem.h
//       only carries raw +288/+292 offsets on an opaque entity blob, no UI-usable accessor)
//   §10 detail button + list of 76 stacked bonus counters
//   §11 special-mount/vehicle morph panels
//   §15 row of ~20 menu buttons (icons/actions not identified)
//
// One "free" gain added (data already available, just never shown): currency amount
// (§2, game::g_World.self.currency, already kept up to date by Net/GameVarDispatch.cpp
// case 3 and Net/CharStatDeltaDispatch.cpp) — see DrawTextPass() in GameHud_Text.cpp.
//
// AUDIT CORRECTION ("ALLIANCE/PARTY FRAMES" mission, 2026-07-14): the line above
// (same-day earlier audit) claimed §8 (alliance/party plates, EA 0x67B891-0x67BD54)
// was already covered by UI/PartyWindow.h — WRONG. Full reread of UI/PartyWindow.h/.cpp:
// that widget is a "Party" panel reading game::g_World.partyRoster (NOT allianceRoster)
// via an entirely different data mechanism (Var addresses dword_1687458/168745C written
// by PartyMemberHpSet/Update, not a by-name lookup in g_World.players), and its own
// banner documents its top-left position as an UNPROVEN invention (no UI_PartyWin_*/
// UI_PartyHud_* function exists in the IDB). The real §8, proven by EA in
// UI_GameHud_Render itself (`Crt_Strcmp(g_AllianceRosterNames, &String) != 0`, exact
// position (0, 155+50*i)), had NO wired equivalent before this mission. Wired in
// GameHud_Alliance.cpp (DrawAllianceFramePanels/DrawAllianceFrameText, see Render() in
// GameHud_Render.cpp) from game::g_World.allianceRoster (already populated by
// Net/GameHandlers_PartyGuild.cpp, cf. Game/GameState.h::AllianceRoster) cross-referenced
// BY NAME with game::g_World.players[] (same method as the §7 target plate, also
// unwired) to derive REAL HP/MP. FIDELITY LIMIT (no invention): PlayerEntity carries no
// maxHp/maxMp for a REMOTE entity (only game::g_World.self has them, via StatEngine) —
// a known non-self member's gauge is therefore drawn grayed "no known max" rather than a
// hallucinated ratio (same honest-degradation policy as UI/PartyWindow.cpp for teammate MP).
//
// "GM DEBUG TIME OVERLAY" WIRING (§17, mission 2026-07-14): identified but unwired by
// earlier audits. Verified by fresh disasm via idaTs2 direct JSON-RPC (disasm on
// 0x6868c0-0x686955, confirmed by find_regex("NowTime") -> aNowtimeDDDDS @0x7baf78, single
// xref, INSIDE UI_GameHud_Render itself, not in Quest_DrawTracker 0x6868AB as the doc
// implied): exact binary condition `dword_1676108 > 0 && g_GmAuthLevel > 0`
// (0x6868e8-0x6868f8), literal format "NowTime : %d / %d %d:%d %s", fixed anchor (10,150)
// (push 0Ah/push 96h @0x686934/0x68692f, UI_DrawNumberValue 0x53FCC0 -> UI_DrawText(a1=text,
// x, y, ...)). The 4 ints (dword_167610C/1676110/1676114/1676118) are already decoded
// faithfully by ApplySetGameVar case 98 (Net/GameVarDispatch.cpp, from dword_1676108) via
// the same g_Client.Var(address) mechanism as the rest of the long tail — nothing to add on
// the network-dispatch side, only the display was missing. The %s field (byte_167611C)
// stays an honest empty string: Crt_StringInit (0x75CAB0), which fills it in the binary, is
// an unported TODO(ui) stub (cf. GameVarDispatch.cpp), so g_Client.Blob(0x167611C, 64) is
// never written here either — honest degraded behavior, not invented text. See
// DrawDebugTimeOverlay() in GameHud_Text.cpp (called from Render(), gated on
// net::g_GmAuthLevel != 0, same convention as Scene/SceneManager.cpp::host.IsGm).
//
// QUEST MARKER CALLOUT WIRING (§17, mission 2026-07-14, explicitly requested verification
// — the §17 note above mentioned ONLY UI/QuestTrackerWindow.h, conflating TWO distinct
// binary functions): Docs/TS2_UI_GAMEHUD_RENDER.md §17 documents
// `Quest_DrawTracker((int)&g_PlayerCmdController)` (EA 0x6868AB, decompiled for this check)
// as "out of scope". It is NOT the permanent top-right panel (that one is
// UI_CharListWnd_*/QuestTbl, wired via UI/QuestTrackerWindow.h through UI/GameWindows.h):
// it is a TRANSIENT CALLOUT (NPC icon + 3 text lines) shown only while
// `*(this+51576)` (QuestMarkerState::active) is true — armed by
// game::Quest_UpdateMarkerTimer (Game/ComboPickupTick.h, already wired into the game tick by
// Scene/SceneManager.cpp::host.UpdateQuestMarkerTimer, cf. earlier mission report) on a new
// or completed objective, decaying after ~30s (or on closing the target warehouse). AUDIT:
// questMarker.active was set to `true` but nothing ever READ it for rendering -> the callout
// never showed, regardless of quest state (same symptom as the ChatWindow/ConsumableBarWindow
// case above). Wired in GameHud_Text.cpp: DrawQuestMarkerPanel() (sprite pass) +
// DrawQuestMarkerText() (standalone font pass, same policy as DrawDebugTimeOverlay()) — see
// GameHud.h::questMarker_ for the reservation about the state instance being OWNED by GameHud
// (Scene/SceneManager.cpp is not modified, cf. that member's banner).
// FIDELITY LIMITS (no invention): the real icon (SkillDefTbl_GetRecord(mNPC, ...)+1320 ->
// g_AssetMgr_UiAtlasSlots) and variant text (StrTable003_Get(mZONENPCINFO, ...)) depend on
// NPC/string tables OUT OF SCOPE (same tables already documented "not modeled" by
// Game/QuestSystem.h and UI/QuestTrackerWindow.h): replaced by a colored dot (same policy as
// §8 alliance above) and by RAW IDENTIFIERS already exposed by
// game::QuestProgressState/QuestStepRecord (zoneId/npcQuestId/targetId/required), exactly as
// UI/QuestTrackerWindow.cpp::BuildLayout does. The real position (`this+24 - 352/196` per
// `dword_184C648`, neither modeled elsewhere in ClientSource) is likewise not statically
// provable: anchored top-center, below the minimap/currency, same "assumed simplification,
// never blocking" policy as §2 above.
//
// WAVE W9 "hud-vitals" (2026-07-16) — 6 fixes, each re-verified by fresh disasm via idaTs2
// (read-only) before writing. Summary of the corrections in this file family:
//
//  1. HUD-02 — the 4 vitals bars were drawn as COLORED RECTS (DrawBarFill). The binary never
//     does that: they are frames from the shared Sprite2D array g_AssetMgr_UiAtlasSlots
//     0x8E8B50 (stride 148 = 0x94), selected by `base + Crt_ftol(cur*41.0/max)` clamped to
//     base+41 (41 discrete steps). Re-proven bases: HP 95/clamp 136 @0x67A462-0x67A471, MP
//     137/178 @0x67A515-0x67A526, EXP 179/220 @0x67A65A-0x67A66B, Mastery 3543/3584
//     @0x67A707-0x67A718 (Chi base was MISSING from the file before this pass). All at x=57
//     (`push 39h`), y = 8/22/36/50. See DrawAtlasBar/DrawAtlasFrame below; DrawBarFillQuantized
//     (41 steps) is the fallback when the .IMG is missing.
//
//  2. HUD-VIT-01 + HUD-VIT-02 — HP/MP bars/text read game::g_World.self (self.hp/maxHp/mp/
//     maxMp), a PARALLEL store the Net layer never resyncs (.mp/.maxMp/.maxHp are NEVER
//     mirrored from the server stream). The binary reads the PLAYER-ARRAY FIELDS
//     (dword_1687234, stride 0x38C, index 0 = self): cur HP dword_1687370 (=players[0]+0x13C
//     = body+292) / max HP dword_168736C (+0x138 = body+288) @0x67A442-0x67A457; cur MP
//     dword_1687378 (+0x144 = body+300) / max MP dword_1687374 (+0x140 = body+296)
//     @0x67A4F5-0x67A50A. These 4 fields are ALREADY written faithfully by Pkt_CharStatDelta
//     0x465D90 (`imul ecx,38Ch` @0x465FF5-0x46604E) -> Net/CharStatDeltaDispatch.cpp
//     (B_288/B_HP/B_296/B_MP = 288/292/296/300), which also mirrors p.hp/p.mp (ReflectBars).
//     The DENOMINATOR is therefore NOT Char_CalcMaxHP 0x4D4ED0 / Char_CalcMaxMP 0x4D59B0:
//     `xrefs_to` on those two functions returns only ONE caller each — cDrawWin_Draw
//     @0x62A191/@0x62A1F8, the stats window, which writes no bar global. self.maxHp/self.maxMp
//     therefore stay correct FOR THAT WINDOW and are kept as-is (§8 alliance, §7 target plates).
//
//  3. HUD-VIT-06 — (a) source-rewriting clamp `if (dword_1687370 < 0) dword_1687370 = 0`
//     @0x67A490-0x67A499: a PERSISTENT side effect of the render path, placed AFTER the bar
//     and BEFORE the text, not reproduced -> added (cf. DrawVitalsFrame in
//     GameHud_Vitals.cpp, and the GameHud.h::DrawTextPass banner for the resulting ordering
//     constraint). (b) currency: the binary formats a BARE `"%d"` (aD @0x7A9780,
//     `push offset aD` @0x67A885); no "Gold: " prefix exists in ANY string in the binary ->
//     removed. (c) "Lv %d": NO level display exists in the vitals block 0x67A3C0-0x67A8FA
//     (only texts: "%d/%d" HP, "%d/%d" MP, "%.3f" EXP, "%d/%d" mastery, "%d" currency) ->
//     pure invention, removed. (d) 41 steps, cf. (1).
//
//  4. HUD-03 — the row of ~17 menu buttons was HIT-TESTED (OnMouseDown) but NEVER drawn:
//     `kMenuButtons` had only 2 occurrences in all of src/ (its definition and the click loop),
//     zero in Render() -> clickable, invisible buttons. Pattern re-proven @0x685177-0x685229
//     (3 exclusive states, `this+7Ch`=124, `dy=-11h`=-17, next button `add ecx,1B2h`=+434).
//     Wired via DrawMenuButtons() in GameHud_Render.cpp, cf. Render().
//
//  5. G01 — game::InitConsumableBar had NO caller anywhere in src/ (exhaustive grep): slots_
//     stayed value-initialized -> all 10 slots stayed `Empty` -> the quickbar only ever drew
//     empty frames. Wired in Init() below.
//
//  6. HUD-09 (partial) — talisman badge at FIXED anchor (190,75). The gap folder's described
//     structure was WRONG (claimed guard `dword_16758A4 <= 0` wrapping the whole block, with
//     unk_981A84 as the "else" of the talisman range). Re-proven truth @0x67A787-0x67A826: the
//     guard is `> 0` and unk_981A84 is ITS branch (`jle 67A7A6` then `jmp 67A826`); the `<= 0`
//     branch tests 10 <= g_TalismanSlot < 20 and draws the SAME sprite unk_9819F0 for BOTH id
//     ranges (19051-19060 AND 19061-19070); currency/durability are NOT guarded by
//     dword_16758A4. Only the statically-readable-anchor part is ported — see
//     DrawTalismanBadge() (GameHud_Vitals.cpp) and its TODO for the durability blocks (sprite
//     widths unreadable in the IDB).
//
// GAP-FOLDER ITEMS DELIBERATELY NOT APPLIED (see mission report):
//  - HUD-VIT-05: REFUTED. The gap folder claims mLEVEL+6380 is never written (.bss stays zero
//    -> the original divides by 0). False: maybe_LevelTable_InitFloats 0x4C2380 writes
//    *(this+1595)..*(this+1606) @0x4C238A-0x4C2419 — exactly the dwords read by 0x4C2BF0
//    (`*(this + a2 + 1594)`, a2∈[1,12]) — and its only caller CrtInit_LevelTableThunk 0x7A5260
//    is a static CRT initializer run at startup. The missing xref on 0x8E8AF4 is explained by
//    computed indexing (ecx=mLEVEL), not by a missing writer. The C++ side ALREADY ports these
//    values correctly (game::GetRebirthExpSpan, Game/GameDatabase.cpp:151) and the C++ line
//    the gap cites no longer exists. Applying the "fix" would have recorded a falsehood and
//    erased a correct port.
//  - G02 (HUD bar driven by g_Container5, 3 pages x 14 slots): a REAL fix but INAPPLICABLE
//    from this front — `kQuickSlotCount` (GameHud.h:62) is consumed by 4 files owned by other
//    fronts (Game/ConsumableBarLogic.{h,cpp}, UI/ConsumableBarWindow.{h,cpp}): bumping it from
//    10 to 14 would silently mutate their ABI (including ConsumableBarWindow.cpp:71's
//    `totalIconsW`). Escalated to the orchestrator.
#include "UI/GameHud.h"
#include "UI/GameHud_Internal.h" // shared constants/helpers (kBarBorder, kVitalsBarSteps, ...)
#include "Game/GameState.h"      // game::g_World (self.*, players[0] = faithful bar source, cf. wave W9 §2)
#include "Game/ConsumableBarLogic.h" // game::InitConsumableBar (G01 wiring, cf. Init())
#include "Game/GameDatabase.h"   // game::GetLevelInfo/LevelInfo (§1 EXP bar, mission W4-F2)
#include "Game/ActionStateMachine.h" // game::CharActionState (CastSlot0-2/Channel -> §16 cast indicator, mission 2026-07-14)
#include "Game/ClientRuntime.h"  // game::g_Client.msg (MessageLog -> ChatWindow, mission 2026-07-14)
#include "Game/StringTables.h"   // game::g_Strings.zoneNames (§17 quest marker callout, mission 2026-07-14)
#include "Net/NetClient.h"       // net::g_GmAuthLevel (§17 GM debug overlay, mission 2026-07-14)
#include "Core/Log.h"
#include "Asset/ImgFile.h"    // .IMG loading (zlib wrapper + DXT FourCC)
#include "UI/BuffStatusPanel.h" // §9 buff grid + §16 bottom-right panel (mission 2026-07-14)
#include "UI/ConsumableBarWindow.h" // §14 real quickbar (mission 2026-07-14, see banner above)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring> // std::memcpy (RdBodyI32/WrBodyI32, cf. GameHud_Internal.h / GameHud_Vitals.cpp)
#include <string>

namespace ts2::ui {

namespace {

// Path template for category-1 Sprite2D array elements: element i is file index
// i+1 (Sprite2D_BuildPath 0x4D68E0 `case 1:` -> "...\001\001_%05d.IMG", a3+1; loop
// AssetMgr_InitAllSlots 0x4DEB50 `for (i=0;i<4500;++i)`). SAME convention as
// kVitalsFrameImgPath below (element 93 -> 001_00094.IMG). The Mastery base 3543
// (+41 = 3584) stays within the 4500 category elements.
constexpr const char* kVitalsAtlasPathFmt = "G03_GDATA\\D01_GIMAGE2D\\001\\001_%05d.IMG";

// Real vitals-frame background (element #93 of the category-1 Sprite2D array,
// identified by static RE — see file header banner above). Literal path identical
// to the binary's template "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG" (%05d = 94),
// no "GameData\" prefix: same convention as
// UI/InventoryWindow.cpp::ResolveItemIconPath (paths relative to the client's
// launch GameData root).
constexpr const char* kVitalsFrameImgPath = "G03_GDATA\\D01_GIMAGE2D\\001\\001_00094.IMG";

// §17 quest marker callout layout constants (width/height/top anchor), used only
// by InitLayout() below to size layout_.questMarker. The rest of the callout's
// palette/paddings live in GameHud_Text.cpp (only consumers: DrawQuestMarkerPanel/
// DrawQuestMarkerText).
constexpr int kQuestMarkerW  = 260; // width = same template as QuestTrackerWindow (kPanelW)
constexpr int kQuestMarkerH  = 74;  // title + 3 lines
constexpr int kQuestMarkerY0 = 90;  // below the minimap/currency (§2, y<=12), above the buffs (§9)

// Creates an opaque 1x1 white texture (D3DPOOL_MANAGED: survives device reset).
IDirect3DTexture9* CreateWhiteTexture(IDirect3DDevice9* dev) {
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)) || !tex) {
        return nullptr;
    }
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits) {
        *static_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu; // opaque white ARGB
        tex->UnlockRect(0);
    }
    return tex;
}
} // namespace

// =============================================================================
// Init / Shutdown
// =============================================================================
// OUT-OF-LINE definitions (declared in GameHud.h): quickBarWindow_ is a
// std::unique_ptr<ConsumableBarWindow> over a type only forward-declared in the
// header (include cycle, cf. GameHud.h banner) — CONSTRUCTOR and destructor must
// both be instantiated in ONE translation unit that sees the complete type (here,
// after #include "UI/ConsumableBarWindow.h" above): an inline `= default`
// constructor in the header would also need the complete type (implicit exception
// cleanup to destroy quickBarWindow_ if a later member throws during construction).
GameHud::GameHud()  = default;
GameHud::~GameHud() { Shutdown(); }

bool GameHud::Init(gfx::Renderer& renderer, int screenW, int screenH) {
    Shutdown();

    device_      = renderer.Device();
    rendererPtr_ = &renderer; // cf. UI/GameHud.h::rendererPtr_ (audit UiContext.renderer 2026-07-14)
    screenW_ = screenW;
    screenH_ = screenH;
    if (!device_) {
        TS2_ERR("GameHud::Init : device nul");
        return false;
    }

    // 2D sprite (D3DXCreateSprite) for the flat rects of bars/frames.
    if (!sprite_.Create(device_)) {
        TS2_ERR("GameHud::Init : SpriteBatch::Create a echoue");
        Shutdown();
        return false;
    }

    // Client default font (GIGASSOFT_12; cf. Font::MakeDefaultDesc).
    gfx::Font::AddTtfResource(false); // best-effort: ignore failure if the TTF is missing
    if (!font_.Init(device_, screenW_, screenH_, /*trVariant=*/false)) {
        TS2_ERR("GameHud::Init : Font::Init a echoue");
        Shutdown();
        return false;
    }

    // Tintable white texture (bars/frames drawn flat).
    white_ = CreateWhiteTexture(device_);
    if (!white_) {
        TS2_ERR("GameHud::Init : creation de la texture blanche 1x1 a echoue");
        Shutdown();
        return false;
    }

    // Real vitals-frame background (best-effort; falls back to the flat colored
    // rect if the file is missing/unreadable — see DrawVitalsFrame in
    // GameHud_Vitals.cpp). The rest of the HUD (bars, quickslots) has no
    // identified .IMG sprite at this stage and keeps using flat rects (see file
    // header banner).
    {
        asset::ImgFile img;
        if (img.Load(kVitalsFrameImgPath) && img.Kind() == asset::ImgKind::TextureDxt) {
            if (vitalsFrameTex_.CreateFromImgFile(device_, img)) {
                TS2_LOG("GameHud : texture cadre vitales chargee (%s, %ux%u)",
                        kVitalsFrameImgPath, vitalsFrameTex_.Width(), vitalsFrameTex_.Height());
            } else {
                TS2_ERR("GameHud : upload GPU de %s echoue, repli sur rect colore",
                        kVitalsFrameImgPath);
            }
        } else {
            TS2_ERR("GameHud : chargement de %s echoue (kind=%d), repli sur rect colore",
                    kVitalsFrameImgPath, static_cast<int>(img.Kind()));
        }
    }

    InitLayout();

    // Minimap (§12) — see UI/MinimapWidget.h. No GPU resource of its own to
    // create (reuses sprite_/font_/white_ above): Init() only computes its layout.
    minimap_.Init(screenW_, screenH_);

    // Buff grid (§9) + bottom-right panel (§16) — AUTONOMOUS widget (its own
    // SpriteBatch + texture cache, cf. UI/BuffStatusPanel.h); passed the device via
    // `renderer` and our already-initialized font (shared, not owned by
    // BuffStatusPanel). Best-effort: a failure does not block the rest of the HUD's
    // init (falls back to colored dots anyway if GPU resources fail, cf.
    // BuffStatusPanel.h banner).
    if (!buffPanel_.Init(renderer, &font_)) {
        TS2_ERR("GameHud : BuffStatusPanel::Init a echoue, grille de buffs desactivee");
    } else {
        buffPanel_.SetScreenSize(screenW_, screenH_);
    }

    // Chat window (§13, mission 2026-07-14) — lightweight widget, no GPU resource
    // to create here (own lazy white texture, cf. ChatWindow.cpp). Cannot fail (no
    // D3D9 handle touched by this call).
    chatWindow_.SetScreenSize(screenW_, screenH_);

    // Real quickbar (§14, mission 2026-07-14) — replaces the placeholder rendering
    // of DrawQuickSlotFrames() (GameHud_Vitals.cpp) below. Trivial construction (no
    // GPU resource of its own, cf. UI/ConsumableBarWindow.h): can only fail via
    // bad_alloc, falling back to nullptr -> Render()/OnMouseDown() revert to the
    // old path.
    quickBarWindow_ = std::make_unique<ConsumableBarWindow>();

    // G01 (wave W9) — initial quickslot population: game::InitConsumableBar
    // (Game/ConsumableBarLogic.cpp:25, port of UI_ConsumableBar_Init 0x68E270) had
    // NO caller anywhere in src/ (exhaustive grep). slots_ stayed value-initialized
    // -> all 10 slots kept type == QuickSlotType::Empty -> ConsumableBarWindow::Render
    // only ever drew empty frames, GetIconTex was never reached, and HitTest/OnClick
    // always returned ConsumableAction::None. `slots_` is a
    // std::array<QuickSlot, kQuickSlotCount> == game::ConsumableSlots (alias, ConsumableBarLogic.h:41).
    // ASSUMED STOPGAP, not the final source: InitConsumableBar's hardcoded catalog
    // (540,565,...) comes from the standalone consumable DIALOG UI_ConsumableBar_*
    // 0x68E270 (a separate 28-slot widget), while the HUD bar is actually driven by
    // g_Container5 (3 pages x 14 slots x 3 dwords, block 0x684DC9-0x685155). Switching
    // is not feasible from this front: it requires kQuickSlotCount 10 -> 14, consumed
    // by 4 files owned by other fronts (see file header banner, gap G02 escalated to
    // the orchestrator).
    game::InitConsumableBar(slots_);

    TS2_LOG("GameHud initialise (%dx%d)", screenW_, screenH_);
    return true;
}

void GameHud::Shutdown() {
    rendererPtr_ = nullptr;
    quickBarWindow_.reset();
    buffPanel_.Shutdown();
    // Atlas frames of the 4 vitals bars (wave W9): release the resident
    // IDirect3DTexture9 objects BEFORE device_ is reset to nullptr below
    // (GpuTexture::Release doesn't need the device, but keeping the order
    // consistent with vitalsFrameTex_).
    vitalsAtlasCache_.Clear();
    vitalsFrameTex_.Release();
    if (white_) { white_->Release(); white_ = nullptr; }
    font_.Shutdown();
    sprite_.Destroy();
    device_ = nullptr;
}

// =============================================================================
// InitLayout — cGameHud_InitLayout 0x62A5B0 (name kept for documentary anchoring),
// but VALUES extracted from UI_GameHud_Render 0x67A3C0, the real always-on HUD
// function (see file header banner). Each rect below cites the EA of its
// computation in the binary. Decompiled via idaTs2 (direct JSON-RPC, tools/call
// "decompile" on 0x67A3C0 then 0x64A900) on 2026-07-14.
// =============================================================================
void GameHud::InitLayout() {
    // --- Vitals frame (portrait + HP/MP bars), ANCHORED TOP-LEFT -------------
    // REAL: the panel background is drawn at the EXACT screen origin, no offset —
    // Sprite2D_Draw(&unk_8EC114, 0, 0) @0x67A43D (hit-test right before it
    // @0x67A41F). Old placeholder: bottom-left { 12, screenH_-100, 232, 88 } —
    // WRONG, the real panel is top-left, not bottom.
    // Width/height not statically readable (Sprite2D loads its width/height at
    // runtime from the .npk, fields +108/+112 are zero in the IDB — verified via
    // get_int): estimated from the logical extent of the 4 real bars (HP/MP/EXP/
    // Mastery, see below) plus margin.
    layout_.frame = HudRect{ 0, 0, 230, 80 };
    const HudRect& f = layout_.frame;

    // Portrait mini-frame: NO separate portrait sprite identified in the
    // decompiled code (probably baked into the background &unk_8EC114 itself, out
    // of scope — no distinct .IMG to load). Repositioned left of the real bars
    // (which start at x=57, see below), height matched to the 2 real HP+MP rows
    // (14px pitch each). Old placeholder: { f.x+8, f.y+12, 64, 64 } (in a
    // bottom-left frame that doesn't exist in the binary).
    layout_.portrait = HudRect{ f.x + 4, f.y + 4, 48, 48 };

    // HP bar: fill drawn at (57, 8) @0x67A48B; "%d/%d" value right-aligned,
    // right edge x=207 @0x67A4F0 (measured by UI_MeasureNumberText then
    // 207-width). Frame selection = ftol(cur*41.0/max)+95, clamped to 136 (so 41
    // visual steps, dword_1687370[0]/dword_168736C[0]) @0x67A465. Logical width
    // kept = 207-57 = 150 (text right edge = visual bar right edge); height 12
    // (real row pitch is 14px). Old placeholder: { dynamic barX after 64px
    // portrait, f.y+14, dynamic barW, 20 } — wrong anchor (bottom-left) and width.
    layout_.hpBar = HudRect{ f.x + 57, f.y + 8, 150, 12 };

    // MP bar: fill at (57, 22) @0x67A540; text right edge x=207, y=21
    // @0x67A576-0x67A592. Frame = ftol(cur*41.0/max)+137, clamped to 178
    // (dword_1687378[0]/dword_1687374[0]) @0x67A51A. Old placeholder:
    // { barX, f.y+44, barW, 20 }.
    layout_.mpBar = HudRect{ f.x + 57, f.y + 22, 150, 12 };

    // NOTE (observed but NOT wired here — precise TODO, cf. file header banner
    // §1: missing data on the Game/GameState.h side, not an editing constraint of
    // this header): the real panel also draws, after the HP/MP bars, an EXP bar
    // at (57,36) @0x67A685 (41 steps 179-220, remaining-EXP computed vs
    // g_SelfLevel/g_SelfLevelBonus @0x67A59E) and an elemental Mastery bar at
    // (57,50) @0x67A732 (FIXED /3000 scale on dword_168746C — reset by
    // WE_ResetElementMastery, cf. Docs/TS2_DISPATCHERS.md L.260). Same 14px pitch,
    // same text right edge x=207. To be integrated if Layout is extended.

    // --- Quickslot bar, ANCHORED AT THE BOTTOM OF THE SCREEN -----------------
    // REAL (still in UI_GameHud_Render 0x67A3C0, NOT in cGameHud_Render): a loop
    // of 14 slots — not 10 — mapping keys 1..0 (10) PLUS the 4 extended Q/W/E/R
    // slots (cf. Docs/TS2_CLIENT_SHELL.md §4, DIK 0x10-0x13): `for (i=0;i<14;++i)`
    // @0x684DC9+, icon drawn at (frameX + 30*i + 24, frameY + 7) — e.g. item case
    // @0x6850CB, skill case @0x684F3D/0x685042; stack counter (if any) at
    // (frameX + 30*i + 38, frameY + 21) @0x685150. No separate "key" label found:
    // likely baked into the background. kQuickSlotCount STAYS 10 here — precise
    // TODO (not an editing constraint, cf. audit 2026-07-14): bumping to 14 to
    // cover the extended Q/W/E/R slots (DIK 0x10-0x13) would require extending
    // `kQuickSlotCount`, `Layout::slots`, AND `slots_` above, PLUS
    // `Game/ConsumableBarLogic.h::ConsumableSlots` (alias over
    // `std::array<QuickSlot, kQuickSlotCount>`) and `UI/ConsumableBarWindow.h`
    // (same constants) — a cross-file change, not done in this pass (priority
    // given to wiring the already-written widgets, cf. file header banner). Only
    // the 10 numeric slots (1..0) are therefore represented here.
    //
    // Real anchor of the background panel @0x684CA8-0x684D80:
    //   if nWidth > 1024: centered horizontally, flush to the bottom of the
    //                     screen (nWidth/2 - width/2 @0x684CEB,
    //                     nHeight - height @0x684D08)
    //   else (standard launch resolution 1024x768, cf. CLAUDE.md
    //         "/0/0/2/1024/768"): FIXED anchor (391, 728) @0x684CB0/0x684CBC.
    // Background sprite width/height not statically readable (same limits as the
    // vitals frame) -> kept the derived-pitch computation (30px, below) instead
    // of loading the texture.
    constexpr int slotSize = 26; // was 40; derived from the real pitch 30 (below)
    constexpr int slotGap  = 4;  // real pitch = slotSize+slotGap = 30 (old pitch 44)
    const int totalW = kQuickSlotCount * slotSize + (kQuickSlotCount - 1) * slotGap;

    int anchorX = 0;
    int anchorY = 0;
    if (screenW_ > 1024) {
        // Centered branch @0x684CCF-0x684D08 (nWidth/2 - width/2; bottom of screen).
        anchorX = screenW_ / 2 - totalW / 2;
        anchorY = screenH_ - (slotSize + 8);
    } else {
        // Fixed branch @0x684CB0/0x684CBC — LITERAL binary values for the launch
        // resolution 1024x768 (old placeholder: dynamically centered, an anchor
        // that doesn't exist in the binary for this case).
        anchorX = 391;
        anchorY = 728;
    }

    // COORDINATE CORRECTION (audit "misaligned windows", 2026-07-14, RE-VERIFIED by
    // FRESH disasm via idaTs2 direct HTTP JSON-RPC, tools/call "disasm" on
    // 0x684CA8-0x685177): icons are NOT drawn at (anchorX+i*pitch, anchorY) as they
    // were below before — they are offset by a FIXED (+24,+7) from the panel
    // background anchor, verified identical across the 3 content branches (skill
    // @0x684F11-0x684F3D/0x685010-0x685042, item @0x685099-0x6850CB: this.x+i*0x1E+0x18,
    // this.y+7 in all three cases). anchorX/anchorY above stay the PANEL background
    // corner (this[0]/this[1], EA 0x684CB0/0x684CBC confirmed correct); only the
    // position of the 1st slot was missing this offset. This path is only a
    // FALLBACK (quickBarWindow_ null) + secondary hit-test — normal rendering goes
    // through ConsumableBarWindow::ComputeLayout (same correction applied there, cf.
    // its banner).
    constexpr int iconOffsetX = 24; // +0x18, verified 0x684F29/0x684FB3/0x6850B7
    constexpr int iconOffsetY = 7;  // +7,    verified 0x684F14/0x684F9A/0x6850A2
    const int iconX0 = anchorX + iconOffsetX;
    const int iconY0 = anchorY + iconOffsetY;

    layout_.quickBar = HudRect{ anchorX - 4, anchorY - 4, iconOffsetX + totalW + 8, slotSize + iconOffsetY + 8 };
    for (int i = 0; i < kQuickSlotCount; ++i) {
        layout_.slots[static_cast<size_t>(i)] =
            HudRect{ iconX0 + i * (slotSize + slotGap), iconY0, slotSize, slotSize };
    }

    // --- Quest marker callout (§17, Quest_DrawTracker 0x510FC0) --------------
    // Real position not statically provable (`this+24`, a field outside
    // QuestMarkerState, cf. file header banner) -> anchored top-center, width
    // matched to QuestTrackerWindow::kPanelW for visual consistency with the
    // permanent panel (see file header banner for the distinction).
    layout_.questMarker = HudRect{ screenW_ / 2 - kQuestMarkerW / 2, kQuestMarkerY0,
                                    kQuestMarkerW, kQuestMarkerH };
}

// =============================================================================
// Drawing primitives (between sprite_.Begin()/End())
// =============================================================================

// Flat rect: 1x1 white texture scaled to (w,h) and tinted. Uses the
// compensatePos=true path (UI_DrawSpriteScaledAlpha 0x457D70): pos = {x/w, y/h}
// then a scale matrix -> w x h quad anchored exactly at (x,y).
void GameHud::DrawFilledRect(const HudRect& r, D3DCOLOR color) {
    if (!white_ || r.w <= 0 || r.h <= 0) return;
    RECT src{ 0, 0, 1, 1 };
    sprite_.DrawSpriteScaled(white_, &src, r.x, r.y,
                             static_cast<float>(r.w), static_cast<float>(r.h),
                             color, /*compensatePos=*/true);
}

// Outline = 4 flat rects (top / bottom / left / right).
void GameHud::DrawBorder(const HudRect& r, int t, D3DCOLOR color) {
    if (t <= 0) return;
    DrawFilledRect(HudRect{ r.x,             r.y,             r.w, t         }, color);
    DrawFilledRect(HudRect{ r.x,             r.y + r.h - t,   r.w, t         }, color);
    DrawFilledRect(HudRect{ r.x,             r.y,             t,   r.h       }, color);
    DrawFilledRect(HudRect{ r.x + r.w - t,   r.y,             t,   r.h       }, color);
}

// Gauge bar: background + filled portion (cur/max ratio) + 1px outline.
void GameHud::DrawBarFill(const HudRect& r, int cur, int max,
                          D3DCOLOR bg, D3DCOLOR fill) {
    DrawFilledRect(r, bg);

    float ratio = 0.0f;
    if (max > 0) {
        ratio = static_cast<float>(cur) / static_cast<float>(max);
        ratio = std::clamp(ratio, 0.0f, 1.0f);
    }
    const int inner   = 1; // margin to keep the outline visible
    const int availW  = r.w - 2 * inner;
    const int fillW   = static_cast<int>(static_cast<float>(availW) * ratio);
    if (fillW > 0) {
        DrawFilledRect(HudRect{ r.x + inner, r.y + inner, fillW, r.h - 2 * inner },
                       fill);
    }
    DrawBorder(r, 1, kBarBorder);
}

// Fallback for the 4 vitals bars when the atlas .IMG is missing (HUD-02/HUD-VIT-06(d),
// wave W9). Identical to DrawBarFill above, but the fill is QUANTIZED to `steps`
// levels: the binary never computes a continuous ratio, it truncates
// `Crt_ftol(cur*41.0/max)` to pick one of 41 frames (@0x67A44B-0x67A45D). At 99% HP
// the binary therefore shows step 40/41, not a 99%-filled bar. Reproduced here with
// the SAME truncation.
void GameHud::DrawBarFillQuantized(const HudRect& r, int cur, int max, int steps,
                                   D3DCOLOR bg, D3DCOLOR fill) {
    DrawFilledRect(r, bg);

    int level = 0;
    if (max > 0 && cur > 0 && steps > 0) {
        // Crt_ftol = truncation toward zero (cf. Gfx: "float->int conversion (truncation)").
        level = static_cast<int>(static_cast<double>(cur) * steps / static_cast<double>(max));
        level = std::clamp(level, 0, steps); // binary clamp `frame > BASE+41 -> BASE+41`
    }
    const int inner  = 1;
    const int availW = r.w - 2 * inner;
    const int fillW  = (steps > 0) ? availW * level / steps : 0;
    if (fillW > 0) {
        DrawFilledRect(HudRect{ r.x + inner, r.y + inner, fillW, r.h - 2 * inner }, fill);
    }
    DrawBorder(r, 1, kBarBorder);
}

// =============================================================================
// Vitals bars as atlas frames — HUD-02 (wave W9)
// Anchor: UI_GameHud_Render 0x67A3C0, x4 pattern (cf. file header banner §1 and the
// base constants defined locally in GameHud_Vitals.cpp). Sprite2D_Draw 0x4D6B20
// blits at the sprite's NATIVE SIZE: DrawSprite(tex, nullptr, x, y) is the exact
// equivalent (no scaling).
// =============================================================================

// Blits array element `frame` of the category-1 Sprite2D table. false if the
// texture is missing (file absent/unreadable) -> the caller falls back to the
// quantized rect.
bool GameHud::DrawAtlasFrame(int frame, int x, int y) {
    if (!device_ || frame < 0 || !sprite_.Ready()) return false;

    char path[128];
    // file index = frame+1 (Sprite2D_BuildPath 0x4D68E0 `case 1:` -> a3+1).
    std::snprintf(path, sizeof(path), kVitalsAtlasPathFmt, frame + 1);

    gfx::GpuTexture* tex = vitalsAtlasCache_.GetOrLoad(device_, std::string(path));
    if (!tex || !tex->Valid()) return false; // failure cached (no retry per frame)

    return SUCCEEDED(sprite_.DrawSprite(tex->Handle(), nullptr, x, y));
}

// frame = base + ftol(cur*41.0/max), clamped to base+41 — then blit at (x,y).
// NB: the Mastery bar divides by a CONSTANT (`fdiv dbl_7EDAE8` = 3000.0 @0x67A6FC,
// not `fidiv` on a global); passing max=kMasteryMax is arithmetically equivalent.
bool GameHud::DrawAtlasBar(int baseFrame, int cur, int max, int x, int y) {
    if (max <= 0) return false; // the binary doesn't guard this division; C++ guard (div/0)
    int frame = baseFrame +
                static_cast<int>(static_cast<double>(cur) * kVitalsBarSteps / static_cast<double>(max));
    if (frame > baseFrame + kVitalsBarSteps) frame = baseFrame + kVitalsBarSteps; // binary clamp
    return DrawAtlasFrame(frame, x, y);
}

// =============================================================================
// Device loss / restore
// =============================================================================
void GameHud::OnDeviceLost() {
    sprite_.OnLostDevice();
    font_.OnDeviceLost();
    // white_ is D3DPOOL_MANAGED: nothing to do.
    buffPanel_.OnDeviceLost(); // BuffStatusPanel's own ID3DXSprite
}

void GameHud::OnDeviceReset() {
    sprite_.OnResetDevice();
    font_.OnDeviceReset();
    buffPanel_.OnDeviceReset();
}

} // namespace ts2::ui

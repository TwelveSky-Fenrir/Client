// Scene/WorldRenderer_Nameplates.cpp — "hovered entity label" pass, split from
// WorldRenderer.cpp (see WorldRenderer.h for the exact scope of the wiring).
#include "Scene/WorldRenderer.h"
#include "Gfx/Renderer.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/NameplateLogic.h"
#include "Game/NpcInteraction.h"  // game::Npc_GetNameplateColor (0x540790) — MONSTER nameplate color (W9)
#include "Game/ClientRuntime.h"   // game::Str (StrTable005_Get 0x4C1D10) — NameplateHost::ResolveString (W9)
#include "Game/ExtraDatabases.h"  // game::NpcDefRecord (NPC label: name@+4, fieldF[1]@+1332) — W9
// world::World_PickEntityAtCursor (0x538AB0) + world::BuildScreenPickCamera — W9.
// LINK DEPENDENCY FOR ORCHESTRATOR: src\World\TerrainPicker.cpp/.h exist but aren't listed in
// TwelveSky2.vcxproj/.vcxproj.filters (0 "TerrainPicker" hits among 138 explicit <ClCompile>,
// unlike World\WorldMap.cpp/World\WorldIntegration.cpp) -> until added, drawNameplatePass() won't
// link (unresolved external: world::World_PickEntityAtCursor, world::BuildScreenPickCamera).
// Out of .vcxproj scope here; reimplementing hit-test would duplicate the 5 ported Scene_RayHit*
// (0x5415E0/0x541680/0x541780/0x5418B0/0x541920) or invent picking (forbidden).
#include "World/TerrainPicker.h"
#include "World/WorldIntegration.h" // F_ENTITY3D (B8): world::WorldAssets + collision::GroundPlane (shadow ground plane)
#include "Config/GameOptions.h"   // config::g_Options (g_Opt_ShowHitMarkers/ShowNameplates 0x84DED0/D4) — W9
#include "Game/StaticNpcLoader.h" // decor NPCs (mission "PNJ DECOR VISIBLES A L'ECRAN", cf. Render())
#include "Game/AnimationTick.h"       // ZoneNpc_AnimTickIsWired() / Monster_MotionTickIsWired() / IMotionFrameCountOracle — W7
#include "Game/PlayerAnimCursorTick.h" // Player_AnimCursorTickIsWired() (player cursor) — front F_PLAYERANIM
#include "Game/EntityLifecycleTick.h" // g_MonsterTickExt (per-monster motionState/animFrame) — W7
#include "Gfx/MotionCache.h"      // animated bone palette (mirror of g_ModelMotionArray 0x8E8B30) — W3-F1
#include "Gfx/PlayerPaperdoll.h"  // player paperdoll (Char_RenderModel 0x527020 layer) — W3-F1
#include "Game/WeaponTrailResolver.h" // weapon trail: id->v6 switch + stems (front F_WEAPONTRAIL)
#include "Core/Log.h"
#include <cstring>

#include "Scene/WorldRenderer_Internal.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2 {

namespace {

// Approximate color-code palette for NameplateLogic (game::kNameColor*).
// OUT OF SCOPE: the real palette lives in UI assets (literals passed to
// UI_DrawNumberValue 0x53FCC0, never direct RGB) — not reversed here. Readable
// hues are chosen per role (neutral/hostile/admin/affiliate/GM...) so the
// nameplate stays visually usable, without claiming pixel fidelity to the real
// palette.
D3DCOLOR ColorFromNameplateCode(int code) {
    switch (code) {
        case game::kNameColorHostileHidden:     return 0xFFFF4040u; // red
        case game::kNameColorAdminPrimary:
        case game::kNameColorAdminSecondary:    return 0xFFFFC040u; // gold
        case game::kNameColorWhisper:           return 0xFF60D0FFu; // cyan
        case game::kNameColorAffiliate:         return 0xFF40FF80u; // green
        case game::kNameColorAllianceAffiliate: return 0xFF40D0D0u; // teal
        case game::kNameColorGmAccount:         return 0xFFFF60FFu; // magenta
        case game::kNameColorVipOrMarketCase0:
        case game::kNameColorMarketCase1:
        case game::kNameColorMarketCase2:
        case game::kNameColorMarketCase3:
        case game::kNameColorMarketGroupA_Std:
        case game::kNameColorMarketGroupA_Alt:  return 0xFFFFA040u; // orange
        case game::kNameColorNeutral:
        default:                                return 0xFFFFFFFFu; // white
    }
}

// ===========================================================================
// Pass 4 / wave W9 — front nameplate-entity: fields read by Char_DrawNameplate 0x56EF40 on
// the ENTITY (g_EntityArray 0x1687234, stride 908), translated into offsets within
// PlayerEntity::body. CONVERSION RULE: the body starts at entity+0x18 (Pkt_SpawnCharacter
// 0x4646C0 does `Crt_Memcpy(&dword_168724C[227*i], v8, 600)` with dword_168724C ==
// g_EntityArray + 0x18) => bodyOffset = entityOffset - 24. Same convention as
// kPlayerBodyWeaponItemIdOffset (WorldRenderer.cpp) and World/TerrainPicker.cpp:107-120.
//
// HARD BOUND: the body is only 600 bytes, i.e. entity+24..entity+623. Entity fields beyond
// +623 (e.g. altWhisperName +756) are NOT in the body — they are runtime fields written
// elsewhere, so they are NOT populated here (and are only used by the "detailed" block anyway,
// which is dead — cf. §DRAWMODE of NameplateLogic.h).
//
// TRIPLE CORROBORATION of the 3 most structural offsets — World_PickEntityAtCursor 0x538AB0
// reads THE SAME fields on THE SAME base and passes them to Combat_CanTargetOnMap with EXACTLY
// the signature of NameplateHost::CanTargetOnMap(element, pkLevel, affiliation):
//     byte_168725C  = 0x1687234 + 0x28  -> entity+40  -> body+16  (affiliation)
//     dword_168728C = 0x1687234 + 0x58  -> entity+88  -> body+64  (element)
//     dword_1687320 = 0x1687234 + 0xEC  -> entity+236 -> body+212 (pkLevel)
// -> identical values to those already ported in World/TerrainPicker.cpp (kBodyAffiliation
// = 40-24, kBodyElement = 88-24, kBodyPkLevel = 236-24). No divergence between the two fronts.
// ===========================================================================
constexpr size_t kNpBodyHasIdentity   = 24  - 24; // entity+24  : resolved identity — SAME field as
                                                   //   the `dword_168724C[227*i]` guard of 0x538AB0
constexpr size_t kNpBodyGmAccount     = 28  - 24; // entity+28  : (this+7)==1  -> color 35 @0x56FFED
constexpr size_t kNpBodyLevel         = 32  - 24; // entity+32  : (this+8)     @0x56F229
constexpr size_t kNpBodyAffiliation   = 40  - 24; // entity+40  : (this+10)    @0x56F8B6
constexpr size_t kNpBodyElement       = 88  - 24; // entity+88  : (this+22)    @0x56FD3F
constexpr size_t kNpBodyTitleBarExtra = 220 - 24; // entity+220 : `cmp [ecx+0DCh],0` @0x56F115
constexpr size_t kNpBodyEnchantRaw    = 224 - 24; // entity+224 : (this+56)    @0x56F6DC
constexpr size_t kNpBodyPkLevel       = 236 - 24; // entity+236 : (this+59)    @0x56F29B
constexpr size_t kNpBodySpecialPk     = 244 - 24; // entity+244 : (this+61)==12 -> color 44 @0x570006
                                                   //   (== EntityManager kPActionState, same field)
constexpr size_t kNpBodyAlliance      = 472 - 24; // entity+472 : (this+118)   @0x56F870
constexpr size_t kNpBodyAdminTitle    = 488 - 24; // entity+488 : (this+122)==1 @0x56F6C9 (cf. "trade"
                                                   //   note in NameplateLogic.h)
constexpr size_t kNpBodyAdminTitleAlt = 496 - 24; // entity+496 : (this+124)==1
constexpr size_t kNpBodyVipWord       = 544 - 24; // entity+544 : WORD (this+272)==1 -> color 5 @0x56FFD7
constexpr size_t kNpBodyRankTier      = 568 - 24; // entity+568 : (this+142)   @0x57002B
constexpr size_t kNpBodySuppressExtra = 576 - 24; // entity+576 : `cmp [edx+240h],0` @0x56F124

// Read bound for affiliation/alliance strings. VALUE TAKEN AS-IS from
// World/TerrainPicker.cpp:111 (`kAffiliationMaxLen = 60 - 40`): the gap to the next known field
// (subAffiliationName entity+60, cf. NameplateActor). NOT PROVEN to be the real buffer capacity —
// it's a DEFENSIVE bound (never an overrun, at worst a truncated name rather than an invented
// one), same convention as EntityManager::kPNameBufLen.
constexpr size_t kNpBodyStringMaxLen = 60 - 40;

// LE 16-bit word read (VIP: `*((_WORD*)this + 272)` @0x56FFD7).
uint16_t ReadBodyU16LE(const std::array<uint8_t, 600>& body, size_t offset) {
    if (offset + sizeof(uint16_t) > body.size()) return 0;
    uint16_t v = 0;
    std::memcpy(&v, body.data() + offset, sizeof(v));
    return v;
}

// Bounded C-string read from the body (same pattern as EntityManager::ReadPlayerName /
// TerrainPicker::ReadCString: never an out-of-bounds byte).
std::string ReadBodyCString(const std::array<uint8_t, 600>& body, size_t offset, size_t maxLen) {
    if (offset >= body.size()) return std::string();
    const size_t avail = body.size() - offset;
    const size_t cap   = (maxLen < avail) ? maxLen : avail;
    const uint8_t* p = body.data() + offset;
    size_t len = 0;
    while (len < cap && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

} // namespace

// ===========================================================================
//  "Hovered entity label" pass — 2D block of Scene_InGameRender 0x52D0B0
//  (@0x52FB58..0x53120B). See the declaration banner in WorldRenderer.h.
// ===========================================================================

void WorldRenderer::drawNameplatePass(const gfx::Camera& camera, const game::DrawCullContext& cull,
                                      const D3DXMATRIX& viewProj) {
    if (!font_.Ready()) return;

    // ---- 1. Cursor position -> client coordinates -------------------------
    // @0x52FB5C `call ds:off_7A6364` (GetPhysicalCursorPos) then @0x52FB6C
    // `call ds:off_7A6368` (ScreenToClient) with hWnd = ds:hWndParent (0x815184);
    // Point.x -> var_94, Point.y -> var_4B4 = the 2 screen args of World_PickEntityAtCursor.
    // ASSUMED GAP (1 call): GetCursorPos is used instead of GetPhysicalCursorPos — same
    // convention as UI/UIManager.cpp:293 (already in place for UI_RenderAllDialogs 0x5AE2D0,
    // which itself also uses GetPhysicalCursorPos). The two only differ under DPI
    // virtualization; aligning with the rest of ClientSource avoids two contradictory cursor
    // conventions in the same process.
    POINT pt{};
    GetCursorPos(&pt);
    if (hwnd_) ScreenToClient(hwnd_, &pt);

    // ---- 2. Hit-test -> category + index -----------------------------------
    // World_PickEntityAtCursor 0x538AB0 (ported: World/TerrainPicker.cpp:206). The binary calls
    // the same function TWICE, the only difference being the 5th argument:
    //   @0x530F7E `push 0` if g_Opt_ShowHitMarkers == 0   (cmp/jnz @0x530F54)
    //   @0x530FA6 `push 1` otherwise
    // -> a5 == (g_Opt_ShowHitMarkers ? 1 : 0). This is the ONLY LIVE effect of this option in
    // the entire "nameplate" domain (cf. §DRAWMODE of Game/NameplateLogic.h).
    const bool allowModifierTargets = (config::g_Options.ShowHitMarkers != 0);

    // byte_8013FE < 0 (SIGNED byte = bit 7 set = key held). World/TerrainPicker.h:136-147
    // establishes that byte_8013FE == DirectInput state[0x2A] == DIK_LSHIFT (DirectInput array
    // base = g_GfxRenderer+5564 = 0x8013D4, doubly corroborated by byte_8013F2 == DIK_A and
    // byte_8013E5 == DIK_W, already ported).
    // ASSUMED AND DOCUMENTED GAP: the Win32 state (GetAsyncKeyState) is read rather than the
    // DirectInput array, because ts2::input::InputSystem has NO global instance (it belongs to
    // App and is passed by reference) and WorldRenderer::Render(camera) — whose caller
    // Scene/SceneManager.cpp is NOT a file of this front — does not receive it. SAME physical
    // key, SAME 2-state semantics; the only theoretical divergence is a one-frame lag between
    // the DirectInput snapshot and the async state. Preferred over an unassigned hook (which
    // would leave this whole pass dead). Cf. report: wiringTodoForOrchestrator.
    const bool modifierKeyDown = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;

    const world::collision::ScreenPickCamera pickCam =
        world::BuildScreenPickCamera(camera, screenW_, screenH_);
    // EntityPickHost::CanTargetOnMap not wired (Combat_CanTargetOnMap 0x558740 not ported —
    // depends on Map_GetPvpMode 0x4FAB90 + the PVP zone system) -> defaults to false -> category
    // 3 (attackable player) never comes out and falls back to category 1. Degradation already
    // assumed by World/TerrainPicker.h:201-203; categories 1/2/3 ALL converge on
    // Char_DrawNameplate(a2=2), so this has no effect on THIS call site.
    const world::EntityPickHost pickHost{};

    int pickKind = 0, pickIndex = -1;
    world::World_PickEntityAtCursor(game::g_World, pickCam, pt.x, pt.y,
                                     allowModifierTargets, modifierKeyDown, pickHost,
                                     pickKind, pickIndex);
    if (pickKind == 0 || pickIndex < 0) return; // nothing under the cursor -> no label

    // ---- 3. Shared context --------------------------------------------------
    // g_SelfMorphNpcId 0x1675A98 == current zone id: equivalence established by
    // Scene/SceneManager.cpp:462 (`game::g_World.zoneId = zoneId; // g_SelfMorphNpcId`) and
    // reused as-is by World/TerrainPicker.cpp:250. This field drives the "market" groups
    // 270..274 / 291 of Char_DrawNameplate.
    game::NameplateViewerContext vctx{};
    vctx.localMorphNpcId = game::g_World.zoneId;
    vctx.selfX = cull.localPlayerPos.x;
    vctx.selfY = cull.localPlayerPos.y;
    vctx.selfZ = cull.localPlayerPos.z;
    vctx.optShowNameplates = (config::g_Options.ShowNameplates != 0); // g_Opt_ShowNameplates 0x84DED4
    vctx.optShowHitMarkers = (config::g_Options.ShowHitMarkers != 0); // g_Opt_ShowHitMarkers 0x84DED0
    // optDetailedNameplates STAYS false — dword_1668F64 is NEVER written in the binary (4 reads,
    // 0 writes): the title/guild/whisper/icons/GM-debug block it guards (@0x57008D) is DEAD
    // CODE. Populating it would show what the original client never shows. DO NOT "turn it on".
    // Cf. §DRAWMODE of Game/NameplateLogic.h.
    //
    // localGmAuthLevel / localFactionSubMode / localFactionFlag / chatColorWhisper /
    // partyFlagIndicatorCount are left at 0 INTENTIONALLY: re-checked this wave, none of them
    // has an observable effect on the live path (drawMode=2) —
    //   · localGmAuthLevel: read by the @0x56F679 gate (short-circuited by `a2 != 1`) and by
    //     `hostile = gm <= 0 && CanTargetOnMap(...)` (CanTargetOnMap not wired -> false
    //     regardless of gm) and by the GM debug block (dead). Wiring it would require
    //     Net/NetClient.h (winsock2.h AFTER WorldRenderer.h's windows.h -> guaranteed include
    //     error) for ZERO effect: refused.
    //   · localFactionSubMode / localFactionFlag: read only in the `hostile` branch (never
    //     taken, cf. above).
    //   · chatColorWhisper / partyFlagIndicatorCount: "detailed" block, dead.
    game::NameplateHost host{};
    // CAMERA near-cull (Target_IsBeyondClickRange 0x5410D0, call @0x56EF96 with a2=20.0). WIRED
    // (gap HUD-NP-09) to the ALREADY existing, identical implementation game::IsBeyondCameraNearCull
    // (Game/EntityDrawLogic.cpp:22) — until now this callback was null and NameplateLogic fell
    // back to `true`, plainly skipping the cull.
    // The callback receives y ALREADY offset by +10 (cf. NameplateHost): `y - 10` is passed back
    // with radius=20 to literally reconstruct the original arguments
    // (`Target_IsBeyondClickRange((float*)this+63, 20.0)` -> dy = camY - (pos.y + 20*0.5)).
    host.IsBeyondCameraNearCull = [&cull](float x, float y, float z) {
        return game::IsBeyondCameraNearCull(game::Vec3{ x, y - 10.0f, z }, 20.0f, cull.cameraPos);
    };
    // StrTable005_Get(g_LangId, id) 0x4C1D10 -> game::Str (stable "#<id>" placeholder as long as
    // 001.DAT isn't decrypted, cf. Game/ClientRuntime.h:198-202). Wired: this is the convention
    // already used by every network handler in the project.
    host.ResolveString = [](int stringId) { return game::Str(stringId); };
    // ResolveValueTierPrefix (UI_GetValueTierString 0x54CD40), ResolveEnchantName
    // (Item_GetEnchantNameString 0x548330), CanTargetOnMap (Combat_CanTargetOnMap 0x558740),
    // IsSameAffiliationAsLocal / IsSameAllianceAsLocal (social sentinels): NONE is ported in
    // ClientSource (exhaustive grep) -> left null, fallbacks documented in NameplateLogic.h
    // ("" / false). TODO [anchors 0x54CD40, 0x548330, 0x558740]: wire right here once these
    // functions exist — this site is their consumer.
    // IsOnScreen (Cam_ProjectToScreen 0x6A24F0): left null -> falls back to `true`. The real
    // screen gate is applied right after by drawEntityLabel -> worldToScreen (which rejects
    // clip.w <= 0 and |ndc| > 1.5), so wiring it here would do the work twice.

    // ---- 4. 8-case switch @0x530FC7 — AT MOST ONE label per frame ---------
    switch (pickKind) {
    // -------------------------------------------------------------------
    // Categories 1/2/3 — PLAYERS (g_EntityArray). All 3 cases call the SAME
    // Char_DrawNameplate(&g_EntityArray[0x38C*idx], /*a2=*/2, /*a3=*/idx, arg_0):
    //   @0x531052 (cat 1, neutral) / @0x5310A5 (cat 2, trade) / @0x5310F8 (cat 3, attackable)
    // `a3` = the picker's loop index, whose player loop starts at `i = 1` (@0x538ACB): index 0
    // (the local player) is NEVER returned -> notSelf is ALWAYS true on this path. The original
    // client therefore never draws a nameplate above ITS OWN head. The `pickIndex >= 1` guard
    // below materializes this invariant (defensive: the picker already guarantees it).
    // -------------------------------------------------------------------
    case 1:
    case 2:
    case 3: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.players.size()) break;
        if (pickIndex < 1) break; // self excluded by construction (@0x538ACB `for (i = 1; ...)`)
        const game::PlayerEntity& p = game::g_World.players[static_cast<size_t>(pickIndex)];

        game::NameplateActor actor{};
        actor.active      = p.active;                                                  // entity+0
        // hasIdentity (entity+24): SAME field as the picker's `dword_168724C[227*i]` guard
        // (0x168724C == g_EntityArray + 0x18 == entity+24 == body+0). Replaces the old
        // `actor.hasIdentity = true; // TODO(fidelity)` that short-circuited the test.
        actor.hasIdentity = (ReadBodyU32LE(p.body, kNpBodyHasIdentity) != 0);
        actor.x = p.x; actor.y = p.y; actor.z = p.z;   // entity+252/+256/+260, cf. @0x56F133..0x56F15D
        actor.name = p.name;                            // entity+72 = body+48 (EntityManager::ReadPlayerName)

        actor.level        = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyLevel));
        actor.element      = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyElement));
        actor.enchantRaw   = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyEnchantRaw));
        actor.pkLevel      = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyPkLevel));
        actor.rankTierValue = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyRankTier));
        actor.isGmAccount   = (ReadBodyU32LE(p.body, kNpBodyGmAccount) == 1);          // @0x56FFED -> 35
        actor.isVipOrHighlighted = (ReadBodyU16LE(p.body, kNpBodyVipWord) == 1);       // @0x56FFD7 -> 5
        actor.isSpecialPkState   = (ReadBodyU32LE(p.body, kNpBodySpecialPk) == 12);    // @0x570006 -> 44
        actor.isAdminTitle       = (ReadBodyU32LE(p.body, kNpBodyAdminTitle) == 1);    // @0x56F6C9
        actor.isAdminTitleAlt    = (ReadBodyU32LE(p.body, kNpBodyAdminTitleAlt) == 1);
        actor.hasTitleBarExtraHeight  = (ReadBodyU32LE(p.body, kNpBodyTitleBarExtra) != 0); // @0x56F115
        actor.suppressExtraNameHeight = (ReadBodyU32LE(p.body, kNpBodySuppressExtra) != 0); // @0x56F124
        actor.affiliationName = ReadBodyCString(p.body, kNpBodyAffiliation, kNpBodyStringMaxLen);
        actor.allianceName    = ReadBodyCString(p.body, kNpBodyAlliance,    kNpBodyStringMaxLen);
        // hpCur/hpMax/mpCur/mpMax (entity+316/+312/+324/+320) NOT populated: their only consumer
        // is the HP/MP bar block, guarded by `a2 == 1` (@0x56EFBF) hence DEAD.
        // titleKind / subAffiliationName / whisperName / statusIconA..F: same, "detailed" block
        // (dword_1668F64 == 1), dead. Do not populate what can't be read.

        const game::NameplateInfo info =
            game::ComputeNameplateInfo(actor, /*drawMode=*/2, /*notSelf=*/true, vctx, host);
        if (info.visible && info.nameBlockVisible && !info.mainLine.text.empty()) {
            const D3DXVECTOR3 labelPos(actor.x, actor.y + info.labelAnchorYOffset, actor.z);
            drawEntityLabel(info.mainLine.text, labelPos,
                            ColorFromNameplateCode(info.mainLine.color), viewProj);
        }
        break;
    }

    // -------------------------------------------------------------------
    // Category 4 — NPC (render pool g_NpcRenderArray, == g_World.npcRenderEntries after the W7
    // unification). Fx_MeleeSwingDrawMarker(&g_NpcRenderArray[0x58*idx], /*a2=*/2, idx) @0x531148.
    // Text = def->name (def+4), color = Quest_GetMarkerSpriteBase -> 10.
    // -------------------------------------------------------------------
    case 4: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.npcRenderEntries.size()) break;
        const game::NpcRenderEntry& n = game::g_World.npcRenderEntries[static_cast<size_t>(pickIndex)];
        if (!n.def) break; // `*(_DWORD*)this` dereferenced without a guard by the binary; we protect it

        game::ZoneNpcLabelRenderState st{};
        st.active      = n.active;
        st.pos         = { n.x, n.y, n.z };
        st.clickRange  = static_cast<int>(n.def->fieldF[1]); // def+1332 (click range, cf. ExtraDatabases.h)
        st.markerDefId = static_cast<int>(n.def->id);        // def+0 (never read by the 0x540770 stub)

        const game::ZoneNpcLabelContent lc =
            game::ComputeZoneNpcLabelContent(st, /*drawMode=*/2, vctx.optShowHitMarkers, cull);
        if (lc.visible) {
            const std::string text(n.def->name, ::strnlen(n.def->name, sizeof(n.def->name)));
            if (!text.empty()) {
                drawEntityLabel(text, D3DXVECTOR3(lc.worldPos.x, lc.worldPos.y, lc.worldPos.z),
                                ColorFromNameplateCode(lc.colorCode), viewProj);
            }
        }
        break;
    }

    // -------------------------------------------------------------------
    // Category 5 — MONSTERS (dword_1766F74, stride 280).
    // Char_DrawOverheadName(&dword_1766F74[0x118*idx], idx) @0x531199. Decompiled:
    //   v5 = (float)*(int*)(*((_DWORD*)this + 24) + 252);          // def+252 = drawSize
    //   if (Target_IsBeyondClickRange(this + 8, v5)) {             // camera near-cull ONLY
    //     v7[1] = (double)(*(def+252) + *(def+260) + 1) + *(this+9);        /*0x5814AE*/
    //     NameplateColor = Npc_GetNameplateColor((int)this);                /*0x5814C3*/
    //     UI_DrawNumberCentered((const char*)(def + 4), v7, NameplateColor);/*0x5814DC*/ }
    // `this+24` (dword) = record+96 = MonsterEntity::def (documented as "+0x60" in
    // GameState.h:211). No 300-unit cull, no ShowHitMarkers guard here.
    // -------------------------------------------------------------------
    case 5: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.monsters.size()) break;
        const game::MonsterEntity& m = game::g_World.monsters[static_cast<size_t>(pickIndex)];
        if (!m.def) break;
        const game::MonsterInfo& mi = *static_cast<const game::MonsterInfo*>(m.def);

        // EntityRenderInfo: drawSize = def+252 = MonsterInfo::collDim[1] (collDim is at +248);
        // nameplateExtraOffset = def+260 = MonsterInfo::field260. This `info` block was NEVER
        // populated by WorldRenderer (gap noted in the WorldRenderer.h banner):
        // ComputeOverheadNameContent therefore received radius=0 and a zero label height.
        game::EntityRenderInfo rinfo{};
        rinfo.drawSize             = mi.collDim[1];
        rinfo.nameplateExtraOffset = static_cast<int>(mi.field260);

        game::EntityRenderState st{};
        st.active = m.active;
        st.pos    = { m.x, m.y, m.z }; // record+32/36/40 (this+8/9/10)
        st.info   = &rinfo;

        const game::OverheadNameContent oc = game::ComputeOverheadNameContent(st, cull);
        if (oc.visible) {
            const std::string text(mi.name, ::strnlen(mi.name, sizeof(mi.name))); // def+4
            if (!text.empty()) {
                // Npc_GetNameplateColor 0x540790 — ALREADY ported (Game/NpcInteraction.cpp:90)
                // and until now NEVER called for monsters: this is THE color source specific to
                // this label (10 = ally/matched element, 2 = hostile, 22/33 = power gap),
                // distinct from Char_DrawNameplate's PK/guild/GM palette.
                game::NpcQuestContext qctx{};
                qctx.localElement = static_cast<int>(game::g_World.self.element); // g_LocalElement 0x1673194
                // dword_1687320[0] = entity[0]+236 = players[0].body+212 — SAME field as the
                // pkLevel read by the picker on other entities (cf. the offsets banner).
                if (!game::g_World.players.empty()) {
                    qctx.factionFlag = static_cast<int>(
                        ReadBodyU32LE(game::g_World.players[0].body, kNpBodyPkLevel));
                }
                // elementLoadout (g_ElementLoadout 0x1685E14) and pairedElement
                // (Char_GetPairedElement 0x557C00) NOT ported -> defaults documented in
                // Game/NpcInteraction.h ({} and -1). TODO [anchors 0x1685E14 / 0x557C00].
                const int colorCode = game::Npc_GetNameplateColor(
                    m.def, qctx, game::g_World.self.level, game::g_World.self.levelBonus);
                drawEntityLabel(text, D3DXVECTOR3(oc.worldPos.x, oc.worldPos.y, oc.worldPos.z),
                                ColorFromNameplateCode(colorCode), viewProj);
            }
        }
        break;
    }

    // -------------------------------------------------------------------
    // Category 6 — dword_17AB534 (stride 152). NO label, INTENTIONALLY.
    // The switch's case 6 (@0x5311A0) ONLY sets the 5/6 cursor pair (`push 5` @0x5311B9): it
    // calls NO drawing function. And the only function that draws a label on this array,
    // Char_DrawNameTag 0x583470, has a SINGLE xref @0x52FCD9 — inside the dead
    // `dword_1668F64 == 1` block.
    // => this array NEVER has a label in the original client. This is also why the
    // `g_World.npcs` loop of Render() no longer emits one (it used to, before W9).
    // -------------------------------------------------------------------
    case 6:
        break;

    // -------------------------------------------------------------------
    // Category 7 — ZONE OBJECTS (g_ZoneObjectArray 0x180EEF4, stride 76).
    // Obj_DrawNameLabel(&g_ZoneObjectArray[19*idx]) @0x531206. Full decompilation:
    //   if (*(_DWORD *)this)                                            /*0x5840CF*/
    //     if (Math_Dist3D((float *)this + 6, flt_1687330) <= 300.0) {   /*0x5840FD*/
    //       Crt_Vsnprintf(v3, "%s", (const char *)this + 49);           /*0x584117*/
    //       v4[0] = *((float *)this + 6);                               /*0x584128*/
    //       v4[1] = *((float *)this + 7) + 12.0;                        /*0x58413A*/
    //       v4[2] = *((float *)this + 8);                               /*0x584146*/
    //       UI_DrawNumberCentered(v3, v4, 7); }                         /*0x58415B*/
    // No camera near-cull, no option guard: ONLY the <= 300 distance to the local player, and a
    // LITERAL color 7. pos = record+24/28/32 = body+0/4/8 (the body starts at record+0x18 — same
    // conversion as World/TerrainPicker.cpp:347); text = record+49 = body+25.
    // -------------------------------------------------------------------
    case 7: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.zoneObjects.size()) break;
        const game::ZoneObjectEntity& z = game::g_World.zoneObjects[static_cast<size_t>(pickIndex)];
        if (!z.active) break; // `if (*(_DWORD*)this)` @0x5840CF

        float zp[3] = { 0.0f, 0.0f, 0.0f };
        std::memcpy(zp, z.body.data(), sizeof(zp)); // record+24/28/32 == body+0/4/8

        // `Math_Dist3D(pos, flt_1687330) <= 300.0` @0x5840FD (local player, NOT the camera).
        const game::Vec3 zpos{ zp[0], zp[1], zp[2] };
        if (game::Distance3D(zpos, cull.localPlayerPos) > game::kSelfProximityDrawDistance) break;

        // Text = record+49 = body+25, C-string. DEFENSIVE bound: up to the end of the body
        // (52 bytes, cf. GameState.h:359) — the real capacity isn't proven (the binary calls
        // Vsnprintf into a 1000-byte buffer with no input bound). Never an overrun.
        const uint8_t* nameStart = z.body.data() + 25;
        const size_t   nameCap   = z.body.size() - 25;
        size_t nameLen = 0;
        while (nameLen < nameCap && nameStart[nameLen] != 0) ++nameLen;
        if (nameLen == 0) break;
        const std::string text(reinterpret_cast<const char*>(nameStart), nameLen);

        // v4[1] = pos.y + 12.0 (@0x58413A); LITERAL color 7 (@0x58415B — an immediate `push 7`,
        // NOT a computed palette, unlike the other 3 labels). The 7 is written as-is: it happens
        // that game::kNameColorWhisper is also 7, but that's a value COINCIDENCE, not a role ->
        // do not reuse that constant here. All these codes live in the SAME space (the color
        // argument of UI_DrawNumberCentered 0x53FD00 / UI_DrawNumberValue 0x53FCC0), hence the
        // same ColorFromNameplateCode for the 4 label families.
        drawEntityLabel(text, D3DXVECTOR3(zp[0], zp[1] + 12.0f, zp[2]),
                        ColorFromNameplateCode(7), viewProj);
        break;
    }

    // Category 0 (`Skill_CanCastAtCursor` @0x530FCE): no label, only a cursor shape (`push 7` /
    // `push 8`). Nothing to draw here.
    case 0:
    default:
        break;
    }
}

} // namespace ts2

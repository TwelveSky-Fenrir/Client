// Game/NameplateLogic.h — Nameplates of TwelveSky2 (ts2::game).
//
// Faithful C++ rewrite (real translation of the disassembly, no invention) of the LOGIC
// (displayed text, color, visibility conditions) of nameplates drawn above characters.
// NO actual D3D rendering here (no sprites, no screen projection, no font): this module
// only computes a decision — the Gfx layer (out of scope for this mission) consumes
// `NameplateInfo` to draw.
//
// Original functions translated (EA -> function):
//   Char_DrawNameplate       0x56EF40 -> ComputeNameplateInfo() (logic extracted; actual
//                                        D3D/sprite rendering is left as a precise // TODO)
//   Npc_GetNameplateColor    0x540790 -> ALREADY implemented in Game/NpcInteraction.h/.cpp
//                                        (Npc_GetNameplateColor) — RE-VERIFIED during this
//                                        mission, 100% CONFORMANT with the current disassembly
//                                        (exhaustive branch-by-branch comparison done on
//                                        2026-07-14, see conformance note at the bottom of the
//                                        file). Not duplicated here: this file reuses/calls
//                                        the one in NpcInteraction.h for all "faction/quest"
//                                        color code (faction/element/PK branches of
//                                        Npc_GetNameplateColor); Char_DrawNameplate has ITS
//                                        OWN distinct color logic (PK/guild/GM/market)
//                                        translated here.
// Callees required for fidelity, translated internally (no external dependency):
//   (no heavy computation — only text/ratio composition; Crt_ftol -> truncation toward
//   zero, per the convention established in Game/StatFormulas.h)
//
// ---------------------------------------------------------------------------------------
// FIELD PROVENANCE (important): Char_DrawNameplate reads ~35 fields on `this` (entity
// pointer, g_EntityArray 0x1687234, stride 908 — cf. Game/GameState.h::PlayerEntity, whose
// CURRENT layout (opaque body[600]) does not name these fields individually, WITH ONE
// EXCEPTION: `name` (this+72 = body+48) IS decoded on the GameState/EntityManager side since
// the NAMEPLATES mission of 2026-07-14 — cf. GameState.h::PlayerEntity::name and
// Game/EntityManager.cpp::ReadPlayerName — and wired by WorldRenderer::Render() into
// `actor.name` instead of the "Player#i" placeholder it used before this mission). As with
// NpcInteractionExt in Game/NpcInteraction.h, we introduce here a `NameplateActor` view with
// EACH field annotated with its original offset (in bytes, `this+N`), to be populated by the
// caller from the (future) full port of the player entity. No value is invented: each field
// below corresponds to an explicit read in the 0x56EF40 disassembly.
//
// UNCERTAIN SEMANTICS (documented honestly, NO silent simplification):
//  - `this+40` (affiliationName) and `this+472` (allianceName) are compared by string equality
//    to distinct globals depending on context (byte_1686138 in "market" morph 291 mode,
//    unk_16746A8 in the normal branch, g_AllianceRosterNames for alliance). RE-VERIFIED
//    (mission "unlock 425-428", 2026-07-14): byte_1686138 is NOT written by a "different
//    network handler" — its only write site in the entire binary is
//    Net_OnWorldEntityDispatch itself (case 422, `Crt_StringInit(&byte_1686138,
//    &byte_1686145)` = copy from the twin buffer byte_1686145), and byte_1686145 is NEVER fed
//    real content anywhere in the image (confirmed exhaustively via xrefs_to on both symbols —
//    see the detailed comment at the implementation site in Net/WorldEntityDispatch.cpp,
//    subcases 425..428). unk_16746A8 (Net_OnTeamFormationDispatch) and g_AllianceRosterNames
//    (Net_OnGuildRosterReset/Join/Leave) remain, for their part, properly written by their
//    respective handlers — only byte_1686138/byte_1686145 is dead on the original client side.
//    The exact role (guild vs party vs alliance) is NOT confirmed with certainty — exposed via
//    2 predicates `NameplateHost::IsSameAffiliationAsLocal` (sentinel depending on the active
//    morph) and `IsSameAllianceAsLocal`, to be wired by the caller onto the real social
//    structures (Game/SocialSystem.h/GuildSystem.h) once their semantics are confirmed by a
//    future RE pass.
//  - `g_SelfMorphNpcId` (0x1675A98, already documented elsewhere — Game/SkillCombat.h,
//    Game/MapWarp.h — as "current NPC/town/posture id of the local player") drives HERE a
//    "market"/special-zone display mode (values 270..274, 291, 324, 342) where the nameplates
//    of ALL characters change presentation (different text + color palette). Confirmed by
//    cross-checking with Combat_CanTargetOnMap 0x558740 (same list of special values, per-zone
//    PVP mode via Map_GetPvpMode).
//  - Char_DrawNameplate's `a2`/`a3` parameters (a4 confirmed NEVER read in the function body —
//    removed from the API): taken over here under the names `drawMode` (a2) and `notSelf`
//    (a3). See the §DRAWMODE section below — the previous wording ("a2=1 for the main pass")
//    described a DEAD path.
//
// =======================================================================================
// §DRAWMODE — WHICH CALL PATH ACTUALLY EXISTS (Pass 4 / wave W9, nameplate-entity front).
// READ BEFORE TOUCHING THIS FILE.
// =======================================================================================
// `xrefs_to(Char_DrawNameplate 0x56EF40)` = EXACTLY 4 sites, ALL in
// Scene_InGameRender 0x52D0B0:
//
//   @0x52FC02  a2=1, a3=i  — `for (i; i < g_EntityCount; ++i)` loop over g_EntityArray.
//                            ***DEAD PATH***, UNREACHABLE: the loop is guarded by
//                            `cmp ds:dword_1668F64, 1 / jz / cmp ds:dword_1668F64, 2 / jnz`
//                            (@0x52FB8E..@0x52FC09) and dword_1668F64 is NEVER WRITTEN.
//   @0x531052  a2=2, a3=idx — hover-switch category 1 (neutral player)
//   @0x5310A5  a2=2, a3=idx — category 2 (trade partner)
//   @0x5310F8  a2=2, a3=idx — category 3 (attackable player)
//
// Proof that dword_1668F64 is never written (re-proven this wave via TWO independent means,
// at the byte level, over the ENTIRE image):
//   · find_bytes('64 8F 66 01', min_ea..max_ea) => 4 occurrences: 0x52FB90, 0x52FB99,
//     0x52FC0B, 0x570088 — all 4 are the OPERANDS of the `cmp` instructions above. No `mov`.
//   · xrefs_to(0x1668F64) => 4 xrefs, all of type READ (0x52FB8E, 0x52FB97,
//     0x52FC09, 0x570086); definition `dword_1668F64 dd 0`.
// => dword_1668F64 is permanently 0. `drawMode == 1` NEVER HAPPENS.
//
// DIRECT CONSEQUENCES, all verified against the disassembly — the following blocks of
// ComputeNameplateInfo() are FAITHFULLY INERT and must STAY that way:
//   · HP/MP bars           : gated by `a2 == 1 && !a3 && g_Opt_ShowNameplates == 1`
//                            (@0x56EFBF) -> dead. (Cf. NameplateBar::atlasFrame: the atlas
//                            formula is modeled for fidelity, NOT to actually be rendered.)
//   · Gate @0x56F679       : `a2 != 1 || g_Opt_ShowHitMarkers && (g_GmAuthLevel >= 1 ||
//                            Math_Dist3D(...) <= 300.0)` -> with a2=2 it ALWAYS short-
//                            circuits true. `optShowHitMarkers`, `localGmAuthLevel` and
//                            `selfX/Y/Z` therefore have NO observable effect on the live
//                            path; they are populated for fidelity, not necessity.
//                            (The only LIVE effect of g_Opt_ShowHitMarkers in this domain
//                            is the 5th argument of World_PickEntityAtCursor 0x538AB0
//                            @0x530F54 — cf. World/TerrainPicker.h.)
//   · "Detailed" block     : `if (dword_1668F64 == 1)` @0x57008D -> dead. It contains the
//                            guild/title line, the whisper line, the 6 status icons
//                            (@0x570448..@0x5706C7) and the GM debug overlay. Hence
//                            `NameplateViewerContext::optDetailedNameplates` MUST stay
//                            false: populating it would display what the binary never
//                            displays.
//
// In other words: the original client only draws a nameplate for the SINGLE entity under
// the cursor, and NEVER for the local player (World_PickEntityAtCursor 0x538AB0's picking
// loop starts at `i = 1` @0x538ACB — index 0 is excluded). The ClientSource call site is
// Scene/WorldRenderer.cpp::drawNameplatePass().
//
// RULE: this file does not edit any existing header. Includes Game/GameState.h (SelfState,
// GameDatabases, g_World), Game/GameDatabase.h (transitively) and Game/StatFormulas.h
// (CalcEvasion, reused as-is for the GM debug line "CRKDEF::[n]").
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <cmath>

#include "Game/GameState.h"
#include "Game/StatFormulas.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Faithful color codes (integer literals passed to UI_DrawNumberValue 0x53FCC0 in the
// binary — exact palette defined on the UI asset side, out of scope for this mission). The
// names below describe the ROLE observed at the call site, not an RGB palette.
// ---------------------------------------------------------------------------
constexpr int kNameColorNeutral           = 1;  // "normal" name (no known affiliation, not GM)
constexpr int kNameColorHostileHidden     = 2;  // targetable enemy -> real name hidden (element/market)
constexpr int kNameColorAdminPrimary      = 3;  // (+122)==1 && (+124)==1 : admin variant A ; ALSO GM debug overlay
constexpr int kNameColorAdminSecondary    = 4;  // (+122)==1 && (+124)!=1 : admin variant B
constexpr int kNameColorVipOrMarketCase0  = 5;  // word (+272)==1 (VIP/highlight) ; market palette 324/342 case 0
constexpr int kNameColorWhisper           = 7;  // "whisper" line ((+125)==1), fixed literal
constexpr int kNameColorAffiliate         = 9;  // affiliation ((+40)) == local player's
constexpr int kNameColorMarketGroupA_Std  = 13; // market group 270..274, (+59) != 1
constexpr int kNameColorMarketCase2       = 24; // market palette 324/342 case 2
constexpr int kNameColorAllianceAffiliate = 29; // alliance ((+472)) == local player's
constexpr int kNameColorMarketGroupA_Alt  = 32; // market group 270..274, (+59) == 1
constexpr int kNameColorMarketCase1       = 33; // market palette 324/342 case 1
constexpr int kNameColorGmAccount         = 35; // (+7)==1 (GM account) — highest priority (tested last)
constexpr int kNameColorMarketCase3       = 44; // market palette 324/342 case 3 ; ALSO (+61)==12 (special status)
constexpr int kNameColorGmDebugLine       = 3;  // debug overlay (LEVEL/RANK/CRKDEF), fixed literal

// ---------------------------------------------------------------------------
// View of "fields read by Char_DrawNameplate on the drawn entity" (this+N, cf. header
// banner). Parallel to g_World.players[i]/g_World.Self() (same pattern as NpcInteractionExt):
// until the full player-entity port (908 bytes) exists, the caller populates this view with
// the real values at draw time.
// ---------------------------------------------------------------------------
struct NameplateActor {
    // --- visibility gates ---
    bool  active       = false; // +0    : entity active/alive
    bool  hasIdentity  = false; // +24   : identity resolved (name received from server)

    // --- world position (used for the screen anchor position ; NO screen math here) ---
    float x = 0.0f, y = 0.0f, z = 0.0f; // +252/+256/+260

    // --- identity / text ---
    std::string name;               // +72  : character name (base text)
    std::string affiliationName;    // +40  : affiliation name (guild or party depending on context, cf. banner)
    std::string subAffiliationName; // +60  : sub-text (secondary title, used if titleKind==2)
    std::string allianceName;       // +472 : alliance name
    std::string whisperName;        // +504 : text shown on the "whisper" line ((+125)==1)
    std::string altWhisperName;     // +756 : alternate text ((+187)!=0 && (+125)==0)

    // --- numeric values ---
    int level          = 0;   // +32 (duplicated at +112 for the debug overlay, added to +108)
    int levelBonus      = 0;   // +108 : level bonus (debug overlay "LEVEL::[level+levelBonus]")
    int rankTierValue   = 0;   // +568 : raw value for the "tier" prefix (UI_GetValueTierString)
    int titleKind        = 0;   // +56  : 0 = no title, 1 = simple title, 2 = title + sub-text
    int element           = 0;   // +88  : character element/faction (0..3)
    int pkLevel            = 0;   // +236 : PK rank / "crime" status (0..3+)
    int enchantRaw          = 0;   // +224 : raw weapon enchant value (grade = enchantRaw/100)

    // --- flags ---
    bool hasTitleBarExtraHeight   = false; // +220 : affects the label's Y offset (rendering, Gfx TODO)
    bool suppressExtraNameHeight  = false; // +576 : same
    // WARNING SUSPECT NAMES — "admin" is NOT proven; strong evidence points to the opposite (W9).
    // World_PickEntityAtCursor 0x538AB0 reads THESE SAME OFFSETS to detect the TRADE
    // PARTNER (click category 2, @0x538BCC):
    //     g_TradePartnerIdLo[0] == 1 && g_TradePartnerIdLo[227*i] == 1
    //  && dword_1687420[0]     == dword_1687420[227*i]
    //  && dword_1687424[0]     != dword_1687424[227*i]
    // with g_TradePartnerIdLo = 0x168741C = entity+488, dword_1687420 = entity+492,
    // dword_1687424 = entity+496 (base g_EntityArray 0x1687234). The picker port already
    // uses these 3 offsets under the names kBodyTradeFlag/kBodyTradeA/kBodyTradeB
    // (World/TerrainPicker.cpp:118-120). Char_DrawNameplate's "admin" branch
    // (`(+122)==1` -> color 3 or 4 depending on `(+124)==1`, text `name + Str(72)`) is
    // therefore very likely the "TRADE IN PROGRESS" display, not an admin title.
    // NOT RENAMED here (rule "never guess a name"): the exact role of +492/+496 (partner
    // idHi/idLo? session counter?) is not proven, and Str(72) is not resolved (language
    // table not decrypted). To be settled by a future RE pass on the trade screen.
    bool isAdminTitle             = false; // +488 : (+122)==1  — == g_TradePartnerIdLo (cf. above)
    bool isAdminTitleAlt          = false; // +496 : (+124)==1  — == dword_1687424 (only meaningful if isAdminTitle)
    bool isWhisperTarget          = false; // +500 : (+125)==1
    bool hasAltWhisperColor       = false; // +748 : (+187)!=0
    bool isVipOrHighlighted       = false; // +544 (word) : (+272)==1
    bool isGmAccount              = false; // +28  : (+7)==1
    bool isSpecialPkState         = false; // +244 : (+61)==12

    // --- HP / MP (bars above the head, "selected target" mode only) ---
    int hpCur = 0, hpMax = 0; // +316 / +312
    int mpCur = 0, mpMax = 0; // +324 / +320

    // --- status icons (0 = none, otherwise "active" ; order faithful ; real sprite = Gfx TODO) ---
    int statusIconA = 0; // +392 (unk_95027C)
    int statusIconB = 0; // +404 (unk_96135C)
    int statusIconC = 0; // +408 (unk_9613F0)
    int statusIconD = 0; // +428 (unk_961484)
    int statusIconE = 0; // +436 (unk_961518)
    int statusIconF = 0; // +440 (reuses sprite unk_961484)
};

// ---------------------------------------------------------------------------
// "Local player + options" context needed by Char_DrawNameplate (globals outside `this`).
// ---------------------------------------------------------------------------
struct NameplateViewerContext {
    // NB: g_LocalElement 0x1673194 is NOT read directly by Char_DrawNameplate (verified
    // against the disassembly `refs` — absent); it only appears indirectly via
    // Combat_CanTargetOnMap (cf. NameplateHost::CanTargetOnMap, up to the host implementation
    // to capture it if needed). No field here to avoid a false dependency.
    int localMorphNpcId    = 0; // g_SelfMorphNpcId 0x1675A98 (drives the "market"/special-zone mode)
    int localGmAuthLevel   = 0; // g_GmAuthLevel 0x1669294
    int localFactionSubMode = 0; // dword_16760F4 (display sub-mode when morph==324)
    int localFactionFlag    = 0; // dword_1687320[0] (same field as NpcQuestContext::factionFlag)

    float selfX = 0.0f, selfY = 0.0f, selfZ = 0.0f; // g_World.Self() position (Math_Dist3D with flt_1687330)

    bool  optShowNameplates    = false; // g_Opt_ShowNameplates 0x84DED4 (UI option "showNameplates")
    bool  optShowHitMarkers    = false; // g_Opt_ShowHitMarkers 0x84DED0 (UI option "showHitMarkers")
    bool  optDetailedNameplates = false; // dword_1668F64==1 (title/affiliation/icons/debug block)

    int   chatColorWhisper      = 0; // g_ChatColor_Whisper 0x84DFDC (literal color for altWhisperName)
    int   partyFlagIndicatorCount = 0; // dword_1675804 (counter ; extra icon if >0 && notSelf)
};

// A text line + color (main name, title/affiliation, whisper, GM debug…).
struct NameplateLine {
    std::string text;
    int color = kNameColorNeutral;
};

// HP/MP bar above the head — Char_DrawNameplate @0x56F029..@0x56F10A.
//
// WARNING DEAD IN THE BINARY: this block is gated by `a2 == 1 && !a3 && g_Opt_ShowNameplates
// == 1` (@0x56EFBF) and `a2 == 1` never happens (cf. §DRAWMODE at the top of the file).
// `atlasFrame` is modeled for FORMULA FIDELITY (documenting exactly what the binary would
// compute), NOT to be rendered: no caller should draw these bars while dword_1668F64 stays
// unwritten — that would add to the screen something the original client never displays.
//
// EXACT formula (transcribed instruction by instruction, W9):
//   HP @0x56F029 : `if (*((int*)this + 79) <= 0) v120 = 1375;`                ("empty" frame)
//                  else `Crt_ftol((double)*((int*)this+79) * 48.0 / (double)*((int*)this+78))`
//                  then `v120 = v4 + 1376; if (v120 > 1424) v120 = 1424;`     @0x56F049..@0x56F05F
//                  draw `Sprite2D_Draw(&g_AssetMgr_UiAtlasSlots + 148*v120, x - 44, y)` @0x56F08F
//   MP @0x56F0A1 : empty = 1425 ; else `ftol(*(this+81) * 48.0 / *(this+80)) + 1426`,
//                  clamp 1474 ; draw at `(x - 44, y + 9)`                     @0x56F10A
// -> 49 frames per bar (HP 1376..1424, MP 1426..1474), ratio TRUNCATED TOWARD ZERO
// (Crt_ftol 0x760810, cf. Ftol() in the .cpp). Atlas stride 148 (0x94), consistent with
// `imul ecx, 94h` @0x530F43. Offsets: hpCur=idx79 (+316), hpMax=idx78 (+312),
// mpCur=idx81 (+324), mpMax=idx80 (+320) — matching NameplateActor above.
struct NameplateBar {
    bool visible = false;
    int current = 0;
    int max = 0;
    // Frame index in the UI atlas (g_AssetMgr_UiAtlasSlots 0x8E8B50, stride 148 bytes).
    // Default 1375 = "empty HP bar" frame (the MP bar's default would be 1425; the value
    // is overwritten by ComputeNameplateInfo as soon as the bar is computed).
    int atlasFrame = 1375;
};

// HP/MP bar atlas bounds (binary literals, cf. NameplateBar banner).
constexpr int kHpBarFrameEmpty = 1375; // @0x56F029
constexpr int kHpBarFrameBase  = 1376; // @0x56F049
constexpr int kHpBarFrameMax   = 1424; // @0x56F05F (clamp)
constexpr int kMpBarFrameEmpty = 1425; // @0x56F0A1
constexpr int kMpBarFrameBase  = 1426;
constexpr int kMpBarFrameMax   = 1474; // clamp
constexpr double kBarFrameSpan = 48.0; // `* 48.0` in both ratios

// Status icon identity (order faithful to the binary; real sprite = Gfx TODO). `A`..`E`/`F`
// correspond to NameplateActor's statusIconA..F fields; `PartyFlag` = condition
// `!notSelf && partyFlagIndicatorCount>0` (not a direct entity field).
enum class NameplateIconSlot { A, B, C, D, E, PartyFlag, F };

// Full decision for a nameplate — consumed by the Gfx layer (out of scope).
struct NameplateInfo {
    bool visible = false; // global gate (active + identity + outside the click zone)

    // Y offset (world) of the name label's anchor point, faithful to the original's
    // 31.0/21.0 selection (position ONLY — no screen projection here, cf. Gfx TODO).
    float labelAnchorYOffset = 21.0f;

    NameplateBar hpBar; // visible only if drawMode==1 && !notSelf && option enabled
    NameplateBar mpBar;

    bool nameBlockVisible = false; // false if outside the gate a2!=1||(hitmarkers&&(gm||<=300))
    NameplateLine mainLine;        // main name (tier prefix + real name OR hidden element name)

    // Variant icon (0/1) used only by the market group 270..274 to choose between 2 sprites
    // (unk_8E9DD0/unk_8E9E64) — selection data, NOT the sprite itself.
    int nameIconVariant = 0;

    // Lines/data of the "detailed" block (dword_1668F64==1): title/affiliation, whisper,
    // GM debug overlay. Order faithful to the original.
    std::vector<NameplateLine> extraLines;

    // ACTIVE status icons only, in binary-faithful order (real sprite = Gfx TODO).
    std::vector<NameplateIconSlot> statusIcons;
};

// ---------------------------------------------------------------------------
// External dependencies not modeled in this mission's shared headers (localized string
// resolution StrTable005, mouse click-marker, social identity). Optional callbacks; default
// behavior documented at the call site — NONE of these callbacks perform D3D rendering.
// ---------------------------------------------------------------------------
struct NameplateHost {
    // Target_IsBeyondClickRange 0x5410D0 — WARNING MISLEADING ORIGINAL NAME: this is NOT a
    // distance to a "last mouse click point". It's the CAMERA NEAR-CULL, identical to
    // Char_Draw's. Full decompile (re-verified Pass 4 / W9):
    //   BOOL __stdcall Target_IsBeyondClickRange(float *a1, float a2)
    //   { v5 = (flt_800138 - a1[2])*(flt_800138 - a1[2])
    //        + (flt_800134 - (a2*0.5 + a1[1]))*(flt_800134 - (a2*0.5 + a1[1]))
    //        + (g_CameraPos - *a1)*(g_CameraPos - *a1);
    //     return Math_Sqrt_0(v5) >= 10.0; }                       /*0x54113B..0x54116B*/
    // The reference triplet is (g_CameraPos 0x800130, flt_800134, flt_800138) = the CAMERA
    // POSITION — SAME reference as DrawCullContext::cameraPos (Game/EntityDrawLogic.h:151).
    // SOURCE OF THE PREVIOUS ERROR IDENTIFIED: the IDB's header comment itself says
    // "[game] distance from world click point (flt_800130..)" while the SYMBOL for this
    // same global is named g_CameraPos; the previous wording of this banner had copied the
    // misleading comment rather than the symbol.
    //
    // RENAMED (W9) `IsBeyondClickRange` -> `IsBeyondCameraNearCull`: no other file
    // referenced this member (World/TerrainPicker.h:199 only cites NameplateHost in a
    // comment).
    //
    // PRESERVED CALL CONVENTION: the caller already passes y offset by +10 — faithful to
    // the original call @0x56EF96 `Target_IsBeyondClickRange((float*)this+63 /* = pos */,
    // 20.0)`, i.e. a1[1] + a2*0.5 = y + 10.
    // NOW WIRED (W9) to game::IsBeyondCameraNearCull (Game/EntityDrawLogic.cpp:22, same
    // formula) by Scene/WorldRenderer.cpp::drawNameplatePass(). Default (unwired): true (no
    // culling) — kept for callers that don't have a camera on hand.
    std::function<bool(float x, float y, float z)> IsBeyondCameraNearCull;

    // Cam_ProjectToScreen 0x6A24F0: does the point project onto the screen? Gates the entire
    // "name" block (but NOT the HP/MP bars, which have their own call). Out of scope (pure
    // Gfx); default (unwired): always on-screen (true).
    std::function<bool(float x, float y, float z)> IsOnScreen;

    // StrTable005_Get(g_LangId, id) 0x4C1D10: resolves a localized string id. Out of scope
    // (string table, asset). Default (unwired): empty string.
    std::function<std::string(int stringId)> ResolveString;

    // UI_GetValueTierString 0x54CD40: "tier" prefix from rankTierValue (thresholds
    // 100/300/600/1000/1500/2100/3000 -> string ids 2077..2083, "" otherwise). Wraps
    // ResolveString + the thresholds (full resolution in one call to simplify the caller).
    // Default (unwired): "" (no prefix).
    std::function<std::string(int rankTierValue)> ResolveValueTierPrefix;

    // Item_GetEnchantNameString 0x548330(grade+1, rawValue): weapon enchant label. Out of
    // scope (Item table). Default (unwired): "".
    std::function<std::string(int grade, int rawValue)> ResolveEnchantName;

    // Combat_CanTargetOnMap 0x558740(g_LocalPlayerSheet, element, pkLevel, affiliationName):
    // true if the entity is a legitimate enemy target on the current map/zone (depends on
    // the zone PVP mode Map_GetPvpMode + special morph, cf. banner). Out of scope (PVP zone
    // system). Default (unwired): false (treats everyone as "ally/non-targetable",
    // conservative — never hides a name by mistake).
    std::function<bool(int element, int pkLevel, const std::string& affiliationName)> CanTargetOnMap;

    // Affiliation equality with the local player (cf. uncertain-semantics note at the top of
    // the file — sentinel differs by context: byte_1686138 in market mode morph==291,
    // unk_16746A8 in the normal branch). Default (unwired): false.
    std::function<bool(const std::string& affiliationName)> IsSameAffiliationAsLocal;

    // Alliance equality with the local player (sentinel g_AllianceRosterNames). Default
    // (unwired): false.
    // NB: Char_GetPairedElement 0x557C00 is NOT called by Char_DrawNameplate (absent from
    // its disassembly `refs`, unlike Npc_GetNameplateColor) -> no dedicated callback here;
    // to be captured by the caller in CanTargetOnMap/IsSameAffiliationAsLocal if a future RE
    // pass on the social layer needs it.
    std::function<bool(const std::string& allianceName)> IsSameAllianceAsLocal;
};

// ===========================================================================
// Char_DrawNameplate 0x56EF40 — pure logic (no D3D rendering). `drawMode`/`notSelf` = original
// a2/a3 (a4 confirmed never read, removed from the API — cf. header banner).
// ===========================================================================
NameplateInfo ComputeNameplateInfo(const NameplateActor& actor,
                                    int drawMode,
                                    bool notSelf,
                                    const NameplateViewerContext& ctx,
                                    const NameplateHost& host);

} // namespace ts2::game

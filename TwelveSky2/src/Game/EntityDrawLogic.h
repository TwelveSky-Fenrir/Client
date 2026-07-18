// Game/EntityDrawLogic.h — decision logic for the entity render dispatcher.
//
// Clean C++ rewrite (not byte-exact — per scope rule: pixel-exact 3D rendering
// is out of fidelity scope, only gameplay FORMULAS/conditions are) of the 5
// functions identified in the disassembly:
//   Char_Draw             0x5805C0  (~0x6F1 bytes)  — main per-entity dispatcher
//   Char_DrawShadow        0x580CE0  (~0x3A4 bytes)  — shadow pass
//   Char_DrawReflection    0x581090  (~0x3A4 bytes)  — reflection pass (mirror/water)
//   Char_DrawOverheadName  0x581440  (~0xA7 bytes)   — floating "clickable" number/text
//   Char_DrawNameTag       0x583470  (~0x133 bytes)  — name(+level) above the head
//
// OUT OF SCOPE (mentioned here for traceability only, NOT decompiled — too
// large, pure visual effects, per scope rule):
//   Char_DrawWeaponTrailEffect      0x55E9D0 (~0x9F7A bytes, ~40 KB)
//   Char_DrawWeaponEffectVariantA   0x568FE0 (~0x2AFF bytes, ~11 KB)
//   Char_DrawWeaponEffectVariantB   0x56BF90 (~0x2AFF bytes, ~11 KB)
//   // TODO(rendering): weapon trails — shaders/particles, to be wired later
//   // in the Gfx layer with no decision logic here.
//
// THIS FILE PERFORMS NO D3D RENDERING: no VB/IB/shader/texture. It exposes PURE
// functions (deterministic, stateless, no I/O) that turn an entity snapshot +
// camera context into typed DECISIONS (what to draw, at what distance
// threshold, in what order). The Gfx layer (future EntityRenderer under Gfx/)
// will consume these decisions to emit the actual D3D9 calls (original
// ModelObj_Draw / SObject_DrawEx / SObject_DrawAnimated[2]).
//
// IMPORTANT — the `this` object of the 5 original functions is NOT
// game::PlayerEntity / MonsterEntity / NpcEntity from GameState.h (those are
// the network records of the entity arrays). It is a separate 3D render object
// (style "cCharObj"), allocated/refreshed by the GXD engine from those
// records, which additionally carries interpolated position, attach state
// (mount), anim scales, and attached-effect flags. Its fields are
// reconstructed below as EntityRenderState with the original offsets (in
// bytes, relative to `this`) — it is the Gfx layer's job to populate this view
// from its actual internal object.
//
// Convention: "+0xNN" comments cite the original offset; "(this+N)" cites the
// form as seen in the Hex-Rays pseudocode (this+N in 4-byte units, whether
// (_DWORD*)this+N or (float*)this+N).
#pragma once
#include <cstdint>
#include <string>

namespace ts2::game {

// Minimal 3D vector, independent of the render engine (no D3DX dependency here
// to keep this module testable/pure — the Gfx layer will convert to D3DXVECTOR3).
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Full 3D Euclidean distance (Math_Dist3D 0x53FAA0).
float Distance3D(const Vec3& a, const Vec3& b);

// Gate for "near camera" culling (Target_IsBeyondClickRange 0x5410D0).
// Misleading original name: this function is used BOTH as a range test for
// mouse-click target picking AND, reused as-is here, as a "not too close/behind
// the camera" render guard. Exact formula identified:
//   dx = cameraPos.x - pos.x
//   dy = cameraPos.y - (pos.y + radius*0.5)   <- vertical offset to model center
//   dz = cameraPos.z - pos.z
//   returns sqrt(dx²+dy²+dz²) >= 10.0
// `radius` = info.drawSize (see EntityRenderState::Info::drawSize) converted to
// float by the caller, as in the original disassembly.
bool IsBeyondCameraNearCull(const Vec3& pos, float radius, const Vec3& cameraPos);

// Shared "info" block (this+24 as dword*, i.e. +96 bytes) describing the
// current model/costume and its render metadata. Referenced in common by
// the 5 functions.
struct EntityRenderInfo {
    int modelCategoryId     = 0; // +0x00: current costume/model id.
                                  //   [589..600]  = range reserved for tribe skill morphs
                                  //   {1141..1144} = "special weapon variant" range (alive/dead)
    int weaponRenderType    = 0; // +0xE8 (+232): 2 = weapon with wear stages animated by HP ratio
    int motionIndex         = 0; // +0xF4 (+244): model/motion index, indexes asset tables
    int drawSize            = 0; // +0xFC (+252): integer size/radius — used (a) converted to float
                                  //   as the radius for IsBeyondCameraNearCull and (b) summed as raw
                                  //   int with nameplateExtraOffset for the floating-name height
    int nameplateExtraOffset = 0; // +0x104 (+260): additional vertical offset for the floating name
    int maxHp                = 0; // +0x170 (+368): max HP, denominator of the weapon wear ratio
};

// Entity render object (`this` of Char_Draw / Char_DrawShadow / Char_DrawReflection
// / Char_DrawOverheadName — the first 4 functions share EXACTLY this layout,
// verified offset by offset in the pseudocode). Char_DrawNameTag uses a
// DIFFERENT object (see NameTagRenderState below).
struct EntityRenderState {
    bool  active = false;    // +0x00: live slot (this[0] != 0); guard common to the whole dispatcher

    Vec3  pos{};              // +0x20..+0x28 (this+8..+10): current world position
    // Rotation fix (2026-07-14): this+7 = animFrame (move-state+8, not an angle); this+14 =
    // heading (move-state+36, degrees, injected as Y rotation; scale hardcoded {1,1,1} in
    // SObject_DrawEx 0x4D9330) — proven via Char_Draw 0x5805C0 + Model_Render 0x40EBB0
    // ("compose S*Rz*Ry*Rx*T") and Game/GameState.h::MonsterEntity::heading. Field names below
    // are INVERTED vs. reality but kept unchanged; WorldRenderer.cpp already populates them
    // correctly (facingOrAnimTimer <- heading, not a timer). scaleY stays 0.0f (unused),
    // falling back to scale 1.0 in WorldRenderer::renderOne with no visible effect.
    float facingOrAnimTimer = 0.0f; // +0x1C (this+7 actually): see note above — used as heading (degrees), not a timer
    float scaleY = 0.0f;      // +0x38 (this+14 actually): see note above — NOT the vertical scale; never populated (falls back to 1.0)

    int   hp = 0;             // +0x5C (this+23): current HP — drives the alive/dead weapon variant
                               //   and the wear stage (see ComputeWeaponOverlayVariant)
    int   stateCategory = 0;  // +0x18 (this+6): state category code -> associated overlay (see
                               //   ClassifyStateOverlay); NOT the same field as weaponRenderType

    bool  attached = false;   // +0xD4 (this+53): mounted/attached to a parent (mount, carried effect...)
    Vec3  attachPos{};        // +0xF0..+0xF8 (this+60..+62): relative position if attached
    float attachScale = 0.0f; // +0xFC (this+63): scale/value if attached

    // 3 fixed attached-effect slots (aura/wing/mount...), each with an active
    // flag + a scale. Draw order = declaration order below.
    bool  effectSlot1Active = false; float effectSlot1Scale = 0.0f; // +0x100/+0x104 (this+64/65) -> fixed model #1
    bool  effectSlot2Active = false; float effectSlot2Scale = 0.0f; // +0x108/+0x10C (this+66/67) -> fixed model #2
    bool  effectSlot3Active = false; float effectSlot3Scale = 0.0f; // +0x110/+0x114 (this+68/69) -> fixed model #3

    const EntityRenderInfo* info = nullptr; // +0x60 (this+24, dword*): shared info block (may be null)
};

// "Name tag" object (`this` of Char_DrawNameTag 0x583470) — structure DISTINCT
// from EntityRenderState (different offsets: pos at +128 not +32, "owner"
// pointer at +100 not +96, no IsBeyondCameraNearCull test here).
struct NameTagRenderState {
    bool active = false;  // +0x00
    Vec3 pos{};            // +0x80..+0x88 (this+128/132/136): name position (before Y offset)
    int  level = 0;        // +0x14 (this+20): level shown in parentheses

    struct OwnerInfo {
        int nameDisplayMode = 0; // +0xBC (+188): 1 or 2 => shows "name(level)", else "name" only
        // +0x04: C-string pointer to the name (resolved by the caller, not modeled here)
    };
    const OwnerInfo* owner = nullptr; // +0x64 (this+100)
    std::string ownerName; // name already resolved by the caller (owner+4 in the original)
};

// Culling context shared by the current frame.
struct DrawCullContext {
    Vec3 cameraPos{};      // g_CameraPos (0x800130, x/y/z vector): used by IsBeyondCameraNearCull
    Vec3 localPlayerPos{}; // flt_1687330: local player position — used by the "300 units" cull
                            //   (Char_DrawShadow / Char_DrawReflection / Char_DrawNameTag)
};

inline constexpr float kSelfProximityDrawDistance = 300.0f; // hard threshold identified in the 3 functions concerned

// Visibility decisions (one per render pass). `drawPass` = original a2
// parameter of Char_Draw, forwarded as-is to the future renderer (1 or 2 =
// valid passes; anything else -> no render). The other 4 original functions
// ignore this parameter (a2/a3 present in their signature but never read).
struct EntityDrawFlags {
    bool showBody       = false; // Char_Draw: body + weapon + state overlays
    bool showShadow     = false; // Char_DrawShadow
    bool showReflection = false; // Char_DrawReflection
    bool showOverheadName = false; // Char_DrawOverheadName (floating "clickable" number/text)
    bool showNameTag    = false; // Char_DrawNameTag — computed separately (different object),
                                  // exposed here only as a convenient aggregate for the caller.
};

// Computes showBody / showShadow / showReflection / showOverheadName from the
// SAME EntityRenderState object (the 4 original functions share this layout).
// showNameTag stays false here: see ComputeNameTagContent (different object).
//   showBody       = active && drawPass∈[1,2] && IsBeyondCameraNearCull(pos, info.drawSize, cam)
//   showShadow     = active && Distance3D(pos, self) <= 300 && IsBeyondCameraNearCull(...)
//   showReflection = same as showShadow (same guards, different draw)
//   showOverheadName = active && IsBeyondCameraNearCull(pos, info.drawSize, cam)  [no 300 cull]
EntityDrawFlags ComputeEntityDrawFlags(const EntityRenderState& state,
                                        const DrawCullContext& cull,
                                        int drawPass);

// Char_DrawOverheadName 0x581440 — position of the floating text/number.
// v7 = { pos.x, (info.drawSize + info.nameplateExtraOffset + 1) + pos.y, pos.z }
// FIDELITY NOTE: drawSize and nameplateExtraOffset are summed as INTEGERS (not
// individually converted to float) before being added to pos.y — see original
// pseudocode line 0x5814ae.
struct OverheadNameContent {
    bool visible = false;
    Vec3 worldPos{};
};
OverheadNameContent ComputeOverheadNameContent(const EntityRenderState& state, const DrawCullContext& cull);

// Char_DrawNameTag 0x583470 — gate + "%s(%d)"/"%s" formatting + position.
//
// ///// DEAD CODE — DO NOT WIRE (Passe 4 / wave W9, nameplate-entity front) /////
// xrefs_to(0x583470) = 1 site, @0x52FCD9, inside `dword_1668F64 == 1` in Scene_InGameRender
// 0x52D0B0 (guard @0x52FC09 skips 0x52FC16..0x52FD3A). dword_1668F64 is NEVER written:
// find_bytes('64 8F 66 01') = 4 hits (0x52FB90/0x52FB99/0x52FC0B/0x570088, all `cmp` operands);
// xrefs_to(0x1668F64) = 4 xrefs, all READS (0x52FB8E/0x52FB97/0x52FC09/0x570086); def
// `dword_1668F64 dd 0`. Zero writes => Char_DrawNameTag is NEVER called by the original client.
//
// Kept below (faithful, see .cpp) but INTENTIONALLY UNCALLED — wiring it would show a label
// the original never displays. W9 gap sheet (HUD-NP-04) proposed wiring it to "an
// unconditional loop over ground objects": REJECTED — (1) path is dead (above); (2) "ground
// objects" misidentifies @0x52FCAE, which iterates `dword_17AB534` (stride 0x98=152, counter
// dword_1687228); World_PickEntityAtCursor 0x538AB0 puts this array in category 6, which has
// NO label draw (switch @0x530FC7).
struct NameTagContent {
    bool visible = false;
    bool showLevelSuffix = false; // true => "%s(%d)" format (name, level); false => "%s" (name only)
    Vec3 worldPos{};               // pos + (0, 2.5, 0)
};
NameTagContent ComputeNameTagContent(const NameTagRenderState& tag, const Vec3& localPlayerPos);

// Quest_GetMarkerSpriteBase 0x540770 — STUB in the binary.
// Full decompilation (re-verified Passe 4 / W9):
//     int __stdcall Quest_GetMarkerSpriteBase(int a1) { return 10; }  /*0x54077C*/
// The argument (`**(_DWORD**)this` at call site @0x580372, i.e. `npcDef->id`) is NEVER
// read. Kept as a function rather than a hardcoded `10` at the call site to keep the
// anchor visible in case the stub ever stops being one.
constexpr int kQuestMarkerSpriteBase = 10;
inline int Quest_GetMarkerSpriteBase(int /*npcDefId — never read, cf. 0x54077C*/) {
    return kQuestMarkerSpriteBase;
}

// Fx_MeleeSwingDrawMarker 0x5802C0 — NPC NAME LABEL (render pool g_NpcRenderArray
// 0x1764D14, stride 88 bytes / 22 dw).
//
// ⚠️ MISNAMED IN THE IDB: this is neither an "attack trail" nor a swing billboard.
// Full decompilation (Passe 4 / W9) — it is the NPC counterpart of
// Char_DrawOverheadName (monsters) and Char_DrawNameplate (players):
//     if (*((_DWORD*)this + 1)) {                                    /*0x5802CC : active slot*/
//       v5 = (float)*(int*)(*(_DWORD*)this + 1332);                  /*0x5802E3 : def+1332*/
//       if (Target_IsBeyondClickRange(this + 5, v5)) {               /*0x5802F2 : camera near-cull*/
//         if (a2 != 1 || g_Opt_ShowHitMarkers && Math_Dist3D(this+5, flt_1687330) <= 300.0) { /*0x580332*/
//           v7[0] = *(this + 5);                                     /*0x58033C : pos.x (byte +20)*/
//           v7[1] = (double)(*(_DWORD*)(*(_DWORD*)this + 1332) + 1) + *(this + 6); /*0x580359*/
//           v7[2] = *(this + 7);                                     /*0x580362 : pos.z (byte +28)*/
//           MarkerSpriteBase = Quest_GetMarkerSpriteBase(**(_DWORD**)this);        /*0x580372*/
//           UI_DrawNumberCentered((const char*)(*(_DWORD*)this + 4), v7, MarkerSpriteBase); /*0x58038A*/
//         } } }
// `*(_DWORD*)this` = the slot's NpcDefRecord* -> text = def+4 (NpcDefRecord::name),
// radius/range = def+1332 (NpcDefRecord::fieldF[1], already RESOLVED in
// Game/ExtraDatabases.h:67-71 as "interaction/click range"), color = 10.
// FULL INTEGER sum `def[1332] + 1` before adding to pos.y (same convention as
// ComputeOverheadNameContent, see 0x5814AE) — NOT a float addition.
//
// CALL SITES (`xrefs_to(0x5802C0)` = 2):
//   @0x52FC72 — DEAD (inside the `dword_1668F64 == 1` block, see NameTagContent banner), a2=1
//   @0x531148 — LIVE: hover-switch category 4 @0x530FC7,                                  a2=2
// With a2=2 the guard @0x580332 short-circuits (`a2 != 1`): neither g_Opt_ShowHitMarkers nor the
// 300-unit cull affects the actually-taken path. Both parameters remain exposed below to stay
// faithful to the function (not just its sole caller).
struct ZoneNpcLabelRenderState {
    bool active       = false; // +4  (this[1]): slot occupied — dword_1764D18[22j]
    Vec3 pos{};                 // +20/+24/+28 (float* this+5/6/7): world position — unk_1764D28 + 22j
    int  clickRange   = 0;      // def+1332 (NpcDefRecord::fieldF[1]): near-cull radius AND label Y offset
    int  markerDefId  = 0;      // def+0 (NpcDefRecord::id): Quest_GetMarkerSpriteBase argument (never read)
};
struct ZoneNpcLabelContent {
    bool visible   = false;
    Vec3 worldPos{};                          // (pos.x, (clickRange + 1) + pos.y, pos.z)
    int  colorCode = kQuestMarkerSpriteBase;  // Quest_GetMarkerSpriteBase -> 10 (literal)
};
// `drawMode` = original a2 (2 at the sole live site); `optShowHitMarkers` = g_Opt_ShowHitMarkers
// 0x84DED0. The text (def->name) is NOT returned here: the caller reads it from the same
// NpcDefRecord (see Scene/WorldRenderer.cpp::drawNameplatePass).
ZoneNpcLabelContent ComputeZoneNpcLabelContent(const ZoneNpcLabelRenderState& npc,
                                                int drawMode,
                                                bool optShowHitMarkers,
                                                const DrawCullContext& cull);

// Main mesh placement (Char_Draw/Shadow/Reflection, branches on `attached`):
// when the entity is attached (mount/parent), the draw uses a fixed relative
// position/rotation (angle=0, pos=attachPos, scale=attachScale) instead of the
// entity's normal position/rotation/scale.
struct BodyMeshPlacement {
    bool  useAttachOffset = false;
    float angle = 0.0f;   // 0.0 if attached, else state.facingOrAnimTimer
    Vec3  pos{};          // attachPos if attached, else state.pos
    float scale = 0.0f;   // attachScale if attached, else state.scaleY
};
BodyMeshPlacement ComputeBodyMeshPlacement(const EntityRenderState& state);

// Selects the "weapon" overlay variant drawn right after the main body in
// Char_Draw (and its equivalent in Shadow/Reflection).
//   - kAliveDeadSpecial: modelCategoryId ∈ {1141,1142,1143,1144} AND
//     weaponRenderType != 2 -> "alive" (hp>0) or "dead" (hp<=0) variant
//   - kWearStage: weaponRenderType == 2 AND high shadow detail -> wear stage
//     0..3 computed from the HP/maxHP ratio (100%->0 pristine, 0%->3 worst state)
//   - kDefaultStage0: all other cases (fixed stage 0)
// `highDetailShadows` corresponds to global g_Opt_GfxDetailShadows == 1, passed
// as a parameter to keep this function pure (graphics option, no global access).
struct WeaponOverlayDecision {
    enum class Kind { kAliveDeadSpecial, kWearStage, kDefaultStage0 } kind = Kind::kDefaultStage0;
    bool aliveVariant   = false; // valid only if kind == kAliveDeadSpecial
    int  wearStageIndex = 0;     // valid only if kind == kWearStage; 0..3 in normal usage
                                  // (unbounded in the original: bogus hp/maxHp -> outside [0,3])
};
WeaponOverlayDecision ComputeWeaponOverlayVariant(const EntityRenderState& state, bool highDetailShadows);

// "Tribe special morph" branch (Char_Draw, very start — 0x580617..0x58074A).
// Taken only when modelCategoryId ∈ [589,600] (NPC costumes reserved for tribe
// skill morphs) AND the local player is currently morphed (valid morph skill
// index). External table resolutions (TribeSkill_SkillIdToIndex, dword_184C218,
// Item_MapUpgradeIconId) remain the Skill/Item system's responsibility — they
// are injected here as already-resolved parameters to keep this function pure.
struct TribeMorphOverlayDecision {
    bool inMorphIdRange = false; // modelCategoryId ∈ [589,600]
    bool active         = false; // + valid morph skill index -> branch taken

    int  itemVariant  = 0; // v10 = table[idx] % 100
    int  upgradeLevel = 0; // v11 = table[idx] / 100

    enum class Overlay { kNone, kUpgradeIcon, kClassOverlay } overlay = Overlay::kNone;

    // true => Char_Draw returns IMMEDIATELY after this pass: the normal body
    // and the weapon overlay are NOT drawn at all for this frame.
    bool skipNormalBodyDraw = false;
};
// `selfMorphSkillIndex` = TribeSkill_SkillIdToIndex(g_SelfMorphNpcId), -1 if none.
// `morphTableValue` = dword_184C218[selfMorphSkillIndex] if the index is valid (0 otherwise).
TribeMorphOverlayDecision ComputeTribeMorphOverlay(const EntityRenderState& state,
                                                    int selfMorphSkillIndex,
                                                    int morphTableValue);

// After the Gfx layer has attempted Item_MapUpgradeIconId(itemVariant, upgradeLevel)
// and actually drawn it (valid icon, != -1), call this to know whether the anim
// timer must be reset to 0 (exact behavior: reset only if the icon was drawn AND
// facingOrAnimTimer had reached >= 41).
bool ShouldResetAnimTimerAfterUpgradeIconDraw(float facingOrAnimTimer);

// Offset vector used for all "attached at height" auras/overlays (morph, state
// overlay, 3 fixed effect slots): (0, scaleY, 0).
Vec3 ComputeAuraOffset(const EntityRenderState& state);

// Generic state overlay (switch on stateCategory, end of Char_Draw,
// 0x580a8f..0x580c0a). Actual id resolution (skill/class/item/special motion)
// belongs to the corresponding systems (Skill/Item/Anim) — modeled here only
// as classification + "should draw" decision.
enum class StateOverlayCategory {
    kNone         = -1,
    kSkillId      = 0,  // maybe_MapCharToSkillId(info.motionIndex)
    kClassId      = 5,  // maybe_MapClassToId(info.motionIndex)
    kItemVariant  = 7,  // Item_MapToVariantId(info.motionIndex)
    kSpecialMotion = 12, // Anim_MapSpecialMotion(info.motionIndex) (0xC)
};
// Classifies stateCategory into an overlay category (does NOT resolve the final id).
StateOverlayCategory ClassifyStateOverlay(const EntityRenderState& state);
// `resolvedOverlayId` = result of the resolver matching the category above
// (already called by the Skill/Item/Anim layer). Returns true if an overlay
// must be drawn (resolved id != -1).
bool ShouldDrawStateOverlay(int resolvedOverlayId);

// 3 fixed attached-effect slots (end of Char_Draw, 0x580c12..0x580ca6). Simple
// flag passthrough, but documents the guaranteed draw order (slot1 then slot2
// then slot3) and the associated fixed Gfx-side models:
//   slot1 -> unk_B63174, slot2 -> unk_B63208, slot3 -> unk_B5A180
//
// ///// WIRING STATUS — Passe 4 / wave W9 (gap erp-01), HONEST AND UNRESOLVED /////
// NO CALLER to date (exhaustive grep of src/: this function only appears at its
// definition in Game/EntityDrawLogic.cpp and this declaration). RE-PROVEN FINDING; the
// wiring is BLOCKED, not forgotten — and the blocker is outside this front's files:
//   Offsets re-verified instruction-by-instruction in Char_Draw 0x5805C0 (W9):
//     580C12  cmp dword ptr [edx+100h], 0     ; slot1 active   (this+256)
//     580C2C  fld dword ptr [edx+104h]        ; slot1 float arg (this+260)
//     580C39  mov ecx, offset unk_B63174      ; slot1 template
//     580C3E  call ModelObj_Draw
//     580C46  cmp dword ptr [ecx+108h], 0     ; slot2 active   (this+264)
//     580C60  fld dword ptr [ecx+10Ch]        ; slot2 float arg (this+268)
//   -> slot3: +0x110 / +0x114. The call form is ModelObj_Draw(template, pass, <float>,
//      this+0x20 /*pos*/, &var, 0), IDENTICAL to Char_DrawAura's (ModelObj_Draw(unk_B60AB8
//      + 148*this[27], pass, 0.0, this+32, this+35)): the float is thus the 2nd POSITIONAL
//      ARGUMENT (animation cursor), NOT a scale — the `slotNScale` name below is therefore
//      suspect, kept as-is for lack of direct proof of its exact role (rule "never guess").
//      Rename to fix once ModelObj_Draw is ported.
//   BLOCKER: ModelObj_Draw 0x4D71B0 and the template tables unk_B63174 / unk_B63208 /
//   unk_B5A180 are NOT modeled in ClientSource (no model-object system), and the
//   effectSlotNActive/Scale fields are populated by nobody (their real producer is
//   Char_UpdateMotionState 0x5816A0 @0x581761/@0x581D57, unported quest logic). Emitting a
//   draw here would require INVENTING the model-object API -> forbidden (rule #8).
struct AttachedEffectSlots {
    bool slot1 = false; float slot1Scale = 0.0f;
    bool slot2 = false; float slot2Scale = 0.0f;
    bool slot3 = false; float slot3Scale = 0.0f;
};
AttachedEffectSlots ComputeAttachedEffectSlots(const EntityRenderState& state);

} // namespace ts2::game

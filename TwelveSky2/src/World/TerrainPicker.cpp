// World/TerrainPicker.cpp — screen -> world picking (see the banner in World/TerrainPicker.h
// for the full map of IDA anchors and proofs).

#include "World/TerrainPicker.h"

// LOAD-BEARING INCLUSION ORDER (project convention: <winsock2.h> BEFORE
// <windows.h>). Game/AutoTargetCombatGate.h transitively pulls in Game/ComboPickupTick.h ->
// Net/NetClient.h, which does `#include <winsock2.h>` THEN `#include <windows.h>` (NetClient.h
// :19-20). Gfx/Camera.h, on the other hand, includes plain <windows.h> (Camera.h:29). This block MUST
// therefore stay BEFORE the Gfx/Camera.h include, otherwise windows.h would be seen first and the
// Winsock redefinitions would break the build.
#include "Game/AutoTargetCombatGate.h" // Combat_IsTargetablePlayerState/MonsterState,
                                       // AutoTarget_PlayerRecordPopulated/MonsterActionState

#include "Gfx/Camera.h"          // gfx::Camera + d3dx9 (BuildScreenPickCamera)
#include "Game/GameDatabase.h"   // game::MonsterInfo (def+232 / +248..+260)
#include "Game/ExtraDatabases.h" // game::NpcDefRecord (def+1328..+1336)

#include <cmath>
#include <cstring>

namespace ts2::world {
namespace {

// Math_Dist3D 0x53FAA0 — full 3D Euclidean distance between two vec3.
float Dist3D(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Cam_ScreenRayVsAABB 0x6A0670 — builds the screen ray from camera parameters then
// tests the segment against the AABB. PROVEN strictly equivalent to the composition below
// (cf. §0 of the .h banner): @0x6A0682..0x6A0725 == collision::BuildScreenRay (identical formula and
// offsets), @0x6A0746 == Collide_SegmentAABB 0x69FB20 == collision::SegmentAABB.
bool ScreenRayVsAABB(const collision::ScreenPickCamera& cam, int sx, int sy,
                     const float bmin[3], const float bmax[3]) {
    float origin[3], dir[3];
    collision::BuildScreenRay(cam, sx, sy, origin, dir); // 0x6A0682..0x6A0725
    return collision::SegmentAABB(origin, dir, bmin, bmax); // 0x6A0746
}

// Scene_RayHitPlayerBox 0x5415E0 — AABB [x±4.5, y..y+20, z±4.5] around a1[63..65]
// (== record+252/256/260 == PlayerEntity::x/y/z).
bool RayHitPlayerBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                     const game::PlayerEntity& p) {
    const float bmin[3] = { p.x - 4.5f, p.y,         p.z - 4.5f }; // 0x5415FE/0x54160A/0x54161C
    const float bmax[3] = { p.x + 4.5f, p.y + 20.0f, p.z + 4.5f }; // 0x54162E/0x541640/0x541652
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);               // 0x54166F
}

// Scene_RayHitNodeBox 0x541920 — AABB [x±4.5, y..y+11, z±4.5] around a1[6..8]
// (== record+24/28/32 == the first 3 floats of ZoneObjectEntity::body).
bool RayHitNodeBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                   const float pos[3]) {
    const float bmin[3] = { pos[0] - 4.5f, pos[1],         pos[2] - 4.5f }; // 0x54193B/0x541944/0x541953
    const float bmax[3] = { pos[0] + 4.5f, pos[1] + 11.0f, pos[2] + 4.5f }; // 0x541962/0x541971/0x541980
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);                        // 0x54199D
}

// Scene_RayHitNpcBox 0x541680 — AABB sized from the mNPC record: w = def+1328,
// h = def+1332, d = def+1336 (== NpcDefRecord::fieldF[0..2]). The binary reads these fields as
// `*(int*)` then promotes them to double before multiplying -> reproduced as-is.
bool RayHitNpcBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                  const game::NpcRenderEntry& n) {
    if (!n.def) return false; // the binary dereferences *(_DWORD*)a1 without a guard; here the slot
                              // may carry a null def (cf. GameState.h:303-306) -> treated as no hit.
    const double w = static_cast<double>(static_cast<int32_t>(n.def->fieldF[0])); // +1328
    const double h = static_cast<double>(static_cast<int32_t>(n.def->fieldF[1])); // +1332
    const double d = static_cast<double>(static_cast<int32_t>(n.def->fieldF[2])); // +1336
    const float bmin[3] = { static_cast<float>(n.x - w * 0.5), // 0x5416A6
                            n.y,                                // 0x5416AF
                            static_cast<float>(n.z - d * 0.5) };// 0x5416C9
    const float bmax[3] = { static_cast<float>(w * 0.5 + n.x),  // 0x5416E3
                            static_cast<float>(h + n.y),        // 0x5416F7
                            static_cast<float>(d * 0.5 + n.z) };// 0x541711
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);            // 0x54172E
}

// Scene_RayHitMonsterBox 0x541780 — AABB sized from the MONSTER_INFO record:
// w = def+248, h = def+252, d = def+256, yOff = def+260 (== MonsterInfo::collDim[0..2] and
// ::field260). `MonsterEntity::def` IS a MonsterInfo* : Game/EntityManager.cpp:148
// (`ResolveMobDef` -> `reinterpret_cast<const uint8_t*>(GetMonsterInfo(id))`) then :523
// (`m->def = def`). Same int -> double promotion as the binary.
bool RayHitMonsterBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                      const game::MonsterEntity& m) {
    if (!m.def) return false; // null def -> no box (spawn is normally rejected without a def)
    const game::MonsterInfo* def = static_cast<const game::MonsterInfo*>(m.def);
    const double w    = static_cast<double>(def->collDim[0]); // +248
    const double h    = static_cast<double>(def->collDim[1]); // +252
    const double d    = static_cast<double>(def->collDim[2]); // +256
    const double yOff = static_cast<double>(static_cast<int32_t>(def->field260)); // +260
    const float bmin[3] = { static_cast<float>(m.x - w * 0.5),    // 0x5417A7
                            static_cast<float>(yOff + m.y),       // 0x5417BC
                            static_cast<float>(m.z - d * 0.5) };  // 0x5417D7
    const float bmax[3] = { static_cast<float>(w * 0.5 + m.x),    // 0x5417F2
                            static_cast<float>(h + m.y + yOff),   // 0x541813 (original order)
                            static_cast<float>(d * 0.5 + m.z) };  // 0x54182E
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);              // 0x54184B
}

// --- RECORD offsets (== slot base) of player fields read by 0x538AB0 -----------------------
// PlayerEntity::body starts at record+0x18 (24) -> body offset = record offset - 24. The
// record offsets below are those of Game/NameplateLogic.h:123/134/135, which models the
// SAME record (same derived addresses).
constexpr std::size_t kBodyAffiliation = 40  - 24; // byte_168725C  (record+40)  : affiliation name
constexpr std::size_t kBodyElement     = 88  - 24; // dword_168728C (record+88)  : element
constexpr std::size_t kBodyPkLevel     = 236 - 24; // dword_1687320 (record+236) : PK rank
// Bounds of the affiliation name: record+40 -> record+60 (next field, NameplateLogic.h:124).
constexpr std::size_t kAffiliationMaxLen = 60 - 40;
// Category-2 triplet (trade partner) — IDA names g_TradePartnerIdLo /
// dword_1687420 / dword_1687424, records +488/+492/+496.
// AMBIGUITY FLAGGED (unresolved, no effect here): Game/NameplateLogic.h:141 interprets
// this SAME record+488 as `isAdminTitle ((+122)==1)`. The two readings are incompatible,
// but the condition is reproduced LITERALLY (raw dword comparisons) — so the
// behavior is correct regardless of the field's real semantics.
constexpr std::size_t kBodyTradeFlag = 488 - 24;
constexpr std::size_t kBodyTradeA    = 492 - 24;
constexpr std::size_t kBodyTradeB    = 496 - 24;

int32_t ReadI32(const game::PlayerEntity& p, std::size_t bodyOff) {
    int32_t v = 0;
    std::memcpy(&v, p.body.data() + bodyOff, sizeof(v));
    return v;
}

// Bounded C string read from the body (the binary's fields are not guaranteed NUL-terminated).
std::string ReadCString(const game::PlayerEntity& p, std::size_t bodyOff, std::size_t maxLen) {
    const char* s = reinterpret_cast<const char*>(p.body.data()) + bodyOff;
    std::size_t n = 0;
    while (n < maxLen && s[n] != '\0') ++n;
    return std::string(s, n);
}

// Monster elemental filter @0x53905E..0x539083 — cf. §2 of the .h banner.
// def+232 in {10,11,12,13} -> K = def+232 - 10 ; targetable iff element != K && element != Paired(K).
bool MonsterPassesElementGate(const game::MonsterInfo& def, int localElement,
                              const game::ElementPairTable& pairs) {
    const int32_t cls = def.field232; // +232
    if (cls < 10 || cls > 13) return true; // outside {10..13} : no restriction
    const int k = static_cast<int>(cls) - 10;
    return localElement != k && localElement != pairs.Paired(k); // Char_GetPairedElement 0x557C00
}

// Current zone forbidding category 1 (@0x538CFD).
bool ZoneBlocksNeutralPlayer(int zoneId) {
    for (int z : kZonesBlockingNeutralPlayer)
        if (zoneId == z) return true;
    return false;
}

} // namespace

// ===========================================================================
// TerrainPicker — implementer of game::ITerrainPicker (G-PICK-06).
// ===========================================================================

bool TerrainPicker::IsPointBlocked(const float pos[3]) {
    return assets_->IsPointBlocked(pos); // World_IsPointBlocked 0x540DA0
}

bool TerrainPicker::PickRayScreen(int screenX, int screenY, CollisionSlot slot, bool twoSide,
                                   float outPos[3]) {
    uint32_t faceIndex = 0; // original *a4 (hit face index): unused by
                            // Skill_CanCastAtCursor, which only reads the 3D point (v7).
    return assets_->PickRayScreen(slot, cam_, screenX, screenY, faceIndex, outPos, twoSide);
}                                                                   // Terrain_PickRayScreen 0x699A80

// ===========================================================================
// BuildScreenPickCamera — cf. §0 of the .h banner.
// ===========================================================================

collision::ScreenPickCamera BuildScreenPickCamera(const gfx::Camera& camera,
                                                   int screenW, int screenH) {
    collision::ScreenPickCamera cam{};

    const D3DXVECTOR3 eye = camera.Eye();   // g_CameraPos 0x800130 (= g_GfxRenderer+792)
    cam.eye[0] = eye.x;
    cam.eye[1] = eye.y;
    cam.eye[2] = eye.z;

    D3DXMATRIX view, invView;
    camera.BuildViewMatrix(view);
    D3DXMatrixInverse(&invView, nullptr, &view); // unk_800194 (= g_GfxRenderer+892)
    std::memcpy(cam.invView, &invView, sizeof(cam.invView)); // 16 floats row-major

    // SAME aspect ratio calc as Gfx_InitDevice 0x69BFC6 and Scene/WorldRenderer.cpp:556-559.
    const float aspect = (screenH > 0)
        ? static_cast<float>(screenW) / static_cast<float>(screenH)
        : 1.0f;
    D3DXMATRIX proj;
    camera.BuildProjMatrix(proj, aspect);
    cam.proj11 = proj._11;   // flt_8000F0 (= g_GfxRenderer+728)
    cam.proj22 = proj._22;   // flt_800104 (= g_GfxRenderer+748)

    cam.screenW = screenW;   // dword_8000A0 (= g_GfxRenderer+648)
    cam.screenH = screenH;   // dword_8000A4 (= g_GfxRenderer+652)
    return cam;
}

// ===========================================================================
// World_PickEntityAtCursor 0x538AB0 — cf. §1..§4 of the .h banner.
// ===========================================================================

bool World_PickEntityAtCursor(const game::GameWorld& world,
                               const collision::ScreenPickCamera& cam,
                               int screenX, int screenY,
                               bool allowModifierTargets, bool modifierKeyDown,
                               const EntityPickHost& host,
                               int& outKind, int& outIndex) {
    outKind  = 0;   // *a3 = 0  @0x538ABC
    outIndex = -1;  // *a4 = -1 @0x538AC5

    // v14 : distance of the best candidate. Uninitialized in the binary — never read
    // before being set, since every test is guarded by `*a3` (== outKind != 0).
    float best = 0.0f;

    // Local player position (flt_1687330 == g_EntityArray[0]+252 == players[0].x/y/z).
    // Same fallback as Scene/WorldRenderer.cpp:567-570 if the pool is empty.
    float selfPos[3] = { 0.0f, 0.0f, 0.0f };
    if (!world.players.empty()) {
        selfPos[0] = world.players[0].x;
        selfPos[1] = world.players[0].y;
        selfPos[2] = world.players[0].z;
    }

    // "Keep the candidate": STRICT `v14 > dist` comparison -> on a tie, the FIRST
    // one found wins. Single form for all 8 sites (the nested if/else variants of
    // categories 1/2/3/7 and `!*a3 || v14 > d` of categories 4/5/6 are semantically
    // identical).
    const auto take = [&](int kind, int index, float dist) {
        if (outKind == 0 || best > dist) {
            outKind  = kind;
            outIndex = index;
            best     = dist;
        }
    };

    // Categories 1/2/3 — remote players. Loop @0x538ACB : `for (i = 1; i < g_EntityCount;
    // ++i)` -> index 0 (self) is EXCLUDED. Same iteration convention as
    // Game/AutoTargetCombatGate.cpp:69.
    const int  localElement = world.self.element;              // g_LocalElement 0x1673194
    // g_SelfMorphNpcId 0x1675A98 == current zone id (cf. Game/SkillCombat.h:33 for the
    // proof). We read g_World.zoneId and NOT g_Client.VarGet(0x1675A98): that
    // escape-hatch key has NO writer anywhere in ClientSource (verified) and would therefore
    // always return 0, making the guard dead. Scene/SceneManager.cpp:374/462 establishes
    // the equivalence (`worldMap_->SetCurrentZoneId(zoneId); // g_SelfMorphNpcId 0x1675A98`).
    const bool zoneBlocksNeutral = ZoneBlocksNeutralPlayer(world.zoneId);

    for (std::size_t i = 1; i < world.players.size(); ++i) {
        const game::PlayerEntity& p = world.players[i];
        if (!p.active) continue;                                         // g_EntityArray[227i]
        if (!game::AutoTarget_PlayerRecordPopulated(p)) continue;        // dword_168724C[227i]
        if (!game::Combat_IsTargetablePlayerState(p.anim.state)) continue; // 0x558AE0 (state != 12)
        if (!RayHitPlayerBox(cam, screenX, screenY, p)) continue;        // 0x5415E0

        const float pos[3] = { p.x, p.y, p.z };
        const float dist   = Dist3D(pos, cam.eye);                       // 0x538B7E (vs g_CameraPos)
        const int   idx    = static_cast<int>(i);

        // --- Category 2 : trade partner (@0x538BCC) ---
        const game::PlayerEntity& self0 = world.players[0];
        const bool tradePair =
            ReadI32(self0, kBodyTradeFlag) == 1 &&
            ReadI32(p,     kBodyTradeFlag) == 1 &&
            ReadI32(self0, kBodyTradeA) == ReadI32(p, kBodyTradeA) &&
            ReadI32(self0, kBodyTradeB) != ReadI32(p, kBodyTradeB);
        if (tradePair) {
            take(2, idx, dist);                                          // @0x538BD9 / @0x538C01
            continue;
        }

        // --- Category 3 : attackable player (@0x538C4E) ---
        // Combat_CanTargetOnMap 0x558740 — not ported, delegated to the host (default: false).
        const bool canTarget = host.CanTargetOnMap
            ? host.CanTargetOnMap(static_cast<int>(ReadI32(p, kBodyElement)),
                                   static_cast<int>(ReadI32(p, kBodyPkLevel)),
                                   ReadCString(p, kBodyAffiliation, kAffiliationMaxLen))
            : false;
        if (canTarget) {
            take(3, idx, dist);                                          // @0x538C62 / @0x538C8A
            continue;
        }

        // --- Category 1 : neutral player (@0x538CFD zone guard, @0x538D07 gating) ---
        if (zoneBlocksNeutral) continue;
        // Modifier gating: allowModifierTargets==false -> ALWAYS eligible ;
        //                  ==true -> requires byte_8013FE < 0 (DIK_LSHIFT held).
        if (allowModifierTargets && !modifierKeyDown) continue;          // @0x538D15
        take(1, idx, dist);                                              // @0x538D22 / @0x538D4A
    }

    // Category 4 — NPCs (render pool g_NpcRenderArray 0x1764D14, stride 88). Loop
    // @0x538DAC. ONLY loop carrying the 500.0 range filter, measured against the PLAYER
    // position (flt_1687330), not the camera.
    for (std::size_t j = 0; j < world.npcRenderEntries.size(); ++j) {
        const game::NpcRenderEntry& n = world.npcRenderEntries[j];
        if (!n.active) continue;                                         // dword_1764D18[22j]
        const float pos[3] = { n.x, n.y, n.z };                          // unk_1764D28 + 22j
        if (Dist3D(pos, selfPos) > 500.0f) continue;                     // @0x538E04 (`<= 500.0`)
        if (!RayHitNpcBox(cam, screenX, screenY, n)) continue;           // 0x541680 @0x538E21
        take(4, static_cast<int>(j), Dist3D(pos, cam.eye));              // @0x538E48 / @0x538E83
    }

    // Category 5 — monsters (dword_1766F74, stride 280). Loop @0x538E9C.
    const game::ElementPairTable pairs = game::Combat_ReadLocalElementPairs(); // Char_GetPairedElement 0x557C00
    for (std::size_t k = 0; k < world.monsters.size(); ++k) {
        const game::MonsterEntity& m = world.monsters[k];
        if (!m.active) continue;                                         // dword_1766F74[70k]
        if (!game::Combat_IsTargetableMonsterState(game::AutoTarget_MonsterActionState(m)))
            continue;                                                    // 0x558B10 (!= 12 && != 19)
        if (!RayHitMonsterBox(cam, screenX, screenY, m)) continue;       // 0x541780
        if (!m.def) continue;
        if (!MonsterPassesElementGate(*static_cast<const game::MonsterInfo*>(m.def),
                                       localElement, pairs))
            continue;                                                    // @0x53905E..0x539083
        const float pos[3] = { m.x, m.y, m.z };                          // unk_1766F94 + 70k
        take(5, static_cast<int>(k), Dist3D(pos, cam.eye));              // @0x539083 / @0x5390BE
    }

    // Category 6 — ground items (dword_17AB534, stride 152, Scene_RayHitItemModel 0x5418B0).
    // TODO [0x5418B0 / 0x6A0750 / 0x4D7130] : NOT PORTED — triple blocker, NO observable
    // effect today (cf. §4 of the .h banner): (a) game::g_World.groundItems is empty
    // by design (152-byte structure not modeled, Game/GameState.h:605-613) ; (b) this
    // hit-test is an OBB test (ModelObj_GetBoneMatrix 0x4D7130 + Collide_SegmentOBB
    // 0x6A0750), neither of which is ported. When (a) falls, restore the
    // loop HERE (order matters for the "first found wins" tie rule).

    // Category 7 — zone objects (g_ZoneObjectArray 0x180EEF4, stride 76). Loop @0x5391A7.
    // Position = g_ZoneObjectPayload 0x180EF0C == record+24 == the first 3 floats of the body
    // (ZoneObjectEntity::body is at record+0x18 == +24). Same modifier gating as
    // category 1 (@0x539220), and NO other filter.
    for (std::size_t n = 0; n < world.zoneObjects.size(); ++n) {
        const game::ZoneObjectEntity& z = world.zoneObjects[n];
        if (!z.active) continue;                                         // g_ZoneObjectArray[19n]
        float pos[3];
        std::memcpy(pos, z.body.data(), sizeof(pos));
        if (!RayHitNodeBox(cam, screenX, screenY, pos)) continue;        // 0x541920 @0x5391F2
        if (allowModifierTargets && !modifierKeyDown) continue;          // @0x539220 / @0x53922F
        take(7, static_cast<int>(n), Dist3D(pos, cam.eye));              // @0x539219 / @0x53923C
    }

    return outKind != 0;
}

// ===========================================================================
// CursorSlotForPickCategory — table of the switch @0x530FC7..0x53120B.
// ===========================================================================

int CursorSlotForPickCategory(int kind, float gameTimeSec, bool canCastAtCursor) {
    if (kind < 0 || kind > 7) return -1;               // `cmp 7 / ja default` @0x530FB4

    // Category 0: NO animation — 7 if castable, 8 otherwise (Skill_CanCastAtCursor 0x540E60
    // called @0x530FE1, its ONLY caller).
    if (kind == 0) return canCastAtCursor ? 7 : 8;     // @0x530FEA / @0x530FF8

    // blink = ((int)(2.0f * g_GameTimeSec)) % 2 — the binary's SIGNED idiom (`fadd st,st` +
    // Crt_ftol + `and eax,80000001h` / `jns` / `dec` / `or eax,0FFFFFFFEh` / `inc`), which
    // exactly matches C++'s `%` (truncated division). 2 Hz alternation.
    const int blink = static_cast<int>(2.0f * gameTimeSec) % 2;

    switch (kind) {
        case 1: return blink + 1; // @0x531022 — neutral player
        case 2: return blink + 3; // @0x531075 — trade partner
        case 3: return blink + 3; // @0x5310C8 — attackable player
        case 4: return blink + 1; // @0x53111B — NPC
        case 5: return blink + 3; // @0x53116B — monster
        case 6: return blink + 5; // @0x5311B9 — ground item
        case 7: return blink + 1; // @0x5311E2 — zone object
        default: return -1;
    }
}

} // namespace ts2::world

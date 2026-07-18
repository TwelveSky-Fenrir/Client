// Net/CharStatDeltaDispatch.cpp — FAITHFUL implementation of the op 0x11 dispatcher.
//
// Byte-exact translation of Pkt_CharStatDelta (EA 0x465d90) decompiled via idaTs2.
// Original structure:
//   1. copies the 6 payload dwords (idHi/idLo/subOp/valA/valB/valC);
//   2. linear scan of the entities (g_EntityArray 0x1687234, stride 908, 227 dwords)
//      to find the index v38 such that active && idHi==payload.idHi && idLo==payload.idLo;
//      stops at the 1st match, v38=-1 otherwise -> no effect;
//   3. switch(subOp): each case writes into the found entity's body and,
//      for self (index 0), into the "self" globals.
//
// -- OFFSET MAP --------------------------------------------------------
// The entity record is 908 bytes: active@+0, idHi@+4, idLo@+8, timestamp@+0x0C,
// body@+0x18. The address of a dword_XXXX field indexed [227*v38] therefore maps to
// body offset = (0xXXXXXXX - 0x168724C)  (0x168724C = 0x1687234 + 0x18).
// Target fields (verified against Game/EntityManager.cpp):
//   body+84  dword_16872A0  level counter (kPLevelCtr)
//   body+88  dword_16872A4
//   body+8   dword_1687254
//   body+196 dword_1687310  | body+200 dword_1687314 | body+208 dword_168731C
//   body+224 g_SelfAnimFrame (float, kPAnimFrame)
//   body+288 dword_168736C  (AR-min max)  | body+292 dword_1687370  (AR-min current = "hp")
//   body+296 dword_1687374  (AR-max max)  | body+300 dword_1687378  (AR-max current = "mp")
//   body+304 dword_168737C  (state array base, kPStateArr)
//   body+308 …380 …388 …38C …394 …398 …39C …3A0 …3A4 …3A8 …3AC …3B0 …3B4 (states/dots)
//   body+420…440 dword_16873F0..1687404 (7 dot counters)
//   body+464 g_TradePartnerIdLo | +468 dword_1687420 | +472 dword_1687424
//   body+508 dword_1687448 (mount/pet) | body+552 dword_1687474 (active grade)
//   body+580 dword_1687490 | +584 dword_1687494 | +588 dword_1687498 | +592 dword_168749C
// The "flash flags" cluster (record +820.. = body+796..) extends past the 600-byte body
// modeled in PlayerEntity; it is stored FAITHFULLY via g_Client.Var/VarF
// keyed on the original address (+ 908*index for non-self entities). Same for the
// long tail of "self" globals (dword_167XXXX / dword_1674XXX) not modeled as a
// dedicated field — an escape hatch sanctioned by ClientRuntime.
//
// -- OUT OF SCOPE (SPECIFIC TODOs) -----------------------------------------------
// Calls into other subsystems are reproduced for their DATA-SIDE EFFECTS,
// but the external call itself is left as a TODO:
//   Snd3D_PlayPositional 0x4da450 / Snd3D_PlayScaledVolume 0x4da380  (audio)
//   cDrawWin_Init 0x628e40 (UI)  | QuestTbl_FindByGroupAndStage 0x4c8a60 (quest table)
//   Net_SendPacket_Op17 0x4b4b70 (send)
// -- WIRED (W2-F3) -------------------------------------------------------------
//   Char_CalcAttackRatingMin 0x4cd970 / Char_CalcAttackRatingMax 0x4ce3f0 -> facade
//     StatEngine::CalcAttackRatingMin/Max(g_World.self, g_World.db) (full aggregation:
//     equip+level+buffs, LIVE value; cf. CalcAtkRatingMin/Max below).
//   Map_BeginWarpToFactionTown 0x55c510 -> ts2::game::BeginWarpToFactionTown (case 11).
#include "Net/CharStatDeltaDispatch.h"

#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/ClientRuntime.h"
#include "Game/StatEngine.h"                 // StatEngine::CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0)
#include "Game/MapWarp.h"                    // ts2::game::BeginWarpToFactionTown (0x55C510)
#include "Game/MotionPoolsCoordResolver.h"  // g_CoordResolver

#include <cstring>
#include <vector>

namespace ts2::game {
namespace {

// ---- bounds-checked LE read/write on a byte buffer (no aliasing UB).
inline int32_t  RdI32(const uint8_t* b, size_t o) { int32_t  v; std::memcpy(&v, b + o, 4); return v; }
inline uint32_t RdU32(const uint8_t* b, size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; }
inline void     WrI32(uint8_t* b, size_t o, int32_t v) { std::memcpy(b + o, &v, 4); }
inline void     WrF32(uint8_t* b, size_t o, float   v) { std::memcpy(b + o, &v, 4); }

// ---- body offsets (relative to the start of the 600-byte body) — cf. file header.
constexpr size_t kBLevelCtr = 84;   // dword_16872A0
constexpr size_t kB88       = 88;   // dword_16872A4
constexpr size_t kB8        = 8;    // dword_1687254
constexpr size_t kB196      = 196;  // dword_1687310
constexpr size_t kB200      = 200;  // dword_1687314
constexpr size_t kB208      = 208;  // dword_168731C
constexpr size_t kBAnim     = 224;  // g_SelfAnimFrame (float)
constexpr size_t kB288      = 288;  // dword_168736C (AR-min max)
constexpr size_t kBHp       = 292;  // dword_1687370 (AR-min current)
constexpr size_t kB296      = 296;  // dword_1687374 (AR-max max)
constexpr size_t kBMp       = 300;  // dword_1687378 (AR-max current)
constexpr size_t kB304      = 304;  // dword_168737C
constexpr size_t kB308      = 308;  // dword_1687380
constexpr size_t kB316      = 316;  // dword_1687388
constexpr size_t kB320      = 320;  // dword_168738C
constexpr size_t kB328      = 328;  // dword_1687394
constexpr size_t kB332      = 332;  // dword_1687398
constexpr size_t kB336      = 336;  // dword_168739C
constexpr size_t kB340      = 340;  // dword_16873A0
constexpr size_t kB344      = 344;  // dword_16873A4
constexpr size_t kB348      = 348;  // dword_16873A8
constexpr size_t kB352      = 352;  // dword_16873AC
constexpr size_t kB356      = 356;  // dword_16873B0
constexpr size_t kB360      = 360;  // dword_16873B4
constexpr size_t kB420      = 420;  // dword_16873F0
constexpr size_t kB424      = 424;  // dword_16873F4
constexpr size_t kB428      = 428;  // dword_16873F8
constexpr size_t kB432      = 432;  // dword_16873FC
constexpr size_t kB436      = 436;  // dword_1687400
constexpr size_t kB440      = 440;  // dword_1687404
constexpr size_t kBTrade    = 464;  // g_TradePartnerIdLo
constexpr size_t kB468      = 468;  // dword_1687420
constexpr size_t kB472      = 472;  // dword_1687424
constexpr size_t kB508      = 508;  // dword_1687448 (mount/pet)
constexpr size_t kB552      = 552;  // dword_1687474 (active grade)
constexpr size_t kB580      = 580;  // dword_1687490
constexpr size_t kB584      = 584;  // dword_1687494
constexpr size_t kB588      = 588;  // dword_1687498
constexpr size_t kB592      = 592;  // dword_168749C

// ---- original addresses of the "flash flags" (record +796.. > 600-byte body) and
//      of the long tail of "self" globals. Stored via g_Client.Var/VarF.
constexpr uint32_t FL_568 = 0x1687568, FL_56C = 0x168756C; // case 1/11
constexpr uint32_t FL_570 = 0x1687570, FL_574 = 0x1687574; // case 14/27
constexpr uint32_t FL_578 = 0x1687578, FL_57C = 0x168757C; // case 4
constexpr uint32_t FL_580 = 0x1687580, FL_584 = 0x1687584; // case 3/22
constexpr uint32_t FL_588 = 0x1687588, FL_58C = 0x168758C; // case 8
constexpr uint32_t FL_590 = 0x1687590, FL_594 = 0x1687594; // case 9
constexpr uint32_t FL_5A0 = 0x16875A0, FL_5A4 = 0x16875A4; // case 10

// Entity record stride (908 bytes), to replicate the [227*v38] indexing of
// off-body fields via an absolute-address key.
constexpr uint32_t kEntStride = 908;

// "Flash flag" (int/float) access for an entity of index idx, keyed on the original address.
inline int32_t& FlagI(uint32_t base, int idx) { return g_Client.Var (base + kEntStride * static_cast<uint32_t>(idx)); }
inline float&   FlagF(uint32_t base, int idx) { return g_Client.VarF(base + kEntStride * static_cast<uint32_t>(idx)); }

// Scalar "self" global from the long tail (single index).
inline int32_t& SV (uint32_t addr) { return g_Client.Var (addr); }
inline float&   SVf(uint32_t addr) { return g_Client.VarF(addr); }

// Element of a "self" dword array (case 30), keyed on base + 4*k.
inline int32_t& Arr(uint32_t base, int k) { return g_Client.Var(base + 4u * static_cast<uint32_t>(k)); }

// LevelTable_GetMaxExp 0x4c2990: returns field dword+3 (=LevelInfo::meta, offset +12)
// of the level record `lvl` (1..145), or 0 otherwise. Used to credit the point pool.
int32_t LevelMetaGain(int lvl) {
    if (lvl < 1 || lvl > 145) return 0;
    const LevelInfo* li = GetLevelInfo(lvl);
    return li ? static_cast<int32_t>(li->meta) : 0;
}

// Char_CalcAttackRatingMin 0x4cd970 / Max 0x4ce3f0: full StatEngine aggregate (equip +
// level + buffs + gems + set + meridian + talisman). The original `this` =
// g_EquipSnapshotScratch 0x8E719C (self snapshot) => g_World.self/db.
inline int32_t CalcAtkRatingMin() { return StatEngine::CalcAttackRatingMin(g_World.self, g_World.db); }
inline int32_t CalcAtkRatingMax() { return StatEngine::CalcAttackRatingMax(g_World.self, g_World.db); }

// QuestTbl_FindByGroupAndStage 0x4c8a60: looks up a quest entry by group
// (secondary element) and tier (level). TODO 0x4c8a60: wire up the quest table.
// Returns <=0 while unwired (=> no active-quest marker), faithful to the
// "no quest found" behavior.
inline int32_t QuestFindByGroupAndStage(int /*group*/, int /*level*/) { return 0; }

// SkillGrowthTbl_GetRecord 0x4c4e90: 1-based accessor into the SKILL_INFO table
// (stride 776). Returns record `idx1` (1-based) only if its 1st dword != 0.
const uint8_t* SkillRecord(int idx1) {
    const DataTable& t = g_World.db.skill;
    if (idx1 < 1 || static_cast<uint32_t>(idx1) > t.count) return nullptr;
    const uint8_t* rec = t.record(static_cast<uint32_t>(idx1) - 1);
    if (!rec || RdI32(rec, 0) == 0) return nullptr;
    return rec;
}

// "-= d, floor 0" clamp on a body field (repeated pattern in cases 4/5/6/31..36).
inline void SubClampFloor0(uint8_t* b, size_t off, int32_t d) {
    int32_t v = RdI32(b, off) - d;
    if (v < 1) v = 0;
    WrI32(b, off, v);
}

// Mirrors the combat bars into a player's plain fields (like EntityManager).
inline void ReflectBars(PlayerEntity& p) {
    const uint8_t* b = p.body.data();
    p.hp = RdI32(b, kBHp);
    p.mp = RdI32(b, kBMp);
}

// Recomputes + clamps the SELF (index 0) combat bars — pattern of cases 12/13/26 and
// (on explicit index 0) 16/17. `sb` = self's body (players[0]).
void RecomputeSelfBars(uint8_t* sb) {
    const int32_t mn = CalcAtkRatingMin();
    WrI32(sb, kB288, mn);
    if (RdI32(sb, kBHp) > mn) WrI32(sb, kBHp, mn);
    const int32_t mx = CalcAtkRatingMax();
    WrI32(sb, kB296, mx);
    if (RdI32(sb, kBMp) > mx) WrI32(sb, kBMp, mx);
}

// System message color (g_SysMsgColor 0x84dfd8) — D3DCOLOR; defaults to opaque white.
uint32_t SysMsgColor() {
    uint32_t c = static_cast<uint32_t>(g_Client.VarGet(0x84dfd8));
    return c ? c : 0xFFFFFFFFu;
}

} // namespace (anonymous)

void ApplyCharStatDelta(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 24) return; // fixed 24-byte payload (size_table 0x1c).

    const uint32_t idHi  = RdU32(payload, 0);
    const uint32_t idLo  = RdU32(payload, 4);
    const int32_t  subOp = RdI32(payload, 8);
    const int32_t  valA  = RdI32(payload, 12); // v36
    const int32_t  valB  = RdI32(payload, 16); // v39
    const int32_t  valC  = RdI32(payload, 20); // v43

    // --- entity resolution: linear scan, stops at the 1st matching active slot.
    int idx = -1;
    for (size_t i = 0; i < g_World.players.size(); ++i) {
        const PlayerEntity& e = g_World.players[i];
        if (e.active && e.id.hi == idHi && e.id.lo == idLo) { idx = static_cast<int>(i); break; }
    }
    if (idx < 0) return; // v38 == -1 -> no effect.

    const bool self = (idx == 0);
    uint8_t*   b    = g_World.players[idx].body.data();
    // Self's body (index 0) — always present once an entity has been found.
    uint8_t*   sb   = g_World.players[0].body.data();

    switch (subOp) {

    // -- case 1: level gain -------------------------------------------------
    case 1:
        if (!self) {
            WrI32(b, kBLevelCtr, RdI32(b, kBLevelCtr) + valA); // dword_16872A0 += v36
            FlagI(FL_568, idx) = 1; FlagF(FL_56C, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional(&flt_1687330[227*idx]) (positional audio).
        } else {
            for (int i = g_World.self.level + 1; i <= valA + g_World.self.level; ++i) {
                if (i - 1 >= 99) {
                    if (i - 1 >= 112) g_World.self.unspentAttr += 30; // g_SelfUnspentAttrPoints
                    else              g_World.self.unspentAttr += 15;
                } else {
                    g_World.self.unspentAttr += 5;
                }
                g_World.self.skillPoints += LevelMetaGain(i); // g_SkillPointPool += LevelTable_GetMaxExp(i)
            }
            g_World.self.level += valA;                        // g_SelfLevel += v36
            WrI32(b, kBLevelCtr, RdI32(b, kBLevelCtr) + valA); // dword_16872A0[0] += v36
            const int32_t mn = CalcAtkRatingMin();
            WrI32(b, kBHp, mn);  WrI32(b, kB288, mn);          // dword_1687370 / dword_168736C
            const int32_t mx = CalcAtkRatingMax();
            WrI32(b, kBMp, mx);  WrI32(b, kB296, mx);          // dword_1687378 / dword_1687374
            FlagI(FL_568, 0) = 1; FlagF(FL_56C, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1) ; TODO 0x628e40 : cDrawWin_Init(&dword_1839290).
            const int32_t q = QuestFindByGroupAndStage(g_World.self.elementSecondary, g_World.self.level);
            SV(0x1675B9C) = q;
            if (q <= 0) {
                SV(0x1675B98) = 0;
            } else {
                SV(0x1675B98) = 1;
                // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            }
            SV(0x1675AE8) = 0;
            SVf(0x1675AEC) = g_World.gameTimeSec - 590.0f; // flt_1675AEC = g_GameTimeSec - 590.0
        }
        break;

    // -- case 2: reset counter body+336 ---------------------------------------
    case 2:
        if (!self) {
            WrI32(b, kB336, 0);                     // dword_168739C
        } else {
            SV(0x1675918) = 0; SV(0x167591C) = 0;
            WrI32(b, kB336, 0);                     // dword_168739C[0]
        }
        break;

    // -- case 3: reset dot block (body+340,+420..+440) ----------------------
    case 3:
        if (!self) {
            WrI32(b, kB340, 0); WrI32(b, kB420, 0); WrI32(b, kB424, 0); WrI32(b, kB428, 0);
            WrI32(b, kB432, 0); WrI32(b, kB436, 0); WrI32(b, kB440, 0);
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
        } else {
            SV(0x1675920) = 0; SV(0x1675924) = 0; WrI32(b, kB340, 0);
            SV(0x16759C0) = 0; SV(0x16759C4) = 0; WrI32(b, kB420, 0);
            SV(0x16759C8) = 0; SV(0x16759CC) = 0; WrI32(b, kB424, 0);
            SV(0x16759D0) = 0; SV(0x16759D4) = 0; WrI32(b, kB428, 0);
            SV(0x16759D8) = 0; SV(0x16759DC) = 0; WrI32(b, kB432, 0);
            SV(0x16759E0) = 0; SV(0x16759E4) = 0; WrI32(b, kB436, 0);
            SV(0x16759E8) = 0; SV(0x16759EC) = 0; WrI32(b, kB440, 0);
            FlagI(FL_580, 0) = 1; FlagF(FL_584, 0) = 0.0f;
        }
        break;

    // -- case 4: HP damage (AR-min current -= valA -= valB, floor 0) --------
    case 4: {
        int32_t hp = RdI32(b, kBHp) - valA;   // dword_1687370 -= v36
        hp -= valB;                            //              -= v39
        if (hp < 1) hp = 0;
        WrI32(b, kBHp, hp);
        FlagI(FL_578, idx) = 1; FlagF(FL_57C, idx) = 0.0f;
        break;
    }

    // -- case 5: dot body+340 -= valA (floor 0), + self mirror --------------
    case 5:
        if (!self) {
            SubClampFloor0(b, kB340, valA);
        } else {
            SV(0x1675920) -= valA;
            if (SV(0x1675920) < 1) { SV(0x1675920) = 0; SV(0x1675924) = 0; }
            SubClampFloor0(b, kB340, valA);
        }
        break;

    // -- case 6: identical to case 5 (dot body+340) -----------------------------
    case 6:
        if (!self) {
            SubClampFloor0(b, kB340, valA);
        } else {
            SV(0x1675920) -= valA;
            if (SV(0x1675920) < 1) { SV(0x1675920) = 0; SV(0x1675924) = 0; }
            SubClampFloor0(b, kB340, valA);
        }
        break;

    // -- case 7: trade partner identity (body+464/468/472) -----------
    case 7:
        WrI32(b, kBTrade, valA); // g_TradePartnerIdLo = v36
        WrI32(b, kB468,   valB); // dword_1687420      = v39
        WrI32(b, kB472,   valC); // dword_1687424      = v43
        break;

    // -- case 8: heal (AR-min current += valA) ---------------------------------
    case 8:
        WrI32(b, kBHp, RdI32(b, kBHp) + valA); // dword_1687370 += v36
        FlagI(FL_588, idx) = 1; FlagF(FL_58C, idx) = 0.0f;
        break;

    // -- case 9: MP heal (AR-max current += valA) ------------------------------
    case 9:
        WrI32(b, kBMp, RdI32(b, kBMp) + valA); // dword_1687378 += v36
        FlagI(FL_590, idx) = 1; FlagF(FL_594, idx) = 0.0f;
        break;

    // -- case 10: simple flash trigger --------------------------------
    case 10:
        FlagI(FL_5A0, idx) = 1; FlagF(FL_5A4, idx) = 0.0f;
        break;

    // -- case 11: form change / level bonus ------------------------
    case 11:
        if (!self) {
            WrI32(b, kB208, 0);    // dword_168731C = 0
            WrI32(b, kB88, valA);  // dword_16872A4 = v36
            FlagI(FL_568, idx) = 1; FlagF(FL_56C, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional (positional audio).
        } else {
            SV(0x16747BC) = 0;
            g_World.self.skillPoints = valB;   // g_SkillPointPool = v39
            SV(0x16731B4) = 0;
            g_World.self.levelBonus = valA;    // g_SelfLevelBonus = v36
            WrI32(b, kB208, 0);                // dword_168731C[0] = 0
            WrI32(b, kB88, valA);              // dword_16872A4[0] = v36
            WrI32(b, kBHp, CalcAtkRatingMin()); // dword_1687370
            WrI32(b, kBMp, CalcAtkRatingMax()); // dword_1687378
            FlagI(FL_568, 0) = 1; FlagF(FL_56C, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            const int32_t morph = SV(0x1675A98); // g_SelfMorphNpcId
            // element = g_LocalElement 0x1673194 = g_World.self.element ; this = g_LocalPlayerSheet ;
            // mode = 0 (original Map_BeginWarpToFactionTown(0)). nc defaults to nullptr (resolution only).
            if (morph == 85) {
                if (g_World.self.levelBonus > 11)
                    BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver); // 0x55c510
            } else if (morph == 196 && g_World.self.levelBonus > 0) {
                BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);     // 0x55c510
            }
        }
        break;

    // -- case 12: equip an item/costume (body+196) + self recompute -----------
    case 12:
        WrI32(b, kB196, valA);   // dword_1687310 = v36
        WrF32(b, kBAnim, 0.0f);  // g_SelfAnimFrame = 0.0
        if (self) RecomputeSelfBars(b); // index 0: recompute + clamp the bars
        break;

    // -- case 13: unequip item/costume (body+196 = 0) + self recompute -------
    case 13:
        WrI32(b, kB196, 0);      // dword_1687310 = 0
        WrF32(b, kBAnim, 0.0f);  // g_SelfAnimFrame = 0.0
        if (self) RecomputeSelfBars(b);
        break;

    // -- case 14: currency/state update + self recompute ------------------
    case 14:
        if (!self) {
            WrI32(b, kB8,   valA); // dword_1687254 = v36
            WrI32(b, kB208, valB); // dword_168731C = v39
            FlagI(FL_570, idx) = 1; FlagF(FL_574, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional (positional audio).
        } else {
            SV(0x16731B4) = 0;
            g_World.self.currency = valA;      // g_Currency = v36
            g_Client.inv.currency = valA;      // inventory mirror (dword_1687254[0])
            SV(0x16747BC) = valB;
            SV(0x16747C0) = valC;
            WrI32(b, kB8,   valA);             // dword_1687254[0] = v36
            WrI32(b, kB208, valB);             // dword_168731C[0] = v39
            WrI32(b, kBHp, CalcAtkRatingMin()); // dword_1687370
            WrI32(b, kBMp, CalcAtkRatingMax()); // dword_1687378
            FlagI(FL_570, 0) = 1; FlagF(FL_574, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            if (SV(0x16747BC) == 12) {
                // TODO 0x4b4b70 : Net_SendPacket_Op17("%s %s", byte_1673184 (local name),
                //                 StrTable005_Get(1583)) — server notification.
            }
        }
        break;

    // -- case 16: equip mount/pet (entity's body+508) + SELF recompute ---
    // NB: the recompute ALWAYS targets index 0 (self), even for a remote entity,
    //      because Char_CalcAttackRating* reads dword_1687448[0] (self's pet).
    case 16:
        WrI32(b, kB508, valA); // dword_1687448[227*idx] = v36
        RecomputeSelfBars(sb); // dword_168736C[0].. (explicit index 0)
        WrF32(b, kBAnim, 0.0f);// g_SelfAnimFrame[227*idx] = 0.0
        break;

    // -- case 17: unequip mount/pet (body+508 = 0) + SELF recompute --------
    case 17:
        WrI32(b, kB508, 0);
        RecomputeSelfBars(sb); // explicit index 0
        WrF32(b, kBAnim, 0.0f);
        break;

    // -- case 18: field body+200 = valA ----------------------------------------
    case 18:
        WrI32(b, kB200, valA); // dword_1687314 = v36
        break;

    // -- case 22: MULTI-FIELD RESET (nested switch on valA, 0..10) ----------
    case 22: {
        // SELF branch: resets the "self" globals BEFORE the body reset (LABEL_107).
        // valA outside [0..10] => immediate return (no body effect), faithful to the default.
        if (self) {
            switch (valA) {
            case 0:  SV(0x1675918) = 0; SV(0x167591C) = 0; break;
            case 1:  SV(0x16758F8) = 0; SV(0x16758FC) = 0; SV(0x1675908) = 0; SV(0x167590C) = 0; break;
            case 2:  SV(0x16758E0) = 0; SV(0x16758E4) = 0; break;
            case 3:  SV(0x16758D8) = 0; SV(0x16758DC) = 0; break;
            case 4:  SV(0x16758F0) = 0; SV(0x16758F4) = 0; SV(0x1675910) = 0; SV(0x1675914) = 0; break;
            case 5:
                SV(0x1675920) = 0; SV(0x1675924) = 0;
                SV(0x16759C0) = 0; SV(0x16759C4) = 0; SV(0x16759C8) = 0; SV(0x16759CC) = 0;
                SV(0x16759D0) = 0; SV(0x16759D4) = 0; SV(0x16759D8) = 0; SV(0x16759DC) = 0;
                SV(0x16759E0) = 0; SV(0x16759E4) = 0; SV(0x16759E8) = 0; SV(0x16759EC) = 0;
                break;
            case 6:  SV(0x1675928) = 0; SV(0x167592C) = 0; break;
            case 7:  SV(0x1675930) = 0; SV(0x1675934) = 0; break;
            case 8:  SV(0x1675938) = 0; SV(0x167593C) = 0; break;
            case 9:  SV(0x1675940) = 0; SV(0x1675944) = 0; break;
            case 10: SV(0x1675948) = 0; SV(0x167594C) = 0; break;
            default: return; // valA out of range -> no body reset.
            }
        }
        // LABEL_107: resets the body fields (index idx) + flash 580.
        switch (valA) {
        case 0:
            WrI32(b, kB336, 0);                                 // dword_168739C
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 1:
            WrI32(b, kB320, 0); WrI32(b, kB328, 0);             // dword_168738C, dword_1687394
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 2:
            WrI32(b, kB308, 0);                                 // dword_1687380
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 3:
            WrI32(b, kB304, 0);                                 // dword_168737C
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 4:
            WrI32(b, kB316, 0); WrI32(b, kB332, 0);             // dword_1687388, dword_1687398
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 5:
            WrI32(b, kB340, 0); WrI32(b, kB420, 0); WrI32(b, kB424, 0); WrI32(b, kB428, 0);
            WrI32(b, kB432, 0); WrI32(b, kB436, 0); WrI32(b, kB440, 0);
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 6:
            WrI32(b, kB344, 0);                                 // dword_16873A4
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 7:
            WrI32(b, kB348, 0);                                 // dword_16873A8
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 8:
            WrI32(b, kB352, 0);                                 // dword_16873AC
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 9:
            WrI32(b, kB356, 0);                                 // dword_16873B0
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 10:
            WrI32(b, kB360, 0);                                 // dword_16873B4
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        default:
            return; // valA out of range -> no effect.
        }
        break;
    }

    // -- case 23: MP = 0 (+ self mirror dword_16746E0) -------------------------
    case 23:
        if (self) SV(0x16746E0) = 0;
        WrI32(b, kBMp, 0);       // dword_1687378 = 0
        break;

    // -- case 24: sets HP (AR-min current) (+ self mirror dword_16746DC) -------
    case 24:
        if (self) SV(0x16746DC) = valA;
        WrI32(b, kBHp, valA);    // dword_1687370 = v36
        break;

    // -- case 25: sets MP (AR-max current) (+ self mirror dword_16746E0) -------
    case 25:
        if (self) SV(0x16746E0) = valA;
        WrI32(b, kBMp, valA);    // dword_1687378 = v36
        break;

    // -- case 26: active grade (body+552) + self recompute -----------------------
    case 26:
        WrI32(b, kB552, valA);   // dword_1687474 = v36
        WrF32(b, kBAnim, 0.0f);  // g_SelfAnimFrame = 0.0
        if (self) RecomputeSelfBars(b); // index 0 (self): recompute + clamp
        break;

    // -- case 27: morph/mount (body+208) + self recompute ---------------------
    case 27:
        if (!self) {
            WrI32(b, kB208, valA); // dword_168731C = v36
            FlagI(FL_570, idx) = 1; FlagF(FL_574, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional (positional audio).
        } else {
            SV(0x16747BC) = valA;
            SV(0x16747C0) = valB;
            WrI32(b, kB208, valA);             // dword_168731C[0] = v36
            WrI32(b, kBHp, CalcAtkRatingMin()); // dword_1687370
            WrI32(b, kBMp, CalcAtkRatingMax()); // dword_1687378
            FlagI(FL_570, 0) = 1; FlagF(FL_574, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            if (SV(0x16747BC) == 12) {
                // TODO 0x4b4b70 : Net_SendPacket_Op17("%s %s", byte_1673184, StrTable005_Get(1583)).
            }
        }
        break;

    // -- case 28: field body+580 = valA ----------------------------------------
    case 28:
        WrI32(b, kB580, valA);   // dword_1687490 = v36
        WrF32(b, kBAnim, 0.0f);  // g_SelfAnimFrame = 0.0
        break;

    // -- case 29: field body+584 = valA ----------------------------------------
    case 29:
        WrI32(b, kB584, valA);   // dword_1687494 = v36
        break;

    // -- case 30: learn/forget a skill (self only) --------
    case 30: {
        WrI32(b, kB588, valA);   // dword_1687498 = v36
        WrI32(b, kB592, valB);   // dword_168749C = v39
        if (!self) break;        // if (v38) return: only self continues.

        // LABEL_159: commits the last change (dword_1675880/1675884).
        auto label159 = [&]() { SV(0x1675880) = valA; SV(0x1675884) = valB; };

        if (valA == 0) {
            // Forgetting a skill: purges the hotbar grid + the learned slot.
            if (valC > -1) {
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 14; ++j) {
                        const int m = 42 * i + 3 * j;
                        if (Arr(0x16742BC, 2 * valC) == Arr(0x16743FC, m)) { // g_LearnedSkills[2*v43]==g_Container5_ItemId[m]
                            Arr(0x16743FC, m) = 0;   // g_Container5_ItemId[m] = 0
                            Arr(0x1674400, m) = 0;   // dword_1674400[m] = 0
                            Arr(0x1674404, m) = 0;   // dword_1674404[m] = 0
                            goto done_forget;
                        }
                    }
                }
            done_forget:
                Arr(0x16742BC, 2 * valC) = 0; // g_LearnedSkills[2*v43] = 0
                Arr(0x16742C0, 2 * valC) = 0; // dword_16742C0[2*v43] = 0
                g_Client.msg.System(Str(2428), SysMsgColor()); // StrTable005_Get(2428)
            }
            label159();
            break;
        }

        if (valC <= -1) { label159(); break; }

        const int32_t v32 = valA - 91254;
        const uint8_t* rec = SkillRecord(v32);
        if (rec) {
            Arr(0x16742BC, 2 * valC) = v32;              // g_LearnedSkills[2*v43] = v32
            Arr(0x16742C0, 2 * valC) = RdI32(rec, 560);  // dword_16742C0[2*v43] = *(rec+560)
            g_Client.msg.System(Str(2429), SysMsgColor()); // StrTable005_Get(2429)
            label159();
        }
        // rec == 0 : break without commit (faithful to the original `break`).
        break;
    }

    // -- case 31..36: dots body+420..+440 -= valA (floor 0) + self mirror ---
    case 31:
        if (!self) {
            SubClampFloor0(b, kB420, valA);
        } else {
            SV(0x16759C0) -= valA;
            if (SV(0x16759C0) < 1) { SV(0x16759C0) = 0; SV(0x16759C4) = 0; }
            SubClampFloor0(b, kB420, valA);
        }
        break;
    case 32:
        if (!self) {
            SubClampFloor0(b, kB424, valA);
        } else {
            SV(0x16759C8) -= valA;
            if (SV(0x16759C8) < 1) { SV(0x16759C8) = 0; SV(0x16759CC) = 0; }
            SubClampFloor0(b, kB424, valA);
        }
        break;
    case 33:
        if (!self) {
            SubClampFloor0(b, kB428, valA);
        } else {
            SV(0x16759D0) -= valA;
            if (SV(0x16759D0) < 1) { SV(0x16759D0) = 0; SV(0x16759D4) = 0; }
            SubClampFloor0(b, kB428, valA);
        }
        break;
    case 34:
        if (!self) {
            SubClampFloor0(b, kB432, valA);
        } else {
            SV(0x16759D8) -= valA;
            if (SV(0x16759D8) < 1) { SV(0x16759D8) = 0; SV(0x16759DC) = 0; }
            SubClampFloor0(b, kB432, valA);
        }
        break;
    case 35:
        if (!self) {
            SubClampFloor0(b, kB436, valA);
        } else {
            SV(0x16759E0) -= valA;
            if (SV(0x16759E0) < 1) { SV(0x16759E0) = 0; SV(0x16759E4) = 0; }
            SubClampFloor0(b, kB436, valA);
        }
        break;
    case 36:
        if (!self) {
            SubClampFloor0(b, kB440, valA);
        } else {
            SV(0x16759E8) -= valA;
            if (SV(0x16759E8) < 1) { SV(0x16759E8) = 0; SV(0x16759EC) = 0; }
            SubClampFloor0(b, kB440, valA);
        }
        break;

    // -- subOp 15/19/20/21 and others: no matching case -> no-op (faithful to the default). --
    default:
        break;
    }

    // Mirrors the combat bars into the plain fields (like EntityManager): found entity
    // + self (cases 16/17 modify index 0 even for a remote target).
    ReflectBars(g_World.players[idx]);
    if (idx != 0) ReflectBars(g_World.players[0]);
}

} // namespace ts2::game

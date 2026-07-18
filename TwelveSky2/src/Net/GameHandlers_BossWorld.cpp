// Net/GameHandlers_BossWorld.cpp — routing for BOSS / ZONE / WORLD packets.
//
// "boss_world" domain (RE/handler_domains.json): boss HP bars
// (init/percent/panel/spawn/decrement), zone buffs, zone/map change,
// zone objects, instances/events, battlefield (war), UI ranking/achievement
// tables. The original faithful semantics are documented in
// RE/net_handler_notes.md (## <handler> (op 0xNN)).
//
//   0x0d ZoneChangeInfo        0x17 MapObjectUpdate        0x5f BossHpInit
//   0x60 ZoneBuffStatus        0x64 BossHpDecrement        0x65 BossSpawnNotice
//   0x67 BossHpInit2           0x68 BossHpPercent          0x86 SpawnZoneObject
//   0x93 BattlefieldStatus     0x94 DataTableLoad_1686F74  0x96 DataTableLoad1686CCC
//   0x98 AchievementDataLoad   0x9a MountTicketPrompt      0x9d BossHpBarUpdate
//   0x9e BossPanelLoad         0xa3 InstanceEnter          0xa6 HonorRankEvent
//   0xaa BattlefieldStateChange 0xb2 RankBoardLoad
//
// RULE: this module does NOT EDIT shared state (ClientRuntime.h) — it uses it.
// Unmodeled globals are stored faithfully via g_Client.Var(originalAddress).
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // net::g_GmCmdCooldownLatch
#include "Game/ClientRuntime.h"
#include "Game/MapWarp.h"      // game::BeginWarpToFactionTown (warp resolution, not the send)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (real coordinates 003.BIN)
#include "Game/GameState.h"    // game::g_World.zoneObjects (ZoneObjectEntity)
#include "Game/AutoTargetCombatGate.h" // kAutoTargetModeAddr/kAutoTargetIdHiAddr (mode 7 == zoneObjects)
#include "Game/SocialSystem.h" // game::g_Achievements (AchievementDataLoad 0x98 -> dword_184C218)
#include "Game/StringTables.h" // game::g_Strings.zoneNames (StrTable003_Get 0x4C1AD0 — 003.DAT/mZONENAME)
#include "Game/GameDatabase.h" // game::GetMonsterInfo (ItemDefTbl_GetRecord 0x4C6570 — mMONSTER)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ts2::net {

namespace {
// g_SelfMorphNpcId ranges considered "battlefield" (BattlefieldStatus 0x93 /
// BattlefieldStateChange 0xaa, RE/net_handler_notes.md): 126..137 / 171..174 / 210..233.
bool InBattlegroundMorphRange(int32_t morphId) {
    return (morphId >= 126 && morphId <= 137) ||
           (morphId >= 171 && morphId <= 174) ||
           (morphId >= 210 && morphId <= 233);
}

// StrTable003_Get(dword_84A6A8, id) 0x4C1AD0 — ZONE NAME table (003.DAT / mZONENAME),
// DISTINCT from StrTable005_Get(g_LangId, id) 0x4C1D10 (005.DAT / mMESSAGE) exposed by game::Str().
// 1-based index, empty string out of bounds (like &String in the original).
// Usage anchors in this module: Net_OnBossHpDecrement 0x4A5640 (EA 0x4A5699),
//   Net_OnBossSpawnNotice 0x4A5710 (EA 0x4A5754), Net_OnMountTicketPrompt 0x4ACA50 (EA 0x4ACB2A).
// Counterpart with INTERNAL LINKAGE to the Str3 in Net/GameHandlers_Misc.cpp (also in an
// anonymous namespace): the two TUs cannot collide on the symbol.
std::string Str3(int id) { return std::string(game::g_Strings.zoneNames.Get(id)); }

// Crt_Vsnprintf(buf, StrTable005_Get(g_LangId, strId), ...) 0x75CD5F — the FORMAT is the
// localized table entry itself (NOT a literal): see Net_OnBattlefieldStatus 0x4ABD00,
// EA 0x4ABDA5 (StrTable005_Get(g_LangId, 2231)) then EA 0x4ABDB2 (Crt_Vsnprintf(v7, v1, v3)).
// 1000-byte buffer like the original (v7[1000], [ebp-3F0h]).
std::string FmtFromStrTable(int strId, ...) {
    char buf[1000];
    va_list ap; va_start(ap, strId);
    std::vsnprintf(buf, sizeof buf, game::Str(strId).c_str(), ap);
    va_end(ap);
    return std::string(buf);
}
} // namespace

void RegisterBossWorldHandlers(NetSystem& sys) {
    using namespace game;  // g_Client, Str()

    // 0x0d ZoneChangeInfo — two opaque zone-change blobs.
    // Original: block1(1324) -> dword_1685E08, block2(2456) -> byte_1686334, then
    // World_LoadCurrentZoneModel(2) based on g_SelfMorphNpcId + flags dword_1686120..30.
    OnPacket<ZoneChangeInfo>(sys, 0x0d, [](const ZoneChangeInfo& p) {
        // Faithful copy of the 2 buffers (internal structure not decoded — raw blob,
        // see g_Client.Blob, pattern already used for byte_1686F74/1686CCC/1825E70).
        std::memcpy(g_Client.Blob(0x1685E08, sizeof p.block1).data(), p.block1, sizeof p.block1);
        std::memcpy(g_Client.Blob(0x1686334, sizeof p.block2).data(), p.block2, sizeof p.block2);
        // Anchor: Pkt_ZoneChangeInfo 0x464500 — switch @0x4645A7 on g_SelfMorphNpcId (270..274),
        //   guards @0x4645B5/0x4645CC/0x4645E3/0x4645FA/0x464611 (`== 1`), calls
        //   World_LoadCurrentZoneModel(g_GameWorld, 2) 0x4DD6E0 @0x4645BE/D5/EC/0x464603/0x46461A.
        //
        // CORRECTION OF A WRONG COMMENT (formerly "flags outside the payload"): the 5 flags ARE
        //   in the payload. block1 is copied to dword_1685E08 over 0x52C=1324 bytes (@0x464559),
        //   so it occupies [0x1685E08, 0x1686334); and 0x1686120 - 0x1685E08 = 0x318 = 792.
        //   The flags dword_1686120/24/28/2C/30 are therefore block1+792/796/800/804/808,
        //   i.e. p.block1[792..811]. Only g_SelfMorphNpcId (0x1675A98) is truly external.
        //
        // TODO(state) [anchor 0x4645BE]: the World_LoadCurrentZoneModel(2) call remains
        //   UNREACHABLE from Net/. The only C++ entry point is the AnimHost::LoadCurrentZoneModel
        //   hook (Game/AnimationTick.h:111), wired to world::WorldMap::LoadCurrentZoneModel in
        //   Scene/SceneManager.cpp:869; there is NO global instance of world::WorldMap
        //   and game::GameWorld exposes no map pointer. Same systemic gap as
        //   Net/WorldEntityDispatch.cpp:2374. The wiring must come from the owner of
        //   Game/AnimationTick.* + Scene/SceneManager.* (out of this front's scope).
    });

    // 0x17 MapObjectUpdate — opaque relay to Pkt_DispatchStorageResponse(a,b,body).
    // Anchor: Pkt_MapObjectUpdate 0x469C80 — copies payload+0 (4 bytes), payload+4 (4 bytes),
    //   payload+8 (0x64=100 bytes) then DELEGATES everything to Pkt_DispatchStorageResponse 0x58A0F0 @0x469CDA.
    //
    // SIGNATURE PROVEN BY THE ASM (the 6-arg IDA prototype is WRONG — it's the source of
    //   the "duplicated v2" in the pseudocode):
    //     char __thiscall Pkt_DispatchStorageResponse(void* this /* = g_LocalPlayerSheet 0x1685748 */,
    //                                                 int a /*payload+0*/, int subOpcode /*payload+4*/,
    //                                                 uint8_t* body /*payload+8, 100 bytes*/);
    //   Proof: only 3 pushes (@0x469CC9 body / @0x469CD0 b / @0x469CD4 a);
    //   `mov ecx, offset g_LocalPlayerSheet` @0x469CD5 (thiscall); `retn 0Ch` @0x58FE8F (12 bytes
    //   = 3 args); the stack_frame of 0x58A0F0 contains ONLY arg_0/arg_4/arg_8.
    //   SWITCH KEY = `b` (payload+4), NOT `a`: `mov ecx, [ebp+arg_4]` @0x58A12F.
    //   Live range: `sub edx, 0C9h` @0x58A13E + bound `ja def_58A167` @0x58A154 on 0x14D
    //   -> sub-codes 201..534; table byte_58FFF8 @0x58A160 -> `jmp jpt_58A167[ecx*4]` @0x58A167.
    //   88 live cases (201-256, 501-531, 534); default def_58A167 0x58FE81 = pure NO-OP (epilogue only).
    //
    // TODO(state) [anchor 0x58A0F0]: the body (23970 bytes) doesn't exist anywhere in C++. Porting
    //   it requires a dedicated module (Net/StorageResponseDispatch.cpp, GameVarDispatch.cpp model)
    //   hence an ADDITION TO THE .vcxproj — forbidden in this wave. Entry point to wire then:
    //     DispatchStorageResponse(g_LocalPlayerSheet, p.a, p.b, p.body);   // matches 0x469CDA
    //   ZERO SHORT-TERM RISK: the framing is already correct (108 bytes consumed, see MapObjectUpdate
    //   in RecvPackets.h) -> no frame desync, only the BODY is missing.
    OnPacket<MapObjectUpdate>(sys, 0x17, [](const MapObjectUpdate&) {
        // TODO(state): interpret body[100] via the storage/map sub-dispatcher.
    });

    // 0x5f BossHpInit — initializes the 1st boss bar (dword_1675BB4 = hp/2).
    OnPacket<BossHpInit>(sys, 0x5f, [](const BossHpInit& p) {
        BossBar& bar = g_Client.boss[0];
        bar.active  = true;
        bar.percent = static_cast<int>(p.hp / 2);  // dword_1675BB4
        bar.a = p.a; bar.b = p.b; bar.c = p.c; bar.d = p.d;  // dword_1675BBC..C8
        g_Client.msg.System("[" + std::to_string(bar.percent) + "]" + Str(843)); // color 1
    });

    // 0x60 ZoneBuffStatus — ON/OFF state of zone buffs per faction (4 flags).
    // Anchor: Net_OnZoneBuffStatus 0x4A52A0 — 4 dwords read at payload+0 (0x10 bytes, @0x4A52C1).
    OnPacket<ZoneBuffStatus>(sys, 0x60, [](const ZoneBuffStatus& p) {
        std::string line;
        // LITERAL binary format aSS_10 "[%s] %s " (0x7A6EAC), 3 unconditional blocks:
        //   str75..77 = faction name; str240/241 = ON/OFF (Crt_Vsnprintf @0x4A530F/92/0x4A5415).
        // The test is `== 1` (jnz @0x4A52E4 / 0x4A5367 / 0x4A53EA), NOT a truthiness check: a
        //   flag at 2 displays OFF in the binary.
        for (int i = 0; i < 3; ++i)
            line += "[" + Str(75 + i) + "] " + (p.flags[i] == 1 ? Str(240) : Str(241)) + " ";
        // 4th block: format aSS_6 "[%s] %s" (0x7A6D54, WITHOUT trailing space), and importantly
        //   gated by `cmp g_SelfMorphNpcId, 153 ; jle` @0x4A5470 -> SIGNED comparison (VarGet returns int32_t).
        if (g_Client.VarGet(WarpAddr::SelfMorphNpcId) > 153)
            line += "[" + Str(78) + "] " + (p.flags[3] == 1 ? Str(240) : Str(241));
        g_Client.msg.System(line);  // Msg_AppendSystemLine(..., 1) @0x4A5507
        // If the local faction's buff is OFF -> return to faction town. The network
        // send remains a TODO(send) internal to MapWarp.cpp (Net_SendPacket_Op20).
        // FAITHFUL ASYMMETRY (do not "harmonize"): the DISPLAY above tests `== 1`
        //   (@0x4A52E4...) while the WARP tests `== 0` (`if (!v9[g_LocalElement])` @0x4A5512,
        //   call Map_BeginWarpToFactionTown @0x4A5523) -> a flag at 2 displays OFF WITHOUT warping.
        // `g_LocalElement < 4` = defensive guard: the binary indexes v9[4] with no bound check.
        if (g_LocalElement < 4 && p.flags[g_LocalElement] == 0) {
            BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
        }
    });

    // 0x64 BossHpDecrement — decrements the remaining-boss counter + updates HP/info.
    // Anchor: Net_OnBossHpDecrement 0x4A5640 — `--dword_1675C8C` @0x4A5672 BEFORE reading the
    //   counter (@0x4A568E), then Crt_Vsnprintf(v8, aSDS_4 "%s [%d]%s", v0, v2, v3) @0x4A56AB
    //   with v0 = StrTable003_Get(dword_84A6A8, 194) @0x4A5699 and v3 = StrTable005_Get(.., 843).
    //   => output "<str003[194]> [<counter>]<str005[843]>" (the 194 comes FIRST and is from
    //   table 003, not 005: the earlier form swapped the order AND the table).
    OnPacket<BossHpDecrement>(sys, 0x64, [](const BossHpDecrement& p) {
        int32_t& remaining = g_Client.Var(0x1675C8C);  // remaining boss counter
        --remaining;
        g_Client.msg.System(Str3(194) + " [" + std::to_string(remaining) + "]" + Str(843));
        g_Client.Var(0x1675C90) = static_cast<int32_t>(p.f0);
        g_Client.Var(0x1675C94) = static_cast<int32_t>(p.f1);
        g_Client.Var(0x1675C98) = static_cast<int32_t>(p.f2);
        g_Client.Var(0x1675C9C) = static_cast<int32_t>(p.f3);
    });

    // 0x65 BossSpawnNotice — boss spawn notification.
    // Anchor: Net_OnBossSpawnNotice 0x4A5710 — Crt_Vsnprintf(v4, "%s [%d]%s", v0, value, v3)
    //   @0x4A5766 with v0 = StrTable003_Get(dword_84A6A8, 194) @0x4A5754 (table 003, NOT 005)
    //   and v3 = StrTable005_Get(g_LangId, 857) @0x4A5745. The order "%s [%d]%s" was already faithful;
    //   only the 194 was resolved in the wrong table (Str -> Str3).
    // COLOR NOT PASSED THROUGH: Msg_AppendSystemLine(.., v4, 3) @0x4A5781 — the 3 is stored RAW as
    //   an index (Msg_AppendSystemLine 0x68D9D0: *(this+4*n+37272)=a3, NOT an ARGB). msg.System
    //   expects a D3DCOLOR ARGB (ClientRuntime.h) -> passing 3 would give alpha=0 (invisible text).
    OnPacket<BossSpawnNotice>(sys, 0x65, [](const BossSpawnNotice& p) {
        g_Client.msg.System(Str3(194) + " [" + std::to_string(p.value) + "]" + Str(857));
    });

    // 0x67 BossHpInit2 — initializes the 2nd boss bar (dword_1675CB8 = hp/2).
    OnPacket<BossHpInit2>(sys, 0x67, [](const BossHpInit2& p) {
        BossBar& bar = g_Client.boss[1];
        bar.active  = true;
        bar.percent = static_cast<int>(p.hp / 2);  // dword_1675CB8
        bar.a = p.a; bar.b = p.b; bar.c = p.c; bar.d = p.d;  // dword_1675CC0..CC
        g_Client.msg.System("[" + std::to_string(bar.percent) + "]" + Str(843)); // color 1
    });

    // 0x68 BossHpPercent — displays the boss HP percentage (hp/2). No persistent state.
    OnPacket<BossHpPercent>(sys, 0x68, [](const BossHpPercent& p) {
        g_Client.msg.System("[" + std::to_string(p.hp / 2) + "]" + Str(843)); // color 1
    });

    // 0x86 SpawnZoneObject — zone object (portal/door): 19-dword stride array
    //   dword_180EEF4 == game::g_World.zoneObjects (fixed capacity 500, see GameData_InitPools
    //   / Game/MiscManagers.cpp), looked up by (idHi,idLo) — faithful to RE/net_handler_notes.md
    //   (## Pkt_SpawnZoneObject, op 0x86):
    //     action==2: slot found -> refresh spawnTimestamp + copy body; else the 1st
    //                 free slot (index < capacity, NO growth — full pool = no-op,
    //                 faithful: the binary never resizes this array).
    //     action==3: free the slot (sub_583F70 == reset to default state) and, if the
    //                 current auto-locked target pointed to THIS slot (dword_1675B24==7 &&
    //                 dword_1675B28==index, see Game/AutoTargetCombatGate.h::
    //                 kAutoTargetModeAddr/kAutoTargetIdHiAddr — mode 7 == g_World.zoneObjects),
    //                 reset dword_1675B24=0.
    //   BEFORE this wiring, g_World.zoneObjects stayed frozen at 500 inactive slots (sized
    //   by GameData_InitPools but never populated): Game/GroundAuraWorldObjectTick.h
    //   (GetWorldObjectCount/IsWorldObjectActive/TickWorldObject) was therefore reading a
    //   perpetually empty pool — network chain break -> gameplay fixed here.
    OnPacket<SpawnZoneObject>(sys, 0x86, [](const SpawnZoneObject& p) {
        auto& zones = g_World.zoneObjects;
        int foundIdx = -1, freeIdx = -1;
        for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
            const ZoneObjectEntity& z = zones[static_cast<size_t>(i)];
            if (z.active && z.objId1 == p.idHi && z.objId2 == p.idLo) { foundIdx = i; break; }
            if (freeIdx < 0 && !z.active) freeIdx = i;
        }

        if (p.action == 2) {
            const int idx = foundIdx >= 0 ? foundIdx : freeIdx;
            if (idx < 0) return; // full pool (500 slots) -> no-op, faithful (no growth).
            ZoneObjectEntity& z = zones[static_cast<size_t>(idx)];
            z.active         = true;
            z.objId1         = p.idHi;
            z.objId2         = p.idLo;
            z.spawnTimestamp = g_World.gameTimeSec;             // flt_815180 (g_GameTimeSec)
            std::memcpy(z.body.data(), p.body, sizeof p.body);
        } else if (p.action == 3 && foundIdx >= 0) {
            zones[static_cast<size_t>(foundIdx)] = ZoneObjectEntity{}; // sub_583F70 (frees the slot)
            if (g_Client.VarGet(kAutoTargetModeAddr) == 7 &&
                g_Client.VarGet(kAutoTargetIdHiAddr) == foundIdx) {
                g_Client.Var(kAutoTargetModeAddr) = 0;
            }
        }
    });

    // 0x93 BattlefieldStatus — zone war state (dword_16692A0) + messages/exit.
    // Anchor: Net_OnBattlefieldStatus 0x4ABD00 — payload layout subState:u8@+0 (v5),
    //   warState:i32@+1 (v6, -> dword_16692A0), param:i32@+5 (v8); 9 unaligned bytes.
    OnPacket<BattlefieldStatus>(sys, 0x93, [](const BattlefieldStatus& p) {
        g_Client.Var(0x16692A0) = p.warState;  // dword_16692A0 = v6 @0x4ABD70
        // Messages gated by `if (v6 == 2 && v8)` @0x4ABD86. The Str(2231) STRING is a FORMAT
        //   passed to Crt_Vsnprintf with param (LABEL_4 @0x4ABD97/0x4ABDB2) — NOT a fabricated
        //   "[param]" prefix. Str(2232) is emitted RAW (no Vsnprintf @0x4ABE69).
        if (p.warState == 2 && p.param != 0) {
            if (p.subState == 0 || (p.subState == 1 && p.param == 60)) {
                // subState==0 (else v5, LABEL_4) OR subState==1 && param==60 (case 60 -> goto
                //   LABEL_4 @0x4ABE15) -> Crt_Vsnprintf(v7, Str(2231), param).
                g_Client.msg.System(FmtFromStrTable(2231, p.param));
            } else if (p.subState == 1 &&
                       (p.param == 5 || p.param == 10 || p.param == 15 ||
                        p.param == 20 || p.param == 25 || p.param == 30)) {
                // subState==1, switch(param) cases 5/10/15/20/25/30 @0x4ABE15 -> raw Str(2232).
                g_Client.msg.System(Str(2232));
            }
            // subState==1 & param outside {5,10,15,20,25,30,60} -> default @0x4ABE15 (nothing);
            //   subState>=2 -> `jnz` @0x4ABDE1 (nothing).
        }
        // Forced exit to faction town: composite condition @0x4ABEF0 —
        //   warState != 2 && warState != 3 && dword_1674708 < 1 && dword_167588C <= 0 &&
        //   morph in [126,137]/[171,174]/[210,233], then switch(g_LocalElement) 0..3 (default =
        //   no warp @0x4ABF0D). Both the notSpecial guard and the 0..3 bound were missing:
        //   without them, warping happened in cases where the original does nothing. Aligned with
        //   the sibling 0xaa BattlefieldStateChange below.
        if (p.warState != 2 && p.warState != 3) {
            const bool notSpecial = g_Client.VarGet(0x1674708) < 1 &&   // @0x4ABEF0 (signed cmp)
                                     g_Client.VarGet(0x167588C) <= 0;
            const int32_t morph = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
            if (notSpecial && InBattlegroundMorphRange(morph) && g_LocalElement <= 3) {
                BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
            }
        }
    });

    // 0x94 DataTableLoad_1686F74 — loads a 680-byte table (byte_1686F74) if flag==0.
    OnPacket<DataTableLoad_1686F74>(sys, 0x94, [](const DataTableLoad_1686F74& p) {
        if (p.flag == 0) {
            std::memcpy(g_Client.Blob(0x1686F74, sizeof p.table).data(), p.table, sizeof p.table);
        }
    });

    // 0x96 DataTableLoad1686CCC — loads a 680-byte UI table (byte_1686CCC) if status==0.
    OnPacket<DataTableLoad1686CCC>(sys, 0x96, [](const DataTableLoad1686CCC& p) {
        if (p.status == 0) {
            std::memcpy(g_Client.Blob(0x1686CCC, sizeof p.table).data(), p.table, sizeof p.table);
        }
    });

    // 0x98 AchievementDataLoad — loads 96 bytes of achievement flags (dword_184C218).
    // g_Achievements (Game/SocialSystem.h) is the DEDICATED model for this global (consumed by
    // AchievementNotice 0x99 below via BuildAchievementNotice/PostAchievementNotice).
    OnPacket<AchievementDataLoad>(sys, 0x98, [](const AchievementDataLoad& p) {
        g_Achievements.LoadFromPayload(p.flags);
    });

    // 0x9a MountTicketPrompt — NPC notification for mount ticket (items 783..789).
    // Anchor: Net_OnMountTicketPrompt 0x4ACA50 — v6=itemId@payload+0, v5=strIndex@payload+4;
    //   result = ItemDefTbl_GetRecord(mMONSTER, itemId) @0x4ACAB1; guard
    //   `result && *result in [783,789]` @0x4ACB0F; name via StrTable003_Get(dword_84A6A8, strIndex)
    //   @0x4ACB2A; format "%s %s" -> str003[strIndex] + " " + str005[2198] @0x4ACB39.
    OnPacket<MountTicketPrompt>(sys, 0x9a, [](const MountTicketPrompt& p) {
        // Resolution ItemDefTbl_GetRecord(mMONSTER, itemId): game::GetMonsterInfo reproduces
        //   ItemDefTbl_GetRecord 0x4C6570 (1-based, guards record[0]!=0, see GameDatabase.cpp).
        //   The test is on *result = the record's 1st dword (MonsterInfo::id, +0), NOT on
        //   p.itemId: do not rely on the id==index+1 invariant. Premise of the old TODO
        //   ("no live ItemDefTbl structure") was factually wrong -> guard restored.
        const game::MonsterInfo* mi = game::GetMonsterInfo(p.itemId);
        if (mi && mi->id >= 783 && mi->id <= 789) {  // *result in [783,789] @0x4ACB0F
            // Name via StrTable003 (Str3), NOT StrTable005: the binary reads StrTable003_Get @0x4ACB2A.
            std::string t = Str3(static_cast<int>(p.strIndex)) + " " + Str(2198);
            g_Client.msg.Floating(2, 1, t);   // HUD_ShowFloatingMessage(.., 2, 1, ..) @0x4ACB53
            g_Client.msg.System(t);           // Msg_AppendSystemLine(.., g_SysMsgColor) @0x4ACB68
        }
        // Outside [783,789]: the binary emits NO message (early-out of the `if`), faithfully reproduced.
    });

    // 0x9d BossHpBarUpdate — updates the boss HP % (dword_1675E9C = hpRaw/2).
    OnPacket<BossHpBarUpdate>(sys, 0x9d, [](const BossHpBarUpdate& p) {
        int pct = static_cast<int>(p.hpRaw / 2);
        g_Client.Var(0x1675E9C) = pct;  // dword_1675E9C (boss hp %)
        std::string t = "[" + std::to_string(pct) + "]" + Str(843);
        g_Client.msg.System(t);  // color 1
        if (pct <= 30 && pct != 0)
            g_Client.msg.Floating(2, 1, t);  // low HP alert
    });

    // 0x9e BossPanelLoad — loads header + body (420 bytes) of the boss panel.
    OnPacket<BossPanelLoad>(sys, 0x9e, [](const BossPanelLoad& p) {
        std::memcpy(g_Client.boss[0].panel.data(), p.body, sizeof p.body);  // dword_1675EB0
        g_Client.Var(0x1675EA0) = static_cast<int32_t>(p.header[0]);
        g_Client.Var(0x1675EA4) = static_cast<int32_t>(p.header[1]);
        g_Client.Var(0x1675EA8) = static_cast<int32_t>(p.header[2]);
        g_Client.Var(0x1675EAC) = static_cast<int32_t>(p.header[3]);
    });

    // 0xa3 InstanceEnter — instance or event entry/result.
    OnPacket<InstanceEnter>(sys, 0xa3, [](const InstanceEnter& p) {
        auto storeParams = [&p] {
            g_Client.Var(0x1675790) = static_cast<int32_t>(p.p0);
            g_Client.Var(0x1675794) = static_cast<int32_t>(p.p1);
            g_Client.Var(0x1675798) = static_cast<int32_t>(p.p2);
            g_Client.Var(0x167579C) = static_cast<int32_t>(p.p3);
        };
        if (p.subop == 1) {
            if (p.code == 0) {
                storeParams();
                g_Client.Var(0x1823198) = 62;               // g_OpenServiceWindow=62
                for (int i = 0; i < 100; ++i)                // clears dword_1822FE0[0..99]
                    g_Client.Var(0x1822FE0 + static_cast<uint32_t>(i) * 4u) = 0;
            } else if (p.code == 1) {
                storeParams();
                g_Client.msg.System(Str(2373));
            }
        } else if (p.subop == 2) {
            if (p.code == 0) {
                storeParams();
                g_Client.msg.System(Str(2377));
                // TODO(ui): UI_NpcWin_CloseRestore (NPC window close — pure UI action,
                //   see identical TODO(ui) in GameHandlers_VendorTrade.cpp).
            } else if (p.code == 1 || p.code == 2) {
                g_Client.msg.System(Str(223));
            } else if (p.code == 3) {
                g_Client.msg.System(Str(117));
            }
        }
    });

    // 0xa6 HonorRankEvent — honor rank/PK events (messages only, except cat 3).
    OnPacket<HonorRankEvent>(sys, 0xa6, [](const HonorRankEvent& p) {
        switch (p.category) {
        case 0:  // switch(value) -> str2673..2676
            if (p.value <= 3) g_Client.msg.System(Str(2673 + static_cast<int>(p.value)));
            break;
        case 1:  // value 0..3 -> str2669..2672
            if (p.value <= 3) g_Client.msg.System(Str(2669 + static_cast<int>(p.value)));
            break;
        case 2:  // displays the 4 lines str2669..2672
            for (int i = 0; i < 4; ++i) g_Client.msg.System(Str(2669 + i));
            break;
        case 3:  // only modified state: dword_16760F4
            g_Client.Var(0x16760F4) = static_cast<int32_t>(p.value);
            if (p.value >= 1 && p.value <= 3) {
                // 2 multi-fragment lines (str2532 / 2685..2688 / 377 / 378) based on value.
                g_Client.msg.System(Str(2532) + " " + Str(2684 + static_cast<int>(p.value)));
                g_Client.msg.System(Str(377) + " " + Str(378));
            }
            // Complete: RE/net_handler_notes.md confirms no other state is modified
            // ("No game state modified besides dword_16760F4").
            break;
        default:
            break;
        }
    });

    // 0xaa BattlefieldStateChange — battlefield state change (dword_16692A0).
    OnPacket<BattlefieldStateChange>(sys, 0xaa, [](const BattlefieldStateChange& p) {
        g_Client.Var(0x16692A0) = static_cast<int32_t>(p.value);  // BG state
        g_Client.msg.System(Str(p.state == 0 ? 2537 : 2538));
        // If value!=3, player NOT special (dword_1674708<1 && dword_167588C<=0) and
        // g_SelfMorphNpcId within a BG range -> ejected to faction town
        // (RE/net_handler_notes.md ## BattlefieldStateChange).
        if (p.value != 3) {
            const bool notSpecial = g_Client.VarGet(0x1674708) < 1 &&
                                     g_Client.VarGet(0x167588C) <= 0;
            const int32_t morph = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
            if (notSpecial && InBattlegroundMorphRange(morph) &&
                g_LocalElement <= 3) {
                BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
            }
        }
    });

    // 0xb2 RankBoardLoad — loads the ranking board (header + 600-byte body).
    OnPacket<RankBoardLoad>(sys, 0xb2, [](const RankBoardLoad& p) {
        g_Client.Var(0x18260C8) = static_cast<int32_t>(p.header);        // total count
        g_Client.Var(0x18260D0) = static_cast<int32_t>(p.header / 10);   // page count
        g_GmCmdCooldownLatch = 0;
        // Raw body (ranking entry structure not decoded by the original
        // handler — faithful blob, see g_Client.Blob).
        std::memcpy(g_Client.Blob(0x1825E70, sizeof p.body).data(), p.body, sizeof p.body);
    });
}

} // namespace ts2::net

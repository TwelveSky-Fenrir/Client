// Game/MapWarp.cpp — implementation of the faction warp system (see MapWarp.h).
//
// Faithful translation of the 4 disassembled functions (EA in each block's comment).
// Scope reminder: this module sets the warp INTENT (guards + staging globals)
// without performing the actual network send or map load (TODOs noted).
#include "Game/MapWarp.h"
#include "Net/SendPackets.h"   // pulls in Net/NetClient.h + Net/Rng.h (DefaultRng, Net_SendWarpRequest)
#include <cstring>

namespace ts2::game {
namespace {

// Unaligned little-endian int32 read (same pattern as the local RdI32 in Game/EntityManager.cpp).
int32_t Rd32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

// *(g_LocalPlayerSheet + 1784) — local player action state, actually
// players[0]+244 (see derivation at the top of MapWarp.h). g_World.Self() creates slot 0
// if it doesn't exist yet (GameState.h behavior, unchanged here).
int32_t SelfActionState() {
    PlayerEntity& self = g_World.Self();
    if (kSelfActionStateOffset + 4 > self.body.size()) return 0;
    return Rd32(self.body.data() + kSelfActionStateOffset);
}

// flt_1675AC4 = (float)(Rng_Next() % 360) — Rng_Next 0x7603FD (net::DefaultRng, SAME rand()
// stream as the builders' nonces: 1 facing draw THEN 4 nonce draws per warp, see the
// shared ordering in ComboPickupTick.cpp:120-124). Sites: 0x55c64e/0x55c85e/0x55cba8 +
// 0x5f5daf (Warp_SendTeleport). The facing angle itself is cosmetic but the draw ORDER
// must stay faithful (facing consumes one draw before the nonces of the packet sent next).
float RandomFacingDeg() {
    return static_cast<float>(ts2::net::DefaultRng().NextMod(360));
}

// Common arming block for the full warp (dword_1675A8C.. flt_1675AC8), faithful to the 3
// functions that execute it (Town/Default/Ex: same 10 writes; Map37: same shape
// with fixed coordinates, see BeginWarpToMap37). Corresponds to:
//   Crt_Memset(&dword_1675AA0, 0, 72); dword_1675AA0=0; dword_1675AA4=flagA4;
//   flt_1675AA8=0.0; flt_1675AAC/AB0/AB4=x/y/z; flt_1675AC4=flt_1675AC8=rand()%360;
// (the 72-byte memset is equivalent here to "any Var() not explicitly rewritten stays at its
// default value 0" — g_Client.Var() returns 0 for a key never set.)
void ArmFullWarp(FactionWarpResolution& r, int32_t warpModeCode, int32_t flagA4,
                  int32_t townNpcId, float x, float y, float z,
                  ts2::net::NetClient* nc = nullptr) {
    g_Client.Var(WarpAddr::MorphInProgress) = 1;          // /*g_MorphInProgress = 1*/
    g_Client.Var(WarpAddr::WarpModeCode)    = warpModeCode;
    g_Client.Var(WarpAddr::WarpSub)         = 0;
    g_Client.Var(WarpAddr::WarpTargetNpc)   = townNpcId;
    // M8 — g_TargetZoneId = townNpcId (0x1675A9C): EA 0x55c69a/0x55c5ee (non-Ex),
    // Default/Map37/Ex counterparts and 0x5f5d46 (teleport). WarpTargetNpc above IS already
    // g_TargetZoneId (see MapWarp.h WarpAddr::WarpTargetNpc == 0x1675A9C); here we set the
    // MIRROR consumed by SceneManager to reload the CORRECT zone (without it, the reload
    // falls back to the current zone). GameState.h:545 documents pendingWarpZoneId == 0x1675A9C.
    g_World.pendingWarpZoneId = townNpcId;                 // g_TargetZoneId 0x1675A9C
    g_Client.Var(WarpAddr::WarpFlagA0)      = 0;
    g_Client.Var(WarpAddr::WarpFlagA4)      = flagA4;
    g_Client.VarF(WarpAddr::WarpDelay)      = 0.0f;
    g_Client.VarF(WarpAddr::WarpPosX)       = x;
    g_Client.VarF(WarpAddr::WarpPosY)       = y;
    g_Client.VarF(WarpAddr::WarpPosZ)       = z;
    const float facing = RandomFacingDeg();
    g_Client.VarF(WarpAddr::WarpFacingA)    = facing;
    g_Client.VarF(WarpAddr::WarpFacingB)    = facing;

    r.action       = WarpAction::ArmFullWarp;
    r.warpModeCode = warpModeCode;
    r.facingDeg    = facing;
    r.townNpcId    = townNpcId;
    r.x = x; r.y = y; r.z = z;

    // Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C, v4) — UNCONDITIONAL in the binary:
    //   EA 0x55C66F / 0x55C87F / 0x55C993 / 0x55CBC9 (Town/Default/Map37/Ex) and 0x5F5DD6
    //   (Warp_SendTeleport). Sent via the i32 alias Net_SendWarpRequest: townNpcId
    //   (140/138/139/165/166) >= 128 is ZERO-extended to 32 bits (see Net/SendPackets.cpp — the
    //   shared int8_t builder would sign-extend incorrectly). H1: since param `nc` is nullptr for ALL
    //   current callers, we send via the global singleton net::g_NetClient 0x8156A0
    //   (GlobalNetClient()) — this is EXACTLY what the binary does (Op20 addresses g_NetClient
    //   directly). The `if (client)` guard remains a rewrite-time safety net (null pointer as long
    //   as no connection has been established; a warp only happens in-game, post-handshake) — not a
    //   fidelity gap. Op20's 4 nonce draws follow the facing draw above:
    //   the now-REAL send restores the faithful RNG stream (previously these 4 draws were
    //   skipped -> desync vs the binary).
    ts2::net::NetClient* client = nc ? nc : ts2::net::GlobalNetClient();
    if (client) ts2::net::Net_SendWarpRequest(*client, warpModeCode, townNpcId);
    // TODO(world) on server response (op 0x0d ZoneChangeInfo, see World/WorldMap.*/World_LoadMap
    //   already written elsewhere): actually load the target map and position the actor at
    //   (x,y,z)/facing above, then reset g_MorphInProgress to 0 (observed in
    //   Net_OnGameServerConnectResult, error codes 1..12).
}

// Op99 (0x63) — Net_SendOp99/Net_SendAutoHuntSync 0x4BD140. The binary reads the blobs GLOBALLY
// (Crt_Memcpy(this+13, byte_16755B0, 0x44) @0x4bd1f5; Crt_Memcpy(this+81, &g_AutoHuntMode 0x16755F4,
// 0x2C) @0x4bd20b) — these are NOT parameters. In the current modeled state these blobs are
// ENTIRELY ZERO: byte_16755B0 is BSS never written (xrefs_to 0x16755B0 = 1, its only
// read is 0x4bd1e9) and neither the quick-skills grid 0x16755B4 (64 B) nor the auto-hunt config
// 0x16755F4 (44 B) have ANY writer in ClientSource (grep 0 hits). What matters for protocol
// fidelity is INDEPENDENT of the blob contents: packet present, seq++ (@0x4bd2cf) and 4 RNG
// draws (@0x4bd157..18b) — a sequence desync would corrupt the ENTIRE session. Sent via the
// singleton (the binary addresses g_NetClient directly), same pattern as ArmFullWarp.
// TODO(state) anchored: wire up the real blobs (byte_16755B0 68 B + g_AutoHuntMode 0x16755F4 44 B,
//   owned by AutoPlaySystem, out of scope) once the auto-hunt config is modeled.
void EmitAutoHuntSync(ts2::net::NetClient* nc) {
    static const uint8_t kBlob68[68] = {0}; // byte_16755B0 (this+13, 68 B) — zero proven/modeled
    static const uint8_t kBlob44[44] = {0}; // g_AutoHuntMode 0x16755F4 (this+81, 44 B) — zero modeled
    if (auto* c = nc ? nc : ts2::net::GlobalNetClient())
        ts2::net::Net_SendAutoHuntSync(*c, 0, kBlob68, kBlob44); // a2=0 (@0x55ca9c/0x55cad9/0x55cb21)
}

// Resolves x/y/z via the optional resolver (else 0/0/0, see IFactionTownCoordResolver).
void ResolveCoords(int32_t element, int32_t townNpcId, const IFactionTownCoordResolver* resolver,
                    FactionWarpResolution& r) {
    if (resolver && resolver->ResolveTownCoords(element, townNpcId, r.x, r.y, r.z)) {
        r.coordsResolved = true;
    }
    // TODO(asset) without a resolver: Motion_GetComboOffsetTable 0x5025E0(element, townNpcId) then,
    //   on failure, GInfo2_GetVec3 0x4FD4C0(flt_1555D08, townNpcId) — NPC/motion tables (.IMG),
    //   out of scope for this module (see IFactionTownCoordResolver in MapWarp.h).
}

} // namespace

// Map_BeginWarpToFactionTown 0x55C510 / Map_BeginWarpToFactionTownEx 0x55C9A0.
FactionWarpResolution BeginWarpToFactionTown(int32_t element, bool ex, int32_t mode,
                                              const IFactionTownCoordResolver* resolver,
                                              ts2::net::NetClient* nc) {
    FactionWarpResolution r{};
    r.element   = element;
    r.townNpcId = FactionTownNpcId(element);
    if (r.townNpcId == 0) {
        // switch(g_LocalElement) default -> v4 stays 0 -> no town, silent abort.
        r.valid = false;
        return r;
    }
    r.valid = true;

    const bool    selfDead         = (SelfActionState() == kDeathRespawnState);
    const int32_t currentTownNpcId = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));

    if (!ex) {
        // 0x55C529: if (*(this+1784) != 12 || a2) { ... } else return;
        //   -> blocked ONLY if dead AND mode not forced (mode==0).
        if (selfDead && mode == 0) {
            r.blockedByDeath = true;
            return r;
        }
        // 0x55C581: if (v4 != g_SelfMorphNpcId) { ... } — else no-op (already there).
        if (r.townNpcId == currentTownNpcId) {
            r.action = WarpAction::None; // already there: the binary does NOTHING here
            return r;
        }
        ResolveCoords(element, r.townNpcId, resolver, r);
        // 0x55C5BD: if (!g_MorphInProgress) { arm + send } else no-op.
        if (g_Client.VarGet(WarpAddr::MorphInProgress) != 0) {
            r.blockedByMorphInProgress = true;
            return r;
        }
        // 0x55C5C7: a2 (mode) != 0 -> code 3 / flagA4=0; a2==0 -> code 7 / flagA4=1.
        const int32_t warpModeCode = (mode != 0) ? 3 : 7;
        const int32_t flagA4       = (mode != 0) ? 0 : 1;
        ArmFullWarp(r, warpModeCode, flagA4, r.townNpcId, r.x, r.y, r.z, nc);
        return r;
    }

    // --- Ex (0x55C9A0) ---------------------------------------------------
    // 0x55C9BA..0x55C9E1: guard by mode.
    //   mode==0: blocked if dead (state==12).
    //   mode==1: blocked UNLESS dead ("dead" state required — this is the call made by
    //             Char_TickDeathRespawn 0x577AEE on the player's actual death).
    //   other mode (2, 3, ...): no guard here (original behavior, never observed
    //   at a real call site — the 6 known xrefs only use 0 or 1).
    if (mode == 0) {
        if (selfDead) { r.blockedByDeath = true; return r; }
    } else if (mode == 1) {
        if (!selfDead) { r.blockedByDeath = true; return r; }
    }

    // 0x55CA41: coordinate resolution happens HERE, unconditionally (contrast
    // with the non-Ex variant where it's deferred until after the "already there" test).
    ResolveCoords(element, r.townNpcId, resolver, r);

    if (r.townNpcId == currentTownNpcId) {
        // 0x55CA65: already at the right town/morph.
        if (mode == 0 || mode == 1) {
            // 0x55CA82 (mode0) / 0x55CAA7 (mode1): no map warp, just a
            // local repositioning + network confirmation.
            r.action = WarpAction::MoveInPlace;
            g_Client.Var(WarpAddr::InvDirtyEnable) = 0; // g_InvDirtyEnable=0  EA 0x55CA87 (mode0) / 0x55CAC3 (mode1)
            // TODO(net) mode0: Net_QueueMoveTo(&g_PlayerCmdController, {x,y,z}, 0,-1,0,0,0,0)
            //   EA 0x55CA82 (Net_QueueMoveTo 0x5119B0); mode1: Net_QueueRespawnMove(...)
            //   EA 0x55CABE (Net_QueueRespawnMove 0x5117A0) -> op1/op0 emitters (via Op15) of
            //   g_PlayerCmdController: module NOT implemented / NOT owned by this front.
            // Op99 sent AFTER g_InvDirtyEnable=0 — EA 0x55CA9C (mode0) / 0x55CAD9 (mode1). Both
            //   branches send an IDENTICAL Op99 (Net_SendOp99(&g_AutoPlayMgr, 0)) -> a single
            //   call here covers whichever mode was actually taken. See EmitAutoHuntSync (blobs proven zero).
            EmitAutoHuntSync(nc);
        } else {
            r.action = WarpAction::None; // unknown mode: the binary does nothing either
        }
        return r;
    }

    // 0x55CAF9..0x55CB06: g_MorphInProgress != 1 && !g_GmCmdCooldownLatch && !g_MorphInProgress
    // (double test equivalent to !g_MorphInProgress as long as this flag only holds 0/1, see
    // comment in MapWarp.h) — blocked if a morph is already armed OR if the cooldown
    // latch is set (generic anti-spam latch, reused elsewhere by
    // administration commands out of scope — not relevant here, just a guard value read).
    const bool morphInProgress = g_Client.VarGet(WarpAddr::MorphInProgress) != 0;
    const bool cooldownLatched = g_Client.VarGet(WarpAddr::CooldownLatch) != 0;
    if (morphInProgress || cooldownLatched) {
        r.blockedByMorphInProgress = morphInProgress;
        r.blockedByCooldown        = cooldownLatched;
        return r;
    }

    g_Client.Var(WarpAddr::InvDirtyEnable) = 0; // g_InvDirtyEnable=0  EA 0x55CB0C
    // Op99 sent BEFORE ArmFullWarp's Op20 — EA 0x55CB21. The binary order (Op99 THEN facing THEN
    //   Op20) is thus mechanically preserved: Op99 draws 4 nonces (seq++), ArmFullWarp then
    //   draws facing (1 draw) then Op20 (4 nonces, seq++). See EmitAutoHuntSync above.
    EmitAutoHuntSync(nc);
    ArmFullWarp(r, /*warpModeCode=*/7, /*flagA4=*/1, r.townNpcId, r.x, r.y, r.z, nc); // 0x55CB26..0x55CBC9
    return r;
}

// Map_BeginWarpToFactionTownDefault 0x55C740 — identical to the non-Ex branch above,
// WITHOUT the "dead" guard (no `this`/state read at function entry) and always in
// "normal" mode (code 7, flagA4=1 — same constants as a2==0 in the non-Ex variant).
FactionWarpResolution BeginWarpToFactionTownDefault(int32_t element,
                                                     const IFactionTownCoordResolver* resolver,
                                                     ts2::net::NetClient* nc) {
    FactionWarpResolution r{};
    r.element   = element;
    r.townNpcId = FactionTownNpcId(element);
    if (r.townNpcId == 0) { // 0x55C761 default -> v2 stays 0
        r.valid = false;
        return r;
    }
    r.valid = true;

    const int32_t currentTownNpcId = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
    if (r.townNpcId == currentTownNpcId) { // 0x55C799
        r.action = WarpAction::None;
        return r;
    }
    ResolveCoords(element, r.townNpcId, resolver, r); // 0x55C7B4/0x55C7CA

    if (g_Client.VarGet(WarpAddr::MorphInProgress) != 0) { // 0x55C7D6
        r.blockedByMorphInProgress = true;
        return r;
    }
    ArmFullWarp(r, /*warpModeCode=*/7, /*flagA4=*/1, r.townNpcId, r.x, r.y, r.z, nc); // 0x55C7E6..0x55C87F
    return r;
}

// Map_BeginWarpToMap37 0x55C8A0 — fixed target (id 37), EXACT hardcoded coordinates.
FactionWarpResolution BeginWarpToMap37(ts2::net::NetClient* nc) {
    FactionWarpResolution r{};
    r.element   = -1;   // not applicable: this variant doesn't depend on g_LocalElement
    r.townNpcId = 37;
    r.valid     = true;

    const bool    selfDead         = (SelfActionState() == kDeathRespawnState); // *(this+1784)!=12
    const int32_t currentTownNpcId = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));

    // 0x55C8EA: *(this+1784)!=12 && g_SelfMorphNpcId!=37 && !g_MorphInProgress.
    if (selfDead) { r.blockedByDeath = true; return r; }
    if (currentTownNpcId == 37) { r.action = WarpAction::None; return r; } // already there
    if (g_Client.VarGet(WarpAddr::MorphInProgress) != 0) {
        r.blockedByMorphInProgress = true;
        return r;
    }

    // Hardcoded coordinates in the binary — NO resolver needed here.
    r.coordsResolved = true;
    ArmFullWarp(r, /*warpModeCode=*/11, /*flagA4=*/1, /*townNpcId=*/37,
                /*x=*/6.0f, /*y=*/97.0f, /*z=*/-3259.0f, nc); // 0x55C8FA..0x55C993
    return r;
}

// Warp_SendTeleport 0x5F5CE0 — __stdcall(u16 a1, float* a2). Body = strictly
// ArmFullWarp(mode=6, flagA4=1, townNpcId=v3[a1], x/y/z=a2[0..2]) guarded by
// (a1<=3 && !g_MorphInProgress), followed by Op20(nc, 6, v3[a1]) (EA 0x5f5dd6).
bool Warp_SendTeleport(uint16_t zoneSel, const float* pos, ts2::net::NetClient* nc) {
    // v3[4] = {138,139,165,166} — EA 0x5f5ce9 / f0 / f7 / fe.
    static constexpr int32_t kTeleportZoneIds[4] = {138, 139, 165, 166};
    // 0x5f5d1a: if (a1 <= 3u && !g_MorphInProgress) — else returns a1 without doing anything.
    if (zoneSel > 3u || g_Client.VarGet(WarpAddr::MorphInProgress) != 0) return false;

    FactionWarpResolution r{};
    r.element        = -1;                        // not applicable: direct zone teleport
    r.townNpcId      = kTeleportZoneIds[zoneSel]; // g_TargetZoneId = v3[a1]  EA 0x5f5d46
    r.valid          = true;
    r.coordsResolved = true;
    r.x = pos[0]; r.y = pos[1]; r.z = pos[2];     // flt_1675AAC/AB0/AB4  EA 0x5f5d7e/8a/96
    // ArmFullWarp reproduces 0x5f5d20..0x5f5dbb (MorphInProgress=1, mode=6, sub=0, memset72,
    // flagA0=0, flagA4=1, delay=0, x/y/z, facing=Rng%360 x2) + Op20(nc,6,zone) EA 0x5f5dd6.
    ArmFullWarp(r, /*warpModeCode=*/6, /*flagA4=*/1, r.townNpcId, r.x, r.y, r.z, nc); // 0x5f5d20..0x5f5dd6
    return true;
}

} // namespace ts2::game

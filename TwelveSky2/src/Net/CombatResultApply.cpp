// Net/CombatResultApply.cpp — byte-exact translation of cGameData_ApplyCombatResult
// (EA 0x55A380) + trampoline Pkt_OnCombatResult (EA 0x468340, op 0x15).
//
// SOURCE OF TRUTH: IDA disassembly (idaTs2). Every field of the 76-byte block, every
// threshold, every operation order and every clamp is taken as-is from the decompiled code.
//
// -----------------------------------------------------------------------------
// BLOCK LAYOUT (76 bytes = 19 DWORD, all read as _DWORD in the binary)
//   a[0]  (+0x00) action type          : 1/2 = PvP attack, 3 = monster target,
//                                        4 = monster attacker. (switch @0x55A3CA)
//   a[1]  (+0x04) attacker.id.hi       (compared to player[i]+6896)
//   a[2]  (+0x08) attacker.id.lo       (compared to player[i]+6900)
//   a[3]  (+0x0C) target.id.hi         (compared to player/monster +6896 / +923696)
//   a[4]  (+0x10) target.id.lo         (compared to +6900 / +923700)
//   a[5..7]       unused by this decoder
//   a[8]  (+0x20) attack category      : 1 = weapon, 2 = skill (sound/fx choice)
//   a[9]  (+0x24) skill id             (sound/fx variant; 78 = ignored case)
//   a[10..11]     unused
//   a[12] (+0x30) "hit": 0 = miss/dodge; else = (weaponId+1) -> MobDb_GetEntry
//   a[13] (+0x34) crit flag            : bit0 = external crit, bit1 = internal crit
//                                        (0=none, 1=ext, 2=int, 3=both)
//   a[14] (+0x38) defense / absorbed
//   a[15] (+0x3C) total external damage (before defense)  -> log text
//   a[16] (+0x40) external damage APPLIED to HP
//   a[17] (+0x44) total internal damage (before defense)  -> log text
//   a[18] (+0x48) internal damage APPLIED to HP
//
// Fidelity note: player HP takes a[16] THEN a[18] (both on the same HP field
// +7208); monster HP only takes a[16]. No MP field is touched by this packet.
// The "self" entity is players[0] (== cGameData player[0]).
// -----------------------------------------------------------------------------
#include "Net/CombatResultApply.h"

#include "Game/GameState.h"      // g_World, PlayerEntity, MonsterEntity, EntityId
#include "Game/ClientRuntime.h"  // g_Client, game::Str(id)
#include "Gfx/FxSetters.h"       // FxPool_* (pool dword_17D06F4) + Fx_Attach* (combat setters)

#include <cstdio>   // snprintf
#include <cstdint>
#include <cstring>  // std::memcpy — mirrors Crt_Memcpy(..., a4, 0x4Cu)
#include <array>
#include <string>
#include <unordered_map>

namespace ts2::game {
namespace {

// Original address of the "local player dead" flag (dword_16760D0), stored in the
// long tail of globals via g_Client.Var(addr) — see GOLDEN RULE in ClientRuntime.h.
constexpr uint32_t kAddr_SelfDeadFlag = 0x16760D0;

// Log-line color/category. The binary passes a category code to
// Msg_AppendSystemLine (EA 0x68D9D0), which stores it as-is (+37272); rendering
// then resolves the color via a table indexed on this code. Lacking that table
// here, the 4 categories used are mapped to STABLE placeholder ARGB values.
// TODO(color): wire up the binary's real category->D3DCOLOR table.
uint32_t CouleurLigne(int categorie) {
    switch (categorie) {
        case 2:  return 0xFFFF6060u; // damage taken (self = target)
        case 3:  return 0xFFFFC040u; // damage dealt (self = attacker)
        case 17: return 0xFFB0B0B0u; // "you missed" (self attacking, miss)
        case 21: return 0xFFB0B0B0u; // "the attacker missed you" (self is target, miss)
        default: return 0xFFFFFFFFu;
    }
}

// Emits a system log line (Msg_AppendSystemLine(texte, categorie)).
void LigneSysteme(const std::string& texte, int categorie) {
    g_Client.msg.System(texte, CouleurLigne(categorie));
}

// snprintf on formats "%s(+%d)" / "%s(-%d)" / " %s(+%d)" / " %s(-%d)"
// (Crt_Vsnprintf @0x75CD5F) then concatenation (Crt_Strcat @0x75CAC0).
std::string Fmt(const char* format, const std::string& s, int d) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), format, s.c_str(), d);
    return std::string(buf);
}

// Linear search for an active player by network identity. Returns the index (order
// of g_World.players, index 0 = self) or -1. Mirrors the binary's scan loops
// (test player[i].active && id.hi==a[hi] && id.lo==a[lo]).
int TrouverJoueur(const EntityId& id) {
    for (size_t i = 0; i < g_World.players.size(); ++i) {
        const PlayerEntity& p = g_World.players[i];
        if (p.active && p.id.hi == id.hi && p.id.lo == id.lo)
            return static_cast<int>(i);
    }
    return -1;
}

// Same for monsters (original stride-280 array).
int TrouverMonstre(const EntityId& id) {
    for (size_t i = 0; i < g_World.monsters.size(); ++i) {
        const MonsterEntity& m = g_World.monsters[i];
        if (m.active && m.id.hi == id.hi && m.id.lo == id.lo)
            return static_cast<int>(i);
    }
    return -1;
}

// "self" = players[0] (== cGameData player[0]).
bool SelfEst(const EntityId& id) {
    return !g_World.players.empty() && g_World.players[0].id.hi == id.hi
                                    && g_World.players[0].id.lo == id.lo;
}

// self.mode (cGameData+7136): gates the Net_QueueAction9/10 network resends for
// modes {1,5,6,7}. Read from g_World.self.mode (GameState.h line 334, read-only;
// field added by MAIN, mirrors cGameData+7136). Same gate as the binary's 4 sites:
//   0x55AA5D (case 1/2 hit) / 0x55A4D0 (case 1/2 miss) /
//   0x55BC38 (case 4 hit)  / 0x55BAA3 (case 4 miss).
bool SelfModeDeclenche() {
    const int m = g_World.self.mode;             // cGameData+7136 (g_World.self.mode)
    return m == 1 || m == 5 || m == 6 || m == 7; // {1,5,6,7}
}

// Cache of the last 76-byte combat block per entity — mirrors Crt_Memcpy(..., a4, 0x4Cu)
// to player[i]+7536 (0x55AC92, 0x55BE85) and monster[i]+923816 (0x55B51F). GameState.h
// (PlayerEntity/MonsterEntity) is read-only for this front-end -> state is modeled
// here (rule #6). Key = network identity (hi<<32 | lo). Future consumer: anim/FX.
struct BlocCombat { std::array<uint8_t, 76> data{}; bool valid = false; };

std::unordered_map<uint64_t, BlocCombat>& CacheJoueur() {
    static std::unordered_map<uint64_t, BlocCombat> m; return m;
}
std::unordered_map<uint64_t, BlocCombat>& CacheMonstre() {
    static std::unordered_map<uint64_t, BlocCombat> m; return m;
}
uint64_t CleId(const EntityId& id) {
    return (static_cast<uint64_t>(id.hi) << 32) | id.lo;
}
void StockerBlocCombat(std::unordered_map<uint64_t, BlocCombat>& cache,
                       const EntityId& id, const uint8_t* block) {
    BlocCombat& b = cache[CleId(id)];
    std::memcpy(b.data.data(), block, 76);   // Crt_Memcpy(..., a4, 0x4Cu)
    b.valid = true;
}

// -----------------------------------------------------------------------------
// FxEntitySource view (a2[N] fields read by the Fx_Attach* setters) from a C++ entity.
// Setters receive the record ADDRESS (&player[i]=this+908·i+6892 / &monster[i]=this+
// 280·i+923692); only proven fields are transposed here. See Gfx/FxSetters.h.
gfx::FxEntitySource SourceFromPlayer(const PlayerEntity& p) {
    gfx::FxEntitySource s;
    s.idHi = p.id.hi;                       // a2[1]  entity+4
    s.idLo = p.id.lo;                       // a2[2]  entity+8
    // a2[6] = entity+24 = body[0]: "model loaded" gate (non-zero once appearance is received).
    if (p.body.size() >= 4)
        std::memcpy(&s.modelReady, p.body.data(), 4);
    s.modelClass    = p.anim.modelIndex;    // a2[23] entity+92 (aTribe RACE 0..2)
    s.modelSubclass = p.anim.modelVariant;  // a2[24] entity+96 (aGender 0..1)
    return s;
}
gfx::FxEntitySource SourceFromMonster(const MonsterEntity& m) {
    gfx::FxEntitySource s;
    s.idHi = m.id.hi;   // a2[1]  record+4
    s.idLo = m.id.lo;   // a2[2]  record+8
    // Monster setters (MuzzleVariant particle / Parry-Deflect mesh) only read the id
    // for the VISIBLE path; the anchor (a2[24]=record+96=model def, +244) belongs to the
    // model subsystem → TODO on the setter side. modelClass/Subclass not transposed (not required).
    return s;
}

// Allocates a free FX slot and returns its pointer, or nullptr if the pool is full. Mirrors the
// shared pattern `for(j…) if(j<g_FxAuraCount) Fx_AttachXxx(&slot[j], …)` (e.g. @0x55ab24/@0x55ab82).
gfx::FxSlot* SlotLibreFx() {
    const int j = gfx::FxPool_FindFreeSlot();
    return (j >= 0) ? &gfx::FxPool_Slots()[j] : nullptr;
}

} // namespace

// =============================================================================
// cGameData_ApplyCombatResult — EA 0x55A380
// =============================================================================
void ApplyCombatResult(const uint8_t* block, uint32_t len) {
    if (!block || len < 76)
        return;

    // All fields are read as 32-bit (signed when compared to 0 / subtracted).
    const int32_t* a = reinterpret_cast<const int32_t*>(block);

    const EntityId atk{ static_cast<uint32_t>(a[1]), static_cast<uint32_t>(a[2]) };
    const EntityId tgt{ static_cast<uint32_t>(a[3]), static_cast<uint32_t>(a[4]) };

    switch (a[0]) {                         // switch @0x55A3CA
    // -------------------------------------------------------------------------
    // Case 1 & 2: player -> player attack.
    // -------------------------------------------------------------------------
    case 1:
    case 2:
        if (a[12]) {                        // hit (@0x55A3D4)
            // --- Block A: self is the ATTACKER -> "damage dealt" log ---
            if (SelfEst(atk)) {             // @0x55A510
                std::string s = Str(198);
                int v33 = a[15] - a[14];    // total ext - defense (@0x55A541)
                if (v33 < 0) v33 = 0;
                if (a[13] == 1 || a[13] == 3)               // external crit
                    s += Fmt("%s(+%d)", Str(200), v33);
                else
                    s += Fmt("%s(+%d)", Str(199), v33);
                if (a[14] > 0)                              // absorbed portion
                    s += Fmt(" %s(+%d)", Str(201), a[14]);
                LigneSysteme(s, 3);

                if (a[17] > 0) {                            // internal damage section
                    std::string s2 = Str(2793);
                    int v34 = a[17] - a[14];                // (@0x55A67E) with clamp
                    if (v34 < 0) v34 = 0;
                    if (a[13] == 2 || a[13] == 3)           // internal crit
                        s2 += Fmt("%s(+%d)", Str(200), v34);
                    else
                        s2 += Fmt("%s(+%d)", Str(199), v34);
                    if (a[14] > 0)
                        s2 += Fmt(" %s(+%d)", Str(201), a[14]);
                    LigneSysteme(s2, 3);
                }
            }

            // --- Block B: self is the TARGET -> "damage taken" log ---
            if (SelfEst(tgt)) {             // @0x55A7AD
                std::string s = Str(203);
                if (a[13] == 1 || a[13] == 3) {             // external crit
                    int v20 = a[15] - a[14];                // (@0x55A7F0) NO clamp
                    s += Fmt("%s(-%d)", Str(200), v20);
                } else {
                    int v21 = a[15] - a[14];                // (@0x55A839) NO clamp
                    s += Fmt("%s(-%d)", Str(199), v21);
                }
                if (a[14] > 0)
                    s += Fmt(" %s(-%d)", Str(201), a[14]);
                LigneSysteme(s, 2);

                if (a[17] > 0) {                            // internal damage section
                    std::string s2 = Str(2793);
                    if (a[13] == 2 || a[13] == 3) {
                        int v23 = a[17] - a[14];            // (@0x55A91B) NO clamp
                        s2 += Fmt("%s(-%d)", Str(200), v23);
                    } else {
                        int v24 = a[17] - a[14];            // (@0x55A964) NO clamp
                        s2 += Fmt("%s(-%d)", Str(199), v24);
                    }
                    if (a[14] > 0)
                        s2 += Fmt(" %s(-%d)", Str(201), a[14]);
                    LigneSysteme(s2, 2);
                }

                // "Dead" flag: self current HP - ext damage - int damage < 1
                // (@0x55AA17). Reads players[0].hp BEFORE the subtraction below.
                const int hpSelf = g_World.players.empty() ? 0 : g_World.players[0].hp;
                if (hpSelf - a[16] - a[18] < 1)
                    g_Client.Var(kAddr_SelfDeadFlag) = 1;   // dword_16760D0 = 1

                if (SelfModeDeclenche()) {
                    // TODO(net) @0x55AA64 : Net_QueueAction10(&g_PlayerCmdController)
                }
            }

            // --- Apply damage to the target PLAYER (a[3],a[4]) ---
            {
                int i = TrouverJoueur(tgt);                 // loop @0x55AA69
                if (i >= 0) {
                    PlayerEntity& p = g_World.players[i];
                    if (a[13] == 1) {
                        // @0x55ABC4: block-guard on external crit. Scan free slot @0x55ab24 then
                        //   Fx_AttachBlockGuard(&slot, 1, &player[i], i==0).
                        if (gfx::FxSlot* fx = SlotLibreFx())
                            gfx::Fx_AttachBlockGuard(fx, 1, SourceFromPlayer(p), i == 0);
                    }
                    p.hp -= a[16];          // external damage (@0x55ABFA)
                    p.hp -= a[18];          // internal damage  (@0x55AC32)
                    if (p.hp < 0) p.hp = 0;
                    if (i == 0) g_World.self.hp = p.hp;     // HUD self mirror
                    // Crt_Memcpy(player[i]+7536, block, 76) @0x55AC92 — combat block
                    //   cache on the entity (modeled off-struct, see rule #6).
                    StockerBlocCombat(CacheJoueur(), tgt, block);
                    // @0x55AD2C: muzzle flash on the hit target. Scan free slot @0x55ac9a then
                    //   Fx_AttachMuzzleFlash(&slot, &player[i], 1).
                    if (gfx::FxSlot* fx = SlotLibreFx())
                        gfx::Fx_AttachMuzzleFlash(fx, SourceFromPlayer(p), 1);
                }
            }

            // --- Positional sound on the attacker PLAYER (a[1],a[2]) ---
            {
                int i = TrouverJoueur(atk);                 // loop @0x55AD31
                if (i >= 0) {
                    if (a[8] == 2 && a[9] == 78)
                        return;             // (@0x55AF11) skill 78 case: bail out
                    // TODO(fx) @0x55AF99/0x55B00D: sound index selection
                    //   (player[i]+6984/+6988, MobDb_GetEntry(mITEM, a[12]-1)+188,
                    //   Skill_GetEffectVariant(a[9])) then Snd3D_PlayPositional at
                    //   flt_149B03C; +432 = "parry" variant if a[13]==1.
                }
            }
        } else {
            // --- Miss / dodge (a[12] == 0) ---
            if (SelfEst(atk)) {             // @0x55A404
                LigneSysteme(Str(197), 17); // "you missed"
            }
            if (SelfEst(tgt)) {             // @0x55A462
                LigneSysteme(Str(202), 21); // "the attacker missed you"
                if (SelfModeDeclenche()) {
                    // TODO(net) @0x55A4D7 : Net_QueueAction9(&g_PlayerCmdController)
                }
            }
        }
        return;

    // -------------------------------------------------------------------------
    // Case 3: player -> MONSTER attack.
    // -------------------------------------------------------------------------
    case 3:
        if (!a[12]) {                       // miss (@0x55B01A)
            if (SelfEst(atk))               // @0x55B046
                LigneSysteme(Str(197), 17); // "you missed"
            return;
        }
        // Hit: log on the attacker side (self).
        if (SelfEst(atk)) {                 // @0x55B0AE
            std::string s = Str(198);
            int v32 = a[15] - a[14];        // (@0x55B0DF) with clamp
            if (v32 < 0) v32 = 0;
            if (a[13])                                      // crit (any bit)
                s += Fmt("%s(+%d)", Str(200), v32);
            else
                s += Fmt("%s(+%d)", Str(199), v32);
            if (a[14] > 0)
                s += Fmt(" %s(+%d)", Str(201), a[14]);
            LigneSysteme(s, 3);
        }

        // Apply damage to the target MONSTER (a[3],a[4]).
        {
            int i = TrouverMonstre(tgt);                    // loop @0x55B1DB
            if (i >= 0) {
                MonsterEntity& m = g_World.monsters[i];
                if (a[13] == 1) {
                    // @0x55B326: parry. Scan free slot @0x55b296 then Fx_AttachParry(&slot, &monster[i]).
                    if (gfx::FxSlot* fx = SlotLibreFx())
                        gfx::Fx_AttachParry(fx, SourceFromMonster(m));
                }
                // @0x55B358: "deflection" if monster.def+232==2 AND g_Opt_GfxDetailShadows==1,
                //   triggered when the crossed HP-bar tier CHANGES (before/after damage).
                //     v43 = 3 - ftol(hp*100 / (def+368)) / 30       (tier before)
                //     v47 = 3 - ftol((hp-a[16])*100 / (def+368)) / 30 (tier after)
                //     if v43 < v47 -> Fx_AttachDeflect(&monster[i]).   (def+368 = MONSTER_INFO max HP)
                // g_Opt_GfxDetailShadows 0x84DEF8 read via the long tail (0 if not wired -> honest
                // no-op); def+232/+368 = raw fields of the resolved MONSTER_INFO record (m.def), guarded.
                if (m.def && g_Client.VarGet(0x84DEF8) == 1) {
                    const uint8_t* def = static_cast<const uint8_t*>(m.def);
                    int32_t deflectMode, maxHp;
                    std::memcpy(&deflectMode, def + 232, 4);   // def+232 (@0x55b358)
                    std::memcpy(&maxHp,       def + 368, 4);   // def+368 max HP (@0x55b39c)
                    if (deflectMode == 2 && maxHp != 0) {
                        const int v43 = 3 - static_cast<int>(static_cast<double>(m.hp) * 100.0 / maxHp) / 30;
                        const int v47 = 3 - static_cast<int>(static_cast<double>(m.hp - a[16]) * 100.0 / maxHp) / 30;
                        if (v43 < v47) {                        // @0x55b42c
                            if (gfx::FxSlot* fx = SlotLibreFx()) // scan @0x55b432
                                gfx::Fx_AttachDeflect(fx, SourceFromMonster(m)); // @0x55b4c2
                        }
                    }
                }
                m.hp -= a[16];              // external damage (@0x55B4F8)
                // Crt_Memcpy(monster[i]+923816, block, 76) @0x55B51F — combat block
                //   cache on the entity (modeled off-struct, see rule #6).
                StockerBlocCombat(CacheMonstre(), tgt, block);
                // @0x55B573: muzzle variant if hp>0 AND monster.def+240==2 (after damage).
                //   Selection: a[8]==1 -> switch a[9] {1->var1, 2->var2, 3->var3}; a[8]==2 -> var2;
                //   else none (LABEL_162/163). def+240 = raw MONSTER_INFO field (m.def, guarded).
                if (m.hp > 0 && m.def) {
                    int32_t muzzleMode;
                    std::memcpy(&muzzleMode, static_cast<const uint8_t*>(m.def) + 240, 4); // def+240
                    if (muzzleMode == 2) {
                        int variant = 0;                        // 0 = no effect
                        if (a[8] == 1) {                        // @0x55b5f6
                            switch (a[9]) {                     // @0x55b61d
                                case 1: variant = 1; break;     // @0x55b668
                                case 2: variant = 2; break;     // LABEL_162 @0x55b6e1
                                case 3: variant = 3; break;     // @0x55b6da
                                default: variant = 0; break;    // (a[9] other) -> none
                            }
                        } else if (a[8] == 2) {                 // @0x55b5ff
                            variant = 2;                        // LABEL_162 @0x55b6e1
                        }
                        if (variant >= 1) {
                            if (gfx::FxSlot* fx = SlotLibreFx()) // scan @0x55b57f
                                gfx::Fx_AttachMuzzleVariant(fx, SourceFromMonster(m), variant);
                        }
                    }
                }
            }
        }

        // Positional sound on the attacker PLAYER (a[1],a[2]) — LABEL_163.
        {
            int i = TrouverJoueur(atk);                     // loop @0x55B718
            if (i >= 0) {
                if (a[8] == 2 && a[9] == 78)
                    return;                 // (@0x55B8F8)
                // TODO(fx) @0x55B980/0x55B9F4: same sound selection as case 1/2
                //   (flt_149B03C, +432 if a[13]==1).
            }
        }
        return;

    // -------------------------------------------------------------------------
    // Case 4: MONSTER -> player attack.
    // -------------------------------------------------------------------------
    case 4:
        if (a[12]) {                        // hit (@0x55BA01)
            // Log on the target side (self).
            if (SelfEst(tgt)) {             // @0x55BAE3
                std::string s = Str(203);
                int v27 = a[15] - a[14];    // (@0x55BB1D) NO clamp
                if (a[13])
                    s += Fmt("%s(-%d)", Str(200), v27);
                else
                    s += Fmt("%s(-%d)", Str(199), v27);
                if (a[14] > 0)
                    s += Fmt(" %s(-%d)", Str(201), a[14]);
                LigneSysteme(s, 2);
                if (SelfModeDeclenche()) {
                    // TODO(net) @0x55BC3F : Net_QueueAction10(&g_PlayerCmdController)
                }
            }

            // Apply damage to the target PLAYER (a[3],a[4]).
            {
                int i = TrouverJoueur(tgt);                 // loop @0x55BC44
                if (i >= 0) {
                    PlayerEntity& p = g_World.players[i];
                    // TODO(fx) @0x55BD35/0x55BD76: Snd3D_PlayPositional(flt_14914FC if
                    //   a[13], else flt_148927C) — blocked-hit / normal sound. (audio, out of FX scope)
                    if (a[13] == 1) {
                        // @0x55BE28: guard on external crit. Scan free slot @0x55bd88 then
                        //   Fx_AttachBlockGuard(&slot, 2, &player[i], i==0).
                        if (gfx::FxSlot* fx = SlotLibreFx())
                            gfx::Fx_AttachBlockGuard(fx, 2, SourceFromPlayer(p), i == 0);
                    }
                    p.hp -= a[16];          // external damage (@0x55BE5E)
                    if (i == 0) g_World.self.hp = p.hp;     // HUD self mirror
                    // NB: no >=0 clamp in case 4 (faithful to the binary).
                    // Crt_Memcpy(player[i]+7536, block, 76) @0x55BE85 — combat block
                    //   cache on the entity (modeled off-struct, see rule #6).
                    StockerBlocCombat(CacheJoueur(), tgt, block);
                    // @0x55BF1F: muzzle flash on the hit target. Scan free slot @0x55be8d then
                    //   Fx_AttachMuzzleFlash(&slot, &player[i], 1).
                    if (gfx::FxSlot* fx = SlotLibreFx())
                        gfx::Fx_AttachMuzzleFlash(fx, SourceFromPlayer(p), 1);
                }
            }
        } else if (SelfEst(tgt)) {          // miss (@0x55BA35)
            LigneSysteme(Str(202), 21);     // "the attacker missed you"
            if (SelfModeDeclenche()) {
                // TODO(net) @0x55BAAA : Net_QueueAction9(&g_PlayerCmdController)
            }
        }
        return;

    default:                                // (@0x55A3CA default)
        return;
    }
}

} // namespace ts2::game

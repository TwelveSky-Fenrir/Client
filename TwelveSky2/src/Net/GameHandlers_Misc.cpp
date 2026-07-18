// Net/GameHandlers_Misc.cpp — routes MISCELLANEOUS packets to runtime state
// (game::g_Client). "misc" domain (RE/handler_domains.json).
//
// Groups the game-variable mega-dispatcher, the game-server connection,
// script triggers (anticheat, ignored), timers/countdowns, quickslot sync,
// the talisman/pet dispatcher, skill cooldowns and auras, cultivation
// (attributes/growth/buffs), summon, buff effects, revive, the PvP tally, and
// notices (system / achievement / server). Faithful translation of the
// semantics described in RE/net_handler_notes.md; automatic sends (Net_SendOp*)
// and exact UI rendering remain `// TODO(send)` / `// TODO(state)`. Long-tail
// scalar globals (dword_XXXX) go through g_Client.Var(addr).
//
//   0x0e SystemMessageBox        0x16 SetGameVar           0x18 GameServerConnectResult
//   0x27 QuestInteractResult     0x28 ToggleObserver       0x39 PvpTallyUpdate
//   0x58 CultivationDispatch     0x5b QuickslotSync        0x61 ServerNameNotice
//   0x62 Sub_4A55E0 (*)          0x63 ScriptTrigger         0x66 PetSlotDispatch
//   0x6f SkillCooldownSet        0x71 Sub_4A7150 (*)        0x72 RevivePrompt
//   0x73 CountdownTimerStart     0x76 MinigameStateLoad     0x7d SkillAuraSync
//   0x82 Sub_4AAB60 (*)          0x84 SummonSpawn           0x85 SystemNotice
//   0x8f Sub_4AB020 (*)          0x99 AchievementNotice     0xae BuffEffectDispatch
//   0xb1 Sub_4B33C0 (*)
// (*) coverage gaps closed — absent from RE/handler_domains.json and from any
//     Register*Handlers before this addition (cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md).
//
// RULE: this module does NOT EDIT shared state (ClientRuntime.h) — it uses it.
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // ts2::net::g_GmCmdCooldownLatch, g_MorphInProgress (0x1675A88)
#include "Net/Login.h"         // net::ConnectGameServer 0x462A70, net::kLoginHostCom, kNet* codes
#include "Net/GameServerDomains.h" // net::SelectGameServerHost (Net_SelectServerDomain 0x53FE90)
#include "Net/SendPackets.h"   // net::Net_SendPacket_Op21 0x4B5190
#include "Game/ClientRuntime.h"
#include "Game/StatEngine.h"   // game::StatEngine::CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0) — M3
#include "Game/BitPacking.h"   // game::Stat_UnpackCombined/PackCombined (0x54CE40/0x54CEB0) — M4
#include "Game/MapWarp.h"      // game::BeginWarpToMap37/BeginWarpToFactionTown (0x62/0x8f) + Warp_SendTeleport (0x84)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (warp coordinate resolution)
#include "Game/SocialSystem.h" // game::g_Achievements/PostAchievementNotice (AchievementNotice 0x99)
#include "Game/GameDatabase.h" // game::GetItemInfo — MobDb_GetEntry(mITEM, id) 0x4C3C00 (0xae case 3)
#include "Game/StringTables.h" // game::g_Strings.zoneNames = StrTable003 (0x4C1AD0) — 0x61 sub-op 1
#include "Game/GameState.h"    // game::g_World.self.elementSecondary / g_World.players — 0x28
#include "Game/QuestSystem.h"  // game::g_QuestProgress (g_CurQuestId/g_QuestObj*) — 0x28
#include "Game/ExtraDatabases.h" // game::g_ExtraDb.quest (mQUEST 0x8E71E4) + QuestDefRecord — 0x28

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ts2::net {

// Original addresses (globals not modeled as a proper field, via g_Client.Var) and
// local helpers. The "0xE00000NN" addresses are STABLE SYNTHETIC keys for
// scalars whose original address isn't recorded in the notes (outside the
// real image range -> no collision with an authentic dword_XXXX).
namespace {
// g_TalismanSlot 0x1674760 — REAL address (confirmed by Game/AutoPlaySystem.h:101 and
// Game/StatFormulas.h:37/StatFormulas.cpp:276, independently of this module); NOT a
// synthetic key, unlike the generic comment above — shared with
// AutoPlaySystem (dword_1675664[slot] for slot<10 in Game/StatFormulas.cpp:276).
constexpr uint32_t kTalismanSlot     = 0x1674760u;  // g_TalismanSlot (talisman index, -1 = none)
constexpr uint32_t kPendingStopReq   = 0xE0000072u; // g_PendingStopRequest (revive invite)
// (ex-kMorphInProgress 0xE0000018 removed: dead synthetic key — the REAL morph
//  global is net::g_MorphInProgress = g_MorphInProgress 0x1675A88, ClientState.h:18,
//  the one that the Net_Send* builders read; now written directly, cf. 0x18.)
// Synthetic base for the SetGameVar switch's long tail (one key per varId).
constexpr uint32_t kGameVarBase      = 0xE0160000u; // dword selected by varId

// NpcTbl_FindIdByType 0x4C83E0 — IDB MISNOMER: this is NOT an NPC table. Its `this`
// is mQUEST 0x8E71E4 (`mov ecx, offset mQUEST` at call site @0x48F17C) and its stride is
// 8444 = sizeof(QuestDefRecord) — the table IS fully modeled (Game/ExtraDatabases.h:98,
// static_assert(sizeof==8444), loaded from 005_00006.IMG by ExtraDatabases.cpp:159).
// The TODO(state) that claimed this table was "absent from the client model" was therefore FALSE.
//
// Twin ALREADY ported: NpcTbl_FindByTypeAndId 0x4C8340 -> game::FindQuestDefByElementAndId
// (Game/ExtraDatabases.cpp:282) — same table, same stride, same predicates (a) and (b); only
// the 3rd predicate (rec[160]==0 here, rec[60]==questId there) and the return value
// (fieldB here, the record there) differ. Literal transcription modeled after it.
//
// Implemented LOCALLY (rather than next to its twin in Game/ExtraDatabases.*) because this
// front doesn't own those files — relocation is a later-pass cleanup.
//
// Reference decompilation (0x4C83E0):
//   for (i = 0; i < *this; ++i)
//     if (Crt_Strcmp(rec+4, "") && *(rec+56) == a2+1 && !*(rec+160)) return *(rec+60);
//   return 0;
// FIDELITY: no loaded-table guard (the binary has none) — on an empty table
// (count == 0) the loop doesn't run and the function returns 0, natural behavior.
int FindFirstQuestIdOfElement(int element0) {
    const game::DataTable& t = game::g_ExtraDb.quest;
    for (uint32_t i = 0; i < t.count; ++i) {                       // i < *this @0x4C83E9
        const uint8_t* raw = t.record(i);
        if (!raw) break;
        const auto* rec = reinterpret_cast<const game::QuestDefRecord*>(raw);
        // (a) Crt_Strcmp(rec+4, "") != 0 @0x4C8456: name NOT empty (String 0x7EC95F == "").
        if (rec->name[0] == '\0') continue;
        // (b) rec[56] == a2 + 1 (the +1 applies to the ARGUMENT, never to the field).
        if (rec->fieldA != static_cast<uint32_t>(element0 + 1)) continue;
        // (c) rec[160] == 0.
        if (rec->fieldK != 0) continue;
        return static_cast<int>(rec->fieldB);                      // return rec[60] @0x4C8473
    }
    return 0;                                                      // @0x4C847C
}

// dword_168728C[0] = g_PlayerArray 0x1687234 + 0x58 -> entity+88 -> body+64 (body @ entity+0x18)
// = the `element` field of player slot 0 (SELF). Corroborated by Scene/WorldRenderer.cpp:175
// (kNpBodyElement = 88-24) and World/TerrainPicker.cpp:108 (kBodyElement = 88-24, named
// "dword_168728C (record+88)"). Direct u32 LE write on .body.data(), a pattern already
// established in Net/ (CharStatDeltaDispatch.cpp). The binary indexes a static array with no
// guard; g_World.players is a std::vector -> `!empty()` guard required (doesn't alter any
// observable behavior: in the flow, the self slot always exists when this packet arrives).
void SetSelfBodyElement(int element) {
    if (game::g_World.players.empty()) return;
    uint8_t* b = game::g_World.players[0].body.data();
    const uint32_t v = static_cast<uint32_t>(element);
    std::memcpy(b + 64, &v, sizeof v);   // entity+88 = body+64
}

// Unaligned u32 read from a byte buffer (like the original memcpy).
inline uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, sizeof v); return v; }
inline float    RdF32(const uint8_t* p) { float v; std::memcpy(&v, p, sizeof v); return v; }

// Attack-rating bound recompute (dword_168736C/1687370/1687374/1687378 = base
// min/cur min/base max/cur max), pattern identical to Net/GameVarDispatch.cpp::
// ratingRecalcBothClamp (cases 32/40/71 of the same stat engine, the dominant one
// in this dispatcher: 3 occurrences out of 5). M3: faithful LIVE recompute — the binary
// (0x4A5790, cases 6/7/8) calls Char_CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0) on
// g_EquipSnapshotScratch on EVERY call; we reproduce this via StatEngine::CalcAttackRatingMin/
// Max(g_World.self, g_World.db) (same EA 0x4CD970/0x4CE3F0), as in GameVarDispatch.cpp.
// Used by PetSlotDispatch (0x66) cases 6/7/8.
void RecalcTalismanAttackRating() {
    // 0x4A59BF..: dword_168736C = Char_CalcAttackRatingMin(...); clamp dword_1687370.
    const int32_t mn = game::StatEngine::CalcAttackRatingMin(game::g_World.self, game::g_World.db); // 0x4CD970
    game::g_Client.Var(0x168736C) = mn;                                          // dword_168736C base min
    if (game::g_Client.VarGet(0x1687370) > mn) game::g_Client.Var(0x1687370) = mn; // dword_1687370 cur min (clamp)
    const int32_t mx = game::StatEngine::CalcAttackRatingMax(game::g_World.self, game::g_World.db); // 0x4CE3F0
    game::g_Client.Var(0x1687374) = mx;                                          // dword_1687374 base max
    if (game::g_Client.VarGet(0x1687378) > mx) game::g_Client.Var(0x1687378) = mx; // dword_1687378 cur max (clamp)
}

// AR recompute WITHOUT clamp — pattern DISTINCT from RecalcTalismanAttackRating() above:
// the binary writes the 4 globals here as a PLAIN ASSIGNMENT (no `if (cur > n)` test).
// Anchor: Net_OnCultivationDispatch 0x493180, cases 19/20 — EA 0x493FD7 (dword_1687370=Min),
// 0x493FE6 (dword_168736C=Min), 0x493FF5 (dword_1687378=Max), 0x494004 (dword_1687374=Max)
// [same for case 20: EA 0x494114/0x494123/0x494132/0x494141]. The binary calls Min and Max
// TWICE each (pure functions, no side effects) -> factored into a single call.
void RecalcAttackRatingSetAll() {
    const int32_t mn = game::StatEngine::CalcAttackRatingMin(game::g_World.self, game::g_World.db); // 0x4CD970
    game::g_Client.Var(0x1687370) = mn;   // 0x493FD7 — NO clamp (unlike cases 9/10)
    game::g_Client.Var(0x168736C) = mn;   // 0x493FE6
    const int32_t mx = game::StatEngine::CalcAttackRatingMax(game::g_World.self, game::g_World.db); // 0x4CE3F0
    game::g_Client.Var(0x1687378) = mx;   // 0x493FF5
    game::g_Client.Var(0x1687374) = mx;   // 0x494004
}

// AR recompute for case 6: ONLY dword_168736C and dword_1687374, no clamp.
// Anchor: Net_OnCultivationDispatch 0x493180, case 6 — EA 0x4935F8 / 0x493607. The binary
// touches NEITHER dword_1687370 NOR dword_1687378 here (unlike cases 9/10/19/20).
void RecalcAttackRatingBaseOnly() {
    game::g_Client.Var(0x168736C) =
        game::StatEngine::CalcAttackRatingMin(game::g_World.self, game::g_World.db); // 0x4935F8
    game::g_Client.Var(0x1687374) =
        game::StatEngine::CalcAttackRatingMax(game::g_World.self, game::g_World.db); // 0x493607
}

// StrTable003_Get(dword_84A6A8, id) 0x4C1AD0 — ZONE-NAME table (003.DAT / mZONENAME),
// DISTINCT from StrTable005_Get(g_LangId, id) 0x4C1D10 (005.DAT / mMESSAGE) exposed by game::Str().
// 1-based index, empty string out of bounds (like &String in the original).
// Anchor: Net_OnServerNameNotice 0x4A5540, EA 0x4A55AC.
const char* Str3(int id) { return game::g_Strings.zoneNames.Get(id); }

// Crt_Vsnprintf(buf, StrTable005_Get(g_LangId, strId), ...) 0x75CD5F — the FORMAT is
// the localized table entry itself (not a literal). 1000-byte buffer like
// the original (v28/v29, [ebp-7F8h]/[ebp-410h] of Net_OnBuffEffectDispatch 0x4A88D0).
std::string FmtFromStrTable(int strId, ...) {
    char buf[1000];
    va_list ap; va_start(ap, strId);
    std::vsnprintf(buf, sizeof buf, game::Str(strId).c_str(), ap);
    va_end(ap);
    return std::string(buf);
}

// vsnprintf on a LITERAL format from the binary ("%s %d", "%s%s", "%s%s %d%s", "[%d]%s").
std::string Fmt(const char* f, ...) {
    char buf[1000];
    va_list ap; va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

// MobDb_GetEntry(mITEM, id) 0x4C3C00; the +4 field of the entry == ItemInfo::name
// (GameDatabase.h:60). Returns nullptr if the entry doesn't exist — the caller MUST
// test it, since the binary uses it to branch (cf. case 3 of 0xae).
const char* ItemNameOrNull(uint32_t itemId) {
    const game::ItemInfo* it = game::GetItemInfo(itemId);
    return it ? it->name : nullptr;
}
} // namespace

// hWndParent 0x815184 — window that receives the WSAAsyncSelect(WM_USER+1) posted by
// Net_ConnectGameServer 0x462A70. In the binary, App_WinMain 0x4609C0 sets this
// global; the rewritten network layer receives it as a PARAMETER (net::ConnectGameServer
// notifyWnd, Net/Login.h:59). The only existing C++ caller (LoginScene, char-select
// flow) passes its own notifyWnd_; handler 0x18 (mid-game reconnection) doesn't have
// this handle. We expose it here, for App to assign.
// TODO [anchor 0x815184]: App (front NOT owned by this module) must set
//   ts2::net::g_GameSocketNotifyWnd = hwnd_ at init (App::Init/HandleMessage), via an
//   extern declaration in an App header. As long as it stays nullptr, handler 0x18
//   degrades honestly (treats the connection as a WSAAsyncSelect failure -> Str107)
//   instead of calling ConnectGameServer with a null window (which would cancel recv).
HWND g_GameSocketNotifyWnd = nullptr;

void RegisterMiscHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, g_World, Str()

    // 0x0e ServerBillboardImage (ex-SystemMessageBox, misnamed) — validates/displays a server image.
    // Original: Billboard_ValidateImageViaTempFile(param, image[1000]). No entity
    // state update; the image is a filename/path (C string).
    OnPacket<ServerBillboardImage>(sys, 0x0e, [](const ServerBillboardImage& p) {
        std::string image(p.image, strnlen(p.image, sizeof p.image));
        (void)image; (void)p.param;
        // TODO(state): Billboard_ValidateImageViaTempFile(p.param, image) — writes
        //   the image to a temp file, validates it and displays it in the system box.
    });

    // 0x16 SetGameVar — mega-dispatcher (~158 cases): writes 'value' into the global
    // selected by varId, often + system line/sound, and warps if value<=0.
    OnPacket<SetGameVar>(sys, 0x16, [](const SetGameVar& p) {
        switch (p.varId) {
        case 2:  // g_SelfUnspentAttrPoints (0x16731D0)
            g_Client.Var(0x16731D0) = p.value;
            break;
        case 3:  // g_Currency (0x1673180) + mirror dword_1687254[0]
            g_Client.inv.currency = p.value;
            break;
        case 19: case 23: case 25:  // g_InvWeight
            g_Client.inv.weight = p.value;
            break;
        default:
            // Long tail: ~150 unreversed cases, each targeting a distinct
            // global. We faithfully store the value in a slot dedicated to that varId.
            g_Client.Var(kGameVarBase + static_cast<uint32_t>(p.varId) * 4u) = p.value;
            break;
        }
        // NOTE: the system line/sound/warp (Map_BeginWarpToFactionTown[Ex]) per varId, and the
        // COMPLETE coverage of the ~158 switch cases, are implemented by the dedicated
        // module Net/GameVarDispatch.cpp (game::ApplySetGameVar), registered LAST in
        // InstallGameHandlers via RegisterCoreOverrideHandlers (Net/GameHandlers_Core.cpp) —
        // it REPLACES this simplified handler for opcode 0x16 (one slot per opcode, the
        // last one registered wins). The code above remains the reference "domain"
        // version for the 4 most common cases, but is NO LONGER the one executed at
        // runtime for 0x16 — cf. Net/GameHandlers_Core.cpp lines 1-18.
    });

    // 0x18 GameServerConnectResult — Pkt_GameServerConnectResult 0x469CF0. Result of
    // selecting/connecting (mid-game reconnect/relay server) to the game server.
    // Decompiled (IDA): v38=resultCode (memcpy@0x8156C1), v39=serverId (0x8156C5),
    // v36=port (0x8156C9). Captures [&sys] to reach sys.Client() (global NetClient&,
    // persistent via InstallGameHandlers — no dangling).
    OnPacket<GameServerConnectResult>(sys, 0x18, [&sys](const GameServerConnectResult& p) {
        switch (p.resultCode) {                                    // switch(v38) — Pkt_GameServerConnectResult 0x469CF0
        case 0: {
            // Net_SelectServerDomain 0x53FE90 (internal EA of Pkt_GameServerConnectResult):
            // translates p.serverId (= received domainId, unk_8156C5) to a hostname via the
            // reconstructed table (Net/GameServerDomains.h). Fallback = net::kLoginHostCom (index
            // out of range / guard OFF). SAME resolution point as LoginScene::ConnectToGameServer.
            const std::string host = net::SelectGameServerHost(p.serverId, net::kLoginHostCom); // 0x53FE90
            // Net_ConnectGameServer 0x462A70: new socket + 5-byte banner + XOR key@+4 /
            // seq@+5 + 141-byte auth + WSAAsyncSelect(WM_USER+1). Requires hWndParent 0x815184.
            const HWND wnd = ts2::net::g_GameSocketNotifyWnd;      // == hWndParent 0x815184
            const int v40 = wnd
                ? net::ConnectGameServer(sys.Client(), host.c_str(),
                                         static_cast<uint16_t>(p.port), wnd)
                : net::kNetErrAsyncSelect;  // no window -> code 6 -> Str107 (honest degradation, cf. §hWndParent)
            switch (v40) {                                         // switch(v40) — Net_ConnectGameServer sub-result
            case 0:
                // Original: g_SceneMgr=5 (0x1676180) / g_SceneSubState=0 (0x1676184) /
                // dword_1676188=0 -> switches to the EnterWorld scene to RELOAD the zone.
                // SceneManager NOT edited (owned by W2-F1): we arm the shared flag
                // consumed by SceneManager (game::g_World.sceneReloadPending, GameState.h:544).
                // 0x469CF0 does NOT write pendingWarpZoneId (no zoneId in this packet) ->
                // left at its upstream value (-1 = reload current zone; the new server
                // will send Pkt_EnterWorld 0x0c with the real zone).
                game::g_World.sceneReloadPending = true;
                break;
            case 1: g_Client.prompt.Open(2, Str(102)); break;      // UI_NoticeDlg_Open(_,2,Str102,"")
            case 2: g_Client.prompt.Open(2, Str(103)); break;      // UI_NoticeDlg_Open(_,2,Str103,"")
            case 3:
            case 4:
            case 5: {
                const int sid = (v40 == 3) ? 104 : (v40 == 4) ? 105 : 106;
                g_Client.msg.System(Str(sid));                     // Msg_AppendSystemLine(g_ChatManager, Str104/105/106, g_SysMsgColor)
                net::Net_SendPacket_Op21(sys.Client());            // Net_SendPacket_Op21(&g_AutoPlayMgr) 0x4B5190
                // FIDELITY GAP [anchor 0x469CF0, EA 0x469E20 -> LABEL_12 0x469EE5]: the original
                // does `call Net_SendPacket_Op21 / test eax,eax / jnz` — builder 0x4B5190
                // does return an int (0 after Net_CloseSocket, 1 on success) and, on failure,
                // jumps to LABEL_12 = UI_NoticeDlg_Open(byte_18225C8, 2, Str20, "") INSTEAD of
                // setting g_MorphInProgress = 0. The fix requires changing the signature of
                // net::Net_SendPacket_Op21 (void -> bool) in Net/SendPackets.h:42, a file NOT
                // OWNED by this front -> gap kept and escalated to the orchestrator. Observable
                // effect limited to the case of an already-broken socket.
                g_MorphInProgress = 0;                             // ts2::net::g_MorphInProgress = g_MorphInProgress 0x1675A88
                break;
            }
            case 6: g_Client.prompt.Open(2, Str(107)); break;      // UI_NoticeDlg_Open(_,2,Str107,"")
            case 7: g_Client.prompt.Open(2, Str(108)); break;      // UI_NoticeDlg_Open(_,2,Str108,"")
            default: break;                                        // default: return (no-op)
            }
            break;
        }
        // resultCode 1..12: system line (StrTable005) + morph end (g_MorphInProgress=0).
        // EXACT decompiled ids (0x469CF0, cases 1..12).
        case 1:  g_Client.msg.System(Str(100));  g_MorphInProgress = 0; break;
        case 2:  g_Client.msg.System(Str(1221)); g_MorphInProgress = 0; break;
        case 3:  g_Client.msg.System(Str(1347)); g_MorphInProgress = 0; break;
        case 4:  g_Client.msg.System(Str(1928)); g_MorphInProgress = 0; break;
        case 5:  g_Client.msg.System(Str(1554)); g_MorphInProgress = 0; break;
        case 6:  g_Client.msg.System(Str(1951)); g_MorphInProgress = 0; break;
        case 7:  g_Client.msg.System(Str(2237)); g_MorphInProgress = 0; break;
        case 8:  g_Client.msg.System(Str(1213)); g_MorphInProgress = 0; break;
        case 9:  g_Client.msg.System(Str(2308)); g_MorphInProgress = 0; break;
        case 10: g_Client.msg.System(Str(2689)); g_MorphInProgress = 0; break;
        case 11: g_Client.msg.System(Str(2330)); g_MorphInProgress = 0; break;
        case 12: g_Client.msg.System(Str(2821)); g_MorphInProgress = 0; break;
        default: break;                                            // default: return (no-op)
        }
    });

    // 0x27 QuestInteractResult — quest interaction result (misnamed). The
    // item content comes from g_pCurQuestStepRecord; invRow==-1 => no inv write.
    OnPacket<QuestInteractResult>(sys, 0x27, [](const QuestInteractResult& p) {
        switch (p.resultCode) {
        case 1: g_Client.msg.System(Str(109)); break;  // advance step + give item
        case 2: g_Client.msg.System(Str(432)); break;  // final reward (3 rewards)
        case 3: g_Client.msg.System(Str(433)); break;  // replace item
        case 4: g_Client.msg.System(Str(434)); break;  // Inventory_ReplaceItem
        case 5: g_Client.msg.System(Str(435)); break;  // failure
        case 6: g_Client.msg.System(Str(436)); break;  // increment counter
        case 7: g_Client.msg.System(Str(438)); break;
        case 8: g_Client.msg.System(Str(437)); break;  // increment counter
        case 9: g_Client.msg.System(Str(439)); break;  // increment counter
        default: break;
        }
        // NOTE: the inventory-cell write ([invRow][invSlot] if invRow!=-1) and the
        // quest counters (killTrack/objective*) are applied by
        // game::ApplyQuestInteractResultState (Game/QuestSystem.h/.cpp), wired for this
        // opcode 0x27 in Net/GameHandlers_Core.cpp (registered after this module — faithfully
        // completes the high-level messages above without duplicating them).
        (void)p.invRow; (void)p.invSlot; (void)p.gridX; (void)p.gridY;
    });

    // 0x28 ToggleObserver — toggles observer mode (element 3) + return to town.
    // Pkt_ToggleObserver 0x48F080 (registered @0x4634F9: (0x8464A8-0x846408)/4 = 40 = 0x28).
    // Both ex-TODO(state) items in this handler rested on FALSE premises, refuted at
    // decompilation (now available; the "0x462..." address they cited was
    // itself wrong):
    //   · "NPC .IMG table absent from the client model" -> NpcTbl_FindIdByType 0x4C83E0 scans
    //     mQUEST, a PORTED table; cf. FindFirstQuestIdOfElement above.
    //   · "don't guess which fields are zeroed" -> the 4 fields are explicit
    //     (0x48F0E9/F3/FD/107) and all modeled in game::QuestProgressState.
    OnPacket<ToggleObserver>(sys, 0x28, [](const ToggleObserver& p) {
        // (0x48F099) `g_GmCmdCooldownLatch = 0` is UNCONDITIONAL: it precedes the `jz`
        // @0x48F0AD, so it ALSO applies to resultCode 1 and >= 2. (The binary rewrites it
        // @0x48F0CB in branch 0 — original redundancy kept below.)
        g_GmCmdCooldownLatch = 0;                                   // /*0x48f099*/
        if (p.resultCode == 0) {                                    // /*0x48f0ad*/
            g_GmCmdCooldownLatch = 0;                               // /*0x48f0cb (redundant)*/
            if (g_LocalElement == 3) {                              // /*0x48f0c5*/
                // Already an observer -> return to the secondary element.
                // BINARY ORDER: the decrement PRECEDES the element assignment.
                --g_Client.Var(0x16747E8);                          // /*0x48f15f*/
                // g_LocalElementSecondary 0x1673198. Source of truth = the modeled field
                // game::g_World.self.elementSecondary (written by UI/LoginScene.cpp:2498, read
                // by 8+ consumers: VendorTrade/ItemSystem/SkillCombat/AutoPlay/...).
                // The old read `g_Client.Var(0x1673198)` targeted a 2nd parallel store
                // that NOBODY writes (exhaustive grep: 1 single occurrence, this read)
                // -> exiting observer mode set the player's element to 0. Compounding it:
                // net::g_LocalElement is a REFERENCE to g_World.self.element
                // (Net/NetClient.h:117), so the corruption propagated to combat gates,
                // quest resolution, and terrain picking.
                g_LocalElement = static_cast<uint32_t>(game::g_World.self.elementSecondary); // /*0x48f16a*/
                game::g_QuestProgress.npcQuestId =
                    FindFirstQuestIdOfElement(game::g_World.self.elementSecondary); // /*0x48f181*/
                game::g_QuestProgress.objectiveMode     = 0;        // g_QuestObjMode   /*0x48f186*/
                game::g_QuestProgress.objectiveType     = 0;        // g_QuestObjType   /*0x48f190*/
                game::g_QuestProgress.objectiveTarget   = 0;        // g_QuestObjParam1 /*0x48f19a*/
                game::g_QuestProgress.objectiveProgress = 0;        // g_QuestObjParam2 /*0x48f1a4*/
                SetSelfBodyElement(game::g_World.self.elementSecondary); // dword_168728C[0] /*0x48f1b3*/
                g_Client.msg.System(Str(1443));                     // /*0x48f1c9-0x48f137*/
            } else {
                g_LocalElement = 3;                                 // becomes observer /*0x48f0d5*/
                game::g_QuestProgress.npcQuestId        = 0;        // g_CurQuestId     /*0x48f0df*/
                game::g_QuestProgress.objectiveMode     = 0;        // g_QuestObjMode   /*0x48f0e9*/
                game::g_QuestProgress.objectiveType     = 0;        // g_QuestObjType   /*0x48f0f3*/
                game::g_QuestProgress.objectiveTarget   = 0;        // g_QuestObjParam1 /*0x48f0fd*/
                game::g_QuestProgress.objectiveProgress = 0;        // g_QuestObjParam2 /*0x48f107*/
                SetSelfBodyElement(3);                              // dword_168728C[0] /*0x48f111*/
                g_Client.msg.System(Str(260));                      // /*0x48f12c-0x48f137*/
            }
            // Returns to faction town (resolution + arming globals; the actual network send
            // remains a TODO(send) internal to Game/MapWarp.cpp, cf. include-header note).
            BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver); // /*0x48f143*/
        } else if (p.resultCode == 1) {                             // /*0x48f0b3, cmp 1 ; jz*/
            // Refused. The binary only shows Str(966) for resultCode == 1 EXACTLY.
            g_Client.msg.System(Str(966));                          // /*0x48f1f8-0x48f203*/
        }
        // resultCode >= 2: TOTAL NO-OP (`jmp loc_48F208` -> `return result`). No message,
        // no state change — apart from the unconditional latch above. /*0x48f208*/
    });

    // 0x39 PvpTallyUpdate — increments the win/loss counters.
    OnPacket<PvpTallyUpdate>(sys, 0x39, [](const PvpTallyUpdate& p) {
        switch (p.code) {
        case 0: ++g_Client.Var(0x182292C); break;  // losses (dword_182292C)
        case 1: ++g_Client.Var(0x1822930); break;  // wins (dword_1822930)
        case 2: g_Client.msg.System(Str(2237)); break;
        default: break;
        }
    });

    // 0x58 CultivationDispatch — cultivation mega-dispatcher (attributes/growth/buffs).
    // Anchor: Net_OnCultivationDispatch 0x493180. Decoding: v72=value @0x8156C1 (memcpy
    // 0x49319E), v67=subOpcode @0x8156C5 (0x4931B4), v73..[100] = body @0x8156C9 (0x4931C7).
    //
    // MODELING NOTE (currency): the binary holds TWO globals — g_Currency 0x1673180
    //   and its mirror dword_1687254 — decremented TOGETHER in cases 4/6/7/16/17/20.
    //   The C++ merges them into a single field (g_Client.inv.currency, ClientRuntime.h:82,
    //   "g_Currency 0x1673180 (+ mirror dword_1687254[0])"); the Var(0x1687254) key has
    //   no reader. Case 15 is the exception: it decrements g_Currency ALONE (EA
    //   0x493C6B/9A/B3, without touching 0x1687254) — an asymmetry NOT representable by the
    //   single-field collapse; we apply the decrement to inv.currency (observable behavior).
    //
    // CROSS-CUTTING GAP (out of scope for this front, for the orchestrator to arbitrate): the C++
    //   has TWO competing representations of g_Currency 0x1673180 — `g_Client.inv.currency`
    //   (written by this module and Net/GameHandlers_InvDispatch.cpp / InvCells.cpp) and
    //   `g_World.self.currency` (written by Net/GameVarDispatch.cpp:163/556 and
    //   Net/CharStatDeltaDispatch.cpp:397, and READ by the HUD — UI/GameHud.cpp:1166). The costs
    //   applied here to inv.currency are therefore not displayed. PRE-EXISTING and
    //   CROSS-CUTTING defect (~15 sites, 4 files not owned): we keep the convention already
    //   in place in this file (cf. 0x16 case 3 above) rather than introducing a double
    //   write here that would double-count against GameVarDispatch for the same opcode.
    OnPacket<CultivationDispatch>(sys, 0x58, [](const CultivationDispatch& p) {
        // g_GmCmdCooldownLatch = 0: the binary sets it PER-CASE, as the 1st instruction of
        // each of cases 1..20 (EA 0x493212 case 1 … 0x49402F case 20), and NEVER on the
        // `default` (def_49320B 0x4941C7 = bare `return` epilogue). Setting it at the top of the
        // lambda would release the lock on an unknown sub-opcode — a real divergence.
        if (p.subOpcode >= 1 && p.subOpcode <= 20) g_GmCmdCooldownLatch = 0;

        switch (p.subOpcode) {
        case 1:  // attribute reset — EVERYTHING is gated by v72==0 (EA 0x49322C)
            if (p.value != 0) break;
            // 0x493255: unspent += (300 + 292 + 304 + 296) - 4; then the 4 attributes set to 1
            // (EA 0x49325A/64/6E/78) — to 1, NOT to 0 (hence the cumulative `-4`).
            // Modeled fields (GameState.h:311-315) and NOT g_Client.Var(0x16731B8..): these are
            // the ones read by the stat engine (StatFormulas.cpp:440/809/1021/1134) and the UI
            // (CharacterStatsWindow.cpp:72-75/145) — the Var() keys have no reader.
            g_World.self.unspentAttr += g_World.self.attrDefensive   // 0x16731B8
                                     +  g_World.self.attrExtForce    // 0x16731BC
                                     +  g_World.self.attrOffensive   // 0x16731C0
                                     +  g_World.self.attrIntForce    // 0x16731C4
                                     -  4;
            g_World.self.attrDefensive  = 1;   // 0x49325A
            g_World.self.attrExtForce   = 1;   // 0x493264
            g_World.self.attrOffensive  = 1;   // 0x49326E
            g_World.self.attrIntForce   = 1;   // 0x493278
            g_Client.Var(0x1687370) = 1;       // 0x493282
            g_Client.Var(0x1687378) = 0;       // 0x49328C
            g_Client.msg.System(Str(601));     // 0x4932A7
            // TODO(ui) [anchor 0x4932BC]: cDrawWin_Init(dword_1839290) 0x628E40 — gamble/draw
            //   popup (this+2 active, clears 9 flags). No abstraction of this popup
            //   exists on the C++ side; don't simulate it.
            break;

        case 2:  // pure messages (EA 0x4932D0)
            switch (p.value) {
            case 0: g_Client.msg.System(Str(775)); break;  // 0x49330E
            case 1: g_Client.msg.System(Str(776)); break;  // 0x493334
            case 2: g_Client.msg.System(Str(778)); break;  // 0x493359
            case 3: g_Client.msg.System(Str(779)); break;  // 0x49337F
            default: break;                                // def_4932F6: return
            }
            break;

        case 3:  // pure message gated on v72==0 (EA 0x49339E / gate 0x4933B8)
            if (p.value == 0) g_Client.msg.System(Str(777));  // 0x4933CC
            break;

        case 4:  // cost 500 (EA 0x4933EB)
            if (p.value == 0) {
                g_Client.inv.currency -= 500;              // 0x49341E g_Currency / 0x49342E mirror
                g_Client.msg.System(Str(783));             // 0x493444
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));             // LABEL_111 0x493ED3
            }
            break;

        case 5:  // latch only, no other effect (EA 0x493489-0x493493)
            break;

        case 6: {  // growth/cultivation: cost tabulated by g_GrowthIndex%100
            if (p.value != 0) break;                       // gate 0x4934B2
            // switch (g_GrowthIndex % 100) -> cost v68 (EA 0x4934DF..0x49356D); any other
            // value = `default: return` (def_4934DF 0x493579) with STRICTLY no effect.
            // g_World.self.growthIndex = g_GrowthIndex 0x1674774 (GameState.h:317): this is the
            // field read by the stat engine (StatFormulas.cpp:310/437/596/724/807/1020);
            // the old write g_Client.Var(0x1674774) had NO reader (dead key).
            int32_t cost;
            switch (g_World.self.growthIndex % 100) {      // 0x4934C4 (idiv 100), 0x4934DF
            case 0:  cost =   800; break;  // 0x4934E6
            case 1:  cost =  1700; break;  // 0x4934F5
            case 2:  cost =  2500; break;  // 0x493501
            case 3:  cost =  3400; break;  // 0x49350D
            case 4:  cost =  4200; break;  // 0x493519
            case 5:  cost =  5100; break;  // 0x493525
            case 6:  cost =  5900; break;  // 0x493531
            case 7:  cost =  6800; break;  // 0x49353D
            case 8:  cost =  7600; break;  // 0x493549
            case 9:  cost =  8500; break;  // 0x493555
            case 10: cost =  9300; break;  // 0x493561
            case 11: cost = 10000; break;  // 0x49356D
            default: return;               // 0x493579: return, no effect
            }
            const int32_t v71 = static_cast<int32_t>(Rd32(p.body));  // memcpy 0x49357E: v71 = body+0
            g_Client.inv.currency -= cost;                 // 0x49359C g_Currency / 0x4935AD mirror
            // EXACT update (0x4935B2-0x4935E4): the binary NEVER assigns
            // g_GrowthIndex = v71 — v71 is a TIER, not an index.
            if (g_World.self.growthIndex != 0 || v71 <= 1)
                ++g_World.self.growthIndex;                // 0x4935DE
            else
                g_World.self.growthIndex = 100 * (v71 - 1) + 1;  // 0x4935CD
            g_Client.Var(0x1687314) = g_World.self.growthIndex;   // 0x4935E9
            RecalcAttackRatingBaseOnly();                  // 0x4935F8 / 0x493607 (2 globals, no clamp)
            g_Client.msg.System(Str(939));                 // 0x49361D (push 3ABh)
            break;
        }

        case 7:  // heart-attribute +/-: v72 >= 3 does NOTHING (0x493656/5F/6C)
            if (p.value > 2) break;
            // Costs shared by the 3 branches v72==0/1/2 (EA 0x49367F/0x4936FF/0x493763).
            g_Client.inv.currency -= 100;                   // g_Currency + mirror dword_1687254
            g_Client.inv.weight   -= 1000000;               // g_InvWeight 0x16732AC
            if (p.value == 0) {
                ++g_Client.Var(0x167477C);                  // 0x4936AD g_CoreAttr (read by StatFormulas.cpp:1021/1134)
                ++g_Client.Var(0x1687318);                  // 0x4936BB
                g_Client.msg.System(Str(1143));             // 0x4936D2
                // TODO(audio) [anchor 0x4936ED]: Snd3D_PlayScaledVolume(flt_14980FC, 0, 100, 1).
            } else if (p.value == 1) {
                g_Client.msg.System(Str(1144));             // 0x493735
                // TODO(audio) [anchor 0x493750]: Snd3D_PlayScaledVolume(flt_14981BC, 0, 100, 1).
            } else {  // p.value == 2
                --g_Client.Var(0x167477C);                  // 0x493790
                --g_Client.Var(0x1687318);                  // 0x49379F
                g_Client.msg.System(Str(1144));             // 0x4937B5
                g_Client.msg.System(Str(1145));             // 0x4937D6
                // TODO(audio) [anchor 0x4937F1]: Snd3D_PlayScaledVolume(flt_149827C, 0, 100, 1).
            }
            break;

        case 8:  // (EA 0x493800)
            if (p.value == 0)      g_Client.Var(0x1674780) = 0;   // 0x493827 — WITHOUT a message
            else if (p.value == 2) g_Client.msg.System(Str(117)); // 0x493843
            break;

        case 9:  // toggle dword_1674798 ON + AR clamp (EA 0x493862)
            if (p.value != 0) break;                       // gate 0x49387C
            g_Client.Var(0x1674798) = 1;                   // 0x493880
            RecalcTalismanAttackRating();                  // 0x493894-0x4938E5 (same clamp)
            break;

        case 10:  // toggle dword_1674798 OFF + same clamp (EA 0x4938F9) — symmetric to 9
            if (p.value != 0) break;                       // gate 0x493913
            g_Client.Var(0x1674798) = 0;                   // 0x493917
            RecalcTalismanAttackRating();                  // 0x49392B-0x49397C
            break;

        case 11:  // latch only, no other effect (EA 0x493990-0x4939AA)
            break;

        case 12:  // (EA 0x4939B4) — sub-switch on v72
            switch (p.value) {
            case 0:
                // The binary DECREMENTS dword_16747D4 (it doesn't assign it) then sets
                // dword_16747D8 = body[0..3] (and not p.value).
                --g_Client.Var(0x16747D4);                          // 0x4939FC
                g_Client.Var(0x16747D8) = static_cast<int32_t>(Rd32(p.body)); // memcpy 0x4939EB / 0x493A05
                g_Client.msg.System(Str(1341));                     // 0x493A1B
                break;
            case 1: g_Client.msg.System(Str(1342)); break;  // LABEL_68 0x493A41
            case 2: g_Client.msg.System(Str(1343)); break;  // 0x493A66
            case 3: g_Client.msg.System(Str(1344)); break;  // 0x493A8C
            case 4: g_Client.msg.System(Str(1336)); break;  // 0x493AB2
            default: break;                                 // return, no effect
            }
            break;

        case 13:  // (EA 0x493AD1) — the binary ZEROES it (does not assign p.value)
            if (p.value == 0) {
                g_Client.Var(0x16747D8) = 0;                // 0x493AF8
                g_Client.msg.System(Str(1339));             // 0x493B13
            } else if (p.value == 1) {
                g_Client.msg.System(Str(1340));             // 0x493B39
            }
            break;

        case 14:  // pure messages (EA 0x493B58)
            if (p.value == 0)      g_Client.msg.System(Str(1493));  // 0x493B99
            else if (p.value == 1) g_Client.msg.System(Str(1494));  // 0x493BBF
            else if (p.value == 2) g_Client.msg.System(Str(1495));  // 0x493BE4
            break;

        case 15:  // (EA 0x493C03) — message FIRST, then cost per level
            switch (p.value) {
            case 0:
                g_Client.msg.System(Str(1768));             // 0x493C40 — BEFORE the cost (0x493C4B)
                // Conditional cost (0x493C60-0x493CB3): g_SelfLevel 0x16731A8 and
                // g_SelfLevelBonus 0x16731AC == g_World.self.level / .levelBonus (GameState.h:308-309).
                // Cf. MODELING NOTE: here the binary does NOT touch the mirror 0x1687254.
                if (g_World.self.level >= 100 && g_World.self.level <= 112) {
                    g_Client.inv.currency -= 20;            // 0x493C6B
                } else if (g_World.self.level >= 113 && g_World.self.level <= 145 &&
                           g_World.self.levelBonus == 0) {
                    g_Client.inv.currency -= 50;            // 0x493C9A
                } else if (g_World.self.levelBonus > 0) {
                    g_Client.inv.currency -= 100;           // 0x493CB3
                }
                break;
            case 1: g_Client.msg.System(Str(1342)); break;  // LABEL_68 0x493A41 (shared with case 12/1)
            case 2: g_Client.msg.System(Str(1803)); break;  // 0x493CF4
            case 3: g_Client.msg.System(Str(1868)); break;  // 0x493D19
            default: break;                                 // return, no effect
            }
            break;

        case 16:  // cost 1 (EA 0x493D33)
            if (p.value == 0) {
                --g_Client.inv.currency;                    // 0x493D63 g_Currency / 0x493D71 mirror
                g_Client.msg.System(Str(783));              // 0x493D87
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));              // LABEL_111 0x493ED3
            }
            break;

        case 17:  // cost 10 (EA 0x493DCC)
            if (p.value == 0) {
                g_Client.inv.currency -= 10;                // 0x493DFC g_Currency / 0x493E0B mirror
                g_Client.msg.System(Str(783));              // 0x493E21
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));              // LABEL_111 0x493ED3
            }
            break;

        case 18:  // WEIGHT cost (EA 0x493E66)
            if (p.value == 0) {
                g_Client.inv.weight -= 500000000;           // 0x493E97 g_InvWeight
                g_Client.msg.System(Str(783));              // 0x493EAD
            } else if (p.value == 1) {
                g_Client.msg.System(Str(784));              // LABEL_111 0x493ED3
            }
            break;

        case 19:
        case 20: {
            // 11 4-byte memcpy from the CONTIGUOUS lvars v73..v83 ([ebp-70h]..[ebp-48h]),
            // all filled by the single `Crt_Memcpy(v73, MEMORY[0x8156C9], 0x64)` (0x4931C7)
            // => v73 = body+0 … v83 = body+40. REAL base = g_AttrBuffActive 0x16758A8.
            // WARNING: the binary SKIPS 0x16758C4 (it chains 0x16758C0 -> 0x16758C8) —
            // a deliberate gap, consumed elsewhere; a contiguous loop would corrupt it.
            // EA case 19: 0x493F07..0x493FC5; case 20: 0x494044..0x494102 (identical).
            g_Client.Var(0x16758A8) = static_cast<int32_t>(Rd32(p.body +  0)); // g_AttrBuffActive 0x493F07
            g_Client.Var(0x16758AC) = static_cast<int32_t>(Rd32(p.body +  4)); // g_AttrBuff300    0x493F1A
            g_Client.Var(0x16758B0) = static_cast<int32_t>(Rd32(p.body +  8)); // g_AttrBuff304    0x493F2D
            g_Client.Var(0x16758B4) = static_cast<int32_t>(Rd32(p.body + 12)); // g_AttrBuff292    0x493F40
            g_Client.Var(0x16758B8) = static_cast<int32_t>(Rd32(p.body + 16)); // g_AttrBuff296    0x493F53
            g_Client.Var(0x16758BC) = static_cast<int32_t>(Rd32(p.body + 20)); //                  0x493F66
            g_Client.Var(0x16758C0) = static_cast<int32_t>(Rd32(p.body + 24)); //                  0x493F79
            g_Client.Var(0x16758C8) = static_cast<int32_t>(Rd32(p.body + 28)); // NB: C4 skipped   0x493F8C
            g_Client.Var(0x16758CC) = static_cast<int32_t>(Rd32(p.body + 32)); //                  0x493F9F
            g_Client.Var(0x16758D0) = static_cast<int32_t>(Rd32(p.body + 36)); //                  0x493FB2
            g_Client.Var(0x16758D4) = static_cast<int32_t>(Rd32(p.body + 40)); //                  0x493FC5
            RecalcAttackRatingSetAll();   // 4 globals as a plain assignment, WITHOUT clamp
            if (p.subOpcode == 19) {
                g_Client.msg.System(Str(2945));            // 0x49401A (push B79h)
            } else {  // subOpcode == 20
                g_Client.inv.currency -= 1000;             // 0x494152 g_Currency / 0x494162 mirror
                if (p.value == 0)      g_Client.msg.System(Str(2943));  // 0x494195 (push B7Fh)
                else if (p.value == 1) g_Client.msg.System(Str(2944));  // 0x4941B7 (push B80h)
            }
            break;
        }

        default:
            break;   // def_49320B 0x4941C7: bare `return` — writes NOTHING (not even the latch).
        }
    });

    // 0x5b QuickslotSync — mode 1 = loads shortcuts; mode 2 = gold/weight.
    // Anchor: Net_OnQuickslotSync 0x4944A0. The nesting follows the binary: the OUTER
    // switch is on v3=mode (0x494517/0x494520), the `if (!v6)` gate is INTERNAL to
    // each mode (0x49452E for mode 1, 0x494588 for mode 2).
    OnPacket<QuickslotSync>(sys, 0x5b, [](const QuickslotSync& p) {
        if (p.mode == 1) {                       // 0x494517
            if (p.flag == 0) {                   // 0x49452E
                for (int i = 0; i < 50; ++i)     // copies quickslot[0..49] -> dword_184C0F8[] (0x49456C)
                    g_Client.Var(0x184C0F8 + static_cast<uint32_t>(i) * 4u) =
                        static_cast<int32_t>(p.quickslot[i]);
            }
        } else if (p.mode == 2) {                // 0x494520
            // g_GmCmdCooldownLatch = 0 as soon as mode==2 (0x494577), INDEPENDENT of v6:
            // the binary releases the lock BEFORE the `if (!v6)` test (0x494588). Nesting it
            // under flag==0 would leave the lock armed on a gold-sync failure.
            g_GmCmdCooldownLatch = 0;
            if (p.flag == 0) {                   // 0x494588
                g_Client.inv.weight = static_cast<int64_t>(p.money);  // g_InvWeight 0x494592
                g_Client.msg.System(Str(640));   // 0x4945A8
            }
        }
    });

    // 0x61 ServerNameNotice — subop1 = message by id (StrTable003); subop2 = 3 floats.
    // Anchor: Net_OnServerNameNotice 0x4A5540.
    OnPacket<ServerNameNotice>(sys, 0x61, [](const ServerNameNotice& p) {
        if (p.subop == 1) {
            uint32_t id = Rd32(p.data);           // memcpy 0x4A5594: data[0..3] = string id
            // 0x4A55AC: StrTable003_Get(dword_84A6A8, v4) — ZONE-NAME table (003.DAT),
            // and NOT StrTable005_Get(g_LangId, …) exposed by game::Str() (005.DAT / mMESSAGE).
            // Distinct tables (different files + strides): the same index yields a
            // different string. Consistent with the RecvPackets.h:731 field comment.
            g_Client.msg.System(Str3(static_cast<int>(id)));
        } else if (p.subop == 2) {
            for (int i = 0; i < 3; ++i)           // data[0..11] = 3 floats -> flt_1687330
                g_Client.VarF(0x1687330 + static_cast<uint32_t>(i) * 4u) = RdF32(p.data + 4 * i);
        }
    });

    // 0x62 Sub_4A55E0 (ea=0x4A55E0) — empty opcode/payload (1 byte): triggers a fixed warp
    // to map 37 (byte_1685748 = local player). Coverage gap closed — function
    // game::BeginWarpToMap37() already existed (Game/MapWarp.h/.cpp), wired here.
    OnTrigger(sys, 0x62, []() {
        BeginWarpToMap37();
        // TODO(net): Net_SendPacket_Op20 (warp confirmation) emitted by ArmFullWarp — no
        //   NetClient in this module, cf. equivalent TODO(net) in Game/MapWarp.cpp.
    });

    // 0x63 ScriptTrigger — GameGuard challenge/verify (ANTICHEAT). Ignored by contract:
    // no gameplay data, no state effect to reproduce on the rewritten client.
    OnPacket<ScriptTrigger>(sys, 0x63, [](const ScriptTrigger&) {
        // Anticheat deliberately ignored (Ac_GuardClient_MakeVerifyData).
    });

    // 0x66 PetSlotDispatch — talisman/pet slot dispatcher (sub-ops 1..8).
    OnPacket<PetSlotDispatch>(sys, 0x66, [](const PetSlotDispatch& p) {
        int32_t& slot = g_Client.Var(kTalismanSlot);  // g_TalismanSlot
        switch (p.subop) {
        case 1: slot = static_cast<int32_t>(p.value); break;
        case 2: slot = -1; break;
        case 3: slot += 10; break;
        case 4: slot -= 10; break;
        case 5:  // clears the current slot
            if (slot >= 0) {
                g_Client.Var(0x1674738 + static_cast<uint32_t>(slot) * 4u) = 0; // dword_1674738[]
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = 0; // dword_167568C[]
                g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = 0; // dword_16756B4[]
            }
            slot = -1;
            g_Client.inv.weight -= 100000000;  // g_InvWeight -= 1e8
            g_Client.msg.System(Str(1511));
            break;
        case 6:  // activates a talisman attribute (AR recompute). 0x4A58AE
            if (p.value != 0) {                                   // 0x4A58AE: if (v13)
                if (slot >= 0) {                                  // 0x4A58DD
                    int32_t hi, lo, packed;
                    if (slot >= 10) {                             // 0x4A58F9
                        // Unpack dword_1675664[slot], force lo=0, repack, write v12.
                        game::Stat_UnpackCombined(g_Client.VarGet(0x1675664 + static_cast<uint32_t>(slot) * 4u), hi, lo); // 0x54CE40
                        game::Stat_PackCombined(hi, 0, packed);   // v15=0; 0x54CEB0
                        g_Client.Var(0x1675664 + static_cast<uint32_t>(slot) * 4u) = packed;              // 0x4A599E
                        g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A59AE
                    } else {
                        game::Stat_UnpackCombined(g_Client.VarGet(0x167568C + static_cast<uint32_t>(slot) * 4u), hi, lo); // 0x4A5916
                        game::Stat_PackCombined(hi, 0, packed);   // 0x4A5933
                        g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = packed;              // 0x4A5940
                        g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5950
                    }
                    RecalcTalismanAttackRating();                 // 0x4A59BF..
                    g_Client.msg.System(Str(2165));               // 0x4A5A25
                }
                // slot<0: no-op (no message) — faithful to 0x4A58D1 "return result"
            } else {
                g_Client.msg.System(Str(2166));                  // 0x4A58C1: value==0 ONLY
            }
            break;
        case 7:  // sets a value into the slot (AR recompute). 0x4A5A3C
            if (slot < 0) break;                                  // 0x4A5A41: no-op
            if (slot >= 10) {
                if (slot >= 20) break;                            // 0x4A5A7E: no-op
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5A88
            } else {
                g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5A65
            }
            RecalcTalismanAttackRating();                         // 0x4A5AA0..
            g_Client.msg.System(Str(2213));                      // 0x4A5B07
            break;
        case 8:  // same as 7, different message. 0x4A5B1E
            if (slot < 0) break;                                  // 0x4A5B23: no-op
            if (slot >= 10) {
                if (slot >= 20) break;                            // 0x4A5B5F: no-op
                g_Client.Var(0x167568C + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5B6A
            } else {
                g_Client.Var(0x16756B4 + static_cast<uint32_t>(slot) * 4u) = static_cast<int32_t>(p.value); // 0x4A5B46
            }
            RecalcTalismanAttackRating();                         // 0x4A5B82..
            g_Client.msg.System(Str(2181));                      // 0x4A5BE9
            break;
        default: break;
        }
    });

    // 0x6f SkillCooldownSet — sets a skill's cooldown (table dword_18217D0).
    OnPacket<SkillCooldownSet>(sys, 0x6f, [](const SkillCooldownSet& p) {
        if (p.skillId >= 1 && p.skillId <= 351)
            g_Client.Var(0x18217D0 + p.skillId * 4u) = static_cast<int32_t>(p.value);
    });

    // 0x71 Sub_4A7150 (ea=0x4A7150, 5 bytes) — coverage gap closed. Confirmed stub with
    // no observable effect in the binary (Crt_Memcpy into a discarded local): payload
    // read then ignored. Registered explicitly (rather than left without a handler) for
    // dispatch-coverage fidelity — cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md.
    OnTrigger(sys, 0x71, []() {});

    // 0x72 RevivePrompt — flag==0: self HP to 0 + arms the revive prompt.
    OnPacket<RevivePrompt>(sys, 0x72, [](const RevivePrompt& p) {
        if (p.flag == 0) {
            g_Client.Var(0x1687378) = 0;       // dword_1687378[0] (self HP)
            g_World.self.hp = 0;               // modeled mirror
            g_Client.Var(kPendingStopReq) = 1; // g_PendingStopRequest=1
        }
    });

    // 0x73 CountdownTimerStart — starts a countdown (timeGetTime base).
    OnPacket<CountdownTimerStart>(sys, 0x73, [](const CountdownTimerStart& p) {
        g_Client.Var(0x183914C) = static_cast<int32_t>(p.mode);  // mode
        g_Client.Var(0x1839150) = static_cast<int32_t>(p.f1);
        g_Client.Var(0x1839154) = static_cast<int32_t>(p.f2);
        g_Client.Var(0x1839134) = 4;   // timer active
        // TODO(state): dword_1839158 = timeGetTime() (start timestamp, ms since
        //   Windows boot). DO NOT FORCE: no wall-clock abstraction (Win32
        //   timeGetTime/GetTickCount) exists in the Game/Net layers — the only time
        //   available here is g_World.gameTimeSec (flt_815180, GAME time at a fixed 30 FPS,
        //   DIFFERENT semantics from a real-time timestamp); substituting one for the
        //   other would break fidelity rather than preserve it. The real countdown (App/
        //   30 FPS loop) must supply this timestamp at UI integration time.
        // TODO(audio): Snd3D_PlayScaledVolume if mode==-1 or mode==1 (audio module not wired
        //   in this network dispatcher, cf. identical stub in Net/GameVarDispatch.cpp).
    });

    // 0x76 MinigameStateLoad — loads 4 dwords of minigame state.
    OnPacket<MinigameStateLoad>(sys, 0x76, [](const MinigameStateLoad& p) {
        g_Client.Var(0x1675D20) = static_cast<int32_t>(p.a);
        g_Client.Var(0x1675D24) = static_cast<int32_t>(p.b);
        g_Client.Var(0x1675D28) = static_cast<int32_t>(p.c);
        g_Client.Var(0x1675D2C) = static_cast<int32_t>(p.d);
    });

    // 0x7d SkillAuraSync — mini-dispatcher of skill/aura toggle states.
    OnPacket<SkillAuraSync>(sys, 0x7d, [](const SkillAuraSync& p) {
        switch (p.subOpcode) {
        case 0: {  // decodes value in base-10 into dword_1675DB8/BC/C0/C4
            int32_t v = p.value;
            g_Client.Var(0x1675DB8) = v / 1000000;
            g_Client.Var(0x1675DBC) = (v % 1000000) / 10000;
            g_Client.Var(0x1675DC0) = (v % 10000) / 100;
            g_Client.Var(0x1675DC4) = v % 100;
            // `else if (!dword_1675DB8[g_LocalElement])` branch (0x4A9E70): besides
            // loading a model, the binary also zeroes TWO states (0x4A9E8B / 0x4A9E9E).
            // These resets are pure state (no dependency on the .IMG asset) — the scope
            // invoked by the TODO(state) below doesn't cover them.
            if (g_Client.VarGet(0x1675DB8 + g_LocalElement * 4u) == 0) {
                g_Client.Var(0x1675D98 + g_LocalElement * 4u)  = 0;      // 0x4A9E8B
                g_Client.VarF(0x1675DA8 + g_LocalElement * 4u) = 0.0f;   // 0x4A9E9E
            }
            // TODO(state) [anchor 0x4A9E41 / 0x4A9E81]: World_LoadCurrentZoneModel(g_GameWorld,
            //   g_LocalElement+1) if dword_1675DB8[elem]==1, else (…, 6) — plus, in the ==1
            //   branch, flt_1675DA8[elem] = ModelObj_GetSubObjectCount(&unk_B68CCC, 0) - 1 (0x4A9E61),
            //   which DEPENDS on the loaded model: not computable without the asset. Out of
            //   scope for the network layer.
            break;
        }
        case 2:  // indexed toggle (value 0..4)
            if (p.value >= 0 && p.value <= 4) {
                uint32_t idx = static_cast<uint32_t>(p.value);
                g_Client.Var(0x1675D98 + idx * 4u) = 1;
                g_Client.Var(0x1675DB8 + idx * 4u) = 1;
                g_Client.VarF(0x1675DA8 + idx * 4u) = 0.0f;
            }
            break;
        case 5: {  // system line + floating message, SAME buffer
            // 0x4A9EF2-0x4A9F05: Crt_Vsnprintf(v6, "[%d]%s", v7, StrTable005_Get(245))
            // -> left-to-right order = "[value]Str245" (not "Str245 value").
            const std::string buf = Fmt("[%d]%s", p.value, Str(245).c_str());
            g_Client.msg.System(buf, 1);         // 0x4A9F18: Msg_AppendSystemLine(_, v6, 1)
            g_Client.msg.Floating(0, 0, buf);    // 0x4A9F2F: HUD_ShowFloatingMessage(_, 0, 0, v6, &String)
            break;
        }
        case 6:
            g_Client.Var(0x1675DCC)  = 1;               // 0x4A9F39
            g_Client.VarF(0x1675DD4) = 0.0f;            // 0x4A9F45
            g_Client.msg.Floating(0, 0, Str(246));      // 0x4A9F69: HUD_ShowFloatingMessage(_, 0, 0, Str246, &String)
            g_Client.msg.System(Str(246), 1);           // 0x4A9F85: Msg_AppendSystemLine(_, Str246, 1)
            break;
        case 7:
        case 9:
            g_Client.msg.System(Str(237));
            BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
            break;
        case 8:
            if (static_cast<uint32_t>(p.value) == g_LocalElement) {
                g_Client.msg.System(Str(1919));
                BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
            }
            break;
        default: break;
        }
    });

    // 0x82 Sub_4AAB60 (ea=0x4AAB60, 61 bytes) — coverage gap closed. Raw copy of 60
    // bytes (15 floats) into the flt_1676130 global block; no parsing/branching in
    // the original binary (Docs/TS2_PROTOCOL_SPEC.md #0x82).
    OnPacket<RawFloatBlob15>(sys, 0x82, [](const RawFloatBlob15& p) {
        for (int i = 0; i < 15; ++i)
            g_Client.VarF(0x1676130 + static_cast<uint32_t>(i) * 4u) = p.values[i];
    });

    // 0x84 SummonSpawn — summon teleport to a fixed position.
    // Anchor: Net_OnSummonSpawn 0x4AA810 — v3=status @0x8156C1 (0x4AA846), v1=slot @0x8156C5
    // (0x4AA859), guard `if (v1 < 4 && !v3)` (0x4AA879), then `return Warp_SendTeleport(v1, v2)`
    // (0x4AA88B) with v2 = {-14.0f, 0.0f, -4242.0f} (0x4AA82A/2F/38).
    OnPacket<SummonSpawn>(sys, 0x84, [](const SummonSpawn& p) {
        if (p.slot < 4u && p.status == 0) {
            // Hardcoded summon position in the original handler.
            const float pos[3] = { -14.0f, 0.0f, -4242.0f };
            // game::Warp_SendTeleport 0x5F5CE0 (MapWarp.h:187, body MapWarp.cpp:281): arms the
            // warp (mode 6, zones {138,139,165,166}) AND emits Op20 (EA 0x5F5DD6). The `nc`
            // parameter stays at its default nullptr -> ArmFullWarp resolves it to
            // net::GlobalNetClient() (MapWarp.cpp:86-87): the send is REAL, consistent with the
            // binary which addresses g_NetClient as a global. The internal guard (a1<=3 &&
            // !g_MorphInProgress, EA 0x5F5D1A) is ported.
            game::Warp_SendTeleport(static_cast<uint16_t>(p.slot), pos);
        }
    });

    // 0x85 SystemNotice — system notice messages (3 variants).
    OnPacket<SystemNotice>(sys, 0x85, [](const SystemNotice& p) {
        switch (p.subOpcode) {
        case 0:  // "[value]str1479"
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1479));
            break;
        case 1:  // "[value]str843"
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(843));
            break;
        case 2:  // value 0..7 -> str1996..2003 as a floating message + system line
            if (p.value <= 7) {
                std::string t = Str(1996 + static_cast<int>(p.value));
                g_Client.msg.Floating(1, 0, t);
                g_Client.msg.System(t);
            }
            break;
        default: break;
        }
    });

    // 0x8f Sub_4AB020 (ea=0x4AB020) — empty opcode/payload (1 byte): triggers a warp to
    // the local player's faction town. Coverage gap closed — reuses
    // game::BeginWarpToFactionTown ("forced"/non-Ex mode), already wired elsewhere
    // (GameHandlers_BossWorld.cpp/GameHandlers_PartyGuild.cpp) for the same semantics.
    OnTrigger(sys, 0x8f, []() {
        BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
    });

    // 0x99 AchievementNotice — achievement notification (name + local state dword_184C218).
    // Delegates to game::PostAchievementNotice (Game/SocialSystem.h/.cpp), which faithfully
    // reproduces idx=TribeSkill_SkillIdToIndex(g_SelfMorphNpcId) then
    // "<str(dword_184C218[idx]%100+2249)> <name> <str2305>" — g_Achievements is fed
    // by AchievementDataLoad (opcode 0x98, GameHandlers_BossWorld.cpp). If idx is invalid
    // (unknown skill/morph), the original shows NOTHING (EA 0x4aca3a) — faithfully
    // reproduced here (silence, no approximate fallback).
    OnPacket<AchievementNotice>(sys, 0x99, [](const AchievementNotice& p) {
        std::string name(p.name, strnlen(p.name, sizeof p.name));
        const int32_t morph = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
        PostAchievementNotice(g_Achievements, morph, name);
    });

    // 0xae BuffEffectDispatch — buff/stat dispatcher + inventory cell update.
    // Anchor: Net_OnBuffEffectDispatch 0x4A88D0 (7 memcpy, EA 0x4A891F..0x4A89BE = 28 useful bytes).
    OnPacket<BuffEffectDispatch>(sys, 0xae, [](const BuffEffectDispatch& p) {
        switch (p.subOpcode) {
        case -1:  // (EA 0x4A91EF) — sets two stat values THEN emits str2659
            g_Client.Var(0x1675894) = p.param5;   // 0x4A91EF
            g_Client.Var(0x1675898) = p.param6;   // 0x4A91F8
            // TODO(player) [anchor 0x4A9202]: Player_CheckStateDigit(&g_PlayerCmdController)
            //   0x511740 — the only C++ port is an EMPTY stub local to the TU GameVarDispatch.cpp
            //   (l.127), not declared in a header: calling it would be a misleading no-op.
            g_Client.msg.System(Str(2659));       // 0x4A9218 — case 5 does NOT have this message
            break;
        case 5:  // (EA 0x4A91D2) — same two Var writes, but NO message (unlike case -1)
            g_Client.Var(0x1675894) = p.param5;   // 0x4A91D2
            g_Client.Var(0x1675898) = p.param6;   // 0x4A91DA
            // TODO(player) [anchor 0x4A91EA]: Player_CheckStateDigit (cf. case -1).
            break;
        case 1: {  // places an item in the inventory grid (EA 0x4A89E2)
            uint32_t count = (p.param1 == 12101) ? 99u : 0u;   // 0x4A8A38 / 0x4A8A49 / 0x4A8A65
            g_Client.inv.Set(p.param2, p.param3, static_cast<uint32_t>(p.param1),
                             p.param4 % 8u, p.param4 / 8u, count, 0, 0);
            g_Client.Var(0x1675894) = p.param5;   // 0x4A8AA7 — was missing until now
            g_Client.Var(0x1675898) = p.param6;   // 0x4A8AB0
            // TODO(player) [anchor 0x4A8ABA]: Player_CheckStateDigit (cf. case -1).
            g_Client.msg.System(Str(2901));       // 0x4A8AD0 — was missing until now
            break;
        }
        case 2: g_Client.msg.System(Str(598));  break;  // 0x4A8AF6
        case 4: g_Client.msg.System(Str(1257)); break;  // 0x4A91BD
        case 3: {  // temporal-effects sub-dispatcher (EA 0x4A8B0E)
            g_Client.Var(0x1675894) = p.param5;   // 0x4A8B0E
            g_Client.Var(0x1675898) = p.param6;   // 0x4A8B16
            // TODO(player) [anchor 0x4A8B21]: Player_CheckStateDigit (cf. case -1).
            const int v26 = p.param1 / 1000;      // 0x4A8B31
            // 0x4A8B45: v31 = -v35 % 1000. Under truncating division (C/C++ like x86 idiv),
            // (-a)%b and -(a%b) are IDENTICAL — the form below is equivalent.
            const int v31 = -(p.param1 % 1000);

            // 1st switch (0x4A8B87): v26 -> v27 (ITEM id) or v30 (STRING id).
            // v27/v30 initialized to 0 (EA 0x4A8B48 / 0x4A8B4F); neither -14 nor -5 has a case.
            int v27 = 0;  // item id (MobDb_GetEntry)
            int v30 = 0;  // string id (StrTable005)
            switch (v26) {
            case -16: v27 =  1894; break;  // 0x4A8C1E
            case -15: v27 =  1097; break;  // 0x4A8C12
            case -13: v27 =  1166; break;  // 0x4A8C06
            case -12: v27 =  1124; break;  // 0x4A8BFA
            case -11: v27 =  1103; break;  // 0x4A8BEE
            case -10: v27 =  1108; break;  // 0x4A8BE2
            case  -9: v27 = 12105; break;  // 0x4A8BD6
            case  -8: v27 =   869; break;  // 0x4A8BCA
            case  -7: v30 =  2318; break;  // 0x4A8BC1
            case  -6: v30 =   918; break;  // 0x4A8BB8
            case  -4: v30 =  2647; break;  // 0x4A8BAF
            case  -3: v30 =  2646; break;  // 0x4A8BA6
            case  -2: v30 =  2645; break;  // 0x4A8B9A
            case  -1: v30 =  2644; break;  // 0x4A8B8E
            default: break;
            }

            // 2nd switch (0x4A8C2F): counters as `+=` (DELTA) — DISTINCT semantics from the
            // `= value` that Net/GameVarDispatch.cpp applies to the SAME globals under 0x16.
            //
            // ASSUMED DEVIATION (original UB): when MobDb_GetEntry returns 0, the original still
            // emits the line with an UNINITIALIZED v28 (1000-byte stack local — EA 0x4A8CB9,
            // 0x4A8DF1, 0x4A8E74, 0x4A8EF7, 0x4A8F7D, 0x4A8FFF, 0x4A9084). Undefined behavior,
            // not reproducible: we emit an EMPTY string. In practice unreachable
            // (v27 are hardcoded ids, present in the item DB).
            const char* nm = nullptr;
            switch (v26) {
            case -6:   // no counter; the format IS Str(v30), with no argument
                g_Client.msg.System(FmtFromStrTable(v30));                  // 0x4A8C3A/47/0x4A8DB1
                break;
            case -7:   // no counter; Str(v30) formatted with 180
                g_Client.msg.System(FmtFromStrTable(v30, 180));             // 0x4A8C6B/78/80
                break;
            case -8:
                g_Client.Var(0x167587C) += v31;                             // 0x4A8C96
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8CAC
                g_Client.msg.System(nm ? FmtFromStrTable(2825, nm, v31) : std::string()); // LABEL_53 0x4A9001/26
                break;
            case -9: {  // no counter
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8D28
                std::string s;
                if (nm) s = (v31 == 1) ? FmtFromStrTable(2823, nm, 1)       // 0x4A8D55/62
                                       : FmtFromStrTable(2824, nm, v31);    // 0x4A8D84/91
                g_Client.msg.System(s);                                     // 0x4A8D6A
                break;
            }
            case -10:
                g_Client.Var(0x16746E4) += v31;                             // 0x4A8DCD
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8DE4
                g_Client.msg.System(nm ? FmtFromStrTable(2826, nm) : std::string()); // LABEL_49 0x4A8F7F/A0
                break;
            case -11:
                g_Client.Var(0x16746E8) += v31;                             // 0x4A8E50
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8E67
                g_Client.msg.System(nm ? FmtFromStrTable(2826, nm) : std::string()); // LABEL_49
                break;
            case -12:
                g_Client.Var(0x1674708) += v31;                             // 0x4A8ED3
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8EEA
                g_Client.msg.System(nm ? FmtFromStrTable(2825, nm, v31) : std::string()); // LABEL_53
                break;
            case -13:
                g_Client.Var(0x1674794) += v31;                             // 0x4A8F59
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8F70
                g_Client.msg.System(nm ? FmtFromStrTable(2826, nm) : std::string()); // LABEL_49
                break;
            case -15:
                g_Client.Var(0x1674700) += v31;                             // 0x4A8FDB
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A8FF2
                g_Client.msg.System(nm ? FmtFromStrTable(2825, nm, v31) : std::string()); // LABEL_53
                break;
            case -16:
                g_Client.Var(0x1674AA0) += v31;                             // 0x4A9061
                nm = ItemNameOrNull(static_cast<uint32_t>(v27));            // 0x4A9077
                g_Client.msg.System(nm ? FmtFromStrTable(2999, nm) : std::string()); // 0x4A909A/A7
                break;
            case -17: {  // no counter; double literal formatting
                const std::string a = Fmt("%s %d", Str(2293).c_str(), v31); // 0x4A90E8/FA
                g_Client.msg.System(Fmt("%s%s", a.c_str(), Str(2648).c_str())); // 0x4A910C/25/40
                break;
            }
            default:  // 0x4A915B — covers v26 = -14, -5..-1 and out-of-range
                g_Client.msg.System(Fmt("%s%s %d%s", Str(2532).c_str(), Str(v30).c_str(),
                                        v31, Str(2648).c_str()));           // 0x4A9179/8B/A5
                break;
            }
            break;
        }
        default: break;   // case 0 and out-of-range: bare `return` (0x4A89C4)
        }
    });

    // 0xb1 Sub_4B33C0 (ea=0x4B33C0, 5 bytes) — coverage gap closed. Trivial setter:
    // copies value into dword_16874A0 and clears the busy latch (Docs/TS2_PROTOCOL_SPEC.md #0xb1).
    OnPacket<RawU32Setter>(sys, 0xb1, [](const RawU32Setter& p) {
        g_GmCmdCooldownLatch = 0;                              // dword_1675B08 -> 0
        g_Client.Var(0x16874A0) = static_cast<int32_t>(p.value);
    });
}

} // namespace ts2::net

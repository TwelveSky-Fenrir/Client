// Net/GameHandlers_ChatSocial.cpp — routing for the "chat_social" domain.
//
// "chat_social" domain (RE/handler_domains.json): chat/whisper/faction/friends,
// notices, confirmation prompts (Dlg10/14/19/20) and result dialogs. Faithfully
// translates the original handlers' state-update logic (RE/net_handler_notes.md)
// to the game::g_Client hub (message log, prompts, notice registry, scalar
// globals via Var()).
//
//   0x14 ChatNotice            0x29 WhisperReceive        0x2a PartyChatOrInvite
//   0x2b ShoutMessage          0x3a FactionBoardSync      0x3b ConfirmPromptOpenDlg19
//   0x3c ConfirmPromptClose19  0x41 ConfirmPromptOpen20   0x42 ConfirmPromptClose20
//   0x43 TradeResultDialog     0x44 RequestTargetNameSet  0x45 RequestCancelClear
//   0x46 RequestStateSet       0x47 ConfirmPromptOpen10   0x48 ConfirmPromptClose10
//   0x49 ResultDialog340       0x50 ConfirmPromptOpen14   0x51 ConfirmPromptClose14
//   0x52 ResultDialog399       0x55 FactionChatMessage    0x57 SelfFactionChat
//   0x59 WhisperMessage        0x5a TradeChatMessage      0x79 SocialListRemove
//   0x7e FriendStatusNotice    0x8b TradeChatMsg          0x90 FriendListEvent
//   0x9f NpcDialogEvent
#include "Net/GameHandlers.h"
#include "Game/ClientRuntime.h"
#include "Net/SendPackets.h"
#include "Config/GameOptions.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {
namespace {

// Reads a fixed-size char[N] field (potentially unterminated) into a std::string.
inline std::string Fixed(const char* s, size_t n) { return std::string(s, strnlen(s, n)); }

// Chat channel colors: g_ChatColor_* globals in the binary (0x84DFDC..0x84DFF4),
// NOT modeled here. The selection LOGIC (which channel / GM vs normal) is preserved;
// the exact ARGB value (D3DCOLOR) is unknown -> stable, distinct placeholders.
// Verified via decompilation+xrefs idaTs2 (2026-07-14): these 7 dwords are zero-initialized
// in .data and have NO write site anywhere in the binary (only reads,
// from Pkt_WhisperReceive/Pkt_ShoutMessage/Pkt_PartyChatOrInvite/Net_OnGuild*/
// Net_On*FactionChat/Net_OnTradeChatMessage/Char_DrawNameplate/UI_GameHud_Render) — the
// real ARGB value is therefore set by a mechanism outside the static binary (runtime-loaded
// config/skin, not captured by this pass), not just "not yet searched for".
constexpr uint32_t kChatColGM      = 0xFFFF3030u; // g_ChatColor_GM
constexpr uint32_t kChatColShout   = 0xFFFFC030u; // g_ChatColor_Shout
constexpr uint32_t kChatColFaction = 0xFF3080FFu; // g_ChatColor_Faction
constexpr uint32_t kChatColParty   = 0xFF30FF30u; // g_ChatColor_Party
constexpr uint32_t kChatColTrade   = 0xFFFFA030u; // g_ChatColor_Trade

// ---------------------------------------------------------------------------
// CHANNEL DISPLAY GATES (g_ChatShow_*) — deliberately NOT implemented here.
//
// Handlers 0x29/0x2b/0x55/0x5a only emit their line if the channel flag is exactly
// 1: g_ChatShow_Whisper (0x184C634) 0x48f278, g_ChatShow_Shout (0x184C644)
// 0x48f6b5, g_ChatShow_Faction (0x184C63C) 0x493058, g_ChatShow_Trade (0x184C640) 0x494465.
//
// What Pass 4 established (and why they can't be wired from Net):
//  - These are FIELDS of the g_ChatManager 0x184C3C8 object (+0x26C whisper, +0x274 faction,
//    +0x278 trade, +0x27C shout), hence no visible write in absolute xrefs.
//  - Their RUNTIME default is 1, set by UI_GameHud_Init 0x675140 (`[this+26Ch]=1` 0x67519e,
//    +0x274 0x6751b8, +0x278 0x6751c5, +0x27C 0x6751d2); and `this == g_ChatManager` is
//    proven by the call site `mov ecx, offset g_ChatManager` 0x5AC1A3 / `call
//    UI_GameHud_Init` 0x5AC1A8 (UI_InitAllDialogs). The STATIC bytes at 0x184C634..0x184C644
//    are all ZERO (get_bytes): the init is therefore mandatory.
//  - The only writers afterward are pure UI toggles (UI_RouteLButtonDown 0x5AC740,
//    `cmp ==1 -> 0 else 1`, e.g. 0x5acb18-0x5acb2d) = the HUD's channel tab buttons.
//
// Consequence: setting these gates here via Var() (default 0) would make ALL chat
// DISAPPEAR — trading a minor defect for a serious regression. Seeding them to 1 from a
// network handler registration would be a misplaced HUD init, and without a writer (UI, out
// of this front's scope) the gate would stay constant at 1 = zero observable change.
// -> Wiring to add on the UI side: init to 1 in the UI_GameHud_Init equivalent + toggle in
//    the UI_RouteLButtonDown equivalent; the gates can then be wired here.
// ---------------------------------------------------------------------------

// Original modal notice registry: dword_18225D0 (active) / dword_18225D8 (type).
// Distinct from the MsgBox prompt (dword_1822440/1822450 = game::PromptState). Faithfully
// modeled via Var(): several result dialogs close this notice.
inline void CloseNoticeIf(int type) {
    if (game::g_Client.Var(0x18225D0) != 0 && game::g_Client.Var(0x18225D8) == type)
        game::g_Client.Var(0x18225D0) = 0;
}

} // namespace

void RegisterChatSocialHandlers(NetSystem& sys) {
    using namespace game;  // g_Client, Str()

    // 0x14 ChatNotice — text notice: floating banner + system log (g_SysMsgColor).
    OnPacket<ChatNotice>(sys, 0x14, [](const ChatNotice& p) {
        std::string t = Fixed(p.text, sizeof p.text);
        g_Client.msg.Floating(0, 0, t);
        g_Client.msg.System(t);
    });

    // 0x29 WhisperReceive — received whisper: "[sender] message" line on the whisper channel.
    OnPacket<WhisperReceive>(sys, 0x29, [](const WhisperReceive& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        // Pkt_WhisperReceive 0x48f210. Format "[%s] %s"(name, message) — 0x48f269: faithful.
        //
        // KNOWN DEFECT (cross-wave wiring required, NOT fixable from this file):
        // the binary copies the first 4 bytes of the name into a pre-zeroed buffer (0x48f2a9)
        // then `if (Crt_Strcmp(&v0, "[GM]")) -> g_ChatColor_Whisper (0x48f304) else
        // -> g_ChatColor_GM (0x48f2e3)` — i.e., GM color iff the name starts with
        // "[GM]" (exact equivalent of strncmp(name,"[GM]",4)==0, see handler 0x2b below which
        // does implement this test). Here the line always goes out in whisper color, because
        // MessageLog::Whisper(t, who) (Game/ClientRuntime.h:44) has NO color parameter and
        // hardcodes 0xFFFF80FF (ClientRuntime.cpp:20); Game/ClientRuntime.* belongs to
        // another wave. Switching to msg.Chat(txt, color, who) would trade one defect for
        // another: MsgKind::Whisper drives the "unread" badge on the Whisper tab
        // (UI/ChatWindow.cpp:157-162), which would be lost.
        // -> Wiring to add: overload `Whisper(const std::string&, const char*, uint32_t)`
        //    keeping MsgKind::Whisper, then `gm ? kChatColGM : <whisper color>` here.
        //
        // Gate g_ChatShow_Whisper==1 (0x48f278): see the "channel gates" note at the top of
        // the file — deliberately NOT implemented here (setting it without its initializer
        // would make all chat disappear). The bubble loop below is OUTSIDE the gate in the
        // binary (0x48f309): its position here is therefore correct.
        g_Client.msg.Whisper("[" + who + "] " + msg, who.c_str());
        // Arms the "chat bubble" flag of the same-name player entity (RE/net_handler_notes.md:
        // dword_1687520[227*i]=1, timestamp unk_1687524=g_GameTimeSec; stride 908 bytes = 227*4,
        // same convention as PartyGuild::FindPlayerIndex). PlayerEntity::name (Game/GameState.h)
        // allows the name lookup, contrary to the original TODO that called it unmodeled.
        for (size_t i = 0; i < g_World.players.size(); ++i) {
            if (g_World.players[i].active && g_World.players[i].name == who) {
                g_Client.Var(0x1687520 + 908u * static_cast<uint32_t>(i)) = 1;
                g_Client.VarF(0x1687524 + 908u * static_cast<uint32_t>(i)) = g_World.gameTimeSec;
                // TODO(state): reinit of the bubble string unk_1687528 (text shown in the
                //   bubble) — no per-entity storage for this text in PlayerEntity;
                //   out of scope for this pass (would require extending GameState.h).
                break;
            }
        }
    });

    // 0x2a PartyChatOrInvite — party notifications (joined/left) or party chat message.
    // Transcription of the 4 Crt_Vsnprintf calls in Pkt_PartyChatOrInvite 0x48f3c0 (formats
    // re-read in disassembly: cdecl vararg order is pushed right->left).
    OnPacket<PartyChatOrInvite>(sys, 0x2a, [](const PartyChatOrInvite& p) {
        std::string name = Fixed(p.name, sizeof p.name);
        std::string msg  = Fixed(p.message, sizeof p.message);
        switch (p.selector) {
        case 0:  // joined: system line + party chat line.
            // "[%s]%s"(name, Str(299)) -> Msg_AppendSystemLine — 0x48f484 / 0x48f49e.
            g_Client.msg.System("[" + name + "]" + Str(299));
            // "%s %s"(Str(302), message) -> Msg_AppendChatLine(g_ChatColor_Party, &String) —
            // 0x48f4c6 / 0x48f4e6. The 4th argument (speaker) is the `String` global 0x7ec95f,
            // whose byte 0 is 0x00 (verified via get_bytes) = EMPTY string, NOT the name: the
            // welcome line is not attributed to anyone. The payload's message (v9) is
            // indeed the 2nd vararg — it was lost by the earlier form "[name]Str302".
            g_Client.msg.Chat(Str(302) + " " + msg, kChatColParty, "");
            break;
        // "[%s]%s"(name, Str(300)) — 0x48f513 ; "[%s]%s"(name, Str(301)) — 0x48f55b.
        // The name (v11) is the FIRST vararg: without it the player can't see WHO left/joined.
        case 1: g_Client.msg.System("[" + name + "]" + Str(300)); break;
        case 2: g_Client.msg.System("[" + name + "]" + Str(301)); break;
        case 3:  // party chat message.
            // Guard 0x48f57f-0x48f58f: `cmp [ebp+var_448], 1 / jge` then
            // `cmp g_Opt_FilterPartyChat, 0 / jnz` = `if (v8 >= 1 || g_Opt_FilterPartyChat)`.
            // `jge` = SIGNED comparison -> filterFlag is re-read as int32_t (a uint32_t would
            // flip 0x80000000..0xFFFFFFFF to the wrong side of the guard).
            // g_Opt_FilterPartyChat 0x84DEFC IS modeled (Config/GameOptions.h:114, default 1
            // @GameOptions.cpp:42): the old "not modeled" comment was stale, and the missing
            // disjunction made Str(370) show up as soon as the server sent 0.
            if (static_cast<int32_t>(p.filterFlag) >= 1 || config::g_Options.FilterPartyChat != 0)
                // TODO(audio) [anchor 0x48f5bf]: Snd3D_PlayScaledVolume(flt_1490B3C, 0, 100, 1)
                //   — audio module not wired from Net (same convention as
                //   GameHandlers_Misc.cpp:452).
                // "%s [%s] %s"(Str(302), name, message), speaker = v11 = name — 0x48f5ee / 0x48f610.
                g_Client.msg.Chat(Str(302) + " [" + name + "] " + msg, kChatColParty, name.c_str());
            else
                g_Client.msg.System(Str(370));  // 0x48f5a2 / 0x48f5ad
            break;
        default: break;
        }
    });

    // 0x2b ShoutMessage — shout/broadcast: "<str114> [sender] message", GM or shout color.
    OnPacket<ShoutMessage>(sys, 0x2b, [](const ShoutMessage& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        bool gm = (std::strncmp(p.senderName, "[GM]", 4) == 0);
        // Gate g_ChatShow_Shout==1 (Pkt_ShoutMessage 0x48f640 @0x48f6b5): not implemented,
        // see "CHANNEL DISPLAY GATES" note at the top of the file.
        g_Client.msg.Chat(Str(114) + " [" + who + "] " + msg,
                          gm ? kChatColGM : kChatColShout, who.c_str());
    });

    // 0x3a FactionBoardSync — quiver/faction resync (the payload only carries the code).
    // Net_OnFactionBoardSync 0x490560. The CORE of the handler is the staging -> live commit: the
    // staging is produced by handler 0x38 GuildInfoUpdate (GameHandlers_PartyGuild.cpp:188-217,
    // which explicitly documents handing off to this file) and is OBSERVABLE only through this
    // commit. The 9 live destinations are not modeled as dedicated fields: the
    // Var()/VarGet() escape hatch from ClientRuntime.h:163 is used (same convention as staging).
    // Strides re-derived from the IDA `refs` (indices on `int`): g_QuiverMain 0x1673EB4 /
    // g_QuiverCount 0x1673EB8 / g_QuiverSocket 0x1673EBC / g_QuiverSerial 0x1673EC0 are
    // indexed [4*i] -> stride 16 bytes; g_QuiverAux 0x1675154 / 0x1675158 / 0x167515C are
    // indexed [3*i] -> stride 12 bytes.
    OnPacket<FactionBoardSync>(sys, 0x3a, [](const FactionBoardSync& p) {
        if (p.code == 0) {
            g_Client.Var(0x1673EB0) = g_Client.VarGet(0x1822848);  // 0x49059a
            g_Client.Var(0x1675624) = g_Client.VarGet(0x1822934);  // 0x4905a5
            for (int i = 0; i < 8; ++i) {                          // loop i<8 — 0x4905aa
                g_Client.Var(0x1673EB4 + 16 * i) = g_Client.VarGet(0x182284C + 16 * i); // g_QuiverMain[4*i]   0x4905d8
                g_Client.Var(0x1673EB8 + 16 * i) = g_Client.VarGet(0x1822850 + 16 * i); // g_QuiverCount[4*i]  0x4905f0
                g_Client.Var(0x1673EBC + 16 * i) = g_Client.VarGet(0x1822854 + 16 * i); // g_QuiverSocket[4*i] 0x490608
                g_Client.Var(0x1673EC0 + 16 * i) = g_Client.VarGet(0x1822858 + 16 * i); // g_QuiverSerial[4*i] 0x490620
                g_Client.Var(0x1675154 + 12 * i) = g_Client.VarGet(0x18228CC + 12 * i); // g_QuiverAux[3*i]    0x490638
                g_Client.Var(0x1675158 + 12 * i) = g_Client.VarGet(0x18228D0 + 12 * i); // dword_1675158[3*i]  0x490650
                g_Client.Var(0x167515C + 12 * i) = g_Client.VarGet(0x18228D4 + 12 * i); // dword_167515C[3*i]  0x490668
            }
            g_Client.Var(0x18398F4) = 3;    // 0x490673
            // TODO(ui) [anchor 0x490684]: UI_ItemListWin_Close(dword_1822820, 0) (0x5D1820) —
            //   the ItemListWin window is not modeled on the C++ side (see twin TODO(ui)
            //   GameHandlers_PartyGuild.cpp:182); Net never includes UI/.
            g_Client.msg.System(Str(335));  // 0x49069a / 0x4906a5
        } else if (p.code == 1) {
            // TODO(ui) [anchors 0x4906b1 / 0x4906bd]: cGameHud_Hide(dword_1839568) (0x62B050)
            //   = closing the HUD binder, then UI_ItemListWin_Close(dword_1822820, 0).
            //   Pure UI actions, unreachable from Net (no Net/ file includes UI/).
            g_Client.msg.System(Str(327));  // 0x4906d2 / 0x4906dd
        }
    });

    // 0x3b ConfirmPromptOpenDlg19 — opens confirmation box 19 if the filter is active;
    // otherwise auto-declines (Net_SendOp55(2), faithful to Net_OnConfirmPromptOpen_Dlg19).
    OnPacket<ConfirmPromptOpenDlg19>(sys, 0x3b, [&sys](const ConfirmPromptOpenDlg19& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (config::g_Options.FilterPrompt19) {
            g_Client.prompt.Open(19, "[" + nm + "]" + Str(499), Str(500));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp55(sys.Client(), 2);
            g_Client.msg.System(Str(498));
        }
    });

    // 0x3c ConfirmPromptClose_Dlg19 — closes dialog 19 if active + str501.
    OnTrigger(sys, 0x3c, [] {
        g_Client.prompt.CloseIf(19);
        g_Client.msg.System(Str(501));
    });

    // 0x41 ConfirmPromptOpen_Dlg20 — opens confirmation box 20 if the filter is active;
    // otherwise auto-declines (Net_SendOp61(2), faithful to Net_OnConfirmPromptOpen_Dlg20).
    OnPacket<ConfirmPromptOpen_Dlg20>(sys, 0x41, [&sys](const ConfirmPromptOpen_Dlg20& p) {
        std::string nm = Fixed(p.nameText, sizeof p.nameText);
        if (config::g_Options.FilterPrompt20) {
            g_Client.prompt.Open(20, "[" + nm + "]" + Str(508), Str(509));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp61(sys.Client(), 2);
            g_Client.msg.System(Str(507));
        }
    });

    // 0x42 ConfirmPromptCloseDlg20 — closes dialog 20 if active + str510.
    OnTrigger(sys, 0x42, [] {
        g_Client.prompt.CloseIf(20);
        g_Client.msg.System(Str(510));
    });

    // 0x43 TradeResultDialog — closes the trade notice (type 9) then message str511..518.
    OnPacket<TradeResultDialog>(sys, 0x43, [&sys](const TradeResultDialog& p) {
        CloseNoticeIf(9);
        if (p.resultCode <= 7)
            g_Client.msg.System(Str(511 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0)  // agreement confirmed -> Net_SendOp62 (opcode 0x3E, no payload).
            Net_SendOp62(sys.Client());
    });

    // 0x44 RequestTargetNameSet — records a request's target; arms the request state (=1).
    // Net_OnRequestTargetNameSet 0x490DA0. COPIES the received target name into the
    // corresponding buffer (g_PendingReqTargetName_Sub1 0x1674697 for subop 1,
    // g_PendingReqTargetName_Sub2 0x167468A for subop 2) — this is NOT a clear, despite
    // the Hex-Rays pseudocode appearance that HIDES the 2nd argument. Disassembly:
    //   0x490DEA lea eax,[ebp+var_14] / push eax          ; arg2 = src = payload name
    //   0x490DEE push offset g_PendingReqTargetName_Sub1  ; arg1 = dest
    //   0x490DF3 call Crt_StringInit                      ; (same 0x490E07-0x490E10 for Sub2)
    // Crt_StringInit 0x75CAB0 == strcpy(dest, src): `push edi / mov edi,[esp+4+arg_0]` (dest)
    // then `jmp loc_75CB25` which falls into Crt_Strcat's copy loop 0x75CB25
    // (`mov ecx,[esp+4+arg_4]` = src, stops at 1st NUL, returns dest). Hence the
    // strcpy semantics below and NOT a blind 13-byte memcpy.
    // Both these blobs DO have real consumers on the C++ side, all of which read up to the
    // first NUL (same conventions as the copy below):
    //   - Scene/SceneManager.cpp:785-792 (readReqName -> game::HasPendingTargetRequest), which
    //     feeds the Net_SendOp64 poll (Game/InGameTickFlow.cpp:28-30);
    //   - UI/GameHud.cpp:1035 (ReadTargetName) -> ResolveTargetPlate(0x167468A / 0x1674697),
    //     which DISPLAYS the target name plates (GameHud.cpp:1138-1139/1213-1214/1350-1351).
    // The previous clearing (.assign(13,0)) therefore permanently left both buffers
    // empty: silent target plates AND the request poll never firing.
    // The real clear, meanwhile, is handler 0x45 (Net_OnRequestCancelClear 0x490E30), below:
    // its src is `offset String` 0x7EC95F, whose byte 0 is 0x00 -> empty string.
    OnPacket<RequestTargetNameSet>(sys, 0x44, [](const RequestTargetNameSet& p) {
        // dest = exactly 13 bytes (0x1674697 - 0x167468A = 13).
        auto setName = [&p](uint32_t addr) {
            auto& b = g_Client.Blob(addr, 13);
            size_t n = 0;
            while (n < sizeof p.name && p.name[n] != 0) ++n;  // strcpy: stops at 1st NUL
            b.assign(13, 0);                                  // rest zero-filled -> NUL-terminated
            std::memcpy(b.data(), p.name, n);
        };
        if (p.subop == 1) {          // 0x490DDC (cmp 1) .. 0x490DFB
            setName(0x1674697);      // strcpy(g_PendingReqTargetName_Sub1, payloadName)
            g_Client.Var(0x1675B14) = 1;
        } else if (p.subop == 2) {   // 0x490DE2 (cmp 2) .. 0x490E18
            setName(0x167468A);      // strcpy(g_PendingReqTargetName_Sub2, payloadName)
            g_Client.Var(0x1675B14) = 1;
        }
    });

    // 0x45 RequestCancelClear — cancels the request: resets state to 0 + str534.
    OnTrigger(sys, 0x45, [] {
        g_Client.Var(0x1675B14) = 0;  // dword_1675B14
        g_Client.Blob(0x167468A, 13).assign(13, 0);
        g_Client.Blob(0x1674697, 13).assign(13, 0);
        g_Client.msg.System(Str(534));
    });

    // 0x46 RequestStateSet — sets the request UI state: state 0 -> 1, state 1 -> 2.
    OnPacket<RequestStateSet>(sys, 0x46, [](const RequestStateSet& p) {
        if (p.state == 0)      g_Client.Var(0x1675B14) = 1;
        else if (p.state == 1) g_Client.Var(0x1675B14) = 2;
    });

    // 0x47 ConfirmPromptOpen_Dlg10 — opens confirmation box 10 if the filter is active;
    // otherwise auto-declines (Net_SendOp67(2), faithful to Net_OnConfirmPromptOpen_Dlg10).
    OnPacket<ConfirmPromptOpen_Dlg10>(sys, 0x47, [&sys](const ConfirmPromptOpen_Dlg10& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (config::g_Options.FilterPrompt10) {
            g_Client.prompt.Open(10, "[" + nm + "]" + Str(337), Str(338));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp67(sys.Client(), 2);
            g_Client.msg.System(Str(336));
        }
        // KNOWN RESIDUAL (out of scope — unmodeled dependencies): after
        // UI_MsgBox_Open (0x490F8E), Net_OnConfirmPromptOpen_Dlg10 0x490EE0 tests
        // `if ((g_AutoHuntFuelA > 0 || g_AutoHuntFuelB > 0) && g_InvDirtyEnable == 1)`
        // (0x490FAC; globals 0x16755A4 / 0x16755A8 / 0x16755AC), sets dword_1822444 = 1
        // (0x490FFA) then calls UI_MsgBox_OnLButtonUp(dword_1822438, v8+170,
        // v5-Height/2+95) (0x49101F) = a SYNTHETIC left click on the box, i.e. an
        // AUTOMATIC ACCEPTANCE of the prompt when auto-hunt is running. Not reimplemented:
        // the coordinates depend on nWidth/nHeight (0x1669184 / 0x1669188) and
        // Sprite2D_GetWidth/GetHeight(&unk_8E8F5C), which are not modeled, and the click is a
        // UI action (Net never includes UI/). Divergence observable only with auto-hunt active:
        // the original auto-accepts, the rewrite leaves the box open.
    });

    // 0x48 ConfirmPromptClose_Dlg10 — closes dialog 10 if active + str339.
    OnTrigger(sys, 0x48, [] {
        g_Client.prompt.CloseIf(10);
        g_Client.msg.System(Str(339));
    });

    // 0x49 ResultDialog340 — closes the notice (type 7) then message str340..345.
    OnPacket<ResultDialog340>(sys, 0x49, [](const ResultDialog340& p) {
        CloseNoticeIf(7);
        if (p.status <= 5)
            g_Client.msg.System(Str(340 + static_cast<int>(p.status)));
    });

    // 0x50 ConfirmPromptOpenDlg14 — opens confirmation box 14 if the filter is active;
    // otherwise auto-declines (Net_SendOp74(2), faithful to Net_OnConfirmPromptOpen_Dlg14).
    OnPacket<ConfirmPromptOpenDlg14>(sys, 0x50, [&sys](const ConfirmPromptOpenDlg14& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (config::g_Options.FilterPrompt14) {
            g_Client.prompt.Open(14, "[" + nm + "]" + Str(408), Str(409));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp74(sys.Client(), 2);
            g_Client.msg.System(Str(407));
        }
    });

    // 0x51 ConfirmPromptClose_Dlg14 — closes dialog 14 if active + str410.
    OnTrigger(sys, 0x51, [] {
        g_Client.prompt.CloseIf(14);
        g_Client.msg.System(Str(410));
    });

    // 0x52 ResultDialog399 — closes the notice (type 8) then message str399..404.
    OnPacket<ResultDialog399>(sys, 0x52, [&sys](const ResultDialog399& p) {
        CloseNoticeIf(8);
        if (p.resultCode <= 5)
            g_Client.msg.System(Str(399 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0)  // GM request confirmed -> Net_SendGuarded_3
            Net_SendGuarded_3(sys.Client());  // (anti-spam morph/cooldown guard already built into the builder).
    });

    // 0x55 FactionChatMessage — faction chat: "<str416> [sender] message".
    OnPacket<FactionChatMessage>(sys, 0x55, [](const FactionChatMessage& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        // Gate g_ChatShow_Faction==1 (Net_OnFactionChatMessage 0x492fe0 @0x493058): not
        // implemented, see "CHANNEL DISPLAY GATES" note at the top of the file.
        g_Client.msg.Faction(Str(416) + " [" + who + "] " + msg, kChatColFaction, who.c_str());
    });

    // 0x57 SelfFactionChat — posts a faction chat line for a given name.
    // Net_OnSelfFactionChat 0x4930d0: `result = Crt_Strcmp(byte_1673184, v3); if (result) {…}`
    // (0x493105 / 0x49310f) -> the line is posted ONLY if the received name DIFFERS from the
    // local player's name (anti-echo gate). byte_1673184 IS modeled: Game/GameState.h:427
    // SelfState::localPlayerName, populated by UI/LoginScene.cpp:1116 — the old "not
    // accessible here" comment was stale (this same field is already read at handler 0x79
    // below and in GameHandlers_PartyGuild.cpp:329). `.empty()` policy documented at
    // GameState.h:425: empty local name -> no false "it's me", it posts.
    OnPacket<SelfFactionChat>(sys, 0x57, [](const SelfFactionChat& p) {
        std::string nm = Fixed(p.name, sizeof p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName)
            return;  // Crt_Strcmp == 0 (names equal) -> 0x49316d, nothing posted
        // "%s [%s]%s"(Str(416), name, Str(417)), speaker = v3 = name — 0x493146 / 0x493168.
        g_Client.msg.Faction(Str(416) + " [" + nm + "]" + Str(417), kChatColFaction, nm.c_str());
    });

    // 0x59 WhisperMessage — received whisper (subop1) or sent echo (subop2): "<prefix> [peer] msg".
    OnPacket<WhisperMessage>(sys, 0x59, [](const WhisperMessage& p) {
        std::string who = Fixed(p.sender, sizeof p.sender);
        std::string msg = Fixed(p.msg, sizeof p.msg);
        if (p.subop == 1) {  // received: floating banner + chat line (channel 12, prefix str840).
            g_Client.msg.Floating(0, 0, "[" + who + "] " + msg);
            g_Client.msg.Whisper(Str(840) + " [" + who + "] " + msg, who.c_str());
        } else if (p.subop == 2) {  // sent echo: chat line (channel 27, prefix str841).
            g_Client.msg.Whisper(Str(841) + " [" + who + "] " + msg, who.c_str());
        }
    });

    // 0x5a TradeChatMessage — trade chat: "<str113> [sender] message".
    OnPacket<TradeChatMessage>(sys, 0x5a, [](const TradeChatMessage& p) {
        std::string who = Fixed(p.senderName, sizeof p.senderName);
        std::string msg = Fixed(p.message, sizeof p.message);
        // Gate g_ChatShow_Trade==1 (Net_OnTradeChatMessage 0x4943f0 @0x494465): not
        // implemented, see "CHANNEL DISPLAY GATES" note at the top of the file.
        g_Client.msg.Chat(Str(113) + " [" + who + "] " + msg, kChatColTrade, who.c_str());
    });

    // 0x79 SocialListRemove — removes a name from the social lists (sub-ops 297/298/299).
    OnPacket<SocialListRemove>(sys, 0x79, [](const SocialListRemove& p) {
        // TODO(state): clear the slot (name+category) in the unmodeled social name arrays:
        //   listOp 297 -> unk_16869C0 (3x5), 298 -> unk_1686AC4 (4x5),
        //   299 -> unk_1686BC8 (4x5), stride 13 bytes. DO NOT FORCE: Game/SocialSystem.h §"WHAT
        //   IS NOT HERE" explicitly documents that these 3 grids are a subsystem DISTINCT
        //   from the friend/enemy AutoPlay (likely a faction/element roster per macro
        //   slot), unproven/unmodeled — a deliberate scoping decision, not an oversight.
        //
        // Sub-opcode gate: the `switch (v3)` 0x4a94c2 in Net_OnSocialListRemove 0x4a9450
        // ONLY contains cases 297/298/299; its `default:` does `return result` at
        // 0x4a94d8, so it NEVER reaches Crt_Strcmp(byte_1673184, v4) (0x4a967d) or
        // Str(1906) (0x4a969a), both placed AFTER the switch. Without this filter, Str(1906)
        // was emitted for any listOp.
        if (p.listOp != 297 && p.listOp != 298 && p.listOp != 299)
            return;  // default: -> 0x4a94d8
        std::string nm = Fixed(p.name, sizeof p.name);
        // `result = Crt_Strcmp(byte_1673184, v4); if (!result)` (0x4a9687) -> posted when the
        // names are EQUAL (it's really ME being removed from the list).
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName)
            g_Client.msg.System(Str(1906));
    });

    // 0x7e FriendStatusNotice — friend online (subop1) / offline (subop2).
    // Net_OnFriendStatusNotice 0x4aa050. Format "%s[%s]%s": the cdecl stacking (right->left)
    // re-read in disassembly 0x4aa113-0x4aa138 gives the varargs (class, name, Str243) — i.e.
    // "class[name]Str243", and NOT "Str243[name]class" as the earlier form built it.
    // Str(classId+75) is called WITHOUT a clamp here (0x4aa129/0x4aa1ba) — faithful: unlike
    // handler 0x90, which does go through Str_GetClassLabel (guard [0,3]).
    OnPacket<FriendStatusNotice>(sys, 0x7e, [](const FriendStatusNotice& p) {
        std::string nm  = Fixed(p.name, sizeof p.name);
        std::string cls = Str(static_cast<int>(p.classId) + 75);  // StrTable005(classId+75)
        if (p.subop == 1) {  // online — 0x4aa0e3
            g_Client.Var(0x1675DC8) = 1;      // dword_1675DC8  0x4aa0f7
            g_Client.VarF(0x1675DD0) = 0.0f;  // flt_1675DD0    0x4aa103
            std::string buf = cls + "[" + nm + "]" + Str(243);  // 0x4aa138
            // Binary order: Msg_AppendSystemLine (0x4aa14b) THEN HUD_ShowFloatingMessage
            // (0x4aa162) — the reverse of the earlier form.
            g_Client.msg.System(buf, 1u);
            g_Client.msg.Floating(0, 0, buf);  // (this, 0, 0, buf, &String) -> floatType=0, flag=0
            g_Client.msg.System("[5]" + Str(245), 1u);  // 0x4aa180 / 0x4aa193
        } else if (p.subop == 2) {  // offline — 0x4aa0ec
            std::string buf = cls + "[" + nm + "]" + Str(244);  // 0x4aa1c9
            g_Client.msg.System(buf, 1u);      // 0x4aa1dc
            g_Client.msg.Floating(0, 0, buf);  // 0x4aa1f3
        }
    });

    // 0x8b TradeChatMsg — posts "[name] message" on chat channel 24 (f0 ignored).
    OnPacket<TradeChatMsg>(sys, 0x8b, [](const TradeChatMsg& p) {
        std::string nm  = Fixed(p.name, sizeof p.name);
        std::string msg = Fixed(p.message, sizeof p.message);
        g_Client.msg.Chat("[" + nm + "] " + msg, 24u, nm.c_str());
    });

    // 0x90 FriendListEvent — friend add/remove notice (4 cases): floating + system line.
    OnPacket<FriendListEvent>(sys, 0x90, [](const FriendListEvent& p) {
        std::string nm  = Fixed(p.name, sizeof p.name);
        // Str_GetClassLabel 0x557A98 -- EXACT transcription (verified via direct decompilation
        // idaTs2, Net_OnFriendListEvent 0x4ab040): Str(75+id) for id in [0,3],
        // empty string otherwise (original &String fallback) -- same logic as
        // WorldEntityDispatch.cpp::ClassLabel(). Previously a plain Str(param), which
        // omitted the +75 offset and the out-of-range guard: fixed here.
        const int32_t classIdRaw = static_cast<int32_t>(p.param);
        std::string cls = (classIdRaw >= 0 && classIdRaw <= 3) ? Str(75 + classIdRaw) : std::string();
        std::string buf;
        switch (p.code) {
        case 0: buf = "[" + cls + "] [" + nm + "] " + Str(243); break;
        case 1: buf = Str(244); break;
        case 2: buf = std::to_string(p.param) + Str(245); break;
        case 3: buf = "[" + cls + "-" + nm + "] " + Str(246); break;
        default: return;
        }
        // HUD_ShowFloatingMessage(dword_1821D58, 1u, 0, v10, &String) — 0x4ab1a5: the pushed
        // slot-type is 1 (`push 0 / push 1` right before, args right->left), NOT 0 -> the
        // floating message was showing in the wrong visual slot. Mapping confirmed by the
        // faithful neighbors (0x9a/0x9d -> Floating(2,1); 0x99 -> Floating(0,0)). Order Floating
        // (0x4ab1a5) then System (0x4ab1cd): matches the binary here (unlike 0x7e, reversed).
        g_Client.msg.Floating(1, 0, buf);
        g_Client.msg.System(buf, 1u);  // Msg_AppendSystemLine(..., 1) — 0x4ab1cd
    });

    // 0x9f NpcDialogEvent — NPC dialog result: "<name(StrTable003)> <text>".
    OnPacket<NpcDialogEvent>(sys, 0x9f, [](const NpcDialogEvent& p) {
        int bodyId;
        switch (p.subOpcode) {
        case 0: bodyId = 2340; break;
        case 1: bodyId = 2342; break;
        case 2: bodyId = 2343; break;
        case 3: bodyId = 2344; break;
        case 4: bodyId = 2345; break;
        case 5: bodyId = 2346; break;
        default: return;
        }
        // StrTable003_Get(dword_84A6A8, p.nameStringId) in the original (Net_OnNpcDialogEvent
        // 0x4ad300, verified via direct decompilation idaTs2): 003.DAT table distinct from
        // StrTable005, not loaded/indexed on the ClientSource side (same real gap as
        // WorldEntityDispatch.cpp::SkillName -- a File/Asset scoping decision, not Net).
        // Str() (StrTable005, stable "#<id>" text) serves as a placeholder in the meantime.
        std::string nm  = Str(static_cast<int>(p.nameStringId));
        std::string buf = nm + " " + Str(bodyId);
        g_Client.msg.Floating(2, 1, buf);
        g_Client.msg.System(buf);
        if (p.subOpcode == 0 || p.subOpcode == 2)  // 2nd line
            g_Client.msg.System(Str(2341));
    });
}

} // namespace ts2::net

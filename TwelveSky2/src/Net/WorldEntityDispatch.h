// Net/WorldEntityDispatch.h — mega-dispatcher for network opcode 0x5e
// (Net_OnWorldEntityDispatch, EA 0x494870, ~62337 o of original code).
//
// ===========================================================================
//  DISPATCHER MAP (source: idaTs2, Hex-Rays decompilation of 0x494870)
// ===========================================================================
//
//  HEADER (0x49488F..0x4948AD) — the payload is copied into 2 local blocks, matching the
//  network size table (t[0x5e] = 105 = 1(opcode) + 4(subOpcode) + 100(raw block), cf.
//  Net/PacketDispatch.h):
//     payload[0..3]   -> v736: SUB-OPCODE (main switch, ~300 values).
//     payload[4..103] -> v702[0..99]: raw block, reinterpreted DIFFERENTLY per sub-case
//                        (most often: u32 index at +0, u32 secondary parameter at +4;
//                        sometimes a 13-byte tag).
//  switch default = return (no-op). This is the behavior for every unlisted sub-opcode below
//  — so ABSENT coverage == faithful no-op (identical to the "unregistered" state before this
//  audit).
//
//  GENERAL ROLE: ACTIVATION-STATE notifications for the LOCAL player's combo/posture/buff
//  skills, keyed by a weapon/category "family" (the "weaponType"/"branch" argument of
//  Skill_GetComboMotionId / Skill_GetSpecialMotionId / Skill_GetBuffMotionId, cf.
//  Game/SkillCombat.h, already wired) with a skill index within the family. The name
//  "WorldEntityDispatch" (chosen by the project's original IDA renaming pass) stays broad:
//  beyond the combo families (covered here), other uncovered sub-opcode ranges also concern
//  zone/world notifications (cf. the "UNCOVERED RANGES" table further below).
//
//  MECHANICAL STRUCTURE OF THE COMBO FAMILIES (confirmed for families 1, 2 AND 3, sub-opcodes
//  1..30 — families 1/2 = 2 x 9 identical slots; family 3 (19..30) reuses EXACTLY slots 1..6
//  (19..24) then 7..9 (28..30), but inserts 3 EXTRA sub-cases with no analogue (25..27, cf.
//  the "confirmed families" block further below) between the two — 12 sub-opcodes total for
//  this family, NOT 9 like 1/2):
//    slot 1 (n)   : NO state write. If Skill_IsAvailableByLevel(motion)
//                   -> message "<name> [<secondaryArg>]<suffix str231>".
//    slot 2 (n+1) : state[idx]=1. If available -> message "<name> <str232>" +
//                   (option FilterWorldEntity) popup "<name><str242>".
//    slot 3 (n+2) : state[idx]=2. If available -> message "<name> <str233>".
//    slot 4 (n+3) : state[idx]=3, (option) secondaryArg. If motion==current morph -> message
//                   "<name> <str234>" + arms the shared charge bar (dword_1675BA4/
//                   flt_1675BA8) and a flag0/flag1 pair SPECIFIC TO THE FAMILY (1675BAC/1675BB0
//                   family 1, 1675BCC/1675BD0 family 2); family 1 additionally writes
//                   dword_1675BB4 = secondaryArg/2 (family 2 does not -- confirmed asymmetry
//                   in the disasm).
//    slot 5 (n+4) : state[idx]=5, timestamp[idx]=Time_GetMonthDayInt(). No message (just
//                   "primes" the availability cache).
//    slot 6 (n+5) : state[idx]=5, timestamp[idx]=timestamp. If motion==current morph ->
//                   message + return to faction town (Map_BeginWarpToFactionTown). Suffix id
//                   DIFFERENT per family (str860 family 1, str235 family 2).
//    slot 7 (n+6) : state[idx]=4, timestamp[idx]=timestamp, secondaryArg = element id to
//                   pair. If available -> resolves Char_GetPairedElement; message with 1 or 2
//                   class labels depending on pairing (str236).
//    slot 8 (n+7) : state[idx]=5. If motion==current morph -> message "<name> <str237>" +
//                   return to faction town.
//    slot 9 (n+8) : state[idx]=0. If available -> nothing (silent reset).
//
//  Families CONFIRMED by full disassembly reading:
//    family 1 (sub-opcodes 1..9)   : Skill_GetComboMotionId(1, idx); state =
//        dword_1685EAC[idx], timestamp = dword_1685EE0[idx].
//    family 2 (sub-opcodes 10..18) : Skill_GetComboMotionId(2, idx); state =
//        dword_1685F14[idx], timestamp = dword_1685F2C[idx].
//    family 3 (sub-opcodes 19..30) : Skill_GetComboMotionId(3, idx); state =
//        dword_1685F44[idx], timestamp = dword_1685F6C[idx]. FULLY WIRED (full disassembly
//        reading, EA 0x4958a0..0x49636d):
//          - slots 1..6 (19..24): same shape as families 1/2 (strSlot6=235 like family 2;
//            slot4/22 with no half-duration, like family 2; own charge flags
//            dword_1675BD4/1675BD8).
//          - 25..27: 3 sub-cases with NO analogue in families 1/2, inserted between slot6
//            and slot7. Gate = motion==current morph (CALCULABLE, no SkillLevelTable) ->
//            fully wired, message included. No state/timestamp written. 25 reads a raw
//            13-byte text TAG (payload+12, a raw name with no table to resolve) + class 0..3
//            (payload+8, str75..78) and arms its own timer (dword_1675BDC/flt_1675BE0); 26 =
//            message only (str244); 27 = message with the raw u32 arg2 displayed as-is
//            (str245).
//          - 28..30: resumes the standard slots 7..9 (state=4/5/0). Slot7 (28) ALSO reads the
//            same 13-byte tag as 25 -- message/timer of its OWN (dword_1675BE4/flt_1675BE8,
//            str246) DIVERGING from the generic slot7, routed via ApplyFamily3Slot28 (NOT
//            ApplyComboFamilySlot). Gated by Skill_IsAvailableByLevel+Char_GetPairedElement
//            -> WIRED message included since 2026-07-14 (SkillLevelTable/ElementPairTable
//            exposed globally, cf. Game/SkillCombat.h::GetSkillLevelTable()/
//            Combat_ReadLocalElementPairs() and Docs/TS2_COMBAT_ELEMENT_GATING.md).
//
//  Skill_GetSpecialMotionId (sub-opcodes 402..410, state dword_16860C0[idx]) -- "Special"
//  family, 9 slots, SAME general mechanic as the combo families, FULLY WIRED (full
//  disassembly reading, EA 0x49ca89..0x49d1cf): gate = Skill_IsSpecialUsable, WIRED (same
//  SkillLevelTable exposed as Skill_IsAvailableByLevel, cf. WorldEntityDispatch_Special.cpp);
//  slot4 (405) does NOT arm the shared charge bar (dword_1675BA4/flt_1675BA8 never touched
//  here, unlike the combo families): own flag dword_1675CB0(int)/flt_1675CB4(FLOAT,
//  confirmed asymmetry) + half-duration dword_1675CB8=arg2/2 + 2nd chat line
//  "[halfDuration]str843" + 4-field reset (dword_1675CBC/CC0[0]/CC4/CC8/CCC). Sub-opcode 401
//  (right before, EA 0x497c66) is a STANDALONE HUD notice with no mechanical link (no
//  Skill_GetSpecialMotionId).
//
//  Skill_GetBuffMotionId (sub-opcodes 411..415, state dword_16860D0[idx]): SIMPLER mechanic
//  (5 sub-cases, not 9 -- 412/413 merged by the compiler into a single body; NO
//  Skill_IsAvailableByLevel/Skill_IsSpecialUsable gate, the messages are written
//  UNCONDITIONALLY except the last, gated by morph==415) -- FULLY WIRED (full disassembly
//  reading, EA 0x49d1d9..0x49d565). Bracketed tag = SAME raw tag13 field (payload+12) as
//  sub-opcode 25. Sub-opcodes 416/417 (EA 0x49d565/0x49d58d) adjacent but UNRELATED (distinct
//  table dword_1686120, no Skill_GetBuffMotionId, no message) -- wired trivially. VERIFIED
//  against Game/GameState.h::ActiveBuff / UI/BuffStatusPanel.h::BuffIconId: dword_16860D0[idx]
//  ("cast in progress" state, motion 241-330) is NOT the source of PlayerEntity::buffs
//  (a disjoint 0..33 catalog, fed by ~50 unrelated systems) -- NO mapping made here to
//  ActiveBuff, that would be an unconfirmed fabrication.
//
//  Combo family 4 (weapon 4, Skill_GetComboMotionId(4, idx), sub-opcodes 755..763, state
//  dword_16862F0[idx]) -- FULLY WIRED (EA 0x4a2159..0x4a293c). NOT an immediate continuation
//  of family 3 (no sub-opcode between 31 and 754 calls Skill_GetComboMotionId; verified by
//  exhaustive grep of the full disassembly). 2 divergences vs families 1/2/3: NO daily
//  timestamp table (dword_16862F0 = state only); slot4 (758) richer (own flag
//  dword_1675E90/E98 + shared bar + 2nd sound flt_1491A3C + half-duration dword_1675E9C +
//  2nd chat line + 5-field reset dword_1675E94/EA0[0]/EA4/EA8/EAC); slot7 (761) does NOT go
//  through Char_GetPairedElement: raw 80-byte block (payload+8, unconditionally copied to
//  dword_1676054, no modeled consumer -> not persisted) + a 4-digit integer decodable into up
//  to 4 class ids, message gated by Skill_IsAvailableByLevel -- gate AVAILABLE since
//  2026-07-14 (GetSkillLevelTable(), cf. slots 1/2/3 of the same families, now wired) BUT the
//  message is STILL OMITTED HERE: the 4-digit integer is read at a payload offset not
//  confirmed in this pass (beyond the 80-byte raw block, i.e. beyond the confirmed
//  idx/arg2/tag13 offsets), precise TODO (cf. WorldEntityDispatch_ComboFamilies.cpp).
//
//  No "family 5" of the Skill_GetComboMotionId(5, ...) kind exists (exhaustive grep: only
//  weaponType 1/2/3/4 are used across the whole dispatcher).
//
//  Sub-opcodes 31/32 ("31..170 survey" mission, 2026-07-14, EA 0x4963b0/0x496583) -- WIRED.
//  FIRST mechanism of the dispatcher's "remainder", confirmed on the full disassembly: NOT a
//  continuation of family 3 (no Skill_GetComboMotionId here). Toggle "exclusion" by (branch
//  0..3 = payload+4, type 0..3 = payload+8) into dword_1686014[4*branch+type] = 0 (31) / 1
//  (32) -- UNCONDITIONAL write (before any range check). Sub-opcodes 31/32 are STRUCTURAL
//  TWINS (compiler-duplicated code): branch label = str75..78 (like ApplyFamily3TagSlot),
//  type label = str251..254 (31) / str255..258 (32), format " %s" = off_7A6E5C (read directly
//  from memory). Common floating notice (floatType=3, flag=4). IMPORTANT: dword_1686014 has
//  THREE real consumers binary-side (xrefs_to confirmed) -- Combo_CheckTransition (0x4fd650,
//  EA 0x4ff2d6), Player_UpdateMovement (0x534500, EA 0x534856), UI_FactionInfoWnd_Render
//  (0x672010, EA 0x67246d) -- NONE ported in ClientSource to date, so the write is
//  faithfully persisted but WITHOUT observable effect for now (TODO(@dword_1686014_consumers)).
//
//  Sub-opcodes 101..115 ("duel/challenge 101-115" mission, 2026-07-14, RE/
//  dispatch_494870_full.c L.2579-2789) -- WIRED (101..109/112/114 duel/challenge; 111/115
//  "branch mastery"; 110 and 113 EXCLUDED, see below), TWO distinct subsystems:
//    - 101..109/112/114: DUEL/CHALLENGE by player NAME comparison -- the payload carries raw
//      13-byte names at offsets SPECIFIC to this sub-range (payload+4, +17, +30, +34, +43 --
//      verified esp-to-esp on the disassembly, DIFFERENT from this file's standard
//      idx/arg2/tag13 convention). Shared state dword_16746B8/dword_168726C (0=none/2=in
//      progress/raw code depending on the case), dword_1687450 (opponent code/result),
//      dword_168744C (102 only), dword_16747F0 (114), attack rating dword_168736C[0]/
//      dword_1687374[0] (same addresses as A_RatingBaseMin/Max, Net/GameVarDispatch.cpp). The
//      `byte_1673184==packet_name` gate (102/107/108/114) uses g_World.self.localPlayerName
//      (never populated by any handler to date, same honest degradation documented in
//      Game/GameState.h -- the gate fails cleanly until the login packet populates this
//      field).
//    - 111/115: "branch mastery" notices (StrTable005 1671..1675/2322), branch 0..3 -> label
//      1672..1675, NO state written.
//    - 110 EXCLUDED: STRUCTURALLY belongs to the "skill branch" cycle 76..100 WIRED above
//      (dword_1685F94[8*a+b], NOT duel/challenge nor branch mastery) -- REMAINS UNWIRED (out
//      of scope for both the 76..100 mission AND the 101-115 mission, cf.
//      Net/WorldEntityDispatch_BranchSkill.cpp::ApplyDuelBranchFamily for the `default`).
//    - 113 EXCLUDED: ENTIRELY gated by `!Crt_Strcmp(dword_16746A8, v686)` (local
//      guild/affiliation name, cf. Game/NameplateLogic.h -- "uncertain semantics, to be
//      confirmed by a future RE pass"), NO unconditional state write to preserve -- SAME
//      status as 425..428: nothing calculable without fabricating an unconfirmed field.
//
//  Sub-opcodes 201..208 ("201-208" mission, 2026-07-14, RE/dispatch_494870_full.c
//  L.2790-2899) -- FULLY WIRED (direct full disassembly reading, not just a survey). "Individual
//  arena" family: FIXED morph 194 (NOT Skill_GetComboMotionId/SpecialMotionId/BuffMotionId --
//  comboMotionId=194 is a hardcoded constant in every sub-case), SCALAR state dword_1686054
//  (NOT an array indexed by idx -- only one individual-arena instance possible at a time,
//  unlike families 1..4/Special/Buff). CLOSE shape (but NOT identical) to the 9-slot combo
//  families -- DIVERGENCES confirmed by direct disassembly reading (NOT the "same shape as
//  ApplySpecialFamilySlot" template assumed by the initial cursory audit):
//    - NO Skill_IsAvailableByLevel/Skill_IsSpecialUsable gate anywhere in this family
//      (201/202/203 write their message UNCONDITIONALLY, never consulting SkillLevelTable --
//      unlike ALL the combo/Special/4 families).
//    - 201 (slot1): idx=payload+4, message "<name194> [<idx>]<str231>", NO state written
//      (the only sub-case in the family without a state write -- consistent with the
//      combo/Special/4 families' slot1, which also writes no state).
//    - 202/203 (slots 2/3): state=1/2, unconditional message (str232/233).
//    - 204 (slot4): state=3, gate=morph==194 -> message str234 + OWN flag (dword_1675C84/
//      flt_1675C88, DISTINCT from the shared bar dword_1675BA4/flt_1675BA8 of the combo/4
//      families -- NEVER touched here, same divergence as the Special family) + FIXED
//      half-duration=600 (dword_1675C8C=600 hardcoded, NOT arg2/2 like combo family 1/
//      Special/4 -- idx isn't even read by this sub-case) + 2nd message "[600]str843" +
//      4-field reset (dword_1675C90[0]/94/98/9C, no prior "ResetA=1" unlike the Special
//      family).
//    - 205 (slot5): state=5 only, no message (identical to the combo/Special families).
//    - 206 (slot7): idx=payload+4 (element to pair), state=4 unconditional. IF idx==-1 ->
//      SHORT message "<name194> <str845>" (NO class label, a branch ABSENT from the combo
//      families' generic slot7 -- divergence specific to this family). ELSE -> Char_
//      GetPairedElement (Combat_ReadLocalElementPairs), message with 1 or 2 class labels
//      (str236, same shape as the generic slot7). Message ALWAYS sent (no SkillLevelTable
//      gate). Warp IF morph==194 AND local element != idx AND local element != pair AND
//      g_SelfCharInvBlock[0] -- via Map_BeginWarpToFactionTownDefault (NOT
//      BeginWarpToFactionTown: this family NEVER uses the other warps' "dead" guard in this
//      file). TODO(@idx=-1): in the idx==-1 branch, the original local variable
//      "PairedElement" is not recomputed -- it is SHARED by the compiler with ~5 other
//      sub-cases of the same giant switch (BYREF, cf. RE/dispatch_494870_full.c:735), its
//      exact value on this precise branch is not statically determinable from the
//      pseudocode alone -- approximated as paired=-1 (a fallback documented elsewhere in
//      this file, which never blocks the warp), plausible but NOT verified bit-for-bit.
//    - 207 (slot8): state=5, gate=morph==194 -> message str237; warp ONLY IF
//      g_SelfCharInvBlock[0] (unlike the combo/Special/4/Buff families' generic slot8, which
//      warp as soon as the morph gate passes, no extra condition).
//    - 208 (slot9): state=0 (reset), no message (identical to the generic slot9).
//  Sound (Snd3D_PlayScaledVolume) OUT OF SCOPE audio on 201..206, same convention as the rest
//  of this file. Precise EAs not captured (map_pseudocode_line_to_eas unstable on this
//  function, cf. the "rest of the function" mission below) -- reliable location = the
//  RE/dispatch_494870_full.c line number cited above.
//
//  Sub-opcodes 418..429 ("end of the Buff block" mission, 2026-07-14, dump L.3215-3364) --
//  NOT the table-driven Buff/combo/Special mechanic (no Skill_Get*MotionId here, confirmed by
//  full disassembly reading): 6 independent notifications + 1 shared tail-merge (423/429,
//  disasm LABEL_135) + 4 unwired sub-cases:
//    418  WE_PlaySound_SysLine_418 (EA 0x49d5b5, confirmed by direct disasm): count
//         (payload+4) -> "[count]str1402" if >0 else "str1403" alone. No state/gate.
//    419  class 0..3 (payload+4) + 13-byte TAG at payload+8 (NOT +12 like this file's
//         standard tag13 -- same offset as ApplyClassTagFamily 671..677) ->
//         "[class] [tag]str1444". No state/gate.
//    420  count (payload+4) -> "[count]str1475", floating HUD (cat.3/type1) + chat.
//    421  arms dword_1686134=1 -- CONFIRMED to be WorldMap::flagZ291Variant (World/
//         WorldMap.h, no global WorldMap instance exposed to network handlers here, same
//         limit as SkillLevelTable); message str1476 (HUD cat.3/type2 + chat) unconditional;
//         if morph==291 (CALCULABLE): arms dword_1675CD0/flt_1675CD4 -- CONFIRMED to be row
//         28 of Game/AnimationTick.cpp::kMorphRows (already consumed by the existing generic
//         timer engine) + 2nd message str1477.
//    422/424  suffix str1478 vs str1480 differ (NOT "strictly identical body", see below):
//         dword_1686134=0 written UNCONDITIONALLY (precedes the gate, same convention as
//         the rest of this file). Message "[name] suffix" + conditional warp: omitted,
//         dependent on byte_1686138 -- same limit as leaderName (629..652 below).
//    425..428  ENTIRELY gated by `!Crt_Strcmp(byte_1686138, dword_16746A8)`, with NO
//         unconditional state write ahead of this gate (unlike 422/424) -> NOTHING
//         calculable here -- NOT WIRED (same status as 600/764-770).
//         RE-VERIFIED ("425-428 + 500/901-903" mission, 2026-07-14) via idaTs2 xrefs_to
//         across the WHOLE binary (not just this function) for byte_1686138 (20 xrefs) AND
//         byte_1686145 (6 xrefs, adjacent twin 13-byte buffer, used by the same gates
//         elsewhere -- UI_ClanWarp_Commit/UI_ClanDisband_Commit): 25 of the 26 sites are
//         READS (confirmed instruction by instruction); the one remaining site is case 422
//         itself (EA 0x496E37, `Crt_StringInit(&byte_1686138, &byte_1686145)`), which WRITES
//         byte_1686138 -- but by copying from byte_1686145, which is ITSELF never fed real
//         content anywhere in the image (confirmed exhaustively, cf. the detailed comment at
//         the implementation site in WorldEntityDispatch_Special.cpp). So byte_1686138 boils
//         down to a deferred copy of a perpetually empty buffer: the gate remains
//         INSURMOUNTABLE statically -- not for lack of a write site, but because that unique
//         write site never propagates real content.
//    423/429  tail-merge LABEL_135 (STRICTLY identical body): count (payload+4), if
//         morph==291 -> "[count]str1479" (chat only).
//
//  Sub-opcodes 601..903 ("rest of the function" mission, 2026-07-14) -- WIRED (with precise
//  TODOs in places):
//    601..610  "level bonus/channel" family (dword_1686058[0..2]).
//    611..615  "element notification" family (gate g_World.self.element).
//    620..628  standalone notices (dword_1686188, etc).
//    629..652  "war declaration/stage" family (dword_168618C[elt 0..3], siege towns
//              kSiegeTownNpc={138,139,165,166}) -- TODO: guild/leader name (buffers not
//              received in this packet, 631/632/637-638/642-643/647-648), warp conditioned
//              on this name (637-638/642-643/647-648), attack-rating recompute
//              (635/640/645/650).
//    659..669  "arena" family (fixed morph 200, dword_16862A0).
//    671..677  "class+tag announce" family (pure notifications).
//    700..729  2nd "siege by element" family (dword_16862A4[elt 0..3], siege towns
//              kSiegeTownNpc2={5,10,15,123}).
//    740..750  3rd "siege/ranking" family (fixed morph 54, dword_16862B4) -- 750 absent
//              from the disasm (sparse).
//    752/753   title/rank table (dword_184C218/184C248[0..11]).
//    754       verb+class+tag announce (SkillName + Str(284)).
//    771..774  "cast announce" class+tag family (with SkillName).
//    780..786  "war 324" events (dword_1686304/1686308).
//    788..791, 795  simple notices/announcements (SkillName + suffix(es)).
//    800..807  "war 342" events (dword_1686310) -- 803 confirmed NO-OP (dead store on the
//              original binary side, verified).
//    901..903  final notices (banner + system line, same shape as sub-opcode 401).
//  TODO (unwired, cf. the top of the relevant section in the split .cpp files): 600
//  (confirmed observable no-op), 764..770 (sparse, absent).
//
// ===========================================================================
//  UNCOVERED RANGES (TODO -- faithful no-op, cf. audit)
// ===========================================================================
//  33..115 ("31..170 survey" mission, 2026-07-14 -- DENSE, full disassembly reading EA
//  0x4963ec..0x4979ec, ~83 sub-cases ALL PRESENT and ALL DISTINCT, unlike families 1..4/
//  Special/Buff: NOT a single repeatable mechanical pattern, several well-separated
//  subsystems identified) -- 33..115 FULLY WIRED (except 110/113, exclusions documented
//  above):
//    33..45     WIRED ("element loadout" mission, 2026-07-14, RE/dispatch_494870_full.c
//               L.1328-1675, cf. ApplyElementLoadoutFamily in WorldEntityDispatch_Element.cpp)
//               -- notices/state of the local player's element "loadout" (dword_1685E10/18/
//               1C/20/24/28, dword_16860B0..BC) -- StrTable005 ids 271..288; ALL write both
//               the floating HUD and a system line (unlike 46..50 which write ONLY the HUD);
//               warp conditioned on g_SelfMorphNpcId (35=38, 38=38/39/74/144-145/310, 40=
//               table {50,52,85,99,100,170,196}). No relation to the combo/Special/Buff
//               families nor to Char_GetPairedElement.
//    46..47     WIRED ("wire ElementPairTable" mission, 2026-07-14, cf. ApplyAlliancePairFamily
//               in WorldEntityDispatch_Element.cpp) -- ALLIANCE pairing by element
//               (g_AlliancePairTable, g_PerElementCounter, dword_1685E48/68); StrTable005
//               ids 377/378/379. Confirmed by idaTs2 address resolution (list_globals) that
//               g_AlliancePairTable IS EXACTLY g_LocalPlayerSheet+0x71C (0x1685E64) and
//               dword_1685E68 IS EXACTLY g_LocalPlayerSheet+0x720 (0x1685E68) -- i.e. the
//               SAME `a`/`b` fields as Char_GetPairedElement (slot7 of the combo families).
//               This sub-opcode is the ONLY client-side WRITE point of
//               Game/SkillCombat.h::ElementPairTable -- cf. Docs/TS2_COMBAT_ELEMENT_GATING.md.
//    48..50     WIRED ("element loadout" mission, 2026-07-14, RE/dispatch_494870_full.c
//               L.1819-1969, cf. ApplyAllianceLabelFamily in WorldEntityDispatch_Element.cpp)
//               -- tail-merge of the 46/47 alliance pairing: 48/49 SHARE the label shape
//               "<selector str380..383> [class] [tag] <suffix>" with 41/42 but remain
//               distinct (HUD floatType=10, NO system line -- verified: no
//               Msg_AppendSystemLine for 46..50, a DIVERGENCE vs 33..45). 49 REPLAYS 47's
//               full state (same g_AlliancePairTable/g_PerElementCounter addresses) before
//               adding its own message (suffix str285). 50 is a pure notification ("<selector>
//               str286").
//    51..62     WIRED ("sub-range 51-75" mission, 2026-07-14, RE/dispatch_494870_full.c
//               L.1970-2229, cf. WorldEntityDispatch_Element.cpp) -- "ranking/scoreboard"
//               system by class (0..3) and slot (0..9): dword_1685E74/78/7C/80 (alliance
//               tiers, 52..56 -- 52 ALONE has a REAL, non-dead full scoreboard reset of the
//               3 tables dword_168653C/16865DC/168667C, the SAME as 57..59), + 4 independent
//               notices (51/60/61/62). DISTINCT from the alliance pairing 46/47 AND from the
//               skill-branch family 63+ (dword_1685F94).
//    63..75     WIRED ("sub-range 51-75" mission, 2026-07-14, RE/dispatch_494870_full.c
//               L.2219-2305) -- START of the same "skill branch" system as 76..100 below
//               (dword_1685F94/Skill_GetMotionId2): 63..65 = "poll/arm1/arm2" preamble (the
//               ONLY place where Skill_IsAvailableByBranch actually influences a branch in
//               the whole family); 66..75 = 1st/2nd iteration of the
//               "flagsA/flagsB/soundOnly/probe/L418/L420/L422" cycle documented right below
//               -- reuse ApplyBranchFlagsSlot/ApplyBranchSoundOnlySlot/ApplyBranchProbe/
//               ApplyBranchLabel418/420/422 verbatim (cf. the "Sub-opcodes 63..68" block in
//               WorldEntityDispatch_BranchSkill.cpp).
//    76..100    "skill branch" system (Skill_GetMotionId2(branch, type),
//               dword_1685F94[8*branch+type]) -- DISTINCT from Skill_GetComboMotionId
//               (families 1..4). FULLY WIRED ("sub-range 76..100" mission, 2026-07-14, EA
//               0x49a514..0x49b4cb, cf. RE/dispatch_tables.json switch jump_ea=0x494a17 and
//               direct disasm on cases 76/78/80): repeating 7-sub-case cycle
//               (probe/L418/L420/L422/flagsA/flagsB/soundOnly, cf. ApplyBranchProbe and
//               following in WorldEntityDispatch_BranchSkill.cpp) -- Skill_IsAvailableByBranch
//               (Game/SkillSystem.h, already exposed) is NOT called: its only caller in this
//               slice ("probe") systematically discards the result (verified by disasm, both
//               branches converge to the same `return`). 110 (out of this range) stays
//               UNWIRED, cf. the "Sub-opcodes 101..115" block above (belongs to THIS 76..100
//               cycle, NOT duel/challenge -- excluded from both missions out of caution).
//    101..115   WIRED ("duel/challenge 101-115" mission, 2026-07-14 -- except 110/113, cf. the
//               "Sub-opcodes 101..115" block above for the full detail).
//  Verdict: DENSE ZONE, NOT wireable in bulk (5+ distinct subsystems with their own
//  semantics/state) -- 33..115 FULLY WIRED (except 110/113, exclusions documented above, to
//  be coupled with a future revalidation of 76..100 / the dword_16746A8 buffer).
//
//  116..200 ("31..170 survey" mission, 2026-07-14) -- CONFIRMED EMPTY: NO `case` in the full
//  disassembly (exhaustive grep on RE/dispatch_494870_full.c, 0 occurrences for 116..200
//  inclusive, so 170 and 196 too -- the IDA jump-table default annotation corroborates this,
//  explicitly listing "116-119,121-200" as default sub-ranges -- the apparent gap at 120 is a
//  jump-table artifact, NOT a real sub-case, verified by direct grep). NOT a TODO: confirmed
//  terminal behavior, faithful no-op to the `default:`.
//
//  201..208 -- FULLY WIRED ("201-208" mission, 2026-07-14), cf. the "Sub-opcodes 201..208"
//  block above ("individual arena" family, fixed morph 194).
//
//  425..428 ("end of the Buff block" mission, 2026-07-14 -- AUDITED: entirely gated by a name
//  buffer byte_1686138 not written in this function, cf. the "Sub-opcodes 418..429" block
//  above -- 418..424/429 WIRED in the same mission), 600, 764..770 (cf. the precise TODO list
//  above -- 601..903 WIRED in the "rest of the function" mission, 2026-07-14).
//  Complete range map and full Hex-Rays dump of the function: RE/dispatch_494870_full.c
//  (generated via ida_hexrays.decompile; line numbers cited in comments across the
//  WorldEntityDispatch_*.cpp split family refer to this file).
//
// SOURCE OF TRUTH: idaTs2 disassembly (EAs cited in comments). Like Net/ItemActionDispatch.h,
// this module does NOT have a fully-exploited Net/RecvPackets.h struct: RecvPackets.h::
// WorldEntityDispatchHeader exists for documentation/coverage-tracking purposes, but
// ApplyWorldEntityDispatch re-parses the raw payload itself (same reasons as
// ItemActionDispatch).
//
// IMPLEMENTATION: split across 6 files (WorldEntityDispatch.cpp hosts only the top-level
// switch below; WorldEntityDispatch_{ComboFamilies,BranchSkill,Element,Special,War}.cpp host
// the family/slot handlers it calls into; WorldEntityDispatch_Internal.h hosts the shared
// address constants/helpers and the cross-file forward declarations). See each file's own
// header comment for its exact sub-opcode ownership.
#pragma once
#include <cstdint>

namespace ts2::game {

// Applies a 0x5e packet. `payload` points at the received payload (104 o expected: subOpcode
// u32 + 100 o raw, mirroring v736/v702 from the decompilation); `len` its size. Sub-opcodes
// 1..30 (combo families 1, 2 and 3), 31/32 ("exclusion" branch/type toggle), 33..45 (element
// loadout), 46/47 (alliance element pairing), 48..50 (alliance-pairing tail-merge, cf. the
// "33..115" block above for these 3 sub-ranges), 51..75 (ranking scoreboard + start of the
// "skill branch" family, cf. the "33..115" block above), 76..100 ("skill branch" family
// Skill_GetMotionId2, cf. the "33..115" block above), 101..115 (duel/challenge by name
// comparison + "branch mastery", EXCEPT 110/113 -- cf. the "Sub-opcodes 101..115" block
// above), 201..208 ("individual arena" family, fixed morph 194, cf. the "Sub-opcodes
// 201..208" block above), 401 (standalone HUD notice), 402..410 ("Special" family), 411..417
// ("Buff" family + 2 trivial adjacent flags), 418..424+429 (independent notifications +
// tail-merge, cf. the "Sub-opcodes 418..429" block above; 425..428 NOT wired, entirely gated
// on an unavailable buffer), 601..807/901..903 (war/siege/ranking families, cf. the detailed
// list above) and 755..763 (combo family 4) are fully wired (with precise TODOs in places,
// cf. each split .cpp's header comment) -- the WHOLE 33..115 range is therefore WIRED (except
// 110/113, exclusions documented above). Everything else is a faithful no-op (the original
// switch's `default:` behavior): 425..428 (buffer unavailable), 600/764..770 (precise TODO,
// cf. "UNCOVERED RANGES" above), 116..200 is CONFIRMED EMPTY (not a TODO).
void ApplyWorldEntityDispatch(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

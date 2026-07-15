// Net/Opcodes.h — opcodes réseau TwelveSky2 (S->C et C->S).
//
// Vérité = désassemblage. Les opcodes/handlers ENTRANTS proviennent de
// Net_InitPacketHandlers 0x463270 (tables dword_846408 = handlers, dword_846808 = tailles).
// Les opcodes SORTANTS proviennent des 234 builders Net_Send* (voir
// Docs/TS2_PROTOCOL_SPEC.md, section « Paquets SORTANTS »).
//
// Framing (rappel) :
//   Entrant  (S->C) : [opcode:u8][payload] ; taille fixe par opcode, sauf 0x63 variable
//                     ([opcode:u8][len:u32][payload]). Aucun XOR côté réception.
//   Sortant  (C->S) : [nonce1:u32][nonce2_lo:3o][seq:u8@7][opcode:u8@8][payload] ^ cle XOR.
#pragma once
#include <cstdint>

namespace ts2::net {

// ---------------------------------------------------------------------------
// Opcodes ENTRANTS (serveur -> client). Nom = handler Pkt_*/Net_On* de l'IDB
// (prefixe Pkt_/Net_ retire ; sub_XXXX -> Sub_XXXX). 165 handlers, plage 0x0c..0xb6.
// Les valeurs absentes (0x00..0x0b, 0x9c, 0xa0..0xa2, 0xa7, 0xb5, 0xb7..0xff) n'ont
// ni handler ni taille (dword_846408[op]==0).
// ---------------------------------------------------------------------------
enum class Incoming : uint8_t {
    EnterWorld                 = 0x0c, // Pkt_EnterWorld 0x464160
    ZoneChangeInfo             = 0x0d, // Pkt_ZoneChangeInfo 0x464500
    SystemMessageBox           = 0x0e, // Pkt_SystemMessageBox 0x464650
    SpawnCharacter             = 0x0f, // Pkt_SpawnCharacter 0x4646c0
    CharStateUpdate            = 0x10, // Pkt_CharStateUpdate 0x464c10
    CharStatDelta              = 0x11, // Pkt_CharStatDelta 0x465d90
    SpawnMonster               = 0x12, // Pkt_SpawnMonster 0x467b00
    SpawnNpc                   = 0x13, // Pkt_SpawnNpc 0x467ec0
    ChatNotice                 = 0x14, // Pkt_ChatNotice 0x4682e0
    OnCombatResult             = 0x15, // Pkt_OnCombatResult 0x468340
    SetGameVar                 = 0x16, // Pkt_SetGameVar 0x468370 (dispatcher 158 cas)
    MapObjectUpdate            = 0x17, // Pkt_MapObjectUpdate 0x469c80
    GameServerConnectResult    = 0x18, // Pkt_GameServerConnectResult 0x469cf0
    GroundItemRemove           = 0x19, // Pkt_GroundItemRemove 0x46a200
    ItemActionDispatch         = 0x1a, // Pkt_ItemActionDispatch 0x46a320
    ItemUpgradeResult          = 0x1b, // Pkt_ItemUpgradeResult 0x488de0
    ItemRefineResult           = 0x1c, // Pkt_ItemRefineResult 0x48a530
    ItemCombineResult          = 0x1d, // Pkt_ItemCombineResult 0x48af50
    ItemSwapResultA            = 0x1e, // Pkt_ItemSwapResultA 0x48b520
    ItemSwapResultB            = 0x1f, // Pkt_ItemSwapResultB 0x48bc60
    ItemDiscardResult          = 0x20, // Pkt_ItemDiscardResult 0x48c3a0
    ItemResultSimple           = 0x21, // Pkt_ItemResultSimple 0x48c9a0
    WarehouseOpen              = 0x22, // Pkt_WarehouseOpen 0x48cb00
    WarehouseClose             = 0x23, // Pkt_WarehouseClose 0x48cd90
    WarehouseUpdate            = 0x24, // Pkt_WarehouseUpdate 0x48ce40
    VendorItemEntry            = 0x25, // Pkt_VendorItemEntry 0x48cf40
    TradeResult                = 0x26, // Pkt_TradeResult 0x48d150
    SmithUpgradeResult         = 0x27, // Pkt_SmithUpgradeResult 0x48e7d0
    ToggleObserver             = 0x28, // Pkt_ToggleObserver 0x48f080
    WhisperReceive             = 0x29, // Pkt_WhisperReceive 0x48f210
    PartyChatOrInvite          = 0x2a, // Pkt_PartyChatOrInvite 0x48f3c0
    ShoutMessage               = 0x2b, // Pkt_ShoutMessage 0x48f640
    DuelResult                 = 0x2c, // Pkt_DuelResult 0x48f760
    RepairResult               = 0x2d, // Pkt_RepairResult 0x48f7b0
    PartyInvitePrompt          = 0x2e, // Pkt_PartyInvitePrompt 0x48fa70
    PartyInviteDecline         = 0x2f, // Pkt_PartyInviteDecline 0x48fb80
    PartyJoinResult            = 0x30, // Pkt_PartyJoinResult 0x48fbd0
    TradeRequestPrompt         = 0x31, // Pkt_TradeRequestPrompt 0x48fd20
    TradeRequestResult         = 0x32, // Pkt_TradeRequestResult 0x48fe10
    TradeActionResult          = 0x33, // Pkt_TradeActionResult 0x48fea0
    AllyInvitePrompt           = 0x34, // Pkt_AllyInvitePrompt 0x48ffb0
    AllyInviteDecline          = 0x35, // Pkt_AllyInviteDecline 0x4900a0
    AllyJoinResult             = 0x36, // Pkt_AllyJoinResult 0x4900f0
    GuildMemberInfo            = 0x37, // Pkt_GuildMemberInfo 0x490290
    GuildInfoUpdate            = 0x38, // Pkt_GuildInfoUpdate 0x490360
    OnPvpTallyUpdate           = 0x39, // Net_OnPvpTallyUpdate 0x4904e0
    OnFactionBoardSync         = 0x3a, // Net_OnFactionBoardSync 0x490560
    OnConfirmPromptOpen_Dlg19  = 0x3b, // Net_OnConfirmPromptOpen_Dlg19 0x4906f0
    OnConfirmPromptClose_Dlg19 = 0x3c, // Net_OnConfirmPromptClose_Dlg19 0x4907b0
    OnPartyResultDialog        = 0x3d, // Net_OnPartyResultDialog 0x490800
    OnPartyMemberNameSet       = 0x3e, // Net_OnPartyMemberNameSet 0x4909a0
    OnPartyMemberValueSet      = 0x3f, // Net_OnPartyMemberValueSet 0x490a10
    OnPartyMemberClear         = 0x40, // Net_OnPartyMemberClear 0x490ab0
    OnConfirmPromptOpen_Dlg20  = 0x41, // Net_OnConfirmPromptOpen_Dlg20 0x490af0
    OnConfirmPromptClose_Dlg20 = 0x42, // Net_OnConfirmPromptClose_Dlg20 0x490bb0
    OnTradeResultDialog        = 0x43, // Net_OnTradeResultDialog 0x490c00
    OnRequestTargetNameSet     = 0x44, // Net_OnRequestTargetNameSet 0x490da0
    OnRequestCancelClear       = 0x45, // Net_OnRequestCancelClear 0x490e30
    OnRequestStateSet          = 0x46, // Net_OnRequestStateSet 0x490e90
    OnConfirmPromptOpen_Dlg10  = 0x47, // Net_OnConfirmPromptOpen_Dlg10 0x490ee0
    OnConfirmPromptClose_Dlg10 = 0x48, // Net_OnConfirmPromptClose_Dlg10 0x491040
    OnResultDialog340          = 0x49, // Net_OnResultDialog340 0x491090
    OnGuildRosterReset         = 0x4a, // Net_OnGuildRosterReset 0x4911d0
    OnGuildMemberJoin          = 0x4b, // Net_OnGuildMemberJoin 0x491330
    OnGuildChatMessage         = 0x4c, // Net_OnGuildChatMessage 0x491420
    OnGuildMemberLeave         = 0x4d, // Net_OnGuildMemberLeave 0x4914d0
    OnGuildMemberKick          = 0x4e, // Net_OnGuildMemberKick 0x4916d0
    OnGuildRosterUpdate        = 0x4f, // Net_OnGuildRosterUpdate 0x4918d0
    OnConfirmPromptOpen_Dlg14  = 0x50, // Net_OnConfirmPromptOpen_Dlg14 0x491c10
    OnConfirmPromptClose_Dlg14 = 0x51, // Net_OnConfirmPromptClose_Dlg14 0x491cd0
    OnResultDialog399          = 0x52, // Net_OnResultDialog399 0x491d20
    OnTeamFormationDispatch    = 0x53, // Net_OnTeamFormationDispatch 0x491e70
    OnGuildNoticeChat          = 0x54, // Net_OnGuildNoticeChat 0x492f40
    OnFactionChatMessage       = 0x55, // Net_OnFactionChatMessage 0x492fe0
    OnTeamSlotAssign           = 0x56, // Net_OnTeamSlotAssign 0x493090
    OnSelfFactionChat          = 0x57, // Net_OnSelfFactionChat 0x4930d0
    OnCultivationDispatch      = 0x58, // Net_OnCultivationDispatch 0x493180
    OnWhisperMessage           = 0x59, // Net_OnWhisperMessage 0x494290
    OnTradeChatMessage         = 0x5a, // Net_OnTradeChatMessage 0x4943f0
    OnQuickslotSync            = 0x5b, // Net_OnQuickslotSync 0x4944a0
    OnGuildActionResult        = 0x5c, // Net_OnGuildActionResult 0x4945c0
    OnPartyInviteResult        = 0x5d, // Net_OnPartyInviteResult 0x4946f0
    OnWorldEntityDispatch      = 0x5e, // Net_OnWorldEntityDispatch 0x494870
    OnBossHpInit               = 0x5f, // Net_OnBossHpInit 0x4a51d0
    OnZoneBuffStatus           = 0x60, // Net_OnZoneBuffStatus 0x4a52a0
    OnServerNameNotice         = 0x61, // Net_OnServerNameNotice 0x4a5540
    Sub_4A55E0                 = 0x62, // sub_4A55E0 0x4a55e0
    OnScriptTrigger            = 0x63, // Net_OnScriptTrigger 0x4a55f0 -- SEUL opcode VARIABLE
    OnBossHpDecrement          = 0x64, // Net_OnBossHpDecrement 0x4a5640
    OnBossSpawnNotice          = 0x65, // Net_OnBossSpawnNotice 0x4a5710
    OnPetSlotDispatch          = 0x66, // Net_OnPetSlotDispatch 0x4a5790
    OnBossHpInit2              = 0x67, // Net_OnBossHpInit2 0x4a5c20
    OnBossHpPercent            = 0x68, // Net_OnBossHpPercent 0x4a5cf0
    OnItemCellSet              = 0x69, // Net_OnItemCellSet 0x4a5d70
    OnItemSellResult           = 0x6a, // Net_OnItemSellResult 0x4a5ed0
    OnGambleResult             = 0x6b, // Net_OnGambleResult 0x4a6060
    OnWarehouseMoveResult      = 0x6c, // Net_OnWarehouseMoveResult 0x4a61f0
    OnVendorInventoryLoad      = 0x6d, // Net_OnVendorInventoryLoad 0x4a6500
    OnVendorClose              = 0x6e, // Net_OnVendorClose 0x4a6830
    OnSkillCooldownSet         = 0x6f, // Net_OnSkillCooldownSet 0x4a6880
    OnItemCombineResult        = 0x70, // Net_OnItemCombineResult 0x4a68f0
    Sub_4A7150                 = 0x71, // sub_4A7150 0x4a7150
    OnRevivePrompt             = 0x72, // Net_OnRevivePrompt 0x4a7170
    OnCountdownTimerStart      = 0x73, // Net_OnCountdownTimerStart 0x4a71b0
    OnCraftResultNotice        = 0x74, // Net_OnCraftResultNotice 0x4a7260
    OnItemEnchantDispatch      = 0x75, // Net_OnItemEnchantDispatch 0x4a7410
    OnMinigameStateLoad        = 0x76, // Net_OnMinigameStateLoad 0x4a73b0
    OnInventoryBulkLoad        = 0x77, // Net_OnInventoryBulkLoad 0x4a7f60
    OnEquipSlotUpdate          = 0x78, // Net_OnEquipSlotUpdate 0x4a92a0
    OnSocialListRemove         = 0x79, // Net_OnSocialListRemove 0x4a9450
    OnItemPlaceResult          = 0x7a, // Net_OnItemPlaceResult 0x4a8710
    OnPartyMemberTargetSet     = 0x7b, // Net_OnPartyMemberTargetSet 0x4a96c0
    OnItemRefineResult         = 0x7c, // Net_OnItemRefineResult 0x4a97a0
    OnSkillAuraSync            = 0x7d, // Net_OnSkillAuraSync 0x4a9d70
    OnFriendStatusNotice       = 0x7e, // Net_OnFriendStatusNotice 0x4aa050
    OnPartyMemberHpSet         = 0x7f, // Net_OnPartyMemberHpSet 0x4aa210
    OnPartyMemberUpdate        = 0x80, // Net_OnPartyMemberUpdate 0x4aa3e0
    OnPartyItemResult          = 0x81, // Net_OnPartyItemResult 0x4aa5b0
    Sub_4AAB60                 = 0x82, // sub_4AAB60 0x4aab60
    OnPlayerEquipVisual        = 0x83, // Net_OnPlayerEquipVisual 0x4aa770
    OnSummonSpawn              = 0x84, // Net_OnSummonSpawn 0x4aa810
    OnSystemNotice             = 0x85, // Net_OnSystemNotice 0x4aa8a0
    SpawnZoneObject            = 0x86, // Pkt_SpawnZoneObject 0x4680f0
    PlayerShopOpen             = 0x87, // Pkt_PlayerShopOpen 0x48d940
    PlayerShopBuyResult        = 0x88, // Pkt_PlayerShopBuyResult 0x48de90
    PlayerShopGoldResult       = 0x89, // Pkt_PlayerShopGoldResult 0x48e660
    OnItemCellClear            = 0x8a, // Net_OnItemCellClear 0x4aac80
    OnTradeChatMsg_Ch24        = 0x8b, // Net_OnTradeChatMsg_Ch24 0x4aadd0
    OnItemCountNotice          = 0x8c, // Net_OnItemCountNotice 0x4aab90
    OnBulkItemConsume          = 0x8d, // Net_OnBulkItemConsume 0x4ab1f0
    OnUpgradeCountNotice       = 0x8e, // Net_OnUpgradeCountNotice 0x4aae70
    Sub_4AB020                 = 0x8f, // sub_4AB020 0x4ab020
    OnFriendListEvent          = 0x90, // Net_OnFriendListEvent 0x4ab040
    OnPartyMemberPosition      = 0x91, // Net_OnPartyMemberPosition 0x4ab9f0
    OnItemMoveResult           = 0x92, // Net_OnItemMoveResult 0x4abb40
    OnBattlefieldStatus        = 0x93, // Net_OnBattlefieldStatus 0x4abd00
    OnDataTableLoad_1686F74    = 0x94, // Net_OnDataTableLoad_1686F74 0x4ac120
    OnItemBatchUpdate          = 0x95, // Net_OnItemBatchUpdate 0x4ac190
    OnDataTableLoad_1686CCC    = 0x96, // Net_OnDataTableLoad_1686CCC 0x4ac580
    OnMultiItemRemove          = 0x97, // Net_OnMultiItemRemove 0x4ac5f0
    OnAchievementDataLoad      = 0x98, // Net_OnAchievementDataLoad 0x4ac920
    OnAchievementNotice        = 0x99, // Net_OnAchievementNotice 0x4ac950
    OnMountTicketPrompt        = 0x9a, // Net_OnMountTicketPrompt 0x4aca50
    OnItemSocketResult         = 0x9b, // Net_OnItemSocketResult 0x4acb80
    OnBossHpBarUpdate          = 0x9d, // Net_OnBossHpBarUpdate 0x4ad1e0
    OnBossPanelLoad            = 0x9e, // Net_OnBossPanelLoad 0x4ad2a0
    OnNpcDialogEvent           = 0x9f, // Net_OnNpcDialogEvent 0x4ad300
    OnInstanceEnter            = 0xa3, // Net_OnInstanceEnter 0x4ad660
    OnItemBuyResult            = 0xa4, // Net_OnItemBuyResult 0x4ad8a0
    OnChargeStackUpdate        = 0xa5, // Net_OnChargeStackUpdate 0x4adc10
    OnHonorRankEvent           = 0xa6, // Net_OnHonorRankEvent 0x4add80
    OnItemUpgradeResult        = 0xa8, // Net_OnItemUpgradeResult 0x4ae2f0
    OnItemFuseResult           = 0xa9, // Net_OnItemFuseResult 0x4ae750
    OnBattlefieldStateChange   = 0xaa, // Net_OnBattlefieldStateChange 0x4abfb0
    OnItemSocketDispatch       = 0xab, // Net_OnItemSocketDispatch 0x4aefb0
    OnItemRefineDispatch       = 0xac, // Net_OnItemRefineDispatch 0x4b0440
    OnItemSlotRefresh          = 0xad, // Net_OnItemSlotRefresh 0x4b2390
    OnBuffEffectDispatch       = 0xae, // Net_OnBuffEffectDispatch 0x4a88d0
    OnItemEnhanceResult        = 0xaf, // Net_OnItemEnhanceResult 0x4b2790
    OnItemEnhanceResult2       = 0xb0, // Net_OnItemEnhanceResult2 0x4b2ca0
    Sub_4B33C0                 = 0xb1, // sub_4B33C0 0x4b33c0
    OnRankBoardLoad            = 0xb2, // Net_OnRankBoardLoad 0x4b33f0
    OnItemDropResult           = 0xb3, // Net_OnItemDropResult 0x4b3440
    OnStatSyncDispatch         = 0xb4, // Net_OnStatSyncDispatch 0x4b3590
    OnItemCellReset            = 0xb6, // Net_OnItemCellReset 0x4b4220
};

// ---------------------------------------------------------------------------
// Opcodes SORTANTS (client -> serveur). Nom = suffixe du builder Net_Send*_OpNN /
// Net_SendOpNN (NN = valeur DECIMALE de l'opcode : Op12 => 0x0c, Op143 => 0x8f).
// L'opcode est ecrit a this+8 par chaque builder. Trois opcodes multiplexent un
// sous-opcode dans leur payload : Op19 (0x13), Op75 (0x4b), Op79 (0x4f). Les
// opcodes « vault » 201..250 (0xc9..0xfa) sont des SOUS-opcodes emis DANS Op19.
// ---------------------------------------------------------------------------
enum class Outgoing : uint8_t {
    Op12  = 0x0c, // login / enter-world / char-create (composite 128+13+72 o)
    Op13  = 0x0d, // selecteur int court
    Op14  = 0x0e, // selecteur int court
    Op15  = 0x0f, // envoi du record partage de 72 o
    Op16  = 0x10, // envoi du record partage de 72 o
    Op17  = 0x11, // struct de 61 o
    Op18  = 0x12, // struct de 76 o
    Op19  = 0x13, // DISPATCHER action/vault : [sous-op:u8] + payload (sous-op 0..255, vault 201..250)
    Op20  = 0x14, // commande a 2 champs
    Op21  = 0x15, // payload VIDE (keepalive/heartbeat/ack)
    Op22  = 0x16, // commande a 2 champs
    Op23  = 0x17, // commande a 3 champs
    Op24  = 0x18, // commande a 4 champs
    Op25  = 0x19, // commande a 4 champs
    Op26  = 0x1a, // commande a 5 champs
    Op27  = 0x1b, // commande a 4 champs
    Op28  = 0x1c, // commande a 4 champs
    Op29  = 0x1d, // commande a 9 champs
    Op30  = 0x1e, // commande a 9 champs
    Op31  = 0x1f, // selecteur + blob de 1232 o
    Op32  = 0x20, // commande a 1 champ
    Op33  = 0x21, // nom/ID de 13 o (whisper/ami/cible)
    Op34  = 0x22, // commande a 2 octets
    Op35  = 0x23, // creation de perso : nom + 7 selections
    Op36  = 0x24, // commande a 5 champs
    Op37  = 0x25, // opcode seul (ping/etat)
    Op38  = 0x26, // blob 61 o (auth candidate)
    Op39  = 0x27, // nom + blob 61 o (login candidate)
    Op40  = 0x28, // blob 61 o
    Op41  = 0x29, // commande a 1 octet
    Op42  = 0x2a, // 3 octets + blob 24 o + octet
    Op43  = 0x2b, // nom + 1 octet
    Op44  = 0x2c, // opcode seul (ping)
    Op45  = 0x2d, // commande a 1 octet
    Op46  = 0x2e, // opcode seul
    Op47  = 0x2f, // nom 13 o
    Op48  = 0x30, // opcode seul
    Op49  = 0x31, // commande a 1 octet
    Op50  = 0x32, // opcode seul
    Op51  = 0x33, // opcode seul
    Op52  = 0x34, // opcode seul
    Op53  = 0x35, // commande + record 13 o
    Op54  = 0x36, // opcode seul
    Op55  = 0x37, // toggle 1 octet
    Op56  = 0x38, // toggle 1 octet
    Op57  = 0x39, // toggle 1 octet
    Op58  = 0x3a, // toggle 1 octet
    Op59  = 0x3b, // commande + record 13 o
    Op60  = 0x3c, // opcode seul
    Op61  = 0x3d, // toggle 1 octet
    Op62  = 0x3e, // opcode seul
    Op63  = 0x3f, // opcode seul
    Op64  = 0x40, // opcode seul
    Op65  = 0x41, // commande + record 13 o
    Op66  = 0x42, // opcode seul
    Op67  = 0x43, // toggle 1 octet
    Op68  = 0x44, // record 61 o
    Op69  = 0x45, // opcode seul
    Op70  = 0x46, // commande + record 13 o
    Op71  = 0x47, // opcode seul
    Op72  = 0x48, // commande + record 13 o
    Op73  = 0x49, // opcode seul
    Op74  = 0x4a, // toggle 1 octet
    Op75  = 0x4b, // DISPATCHER chat/guarded : [sous-op:u8] + payload (513 o max)
    Op76  = 0x4c, // record 61 o
    Op77  = 0x4d, // record 61 o
    Op78  = 0x4e, // record 13 o
    Op79  = 0x4f, // DISPATCHER menu/dialog : [sous-op:u8] + bloc 100 o
    Op80  = 0x50, // record 61 o
    Op81  = 0x51, // record 61 o
    Op82  = 0x52, // commande a 2 octets
    Op83  = 0x53, // commande a 2 octets
    Op84  = 0x54, // bloc 101 o + octet
    Op85  = 0x55, // TLV VARIABLE (seul builder a taille variable : *(a2+4)+13)
    Op86  = 0x56, // commande a 2 octets
    Op87  = 0x57, // commande a 2 octets
    Op88  = 0x58, // commande a 9 octets
    Op89  = 0x59, // commande a 2 octets
    Op90  = 0x5a, // commande a 2 octets
    Op91  = 0x5b, // opcode seul (heartbeat)
    Op92  = 0x5c, // commande a 1 octet
    Op94  = 0x5e, // bloc global 64 o
    Op95  = 0x5f, // maj position : float3 monde + flag octet
    Op96  = 0x60, // 3 octets
    Op97  = 0x61, // commande a 1 octet
    Op98  = 0x62, // action a 3 octets
    Op99  = 0x63, // creation de perso : slot/param + stage
    Op100 = 0x64, // 2 octets + struct 13 o
    Op101 = 0x65, // opcode seul (ping)
    Op102 = 0x66, // action a 4 octets
    Op103 = 0x67, // action a 3 octets
    Op104 = 0x68, // action a 2 octets
    Op105 = 0x69, // opcode seul (ping)
    Op106 = 0x6a, // 1 octet + struct 12 o
    Op107 = 0x6b, // 1 octet + struct 13 o
    Op108 = 0x6c, // 2 octets + nom cible
    Op109 = 0x6d, // creation de perso : nom + 7 selections + octet
    Op110 = 0x6e, // commande a 2 octets
    Op111 = 0x6f, // action a 1 octet
    Op112 = 0x70, // struct 61 o
    Op113 = 0x71, // action a 1 octet
    Op114 = 0x72, // action a 3 octets
    Op115 = 0x73, // action a 1 octet
    Op116 = 0x74, // opcode seul (ping)
    Op117 = 0x75, // opcode seul (ping)
    Op118 = 0x76, // opcode seul (ping)
    Op119 = 0x77, // opcode seul (ping)
    Op120 = 0x78, // action a 3 octets
    Op121 = 0x79, // action a 4 octets
    Op126 = 0x7e, // commande a 1 octet
    Op127 = 0x7f, // commande a 4 octets
    Op128 = 0x80, // opcode seul
    Op129 = 0x81, // commande a 2 octets
    Op131 = 0x83, // commande a 9 octets
    Op132 = 0x84, // commande a 4 octets
    Op133 = 0x85, // commande a 9 octets
    Op134 = 0x86, // commande a 4 octets
    Op135 = 0x87, // commande a 9 octets
    Op136 = 0x88, // opcode seul (ping)
    Op137 = 0x89, // commande a 4 octets
    Op138 = 0x8a, // commande a 13 octets
    Op139 = 0x8b, // commande a 1 octet
    Op140 = 0x8c, // opcode seul
    Op141 = 0x8d, // 3 octets + blob 24 o
    Op142 = 0x8e, // commande a 4 octets
    Op143 = 0x8f, // commande a 3 octets
};

// Nom du handler entrant (chaine statique de l'IDB) ou nullptr si opcode non gere.
// Utile pour journaliser les paquets. Defini dans PacketDispatch.cpp.
const char* IncomingName(uint8_t opcode);

} // namespace ts2::net


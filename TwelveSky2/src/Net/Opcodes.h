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
    EnterWorld                 = 0x0c, // Pkt_EnterWorld 0x464160 // ex-VeryOldClient: REGISTER_AVATAR_RECV (PLAUSIBLE)
    ZoneChangeInfo             = 0x0d, // Pkt_ZoneChangeInfo 0x464500 // ex-VeryOldClient: BROADCAST_WORLD_INFO (PLAUSIBLE)
    ServerBillboardImage       = 0x0e, // Pkt_SystemMessageBox 0x464650 // ex-nom: SystemMessageBox (mal nomme, cf. 0x464650 -> Billboard_ValidateImageViaTempFile -> TEMP.IMG -> Tex_LoadCompressedDDS = image serveur, PAS un message-box)
    SpawnCharacter             = 0x0f, // Pkt_SpawnCharacter 0x4646c0 // ex-VeryOldClient: AVATAR_ACTION_RECV (PLAUSIBLE)
    CharStateUpdate            = 0x10, // Pkt_CharStateUpdate 0x464c10 // ex-VeryOldClient: AVATAR_EFFECT_VALUE_INFO (PLAUSIBLE)
    CharStatDelta              = 0x11, // Pkt_CharStatDelta 0x465d90 // ex-VeryOldClient: AVATAR_CHANGE_INFO_1 (PLAUSIBLE)
    SpawnMonster               = 0x12, // Pkt_SpawnMonster 0x467b00 // ex-VeryOldClient: MONSTER_ACTION_RECV
    SpawnNpc                   = 0x13, // Pkt_SpawnNpc 0x467ec0
    ChatNotice                 = 0x14, // Pkt_ChatNotice 0x4682e0 // ex-VeryOldClient: GENERAL_NOTICE_RECV (PLAUSIBLE)
    OnCombatResult             = 0x15, // Pkt_OnCombatResult 0x468340 // ex-VeryOldClient: PROCESS_ATTACK_RECV
    SetGameVar                 = 0x16, // Pkt_SetGameVar 0x468370 (dispatcher 158 cas) // ex-VeryOldClient: PROCESS_DATA_RECV
    MapObjectUpdate            = 0x17, // Pkt_MapObjectUpdate 0x469c80
    GameServerConnectResult    = 0x18, // Pkt_GameServerConnectResult 0x469cf0 // ex-VeryOldClient: DEMAND_ZONE_SERVER_INFO_2_RESULT (PLAUSIBLE)
    GroundItemRemove           = 0x19, // Pkt_GroundItemRemove 0x46a200 // ex-VeryOldClient: USE_HOTKEY_ITEM_RECV (PLAUSIBLE) // NOTE(F1): role reel = decrement d'une pile de raccourci/quickslot (g_Container5 3x14, cf. 0x46a200), pas un item au sol ; 'GroundItemRemove' douteux mais PLAUSIBLE non prouve IDA -> PAS de rename
    ItemActionDispatch         = 0x1a, // Pkt_ItemActionDispatch 0x46a320 // ex-VeryOldClient: ITEM_ACTION_RECV
    ItemUpgradeResult          = 0x1b, // Pkt_ItemUpgradeResult 0x488de0 // ex-VeryOldClient: IMPROVE_ITEM_RECV (PLAUSIBLE)
    ItemRefineResult           = 0x1c, // Pkt_ItemRefineResult 0x48a530 // ex-VeryOldClient: REFINE_ITEM_RECV (PLAUSIBLE)
    ItemCombineResult          = 0x1d, // Pkt_ItemCombineResult 0x48af50 // ex-VeryOldClient: MAKE_ITEM_RECV (PLAUSIBLE)
    ItemSwapResultA            = 0x1e, // Pkt_ItemSwapResultA 0x48b520 // ex-VeryOldClient: EXCHANGE_ITEM_RECV (PLAUSIBLE)
    ItemSwapResultB            = 0x1f, // Pkt_ItemSwapResultB 0x48bc60 // ex-VeryOldClient: EXCHANGE_ITEM_RECV (PLAUSIBLE)
    ItemDiscardResult          = 0x20, // Pkt_ItemDiscardResult 0x48c3a0 // ex-VeryOldClient: DESTROY_ITEM_RECV (PLAUSIBLE)
    ItemResultSimple           = 0x21, // Pkt_ItemResultSimple 0x48c9a0 // ex-VeryOldClient: ADD_ITEM_RECV (PLAUSIBLE)
    WarehouseOpen              = 0x22, // Pkt_WarehouseOpen 0x48cb00
    WarehouseClose             = 0x23, // Pkt_WarehouseClose 0x48cd90
    WarehouseUpdate            = 0x24, // Pkt_WarehouseUpdate 0x48ce40
    VendorItemEntry            = 0x25, // Pkt_VendorItemEntry 0x48cf40 // ex-VeryOldClient: PSHOP_ITEM_INFO_RECV (PLAUSIBLE)
    TradeResult                = 0x26, // Pkt_TradeResult 0x48d150
    QuestInteractResult        = 0x27, // Pkt_SmithUpgradeResult 0x48e7d0 // ex-nom: SmithUpgradeResult (mal nomme, cf. 0x48e7d0 = resultat d'interaction de QUETE : g_pCurQuestStepRecord/g_CurQuestId, resultCode 1..9 ; struct RecvPackets.h deja QuestInteractResult)
    ToggleObserver             = 0x28, // Pkt_ToggleObserver 0x48f080 // ex-VeryOldClient: CHANGE_TO_TRIBE4_RECV (PLAUSIBLE)
    WhisperReceive             = 0x29, // Pkt_WhisperReceive 0x48f210 // ex-VeryOldClient: SECRET_CHAT_RECV (PLAUSIBLE)
    PartyChatOrInvite          = 0x2a, // Pkt_PartyChatOrInvite 0x48f3c0 // ex-VeryOldClient: PARTY_CHAT_RECV (PLAUSIBLE)
    ShoutMessage               = 0x2b, // Pkt_ShoutMessage 0x48f640 // ex-VeryOldClient: GENERAL_SHOUT_RECV (PLAUSIBLE)
    DuelResult                 = 0x2c, // Pkt_DuelResult 0x48f760 // ex-VeryOldClient: DUEL_END_RECV (PLAUSIBLE)
    RepairResult               = 0x2d, // Pkt_RepairResult 0x48f7b0
    PartyInvitePrompt          = 0x2e, // Pkt_PartyInvitePrompt 0x48fa70 // ex-VeryOldClient: PARTY_ASK_RECV (PLAUSIBLE)
    PartyInviteDecline         = 0x2f, // Pkt_PartyInviteDecline 0x48fb80 // ex-VeryOldClient: PARTY_CANCEL_RECV (PLAUSIBLE)
    PartyJoinResult            = 0x30, // Pkt_PartyJoinResult 0x48fbd0 // ex-VeryOldClient: PARTY_JOIN_INFO (PLAUSIBLE)
    TradeRequestPrompt         = 0x31, // Pkt_TradeRequestPrompt 0x48fd20 // ex-VeryOldClient: TRADE_ASK_RECV
    TradeRequestResult         = 0x32, // Pkt_TradeRequestResult 0x48fe10
    TradeActionResult          = 0x33, // Pkt_TradeActionResult 0x48fea0 // ex-VeryOldClient: TRADE_ANSWER_RECV (PLAUSIBLE)
    AllyInvitePrompt           = 0x34, // Pkt_AllyInvitePrompt 0x48ffb0 // ex-VeryOldClient: GUILD_ASK_RECV (PLAUSIBLE)
    AllyInviteDecline          = 0x35, // Pkt_AllyInviteDecline 0x4900a0 // ex-VeryOldClient: GUILD_CANCEL_RECV (PLAUSIBLE)
    AllyJoinResult             = 0x36, // Pkt_AllyJoinResult 0x4900f0 // ex-VeryOldClient: GUILD_ANSWER_RECV (PLAUSIBLE)
    GuildMemberInfo            = 0x37, // Pkt_GuildMemberInfo 0x490290 // ex-VeryOldClient: GUILD_LOGIN_INFO (PLAUSIBLE)
    GuildInfoUpdate            = 0x38, // Pkt_GuildInfoUpdate 0x490360 // ex-VeryOldClient: GUILD_LOGIN_INFO (PLAUSIBLE)
    OnPvpTallyUpdate           = 0x39, // Net_OnPvpTallyUpdate 0x4904e0
    OnFactionBoardSync         = 0x3a, // Net_OnFactionBoardSync 0x490560 // ex-VeryOldClient: TRIBE_ALLIANCE_INFO (PLAUSIBLE)
    OnConfirmPromptOpen_Dlg19  = 0x3b, // Net_OnConfirmPromptOpen_Dlg19 0x4906f0
    OnConfirmPromptClose_Dlg19 = 0x3c, // Net_OnConfirmPromptClose_Dlg19 0x4907b0
    OnPartyResultDialog        = 0x3d, // Net_OnPartyResultDialog 0x490800 // ex-VeryOldClient: PARTY_ANSWER_RECV (PLAUSIBLE)
    OnPartyMemberNameSet       = 0x3e, // Net_OnPartyMemberNameSet 0x4909a0 // ex-VeryOldClient: PARTY_MAKE_INFO (PLAUSIBLE)
    OnPartyMemberValueSet      = 0x3f, // Net_OnPartyMemberValueSet 0x490a10
    OnPartyMemberClear         = 0x40, // Net_OnPartyMemberClear 0x490ab0 // ex-VeryOldClient: PARTY_EXILE_RECV (PLAUSIBLE)
    OnConfirmPromptOpen_Dlg20  = 0x41, // Net_OnConfirmPromptOpen_Dlg20 0x490af0
    OnConfirmPromptClose_Dlg20 = 0x42, // Net_OnConfirmPromptClose_Dlg20 0x490bb0
    OnTradeResultDialog        = 0x43, // Net_OnTradeResultDialog 0x490c00 // ex-VeryOldClient: TRADE_ANSWER_RECV (PLAUSIBLE)
    OnRequestTargetNameSet     = 0x44, // Net_OnRequestTargetNameSet 0x490da0 // ex-VeryOldClient: TEACHER_ANSWER_RECV (PLAUSIBLE)
    OnRequestCancelClear       = 0x45, // Net_OnRequestCancelClear 0x490e30 // ex-VeryOldClient: TEACHER_CANCEL_RECV (PLAUSIBLE)
    OnRequestStateSet          = 0x46, // Net_OnRequestStateSet 0x490e90 // ex-VeryOldClient: TEACHER_STATE_RECV (PLAUSIBLE)
    OnConfirmPromptOpen_Dlg10  = 0x47, // Net_OnConfirmPromptOpen_Dlg10 0x490ee0
    OnConfirmPromptClose_Dlg10 = 0x48, // Net_OnConfirmPromptClose_Dlg10 0x491040
    OnResultDialog340          = 0x49, // Net_OnResultDialog340 0x491090
    OnGuildRosterReset         = 0x4a, // Net_OnGuildRosterReset 0x4911d0 // ex-VeryOldClient: GUILD_LOGIN_INFO (PLAUSIBLE)
    OnGuildMemberJoin          = 0x4b, // Net_OnGuildMemberJoin 0x491330
    OnGuildChatMessage         = 0x4c, // Net_OnGuildChatMessage 0x491420 // ex-VeryOldClient: GUILD_CHAT_RECV
    OnGuildMemberLeave         = 0x4d, // Net_OnGuildMemberLeave 0x4914d0
    OnGuildMemberKick          = 0x4e, // Net_OnGuildMemberKick 0x4916d0
    OnGuildRosterUpdate        = 0x4f, // Net_OnGuildRosterUpdate 0x4918d0
    OnConfirmPromptOpen_Dlg14  = 0x50, // Net_OnConfirmPromptOpen_Dlg14 0x491c10
    OnConfirmPromptClose_Dlg14 = 0x51, // Net_OnConfirmPromptClose_Dlg14 0x491cd0
    OnResultDialog399          = 0x52, // Net_OnResultDialog399 0x491d20
    OnGuildWorkDispatch        = 0x53, // Net_OnTeamFormationDispatch 0x491e70 // ex-nom: OnTeamFormationDispatch (mal nomme, cf. 0x491e70 = dispatcher 17 sous-ops de GUILDE create/upgrade/invite/kick/promote/dissolve sur g_Guild/unk_1839968) // ex-VeryOldClient: GUILD_WORK_RECV
    OnFactionNoticeChat        = 0x54, // Net_OnGuildNoticeChat 0x492f40 // ex-nom: OnGuildNoticeChat (mal nomme, cf. 0x492f40 = poste sur canal FACTION g_ChatColor_Faction/StrTable005 543, pas guilde) // ex-VeryOldClient: TRIBE_NOTICE_RECV
    OnFactionChatMessage       = 0x55, // Net_OnFactionChatMessage 0x492fe0 // ex-VeryOldClient: TRIBE_CHAT_RECV
    OnTeamSlotAssign           = 0x56, // Net_OnTeamSlotAssign 0x493090
    OnSelfFactionChat          = 0x57, // Net_OnSelfFactionChat 0x4930d0 // ex-VeryOldClient: TRIBE_CHAT_RECV (PLAUSIBLE)
    OnCultivationDispatch      = 0x58, // Net_OnCultivationDispatch 0x493180 // ex-VeryOldClient: CONTINUE_SKILL_STAT_RECV (PLAUSIBLE)
    OnWhisperMessage           = 0x59, // Net_OnWhisperMessage 0x494290 // ex-VeryOldClient: SECRET_CHAT_RECV
    OnTradeChatMessage         = 0x5a, // Net_OnTradeChatMessage 0x4943f0
    OnQuickslotSync            = 0x5b, // Net_OnQuickslotSync 0x4944a0 // ex-VeryOldClient: SET_HOTKEY_INVENTORY_RECV (PLAUSIBLE)
    OnGuildActionResult        = 0x5c, // Net_OnGuildActionResult 0x4945c0 // ex-VeryOldClient: GUILD_ANSWER_RECV (PLAUSIBLE)
    OnPartyInviteResult        = 0x5d, // Net_OnPartyInviteResult 0x4946f0 // ex-VeryOldClient: PARTY_ANSWER_RECV (PLAUSIBLE)
    OnWorldEntityDispatch      = 0x5e, // Net_OnWorldEntityDispatch 0x494870
    OnBossHpInit               = 0x5f, // Net_OnBossHpInit 0x4a51d0
    OnZoneBuffStatus           = 0x60, // Net_OnZoneBuffStatus 0x4a52a0
    OnServerNameNotice         = 0x61, // Net_OnServerNameNotice 0x4a5540
    Sub_4A55E0                 = 0x62, // sub_4A55E0 0x4a55e0
    OnGameGuardChallenge       = 0x63, // Net_OnScriptTrigger 0x4a55f0 -- SEUL opcode VARIABLE // ex-nom: OnScriptTrigger (mal nomme, cf. 0x4a55f0 = challenge anticheat GameGuard/nProtect -> Ac_GuardClient_MakeVerifyData 0x6de419, dans la bande AC [0x6D7234,0x6FD04C))
    OnBossHpDecrement          = 0x64, // Net_OnBossHpDecrement 0x4a5640
    OnBossSpawnNotice          = 0x65, // Net_OnBossSpawnNotice 0x4a5710
    OnPetSlotDispatch          = 0x66, // Net_OnPetSlotDispatch 0x4a5790 // ex-VeryOldClient: PAT_ACTION_RECV (PLAUSIBLE)
    OnBossHpInit2              = 0x67, // Net_OnBossHpInit2 0x4a5c20
    OnBossHpPercent            = 0x68, // Net_OnBossHpPercent 0x4a5cf0
    OnItemCellSet              = 0x69, // Net_OnItemCellSet 0x4a5d70 // ex-VeryOldClient: SET_INVENTORY_ITEM_RECV (PLAUSIBLE)
    OnItemSellResult           = 0x6a, // Net_OnItemSellResult 0x4a5ed0
    OnGambleResult             = 0x6b, // Net_OnGambleResult 0x4a6060
    OnWarehouseMoveResult      = 0x6c, // Net_OnWarehouseMoveResult 0x4a61f0
    OnVendorInventoryLoad      = 0x6d, // Net_OnVendorInventoryLoad 0x4a6500
    OnVendorClose              = 0x6e, // Net_OnVendorClose 0x4a6830
    OnSkillCooldownSet         = 0x6f, // Net_OnSkillCooldownSet 0x4a6880
    OnItemCombineResult        = 0x70, // Net_OnItemCombineResult 0x4a68f0
    Sub_4A7150                 = 0x71, // sub_4A7150 0x4a7150
    OnRevivePrompt             = 0x72, // Net_OnRevivePrompt 0x4a7170
    OnCountdownTimerStart      = 0x73, // Net_OnCountdownTimerStart 0x4a71b0 // ex-VeryOldClient: 194_TYPE_BATTLE_COUNTDOWN (PLAUSIBLE)
    OnCraftResultNotice        = 0x74, // Net_OnCraftResultNotice 0x4a7260 // ex-VeryOldClient: MAKE_ITEM_RECV (PLAUSIBLE)
    OnItemEnchantDispatch      = 0x75, // Net_OnItemEnchantDispatch 0x4a7410
    OnMinigameStateLoad        = 0x76, // Net_OnMinigameStateLoad 0x4a73b0
    OnInventoryBulkLoad        = 0x77, // Net_OnInventoryBulkLoad 0x4a7f60 // ex-VeryOldClient: MULTI_ITEM_CREATE_RECV (PLAUSIBLE)
    OnEquipSlotUpdate          = 0x78, // Net_OnEquipSlotUpdate 0x4a92a0
    OnSocialListRemove         = 0x79, // Net_OnSocialListRemove 0x4a9450 // ex-VeryOldClient: FRIEND_DELETE_RECV (PLAUSIBLE)
    OnItemPlaceResult          = 0x7a, // Net_OnItemPlaceResult 0x4a8710
    OnPartyMemberTargetSet     = 0x7b, // Net_OnPartyMemberTargetSet 0x4a96c0
    OnItemRefineResult         = 0x7c, // Net_OnItemRefineResult 0x4a97a0 // ex-VeryOldClient: REFINE_ITEM_RECV (PLAUSIBLE)
    OnSkillAuraSync            = 0x7d, // Net_OnSkillAuraSync 0x4a9d70
    OnFriendStatusNotice       = 0x7e, // Net_OnFriendStatusNotice 0x4aa050
    OnPartyMemberHpSet         = 0x7f, // Net_OnPartyMemberHpSet 0x4aa210
    OnPartyMemberUpdate        = 0x80, // Net_OnPartyMemberUpdate 0x4aa3e0
    OnPartyItemResult          = 0x81, // Net_OnPartyItemResult 0x4aa5b0
    Sub_4AAB60                 = 0x82, // sub_4AAB60 0x4aab60
    OnPlayerEquipVisual        = 0x83, // Net_OnPlayerEquipVisual 0x4aa770 // ex-VeryOldClient: COSTUME_STATE_RECV (PLAUSIBLE)
    OnSummonSpawn              = 0x84, // Net_OnSummonSpawn 0x4aa810 // ex-VeryOldClient: MAKE_PET_RECV (PLAUSIBLE)
    OnSystemNotice             = 0x85, // Net_OnSystemNotice 0x4aa8a0 // ex-VeryOldClient: GENERAL_NOTICE_RECV (PLAUSIBLE)
    SpawnZoneObject            = 0x86, // Pkt_SpawnZoneObject 0x4680f0
    PlayerShopOpen             = 0x87, // Pkt_PlayerShopOpen 0x48d940 // ex-VeryOldClient: START_PSHOP_RECV (PLAUSIBLE)
    PlayerShopBuyResult        = 0x88, // Pkt_PlayerShopBuyResult 0x48de90 // ex-VeryOldClient: BUY_PSHOP_RECV (PLAUSIBLE)
    PlayerShopGoldResult       = 0x89, // Pkt_PlayerShopGoldResult 0x48e660 // ex-VeryOldClient: SET_DEPUTY_PSHOP_MONEY_RECV (PLAUSIBLE)
    OnItemCellClear            = 0x8a, // Net_OnItemCellClear 0x4aac80
    OnTradeChatMsg_Ch24        = 0x8b, // Net_OnTradeChatMsg_Ch24 0x4aadd0
    OnItemCountNotice          = 0x8c, // Net_OnItemCountNotice 0x4aab90
    OnBulkItemConsume          = 0x8d, // Net_OnBulkItemConsume 0x4ab1f0
    OnUpgradeCountNotice       = 0x8e, // Net_OnUpgradeCountNotice 0x4aae70
    Sub_4AB020                 = 0x8f, // sub_4AB020 0x4ab020 // ex-VeryOldClient: RETURN_TO_AUTO_ZONE (PLAUSIBLE)
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
    OnItemSocketResult         = 0x9b, // Net_OnItemSocketResult 0x4acb80 // ex-VeryOldClient: ZC_SOCKET_SLOT_INSERT_RECV
    OnBossHpBarUpdate          = 0x9d, // Net_OnBossHpBarUpdate 0x4ad1e0
    OnBossPanelLoad            = 0x9e, // Net_OnBossPanelLoad 0x4ad2a0
    OnNpcDialogEvent           = 0x9f, // Net_OnNpcDialogEvent 0x4ad300
    OnInstanceEnter            = 0xa3, // Net_OnInstanceEnter 0x4ad660
    OnItemBuyResult            = 0xa4, // Net_OnItemBuyResult 0x4ad8a0
    OnChargeStackUpdate        = 0xa5, // Net_OnChargeStackUpdate 0x4adc10
    OnHonorRankEvent           = 0xa6, // Net_OnHonorRankEvent 0x4add80 // ex-VeryOldClient: ZC_HERORANK_INFO_RECV
    OnItemUpgradeResult        = 0xa8, // Net_OnItemUpgradeResult 0x4ae2f0
    OnItemFuseResult           = 0xa9, // Net_OnItemFuseResult 0x4ae750
    OnBattlefieldStateChange   = 0xaa, // Net_OnBattlefieldStateChange 0x4abfb0
    OnItemSocketDispatch       = 0xab, // Net_OnItemSocketDispatch 0x4aefb0 // ex-VeryOldClient: ZC_SOCKET_ITEM_RECV
    OnItemRefineDispatch       = 0xac, // Net_OnItemRefineDispatch 0x4b0440 // ex-VeryOldClient: ZC_REFINE_ITEM_RECV
    OnItemSlotRefresh          = 0xad, // Net_OnItemSlotRefresh 0x4b2390
    OnBuffEffectDispatch       = 0xae, // Net_OnBuffEffectDispatch 0x4a88d0
    OnItemEnhanceResult        = 0xaf, // Net_OnItemEnhanceResult 0x4b2790
    OnItemEnhanceResult2       = 0xb0, // Net_OnItemEnhanceResult2 0x4b2ca0
    Sub_4B33C0                 = 0xb1, // sub_4B33C0 0x4b33c0
    OnRankBoardLoad            = 0xb2, // Net_OnRankBoardLoad 0x4b33f0 // ex-VeryOldClient: ZC_HERORANK_INFO_RECV
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
    Net_SendEnterWorld = 0x0c, // Op12 = 0x0c ; Net_SendPacket_Op12 0x4B43C0 ; requete enter-world (composite 128+13+72 o : compte+nom+MoveStateBlock) ; appelant Scene_EnterWorldUpdate 0x52bff0 // ex-VeryOldClient: CZ_REGISTER_AVATAR_SEND (CONFIRMED)
    Net_SendHeartbeat = 0x0d, // Op13 = 0x0d ; Net_SendPacket_Op13 0x4B4570 ; keepalive periodique (~10s) portant g_LocalElement ; appelant Scene_InGameUpdate 0x52c600, echec=deconnexion (CONFIRMED)
    Op14  = 0x0e, // selecteur int court
    Net_SendAvatarActionMove = 0x0f, // Op15 = 0x0f ; Net_SendPacket_Op15 0x4B4870 ; bloc g_SelfMoveStateBlock 72 o (action/deplacement) ; appelants intents Net_QueueMoveTo/RunTo/Attack* 0x5117a0.. (CONFIRMED)
    Net_SendAvatarActionSync = 0x10, // Op16 = 0x10 ; Net_SendPacket_Op16 0x4B49F0 ; meme bloc action-state 72 o emis en fin d'anim (joueur local) ; appelants Char_AnimTick/Combat_TickAttackState 0x5746e0.. (CONFIRMED)
    Op17  = 0x11, // struct de 61 o
    Net_SendCombatAttack = 0x12, // Op18 = 0x12 ; Net_SendPacket_Op18 0x4B4CF0 ; struct attaque 76 o (type, targetId, pos, skillId) ; appelants Combat_QueueMeleeAttack/SkillAction/AoE 0x573130.. (CONFIRMED)
    Op19  = 0x13, // DISPATCHER action/vault : [sous-op:u8] + payload (sous-op 0..255, vault 201..250)
    Op20  = 0x14, // commande a 2 champs
    Op21  = 0x15, // payload VIDE (keepalive/heartbeat/ack)
    Net_SendUseQuickslotItem = 0x16, // Op22 = 0x16 ; Net_SendPacket_Op22 0x4B5300 ; (row 0..2, col 0..13) grille quickslot g_Container5 3x14 ; appelants Game_AutoUsePotion 0x5c4800/Game_OnHotkey (CONFIRMED)
    Op23  = 0x17, // commande a 3 champs
    Op24  = 0x18, // commande a 4 champs
    Op25  = 0x19, // commande a 4 champs
    Op26  = 0x1a, // commande a 5 champs
    Net_SendItemUpgradeReqA = 0x1b, // Op27 = 0x1b ; Net_SendPacket_Op27 0x4B5B90 ; amelioration item (slotA,contA,slotB,contB) ; seul appelant UI_ItemUpgradeA_Click 0x5ef410 (CONFIRMED)
    Net_SendItemUpgradeReqB = 0x1c, // Op28 = 0x1c ; Net_SendPacket_Op28 0x4B5D50 ; amelioration item (fenetre B, parallele A) ; seul appelant UI_ItemUpgradeB_Click 0x5f6090 (CONFIRMED)
    Net_SendCraftItemReqA = 0x1d, // Op29 = 0x1d ; Net_SendPacket_Op29 0x4B5F10 ; fabrication recette + 4 materiaux ; seul appelant UI_Craft_OnLUp 0x5e6900 (CONFIRMED)
    Net_SendCraftItemReqB = 0x1e, // Op30 = 0x1e ; Net_SendPacket_Op30 0x4B6130 ; fabrication recette + 4 materiaux (2e fenetre) ; seul appelant UI_Craft_Click 0x5edc40 (CONFIRMED)
    Op31  = 0x1f, // selecteur + blob de 1232 o
    Op32  = 0x20, // commande a 1 champ
    Op33  = 0x21, // nom/ID de 13 o (whisper/ami/cible)
    Op34  = 0x22, // commande a 2 octets
    Op35  = 0x23, // nom + 7 o ; appelants reels UI_MainInventory_OnLButtonUp 0x5b20b0 + UI_SkillBook_OnLUp 0x6050f0 (PAS char-create : 'creation de perso' etait ERRONE) ; role inventaire/skillbook non tranche (PLAUSIBLE)
    Net_SendAppraiseItem = 0x24, // Op36 = 0x24 ; Net_SendOp36 0x4B7110 ; identification item (mode 1) + reclamation recompense quete (modes 2/3/4) ; appelant UI_Appraise_OnLUp 0x5e0a00 (CONFIRMED)
    Op37  = 0x25, // opcode seul (ping/etat)
    Op38  = 0x26, // blob 61 o (auth candidate)
    Net_SendWhisper = 0x27, // Op39 = 0x27 ; Net_SendOp39 0x4B75D0 ; whisper (nom cible 13 o + message 61 o) ; UI_Chat_SubmitInput 0x68b330 canal 0, test anti-self (CONFIRMED)
    Op40  = 0x28, // blob 61 o
    Net_SendWarehouseOpen = 0x29, // Op41 = 0x29 ; Net_SendOp41 0x4B78E0 ; requete ouverture entrepot perso (arg 1) ; seul appelant UI_Warehouse_Open 0x5f3db0 (CONFIRMED)
    Op42  = 0x2a, // 3 octets + blob 24 o + octet
    Op43  = 0x2b, // nom + 1 octet
    Op44  = 0x2c, // opcode seul (ping)
    Net_SendPartyInviteResponse = 0x2d, // Op45 = 0x2d ; Net_SendOp45 0x4B7F30 ; reponse invitation groupe (1=accepter/2=refuser) ; Pkt_PartyInvitePrompt 0x48fa70 + UI_MsgBox type 8 (CONFIRMED)
    Op46  = 0x2e, // opcode seul
    Op47  = 0x2f, // nom 13 o
    Op48  = 0x30, // opcode seul
    Net_SendAllyInviteResponse = 0x31, // Op49 = 0x31 ; Net_SendOp49 0x4B8510 ; reponse invitation alliance (1=accepter/2=refuser) ; Pkt_AllyInvitePrompt 0x48ffb0 + UI_MsgBox type 9 (CONFIRMED)
    Op50  = 0x32, // opcode seul
    Op51  = 0x33, // opcode seul
    Op52  = 0x34, // opcode seul
    Op53  = 0x35, // commande + record 13 o
    Op54  = 0x36, // opcode seul
    Op55  = 0x37, // reponse prompt confirm Dlg19 (in 0x3b/0x3c) ; octet accept/refus (2=refus auto ; Net_OnConfirmPromptOpen_Dlg19) (PLAUSIBLE)
    Op56  = 0x38, // action membre-party par index (Net_OnPartyResultDialog case0, g_PartyRosterNames) (PLAUSIBLE)
    Op57  = 0x39, // requete valeur/detail membre-party par index de slot (UI_MemberSelectWnd_Open, Net_OnPartyMemberValueSet) (PLAUSIBLE)
    Op58  = 0x3a, // reponse (accept) boite confirm generique (UI_MsgBox_OnLButtonUp) (PLAUSIBLE)
    Op59  = 0x3b, // requete relation/clan par nom cible (UI_ClanWin, niveau>=113 ; ouvre NoticeDlg type9 -> confirm Op60) (PLAUSIBLE)
    Op60  = 0x3c, // confirm/commit de Op59 (UI_NoticeDlg_OnLButtonUp case9) (PLAUSIBLE)
    Op61  = 0x3d, // reponse prompt confirm Dlg20 (in 0x41/0x42) ; octet accept/refus (2=refus auto) (PLAUSIBLE)
    Op62  = 0x3e, // ack systeme d'echange (Net_OnTradeResultDialog case0) (PLAUSIBLE)
    Op63  = 0x3f, // reponse (accept) boite confirm generique (UI_MsgBox_OnLButtonUp) (PLAUSIBLE)
    Op64  = 0x40, // heartbeat/re-assert requete cible en attente (Scene_InGameUpdate si g_PendingReqTargetName set ; Player_ResetCombatState) (PLAUSIBLE)
    Op65  = 0x41, // requete relation/clan par nom cible (UI_ClanWin ; ouvre NoticeDlg type7 -> confirm Op66) (PLAUSIBLE)
    Op66  = 0x42, // confirm/commit de Op65 (UI_NoticeDlg_OnLButtonUp case7) (PLAUSIBLE)
    Op67  = 0x43, // reponse prompt confirm Dlg10 (in 0x47/0x48) ; octet accept/refus (2=refus auto) (PLAUSIBLE)
    Op68  = 0x44, // chat canal ALLIANCE/union (prefixe '~' / canal 4 ; g_AllianceRosterNames ; UI_Chat_SubmitInput) (PLAUSIBLE)
    Op69  = 0x45, // reponse (accept) boite confirm generique (UI_MsgBox_OnLButtonUp) (PLAUSIBLE)
    Op70  = 0x46, // reponse (avec nom cible) a boite confirm (UI_MsgBox_OnLButtonUp) (PLAUSIBLE)
    Op71  = 0x47, // signal validation alliance/union (AutoPlay_ValidateChatName, g_AllianceRosterNames) (PLAUSIBLE)
    Op72  = 0x48, // requete relation/clan par nom cible (UI_ClanWin, expulsion probable ; ouvre NoticeDlg type8 -> confirm Op73) (PLAUSIBLE)
    Op73  = 0x49, // confirm/commit de Op72 (UI_NoticeDlg_OnLButtonUp case8) (PLAUSIBLE)
    Op74  = 0x4a, // reponse prompt confirm Dlg14 (in 0x50/0x51) ; octet accept/refus (2=refus auto) (PLAUSIBLE)
    Op75  = 0x4b, // DISPATCHER chat/guarded : [sous-op:u8] + payload (513 o max)
    Net_SendGuildAddMember = 0x4c, // Op76 = 0x4c ; Net_SendOp76 0x4BACE0 ; record 61 o = ajout/invitation membre de guilde par nom saisi (Guild_AddMemberFromInput) [CONFIRMED]
    Op77  = 0x4d, // chat canal 3 (prefixe '!' ; dword_16746A8 ; UI_Chat_SubmitInput) (PLAUSIBLE)
    Op78  = 0x4e, // requete detail membre de guilde par nom (Guild_SelectNextMember) (PLAUSIBLE)
    Op79  = 0x4f, // DISPATCHER menu/dialog : [sous-op:u8] + bloc 100 o
    Net_SendChatMessage = 0x50, // Op80 = 0x50 ; Net_SendOp80 0x4BB2F0 ; record 61 o = chat general/local 'dire' (Chat_SubmitTypedMessage, sans prefixe) ; ex-VeryOldClient: GENERAL_CHAT_SEND [CONFIRMED]
    Op81  = 0x51, // chat canal 4 (prefixe '@', faction/element probable ; UI_Chat_SubmitInput) ; ex-VeryOldClient: GENERAL_CHAT_SEND (PLAUSIBLE)
    Op82  = 0x52, // DISPATCHER fenetre membre-faction (1=requete roster, 2=reward membre i ; UI_FactionMemberWnd) (PLAUSIBLE)
    Op83  = 0x53, // reponse 2 champs a boite confirm (UI_MsgBox_OnLButtonUp) (PLAUSIBLE)
    Op84  = 0x54, // bloc 101 o + octet ; AUCUNE xref (builder mort/inutilise dans ce build) (PLAUSIBLE)
    Net_SendGameGuardAuth = 0x55, // Op85 = 0x55 ; Net_SendOp85 0x4BBAA0 ; TLV variable (*(a2+4)+13) = jeton auth anticheat GameGuard (g_GuardAuthToken ; Scene_InGameUpdate apres Ac_GameGuard_Heartbeat) [CONFIRMED]
    Op86  = 0x56, // reglage plage min/max (UI_RangePicker, valeurs 0..5) (PLAUSIBLE)
    Op87  = 0x57, // DISPATCHER service boutique/talisman (sous-cmd 3/4/5 ; Net_ShopAction_3/4/5, UI_Shop, g_TalismanSlot) (PLAUSIBLE)
    Op88  = 0x58, // requete craft/combinaison 4 materiaux + recette (UI_Dlg41_OnLUp) (PLAUSIBLE)
    Op89  = 0x59, // action item 2 champs (slot,param ; UI_Dlg42_OnLUp, garde or) (PLAUSIBLE)
    Net_SendWarehouseMoveItem = 0x5a, // Op90 = 0x5a ; Net_SendOp90 0x4BC310 ; 2 o (sous-cmd 1=select/2=confirm, slot) = deplacement/select item en entrepot (UI_Warehouse ; in 0x6c OnWarehouseMoveResult) [CONFIRMED]
    Net_SendWarehouseClose = 0x5b, // Op91 = 0x5b ; Net_SendOp91 0x4BC4A0 ; opcode seul = fin/fermeture session entrepot (UI_Warehouse_Open reopen, Net_OnVendorClose ; g_OpenServiceWindow==21) ; 'heartbeat' etait ERRONE [CONFIRMED]
    Op92  = 0x5c, // requete liste/service par octet (=84 pour skill trainer/learn ; UI_SkillTrain/SkillLearn/cGameHud) ; CZ MAKE_SKILL_SEND NON concordant (PLAUSIBLE)
    Net_SendQuickslotBar = 0x5e, // Op94 = 0x5e ; Net_SendOp94 0x4BC950 ; bloc global 64 o (quickslot-bar) ; pas de CZ VeryOld
    Net_SendAvatarMove = 0x5f, // Op95 = 0x5f ; Net_SendOp95 0x4BCAD0 ; maj position : float3 monde + flag octet ; ex-VeryOldClient: UPDATE_AVATAR_ACTION
    Net_SendGambleWager = 0x60, // Op96 = 0x60 ; Net_SendOp96 0x4BCC60 ; 3 o (type,choix,mise) = mise/spin jeu de hasard (UI_Gamble_Draw) [CONFIRMED]
    Net_SendSelectClassBranch = 0x61, // Op97 = 0x61 ; Net_SendOp97 0x4BCE10 ; 1 o (id branche) = selection branche/avancement de classe (UI_ClassBranch_OnLUp) ; ex-VeryOldClient: CHANGE_TO_TRIBE4_SEND [CONFIRMED]
    Op98  = 0x62, // action item 3 o multi-usage (UI_GemSocket/Dlg14/Dlg22/Dlg23/cCraftWin75) ; ex-VeryOldClient: SOCKET_SYSTEM_SEND concorde avec 1 appelant seult (PLAUSIBLE)
    Net_SendAutoHuntConfig = 0x63, // Op99 = 0x63 ; Net_SendOp99 0x4BD140 ; 1 o + bloc config auto-hunt (g_AutoHuntMode, 68+44 o) = push/sync config autoplay (AutoPlay_*) ; 'creation de perso' etait ERRONE [CONFIRMED]
    Op100 = 0x64, // 2 octets + struct 13 o
    Op101 = 0x65, // opcode seul (ping)
    Op102 = 0x66, // action a 4 octets
    Op103 = 0x67, // action a 3 octets
    Net_SendCombatActionState = 0x68, // Op104 = 0x68 ; Net_SendOp104 0x4BDAE0 ; (actionKind:u8, substate:u8) = sync etat action combat garde/parade ; appelants Char_ActionTick_Guard* 0x57f260.. ; ex-Op104 ; ex-VeryOldClient: CZ_PROCESS_ATTACK_SEND [CONFIRMED]
    Op105 = 0x69, // opcode seul (ping)
    Net_SendProximityPointReport = 0x6a, // Op106 = 0x6a ; Net_SendOp106 0x4BDDE0 ; (index:u8, selfPos:float3[12o]) = rapport de proximite a un point marque par le serveur ; appelant Scene_InGameUpdate 0x52c600 (<100u d'un des 5 points flt_1676130) ; ex-Op106 [CONFIRMED]
    Net_SendWarpKeywordRequest = 0x6b, // Op107 = 0x6b ; Net_SendOp107 0x4BDF70 ; (element:u8, destName13) = warp par nom/mot-clef ; seul appelant Warp_ProcessKeyword 0x5f54e0 case2 ; ex-Op107 ; ex-VeryOldClient: CZ_FAIL_MOVE_ZONE_2_SEND [CONFIRMED]
    Net_SendWarehouseOpenRequest = 0x6c, // Op108 = 0x6c ; Net_SendPacket_Op108 0x4B6B90 ; (mode:u8, param:u8, accountName13) = requete ouverture entrepot/coffre (onglet 1/2/3) ; seul appelant UI_StorageWin_Open 0x5d27a0 cases 3/4/5 ; ex-Op108 [CONFIRMED]
    Op109 = 0x6d, // Net_SendPacket_Op109 0x4B6D40 ; name13 + 7 dwords + leadByte + blob28 ; appelants UI_MainInventory_OnLButtonUp 0x5b20b0 + UI_SkillBook_OnLUp 0x6050f0 = reclamation d'objet reserve/lie-au-nom (variante Op35) ('creation de perso' etait ERRONE) ; verbe metier exact non prouve (PLAUSIBLE)
    Op110 = 0x6e, // commande a 2 octets
    Op111 = 0x6f, // action a 1 octet
    Op112 = 0x70, // struct 61 o
    Op113 = 0x71, // action a 1 octet
    Op114 = 0x72, // action a 3 octets
    Op115 = 0x73, // action a 1 octet
    Op116 = 0x74, // opcode seul (ping)
    Op117 = 0x75, // opcode seul (ping)
    Net_SendRankBoardOpen = 0x76, // Op118 = 0x76 ; Net_SendOp118 0x4BEB90 ; payload vide = requete donnees classement a l'ouverture ; seul appelant UI_RankWnd_Open 0x6715e0 ; ex-Op118 ; ex-VeryOldClient: CZ_HERORANK_INFO_SEND [CONFIRMED]
    Net_SendRankBoardRefresh = 0x77, // Op119 = 0x77 ; Net_SendOp119 0x4BED00 ; payload vide = refresh/re-requete du classement (bouton refresh) ; seul appelant UI_RankWnd_OnClick 0x671780 ; ex-Op119 ; ex-VeryOldClient: CZ_HERORANK_INFO_SEND [CONFIRMED]
    Op120 = 0x78, // action a 3 octets
    Op121 = 0x79, // action a 4 octets
    Op126 = 0x7e, // commande a 1 octet
    Op127 = 0x7f, // commande a 4 octets
    Net_SendConsignmentSubmit = 0x80, // Op128 = 0x80 ; Net_SendOp128 0x4BF520 ; payload vide = finalisation mise en consignation (boutique perso 14 slots) ; seul appelant UI_Consign_Submit 0x5cfbe0 ; ex-Op128 [CONFIRMED]
    Op129 = 0x81, // commande a 2 octets
    Op131 = 0x83, // commande a 9 octets // ex-VeryOldClient: MAKE_ITEM_SEND (PLAUSIBLE)
    Net_SendItemFusionCommit = 0x84, // Op132 = 0x84 ; Net_SendOp132 0x4BFA40 ; (slotA:u8, subA:u8, slotB:u8, subB:u8) = commit fusion/combinaison de deux objets ; seul appelant cCraftWin67_OnCommit 0x618c20 ; ex-Op132 ; ex-VeryOldClient: CZ_MAKE_ITEM_SEND [CONFIRMED]
    Op133 = 0x85, // commande a 9 octets // ex-VeryOldClient: MAKE_ITEM_SEND (PLAUSIBLE)
    Op134 = 0x86, // commande a 4 octets // ex-VeryOldClient: MAKE_ITEM_SEND (PLAUSIBLE)
    Op135 = 0x87, // commande a 9 octets // ex-VeryOldClient: MAKE_ITEM_SEND (PLAUSIBLE)
    Op136 = 0x88, // opcode seul (ping)
    Net_SendItemEnhanceCommit = 0x89, // Op137 = 0x89 ; Net_SendOp137 0x4C01F0 ; (slotA:u8, subA:u8, slotB:u8, subB:u8) = commit amelioration/enchant (montee +N) ; seul appelant cCraftWin72_OnCommit 0x622690 ; ex-Op137 ; ex-VeryOldClient: CZ_MAKE_ITEM_SEND [CONFIRMED]
    Op138 = 0x8a, // commande a 13 octets // ex-VeryOldClient: MAKE_ITEM_SEND (PLAUSIBLE)
    Op139 = 0x8b, // commande a 1 octet
    Net_SendExchangeListRequest = 0x8c, // Op140 = 0x8c ; Net_SendOp140 0x4C07B0 ; payload vide = requete liste d'echange/recompense a l'ouverture ; seul appelant cExchangeWin74_Init 0x625860 ; ex-Op140 ; ex-VeryOldClient: CZ_TRADE_MENU_SEND [CONFIRMED]
    Op141 = 0x8d, // 3 octets + blob 24 o
    Op142 = 0x8e, // commande a 4 octets // ex-VeryOldClient: MAKE_ITEM_SEND (PLAUSIBLE)
    Op143 = 0x8f, // commande a 3 octets
};

// Nom du handler entrant (chaine statique de l'IDB) ou nullptr si opcode non gere.
// Utile pour journaliser les paquets. Defini dans PacketDispatch.cpp.
const char* IncomingName(uint8_t opcode);

} // namespace ts2::net


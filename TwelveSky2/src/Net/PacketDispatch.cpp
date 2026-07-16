// Net/PacketDispatch.cpp — implementation de la boucle de reception/dispatch.
// Fidele a Net_RecvDispatch 0x463040 et Net_InitPacketHandlers 0x463270.
#include "Net/PacketDispatch.h"

#include <cstring>
#include "Core/Log.h"

#pragma comment(lib, "ws2_32.lib")

namespace ts2::net {

PacketDispatcher::PacketDispatcher()
    : sizes_(MakeSizeTable()) {
    // handlers_ : par defaut vides (equivaut au for(i<256) dword_846408[i]=0).
}

void PacketDispatcher::SetHandler(std::uint8_t opcode, Handler h) {
    handlers_[opcode] = std::move(h);
}

void PacketDispatcher::ClearHandlers() {
    for (auto& h : handlers_) h = nullptr;
}

void PacketDispatcher::Consume(std::uint32_t n) {
    // Net_RecvDispatch : Crt_Memmove(this+32, this+32+n, filled - n) ; filled -= n.
    std::memmove(buffer_.data(), buffer_.data() + n, filled_ - n);
    filled_ -= n;
}

void PacketDispatcher::Drain() {
    // while ( *(this+200032) >= 1 ) ...
    while (filled_ >= 1) {
        const std::uint8_t op = buffer_[0];      // v7 = *(this+32)
        const std::uint32_t need = sizes_[op];   // dword_846808[v7]

        if (need == 0) {
            // Opcode non gere : le client d'origine sauterait sur un handler nul (crash).
            // Le serveur officiel n'emet jamais ces opcodes. On stoppe le drain sans
            // consommer (need==0 -> memmove de 0 octet == boucle infinie) : garde de surete.
            TS2_WARN("PacketDispatcher: opcode inconnu 0x%02X, drain arrete (%u o restants)",
                     op, filled_);
            break;
        }

        // if ( *(this+200032) < dword_846808[v7] ) break;  -- pas assez d'octets.
        if (filled_ < need) break;

        const std::uint8_t* payload;
        std::uint32_t payloadLen;
        std::uint32_t frameLen;

        if (op == kVariableOpcode) {
            // if ( v7 == 99 ) { Crt_Memcpy(&v6, this+33, 4); if (filled < v6+5) break; }
            std::uint32_t varLen = 0;
            std::memcpy(&varLen, &buffer_[1], 4);
            // Comparaison en 64 bits pour eviter le debordement de (varLen + 5) present
            // dans le binaire 32 bits ; sans effet pour les longueurs legitimes.
            if (static_cast<std::uint64_t>(filled_) <
                static_cast<std::uint64_t>(varLen) + kVariableHeaderSize) {
                break;
            }
            frameLen   = varLen + kVariableHeaderSize; // len + 5
            payload    = &buffer_[kVariableHeaderSize]; // this+37
            payloadLen = varLen;
        } else {
            frameLen   = need;
            payload    = &buffer_[1];  // this+33 (unk_8156C1)
            payloadLen = need - 1;
        }

        // ((void (*)(void))dword_846408[v7])();  -- dispatch.
        const Handler& h = handlers_[op];
        if (h) h(op, payload, payloadLen);

        // Consommation du frame (Crt_Memmove + maj du compteur).
        if (op == kVariableOpcode) {
            // Crt_Memmove(this+32, this+v5+37, filled - (v5+5)) ; filled -= v5+5.
            Consume(frameLen);
        } else {
            // if ( filled >= dword_846808[v7] ) { memmove ; filled -= size; }
            if (filled_ >= need) Consume(need);
        }
    }
}

RecvResult PacketDispatcher::OnSocketEvent(SOCKET s, std::uint16_t netEvent) {
    if (netEvent == FD_READ) {
        // v8 = recv(this+12, this + filled + 32, 200000 - filled, 0);
        const int n = ::recv(s,
                             reinterpret_cast<char*>(buffer_.data() + filled_),
                             static_cast<int>(kRecvBufferSize - filled_),
                             0);
        if (n > 0) {
            filled_ += static_cast<std::uint32_t>(n);
            Drain();
            return RecvResult::Ok;
        }
        if (n != SOCKET_ERROR) {
            // v8 == 0 : fermeture gracieuse (v8 != -1 -> Net_CloseSocket).
            return RecvResult::Closed;
        }
        // v8 == -1 : erreur. WSAEWOULDBLOCK (10035) est benin, tout le reste ferme.
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            return RecvResult::Closed;
        }
        return RecvResult::Ok;
    }
    if (netEvent == FD_CLOSE) {
        // else if ( a3 == 32 ) return Net_CloseSocket(this);
        return RecvResult::Closed;
    }
    return RecvResult::Ok;
}

bool PacketDispatcher::PushBytes(const std::uint8_t* data, std::uint32_t n) {
    if (static_cast<std::uint64_t>(filled_) + n > kRecvBufferSize) return false;
    std::memcpy(buffer_.data() + filled_, data, n);
    filled_ += n;
    Drain();
    return true;
}

// ---------------------------------------------------------------------------
// Nom du handler entrant (pour journalisation). Reprend les symboles de l'IDB.
// ---------------------------------------------------------------------------
const char* IncomingName(std::uint8_t opcode) {
    switch (static_cast<Incoming>(opcode)) {
    case Incoming::EnterWorld:                 return "Pkt_EnterWorld";
    case Incoming::ZoneChangeInfo:             return "Pkt_ZoneChangeInfo";
    case Incoming::ServerBillboardImage:       return "Pkt_SystemMessageBox";
    case Incoming::SpawnCharacter:             return "Pkt_SpawnCharacter";
    case Incoming::CharStateUpdate:            return "Pkt_CharStateUpdate";
    case Incoming::CharStatDelta:              return "Pkt_CharStatDelta";
    case Incoming::SpawnMonster:               return "Pkt_SpawnMonster";
    case Incoming::SpawnNpc:                   return "Pkt_SpawnNpc";
    case Incoming::ChatNotice:                 return "Pkt_ChatNotice";
    case Incoming::OnCombatResult:             return "Pkt_OnCombatResult";
    case Incoming::SetGameVar:                 return "Pkt_SetGameVar";
    case Incoming::MapObjectUpdate:            return "Pkt_MapObjectUpdate";
    case Incoming::GameServerConnectResult:    return "Pkt_GameServerConnectResult";
    case Incoming::GroundItemRemove:           return "Pkt_GroundItemRemove";
    case Incoming::ItemActionDispatch:         return "Pkt_ItemActionDispatch";
    case Incoming::ItemUpgradeResult:          return "Pkt_ItemUpgradeResult";
    case Incoming::ItemRefineResult:           return "Pkt_ItemRefineResult";
    case Incoming::ItemCombineResult:          return "Pkt_ItemCombineResult";
    case Incoming::ItemSwapResultA:            return "Pkt_ItemSwapResultA";
    case Incoming::ItemSwapResultB:            return "Pkt_ItemSwapResultB";
    case Incoming::ItemDiscardResult:          return "Pkt_ItemDiscardResult";
    case Incoming::ItemResultSimple:           return "Pkt_ItemResultSimple";
    case Incoming::WarehouseOpen:              return "Pkt_WarehouseOpen";
    case Incoming::WarehouseClose:             return "Pkt_WarehouseClose";
    case Incoming::WarehouseUpdate:            return "Pkt_WarehouseUpdate";
    case Incoming::VendorItemEntry:            return "Pkt_VendorItemEntry";
    case Incoming::TradeResult:                return "Pkt_TradeResult";
    case Incoming::QuestInteractResult:        return "Pkt_SmithUpgradeResult";
    case Incoming::ToggleObserver:             return "Pkt_ToggleObserver";
    case Incoming::WhisperReceive:             return "Pkt_WhisperReceive";
    case Incoming::PartyChatOrInvite:          return "Pkt_PartyChatOrInvite";
    case Incoming::ShoutMessage:               return "Pkt_ShoutMessage";
    case Incoming::DuelResult:                 return "Pkt_DuelResult";
    case Incoming::RepairResult:               return "Pkt_RepairResult";
    case Incoming::PartyInvitePrompt:          return "Pkt_PartyInvitePrompt";
    case Incoming::PartyInviteDecline:         return "Pkt_PartyInviteDecline";
    case Incoming::PartyJoinResult:            return "Pkt_PartyJoinResult";
    case Incoming::TradeRequestPrompt:         return "Pkt_TradeRequestPrompt";
    case Incoming::TradeRequestResult:         return "Pkt_TradeRequestResult";
    case Incoming::TradeActionResult:          return "Pkt_TradeActionResult";
    case Incoming::AllyInvitePrompt:           return "Pkt_AllyInvitePrompt";
    case Incoming::AllyInviteDecline:          return "Pkt_AllyInviteDecline";
    case Incoming::AllyJoinResult:             return "Pkt_AllyJoinResult";
    case Incoming::GuildMemberInfo:            return "Pkt_GuildMemberInfo";
    case Incoming::GuildInfoUpdate:            return "Pkt_GuildInfoUpdate";
    case Incoming::OnPvpTallyUpdate:           return "Net_OnPvpTallyUpdate";
    case Incoming::OnFactionBoardSync:         return "Net_OnFactionBoardSync";
    case Incoming::OnConfirmPromptOpen_Dlg19:  return "Net_OnConfirmPromptOpen_Dlg19";
    case Incoming::OnConfirmPromptClose_Dlg19: return "Net_OnConfirmPromptClose_Dlg19";
    case Incoming::OnPartyResultDialog:        return "Net_OnPartyResultDialog";
    case Incoming::OnPartyMemberNameSet:       return "Net_OnPartyMemberNameSet";
    case Incoming::OnPartyMemberValueSet:      return "Net_OnPartyMemberValueSet";
    case Incoming::OnPartyMemberClear:         return "Net_OnPartyMemberClear";
    case Incoming::OnConfirmPromptOpen_Dlg20:  return "Net_OnConfirmPromptOpen_Dlg20";
    case Incoming::OnConfirmPromptClose_Dlg20: return "Net_OnConfirmPromptClose_Dlg20";
    case Incoming::OnTradeResultDialog:        return "Net_OnTradeResultDialog";
    case Incoming::OnRequestTargetNameSet:     return "Net_OnRequestTargetNameSet";
    case Incoming::OnRequestCancelClear:       return "Net_OnRequestCancelClear";
    case Incoming::OnRequestStateSet:          return "Net_OnRequestStateSet";
    case Incoming::OnConfirmPromptOpen_Dlg10:  return "Net_OnConfirmPromptOpen_Dlg10";
    case Incoming::OnConfirmPromptClose_Dlg10: return "Net_OnConfirmPromptClose_Dlg10";
    case Incoming::OnResultDialog340:          return "Net_OnResultDialog340";
    case Incoming::OnGuildRosterReset:         return "Net_OnGuildRosterReset";
    case Incoming::OnGuildMemberJoin:          return "Net_OnGuildMemberJoin";
    case Incoming::OnGuildChatMessage:         return "Net_OnGuildChatMessage";
    case Incoming::OnGuildMemberLeave:         return "Net_OnGuildMemberLeave";
    case Incoming::OnGuildMemberKick:          return "Net_OnGuildMemberKick";
    case Incoming::OnGuildRosterUpdate:        return "Net_OnGuildRosterUpdate";
    case Incoming::OnConfirmPromptOpen_Dlg14:  return "Net_OnConfirmPromptOpen_Dlg14";
    case Incoming::OnConfirmPromptClose_Dlg14: return "Net_OnConfirmPromptClose_Dlg14";
    case Incoming::OnResultDialog399:          return "Net_OnResultDialog399";
    case Incoming::OnGuildWorkDispatch:        return "Net_OnTeamFormationDispatch";
    case Incoming::OnFactionNoticeChat:        return "Net_OnGuildNoticeChat";
    case Incoming::OnFactionChatMessage:       return "Net_OnFactionChatMessage";
    case Incoming::OnTeamSlotAssign:           return "Net_OnTeamSlotAssign";
    case Incoming::OnSelfFactionChat:          return "Net_OnSelfFactionChat";
    case Incoming::OnCultivationDispatch:      return "Net_OnCultivationDispatch";
    case Incoming::OnWhisperMessage:           return "Net_OnWhisperMessage";
    case Incoming::OnTradeChatMessage:         return "Net_OnTradeChatMessage";
    case Incoming::OnQuickslotSync:            return "Net_OnQuickslotSync";
    case Incoming::OnGuildActionResult:        return "Net_OnGuildActionResult";
    case Incoming::OnPartyInviteResult:        return "Net_OnPartyInviteResult";
    case Incoming::OnWorldEntityDispatch:      return "Net_OnWorldEntityDispatch";
    case Incoming::OnBossHpInit:               return "Net_OnBossHpInit";
    case Incoming::OnZoneBuffStatus:           return "Net_OnZoneBuffStatus";
    case Incoming::OnServerNameNotice:         return "Net_OnServerNameNotice";
    case Incoming::Sub_4A55E0:                 return "sub_4A55E0";
    case Incoming::OnGameGuardChallenge:       return "Net_OnScriptTrigger";
    case Incoming::OnBossHpDecrement:          return "Net_OnBossHpDecrement";
    case Incoming::OnBossSpawnNotice:          return "Net_OnBossSpawnNotice";
    case Incoming::OnPetSlotDispatch:          return "Net_OnPetSlotDispatch";
    case Incoming::OnBossHpInit2:              return "Net_OnBossHpInit2";
    case Incoming::OnBossHpPercent:            return "Net_OnBossHpPercent";
    case Incoming::OnItemCellSet:              return "Net_OnItemCellSet";
    case Incoming::OnItemSellResult:           return "Net_OnItemSellResult";
    case Incoming::OnGambleResult:             return "Net_OnGambleResult";
    case Incoming::OnWarehouseMoveResult:      return "Net_OnWarehouseMoveResult";
    case Incoming::OnVendorInventoryLoad:      return "Net_OnVendorInventoryLoad";
    case Incoming::OnVendorClose:              return "Net_OnVendorClose";
    case Incoming::OnSkillCooldownSet:         return "Net_OnSkillCooldownSet";
    case Incoming::OnItemCombineResult:        return "Net_OnItemCombineResult";
    case Incoming::Sub_4A7150:                 return "sub_4A7150";
    case Incoming::OnRevivePrompt:             return "Net_OnRevivePrompt";
    case Incoming::OnCountdownTimerStart:      return "Net_OnCountdownTimerStart";
    case Incoming::OnCraftResultNotice:        return "Net_OnCraftResultNotice";
    case Incoming::OnItemEnchantDispatch:      return "Net_OnItemEnchantDispatch";
    case Incoming::OnMinigameStateLoad:        return "Net_OnMinigameStateLoad";
    case Incoming::OnInventoryBulkLoad:        return "Net_OnInventoryBulkLoad";
    case Incoming::OnEquipSlotUpdate:          return "Net_OnEquipSlotUpdate";
    case Incoming::OnSocialListRemove:         return "Net_OnSocialListRemove";
    case Incoming::OnItemPlaceResult:          return "Net_OnItemPlaceResult";
    case Incoming::OnPartyMemberTargetSet:     return "Net_OnPartyMemberTargetSet";
    case Incoming::OnItemRefineResult:         return "Net_OnItemRefineResult";
    case Incoming::OnSkillAuraSync:            return "Net_OnSkillAuraSync";
    case Incoming::OnFriendStatusNotice:       return "Net_OnFriendStatusNotice";
    case Incoming::OnPartyMemberHpSet:         return "Net_OnPartyMemberHpSet";
    case Incoming::OnPartyMemberUpdate:        return "Net_OnPartyMemberUpdate";
    case Incoming::OnPartyItemResult:          return "Net_OnPartyItemResult";
    case Incoming::Sub_4AAB60:                 return "sub_4AAB60";
    case Incoming::OnPlayerEquipVisual:        return "Net_OnPlayerEquipVisual";
    case Incoming::OnSummonSpawn:              return "Net_OnSummonSpawn";
    case Incoming::OnSystemNotice:             return "Net_OnSystemNotice";
    case Incoming::SpawnZoneObject:            return "Pkt_SpawnZoneObject";
    case Incoming::PlayerShopOpen:             return "Pkt_PlayerShopOpen";
    case Incoming::PlayerShopBuyResult:        return "Pkt_PlayerShopBuyResult";
    case Incoming::PlayerShopGoldResult:       return "Pkt_PlayerShopGoldResult";
    case Incoming::OnItemCellClear:            return "Net_OnItemCellClear";
    case Incoming::OnTradeChatMsg_Ch24:        return "Net_OnTradeChatMsg_Ch24";
    case Incoming::OnItemCountNotice:          return "Net_OnItemCountNotice";
    case Incoming::OnBulkItemConsume:          return "Net_OnBulkItemConsume";
    case Incoming::OnUpgradeCountNotice:       return "Net_OnUpgradeCountNotice";
    case Incoming::Sub_4AB020:                 return "sub_4AB020";
    case Incoming::OnFriendListEvent:          return "Net_OnFriendListEvent";
    case Incoming::OnPartyMemberPosition:      return "Net_OnPartyMemberPosition";
    case Incoming::OnItemMoveResult:           return "Net_OnItemMoveResult";
    case Incoming::OnBattlefieldStatus:        return "Net_OnBattlefieldStatus";
    case Incoming::OnDataTableLoad_1686F74:    return "Net_OnDataTableLoad_1686F74";
    case Incoming::OnItemBatchUpdate:          return "Net_OnItemBatchUpdate";
    case Incoming::OnDataTableLoad_1686CCC:    return "Net_OnDataTableLoad_1686CCC";
    case Incoming::OnMultiItemRemove:          return "Net_OnMultiItemRemove";
    case Incoming::OnAchievementDataLoad:      return "Net_OnAchievementDataLoad";
    case Incoming::OnAchievementNotice:        return "Net_OnAchievementNotice";
    case Incoming::OnMountTicketPrompt:        return "Net_OnMountTicketPrompt";
    case Incoming::OnItemSocketResult:         return "Net_OnItemSocketResult";
    case Incoming::OnBossHpBarUpdate:          return "Net_OnBossHpBarUpdate";
    case Incoming::OnBossPanelLoad:            return "Net_OnBossPanelLoad";
    case Incoming::OnNpcDialogEvent:           return "Net_OnNpcDialogEvent";
    case Incoming::OnInstanceEnter:            return "Net_OnInstanceEnter";
    case Incoming::OnItemBuyResult:            return "Net_OnItemBuyResult";
    case Incoming::OnChargeStackUpdate:        return "Net_OnChargeStackUpdate";
    case Incoming::OnHonorRankEvent:           return "Net_OnHonorRankEvent";
    case Incoming::OnItemUpgradeResult:        return "Net_OnItemUpgradeResult";
    case Incoming::OnItemFuseResult:           return "Net_OnItemFuseResult";
    case Incoming::OnBattlefieldStateChange:   return "Net_OnBattlefieldStateChange";
    case Incoming::OnItemSocketDispatch:       return "Net_OnItemSocketDispatch";
    case Incoming::OnItemRefineDispatch:       return "Net_OnItemRefineDispatch";
    case Incoming::OnItemSlotRefresh:          return "Net_OnItemSlotRefresh";
    case Incoming::OnBuffEffectDispatch:       return "Net_OnBuffEffectDispatch";
    case Incoming::OnItemEnhanceResult:        return "Net_OnItemEnhanceResult";
    case Incoming::OnItemEnhanceResult2:       return "Net_OnItemEnhanceResult2";
    case Incoming::Sub_4B33C0:                 return "sub_4B33C0";
    case Incoming::OnRankBoardLoad:            return "Net_OnRankBoardLoad";
    case Incoming::OnItemDropResult:           return "Net_OnItemDropResult";
    case Incoming::OnStatSyncDispatch:         return "Net_OnStatSyncDispatch";
    case Incoming::OnItemCellReset:            return "Net_OnItemCellReset";
    default:                                   return nullptr;
    }
}

} // namespace ts2::net

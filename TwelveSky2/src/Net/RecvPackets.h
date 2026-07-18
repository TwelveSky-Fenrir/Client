// Net/RecvPackets.h — umbrella header for INCOMING (S->C) packet structs/parsers of TwelveSky2.
// GENERATED (workflow ts2-net-handlers-codegen) from Hex-Rays decompiles, then split by
// domain (workflow ts2-net-handlers-split) into the 9 RecvPackets_<Domain>.h files below.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) is documented in RE/net_handler_notes.md.
#pragma once

#include "Net/RecvPackets_BossWorld.h"
#include "Net/RecvPackets_ChatSocial.h"
#include "Net/RecvPackets_Core.h"
#include "Net/RecvPackets_Entity.h"
#include "Net/RecvPackets_InvCells.h"
#include "Net/RecvPackets_InvDispatch.h"
#include "Net/RecvPackets_Misc.h"
#include "Net/RecvPackets_PartyGuild.h"
#include "Net/RecvPackets_VendorTrade.h"

// Net/GameHandlers_BossWorld.cpp — routage des paquets BOSS / ZONE / MONDE.
//
// Domaine « boss_world » (RE/handler_domains.json) : barres de vie de boss
// (init/percent/panel/spawn/decrement), buffs de zone, changement de zone/carte,
// objets de zone, instances/événements, champ de bataille (guerre), tables UI de
// classement/accomplissements. La sémantique fidèle d'origine est documentée dans
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
// RÈGLE : ce module N'ÉDITE PAS l'état partagé (ClientRuntime.h) — il l'utilise.
// Les globals non modélisés sont stockés fidèlement via g_Client.Var(adresseOrigine).
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // net::g_GmCmdCooldownLatch
#include "Game/ClientRuntime.h"
#include "Game/MapWarp.h"      // game::BeginWarpToFactionTown (résolution warp, pas l'envoi)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (coordonnées réelles 003.BIN)
#include "Game/GameState.h"    // game::g_World.zoneObjects (ZoneObjectEntity)
#include "Game/AutoTargetCombatGate.h" // kAutoTargetModeAddr/kAutoTargetIdHiAddr (mode 7 == zoneObjects)
#include "Game/SocialSystem.h" // game::g_Achievements (AchievementDataLoad 0x98 -> dword_184C218)

#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

namespace {
// Plages de g_SelfMorphNpcId considérées « champ de bataille » (BattlefieldStatus 0x93 /
// BattlefieldStateChange 0xaa, RE/net_handler_notes.md) : 126..137 / 171..174 / 210..233.
bool InBattlegroundMorphRange(int32_t morphId) {
    return (morphId >= 126 && morphId <= 137) ||
           (morphId >= 171 && morphId <= 174) ||
           (morphId >= 210 && morphId <= 233);
}
} // namespace

void RegisterBossWorldHandlers(NetSystem& sys) {
    using namespace game;  // g_Client, Str()

    // 0x0d ZoneChangeInfo — deux blobs opaques de changement de zone.
    // Original : block1(1324) -> dword_1685E08, block2(2456) -> byte_1686334, puis
    // World_LoadCurrentZoneModel(2) selon g_SelfMorphNpcId + flags dword_1686120..30.
    OnPacket<ZoneChangeInfo>(sys, 0x0d, [](const ZoneChangeInfo& p) {
        // Recopie fidèle des 2 tampons (structure interne non décodée — blob brut,
        // cf. g_Client.Blob, motif déjà utilisé pour byte_1686F74/1686CCC/1825E70).
        std::memcpy(g_Client.Blob(0x1685E08, sizeof p.block1).data(), p.block1, sizeof p.block1);
        std::memcpy(g_Client.Blob(0x1686334, sizeof p.block2).data(), p.block2, sizeof p.block2);
        // TODO(state): recharger le modèle de zone courante (World_LoadCurrentZoneModel(2),
        //   ea=0x4644xx?) selon g_SelfMorphNpcId (270..274, hors payload) + flags
        //   dword_1686120/1686124/1686128/168612C/1686130 — pipeline de rendu monde
        //   (World/WorldMap.*), hors périmètre réseau de ce module.
    });

    // 0x17 MapObjectUpdate — relais opaque vers Pkt_DispatchStorageResponse(a,b,body).
    OnPacket<MapObjectUpdate>(sys, 0x17, [](const MapObjectUpdate&) {
        // TODO(state): interpréter body[100] via le sous-dispatcher de stockage/carte.
    });

    // 0x5f BossHpInit — initialise la 1re barre de boss (dword_1675BB4 = hp/2).
    OnPacket<BossHpInit>(sys, 0x5f, [](const BossHpInit& p) {
        BossBar& bar = g_Client.boss[0];
        bar.active  = true;
        bar.percent = static_cast<int>(p.hp / 2);  // dword_1675BB4
        bar.a = p.a; bar.b = p.b; bar.c = p.c; bar.d = p.d;  // dword_1675BBC..C8
        g_Client.msg.System("[" + std::to_string(bar.percent) + "]" + Str(843)); // couleur 1
    });

    // 0x60 ZoneBuffStatus — état ON/OFF des buffs de zone par faction (4 drapeaux).
    OnPacket<ZoneBuffStatus>(sys, 0x60, [](const ZoneBuffStatus& p) {
        std::string line;
        for (int i = 0; i < 3; ++i)  // str75..77 = nom de faction ; str240/241 = ON/OFF
            line += Str(75 + i) + (p.flags[i] ? Str(240) : Str(241)) + " ";
        // Le 4e drapeau (str78) n'est ajouté que si g_SelfMorphNpcId > 153 (global non modélisé).
        line += Str(78) + (p.flags[3] ? Str(240) : Str(241));
        g_Client.msg.System(line);
        // Si le buff de la faction locale est OFF -> retour ville de faction. L'envoi
        // réseau reste un TODO(send) interne à MapWarp.cpp (Net_SendPacket_Op20).
        if (g_LocalElement < 4 && p.flags[g_LocalElement] == 0) {
            BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
        }
    });

    // 0x64 BossHpDecrement — décrémente le compteur de boss restants + màj PV/infos.
    OnPacket<BossHpDecrement>(sys, 0x64, [](const BossHpDecrement& p) {
        int32_t& remaining = g_Client.Var(0x1675C8C);  // compteur de boss restants
        --remaining;
        g_Client.msg.System("[" + std::to_string(remaining) + "]" + Str(194) + Str(843));
        g_Client.Var(0x1675C90) = static_cast<int32_t>(p.f0);
        g_Client.Var(0x1675C94) = static_cast<int32_t>(p.f1);
        g_Client.Var(0x1675C98) = static_cast<int32_t>(p.f2);
        g_Client.Var(0x1675C9C) = static_cast<int32_t>(p.f3);
    });

    // 0x65 BossSpawnNotice — notification d'apparition de boss (message couleur 3).
    OnPacket<BossSpawnNotice>(sys, 0x65, [](const BossSpawnNotice& p) {
        g_Client.msg.System(Str(194) + " [" + std::to_string(p.value) + "]" + Str(857));
    });

    // 0x67 BossHpInit2 — initialise la 2e barre de boss (dword_1675CB8 = hp/2).
    OnPacket<BossHpInit2>(sys, 0x67, [](const BossHpInit2& p) {
        BossBar& bar = g_Client.boss[1];
        bar.active  = true;
        bar.percent = static_cast<int>(p.hp / 2);  // dword_1675CB8
        bar.a = p.a; bar.b = p.b; bar.c = p.c; bar.d = p.d;  // dword_1675CC0..CC
        g_Client.msg.System("[" + std::to_string(bar.percent) + "]" + Str(843)); // couleur 1
    });

    // 0x68 BossHpPercent — affiche le pourcentage de PV de boss (hp/2). Pas d'état persistant.
    OnPacket<BossHpPercent>(sys, 0x68, [](const BossHpPercent& p) {
        g_Client.msg.System("[" + std::to_string(p.hp / 2) + "]" + Str(843)); // couleur 1
    });

    // 0x86 SpawnZoneObject — objet de zone (portail/porte) : tableau à stride 19 dwords
    //   dword_180EEF4 == game::g_World.zoneObjects (capacité fixe 500, cf. GameData_InitPools
    //   / Game/MiscManagers.cpp), recherché par (idHi,idLo) — fidèle à RE/net_handler_notes.md
    //   (## Pkt_SpawnZoneObject, op 0x86) :
    //     action==2 : slot trouvé -> rafraîchir spawnTimestamp + recopier body ; sinon 1er
    //                 slot libre (index < capacité, PAS d'agrandissement — pool plein = no-op,
    //                 fidèle : le binaire ne redimensionne jamais ce tableau).
    //     action==3 : libérer le slot (sub_583F70 == reset à l'état par défaut) et, si la
    //                 cible auto verrouillée courante pointait CE slot (dword_1675B24==7 &&
    //                 dword_1675B28==index, cf. Game/AutoTargetCombatGate.h::
    //                 kAutoTargetModeAddr/kAutoTargetIdHiAddr — mode 7 == g_World.zoneObjects),
    //                 RAZ dword_1675B24=0.
    //   AVANT ce câblage, g_World.zoneObjects restait figé à 500 slots inactifs (dimensionné
    //   par GameData_InitPools mais jamais peuplé) : Game/GroundAuraWorldObjectTick.h
    //   (GetWorldObjectCount/IsWorldObjectActive/TickWorldObject) lisait donc un pool
    //   perpétuellement vide — rupture de chaîne réseau -> gameplay corrigée ici.
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
            if (idx < 0) return; // pool plein (500 slots) -> no-op, fidèle (pas d'agrandissement).
            ZoneObjectEntity& z = zones[static_cast<size_t>(idx)];
            z.active         = true;
            z.objId1         = p.idHi;
            z.objId2         = p.idLo;
            z.spawnTimestamp = g_World.gameTimeSec;             // flt_815180 (g_GameTimeSec)
            std::memcpy(z.body.data(), p.body, sizeof p.body);
        } else if (p.action == 3 && foundIdx >= 0) {
            zones[static_cast<size_t>(foundIdx)] = ZoneObjectEntity{}; // sub_583F70 (libère le slot)
            if (g_Client.VarGet(kAutoTargetModeAddr) == 7 &&
                g_Client.VarGet(kAutoTargetIdHiAddr) == foundIdx) {
                g_Client.Var(kAutoTargetModeAddr) = 0;
            }
        }
    });

    // 0x93 BattlefieldStatus — état de guerre de zone (dword_16692A0) + messages/sortie.
    OnPacket<BattlefieldStatus>(sys, 0x93, [](const BattlefieldStatus& p) {
        g_Client.Var(0x16692A0) = p.warState;  // état de guerre
        if (p.warState == 2 && p.param != 0) {
            if (p.subState == 0)
                g_Client.msg.System("[" + std::to_string(p.param) + "]" + Str(2231));
            else  // subState==1 : str2232 pour param in {5,10,15,20,25,30}, cas 60 spécial
                g_Client.msg.System("[" + std::to_string(p.param) + "]" + Str(2232));
        }
        // Si warState hors {2,3} et le perso est « morph » dans une plage BG -> sortie
        // forcée vers la ville de faction (RE/net_handler_notes.md ## BattlefieldStatus).
        if (p.warState != 2 && p.warState != 3) {
            const int32_t morph = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
            if (InBattlegroundMorphRange(morph)) {
                BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
            }
        }
    });

    // 0x94 DataTableLoad_1686F74 — charge une table de 680 o (byte_1686F74) si flag==0.
    OnPacket<DataTableLoad_1686F74>(sys, 0x94, [](const DataTableLoad_1686F74& p) {
        if (p.flag == 0) {
            std::memcpy(g_Client.Blob(0x1686F74, sizeof p.table).data(), p.table, sizeof p.table);
        }
    });

    // 0x96 DataTableLoad1686CCC — charge une table UI de 680 o (byte_1686CCC) si status==0.
    OnPacket<DataTableLoad1686CCC>(sys, 0x96, [](const DataTableLoad1686CCC& p) {
        if (p.status == 0) {
            std::memcpy(g_Client.Blob(0x1686CCC, sizeof p.table).data(), p.table, sizeof p.table);
        }
    });

    // 0x98 AchievementDataLoad — charge 96 o de flags d'accomplissements (dword_184C218).
    // g_Achievements (Game/SocialSystem.h) est le modèle DÉDIÉ pour ce global (consommé par
    // AchievementNotice 0x99 ci-dessous via BuildAchievementNotice/PostAchievementNotice).
    OnPacket<AchievementDataLoad>(sys, 0x98, [](const AchievementDataLoad& p) {
        g_Achievements.LoadFromPayload(p.flags);
    });

    // 0x9a MountTicketPrompt — notification NPC pour ticket de monture (items 783..789).
    OnPacket<MountTicketPrompt>(sys, 0x9a, [](const MountTicketPrompt& p) {
        // Original : ItemDefTbl_GetRecord(itemId) ; si type d'objet dans [783,789] ->
        //   "<StrTable003[strIndex]> <str2198>" en message flottant HUD(2,1) + ligne système.
        std::string t = Str(static_cast<int>(p.strIndex)) + " " + Str(2198);
        g_Client.msg.Floating(2, 1, t);
        g_Client.msg.System(t);
        // TODO(state): garde sur le type d'item (ItemDefTbl_GetRecord(p.itemId), type in
        //   [783,789]). NE PAS FORCER : ItemDefTbl est chargé depuis 005_00004.IMG (asset
        //   .IMG, cf. Game/GameDatabase.h), un runtime table lookup hors périmètre réseau —
        //   aucune structure ItemDefTbl vivante n'existe encore côté C++ réécrit. Le message
        //   ci-dessus reste donc affiché inconditionnellement (légèrement moins fidèle que
        //   l'original qui filtre par type d'objet), sans donnée inventée.
    });

    // 0x9d BossHpBarUpdate — met à jour le % de PV de boss (dword_1675E9C = hpRaw/2).
    OnPacket<BossHpBarUpdate>(sys, 0x9d, [](const BossHpBarUpdate& p) {
        int pct = static_cast<int>(p.hpRaw / 2);
        g_Client.Var(0x1675E9C) = pct;  // dword_1675E9C (boss hp %)
        std::string t = "[" + std::to_string(pct) + "]" + Str(843);
        g_Client.msg.System(t);  // couleur 1
        if (pct <= 30 && pct != 0)
            g_Client.msg.Floating(2, 1, t);  // alerte PV bas
    });

    // 0x9e BossPanelLoad — charge en-tête + corps (420 o) du panneau de boss.
    OnPacket<BossPanelLoad>(sys, 0x9e, [](const BossPanelLoad& p) {
        std::memcpy(g_Client.boss[0].panel.data(), p.body, sizeof p.body);  // dword_1675EB0
        g_Client.Var(0x1675EA0) = static_cast<int32_t>(p.header[0]);
        g_Client.Var(0x1675EA4) = static_cast<int32_t>(p.header[1]);
        g_Client.Var(0x1675EA8) = static_cast<int32_t>(p.header[2]);
        g_Client.Var(0x1675EAC) = static_cast<int32_t>(p.header[3]);
    });

    // 0xa3 InstanceEnter — entrée/résultat d'instance ou d'événement.
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
                for (int i = 0; i < 100; ++i)                // efface dword_1822FE0[0..99]
                    g_Client.Var(0x1822FE0 + static_cast<uint32_t>(i) * 4u) = 0;
            } else if (p.code == 1) {
                storeParams();
                g_Client.msg.System(Str(2373));
            }
        } else if (p.subop == 2) {
            if (p.code == 0) {
                storeParams();
                g_Client.msg.System(Str(2377));
                // TODO(ui): UI_NpcWin_CloseRestore (fermeture fenêtre NPC — action UI pure,
                //   cf. TODO(ui) identiques dans GameHandlers_VendorTrade.cpp).
            } else if (p.code == 1 || p.code == 2) {
                g_Client.msg.System(Str(223));
            } else if (p.code == 3) {
                g_Client.msg.System(Str(117));
            }
        }
    });

    // 0xa6 HonorRankEvent — événements de rang honneur/PK (purement messages, sauf cat 3).
    OnPacket<HonorRankEvent>(sys, 0xa6, [](const HonorRankEvent& p) {
        switch (p.category) {
        case 0:  // switch(value) -> str2673..2676
            if (p.value <= 3) g_Client.msg.System(Str(2673 + static_cast<int>(p.value)));
            break;
        case 1:  // value 0..3 -> str2669..2672
            if (p.value <= 3) g_Client.msg.System(Str(2669 + static_cast<int>(p.value)));
            break;
        case 2:  // affiche les 4 lignes str2669..2672
            for (int i = 0; i < 4; ++i) g_Client.msg.System(Str(2669 + i));
            break;
        case 3:  // seul état modifié : dword_16760F4
            g_Client.Var(0x16760F4) = static_cast<int32_t>(p.value);
            if (p.value >= 1 && p.value <= 3) {
                // 2 lignes multi-fragments (str2532 / 2685..2688 / 377 / 378) selon value.
                g_Client.msg.System(Str(2532) + " " + Str(2684 + static_cast<int>(p.value)));
                g_Client.msg.System(Str(377) + " " + Str(378));
            }
            // Complet : RE/net_handler_notes.md confirme qu'aucun autre état n'est modifié
            // (« Aucun état de jeu modifié hormis dword_16760F4 »).
            break;
        default:
            break;
        }
    });

    // 0xaa BattlefieldStateChange — changement d'état du champ de bataille (dword_16692A0).
    OnPacket<BattlefieldStateChange>(sys, 0xaa, [](const BattlefieldStateChange& p) {
        g_Client.Var(0x16692A0) = static_cast<int32_t>(p.value);  // état du BG
        g_Client.msg.System(Str(p.state == 0 ? 2537 : 2538));
        // Si value!=3, joueur NON spécial (dword_1674708<1 && dword_167588C<=0) et
        // g_SelfMorphNpcId dans une plage BG -> éjection vers la ville de faction
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

    // 0xb2 RankBoardLoad — charge le tableau de classement (en-tête + corps 600 o).
    OnPacket<RankBoardLoad>(sys, 0xb2, [](const RankBoardLoad& p) {
        g_Client.Var(0x18260C8) = static_cast<int32_t>(p.header);        // nb total
        g_Client.Var(0x18260D0) = static_cast<int32_t>(p.header / 10);   // nb de pages
        g_GmCmdCooldownLatch = 0;
        // Corps brut (structure d'entrée de classement non décodée par le handler
        // d'origine — blob fidèle, cf. g_Client.Blob).
        std::memcpy(g_Client.Blob(0x1825E70, sizeof p.body).data(), p.body, sizeof p.body);
    });
}

} // namespace ts2::net

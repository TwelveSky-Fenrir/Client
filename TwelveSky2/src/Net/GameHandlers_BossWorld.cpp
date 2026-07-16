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
#include "Game/StringTables.h" // game::g_Strings.zoneNames (StrTable003_Get 0x4C1AD0 — 003.DAT/mZONENAME)
#include "Game/GameDatabase.h" // game::GetMonsterInfo (ItemDefTbl_GetRecord 0x4C6570 — mMONSTER)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
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

// StrTable003_Get(dword_84A6A8, id) 0x4C1AD0 — table des NOMS DE ZONE (003.DAT / mZONENAME),
// DISTINCTE de StrTable005_Get(g_LangId, id) 0x4C1D10 (005.DAT / mMESSAGE) qu'expose game::Str().
// Index 1-based, chaîne vide hors bornes (comme &String dans l'original).
// Ancres d'usage dans ce module : Net_OnBossHpDecrement 0x4A5640 (EA 0x4A5699),
//   Net_OnBossSpawnNotice 0x4A5710 (EA 0x4A5754), Net_OnMountTicketPrompt 0x4ACA50 (EA 0x4ACB2A).
// Homologue à LINKAGE INTERNE du Str3 de Net/GameHandlers_Misc.cpp (lui aussi en namespace
// anonyme) : les deux TU ne peuvent pas entrer en collision de symbole.
std::string Str3(int id) { return std::string(game::g_Strings.zoneNames.Get(id)); }

// Crt_Vsnprintf(buf, StrTable005_Get(g_LangId, strId), ...) 0x75CD5F — le FORMAT est l'entrée
// de table localisée elle-même (et NON un littéral) : cf. Net_OnBattlefieldStatus 0x4ABD00,
// EA 0x4ABDA5 (StrTable005_Get(g_LangId, 2231)) puis EA 0x4ABDB2 (Crt_Vsnprintf(v7, v1, v3)).
// Buffer 1000 o comme l'original (v7[1000], [ebp-3F0h]).
std::string FmtFromStrTable(int strId, ...) {
    char buf[1000];
    va_list ap; va_start(ap, strId);
    std::vsnprintf(buf, sizeof buf, game::Str(strId).c_str(), ap);
    va_end(ap);
    return std::string(buf);
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
        // Ancre : Pkt_ZoneChangeInfo 0x464500 — switch @0x4645A7 sur g_SelfMorphNpcId (270..274),
        //   gardes @0x4645B5/0x4645CC/0x4645E3/0x4645FA/0x464611 (`== 1`), appels
        //   World_LoadCurrentZoneModel(g_GameWorld, 2) 0x4DD6E0 @0x4645BE/D5/EC/0x464603/0x46461A.
        //
        // CORRECTION D'UN COMMENTAIRE FAUX (ex-« flags hors payload ») : les 5 drapeaux SONT
        //   dans le payload. block1 est recopié à dword_1685E08 sur 0x52C=1324 o (@0x464559),
        //   donc il occupe [0x1685E08, 0x1686334) ; or 0x1686120 - 0x1685E08 = 0x318 = 792.
        //   Les drapeaux dword_1686120/24/28/2C/30 sont donc block1+792/796/800/804/808,
        //   c.-à-d. p.block1[792..811]. Seul g_SelfMorphNpcId (0x1675A98) est réellement externe.
        //
        // TODO(state) [ancre 0x4645BE] : l'appel World_LoadCurrentZoneModel(2) reste INJOIGNABLE
        //   depuis Net/. Unique point d'entrée C++ = le hook AnimHost::LoadCurrentZoneModel
        //   (Game/AnimationTick.h:111), câblé sur world::WorldMap::LoadCurrentZoneModel dans
        //   Scene/SceneManager.cpp:869 ; il n'existe AUCUNE instance globale de world::WorldMap
        //   et game::GameWorld n'expose aucun pointeur de carte. Même manque systémique que
        //   Net/WorldEntityDispatch.cpp:2374. Le câblage doit venir du propriétaire de
        //   Game/AnimationTick.* + Scene/SceneManager.* (hors périmètre de ce front).
    });

    // 0x17 MapObjectUpdate — relais opaque vers Pkt_DispatchStorageResponse(a,b,body).
    // Ancre : Pkt_MapObjectUpdate 0x469C80 — recopie payload+0 (4 o), payload+4 (4 o),
    //   payload+8 (0x64=100 o) puis DÉLÈGUE tout à Pkt_DispatchStorageResponse 0x58A0F0 @0x469CDA.
    //
    // SIGNATURE PROUVÉE PAR L'ASM (le prototype IDA à 6 args est FAUX — c'est lui qui produit
    //   le « v2 dupliqué » du pseudocode) :
    //     char __thiscall Pkt_DispatchStorageResponse(void* this /* = g_LocalPlayerSheet 0x1685748 */,
    //                                                 int a /*payload+0*/, int subOpcode /*payload+4*/,
    //                                                 uint8_t* body /*payload+8, 100 o*/);
    //   Preuves : 3 push seulement (@0x469CC9 body / @0x469CD0 b / @0x469CD4 a) ;
    //   `mov ecx, offset g_LocalPlayerSheet` @0x469CD5 (thiscall) ; `retn 0Ch` @0x58FE8F (12 o
    //   = 3 args) ; le stack_frame de 0x58A0F0 ne contient QUE arg_0/arg_4/arg_8.
    //   CLÉ DU SWITCH = `b` (payload+4), PAS `a` : `mov ecx, [ebp+arg_4]` @0x58A12F.
    //   Plage vivante : `sub edx, 0C9h` @0x58A13E + borne `ja def_58A167` @0x58A154 sur 0x14D
    //   -> sous-codes 201..534 ; table byte_58FFF8 @0x58A160 -> `jmp jpt_58A167[ecx*4]` @0x58A167.
    //   88 cas vivants (201-256, 501-531, 534) ; défaut def_58A167 0x58FE81 = NO-OP pur (épilogue seul).
    //
    // TODO(state) [ancre 0x58A0F0] : le corps (23970 o) n'existe nulle part en C++. Le portage
    //   exige un module dédié (Net/StorageResponseDispatch.cpp, modèle GameVarDispatch.cpp) donc
    //   un AJOUT AU .vcxproj — interdit dans cette vague. Point d'entrée à câbler alors :
    //     DispatchStorageResponse(g_LocalPlayerSheet, p.a, p.b, p.body);   // conforme à 0x469CDA
    //   RISQUE COURT TERME NUL : le cadrage est déjà correct (108 o consommés, cf. MapObjectUpdate
    //   dans RecvPackets.h) -> aucune désynchronisation de trame, seul le CORPS manque.
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
    // Ancre : Net_OnZoneBuffStatus 0x4A52A0 — 4 dwords lus à payload+0 (0x10 o, @0x4A52C1).
    OnPacket<ZoneBuffStatus>(sys, 0x60, [](const ZoneBuffStatus& p) {
        std::string line;
        // Format LITTÉRAL du binaire aSS_10 "[%s] %s " (0x7A6EAC), 3 blocs inconditionnels :
        //   str75..77 = nom de faction ; str240/241 = ON/OFF (Crt_Vsnprintf @0x4A530F/92/0x4A5415).
        // Le test est `== 1` (jnz @0x4A52E4 / 0x4A5367 / 0x4A53EA), PAS une véracité : un
        //   drapeau à 2 affiche OFF dans le binaire.
        for (int i = 0; i < 3; ++i)
            line += "[" + Str(75 + i) + "] " + (p.flags[i] == 1 ? Str(240) : Str(241)) + " ";
        // 4e bloc : format aSS_6 "[%s] %s" (0x7A6D54, SANS espace final), et surtout gardé par
        //   `cmp g_SelfMorphNpcId, 153 ; jle` @0x4A5470 -> comparaison SIGNÉE (VarGet renvoie int32_t).
        if (g_Client.VarGet(WarpAddr::SelfMorphNpcId) > 153)
            line += "[" + Str(78) + "] " + (p.flags[3] == 1 ? Str(240) : Str(241));
        g_Client.msg.System(line);  // Msg_AppendSystemLine(..., 1) @0x4A5507
        // Si le buff de la faction locale est OFF -> retour ville de faction. L'envoi
        // réseau reste un TODO(send) interne à MapWarp.cpp (Net_SendPacket_Op20).
        // ASYMÉTRIE FIDÈLE (à ne pas « harmoniser ») : l'AFFICHAGE ci-dessus teste `== 1`
        //   (@0x4A52E4...) alors que le WARP teste `== 0` (`if (!v9[g_LocalElement])` @0x4A5512,
        //   appel Map_BeginWarpToFactionTown @0x4A5523) -> un drapeau à 2 affiche OFF SANS warper.
        // `g_LocalElement < 4` = garde-fou défensif : le binaire indexe v9[4] sans borne.
        if (g_LocalElement < 4 && p.flags[g_LocalElement] == 0) {
            BeginWarpToFactionTown(static_cast<int32_t>(g_LocalElement), false, 0, &g_CoordResolver);
        }
    });

    // 0x64 BossHpDecrement — décrémente le compteur de boss restants + màj PV/infos.
    // Ancre : Net_OnBossHpDecrement 0x4A5640 — `--dword_1675C8C` @0x4A5672 AVANT lecture du
    //   compteur (@0x4A568E), puis Crt_Vsnprintf(v8, aSDS_4 "%s [%d]%s", v0, v2, v3) @0x4A56AB
    //   avec v0 = StrTable003_Get(dword_84A6A8, 194) @0x4A5699 et v3 = StrTable005_Get(.., 843).
    //   => sortie "<str003[194]> [<compteur>]<str005[843]>" (le 194 est EN TÊTE et vient de la
    //   table 003, pas de la 005 : l'ancienne forme inversait l'ordre ET la table).
    OnPacket<BossHpDecrement>(sys, 0x64, [](const BossHpDecrement& p) {
        int32_t& remaining = g_Client.Var(0x1675C8C);  // compteur de boss restants
        --remaining;
        g_Client.msg.System(Str3(194) + " [" + std::to_string(remaining) + "]" + Str(843));
        g_Client.Var(0x1675C90) = static_cast<int32_t>(p.f0);
        g_Client.Var(0x1675C94) = static_cast<int32_t>(p.f1);
        g_Client.Var(0x1675C98) = static_cast<int32_t>(p.f2);
        g_Client.Var(0x1675C9C) = static_cast<int32_t>(p.f3);
    });

    // 0x65 BossSpawnNotice — notification d'apparition de boss.
    // Ancre : Net_OnBossSpawnNotice 0x4A5710 — Crt_Vsnprintf(v4, "%s [%d]%s", v0, value, v3)
    //   @0x4A5766 avec v0 = StrTable003_Get(dword_84A6A8, 194) @0x4A5754 (table 003, PAS 005)
    //   et v3 = StrTable005_Get(g_LangId, 857) @0x4A5745. L'ordre "%s [%d]%s" était déjà fidèle ;
    //   seul le 194 était résolu dans la mauvaise table (Str -> Str3).
    // COULEUR NON TRANSMISE : Msg_AppendSystemLine(.., v4, 3) @0x4A5781 — le 3 est stocké BRUT en
    //   index (Msg_AppendSystemLine 0x68D9D0 : *(this+4*n+37272)=a3, PAS un ARGB). msg.System
    //   attend un D3DCOLOR ARGB (ClientRuntime.h) -> passer 3 donnerait alpha=0 (texte invisible).
    OnPacket<BossSpawnNotice>(sys, 0x65, [](const BossSpawnNotice& p) {
        g_Client.msg.System(Str3(194) + " [" + std::to_string(p.value) + "]" + Str(857));
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
    // Ancre : Net_OnBattlefieldStatus 0x4ABD00 — layout payload subState:u8@+0 (v5),
    //   warState:i32@+1 (v6, -> dword_16692A0), param:i32@+5 (v8) ; 9 o non alignés.
    OnPacket<BattlefieldStatus>(sys, 0x93, [](const BattlefieldStatus& p) {
        g_Client.Var(0x16692A0) = p.warState;  // dword_16692A0 = v6 @0x4ABD70
        // Messages gardés par `if (v6 == 2 && v8)` @0x4ABD86. La CHAÎNE Str(2231) est un FORMAT
        //   passé à Crt_Vsnprintf avec param (LABEL_4 @0x4ABD97/0x4ABDB2) — PAS de préfixe fabriqué
        //   « [param] ». Str(2232) est émise BRUTE (aucun Vsnprintf @0x4ABE69).
        if (p.warState == 2 && p.param != 0) {
            if (p.subState == 0 || (p.subState == 1 && p.param == 60)) {
                // subState==0 (else v5, LABEL_4) OU subState==1 && param==60 (case 60 -> goto
                //   LABEL_4 @0x4ABE15) -> Crt_Vsnprintf(v7, Str(2231), param).
                g_Client.msg.System(FmtFromStrTable(2231, p.param));
            } else if (p.subState == 1 &&
                       (p.param == 5 || p.param == 10 || p.param == 15 ||
                        p.param == 20 || p.param == 25 || p.param == 30)) {
                // subState==1, switch(param) cases 5/10/15/20/25/30 @0x4ABE15 -> Str(2232) brute.
                g_Client.msg.System(Str(2232));
            }
            // subState==1 & param hors {5,10,15,20,25,30,60} -> default @0x4ABE15 (rien) ;
            //   subState>=2 -> `jnz` @0x4ABDE1 (rien).
        }
        // Sortie forcée vers la ville de faction : condition composite @0x4ABEF0 —
        //   warState != 2 && warState != 3 && dword_1674708 < 1 && dword_167588C <= 0 &&
        //   morph in [126,137]/[171,174]/[210,233], puis switch(g_LocalElement) 0..3 (défaut =
        //   pas de warp @0x4ABF0D). Les deux gardes notSpecial et la borne 0..3 manquaient :
        //   sans elles, on warpait dans des cas où l'original ne fait rien. Aligné sur le frère
        //   0xaa BattlefieldStateChange ci-dessous.
        if (p.warState != 2 && p.warState != 3) {
            const bool notSpecial = g_Client.VarGet(0x1674708) < 1 &&   // @0x4ABEF0 (cmp signés)
                                     g_Client.VarGet(0x167588C) <= 0;
            const int32_t morph = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
            if (notSpecial && InBattlegroundMorphRange(morph) && g_LocalElement <= 3) {
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
    // Ancre : Net_OnMountTicketPrompt 0x4ACA50 — v6=itemId@payload+0, v5=strIndex@payload+4 ;
    //   result = ItemDefTbl_GetRecord(mMONSTER, itemId) @0x4ACAB1 ; garde
    //   `result && *result in [783,789]` @0x4ACB0F ; nom via StrTable003_Get(dword_84A6A8, strIndex)
    //   @0x4ACB2A ; format "%s %s" -> str003[strIndex] + " " + str005[2198] @0x4ACB39.
    OnPacket<MountTicketPrompt>(sys, 0x9a, [](const MountTicketPrompt& p) {
        // Résolution ItemDefTbl_GetRecord(mMONSTER, itemId) : game::GetMonsterInfo reproduit
        //   ItemDefTbl_GetRecord 0x4C6570 (1-based, garde record[0]!=0, cf. GameDatabase.cpp).
        //   Le test porte sur *result = le 1er dword du record (MonsterInfo::id, +0), PAS sur
        //   p.itemId : ne pas dépendre de l'invariant id==index+1. Prémisse de l'ancien TODO
        //   (« aucune structure ItemDefTbl vivante ») factuellement fausse -> garde restaurée.
        const game::MonsterInfo* mi = game::GetMonsterInfo(p.itemId);
        if (mi && mi->id >= 783 && mi->id <= 789) {  // *result in [783,789] @0x4ACB0F
            // Nom via StrTable003 (Str3), PAS StrTable005 : le binaire lit StrTable003_Get @0x4ACB2A.
            std::string t = Str3(static_cast<int>(p.strIndex)) + " " + Str(2198);
            g_Client.msg.Floating(2, 1, t);   // HUD_ShowFloatingMessage(.., 2, 1, ..) @0x4ACB53
            g_Client.msg.System(t);           // Msg_AppendSystemLine(.., g_SysMsgColor) @0x4ACB68
        }
        // Hors [783,789] : le binaire n'émet AUCUN message (early-out du `if`), fidèlement reproduit.
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

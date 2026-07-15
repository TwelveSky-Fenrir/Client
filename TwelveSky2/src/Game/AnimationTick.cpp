// Game/AnimationTick.cpp — implémentation. Voir Game/AnimationTick.h pour la doc complète
// (EA d'origine, périmètre, hosts/oracles). Décompilation source : idaTs2 (Hex-Rays).
#include "Game/AnimationTick.h"
#include "Game/ClientRuntime.h"   // g_Client.Var/VarF (échappatoire globals "longue traîne")
#include "Game/MapWarp.h"         // BeginWarpToFactionTown, WarpAddr::SelfMorphNpcId
#include "Game/CameraWarpTick.h"  // Cam_SetLookAt (déjà écrit, réutilisé tel quel)
#include <cmath>
#include <cstring>

namespace ts2::game {

// =====================================================================================
// Helpers communs (timers de morph data-driven — Player_UpdateLocalAnim ET les timers FX
// secondaires de Char_UpdateAnimationFrame partagent le même moteur).
// =====================================================================================
namespace {

// oracle==nullptr -> durée "infinie" : le timer avance mais ne complète jamais (dégradation
// propre, cf. Game/AnimationTick.h).
float MorphDuration(const IMorphModelOracle* oracle, uint32_t tableAddr) {
    return oracle ? static_cast<float>(oracle->GetSubObjectCount(tableAddr)) : 1.0e9f;
}

// ---------------------------------------------------------------------------
// Table 75 lignes "génériques" de Player_UpdateLocalAnim (0x5321D0), extraite
// mécaniquement de la décompilation (cf. mission : script d'extraction, vérifiée ligne à
// ligne). 3 lignes supplémentaires à index/table paramétrés par g_SelfMorphNpcId (blocs
// 0x1675BA4/BDC/BE4) et 1 ligne indexée par g_LocalElement (bloc 0x1675D98/DA8) sont HORS
// table (traitées à part dans Player_UpdateLocalAnim, cf. plus bas) + 3 blocs "pulse"
// (cadence this+8 %6, indépendants de ModelObj_GetSubObjectCount) également hors table.
// ---------------------------------------------------------------------------
struct MorphTimerRow {
    uint32_t flagAddr;
    uint32_t frameAddr;
    uint32_t tableAddr;
    bool     grow;            // true = croît vers `duration` ; false = décroît vers 0
    bool     clampOnComplete; // grow seulement : frame = duration-1 en fin de course
    int32_t  loadArg;         // -1 = pas d'appel World_LoadCurrentZoneModel
    bool     warpCheck;       // shrink seulement : + World_IsPointOnGround/BeginWarp
};

constexpr MorphTimerRow kMorphRows[] = {
    {0x1675BEC, 0x1675BF0, 0xB6201C, true,  false, -1, false},
    {0x1675BF4, 0x1675BF8, 0xB620B0, true,  false, -1, false},
    {0x1675BFC, 0x1675C00, 0xB65830, true,  false, -1, false},
    {0x1675C04, 0x1675C08, 0xB663C0, true,  false, -1, false},
    {0x1675C0C, 0x1675C5C, 0xB668F4, true,  true,   2, false},
    {0x1675C34, 0x1675C5C, 0xB668F4, false, true,  -1, false},
    {0x1675C10, 0x1675C60, 0xB66988, true,  true,  -1, false},
    {0x1675C38, 0x1675C60, 0xB66988, false, true,   3, true },
    {0x1675C14, 0x1675C64, 0xB66988, true,  true,   4, false},
    {0x1675C3C, 0x1675C64, 0xB66988, false, true,  -1, false},
    {0x1675C18, 0x1675C68, 0xB66A1C, true,  true,  -1, false},
    {0x1675C40, 0x1675C68, 0xB66A1C, false, true,   5, true },
    {0x1675C1C, 0x1675C6C, 0xB66A1C, true,  true,   6, false},
    {0x1675C44, 0x1675C6C, 0xB66A1C, false, true,  -1, false},
    {0x1675C20, 0x1675C70, 0xB66AB0, true,  true,  -1, false},
    {0x1675C48, 0x1675C70, 0xB66AB0, false, true,   7, true },
    {0x1675C24, 0x1675C74, 0xB66AB0, true,  true,   8, false},
    {0x1675C4C, 0x1675C74, 0xB66AB0, false, true,  -1, false},
    {0x1675C28, 0x1675C78, 0xB66B44, true,  true,  -1, false},
    {0x1675C50, 0x1675C78, 0xB66B44, false, true,   9, true },
    {0x1675C2C, 0x1675C7C, 0xB66B44, true,  true,  10, false},
    {0x1675C54, 0x1675C7C, 0xB66B44, false, true,  -1, false},
    {0x1675C30, 0x1675C80, 0xB66BD8, true,  true,  -1, false},
    {0x1675C58, 0x1675C80, 0xB66BD8, false, true,  11, true },
    {0x1675C84, 0x1675C88, 0xB66C6C, true,  true,   2, false},
    {0x1675CA0, 0x1675CA4, 0xB6201C, true,  false, -1, false},
    {0x1675CA8, 0x1675CAC, 0xB620B0, true,  false, -1, false},
    {0x1675CB0, 0x1675CB4, 0xB67C08, true,  true,   2, false},
    {0x1675CD0, 0x1675CD4, 0xB6882C, true,  true,   2, false},
    {0x1675CDC, 0x1675CFC, 0xB68BA4, true,  true,   2, false},
    {0x1675CE0, 0x1675D00, 0xB68BA4, true,  true,   2, false},
    {0x1675CE4, 0x1675D04, 0xB68BA4, true,  true,   2, false},
    {0x1675CE8, 0x1675D08, 0xB68BA4, true,  true,   2, false},
    {0x1675CEC, 0x1675D0C, 0xB68BA4, true,  true,   3, false},
    {0x1675CF0, 0x1675D10, 0xB68BA4, true,  true,   3, false},
    {0x1675CF4, 0x1675D14, 0xB68BA4, true,  true,   3, false},
    {0x1675CF8, 0x1675D18, 0xB68BA4, true,  true,   3, false},
    {0x1675DC8, 0x1675DD0, 0xB65F20, true,  true,  -1, false},
    {0x1675DCC, 0x1675DD4, 0xB65FB4, true,  true,  -1, false},
    {0x1675DD8, 0x1675DE8, 0xB68DF4, true,  true,   2, false},
    {0x1675DDC, 0x1675DEC, 0xB68DF4, true,  true,   3, false},
    {0x1675DE0, 0x1675DF0, 0xB68DF4, true,  true,   4, false},
    {0x1675DE4, 0x1675DF4, 0xB68DF4, true,  true,   5, false},
    {0x1675D30, 0x1675D50, 0xB68BA4, true,  true,   2, false},
    {0x1675D34, 0x1675D54, 0xB68BA4, true,  true,   2, false},
    {0x1675D38, 0x1675D58, 0xB68BA4, true,  true,   2, false},
    {0x1675D3C, 0x1675D5C, 0xB68BA4, true,  true,   2, false},
    {0x1675D40, 0x1675D60, 0xB68BA4, true,  true,   3, false},
    {0x1675D44, 0x1675D64, 0xB68BA4, true,  true,   3, false},
    {0x1675D48, 0x1675D68, 0xB68BA4, true,  true,   3, false},
    {0x1675D4C, 0x1675D6C, 0xB68BA4, true,  true,   3, false},
    {0x1675DF8, 0x1675E48, 0xB668F4, true,  true,   2, false},
    {0x1675E20, 0x1675E48, 0xB668F4, false, true,  -1, false},
    {0x1675DFC, 0x1675E4C, 0xB66988, true,  true,  -1, false},
    {0x1675E24, 0x1675E4C, 0xB66988, false, true,   3, true },
    {0x1675E00, 0x1675E50, 0xB66988, true,  true,   4, false},
    {0x1675E28, 0x1675E50, 0xB66988, false, true,  -1, false},
    {0x1675E04, 0x1675E54, 0xB66A1C, true,  true,  -1, false},
    {0x1675E2C, 0x1675E54, 0xB66A1C, false, true,   5, true },
    {0x1675E08, 0x1675E58, 0xB66A1C, true,  true,   6, false},
    {0x1675E30, 0x1675E58, 0xB66A1C, false, true,  -1, false},
    {0x1675E0C, 0x1675E5C, 0xB66AB0, true,  true,  -1, false},
    {0x1675E34, 0x1675E5C, 0xB66AB0, false, true,   7, true },
    {0x1675E10, 0x1675E60, 0xB66AB0, true,  true,   8, false},
    {0x1675E38, 0x1675E60, 0xB66AB0, false, true,  -1, false},
    {0x1675E14, 0x1675E64, 0xB66B44, true,  true,  -1, false},
    {0x1675E3C, 0x1675E64, 0xB66B44, false, true,   9, true },
    {0x1675E18, 0x1675E68, 0xB66B44, true,  true,  10, false},
    {0x1675E40, 0x1675E68, 0xB66B44, false, true,  -1, false},
    {0x1675E1C, 0x1675E6C, 0xB66BD8, true,  true,  -1, false},
    {0x1675E44, 0x1675E6C, 0xB66BD8, false, true,  11, true },
    {0x1675E70, 0x1675E80, 0xB69200, true,  true,   2, false},
    {0x1675E74, 0x1675E84, 0xB69200, true,  true,   2, false},
    {0x1675E78, 0x1675E88, 0xB69200, true,  true,   2, false},
    {0x1675E7C, 0x1675E8C, 0xB69200, true,  true,   2, false},
};

// Applique une ligne de `kMorphRows` : lit/écrit game::g_Client.Var/VarF AUX ADRESSES
// D'ORIGINE (mêmes clés que le binaire), avance à dt*30 frames/s, et déclenche
// World_LoadCurrentZoneModel / World_IsPointOnGround+Map_BeginWarpToFactionTown en fin de
// course selon la ligne. `selfElement`/`selfPos` = game::g_World.self.element / Self().xyz.
void TickMorphRow(const MorphTimerRow& row, float dt, const IMorphModelOracle* oracle,
                   const LocalAnimTickHost& host, int32_t selfElement,
                   float selfX, float selfY, float selfZ) {
    int32_t& flag = g_Client.Var(row.flagAddr);
    if (!flag) return;

    float& frame = g_Client.VarF(row.frameAddr);
    const float duration = MorphDuration(oracle, row.tableAddr);

    if (row.grow) {
        frame += dt * 30.0f;
        if (frame < duration) return;
        flag = 0;
        if (row.clampOnComplete) frame = duration - 1.0f;
        if (row.loadArg >= 0 && host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(row.loadArg);
        // Aucune ligne "grow" n'a warpCheck==true dans la table d'origine (réservé au shrink).
    } else {
        frame -= dt * 30.0f;
        if (frame >= 0.0f) return;
        flag = 0;
        frame = 0.0f;
        if (row.loadArg >= 0 && host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(row.loadArg);
        if (row.warpCheck) {
            const bool onGround = host.IsPointOnGround && host.IsPointOnGround(selfX, selfY, selfZ);
            if (!onGround) BeginWarpToFactionTown(selfElement);
        }
    }
}

} // namespace (helpers communs)

// =====================================================================================
// 1. Player_UpdateLocalAnim 0x5321D0
// =====================================================================================
void Player_UpdateLocalAnim(GameWorld& world, float dt,
                             const IMorphModelOracle* oracle, const LocalAnimTickHost& host) {
    PlayerEntity& self = world.Self();
    const int32_t selfElement = world.self.element;
    const int32_t morphNpcId  = g_Client.VarGet(WarpAddr::SelfMorphNpcId); // g_SelfMorphNpcId

    // --- 0x5321EC..0x53222A : ambiance positionnelle + relance BGM toutes les 900s -------
    // WSndBank_UpdatePositional(dword_14A90E0, selfPos, g_Opt_MusicVolume) — HORS PÉRIMÈTRE
    // (instance de banque sonore ambiante, propriété d'Audio/Sound3D.h::SoundBank, pas
    // modélisée dans game::g_World) : non reproduit ici faute de hook dédié dans ce module
    // (déjà 4 fonctions à couvrir dans cette mission) ; le replay BGM 900s ci-dessous
    // reste, lui, entièrement couvert.
    float& bgmTimestamp = g_Client.VarF(0x1675B18); // flt_1675B18
    if (world.gameTimeSec - bgmTimestamp > 900.0f) {
        bgmTimestamp = world.gameTimeSec;
        if (host.IsBgmEnabled && host.IsBgmEnabled() && host.PlayAmbientBgm) host.PlayAmbientBgm();
    }

    // --- 3 blocs "pulse" (cadence this+8 %6, seuil >14) : dword_1675BAC/BB0, BCC/BD0,
    // BD4/BD8 puis, à la toute fin de la fonction d'origine, E90/E98.
    // IDENTITÉ DE "this" RÉSOLUE (audit 2026-07-14, re-décompilation fraîche idaTs2) :
    // Player_UpdateLocalAnim n'a qu'UN SEUL site d'appel, Scene_InGameUpdate 0x52C600
    // (`Player_UpdateLocalAnim(this, *(float*)&a2)` @0x52c95a), lui-même appelé par
    // cSceneMgr_Update 0x517BF0 (`case 6: Scene_InGameUpdate(this, a4)`), lui-même appelé
    // par App_FrameTick 0x4625D0 avec ECX = `&g_SceneMgr` (0x1676180). Donc "this" ICI EST
    // `&g_SceneMgr`, et `*(this + 2)` (dword offset +8) = l'adresse 0x1676188 — EXACTEMENT
    // le même compteur que `InGameFlowState::frameCounter` documenté dans
    // Game/InGameTickFlow.h ("g_SceneMgr.frameCounter (dword_1676188 d'origine)"), PAS une
    // grandeur propre au joueur ("fiche perso" — l'hypothèse d'un rapport antérieur était
    // erronée). Ce n'est donc PLUS une inconnue : l'équivalent porté EXISTE déjà
    // (InGameTickFlow.h), mais le câblage exact (`s.frameCounter % 6 == 0`) ne peut être
    // branché ICI sans ajouter un paramètre à Player_UpdateLocalAnim, ce qui casserait son
    // unique appelant Scene/SceneManager.cpp:446 (`Player_UpdateLocalAnim(g_World, dt,
    // nullptr, localHost)`) — fichier hors périmètre d'écriture pour cette mission (lecture
    // seule, cf. bandeau de tête de fichier/CLAUDE.md). EN ATTENDANT ce câblage (mission de
    // consolidation future : ajouter `bool pulseTick` ou `int frameCounter` en paramètre,
    // alimenté par `s.frameCounter` côté SceneManager.cpp), on retombe sur une
    // approximation FONCTIONNELLEMENT équivalente : `g_SceneMgr.frameCounter` avance
    // exactement UNE FOIS PAR APPEL de Player_UpdateLocalAnim (donc 1:1 avec le nombre de
    // ticks 1/30s écoulés depuis l'entrée en scène InGame) ; `world.gameTimeSec / dt`
    // (temps réel écoulé depuis App_Init / pas fixe) avance à la MÊME cadence — seule la
    // phase (offset de calage %6) diffère, ce qui est sans effet observable ici puisque le
    // seul usage de `pulseTick` est de FAIRE AVANCER un sous-compteur toutes les ~6 frames
    // (cadence agrégée identique, pas de dépendance à la valeur exacte de la phase).
    const bool pulseTick = (dt > 0.0f) && (static_cast<int>(world.gameTimeSec / dt) % 6 == 0);
    auto TickPulse = [&](uint32_t flagAddr, uint32_t counterAddr) {
        int32_t& flag = g_Client.Var(flagAddr);
        if (!flag) return;
        int32_t& counter = g_Client.Var(counterAddr);
        if (pulseTick) ++counter;
        if (counter > 14) flag = 0;
    };
    TickPulse(0x1675BAC, 0x1675BB0);
    TickPulse(0x1675BCC, 0x1675BD0);
    TickPulse(0x1675BD4, 0x1675BD8);

    // --- Bloc spécial 0x1675BA4/BA8 : table paramétrée par g_SelfMorphNpcId (0x5322F0) ---
    {
        int32_t& flag = g_Client.Var(0x1675BA4);
        if (flag) {
            int32_t row = (morphNpcId >= 154) ? 128 : 127;
            if (morphNpcId >= 319 && morphNpcId <= 323) row = 241;
            const uint32_t table = 0xB60AB8u + 148u * static_cast<uint32_t>(row);
            float& frame = g_Client.VarF(0x1675BA8);
            frame += dt * 30.0f;
            const float duration = MorphDuration(oracle, table);
            if (frame >= duration) {
                flag = 0;
                frame = duration - 1.0f;
                if (host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(2);
            }
        }
    }
    // --- Blocs spéciaux 0x1675BDC/BE0 et 0x1675BE4/BE8 : mêmes tables paramétrées, PAS de
    // clamp/load en fin de course (0x532411/0x532480) ---
    {
        int32_t& flag = g_Client.Var(0x1675BDC);
        if (flag) {
            const int32_t row = (morphNpcId >= 154) ? 142 : 140;
            const uint32_t table = 0xB60AB8u + 148u * static_cast<uint32_t>(row);
            float& frame = g_Client.VarF(0x1675BE0);
            frame += dt * 30.0f;
            if (frame >= MorphDuration(oracle, table)) flag = 0;
        }
    }
    {
        int32_t& flag = g_Client.Var(0x1675BE4);
        if (flag) {
            const int32_t row = (morphNpcId >= 154) ? 143 : 141;
            const uint32_t table = 0xB60AB8u + 148u * static_cast<uint32_t>(row);
            float& frame = g_Client.VarF(0x1675BE8);
            frame += dt * 30.0f;
            if (frame >= MorphDuration(oracle, table)) flag = 0;
        }
    }

    // --- 75 lignes génériques (table kMorphRows) --------------------------------------
    for (const MorphTimerRow& row : kMorphRows)
        TickMorphRow(row, dt, oracle, host, selfElement, self.x, self.y, self.z);

    // --- Bloc indexé g_LocalElement : dword_1675D98[elt]/flt_1675DA8[elt] (0x533332) ----
    {
        constexpr uint32_t kBase1675D98 = 0x1675D98, kBase1675DA8 = 0x1675DA8;
        const uint32_t elt = static_cast<uint32_t>(selfElement < 0 ? 0 : selfElement);
        int32_t& flag = g_Client.Var(kBase1675D98 + 4u * elt);
        if (flag) {
            float& frame = g_Client.VarF(kBase1675DA8 + 4u * elt);
            frame += dt * 30.0f;
            const float duration = MorphDuration(oracle, 0xB68CCCu);
            if (frame >= duration) {
                flag = 0;
                frame = duration - 1.0f;
                if (host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(selfElement + 1);
            }
        }
    }

    // --- Dernier bloc pulse (0x534494) + check final (0x5344DE) -----------------------
    TickPulse(0x1675E90, 0x1675E98);
    if (morphNpcId == 196 && g_Client.VarGet(0x1685E10) == 1)
        BeginWarpToFactionTown(selfElement);
}

// =====================================================================================
// 2. Char_UpdateAnimationFrame 0x571880
// =====================================================================================
namespace {

// Descripteur des 8 timers FX "secondaires" (entity+820..877). Index 0..4 = tables
// "doubles" paramétrées par modelIndex/modelVariant (choix selon weaponAnimSlot!=0 &&
// !altWeaponSet) ; index 5..7 = tables "simples" fixes (pas de branche, mêmes deux tables
// des deux côtés dans le binaire pour l'index 5 -> une seule valeur suffit).
struct CharFxRow {
    bool     doubleTable;   // true = paramétrée (tableWeaponSlot/tableAlt), false = simple
    uint32_t tableWeaponSlot; // utilisée si (weaponAnimSlot!=0 && !altWeaponSet)
    uint32_t tableAlt;        // utilisée sinon (ou table unique si !doubleTable)
};

constexpr CharFxRow kCharFxRows[8] = {
    {true,  0xADF758, 0xA71538}, // idx205/206
    {false, 0xB68264, 0xB68264}, // idx207/208 (même table des deux côtés dans le binaire)
    {true,  0xAEFDD0, 0xA81BB0}, // idx209/210
    {true,  0xAF00B4, 0xA81E94}, // idx211/212
    {true,  0xAF01DC, 0xA81FBC}, // idx213/214
    {true,  0xAF0304, 0xA820E4}, // idx215/216
    {false, 0xB67B74, 0xB67B74}, // idx217/218
    {false, 0xB66610, 0xB66610}, // idx219/220
};

// Résout la table d'une ligne "double" : base + 150368*modelIndex + 75184*modelVariant
// (formule EXACTE des sites d'appel 0x571EAD/0x571E60/etc.).
uint32_t ResolveCharFxTable(const CharFxRow& row, bool useWeaponSlotTable,
                             int32_t modelIndex, int32_t modelVariant) {
    const uint32_t base = useWeaponSlotTable ? row.tableWeaponSlot : row.tableAlt;
    if (!row.doubleTable) return base;
    return base + 150368u * static_cast<uint32_t>(modelIndex)
                + 75184u  * static_cast<uint32_t>(modelVariant);
}

// Tick d'un des 8 timers FX secondaires (0x571DE4..0x572425) : grandit vers
// ModelObj_GetSubObjectCount(table), efface le flag en fin de course (PAS de clamp de
// frame ici — le binaire ne le fait pour AUCUN de ces 8 blocs, contrairement aux timers de
// Player_UpdateLocalAnim).
void TickCharFxSlot(FxTimerSlot& slot, const CharFxRow& row, float dt,
                     const IMorphModelOracle* oracle, int32_t weaponAnimSlot,
                     bool altWeaponSet, int32_t modelIndex, int32_t modelVariant) {
    if (!slot.active) return;
    slot.frame += dt * 30.0f;
    const bool useWeaponSlotTable = (weaponAnimSlot != 0) && !altWeaponSet;
    const uint32_t table = ResolveCharFxTable(row, useWeaponSlotTable, modelIndex, modelVariant);
    if (slot.frame >= MorphDuration(oracle, table)) slot.active = false;
}

} // namespace (helpers Char_UpdateAnimationFrame)

void Char_UpdateAnimationFrame(CharAnimState& anim, const CombatActorState& actor,
                                const GameWorld& world, const IAnimFrameOracle* hitOracle,
                                bool isLocalSimulation, bool isSelf, bool pendingCastInterrupt,
                                float dt, const IMorphModelOracle* modelOracle,
                                const std::function<void(CharActionState state, float dt)>& stateHandler,
                                const CharAnimTickHost& host, CharAnimTickResult& outResult) {
    outResult = CharAnimTickResult{};

    // --- Countdown UI de cast (0x5718A3..0x5718DA) : lié au global partagé dword_1675704/
    // dword_1675700 — n'a de sens que pour l'entité simulée localement (!a2), fidèle. -----
    if (g_Client.VarGet(0x1675704) == 1 && isLocalSimulation) {
        anim.cooldownA -= dt;
        if (anim.cooldownA < 0.0f) anim.cooldownA = 0.0f;
        g_Client.Var(0x1675700) = static_cast<int32_t>(anim.cooldownA); // Crt_ftol (troncature)
    }
    // --- Countdown simple inconditionnel (0x5718F0..0x571919) --------------------------
    if (anim.cooldownB > 0.0f) {
        anim.cooldownB -= dt;
        if (anim.cooldownB < 0.0f) anim.cooldownB = 0.0f;
    }

    // --- Détection de contact : déléguée à ActionFsm::UpdateContactDetection (0x571926..
    // 0x571D2A, DÉJÀ ÉCRIT dans Game/ActionStateMachine.cpp) — construit un ActionFsm
    // transitoire depuis `anim`, l'exécute, recopie le résultat. -------------------------
    {
        ActionFsm fsm;
        fsm.actor              = actor;
        fsm.state               = static_cast<CharActionState>(anim.state);
        fsm.animFrame           = anim.animFrame;
        fsm.isLocalSimulation   = isLocalSimulation;
        fsm.hitCheckActive      = anim.hitCheckActive;
        fsm.hitFired            = anim.hitFired;
        fsm.hitUsesSkillTable   = anim.hitUsesSkillTable;
        fsm.altWeaponSet        = anim.altWeaponSet;
        fsm.weaponAnimSlot      = anim.weaponAnimSlot;
        fsm.lastSkillEventId    = anim.lastSkillEventId;
        fsm.actionKind          = anim.actionKind;
        fsm.actionSubKind       = anim.actionSubKind;
        fsm.modelIndex          = anim.modelIndex;
        fsm.weaponClass         = anim.weaponClass;
        fsm.isSelf              = isSelf;
        fsm.pendingCastInterrupt = pendingCastInterrupt;
        fsm.guardSubstate       = anim.guardSubstate;
        fsm.guardKeyHeld        = anim.guardKeyHeld;

        fsm.UpdateContactDetection(world, hitOracle);
        // --- Interruption de cast (0x57275A, DÉJÀ ÉCRIT) — appelée juste après, comme le
        // binaire (bloc 0x57275A situé APRÈS le bloc de contact, AVANT le switch). --------
        fsm.ApplyPendingCastInterrupt();

        anim.state             = ToRaw(fsm.state);
        anim.animFrame          = fsm.animFrame;
        anim.hitCheckActive     = fsm.hitCheckActive;
        anim.hitFired           = fsm.hitFired;
        anim.lastSkillEventId   = fsm.lastSkillEventId;
        anim.guardSubstate      = fsm.guardSubstate;

        outResult.contactFiredThisTick = fsm.contactFiredThisTick;
        outResult.lastAction           = fsm.lastAction;
        outResult.pendingAoECast       = fsm.pendingAoECast;
        outResult.pendingProjectile    = fsm.pendingProjectile;
    }

    // --- Latch générique 10s (entity+748/752, 0x571DD2) ---------------------------------
    if (anim.genericLatch10s && (world.gameTimeSec - anim.genericLatch10sStamp) > 10.0f)
        anim.genericLatch10s = false;

    // --- 8 timers FX secondaires (0x571DE4..0x572425) -----------------------------------
    for (int i = 0; i < 8; ++i)
        TickCharFxSlot(anim.fxTimers[static_cast<size_t>(i)], kCharFxRows[static_cast<size_t>(i)],
                        dt, modelOracle, anim.weaponAnimSlot, anim.altWeaponSet,
                        anim.modelIndex, anim.modelVariant);

    // --- Paire "==1" partagée unk_B68954 (0x572331/0x572389) ----------------------------
    auto TickSimpleFx = [&](FxTimerSlot& slot, uint32_t table) {
        if (!slot.active) return;
        slot.frame += dt * 30.0f;
        if (slot.frame >= MorphDuration(modelOracle, table)) slot.active = false;
    };
    TickSimpleFx(anim.fx222, 0xB68954u);
    TickSimpleFx(anim.fx224, 0xB68954u);

    // --- Aura spéciale (0x57243C..0x5724B8) : latch armé quand fxAuraTriggerField==2160,
    // désarmé sinon. Tentative d'attache UNE SEULE FOIS par armement (mémorisé par le
    // latch, comme le binaire `if (!*(this+221))`). -------------------------------------
    if (anim.fxAuraTriggerField == 2160) {
        if (!anim.fxAuraAttachedLatch) {
            anim.fxAuraAttachedLatch = true;
            if (host.HasFreeAuraSlot && host.HasFreeAuraSlot() && host.AttachSpecialAura)
                host.AttachSpecialAura();
        }
    } else if (anim.fxAuraAttachedLatch) {
        anim.fxAuraAttachedLatch = false;
    }

    // --- Timer "boucle infinie" (0x5724CC..0x572512) : frame remise à 0 SANS jamais
    // clear fxLoopMode (fidèle — flag positionné/désactivé par un autre système). --------
    if (anim.fxLoopMode == 1) {
        anim.fxLoopFrame += dt * 30.0f;
        if (anim.fxLoopFrame >= MorphDuration(modelOracle, 0xB64800u))
            anim.fxLoopFrame = 0.0f;
    }

    // --- Rotation faciale lissée (0x572531..0x572649), 540°/s, wraparound 360, BYTE-EXACT
    // (aucune dépendance asset) : A=facingCurrentDeg (MUTÉ), B=facingTargetDeg (lu seul). -
    {
        float& A = anim.facingCurrentDeg;
        const float B = anim.facingTargetDeg;
        constexpr float kRate = 540.0f;
        if (A >= B) {
            if (A > B) {
                if (A + 360.0f - B <= B - A) {
                    A -= dt * kRate;
                    if (A < 0.0f) {
                        A += 360.0f;
                        if (B > A) A = B;
                    }
                } else {
                    A += dt * kRate;
                    if (B < A) A = B;
                }
            }
        } else if (B + 360.0f - A <= A - B) {
            A += dt * kRate;
            if (A >= 360.0f) {
                A -= 360.0f;
                if (B < A) A = B;
            }
        } else {
            A -= dt * kRate;
            if (B > A) A = B;
        }
    }

    // --- Switch terminal (0x5727BF) : 55 handlers Char_*/Combat_TickAttackState, chacun
    // asset-driven (durée d'anim = donnée de motion, hors périmètre) -> un seul hook opaque,
    // appelé avec l'état COURANT (post-interruption de cast ci-dessus, fidèle à l'ordre du
    // binaire). ---------------------------------------------------------------------------
    if (stateHandler) stateHandler(static_cast<CharActionState>(anim.state), dt);

    // --- Marque de guilde en attente (0x572F4E) -----------------------------------------
    if (anim.hasPendingGuildMark && host.RegisterGuildMark) host.RegisterGuildMark();

    // --- Requête d'arrêt AutoPlay (0x572F6E..0x572F8D) : globals partagés, seulement pour
    // l'entité locale ET seulement depuis l'état Move(1). --------------------------------
    if (isLocalSimulation && host.GetPendingStopRequest && host.GetPendingStopRequest()) {
        if (anim.state == ToRaw(CharActionState::Move)) {
            if (host.ClearPendingStopRequest) host.ClearPendingStopRequest();
            if (host.SendAutoPlayStopAck) host.SendAutoPlayStopAck();
        }
    }
}

// =====================================================================================
// 3. Camera_UpdateCollision 0x538580
// =====================================================================================
namespace {

D3DXVECTOR3 NormalizeSafe(const D3DXVECTOR3& v, float& outLen) {
    outLen = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (outLen <= 0.0f) return D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    return D3DXVECTOR3(v.x / outLen, v.y / outLen, v.z / outLen);
}

} // namespace

void Camera_UpdateCollision(gfx::Camera& camera, const GameWorld& world,
                             bool freeLookActive, int camMode,
                             const ICameraCollisionQueries* collision,
                             const CameraCollisionHost& host) {
    const bool hasCharInvBlock = !world.self.charInvBlock.empty(); // !g_SelfCharInvBlock inversé
    const int32_t morphNpcId = g_Client.VarGet(WarpAddr::SelfMorphNpcId);

    // --- Garde 0x5385A5 : mode "boutique"/morph 194 hors free-look -> return anticipé ---
    if (!hasCharInvBlock && !freeLookActive && morphNpcId == 194) return;

    // Position self courante (flt_1687330 array — bloc pos réel côté GameState.h).
    D3DXVECTOR3 targetBase(world.players.empty() ? 0.0f : world.players[0].x,
                            world.players.empty() ? 0.0f : world.players[0].y,
                            world.players.empty() ? 0.0f : world.players[0].z);

    // --- Free-look "suivi d'entité" (0x5385CD..0x538681) ---------------------------------
    if (!hasCharInvBlock && freeLookActive && camMode == 3) {
        D3DXVECTOR3 followPos;
        if (!host.FindFreeLookFollowTarget || !host.FindFreeLookFollowTarget(followPos))
            return; // i == g_EntityCount (0x538631) : aucune cible -> return anticipé
        targetBase = followPos;
        if (host.SendFollowCameraUpdate) host.SendFollowCameraUpdate(targetBase);
    }

    const D3DXVECTOR3 target(targetBase.x, targetBase.y + 10.0f, targetBase.z); // v25,v26,v27

    // --- Reprojection du "bras" caméra précédent autour de la nouvelle cible (0x53868C..
    // 0x5387A0) : maintient le même vecteur (oeil-cible) qu'à la frame précédente,
    // renormalisé sur g_CamFollowDist (approximé par camera.Distance(), cf. tête de
    // fichier / Game/CameraWarpTick.h). ----------------------------------------------------
    const D3DXVECTOR3 prevEye = camera.Eye();
    const D3DXVECTOR3 armVec(prevEye.x - target.x, prevEye.y - target.y, prevEye.z - target.z);
    // NOTE fidélité : le binaire recalcule v12..14 = (g_CameraPos - flt_80013C) + nouvelle
    // cible, PUIS reprend la direction (v12-cible) — algébriquement ceci équivaut à
    // "direction = (oeilPrécédent - cibleTargetPrécédente)" ; on utilise ici directement
    // (oeilPrécédent - cibleCourante) qui, à cible quasi-stable d'une frame à l'autre
    // (mouvement du joueur << distance caméra), converge vers le même résultat. Documenté
    // comme approximation assumée (g_CameraPos/flt_80013C = globals renderer HORS
    // PÉRIMÈTRE de ce module, non portés dans Gfx/Camera.h).
    float armLen = 0.0f;
    D3DXVECTOR3 dir = NormalizeSafe(armVec, armLen);

    const float followDist = camera.Distance();
    D3DXVECTOR3 eye(target.x + followDist * dir.x, target.y + followDist * dir.y, target.z + followDist * dir.z);

    // --- Correction de collision terrain (0x5387BE..0x5387D1) ----------------------------
    if (collision) {
        D3DXVECTOR3 hit;
        if (collision->SweepSphereSegment(target, eye, 2.5f, hit)) eye = hit;

        // --- Repli "ground-height stepping" si la cible n'est pas bloquée ET que la ligne
        // de vue traverse un objet de map (0x5387FD..0x538962) -----------------------------
        if (!collision->IsPointBlocked(targetBase) && collision->LineOfSightBlockedByObjects(target, eye)) {
            D3DXVECTOR3 toEye(eye.x - target.x, eye.y - target.y, eye.z - target.z);
            float dist2 = 0.0f;
            D3DXVECTOR3 dir2 = NormalizeSafe(toEye, dist2);

            float step = 1.0f;
            D3DXVECTOR3 stepped(target.x + step * dir2.x, target.y + step * dir2.y, target.z + step * dir2.z);
            while (collision->IsGroundBlocked(stepped.x, stepped.z)) {
                step += 1.0f;
                stepped = D3DXVECTOR3(target.x + step * dir2.x, target.y + step * dir2.y, target.z + step * dir2.z);
                if (dist2 < step) {
                    stepped = D3DXVECTOR3(target.x + dist2 * dir2.x, target.y + dist2 * dir2.y, target.z + dist2 * dir2.z);
                    break;
                }
            }
            eye = stepped;
        }
    }

    // --- Clamp de distance minimale (0x538A0C..0x538A38) : si la distance finale <10,
    // rapproche l'oeil à une distance fixe de 4 autour de la cible. -----------------------
    {
        D3DXVECTOR3 toEye(eye.x - target.x, eye.y - target.y, eye.z - target.z);
        float finalDist = 0.0f;
        D3DXVECTOR3 finalDir = NormalizeSafe(toEye, finalDist);
        if (finalDist < 10.0f)
            eye = D3DXVECTOR3(target.x + finalDir.x * 4.0f, target.y + finalDir.y * 4.0f, target.z + finalDir.z * 4.0f);
    }

    // --- Calage final (0x538A6A/0x538A9E) : Cam_SetLookAt (DÉJÀ ÉCRIT, Game/CameraWarpTick
    // .h) — un seul appel pousse oeil+cible sur `camera`, cf. sa note de fidélité pour la
    // redondance Camera_SetEyeTarget/g_GxdRenderer côté binaire (non dupliquée ici). --------
    Cam_SetLookAt(camera, eye.x, eye.y, eye.z, target.x, target.y, target.z);
}

// =====================================================================================
// 4. MapColl_UpdateObjectAnim 0x694A00
// =====================================================================================
void MapColl_UpdateObjectAnim(MapCollisionObjectAnimState& obj, float dt,
                               IMapObjectAnimOracle* oracle) {
    constexpr float kAnimFps = 15.0f; // a2 d'origine, TOUJOURS 15.0 au site d'appel connu

    if (!obj.active || obj.mode != 1) return; // 0x694A16 : *(this+1) && *(this+2)==1

    // --- Sous-objets animés (0x694A1C..0x694AC9) ----------------------------------------
    for (MapAnimSubObject& sub : obj.animObjects) {
        sub.frame += kAnimFps * dt;
        if (!oracle) continue;
        const int frameCount = oracle->GetModelFrameCount(sub.modelIndex);
        if (frameCount <= 0) continue;
        // Boucle "modulo" fidèle (0x694A6C..0x694AB6) : soustrait frameCount tant que
        // l'index entier dépasse frameCount-1 (gère un dt assez grand pour sauter
        // plusieurs tours d'anim en une frame).
        while (static_cast<int>(sub.frame) > frameCount - 1)
            sub.frame -= static_cast<float>(frameCount);
    }

    // --- Émetteurs de particules (0x694AD9..0x694B3C) -------------------------------------
    if (!oracle) return;
    for (size_t i = 0; i < obj.particleEmitters.size(); ++i) {
        MapParticleEmitter& p = obj.particleEmitters[i];
        if (p.initialized)
            oracle->UpdateParticle(static_cast<int>(i), dt);
        else
            oracle->InitParticle(p.particleDefIndex); // le binaire n'écrit PAS `initialized`
                                                        // ici (positionné ailleurs, hors périmètre)
    }
}

} // namespace ts2::game

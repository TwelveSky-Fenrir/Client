// Game/MiscManagers.cpp — implémentation des 5 managers divers (voir MiscManagers.h
// pour la table de correspondance EA <-> fonction et le résumé de comportement).
#include "Game/MiscManagers.h"
#include "Game/GameState.h"
#include "Core/Log.h"
#include <cmath>
#include <limits>

namespace ts2::game {

// ===========================================================================
// mPOINTER — CursorSet::LoadResources — fidèle à CursorSet_LoadResources 0x4C0FA0.
// ===========================================================================
// Décompilation d'origine (this = &unk_8E714C) :
//   *this = 0;
//   *(this+1) = LoadCursorA(hInstance, (LPCSTR)0x66);
//   *(this+2) = LoadCursorA(hInstance, (LPCSTR)0x67);
//   *(this+3) = LoadCursorA(hInstance, (LPCSTR)0x68);
//   *(this+4) = LoadCursorA(hInstance, (LPCSTR)0x69);
//   *(this+5) = LoadCursorA(hInstance, (LPCSTR)0x6A);
//   *(this+6) = LoadCursorA(hInstance, (LPCSTR)0x6B);
//   *(this+7) = LoadCursorA(hInstance, (LPCSTR)0x6C);
//   *(this+8) = LoadCursorA(hInstance, (LPCSTR)0x75);
//   *(this+9) = LoadCursorA(hInstance, (LPCSTR)0x77);
//   for (i = 0; i < 9; ++i) if (!*(this+i+1)) return 0;
//   return 1;
// hInstance est le HINSTANCE global du module (`hInstance` @ 0x815578, posé
// par WinMain 0x4609C0). Les 9 IDs de ressource (0x66..0x6C, 0x75, 0x77) sont
// des RT_GROUP_CURSOR embarqués dans le .exe d'origine (pas des fichiers).
bool CursorSet::LoadResources(HINSTANCE hInstance) {
    state = 0;

    slot66 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x66));
    slot67 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x67));
    slot68 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x68));
    slot69 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x69));
    slot6A = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x6A));
    slot6B = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x6B));
    slot6C = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x6C));
    slot75 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x75));
    slot77 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x77));

    const HCURSOR* const all[9] = { &slot66, &slot67, &slot68, &slot69,
                                     &slot6A, &slot6B, &slot6C, &slot75, &slot77 };
    for (const HCURSOR* h : all) {
        if (*h == nullptr) {
            TS2_ERR("CursorSet::LoadResources : une ressource RT_GROUP_CURSOR "
                     "manque dans le .rc (l'original chargeait ids 0x66..0x6C,0x75,0x77)");
            return false;
        }
    }
    return true;
}

// CursorSet_DestroyAll 0x4C10B0 (App_Shutdown, mPOINTER teardown) — voir MiscManagers.h.
void CursorSet::DestroyAll() {
    state = 0;
    HCURSOR* const all[9] = { &slot66, &slot67, &slot68, &slot69,
                               &slot6A, &slot6B, &slot6C, &slot75, &slot77 };
    for (HCURSOR* h : all) {
        if (*h) {
            DestroyIcon(reinterpret_cast<HICON>(*h));
            *h = nullptr;
        }
    }
}

// CursorSet::AnimateTick — fidèle à Cursor_AnimateTick 0x4C1140 (voir MiscManagers.h
// pour le mécanisme complet). Décompilation d'origine :
//   HCURSOR __thiscall Cursor_AnimateTick(_DWORD *this) {
//       return SetCursor((HCURSOR)*(this + *this + 1));
//   }
// this[0] = state (index actif), this[1..9] = les 9 HCURSOR — indexation
// `this + *this + 1` = this[state + 1], reproduite ci-dessous via le même
// tableau de pointeurs que LoadResources()/DestroyAll(). Aucun clamp ici dans
// l'original : `state` est déjà garanti dans [0,8] par Util_SetClampedU8Field
// côté écriture (SetActiveSlot()) — un clamp défensif est ajouté pour ne
// jamais lire hors tableau si un futur appelant écrivait `state` directement.
HCURSOR CursorSet::AnimateTick() const {
    const HCURSOR* const all[9] = { &slot66, &slot67, &slot68, &slot69,
                                     &slot6A, &slot6B, &slot6C, &slot75, &slot77 };
    const int32_t idx = (state >= 0 && state <= 8) ? state : 0;
    return SetCursor(*all[idx]);
}

// CursorSet::SetActiveSlot — fidèle à Util_SetClampedU8Field 0x4C1110 appliqué à
// dword_8E714C (le champ `state`) : *this = a2 si a2 <= 8, sinon aucun effet.
bool CursorSet::SetActiveSlot(uint32_t idx) {
    if (idx > 8) return false;
    state = static_cast<int32_t>(idx);
    return true;
}

// ===========================================================================
// mMYINFO — Player_ResetAnimState — fidèle à Player_ResetAnimState 0x50F520.
// ===========================================================================
// Décompilation d'origine (this = float* = &g_PlayerCmdController 0x1669170) :
//   *this = 0.0;                       // offset dword 0    (float)
//   *(this+1) = g_GameTimeSec;         // offset dword 1    (float, horodatage)
//   *(this+12870) = 0.0;               // offset dword 12870
//   *(this+13286) = 0.0;               // offset dword 13286
//   *(this+13287) = 0.0;               // offset dword 13287
//   *(this+13288) = 0.0;               // offset dword 13288
//   *(this+13289) = 0.0;               // offset dword 13289
//   *(this+13290) = 0.0;               // offset dword 13290
//   Crt_Memset(this+13291, 0, 20);     // offsets dword 13291..13295 (5 floats)
//   *(this+13314) = NAN;               // offset dword 13314 (sentinelle)
//   return 1;
// Champs épars d'un très gros bloc non encore modélisé dans ClientSource ;
// reproduits ici par index de float pour rester fidèle sans inventer de
// struct. `playerCmdController` doit pointer sur un buffer d'AU MOINS
// 13315 floats (53260 octets) — taille réelle du bloc d'origine inconnue au-
// delà de ce point (aucune autre écriture observée dans cette fonction).
void Player_ResetAnimState(float* playerCmdController, float gameTimeSec) {
    float* const p = playerCmdController;

    p[0]     = 0.0f;
    p[1]     = gameTimeSec;
    p[12870] = 0.0f;
    p[13286] = 0.0f;
    p[13287] = 0.0f;
    p[13288] = 0.0f;
    p[13289] = 0.0f;
    p[13290] = 0.0f;
    p[13291] = 0.0f;
    p[13292] = 0.0f;
    p[13293] = 0.0f;
    p[13294] = 0.0f;
    p[13295] = 0.0f;
    p[13314] = std::numeric_limits<float>::quiet_NaN();
}

// ===========================================================================
// mPLAY — GameData_InitPools — fidèle à cGameData_InitPools 0x5575D0.
// ===========================================================================
// Décompilation d'origine (this = &g_LocalPlayerSheet 0x1685748) :
//   *(this+1717) = 1000;   for (i=0;   i<1000; ++i) sub_55D6F0(this + 227*i + 1723);
//   *(this+1718) = 100;    for (j=0;   j<100;  ++j) sub_57FE50(this +  22*j + 228723);
//   *(this+1719) = 1000;   for (k=0;   k<1000; ++k) sub_580530(this +  70*k + 230923);
//   *(this+1720) = 1000;   for (m=0;   m<1000; ++m) sub_583370(this +  38*m + 300923);
//   *(this+1721) = 1000;   for (n=0;   n<1000; ++n) sub_5841F0(this +  64*n + 338923);
//   *(this+1722) = 500;    for (ii=0;  ii<dword_1687230(=this+1722); ++ii)
//                                          sub_583F50(this +  19*ii + 402923);
//   return 1;
//
// Vérification d'adresse absolue (base = 0x1685748) — CONFIRME que les 6
// pools sont EXACTEMENT les tableaux d'entités documentés dans
// Game/GameState.h (calcul : base + index_dword*4) :
//   pool A  this+1723    -> 0x1687234  == dword_1687234 (joueurs,      stride 908 o / 227 dw) [DataTable: g_World.players,     N=1000]
//   pool B  this+228723  -> 0x1764D14  == dword_1764D14 (objets sol,   stride  88 o /  22 dw) [DataTable: g_World.groundItems, N=100]
//   pool C  this+230923  -> 0x1766F74  == dword_1766F74 (monstres,     stride 280 o /  70 dw) [DataTable: g_World.monsters,    N=1000]
//   pool D  this+300923  -> 0x17AB534  == dword_17AB534 (PNJ,          stride 152 o /  38 dw) [DataTable: g_World.npcs,        N=1000]
//   pool E  this+338923  -> 0x17D06F4  == dword_17D06F4 (projectiles,  stride 256 o /  64 dw), N=1000 (=g_FxAuraCount 0x168722C)
//   pool F  this+402923  -> 0x180EEF4  == dword_180EEF4 (objets zone), stride  76 o /  19 dw,  N=500  (=dword_1687230)
//   (compteur pool E = this+1721 -> 0x168722C == g_FxAuraCount ; compteur pool F = this+1722 -> 0x1687230 == dword_1687230 ;
//    les deux compteurs sont ADJACENTS en mémoire mais gouvernent des pools DISTINCTS)
//
// Identification pools E/F (résolue — mission "aura/objets-de-monde", 2026-07-14) :
//  - Pool E (0x17D06F4, N=1000, compteur g_FxAuraCount) = pool SoA de PROJECTILES
//    D'ATTAQUE, alloué par Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10 (boucle
//    `for (i=0; i<g_FxAuraCount && dword_17D06F4[64*i]; ++i)` = recherche du 1er
//    slot libre, borne = g_FxAuraCount), mis à jour 1x/frame par
//    Fx_HomingProjectileUpdate 0x5862D0 (appelée depuis Scene_InGameUpdate). Pool
//    DÉJÀ CATALOGUÉ en détail dans Docs/TS2_FX_CATALOG.md (~30 tableaux parallèles
//    dword_17D06F4..dword_17D07D4 : état/type/sous-type, ids source/cible, pos
//    départ/cible xyz, vitesse, flag homing, motion d'arme). PAS un pool d'« auras »
//    au sens buff/debuff — le nom de hook GetFxAuraCount (InGameTickFlow.h) désigne
//    en réalité ce pool de projectiles homing ; aucun conteneur GameState.h dédié
//    n'est ajouté ici (déjà documenté ailleurs, cf. mission consigne "pas de nouveau
//    code nécessaire").
//  - Pool F (0x180EEF4, N=500, compteur dword_1687230) = pool d'OBJETS DE ZONE /
//    nœuds de ressource (mine, portail, etc.), peuplé par le handler réseau
//    Pkt_SpawnZoneObject (opcode 0x86, EA 0x4680F0) et lu par World_PickEntityAtCursor
//    0x538AB0, World_IsPositionOccupied 0x541DD0, Scene_PickResourceNodeAtScreen
//    0x541510. Layout confirmé par Docs/TS2_PROTOCOL_SPEC.md ([SC b08]) : stride 19
//    dwords = actif, objId1, objId2, horodatage spawn (float), puis 52 o de données
//    brutes. Pool DISTINCT du pool E malgré la contiguïté mémoire des deux compteurs
//    (0x168722C puis 0x1687230) — voir aussi TS2_SUBSYSTEM_MAP.md ("resource nodes").
//    Modélisé ci-dessous via ZoneObjectEntity (Game/GameState.h, g_World.zoneObjects).
//
// Les petits constructeurs par emplacement (sub_55D6F0/57FE50/580530/583370/
// 5841F0+sub_6A6FE0/583F50) ne font QUE zéro-initialiser 1 à 4 champs par
// emplacement (PAS un memset intégral du slot) — l'équivalent fonctionnel est
// un emplacement "vide/inactif" par défaut, ce que fournissent déjà les
// constructeurs par défaut de PlayerEntity/GroundItem/MonsterEntity/NpcEntity/
// ZoneObjectEntity (active=false, id={0,0}, ...) dans GameState.h. On reproduit
// donc l'effet net (capacité fixe + slots vides) en redimensionnant g_World,
// plutôt qu'en dupliquant un layout mémoire brut que GameState.h a
// délibérément remplacé par des types propres.
//
// Pool E : toujours SANS conteneur ici (cf. identification ci-dessus — le
// pool est déjà modélisé/documenté côté FX, le câblage runtime reste une
// mission séparée). Pool F : conteneur ajouté (g_World.zoneObjects).
bool GameData_InitPools() {
    g_World.players.assign(1000, PlayerEntity{});
    g_World.groundItems.assign(100, GroundItem{});
    g_World.monsters.assign(1000, MonsterEntity{});
    g_World.npcs.assign(1000, NpcEntity{});
    g_World.zoneObjects.assign(500, ZoneObjectEntity{});

    TS2_LOG("GameData_InitPools : pools joueurs=1000 objets_sol=100 monstres=1000 "
            "pnj=1000 objets_zone=500 (pool projectiles 17D06F4/g_FxAuraCount N=1000 "
            "deja catalogue Docs/TS2_FX_CATALOG.md, non modelise ici, cf. commentaire)");
    return true;
}

// cGameData_DestroyPools 0x557780 (App_Shutdown, mPLAY teardown) — voir MiscManagers.h.
// Vide les 5 pools modélisés (miroir exact des 5 pools remplis par GameData_InitPools
// ci-dessus ; pool E "projectiles" 0x17D06F4 non modélisé, cf. commentaire d'InitPools).
// clear() + shrink_to_fit() pour reproduire l'INTENTION "libérer" du binaire (GlobalFree-
// like), plutôt qu'un simple clear() qui garderait la capacité réservée.
bool GameData_DestroyPools() {
    g_World.players.clear();     g_World.players.shrink_to_fit();
    g_World.groundItems.clear(); g_World.groundItems.shrink_to_fit();
    g_World.monsters.clear();    g_World.monsters.shrink_to_fit();
    g_World.npcs.clear();        g_World.npcs.shrink_to_fit();
    g_World.zoneObjects.clear(); g_World.zoneObjects.shrink_to_fit();

    TS2_LOG("GameData_DestroyPools : pools joueurs/objets_sol/monstres/pnj/objets_zone vides.");
    return true;
}

} // namespace ts2::game

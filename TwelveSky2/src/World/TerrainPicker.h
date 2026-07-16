// World/TerrainPicker.h — picking écran -> monde du client TwelveSky2 (réécriture fidèle).
//
// Vague W10, front « picking-terrain ». Comble TROIS gaps prouvés (G-PICK-03/05/06). Vérité
// = le désassemblage de TwelveSky2.exe (imagebase 0x400000) via MCP idaTs2.
//
// Fonctions d'origine reproduites ici :
//   World_PickEntityAtCursor  0x538AB0 -> World_PickEntityAtCursor  (8 catégories de clic)
//   Scene_RayHitPlayerBox     0x5415E0 -> RayHitPlayerBox   (statique .cpp)
//   Scene_RayHitNpcBox        0x541680 -> RayHitNpcBox      (statique .cpp)
//   Scene_RayHitMonsterBox    0x541780 -> RayHitMonsterBox  (statique .cpp)
//   Scene_RayHitNodeBox       0x541920 -> RayHitNodeBox     (statique .cpp)
//   Cam_ScreenRayVsAABB       0x6A0670 -> collision::BuildScreenRay + collision::SegmentAABB
//   Terrain_PickRayScreen     0x699A80 -> TerrainPicker::PickRayScreen (via WorldAssets)
//   World_IsPointBlocked      0x540DA0 -> TerrainPicker::IsPointBlocked (via WorldAssets)
//   Scene_InGameRender @0x530FC7..0x53120B (table de forme de curseur) -> CursorSlotForPickCategory
//
// ---------------------------------------------------------------------------------------
// §0. POURQUOI AUCUNE MATH NOUVELLE N'EST ÉCRITE ICI (preuve arithmétique)
// ---------------------------------------------------------------------------------------
// Les 4 hit-tests d'entité passent TOUS par Cam_ScreenRayVsAABB 0x6A0670, qui lit ses
// paramètres caméra dans le singleton g_GfxRenderer (0x7FFE18) :
//   this+792 -> 0x800130 (= g_CameraPos)  this+648 -> 0x8000A0 (= largeur écran)
//   this+796 -> 0x800134                  this+652 -> 0x8000A4 (= hauteur écran)
//   this+800 -> 0x800138                  this+728 -> 0x8000F0 (= proj._11)
//   this+892 -> 0x800194 (= invView)      this+748 -> 0x800104 (= proj._22)
// Ce sont EXACTEMENT les mêmes octets que les globales lues par Terrain_PickRayScreen
// 0x699A80 (@0x699AA6 g_CameraPos, @0x699AD7 dword_8000A4, @0x699AE7 flt_8000F0,
// @0x699B31 flt_800104, @0x699B40 unk_800194) — vérifié adresse par adresse. Et le corps
// de 0x6A0670 se réduit à : construire le rayon écran (formule IDENTIQUE à
// collision::BuildScreenRay, World/WorldMap.cpp:1139) puis appeler Collide_SegmentAABB
// 0x69FB20 (== collision::SegmentAABB, World/WorldMap.cpp:795).
// => UN SEUL collision::ScreenPickCamera sert les 6 chemins de picking du client.
//
// ---------------------------------------------------------------------------------------
// §1. LES 8 CATÉGORIES DE CLIC (World_PickEntityAtCursor 0x538AB0)
// ---------------------------------------------------------------------------------------
// Signature d'origine : int __stdcall (int sx, int sy, _DWORD* outKind, int* outIndex,
// int allowModifierTargets). Init : *outKind=0 @0x538ABC, *outIndex=-1 @0x538AC5. Le `ecx`
// chargé au site d'appel est MORT (convention stdcall). La valeur de retour (eax) est du
// scratch jamais consommé par l'appelant (le switch @0x530FC7 porte sur outKind) — d'où le
// `bool` de convenance ici.
//   0 = sol/rien (défaut)      4 = PNJ                (boucle @0x538DAC)
//   1 = joueur neutre          5 = monstre            (boucle @0x538E9C)
//   2 = partenaire d'échange   6 = objet au sol       (boucle @0x5390D7) — NON PORTÉ, cf. §4
//   3 = joueur attaquable      7 = objet de zone      (boucle @0x5391A7)
// Le switch de Scene_InGameRender @0x530FB4 (`cmp 7 / ja default`) confirme 8 cas.
//
// PRIORITÉ : la catégorie retenue est la plus PROCHE DE LA CAMÉRA, via
// Math_Dist3D(posEntité, g_CameraPos) et une comparaison STRICTE `v14 > dist` -> à
// distance égale, le PREMIER trouvé gagne. Les catégories 4/5/6 s'écrivent
// `!*outKind || v14 > d`, les 1/2/3/7 en if/else imbriqué : SÉMANTIQUEMENT IDENTIQUES.
//
// GARDES par boucle (toutes re-prouvées au décompilé de 0x538AB0) :
//  - joueurs (i démarre à 1, self exclu ; borne g_EntityCount 0x168721C ; stride 908) :
//      active(+0) && dword_168724C[227i](= body[0], « enregistrement peuplé »)
//      && Char_IsTargetablePlayerState(g_SelfActionState[227i]) (0x558AE0 : state != 12)
//      && Scene_RayHitPlayerBox
//  - PNJ (borne g_NpcCount 0x1687220 ; stride 88) : active(+4) && filtre de portée
//      Math_Dist3D(posNpc, flt_1687330) <= 500.0 @0x538E04 — mesuré contre la position du
//      JOUEUR, pas de la caméra ; SEULE cette boucle a ce filtre.
//  - monstres (borne g_MonsterCount 0x1687224 ; stride 280) : active(+0)
//      && Char_IsTargetableMonsterState(dword_1766F8C[70k]) (0x558B10 : != 12 && != 19)
//      && Scene_RayHitMonsterBox && FILTRE ÉLÉMENTAIRE @0x53905E..0x539083 (cf. §2)
//  - objets de zone (borne g_ZoneObjectCount 0x1687230 ; stride 76) : active(+0)
//      && Scene_RayHitNodeBox
//
// GATING MODIFICATEUR — catégories 1 et 7 UNIQUEMENT (@0x538D07 / @0x539220) :
//   `if (allowModifierTargets) { if (byte_8013FE < 0) { ...retenir... } } else { ...retenir... }`
// Autrement dit : allowModifierTargets==false -> catégorie TOUJOURS éligible ;
//                 allowModifierTargets==true  -> exige la touche modificatrice enfoncée.
// NE PAS coder `eligible = allowModifier && keyDown` (exclurait à tort le cas false).
// Les catégories 2/3/4/5/6 ne sont JAMAIS gatées.
//
// byte_8013FE IDENTIFIÉ (vague W10) = **DIK_LSHIFT (0x2A)**, cf. kModifierDik ci-dessous.
//
// ---------------------------------------------------------------------------------------
// §2. FILTRE ÉLÉMENTAIRE DES MONSTRES (@0x53905E..0x539083) — absent du dossier de gaps
// ---------------------------------------------------------------------------------------
// Pour def = dword_1766FD4[70k] (== MonsterEntity::def, un MONSTER_INFO), si
// def+232 (== MonsterInfo::field232, domaine prouvé [1,15]) vaut 10/11/12/13, le monstre
// n'est ciblable que si les quatre clauses du décompilé tiennent. Elles se réduisent à une
// règle UNIFORME (démonstration : la clause 10 s'écrit `g_LocalElement && g_LocalElement !=
// Paired(0)`, or `g_LocalElement` != 0 EST `g_LocalElement != 0` -> c'est le cas K=0 de la
// forme générale `element != K && element != Paired(K)` des clauses 11/12/13) :
//     K = field232 - 10  (0..3)  ->  ciblable ssi (element != K && element != Paired(K))
// Sémantique : gardiens élémentaires — on ne cible ni celui de son propre élément, ni celui
// de l'élément pairé. Char_GetPairedElement 0x557C00 == ElementPairTable::Paired (vérifié
// bit-à-bit : this[455..458] <-> a/b/c/d, mêmes 4 tests, même repli -1).
//
// ---------------------------------------------------------------------------------------
// §3. CATÉGORIE 2 / CATÉGORIE 3
// ---------------------------------------------------------------------------------------
// Cat 2 @0x538BCC (reproduite LITTÉRALEMENT — cf. note d'ambiguïté sur kRecTradeFlag) :
//   g_TradePartnerIdLo[0]==1 && g_TradePartnerIdLo[227i]==1
//   && dword_1687420[0]==dword_1687420[227i] && dword_1687424[0]!=dword_1687424[227i]
// Cat 3 @0x538C4E : Combat_CanTargetOnMap(g_LocalPlayerSheet, dword_168728C[227i],
//   dword_1687320[227i], &byte_168725C[908i]) — NON PORTÉE (système de zones PVP), exposée
//   en callback hôte, cf. EntityPickHost::CanTargetOnMap.
//
// ---------------------------------------------------------------------------------------
// §4. CE QUI N'EST DÉLIBÉRÉMENT PAS PORTÉ (et pourquoi c'est FIDÈLE)
// ---------------------------------------------------------------------------------------
// CATÉGORIE 6 (objet au sol, Scene_RayHitItemModel 0x5418B0) : non portée. Triple blocage,
// aucun effet observable :
//   (a) le pool source `game::g_World.groundItems` est VIDE PAR CONCEPTION — la structure
//       152 o de dword_17AB534 n'est pas modélisée (Game/GameState.h:605-613 le dit
//       explicitement) ; une boucle sur un vecteur vide ne peut RIEN retenir ;
//   (b) 0x5418B0 n'est PAS un test AABB comme les 4 autres : c'est un test d'OBB via
//       ModelObj_GetBoneMatrix 0x4D7130 + Cam_ScreenRayVsOBB/Collide_SegmentOBB 0x6A0750 —
//       AUCUNE de ces deux briques n'est portée côté C++ ;
//   (c) donc l'omettre produit EXACTEMENT le même résultat observable qu'un pool vide.
// Le jour où (a) tombe, il faudra porter (b) puis rétablir la boucle ENTRE les monstres et
// les objets de zone (l'ordre compte pour la règle « premier trouvé gagne » à égalité).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "Game/GameState.h"    // game::GameWorld
#include "Game/SkillCombat.h"  // game::ITerrainPicker (+ world::CollisionSlot par transitivité)
#include "World/WorldMap.h"    // world::CollisionSlot, world::collision::ScreenPickCamera
#include "World/WorldIntegration.h" // world::WorldAssets

// Déclaration ANTICIPÉE : garde ce header libre de <d3dx9.h> (Gfx/Camera.h est tiré par le
// .cpp seul). `Camera` est bien déclarée `class` dans Gfx/Camera.h:35 -> pas de C4099.
namespace ts2::gfx { class Camera; }

namespace ts2::world {

// ---------------------------------------------------------------------------
// Touche modificatrice de World_PickEntityAtCursor (byte_8013FE, catégories 1 et 7).
//
// IDENTIFIÉE (vague W10) — le dossier de gaps ne disait que « un modificateur clavier ».
// Preuve : Input/InputSystem.h:23-25 établit que le tableau d'état DirectInput du client
// est `BYTE state[256]` à g_GfxRenderer+5564 = **0x8013D4** (rempli par GetDeviceState).
// Donc byte_8013FE == state[0x8013FE - 0x8013D4] == state[0x2A] == DIK_LSHIFT.
// DOUBLE corroboration indépendante par les mappings déjà portés et vérifiés :
//   byte_8013F2 == state[0x1E] == DIK_A (Input/InputSystem.h:95, App/PlayerInputController.cpp:56)
//   byte_8013E5 == state[0x11] == DIK_W (Input/InputSystem.h:92, App/PlayerInputController.cpp:75)
// Les deux tombent juste sur les scancodes DIK standard -> la base 0x8013D4 est certaine.
//
// La constante est posée ICI et non dans Input/InputSystem.h::dik (fichier NON possédé par
// ce front) ; à remonter dans ce namespace lors d'une passe de cohérence.
//
// Convention de test : le binaire fait `byte_8013FE < 0` sur un octet SIGNÉ, c.-à-d. bit 7
// posé == touche enfoncée — strictement équivalent à InputSystem::IsKeyDown (`& 0x80`).
inline constexpr int kModifierDik = 0x2A; // DIK_LSHIFT — byte_8013FE (0x8013D4 + 0x2A)

// ---------------------------------------------------------------------------
// Zones interdisant la catégorie 1 (joueur neutre) — @0x538CFD.
// Le binaire teste g_SelfMorphNpcId 0x1675A98, dont la vague W10 a établi qu'il est l'ID DE
// ZONE COURANT et non un « morph » (cf. le bandeau de Game/SkillCombat.h:33) : ce sont donc
// des zones PVP, où un joueur ne peut jamais être « neutre ».
inline constexpr int kZonesBlockingNeutralPlayer[6] = { 270, 271, 272, 273, 274, 324 };

// ---------------------------------------------------------------------------
// TerrainPicker — IMPLÉMENTEUR de game::ITerrainPicker (G-PICK-06 : l'interface n'en avait
// AUCUN). Ne possède rien : référence les mailles .WM/.WJ déjà décodées par WorldAssets et
// un instantané de caméra. À construire par frame (objet trivial, aucune allocation).
// ---------------------------------------------------------------------------
class TerrainPicker final : public game::ITerrainPicker {
public:
    TerrainPicker(const WorldAssets& assets, const collision::ScreenPickCamera& cam)
        : assets_(&assets), cam_(cam) {}

    // World_IsPointBlocked 0x540DA0 (via WorldAssets, couches Main + WJ).
    bool IsPointBlocked(const float pos[3]) override;

    // Terrain_PickRayScreen 0x699A80 sur la maille `slot`. `twoSide` = 6e arg d'origine.
    bool PickRayScreen(int screenX, int screenY, CollisionSlot slot, bool twoSide,
                        float outPos[3]) override;

private:
    const WorldAssets*          assets_;
    collision::ScreenPickCamera cam_;
};

// ---------------------------------------------------------------------------
// Construit l'instantané caméra commun aux 6 chemins de picking (cf. §0) depuis la caméra
// du moteur et le viewport courant. `screenW`/`screenH` = dword_8000A0/dword_8000A4.
//   eye     <- Camera::Eye()                    (g_CameraPos 0x800130)
//   invView <- inverse(BuildViewMatrix())       (unk_800194)
//   proj11  <- BuildProjMatrix(aspect)._11      (flt_8000F0)
//   proj22  <- BuildProjMatrix(aspect)._22      (flt_800104)
// `aspect` = screenW/screenH, MÊME calcul que Gfx_InitDevice 0x69BFC6 et que
// Scene/WorldRenderer.cpp:556-559.
// ---------------------------------------------------------------------------
collision::ScreenPickCamera BuildScreenPickCamera(const gfx::Camera& camera,
                                                   int screenW, int screenH);

// ---------------------------------------------------------------------------
// Dépendances externes de World_PickEntityAtCursor non portées côté C++.
// ---------------------------------------------------------------------------
struct EntityPickHost {
    // Combat_CanTargetOnMap 0x558740(g_LocalPlayerSheet, element, pkLevel, affiliationName)
    // -> catégorie 3 (joueur attaquable). NON PORTÉE : elle dépend de Map_GetPvpMode
    // 0x4FAB90 + d'un switch de zones (291/138/139/165/166/324/342/270-274/54) et d'un
    // sous-mode dword_16760F4, tout un système de zones PVP hors périmètre de ce front.
    // MÊME signature exacte que game::NameplateHost::CanTargetOnMap (Game/NameplateLogic.h:268)
    // -> l'hôte peut brancher LE MÊME lambda sur les deux.
    // Défaut (non branché) : false -> aucun joueur n'est jamais classé « attaquable » et
    // retombe en catégorie 1 (neutre). Dégradation HONNÊTE et déjà assumée telle quelle par
    // NameplateLogic ; à lever quand le système de zones PVP sera porté.
    std::function<bool(int element, int pkLevel, const std::string& affiliationName)> CanTargetOnMap;
};

// ---------------------------------------------------------------------------
// World_PickEntityAtCursor 0x538AB0 — cf. §1..§4 du bandeau.
//
// `cam`                  : instantané caméra (BuildScreenPickCamera).
// `allowModifierTargets` : a5 d'origine = (GameOptions::ShowHitMarkers ? 1 : 0), cf. les
//                          deux sites d'appel @0x530F7E (0) / @0x530FA6 (1).
// `modifierKeyDown`      : byte_8013FE < 0, c.-à-d. input.IsKeyDown(kModifierDik).
// `outKind` (0..7) / `outIndex` (-1 si aucun) : *a3 / *a4 d'origine, TOUJOURS écrits.
// Retour : convenance == (outKind != 0). L'eax d'origine est du scratch non consommé.
// ---------------------------------------------------------------------------
bool World_PickEntityAtCursor(const game::GameWorld& world,
                               const collision::ScreenPickCamera& cam,
                               int screenX, int screenY,
                               bool allowModifierTargets, bool modifierKeyDown,
                               const EntityPickHost& host,
                               int& outKind, int& outIndex);

// ---------------------------------------------------------------------------
// Forme de curseur associée à une catégorie de survol — table EXACTE du switch de
// Scene_InGameRender @0x530FC7..0x53120B (arguments passés à Util_SetClampedU8Field
// 0x4C1110, cible unique dword_8E714C == game::CursorSet).
//
//   blink = ((int)(2.0f * g_GameTimeSec)) % 2   -- idiome SIGNÉ du binaire
//           (`fadd st,st` / Crt_ftol / `and eax,80000001h` / `jns` / `dec` /
//            `or eax,0FFFFFFFEh` / `inc`), alternance à 2 Hz.
//
//   cat 0 : 7 si canCastAtCursor sinon 8       (@0x530FEA / @0x530FF8) — PAS d'animation
//   cat 1 : blink + 1                          (@0x531022)
//   cat 2 : blink + 3                          (@0x531075)
//   cat 3 : blink + 3                          (@0x5310C8) — le dossier notait « +? »
//   cat 4 : blink + 1                          (@0x53111B)
//   cat 5 : blink + 3                          (@0x53116B)
//   cat 6 : blink + 5                          (@0x5311B9)
//   cat 7 : blink + 1                          (@0x5311E2)
//
// `canCastAtCursor` n'est lu QUE pour la catégorie 0 (le binaire n'appelle
// Skill_CanCastAtCursor 0x540E60 @0x530FE1 que dans ce cas — c'est son UNIQUE appelant,
// xrefs_to = 1). Renvoie -1 si `kind` est hors [0,7] (== `default` du switch : aucune
// écriture de curseur).
// ---------------------------------------------------------------------------
int CursorSlotForPickCategory(int kind, float gameTimeSec, bool canCastAtCursor);

} // namespace ts2::world

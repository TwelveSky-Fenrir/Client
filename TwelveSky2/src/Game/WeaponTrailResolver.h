// Game/WeaponTrailResolver.h — résolution de la « traînée d'arme » joueur (effet de swoosh/lueur
// skinné qui accompagne un cast). Logique PURE (aucune dépendance D3D/gfx) : le switch
// weaponAnimSlot→index d'effet, le gate d'état d'action→sous-bloc de motion, et le stem SObject.
//
// SOURCE IDA (vérité unique) — DEUX fonctions jumelles, même switch + même gate, seule la
// primitive de dessin diffère (cf. Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §1/§4) :
//   Char_DrawWeaponTrailEffect     0x55E9D0  (self, passe OPAQUE : SObject_DrawEx 0x4D9330 ->
//                                             Model_Render 0x40EBB0). Appelant @0x52DB7C.
//   Char_DrawWeaponEffectVariantB  0x56BF90  (passe OMBRE : SObject_DrawAnimated2 0x4D91C0 ->
//                                             Model_RenderPlanarShadow 0x40F720). Appelant @0x52DA41
//                                             (bracket d'ombre 0x52D9DC..0x52DB15).
// Les deux bouclent sur g_EntityArray (dword_1687234, stride 908) => EFFET JOUEUR UNIQUEMENT
// (les monstres dword_1766F74 / PNJ n'ont pas de traînée d'arme). L'IDB décompilé de 0x56BF90
// est la référence octet-exacte utilisée pour transcrire le switch (@0x56c001..0x56c3eb) et le
// gate (@0x56c411) ci-dessous ; 0x55E9D0 (disasm loc_55ED14.. relu) porte des valeurs identiques.
//
// GATE MAÎTRE (entrée du switch, @0x56c01b) : dessiné SEULEMENT si
//     weaponAnimSlot (entity+220 = this+55) != 0   ET   !altWeaponSet (entity+576 = this+144).
// Placement (identique corps) : origine entité this+63 (=entity+252, pos monde) + cap this+69
// (=entity+276 = PlayerEntity::heading) ; animTime this+62 (=entity+248 = CharAnimState::animFrame,
// le MÊME curseur que le corps). PAS d'attach transform d'os (Model_GetAttachTransform 0x40FDC0
// non appelé, cf. doc §5).
#pragma once
#include <string>

namespace ts2::game {

// Switch weaponAnimSlot (this+55 = entity+220) -> index d'effet de traînée v6 ∈ [0,41], ou -1 si
// l'id d'anim actif n'a AUCUNE traînée associée (cas default du switch). 42 valeurs distinctes,
// cohérentes avec les 42 entrées du catalogue flt_113E2DC (name-buildé au boot par
// AssetMgr_InitAllSlots 0x4DEB50). Transcrit BIT-À-BIT du décompilé Hex-Rays de
// Char_DrawWeaponEffectVariantB 0x56BF90 — AUCUNE valeur inventée (cf. bandeau de tête).
// ⚠ La table-résumé de Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §1 contenait une coquille
// (2266–2275 y donnait 29) ; la vérité est le décompilé : 2266–2275 → 28, 2276–2285 → 29.
int ResolveWeaponTrailIndex(int weaponAnimSlot);

// Gate d'état d'action (state = this+61 = entity+244 = CharAnimState::state) -> quel des 3
// sous-blocs de MOTION de l'effet dessiner (switch @0x56c411 de 0x56BF90) :
//   0 = sous-bloc 0 (unk_F54DB4, motionSub 0) — dessin INCONDITIONNEL (state==1).
//   1 = sous-bloc 1 (unk_F54E50, motionSub 1) — dessin INCONDITIONNEL (state==2 ou 0x20).
//   2 = sous-bloc 2 (unk_F54EEC, motionSub 2) — dessin gaté par frameCount>=1 (state==0 ou grand
//       ensemble d'états de cast). Le binaire ne dessine ce sous-bloc que si
//       Motion_GetFrameCount(unk_F54EEC + 468*v6) >= 1 (@0x56c43e) — cf. WeaponTrailResolver.cpp.
//  -1 = état non géré (default du switch) -> AUCUN dessin.
// La valeur de retour EST l'indice motionSub (0/1/2) passé à MotionCache::GetForWeaponTrail
// (motion cat. 5 "F%03d001%03d" % (v6+1, motionSub+1), dossier 005 — cf. Motion_BuildPathAndLoad
// 0x4D7390 case 5 et Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §3).
int ResolveWeaponTrailMotionSub(int actionState);

// true si le sous-bloc de motion `motionSub` (0/1/2) impose la garde frameCount>=1 avant dessin :
// UNIQUEMENT le sous-bloc 2 (branche LABEL_116 de 0x56BF90). Les sous-blocs 0/1 sont dessinés
// inconditionnellement (repli identité si la motion manque). Exposé pour que l'appelant applique
// exactement le gate du binaire sans dupliquer la constante « 2 ».
inline bool WeaponTrailMotionSubIsFrameGated(int motionSub) { return motionSub == 2; }

// Stem SObject de l'effet (catégorie 9, dossier 009, préfixe 'Y') : "Y%03d001" % (trailIndex+1).
// Format = SObject_BuildPath 0x4D89C0 case 9 ("G03_GDATA\\D04_GSOBJECT\\009\\Y%03d%03d.SOBJECT" %
// (a3+1, a4+1)) avec a3=trailIndex (=v6, boucle i80 d'AssetMgr_InitAllSlots) et a4=0 (boucle
// interne i81<1) -> second champ toujours "001". Résoluble tel quel par gfx::ModelCache::Get
// (kSObjectCategories contient déjà {'Y',9}). Chaîne vide si trailIndex hors [0,42) — pas
// d'exception (même contrat que BuildMonsterStem/BuildNpcStem).
std::string BuildWeaponTrailStem(int trailIndex);

} // namespace ts2::game

// Gfx/EquipModelResolver.h — résolution de l'équipement joueur vers un STEM.
//
// Deux résolutions distinctes, prouvées en IDA (seule vérité, IDB RE/TwelveSky2.exe.i64) :
//
//   (A) MODÈLE d'équipement (paperdoll) -> stem SObject consommable par ModelCache::Get.
//       Port fidèle de SObject_BuildPath 0x4D89C0 : familles W(arme)/L/H/A/P + pièces de corps
//       (slots 14..21 + 0/1/2 du méga-switch case 1). Chaque stem = "C…/W…/L…/H…/A…/P…"
//       (préfixe = 1re lettre, cf. ModelCache::ResolveSObjectFolder).
//
//   (B) MOTION d'arme (palette d'os partagée) -> slot MOTION + stem "C…/X….MOTION".
//       Port fidèle du méga-switch de PcModel_ResolveEquipSlot 0x4E46A0 (plages d'id d'item ->
//       famille d'animation d'arme). Consommable par MotionCache (PAS ModelCache).
//
// ─────────────────────────────────────────────────────────────────────────────────────────────
//  CORRECTION MAJEURE (prouvée ici — CONTREDIT l'hypothèse T3 de Docs/TS2_DEEP_SOBJECT.md §3/§6)
// ─────────────────────────────────────────────────────────────────────────────────────────────
//  T3 supposait que PcModel_ResolveEquipSlot 0x4E46A0 mappe un id d'item -> un MODÈLE SObject de
//  paperdoll dans un catalogue pré-généré. C'EST FAUX. La dérivation demandée par la tâche (via
//  AssetMgr_InitAllSlots 0x4DEB50, même méthode que le catalogue monstre flt_FBBF3C/flt_FF67CC)
//  prouve que le pointeur de retour indexe le catalogue **MOTION** (entrées de 156 o), PAS le
//  catalogue SObject (entrées de 144 o) :
//
//    retour = g_ModelMotionArray(0x8E8B30) + 479232·race + 159744·genre + 19968·f2 + 156·f3 + base
//      base 2624960 (0x280F40) == boucle Motion_BuildPathAndLoad **catégorie 1** d'AssetMgr
//                                 (this + 479232·i10 + 159744·i11 + 19968·i12 + 156·i13 + 2624960)
//                                 -> corps joueur, "D03_GMOTION\001\C%03d%03d%03d.MOTION"
//      base 4062656 (0x3DFF40) == boucle Motion_BuildPathAndLoad **catégorie 6** d'AssetMgr
//                                 (this + 479232·i25 + 159744·i26 + 19968·i27 + 156·i28 + 4062656)
//                                 -> arme, "D03_GMOTION\006\X%03d%03d%03d.MOTION"
//    stride 156 == taille d'une entrée MOTION (cf. MotionCache.h : "g_ModelMotionArray 0x8E8B30,
//    entrées de 156 o chacune"). Le catalogue SObject utilise 144 (toutes les boucles
//    SObject_BuildPath d'AssetMgr) -> incompatible.
//
//  Confirmation par le CONSOMMATEUR : Char_RenderModel 0x527020 fait
//    v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, race, genre, f2, f3, 1, 0, 0);
//  puis passe v37 comme 7ᵉ argument (palette d'os) à CHAQUE SObject_DrawEx du paperdoll
//  (PlayerPaperdoll.h : "v37 = … = UNE palette d'os partagée par TOUTES les pièces"). v37 est
//  donc une MOTION, pas un modèle. MotionCache.h §33 documente déjà 0x4E46A0 comme résolveur de
//  palette joueur — la présente correction généralise au méga-switch (plages d'id d'arme).
//
// ─────────────────────────────────────────────────────────────────────────────────────────────
//  RÉSOLUTION RÉELLE DU MODÈLE d'équipement (Char_RenderModel 0x527020 — la vraie table)
// ─────────────────────────────────────────────────────────────────────────────────────────────
//  Le modèle d'une pièce d'équipement N'EST PAS résolu par 0x4E46A0. Il l'est ainsi :
//    itemRec    = MobDb_GetEntry(&mITEM, equippedItemId);   // enregistrement ITEM_INFO de l'item
//    modelIndex = *(itemRec + 196);                         // champ +196 = INDICE DE MODÈLE visuel
//    desc       = flt_F5xxxx[strideRace·race + strideGenre·genre + 36·modelIndex]; // 36 floats=144 o
//  où flt_F5xxxx est un catalogue SObject pré-généré par AssetMgr_InitAllSlots (SObject_BuildPath).
//  Exemple (casque, v29=a4[34]) : flt_F6685C[3672·race + 1836·genre + 36·*(v29+196)]
//    = octets 14688·race + 7344·genre + 144·modelIndex  ==  boucle AssetMgr case 1 / slot 14
//      (SObject_BuildPath(…, 1, race, genre, 14, i-1, 0), i<51)  ->  "C%03d003%03d".
//  => le STEM d'une pièce de corps = BuildArmorBodyStem(race, genre, slot, modelIndex) ci-dessous ;
//     le liant "id d'item -> modelIndex" = champ ITEM_INFO +196 (ancre 0x527020, MobDb_GetEntry).
//
//  Ce module reste AUTONOME : il ne lit pas la base d'items lui-même — l'appelant fournit
//  `modelIndex` (= ITEM_INFO+196). Aucun câblage, aucune dépendance projet (stems purs).
//
// TODO ancre (NON dérivable en statique par ce front — voir §7 de Docs/TS2_DEEP_SOBJECT.md) :
//   - Le mapping complet "slot de paperdoll (a4[30..74]) -> catalogue flt_F5xxxx -> (catégorie,
//     bodySlot)" pour les ~9 slots de Char_RenderModel : seuls les slots de CORPS (0/1/2/14..21)
//     et l'arme sont dérivés ici. Les autres catalogues (flt_100EA3C arme, unk_F86CBC capes,
//     flt_FABEBC/flt_FB7BBC via *(Entry+188), …) exigent de tracer chaque base individuellement.
//   - La sous-sélection de modèle d'arme `v26` (Char_RenderModel : Item_GetAttribByte0 0x545610 /
//     Item_ClassifyById 0x550800 -> index 0..8) : logique intriquée pilotée par attributs d'item,
//     laissée à un front dédié.
//
// Ancres : SObject_BuildPath 0x4D89C0 ; PcModel_ResolveEquipSlot 0x4E46A0 ;
//          Motion_IsValidWeaponPose 0x4E3A30 ; AssetMgr_InitAllSlots 0x4DEB50 ;
//          Motion_BuildPathAndLoad 0x4D7390 ; Char_RenderModel 0x527020 ;
//          g_ModelMotionArray 0x8E8B30.
#pragma once
#include <cstdint>
#include <string>

namespace ts2::gfx {

// ─────────────────────────────────────────────────────────────────────────────
//  (A) STEMS DE MODÈLE SObject — port fidèle de SObject_BuildPath 0x4D89C0.
// ─────────────────────────────────────────────────────────────────────────────

// Catégorie SObject (2ᵉ argument `a2` de SObject_BuildPath 0x4D89C0 @0x4d89e5). Les valeurs
// numériques SONT celles du binaire (préfixe/dossier déduits — cf. commentaires).
enum class SObjectEquipCategory : int {
    PlayerBody = 1, // "C…" 001  (corps + pièces de corps ; voir BuildArmorBodyStem pour les slots)
    P          = 4, // "P%03d%03d%03d" 004
    L          = 5, // "L%03d%03d%03d" 005  (kind = race+3·genre+1)
    Weapon     = 6, // "W%03d%03d%03d%03d" 006  (arme ; 4 indices)
    H          = 7, // "H%03d%03d%03d" 007
    Y          = 9, // "Y%03d%03d" 009
    LSet2      = 10, // "L%03d%03d%03d" 005  (kind = race+3·genre+7)
    A_001      = 11, // "A%03d001%03d" 010  (kind = race+3·genre+1)
    A_004      = 12, // "A%03d004%03d" 010  (kind = race+3·genre+1)
    A_001Set2  = 13, // "A%03d001%03d" 010  (kind = race+3·genre+7)
    A_004Set2  = 14, // "A%03d004%03d" 010  (kind = race+3·genre+7)
};

// Slot de pièce de corps joueur — sélecteur `a5` du case 1 de SObject_BuildPath (switch @0x4d8a08).
// Le token médian (003/004/…) et l'offset de kind (+1 ou +7) sont ceux du binaire (chaînes de
// format @0x4d8a30..0x4d8baf). Les slots 0/1/2 relèvent du cas défaut (token = slot+1).
enum class EquipBodySlot : int {
    Base0    = 0,  // "C%03d001%03d"  kind+1  (corps SLOT0, flt_F59A7C)
    Base1    = 1,  // "C%03d002%03d"  kind+1  (corps SLOT1, flt_F5B21C)
    Base2    = 2,  // "C%03d003%03d"  kind+1  (unk_F5BC3C ; défaut, token=slot+1)
    Slot14   = 14, // "C%03d003%03d"  kind+7  (flt_F6685C)
    Slot15   = 15, // "C%03d004%03d"  kind+7  (flt_F7C09C)
    Slot16   = 16, // "C%03d014%03d"  kind+7
    Slot17   = 17, // "C%03d012%03d"  kind+7
    Slot18   = 18, // "C%03d013%03d"  kind+7
    Slot19   = 19, // "C%03d008%03d"  kind+7
    Slot20   = 20, // "C%03d015%03d"  kind+1
    Slot21   = 21, // "C%03d016%03d"  kind+1
};

// Port 1:1 de SObject_BuildPath 0x4D89C0 (partie STEM : nom de fichier SANS dossier ni ".SOBJECT").
// `a3`=race, `a4`=genre (les autres indices selon la catégorie, cf. tableau du .cpp). Chaîne vide si
// la catégorie est inconnue (branche défaut @0x4d8df9) ou si race/genre sont hors [0,3)/[0,2).
// Pour la catégorie PlayerBody, préférer BuildArmorBodyStem (gère le token médian par slot).
std::string BuildSObjectStem(SObjectEquipCategory cat, int a3, int a4, int a5, int a6 = 0, int a7 = 0);

// Pièce de corps (paperdoll) : "C{kind:03d}{token:03d}{variant+1:03d}" (SObject_BuildPath case 1).
// kind = race+3·genre (+1 ou +7 selon le slot, cf. EquipBodySlot). `variant` = indice de modèle
// visuel = ITEM_INFO+196 pour une pièce d'équipement (ancre Char_RenderModel 0x527020), ou l'indice
// de costume pour le corps de base (slots 0/1). Chaîne vide si race/genre hors bornes ([0,3)/[0,2))
// ou variant < 0. (Note : le binaire ne borne pas `variant` — l'appelant garantit la présence disque.)
std::string BuildArmorBodyStem(int race, int gender, EquipBodySlot slot, int variant);

// Arme (paperdoll) : "W{race+3·genre+1:03d}{type+1:03d}{subType+1:03d}{level+1:03d}"
// (SObject_BuildPath case 6 @0x4d8ca0). Chaîne vide si race/genre hors bornes ou indices < 0.
std::string BuildWeaponStem(int race, int gender, int type, int subType, int level);

// ─────────────────────────────────────────────────────────────────────────────
//  (B) SLOT/STEM DE MOTION D'ARME — port fidèle de PcModel_ResolveEquipSlot 0x4E46A0.
// ─────────────────────────────────────────────────────────────────────────────

// Décodage du pointeur de retour de PcModel_ResolveEquipSlot 0x4E46A0 en indices de catalogue
// MOTION (voir bandeau de tête pour la preuve de décomposition d'offset). `field2`/`field3` sont
// les indices tels qu'ils entrent dans le chemin MOTION (Motion_BuildPathAndLoad 0x4D7390) :
//   catégorie 1 -> "C%03d%03d%03d" % (race+3·genre+1, field2+1, field3+1)
//   catégorie 6 -> "X%03d%03d%03d" % (race+3·genre+1, field2+1, field3+1)
struct EquipMotionSlot {
    bool valid       = false; // false = garde d'entrée 0x4e46cc échouée (pose invalide) -> sentinelle
    int  motionCat   = 0;     // 1 (corps joueur, base 2624960) ou 6 (arme, base 4062656)
    int  race        = 0;     // a2 (0..2)
    int  gender      = 0;     // a3 (0..1)
    int  field2      = 0;     // stride 19968 : "pose" (a4), OU 0 pour les slots fixes du switch
    int  field3      = 0;     // stride 156 : "état/type" (a5), OU l'index d'anim fixe (77/84/100..127)
};

// Port fidèle du méga-switch de PcModel_ResolveEquipSlot 0x4E46A0 : mappe l'id d'arme équipée `itemId`
// (a8) + race/genre + pose(a4)/état(a5) + a6/a7 vers le slot MOTION correspondant. Reproduit à
// l'identique la garde d'entrée (Motion_IsValidWeaponPose 0x4E3A30) et chaque branche du switch d'id
// (plages 510/559/814..821/1301..2489/19002..19280…). Renvoie {valid=false} pour la sentinelle
// d'entrée (retour `this + 2644772` == cat 1, race0/genre0, field3=127).
EquipMotionSlot ResolveWeaponMotionSlot(int itemId, int race, int gender,
                                        int a4_pose, int a5_state, int a6 = 1, int a7 = 0);

// Stem MOTION du slot résolu : "C{…}" (cat 1) ou "X{…}" (cat 6), SANS dossier ni ".MOTION".
// Destiné à MotionCache (dossier D03_GMOTION\001 ou \006), PAS à ModelCache. Chaîne vide si le slot
// est invalide ou hors bornes. (Cas spécial anim==120 de Motion_BuildPathAndLoad case 1 NON reproduit
// ici : field3 est déjà un index d'anim résolu, jamais 120 dans les retours du switch.)
std::string BuildWeaponMotionStem(const EquipMotionSlot& slot);

} // namespace ts2::gfx

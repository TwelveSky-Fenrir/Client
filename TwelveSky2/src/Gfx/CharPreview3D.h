// Gfx/CharPreview3D.h — apercu 3D du personnage en CharSelect (scene 4) + composition
// de l'apparence. Calque STRICT sur Char_RenderModel 0x527020 et sur les 4 sites d'appel
// de Scene_CharSelectRender 0x51CED0.
//
// ============================================================================
//  1. ROLE
// ============================================================================
// Char_RenderModel 0x527020 est appele EXACTEMENT 4 fois dans tout le binaire, toutes
// depuis Scene_CharSelectRender, en DEUX paires (passe 1 puis passe 2) :
//   ecran LISTE    (this[15714]==1) : (pass=1,isCreate=0) @0x51D361 ; (pass=2,isCreate=0) @0x51D3CC
//                                     record = &unk_1669380 + 0x2768*this[15715]
//   ecran CREATION (this[15714]==2) : (pass=1,isCreate=1) @0x51D429 ; (pass=2,isCreate=1) @0x51D480
//                                     record = &dword_16709B8
// Garde LISTE : `cmp [eax+0F58Ch], 0FFFFFFFFh ; jnz` @0x51D2ED -> si slot == -1, les DEUX
// appels sont sautes (aucun modele dessine).
//
// Signature reelle (frame IDA + ordre de push @0x51D2FB-0x51D361) :
//   Char_RenderModel(this=scene@ecx, int pass, int isCreate, DWORD* rec,
//                    int motion    = this[15717] (+0xF594),
//                    int animState = this[15718] (+0xF598),
//                    float animTime= this[15719] (+0xF59C),
//                    float* pos    = &this[15720] (+0xF5A0),
//                    int rotBlock  = &this[15723] (+0xF5AC))
// yaw = *(float*)(rotBlock+4) = this[15724] (+0xF5B0) — `fld dword ptr [ecx+4]` @0x527076.
//
// ============================================================================
//  2. ⭐ RESOLUTION DES CATALOGUES D'APPARENCE — TROU §13.5/§13.6 COMBLE
// ============================================================================
// La spec consolidee declarait (§13.6) : « Aucun ecrivain trouve pour flt_F59A7C /
// F5B21C / F5CFEC / F7282C / F87F4C / F93FAC / FA000C / FABB5C => chemins d'assets
// visage/cheveux/corps/arme INCONNUS — principal trou restant », et (§13.5) « les 4
// catalogues a 51 variantes = SLOTs {2,3,14,15} et les 3 a 57 = SLOTs {4,5,6}, MAIS PAS
// LEQUEL = LEQUEL => chemins C%03d{SLOT+1}%03d.SOBJECT non reproductibles ».
//
// LES DEUX SONT RESOLUS ICI. `data_refs` ne voyait pas les ecrivains parce que
// AssetMgr_InitAllSlots 0x4DEB50 ecrit via un POINTEUR CALCULE (`lea ecx, disp[base+idx]`),
// jamais via une reference directe au symbole. Le desassemblage direct des boucles donne
// la table complete. Base : `var_18` = g_ModelMotionArray = 0x8E8B30 (App_Init passe
// ecx=0x8E8B30 @0x46224B ; recoupe 13/13 fois ci-dessous).
//
//   for (race<3) for (gender<2) for (i<count)
//       SObject_BuildPath(g_ModelMotionArray + disp + race*rs + gender*gs + i*144,
//                         /*categorie*/1, race, gender, /*a5=slot*/S, /*a6*/f(i), 0);
//
// SObject_BuildPath 0x4D89C0 cas 1 (defaut @0x4D8BA7) :
//       "G03_GDATA\D04_GSOBJECT\001\C%03d%03d%03d.SOBJECT" % (race+3*gender+1, a5+1, a6+1)
// cas speciaux a5=14 @0x4D8A30 / a5=15 @0x4D8A5E : token FIGE "003"/"004" et kindIndex
// **+7** au lieu de +1 (jeu de modeles ALTERNATIF) ; a5=20 @0x4D8B44 : token "015", +1.
//
//  a5 | EA du call     | base (=0x8E8B30+disp) | disp     | race/gender | n  | a6    | token
//  ---+----------------+-----------------------+----------+-------------+----+-------+------
//   0 | 0x4DF3CC       | 0xF59A7C flt_F59A7C   | 0x670F4C | 2016 / 1008 |  7 | i     | 001  VISAGE
//   1 | 0x4DF426       | 0xF5B21C flt_F5B21C   | 0x6726EC |  864 /  432 |  3 | i     | 002  CHEVEUX
//   2 | 0x4DF483       | 0xF5BC3C unk_F5BC3C   | 0x67310C |14688 / 7344 | 51 | i-1   | 003  TORSE
//  14 | 0x4DF8C1       | 0xF6685C flt_F6685C   | 0x67DD2C |14688 / 7344 | 51 | i-1   | 003 (kind+7)
//   3 | 0x4DF4E0       | 0xF7147C unk_F7147C   | 0x68894C |14688 / 7344 | 51 | i-1   | 004  JAMBES
//  15 | 0x4DF91E       | 0xF7C09C flt_F7C09C   | 0x69356C |14688 / 7344 | 51 | i-1   | 004 (kind+7)
//   4 | 0x4DF53A       | 0xF86CBC unk_F86CBC   | 0x69E18C |16416 / 8208 | 57 | i     | 005  ARME A
//   5 | 0x4DF594       | 0xF92D1C unk_F92D1C   | 0x6AA1EC |16416 / 8208 | 57 | i     | 006  ARME B
//   6 | 0x4DF5EE       | 0xF9ED7C unk_F9ED7C   | 0x6B624C |16416 / 8208 | 57 | i     | 007  ARME C
//   8 | 0x4DF6A2       | 0xFABB5C flt_FABB5C   | 0x6C302C |  288 /  144 |  1 | i     | 009
//   9 | 0x4DF6FC       | 0xFABEBC flt_FABEBC   | 0x6C338C | 6624 / 3312 | 23 | i     | 010
//  10 | 0x4DF756       | 0xFB0C5C flt_FB0C5C   | 0x6C812C |  288 /  144 |  1 | i     | 011
//  20 | 0x4DFAE0       | 0xFB7BBC flt_FB7BBC   | 0x6CF08C | 2304 / 1152 |  8 | i     | 015
// (« a6 = i-1 » => le NUMERO de fichier imprime, a6+1, vaut i ; « a6 = i » => il vaut i+1.)
// Catalogue hors categorie 1 :
//  cat 7 | 0x4E01D7 | 0x113DF7C flt_113DF7C | 0x85544C | 288 / 144 | 1 | a5=0,a6=0 -> "H%03d001001"
//        (SObject_BuildPath cas 7 @0x4D8CD5 : "…\007\H%03d%03d%03d.SOBJECT" % (k+1,a5+1,a6+1))
//
// PREUVE DE CONTIGUITE (chaque catalogue finit exactement ou commence le suivant) :
//   0xF59A7C +3*2016=0xF5B21C ✓ +3*864=0xF5BC3C ✓ +3*14688=0xF6685C ✓ +…=0xF7147C ✓
//   +…=0xF7C09C ✓ +…=0xF86CBC ✓ +3*16416=0xF92D1C ✓ +…=0xF9ED7C ✓ ; 0xFABB5C+3*288=0xFABEBC ✓
//
// VERIFICATION DISQUE INDEPENDANTE (G03_GDATA\D04_GSOBJECT\001, comptes pour k=1) :
//   token 001 -> 7 fichiers (=n)   002 -> 3 (=n)    009 -> 1 (=n)
//   010 -> 23 (=n)                 011 -> 1 (=n)    015 -> 8 (=n)
//   -> les 6 comptes non triviaux tombent EXACTEMENT sur les bornes de boucle lues en IDA.
//   Les kindIndex presents pour le token 003 sont 001..012 = {k+1 : k∈[0,6)} ∪ {k+7} :
//   confirmation directe du cas special a5=14 (C007003011.SOBJECT existe).
//
// ============================================================================
//  3. PIECES DESSINEES — ORDRE EXACT
// ============================================================================
// BRANCHE CREATION (isCreate != 0, @0x527033) — indexee par le champ +36 (« job ») comme
// RACE, +44 comme genre. Aucune resolution d'equipement (aucun MobDb_GetEntry).
//   1. CHEVEUX  flt_F5B21C[216*rec36 + 108*rec44 + 36*rec52]  @0x5270B8 -> a5=1, entree[hair]
//   2. VISAGE   flt_F59A7C[504*rec36 + 252*rec44 + 36*rec48]  @0x527113 -> a5=0, entree[face]
//   3. CORPS A  flt_F5CFEC[3672*rec36 + 1836*rec44]           @0x527160
//        0xF5CFEC - 0xF5BC3C = 5040 = 35*144 -> a5=2, entree[35] -> "C%03d003035"
//        (c'est le « trou de 5040 o (35x144) non identifie » de la spec §13.5 : ce n'est
//         PAS un trou, c'est un OFFSET D'ENTREE dans le catalogue a5=2.)
//   4. CORPS B  flt_F7282C[3672*rec36 + 1836*rec44]           @0x5271AD
//        0xF7282C - 0xF7147C = 5040 = 35*144 -> a5=3, entree[35] -> "C%03d004035"
//   5. ARME, selon this[15716] (SCENE, PAS rec+216) — `mov edx,[ecx+0F590h]` @0x5271B8 :
//        variant 0 -> flt_F87F4C @0x527230 : 0xF87F4C-0xF86CBC = 4752 = 33*144 -> a5=4, e[33] -> "C%03d005034"
//        variant 1 -> flt_F93FAC @0x527282 : idem -> a5=5, e[33] -> "C%03d006034"
//        variant 2 -> flt_FA000C @0x5272D4 : idem -> a5=6, e[33] -> "C%03d007034"
//        defaut    -> RIEN (`jmp loc_52744D` — aucune arme)
//   6. si variant==2 : effet attache a un os (SObject_Draw + ModelObj_Draw) — NON PORTE,
//      cf. TODO [0x5272DF] dans le .cpp. ⚠ La spec §5.2 decrit ce bloc comme « si
//      variant==2 ET race∈{0,2} : 2x SObject_Draw (os 2 puis 3) + ModelObj_Draw(unk_B61A54) » ;
//      c'est FAUX pour race==2 — le desassemblage donne DEUX branches DISTINCTES :
//        race==0 @0x527308 : os 2 + ModelObj_Draw(unk_B61A54) PUIS os 3 + ModelObj_Draw(unk_B61A54)
//        race==2 @0x5273E2 : os 2 + ModelObj_Draw(unk_B61AE8)  <- UN SEUL, et objet DIFFERENT
//        race==1           : RIEN
//
// BRANCHE LISTE (isCreate == 0, @0x527452) — indexee par +40 (race) / +44 (genre) ;
// +36 n'y sert que de SENTINELLE `== 3` (`cmp dword ptr [ecx+24h], 3` @0x52754A).
//   0. 9 resolutions MobDb_GetEntry(mITEM, rec+{120,136,184,216,232,248,264,280,296})
//      @0x527463..0x52751B. NB : celle de +232 (v32) est calculee mais JAMAIS relue.
//   1. si rec+36==3 : CORPS cat7 flt_113DF7C[72*rec40 + 36*rec44] @0x527615/0x527664
//      -> "H%03d001001" % (race+3*gender+1).  ⭐ Repond au « TODO [0x52754A] : elucider ce
//      que le bloc garde dessine reellement » de Net/CharSelectPackets.h.
//   2. si rec+4 != 1 : VISAGE a5=0 entree[rec+48] @0x527753/0x5277B0
//   3. CHEVEUX a5=1 entree[rec+52] @0x52780B (inconditionnel)
//   4. TORSE : item(+136) ? (rec36==3 && Load(a5=14[f196]) ? a5=14[f196] : a5=2[f196]) : a5=2[0]
//              @0x527968 / 0x5279C8 / 0x52785E
//   5. JAMBES: item(+184) ? (rec36==3 && Load(a5=15[f196]) ? a5=15[f196] : a5=3[f196]) : a5=3[0]
//              @0x527B25 / 0x527B85 / 0x527A1B
//   6. slots restants (+216 arme, +248, +264, +280, +296, +120) -> NON PORTES, catalogues
//      0x100EA3C/0x102901C/0x1012DBC/0x102D39C/0x1020FDC/0x103B5BC/0x1024FFC/0x103F5DC/
//      0x10435FC/0x104Bxxxx non resolus. TODO [0x527B8E] dans le .cpp.
// Masquage par casque : `if (item136 && rec+44==1) { if (f196 != 37 && f196 != 39) draw; }
// else draw;` @0x5275B3/0x5275CB (corps cat7) et @0x5276E3/0x5276FB (visage) — certains
// casques (f196 37/39) masquent visage+corps cat7 sur le genre 1.
//
// ============================================================================
//  4. PALETTE D'OS — LE SWITCH ~500 CAS EST MORT ICI
// ============================================================================
// UNE palette partagee par TOUTES les pieces :
//   v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, race, gender, motion, animState, 1, 0, 0)
//         @0x52705F (creation) / @0x527544 (liste)
// PcModel_ResolveEquipSlot 0x4E46A0 :
//   garde @0x4E46CC : `if (a2 > 2 || a3 > 1 || !Motion_IsValidWeaponPose(a4,a5))
//                        return this + 2644772;`  <- slot de REPLI
//   a8 = 0 EN DUR aux 2 sites => le switch sur a8 (~500 cas) tombe TOUJOURS sur LABEL_152
//   @0x4E5708 : `if (a4%2 || a5!=1 || a6<=112) return this + 479232*a2 + 159744*a3
//                                                     + 19968*a4 + 156*a5 + 2624960;`
//   a6 = 1 EN DUR => `a6 <= 112` est TOUJOURS VRAI => c'est TOUJOURS ce return.
//   -> slot = g_ModelMotionArray + 2624960 + 159744*(3*race+gender) + 19968*motion + 156*animState
// Boucle ecrivain Motion_BuildPathAndLoad @0x4DEFC6-0x4DF00C : disp **0x280DC0 = 2624960**
// (identique a la constante ci-dessus, au bit pres), strides 0x75000/0x27000/0x4E00/0x9C
// = 479232/159744/19968/156 ✓, 128 animStates.
// Motion_BuildPathAndLoad 0x4D7390 cas 1 @0x4D741E :
//   "G03_GDATA\D03_GMOTION\001\C%03d%03d%03d.MOTION" % (race+3*gender+1, motion+1, animState+1)
// => EXACTEMENT MotionCache::GetForPlayer(race, gender, motion, animState).
// Repli : 2644772 - 2624960 = 19812 = 127*156 -> (race0, gender0, motion0, animState127)
//   = "C001001128.MOTION" -> GetForPlayer(0,0,0,127).
// Motion_IsValidWeaponPose 0x4E3A30 : animState==1 -> motion<8 (@0x4E3A7D) ;
//                                     animState==3 -> motion ∈ {0,2,4,6} (@0x4E3ACF).
// CharSelect n'utilise QUE animState 1 (Idle, `this[15718]=1` @0x51C363) et 3 (Entering,
// @0x52516F) — JAMAIS 0 (contredit Gfx/PlayerPaperdoll.cpp:21, qui passe animState=0).
//
// ============================================================================
//  5. CAMERA / LUMIERE / ECHELLE
// ============================================================================
// CAMERA — ecrite UNE FOIS au passage sous-etat 0->1 (`cmp [edx+8], 1Eh` @0x51BDE0, soit
// frameCounter >= 30), sur les DEUX singletons :
//   g_GfxRenderer : 0x800130=0.0 @0x51BDED · 0x800134=flt_7EDA1C @0x51BDF9 ·
//                   0x800138=flt_7A9764 @0x51BE05 · 0x80013C=0.0 @0x51BE0D ·
//                   0x800140=flt_7A8D74 @0x51BE19 · 0x800144=0.0 @0x51BE21
//   copie identique -> g_GxdRenderer 0x18C51C0..0x18C51D4 @0x51BE2C-0x51BE65
//   flt_7EDA1C = 40 A0 00 00 = 5.0f | flt_7A9764 = C1 E0 00 00 = -28.0f
//   flt_7A8D74 = 41 20 00 00 = 10.0f   (get_bytes, verifie a l'octet)
//   => eye = (0, 5, -28) ; target = (0, 10, 0).
// 0x800130 = g_GfxRenderer(0x7FFE18) + 792 et 0x80013C = +804 : ce sont EXACTEMENT les
// arguments eye/at de Gfx_BeginFrame @0x6A2352 :
//   `up = (0,1,0)` @0x6A233A-0x6A234A ; `D3DXMatrixLookAtLH(this+828, this+792, this+804, up)`
//   puis SetTransform(D3DTS_VIEW=2, this+828) @0x6A2363.
//   -> ⭐ le « up » de la spec §13.4 (« non prouve ») EST prouve : (0,1,0).
// ⚠ PROJECTION : Gfx_BeginFrame ne la touche PAS, et Scene_CharSelectRender non plus
//   (les seuls memcpy de matrices @0x51CF32-0x51CF4D copient le WORLD 0x800244 et la VUE
//   0x800154 vers le GXD). La projection est celle, APPLICATIVE, posee au boot par
//   Gfx_InitDevice 0x69B9B0 (D3DXMatrixPerspectiveFovLH @0x69BFC6, fovY = champ runtime
//   +0x80 * flt_7BB26C — cf. Gfx/Camera.h : 45 deg). CharPreview3D NE DOIT PAS y toucher :
//   il ne fournit QUE la vue (BuildViewMatrix ci-dessous). Le FOV reste le TODO §13.4.
//
// LUMIERE — D3DLIGHT9 @ 0x18C5358, remplie a CHAQUE frame de rendu puis SetLight(0,&light)
// (vtbl+0xCC) @0x51D226 :
//   Type      = 3 (D3DLIGHT_DIRECTIONAL)          `mov ds:dword_18C5358, 3` @0x51D034
//   Diffuse   = (0.8, 0.8, 0.8, 1.0)              @0x51D070-0x51D093
//   Specular  = (0.0, 0.0, 0.0, 1.0)              @0x51D0BF-0x51D0E2
//   Ambient   = (0.8, 0.8, 0.8, 1.0)              @0x51D11A-0x51D13D
//   Position  = (0, 0, 0)                         @0x51D160-0x51D178
//   Direction = normalize(-1, -1, -1)             @0x51D1A7 puis Vec3_Normalize @0x51D1CE
//   Range/Falloff/Atten0..2/Theta/Phi = 0         @0x51D1D5-0x51D205
//   flt_7A9784 = CD CC 4C 3F = 0.8f | flt_7EDA10 = BF 80 00 00 = -1.0f (verifie a l'octet)
// ⚠ TODO [0x51D226] : AUCUN LightEnable ni D3DRS_LIGHTING dans Scene_CharSelectRender —
//   on ignore si ce D3DLIGHT9 (etat FIXED-FUNCTION) influence reellement le chemin SKINNE,
//   qui rebinde ses propres shaders dans Model_DrawSkinnedSubset 0x40CA40. ApplyLight()
//   pousse les memes valeurs dans MeshRenderer (equivalent le plus proche), sans certitude
//   que le binaire les consomme. Spec §13.3, non tranche.
//
// ECHELLE — ⚠ CORRECTION DE LA SPEC §5 (« Echelle : flt_7ED9F8 = 20.0f ») : ce 20.0f n'est
//   PAS une echelle. SObject_DrawEx 0x4D9330 @0x4D946D le passe en arg_0 de Model_Render
//   0x40EBB0, qui l'utilise UNIQUEMENT pour la SPHERE ENGLOBANTE du frustum :
//   centre = pos + (0, 20.0*dbl_7ED9D0, 0), rayon = 20.0*dbl_7ED9D0, avec
//   dbl_7ED9D0 = 3F E0 00 00 00 00 00 00 = 0.5 -> centre.y = pos.y+10, rayon 10
//   (`fmul dbl_7ED9D0` @0x40EC19 ; Frustum_IntersectsSphere @0x40EC3A).
//   Le VRAI vecteur d'echelle est v11 = (1.0, 1.0, 1.0), pose @0x4D9369-0x4D9373 et passe
//   en arg_4. Le modele est donc a l'echelle 1 — dessiner a 20 le rendrait 20x trop grand.
//
// PASSES — `pass ∈ {1,2}` obligatoire : Model_Render @0x40EBD5 fait `dec eax ; cmp eax,1 ;
//   ja -> sortie` => toute autre valeur ne dessine RIEN. Le binaire dessine le paperdoll
//   ENTIER en passe 1, puis le paperdoll ENTIER en passe 2 (les 4 sites sont deux paires
//   adjacentes) : l'appelant doit donc appeler Render(...,1) PUIS Render(...,2) —
//   surtout PAS les deux passes par piece (cf. Gfx/MeshRenderer.h, note kPassBoth).
#pragma once
#include "Gfx/MeshRenderer.h"   // gfx::MeshRenderer / gfx::SkinnedModel / gfx::BonePalette
#include "Gfx/ModelCache.h"     // gfx::ModelCache
#include "Gfx/MotionCache.h"    // gfx::MotionCache
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  Bornes de choix — TOUTES issues des bornes de boucle d'AssetMgr_InitAllSlots
//  0x4DEB50 et des gardes de PcModel_ResolveEquipSlot 0x4E46A0 (@0x4E46CC).
// ---------------------------------------------------------------------------
inline constexpr int kCharPreviewRaceCount    = 3; // a2>2 -> repli (0x4E46CC) ; boucles race<3
inline constexpr int kCharPreviewGenderCount  = 2; // a3>1 -> repli (0x4E46CC) ; boucles gender<2
inline constexpr int kCharPreviewFaceCount    = 7; // catalogue a5=0, `cmp var_4, 7`  @0x4DF38B
inline constexpr int kCharPreviewHairCount    = 3; // catalogue a5=1, `cmp var_4, 3`  @0x4DF3E5
inline constexpr int kCharPreviewVariantCount = 3; // this[15716] ∈ [0,2] (0x526490 / 0x52650B)

// Nombre d'entrees par catalogue (bornes de boucle) — expose pour les gardes de port.
inline constexpr int kCharCatalogCount_EquipA = 51; // a5=2/14, `cmp var_4, 33h` @0x4DF43F/0x4DF87D
inline constexpr int kCharCatalogCount_EquipB = 51; // a5=3/15, @0x4DF49C / 0x4DF8DA
inline constexpr int kCharCatalogCount_Weapon = 57; // a5=4/5/6, `cmp var_4, 39h` @0x4DF4F9/0x4DF553/0x4DF5AD

// Slots de catalogue de la categorie 1 utilises par CharSelect (= a5 de SObject_BuildPath).
//
// NOMMAGE — regle « ne jamais deviner » :
//   Face/Hair sont PROUVES par leur consommateur (a5=0 indexe par la fiche +48 = le choix
//   « visage », 7 entrees = les 7 fleches ; a5=1 indexe par +52 = « couleur de cheveux »,
//   3 entrees). Weapon* est PROUVE par la branche LISTE (switch sur le typeCode de l'item
//   +216 = l'ARME, @0x5282A1) autant que par le `variant` de la creation.
//   EquipA/EquipB sont VOLONTAIREMENT NEUTRES : on sait seulement que a5=2/14 est indexe
//   par l'item de la fiche +136 et a5=3/15 par celui de +184 (@0x527814 / @0x5279D1). Leur
//   role vestimentaire reel (torse ? jambes ? robe ?) n'est PAS prouve — ne pas les
//   renommer « Torso »/« Legs » sans un consommateur qui l'etablisse.
enum class CharCatalogSlot : int {
    Face      = 0,  // 0xF59A7C, token "001", 7 entrees  — indexe par fiche +48
    Hair      = 1,  // 0xF5B21C, token "002", 3 entrees  — indexe par fiche +52
    EquipA    = 2,  // 0xF5BC3C, token "003", 51 entrees, kind+1 — item fiche +136
    EquipAAlt = 14, // 0xF6685C, token "003", 51 entrees, kind+7 (jeu alternatif, job==3)
    EquipB    = 3,  // 0xF7147C, token "004", 51 entrees, kind+1 — item fiche +184
    EquipBAlt = 15, // 0xF7C09C, token "004", 51 entrees, kind+7
    WeaponA   = 4,  // 0xF86CBC, token "005", 57 entrees — variant 0 (creation)
    WeaponB   = 5,  // 0xF92D1C, token "006", 57 entrees — variant 1
    WeaponC   = 6,  // 0xF9ED7C, token "007", 57 entrees — variant 2
    Slot009   = 8,  // 0xFABB5C, token "009", 1 entree — role NON PROUVE (effet os, cf. .cpp)
};

// Entrees FIGEES lues par la branche CREATION (offsets d'entree, cf. bandeau §3).
inline constexpr int kCreateBodyEntryIndex   = 35; // (0xF5CFEC-0xF5BC3C)/144 = (0xF7282C-0xF7147C)/144
inline constexpr int kCreateWeaponEntryIndex = 33; // (0xF87F4C-0xF86CBC)/144 = … = 4752/144

// ---------------------------------------------------------------------------
//  Camera / echelle / lumiere — constantes litterales verifiees a l'octet.
// ---------------------------------------------------------------------------
// Diametre de la sphere englobante du frustum (flt_7ED9F8 = 20.0f) — PAS une echelle.
inline constexpr float kCharPreviewBoundSize = 20.0f;
// Vecteur d'echelle reel passe a Model_Render (v11, @0x4D9369-0x4D9373).
inline constexpr float kCharPreviewScale     = 1.0f;

// ---------------------------------------------------------------------------
//  Choix du formulaire de CREATION (branche isCreate != 0).
//  ⚠ `job` (+36) EST l'index de race de cette branche (`mov edx,[ecx+24h]` @0x527051) ;
//     la branche LISTE, elle, lit +40 (@0x527536). Ne pas confondre.
// ---------------------------------------------------------------------------
struct CharPreviewChoices {
    int32_t job       = 0; // fiche +36 (dword_16709DC), 0..2 — index de RACE de l'apercu
    int32_t gender    = 0; // fiche +44 (dword_16709E4), 0..1
    int32_t face      = 0; // fiche +48 (dword_16709E8), 0..6
    int32_t hairColor = 0; // fiche +52 (dword_16709EC), 0..2
    int32_t variant   = 0; // ⚠ SCENE this[15716] (+0xF590), 0..2 — PAS la fiche (@0x5271B8)
};

// ---------------------------------------------------------------------------
//  Pose de l'apercu — les 5 derniers arguments de Char_RenderModel.
// ---------------------------------------------------------------------------
struct CharPreviewPose {
    int32_t motion    = 0;    // this[15717] (+0xF594) — 0 a l'Init ; 2*classe d'arme a l'armement
    int32_t animState = 1;    // this[15718] (+0xF598) — 1=Idle, 3=Entering. JAMAIS 0.
    float   animTime  = 0.0f; // this[15719] (+0xF59C) — curseur de frame, pas une horloge
    float   pos[3]    = {0.0f, 0.0f, 0.0f}; // this[15720..22] (+0xF5A0..A8)
    float   yawDeg    = 0.0f; // this[15724] (+0xF5B0) — degres (Model_Render 0x40EBB0)
};

// ---------------------------------------------------------------------------
//  Resultat : la palette PARTAGEE + les pieces DANS L'ORDRE DE DESSIN du binaire.
// ---------------------------------------------------------------------------
struct CharPreviewResult {
    BonePalette                      palette{}; // v37 — partagee par toutes les pieces
    std::vector<const SkinnedModel*> pieces;    // ordre exact de Char_RenderModel
    bool                             valid = false;
};

// ---------------------------------------------------------------------------
//  CharPreview3D — resolveur SANS ETAT (tout passe par arguments), calque sur
//  Char_RenderModel 0x527020.
// ---------------------------------------------------------------------------
class CharPreview3D {
public:
    // --- Camera (Scene_CharSelectUpdate 0x51BDED-0x51BE65 + Gfx_BeginFrame 0x6A2352) ---
    static D3DXVECTOR3 CameraEye()    { return D3DXVECTOR3(0.0f,  5.0f, -28.0f); } // flt_7EDA1C / flt_7A9764
    static D3DXVECTOR3 CameraTarget() { return D3DXVECTOR3(0.0f, 10.0f,   0.0f); } // flt_7A8D74
    static D3DXVECTOR3 CameraUp()     { return D3DXVECTOR3(0.0f,  1.0f,   0.0f); } // @0x6A233A-0x6A234A

    // Vue = D3DXMatrixLookAtLH(eye, target, up) — l'operation EXACTE de Gfx_BeginFrame
    // @0x6A2352. NE TOUCHE PAS a la projection (la scene 4 ne la pose jamais) : l'appelant
    // conserve la projection applicative et fait mesh.SetCamera(view, projApplicative).
    static void BuildViewMatrix(D3DXMATRIX& out);

    // Pousse la lumiere du D3DLIGHT9 0x18C5358 dans le MeshRenderer (equivalent le plus
    // proche du chemin shader). Cf. TODO [0x51D226] du bandeau §5.
    static void ApplyLight(MeshRenderer& mesh);

    // --- Composition ---
    // Branche CREATION (isCreate != 0, @0x527033) : apercu LIVE du formulaire. Se
    // recalcule a chaque changement de choix (aucun cache d'etat : rappeler suffit).
    static CharPreviewResult BuildFromChoices(ModelCache& models, MotionCache& motions,
                                              const CharPreviewChoices& choices,
                                              const CharPreviewPose&    pose);

    // Branche LISTE (isCreate == 0, @0x527452) : apercu du personnage selectionne.
    // `rec` = fiche BRUTE de 10088 o (&unk_1669380 + 0x2768*slot = net::g_CharRecords[slot]).
    // Renvoie un resultat invalide si `rec` est nul.
    static CharPreviewResult BuildFromRecord(ModelCache& models, MotionCache& motions,
                                             const uint8_t* rec, const CharPreviewPose& pose);

    // Dessine TOUTES les pieces de `r` pour UNE passe. `pass` DOIT valoir 1 ou 2
    // (Model_Render @0x40EBD5) ; toute autre valeur ne dessine rien, comme le binaire.
    // Appeler DEUX FOIS (1 puis 2) — jamais les deux passes par piece.
    static void Render(MeshRenderer& mesh, const CharPreviewResult& r,
                       const CharPreviewPose& pose, int pass);

    // --- Stems (exposes pour diagnostic/test) ---
    // "C%03d%03d%03d" % (race+3*gender+1 [ou +7 si slot∈{14,15}], slot+1 [fige pour les
    // cas speciaux], fileNumber). `entryIndex` = INDICE D'ENTREE dans le catalogue ; la
    // conversion entree -> numero de fichier depend du catalogue (cf. colonne « a6 »).
    // Chaine vide si race/gender/slot/entryIndex hors bornes.
    static std::string BuildCatalogStem(CharCatalogSlot slot, int race, int gender, int entryIndex);

    // "H%03d001001" % (race+3*gender+1) — corps categorie 7, garde `rec+36 == 3`.
    static std::string BuildCat7BodyStem(int race, int gender);

    // Palette d'os partagee (PcModel_ResolveEquipSlot 0x4E46A0), repli inclus.
    static BonePalette ResolveBonePalette(MotionCache& motions, int race, int gender,
                                          int motion, int animState, float animTime);
};

} // namespace ts2::gfx

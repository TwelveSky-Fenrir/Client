// Game/GameDatabase.h — chargeur des tables de donnees statiques (.IMG 005_*) vers g_World.db.
//
// Reproduit fidelement les 5 chargeurs du client (cluster 0x4C2680..0x4C7390) :
//   005_00001.IMG -> LEVEL_INFO   (LevelTable_LoadImg 0x4C2680, magic 0xE31,   145 x 44 o)
//   005_00002.IMG -> ITEM_INFO    (MobDb_LoadImg      0x4C3930, magic 0x1CB3, 99999 x 436 o)
//   005_00003.IMG -> SKILL_INFO   (SkillGrowthTbl...  0x4C4BC0, magic 0xC7E,   300 x 776 o)
//   005_00004.IMG -> MONSTER_INFO (ItemDefTbl_LoadImg 0x4C62A0, magic 0x1583, 10000 x 944 o)
//   005_00010.IMG -> SOCKET_INFO  (AnchorTbl_LoadImg  0x4C7390, magic 0xFDB,  3031 x 20 o)
//
// Enveloppe [rawSize][packedSize][zlib] -> payload ; payload[0]^magic == count (garde
// d'integrite en dur) ; enregistrements a l'offset `header`, stride fixe. Cf.
// Docs/TS2_IMG_FORMAT.md et Docs/TS2_GAMEPLAY_LOGIC.md (layouts prouves par les validateurs).
#pragma once
#include "Game/GameState.h"
#include <cstdint>
#include <string>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Enregistrements types (layout byte-exact des .IMG).
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

// LEVEL_INFO — 44 o = 11 dwords. Champs prouves par LevelTable_ValidateEntry 0x4C2430.
// Les stats de base sont lues par LevelTable_GetStatK (Stat3=extAtk .. Stat9=ratingMax) ;
// il n'y a AUCUNE base de MP dans cette table (cf. TS2_GAMEPLAY_LOGIC.md 2.4).
// Cross-check VeryOldClient : classe LEVEL (VeryOldClient/GameSystem/CLEVEL.cpp). En-tete
// +0..+12 CONFIRMED ; bloc stats +16..+40 = CONFLICT J-1 (bloc REORDONNE entre builds +
// pas de base MP dans ce build) — cf. Docs/TS2_TABLES_ROSETTA.md §2 + Journal J-1. IDA prime,
// noms VeryOld = indice semantique SEUL, jamais de transposition d'offset/valeur.
struct LevelInfo {
    uint32_t level;            // +0  1..145 (doit valoir index+1) ; ex-VeryOldClient: lIndex (CONFIRMED)
    int32_t  expCumul;         // +4  experience cumulee (< expNext ; == expCumul[niv+1]-1) ; ex-VeryOldClient: lRangeInfo[0] (expMin) (CONFIRMED)
    int32_t  expNext;          // +8  seuil du niveau suivant (1..2 000 000 000) ; ex-VeryOldClient: lRangeInfo[1] (expMax) (CONFIRMED)
    uint32_t meta;             // +12 champ meta (<= 100) ; ex-VeryOldClient: lRangeInfo[2] (facteur%) (CONFIRMED, borne<=100 => %)
    // +16..+40 : bloc stats consumer-prouve (Char_Calc* 0x4D0530/0x4D1830/0x4D2830/0x4D34B0/
    // 0x4D4ED0/0x4CD970/0x4CE3F0). VeryOld nomme ce bloc dans un ORDRE DIFFERENT (CONFLICT J-1) :
    // ne PAS lire les ex-VeryOldClient ci-dessous comme le role du champ — c'est le nom VeryOld
    // aligne par ORDINAL, dementi par le consumer IDA (IDA gagne).
    int32_t  baseExtAttack;    // +16 base attaque externe   (Stat3) ; ex-VeryOldClient: lAttackPower  (CONFLICT J-1 — IDA=atk EXT)
    int32_t  baseIntAttack;    // +20 base attaque interne   (Stat4) ; ex-VeryOldClient: lDefensePower (CONFLICT J-1 — IDA=atk INT, sens oppose)
    int32_t  baseExtDefense;   // +24 base defense externe   (Stat5) ; ex-VeryOldClient: lAttackSuccess(CONFLICT J-1 — IDA=def EXT)
    int32_t  baseIntDefense;   // +28 base defense interne   (Stat6) ; ex-VeryOldClient: lAttackBlock  (CONFLICT J-1 — IDA=def INT)
    int32_t  baseMaxHp;        // +32 base PV max            (Stat7) ; ex-VeryOldClient: lElementAttack(CONFLICT J-1 — IDA=PV MAX)
    int32_t  baseAtkRatingMin; // +36 base rating min degats (Stat8) ; ex-VeryOldClient: lLife        (CONFLICT J-1 — le vrai HP est +32)
    int32_t  baseAtkRatingMax; // +40 base rating max degats (Stat9) ; ex-VeryOldClient: lMana        (CONFLICT J-1 — lMana N'EXISTE PAS dans ce build)
};
static_assert(sizeof(LevelInfo) == 44, "LevelInfo doit faire 44 o");

// ITEM_INFO — 436 o. Champs prouves par MobDb_ValidateEntry 0x4C2C50 + TS2_GAMEPLAY_LOGIC.md 2.4.
// Les champs `fieldNNN` sont valides (bornes connues) mais leur role reste a documenter.
// Les offsets de stats (+292..+432) sont ceux consommes par le moteur de stats (Char_Calc*).
// Cross-check VeryOldClient : classe ITEM (VeryOldClient/GameSystem/CITEM.cpp, singleton mITEM ;
// accesseur ITEM::ReturnDataAddr == MobDb_GetEntry 0x4C3C00). ATTENTION: MODELE DIVERGENT : VeryOld a
// 5 attrs (Str/Dex/Vit/Int/Luck) la ou IDA n'a que 4 champs de contribution (+292/296/300/304) —
// NE PAS mapper 1:1. Detail par offset : Docs/TS2_TABLES_ROSETTA.md §3 + Journal J-A/J-B/J-C.
struct ItemInfo {
    uint32_t itemId;         // +0   1-based ; 0 = slot vide. (== index+1) ; ex-VeryOldClient: iIndex (CONFIRMED)
    char     name[25];       // +4   nom (terminaison nulle dans [0..24]) ; ex-VeryOldClient: iName (CONFIRMED)
    // +29 : layout 3x51 PROUVE (record 436 o) ; le LABEL `model` n'est PAS prouve par un lecteur.
    // ex-VeryOldClient: iDescription[3][51] (PLAUSIBLE, memes dims — J-A : revue relabel model->description recommandee).
    char     model[3][51];   // +29  3 noms de modeles (51 o chacun)
    uint8_t  _pad182[2];     // +182 reserve
    // +184 : classificateur SECONDAIRE prouve (Item_MeetsEquipRequirement 0x64ECD0).
    // ex-VeryOldClient: iType/iSort (PLAUSIBLE, J-B — iType(1..8)/iSort(1..99) ne couvrent pas 1..6 ; valeurs non transposees).
    uint32_t category;       // +184 categorie (1..6 ; 5=equip/arme, 6=classe4)
    // +188 : classificateur MAITRE (Item_GetEquipCategory 0x54C940, Weapon_ClassFromEquip 0x4CC9F0,
    // Item_IsUpgradeableWeapon 0x4C9960 ==28=arme). ex-VeryOldClient: iType (CONFIRMED role ;
    // ATTENTION: plages divergentes IDA 1..36 vs VeryOld 1..8 — nom aligne, valeurs JAMAIS transposees).
    uint32_t typeCode;       // +188 type (1..36 ; 28=arme, 29=arme elem., 30=monture/costume, 35/36=pet)
    // +192 IconID (1-based, PAS confondre avec itemId/+0) : index dans le pool d'icones
    // g_AssetMgr_ItemIconSlots (dossier G03_GDATA\D01_GIMAGE2D\002\, 4000 slots), formate
    // en "002_%05u.IMG". Confirme par decompilation de cGameHud_Render 0x64A900
    // (`v133 = *(ITEM_INFO+192) - 1` = index pool 0-based ; Sprite2D_BuildPath demontre que
    // le suffixe fichier = index pool 0-based + 1 = IconID tel quel, donc AUCUN offset a
    // appliquer entre ce champ et le numero de fichier). Voir Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md.
    uint32_t iconId;         // +192 (1..10000) ; ex-VeryOldClient: iDataNumber2D (CONFIRMED, index 0-based icone)
    uint32_t field196;       // +196 (<= 10000) ; ex-VeryOldClient: iDataNumber3D (PLAUSIBLE, adjacence 2D)
    uint32_t field200;       // +200 (<= 10000) ; ex-VeryOldClient: iAddDataNumber3D (PLAUSIBLE)
    uint32_t itemLevel;      // +204 niveau item (1..145 ; seuils scaling 45/100/113/146) ; ex-VeryOldClient: iLevel (CONFIRMED)
    uint32_t field208;       // +208 (<= 12)  ; ex-VeryOldClient: iMartialLevel ? (PLAUSIBLE, non tranche Rosetta)
    // +212 : FACTION requise. Garde d'eligibilite @0x64ECF5 (Item_MeetsEquipRequirement
    // 0x64ECD0) : `if (a2[53] != 1 && a2[53] - 2 != g_LocalElementSecondary) return 0` —
    // la valeur 1 = « toutes factions » (passe-partout), sinon (valeur - 2) doit egaler
    // g_LocalElementSecondary 0x1673198 (= g_World.self.elementSecondary).
    uint32_t field212;       // +212 (1..4) faction requise ; 1 = toutes [0x64ECF5]
    uint32_t subtype;        // +216 sous-type (1..14 ; 2/4/5/6/7/9=classe1, 11..14=classe2 gemmes) ; ex-VeryOldClient: iEquipInfo[2] (CONFIRMED, code slot d'equip)
    // +220 : PRIX d'achat dans la monnaie secondaire, PAS en or. Prouve par
    // UI_NpcShop_OnRDown_Buy 0x5E5000 : `mov ecx, ds:g_InvWeight / cmp ecx, [ebp+var_20] /
    // jge` @0x5e548e-0x5e5497 -> compare a g_InvWeight 0x16732AC (misnomer IDB : ce global
    // est une MONNAIE, cf. Net/GameHandlers_Misc.cpp:314/361 qui y ecrit `p.money` et en
    // retire 100000000). Modificateurs prouves au meme site : x0.9 si g_SelfMorphNpcId==291
    // (ftol(v32[55] * 0.9)), et x99 (quantite) si typeCode(+188)==2 (empilable). Echec ->
    // StrTable005_Get(0xD6=214). ex-VeryOldClient: iBuyCost (CONFIRMED role = prix).
    uint32_t field220;       // +220 (1..2 000 000 000) prix en monnaie secondaire [0x5e5497]
    uint32_t field224;       // +224 (<= 2 000 000 000)
    // +228 : PRIX d'achat EN OR (et non un prerequis de stat — correction W7). Prouve par
    // UI_NpcShop_OnRDown_Buy : `mov ecx, ds:g_Currency / cmp ecx, [eax+0E4h] / jge`
    // @0x5e54c2-0x5e54ce -> compare a g_Currency 0x1673180 (= g_World.self.currency, affiche
    // « Or : %d » par UI/GameHud.cpp). Sans remise et sans x99, contrairement a +220.
    // Echec -> StrTable005_Get(0x586=1414).
    uint32_t field228;       // +228 (<= 2 000 000 000) prix en or [0x5e54ce]
    // +232/+236 : EXIGENCE D'EQUIPEMENT SOMMEE (la seule prouvee). Garde @0x64ED49 :
    // `if (a2[59] + a2[58] > g_SelfLevelBonus + g_SelfLevel) return 0` — soit
    // field236 + field232 > self.levelBonus + self.level => refus. Les deux termes sont
    // homogenes aux deux globals : +232 (1..145) = niveau, +236 (<=12) = palier de
    // renaissance. NB : ce n'est PAS itemLevel (+204), qui ne sert qu'au scaling de stats.
    uint32_t field232;       // +232 (1..145) niveau requis (terme 1/2) [0x64ED49]
    uint32_t field236;       // +236 (<= 12) palier renaissance requis (terme 2/2) [0x64ED49]
    uint32_t flags[11];      // +240 11 drapeaux (chacun 1..2) ; ex-VeryOldClient: famille iCheck* (~13 flags drop/sell/trade/...) (PLAUSIBLE, corr. de FAMILLE, comptes 11 vs 13 divergents)
    uint32_t skillFlag;      // +284 1=normal, 2=skill, 3=upgrade (scalingMode ; Equip_ComputeGemSetBonus 0x54E420 ==2) ; aucun champ VeryOld aligne (PLAUSIBLE role IDA)
    uint32_t field288;       // +288 (< 366)
    // +292..+304 : bloc attrs. ATTENTION: MODELE DIVERGENT (J-C) : 5 attrs VeryOld vs 4 champs IDA — pas de mapping 1:1.
    int32_t  attrPrimaryA;   // +292 base Force externe (atk/def ext) ; ex-VeryOldClient: iStrength (Force externe / waegong) (PLAUSIBLE J-C)
    int32_t  attrPrimaryB;   // +296 base Force interne (atk/def int) ; ex-VeryOldClient: iIntelligent (Force interne / naegong) (PLAUSIBLE, meme divergence de modele)
    int32_t  attrRatingMin;  // +300 base rating min / def interne (x0.9) ; attr defensif sec. (Char_SumGemStatC slot4) ; aucun champ VeryOld (PLAUSIBLE)
    int32_t  attrRatingMax;  // +304 base rating max ; attr offensif sec. (Char_SumGemStatA slots 2,7) ; aucun champ VeryOld (PLAUSIBLE)
    int32_t  critRate;       // +308 taux crit plat ; ex-VeryOldClient: iCritical (PLAUSIBLE, nomme par bornes, hors bloc reader-prouve 312..336)
    int32_t  extAttack;      // +312 attaque externe plate (Char_CalcExternalAttack) ; ex-VeryOldClient: iAttackPower (CONFIRMED, VeryOld a UN seul iAttackPower => aligne sur l'externe)
    int32_t  intAttack;      // +316 attaque interne plate (Char_CalcInternalAttack) ; variante interne (PLAUSIBLE, VeryOld ne distingue pas int/ext)
    int32_t  extDefense;     // +320 defense externe plate (Char_CalcExternalDefense) ; ex-VeryOldClient: iDefensePower (CONFIRMED, aligne sur def externe)
    int32_t  intDefense;     // +324 defense interne plate (Char_CalcInternalDefense) ; pas de champ VeryOld distinct (PLAUSIBLE)
    int32_t  maxHp;          // +328 PV max plats (reader-prouve) ; absent VeryOld (CONFIRMED cote IDA, indice VeryOld null)
    int32_t  maxMp;          // +332 PM max plats (Char_CalcMaxMP) ; absent VeryOld (PLAUSIBLE, nomme par bornes)
    int32_t  accuracy;       // +336 precision plate (reader-prouve) ; ex-VeryOldClient: iAttackSucess (CONFIRMED, precision = taux de reussite d'attaque)
    uint32_t field340;       // +340 (<= 16)
    uint32_t field344;       // +344 (<= 10000)
    uint32_t field348;       // +348 (<= 300) ; id competence acquise (cat 5 = manuel ; Pkt_ItemActionDispatch 0x46A320 G0) ; ex-VeryOldClient: iGainSkillNumber (CONFIRMED)
    uint32_t field352;       // +352 (<= 100)
    uint32_t field356;       // +356 (<= 1000)
    int32_t  regen;          // +360 regen / reduction % cout MP (Player_CastSkill 0x53BC40) ; aucun champ VeryOld aligne (PLAUSIBLE)
    int32_t  evasion;        // +364 esquive plate ; ex-VeryOldClient: iAttackBlock (blocage/makgi) (PLAUSIBLE — blocage != esquive (hoepi), a lever en dynamique, cf. GAP-4)
    int32_t  resistAll;      // +368 resistance elementaire globale ; ex-VeryOldClient: iElementDefensePower (PLAUSIBLE, non prouve par lecteur nomme)
    // +372..+435 ferme le record 436 o. ex-VeryOldClient: queue composite (iPotionType[2], iLastAttackBonusInfo[2],
    // iCapeInfo[3], iBonusSkillInfo[N][2]) (PLAUSIBLE, corr. STRUCTURELLE paires cle/valeur, pas champ-a-champ).
    struct { int32_t key; int32_t val; } resist[8]; // +372..+432 : 8 paires (cle,valeur)
};
static_assert(sizeof(ItemInfo) == 436, "ItemInfo doit faire 436 o");

// SKILL_INFO — 776 o. Table SKILL (005_00003.IMG), chargee brute dans kTables[2]
// (g_World.db.skill). Layout PROUVE en octet par SkillGrowthTbl_ValidateRecord 0x4C4160
// (lit chaque offset en dur), l'interpolation SkillGrowthTbl_InterpStatByLevel 0x4C4EE0
// (blocs statMin/statMax + denominateur levelNorm) et l'accesseur SkillGrowthTbl_GetRecord
// 0x4C4E90 (base + 776*(id-1)). Cross-check VeryOldClient : classe SKILL (VeryOldClient/
// GameSystem/CSKILL.cpp, singleton mSKILL ; SKILL::ReturnDataAddr == SkillGrowthTbl_GetRecord).
// Detail par offset : Docs/TS2_TABLES_ROSETTA.md §4. Les champs `fieldNNN` portent la borne
// PROUVEE par le validateur ; +568/+572 sont un GAP (dwords [142]/[143] non attribues).
struct SkillInfo {
    uint32_t skillId;             // +0   1..300, DOIT valoir index+1 (0 = slot vide) [SkillGrowthTbl_ValidateRecord 0x4C4160] ; ex-VeryOldClient: sIndex (CONFIRMED)
    char     name[25];            // +4   nom (NUL-term dans [0..24], rec0 "Mercy Healing") [validateur : scan 25 o -> echoue si pas de NUL] ; ex-VeryOldClient: sName[25] (CONFIRMED)
    char     description[10][51]; // +29  10 chaines de 51 o (NUL-term) [validateur : boucle 10 x 51] ; ex-VeryOldClient: sDescription[10][51] (CONFIRMED, dims identiques)
    uint8_t  _pad539[1];          // +539 reserve (aligne dword[135] a +540)
    uint32_t field540;            // +540 (validateur [1,4]) ; ex-VeryOldClient: element_flag ? (PLAUSIBLE, Rosetta §4 dword[135])
    uint32_t category;            // +544 (validateur [1,5]) type de competence (4/5 = posture/stance) [Player_CastSkill 0x53BC40] ; ex-VeryOldClient: sType (CONFIRMED role, Rosetta §4)
    uint32_t field548;            // +548 (validateur [1,10000]) ; ex-VeryOldClient: subcategory / sAttackType (PLAUSIBLE, Rosetta §4 dword[137])
    uint32_t field552;            // +552 (validateur [1,4]) ; ex-VeryOldClient: req_element/branche / sTribeInfo[2] (PLAUSIBLE, Rosetta §4 dword[138])
    uint32_t field556;            // +556 (validateur [1,10]) ; req_weapon_type (1=any, sinon item.+188 - 11) (PLAUSIBLE, Rosetta §4 dword[139])
    uint32_t spCost;              // +560 (validateur [1,1000]) cout en points de competence (apprentissage : dword_16731D4 -= rec[0x230]) ; ex-VeryOldClient: sLearnSkillPoint (CONFIRMED, Rosetta §4)
    uint32_t levelNorm;           // +564 (validateur [1,1000]) denominateur d'interpolation = niveau-cap [SkillGrowthTbl_InterpStatByLevel 0x4C4EE0 : / (int)rec[141]] ; ex-VeryOldClient: sMaxUpgradePoint (CONFIRMED)
    uint32_t field568;            // +568 (validateur [0,10])  GAP (dword[142] non attribue, Rosetta §4)
    uint32_t field572;            // +572 (validateur [0,1000]) GAP (dword[143] non attribue, Rosetta §4)
    // +576 / +676 : deux blocs de 25 stats interpolees par niveau (v = statMin + level *
    // (statMax - statMin) / levelNorm, division ENTIERE) [SkillGrowthTbl_InterpStatByLevel
    // 0x4C4EE0, cases 1..25 : statMin = rec[144+(k-1)], statMax = rec[169+(k-1)]]. statIndex 1
    // = cout MP (Player_CastSkill / Skill_CastStoredAtTarget 0x53E740 : g_SelfMana -= cost).
    // ex-VeryOldClient: sGradeInfo[0]/[1] (grade min/max) (CONFIRMED ; sous-champs 22 vs 25 =
    // build different, valeurs jamais transposees).
    int32_t  statMin[25];         // +576 (dword[144..168]) bornes basses par palier
    int32_t  statMax[25];         // +676 (dword[169..193]) bornes hautes par palier (ferme le record 776 o)
};
static_assert(sizeof(SkillInfo) == 776, "SkillInfo doit faire 776 o");

// Entree de table de drop (8 o) — {u32<=1000000 ; u32<100000} par le validateur
// ItemDefTbl_ValidateRecord 0x4C5F60 (paires @+448 dropA[5] et @+544 dropB[50], pas 8 o).
struct DropEntry { uint32_t itemId; uint32_t chance; };
static_assert(sizeof(DropEntry) == 8, "DropEntry doit faire 8 o");

// MONSTER_INFO — 944 o. Table MONSTER (005_00004.IMG), chargee brute dans kTables[3]
// (g_World.db.monster). MISNOMER IDB : les symboles "ItemDefTbl_*" chargent/valident/lisent
// EN REALITE le fichier MONSTER_INFO (nom embarque "MONSTER_INFO", rec[0]="Goblin"), PAS
// ITEM_INFO. Layout PROUVE en octet par ItemDefTbl_ValidateRecord 0x4C5F60 (lit chaque offset
// en dur) ; accesseur STRICTEMENT 1-based ItemDefTbl_GetRecord 0x4C6570 (base+944*(id-1),
// rejet id<1||id>count, garde 1er dword!=0). Deux champs sont prouves par CONSOMMATEUR de
// rendu Char_Draw 0x5805C0 : kindIndexP1 (+244, `*(def+244)-1` = indice de modele visuel) et
// hpMax (+368, diviseur de l'etat de degat). Cf. Docs/TS2_MONSTER_NPC_MODEL.md.
struct MonsterInfo {
    uint32_t id;                 // +0    1-based, ==index+1 (validateur : <1||>10000 ; ==a2+1)
    char     name[25];           // +4    nom (scan 25 o, echoue si pas de NUL) — rec0 "Goblin"
    // +29 : 2 chaines optionnelles 101 o (validateur : for j<2, k<101 @ 101*j+k+29). TOUJOURS
    // VIDES sur les mobs reels — CE N'EST PAS un champ "nom de modele" (cf. Gfx/ModelCache.h :
    // le modele est resolu par printf depuis kindIndexP1, pas stocke ici). Role non elucide.
    char     textBlock[2][101];  // +29   (2x101 = 202 o -> ferme a +231)
    uint8_t  _pad231[1];         // +231  align (29+202=231 ; field232 a +232)
    int32_t  field232;           // +232  [1,15] type/classe ; ==2 => a des etats de degat (Char_Draw)
    int32_t  field236;           // +236  [1,51]
    int32_t  field240;           // +240  [1,2]
    int32_t  kindIndexP1;        // +244  [1,10000] INDICE DE MODELE 1-based (Char_Draw : *(def+244)-1) — CRITIQUE
    int32_t  collDim[3];         // +248  3x [1,1000] : rayon collision sqrt(collDim[2]^2+collDim[0]^2)*0.5 (Pkt_SpawnMonster 0x467B00)
    uint32_t field260;           // +260  <=1000
    int32_t  field264;           // +264  [1,4]
    int32_t  field268;           // +268  [1,2]
    int32_t  field272[6];        // +272  6x [1,10000] (272..292 -> ferme a +296)
    uint32_t field296;           // +296  <4
    uint32_t field300[3];        // +300  3x <=10000 (300..308 -> ferme a +312)
    uint32_t field312;           // +312  <4
    uint32_t field316[3];        // +316  3x <=10000 (316..324 -> ferme a +328)
    int32_t  field328;           // +328  [1,10000]
    int32_t  field332;           // +332  [1,10000]
    int32_t  rewardMin;          // +336  [1,1000000], +336<=+340 (paire min/max, role non prouve)
    int32_t  rewardMax;          // +340  [1,1000000]
    int32_t  level;              // +344  [1,145] niveau
    uint32_t field348;           // +348  <=12
    int32_t  field352;           // +352  [1,1000]
    uint32_t field356;           // +356  <=1000
    uint32_t field360;           // +360  <=100000000
    uint32_t field364;           // +364  <=100000000
    int32_t  hpMax;              // +368  [1,2000000000] MAX HP — diviseur damage-state (Char_Draw def[+368]) — CRITIQUE
    int32_t  field372;           // +372  [1,6]
    uint32_t field376;           // +376  <=10000, +376<=+380
    uint32_t field380;           // +380  <=10000
    uint32_t field384;           // +384  <=1000
    uint32_t field388;           // +388  <=1000
    uint32_t field392;           // +392  <=1000
    uint32_t field396;           // +396  <=300000
    uint32_t field400;           // +400  <=200000
    uint32_t field404[4];        // +404  4x <=100000 (404..416 -> ferme a +420)
    uint32_t field420;           // +420  <=100
    uint32_t field424;           // +424  <=100, +424<=+428
    uint32_t field428;           // +428  <=100
    int32_t  field432;           // +432  /100 in {0,2,3,4,5}, /100<=15
    uint32_t field436;           // +436  <=1000000
    uint32_t goldMin;            // +440  <=100000000, +440<=+444 GOLD paire min/max
    uint32_t goldMax;            // +444  <=100000000
    DropEntry dropA[5];          // +448  5x {u32<=1000000 ; u32<100000}, pas 8 o (448..487 -> ferme a +488)
    uint32_t field488[12];       // +488  12x <=1000000 (488..535 -> ferme a +536)
    uint32_t field536;           // +536  <=1000000
    uint32_t field540;           // +540  <100000
    DropEntry dropB[50];         // +544  50x {u32<=1000000 ; u32<100000}, pas 8 o (544..943 -> ferme a +944)
};
static_assert(sizeof(MonsterInfo) == 944, "MonsterInfo doit faire 944 o");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// API.
// ---------------------------------------------------------------------------

// Charge les 5 tables .IMG dans g_World.db. `gameDataDir` = racine "GameData"
// (les fichiers sont sous <gameDataDir>\G03_GDATA\D01_GIMAGE2D\005\).
// Renvoie true si TOUTES les tables sont chargees et validees (garde de compteur OK ET
// boucle *_ValidateRecord OK sur chaque enregistrement, comme les chargeurs d'origine qui
// renvoient 0 des le premier enregistrement invalide — cf. @0x4C64F5..0x4C6527).
//
// `useTR` = etat du flag g_UseTRVariant 0x1669190 (champ 1 de la cmdline, ecrit @0x460C48).
// A 1, SEULES les tables ITEM (005_00002), SKILL (005_00003) et MONSTER (005_00004) basculent
// sur le sous-dossier localise ...\005\TR\ : ce sont les seules dont le chargeur teste le flag
// (`cmp ds:g_UseTRVariant, 1` @0x4C3939 / @0x4C4BC9 / @0x4C62A9). LEVEL (005_00001) et SOCKET
// (005_00010) n'ont AUCUNE branche TR dans le binaire et restent EU meme en mode TR, bien que
// 005\TR\005_00001.IMG existe sur disque — c'est voulu (fidelite).
// Defaut `false` = comportement EU historique (les appelants de test n'ont rien a changer).
bool LoadGameDatabases(const std::string& gameDataDir, bool useTR = false);

// Accesseur type LEVEL_INFO. `level` 1-based (1..145) ; nullptr hors bornes.
// (le client indexe record[level-1].)
const LevelInfo* GetLevelInfo(int level);

// Sous-table EXP de RENAISSANCE — 12 dwords accoles au manager mLEVEL 0x8E7208, apres
// les 145 enregistrements LEVEL_INFO (145 x 44 = 6380 o = 0x18EC : la sous-table commence
// donc pile a mLEVEL+0x18EC, soit le dword d'indice 1595).
//
// Ces 12 valeurs ne sont PAS chargees depuis 005_00001.IMG : elles sont CABLEES EN DUR dans
// le binaire par maybe_LevelTable_InitFloats 0x4C2380 (12 `mov dword ptr [reg+0x18EC..0x1918],
// imm32`, EA 0x4c238a..0x4c2419), elle-meme appelee uniquement par l'initialiseur statique CRT
// CrtInit_LevelTableThunk 0x7A5260 (xref unique). Les recopier en dur est donc FIDELE.
//
// PIEGE : le commentaire IDB « 12 float scaling constants » est FAUX — ce sont des int32.
// Preuve dure dans UI_GameHud_Render : `fidiv [ebp+var_850]` @0x67A64F (barre) et @0x67A696
// (texte) ; FIDIV n'accepte QUE un operande memoire entier (m32int), et var_850 recoit
// directement le retour de l'accesseur (@0x67a624 call -> @0x67a629 mov [ebp+var_850], eax).
// Corroboration : les 12 valeurs (962M..1481M) tombent sous le plafond d'EXP 0x77359400
// = 2 000 000 000 utilise @0x67a5ea ; lues en float elles vaudraient 2e-4 .. 8.8e14.
//
// `tier` = palier de renaissance (g_SelfLevelBonus 0x16731AC). Reproduit l'accesseur
// 0x4C2BF0 : garde `tier >= 1 && tier <= 12` (@0x4c2bf7 jl / @0x4c2c01 jle) sinon 0 ;
// lecture `[this + 4*tier + 0x18E8]` (@0x4c2c0d) — d'ou tier=1 -> +0x18EC.
int32_t GetRebirthExpSpan(int tier);

// Accesseur type ITEM_INFO. `itemId` 1-based ; index = itemId-1 (cf. MobDb_GetEntry 0x4C3C00,
// `base+436*(id-1)`, garde id!=0). ex-VeryOldClient: ITEM::ReturnDataAddr (CONFIRMED, logique identique).
// Renvoie nullptr hors bornes OU si le slot est vide (champ itemId == 0).
const ItemInfo* GetItemInfo(uint32_t itemId);

// Accesseur type SKILL_INFO. `skillId` 1-based (1..300) ; index = skillId-1 (cf.
// SkillGrowthTbl_GetRecord 0x4C4E90, `base+776*(id-1)`, garde id!=0 & record[0]!=0).
// ex-VeryOldClient: SKILL::ReturnDataAddr (CONFIRMED, logique identique). Renvoie nullptr
// hors bornes OU si le slot est vide (champ skillId == 0).
const SkillInfo* GetSkillInfo(uint32_t skillId);

// Accesseur type MONSTER_INFO. `monsterId` 1-based ; index = monsterId-1 (cf.
// ItemDefTbl_GetRecord 0x4C6570, base+944*(id-1), rejet id<1||id>count, garde 1er dword!=0).
// Pkt_SpawnMonster 0x467B00 passe l'id reseau BRUT (1-based) a ce getter -> l'appelant C++
// manipule un id 1-based, cet accesseur fait le -1. Renvoie nullptr hors bornes OU si le slot
// est vide (champ id == 0). Table = g_World.db.monster.
const MonsterInfo* GetMonsterInfo(uint32_t monsterId);

} // namespace ts2::game

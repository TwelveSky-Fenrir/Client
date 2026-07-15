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
struct LevelInfo {
    uint32_t level;            // +0  1..145 (doit valoir index+1)
    int32_t  expCumul;         // +4  experience cumulee (< expNext ; == expCumul[niv+1]-1)
    int32_t  expNext;          // +8  seuil du niveau suivant (1..2 000 000 000)
    uint32_t meta;             // +12 champ meta (<= 100)
    int32_t  baseExtAttack;    // +16 base attaque externe   (Stat3)
    int32_t  baseIntAttack;    // +20 base attaque interne   (Stat4)
    int32_t  baseExtDefense;   // +24 base defense externe   (Stat5)
    int32_t  baseIntDefense;   // +28 base defense interne   (Stat6)
    int32_t  baseMaxHp;        // +32 base PV max            (Stat7)
    int32_t  baseAtkRatingMin; // +36 base rating min degats (Stat8)
    int32_t  baseAtkRatingMax; // +40 base rating max degats (Stat9)
};
static_assert(sizeof(LevelInfo) == 44, "LevelInfo doit faire 44 o");

// ITEM_INFO — 436 o. Champs prouves par MobDb_ValidateEntry 0x4C2C50 + TS2_GAMEPLAY_LOGIC.md 2.4.
// Les champs `fieldNNN` sont valides (bornes connues) mais leur role reste a documenter.
// Les offsets de stats (+292..+432) sont ceux consommes par le moteur de stats (Char_Calc*).
struct ItemInfo {
    uint32_t itemId;         // +0   1-based ; 0 = slot vide. (== index+1)
    char     name[25];       // +4   nom (terminaison nulle dans [0..24])
    char     model[3][51];   // +29  3 noms de modeles (51 o chacun)
    uint8_t  _pad182[2];     // +182 reserve
    uint32_t category;       // +184 categorie (1..6 ; 5=equip/arme, 6=classe4)
    uint32_t typeCode;       // +188 type (1..36 ; 28=arme, 29=arme elem., 30=monture/costume, 35/36=pet)
    // +192 IconID (1-based, PAS confondre avec itemId/+0) : index dans le pool d'icones
    // g_AssetMgr_ItemIconSlots (dossier G03_GDATA\D01_GIMAGE2D\002\, 4000 slots), formate
    // en "002_%05u.IMG". Confirme par decompilation de cGameHud_Render 0x64A900
    // (`v133 = *(ITEM_INFO+192) - 1` = index pool 0-based ; Sprite2D_BuildPath demontre que
    // le suffixe fichier = index pool 0-based + 1 = IconID tel quel, donc AUCUN offset a
    // appliquer entre ce champ et le numero de fichier). Voir Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md.
    uint32_t iconId;         // +192 (1..10000)
    uint32_t field196;       // +196 (<= 10000)
    uint32_t field200;       // +200 (<= 10000)
    uint32_t itemLevel;      // +204 niveau item (1..145 ; seuils scaling 45/100/113/146)
    uint32_t field208;       // +208 (<= 12)
    uint32_t field212;       // +212 (1..4)
    uint32_t subtype;        // +216 sous-type (1..14 ; 2/4/5/6/7/9=classe1, 11..14=classe2 gemmes)
    uint32_t field220;       // +220 (1..2 000 000 000)
    uint32_t field224;       // +224 (<= 2 000 000 000)
    uint32_t field228;       // +228 (<= 2 000 000 000)
    uint32_t field232;       // +232 (1..145)
    uint32_t field236;       // +236 (<= 12)
    uint32_t flags[11];      // +240 11 drapeaux (chacun 1..2)
    uint32_t skillFlag;      // +284 1=normal, 2=skill, 3=upgrade
    uint32_t field288;       // +288 (< 366)
    int32_t  attrPrimaryA;   // +292 base Force externe (atk/def ext)
    int32_t  attrPrimaryB;   // +296 base Force interne (atk/def int)
    int32_t  attrRatingMin;  // +300 base rating min / def interne (x0.9)
    int32_t  attrRatingMax;  // +304 base rating max
    int32_t  critRate;       // +308 taux crit plat
    int32_t  extAttack;      // +312 attaque externe plate
    int32_t  intAttack;      // +316 attaque interne plate
    int32_t  extDefense;     // +320 defense externe plate
    int32_t  intDefense;     // +324 defense interne plate
    int32_t  maxHp;          // +328 PV max plats
    int32_t  maxMp;          // +332 PM max plats
    int32_t  accuracy;       // +336 precision plate
    uint32_t field340;       // +340 (<= 16)
    uint32_t field344;       // +344 (<= 10000)
    uint32_t field348;       // +348 (<= 300)
    uint32_t field352;       // +352 (<= 100)
    uint32_t field356;       // +356 (<= 1000)
    int32_t  regen;          // +360 regen / reduction % cout MP
    int32_t  evasion;        // +364 esquive plate
    int32_t  resistAll;      // +368 resistance elementaire globale
    struct { int32_t key; int32_t val; } resist[8]; // +372..+432 : 8 paires (cle,valeur)
};
static_assert(sizeof(ItemInfo) == 436, "ItemInfo doit faire 436 o");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// API.
// ---------------------------------------------------------------------------

// Charge les 5 tables .IMG dans g_World.db. `gameDataDir` = racine "GameData"
// (les fichiers sont sous <gameDataDir>\G03_GDATA\D01_GIMAGE2D\005\).
// Renvoie true si TOUTES les tables sont chargees et validees (garde de compteur OK).
// NB : variante TR (dossier 005\TR\, active si g_UseTRVariant==1 dans l'original)
// non geree ici — on charge les tables EU par defaut.
bool LoadGameDatabases(const std::string& gameDataDir);

// Accesseur type LEVEL_INFO. `level` 1-based (1..145) ; nullptr hors bornes.
// (le client indexe record[level-1].)
const LevelInfo* GetLevelInfo(int level);

// Accesseur type ITEM_INFO. `itemId` 1-based ; index = itemId-1 (cf. MobDb_GetEntry 0x4C3C00).
// Renvoie nullptr hors bornes OU si le slot est vide (champ itemId == 0).
const ItemInfo* GetItemInfo(uint32_t itemId);

} // namespace ts2::game

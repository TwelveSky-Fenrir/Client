// Game/GameDatabase.cpp — chargement des tables de donnees .IMG (fidele aux chargeurs
// 0x4C2680/0x4C3930/0x4C4BC0/0x4C62A0/0x4C7390).
//
// Algorithme commun (identique dans les 5 chargeurs du binaire) :
//   1. choisir le chemin : ...\005\<file> ou ...\005\TR\<file> si g_UseTRVariant == 1
//      (0x1669190 — SEULEMENT pour les 3 tables `hasTR` d'ici : ITEM/SKILL/MONSTER)
//   2. lire le fichier ; decoder l'enveloppe [rawSize][packedSize][zlib] -> payload
//   3. count = *(u32*)payload ^ magic
//   4. garde d'integrite : count DOIT valoir la constante attendue en dur (sinon echec)
//   5. copier count*stride octets a partir de payload+header dans DataTable.data
//   6. valider CHAQUE enregistrement (*_ValidateRecord) ; le premier invalide rejette la
//      table EN BLOC (@0x4C64F5..0x4C6527) -> App_Init 0x461C20 avorte « [Error::mXXX.Init()] »
// La table SOCKET a ~9.4 Ko de padding en fin de payload : on ne copie que count*stride
// (le chargeur d'origine alloue exactement 0xECCC = 3031*20 et ignore le reste).
#include "Game/GameDatabase.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>
#include <string>

namespace ts2::game {

namespace {

// Descripteur d'une table (une ligne = un chargeur d'origine).
struct TableSpec {
    const char* file;    // nom du .IMG
    uint32_t    magic;   // cle XOR du compteur
    uint32_t    count;   // compteur attendu (garde d'integrite en dur)
    uint32_t    header;  // offset des enregistrements dans le payload
    uint32_t    stride;  // taille d'un enregistrement
    const char* label;   // nom logique / embarque (journalisation + audit)
    DataTable GameDatabases::* member; // membre cible dans g_World.db
    // hasTR : le chargeur d'origine teste-t-il `cmp ds:g_UseTRVariant, 1` (global 0x1669190) ?
    // data_refs(0x1669190) = 14 refs, dont EXACTEMENT 5 chargeurs de tables : MobDb_LoadImg
    // @0x4C3939, SkillGrowthTbl_LoadImg @0x4C4BC9, ItemDefTbl_LoadImg @0x4C62A9,
    // SkillDefTbl_LoadImg @0x4C6BD9 (ExtraDatabases.cpp), NpcTbl_LoadImg @0x4C8099 (idem).
    // LevelTable_LoadImg 0x4C2680 et AnchorTbl_LoadImg 0x4C7390 n'ont AUCUNE branche TR :
    // le binaire ne lit JAMAIS 005\TR\005_00001.IMG ni 005\TR\005_00010.IMG, meme si le
    // fichier existe sur disque. Ne PAS « ameliorer » (fidelite).
    bool        hasTR;
    // Validateur d'enregistrement du binaire (nullptr = non transcrit). Signature calquee sur
    // QuestTbl_ValidateRecord (Game/QuestSystem.cpp:74), seul validateur deja en place avant W9.
    bool (*validate)(const DataTable&, int);
};

// ---------------------------------------------------------------------------
// Lecteurs bruts d'enregistrement.
// `memcpy` obligatoire : `data` est une copie octet a octet du payload .IMG, sans aucune
// garantie d'alignement (les strides 436/776/944 ne sont pas tous multiples de 4).
// La distinction I32/U32 n'est PAS cosmetique : les validateurs d'origine melangent
// `cmp` signes (jl/jg) et non signes (ja/jb) sur les MEMES champs — on transcrit le
// signe de chaque comparaison tel qu'il apparait dans le desassemblage.
// ---------------------------------------------------------------------------
inline int32_t RecI32(const uint8_t* r, size_t off) {
    int32_t v; std::memcpy(&v, r + off, 4); return v;
}
inline uint32_t RecU32(const uint8_t* r, size_t off) {
    uint32_t v; std::memcpy(&v, r + off, 4); return v;
}
// Motif `for (i = 0; i < N && rec[off+i]; ++i); if (i == N) return 0;` present dans les 5
// validateurs a champs texte (ITEM/SKILL/MONSTER ici, NPC/QUEST dans ExtraDatabases.cpp ;
// LEVEL et SOCKET n'ont aucune chaine) : echoue si AUCUN octet nul dans [off, off+maxLen).
inline bool RecHasNul(const uint8_t* r, size_t off, size_t maxLen) {
    for (size_t i = 0; i < maxLen; ++i)
        if (r[off + i] == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Validateurs d'enregistrement — transcription littérale des validateurs du binaire.
// Chaque chargeur d'origine boucle sur TOUS ses enregistrements et renvoie 0 au premier
// invalide (cf. la boucle @0x4C64F5..0x4C6527 citee dans LoadOneTable).
// ---------------------------------------------------------------------------

// LevelTable_ValidateEntry 0x4C2430 — LEVEL_INFO, 44 o = 11 dwords.
bool ValidateLevel(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // NB : ce validateur N'A PAS d'accept anticipe « slot vide » (contrairement aux 6 autres) —
    // il attaque directement la borne d'id @0x4c2457. La table LEVEL est dense (145/145).
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 145) return false;             /*0x4c2457*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c2472*/
    if (RecU32(r, 4) > 0x77359400u) return false;                         /*0x4c2489*/
    if (RecI32(r, 8) < 1 || RecI32(r, 8) > 2000000000) return false;      /*0x4c24c6*/
    if (RecI32(r, 4) >= RecI32(r, 8)) return false;                       /*0x4c24e9*/
    // Garde INTER-ENREGISTREMENTS @0x4c251b — la seule du binaire qui lit le record SUIVANT :
    //   `if (a2 < 144 && *(this + 11*a2 + 2) != *(this + 11*a2 + 12) - 1) return 0;`
    // dword[11*a2+12] == dword[11*(a2+1)+1], soit l'offset +4 du record a2+1. Autrement dit
    // expNext[n] doit valoir expCumul[n+1] - 1.
    if (row0 < 144) {
        // La garde de compteur (count == 145) garantit record(row0+1) != nullptr ici.
        const uint8_t* next = t.record(static_cast<uint32_t>(row0 + 1));
        if (next && RecI32(r, 8) != RecI32(next, 4) - 1) return false;
    }
    if (RecU32(r, 12) > 0x64u) return false;                              /*0x4c2532*/
    for (size_t off = 16; off <= 40; off += 4)                            /*0x4c2559..0x4c264c*/
        if (RecU32(r, off) > 0x2710u) return false;
    return true;                                                          /*0x4c264c*/
}

// MobDb_ValidateEntry 0x4C2C50 — ITEM_INFO, 436 o.
bool ValidateItem(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    // ACCEPT ANTICIPE « slot vide » @0x4c2c68 : `if (!*(DWORD*)(436*a2 + records)) return 1;`
    // CRITIQUE — la table ITEM declare 99999 enregistrements dont l'immense majorite est
    // vide : sans cet accept, la boucle de validation rejetterait la table des le premier
    // trou et LoadGameDatabases renverrait false sur des donnees pourtant saines.
    if (RecU32(r, 0) == 0) return true;
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 99999) return false;           /*0x4c2ca3*/
    if (RecI32(r, 0) != row0 + 1) return false;                           /*0x4c2cc4*/
    if (!RecHasNul(r, 4, 25)) return false;                               /*0x4c2ccd nom[25]*/
    for (size_t j = 0; j < 3; ++j)                                        /*0x4c2d13 3 x 51*/
        if (!RecHasNul(r, 29 + 51 * j, 51)) return false;
    if (RecI32(r, 184) < 1 || RecI32(r, 184) > 6) return false;           /*0x4c2da9*/
    if (RecI32(r, 188) < 1 || RecI32(r, 188) > 36) return false;          /*0x4c2de2*/
    if (RecI32(r, 192) < 1 || RecI32(r, 192) > 10000) return false;       /*0x4c2e1e*/
    if (RecU32(r, 196) > 0x2710u) return false;                           /*0x4c2e3e*/
    if (RecU32(r, 200) > 0x2710u) return false;                           /*0x4c2e7a*/
    if (RecI32(r, 204) < 1 || RecI32(r, 204) > 145) return false;         /*0x4c2ed2*/
    if (RecU32(r, 208) > 0xCu) return false;                              /*0x4c2ef2*/
    if (RecI32(r, 212) < 1 || RecI32(r, 212) > 4) return false;           /*0x4c2f44*/
    if (RecI32(r, 216) < 1 || RecI32(r, 216) > 14) return false;          /*0x4c2f7d*/
    if (RecI32(r, 220) < 1 || RecI32(r, 220) > 2000000000) return false;  /*0x4c2fb9*/
    if (RecU32(r, 224) > 0x77359400u) return false;                       /*0x4c2fd9*/
    if (RecU32(r, 228) > 0x77359400u) return false;                       /*0x4c3015*/
    if (RecI32(r, 232) < 1 || RecI32(r, 232) > 145) return false;         /*0x4c306d*/
    if (RecU32(r, 236) > 0xCu) return false;                              /*0x4c308d*/
    // 11 champs (+240..+280), chacun [1,2] — cf. ItemInfo::flags[11].
    for (size_t off = 240; off <= 280; off += 4)                          /*0x4c30df..0x4c3319*/
        if (RecI32(r, off) < 1 || RecI32(r, off) > 2) return false;
    if (RecI32(r, 284) < 1 || RecI32(r, 284) > 3) return false;           /*0x4c3352*/
    if (RecU32(r, 288) >= 0x16Eu) return false;                           /*0x4c3372 (< 366)*/
    for (size_t off = 292; off <= 308; off += 4)                          /*0x4c33ae..0x4c34bc*/
        if (RecU32(r, off) > 0x2710u) return false;
    if (RecU32(r, 312) > 0x4E20u) return false;                           /*0x4c34da*/
    if (RecU32(r, 316) > 0x2710u) return false;                           /*0x4c3516*/
    if (RecU32(r, 320) > 0x4E20u) return false;                           /*0x4c3552*/
    if (RecU32(r, 324) > 0x2710u) return false;                           /*0x4c358e*/
    if (RecU32(r, 328) > 0x2710u) return false;                           /*0x4c35ca*/
    if (RecU32(r, 332) > 0x2710u) return false;                           /*0x4c3606*/
    if (RecU32(r, 336) > 0x64u) return false;                             /*0x4c3642*/
    if (RecU32(r, 340) > 0x10u) return false;                             /*0x4c367b*/
    if (RecU32(r, 344) > 0x2710u) return false;                           /*0x4c36b4*/
    // Garde CONDITIONNELLE @0x4c3722 : field340 == 9 => field344 doit valoir [1,3].
    if (RecU32(r, 340) == 9 && (RecI32(r, 344) < 1 || RecI32(r, 344) > 3)) return false;
    if (RecU32(r, 348) > 0x12Cu) return false;                            /*0x4c3742*/
    if (RecU32(r, 352) > 0x64u) return false;                             /*0x4c377e*/
    if (RecU32(r, 356) > 0x3E8u) return false;                            /*0x4c37b7*/
    if (RecU32(r, 360) > 0x64u) return false;                             /*0x4c37f3*/
    if (RecU32(r, 364) > 0x64u) return false;                             /*0x4c382c*/
    if (RecU32(r, 368) > 0x64u) return false;                             /*0x4c3865*/
    for (size_t m = 0; m < 8; ++m) {                                      /*0x4c3887 resist[8]*/
        if (RecU32(r, 372 + 8 * m) > 0x12Cu) return false;                /*0x4c38bd*/
        if (RecU32(r, 376 + 8 * m) > 0x64u) return false;                 /*0x4c38fc*/
    }
    return true;                                                          /*0x4c3928*/
}

// Bornes des 2 blocs de 25 stats de SKILL_INFO (+576 statMin / +676 statMax). Le validateur
// 0x4C4160 les teste via une boucle `for (m = 0; m < 2; ++m)` sur la base `576 + 100*m`,
// avec une borne DIFFERENTE par indice de stat (relevees une a une dans le decompile).
const uint32_t kSkillStatMax[25] = {
    0x2710, 0x2710, 0x2710, 0x64,   0x64,    // +576 +580 +584 +588 +592
    0x3E8,  0x3E8,  0x3E8,  0x3E8,  0x2710,  // +596 +600 +604 +608 +612
    0x3E8,  0x3E8,  0x3E8,  0x3E8,  0x3E8,   // +616 +620 +624 +628 +632
    0x3E8,  0x3E8,  0x3E8,  0x3E8,  0x3E8,   // +636 +640 +644 +648 +652
    0x3E8,  0x3E8,  0x2710, 0x3E8,  0x2710,  // +656 +660 +664 +668 +672
};

// SkillGrowthTbl_ValidateRecord 0x4C4160 — SKILL_INFO, 776 o.
bool ValidateSkill(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    if (RecU32(r, 0) == 0) return true;                                   // accept slot vide
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 300) return false;
    if (RecI32(r, 0) != row0 + 1) return false;
    if (!RecHasNul(r, 4, 25)) return false;                               // nom[25]
    for (size_t j = 0; j < 10; ++j)                                       // description[10][51]
        if (!RecHasNul(r, 29 + 51 * j, 51)) return false;
    if (RecI32(r, 540) < 1 || RecI32(r, 540) > 4) return false;
    if (RecI32(r, 544) < 1 || RecI32(r, 544) > 5) return false;
    if (RecI32(r, 548) < 1 || RecI32(r, 548) > 10000) return false;
    if (RecI32(r, 552) < 1 || RecI32(r, 552) > 4) return false;
    if (RecI32(r, 556) < 1 || RecI32(r, 556) > 10) return false;
    if (RecI32(r, 560) < 1 || RecI32(r, 560) > 1000) return false;
    if (RecI32(r, 564) < 1 || RecI32(r, 564) > 1000) return false;
    if (RecU32(r, 568) > 0xAu) return false;
    if (RecU32(r, 572) > 0x3E8u) return false;
    for (size_t m = 0; m < 2; ++m)                                        // statMin puis statMax
        for (size_t k = 0; k < 25; ++k)
            if (RecU32(r, 576 + 100 * m + 4 * k) > kSkillStatMax[k]) return false;
    return true;
}

// ItemDefTbl_ValidateRecord 0x4C5350 — MONSTER_INFO, 944 o (misnomer IDB : valide un MONSTRE).
bool ValidateMonster(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    if (RecU32(r, 0) == 0) return true;                                   // accept slot vide
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 10000) return false;
    if (RecI32(r, 0) != row0 + 1) return false;
    if (!RecHasNul(r, 4, 25)) return false;                               // nom[25]
    for (size_t j = 0; j < 2; ++j)                                        // textBlock[2][101]
        if (!RecHasNul(r, 29 + 101 * j, 101)) return false;
    if (RecI32(r, 232) < 1 || RecI32(r, 232) > 15) return false;
    if (RecI32(r, 236) < 1 || RecI32(r, 236) > 51) return false;
    if (RecI32(r, 240) < 1 || RecI32(r, 240) > 2) return false;
    if (RecI32(r, 244) < 1 || RecI32(r, 244) > 10000) return false;       // kindIndexP1
    for (size_t m = 0; m < 3; ++m)                                        // collDim[3]
        if (RecI32(r, 248 + 4 * m) < 1 || RecI32(r, 248 + 4 * m) > 1000) return false;
    if (RecU32(r, 260) > 0x3E8u) return false;
    if (RecI32(r, 264) < 1 || RecI32(r, 264) > 4) return false;
    if (RecI32(r, 268) < 1 || RecI32(r, 268) > 2) return false;
    for (size_t n = 0; n < 6; ++n)                                        // field272[6]
        if (RecI32(r, 272 + 4 * n) < 1 || RecI32(r, 272 + 4 * n) > 10000) return false;
    if (RecU32(r, 296) >= 4u) return false;
    for (size_t i = 0; i < 3; ++i)                                        // field300[3]
        if (RecU32(r, 300 + 4 * i) > 0x2710u) return false;
    if (RecU32(r, 312) >= 4u) return false;
    for (size_t i = 0; i < 3; ++i)                                        // field316[3]
        if (RecU32(r, 316 + 4 * i) > 0x2710u) return false;
    if (RecI32(r, 328) < 1 || RecI32(r, 328) > 10000) return false;
    if (RecI32(r, 332) < 1 || RecI32(r, 332) > 10000) return false;
    if (RecI32(r, 336) < 1 || RecI32(r, 336) > 1000000) return false;     // rewardMin
    if (RecI32(r, 340) < 1 || RecI32(r, 340) > 1000000) return false;     // rewardMax
    if (RecU32(r, 336) > RecU32(r, 340)) return false;                    // min <= max (non signe)
    if (RecI32(r, 344) < 1 || RecI32(r, 344) > 145) return false;         // level
    if (RecU32(r, 348) > 0xCu) return false;
    if (RecI32(r, 352) < 1 || RecI32(r, 352) > 1000) return false;
    if (RecU32(r, 356) > 0x3E8u) return false;
    if (RecU32(r, 360) > 0x5F5E100u) return false;
    if (RecU32(r, 364) > 0x5F5E100u) return false;
    if (RecI32(r, 368) < 1 || RecI32(r, 368) > 2000000000) return false;  // hpMax
    if (RecI32(r, 372) < 1 || RecI32(r, 372) > 6) return false;
    if (RecU32(r, 376) > 0x2710u) return false;
    if (RecU32(r, 380) > 0x2710u) return false;
    if (RecU32(r, 376) > RecU32(r, 380)) return false;
    if (RecU32(r, 384) > 0x3E8u) return false;
    if (RecU32(r, 388) > 0x3E8u) return false;
    if (RecU32(r, 392) > 0x3E8u) return false;
    if (RecU32(r, 396) > 0x493E0u) return false;                          // <= 300000
    if (RecU32(r, 400) > 0x30D40u) return false;                          // <= 200000
    for (size_t i = 0; i < 4; ++i)                                        // field404[4]
        if (RecU32(r, 404 + 4 * i) > 0x186A0u) return false;
    if (RecU32(r, 420) > 0x64u) return false;
    if (RecU32(r, 424) > 0x64u) return false;
    if (RecU32(r, 428) > 0x64u) return false;
    if (RecU32(r, 424) > RecU32(r, 428)) return false;
    if (RecI32(r, 432) < 0) return false;
    // Garde composite du binaire : field432/100 doit valoir 2, 3, 4 ou 5 — SAUF si field432
    // est nul (le dernier terme `&& *(...+432)` autorise la valeur 0 en bloc).
    {
        const uint32_t v432 = RecU32(r, 432);
        const uint32_t q = v432 / 100u;
        if (q != 2u && q != 3u && q != 4u && q != 5u && v432 != 0u) return false;
        if (q > 15u) return false;
    }
    if (RecU32(r, 436) > 0xF4240u) return false;
    if (RecU32(r, 440) > 0x5F5E100u) return false;                        // goldMin
    if (RecU32(r, 444) > 0x5F5E100u) return false;                        // goldMax
    if (RecU32(r, 440) > RecU32(r, 444)) return false;
    for (size_t k = 0; k < 5; ++k) {                                      // dropA[5]
        if (RecU32(r, 448 + 8 * k) > 0xF4240u) return false;
        if (RecU32(r, 452 + 8 * k) >= 0x186A0u) return false;
    }
    for (size_t m = 0; m < 12; ++m)                                       // field488[12]
        if (RecU32(r, 488 + 4 * m) > 0xF4240u) return false;
    if (RecU32(r, 536) > 0xF4240u) return false;
    if (RecU32(r, 540) >= 0x186A0u) return false;
    for (size_t n = 0; n < 50; ++n) {                                     // dropB[50]
        if (RecU32(r, 544 + 8 * n) > 0xF4240u) return false;
        if (RecU32(r, 548 + 8 * n) >= 0x186A0u) return false;
    }
    return true;
}

// AnchorTbl_ValidateRecord 0x4C6F20 — SOCKET_INFO, 20 o {type,+4,attachId,offX,offY}.
bool ValidateSocket(const DataTable& t, int row0) {
    const uint8_t* r = t.record(static_cast<uint32_t>(row0));
    if (!r) return false;
    if (RecU32(r, 8) == 0) return true;                                   /*0x4c6f33*/
    if (RecU32(r, 0) == 0) return true;                                   /*0x4c6f50*/
    if (RecI32(r, 0) < 1 || RecI32(r, 0) > 54) return false;              /*0x4c6f82*/
    // BUG D'ORIGINE CONSERVE (regle de fidelite) : le binaire teste `< 0 ET > 10000`, une
    // conjonction qui ne peut JAMAIS etre vraie -> garde MORTE. Verifie au desassemblage et
    // non seulement au decompile : `cmp [+4], 0 / jge 4C6FBB` @0x4c6f97-0x4c6f9c puis
    // `cmp [+4], 2710h / jle 4C6FBB` @0x4c6faa-0x4c6fb2 (les deux sorties menent au meme
    // label de succes). L'auteur voulait vraisemblablement `||` : ne PAS « corriger ».
    if (RecI32(r, 4) < 0 && RecI32(r, 4) > 10000) return false;           /*0x4c6fb2*/
    if (RecU32(r, 0) == 1) {                                              /*0x4c6fcb*/
        if (RecI32(r, 8) < 1 || RecI32(r, 8) > 33) return false;          /*0x4c6ff5*/
        if (RecU32(r, 12) > 0x190u) return false;                         /*0x4c700f*/
        if (RecU32(r, 16) > 0x190u) return false;                         /*0x4c703f*/
    } else if (RecI32(r, 0) <= 1 || RecI32(r, 0) >= 30) {                 /*0x4c7089*/
        if (RecI32(r, 0) >= 30 && RecI32(r, 0) <= 38) {                   /*0x4c7147*/
            if (RecI32(r, 8) < 1 || RecI32(r, 8) > 2) return false;       /*0x4c716d*/
            if (RecU32(r, 12) > 0x32u) return false;                      /*0x4c7187*/
            if (RecU32(r, 16) != 0) return false;                         /*0x4c71af*/
        } else if (RecI32(r, 0) >= 39 && RecI32(r, 0) <= 42) {            /*0x4c71e4*/
            if (RecI32(r, 8) < 1 || RecI32(r, 8) > 10) return false;      /*0x4c720a*/
            if (RecI32(r, 12) < 1) return false;                          /*0x4c7224*/
            if (RecU32(r, 16) != 0) return false;                         /*0x4c7239*/
        } else if (RecI32(r, 0) >= 43 && RecI32(r, 0) <= 46) {            /*0x4c726e*/
            if (RecI32(r, 8) < 1 || RecI32(r, 8) > 10) return false;      /*0x4c7294*/
            if (RecI32(r, 12) < 6) return false;                          /*0x4c72ae*/
            if (RecU32(r, 16) != 0) return false;                         /*0x4c72c3*/
        } else if (RecI32(r, 0) >= 47 && RecI32(r, 0) <= 54) {            /*0x4c72fc*/
            if (RecI32(r, 8) < 11 || RecI32(r, 8) > 45) return false;     /*0x4c7322*/
            if (RecI32(r, 12) < -5 || RecI32(r, 12) > 15) return false;   /*0x4c734c*/
            if (RecI32(r, 16) < -4 || RecI32(r, 16) > 65) return false;   /*0x4c7376*/
        }
        // Aucune garde pour les types hors {1, 30..54} : le binaire tombe en `return 1`.
    } else {                                                              // type dans [2, 29]
        if (RecI32(r, 8) < 1 || RecI32(r, 8) > 100) return false;         /*0x4c70b3*/
        if (RecU32(r, 12) > 0x3E8u) return false;                         /*0x4c70cd*/
        if (RecU32(r, 16) > 0x3E8u) return false;                         /*0x4c70fd*/
    }
    return true;                                                          /*0x4c7381*/
}

// Ordre et constantes preleves dans le desassemblage (Docs/TS2_IMG_FORMAT.md 4.1).
// magic/count/header/stride = VERITE IDA (jamais transposes de VeryOld). Cross-check classes
// VeryOldClient (noms de classe seulement, Docs/TS2_TABLES_ROSETTA.md §1) ; ATTENTION: MISNOMERS IDB :
// MobDb_LoadImg=ITEM, ItemDefTbl_LoadImg=MONSTER, AnchorTbl_LoadImg=SOCKET (decalage d'un cran).
const TableSpec kTables[] = {
    // LevelTable_LoadImg 0x4C2680 -> mLEVEL 0x8E7208. ex-VeryOldClient: LEVEL/CLEVEL.cpp (CONFIRMED)
    // hasTR=false : AUCUNE branche TR (absent des 14 data_refs de g_UseTRVariant 0x1669190).
    { "005_00001.IMG", 0x0E31,   145,  34,  44, "LEVEL_INFO",   &GameDatabases::level,   false, &ValidateLevel   },
    // MobDb_LoadImg 0x4C3930 (misnomer) -> mITEM 0x8E71EC. ex-VeryOldClient: ITEM/CITEM.cpp (CONFIRMED)
    // hasTR=true : `cmp ds:g_UseTRVariant, 1` @0x4C3939 -> ...\005\TR\005_00002.IMG (0x7A704C).
    { "005_00002.IMG", 0x1CB3, 99999,  67, 436, "ITEM_INFO",    &GameDatabases::item,    true,  &ValidateItem    },
    // SkillGrowthTbl_LoadImg 0x4C4BC0. ex-VeryOldClient: SKILL/CSKILL.cpp (CONFIRMED)
    // hasTR=true : cmp @0x4C4BC9 -> ...\005\TR\005_00003.IMG (0x7A70A4).
    { "005_00003.IMG", 0x0C7E,   300,  84, 776, "SKILL_INFO",   &GameDatabases::skill,   true,  &ValidateSkill   },
    // ItemDefTbl_LoadImg 0x4C62A0 (misnomer) -> mMONSTER 0x8E71FC. ex-VeryOldClient: MONSTER (CONFIRMED)
    // hasTR=true : cmp @0x4C62A9 -> ...\005\TR\005_00004.IMG (0x7A7104), branche EU @0x4c62f1.
    { "005_00004.IMG", 0x1583, 10000,  88, 944, "MONSTER_INFO", &GameDatabases::monster, true,  &ValidateMonster },
    // AnchorTbl_LoadImg 0x4C7390 (misnomer) -> mSOCKET 0x8E71D0. ex-VeryOldClient: GSOCKET/CSOCKET.cpp
    // (CONFIRMED table ; ATTENTION: CONFLICT J-4 sur le count : IDA=3031 vs VeryOld 2891 valides/3000 fixes — IDA gagne).
    // hasTR=false : AUCUNE branche TR (cf. LEVEL) — 005\TR\005_00010.IMG n'est jamais lu.
    { "005_00010.IMG", 0x0FDB,  3031, 103,  20, "SOCKET_INFO",  &GameDatabases::socketT, false, &ValidateSocket  },
};

// Sous-dossier commun a toutes les tables (chemins en dur dans le binaire).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Charge une table dans `out`. Renvoie false en cas d'echec.
// `useTR` = etat de g_UseTRVariant 0x1669190 (cmdline champ 1) ; seules les tables `hasTR`
// basculent sur le sous-dossier localise.
// NB : en cas d'echec de la BOUCLE DE VALIDATION, `out` a deja ete peuple — c'est fidele au
// binaire, qui publie `*this = count` / `*(this+1) = records` (@0x4c64ea/@0x4c64f2) AVANT
// d'entrer dans la boucle @0x4c64f5. Seuls les echecs anterieurs laissent `out` inchange.
bool LoadOneTable(const std::string& gameDataDir, const TableSpec& s, DataTable& out, bool useTR) {
    // Variante TR : les 5 chargeurs concernes ouvrent un chemin ENTIEREMENT distinct quand
    // g_UseTRVariant == 1. Modele : ItemDefTbl_LoadImg 0x4C62A0 —
    //   `if (g_UseTRVariant == 1)` @0x4c62a9/@0x4c62b0
    //     CreateFileA("G03_GDATA\D01_GIMAGE2D\005\TR\005_00004.IMG")  @0x4c62cf
    //   else
    //     CreateFileA("G03_GDATA\D01_GIMAGE2D\005\005_00004.IMG")     @0x4c62f1
    // Le choix est PAR TABLE (`s.hasTR`), pas global : cf. le champ hasTR de TableSpec.
    const std::string file = (useTR && s.hasTR) ? std::string("TR\\") + s.file
                                                : std::string(s.file);
    const std::string path = Join(Join(gameDataDir, kTablesDir), file);

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("DB %s : .IMG illisible : %s", s.label, path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() < 4) {
        TS2_ERR("DB %s : payload trop court (%zu o)", s.label, payload.size());
        return false;
    }

    // count = premier dword XOR magic (cf. Crt_Memcpy(&v, hMem, 4); v ^ magic).
    // GAP-1 (XOR-magic garde du compteur, famille D) : cette garde vit DEJA au bon endroit —
    // dans le chargeur de table, JAMAIS dans Asset/ImgFile (qui s'arrete a l'enveloppe
    // [rawSize][packedSize][zlib]). 5 magics ici (0xE31/0x1CB3/0xC7E/0x1583/0xFDB), le 6e (NPC
    // 0x1022) dans ExtraDatabases.cpp. ATTENTION: magics VeryOld DIFFERENTS (build alt.) — NE PAS
    // transposer, NE PAS deplacer/reimplementer cette passe. Cf. Docs/TS2_TABLES_ROSETTA.md §11 GAP-1.
    uint32_t first = 0;
    std::memcpy(&first, payload.data(), 4);
    const uint32_t count = first ^ s.magic;

    // Garde d'integrite : le chargeur d'origine echoue si count != constante attendue.
    if (count != s.count) {
        TS2_ERR("DB %s : compteur invalide (%u, attendu %u) — table alteree ?",
                s.label, count, s.count);
        return false;
    }

    const size_t need = static_cast<size_t>(s.header)
                      + static_cast<size_t>(count) * s.stride;
    if (payload.size() < need) {
        TS2_ERR("DB %s : payload tronque (%zu < %zu o)", s.label, payload.size(), need);
        return false;
    }

    // Copie des enregistrements SEULS (count*stride octets depuis l'offset header).
    const uint8_t* rec = payload.data() + s.header;
    out.data.assign(rec, rec + static_cast<size_t>(count) * s.stride);
    out.count  = count;
    out.stride = s.stride;

    // Boucle de validation EN BLOC — ItemDefTbl_LoadImg @0x4C64F5..0x4C6527 :
    //   `for (i = 0; i < *this; ++i) if (!ItemDefTbl_ValidateRecord(this, i)) return 0;`
    //   `return 1;`  (echec @0x4c6523, succes @0x4c6527)
    // Motif identique dans les 8 chargeurs du binaire. Un SEUL enregistrement invalide fait
    // echouer le chargeur entier -> App_Init 0x461C20 avorte sur « [Error::mXXX.Init()] ».
    // La table est donc rejetee EN BLOC, jamais partiellement.
    // IMPERATIF : cette boucle vient APRES l'affectation de out.count/out.stride ci-dessus —
    // les validateurs indexent via out.record(i), qui depend des deux.
    if (s.validate) {
        for (uint32_t i = 0; i < out.count; ++i) {
            if (!s.validate(out, static_cast<int>(i))) {
                TS2_ERR("DB %s : enregistrement %u invalide — table rejetee", s.label, i);
                return false;
            }
        }
    }

    // Audit IMG-TRUTH : le nom embarque doit COMMENCER par le label attendu.
    // Comparaison en PREFIXE et non en egalite : les .IMG REPETENT le nom pour remplir
    // l'en-tete, d'ou header == 4 + strlen(label) * k avec k entier — verifie sur les 5
    // constantes header en dur des chargeurs (colonne `header` de kTables) :
    //   LEVEL_INFO 4+10*3=34 · ITEM_INFO 4+9*7=67 · SKILL_INFO 4+10*8=84
    //   MONSTER_INFO 4+12*7=88 · SOCKET_INFO 4+11*9=103   (5/5 divisions exactes)
    // Asset/ImgFile.cpp:61 lit 30 octets bruts sans borner sur la periode : TableName() vaut
    // donc p.ex. "ITEM_INFOITEM_INFOITEM_INFOITE" ou "MONSTER_INFOMONSTER_INFOMONSTE".
    // Une egalite stricte etait TOUJOURS fausse -> TS2_WARN systematique sur les 5 tables
    // (l'audit ne detectait rien et noyait le log). Le prefixe, lui, tient sur les 5.
    // NB : le binaire ne fait AUCUN audit de nom — il saute l'en-tete par la constante en dur
    // (`v10 = 88` @0x4C647D puis `Crt_Memcpy(v4, hMem + v10, 944 * v12)` @0x4C64C8). Cet audit
    // est une addition C++ ; les constantes header ne se derivent PAS du nom, ne pas y toucher.
    const std::string& embedded = img.TableName();
    if (!embedded.empty() && embedded.rfind(s.label, 0) != 0)
        TS2_WARN("DB %s : nom embarque different = '%s'", s.label, embedded.c_str());

    TS2_LOG("DB %s : %u enregistrements x %u o", s.label, out.count, out.stride);
    return true;
}

} // namespace

bool LoadGameDatabases(const std::string& gameDataDir, bool useTR) {
    bool allOk = true;
    for (const TableSpec& s : kTables) {
        DataTable& t = g_World.db.*(s.member);
        if (!LoadOneTable(gameDataDir, s, t, useTR))
            allOk = false; // on tente quand meme les autres tables
    }
    return allOk;
}

const LevelInfo* GetLevelInfo(int level) {
    if (level < 1) return nullptr;
    const uint8_t* r = g_World.db.level.record(static_cast<uint32_t>(level - 1));
    return reinterpret_cast<const LevelInfo*>(r); // nullptr si hors bornes
}

// ---------------------------------------------------------------------------
// GetRebirthExpSpan — sous-table EXP de renaissance (mLEVEL 0x8E7208 + 0x18EC).
// Voir GameDatabase.h pour la preuve complete (int32 et non float : fidiv @0x67A64F).
//
// Table recopiee a l'octet depuis maybe_LevelTable_InitFloats 0x4C2380 (disasm relu cette
// mission), immediats des 12 `mov dword ptr [reg+off], imm32` :
//   tier  off      imm32 (hex)   valeur int32
//    1   0x18EC    39589228h      962105896   [EA 0x4c238a]
//    2   0x18F0    3BA3CB33h     1000590131   [EA 0x4c2397]
//    3   0x18F4    3E068168h     1040613736   [EA 0x4c23a4]
//    4   0x18F8    4081A54Dh     1082238285   [EA 0x4c23b1]
//    5   0x18FC    43163108h     1125527816   [EA 0x4c23be]
//    6   0x1900    45C528C0h     1170548928   [EA 0x4c23cb]
//    7   0x1904    488F9B05h     1217370885   [EA 0x4c23d8]
//    8   0x1908    4B76A138h     1266065720   [EA 0x4c23e5]
//    9   0x190C    4E7B5FFCh     1316708348   [EA 0x4c23f2]
//   10   0x1910    519F07A9h     1369376681   [EA 0x4c23ff]
//   11   0x1914    54E2D4C4h     1424151748   [EA 0x4c240c]
//   12   0x1918    58481079h     1481117817   [EA 0x4c2419]
// ---------------------------------------------------------------------------
int32_t GetRebirthExpSpan(int tier) {
    // Garde de l'accesseur 0x4C2BF0 : hors [1..12] -> 0 (@0x4c2bf7 jl / @0x4c2c01 jle,
    // branche loc_4C2C03 `xor eax, eax`).
    if (tier < 1 || tier > 12) return 0;
    // Indice 1-based (le binaire lit [this + 4*tier + 0x18E8], soit +0x18EC pour tier=1).
    static const int32_t kRebirthExpSpan[13] = {
        0,           // [0] inutilise (le binaire n'y accede jamais : garde tier>=1)
        962105896,   // tier 1  — 39589228h
        1000590131,  // tier 2  — 3BA3CB33h
        1040613736,  // tier 3  — 3E068168h
        1082238285,  // tier 4  — 4081A54Dh
        1125527816,  // tier 5  — 43163108h
        1170548928,  // tier 6  — 45C528C0h
        1217370885,  // tier 7  — 488F9B05h
        1266065720,  // tier 8  — 4B76A138h
        1316708348,  // tier 9  — 4E7B5FFCh
        1369376681,  // tier 10 — 519F07A9h
        1424151748,  // tier 11 — 54E2D4C4h
        1481117817,  // tier 12 — 58481079h
    };
    return kRebirthExpSpan[tier];
}

const ItemInfo* GetItemInfo(uint32_t itemId) {
    if (itemId < 1) return nullptr;
    const uint8_t* r = g_World.db.item.record(itemId - 1);
    const ItemInfo* it = reinterpret_cast<const ItemInfo*>(r);
    // Slot vide (itemId == 0) => introuvable, comme MobDb_GetEntry 0x4C3C00.
    return (it && it->itemId != 0) ? it : nullptr;
}

const SkillInfo* GetSkillInfo(uint32_t skillId) {
    if (skillId < 1) return nullptr;
    const uint8_t* r = g_World.db.skill.record(skillId - 1);
    const SkillInfo* sk = reinterpret_cast<const SkillInfo*>(r);
    // Slot vide (skillId == 0) => introuvable, comme SkillGrowthTbl_GetRecord 0x4C4E90
    // (`if (*(base+776*(id-1))) return ... ; return 0;` — teste le 1er dword du record).
    return (sk && sk->skillId != 0) ? sk : nullptr;
}

const MonsterInfo* GetMonsterInfo(uint32_t monsterId) {
    if (monsterId < 1) return nullptr;                          // ItemDefTbl_GetRecord 0x4C6570 : a2<1 => 0
    const uint8_t* r = g_World.db.monster.record(monsterId - 1); // base+944*(id-1) ; record() gere id>count
    const MonsterInfo* mi = reinterpret_cast<const MonsterInfo*>(r);
    // Slot vide (id==0) => introuvable, comme la garde 1er dword de 0x4C6570
    // (`if (*(base+944*(id-1))) return ... ; return 0;`).
    return (mi && mi->id != 0) ? mi : nullptr;
}

} // namespace ts2::game

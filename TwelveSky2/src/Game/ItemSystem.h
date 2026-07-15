// Game/ItemSystem.h — système d'items du client TS2 : lecture d'un enregistrement
// ITEM_INFO (436 o), décodage des bitfields du mot socket/gemme, et calcul des
// bonus (scaling par niveau, sockets, gemmes, grade, enchant, élément).
//
// Réécriture C++ PROPRE, mais NUMÉRIQUEMENT FIDÈLE au désassemblage de
// TwelveSky2.exe (les tables/constantes sont recopiées à l'octet/flottant près).
// Vérité : Docs/TS2_GAMEPLAY_LOGIC.md §2 (stats items) + IDB idaTs2.
//
// Correspondance fonction ↔ adresse d'origine :
//   Item_GetAttribByte0..3   0x545610 / 0x545640 / 0x545670 / 0x5456A0
//   Item_GetScaledStat       0x545980
//   Item_SocketBonusInt      0x4CA620      Item_SocketBonusFloat   0x4CAC30
//   Item_GetElementalBonus   0x54F590
//   Item_DecodeGemBonus      0x54D390
//   Item_GetGradeValue       0x54D750      Item_GetGradeMultiplier 0x54D9A0
//   Item_GetEnchantStatDelta 0x553D50
//   Item_ScaleStatByTypeA/B/C/D  0x4C91B0 / 0x4C95C0 / 0x4CB0D0 / 0x4CB3F0
//   ItemLookup (MobDb_GetEntry)  0x4C3C00
#pragma once
#include <cstdint>
#include <cstring>
#include "Game/GameState.h"

namespace ts2::game {

// =====================================================================
// Vue en lecture seule sur un enregistrement ITEM_INFO (stride 436 o).
// Les offsets proviennent du moteur de stats (Char_Calc*). Cf. doc §2.4.
// =====================================================================
namespace ItemOff {
    constexpr uint32_t kItemId       = 0;    // id (plages 201-218/2303-2305 = élément)
    constexpr uint32_t kCategory     = 184;  // idx46 : 5=équip/arme, 6=classe4
    constexpr uint32_t kTypeCode     = 188;  // idx47 : 28=arme, 29=arme élém., 30=monture…
    constexpr uint32_t kItemLevel    = 204;  // seuils de scaling 45/100/113/146
    constexpr uint32_t kSubtype      = 216;  // idx54
    constexpr uint32_t kSkillFlag    = 284;  // 1=normal, 2=skill, 3=upgrade
    constexpr uint32_t kAttrPrimaryA = 292;  // Force externe (Field292)
    constexpr uint32_t kAttrPrimaryB = 296;  // Force interne (Field296)
    constexpr uint32_t kAttrRatingMin= 300;  // base rating min / def interne ×0.9 (Field300)
    constexpr uint32_t kAttrRatingMax= 304;  // base rating max (Field304)
    constexpr uint32_t kCritRate     = 308;
    constexpr uint32_t kExtAttack    = 312;
    constexpr uint32_t kIntAttack    = 316;
    constexpr uint32_t kExtDefense   = 320;
    constexpr uint32_t kIntDefense   = 324;
    constexpr uint32_t kMaxHp        = 328;
    constexpr uint32_t kMaxMp        = 332;
    constexpr uint32_t kAccuracy     = 336;
    constexpr uint32_t kRegen        = 360;
    constexpr uint32_t kEvasion      = 364;
    constexpr uint32_t kResistAll    = 368;
    constexpr uint32_t kResistPair0  = 372;  // 8 paires (clé,valeur) : +372/+376 … +428/+432
}

struct ItemInfoView {
    const uint8_t* rec = nullptr;

    ItemInfoView() = default;
    explicit ItemInfoView(const uint8_t* r) : rec(r) {}
    bool valid() const { return rec != nullptr; }

    // Lectures brutes (memcpy = tolérant à l'alignement).
    int32_t i32(uint32_t off) const {
        int32_t v = 0; std::memcpy(&v, rec + off, 4); return v;
    }
    uint32_t u32(uint32_t off) const {
        uint32_t v = 0; std::memcpy(&v, rec + off, 4); return v;
    }

    // Champs nommés (lus par les moteurs de stats).
    uint32_t itemId()        const { return u32(ItemOff::kItemId); }
    uint32_t category()      const { return u32(ItemOff::kCategory); }
    uint32_t typeCode()      const { return u32(ItemOff::kTypeCode); }
    uint32_t itemLevel()     const { return u32(ItemOff::kItemLevel); }
    uint32_t subtype()       const { return u32(ItemOff::kSubtype); }
    uint32_t skillFlag()     const { return u32(ItemOff::kSkillFlag); }
    int32_t  attrPrimaryA()  const { return i32(ItemOff::kAttrPrimaryA); }
    int32_t  attrPrimaryB()  const { return i32(ItemOff::kAttrPrimaryB); }
    int32_t  attrRatingMin() const { return i32(ItemOff::kAttrRatingMin); }
    int32_t  attrRatingMax() const { return i32(ItemOff::kAttrRatingMax); }
    int32_t  critRate()      const { return i32(ItemOff::kCritRate); }
    int32_t  extAttack()     const { return i32(ItemOff::kExtAttack); }
    int32_t  intAttack()     const { return i32(ItemOff::kIntAttack); }
    int32_t  extDefense()    const { return i32(ItemOff::kExtDefense); }
    int32_t  intDefense()    const { return i32(ItemOff::kIntDefense); }
    int32_t  maxHp()         const { return i32(ItemOff::kMaxHp); }
    int32_t  maxMp()         const { return i32(ItemOff::kMaxMp); }
    int32_t  accuracy()      const { return i32(ItemOff::kAccuracy); }
    int32_t  regen()         const { return i32(ItemOff::kRegen); }
    int32_t  evasion()       const { return i32(ItemOff::kEvasion); }
    int32_t  resistAll()     const { return i32(ItemOff::kResistAll); }
    // Paire de résistance p ∈ [0..7] : clé + valeur.
    int32_t  resistKey(int p) const { return i32(ItemOff::kResistPair0 + 8 * p); }
    int32_t  resistVal(int p) const { return i32(ItemOff::kResistPair0 + 8 * p + 4); }
};

// Résout un id d'item (1-based) en enregistrement ITEM_INFO. Reproduit
// MobDb_GetEntry 0x4C3C00 : borne 1..count et rejette les slots vides (id==0).
ItemInfoView ItemLookup(const DataTable& itemTbl, uint32_t itemId);

// =====================================================================
// Décodage du mot socket/gemme, octet par octet (0x545610/40/70/A0).
// Un mot socket 32 bits : octet0=cat socket int / raffinage, octet1=élément
// ou grade gemme, octet2=cat socket float / nb gemmes, octet3=niveau enchant.
// Les fonctions d'origine font un memcpy de 4 octets de l'argument poussé,
// d'où l'extraction directe de l'octet N du mot 32 bits.
// =====================================================================
inline uint8_t Item_GetAttribByte0(uint32_t w) { return static_cast<uint8_t>(w & 0xFFu); }
inline uint8_t Item_GetAttribByte1(uint32_t w) { return static_cast<uint8_t>((w >> 8) & 0xFFu); }
inline uint8_t Item_GetAttribByte2(uint32_t w) { return static_cast<uint8_t>((w >> 16) & 0xFFu); }
inline uint8_t Item_GetAttribByte3(uint32_t w) { return static_cast<uint8_t>((w >> 24) & 0xFFu); }

// =====================================================================
// Bonus scalé par niveau/type de l'item (Item_GetScaledStat 0x545980).
// statIdx ∈ 1..6 sélectionne la stat ; le résultat est tronqué (ftol).
// Renvoie 0 si le type/niveau ne correspond à aucune courbe.
// =====================================================================
int Item_GetScaledStat(const ItemInfoView& item, int statIdx);

// =====================================================================
// Bonus socket d'une arme (type_code == 28).
//   weaponId    : id de l'arme (résolu dans itemTbl)
//   socketWord  : mot socket de l'arme (octet0 pour int, octet2 pour float)
//   category    : catégorie attendue (octet/10) — 1..6 (int) / 1..5 (float)
// Item_SocketBonusInt 0x4CA620 / Item_SocketBonusFloat 0x4CAC30.
// =====================================================================
int    Item_SocketBonusInt  (const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int category);
double Item_SocketBonusFloat(const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int category);

// Bonus élémentaire d'une arme élémentaire (type_code == 29), octet1 du mot
// socket (Item_GetElementalBonus 0x54F590). key ∈ 1..8.
double Item_GetElementalBonus(const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int key);

// =====================================================================
// Décodage d'un bonus de gemme depuis un mot décimal (Item_DecodeGemBonus 0x54D390).
//   group 1 : chiffres millions (a3/1000000)  → gemme d'attribut
//   group 2 : chiffres milliers (a3%1e6/1000)  → gemme spéciale (clé 30/130)
//   group 3 : chiffres unités   (a3%1000)      → gemme plate (×30/20/100/200…)
// key = préfixe attendu ; renvoie 0 si non concordant.
// =====================================================================
double Item_DecodeGemBonus(int group, int key, int gemWord);

// =====================================================================
// Grade (Item_GetGradeValue 0x54D750 / Item_GetGradeMultiplier 0x54D9A0).
//   GradeValue      : id d'item → valeur plate 10/15/20/30 (0 sinon)
//   GradeMultiplier : grade 1..10 → multiplicateur 1.05..1.4 (1.0 sinon)
// =====================================================================
int    Item_GetGradeValue(int itemId);
double Item_GetGradeMultiplier(int grade);

// =====================================================================
// Delta de stat d'enchantement (Item_GetEnchantStatDelta 0x553D50).
// Grande table (classe, indice de slot, niveau d'enchant 1..59, clé) → delta
// signé (centièmes pour les stats principales, unités pour préc./esquive).
//   itemClass  : classe de l'item — Item_ClassifyRecord (1, 4 → armes/armures ;
//                8 → cas spécial, uniquement slot 1)
//   slot       : indice de slot d'équipement 0..12 (les moteurs bouclent i≠8 ;
//                slots {0,2,3,4,5}=armures, 7=arme, 1=cas classe 8)
//   socketWord : mot socket du slot (octet3 = niveau d'enchant 1..59)
//   key        : clé de stat (10=atkExt, 20=atkInt, 30=defExt, 40=defInt,
//                50=HP, 60=MP, 70=préc., 80=esq., 90/100=rating…)
// Appel type : Item_GetEnchantStatDelta(class, i, g_Slot#Socket[i], clé).
// =====================================================================
int Item_GetEnchantStatDelta(int itemClass, int slot, uint32_t socketWord, int key);

// =====================================================================
// Scaling d'une stat vers un plafond par type d'arme (Item_ScaleStatByType*).
//   caps    : 4 plafonds d'arme (mPAT @0x8E717C — dword_8E717C[4])
//   itemId  : id de l'arme/objet spécial (sélectionne caps[0..3])
//   value   : valeur à scaler (comparée au plafond)
//   flag    : compteur d'upgrades (>0 requis pour A/B), sinon renvoie 0
// A 0x4C91B0 (1000/2000) · B 0x4C95C0 (2000/4000) · C 0x4CB0D0 (2000/4000)
// · D 0x4CB3F0 (1800/3600). Renvoie 0 si l'id n'est pas éligible.
// =====================================================================
double Item_ScaleStatByTypeA(const int32_t caps[4], int itemId, int value, int flag);
double Item_ScaleStatByTypeB(const int32_t caps[4], int itemId, int value, int flag);
double Item_ScaleStatByTypeC(const int32_t caps[4], int itemId, int value);
double Item_ScaleStatByTypeD(const int32_t caps[4], int itemId, int value);

} // namespace ts2::game

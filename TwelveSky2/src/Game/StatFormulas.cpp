// Game/StatFormulas.cpp — implémentation byte-exacte du cluster Char_Calc*.
// Voir StatFormulas.h pour la carte des EA et la politique de neutralisation.
#include "Game/StatFormulas.h"
#include "Game/StatBonusContributors.h"   // Item_SumGemStatBonus, Char_SumGemStat*, SkillTree_SumBonuses
                                          // (inclut déjà ItemSystem.h : Item_GetElementalBonus, etc.)
#include "Game/ClientRuntime.h"          // g_Client.VarGet — cf. bloc g_EquipAux ci-dessous

namespace ts2::game {
namespace {

// ---------------------------------------------------------------------------
// Crt_ftol 0x760810 : conversion double->int par troncature vers zéro (fistp après
// réglage du mode d'arrondi -> chop). Équivaut au cast (int) en C++.
// ---------------------------------------------------------------------------
inline int ftol(double d) { return static_cast<int>(d); }

// ---------------------------------------------------------------------------
// MobDb_GetEntry 0x4C3C00 (réduit) : record ITEM_INFO 1-based ; nullptr si vide.
// ---------------------------------------------------------------------------
inline const ItemInfo* itemRec(const GameDatabases& db, uint32_t id) {
    if (id == 0) return nullptr;
    const uint8_t* p = db.item.record(id - 1);       // index = id-1
    if (!p) return nullptr;
    const ItemInfo* it = reinterpret_cast<const ItemInfo*>(p);
    if (it->itemId == 0) return nullptr;              // slot vide
    return it;
}

// LevelTable_GetStatK 0x4C29C0.. : champ dword `fieldIdx` (4..10 = Stat3..Stat9)
// du record LEVEL_INFO de niveau `lvl`, avec plafond tier (lvl>145 -> 145 ; hors
// [1..157] -> 0). fieldIdx dans LevelInfo : 4=baseExtAttack(+16) .. 10=baseAtkRatingMax(+40).
inline int levelStat(const GameDatabases& db, int lvl, int fieldIdx) {
    if (lvl < 1 || lvl > 157) return 0;              // garde 0x4C29D4
    int L = (lvl > 145) ? 145 : lvl;                 // fallback tier 0x4C29E1
    const uint8_t* p = db.level.record(L - 1);
    if (!p) return 0;
    return reinterpret_cast<const int32_t*>(p)[fieldIdx];
}

// Item_ClassifyRecord 0x5509A0 : catégorie 0..9 (ou -1 si null) d'un record ITEM_INFO.
inline int classifyRecord(const ItemInfo* it) {
    if (!it) return -1;
    if (it->category == 5) {                         // a1[46]==5
        uint32_t st = it->subtype;                   // a1[54]
        if (st == 2 || st == 4 || st == 5 || st == 6 || st == 7 || st == 9) return 1;
        if (st == 11 || st == 12 || st == 13 || st == 14) return 2;
        switch (it->typeCode) {                      // a1[47]
            case 31: return 5;
            case 32: return 6;
            case 8:  return 8;
            case 29: return 9;
        }
    } else {
        if (it->category == 6) return 4;             // a1[46]==6
        if ((it->itemId >= 201 && it->itemId <= 218) ||
            (it->itemId >= 2303 && it->itemId <= 2305)) return 3;
        if (it->typeCode == 28) return 7;
    }
    return 0;
}

// Item_ClassifyById 0x550800 : idem via lookup id.
inline int classifyById(const GameDatabases& db, uint32_t id) {
    if (id == 0) return -1;
    const ItemInfo* it = itemRec(db, id);
    if (!it) return -1;
    return classifyRecord(it);
}

// Item_GetSlotGroup 0x54D700 : 30 si typeCode==30, sinon 3 (0 si id inconnu). Toutes ses
// invocations d'origine portent sur g_SpecialItem (0x1687310, absent) -> non appelée ici.
[[maybe_unused]] inline int slotGroup(const GameDatabases& db, uint32_t id) {
    const ItemInfo* it = itemRec(db, id);
    if (!it) return 0;
    return (it->typeCode == 30) ? 30 : 3;
}

// sub_545610/545640/545670 0x545610.. : octets 0/1/2 du mot de socket (attribut item).
inline int attrByte0(uint32_t socket) { return  socket        & 0xFF; }
// byte1 (sub_545640) n'est consommé que par des termes de skill neutralisés [hook].
[[maybe_unused]] inline int attrByte1(uint32_t socket) { return (socket >> 8)  & 0xFF; }
inline int attrByte2(uint32_t socket) { return (socket >> 16) & 0xFF; }

// ---------------------------------------------------------------------------
// Tables de croissance par cultivation (Char_AttrGrowth_Field292/296/300/304
// 0x4CF030/0x4CFA90/0x4CEC20/0x4CF560). Indexées par growthIndex : bucket de tier
// (bornes 100/200/300/400) puis palier growthIndex%100 dans [1..15]. Palier 0 ou
// hors [1..15] -> 0. Reproduction byte-exacte des switch d'origine.
// ---------------------------------------------------------------------------
// [16] : index 0 inutilisé.
const int G292_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G292_101_200[16] = {0,2,6,12,20,30,42,56,72,90,110,134,164,194,224,254};
const int G292_201_300[16] = {0,1,2,3,5,7,10,14,18,22,27,33,41,49,57,65};
const int G292_301_400[16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};
const int G292_gt400  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};

const int G296_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G296_101_200[16] = {0,1,2,3,5,7,10,14,18,22,27,33,41,49,57,65};
const int G296_201_300[16] = {0,3,8,15,25,37,52,70,90,112,137,167,205,243,281,319};
const int G296_gt300  [16] = {0,1,2,3,5,7,10,14,18,22,27,33,41,49,57,65};

const int G300_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G300_101_300[16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};
const int G300_301_400[16] = {0,2,6,12,20,30,42,56,72,90,110,134,164,194,224,254};
const int G300_gt400  [16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};

const int G304_le100  [16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G304_101_200[16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G304_201_300[16] = {0,0,1,3,5,8,11,14,18,23,28,34,41,48,55,62};
const int G304_301_400[16] = {0,1,3,6,10,15,21,28,36,45,55,67,82,97,112,127};
const int G304_gt400  [16] = {0,2,6,12,20,30,42,56,72,90,110,134,164,194,224,254};

inline int pick(const int* t, int gi) { int k = gi % 100; return (k >= 1 && k <= 15) ? t[k] : 0; }

inline int growth292(int gi) {           // 0x4CF030
    if (gi > 400) return pick(G292_gt400, gi);
    if (gi > 300) return pick(G292_301_400, gi);
    if (gi > 200) return pick(G292_201_300, gi);
    if (gi > 100) return pick(G292_101_200, gi);
    return pick(G292_le100, gi);
}
inline int growth296(int gi) {           // 0x4CFA90 (bucket 101..200 == 201..300 fusionné ? non : <=300/<=200/<=100)
    if (gi > 300) return pick(G296_gt300, gi);
    if (gi > 200) return pick(G296_201_300, gi);
    if (gi > 100) return pick(G296_101_200, gi);
    return pick(G296_le100, gi);
}
inline int growth300(int gi) {           // 0x4CEC20 (buckets <=400/<=300/<=100 : 101..300 fusionné)
    if (gi > 400) return pick(G300_gt400, gi);
    if (gi > 300) return pick(G300_301_400, gi);
    if (gi > 100) return pick(G300_101_300, gi);
    return pick(G300_le100, gi);
}
inline int growth304(int gi) {           // 0x4CF560
    if (gi > 400) return pick(G304_gt400, gi);
    if (gi > 300) return pick(G304_301_400, gi);
    if (gi > 200) return pick(G304_201_300, gi);
    if (gi > 100) return pick(G304_101_200, gi);
    return pick(G304_le100, gi);
}

// ---------------------------------------------------------------------------
// Snapshot d'équipement (Char_BuildEquipSnapshot 0x4CC1C0). Résout equip[i].itemId
// -> record ITEM_INFO. r[1] = arme (== equip[1].itemId == g_LocalPlayerWeaponId).
// L'override de costume/forme (branche g_SpecialFormActive 0x16760D4>0) est
// [runtime absent] -> non appliqué (voir TODO plus bas).
// ---------------------------------------------------------------------------
struct Snapshot {
    const ItemInfo* r[13];
    uint32_t        sock[13];   // equip[i].socket (mot dword_16731E0 + 16*i)
    uint32_t        id[13];     // equip[i].itemId
};
Snapshot buildSnapshot(const SelfState& s, const GameDatabases& db) {
    Snapshot sn{};
    for (int i = 0; i < 13; ++i) {
        sn.id[i]   = s.equip[i].itemId;
        sn.sock[i] = s.equip[i].socket;
        sn.r[i]    = itemRec(db, sn.id[i]);
    }
    // TODO [runtime absent] 0x4CC310 : si g_SpecialFormActive>0 && Npc_IsSpecialType(
    //   g_SelfMorphNpcId 0x1675A98)==1, les slots 0..7 sont remplacés par des ids de
    //   costume (tables 86673.. selon niveau+elementSecondary) — non modélisé (état morph
    //   absent de SelfState). RE-VÉRIFIÉ (audit 2026-07-14) : g_SpecialFormActive 0x16760D4
    //   n'est écrit NULLE PART dans ClientSource (grep négatif) -> reste 0 -> cette branche
    //   ne se déclenche jamais côté client réécrit, TODO inchangé (contrairement à
    //   g_SelfMorphNpcId 0x1675A98 lui-même, lu via g_Client.VarGet() dans une dizaine de
    //   modules — mais son gate g_SpecialFormActive, lui, reste absent).
    return sn;
}

// ---------------------------------------------------------------------------
// g_EquipAux 0x16750B8/dword_16750BC/dword_16750C0 — 13 slots, stride 0x0C (3 dwords
// bit-packés PAR SLOT), arguments block0/1/2 de SkillTree_SumBonuses (cf.
// Game/StatBonusContributors.h). RE-DÉCOUVERT DISPONIBLE (audit formules 2026-07-14) :
// Net/ItemActionDispatch.cpp écrit RÉELLEMENT ces 3 blocs via l'échappatoire
// g_Client.Var(kEquipAux0/1/2 + slot*0x0C) à chaque déplacement inventaire->équipement
// (miroir de g_InvAux, cf. son bandeau de tête) — l'état N'EST DONC PLUS absent, même
// s'il n'existe toujours PAS de champ dédié dans EquipSlot (qui ne porte que
// extra0/extra1 = g_EquipDurability/g_EquipSerial, une paire SANS RAPPORT). On lit ces
// 3 blocs directement sur le singleton g_Client (comme le fait déjà ItemActionDispatch.cpp),
// pas sur `s`/`sn`, pour ne pas dupliquer une 2e source de vérité dans SelfState.
// Adresses identiques à Net/ItemActionDispatch.cpp (kEquipAux0/1/2), redéclarées ici en
// interne (linkage anonyme, pas d'export) faute de header commun partagé pour ces constantes.
constexpr uint32_t kEquipAux0 = 0x16750B8;
constexpr uint32_t kEquipAux1 = 0x16750BC;
constexpr uint32_t kEquipAux2 = 0x16750C0;

// Boucle SkillTree_SumBonuses(category, g_EquipAux[3*i], dword_16750BC[3*i], dword_16750C0[3*i])
// sur les 13 slots pour lesquels sn.r[i] != nullptr — reproduit IDENTIQUEMENT dans les 7 sites
// d'appel confirmés par re-décompilation (CalcMaxHP 0x4D57C9 cat=7, CalcMaxMP 0x4D6299 cat=8,
// CalcExternalAttack 0x4D151B cat=1, CalcInternalAttack 0x4D25F7 cat=2, CalcExternalDefense
// 0x4D330B cat=5, CalcInternalDefense 0x4D40FF cat=6, CalcAttackRatingMax 0x4CEAB1 cat=4).
// Aucune des 7 boucles n'exclut le slot 8 (contrairement aux boucles de somme d'équip
// principales) — confirmé par désassemblage : la garde est uniquement `sn.r[i] != nullptr`.
// CalcAttackRatingMin (0x4CE18F, cat=3) appelle le même helper mais FUSIONNÉ dans sa propre
// boucle 0..13 (câblé séparément dans CalcAttackRatingMin, cf. plus bas) — addition entière
// commutative, résultat identique qu'on l'agrège ici ou inline.
inline int skillTreeEquipBonus(int category, const Snapshot& sn, const GameDatabases& db) {
    int total = 0;
    for (int i = 0; i < 13; ++i) {
        if (!sn.r[i]) continue;
        const uint32_t block0 = static_cast<uint32_t>(g_Client.VarGet(kEquipAux0 + i * 0x0C));
        const uint32_t block1 = static_cast<uint32_t>(g_Client.VarGet(kEquipAux1 + i * 0x0C));
        const uint32_t block2 = static_cast<uint32_t>(g_Client.VarGet(kEquipAux2 + i * 0x0C));
        total += SkillTree_SumBonuses(category, block0, block1, block2, db);
    }
    return total;
}

} // namespace

// ===========================================================================
// Equip_GetSetBonusId 0x548CE0 (réduit aux branches déterministes).
// Toutes les familles EquipSet_Match*/EquipSet_IsPiece87206 (tables d'ids non
// extraites) sont [hook] -> aucun match. Il reste la branche de comptage finale
// (0x5492D2) : v11 = nb de slots {0,2,3,4,5,7} de classe 1||4 ; v12 = nb IsPiece
// [hook]=0. Renvoie 50 si v11==6, 20 si v12==6 (jamais), 30 si v12+v11==6 (==v11==6,
// déjà pris), sinon 0. -> 50 ssi les 6 slots d'armure sont classe 1||4, sinon 0.
// ===========================================================================
int EquipSetBonusId(const SelfState& s, const GameDatabases& db) {
    int v11 = 0;
    const int armor[6] = {0, 2, 3, 4, 5, 7};
    for (int j : armor) {
        int c = classifyById(db, s.equip[j].itemId);
        if (c == 1 || c == 4) ++v11;
    }
    if (v11 == 6) return 50;
    return 0;
}

// ===========================================================================
// Char_CalcMaxHP 0x4D4ED0  — canal 7 (field328 = ItemInfo.maxHp).
// ===========================================================================
int CalcMaxHP(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level;                         // g_SelfLevel 0x16731A8
    const int lb  = s.levelBonus;                    // g_SelfLevelBonus 0x16731AC
    const int setId = EquipSetBonusId(s, db);        // v17 (0x4D4F32)
    Snapshot sn = buildSnapshot(s, db);
    int v25 = 0;

    // --- boucle set-factors + spécial slot4/slot10 (0x4D4F3D) ---
    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->maxHp;                        // +328
        v25 += base;
        if (i == 8) continue;                        // 0x4D4F80
        bool doTail = true;                          // atteindre LABEL_25 (slot4/slot10)
        switch (setId) {                             // v17
            case 1: case 6:   v25 += ftol((double)base * 0.4000000059604645); break;
            case 5: case 10:  v25 += ftol((double)base * 1.0); break;
            case 11: case 16: v25 += ftol((double)base * 0.6000000238418579); break;
            case 15: case 20: v25 += ftol((double)base * 1.100000023841858); break; // LABEL_10
            case 21:          v25 += ftol((double)base * 0.2000000029802322); break;
            case 22:          v25 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) { v25 += ftol((double)base * 0.550000011920929); }
                else if (i != 9 && i != 11 && i != 12) {
                    int c = classifyRecord(it);
                    if (c == 1 || c == 4) v25 += ftol((double)base * 0.550000011920929);
                    else                  v25 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v25 += ftol((double)base * 0.550000011920929); break; // LABEL_24
            default: break;                          // pas de facteur de set
        }
        (void)doTail;
        // LABEL_25 (0x4D51BB) : traitements slot4 / slot10 (exécutés quel que soit setId).
        if (i == 4) {
            int c = classifyRecord(it);
            if (c == 1 || c == 4) {
                int v31 = attrByte0(sn.sock[i]);     // sub_545610(slot4 socket)
                if (v31 > 0) { if (v31 > 100) v31 -= 100; v25 += 200 * v31; }
            } else if (it->skillFlag == 2) {         // +284==2
                // [hook] Skill_GetValueClass4 0x54ECC0 -> 0
            } else {
                // [hook] Item_GetScaledStat 0x545980 -> 0 ; v25 += 0 * niveau
            }
        } else if (i == 10) {
            float v14 = (it->itemId == 210 || it->itemId == 211 || it->itemId == 212 ||
                         it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                         it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                        ? 7.8000002f : 3.9000001f;
            if (classifyRecord(it) != 2) {
                int v31 = attrByte0(sn.sock[i]);     // sub_545610(slot10 socket)
                if (v31 > 0) v25 += ftol((double)v31 * v14);
            }
        }
    }

    // --- enchant loop (0x4D53EE) : Item_GetEnchantStatDelta 0x553D50 [hook] -> 0 ---
    // Structure conservée : slot1(classe8) et slots {0,2,3,4,5,7}(classe1||4) ajouteraient
    // Item_GetEnchantStatDelta(...,50) avec plancher v25>=1. Delta [hook]=0 -> aucun effet
    // (le plancher v25>=1 ne s'applique qu'après un +=delta, ici absent). TODO 0x4D5467/0x4D54CD.

    // --- base LEVEL_INFO Stat7 (0x4D54FF) ---
    // Branche morph (g_SpecialFormActive) [runtime absent] : sinon niveau = lb+lvl.
    v25 += levelStat(db, lb + lvl, 8);               // LevelTable_GetStat7 (field8 = baseMaxHp)

    // --- bonus de compétence/stance (0x4D55B2) ---
    // Prédicat = (!stanceSet && !special) || morph∈[319..323], puis condition d'id de morph.
    // Skill_IsCurrentStanceSet/Special 0x4FB0F0/0x4FC800 [hook]=false, g_SelfMorphNpcId
    // 0x1675A98 [runtime absent]=0, dword_1675630 [runtime absent]=0.
    // -> !false&&!false=true ; (0!=84 && 0<235)=true ; terme = 10*(0/1000)=0. Aucun effet.

    // --- g_SpecialItem/grade 0x1687310 [runtime absent]=0 (0x4D55E7..0x4D5753) ---
    // slotGroup(0)=0 != 30 ; switch(0) sans case -> aucun multiplicateur/gemme spéciale.

    // --- buffs % (0x4D575D/0x4D5787) : dword_16758F8 [runtime absent]=0 ; growthIndex %HP ---
    if (s.growthIndex > 0)                            // 0x4D5787 (dword_1674774 = growthIndex)
        v25 = ftol((double)v25 * (double)(s.growthIndex % 100 + 100) * 0.009999999776482582);
    if (setId == 19) v25 += 500;                     // 0x4D57BA (jamais avec setId réduit ∈{0,50})

    // SkillTree_SumBonuses 0x54B700 (0x4D57C9, cat=7) — CÂBLÉE (audit 2026-07-14) : g_EquipAux
    // désormais disponible via g_Client.Var(), cf. bandeau de tête skillTreeEquipBonus() ci-dessus.
    v25 += skillTreeEquipBonus(7, sn, db);
    // --- g_ElementMastery 0x1675680 [runtime absent]=0 (0x4D582F) : pas de +1000. ---
    // --- talisman 0x1674760/0x1675664 [runtime absent] (0x4D584D) : slot∉[10,20) -> ignoré. ---
    // --- Item_SocketBonusInt/Float 0x4CA620/0x4CAC30 [hook]=0 (0x4D58CE/0x4D58EE).
    //   TODO(verif) : décompilation confirmée — ces deux appels utilisent (g_SelectedInvItemId
    //   0x1673258, dword_1673260) et NON (g_LocalPlayerWeaponId, g_Slot1Socket) : c'est un item
    //   "sélectionné" distinct de l'équipement (probablement une UI d'amélioration de socket),
    //   sans champ correspondant dans SelfState. Pattern identique dans les 8 autres Char_Calc*
    //   qui les appellent (jamais la paire weaponId/g_Slot1Socket) -> genuinely absent partout. ---
    // --- Escort_IsCurrentTarget 0x54F440 [hook]=false (0x4D5908). ---
    // --- Item_GetElementalBonus 0x54F590 (0x4D5939) : CÂBLÉE — args confirmés par décompilation
    //   (g_LocalPlayerWeaponId 0x16731E8, g_Slot1Socket 0x16731F0) = (s.weaponId, s.equip[1].socket),
    //   tous deux présents dans SelfState. key=5 pour ce canal (HP). ---
    v25 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 5));
    return v25;
}

// ===========================================================================
// Char_CalcMaxMP 0x4D59B0 — canal 8 (field332 = ItemInfo.maxMp).
// ===========================================================================
int CalcMaxMP(const SelfState& s, const GameDatabases& db) {
    // NB : la table LEVEL_INFO ne fournit AUCUNE base de MP (pas de LevelTable_GetStat*).
    const int setId = EquipSetBonusId(s, db);        // v16
    Snapshot sn = buildSnapshot(s, db);
    int v24 = 0;

    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->maxMp;                        // +332
        v24 += base;
        if (i == 8) continue;
        bool reachTail = true;
        switch (setId) {
            case 1: case 6:   v24 += ftol((double)base * 0.4000000059604645); break;
            case 5: case 10:  v24 += ftol((double)base * 1.0); break;
            case 11: case 16: v24 += ftol((double)base * 0.6000000238418579); break;
            case 15: case 20: v24 += ftol((double)base * 1.100000023841858); break;
            case 21:          v24 += ftol((double)base * 0.2000000029802322); break;
            case 22:          v24 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v24 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    int c = classifyRecord(it);
                    if (c == 1 || c == 4) v24 += ftol((double)base * 0.550000011920929);
                    else                  v24 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v24 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        (void)reachTail;
        // LABEL_25 switch(i) (0x4D5CAB) :
        switch (i) {
            case 0: {
                int c = classifyRecord(it);
                if (c != 1 && c != 4) {
                    if (it->skillFlag == 2) {        // +284==2
                        // [hook] Skill_GetValueClass0 0x54ED90 -> 0
                    } else {
                        // [hook] Item_GetScaledStat 0x545980 -> 0
                    }
                }
                break;
            }
            case 4: {
                int c = classifyRecord(it);
                if (c == 1 || c == 4) {
                    int v29 = attrByte0(sn.sock[i]);
                    if (v29 > 0) { if (v29 > 100) v29 -= 100; v24 += 200 * v29; }
                }
                break;
            }
            case 9: case 11: case 12:
                // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=8, canal MP ; 0x4D5F49/0x4D5F67).
                if (classifyRecord(it) == 2) v24 += Item_SumGemStatBonus(8, sn.sock[i]);
                break;
            case 10: {
                float v13 = (it->itemId == 207 || it->itemId == 208 || it->itemId == 209 ||
                             it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                             it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                            ? 7.8000002f : 3.9000001f;
                if (classifyRecord(it) == 2) {
                    // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=8 ; 0x4D5EC8..0x4D5EE8).
                    v24 += Item_SumGemStatBonus(8, sn.sock[i]);
                } else {
                    int v29 = attrByte0(sn.sock[i]);
                    if (v29 > 0) v24 += ftol((double)v29 * v13);
                }
                break;
            }
            default: break;
        }
    }

    // enchant loop (0x4D5F6F) : Item_GetEnchantStatDelta(...,60) [hook]=0 -> aucun effet.

    // bonus stance/morph (0x4D60CE) : dword_1675630 [runtime absent]=0 -> 10*(0%1000)=0.

    // g_SpecialItem 0x1687310 [runtime absent]=0 : switch/gemmes -> aucun effet (0x4D6103..).

    // buffs % (0x4D6246) : dword_1675900 [runtime absent]=0 ; growthIndex %MP (0x4D6270)
    if (s.growthIndex > 0)
        v24 = ftol((double)v24 * (double)(s.growthIndex % 100 + 100) * 0.009999999776482582);

    // SkillTree_SumBonuses(8,..) (0x4D6299) — CÂBLÉE (audit 2026-07-14) : cf. CalcMaxHP.
    v24 += skillTreeEquipBonus(8, sn, db);
    // g_ElementMastery==2 [absent] ; talisman [absent] ;
    // Item_SocketBonusInt/Float [hook]=0 — TODO(verif) : paire g_SelectedInvItemId/dword_1673260
    //   (0x4D639D/0x4D63BC), pas la paire weaponId/socket1 -> genuinely absent (cf. CalcMaxHP).
    // Escort [hook]=false.
    // Item_GetElementalBonus (0x4D6409) : CÂBLÉE — (s.weaponId, s.equip[1].socket), key=6.
    v24 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 6));
    return v24;
}

// ===========================================================================
// Char_CalcExternalAttack 0x4D0530 — canal 1 (attaque externe, arme-scalée).
// ===========================================================================
int CalcExternalAttack(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    // --- attributs de base (branche non-morph 0x4D060E) ---
    // g_CoreAttr (sub_546380) 0x167477C [runtime absent]=0.
    int v41 = s.attrExtForce  + growth292(gi);       // 0x16731BC + croissance292
    int v45 = s.attrOffensive + growth304(gi);       // 0x16731C0 + croissance304
    // TODO [runtime absent] 0x4D0572 : branche morph (Level_ToTierValueB 0x54EFF0,
    //   maybe_StubReturn1A 0x54F030) — g_SpecialFormActive absent.
    // g_AttrBuffActive 0x16758A8 [runtime absent]=0 : pas de buff d'attribut (0x4D0670).

    const int setId = EquipSetBonusId(s, db);        // v39
    Snapshot sn = buildSnapshot(s, db);

    for (int i = 0; i < 13; ++i) {                   // somme attributs d'équip (0x4D0709)
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        v41 += it->attrPrimaryA;                     // +292
        v45 += it->attrRatingMax;                    // +304
    }
    // pet (dword_1687448) [runtime absent].
    // Char_SumGemStatA/D 0x54CB00/0x54CC90 — CÂBLÉES (0x4D07BA/0x4D07CA) : A alimente v45
    // (attrRatingMax/304), D alimente v41 (attrPrimaryA/292), conforme à l'ordre d'origine.
    v45 += Char_SumGemStatA(s);
    v41 += Char_SumGemStatD(s);
    // grade dword_1687474 [runtime absent] (0x4D07D4..0x4D0800).

    int v50 = 0;
    // --- conversion de classe d'arme (slot 7 = r[7], 0x4D082A) ---
    if (sn.r[7]) {
        switch (sn.r[7]->typeCode) {                 // +188
            case 0x0D: case 0x11: case 0x13:
                v50 += ftol((double)v41 * 2.650000095367432);
                v50 += ftol((double)v45 * 1.429999947547913);
                break;
            case 0x0E: case 0x10: case 0x14:
                v50 += ftol((double)v41 * 2.799999952316284);
                v50 += ftol((double)v45 * 1.509999990463257);
                break;
            case 0x0F: case 0x12: case 0x15:
                v50 += ftol((double)v41 * 2.509999990463257);
                v50 += ftol((double)v45 * 1.350000023841858);
                break;
            default: break;
        }
    } else {
        v50 += ftol((double)v41 * 1.25);
        v50 += ftol((double)v45 * 0.6700000166893005);
    }
    // pet-mastery flat (dword_1674798/16747C8/167479C) [runtime absent] : +413/+275 ignorés (0x4D0941).

    // --- base LEVEL_INFO Stat3 (0x4D09AF) ---
    v50 += levelStat(db, lb + lvl, 4);               // baseExtAttack (field4)

    // --- somme extAttack d'équip + facteurs de set + spécial slots (0x4D09FE) ---
    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->extAttack;                    // +312
        v50 += base;
        if (i == 8) continue;
        int c48 = classifyRecord(it);                // v48
        switch (setId) {
            case 2: case 7:                       v50 += ftol((double)base * 0.4000000059604645); break;
            case 4: case 9: case 12: case 17:     v50 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                      v50 += ftol((double)base * 1.0); break;
            case 14: case 19:                     v50 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                     v50 += ftol((double)base * 1.100000023841858); break;
            case 21:                              v50 += ftol((double)base * 0.2000000029802322); break;
            case 22:                              v50 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v50 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    if (classifyRecord(it) == 1 || classifyById(db, it->itemId) == 4)
                        v50 += ftol((double)base * 0.550000011920929);
                    else
                        v50 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v50 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_67 (0x4D0CF3) : bonus déterministes par slot ---
        if (i == 1 && c48 != 9 && it->typeCode == 29) {  // +188==29
            int v57 = attrByte0(sn.sock[i]);
            if (v57 > 0) v50 += 6 * v57;
        }
        if (i == 7) {
            if (classifyRecord(sn.r[7]) == 1 || classifyById(db, it->itemId) == 4) {
                int v57 = attrByte0(sn.sock[i]);
                if (v57 > 0) { if (v57 >= 100) v57 -= 100; v50 += 1200 * v57; }
            } else if (it->skillFlag == 2) {         // +284==2
                // [hook] Skill_GetValueTier7 0x54E550 -> 0 ; puis terme arme-scalé :
                int v57 = attrByte0(sn.sock[i]);
                if (v57 > 0) v50 += ftol((double)base * ((double)v57 * 0.02999999932944775));
            } else if (it->skillFlag == 3) {         // +284==3
                // [hook] Skill_GetUpgradeCostTier 0x54F4D0 -> 0
            } else {
                // [hook] Item_GetScaledStat 0x545980 -> 0 ; v42*v43==0
                int v57 = attrByte0(sn.sock[i]);     // sub_545610(slot7 socket)
                if (v57 > 0)                          // terme (0 + base) * (v57*0.03)
                    v50 += ftol((double)(0 + base) * ((double)v57 * 0.02999999932944775));
            }
        }
        if (i == 10) {
            float v37 = (it->itemId == 213 || it->itemId == 214 || it->itemId == 215 ||
                         it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                         it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                        ? 23.4f : 11.7f;
            if (classifyRecord(it) == 2) {
                // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=3, canal extAttack ; 0x4D1008/0x4D1026).
                v50 += Item_SumGemStatBonus(3, sn.sock[i]);
            } else {
                int v57 = attrByte0(sn.sock[i]);
                if (v57 > 0) v50 += ftol((double)v57 * v37);
            }
        }
        // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=3 ; 0x4D1097/0x4D10B5).
        if ((i == 9 || i == 11 || i == 12) && classifyRecord(it) == 2)
            v50 += Item_SumGemStatBonus(3, sn.sock[i]);
    }

    // enchant loop (0x4D10BD) : Item_GetEnchantStatDelta(...,10) [hook]=0 -> aucun effet.

    // --- bonus stance/compétence (0x4D11E7) ---
    // Skill_IsCurrentStanceSet/Special [hook]=false, morph [absent]=0 -> prédicat vrai.
    // Char_FindSkillSlotByName 0x55D520 [hook] (v36 reste -1). dword_1675668/16760CC/
    // 16760C0/1674730 [runtime absent]=0. -> v36!=0 (=-1) donc pas le +600 ; branche else :
    // dword_16760CC(0)==element? si element==0 -> combos absents=0 -> v50 += 3*0 = 0.
    // Aucun terme déterministe (dword_1674730 absent). TODO 0x4D11E7 (état de skill absent).

    // buffs/grade/talisman/sockets/escort/élément (0x4D12DA..fin) :
    //   dword_1674A50/1675728 [absent]=0 ; dword_1675704 [absent] ; g_SpecialItem 0x1687310
    //   [absent]=0 (switch sans effet) ; slotGroup(0)!=30 ; dword_16758D8[0] [absent]=0 ;
    //   Item_ScaleStatByTypeA 0x4C91B0 [hook]=0 (v50>=0 -> += 0) — TODO(verif) : décompilation de
    //   0x4C91B0 confirme caps=dword_8E717C (table mPAT). Docs/TS2_GAMEPLAY_LOGIC.md §"Plafonds
    //   d'arme" : le loader mPAT est STUBBÉ (PatTbl_LoadImg_STUB -> renvoie 1 sans charger le
    //   fichier 005_00008.IMG) -> les 4 plafonds sont RÉELLEMENT indéterminés au runtime, pas
    //   seulement absents de SelfState. Ses autres arguments (dword_1673258/1673260/167325C =
    //   g_SelectedInvItemId + valeur + compteur d'upgrade) sont aussi absents de SelfState (item
    //   "sélectionné" hors équipement). Deviner ces 4 valeurs romprait la fidélité -> reste [hook].
    //   g_ElementMastery==7 [absent] ; talisman [absent] ; Item_StatBonusTier 0x4CB6D0 [hook]=0 ;
    //   dword_16760D8 [absent] ; Escort [hook]=false ; Char_CompareSkillLoadout 0x557B00 [hook]=0 ;
    //   Item_SocketBonusInt/Float [hook]=0 — TODO(verif) : paire g_SelectedInvItemId/dword_1673260
    //   (0x4D16FE/0x4D171D), cf. CalcMaxHP. Item_GemSetBonusMultiplier [hook]=0 (hors liste).
    //   dword_16756E0 [absent]=0.
    // NB : Item_ScaleStatByTypeA vaut 0 -> la garde `v50 >= ftol(0)` est vraie -> v50 += 0
    //   (jamais la branche v50*=2). Idem canaux 2 (TypeB).
    // SkillTree_SumBonuses(1,..) (0x4D151B) — CÂBLÉE (audit 2026-07-14) : cf. bandeau
    //   skillTreeEquipBonus() en tête de fichier (g_EquipAux désormais lisible via g_Client.Var()).
    v50 += skillTreeEquipBonus(1, sn, db);
    // Item_GetElementalBonus (0x4D177D) : CÂBLÉE — (s.weaponId, s.equip[1].socket), key=1.
    v50 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 1));
    return v50;
}

// ===========================================================================
// Char_CalcInternalAttack 0x4D1830 — canal 2 (attaque interne, arme-scalée).
// ===========================================================================
int CalcInternalAttack(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    int v44 = s.attrIntForce + growth296(gi);        // 0x16731C4 + croissance296 (+ g_CoreAttr[absent]=0)
    // TODO [runtime absent] 0x4D1864 : branche morph (maybe_StubReturn1B 0x54F070).
    // g_AttrBuffActive [absent]=0 (0x4D18E7).

    const int setId = EquipSetBonusId(s, db);        // v28
    Snapshot sn = buildSnapshot(s, db);
    int v37 = 0;

    for (int i = 0; i < 13; ++i) {                   // somme attrPrimaryB (+296) (0x4D1987)
        const ItemInfo* it = sn.r[i];
        if (it) v44 += it->attrPrimaryB;
    }
    // pet (dword_1687448) [absent] ; grade [absent] (0x4D19D5..).
    // Char_SumGemStatB 0x54CB80 — CÂBLÉE (0x4D1A14), alimente v44 (attrPrimaryB/296).
    v44 += Char_SumGemStatB(s);

    v37 += ftol((double)v44 * 1.629999995231628);    // conversion interne (0x4D1A47)
    // pet-mastery flat +825/+550 [absent] (0x4D1A7E).

    v37 += levelStat(db, lb + lvl, 5);               // baseIntAttack Stat4 (field5) (0x4D1AEC)

    for (int i = 0; i < 13; ++i) {                   // somme intAttack (+316) + set + spécial (0x4D1B35)
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->intAttack;                    // +316
        v37 += base;
        if (i == 8) continue;
        int v34 = classifyRecord(it);
        switch (setId) {
            case 2: case 7:                       v37 += ftol((double)base * 0.4000000059604645); break;
            case 3: case 9: case 12: case 17:     v37 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                      v37 += ftol((double)base * 1.0); break;
            case 13: case 19:                     v37 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                     v37 += ftol((double)base * 1.100000023841858); break;
            case 21:                              v37 += ftol((double)base * 0.2000000029802322); break;
            case 22:                              v37 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v37 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    if (v34 == 1 || v34 == 4) v37 += ftol((double)base * 0.550000011920929);
                    else                      v37 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v37 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_61 switch(i) (0x4D1DFD) :
        switch (i) {
            case 1:
                if (v34 != 8 && v34 != 9) {
                    if (it->typeCode == 29) {        // +188==29
                        int v45 = attrByte0(sn.sock[i]);
                        if (v45 > 0) v37 += 6 * v45;
                    } else if (it->skillFlag == 2) { // +284==2
                        // [hook] Skill_GetValueByClassA 0x54E620 -> 0
                        int v45 = attrByte0(sn.sock[i]);
                        if (v45 > 0) v37 += 6 * v45;
                    } else {
                        // [hook] Item_GetScaledStat 0x545980 -> 0
                        int v45 = attrByte0(sn.sock[i]);
                        if (v45 > 0) v37 += 6 * v45;
                    }
                }
                break;
            case 2:
                if (v34 == 1 || v34 == 4) {
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) { if (v45 >= 100) v45 -= 100; v37 += 1000 * v45; }
                } else if (it->skillFlag == 2) {
                    // [hook] Skill_GetValueByClassA -> 0
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) v37 += ftol((double)base * ((double)v45 * 0.02999999932944775));
                } else {
                    // [hook] Item_GetScaledStat -> 0 ; v30*v31==0
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) v37 += ftol((double)(0 + base) * ((double)v45 * 0.02999999932944775));
                }
                break;
            case 3:
                if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassA -> 0 */ }
                else { /* [hook] Item_GetScaledStat -> 0 */ }
                break;
            case 5:
                if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassA -> 0 */ }
                else { /* [hook] Item_GetScaledStat -> 0 */ }
                break;
            case 9: case 11: case 12:
                // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=4, canal intAttack ; 0x4D235D/0x4D237B).
                if (v34 == 2) v37 += Item_SumGemStatBonus(4, sn.sock[i]);
                break;
            case 10: {
                float v26 = (it->itemId == 204 || it->itemId == 205 || it->itemId == 206 ||
                             it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                             it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                            ? 48.75f : 24.35f;
                if (v34 == 2) {
                    // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=4 ; 0x4D22F2/0x4D2310).
                    v37 += Item_SumGemStatBonus(4, sn.sock[i]);
                } else {
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) v37 += ftol((double)v45 * v26);
                }
                break;
            }
            default: break;
        }
    }

    // enchant loop (...,20) [hook]=0. g_SpecialItem switch [absent]=0. slotGroup(0)!=30.
    // dword_16758E0 buff [absent]=0. Item_ScaleStatByTypeB 0x4C95C0 [hook]=0 (garde -> += 0) —
    //   TODO(verif) : mêmes causes que Item_ScaleStatByTypeA en CalcExternalAttack (caps
    //   dword_8E717C = mPAT au loader stubbé, undumped ; item "sélectionné" absent de SelfState).
    // dword_168744C/1687450 [absent] : pas de +500.
    // g_ElementMastery==1 [absent]. talisman [absent]. Escort [hook]=false.
    // Char_CompareSkillLoadout [hook]=0. dword_16756E4 [absent]=0.
    // SkillTree_SumBonuses(2,..) (0x4D25F7) — CÂBLÉE (audit 2026-07-14) : cf. CalcMaxHP.
    v37 += skillTreeEquipBonus(2, sn, db);
    // Item_GetElementalBonus (0x4D2765) : CÂBLÉE — (s.weaponId, s.equip[1].socket), key=2.
    v37 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 2));
    return v37;
}

// ===========================================================================
// Char_CalcExternalDefense 0x4D2830 — canal 5 (field320).
// ===========================================================================
int CalcExternalDefense(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    int v21 = s.attrExtForce + growth292(gi);        // 0x16731BC + croissance292 (+ g_CoreAttr=0)
    // TODO [runtime absent] 0x4D286B : branche morph. g_AttrBuffActive [absent]=0.

    const int setId = EquipSetBonusId(s, db);        // v20
    Snapshot sn = buildSnapshot(s, db);
    int v29 = 0;

    for (int i = 0; i < 13; ++i) {                   // somme attrPrimaryA (+292)
        const ItemInfo* it = sn.r[i];
        if (it) v21 += it->attrPrimaryA;
    }
    // pet [absent] ; grade [absent].
    // Char_SumGemStatD 0x54CC90 — CÂBLÉE (0x4D2A14), alimente v21 (attrPrimaryA/292).
    v21 += Char_SumGemStatD(s);
    v29 += ftol((double)v21 * 1.710000038146973);    // conversion (0x4D2A47)

    v29 += levelStat(db, lb + lvl, 6);               // baseExtDefense Stat5 (field6)

    for (int i = 0; i < 13; ++i) {                   // somme extDefense (+320) + set + spécial
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->extDefense;                   // +320
        v29 += base;
        if (i == 8) continue;
        switch (setId) {
            case 3: case 9:                   v29 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                  v29 += ftol((double)base * 1.0); break;
            case 8:                           v29 += ftol((double)base * 0.4000000059604645); break;
            case 13: case 18: case 19:        v29 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                 v29 += ftol((double)base * 1.100000023841858); break;
            case 21:                          v29 += ftol((double)base * 0.2000000029802322); break;
            case 22:                          v29 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v29 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    if (classifyRecord(it) == 1 || classifyById(db, it->itemId) == 4)
                        v29 += ftol((double)base * 0.550000011920929);
                    else
                        v29 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v29 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_47 (0x4D2D54) : slots 3 et 7 ---
        if (i == 3) {
            if (classifyRecord(it) == 1 || classifyRecord(it) == 4) {
                int v36 = attrByte0(sn.sock[i]);
                if (v36 > 0) { if (v36 >= 100) v36 -= 100; v29 += 1500 * v36; }
            } else if (it->skillFlag == 2) {
                // [hook] Skill_GetValueByClassB 0x54E980 -> 0
                int v36 = attrByte0(sn.sock[i]);
                if (v36 > 0) v29 += ftol((double)base * ((double)v36 * 0.02999999932944775));
            } else {
                // [hook] Item_GetScaledStat -> 0
                int v36 = attrByte0(sn.sock[i]);
                if (v36 > 0) v29 += ftol((double)(0 + base) * ((double)v36 * 0.02999999932944775));
            }
        } else if (i == 7) {
            if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassB -> 0 */ }
            else { /* [hook] Item_GetScaledStat -> 0 */ }
        }
    }

    // enchant loop (...,30) [hook]=0. bonus stance (0x4D30E3) : état de skill [absent] ->
    //   v17<=2 && dword_167566C(0)==1 faux ; combos [absent]=0 -> v29 += 2*0 = 0. TODO 0x4D30E3.
    // dword_168744C/1687450 [absent]. g_SpecialItem switch [absent]=0. slotGroup(0)!=30.
    // dword_16758E8/1675960 buffs [absent]=0. setId==14 [jamais avec réduit].
    // SkillTree_SumBonuses(5,..) (0x4D330B) — CÂBLÉE (audit 2026-07-14) : cf. CalcMaxHP.
    v29 += skillTreeEquipBonus(5, sn, db);
    // g_ElementMastery==5 [absent]. talisman [absent]. Item_SocketBonusInt [hook]=0 —
    //   TODO(verif) : paire g_SelectedInvItemId/dword_1673260 (0x4D3410), cf. CalcMaxHP.
    //   Escort [hook]=false. (Pas d'appel Item_GetElementalBonus/Item_SumGemStatBonus dans ce
    //   canal — confirmé par décompilation de 0x4D2830, aucun des deux n'est référencé.)
    return v29;
}

// ===========================================================================
// Char_CalcInternalDefense 0x4D34B0 — canal 6 (field324).
// ===========================================================================
int CalcInternalDefense(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    int v39 = s.attrIntForce  + growth296(gi);       // 0x16731C4 + croissance296 (+ g_CoreAttr=0)
    int v32 = s.attrDefensive + growth300(gi);       // 0x16731B8 + croissance300
    // TODO [runtime absent] 0x4D34F6 : branche morph (maybe_StubReturn1B 0x54F070,
    //   Level_ToTierValueA 0x54EFB0). g_AttrBuffActive [absent]=0.

    const int setId = EquipSetBonusId(s, db);        // v23
    Snapshot sn = buildSnapshot(s, db);
    int v31 = 0;

    for (int i = 0; i < 13; ++i) {                   // sommes attrPrimaryB(+296) & attrRatingMin(+300)
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        v39 += it->attrPrimaryB;                     // +296
        v32 += it->attrRatingMin;                    // +300
    }
    // pet [absent] ; grade [absent].
    // Char_SumGemStatB/C 0x54CB80/0x54CC40 — CÂBLÉES (0x4D3762/0x4D3772) : B alimente v39
    // (attrPrimaryB/296), C alimente v32 (attrRatingMin/300).
    v39 += Char_SumGemStatB(s);
    v32 += Char_SumGemStatC(s);
    v31 += ftol((double)v39 * 1.669999957084656);    // 0x4D37BC
    v31 += ftol((double)v32 * 0.8999999761581421);   // 0x4D37D0

    v31 += levelStat(db, lb + lvl, 7);               // baseIntDefense Stat6 (field7)

    for (int i = 0; i < 13; ++i) {                   // somme intDefense (+324) + set + spécial
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->intDefense;                   // +324
        v31 += base;
        if (i == 8) continue;
        switch (setId) {
            case 3: case 4: case 9:           v31 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                  v31 += ftol((double)base * 1.0); break;
            case 8:                           v31 += ftol((double)base * 0.4000000059604645); break;
            case 13: case 14: case 18: case 19: v31 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                 v31 += ftol((double)base * 1.100000023841858); break;
            case 21:                          v31 += ftol((double)base * 0.2000000029802322); break;
            case 22:                          v31 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v31 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    int c = classifyRecord(it);
                    if (c == 1 || c == 4) v31 += ftol((double)base * 0.550000011920929);
                    else                  v31 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v31 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_47 switch(i) (0x4D3B19) :
        switch (i) {
            case 2:
                if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassC 0x54EB20 -> 0 */ }
                else { /* [hook] Item_GetScaledStat -> 0 */ }
                break;
            case 5: {
                int c = classifyRecord(it);
                if (c == 1 || c == 4) {
                    int v40 = attrByte0(sn.sock[i]);
                    if (v40 > 0) { if (v40 >= 100) v40 -= 100; v31 += 300 * v40; }
                } else if (it->skillFlag == 2) {
                    // [hook] Skill_GetValueByClassC -> 0
                    int v40 = attrByte0(sn.sock[i]);
                    if (v40 > 0) v31 += ftol((double)base * ((double)v40 * 0.02999999932944775));
                } else {
                    // [hook] Item_GetScaledStat -> 0
                    int v40 = attrByte0(sn.sock[i]);
                    if (v40 > 0) v31 += ftol((double)(0 + base) * ((double)v40 * 0.02999999932944775));
                }
                break;
            }
            case 9: case 10: case 11: case 12: {
                // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=6, canal intDefense ; 0x4D3D88/0x4D3DA6).
                // NB : slot10 inclus dans le MÊME case ici (contrairement aux autres canaux où le
                // slot10 a un traitement séparé) — confirmé par décompilation de 0x4D34B0.
                int c = classifyRecord(it);
                if (c == 2) v31 += Item_SumGemStatBonus(6, sn.sock[i]);
                break;
            }
            default: break;
        }
    }

    // enchant loop (...,40) [hook]=0. bonus stance (0x4D3ED8) : état skill [absent] -> 0. TODO.
    // dword_168744C/1687450 [absent]. g_SpecialItem switch [absent]=0. slotGroup(0)!=30.
    // dword_16758F0/1675968 buffs [absent]=0.
    // SkillTree_SumBonuses(6,..) (0x4D40FF) — CÂBLÉE (audit 2026-07-14) : cf. CalcMaxHP.
    v31 += skillTreeEquipBonus(6, sn, db);
    // g_ElementMastery==4 [absent].
    // talisman [absent]. Item_SocketBonusInt [hook]=0 — TODO(verif) : paire g_SelectedInvItemId/
    //   dword_1673260 (0x4D4204), cf. CalcMaxHP. Event_CanTrigger 0x54F120 [hook]=false.
    // (Pas d'appel Item_GetElementalBonus dans ce canal — confirmé par décompilation de 0x4D34B0.)
    return v31;
}

// ===========================================================================
// Char_CalcAccuracy 0x4D42D0 — field336.
// ===========================================================================
int CalcAccuracy(const SelfState& s, const GameDatabases& db) {
    const int setId = EquipSetBonusId(s, db);        // v10
    Snapshot sn = buildSnapshot(s, db);
    int v15 = 2;                                     // base 2 (0x4D42D9)

    for (int i = 0; i < 13; ++i) {                   // somme accuracy (+336) + set + slot4
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->accuracy;                     // +336
        v15 += base;
        if (i == 8) continue;
        switch (setId) {
            case 5: case 9: case 10: case 21:
                v15 += ftol((double)base * 0.05000000074505806); break;
            case 22:
                v15 += ftol((double)base * 0.03999999910593033); break;
            default: break;
        }
        if (i == 4 && classifyRecord(it) != 1 && classifyRecord(it) != 4) {
            int v21 = attrByte0(sn.sock[i]);         // sub_545610(slot4 socket)
            if (v21 > 0) v15 += v21 / 4;
        }
    }

    // enchant loop (...,70) [hook]=0 (0x4D449E).
    // dword_1674A58/1675728 [absent]=0 : pas de +5 (0x4D45A2).
    // g_SpecialItem 0x1687310 [absent]=0 : switch/gemmes -> aucun effet (0x4D45CA..0x4D472A).
    // dword_1675928 buff [absent]=0 (0x4D4734).
    // dword_16747BC [absent]=0 : 0<=6 && !=12 -> aucun ajout (0x4D4767).
    switch (setId) {                                 // 0x4D47AB
        case 11: case 15: case 16: v15 += 2; break;
        case 19:                   v15 += 5; break;
        case 20: case 30: case 50: v15 += 7; break;
        default: break;
    }
    // dword_168744C/1687450 [absent] : pas de +1 (0x4D47E1).
    // GemStat_AccuracyPct 0x54CA20 [hook]=0 -> v18 = v15 + ftol(0*v15) = v15.
    // Char_CompareSkillLoadout [hook]=0 : pas de +2.
    // Item_SocketBonusFloat [hook]=0 — TODO(verif) : paire g_SelectedInvItemId/dword_1673260
    //   (0x4D4841), cf. CalcMaxHP. Escort [hook]=false.
    // Item_GetElementalBonus (0x4D488D) : CÂBLÉE — (s.weaponId, s.equip[1].socket), key=7.
    v15 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 7));
    // dword_16756E0 [absent]=0 : pas de +1.
    return v15;
}

// ===========================================================================
// Char_CalcEvasion 0x4D4920 — field364.
// ===========================================================================
int CalcEvasion(const SelfState& s, const GameDatabases& db) {
    const int setId = EquipSetBonusId(s, db);        // v8
    Snapshot sn = buildSnapshot(s, db);
    int v11 = 0;

    for (int i = 0; i < 13; ++i) {                   // somme evasion (+364) + set
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->evasion;                      // +364
        v11 += base;
        if (i == 8) continue;
        switch (setId) {
            case 5: case 10: case 21:
                v11 += ftol((double)base * 0.05000000074505806); break;
            case 22:
                v11 += ftol((double)base * 0.03999999910593033); break;
            default: break;                          // (case default: `continue` d'origine ; base déjà ajoutée)
        }
    }

    // enchant loop (...,80) [hook]=0 (0x4D4A4D).
    switch (setId) {                                 // 0x4D4B60
        case 15:                   v11 += 2; break;
        case 20: case 30: case 50: v11 += 7; break;
        default: break;
    }
    // dword_16747BC [absent]=0 : switch(0) -> aucun ajout (0x4D4B9D).
    // g_SpecialItem 0x1687310 [absent]=0 : slotGroup(0)!=30 -> pas de gemme (0x4D4BD8).
    // GemStat_EvasionPct 0x54CAD0 [hook]=0 -> v13 = v11 + ftol(0*v11) = v11.
    // g_CoreAttr 0x167477C [absent]=0 : v14 = v13 + 0/10 = v13 (0x4D4C72).
    // Escort [hook]=false. Item_GetElementalBonus [hook]=0. dword_16756E4 [absent]=0.
    return v11;
}

// ===========================================================================
// Char_CalcCritRate 0x4D4D70 — field308.
// ===========================================================================
int CalcCritRate(const SelfState& s, const GameDatabases& db) {
    const int setId = EquipSetBonusId(s, db);        // v4
    Snapshot sn = buildSnapshot(s, db);
    int v6 = 0;

    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->critRate;                     // +308
        v6 += base;
        if (i == 8) continue;
        if (setId == 10 || setId == 30 || setId == 50)
            v6 += ftol((double)base * 0.05000000074505806);
        if (i == 0 && classifyRecord(it) != 1 && classifyRecord(it) != 4) {
            int v7 = attrByte0(sn.sock[0]);          // sub_545610(slot0 socket)
            if (v7 > 0) v6 += 12 * v7;
        }
    }
    // dword_1675930 buff [absent]=0 (0x4D4E9D) : pas de multiplicateur %.
    return v6;
}

// ===========================================================================
// Char_CalcAttackRatingMin 0x4CD970 — field300 (rating min de dégâts).
// ===========================================================================
int CalcAttackRatingMin(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    int v39 = s.attrDefensive + growth300(gi) + g_Client.VarGet(0x167477C); // 0x16731B8 + croissance300 + g_CoreAttr (Stat_AddCoreAttr 0x546380 = identité ; 0x4CDA2C/0x4CDA52)
    // TODO [runtime absent] 0x4CD9D9 : branche morph (g_SpecialFormActive 0x16760D4 jamais écrit=0).
    if (g_Client.VarGet(0x16758A8) > 0)                       // g_AttrBuffActive (0x4CDA5C)
        v39 += g_Client.VarGet(0x16758AC) / 100 + g_Client.VarGet(0x16758AC) % 100; // g_AttrBuff300 (0x4CDA7F)
    Snapshot sn = buildSnapshot(s, db);
    int v33 = 0;

    for (int i = 0; i < 13; ++i) {                   // somme attrRatingMin (+300)
        const ItemInfo* it = sn.r[i];
        if (it) v39 += it->attrRatingMin;
    }
    // pet [absent] ; grade [absent].
    // Char_SumGemStatC 0x54CC40 — CÂBLÉE (0x4CDB16), alimente v39 (attrRatingMin/300).
    v39 += Char_SumGemStatC(s);
    v33 += ftol((double)v39 * 20.0);                 // 0x4CDB6D
    // pet-mastery flat +825/+550 [absent] (0x4CDBA4).

    for (int i = 9; i < 13; ++i) {                   // gemmes slots 9..12 (0x4CDBF6)
        const ItemInfo* it = sn.r[i];
        if (it && classifyRecord(it) == 2) {
            // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=1, canal atkRatingMin ; 0x4CDC55).
            v33 += Item_SumGemStatBonus(1, sn.sock[i]);
            if (v33 < 0) v33 = 0;
        }
    }
    // enchant loop {0,2,3,4,5,7} classe1||4 (...,100) [hook]=0 ; plancher v33>=0 (0x4CDC63).

    // bonus méridien/stance (0x4CDD43). Skill_IsCurrentStanceSet/Special 0x4FB0F0/0x4FC800
    // [hook]=false -> prédicat d'entrée (!set && !special || morph∈[319,323]) = true (bloc toujours
    // exécuté). Char_FindSkillSlotByName 0x55D520 [hook] -> v40 reste -1.
    {
        const int v40 = -1;                                    // 0x55D520 [hook]
        if (v40 <= 1 && g_Client.VarGet(0x1675660) == 1) {     // dword_1675660 (0x4CDD7E)
            v33 += 4000;                                       // 0x4CDD89
        } else {
            const int mEl   = g_Client.VarGet(0x16760CC);      // g_MeridianElement (0x4CDDA5)
            const int mTier = g_Client.VarGet(0x16760C0);      // g_MeridianTier
            const int mPts  = g_Client.VarGet(0x16731C8);      // g_MeridianPts_RatingMin
            if (mEl == s.element || mEl == 4) {                // g_LocalElement 0x1673194
                if      (mTier == 10 && mPts < 100) v33 += 2000; // 0x4CDDCA
                else if (mTier == 20 && mPts < 200) v33 += 4000; // 0x4CDDF1
                else if (mTier == 30)               v33 += 6000; // 0x4CDE08
                else                                v33 += 20 * mPts; // 0x4CDE18
            } else {
                v33 += 20 * mPts;                              // 0x4CDE29
            }
        }
    }

    v33 += levelStat(db, lb + lvl, 9);               // baseAtkRatingMin Stat8 (field9) (0x4CDE49)

    if (g_Client.VarGet(0x1674A54) > 0 || g_Client.VarGet(0x1675728) > 0) // dword_1674A54 (case47)/dword_1675728 (case91) ; 0x4CDEA1
        v33 = ftol((double)v33 * 1.200000047683716);          // 0x4CDEAC
    // g_SpecialItem switch [absent]=0 (0x4CDECA). slotGroup(0)!=30 -> pas de gemme (0x4CDF89).
    // Item_ScaleStatByTypeC 0x4CB0D0 [hook]=0 : garde v33>=0 -> += 0 (jamais *2) (0x4CE02B) —
    //   TODO(verif) : mêmes causes que Item_ScaleStatByTypeA en CalcExternalAttack (caps
    //   dword_8E717C = mPAT au loader stubbé, undumped ; item "sélectionné" absent de SelfState).

    int v32 = EquipSetBonusId(s, db);                // recalcul (0x4CE077)
    if (v32 == 13)      v33 += 1000;
    else if (v32 == 18) v33 += 1100;

    int v45 = 0;
    for (int i = 0; i < 13; ++i) {                   // 0x4CE0B6
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        if (classifyRecord(it) == 1 || classifyById(db, it->itemId) == 4) {
            v45 = 1;
            if (i == 0) {
                int v48 = attrByte0(sn.sock[0]);
                if (v48 > 0) { if (v48 > 100) v48 -= 100; v33 += 500 * v48; }
            }
        }
        // SkillTree_SumBonuses(3, g_EquipAux[3*i], dword_16750BC[3*i], dword_16750C0[3*i])
        // fusionné dans CETTE boucle côté binaire (0x4CE18F, appelé à chaque itération i où
        // sn.r[i] existe, indépendamment du if ci-dessus) — câblé plus bas via
        // skillTreeEquipBonus() (addition entière commutative, résultat identique).
    }
    // SkillTree_SumBonuses(3,..) (0x4CE18F) — CÂBLÉE (audit 2026-07-14) : cf. CalcMaxHP.
    v33 += skillTreeEquipBonus(3, sn, db);
    if (v45 == 1) v33 += 30000;                      // 0x4CE1A3

    // GemStat_AtkRatingMinFlat 0x54CA50 [hook]=0.
    if (g_Client.VarGet(0x1675680) == 6) v33 += 1000;          // g_ElementMastery (0x4CE1C8/0x4CE1D2)
    // talisman (0x4CE1E5) : Stat_UnpackCombined 0x54CE40 (garde a2) + Num_ToDigits8 0x54CF00 (digit 10^5).
    {
        const int tslot = g_Client.VarGet(0x1674760);          // g_TalismanSlot
        if (tslot >= 10 && tslot < 20) {
            const int combined = g_Client.VarGet(0x1675664 + 4u * static_cast<uint32_t>(tslot)); // dword_1675664[slot]
            int a2 = 0;                                        // Stat_UnpackCombined .a2 (0x54CE40)
            if (combined >= 0) { a2 = combined / 1000000; if (a2 > 100) a2 = 0; }
            if (a2 > 0) {                                      // 0x4CE20A
                const int digits = g_Client.VarGet(0x167568C + 4u * static_cast<uint32_t>(tslot)); // dword_167568C[slot]
                v33 += 100 * ((digits / 100000) % 10);         // Num_ToDigits8 -> a4/v41 (10^5) ; 0x4CE24E
            }
        }
    }
    // Equip_ComputeGemSetBonus 0x54E420 [hook]=0. Weapon_BaseMinDamage 0x4C99F0 [hook]=0.
    // Item_SocketBonusInt/Float [hook]=0 — TODO(verif) : paire g_SelectedInvItemId/dword_1673260
    //   (0x4CE29A/0x4CE2B9), cf. CalcMaxHP. Weapon_SpecialDamageA 0x4CA230 [hook]=0.
    // Item_GetElementalBonus (0x4CE2F9) : CÂBLÉE — (s.weaponId, s.equip[1].socket), key=3.
    v33 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 3));
    // boucle grade slots (0x4CE31E) : Item_GetGradeStatValues 0x550B20 [hook]=false -> 0.
    // Escort [hook]=false.
    return v33;
}

// ===========================================================================
// Char_CalcAttackRatingMax 0x4CE3F0 — field304 (rating max de dégâts).
// ===========================================================================
int CalcAttackRatingMax(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    int v24 = s.attrOffensive + growth304(gi) + g_Client.VarGet(0x167477C); // 0x16731C0 + croissance304 + g_CoreAttr (0x4CE4A5/0x4CE4CB)
    // TODO [runtime absent] 0x4CE452 : branche morph (g_SpecialFormActive=0 ; maybe_StubReturn1A 0x54F030).
    if (g_Client.VarGet(0x16758A8) > 0)                        // g_AttrBuffActive (0x4CE4D5)
        v24 += g_Client.VarGet(0x16758B0) / 100 + g_Client.VarGet(0x16758B0) % 100; // g_AttrBuff304 (0x4CE4F8)
    Snapshot sn = buildSnapshot(s, db);
    int v23 = 0;

    for (int i = 0; i < 13; ++i) {                   // somme attrRatingMax (+304)
        const ItemInfo* it = sn.r[i];
        if (it) v24 += it->attrRatingMax;
    }
    // pet [absent] ; grade [absent].
    // Char_SumGemStatA 0x54CB00 — CÂBLÉE (0x4CE58F), alimente v24 (attrRatingMax/304).
    v24 += Char_SumGemStatA(s);
    v23 += ftol((double)v24 * 15.3100004196167);     // 0x4CE5C3
    // pet-mastery flat +750/+500 [absent] (0x4CE5FA).

    for (int i = 9; i < 13; ++i) {                   // gemmes slots 9..12 (0x4CE64B)
        const ItemInfo* it = sn.r[i];
        if (it && classifyRecord(it) == 2) {
            // Item_SumGemStatBonus 0x4C3CC0 — CÂBLÉE (key=2, canal atkRatingMax ; 0x4CE688/0x4CE6A6).
            // NB : pas de plancher v23>=0 ici (contrairement à CalcAttackRatingMin) — confirmé
            // par décompilation de 0x4CE3F0.
            v23 += Item_SumGemStatBonus(2, sn.sock[i]);
        }
    }
    // bonus méridien/stance (0x4CE6E1). Skill state [hook]=false -> bloc toujours exécuté ;
    // v25=-1 (Char_FindSkillSlotByName 0x55D520 [hook]).
    {
        const int v25 = -1;
        if (v25 < 4 && g_Client.VarGet(0x1675664) == 1) {      // dword_1675664[0] (index 0, PAS [slot]) ; 0x4CE728
            v23 += 5000;                                       // 0x4CE732
        } else {
            const int mEl   = g_Client.VarGet(0x16760CC);      // g_MeridianElement (0x4CE74F)
            const int mTier = g_Client.VarGet(0x16760C0);      // g_MeridianTier
            const int mPts  = g_Client.VarGet(0x16731CC);      // g_MeridianPts_RatingMax
            if (mEl == s.element || mEl == 4) {
                if      (mTier == 10 && mPts < 100) v23 += 2500; // 0x4CE773
                else if (mTier == 20 && mPts < 200) v23 += 5000; // 0x4CE79B
                else if (mTier == 30)               v23 += 7500; // 0x4CE7B1
                else                                v23 += 25 * mPts; // 0x4CE7C2
            } else {
                v23 += 25 * mPts;                              // 0x4CE7D3
            }
        }
    }

    v23 += levelStat(db, lb + lvl, 10);              // baseAtkRatingMax Stat9 (field10) (0x4CE7F2)

    // g_SpecialItem switch [absent]=0 (0x4CE899).
    // Item_ScaleStatByTypeD 0x4CB3F0 [hook]=0 : garde v23>=0 -> += 0 (jamais *2) (0x4CEA36) —
    //   TODO(verif) : mêmes causes que Item_ScaleStatByTypeA en CalcExternalAttack (caps
    //   dword_8E717C = mPAT au loader stubbé, undumped ; item "sélectionné" absent de SelfState).

    int v19 = EquipSetBonusId(s, db);                // 0x4CEA81
    if (v19 == 12)      v23 += 1000;
    else if (v19 == 17) v23 += 1100;

    // SkillTree_SumBonuses(4,..) (0x4CEAB1) — CÂBLÉE (audit 2026-07-14) : cf. CalcMaxHP.
    v23 += skillTreeEquipBonus(4, sn, db);
    // talisman (0x4CEB20) : Stat_UnpackCombined 0x54CE40 + Num_ToDigits8 0x54CF00 (digit 10^4, ×200).
    {
        const int tslot = g_Client.VarGet(0x1674760);          // g_TalismanSlot
        if (tslot >= 10 && tslot < 20) {
            const int combined = g_Client.VarGet(0x1675664 + 4u * static_cast<uint32_t>(tslot));
            int a2 = 0;
            if (combined >= 0) { a2 = combined / 1000000; if (a2 > 100) a2 = 0; }
            if (a2 > 0) {                                      // 0x4CEB46
                const int digits = g_Client.VarGet(0x167568C + 4u * static_cast<uint32_t>(tslot));
                v23 += 200 * ((digits / 10000) % 10);          // Num_ToDigits8 -> a5/v38 (10^4) ; 0x4CEB8D
            }
        }
    }
    // Weapon_BaseMaxDamage 0x4C9E10 [hook]=0. Item_SocketBonusInt [hook]=0 — TODO(verif) : paire
    //   g_SelectedInvItemId/dword_1673260 (0x4CEBC0), cf. CalcMaxHP.
    // Weapon_SpecialDamageB 0x4CA350 [hook]=0.
    // Item_GetElementalBonus (0x4CEBFB) : CÂBLÉE — (s.weaponId, s.equip[1].socket), key=4.
    v23 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 4));
    return v23;
}

// ===========================================================================
// Char_CalcAttackSpeed 0x4CCAB0 — vitesse d'attaque (base 60).
// ===========================================================================
double CalcAttackSpeed(const SelfState& s, const GameDatabases& db) {
    (void)db;
    float v1 = 60.0f;
    // g_SpecialItem 0x1687310 [runtime absent]=0 : ni la liste d'ids ni slotGroup(0)==30
    //   -> pas de *1.1 (0x4CCD14/0x4CCD37).
    // dword_1675910 buff [absent]=0 ; dword_16759A0 [absent] ; dword_16759B8 [absent] : sans effet.
    if (s.weaponId == 1407 && s.level <= 100) {      // 0x4CCDA3 (déterministe)
        // [hook] Skill_CalcAttackSpeed(2,8) 0x4CCDD0 -> valeur d'origine indéterminée sans
        //   les tables de skill ; on conserve la base. TODO 0x4CCDB1.
    }
    // dword_16759F0 (g_GlobalStunActive) [absent]=0 -> retourne v1 (sinon 0.0) (0x4CCDBB).
    return v1;
}

// ===========================================================================
// Char_CalcWeaponRatePct 0x4CD900 — taux d'arme (base 100).
// a1 = contexte de buffs runtime (a1+24 buff, a1+100 debuff) [absent] ; a2 = mot d'attribut
// d'arme -> GemStat_WeaponRateFactor(octet2)*0.005. On lit l'octet2 du socket de l'arme
// (slot 1). Les buffs (a1+24/a1+100) sont [runtime absent].
// ===========================================================================
double CalcWeaponRatePct(const SelfState& s, const GameDatabases& db) {
    (void)db;
    float v4 = 100.0f;
    // if (*(a1+24) > 0)  v4 += *(a1+24);   [runtime absent] -> 0
    // if (*(a1+100) > 0) v4 *= 0.5;        [runtime absent] -> non pris
    // GemStat_WeaponRateFactor 0x54CA70 = Item_GetAttribByte2(mot)*0.004999999888241291.
    double v2 = (double)attrByte2(s.equip[1].socket) * 0.004999999888241291;
    return (double)(float)((double)ftol(v2 * v4) + v4);
}

// ===========================================================================
// Char_CalcElementResist 0x4D64B0 — résistance élémentaire pour la clé `element`.
// ===========================================================================
int CalcElementResist(const SelfState& s, const GameDatabases& db, int element) {
    Snapshot sn = buildSnapshot(s, db);
    int v7 = 0;

    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        // 8 paires (clé +372+8k, valeur +376+8k).
        for (int k = 0; k < 8; ++k)
            if (it->resist[k].key == element) v7 += it->resist[k].val;
        v7 += it->resistAll;                         // +368 (ajouté par slot)
    }

    // classe de l'item slot8 (dword_1673258 = equip[8].itemId) pour les digits de socket.
    int v9 = classifyById(db, s.equip[8].itemId);
    if (v9 == 7 || v9 == 5 || v9 == 6) {
        // [hook] Item_SocketDigit 0x4CAB40 -> 0 (selon la clé 'g'/'R'/'S'/'i'/'h'/'T').
    }
    // Item_GemSetBonusMultiplier 0x4CA440 [hook]=0. SkillGrowthTbl_GetRecord 0x4C4E90 +
    //   dword_168744C/1687450 [absent] : pas de +1.
    return v7;
}

// ===========================================================================
// Char_CalcRegen 0x4D67F0 — somme regen (field360).
// ===========================================================================
int CalcRegen(const SelfState& s, const GameDatabases& db) {
    Snapshot sn = buildSnapshot(s, db);
    int v4 = 0;
    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (it) v4 += it->regen;                     // +360
    }
    return v4;
}

} // namespace ts2::game

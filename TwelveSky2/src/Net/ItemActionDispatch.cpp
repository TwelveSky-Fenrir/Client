// Net/ItemActionDispatch.cpp — reecriture de Pkt_ItemActionDispatch (opcode 0x1a, EA 0x46A320).
//
// ===========================================================================
//  CARTE DU DISPATCHER (source : idaTs2, EA 0x46A320)
// ===========================================================================
//
//  EN-TETE (0x46A320..0x46A40D) — le payload est 4 dwords consecutifs, recopies
//  depuis le buffer de reception (unk_8156C1..) vers des locales :
//     payload[0] -> var_414 : drapeau d'action / code resultat serveur
//                             (0 = executer l'action ; !=0 = juste notifier).
//     payload[1] -> var_42C : LIGNE   de la cellule d'inventaire.
//     payload[2] -> var_43C : COLONNE de la cellule d'inventaire.
//     payload[3] -> var_428 : index destination « D » (conteneur/ceinture/rack).
//     g_GmCmdCooldownLatch (0x1675B08) est remis a 0.
//     Cellule = g_InvMain[(ligne%100)*0x600 + (colonne%100)*0x18] -> itemId.
//     item = MobDb_GetEntry(mITEM, itemId) (fiche ITEM_INFO 436 o) ; nul -> retour.
//
//  SWITCH EXTERNE (0x46A426) sur item->typeCode (ITEM_INFO +0xBC = +188).
//  Aiguillage via byte_487D28[typeCode-5] -> jpt_46A44F (5 blocs + defaut) :
//   ---------------------------------------------------------------------------
//   typeCode | bloc                | EA        | role
//   ---------|---------------------|-----------|-------------------------------
//     5      | A SkillLearn        | 0x46A456  | livre de competence -> barre
//     6..22  | B EquipSwap         | 0x46A8A1  | equiper (echange sac<->slot)
//     28,29  |   idem              |           |
//     31,32  |   idem              |           |
//     23,24  | C SpecialContainer  | 0x46AC7F  | ranger dans conteneur special
//     26     | D AutoPotionBelt    | 0x46AE24  | ceinture auto-potion
//     35,36  | E ThrowWeaponRack   | 0x46AF76  | rack d'armes de jet / familier
//     25,27  | (cascade route 5)   | 0x46B10F  | consommable : mega-switch d'effets
//     30,33,34| (cascade route 5)   |           | (def_46A44F, PAS l'epilogue 0x46B168)
//    hors 5..36 (cascade directe)  | 0x46B10F  |
//   ---------------------------------------------------------------------------
//
//  DEUX cibles de saut DISTINCTES (correction H3, prouvee IDA 0x46A426) :
//    - def_46B168 (label 0x487CFE, code 0x46B168) = EPILOGUE / no-op de RETOUR.
//      Tous les blocs A-E (et leurs sorties anticipees flag!=0) y sautent : ils
//      RETOURNENT, ils ne tombent PAS sur la cascade.
//    - def_46A44F (0x46B10F) = LA CASCADE : un ENORME sous-switch (>1000 cas, sur
//      var_480 = item->itemId) qui applique l'effet consommable (potions, buffs,
//      teleports, transformations, etc.). Elle n'est atteinte QUE par la route 5 du
//      switch externe (typeCodes 25,27,30,33,34) ET par les typeCodes hors [5,36]
//      (46A435 cmp 0x1F ; 46A43C ja def_46A44F). Hors perimetre ici -> voir
//      Net/ItemEffectDispatch.cpp.
//
//  Ce module implemente FIDELEMENT : l'en-tete, le routage externe, le bloc A
//  (apprentissage de competence, complet), le bloc B (echange d'equipement, avec
//  l'etat FSM self.mode pose au tail = g_SelfActionState 0x1687328), et les
//  stockages + vidage de cellule des blocs C/D/E. Les queues profondes (recalcul
//  de stats, formatage de messages localises, mega-switch d'effets) sont laissees
//  en // TODO(@EA).
//
//  Etat mute : g_Client.inv (grille), g_World.self (equip/points de comp.), et la
//  longue traine de globals SoA via g_Client.Var(adresseOrigine).
// ===========================================================================

#include "Net/ItemActionDispatch.h"
#include "Net/ItemEffectDispatch.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"

#include <cstdint>
#include <cstring>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Adresses d'origine des globals SoA non modelises en champ propre. On les
// reproduit bit-a-bit via g_Client.Var(addr) (echappatoire prevu par le socle).
// ---------------------------------------------------------------------------
namespace {

// Barre de competences apprises : 40 slots, stride 8 = {+0 skillID, +4 spCost}.
constexpr uint32_t kLearnedSkills_Id   = 0x16742BC; // g_LearnedSkills
constexpr uint32_t kLearnedSkills_Cost = 0x16742C0; // dword_16742C0

// Equipement principal : 13 slots, stride 0x10 = {+0 main, +4 durab, +8 socket, +C serial}.
constexpr uint32_t kEquipMain       = 0x16731D8;
constexpr uint32_t kEquipDurability = 0x16731DC;
constexpr uint32_t kEquipSocket     = 0x16731E0; // g_Slot0Socket
constexpr uint32_t kEquipSerial     = 0x16731E4;
// Equipement auxiliaire : 13 slots, stride 0x0C = {+0, +4, +8}.
constexpr uint32_t kEquipAux0 = 0x16750B8;
constexpr uint32_t kEquipAux1 = 0x16750BC;
constexpr uint32_t kEquipAux2 = 0x16750C0;
// Miroir de rendu de l'equipement : 13 slots, stride 8 = {+0 main, +4 socket}.
constexpr uint32_t kEquipVisible_Main   = 0x16872A8;
constexpr uint32_t kEquipVisible_Socket = 0x16872AC;

// Inventaire auxiliaire (SoA parallele a la grille) : ligne stride 0x300, colonne 0x0C.
constexpr uint32_t kInvAux0 = 0x1674AB8;
constexpr uint32_t kInvAux1 = 0x1674ABC;
constexpr uint32_t kInvAux2 = 0x1674AC0;

// Conteneurs specialises.
constexpr uint32_t kSpecialContainer = 0x1675808; // cat 23/24 : [(type-23)*0x38 + D*4] = itemID
constexpr uint32_t kAutoPotionBelt   = 0x16757B0; // cat 26    : [D*4] = itemID
constexpr uint32_t kAutoPotionTimer  = 0x16757D8; // cat 26    : [D*4] = 30
constexpr uint32_t kThrowWeaponRack  = 0x16749FC; // cat 35/36 : [D*4] = itemID

constexpr uint32_t kGmCmdCooldownLatch = 0x1675B08;

// Identifiants de chaines localisees 005.DAT (StrTable005_Get).
constexpr int kMsg_SkillLearned = 0x1A9; // « competence apprise »
constexpr int kMsg_PetNotUsable = 0x74D; // familier deja actif / non utilisable

// Lecture d'un dword a un offset d'une fiche brute (fiche SKILL_INFO 776 o, etc.).
inline uint32_t RecU32(const uint8_t* rec, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, rec + off, sizeof(v));
    return v;
}

// Fiche SKILL_INFO 1-based (mSKILL / SkillGrowthTbl_GetRecord 0x4C4E90).
const uint8_t* GetSkillRecord(uint32_t skillId) {
    if (skillId == 0 || skillId > g_World.db.skill.count) return nullptr;
    return g_World.db.skill.record(skillId - 1);
}

// ---------------------------------------------------------------------------
// Bloc A — apprentissage de competence (typeCode 5). EA 0x46A456..0x46A89C.
// L'objet est un « livre » : on lit item->field348 (skillId), on resout la fiche
// SKILL_INFO, et selon skillRec[+0x21C] (1..4) on cherche un slot libre dans une
// plage de la barre g_LearnedSkills, puis on « valide » (loc_46A727) : depense des
// points, ecriture du slot, vidage de la cellule, message, note de quickslot.
// ---------------------------------------------------------------------------

// Cherche le 1er slot libre dans [debut,fin) (libre = skillID < 1). Renvoie `fin`
// si aucun (comportement exact des boucles 0x46A4D6.., 0x46A531.., etc.).
int ScanFreeSkillSlot(int debut, int fin) {
    int i = debut;
    while (i < fin) {
        if (static_cast<int32_t>(g_Client.Var(kLearnedSkills_Id + i * 8)) < 1) break; // libre
        ++i;
    }
    return i;
}

// Validation commune (loc_46A727) : ecrit la competence dans le slot `slot`,
// depense skillRec[+0x230] points, vide la cellule (ligne,colonne), affiche le
// message 0x1A9, et (re)initialise la fenetre de quickslot selon la plage du slot.
void CommitSkillLearn(const uint8_t* skillRec, int slot, uint32_t row, uint32_t col) {
    // g_SkillPointPool -= skillRec[+0x230]   (0x46A72D)
    const uint32_t cost = RecU32(skillRec, 0x230);
    g_World.self.skillPoints -= static_cast<int>(cost);
    // g_LearnedSkills[slot] = {skillID, cost}   (0x46A74D / 0x46A766)
    g_Client.Var(kLearnedSkills_Id   + slot * 8) = static_cast<int32_t>(RecU32(skillRec, 0x0));
    g_Client.Var(kLearnedSkills_Cost + slot * 8) = static_cast<int32_t>(cost);
    // Vidage complet de la cellule source   (0x46A76D..0x46A82D)
    g_Client.inv.ClearCell(row % 100, col % 100);
    // Msg_AppendSystemLine(StrTable005_Get(0x1A9), g_SysMsgColor)   (0x46A82D)
    g_Client.msg.System(Str(kMsg_SkillLearned));
    // cQuickSlotWin_Init(dword_18392C0, page) selon la plage du slot   (0x46A84E..)
    //   slot<0x0A -> page 0 ; <0x14 -> page 1 ; <0x1E -> page 2.
    // TODO(@0x46A84E) : reinitialisation de la fenetre de quickslot (UI hors socle).
}

// Renvoie true si l'objet a ete traite (apprentissage tente), false pour tomber
// sur le defaut sans rien faire (echec/plein).
void HandleSkillLearn(const ItemInfo* item, uint32_t row, uint32_t col) {
    // (0x46A456) var_414 doit valoir 0 pour agir ; sinon -> defaut.
    // (garde deja filtree par l'appelant via `flag`)
    const uint32_t skillId = item->field348;               // itemRec[+0x15C]  (0x46A46A)
    const uint8_t* skillRec = GetSkillRecord(skillId);      // mSKILL[skillId]
    if (!skillRec) return;                                  // (0x46A488) nul -> defaut

    const uint32_t skillType = RecU32(skillRec, 0x21C);     // switch 1..4  (0x46A495)
    int slot;
    switch (skillType) {
    case 1: // (0x46A4CA) plage 0..9
        slot = ScanFreeSkillSlot(0, 0x0A);
        if (slot == 0x0A) return;                           // plein -> defaut
        // [audio] Snd3D_PlayScaledVolume(flt_1494F7C)  (0x46A510)
        CommitSkillLearn(skillRec, slot, row, col);
        return;

    case 2: // (0x46A525) plage 20..29
        slot = ScanFreeSkillSlot(0x14, 0x1E);
        if (slot == 0x1E) return;                           // plein -> defaut
        // Sous-switch sonore sur skillRec[+0x220] (2..5), sans effet d'etat.
        //   (0x46A56B) case2->flt_149503C, case3->flt_14950FC,
        //              case4->flt_14951BC, case5->flt_149527C  [audio]
        CommitSkillLearn(skillRec, slot, row, col);
        return;

    case 3: // (0x46A5ED) plage 10..19, sinon 30..39
    case 4: // (0x46A689) identique au case 3
        slot = ScanFreeSkillSlot(0x0A, 0x14);
        if (slot == 0x14) {                                 // 10..19 plein
            slot = ScanFreeSkillSlot(0x1E, 0x28);           // essai 30..39
            if (slot == 0x28) return;                       // plein -> defaut
        }
        // [audio] Snd3D_PlayScaledVolume(flt_1494F7C)  (0x46A674 / 0x46A710)
        CommitSkillLearn(skillRec, slot, row, col);
        return;

    default: // (def_46A4C3, 0x46A722) -> defaut
        return;
    }
}

// ---------------------------------------------------------------------------
// Bloc B — echange d'equipement (typeCode 6..22, 28,29,31,32). EA 0x46A8A1.
// slot = item->subtype - 2 (itemRec[+0xD8]-2), doit etre dans [0,12]. On ECHANGE
// la cellule d'inventaire (ligne,colonne) avec le slot d'equipement : l'objet du
// sac va dans le slot, l'ancien equipement redescend dans la cellule.
// ---------------------------------------------------------------------------
void HandleEquipSwap(const ItemInfo* item, uint32_t row, uint32_t col) {
    // (0x46A8A1) var_414 doit valoir 0 (deja filtre par l'appelant).
    const int slot = static_cast<int>(item->subtype) - 2;  // itemRec[+0xD8]-2  (0x46A8B5)
    if (slot < 0 || slot > 0x0C) return;                   // (0x46A8C4/CD) -> defaut

    InvCell& cell = g_Client.inv.At(row % 100, col % 100);
    EquipSlot& eq = g_World.self.equip[slot];

    // 1) Sauvegarde de l'ancien equipement (var_410..var_3F8).   (0x46A8DD..0x46A961)
    const uint32_t oldMain    = eq.itemId;                       // g_EquipMain
    const uint32_t oldDurab   = eq.extra0;                       // g_EquipDurability
    const uint32_t oldSocket  = eq.socket;                       // g_Slot0Socket
    const uint32_t oldSerial  = eq.extra1;                       // g_EquipSerial
    const int32_t  oldAux0    = g_Client.Var(kEquipAux0 + slot * 0x0C);
    const int32_t  oldAux1    = g_Client.Var(kEquipAux1 + slot * 0x0C);
    const int32_t  oldAux2    = g_Client.Var(kEquipAux2 + slot * 0x0C);

    // 2) Cellule d'inventaire -> slot d'equipement.   (0x46A967..0x46AA8E)
    //   Correspondance SoA : InvMain->EquipMain, InvGrid_Count->EquipDurability,
    //   InvGrid_Durability->Slot0Socket, InvGrid_InstanceSerial->EquipSerial,
    //   InvAux[0..2]->EquipAux[0..2].
    eq.itemId = cell.itemId;        // InvMain            -> g_EquipMain
    eq.extra0 = cell.flag;          // InvGrid_Count      -> g_EquipDurability
    eq.socket = cell.color;         // InvGrid_Durability -> g_Slot0Socket
    eq.extra1 = cell.durability;    // InvGrid_InstanceSerial -> g_EquipSerial
    g_Client.Var(kEquipAux0 + slot * 0x0C) = static_cast<int32_t>(g_Client.Var(kInvAux0 + (row % 100) * 0x300 + (col % 100) * 0x0C));
    g_Client.Var(kEquipAux1 + slot * 0x0C) = static_cast<int32_t>(g_Client.Var(kInvAux1 + (row % 100) * 0x300 + (col % 100) * 0x0C));
    g_Client.Var(kEquipAux2 + slot * 0x0C) = static_cast<int32_t>(g_Client.Var(kInvAux2 + (row % 100) * 0x300 + (col % 100) * 0x0C));

    // Miroir de rendu.   (0x46AA94..0x46AAC5)
    g_Client.Var(kEquipVisible_Main   + slot * 8) = static_cast<int32_t>(eq.itemId);
    g_Client.Var(kEquipVisible_Socket + slot * 8) = static_cast<int32_t>(eq.socket);

    // 3) Ancien equipement -> cellule d'inventaire.   (0x46AACC..0x46AB16 et suite
    //    symetrique inferee 0x46AB10..). GridX/GridY conserves (l'equipement n'a
    //    pas de position en grille).
    cell.itemId     = oldMain;      // var_410 -> g_InvMain
    cell.flag       = oldDurab;     // var_40C -> g_InvGrid_Count
    cell.color      = oldSocket;    // var_408 -> g_InvGrid_Durability
    cell.durability = oldSerial;    // var_404 -> g_InvGrid_InstanceSerial
    g_Client.Var(kInvAux0 + (row % 100) * 0x300 + (col % 100) * 0x0C) = oldAux0;
    g_Client.Var(kInvAux1 + (row % 100) * 0x300 + (col % 100) * 0x0C) = oldAux1;
    g_Client.Var(kInvAux2 + (row % 100) * 0x300 + (col % 100) * 0x0C) = oldAux2;

    // Tail bloc B (equip-swap reussi) : etat FSM de self. (0x46abcb)
    //   mov ds:g_SelfActionState(0x1687328), 1  -- valeur 1, inconditionnelle sur le
    //   chemin swap-reussi. self.mode ≡ g_SelfActionState (lu par CombatResultApply
    //   quand mode in {1,5,6,7}). Sans cet ecrivain, SelfModeDeclenche() lit toujours 0.
    g_World.self.mode = 1;                                      // (0x46abcb) g_SelfActionState = 1

    // TODO(@0x46AB16..0x46B168) : queue du bloc B — recalcul des stats derivees
    //   (Char_CalcAttackRatingMin/Max 0x4CD970/0x4CE3F0, snapshot g_EquipSnapshotScratch),
    //   effet sonore et rafraichissement UI, avant de tomber sur le defaut.
}

// ---------------------------------------------------------------------------
// Bloc C — conteneur special (typeCode 23,24). EA 0x46AC7F.
// Si flag!=0 : formate un message localise (0xBB8) avec le nom de l'objet -> TODO.
// Sinon : range l'itemID dans g_SpecialContainer[(type-23)*0x38 + D*4], vide la
// cellule, joue un son, prepare un message.
// ---------------------------------------------------------------------------
void HandleSpecialContainer(const ItemInfo* item, uint32_t flag, uint32_t row,
                            uint32_t col, uint32_t dstD) {
    if (flag != 0) {
        // (0x46AC88) memset(buf,0,0x3E8) ; StrTable005_Get(0xBB8) ; vsnprintf(buf, fmt, name)
        // TODO(@0x46AC88) : formatage/affichage du message localise 0xBB8 (item->name).
        return;
    }
    // (0x46ACCC) idx = typeCode - 0x17 ; conteneur[idx*0x38 + D*4] = itemID.
    const uint32_t idx = item->typeCode - 0x17;             // 0 ou 1
    g_Client.Var(kSpecialContainer + idx * 0x38 + dstD * 4) = static_cast<int32_t>(item->itemId);
    // Vidage de la cellule.   (0x46ACFF..0x46ADBF)
    g_Client.inv.ClearCell(row % 100, col % 100);
    // [audio] Snd3D_PlayScaledVolume(unk_1495ABC)   (0x46ADBF)
    // TODO(@0x46ADCF) : second message localise (memset + StrTable005 + vsnprintf).
}

// ---------------------------------------------------------------------------
// Bloc D — ceinture auto-potion (typeCode 26). EA 0x46AE24.
// Si flag!=0 -> defaut. Sinon : belt[D]=itemID, timer[D]=30, vide la cellule.
// ---------------------------------------------------------------------------
void HandleAutoPotionBelt(const ItemInfo* item, uint32_t row, uint32_t col, uint32_t dstD) {
    // (0x46AE24) flag deja filtre par l'appelant.
    g_Client.Var(kAutoPotionBelt + dstD * 4) = static_cast<int32_t>(item->itemId); // (0x46AE40)
    g_Client.Var(kAutoPotionTimer + dstD * 4) = 0x1E;                              // 30 (0x46AE4D)
    g_Client.inv.ClearCell(row % 100, col % 100);                                  // (0x46AE58..)
    // [audio] Snd3D_PlayScaledVolume(unk_1495ABC)   (0x46AF18)
    // TODO(@0x46AF28) : message localise 0xBB7 (memset + StrTable005 + vsnprintf).
}

// ---------------------------------------------------------------------------
// Bloc E — rack d'armes de jet / familier (typeCode 35,36). EA 0x46AF76.
// Si flag!=0 : message systeme 0x74D -> defaut. Sinon : rack[D]=itemID, vide la
// cellule, recalcule le rating d'attaque.
// ---------------------------------------------------------------------------
void HandleThrowWeaponRack(const ItemInfo* item, uint32_t flag, uint32_t row,
                           uint32_t col, uint32_t dstD) {
    if (flag != 0) {
        // (0x46AF7F) Msg_AppendSystemLine(StrTable005_Get(0x74D), g_SysMsgColor)
        g_Client.msg.System(Str(kMsg_PetNotUsable));
        return;
    }
    // (0x46AFA4) rack[D] = itemID ; vidage de la cellule.
    g_Client.Var(kThrowWeaponRack + dstD * 4) = static_cast<int32_t>(item->itemId);
    g_Client.inv.ClearCell(row % 100, col % 100);
    // TODO(@0x46B079..0x46B168) : recalcul du rating d'attaque min/max
    //   (Char_CalcAttackRatingMin 0x4CD970, Char_CalcAttackRatingMax 0x4CE3F0 ;
    //    ecritures dword_168736C/1687370/1687374) avant de tomber sur le defaut.
}

// ---------------------------------------------------------------------------
// Table de routage externe byte_487D28[typeCode-5] (32 octets, valeur exacte du
// binaire) -> index de bloc : 0=A, 1=B, 2=C, 3=D, 4=E, 5=defaut.
// ---------------------------------------------------------------------------
const uint8_t kTypeRoute[32] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // typeCode 5..20
    1, 1, 2, 2, 5, 3, 5, 1, 1, 5, 1, 1, 5, 5, 4, 4, // typeCode 21..36
};

} // namespace

// ===========================================================================
//  Point d'entree.
// ===========================================================================
void ApplyItemActionDispatch(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 16) return;                       // le binaire lit 16 o fixes

    // --- En-tete : 4 dwords LE (0x46A343..0x46A385). ---
    uint32_t flag, row, col, dstD;                          // var_414, var_42C, var_43C, var_428
    std::memcpy(&flag, payload + 0, 4);
    std::memcpy(&row,  payload + 4, 4);
    std::memcpy(&col,  payload + 8, 4);
    std::memcpy(&dstD, payload + 12, 4);

    // g_GmCmdCooldownLatch = 0   (0x46A3BD)
    g_Client.Var(kGmCmdCooldownLatch) = 0;

    // --- Cellule d'inventaire -> itemID -> fiche ITEM_INFO. (0x46A3C7..0x46A40D) ---
    const InvCell& cell = g_Client.inv.At(row % 100, col % 100);
    const ItemInfo* item = GetItemInfo(cell.itemId);        // MobDb_GetEntry(mITEM, itemId)
    if (!item) return;                                      // (0x46A40D) nul -> retour

    // --- Switch externe sur item->typeCode (ITEM_INFO +0xBC). (0x46A426) ---
    // H3 (BUG CRITIQUE corrige) : la cascade def_46A44F 0x46B10F (mega-switch d'effets)
    // est atteinte UNIQUEMENT par la route 5 (typeCodes 25,27,30,33,34) et par les
    // typeCodes hors [5,36]. Les blocs A-E, eux, RETOURNENT (ils sautent a l'epilogue
    // def_46B168 0x487CFE, DISTINCT de la cascade) — ils ne doivent PAS enchainer sur
    // ApplyItemEffectDispatch.
    const uint32_t typeCode = item->typeCode;

    // Hors [5,36] : 46A435 cmp var_474,0x1F ; 46A43C ja def_46A44F -> cascade directe.
    if (typeCode < 5 || typeCode > 36) {
        ApplyItemEffectDispatch(item, flag, row, col, dstD);    // def_46A44F 0x46B10F
        return;
    }

    switch (kTypeRoute[typeCode - 5]) {                         // byte_487D28[typeCode-5] -> jpt_46A44F 0x487D10
    case 0: // A — apprentissage. flag!=0 -> def_46B168 (epilogue) ; sinon traite -> def_46B168.
        if (flag == 0) HandleSkillLearn(item, row, col);        // 0x46A456 ; 0x46A45F jmp def_46B168
        return;                                                 // toutes sorties bloc A -> def_46B168 (RETOUR, pas cascade)
    case 1: // B — echange d'equipement. flag!=0 -> def_46B168.
        if (flag == 0) HandleEquipSwap(item, row, col);         // 0x46A8A1
        return;
    case 2: // C — conteneur special (gere flag en interne). 0x46AC7F -> def_46B168.
        HandleSpecialContainer(item, flag, row, col, dstD);
        return;
    case 3: // D — ceinture auto-potion. flag!=0 -> def_46B168.
        if (flag == 0) HandleAutoPotionBelt(item, row, col, dstD); // 0x46AE24
        return;
    case 4: // E — rack d'armes de jet / familier (gere flag en interne). 0x46AF76 -> def_46B168.
        HandleThrowWeaponRack(item, flag, row, col, dstD);
        return;
    case 5: // route defaut du switch externe = def_46A44F cascade (typeCodes 25,27,30,33,34).
    default:
        // =======================================================================
        //  CASCADE def_46A44F (0x46B10F) — mega-switch d'effets consommables.
        //  Cle du switch : var_480 = ITEM_INFO[+0] == item->itemId. Aiguillage par
        //  plages sur item->itemId (sous-tables byte_487E90/488014/4882F8/4884B4/
        //  488584/4886F0), handlers 0x46B658.. . Voir Net/ItemEffectDispatch.cpp.
        ApplyItemEffectDispatch(item, flag, row, col, dstD);    // def_46A44F 0x46B10F
        return;
    }
}

} // namespace ts2::game

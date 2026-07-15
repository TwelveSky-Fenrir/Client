// Net/RecvPackets.h — structs + parseurs des paquets ENTRANTS (S->C) de TwelveSky2.
// GÉNÉRÉ (workflow ts2-net-handlers-codegen) depuis les décompilations Hex-Rays.
// Framing entrant : [opcode:u8][payload]. Chaque struct décode le payload (LE, byte-exact).
// La logique de MISE A JOUR d'état (globals/tableaux d'entités) est documentée dans
// RE/net_handler_notes.md et sera branchée au jalon Game/.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Pkt_EnterWorld (opcode 0x0c) — chargement de la zone/monde, ré-init des tableaux d'entités.
// Payload = 2 blocs contigus copiés en vrac : le bloc perso/inventaire (10088 o) puis le bloc d'état de zone (288 o).
struct EnterWorld {
    uint8_t selfCharInvBlock[10088]; // payload+0     -> g_SelfCharInvBlock (perso local + inventaire)
    uint8_t zoneStateBlock[288];     // payload+10088 -> dword_16758D8 (état de zone)
    static EnterWorld Parse(const uint8_t* payload, size_t len);
};

// Pkt_SpawnNpc (opcode 0x13) — création/mise à jour/suppression d'un NPC.
struct SpawnNpc {
    uint32_t idHi;      // payload+0  -> dword_17AB538 (id réseau haut)
    uint32_t idLo;      // payload+4  -> dword_17AB53C (id réseau bas)
    uint8_t  body[84];  // payload+8  -> dword_17AB544 (corps NPC ; body[0..3] = id modèle mob-db)
    uint32_t action;    // payload+92 (== 3 : suppression/despawn ; sinon création/refresh)
    static SpawnNpc Parse(const uint8_t* payload, size_t len);
};

// Pkt_GroundItemRemove (opcode 0x19) — décrémente/retire une pile d'objet au sol (conteneur/slot).
struct GroundItemRemove {
    uint32_t status;         // payload+0 (0 = retrait effectif ; 1 = simple libération du latch)
    uint32_t containerIndex; // payload+4 (index conteneur, base *42 dans dword_1674400)
    uint32_t slotIndex;      // payload+8 (index slot, base *3)
    static GroundItemRemove Parse(const uint8_t* payload, size_t len);
};

// Pkt_ItemResultSimple (opcode 0x21) — résultat simple d'action objet : ré-écrit une cellule d'inventaire.
struct ItemResultSimple {
    uint32_t status;         // payload+0 (0 = succès, ré-applique la cellule)
    uint32_t itemId;         // payload+4  -> g_InvMain
    uint32_t gridX;          // payload+8  -> g_InvGrid_GridX
    uint32_t gridY;          // payload+12 -> g_InvGrid_GridY
    uint32_t count;          // payload+16 -> g_InvGrid_Count
    uint32_t durability;     // payload+20 -> g_InvGrid_Durability
    uint32_t instanceSerial; // payload+24 -> g_InvGrid_InstanceSerial
    static ItemResultSimple Parse(const uint8_t* payload, size_t len);
};

// Pkt_PlayerShopBuyResult (opcode 0x88) — résultat d'achat/maj boutique-joueur + resync de la grille marchand.
// shopBlock (824 o) : préfixe chaîne (nom vendeur, utilisé en "%s%s"), puis grille 5x5 d'entrées objet
// (5 dwords/cellule à partir de +16, pas 20 o, base ligne 100 o) et bloc prix 5x5 (3 dwords/cellule à +516, pas 12 o, base 60 o).
struct PlayerShopBuyResult {
    uint32_t resultCode;   // payload+0   (0/1000 = succès resync ; 100/1..5/'e'..'i' = codes d'erreur -> messages)
    uint8_t  shopBlock[824];// payload+4   -> nom vendeur + grille 5x5 (dword_18229EC..) + prix (dword_1822D70..)
    uint32_t dstRow;       // payload+828 (ligne inventaire destination, base *384)
    uint32_t dstCol;       // payload+832 (colonne, base *6)
    uint32_t itemCell[10]; // payload+836 (cellule objet+aux : id,gridX,gridY,count,durab,serial,aux0,aux1,aux2,poids)
    static PlayerShopBuyResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_DuelResult (opcode 0x2c) — résultat de duel/entrepôt ; ouvre la fenêtre entrepôt si flag==1.
struct DuelResult {
    uint32_t param; // payload+0 (transmis à UI_Warehouse_Open comme 2e arg)
    uint32_t flag;  // payload+4 (==1 : ouvre l'entrepôt en mode 2)
    static DuelResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_TradeActionResult (opcode 0x33) — résultat accept/annulation d'échange, remet à zéro l'état de trade.
struct TradeActionResult {
    uint32_t code; // payload+0 (0=annulé, 1/2=accepté par soi/partenaire, 3=terminé)
    static TradeActionResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnFactionBoardSync (opcode 0x3a) — resync du carquois/faction (code de statut uniquement).
struct FactionBoardSync {
    uint32_t code; // payload+0 (0 = applique le staging carquois ; 1 = échec, ferme l'UI)
    static FactionBoardSync Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptOpen_Dlg20 (opcode 0x41) — ouvre la boîte de confirmation 20 (avec un nom/texte de 13 o).
struct ConfirmPromptOpen_Dlg20 {
    char nameText[13]; // payload+0 (chaîne nom, injectée dans "[%s]%s")
    static ConfirmPromptOpen_Dlg20 Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptClose_Dlg10 (opcode 0x48) — ferme la boîte de dialogue 10 si active. AUCUN champ de payload.
struct ConfirmPromptClose_Dlg10 {
    // Le handler ne lit rien depuis le payload (aucun sub_75C740/Crt_Memcpy sur unk_8156C1).
    static ConfirmPromptClose_Dlg10 Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildRosterUpdate (opcode 0x4f) — dissolution/départ d'alliance, entretien du roster.
struct GuildRosterUpdate {
    uint32_t code;    // payload+0 (1/2/3 : différents cas de départ/dissolution)
    char     name[13];// payload+4 (nom concerné, 13 o)
    static GuildRosterUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnTeamSlotAssign (opcode 0x56) — écrit une valeur dans le slot d'équipe courant.
struct TeamSlotAssign {
    uint32_t value; // payload+0 -> dword_1839EDC[dword_183A020]
    static TeamSlotAssign Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyInviteResult (opcode 0x5d) — résultat d'invitation de groupe (5 cas de message).
struct PartyInviteResult {
    uint32_t code;  // payload+0 (1..5 : cas de résultat)
    uint32_t param; // payload+4 (id/valeur injectée dans certains messages "[%d]%s")
    static PartyInviteResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnPetSlotDispatch (opcode 0x66) — MEGA-DISPATCHER pet/monture (8 sous-opcodes), gère g_TalismanSlot & stats.
struct PetSlotDispatch {
    uint32_t subop; // payload+0 (1..8 : action)
    uint32_t value; // payload+4 (valeur/attribut selon le sous-opcode)
    static PetSlotDispatch Parse(const uint8_t* payload, size_t len);
};

// Net_OnVendorInventoryLoad (opcode 0x6d) — charge la table d'objets vendeur et reconstruit la grille par onglet.
struct VendorInventoryLoad {
    uint32_t status;             // payload+0 (0 = construit la grille ; !=0 = marque dword_1837E64=1)
    uint32_t param;              // payload+4 -> dword_1837E6C (id/param vendeur)
    uint32_t shopItemTable[2240];// payload+8 -> g_ShopItemTable (8960 o = 2240 dwords)
    static VendorInventoryLoad Parse(const uint8_t* payload, size_t len);
};

// Net_OnMinigameStateLoad (opcode 0x76) — charge 4 dwords d'état de mini-jeu.
struct MinigameStateLoad {
    uint32_t a; // payload+0  -> dword_1675D20
    uint32_t b; // payload+4  -> dword_1675D24
    uint32_t c; // payload+8  -> dword_1675D28
    uint32_t d; // payload+12 -> dword_1675D2C
    static MinigameStateLoad Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyMemberTargetSet (opcode 0x7b) — fixe la cible d'un membre de groupe (résolu par id réseau).
struct PartyMemberTargetSet {
    uint32_t idHi;      // payload+0 (id réseau haut du membre -> dword_1687238)
    uint32_t idLo;      // payload+4 (id réseau bas -> dword_168723C)
    uint32_t targetVal; // payload+8 (valeur cible, stockée en u16 dans word_1687454)
    static PartyMemberTargetSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnPlayerEquipVisual (opcode 0x83) — charge les 7 chaînes d'apparence d'équipement (par élément).
// visual (364 o) = 4 blocs d'élément * 91 o ; chaque bloc = 7 slots * 13 o (nom d'apparence).
struct PlayerEquipVisual {
    uint8_t visual[364]; // payload+0 (visual[91*element + 13*slot], element choisi par g_LocalElement, slot 0..6)
    static PlayerEquipVisual Parse(const uint8_t* payload, size_t len);
};

// Net_OnFriendListEvent (opcode 0x90) — notice d'ajout/retrait d'ami (4 cas), avec nom & classe.
struct FriendListEvent {
    uint32_t code;    // payload+0 (0..3 : type de notice)
    uint32_t param;   // payload+4 (classe/compteur selon le cas ; Str_GetClassLabel(param))
    char     name[13];// payload+8 (nom de l'ami, 13 o)
    static FriendListEvent Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemBatchUpdate (opcode 0x95) — maj groupée de cellules d'inventaire via indices packés en base 10/100.
struct ItemBatchUpdate {
    uint32_t header;      // payload+0  (header%1000 = sous-code ; header/1000 = nb d'objets, 0..8)
    uint32_t rowPacked;   // payload+4  (indices de LIGNE inventaire packés base-10, 1 chiffre/objet)
    uint32_t colPackedLo; // payload+8  (indices de COLONNE base-100 pour objets 0..3)
    uint32_t colPackedHi; // payload+12 (indices de COLONNE base-100 pour objets 4..7)
    uint32_t posPackedLo; // payload+16 (positions grille base-100 pour objets 0..3)
    uint32_t posPackedHi; // payload+20 (positions grille base-100 pour objets 4..7)
    uint32_t itemIds[8];  // payload+24 (id d'objet par cellule)
    static ItemBatchUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnBossHpBarUpdate (opcode 0x9d) — met à jour la barre de vie du boss (pourcentage = valeur/2).
struct BossHpBarUpdate {
    uint32_t hpRaw; // payload+0 (stocké /2 -> dword_1675E9C, en pourcentage)
    static BossHpBarUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemUpgradeResult (opcode 0xa8) — résultat d'amélioration d'objet : ré-applique la cellule + coût 100 or.
struct ItemUpgradeResult {
    uint32_t status;         // payload+0 (-1 / 0 / 1 : variantes de succès, message différent)
    uint32_t itemId;         // payload+4  -> g_InvMain
    uint32_t gridX;          // payload+8  -> g_InvGrid_GridX
    uint32_t gridY;          // payload+12 -> g_InvGrid_GridY
    uint32_t count;          // payload+16 -> g_InvGrid_Count
    uint32_t durability;     // payload+20 -> g_InvGrid_Durability
    uint32_t instanceSerial; // payload+24 -> g_InvGrid_InstanceSerial
    static ItemUpgradeResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnRankBoardLoad (opcode 0xb2) — charge le tableau de classement (en-tête + corps 600 o).
struct RankBoardLoad {
    uint32_t header;    // payload+0  -> dword_18260C8 (nb total ; pages = header/10)
    uint8_t  body[600]; // payload+4  -> dword_1825E70 (corps du classement)
    static RankBoardLoad Parse(const uint8_t* payload, size_t len);
};

// Pkt_ZoneChangeInfo (opcode 0x0d) — ea=0x464500 — blob d'infos de changement de zone.
// Deux blocs contigus dans le payload (recv buffer contigu : unk_815BED == payload+1324).
struct ZoneChangeInfo {
    uint8_t block1[1324];  // payload+0    (recopié dans dword_1685E08)
    uint8_t block2[2456];  // payload+1324 (recopié dans byte_1686334)
    static ZoneChangeInfo Parse(const uint8_t* payload, size_t len);
};

// Pkt_SpawnZoneObject (opcode 0x86 / 134) — ea=0x4680F0 — création/màj/suppression d'objet de zone (portail/porte).
struct SpawnZoneObject {
    uint32_t idHi;      // payload+0   (clé objet, poids fort)
    uint32_t idLo;      // payload+4   (clé objet, poids faible)
    uint8_t  body[52];  // payload+8   (corps de l'objet, recopié tel quel)
    uint32_t action;    // payload+60  (2 = créer/màj, 3 = supprimer)
    static SpawnZoneObject Parse(const uint8_t* payload, size_t len);
};

// Pkt_WarehouseOpen (opcode 0x22 / 34) — ea=0x48CB00 — ouverture/résultat entrepôt (blob 1232 o).
struct WarehouseOpen {
    uint32_t status;      // payload+0 (0 = ouvrir, 100..105 = messages de résultat)
    uint8_t  blob[1232];  // payload+4 (grille entrepôt, recopiée dans dword_18229CC)
    static WarehouseOpen Parse(const uint8_t* payload, size_t len);
};

// Pkt_PlayerShopGoldResult (opcode 0x89 / 137) — ea=0x48E660 — résultat or/règlement boutique joueur.
struct PlayerShopGoldResult {
    uint32_t status;       // payload+0 (0 = succès, 1..4 = erreurs)
    uint32_t weightDelta;  // payload+4 (g_InvWeight += weightDelta)
    uint32_t goldDelta;    // payload+8 (dword_1675620 += goldDelta)
    static PlayerShopGoldResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_RepairResult (opcode 0x2d / 45) — ea=0x48F7B0 — résultat réparation / changement d'or.
struct RepairResult {
    uint32_t status;        // payload+0 (0..7)
    uint32_t goldRemaining; // payload+4  (nouvel or ; dword_1823B4C = goldRemaining)
    uint32_t invRow;        // payload+8  (rangée/sac d'inventaire)
    uint32_t invCol;        // payload+12 (colonne d'inventaire)
    uint32_t itemCell[6];   // payload+16 (24 o : id, gridX, gridY, count, durability, serial)
    static RepairResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_AllyInvitePrompt (opcode 0x34 / 52) — ea=0x48FFB0 — invitation guilde/allié.
struct AllyInvitePrompt {
    char     name[13];  // payload+0  (nom de l'invitant, chaîne fixe 13 o)
    uint32_t inviterId; // payload+13 (id de l'invitant ; dword_1822838 = inviterId)
    static AllyInvitePrompt Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptOpen_Dlg19 (opcode 0x3b / 59, taille 14) — ea=0x4906F0 — ouvre le dialogue de confirmation 19.
struct ConfirmPromptOpenDlg19 {
    char name[13];  // payload+0 (nom affiché dans le prompt, chaîne fixe 13 o)
    static ConfirmPromptOpenDlg19 Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptClose_Dlg20 (opcode 0x42 / 66) — ea=0x490BB0 — ferme le dialogue 20 s'il est actif.
// AUCUN champ lu du payload (le handler n'accède qu'à des globals).
struct ConfirmPromptCloseDlg20 {
    static ConfirmPromptCloseDlg20 Parse(const uint8_t* payload, size_t len);
};

// Net_OnResultDialog340 (opcode 0x49 / 73) — ea=0x491090 — messages de résultat 6 cas (strings 340..345).
struct ResultDialog340 {
    uint32_t status;  // payload+0 (0..5 -> string 340..345)
    static ResultDialog340 Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptOpen_Dlg14 (opcode 0x50 / 80, taille 14) — ea=0x491C10 — ouvre le dialogue de confirmation 14.
struct ConfirmPromptOpenDlg14 {
    char name[13];  // payload+0 (nom affiché, chaîne fixe 13 o)
    static ConfirmPromptOpenDlg14 Parse(const uint8_t* payload, size_t len);
};

// Net_OnSelfFactionChat (opcode 0x57 / 87) — ea=0x4930D0 — poste un chat de faction pour un nom donné.
struct SelfFactionChat {
    char name[13];  // payload+0 (nom de l'émetteur, chaîne fixe 13 o)
    static SelfFactionChat Parse(const uint8_t* payload, size_t len);
};

// Net_OnBossHpInit (opcode 0x5f / 95, taille 21) — ea=0x4A51D0 — initialise la barre de vie de boss #1 (dword_1675BB4).
struct BossHpInit {
    uint32_t a;   // payload+0  (-> dword_1675BBC)
    uint32_t b;   // payload+4  (-> dword_1675BC0 ; valeur retournée)
    uint32_t c;   // payload+8  (-> dword_1675BC4)
    uint32_t d;   // payload+12 (-> dword_1675BC8)
    uint32_t hp;  // payload+16 (PV max ; dword_1675BB4 = hp/2)
    static BossHpInit Parse(const uint8_t* payload, size_t len);
};

// Net_OnBossHpInit2 (opcode 0x67 / 103) — ea=0x4A5C20 — initialise la barre de vie de boss #2 (dword_1675CB8).
// Layout identique à BossHpInit.
struct BossHpInit2 {
    uint32_t a;   // payload+0  (-> dword_1675CC0)
    uint32_t b;   // payload+4  (-> dword_1675CC4 ; valeur retournée)
    uint32_t c;   // payload+8  (-> dword_1675CC8)
    uint32_t d;   // payload+12 (-> dword_1675CCC)
    uint32_t hp;  // payload+16 (PV max ; dword_1675CB8 = hp/2)
    static BossHpInit2 Parse(const uint8_t* payload, size_t len);
};

// Net_OnVendorClose (opcode 0x6e / 110, taille 1) — ea=0x4A6830 — ferme la fenêtre marchand.
// AUCUN champ lu du payload (le handler n'accède qu'à des globals).
struct VendorClose {
    static VendorClose Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemEnchantDispatch (opcode 0x75 / 117) — ea=0x4A7410 — résultat d'enchantement par paliers.
// Méga-dispatcher : tier = code%100 (1..5), index message = code/100.
struct ItemEnchantDispatch {
    uint32_t status;  // payload+0  (sous-état par tier : 0 = succès, 1..3 = échecs)
    uint32_t code;    // payload+4  (code%100 = tier 1..5 ; code/100 = décalage StrTable)
    uint32_t aux0;    // payload+8  (-> g_InvAux)
    uint32_t aux1;    // payload+12 (-> dword_1674ABC)
    uint32_t aux2;    // payload+16 (-> dword_1674AC0)
    static ItemEnchantDispatch Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemRefineResult (opcode 0x7c / 124, taille 13) — ea=0x4A97A0 — résultat de raffinage d'objet.
struct ItemRefineResult {
    uint32_t status;      // payload+0 (init -1 ; 0/1/2 = cas de résultat)
    uint32_t goldCost;    // payload+4 (g_Currency -= goldCost)
    uint32_t attribDelta; // payload+8 (delta d'attribut via Bits_AddByte2)
    static ItemRefineResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnSummonSpawn (opcode 0x84 / 132) — ea=0x4AA810 — apparition d'un familier/invocation pour slot<4.
struct SummonSpawn {
    uint32_t status;  // payload+0 (init -1 ; 0 = succès)
    uint32_t slot;    // payload+4 (init -1 ; slot d'invocation, doit être < 4)
    static SummonSpawn Parse(const uint8_t* payload, size_t len);
};

// Net_OnBulkItemConsume (opcode 0x8d / 141) — ea=0x4AB1F0 — consommation en masse : remboursement + vidage de cellules.
// Champs packés en base-10/base-100 (décodés localement, cf. notes).
struct BulkItemConsume {
    uint32_t code;         // payload+0  (code%1000 = statut d'erreur 0..13 ; code/1000 = nb de cellules)
    uint32_t rowPack;      // payload+4  (rangées packées base-10, chiffre i = row de la cellule i)
    uint32_t colPackA;     // payload+8  (colonnes packées base-100, cellules 0..3)
    uint32_t colPackB;     // payload+12 (colonnes packées base-100, cellules 4..7)
    uint32_t gridPackA;    // payload+16 (index grille packé base-100, cellules 0..3)
    uint32_t gridPackB;    // payload+20 (index grille packé base-100, cellules 4..7)
    uint32_t currencyType; // payload+24 (1/2/3 -> dword_16756F8 / dword_167478C / dword_1674790)
    uint32_t unitPrice;    // payload+28 (montant remboursé par cellule : currency -= nb*unitPrice)
    uint32_t itemIds[8];   // payload+32 (32 o : ids d'objets par cellule)
    static BulkItemConsume Parse(const uint8_t* payload, size_t len);
};

// Net_OnDataTableLoad_1686CCC (opcode 0x96 / 150) — ea=0x4AC580 — charge une table UI de 680 o (classement).
struct DataTableLoad1686CCC {
    uint32_t status;      // payload+0 (0 = appliquer la table)
    uint8_t  table[680];  // payload+4 (recopiée dans byte_1686CCC si status==0)
    static DataTableLoad1686CCC Parse(const uint8_t* payload, size_t len);
};

// Net_OnBossPanelLoad (opcode 0x9e / 158, taille 437) — ea=0x4AD2A0 — charge l'en-tête + corps du panneau de boss.
struct BossPanelLoad {
    uint32_t header[4];  // payload+0  (16 o : -> dword_1675EA0/EA4/EA8/EAC)
    uint8_t  body[420];  // payload+16 (-> dword_1675EB0)
    static BossPanelLoad Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemFuseResult (opcode 0xa9 / 169) — ea=0x4AE750 — fusion de deux objets (source F08 + cible F2C).
struct ItemFuseResult {
    uint32_t status;   // payload+0  (0 = maj source seule, 1 = maj source + cible)
    uint32_t subMode;  // payload+4  (variante d'effet : 1..6, 11, 12)
    uint32_t aux0;     // payload+8  (delta byte0 : Bits_AddByte0)
    uint32_t aux1;     // payload+12 (delta byte1 : Bits_AddByte1)
    uint32_t aux2;     // payload+16 (delta byte2 : Bits_SetByte2)
    static ItemFuseResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemDropResult (opcode 0xb3 / 179) — ea=0x4B3440 — résultat de lâcher d'objet, pose une cellule d'inventaire.
struct ItemDropResult {
    uint32_t status;      // payload+0  (0 = succès)
    uint32_t goldOrValue; // payload+4  (dword_1675644 = goldOrValue)
    uint32_t invRow;      // payload+8  (rangée/sac d'inventaire)
    uint32_t invCol;      // payload+12 (colonne d'inventaire)
    uint32_t itemCell[6]; // payload+16 (24 o : id, gridX, gridY, count, durability, serial)
    static ItemDropResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_SystemMessageBox (opcode 0x0e / 14 déc) — boîte de dialogue système avec image serveur.
// Payload : [param:u32][image:char[1000]] = 1004 octets.
struct SystemMessageBox {
    uint32_t param;        // payload+0 : id/type transmis à Billboard_ValidateImageViaTempFile
    char     image[1000];  // payload+4 : chemin/nom du fichier image (chaîne C)
    // TODO(state): appeler Billboard_ValidateImageViaTempFile(param, image) —
    //   valide l'image via un fichier temporaire puis l'affiche dans la boîte de dialogue.
    static SystemMessageBox Parse(const uint8_t* payload, size_t len);
};

// Pkt_ChatNotice (opcode 0x14 / 20 déc) — ligne de notice système (chat).
// Payload : [text:char[61]] = 61 octets.
struct ChatNotice {
    char text[61];  // payload+0 : texte de la notice (chaîne C, 61 octets fixes)
    // ÉTAT : implémenté — Net/GameHandlers_ChatSocial.cpp (0x14) : g_Client.msg.Floating(0,0,text)
    //   + g_Client.msg.System(text).
    static ChatNotice Parse(const uint8_t* payload, size_t len);
};

// Pkt_WarehouseClose (opcode 0x23 / 35 déc) — fermeture UI entrepôt/stockage.
// Payload : [mode:u32] = 4 octets.
struct WarehouseClose {
    uint32_t mode;  // payload+0 : 1 = ferme+refocus editbox, 2 = commit grille+réouvre
    // ÉTAT : implémenté — Net/GameHandlers_VendorTrade.cpp (0x23) : mode==1 pose
    //   dword_1687428=0 et dword_18229B8=1 (si dword_1822998) ; mode==2 appelle
    //   g_Warehouse.CommitAllToInventory() + message str2110. Restent TODO(ui) purs
    //   (UI_FocusEditBox/cGameHud_ResetUiState/UI_StorageWin_Open — pas d'état supplémentaire).
    static WarehouseClose Parse(const uint8_t* payload, size_t len);
};

// Pkt_SmithUpgradeResult (opcode 0x27 / 39 déc) — RÉSULTAT D'INTERACTION DE QUÊTE (mal nommé).
// Payload : [resultCode:u32][invRow:u32][invSlot:u32][gridX:u32][gridY:u32] = 20 octets.
struct QuestInteractResult {
    uint32_t resultCode;  // payload+0 : code 1..9, pilote l'avance d'étape / récompense / échec
    uint32_t invRow;      // payload+4 : ligne/page d'inventaire cible (-1 = aucune) (v34)
    uint32_t invSlot;     // payload+8 : colonne/slot d'inventaire cible (v32)
    uint32_t gridX;       // payload+12 : position X dans la grille (v36)
    uint32_t gridY;       // payload+16 : position Y dans la grille (v33)
    // ÉTAT : implémenté — messages haut-niveau dans Net/GameHandlers_Misc.cpp (0x27) ; écriture
    //   inventaire [invRow][invSlot] + compteurs de quête (QuestProgressState) dans
    //   game::ApplyQuestInteractResultState (Game/QuestSystem.h/.cpp), câblée en override par
    //   Net/GameHandlers_Core.cpp (enregistré en dernier, complète le handler ci-dessus sans le
    //   dupliquer).
    static QuestInteractResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_PartyInvitePrompt (opcode 0x2e / 46 déc) — invitation de groupe (prompt oui/non).
// Payload : [inviterName:char[13]][flag:u32] = 17 octets.
struct PartyInvitePrompt {
    char     inviterName[13];  // payload+0 : nom de l'inviteur (chaîne C, 13 octets fixes)
    uint32_t flag;             // payload+13 : 1 = str305, sinon str426
    // ÉTAT : implémenté — Net/GameHandlers_PartyGuild.cpp (0x2e) : g_Options.FilterPartyInvite
    //   -> g_Client.prompt.Open(8, ...) ; sinon Net_SendOp45(2) + message str304.
    static PartyInvitePrompt Parse(const uint8_t* payload, size_t len);
};

// Pkt_AllyInviteDecline (opcode 0x35 / 53 déc) — invitation guilde/alliance refusée. Aucun payload.
struct AllyInviteDecline {
    // ÉTAT : implémenté — Net/GameHandlers_PartyGuild.cpp (0x35) : g_Client.prompt.CloseIf(9) +
    //   message str327.
    static AllyInviteDecline Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptClose_Dlg19 (opcode 0x3c / 60 déc) — ferme le dialogue de confirmation 19. Aucun payload.
struct ConfirmPromptClose_Dlg19 {
    // ÉTAT : implémenté — Net/GameHandlers_ChatSocial.cpp (0x3c) : g_Client.prompt.CloseIf(19) +
    //   message str501.
    static ConfirmPromptClose_Dlg19 Parse(const uint8_t* payload, size_t len);
};

// Net_OnTradeResultDialog (opcode 0x43 / 67 déc) — messages de résultat d'échange (str 511-518).
// Payload : [resultCode:u32] = 4 octets.
struct TradeResultDialog {
    uint32_t resultCode;  // payload+0 : code 0..7 -> message str511..518
    // ÉTAT : implémenté — Net/GameHandlers_ChatSocial.cpp (0x43) : CloseNoticeIf(9) puis
    //   message str(511+resultCode) ; resultCode==0 -> Net_SendOp62 en plus.
    static TradeResultDialog Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildRosterReset (opcode 0x4a / 74 déc) — réinitialise le roster d'alliance/guilde (5 noms).
// Payload : [mode:u32][name1..name5:char[13] chacun] = 69 octets.
struct GuildRosterReset {
    uint32_t mode;         // payload+0 : 1 -> str349, 2 -> str881
    char     name1[13];    // payload+4
    char     name2[13];    // payload+17
    char     name3[13];    // payload+30
    char     name4[13];    // payload+43
    char     name5[13];    // payload+56
    // ÉTAT : implémenté — Net/GameHandlers_PartyGuild.cpp (0x4a) : g_World.allianceRoster.Reset()
    //   inconditionnel (les 5 noms lus ci-dessus ne sont PAS réutilisés, fidèle à l'original) ;
    //   mode 1 -> str349, mode 2 -> str881.
    static GuildRosterReset Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptClose_Dlg14 (opcode 0x51 / 81 déc) — ferme le dialogue de confirmation 14. Aucun payload.
struct ConfirmPromptClose_Dlg14 {
    // ÉTAT : implémenté — Net/GameHandlers_ChatSocial.cpp (0x51) : g_Client.prompt.CloseIf(14) +
    //   message str410.
    static ConfirmPromptClose_Dlg14 Parse(const uint8_t* payload, size_t len);
};

// Net_OnCultivationDispatch (opcode 0x58 / 88 déc) — MÉGA-DISPATCHER cultivation/attributs (20 sous-op).
// Payload : [value:u32][subOpcode:u32][body:u8[100]] = 108 octets.
struct CultivationDispatch {
    uint32_t value;      // payload+0 : valeur/code résultat (v72), sémantique dépend du sous-op
    uint32_t subOpcode;  // payload+4 : sélecteur du sous-op (v67), switch 1..20
    uint8_t  body[100];  // payload+8 : corps brut, interprété comme u32[] selon le sous-op
    // ÉTAT : PARTIELLEMENT implémenté — Net/GameHandlers_Misc.cpp (0x58) couvre les cas
    //   1 (message str601, PAS encore le remaniement exact de g_SelfBaseAttr292/296/300/304),
    //   6 (g_GrowthIndex), 7 (coût 100 argent + 1M poids), 12/13 (toggle dword_16747D4/D8),
    //   19/20 (11 u32 de buffs d'attributs depuis body[0..43] + coût 1000 pour 20). Le détail fin
    //   par sous-op (attribution exacte des points, recalc AR min/max) reste TODO(state) dans ce
    //   handler — struct minimale ~4.2 Ko non entièrement décompilée à ce stade (20 sous-cas).
    static CultivationDispatch Parse(const uint8_t* payload, size_t len);
};

// Net_OnZoneBuffStatus (opcode 0x60 / 96 déc) — état on/off des buffs de zone (par faction).
// Payload : [flags:u32[4]] = 16 octets.
struct ZoneBuffStatus {
    uint32_t flags[4];  // payload+0 : 4 drapeaux on(1)/off, indexés par faction (str 75..78)
    // ÉTAT : implémenté — Net/GameHandlers_BossWorld.cpp (0x60) : ligne agrégée str75..78+ON/OFF
    //   (flags[3] inconditionnel — la garde g_SelfMorphNpcId>153 n'est PAS reproduite, cf. commentaire
    //   du handler) ; si flags[g_LocalElement]==0 -> BeginWarpToFactionTown (Game/MapWarp.h).
    static ZoneBuffStatus Parse(const uint8_t* payload, size_t len);
};

// Net_OnBossHpPercent (opcode 0x68 / 104 déc) — pourcentage de PV de boss.
// Payload : [hp:u32] = 4 octets.
struct BossHpPercent {
    uint32_t hp;  // payload+0 : valeur brute ; le pourcentage affiché est hp/2
    // ÉTAT : implémenté — Net/GameHandlers_BossWorld.cpp (0x68) : message "[hp/2]str843".
    static BossHpPercent Parse(const uint8_t* payload, size_t len);
};

// Net_OnSkillCooldownSet (opcode 0x6f / 111 déc) — fixe le cooldown d'une compétence.
// Payload : [skillId:u32][value:u32] = 8 octets.
struct SkillCooldownSet {
    uint32_t skillId;  // payload+0 : index compétence (valide si 1..351)
    uint32_t value;    // payload+4 : valeur de cooldown à stocker
    // ÉTAT : implémenté — Net/GameHandlers_Misc.cpp (0x6f) : dword_18217D0[skillId]=value si
    //   1<=skillId<=351.
    static SkillCooldownSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnInventoryBulkLoad (opcode 0x77 / 119 déc) — chargement en masse d'objets (coords compactées).
// Payload : [header:u32][rowPacked:u32][colPackedA:u32][colPackedB:u32]
//           [posPackedA:u32][posPackedB:u32][itemIds:u32[8]][durPacked:u32] = 60 octets.
struct InventoryBulkLoad {
    uint32_t header;      // payload+0 : /1000 = code message, %1000 = nombre d'items (<=8)
    uint32_t rowPacked;   // payload+4 : lignes compactées base-10 (digit i = rowPacked%(10*10^i)/10^i)
    uint32_t colPackedA;  // payload+8 : colonnes compactées base-100 pour i<4 (v11)
    uint32_t colPackedB;  // payload+12 : colonnes compactées base-100 pour i>=4 (v23)
    uint32_t posPackedA;  // payload+16 : positions grille base-100 pour i<4 (v21)
    uint32_t posPackedB;  // payload+20 : positions grille base-100 pour i>=4 (v17)
    uint32_t itemIds[8];  // payload+24 : 8 IDs d'objet (v18)
    uint32_t durPacked;   // payload+56 : durabilité/flags compactés (v24)
    // ÉTAT : implémenté — Net/GameHandlers_InvDispatch.cpp (0x77) : décode ligne/colonne/position
    //   (base-10/100) et écrit g_InvMain/g_InvGrid_* pour i<header%1000 (<=8), durabilité via
    //   Bits_PackByte012(durPacked) (voie par défaut). TODO(item)/TODO(msg) restants (hors
    //   TODO(state)/TODO(send)) : garde MobDb_GetEntry(itemIds[i]) et message str1849/1788/2999
    //   selon header/1000 — nécessitent ItemDefTbl, absent du modèle client (cf. justification
    //   identique dans GameHandlers_BossWorld.cpp ## MountTicketPrompt).
    static InventoryBulkLoad Parse(const uint8_t* payload, size_t len);
};

// Net_OnSkillAuraSync (opcode 0x7d / 125 déc) — synchro états toggle compétence/aura (mini-dispatcher).
// Payload : [subOpcode:u32][value:u32] = 8 octets.
struct SkillAuraSync {
    uint32_t subOpcode;  // payload+0 : sélecteur (0,1,2,5,6,7,8,9)
    int32_t  value;      // payload+4 : valeur (signée ; base-10 compactée en cas 0)
    // ÉTAT : implémenté — Net/GameHandlers_Misc.cpp (0x7d) : décode base-10 (cas 0, mais SANS le
    //   chargement/déchargement du modèle de zone — TODO(state) restant, asset .IMG hors périmètre
    //   réseau), toggle indexé (cas 2), messages (5/6), warp de faction (7/9/8 via
    //   BeginWarpToFactionTown, Game/MapWarp.h).
    static SkillAuraSync Parse(const uint8_t* payload, size_t len);
};

// Net_OnSystemNotice (opcode 0x85 / 133 déc) — messages de notice système.
// Payload : [subOpcode:u32][value:u32] = 8 octets.
struct SystemNotice {
    uint32_t subOpcode;  // payload+0 : 0,1,2
    uint32_t value;      // payload+4 : valeur affichée (cas 0/1) ou sous-sélecteur (cas 2 : 0..7)
    // ÉTAT : implémenté — Net/GameHandlers_Misc.cpp (0x85) : les 3 cas (0/1 -> "[value]str" ;
    //   2 -> message flottant + ligne système selon value 0..7 -> str1996..2003).
    static SystemNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyMemberPosition (opcode 0x91 / 145 déc) — position monde d'un membre de groupe.
// Payload : [idHi:u32][idLo:u32][gridX:u32][gridY:u32][pos:f32[3]] = 28 octets.
struct PartyMemberPosition {
    uint32_t idHi;    // payload+0 : identité réseau haute (comparée à dword_1687238[227*i])
    uint32_t idLo;    // payload+4 : identité réseau basse (comparée à dword_168723C[227*i])
    uint32_t gridX;   // payload+8 : cellule grille X (-> dword_1687478)
    uint32_t gridY;   // payload+12 : cellule grille Y (-> dword_168747C)
    float    pos[3];  // payload+16 : position monde X,Y,Z (-> unk_1687480/84/88)
    // ÉTAT : implémenté — Game/EntityManager.cpp::EntityManager::OnPartyMemberPosition, câblée
    //   sur l'opcode 0x91 par Net/GameHandlers_Entity.cpp. Retrouve l'entité via FindPlayer(idHi,
    //   idLo) et écrit gridX/gridY/pos dans PlayerEntity::body (+ miroir x/y/z).
    static PartyMemberPosition Parse(const uint8_t* payload, size_t len);
};

// Net_OnMultiItemRemove (opcode 0x97 / 151 déc) — retrait de plusieurs cellules d'inventaire (coords compactées).
// Payload : [resultCode:u32][rowPackedA:u32][rowPackedB:u32][colPackedA:u32][colPackedB:u32][count:u32] = 24 octets.
struct MultiItemRemove {
    uint32_t resultCode;  // payload+0 : 0 = retrait, 1 = erreur(str2246), 2 = erreur(str2247)
    uint32_t rowPackedA;  // payload+4 : 4 indices de ligne compactés base-100 (v22)
    uint32_t rowPackedB;  // payload+8 : 5e indice de ligne (v23 % 100)
    uint32_t colPackedA;  // payload+12 : 4 indices de colonne compactés base-100 (v8)
    uint32_t colPackedB;  // payload+16 : 5e indice de colonne (v9 % 100)
    uint32_t count;       // payload+20 : nombre de cellules à effacer - 1 (boucle i<count+1)
    // ÉTAT : implémenté — Net/GameHandlers_InvDispatch.cpp (0x97) : décode jusqu'à 5 (row,col)
    //   base-100 et efface g_InvMain/g_InvGrid_* pour k in [0,count] (message str2259) ;
    //   resultCode 1/2 -> messages str2246/str2247.
    static MultiItemRemove Parse(const uint8_t* payload, size_t len);
};

// Net_OnNpcDialogEvent (opcode 0x9f / 159 déc) — messages de résultat de dialogue PNJ.
// Payload : [subOpcode:u32][nameStringId:u32] = 8 octets.
struct NpcDialogEvent {
    uint32_t subOpcode;     // payload+0 : 0..5, sélectionne str2340..2346
    uint32_t nameStringId;  // payload+4 : index dans StrTable003 (nom PNJ/objet)
    // ÉTAT : implémenté — Net/GameHandlers_ChatSocial.cpp (0x9f) : "<nom> <str2340..2346 selon
    //   subOpcode>" en flottant + ligne système ; 2e ligne str2341 pour subOpcode 0/2.
    static NpcDialogEvent Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemSocketDispatch (opcode 0xab / 171 déc) — MÉGA-DISPATCHER sertissage/gemmes (5 cas).
// Payload : [resultCode:u32][actionType:u32][cost:u32][itemSnapshot:u32[6]] = 36 octets.
struct ItemSocketDispatch {
    uint32_t resultCode;       // payload+0 : sélecteur principal (switch 0..4)
    uint32_t actionType;       // payload+4 : sous-type d'action (v25[0]) modulant messages & logique
    uint32_t cost;             // payload+8 : coût en argent (retranché de g_Currency)
    uint32_t itemSnapshot[6];  // payload+12 : snapshot item (id, gridX, gridY, count, durability, serial) (v23)
    // ÉTAT : implémenté — Net/GameHandlers_InvDispatch.cpp (0xab) : g_Currency-=cost ; switch
    //   resultCode 0 (écrit itemSnapshot dans le slot pending-move), 1 (déplace depuis pending),
    //   2/3/4 (efface 1/2/3 cellules). Restent TODO(state) : garde MobDb_GetEntry(dword_1822F08)
    //   (ItemDefTbl absent, cf. justification dans GameHandlers_BossWorld.cpp), recalcul des noeuds
    //   de skill (Skill_UnpackTreeNodes, actionType in {1,6,7} — opérandes source non confirmés pour
    //   ce site d'appel) et sélection exacte du message par actionType (str222/3390/2710).
    static ItemSocketDispatch Parse(const uint8_t* payload, size_t len);
};

// Net_OnStatSyncDispatch (opcode 0xb4 / 180 déc) — sync argent/poids + résultat d'action inventaire (4 cas).
// Payload : [resultCode:u32][invWeight:u32][currency:u32][counter:u32][durability:u32] = 20 octets.
struct StatSyncDispatch {
    uint32_t resultCode;  // payload+0 : sélecteur (switch 0..3)
    uint32_t invWeight;   // payload+4 : nouveau poids d'inventaire -> g_InvWeight
    uint32_t currency;    // payload+8 : nouvel argent -> g_Currency et dword_1687254[0]
    uint32_t counter;     // payload+12 : compteur -> dword_16746E8
    uint32_t durability;  // payload+16 : durabilité -> dword_1822F18 (item pending-move)
    // ÉTAT : implémenté — Net/GameHandlers_InvDispatch.cpp (0xb4) : applique inconditionnellement
    //   g_InvWeight/g_Currency/dword_16746E8, puis switch resultCode 0/1/2 (écrit l'item pending-move
    //   avec durability, vide l'ancien slot, message str2748/2749/654) et 3 (vide les deux slots,
    //   str224). Garde MobDb_GetEntry(dword_1822F08) et son (type item 6..21) non reproduits
    //   (ItemDefTbl/audio hors périmètre réseau).
    static StatSyncDispatch Parse(const uint8_t* payload, size_t len);
};

// Pkt_SpawnCharacter (opcode 0x0f) — creation/mise a jour d'un enregistrement personnage (record 908 o).
// Identifie l'entite par la paire (idHi,idLo) puis copie 600 o de corps dans le slot.
struct SpawnCharacter {
    uint32_t idHi;      // payload+0   moitie haute de l'ID reseau (dword_1687238)
    uint32_t idLo;      // payload+4   moitie basse de l'ID reseau (dword_168723C)
    uint8_t  body[600]; // payload+8   corps perso, copie tel quel dans dword_168724C
                        //   body+48  : nom du personnage (char[], NUL-termine, <=16 o) --
                        //   confirme via Char_DrawNameplate 0x56EF40 (this+72 == body+48),
                        //   extrait par EntityManager::ReadPlayerName (cf. Game/GameState.h::PlayerEntity::name)
                        //   body+216 : bloc move-state 72 o (g_SelfMoveStateBlock)
                        //   body+220 : action-state ; body+272 : id anim/stun (buff visuel)
    uint32_t mode;      // payload+608 1=full update, 2=deplacement, 3=spawn local
    static SpawnCharacter Parse(const uint8_t* payload, size_t len);
};

// Pkt_OnCombatResult (opcode 0x15) — resultat de combat, bloc opaque de 76 o.
struct OnCombatResult {
    uint8_t block[76]; // payload+0  bloc resultat combat (structure interne non decodee ici)
    static OnCombatResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_ItemCombineResult (opcode 0x1d) — resultat de combinaison/craft d'objet (size table = 33).
struct ItemCombineResult {
    uint32_t resultCode;   // payload+0   0/1/10 -> succes/echec/variante
    uint32_t weightDelta;  // payload+4   poids retire de l'inventaire (g_InvWeight -= v)
    uint32_t itemId;       // payload+8   id objet resultant
    uint32_t gridX;        // payload+12
    uint32_t gridY;        // payload+16
    uint32_t count;        // payload+20  quantite
    uint32_t durability;   // payload+24
    uint32_t serial;       // payload+28  serial d'instance
    static ItemCombineResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_WarehouseUpdate (opcode 0x24) — mise a jour du coffre/entrepot.
struct WarehouseUpdate {
    uint32_t mode;        // payload+0    0/3=data, 1/2=messages resultat
    uint8_t  data[1232];  // payload+4    bloc entrepot (copie dans dword_18229CC si mode 0/3)
    static WarehouseUpdate Parse(const uint8_t* payload, size_t len);
};

// Pkt_ToggleObserver (opcode 0x28) — bascule mode observateur/faction 3.
struct ToggleObserver {
    uint32_t resultCode;  // payload+0   0=bascule OK, 1=refus
    static ToggleObserver Parse(const uint8_t* payload, size_t len);
};

// Pkt_PartyInviteDecline (opcode 0x2f) — invitation de groupe refusee (size table = 1, aucun champ).
struct PartyInviteDecline {
    // Aucun champ : le paquet ne lit rien du payload (opcode seul).
    static PartyInviteDecline Parse(const uint8_t* payload, size_t len);
};

// Pkt_AllyJoinResult (opcode 0x36) — code resultat d'adhesion guilde/alliance (0..6).
struct AllyJoinResult {
    uint32_t resultCode;  // payload+0   0=succes, 1..6=erreurs
    static AllyJoinResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyResultDialog (opcode 0x3d) — code resultat d'action de groupe (0..5).
struct PartyResultDialog {
    uint32_t resultCode;  // payload+0   0=succes, 1..5=erreurs
    static PartyResultDialog Parse(const uint8_t* payload, size_t len);
};

// Net_OnRequestTargetNameSet (opcode 0x44) — memorise le nom de la cible d'une requete (size table = 18).
struct RequestTargetNameSet {
    uint32_t subop;    // payload+0   1 ou 2 (type de requete)
    char     name[13]; // payload+4   nom cible (13 o, termine NUL)
    static RequestTargetNameSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildMemberJoin (opcode 0x4b) — ajout d'un membre a la premiere case libre du roster alliance.
struct GuildMemberJoin {
    char name[13];  // payload+0   nom du nouveau membre (13 o, termine NUL)
    static GuildMemberJoin Parse(const uint8_t* payload, size_t len);
};

// Net_OnResultDialog399 (opcode 0x52) — code resultat generique (0..5, strings 399-404).
struct ResultDialog399 {
    uint32_t resultCode;  // payload+0   0=succes, 1..5=erreurs
    static ResultDialog399 Parse(const uint8_t* payload, size_t len);
};

// Net_OnWhisperMessage (opcode 0x59) — message chuchote (whisper), size table = 79.
struct WhisperMessage {
    uint32_t subop;      // payload+0    1=recu, 2=envoye (echo)
    char     sender[13]; // payload+4    nom de l'interlocuteur (13 o)
    char     msg[61];    // payload+17   texte du message (61 o)
    static WhisperMessage Parse(const uint8_t* payload, size_t len);
};

// Net_OnServerNameNotice (opcode 0x61) — sous-op 1 = message par id, sous-op 2 = 3 floats de position.
struct ServerNameNotice {
    uint32_t subop;     // payload+0   1=message table, 2=coordonnees
    uint8_t  data[100]; // payload+4   sous-op1: data[0..3]=id string(StrTable003) ; sous-op2: data[0..11]=3 floats
    static ServerNameNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemCellSet (opcode 0x69) — place un objet (6 dwords) dans une case de la grille d'inventaire.
struct ItemCellSet {
    uint32_t resultCode; // payload+0   0=succes (place l'objet), autre=ignore
    uint32_t itemId;     // payload+4
    uint32_t gridX;      // payload+8
    uint32_t gridY;      // payload+12
    uint32_t count;      // payload+16
    uint32_t durability;  // payload+20
    uint32_t serial;     // payload+24  serial d'instance
    static ItemCellSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemCombineResult (opcode 0x70) — resultat combine/sertissage, met a jour 1 ou 2 cases de grille.
struct ItemCombineResult2 {
    uint32_t resultCode;  // payload+0   0/1/2 -> variantes de mise a jour
    uint32_t weightDelta; // payload+4   poids retire (g_InvWeight -= v)
    uint32_t itemId;      // payload+8
    uint32_t gridX;       // payload+12
    uint32_t gridY;       // payload+16
    uint32_t count;       // payload+20
    uint32_t durability;  // payload+24
    uint32_t serial;      // payload+28  serial d'instance
    static ItemCombineResult2 Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemPlaceResult (opcode 0x7a) — resultat de pose d'objet dans une case (6 u32).
struct ItemPlaceResult {
    uint32_t resultCode; // payload+0   0/1=place, 2=erreur
    uint32_t itemId;     // payload+4
    uint32_t bagRow;     // payload+8   sac/onglet (index ligne, *384)
    uint32_t slotCol;    // payload+12  colonne (index, *6)
    uint32_t cellIndex;  // payload+16  gridX = cellIndex%8, gridY = cellIndex/8
    uint32_t durability; // payload+20
    static ItemPlaceResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnFriendStatusNotice (opcode 0x7e) — notification ami en ligne/hors ligne.
struct FriendStatusNotice {
    uint32_t subop;    // payload+0    1=en ligne, 2=hors ligne
    char     name[13]; // payload+4    nom de l'ami (13 o)
    uint32_t classId;  // payload+17   id classe/faction (utilise comme StrTable005_Get(classId+75))
    static FriendStatusNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemCountNotice (opcode 0x8c) — notification de quantite d'objet (str 2074/1351).
struct ItemCountNotice {
    uint32_t subop;  // payload+0   0 ou 1 (choix du libelle)
    uint32_t count;  // payload+4   quantite affichee
    static ItemCountNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemMoveResult (opcode 0x92) — resultat de deplacement d'objet, ecrit une case (str 223/117/119).
struct ItemMoveResult {
    uint32_t resultCode; // payload+0   0=succes (ecrit la case), 1/2=erreurs
    uint32_t itemId;     // payload+4
    uint32_t bagRow;     // payload+8   sac/onglet (*384)
    uint32_t slotCol;    // payload+12  colonne (*6)
    uint32_t cellIndex;  // payload+16  gridX = cellIndex%8, gridY = cellIndex/8
    static ItemMoveResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnAchievementDataLoad (opcode 0x98) — chargement des flags d'accomplissements (96 o).
struct AchievementDataLoad {
    uint8_t flags[96]; // payload+0   bloc de flags, copie dans dword_184C218
    static AchievementDataLoad Parse(const uint8_t* payload, size_t len);
};

// Net_OnInstanceEnter (opcode 0xa3) — entree/resultat instance ou evenement (6 u32).
struct InstanceEnter {
    uint32_t subop; // payload+0    1=entree, 2=resultat
    uint32_t code;  // payload+4    sous-code (0=ok, 1..3 selon subop)
    uint32_t p0;    // payload+8    -> dword_1675790
    uint32_t p1;    // payload+12   -> dword_1675794
    uint32_t p2;    // payload+16   -> dword_1675798
    uint32_t p3;    // payload+20   -> dword_167579C
    static InstanceEnter Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemRefineDispatch (opcode 0xac) — MEGA-DISPATCHER resultat de raffinage/amelioration d'objet.
// Enorme switch sur `op` (nombreux sous-cas) ; seul l'en-tete de 4 u32 est lu du payload.
struct ItemRefineDispatch {
    uint32_t op; // payload+0   sous-opcode / code resultat pilotant le switch
    uint32_t a;  // payload+4   parametre 1 (contexte selon op)
    uint32_t b;  // payload+8   parametre 2
    uint32_t c;  // payload+12  parametre 3
    static ItemRefineDispatch Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemCellReset (opcode 0xb6) — vide une case de grille et memorise des coords (size table = 25).
struct ItemCellReset {
    uint32_t flag;      // payload+0    indicateur/etat (lu sur 4 o, seul le contexte importe)
    uint32_t bagRow;    // payload+4    sac/onglet (*384)
    uint32_t slotCol;   // payload+8    colonne (*6)
    uint32_t coordA;    // payload+12   -> dword_1675118
    uint32_t coordB;    // payload+16   -> dword_167511C
    uint32_t coordC;    // payload+20   -> dword_1675120
    static ItemCellReset Parse(const uint8_t* payload, size_t len);
};

// Pkt_CharStateUpdate (opcode 0x10 / 16) — maj des bitfields d'etat d'un personnage.
// Payload lu : idHi(+0) idLo(+4) stateValues[72](+8, 288o) stateFlags[36](+296, 144o). Total 440o.
struct CharStateUpdate {
    uint32_t entityIdHi;        // payload+0  (compare a dword_1687238[227*i])
    uint32_t entityIdLo;        // payload+4  (compare a dword_168723C[227*i])
    uint32_t stateValues[72];   // payload+8  : 36 paires [valeur, extra], indexees v3[2*i]/v3[2*i+1]
    uint32_t stateFlags[36];    // payload+296: 1 = poser l'etat i, 2 = effacer l'etat i
    static CharStateUpdate Parse(const uint8_t* payload, size_t len);
};

// Pkt_SetGameVar (opcode 0x16 / 22) — mega-dispatcher 'set variable de jeu' (158 cas).
// Payload lu : varId(+0) value(+4). Total 8o.
struct SetGameVar {
    uint32_t varId;   // payload+0 : selecteur du switch (1..158)
    int32_t  value;   // payload+4 : valeur a appliquer
    static SetGameVar Parse(const uint8_t* payload, size_t len);
};

// Pkt_ItemSwapResultA (opcode 0x1e / 30) — resultat deplacement/echange d'objet.
// Payload lu : resultCode(+0) weightDelta(+4) itemCell[6](+8, 24o). Total 32o.
struct ItemSwapResultA {
    uint32_t resultCode;   // payload+0 : 0 = OK simple, 1 = OK avec objet, 2 = OK echange
    int32_t  weightDelta;  // payload+4 : soustrait de g_InvWeight
    uint32_t itemCell[6];  // payload+8 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemSwapResultA Parse(const uint8_t* payload, size_t len);
};

// Pkt_VendorItemEntry (opcode 0x25 / 37) — une entree de liste marchand/recherche.
// Payload lu (offsets non alignes a cause du name[13]) : itemId(+0) name[13](+4) f17(+17) f21(+21) blob[36](+25) price0(+61) price1(+65) price2(+69) listId(+73). Total 77o.
struct VendorItemEntry {
    uint32_t itemId;    // payload+0  -> dword_182613C[idx]
    char     name[13];  // payload+4  (nom vendeur/objet)
    uint32_t field17;   // payload+17 -> dword_182A3A4[idx]
    uint32_t field21;   // payload+21 -> dword_182B344[idx]
    uint8_t  blob[36];  // payload+25 -> unk_182C2E4 + 36*idx
    uint32_t price0;    // payload+61 -> dword_1834F84[3*idx]
    uint32_t price1;    // payload+65 -> dword_1834F88[3*idx]
    uint32_t price2;    // payload+69 -> dword_1834F8C[3*idx]
    uint32_t listId;    // payload+73 -> id de session/liste (dword_1826138)
    static VendorItemEntry Parse(const uint8_t* payload, size_t len);
};

// Pkt_WhisperReceive (opcode 0x29 / 41, taille table 75) — ligne de chuchotement/MP recue.
// Payload lu : senderName[13](+0) message[61](+13). Total 74o.
struct WhisperReceive {
    char senderName[13];   // payload+0
    char message[61];      // payload+13
    static WhisperReceive Parse(const uint8_t* payload, size_t len);
};

// Pkt_PartyJoinResult (opcode 0x30 / 48) — code resultat d'adhesion a un groupe.
// Payload lu : resultCode(+0). Total 4o.
struct PartyJoinResult {
    uint32_t resultCode;   // payload+0 : 0..5
    static PartyJoinResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_GuildMemberInfo (opcode 0x37 / 55) — bloc d'infos membre/liste de guilde.
// Payload lu : field0(+0) blockA[128](+4) blockB[96](+132) field228(+228). Total 232o.
struct GuildMemberInfo {
    uint32_t field0;       // payload+0   (1er arg de UI_ItemListWin_Open : count/type)
    uint8_t  blockA[128];  // payload+4   (bloc principal)
    uint8_t  blockB[96];   // payload+132 (bloc secondaire)
    uint32_t field228;     // payload+228 (4e arg)
    static GuildMemberInfo Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyMemberNameSet (opcode 0x3e / 62, taille table 18) — pose le nom d'un membre dans un slot du roster.
// Payload lu : slotIndex(+0) name[13](+4). Total 17o.
struct PartyMemberNameSet {
    uint32_t slotIndex;   // payload+0 : index du slot roster
    char     name[13];    // payload+4 : nom du membre
    static PartyMemberNameSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnRequestCancelClear (opcode 0x45 / 69) — annulation d'une requete : efface les noms cibles.
// Aucun champ lu depuis le payload (payload vide / ignore).
struct RequestCancelClear {
    // aucun champ : le handler ne lit rien du payload
    static RequestCancelClear Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildChatMessage (opcode 0x4c / 76) — message de chat de guilde.
// Payload lu : senderName[13](+0) message[61](+13). Total 74o.
struct GuildChatMessage {
    char senderName[13];   // payload+0
    char message[61];      // payload+13
    static GuildChatMessage Parse(const uint8_t* payload, size_t len);
};

// Net_OnTeamFormationDispatch (opcode 0x53 / 83, taille table 1397) — mega-dispatcher guilde (17 sous-opcodes).
// Payload lu : statusCode(+0) subOpcode(+4) guildBlob[1388](+8). Total 1396o.
struct TeamFormationDispatch {
    uint32_t statusCode;      // payload+0 : code resultat/statut (v88 ; 0 = succes, >0 = erreur)
    uint32_t subOpcode;       // payload+4 : selecteur du sous-opcode (v86 : 1..17)
    uint8_t  guildBlob[1388]; // payload+8 : bloc roster/guilde (copie vers unk_1839970 selon le cas)
    static TeamFormationDispatch Parse(const uint8_t* payload, size_t len);
};

// Net_OnTradeChatMessage (opcode 0x5a / 90) — message de chat commerce/trade.
// Payload lu : senderName[13](+0) message[61](+13). Total 74o.
struct TradeChatMessage {
    char senderName[13];   // payload+0
    char message[61];      // payload+13
    static TradeChatMessage Parse(const uint8_t* payload, size_t len);
};

// Net_OnScriptTrigger (opcode 0x63 / 99) — PAQUET VARIABLE ([op][len:u32][data]). Declencheur script/quest verifie par l'anticheat.
// AUDIT reseau (2026-07-14) : contrairement a la convention generique "payload =
// buffer+1" du reste de ce fichier, le SEUL opcode variable est deja depouille de
// son champ len:u32 par le dispatcher AVANT l'appel du handler (cf. Net/PacketDispatch.cpp
// ::Drain() cas kVariableOpcode, et Net/Framing.cpp::PacketReader::TryParse, memes offsets) :
// `payload` pointe DEJA sur les octets de data (buffer+5), et `len` (parametre de Parse)
// EST deja le compte d'octets de data (le champ len:u32 du fil, deja lu et retire par le
// dispatcher). Se re-fier a `payload+0` comme un 2e champ de longueur (ancien bug : relisait
// les 4 premiers octets de DATA comme une longueur, decalait `data` de 4 o de trop, et
// lancait ts2::asset::AssetError -- exception NON rattrapee nulle part dans la chaine
// OnPacket<T>/PacketDispatcher::Drain()/WndProc -- pour toute trame 0x63 avec len<4o, un
// crash que le binaire d'origine n'a PAS : Net_OnScriptTrigger 0x4A55F0 lit lui-meme la
// longueur directement sur unk_8156C1 (buffer+1, AVANT le depouillement) et passe
// (unk_8156C5, v1) tel quel a Ac_GuardClient_MakeVerifyData, sans re-lecture ni exception
// possible). Corrige : p.length = len (deja connu), p.data = payload (deja positionne).
struct ScriptTrigger {
    uint32_t       length;   // = len (parametre Parse, deja lu/valide par le dispatcher)
    const uint8_t* data;     // = payload (deja positionne sur les octets de data par le dispatcher)
    static ScriptTrigger Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemSellResult (opcode 0x6a / 106) — resultat de vente d'objet (ajoute or, recharge la cellule).
// Payload lu : resultCode(+0) weightDelta(+4) itemCell[6](+8, 24o). Total 32o.
struct ItemSellResult {
    uint32_t resultCode;   // payload+0 : 0 = succes, 1 = echec
    int32_t  weightDelta;  // payload+4 : ajoute a g_InvWeight
    uint32_t itemCell[6];  // payload+8 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemSellResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnRevivePrompt (opcode 0x72 / 114) — pose le flag 'reanimation disponible'.
// Payload lu : flag(+0). Total 4o.
struct RevivePrompt {
    uint32_t flag;   // payload+0 : 0 = mort / revive disponible
    static RevivePrompt Parse(const uint8_t* payload, size_t len);
};

// Net_OnBuffEffectDispatch (opcode 0xae / 174) — messages de buff/stat + maj cellule inventaire (dispatcher sous-opcodes -1..5).
// Payload lu : subOpcode(+0) param1(+4) param2(+8) param3(+12) param4(+16) param5(+20) param6(+24). Total 28o.
struct BuffEffectDispatch {
    int32_t  subOpcode;  // payload+0  : selecteur (-1..5)
    int32_t  param1;     // payload+4  : itemId / code effet (v35)
    uint32_t param2;     // payload+8  : page inventaire (v33)
    uint32_t param3;     // payload+12 : slot inventaire (v36)
    uint32_t param4;     // payload+16 : position grille (v32)
    int32_t  param5;     // payload+20 : valeur A (v34)
    int32_t  param6;     // payload+24 : valeur B (v38)
    static BuffEffectDispatch Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyMemberHpSet (opcode 0x7f / 127, taille table 21) — pose HP/MP courant+max d'un membre de groupe.
// Payload lu : entityIdHi(+0) entityIdLo(+4) kind(+8) curValue(+12) maxValue(+16). Total 20o.
struct PartyMemberHpSet {
    uint32_t entityIdHi;  // payload+0
    uint32_t entityIdLo;  // payload+4
    uint32_t kind;        // payload+8  : 1 ou 2 (HP vs MP / variante d'affichage)
    int32_t  curValue;    // payload+12 : valeur courante (v10)
    int32_t  maxValue;    // payload+16 : valeur max (v7)
    static PartyMemberHpSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemCellClear (opcode 0x8a / 138, taille table 21) — pose/efface une cellule de la grille d'inventaire.
// Payload lu : resultCode(+0) itemId(+4) invPage(+8) invSlot(+12) gridPos(+16). Total 20o.
struct ItemCellClear {
    uint32_t resultCode;  // payload+0  : 0 = appliquer
    uint32_t itemId;      // payload+4
    uint32_t invPage;     // payload+8  (v2)
    uint32_t invSlot;     // payload+12 (v4)
    uint32_t gridPos;     // payload+16 (v1 ; gridX=gridPos%8, gridY=gridPos/8)
    static ItemCellClear Parse(const uint8_t* payload, size_t len);
};

// Net_OnBattlefieldStatus (opcode 0x93 / 147) — etat de zone/guerre ; sortie de map conditionnee au niveau.
// Payload lu (non aligne) : subState(u8,+0) warState(u32,+1) param(u32,+5). Total 9o.
struct BattlefieldStatus {
    uint8_t  subState;   // payload+0 : sous-etat (v5)
    int32_t  warState;   // payload+1 : etat de guerre -> dword_16692A0 (v6)
    int32_t  param;      // payload+5 : parametre (niveau requis / minuterie) (v8)
    static BattlefieldStatus Parse(const uint8_t* payload, size_t len);
};

// Net_OnAchievementNotice (opcode 0x99 / 153) — notification d'exploit/achievement (table dword_184C218).
// Payload lu : name[13](+0). Total 13o.
struct AchievementNotice {
    char name[13];   // payload+0 : nom (joueur/exploit)
    static AchievementNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemBuyResult (opcode 0xa4 / 164, taille table 29) — resultat d'achat, deduit l'argent, remplit la cellule.
// Payload lu : resultCode(+0) itemCell[6](+4, 24o). Total 28o.
struct ItemBuyResult {
    uint32_t resultCode;   // payload+0 : 0 ou 1
    uint32_t itemCell[6];  // payload+4 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemBuyResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemSlotRefresh (opcode 0xad / 173) — rafraichit une cellule depuis 6 dwords + son, deduit l'or.
// Payload lu : resultCode(+0) goldDelta(+4) itemCell[6](+8, 24o). Total 32o.
struct ItemSlotRefresh {
    uint32_t resultCode;   // payload+0 : 0/10 = succes, 1/2 = echec
    int32_t  goldDelta;    // payload+4 : deduit de g_Currency
    uint32_t itemCell[6];  // payload+8 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemSlotRefresh Parse(const uint8_t* payload, size_t len);
};

// Pkt_CharStatDelta (opcode 0x11 / 17) — mega-dispatcher de deltas de stats/exp/hp/mp (36 sous-cas).
// Payload fixe 24 o (size_table = 25 = 24 + octet opcode).
struct CharStatDelta {
    uint32_t idHi;   // payload+0  — identité entité (haut), matchée contre dword_1687238[227*i]
    uint32_t idLo;   // payload+4  — identité entité (bas),  matchée contre dword_168723C[227*i]
    uint32_t subOp;  // payload+8  — sous-opcode (switch 1..36) sélectionnant le champ à modifier
    uint32_t valA;   // payload+12 — delta principal (v36)
    uint32_t valB;   // payload+16 — valeur secondaire (v39)
    uint32_t valC;   // payload+20 — valeur tertiaire (v43)
    static CharStatDelta Parse(const uint8_t* payload, size_t len);
};

// Pkt_MapObjectUpdate (opcode 0x17 / 23) — transfert de donnees d'objet-carte/stockage.
// Payload fixe 108 o (size_table = 109). Simple relais vers Pkt_DispatchStorageResponse(a, b, body).
struct MapObjectUpdate {
    uint32_t a;         // payload+0  (v2) — 1er argument passe au dispatcher stockage
    uint32_t b;         // payload+4  (v1) — 2e argument
    uint8_t  body[100]; // payload+8  (v3) — bloc de donnees opaque (100 o)
    static MapObjectUpdate Parse(const uint8_t* payload, size_t len);
};

// Pkt_ItemSwapResultB (opcode 0x1f / 31) — resultat de deplacement/echange d'item (variante).
// Payload fixe 32 o. resultCode aiguille 3 branches (0 / 1 / 2).
struct ItemSwapResultB {
    uint32_t resultCode;  // payload+0  (v9) — 0=commit direct, 1=commit via scratch, 2=swap
    uint32_t weightDelta; // payload+4  (v7) — poids retire (g_InvWeight -= weightDelta)
    uint32_t item[6];     // payload+8  (v8) — itemId, gridX, gridY, count, durability, instanceSerial
    static ItemSwapResultB Parse(const uint8_t* payload, size_t len);
};

// Pkt_TradeResult (opcode 0x26 / 38) — resultat de transaction (vente/entrepot). Payload 52 o (size_table = 53).
// resultCode : switch 0..8 (0=vendu, 1..5/7/8=erreurs, 6=annulation/remboursement).
struct TradeResult {
    uint32_t resultCode;  // payload+0  (v25) — selecteur 0..8
    uint32_t weightDelta; // payload+4  (v20) — poids ajoute/retire
    uint32_t invRow;      // payload+8  (v21) — onglet/rangee d'inventaire
    uint32_t invCol;      // payload+12 (v19) — colonne d'inventaire
    uint32_t item[6];     // payload+16 (v23) — itemId, gridX, gridY, count, durability, instanceSerial
    uint32_t aux0;        // payload+40 (v22) — g_InvAux
    uint32_t aux1;        // payload+44 (v26) — dword_1674ABC
    uint32_t aux2;        // payload+48 (v24) — dword_1674AC0
    static TradeResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_PartyChatOrInvite (opcode 0x2a / 42) — chat de groupe / notification d'invitation. Payload 86 o.
struct PartyChatOrInvite {
    uint32_t selector;    // payload+0  (v13) — 0=rejoint, 1/2=notifs, 3=message de chat
    uint32_t flags;       // payload+4  (v10) — champ 4 o auxiliaire
    char     name[13];    // payload+8  (v11) — nom de l'expediteur/cible (buffer 16, 13 lus)
    char     message[61]; // payload+21 (v9)  — texte du message
    uint32_t filterFlag;  // payload+82 (v8)  — cas 3 : si <1 et filtre actif, message ignore
    static PartyChatOrInvite Parse(const uint8_t* payload, size_t len);
};

// Pkt_TradeRequestPrompt (opcode 0x31 / 49) — invite d'echange entrante. Payload 20 o.
struct TradeRequestPrompt {
    uint32_t partner[3]; // payload+0  (v7) — identite du partenaire : idLo, val1, val2
    uint32_t promptId;   // payload+12 (v4) — id affiche entre crochets (%d)
    uint32_t extra;      // payload+16 (v5) — valeur additionnelle -> dword_1675D84
    static TradeRequestPrompt Parse(const uint8_t* payload, size_t len);
};

// Pkt_GuildInfoUpdate (opcode 0x38 / 56) — mise a jour info + roster de guilde. Payload 232 o (size_table = 233).
struct GuildInfoUpdate {
    uint32_t header;        // payload+0   (v2) — en-tete/compteur -> dword_1822848
    uint8_t  block128[128]; // payload+4   (v5) — bloc 128 o (nom/notice de guilde) -> dword_182284C
    uint8_t  members[96];   // payload+132 (v3) — 8 membres x 12 o (3 dwords : id, val1, val2)
    uint32_t footer;        // payload+228 (v7) — champ final -> dword_1822934
    static GuildInfoUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyMemberValueSet (opcode 0x3f / 63) — fixe une valeur de membre et re-scanne le roster. Payload 8 o.
struct PartyMemberValueSet {
    uint32_t index; // payload+0 (v1) — index dans dword_184BE50
    uint32_t value; // payload+4 (v2) — valeur a ecrire
    static PartyMemberValueSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnRequestStateSet (opcode 0x46 / 70) — fixe l'etat UI d'une requete. Payload 4 o.
struct RequestStateSet {
    uint32_t state; // payload+0 (v1) — 0 => dword_1675B14=1 ; 1 => dword_1675B14=2
    static RequestStateSet Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildMemberLeave (opcode 0x4d / 77) — retire un membre du roster d'alliance et decale. Payload 13 o (size_table = 14).
struct GuildMemberLeave {
    char name[13]; // payload+0 (v5) — nom du membre partant (buffer 16, 13 lus)
    static GuildMemberLeave Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildNoticeChat (opcode 0x54 / 84) — poste un message de chat de faction/guilde. Payload 74 o.
struct GuildNoticeChat {
    char name[13];    // payload+0  (v3) — nom de l'expediteur
    char message[61]; // payload+13 (v2) — texte
    static GuildNoticeChat Parse(const uint8_t* payload, size_t len);
};

// Net_OnQuickslotSync (opcode 0x5b / 91) — charge 50 ids de quickslot + valeur d'argent. Payload 212 o.
struct QuickslotSync {
    uint32_t flag;         // payload+0   (v6) — 0 => appliquer
    uint32_t mode;         // payload+4   (v3) — 1 = quickslots, 2 = argent/poids
    uint32_t quickslot[50];// payload+8   (v7) — 50 ids de raccourci
    uint32_t money;        // payload+208 (v5) — valeur -> g_InvWeight (mode 2)
    static QuickslotSync Parse(const uint8_t* payload, size_t len);
};

// Net_OnBossHpDecrement (opcode 0x64 / 100) — decremente le compteur de boss et met a jour ses PV. Payload 16 o.
struct BossHpDecrement {
    uint32_t f0; // payload+0  (v4) -> dword_1675C90
    uint32_t f1; // payload+4  (v5) -> dword_1675C94 (valeur renvoyee)
    uint32_t f2; // payload+8  (v6) -> dword_1675C98
    uint32_t f3; // payload+12 (v7) -> dword_1675C9C
    static BossHpDecrement Parse(const uint8_t* payload, size_t len);
};

// Net_OnGambleResult (opcode 0x6b / 107) — resultat de loterie/pari ; deconnecte a l'echec. Payload 8 o (size_table = 9).
struct GambleResult {
    uint32_t selector; // payload+0 (v4) — 1=gain, 2=fin/echec, 3=info
    uint32_t value;    // payload+4 (v6) — montant/compteur affiche
    static GambleResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnCountdownTimerStart (opcode 0x73 / 115) — demarre un compte a rebours (base timeGetTime). Payload 12 o.
struct CountdownTimerStart {
    uint32_t mode; // payload+0 (v2) -> dword_183914C (-1/1 => son)
    uint32_t f1;   // payload+4 (v1) -> dword_1839150
    uint32_t f2;   // payload+8 (v3) -> dword_1839154
    static CountdownTimerStart Parse(const uint8_t* payload, size_t len);
};

// Net_OnEquipSlotUpdate (opcode 0x78 / 120) — met a jour un slot d'equipement et vide la cellule source. Payload 28 o (size_table = 29).
struct EquipSlotUpdate {
    uint32_t invRow;  // payload+0  (v6) — rangee inventaire source
    uint32_t invCol;  // payload+4  (v7) — colonne inventaire source
    uint32_t contRow; // payload+8  (v1) — rangee conteneur equipement (g_Container5, stride 42)
    uint32_t contCol; // payload+12 (v2) — colonne conteneur (stride 3)
    uint32_t itemId;  // payload+16 (v4) — id item equipe
    uint32_t field1;  // payload+20 (v5) -> dword_1674400
    uint32_t field2;  // payload+24 (v3) -> dword_1674404
    static EquipSlotUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyMemberUpdate (opcode 0x80 / 128) — met a jour un champ de membre de groupe + gestion depart. Payload 20 o.
struct PartyMemberUpdate {
    uint32_t idHi;     // payload+0  (v4) — identite entite (haut), match dword_1687238
    uint32_t idLo;     // payload+4  (v5) — identite entite (bas),  match dword_168723C
    uint32_t selector; // payload+8  (v10) — 1..4 (quel champ)
    uint32_t value1;   // payload+12 (v9) -> dword_1687458
    uint32_t value2;   // payload+16 (v6) -> dword_168745C
    static PartyMemberUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnTradeChatMsg_Ch24 (opcode 0x8b / 139) — poste un message sur le canal de chat 24. Payload 78 o.
struct TradeChatMsg {
    uint32_t f0;      // payload+0  (v4) — champ 4 o (non utilise pour l'affichage)
    char     name[13];// payload+4  (v2) — nom de l'expediteur
    char     message[61]; // payload+17 (v1) — texte
    static TradeChatMsg Parse(const uint8_t* payload, size_t len);
};

// Net_OnBattlefieldStateChange (opcode 0xaa / 170) — changement d'etat du champ de bataille. Payload 5 o.
// ATTENTION : state est sur 1 octet (pas de promotion cote entrant).
struct BattlefieldStateChange {
    uint8_t  state; // payload+0 (v5) — 1 OCTET : 0=ouverture, 1=fermeture
    uint32_t value; // payload+1 (v6[0]) -> dword_16692A0 (etat du BG)
    static BattlefieldStateChange Parse(const uint8_t* payload, size_t len);
};

// Net_OnMountTicketPrompt (opcode 0x9a / 154) — invite NPC pour ticket de monture (items 783..789). Payload 8 o (size_table = 9).
struct MountTicketPrompt {
    uint32_t itemId;   // payload+0 (v6) — id item (lookup ItemDefTbl_GetRecord)
    uint32_t strIndex; // payload+4 (v5) — index de chaine (StrTable003)
    static MountTicketPrompt Parse(const uint8_t* payload, size_t len);
};

// Net_OnChargeStackUpdate (opcode 0xa5 / 165) — met a jour les stacks de charge (ceinture auto-potion). Payload 12 o.
struct ChargeStackUpdate {
    uint32_t flag;  // payload+0 (v3) — 0 => appliquer
    uint32_t mode;  // payload+4 (v1) — 0 = consommation, 1 = recharge
    uint32_t index; // payload+8 (v2) — index de slot (dword_16757D8[index], g_AutoPotionBelt[index])
    static ChargeStackUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemEnhanceResult (opcode 0xaf / 175) — resultat d'amelioration/enchant d'item. Payload 12 o (size_table = 13).
struct ItemEnhanceResult {
    uint32_t resultCode;  // payload+0 (v7) — 1 = succes, 2 = echec/downgrade
    uint32_t cost;        // payload+4 (v6) — cout (g_Currency -= cost)
    uint32_t enhanceByte; // payload+8 (v5) — valeur de niveau ecrite dans l'octet 2 de la durabilite
    static ItemEnhanceResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_SpawnMonster (opcode 0x12) — création/mise à jour d'un enregistrement monstre.
// Payload = 92 octets. body[80] est l'enregistrement recopié dans dword_1766F84 (stride 70 dwords/280 o).
struct SpawnMonster {
    uint32_t idHi;         // payload+0  : clé haute d'identité réseau (dword_1766F78)
    uint32_t idLo;         // payload+4  : clé basse d'identité réseau (dword_1766F7C)
    uint8_t  body[80];     // payload+8  : corps monstre (id modèle en tête -> ItemDefTbl_GetRecord)
    uint32_t updateFlag;   // payload+88 : si ==1, réapplique l'état de mouvement sur un monstre existant
    static SpawnMonster Parse(const uint8_t* payload, size_t len);
};

// Pkt_GameServerConnectResult (opcode 0x18) — résultat de sélection de serveur, déclenche la connexion.
// Payload = 12 octets.
struct GameServerConnectResult {
    uint32_t resultCode;   // payload+0 : code global (0 = poursuivre la connexion, 1..12 = messages d'erreur)
    uint32_t serverId;     // payload+4 : index de serveur (-> Net_SelectServerDomain, sous resultCode==0)
    uint32_t port;         // payload+8 : port du game server (-> Net_ConnectGameServer)
    static GameServerConnectResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_ItemDiscardResult (opcode 0x20, taille 37 = 1 opcode + 36 payload) — résultat de jet/suppression d'objet.
// Payload = 36 octets.
struct ItemDiscardResult {
    uint32_t resultCode;      // payload+0  : code (0/1 = messages, 40 = 3 cellules restaurées, 100/101 = slot aux)
    uint32_t itemId;          // payload+4  : id objet écrit dans la cellule source
    uint32_t gridX;           // payload+8  : colonne grille
    uint32_t gridY;           // payload+12 : ligne grille
    uint32_t count;           // payload+16 : quantité
    uint32_t durability;      // payload+20 : durabilité
    uint32_t instanceSerial;  // payload+24 : série d'instance
    uint32_t adjustMode;      // payload+28 : 1 = ajuster poids, 2 = ajuster monnaie
    uint32_t amount;          // payload+32 : delta (poids ou monnaie) à retrancher
    static ItemDiscardResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_PlayerShopOpen (opcode 0x87) — ouverture de la grille de boutique personnelle d'un joueur.
// Payload = 832 octets. blob = grille de vente structurée (voir notes pour le layout interne).
struct PlayerShopOpen {
    uint32_t focusState;   // payload+0 : état de focus UI (1 = éditeur nom, 2, 3)
    uint32_t resultCode;   // payload+4 : 0 = ouverture OK, 1 = MsgBox, 100/101/102/103 = messages système
    uint8_t  blob[824];    // payload+8 : données de la boutique (nom + 25 cellules objets + 25 cellules prix/qté + 2 dwords)
    static PlayerShopOpen Parse(const uint8_t* payload, size_t len);
};

// Pkt_ShoutMessage (opcode 0x2b) — message de diffusion (cri / GM).
// Payload = 74 octets. Chaînes à taille fixe (non forcément terminées -> traiter comme buffers).
struct ShoutMessage {
    char senderName[13];   // payload+0  : nom de l'émetteur (préfixe "[GM]" -> couleur GM)
    char message[61];      // payload+13 : texte du cri
    static ShoutMessage Parse(const uint8_t* payload, size_t len);
};

// Pkt_TradeRequestResult (opcode 0x32, taille 5 = 1 opcode + 4 payload) — ligne de résultat de demande d'échange.
// Payload = 4 octets.
struct TradeRequestResult {
    uint32_t code;   // payload+0 : code affiché dans le message d'échange ("%s [%d]%s")
    static TradeRequestResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnPvpTallyUpdate (opcode 0x39) — incrémente les compteurs victoire/défaite selon le code.
// Payload = 4 octets.
struct PvpTallyUpdate {
    uint32_t code;   // payload+0 : 0 = défaite (++dword_182292C), 1 = victoire (++dword_1822930), 2 = message
    static PvpTallyUpdate Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyMemberClear (opcode 0x40) — vide un slot du roster de groupe par index.
// Payload = 4 octets. (L'argument fastcall a1 d'origine est immédiatement écrasé par le memcpy du payload.)
struct PartyMemberClear {
    uint32_t slotIndex;   // payload+0 : index du slot roster à réinitialiser (g_PartyRosterNames, stride 13 o)
    static PartyMemberClear Parse(const uint8_t* payload, size_t len);
};

// Net_OnConfirmPromptOpen_Dlg10 (opcode 0x47, taille 14 = 1 opcode + 13 payload) — ouvre le dialogue de confirmation 10.
// Payload = 13 octets.
struct ConfirmPromptOpen_Dlg10 {
    char name[13];   // payload+0 : nom affiché dans le prompt ("[%s]%s")
    static ConfirmPromptOpen_Dlg10 Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildMemberKick (opcode 0x4e) — expulse le nom correspondant du roster d'alliance.
// Payload = 13 octets.
struct GuildMemberKick {
    char name[13];   // payload+0 : nom du membre expulsé
    static GuildMemberKick Parse(const uint8_t* payload, size_t len);
};

// Net_OnFactionChatMessage (opcode 0x55) — message de chat de faction.
// Payload = 74 octets.
struct FactionChatMessage {
    char senderName[13];   // payload+0  : nom de l'émetteur
    char message[61];      // payload+13 : texte
    static FactionChatMessage Parse(const uint8_t* payload, size_t len);
};

// Net_OnGuildActionResult (opcode 0x5c, taille 13 = 1 opcode + 12 payload) — résultat d'action de guilde à 3 cas.
// Payload = 12 octets.
struct GuildActionResult {
    uint32_t flag;    // payload+0 : drapeau de résultat (0 = succès selon action)
    uint32_t action;  // payload+4 : sélecteur d'action (1, 2, 3)
    uint32_t extra;   // payload+8 : lu mais non utilisé dans le handler observé
    static GuildActionResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnBossSpawnNotice (opcode 0x65, taille 5 = 1 opcode + 4 payload) — notification d'apparition de boss.
// Payload = 4 octets.
struct BossSpawnNotice {
    uint32_t value;   // payload+0 : valeur affichée dans le message (id/index de boss)
    static BossSpawnNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnWarehouseMoveResult (opcode 0x6c) — résultat de déplacement d'entrepôt / râtelier d'armes.
// Payload = 28 octets.
struct WarehouseMoveResult {
    uint32_t status;   // payload+0  : code d'état (0 = succès pour les actions 1..4)
    uint32_t action;   // payload+4  : sélecteur d'action (1..5)
    uint32_t index;    // payload+8  : index de slot/râtelier concerné
    uint32_t invRow;   // payload+12 : ligne d'inventaire cible (384*invRow)
    uint32_t invCol;   // payload+16 : colonne d'inventaire cible (6*invCol)
    uint32_t gridPos;  // payload+20 : position linéaire (gridX = %8, gridY = /8)
    uint32_t itemId;   // payload+24 : id objet placé dans la cellule
    static WarehouseMoveResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnCraftResultNotice (opcode 0x74, taille 13 = 1 opcode + 12 payload) — message de résultat de craft/production.
// Payload = 12 octets.
struct CraftResultNotice {
    uint32_t mode;    // payload+0 : 0 ou 1 (choisit la branche de message)
    uint32_t count;   // payload+4 : quantité (si >1 en mode 1 -> StrTable005(1479))
    uint32_t value;   // payload+8 : valeur/id affichée ("[%d]%s")
    static CraftResultNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnSocialListRemove (opcode 0x79) — retire un nom des listes amis/blocage (sous-ops 297/298/299).
// Payload = 21 octets.
struct SocialListRemove {
    uint32_t listOp;    // payload+0 : sous-opcode (297 = liste A, 298/299 = listes B/C)
    uint32_t category;  // payload+4 : index de catégorie/onglet à matcher (comparé à i)
    char     name[13];  // payload+8 : nom à retirer
    static SocialListRemove Parse(const uint8_t* payload, size_t len);
};

// Net_OnPartyItemResult (opcode 0x81) — pose une cellule d'inventaire / quitte le groupe.
// Payload = 20 octets.
struct PartyItemResult {
    uint32_t status;   // payload+0  : 1 = pose objet, 2 = quitter le groupe/retour ville
    uint32_t itemId;   // payload+4  : id objet placé dans la cellule
    uint32_t invRow;   // payload+8  : ligne d'inventaire (384*invRow)
    uint32_t invCol;   // payload+12 : colonne d'inventaire (6*invCol)
    uint32_t gridPos;  // payload+16 : position linéaire (gridX = %8, gridY = /8)
    static PartyItemResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnUpgradeCountNotice (opcode 0x8e) — notices de compteur d'amélioration.
// Payload = 8 octets.
struct UpgradeCountNotice {
    uint32_t mode;    // payload+0 : 0, 1 ou 2 (choix des chaînes de message)
    uint32_t count;   // payload+4 : compteur affiché ("%d%s")
    static UpgradeCountNotice Parse(const uint8_t* payload, size_t len);
};

// Net_OnDataTableLoad_1686F74 (opcode 0x94, taille 685 = 1 opcode + 684 payload) — charge une table de 680 o.
// Payload = 684 octets.
struct DataTableLoad_1686F74 {
    uint32_t flag;        // payload+0 : si 0 -> recopier table vers byte_1686F74
    uint8_t  table[680];  // payload+4 : contenu de la table
    static DataTableLoad_1686F74 Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemSocketResult (opcode 0x9b) — résultat de sertissage d'objet (item socket).
// Payload = 16 octets.
struct ItemSocketResult {
    uint32_t status;    // payload+0  : combiné (status/100 = variante de message, status%100 = cas 0/1)
    uint32_t socket0;   // payload+4  : valeur socket 0 (-> dword_1822F20)
    uint32_t socket1;   // payload+8  : valeur socket 1 (-> dword_1822F24)
    uint32_t socket2;   // payload+12 : valeur socket 2 (-> dword_1822F28)
    static ItemSocketResult Parse(const uint8_t* payload, size_t len);
};

// Net_OnHonorRankEvent (opcode 0xa6) — événements de rang honneur/PK.
// Payload = 8 octets.
struct HonorRankEvent {
    uint32_t category;   // payload+0 : catégorie d'événement (0, 1, 2, 3)
    uint32_t value;      // payload+4 : rang/valeur (interprété selon category ; -> dword_16760F4 en cat 3)
    static HonorRankEvent Parse(const uint8_t* payload, size_t len);
};

// Net_OnItemEnhanceResult2 (opcode 0xb0) — variante de résultat d'amélioration d'objet (6 champs).
// Payload = 24 octets. Les 4 statByte sont repackés en durability via Bits_PackByte012 + Bits_SetByte3.
struct ItemEnhanceResult2 {
    uint32_t resultCode;  // payload+0  : 1 = succès amélioration ; 10..14 = branche transmutation (-1000 monnaie)
    uint32_t newItemId;   // payload+4  : nouvel id objet (utilisé dans la branche 10..14 -> dword_1822F08)
    uint32_t statByte0;   // payload+8  : octet 0 de durabilité (Bits_PackByte012 arg0)
    uint32_t statByte1;   // payload+12 : octet 1 (arg1)
    uint32_t statByte2;   // payload+16 : octet 2 (arg2)
    uint32_t statByte3;   // payload+20 : octet 3 (Bits_SetByte3)
    static ItemEnhanceResult2 Parse(const uint8_t* payload, size_t len);
};

// Pkt_ItemUpgradeResult (opcode 0x1b / 27, taille 13) — ea=0x488DE0 — résultat brut
// d'amélioration/enchantement d'objet (nom IDA historique en collision avec le struct
// « ItemUpgradeResult » ci-dessus qui couvre en réalité Net_OnItemUpgradeResult 0xa8 ;
// désambiguïsé ici en LegacyItemUpgradeResult — AUDIT: opcode absent de RE/handler_domains.json).
struct LegacyItemUpgradeResult {
    uint32_t resultCode;    // payload+0 (switch 0..7 : 0/1=succès, 2=échec, 3/6=dégradation, 5/7=autre)
    uint32_t cost;          // payload+4 (or, dword_16732AC -= cost)
    uint32_t newLevelDelta; // payload+8 (fusionné via Bits_AddByte0(dword_1822F18, val))
    static LegacyItemUpgradeResult Parse(const uint8_t* payload, size_t len);
};

// Pkt_ItemRefineResult (opcode 0x1c / 28, taille 9) — ea=0x48A530 — résultat brut de
// raffinage/sertissage d'objet (nom IDA historique en collision avec le struct
// « ItemRefineResult » ci-dessus qui couvre en réalité Net_OnItemRefineResult 0x7c ;
// désambiguïsé ici en LegacyItemRefineResult — AUDIT: opcode absent de RE/handler_domains.json).
struct LegacyItemRefineResult {
    uint32_t resultCode; // payload+0 (switch 0..3 : 0=succès, 1=succès+2e objet, 2=échec, 3=autre)
    uint32_t cost;       // payload+4 (or, dword_16732AC -= cost)
    static LegacyItemRefineResult Parse(const uint8_t* payload, size_t len);
};

// sub_4AAB60 (opcode 0x82 / 130, taille 61) — ea=0x4AAB60 — copie brute de 60 octets
// (15 floats) dans le bloc global flt_1676130. Aucun parsing/dispatch dans le binaire.
struct RawFloatBlob15 {
    float values[15]; // payload+0..59 -> flt_1676130 (memcpy verbatim, 15 floats)
    static RawFloatBlob15 Parse(const uint8_t* payload, size_t len);
};

// sub_4B33C0 (opcode 0xb1 / 177, taille 5) — ea=0x4B33C0 — setter trivial d'un u32 global.
struct RawU32Setter {
    uint32_t value; // payload+0 -> dword_16874A0 (copié verbatim) ; efface aussi dword_1675B08 (busy flag)
    static RawU32Setter Parse(const uint8_t* payload, size_t len);
};

// Net_OnWorldEntityDispatch (opcode 0x5e / 94, taille sur le fil 105 -> payload 104) —
// ea=0x494870 — MEGA-DISPATCHER (62337 o de code d'origine, ~300 sous-cas, machine à
// états d'activation des compétences de combo/posture du joueur local). Payload = un
// sous-opcode u32 (switch principal, v736 dans le désasm) suivi d'un bloc brut de 100 o
// (v702, réinterprété différemment par sous-cas : u32 idx, u32 idx secondaire, tag 13 o,
// floats...). Comme 0x1a (ItemActionDispatch), ce paquet est un mega-dispatcher hors du
// motif générique struct+OnPacket<T> : ce struct sert de documentation/repère de
// couverture, mais Net/WorldEntityDispatch.h (namespace ts2::game) reparse le payload
// brut directement (mêmes raisons qu'ItemActionDispatch.h). Voir ce fichier pour la
// carte détaillée des sous-opcodes couverts (1..18, familles de combo 1 et 2) et ceux
// documentés en TODO (le reste, ~284 sous-cas, cf. audit Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md §2.4).
struct WorldEntityDispatchHeader {
    uint32_t subOpcode; // payload+0  -> v736 (switch principal)
    uint8_t  raw[100];  // payload+4  -> v702[0..99] (paramètres bruts, dépendants du sous-opcode)
    static WorldEntityDispatchHeader Parse(const uint8_t* payload, size_t len);
};

// ---- parseurs (inline) ----

inline EnterWorld EnterWorld::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    EnterWorld p{};
    r.Read(p.selfCharInvBlock, sizeof(p.selfCharInvBlock)); // 10088 o
    r.Read(p.zoneStateBlock,   sizeof(p.zoneStateBlock));   // 288 o
    return p;
}

inline SpawnNpc SpawnNpc::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnNpc p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body)); // 84 o
    p.action = r.U32();
    return p;
}

inline GroundItemRemove GroundItemRemove::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GroundItemRemove p{};
    p.status         = r.U32();
    p.containerIndex = r.U32();
    p.slotIndex      = r.U32();
    return p;
}

inline ItemResultSimple ItemResultSimple::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemResultSimple p{};
    p.status         = r.U32();
    p.itemId         = r.U32();
    p.gridX          = r.U32();
    p.gridY          = r.U32();
    p.count          = r.U32();
    p.durability     = r.U32();
    p.instanceSerial = r.U32();
    return p;
}

inline PlayerShopBuyResult PlayerShopBuyResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerShopBuyResult p{};
    p.resultCode = r.U32();
    r.Read(p.shopBlock, sizeof(p.shopBlock)); // 824 o
    p.dstRow = r.U32();
    p.dstCol = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));   // 40 o (10 dwords)
    return p;
}

inline DuelResult DuelResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    DuelResult p{};
    p.param = r.U32();
    p.flag  = r.U32();
    return p;
}

inline TradeActionResult TradeActionResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeActionResult p{};
    p.code = r.U32();
    return p;
}

inline FactionBoardSync FactionBoardSync::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FactionBoardSync p{};
    p.code = r.U32();
    return p;
}

inline ConfirmPromptOpen_Dlg20 ConfirmPromptOpen_Dlg20::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpen_Dlg20 p{};
    r.Read(p.nameText, sizeof(p.nameText)); // 13 o
    return p;
}

inline ConfirmPromptClose_Dlg10 ConfirmPromptClose_Dlg10::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return ConfirmPromptClose_Dlg10{};
}

inline GuildRosterUpdate GuildRosterUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildRosterUpdate p{};
    p.code = r.U32();
    r.Read(p.name, sizeof(p.name)); // 13 o
    return p;
}

inline TeamSlotAssign TeamSlotAssign::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TeamSlotAssign p{};
    p.value = r.U32();
    return p;
}

inline PartyInviteResult PartyInviteResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyInviteResult p{};
    p.code  = r.U32();
    p.param = r.U32();
    return p;
}

inline PetSlotDispatch PetSlotDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PetSlotDispatch p{};
    p.subop = r.U32();
    p.value = r.U32();
    return p;
}

inline VendorInventoryLoad VendorInventoryLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    VendorInventoryLoad p{};
    p.status = r.U32();
    p.param  = r.U32();
    r.Read(p.shopItemTable, sizeof(p.shopItemTable)); // 8960 o
    return p;
}

inline MinigameStateLoad MinigameStateLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MinigameStateLoad p{};
    p.a = r.U32();
    p.b = r.U32();
    p.c = r.U32();
    p.d = r.U32();
    return p;
}

inline PartyMemberTargetSet PartyMemberTargetSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberTargetSet p{};
    p.idHi      = r.U32();
    p.idLo      = r.U32();
    p.targetVal = r.U32();
    return p;
}

inline PlayerEquipVisual PlayerEquipVisual::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerEquipVisual p{};
    r.Read(p.visual, sizeof(p.visual)); // 364 o
    return p;
}

inline FriendListEvent FriendListEvent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FriendListEvent p{};
    p.code  = r.U32();
    p.param = r.U32();
    r.Read(p.name, sizeof(p.name)); // 13 o
    return p;
}

inline ItemBatchUpdate ItemBatchUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemBatchUpdate p{};
    p.header      = r.U32();
    p.rowPacked   = r.U32();
    p.colPackedLo = r.U32();
    p.colPackedHi = r.U32();
    p.posPackedLo = r.U32();
    p.posPackedHi = r.U32();
    r.Read(p.itemIds, sizeof(p.itemIds)); // 32 o (8 dwords)
    return p;
}

inline BossHpBarUpdate BossHpBarUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpBarUpdate p{};
    p.hpRaw = r.U32();
    return p;
}

inline ItemUpgradeResult ItemUpgradeResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemUpgradeResult p{};
    p.status         = r.U32();
    p.itemId         = r.U32();
    p.gridX          = r.U32();
    p.gridY          = r.U32();
    p.count          = r.U32();
    p.durability     = r.U32();
    p.instanceSerial = r.U32();
    return p;
}

inline LegacyItemUpgradeResult LegacyItemUpgradeResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    LegacyItemUpgradeResult p{};
    p.resultCode    = r.U32();
    p.cost          = r.U32();
    p.newLevelDelta = r.U32();
    return p;
}

inline RankBoardLoad RankBoardLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RankBoardLoad p{};
    p.header = r.U32();
    r.Read(p.body, sizeof(p.body)); // 600 o
    return p;
}

inline ZoneChangeInfo ZoneChangeInfo::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ZoneChangeInfo p{};
    r.Read(p.block1, sizeof(p.block1));
    r.Read(p.block2, sizeof(p.block2));
    return p;
}

inline SpawnZoneObject SpawnZoneObject::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnZoneObject p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body));
    p.action = r.U32();  // lu à unk_8156FD = payload+60 (après les 52 o de body)
    return p;
}

inline WarehouseOpen WarehouseOpen::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseOpen p{};
    p.status = r.U32();
    r.Read(p.blob, sizeof(p.blob));
    return p;
}

inline PlayerShopGoldResult PlayerShopGoldResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerShopGoldResult p{};
    p.status      = r.U32();
    p.weightDelta = r.U32();
    p.goldDelta   = r.U32();
    return p;
}

inline RepairResult RepairResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RepairResult p{};
    p.status        = r.U32();
    p.goldRemaining = r.U32();
    p.invRow        = r.U32();
    p.invCol        = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

inline AllyInvitePrompt AllyInvitePrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AllyInvitePrompt p{};
    r.Read(p.name, sizeof(p.name));
    p.inviterId = r.U32();  // lu à unk_8156CE = payload+13
    return p;
}

inline ConfirmPromptOpenDlg19 ConfirmPromptOpenDlg19::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpenDlg19 p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline ConfirmPromptCloseDlg20 ConfirmPromptCloseDlg20::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return ConfirmPromptCloseDlg20{};
}

inline ResultDialog340 ResultDialog340::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ResultDialog340 p{};
    p.status = r.U32();
    return p;
}

inline ConfirmPromptOpenDlg14 ConfirmPromptOpenDlg14::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpenDlg14 p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline SelfFactionChat SelfFactionChat::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SelfFactionChat p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline BossHpInit BossHpInit::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpInit p{};
    p.a  = r.U32();  // les 4 premiers u32 sont lus en un seul memcpy de 16 o à l'origine
    p.b  = r.U32();
    p.c  = r.U32();
    p.d  = r.U32();
    p.hp = r.U32();  // lu à unk_8156D1 = payload+16
    return p;
}

inline BossHpInit2 BossHpInit2::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpInit2 p{};
    p.a  = r.U32();
    p.b  = r.U32();
    p.c  = r.U32();
    p.d  = r.U32();
    p.hp = r.U32();  // lu à unk_8156D1 = payload+16
    return p;
}

inline VendorClose VendorClose::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return VendorClose{};
}

inline ItemEnchantDispatch ItemEnchantDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemEnchantDispatch p{};
    p.status = r.U32();
    p.code   = r.U32();
    p.aux0   = r.U32();  // v35
    p.aux1   = r.U32();  // v37 (payload+12)
    p.aux2   = r.U32();  // v36 (payload+16)
    return p;
}

inline ItemRefineResult ItemRefineResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemRefineResult p{};
    p.status      = r.U32();
    p.goldCost    = r.U32();
    p.attribDelta = r.U32();
    return p;
}

inline LegacyItemRefineResult LegacyItemRefineResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    LegacyItemRefineResult p{};
    p.resultCode = r.U32();
    p.cost       = r.U32();
    return p;
}

inline SummonSpawn SummonSpawn::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SummonSpawn p{};
    p.status = r.U32();
    p.slot   = r.U32();
    return p;
}

inline BulkItemConsume BulkItemConsume::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BulkItemConsume p{};
    p.code         = r.U32();
    p.rowPack      = r.U32();
    p.colPackA     = r.U32();
    p.colPackB     = r.U32();
    p.gridPackA    = r.U32();
    p.gridPackB    = r.U32();
    p.currencyType = r.U32();
    p.unitPrice    = r.U32();
    r.Read(p.itemIds, sizeof(p.itemIds));
    return p;
}

inline DataTableLoad1686CCC DataTableLoad1686CCC::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    DataTableLoad1686CCC p{};
    p.status = r.U32();
    r.Read(p.table, sizeof(p.table));
    return p;
}

inline BossPanelLoad BossPanelLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossPanelLoad p{};
    r.Read(p.header, sizeof(p.header));  // memcpy 16 o à l'origine
    r.Read(p.body, sizeof(p.body));      // memcpy 420 o à unk_8156D1 = payload+16
    return p;
}

inline ItemFuseResult ItemFuseResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemFuseResult p{};
    p.status  = r.U32();
    p.subMode = r.U32();
    p.aux0    = r.U32();  // v13
    p.aux1    = r.U32();  // v14[0] (payload+12)
    p.aux2    = r.U32();  // v15[0] (payload+16)
    return p;
}

inline ItemDropResult ItemDropResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemDropResult p{};
    p.status      = r.U32();
    p.goldOrValue = r.U32();
    p.invRow      = r.U32();
    p.invCol      = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

inline SystemMessageBox SystemMessageBox::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SystemMessageBox p{};
    p.param = r.U32();
    r.Read(p.image, sizeof(p.image));
    return p;
}

inline ChatNotice ChatNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ChatNotice p{};
    r.Read(p.text, sizeof(p.text));
    return p;
}

inline WarehouseClose WarehouseClose::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseClose p{};
    p.mode = r.U32();
    return p;
}

inline QuestInteractResult QuestInteractResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    QuestInteractResult p{};
    p.resultCode = r.U32();
    p.invRow = r.U32();
    p.invSlot = r.U32();
    p.gridX = r.U32();
    p.gridY = r.U32();
    return p;
}

inline PartyInvitePrompt PartyInvitePrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyInvitePrompt p{};
    r.Read(p.inviterName, sizeof(p.inviterName));
    p.flag = r.U32();
    return p;
}

inline AllyInviteDecline AllyInviteDecline::Parse(const uint8_t* /*payload*/, size_t /*len*/) {
    return {};
}

inline ConfirmPromptClose_Dlg19 ConfirmPromptClose_Dlg19::Parse(const uint8_t* /*payload*/, size_t /*len*/) {
    return {};
}

inline TradeResultDialog TradeResultDialog::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeResultDialog p{};
    p.resultCode = r.U32();
    return p;
}

inline GuildRosterReset GuildRosterReset::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildRosterReset p{};
    p.mode = r.U32();
    r.Read(p.name1, sizeof(p.name1));
    r.Read(p.name2, sizeof(p.name2));
    r.Read(p.name3, sizeof(p.name3));
    r.Read(p.name4, sizeof(p.name4));
    r.Read(p.name5, sizeof(p.name5));
    return p;
}

inline ConfirmPromptClose_Dlg14 ConfirmPromptClose_Dlg14::Parse(const uint8_t* /*payload*/, size_t /*len*/) {
    return {};
}

inline CultivationDispatch CultivationDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CultivationDispatch p{};
    p.value = r.U32();
    p.subOpcode = r.U32();
    r.Read(p.body, sizeof(p.body));
    return p;
}

inline ZoneBuffStatus ZoneBuffStatus::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ZoneBuffStatus p{};
    for (int i = 0; i < 4; ++i) p.flags[i] = r.U32();
    return p;
}

inline BossHpPercent BossHpPercent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpPercent p{};
    p.hp = r.U32();
    return p;
}

inline SkillCooldownSet SkillCooldownSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SkillCooldownSet p{};
    p.skillId = r.U32();
    p.value = r.U32();
    return p;
}

inline InventoryBulkLoad InventoryBulkLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    InventoryBulkLoad p{};
    p.header = r.U32();
    p.rowPacked = r.U32();
    p.colPackedA = r.U32();
    p.colPackedB = r.U32();
    p.posPackedA = r.U32();
    p.posPackedB = r.U32();
    for (int i = 0; i < 8; ++i) p.itemIds[i] = r.U32();
    p.durPacked = r.U32();
    return p;
}

inline SkillAuraSync SkillAuraSync::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SkillAuraSync p{};
    p.subOpcode = r.U32();
    p.value = r.I32();
    return p;
}

inline SystemNotice SystemNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SystemNotice p{};
    p.subOpcode = r.U32();
    p.value = r.U32();
    return p;
}

inline PartyMemberPosition PartyMemberPosition::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberPosition p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    p.gridX = r.U32();
    p.gridY = r.U32();
    p.pos[0] = r.F32();
    p.pos[1] = r.F32();
    p.pos[2] = r.F32();
    return p;
}

inline MultiItemRemove MultiItemRemove::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MultiItemRemove p{};
    p.resultCode = r.U32();
    p.rowPackedA = r.U32();
    p.rowPackedB = r.U32();
    p.colPackedA = r.U32();
    p.colPackedB = r.U32();
    p.count = r.U32();
    return p;
}

inline NpcDialogEvent NpcDialogEvent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    NpcDialogEvent p{};
    p.subOpcode = r.U32();
    p.nameStringId = r.U32();
    return p;
}

inline ItemSocketDispatch ItemSocketDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSocketDispatch p{};
    p.resultCode = r.U32();
    p.actionType = r.U32();
    p.cost = r.U32();
    for (int i = 0; i < 6; ++i) p.itemSnapshot[i] = r.U32();
    return p;
}

inline StatSyncDispatch StatSyncDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    StatSyncDispatch p{};
    p.resultCode = r.U32();
    p.invWeight = r.U32();
    p.currency = r.U32();
    p.counter = r.U32();
    p.durability = r.U32();
    return p;
}

inline SpawnCharacter SpawnCharacter::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnCharacter p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body));
    p.mode = r.U32();
    return p;
}

inline OnCombatResult OnCombatResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    OnCombatResult p{};
    r.Read(p.block, sizeof(p.block));
    return p;
}

inline ItemCombineResult ItemCombineResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCombineResult p{};
    p.resultCode  = r.U32();
    p.weightDelta = r.U32();
    p.itemId      = r.U32();
    p.gridX       = r.U32();
    p.gridY       = r.U32();
    p.count       = r.U32();
    p.durability  = r.U32();
    p.serial      = r.U32();
    return p;
}

inline WarehouseUpdate WarehouseUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseUpdate p{};
    p.mode = r.U32();
    r.Read(p.data, sizeof(p.data));
    return p;
}

inline ToggleObserver ToggleObserver::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ToggleObserver p{};
    p.resultCode = r.U32();
    return p;
}

inline PartyInviteDecline PartyInviteDecline::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return PartyInviteDecline{};
}

inline AllyJoinResult AllyJoinResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AllyJoinResult p{};
    p.resultCode = r.U32();
    return p;
}

inline PartyResultDialog PartyResultDialog::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyResultDialog p{};
    p.resultCode = r.U32();
    return p;
}

inline RequestTargetNameSet RequestTargetNameSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RequestTargetNameSet p{};
    p.subop = r.U32();
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline GuildMemberJoin GuildMemberJoin::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberJoin p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline ResultDialog399 ResultDialog399::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ResultDialog399 p{};
    p.resultCode = r.U32();
    return p;
}

inline WhisperMessage WhisperMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WhisperMessage p{};
    p.subop = r.U32();
    r.Read(p.sender, sizeof(p.sender));
    r.Read(p.msg, sizeof(p.msg));
    return p;
}

inline ServerNameNotice ServerNameNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ServerNameNotice p{};
    p.subop = r.U32();
    r.Read(p.data, sizeof(p.data));
    return p;
}

inline ItemCellSet ItemCellSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCellSet p{};
    p.resultCode = r.U32();
    p.itemId     = r.U32();
    p.gridX      = r.U32();
    p.gridY      = r.U32();
    p.count      = r.U32();
    p.durability = r.U32();
    p.serial     = r.U32();
    return p;
}

inline ItemCombineResult2 ItemCombineResult2::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCombineResult2 p{};
    p.resultCode  = r.U32();
    p.weightDelta = r.U32();
    p.itemId      = r.U32();
    p.gridX       = r.U32();
    p.gridY       = r.U32();
    p.count       = r.U32();
    p.durability  = r.U32();
    p.serial      = r.U32();
    return p;
}

inline ItemPlaceResult ItemPlaceResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemPlaceResult p{};
    p.resultCode = r.U32();
    p.itemId     = r.U32();
    p.bagRow     = r.U32();
    p.slotCol    = r.U32();
    p.cellIndex  = r.U32();
    p.durability = r.U32();
    return p;
}

inline FriendStatusNotice FriendStatusNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FriendStatusNotice p{};
    p.subop = r.U32();
    r.Read(p.name, sizeof(p.name));
    p.classId = r.U32();
    return p;
}

inline ItemCountNotice ItemCountNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCountNotice p{};
    p.subop = r.U32();
    p.count = r.U32();
    return p;
}

inline ItemMoveResult ItemMoveResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemMoveResult p{};
    p.resultCode = r.U32();
    p.itemId     = r.U32();
    p.bagRow     = r.U32();
    p.slotCol    = r.U32();
    p.cellIndex  = r.U32();
    return p;
}

inline AchievementDataLoad AchievementDataLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AchievementDataLoad p{};
    r.Read(p.flags, sizeof(p.flags));
    return p;
}

inline InstanceEnter InstanceEnter::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    InstanceEnter p{};
    p.subop = r.U32();
    p.code  = r.U32();
    p.p0    = r.U32();
    p.p1    = r.U32();
    p.p2    = r.U32();
    p.p3    = r.U32();
    return p;
}

inline ItemRefineDispatch ItemRefineDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemRefineDispatch p{};
    p.op = r.U32();
    p.a  = r.U32();
    p.b  = r.U32();
    p.c  = r.U32();
    return p;
}

inline ItemCellReset ItemCellReset::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCellReset p{};
    p.flag    = r.U32();
    p.bagRow  = r.U32();
    p.slotCol = r.U32();
    p.coordA  = r.U32();
    p.coordB  = r.U32();
    p.coordC  = r.U32();
    return p;
}

inline CharStateUpdate CharStateUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CharStateUpdate p{};
    p.entityIdHi = r.U32();
    p.entityIdLo = r.U32();
    r.Read(p.stateValues, sizeof(p.stateValues));
    r.Read(p.stateFlags, sizeof(p.stateFlags));
    return p;
}

inline SetGameVar SetGameVar::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SetGameVar p{};
    p.varId = r.U32();
    p.value = r.I32();
    return p;
}

inline ItemSwapResultA ItemSwapResultA::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSwapResultA p{};
    p.resultCode = r.U32();
    p.weightDelta = r.I32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

inline VendorItemEntry VendorItemEntry::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    VendorItemEntry p{};
    p.itemId = r.U32();
    r.Read(p.name, sizeof(p.name));
    p.field17 = r.U32();
    p.field21 = r.U32();
    r.Read(p.blob, sizeof(p.blob));
    p.price0 = r.U32();
    p.price1 = r.U32();
    p.price2 = r.U32();
    p.listId = r.U32();
    return p;
}

inline WhisperReceive WhisperReceive::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WhisperReceive p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

inline PartyJoinResult PartyJoinResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyJoinResult p{};
    p.resultCode = r.U32();
    return p;
}

inline GuildMemberInfo GuildMemberInfo::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberInfo p{};
    p.field0 = r.U32();
    r.Read(p.blockA, sizeof(p.blockA));
    r.Read(p.blockB, sizeof(p.blockB));
    p.field228 = r.U32();
    return p;
}

inline PartyMemberNameSet PartyMemberNameSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberNameSet p{};
    p.slotIndex = r.U32();
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline RequestCancelClear RequestCancelClear::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return RequestCancelClear{};
}

inline GuildChatMessage GuildChatMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildChatMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

inline TeamFormationDispatch TeamFormationDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TeamFormationDispatch p{};
    p.statusCode = r.U32();
    p.subOpcode = r.U32();
    r.Read(p.guildBlob, sizeof(p.guildBlob));
    return p;
}

inline TradeChatMessage TradeChatMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeChatMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

inline ScriptTrigger ScriptTrigger::Parse(const uint8_t* payload, size_t len) {
    // AUDIT reseau (2026-07-14) : `payload`/`len` sont DEJA le champ data + sa taille
    // (le champ len:u32 du fil a ete lu et retire par le dispatcher, cf. commentaire de
    // tete de ScriptTrigger) -- aucune relecture ni ByteReader necessaire, et donc aucune
    // exception possible ici meme pour une trame 0x63 de data vide (len==0).
    ScriptTrigger p{};
    p.length = static_cast<uint32_t>(len);
    p.data = payload;
    return p;
}

inline ItemSellResult ItemSellResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSellResult p{};
    p.resultCode = r.U32();
    p.weightDelta = r.I32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

inline RevivePrompt RevivePrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RevivePrompt p{};
    p.flag = r.U32();
    return p;
}

inline BuffEffectDispatch BuffEffectDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BuffEffectDispatch p{};
    p.subOpcode = r.I32();
    p.param1 = r.I32();
    p.param2 = r.U32();
    p.param3 = r.U32();
    p.param4 = r.U32();
    p.param5 = r.I32();
    p.param6 = r.U32();
    return p;
}

inline PartyMemberHpSet PartyMemberHpSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberHpSet p{};
    p.entityIdHi = r.U32();
    p.entityIdLo = r.U32();
    p.kind = r.U32();
    p.curValue = r.I32();
    p.maxValue = r.I32();
    return p;
}

inline ItemCellClear ItemCellClear::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCellClear p{};
    p.resultCode = r.U32();
    p.itemId = r.U32();
    p.invPage = r.U32();
    p.invSlot = r.U32();
    p.gridPos = r.U32();
    return p;
}

inline BattlefieldStatus BattlefieldStatus::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BattlefieldStatus p{};
    p.subState = r.U8();
    p.warState = r.I32();
    p.param = r.I32();
    return p;
}

inline AchievementNotice AchievementNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AchievementNotice p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline ItemBuyResult ItemBuyResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemBuyResult p{};
    p.resultCode = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

inline ItemSlotRefresh ItemSlotRefresh::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSlotRefresh p{};
    p.resultCode = r.U32();
    p.goldDelta = r.I32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

inline CharStatDelta CharStatDelta::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CharStatDelta p{};
    p.idHi = r.U32(); p.idLo = r.U32(); p.subOp = r.U32();
    p.valA = r.U32(); p.valB = r.U32(); p.valC = r.U32();
    return p;
}

inline MapObjectUpdate MapObjectUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MapObjectUpdate p{};
    p.a = r.U32(); p.b = r.U32(); r.Read(p.body, sizeof(p.body));
    return p;
}

inline ItemSwapResultB ItemSwapResultB::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSwapResultB p{};
    p.resultCode = r.U32(); p.weightDelta = r.U32();
    r.Read(p.item, sizeof(p.item));
    return p;
}

inline TradeResult TradeResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeResult p{};
    p.resultCode = r.U32(); p.weightDelta = r.U32();
    p.invRow = r.U32(); p.invCol = r.U32();
    r.Read(p.item, sizeof(p.item));
    p.aux0 = r.U32(); p.aux1 = r.U32(); p.aux2 = r.U32();
    return p;
}

inline PartyChatOrInvite PartyChatOrInvite::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyChatOrInvite p{};
    p.selector = r.U32(); p.flags = r.U32();
    r.Read(p.name, sizeof(p.name));
    r.Read(p.message, sizeof(p.message));
    p.filterFlag = r.U32();
    return p;
}

inline TradeRequestPrompt TradeRequestPrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeRequestPrompt p{};
    r.Read(p.partner, sizeof(p.partner));
    p.promptId = r.U32(); p.extra = r.U32();
    return p;
}

inline GuildInfoUpdate GuildInfoUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildInfoUpdate p{};
    p.header = r.U32();
    r.Read(p.block128, sizeof(p.block128));
    r.Read(p.members, sizeof(p.members));
    p.footer = r.U32();
    return p;
}

inline PartyMemberValueSet PartyMemberValueSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberValueSet p{};
    p.index = r.U32(); p.value = r.U32();
    return p;
}

inline RequestStateSet RequestStateSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RequestStateSet p{};
    p.state = r.U32();
    return p;
}

inline GuildMemberLeave GuildMemberLeave::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberLeave p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline GuildNoticeChat GuildNoticeChat::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildNoticeChat p{};
    r.Read(p.name, sizeof(p.name));
    r.Read(p.message, sizeof(p.message));
    return p;
}

inline QuickslotSync QuickslotSync::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    QuickslotSync p{};
    p.flag = r.U32(); p.mode = r.U32();
    r.Read(p.quickslot, sizeof(p.quickslot));
    p.money = r.U32();
    return p;
}

inline BossHpDecrement BossHpDecrement::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpDecrement p{};
    p.f0 = r.U32(); p.f1 = r.U32(); p.f2 = r.U32(); p.f3 = r.U32();
    return p;
}

inline GambleResult GambleResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GambleResult p{};
    p.selector = r.U32(); p.value = r.U32();
    return p;
}

inline CountdownTimerStart CountdownTimerStart::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CountdownTimerStart p{};
    p.mode = r.U32(); p.f1 = r.U32(); p.f2 = r.U32();
    return p;
}

inline EquipSlotUpdate EquipSlotUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    EquipSlotUpdate p{};
    p.invRow = r.U32(); p.invCol = r.U32();
    p.contRow = r.U32(); p.contCol = r.U32();
    p.itemId = r.U32(); p.field1 = r.U32(); p.field2 = r.U32();
    return p;
}

inline PartyMemberUpdate PartyMemberUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberUpdate p{};
    p.idHi = r.U32(); p.idLo = r.U32(); p.selector = r.U32();
    p.value1 = r.U32(); p.value2 = r.U32();
    return p;
}

inline TradeChatMsg TradeChatMsg::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeChatMsg p{};
    p.f0 = r.U32();
    r.Read(p.name, sizeof(p.name));
    r.Read(p.message, sizeof(p.message));
    return p;
}

inline BattlefieldStateChange BattlefieldStateChange::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BattlefieldStateChange p{};
    p.state = r.U8(); p.value = r.U32();
    return p;
}

inline MountTicketPrompt MountTicketPrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MountTicketPrompt p{};
    p.itemId = r.U32(); p.strIndex = r.U32();
    return p;
}

inline ChargeStackUpdate ChargeStackUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ChargeStackUpdate p{};
    p.flag = r.U32(); p.mode = r.U32(); p.index = r.U32();
    return p;
}

inline ItemEnhanceResult ItemEnhanceResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemEnhanceResult p{};
    p.resultCode = r.U32(); p.cost = r.U32(); p.enhanceByte = r.U32();
    return p;
}

inline SpawnMonster SpawnMonster::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnMonster p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body));
    p.updateFlag = r.U32();
    return p;
}

inline GameServerConnectResult GameServerConnectResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GameServerConnectResult p{};
    p.resultCode = r.U32();
    p.serverId   = r.U32();
    p.port       = r.U32();
    return p;
}

inline ItemDiscardResult ItemDiscardResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemDiscardResult p{};
    p.resultCode     = r.U32();
    p.itemId         = r.U32();
    p.gridX          = r.U32();
    p.gridY          = r.U32();
    p.count          = r.U32();
    p.durability     = r.U32();
    p.instanceSerial = r.U32();
    p.adjustMode     = r.U32();
    p.amount         = r.U32();
    return p;
}

inline PlayerShopOpen PlayerShopOpen::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerShopOpen p{};
    p.focusState = r.U32();
    p.resultCode = r.U32();
    r.Read(p.blob, sizeof(p.blob));
    return p;
}

inline ShoutMessage ShoutMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ShoutMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

inline TradeRequestResult TradeRequestResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeRequestResult p{};
    p.code = r.U32();
    return p;
}

inline PvpTallyUpdate PvpTallyUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PvpTallyUpdate p{};
    p.code = r.U32();
    return p;
}

inline PartyMemberClear PartyMemberClear::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberClear p{};
    p.slotIndex = r.U32();
    return p;
}

inline ConfirmPromptOpen_Dlg10 ConfirmPromptOpen_Dlg10::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpen_Dlg10 p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline GuildMemberKick GuildMemberKick::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberKick p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline FactionChatMessage FactionChatMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FactionChatMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

inline GuildActionResult GuildActionResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildActionResult p{};
    p.flag   = r.U32();
    p.action = r.U32();
    p.extra  = r.U32();
    return p;
}

inline BossSpawnNotice BossSpawnNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossSpawnNotice p{};
    p.value = r.U32();
    return p;
}

inline WarehouseMoveResult WarehouseMoveResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseMoveResult p{};
    p.status  = r.U32();
    p.action  = r.U32();
    p.index   = r.U32();
    p.invRow  = r.U32();
    p.invCol  = r.U32();
    p.gridPos = r.U32();
    p.itemId  = r.U32();
    return p;
}

inline CraftResultNotice CraftResultNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CraftResultNotice p{};
    p.mode  = r.U32();
    p.count = r.U32();
    p.value = r.U32();
    return p;
}

inline SocialListRemove SocialListRemove::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SocialListRemove p{};
    p.listOp   = r.U32();
    p.category = r.U32();
    r.Read(p.name, sizeof(p.name));
    return p;
}

inline PartyItemResult PartyItemResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyItemResult p{};
    p.status  = r.U32();
    p.itemId  = r.U32();
    p.invRow  = r.U32();
    p.invCol  = r.U32();
    p.gridPos = r.U32();
    return p;
}

inline UpgradeCountNotice UpgradeCountNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    UpgradeCountNotice p{};
    p.mode  = r.U32();
    p.count = r.U32();
    return p;
}

inline DataTableLoad_1686F74 DataTableLoad_1686F74::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    DataTableLoad_1686F74 p{};
    p.flag = r.U32();
    r.Read(p.table, sizeof(p.table));
    return p;
}

inline ItemSocketResult ItemSocketResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSocketResult p{};
    p.status  = r.U32();
    p.socket0 = r.U32();
    p.socket1 = r.U32();
    p.socket2 = r.U32();
    return p;
}

inline HonorRankEvent HonorRankEvent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    HonorRankEvent p{};
    p.category = r.U32();
    p.value    = r.U32();
    return p;
}

inline ItemEnhanceResult2 ItemEnhanceResult2::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemEnhanceResult2 p{};
    p.resultCode = r.U32();
    p.newItemId  = r.U32();
    p.statByte0  = r.U32();
    p.statByte1  = r.U32();
    p.statByte2  = r.U32();
    p.statByte3  = r.U32();
    return p;
}

inline RawFloatBlob15 RawFloatBlob15::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RawFloatBlob15 p{};
    for (float& v : p.values) v = r.F32();
    return p;
}

inline RawU32Setter RawU32Setter::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RawU32Setter p{};
    p.value = r.U32();
    return p;
}

inline WorldEntityDispatchHeader WorldEntityDispatchHeader::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WorldEntityDispatchHeader p{};
    p.subOpcode = r.U32();
    r.Read(p.raw, sizeof(p.raw));
    return p;
}

} // namespace ts2::net

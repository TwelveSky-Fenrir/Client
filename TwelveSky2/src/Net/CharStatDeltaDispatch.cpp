// Net/CharStatDeltaDispatch.cpp — implementation FIDELE du dispatcher op 0x11.
//
// Traduction byte-exacte de Pkt_CharStatDelta (EA 0x465d90) decompile via idaTs2.
// Structure d'origine :
//   1. copie des 6 dwords du payload (idHi/idLo/subOp/valA/valB/valC) ;
//   2. scan lineaire des entites (g_EntityArray 0x1687234, stride 908, 227 dwords)
//      pour trouver l'index v38 tel que active && idHi==payload.idHi && idLo==payload.idLo ;
//      arret au 1er match, v38=-1 sinon -> aucun effet ;
//   3. switch(subOp) : chaque cas ecrit dans le corps (body) de l'entite trouvee et,
//      pour le self (index 0), dans les globals « self ».
//
// -- MAPPAGE DES OFFSETS --------------------------------------------------------
// Le record d'entite fait 908 o : active@+0, idHi@+4, idLo@+8, timestamp@+0x0C,
// body@+0x18. L'adresse d'un champ dword_XXXX indexe [227*v38] correspond donc a
// l'offset body = (0xXXXXXXX - 0x168724C)  (0x168724C = 0x1687234 + 0x18).
// Champs cibles (verifies contre Game/EntityManager.cpp) :
//   body+84  dword_16872A0  compteur de niveau (kPLevelCtr)
//   body+88  dword_16872A4
//   body+8   dword_1687254
//   body+196 dword_1687310  | body+200 dword_1687314 | body+208 dword_168731C
//   body+224 g_SelfAnimFrame (float, kPAnimFrame)
//   body+288 dword_168736C  (AR-min max)  | body+292 dword_1687370  (AR-min courant = "hp")
//   body+296 dword_1687374  (AR-max max)  | body+300 dword_1687378  (AR-max courant = "mp")
//   body+304 dword_168737C  (base tableau d'etats, kPStateArr)
//   body+308 …380 …388 …38C …394 …398 …39C …3A0 …3A4 …3A8 …3AC …3B0 …3B4 (etats/dots)
//   body+420…440 dword_16873F0..1687404 (7 compteurs de dot)
//   body+464 g_TradePartnerIdLo | +468 dword_1687420 | +472 dword_1687424
//   body+508 dword_1687448 (monture/pet) | body+552 dword_1687474 (grade actif)
//   body+580 dword_1687490 | +584 dword_1687494 | +588 dword_1687498 | +592 dword_168749C
// Le cluster de « drapeaux flash » (record +820.. = body+796..) depasse le body de
// 600 o modelise dans PlayerEntity ; on le stocke FIDELEMENT via g_Client.Var/VarF
// keyed sur l'adresse d'origine (+ 908*index pour les entites non-self). Idem pour la
// longue traine de globals « self » (dword_167XXXX / dword_1674XXX) non modelises en
// champ propre — echappatoire sanctionne par ClientRuntime.
//
// -- HORS PERIMETRE (TODO PRECIS) -----------------------------------------------
// Les appels a d'autres sous-systemes sont reproduits pour leurs EFFETS DE DONNEES,
// mais l'appel externe lui-meme est laisse en TODO :
//   Snd3D_PlayPositional 0x4da450 / Snd3D_PlayScaledVolume 0x4da380  (audio)
//   Char_CalcAttackRatingMin 0x4cd970 / Char_CalcAttackRatingMax 0x4ce3f0 (StatEngine :
//     agregation complete equip+niveau+buffs ; ici on relit les stats derivees deja
//     calculees dans g_World.self.atkRatingMin/atkRatingMax)
//   cDrawWin_Init 0x628e40 (UI)  | QuestTbl_FindByGroupAndStage 0x4c8a60 (table quete)
//   Map_BeginWarpToFactionTown 0x55c510 (teleport)  | Net_SendPacket_Op17 0x4b4b70 (envoi)
#include "Net/CharStatDeltaDispatch.h"

#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/ClientRuntime.h"

#include <cstring>
#include <vector>

namespace ts2::game {
namespace {

// ---- lecture/ecriture LE bornee sur un buffer d'octets (sans UB d'aliasing).
inline int32_t  RdI32(const uint8_t* b, size_t o) { int32_t  v; std::memcpy(&v, b + o, 4); return v; }
inline uint32_t RdU32(const uint8_t* b, size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; }
inline void     WrI32(uint8_t* b, size_t o, int32_t v) { std::memcpy(b + o, &v, 4); }
inline void     WrF32(uint8_t* b, size_t o, float   v) { std::memcpy(b + o, &v, 4); }

// ---- offsets body (relatifs au debut du corps de 600 o) — cf. en-tete de fichier.
constexpr size_t B_LEVELCTR = 84;   // dword_16872A0
constexpr size_t B_88       = 88;   // dword_16872A4
constexpr size_t B_8        = 8;    // dword_1687254
constexpr size_t B_196      = 196;  // dword_1687310
constexpr size_t B_200      = 200;  // dword_1687314
constexpr size_t B_208      = 208;  // dword_168731C
constexpr size_t B_ANIM     = 224;  // g_SelfAnimFrame (float)
constexpr size_t B_288      = 288;  // dword_168736C (AR-min max)
constexpr size_t B_HP       = 292;  // dword_1687370 (AR-min courant)
constexpr size_t B_296      = 296;  // dword_1687374 (AR-max max)
constexpr size_t B_MP       = 300;  // dword_1687378 (AR-max courant)
constexpr size_t B_304      = 304;  // dword_168737C
constexpr size_t B_308      = 308;  // dword_1687380
constexpr size_t B_316      = 316;  // dword_1687388
constexpr size_t B_320      = 320;  // dword_168738C
constexpr size_t B_328      = 328;  // dword_1687394
constexpr size_t B_332      = 332;  // dword_1687398
constexpr size_t B_336      = 336;  // dword_168739C
constexpr size_t B_340      = 340;  // dword_16873A0
constexpr size_t B_344      = 344;  // dword_16873A4
constexpr size_t B_348      = 348;  // dword_16873A8
constexpr size_t B_352      = 352;  // dword_16873AC
constexpr size_t B_356      = 356;  // dword_16873B0
constexpr size_t B_360      = 360;  // dword_16873B4
constexpr size_t B_420      = 420;  // dword_16873F0
constexpr size_t B_424      = 424;  // dword_16873F4
constexpr size_t B_428      = 428;  // dword_16873F8
constexpr size_t B_432      = 432;  // dword_16873FC
constexpr size_t B_436      = 436;  // dword_1687400
constexpr size_t B_440      = 440;  // dword_1687404
constexpr size_t B_TRADE    = 464;  // g_TradePartnerIdLo
constexpr size_t B_468      = 468;  // dword_1687420
constexpr size_t B_472      = 472;  // dword_1687424
constexpr size_t B_508      = 508;  // dword_1687448 (monture/pet)
constexpr size_t B_552      = 552;  // dword_1687474 (grade actif)
constexpr size_t B_580      = 580;  // dword_1687490
constexpr size_t B_584      = 584;  // dword_1687494
constexpr size_t B_588      = 588;  // dword_1687498
constexpr size_t B_592      = 592;  // dword_168749C

// ---- adresses d'origine des « drapeaux flash » (record +796.. > body de 600 o) et
//      de la longue traine de globals « self ». Stockees via g_Client.Var/VarF.
constexpr uint32_t FL_568 = 0x1687568, FL_56C = 0x168756C; // cas 1/11
constexpr uint32_t FL_570 = 0x1687570, FL_574 = 0x1687574; // cas 14/27
constexpr uint32_t FL_578 = 0x1687578, FL_57C = 0x168757C; // cas 4
constexpr uint32_t FL_580 = 0x1687580, FL_584 = 0x1687584; // cas 3/22
constexpr uint32_t FL_588 = 0x1687588, FL_58C = 0x168758C; // cas 8
constexpr uint32_t FL_590 = 0x1687590, FL_594 = 0x1687594; // cas 9
constexpr uint32_t FL_5A0 = 0x16875A0, FL_5A4 = 0x16875A4; // cas 10

// Stride d'un record d'entite (908 o), pour dupliquer l'indexation [227*v38] des
// champs hors-body via une cle d'adresse absolue.
constexpr uint32_t kEntStride = 908;

// Acces « flash flag » (int/float) d'une entite d'index idx, keye sur l'adresse d'origine.
inline int32_t& FlagI(uint32_t base, int idx) { return g_Client.Var (base + kEntStride * static_cast<uint32_t>(idx)); }
inline float&   FlagF(uint32_t base, int idx) { return g_Client.VarF(base + kEntStride * static_cast<uint32_t>(idx)); }

// Global « self » scalaire de la longue traine (index unique).
inline int32_t& SV (uint32_t addr) { return g_Client.Var (addr); }
inline float&   SVf(uint32_t addr) { return g_Client.VarF(addr); }

// Element d'un tableau de dwords « self » (cas 30), keye sur base + 4*k.
inline int32_t& Arr(uint32_t base, int k) { return g_Client.Var(base + 4u * static_cast<uint32_t>(k)); }

// LevelTable_GetMaxExp 0x4c2990 : renvoie le champ dword+3 (=LevelInfo::meta, offset +12)
// du record de niveau `lvl` (1..145), sinon 0. Utilise pour créditer le pool de points.
int32_t LevelMetaGain(int lvl) {
    if (lvl < 1 || lvl > 145) return 0;
    const LevelInfo* li = GetLevelInfo(lvl);
    return li ? static_cast<int32_t>(li->meta) : 0;
}

// Char_CalcAttackRatingMin 0x4cd970 : agregat StatEngine (equip + niveau + buffs +
// gemmes + set + skill...). TODO 0x4cd970 : reproduire l'agregation complete ; ici on
// relit la stat derivee deja calculee par le StatEngine dans g_World.self.
inline int32_t CalcAtkRatingMin() { return g_World.self.atkRatingMin; }
// Char_CalcAttackRatingMax 0x4ce3f0 : idem (borne max). TODO 0x4ce3f0.
inline int32_t CalcAtkRatingMax() { return g_World.self.atkRatingMax; }

// QuestTbl_FindByGroupAndStage 0x4c8a60 : recherche d'une entree de quete par groupe
// (element secondaire) et palier (niveau). TODO 0x4c8a60 : brancher la table de quetes.
// Renvoie <=0 tant que non branchee (=> pas de marqueur de quete active), fidele au
// comportement « aucune quete trouvee ».
inline int32_t QuestFindByGroupAndStage(int /*group*/, int /*level*/) { return 0; }

// SkillGrowthTbl_GetRecord 0x4c4e90 : accesseur 1-based dans la table SKILL_INFO
// (stride 776). Renvoie le record `idx1` (1-based) seulement si son 1er dword != 0.
const uint8_t* SkillRecord(int idx1) {
    const DataTable& t = g_World.db.skill;
    if (idx1 < 1 || static_cast<uint32_t>(idx1) > t.count) return nullptr;
    const uint8_t* rec = t.record(static_cast<uint32_t>(idx1) - 1);
    if (!rec || RdI32(rec, 0) == 0) return nullptr;
    return rec;
}

// Clamp « -= d, plancher 0 » sur un champ body (motif repete des cas 4/5/6/31..36).
inline void SubClampFloor0(uint8_t* b, size_t off, int32_t d) {
    int32_t v = RdI32(b, off) - d;
    if (v < 1) v = 0;
    WrI32(b, off, v);
}

// Reflet des barres de combat vers les champs clairs d'un joueur (comme EntityManager).
inline void ReflectBars(PlayerEntity& p) {
    const uint8_t* b = p.body.data();
    p.hp = RdI32(b, B_HP);
    p.mp = RdI32(b, B_MP);
}

// Recalcul + clamp des barres de combat du SELF (index 0) — motif des cas 12/13/26 et
// (sur index 0 explicite) 16/17. `sb` = body du self (players[0]).
void RecomputeSelfBars(uint8_t* sb) {
    const int32_t mn = CalcAtkRatingMin();
    WrI32(sb, B_288, mn);
    if (RdI32(sb, B_HP) > mn) WrI32(sb, B_HP, mn);
    const int32_t mx = CalcAtkRatingMax();
    WrI32(sb, B_296, mx);
    if (RdI32(sb, B_MP) > mx) WrI32(sb, B_MP, mx);
}

// Couleur des messages systeme (g_SysMsgColor 0x84dfd8) — D3DCOLOR ; defaut blanc opaque.
uint32_t SysMsgColor() {
    uint32_t c = static_cast<uint32_t>(g_Client.VarGet(0x84dfd8));
    return c ? c : 0xFFFFFFFFu;
}

} // namespace (anonyme)

// ---------------------------------------------------------------------------
void ApplyCharStatDelta(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 24) return; // payload fixe 24 o (size_table 0x1c).

    const uint32_t idHi  = RdU32(payload, 0);
    const uint32_t idLo  = RdU32(payload, 4);
    const int32_t  subOp = RdI32(payload, 8);
    const int32_t  valA  = RdI32(payload, 12); // v36
    const int32_t  valB  = RdI32(payload, 16); // v39
    const int32_t  valC  = RdI32(payload, 20); // v43

    // --- resolution de l'entite : scan lineaire, arret au 1er slot actif correspondant.
    int idx = -1;
    for (size_t i = 0; i < g_World.players.size(); ++i) {
        const PlayerEntity& e = g_World.players[i];
        if (e.active && e.id.hi == idHi && e.id.lo == idLo) { idx = static_cast<int>(i); break; }
    }
    if (idx < 0) return; // v38 == -1 -> aucun effet.

    const bool self = (idx == 0);
    uint8_t*   b    = g_World.players[idx].body.data();
    // Body du self (index 0) — toujours present si une entite a ete trouvee.
    uint8_t*   sb   = g_World.players[0].body.data();

    switch (subOp) {

    // -- cas 1 : gain de niveau -------------------------------------------------
    case 1:
        if (!self) {
            WrI32(b, B_LEVELCTR, RdI32(b, B_LEVELCTR) + valA); // dword_16872A0 += v36
            FlagI(FL_568, idx) = 1; FlagF(FL_56C, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional(&flt_1687330[227*idx]) (audio positionnel).
        } else {
            for (int i = g_World.self.level + 1; i <= valA + g_World.self.level; ++i) {
                if (i - 1 >= 99) {
                    if (i - 1 >= 112) g_World.self.unspentAttr += 30; // g_SelfUnspentAttrPoints
                    else              g_World.self.unspentAttr += 15;
                } else {
                    g_World.self.unspentAttr += 5;
                }
                g_World.self.skillPoints += LevelMetaGain(i); // g_SkillPointPool += LevelTable_GetMaxExp(i)
            }
            g_World.self.level += valA;                        // g_SelfLevel += v36
            WrI32(b, B_LEVELCTR, RdI32(b, B_LEVELCTR) + valA); // dword_16872A0[0] += v36
            const int32_t mn = CalcAtkRatingMin();
            WrI32(b, B_HP, mn);  WrI32(b, B_288, mn);          // dword_1687370 / dword_168736C
            const int32_t mx = CalcAtkRatingMax();
            WrI32(b, B_MP, mx);  WrI32(b, B_296, mx);          // dword_1687378 / dword_1687374
            FlagI(FL_568, 0) = 1; FlagF(FL_56C, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1) ; TODO 0x628e40 : cDrawWin_Init(&dword_1839290).
            const int32_t q = QuestFindByGroupAndStage(g_World.self.elementSecondary, g_World.self.level);
            SV(0x1675B9C) = q;
            if (q <= 0) {
                SV(0x1675B98) = 0;
            } else {
                SV(0x1675B98) = 1;
                // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            }
            SV(0x1675AE8) = 0;
            SVf(0x1675AEC) = g_World.gameTimeSec - 590.0f; // flt_1675AEC = g_GameTimeSec - 590.0
        }
        break;

    // -- cas 2 : reset compteur body+336 ---------------------------------------
    case 2:
        if (!self) {
            WrI32(b, B_336, 0);                     // dword_168739C
        } else {
            SV(0x1675918) = 0; SV(0x167591C) = 0;
            WrI32(b, B_336, 0);                     // dword_168739C[0]
        }
        break;

    // -- cas 3 : reset bloc de dots (body+340,+420..+440) ----------------------
    case 3:
        if (!self) {
            WrI32(b, B_340, 0); WrI32(b, B_420, 0); WrI32(b, B_424, 0); WrI32(b, B_428, 0);
            WrI32(b, B_432, 0); WrI32(b, B_436, 0); WrI32(b, B_440, 0);
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
        } else {
            SV(0x1675920) = 0; SV(0x1675924) = 0; WrI32(b, B_340, 0);
            SV(0x16759C0) = 0; SV(0x16759C4) = 0; WrI32(b, B_420, 0);
            SV(0x16759C8) = 0; SV(0x16759CC) = 0; WrI32(b, B_424, 0);
            SV(0x16759D0) = 0; SV(0x16759D4) = 0; WrI32(b, B_428, 0);
            SV(0x16759D8) = 0; SV(0x16759DC) = 0; WrI32(b, B_432, 0);
            SV(0x16759E0) = 0; SV(0x16759E4) = 0; WrI32(b, B_436, 0);
            SV(0x16759E8) = 0; SV(0x16759EC) = 0; WrI32(b, B_440, 0);
            FlagI(FL_580, 0) = 1; FlagF(FL_584, 0) = 0.0f;
        }
        break;

    // -- cas 4 : degats PV (AR-min courant -= valA -= valB, plancher 0) --------
    case 4: {
        int32_t hp = RdI32(b, B_HP) - valA;   // dword_1687370 -= v36
        hp -= valB;                            //              -= v39
        if (hp < 1) hp = 0;
        WrI32(b, B_HP, hp);
        FlagI(FL_578, idx) = 1; FlagF(FL_57C, idx) = 0.0f;
        break;
    }

    // -- cas 5 : dot body+340 -= valA (plancher 0), + miroir self --------------
    case 5:
        if (!self) {
            SubClampFloor0(b, B_340, valA);
        } else {
            SV(0x1675920) -= valA;
            if (SV(0x1675920) < 1) { SV(0x1675920) = 0; SV(0x1675924) = 0; }
            SubClampFloor0(b, B_340, valA);
        }
        break;

    // -- cas 6 : identique au cas 5 (dot body+340) -----------------------------
    case 6:
        if (!self) {
            SubClampFloor0(b, B_340, valA);
        } else {
            SV(0x1675920) -= valA;
            if (SV(0x1675920) < 1) { SV(0x1675920) = 0; SV(0x1675924) = 0; }
            SubClampFloor0(b, B_340, valA);
        }
        break;

    // -- cas 7 : identite de partenaire d'echange (body+464/468/472) -----------
    case 7:
        WrI32(b, B_TRADE, valA); // g_TradePartnerIdLo = v36
        WrI32(b, B_468,   valB); // dword_1687420      = v39
        WrI32(b, B_472,   valC); // dword_1687424      = v43
        break;

    // -- cas 8 : soin (AR-min courant += valA) ---------------------------------
    case 8:
        WrI32(b, B_HP, RdI32(b, B_HP) + valA); // dword_1687370 += v36
        FlagI(FL_588, idx) = 1; FlagF(FL_58C, idx) = 0.0f;
        break;

    // -- cas 9 : soin PM (AR-max courant += valA) ------------------------------
    case 9:
        WrI32(b, B_MP, RdI32(b, B_MP) + valA); // dword_1687378 += v36
        FlagI(FL_590, idx) = 1; FlagF(FL_594, idx) = 0.0f;
        break;

    // -- cas 10 : simple declenchement de flash --------------------------------
    case 10:
        FlagI(FL_5A0, idx) = 1; FlagF(FL_5A4, idx) = 0.0f;
        break;

    // -- cas 11 : changement de forme / bonus de niveau ------------------------
    case 11:
        if (!self) {
            WrI32(b, B_208, 0);    // dword_168731C = 0
            WrI32(b, B_88, valA);  // dword_16872A4 = v36
            FlagI(FL_568, idx) = 1; FlagF(FL_56C, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional (audio positionnel).
        } else {
            SV(0x16747BC) = 0;
            g_World.self.skillPoints = valB;   // g_SkillPointPool = v39
            SV(0x16731B4) = 0;
            g_World.self.levelBonus = valA;    // g_SelfLevelBonus = v36
            WrI32(b, B_208, 0);                // dword_168731C[0] = 0
            WrI32(b, B_88, valA);              // dword_16872A4[0] = v36
            WrI32(b, B_HP, CalcAtkRatingMin()); // dword_1687370
            WrI32(b, B_MP, CalcAtkRatingMax()); // dword_1687378
            FlagI(FL_568, 0) = 1; FlagF(FL_56C, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            const int32_t morph = SV(0x1675A98); // g_SelfMorphNpcId
            if (morph == 85) {
                if (g_World.self.levelBonus > 11) {
                    // TODO 0x55c510 : Map_BeginWarpToFactionTown(0).
                }
            } else if (morph == 196 && g_World.self.levelBonus > 0) {
                // TODO 0x55c510 : Map_BeginWarpToFactionTown(0).
            }
        }
        break;

    // -- cas 12 : pose d'un objet/costume (body+196) + recalcul self -----------
    case 12:
        WrI32(b, B_196, valA);   // dword_1687310 = v36
        WrF32(b, B_ANIM, 0.0f);  // g_SelfAnimFrame = 0.0
        if (self) RecomputeSelfBars(b); // index 0 : recalcul + clamp des barres
        break;

    // -- cas 13 : retrait d'objet/costume (body+196 = 0) + recalcul self -------
    case 13:
        WrI32(b, B_196, 0);      // dword_1687310 = 0
        WrF32(b, B_ANIM, 0.0f);  // g_SelfAnimFrame = 0.0
        if (self) RecomputeSelfBars(b);
        break;

    // -- cas 14 : mise a jour d'argent / etat + recalcul self ------------------
    case 14:
        if (!self) {
            WrI32(b, B_8,   valA); // dword_1687254 = v36
            WrI32(b, B_208, valB); // dword_168731C = v39
            FlagI(FL_570, idx) = 1; FlagF(FL_574, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional (audio positionnel).
        } else {
            SV(0x16731B4) = 0;
            g_World.self.currency = valA;      // g_Currency = v36
            g_Client.inv.currency = valA;      // miroir d'inventaire (dword_1687254[0])
            SV(0x16747BC) = valB;
            SV(0x16747C0) = valC;
            WrI32(b, B_8,   valA);             // dword_1687254[0] = v36
            WrI32(b, B_208, valB);             // dword_168731C[0] = v39
            WrI32(b, B_HP, CalcAtkRatingMin()); // dword_1687370
            WrI32(b, B_MP, CalcAtkRatingMax()); // dword_1687378
            FlagI(FL_570, 0) = 1; FlagF(FL_574, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            if (SV(0x16747BC) == 12) {
                // TODO 0x4b4b70 : Net_SendPacket_Op17("%s %s", byte_1673184 (nom local),
                //                 StrTable005_Get(1583)) — notification serveur.
            }
        }
        break;

    // -- cas 16 : pose de monture/pet (body+508 de l'entite) + recalcul SELF ---
    // NB : le recalcul cible TOUJOURS l'index 0 (self), meme pour une entite distante,
    //      car Char_CalcAttackRating* lit dword_1687448[0] (pet du self).
    case 16:
        WrI32(b, B_508, valA); // dword_1687448[227*idx] = v36
        RecomputeSelfBars(sb); // dword_168736C[0].. (index 0 explicite)
        WrF32(b, B_ANIM, 0.0f);// g_SelfAnimFrame[227*idx] = 0.0
        break;

    // -- cas 17 : retrait de monture/pet (body+508 = 0) + recalcul SELF --------
    case 17:
        WrI32(b, B_508, 0);
        RecomputeSelfBars(sb); // index 0 explicite
        WrF32(b, B_ANIM, 0.0f);
        break;

    // -- cas 18 : champ body+200 = valA ----------------------------------------
    case 18:
        WrI32(b, B_200, valA); // dword_1687314 = v36
        break;

    // -- cas 22 : RESET MULTI-CHAMP (switch imbrique sur valA, 0..10) ----------
    case 22: {
        // Volet SELF : reset des globals « self » AVANT le reset du body (LABEL_107).
        // valA hors [0..10] => retour immediat (aucun effet body), fidele au default.
        if (self) {
            switch (valA) {
            case 0:  SV(0x1675918) = 0; SV(0x167591C) = 0; break;
            case 1:  SV(0x16758F8) = 0; SV(0x16758FC) = 0; SV(0x1675908) = 0; SV(0x167590C) = 0; break;
            case 2:  SV(0x16758E0) = 0; SV(0x16758E4) = 0; break;
            case 3:  SV(0x16758D8) = 0; SV(0x16758DC) = 0; break;
            case 4:  SV(0x16758F0) = 0; SV(0x16758F4) = 0; SV(0x1675910) = 0; SV(0x1675914) = 0; break;
            case 5:
                SV(0x1675920) = 0; SV(0x1675924) = 0;
                SV(0x16759C0) = 0; SV(0x16759C4) = 0; SV(0x16759C8) = 0; SV(0x16759CC) = 0;
                SV(0x16759D0) = 0; SV(0x16759D4) = 0; SV(0x16759D8) = 0; SV(0x16759DC) = 0;
                SV(0x16759E0) = 0; SV(0x16759E4) = 0; SV(0x16759E8) = 0; SV(0x16759EC) = 0;
                break;
            case 6:  SV(0x1675928) = 0; SV(0x167592C) = 0; break;
            case 7:  SV(0x1675930) = 0; SV(0x1675934) = 0; break;
            case 8:  SV(0x1675938) = 0; SV(0x167593C) = 0; break;
            case 9:  SV(0x1675940) = 0; SV(0x1675944) = 0; break;
            case 10: SV(0x1675948) = 0; SV(0x167594C) = 0; break;
            default: return; // valA hors plage -> pas de reset body.
            }
        }
        // LABEL_107 : reset des champs body (index idx) + flash 580.
        switch (valA) {
        case 0:
            WrI32(b, B_336, 0);                                 // dword_168739C
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 1:
            WrI32(b, B_320, 0); WrI32(b, B_328, 0);             // dword_168738C, dword_1687394
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 2:
            WrI32(b, B_308, 0);                                 // dword_1687380
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 3:
            WrI32(b, B_304, 0);                                 // dword_168737C
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 4:
            WrI32(b, B_316, 0); WrI32(b, B_332, 0);             // dword_1687388, dword_1687398
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 5:
            WrI32(b, B_340, 0); WrI32(b, B_420, 0); WrI32(b, B_424, 0); WrI32(b, B_428, 0);
            WrI32(b, B_432, 0); WrI32(b, B_436, 0); WrI32(b, B_440, 0);
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 6:
            WrI32(b, B_344, 0);                                 // dword_16873A4
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 7:
            WrI32(b, B_348, 0);                                 // dword_16873A8
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 8:
            WrI32(b, B_352, 0);                                 // dword_16873AC
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 9:
            WrI32(b, B_356, 0);                                 // dword_16873B0
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        case 10:
            WrI32(b, B_360, 0);                                 // dword_16873B4
            FlagI(FL_580, idx) = 1; FlagF(FL_584, idx) = 0.0f;
            break;
        default:
            return; // valA hors plage -> aucun effet.
        }
        break;
    }

    // -- cas 23 : PM = 0 (+ miroir self dword_16746E0) -------------------------
    case 23:
        if (self) SV(0x16746E0) = 0;
        WrI32(b, B_MP, 0);       // dword_1687378 = 0
        break;

    // -- cas 24 : fixe PV (AR-min courant) (+ miroir self dword_16746DC) -------
    case 24:
        if (self) SV(0x16746DC) = valA;
        WrI32(b, B_HP, valA);    // dword_1687370 = v36
        break;

    // -- cas 25 : fixe PM (AR-max courant) (+ miroir self dword_16746E0) -------
    case 25:
        if (self) SV(0x16746E0) = valA;
        WrI32(b, B_MP, valA);    // dword_1687378 = v36
        break;

    // -- cas 26 : grade actif (body+552) + recalcul self -----------------------
    case 26:
        WrI32(b, B_552, valA);   // dword_1687474 = v36
        WrF32(b, B_ANIM, 0.0f);  // g_SelfAnimFrame = 0.0
        if (self) RecomputeSelfBars(b); // index 0 (self) : recalcul + clamp
        break;

    // -- cas 27 : morph/monture (body+208) + recalcul self ---------------------
    case 27:
        if (!self) {
            WrI32(b, B_208, valA); // dword_168731C = v36
            FlagI(FL_570, idx) = 1; FlagF(FL_574, idx) = 0.0f;
            // TODO 0x4da450 : Snd3D_PlayPositional (audio positionnel).
        } else {
            SV(0x16747BC) = valA;
            SV(0x16747C0) = valB;
            WrI32(b, B_208, valA);             // dword_168731C[0] = v36
            WrI32(b, B_HP, CalcAtkRatingMin()); // dword_1687370
            WrI32(b, B_MP, CalcAtkRatingMax()); // dword_1687378
            FlagI(FL_570, 0) = 1; FlagF(FL_574, 0) = 0.0f;
            // TODO 0x4da380 : Snd3D_PlayScaledVolume(0,100,1).
            if (SV(0x16747BC) == 12) {
                // TODO 0x4b4b70 : Net_SendPacket_Op17("%s %s", byte_1673184, StrTable005_Get(1583)).
            }
        }
        break;

    // -- cas 28 : champ body+580 = valA ----------------------------------------
    case 28:
        WrI32(b, B_580, valA);   // dword_1687490 = v36
        WrF32(b, B_ANIM, 0.0f);  // g_SelfAnimFrame = 0.0
        break;

    // -- cas 29 : champ body+584 = valA ----------------------------------------
    case 29:
        WrI32(b, B_584, valA);   // dword_1687494 = v36
        break;

    // -- cas 30 : apprentissage / oubli de competence (self uniquement) --------
    case 30: {
        WrI32(b, B_588, valA);   // dword_1687498 = v36
        WrI32(b, B_592, valB);   // dword_168749C = v39
        if (!self) break;        // if (v38) return : seul le self poursuit.

        // LABEL_159 : commit du dernier changement (dword_1675880/1675884).
        auto label159 = [&]() { SV(0x1675880) = valA; SV(0x1675884) = valB; };

        if (valA == 0) {
            // Oubli d'une competence : purge de la grille de raccourcis + du slot appris.
            if (valC > -1) {
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 14; ++j) {
                        const int m = 42 * i + 3 * j;
                        if (Arr(0x16742BC, 2 * valC) == Arr(0x16743FC, m)) { // g_LearnedSkills[2*v43]==g_Container5_ItemId[m]
                            Arr(0x16743FC, m) = 0;   // g_Container5_ItemId[m] = 0
                            Arr(0x1674400, m) = 0;   // dword_1674400[m] = 0
                            Arr(0x1674404, m) = 0;   // dword_1674404[m] = 0
                            goto done_forget;
                        }
                    }
                }
            done_forget:
                Arr(0x16742BC, 2 * valC) = 0; // g_LearnedSkills[2*v43] = 0
                Arr(0x16742C0, 2 * valC) = 0; // dword_16742C0[2*v43] = 0
                g_Client.msg.System(Str(2428), SysMsgColor()); // StrTable005_Get(2428)
            }
            label159();
            break;
        }

        if (valC <= -1) { label159(); break; }

        const int32_t v32 = valA - 91254;
        const uint8_t* rec = SkillRecord(v32);
        if (rec) {
            Arr(0x16742BC, 2 * valC) = v32;              // g_LearnedSkills[2*v43] = v32
            Arr(0x16742C0, 2 * valC) = RdI32(rec, 560);  // dword_16742C0[2*v43] = *(rec+560)
            g_Client.msg.System(Str(2429), SysMsgColor()); // StrTable005_Get(2429)
            label159();
        }
        // rec == 0 : break sans commit (fidele au `break` d'origine).
        break;
    }

    // -- cas 31..36 : dots body+420..+440 -= valA (plancher 0) + miroir self ---
    case 31:
        if (!self) {
            SubClampFloor0(b, B_420, valA);
        } else {
            SV(0x16759C0) -= valA;
            if (SV(0x16759C0) < 1) { SV(0x16759C0) = 0; SV(0x16759C4) = 0; }
            SubClampFloor0(b, B_420, valA);
        }
        break;
    case 32:
        if (!self) {
            SubClampFloor0(b, B_424, valA);
        } else {
            SV(0x16759C8) -= valA;
            if (SV(0x16759C8) < 1) { SV(0x16759C8) = 0; SV(0x16759CC) = 0; }
            SubClampFloor0(b, B_424, valA);
        }
        break;
    case 33:
        if (!self) {
            SubClampFloor0(b, B_428, valA);
        } else {
            SV(0x16759D0) -= valA;
            if (SV(0x16759D0) < 1) { SV(0x16759D0) = 0; SV(0x16759D4) = 0; }
            SubClampFloor0(b, B_428, valA);
        }
        break;
    case 34:
        if (!self) {
            SubClampFloor0(b, B_432, valA);
        } else {
            SV(0x16759D8) -= valA;
            if (SV(0x16759D8) < 1) { SV(0x16759D8) = 0; SV(0x16759DC) = 0; }
            SubClampFloor0(b, B_432, valA);
        }
        break;
    case 35:
        if (!self) {
            SubClampFloor0(b, B_436, valA);
        } else {
            SV(0x16759E0) -= valA;
            if (SV(0x16759E0) < 1) { SV(0x16759E0) = 0; SV(0x16759E4) = 0; }
            SubClampFloor0(b, B_436, valA);
        }
        break;
    case 36:
        if (!self) {
            SubClampFloor0(b, B_440, valA);
        } else {
            SV(0x16759E8) -= valA;
            if (SV(0x16759E8) < 1) { SV(0x16759E8) = 0; SV(0x16759EC) = 0; }
            SubClampFloor0(b, B_440, valA);
        }
        break;

    // -- subOp 15/19/20/21 et autres : aucun cas -> no-op (fidele au default). --
    default:
        break;
    }

    // Reflet des barres de combat vers les champs clairs (comme EntityManager) : entite
    // trouvee + self (les cas 16/17 modifient l'index 0 meme pour une cible distante).
    ReflectBars(g_World.players[idx]);
    if (idx != 0) ReflectBars(g_World.players[0]);
}

} // namespace ts2::game

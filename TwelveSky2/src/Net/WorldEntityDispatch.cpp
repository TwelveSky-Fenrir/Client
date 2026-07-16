// Net/WorldEntityDispatch.cpp — reecriture partielle de Net_OnWorldEntityDispatch
// (opcode 0x5e, EA 0x494870). Voir Net/WorldEntityDispatch.h pour la carte complete.
//
// Portee de cette passe : sous-opcodes 1..30 (familles de combo 1, 2 et 3, integralement
// lues dans le desassemblage -- la famille 3 compte 12 sous-cas, pas 9, cf. commentaire
// sur kFamily3/ApplyFamily3TagSlot plus bas), 401 (notice HUD autonome), 402..410
// (famille "Special", meme forme mecanique) et 755..763 (famille de combo 4, arme 4 --
// PAS de suite immediate a la famille 3 : c'est la SEULE autre valeur de weaponType
// utilisee par Skill_GetComboMotionId dans tout le dispatcher, verifie par grep exhaustif
// sur le desassemblage complet ; aucune famille "5" de ce type n'existe) et 411..417
// (famille "Buff", Skill_GetBuffMotionId -- 5 sous-cas 411..415 SANS AUCUN gate de
// disponibilite (messages inconditionnels, seule divergence structurelle vs les familles
// de combo/Special ci-dessus) + 416/417 triviaux sur un tableau distinct dword_1686120,
// cf. ApplyBuffFamilySlot/ApplyMiscFlagSlot plus bas -- VERIFIE : dword_16860D0[idx] N'EST
// PAS la source de PlayerEntity::buffs/UI::BuffIconId, cf. commentaire de tete d'
// ApplyBuffFamilySlot). Le reste du mega-switch reste un no-op fidele au `default: return;`
// d'origine -- cf. audit Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md §2.4.
//
// MISE A JOUR (2026-07-14, Docs/TS2_COMBAT_ELEMENT_GATING.md) -- SkillLevelTable ET
// ElementPairTable sont desormais EXPOSEES globalement (Game/SkillCombat.h ::
// GetSkillLevelTable() / Combat_ReadLocalElementPairs(), memes echappatoires
// g_Client.Var()/VarGet() que g_SelfMorphNpcId). Les slots 1/2/3/7 des familles de
// combo 1/2/3/Special (y compris 19/20/21/28 en famille 3 via ApplyFamily3Slot28,
// 402/403/404/408 en famille Special) et 1/2/3 en famille 4 (755/756/757) sont donc
// CABLES INTEGRALEMENT message compris ci-dessous. RESTE OMIS (limite precise,
// documentee au site) : le slot7 de la famille 4 (761) -- son message decode un champ
// 4 chiffres situe a un offset payload AU-DELA de idx/arg2/tag13 confirmes (les seuls
// surs dans cette passe), non verifie bit-a-bit ici -- laisse TODO plutot que de risquer
// une fabrication non fidele. Le popup filtre (UI_MsgBox_Open, slot2 de chaque famille)
// reste HORS PERIMETRE (aucun sink UI modelise dans ClientRuntime, meme convention que
// les popups deja notes TODO ailleurs dans ce fichier, ex. sous-opcode 601/660) : seul le
// message chat (Msg_AppendSystemLine, cote fidele "le message") est cable.
// Les slots gates par `motion == g_SelfMorphNpcId` (4/6/8, y compris 22/24/29, 405/407/409,
// 758/760/762, ET les 3 sous-cas propres a la famille 3 : 25/26/27) etaient DEJA
// CALCULABLES (lecture directe de g_SelfMorphNpcId via l'echappatoire Var()) et donc deja
// integralement cables, message compris.
//
// MISE A JOUR (mission "201-208", 2026-07-14) -- famille "arene individuelle" (morph FIXE
// 194, etat scalaire dword_1686054, cf. ApplyIndividualArenaFamily) CABLEE INTEGRALEMENT
// par lecture directe de RE/dispatch_494870_full.c L.2790-2899 : AUCUN gate
// Skill_IsAvailableByLevel/Skill_IsSpecialUsable dans cette famille (divergence vs toutes
// les familles ci-dessus), demi-duree FIXE=600 au slot4 (204, pas arg2/2), warp via
// Map_BeginWarpToFactionTownDefault (PAS BeginWarpToFactionTown -- pas de garde "mort"
// ici) conditionne EN PLUS par g_SelfCharInvBlock[0] aux slots7/8 (206/207), forme courte
// sans etiquette de classe si idx==-1 au slot7 (206, absente du slot7 generique des
// familles de combo). Cf. bandeau de tete d'ApplyIndividualArenaFamily pour le detail
// complet et le TODO precis (@idx=-1, variable locale partagee non recalculee dans le
// binaire d'origine pour cette branche).
//
// RECONCILIATION DOC (2026-07-14) -- la ligne 16-17 ci-dessus ("Le reste du mega-switch
// reste un no-op fidele...") decrivait la portee de la TOUTE PREMIERE passe sur ce fichier
// et est desormais OBSOLETE prise isolement : des missions ulterieures (non annoncees par
// un bandeau "MISE A JOUR" ici, mais documentees a leur site d'implementation dans ce
// fichier ET dans Net/WorldEntityDispatch.h) ont depuis cable integralement 31/32, 33..115
//     (sauf 110/113), 201..208 (bandeau ci-dessus) et 301..302, 500, 601..603, 611..615,
//     620..629, 659..669, 671..677, 700..754, 755..763, 771..774, 780..795, 800..807 et
//     901..903 -- cf. Net/WorldEntityDispatch.h pour la carte de couverture A JOUR (section
//     "PLAGES NON COUVERTES" + commentaire de tete de ApplyWorldEntityDispatch). Le residu
//     REELLEMENT non cable a ce jour se limite a : 425..428 (buffer byte_1686138 jamais ecrit,
//     insurmontable en statique), 600 (no-op observable confirme), 764..770 (sparse/absent),
//     110/113 (exclusions documentees) et 116..200 (confirme vide, pas un TODO).
#include "Net/WorldEntityDispatch.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"   // GetMonsterInfo / MonsterInfo (resolution 1-based, Apply792to794)
#include "Game/SkillCombat.h"
#include "Game/MapWarp.h"
#include "Game/MotionPoolsCoordResolver.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

namespace ts2::game {
namespace {

// g_SelfMorphNpcId 0x1675A98 — id d'action/posture/« morph » courant du joueur local
// (cf. Game/SkillCombat.h : CombatMorphState::currentActionId, meme adresse). Lu ici en
// LECTURE SEULE via l'echappatoire Var() (aucune instance CombatMorphState globale n'est
// exposee aux handlers reseau a ce jour).
constexpr uint32_t kSelfMorphNpcId = 0x1675A98;

// dword_16747BC -- compteur de palier de renaissance (cf. Game/SkillCombat.h ::
// CombatMorphState::rebirthTier, MEME adresse). Lu ici en LECTURE SEULE via
// l'echappatoire Var(), MEME convention que kSelfMorphNpcId (deja ecrit par
// Net/CharStatDeltaDispatch.cpp via ce meme echappatoire -- aucune duplication de
// stockage introduite ici).
constexpr uint32_t kRebirthTierAddr = 0x16747BC;

// Familles de combo confirmees (etat + horodatage journalier par indice de competence).
constexpr uint32_t kFam1State = 0x1685EAC, kFam1Stamp = 0x1685EE0;
constexpr uint32_t kFam2State = 0x1685F14, kFam2Stamp = 0x1685F2C;
constexpr uint32_t kFam3State = 0x1685F44, kFam3Stamp = 0x1685F6C; // famille 3 (sous-opcodes 19..30)

// Barre de charge partagee (slot 4), commune aux familles.
constexpr uint32_t kChargeArmedTimer = 0x1675BA4; // dword_1675BA4
constexpr uint32_t kChargeElapsed    = 0x1675BA8; // flt_1675BA8
// Drapeaux + demi-duree PROPRES a chaque famille (slot 4).
constexpr uint32_t kFam1ChargeFlag1 = 0x1675BAC, kFam1ChargeFlag0 = 0x1675BB0, kFam1HalfDur = 0x1675BB4;
constexpr uint32_t kFam2ChargeFlag1 = 0x1675BCC, kFam2ChargeFlag0 = 0x1675BD0;
constexpr uint32_t kFam3ChargeFlag1 = 0x1675BD4, kFam3ChargeFlag0 = 0x1675BD8; // pas de demi-duree (comme famille 2)

// Bloc alliance / retour ville (utilise avant sa definition plus bas).
constexpr uint32_t kPerElementCounterAddr = 0x1685E44; // g_PerElementCounter[2*elem]
constexpr uint32_t kPerElementFlagAddr    = 0x1685E48; // dword_1685E48[2*elem]
constexpr uint32_t kAllySlot46ArmFlag      = 0x1675BFC; // kMorphRows[2].flagAddr
constexpr uint32_t kAllySlot46ArmFrame     = 0x1675C00; // kMorphRows[2].frameAddr
constexpr uint32_t kAllySlot47ArmFlag      = 0x1675C04; // kMorphRows[3].flagAddr
constexpr uint32_t kAllySlot47ArmFrame     = 0x1675C08; // kMorphRows[3].frameAddr

// Timer PROPRE au sous-opcode 25 (EA 0x495f47/0x495f53) -- distinct de la barre de
// charge slot4 ; n'a pas d'analogue dans les familles 1/2 (cf. ApplyFamily3TagSlot).
constexpr uint32_t kFam3TagArmedTimer = 0x1675BDC, kFam3TagElapsed = 0x1675BE0;

// Time_GetMonthDayInt (EA non capturee dans cette passe) : horodatage "jour courant"
// utilise pour le cooldown journalier de competence (dword_1685EE0/F2C[idx]). Encodage
// exact (mois*100+jour, par analogie) NON confirme bit-a-bit sur le binaire.
// TODO(@Time_GetMonthDayInt) : retrouver l'EA d'origine et verifier l'encodage precis.
int32_t Time_GetMonthDayInt() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    return (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

// StrTable003_Get(dword_84A6A8, comboMotionId) — nom d'affichage de la competence/motion.
// Meme convention placeholder que le reste du code (cf. GameHandlers_ChatSocial.cpp,
// "Str_GetClassLabel(param) — placeholder") : 003.DAT n'est pas charge/indexe par id de
// motion ici -> Str() (StrTable005, texte stable "#<id>") sert de repere en attendant.
inline std::string SkillName(int comboMotionId) { return Str(comboMotionId); }

// Str_GetClassLabel 0x557A98 -- transcription EXACTE (verifiee par decompilation
// directe idaTs2) : Str(75+id) pour id dans [0,3], chaine vide sinon (repli &String
// d'origine, une constante chaine vide).
inline std::string ClassLabel(int32_t id) {
    return (id >= 0 && id <= 3) ? Str(75 + id) : std::string();
}

// Disponibilite par niveau (Skill_IsAvailableByLevel, gate des slots 1/2/3/7 des
// familles de combo 1/2/3/4) -- SkillLevelTable desormais exposee via
// Game/SkillCombat.h::GetSkillLevelTable(), meme echappatoire g_World.self pour
// level/levelBonus et Var() pour le palier de renaissance (kRebirthTierAddr).
inline bool ComboSkillAvailable(int comboMotionId) {
    return Skill_IsAvailableByLevel(GetSkillLevelTable(), comboMotionId, g_World.self.level,
                                     g_World.self.levelBonus, g_Client.VarGet(kRebirthTierAddr));
}

// Disponibilite de la famille "Special" (Skill_IsSpecialUsable, gate des slots
// 1/2/3/7 -- confirme par decompilation directe EA 0x49ca89 et suivants : c'est
// Skill_IsSpecialUsable, PAS Skill_IsAvailableByLevel, qui gate cette famille).
// CombatMorphState reconstruit a la demande depuis les MEMES echappatoires que
// partout ailleurs dans ce fichier (kSelfMorphNpcId/kRebirthTierAddr).
inline bool SpecialSkillUsable(int specialMotionId) {
    const CombatMorphState morph{g_Client.VarGet(kSelfMorphNpcId), g_Client.VarGet(kRebirthTierAddr)};
    return Skill_IsSpecialUsable(specialMotionId, g_World.self, morph, GetSkillLevelTable());
}

// AUDIT DISPATCH 2026-07-14 (Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md) -- les deux helpers
// ci-dessous factorisent deux paires d'instructions reproduites A L'IDENTIQUE (meme texte,
// memes arguments) dans plusieurs familles editees independamment (ApplyWarStageFamily,
// ApplySiegeStage2Family, ApplyRankFamily740, et la quasi-totalite des slots de retour ville
// de faction de TOUTES les familles ci-dessous) -- pure factorisation mecanique, AUCUN
// changement de comportement (memes appels, memes arguments, meme ordre).

// BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver) -- retour ville de
// faction du joueur local, appele A L'IDENTIQUE 24 fois dans ce fichier (verifie par grep
// exhaustif avant factorisation, y compris dans la famille "branche de competence" 76..100).
inline void WarpToOwnFactionTown() {
    BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
}

// HUD flottant (categorie 1, type 0) + ligne systeme -- meme paire d'appels reproduite A
// L'IDENTIQUE 22 fois, dispersee entre 3 familles distinctes (ApplyWarStageFamily x10,
// ApplySiegeStage2Family x4, ApplyRankFamily740 x8) qui ne partagent par ailleurs aucun code
// (etats/adresses/EA differents) -- seul cet idiome d'annonce est commun aux trois.
inline void AnnounceFloating10(const std::string& s) {
    g_Client.msg.Floating(1, 0, s);
    g_Client.msg.System(s);
}

// HUD flottant (categorie 2, type 4) + ligne systeme -- meme paire d'appels reproduite A
// L'IDENTIQUE 7 fois, dispersee entre 3 fonctions distinctes (ApplyCastAnnounce771to774
// x2, ApplyWarEvent780to786 x4, ApplyWarEvent800to807 x1) qui ne partagent par ailleurs
// aucun etat -- meme rationale de factorisation qu'AnnounceFloating10 ci-dessus (audit
// "dispatch 0x5e" 2026-07-14).
inline void AnnounceFloating24(const std::string& s) {
    g_Client.msg.Floating(2, 4, s);
    g_Client.msg.System(s);
}

template <typename... Args>
inline std::string FormatString(const std::string& fmt, Args... args) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), fmt.c_str(), args...);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Parametres d'une famille de combo (9 slots identiques, cf. Net/WorldEntityDispatch.h).
// ---------------------------------------------------------------------------
struct ComboFamilyCtx {
    int      weaponType;      // argument de Skill_GetComboMotionId(weaponType, idx)
    uint32_t stateAddr;       // dword_1685EAC / dword_1685F14 (etat[idx])
    uint32_t stampAddr;       // dword_1685EE0 / dword_1685F2C (horodatage[idx])
    uint32_t chargeFlag1Addr; // slot4 : dword_1675BAC / dword_1675BCC
    uint32_t chargeFlag0Addr; // slot4 : dword_1675BB0 / dword_1675BD0
    bool     writeHalfDur;    // slot4 : famille 1 ecrit dword_1675BB4=arg2/2 ; famille 2 non (asymetrie confirmee)
    int      strSlot6;        // id StrTable005 du message slot6 (860 famille 1, 235 famille 2)
};

constexpr ComboFamilyCtx kFamily1{1, kFam1State, kFam1Stamp, kFam1ChargeFlag1, kFam1ChargeFlag0, true,  860};
constexpr ComboFamilyCtx kFamily2{2, kFam2State, kFam2Stamp, kFam2ChargeFlag1, kFam2ChargeFlag0, false, 235};
// Famille 3 : slots 1..6 (sous-opcodes 19..24) et 7..9 (28..30) seulement -- cf.
// ApplyFamily3TagSlot/Slot26/Slot27 pour les 3 sous-cas 25..27 qui s'intercalent
// entre slot6 et slot7 SANS analogue dans les familles 1/2 (verifie par lecture
// complete du desassemblage : EA 0x495d66..0x4960bc).
constexpr ComboFamilyCtx kFamily3{3, kFam3State, kFam3Stamp, kFam3ChargeFlag1, kFam3ChargeFlag0, false, 235};

// `idx` = payload+4 (indice de competence dans la famille) ; `arg2` = payload+8 (parametre
// secondaire, seulement lu par les slots 1/4/7). `slot` = position 1..9 dans la famille
// (sous-opcode - base de la famille).
void ApplyComboFamilySlot(const ComboFamilyCtx& fam, int slot, uint32_t idx, uint32_t arg2) {
    const int  comboMotionId = Skill_GetComboMotionId(fam.weaponType, static_cast<int>(idx));
    const bool isCurrentMorph = (comboMotionId == g_Client.VarGet(kSelfMorphNpcId));

    switch (slot) {
    case 1:
        // (sous-opcode base+0, EA 0x494a17/0x4951b0) — AUCUNE ecriture d'etat dans le
        // binaire. Gate = Skill_IsAvailableByLevel -> CABLE (SkillLevelTable exposee via
        // GetSkillLevelTable()) : message "<nom> [<arg2>]<str231>".
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " [" + std::to_string(arg2) + "]" + Str(231));
        }
        break;

    case 2:
        // (base+1) etat=1, ECRIT inconditionnellement (precede le gate dans le desasm).
        // Gate = Skill_IsAvailableByLevel -> CABLE : message "<nom> <str232>". Popup filtree
        // (str242, option g_Options.FilterWorldEntity) + dword_1675BA0=comboMotionId : HORS
        // PERIMETRE (aucun sink UI_MsgBox_Open modelise dans ClientRuntime, meme convention
        // que les autres popups TODO de ce fichier, ex. sous-opcode 601/660).
        g_Client.Var(fam.stateAddr + 4 * idx) = 1;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(232));
        }
        break;

    case 3:
        // (base+2) etat=2, ecrit inconditionnellement. Gate = Skill_IsAvailableByLevel ->
        // CABLE : message "<nom> <str233>" (via le tail-merge LABEL_9, partage avec le
        // sous-opcode 402/Special).
        g_Client.Var(fam.stateAddr + 4 * idx) = 2;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(233));
        }
        break;

    case 4:
        // (base+3) etat=3, ecrit inconditionnellement. Gate = motion==morph courant
        // (CALCULABLE) -> integralement cable : message "<nom> <str234>" + armement de la
        // barre de charge (drapeaux propres a la famille + timer/elapsed partages) + (famille
        // 1 seulement) demi-duree = arg2/2.
        g_Client.Var(fam.stateAddr + 4 * idx) = 3;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(234));
            g_Client.Var(fam.chargeFlag1Addr) = 1;
            g_Client.Var(fam.chargeFlag0Addr) = 0;
            g_Client.Var(kChargeArmedTimer)   = 1;
            g_Client.VarF(kChargeElapsed)     = 0.0f;
            if (fam.writeHalfDur) {
                g_Client.Var(kFam1HalfDur) = static_cast<int32_t>(arg2) / 2; // famille 1 uniquement
            }
        }
        break;

    case 5:
        // (base+4) etat=5, horodatage=jour courant. Gate = disponibilite MAIS resultat
        // JETE dans le binaire (fonction pure, aucune branche, aucun message) -> reproduit
        // a l'identique sans le gate.
        g_Client.Var(fam.stateAddr + 4 * idx) = 5;
        g_Client.Var(fam.stampAddr + 4 * idx) = Time_GetMonthDayInt();
        break;

    case 6:
        // (base+5) etat=5, horodatage=jour courant, ecrits inconditionnellement. Gate =
        // motion==morph courant (CALCULABLE) -> integralement cable : message
        // "<nom> <strSlot6>" + retour ville de faction.
        g_Client.Var(fam.stateAddr + 4 * idx) = 5;
        g_Client.Var(fam.stampAddr + 4 * idx) = Time_GetMonthDayInt();
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(fam.strSlot6));
            WarpToOwnFactionTown();
        }
        break;

    case 7:
        // (base+6) etat=4, horodatage=jour courant, ecrits inconditionnellement. arg2 =
        // id d'element a apparier (Char_GetPairedElement). Gate = Skill_IsAvailableByLevel ;
        // message depend de l'appariement d'element (ElementPairTable)/etiquette de classe
        // -> CABLE (GetSkillLevelTable() + Combat_ReadLocalElementPairs(), meme forme que le
        // tail-merge LABEL_17/LABEL_18 du binaire : forme courte si pas de paire, forme
        // longue "[argLabel],[pairedLabel]" sinon). NB : famille 3 (sous-opcode 28) ne passe
        // PAS par ce case -- forme distincte (tag13 + str246 + timer propre), cf.
        // ApplyFamily3Slot28 plus bas.
        g_Client.Var(fam.stateAddr + 4 * idx) = 4;
        g_Client.Var(fam.stampAddr + 4 * idx) = Time_GetMonthDayInt();
        if (ComboSkillAvailable(comboMotionId)) {
            const ElementPairTable pairs = Combat_ReadLocalElementPairs();
            const int paired = pairs.Paired(static_cast<int>(arg2));
            std::string msg = SkillName(comboMotionId) + " [" + ClassLabel(static_cast<int>(arg2)) + "]";
            if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
            msg += " " + Str(236);
            g_Client.msg.System(msg);
        }
        break;

    case 8:
        // (base+7, tail-merge LABEL_544) etat=5, ecrit inconditionnellement. Gate =
        // motion==morph courant (CALCULABLE) -> cable : message "<nom> <str237>" + retour
        // ville de faction.
        g_Client.Var(fam.stateAddr + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;

    case 9:
        // (base+8) etat=0 (reset). Gate = disponibilite, resultat jete, aucun message dans
        // le binaire -> reproduit a l'identique.
        g_Client.Var(fam.stateAddr + 4 * idx) = 0;
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Famille 3 -- sous-opcodes 25..27 (EA 0x495d66..0x4960bc), SANS analogue dans les
// familles 1/2 : s'intercalent entre le slot6 (24, str235+retour ville) et la reprise
// du slot7 standard (28, cf. switch(slot)==7 ci-dessus). Aucun etat/horodatage ecrit
// (dword_1685F44/1685F6C intouches) ; gate = motion == g_SelfMorphNpcId (CALCULABLE,
// comme les slots 4/6/8) -> integralement cablables, message compris (aucune table
// externe requise, contrairement aux slots 1/3/7 gates par SkillLevelTable).
// ---------------------------------------------------------------------------

// Sous-opcode 25 (EA 0x495d66) : idx = payload+4, branche de classe (0..3) = payload+8,
// tag texte 13 o brut (nom d'objet/competence de l'appariement, pas de table a
// resoudre) = payload+12. Message "<nom> [<classe>] [<tag>] <str243>" (classe = str75
// +branche, meme convention que les sous-opcodes 31/32/35 plus loin dans le switch) ;
// arme un timer PROPRE a ce sous-cas (kFam3TagArmedTimer/kFam3TagElapsed, distinct de
// la barre de charge slot4 dword_1675BA4/flt_1675BA8).
void ApplyFamily3TagSlot(uint32_t idx, uint32_t classBranch, const char* tag13) {
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (comboMotionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    const int classStrId = 75 + static_cast<int>(classBranch & 3); // str75..78 (EA 0x495e0f..0x495f1a)
    g_Client.msg.System(SkillName(comboMotionId) + " [" + Str(classStrId) + "] [" + tag13 + "] " + Str(243));
    g_Client.Var(kFam3TagArmedTimer) = 1;
    g_Client.VarF(kFam3TagElapsed)   = 0.0f;
}

// Sous-opcode 26 (EA 0x495f6e) : idx = payload+4. Pas d'etat, pas de timer. Message
// "<nom> <str244>" seul.
void ApplyFamily3Slot26(uint32_t idx) {
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (comboMotionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(comboMotionId) + " " + Str(244));
}

// Sous-opcode 27 (EA 0x496010) : idx = payload+4, arg2 = payload+8 (parametre affiche
// tel quel, pas d'identifiant a resoudre). Pas d'etat. Message "<nom> [<arg2>]<str245>".
void ApplyFamily3Slot27(uint32_t idx, uint32_t arg2) {
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (comboMotionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(comboMotionId) + " [" + std::to_string(arg2) + "]" + Str(245));
}

// Timer PROPRE au sous-opcode 28 (dword_1675BE4/flt_1675BE8, EA 0x496255/0x496261) --
// DISTINCT du timer du sous-opcode 25 (kFam3TagArmedTimer/kFam3TagElapsed) et de la
// barre de charge partagee slot4 (kChargeArmedTimer/kChargeElapsed).
constexpr uint32_t kFam3PairArmedTimer = 0x1675BE4, kFam3PairElapsed = 0x1675BE8;

// Sous-opcode 28 (EA 0x496277..0x49636d) -- reprise du slot7 standard (etat=4,
// horodatage=jour courant, ECRITS inconditionnellement, meme forme que
// ApplyComboFamilySlot case 7) MAIS avec un tag13 (payload+12, MEME champ brut que le
// sous-opcode 25) et des string/timer PROPRES a ce sous-cas (str246 au lieu de 236,
// format "[classe-tag]" au lieu de "[classe]", timer kFam3PairArmedTimer/Elapsed au
// lieu du timer partage) -- verifie EXHAUSTIVEMENT par decompilation directe (diverge
// du case 7 generique de ApplyComboFamilySlot, PAS route via lui, cf. dispatch
// principal). Gate = Skill_IsAvailableByLevel ; message = Char_GetPairedElement
// (ElementPairTable) + Str_GetClassLabel, memes echappatoires que le case 7 generique.
void ApplyFamily3Slot28(uint32_t idx, uint32_t elt, const char* tag13) {
    g_Client.Var(kFam3State + 4 * idx) = 4;
    g_Client.Var(kFam3Stamp + 4 * idx) = Time_GetMonthDayInt();
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (!ComboSkillAvailable(comboMotionId)) return;
    const ElementPairTable pairs = Combat_ReadLocalElementPairs();
    const int paired = pairs.Paired(static_cast<int>(elt));
    std::string msg = SkillName(comboMotionId) + " [" + ClassLabel(static_cast<int>(elt)) + "-" + tag13 + "]";
    if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
    msg += " " + Str(246);
    g_Client.msg.System(msg);
    g_Client.Var(kFam3PairArmedTimer) = 1;
    g_Client.VarF(kFam3PairElapsed)   = 0.0f;
}

// ---------------------------------------------------------------------------
// Famille "arene individuelle" (morph FIXE 194, RE/dispatch_494870_full.c
// L.2790-2899, sous-opcodes 201..208) -- CABLEE INTEGRALEMENT (2026-07-14,
// lecture directe du desassemblage complet). Etat SCALAIRE dword_1686054 (PAS
// un tableau indexe par idx, contrairement aux familles de combo/Special/4 --
// une seule instance d'arene individuelle possible a la fois). AUCUN gate
// Skill_IsAvailableByLevel/Skill_IsSpecialUsable dans cette famille (verifie :
// les 8 sous-cas ecrivent leur message SANS jamais consulter SkillLevelTable,
// contrairement A TOUTES les familles de combo/Special/4 ci-dessus) -- seul
// gate = g_SelfMorphNpcId==194 (204/206/207). Son (Snd3D_PlayScaledVolume)
// HORS PERIMETRE audio, meme convention que le reste du fichier.
//   201 (L.2790) : idx=payload+4. Message INCONDITIONNEL "<nom194> [<idx>]
//        <str231>". Pas d'etat ecrit (seul sous-cas de la famille sans
//        ecriture d'etat).
//   202 (L.2800) : etat=1. Message inconditionnel "<nom194> <str232>".
//   203 (L.2808) : etat=2. Message inconditionnel "<nom194> <str233>".
//   204 (L.2816) : etat=3. Gate=morph==194 -> message "<nom194> <str234>" +
//        drapeau PROPRE (dword_1675C84/flt_1675C88, DISTINCT de la barre
//        partagee des familles de combo/4, kChargeArmedTimer/kChargeElapsed
//        JAMAIS touchee ici) + demi-duree FIXE=600 (PAS arg2/2 comme les
//        autres familles -- verifie : dword_1675C8C=600 code en dur dans le
//        binaire, idx n'est meme pas lu par ce sous-cas) + 2e message
//        "<nom194> [600]<str843>" + reset 4 champs (dword_1675C90[0]/94/98/9C).
//   205 (L.2840) : etat=5 seul, aucun message (comme le slot5 des familles
//        combo/Special).
//   206 (L.2844) : idx=payload+4 (element a apparier). Etat=4 ecrit
//        inconditionnellement. SI idx==-1 (0xFFFFFFFF) -> message
//        "<nom194> <str845>" (forme COURTE sans etiquette de classe,
//        DIVERGENTE du slot7 generique des familles de combo qui n'a pas
//        cette branche). SINON -> Char_GetPairedElement (Combat_
//        ReadLocalElementPairs()), message "[<classe>]" ou "[<classe>],
//        [<paire>]" + str236 (meme forme que le slot7 generique). Message
//        TOUJOURS envoye (pas de gate Skill_IsAvailableByLevel ici). Warp SI
//        morph==194 ET element local != idx ET element local != paire ET
//        g_SelfCharInvBlock[0] (Map_BeginWarpToFactionTownDefault, PAS
//        BeginWarpToFactionTown -- confirme par le desasm : cette famille
//        n'utilise JAMAIS la garde "mort" des 3 autres warps de ce fichier).
//        TODO(@idx=-1) : dans la branche idx==-1, le binaire n'appelle PAS
//        Char_GetPairedElement -- la variable locale "PairedElement" d'origine
//        est PARTAGEE par le compilateur avec de nombreux autres sous-cas du
//        meme switch geant (BYREF, cf. RE/dispatch_494870_full.c:735, reutilisee
//        aux L.879/988/1187/2857/3119) : sa valeur exacte dans CETTE branche
//        precise n'est pas determinable statiquement (dependrait du chemin
//        d'execution precedent en assembleur, non modelisable depuis le seul
//        pseudocode). Approxime ici par paired=-1 (repli documente ailleurs
//        dans ce fichier comme le comportement par defaut "sans paire", cf.
//        Combat_ReadLocalElementPairs), ce qui ne bloque jamais le warp sur la
//        comparaison "!= paire" -- comportement plausible (idx=-1 = "pas
//        d'adversaire", le jeu renvoie alors en ville) mais NON verifie
//        bit-a-bit sur le binaire.
//   207 (L.2883) : etat=5. Gate=morph==194 -> message "<nom194> <str237>" ;
//        warp SEULEMENT SI g_SelfCharInvBlock[0] (contrairement aux slots8
//        generiques des familles combo/Special/4/Buff qui warpent des que le
//        gate morph passe, sans condition supplementaire).
//   208 (L.2896) : etat=0 (reset). Aucun message (comme le slot9 generique).
// ---------------------------------------------------------------------------
constexpr uint32_t kIndivArenaState = 0x1686054; // scalaire, PAS de tableau indexe
constexpr int32_t  kIndivArenaMorph = 194;
constexpr uint32_t kIndivArenaOwnFlag    = 0x1675C84; // dword_1675C84 -- drapeau PROPRE (pas la barre partagee)
constexpr uint32_t kIndivArenaOwnElapsed = 0x1675C88; // flt_1675C88
constexpr uint32_t kIndivArenaHalfDur    = 0x1675C8C; // dword_1675C8C -- FIXE=600, jamais arg2/2 ici
constexpr uint32_t kIndivArenaReset0 = 0x1675C90, kIndivArenaReset1 = 0x1675C94,
                    kIndivArenaReset2 = 0x1675C98, kIndivArenaReset3 = 0x1675C9C;

void ApplyIndividualArenaFamily(uint32_t subOpcode, uint32_t idx) {
    switch (subOpcode) {
    case 201:
        g_Client.msg.System(SkillName(kIndivArenaMorph) + " [" + std::to_string(idx) + "]" + Str(231));
        return;
    case 202:
        g_Client.Var(kIndivArenaState) = 1;
        g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(232));
        return;
    case 203:
        g_Client.Var(kIndivArenaState) = 2;
        g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(233));
        return;
    case 204:
        g_Client.Var(kIndivArenaState) = 3;
        if (g_Client.VarGet(kSelfMorphNpcId) == kIndivArenaMorph) {
            g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(234));
            g_Client.Var(kIndivArenaOwnFlag)     = 1;
            g_Client.VarF(kIndivArenaOwnElapsed) = 0.0f;
            g_Client.Var(kIndivArenaHalfDur)     = 600; // FIXE, pas arg2/2 (verifie desasm)
            g_Client.msg.System(SkillName(kIndivArenaMorph) + " [600]" + Str(843));
            g_Client.Var(kIndivArenaReset0) = 0; g_Client.Var(kIndivArenaReset1) = 0;
            g_Client.Var(kIndivArenaReset2) = 0; g_Client.Var(kIndivArenaReset3) = 0;
        }
        return;
    case 205:
        g_Client.Var(kIndivArenaState) = 5;
        return;
    case 206: {
        g_Client.Var(kIndivArenaState) = 4;
        const int32_t elt = static_cast<int32_t>(idx);
        int32_t paired = -1; // cf. TODO(@idx=-1) ci-dessus si elt==-1 (jamais recalcule dans ce cas la).
        std::string msg;
        if (elt == -1) {
            msg = SkillName(kIndivArenaMorph) + " " + Str(845);
        } else {
            const ElementPairTable pairs = Combat_ReadLocalElementPairs();
            paired = pairs.Paired(elt);
            msg = SkillName(kIndivArenaMorph) + " [" + ClassLabel(elt) + "]";
            if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
            msg += " " + Str(236);
        }
        g_Client.msg.System(msg);
        const bool hasCharInvBlock = !g_World.self.charInvBlock.empty(); // g_SelfCharInvBlock[0]
        if (g_Client.VarGet(kSelfMorphNpcId) == kIndivArenaMorph &&
            g_World.self.element != elt && g_World.self.element != paired && hasCharInvBlock) {
            BeginWarpToFactionTownDefault(g_World.self.element);
        }
        return;
    }
    case 207:
        g_Client.Var(kIndivArenaState) = 5;
        if (g_Client.VarGet(kSelfMorphNpcId) == kIndivArenaMorph) {
            g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(237));
            if (!g_World.self.charInvBlock.empty()) BeginWarpToFactionTownDefault(g_World.self.element);
        }
        return;
    case 208:
        g_Client.Var(kIndivArenaState) = 0;
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Sous-opcodes 51..62 (mission "sous-plage 51-75", 2026-07-14, RE/dispatch_494870_full.c
// L.1970-2229) -- AUDITES INTEGRALEMENT par decompilation directe. DISTINCTS de
// l'appariement d'alliance 46/47 (g_AlliancePairTable, hors plage assignee) ET de la
// famille de branche de competence 63+ (dword_1685F94, cf. bloc suivant) : systeme de
// "classement/scoreboard" par classe (0..3) et slot (0..9), plus 4 notices independantes
// adjacentes (51, 60, 61, 62) qui ne touchent aucun etat de ce scoreboard :
//   51  notice "[classe] [tag]str259" -- classe=payload+4, tag 13o=payload+8 (PAS +12
//       standard, meme irregularite qu'Apply419/ApplyClassTagFamily). Pas d'etat.
//   52..55  "reinitialisation de palier d'alliance" (dword_1685E74[0]/78/7C/80 = 1/2/3/4).
//       Chacun appelle en plus StrTable005_Get/nettoyages de grille (52..54) ou une
//       recherche de "meilleur slot" v729 (55) -- le HUD_ShowFloatingMessage final de CHAQUE
//       sous-cas est CONFIRME DEAD (v677 jamais rempli -- Crt_StringInit() le vide juste avant
//       sans Crt_Vsnprintf/Crt_Strcat intermediaire -- et v729 de 55 n'est jamais reexploite),
//       meme convention que le dead store deja identifie en 803 (cf. bandeau de
//       ApplyWarEvent800to807) -- MAIS 52 SEUL a un effet annexe REEL (PAS dead, releu par
//       RE-VERIFICATION 2026-07-14) : boucle imbriquee i=0..4/j=0..10 qui remet a 0 LES TROIS
//       tableaux du scoreboard EN ENTIER (dword_168653C/16865DC/168667C, les MEMES que 57/58/59
//       plus bas) -- verifie ligne par ligne sur RE/dispatch_494870_full.c L.2001-2019 (le
//       Crt_StringInit() par slot a l'interieur de la boucle cible tres probablement le buffer
//       nom byte_1686334[130*i+13*j], meme NON CONFIRME depuis le seul pseudocode -- HORS
//       PERIMETRE, meme limite que le reste du fichier pour ce buffer). 53/54/55/56 N'ONT PAS
//       cette boucle (verifie : 53/54 = juste les 4 ecritures scalaires ; 55 = boucle DIFFERENTE,
//       dead, cf. ci-dessus). Etat (dword_1685E74/78/7C/80) + RAZ scoreboard (52 seulement) sont
//       cables -- cf. ApplyAllianceTierReset/ApplyScoreboardFullReset.
//   56  meme etat remis a 0 (reset complet), aucun appel annexe.
//   57  enregistre un slot de classement (classe=+4, slotIdx 0..9=+8, tag 13o=+12
//       [standard], score=+25, rang=+29) : message "[classe] [tag]str766" (chat) PUIS
//       dword_168653C[10*classe+slotIdx]=score, dword_16865DC[...]=rang,
//       dword_168667C[...]=0.
//   58  desenregistre un slot : le message d'origine "[classe] [<nom stocke>]str767" lit
//       byte_1686334[130*classe+13*slotIdx] -- un buffer NOM CONFIRME NON ECRIT dans cette
//       fonction (grep exhaustif sur RE/dispatch_494870_full.c, meme limite que
//       byte_1686138 en 422/424/425-428, cf. bandeau "Sous-opcodes 418..429" plus haut) ->
//       message OMIS, seul l'etat (les 3 tableaux remis a 0 pour ce slot) est cable.
//   59  incremente le compteur du slot (dword_168667C[10*classe+slotIdx] += delta=+12).
//   60/61  notice "[classe] [tag]str773/774" -- classe=+4, tag 13o=+12 [standard]. v720=+8
//       lu mais jamais reexploite (meme convention que kFam4State case7/arg2). Pas d'etat.
//   62  notice conditionnelle : gate = g_LocalElement==elt(+4) -> message "str812(valeur)"
//       (valeur=+8) -- chat seul, pas d'etat.
// ---------------------------------------------------------------------------
constexpr uint32_t kAllianceTierA     = 0x1685E74; // dword_1685E74[0] (scalaire malgre le nom "tableau")
constexpr uint32_t kAllianceTierB     = 0x1685E78;
constexpr uint32_t kAllianceTierC     = 0x1685E7C;
constexpr uint32_t kAllianceTierD     = 0x1685E80;
constexpr uint32_t kScoreboardScore   = 0x168653C; // [10*classe+slotIdx]
constexpr uint32_t kScoreboardRank    = 0x16865DC; // [10*classe+slotIdx]
constexpr uint32_t kScoreboardCounter = 0x168667C; // [10*classe+slotIdx]

// 51 : classe+tag a offset irregulier (+8, pas +12) -- self-contained, meme style qu'Apply419.
void Apply51(const uint8_t* payload, uint32_t len) {
    if (len < 21) return; // payload+8..+20 (13 o de tag) doivent etre disponibles
    uint32_t classId = 0;
    std::memcpy(&classId, payload + 4, 4);
    char tag[14] = {};
    std::memcpy(tag, payload + 8, 13);
    g_Client.msg.System("[" + ClassLabel(static_cast<int32_t>(classId)) + "] [" + tag + "]" + Str(259));
}

// 52..56 : reinitialisation de palier (dword_1685E74/78/7C/80 = value). value=0 pour 56.
void ApplyAllianceTierReset(int32_t value) {
    g_Client.Var(kAllianceTierA) = value;
    g_Client.Var(kAllianceTierB) = value;
    g_Client.Var(kAllianceTierC) = value;
    g_Client.Var(kAllianceTierD) = value;
}

// 52 SEUL (en plus du palier ci-dessus) : RAZ integrale du scoreboard, les 3 tableaux pour
// les 4 classes x 10 slots -- cf. bandeau de tete (boucle REELLE, PAS dead comme le HUD final).
void ApplyScoreboardFullReset() {
    for (uint32_t classId = 0; classId < 4; ++classId) {
        for (uint32_t slotIdx = 0; slotIdx < 10; ++slotIdx) {
            const uint32_t slot = 10 * classId + slotIdx;
            g_Client.Var(kScoreboardScore   + 4 * slot) = 0;
            g_Client.Var(kScoreboardRank    + 4 * slot) = 0;
            g_Client.Var(kScoreboardCounter + 4 * slot) = 0;
        }
    }
}

// 57 : enregistrement d'un slot de classement -- self-contained (score/rang a offset
// non partage avec idx/arg2/tag13 standards, cf. bandeau de tete).
void Apply57(const uint8_t* payload, uint32_t len) {
    if (len < 33) return; // payload+25..+32 (score+rang) doivent etre disponibles
    uint32_t classId = 0, slotIdx = 0, score = 0, rank = 0;
    std::memcpy(&classId, payload + 4, 4);
    std::memcpy(&slotIdx, payload + 8, 4);
    char tag[14] = {};
    std::memcpy(tag, payload + 12, 13); // offset standard (tag13)
    std::memcpy(&score, payload + 25, 4);
    std::memcpy(&rank,  payload + 29, 4);
    g_Client.msg.System("[" + ClassLabel(static_cast<int32_t>(classId)) + "] [" + tag + "]" + Str(766));
    const uint32_t slot = 10 * classId + slotIdx;
    g_Client.Var(kScoreboardScore   + 4 * slot) = static_cast<int32_t>(score);
    g_Client.Var(kScoreboardRank    + 4 * slot) = static_cast<int32_t>(rank);
    g_Client.Var(kScoreboardCounter + 4 * slot) = 0;
}

// 58 : desenregistrement -- message omis (buffer nom byte_1686334 non ecrit ici, cf. bandeau).
void Apply58(uint32_t classId, uint32_t slotIdx) {
    const uint32_t slot = 10 * classId + slotIdx;
    g_Client.Var(kScoreboardScore   + 4 * slot) = 0;
    g_Client.Var(kScoreboardRank    + 4 * slot) = 0;
    g_Client.Var(kScoreboardCounter + 4 * slot) = 0;
}

// 59 : increment du compteur de slot.
void Apply59(uint32_t classId, uint32_t slotIdx, int32_t delta) {
    const uint32_t slot = 10 * classId + slotIdx;
    g_Client.Var(kScoreboardCounter + 4 * slot) = g_Client.VarGet(kScoreboardCounter + 4 * slot) + delta;
}

// 60/61 : notice classe+tag standard (tag13 = payload+12), reutilisable telle quelle par
// le dispatcher (idx/tag13 deja extraits en commun).
void ApplyClassTagNotice(uint32_t classId, const char* tag13, int strId) {
    g_Client.msg.System("[" + ClassLabel(static_cast<int32_t>(classId)) + "] [" + tag13 + "]" + Str(strId));
}

// 62 : notice conditionnelle sur l'element local.
void Apply62(uint32_t elt, int32_t value) {
    if (static_cast<int32_t>(elt) != g_World.self.element) return;
    g_Client.msg.System(Str(812) + "(" + std::to_string(value) + ")");
}

// ---------------------------------------------------------------------------
// Famille "branche de competence" (Skill_GetMotionId2(branch, type), sous-
// opcodes 33..115 dans leur ensemble d'apres l'audit sommaire -- la tranche
// 76..100 est CABLEE dans cette passe (mission "sous-plage 76..100",
// 2026-07-14) ; 63..75 (preambule "poll/arm1/arm2" + 1re/2e iteration du
// cycle ci-dessous) CABLES par la mission "sous-plage 51-75" (meme jour, cf.
// bloc "Sous-opcodes 63..68" plus bas et le dispatcher pour 69..75, qui
// reutilisent les fonctions ApplyBranch* definies ici). 101..115 = sous-
// systeme duel/defi DISTINCT (cf. ApplyDuelBranchFamily). Etat dword_1685F94[8*branch+type]
// (branch=payload+4, type=payload+8, memes offsets idx/arg2 que tout le
// fichier). DISTINCT de Skill_GetComboMotionId (familles 1..4 plus haut) --
// verifie par lecture directe du desassemblage (switch jump_ea=0x494a17,
// EA de tete 0x49a514..0x49b4cb pour cette tranche, cf. RE/
// dispatch_tables.json et RE/dispatch_494870_full.c L.2306-2578).
// Skill_IsAvailableByBranch(lvlTbl, skillId, level, levelBonus, element)
// EXISTE deja (Game/SkillSystem.h/.cpp) mais N'EST PAS appelee ici : le seul
// sous-cas qui l'invoque dans cette tranche (76/83/90/97, "probe") JETTE le
// resultat -- verifie par disasm direct EA 0x49a58d/0x49a58f/0x49a596 : les
// DEUX branches du `test eax,eax` convergent vers le meme saut au `default:`,
// aucune divergence de comportement observable -- meme convention "resultat
// jete -> reproduit sans le gate" que le reste du fichier (cf.
// ApplyComboFamilySlot case 5/9).
//
// A l'interieur de la tranche 76..100, le desassemblage revele un cycle de 7
// sous-cas qui se repete (76..82, 83..89, 90..96, plus une 4e repetition
// PARTIELLE 97..100 -- seulement probe/L418/L420/L422, la tranche s'arretant
// a 100) -- VERIFIE par disasm direct sur les cases 76/78/80 (EA 0x49a514/
// 0x49a670/0x49a7d5, constantes exactement conformes au dump Hex-Rays RE/
// dispatch_494870_full.c ligne 2306 et suivantes) :
//   1. "probe"     (76/83/90/97, EA 0x49a514/a9ad/ae46/b2df) : etat=23 SEUL
//      (Skill_IsAvailableByBranch appelee mais resultat jete, cf. ci-dessus).
//   2. "L418"      (77/84/91/98, EA 0x49a59b/aa34/aecd/b366) : etat=23. Gate =
//      motion==morph courant (CALCULABLE) -> message "<nom> <str235>" +
//      retour ville de faction (MEME tail-merge LABEL_418 que le desasm,
//      str235 = meme id que kFamily2/3.strSlot6 plus haut).
//   3. "L420"      (78/85/92/99, etat=10/14/18/22 respectivement, EA
//      0x49a670/ab09/afa2/b43b) : gate=morph -> SON SEUL (Snd3D_
//      PlayScaledVolume flt_14972BC, fixe, HORS PERIMETRE audio -- meme
//      convention que le reste du fichier) ; PAS de message.
//   4. "L422"      (79/86/93/100, EA 0x49a700/ab99/b032/b4cb) : etat=23.
//      Gate=morph -> message "<nom> <str237>" + retour ville de faction
//      (str237 = meme id que le slot8 des familles de combo/Special).
//   5. "flagsA"    (80/87/94, etat=11/15/19, EA 0x49a7d5/ac6e/b107) : gate=
//      morph -> son (HORS PERIMETRE) + arme 2 drapeaux PROPRES a l'iteration,
//      TOUS deux =1 (dword_1675C1C/C20, C24/C28, C2C/C30) -- PAS de barre de
//      charge partagee ni de demi-duree, contrairement aux familles de combo.
//   6. "flagsB"    (81/88/95, etat=12/16/20, EA 0x49a879/ad12/b1ab) : meme
//      forme que flagsA, drapeaux DIFFERENTS (dword_1675C44/C48, C4C/C50,
//      C54/C58), MEME son que le flagsA de la meme iteration.
//   7. "soundOnly" (82/89/96, etat=13/17/21, EA 0x49a91d/adb6/b24f) : gate=
//      morph -> son SEUL (HORS PERIMETRE), aucun autre effet observable ->
//      reproduit par l'ecriture d'etat seule (meme convention resultat/gate
//      sans effet que "probe" ci-dessus).
// Le cycle recommence a 69 pour la 1re iteration (69..75) -- CABLE par la mission
// "sous-plage 51-75" (2026-07-14, cf. bloc "Sous-opcodes 63..68" plus bas, qui
// reutilise telles quelles ApplyBranchProbe/Label418/Label420/Label422/FlagsSlot/
// SoundOnlySlot ci-dessous pour 69..75).
// ---------------------------------------------------------------------------
constexpr uint32_t kBranchState = 0x1685F94; // dword_1685F94[8*branch+type]

inline int BranchMotionId(uint32_t branch, uint32_t type) {
    return Skill_GetMotionId2(static_cast<int>(branch), static_cast<int>(type));
}

// "probe" (76/83/90/97) : etat=23 seul, Skill_IsAvailableByBranch jamais appelee
// (resultat toujours jete cote binaire, cf. bandeau de tete).
void ApplyBranchProbe(uint32_t branch, uint32_t type) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = 23;
}

// "L418" (77/84/91/98) : etat=23, message str235 + retour ville de faction si morph.
void ApplyBranchLabel418(uint32_t branch, uint32_t type) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = 23;
    const int motionId = BranchMotionId(branch, type);
    if (motionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(motionId) + " " + Str(235));
    WarpToOwnFactionTown();
}

// "L420" (78/85/92/99) : etat=stateValue (10/14/18/22), son seul si morph (HORS
// PERIMETRE audio -- aucun effet modelise au-dela de l'etat).
void ApplyBranchLabel420(uint32_t branch, uint32_t type, int32_t stateValue) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
}

// "L422" (79/86/93/100) : etat=23, message str237 + retour ville de faction si morph.
void ApplyBranchLabel422(uint32_t branch, uint32_t type) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = 23;
    const int motionId = BranchMotionId(branch, type);
    if (motionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(motionId) + " " + Str(237));
    WarpToOwnFactionTown();
}

// "flagsA"/"flagsB" (80/81, 87/88, 94/95) : etat=stateValue, arme 2 drapeaux
// PROPRES (=1 chacun) si morph (son HORS PERIMETRE audio).
void ApplyBranchFlagsSlot(uint32_t branch, uint32_t type, int32_t stateValue,
                           uint32_t flagAddr1, uint32_t flagAddr2) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
    const int motionId = BranchMotionId(branch, type);
    if (motionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.Var(flagAddr1) = 1;
    g_Client.Var(flagAddr2) = 1;
}

// "soundOnly" (82/89/96) : etat=stateValue seul (son HORS PERIMETRE audio, aucun
// autre effet observable -> pas besoin de calculer motionId/gate, cf. ApplyBranchProbe).
void ApplyBranchSoundOnlySlot(uint32_t branch, uint32_t type, int32_t stateValue) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
}

// ---------------------------------------------------------------------------
// Sous-opcodes 63..68 (mission "sous-plage 51-75", 2026-07-14, RE/dispatch_494870_full.c
// L.2219-2305) -- PREAMBULE de la famille de branche de competence ci-dessus, AVANT le
// cycle repetitif "probe/L418/L420/L422/flagsA/flagsB/soundOnly" qui commence formellement
// a 69 (cf. bandeau de tete du bloc precedent, "Le cycle recommence a 69") : 3 sous-cas
// "poll/arm1/arm2" SANS analogue dans le cycle 76..100 (63/64/65 -- CE sont les SEULS
// endroits de toute la famille de branche ou Skill_IsAvailableByBranch influence
// reellement un branchement ; le "probe" 69/76/83/90/97 jette systematiquement son
// resultat, cf. bandeau ci-dessus), PUIS 3 sous-cas (66/67/68) qui sont en realite la
// PREMIERE iteration du cycle "flagsA/flagsB/soundOnly" (etats 3/4/5, progression +4
// confirmee jusqu'a 19/20/21 en 94/95/96) -- reutilisent TELLES QUELLES
// ApplyBranchFlagsSlot/ApplyBranchSoundOnlySlot deja definies ci-dessus :
//   63  "poll" (L.2230) : LECTURE SEULE (dword_1685F94 jamais ecrit ici). Gate =
//       Skill_IsAvailableByBranch ET etat courant<=0 -> message "<nom> [<arg3>]<str231>"
//       (HUD flottant cat5/type1 + chat). arg3 = payload+12 (entier -- reinterpretation du
//       meme champ brut que tag13 ailleurs dans ce fichier, comme partout ici).
//   64  "arm1" (L.2245) : etat=1, ecrit inconditionnellement. Gate=avail -> message
//       "<nom> <str232>" (HUD cat5/type2 + chat).
//   65  "arm2" (L.2259) : etat=2, ecrit inconditionnellement. Gate=avail -> message
//       "<nom> <str233>" (HUD cat5/type3 + chat).
//   66  "flagsA" 1re iteration (L.2273) : etat=3, drapeaux dword_1675C0C/1675C10=1 si
//       morph courant (son HORS PERIMETRE) -> ApplyBranchFlagsSlot(...,3,0x1675C0C,0x1675C10).
//   67  "flagsB" 1re iteration (L.2285) : etat=4, drapeaux dword_1675C34/1675C38=1 si
//       morph -> ApplyBranchFlagsSlot(...,4,0x1675C34,0x1675C38).
//   68  "soundOnly" 1re iteration (L.2297) : etat=5, son seul si morph (HORS PERIMETRE)
//       -> ApplyBranchSoundOnlySlot(...,5).
// ---------------------------------------------------------------------------
inline bool BranchSkillAvailable(int motionId) {
    return Skill_IsAvailableByBranch(GetSkillLevelTable(), motionId, g_World.self.level,
                                      g_World.self.levelBonus, g_World.self.element);
}

// 63 : "poll", resultat de Skill_IsAvailableByBranch REELLEMENT utilise (contrairement au
// "probe" 69/76/83/90/97 qui le jette).
void ApplyBranchPoll(uint32_t branch, uint32_t type, uint32_t arg3) {
    const int motionId = BranchMotionId(branch, type);
    const int32_t state = g_Client.VarGet(kBranchState + 4 * (8 * branch + type));
    if (state > 0 || !BranchSkillAvailable(motionId)) return;
    const std::string msg = SkillName(motionId) + " [" + std::to_string(arg3) + "]" + Str(231);
    g_Client.msg.Floating(5, 1, msg);
    g_Client.msg.System(msg);
}

// 64/65 : "arm1"/"arm2", etat ecrit inconditionnellement puis message gate par disponibilite.
void ApplyBranchArm(uint32_t branch, uint32_t type, int32_t stateValue, int strId, int floatType) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
    const int motionId = BranchMotionId(branch, type);
    if (!BranchSkillAvailable(motionId)) return;
    const std::string msg = SkillName(motionId) + " " + Str(strId);
    g_Client.msg.Floating(5, floatType, msg);
    g_Client.msg.System(msg);
}

// ---------------------------------------------------------------------------
// Sous-opcode 401 (EA 0x497c66) — notification HUD AUTONOME, SANS lien avec la
// famille "Special" qui suit (pas de Skill_GetSpecialMotionId, payload pas lu).
// Banniere flottante (type=2, flag=2) + ligne systeme. Couleur d'origine
// g_SysMsgColor (0x84DFD8) non modelisee ici -> couleur par defaut, meme
// convention que Game/NpcInteraction.cpp.
// ---------------------------------------------------------------------------
void ApplyNotice401() {
    const std::string text = Str(1139);
    g_Client.msg.Floating(2, 2, text);
    g_Client.msg.System(text);
}

// ---------------------------------------------------------------------------
// Famille "Special" (Skill_GetSpecialMotionId, sous-opcodes 402..410, EA
// 0x49ca89..0x49d1cf) : MEME mecanique de 9 slots que les familles de combo
// (kFamily1/2/3), avec 3 divergences confirmees par lecture complete du
// desassemblage :
//   - gate d'indisponibilite = Skill_IsSpecialUsable(id, self, morph, lvlTbl) au
//     lieu de Skill_IsAvailableByLevel -- CABLE (SpecialSkillUsable(), meme
//     SkillLevelTable exposee via GetSkillLevelTable()) : messages des slots
//     1/2/3/7 desormais presents.
//   - slot4 (405) N'ARME PAS la barre de charge partagee (dword_1675BA4/
//     flt_1675BA8 JAMAIS touchee ici, contrairement A TOUTES les familles de
//     combo 1/2/3/4) : seul un drapeau propre est arme -- dword_1675CB0 (int)
//     PUIS flt_1675CB4 (FLOAT, asymetrie confirmee : c'est un flt_ dans le
//     desasm, pas un dword_ comme le flag0 des familles combo) -- plus une
//     demi-duree (dword_1675CB8=arg2/2) + 2e ligne de chat
//     "[demiduree]<str843>" + reset a 4 champs (dword_1675CBC/CC0[0]/CC4/CC8/
//     CCC), meme FORME que la famille 4 (slot4) mais SANS le toucher de la
//     barre partagee ni le 2e son.
//   - slot7 (408) utilise Char_GetPairedElement (ElementPairTable) -- CABLE
//     (Combat_ReadLocalElementPairs()), meme forme de message que le slot7
//     generique des familles 1/2 (str236, "[argLabel],[pairedLabel]" si paire).
// ---------------------------------------------------------------------------
constexpr uint32_t kSpecialState = 0x16860C0;
constexpr uint32_t kSpecialFlag1 = 0x1675CB0;   // dword_1675CB0 (int)
constexpr uint32_t kSpecialFlag0 = 0x1675CB4;   // flt_1675CB4 (FLOAT -- asymetrie vs familles combo)
constexpr uint32_t kSpecialHalfDur = 0x1675CB8;
constexpr uint32_t kSpecialResetA  = 0x1675CBC, kSpecialResetB0 = 0x1675CC0,
                    kSpecialResetB1 = 0x1675CC4, kSpecialResetB2 = 0x1675CC8, kSpecialResetB3 = 0x1675CCC;

void ApplySpecialFamilySlot(int slot, uint32_t idx, uint32_t arg2) {
    const int  specialMotionId = Skill_GetSpecialMotionId(static_cast<int>(idx));
    const bool isCurrentMorph = (specialMotionId == g_Client.VarGet(kSelfMorphNpcId));

    switch (slot) {
    case 1: // 402 -- aucune ecriture d'etat. Gate=Skill_IsSpecialUsable -> CABLE.
        if (SpecialSkillUsable(specialMotionId)) {
            g_Client.msg.System(SkillName(specialMotionId) + " [" + std::to_string(arg2) + "]" + Str(231));
        }
        break;
    case 2: // 403 etat=1, ecrit inconditionnellement. Gate -> CABLE (popup filtre HORS PERIMETRE).
        g_Client.Var(kSpecialState + 4 * idx) = 1;
        if (SpecialSkillUsable(specialMotionId)) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(232));
        }
        break;
    case 3: // 404 etat=2, ecrit inconditionnellement. Gate -> CABLE.
        g_Client.Var(kSpecialState + 4 * idx) = 2;
        if (SpecialSkillUsable(specialMotionId)) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(233));
        }
        break;
    case 4: // 405 etat=3, ecrit inconditionnellement. Gate=morph (CALCULABLE) -> cable.
        g_Client.Var(kSpecialState + 4 * idx) = 3;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(234));
            g_Client.Var(kSpecialFlag1)  = 1;
            g_Client.VarF(kSpecialFlag0) = 0.0f;
            const int32_t halfDur = static_cast<int32_t>(arg2) / 2;
            g_Client.Var(kSpecialHalfDur) = halfDur;
            g_Client.msg.System("[" + std::to_string(halfDur) + "]" + Str(843));
            g_Client.Var(kSpecialResetA)  = 1;
            g_Client.Var(kSpecialResetB0) = 0;
            g_Client.Var(kSpecialResetB1) = 0;
            g_Client.Var(kSpecialResetB2) = 0;
            g_Client.Var(kSpecialResetB3) = 0;
        }
        break;
    case 5: // 406 etat=5. Gate=disponibilite, resultat jete -> reproduit sans gate.
        g_Client.Var(kSpecialState + 4 * idx) = 5;
        break;
    case 6: // 407 etat=5, gate=morph -> message str860 + retour ville de faction.
        g_Client.Var(kSpecialState + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(860));
            WarpToOwnFactionTown();
        }
        break;
    case 7: // 408 etat=4, ecrit inconditionnellement. Gate=Skill_IsSpecialUsable ; message =
            // Char_GetPairedElement (ElementPairTable) + Str_GetClassLabel -> CABLE (meme
            // forme que le slot7 generique des familles de combo, str236).
        g_Client.Var(kSpecialState + 4 * idx) = 4;
        if (SpecialSkillUsable(specialMotionId)) {
            const ElementPairTable pairs = Combat_ReadLocalElementPairs();
            const int paired = pairs.Paired(static_cast<int>(arg2));
            std::string msg = SkillName(specialMotionId) + " [" + ClassLabel(static_cast<int>(arg2)) + "]";
            if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
            msg += " " + Str(236);
            g_Client.msg.System(msg);
        }
        break;
    case 8: // 409 etat=5, gate=morph -> message str237 + retour ville de faction.
        g_Client.Var(kSpecialState + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;
    case 9: // 410 etat=0. Gate=disponibilite, resultat jete.
        g_Client.Var(kSpecialState + 4 * idx) = 0;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Famille de combo 4 (arme 4, Skill_GetComboMotionId(4, idx), sous-opcodes
// 755..763, EA 0x4a2159..0x4a293c) : MEME mecanique de 9 slots, avec deux
// divergences confirmees par lecture complete du desassemblage :
//   - AUCUN tableau d'horodatage journalier (dword_16862F0 = etat SEUL -- seule
//     famille de combo sans cooldown journalier aux slots 5/6/7/8, contrairement
//     aux familles 1/2/3 qui ecrivent toutes un horodatage la ou celle-ci n'en
//     ecrit aucun).
//   - slot4 (758) BEAUCOUP plus riche que les familles 1/2/3 : en plus d'armer
//     SON drapeau propre (dword_1675E90/E98) ET la barre de charge PARTAGEE
//     (dword_1675BA4/flt_1675BA8, comme familles 1/2/3), rejoue un 2e son
//     (flt_1491A3C), affiche une 2e ligne de chat "[demiduree]<str843>"
//     (dword_1675E9C=arg2/2) et reinitialise un bloc de 5 champs
//     (dword_1675E94/EA0[0]/EA4/EA8/EAC) -- forme proche du slot4 de la famille
//     "Special" (meme reset a 4/5 champs + halfDur + 2e message) mais AVEC en
//     plus le toucher de la barre partagee et le 2e son, absents chez Special.
//   - slot7 (761) NE PASSE PAS par Char_GetPairedElement : le payload transporte
//     un bloc brut de 80 o (copie INCONDITIONNELLE par le binaire vers le global
//     dword_1676054, EA 0x4a26b7) suivi d'un entier 4 chiffres decomposable en
//     jusqu'a 4 ids de classe (message par chiffre valide >=0, boucle
//     Str_GetClassLabel). Gate = Skill_IsAvailableByLevel -- DESORMAIS DISPONIBLE
//     (GetSkillLevelTable()) MAIS le bloc/decodage reste OMIS ICI : l'entier 4
//     chiffres est lu par le binaire a un offset payload AU-DELA du bloc brut de
//     80 o (donc au-dela de idx=payload+4/arg2=payload+8/tag13=payload+12, les
//     SEULS offsets confirmes surs et deja utilises par tout le reste de ce
//     fichier) -- non revalide bit-a-bit sur le desassemblage dans cette passe,
//     laisse TODO plutot que de risquer un decalage d'offset fabrique. Le bloc
//     brut de 80 o lui-meme N'A TOUJOURS AUCUN consommateur modelise dans ce
//     dispatcher (dword_1676054 n'est plus lu par la suite de la fonction) ->
//     non persiste ici (pas de sink cote ClientSource a ce jour), TODO precis.
// ---------------------------------------------------------------------------
constexpr uint32_t kFam4State = 0x16862F0;
constexpr uint32_t kFam4ChargeFlag1 = 0x1675E90, kFam4ChargeFlag0 = 0x1675E98, kFam4HalfDur = 0x1675E9C;
constexpr uint32_t kFam4ResetA = 0x1675E94, kFam4ResetB0 = 0x1675EA0,
                    kFam4ResetB1 = 0x1675EA4, kFam4ResetB2 = 0x1675EA8, kFam4ResetB3 = 0x1675EAC;

void ApplyComboFamily4Slot(int slot, uint32_t idx, uint32_t arg2) {
    const int  comboMotionId = Skill_GetComboMotionId(4, static_cast<int>(idx));
    const bool isCurrentMorph = (comboMotionId == g_Client.VarGet(kSelfMorphNpcId));

    switch (slot) {
    case 1: // 755 -- aucune ecriture d'etat. Gate=Skill_IsAvailableByLevel -> CABLE.
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " [" + std::to_string(arg2) + "]" + Str(231));
        }
        break;
    case 2: // 756 etat=1, ecrit inconditionnellement. Gate -> CABLE (popup filtre HORS PERIMETRE).
        g_Client.Var(kFam4State + 4 * idx) = 1;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(232));
        }
        break;
    case 3: // 757 etat=2, ecrit inconditionnellement. Gate -> CABLE.
        g_Client.Var(kFam4State + 4 * idx) = 2;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(233));
        }
        break;
    case 4: // 758 etat=3, ecrit inconditionnellement. Gate=morph (CALCULABLE) -> cable.
        g_Client.Var(kFam4State + 4 * idx) = 3;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(234));
            g_Client.Var(kFam4ChargeFlag1)  = 1;
            g_Client.Var(kFam4ChargeFlag0)  = 0;
            g_Client.Var(kChargeArmedTimer) = 1;   // barre partagee (dword_1675BA4)
            g_Client.VarF(kChargeElapsed)   = 0.0f;
            const int32_t halfDur = static_cast<int32_t>(arg2) / 2;
            g_Client.Var(kFam4HalfDur) = halfDur;
            g_Client.msg.System("[" + std::to_string(halfDur) + "]" + Str(843));
            g_Client.Var(kFam4ResetA)  = 1;
            g_Client.Var(kFam4ResetB0) = 0;
            g_Client.Var(kFam4ResetB1) = 0;
            g_Client.Var(kFam4ResetB2) = 0;
            g_Client.Var(kFam4ResetB3) = 0;
        }
        break;
    case 5: // 759 etat=5. AUCUN horodatage (asymetrie famille 4). Gate=disponibilite jete.
        g_Client.Var(kFam4State + 4 * idx) = 5;
        break;
    case 6: // 760 etat=5, gate=morph -> message str860 + retour ville de faction.
        g_Client.Var(kFam4State + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(860));
            WarpToOwnFactionTown();
        }
        break;
    case 7: // 761 etat=4, ecrit inconditionnellement. Gate=Skill_IsAvailableByLevel DISPONIBLE
            // (ComboSkillAvailable) mais TODO(offset) : le message decode un entier a un
            // offset payload non confirme dans cette passe (au-dela du bloc brut de 80 o),
            // cf. bandeau de tete -- non cable pour eviter une fabrication d'offset.
        g_Client.Var(kFam4State + 4 * idx) = 4;
        (void)arg2; // lu/copie par le binaire (payload+4) mais jamais reexploite hors du gate.
        break;
    case 8: // 762 etat=5, gate=morph -> message str237 + retour ville de faction.
        g_Client.Var(kFam4State + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;
    case 9: // 763 etat=0. Gate=disponibilite, resultat jete.
        g_Client.Var(kFam4State + 4 * idx) = 0;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Famille "Buff" (Skill_GetBuffMotionId, sous-opcodes 411..415, EA 0x49d1d9..0x49d565,
// etat dword_16860D0[idx]) -- CABLEE INTEGRALEMENT (lecture complete du desassemblage).
// C'est le bloc "different des familles mecaniques 1-9" repere lors de l'audit initial :
// mecanique PLUS SIMPLE (5 sous-cas, PAS 9) et surtout SANS AUCUN gate
// Skill_IsAvailableByLevel/Skill_IsSpecialUsable -- contrairement a TOUTES les familles
// de combo/Special ci-dessus, les messages "%s [%s] %s" sont ecrits INCONDITIONNELLEMENT
// (aucune branche de disponibilite avant le Msg_AppendSystemLine) :
//   - 411 (slot1, EA 0x49d1d9) : etat=1, message "<nom> [<tag>] <str1244>".
//   - 412 (slot2, EA 0x49d2a5) et 413 (slot3, EA 0x49d359) : CORPS STRICTEMENT IDENTIQUE
//     (etat=2, meme son flt_1498DBC, meme suffixe str1245) -- verifie par comparaison
//     instruction-a-instruction des deux blocs disassembles, confirme la fusion notee
//     dans l'audit initial ("412/413 fusionnes par le compilateur").
//   - 414 (slot4, EA 0x49d40d) : etat=2, message "<nom> [<tag>] <str1246>" (meme forme,
//     suffixe différent, PAS fusionne avec 412/413 -- son propre flt_1498CFC).
//   - 415 (slot5, EA 0x49d4c1) : etat=0 (reset), ECRIT INCONDITIONNELLEMENT. Seul sous-cas
//     avec un gate : motion == g_SelfMorphNpcId (CALCULABLE, meme echappatoire que le
//     reste du fichier) -> message "<nom> <str237>" + retour ville de faction (Map_
//     BeginWarpToFactionTown), EXACTEMENT la meme forme que le slot8 des familles de
//     combo/Special (str237 partage, cf. ApplyComboFamilySlot/ApplySpecialFamilySlot).
// Le tag entre crochets est le MEME champ brut 13 o (payload+12, non resolu via table --
// nom/etiquette brut) que le `tag13` deja lu par ApplyFamily3TagSlot (sous-opcode 25) :
// verifie sur le desassemblage (411/412/413/414 lisent tous une adresse LOCALE deja
// peuplee par le prologue partage de la mega-fonction, au meme rang que celle utilisee
// par le sous-opcode 25 -- meme pattern memcpy(13o) source, jamais retouche avant l'
// utilisation comme %s brut). Son (Snd3D_PlayScaledVolume, flt_1498C3C/1498DBC/1498CFC)
// HORS PERIMETRE audio, meme convention que le reste de ce fichier (aucun sous-systeme
// audio cable dans WorldEntityDispatch.cpp a ce jour, cf. Net/GameVarDispatch.cpp pour
// le stub Snd3D_PlayScaledVolume no-op utilise ailleurs dans ClientSource).
//
// IMPORTANT (verifie contre Game/GameState.h::ActiveBuff et UI/BuffStatusPanel.h) :
// dword_16860D0[idx] N'EST PAS une source de donnees pour `PlayerEntity::buffs`/
// `UI::BuffIconId`. Skill_GetBuffMotionId(idx) retourne un id de MOTION D'ANIMATION
// (241-330, cf. Game/SkillCombat.cpp) -- c'est un etat de "cast en cours" pour la barre
// d'annonce de sort (comme dword_1685EAC/1685F14/1685F44/16862F0/16860C0 pour les
// familles de combo/Special ci-dessus), PAS un catalogue de buffs actifs. `BuffIconId`
// (UI/BuffStatusPanel.h) est un catalogue 0..33 totalement disjoint, alimente par ~50
// variables de systemes non lies (combos elementaires, synergie de paire, rang de
// guilde, gemme d'arme, debuffs a duree dword_16758D8, etc. -- cf. GameState.h::
// ActiveBuff, commentaire de tete). Faire dword_16860D0[idx] -> ActiveBuff{id=idx} serait
// une FABRICATION (mapping non confirme par le desassemblage) -- non fait ici. La vraie
// source de PlayerEntity::buffs reste a reverser au cas par cas (cf. TODO GameState.h).
// ---------------------------------------------------------------------------
constexpr uint32_t kBuffState = 0x16860D0;

void ApplyBuffFamilySlot(int slot, uint32_t idx, const char* tag13) {
    const int buffMotionId = Skill_GetBuffMotionId(static_cast<int>(idx));

    switch (slot) {
    case 1: // 411 -- etat=1, message inconditionnel.
        g_Client.Var(kBuffState + 4 * idx) = 1;
        g_Client.msg.System(SkillName(buffMotionId) + " [" + tag13 + "] " + Str(1244));
        break;
    case 2: // 412
    case 3: // 413 -- corps identique a 412 (verifie desasm, cf. commentaire ci-dessus).
        g_Client.Var(kBuffState + 4 * idx) = 2;
        g_Client.msg.System(SkillName(buffMotionId) + " [" + tag13 + "] " + Str(1245));
        break;
    case 4: // 414 -- etat=2, suffixe distinct de 412/413.
        g_Client.Var(kBuffState + 4 * idx) = 2;
        g_Client.msg.System(SkillName(buffMotionId) + " [" + tag13 + "] " + Str(1246));
        break;
    case 5: // 415 -- etat=0 (reset), ecrit inconditionnellement. Gate=morph (CALCULABLE)
            // -> message + retour ville de faction (meme forme que le slot8 combo/Special).
        g_Client.Var(kBuffState + 4 * idx) = 0;
        if (buffMotionId == g_Client.VarGet(kSelfMorphNpcId)) {
            g_Client.msg.System(SkillName(buffMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Sous-opcodes 416/417 (EA 0x49d565/0x49d58d) -- SANS RAPPORT avec la famille "Buff"
// ci-dessus (tableau DIFFERENT, dword_1686120, PAS de Skill_GetBuffMotionId, aucun
// message). Adjacents dans le desassemblage et triviaux -> cables dans la meme passe :
// 416 met dword_1686120[idx]=1, 417 le remet a 0 (idx = payload+4, meme convention).
// Role exact de dword_1686120 non determine cette passe (candidat : flag "exclusion"
// generique similaire a dword_1686014 rencontre en sous-opcode 31/32, non confirme).
// ---------------------------------------------------------------------------
constexpr uint32_t kMiscFlag1686120 = 0x1686120;

void ApplyMiscFlagSlot(uint32_t idx, int32_t value) {
    g_Client.Var(kMiscFlag1686120 + 4 * idx) = value;
}

// ---------------------------------------------------------------------------
// Sous-opcodes 418..429 (dump L.3215-3364, mission "fin du bloc Buff" 2026-07-14) --
// AUDITES INTEGRALEMENT : PAS la mecanique table-driven des familles Buff/combo/
// Special ci-dessus (aucun Skill_GetBuffMotionId/GetComboMotionId/GetSpecialMotionId
// ici), verifie par lecture complete du desassemblage. Ce sont 6 notifications
// independantes (418, 419, 420, 421, le couple a corps identique 422/424) + un
// tail-merge partage entre 423 et 429 (LABEL_135 du desasm, corps
// instruction-a-instruction identique) + 4 sous-cas ENTIEREMENT gates par un
// buffer absent (425..428, cf. TODO plus bas) :
//   - 418 (WE_PlaySound_SysLine_418, EA 0x49d5b5, confirme par disasm direct) :
//     v654=payload+4 (compte, int SIGNE). Message "[compte]<str1402>" si
//     compte>0, sinon "<str1403>" seul (aucun argument). Son (flt_149947C/
//     flt_14993BC) HORS PERIMETRE audio (meme convention que le reste du fichier).
//     Pas d'etat ecrit, pas de gate.
//   - 419 (dump L.3231) : classe 0..3 = payload+4, PUIS TAG 13 o = payload+8 --
//     PAS +12 comme le tag13 "standard" du fichier (verifie : memcpy source =
//     v703, pas v704, cf. commentaire tete de ApplyWorldEntityDispatch) : MEME
//     offset/forme que ApplyClassTagFamily (671..677 -- classe -> str75-78 + tag
//     brut a payload+8). Message "[<classe>] [<tag>]<str1444>", couleur
//     g_SysMsgColor non modelisee (defaut, meme convention que le reste du
//     fichier). Pas d'etat, pas de gate.
//   - 420 (dump L.3262) : v654=payload+4 (compte). Message "[compte]<str1475>",
//     HUD flottant (categorie 3, type 1) + ligne systeme. Pas d'etat, pas de gate.
//   - 421 (dump L.3269) : arme dword_1686134=1 -- CONFIRME etre
//     WorldMap::flagZ291Variant (cf. World/WorldMap.h : 0->Z291_1.WM, sinon
//     Z291_2.WM) ; aucune instance WorldMap globale exposee aux handlers reseau
//     ici (meme limite d'injection que SkillLevelTable) -> ecriture brute via
//     Var(), la VRAIE consommation restant dans WorldMap::LoadZoneResource.
//     Message "<str1476>" (HUD cat.3/type2 + chat) inconditionnel ; SI
//     g_SelfMorphNpcId==291 (CALCULABLE) : arme un timer PROPRE
//     dword_1675CD0/flt_1675CD4 -- CONFIRME etre la ligne 28 de la table
//     Game/AnimationTick.cpp::kMorphRows (deja consommee par le moteur de
//     timers generique existant, aucun cablage supplementaire necessaire ici)
//     + son (flt_1499EFC, hors perimetre) + 2e ligne systeme "<str1477>".
//   - 422/424 (dump L.3285/3301) : CORPS IDENTIQUE (seul le suffixe str1478 vs
//     str1480 change) : dword_1686134=0 ecrit INCONDITIONNELLEMENT (precede le
//     reste, meme convention "ecriture d'etat avant gate" que tout le fichier).
//     Le message "[<nom>] <suffixe>" ET le warp conditionnel (SI morph==291 ET
//     <nom> != dword_16746A8) dependent tous deux de byte_1686138 -- un buffer
//     NOM (pas un entier), CONFIRME NON ECRIT nulle part dans cette fonction
//     (grep exhaustif du dump complet : byte_1686138 n'apparait qu'en LECTURE
//     ici) -- peuple par un tout autre handler reseau (cf. Game/NameplateLogic.h
//     commentaire de tete : "byte_1686138 en mode marche morph 291 ... ecrits
//     par des handlers reseau differents"). MEME limite que leaderName pour
//     631/632/637-638/642-643/647-648 (ApplyWarStageFamily) : omis (l'echappatoire
//     Var()/VarGet() ne modelise que des entiers/flottants 4 o, pas des buffers de
//     nom bruts arbitraires).
//   - 425..428 (dump L.3311-3354) : ENTIEREMENT gates par
//     `!Crt_Strcmp(byte_1686138, dword_16746A8)` -- AUCUNE ecriture d'etat
//     inconditionnelle avant ce gate (contrairement a 422/424 ou l'etat precede
//     le gate) -> RIEN de calculable avec les globals exposes ici (meme buffer
//     manquant que ci-dessus) -- PAS cables, cf. TODO en tete de fichier (meme
//     statut que 600/764-770 : aucun effet observable
//     reproductible sans fabrication). 426 ajoute un message/son supplementaire
//     (str1477, comme 421) SI EN PLUS morph==291, mais reste sous le MEME strcmp
//     externe -> egalement omis en bloc.
//     RE-VERIFIE (mission "425-428 + 500/901-903", 2026-07-14) : xrefs_to idaTs2
//     sur byte_1686138 ET byte_1686145 (les DEUX buffers 13o adjacents utilises
//     dans ces gates/ceux de 422/424 et de UI_ClanWarp_Commit/UI_ClanDisband_Commit)
//     couvre CETTE FOIS TOUT LE BINAIRE (pas seulement le dump de cette fonction) :
//     20 xrefs pour byte_1686138 (Net_OnWorldEntityDispatch x11, Motion_GetComboOffsetTable
//     x4 -- ces 4 sont elles-memes des LECTURES, meme gate `!Crt_Strcmp(dword_16746A8,
//     byte_1686138)` reutilise pour le case 291 de ce warp-selector --, Char_DrawNameplate,
//     Char_TickDeathRespawn, UI_ClanWarp_Commit, UI_ClanDisband_Commit, UI_FactionInfoWnd_Render)
//     et 6 xrefs pour byte_1686145 -- AUCUNE n'est une ecriture (confirme instruction par
//     instruction sur les 26 sites). Ce buffer (nom de chef de guilde/clan, cf.
//     Game/NameplateLogic.h) n'est donc ECRIT NULLE PART dans le binaire par une reference
//     directe au symbole -- soit il est peuple par un mecanisme d'ecriture indirecte que
//     l'analyse statique ne peut pas resoudre (pointeur calcule a l'execution vers cette
//     adresse fixe), soit il s'agit d'une fonctionnalite morte/jamais branchee cote client
//     d'origine. Dans les deux cas : AUCUNE base fiable pour cabler 425..428 sans fabrication.
//     Gate confirme INSURMONTABLE en statique -- reste NON CABLE.
//
//     RE-VERIFIE ENCORE (mission "deblocage 425-428", 2026-07-14) : la piste "ecriture
//     indirecte non resolue statiquement" ci-dessus est ELIMINEE -- le mecanisme EXACT de
//     la (fausse) piste "byte_1686138 ecrit" est desormais identifie noir sur blanc, ce qui
//     ferme definitivement la question :
//       - Crt_StringInit (0x75CAB0, alias interne du meme corps que Crt_Strcat 0x75CB00,
//         entree directe dans la boucle de copie a 0x75CB25 sans le scan-fin-de-dest
//         0x75CAE0-0x75CB22) a la signature reelle `char* Crt_StringInit(char* dest,
//         const char* src)` -- CONFIRME par lecture desasm : `dest`=1er argument (dernier
//         pushe avant le call, `[esp+8]`=`edi` au prologue), `src`=2e argument (premier
//         pushe, `[esp+0Ch]`=`ecx` au label loc_75CB25) ; le corps est un strcpy-like
//         dword-unrolled classique (copie octet/mot/dword jusqu'au NUL). CE N'EST PAS un
//         "constructeur std::string" au sens C++ malgre le nom hooke -- c'est un strcpy.
//       - Sur cette base, le SEUL site qui "ecrit" reellement byte_1686138 dans tout le
//         binaire est le case 422 (EA 0x496E37, `call Crt_StringInit` avec dest=byte_1686138,
//         src=byte_1686145) : `Crt_StringInit(&byte_1686138, &byte_1686145)` = COPIE le
//         contenu COURANT de byte_1686145 dans byte_1686138, PUIS byte_1686145 est
//         reinitialise a "" (chaine constante partagee `String` @0x7EC95F, confirmee vide
//         par read_cstring) via un 2e `Crt_StringInit(&byte_1686145, &String)` (EA 0x496E4E).
//         Le case 424 (EA 0x496F5A) NE FAIT PAS cette copie : il reinitialise directement
//         byte_1686145 a "" (0x496F69) SANS l'avoir prealablement recopie dans byte_1686138
//         -- asymetrie reelle entre 422 et 424 malgre le commentaire "CORPS IDENTIQUE" plus
//         haut (qui ne concerne que le suffixe de message/str1478 vs str1480, pas cette
//         etape de copie ; laisse tel quel ci-dessus par fidelite a l'annotation d'origine,
//         mais precision ajoutee ici pour eviter toute confusion future).
//       - CONSEQUENCE : byte_1686138 n'est donc PAS un buffer independant -- c'est une
//         COPIE DIFFEREE de byte_1686145 (uniquement via le case 422). Le vrai "buffer nom"
//         a tracer est byte_1686145. Or, EXHAUSTIVEMENT (les 6 xrefs deja enumerees
//         ci-dessus) : byte_1686145 n'est JAMAIS ecrit avec un contenu reel nulle part dans
//         le binaire -- il n'est QUE (a) lu comme source de la copie 422 ci-dessus, (b) remis
//         a "" par 422 et 424, (c) lu en comparaison strcmp dans Char_TickDeathRespawn,
//         UI_ClanWarp_Commit, UI_FactionInfoWnd_Render (tous confirmes lectures, pas
//         d'ecriture, cf. disasm instruction-par-instruction). Aucun memcpy/memmove ni
//         ecriture via pointeur calcule vers cette paire d'adresses fixes n'existe ailleurs
//         dans le binaire (verifie par data_refs exhaustif sur les deux symboles).
//       - PAS DE MODELE EQUIVALENT DEJA CABLE COTE CLIENTSOURCE NON PLUS : contrairement a
//         l'hypothese de depart de cette mission, aucune autre entite (guilde/alliance) ne
//         peut servir de source de substitution fidele -- Game/GameState.h::AllianceRoster
//         (g_LocalGuildName 0x168740C, g_AllianceRosterNames 0x16749B8) est un bloc memoire
//         COMPLETEMENT DISTINCT de byte_1686138/byte_1686145 (adresses tres eloignees, aucun
//         xref croise entre les deux blocs dans le desassemblage), et le commentaire de tete
//         de Game/NameplateLogic.h qui suggerait "ecrit par des handlers reseau differents"
//         se refere en realite au MEME site (le case 422 de CETTE fonction) -- il n'y a pas
//         de "handler different" cache ailleurs qui alimenterait byte_1686145.
//     CONCLUSION INCHANGEE mais desormais etablie avec certitude (pas seulement par absence
//     de preuve) : le binaire d'origine ne branche jamais de contenu reel dans ce buffer --
//     soit une regression/feature jamais completee cote serveur EU d'origine, soit un
//     vestige mort. 425..428 restent NON CABLES ; TODO honnete confirme, pas une limite de
//     l'analyse statique mais un fait du binaire.
//   - 423/429 (dump L.3296/3355, TAIL-MERGE LABEL_135 -- corps STRICTEMENT
//     identique, verifie instruction-a-instruction) : v654=payload+4. SI
//     g_SelfMorphNpcId==291 (CALCULABLE) -> message "[v654]<str1479>" (chat
//     seul, pas de HUD flottant). Pas d'etat.
// ---------------------------------------------------------------------------
constexpr uint32_t kZone291Variant    = 0x1686134; // WorldMap::flagZ291Variant (World/WorldMap.h)
constexpr uint32_t kZone291TimerFlag  = 0x1675CD0; // ligne 28 Game/AnimationTick.cpp::kMorphRows
constexpr uint32_t kZone291TimerFrame = 0x1675CD4;
constexpr int32_t  kZone291Morph      = 291;

void Apply418(int32_t count) {
    if (count <= 0) {
        g_Client.msg.System(Str(1403));
    } else {
        g_Client.msg.System("[" + std::to_string(count) + "]" + Str(1402));
    }
}

void Apply419(const uint8_t* payload, uint32_t len) {
    if (len < 21) return; // payload+8..+20 (13 o de tag) doivent etre disponibles
    uint32_t classBranch = 0;
    std::memcpy(&classBranch, payload + 4, 4);
    char tag[14] = {};
    std::memcpy(tag, payload + 8, 13);
    const std::string s = "[" + Str(75 + static_cast<int>(classBranch & 3)) + "] [" + tag + "]" + Str(1444);
    g_Client.msg.System(s);
}

void Apply420(int32_t count) {
    const std::string s = "[" + std::to_string(count) + "]" + Str(1475);
    g_Client.msg.Floating(3, 1, s);
    g_Client.msg.System(s);
}

void Apply421() {
    g_Client.Var(kZone291Variant) = 1;
    const std::string s = Str(1476);
    g_Client.msg.Floating(3, 2, s);
    g_Client.msg.System(s);
    if (g_Client.VarGet(kSelfMorphNpcId) == kZone291Morph) {
        g_Client.Var(kZone291TimerFlag)   = 1;
        g_Client.VarF(kZone291TimerFrame) = 0.0f;
        g_Client.msg.System(Str(1477));
    }
}

// 422/424 -- cf. commentaire de tete ci-dessus (buffer byte_1686138 non recu ici).
void Apply422Or424() {
    g_Client.Var(kZone291Variant) = 0;
}

// 423/429 -- tail-merge LABEL_135 du desasm (corps identique).
void ApplyZone291CountNotice(int32_t count) {
    if (g_Client.VarGet(kSelfMorphNpcId) != kZone291Morph) return;
    g_Client.msg.System("[" + std::to_string(count) + "]" + Str(1479));
}

// 301 -- réglages flottants/entiers divers.
constexpr uint32_t kMiscFloat21 = 0x1686070, kMiscFloat22 = 0x1686074, kMiscFloat23 = 0x1686078,
                    kMiscFloat24 = 0x168607C, kMiscFloat31 = 0x1686080, kMiscFloat32 = 0x1686084,
                    kMiscFloat33 = 0x1686088, kMiscFloat34 = 0x168608C, kMiscFloat41 = 0x1686090,
                    kMiscFloat42 = 0x1686094, kMiscFloat43 = 0x1686098, kMiscFloat44 = 0x168609C;
constexpr uint32_t kMiscInt51   = 0x16860A0, kMiscInt52   = 0x16860A4, kMiscInt53   = 0x16860A8,
                    kMiscInt54   = 0x16860AC;

void Apply301(uint32_t valueId, uint32_t value) {
    const float scaled = static_cast<float>(value) * 0.1f;
    switch (valueId) {
    case 21: g_Client.VarF(kMiscFloat21) = scaled; break;
    case 22: g_Client.VarF(kMiscFloat22) = scaled; break;
    case 23: g_Client.VarF(kMiscFloat23) = scaled; break;
    case 24: g_Client.VarF(kMiscFloat24) = scaled; break;
    case 31: g_Client.VarF(kMiscFloat31) = scaled; break;
    case 32: g_Client.VarF(kMiscFloat32) = scaled; break;
    case 33: g_Client.VarF(kMiscFloat33) = scaled; break;
    case 34: g_Client.VarF(kMiscFloat34) = scaled; break;
    case 41: g_Client.VarF(kMiscFloat41) = scaled; break;
    case 42: g_Client.VarF(kMiscFloat42) = scaled; break;
    case 43: g_Client.VarF(kMiscFloat43) = scaled; break;
    case 44: g_Client.VarF(kMiscFloat44) = scaled; break;
    case 51: g_Client.Var(kMiscInt51) = static_cast<int32_t>(value); break;
    case 52: g_Client.Var(kMiscInt52) = static_cast<int32_t>(value); break;
    case 53: g_Client.Var(kMiscInt53) = static_cast<int32_t>(value); break;
    case 54: g_Client.Var(kMiscInt54) = static_cast<int32_t>(value); break;
    default: return;
    }
}

void Apply302(uint32_t idx, int32_t value) {
    g_Client.Var(0x16860B0u + 4u * idx) = value;

    std::string text;
    switch (idx) {
    case 0: text = "[" + Str(75) + "]"; break;
    case 1: text = "[" + Str(76) + "]"; break;
    case 2: text = "[" + Str(77) + "]"; break;
    case 3: text = "[" + Str(78) + "]"; break;
    default: break;
    }
    switch (value) {
    case 0: text += " " + Str(934); break;
    case 1: text += " " + Str(930); break;
    case 2: text += " " + Str(931); break;
    case 3: text += " " + Str(932); break;
    case 4: text += " " + Str(933); break;
    default: break;
    }
    g_Client.msg.System(text);
}

// 500 -- bloc alliance/retour ville ; la partie observable est cablée.
void Apply500() {
    const int32_t count = 0; // v636 (valeur affichée)
    for (uint32_t k = 0; k < 4; ++k) {
        g_Client.Var(kPerElementCounterAddr + 8u * k) = count;
        g_Client.Var(kPerElementFlagAddr + 8u * k)    = 1;
    }

    const ElementPairTable pairs = Combat_ReadLocalElementPairs();
    const int32_t v633 = pairs.Paired(g_World.self.element);

    g_Client.Var(kElementPairAAddr) = -1;
    g_Client.Var(kElementPairBAddr) = -1;
    g_Client.Var(kElementPairCAddr) = -1;
    g_Client.Var(kElementPairDAddr) = -1;

    const std::string s = Str(1670);
    g_Client.msg.System(s);
    g_Client.msg.Floating(10, 2, s);

    if (g_Client.VarGet(kSelfMorphNpcId) == 37) {
        g_Client.Var(kAllySlot47ArmFlag)   = 1;
        g_Client.VarF(kAllySlot47ArmFrame) = 0.0f;
    }

    switch (v633) {
    case 0:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 1: case 2: case 3: case 4: case 16: case 17: case 18: case 40: case 43: case 46:
        case 56: case 59: case 62: case 63: case 64: case 76: case 80: case 91: case 95:
        case 202: case 206:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    case 1:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 6: case 7: case 8: case 9: case 22: case 23: case 24: case 41: case 44: case 47:
        case 57: case 60: case 65: case 66: case 67: case 77: case 81: case 92: case 96:
        case 203: case 207:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    case 2:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 11: case 12: case 13: case 14: case 28: case 29: case 30: case 42: case 45:
        case 48: case 58: case 61: case 68: case 69: case 70: case 78: case 83: case 93:
        case 97: case 204: case 208:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    case 3:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 79: case 83: case 94: case 98: case 140: case 141: case 142: case 143: case 205:
        case 209:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    const int32_t morph = g_Client.VarGet(kSelfMorphNpcId);
    if (morph > 145) {
        if (morph == 310) BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
    } else if (morph >= 144 || morph == 39 || morph == 74) {
        BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
    }
}

constexpr uint32_t kBuffMisc1 = 0x16862BC;
constexpr uint32_t kBuffMisc2 = 0x16862CC;

// 751 -- copie de structures bonus divers + notice classe/compétence localisée.
void Apply751(const uint8_t* payload, uint32_t len) {
    if (len < 68) return;
    uint32_t classId = 0, comboMotionId = 0;
    std::memcpy(&classId, payload + 8, 4);
    std::memcpy(&comboMotionId, payload + 12, 4);
    std::memcpy(reinterpret_cast<void*>(kBuffMisc1), payload + 16, 16);
    std::memcpy(reinterpret_cast<void*>(kBuffMisc2), payload + 32, 36);
    const std::string classLabel = Str(static_cast<int>(classId) + 1672);
    const std::string motionName = SkillName(static_cast<int>(comboMotionId));
    const std::string fmt = Str(2199);
    const std::string text = FormatString(fmt, classLabel.c_str(), motionName.c_str());
    g_Client.msg.Floating(2, 1, text);
    g_Client.msg.System(text);
}

// 792..794 -- notices item/monster avec suffixe localisé.
void Apply792to794(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (len < 12) return;
    int32_t comboMotionId = 0;
    uint32_t monsterId = 0;
    std::memcpy(&comboMotionId, payload + 4, 4);
    std::memcpy(&monsterId, payload + 8, 4);
    // CORRECTION off-by-one : le getter MONSTER 0x4C6570 est STRICTEMENT 1-based
    // (base+944*(id-1)) ; l'ancien `record(monsterId)` SANS -1 etait FAUX. GetMonsterInfo
    // applique le -1 et la garde 1er dword!=0.
    const MonsterInfo* info = GetMonsterInfo(monsterId);
    if (!info) return;

    std::string format;
    switch (subOpcode) {
    case 792: format = Str(2714); break;
    case 793: format = Str(2715); break;
    case 794: format = Str(2716); break;
    default: return;
    }

    const std::string suffix = FormatString(format, info->name); // info->name = record + 4 (char[25])
    const std::string text = SkillName(comboMotionId) + " " + suffix;
    g_Client.msg.System(text);
}

// ===========================================================================
// MISSION "reste de la fonction" (2026-07-14) -- sous-opcodes 500..903, fin du
// dispatcher. Source de verite : RE/dispatch_494870_full.c (dump Hex-Rays COMPLET
// de Net_OnWorldEntityDispatch, genere via ida_hexrays.decompile + ecriture fichier
// -- necessaire car le pseudocode depasse la taille rapatriable en un seul appel
// MCP decompile ; conserve dans RE/ pour tracabilite, numeros de ligne cites
// ci-dessous renvoient a CE fichier). EA precises NON re-citees individuellement
// par sous-cas dans cette section (map_pseudocode_line_to_eas s'est revele
// instable/lent sur une fonction de cette taille) -- la localisation fiable est
// le numero de ligne dans le dump, cf. commentaire de chaque bloc.
//
// Sous-opcodes 500 et 600 restent NON cables (TODO) : 500 (dump L.3365-3524) est
// une table de correspondance faction/alliance + ~80 id de morph codes en dur
// (guerre d'alliance, retour ville conditionnel) -- risque de transcription trop
// eleve pour la valeur (edge case) sans pouvoir re-tester en jeu ; 600 (dump
// L.3525-3538) ne fait que re-cabler Crt_StringInit() selon une plage de v644,
// sans effet observable modelisable (aucun message/etat ecrit) -- confirme no-op
// utile.
//
// RE-AUDITE (mission "425-428 + 500/901-903", 2026-07-14) : le refus de cabler 500
// est CONFIRME, avec une raison plus precise que "risque de transcription" seul --
// la structure meme du sous-cas est AMBIGUE statiquement, pas seulement volumineuse :
//   - 4 lectures brutes payload (v636 scalaire <- payload, PUIS v637/v638/v639 =
//     copies dans des BUFFERS, pas des scalaires -- alias probable avec le pattern
//     tag13 vu ailleurs dans ce fichier, mais offsets non confirmes ici) ;
//   - ecriture inconditionnelle sur g_PerElementCounter[2*k] et dword_1685E48[2*k]
//     pour k=0..3 (4 slots, PAS d'evidence qu'il s'agit du meme g_AlliancePairTable
//     que Game/SkillCombat.h::kElementPairAAddr/B/C/D -- CES DERNIERS vivent a
//     0x1685E64..70, alors que dword_1685E48 est une adresse DIFFERENTE, ~0x20
//     avant, donc un bloc DISTINCT malgre le nom voisin) ;
//   - la boucle de derivation de `v633` lit elle `g_AlliancePairTable[2*k+m]` --
//     CETTE FOIS le meme bloc que kElementPairAAddr..D (0x1685E64..70, confirme par
//     Game/SkillCombat.h) -- ET REMET A -1 g_AlliancePairTable[0]/dword_1685E68[0]/
//     dword_1685E6C/dword_1685E70 (donc 1685E64/68/6C/70 -- les 4 memes adresses,
//     mais nommees DIFFEREMMENT par Hex-Rays selon le site d'acces, signe que la
//     frontiere entre "g_AlliancePairTable" et "dword_1685E68" telle que vue par le
//     desassembleur est un artefact de typage local, PAS une preuve de 2 tableaux
//     distincts -- lever cette ambiguite demanderait une verification runtime
//     (x32dbg) hors de la portee de cette passe statique).
// Conclusion : cablable EN PRINCIPE (toute l'info est dans le dump, aucun buffer
// absent comme pour 425..428), mais seulement apres une passe dediee qui (a) fixe
// les offsets payload exacts de v637/v638/v639 et (b) confirme via x32dbg si
// g_PerElementCounter/dword_1685E48 sont bien un bloc a part de g_AlliancePairTable
// -- meme subsystem que la famille 46/47 "appariement ALLIANCE" (33..115, cf.
// WorldEntityDispatch.h), donc a traiter dans LA MEME vague dediee que ce sous-
// systeme plutot qu'isolement ici. RESTE NON CABLE.
// ===========================================================================

// ---------------------------------------------------------------------------
// Famille "bonus de niveau / canal" (dump L.3539-3799, sous-opcodes 601..610,
// etat dword_1686058[canal] ou canal in {0,1,2}). Gate de canal COMMUN a
// 601/602/609/610 : canal 0 -> g_SelfLevel in [100,112] ; canal 1 -> g_SelfLevel
// in [113,145] ET !g_SelfLevelBonus ; canal 2 -> g_SelfLevelBonus>0. Le morph cible
// du canal = canal+297 (dword_1675CD8 dans 601, compare directement ailleurs).
// TODO precis omis (memes conventions que le reste du fichier) : popup
// Skill_CheckBuffState/g_Opt_FilterWorldEntity (601), Game_GetTierValue (608),
// UI_Macro_ClearGrid (604/605/608), Snd3D_PlayScaledVolume (603, son seul).
// ---------------------------------------------------------------------------
constexpr uint32_t kBonusChanState  = 0x1686058;  // dword_1686058[canal]
constexpr uint32_t kBonusChanFlag2  = 0x1686064;  // dword_1686064[canal]
constexpr uint32_t kBonusSelMorph   = 0x1675CD8;  // dword_1675CD8 = canal+297
constexpr uint32_t kBonusResetCDC = 0x1675CDC, kBonusResetCE0 = 0x1675CE0,
                    kBonusResetCE4 = 0x1675CE4, kBonusResetCE8 = 0x1675CE8;
constexpr uint32_t kBonusAckArr     = 0x1675CEC;  // [classe] (stride 4, 0..3)
constexpr uint32_t kBonusD1C        = 0x1675D1C;
constexpr uint32_t kBonusResetArr   = 0x1675D20;  // [classe] (stride 4, 0..3)

bool BonusChannelGatePasses(uint32_t chan) {
    if (chan == 0) return g_World.self.level >= 100 && g_World.self.level <= 112;
    if (chan == 1) return g_World.self.level >= 113 && g_World.self.level <= 145 && !g_World.self.levelBonus;
    if (chan == 2) return g_World.self.levelBonus > 0;
    return false;
}

void ResetBonusBlockA() { // dword_1675CDC/CE0/CE4/CE8 + kBonusAckArr[0..3] = 0
    g_Client.Var(kBonusResetCDC) = 0; g_Client.Var(kBonusResetCE0) = 0;
    g_Client.Var(kBonusResetCE4) = 0; g_Client.Var(kBonusResetCE8) = 0;
    for (int c = 0; c < 4; ++c) g_Client.Var(kBonusAckArr + 4 * c) = 0;
}

void ApplyLevelBonusFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t chan = 0, arg2 = 0;
    if (len >= 8)  std::memcpy(&chan, payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    const bool onMorph = (static_cast<int32_t>(chan) + 297 == g_Client.VarGet(kSelfMorphNpcId));

    switch (subOpcode) {
    case 601: // dump L.3559
        g_Client.Var(kBonusChanState + 4 * chan) = 0;
        g_Client.Var(kBonusChanFlag2 + 4 * chan) = 1;
        if (!BonusChannelGatePasses(chan)) return;
        g_Client.Var(kBonusSelMorph) = static_cast<int32_t>(chan) + 297;
        g_Client.msg.System(SkillName(static_cast<int32_t>(chan) + 297) + " " + Str(232));
        // TODO(gate) : popup Str(1757) si !g_Opt_FilterWorldEntity/Skill_CheckBuffState -- non modelise ici.
        return;
    case 602: // dump L.3572
        g_Client.Var(kBonusChanState + 4 * chan) = 1;
        g_Client.Var(kBonusChanFlag2 + 4 * chan) = 0;
        if (!BonusChannelGatePasses(chan)) return;
        g_Client.msg.System(Str(1838));
        if (onMorph) g_Client.msg.System("[" + std::to_string(arg2) + "]" + Str(1839));
        return;
    case 603: // dump L.3603
        g_Client.Var(kBonusChanState + 4 * chan) = 2;
        if (onMorph) {
            g_Client.msg.System(Str(1764));
            g_Client.Var(kBonusResetCDC) = 1; g_Client.Var(kBonusResetCE0) = 1;
            g_Client.Var(kBonusResetCE4) = 1; g_Client.Var(kBonusResetCE8) = 1;
            g_Client.Var(kBonusD1C) = 1;
            for (int c = 0; c < 4; ++c) g_Client.Var(kBonusResetArr + 4 * c) = 0;
        }
        return;
    case 604: // dump L.3623 -- TODO(UI_Macro_ClearGrid) non modelise (UI externe).
        g_Client.Var(kBonusChanState + 4 * chan) = 5;
        if (onMorph) ResetBonusBlockA();
        return;
    case 605: // dump L.3639 -- TODO(UI_Macro_ClearGrid).
        g_Client.Var(kBonusChanState + 4 * chan) = 5;
        if (onMorph) {
            g_Client.msg.System(Str(235));
            WarpToOwnFactionTown();
            ResetBonusBlockA();
        }
        return;
    case 606: { // dump L.3660 -- meme table de messages/logique "ack ou warp" que le cas 666
                // de ApplyArenaFamily plus bas (audit dispatch 2026-07-14) : PAS factorise
                // (adresses d'etat cibles differentes -- kBonusAckArr/kBonusResetArr ici vs
                // kArenaBlockB_D40/kArenaD74..D80 la-bas -- chaque site verifie independamment
                // contre son propre EA ; regrouper risquerait de brouiller la tracabilite
                // desasm sans reduction de risque reelle, cf. instruction d'audit).
        g_Client.Var(kBonusChanState + 4 * chan) = 3;
        if (!onMorph) return;
        if (arg2 > 3) return; // v693 hors 0..3 -> default: return (fidele)
        static constexpr int kMsg[4] = {1758, 1759, 1760, 1761};
        g_Client.msg.System(Str(kMsg[arg2]));
        if (static_cast<uint32_t>(g_World.self.element) == arg2)
            g_Client.Var(kBonusAckArr + 4 * arg2) = 1;
        else
            WarpToOwnFactionTown();
        for (int c = 0; c < 4; ++c) g_Client.Var(kBonusResetArr + 4 * c) = 0;
        return;
    }
    case 607: // dump L.3698
        g_Client.Var(kBonusChanState + 4 * chan) = 4;
        if (onMorph) g_Client.Var(kBonusD1C) = 0;
        return;
    case 608: // dump L.3704 -- TODO(UI_Macro_ClearGrid, Game_GetTierValue) non modelises.
        g_Client.Var(kBonusChanState + 4 * chan) = 5;
        if (onMorph) {
            g_Client.msg.System(Str(1844));
            WarpToOwnFactionTown();
            ResetBonusBlockA();
        }
        return;
    case 609: // dump L.3728
        g_Client.Var(kBonusChanState + 4 * chan) = 0;
        if (BonusChannelGatePasses(chan)) {
            static constexpr int kMsg[3] = {1762, 1846, 1847};
            const std::string s = Str(chan <= 2 ? kMsg[chan] : 1762);
            g_Client.msg.System(s);
            g_Client.msg.Floating(0, 0, s);
        }
        if (onMorph) {
            ResetBonusBlockA();
            WarpToOwnFactionTown();
        }
        return;
    case 610: // dump L.3776 -- format-table Str(1843) applique a `arg2` dans le binaire (le
              // %s de la table masque en realite un %d) ; approxime en prefixe "[n]" (meme
              // convention que les autres "format-depuis-table" de cette passe, cf. 661/668).
        if (BonusChannelGatePasses(chan))
            g_Client.msg.System("[" + std::to_string(arg2) + "]" + Str(1843));
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Famille "notification d'element" (dump L.3800-3850, sous-opcodes 611..615) --
// meme forme que les slots simples deja cables : idx=element (payload+4), gate =
// element==g_World.self.element, message optionnel avec un compte (payload+8).
// ---------------------------------------------------------------------------
void ApplyElementNotifyFamily(uint32_t subOpcode, uint32_t elt, uint32_t count) {
    if (static_cast<int32_t>(elt) != g_World.self.element) return;
    std::string s;
    switch (subOpcode) {
    case 611: s = std::to_string(count) + " " + Str(1859); break;
    case 612: s = Str(1860); break;
    case 613: s = Str(1862); break;
    case 614: s = Str(1861); break;
    case 615: s = std::to_string(count) + " " + Str(1944); break;
    default: return;
    }
    g_Client.msg.Floating(0, 0, s);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// Notifications autonomes 620..628 (dump L.3852-3902), sans rapport mecanique
// entre elles (juste adjacentes dans le desassemblage) -- cablees individuellement.
// ---------------------------------------------------------------------------
constexpr uint32_t kWarDeclaredClass = 0x1686188; // dword_1686188 (624/626)
constexpr uint32_t kZoneResetArr622  = 0x1675DB8; // [0..3] stride 4 (622)

void Apply620(uint32_t count) { g_Client.msg.System(std::to_string(count) + Str(1937)); }
void Apply621() { g_Client.msg.System(Str(1938)); }
void Apply622() { // dump L.3862 -- TODO(World_LoadCurrentZoneModel) non modelise (rechargement 3D).
    g_Client.msg.System(Str(1936));
    for (int c = 0; c < 4; ++c) g_Client.Var(kZoneResetArr622 + 4 * c) = 0;
}
void Apply624(uint32_t classId) {
    g_Client.Var(kWarDeclaredClass) = static_cast<int32_t>(classId);
    g_Client.msg.System(Str(75 + (classId & 3)) + Str(1929));
}
void Apply625(uint32_t count) { g_Client.msg.System(std::to_string(count) + Str(1935)); }
void Apply626() {
    g_Client.Var(kWarDeclaredClass) = -1;
    if (g_Client.VarGet(kSelfMorphNpcId) == 88) {
        g_Client.msg.System(Str(235));
        WarpToOwnFactionTown();
    }
}
void Apply628(const uint8_t* payload, uint32_t len) {
    if (len < 16) return;
    int32_t comboMotionId = 0, classId = 0;
    std::memcpy(&comboMotionId, payload + 8, 4);
    std::memcpy(&classId, payload + 12, 4);
    const std::string s = SkillName(comboMotionId) + Str(classId + 2019);
    g_Client.msg.Floating(2, 1, s);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// Buffers de nom (13 o) écrits par ce module — helpers `strcpy(dest, src)` fidèles.
// FAUX AMI LEVÉ (vague W11) : `Crt_StringInit 0x75CAB0` n'est PAS un constructeur de
// std::string (ce que dit son commentaire IDA), c'est `strcpy(dest, src)` : entrée
// alternative de `Crt_Strcat 0x75CAC0` sautant le scan de fin (0x75CAB1 `mov edi,
// [esp+4+arg_0]` = dest tel quel ; corps commun 0x75CB25 `mov ecx,[esp+4+arg_4]` = src).
// `offset String` 0x7EC95F a son octet 0 à 0 -> chaîne vide -> « strcpy(dest, String) » = clear.
// ÉCART ASSUMÉ : strcpy laisse la QUEUE de dest intacte ; on zero-fill jusqu'à 13. Inobservable
// (tous les lecteurs sont strcmp/%s, arrêt au 1er NUL) et NUL-terminant, cf. l'idiome établi
// GameHandlers_ChatSocial.cpp:294-299.
//
// DEUX STORES DISTINCTS selon la nature de la cible :
//   • globals AUTONOMES de la longue traîne (T1/T2 de guerre, 0x16746A8/0x16746BC) -> Blob(13).
//     ⚠️ `Blob` fige la taille au 1er appelant (ClientRuntime.h:179) : ces clés doivent être
//     ouvertes à 13 PARTOUT (0x16746A8 l'est déjà par UI/ClanContextMenu.cpp:92 `BlobNonEmpty
//     -> Blob(addr,13)` ; l'ouvrir à 16 déborderait le tas). Slot binaire = 16 o mais champ FIL
//     = 13 (strides T1/T2 de 13, et byte_1686145-byte_1686138 = 13).
//   • champs de la FICHE d'entité de self (0x168725C = entity[0]+40 = body+16 ; 0x1687270 =
//     entity[0]+60 = body+36) : leur home MODÉLISÉ est g_World.players[0].body (index 0 = self,
//     GameState.h:122). body+16 a des lecteurs VIVANTS (Scene/WorldRenderer.cpp:803 affiliation,
//     World/TerrainPicker.cpp:280) -> écrire ailleurs qu'au body créerait un store fantôme.
// ---------------------------------------------------------------------------
void BlobStrcpy13(uint32_t addr, const char* src) {
    auto& b = g_Client.Blob(addr, 13);
    size_t n = 0;
    while (n < 13 && src[n] != 0) ++n;   // strcpy : arrêt au 1er NUL
    b.assign(13, 0);
    std::memcpy(b.data(), src, n);
}
void SelfBodyStrcpy13(size_t bodyOffset, const char* src) {
    auto& players = g_World.players;
    if (players.empty()) return;         // pas de self fantôme (cf. App/App.cpp:770/1161)
    auto& body = players[0].body;
    if (bodyOffset + 13 > body.size()) return;
    size_t n = 0;
    while (n < 13 && src[n] != 0) ++n;
    std::memset(body.data() + bodyOffset, 0, 13);
    std::memcpy(body.data() + bodyOffset, src, n);
}
// Identité d'affiliation de self (miroir EXACT : 0x16746A8/+16/+20 == entity+40/+56/+60).
constexpr uint32_t kLocalAffilName  = 0x16746A8; // dword_16746A8 (= UI/ClanContextMenu::kVarGuildTag)
constexpr uint32_t kLocalAffilName2 = 0x16746BC; // unk_16746BC
constexpr size_t   kSelfBodyAffil   = 40 - 24;   // byte_168725C (= WorldRenderer::kNpBodyAffiliation)
constexpr size_t   kSelfBodyAffil2  = 60 - 24;   // unk_1687270

// ---------------------------------------------------------------------------
// Famille "declaration de guerre / palier" (dump L.3903-4228, sous-opcodes
// 629..652) : idx=element 0..3 (payload+4), gate elt<4. dword_168618C[elt] = palier
// (0..14). Chaque palier ecrit son etat inconditionnellement puis, si
// elt==g_World.self.element (paliers "annonce") ou si le morph courant correspond a
// la ville de siege de l'element (kSiegeTownNpc[elt] = {138,139,165,166}, table
// v682 du desasm, DISTINCTE de MapWarp::FactionTownNpcId), affiche le message et/ou
// arme un drapeau simple.
//
// DEUX NOMS DE 13 o sont PORTÉS par le payload (rectification WARP-01/02, vague W11 —
// l'ancien bandeau « non recus dans CE paquet / donnees indisponibles » etait FACTUELLEMENT
// FAUX) : name1 = payload+8, name2 = payload+21 (derivation du cadre : Crt_Memcpy(&var_F8,
// recvBuf+5, 0x64) @0x4948a5 ; recvBuf+5 = payload+4 ; var_F4@+8, var_E7@+21). Deux tables
// de noms distinctes (bornees des deux cotes par des voisins nommes) :
//   T1 = unk_168619C : 4 elt x 52 o = 4 emplacements de 13 (slot_i = +13*i) ; borne
//        haute = unk_168626C (0x168619C + 208).
//   T2 = unk_168626C : 4 elt x 13 o ; borne haute = dword_16862A0 (0x168626C + 52, EXACT).
// Repartition PROUVEE instruction-a-instruction (imul 34h pour T1, imul 0Dh pour T2, gate
// elt<4 a chaque case, ecriture AVANT le test onSelfElt) :
//   629 -> T2[elt] <- name1 (0x49e9c5)
//   635 -> T1.slot3[elt] <- name1 (0x49ef36) ; T2[elt] <- name2 (0x49ef52)
//   640 -> T1.slot2[elt] <- name1 (0x49f2a7) ; T1.slot3[elt] <- name2 (0x49f2c3)
//   645 -> T1.slot1[elt] <- name1 (0x49f600) ; T1.slot2[elt] <- name2 (0x49f61b)
//   650 -> T1.slot0[elt] <- name1 (0x49f94e) ; T1.slot1[elt] <- name2 (0x49f96a)
//   652 -> T2[elt] <- "" (clear, 0x49fa98, precede de Map_BeginWarpToFactionTown @0x49fa6d)
// Les paires (slot_k, slot_k+1) CHEVAUCHENT d'une case a l'autre : le motif est regulier
// et indexe par palier, mais ce n'est PAS une « echelle » monotone -> aucun nom conceptuel
// invente ici, on decrit les faits (palier -> couple de slots).
// LECTEURS (NON portes -> WARP-04, hors de ce fichier) : Warp_ProcessKeyword 0x5F54E0 /
//   Warp_LookupDest 0x5F5B60 (strcmp T1.slot3/T2 vs dword_16746A8) ; 0x49f4ae strcmp
//   T1.slot3[elt] vs g_LocalClanName sous gate kSiegeTownNpc -> Map_BeginWarpToFactionTown.
// TODO ancre (non modelise, hors perimetre W11) : recalcul jauge d'attaque
//   (Char_CalcAttackRatingMin/Max sur g_EquipSnapshotScratch) sur 635/640/645/650 ;
//   UI_RemoveActiveBuffSlot() sur 650 ; 2e message Str(2037)+nom sur 631/632.
// ---------------------------------------------------------------------------
constexpr uint32_t kWarStage        = 0x168618C; // dword_168618C[elt]
constexpr uint32_t kWarT1           = 0x168619C; // unk_168619C : 4 elt x 52 o, slot_i = +13*i
constexpr uint32_t kWarT2           = 0x168626C; // unk_168626C : 4 elt x 13 o (borne dword_16862A0)
constexpr int32_t  kSiegeTownNpc[4] = {138, 139, 165, 166}; // v682
constexpr uint32_t kWarFlag1675DD8  = 0x1675DD8; // 634
constexpr uint32_t kWarFlag1675DDC  = 0x1675DDC; // 639
constexpr uint32_t kWarFlag1675DE0  = 0x1675DE0; // 644
constexpr uint32_t kWarFlag1675DE4  = 0x1675DE4; // 649
constexpr uint32_t kWarFloatReset   = 0x1675DE8; // [0..3] stride 4 (652)

void ApplyWarStageFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t elt = 0, arg2 = 0;
    if (len >= 8)  std::memcpy(&elt,  payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    if (elt >= 4) return;
    const bool onSiegeMorph = (g_Client.VarGet(kSelfMorphNpcId) == kSiegeTownNpc[elt]);
    const bool onSelfElt    = (static_cast<int32_t>(elt) == g_World.self.element);
    // Les deux noms 13 o portes par les cases 629/635/640/645/650 (arg2 CHEVAUCHE name1 sur
    // payload+8, exactement comme le binaire : les cases "annonce" 630/631/... lisent le meme
    // offset comme un entier). NUL-borne defensif (paquet reel = 105 o -> les deux presents).
    char name1[14] = {}, name2[14] = {};
    if (len >= 21) std::memcpy(name1, payload + 8,  13);
    if (len >= 34) std::memcpy(name2, payload + 21, 13);
    // Cle d'un emplacement de T1 (slot 0..3 de l'element courant) / de T2.
    const uint32_t t1slot0 = kWarT1 + 52u * elt;
    const uint32_t t2key   = kWarT2 + 13u * elt;

    switch (subOpcode) {
    case 629: { // dump L.3903 -- T2[elt] <- name1 (0x49e9c5), PUIS message si onSelfElt.
        BlobStrcpy13(t2key, name1);                    // 0x49e9c5 (ecrit AVANT le test onSelfElt)
        if (onSelfElt) g_Client.msg.System(std::string(name1) + Str(1986));
        return;
    }
    case 630:
        g_Client.Var(kWarStage + 4 * elt) = 1;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1984);
            AnnounceFloating10(s);
        }
        if (onSiegeMorph) WarpToOwnFactionTown();
        return;
    case 631: // TODO(leaderName) : 2e message Str(2037)+nom omis (buffer non recu ici).
        g_Client.Var(kWarStage + 4 * elt) = 2;
        if (onSelfElt) {
            g_Client.msg.System(Str(1985));
            const std::string s = std::to_string(arg2) + Str(1988);
            AnnounceFloating10(s);
        }
        return;
    case 632: // TODO(leaderName) idem 631.
        g_Client.Var(kWarStage + 4 * elt) = 2;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1988);
            AnnounceFloating10(s);
        }
        return;
    case 633:
        g_Client.Var(kWarStage + 4 * elt) = 3;
        if (onSelfElt) g_Client.msg.System(Str(1987));
        return;
    case 634:
        g_Client.Var(kWarStage + 4 * elt) = 4;
        if (onSelfElt) {
            const std::string s = Str(1989);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DD8) = 1;
        }
        return;
    case 635: // TODO(Char_CalcAttackRatingMin/Max) non modelise -- noms + palier.
        BlobStrcpy13(t1slot0 + 13u * 3, name1);        // 0x49ef36 : T1.slot3 <- name1
        BlobStrcpy13(t2key,             name2);        // 0x49ef52 : T2[elt] <- name2
        g_Client.Var(kWarStage + 4 * elt) = 5;
        return;
    case 636:
        g_Client.Var(kWarStage + 4 * elt) = 5;
        return;
    case 637: case 638: // TODO(leaderName + warp conditionne par strcmp guilde) omis.
        g_Client.Var(kWarStage + 4 * elt) = 6;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1990);
            AnnounceFloating10(s);
        }
        return;
    case 639:
        g_Client.Var(kWarStage + 4 * elt) = 7;
        if (onSelfElt) {
            const std::string s = Str(1991);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DDC) = 1;
        }
        return;
    case 640: // TODO(Char_CalcAttackRatingMin) -- noms + palier.
        BlobStrcpy13(t1slot0 + 13u * 2, name1);        // 0x49f2a7 : T1.slot2 <- name1
        BlobStrcpy13(t1slot0 + 13u * 3, name2);        // 0x49f2c3 : T1.slot3 <- name2
        g_Client.Var(kWarStage + 4 * elt) = 8;
        return;
    case 641:
        g_Client.Var(kWarStage + 4 * elt) = 8;
        return;
    case 642: case 643: // TODO(leaderName + warp conditionne) omis.
        g_Client.Var(kWarStage + 4 * elt) = 9;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1992);
            AnnounceFloating10(s);
        }
        return;
    case 644:
        g_Client.Var(kWarStage + 4 * elt) = 10;
        if (onSelfElt) {
            const std::string s = Str(1993);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DE0) = 1;
        }
        return;
    case 645: // TODO(Char_CalcAttackRatingMin) -- noms + palier.
        BlobStrcpy13(t1slot0 + 13u * 1, name1);        // 0x49f600 : T1.slot1 <- name1
        BlobStrcpy13(t1slot0 + 13u * 2, name2);        // 0x49f61b : T1.slot2 <- name2
        g_Client.Var(kWarStage + 4 * elt) = 11;
        return;
    case 646:
        g_Client.Var(kWarStage + 4 * elt) = 11;
        return;
    case 647: case 648: // TODO(leaderName + warp conditionne) omis.
        g_Client.Var(kWarStage + 4 * elt) = 12;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1994);
            AnnounceFloating10(s);
        }
        return;
    case 649:
        g_Client.Var(kWarStage + 4 * elt) = 13;
        if (onSelfElt) {
            const std::string s = Str(1995);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DE4) = 1;
        }
        return;
    case 650: // TODO(UI_RemoveActiveBuffSlot) -- noms + palier.
        BlobStrcpy13(t1slot0 + 13u * 0, name1);        // 0x49f94e : T1.slot0 <- name1
        BlobStrcpy13(t1slot0 + 13u * 1, name2);        // 0x49f96a : T1.slot1 <- name2
        g_Client.Var(kWarStage + 4 * elt) = 14;
        return;
    case 651:
        g_Client.Var(kWarStage + 4 * elt) = 14;
        return;
    case 652:
        if (onSiegeMorph) {
            for (int c = 0; c < 4; ++c) g_Client.VarF(kWarFloatReset + 4 * c) = 0.0f;
            WarpToOwnFactionTown();                     // Map_BeginWarpToFactionTown @0x49fa6d
        }
        g_Client.Var(kWarStage + 4 * elt) = 0;         // dword_168618C[elt]=0 @0x49fa78
        BlobStrcpy13(t2key, "");                        // 0x49fa98 : T2[elt] <- "" (clear)
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Famille "arene" (morph fixe 200, dump L.4229-4380, sous-opcodes 659..669),
// etat dword_16862A0 (SANS index -- global unique, pas par element). Gate morph =
// g_SelfMorphNpcId==200 (CALCULABLE) pour tous les sous-cas sauf 659/661/662 (etat
// seul, message inconditionnel).
// ---------------------------------------------------------------------------
constexpr uint32_t kArenaState = 0x16862A0;
constexpr int32_t  kArenaMorph = 200;
constexpr uint32_t kArenaBlockB_D30 = 0x1675D30, kArenaBlockB_D34 = 0x1675D34,
                    kArenaBlockB_D38 = 0x1675D38, kArenaBlockB_D3C = 0x1675D3C;
constexpr uint32_t kArenaBlockB_D40 = 0x1675D40; // [classe] stride4 0..3
constexpr uint32_t kArenaD70 = 0x1675D70;
constexpr uint32_t kArenaD74 = 0x1675D74, kArenaD78 = 0x1675D78,
                    kArenaD7C = 0x1675D7C, kArenaD80 = 0x1675D80;

// Reset du bloc B (dword_1675D30/34/38/3C + kArenaBlockB_D40[0..3]) -- CORPS IDENTIQUE
// reproduit deux fois dans le desassemblage (cas 664/665 ET 669, cf. audit dispatch
// 2026-07-14) -- factorise ici, AUCUN changement de comportement.
void ResetArenaBlockB() {
    g_Client.Var(kArenaBlockB_D30) = 0; g_Client.Var(kArenaBlockB_D34) = 0;
    g_Client.Var(kArenaBlockB_D38) = 0; g_Client.Var(kArenaBlockB_D3C) = 0;
    for (int c = 0; c < 4; ++c) g_Client.Var(kArenaBlockB_D40 + 4 * c) = 0;
}

void ApplyArenaFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t v = 0;
    if (len >= 8) std::memcpy(&v, payload + 4, 4);
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kArenaMorph);

    switch (subOpcode) {
    case 659:
        g_Client.Var(kArenaState) = 1;
        g_Client.msg.System(std::to_string(v) + Str(1939));
        return;
    case 660:
        g_Client.Var(kArenaState) = 2;
        g_Client.msg.System(Str(1940));
        // TODO(gate) : popup Str(2060/2061) si g_SelfLevelBonus==12 && g_Opt_FilterWorldEntity -- omis.
        return;
    case 661: // format-table Str(1941) applique a v676 -- approxime en prefixe "[n]" (cf. 610).
        g_Client.Var(kArenaState) = 2;
        g_Client.msg.System("[" + std::to_string(v) + "]" + Str(1941));
        return;
    case 662:
        g_Client.Var(kArenaState) = 3;
        g_Client.msg.System(Str(1942));
        return;
    case 663:
        g_Client.Var(kArenaState) = 4;
        if (onMorph) {
            g_Client.msg.System(Str(1764));
            g_Client.Var(kArenaBlockB_D30) = 1; g_Client.Var(kArenaBlockB_D34) = 1;
            g_Client.Var(kArenaBlockB_D38) = 1; g_Client.Var(kArenaBlockB_D3C) = 1;
            g_Client.Var(kArenaD70) = 1;
            g_Client.Var(kArenaD74) = 0; g_Client.Var(kArenaD78) = 0;
            g_Client.Var(kArenaD7C) = 0; g_Client.Var(kArenaD80) = 0;
        }
        return;
    case 664:
    case 665:
        g_Client.Var(kArenaState) = 7;
        if (onMorph) {
            ResetArenaBlockB();
            if (subOpcode == 665) WarpToOwnFactionTown();
        }
        return;
    case 666: { // meme table de messages/logique "ack ou warp" que le cas 606 de
                // ApplyLevelBonusFamily plus haut (audit dispatch 2026-07-14) -- PAS factorise,
                // cf. commentaire de tete du cas 606 pour la raison (adresses d'etat differentes).
        g_Client.Var(kArenaState) = 5;
        if (!onMorph || v > 3) return;
        static constexpr int kMsg[4] = {1758, 1759, 1760, 1761};
        g_Client.msg.System(Str(kMsg[v]));
        if (static_cast<uint32_t>(g_World.self.element) == v)
            g_Client.Var(kArenaBlockB_D40 + 4 * v) = 1;
        else
            WarpToOwnFactionTown();
        g_Client.Var(kArenaD74) = 0; g_Client.Var(kArenaD78) = 0;
        g_Client.Var(kArenaD7C) = 0; g_Client.Var(kArenaD80) = 0;
        return;
    }
    case 667:
        g_Client.Var(kArenaState) = 6;
        if (onMorph) g_Client.Var(kArenaD70) = 0;
        return;
    case 668:
        g_Client.Var(kArenaState) = 7;
        if (onMorph) {
            g_Client.msg.System(Str(1844));
            g_Client.msg.System("[100]" + Str(1845)); // valeur 100 codee en dur dans le binaire.
        }
        return;
    case 669:
        g_Client.Var(kArenaState) = 0;
        if (onMorph) {
            ResetArenaBlockB();
            WarpToOwnFactionTown();
        }
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Famille "annonce classe+tag" (dump L.4380-4480, sous-opcodes 671..677) : classe
// 0..3 (payload+4) + tag texte 13 o (payload+8, PAS payload+12 -- offset propre a
// cette famille) -> message "[(classe)] tag suffixe", HUD flottant + chat. Purement
// des notifications, aucun etat ecrit.
// ---------------------------------------------------------------------------
void ApplyClassTagFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (len < 21) return;
    uint32_t classId = 0; std::memcpy(&classId, payload + 4, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
    const std::string classLabel = Str(75 + static_cast<int>(classId & 3));

    int strId = 0;
    if (subOpcode == 673) {
        uint32_t type = 0; if (len >= 25) std::memcpy(&type, payload + 8, 4); // NB: tag reste a +8 pour 673? voir ci-dessous
        // 673 (dump L.4398) : v691=class@4, v701=type@8 (21..25), tag@12 (PAS @8) --
        // relecture du tag a l'offset correct pour ce sous-cas precis.
        char tag12[14] = {};
        if (len >= 25) std::memcpy(tag12, payload + 12, 13);
        static constexpr int kMsg[5] = {2210, 2211, 2216, 2217, 2218};
        if (type < 21 || type > 25) return;
        const std::string s = "[(" + classLabel + ")] " + std::string(tag12) + " " + Str(kMsg[type - 21]);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
        return;
    }
    if (subOpcode == 676) {
        uint32_t sel = 0; if (len >= 25) std::memcpy(&sel, payload + 8, 4);
        char tag12[14] = {}; if (len >= 25) std::memcpy(tag12, payload + 12, 13);
        if (sel != 4 && sel != 5) return; // sinon buffer non-initialise cote binaire -> no-op ici.
        strId = (sel == 4) ? 2221 : 2222;
        const std::string s = "[(" + classLabel + ")] " + std::string(tag12) + " " + Str(strId);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
        return;
    }
    switch (subOpcode) {
    case 671: strId = 2084; break;
    case 672: strId = 2208; break;
    case 674: strId = 2219; break;
    case 675: strId = 2220; break;
    case 677: strId = 2230; break;
    default: return;
    }
    if (subOpcode == 671) {
        const std::string s = classLabel + " " + std::string(tag) + " " + Str(strId);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
    } else {
        const std::string s = "[(" + classLabel + ")] " + std::string(tag) + " " + Str(strId);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
    }
}

// ---------------------------------------------------------------------------
// 2e famille "siege par element" (dump L.4481-4847, sous-opcodes 700..729), etat
// dword_16862A4[elt]. 700/701/702 se comparent a g_World.self.element (comme
// 611..615) ; 703+ se comparent au morph via une 2e table de villes de siege
// kSiegeTownNpc2 = {5,10,15,123} (v672 du desasm, DISTINCTE de kSiegeTownNpc).
// ---------------------------------------------------------------------------
constexpr uint32_t kSiege2State        = 0x16862A4;
constexpr int32_t  kSiegeTownNpc2[4]   = {5, 10, 15, 123};
constexpr uint32_t kSiege2Pair703_A = 0x1675DF8, kSiege2Pair703_B = 0x1675DFC;
constexpr uint32_t kSiege2Pair704_A = 0x1675E20, kSiege2Pair704_B = 0x1675E24;
constexpr uint32_t kSiege2Pair710_A = 0x1675E00, kSiege2Pair710_B = 0x1675E04;
constexpr uint32_t kSiege2Pair711_A = 0x1675E28, kSiege2Pair711_B = 0x1675E2C;
constexpr uint32_t kSiege2Pair714_A = 0x1675E08, kSiege2Pair714_B = 0x1675E0C;
constexpr uint32_t kSiege2Pair715_A = 0x1675E30, kSiege2Pair715_B = 0x1675E34;
constexpr uint32_t kSiege2Pair718_A = 0x1675E10, kSiege2Pair718_B = 0x1675E14;
constexpr uint32_t kSiege2Pair719_A = 0x1675E38, kSiege2Pair719_B = 0x1675E3C;
constexpr uint32_t kSiege2Pair724_A = 0x1675E18, kSiege2Pair724_B = 0x1675E1C;
constexpr uint32_t kSiege2Pair725_A = 0x1675E40, kSiege2Pair725_B = 0x1675E44;

void SetSiege2Pair(uint32_t a, uint32_t b) { g_Client.Var(a) = 1; g_Client.Var(b) = 1; }

void ApplySiegeStage2Family(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t elt = 0, arg2 = 0;
    if (len >= 8)  std::memcpy(&elt,  payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    if (elt >= 4) return;
    const bool onMorph   = (g_Client.VarGet(kSelfMorphNpcId) == kSiegeTownNpc2[elt]);
    const bool onSelfElt = (static_cast<int32_t>(elt) == g_World.self.element);

    auto twoLineMsg = [&](int primaryStr, int subtitleStr) {
        const std::string s = Str(primaryStr);
        g_Client.msg.Floating(0xC, 0, s);
        g_Client.msg.System(s);
        g_Client.msg.System(Str(subtitleStr));
    };
    auto countFloatOnly = [&](int literalCount, int strId) {
        g_Client.msg.Floating(0xC, 0, std::to_string(literalCount) + Str(strId));
    };

    switch (subOpcode) {
    case 700:
        g_Client.Var(kSiege2State + 4 * elt) = 0;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(2064);
            AnnounceFloating10(s);
        }
        return;
    case 701:
        g_Client.Var(kSiege2State + 4 * elt) = 1;
        if (onSelfElt) { const std::string s = Str(2065); AnnounceFloating10(s); }
        return;
    case 702:
        g_Client.Var(kSiege2State + 4 * elt) = 2;
        if (onSelfElt) { const std::string s = Str(2071); AnnounceFloating10(s); }
        return;
    case 703: g_Client.Var(kSiege2State + 4 * elt) = 3;  if (onMorph) SetSiege2Pair(kSiege2Pair703_A, kSiege2Pair703_B); return;
    case 704: g_Client.Var(kSiege2State + 4 * elt) = 4;  if (onMorph) SetSiege2Pair(kSiege2Pair704_A, kSiege2Pair704_B); return;
    case 705: g_Client.Var(kSiege2State + 4 * elt) = 5;  if (onMorph) twoLineMsg(2134, 2146); return;
    case 706: g_Client.Var(kSiege2State + 4 * elt) = 30; return;
    case 707:
        g_Client.Var(kSiege2State + 4 * elt) = 26;
        if (onMorph) {
            const std::string s = std::to_string(arg2) + Str(2076);
            AnnounceFloating10(s);
        }
        return;
    case 708: g_Client.Var(kSiege2State + 4 * elt) = 6; return;
    case 709: g_Client.Var(kSiege2State + 4 * elt) = 7;  if (onMorph) countFloatOnly(1, 2075); return;
    case 710: g_Client.Var(kSiege2State + 4 * elt) = 8;  if (onMorph) SetSiege2Pair(kSiege2Pair710_A, kSiege2Pair710_B); return;
    case 711: g_Client.Var(kSiege2State + 4 * elt) = 9;  if (onMorph) SetSiege2Pair(kSiege2Pair711_A, kSiege2Pair711_B); return;
    case 712: g_Client.Var(kSiege2State + 4 * elt) = 11; if (onMorph) twoLineMsg(2135, 2147); return;
    case 713: g_Client.Var(kSiege2State + 4 * elt) = 12; if (onMorph) countFloatOnly(2, 2075); return;
    case 714: g_Client.Var(kSiege2State + 4 * elt) = 13; if (onMorph) SetSiege2Pair(kSiege2Pair714_A, kSiege2Pair714_B); return;
    case 715: g_Client.Var(kSiege2State + 4 * elt) = 14; if (onMorph) SetSiege2Pair(kSiege2Pair715_A, kSiege2Pair715_B); return;
    case 716: g_Client.Var(kSiege2State + 4 * elt) = 15; if (onMorph) twoLineMsg(2136, 2148); return;
    case 717: g_Client.Var(kSiege2State + 4 * elt) = 16; if (onMorph) countFloatOnly(3, 2075); return;
    case 718: g_Client.Var(kSiege2State + 4 * elt) = 17; if (onMorph) SetSiege2Pair(kSiege2Pair718_A, kSiege2Pair718_B); return;
    case 719: g_Client.Var(kSiege2State + 4 * elt) = 18; if (onMorph) SetSiege2Pair(kSiege2Pair719_A, kSiege2Pair719_B); return;
    case 720: g_Client.Var(kSiege2State + 4 * elt) = 19; if (onMorph) twoLineMsg(2137, 2149); return;
    case 721: g_Client.Var(kSiege2State + 4 * elt) = 20; return;
    case 722: g_Client.Var(kSiege2State + 4 * elt) = 19; return;
    case 723: g_Client.Var(kSiege2State + 4 * elt) = 21; if (onMorph) countFloatOnly(4, 2075); return;
    case 724: g_Client.Var(kSiege2State + 4 * elt) = 22; if (onMorph) SetSiege2Pair(kSiege2Pair724_A, kSiege2Pair724_B); return;
    case 725: g_Client.Var(kSiege2State + 4 * elt) = 23; if (onMorph) SetSiege2Pair(kSiege2Pair725_A, kSiege2Pair725_B); return;
    case 726: g_Client.Var(kSiege2State + 4 * elt) = 24; if (onMorph) twoLineMsg(2138, 2150); return;
    case 727: g_Client.Var(kSiege2State + 4 * elt) = 25; return;
    case 728: g_Client.Var(kSiege2State + 4 * elt) = 30; if (onMorph) countFloatOnly(5, 2075); return;
    case 729:
        g_Client.Var(kSiege2State + 4 * elt) = 0;
        if (onMorph) WarpToOwnFactionTown();
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// 3e famille "siege / classement" (morph fixe 54, dump L.4848-4964, sous-opcodes
// 740..749), etat dword_16862B4 (global, sans index). Deux sous-cas (740/741/742)
// se gatent sur g_World.self.levelBonus==12 plutot que sur le morph.
// ---------------------------------------------------------------------------
constexpr uint32_t kRankState   = 0x16862B4;
constexpr int32_t  kRankMorph   = 54;
constexpr uint32_t kRankArmA = 0x1675E70, kRankArmB = 0x1675E74,
                    kRankArmC = 0x1675E78, kRankArmD = 0x1675E7C;

void ApplyRankFamily740(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kRankMorph);
    switch (subOpcode) {
    case 740: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.Var(kRankState) = 0;
        if (g_World.self.levelBonus == 12) {
            const std::string s = std::to_string(count) + Str(2192);
            AnnounceFloating10(s);
        }
        return;
    }
    case 741:
        g_Client.Var(kRankState) = 1;
        if (g_World.self.levelBonus == 12) { const std::string s = Str(2193); AnnounceFloating10(s); }
        return;
    case 742:
        g_Client.Var(kRankState) = 2;
        if (g_World.self.levelBonus == 12) { const std::string s = Str(2194); AnnounceFloating10(s); }
        return;
    case 743: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.Var(kRankState) = 3;
        if (onMorph) {
            const std::string s = std::to_string(count) + Str(2074);
            AnnounceFloating10(s);
        }
        return;
    }
    case 744:
        g_Client.Var(kRankState) = 6;
        return;
    case 745: {
        int32_t c1 = -1, c2 = -1; uint32_t count = 0;
        if (len >= 8)  std::memcpy(&c1, payload + 4, 4);
        if (len >= 12) std::memcpy(&c2, payload + 8, 4);
        if (len >= 16) std::memcpy(&count, payload + 12, 4);
        g_Client.Var(kRankState) = 6;
        if (!onMorph) return;
        if (c1 >= 0) { const std::string s = Str(75 + (c1 & 3)) + Str(2197); AnnounceFloating10(s); }
        if (c2 >= 0) { const std::string s = Str(75 + (c2 & 3)) + Str(2197); g_Client.msg.Floating(0, 0, s); g_Client.msg.System(s); }
        { const std::string s = std::to_string(count) + Str(2201); g_Client.msg.Floating(3, 0, s); g_Client.msg.System(s); }
        return;
    }
    case 746: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.Var(kRankState) = 5;
        if (onMorph) {
            const std::string s = std::to_string(count) + Str(2074);
            AnnounceFloating10(s);
            g_Client.Var(kRankArmA) = 1; g_Client.Var(kRankArmB) = 1;
            g_Client.Var(kRankArmC) = 1; g_Client.Var(kRankArmD) = 1;
        }
        return;
    }
    case 747:
        g_Client.Var(kRankState) = 7;
        if (onMorph) {
            const std::string s = Str(1702);
            AnnounceFloating10(s);
            WarpToOwnFactionTown();
        }
        return;
    case 748: {
        int32_t cls = -1; if (len >= 8) std::memcpy(&cls, payload + 4, 4);
        g_Client.Var(kRankState) = 6;
        if (onMorph && cls >= 0) {
            const std::string s = Str(75 + (cls & 3)) + Str(2202);
            AnnounceFloating10(s);
        }
        return;
    }
    case 749:
        g_Client.Var(kRankState) = 0;
        if (onMorph) WarpToOwnFactionTown();
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Table de titres/rang (dump L.4977-5054, sous-opcodes 752/753) : dword_184C218[0..11]
// (etat), dword_184C248[0..11] (753, cible non consommee par ce dispatcher -- pas de
// bornage dans le binaire d'origine ; borne ici a 12 par prudence defensive, meme
// convention que le reste de ClientSource -- fidelite comportementale MAIS pas
// fidelite bit-exacte d'un potentiel depassement mémoire d'origine).
// ---------------------------------------------------------------------------
constexpr uint32_t kRankTable1 = 0x184C218; // [0..11]
constexpr uint32_t kRankTable2 = 0x184C248; // [0..11]
constexpr int32_t  kRankTitleId[12] = {2, 3, 4, 7, 8, 9, 12, 13, 14, 141, 142, 143};

void ApplyRankTable(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t idx = 0; int32_t val = 0;
    if (len >= 8)  std::memcpy(&idx, payload + 4, 4);
    if (len >= 12) std::memcpy(&val, payload + 8, 4);
    if (subOpcode == 752) {
        if (idx < 12) { g_Client.Var(kRankTable1 + 4 * idx) = val; }
        if (idx >= 12) return;
        const int32_t cur = g_Client.VarGet(kRankTable1 + 4 * idx);
        if (cur / 100 != 9 && cur / 100 != 1) return;
        const int32_t titleId = kRankTitleId[idx];
        const std::string s = SkillName(titleId) + " " + Str((cur % 100) + 2249) + " " + Str(cur / 100 == 9 ? 2301 : 2300);
        g_Client.msg.Floating(0, 0, s);
        g_Client.msg.System(s);
        return;
    }
    if (subOpcode == 753) {
        if (idx < 12) g_Client.Var(kRankTable2 + 4 * idx) = val;
        return;
    }
}

// ---------------------------------------------------------------------------
// Sous-opcode 754 (dump L.5055-5111) : verbe (0/1/2, payload+4) + classe 0..3
// (payload+8) + comboMotionId (payload+12) + tag texte 13 o (payload+16, offset
// PROPRE a ce sous-cas). Message = SkillName + verbe + " [classe] [tag] " + str284.
// Verbes/classes hors plage -> no-op (le binaire reutilise un buffer non
// re-initialise dans ce cas, comportement fragile non reproduit ici).
// ---------------------------------------------------------------------------
void Apply754(const uint8_t* payload, uint32_t len) {
    if (len < 29) return;
    uint32_t verb = 0, classId = 0; int32_t comboMotionId = 0;
    std::memcpy(&verb, payload + 4, 4);
    std::memcpy(&classId, payload + 8, 4);
    std::memcpy(&comboMotionId, payload + 12, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 16, 13);
    if (verb > 2 || classId > 3) return;
    static constexpr int kVerb[3] = {2250, 2251, 2252};
    const std::string s = SkillName(comboMotionId) + " " + Str(kVerb[verb]) +
                           " [" + Str(75 + static_cast<int>(classId)) + "] [" + tag + "] " + Str(284);
    g_Client.msg.Floating(0, 0, s);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// Famille "annonce de cast" classe+tag (dump L.5254-5368, sous-opcodes 771..774) --
// meme squelette que 671..677 mais avec un comboMotionId (SkillName) en plus, et un
// double-espace fidele au format d'origine ("[%s] [%s]  %s %s"). 771/774 arment en
// plus un timer partage si le morph courant est un de {85,99,100,196} (CALCULABLE).
// ---------------------------------------------------------------------------
constexpr uint32_t kCastArmA_771 = 0x1675CA0, kCastArmB_771 = 0x1675CA4;
constexpr uint32_t kCastArmA_774 = 0x1675CA8, kCastArmB_774 = 0x1675CAC;

bool IsCastAnnounceMorph(int32_t morph) { return morph == 85 || morph == 99 || morph == 100 || morph == 196; }

void ApplyCastAnnounce771to774(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (subOpcode == 772) {
        if (len < 8) return;
        int32_t comboMotionId = 0; std::memcpy(&comboMotionId, payload + 4, 4);
        const std::string s = SkillName(comboMotionId) + " " + Str(2431);
        AnnounceFloating24(s);
        return;
    }
    if (subOpcode == 773) {
        if (len < 12) return;
        uint32_t count = 0; int32_t comboMotionId = 0;
        std::memcpy(&count, payload + 4, 4);
        std::memcpy(&comboMotionId, payload + 8, 4);
        const std::string s = SkillName(comboMotionId) + " " + Str(2432) + " " + std::to_string(count) + Str(79);
        AnnounceFloating24(s);
        return;
    }
    // 771 / 774 : classe (payload+4) + comboMotionId (payload+8) + tag13 (payload+12).
    if (len < 25) return;
    uint32_t classId = 0; int32_t comboMotionId = 0;
    std::memcpy(&classId, payload + 4, 4);
    std::memcpy(&comboMotionId, payload + 8, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
    if (classId > 3) return;
    const int strId = (subOpcode == 771) ? 2430 : 2433;
    const std::string s = "[" + Str(75 + static_cast<int>(classId)) + "] [" + tag + "]  " +
                           SkillName(comboMotionId) + " " + Str(strId);
    g_Client.msg.Floating(2, (subOpcode == 771) ? 4 : 3, s);
    g_Client.msg.System(s);
    const int32_t morph = g_Client.VarGet(kSelfMorphNpcId);
    if (!IsCastAnnounceMorph(morph)) return;
    if (subOpcode == 771) { g_Client.Var(kCastArmA_771) = 1; g_Client.VarF(kCastArmB_771) = 0.0f; }
    else                  { g_Client.Var(kCastArmA_774) = 1; g_Client.VarF(kCastArmB_774) = 0.0f; }
}

// ---------------------------------------------------------------------------
// Notifications "SkillName + suffixe" simples (dump L.5450-5470, sous-opcodes
// 788/789/790) : comboMotionId=payload+4, message chat seul (pas de HUD).
// ---------------------------------------------------------------------------
void ApplySimpleSkillNotice(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (len < 8) return;
    int32_t comboMotionId = 0; std::memcpy(&comboMotionId, payload + 4, 4);
    int strId = 0;
    switch (subOpcode) {
    case 788: strId = 2462; break;
    case 789: strId = 2502; break;
    case 790: strId = 2463; break;
    default: return;
    }
    g_Client.msg.System(SkillName(comboMotionId) + " " + Str(strId));
}

// Sous-opcode 791 (dump L.5471) : comboMotionId + classe + tag13 (payload+4/+8/+12,
// offsets standard). Message HUD+chat.
void Apply791(const uint8_t* payload, uint32_t len) {
    if (len < 25) return;
    int32_t comboMotionId = 0; uint32_t classId = 0;
    std::memcpy(&comboMotionId, payload + 4, 4);
    std::memcpy(&classId, payload + 8, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
    const std::string s = SkillName(comboMotionId) + " " + Str(2459) + " " + Str(75 + static_cast<int>(classId & 3)) +
                           " " + Str(2460) + " " + tag + " " + Str(2461);
    g_Client.msg.Floating(2, 1, s);
    g_Client.msg.System(s);
}

// Sous-opcode 795 (dump L.5526) : classe (payload+4, offset +1672 -> etiquette),
// tag13 (payload+8), compte (payload+21). Le format-table Str(2576) est applique au
// compte dans le binaire -- approxime en suffixe "[n]" (meme convention que 610/661).
void Apply795(const uint8_t* payload, uint32_t len) {
    if (len < 25) return;
    uint32_t classId = 0; int32_t count = 0;
    std::memcpy(&classId, payload + 4, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
    if (len >= 25) std::memcpy(&count, payload + 21, 4);
    const std::string s = Str(static_cast<int>(classId) + 1672) + " " + tag + "[" + std::to_string(count) + "]" + Str(2576);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// Evenements "guerre 324" (dump L.5369-5450, sous-opcodes 780..786) : etat
// dword_1686304 (rang, -1=vide)/dword_1686308 (valeur). Gate morph = 324.
// TODO(World_LoadCurrentZoneModel) omis partout ou cite (rechargement de modele de
// zone, sous-systeme non modelise dans ClientSource).
// ---------------------------------------------------------------------------
constexpr uint32_t kWar324Rank  = 0x1686304;
constexpr uint32_t kWar324Value = 0x1686308;
constexpr int32_t  kWar324Morph = 324;

void ApplyWarEvent780to786(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kWar324Morph);
    switch (subOpcode) {
    case 780: { // TODO(World_LoadCurrentZoneModel(6)) si onMorph.
        uint32_t count = 0; int32_t j = 0;
        if (len >= 8)  std::memcpy(&count, payload + 4, 4);
        if (len >= 12) std::memcpy(&j, payload + 8, 4);
        g_Client.Var(kWar324Value) = j;
        const std::string s = std::to_string(count) + " " + Str(2439);
        AnnounceFloating24(s);
        g_Client.msg.System(Str(2440));
        return;
    }
    case 781: { // TODO(World_LoadCurrentZoneModel(2)) si onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar324Value) = j;
        g_Client.Var(kWar324Rank) = -1;
        const std::string s = Str(2441);
        AnnounceFloating24(s);
        return;
    }
    case 782: {
        int32_t rank = 0, j = 0;
        if (len >= 8)  std::memcpy(&rank, payload + 4, 4);
        if (len >= 12) std::memcpy(&j, payload + 8, 4);
        g_Client.Var(kWar324Rank) = rank;
        g_Client.Var(kWar324Value) = j;
        std::string s;
        if (rank >= 0)
            s = Str(2532) + " [ " + Str(rank / 10 + 2685) + " ] " + Str(377) + " [ " + Str(rank % 10 + 2685) + " ] " + Str(2442);
        else
            s = Str(2532) + " " + Str(2447);
        AnnounceFloating24(s);
        g_Client.msg.System(Str(2446));
        return;
    }
    case 784: {
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar324Value) = j;
        const std::string s = Str(2445);
        AnnounceFloating24(s);
        if (onMorph) g_Client.msg.System(Str(2446));
        return;
    }
    case 785: { // TODO(World_LoadCurrentZoneModel(1)) si onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar324Value) = j;
        if (onMorph) WarpToOwnFactionTown();
        return;
    }
    case 786: { // NB : lit payload+8 (pas +4), fidele au desasm (v703, pas v702).
        int32_t j = 0; if (len >= 12) std::memcpy(&j, payload + 8, 4);
        g_Client.Var(kWar324Rank) = -1;
        g_Client.Var(kWar324Value) = j;
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Evenements "guerre 342" (dump L.5537-5610, sous-opcodes 800..807), meme forme
// que 780..786 avec un morph cible different (342) et un seul etat dword_1686310.
// 803 (dump L.5558) INVESTIGUE ET CONFIRME NO-OP : copie 16 o dans un tampon LOCAL
// jamais relu (dead store cote binaire d'origine) -- aucun code ajoute ici (le
// `default:` du dispatcheur le couvre deja fidelement).
// ---------------------------------------------------------------------------
constexpr uint32_t kWar342Value = 0x1686310;
constexpr int32_t  kWar342Morph = 342;

void ApplyWarEvent800to807(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kWar342Morph);
    switch (subOpcode) {
    case 800: {
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar342Value) = j;
        return;
    }
    case 801: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.msg.System(std::to_string(count) + " " + Str(2761));
        return;
    }
    case 802: { // TODO(World_LoadCurrentZoneModel(1)) si onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar342Value) = j;
        const std::string s = Str(2762);
        AnnounceFloating24(s);
        return;
    }
    case 804: { // TODO(World_LoadCurrentZoneModel(2)) si onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar342Value) = j;
        const std::string s = Str(2764);
        g_Client.msg.Floating(2, 4, s);
        if (onMorph) { g_Client.Var(kChargeArmedTimer) = 1; g_Client.VarF(kChargeElapsed) = 0.0f; }
        g_Client.msg.System(s);
        return;
    }
    case 805: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.msg.System(std::to_string(count) + " " + Str(2766));
        return;
    }
    case 806: {
        int32_t j = 0, val = 0;
        if (len >= 8)  std::memcpy(&j, payload + 4, 4);
        if (len >= 12) std::memcpy(&val, payload + 8, 4);
        g_Client.Var(kWar342Value) = val;
        g_Client.msg.System(Str(2767));
        if (j == -1) g_Client.msg.System(Str(2765));
        else         g_Client.msg.System("[ " + Str(j + 2685) + " ] " + Str(2442));
        return;
    }
    case 807: // TODO(World_LoadCurrentZoneModel(1)) si onMorph.
        if (onMorph) WarpToOwnFactionTown();
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Sous-opcodes 31 (EA 0x4963b0) / 32 (EA 0x496583) -- mission "survol 31..170"
// (2026-07-14) : PREMIER mecanisme du "reste" du dispatcher (verifie sur le
// desassemblage complet, PAS une suite de la famille 3 -- Skill_GetComboMotionId
// n'est jamais appele ici). Les deux sous-opcodes sont STRUCTURELLEMENT JUMEAUX
// (memes instructions dupliquees par le compilateur) : seule la valeur ecrite
// (0/1) et la 2e plage de libelles (251..254 vs 255..258) different.
//   branche = payload+4 (etiquette str75..78, EA 0x4963f5..0x49645e -- meme
//     convention que ApplyFamily3TagSlot/ApplyRankFamily740).
//   type    = payload+8 (etiquette str251..254 pour 31, str255..258 pour 32,
//     EA 0x4964b8..0x49652a ; format " %s" = off_7A6E5C, LU DIRECTEMENT en
//     memoire -- pas de litteral C recree a la main).
// Le binaire NE BORNE l'index (0..3, "ja default") QUE pour le CHOIX DU LIBELLE
// (EA 0x4963e2/0x4964a5) -- hors 0..3, le segment de texte correspondant est
// simplement omis (StrTable005_Get n'est pas appele). L'ECRITURE dans
// dword_1686014, elle, est INCONDITIONNELLE et precede ces controles (comme la
// convention "etat avant gate" des familles de combo). Notice flottante
// HUD_ShowFloatingMessage(floatType=3, flag=4) commune aux deux sous-opcodes.
// AUCUN consommateur de dword_1686014 n'est porte dans ClientSource a ce jour --
// verifie par xrefs_to idaTs2 : trois lecteurs existent cote binaire
// (Combo_CheckTransition 0x4fd650 EA 0x4ff2d6, Player_UpdateMovement 0x534500
// EA 0x534856, UI_FactionInfoWnd_Render 0x672010 EA 0x67246d) -> ecriture
// persistee fidelement mais SANS EFFET observable tant que ces 3 fonctions ne
// sont pas reecrites cote client. TODO(@dword_1686014_consumers).
// ---------------------------------------------------------------------------
constexpr uint32_t kExclusionTable = 0x1686014; // dword_1686014[4*branche+type], EA 0x4963b0/0x496583

void ApplyExclusionToggleSlot(uint32_t branch, uint32_t type, uint32_t value) {
    g_Client.Var(kExclusionTable + 4 * (4 * branch + type)) = static_cast<int32_t>(value);

    const int typeBase = value ? 255 : 251; // 31(value=0)=251..254 ; 32(value=1)=255..258
    std::string text;
    if (branch <= 3) text += "[" + Str(75 + static_cast<int>(branch)) + "]";
    if (type   <= 3) text += " " + Str(typeBase + static_cast<int>(type));
    g_Client.msg.Floating(3, 4, text);
}

// ---------------------------------------------------------------------------
// Sous-opcodes 33..45 (dump L.1328-1675, mission "loadout d'element" 2026-07-14) --
// notices/etat du "loadout" (chargement) d'element du joueur local. Aucun rapport
// avec Skill_GetComboMotionId/GetSpecialMotionId/GetBuffMotionId ni avec
// Char_GetPairedElement : systeme independant, verifie par lecture complete du
// desassemblage (RE/dispatch_494870_full.c L.1328-1675). Categorie HUD flottante
// 2 pour 33..38, 3 pour 39..45 ; TOUS les sous-cas 33..45 ecrivent HUD + ligne
// systeme (verifie instruction par instruction -- contrairement a 46..50
// ci-dessous qui n'ecrivent QUE le HUD).
//   33 (dump L.1328) : compte=payload+4 -> "[compte]str271", HUD cat2/type1. Pas d'etat.
//   34 (dump L.1335) : aucun payload lu. "str272" seul, HUD cat2/type2. Pas d'etat.
//   35 (dump L.1341) : classe 0..3=payload+4, tag13=payload+8 (PAS +12 -- offset
//       propre, verifie esp-a-esp) -> "[classe] [tag] str273", HUD cat2/type4. Gate
//       CALCULABLE (morph==38) -> arme un timer PROPRE (dword_1675BEC/flt_1675BF0),
//       SANS rapport avec la barre de charge partagee des familles de combo. Pas
//       d'etat ecrit.
//   36 (dump L.1377) : aucun payload lu. "str274" seul, HUD cat2/type4. Pas d'etat.
//   37 (dump L.1383) : compte=payload+4 -> "[compte]str275", HUD cat2/type4. Pas d'etat.
//   38 (dump L.1390) : classe 0..3=payload+4, tag13=payload+8 (meme offset
//       non-standard que 35) -> ETAT ECRIT INCONDITIONNELLEMENT (dword_1685E08=
//       classe, dword_1685E0C=jour courant), message "[classe] [tag] str276", HUD
//       cat2/type3. Puis warp CALCULABLE : morph==38 -> arme un timer PROPRE
//       distinct de 35 (dword_1675BF4/flt_1675BF8, PAS de warp) ; morph in
//       {39,74,144,145} -> retour ville de faction ; morph==310 ET g_LocalElement
//       != classe ET !dword_16757A8 (garde anti-reentrance non ecrite ailleurs,
//       lue seule ici via Var(), meme statut que g_SelfCharInvBlock) -> retour
//       ville de faction ; tout autre morph -> rien.
//   39 (dump L.1448) : compte=payload+4 -> "[compte]str277", HUD cat3/type1. Pas d'etat.
//   40 (dump L.1455) : aucun payload lu. RESET du loadout (valeurs par defaut codees
//       en dur, PAS un pattern uniforme) : dword_1685E10=1, g_ElementLoadout
//       (0x1685E14)=0, dword_1685E18=1, dword_1685E1C=2, dword_1685E20=3 ; message
//       "str278" (HUD cat3/type2) ; PUIS warp INCONDITIONNEL si morph in
//       {50,52,85,99,100,170,196} (table en dur) ; PUIS reset maitrise :
//       g_ElementMastery(0x1675680)=0, dword_1675678=0, dword_168746C=0.
//   41 (dump L.1483) : selector 0..4=payload+4 (labels str279..283), classe
//       0..3=payload+8, tag13=payload+12 (offset STANDARD, contrairement a 35/38) ->
//       "<label> [classe] [tag] str284", HUD cat3/type3. Notification PURE (contrairement
//       a 42, MEME libelle, qui ECRIT).
//   42 (dump L.1543) : selector 0..4=payload+4, valeur(classe 0..3)=payload+8 -> ECRIT
//       le slot du loadout designe par le selector (0->g_ElementLoadout,
//       1->dword_1685E18, 2->dword_1685E1C, 3->dword_1685E20, 4->dword_1685E24 +
//       dword_1685E28=jour courant, SEUL slot avec horodatage) ; message
//       "<label> [classe] str285", HUD cat3/type4.
//   43 (dump L.1623) : selector 0..4=payload+4 -> "<label> str286", HUD cat3/type5. Pas d'etat.
//   44 (dump L.1658) : compte=payload+4 -> "[compte]str287", HUD cat3/type6. Pas d'etat.
//   45 (dump L.1665) : aucun payload lu. RESET : dword_1685E10=0, dword_16860B0/B4/
//       B8/BC=0 (4 champs distincts, PAS les slots 1685E14..24), message "str288",
//       HUD cat3/type7.
// EA precises non recapturees individuellement -- localisation fiable = le numero de
// ligne du dump (RE/dispatch_494870_full.c) cite par cas.
// ---------------------------------------------------------------------------
constexpr uint32_t kElemLoadoutFlag       = 0x1685E10; // "loadout arme" (40=1, 45=0)
constexpr uint32_t kElemLoadoutSlot0      = 0x1685E14; // == g_ElementLoadout
constexpr uint32_t kElemLoadoutSlot1      = 0x1685E18;
constexpr uint32_t kElemLoadoutSlot2      = 0x1685E1C;
constexpr uint32_t kElemLoadoutSlot3      = 0x1685E20;
constexpr uint32_t kElemLoadoutSlot4      = 0x1685E24;
constexpr uint32_t kElemLoadoutSlot4Stamp = 0x1685E28;
constexpr uint32_t kElemLastClass         = 0x1685E08; // 38 : classe recue
constexpr uint32_t kElemLastStamp         = 0x1685E0C; // 38 : jour courant
constexpr uint32_t kElemResetB0 = 0x16860B0, kElemResetB1 = 0x16860B4,
                    kElemResetB2 = 0x16860B8, kElemResetB3 = 0x16860BC; // 45
constexpr uint32_t kElemMastery     = 0x1675680; // == g_ElementMastery (40, reset)
constexpr uint32_t kElemMasteryAux1 = 0x1675678; // 40, reset
constexpr uint32_t kElemMasteryAux2 = 0x168746C; // 40, reset
constexpr uint32_t kElem35TimerFlag  = 0x1675BEC, kElem35TimerFrame = 0x1675BF0; // 35, gate morph==38
constexpr uint32_t kElem38TimerFlag  = 0x1675BF4, kElem38TimerFrame = 0x1675BF8; // 38, gate morph==38
constexpr uint32_t kElemWarpGuard310 = 0x16757A8; // 38, garde morph==310 (lecture seule ici)

// Libelle de composant du loadout (str279..283, selector 0..4) -- partage par 41/42/43.
inline std::string LoadoutComponentLabel(uint32_t selector) {
    static constexpr int kIds[5] = {279, 280, 281, 282, 283};
    return (selector <= 4) ? Str(kIds[selector]) : std::string();
}

void ApplyElementLoadoutFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    switch (subOpcode) {
    case 33: { // dump L.1328
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(271);
        g_Client.msg.Floating(2, 1, s);
        g_Client.msg.System(s);
        return;
    }
    case 34: { // dump L.1335
        const std::string s = Str(272);
        g_Client.msg.Floating(2, 2, s);
        g_Client.msg.System(s);
        return;
    }
    case 35: { // dump L.1341 -- tag a payload+8 (PAS +12).
        if (len < 21) return;
        int32_t cls = 0; std::memcpy(&cls, payload + 4, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
        const std::string s = "[" + ClassLabel(cls) + "] [" + tag + "] " + Str(273);
        g_Client.msg.Floating(2, 4, s);
        g_Client.msg.System(s);
        if (g_Client.VarGet(kSelfMorphNpcId) == 38) {
            g_Client.Var(kElem35TimerFlag)   = 1;
            g_Client.VarF(kElem35TimerFrame) = 0.0f;
        }
        return;
    }
    case 36: { // dump L.1377
        const std::string s = Str(274);
        g_Client.msg.Floating(2, 4, s);
        g_Client.msg.System(s);
        return;
    }
    case 37: { // dump L.1383
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(275);
        g_Client.msg.Floating(2, 4, s);
        g_Client.msg.System(s);
        return;
    }
    case 38: { // dump L.1390 -- tag a payload+8 (comme 35). Etat ecrit AVANT le message (fidele).
        if (len < 21) return;
        int32_t cls = 0; std::memcpy(&cls, payload + 4, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
        g_Client.Var(kElemLastClass) = cls;
        g_Client.Var(kElemLastStamp) = Time_GetMonthDayInt();
        const std::string s = "[" + ClassLabel(cls) + "] [" + tag + "] " + Str(276);
        g_Client.msg.Floating(2, 3, s);
        g_Client.msg.System(s);
        const int32_t morph = g_Client.VarGet(kSelfMorphNpcId);
        if (morph == 38) {
            g_Client.Var(kElem38TimerFlag)   = 1;
            g_Client.VarF(kElem38TimerFrame) = 0.0f;
        } else if (morph == 39 || morph == 74 || morph == 144 || morph == 145) {
            BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
        } else if (morph == 310 && g_World.self.element != cls && !g_Client.VarGet(kElemWarpGuard310)) {
            BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
        }
        return;
    }
    case 39: { // dump L.1448
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(277);
        g_Client.msg.Floating(3, 1, s);
        g_Client.msg.System(s);
        return;
    }
    case 40: { // dump L.1455 -- reset loadout (valeurs par defaut codees en dur) + warp par
               // table de morphs + reset maitrise. Aucun payload lu.
        g_Client.Var(kElemLoadoutFlag)  = 1;
        g_Client.Var(kElemLoadoutSlot0) = 0;
        g_Client.Var(kElemLoadoutSlot1) = 1;
        g_Client.Var(kElemLoadoutSlot2) = 2;
        g_Client.Var(kElemLoadoutSlot3) = 3;
        const std::string s = Str(278);
        g_Client.msg.Floating(3, 2, s);
        g_Client.msg.System(s);
        switch (static_cast<int32_t>(g_Client.VarGet(kSelfMorphNpcId))) {
        case 50: case 52: case 85: case 99: case 100: case 170: case 196:
            BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        g_Client.Var(kElemMastery)     = 0;
        g_Client.Var(kElemMasteryAux1) = 0;
        g_Client.Var(kElemMasteryAux2) = 0;
        return;
    }
    case 41: { // dump L.1483 -- tag a payload+12 (offset standard, contrairement a 35/38).
               // Notification pure (contrairement a 42, meme libelle, qui ECRIT).
        if (len < 25) return;
        uint32_t selector = 0, cls = 0;
        std::memcpy(&selector, payload + 4, 4);
        std::memcpy(&cls, payload + 8, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
        const std::string s = LoadoutComponentLabel(selector) + " [" + ClassLabel(static_cast<int32_t>(cls)) +
                               "] [" + tag + "] " + Str(284);
        g_Client.msg.Floating(3, 3, s);
        g_Client.msg.System(s);
        return;
    }
    case 42: { // dump L.1543 -- ECRIT le slot du loadout designe par le selector.
        if (len < 12) return;
        uint32_t selector = 0; std::memcpy(&selector, payload + 4, 4);
        int32_t value = 0; std::memcpy(&value, payload + 8, 4);
        switch (selector) {
        case 0: g_Client.Var(kElemLoadoutSlot0) = value; break;
        case 1: g_Client.Var(kElemLoadoutSlot1) = value; break;
        case 2: g_Client.Var(kElemLoadoutSlot2) = value; break;
        case 3: g_Client.Var(kElemLoadoutSlot3) = value; break;
        case 4:
            g_Client.Var(kElemLoadoutSlot4)      = value;
            g_Client.Var(kElemLoadoutSlot4Stamp) = Time_GetMonthDayInt();
            break;
        default:
            break;
        }
        const std::string s = LoadoutComponentLabel(selector) + " [" + ClassLabel(value) + "] " + Str(285);
        g_Client.msg.Floating(3, 4, s);
        g_Client.msg.System(s);
        return;
    }
    case 43: { // dump L.1623 -- notification pure, aucun etat.
        if (len < 8) return;
        uint32_t selector = 0; std::memcpy(&selector, payload + 4, 4);
        const std::string s = LoadoutComponentLabel(selector) + " " + Str(286);
        g_Client.msg.Floating(3, 5, s);
        g_Client.msg.System(s);
        return;
    }
    case 44: { // dump L.1658
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(287);
        g_Client.msg.Floating(3, 6, s);
        g_Client.msg.System(s);
        return;
    }
    case 45: { // dump L.1665 -- reset (4 champs distincts, PAS les slots 1685E14..24).
               // Aucun payload lu.
        g_Client.Var(kElemLoadoutFlag) = 0;
        g_Client.Var(kElemResetB0) = 0;
        g_Client.Var(kElemResetB1) = 0;
        g_Client.Var(kElemResetB2) = 0;
        g_Client.Var(kElemResetB3) = 0;
        const std::string s = Str(288);
        g_Client.msg.Floating(3, 7, s);
        g_Client.msg.System(s);
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Sous-opcodes 46 (EA 0x497ce4) / 47 (EA 0x497d76) -- appariement d'ALLIANCE par
// element du joueur local (mission "cablage ElementPairTable", 2026-07-14,
// Docs/TS2_COMBAT_ELEMENT_GATING.md addendum). CORRECTIF d'une note anterieure
// (cf. Net/WorldEntityDispatch.h, ancien bandeau "46..47 ... DIFFERENT de
// Char_GetPairedElement") : verifie par resolution d'adresse idaTs2
// (list_globals) -- g_AlliancePairTable EST EXACTEMENT g_LocalPlayerSheet+0x71C
// (0x1685E64) et dword_1685E68 EST EXACTEMENT g_LocalPlayerSheet+0x720
// (0x1685E68), c.-a-d. les champs `a`/`b` de Game/SkillCombat.h::ElementPairTable
// (this[455]/this[456] lus par Char_GetPairedElement 0x557C00 sur `this =
// g_LocalPlayerSheet`). C'est donc bien LA MEME memoire : 46 ETABLIT une paire
// (slot libre 0..1, c.a.d. a/b OU c/d) ; 47 EFFACE la paire correspondante
// (-1/-1). AVANT cette passe, Combat_ReadLocalElementPairs() (SkillCombat.cpp)
// lisait un g_Client.Var() JAMAIS ECRIT -> ElementPairTable restait {0,0,0,0} en
// permanence (repli "aucune paire", fonctionnellement correct mais jamais mis a
// jour par le serveur, cf. note de fidelite dans SkillCombat.h). Ce bloc comble
// le trou de cablage cote ECRITURE (le decodage/la formule de gating restent
// inchanges, cf. Game/ComboPickupTick.cpp::Combat_IsElementAllowedOnMap).
//   elemA = payload+4 (=idx), elemB = payload+8 (=arg2) : les 2 elements a
//     apparier (46) ou desapparier (47).
//   46 seulement : `useSlot2` choisit le slot libre en testant l'OCCUPATION du
//     slot 1 (a/b, index FIXE 0, peu importe le slot cible) -- occupe
//     (a!=-1 || b!=-1) -> ecrit dans le slot 2 (c/d).
//   47 seulement : `useSlot2` = VRAI sauf si le slot 1 (a,b) correspond a
//     (elemA,elemB) dans un ordre OU L'AUTRE -- alors efface le slot 1, sinon le
//     slot 2. elemC = payload+12, elemD = payload+16 : nouveaux compteurs
//     g_PerElementCounter[elemA]/[elemB] (AUCUN consommateur identifie ailleurs
//     dans ClientSource -- ecrits fidelement pour tracabilite future, memes
//     adresses que le desassemblage). Notice flottante floatType=10 (0xA),
//     flag=1 (46) / flag=2 (47) ; si g_SelfMorphNpcId==37, arme un timer
//     generique DEJA modelise par Game/AnimationTick.cpp::kMorphRows[2] (46,
//     0x1675BFC/0x1675C00) ou kMorphRows[3] (47, 0x1675C04/0x1675C08) -- MEME
//     convention que Apply421 (cf. kZone291TimerFlag) : arme ici via
//     l'echappatoire Var(), consomme par le moteur de timers generique existant.
// ---------------------------------------------------------------------------
// NETTOYAGE (audit "dispatch 0x5e" 2026-07-14) -- bloc d'ecriture "desappariement"
// reproduit A L'IDENTIQUE (memes 6 ecritures Var(), meme calcul matchesSlot1/useSlot2)
// dans le sous-opcode 47 (ApplyAlliancePairFamily) ET dans le tail-merge 49
// (ApplyAllianceLabelFamily, cf. son propre commentaire "tail-merge de 47 cote ETAT") --
// verifie ligne a ligne, ecrit independamment par 2 missions differentes ("cablage
// ElementPairTable" et "loadout d'element"). Factorise ici, AUCUN changement de
// comportement (slot1A/slot1B relus fraichement a l'appel, comme le faisait deja
// chaque site individuellement -- aucune ecriture ne s'intercale entre la lecture et
// l'usage dans les deux appelants, donc equivalent bit-a-bit a l'ordre du binaire).
void ApplyAllianceUnpairState(uint32_t elemA, uint32_t elemB, uint32_t elemC, uint32_t elemD) {
    const int32_t slot1A = g_Client.VarGet(kElementPairAAddr);
    const int32_t slot1B = g_Client.VarGet(kElementPairBAddr);
    const bool matchesSlot1 =
        (slot1A == static_cast<int32_t>(elemA) && slot1B == static_cast<int32_t>(elemB)) ||
        (slot1B == static_cast<int32_t>(elemA) && slot1A == static_cast<int32_t>(elemB));
    const bool useSlot2 = !matchesSlot1;

    g_Client.Var(kPerElementCounterAddr + 8 * elemA) = static_cast<int32_t>(elemC);
    g_Client.Var(kPerElementFlagAddr    + 8 * elemA) = 1;
    g_Client.Var(kPerElementCounterAddr + 8 * elemB) = static_cast<int32_t>(elemD);
    g_Client.Var(kPerElementFlagAddr    + 8 * elemB) = 1;
    g_Client.Var(kElementPairAAddr + (useSlot2 ? 8u : 0u)) = -1;
    g_Client.Var(kElementPairBAddr + (useSlot2 ? 8u : 0u)) = -1;
}

void ApplyAlliancePairFamily(uint32_t subOpcode, uint32_t elemA, uint32_t elemB,
                              const uint8_t* payload, uint32_t len) {
    // Slot 1 courant (index fixe 0), lu AVANT toute ecriture -- sert au choix du
    // slot cible dans les deux sous-cas (fidele a l'ordre du binaire).
    const int32_t slot1A = g_Client.VarGet(kElementPairAAddr);
    const int32_t slot1B = g_Client.VarGet(kElementPairBAddr);

    if (subOpcode == 46) {
        const bool useSlot2 = (slot1A != -1 || slot1B != -1);
        g_Client.Var(kPerElementCounterAddr + 8 * elemA) = 0;
        g_Client.Var(kPerElementFlagAddr    + 8 * elemA) = 0;
        g_Client.Var(kPerElementCounterAddr + 8 * elemB) = 0;
        g_Client.Var(kPerElementFlagAddr    + 8 * elemB) = 0;
        g_Client.Var(kElementPairAAddr + (useSlot2 ? 8u : 0u)) = static_cast<int32_t>(elemA);
        g_Client.Var(kElementPairBAddr + (useSlot2 ? 8u : 0u)) = static_cast<int32_t>(elemB);

        const std::string s = "[" + ClassLabel(static_cast<int32_t>(elemA)) + "]" + Str(377) +
                               " [" + ClassLabel(static_cast<int32_t>(elemB)) + "]" + Str(378);
        g_Client.msg.Floating(10, 1, s);
        // CORRECTIF (2026-07-14, mission "loadout d'element" 33-50) : PAS de
        // Msg_AppendSystemLine ici -- verifie esp-a-esp sur RE/dispatch_494870_full.c
        // L.1676-1745 (case 46) : seul HUD_ShowFloatingMessage est appele, aucun appel
        // Msg_AppendSystemLine dans tout le corps du case (le precedent depuis 33..45 est
        // en L.1674, le suivant en L.1999/case 51) -- une precedente passe avait ajoute un
        // g_Client.msg.System(s) ici par erreur (calque sur 33..45, qui EUX ecrivent bien
        // les deux), retire pour rester fidele.
        if (g_Client.VarGet(kSelfMorphNpcId) == 37) {
            g_Client.Var(kAllySlot46ArmFlag)   = 1;
            g_Client.VarF(kAllySlot46ArmFrame) = 0.0f;
        }
    } else { // 47
        uint32_t elemC = 0, elemD = 0;
        if (len >= 16) std::memcpy(&elemC, payload + 12, 4);
        if (len >= 20) std::memcpy(&elemD, payload + 16, 4);

        ApplyAllianceUnpairState(elemA, elemB, elemC, elemD);

        const std::string s = "[" + ClassLabel(static_cast<int32_t>(elemA)) + "]" + Str(377) +
                               " [" + ClassLabel(static_cast<int32_t>(elemB)) + "]" + Str(379);
        g_Client.msg.Floating(10, 2, s);
        // NB (verifie desasm, dump L.1746-1818) : PAS de Msg_AppendSystemLine ici NON PLUS
        // (comme 46 ci-dessus -- cf. correctif de tete) : seul le HUD flottant est ecrit
        // pour 46/47/48/49/50.
        if (g_Client.VarGet(kSelfMorphNpcId) == 37) {
            g_Client.Var(kAllySlot47ArmFlag)   = 1;
            g_Client.VarF(kAllySlot47ArmFrame) = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Sous-opcodes 48..50 (dump L.1819-1969, tail-merge de la famille "appariement
// d'alliance" 46/47 ci-dessus, mission "loadout d'element" 2026-07-14) : 48/49
// PARTAGENT la FORME de libelle "<selector str380..383> [classe] [tag] <suffixe>"
// avec 41/42 (ApplyElementLoadoutFamily) mais restent DISTINCTS (suffixes/HUD
// differents : floatType=10 ici contre 3 pour 41/42) -- verifie par lecture
// complete du desassemblage. AUCUN des trois n'ecrit de ligne systeme (seul le HUD
// flottant est appele -- verifie : Msg_AppendSystemLine absent du binaire pour
// 48/49/50, DIVERGENCE confirmee vs 33..45 qui, eux, ecrivent systematiquement les
// deux) :
//   48 (dump L.1819) : selector 0..3=payload+4 (str380..383), classe 0..3=payload+8,
//     tag13=payload+12 -> "<selector> [classe] [tag] str284", HUD cat.10/type3.
//     Notification PURE (aucun etat ecrit).
//   49 (dump L.1874) : tail-merge de 47 cote ETAT (RELIT/REECRIT EXACTEMENT les
//     memes 4 champs que ApplyAlliancePairFamily(47) -- elemA/B=payload+4/8,
//     elemC/D=payload+12/16, MEMES adresses kElementPairAAddr/B/
//     kPerElementCounterAddr/kPerElementFlagAddr -- verifie esp-a-esp), PUIS
//     ajoute selector 0..3=payload+20 (str380..383) + classe 0..3=payload+24 +
//     tag13=payload+28 -> "<selector> [classe] [tag] str285", HUD cat.10/type4.
//     AUCUN gate morph==37/timer ici (contrairement a 47 -- le bloc
//     `if (g_SelfMorphNpcId==37)` est ABSENT du corps de 49 dans le desassemblage).
//   50 (dump L.1941) : selector 0..3=payload+4 (str380..383) -> "<selector> str286",
//     HUD cat.10/type5. Notification pure, aucun etat.
// ---------------------------------------------------------------------------
// Libelle "alliance" (str380..383, selector 0..3) -- distinct du libelle "loadout"
// (str279..283, LoadoutComponentLabel) meme si la FORME du message est identique.
inline std::string AllianceComponentLabel(uint32_t selector) {
    static constexpr int kIds[4] = {380, 381, 382, 383};
    return (selector <= 3) ? Str(kIds[selector]) : std::string();
}

void ApplyAllianceLabelFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    switch (subOpcode) {
    case 48: { // dump L.1819
        if (len < 25) return;
        uint32_t selector = 0, cls = 0;
        std::memcpy(&selector, payload + 4, 4);
        std::memcpy(&cls, payload + 8, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
        const std::string s = AllianceComponentLabel(selector) + " [" + ClassLabel(static_cast<int32_t>(cls)) +
                               "] [" + tag + "] " + Str(284);
        g_Client.msg.Floating(10, 3, s);
        return;
    }
    case 49: { // dump L.1874 -- reprise integrale de l'etat de 47 (memes adresses) + message
               // propre au format "selector/classe/tag" de 48 (suffixe str285).
        if (len < 41) return;
        uint32_t elemA = 0, elemB = 0, elemC = 0, elemD = 0, selector = 0, cls = 0;
        std::memcpy(&elemA, payload + 4, 4);
        std::memcpy(&elemB, payload + 8, 4);
        std::memcpy(&elemC, payload + 12, 4);
        std::memcpy(&elemD, payload + 16, 4);
        std::memcpy(&selector, payload + 20, 4);
        std::memcpy(&cls, payload + 24, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 28, 13);

        ApplyAllianceUnpairState(elemA, elemB, elemC, elemD);

        const std::string s = AllianceComponentLabel(selector) + " [" + ClassLabel(static_cast<int32_t>(cls)) +
                               "] [" + tag + "] " + Str(285);
        g_Client.msg.Floating(10, 4, s);
        return;
    }
    case 50: { // dump L.1941
        if (len < 8) return;
        uint32_t selector = 0; std::memcpy(&selector, payload + 4, 4);
        const std::string s = AllianceComponentLabel(selector) + " " + Str(286);
        g_Client.msg.Floating(10, 5, s);
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Sous-opcodes 101..115 (dump L.2579-2789, mission "duel/defi 101-115" 2026-07-14) --
// DEUX sous-systemes distincts identifies par l'audit initial (cf. Net/WorldEntityDispatch.h,
// plage "PLAGES NON COUVERTES") :
//   - 101..109/112/114 : DUEL/DEFI par comparaison de PSEUDO joueur -- le payload transporte
//     des noms bruts 13 o (PAS des indices) a des offsets DIFFERENTS de la convention
//     idx/arg2/tag13 standard du fichier (relus directement ici depuis `payload`, verifie
//     esp-a-esp sur le desassemblage : v702=payload+4, v703=payload+8, v706=payload+17,
//     v713=payload+30, v715=payload+34, v716=payload+43). Etat partage : dword_16746B8/
//     dword_168726C (etat de duel, 0=aucun/2=en cours/code brut selon le cas -- valeurs
//     confirmees par les ecritures de 102/107/108/114), dword_1687450 (code
//     opposant/resultat, reutilise par 102/107/112/114 -- 113 exclu, cf. plus bas),
//     dword_168744C (102 uniquement), dword_16747F0 (114, 1er gate), dword_168736C[0]/
//     dword_1687374[0] (rating d'attaque min/max, MEMES adresses que A_RatingBaseMin/Max
//     de Net/GameVarDispatch.cpp -- dupliquees ici en constantes locales, ce fichier ne
//     partage aucune adresse via header, meme convention partout ailleurs ici).
//   - 111/115 : notices "maitrise de branche" (StrTable005 1671..1675/2322), MEME forme que
//     96..100/110 (branche 0..3 -> label 1672..1675) mais SANS Skill_GetMotionId2/
//     dword_1685F94 -- purement une notification, aucun etat ecrit.
// EXCLUS de cette passe (documente, PAS un oubli -- cf. `default` de ApplyDuelBranchFamily) :
//   - 110 : appartient STRUCTURELLEMENT a la famille "branche de competence" 96..100
//     (dword_1685F94[8*a+b], Skill_GetMotionId2/Skill_IsAvailableByBranch) -- PAS a
//     "duel/defi" ni a "maitrise de branche", hors perimetre de cette mission (33..100
//     restent une zone dense non cablee pour une future vague, cf. Net/WorldEntityDispatch.h).
//   - 113 : ENTIEREMENT gate par `!Crt_Strcmp(dword_16746A8, v686)` (comparaison avec le nom
//     de guilde/affiliation LOCAL -- cf. Game/NameplateLogic.h, commentaire de tete : "semantique
//     incertaine ... a confirmer par un futur RE"), AUCUNE ecriture d'etat inconditionnelle a
//     preserver avant ce gate -- MEME statut que 425..428 (cf. bloc "Sous-opcodes 418..429" plus
//     haut) : rien de calculable sans fabriquer un champ non confirme -> PAS cable.
// Le gate `byte_1673184 == nom_paquet` (102/107/108/114) utilise g_World.self.localPlayerName
// (MEME champ que Game/GameState.h::SelfState::localPlayerName, deja documente comme
// "probablement byte_1673184" -- jamais peuple par aucun handler a ce jour dans ClientSource,
// donc ce gate echoue proprement/systematiquement tant que le paquet de login ne le peuple
// pas -- MEME politique de degradation honnete que le reste du fichier, PAS une regression
// introduite ici). Plusieurs `Crt_StringInit()` SANS argument visible (Hex-Rays a elide le
// pointeur `this`) precedent certaines ecritures dans le binaire (102/107/109/114) -- cibles
// NON identifiables depuis ce dump -> omises (TODO), SEULES les ecritures scalaires
// confirmees (dword_167xxxx) sont reproduites ci-dessous.
// ---------------------------------------------------------------------------
constexpr uint32_t kDuelStateA       = 0x16746B8; // dword_16746B8 (102/107/108/114)
constexpr uint32_t kDuelStateB       = 0x168726C; // dword_168726C (102/107/108/114)
constexpr uint32_t kDuelOpponentVal  = 0x1687450; // dword_1687450 (102/107/112/114)
constexpr uint32_t kDuelFlag744C     = 0x168744C; // dword_168744C (102 seulement)
constexpr uint32_t kDuelRatingMin    = 0x168736C; // dword_168736C[0] (== A_RatingBaseMin, GameVarDispatch.cpp)
constexpr uint32_t kDuelRatingMax    = 0x1687374; // dword_1687374[0] (== A_RatingBaseMax, GameVarDispatch.cpp)
constexpr uint32_t kDuelField16747F0 = 0x16747F0; // dword_16747F0 (114, 1er gate uniquement)

// Char_CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0) -- TODO(stat), MEME approximation que
// Net/GameVarDispatch.cpp::Char_CalcAttackRatingMin/Max (moteur de stats Char_Calc* non
// encore porte ; retourne la valeur derivee courante de self). Duplique localement --
// ce fichier ne partage pas ses helpers via header, meme convention partout ailleurs ici.
int DuelCalcAttackRatingMin() { return g_World.self.atkRatingMin; }
int DuelCalcAttackRatingMax() { return g_World.self.atkRatingMax; }

// Lit un champ nom/pseudo brut 13 o a `payload+offset`, NUL-termine defensivement (meme
// convention que le `tag13` standard du fichier, cf. ApplyFamily3TagSlot).
std::string ReadName13(const uint8_t* payload, uint32_t offset) {
    char buf[14] = {};
    std::memcpy(buf, payload + offset, 13);
    return std::string(buf);
}

// NETTOYAGE (audit "dispatch 0x5e" 2026-07-14) -- corps STRICTEMENT identique entre 111
// et 115 (branche 0..3=payload+4, tag=payload+8, HUD cat.0/type0 + chat), seul le
// suffixe change (str1671 pour 111, "maitrise de branche" ; str2322 pour 115) -- verifie
// instruction-a-instruction, cf. commentaires d'origine des deux cas. Factorise ici,
// AUCUN changement de comportement.
void ApplyBranchMasteryNotice(const uint8_t* payload, uint32_t len, int suffixStrId) {
    if (len < 21) return;
    int32_t branch = 0; std::memcpy(&branch, payload + 4, 4);
    if (branch < 0 || branch > 3) return; // hors 0..3 : le binaire reutilise un buffer non
                                           // re-initialise (meme convention que Apply754).
    static constexpr int kLabel[4] = {1672, 1673, 1674, 1675};
    const std::string tag = ReadName13(payload, 8);
    const std::string s = "[" + Str(kLabel[branch]) + "] " + tag + " " + Str(suffixStrId);
    g_Client.msg.Floating(0, 0, s);
    g_Client.msg.System(s);
}

void ApplyDuelBranchFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    switch (subOpcode) {
    case 101: { // dump L.2579 -- notification pure, aucun etat.
        if (len < 30) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(396));
        return;
    }
    case 102: { // dump L.2587 -- gate = suis-je la cible (nom1==mon nom) -> amorce le duel.
        if (len < 38) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        int32_t oppVal = 0, flag744C = 0;
        std::memcpy(&oppVal, payload + 30, 4);
        std::memcpy(&flag744C, payload + 34, 4);
        if (g_World.self.localPlayerName == name1) {
            // Identite d'affiliation de self <- name2 (var_54C = payload+17). RE-PROUVE W11
            // (WARP-03) : ces 4 Crt_StringInit etaient elides par Hex-Rays (this implicite) mais
            // le desassemblage les donne sans ambiguite. NB : kDuelState* est probablement un
            // MISNOMER (ce global 0x16746B8 est ecrit par le dispatcher GUILDE 0x53 et lu par de
            // l'UI clan) -> non renomme ici (toucherait 101..115, hors perimetre W11) ; SIGNALE.
            BlobStrcpy13(kLocalAffilName,      name2.c_str()); // 0x49b73e : 0x16746A8 <- name2
            g_Client.Var(kDuelStateA) = 2;                     // 0x49b746 : 0x16746B8 = 2
            BlobStrcpy13(kLocalAffilName2,     "");            // 0x49b75a : 0x16746BC <- ""
            SelfBodyStrcpy13(kSelfBodyAffil,   name2.c_str()); // 0x49b76e : 0x168725C <- name2
            g_Client.Var(kDuelStateB) = 2;                     // 0x49b776 : 0x168726C = 2
            SelfBodyStrcpy13(kSelfBodyAffil2,  "");            // 0x49b78a : 0x1687270 <- ""
            g_Client.Var(kDuelOpponentVal) = oppVal;           // 0x49b798 : 0x1687450 <- oppVal
            g_Client.Var(kDuelFlag744C) = flag744C;            // 0x49b7a3 : 0x168744C <- flag744C
            g_Client.Var(kDuelRatingMin) = DuelCalcAttackRatingMin();
            g_Client.Var(kDuelRatingMax) = DuelCalcAttackRatingMax();
        }
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(415));
        return;
    }
    case 103: { // dump L.2610 -- notification pure, aucun etat.
        if (len < 30) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(482));
        return;
    }
    case 104: { // dump L.2618 -- notification pure (1 seul nom), aucun etat.
        if (len < 17) return;
        g_Client.msg.System("[" + ReadName13(payload, 4) + "]" + Str(563));
        return;
    }
    case 105: { // dump L.2624 -- idem 104, suffixe distinct.
        if (len < 17) return;
        g_Client.msg.System("[" + ReadName13(payload, 4) + "]" + Str(480));
        return;
    }
    case 106: { // dump L.2630 -- idem 104, suffixe distinct.
        if (len < 17) return;
        g_Client.msg.System("[" + ReadName13(payload, 4) + "]" + Str(564));
        return;
    }
    case 107: { // dump L.2636 -- gate = suis-je la cible -> ANNULE le duel (etat=0).
        if (len < 30) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        if (g_World.self.localPlayerName == name1) {
            // Reset de l'identite d'affiliation de self (4 strcpy vers "" — WARP-03).
            BlobStrcpy13(kLocalAffilName,     "");             // 0x49ba22 : 0x16746A8 <- ""
            g_Client.Var(kDuelStateA) = 0;                     // 0x49ba2f : 0x16746B8 = 0
            BlobStrcpy13(kLocalAffilName2,    "");             // 0x49ba3e : 0x16746BC <- ""
            SelfBodyStrcpy13(kSelfBodyAffil,  "");             // 0x49ba50 : 0x168725C <- ""
            g_Client.Var(kDuelStateB) = 0;                     // 0x49ba5d : 0x168726C = 0
            SelfBodyStrcpy13(kSelfBodyAffil2, "");             // 0x49ba71 : 0x1687270 <- ""
            g_Client.Var(kDuelOpponentVal) = 0;                // 0x49ba79 : 0x1687450 = 0
            g_Client.Var(kDuelFlag744C) = 0;
        }
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(481));
        return;
    }
    case 108: { // dump L.2655 -- gate = suis-je la cible -> etat = code recu (v656, 1=accepte).
        if (len < 34) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        int32_t code = 0;
        std::memcpy(&code, payload + 30, 4);
        if (g_World.self.localPlayerName == name1) {
            g_Client.Var(kDuelStateA) = code;
            g_Client.Var(kDuelStateB) = code;
        }
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(code == 1 ? 554 : 555));
        return;
    }
    case 109: { // dump L.2672 -- gate = suis-je la cible (resets non modelisables, TODO ci-dessus) ;
                // suffixe selon un tag 5 o (payload+30) vide ou non (Crt_Strcmp(tag,"") : puisque
                // "" est vide, le resultat ne depend QUE du 1er octet de tag -- fait mecanique,
                // pas une supposition sur la semantique du champ).
        if (len < 35) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        const bool tagNonEmpty = (payload[30] != 0);
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(tagNonEmpty ? 561 : 562));
        return;
    }
    case 111: // dump L.2696 -- "maitrise de branche" : branche 0..3 -> label 1672..1675 (str1671).
        ApplyBranchMasteryNotice(payload, len, 1671);
        return;
    case 112: { // dump L.2725 -- ecriture inconditionnelle + message par table (1719+code).
                // NB : le nom du paquet (payload+4) est lu par le binaire mais jamais reutilise
                // dans le message ni l'etat -- lecture morte fidele (non recopiee ici).
        if (len < 21) return;
        int32_t code = 0; std::memcpy(&code, payload + 17, 4);
        g_Client.Var(kDuelOpponentVal) = code;
        g_Client.msg.System(Str(1719 + code));
        return;
    }
    case 114: { // dump L.2742 -- DEUX gates independants (aucun message) : nom1==moi -> reset ;
                // nom3==moi -> arme l'etat "en cours" (memes valeurs que 102/108).
        if (len < 47) return;
        const std::string name1 = ReadName13(payload, 4);
        const std::string name3 = ReadName13(payload, 30);
        int32_t v = 0; std::memcpy(&v, payload + 43, 4);
        if (g_World.self.localPlayerName == name1) {
            g_Client.Var(kDuelStateA) = 0;
            g_Client.Var(kDuelStateB) = 0;
            // TODO(@duel_stringinit_114) : 2x Crt_StringInit() sans cible identifiable, omis.
            g_Client.Var(kDuelField16747F0) = v;
        }
        if (g_World.self.localPlayerName == name3) {
            g_Client.Var(kDuelStateA) = 2;
            g_Client.Var(kDuelStateB) = 2;
        }
        return;
    }
    case 115: // dump L.2761 -- jumeau de 111 (str2322 au lieu de str1671).
        ApplyBranchMasteryNotice(payload, len, 2322);
        return;
    default:
        // 110 (famille "branche" 96-100, hors perimetre de cette mission) et 113 tombent ici
        // -- no-op fidele. NB : depuis W11, dword_16746A8 EST peuple (cases 1/4/6 guilde +
        // 102/107) donc le gate de 113 `!Crt_Strcmp(dword_16746A8, v686)` serait desormais
        // evaluable ; 113 reste neanmoins non porte (hors perimetre, cf. bandeau de tete).
        return;
    }
}

// ---------------------------------------------------------------------------
// Notifications finales 901..903 (dump L.5610-5630) -- banniere + ligne systeme,
// meme forme que le sous-opcode 401 (ApplyNotice401), aucun etat.
// ---------------------------------------------------------------------------
void Apply901to903(uint32_t subOpcode) {
    int strId = 0;
    switch (subOpcode) {
    case 901: strId = 2990; break;
    case 902: strId = 2991; break;
    case 903: strId = 2992; break;
    default: return;
    }
    const std::string s = Str(strId);
    g_Client.msg.Floating(2, 3, s);
    g_Client.msg.System(s);
}

} // namespace

void ApplyWorldEntityDispatch(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 4) return; // le binaire lit toujours au moins le sous-opcode

    uint32_t subOpcode = 0;
    std::memcpy(&subOpcode, payload, 4);

    // idx/arg2 : mêmes offsets (payload+4, payload+8) pour tous les slots des familles 1/2
    // (v702[0..3] / v702[4..7] dans le désasm). Lecture tolérante (paquet toujours 104 o
    // sur le fil, cf. Net/PacketDispatch.h::kPacketSize[0x5e]=105, mais défensive si appelé
    // avec un buffer plus court, ex. tests).
    uint32_t idx = 0, arg2 = 0, arg3 = 0;
    if (len >= 8)  std::memcpy(&idx,  payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    // arg3 = payload+12 en ENTIER (pas le tag13 ci-dessous -- reinterpretation du meme
    // champ brut, meme convention que le reste du fichier) : seulement lu par 59/63
    // (mission "sous-plage 51-75").
    if (len >= 16) std::memcpy(&arg3, payload + 12, 4);
    // Tag texte 13 o (payload+12..+24) : uniquement lu par le sous-opcode 25 (famille 3,
    // cf. ApplyFamily3TagSlot). Zero-termine defensivement (le binaire ne le fait pas
    // explicitement, mais le champ paquet est un nom fixe potentiellement pad de nuls).
    char tag13[14] = {};
    if (len >= 25) std::memcpy(tag13, payload + 12, 13);

    if (subOpcode >= 1 && subOpcode <= 9) {
        ApplyComboFamilySlot(kFamily1, static_cast<int>(subOpcode), idx, arg2);
    } else if (subOpcode >= 10 && subOpcode <= 18) {
        ApplyComboFamilySlot(kFamily2, static_cast<int>(subOpcode) - 9, idx, arg2);
    } else if (subOpcode >= 19 && subOpcode <= 24) {
        // Famille 3, slots 1..6 (meme forme mecanique que familles 1/2 ; strSlot6=235
        // comme famille 2, pas de demi-duree au slot4 comme famille 2 -- verifie EA
        // 0x4958a0..0x495d51).
        ApplyComboFamilySlot(kFamily3, static_cast<int>(subOpcode) - 18, idx, arg2);
    } else if (subOpcode == 25) {
        ApplyFamily3TagSlot(idx, arg2, tag13);
    } else if (subOpcode == 26) {
        ApplyFamily3Slot26(idx);
    } else if (subOpcode == 27) {
        ApplyFamily3Slot27(idx, arg2);
    } else if (subOpcode == 28) {
        // Famille 3, reprise du slot7 standard apres les 3 sous-cas 25..27 (EA
        // 0x496277..0x49636d) -- MAIS avec un tag 13 o (payload+12) et un message/timer
        // PROPRES (str246, timer dword_1675BE4/flt_1675BE8) divergents du slot7 generique
        // -> route vers ApplyFamily3Slot28 (PAS ApplyComboFamilySlot), desormais cablee
        // message compris (SkillLevelTable+ElementPairTable exposees, cf. bandeau de tete).
        ApplyFamily3Slot28(idx, arg2, tag13);
    } else if (subOpcode >= 29 && subOpcode <= 30) {
        // Famille 3, slots 8..9 standard (EA 0x4962d3..0x49636d) -- forme IDENTIQUE aux
        // familles 1/2, aucune divergence (contrairement au slot7/28 ci-dessus).
        ApplyComboFamilySlot(kFamily3, static_cast<int>(subOpcode) - 21, idx, arg2);
    } else if (subOpcode == 31) {
        // Toggle "exclusion" (branche, type) -- cf. ApplyExclusionToggleSlot, EA 0x4963b0.
        ApplyExclusionToggleSlot(idx, arg2, 0);
    } else if (subOpcode == 32) {
        // Jumeau structurel de 31 (EA 0x496583), valeur ecrite=1, libelles 255..258.
        ApplyExclusionToggleSlot(idx, arg2, 1);
    } else if (subOpcode >= 33 && subOpcode <= 45) {
        // Notices/etat "loadout d'element" (mission "loadout d'element", 2026-07-14) --
        // cf. ApplyElementLoadoutFamily pour le detail (aucun rapport avec les familles
        // de combo/Special/Buff ni avec Char_GetPairedElement).
        ApplyElementLoadoutFamily(subOpcode, payload, len);
    } else if (subOpcode == 46 || subOpcode == 47) {
        // Appariement d'alliance par element -- ALIMENTE ElementPairTable
        // (Game/SkillCombat.h::Combat_ReadLocalElementPairs()) via les MEMES
        // adresses que Char_GetPairedElement/Combat_IsElementAllowedOnMap, cf.
        // ApplyAlliancePairFamily plus haut et Docs/TS2_COMBAT_ELEMENT_GATING.md.
        ApplyAlliancePairFamily(subOpcode, idx, arg2, payload, len);
    } else if (subOpcode >= 48 && subOpcode <= 50) {
        // Tail-merge de l'appariement d'alliance (49 reprend l'etat de 47) -- cf.
        // ApplyAllianceLabelFamily, mission "loadout d'element" 2026-07-14.
        ApplyAllianceLabelFamily(subOpcode, payload, len);
    } else if (subOpcode == 51) {
        Apply51(payload, len); // tag a payload+8, PAS +12 (offset irregulier, cf. Apply419).
    } else if (subOpcode == 52) {
        // 52 EST DISTINCT de 53..55 : en plus du palier, RAZ integrale du scoreboard (40 slots,
        // dword_168653C/16865DC/168667C) -- verifie desasm (boucle i:0..4 x j:0..10), PAS un
        // dead-store comme le HUD_ShowFloatingMessage final (v677 jamais rempli) -- cf. bandeau
        // ApplyAllianceTierReset/ApplyScoreboardFullReset.
        ApplyAllianceTierReset(1);
        ApplyScoreboardFullReset();
    } else if (subOpcode >= 53 && subOpcode <= 55) {
        // Reinitialisation de palier d'alliance seule -- effets annexes (StrTable005_Get/
        // nettoyages de grille/recherche "meilleur slot") CONFIRMES DEAD, cf. bandeau
        // ApplyAllianceTierReset (53/54 n'ont PAS la boucle de RAZ scoreboard de 52 ; 55 a une
        // boucle DIFFERENTE, elle-meme dead).
        ApplyAllianceTierReset(static_cast<int32_t>(subOpcode) - 51); // 53->2,54->3,55->4
    } else if (subOpcode == 56) {
        ApplyAllianceTierReset(0);
    } else if (subOpcode == 57) {
        Apply57(payload, len);
    } else if (subOpcode == 58) {
        Apply58(idx, arg2); // message omis (buffer nom byte_1686334 non ecrit ici).
    } else if (subOpcode == 59) {
        Apply59(idx, arg2, static_cast<int32_t>(arg3));
    } else if (subOpcode == 60) {
        ApplyClassTagNotice(idx, tag13, 773);
    } else if (subOpcode == 61) {
        ApplyClassTagNotice(idx, tag13, 774);
    } else if (subOpcode == 62) {
        Apply62(idx, static_cast<int32_t>(arg2));
    } else if (subOpcode == 63) {
        // Famille "branche de competence" (Skill_GetMotionId2) -- preambule "poll" AVANT le
        // cycle 69..100, cf. bandeau ApplyBranchPoll.
        ApplyBranchPoll(idx, arg2, arg3);
    } else if (subOpcode == 64) {
        ApplyBranchArm(idx, arg2, 1, 232, 2);
    } else if (subOpcode == 65) {
        ApplyBranchArm(idx, arg2, 2, 233, 3);
    } else if (subOpcode == 66) {
        // 1re iteration du cycle "flagsA" (etat=3) -- reutilise ApplyBranchFlagsSlot (meme
        // fonction que 80/87/94, cf. bandeau de tete du bloc 63..68).
        ApplyBranchFlagsSlot(idx, arg2, 3, 0x1675C0C, 0x1675C10);
    } else if (subOpcode == 67) {
        ApplyBranchFlagsSlot(idx, arg2, 4, 0x1675C34, 0x1675C38); // 1re iteration "flagsB".
    } else if (subOpcode == 68) {
        ApplyBranchSoundOnlySlot(idx, arg2, 5); // 1re iteration "soundOnly".
    } else if (subOpcode == 69) {
        // Cycle "branche de competence" 69..100 -- 1re iteration, slot "probe" (etat=23,
        // resultat de Skill_IsAvailableByBranch jete, cf. ApplyBranchProbe/bandeau 76..100).
        ApplyBranchProbe(idx, arg2);
    } else if (subOpcode == 70) {
        ApplyBranchLabel418(idx, arg2); // 1re iteration "L418" (str235 + retour ville).
    } else if (subOpcode == 71) {
        ApplyBranchLabel420(idx, arg2, 6); // 1re iteration "L420" (son seul, etat=6).
    } else if (subOpcode == 72) {
        ApplyBranchLabel422(idx, arg2); // 1re iteration "L422" (str237 + retour ville).
    } else if (subOpcode == 73) {
        ApplyBranchFlagsSlot(idx, arg2, 7, 0x1675C14, 0x1675C18); // 2e iteration "flagsA".
    } else if (subOpcode == 74) {
        ApplyBranchFlagsSlot(idx, arg2, 8, 0x1675C3C, 0x1675C40); // 2e iteration "flagsB".
    } else if (subOpcode == 75) {
        ApplyBranchSoundOnlySlot(idx, arg2, 9); // 2e iteration "soundOnly".
    } else if (subOpcode == 76 || subOpcode == 83 || subOpcode == 90 || subOpcode == 97) {
        // Famille "branche de competence" (Skill_GetMotionId2), slot "probe" -- cf.
        // ApplyBranchProbe en tete de fichier pour le cycle complet 76..100.
        ApplyBranchProbe(idx, arg2);
    } else if (subOpcode == 77 || subOpcode == 84 || subOpcode == 91 || subOpcode == 98) {
        ApplyBranchLabel418(idx, arg2);
    } else if (subOpcode == 78) {
        ApplyBranchLabel420(idx, arg2, 10);
    } else if (subOpcode == 85) {
        ApplyBranchLabel420(idx, arg2, 14);
    } else if (subOpcode == 92) {
        ApplyBranchLabel420(idx, arg2, 18);
    } else if (subOpcode == 99) {
        ApplyBranchLabel420(idx, arg2, 22);
    } else if (subOpcode == 79 || subOpcode == 86 || subOpcode == 93 || subOpcode == 100) {
        ApplyBranchLabel422(idx, arg2);
    } else if (subOpcode == 80) {
        ApplyBranchFlagsSlot(idx, arg2, 11, 0x1675C1C, 0x1675C20);
    } else if (subOpcode == 87) {
        ApplyBranchFlagsSlot(idx, arg2, 15, 0x1675C24, 0x1675C28);
    } else if (subOpcode == 94) {
        ApplyBranchFlagsSlot(idx, arg2, 19, 0x1675C2C, 0x1675C30);
    } else if (subOpcode == 81) {
        ApplyBranchFlagsSlot(idx, arg2, 12, 0x1675C44, 0x1675C48);
    } else if (subOpcode == 88) {
        ApplyBranchFlagsSlot(idx, arg2, 16, 0x1675C4C, 0x1675C50);
    } else if (subOpcode == 95) {
        ApplyBranchFlagsSlot(idx, arg2, 20, 0x1675C54, 0x1675C58);
    } else if (subOpcode == 82) {
        ApplyBranchSoundOnlySlot(idx, arg2, 13);
    } else if (subOpcode == 89) {
        ApplyBranchSoundOnlySlot(idx, arg2, 17);
    } else if (subOpcode == 96) {
        ApplyBranchSoundOnlySlot(idx, arg2, 21);
    } else if (subOpcode >= 101 && subOpcode <= 115) {
        // Duel/defi (101-109/112/114) + "maitrise de branche" (111/115) -- cf.
        // ApplyDuelBranchFamily pour le detail (110 hors perimetre de cette sous-plage :
        // famille "branche" 76-100 ci-dessus ; 113 gate entierement sur un buffer
        // indisponible, cf. bandeau de tete de ApplyDuelBranchFamily).
        ApplyDuelBranchFamily(subOpcode, payload, len);
    } else if (subOpcode >= 201 && subOpcode <= 208) {
        // Famille "arene individuelle" (morph fixe 194) -- cf. ApplyIndividualArenaFamily.
        ApplyIndividualArenaFamily(subOpcode, idx);
    } else if (subOpcode == 401) {
        ApplyNotice401();
    } else if (subOpcode >= 402 && subOpcode <= 410) {
        // Famille "Special" (Skill_GetSpecialMotionId), meme forme mecanique que les
        // familles de combo -- cf. ApplySpecialFamilySlot pour les 3 divergences.
        ApplySpecialFamilySlot(static_cast<int>(subOpcode) - 401, idx, arg2);
    } else if (subOpcode >= 755 && subOpcode <= 763) {
        // Famille de combo 4 (arme 4, Skill_GetComboMotionId(4, idx)) -- cf.
        // ApplyComboFamily4Slot pour les 2 divergences (pas d'horodatage, slot4/slot7
        // enrichis).
        ApplyComboFamily4Slot(static_cast<int>(subOpcode) - 754, idx, arg2);
    } else if (subOpcode >= 411 && subOpcode <= 415) {
        // Famille "Buff" (Skill_GetBuffMotionId) -- cf. ApplyBuffFamilySlot pour la forme
        // (5 sous-cas, aucun gate de disponibilite, tag13 = payload+12 comme sous-opcode 25).
        ApplyBuffFamilySlot(static_cast<int>(subOpcode) - 410, idx, tag13);
    } else if (subOpcode == 416) {
        ApplyMiscFlagSlot(idx, 1); // dword_1686120[idx]=1, SANS rapport avec la famille Buff.
    } else if (subOpcode == 417) {
        ApplyMiscFlagSlot(idx, 0); // dword_1686120[idx]=0.
    } else if (subOpcode == 418) {
        Apply418(static_cast<int32_t>(idx));
    } else if (subOpcode == 419) {
        Apply419(payload, len); // tag a payload+8, PAS +12 (cf. commentaire ApplyWorldEntityDispatch).
    } else if (subOpcode == 420) {
        Apply420(static_cast<int32_t>(idx));
    } else if (subOpcode == 421) {
        Apply421();
    } else if (subOpcode == 422 || subOpcode == 424) {
        Apply422Or424(); // etat seul -- message/warp omis (buffer byte_1686138 non recu ici).
    } else if (subOpcode == 423 || subOpcode == 429) {
        ApplyZone291CountNotice(static_cast<int32_t>(idx)); // tail-merge LABEL_135.
    } else if (subOpcode == 301) {
        Apply301(idx, arg2);
    } else if (subOpcode == 302) {
        Apply302(idx, static_cast<int32_t>(arg2));
    } else if (subOpcode == 500) {
        Apply500();
    } else if (subOpcode >= 601 && subOpcode <= 610) {
        ApplyLevelBonusFamily(subOpcode, payload, len);
    } else if (subOpcode >= 611 && subOpcode <= 615) {
        ApplyElementNotifyFamily(subOpcode, idx, arg2);
    } else if (subOpcode == 620) {
        Apply620(idx);
    } else if (subOpcode == 621) {
        Apply621();
    } else if (subOpcode == 622) {
        Apply622();
    } else if (subOpcode == 624) {
        Apply624(idx);
    } else if (subOpcode == 625) {
        Apply625(idx);
    } else if (subOpcode == 626) {
        Apply626();
    } else if (subOpcode == 628) {
        Apply628(payload, len);
    } else if (subOpcode >= 629 && subOpcode <= 652) {
        // Famille "declaration de guerre / palier" -- cf. ApplyWarStageFamily pour la
        // forme (etat dword_168618C[elt] + TODO precis omis, en tete de la fonction).
        ApplyWarStageFamily(subOpcode, payload, len);
    } else if (subOpcode >= 659 && subOpcode <= 669) {
        ApplyArenaFamily(subOpcode, payload, len);
    } else if (subOpcode >= 671 && subOpcode <= 677) {
        ApplyClassTagFamily(subOpcode, payload, len);
    } else if (subOpcode >= 700 && subOpcode <= 729) {
        ApplySiegeStage2Family(subOpcode, payload, len);
    } else if (subOpcode >= 740 && subOpcode <= 749) {
        ApplyRankFamily740(subOpcode, payload, len);
    } else if (subOpcode == 751) {
        Apply751(payload, len);
    } else if (subOpcode == 752 || subOpcode == 753) {
        ApplyRankTable(subOpcode, payload, len);
    } else if (subOpcode == 754) {
        Apply754(payload, len);
    } else if (subOpcode >= 771 && subOpcode <= 774) {
        ApplyCastAnnounce771to774(subOpcode, payload, len);
    } else if (subOpcode == 788 || subOpcode == 789 || subOpcode == 790) {
        ApplySimpleSkillNotice(subOpcode, payload, len);
    } else if (subOpcode == 791) {
        Apply791(payload, len);
    } else if (subOpcode >= 792 && subOpcode <= 794) {
        Apply792to794(subOpcode, payload, len);
    } else if (subOpcode == 795) {
        Apply795(payload, len);
    } else if (subOpcode >= 780 && subOpcode <= 786) {
        ApplyWarEvent780to786(subOpcode, payload, len);
    } else if (subOpcode >= 800 && subOpcode <= 807) {
        // 803 confirme no-op (dead store cote binaire) -- couvert par ApplyWarEvent800to807
        // qui tombe sur son propre `default: return;` pour cette valeur.
        ApplyWarEvent800to807(subOpcode, payload, len);
    } else if (subOpcode >= 901 && subOpcode <= 903) {
        Apply901to903(subOpcode);
    }
    // 33..45 CABLE (ApplyElementLoadoutFamily), 46..47 CABLE (ApplyAlliancePairFamily,
    // mission "cablage ElementPairTable" 2026-07-14 -- appariement d'alliance, alimente
    // desormais ElementPairTable en donnees reelles, cf. Game/SkillCombat.h), 48..50 CABLE
    // (ApplyAllianceLabelFamily) -- l'ensemble 33..50 est CABLE (mission "loadout d'element"
    // 2026-07-14). 51..75 et 101..115 dense CABLE ci-dessus egalement (301-302/425-428/500/
    // 600/764-770 -- no-op/sparse confirmes ; 76..100 CABLE
    // ci-dessus, mission "sous-plage 76..100" 2026-07-14 ; 201..208 CABLE ci-dessus,
    // famille "arene individuelle" morph fixe 194, mission "201-208" 2026-07-14) : no-op
    // fidele au `default: return;` d'origine pour tout sous-opcode restant non liste. 116..200
    // CONFIRME VIDE (aucun `case` dans le desasm, survol 2026-07-14) -- pas un TODO,
    // comportement terminal. 425-428 : gate ENTIEREMENT sur byte_1686138 (buffer nom non
    // recu ici, cf. commentaire de tete de ApplyZone291CountNotice/Apply422Or424 plus haut) --
    // aucune ecriture d'etat inconditionnelle a preserver, contrairement a 422/424.
}

} // namespace ts2::game

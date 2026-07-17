// Net/CombatResultApply.cpp — traduction byte-exacte de cGameData_ApplyCombatResult
// (EA 0x55A380) + trampoline Pkt_OnCombatResult (EA 0x468340, op 0x15).
//
// SOURCE DE VÉRITÉ : désassemblage IDA (idaTs2). Chaque champ du bloc de 76 o, chaque
// seuil, chaque ordre d'opération et chaque clamp est repris tel quel du décompilé.
//
// -----------------------------------------------------------------------------
// STRUCTURE DU BLOC (76 o = 19 DWORD, tous lus en _DWORD dans le binaire)
//   a[0]  (+0x00) type d'action        : 1/2 = attaque PvP, 3 = cible monstre,
//                                        4 = attaquant monstre. (switch @0x55A3CA)
//   a[1]  (+0x04) attaquant.id.hi      (comparé à player[i]+6896)
//   a[2]  (+0x08) attaquant.id.lo      (comparé à player[i]+6900)
//   a[3]  (+0x0C) cible.id.hi          (comparé à player/monstre +6896 / +923696)
//   a[4]  (+0x10) cible.id.lo          (comparé à +6900 / +923700)
//   a[5..7]       inutilisés par ce décodeur
//   a[8]  (+0x20) catégorie d'attaque  : 1 = arme, 2 = compétence (choix son/fx)
//   a[9]  (+0x24) id de compétence     (variante son/fx ; 78 = cas ignoré)
//   a[10..11]     inutilisés
//   a[12] (+0x30) « hit » : 0 = raté/esquivé ; sinon = (idArme+1) -> MobDb_GetEntry
//   a[13] (+0x34) drapeau critique     : bit0 = crit externe, bit1 = crit interne
//                                        (0=aucun, 1=ext, 2=int, 3=les deux)
//   a[14] (+0x38) défense / absorbé
//   a[15] (+0x3C) total dégâts externes (avant défense)  -> texte journal
//   a[16] (+0x40) dégâts externes APPLIQUÉS aux PV
//   a[17] (+0x44) total dégâts internes (avant défense)  -> texte journal
//   a[18] (+0x48) dégâts internes APPLIQUÉS aux PV
//
// NB fidélité : les PV joueur subissent a[16] PUIS a[18] (les deux sur le même champ
// PV +7208) ; les PV monstre ne subissent que a[16]. Aucun champ MP n'est touché par
// ce paquet. Le « self » est l'entité players[0] (== cGameData player[0]).
// -----------------------------------------------------------------------------
#include "Net/CombatResultApply.h"

#include "Game/GameState.h"      // g_World, PlayerEntity, MonsterEntity, EntityId
#include "Game/ClientRuntime.h"  // g_Client, game::Str(id)
#include "Gfx/FxSetters.h"       // FxPool_* (pool dword_17D06F4) + Fx_Attach* (setters combat)

#include <cstdio>   // snprintf
#include <cstdint>
#include <cstring>  // std::memcpy — miroir de Crt_Memcpy(..., a4, 0x4Cu)
#include <array>
#include <string>
#include <unordered_map>

namespace ts2::game {
namespace {

// Adresse d'origine du drapeau « joueur local mort » (dword_16760D0), rangé dans la
// longue traîne de globals via g_Client.Var(addr) — cf. RÈGLE D'OR ClientRuntime.h.
constexpr uint32_t kAddr_SelfDeadFlag = 0x16760D0;

// Couleur/catégorie de ligne de journal. Le binaire passe un code de catégorie à
// Msg_AppendSystemLine (EA 0x68D9D0) qui le stocke tel quel (+37272) ; le rendu
// résout ensuite la couleur par une table indexée sur ce code. Faute de cette table
// ici, on mappe les 4 catégories utilisées vers des ARGB de substitution STABLES.
// TODO(couleur) : câbler la vraie table catégorie->D3DCOLOR du binaire.
uint32_t CouleurLigne(int categorie) {
    switch (categorie) {
        case 2:  return 0xFFFF6060u; // dégâts subis (self = cible)
        case 3:  return 0xFFFFC040u; // dégâts infligés (self = attaquant)
        case 17: return 0xFFB0B0B0u; // « vous avez raté » (self attaque, miss)
        case 21: return 0xFFB0B0B0u; // « l'attaquant vous a raté » (self cible, miss)
        default: return 0xFFFFFFFFu;
    }
}

// Émet une ligne du journal système (Msg_AppendSystemLine(texte, categorie)).
void LigneSysteme(const std::string& texte, int categorie) {
    g_Client.msg.System(texte, CouleurLigne(categorie));
}

// snprintf sur les formats "%s(+%d)" / "%s(-%d)" / " %s(+%d)" / " %s(-%d)"
// (Crt_Vsnprintf @0x75CD5F) puis concaténation (Crt_Strcat @0x75CAC0).
std::string Fmt(const char* format, const std::string& s, int d) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), format, s.c_str(), d);
    return std::string(buf);
}

// Recherche linéaire d'un joueur actif par identité réseau. Renvoie l'index (ordre
// de g_World.players, index 0 = self) ou -1. Reproduit les boucles de scan du binaire
// (test player[i].active && id.hi==a[hi] && id.lo==a[lo]).
int TrouverJoueur(const EntityId& id) {
    for (size_t i = 0; i < g_World.players.size(); ++i) {
        const PlayerEntity& p = g_World.players[i];
        if (p.active && p.id.hi == id.hi && p.id.lo == id.lo)
            return static_cast<int>(i);
    }
    return -1;
}

// Idem pour les monstres (tableau stride 280 d'origine).
int TrouverMonstre(const EntityId& id) {
    for (size_t i = 0; i < g_World.monsters.size(); ++i) {
        const MonsterEntity& m = g_World.monsters[i];
        if (m.active && m.id.hi == id.hi && m.id.lo == id.lo)
            return static_cast<int>(i);
    }
    return -1;
}

// « self » = players[0] (== cGameData player[0]).
bool SelfEst(const EntityId& id) {
    return !g_World.players.empty() && g_World.players[0].id.hi == id.hi
                                    && g_World.players[0].id.lo == id.lo;
}

// self.mode (cGameData+7136) : gate les renvois réseau Net_QueueAction9/10 pour les
// modes {1,5,6,7}. Lu depuis g_World.self.mode (GameState.h ligne 334, lecture seule ;
// champ ajouté par MAIN, miroir de cGameData+7136). Gate identique aux 4 sites du binaire :
//   0x55AA5D (cas 1/2 touche) / 0x55A4D0 (cas 1/2 raté) /
//   0x55BC38 (cas 4 touche)  / 0x55BAA3 (cas 4 raté).
bool SelfModeDeclenche() {
    const int m = g_World.self.mode;             // cGameData+7136 (g_World.self.mode)
    return m == 1 || m == 5 || m == 6 || m == 7; // {1,5,6,7}
}

// Cache du dernier bloc de combat 76 o par entité — miroir de Crt_Memcpy(..., a4, 0x4Cu)
// vers player[i]+7536 (0x55AC92, 0x55BE85) et monstre[i]+923816 (0x55B51F). GameState.h
// (PlayerEntity/MonsterEntity) est en lecture seule pour ce front -> l'état est modélisé
// ici (règle #6). Clef = identité réseau (hi<<32 | lo). Consommateur futur : anim/FX.
struct BlocCombat { std::array<uint8_t, 76> data{}; bool valid = false; };

std::unordered_map<uint64_t, BlocCombat>& CacheJoueur() {
    static std::unordered_map<uint64_t, BlocCombat> m; return m;
}
std::unordered_map<uint64_t, BlocCombat>& CacheMonstre() {
    static std::unordered_map<uint64_t, BlocCombat> m; return m;
}
uint64_t CleId(const EntityId& id) {
    return (static_cast<uint64_t>(id.hi) << 32) | id.lo;
}
void StockerBlocCombat(std::unordered_map<uint64_t, BlocCombat>& cache,
                       const EntityId& id, const uint8_t* block) {
    BlocCombat& b = cache[CleId(id)];
    std::memcpy(b.data.data(), block, 76);   // Crt_Memcpy(..., a4, 0x4Cu)
    b.valid = true;
}

// -----------------------------------------------------------------------------
// Vue FxEntitySource (champs a2[N] lus par les setters Fx_Attach*) depuis une entité C++.
// Les setters reçoivent l'ADRESSE du record (&player[i]=this+908·i+6892 / &monster[i]=this+
// 280·i+923692) ; on n'y transpose que les champs prouvés. Cf. Gfx/FxSetters.h.
gfx::FxEntitySource SourceFromPlayer(const PlayerEntity& p) {
    gfx::FxEntitySource s;
    s.idHi = p.id.hi;                       // a2[1]  entity+4
    s.idLo = p.id.lo;                       // a2[2]  entity+8
    // a2[6] = entity+24 = body[0] : gate « modèle chargé » (non nul quand l'apparence est reçue).
    if (p.body.size() >= 4)
        std::memcpy(&s.modelReady, p.body.data(), 4);
    s.modelClass    = p.anim.modelIndex;    // a2[23] entity+92 (aTribe RACE 0..2)
    s.modelSubclass = p.anim.modelVariant;  // a2[24] entity+96 (aGender 0..1)
    return s;
}
gfx::FxEntitySource SourceFromMonster(const MonsterEntity& m) {
    gfx::FxEntitySource s;
    s.idHi = m.id.hi;   // a2[1]  record+4
    s.idLo = m.id.lo;   // a2[2]  record+8
    // Les setters monstre (MuzzleVariant particule / Parry-Deflect mesh) ne lisent que l'id
    // pour le chemin VISIBLE ; l'ancre (a2[24]=record+96=def modèle, +244) relève du sous-
    // système modèle → TODO côté setter. modelClass/Subclass non transposés (non requis).
    return s;
}

// Alloue un slot FX libre et renvoie son pointeur, ou nullptr si le pool est plein. Miroir du
// motif partagé `for(j…) if(j<g_FxAuraCount) Fx_AttachXxx(&slot[j], …)` (ex. @0x55ab24/@0x55ab82).
gfx::FxSlot* SlotLibreFx() {
    const int j = gfx::FxPool_FindFreeSlot();
    return (j >= 0) ? &gfx::FxPool_Slots()[j] : nullptr;
}

} // namespace

// =============================================================================
// cGameData_ApplyCombatResult — EA 0x55A380
// =============================================================================
void ApplyCombatResult(const uint8_t* block, uint32_t len) {
    if (!block || len < 76)
        return;

    // Tous les champs sont lus en 32 bits (signés lorsque comparés à 0 / soustraits).
    const int32_t* a = reinterpret_cast<const int32_t*>(block);

    const EntityId atk{ static_cast<uint32_t>(a[1]), static_cast<uint32_t>(a[2]) };
    const EntityId tgt{ static_cast<uint32_t>(a[3]), static_cast<uint32_t>(a[4]) };

    switch (a[0]) {                         // switch @0x55A3CA
    // -------------------------------------------------------------------------
    // Cas 1 & 2 : attaque joueur -> joueur.
    // -------------------------------------------------------------------------
    case 1:
    case 2:
        if (a[12]) {                        // touche (@0x55A3D4)
            // --- Bloc A : self est l'ATTAQUANT -> journal « dégâts infligés » ---
            if (SelfEst(atk)) {             // @0x55A510
                std::string s = Str(198);
                int v33 = a[15] - a[14];    // total ext - défense (@0x55A541)
                if (v33 < 0) v33 = 0;
                if (a[13] == 1 || a[13] == 3)               // crit externe
                    s += Fmt("%s(+%d)", Str(200), v33);
                else
                    s += Fmt("%s(+%d)", Str(199), v33);
                if (a[14] > 0)                              // part absorbée
                    s += Fmt(" %s(+%d)", Str(201), a[14]);
                LigneSysteme(s, 3);

                if (a[17] > 0) {                            // volet dégâts internes
                    std::string s2 = Str(2793);
                    int v34 = a[17] - a[14];                // (@0x55A67E) avec clamp
                    if (v34 < 0) v34 = 0;
                    if (a[13] == 2 || a[13] == 3)           // crit interne
                        s2 += Fmt("%s(+%d)", Str(200), v34);
                    else
                        s2 += Fmt("%s(+%d)", Str(199), v34);
                    if (a[14] > 0)
                        s2 += Fmt(" %s(+%d)", Str(201), a[14]);
                    LigneSysteme(s2, 3);
                }
            }

            // --- Bloc B : self est la CIBLE -> journal « dégâts subis » ---
            if (SelfEst(tgt)) {             // @0x55A7AD
                std::string s = Str(203);
                if (a[13] == 1 || a[13] == 3) {             // crit externe
                    int v20 = a[15] - a[14];                // (@0x55A7F0) SANS clamp
                    s += Fmt("%s(-%d)", Str(200), v20);
                } else {
                    int v21 = a[15] - a[14];                // (@0x55A839) SANS clamp
                    s += Fmt("%s(-%d)", Str(199), v21);
                }
                if (a[14] > 0)
                    s += Fmt(" %s(-%d)", Str(201), a[14]);
                LigneSysteme(s, 2);

                if (a[17] > 0) {                            // volet dégâts internes
                    std::string s2 = Str(2793);
                    if (a[13] == 2 || a[13] == 3) {
                        int v23 = a[17] - a[14];            // (@0x55A91B) SANS clamp
                        s2 += Fmt("%s(-%d)", Str(200), v23);
                    } else {
                        int v24 = a[17] - a[14];            // (@0x55A964) SANS clamp
                        s2 += Fmt("%s(-%d)", Str(199), v24);
                    }
                    if (a[14] > 0)
                        s2 += Fmt(" %s(-%d)", Str(201), a[14]);
                    LigneSysteme(s2, 2);
                }

                // Drapeau « mort » : PV self courants - dégâts ext - dégâts int < 1
                // (@0x55AA17). Lit players[0].hp AVANT la soustraction du bas.
                const int hpSelf = g_World.players.empty() ? 0 : g_World.players[0].hp;
                if (hpSelf - a[16] - a[18] < 1)
                    g_Client.Var(kAddr_SelfDeadFlag) = 1;   // dword_16760D0 = 1

                if (SelfModeDeclenche()) {
                    // TODO(net) @0x55AA64 : Net_QueueAction10(&g_PlayerCmdController)
                }
            }

            // --- Application des dégâts sur le JOUEUR cible (a[3],a[4]) ---
            {
                int i = TrouverJoueur(tgt);                 // boucle @0x55AA69
                if (i >= 0) {
                    PlayerEntity& p = g_World.players[i];
                    if (a[13] == 1) {
                        // @0x55ABC4 : parade sur crit externe. Scan slot libre @0x55ab24 puis
                        //   Fx_AttachBlockGuard(&slot, 1, &player[i], i==0).
                        if (gfx::FxSlot* fx = SlotLibreFx())
                            gfx::Fx_AttachBlockGuard(fx, 1, SourceFromPlayer(p), i == 0);
                    }
                    p.hp -= a[16];          // dégâts externes (@0x55ABFA)
                    p.hp -= a[18];          // dégâts internes  (@0x55AC32)
                    if (p.hp < 0) p.hp = 0;
                    if (i == 0) g_World.self.hp = p.hp;     // miroir HUD self
                    // Crt_Memcpy(player[i]+7536, block, 76) @0x55AC92 — cache du bloc
                    //   de combat sur l'entité (modélisé hors-struct, cf. règle #6).
                    StockerBlocCombat(CacheJoueur(), tgt, block);
                    // @0x55AD2C : muzzle sur la cible touchée. Scan slot libre @0x55ac9a puis
                    //   Fx_AttachMuzzleFlash(&slot, &player[i], 1).
                    if (gfx::FxSlot* fx = SlotLibreFx())
                        gfx::Fx_AttachMuzzleFlash(fx, SourceFromPlayer(p), 1);
                }
            }

            // --- Son positionnel sur le JOUEUR attaquant (a[1],a[2]) ---
            {
                int i = TrouverJoueur(atk);                 // boucle @0x55AD31
                if (i >= 0) {
                    if (a[8] == 2 && a[9] == 78)
                        return;             // (@0x55AF11) cas compétence 78 : abandon
                    // TODO(fx) @0x55AF99/0x55B00D : sélection d'index sonore
                    //   (player[i]+6984/+6988, MobDb_GetEntry(mITEM, a[12]-1)+188,
                    //   Skill_GetEffectVariant(a[9])) puis Snd3D_PlayPositional dans
                    //   flt_149B03C ; +432 = variante « parade » si a[13]==1.
                }
            }
        } else {
            // --- Raté / esquive (a[12] == 0) ---
            if (SelfEst(atk)) {             // @0x55A404
                LigneSysteme(Str(197), 17); // « vous avez raté »
            }
            if (SelfEst(tgt)) {             // @0x55A462
                LigneSysteme(Str(202), 21); // « l'attaquant vous a raté »
                if (SelfModeDeclenche()) {
                    // TODO(net) @0x55A4D7 : Net_QueueAction9(&g_PlayerCmdController)
                }
            }
        }
        return;

    // -------------------------------------------------------------------------
    // Cas 3 : attaque joueur -> MONSTRE.
    // -------------------------------------------------------------------------
    case 3:
        if (!a[12]) {                       // raté (@0x55B01A)
            if (SelfEst(atk))               // @0x55B046
                LigneSysteme(Str(197), 17); // « vous avez raté »
            return;
        }
        // Touche : journal côté attaquant (self).
        if (SelfEst(atk)) {                 // @0x55B0AE
            std::string s = Str(198);
            int v32 = a[15] - a[14];        // (@0x55B0DF) avec clamp
            if (v32 < 0) v32 = 0;
            if (a[13])                                      // crit (bit quelconque)
                s += Fmt("%s(+%d)", Str(200), v32);
            else
                s += Fmt("%s(+%d)", Str(199), v32);
            if (a[14] > 0)
                s += Fmt(" %s(+%d)", Str(201), a[14]);
            LigneSysteme(s, 3);
        }

        // Application des dégâts sur le MONSTRE cible (a[3],a[4]).
        {
            int i = TrouverMonstre(tgt);                    // boucle @0x55B1DB
            if (i >= 0) {
                MonsterEntity& m = g_World.monsters[i];
                if (a[13] == 1) {
                    // @0x55B326 : parade. Scan slot libre @0x55b296 puis Fx_AttachParry(&slot, &monstre[i]).
                    if (gfx::FxSlot* fx = SlotLibreFx())
                        gfx::Fx_AttachParry(fx, SourceFromMonster(m));
                }
                // @0x55B358 : « déviation » si monstre.def+232==2 ET g_Opt_GfxDetailShadows==1,
                //   déclenchée quand le palier de barre de vie franchi CHANGE (avant/après dégâts).
                //     v43 = 3 - ftol(hp*100 / (def+368)) / 30       (palier avant)
                //     v47 = 3 - ftol((hp-a[16])*100 / (def+368)) / 30 (palier après)
                //     si v43 < v47 -> Fx_AttachDeflect(&monstre[i]).   (def+368 = PV max MONSTER_INFO)
                // g_Opt_GfxDetailShadows 0x84DEF8 lu via la longue traîne (0 si non câblé -> no-op
                // honnête) ; def+232/+368 = champs bruts du record MONSTER_INFO résolu (m.def), guardé.
                if (m.def && g_Client.VarGet(0x84DEF8) == 1) {
                    const uint8_t* def = static_cast<const uint8_t*>(m.def);
                    int32_t deflectMode, maxHp;
                    std::memcpy(&deflectMode, def + 232, 4);   // def+232 (@0x55b358)
                    std::memcpy(&maxHp,       def + 368, 4);   // def+368 PV max (@0x55b39c)
                    if (deflectMode == 2 && maxHp != 0) {
                        const int v43 = 3 - static_cast<int>(static_cast<double>(m.hp) * 100.0 / maxHp) / 30;
                        const int v47 = 3 - static_cast<int>(static_cast<double>(m.hp - a[16]) * 100.0 / maxHp) / 30;
                        if (v43 < v47) {                        // @0x55b42c
                            if (gfx::FxSlot* fx = SlotLibreFx()) // scan @0x55b432
                                gfx::Fx_AttachDeflect(fx, SourceFromMonster(m)); // @0x55b4c2
                        }
                    }
                }
                m.hp -= a[16];              // dégâts externes (@0x55B4F8)
                // Crt_Memcpy(monstre[i]+923816, block, 76) @0x55B51F — cache du bloc
                //   de combat sur l'entité (modélisé hors-struct, cf. règle #6).
                StockerBlocCombat(CacheMonstre(), tgt, block);
                // @0x55B573 : variante de muzzle si hp>0 ET monstre.def+240==2 (après dégâts).
                //   Sélection : a[8]==1 -> switch a[9] {1->var1, 2->var2, 3->var3} ; a[8]==2 -> var2 ;
                //   sinon aucun (LABEL_162/163). def+240 = champ brut MONSTER_INFO (m.def, guardé).
                if (m.hp > 0 && m.def) {
                    int32_t muzzleMode;
                    std::memcpy(&muzzleMode, static_cast<const uint8_t*>(m.def) + 240, 4); // def+240
                    if (muzzleMode == 2) {
                        int variant = 0;                        // 0 = aucun effet
                        if (a[8] == 1) {                        // @0x55b5f6
                            switch (a[9]) {                     // @0x55b61d
                                case 1: variant = 1; break;     // @0x55b668
                                case 2: variant = 2; break;     // LABEL_162 @0x55b6e1
                                case 3: variant = 3; break;     // @0x55b6da
                                default: variant = 0; break;    // (a[9] autre) -> aucun
                            }
                        } else if (a[8] == 2) {                 // @0x55b5ff
                            variant = 2;                        // LABEL_162 @0x55b6e1
                        }
                        if (variant >= 1) {
                            if (gfx::FxSlot* fx = SlotLibreFx()) // scan @0x55b57f
                                gfx::Fx_AttachMuzzleVariant(fx, SourceFromMonster(m), variant);
                        }
                    }
                }
            }
        }

        // Son positionnel sur le JOUEUR attaquant (a[1],a[2]) — LABEL_163.
        {
            int i = TrouverJoueur(atk);                     // boucle @0x55B718
            if (i >= 0) {
                if (a[8] == 2 && a[9] == 78)
                    return;                 // (@0x55B8F8)
                // TODO(fx) @0x55B980/0x55B9F4 : même sélection sonore que cas 1/2
                //   (flt_149B03C, +432 si a[13]==1).
            }
        }
        return;

    // -------------------------------------------------------------------------
    // Cas 4 : attaque MONSTRE -> joueur.
    // -------------------------------------------------------------------------
    case 4:
        if (a[12]) {                        // touche (@0x55BA01)
            // Journal côté cible (self).
            if (SelfEst(tgt)) {             // @0x55BAE3
                std::string s = Str(203);
                int v27 = a[15] - a[14];    // (@0x55BB1D) SANS clamp
                if (a[13])
                    s += Fmt("%s(-%d)", Str(200), v27);
                else
                    s += Fmt("%s(-%d)", Str(199), v27);
                if (a[14] > 0)
                    s += Fmt(" %s(-%d)", Str(201), a[14]);
                LigneSysteme(s, 2);
                if (SelfModeDeclenche()) {
                    // TODO(net) @0x55BC3F : Net_QueueAction10(&g_PlayerCmdController)
                }
            }

            // Application des dégâts sur le JOUEUR cible (a[3],a[4]).
            {
                int i = TrouverJoueur(tgt);                 // boucle @0x55BC44
                if (i >= 0) {
                    PlayerEntity& p = g_World.players[i];
                    // TODO(fx) @0x55BD35/0x55BD76 : Snd3D_PlayPositional(flt_14914FC si
                    //   a[13], sinon flt_148927C) — son coup bloqué / normal. (audio, hors périmètre FX)
                    if (a[13] == 1) {
                        // @0x55BE28 : garde sur crit externe. Scan slot libre @0x55bd88 puis
                        //   Fx_AttachBlockGuard(&slot, 2, &player[i], i==0).
                        if (gfx::FxSlot* fx = SlotLibreFx())
                            gfx::Fx_AttachBlockGuard(fx, 2, SourceFromPlayer(p), i == 0);
                    }
                    p.hp -= a[16];          // dégâts externes (@0x55BE5E)
                    if (i == 0) g_World.self.hp = p.hp;     // miroir HUD self
                    // NB : pas de clamp >=0 dans le cas 4 (fidèle au binaire).
                    // Crt_Memcpy(player[i]+7536, block, 76) @0x55BE85 — cache du bloc
                    //   de combat sur l'entité (modélisé hors-struct, cf. règle #6).
                    StockerBlocCombat(CacheJoueur(), tgt, block);
                    // @0x55BF1F : muzzle sur la cible touchée. Scan slot libre @0x55be8d puis
                    //   Fx_AttachMuzzleFlash(&slot, &player[i], 1).
                    if (gfx::FxSlot* fx = SlotLibreFx())
                        gfx::Fx_AttachMuzzleFlash(fx, SourceFromPlayer(p), 1);
                }
            }
        } else if (SelfEst(tgt)) {          // raté (@0x55BA35)
            LigneSysteme(Str(202), 21);     // « l'attaquant vous a raté »
            if (SelfModeDeclenche()) {
                // TODO(net) @0x55BAAA : Net_QueueAction9(&g_PlayerCmdController)
            }
        }
        return;

    default:                                // (@0x55A3CA default)
        return;
    }
}

} // namespace ts2::game

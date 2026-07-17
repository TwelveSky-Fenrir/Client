// Gfx/PlayerMotionSlotResolver.h — resolveur de clip d'animation joueur par arme/competence.
//
// ============================ RAISON D'ETRE ================================
// MotionCache::GetForPlayer 0x4E46A0 (Gfx/MotionCache.*) ne porte que le CHEMIN PAR
// DEFAUT du corps joueur : il fabrique toujours un stem "C%03d%03d%03d" (dossier 001,
// categorie 1). Or le vrai client redirige, selon l'arme/competence portee (a8 =
// item/skillId), l'etat d'animation vers des REGIONS DE CLIP DEDIEES d'une AUTRE
// categorie motion (categorie 6, stem "X%03d%03d%03d", dossier 006), voire vers des
// SLOTS FIXES d'action (poses "d'entree" a5 in {1,2,32}). Sans ce meta-switch, tous
// les clips d'arme/skill tombent sur le clip de corps generique (l'idle par defaut).
// Ce module PORTE ce meta-switch — le gap A du backlog Docs/TS2_DEEP_MOTION.md §9.2.
//
// Module AUTONOME et LECTURE-SEULE cote binaire : il ne calcule QUE la decomposition
// (categorie, kind, variante, etat) d'un slot ; il n'ouvre aucun fichier, ne touche
// aucun device D3D, et n'est cable dans aucune boucle de rendu (la consolidation MAIN
// branchera son resultat sur MotionCache).
//
// ============================ ANCRES IDA (seule verite, imagebase 0x400000) =========
//   PcModel_ResolveEquipSlot   0x4E46A0  — le meta-switch porte ici (a8 -> region de clip).
//       thiscall(this=g_ModelMotionArray 0x8E8B30, a2=race, a3=gender, a4=weaponPose,
//                a5=animState, a6=ctxA6, a7=ctxA7, a8=item/skillId) -> &MotionSlot.
//       Garde 0x4e46cc : race>2 || gender>1 || !Motion_IsValidWeaponPose(a4,a5) -> fallback.
//   Motion_IsValidWeaponPose   0x4E3A30  — table de validite (weaponPose, animState).
//       ATTENTION ordre : appele Motion_IsValidWeaponPose(a4 /*pose*/, a5 /*state*/) ; la
//       fonction fait switch(a2=state) et borne a1=pose -> voir IsValidPlayerWeaponPose.
//   PcModel_ResolveSlotAndApply 0x4E5A00 — wrapper : ResolveEquipSlot puis Motion_GetFrameCount.
//   Motion_BuildPathAndLoad    0x4D7390  — cat 1 "001\\C%03d%03d%03d" %(race+3*gender+1, wp+1,
//       state+1) ; cat 6 "006\\X%03d%03d%03d" %(race+3*gender+1, wp+1, state+1). MEME formule.
//   AssetMgr_InitAllSlots      0x4DEB50  — POPULATEUR = preuve du mapping offset->stem :
//       cat 1 @0x4df00c : slot = this + 479232*race + 159744*gender + 19968*wp + 156*state
//                                       + 2624960, (cat=1, race, gender, wp, state).
//       cat 6 @0x4df32e : slot = this + 479232*race + 159744*gender + 19968*wp + 156*state
//                                       + 4062656, (cat=6, race, gender, wp, state).
//   g_ModelMotionArray         0x8E8B30  — pool statique (base `this`).
//
// ============================ MAPPING OFFSET -> STEM (PROUVE, ZERO INVENTION) =======
// Chaque `return this + OFFSET` de 0x4E46A0 se decompose de facon UNIQUE via l'arithmetique
// EXACTE du populateur 0x4DEB50 (memes strides 479232/159744/19968/156, memes stems) :
//   * OFFSET parametre  = 479232*race + 159744*gender + 19968*wp + 156*state + BASE
//       - BASE = 2624960 -> categorie 1 (C, dossier 001), (race, gender, wp, state) tels quels.
//       - BASE = 4062656 -> categorie 6 (X, dossier 006), (race, gender, wp, state) tels quels.
//   * OFFSET fixe (dedie) = 479232*race + 159744*gender + FIXED   (wp implicite = 0)
//       -> region = (FIXED >= 4062656) ? X : C ; rel = FIXED - baseRegion ;
//          wp = rel/19968 (== 0), state = rel/156. TOUS verifies exacts (rel % 156 == 0,
//          rel < 19968) — cf. tableau ci-dessous. Ces slots FIXES SONT PEUPLES par le
//          populateur (cat 1 states 77/84 ; cat 6 states 100..127) : ce ne sont PAS des
//          "bases sans stem", ils ont un stem PROUVE. Aucun TODO d'invention requis.
//   * fallback garde 0x4e46dd = this + 2644772 (ABSOLU, sans terme race/gender) -> categorie 1,
//       race=0, gender=0, wp=0, state=127 -> stem "C001001128" (idle de repli).
//
//   Offsets fixes -> (region, wp, state)  [verifie par script, tous exacts] :
//     C : 2636972->state77  2638064->state84  2644772->state127(fallback)
//     X : 4078256->100 4078412->101 4078568->102 4078724->103 4078880->104 4079036->105
//         4079192->106 4079348->107 4079504->108 4079660->109 4079816->110 4079972->111
//         4080128->112 4080284->113 4080440->114 4080596->115 4080752->116 4080908->117
//         4081064->118 4081220->119 4081376->120 4081532->121 4081688->122 4081844->123
//         4082000->124 4082156->125 4082312->126 4082468->127   (tous wp=0)
//
// NOTE stem special NON atteignable : Motion_BuildPathAndLoad cas 1 emet "C%03d%03d011"
// (state fige a 011, kind+6) UNIQUEMENT si a6==120 dans le populateur (state index 120).
// Le resolveur ne renvoie JAMAIS un slot C au state 120 (guard limite state a la table
// 0x4E3A30, max 95 ; les fixes C sont 77/84/127). Donc ce stem special ne concerne pas
// ce module. (cf. Docs/TS2_DEEP_MOTION.md §3.)
#pragma once
#include <cstdint>
#include <string>

namespace ts2::gfx {

// Categorie de motion resolue = dossier + lettre de stem (Motion_BuildPathAndLoad 0x4D7390).
enum class PlayerMotionCategory : int {
    BodyC       = 1,   // "001\\C%03d%03d%03d.MOTION" — corps generique (cat 1, base 2624960)
    WeaponSkillX = 6,  // "006\\X%03d%03d%03d.MOTION" — clip arme/competence (cat 6, base 4062656)
};

// Resultat de resolution : la decomposition (categorie, kind, variante/etat) d'un &MotionSlot
// renvoye par PcModel_ResolveEquipSlot 0x4E46A0. Les champs sont 0-based (indices bruts) ; les
// accesseurs stem*() appliquent le +1 des printf du binaire.
struct PlayerMotionSlot {
    PlayerMotionCategory category = PlayerMotionCategory::BodyC;

    // Indices bruts (0-based) tels que decomposes depuis l'offset renvoye (arithmetique 0x4DEB50).
    int race        = 0;   // a2 (0..2) — inchange par le switch (terme 479232*race toujours present)
    int gender      = 0;   // a3 (0..1) — inchange (terme 159744*gender toujours present)
    int weaponIndex = 0;   // variante de stem 0-based : a4 (chemin parametre) ou 0 (slot fixe)
    int stateIndex  = 0;   // etat de stem 0-based : a5 (parametre) ou l'index fixe (77/84/100..127)

    // true si la garde d'entree a echoue (race>2 || gender>1 || pose invalide) -> slot idle de repli
    // absolu "C001001128" (0x4e46dd). Dans ce cas race/gender/weaponIndex sont forces a 0.
    bool guardFallback = false;

    // Offset OCTET du slot dans g_ModelMotionArray 0x8E8B30 (= valeur `this + X` renvoyee par
    // 0x4E46A0, moins `this`). Fourni pour verification/traçage (doit egaler l'arithmetique du
    // binaire a l'octet pres) ; NON requis pour construire le stem.
    std::uint32_t slotByteOffset = 0;

    // --- Champs de stem (printf Motion_BuildPathAndLoad 0x4D7390, deja +1) ---
    // stem = "%c%03d%03d%03d" % (stemLetter, stemKind, stemVariant, stemState).
    int  stemKind()    const { return race + 3 * gender + 1; } // field1 : race+3*gender+1
    int  stemVariant() const { return weaponIndex + 1; }       // field2 : weaponPose+1
    int  stemState()   const { return stateIndex + 1; }        // field3 : animState+1
    char stemLetter()  const { return category == PlayerMotionCategory::BodyC ? 'C' : 'X'; }
    // Dossier GMOTION (Motion_BuildPathAndLoad) : "001" (cat 1) ou "006" (cat 6).
    const char* motionFolder() const {
        return category == PlayerMotionCategory::BodyC ? "001" : "006";
    }

    // Stem complet SANS extension ni dossier, ex "X002001105" — a passer a un cache motion
    // (meme convention que MotionCache::BuildPlayerMotionStem, etendue a la lettre X/dossier 006).
    std::string BuildStem() const;
};

// -----------------------------------------------------------------------------
//  API du resolveur.
// -----------------------------------------------------------------------------

// Reproduit la table Motion_IsValidWeaponPose 0x4E3A30 (garde d'entree de 0x4E46A0).
// Renvoie true si le couple (weaponPose, animState) est admis. NB (ancre 0x4e46cc) : le binaire
// appelle Motion_IsValidWeaponPose(a4=weaponPose, a5=animState) ; la fonction fait switch(state)
// et borne pose -> l'ordre des arguments ici est (weaponPose, animState).
bool IsValidPlayerWeaponPose(int weaponPose, int animState);

// Porte le meta-switch complet de PcModel_ResolveEquipSlot 0x4E46A0.
//   race, gender      : a2, a3 (identite du joueur ; g_EntityArray this+0x5C/+0x60).
//   weaponPose        : a4 (pose d'arme 0..7, cf. table de validite 0x4E3A30).
//   animState         : a5 (etat d'animation ; sous-switch a5 in {1,2,32} -> slots fixes).
//   ctxA6, ctxA7      : a6, a7 — CONTEXTE OPAQUE passe tel quel par les appelants
//                       (Char_RenderModel 0x527020 @0x52705a/@0x52753f, Char_*AnimTick_* via
//                       PcModel_ResolveSlotAndApply 0x4E5A00). Seul emploi : la branche
//                       LABEL_152 0x4e5708 (a6>112 && a5==1 && a4 pair -> clips C fixes 78/85,
//                       gate par a7>=1). TODO ancre : semantique fine de a6/a7 non tracee cote
//                       appelant — reproduits verbatim, jamais interpretes ici.
//   itemOrSkillId     : a8 — l'id d'item/competence qui selectionne la famille de clip.
// Renvoie la decomposition (categorie, kind, variante, etat) — voir PlayerMotionSlot.
PlayerMotionSlot ResolvePlayerMotionSlot(int race, int gender, int weaponPose, int animState,
                                         int ctxA6, int ctxA7, int itemOrSkillId);

} // namespace ts2::gfx

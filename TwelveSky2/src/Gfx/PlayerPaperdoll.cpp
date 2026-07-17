// Gfx/PlayerPaperdoll.cpp — implementation. Voir PlayerPaperdoll.h pour la preuve IDA
// (Char_RenderModel 0x527020 : v37 palette partagee, corps 2 pieces + arme skinnee au bone de main).
//
// ///// FLOTTE C / FRONT C3 — CABLAGE B2 + B3 (integration, 2026-07-17) /////
// Ce fichier RELIE deux modules FLOTTE B jusqu'ici non branches au site de rendu :
//   B2  Gfx/EquipModelResolver.h  — BuildArmorBodyStem (stem SObject "C..." des pieces de corps,
//        port fidele de SObject_BuildPath 0x4D89C0 case 1) passe a ModelCache::Get (public, EXISTANT).
//   B3  Gfx/PlayerMotionSlotResolver.h — ResolvePlayerMotionSlot (meta-switch + garde
//        Motion_IsValidWeaponPose 0x4E3A30 de PcModel_ResolveEquipSlot 0x4E46A0) route la palette
//        d'os vers MotionCache::GetForPlayer (public, EXISTANT).
// Aucune API des caches n'est modifiee (contrainte de disjonction FLOTTE C).
#include "Gfx/PlayerPaperdoll.h"
#include "Gfx/EquipModelResolver.h"        // B2 : BuildArmorBodyStem (stem SObject des pieces de corps)
#include "Gfx/PlayerMotionSlotResolver.h"  // B3 : ResolvePlayerMotionSlot (garde + slot MOTION joueur)
#include "Game/GameDatabase.h"             // game::GetItemInfo (resolution modele arme via ITEM_INFO)

namespace ts2::gfx {

PaperdollResult PlayerPaperdoll::Resolve(ModelCache& models, MotionCache& motions,
                                         int race, int gender, int costume0, int costume1,
                                         uint32_t weaponItemId, int weaponPose,
                                         int animState, float animCursor, bool hasAnimCursor,
                                         float gameTimeSec) {
    PaperdollResult r;

    // ===========================================================================
    // 1) Palette d'os PARTAGEE — B3 : ResolvePlayerMotionSlot 0x4E46A0.
    // ===========================================================================
    // Char_RenderModel 0x527020 resout v37 par :
    //     v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, a4[9], a4[11], a5, a6, 1, 0, 0);  /*0x52705f/0x527544*/
    // -> ANCRES DES ARGUMENTS (relu en IDA cette session) : a2=race, a3=gender, a4(pose)=Char_RenderModel::a5,
    //    a5(etat)=Char_RenderModel::a6, a6=1, a7=0, a8(itemOrSkillId)=0. Le a8 EST LITTERALEMENT 0 aux
    //    DEUX sites d'appel de Char_RenderModel -> le meta-switch d'id d'arme (0x4e46ef..0x4e5708) tombe
    //    TOUJOURS sur LABEL_152 (categorie 1, corps "C"), jamais sur une famille cat.6 "X". De plus
    //    a6=1 (<=112) force ResolveDefaultL152 sur sa 1re branche (MakeParam, base 2624960) : le slot
    //    resolu est donc TOUJOURS categorie BodyC (dossier 001), variante=pose(=0), etat=animState.
    //
    // CONSEQUENCE DE CABLAGE (fidelite + contrainte "ne pas changer l'API MotionCache") : puisque le
    // slot est TOUJOURS categorie 1 "C", le stem "C%03d%03d%03d" produit par ResolvePlayerMotionSlot
    // (PlayerMotionSlot::BuildStem, dossier motionFolder()="001") est BIT-POUR-BIT celui que fabrique
    // MotionCache::GetForPlayer 0x4E46A0 (BuildPlayerMotionStem : "C%03d%03d%03d" % (race+3*gender+1,
    // weaponType+1, animState+1)). On route donc via le GetForPlayer PUBLIC en lui passant les indices
    // decomposes du slot (weaponIndex/stateIndex) -- MotionCache::Get(stem, folder) est PRIVE, on ne
    // peut pas l'appeler directement, et on n'a pas le droit d'ouvrir son API.
    //
    // CE QUE B3 AJOUTE par rapport a l'ancien `GetForPlayer(race, gender, 0, animState)` : la GARDE
    // Motion_IsValidWeaponPose 0x4E3A30 (0x4e46cc). Pour un couple (pose=0, animState) INVALIDE (ex.
    // les etats d'attaque 4..9/42..46/... qui exigent une pose d'arme non nulle), 0x4E46A0 renvoie le
    // slot idle de repli ABSOLU `this + 2644772` = "C001001128" (race0/gen0/wp0/state127, 0x4e46dd) au
    // lieu de fabriquer un stem "C{kind}001{state+1}" hors table. Pour les etats de LOCOMOTION communs
    // (0,1,2,3,10,11,12..) la garde passe et le resultat est STRICTEMENT IDENTIQUE a l'ancien code
    // (MakeParam, wp=0). B3 est donc un sur-ensemble fidele : identique quand valide, idle prouve quand
    // invalide (jamais un stem hors table). weaponPose=0 assume (le vrai a4=pose in-game vient du
    // switch a8, non porte) -- MEME repli que l'ancien weaponType=0, pas une invention.
    const gfx::PlayerMotionSlot ms = gfx::ResolvePlayerMotionSlot(
        race, gender, weaponPose, animState, /*ctxA6=*/1, /*ctxA7=*/0, /*itemOrSkillId=*/0);

    if (ms.category == gfx::PlayerMotionCategory::BodyC) {
        // Categorie 1 "C" (le seul cas atteignable ici, a8=0) -> GetForPlayer public, stem identique.
        if (const gfx::MotionPalette* mp =
                motions.GetForPlayer(ms.race, ms.gender, ms.weaponIndex, ms.stateIndex)) {
            // Curseur REEL par entite (entity+248) UNIQUEMENT s'il est reellement avance
            // (game::Player_AnimCursorTickIsWired via hasAnimCursor) : SampleByCursor reproduit le
            // chemin de dessin fidele (frame = ftol(animTime), wrap par soustraction — Char_TickMoveState
            // 0x574830 @0x574922). Sinon repli horloge globale ASSUME (SampleByGameTime) : sans l'avance
            // du curseur, brancher SampleByCursor(0) figerait TOUS les joueurs a la frame 0 (garde
            // anti-regression OBLIGATOIRE, cf. header).
            r.palette = hasAnimCursor
                ? MotionCache::SampleByCursor(*mp, animCursor)      // curseur entity+248 (0x574922)
                : MotionCache::SampleByGameTime(*mp, gameTimeSec);  // repli horloge globale (non wired)
        }
        // sinon r.palette reste invalide (identite cote MeshRenderer) — degrade honnete, pas d'invention.
    }
    // else : categorie WeaponSkillX (dossier 006, stem "X...") -> INATTEIGNABLE a ce site (a8=0). Aucun
    // accesseur MotionCache PUBLIC ne produit un stem "X"/dossier 006 (seul GetForPlayer existe, dossier
    // 001 "C"), et MotionCache::Get(stem, folder) est PRIVE. TODO-ancre [0x4E46A0 cat.6] : cabler les
    // clips d'arme "X" exigerait une nouvelle entree publique dans MotionCache (hors perimetre C3 :
    // interdiction de modifier l'API des caches) -> palette identite en repli, jamais un stem invente.

    // ===========================================================================
    // 2) Corps de base 2 pieces — B2 : BuildArmorBodyStem -> ModelCache::Get (public).
    // ===========================================================================
    // Char_RenderModel 0x527020 (branche a3==0) dessine le corps de base par :
    //     SObject_DrawEx(&flt_F59A7C[504*race + 252*gender + 36*a4[12]], .., v37, 1);  /*0x5277b0*/  (SLOT0)
    //     SObject_DrawEx(&flt_F5B21C[216*race + 108*gender + 36*a4[13]], .., v37, 1);  /*0x52780b*/  (SLOT1)
    // flt_F59A7C / flt_F5B21C sont pre-generes par AssetMgr_InitAllSlots 0x4DEB50 en appelant
    // SObject_BuildPath 0x4D89C0 case 1 : SLOT0 = "C%03d001%03d", SLOT1 = "C%03d002%03d" (kind+1,
    // variant+1). C'est EXACTEMENT BuildArmorBodyStem(race, gender, Base0/Base1, costumeN) (B2). Le stem
    // ainsi produit est BIT-POUR-BIT celui de ModelCache::BuildPlayerBodyStem (l'ancien chemin
    // GetForPlayerBody) -> on route desormais par B2 sans changer le rendu.
    //
    // Bornes de variante PRESERVEES de l'ancien GetForPlayerBody (costume0 in [0,7) catalogue flt_F59A7C,
    // costume1 in [0,3) catalogue flt_F5B21C, cf. Gfx/ModelCache.h::BuildPlayerBodyStem) : ce sont des
    // gardes DEFENSIVES ClientSource (le binaire ne borne pas la variante) — on ne change pas ce
    // comportement existant. Hors borne -> piece non resolue (comme avant), jamais un stem hasardeux.
    if (costume0 >= 0 && costume0 < 7) {
        const std::string s0 = gfx::BuildArmorBodyStem(race, gender, gfx::EquipBodySlot::Base0, costume0);
        if (const gfx::SkinnedModel* m = models.Get(s0))       // ModelCache::Get (public, EXISTANT)
            if (!m->Empty()) r.pieces.push_back(m);            // SLOT0 (flt_F59A7C)
    }
    if (costume1 >= 0 && costume1 < 3) {
        const std::string s1 = gfx::BuildArmorBodyStem(race, gender, gfx::EquipBodySlot::Base1, costume1);
        if (const gfx::SkinnedModel* m = models.Get(s1))
            if (!m->Empty()) r.pieces.push_back(m);            // SLOT1 (flt_F5B21C)
    }

    // ===========================================================================
    // 3) Arme — repli sur le comportement actuel (GetForItem), catalogue non trace.
    // ===========================================================================
    // Char_RenderModel 0x527bfe dessine l'arme via le catalogue flt_100EA3C indexe par
    // *(itemRec+196) (= ITEM_INFO.field196, le liant id-item->modelIndex) :
    //     SObject_DrawEx(&flt_100EA3C[1440*race - 36 + 720*gender + 36*(*(v33+196))], .., v37, 1);
    // flt_100EA3C est un catalogue SObject cat.6 "W..." pre-genere dont la loi de population
    // (AssetMgr_InitAllSlots, boucle cat.6) N'EST PAS TRACEE -> on ne peut pas inverser field196 vers le
    // triplet (type, subType, level) qu'exige BuildWeaponStem (B2). REPLI DOCUMENTE : on garde la
    // resolution actuelle par ITEM_INFO.model[0] (+29) via ModelCache::GetForItem(item, 0), MEME
    // transformee + MEME palette v37 que le corps (arme skinnee au bone de main). Pas d'invention de stem.
    // TODO-ancre [0x527bfe / flt_100EA3C / 0x4DEB50 cat.6] : tracer la loi de population du catalogue
    // d'armes pour resoudre exactement comme le binaire (field196 -> entree flt_100EA3C).
    if (weaponItemId != 0) {
        if (const game::ItemInfo* it = game::GetItemInfo(weaponItemId))
            if (const gfx::SkinnedModel* w = models.GetForItem(*it, /*slot=*/0))
                if (!w->Empty()) r.pieces.push_back(w);
    }

    // ===========================================================================
    // TODO-ancre [Char_RenderModel 0x527020, slots corps 2 / 14..21 + accessoires] :
    // Les pieces d'equipement (casque a4[34]->flt_F6685C=Slot14, torse a4[46]->flt_F7C09C=Slot15, ...
    // slots 14..21 via BuildArmorBodyStem(race, gender, Slot14..21, *(itemRec+196))) SONT desormais
    // resolvables cote STEM (B2 fournit BuildArmorBodyStem pour 14..21) -- mais la DONNEE manque :
    //   * Char_RenderModel opere sur la structure CHARSELECT (a4[30..74]) ; l'IDB prouve qu'il n'est
    //     appele QUE depuis Scene_CharSelectRender, JAMAIS en jeu (le dessin du corps joueur EN JEU
    //     n'est pas localise statiquement). Le LAYOUT in-game des id d'items equipes n'est donc PAS
    //     prouve : cote ClientSource, seul l'ARME est reversee (self=SelfState::equip[7], distant=
    //     PlayerEntity::body+148, cf. GameState.h) ; les slots casque/torse/etc. des joueurs DISTANTS
    //     n'ont aucun offset prouve dans le body reseau (600 o), et le mapping SelfState::equip[k] ->
    //     slot de paperdoll (14..21) n'est pas trace non plus.
    // -> Resoudre 14..21 exigerait d'INVENTER soit ces offsets, soit ce mapping : hors perimetre
    //    (regle "IDA = unique verite"). On laisse donc le paperdoll = corps 2 pieces + arme, comme le
    //    comportement actuel. Le pipeline B2 est pret (BuildArmorBodyStem gere 14..21) : brancher ces
    //    slots ne demandera QUE la source de donnees (id d'item par slot) une fois reversee.
    // ===========================================================================

    r.valid = !r.pieces.empty();
    return r;
}

} // namespace ts2::gfx

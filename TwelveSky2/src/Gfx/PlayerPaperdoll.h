// Gfx/PlayerPaperdoll.h — resolveur de paperdoll joueur, calque sur Char_RenderModel 0x527020.
//
// ROLE : produire, pour un joueur, (a) la palette d'os animee PARTAGEE et (b) la liste ordonnee
// des pieces de modele a dessiner a la MEME transformee — en remplacement du corps 2-pieces
// inline actuel de Scene/WorldRenderer.cpp.
//
// PREUVE IDA (Char_RenderModel 0x527020, dessin par piece de l'ecran CharSelect ; le dessin du
// corps joueur EN JEU n'est pas localise statiquement -> l'usage en jeu est une extrapolation
// honnete de cette fonction) :
//   - v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, race, gender, weaponPose, animState,1,0,0)
//     = UNE palette d'os partagee par TOUTES les pieces.
//   - Corps de base 2 pieces : SObject_DrawEx(&flt_F5B21C[..], .., v37, 1) (SLOT1)
//                            + SObject_DrawEx(&flt_F59A7C[..], .., v37, 1) (SLOT0).
//   - Arme (v33, a4[62]) : SObject_DrawEx(&flt_100EA3C[.. + 36*(*(v33+196))], .., v37, 1)
//     -> MEME palette v37, MEME transformee a8(pos)/a7(animTime) que le corps : l'arme est
//        SKINNEE au bone de main via la palette du corps, PAS positionnee par un offset. C'est
//        l'attache "main" demandee (Char_RenderModel 0x527bfe).
//
// HORS PERIMETRE (TODO ancre) : les 12 autres slots d'equipement (casque/armure/...) via
// catalogues flt_F5xxxx indexes *(entry+196) (Char_RenderModel) — necessite de reverser ~40
// adresses de base de catalogue non resolvables par l'API stem actuelle de ModelCache ; et le
// body distant ne porte que l'arme (@body+148, cf. GameState.h). Le paperdoll = corps 2 pieces
// + arme ; le reste reste en TODO ancre.
#pragma once
#include "Gfx/ModelCache.h"
#include "Gfx/MotionCache.h"
#include "Game/GameState.h"
#include <vector>

namespace ts2::gfx {

// Resultat d'une resolution de paperdoll. `palette` (animee, partagee) s'applique a TOUTES les
// pieces (Char_RenderModel v37). `pieces` dans l'ordre SLOT0, SLOT1, [arme].
struct PaperdollResult {
    gfx::BonePalette                        palette{};       // Char_RenderModel 0x527020 : v37 partage
    std::vector<const gfx::SkinnedModel*>   pieces;          // SLOT0 (flt_F59A7C), SLOT1 (flt_F5B21C), [arme]
    bool                                    valid = false;
};

// Resolveur SANS etat (refs passees a l'appel) — evite d'ajouter un membre a WorldRenderer.h
// (NON editable). Calque sur Char_RenderModel 0x527020.
class PlayerPaperdoll {
public:
    static PaperdollResult Resolve(ModelCache& models, MotionCache& motions,
                                   int race, int gender, int costume0, int costume1,
                                   uint32_t weaponItemId, float gameTimeSec);
};

} // namespace ts2::gfx

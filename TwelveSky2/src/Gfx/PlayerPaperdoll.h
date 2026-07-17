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
// CABLAGE FLOTTE C / FRONT C3 (integration, 2026-07-17) — voir PlayerPaperdoll.cpp pour le detail
// et les ancres. Resume :
//   * B3 Gfx/PlayerMotionSlotResolver.h : la palette v37 passe desormais par ResolvePlayerMotionSlot
//     0x4E46A0 (avec sa garde Motion_IsValidWeaponPose 0x4E3A30). Comme Char_RenderModel appelle
//     PcModel_ResolveEquipSlot avec a8=0 ET a6=1, le slot est TOUJOURS categorie 1 "C" -> route via le
//     MotionCache::GetForPlayer PUBLIC (stem "C..." identique) sans toucher a l'API du cache. Effet net :
//     un couple (pose=0, etat) invalide tombe sur l'idle prouve C001001128 au lieu d'un stem hors table.
//   * B2 Gfx/EquipModelResolver.h : le corps de base 2 pieces passe par BuildArmorBodyStem(Base0/Base1)
//     -> ModelCache::Get (public), stem BIT-POUR-BIT celui de l'ancien GetForPlayerBody.
//
// HORS PERIMETRE (TODO ancre) : les 12 autres slots d'equipement (casque/armure/...) — B2 fournit
// desormais BuildArmorBodyStem pour les slots 14..21 (stem "C..." indexe par *(itemRec+196)), MAIS la
// DONNEE manque : Char_RenderModel n'est appele QU'EN CharSelect (a4[30..74]), le layout IN-GAME des id
// d'items equipes n'est pas prouve -> seul l'arme est reversee (self=equip[7], distant=body+148, cf.
// GameState.h) ; casque/torse/etc. des joueurs distants n'ont aucun offset prouve. L'arme elle-meme
// reste en repli GetForItem (le vrai catalogue flt_100EA3C indexe par field196 n'est pas trace). Le
// paperdoll = corps 2 pieces + arme ; le reste reste en TODO ancre (pas d'invention de stem/offset).
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
//
// ANIMATION VIVANTE (front F_PLAYERANIM, 2026-07-17) — params ajoutes :
//   animState     = etat FSM entity+244 = CharAnimState::state (peuple depuis le reseau,
//                   Game/EntityManager.cpp:390 = body+220). SELECTEUR de clip : passe TEL QUEL a
//                   PcModel_ResolveEquipSlot 0x4E46A0 (base + 156*etat), remplace l'ancien 0 fige.
//   animCursor    = curseur entity+248 = CharAnimState::animFrame, avance par
//                   game::Player_AdvanceAnimCursor (Game/PlayerAnimCursorTick.h) en phase UPDATE.
//   hasAnimCursor = game::Player_AnimCursorTickIsWired() : true UNIQUEMENT si le curseur est
//                   reellement avance (garde anti-regression, cf. .cpp) -> SampleByCursor ; sinon
//                   repli SampleByGameTime (clip correct via animState, cadence horloge globale).
// Meme patron exact que monstres/PNJ (Scene/WorldRenderer.cpp renderOne : animType toujours
// passe, seul le SampleByCursor vs SampleByGameTime est garde par hasAnimCursor).
class PlayerPaperdoll {
public:
    // weaponPose (G5, DEEP IDA render) = a4 de PcModel_ResolveEquipSlot 0x4E46A0 @0x4e578e =
    // animSlot (entity+240 = body+216 = 2*weaponClass). Sélectionne le jeu MOTION de la POSE D'ARME
    // (base + 19968*weaponPose). 0 = pose désarmée (repli). DOIT être identique à celui passé à
    // WorldPlayerMotionFrameCount (sinon wrap du curseur et clip dessiné divergent).
    // torsoItemId/legsItemId (G3, DEEP IDA #5 equip-slot-layout) : id d'item ITEM_INFO des pieces
    // d'armure de CORPS dessinees en jeu par Char_DrawWeaponTrailEffect 0x55E9D0 (bloc corps 0x5616EC) :
    //   torse = equip[2] = body+92+8*2 = body+108 (token SObject 003, catalogue unk_F5BC3C @0x561bd1)
    //   jambes= equip[5] = body+92+8*5 = body+132 (token SObject 004, catalogue unk_F7147C @0x561e47)
    // variant de mesh = ITEM_INFO(item)+196 (field196, SANS -1 pour l'armure de corps) ; 0 = rien equipe
    // (catalogue[0]). 0 -> corps de base. Sans eux, le joueur reste en corps de base (nu).
    static PaperdollResult Resolve(ModelCache& models, MotionCache& motions,
                                   int race, int gender, int costume0, int costume1,
                                   uint32_t weaponItemId, uint32_t torsoItemId, uint32_t legsItemId,
                                   int weaponPose,
                                   int animState, float animCursor, bool hasAnimCursor,
                                   float gameTimeSec);
};

} // namespace ts2::gfx

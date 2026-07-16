// Gfx/PlayerPaperdoll.cpp — implementation. Voir PlayerPaperdoll.h pour la preuve IDA
// (Char_RenderModel 0x527020 : v37 palette partagee, corps 2 pieces + arme skinnee au bone de main).
#include "Gfx/PlayerPaperdoll.h"
#include "Game/GameDatabase.h" // game::GetItemInfo (resolution modele arme via ITEM_INFO)

namespace ts2::gfx {

PaperdollResult PlayerPaperdoll::Resolve(ModelCache& models, MotionCache& motions,
                                         int race, int gender, int costume0, int costume1,
                                         uint32_t weaponItemId,
                                         int animState, float animCursor, bool hasAnimCursor,
                                         float gameTimeSec) {
    PaperdollResult r;

    // 1) Palette d'os PARTAGEE — Char_RenderModel 0x527020 : v37 = PcModel_ResolveEquipSlot
    //    0x4E46A0(g_ModelMotionArray, race, gender, weaponPose, animState, 1,0,0). Une seule
    //    palette pour toutes les pieces (corps + arme).
    //
    //    ///// BRANCHE VIVANT — front F_PLAYERANIM (2026-07-17) /////
    //    AVANT : `animState = 0` fige (idle) + SampleByGameTime (horloge globale) -> tous les
    //    joueurs jouaient le MEME idle EN PHASE. Le clip suit desormais l'etat FSM reel
    //    (entity+244 = CharAnimState::state ; PcModel_ResolveEquipSlot resout base + 156*etat,
    //    cf. Docs/TS2_EXTRACT_PLAYER_ANIM.md §4) : idle/marche/course/attaque/cast. animState est
    //    passe TEL QUEL a GetForPlayer (meme patron que monstres/PNJ dans renderOne), donc le CLIP
    //    est correct MEME quand le curseur n'est pas encore cable (repli horloge globale ci-dessous).
    //    TODO [ancre 0x4E46A0] weaponPose : provient du switch a8=weaponItemId de
    //    PcModel_ResolveEquipSlot (~500 cas non reverses) -> laisse a 0 ; l'animType (etat field 61)
    //    en est INDEPENDANT (le clip d'arme exact reste approxime, pas invente).
    const int weaponType = 0;
    if (const gfx::MotionPalette* mp = motions.GetForPlayer(race, gender, weaponType, animState))
        // Curseur REEL par entite (entity+248) UNIQUEMENT s'il est reellement avance
        // (game::Player_AnimCursorTickIsWired via hasAnimCursor) : SampleByCursor reproduit le
        // chemin de dessin fidele (frame = ftol(animTime), wrap par soustraction — Char_TickMoveState
        // 0x574830 @0x574922, cf. Gfx/MotionCache.h::SampleByCursor). Sinon repli horloge globale
        // ASSUME (SampleByGameTime) : sans l'avance du curseur, brancher SampleByCursor(0) figerait
        // TOUS les joueurs a la frame 0 (blocker recon, garde anti-regression OBLIGATOIRE).
        r.palette = hasAnimCursor
            ? MotionCache::SampleByCursor(*mp, animCursor)      // curseur entity+248 (0x574922)
            : MotionCache::SampleByGameTime(*mp, gameTimeSec);  // repli horloge globale (non wired)
    // sinon r.palette reste invalide (identite cote MeshRenderer) — degrade honnete, pas d'invention.

    // 2) Corps de base 2 pieces — ModelCache::GetForPlayerBody (deja cable sur flt_F59A7C SLOT0 /
    //    flt_F5B21C SLOT1 de Char_RenderModel). Chaque piece est independamment nullptr/vide.
    const gfx::PlayerBodyModel body = models.GetForPlayerBody(race, gender, costume0, costume1);
    if (body.slot0 && !body.slot0->Empty()) r.pieces.push_back(body.slot0);
    if (body.slot1 && !body.slot1->Empty()) r.pieces.push_back(body.slot1);

    // 3) Arme — Char_RenderModel 0x527bfe : MEME palette v37, MEME transformee que le corps.
    //    Resolue via ITEM_INFO.model (ModelCache::GetForItem, slot 0), meme chemin que l'ancien
    //    WorldRenderer. Ajoutee en piece -> skinnee au bone de main par la palette du corps.
    if (weaponItemId != 0) {
        if (const game::ItemInfo* it = game::GetItemInfo(weaponItemId))
            if (const gfx::SkinnedModel* w = models.GetForItem(*it, /*slot=*/0))
                if (!w->Empty()) r.pieces.push_back(w);
    }

    // TODO [ancre 0x527020] les 12 autres slots d'equipement (casque/armure/bottes/accessoires)
    // via catalogues flt_F5xxxx indexes MobDb_GetEntry(mITEM,a4[slot]) 0x4C3C00 +196 (stride 144) :
    // ~40 adresses de base de catalogue non resolvables par l'API stem actuelle de ModelCache, et
    // le body distant ne porte que l'arme (@body+148, GameState.h). Laisse en TODO ancre.

    r.valid = !r.pieces.empty();
    return r;
}

} // namespace ts2::gfx

// Game/PlayerAnimCursorTick.cpp — implémentation. Voir le header pour la preuve IDA complète
// (Char_AnimTick_5746E0 0x5746E0 / Char_TickMoveState 0x574830 : frame += dt*30, wrap par
// soustraction @0x574922 ; frameCount = PcModel_ResolveSlotAndApply 0x4E5A00). Front F_PLAYERANIM.
#include "Game/PlayerAnimCursorTick.h"

namespace ts2::game {

namespace {
// Latch de câblage (cf. Player_AnimCursorTickIsWired) — passe à true au 1er tick réel. Durée de
// vie process, même motif que les statiques de Game/AnimationTick.cpp (Monster/ZoneNpc IsWired).
bool s_playerAnimCursorWired = false;
} // namespace

void Player_AdvanceAnimCursor(CharAnimState& anim, float dt, int frameCount) {
    // Avance — Char_AnimTick_5746E0 0x5746E0 @0x5747a8 (idle) / Char_TickMoveState 0x574830
    //   @0x5748f8 (move) : *(float*)(this+248) = a3*30.0 + *(float*)(this+248). (a3 = dt ;
    //   this+248 = CharAnimState::animFrame). Le facteur 30.0 convertit le dt (s) en frames.
    anim.animFrame += dt * 30.0f;

    // Wrap par SOUSTRACTION — Char_TickMoveState 0x574830 @0x574911/@0x574922 :
    //   if (this+248 >= (double)v8) this+248 -= (double)v8.  JAMAIS un modulo (fidélité :
    //   Gfx/MotionCache.h::SampleByCursor — le wrap est une soustraction unique, un modulo
    //   ferait repartir de 0 les états gelés-en-fin-de-clip type Mort/Knockback).
    // Le binaire fait UNE soustraction (`if`), car l'avance vaut exactement 1.0 frame/tick
    // (dt=1/30 @30 FPS, dt*30=1.0) : une passe suffit toujours (sauf clip d'1 frame, où une
    // passe suffit aussi). La boucle `while` ci-dessous en est un sur-ensemble DÉFENSIF
    // (identique pour dt=1/30 ; robuste à un pic de dt), gardée par frameCount>0.
    // frameCount <= 0 (slot non résolu -> Motion_GetFrameCount renvoie 0) -> avance SANS wrap :
    //   durée inconnue, on ne fabrique aucune borne (même politique que Monster_DispatchMotionTick,
    //   Game/AnimationTick.h §5). Garde aussi contre une boucle infinie du `while`.
    if (frameCount > 0) {
        const float fc = static_cast<float>(frameCount);
        while (anim.animFrame >= fc)
            anim.animFrame -= fc;
    }

    s_playerAnimCursorWired = true; // garde de non-régression (cf. header / Player_AnimCursorTickIsWired)
}

bool Player_AnimCursorTickIsWired() {
    return s_playerAnimCursorWired;
}

} // namespace ts2::game

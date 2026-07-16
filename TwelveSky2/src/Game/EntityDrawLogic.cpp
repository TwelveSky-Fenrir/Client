// Game/EntityDrawLogic.cpp — implémentation des fonctions pures déclarées dans
// EntityDrawLogic.h. Voir le header pour les EAs d'origine et les écarts.
#include "Game/EntityDrawLogic.h"

#include <cmath>

namespace ts2::game {

namespace {
float SquaredLen(float dx, float dy, float dz) {
    return dx * dx + dy * dy + dz * dz;
}
} // namespace

// Math_Dist3D 0x53FAA0 : (a1[2]-a2[2])^2 + (a1[1]-a2[1])^2 + (a1[0]-a2[0])^2, sqrt.
float Distance3D(const Vec3& a, const Vec3& b) {
    return std::sqrt(SquaredLen(a.x - b.x, a.y - b.y, a.z - b.z));
}

// Target_IsBeyondClickRange 0x5410D0 : distance caméra -> (pos décalée en Y de
// radius*0.5) >= 10.0. Réutilisée telle quelle comme garde de rendu "près caméra".
bool IsBeyondCameraNearCull(const Vec3& pos, float radius, const Vec3& cameraPos) {
    const float dx = cameraPos.x - pos.x;
    const float dy = cameraPos.y - (pos.y + radius * 0.5f);
    const float dz = cameraPos.z - pos.z;
    return std::sqrt(SquaredLen(dx, dy, dz)) >= 10.0f;
}

EntityDrawFlags ComputeEntityDrawFlags(const EntityRenderState& state,
                                        const DrawCullContext& cull,
                                        int drawPass) {
    EntityDrawFlags flags{};
    if (!state.active) {
        return flags; // this[0] == 0 -> aucune passe ne dessine rien (garde commune)
    }

    const float radius = state.info ? static_cast<float>(state.info->drawSize) : 0.0f;
    const bool nearCullOk = IsBeyondCameraNearCull(state.pos, radius, cull.cameraPos);

    // Char_Draw 0x5805C0 : a2 < 1 ou a2 > 2 -> return immédiat, puis garde near-cull.
    flags.showBody = (drawPass >= 1 && drawPass <= 2) && nearCullOk;

    // Char_DrawShadow 0x580CE0 / Char_DrawReflection 0x581090 : distance au joueur
    // local <= 300 UNITÉS, PUIS garde near-cull (même formule que showBody).
    const bool withinSelfRange = Distance3D(state.pos, cull.localPlayerPos) <= kSelfProximityDrawDistance;
    flags.showShadow     = withinSelfRange && nearCullOk;
    flags.showReflection = withinSelfRange && nearCullOk;

    // Char_DrawOverheadName 0x581440 : garde near-cull seule (pas de cull 300).
    flags.showOverheadName = nearCullOk;

    // showNameTag reste false : objet distinct, voir ComputeNameTagContent.
    return flags;
}

OverheadNameContent ComputeOverheadNameContent(const EntityRenderState& state, const DrawCullContext& cull) {
    OverheadNameContent out{};
    if (!state.active) return out;

    const float radius = state.info ? static_cast<float>(state.info->drawSize) : 0.0f;
    if (!IsBeyondCameraNearCull(state.pos, radius, cull.cameraPos)) return out;

    out.visible = true;
    out.worldPos.x = state.pos.x;
    // Somme ENTIÈRE drawSize + nameplateExtraOffset + 1, puis ajout à pos.y en double
    // (fidèle à `(double)(dword+dword+1) + float`, cf. 0x5814ae).
    const int heightSum = (state.info ? (state.info->drawSize + state.info->nameplateExtraOffset) : 0) + 1;
    out.worldPos.y = static_cast<float>(static_cast<double>(heightSum) + state.pos.y);
    out.worldPos.z = state.pos.z;
    return out;
}

NameTagContent ComputeNameTagContent(const NameTagRenderState& tag, const Vec3& localPlayerPos) {
    NameTagContent out{};
    if (!tag.active) return out;
    if (Distance3D(tag.pos, localPlayerPos) > kSelfProximityDrawDistance) return out;

    out.visible = true;
    const int mode = tag.owner ? tag.owner->nameDisplayMode : 0;
    out.showLevelSuffix = (mode == 1 || mode == 2);
    out.worldPos = { tag.pos.x, tag.pos.y + 2.5f, tag.pos.z };
    return out;
}

// Fx_MeleeSwingDrawMarker 0x5802C0 — libellé de nom des PNJ (g_NpcRenderArray). Voir le
// bandeau de EntityDrawLogic.h pour la décompilation intégrale et les 2 sites d'appel
// (@0x52FC72 mort / @0x531148 vivant, catégorie 4 du survol).
ZoneNpcLabelContent ComputeZoneNpcLabelContent(const ZoneNpcLabelRenderState& npc,
                                                int drawMode,
                                                bool optShowHitMarkers,
                                                const DrawCullContext& cull) {
    ZoneNpcLabelContent out{};
    if (!npc.active) return out; // `if (*((_DWORD*)this + 1))` @0x5802CC

    // v5 = (float)*(int*)(def + 1332) @0x5802E3, puis
    // Target_IsBeyondClickRange(this + 5 /* = pos */, v5) @0x5802F2 — MÊME primitive que
    // le near-cull caméra de Char_Draw (cf. IsBeyondCameraNearCull ci-dessus).
    const float radius = static_cast<float>(npc.clickRange);
    if (!IsBeyondCameraNearCull(npc.pos, radius, cull.cameraPos)) return out;

    // `if (a2 != 1 || g_Opt_ShowHitMarkers && Math_Dist3D(this+5, flt_1687330) <= 300.0)`
    // @0x580332. Au seul site VIVANT (@0x531148) a2 vaut 2 -> court-circuit sur `a2 != 1`.
    const bool showLabel = (drawMode != 1) ||
        (optShowHitMarkers &&
         Distance3D(npc.pos, cull.localPlayerPos) <= kSelfProximityDrawDistance);
    if (!showLabel) return out;

    out.visible    = true;
    out.worldPos.x = npc.pos.x; // @0x58033C
    // Somme ENTIÈRE `def[1332] + 1` PUIS ajout à pos.y en double (fidèle à
    // `(double)(*(_DWORD*)(def + 1332) + 1) + *(this + 6)` @0x580359) — pas d'addition
    // flottante des deux termes séparément.
    out.worldPos.y = static_cast<float>(static_cast<double>(npc.clickRange + 1) + npc.pos.y);
    out.worldPos.z = npc.pos.z; // @0x580362
    out.colorCode  = Quest_GetMarkerSpriteBase(npc.markerDefId); // @0x580372 -> 10 (stub)
    return out;
}

BodyMeshPlacement ComputeBodyMeshPlacement(const EntityRenderState& state) {
    BodyMeshPlacement out{};
    out.useAttachOffset = state.attached;
    if (state.attached) {
        out.angle = 0.0f;
        out.pos   = state.attachPos;
        out.scale = state.attachScale;
    } else {
        out.angle = state.facingOrAnimTimer;
        out.pos   = state.pos;
        out.scale = state.scaleY;
    }
    return out;
}

WeaponOverlayDecision ComputeWeaponOverlayVariant(const EntityRenderState& state, bool highDetailShadows) {
    WeaponOverlayDecision d{};
    const int  modelId          = state.info ? state.info->modelCategoryId : 0;
    const bool isWeaponWearType = state.info && state.info->weaponRenderType == 2;
    const bool isSpecialVariant = (modelId == 1141 || modelId == 1142 || modelId == 1143 || modelId == 1144);

    if (!isWeaponWearType) {
        if (isSpecialVariant) {
            d.kind = WeaponOverlayDecision::Kind::kAliveDeadSpecial;
            d.aliveVariant = state.hp > 0;
            return d;
        }
        d.kind = WeaponOverlayDecision::Kind::kDefaultStage0;
        return d;
    }

    if (!highDetailShadows) {
        d.kind = WeaponOverlayDecision::Kind::kDefaultStage0;
        return d;
    }

    // Palier = 3 - trunc(hp*100/maxHp)/30 ; 100% PV -> palier 0 (pristine),
    // 0% PV -> palier 3 (pire état). Division entière fidèle à l'original
    // (Crt_ftol suivi d'une division entière /30, cf. 0x580879).
    // TODO(fidélité) : l'original ne garde AUCUNE protection contre maxHp == 0
    // (division par zéro FPU) ; on neutralise ici en traitant maxHp<=0 comme
    // ratio 0 plutôt que de reproduire le crash.
    const int maxHp = state.info->maxHp;
    const int ratioPct = (maxHp > 0) ? static_cast<int>((static_cast<double>(state.hp) * 100.0) / static_cast<double>(maxHp)) : 0;
    d.kind = WeaponOverlayDecision::Kind::kWearStage;
    d.wearStageIndex = 3 - (ratioPct / 30);
    return d;
}

TribeMorphOverlayDecision ComputeTribeMorphOverlay(const EntityRenderState& state,
                                                    int selfMorphSkillIndex,
                                                    int morphTableValue) {
    TribeMorphOverlayDecision d{};
    const int modelId = state.info ? state.info->modelCategoryId : 0;
    d.inMorphIdRange = (modelId >= 589 && modelId <= 600);
    if (!d.inMorphIdRange) return d;
    if (selfMorphSkillIndex == -1) return d; // pas morphé -> pas de branchement (dessin normal)

    d.active = true;
    d.itemVariant  = morphTableValue % 100;
    d.upgradeLevel = morphTableValue / 100;

    const int lvl = d.upgradeLevel;
    const bool upgradeLevelIsOdd = (lvl == 1 || lvl == 3 || lvl == 5 || lvl == 7);

    if (upgradeLevelIsOdd) {
        d.overlay = TribeMorphOverlayDecision::Overlay::kUpgradeIcon;
        d.skipNormalBodyDraw = true; // return inconditionnel dans l'original, icône valide ou non
        return d;
    }
    if (d.itemVariant == 3) {
        d.overlay = TribeMorphOverlayDecision::Overlay::kClassOverlay;
        // PAS de skip : l'original enchaîne sur le dessin normal du corps.
    }
    return d;
}

bool ShouldResetAnimTimerAfterUpgradeIconDraw(float facingOrAnimTimer) {
    return facingOrAnimTimer >= 41.0f;
}

Vec3 ComputeAuraOffset(const EntityRenderState& state) {
    return { 0.0f, state.scaleY, 0.0f };
}

StateOverlayCategory ClassifyStateOverlay(const EntityRenderState& state) {
    switch (state.stateCategory) {
        case 0:  return StateOverlayCategory::kSkillId;
        case 5:  return StateOverlayCategory::kClassId;
        case 7:  return StateOverlayCategory::kItemVariant;
        case 12: return StateOverlayCategory::kSpecialMotion;
        default: return StateOverlayCategory::kNone;
    }
}

bool ShouldDrawStateOverlay(int resolvedOverlayId) {
    return resolvedOverlayId != -1;
}

AttachedEffectSlots ComputeAttachedEffectSlots(const EntityRenderState& state) {
    AttachedEffectSlots out{};
    out.slot1 = state.effectSlot1Active; out.slot1Scale = state.effectSlot1Scale;
    out.slot2 = state.effectSlot2Active; out.slot2Scale = state.effectSlot2Scale;
    out.slot3 = state.effectSlot3Active; out.slot3Scale = state.effectSlot3Scale;
    return out;
}

} // namespace ts2::game

// UI/EnterWorldRender.cpp — implémentation du rendu EnterWorld.
// Vérité = Scene_EnterWorldRender 0x52C260 (voir UI/EnterWorldRender.h pour le détail
// EA/formules).
#include "UI/EnterWorldRender.h"
#include "Asset/ImgFile.h" // asset::ImgFile (chargeur .IMG, fond de zone + barre réels)
#include "Game/ClientRuntime.h" // game::Str (StrTable005_Get id 69, même table que
                                 // SceneManager.cpp pour les notices EnterWorld 67/68)
#include "Core/Log.h"
#include <cstdio>
#include <algorithm>

namespace ts2::ui {

namespace {
// Écran nu (état WaitBeforeUnload) — même noir pur que IntroRender::kColBackdrop
// (dword_18C5434, jamais réécrit ailleurs, cf. commentaire de IntroRender.cpp) :
// Gfx_BeginFrame(nullptr) clear avec cette même couleur dans Scene_EnterWorldRender
// (EA 0x52c2b8, MÊME appel que Scene_IntroRender).
constexpr D3DCOLOR kColBackdrop  = Argb(255, 0, 0, 0);
// Replis aplat (texture 008_%05d.IMG absente) : distincts des couleurs Intro/ServerSelect
// pour rester diagnosticables visuellement (pas une couleur réelle du binaire).
constexpr D3DCOLOR kColBgFallback   = Argb(255, 20, 24, 34);
constexpr D3DCOLOR kColBarFallback  = Argb(255, 60, 130, 90);
constexpr D3DCOLOR kColBarEdge      = Argb(255, 120, 200, 150);
constexpr D3DCOLOR kColLabel        = Argb(230, 220, 220, 200);
} // namespace

gfx::GpuTexture* EnterWorldRender::GetBackground(int zoneId) {
    if (!device_) return nullptr;
    auto it = bgCache_.find(zoneId);
    if (it != bgCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    // Sprite2D_BuildPath 0x4D68E0, case 7 -> "G03_GDATA\D01_GIMAGE2D\008\008_%05d.IMG",
    // index = slot+1 ; slot d'origine = previousZoneId (zoneId-1) -> index = zoneId
    // (cf. audit en tête de UI/EnterWorldRender.h).
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/008/008_%05d.IMG", zoneId);
    asset::ImgFile img;
    if (img.Load(path) && tex.CreateFromImgFile(device_, img)) {
        TS2_LOG("EnterWorldRender : fond de zone charge (\"%s\", zoneId=%d, %ux%u)",
                path, zoneId, tex.Width(), tex.Height());
    } else {
        TS2_WARN("EnterWorldRender : fond de zone indisponible, repli aplat (\"%s\", zoneId=%d)",
                 path, zoneId);
    }

    auto res = bgCache_.emplace(zoneId, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

gfx::GpuTexture* EnterWorldRender::GetBarFrame(int slotIndex) {
    if (!device_) return nullptr;
    auto it = barCache_.find(slotIndex);
    if (it != barCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    // Sprite2D_BuildPath case 1 -> "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG", index=slot+1
    // (MÊME atlas/décalage que IntroRender::GetLogoSprite / ServerSelectRender::GetSprite).
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG", slotIndex + 1);
    asset::ImgFile img;
    if (img.Load(path) && tex.CreateFromImgFile(device_, img)) {
        TS2_LOG("EnterWorldRender : barre de progression chargee (\"%s\", slot=%d, %ux%u)",
                path, slotIndex, tex.Width(), tex.Height());
    } else {
        TS2_WARN("EnterWorldRender : frame de barre indisponible, repli aplat (\"%s\", slot=%d)",
                 path, slotIndex);
    }

    auto res = barCache_.emplace(slotIndex, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

void EnterWorldRender::Render(const UiContext& ctx, const game::EnterWorldFlowState& state, int zoneId) {
    // EA 0x52c3d4 : `if (*(this + 1))` — état WaitBeforeUnload (0) : RIEN de plus que le
    // clear noir déjà fait par Gfx_BeginFrame en amont (Scene_EnterWorldRender d'origine).
    // Ce FillRect matérialise ce même "écran nu" (cf. IntroRender::Render, subState==0).
    if (state.state == game::EnterWorldState::WaitBeforeUnload) {
        ctx.FillRect(0, 0, ctx.screenW, ctx.screenH, kColBackdrop);
        return;
    }

    // Fond de zone, centré sur SA taille réelle (repli sur les dimensions nominales si
    // le fichier 008_%05d.IMG est indisponible), EA 0x52c3fd/0x52c426/0x52c433/0x52c45c.
    gfx::GpuTexture* bg = GetBackground(zoneId);
    const bool hasRealBg = bg && bg->Handle() && bg->Width() > 0 && bg->Height() > 0;
    const int bgW = hasRealBg ? static_cast<int>(bg->Width())  : enterworld_layout::kBgW;
    const int bgH = hasRealBg ? static_cast<int>(bg->Height()) : enterworld_layout::kBgH;
    const int baseX = ctx.screenW / 2 - bgW / 2;
    const int baseY = ctx.screenH / 2 - bgH / 2;

    if (ctx.phase == UiPhase::Panels) {
        if (hasRealBg && ctx.sprites && ctx.sprites->Ready()) {
            ctx.sprites->DrawSpriteScaled(bg->Handle(), nullptr, baseX, baseY, 1.0f, 1.0f,
                                          gfx::kSpriteWhite, /*compensatePos=*/true);
        } else {
            ctx.FillRect(baseX, baseY, bgW, bgH, kColBgFallback);
            ctx.DrawFrame(baseX, baseY, bgW, bgH, kColBarEdge, 2);
        }

        // Barre de progression : 21 frames réelles (atlas 001, slots 1140..1160), animée
        // par zoneResourceIndex (0..20, cf. EnterWorldFlowState). EA 0x52c593/0x52c5c8.
        const int barSlot = enterworld_layout::BarFrameSlot(
            std::clamp(state.zoneResourceIndex, 0, 20));
        gfx::GpuTexture* bar = GetBarFrame(barSlot);
        const bool hasRealBar = bar && bar->Handle() && bar->Width() > 0 && bar->Height() > 0;
        const int barX = baseX + enterworld_layout::kBarOffsetX;
        const int barY = baseY + enterworld_layout::kBarOffsetY;
        if (hasRealBar && ctx.sprites && ctx.sprites->Ready()) {
            ctx.sprites->DrawSpriteScaled(bar->Handle(), nullptr, barX, barY, 1.0f, 1.0f,
                                          gfx::kSpriteWhite, /*compensatePos=*/true);
        } else {
            // Repli : rectangle plein simple, largeur proportionnelle à la progression
            // (0..20 sur 21 frames) — approximation visuelle, PAS une reproduction de la
            // sprite d'animation réelle.
            const int fillW = 20 + (std::clamp(state.zoneResourceIndex, 0, 20) * 4);
            ctx.FillRect(barX, barY, fillW, 16, kColBarFallback);
            ctx.DrawFrame(barX, barY, 84, 16, kColBarEdge, 1);
        }
    }

    if (ctx.phase == UiPhase::Text) {
        // EA 0x52c496-0x52c57c : StrTable003_Get(zoneId) + StrTable005_Get(id=69), dessinés
        // via UI_DrawNumberValue (police numérique bitmap dédiée) centrés sur
        // (baseX+363, baseY+475). Reproduit ici par UNE ligne via ctx.Text (police UI
        // normale, PAS le rendu bitmap numérique pixel-exact) — fidélité PARTIELLE
        // assumée et documentée (cf. UI/EnterWorldRender.h, TODO UI_DrawNumberValue).
        char label[64];
        std::snprintf(label, sizeof(label), "Zone %d - %s", zoneId, game::Str(69).c_str());
        const int labelW = ctx.MeasureText(label);
        const int tx = baseX + enterworld_layout::kTextOffsetX - labelW / 2;
        const int ty = baseY + enterworld_layout::kTextOffsetY;
        ctx.Text(label, tx, ty, kColLabel);
    }

    // UI_RenderAllDialogs() (EA 0x52c5d2) : les notices d'erreur (Failed, StrTable005 id
    // 67/68, cf. Game/EnterWorldFlow.h::EnterWorldFlowHost::ShowErrorNotice) sont rendues
    // par le pipeline de prompt existant côté ClientSource (ClientRuntime::PromptState),
    // PAS dupliquées ici — même découplage que GameHud/GameWindows.
}

} // namespace ts2::ui

// UI/PanelSkin.cpp — implémentation (cf. UI/PanelSkin.h pour la méthodologie
// d'identification du dossier/gabarit .IMG utilisée, non confirmée par IDA).
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"

namespace ts2::ui {

void PanelSkin::EnsureLoaded(const UiContext& ctx) const {
    if (tried_) return;
    tried_ = true; // une seule tentative, succès ou échec (pas de re-essai par frame)

    if (!ctx.renderer || !ctx.renderer->Device()) {
        TS2_WARN("PanelSkin : device D3D9 indisponible, repli FillRect (\"%s\")", path_);
        return;
    }

    asset::ImgFile img;
    if (!img.Load(path_)) {
        TS2_WARN("PanelSkin : chargement/decompression impossible, repli FillRect (\"%s\")", path_);
        return;
    }
    if (img.Kind() != asset::ImgKind::TextureDxt) {
        TS2_WARN("PanelSkin : \"%s\" n'est pas une texture DXT (kind=%d), repli FillRect",
                 path_, static_cast<int>(img.Kind()));
        return;
    }
    if (!tex_.CreateFromImgFile(*ctx.renderer, img)) {
        TS2_WARN("PanelSkin : creation texture GPU echouee, repli FillRect (\"%s\")", path_);
        return;
    }
    TS2_LOG("PanelSkin : fond de panneau charge (\"%s\", %ux%u)", path_, tex_.Width(), tex_.Height());
}

bool PanelSkin::Draw(const UiContext& ctx, int x, int y, int w, int h, D3DCOLOR fallbackColor) const {
    if (ctx.phase != UiPhase::Panels) return false; // FillRect/blit sprite : phase Panels uniquement
    if (w <= 0 || h <= 0) return false;

    EnsureLoaded(ctx);

    if (tex_.Valid() && tex_.Width() > 0 && tex_.Height() > 0 &&
        ctx.sprites && ctx.sprites->Ready()) {
        const float sx = static_cast<float>(w) / static_cast<float>(tex_.Width());
        const float sy = static_cast<float>(h) / static_cast<float>(tex_.Height());
        // compensatePos=true : la position affichée reste (x,y) malgré l'échelle,
        // exactement comme UiContext::FillRect (UI/UIManager.cpp).
        ctx.sprites->DrawSpriteScaled(tex_.Handle(), nullptr, x, y, sx, sy,
                                      gfx::kSpriteWhite, /*compensatePos=*/true);
        return true;
    }

    // Repli : rectangle plein coloré existant (comportement inchangé de la mission
    // précédente). Ne bloque jamais le rendu.
    ctx.FillRect(x, y, w, h, fallbackColor);
    return false;
}

} // namespace ts2::ui

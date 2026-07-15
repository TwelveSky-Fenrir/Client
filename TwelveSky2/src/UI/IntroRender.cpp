// UI/IntroRender.cpp — implémentation du rendu Intro.
// Vérité = Scene_IntroRender 0x518880 (voir UI/IntroRender.h pour le détail EA/formules).
#include "UI/IntroRender.h"
#include "Asset/ImgFile.h" // asset::ImgFile (chargeur .IMG, logo réel)
#include "Core/Log.h"
#include <cstdio>

namespace ts2::ui {

namespace {
// RE-VÉRIFIÉ 2026-07-15 (décompilation FRAÎCHE Scene_IntroRender 0x518880 + Gfx_BeginFrame
// 0x6A2280) : Scene_IntroRender appelle Gfx_BeginFrame(g_GfxRenderer, nullptr) (EA 0x5188c3)
// -> IDirect3DDevice9::Clear(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, color=g_GfxRenderer+1308,
// Z=1.0). g_GfxRenderer+1308 (0x800334) vaut 0 -> fond NOIR PUR, assuré par le clear du
// device (Gfx/Renderer::BeginFrame). Scene_IntroRender ne dessine AUCUN quad de fond ni
// aucun texte : uniquement le sprite logo courant (Sprite2D_Draw). Aucune constante de
// couleur/aplat n'est nécessaire ici (tout repli coloré serait une invention non fidèle).
} // namespace

gfx::GpuTexture* IntroRender::GetLogoSprite(const UiContext& ctx, int slotIndex) {
    (void)ctx; // conservé pour compat signature ; le vrai device vient de SetDevice() (cf. .h)
    if (!device_) return nullptr;
    auto it = logoCache_.find(slotIndex);
    if (it != logoCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG", slotIndex + 1);
    asset::ImgFile img;
    if (img.Load(path) && tex.CreateFromImgFile(device_, img)) {
        TS2_LOG("IntroRender : logo charge (\"%s\", slot=%d, %ux%u)", path, slotIndex, tex.Width(), tex.Height());
    } else {
        // Échec de chargement : on ne dessine RIEN (fidèle à Sprite2D_Draw, qui ne dessine
        // rien si EnsureLoaded échoue). AUCUN aplat/label de repli inventé.
        TS2_WARN("IntroRender : logo indisponible (\"%s\", slot=%d) — rien dessine (fidele).", path, slotIndex);
    }

    auto res = logoCache_.emplace(slotIndex, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

void IntroRender::Render(const UiContext& ctx, const game::IntroState& state) {
    // Scene_IntroRender 0x518880 ne dessine AUCUN quad de fond (le clear noir du device
    // suffit, cf. bandeau) ni aucun texte. En sous-état 0 (Init/attente, kIntroWaitFrames)
    // -> RIEN dessiné (EA 0x5189E1 `if (*(this+1))`).
    if (state.subState == 0)
        return;

    // EA 0x518A0B-0x518A8F : logo courant, centré sur SA taille réelle (comme Sprite2D_Draw),
    // plafonné à l'id 830 (slot d'atlas ; fichier réel = slot+1). Dessiné UNIQUEMENT en phase
    // Panels. Si la texture réelle n'est pas disponible -> RIEN (aucun aplat/label inventé,
    // fidèle au binaire qui ne dessine rien quand le sprite n'est pas chargé).
    const int logoId = intro_layout::LogoSpriteIndex(state.subState);
    gfx::GpuTexture* logo = GetLogoSprite(ctx, logoId);
    const bool hasRealLogo = logo && logo->Handle() && logo->Width() > 0 && logo->Height() > 0;
    if (hasRealLogo && ctx.phase == UiPhase::Panels && ctx.sprites && ctx.sprites->Ready()) {
        const int lx = ctx.screenW / 2 - static_cast<int>(logo->Width()) / 2;
        const int ly = ctx.screenH / 2 - static_cast<int>(logo->Height()) / 2;
        ctx.sprites->DrawSpriteScaled(logo->Handle(), nullptr, lx, ly, 1.0f, 1.0f,
                                      gfx::kSpriteWhite, /*compensatePos=*/true);
    }
}

} // namespace ts2::ui

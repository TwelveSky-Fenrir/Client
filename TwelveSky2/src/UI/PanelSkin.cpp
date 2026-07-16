// UI/PanelSkin.cpp — implémentation (cf. UI/PanelSkin.h pour la dérivation
// slot -> fichier, prouvée par désassemblage).
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"

#include <cstdio>

namespace ts2::ui {
namespace {

// Miroir de g_UseTRVariant 0x1669190 — initialisé à 0 par WinMain @0x4609FB.
bool s_useTRVariant = false;

} // namespace

void SetUseTRVariant(bool on) { s_useTRVariant = on; }

// Sprite2D_BuildPath 0x4D6900 case 1 : réplique EXACTE des deux branches.
//   g_UseTRVariant == 1 -> "G03_GDATA\D01_GIMAGE2D\001\TR\001_%05d.IMG"  @0x4d6928
//   sinon               -> "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"     @0x4d6945
// Le numéro de fichier vaut (index de slot) + 1 (argument `a3 + 1`).
std::string Cat1SlotPath(int slot) {
    if (slot < 0) return std::string();
    char buf[80];
    if (s_useTRVariant) // `cmp g_UseTRVariant, 1 / jnz` @0x4d6913
        std::snprintf(buf, sizeof(buf),
                      "G03_GDATA\\D01_GIMAGE2D\\001\\TR\\001_%05d.IMG", slot + 1);
    else
        std::snprintf(buf, sizeof(buf),
                      "G03_GDATA\\D01_GIMAGE2D\\001\\001_%05d.IMG", slot + 1);
    return std::string(buf);
}

void PanelSkin::EnsureLoaded(const UiContext& ctx) const {
    if (tried_) return;
    tried_ = true; // une seule tentative, succès ou échec (pas de re-essai par frame)

    // Résolution paresseuse du chemin pour la forme `Cat1Slot` : elle DOIT se faire
    // ici et pas dans le constructeur, car les PanelSkin sont des `static const`
    // construits avant que App::Init n'ait fixé la variante TR (cf. SetUseTRVariant).
    if (path_.empty() && slot_ >= 0)
        path_ = Cat1SlotPath(slot_);

    if (path_.empty()) {
        TS2_WARN("PanelSkin : aucun chemin (slot=%d), repli appelant", slot_);
        return;
    }

    if (!ctx.renderer || !ctx.renderer->Device()) {
        TS2_WARN("PanelSkin : device D3D9 indisponible, repli (\"%s\")", path_.c_str());
        return;
    }

    asset::ImgFile img;
    if (!img.Load(path_.c_str())) {
        TS2_WARN("PanelSkin : chargement/decompression impossible, repli (\"%s\")", path_.c_str());
        return;
    }
    if (img.Kind() != asset::ImgKind::TextureDxt) {
        TS2_WARN("PanelSkin : \"%s\" n'est pas une texture DXT (kind=%d), repli",
                 path_.c_str(), static_cast<int>(img.Kind()));
        return;
    }
    if (!tex_.CreateFromImgFile(*ctx.renderer, img)) {
        TS2_WARN("PanelSkin : creation texture GPU echouee, repli (\"%s\")", path_.c_str());
        return;
    }
    TS2_LOG("PanelSkin : fond charge (\"%s\", %ux%u)", path_.c_str(), tex_.Width(), tex_.Height());
}

// Blit à TAILLE NATURELLE — Sprite2D_Draw 0x4D6B20 : UI_DrawSprite(this+104, x, y,
// 0,0,0,0,0) @0x4D6B72, sans aucun facteur d'échelle.
bool PanelSkin::Draw(const UiContext& ctx, int x, int y) const {
    if (ctx.phase != UiPhase::Panels) return false; // blit sprite : phase Panels uniquement

    EnsureLoaded(ctx);

    if (!tex_.Valid() || tex_.Width() == 0 || tex_.Height() == 0 ||
        !ctx.sprites || !ctx.sprites->Ready())
        return false; // indisponible : l'appelant gère son propre repli

    ctx.sprites->DrawSpriteScaled(tex_.Handle(), nullptr, x, y, 1.0f, 1.0f,
                                  gfx::kSpriteWhite, /*compensatePos=*/true);
    return true;
}

uint32_t PanelSkin::TexW(const UiContext& ctx) const {
    EnsureLoaded(ctx);
    return tex_.Valid() ? tex_.Width() : 0u;
}

uint32_t PanelSkin::TexH(const UiContext& ctx) const {
    EnsureLoaded(ctx);
    return tex_.Valid() ? tex_.Height() : 0u;
}

// Blit ÉTIRÉ — NON fidèle, conservé pour les 11 appelants existants.
// TODO [ancre 0x4D6B20] : voir le pavé de la déclaration dans UI/PanelSkin.h
// (gap TT-06). Correctif = migration de chaque fenêtre vers Draw(ctx,x,y) +
// TexW/TexH, dans son propre .cpp — hors du périmètre de ce front.
bool PanelSkin::Draw(const UiContext& ctx, int x, int y, int w, int h,
                     D3DCOLOR fallbackColor) const {
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

    // Repli : rectangle plein coloré. Ne bloque jamais le rendu.
    ctx.FillRect(x, y, w, h, fallbackColor);
    return false;
}

} // namespace ts2::ui

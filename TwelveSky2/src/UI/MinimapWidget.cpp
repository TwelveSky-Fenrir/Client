// UI/MinimapWidget.cpp — voir UI/MinimapWidget.h pour le contrat + les EA.
#include "UI/MinimapWidget.h"
#include "Game/StaticNpcLoader.h"

#include <algorithm>
#include <cstdio>

namespace ts2::ui {

namespace {
// --- Palette (ARGB), cohérente avec la charte du HUD (voir UI/GameHud.cpp). ---
constexpr D3DCOLOR kPanelBg        = 0xC0101018u; // fond du panneau (identique cadre vitales)
constexpr D3DCOLOR kPanelBorder    = 0xFF3A3A48u;
constexpr D3DCOLOR kButtonBg       = 0xFF303038u;
constexpr D3DCOLOR kButtonBorder   = 0xFF505060u;
constexpr D3DCOLOR kViewportBg     = 0xD0081018u; // fond « carte » (bleu-nuit translucide)
constexpr D3DCOLOR kViewportBorder = 0xFF2A2A38u;
constexpr D3DCOLOR kSelfDotColor   = 0xFFFFFFFFu; // joueur local (blanc)
constexpr D3DCOLOR kPlayerDotColor = 0xFF40C0FFu; // joueurs distants (cyan)
constexpr D3DCOLOR kMonsterDotColor= 0xFFE03030u; // monstres (rouge)
constexpr D3DCOLOR kNpcDotColor    = 0xFF30D040u; // PNJ de décor (vert, marchands/gardes/quête)
constexpr D3DCOLOR kQuestDotColor  = 0xFFFFD030u; // surbrillance de quête (or, clignotant)
constexpr D3DCOLOR kTextColor      = 0xFFFFFFFFu;

constexpr int kSelfDotSize  = 5;
constexpr int kOtherDotSize = 4;

// Dimensions de panneau (§12a : dimensions réelles des sprites .IMG non lisibles
// statiquement — mêmes limites que le cadre vitales de GameHud, cf. son bandeau
// de tête — estimées ici à partir de la fenêtre de clamp documentée 145x128
// §12b + marges bouton/texte).
constexpr int kSmallPanelW = 150, kSmallPanelH = 150;
constexpr int kBigPanelW   = 210, kBigPanelH   = 260;
constexpr int kToggleW = 20, kToggleH = 16;
constexpr int kViewportMarginSide = 6;
constexpr int kViewportTop        = 22; // sous le bouton bascule
constexpr int kViewportBottomSmall = 6;
constexpr int kViewportBottomBig   = 40; // réserve nom de zone + coordonnées (§12a)

// Rayon monde (unités jeu) visible depuis le centre de la mini-carte. RÉEL dans
// le binaire : bornes de map dword_14A88C8 (bbox 6 floats) + échelle par mode
// dword_14A906C/dword_14A9070 (table 10 floats/mode, indexée this+616) — AUCUNE
// des deux n'est modélisée dans Game/GameState.h (pas de table de bbox de map)
// dans cette passe. Remplacé par une constante empirique (échelle « zoom
// standard » d'un MMORPG de ce gabarit) ; à remplacer par la vraie bbox/échelle
// runtime dès qu'elle sera dumpée (cf. CLAUDE.md, x32dbg).
constexpr float kWorldViewRadius = 4000.0f;
} // namespace

// =============================================================================
// Layout
// =============================================================================
void MinimapWidget::Init(int screenW, int screenH) {
    screenW_ = screenW;
    screenH_ = screenH;
    RecomputeLayout();
}

void MinimapWidget::RecomputeLayout() {
    const int panelW = bigMode_ ? kBigPanelW : kSmallPanelW;
    const int panelH = bigMode_ ? kBigPanelH : kSmallPanelH;

    // Ancrage réel : (nWidth - largeur(fond), 0) @0x6815BD-0x6816E6 — haut-droite,
    // collé au bord supérieur.
    layout_.frame = MmRect{ screenW_ - panelW, 0, panelW, panelH };
    layout_.toggleBtn = MmRect{ layout_.frame.x + 2, layout_.frame.y + 2, kToggleW, kToggleH };

    const int vpBottom = bigMode_ ? kViewportBottomBig : kViewportBottomSmall;
    layout_.viewport = MmRect{
        layout_.frame.x + kViewportMarginSide,
        layout_.frame.y + kViewportTop,
        panelW - 2 * kViewportMarginSide,
        panelH - kViewportTop - vpBottom
    };
}

void MinimapWidget::ToggleSize() { SetBigMode(!bigMode_); }

void MinimapWidget::SetBigMode(bool big) {
    bigMode_ = big;
    RecomputeLayout();
}

// =============================================================================
// Hit-test — §12a bouton bascule + clic générique sur le panneau.
// =============================================================================
bool MinimapWidget::OnMouseDown(int x, int y) {
    if (layout_.toggleBtn.Contains(x, y)) {
        ToggleSize();
        return true;
    }
    // Clic ailleurs sur le panneau : consommé (bloque le passage au monde 3D
    // derrière le HUD, comme GameHud::OnMouseDown sur son propre cadre).
    return layout_.frame.Contains(x, y);
}

// =============================================================================
// Primitives de dessin (mêmes formules que GameHud::DrawFilledRect/DrawBorder,
// dupliquées ici pour ne pas coupler ce widget à GameHud — voir bandeau .h).
// =============================================================================
void MinimapWidget::DrawFilledRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                                   const MmRect& r, D3DCOLOR color) {
    if (!whiteTex || r.w <= 0 || r.h <= 0) return;
    RECT src{ 0, 0, 1, 1 };
    sprite.DrawSpriteScaled(whiteTex, &src, r.x, r.y,
                            static_cast<float>(r.w), static_cast<float>(r.h),
                            color, /*compensatePos=*/true);
}

void MinimapWidget::DrawBorderRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                                   const MmRect& r, int t, D3DCOLOR color) {
    if (t <= 0) return;
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x,           r.y,           r.w, t   }, color);
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x,           r.y + r.h - t, r.w, t   }, color);
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x,           r.y,           t,   r.h }, color);
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x + r.w - t, r.y,           t,   r.h }, color);
}

void MinimapWidget::DrawDot(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                            int cx, int cy, int size, D3DCOLOR color) {
    DrawFilledRect(sprite, whiteTex,
                   MmRect{ cx - size / 2, cy - size / 2, size, size }, color);
}

// =============================================================================
// Projection monde -> écran (§12c, formule d'origine :
// (monde - min) * échelle / (max - min), ici recentrée sur le joueur local
// faute de bbox de map modélisée — voir kWorldViewRadius ci-dessus).
// =============================================================================
bool MinimapWidget::ProjectToViewport(float wx, float wz, float selfX, float selfZ,
                                      int& outX, int& outY) const {
    const float scaleX = (static_cast<float>(layout_.viewport.w) * 0.5f) / kWorldViewRadius;
    const float scaleY = (static_cast<float>(layout_.viewport.h) * 0.5f) / kWorldViewRadius;
    const int cx = layout_.viewport.x + layout_.viewport.w / 2;
    const int cy = layout_.viewport.y + layout_.viewport.h / 2;

    int px = cx + static_cast<int>((wx - selfX) * scaleX);
    int py = cy + static_cast<int>((wz - selfZ) * scaleY);

    const int minX = layout_.viewport.x + 1, maxX = layout_.viewport.x + layout_.viewport.w - 2;
    const int minY = layout_.viewport.y + 1, maxY = layout_.viewport.y + layout_.viewport.h - 2;

    switch (windowMode_) {
        case MinimapWindowMode::Free:
            // §12b case 2 : pas de clamp dans le binaire (blit direct plein
            // cadre) — ici on omet simplement les points hors panneau pour ne
            // pas dessiner par-dessus le reste du HUD.
            if (px < minX || px > maxX || py < minY || py > maxY) return false;
            break;
        case MinimapWindowMode::Full:
        case MinimapWindowMode::ClampedCenter:
        default:
            // §12b case 0/1 : fenêtre clampée — le marqueur reste visible au
            // bord (comportement radar), jamais omis.
            px = std::clamp(px, minX, maxX);
            py = std::clamp(py, minY, maxY);
            break;
    }

    outX = px;
    outY = py;
    return true;
}

// =============================================================================
// DrawPanels — fond + viewport + marqueurs (dans le sprite.Begin()/End() de
// l'appelant, voir GameHud::Render).
// =============================================================================
void MinimapWidget::DrawPanels(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex) {
    // Fond du panneau (§12a — substitut du sprite .IMG unk_8F16A4/unk_8F1738,
    // dimensions réelles non lisibles statiquement, cf. bandeau de tête).
    DrawFilledRect(sprite, whiteTex, layout_.frame, kPanelBg);
    DrawBorderRect(sprite, whiteTex, layout_.frame, 1, kPanelBorder);

    // Bouton bascule taille (§12a).
    DrawFilledRect(sprite, whiteTex, layout_.toggleBtn, kButtonBg);
    DrawBorderRect(sprite, whiteTex, layout_.toggleBtn, 1, kButtonBorder);

    // Zone de projection (« carte ») — le cercle/carré de fond minimal demandé
    // par la mission (carré, fidèle au binaire qui blitte un sprite rectangulaire,
    // pas un disque).
    DrawFilledRect(sprite, whiteTex, layout_.viewport, kViewportBg);
    DrawBorderRect(sprite, whiteTex, layout_.viewport, 1, kViewportBorder);

    // Joueur local au centre (§12c « joueur lui-même » — players[0], cf.
    // Game/GameState.h : GameWorld::Self() garantit un slot valide).
    const game::PlayerEntity& self = game::g_World.Self();
    const float selfX = self.x, selfZ = self.z;

    const int cx = layout_.viewport.x + layout_.viewport.w / 2;
    const int cy = layout_.viewport.y + layout_.viewport.h / 2;
    DrawDot(sprite, whiteTex, cx, cy, kSelfDotSize, kSelfDotColor);

    // Joueurs distants (§12c — roster d'alliance non modélisé côté GameState,
    // cf. TODO du .h : tous les joueurs actifs hors self sont projetés).
    for (size_t i = 1; i < game::g_World.players.size(); ++i) {
        const game::PlayerEntity& p = game::g_World.players[i];
        if (!p.active) continue;
        int px, py;
        if (ProjectToViewport(p.x, p.z, selfX, selfZ, px, py))
            DrawDot(sprite, whiteTex, px, py, kOtherDotSize, kPlayerDotColor);
    }

    // Monstres (§12c) — surbrillance clignotante si désigné par
    // SetQuestHighlightMonster (formule d'origine §9 : ftol(gameTime*2)%2==1,
    // une frame sur deux masquée).
    for (const game::MonsterEntity& m : game::g_World.monsters) {
        if (!m.active) continue;
        int px, py;
        if (!ProjectToViewport(m.x, m.z, selfX, selfZ, px, py)) continue;

        D3DCOLOR color = kMonsterDotColor;
        if (questHighlightMonster_.valid() && m.id == questHighlightMonster_) {
            const bool blinkOn = (static_cast<long>(gfx::g_GameTimeSec * 2.0f) % 2) == 0;
            if (!blinkOn) continue; // clignote : une frame sur deux non dessinée
            color = kQuestDotColor;
        }
        DrawDot(sprite, whiteTex, px, py, kOtherDotSize, color);
    }

    // PNJ de décor (§12c « NPC » — g_NpcRenderArray côté binaire, équivalent
    // client-source game::ZoneNpcs()/StaticNpcLoader, cf. bandeau de tête du
    // .h). Pas de tableau gameplay `game::g_World.npcs` ici : la doc §12c
    // confirme que l'original lit le tableau de RENDU, pas le tableau réseau
    // dword_17AB534 — cf. justification complète dans MinimapWidget.h.
    for (const game::StaticNpcSlot& n : game::ZoneNpcs()) {
        int px, py;
        if (ProjectToViewport(n.x, n.z, selfX, selfZ, px, py))
            DrawDot(sprite, whiteTex, px, py, kOtherDotSize, kNpcDotColor);
    }

    // TODO groupe/alliance : voir bandeau de tête de MinimapWidget.h (aucun
    // roster d'alliance côté GameState) — repli propre, le reste de la
    // mini-carte reste pleinement fonctionnel.
}

// =============================================================================
// DrawText — bouton bascule + (mode grand) nom de zone/coordonnées (§12a),
// dans le font.BeginBatch()/EndBatch() de l'appelant.
// =============================================================================
void MinimapWidget::DrawText(gfx::Font& font) {
    if (!font.Ready()) return;
    char buf[64];

    // Libellé du bouton bascule (texte réel = ServerSelect_GetButtonTextId(nom
    // de map), table StrTable non câblée ici -> symbole générique +/-).
    font.DrawTextStyled(bigMode_ ? "-" : "+",
                        layout_.toggleBtn.x + kToggleW / 2 - 3, layout_.toggleBtn.y + 2,
                        kTextColor, gfx::kStyleShadow);

    if (!bigMode_) return; // petite carte : pas de nom de zone ni de coordonnées (§12a)

    // Nom de zone (approximé par l'id de zone brut, faute de StrTable003 câblée
    // dans ce modèle — cf. Docs/TS2_UI_GAMEHUD_RENDER.md §12a).
    std::snprintf(buf, sizeof(buf), "Zone %d", game::g_World.zoneId);
    int tw = font.MeasureText(buf);
    font.DrawTextStyled(buf, layout_.frame.x + (layout_.frame.w - tw) / 2,
                        layout_.viewport.y + layout_.viewport.h + 4,
                        kTextColor, gfx::kStyleShadow);

    // Coordonnées du joueur local, format "%d , %d" fidèle au binaire (§12a).
    const game::PlayerEntity& self = game::g_World.Self();
    std::snprintf(buf, sizeof(buf), "%d , %d", static_cast<int>(self.x), static_cast<int>(self.z));
    tw = font.MeasureText(buf);
    font.DrawTextStyled(buf, layout_.frame.x + (layout_.frame.w - tw) / 2,
                        layout_.viewport.y + layout_.viewport.h + 18,
                        kTextColor, gfx::kStyleShadow);
}

} // namespace ts2::ui

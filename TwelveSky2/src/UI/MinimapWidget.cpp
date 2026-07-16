// UI/MinimapWidget.cpp — voir UI/MinimapWidget.h pour le contrat + les EA.
#include "UI/MinimapWidget.h"
#include "Game/StaticNpcLoader.h"

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

// --- Géométrie du viewport : PROUVÉE (BEW-01), plus estimée ---------------------------------
// UI_DrawSprite du fond de carte @0x681A6C-0x681AB1 : push 0x80 / push 0x91 (h/w) puis
// destination `[var_89C][4] + 0x2A` (y) et `[var_89C][0] + 4` (x). Idem en mode 2 @0x681D33.
// Le viewport est donc FIXE à (frame.x+4, frame.y+42, 145, 128) — indépendant de bigMode_ —
// et NON un rectangle dérivé de marges (l'ancien kViewportMarginSide/kViewportTop/... était
// une invention). Le panneau, lui, reste estimé (sprites unk_8F16A4/unk_8F1AB0 non lisibles
// statiquement), mais il est dimensionné pour contenir ce viewport prouvé.
constexpr int kViewportX = 0x04; // @0x681A98 (`add edx, 4`)
constexpr int kViewportY = 0x2A; // @0x681A8C (`add eax, 2Ah`)  = 42
constexpr int kViewportW = 0x91; // @0x681A71 (`push 91h`)      = 145
constexpr int kViewportH = 0x80; // @0x681A6C (`push 80h`)      = 128

// Décalage du crop : le self est visé au CENTRE de la fenêtre 145x128.
// @0x681995 (`sub ecx, 48h`) et @0x681A08 (`sub ecx, 40h`).
constexpr int kCropHalfW = 0x48; // 72
constexpr int kCropHalfH = 0x40; // 64

// Bornes d'OMISSION des marqueurs @0x681EC0-0x681F03 (asymétriques dans la cible : 0x96=150 et
// 0xA8=168 ne collent pas exactement à 4+145=149 / 42+128=170 — reproduit tel quel, pas « corrigé »).
constexpr int kMarkerMaxX = 0x96; // @0x681ED3 (`add edx, 96h`)
constexpr int kMarkerMaxY = 0xA8; // @0x681EFB (`add eax, 0A8h`)
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

    // BEW-01 : viewport PROUVÉ (frame + (4,42), 145x128) — cf. constantes ci-dessus. Fixe dans la
    // cible, quel que soit this+612/this+616 : les 3 modes blittent au MÊME endroit, seul le
    // rectangle SOURCE change (@0x681A6C-0x681AB1 vs @0x681D33-0x681D6B).
    layout_.viewport = MmRect{
        layout_.frame.x + kViewportX,
        layout_.frame.y + kViewportY,
        kViewportW,
        kViewportH
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
// BEW-01 — Projection monde -> image -> écran. Formules RÉELLES, relevées à l'octet dans
// UI_GameHud_Render 0x67A3C0 ; elles remplacent l'ancien ProjectToViewport « radar » qui
// utilisait un rayon monde inventé (kWorldViewRadius=4000) ET inversait clamp/omission.
// =============================================================================

// @0x681915-0x68193C (X) et @0x681942-0x681969 (Y). L'ordre FPU exact est :
//   fild imgW ; fld wx ; fsub minX ; fmulp -> imgW*(wx-minX)
//   fld maxX  ; fsub minX ; fdivp -> / (maxX-minX) ; call Crt_ftol (troncature vers zéro).
// Le -wz vient du `fchs` @0x68190D (le monde est en Z, l'image en Y descendant).
void MinimapWidget::WorldToImagePixel(const MinimapSource& s, float wx, float wz,
                                      int& px, int& py) {
    const float spanX = s.maxX - s.minX;          // var_844 - var_848
    const float spanY = s.negMinZ - s.negMaxZ;    // var_83C - var_840
    px = (spanX != 0.0f)
             ? static_cast<int>(static_cast<float>(s.imgW) * (wx - s.minX) / spanX)
             : 0;
    py = (spanY != 0.0f)
             ? static_cast<int>(static_cast<float>(s.imgH) * (-wz - s.negMaxZ) / spanY)
             : 0;
}

// @0x68198F-0x6819FC (X) / @0x681A02-0x681A54 (Y). Structure de la cible, branche par branche :
//   srcX = selfPx - 0x48 ; if (srcX < 0) srcX = 0 ;                          (`jns` @0x68199E)
//                          else if (srcX > imgW-0x91) srcX = imgW-0x91 ;     (`jle` @0x6819CF)
// (Le binaire compense en parallèle la position du marqueur self var_838 ; cette compensation est
// algébriquement équivalente à `sx = frame.x + 4 + px - srcX` dans les TROIS branches — vérifié —
// donc MarkerScreenPos la reproduit sans dupliquer le calcul.)
// NB fidélité : si imgW < 0x91, `imgW-0x91` est négatif et la cible clampe srcX à une valeur
// NÉGATIVE. Reproduit tel quel (aucune minimap réelle n'est plus étroite que 145).
void MinimapWidget::ComputeCrop(const MinimapSource& s, int selfPx, int selfPy,
                                int& srcX, int& srcY) {
    srcX = selfPx - kCropHalfW;                       // @0x681995
    if (srcX < 0) srcX = 0;                           // @0x68199E -> @0x6819B2
    else if (srcX > s.imgW - kViewportW)              // @0x6819C4/@0x6819CF
        srcX = s.imgW - kViewportW;                   // @0x6819FC

    srcY = selfPy - kCropHalfH;                       // @0x681A08
    if (srcY < 0) srcY = 0;                           // @0x681A0E -> @0x681A19
    else if (srcY > s.imgH - kViewportH)              // @0x681A28/@0x681A30
        srcY = s.imgH - kViewportH;                   // @0x681A54
}

// @0x681E7E-0x681F05. Position écran du marqueur + test d'OMISSION (jamais de clamp).
bool MinimapWidget::MarkerScreenPos(int px, int py, int srcX, int srcY,
                                    int& sx, int& sy) const {
    sx = layout_.frame.x + px - srcX + kViewportX;  // @0x681E8C/@0x681E90
    sy = layout_.frame.y + py - srcY + kViewportY;  // @0x681EAB/@0x681EAF
    // @0x681EC3 `jl` / @0x681ED9 `jg` / @0x681EED `jl` / @0x681F00 `jle` -> sinon jmp loc_68229E.
    if (sx < layout_.frame.x + kViewportX)  return false;
    if (sx > layout_.frame.x + kMarkerMaxX) return false;
    if (sy < layout_.frame.y + kViewportY)  return false;
    if (sy > layout_.frame.y + kMarkerMaxY) return false;
    return true;
}

// =============================================================================
// DrawPanels — fond + viewport + marqueurs (dans le sprite.Begin()/End() de
// l'appelant, voir GameHud::Render).
// =============================================================================
void MinimapWidget::DrawPanels(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex) {
    // Fond du panneau (§12a — substitut du sprite .IMG unk_8F16A4/unk_8F1AB0, dimensions réelles
    // non lisibles statiquement). Dessiné AVANT la garde de taille, comme @0x6818A9 (Sprite2D_Draw
    // de unk_8F1AB0) qui précède le `cmp [eax+264h], 1` @0x6818B4.
    DrawFilledRect(sprite, whiteTex, layout_.frame, kPanelBg);
    DrawBorderRect(sprite, whiteTex, layout_.frame, 1, kPanelBorder);

    // Bouton bascule taille (§12a).
    DrawFilledRect(sprite, whiteTex, layout_.toggleBtn, kButtonBg);
    DrawBorderRect(sprite, whiteTex, layout_.toggleBtn, 1, kButtonBorder);

    // GARDE PROUVÉE @0x6818B4 : `cmp dword ptr [eax+264h], 1 ; jnz loc_683934` — en PETITE carte,
    // la cible ne dessine NI fond de carte NI marqueurs. L'ancien code les dessinait toujours.
    if (!bigMode_) return;

    // --- Fond de carte de la zone (BEW-01) --------------------------------------------------
    // this+616 (windowMode_) indexe les 3 textures : `imul ecx, 28h ; add ecx, offset unk_14A9068`
    // @0x681AA8/@0x681AAB. Le provider est câblé depuis Scene/SceneManager.cpp.
    MinimapSource src{};
    const bool haveSrc = sourceProvider_
                         && sourceProvider_(static_cast<int>(windowMode_), src)
                         && src.tex != nullptr && src.imgW > 0 && src.imgH > 0
                         && src.maxX != src.minX && src.negMinZ != src.negMaxZ;

    const game::PlayerEntity& self = game::g_World.Self(); // flt_1687330/flt_1687338 = dword_1687234+0xFC/+0x104
    int srcX = 0, srcY = 0;

    if (!haveSrc) {
        // REPLI (zone sans minimap chargée / provider absent). La cible ne connaît pas ce cas :
        // UI_DrawSprite 0x6A3080 sort immédiatement si l'objet texture n'est pas valide (`if (*this)`
        // @0x6A3080) -> l'aire reste simplement vide. On garde un aplat + bordure pour que le HUD
        // reste lisible, et on continue à projeter les marqueurs (sans crop : srcX=srcY=0).
        DrawFilledRect(sprite, whiteTex, layout_.viewport, kViewportBg);
        DrawBorderRect(sprite, whiteTex, layout_.viewport, 1, kViewportBorder);
    } else if (windowMode_ == MinimapWindowMode::Free) {
        // case 2 @0x681C7D : srcX=srcY=0 (@0x681D1F/@0x681D29) puis UI_DrawSprite avec a4=0
        // (@0x681D33-0x681D6B) -> rect source plein (0,0,imgW,imgH) et blit à la taille NATURELLE
        // de l'image (pas de mise à l'échelle : UI_DrawSprite ne fait que Draw(tex, rect, pos)).
        RECT full{ 0, 0, src.imgW, src.imgH };
        sprite.DrawSprite(src.tex, &full,
                          layout_.frame.x + kViewportX, layout_.frame.y + kViewportY,
                          gfx::kSpriteWhite); // couleur -1 codée en dur @0x6A30FC
    } else {
        // cases 0 (@0x6818FB) et 1 (@0x681ABB) : IDENTIQUES dans la cible (doc §12b « redondant/
        // vestige de refactor ») -> crop 145x128 défilant, centré sur le self et clampé aux bords.
        int selfPx = 0, selfPy = 0;
        WorldToImagePixel(src, self.x, self.z, selfPx, selfPy);
        ComputeCrop(src, selfPx, selfPy, srcX, srcY);
        RECT crop{ srcX, srcY, srcX + kViewportW, srcY + kViewportH }; // a5..a8 @0x681A6C-0x681A80
        sprite.DrawSprite(src.tex, &crop,
                          layout_.frame.x + kViewportX, layout_.frame.y + kViewportY,
                          gfx::kSpriteWhite);
    }

    // --- Marqueurs --------------------------------------------------------------------------
    // Tous passent par la MÊME règle que la cible : pixel image -> écran via le crop courant,
    // puis OMISSION hors bornes (jamais de clamp au bord) — cf. MarkerScreenPos.

    // Joueur local (var_838/var_20). PROUVÉ dans les TROIS modes : cases 0/1 le calculent
    // @0x68196F-0x68198C (avec compensation du clamp) et le mode 2 le calcule aussi
    // @0x681CF1-0x681D1C (frame + px + 4 / frame + py + 0x2A, crop nul) -> pas de gate de mode ici.
    // Le centre du viewport n'est le bon endroit QUE si le crop n'est pas clampé : en bord de carte
    // le self DÉRIVE du centre. L'ancien code le fixait au centre en permanence — infidèle.
    if (haveSrc) {
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, self.x, self.z, px, py);
        if (MarkerScreenPos(px, py, srcX, srcY, sx, sy))
            DrawDot(sprite, whiteTex, sx, sy, kSelfDotSize, kSelfDotColor);
    } else {
        // Repli sans carte : le self au centre (aucune bbox pour projeter quoi que ce soit).
        DrawDot(sprite, whiteTex, layout_.viewport.x + layout_.viewport.w / 2,
                layout_.viewport.y + layout_.viewport.h / 2, kSelfDotSize, kSelfDotColor);
        return; // sans bbox réelle, aucune autre projection n'a de sens
    }

    // Gate de mode des marqueurs d'entités. PROUVÉ pour la boucle PNJ uniquement : le switch PAR
    // ENTRÉE @0x681DDD-@0x681DF3 route case 0 -> 0x681DF8, case 1 -> 0x68204D, et DÉFAUT (mode 2)
    // -> `jmp loc_68229E` = l'étiquette de continuation de boucle -> aucun PNJ dessiné en mode 2.
    // INFERENCE (non prouvée) pour joueurs/monstres : ils ne figurent PAS dans ce bloc de la cible
    // (qui ne projette que g_NpcRenderArray) — ce sont des ajouts de modélisation, cf. bandeau du
    // .h. On les aligne sur la seule règle prouvée voisine plutôt que d'inventer un comportement.
    if (windowMode_ == MinimapWindowMode::Free) return; // @0x681DF3

    // Joueurs distants (§12c — roster d'alliance non modélisé côté GameState, cf. TODO du .h :
    // tous les joueurs actifs hors self sont projetés).
    for (size_t i = 1; i < game::g_World.players.size(); ++i) {
        const game::PlayerEntity& p = game::g_World.players[i];
        if (!p.active) continue;
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, p.x, p.z, px, py);
        if (MarkerScreenPos(px, py, srcX, srcY, sx, sy))
            DrawDot(sprite, whiteTex, sx, sy, kOtherDotSize, kPlayerDotColor);
    }

    // Monstres (§12c) — surbrillance clignotante si désigné par SetQuestHighlightMonster
    // (formule d'origine §9 : ftol(gameTime*2)%2==1, une frame sur deux masquée).
    for (const game::MonsterEntity& m : game::g_World.monsters) {
        if (!m.active) continue;
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, m.x, m.z, px, py);
        if (!MarkerScreenPos(px, py, srcX, srcY, sx, sy)) continue;

        D3DCOLOR color = kMonsterDotColor;
        if (questHighlightMonster_.valid() && m.id == questHighlightMonster_) {
            const bool blinkOn = (static_cast<long>(gfx::g_GameTimeSec * 2.0f) % 2) == 0;
            if (!blinkOn) continue; // clignote : une frame sur deux non dessinée
            color = kQuestDotColor;
        }
        DrawDot(sprite, whiteTex, sx, sy, kOtherDotSize, color);
    }

    // PNJ de décor — la SEULE catégorie que ce bloc de la cible dessine réellement
    // (g_NpcRenderArray 0x1764D14, stride 0x58 ; boucle @0x681D8B bornée par g_NpcCount 0x1687220).
    // Équivalent client-source = game::ZoneNpcs()/StaticNpcLoader (cf. bandeau du .h).
    // Icônes par type (switch sur def+0x520 = fieldB @0x681F19 -> atlas 0x92D/0x930/0x933/0x936/
    // 0x939) non modélisées : l'atlas appartient à GameHud, hors périmètre -> point de couleur unique.
    for (const game::StaticNpcSlot& n : game::ZoneNpcs()) {
        // @0x681DA6 `cmp ds:dword_1764D18[edx], 0 ; jnz` -> slot inactif (entry+4 == 0) = SAUTÉ.
        // Indispensable depuis W7 : ZoneNpcs() est un pool de 100 slots FIXES à TROUS (cf.
        // Game/StaticNpcLoader.h) — l'ancienne boucle, sans ce test, plantait un marqueur à
        // l'origine du monde pour chaque slot vide.
        if (!n.active) continue;
        // @0x681DC0 `cmp dword ptr [ecx+524h], 4 ; jnz` avec ecx = entry->def -> def+1316 =
        // NpcDefRecord::fieldC (Game/ExtraDatabases.h:56). La valeur 4 est exclue de la mini-carte.
        if (n.def && n.def->fieldC == 4u) continue; // 4u : fieldC est un uint32_t (pas de C4389)
        // x @0x1764D28+i*88 (= entry+20) et z @0x1764D30+i*88 (= entry+28) @0x681E01/@0x681E16.
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, n.x, n.z, px, py);
        if (MarkerScreenPos(px, py, srcX, srcY, sx, sy))
            DrawDot(sprite, whiteTex, sx, sy, kOtherDotSize, kNpcDotColor);
    }

    // TODO groupe/alliance : voir bandeau de tête de MinimapWidget.h (aucun roster d'alliance
    // côté GameState) — repli propre, le reste de la mini-carte reste pleinement fonctionnel.
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

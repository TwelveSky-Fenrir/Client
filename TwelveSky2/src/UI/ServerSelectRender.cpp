// UI/ServerSelectRender.cpp — implémentation du rendu ServerSelect.
// Vérité = Scene_ServerSelectRender 0x519250 et ses helpers de layout (voir
// UI/ServerSelectRender.h pour le détail EA/formules).
#include "UI/ServerSelectRender.h"
#include "Core/Types.h" // ts2::kRefWidth/kRefHeight
#include "Core/Log.h"   // TS2_WARN (avertit si un .IMG est illisible ; aucun repli visuel)
#include "Asset/ImgFile.h" // asset::ImgFile (chargeur .IMG, fond/boutons réels)
#include <algorithm>
#include <cstdio>

namespace ts2::ui {

namespace serverselect_layout {

int ButtonOffsetX(int id) {
    // ServerSelect_GetButtonX 0x519F40 : switch(a1) { case 0..9: v3+291; case 60: v3+539; default: 0; }
    switch (id) {
    case 0: case 1: case 2: case 3: case 4:
    case 5: case 6: case 7: case 8: case 9:
        return 291;
    case kSpecialSlotC: // 60
        return 539;
    default:
        return 0;
    }
}

int ButtonOffsetY(int id) {
    // ServerSelect_GetButtonY 0x51A0A0 : switch(a1) { ... } — valeurs EXACTES relevées,
    // y compris la divergence 278 (case 7) vs 287 (case 1), fidèle au désassemblage.
    switch (id) {
    case 0: return 196;
    case 1: return 287;
    case 2: return 378;
    case 3: return 469;
    case 4: return 560;
    case 5: return 651;
    case 6: return 196;
    case 7: return 278; // (pas 287 — écart réel du binaire, pas une coquille)
    case 8: return 378;
    case 9: return 469;
    case kSpecialSlotA: return 196; // 40
    case kSpecialSlotB: return 196; // 50
    case kSpecialSlotC: return 469; // 60
    default: return 0;
    }
}

int ButtonImageId(int id) {
    // ServerSelect_GetButtonImageId 0x51A220.
    switch (id) {
    case 0: return 1786;
    case 1: return 1788;
    case 2: return 1790;
    case 3: return 1792;
    case 4: return 1794;
    case 5: return 1810;
    case 6: return 1798;
    case 7: return 1800;
    case 8: return 1802;
    case 9: return 1808;
    case kSpecialSlotA: return 3452; // 40
    case kSpecialSlotB: return 3099; // 50
    case kSpecialSlotC: return 2630; // 60
    default: return 0;
    }
}

LoadBarOffsets GetLoadBarOffsets(int id) {
    // ServerSelect_DrawLoadBar 0x51A440 : switch(a2) { case 0..9,60: ...; default: (rien,
    // variables locales non initialisées dans le binaire) }. On retourne valid=false pour
    // le défaut plutôt que d'inventer une position.
    LoadBarOffsets r;
    r.valid = true;
    switch (id) {
    case 0: r.barOffX = 291; r.barOffY = 237; r.fullOffX = 443; r.fullOffY = 234; break;
    case 1: r.barOffX = 291; r.barOffY = 328; r.fullOffX = 443; r.fullOffY = 325; break;
    case 2: r.barOffX = 291; r.barOffY = 419; r.fullOffX = 443; r.fullOffY = 416; break;
    case 3: r.barOffX = 291; r.barOffY = 510; r.fullOffX = 443; r.fullOffY = 507; break;
    case 4: r.barOffX = 291; r.barOffY = 601; r.fullOffX = 443; r.fullOffY = 598; break;
    case 5: r.barOffX = 291; r.barOffY = 692; r.fullOffX = 443; r.fullOffY = 689; break;
    case 6: r.barOffX = 291; r.barOffY = 237; r.fullOffX = 443; r.fullOffY = 234; break;
    case 7: r.barOffX = 291; r.barOffY = 328; r.fullOffX = 443; r.fullOffY = 325; break;
    case 8: r.barOffX = 291; r.barOffY = 419; r.fullOffX = 443; r.fullOffY = 416; break;
    case 9: r.barOffX = 291; r.barOffY = 510; r.fullOffX = 443; r.fullOffY = 507; break;
    case kSpecialSlotC: // 60
        r.barOffX = 542; r.barOffY = 510; r.fullOffX = 694; r.fullOffY = 507; break;
    default:
        r.valid = false; break;
    }
    return r;
}

} // namespace serverselect_layout

// Import explicite (plutôt que de compter sur la transitivité des using-directive à
// travers le namespace anonyme ci-dessous) : rend kPanelW/kButtonW/... directement
// visibles dans TOUT le reste de ce fichier (y compris ServerSelectRender::Render,
// défini hors du namespace anonyme).
using namespace serverselect_layout;

namespace {

inline bool PtInRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

inline int SpriteW(gfx::GpuTexture* tex, int fallback) {
    return (tex && tex->Handle() && tex->Width() > 0) ? static_cast<int>(tex->Width()) : fallback;
}

inline int SpriteH(gfx::GpuTexture* tex, int fallback) {
    return (tex && tex->Handle() && tex->Height() > 0) ? static_cast<int>(tex->Height()) : fallback;
}

inline void ProjectActionButton(const UiContext& ctx, int spriteW, int spriteH, int& outX, int& outY) {
    // UI_ProjectSpriteToScreen 0x50F5D0, appelé par ServerSelect avec (imgId=4, 891,701)
    // aux EA 0x5196D1/0x519A79/0x519AED : half = Sprite2D_GetWidth/Height(...)/2 en
    // division ENTIÈRE, puis Crt_ftol(scale * (ref + half)) - half.
    const float scaleX = static_cast<float>(ctx.screenW) / static_cast<float>(ts2::kRefWidth);
    const float scaleY = static_cast<float>(ctx.screenH) / static_cast<float>(ts2::kRefHeight);
    const int halfW = spriteW / 2;
    const int halfH = spriteH / 2;
    outX = static_cast<int>(scaleX * static_cast<float>(891 + halfW) + 0.5f) - halfW;
    outY = static_cast<int>(scaleY * static_cast<float>(701 + halfH) + 0.5f) - halfH;
}

// Palier 0..7 d'une population, EXACTEMENT la chaîne de comparaisons de
// ServerSelect_DrawLoadBar 0x51A440 (EA 0x51a70b-0x51a82f) : level = nombre de k in 1..7
// tel que pop >= k*loadStep (this[13371+i], alimenté par le serveur via le record de
// statut). Borné à 7. NB : si loadStep==0 (statut pas encore reçu) et pop>=0, toutes les
// comparaisons pop>=0 sont vraies -> level 7 (barre pleine, sprite 001_01907.IMG), fidèle
// au binaire (pas d'invention d'un pas maxPop/8).
int LoadLevel(int32_t pop, int32_t loadStep) {
    int level = 0;
    for (; level < 7 && pop >= static_cast<int32_t>(level + 1) * loadStep; ++level) {}
    return level;
}

// Blit d'un sprite atlas à sa taille native (Sprite2D_Draw 0x4d6b20) UNIQUEMENT si la
// texture est réellement chargée ; sinon ne dessine STRICTEMENT RIEN — comportement
// FIDÈLE de Sprite2D_Draw (qui n'affiche rien si EnsureLoaded échoue). AUCUN repli coloré,
// AUCUN cadre, AUCUN texte inventé : le binaire ne dessine que des sprites `.IMG` réels.
// Phase Panels uniquement (lot ID3DXSprite ouvert par l'appelant), no-op hors cette phase.
void DrawAtlas(const UiContext& ctx, gfx::GpuTexture* tex, int x, int y) {
    if (ctx.phase != UiPhase::Panels) return;
    if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0
        && ctx.sprites && ctx.sprites->Ready()) {
        ctx.sprites->DrawSprite(tex->Handle(), nullptr, x, y, gfx::kSpriteWhite);
    }
}

} // namespace

gfx::GpuTexture* ServerSelectRender::GetSprite(int slotIndex) {
    if (!device_) return nullptr; // SetDevice() pas encore appelé -> rien dessiné (pas d'aplat)
    auto it = spriteCache_.find(slotIndex);
    if (it != spriteCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG", slotIndex + 1);
    asset::ImgFile img;
    if (img.Load(path))
        tex.CreateFromImgFile(device_, img);
    else
        TS2_WARN("ServerSelectRender : slot %d illisible (%s) - sprite non dessine.", slotIndex, path);

    auto res = spriteCache_.emplace(slotIndex, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

void ServerSelectRender::Render(const UiContext& ctx, const game::ServerSelectState& state,
                                 int cursorX, int cursorY, bool singleServerMode) {
    // Sous-état Init (this[1]==0) : le binaire ne dessine RIEN d'autre que le clear de
    // device déjà effectué en amont (Gfx_BeginFrame) -> on ne trace rien non plus ici
    // (aucun aplat de repli — fidèle au désassemblage).
    if (state.subState == game::ServerSelectSubState::Init) {
        return;
    }

    // --- Fond plein écran mis à l'échelle (this[168] = backgroundImageId, EA 0x519461) ---
    // scaleX/scaleY = nWidth/kRefWidth, nHeight/kRefHeight (v13/v14 0x519435/0x519419).
    // Texture réelle de l'atlas (2380/2381, cf. Scene_ServerSelectUpdate 0x518B30) dessinée
    // SCALED plein écran ; si le chargement échoue, on ne dessine RIEN (fidèle à
    // Sprite2D_DrawScaled -> EnsureLoaded qui n'affiche rien en cas d'échec — JAMAIS un
    // aplat coloré). Le dessin sprite n'a de sens qu'en phase Panels (lot ID3DXSprite
    // ouvert par l'appelant).
    {
        gfx::GpuTexture* bg = GetSprite(state.backgroundImageId);
        if (ctx.phase == UiPhase::Panels && bg && bg->Handle() && bg->Width() > 0 && bg->Height() > 0
            && ctx.sprites && ctx.sprites->Ready()) {
            const float sx = static_cast<float>(ctx.screenW) / static_cast<float>(bg->Width());
            const float sy = static_cast<float>(ctx.screenH) / static_cast<float>(bg->Height());
            ctx.sprites->DrawSpriteScaled(bg->Handle(), nullptr, 0, 0, sx, sy,
                                          gfx::kSpriteWhite, /*compensatePos=*/true);
        }
    }

    // --- Panneau central (unk_929344, slot 1785 -> 001_01786.IMG), centré
    // (EA 0x519470-0x5194BF). Centrage sur les dimensions RÉELLES de la texture quand
    // chargée (= Sprite2D_GetWidth/Height(unk_929344) de l'original), dimensions nominales
    // kPanelW/kPanelH SEULEMENT pour positionner boutons/barres si la texture n'est pas
    // chargée (positionnement pur, AUCUN dessin de repli).
    gfx::GpuTexture* panelTex = GetSprite(kPanelImgSlot);
    const bool panelValid = panelTex && panelTex->Handle() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    const int baseX = ctx.screenW / 2 - panelW / 2;
    const int baseY = ctx.screenH / 2 - panelH / 2;
    // FIDÉLITÉ : le désassemblage dessine le panneau via un unique
    // `Sprite2D_Draw(&unk_929344, v26, v19)` (EA 0x5194BF) et NE trace AUCUN titre texte
    // (le libellé visible est GRAVÉ dans 001_01786.IMG). Si le sprite n'est pas chargé, on
    // ne dessine RIEN — pas d'aplat coloré, pas de cadre, pas de titre inventé.
    DrawAtlas(ctx, panelTex, baseX, baseY);

    if (singleServerMode) {
        // --- Branche "gros nombre" (g_ServerModeFlag != 0), EA 0x519664-0x5196B0 ---
        // Le binaire dessine ici la population du serveur 0 via UI_DrawNumberValue
        // (EA 0x53FCC0), qui appelle UI_DrawText (EA 0x69E750, mode 2 = contour) : c'est du
        // TEXTE TTF rendu par ID3DXFont::DrawTextA (vtable +56) — PAS des sprites de chiffres
        // `.IMG`. Ce texte passe donc par Gfx/Font (déjà présent), pas par l'atlas de sprites.
        // Le rendu texte de ce nombre n'est pas encore câblé dans ce socle : on ne dessine donc
        // AUCUN nombre plutôt qu'un texte inventé (ce chemin n'est de toute façon pas actif
        // pour la commande de lancement documentée /0/0/2). Seule la barre de charge, faite
        // de sprites `.IMG` réels, est dessinée.
        if (!state.servers.empty()) {
            const auto lb = GetLoadBarOffsets(0);
            if (lb.valid) {
                const int32_t pop = state.servers[0].currentPopulation;
                const int32_t maxPop = state.servers[0].maxPopulation;
                const int32_t loadStep = state.servers[0].loadStep; // this[13371] (pas de palier réel)
                const int barSlot = (pop >= 0) ? kLoadBarStepSlot[LoadLevel(pop, loadStep)] : kLoadBarPendingSlot;
                DrawAtlas(ctx, GetSprite(barSlot), baseX + lb.barOffX, baseY + lb.barOffY);
                if (pop >= maxPop)
                    DrawAtlas(ctx, GetSprite(kLoadBarFullSlot), baseX + lb.fullOffX, baseY + lb.fullOffY);
            }
        }
    } else {
        // --- Branche boucle (g_ServerModeFlag == 0), EA 0x5194DD-0x51964C — CAS RÉELLEMENT
        // ACTIF pour la commande de lancement documentée. L'appelant (LoginScene.cpp) borne
        // désormais [selectedGroupBtnLo, selectedGroupBtnHi] à l'unique entrée SingleServer
        // (plus de 6 canaux) : cette boucle dessine donc exactement UN bouton, fidèle au
        // binaire, pas une grille inventée.
        const int lo = std::max(0, state.selectedGroupBtnLo);
        const int hiTable = std::min(state.selectedGroupBtnHi,
                                     static_cast<int>(state.servers.size()) - 1);
        for (int i = lo; i <= hiTable; ++i) {
            const auto& sv = state.servers[static_cast<size_t>(i)];
            const int x = baseX + ButtonOffsetX(i);
            const int y = baseY + ButtonOffsetY(i);
            const bool populationKnown = sv.currentPopulation >= 0;
            // Fidèle (EA 0x51958C) : état survolé UNIQUEMENT si pop>=0 ET pop<maxPop
            // (this[12371]) — PAS d'échappatoire maxPop<=0. Avec maxPop==0 (statut pas
            // encore reçu), 0<0 est faux, donc le binaire ne dessine JAMAIS l'état survolé.
            const bool notFull = sv.currentPopulation < sv.maxPopulation;
            gfx::GpuTexture* btnTex = GetSprite(ButtonImageId(i));
            // Sprite2D_HitTest 0x51958C/0x5199E6 : rectangle natif du sprite ButtonImageId(i)
            // (001_01787.IMG et variantes = 153x23 à 1024x768), pas un gabarit inventé.
            const bool hover = PtInRect(cursorX, cursorY, x, y,
                                        SpriteW(btnTex, kButtonW), SpriteH(btnTex, kButtonH));

            // Sprite2D_HitTest (EA 0x51958C) + garde population/capacité -> variante
            // "survolée/active" (imageId+1) sinon variante "relâchée" (imageId), EA
            // 0x5195D3-0x51962C. On dessine UNIQUEMENT le sprite `.IMG` réel du bouton (slot
            // ButtonImageId(i), variante hover = slot SUIVANT +1) ; s'il n'est pas chargé,
            // rien n'est dessiné — AUCUN aplat coloré ni libellé texte (le nom/la population
            // sont GRAVÉS dans le sprite du bouton ; la boucle du binaire ne trace aucun
            // texte). Le hit-test ci-dessus partage les dimensions natives du même sprite :
            // rendu et clic coïncident au pixel.
            const bool useHover = hover && populationKnown && notFull;
            DrawAtlas(ctx, useHover ? GetSprite(ButtonImageId(i) + 1) : btnTex, x, y);

            // Barres de charge (EA 0x51964C -> ServerSelect_DrawLoadBar) — sprites RÉELS
            // (9 fichiers 001_01900..001_01908.IMG + 001_02600.IMG) uniquement.
            const auto lb = GetLoadBarOffsets(i);
            if (lb.valid) {
                const int barSlot = populationKnown
                    ? kLoadBarStepSlot[LoadLevel(sv.currentPopulation, sv.loadStep)]
                    : kLoadBarPendingSlot;
                DrawAtlas(ctx, GetSprite(barSlot), baseX + lb.barOffX, baseY + lb.barOffY);
                if (populationKnown && sv.currentPopulation >= sv.maxPopulation)
                    DrawAtlas(ctx, GetSprite(kLoadBarFullSlot), baseX + lb.fullOffX, baseY + lb.fullOffY);
            }
        }
    }

    // --- Bouton retour/action (UI_ProjectSpriteToScreen 0x50F5D0, ancre design (891,701),
    // 3 états RÉELS slots 4/5/6 -> 001_00005/6/7.IMG, unk_8E8DA0/unk_8E8E34/unk_8E8EC8) ---
    gfx::GpuTexture* backNormal = GetSprite(kActionBtnNormalSlot);
    const int backW = SpriteW(backNormal, kBackBtnW);
    const int backH = SpriteH(backNormal, kBackBtnH);
    int backX = 0, backY = 0;
    ProjectActionButton(ctx, backW, backH, backX, backY);
    const bool backHover = PtInRect(cursorX, cursorY, backX, backY, backW, backH);
    const int backSlot = actionButtonPressed_ ? kActionBtnPressedSlot
                        : backHover            ? kActionBtnHoverSlot
                                                : kActionBtnNormalSlot;
    // Sprite `.IMG` réel de l'état courant uniquement ; s'il n'est pas chargé, rien n'est
    // dessiné (AUCUN aplat coloré ni cadre de repli).
    DrawAtlas(ctx, backSlot == kActionBtnNormalSlot ? backNormal : GetSprite(backSlot), backX, backY);

    // Le binaire enchaîne ici sur UI_RenderAllDialogs (EA 0x51974E) : à l'appelant
    // d'invoquer UIManager::Instance().Render() APRÈS ce Render() pour préserver
    // l'ordre (popups au-dessus de l'écran ServerSelect).
}

void ServerSelectRender::OnActionButtonMouseDown(int cursorX, int cursorY, const UiContext& ctx) {
    gfx::GpuTexture* backNormal = GetSprite(serverselect_layout::kActionBtnNormalSlot);
    const int backW = SpriteW(backNormal, serverselect_layout::kBackBtnW);
    const int backH = SpriteH(backNormal, serverselect_layout::kBackBtnH);
    int backX = 0, backY = 0;
    ProjectActionButton(ctx, backW, backH, backX, backY);
    if (PtInRect(cursorX, cursorY, backX, backY, backW, backH))
        actionButtonPressed_ = true;
}

bool ServerSelectRender::OnActionButtonMouseUp(int cursorX, int cursorY, const UiContext& ctx) {
    // Scene_ServerSelectOnMouseUp 0x519AC0 : désarme le latch (this[3]=0, EA 0x519AFE) puis,
    // s'il était armé, re-teste le survol du sprite du bouton (Sprite2D_HitTest unk_8E8DA0,
    // EA 0x519B1A). Renvoie true seulement si le clic complet (down+up) est resté sur le
    // bouton -> l'appelant ouvre la confirmation de sortie (EA 0x519B3E).
    const bool wasPressed = actionButtonPressed_;
    actionButtonPressed_ = false;
    if (!wasPressed) return false;
    gfx::GpuTexture* backNormal = GetSprite(serverselect_layout::kActionBtnNormalSlot);
    const int backW = SpriteW(backNormal, serverselect_layout::kBackBtnW);
    const int backH = SpriteH(backNormal, serverselect_layout::kBackBtnH);
    int backX = 0, backY = 0;
    ProjectActionButton(ctx, backW, backH, backX, backY);
    return PtInRect(cursorX, cursorY, backX, backY, backW, backH);
}

} // namespace ts2::ui

// UI/VendorShopWindow.cpp — implémentation de la fenêtre marchand.
// Voir UI/VendorShopWindow.h pour le contrat d'interaction et les TODO(send).
#include "UI/VendorShopWindow.h"
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"
#include "Game/GameDatabase.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit (400,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec le panneau Marchand (340x372, ratio quasi identique ;
// cf. méthodologie détaillée dans UI/PanelSkin.h). Indice distinct de celui
// utilisé par OptionsWindow (même cluster de tailles, fichiers différents).
// Repli automatique sur kColPanelBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_03640.IMG");

// Tronque `s` (en place) pour que sa largeur rendue ne dépasse pas `maxW` px,
// en ajoutant une ellipse ".." — même esprit que le repli id/nom d'objet de
// InventoryWindow, mais mesuré via la police UI (ctx.MeasureText) puisque le
// nom ITEM_INFO est de longueur variable (contrairement au libellé "#<id>" fixe
// affiché auparavant). No-op si la police est indisponible (MeasureText -> 0).
void TruncateToWidth(const UiContext& ctx, std::string& s, int maxW) {
    if (maxW <= 0) return;
    if (ctx.MeasureText(s.c_str()) <= maxW) return;
    while (!s.empty() && ctx.MeasureText((s + "..").c_str()) > maxW)
        s.pop_back();
    s += "..";
}
} // namespace

VendorShopWindow::VendorShopWindow() {
    // Position par défaut avant le premier Render (référence 1024x768) ; RÉELLEMENT
    // recalculée à CHAQUE frame par RecomputeAnchor(ctx.screenW, ctx.screenH), voir
    // Render() plus bas. Ne PAS retirer cet appel dans Render() : le figer ici comme
    // avant (bug corrigé) désynchronise x_/y_ de la résolution d'affichage réelle dès
    // que celle-ci diffère de la référence au moment de la construction de l'objet.
    RecomputeAnchor(ts2::kRefWidth, ts2::kRefHeight);
}

// Ancrage RÉEL (preuve IDA fraîche, 2026-07-14, ré-audit "coordonnées pétées") :
// UI_NpcShop_Draw (0x5e4910), UI_NpcShop_OnLDown (0x5e44a0) et
// UI_NpcShop_HitTestWindow (0x5e4f60) — les 3 fonctions de la fenêtre « marchand
// PNJ » côté binaire (case 8 du dispatcher UI_NpcWin_Draw_Dispatch 0x5de180) —
// exécutent TOUTES littéralement la même séquence :
//   UI_ProjectSpriteToScreen(&g_PlayerCmdController, /*sprite*/299, /*x*/764, /*y*/182,
//                            &this->x, &this->y);      // 0x50f5d0
//   this->x = (this->x + 23) - Sprite2D_GetWidth(panelSprite);   // post-ajustement X
//   // this->y n'est PAS ré-ajusté après le projet.
// UI_ProjectSpriteToScreen (0x50f5d0) calcule :
//   outX = round(scaleX * (764 + spriteW/2)) - spriteW/2   avec scaleX = écran actuel / référence (1024)
//   outY = round(scaleY * (182 + spriteH/2)) - spriteH/2   avec scaleY = écran actuel / référence (768)
// c'est-à-dire un ANCRAGE design-space (764,182), PAS un centrage écran — et le
// facteur d'échelle réel/référence EST bien appliqué par le moteur d'origine (donc
// PAS un simple `(kRefWidth-kPanelW)/2` figé comme le faisait l'ancien constructeur).
// kPanelW/kPanelH tiennent lieu d'approximation de la largeur/hauteur réelle du
// sprite de fond (non extractible statiquement, chargé depuis un .IMG à l'exécution).
// À l'échelle 1 (résolution 1024x768 = référence, cf. lancement
// `/0/0/2/1024/768` du CLAUDE.md), la formule se réduit exactement à x_=764+23-kPanelW,
// y_=182 — c'est la position utilisée pour les captures d'écran de vérification.
void VendorShopWindow::RecomputeAnchor(int screenW, int screenH) {
    constexpr int kAnchorX = 764; // design-space (UI_NpcShop_Draw/OnLDown/HitTestWindow)
    constexpr int kAnchorY = 182;
    const float scaleX = static_cast<float>(screenW)  / static_cast<float>(ts2::kRefWidth);
    const float scaleY = static_cast<float>(screenH) / static_cast<float>(ts2::kRefHeight);
    const int projX = static_cast<int>(scaleX * (kAnchorX + kPanelW / 2.0f) + 0.5f) - kPanelW / 2;
    const int projY = static_cast<int>(scaleY * (kAnchorY + kPanelH / 2.0f) + 0.5f) - kPanelH / 2;
    x_ = projX + 23 - kPanelW; // post-ajustement X confirmé dans les 3 fonctions IDA
    y_ = projY;                // y non ré-ajusté après le projet (idem binaire)
}

// ============================================================================
// Accès au catalogue (adresses d'origine, via g_Client.Var/VarGet)
// ============================================================================
int VendorShopWindow::EntryCount() const {
    const int n = game::g_Client.VarGet(kAddrEntryCount);
    return n > 0 ? n : 0;
}

int VendorShopWindow::PageCount() const {
    const int n = game::g_Client.VarGet(kAddrPageCount);
    return n > 0 ? n : 1;
}

int VendorShopWindow::Selection() const {
    return game::g_Client.VarGet(kAddrSelection);
}

void VendorShopWindow::SetSelection(int idx) {
    game::g_Client.Var(kAddrSelection) = idx;
}

uint32_t VendorShopWindow::ItemIdAt(int idx) const {
    return static_cast<uint32_t>(game::g_Client.VarGet(kAddrItemIdBase + 4u * static_cast<uint32_t>(idx)));
}

uint32_t VendorShopWindow::PriceAt(int idx, int component) const {
    return static_cast<uint32_t>(game::g_Client.VarGet(
        kAddrPriceBase + 12u * static_cast<uint32_t>(idx) + 4u * static_cast<uint32_t>(component)));
}

// Nom réel via la base ITEM_INFO locale (cf. UI/VendorShopWindow.h : le nom brut
// serveur, p.name 13 o, n'est pas stocké côté GameHandlers_VendorTrade.cpp). Repli
// "#<id>" si la base n'est pas chargée ou si le slot itemId est vide/hors bornes
// (game::GetItemInfo renvoie nullptr) — reste au moins aussi informatif que
// l'ancien affichage brut "#<id>" qu'il remplace.
std::string VendorShopWindow::ItemNameAt(int idx) const {
    const uint32_t id = ItemIdAt(idx);
    if (const game::ItemInfo* info = game::GetItemInfo(id))
        return std::string(info->name, strnlen(info->name, sizeof(info->name)));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%u", id);
    return buf;
}

// Icône d'une entrée catalogue : chemin dérivé d'ITEM_INFO::iconId (PAS itemId,
// cf. commentaire détaillé dans InventoryWindow.cpp/ResolveItemIconPath — même
// pool d'icônes G03_GDATA\D01_GIMAGE2D\002\, gabarit "002_%05u.IMG"). Chargement
// paresseux + cache par itemId (une seule tentative, échec inclus, pas de re-essai
// par frame), pattern identique à InventoryWindow::GetIconTex.
gfx::GpuTexture* VendorShopWindow::GetIconTex(const UiContext& ctx, uint32_t itemId) {
    // Cache PARTAGÉ par chemin de fichier (cf. SetIconCache/ActiveIconCache) : une icône
    // déjà chargée par InventoryWindow/WarehouseWindow/EnchantWindow est réutilisée sans
    // re-décoder/re-uploader en VRAM (même fichier .IMG, même ITEM_INFO::iconId).
    if (!ctx.renderer || !ctx.renderer->Device()) return nullptr;
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return nullptr;
    char path[64];
    std::snprintf(path, sizeof(path), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return ActiveIconCache().GetOrLoad(ctx.renderer->Device(), path);
}

// ============================================================================
// Cycle de vie
// ============================================================================
void VendorShopWindow::Open() {
    Dialog::Open();
    statusText_.clear();
    qty_ = 1;
    // Recale la page courante sur la sélection déjà en cours (ex. : rouverte
    // après avoir été poussée en arrière-plan par une autre fenêtre modale),
    // sinon conserve la dernière page consultée, bornée au nombre de pages réel.
    const int sel = Selection();
    if (sel >= 0 && sel < EntryCount())
        curPage_ = sel / kRowsPerPage + 1;
    ClampPage();
}

void VendorShopWindow::Close() {
    // Ne laisse pas une sélection « en l'air » entre deux ouvertures (fidèle au
    // comportement des handlers, qui remettent Var(0x1826130)=-1 à chaque
    // rechargement de liste — cf. Net/GameHandlers_VendorTrade.cpp, opcode 0x25).
    SetSelection(-1);
    statusText_.clear();
    Dialog::Close();
}

void VendorShopWindow::ClampPage() {
    const int pc = PageCount();
    if (curPage_ < 1)  curPage_ = 1;
    if (curPage_ > pc) curPage_ = pc;
}

// ============================================================================
// Géométrie
// ============================================================================
VendorShopWindow::Rect VendorShopWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

VendorShopWindow::Rect VendorShopWindow::CloseButtonRect() const {
    return { x_ + kPanelW - kGridPad - kCloseSize, y_ + (kHeaderH - kCloseSize) / 2,
             kCloseSize, kCloseSize };
}

VendorShopWindow::Rect VendorShopWindow::RowRect(int rowInPage) const {
    return { x_ + kGridPad, y_ + kHeaderH + kGridPad + rowInPage * (kRowH + kRowGap),
             kPanelW - 2 * kGridPad, kRowH };
}

VendorShopWindow::Rect VendorShopWindow::PrevPageButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kGridPad, footerTop + kFooterRow1Y, kPageBtnW, kPageBtnH };
}

VendorShopWindow::Rect VendorShopWindow::NextPageButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kPanelW - kGridPad - kPageBtnW, footerTop + kFooterRow1Y, kPageBtnW, kPageBtnH };
}

VendorShopWindow::Rect VendorShopWindow::QtyMinusButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kGridPad, footerTop + kFooterRow2Y + (kBuyBtnH - kQtyBtnH) / 2, kQtyBtnW, kQtyBtnH };
}

VendorShopWindow::Rect VendorShopWindow::QtyPlusButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    static constexpr int kQtyNumW = 30; // largeur réservée à l'affichage du nombre
    return { x_ + kGridPad + kQtyBtnW + kQtyNumW, footerTop + kFooterRow2Y + (kBuyBtnH - kQtyBtnH) / 2,
             kQtyBtnW, kQtyBtnH };
}

VendorShopWindow::Rect VendorShopWindow::BuyButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kPanelW - kGridPad - kBuyBtnW, footerTop + kFooterRow2Y, kBuyBtnW, kBuyBtnH };
}

bool VendorShopWindow::RowAt(int mx, int my, int& outRowInPage) const {
    for (int r = 0; r < kRowsPerPage; ++r) {
        const Rect rr = RowRect(r);
        if (PointInRect(mx, my, rr.x, rr.y, rr.w, rr.h)) {
            outRowInPage = r;
            return true;
        }
    }
    return false;
}

bool VendorShopWindow::PointInPanel(int mx, int my) const {
    return PointInRect(mx, my, x_, y_, kPanelW, kPanelH);
}

// ============================================================================
// Événements souris / clavier
// ============================================================================
bool VendorShopWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    // Consomme le clic-enfoncé s'il tombe dans le panneau (règle « premier
    // consommateur gagne ») ; toute la logique d'action est au relâchement
    // (OnClick), comme documenté dans le contrat Dialog.
    return PointInPanel(x, y);
}

bool VendorShopWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    if (!PointInPanel(x, y)) return false;

    const Rect closeBtn = CloseButtonRect();
    if (PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        Close();
        return true;
    }

    int rowInPage = -1;
    if (RowAt(x, y, rowInPage)) {
        HandleRowClick(rowInPage);
        return true;
    }

    const Rect prevBtn = PrevPageButtonRect();
    if (PointInRect(x, y, prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h)) {
        if (curPage_ > 1) { --curPage_; statusText_.clear(); }
        return true;
    }

    const Rect nextBtn = NextPageButtonRect();
    if (PointInRect(x, y, nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h)) {
        if (curPage_ < PageCount()) { ++curPage_; statusText_.clear(); }
        return true;
    }

    const bool hasSel = Selection() >= 0 && Selection() < EntryCount();

    const Rect qtyMinus = QtyMinusButtonRect();
    if (hasSel && PointInRect(x, y, qtyMinus.x, qtyMinus.y, qtyMinus.w, qtyMinus.h)) {
        qty_ = std::max(1, qty_ - 1);
        return true;
    }

    const Rect qtyPlus = QtyPlusButtonRect();
    if (hasSel && PointInRect(x, y, qtyPlus.x, qtyPlus.y, qtyPlus.w, qtyPlus.h)) {
        qty_ = std::min(99, qty_ + 1);
        return true;
    }

    const Rect buyBtn = BuyButtonRect();
    if (hasSel && PointInRect(x, y, buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h)) {
        HandleBuyClick();
        return true;
    }

    // Clic dans le panneau mais hors zone active (fond, en-tête...) : consommé
    // quand même, la fenêtre est modale de fait pendant qu'elle est ouverte.
    return true;
}

bool VendorShopWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// ============================================================================
// Actions
// ============================================================================
void VendorShopWindow::HandleRowClick(int rowInPage) {
    const int idx = (curPage_ - 1) * kRowsPerPage + rowInPage;
    if (idx < 0 || idx >= EntryCount())
        return; // ligne vide de la page courante : rien à sélectionner.

    if (Selection() == idx) {
        // Reclic sur l'entrée déjà sélectionnée : désélection.
        SetSelection(-1);
        statusText_.clear();
        return;
    }

    SetSelection(idx);
    qty_ = 1; // nouvelle sélection : quantité d'achat remise à 1.
    statusText_.clear();
}

void VendorShopWindow::HandleBuyClick() {
    const int sel = Selection();
    if (sel < 0 || sel >= EntryCount()) {
        statusText_ = "Aucun objet selectionne.";
        return;
    }

    const uint32_t itemId = ItemIdAt(sel);
    const uint32_t npcId  = static_cast<uint32_t>(game::g_Client.VarGet(kAddrVendorNpc)); // dword_1837E6C

    // TODO(send): requête d'achat marchand réelle.
    // CORRECTIF (ré-audit 2026-07-14, preuve IDA fraîche) : l'ancien commentaire ci-dessous
    // citait Net_SendVaultReq_215 (sous-opcode 215 du dispatcher Op19) comme candidat — CE
    // N'EST PAS le bon builder, purement une supposition non vérifiée. La décompilation
    // directe du VRAI handler de clic marchand (xrefs_to dword_1834F84 -> fonction 0x6050f0,
    // mal-nommée "UI_SkillBook_OnLUp" par l'analyse IDA mais confirmée être la fenêtre
    // marchand/boutique-joueur PNJ par son ancrage UI_ProjectSpriteToScreen(299,764,182) —
    // même triplet d'ancrage EXACT que UI_NpcShop_Draw/OnLDown/HitTestWindow documenté dans
    // RecomputeAnchor ci-dessus) montre un branchement en DEUX chemins selon que l'entrée
    // appartient à un PNJ ou à une boutique-joueur (comparaison de nom vs byte_1673184,
    // le nom du joueur local) :
    //   - branche PNJ    (nom vendeur == soi n'a pas de sens ici -> vendeur != soi) :
    //       Net_SendPacket_Op35(nc, flag, name13, arg0..arg6)     // opcode SORTANT 35 (0x23)
    //     PAS de dword_1834F84/88/8C dans les arguments : le prix n'est PAS renvoyé au
    //     serveur pour un achat PNJ (le serveur connaît déjà son propre prix catalogue).
    //   - branche boutique-joueur (stall) :
    //       Net_SendPacket_Op109(nc, flag, name13, arg0..arg6, blob28)  // opcode SORTANT 109
    //     blob28 CONTIENT dword_1834F84[3*sel]/1834F88[3*sel]/1834F8C[3*sel] (le prix
    //     affiché ici) parmi ses 7 premiers dwords — cf. EA 0x605612/0x605627/0x60563c.
    // Les DEUX builders existent déjà (Net/SendPackets.h). Le mapping exact des arguments
    // (this+7479/+8479/+9479/+3229 etc. dans le binaire d'origine) correspond à des champs
    // d'un objet fenêtre partagée non modélisé dans VendorShopWindow (page/slot destination
    // inventaire, essentiellement) — non recopiable ici sans réifier cet objet ; câblage
    // réel du bouton Acheter donc TOUJOURS TODO, mais avec la BONNE cible désormais connue
    // (Net_SendPacket_Op35 pour le cas PNJ couvert par cette fenêtre, PAS Op19/215).
    // Le serveur répondrait par Pkt_TradeResult (0x26) ou Net_OnItemBuyResult (0xa4),
    // déjà routés côté Net/GameHandlers_VendorTrade.cpp (mettent à jour g_Client.inv).
    // On ne fabrique PAS l'écriture d'inventaire ici : elle vient du round-trip serveur.
    (void)npcId;

    char line[96];
    std::snprintf(line, sizeof(line), "Achat demande : objet #%u x%d (non envoye).", itemId, qty_);
    statusText_ = line;
    game::g_Client.msg.System(statusText_);
}

// ============================================================================
// Rendu
// ============================================================================
void VendorShopWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;

    // Audit coordonnées 2026-07-14 (ré-vérification "fenêtres mal ajustées") : le
    // commentaire de tête ci-dessus et VendorShopWindow.h annoncent un recalcul
    // À CHAQUE FRAME via RecomputeAnchor(ctx.screenW, ctx.screenH), mais cet appel
    // manquait ici -- seul le constructeur l'invoquait, une fois, avec les
    // constantes kRefWidth/kRefHeight littérales. (x_, y_) restaient donc figés à
    // la position "résolution de référence" pour toute la durée de vie de l'objet
    // au lieu de suivre ctx.screenW/H comme documenté et comme le fait le moteur
    // d'origine (UI_ProjectSpriteToScreen 0x50f5d0). Invisible au lancement par
    // défaut (1024x768 == kRefWidth/kRefHeight => scaleX=scaleY=1, donc même
    // résultat), mais désynchronisé dès que ctx.screenW/H diffère de la référence.
    RecomputeAnchor(ctx.screenW, ctx.screenH);

    const Rect panel    = PanelRect();
    const Rect closeBtn = CloseButtonRect();
    const Rect prevBtn  = PrevPageButtonRect();
    const Rect nextBtn  = NextPageButtonRect();
    const Rect qtyMinus = QtyMinusButtonRect();
    const Rect qtyPlus  = QtyPlusButtonRect();
    const Rect buyBtn   = BuyButtonRect();

    const int  entryCount = EntryCount();
    const int  pageCount  = PageCount();
    const int  sel        = Selection();
    const bool hasSel      = sel >= 0 && sel < entryCount;
    const bool canPrev     = curPage_ > 1;
    const bool canNext     = curPage_ < pageCount;

    if (ctx.phase == UiPhase::Panels) {
        // Fond + cadre du panneau.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColPanelBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        // Bandeau titre.
        ctx.FillRect(panel.x, panel.y, panel.w, kHeaderH, kColHeaderBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kHeaderH, kColFrame, 1);

        // Bouton fermer (croix), survol en rouge.
        const bool closeHover = PointInRect(cursorX, cursorY, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h);
        ctx.FillRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, closeHover ? kColError : kColBtnBg);
        ctx.DrawFrame(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, kColFrame, 1);

        // Lignes du catalogue (page courante).
        for (int r = 0; r < kRowsPerPage; ++r) {
            const int idx = (curPage_ - 1) * kRowsPerPage + r;
            const Rect rr = RowRect(r);
            const bool valid    = idx < entryCount;
            const bool selected = valid && idx == sel;
            const bool hovered  = valid && PointInRect(cursorX, cursorY, rr.x, rr.y, rr.w, rr.h);

            D3DCOLOR bg = (r % 2 == 0) ? kColRowBg : kColRowBgAlt;
            if (selected)      bg = kColSelect;
            else if (hovered)  bg = kColSelect;

            ctx.FillRect(rr.x, rr.y, rr.w, rr.h, bg);
            ctx.DrawFrame(rr.x, rr.y, rr.w, rr.h, kColFrame, 1);

            if (!valid) continue;

            // Icône (carré ancré à gauche de la ligne, kRowH-4 px). Texture réelle si
            // résolue via ITEM_INFO::iconId ; sinon repli rect coloré + cadre (mission :
            // "repli sur rect colore si l'icone ne resout pas").
            const int iconSize = kRowH - 4;
            const int iconX = rr.x + 3;
            const int iconY = rr.y + (rr.h - iconSize) / 2;
            gfx::GpuTexture* icon = GetIconTex(ctx, ItemIdAt(idx));
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 &&
                ctx.sprites && ctx.sprites->Ready()) {
                const float sx = static_cast<float>(iconSize) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(iconSize) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, iconX, iconY, sx, sy,
                                              gfx::kSpriteWhite, /*compensatePos=*/true);
            } else {
                ctx.FillRect(iconX, iconY, iconSize, iconSize, kColIconFallback);
                ctx.DrawFrame(iconX, iconY, iconSize, iconSize, kColFrame, 1);
            }
        }

        // Pagination (précédente/suivante).
        ctx.FillRect(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h, canPrev ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h, kColFrame, 1);
        ctx.FillRect(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h, canNext ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h, kColFrame, 1);

        // Quantité (-/+), actifs seulement si une entrée est sélectionnée.
        ctx.FillRect(qtyMinus.x, qtyMinus.y, qtyMinus.w, qtyMinus.h, hasSel ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(qtyMinus.x, qtyMinus.y, qtyMinus.w, qtyMinus.h, kColFrame, 1);
        ctx.FillRect(qtyPlus.x, qtyPlus.y, qtyPlus.w, qtyPlus.h, hasSel ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(qtyPlus.x, qtyPlus.y, qtyPlus.w, qtyPlus.h, kColFrame, 1);

        // Bouton Acheter, actif seulement si une entrée est sélectionnée.
        const bool buyHover = hasSel && PointInRect(cursorX, cursorY, buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h);
        ctx.FillRect(buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h,
                     !hasSel ? kColBtnBgOff : (buyHover ? kColSelect : kColBtnBg));
        ctx.DrawFrame(buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h, kColFrame, 1);
        return;
    }

    // --- Phase texte ---
    ctx.Text("Marchand", panel.x + kGridPad, panel.y + (kHeaderH - 12) / 2, kColTitle);

    const int closeLblW = ctx.MeasureText("X");
    ctx.Text("X", closeBtn.x + (closeBtn.w - closeLblW) / 2, closeBtn.y + 2, kColText);

    for (int r = 0; r < kRowsPerPage; ++r) {
        const int idx = (curPage_ - 1) * kRowsPerPage + r;
        if (idx >= entryCount) continue;
        const Rect rr = RowRect(r);

        // Prix : composante principale (payload+61) toujours affichée ; les
        // composantes secondaires (payload+65/+69) seulement si non nulles
        // (sémantique exacte des 3 composantes non documentée côté serveur).
        const uint32_t p0 = PriceAt(idx, 0);
        const uint32_t p1 = PriceAt(idx, 1);
        const uint32_t p2 = PriceAt(idx, 2);
        char priceLabel[64];
        if (p1 == 0 && p2 == 0)
            std::snprintf(priceLabel, sizeof(priceLabel), "%u", p0);
        else
            std::snprintf(priceLabel, sizeof(priceLabel), "%u / %u / %u", p0, p1, p2);
        const int priceW = ctx.MeasureText(priceLabel);
        const int priceX = rr.x + rr.w - priceW - 6;
        ctx.Text(priceLabel, priceX, rr.y + (rr.h - 12) / 2, kColTextDim);

        // Nom (ITEM_INFO), tronqué pour ne pas chevaucher le prix. Ancré à droite de
        // l'icône dessinée en phase Panels (kRowH-4 px à rr.x+3).
        const int iconSize = kRowH - 4;
        const int nameX = rr.x + 3 + iconSize + 6;
        std::string name = ItemNameAt(idx);
        TruncateToWidth(ctx, name, priceX - nameX - 6);
        ctx.Text(name.c_str(), nameX, rr.y + (rr.h - 12) / 2, kColText);
    }

    // Pagination : libellés des flèches + « Page X / Y ».
    ctx.Text("<", prevBtn.x + (prevBtn.w - ctx.MeasureText("<")) / 2, prevBtn.y + 3,
              canPrev ? kColText : kColTextDim);
    ctx.Text(">", nextBtn.x + (nextBtn.w - ctx.MeasureText(">")) / 2, nextBtn.y + 3,
              canNext ? kColText : kColTextDim);
    char pageLabel[32];
    std::snprintf(pageLabel, sizeof(pageLabel), "Page %d / %d", curPage_, pageCount);
    const int pageLabelW = ctx.MeasureText(pageLabel);
    ctx.Text(pageLabel, panel.x + (panel.w - pageLabelW) / 2, prevBtn.y + 3, kColText);

    // Quantité (-/+) + valeur courante.
    ctx.Text("-", qtyMinus.x + (qtyMinus.w - ctx.MeasureText("-")) / 2, qtyMinus.y + 2,
              hasSel ? kColText : kColTextDim);
    ctx.Text("+", qtyPlus.x + (qtyPlus.w - ctx.MeasureText("+")) / 2, qtyPlus.y + 2,
              hasSel ? kColText : kColTextDim);
    char qtyLabel[16];
    std::snprintf(qtyLabel, sizeof(qtyLabel), "%d", qty_);
    const int qtyLabelW = ctx.MeasureText(qtyLabel);
    const int qtyLabelX = qtyMinus.x + qtyMinus.w + ((qtyPlus.x - (qtyMinus.x + qtyMinus.w)) - qtyLabelW) / 2;
    ctx.Text(qtyLabel, qtyLabelX, qtyMinus.y + 2, hasSel ? kColText : kColTextDim);

    // Libellé du bouton Acheter.
    const char* buyLabel = hasSel ? "Acheter" : "(selectionner)";
    const int buyLabelW = ctx.MeasureText(buyLabel);
    ctx.Text(buyLabel, buyBtn.x + (buyBtn.w - buyLabelW) / 2,
              buyBtn.y + (buyBtn.h - 12) / 2, hasSel ? kColText : kColTextDim);

    // Or courant (game::g_Client.inv, cf. Game/ClientRuntime.h) + statut d'achat,
    // sur la 3e ligne du pied de fenêtre (sous pagination et qté/Acheter).
    const int footerTop = panel.y + panel.h - kFooterH;
    const int row3Y = footerTop + kFooterRow3Y;
    char goldLine[64];
    std::snprintf(goldLine, sizeof(goldLine), "Or: %lld",
                  static_cast<long long>(game::g_Client.inv.currency));
    const int goldW = ctx.MeasureText(goldLine);
    ctx.Text(goldLine, panel.x + panel.w - kGridPad - goldW, row3Y, kColTextDim);

    if (!statusText_.empty())
        ctx.Text(statusText_.c_str(), panel.x + kGridPad, row3Y, kColSuccess);
}

} // namespace ts2::ui

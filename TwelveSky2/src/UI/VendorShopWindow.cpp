// UI/VendorShopWindow.cpp — implémentation de la fenêtre marchand.
// Voir UI/VendorShopWindow.h pour le contrat d'interaction et les TODO(send).
#include "UI/VendorShopWindow.h"
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"
#include "Game/GameDatabase.h"
#include "Game/GameState.h"    // game::g_World.self.level (g_SelfLevel 0x16731A8)

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ts2::ui {

namespace {

// ---------------------------------------------------------------------------
// Adresses d'origine consultées par la chaîne de gardes d'achat
// (UI_NpcShop_OnRDown_Buy 0x5e5000). Accès via l'échappatoire g_Client.Var/VarGet
// (Game/ClientRuntime.h) — même convention que Game/QuestSystem.cpp (`Var(0x1675B08)`)
// et surtout Game/MapWarp.cpp:191-197, qui garde DÉJÀ une émission avec le MÊME
// couple morph/verrou (EA 0x55CAF9) via WarpAddr::MorphInProgress/CooldownLatch.
// ---------------------------------------------------------------------------
constexpr uint32_t kAddrMorphInProgress = 0x1675A88; // g_MorphInProgress
constexpr uint32_t kAddrCooldownLatch   = 0x1675B08; // g_GmCmdCooldownLatch
constexpr uint32_t kAddrSelfMorphNpcId  = 0x1675A98; // g_SelfMorphNpcId (== 291 => remise 10 %)
constexpr uint32_t kAddrVar16747BC      = 0x16747BC; // dword_16747BC (palier de progression)
constexpr uint32_t kAddrVar16851B8      = 0x16851B8; // dword_16851B8  (carte/faction courante)
constexpr uint32_t kAddrVar167616C      = 0x167616C; // dword_167616C  (garde de l'objet 2314)

// ids StrTable005 EXACTS relevés au désassemblage de UI_NpcShop_OnRDown_Buy 0x5e5000
// (convention Game/CharSelectFlow.cpp:39 : l'id est la vérité, le texte vient de
// game::Str() qui résout la vraie table 005.DAT chargée par App::Init).
// (l'id 117 « sac plein », EA 0x5e5661, n'est pas listé : sa garde dépend de
//  Inventory_FindFreeGridSlot, non modélisée — cf. TODO (f) dans HandleBuyClick)
constexpr int kStrMsg214  = 214;  // ressource insuffisante vs coût (EA 0x5e54aa)
constexpr int kStrMsg606  = 606;  // niveau 113 requis (EA 0x5e5243)
constexpr int kStrMsg1414 = 1414; // monnaie insuffisante vs ITEM_INFO+228 (EA 0x5e54e1)
constexpr int kStrMsg1416 = 1416; // dword_16747BC < 1 (EA 0x5e5281)
constexpr int kStrMsg1909 = 1909; // dword_16747BC < 7 (EA 0x5e52c0)
constexpr int kStrMsg2307 = 2307; // niveau 145 requis (EA 0x5e5302)
constexpr int kStrMsg2385 = 2385; // dword_16851B8 == 50 (EA 0x5e5340)
constexpr int kStrMsg2055 = 2055; // objet interdit ici (EA 0x5e556b)
constexpr int kStrMsg2547 = 2547; // dword_167616C >= 1, objet 2314 (EA 0x5e55d3, `push 9F3h`)

// itemIds testés par le `switch (*v32)` de prérequis (EA 0x5e51aa). Valeurs en hexa
// dans le binaire, reportées telles quelles pour la traçabilité.
constexpr uint32_t kItemPrereqLvl113A = 0x44A; // 1098
constexpr uint32_t kItemPrereqLvl113B = 0x449; // 1097
constexpr uint32_t kItemPrereqLvl113C = 0x417; // 1047
constexpr uint32_t kItemPrereqStage1  = 0x59A; // 1434
constexpr uint32_t kItemPrereqStage7  = 0x219; // 537
constexpr uint32_t kItemPrereqLvl145  = 0x29B; // 667
constexpr uint32_t kItemPrereqMap50   = 0x3D3; // 979

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

// Refus : idiome `Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id),
// g_SysMsgColor)` de UI_NpcShop_OnRDown_Buy 0x5e5000.
// TODO fidélité [ancre 0x84DFD8] : la couleur d'origine g_SysMsgColor n'est pas
// modélisée — MessageLog applique sa couleur blanche par défaut (même limitation
// déjà documentée dans Game/NpcInteraction.cpp:209). L'id StrTable005, lui, est
// résolu RÉELLEMENT par game::Str() (table 005.DAT chargée par App::Init).
void VendorShopWindow::Refuse(int strTableId) {
    statusText_ = game::Str(strTableId);
    game::g_Client.msg.System(statusText_);
}

// ============================================================================
// UI_NpcShop_OnRDown_Buy 0x5e5000 — SEULE émission de la famille UI_NpcShop_*.
// La chaîne de gardes est reproduite dans l'ORDRE EXACT du binaire ; chaque garde
// porte l'EA de la décompilation Hex-Rays.
//
// DIVERGENCE DE MODÈLE ASSUMÉE (héritée, hors périmètre de cette vague) : le binaire
// lit l'objet dans l'ENTRÉE PNJ (`*(*(this+2) + 112*catégorie + 1740 + 4*slot)`,
// EA 0x5e5117) ; cette fenêtre lit le catalogue dword_182613C (cf. bandeau du .h).
// Le npcId du paquet vient lui aussi de l'entrée PNJ (`*(_DWORD *)*(this+2)`).
// ============================================================================
void VendorShopWindow::HandleBuyClick() {
    const int sel = Selection();
    if (sel < 0 || sel >= EntryCount()) {
        statusText_ = "Aucun objet selectionne.";
        return;
    }
    const uint32_t catalogItemId = ItemIdAt(sel);

    // (d) MobDb_GetEntry(mITEM, itemId) — entrée absente => abandon SILENCIEUX,
    //     aucun message (EA 0x5e5117, test 0x5e5123).
    const game::ItemInfo* entry = game::GetItemInfo(catalogItemId);
    if (!entry) return;

    const uint32_t itemId = entry->itemId; // `*v32` du binaire (ITEM_INFO+0)

    // (e) quantité : ITEM_INFO+188 (typeCode == v32[47]) == 2 => 99, sinon 0
    //     (EA 0x5e5134/0x5e5136/0x5e513f).
    //     NB FIDÉLITÉ : UI_NpcShop n'a AUCUN sélecteur de quantité — qty_ (boutons
    //     -/+ de cette fenêtre) est une construction locale héritée qui n'alimente
    //     PAS le paquet. La quantité du fil est CELLE-CI. Cf. rapport (résiduel).
    const int qty = (entry->typeCode == 2u) ? 99 : 0;

    // (f)(g)(h) Inventory_FindFreeGridSlot 0x54DDE0 -> (page, slot2), sinon Msg 117
    //     (EA 0x5e515e -> 0x5e5659) ; puis freeSlot = Inventory_FindFreePageSlot(page)
    //     0x54E1D0 (EA 0x5e5172), col = slot2 % 8 (EA 0x5e5185), row = slot2 / 8
    //     (EA 0x5e5191), et abandon SILENCIEUX si freeSlot == -1 (EA 0x5e519b).
    //
    // TODO [ancre 0x5e515e / 0x5e5172] : aucune de ces deux fonctions n'a d'équivalent
    // côté C++. Elles dépendent de Inventory_BuildOccupancyGrid 0x54E010, de
    // g_Inv_ExtraPageCount 0x16732A8 et du bloc self g_SelfCharInvBlock 0x1673170
    // (indexé [384*page + 80 + 6*slot]) — c'est de l'ALLOCATION d'inventaire, qui
    // relève de Game/ et non de cette fenêtre. Tant qu'elles manquent, page/freeSlot/
    // col/row sont INCONNUS : la chaîne se poursuit pour reproduire fidèlement les
    // refus suivants, mais l'émission finale reste bloquée (voir (p)).

    // (i) prérequis par objet : `switch (*v32)` (EA 0x5e51aa).
    const int     selfLevel   = game::g_World.self.level;                    // g_SelfLevel 0x16731A8
    const int32_t var16747BC  = game::g_Client.VarGet(kAddrVar16747BC);
    const int32_t var16851B8  = game::g_Client.VarGet(kAddrVar16851B8);
    switch (itemId) {
    case kItemPrereqLvl113A: // EA 0x5e51b3
    case kItemPrereqLvl113B: // EA 0x5e51f1
    case kItemPrereqLvl113C: // EA 0x5e5230
        if (selfLevel < 113) { Refuse(kStrMsg606); return; }
        break;
    case kItemPrereqStage1:  // EA 0x5e526f
        if (var16747BC < 1) { Refuse(kStrMsg1416); return; }
        break;
    case kItemPrereqStage7:  // EA 0x5e52ad
        if (var16747BC < 7) { Refuse(kStrMsg1909); return; }
        break;
    case kItemPrereqLvl145:  // EA 0x5e52ef
        if (selfLevel < 145) { Refuse(kStrMsg2307); return; }
        break;
    case kItemPrereqMap50:   // EA 0x5e532e
        if (var16851B8 == 50) { Refuse(kStrMsg2385); return; }
        break;
    default:
        // Branche par défaut (EA 0x5e53a1) : si (dword_1685E74|78|7C|80) est non nul
        // ET itemId ∈ {1447,1448,1449}, le binaire balaie
        // `Crt_Strcmp(&byte_1686334[130*g_LocalElement + 13*i], byte_1673184)` pour
        // i < 10 et refuse par Msg 1506 (EA 0x5e5401) si une correspondance existe.
        // TODO [ancre 0x5e53a1] : table de noms byte_1686334 (10 x 13 o par élément),
        // g_LocalElement 0x1673194 et le nom du joueur local byte_1673184 ne sont pas
        // modélisés dans cette fenêtre — garde non reproductible ici (cf. rapport).
        break;
    }

    // (j) coût (EA 0x5e5420..0x5e548b) : prix unitaire = ITEM_INFO+220 (v32[55],
    //     iBuyCost) ; g_SelfMorphNpcId == 291 => Crt_ftol(prix * 0.9) — troncature
    //     vers zéro, la constante flottante est celle du binaire (double de 0.9f,
    //     EA 0x5e543d / 0x5e5478) ; empilable (v32[47]==2) => coût = qty * prix.
    const int32_t unitPriceRaw = static_cast<int32_t>(entry->field220);
    const bool    morph291     = (game::g_Client.VarGet(kAddrSelfMorphNpcId) == 291);
    const int32_t unitPrice    = morph291
        ? static_cast<int32_t>(static_cast<double>(unitPriceRaw) * 0.8999999761581421) // Crt_ftol 0x760810
        : unitPriceRaw;
    const int64_t cost = (entry->typeCode == 2u)
        ? static_cast<int64_t>(qty) * unitPrice  // EA 0x5e5446 / 0x5e5458
        : static_cast<int64_t>(unitPrice);       // EA 0x5e547d / 0x5e548b

    // (k) g_InvWeight (0x16732AC) < coût => Msg 214 (EA 0x5e5497 -> 0x5e54aa).
    //     NB : le binaire compare bien le champ étiqueté g_InvWeight au COÛT ; le
    //     libellé « weight » est donc à prendre avec réserve (cf. rapport). On
    //     reproduit la comparaison telle quelle, sans réinterpréter.
    if (game::g_Client.inv.weight < cost) { Refuse(kStrMsg214); return; }

    // (l) g_Currency (0x1673180) < ITEM_INFO+228 (v32[57]) => Msg 1414 (EA 0x5e54ce -> 0x5e54e1).
    if (game::g_Client.inv.currency < static_cast<int64_t>(entry->field228)) {
        Refuse(kStrMsg1414);
        return;
    }

    // (m) g_MorphInProgress != 1 && !g_GmCmdCooldownLatch (EA 0x5e5506) : sinon
    //     abandon SILENCIEUX (aucun message). Même couple de gardes, même idiome que
    //     Game/MapWarp.cpp:191-197 (EA 0x55CAF9) -> même représentation (Var).
    if (game::g_Client.VarGet(kAddrMorphInProgress) == 1) return;
    if (game::g_Client.VarGet(kAddrCooldownLatch) != 0) return;

    // (n) (itemId == 2141 && dword_16851B8 != 2) || (itemId == 574 && dword_16851B8 == 40)
    //     => Msg 2055 (EA 0x5e5559 -> LABEL_68 0x5e555b/0x5e556b).
    if ((itemId == 2141u && var16851B8 != 2) || (itemId == 574u && var16851B8 == 40)) {
        Refuse(kStrMsg2055);
        return;
    }

    // (o) itemId == 2314 (EA 0x5e5589) : dword_16851B8 != 40 => Msg 2055 (EA 0x5e5592) ;
    //     PUIS dword_167616C >= 1 => Msg 2547 (EA 0x5e55c1 : `cmp ds:dword_167616C,1 ;
    //     jl` -> `push 9F3h` -> Msg_AppendSystemLine). Ce 2e test est bien IMBRIQUÉ
    //     dans `itemId == 2314`, ce n'est PAS une garde de premier niveau.
    if (itemId == 2314u) {
        if (var16851B8 != 40) { Refuse(kStrMsg2055); return; }
        if (game::g_Client.VarGet(kAddrVar167616C) >= 1) { Refuse(kStrMsg2547); return; }
    }

    // (p) ÉMISSION (EA 0x5e562a) — le binaire émet ICI, inconditionnellement une fois
    //     toutes les gardes ci-dessus franchies :
    //         Net_SendVaultReq_215(npcId, itemId, qty, page, freeSlot, col, row)  // 0x590a10
    //     Args recoupés au désassemblage de UI_NpcShop_OnRDown_Buy (0x5e562a) :
    //         npcId    = *(_DWORD *)*(this+2)                   (1er dword de l'entrée PNJ)
    //         itemId   = *(*(this+2) + 112*cat + 1740 + 4*slot) (case marchande)
    //         qty      = v38 (99 ou 0)
    //         page     = v36 ; freeSlot = FreePageSlot ; col = v33 % 8 ; row = v33 / 8
    //     où (v36 page, v33 slot2) sortent de Inventory_FindFreeGridSlot 0x54DDE0 (garde f :
    //     Msg 117 si sac plein) et FreePageSlot de Inventory_FindFreePageSlot 0x54E1D0
    //     (garde h : -1 => abandon silencieux).
    //
    // BUILDER (recoupage IDA de cette vague) : net::Net_SendVaultReq_215 EXISTE désormais et
    // est BYTE-EXACT (SendPackets.cpp:1423, params int32_t ; trame opcode 0x13 | sous-code
    // 215 (u32 @+9) | bloc 100 o = 7 int32 [npcId,itemId,qty,page,freeSlot,col,row] + 72 o à
    // zéro | total 113) — vérifié vs Net_SendVaultReq_215 0x590a10. L'ancienne mention
    // « wrapper CASSÉ / opcode 0xD7 / params int8_t » est PÉRIMÉE (le builder a été corrigé
    // par la vague dédiée). Le TRANSPORT n'est donc PLUS le blocage.
    //
    // BLOCAGE RÉEL = l'ÉTAT (hors de ce fichier) — on N'ÉMET PAS (règle #8 : ne jamais
    // deviner) :
    //   1. page/freeSlot/col/row (4 des 7 champs) exigent Inventory_FindFreeGridSlot
    //      0x54DDE0 + Inventory_FindFreePageSlot 0x54E1D0 — allocation de SAC (Game/), sans
    //      équivalent C++ (seul WarehouseState::FindFreeSlot 0x54e240 existe, autre
    //      sous-système). Fabriquer ces valeurs placerait l'objet dans une case arbitraire
    //      du sac ET sauterait le refus « sac plein » (Msg 117) : STRICTEMENT PIRE que rien.
    //   2. npcId vient de l'entrée PNJ (*(this+2)) ; cette fenêtre lit le catalogue
    //      dword_182613C — divergence de modèle préexistante (cf. bandeau du .h).
    // Dès que Game/ exposera ces deux allocateurs de sac, l'appel devient direct et fidèle :
    //   if (net::NetClient* nc = net::GlobalNetClient())                    // g_NetClient 0x8156A0
    //       net::Net_SendVaultReq_215(*nc, npcId, itemId, qty, page, freeSlot, col, row);
    // Cf. `missingBuilders`/état manquant du rapport de vague.
    //
    // Effets locaux du binaire, volontairement NON reproduits : ils sont TOUS
    // postérieurs à l'émission (g_VaultOpPending=1 EA 0x5e562f, g_GmCmdCooldownLatch=1
    // EA 0x5e5639, flt_1675B0C=g_GameTimeSec EA 0x5e5649). Les poser sans avoir émis
    // verrouillerait le client sur une requête qui n'est jamais partie.
    // Aucun effet optimiste sur l'inventaire : le binaire n'en fait pas non plus, il
    // attend la réponse (Pkt_TradeResult 0x26 / Net_OnItemBuyResult 0xa4, déjà routés
    // par Net/GameHandlers_VendorTrade.cpp).
    // Retour LOCAL au joueur : pied de fenêtre UNIQUEMENT. Sur son chemin d'émission
    // (0x5e562a+), le binaire ne pousse AUCUNE ligne de chat — seules les branches de refus
    // (Refuse) appellent Msg_AppendSystemLine. On n'écrit donc PAS dans
    // game::g_Client.msg.System ici (règle #4 : pas d'effet que le binaire ne fait pas sur
    // le chemin succès). Tant que l'émission reste bloquée (état de sac manquant, cf. (p)),
    // on se contente d'un indicateur de fenêtre non intrusif.
    char line[96];
    std::snprintf(line, sizeof(line), "Achat : objet #%u x%d (envoi bloque : etat de sac manquant).", itemId, qty);
    statusText_ = line;
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

// UI/InventoryWindow.cpp — implémentation de la fenêtre inventaire & équipement.
// Voir UI/InventoryWindow.h et Docs/TS2_CLIENT_SHELL.md §2.3.
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant
// <windows.h>, qu'UI/InventoryWindow.h tire directement en tête) — même
// convention que UI/ChatWindow.cpp / UI/WarehouseWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h : winsock2 puis windows (ordre sur)
#include "Net/NetClient.h"
#include "UI/InventoryWindow.h"
#include "Asset/ImgFile.h"
#include "Game/GameDatabase.h"
#include "Game/ClientRuntime.h" // game::g_Client.inv (InventoryState) : modèle source de vérité (cf. UI/InventoryWindow.h)
#include "Core/Log.h"

#include <cstring>
#include <cstdio>

namespace ts2::ui {

namespace {
// Chemin .IMG de l'icône d'un objet, dérivé de son itemId — PATTERN DE RÉFÉRENCE partagé
// (dupliqué à l'identique dans WarehouseWindow.cpp / EnchantWindow.cpp pour cette mission :
// IconPathResolver est un pointeur de fonction NON capturant côté InventoryWindow, donc pas
// de header commun pratique sans toucher à l'architecture existante).
//
// CONFIRMÉ par désassemblage (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md, décompilation de
// cGameHud_Render 0x64A900 + Sprite2D_BuildPath 0x4D68E0) : l'index de fichier N'EST PAS
// itemId (ancienne hypothèse, FAUSSE, déduite par sondage de tailles .IMG sans IDA — l'écart
// d'échelle 99999 itemId déclarés vs 4000 slots réels du pool 002\ aurait dû l'exclure). Le
// vrai index est le champ SÉPARÉ ITEM_INFO+192 ("IconID", game::ItemInfo::iconId, 1-based),
// lu via l'accesseur existant game::GetItemInfo(). Le format de chemin "002\002_%05u.IMG"
// (dossier `002`, gabarit `002_%05d.IMG`) était en revanche déjà correct.
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}
} // namespace

// Table des rects des 13 slots d'équipement, RELATIFS à la base (baseX, baseY).
// Relevée telle quelle dans cGameHud_InitLayout 0x62A5B0 (this[4*slot+2..+5]).
// L'ordre suit slotIds[13] = {2,3,4,5,6,7,99,9,10,11,12,13,14}.
// Le slot 6 (id 99, costume inutilisé) est à {0,0,0,0} absolu dans l'original.
namespace {
constexpr int kEquipDelta[13][4] = {
    { 57,  86,  81, 110}, // 0  id 2   (accessoire ~24x24)
    {193,  60, 243, 110}, // 1  id 3
    {139,  60, 189, 110}, // 2  id 4
    { 85, 115, 135, 165}, // 3  id 5
    { 57,  60,  81,  84}, // 4  id 6   (~24x24)
    {139, 115, 189, 165}, // 5  id 7
    {  0,   0,   0,   0}, // 6  id 99  (inutilisé)
    { 85,  60, 135, 110}, // 7  id 9
    {193, 115, 243, 165}, // 8  id 10
    { 34,  87,  84, 137}, // 9  id 11  (page 2)
    { 86,  87, 136, 137}, // 10 id 12
    {138,  87, 188, 137}, // 11 id 13
    {190,  87, 240, 137}, // 12 id 14
};
} // namespace

// ============================================================================
// Init / Shutdown / configuration
// ============================================================================
bool InventoryWindow::Init(gfx::Renderer& renderer, gfx::Font* font) {
    device_ = renderer.Device();
    font_   = font;
    if (!device_) return false;
    if (!sprite_.Create(device_)) return false;
    // Câblage par défaut du résolveur d'icônes d'objet (cf. ResolveItemIconPath ci-dessus) —
    // n'écrase PAS un résolveur déjà posé par l'appelant via SetIconResolver() avant Init().
    if (!iconResolver_) SetIconResolver(&ResolveItemIconPath);
    RecomputeLayout();

    // Texture blanche 1x1 pour FillRect() (rectangle plein uni générique, utilitaire
    // partagé conservé même si aucun appelant actif dans cette fenêtre depuis la
    // correction du retour visuel de glisser — cf. commentaire de Render()) — même
    // technique que UI/UIManager.cpp::CreateWhiteTexture. Non fatal si l'échec :
    // FillRect() tolère whiteTex_==nullptr (pas de retour visuel, pas de crash).
    if (SUCCEEDED(device_->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                         D3DPOOL_MANAGED, &whiteTex_, nullptr))) {
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(whiteTex_->LockRect(0, &lr, nullptr, 0))) {
            *reinterpret_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
            whiteTex_->UnlockRect(0);
        }
    } else {
        TS2_WARN("InventoryWindow : CreateTexture(1x1 blanche) a echoue.");
        whiteTex_ = nullptr;
    }
    return true;
}

void InventoryWindow::Shutdown() {
    if (drag_.active) CancelDrag();
    ownIconCache_.Clear(); // sharedIconCache_ (si injecté) n'appartient pas a cette fenetre — pas libere ici
    background_.Release();
    if (whiteTex_) { whiteTex_->Release(); whiteTex_ = nullptr; }
    sprite_.Destroy();
    device_ = nullptr;
    font_   = nullptr;
}

// Pattern UI/GameHud.cpp::OnDeviceLost/OnDeviceReset : sprite_ (ID3DXSprite propre
// à cette fenêtre) doit être libéré avant Reset() et reconstruit après. Les
// textures (background_/iconCache_/whiteTex_) sont D3DPOOL_MANAGED : le runtime
// D3D9 les restaure seul, aucun traitement supplémentaire requis ici.
void InventoryWindow::OnDeviceLost()  { sprite_.OnLostDevice(); }
void InventoryWindow::OnDeviceReset() { sprite_.OnResetDevice(); }

// Rectangle plein uni — cf. UI/InventoryWindow.h::FillRect (même technique que
// UI/UIManager.cpp::FillRect : blit d'une texture blanche 1x1 mise à l'échelle,
// modulée par `color`, compensatePos=true pour que (x,y) reste la position finale).
void InventoryWindow::FillRect(int x, int y, int w, int h, D3DCOLOR color) {
    if (!whiteTex_ || w <= 0 || h <= 0) return;
    static const RECT kUnitSrc = {0, 0, 1, 1};
    sprite_.DrawSpriteScaled(whiteTex_, &kUnitSrc, x, y,
                             static_cast<float>(w), static_cast<float>(h),
                             color, /*compensatePos=*/true);
}

bool InventoryWindow::SetBackgroundImage(const std::string& imgPath) {
    if (!device_) return false;
    asset::ImgFile img;
    if (!img.Load(imgPath)) return false;
    if (!background_.CreateFromImgFile(device_, img)) return false;
    bgHalfW_ = static_cast<int>(background_.Width())  / 2;
    bgHalfH_ = static_cast<int>(background_.Height()) / 2;
    RecomputeLayout();
    return true;
}

void InventoryWindow::SetScreenSize(int width, int height) {
    screenW_ = (width  > 0) ? width  : ts2::kRefWidth;
    screenH_ = (height > 0) ? height : ts2::kRefHeight;
    RecomputeLayout();
}

// UI_ProjectSpriteToScreen 0x50F5D0 : ancre les coords de référence (kRefX/kRefY)
// à l'échelle écran/1024x768. L'original centre sur le sprite HUD #299 ; on centre
// ici sur le fond du panneau si présent (bgHalfW/H), sinon on ancre le coin haut-gauche.
void InventoryWindow::RecomputeLayout() {
    baseX_ = (screenW_ * kRefX) / ts2::kRefWidth  - bgHalfW_;
    baseY_ = (screenH_ * kRefY) / ts2::kRefHeight - bgHalfH_;
}

// ============================================================================
// Cycle de vie
// ============================================================================
void InventoryWindow::Open() {
    visible_     = true;                     // this[175] = 1
    activeTab_   = 1;                        // this[226] = 1 (inventaire/équipement)
    equipSubTab_ = EquipSubTab::EquipPage1;  // this[227] = 1
    bagPage_     = 0;                        // this[228] = 0
    RecomputeLayout();
}

void InventoryWindow::Close() {
    if (drag_.active) CancelDrag();
    visible_ = false;                        // this[175] = 0
}

void InventoryWindow::Toggle() { visible_ ? Close() : Open(); }

// ============================================================================
// Géométrie / hit-tests
// ============================================================================
InventoryWindow::SlotRect InventoryWindow::EquipSlotRect(int slot) const {
    if (slot < 0 || slot >= 13) return {0, 0, 0, 0};
    const int* d = kEquipDelta[slot];
    if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0)
        return {0, 0, 0, 0}; // slot 99 inutilisé (rect absolu 0)
    return { baseX_ + d[0], baseY_ + d[1], baseX_ + d[2], baseY_ + d[3] };
}

// Slot d'équipement sous le curseur, du sous-onglet courant (rempli OU vide).
int InventoryWindow::EquipSlotRectAt(int mx, int my) const {
    int lo, hi;
    switch (equipSubTab_) {
        case EquipSubTab::EquipPage1: lo = 0; hi = 9;  break;
        case EquipSubTab::EquipPage2: lo = 9; hi = 13; break;
        default:                      return -1; // carquois : géré ailleurs
    }
    for (int i = lo; i < hi; ++i) {
        const SlotRect r = EquipSlotRect(i);
        if (r.r > r.l && mx >= r.l && mx <= r.r && my >= r.t && my <= r.b)
            return i;
    }
    return -1;
}

// cGameHud_EquipSlotAtFilled 0x64EFC0 : slot occupé (g_EquipMain[4*i] > 0) sous le curseur.
int InventoryWindow::EquipSlotAt(int mx, int my) const {
    if (!visible_ || activeTab_ != 1) return -1;
    const int s = EquipSlotRectAt(mx, my);
    if (s < 0) return -1;
    return (game::g_World.self.equip[static_cast<size_t>(s)].itemId > 0) ? s : -1;
}

// Portion « grille 8x8 » de cGameHud_InvCellAt 0x64F9F0 : trouve la case (col,row)
// sous le curseur. rect X = base+26*i+34..+59 ; rect Y = base+26*j+193..+218.
bool InventoryWindow::GridCellAt(int mx, int my, int& col, int& row) const {
    for (int i = 0; i < kGridCols; ++i) {
        const int l = baseX_ + kCellStep * i + kCellOffX;
        const int r = l + kCellSize;
        if (mx < l || mx > r) continue;
        for (int j = 0; j < kGridRows; ++j) {
            const int t = baseY_ + kCellStep * j + kCellOffY;
            const int b = t + kCellSize;
            if (my < t || my > b) continue;
            col = i; row = j;
            return true;
        }
    }
    return false;
}

// cGameHud_InvCellAt 0x64F9F0 : case -> objet la recouvrant (test taille 1x1/2x2).
// Renvoie le SLOT (0..63, StorageCol) dans game::g_Client.inv à la page bagPage_
// courante, ou -1. La page EST l'indexation (row) dans g_Client.inv — inutile de
// filtrer un champ « page » séparé (cf. Game/GameState.h::InvCell, retiré) : on ne
// scanne QUE la page affichée, donc pas de collision possible avec l'autre page.
int InventoryWindow::InvCellAt(int mx, int my) const {
    if (!visible_ || activeTab_ != 1) return -1;
    int col, row;
    if (!GridCellAt(mx, my, col, row)) return -1;
    const uint32_t page = static_cast<uint32_t>(bagPage_);
    for (uint32_t c = 0; c < game::InventoryState::kCols; ++c) {
        const game::InvCell& cell = game::g_Client.inv.At(page, c);
        if (cell.itemId == 0) continue;
        const int sz = ItemGridSize(cell.itemId);
        if (col >= static_cast<int>(cell.gridX) && col < static_cast<int>(cell.gridX) + sz &&
            row >= static_cast<int>(cell.gridY) && row < static_cast<int>(cell.gridY) + sz)
            return static_cast<int>(c);
    }
    return -1;
}

// Taille de grille de l'objet : ITEM_INFO +188 (type). 2/7/11 => 1x1, sinon 2x2
// (cf. MobDb_GetEntry(&mITEM,...) dans 0x64F9F0). Défaut 1x1 si base non chargée.
int InventoryWindow::ItemGridSize(uint32_t itemId) {
    const game::DataTable& db = game::g_World.db.item;
    const uint8_t* rec = db.record(itemId);
    if (rec && db.stride >= 192) {
        const int type = *reinterpret_cast<const int*>(rec + 188);
        return (type == 2 || type == 7 || type == 11) ? 1 : 2;
    }
    return 1;
}

bool InventoryWindow::PointInPanel(int mx, int my) const {
    const int w = background_.Valid() ? static_cast<int>(background_.Width())  : 300;
    const int h = background_.Valid() ? static_cast<int>(background_.Height()) : 420;
    return mx >= baseX_ && mx <= baseX_ + w && my >= baseY_ && my <= baseY_ + h;
}

// ============================================================================
// Icônes
// ============================================================================
gfx::GpuTexture* InventoryWindow::GetIconTex(uint32_t itemId) {
    // Cache PARTAGÉ par chemin de fichier (cf. SetIconCache/ActiveIconCache) : une icône
    // déjà chargée par WarehouseWindow/EnchantWindow/VendorShopWindow (même fichier .IMG,
    // même ITEM_INFO::iconId) est réutilisée sans re-décoder/re-uploader en VRAM.
    if (!iconResolver_ || !device_) return nullptr;
    const std::string path = iconResolver_(itemId);
    return ActiveIconCache().GetOrLoad(device_, path);
}

// Dessine l'icône dans la case ; repli = nom d'objet en texte (passe font différée).
void InventoryWindow::DrawItemIcon(uint32_t itemId, int x, int y, int wPx, int hPx, int count) {
    gfx::GpuTexture* g = GetIconTex(itemId);
    if (g && g->Handle() && g->Width() > 0 && g->Height() > 0) {
        const float sx = static_cast<float>(wPx) / static_cast<float>(g->Width());
        const float sy = static_cast<float>(hPx) / static_cast<float>(g->Height());
        // compensatePos=true => position nette (x,y) malgré la matrice d'échelle.
        sprite_.DrawSpriteScaled(g->Handle(), nullptr, x, y, sx, sy, gfx::kSpriteWhite, true);
    } else {
        const game::ItemInfo* info = game::GetItemInfo(itemId);
        if (info && info->name[0] != '\0') {
            pendingText_.push_back({ x + 2, y + 2,
                                     std::string(info->name, strnlen(info->name, sizeof(info->name))),
                                     kLabelColor });
        }
    }
    if (count > 1)
        pendingText_.push_back({ x + wPx - 14, y + hPx - 14, std::to_string(count), kCountColor });
}

// ============================================================================
// Rendu (sous-ensemble inventaire/équipement de cGameHud_Render 0x64A900)
// ============================================================================
void InventoryWindow::Render() {
    if (!visible_ || activeTab_ != 1) return;
    if (!sprite_.Ready()) return;

    pendingText_.clear();
    sprite_.Begin(D3DXSPRITE_ALPHABLEND);

    // Fond du panneau.
    if (background_.Valid())
        sprite_.DrawSprite(background_.Handle(), nullptr, baseX_, baseY_, gfx::kSpriteWhite);

    // Retour visuel du glisser : PAS de case grisee. CORRIGÉ par désassemblage
    // (mission « retour visuel du glisser-déposer », 2026-07-14, décompilation
    // Item_BeginDragTransaction 0x5AFDF0 + Inv_RemoveItemQuantity 0x5B0340 +
    // UI_StorageWin_Draw 0x5D6610 + maybe_UI_QuickSlotBar_Render 0x5BE340) :
    // dans le binaire, la case source est simplement VIDÉE (itemId à 0) au
    // moment de la saisie et son icône n'est donc plus dessinée DU TOUT — pas
    // de teinte grisée par-dessus. BeginPickup() ci-dessous vide déjà `equip[es]`/
    // `src` de la même façon, donc les boucles Équipement/Sac plus bas ne
    // dessinent RIEN pour cette case automatiquement : aucun traitement
    // supplémentaire n'est nécessaire ici. L'ancien FillRect(dragSourceRect_,
    // kDragSourceCol) — approximation non confirmée par le binaire — a été
    // retiré (avec le champ dragSourceRect_/la couleur kDragSourceCol, devenus
    // sans usage).

    // Équipement : slots occupés du sous-onglet courant.
    {
        int lo = 0, hi = 0;
        if (equipSubTab_ == EquipSubTab::EquipPage1) { lo = 0; hi = 9;  }
        else if (equipSubTab_ == EquipSubTab::EquipPage2) { lo = 9; hi = 13; }
        const auto& eq = game::g_World.self.equip;
        for (int i = lo; i < hi; ++i) {
            if (eq[static_cast<size_t>(i)].itemId == 0) continue;
            const SlotRect r = EquipSlotRect(i);
            const int w = r.r - r.l, h = r.b - r.t;
            if (w <= 0 || h <= 0) continue;
            DrawItemIcon(eq[static_cast<size_t>(i)].itemId, r.l, r.t, w, h, 1);
        }
    }

    // Sac : objets de la page courante (bagPage_), lus depuis game::g_Client.inv —
    // la page EST l'index (row) dans la grille, donc aucun filtrage supplémentaire
    // n'est nécessaire (on ne parcourt que les 64 slots de CETTE page).
    {
        const uint32_t page = static_cast<uint32_t>(bagPage_);
        for (uint32_t k = 0; k < game::InventoryState::kCols; ++k) {
            const game::InvCell& c = game::g_Client.inv.At(page, k);
            if (c.itemId == 0) continue;
            const int sz = ItemGridSize(c.itemId);
            const int x  = baseX_ + kCellStep * static_cast<int>(c.gridX) + kCellOffX;
            const int y  = baseY_ + kCellStep * static_cast<int>(c.gridY) + kCellOffY;
            const int px = kCellStep * sz - 1;
            DrawItemIcon(c.itemId, x, y, px, px, static_cast<int>(c.flag ? c.flag : 1));
        }
    }

    // Objet en cours de glissement, collé sous le curseur.
    if (drag_.active && drag_.itemId) {
        const int sz = ItemGridSize(drag_.itemId);
        const int px = kCellStep * sz - 1;
        DrawItemIcon(drag_.itemId, cursorX_ - drag_.grabOffsetX, cursorY_ - drag_.grabOffsetY,
                     px, px, drag_.count);
    }

    sprite_.End();

    // Passe texte (compteurs de pile + repli id) — HORS du batch sprite (la police a le sien).
    if (font_ && font_->Ready() && !pendingText_.empty()) {
        font_->BeginBatch();
        for (const TextItem& t : pendingText_)
            font_->DrawTextStyled(t.text.c_str(), t.x, t.y, t.color, gfx::kStyleShadow);
        font_->EndBatch();
    }
}

// ============================================================================
// Drag&drop — « clic pour prendre / reclic pour poser »
// ============================================================================
uint32_t InventoryWindow::DragColor() const {
    return (drag_.srcType == DragSource::Bag) ? dragBagCell_.color : dragEquipCell_.socket;
}
uint32_t InventoryWindow::DragDurability() const {
    return (drag_.srcType == DragSource::Bag) ? dragBagCell_.durability : dragEquipCell_.extra0;
}

// Item_BeginDragTransaction 0x5AFDF0 + Inv_RemoveItemQuantity 0x5B0340 :
// on capture l'objet sur le curseur ET on le retire de sa source.
// Ordre fidèle : équipement testé avant le sac (cf. cGameHud_OnMouseDown).
bool InventoryWindow::BeginPickup(int mx, int my) {
    const int es = EquipSlotAt(mx, my);
    if (es >= 0) {
        game::EquipSlot& e = game::g_World.self.equip[static_cast<size_t>(es)];
        dragEquipCell_     = e;
        drag_.active       = true;
        drag_.srcType      = DragSource::Equip;
        drag_.srcPage      = 0;
        drag_.srcSlot      = es;
        drag_.itemId       = e.itemId;
        drag_.count        = 1;
        drag_.grabOffsetX  = kCellSize / 2;
        drag_.grabOffsetY  = kCellSize / 2;
        e = game::EquipSlot{};            // retire de la source (case rendue vide, cf. Render())
        return true;
    }
    const int ic = InvCellAt(mx, my);
    if (ic >= 0) {
        game::InvCell& src = game::g_Client.inv.At(static_cast<uint32_t>(bagPage_),
                                                    static_cast<uint32_t>(ic));
        dragBagCell_     = src;
        drag_.active     = true;
        drag_.srcType    = DragSource::Bag;
        drag_.srcPage    = bagPage_;
        drag_.srcSlot    = ic;
        drag_.itemId     = dragBagCell_.itemId;
        drag_.count      = static_cast<int>(dragBagCell_.flag ? dragBagCell_.flag : 1);
        drag_.grabOffsetX = kCellSize / 2;
        drag_.grabOffsetY = kCellSize / 2;
        src = game::InvCell{};            // retire de la source (case rendue vide, cf. Render())
        return true;
    }
    return false;
}

// Pose l'objet du curseur sur la cible ; échange si la cible est occupée.
bool InventoryWindow::PlaceDrag(int mx, int my) {
    if (!drag_.active) return false;

    // Snapshot AVANT mutation, pour le point d'accroche réseau (cf. NotifyServerItemMove) :
    // en cas d'échange, drag_ est réécrit ci-dessous pour représenter le NOUVEL objet
    // resté sur le curseur — le hook réseau doit voir le déplacement qui vient d'avoir lieu.
    const DragContext preMove = drag_;

    // Cible = slot d'équipement ?
    const int es = EquipSlotRectAt(mx, my);
    if (es >= 0) {
        game::EquipSlot& e = game::g_World.self.equip[static_cast<size_t>(es)];
        if (e.itemId == 0) {
            e.itemId = drag_.itemId;
            e.socket = DragColor();
            e.extra0 = DragDurability();
            drag_.reset();
        } else {
            // Échange : l'ancien équipement passe sur le curseur.
            const game::EquipSlot old = e;
            e.itemId = drag_.itemId;
            e.socket = DragColor();
            e.extra0 = DragDurability();
            dragEquipCell_ = old;
            drag_.srcType  = DragSource::Equip;
            drag_.srcSlot  = es;
            drag_.itemId   = old.itemId;
            drag_.count    = 1;
        }
        NotifyServerItemMove(preMove); // repli propre documenté (cf. UI/InventoryWindow.h)
        return true;
    }

    // Cible = case du sac ?
    int col, row;
    if (GridCellAt(mx, my, col, row)) {
        const uint32_t page = static_cast<uint32_t>(bagPage_);
        const int occ = InvCellAt(mx, my);

        game::InvCell nc{};
        nc.itemId     = drag_.itemId;
        nc.gridX      = static_cast<uint32_t>(col);
        nc.gridY      = static_cast<uint32_t>(row);
        nc.flag       = static_cast<uint32_t>(drag_.count > 0 ? drag_.count : 1);
        nc.color      = DragColor();
        nc.durability = DragDurability();

        if (occ < 0) {
            // Case libre : la cellule est ancrée exactement à (col,row) -> le slot de
            // stockage EST StorageCol(col,row) (cf. UI/InventoryWindow.h::StorageCol).
            game::g_Client.inv.At(page, StorageCol(nc.gridX, nc.gridY)) = nc;
            drag_.reset();
        } else {
            // Échange : conserve l'empreinte de l'ancien objet, qui passe sur le curseur.
            const game::InvCell old = game::g_Client.inv.At(page, static_cast<uint32_t>(occ));
            nc.gridX = old.gridX;
            nc.gridY = old.gridY;
            game::g_Client.inv.At(page, static_cast<uint32_t>(occ)) = nc;
            dragBagCell_  = old;
            drag_.srcType = DragSource::Bag;
            drag_.srcPage = bagPage_;
            drag_.srcSlot = occ;
            drag_.itemId  = old.itemId;
            drag_.count   = static_cast<int>(old.flag ? old.flag : 1);
        }
        NotifyServerItemMove(preMove); // repli propre documenté (cf. UI/InventoryWindow.h)
        return true;
    }

    return false; // cible hors panneau : l'objet reste sur le curseur
}

// Point d'accroche réseau — NO-OP désormais PROUVÉ FIDÈLE pour le réarrangement
// intra-sac (ré-audit W4-F3). Le déplacement d'un objet à l'intérieur du sac
// passe par Item_BeginDragTransaction 0x5AFDF0 (prise LOCALE : vide la case
// source SANS envoyer) puis pose locale ; le serveur NE SUIT PAS la disposition
// de grille du client -> AUCUN paquet n'est émis pour un simple réarrangement.
// (Pkt_TradeResult 0x48D150, opcode 0x26, réécrit g_InvMain UNIQUEMENT en réponse
// à un achat/vente/vault — round-trip serveur —, jamais à un déplacement client.)
// Le seul cas qui DÉCLENCHE un envoi est l'équip/déséquip (drop sac->slot équip),
// mais via le chemin de drop cGameHud (gardé anticheat), non isolable en un
// builder unique ici -> reste en TODO pour ce cas précis. `ctx` documente ce
// qu'un tel envoi devrait transmettre. InventoryWindow POSSÈDE bien un net_ :
// dès que l'opcode d'équip sera prouvé, il sera câblable ici.
void InventoryWindow::NotifyServerItemMove(const DragContext& ctx) const {
    (void)ctx;
    if (!net_) return; // aucune session liée : rien à faire de toute façon.
    // Réarrangement intra-sac = local-only (fidèle, cf. ci-dessus) : rien à envoyer.
    // TODO(send) équip/déséquip uniquement : brancher le builder du chemin de drop
    // cGameHud une fois l'opcode identifié (aucun candidat confirmé à ce jour —
    // cf. UI/InventoryWindow.h). Exemple attendu :
    //   net::Net_SendOpNN(*net_, /* champs de ctx : srcType/srcPage/srcSlot/itemId/count */);
}

// Rend l'objet à sa source (fermeture, échec) — pas de perte d'objet. Restaure au
// slot EXACT (srcPage, srcSlot) d'où l'objet a été retiré par BeginPickup, plutôt
// que de le rajouter en fin de collection comme le faisait l'ancien modèle vecteur.
void InventoryWindow::CancelDrag() {
    if (!drag_.active) return;
    if (drag_.srcType == DragSource::Equip && drag_.srcSlot >= 0 && drag_.srcSlot < 13)
        game::g_World.self.equip[static_cast<size_t>(drag_.srcSlot)] = dragEquipCell_;
    else if (drag_.srcType == DragSource::Bag && drag_.srcSlot >= 0 &&
             drag_.srcSlot < static_cast<int>(game::InventoryState::kCols))
        game::g_Client.inv.At(static_cast<uint32_t>(drag_.srcPage),
                              static_cast<uint32_t>(drag_.srcSlot)) = dragBagCell_;
    drag_.reset();
}

// ============================================================================
// Événements souris
// ============================================================================
bool InventoryWindow::OnMouseDown(int mouseX, int mouseY) {
    cursorX_ = mouseX; cursorY_ = mouseY;
    if (!visible_ || activeTab_ != 1) return false;

    if (drag_.active) {              // reclic -> pose
        PlaceDrag(mouseX, mouseY);
        return true;
    }
    if (BeginPickup(mouseX, mouseY)) // clic -> prise
        return true;

    // Clic dans le panneau sans objet : consommé (premier-consommateur-gagne).
    return PointInPanel(mouseX, mouseY);
}

bool InventoryWindow::OnMouseUp(int mouseX, int mouseY) {
    cursorX_ = mouseX; cursorY_ = mouseY;
    if (!visible_ || activeTab_ != 1) return false;
    // Modèle « clic pour prendre / reclic pour poser » : la pose se fait au clic
    // (OnMouseDown), pas au relâchement. On consomme seulement si sur le panneau.
    return PointInPanel(mouseX, mouseY);
}

} // namespace ts2::ui

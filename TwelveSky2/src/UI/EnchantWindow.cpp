// UI/EnchantWindow.cpp — implémentation de la fenêtre « Enchantement ».
// Voir UI/EnchantWindow.h pour le contrat, l'hypothèse de mapping classe/slot et
// les références de RE (Game/ItemSystem.h, Game/GameState.h).
#include "UI/EnchantWindow.h"
#include "Game/GameDatabase.h"
#include "Asset/ImgFile.h"

#include <cstdio>
#include <cstddef>

namespace ts2::ui {
namespace {

// Résolveur d'icône d'objet — IDENTIQUE à ResolveItemIconPath de UI/InventoryWindow.cpp
// (pattern de référence de la mission, dupliqué faute de header commun sans toucher à
// l'architecture existante). CONFIRMÉ par désassemblage (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md) :
// l'index de fichier N'EST PAS itemId (ancienne hypothèse, FAUSSE) mais le champ SÉPARÉ
// ITEM_INFO+192 ("IconID", game::ItemInfo::iconId, 1-based), lu via game::GetItemInfo().
// (mise à l'échelle par DrawSpriteScaled comme les autres fenêtres, kSlotSize=48 ici.)
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}

// ===========================================================================
// Palette (ARGB, D3DCOLOR = 0xAARRGGBB) — mêmes teintes que les autres fenêtres
// modales du shell (ex. CharacterStatsWindow.cpp), cf. contrat UI.
// ===========================================================================
constexpr D3DCOLOR kColBg        = Argb(0xE0, 0x20, 0x20, 0x28); // fond panneau
constexpr D3DCOLOR kColTitleBg   = Argb(0xF0, 0x18, 0x18, 0x20); // bandeau titre
constexpr D3DCOLOR kColFrame     = Argb(0xFF, 0x80, 0x80, 0x80); // cadre
constexpr D3DCOLOR kColText      = Argb(0xFF, 0xFF, 0xFF, 0xFF); // texte normal
constexpr D3DCOLOR kColTitle     = Argb(0xFF, 0xFF, 0xDD, 0x66); // titre
constexpr D3DCOLOR kColLabel     = Argb(0xFF, 0xC0, 0xC0, 0xC8); // libellés (gris clair)
constexpr D3DCOLOR kColHover     = Argb(0xFF, 0x40, 0x60, 0xA0); // survol
constexpr D3DCOLOR kColSelected  = Argb(0xFF, 0xFF, 0xDD, 0x66); // cadre du slot sélectionné
constexpr D3DCOLOR kColBtn       = Argb(0xFF, 0x38, 0x40, 0x50); // bouton normal
constexpr D3DCOLOR kColBtnDown   = Argb(0xFF, 0x58, 0x84, 0xC8); // bouton enfoncé
constexpr D3DCOLOR kColBtnOff    = Argb(0xFF, 0x30, 0x30, 0x34); // bouton désactivé
constexpr D3DCOLOR kColSlotEmpty = Argb(0xFF, 0x2A, 0x2A, 0x30); // slot vide
constexpr D3DCOLOR kColSuccess   = Argb(0xFF, 0x60, 0xFF, 0x60); // delta positif
constexpr D3DCOLOR kColError     = Argb(0xFF, 0xFF, 0x60, 0x60); // delta négatif / erreur
constexpr D3DCOLOR kColDivider   = Argb(0xFF, 0x50, 0x50, 0x58); // séparateur
constexpr D3DCOLOR kColDim       = Argb(0xFF, 0x70, 0x70, 0x78); // texte estompé (delta nul)

// --- Constantes de géométrie ---
constexpr int kBoxW      = 580;
constexpr int kBoxH      = 440;
constexpr int kTitleH    = 28;
constexpr int kCloseSize = 18;

constexpr int kGridCols  = 5;
constexpr int kSlotSize  = 48;
constexpr int kSlotGap   = 10;
constexpr int kGridOffX  = 24;              // depuis box.x
constexpr int kGridOffY  = kTitleH + 24;    // depuis box.y
constexpr int kGridW     = kGridCols * kSlotSize + (kGridCols - 1) * kSlotGap;

constexpr int kPanelGapX        = 24; // espace entre la grille et le panneau info
constexpr int kPanelRightMargin = 24;
constexpr int kBtnBottomMargin  = 70; // hauteur réservée en bas pour le bouton

constexpr int kEnchantBtnW = 180;
constexpr int kEnchantBtnH = 34;

// Table des 13 identifiants de type d'équipement associés à l'ordre des slots,
// reprise TELLE QUELLE de UI/InventoryWindow.cpp (kEquipDelta / cGameHud_InitLayout
// 0x62A5B0, ordre slotIds[13] = {2,3,4,5,6,7,99,9,10,11,12,13,14}). Sert
// UNIQUEMENT d'étiquette repère (id de type) : aucun nom d'emplacement humain
// n'est confirmé par le désassemblage pour cette fenêtre, donc on n'en invente pas.
constexpr int kEquipSlotIds[kEnchantSlotCount] = {
    2, 3, 4, 5, 6, 7, 99, 9, 10, 11, 12, 13, 14,
};

// ---------------------------------------------------------------------------
// Mapping classe/slot DOCUMENTÉ (cf. commentaire d'en-tête de UI/EnchantWindow.h
// et Game/ItemSystem.h lignes 155-161, commentaire de Item_GetEnchantStatDelta).
// Seule source de vérité pour ce mapping dans ce fichier (le membre privé
// EnchantWindow::GuessItemClass délègue ici).
// ---------------------------------------------------------------------------
int ClassifySlotForEnchant(int slot) {
    if (slot == 1) return 8;                                   // cas spécial (slot 1 uniquement)
    if (slot == 0 || slot == 2 || slot == 3 || slot == 4 || slot == 5) return 4; // armure
    if (slot == 7) return 1;                                    // arme
    return -1;                                                  // non couvert par la table connue
}

const char* ClassLabelFor(int itemClass) {
    switch (itemClass) {
        case 1: return "Arme";
        case 4: return "Armure";
        case 8: return "Spécial";
        default: return "Non pris en charge";
    }
}

D3DCOLOR SlotColorFor(int slot) {
    // Palette fixe de 13 teintes distinctes — REPLI utilisé quand l'icône .IMG réelle de
    // l'objet équipé (GetIconTex/ResolveItemIconPath ci-dessus) n'a pas pu être chargée.
    static constexpr D3DCOLOR kPalette[kEnchantSlotCount] = {
        Argb(0xFF, 0xC0, 0x50, 0x50), Argb(0xFF, 0xC0, 0x80, 0x40),
        Argb(0xFF, 0xC0, 0xB0, 0x40), Argb(0xFF, 0x90, 0xB0, 0x40),
        Argb(0xFF, 0x50, 0xA0, 0x50), Argb(0xFF, 0x40, 0xA0, 0x90),
        Argb(0xFF, 0x40, 0x80, 0xB0), Argb(0xFF, 0x40, 0x60, 0xC0),
        Argb(0xFF, 0x70, 0x50, 0xC0), Argb(0xFF, 0xA0, 0x40, 0xB0),
        Argb(0xFF, 0xC0, 0x40, 0x80), Argb(0xFF, 0x90, 0x70, 0x50),
        Argb(0xFF, 0x60, 0x60, 0x60),
    };
    if (slot < 0 || slot >= kEnchantSlotCount) return kColSlotEmpty;
    return kPalette[slot];
}

const char* SlotLabelFor(int slot) {
    static char buf[kEnchantSlotCount][24];
    if (slot < 0 || slot >= kEnchantSlotCount) return "?";
    std::snprintf(buf[slot], sizeof(buf[slot]), "Slot %d (id %d)", slot, kEquipSlotIds[slot]);
    return buf[slot];
}

// Clés/labels d'affichage du delta d'enchantement (cf. UI/EnchantWindow.h).
const char* StatKeyLabel(EnchantStatKey key) {
    switch (key) {
        case EnchantStatKey::AtkExt:    return "Attaque Externe";
        case EnchantStatKey::AtkInt:    return "Attaque Interne";
        case EnchantStatKey::DefExt:    return "Défense Externe";
        case EnchantStatKey::DefInt:    return "Défense Interne";
        case EnchantStatKey::MaxHp:     return "PV Max";
        case EnchantStatKey::MaxMp:     return "PM Max";
        case EnchantStatKey::Precision: return "Précision";
        case EnchantStatKey::Evasion:   return "Esquive";
        case EnchantStatKey::Rating:    return "Rating";
    }
    return "?";
}

// Les clés 10/20/30/40 sont en CENTIÈMES (converties /100 à l'affichage), les
// autres en UNITÉS — fidèle au commentaire de Item_GetEnchantStatDelta.
bool IsHundredthsKey(EnchantStatKey key) {
    return key == EnchantStatKey::AtkExt || key == EnchantStatKey::AtkInt ||
           key == EnchantStatKey::DefExt || key == EnchantStatKey::DefInt;
}

void FormatDelta(char* buf, size_t n, EnchantStatKey key, int rawDelta) {
    if (IsHundredthsKey(key)) {
        const double v = static_cast<double>(rawDelta) / 100.0;
        std::snprintf(buf, n, "%s%.2f", (v > 0.0 ? "+" : ""), v);
    } else {
        std::snprintf(buf, n, "%s%d", (rawDelta > 0 ? "+" : ""), rawDelta);
    }
}

// Delta d'enchantement pour `slot`, au niveau `previewLevel` (remplace octet3 du
// mot socket). Ne dépend que de l'octet3 (cf. Game/ItemSystem.cpp — seule lecture
// de socketWord dans Item_GetEnchantStatDelta), donc un mot minimal suffit.
int PreviewDeltaFor(int itemClass, int slot, int previewLevel, EnchantStatKey key) {
    if (itemClass < 0) return 0;
    int lvl = previewLevel;
    if (lvl < 1) lvl = 1;
    if (lvl > 59) lvl = 59;
    const uint32_t previewSocket = static_cast<uint32_t>(lvl) << 24;
    return game::Item_GetEnchantStatDelta(itemClass, slot, previewSocket, static_cast<int>(key));
}

// État d'enchantement dérivé du slot sélectionné (item courant + niveau courant/
// prochain + classe résolue). Centralise la logique utilisée par le hit-test des
// boutons ET par le rendu (une seule source de vérité).
struct EnchantState {
    bool     valid     = false; // slot dans [0..12] avec un objet équipé
    uint32_t itemId    = 0;
    int      itemClass = -1;    // -1 = non couvert par la table connue
    int      curLvl    = 0;     // niveau d'enchant courant (octet3 du mot socket)
    int      nextLvl   = 1;     // niveau prévisualisé (curLvl+1, plafonné à 59)
    bool     atMax     = false; // curLvl >= 59 (plus de progression possible)
};

EnchantState ComputeState(int slot) {
    EnchantState st;
    if (slot < 0 || slot >= kEnchantSlotCount) return st;
    const game::EquipSlot& e = game::g_World.self.equip[static_cast<size_t>(slot)];
    if (e.itemId == 0) return st;
    st.valid     = true;
    st.itemId    = e.itemId;
    st.itemClass = ClassifySlotForEnchant(slot);
    st.curLvl    = static_cast<int>(game::Item_GetAttribByte3(e.socket));
    st.nextLvl   = st.curLvl + 1;
    if (st.nextLvl > 59) st.nextLvl = 59;
    st.atMax     = st.curLvl >= 59;
    return st;
}

} // namespace

// ===========================================================================
// Délégations des membres privés statiques vers la logique centralisée ci-dessus
// (garde une unique source de vérité pour le mapping classe/slot et la palette).
// ===========================================================================
int EnchantWindow::GuessItemClass(int slot) { return ClassifySlotForEnchant(slot); }
const char* EnchantWindow::ItemClassLabel(int itemClass) { return ClassLabelFor(itemClass); }
const char* EnchantWindow::SlotLabel(int slot) { return SlotLabelFor(slot); }
D3DCOLOR EnchantWindow::SlotColor(int slot) { return SlotColorFor(slot); }

int EnchantWindow::PreviewDelta(int slot, int previewLevel, EnchantStatKey key) const {
    if (slot < 0 || slot >= kEnchantSlotCount) return 0;
    const int itemClass = ClassifySlotForEnchant(slot);
    return PreviewDeltaFor(itemClass, slot, previewLevel, key);
}

// ===========================================================================
// Icônes (même pattern paresseux+cache que InventoryWindow::GetIconTex)
// ===========================================================================
gfx::GpuTexture* EnchantWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t itemId) {
    // Cache PARTAGÉ par chemin de fichier (cf. SetIconCache/ActiveIconCache) : une icône
    // déjà chargée par InventoryWindow/WarehouseWindow/VendorShopWindow est réutilisée sans
    // re-décoder/re-uploader en VRAM (même fichier .IMG, même ITEM_INFO::iconId).
    const std::string path = ResolveItemIconPath(itemId);
    return ActiveIconCache().GetOrLoad(dev, path);
}

// ===========================================================================
// Layout — centré sur l'écran courant (recalculé chaque frame, comme MsgBoxDialog
// / CharacterStatsWindow).
// ===========================================================================
void EnchantWindow::ComputeLayout(int screenW, int screenH, Layout& L) const {
    L.box.w = kBoxW;
    L.box.h = kBoxH;
    L.box.x = screenW / 2 - kBoxW / 2;
    L.box.y = screenH / 2 - kBoxH / 2;

    L.titleBar = Rect{ L.box.x, L.box.y, L.box.w, kTitleH };

    L.closeBtn = Rect{ L.box.x + L.box.w - kCloseSize - 6, L.box.y + 5,
                        kCloseSize, kCloseSize };

    const int gridX = L.box.x + kGridOffX;
    const int gridY = L.box.y + kGridOffY;
    for (int i = 0; i < kEnchantSlotCount; ++i) {
        const int col = i % kGridCols;
        const int row = i / kGridCols;
        L.slot[i] = Rect{ gridX + col * (kSlotSize + kSlotGap),
                           gridY + row * (kSlotSize + kSlotGap),
                           kSlotSize, kSlotSize };
    }

    const int panelX = gridX + kGridW + kPanelGapX;
    const int panelY = gridY;
    const int panelW = (L.box.x + L.box.w) - kPanelRightMargin - panelX;
    const int panelH = (L.box.y + L.box.h) - kBtnBottomMargin - panelY;
    L.panel = Rect{ panelX, panelY, panelW, panelH };

    L.enchantBtn = Rect{ L.box.x + L.box.w / 2 - kEnchantBtnW / 2,
                          L.box.y + L.box.h - kBtnBottomMargin + 18,
                          kEnchantBtnW, kEnchantBtnH };
}

// ===========================================================================
// Cycle de vie
// ===========================================================================
void EnchantWindow::Open() {
    Dialog::Open();
    closeArmed_   = false;
    enchantArmed_ = false;
    for (bool& b : slotArmed_) b = false;
    selectedSlot_ = -1;
}

// ===========================================================================
// Souris
// ===========================================================================
bool EnchantWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) {
        closeArmed_ = true;
        return true;
    }

    for (int i = 0; i < kEnchantSlotCount; ++i) {
        const Rect& r = L.slot[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h)) {
            slotArmed_[i] = true;
            return true;
        }
    }

    const EnchantState st = ComputeState(selectedSlot_);
    if (st.valid && st.itemClass >= 0 && !st.atMax &&
        PointInRect(x, y, L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h)) {
        enchantArmed_ = true;
        return true;
    }

    // Clic n'importe où ailleurs dans le panneau : consommé (empêche le clic de
    // "traverser" jusqu'au monde 3D derrière la fenêtre) mais n'arme rien.
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

bool EnchantWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    if (closeArmed_) {
        closeArmed_ = false;
        if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) {
            Close();
            return true;
        }
    }

    for (int i = 0; i < kEnchantSlotCount; ++i) {
        if (!slotArmed_[i]) continue;
        slotArmed_[i] = false;
        const Rect& r = L.slot[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h)) {
            selectedSlot_ = i; // sélectionne l'objet à enchanter (même si le slot est vide
                                // ou hors table -> le panneau affichera le statut correspondant)
            return true;
        }
    }

    if (enchantArmed_) {
        enchantArmed_ = false;
        if (PointInRect(x, y, L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h)) {
            const EnchantState st = ComputeState(selectedSlot_);
            if (st.valid && st.itemClass >= 0 && !st.atMax) {
                // TODO(send) : demande d'enchantement au serveur. Aucun builder
                // Net_Send* dédié à l'enchantement n'est identifié dans
                // Net/SendPackets.h à ce jour (pas de "Net_SendEnchant*"). Le
                // candidat le plus probable est le DISPATCHER générique action/
                // inventaire opcode SORTANT 0x13 (Outgoing::Op19, Net/Opcodes.h,
                // "sous-op 0..255, vault 201..250"), symétrique du dispatcher
                // ENTRANT Pkt_ItemActionDispatch (opcode 0x1a, EA 0x46A320,
                // Net/ItemActionDispatch.h) qui applique le résultat serveur
                // (succès/échec/casse) aux cellules d'équipement/sac. Builder
                // exact à appeler : Net_SendPacket_Op19(NetClient&, uint8_t
                // subCmd, const void* payload) — Net/SendPackets.h ligne 216. Le
                // sous-op précis "lancer un enchantement sur le slot N" N'EST PAS
                // isolé dans RE/opcode_table.json ni RE/outbound_results.json :
                // NE PAS deviner sa valeur ; la relever en dynamique (breakpoint
                // sur Net_SendPacket_Op19 pendant un clic "Enchanter" en jeu),
                // puis appeler ici :
                //   Net_SendPacket_Op19(nc, /*subCmd=*/<relevé>, /*payload=*/&req);
                // où `req` encoderait a minima {slot=selectedSlot_, itemId=st.itemId}.

                // Mise à jour LOCALE optimiste (état visible immédiatement, comme demandé
                // par la mission, même sans envoi réseau réel) : on avance le niveau
                // d'enchant affiché (octet3 du mot socket) de l'équipement sélectionné.
                // Le serveur écrasera cette valeur avec le résultat RÉEL (réussite,
                // échec ou casse de l'objet) à réception de sa réponse via
                // Pkt_ItemActionDispatch — cette prévisualisation n'est PAS garantie.
                game::EquipSlot& e = game::g_World.self.equip[static_cast<size_t>(selectedSlot_)];
                e.socket = (e.socket & 0x00FFFFFFu) | (static_cast<uint32_t>(st.nextLvl) << 24);
            }
            return true;
        }
    }

    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

bool EnchantWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// ===========================================================================
// Rendu
// ===========================================================================
void EnchantWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Mémorise les dims écran courantes pour que le hit-test (routé entre deux
    // frames) s'aligne sur la géométrie effectivement dessinée. Fait dans les deux
    // sous-passes (Panels puis Text), comme MsgBoxDialog/CharacterStatsWindow.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Layout L;
    ComputeLayout(ctx.screenW, ctx.screenH, L);

    const auto& equip = game::g_World.self.equip;
    const EnchantState st = ComputeState(selectedSlot_);
    const bool canEnchant = st.valid && st.itemClass >= 0 && !st.atMax;

    char buf[128];

    if (ctx.phase == UiPhase::Panels) {
        // --- Fond + cadre + bandeau de titre ---
        ctx.FillRect(L.box.x, L.box.y, L.box.w, L.box.h, kColBg);
        ctx.FillRect(L.titleBar.x, L.titleBar.y, L.titleBar.w, L.titleBar.h, kColTitleBg);
        ctx.DrawFrame(L.box.x, L.box.y, L.box.w, L.box.h, kColFrame, 2);
        ctx.FillRect(L.box.x, L.box.y + kTitleH, L.box.w, 1, kColDivider);

        // --- Bouton fermeture ---
        const bool closeHover = PointInRect(cursorX, cursorY, L.closeBtn.x, L.closeBtn.y,
                                             L.closeBtn.w, L.closeBtn.h);
        const D3DCOLOR closeCol = closeArmed_ ? kColBtnDown : (closeHover ? kColHover : kColBtn);
        ctx.FillRect(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, closeCol);
        ctx.DrawFrame(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, kColFrame, 1);

        // --- 13 slots d'équipement : icône .IMG réelle si résolue, sinon repli sur le
        // carré coloré générique par slot (SlotColor) — comportement d'origine inchangé.
        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;
        for (int i = 0; i < kEnchantSlotCount; ++i) {
            const Rect& r = L.slot[i];
            const uint32_t itemId = equip[static_cast<size_t>(i)].itemId;
            const bool occupied = itemId != 0;
            const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);

            gfx::GpuTexture* icon = occupied ? GetIconTex(dev, itemId) : nullptr;
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColSlotEmpty); // fond neutre sous l'icône
                const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                               gfx::kSpriteWhite, /*compensatePos=*/true);
            } else {
                ctx.FillRect(r.x, r.y, r.w, r.h, occupied ? SlotColor(i) : kColSlotEmpty);
            }

            D3DCOLOR frameCol = kColFrame;
            int thickness = 1;
            if (selectedSlot_ == i) { frameCol = kColSelected; thickness = 2; }
            else if (hover)         { frameCol = kColHover;    thickness = 2; }
            ctx.DrawFrame(r.x, r.y, r.w, r.h, frameCol, thickness);
        }

        // --- Panneau d'information / prévisualisation (droite) ---
        ctx.FillRect(L.panel.x, L.panel.y, L.panel.w, L.panel.h, kColBg);
        ctx.DrawFrame(L.panel.x, L.panel.y, L.panel.w, L.panel.h, kColFrame, 1);

        // --- Bouton "Enchanter" ---
        const bool enchantHover = PointInRect(cursorX, cursorY, L.enchantBtn.x, L.enchantBtn.y,
                                               L.enchantBtn.w, L.enchantBtn.h);
        D3DCOLOR btnCol = kColBtnOff;
        if (canEnchant)
            btnCol = enchantArmed_ ? kColBtnDown : (enchantHover ? kColHover : kColBtn);
        ctx.FillRect(L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h, btnCol);
        ctx.DrawFrame(L.enchantBtn.x, L.enchantBtn.y, L.enchantBtn.w, L.enchantBtn.h, kColFrame, 1);
        return;
    }

    // --- Phase texte -----------------------------------------------------
    const int titleW = ctx.MeasureText("Enchantement");
    ctx.Text("Enchantement", L.box.x + (L.box.w - titleW) / 2, L.titleBar.y + 6, kColTitle);
    ctx.Text("X", L.closeBtn.x + 5, L.closeBtn.y + 2, kColText);

    // Repère index dans chaque slot (numéro + id de type, cf. kEquipSlotIds).
    for (int i = 0; i < kEnchantSlotCount; ++i) {
        const Rect& r = L.slot[i];
        std::snprintf(buf, sizeof(buf), "%d", i);
        ctx.Text(buf, r.x + 3, r.y + 2, kColText);
        if (equip[static_cast<size_t>(i)].itemId != 0) {
            const int lvl = static_cast<int>(game::Item_GetAttribByte3(
                equip[static_cast<size_t>(i)].socket));
            if (lvl > 0) {
                std::snprintf(buf, sizeof(buf), "+%d", lvl);
                ctx.Text(buf, r.x + 3, r.y + r.h - 14, kColTitle);
            }
        }
    }

    // --- Panneau d'information ---
    int ty = L.panel.y + 10;
    const int tx = L.panel.x + 12;
    const int lineH = 16;

    if (!st.valid) {
        ctx.Text(selectedSlot_ < 0 ? "Sélectionnez un emplacement" : "Emplacement vide",
                  tx, ty, kColLabel);
        ty += lineH;
        ctx.Text("d'équipement à enchanter.", tx, ty, kColLabel);
    } else {
        const game::ItemInfo* info = game::GetItemInfo(st.itemId);
        std::snprintf(buf, sizeof(buf), "%s", info ? info->name : SlotLabel(selectedSlot_));
        ctx.Text(buf, tx, ty, kColTitle);
        ty += lineH + 4;

        std::snprintf(buf, sizeof(buf), "Classe : %s", ItemClassLabel(st.itemClass));
        ctx.Text(buf, tx, ty, kColLabel);
        ty += lineH;

        std::snprintf(buf, sizeof(buf), "Niveau d'enchant : +%d", st.curLvl);
        ctx.Text(buf, tx, ty, kColText);
        ty += lineH + 6;

        ctx.FillRect(L.panel.x + 8, ty, L.panel.w - 16, 1, kColDivider);
        ty += 10;

        if (st.itemClass < 0) {
            ctx.Text("Non pris en charge par la table", tx, ty, kColError);
            ty += lineH;
            ctx.Text("d'enchantement (mapping classe/slot", tx, ty, kColError);
            ty += lineH;
            ctx.Text("non confirmé pour ce slot).", tx, ty, kColError);
        } else if (st.atMax) {
            ctx.Text("Niveau maximum atteint (+59).", tx, ty, kColLabel);
        } else {
            std::snprintf(buf, sizeof(buf), "Aperçu au niveau +%d :", st.nextLvl);
            ctx.Text(buf, tx, ty, kColLabel);
            ty += lineH + 2;

            bool anyNonZero = false;
            for (int k = 0; k < kEnchantStatKeyCount; ++k) {
                const EnchantStatKey key = kEnchantStatKeys[k];
                const int delta = PreviewDeltaFor(st.itemClass, selectedSlot_, st.nextLvl, key);
                if (delta == 0) continue;
                anyNonZero = true;

                std::snprintf(buf, sizeof(buf), "%s :", StatKeyLabel(key));
                ctx.Text(buf, tx, ty, kColLabel);

                char dbuf[32];
                FormatDelta(dbuf, sizeof(dbuf), key, delta);
                const int dw = ctx.MeasureText(dbuf);
                ctx.Text(dbuf, L.panel.x + L.panel.w - 12 - dw, ty,
                          delta > 0 ? kColSuccess : kColError);
                ty += lineH;
            }
            if (!anyNonZero) {
                ctx.Text("(aucun bonus à ce palier précis)", tx, ty, kColDim);
                ty += lineH;
            }
        }
    }

    // Libellé du bouton "Enchanter" (estompé si désactivé).
    const int btnTextW = ctx.MeasureText("Enchanter");
    ctx.Text("Enchanter",
              L.enchantBtn.x + (L.enchantBtn.w - btnTextW) / 2, L.enchantBtn.y + 9,
              canEnchant ? kColText : kColDim);
}

} // namespace ts2::ui

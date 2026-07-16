// UI/ConsumableBarWindow.cpp — voir UI/ConsumableBarWindow.h pour le contrat.
#include "UI/ConsumableBarWindow.h"
#include "Core/Log.h"
#include "Core/Types.h" // ts2::kRefWidth (seuil de branche centrée/fixe, cf. ComputeLayout)
#include "Game/GameDatabase.h" // game::GetItemInfo / ItemInfo::iconId (icônes réelles, cf. GetIconTex)
#include "Gfx/Renderer.h"      // ctx.renderer->Device() (cf. UI/VendorShopWindow.cpp::GetIconTex)
#include "Gfx/SpriteBatch.h"   // ctx.sprites->DrawSpriteScaled (icône réelle en phase Panels)

#include <cstdio>

namespace ts2::ui {

namespace {
// --- Palette (ARGB), cohérente avec la charte du HUD (voir UI/GameHud.cpp). ---
constexpr D3DCOLOR kBarBg        = 0xE0202028u; // fond du bandeau (panneau)
constexpr D3DCOLOR kBarBorder    = 0xFF808080u; // cadre du bandeau
constexpr D3DCOLOR kCellEmptyBrd = 0xFF505058u; // cadre d'une case vide
constexpr D3DCOLOR kCellFilledBg = 0xFF2E3A2Eu; // fond d'une case assignée (objet)
constexpr D3DCOLOR kCellFilledBrd= 0xFF808080u; // cadre d'une case assignée
constexpr D3DCOLOR kCellHover    = 0xFF4060A0u; // survol/sélection
constexpr D3DCOLOR kCellUnusable = 0xFF5A2020u; // teinte « objet manquant » (fond assombri rouge)
constexpr D3DCOLOR kTextWhite    = 0xFFFFFFFFu; // texte normal
constexpr D3DCOLOR kTextDim      = 0xFFB0B0B0u; // numéro de touche (discret)
constexpr D3DCOLOR kTextError    = 0xFFFF6060u; // erreur
constexpr D3DCOLOR kTextSuccess  = 0xFF60FF60u; // succès
constexpr D3DCOLOR kTextInfo     = 0xFFFFDD66u; // info/tooltip

// ⚠️ CORRECTION DE COORDONNÉES (audit "fenêtres mal ajustées", 2026-07-14, RE-VÉRIFIÉ
// par décompilation FRAÎCHE via idaTs2 direct HTTP JSON-RPC, http://127.0.0.1:13337/mcp,
// tools/call "disasm" — l'ancien layout ci-dessous était un centrage INVENTÉ (kSlotSize=40,
// kSlotGap=4, kBarMarginBottom=14, tous SANS AUCUN rapport avec le binaire) qui ignorait
// complètement l'ancre RÉELLE déjà extraite (et déjà correcte) dans GameHud::InitLayout.
// Preuve par désassemblage de UI_GameHud_Render 0x67A3C0, bloc quickbar 0x684CA8-0x685177 :
//   0x684C9E : cmp ds:nWidth, 400h ; 0x684CA8 : jg short loc_684CC5
//     -> si nWidth > 1024 (ts2::kRefWidth) : branche centrée (this[0]=nWidth/2-largeur/2,
//        this[1]=nHeight-hauteur, calculée via Sprite2D_GetWidth/Height @0x684CD1-0x684D08) ;
//     -> SINON (nWidth <= 1024, cas du lancement standard "/0/0/2/1024/768", CLAUDE.md) :
//        ANCRE LITTÉRALE this[0]=0x187=391, this[1]=0x2D8=728 @0x684CB0/0x684CBC.
//   Icône de slot i (boucle 0x684DC9+, i=0..13 dans le binaire — 14 slots réels, cf. TODO
//   kQuickSlotCount=10 existant plus bas) : PAS dessinée à (this[0]+i*pitch, this[1]) comme
//   l'ancien code le faisait, mais à (this[0] + i*0x1E + 0x18, this[1] + 7) — vérifié
//   IDENTIQUE sur les 3 branches de contenu (compétence @0x684F11-0x684F3D et
//   @0x685010-0x685042, objet @0x685099-0x6850CB) : pitch = 0x1E = 30, offset FIXE
//   (+24,+7) jamais appliqué par l'ancien ComputeLayout. Compteur d'empilement (texte) :
//   (this[0]+i*30+0x26, this[1]+0x15) = (+38,+21) @0x685121-0x685150.
// Conclusion : l'ancien rendu plaçait CHAQUE case ~24px trop à gauche et ~7px trop haut
// par rapport au binaire, ET recentrait toute la barre à un endroit totalement différent
// de l'ancre réelle (391,728) — exactement le symptôme "coordonnées pétées / bar décalée"
// rapporté. kSlotSize=26 (pas une valeur lisible statiquement — le sprite d'icône est
// chargé dynamiquement, cf. limite déjà documentée dans GameHud::InitLayout — mais dérivée
// du pitch réel 30 = 26+kSlotGap, cohérente avec l'estimation déjà retenue côté GameHud).
constexpr int kSlotSize = 26;
constexpr int kSlotGap  = 4;   // pitch réel this.x + i*30 + 24 ; 26+4 = 30 = 0x1E confirmé
constexpr int kIconOffsetX = 24; // +0x18, vérifié 0x684F29/0x684FB3/0x6850B7 (3 branches)
constexpr int kIconOffsetY = 7;  // +7,    vérifié 0x684F14/0x684F9A/0x6850A2 (3 branches)
constexpr int kBarPad = 4;           // marge du fond de bandeau autour des cases (esthétique,
                                      // le fond réel a une largeur/hauteur de sprite inconnue
                                      // statiquement — même limite que GameHud::InitLayout)

constexpr float kMessageDurationSec = 2.5f;
} // namespace

// =============================================================================
// Layout — RÉEL : ancre fixe (391,728) à la résolution de lancement 1024x768 (branche
// `nWidth<=1024` du binaire), sinon centrée sur nWidth/2 (branche `nWidth>1024`) — voir
// le bandeau de constantes ci-dessus pour les EA exactes. Recalculé chaque frame (ce
// widget ne connaît pas les resize events), comme GameHud::InitLayout.
// =============================================================================
ConsumableBarWindow::Layout ConsumableBarWindow::ComputeLayout(int screenW, int screenH) {
    Layout lo;
    const int totalIconsW = kQuickSlotCount * kSlotSize + (kQuickSlotCount - 1) * kSlotGap;

    int anchorX, anchorY;
    if (screenW > ts2::kRefWidth) {
        // Branche centrée @0x684CC5-0x684D08 : nWidth/2 - largeur(fond)/2 ; nHeight -
        // hauteur(fond). Largeur/hauteur du sprite de fond non lisibles statiquement ->
        // approximées par l'étendue des cases + offset icône (même limite assumée que
        // GameHud::InitLayout, qui utilise la même formule).
        anchorX = screenW / 2 - (totalIconsW + kIconOffsetX) / 2;
        anchorY = screenH - (kSlotSize + kIconOffsetY + kBarPad);
    } else {
        // Branche fixe @0x684CB0/0x684CBC — valeurs LITTÉRALES du binaire (résolution de
        // lancement standard "/0/0/2/1024/768", cf. CLAUDE.md).
        anchorX = 391;
        anchorY = 728;
    }

    // Icône du slot 0 (et donc coin haut-gauche de la case) = ancre + offset RÉEL (+24,+7),
    // PAS l'ancre elle-même (celle-ci est le coin du fond de panneau, pas de la 1ère case).
    const int iconX0 = anchorX + kIconOffsetX;
    const int iconY0 = anchorY + kIconOffsetY;

    lo.bar = SlotRect{ anchorX - kBarPad, anchorY - kBarPad,
                        kIconOffsetX + totalIconsW + 2 * kBarPad, kSlotSize + kIconOffsetY + 2 * kBarPad };
    for (int i = 0; i < kQuickSlotCount; ++i) {
        lo.cells[static_cast<size_t>(i)] =
            SlotRect{ iconX0 + i * (kSlotSize + kSlotGap), iconY0, kSlotSize, kSlotSize };
    }
    return lo;
}

// =============================================================================
// Icône réelle — même méthode que InventoryWindow::ResolveItemIconPath /
// VendorShopWindow::GetIconTex (audit "GpuTexture pattern" 2026-07-14, cf.
// bandeau de tête de UI/GameHud.h §quickBarWindow_) : l'index de fichier N'EST
// PAS l'itemId mais le champ SÉPARÉ ITEM_INFO+192 ("IconID", ItemInfo::iconId,
// 1-based), chemin "002\002_%05u.IMG" — CONFIRMÉ par désassemblage
// (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md). ctx.renderer->Device() + ctx.sprites
// (déjà en Begin() par GameHud::Render, cf. UiContext) remplacent le couple
// device_/sprite_ propre qu'utilise InventoryWindow (ce widget n'en possède
// pas, il consomme ceux de GameHud via UiContext).
// =============================================================================
gfx::GpuTexture* ConsumableBarWindow::GetIconTex(const UiContext& ctx, uint32_t itemId) {
    if (!ctx.renderer || !ctx.renderer->Device()) return nullptr;
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return nullptr;
    char path[64];
    std::snprintf(path, sizeof(path), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return ActiveIconCache().GetOrLoad(ctx.renderer->Device(), path);
}

int ConsumableBarWindow::HitTest(int mx, int my) const {
    for (int i = 0; i < kQuickSlotCount; ++i) {
        if (lastLayout_.cells[static_cast<size_t>(i)].Contains(mx, my)) return i;
    }
    return -1;
}

// =============================================================================
// Décision -> message affiché (log visuel). Ne matérialise AUCUN effet de jeu
// réel : TriggerSlot() ne fait que DÉCIDER (cf. Game/ConsumableBarLogic.h).
// =============================================================================
void ConsumableBarWindow::ApplyDecision(const game::ConsumableDecision& d, int index,
                                         const std::array<QuickSlot, kQuickSlotCount>& slots) {
    lastDecision_ = d;
    char buf[128];

    switch (d.action) {
        case game::ConsumableAction::None:
            // Case vide ou hors bornes : rien à afficher (comportement muet,
            // fidèle aux EA d'origine qui ne réagissent pas sur case vide).
            lastMessage_.clear();
            messagePending_ = false;
            return;

        case game::ConsumableAction::Invalid:
            std::snprintf(buf, sizeof(buf), "Case %d : objet inconnu (id %u, hors ITEM_INFO)",
                          index + 1, static_cast<unsigned>(d.refId));
            lastMessage_ = buf;
            lastMessageColor_ = kTextError;
            break;

        case game::ConsumableAction::Unsupported:
            std::snprintf(buf, sizeof(buf), "Case %d : compétences non gérées par cette barre (TODO SkillSystem)",
                          index + 1);
            lastMessage_ = buf;
            lastMessageColor_ = kTextInfo;
            break;

        case game::ConsumableAction::ShowTooltip:
            std::snprintf(buf, sizeof(buf), "Tooltip objet %u (case %d)",
                          static_cast<unsigned>(d.refId), index + 1);
            lastMessage_ = buf;
            lastMessageColor_ = kTextInfo;
            break;

        case game::ConsumableAction::BeginItemDrag: {
            const uint32_t owned = game::InventoryCount(d.refId);
            if (owned == 0) {
                std::snprintf(buf, sizeof(buf), "Case %d : objet %u manquant (0 en inventaire)",
                              index + 1, static_cast<unsigned>(d.refId));
                lastMessage_ = buf;
                lastMessageColor_ = kTextError;
            } else {
                std::snprintf(buf, sizeof(buf), "Utilisation objet %u (case %d, %u en stock)",
                              static_cast<unsigned>(d.refId), index + 1, owned);
                lastMessage_ = buf;
                lastMessageColor_ = kTextSuccess;
                // MÉCANISME PROUVÉ (ré-audit W4-F3, décompilation UI_ConsumableBar_OnClick
                // 0x68E4B4) : un clic sur un slot de la barre N'ENVOIE PAS de paquet
                // « usage d'objet » isolé. Pour un objet dont ITEM_INFO+188 == 2
                // (consommable), le binaire appelle
                //   Item_BeginDragTransaction(g_DragCtx, /*type=*/21, 0, 0,
                //                             itemId=*(this+slot+4), amount, ...)  // 0x5AFDF0
                // c.-à-d. une PRISE de drag LOCALE (conteneur type 21 = barre rapide/
                // consommable), pas un send. `byte_8013FE<0` -> drag avec offset curseur
                // (a4-52,a5-72) et quantité 0 ; sinon quantité 99. L'usage réel
                // (consommation) survient au DROP de cet objet sur soi, validé côté
                // cGameHud (chemin gardé anticheat), non isolable en un builder unique.
                // -> Aucun Net_SendOpNN direct à câbler ici ; contrepartie ENTRANTE du
                // résultat = Pkt_ItemActionDispatch (opcode 0x1a). Le message visuel
                // ci-dessus reste correct (décision locale, cf. Game/ConsumableBarLogic.h).
            }
            (void)slots;
            break;
        }

        case game::ConsumableAction::Ignored:
        case game::ConsumableAction::ArmCloseButton:
        case game::ConsumableAction::ClosePanel:
            // Ce widget n'a pas de bouton fermer (barre non modale) : ces
            // branches de game::OnClick/OnMouseUp ne sont pas exercées ici
            // (on appelle TriggerSlot() directement, pas OnClick()).
            lastMessage_.clear();
            messagePending_ = false;
            return;
    }

    messagePending_ = true;
    TS2_LOG("ConsumableBar : %s", lastMessage_.c_str());
}

// =============================================================================
// Événements souris/clavier
// =============================================================================
bool ConsumableBarWindow::OnMouseDown(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    (void)slots;
    // Consomme le clic s'il tombe sur la barre (bloque le passage à la scène
    // 3D derrière le HUD, comme GameHud::OnMouseDown) ; l'action réelle se
    // décide au relâchement (OnClick), pattern latch de Widgets.h::Button.
    return lastLayout_.bar.Contains(x, y);
}

bool ConsumableBarWindow::OnClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const int index = HitTest(x, y);
    if (index < 0) {
        // Toujours consommé si on reste dans le fond de la barre (zone morte
        // entre les cases), sinon laisse passer l'événement.
        return lastLayout_.bar.Contains(x, y);
    }

    const game::ConsumableDecision d = game::TriggerSlot(slots, index, /*rightClick=*/false);
    ApplyDecision(d, index, slots);
    return true; // case sous le curseur (pleine ou vide) -> toujours consommé
}

bool ConsumableBarWindow::OnRightClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const int index = HitTest(x, y);
    if (index < 0) return lastLayout_.bar.Contains(x, y);

    const game::ConsumableDecision d = game::TriggerSlot(slots, index, /*rightClick=*/true);
    ApplyDecision(d, index, slots);
    return true;
}

// VÉRIFICATION ÉMISSION (W6) : ce widget N'ÉMET AUCUN paquet, et c'est FIDÈLE.
//  - Le clic sur une case (UI_ConsumableBar_OnClick 0x68E3C0 / ProcInput 0x68E5A0 /
//    OnRightClick 0x68E940) ne fait qu'une PRISE de drag LOCALE (Item_BeginDragTransaction,
//    conteneur type 21) — aucun Net_Send* dans ses callees (cf. ApplyDecision, BeginItemDrag).
//  - Le raccourci clavier de la barre-consommables DIALOG (UI_ConsumableBar_OnKeyInput
//    0x68E6C0) est un NO-OP dans le binaire : cet OnHotkey (décision locale via
//    TriggerSlotByHotkey, sans envoi) reste donc fidèle SUR L'AXE ÉMISSION.
//  - Le vrai « utiliser un quickslot » émet via le builder Net_SendPacket_Op22
//    (Net/SendPackets.h:77 ; opcode enum Net_SendUseQuickslotItem = 0x16, Net/Opcodes.h:209,
//    CONFIRMED) depuis Game_OnHotkey 0x537330 / Game_AutoUsePotion — PAS depuis ce widget UI,
//    et HORS de ce front. Ce builder EXISTE mais n'a AUCUN appelant réel dans ClientSource :
//    builder MORT à câbler côté Game/ (rapporté, hors périmètre de ce front UI).
bool ConsumableBarWindow::OnHotkey(uint8_t dikScanCode, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const game::ConsumableDecision d = game::TriggerSlotByHotkey(slots, dikScanCode);
    if (d.slotIndex < 0) return false; // scancode hors plage 0x02..0x0B
    ApplyDecision(d, d.slotIndex, slots);
    return true;
}

// =============================================================================
// Render — deux passes (Panels puis Text), voir UIManager.h.
// =============================================================================
void ConsumableBarWindow::Render(const UiContext& ctx, const std::array<QuickSlot, kQuickSlotCount>& slots,
                                  int cursorX, int cursorY) {
    // Recalcule la géométrie pour CETTE frame et la mémorise pour le hit-test
    // (OnMouseDown/OnClick sont routés entre deux frames, comme MsgBoxDialog).
    lastLayout_   = ComputeLayout(ctx.screenW, ctx.screenH);
    lastScreenW_  = ctx.screenW;
    lastScreenH_  = ctx.screenH;

    if (messagePending_) {
        messageUntilSec_ = ctx.gameTimeSec + kMessageDurationSec;
        messagePending_  = false;
    }

    const int hoverIndex = (cursorX >= 0 && cursorY >= 0) ? HitTest(cursorX, cursorY) : -1;

    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(lastLayout_.bar.x, lastLayout_.bar.y, lastLayout_.bar.w, lastLayout_.bar.h, kBarBg);
        ctx.DrawFrame(lastLayout_.bar.x, lastLayout_.bar.y, lastLayout_.bar.w, lastLayout_.bar.h, kBarBorder);

        for (int i = 0; i < kQuickSlotCount; ++i) {
            const SlotRect& c = lastLayout_.cells[static_cast<size_t>(i)];
            const QuickSlot& s = slots[static_cast<size_t>(i)];

            if (s.empty()) {
                // Case vide = cadre seul (mission : « case vide = cadre seul »).
                ctx.DrawFrame(c.x, c.y, c.w, c.h, kCellEmptyBrd);
            } else {
                const bool usable = game::IsSlotUsable(slots, i);
                ctx.FillRect(c.x, c.y, c.w, c.h, usable ? kCellFilledBg : kCellUnusable);

                // Icône réelle de l'objet (cf. GetIconTex ci-dessus), par-dessus le
                // fond teinté (teinte conservée comme indicateur usable/manquant même
                // quand l'icône se charge). Repli SANS icône = fond teinté seul
                // (même politique que VendorShopWindow::Render quand la résolution
                // échoue), pas de placeholder inventé.
                if (s.type == QuickSlotType::Item) {
                    gfx::GpuTexture* icon = GetIconTex(ctx, s.refId);
                    if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 &&
                        ctx.sprites && ctx.sprites->Ready()) {
                        const float sx = static_cast<float>(c.w) / static_cast<float>(icon->Width());
                        const float sy = static_cast<float>(c.h) / static_cast<float>(icon->Height());
                        ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, c.x, c.y, sx, sy,
                                                      gfx::kSpriteWhite, /*compensatePos=*/true);
                    }
                }

                ctx.DrawFrame(c.x, c.y, c.w, c.h, kCellFilledBrd);
            }

            if (i == hoverIndex) {
                ctx.DrawFrame(c.x - 1, c.y - 1, c.w + 2, c.h + 2, kCellHover, 2);
            }
        }
    }

    if (ctx.phase == UiPhase::Text) {
        for (int i = 0; i < kQuickSlotCount; ++i) {
            const SlotRect& c = lastLayout_.cells[static_cast<size_t>(i)];
            const QuickSlot& s = slots[static_cast<size_t>(i)];

            // Numéro de touche (1..9 puis 0), coin haut-gauche.
            char keyBuf[4];
            const int key = (i + 1) % 10; // slot 10 -> touche '0'
            std::snprintf(keyBuf, sizeof(keyBuf), "%d", key);
            ctx.Text(keyBuf, c.x + 2, c.y + 1, kTextDim);

            if (!s.empty() && s.type == QuickSlotType::Item) {
                // Badge quantité possédée (extension au-delà du strict « numéro »
                // demandé : indispensable pour qu'une barre « déjà fonctionnelle »
                // reflète l'inventaire réel, cf. game::InventoryCount).
                const uint32_t owned = game::InventoryCount(s.refId);
                char qtyBuf[16];
                std::snprintf(qtyBuf, sizeof(qtyBuf), "%u", owned);
                const int tw = ctx.MeasureText(qtyBuf);
                ctx.Text(qtyBuf, c.x + c.w - tw - 2, c.y + c.h - 12,
                         owned > 0 ? kTextWhite : kTextError);
            }
        }

        // Message de retour (résultat du dernier TriggerSlot), centré au-dessus
        // de la barre, tant qu'il n'a pas expiré.
        if (!lastMessage_.empty() && ctx.gameTimeSec <= messageUntilSec_) {
            const int tw = ctx.MeasureText(lastMessage_.c_str());
            const int tx = lastLayout_.bar.x + (lastLayout_.bar.w - tw) / 2;
            const int ty = lastLayout_.bar.y - 18;
            ctx.Text(lastMessage_.c_str(), tx, ty, lastMessageColor_);
        }
    }
}

} // namespace ts2::ui

// UI/ConsumableBarWindow.cpp — voir UI/ConsumableBarWindow.h pour le contrat et,
// SURTOUT, le bandeau « SOURCE DE VÉRITÉ DES CASES » (la barre lit g_Container5,
// pas le paramètre `slots`).
#include "UI/ConsumableBarWindow.h"
#include "Core/Log.h"
#include "Core/Types.h"        // ts2::kRefWidth (seuil de branche centrée/fixe, cf. ComputeLayout)
#include "Config/GameOptions.h" // config::g_Options.MorphUiMode (g_MorphUiMode 0x84DEF4)
#include "Game/ClientRuntime.h" // game::g_Client.Var/VarGet (g_Container5, page, slot sélectionné)
#include "Game/GameDatabase.h"  // game::GetItemInfo / game::GetSkillInfo (mITEM / mSKILL)
#include "Gfx/Renderer.h"       // ctx.renderer->Device()
#include "Gfx/SpriteBatch.h"    // ctx.sprites->DrawSprite (blit taille naturelle, cf. Sprite2D_Draw)

#include <cstdio>

namespace ts2::ui {

namespace {

// =============================================================================
// Ancrage écran — UI_GameHud_Render 0x67A3C0, bloc quickbar 0x684CA8-0x685177.
// =============================================================================
//   0x684C9E : cmp ds:nWidth, 400h ; 0x684CA8 : jg short loc_684CC5
//     -> si nWidth > 1024 (ts2::kRefWidth) : branche centrée (this[0]=nWidth/2-largeur/2,
//        this[1]=nHeight-hauteur, via Sprite2D_GetWidth/Height @0x684CD1-0x684D08) ;
//     -> SINON (nWidth <= 1024, cas du lancement standard "/0/0/2/1024/768", CLAUDE.md) :
//        ANCRE LITTÉRALE this[0]=0x187=391, this[1]=0x2D8=728 @0x684CB0/0x684CBC.
// Icône du slot i : (this[0] + i*0x1E + 0x18, this[1] + 7) — IDENTIQUE sur les 3
// branches de contenu (compétence @0x684F11-0x684F3D, tab-icon @0x685010-0x685042,
// objet @0x685099-0x6850CB) : pitch 0x1E = 30, offset FIXE (+24,+7).
// Compteur de pile (texte) : (this[0]+i*30+0x26, this[1]+0x15) = (+38,+21) @0x685121-0x685150.
// Numéro de page (texte)  : (this[0]+9, this[1]+0Fh) = (+9,+15) @0x684DA2-0x684DC9.
constexpr int kSlotPitch   = 30; // 0x1E
constexpr int kIconOffsetX = 24; // +0x18, vérifié 0x684F29/0x684FB3/0x68502E/0x6850B7
constexpr int kIconOffsetY = 7;  // +7,    vérifié 0x684F14/0x684F9A/0x685019/0x6850A2
constexpr int kCountOffsetX= 38; // +0x26, @0x68513F
constexpr int kCountOffsetY= 21; // +0x15, @0x68512A
constexpr int kPageOffsetX = 9;  // +9,    @0x684DB9
constexpr int kPageOffsetY = 15; // +0Fh,  @0x684DAD
// kSlotSize n'est PAS lisible statiquement (le sprite d'icône est chargé
// dynamiquement) — dérivé du pitch réel 30, comme GameHud::InitLayout. Sert
// UNIQUEMENT au hit-test/au rect de blocage : le rendu blitte à taille naturelle.
constexpr int kSlotSize = 26;
constexpr int kBarPad   = 4;

// =============================================================================
// Adresses g_Container5 et curseurs — cf. bandeau de UI/ConsumableBarWindow.h.
// =============================================================================
constexpr uint32_t kC5ItemId   = 0x16743FCu; // g_Container5_ItemId  @0x684E55/0x684ECF/0x684FE7/0x685061
constexpr uint32_t kC5Count    = 0x1674400u; // dword_1674400        @0x684EB2/0x684F70/0x6850E4/0x685103
constexpr uint32_t kC5Type     = 0x1674404u; // dword_1674404        @0x684E0B
constexpr uint32_t kVarPage    = 0x1675B1Cu; // dword_1675B1C        @0x684D85/0x684DF6
constexpr uint32_t kVarSelSlot = 0x1675B20u; // dword_1675B20        @0x684E81
constexpr int      kPageStride = 0xA8;       // 168 = 14 slots * 3 dwords * 4 o @0x684DFC
constexpr int      kSlotStride = 0xC;        // 3 dwords              @0x684E08

// =============================================================================
// Numéros de fichier .IMG des sprites nommés du bloc quickbar.
// =============================================================================
// Méthode (identique à UI/BuffStatusPanel.h, étendue aux 3 catégories) : la table
// Sprite2D globale est construite par AssetMgr_InitAllSlots 0x4DEB50 sur `this` =
// 0x8E8B30 ; chaque catégorie est un sous-tableau contigu de pas 148 :
//   cat.1 @0x4DEB97 : `Sprite2D_BuildPath(this + 148*i + 32, 1, i, 0)`, 4500 entrées
//         -> base 0x8E8B30 + 32 = 0x8E8B50 (== g_AssetMgr_UiAtlasSlots)
//   cat.2 @0x4DEBD4 : `... (this + 148*j + 666032, 2, j, 0)`, 4000 entrées
//         -> base 0x8E8B30 + 666032 = 0x98B4E0 (== g_AssetMgr_ItemIconSlots)
//   cat.3 @0x4DEC11 : `... (this + 148*k + 1258032, 3, k, 0)`, 760 entrées
//         -> base 0x8E8B30 + 1258032 = 0xA1BD60 (== unk_A1BD60, atlas COMPÉTENCES)
// Pour une adresse A d'une catégorie : slot = (A - base)/148 (division EXACTE) et
// fichier = slot + 1 (paramètre `a3 + 1` de Sprite2D_BuildPath 0x4D68E0).
//   unk_8EA020 : (0x8EA020 - 0x8E8B50)/148 = 5328/148 = 36  (exact) -> cat.1 fichier 37
//   unk_940388 : (0x940388 - 0x8E8B50)/148 = 358456/148 = 2422 (exact) -> cat.1 fichier 2423
constexpr int kBgMorphFile  = 37;   // unk_8EA020 @0x684D5C — fond si g_MorphUiMode == 1
constexpr int kBgNormalFile = 2423; // unk_940388 @0x684D7B — fond normal (= référence de taille)

// Overlay de recharge : `Sprite2D_Draw(g_AssetMgr_UiAtlasSlots + 148*(v + 0x268), ...)`
// @0x684FB4-0x684FC9 -> slot 616 + v -> fichier 617 + v, v ∈ [0,10] (11 paliers).
constexpr int kCooldownFile0 = 617; // 0x268 = 616, +1 (convention a3+1)
// dbl_7A9C48 = 0x4026000000000000 = 11.0 exactement (get_global_value) @0x684F77.
constexpr double kCooldownSteps = 11.0;

// Couleur du texte : UI_DrawNumberValue(unk_1685740, str, x, y, 1) — le dernier
// paramètre (1) est un sélecteur de police/couleur de l'objet unk_1685740, non
// modélisé côté C++ (ctx.Text n'expose qu'une D3DCOLOR). Blanc = neutre.
constexpr D3DCOLOR kTextWhite = 0xFFFFFFFFu;

} // namespace

// =============================================================================
// Lecture d'une case de g_Container5 — ancre 0x684DF6-0x684E12.
// =============================================================================
ConsumableBarWindow::LiveSlot ConsumableBarWindow::ReadContainer5(int page, int i) {
    const uint32_t off = static_cast<uint32_t>(kPageStride * page + kSlotStride * i);
    LiveSlot s;
    s.refId = game::g_Client.VarGet(kC5ItemId + off);
    s.count = game::g_Client.VarGet(kC5Count  + off);
    s.type  = game::g_Client.VarGet(kC5Type   + off);
    return s;
}

// =============================================================================
// Layout — 14 cases (0x684DE9 `cmp var_438, 0Eh`), ancre cf. bandeau ci-dessus.
// =============================================================================
ConsumableBarWindow::Layout ConsumableBarWindow::ComputeLayout(int screenW, int screenH) {
    Layout lo;
    const int totalIconsW = (kBarSlotCount - 1) * kSlotPitch + kSlotSize;

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

    const int iconX0 = anchorX + kIconOffsetX;
    const int iconY0 = anchorY + kIconOffsetY;

    lo.bar = SlotRect{ anchorX - kBarPad, anchorY - kBarPad,
                        kIconOffsetX + totalIconsW + 2 * kBarPad, kSlotSize + kIconOffsetY + 2 * kBarPad };
    for (int i = 0; i < kBarSlotCount; ++i) {
        lo.cells[static_cast<size_t>(i)] =
            SlotRect{ iconX0 + i * kSlotPitch, iconY0, kSlotSize, kSlotSize };
    }
    return lo;
}

// =============================================================================
// Textures — Sprite2D_BuildPath 0x4D68E0 (cf. déclaration dans le .h).
// =============================================================================
gfx::GpuTexture* ConsumableBarWindow::GetCatTex(const UiContext& ctx, int category, int fileNo) {
    if (!ctx.renderer || !ctx.renderer->Device()) return nullptr;
    if (fileNo <= 0) return nullptr;

    const char* dir = nullptr;
    switch (category) {
        case 1: dir = "001"; break; // atlas UI générique   @0x4D6945
        case 2: dir = "002"; break; // icônes d'objets      @0x4D6965
        case 3: dir = "003"; break; // icônes de compétences @0x4D6985
        default: return nullptr;
    }
    char path[64];
    std::snprintf(path, sizeof(path), "G03_GDATA\\D01_GIMAGE2D\\%s\\%s_%05d.IMG", dir, dir, fileNo);
    return ActiveIconCache().GetOrLoad(ctx.renderer->Device(), path);
}

gfx::GpuTexture* ConsumableBarWindow::GetIconTex(const UiContext& ctx, uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return nullptr;
    // Binaire : slot = ITEM_INFO+192 - 1 (@0x68508D `mov edx,[ecx+0C0h]; sub edx,1`),
    // donc fichier = slot + 1 = iconId.
    return GetCatTex(ctx, 2, static_cast<int>(info->iconId));
}

namespace {
// Blit à TAILLE NATURELLE : Sprite2D_Draw 0x4D6B20 ne prend que (x, y) — le binaire
// ne met JAMAIS une icône de quickbar à l'échelle.
void BlitNatural(const UiContext& ctx, gfx::GpuTexture* tex, int x, int y) {
    if (!tex || !tex->Handle() || tex->Width() == 0 || tex->Height() == 0) return;
    if (!ctx.sprites || !ctx.sprites->Ready()) return;
    ctx.sprites->DrawSprite(tex->Handle(), nullptr, x, y, gfx::kSpriteWhite);
}
} // namespace

int ConsumableBarWindow::HitTest(int mx, int my) const {
    for (int i = 0; i < kBarSlotCount; ++i) {
        if (lastLayout_.cells[static_cast<size_t>(i)].Contains(mx, my)) return i;
    }
    return -1;
}

// =============================================================================
// Décision -> message (log/appelant uniquement, PLUS dessiné : le binaire
// n'affiche aucun message au-dessus de la barre — cf. bandeau du .h).
// =============================================================================
void ConsumableBarWindow::ApplyDecision(const game::ConsumableDecision& d, int index,
                                         const std::array<QuickSlot, kQuickSlotCount>& slots) {
    lastDecision_ = d;
    (void)slots;
    char buf[128];

    switch (d.action) {
        case game::ConsumableAction::None:
        case game::ConsumableAction::Ignored:
        case game::ConsumableAction::ArmCloseButton:
        case game::ConsumableAction::ClosePanel:
            lastMessage_.clear();
            return;

        case game::ConsumableAction::Invalid:
            std::snprintf(buf, sizeof(buf), "Case %d : objet inconnu (id %u, hors ITEM_INFO)",
                          index + 1, static_cast<unsigned>(d.refId));
            break;

        case game::ConsumableAction::Unsupported:
            std::snprintf(buf, sizeof(buf), "Case %d : competences non gerees par cette barre (TODO SkillSystem)",
                          index + 1);
            break;

        case game::ConsumableAction::ShowTooltip:
            std::snprintf(buf, sizeof(buf), "Tooltip objet %u (case %d)",
                          static_cast<unsigned>(d.refId), index + 1);
            break;

        case game::ConsumableAction::BeginItemDrag:
            // MÉCANISME PROUVÉ (ré-audit W4-F3, décompilation UI_ConsumableBar_OnClick
            // 0x68E4B4) : un clic sur un slot N'ENVOIE PAS de paquet « usage d'objet »
            // isolé — c'est une PRISE de drag LOCALE (Item_BeginDragTransaction 0x5AFDF0,
            // conteneur type 21). L'usage réel survient au DROP. -> aucun Net_SendOpNN
            // direct à câbler ici ; contrepartie entrante = Pkt_ItemActionDispatch (0x1a).
            std::snprintf(buf, sizeof(buf), "Prise de drag objet %u (case %d)",
                          static_cast<unsigned>(d.refId), index + 1);
            break;
    }

    lastMessage_ = buf;
    TS2_LOG("ConsumableBar : %s", lastMessage_.c_str());
}

// =============================================================================
// Événements souris/clavier
// =============================================================================
// NOTE DE CÂBLAGE (front hud-quickbar-buff, W9) : le chemin de CLIC de la barre HUD
// du binaire est cGameHud_OnMouseDown 0x62B080 — NON reversé à ce jour. Les décisions
// ci-dessous restent donc celles de game::ConsumableBarLogic (portage du panneau
// 28 cases 0x68E270+, un objet DISTINCT — cf. bandeau de Game/ConsumableBarLogic.h) :
// c'est du pré-existant conservé tel quel, PAS une preuve pour la barre HUD.
// `game::TriggerSlot` garde `index >= slots.size()` (ConsumableBarLogic.cpp:59) : les
// index 10..13 du hit-test (14 cases réelles) y retournent None sans débordement.
bool ConsumableBarWindow::OnMouseDown(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    (void)slots;
    // Consomme le clic s'il tombe sur la barre (bloque le passage à la scène 3D
    // derrière le HUD) ; l'action réelle se décide au relâchement (OnClick).
    return lastLayout_.bar.Contains(x, y);
}

bool ConsumableBarWindow::OnClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const int index = HitTest(x, y);
    if (index < 0) return lastLayout_.bar.Contains(x, y);

    const game::ConsumableDecision d = game::TriggerSlot(slots, index, /*rightClick=*/false);
    ApplyDecision(d, index, slots);
    return true;
}

bool ConsumableBarWindow::OnRightClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const int index = HitTest(x, y);
    if (index < 0) return lastLayout_.bar.Contains(x, y);

    const game::ConsumableDecision d = game::TriggerSlot(slots, index, /*rightClick=*/true);
    ApplyDecision(d, index, slots);
    return true;
}

bool ConsumableBarWindow::OnHotkey(uint8_t dikScanCode, const std::array<QuickSlot, kQuickSlotCount>& slots) {
    const game::ConsumableDecision d = game::TriggerSlotByHotkey(slots, dikScanCode);
    if (d.slotIndex < 0) return false; // scancode hors plage 0x02..0x0B
    ApplyDecision(d, d.slotIndex, slots);
    return true;
}

// =============================================================================
// Render — deux passes (Panels puis Text), voir UIManager.h.
// Portage de UI_GameHud_Render 0x684D40-0x685177.
// =============================================================================
void ConsumableBarWindow::Render(const UiContext& ctx, const std::array<QuickSlot, kQuickSlotCount>& slots,
                                  int cursorX, int cursorY) {
    // `slots` (GameHud::slots_) n'a AUCUN écrivain dans src/ : ce n'est PAS la
    // source de vérité (cf. bandeau du .h). La barre lit g_Container5.
    (void)slots;
    // Le binaire ne dessine AUCUN survol sur la quickbar (0x684D40-0x685177 :
    // aucun Sprite2D_HitTest) -> curseur inutilisé.
    (void)cursorX;
    (void)cursorY;

    lastLayout_   = ComputeLayout(ctx.screenW, ctx.screenH);
    lastScreenW_  = ctx.screenW;
    lastScreenH_  = ctx.screenH;

    const int anchorX = lastLayout_.bar.x + kBarPad;
    const int anchorY = lastLayout_.bar.y + kBarPad;

    // TODO [dword_1675B1C 0x684D85 / dword_1675B20 0x684E81] : ces deux curseurs n'ont
    // AUCUN écrivain côté C++ (Grep exhaustif : uniquement des lecteurs) -> VarGet
    // renvoie 0 pour les deux. Ce n'est PAS une dégradation : ce sont des globals BSS
    // du binaire, donc eux aussi à 0 tant que rien ne les a écrits — le rendu obtenu
    // est exactement celui du binaire dans le même état (page 0 affichée « 1 », case 0
    // considérée comme sélectionnée). La page 0 EST alimentée par le réseau
    // (Net/GameHandlers_InvCells.cpp:462-464) : la barre affiche donc de vraies cases.
    // Restent à câbler, hors de ce front : le changement de page et la sélection de
    // case (la sentinelle « aucune sélection » du binaire est -1, cf.
    // Game/SkillCombat.h:237 et UI/AutoPlayWindow.cpp:224 @0x45AA6C).
    const int page = game::g_Client.VarGet(kVarPage);    // dword_1675B1C @0x684D85
    const int sel  = game::g_Client.VarGet(kVarSelSlot); // dword_1675B20 @0x684E81

    if (ctx.phase == UiPhase::Panels) {
        // --- Fond : deux variantes selon g_MorphUiMode (0x84DEF4) ---------------
        // `cmp ds:g_MorphUiMode, 1 / jnz short loc_684D68` @0x684D40 :
        //   ==1 -> unk_8EA020 (fichier 37) ; sinon -> unk_940388 (fichier 2423).
        // g_MorphUiMode est modélisé fidèlement par config::g_Options.MorphUiMode
        // (Config/GameOptions.h:107, offset 0x34, static_assert L.180, défaut 2,
        // borné [1,2]) — xrefs_to 0x84DEF4 confirme la lecture @0x684D40.
        const int bgFile = (config::g_Options.MorphUiMode == 1) ? kBgMorphFile : kBgNormalFile;
        BlitNatural(ctx, GetCatTex(ctx, 1, bgFile), anchorX, anchorY);

        // --- 14 cases ----------------------------------------------------------
        for (int i = 0; i < kBarSlotCount; ++i) {
            const LiveSlot ls = ReadContainer5(page, i);
            const int x = anchorX + i * kSlotPitch + kIconOffsetX;
            const int y = anchorY + kIconOffsetY;

            if (ls.type == kSlotTypeSkill) {
                // ---- case 1 : compétence (loc_684E40) --------------------------
                // `SkillGrowthTbl_GetRecord(mSKILL, id)` @0x684E62 ; NUL -> case
                // ignorée @0x684E6D-0x684E76.
                const game::SkillInfo* rec = game::GetSkillInfo(static_cast<uint32_t>(ls.refId));
                if (!rec) continue;

                // Trois états autour de Record[137] (= +548 = SkillInfo::field548) :
                //   i != dword_1675B20            -> +1  @0x684E89-0x684E98
                //   i == dword_1675B20 et auto-use déclencherait -> +2  @0x684EE5-0x684EF4
                //   i == dword_1675B20 sinon      -> +3  @0x684EF9-0x684F08
                int tabIcon;
                if (i != sel) {
                    tabIcon = static_cast<int>(rec->field548) + 1;
                } else {
                    // TODO [AutoUse_ShouldTrigger 0x662590, appelée @0x684EDC avec
                    // (ecx=dword_18392C0, a1=skillId, a2=count)] : prédicat NON porté.
                    // Il dépend de g_LearnedSkills 0x16742BC (+ niveaux 0x16742C0),
                    // SkillGrowthTbl_InterpStatByLevel 0x4C4EE0, Char_CalcRegen 0x4D67F0,
                    // g_EquipSnapshotScratch 0x8E719C, dword_1687378 (HP courants) et
                    // dword_1673248 (arme équipée) — aucun de ces éléments n'est modélisé
                    // côté C++, et leur portage relève de Game/ (hors de ce front).
                    // DÉGRADATION ASSUMÉE : on prend la branche NÉGATIVE (+3 = « slot
                    // sélectionné, auto-use ne se déclencherait pas »). L'état +2 n'est
                    // donc jamais atteint — on ne fabrique jamais un signal positif faux.
                    tabIcon = static_cast<int>(rec->field548) + 3;
                }
                // Atlas COMPÉTENCES unk_A1BD60 = base de la catégorie 3 (@0x4DEC11,
                // 0x8E8B30 + 1258032 = 0xA1BD60, division exacte) : slot = tabIcon,
                // donc fichier = tabIcon + 1. Blit @0x684F31-0x684F3D.
                BlitNatural(ctx, GetCatTex(ctx, 3, tabIcon + 1), x, y);

                // ---- overlay de recharge (11 paliers) --------------------------
                // Gate @0x684F42-0x684F5A : `Record[140] != Record[141]`
                // (0x230/4=140 = SkillInfo::spCost ; 0x234/4=141 = SkillInfo::levelNorm).
                if (rec->spCost != rec->levelNorm) {
                    // @0x684F70-0x684F89 : fild count / fmul 11.0 / fidiv levelNorm /
                    // Crt_ftol (troncature vers zéro, == static_cast<int>).
                    // `fidiv` divise par un ENTIER -> cast explicite en int32 puis double.
                    // levelNorm est validé [1,1000] par SkillGrowthTbl_ValidateRecord
                    // 0x4C4160 : la garde != 0 est un simple garde-fou anti-UB (le
                    // binaire lèverait une exception FP), jamais prise en pratique.
                    const int32_t norm = static_cast<int32_t>(rec->levelNorm);
                    if (norm != 0) {
                        const int v = static_cast<int>(static_cast<double>(ls.count) * kCooldownSteps
                                                       / static_cast<double>(norm));
                        // @0x684FB4-0x684FC9 : slot = v + 0x268 (616) de l'atlas cat.1
                        // (g_AssetMgr_UiAtlasSlots 0x8E8B50) -> fichier = 617 + v.
                        // Superposé à l'icône, AU MÊME point (x, y). Le binaire ne borne
                        // pas v : un v hors [0,10] y lirait un slot voisin ; ici le
                        // chargement échoue simplement -> rien n'est dessiné.
                        BlitNatural(ctx, GetCatTex(ctx, 1, kCooldownFile0 + v), x, y);
                    }
                }
            } else if (ls.type == kSlotTypeTabIcon) {
                // ---- case 2 : icône d'onglet (loc_684FD3) ----------------------
                // TODO [cQuickSlotWin_GetTabIcon 0x662750, appelée @0x684FF4 avec
                // (ecx=dword_18392C0, a1=itemId)] : « map tab index 1-9 to icon sprite
                // base (600-640) » — table NON portée. Le binaire : `if (v == -1)
                // continue;` @0x684FFC-0x685002, puis `v += 2` @0x685007-0x68500D et
                // `Sprite2D_Draw(unk_A1BD60 + 148*v, x, y)` @0x685036-0x685042 (atlas
                // cat.3, donc fichier = v + 1). Sans la table, l'index est inconnu ->
                // aucune icône dessinée (repli muet, jamais une icône fausse).
                continue;
            } else if (ls.type == kSlotTypeItem) {
                // ---- case 3 : objet (loc_68504C) -------------------------------
                // `MobDb_GetEntry(mITEM, id)` @0x68506E ; NUL -> case ignorée
                // @0x685079-0x685082.
                const game::ItemInfo* info = game::GetItemInfo(static_cast<uint32_t>(ls.refId));
                if (!info) continue;
                // Atlas OBJETS g_AssetMgr_ItemIconSlots 0x98B4E0 (catégorie 2), base
                // DISTINCTE de celle des compétences. Blit @0x6850BC-0x6850CB.
                BlitNatural(ctx, GetIconTex(ctx, static_cast<uint32_t>(ls.refId)), x, y);
            }
            // type == 0 (ou toute autre valeur) : `jmp loc_685155` @0x684E3B -> case
            // vide, RIEN n'est dessiné (pas même un cadre : le binaire n'en dessine
            // aucun sur cette barre).
        }
    }

    if (ctx.phase == UiPhase::Text) {
        // --- Numéro de page ----------------------------------------------------
        // @0x684D85-0x684DC9 : `Crt_Vsnprintf(&String, "%d", dword_1675B1C + 1)` puis
        // `UI_DrawNumberValue(unk_1685740, &String, this[0]+9, this[1]+0Fh, 1)`.
        // Affichage 1-based.
        {
            char pageBuf[16];
            std::snprintf(pageBuf, sizeof(pageBuf), "%d", page + 1);
            ctx.Text(pageBuf, anchorX + kPageOffsetX, anchorY + kPageOffsetY, kTextWhite);
        }

        // --- Compteur de pile par case -----------------------------------------
        // @0x6850D0-0x685150, UNIQUEMENT dans la branche objet (case 3) et seulement
        // si `dword_1674400[...] > 0` (`cmp ..., 0 / jle loc_685155` @0x6850E4).
        // C'est le COMPTEUR DU SLOT, pas game::InventoryCount(refId) (le binaire
        // n'interroge jamais l'inventaire possédé ici).
        for (int i = 0; i < kBarSlotCount; ++i) {
            const LiveSlot ls = ReadContainer5(page, i);
            if (ls.type != kSlotTypeItem) continue;
            if (!game::GetItemInfo(static_cast<uint32_t>(ls.refId))) continue; // @0x685079
            if (ls.count <= 0) continue;                                       // @0x6850EC

            char qtyBuf[16];
            std::snprintf(qtyBuf, sizeof(qtyBuf), "%d", ls.count);
            ctx.Text(qtyBuf, anchorX + i * kSlotPitch + kCountOffsetX,
                     anchorY + kCountOffsetY, kTextWhite);
        }
    }
}

} // namespace ts2::ui

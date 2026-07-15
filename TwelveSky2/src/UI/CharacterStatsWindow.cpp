// UI/CharacterStatsWindow.cpp — implémentation de la fiche personnage.
// Voir UI/CharacterStatsWindow.h pour les références de RE (StatFormulas.h,
// GameState.h, opcode 0x58 cultivation).
#include "UI/CharacterStatsWindow.h"
#include "UI/PanelSkin.h"

#include <cstdio>

namespace ts2::ui {

// ===========================================================================
// Palette (ARGB, D3DCOLOR = 0xAARRGGBB) — cf. contrat UI.
// ===========================================================================
namespace {

// Fond de panneau réel (best effort) : gabarit (446,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec la fiche personnage (480x380 ; cf. méthodologie
// détaillée dans UI/PanelSkin.h). Indice distinct de celui utilisé par
// GuildWindow (même cluster de tailles, fichiers différents). Repli
// automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01338.IMG");

constexpr D3DCOLOR kColBg        = Argb(0xE0, 0x20, 0x20, 0x28); // fond panneau
constexpr D3DCOLOR kColTitleBg   = Argb(0xF0, 0x18, 0x18, 0x20); // bandeau titre (plus sombre)
constexpr D3DCOLOR kColFrame     = Argb(0xFF, 0x80, 0x80, 0x80); // cadre
constexpr D3DCOLOR kColText      = Argb(0xFF, 0xFF, 0xFF, 0xFF); // texte normal
constexpr D3DCOLOR kColTitle     = Argb(0xFF, 0xFF, 0xDD, 0x66); // titre
constexpr D3DCOLOR kColLabel     = Argb(0xFF, 0xC0, 0xC0, 0xC8); // libellés (gris clair)
constexpr D3DCOLOR kColHover     = Argb(0xFF, 0x40, 0x60, 0xA0); // survol
constexpr D3DCOLOR kColBtn       = Argb(0xFF, 0x38, 0x40, 0x50); // bouton normal
constexpr D3DCOLOR kColBtnDown   = Argb(0xFF, 0x58, 0x84, 0xC8); // bouton enfoncé
constexpr D3DCOLOR kColHp        = Argb(0xFF, 0xE0, 0x40, 0x40); // teinte "Vie"
constexpr D3DCOLOR kColMp        = Argb(0xFF, 0x40, 0x60, 0xE0); // teinte "Mana"
constexpr D3DCOLOR kColUnspent   = Argb(0xFF, 0x80, 0xE0, 0x80); // points non dépensés (vert)
constexpr D3DCOLOR kColDivider   = Argb(0xFF, 0x50, 0x50, 0x58); // ligne de séparation

// --- Constantes de géométrie ---
constexpr int kBoxW      = 480;
constexpr int kBoxH      = 380;
constexpr int kTitleH    = 28;
constexpr int kCloseSize = 18;
constexpr int kRowH      = 24;
constexpr int kPlusSize  = 18;

// Grille 2x2 des attributs primaires — RÉELLE (cDrawWin_Draw 0x629C9E/0x629D66,
// cf. bandeau CONFIRME_FIDELE du .h) : valeurs alignées à droite en (+107,+110)
// et (+203,+110)/(+107,+132)/(+203,+132) depuis l'origine panneau.
constexpr int kAttrRowY0      = 110; // ligne haute (ExtForce / IntForce)
constexpr int kAttrRowY1      = 132; // ligne basse (Defensive / Offensive)
constexpr int kAttrValueColX0 = 107; // colonne gauche (bord droit du texte valeur)
constexpr int kAttrValueColX1 = 203; // colonne droite

constexpr int kStatsStartYOff = 200; // depuis box.y (sous le séparateur) — non ré-audité cette passe
} // namespace

// ===========================================================================
// Libellés / accès attributs primaires
// ===========================================================================
const char* CharacterStatsWindow::AttrLabel(PrimaryAttr a) {
    switch (a) {
        case PrimaryAttr::ExtForce:  return "Force Externe";
        case PrimaryAttr::IntForce:  return "Force Interne";
        case PrimaryAttr::Defensive: return "Défensif";
        case PrimaryAttr::Offensive: return "Offensif";
    }
    return "?";
}

int CharacterStatsWindow::AttrValue(const game::SelfState& s, PrimaryAttr a) {
    switch (a) {
        case PrimaryAttr::ExtForce:  return s.attrExtForce;
        case PrimaryAttr::IntForce:  return s.attrIntForce;
        case PrimaryAttr::Defensive: return s.attrDefensive;
        case PrimaryAttr::Offensive: return s.attrOffensive;
    }
    return 0;
}

// ===========================================================================
// Layout — ANCRAGE PROPORTIONNEL réel (CONFIRME_FIDELE, cf. bandeau du .h) :
// PAS un centrage écran. Reproduit exactement UI_ProjectSpriteToScreen 0x50F5D0
// tel qu'appelé par cDrawWin_Draw/cDrawWin_OnMouseDown (0x6299AA/0x628ED0) :
//   x = round((kDesignAnchorX + w/2) * screenW / kRefWidth)  - w/2
//   y = round((kDesignAnchorY + h/2) * screenH / kRefHeight) - h/2
// où w/h = dimensions du panneau (kBoxW/kBoxH ; réelles non confirmées, cf. .h).
// À la résolution de référence (kRefWidth x kRefHeight), se réduit exactement à
// (x,y) = (kDesignAnchorX, kDesignAnchorY) = (115,105) — coin HAUT-GAUCHE de
// l'écran, pas le centre.
// ===========================================================================
void CharacterStatsWindow::ComputeLayout(int screenW, int screenH, Layout& L) const {
    L.box.w = kBoxW;
    L.box.h = kBoxH;

    // Formule identique à UI_ProjectSpriteToScreen 0x50F5D0 (ancre le CENTRE du
    // panneau à la même fraction d'écran que sa position de conception, taille
    // pixel du panneau non mise à l'échelle).
    const long long centerXNum = static_cast<long long>(kDesignAnchorX + kBoxW / 2) * screenW;
    const long long centerYNum = static_cast<long long>(kDesignAnchorY + kBoxH / 2) * screenH;
    const int centerX = static_cast<int>(centerXNum / ts2::kRefWidth);
    const int centerY = static_cast<int>(centerYNum / ts2::kRefHeight);
    L.box.x = centerX - kBoxW / 2;
    L.box.y = centerY - kBoxH / 2;

    L.titleBar = Rect{ L.box.x, L.box.y, L.box.w, kTitleH };

    // Bouton fermeture réel : offset fixe (8,6) depuis l'origine panneau
    // (HAUT-GAUCHE), cf. cDrawWin_OnMouseDown 0x629188.
    L.closeBtn = Rect{ L.box.x + kCloseOffX, L.box.y + kCloseOffY,
                        kCloseSize, kCloseSize };

    // Boutons "+1" réels : grille 2x2 fixe (PAS une colonne), cf. bandeau du .h.
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        L.plusBtn[i] = Rect{ L.box.x + kPlusOffX[col], L.box.y + kPlusOffY[row],
                              kPlusSize, kPlusSize };
    }
}

// ===========================================================================
// Cycle de vie
// ===========================================================================
void CharacterStatsWindow::Open() {
    Dialog::Open();
    closeArmed_ = false;
    for (bool& b : plusArmed_) b = false;
}

// ===========================================================================
// Souris
// ===========================================================================
bool CharacterStatsWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) {
        closeArmed_ = true;
        return true;
    }

    const game::SelfState& self = game::g_World.self;
    if (self.unspentAttr > 0) {
        for (int i = 0; i < kPrimaryAttrCount; ++i) {
            const Rect& r = L.plusBtn[i];
            if (PointInRect(x, y, r.x, r.y, r.w, r.h)) {
                plusArmed_[i] = true;
                return true;
            }
        }
    }

    // Clic n'importe où ailleurs dans le panneau : consommé (empêche le clic de
    // "traverser" jusqu'au monde 3D derrière la fenêtre) mais n'arme rien.
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

bool CharacterStatsWindow::OnClick(int x, int y) {
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

    const game::SelfState& self = game::g_World.self;
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        if (!plusArmed_[i]) continue;
        plusArmed_[i] = false;
        const Rect& r = L.plusBtn[i];
        if (self.unspentAttr > 0 &&
            PointInRect(x, y, r.x, r.y, r.w, r.h)) {
            // TODO(send) : dépense d'un point sur l'attribut primaire AttrLabel(PrimaryAttr(i)).
            // CORRECTIF (ré-audit 2026-07-14, preuve IDA fraîche) : l'ancien commentaire
            // ci-dessous citait Net_SendOp88 comme candidat par simple rapprochement de
            // description spec ("9x int8, relevé stats perso") — CE N'EST PAS le bon
            // builder, hypothèse non vérifiée. La décompilation directe (xrefs_to sur
            // Net_SendOp88 0x4BBF60 : UN SEUL appelant dans tout le binaire, 0x602b7b)
            // montre que l'unique site d'appel d'origine est UI_Dlg41_OnLUp (0x602490),
            // le gestionnaire de clic d'une fenêtre de COMBINAISON D'OBJETS À 4 SLOTS
            // (matrice de compatibilité d'ID objets 1002-1011/1178-1184-1235, "recipe"
            // de craft/fusion), PAS la fiche personnage : les 9 arguments envoyés sont
            // *(this+799) (sous-résultat de la combinaison, 0-3) puis 4 PAIRES
            // *(this+4/10, +5/11, +6/12, +7/13) (les 4 objets des slots), rien à voir
            // avec un attribut primaire ni un point à dépenser. Aucune fenêtre de
            // combinaison/craft n'existe dans ClientSource à ce jour -> Net_SendOp88
            // reste À JUSTE TITRE non câblé ici ; le VRAI opcode "dépenser 1 point
            // d'attribut" (dispatcher ENTRANT symétrique Net_OnCultivationDispatch
            // 0x493180, opcode 0x58, Net/RecvPackets.h::CultivationDispatch, 20
            // sous-opcodes réécrivant dword_16731B8/BC/C0/C4/D0 = attrDefensive/
            // attrExtForce/attrOffensive/attrIntForce/unspentAttr) n'est TOUJOURS PAS
            // identifié dans RE/opcode_table.json ni RE/outbound_results.json -> NE
            // PAS deviner ; nécessite soit une RE statique plus poussée du dispatcher
            // envoyant sur ce même opcode 0x58 depuis un autre site, soit un
            // breakpoint dynamique sur le clic "+" en jeu pour capturer le vrai
            // builder/sous-opcode.
            return true;
        }
    }

    // Relâché n'importe où dans le panneau : consommé.
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

bool CharacterStatsWindow::OnKey(int vk) {
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
void CharacterStatsWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Mémorise les dims écran courantes pour que le hit-test (routé entre deux
    // frames) s'aligne sur la géométrie effectivement dessinée. Fait dans les
    // deux sous-passes (Panels puis Text), comme MsgBoxDialog.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Layout L;
    ComputeLayout(ctx.screenW, ctx.screenH, L);

    const game::SelfState& self = game::g_World.self;
    const bool hasPoints = self.unspentAttr > 0;

    char buf[96];

    if (ctx.phase == UiPhase::Panels) {
        // --- Fond + cadre + bandeau de titre ---
        kPanelBg.Draw(ctx, L.box.x, L.box.y, L.box.w, L.box.h, kColBg);
        ctx.FillRect(L.titleBar.x, L.titleBar.y, L.titleBar.w, L.titleBar.h, kColTitleBg);
        ctx.DrawFrame(L.box.x, L.box.y, L.box.w, L.box.h, kColFrame, 2);
        ctx.FillRect(L.box.x, L.box.y + kTitleH, L.box.w, 1, kColDivider);

        // --- Bouton fermeture ---
        const bool closeHover = PointInRect(cursorX, cursorY, L.closeBtn.x, L.closeBtn.y,
                                            L.closeBtn.w, L.closeBtn.h);
        const D3DCOLOR closeCol = closeArmed_ ? kColBtnDown : (closeHover ? kColHover : kColBtn);
        ctx.FillRect(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, closeCol);
        ctx.DrawFrame(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, kColFrame, 1);

        // --- Séparateur avant les stats dérivées ---
        const int sepY = L.box.y + kStatsStartYOff - 12;
        ctx.FillRect(L.box.x + 16, sepY, L.box.w - 32, 1, kColDivider);

        // --- Boutons "+" par attribut primaire (seulement s'il reste des points) ---
        if (hasPoints) {
            for (int i = 0; i < kPrimaryAttrCount; ++i) {
                const Rect& r = L.plusBtn[i];
                const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
                const D3DCOLOR col = plusArmed_[i] ? kColBtnDown : (hover ? kColHover : kColBtn);
                ctx.FillRect(r.x, r.y, r.w, r.h, col);
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColFrame, 1);
            }
        }
        return;
    }

    // --- Phase texte -----------------------------------------------------
    // Titre centré dans le bandeau.
    const int titleW = ctx.MeasureText("Personnage");
    ctx.Text("Personnage", L.box.x + (L.box.w - titleW) / 2, L.titleBar.y + 6, kColTitle);
    ctx.Text("X", L.closeBtn.x + 5, L.closeBtn.y + 2, kColText);

    // Ligne niveau + points non dépensés.
    std::snprintf(buf, sizeof(buf), "Niveau %d", self.level);
    ctx.Text(buf, L.box.x + 20, L.box.y + kTitleH + 12, kColText);
    std::snprintf(buf, sizeof(buf), "Points non dépensés : %d", self.unspentAttr);
    ctx.Text(buf, L.box.x + 180, L.box.y + kTitleH + 12,
             hasPoints ? kColUnspent : kColLabel);

    // Attributs primaires — grille RÉELLE 2 colonnes x 2 lignes (cf. bandeau
    // CONFIRME_FIDELE du .h : cDrawWin_Draw dessine les 4 valeurs à
    // (+107,+110)/(+203,+110)/(+107,+132)/(+203,+132) alignées à droite, bouton
    // "+1" juste à gauche de chaque colonne). Le libellé texte (absent du binaire
    // d'origine — probablement intégré au bitmap de fond) reste une adjonction
    // pragmatique, positionnée juste avant chaque valeur.
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        const auto attr = static_cast<PrimaryAttr>(i);
        const int col = i % 2;
        const int row = i / 2;
        const int y = L.box.y + (row == 0 ? kAttrRowY0 : kAttrRowY1);
        const int valueRightX = L.box.x + (col == 0 ? kAttrValueColX0 : kAttrValueColX1);

        std::snprintf(buf, sizeof(buf), "%d", AttrValue(self, attr));
        const int vw = ctx.MeasureText(buf);
        ctx.Text(buf, valueRightX - vw, y, kColText);

        std::snprintf(buf, sizeof(buf), "%s :", AttrLabel(attr));
        const int lw = ctx.MeasureText(buf);
        ctx.Text(buf, valueRightX - vw - lw - 4, y, kColLabel);

        if (hasPoints) {
            const Rect& r = L.plusBtn[i];
            const int plusW = ctx.MeasureText("+");
            ctx.Text("+", r.x + (r.w - plusW) / 2, r.y + 1, kColText);
        }
    }

    // Stats dérivées — 2 colonnes x 6 lignes (byte-exact, lues depuis SelfState,
    // calculées par StatEngine::Recompute via Game/StatFormulas.h/.cpp).
    struct StatRow { const char* label; int value; D3DCOLOR color; };
    const StatRow col1[6] = {
        { "Vie Max",            self.maxHp,       kColHp   },
        { "Mana Max",           self.maxMp,       kColMp   },
        { "Attaque Externe",    self.extAtk,      kColText },
        { "Attaque Interne",    self.intAtk,      kColText },
        { "Défense Externe",    self.extDef,      kColText },
        { "Défense Interne",    self.intDef,      kColText },
    };
    const StatRow col2[6] = {
        { "Précision",          self.accuracy,     kColText },
        { "Esquive",            self.evasion,      kColText },
        { "Taux Critique",      self.critRate,     kColText },
        { "Rating Att. Min",    self.atkRatingMin, kColText },
        { "Rating Att. Max",    self.atkRatingMax, kColText },
        { "Vitesse Attaque",    self.attackSpeed,  kColText },
    };

    const int col1LabelX = L.box.x + 20;
    const int col1ValueX = L.box.x + 210;
    const int col2LabelX = L.box.x + 250;
    const int col2ValueX = L.box.x + 445;

    for (int i = 0; i < 6; ++i) {
        const int y = L.box.y + kStatsStartYOff + i * kRowH;

        std::snprintf(buf, sizeof(buf), "%s :", col1[i].label);
        ctx.Text(buf, col1LabelX, y, kColLabel);
        std::snprintf(buf, sizeof(buf), "%d", col1[i].value);
        const int v1w = ctx.MeasureText(buf);
        ctx.Text(buf, col1ValueX - v1w, y, col1[i].color);

        std::snprintf(buf, sizeof(buf), "%s :", col2[i].label);
        ctx.Text(buf, col2LabelX, y, kColLabel);
        std::snprintf(buf, sizeof(buf), "%d", col2[i].value);
        const int v2w = ctx.MeasureText(buf);
        ctx.Text(buf, col2ValueX - v2w, y, col2[i].color);
    }
}

} // namespace ts2::ui

// UI/ItemTooltip.cpp — implémentation de l'infobulle d'objet réutilisable.
// Voir UI/ItemTooltip.h.
#include "UI/ItemTooltip.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ts2::ui {
namespace {

// Palette (D3DCOLOR = 0xAARRGGBB) — reprend les couleurs communes du shell UI.
constexpr D3DCOLOR kColBg     = 0xE0202028u; // fond du panneau
constexpr D3DCOLOR kColBorder = 0xFF808080u; // cadre
constexpr D3DCOLOR kColTitle  = 0xFFFFDD66u; // nom de l'objet
constexpr D3DCOLOR kColText   = 0xFFFFFFFFu; // texte courant (niveau, quantité, stats)
constexpr D3DCOLOR kColHp     = 0xFFE04040u; // ligne PV max
constexpr D3DCOLOR kColMp     = 0xFF4060E0u; // ligne PM max

constexpr int kPad    = 8;  // marge interne du panneau
constexpr int kRowH   = 14; // hauteur de ligne (police 12 + interligne)
constexpr int kGapTop = 4;  // espace supplémentaire après le titre
constexpr int kOffset = 16; // décalage par rapport au curseur (ne pas le masquer)

struct Line {
    std::string text;
    D3DCOLOR    color;
};

// Ajoute une ligne "label : ±valeur" si `val` != 0 (stats plates non nulles).
void AddStatLine(std::vector<Line>& lines, const char* label, int32_t val, D3DCOLOR color) {
    if (val == 0) return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s : %s%d", label, (val > 0 ? "+" : ""), static_cast<int>(val));
    lines.push_back({buf, color});
}

} // namespace

void DrawItemTooltip(const UiContext& ctx, int x, int y, uint32_t itemId,
                      const game::InvCell* cellOpt) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info) return; // id invalide / slot vide (MobDb_GetEntry 0x4C3C00 : nullptr)

    // Niveau d'enchant décodé depuis le mot socket/durabilité de la cellule (octet 3),
    // cf. Item_GetAttribByte3 / note de fidélité en tête de ItemTooltip.h.
    int enchantLvl = 0;
    if (cellOpt && cellOpt->itemId != 0) {
        enchantLvl = static_cast<int>(game::Item_GetAttribByte3(cellOpt->color));
    }

    // --- Construction du contenu (indépendante de la phase de rendu) ---
    std::vector<Line> lines;
    lines.reserve(12);

    std::string title(info->name); // char[25] garanti nul-terminé dans [0..24]
    if (enchantLvl > 0) title = "+" + std::to_string(enchantLvl) + " " + title;
    lines.push_back({title, kColTitle});

    lines.push_back({"Niveau requis : " + std::to_string(info->itemLevel), kColText});

    if (cellOpt && cellOpt->flag > 1) {
        lines.push_back({"Quantité : " + std::to_string(cellOpt->flag), kColText});
    }

    AddStatLine(lines, "Attaque externe", info->extAttack,  kColText);
    AddStatLine(lines, "Attaque interne", info->intAttack,  kColText);
    AddStatLine(lines, "Défense externe", info->extDefense, kColText);
    AddStatLine(lines, "Défense interne", info->intDefense, kColText);
    AddStatLine(lines, "PV max",          info->maxHp,      kColHp);
    AddStatLine(lines, "PM max",          info->maxMp,      kColMp);
    AddStatLine(lines, "Précision",       info->accuracy,   kColText);
    AddStatLine(lines, "Esquive",         info->evasion,    kColText);
    AddStatLine(lines, "Taux critique",   info->critRate,   kColText);

    // --- Géométrie (largeur = texte le plus large ; hauteur = somme des lignes) ---
    int maxTextW = 0;
    for (const Line& l : lines) {
        const int w = ctx.MeasureText(l.text.c_str());
        if (w > maxTextW) maxTextW = w;
    }
    const int boxW = maxTextW + kPad * 2;
    const int boxH = kPad * 2 + kGapTop + static_cast<int>(lines.size()) * kRowH;

    // --- Positionnement près du curseur, clampé à l'écran (ne déborde jamais) ---
    int tx = x + kOffset;
    int ty = y + kOffset;
    if (tx + boxW > ctx.screenW) tx = x - kOffset - boxW; // bascule à gauche du curseur
    if (ty + boxH > ctx.screenH) ty = y - kOffset - boxH; // bascule au-dessus du curseur
    if (tx + boxW > ctx.screenW) tx = ctx.screenW - boxW; // écran trop étroit : colle au bord
    if (ty + boxH > ctx.screenH) ty = ctx.screenH - boxH;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;

    // --- Rendu : phase Panels (fond + cadre) ---
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(tx, ty, boxW, boxH, kColBg);
        ctx.DrawFrame(tx, ty, boxW, boxH, kColBorder, 1);
        return;
    }

    // --- Rendu : phase Text (nom, niveau, quantité, stats) ---
    int ly = ty + kPad;
    for (size_t i = 0; i < lines.size(); ++i) {
        ctx.Text(lines[i].text.c_str(), tx + kPad, ly, lines[i].color);
        ly += kRowH;
        if (i == 0) ly += kGapTop; // respire après le titre
    }
}

} // namespace ts2::ui

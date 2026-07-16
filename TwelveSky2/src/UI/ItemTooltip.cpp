// UI/ItemTooltip.cpp — implémentation de l'infobulle d'objet (Item_DrawTooltip
// 0x652AD0). Voir UI/ItemTooltip.h pour la correction de prémisse et le TODO de
// câblage (gap TT-01).
#include "UI/ItemTooltip.h"
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h"   // game::Str (StrTable005_Get 0x4C1D10)
#include "Game/StringTables.h"    // game::g_Strings.colors (mFONTCOLOR 0x84DF20)

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ts2::ui {
namespace {

// --- Constantes relevées dans le cadre de pile / les opérandes de 0x652AD0 -----
constexpr int kLineStride = 101; // `imul …, 65h` — stride d'une ligne (var_3380)
constexpr int kMaxLines   = 100; // var_8D8 = _DWORD[101], var_3380 = char[10104]
constexpr int kTileW      = 13;  // `imul eax, 0Dh` @0x65e2f9
constexpr int kTileH      = 15;  // `imul edx, 0Fh` @0x65e2ec
constexpr int kTextTopPad = 16;  // `add ecx, 10h`  @0x65e333
constexpr int kLineAdv    = 15;  // `add eax, 0Fh`  @0x65e584

// Tuile de fond : Sprite2D unk_9404B0 = slot 2424 de l'atlas UI catégorie 1
// (`mov ecx, offset unk_9404B0 / call Sprite2D_Draw` @0x65e300-0x65e305).
// (0x9404B0 - 0x8E8B50) / 148 = 2424, reste 0 -> « 001_02425.IMG ». La tuile fait
// EXACTEMENT 13x15 (l'en-tête GXD du fichier décodé donne dword[0]=13, dword[1]=15),
// ce qui recoupe les strides 0x0D/0x0F du désassemblage.
constexpr int kTooltipBgSlot = 2424;

// Repli si l'atlas est absent/illisible : le binaire ne dessinerait rien du tout,
// mais un fond neutre garde le texte lisible plutôt que de le noyer dans la scène 3D.
// SEULE divergence volontaire de ce fichier, cantonnée au chemin d'erreur.
constexpr D3DCOLOR kColBgFallback = 0xE0202028u;

// --- Une ligne construite : texte + INDEX de couleur (var_3380[] / var_8D8[]) ---
// Le binaire ne stocke JAMAIS de D3DCOLOR ici : var_8D8[i] est un index résolu au
// dessin par ColorTable_GetColor(dword_84DF20, idx) dans UI_DrawNumberValue
// 0x53FCC0 (@0x53fcd2).
struct TooltipLine {
    char text[kLineStride];
    int  colorIdx;
};

class LineList {
public:
    // Équivalent de `Crt_Vsnprintf(&var_3380[n*0x65], fmt, …) ; var_8D8[n] = idx ; ++n`.
    void Add(int colorIdx, const char* fmt, ...) {
        if (lines_.size() >= static_cast<size_t>(kMaxLines)) return; // borne var_3380/var_8D8
        TooltipLine l{};
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(l.text, sizeof(l.text), fmt, ap);
        va_end(ap);
        l.colorIdx = colorIdx;
        lines_.push_back(l);
    }
    int Count() const { return static_cast<int>(lines_.size()); }
    const TooltipLine& operator[](int i) const { return lines_[static_cast<size_t>(i)]; }

private:
    std::vector<TooltipLine> lines_;
};

// --- Construction du contenu (switch principal) --------------------------------
// Item_DrawTooltip branche sur rec[0xBC] (= ItemInfo::typeCode, +188) via une
// jumptable de 36 cas : `sub edx, 1 / cmp ecx, 23h / ja def_652BE3 /
// jmp jpt_652BE3[ecx*4]` @0x652bc0-0x652be3 (table 0x65EA1C, index byte_65EA5C ;
// les cas 25/27/33/34 tombent sur le défaut 0x654AA8).
void BuildLines(const game::ItemInfo* info, LineList& lines) {
    const char* name = info->name; // rec+4 (`add edx, 4` @0x652bf0)

    switch (info->typeCode) {
    // Cas 1 (@0x652bea) et 2 (@0x652c37) : nom seul, index de couleur 1.
    //   Crt_Vsnprintf(line, "%s", rec+4) ; var_8D8[n] = 1 ; ++n
    case 1:
    case 2:
        lines.Add(1, "%s", name);                                  // @0x652c18 / 0x652c65
        break;

    // Cas 3 (@0x652c84) : sous-switch de 6 cas sur rec[0xB8] (= category, +184) —
    // `sub edx, 1 / cmp …, 5 / ja def_652CB8 / jmp jpt_652CB8[eax*4]`
    // @0x652c9c-0x652cb8 (table 0x65EA80). Chaque cas préfixe le nom par un libellé
    // 005.DAT : Crt_Vsnprintf(line, "%s%s", StrTable005_Get(id), rec+4), couleur 1.
    // Les 6 ids sont lus directement dans les opérandes `push imm32` :
    //   cat 1 -> 0x3E9 @0x652cbf   cat 2 -> 0x3EA @0x652d1c
    //   cat 3 -> 0x3EB @0x652d79   cat 4 -> 0x3EC @0x652dd6
    //   cat 5 -> 0xA00 @0x652e33   cat 6 -> 0xA00 @0x652e8d  (le MÊME id, pas une faute)
    case 3: {
        int labelId = 0;
        switch (info->category) {
        case 1: labelId = 0x3E9; break;
        case 2: labelId = 0x3EA; break;
        case 3: labelId = 0x3EB; break;
        case 4: labelId = 0x3EC; break;
        case 5: labelId = 0xA00; break;
        case 6: labelId = 0xA00; break;
        default:
            // TODO [ancre 0x652EE5] — défaut du sous-switch de catégorie non
            // transposé (corps non relevé). Aucune ligne émise plutôt qu'une
            // invention.
            break;
        }
        if (labelId != 0)
            lines.Add(1, "%s%s", game::Str(labelId).c_str(), name);  // "%s%s" 0x7ED578, couleur @0x652cfd
        break;
    }

    default:
        // TODO [ancre 0x652BE3, jumptable 0x65EA1C / index byte_65EA5C] — les 33
        // autres cas de typeCode (4..24, 26, 28..32, 35, 36 ; 25/27/33/34 = défaut
        // 0x654AA8) construisent chacun leurs propres lignes via Crt_Vsnprintf avec
        // les formats "%s", "%s%s", "%s : %d%%" (0x7BAD70), "%s : %s%s" (0x7BAD2C),
        // "%s : %s%s[%s(%d)]" (0x7BAD18) et des ids 005.DAT relevés au coup par coup
        // (ex. 0x422 @0x65bd67, 0x463 @0x65bdcf), avec leurs propres index de couleur
        // (1, 0x18, 0x1B...). ~143 Ko de pseudocode NON transposés ici : chaque cas
        // exige d'être prouvé ligne à ligne. Ne RIEN émettre est fidèle au silence de
        // ce fichier sur ces cas — c'est une lacune assumée, pas une devinette.
        break;
    }

    // TODO [ancre 0x65E0E0] — LIGNE DE PRIX non émise (gap TT-04, partie prix).
    // Le CORPS est prouvé et transposable tel quel :
    //     Str_FormatThousands(rec[0xE0] /*= ItemInfo::field224, +224*/, &buf, &colorIdx)  @0x65e107
    //     Crt_Vsnprintf(line, "%s : %s%s" /*0x7BAD2C*/,
    //                   StrTable005_Get(0x425) /*@0x65e128*/, buf,
    //                   StrTable005_Get(0x24D) /*@0x65e111*/)                             @0x65e149
    //     ++var_3384                                                                       @0x65e157
    // Str_FormatThousands 0x546390 est lui aussi entièrement relevé (cascade de 10
    // paliers ; l'index de couleur est un PARAMÈTRE DE SORTIE, pas une constante :
    // 1 sous le million, puis 28 @0x5466f6 / 24 @0x54663a / 3 @0x546568 / 25
    // @0x546480, et chaîne VIDE + index 2 si la valeur est négative @0x546985).
    // Il n'est PAS transposé ici : sans la garde ci-dessous il n'aurait aucun
    // appelant. La GARDE qui mène à ce bloc n'est PAS établie — 0x65E0E0 n'est pas un
    // fallthrough du bloc "%s : %s%s[%s(%d)]" qui le précède (celui-ci se termine
    // @0x65e0ce et rejoint le label loc_65E0D1), c'est une branche alternative dont
    // la condition reste à prouver. L'émettre inconditionnellement ajouterait une
    // ligne de prix à des objets qui n'en ont pas. À compléter en décompilant le bloc
    // 0x65DF00-0x65E0E0, puis transposer les deux fonctions ensemble.

    // TODO [ancre 0x65E598] — SECOND PANNEAU non émis (gap TT-08a). Garde prouvée
    // `cmp [ecx+0BCh], 21h / jnz loc_65EA08` @0x65e5a8-0x65e5af (typeCode == 0x21) ;
    // construit un bloc de lignes SÉPARÉ (var_5EF8, même stride 101) avec ses propres
    // index de couleur 0x18 (@0x65e60b/0x65e659/0x65e6a7), des libellés 005.DAT
    // 0xAF5+ (@0x65e5d7/0x65e625/0x65e673/0x65e6c1) au format "%s : " (0x7BAD10),
    // ancré à var_10 = x - cols*13 (@0x65e5b5-0x65e5ce, MÊME formule que le panneau
    // principal => collé à sa GAUCHE) et var_4 = y (@0x65e5d1). Non transposé : le
    // contenu exact des lignes (au-delà des 4 premiers ids) n'est pas relevé.

    // TODO [ancre 0x65BE45-0x65C72F] — SOUS-PANNEAU GEMMES non émis (gap TT-08b).
    // Grille de 10 slots (var_740 0..9 @0x65beac), pas 0x37=55, centrée sur le sprite
    // unk_94B87C (slot 2735), cellules +0x13/+0x29 (@0x65bed0/0x65bef1). Décodage
    // réel de dword_167568C[i] (@0x65bf43-0x65bfc6) — ATTENTION, la formule est
    // inhabituelle et ne correspond PAS à un simple découpage décimal :
    //     g1 = (a / 0x989680 /*1e7*/) * 10                       -> var_6388
    //     g2 = ((a % 0xF4240 /*1e6*/) / 0x2710 /*1e4*/) * 10     -> var_6380
    //     g3 = ((a % 0x3E8  /*1e3*/) / 0xA) * 10                 -> var_6374
    //     switch (g1 - 0xA)  ; 71 cas ; `cmp …, 0x46`  (jpt_65BFE0 0x65ED1C)
    // puis Item_DecodeGemBonus (@0x65c07d, 0x65c0fc, 0x65c17b...). Le tableau source
    // (équivalent de dword_167568C) n'a par ailleurs pas d'homologue côté C++.
}

} // namespace

void DrawItemTooltip(const UiContext& ctx, int x, int y, uint32_t itemId,
                     uint32_t socketWord) {
    // MobDb_GetEntry(mITEM 0x8E71EC, arg_8) @0x652afa ; nullptr -> `jmp loc_65EA08`
    // @0x652b0e (sortie immédiate, rien n'est dessiné).
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info) return;

    // TODO [ancres 0x652B1C / 0x652B2D / 0x652B4B / 0x652B7C] — PROLOGUE DE DÉCODAGE
    // non transposé (gap TT-07). Le binaire décode `socketWord` (arg_10) sur les
    // QUATRE octets avant le switch :
    //     octet0 -> var_C    (Item_GetAttribByte0 0x545610 @0x652b1c, stocké @0x652b21)
    //     octet1 -> var_3388 (Item_GetAttribByte1 0x545640 @0x652b2d, stocké @0x652b32)
    //     octet2 -> var_594  (Item_GetAttribByte2 0x545670 @0x652b4b), avec le test
    //               dédié `cmp var_594, 19h / mov var_73C, 1` @0x652b60-0x652b69
    //     octet3 -> var_59C  (Item_GetAttribByte3 0x5456A0 @0x652b7c, stocké @0x652b81)
    // Les 4 accesseurs existent déjà côté C++ (Game/ItemSystem.h, Item_GetAttribByte0..3).
    // Ils ne sont PAS appelés ici parce que leurs SEULS lecteurs sont les cas du
    // switch principal laissés en TODO dans BuildLines() : les décoder maintenant
    // produirait quatre variables que personne ne consomme. À transposer EN MÊME
    // TEMPS que les cas qui les lisent.
    (void)socketWord;

    // TODO [ancre 0x652B93] — `Item_ClassifyRecord(unk_1685740, rec)` -> var_744
    // (catégorie 0..9) n'est PAS appelé : la fonction est bien transposée côté C++
    // mais reste INTERNE à Game/StatFormulas.cpp (l.40 ; aucune déclaration dans
    // Game/StatFormulas.h), et ce front ne possède pas ces fichiers. Câblage à faire
    // par leur propriétaire : exposer `int Item_ClassifyRecord(const ItemInfo*);`
    // dans Game/StatFormulas.h, puis l'appeler ici avant le switch. Même remarque que
    // ci-dessus : var_744 n'est lu que par des cas non encore transposés.

    // --- Contenu ---------------------------------------------------------------
    LineList lines;
    BuildLines(info, lines);
    const int lineCount = lines.Count();
    if (lineCount <= 0) return; // rien à dessiner (cas de typeCode non transposé)

    // --- Géométrie (EA 0x65E160-0x65E336) --------------------------------------
    // ATTENTION : var_5A8 est une longueur en CARACTÈRES (Crt_Strlen @0x65e1a4 /
    // @0x65e1c5), PAS une largeur en pixels — toute la géométrie du panneau est
    // dérivée du nombre de caractères de la ligne la plus longue.
    int maxLen = 0; // var_5A8, remis à 0 @0x65e160
    for (int i = 0; i < lineCount; ++i) {
        const int len = static_cast<int>(std::strlen(lines[i].text));
        if (len > maxLen) maxLen = len; // `cmp eax, var_5A8 / jle` @0x65e1ac
    }

    // Clamp CONDITIONNEL à 33 caractères (@0x65E1D5-0x65E247) :
    //   category (+184) ∈ {3,4,5,6}  ET  (typeCode (+188) ∈ [7,0x15]  OU  == 0x1D)
    // -> maxLen = max(maxLen, 0x21). Sinon aucun plancher.
    {
        const uint32_t cat = info->category;
        const uint32_t tc  = info->typeCode;
        const bool catHit = (cat == 3 || cat == 4 || cat == 5 || cat == 6); // @0x65e1db-0x65e20f
        const bool tcHit  = ((tc >= 7 && tc <= 0x15) || tc == 0x1D);        // @0x65e217-0x65e23c
        if (catHit && tcHit && maxLen < 0x21) maxLen = 0x21;                // @0x65e23e-0x65e247
    }

    // cols = (maxLen - 1) / 2 + 1  (division SIGNÉE : `sub eax,1 / cdq / sub eax,edx
    // / sar eax,1 / add eax,1` @0x65e251-0x65e25f) ; rows = lineCount + 2 (@0x65e273).
    const int cols = (maxLen - 1) / 2 + 1;
    const int rows = lineCount + 2;

    // Ancrage : le panneau est posé À GAUCHE du curseur (largeur PLEINE retranchée,
    // pas la moitié) et CENTRÉ VERTICALEMENT sur lui. Aucun clamp écran dans le
    // binaire — l'infobulle peut déborder, on ne « corrige » pas.
    const int tx = x - cols * kTileW;              // `imul eax,0Dh / sub edx,eax` @0x65e262-0x65e26a
    const int ty = y - (rows * kTileH) / 2;        // `imul eax,0Fh / cdq / sar / sub ecx,eax` @0x65e276-0x65e283

    // --- Phase Panels : tuilage du fond (EA 0x65E286-0x65E30A) ------------------
    if (ctx.phase == UiPhase::Panels) {
        // Le binaire ne dessine JAMAIS de rectangle plein ici : double boucle de
        // blits NON étirés de la MÊME tuile 13x15.
        static const PanelSkin s_bg(PanelSkin::Cat1Slot{kTooltipBgSlot});
        if (s_bg.TexW(ctx) != 0) {
            for (int r = 0; r < rows; ++r)                                   // @0x65e2a1-0x65e2b0
                for (int c = 0; c < cols; ++c)                               // @0x65e2cd-0x65e2e4
                    s_bg.Draw(ctx, tx + c * kTileW, ty + r * kTileH);        // @0x65e2e6-0x65e305
        } else {
            // Repli hors binaire (atlas absent) : un seul rect de la taille exacte
            // qu'aurait couverte la mosaïque.
            ctx.FillRect(tx, ty, cols * kTileW, rows * kTileH, kColBgFallback);
        }
        return;
    }

    // --- Phase Text : lignes CENTRÉES (EA 0x65E30E-0x65E3C5, avance @0x65E584) ---
    // centerX = tx + (cols*13)/2  (`imul 0Dh / cdq / sar / add arg_0` @0x65e30e-0x65e32a)
    const int centerX = tx + (cols * kTileW) / 2;
    int lineY = ty + kTextTopPad; // var_3778 = arg_4 + 0x10 @0x65e330-0x65e336

    for (int i = 0; i < lineCount; ++i) {
        const TooltipLine& l = lines[i];

        // x = centerX - largeur/2 (@0x65e394-0x65e3a7). Le binaire mesure avec la
        // police bitmap numérique (UI_MeasureNumberText 0x53FCA0, this = unk_1685740)
        // et dessine via UI_DrawNumberValue 0x53FCC0 -> UI_DrawText(g_GfxRenderer, s,
        // x, y, color, 2). Ici on passe par ctx.MeasureText/ctx.Text (ID3DXFont) :
        // divergence de POLICE et de style (2 = ombré/contour vs mode normal) déjà
        // documentée pour tout le shell, pas propre à ce fichier.
        const int lx = centerX - ctx.MeasureText(l.text) / 2;

        // Couleur = ColorTable_GetColor(dword_84DF20, idx) @0x53fcd2 — c'est-à-dire
        // la palette mFONTCOLOR (45 entrées, index 1-based, 0xFF000000 hors bornes).
        const D3DCOLOR col = static_cast<D3DCOLOR>(game::g_Strings.colors.Get(l.colorIdx));

        ctx.Text(l.text, lx, lineY, col);
        lineY += kLineAdv; // `add eax, 0Fh` @0x65e584
    }
}

void DrawItemTooltip(const UiContext& ctx, int x, int y, const game::InvCell& cell) {
    if (cell.empty()) return;
    // InvCell::color EST le mot d'attributs (cf. Net/ItemActionDispatch.cpp :
    // `eq.socket = cell.color;`).
    DrawItemTooltip(ctx, x, y, cell.itemId, cell.color);
}

} // namespace ts2::ui

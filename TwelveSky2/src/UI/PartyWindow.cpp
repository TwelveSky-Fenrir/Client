// UI/PartyWindow.cpp — implémentation du panneau HUD « Groupe ».
// Voir UI/PartyWindow.h pour le contrat et les réserves sur les données affichées
// (noms de membres et PM des coéquipiers non modélisés côté handler réseau).
#include "UI/PartyWindow.h"
#include "UI/PanelSkin.h"

#include <cstdio>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit étroit/haut (252,440), le PLUS
// répété (63 occurrences non consécutives) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, retenu par
// défaut pour ce panneau HUD étroit (210 px de large, hauteur dynamique selon
// le nombre de membres ; cf. méthodologie détaillée dans UI/PanelSkin.h).
// Repli automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00472.IMG");

// Formatage sans allocation dynamique excessive (snprintf -> std::string).
std::string Fmt(const char* fmt, size_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, v);
    return std::string(buf);
}
} // namespace

PartyWindow::Layout PartyWindow::BuildLayout(int screenW, int screenH) const {
    Layout L;
    (void)screenH;

    auto& client = game::g_Client;
    auto& world  = game::g_World;

    // Garde de visibilité : dword_184BE40 (« groupe actif »), LU comme garde par le
    // handler PartyMemberValueSet (opcode 0x3f, Net/GameHandlers_PartyGuild.cpp).
    // Groupe inactif ou aucune entité joueur connue -> panneau entièrement masqué.
    const bool partyActive = client.VarGet(kVarPartyActive) != 0;
    if (!partyActive || world.players.empty()) return L; // L.visible reste false

    struct RowSrc {
        std::string name;
        int hp = 0, hpMax = 0;
        int mp = 0, mpMax = 0;
        bool hasMp = false;
    };
    std::vector<RowSrc> src;
    src.reserve(kMaxRows);

    // --- Soi (toujours en tête si présent, source réelle SelfState -> StatEngine) ---
    if (world.players[0].active) {
        RowSrc r;
        r.name   = "Moi";
        r.hp     = world.self.hp;
        r.hpMax  = world.self.maxHp;
        r.mp     = world.self.mp;
        r.mpMax  = world.self.maxMp;
        r.hasMp  = true;
        src.push_back(std::move(r));
    }

    // --- Autres membres : un slot du tableau joueurs est traité comme « membre de
    // groupe résolu » si PartyMemberHpSet/PartyMemberUpdate (opcodes 0x7f/0x80) a
    // déjà écrit une PV MAX non nulle pour ce slot. Ces deux opcodes sont émis par
    // le serveur UNIQUEMENT pour de véritables membres de groupe (contrairement à
    // world.players[], qui contient TOUT joueur visible à proximité) — c'est donc
    // le meilleur signal disponible pour ne pas afficher des inconnus. ---
    for (size_t i = 1; i < world.players.size() && src.size() < static_cast<size_t>(kMaxRows); ++i) {
        if (!world.players[i].active) continue;
        const uint32_t addr = static_cast<uint32_t>(kMemberStride * i);
        const int hpMax = client.VarGet(kVarMemberHpMaxBase + addr);
        if (hpMax <= 0) continue; // aucune donnée de groupe reçue pour ce slot

        RowSrc r;
        // Nom réel : g_PartyRosterNames (game::g_World.partyRoster.names), peuplé par
        // Net_OnPartyMemberNameSet/_Clear (opcodes 0x3e/0x40, Net/GameHandlers_PartyGuild.cpp).
        // ATTENTION (best-effort, cf. Game/GameState.h::PartyRoster) : `i` est ici un index
        // d'ENTITÉ (world.players, résolu par identité réseau via PartyMemberHpSet/Update),
        // alors que le roster de noms est indexé par un slot ASSIGNÉ PAR LE SERVEUR sans lien
        // prouvé avec l'index d'entité. On lit quand même names[i] en repli le plus probable
        // (aucune clé de jointure connue dans le désassemblage) ; si ce slot est vide/pas
        // encore reçu, on retombe sur un libellé générique plutôt que d'inventer un nom.
        const std::string& rosterName =
            (i < world.partyRoster.names.size()) ? world.partyRoster.names[i] : std::string();
        r.name = !rosterName.empty() ? rosterName : "Membre";
        r.hp    = client.VarGet(kVarMemberHpBase + addr);
        r.hpMax = hpMax;
        r.hasMp = false; // PartyMemberHpSet écrit la MÊME adresse pour kind==1 (PV)
                          // et kind==2 (PM) : aucune adresse distincte pour le PM
                          // des coéquipiers dans l'état actuel du handler.
        src.push_back(std::move(r));
    }

    if (src.empty()) return L; // groupe actif mais aucun membre résolu -> masqué

    // --- Géométrie (ancré haut-gauche, indépendant de la résolution écran) ---
    L.visible = true;
    L.x = kMarginX;
    L.y = kMarginY;
    L.w = kPanelW;

    const int barW  = kPanelW - 2 * kPadX;
    const int rowH  = kNameH + kBarGapY + kBarH + kBarGapY + kBarH + kRowGapY;
    int ty = kPadY + kTitleH + 4;

    for (const auto& s : src) {
        RowLayout rl;
        rl.name   = s.name;
        rl.nameY  = L.y + ty;
        rl.hp     = s.hp;
        rl.hpMax  = s.hpMax;
        rl.mp     = s.mp;
        rl.mpMax  = s.mpMax;
        rl.hasMp  = s.hasMp;

        rl.hpX = L.x + kPadX; rl.hpY = L.y + ty + kNameH + kBarGapY; rl.hpW = barW; rl.hpH = kBarH;
        rl.mpX = rl.hpX;      rl.mpY = rl.hpY + kBarH + kBarGapY;    rl.mpW = barW; rl.mpH = kBarH;

        L.rows.push_back(std::move(rl));
        ty += rowH;
    }

    L.h = ty - kRowGapY + kPadY;
    (void)screenW;
    return L;
}

bool PartyWindow::OnMouseDown(int x, int y) {
    // Consomme uniquement si le clic tombe SUR le panneau actuellement dessiné
    // (évite le clic-traversant vers le monde 3D sous ce HUD). Pas de bouton :
    // aucune autre action (panneau d'information pure, lecture seule).
    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

bool PartyWindow::OnClick(int x, int y) {
    if (!lastVisible_) return false;
    return PointInRect(x, y, lastX_, lastY_, lastW_, lastH_);
}

void PartyWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    (void)cursorX; (void)cursorY;

    const Layout L = BuildLayout(ctx.screenW, ctx.screenH);

    // bOpen_/x_/y_ (champs protégés de Dialog) reflètent l'état auto-masqué,
    // recalculé à CHAQUE Render (les deux phases donnent le même résultat dans la
    // même frame) -> conforme à « toujours visible si le groupe a des membres ».
    bOpen_ = L.visible;
    x_ = L.x;
    y_ = L.y;

    lastVisible_ = L.visible;
    lastX_ = L.x; lastY_ = L.y; lastW_ = L.w; lastH_ = L.h;

    if (!L.visible) return;

    auto BarFill = [](int cur, int max) -> float {
        if (max <= 0) return 0.0f;
        float t = static_cast<float>(cur) / static_cast<float>(max);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return t;
    };

    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, L.x, L.y, L.w, L.h, kColBg);
        ctx.DrawFrame(L.x, L.y, L.w, L.h, kColBorder, 1);

        for (const auto& r : L.rows) {
            // Barre PV (toujours des données réelles pour les lignes affichées).
            ctx.FillRect(r.hpX, r.hpY, r.hpW, r.hpH, kColHpBg);
            const int hpFillW = static_cast<int>(r.hpW * BarFill(r.hp, r.hpMax));
            if (hpFillW > 0) ctx.FillRect(r.hpX, r.hpY, hpFillW, r.hpH, kColHpFill);
            ctx.DrawFrame(r.hpX, r.hpY, r.hpW, r.hpH, kColBorder, 1);

            // Barre PM : grisée/vide si aucune donnée réelle (cf. bandeau .h).
            if (r.hasMp) {
                ctx.FillRect(r.mpX, r.mpY, r.mpW, r.mpH, kColMpBg);
                const int mpFillW = static_cast<int>(r.mpW * BarFill(r.mp, r.mpMax));
                if (mpFillW > 0) ctx.FillRect(r.mpX, r.mpY, mpFillW, r.mpH, kColMpFill);
            } else {
                ctx.FillRect(r.mpX, r.mpY, r.mpW, r.mpH, kColNoData);
            }
            ctx.DrawFrame(r.mpX, r.mpY, r.mpW, r.mpH, kColBorder, 1);
        }
        return;
    }

    // Phase texte.
    const char* title = "Groupe";
    ctx.Text(title, L.x + (L.w - ctx.MeasureText(title)) / 2, L.y + kPadY, kColTitle);

    for (const auto& r : L.rows) {
        ctx.Text(r.name.c_str(), L.x + kPadX, r.nameY, kColText);
    }
}

} // namespace ts2::ui

// UI/BuffStatusPanel.cpp — implémentation de la grille de buffs (§9) et du panneau
// de statut bas-droite (§16). Voir UI/BuffStatusPanel.h pour le périmètre, les
// simplifications assumées et la méthode de résolution des icônes.
#include "UI/BuffStatusPanel.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h" // game::g_Client.VarGet (mission "CABLAGE GRILLE DE BUFFS", 2026-07-14)
#include "Asset/ImgFile.h"
#include "Core/Types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ts2::ui {

namespace {

// Gabarit de chemin identique à celui utilisé par GameHud.cpp (kVitalsFrameImgPath) :
// catégorie 1 de la table Sprite2D partagée (base unk_8E8B50, cf. bandeau de tête de
// GameHud.cpp pour la méthode de déduction complète). %05d = numéro de fichier
// (index table + 1).
std::string GxdCategory1Path(int fileNo) {
    if (fileNo <= 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\001\\001_%05d.IMG", fileNo);
    return std::string(buf);
}

// Table des icônes RÉELLEMENT résolues pour la grille §9 (voir bandeau de tête du
// header pour la méthode). Indexée par BuffIconId. Chaque `fileNo` a été calculé par
// `(adresse_unk - 0x8E8B50) / 148 + 1`, division vérifiée exacte pour les 33 adresses
// ci-dessous (33, pas 34 : kBuffKnownIconCount compte aussi la borne finale de l'enum).
struct KnownIconDef { int fileNo; const char* symbol; const char* note; };
constexpr KnownIconDef kKnownIcons[kBuffKnownIconCount] = {
    { 3735, "unk_96FA08", "combo elementaire A" },          // kBuffComboA
    { 3737, "unk_96FB30", "combo elementaire B" },          // kBuffComboB
    { 3739, "unk_96FC58", "combo elementaire C" },          // kBuffComboC
    { 2820, "unk_94E90C", "etat elementaire 1" },           // kBuffElemState1
    { 2821, "unk_94E9A0", "etat elementaire 2" },           // kBuffElemState2
    { 2822, "unk_94EA34", "etat elementaire 3" },           // kBuffElemState3
    { 2823, "unk_94EAC8", "etat elementaire 4" },           // kBuffElemState4
    {  870, "unk_9081B4", "synergie paire elementaire" },   // kBuffElemPair
    { 3990, "unk_978D74", "loadout competences favorable" },// kBuffLoadoutGood
    { 3992, "unk_978E9C", "loadout competences defavorable" }, // kBuffLoadoutBad
    { 4165, "unk_97F2A0", "bonus rang (defaut)" },          // kBuffRankDefault
    { 4166, "unk_97F334", "bonus rang 1" },                 // kBuffRank1
    { 4167, "unk_97F3C8", "bonus rang 2" },                 // kBuffRank2
    { 4168, "unk_97F45C", "bonus rang 3" },                 // kBuffRank3
    { 4192, "unk_98023C", "gemme arme palier 1" },          // kBuffGem1
    { 4193, "unk_9802D0", "gemme arme palier 2" },          // kBuffGem2
    { 4194, "unk_980364", "gemme arme palier 3" },          // kBuffGem3
    { 4195, "unk_9803F8", "gemme arme palier 4" },          // kBuffGem4
    { 4231, "unk_9818C8", "drapeau simple A (dword_16756E0)" }, // kBuffFlagA
    { 4232, "unk_98195C", "drapeau simple B (dword_16756E4)" }, // kBuffFlagB
    { 4452, "unk_98988C", "drapeau simple C (dword_16758C4)" }, // kBuffFlagC
    {  871, "unk_908248", "harmonie elementaire" },         // kBuffHarmony
    {  872, "unk_9082DC", "mesentente elementaire" },       // kBuffMismatch
    { 3750, "unk_9702B4", "bonus divers 1 (dword_16862BC)" }, // kBuffMisc1
    {  874, "unk_908404", "bonus divers 2 (flt_1686070)" }, // kBuffMisc2
    {  875, "unk_908498", "bonus divers 3 (flt_1686080)" }, // kBuffMisc3
    {  876, "unk_90852C", "bonus divers 4 (flt_1686090)" }, // kBuffMisc4
    {  877, "unk_9085C0", "bonus divers 5 (dword_16860A0)" }, // kBuffMisc5
    { 3652, "unk_96CA0C", "bonus temporel serveur (==360)" }, // kBuffServerBonusMax
    { 3602, "unk_96AD24", "drapeau additionnel 1 (dword_1675704)" }, // kBuffFlagAdd1
    { 3225, "unk_95D330", "drapeau additionnel 2 (dword_16747BC)" }, // kBuffFlagAdd2
    { 3226, "unk_95D3C4", "drapeau additionnel 2 cumul (>6)" },      // kBuffFlagAdd3
    { 3840, "unk_9736BC", "drapeau additionnel 3 (dword_16757A4)" }, // kBuffFlagAdd4
    { 3929, "unk_976A30", "drapeau additionnel 4 (dword_1675878)" }, // kBuffFlagAdd5

    // --- Ajouts mission "CABLAGE GRILLE DE BUFFS" (2026-07-14) : icônes DYNAMIQUES,
    // fileNo = niveau/minute + constante -- même table Sprite2D partagée que ci-dessus,
    // formule vérifiée par la même méthode (division exacte par 148). Voir
    // CollectWiredConditionBuffs plus bas pour la lecture des globals correspondants.
    // Maîtrise élémentaire (§9.13) : frame = unk_8E8B50 + 148*g_ElementMastery + 521996,
    // soit fileNo = g_ElementMastery + 3528 (521996/148 = 3527 EXACT). Plage 1..7
    // confirmée par IDA (data_refs 0x1675680 : commentaire IDB d'origine "élément
    // maîtrisé 1..7 -> +1000 stat correspondante", PAS une estimation depuis la barre
    // /3000 de §1).
    { 3529, "unk_8E8B50+521996(niv1)", "maitrise elementaire niveau 1" }, // kBuffElemMastery1
    { 3530, "unk_8E8B50+521996(niv2)", "maitrise elementaire niveau 2" }, // kBuffElemMastery2
    { 3531, "unk_8E8B50+521996(niv3)", "maitrise elementaire niveau 3" }, // kBuffElemMastery3
    { 3532, "unk_8E8B50+521996(niv4)", "maitrise elementaire niveau 4" }, // kBuffElemMastery4
    { 3533, "unk_8E8B50+521996(niv5)", "maitrise elementaire niveau 5" }, // kBuffElemMastery5
    { 3534, "unk_8E8B50+521996(niv6)", "maitrise elementaire niveau 6" }, // kBuffElemMastery6
    { 3535, "unk_8E8B50+521996(niv7)", "maitrise elementaire niveau 7" }, // kBuffElemMastery7
    // Bonus temporel serveur par tranche de minute (§9.11, cas >=120 && !=360) :
    // frame = unk_8E8B50 + 148*(dword_1674AB0/60) + 483516, soit
    // fileNo = (dword_1674AB0/60) + 3268 (483516/148 = 3267 EXACT). 360s (palier max,
    // 6 min) reste couvert par kBuffServerBonusMax ci-dessus (icône FIXE distincte dans
    // le binaire, PAS cette table indexée par minute).
    { 3270, "unk_8E8B50+483516(2m)", "bonus temporel serveur 2 min" }, // kBuffServerBonusMin2
    { 3271, "unk_8E8B50+483516(3m)", "bonus temporel serveur 3 min" }, // kBuffServerBonusMin3
    { 3272, "unk_8E8B50+483516(4m)", "bonus temporel serveur 4 min" }, // kBuffServerBonusMin4
    { 3273, "unk_8E8B50+483516(5m)", "bonus temporel serveur 5 min" }, // kBuffServerBonusMin5
};
static_assert(sizeof(kKnownIcons) / sizeof(kKnownIcons[0]) == kBuffKnownIconCount,
              "kKnownIcons doit couvrir exactement BuffIconId");

// Icônes du panneau bas-droite §16 (état off/on des 4 icônes internes). Même méthode
// de résolution que ci-dessus.
struct StatusIconDef { int fileOff, fileOn; };
constexpr StatusIconDef kStatusIcons[4] = {
    { 2004, 2003 }, // this+176 : unk_93114C (off) / unk_9310B8 (on)
    { 2074, 2073 }, // this+180 : unk_9339C4 (off) / unk_933930 (on)
    { 2077, 2076 }, // this+184 : unk_933B80 (off) / unk_933AEC (on)
    { 2080, 2079 }, // this+188 : unk_933D3C (off) / unk_933CA8 (on)
};

// Palette de repli (« pastilles colorées ») pour les icônes de la grille hors table
// connue (id < 0 ou >= kBuffKnownIconCount — notamment les 36 debuffs à durée §9.10).
// Teintes pastel/saturées choisies pour rester lisibles en 24x24 sur fond sombre,
// dans l'esprit de la « pastille » demandée par la mission (même principe que le
// repli `ctx.FillRect` coloré de WarehouseWindow::Render pour ses cellules).
constexpr D3DCOLOR kPillPalette[] = {
    0xFFE05656u, 0xFF56A0E0u, 0xFF56D080u, 0xFFE0C256u, 0xFFB066E0u,
    0xFFE0863Cu, 0xFF3CC8C8u, 0xFFE056A8u, 0xFF9AD046u, 0xFF6E7CE0u,
};
constexpr size_t kPillPaletteCount = sizeof(kPillPalette) / sizeof(kPillPalette[0]);

D3DCOLOR PillColorForId(int buffId) {
    // Dérivation déterministe simple (pas de vraie sémantique par id, juste une
    // teinte stable pour distinguer visuellement les buffs entre eux).
    const unsigned h = static_cast<unsigned>(buffId) * 2654435761u; // Knuth multiplicative hash
    return kPillPalette[h % kPillPaletteCount];
}

// Crée une texture 1x1 opaque blanche (D3DPOOL_MANAGED : survit au device reset).
// Duplication assumée du même helper que GameHud.cpp — pattern déjà établi dans ce
// projet (pas de header commun pratique pour un si petit helper, cf. commentaire
// équivalent dans InventoryWindow.cpp au sujet de ResolveItemIconPath).
IDirect3DTexture9* CreateWhiteTexture(IDirect3DDevice9* dev) {
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)) || !tex) {
        return nullptr;
    }
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits) {
        *static_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
        tex->UnlockRect(0);
    }
    return tex;
}

} // namespace

// =============================================================================
// Init / Shutdown
// =============================================================================
bool BuffStatusPanel::Init(gfx::Renderer& renderer, gfx::Font* font) {
    Shutdown();
    device_ = renderer.Device();
    font_   = font;
    if (!device_) return false;
    if (!sprite_.Create(device_)) { device_ = nullptr; return false; }

    white_ = CreateWhiteTexture(device_);
    if (!white_) { Shutdown(); return false; }

    // Cadre §16 (best-effort ; repli sur rect coloré dans RenderStatusPanel si absent).
    {
        asset::ImgFile img;
        const std::string path = GxdCategory1Path(kStatusFrameFile);
        if (img.Load(path) && img.Kind() == asset::ImgKind::TextureDxt)
            statusFrameTex_.CreateFromImgFile(device_, img);
    }

    SetScreenSize(ts2::kRefWidth, ts2::kRefHeight);
    return true;
}

void BuffStatusPanel::Shutdown() {
    gridIconCache_.clear();
    panelIconCache_.clear();
    statusFrameTex_.Release();
    if (white_) { white_->Release(); white_ = nullptr; }
    sprite_.Destroy();
    device_ = nullptr;
    font_   = nullptr;
}

void BuffStatusPanel::SetScreenSize(int width, int height) {
    screenW_ = (width  > 0) ? width  : ts2::kRefWidth;
    screenH_ = (height > 0) ? height : ts2::kRefHeight;
}

// =============================================================================
// Primitives
// =============================================================================
void BuffStatusPanel::DrawFilledRect(int x, int y, int w, int h, D3DCOLOR color) {
    if (!white_ || w <= 0 || h <= 0) return;
    RECT src{ 0, 0, 1, 1 };
    sprite_.DrawSpriteScaled(white_, &src, x, y, static_cast<float>(w), static_cast<float>(h),
                             color, /*compensatePos=*/true);
}

void BuffStatusPanel::DrawBorder(int x, int y, int w, int h, int t, D3DCOLOR color) {
    if (t <= 0) return;
    DrawFilledRect(x,         y,         w, t, color);
    DrawFilledRect(x,         y + h - t, w, t, color);
    DrawFilledRect(x,         y,         t, h, color);
    DrawFilledRect(x + w - t, y,         t, h, color);
}

// =============================================================================
// Icônes
// =============================================================================
gfx::GpuTexture* BuffStatusPanel::GetGridIconTex(int buffId) {
    if (buffId < 0 || buffId >= kBuffKnownIconCount) return nullptr; // hors table -> pastille

    auto it = gridIconCache_.find(buffId);
    if (it != gridIconCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    if (device_) {
        const std::string path = GxdCategory1Path(kKnownIcons[buffId].fileNo);
        asset::ImgFile img;
        if (!path.empty() && img.Load(path))
            tex.CreateFromImgFile(device_, img);
    }
    auto res = gridIconCache_.emplace(buffId, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

gfx::GpuTexture* BuffStatusPanel::GetPanelIconTex(int fileNo) {
    if (fileNo <= 0) return nullptr;

    auto it = panelIconCache_.find(fileNo);
    if (it != panelIconCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    if (device_) {
        const std::string path = GxdCategory1Path(fileNo);
        asset::ImgFile img;
        if (img.Load(path))
            tex.CreateFromImgFile(device_, img);
    }
    auto res = panelIconCache_.emplace(fileNo, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

void BuffStatusPanel::DrawGridIcon(int buffId, int x, int y, int size) {
    gfx::GpuTexture* tex = GetGridIconTex(buffId);
    if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0) {
        const float sx = static_cast<float>(size) / static_cast<float>(tex->Width());
        const float sy = static_cast<float>(size) / static_cast<float>(tex->Height());
        sprite_.DrawSpriteScaled(tex->Handle(), nullptr, x, y, sx, sy, gfx::kSpriteWhite, true);
        return;
    }
    // Repli « pastille colorée » (icône .IMG non résolue pour cet id).
    DrawFilledRect(x, y, size, size, PillColorForId(buffId));
    DrawBorder(x, y, size, size, 1, 0xFF000000u);
}

// =============================================================================
// Sources de données RÉELLEMENT modélisées pour la grille §9 (mission "CABLAGE
// GRILLE DE BUFFS", 2026-07-14)
// =============================================================================
// Le doc (Docs/TS2_UI_GAMEHUD_RENDER.md §9) catalogue ~50 conditions de déclenchement
// distinctes (points 1-14). Chacune lit un ou plusieurs globals `dword_XXXXXXX`. Cette
// passe a vérifié, PAR ADRESSE, laquelle de ces globals a un écrivain réel côté
// ClientSource (donc une valeur qui peut effectivement devenir non nulle en jeu) :
//
//   MÉTHODE : Grep de chaque adresse citée par le doc à travers ClientSource/, puis pour
//   les cas ambigus (adresse réutilisée par plusieurs systèmes), vérification croisée
//   par decompile()/data_refs() direct sur l'IDB (idaTs2 MCP, http://127.0.0.1:13337).
//
//   CABLÉES ci-dessous (source confirmée, écrivain identifié, PAS de conflit sémantique
//   avec un autre système déjà modélisé) :
//     §9.5  dword_167564C  -- Net/GameVarDispatch.cpp case 128 (écriture directe,
//           aucun autre écrivain). Condition originale : `%10000 > 0`, icône selon
//           `/10000` (0=défaut, 1/2/3=paliers).
//     §9.7  dword_16756E0, dword_16756E4 -- GameVarDispatch.cpp cases 76/77. Simple
//           drapeau `> 0`.
//     §9.11 dword_1674AB0 -- GameVarDispatch.cpp case 55 (branche `value != -1`).
//           `==360` -> kBuffServerBonusMax ; `>=120` (et !=360) -> palier/minute.
//     §9.13 g_ElementMastery (0x1675680) -- GameVarDispatch.cpp case 68 (écrit la
//           valeur 1..7 telle quelle) + reset case 68/WE_ResetElementMastery. Confirmé
//           par IDA (data_refs) : commentaire IDB d'origine "élément maîtrisé 1..7".
//     §9.14 dword_1675704 -- GameVarDispatch.cpp case 81. dword_16747BC -- écrit par
//           CharStatDeltaDispatch.cpp (`SV(0x16747BC)`, alias de g_Client.Var) ET
//           documenté comme `rebirthTier` dans Game/SkillCombat.h. dword_1675878 --
//           GameVarDispatch.cpp case 109.
//     §9.3  branche ÉGALITÉ SEULE de "g_LocalElement == dword_1685E08 ou paire via
//           Char_GetPairedElement" -- les DEUX opérandes sont modélisés :
//           `game::g_World.self.element` (g_LocalElement, SelfState) et dword_1685E08
//           (`g_Client.VarGet`, écrit "inconditionnellement" par le sous-cas 38 de
//           Net/WorldEntityDispatch.cpp comme "classe reçue"). PRÉCÉDENT DANS CE MÊME
//           CODEBASE : Net/GameVarDispatch.cpp case 106 fait DÉJÀ exactement cette
//           comparaison (`g_World.self.element != g_Client.VarGet(0x1685E08)`) --
//           confirmation indépendante que cette lecture est un pattern établi, pas une
//           invention de cette mission. La branche `Char_GetPairedElement` (fonction
//           NON portée) N'EST PAS couverte -> ce câblage est un sous-ensemble
//           STRICTEMENT PLUS PRUDENT que l'original (peut manquer un vrai positif rare,
//           ne peut jamais en fabriquer un faux). ⚠️ dword_1685E08 est AUSSI la cible
//           d'un gros memcpy 1324 o (ZoneChangeInfo::block1, Net/GameHandlers_BossWorld.cpp)
//           dans le binaire d'origine -- SANS CONSÉQUENCE ici : ce fichier écrit via
//           `g_Client.Blob(0x1685E08, ...)`, une table `unordered_map` SÉPARÉE de celle
//           de `g_Client.Var(0x1685E08)` (cf. Game/ClientRuntime.h) -- aucune collision
//           possible côté ClientSource, contrairement au binaire où c'était la même
//           mémoire physique.
//
//   ÉCARTÉES (adresse SANS écrivain confirmé, ou en conflit sémantique avéré avec un
//   autre système DÉJÀ modélisé sous une interprétation différente et mieux établie --
//   câbler quand même aurait fabriqué un signal trompeur, ce que ce projet interdit) :
//     §9.1  dword_184C218 (combos élémentaires) -- vérifié par data_refs() IDA : CE
//           MÊME tableau est déjà modélisé DEUX FOIS ailleurs sous des sémantiques
//           différentes et mieux établies (Game/SocialSystem.h::AchievementState, 24
//           flags d'exploit écrits par Net_OnAchievementDataLoad/Notice ; Net/
//           WorldEntityDispatch.cpp::kRankTable1, table de titres/rang [0..11] écrite
//           par le sous-dispatch 752/753) -- la lecture "combo/100, combo%100" du doc
//           HUD n'est qu'une hypothèse non confirmée par les string tables (le doc le
//           dit lui-même). Câbler une 3e interprétation concurrente sur la même adresse
//           aurait affiché un signal incohérent avec les deux modèles déjà écrits.
//     §9.2  dword_16860B0[elem] (état élémentaire local) -- WorldEntityDispatch.cpp
//           (sous-cas 45) ne fait que le RÉINITIALISER à 0 ; aucun sous-cas ne lui
//           écrit de valeur non nulle dans ce qui est reversé à ce jour -> la condition
//           ne peut jamais se déclencher avec les données actuellement modélisées.
//     §9.4, §9.6, §9.8, §9.9 -- dépendent de fonctions non portées
//           (Char_CompareSkillLoadout, Item_ClassifyById + sockets d'arme,
//           Char_GetElementHarmonyBonus/MismatchPenalty) ou de globals sans écrivain
//           trouvé (dword_16862BC, flt_1686070/80/90, dword_16860A0) -- aucune n'a de
//           source de données côté ClientSource. (§9.3 a une branche partiellement
//           câblée ci-dessus -- seule la partie dépendant de Char_GetPairedElement,
//           NON portée, reste hors périmètre.)
//     §9.10 banque de 36 debuffs à durée (dword_16758D8) -- l'adresse EST modélisée,
//           mais sous une AUTRE forme (SelfState::zoneState, blob brut 288 o de
//           Pkt_EnterWorld, jamais décodé champ par champ) -- pas un vecteur de 36
//           {id,durée} exploitable tel quel sans décoder ce blob, hors périmètre ici.
//     §9.12 buff nourriture morph (dword_168744C==1 && dword_1687450<=4) --
//           data_refs()/relecture de Net/WorldEntityDispatch.cpp montre que
//           dword_1687450 (`kDuelOpponentVal`) est en réalité écrit par le sous-système
//           DUEL/DÉFI (sous-cas 102/107/112/114, "code opposant/résultat") -- une
//           sémantique confirmée par RE ligne-à-ligne, largement plus fiable que le
//           libellé "buff nourriture morph" du doc HUD (une hypothèse non confirmée,
//           posée sans accès aux string tables). Câbler la lecture `<=4` du doc aurait
//           affiché une icône de "buff nourriture" pendant un duel -- signal FAUX.
//     dword_16758C4 (kBuffFlagC), dword_16757A4 (kBuffFlagAdd4) -- greppés dans tout
//           ClientSource/, AUCUN écrivain trouvé (uniquement cités dans ce fichier) :
//           resteraient à 0 en permanence, rien à câbler.
void BuffStatusPanel::CollectWiredConditionBuffs(std::vector<game::ActiveBuff>& out) const {
    using game::g_Client;

    // §9.3 synergie de paire élémentaire -- branche égalité seule (cf. bandeau
    // ci-dessus : Char_GetPairedElement non porté, sous-ensemble prudent). Accompagné
    // d'un texte fixe "10%" (PAS une valeur lue -- littéral du binaire, cf. doc §9.3),
    // géré comme cas spécial dans RenderGrid (ActiveBuff ne porte pas de champ légende).
    // GARDE `elemLastClass != 0` INDISPENSABLE : `game::g_Client.VarGet` ET
    // `SelfState::element` renvoient tous deux 0 par défaut (avant tout paquet reçu) --
    // sans cette garde, un client qui vient de démarrer afficherait FAUSSEMENT l'icône
    // (0==0) avant même d'avoir reçu le moindre paquet de classe élémentaire. Ce garde
    // introduit un faux négatif pour la classe légitime 0 (indiscernable de "jamais
    // reçu" avec les données actuellement modélisées) mais JAMAIS de faux positif --
    // conforme à la politique "sous-ensemble prudent" de ce fichier.
    {
        const int32_t elemLastClass = g_Client.VarGet(0x1685E08);
        if (elemLastClass != 0 && game::g_World.self.element == elemLastClass)
            out.push_back({ kBuffElemPair, 0.0f });
    }

    // §9.5 (EA ~0x67C6xx, doc §9 point 5) : `dword_167564C % 10000 > 0` -> icône selon
    // le quotient `/10000` (0=défaut, 1/2/3=paliers, >=4 clampé sur le palier 3, pas de
    // 4e icône dans le désassemblage).
    {
        const int32_t v = g_Client.VarGet(0x167564C);
        if (v % 10000 > 0) {
            const int tier = v / 10000;
            int id = kBuffRankDefault;
            if (tier == 1) id = kBuffRank1;
            else if (tier == 2) id = kBuffRank2;
            else if (tier >= 3) id = kBuffRank3;
            out.push_back({ id, 0.0f });
        }
    }

    // §9.7 drapeaux simples (icône seule si `> 0`, PAS de kBuffFlagC : dword_16758C4
    // sans écrivain, cf. bandeau ci-dessus).
    if (g_Client.VarGet(0x16756E0) > 0) out.push_back({ kBuffFlagA, 0.0f });
    if (g_Client.VarGet(0x16756E4) > 0) out.push_back({ kBuffFlagB, 0.0f });

    // §9.11 bonus temporel serveur : `==360` -> icône max fixe ; `>=120` (et !=360) ->
    // icône par tranche de minute pleine (`dword_1674AB0/60`, paliers 2..5 -- 6 min
    // équivaut à 360 donc déjà couvert par le cas ==360 ci-dessus).
    {
        const int32_t v = g_Client.VarGet(0x1674AB0);
        if (v == 360) {
            out.push_back({ kBuffServerBonusMax, 0.0f });
        } else if (v >= 120) {
            const int minutes = std::clamp(v / 60, 2, 5);
            out.push_back({ kBuffServerBonusMin2 + (minutes - 2), 0.0f });
        }
    }

    // §9.13 maîtrise élémentaire : `g_ElementMastery > 0` -> icône de palier (plage
    // réelle 1..7, confirmée par IDA -- cf. bandeau ci-dessus). Toute valeur hors plage
    // (ne devrait pas arriver, mais défensif) est clampée plutôt qu'ignorée.
    {
        const int32_t mastery = g_Client.VarGet(0x1675680); // g_ElementMastery
        if (mastery > 0) {
            const int lvl = std::clamp(mastery, 1, 7);
            out.push_back({ kBuffElemMastery1 + (lvl - 1), 0.0f });
        }
    }

    // §9.14 drapeaux additionnels : dword_1675704==1 ; dword_16747BC dans (0,13), avec
    // icône CUMULÉE (2e icône en plus, pas en remplacement -- cf. doc : "2 icônes
    // cumulées") si en plus >6 ; dword_1675878>0. PAS de kBuffFlagAdd4
    // (dword_16757A4 sans écrivain, cf. bandeau ci-dessus).
    if (g_Client.VarGet(0x1675704) == 1) out.push_back({ kBuffFlagAdd1, 0.0f });
    {
        // Même adresse d'origine que Game/SkillCombat.h::CombatMorphState::rebirthTier
        // (struct locale séparée, PAS synchronisée avec g_Client.Var) -- on lit ici
        // directement le Var, seule instance effectivement écrite par le handler réseau
        // (Net/CharStatDeltaDispatch.cpp::SV, alias de g_Client.Var).
        const int32_t rebirth = g_Client.VarGet(0x16747BC);
        if (rebirth > 0 && rebirth < 13) {
            out.push_back({ kBuffFlagAdd2, 0.0f });
            if (rebirth > 6) out.push_back({ kBuffFlagAdd3, 0.0f });
        }
    }
    if (g_Client.VarGet(0x1675878) > 0) out.push_back({ kBuffFlagAdd5, 0.0f });
}

std::vector<game::ActiveBuff> BuffStatusPanel::BuildLiveBuffList() const {
    std::vector<game::ActiveBuff> out = game::g_World.Self().buffs; // futures sources réseau/expiry
    CollectWiredConditionBuffs(out);
    return out;
}

// =============================================================================
// §9 — Grille de buffs/debuffs (7 colonnes)
// =============================================================================
void BuffStatusPanel::RenderGrid() {
    game::PlayerEntity& self = game::g_World.Self();
    const float now = game::g_World.gameTimeSec;

    // Purge des entrées expirées. Le binaire d'origine ne « stocke » jamais un buff
    // expiré (il RECALCULE la grille depuis les variables sources chaque frame) ;
    // notre modèle générique doit donc nettoyer explicitement ce que le binaire
    // n'aurait simplement plus dessiné.
    auto& buffs = self.buffs;
    buffs.erase(std::remove_if(buffs.begin(), buffs.end(),
                    [now](const game::ActiveBuff& b) {
                        return b.expiryTime > 0.0f && now >= b.expiryTime;
                    }),
                buffs.end());

    // self.buffs (purgé ci-dessus) + conditions §9 câblées cette mission (mission
    // "CABLAGE GRILLE DE BUFFS", 2026-07-14 -- cf. bandeau de CollectWiredConditionBuffs
    // pour le détail par adresse). Ces dernières n'ont pas d'expiryTime connue (le
    // binaire ne dessine qu'une icône `> 0`, pas de compte à rebours pour elles) donc
    // aucun texte de durée ne leur est associé ci-dessous (remaining reste -1).
    const std::vector<game::ActiveBuff> liveList = BuildLiveBuffList();

    const int count = std::min<int>(static_cast<int>(liveList.size()), kGridMaxIcons);
    for (int j = 0; j < count; ++j) {
        const game::ActiveBuff& b = liveList[static_cast<size_t>(j)];
        const int col = j % kGridCols;
        const int row = j / kGridCols;
        const int x = kGridX + col * kIconPitch;
        const int y = kGridY + row * kIconPitch;

        float remaining = -1.0f;
        if (b.expiryTime > 0.0f) remaining = b.expiryTime - now;

        // Clignotement (doc §9.10) : icône dessinée une frame sur deux quand il
        // reste <= 10 s (`Crt_ftol(g_GameTimeSec*2)%2==1`).
        const bool nearExpiry = remaining >= 0.0f && remaining <= 10.0f;
        const bool skipDraw   = nearExpiry && (static_cast<int>(now * 2.0f) % 2 == 1);
        if (!skipDraw)
            DrawGridIcon(b.id, x, y, kIconSize);

        // Timer de durée restante, en texte, si l'état le permet (durée finie).
        if (remaining >= 0.0f) {
            char buf[16];
            const int secs = static_cast<int>(std::ceil(remaining));
            std::snprintf(buf, sizeof(buf), "%d", secs);
            const D3DCOLOR col2 = nearExpiry ? 0xFFFF6060u : 0xFFFFFFFFu;
            pendingText_.push_back({ x, y + kIconSize - 11, buf, col2 });
        }
    }
}

// =============================================================================
// §16 — Panneau de statut bas-droite (4 icônes + indicateur de cast)
// =============================================================================
void BuffStatusPanel::RenderStatusPanel() {
    const bool hasFrame = statusFrameTex_.Valid();
    const int frameW = hasFrame ? static_cast<int>(statusFrameTex_.Width())  : kStatusFallbackW;
    const int frameH = hasFrame ? static_cast<int>(statusFrameTex_.Height()) : kStatusFallbackH;

    const int fx = screenW_ - frameW;
    const int fy = screenH_ - frameH;

    if (hasFrame) {
        sprite_.DrawSprite(statusFrameTex_.Handle(), nullptr, fx, fy);
    } else {
        DrawFilledRect(fx, fy, frameW, frameH, 0xC0181820u);
        DrawBorder(fx, fy, frameW, frameH, 1, 0xFF3A3A48u);
    }

    for (int i = 0; i < 4; ++i) {
        const int ix = fx + kStatusOffsets[i].dx;
        const int iy = fy + kStatusOffsets[i].dy;
        const int fileNo = statusFlags_[i] ? kStatusIcons[i].fileOn : kStatusIcons[i].fileOff;
        gfx::GpuTexture* tex = GetPanelIconTex(fileNo);
        if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0) {
            const float sx = static_cast<float>(kStatusIconSize) / static_cast<float>(tex->Width());
            const float sy = static_cast<float>(kStatusIconSize) / static_cast<float>(tex->Height());
            sprite_.DrawSpriteScaled(tex->Handle(), nullptr, ix, iy, sx, sy, gfx::kSpriteWhite, true);
        } else {
            DrawFilledRect(ix, iy, kStatusIconSize, kStatusIconSize,
                           statusFlags_[i] ? 0xFF4C8C4Cu : 0xFF3A3A46u);
            DrawBorder(ix, iy, kStatusIconSize, kStatusIconSize, 1, 0xFF000000u);
        }
    }

    // Indicateur de cast (§16) : superposé à la 4e position. Séquence d'icônes non
    // identifiée dans le désassemblage (seules la formule de déclenchement et le
    // cycle de frame le sont) -> pastille pulsante + numéro de frame en texte
    // (repli PERMANENT, cf. bandeau de tête du header).
    if (casting_) {
        const int ix = fx + kStatusOffsets[3].dx;
        const int iy = fy + kStatusOffsets[3].dy;
        const int frame = static_cast<int>(game::g_World.gameTimeSec * 16.0f) % 8; // Crt_ftol(t*16)%8
        const uint8_t alpha = static_cast<uint8_t>(128 + 127 * frame / 7); // pulsation 0..7 -> 128..255
        const D3DCOLOR castColor = (static_cast<D3DCOLOR>(alpha) << 24) | 0x00FFD060u;
        DrawFilledRect(ix, iy, kStatusIconSize, kStatusIconSize, castColor);
        DrawBorder(ix, iy, kStatusIconSize, kStatusIconSize, 1, 0xFFFFFFFFu);
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%d", frame);
        pendingText_.push_back({ ix + kStatusIconSize / 2 - 3, iy + kStatusIconSize / 2 - 6,
                                  buf, 0xFF000000u });
    }
}

// =============================================================================
// Render
// =============================================================================
void BuffStatusPanel::Render() {
    if (!device_ || !sprite_.Ready()) return;

    pendingText_.clear();
    if (SUCCEEDED(sprite_.Begin(D3DXSPRITE_ALPHABLEND))) {
        RenderGrid();
        RenderStatusPanel();
        sprite_.End();
    }

    if (font_ && font_->Ready() && !pendingText_.empty()) {
        font_->BeginBatch();
        for (const TextItem& t : pendingText_)
            font_->DrawTextStyled(t.text.c_str(), t.x, t.y, t.color, gfx::kStyleShadow);
        font_->EndBatch();
    }
}

// =============================================================================
// Hit-test
// =============================================================================
bool BuffStatusPanel::OnMouseDown(int x, int y) {
    // Grille §9 : zone rectangulaire englobant les lignes effectivement peuplées
    // (doc : « toutes les icônes de la grille sont cliquables » -> ouverture de
    // tooltip générique, non modélisée ici ; on reproduit seulement la consommation
    // de l'événement).
    const int count = std::min<int>(static_cast<int>(BuildLiveBuffList().size()), kGridMaxIcons);
    if (count > 0) {
        const int rows = (count + kGridCols - 1) / kGridCols;
        const int gridW = kGridCols * kIconPitch;
        const int gridH = rows * kIconPitch;
        if (x >= kGridX && x < kGridX + gridW && y >= kGridY && y < kGridY + gridH)
            return true;
    }

    // Panneau bas-droite §16.
    const bool hasFrame = statusFrameTex_.Valid();
    const int frameW = hasFrame ? static_cast<int>(statusFrameTex_.Width())  : kStatusFallbackW;
    const int frameH = hasFrame ? static_cast<int>(statusFrameTex_.Height()) : kStatusFallbackH;
    const int fx = screenW_ - frameW;
    const int fy = screenH_ - frameH;
    if (x >= fx && x < fx + frameW && y >= fy && y < fy + frameH)
        return true;

    return false;
}

// =============================================================================
// Hooks §16
// =============================================================================
void BuffStatusPanel::SetStatusFlag(int index, bool active) {
    if (index < 0 || index >= 4) return;
    statusFlags_[static_cast<size_t>(index)] = active;
}

// =============================================================================
// Device lost/reset
// =============================================================================
void BuffStatusPanel::OnDeviceLost() {
    sprite_.OnLostDevice();
    // white_/statusFrameTex_ sont en D3DPOOL_MANAGED : rien à faire.
}

void BuffStatusPanel::OnDeviceReset() {
    sprite_.OnResetDevice();
}

} // namespace ts2::ui

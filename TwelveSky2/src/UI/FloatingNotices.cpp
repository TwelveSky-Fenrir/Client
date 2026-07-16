// UI/FloatingNotices.cpp — implémentation des notices flottantes du HUD.
// Voir UI/FloatingNotices.h pour le layout prouvé de dword_1821D58, le périmètre et
// les déviations assumées.
#include "Gfx/SpriteBatch.h"     // gfx::SpriteBatch / kSpriteWhite (Sprite2D_Draw 0x4D6B20)
#include "Gfx/Font.h"            // gfx::Font (UI_MeasureText 0x69E680 / UI_DrawText 0x69E750)
#include "Gfx/GpuTexture.h"      // gfx::GpuTexture (définition de FloatingNotices::Gpu)
#include "Asset/ImgFile.h"       // asset::ImgFile (001_%05d.IMG)
#include "UI/FloatingNotices.h"
#include "Core/Types.h"          // ts2::kRefWidth / kRefHeight (référence 1024x768)
#include "Scene/SceneManager.h"  // ts2::g_SceneSubState 0x1676184 (garde @0x5AF4DA)
#include "Game/StringTables.h"   // game::g_Strings.colors (ColorTable_GetColor 0x4C1FE0)

#include <cstdio>

namespace ts2::ui {

namespace {

// Gabarit de chemin de la table Sprite2D PARTAGÉE de catégorie 1 (base
// g_AssetMgr_UiAtlasSlots 0x8E8B50, pas 148 o, remplie par AssetMgr_InitAllSlots
// 0x4DEB50). Identique à UI/BuffStatusPanel.cpp::GxdCategory1Path (file-local là-bas,
// d'où cette copie plutôt qu'une dépendance croisée entre deux widgets autonomes).
std::string GxdCategory1Path(int fileNo) {
    if (fileNo <= 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\001\\001_%05d.IMG", fileNo);
    return std::string(buf);
}

// Les deux sprites d'ancrage. ⚠️ Les littéraux 744 / 1027 de HUD_RenderFloatingMessages
// ne sont PAS des coordonnées : ce sont les INDEX de sprite passés en a2 à
// UI_ProjectSpriteToScreen 0x50F5D0, qui lit ses dimensions via
// Sprite2D_GetWidth/GetHeight(g_AssetMgr_UiAtlasSlots + 148*a2). Preuve arithmétique
// (division EXACTE, méthode établie — cf. bandeau de UI/BuffStatusPanel.h) :
//   0x8E8B50 + 148*744  = 0x903970 = unk_903970 dessiné @0x5AF5B4 (types 0..11)
//   0x8E8B50 + 148*1027 = 0x90DD0C = unk_90DD0C dessiné @0x5AFC83 (type 12)
// Le sprite d'ancrage EST donc le sprite dessiné. Fichier = "001_%05d.IMG", %05d = idx+1.
constexpr int kSpriteFileNo[2] = { 745, 1028 };

// Géométrie par type — relevée case par case dans le switch @0x5AF577 de
// HUD_RenderFloatingMessages 0x5AF4C0.
//   tex      : 0 = sprite idx 744, 1 = sprite idx 1027
//   designX/Y: arguments a3/a4 de UI_ProjectSpriteToScreen
//   spriteDy : dy du Sprite2D_Draw
//   textDy   : dy du UI_DrawNumberValue
//   textCx   : centrage horizontal -> x + textCx - largeur_texte/2
struct TypeLayout { int tex; int designX; int designY; int spriteDy; int textDy; int textCx; };
constexpr TypeLayout kLayout[FloatingNotices::kSlotCount] = {
    /* 0 */ { 0, 287, 56, 22, 26, 225 }, // @0x5AF59A / 0x5AF5B4 / 0x5AF5C4 / 0x5AF5CA
    /* 1 */ { 0, 287, 56,  0,  4, 225 }, // @0x5AF62C / 0x5AF643 / 0x5AF653 / 0x5AF659
    /* 2 */ { 0, 287, 56, 22, 26, 225 }, // idem case 0 (case 0/2 groupées @0x5AF577)
    /* 3 */ { 0, 287, 56, 44, 48, 225 }, // @0x5AF74D / 0x5AF767 / 0x5AF777 / 0x5AF77D
    /* 4 */ { 0, 287, 56, 66, 70, 225 }, // @0x5AF7DF / 0x5AF7F9 / 0x5AF809 / 0x5AF80F
    /* 5 */ { 0, 287, 56, 66, 70, 225 }, // idem case 4 (case 4/5 groupées)
    /* 6 */ { 0, 287, 56, 88, 92, 225 }, // @0x5AFAB9 / 0x5AFAD3 / 0x5AFAE3 / 0x5AFAE9
    /* 7 */ { 0, 287, 56, 88, 92, 225 }, // idem case 6 (case 6/7/8/9 groupées)
    /* 8 */ { 0, 287, 56, 88, 92, 225 },
    /* 9 */ { 0, 287, 56, 88, 92, 225 },
    /*10 */ { 0, 287, 56, 44, 48, 225 }, // idem case 3 (case 3/10 groupées)
    /*11 */ { 0, 287, 56,  0,  4, 225 }, // idem case 1 (case 1/11 groupées)
    /*12 */ { 1, 287, 78,  0, 16, 222 }, // @0x5AFC6C / 0x5AFC83 / 0x5AFC93 / 0x5AFC99
};

// Type 12 UNIQUEMENT : 2e ligne (text2_) @0x5AFCE5 (dy) / 0x5AFCEB (cx) / 0x5AFCFF
// (mesure) / 0x5AFD1E (dessin).
constexpr int kType12Line2Dy = 38;
constexpr int kType12Line2Cx = 222;

// Index de couleur littéral `1` passé à UI_DrawNumberValue 0x53FCC0 @0x5AF606.
// ⚠️ C'est un INDEX de palette, pas une couleur : UI_DrawNumberValue fait
// `ColorTable_GetColor(dword_84DF20, 1)` @0x53FCD2. ColorTable_InitPalette 0x4C1D60
// pose `this[1] = 0xFFFFFFFF` @0x4C1D73 -> blanc opaque. On résout ici via le MÊME
// accesseur (game::ColorPalette::Get == ColorTable_GetColor) plutôt qu'en dur.
constexpr int kNoticeColorIndex = 1;

// Extinctions mutuelles — switch @0x5AEF7A de HUD_ShowFloatingMessage. Chaque Show
// éteint les slots concurrents. Conversion offset écrit -> slot : (offset - 8) / 4.
// Sans cette table, des notices contradictoires coexistent à l'écran.
struct Extinction { int count; int slot[3]; };
constexpr Extinction kExtinct[FloatingNotices::kSlotCount] = {
    /* 0 */ { 2, {  2, 12, -1 } }, // +16 @0x5AEF84 ; +56 @0x5AEF8E
    /* 1 */ { 1, { 11, -1, -1 } }, // +52 @0x5AEFAD
    /* 2 */ { 2, {  0, 12, -1 } }, // +8  @0x5AEFCC ; +56 @0x5AEFD6
    /* 3 */ { 1, { 10, -1, -1 } }, // +48 @0x5AF04A
    /* 4 */ { 1, {  5, -1, -1 } }, // +28 @0x5AF0F8
    /* 5 */ { 1, {  4, -1, -1 } }, // +24 @0x5AF16C
    /* 6 */ { 3, {  7,  8,  9 } }, // +36 @0x5AF1E0 ; +40 @0x5AF1EA ; +44 @0x5AF1F4
    /* 7 */ { 3, {  6,  8,  9 } }, // +32 @0x5AF213 ; +40 @0x5AF21D ; +44 @0x5AF227
    /* 8 */ { 3, {  6,  7,  9 } }, // +32 @0x5AF246 ; +36 @0x5AF250 ; +44 @0x5AF25A
    /* 9 */ { 3, {  6,  7,  8 } }, // +32 @0x5AF279 ; +36 @0x5AF283 ; +40 @0x5AF28D
    /*10 */ { 1, {  3, -1, -1 } }, // +20 @0x5AF2AC
    /*11 */ { 1, {  1, -1, -1 } }, // +12 @0x5AF332
    /*12 */ { 2, {  0,  2, -1 } }, // +8  @0x5AF3A3 ; +16 @0x5AF3AD
};

} // namespace

// PIMPL : cache paresseux des deux sprites d'ancrage/dessin (cf. « NB INCLUSION »
// du header — garde <d3dx9.h> hors de UI/ChatWindow.h).
struct FloatingNotices::Gpu {
    gfx::GpuTexture tex[2];
    bool            tried[2] = { false, false };
};

FloatingNotices::FloatingNotices() : gpu_(new Gpu()) {}
FloatingNotices::~FloatingNotices() = default;

// Branche `else` de la garde de scène : RAZ des 13 slots @0x5AF4FA.
void FloatingNotices::ClearAll() {
    active_.fill(0);
}

// -----------------------------------------------------------------------------
// HUD_ShowFloatingMessage 0x5AEEC0.
void FloatingNotices::Show(int type, int subType, const std::string& text,
                           const std::string& text2) {
    // Garde SIGNÉE @0x5AEECD (`jl`) / @0x5AEED3 (`jle 0Ch`) : hors [0,12] -> saut au
    // `default` du switch, c.-à-d. retour sans rien faire (@0x5AEED5).
    if (type < 0 || type >= kSlotCount) return;

    (void)subType; // sélecteur de SON uniquement (sous-switch @0x5AEFF5..) — non reproduit, cf. bandeau du .h

    active_[static_cast<size_t>(type)] = 1;              // *(this + 4*type + 8) = 1  @0x5AEEE0

    // Crt_Memset(this + 1373, 0, 0x65) @0x5AEEF6 puis Crt_StringInit(this + 1373, arg_C)
    // @0x5AEF26 : la 2e ligne est réinitialisée INCONDITIONNELLEMENT à CHAQUE Show
    // (pas seulement pour le type 12) — la RAZ précède le switch de type.
    text2_ = text2.substr(0, static_cast<size_t>(kTextLen - 1));

    // Crt_StringInit(this + 101*type + 60, arg_8) @0x5AEF0B/@0x5AEF10 : tampon de
    // 101 o NUL inclus -> 100 caractères utiles.
    text_[static_cast<size_t>(type)] = text.substr(0, static_cast<size_t>(kTextLen - 1));

    // *(float *)(this + 4*type + 1476) = g_GameTimeSec @0x5AEF3A. L'original lit le
    // GLOBAL flt_815180 (pas un paramètre) : on fait de même via son miroir C++
    // gfx::g_GameTimeSec (Gfx/SpriteBatch.cpp L11 « flt_815180 / g_GameTimeSec »).
    // COHÉRENCE AVEC Render() VÉRIFIÉE : App.cpp L764-765 écrit `gfx::g_GameTimeSec` et
    // `game::g_World.gameTimeSec` depuis LE MÊME `gameClockSec_` sur deux lignes
    // adjacentes -> le `nowSec` reçu par Render() (issu de ChatWindow::Tick(
    // game::g_World.gameTimeSec), UI/GameHud.cpp L1283) porte la même valeur. Pas de
    // dérive possible entre l'horodatage et le test de durée de vie.
    ts_[static_cast<size_t>(type)] = gfx::g_GameTimeSec;

    // Type 12 seul : ts[12] += 20.0 (dbl_7A7368) @0x5AEF45/@0x5AEF60. `this+1524` EST
    // ts[12] (1476 + 4*12) -> horodatage POSTDATÉ, donc vie utile 30 s et non 10 s.
    if (type == 12) ts_[12] += kType12TimeBonus;

    // Extinctions mutuelles du switch @0x5AEF7A.
    const Extinction& ex = kExtinct[static_cast<size_t>(type)];
    for (int k = 0; k < ex.count; ++k) {
        active_[static_cast<size_t>(ex.slot[k])] = 0;
    }
}

// -----------------------------------------------------------------------------
// UI_ProjectSpriteToScreen 0x50F5D0 :
//   x = Crt_ftol((double)(screenW * (designX + W/2)) / refW) - W/2   @0x50F5FC..0x50F634
//   y = Crt_ftol((double)(screenH * (designY + H/2)) / refH) - H/2   @0x50F645..0x50F68B
// où W/H = Sprite2D_GetWidth/GetHeight(g_AssetMgr_UiAtlasSlots + 148*idx), et refW/refH
// = champs +8/+12 de g_PlayerCmdController 0x1669170 = 1024.0f/768.0f (ts2::kRefWidth/
// kRefHeight). Crt_ftol tronque vers zéro, comme static_cast<int> depuis un double.
// Ancre le CENTRE du sprite à la même fraction d'écran que sa position de conception ;
// le sprite lui-même n'est PAS mis à l'échelle. À 1024x768 se réduit à (designX, designY).
void FloatingNotices::Project(int designX, int designY, int spriteW, int spriteH,
                              int screenW, int screenH, int& outX, int& outY) {
    const int halfW = spriteW / 2;
    const int halfH = spriteH / 2;
    outX = static_cast<int>(static_cast<double>(screenW * (designX + halfW)) /
                            static_cast<double>(ts2::kRefWidth)) - halfW;
    outY = static_cast<int>(static_cast<double>(screenH * (designY + halfH)) /
                            static_cast<double>(ts2::kRefHeight)) - halfH;
}

// -----------------------------------------------------------------------------
// Chargement paresseux d'un des deux sprites. Le device est obtenu via
// ID3DXSprite::GetDevice (même technique que ChatWindow::EnsureWhiteTexture, qui évite
// de faire dépendre le widget de gfx::Renderer).
gfx::GpuTexture* FloatingNotices::EnsureTexture(gfx::SpriteBatch& sprites, int which) {
    if (!gpu_ || which < 0 || which > 1) return nullptr;
    if (gpu_->tried[which])
        return gpu_->tex[which].Valid() ? &gpu_->tex[which] : nullptr;

    ID3DXSprite* spr = sprites.Get();
    if (!spr) return nullptr;                        // sprite pas encore créé -> réessai à la frame suivante
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(spr->GetDevice(&dev)) || !dev) return nullptr;

    gpu_->tried[which] = true;                       // tentative RÉELLE : ne pas retenter en boucle
    asset::ImgFile img;
    const std::string path = GxdCategory1Path(kSpriteFileNo[which]);
    if (!path.empty() && img.Load(path))
        gpu_->tex[which].CreateFromImgFile(dev, img);
    dev->Release();

    return gpu_->tex[which].Valid() ? &gpu_->tex[which] : nullptr;
}

// -----------------------------------------------------------------------------
// HUD_RenderFloatingMessages 0x5AF4C0.
//
// DÉVIATION ASSUMÉE : le binaire fait UNE boucle qui alterne Sprite2D_Draw et
// UI_DrawNumberValue par slot ; ici le rendu est scindé en une passe sprite
// (SpriteBatch::Begin/End) puis une passe texte (Font::BeginBatch/EndBatch), contrainte
// imposée par l'API de lot — même scission que ChatWindow (RenderTabPanels /
// RenderTabLabels). Résultat identique au pixel : les sprites d'un slot et son texte ne
// se recouvrent jamais (le texte est dessiné SOUS le sprite, dy texte > dy sprite).
void FloatingNotices::Render(gfx::SpriteBatch& sprites, gfx::Font& font, float nowSec,
                             int screenW, int screenH) {
    // Garde @0x5AF4DA : `if (g_SceneMgr == 6 && g_SceneSubState == 4)`, sinon RAZ des
    // 13 slots @0x5AF4FA. Le volet `g_SceneMgr == 6` (InGame) est STRUCTURELLEMENT
    // satisfait par le site d'appel : cette méthode n'est atteinte que via
    // ChatWindow::Render <- GameHud::Render, lui-même appelé sous le SEUL
    // `case Scene::InGame` de SceneManager::Render (Scene/SceneManager.cpp L1300).
    // ClientSource n'expose pas de miroir global de g_SceneMgr 0x1676180 (seul
    // ts2::g_SceneSubState 0x1676184 l'est, cf. Scene/SceneManager.h L54) — tester le
    // seul sous-état ici est donc EXACTEMENT équivalent à la garde d'origine.
    if (ts2::g_SceneSubState != kSubStateMainTick) {
        ClearAll();
        return;
    }

    // Expiration : `g_GameTimeSec - ts <= 10.0` @0x5AF552, sinon slot = 0 @0x5AF55A.
    // Coupure NETTE (aucun fondu/lerp/rampe alpha dans l'original — cf. le constat déjà
    // consigné dans Game/ClientRuntime.h::MessageLog::Floating). Fait AVANT le dessin et
    // indépendamment de la disponibilité du sprite/de la police, pour que les slots
    // expirent même si le rendu est indisponible.
    for (int i = 0; i < kSlotCount; ++i) {
        if (!active_[static_cast<size_t>(i)]) continue;
        if (nowSec - ts_[static_cast<size_t>(i)] > kLifetimeSec)
            active_[static_cast<size_t>(i)] = 0;
    }

    // Résolution des deux sprites AVANT l'ouverture du lot (ID3DXSprite::Begin sauvegarde
    // l'état du device : on évite de créer une texture au milieu du lot) et UNE SEULE fois
    // pour les deux passes. Chargement uniquement si un slot actif le réclame — le sprite
    // du type 12 (001_01028.IMG) n'est donc jamais chargé tant qu'aucune notice de ce type
    // n'est affichée.
    gfx::GpuTexture* tex[2] = { nullptr, nullptr };
    bool needed[2] = { false, false };
    for (int i = 0; i < kSlotCount; ++i) {
        if (active_[static_cast<size_t>(i)])
            needed[kLayout[static_cast<size_t>(i)].tex] = true;
    }
    for (int t = 0; t < 2; ++t) {
        if (needed[t]) tex[t] = EnsureTexture(sprites, t);
    }

    // --- Passe sprite (Sprite2D_Draw 0x4D6B20 : blit plein-cadre blanc, sans échelle).
    if (sprites.Ready()) {
        sprites.Begin();
        for (int i = 0; i < kSlotCount; ++i) {
            if (!active_[static_cast<size_t>(i)]) continue;
            const TypeLayout& L = kLayout[static_cast<size_t>(i)];
            gfx::GpuTexture* t = tex[L.tex];
            if (!t || !t->Handle()) continue; // fidèle : le binaire n'a aucun repli graphique
            int x = 0, y = 0;
            Project(L.designX, L.designY, static_cast<int>(t->Width()),
                    static_cast<int>(t->Height()), screenW, screenH, x, y);
            sprites.DrawSprite(t->Handle(), nullptr, x, y + L.spriteDy, gfx::kSpriteWhite);
        }
        sprites.End();
    }

    // --- Passe texte (UI_DrawNumberValue 0x53FCC0 -> UI_DrawText 0x69E750 mode 2 =
    // contour noir 8 directions == gfx::kStyleOutline).
    if (!font.Ready()) return;
    const D3DCOLOR color = game::g_Strings.colors.Get(kNoticeColorIndex); // ColorTable_GetColor 0x4C1FE0

    font.BeginBatch();
    for (int i = 0; i < kSlotCount; ++i) {
        if (!active_[static_cast<size_t>(i)]) continue;
        const TypeLayout& L = kLayout[static_cast<size_t>(i)];

        // Le texte est ancré sur les MÊMES coordonnées projetées que le sprite : si la
        // texture n'a pas pu être chargée, W/H valent 0 et la projection dégénère en
        // (designX*screenW/1024, designY*screenH/768) — le texte reste lisible plutôt
        // que de disparaître avec le sprite (seul repli conservé, aucun rect coloré).
        const gfx::GpuTexture* t = tex[L.tex];
        const int tw = t ? static_cast<int>(t->Width())  : 0;
        const int th = t ? static_cast<int>(t->Height()) : 0;
        int x = 0, y = 0;
        Project(L.designX, L.designY, tw, th, screenW, screenH, x, y);

        // x + textCx - UI_MeasureNumberText(txt)/2  (@0x5AF5CA/0x5AF5E3/0x5AF606).
        // UI_MeasureNumberText 0x53FCA0 == UI_MeasureText(g_GfxRenderer, txt) 0x69E680
        // == gfx::Font::MeasureText.
        const char* txt = text_[static_cast<size_t>(i)].c_str();
        const int   w   = font.MeasureText(txt);
        font.DrawTextStyled(txt, x + L.textCx - w / 2, y + L.textDy, color, gfx::kStyleOutline);

        // Type 12 seul : 2e ligne (this+1373) @0x5AFCE5..0x5AFD1E.
        if (i == 12) {
            const char* txt2 = text2_.c_str();
            const int   w2   = font.MeasureText(txt2);
            font.DrawTextStyled(txt2, x + kType12Line2Cx - w2 / 2, y + kType12Line2Dy,
                                color, gfx::kStyleOutline);
        }
    }
    font.EndBatch();
}

} // namespace ts2::ui

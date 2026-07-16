// Gfx/SkyRenderer.cpp — voir SkyRenderer.h pour le bandeau complet (ce qui est dérivé des
// vraies données .ATM vs simplifié/absent). Dessin = quad plein écran XYZRHW 2 couleurs
// (mêmes réglages D3D que l'ancien repli de Gfx/WorldGeometryRenderer.cpp::
// drawSkyGradientFallback, dont ce fichier reprend/remplace la logique de dessin — la
// NOUVEAUTÉ ici est le calcul des couleurs, dérivé de l'heure RÉELLE du .ATM au lieu de
// deux teintes fixes codées en dur).
#include "Gfx/SkyRenderer.h"
#include "Asset/AtmosphereFile.h"
#include "World/WorldIntegration.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#pragma comment(lib, "d3d9.lib")

namespace ts2::gfx {

namespace {

struct ScreenGradientVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
};
constexpr DWORD kScreenGradientFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

// ---------------------------------------------------------------------------------------
// Modèle jour/nuit SIMPLE à keyframes horaires (PAS d'éphéméride réelle, cf. bandeau .h).
// Deux tables séparées (zénith / horizon) : le zénith reste dans les bleus (plus sombre la
// nuit), l'horizon prend les teintes chaudes typiques de l'aube/du crépuscule — c'est la
// seule "invention" de ce module, clairement documentée comme un modèle procédural
// simplifié, PAS une lecture de la diffusion Perez réelle. Les heures/couleurs sont choisies
// pour être plausibles et lisibles, interpolées LINÉAIREMENT (RGB) entre les deux keyframes
// encadrant l'heure courante, avec repli circulaire (24h == 0h).
// ---------------------------------------------------------------------------------------
struct ColorKeyframe { double hour; float r, g, b; };

constexpr ColorKeyframe kZenithKeyframes[] = {
    { 0.0,  0.02f, 0.03f, 0.10f }, // minuit — bleu nuit très sombre
    { 4.0,  0.03f, 0.05f, 0.14f }, // creux de nuit
    { 6.0,  0.20f, 0.30f, 0.55f }, // aube — le zénith s'éclaircit
    { 8.0,  0.25f, 0.45f, 0.80f }, // matin
    { 12.0, 0.20f, 0.45f, 0.85f }, // midi — bleu ciel plein
    { 17.0, 0.22f, 0.35f, 0.65f }, // fin d'après-midi
    { 19.0, 0.10f, 0.12f, 0.30f }, // crépuscule — assombrissement rapide
    { 21.0, 0.04f, 0.06f, 0.16f }, // nuit tombée
    { 24.0, 0.02f, 0.03f, 0.10f }, // == 0h (boucle)
};

constexpr ColorKeyframe kHorizonKeyframes[] = {
    { 0.0,  0.05f, 0.06f, 0.14f }, // minuit
    { 4.0,  0.08f, 0.08f, 0.18f }, // creux de nuit
    { 6.0,  0.90f, 0.55f, 0.35f }, // aube — orangé
    { 8.0,  0.75f, 0.75f, 0.80f }, // matin — clair
    { 12.0, 0.60f, 0.72f, 0.88f }, // midi — pâle bleuté
    { 17.0, 0.85f, 0.60f, 0.45f }, // fin d'après-midi — doré
    { 19.0, 0.80f, 0.35f, 0.25f }, // crépuscule — rouge/orange
    { 21.0, 0.12f, 0.10f, 0.20f }, // nuit tombée
    { 24.0, 0.05f, 0.06f, 0.14f }, // == 0h (boucle)
};

// Interpole linéairement dans une table de keyframes triée par heure croissante [0,24].
void LerpKeyframes(const ColorKeyframe* table, size_t count, double hour,
                    float& outR, float& outG, float& outB) {
    hour = std::clamp(hour, 0.0, 24.0);
    for (size_t i = 0; i + 1 < count; ++i) {
        const ColorKeyframe& a = table[i];
        const ColorKeyframe& b = table[i + 1];
        if (hour >= a.hour && hour <= b.hour) {
            const double span = (b.hour - a.hour);
            const float t = span > 0.0 ? static_cast<float>((hour - a.hour) / span) : 0.0f;
            outR = a.r + (b.r - a.r) * t;
            outG = a.g + (b.g - a.g) * t;
            outB = a.b + (b.b - a.b) * t;
            return;
        }
    }
    // Hors bornes (ne devrait pas arriver avec des tables [0,24]) : dernier keyframe.
    outR = table[count - 1].r; outG = table[count - 1].g; outB = table[count - 1].b;
}

D3DCOLOR ToArgb(float r, float g, float b) {
    auto clampByte = [](float v) -> DWORD {
        return static_cast<DWORD>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return D3DCOLOR_ARGB(0xFF, clampByte(r), clampByte(g), clampByte(b));
}

} // namespace

bool SkyRenderer::Init(Renderer& renderer) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("SkyRenderer::Init : device nul."); return false; }
    ready_ = true;
    recomputeColors(); // couleurs de repli ("midi") tant qu'aucun .ATM n'a été fourni
    TS2_LOG("SkyRenderer pret (gradient procedural derive de l'heure .ATM reelle).");
    return true;
}

void SkyRenderer::Shutdown() {
    dev_ = nullptr;
    ready_ = false;
}

void SkyRenderer::ApplyConfig(const world::SilverLiningConfig& cfg) {
    config_.defaultLongitude = cfg.defaultLongitude;
    config_.defaultLatitude = cfg.defaultLatitude;
    config_.defaultAltitude = cfg.defaultAltitude;
    config_.defaultYear = cfg.defaultYear;
    config_.defaultMonth = cfg.defaultMonth;
    config_.defaultDay = cfg.defaultDay;
    config_.defaultHour = cfg.defaultHour;
    config_.defaultMinute = cfg.defaultMinute;
    config_.defaultSecond = cfg.defaultSecond;
    config_.defaultTimezone = cfg.defaultTimezone;
    config_.defaultDst = cfg.defaultDst;
    config_.defaultTurbidity = cfg.defaultTurbidity;
    config_.disableToneMapping = cfg.disableToneMapping;
    config_.enableAtmosphereFromSpace = cfg.enableAtmosphereFromSpace;
    config_.atmosphereHeight = cfg.atmosphereHeight;
    config_.atmosphereScaleHeightMeters = cfg.atmosphereScaleHeightMeters;
    TS2_LOG("SkyRenderer : config SilverLining chargee (turbidity=%.1f, altitude atmos=%.0f m).",
            config_.defaultTurbidity, config_.atmosphereHeight);
}

void SkyRenderer::SetAtmosphere(const asset::AtmosphereFile& atm) {
    hasRealAtmosphere_ = atm.Valid();
    if (hasRealAtmosphere_) {
        hourOfDay_     = atm.HourOfDay();
        latitude_      = atm.Latitude();
        longitude_     = atm.Longitude();
        forceBlackSky_ = atm.RenderFlagForceBlackSky();
        TS2_LOG("SkyRenderer::SetAtmosphere : \"%s\" reel charge (heure=%.2fh lat=%.3f lon=%.3f "
                "forceBlack=%d skipCelestial=%d).",
                atm.Path().c_str(), hourOfDay_, latitude_, longitude_,
                forceBlackSky_ ? 1 : 0, atm.RenderFlagSkipCelestial() ? 1 : 0);
    } else {
        // Repli CORRIGÉ (D-2/T-14 — IDA gagne) : quand la donnée d'atmosphère de zone manque
        // ("Atmosphere.DAT"/.ATM absent), le moteur d'origine NE retombe PAS sur les défauts
        // SilverLining.config (30/-122) mais sur une géo Séoul codée en dur, prouvée par :
        //   World_LoadMap 0x4116B0 @0x41184b : Env_SetGeoLocation(this, 37.6, 127.0, 0.0)
        //   (branche prise quand File_IfstreamOpen("Atmosphere.DAT") échoue -> v9[2]&4 != 0 || !v9[21]).
        //   Env_SetGeoLocation 0x411D30 override lat/lon [-90,90]/[-180,180] puis fixe DateTime
        //   2010-07-26 15:00 (@0x411dcf..0x411de7 : v10=2010/v11=7/v12=26/v13=15) et turbidité 2.0.
        // Rappel : les défauts config_ (30/-122, 2006-08-15 12:00) restent les valeurs stock du
        //   SilverLining.config (cAtmosphere_Initialize 0x793390) — À NE PAS confondre avec ce
        //   fallback Séoul du chemin World_LoadMap. lat/lon ne pilotent pas encore le gradient (T-5) ;
        //   l'heure 15:00, elle, EST consommée par le modèle jour/nuit -> repli fidèle observable.
        latitude_  = 37.6;   // World_LoadMap 0x4116B0 @0x41184b -> Env_SetGeoLocation(.,37.6,.)
        longitude_ = 127.0;  // World_LoadMap 0x4116B0 @0x41184b -> Env_SetGeoLocation(.,.,127.0)
        hourOfDay_ = 15.0;   // Env_SetGeoLocation 0x411D30 @0x411de7 : v13=15 (2010-07-26 15:00, min/sec=0)
        forceBlackSky_ = false;
        TS2_WARN("SkyRenderer::SetAtmosphere : .ATM invalide/absent -> repli Seoul 37.6/127.0 15:00 "
                 "(fidele World_LoadMap 0x4116B0 -> Env_SetGeoLocation 0x411D30).");
    }
    recomputeColors();
}

void SkyRenderer::recomputeColors() {
    if (forceBlackSky_) {
        // Reproduit fidèlement l'override "ciel noir" du moteur d'origine (@+642, cf. bandeau
        // .h / Docs/TS2_SILVERLINING_CONFIG.md §3.2-3.3) : zénith ET horizon en noir opaque.
        // CONFIRMED — cAtmosphere_RenderFrame 0x793B80 : si @+642, les 3 échantillons (ciel base
        //   dword_811860.., ciel secondaire dword_811AAC.., horizon qword_8117A0..) sont forcés à (0,0,0,1)
        //   au lieu du calcul Perez (SL_GetSkyColorRGBA/SL_GetHorizonColor). (Flag .ATM — pas d'indice VeryOldClient.)
        zenithColor_ = horizonColor_ = D3DCOLOR_ARGB(0xFF, 0, 0, 0);
        return;
    }
    float zr, zg, zb, hr, hg, hb;
    LerpKeyframes(kZenithKeyframes, std::size(kZenithKeyframes), hourOfDay_, zr, zg, zb);
    LerpKeyframes(kHorizonKeyframes, std::size(kHorizonKeyframes), hourOfDay_, hr, hg, hb);
    zenithColor_  = ToArgb(zr, zg, zb);
    horizonColor_ = ToArgb(hr, hg, hb);
}

// =======================================================================================
// Skybox cube 6 faces — repli FIDÈLE de Env_RenderSkyCube 0x6a8f60 (avant SilverLining).
//
// DEFERRED : SilverLining complet reste non implémenté. cAtmosphere_RenderFrame 0x793B80
//   (rendu réel du ciel/atmosphère par frame) et SL_Renderer_LoadBackendDLL 0x71F300
//   (LoadLibraryA("<cfg>/VC9/win32/SilverLiningDirectX9-MT.dll") au chargement de map, puis
//   ~90 GetProcAddress remplissant la table C g_SL_* 0x186381C-0x186397C) dépendent de la lib
//   SilverLiningDirectX9-MT.dll / SilverLining-MT.lib, NON redistribuable. Le cube ci-dessous
//   est l'étape intermédiaire fidèle : la géométrie/l'état D3D9 exacts d'Env_RenderSkyCube,
//   alimentés au runtime par les setters (textures ciel de zone, rayon, position caméra).
// =======================================================================================

// Reconstruit les 24 sommets du cube ciel. Env_RenderSkyCube 0x6a8f60 @0x6a8f9b..0x6a92c6.
void SkyRenderer::rebuildCubeVertices() {
    // Cache @0x6a8f88 / @0x6a92c6 : ne rebâtir que si le rayon (flt_7FFEA0) a changé.
    if (cubeCacheRadius_ == skyRadius_) return;

    // @0x6a8f9b : v3 = flt_7FFEA0 / sqrt(3.0)  (dbl_7EDA38 = 3.0). s = demi-arête faces 1-5.
    const float s = skyRadius_ / std::sqrt(3.0f);
    // ⚠️ ASYMÉTRIE FIDÈLE PROUVÉE (decompile 0x6a9039..0x6a9087) : la face 0 (+Z) utilise
    //   ±0.5·v3 (moitié de taille) alors que les faces 1-5 utilisent ±v3. À reproduire tel
    //   quel (le binaire d'origine EST ainsi — règle FIDÉLITÉ, ne pas "corriger" en cube
    //   symétrique). flt_7A939C=+0.5, flt_7BB294=-0.5, flt_7EDA10=-1.0.
    const float h = 0.5f * s;

    // UV identiques pour les 6 faces (prouvé @0x6a8fa1.. et par face : v0=(0,1) v1=(0,0)
    //   v2=(1,1) v3=(1,0)). Table des positions = §A.1 du plan (tracé pas-à-pas de la pile FPU).
    const SkyCubeVertex verts[24] = {
        // Face 0 (+Z, faceTex_[0]) — DEMI-TAILLE h. @0x6a9039..0x6a9087
        { -h, -h,  h, 0.0f, 1.0f }, {  -h,  h,  h, 0.0f, 0.0f },
        {  h, -h,  h, 1.0f, 1.0f }, {   h,  h,  h, 1.0f, 0.0f },
        // Face 1 (-Z, faceTex_[1]). @0x6a908d..0x6a90dd
        {  s, -s, -s, 0.0f, 1.0f }, {   s,  s, -s, 0.0f, 0.0f },
        { -s, -s, -s, 1.0f, 1.0f }, {  -s,  s, -s, 1.0f, 0.0f },
        // Face 2 (-X, faceTex_[2]). @0x6a90e3..0x6a9135
        { -s, -s, -s, 0.0f, 1.0f }, {  -s,  s, -s, 0.0f, 0.0f },
        { -s, -s,  s, 1.0f, 1.0f }, {  -s,  s,  s, 1.0f, 0.0f },
        // Face 3 (+X, faceTex_[3]). @0x6a913b..0x6a919d
        {  s, -s,  s, 0.0f, 1.0f }, {   s,  s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {   s,  s, -s, 1.0f, 0.0f },
        // Face 4 (+Y haut, faceTex_[4]). @0x6a91a3..0x6a91fd
        { -s,  s,  s, 0.0f, 1.0f }, {  -s,  s, -s, 0.0f, 0.0f },
        {  s,  s,  s, 1.0f, 1.0f }, {   s,  s, -s, 1.0f, 0.0f },
        // Face 5 (-Y bas, faceTex_[5]). @0x6a9205..0x6a92bb
        { -s, -s, -s, 0.0f, 1.0f }, {  -s, -s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {   s, -s,  s, 1.0f, 0.0f },
    };
    for (int i = 0; i < 24; ++i) cubeVerts_[i] = verts[i];

    cubeCacheRadius_ = skyRadius_; // @0x6a92c6 : this+210 = flt_7FFEA0
}

// Dessine le cube ciel centré caméra. Séquence EXACTE d'Env_RenderSkyCube 0x6a8f60.
void SkyRenderer::renderCube() {
    rebuildCubeVertices();

    // @0x6a92d7 : SetRenderState(D3DRS_ZWRITEENABLE=7, FALSE) — z-write off (ciel au fond).
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

    // @0x6a9324 : Gfx_SetLight(g_GfxRenderer, 1, ...) configure le light slot 0 (champs
    //   d'Object A non possédés par ce front). La FVF (XYZ|TEX1) n'a NI normale NI diffuse =>
    //   l'éclairage n'affecte pas visuellement le cube => on force lighting off (fidèle rendu).
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);

    // @0x6a933c : si dword_7FFEA4 (fog actif), SetRenderState(D3DRS_FOGENABLE=28, FALSE).
    if (fogActive_) dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);

    // @0x6a934f : SetFVF(258 = 0x102 = D3DFVF_XYZ | D3DFVF_TEX1).
    dev_->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);

    // @0x6a936e : D3DXMatrixTranslation(&m, g_CameraPos, flt_800134, flt_800138) puis
    //   @0x6a9385 SetTransform(D3DTS_WORLD=256, &m) => cube centré sur l'œil caméra (skybox
    //   infinie). Matrice identité + translation posée à la main (équivalent bit-exact, pas de
    //   dépendance D3DX legacy).
    D3DMATRIX m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        camPos_[0], camPos_[1], camPos_[2], 1.0f,
    };
    dev_->SetTransform(D3DTS_WORLD, &m);

    // @0x6a9398 / @0x6a93ab : clamp U/V (D3DSAMP_ADDRESSU=1/ADDRESSV=2 -> D3DTADDRESS_CLAMP=3)
    //   pour éviter le suintement de bord entre faces.
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    // État global NON touché par le binaire (hérite du pipeline, non possédé par ce front) :
    //   posé explicitement pour un cube texturé intérieur, puis restauré ci-dessous. CULLMODE
    //   NONE garantit la visibilité des faces vues de l'intérieur ; COLOROP=SELECTARG1(TEXTURE)
    //   => texture pure (lighting off, pas de diffuse).
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    // Boucle 6 faces @0x6a93b1..0x6a93ee : SetTexture(0, faceTex[i]) (@0x6a93cc, vtbl+260) puis
    //   DrawPrimitiveUP(D3DPT_TRIANGLESTRIP=5, PrimitiveCount=2, &face, Stride=20) (@0x6a93e1,
    //   vtbl+332). faceTex_ = 1er dword des descripteurs de face 52 o (v7 += 13) de l'objet sky.
    for (int i = 0; i < 6; ++i) {
        dev_->SetTexture(0, faceTex_[i]);
        dev_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &cubeVerts_[i * 4], sizeof(SkyCubeVertex));
    }

    // @0x6a93fd / @0x6a9410 : restaurer wrap U/V (D3DTADDRESS_WRAP=1).
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    // @0x6a942a : si fog, SetRenderState(D3DRS_FOGENABLE=28, TRUE).
    if (fogActive_) dev_->SetRenderState(D3DRS_FOGENABLE, TRUE);

    // @0x6a9435 : Gfx_SkyboxEndState 0x69d780 réapplique le material (SetMaterial, champs d'Object
    //   A non possédés) — ignoré ici ; on restaure à la place les états globaux compensés ci-dessus.
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);

    // @0x6a9446 : SetRenderState(D3DRS_ZWRITEENABLE=7, TRUE) — restaure z-write.
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
}

// --- Setters de la skybox cube (état runtime modélisé, cf. SkyRenderer.h) -----------------
void SkyRenderer::SetSkyCubeTextures(IDirect3DTexture9* const faces[6]) {
    for (int i = 0; i < 6; ++i) faceTex_[i] = faces ? faces[i] : nullptr;
}

void SkyRenderer::SetSkyRadius(float r) {
    if (r != skyRadius_) {
        skyRadius_ = r;
        cubeCacheRadius_ = -1.0f; // force la reconstruction au prochain rebuildCubeVertices()
    }
}

void SkyRenderer::SetCameraPosition(float x, float y, float z) {
    camPos_[0] = x; camPos_[1] = y; camPos_[2] = z; // g_CameraPos 0x800130
}

void SkyRenderer::SetFogActive(bool on) {
    fogActive_ = on; // dword_7FFEA4 = g_GfxRenderer+140
}

bool SkyRenderer::HasSkyCube() const {
    // Actif quand le gate d'Env_RenderSkyCube serait franchi : rayon > 0 (this+0 != 0 => flt_7FFEA0
    //   alimenté) et au moins une texture de face présente.
    if (skyRadius_ <= 0.0f) return false;
    for (int i = 0; i < 6; ++i) if (faceTex_[i]) return true;
    return false;
}

void SkyRenderer::Render(int screenW, int screenH) {
    if (!ready_ || !dev_ || screenW <= 0 || screenH <= 0) return;

    // Repli FIDÈLE (Env_RenderSkyCube 0x6a8f60) dès qu'une skybox cube est alimentée par la zone.
    if (HasSkyCube()) { renderCube(); return; }

    // --- PLACEHOLDER (inchangé) : gradient plein écran jour/nuit (cf. bandeau .h). Modèle
    //     procédural simplifié, PAS une lecture de la diffusion Perez réelle — ne PAS raffiner
    //     les couleurs inventées (consigne). Sert de repli tant que la skybox cube n'est pas
    //     alimentée (rayon/textures ciel de zone non encore câblés).
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    // -0.5f de décalage pixel-texel D3D9 : sans importance ici (pas de texture), gardé pour
    // cohérence avec les autres quads plein écran du moteur (cf. ancien
    // WorldGeometryRenderer::drawSkyGradientFallback).
    ScreenGradientVertex verts[4] = {
        { -0.5f,      -0.5f,      1.0f, 1.0f, zenithColor_  }, // haut-gauche
        { w - 0.5f,   -0.5f,      1.0f, 1.0f, zenithColor_  }, // haut-droit
        { -0.5f,      h - 0.5f,   1.0f, 1.0f, horizonColor_ }, // bas-gauche
        { w - 0.5f,   h - 0.5f,   1.0f, 1.0f, horizonColor_ }, // bas-droit
    };

    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetVertexDeclaration(nullptr);
    dev_->SetFVF(kScreenGradientFVF);

    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev_->SetTexture(0, nullptr);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    dev_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(ScreenGradientVertex));

    // Restaure l'état attendu par le reste de la frame (même politique que l'ancien
    // drawSkyGradientFallback / WorldRenderer::drawPlaceholderCube).
    dev_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

} // namespace ts2::gfx

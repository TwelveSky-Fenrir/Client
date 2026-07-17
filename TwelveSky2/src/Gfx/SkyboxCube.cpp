// Gfx/SkyboxCube.cpp — transcription bit-fidèle du CORPS de Env_RenderSkyCube 0x6a8f60.
// Voir SkyboxCube.h pour le VERDICT (code mort dans ce build) et la provenance des textures.
//
// ⚠️ MODULE AUTONOME, NON CÂBLÉ : aucun appelant dans ce commit. Le vrai build n'atteint
//    jamais Env_RenderSkyCube (les 8 sites Gfx_BeginFrame passent a2=NULL, cf. .h) ; ce
//    fichier existe pour garder la géométrie/les états EXACTS sous la main, prêts si une
//    mission future réactivait la couche (après avoir identifié l'objet sky au runtime).
//    La représentation FIDÈLE du ciel de ce build reste Gfx/SkyRenderer.* (gradient .ATM).
#include "Gfx/SkyboxCube.h"

#include <cmath>

#pragma comment(lib, "d3d9.lib")

namespace ts2::gfx {

// Reconstruit les 24 sommets. Env_RenderSkyCube @0x6a8f9b..0x6a92c6.
// Constantes .rdata prouvées (lues dans l'IDB) : flt_7A939C = +0.5 (0x3F000000),
// flt_7BB294 = -0.5 (0xBF000000), flt_7EDA10 = -1.0 (0xBF800000), dbl 3.0 sous le sqrt.
void SkyboxCube::rebuild() {
    // Cache @0x6a8f88 / @0x6a92c6 : ne rebâtir que si le rayon (flt_7FFEA0) a changé.
    if (cacheRadius_ == radius_) return;

    // @0x6a8f9b : s = flt_7FFEA0 / sqrt(3.0). s = demi-arête des faces 1 à 5.
    const float s = radius_ / std::sqrt(3.0f);
    // ⚠️ ASYMÉTRIE FIDÈLE (decompile 0x6a9031..0x6a9087) : la face 0 (+Z) est construite à
    //    ±0.5*s (flt_7BB294 = -0.5 / flt_7A939C = +0.5), alors que les faces 1 à 5 utilisent
    //    ±s (facteur 1.0) et -s (flt_7EDA10 = -1.0). À REPRODUIRE TEL QUEL : le binaire EST
    //    ainsi (la face +Z est deux fois plus petite). Ne PAS « corriger » en cube symétrique.
    const float h = 0.5f * s;

    // UV identiques sur les 6 faces (prouvé : v0=(0,1) v1=(0,0) v2=(1,1) v3=(1,0), stores
    // ebx=0.0/eax=1.0 @0x6a8fa1.. et par face). Positions = tracé pas-à-pas de la pile x87.
    const Vertex verts[24] = {
        // Face 0 (+Z, faceTex_[0]) — DEMI-TAILLE h. @0x6a9039..0x6a9087
        { -h, -h,  h, 0.0f, 1.0f }, { -h,  h,  h, 0.0f, 0.0f },
        {  h, -h,  h, 1.0f, 1.0f }, {  h,  h,  h, 1.0f, 0.0f },
        // Face 1 (-Z, faceTex_[1]). @0x6a908d..0x6a90dd
        {  s, -s, -s, 0.0f, 1.0f }, {  s,  s, -s, 0.0f, 0.0f },
        { -s, -s, -s, 1.0f, 1.0f }, { -s,  s, -s, 1.0f, 0.0f },
        // Face 2 (-X, faceTex_[2]). @0x6a90e3..0x6a9135
        { -s, -s, -s, 0.0f, 1.0f }, { -s,  s, -s, 0.0f, 0.0f },
        { -s, -s,  s, 1.0f, 1.0f }, { -s,  s,  s, 1.0f, 0.0f },
        // Face 3 (+X, faceTex_[3]). @0x6a913b..0x6a919d
        {  s, -s,  s, 0.0f, 1.0f }, {  s,  s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {  s,  s, -s, 1.0f, 0.0f },
        // Face 4 (+Y haut, faceTex_[4]). @0x6a91a3..0x6a91fd
        { -s,  s,  s, 0.0f, 1.0f }, { -s,  s, -s, 0.0f, 0.0f },
        {  s,  s,  s, 1.0f, 1.0f }, {  s,  s, -s, 1.0f, 0.0f },
        // Face 5 (-Y bas, faceTex_[5]). @0x6a9205..0x6a92bb
        { -s, -s, -s, 0.0f, 1.0f }, { -s, -s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {  s, -s,  s, 1.0f, 0.0f },
    };
    for (int i = 0; i < 24; ++i) verts_[i] = verts[i];

    cacheRadius_ = radius_; // @0x6a92c6 : this+210 = flt_7FFEA0 (dernier rayon construit)
}

void SkyboxCube::SetRadius(float radius) {
    if (radius != radius_) {
        radius_ = radius;
        cacheRadius_ = -1.0f; // force la reconstruction au prochain rebuild()
    }
}

void SkyboxCube::SetFaceTextures(IDirect3DTexture9* const faces[6]) {
    for (int i = 0; i < 6; ++i) faceTex_[i] = faces ? faces[i] : nullptr;
}

IDirect3DTexture9* SkyboxCube::FaceTexture(int i) const {
    return (i >= 0 && i < 6) ? faceTex_[i] : nullptr;
}

bool SkyboxCube::IsActive() const {
    if (radius_ <= 0.0f) return false;
    for (int i = 0; i < 6; ++i) if (faceTex_[i]) return true;
    return false;
}

// Séquence d'états EXACTE d'Env_RenderSkyCube 0x6a8f60 (à partir de @0x6a92d7). On reproduit
// UNIQUEMENT les états que le binaire modifie ; le cull et les texture-stage states ne sont
// PAS touchés par l'original (hérités du pipeline appelant) — on ne les ajoute donc pas.
void SkyboxCube::Render(IDirect3DDevice9* dev, const float camPos[3], bool fogEnabled) {
    if (!dev || radius_ <= 0.0f) return;
    rebuild();

    // @0x6a92d7 : SetRenderState(RS 7 = D3DRS_ZENABLE, FALSE). Le test de profondeur est
    //   DÉSACTIVÉ pendant le dessin du ciel (le cube remplit l'écran ; le monde se dessine
    //   par-dessus ensuite). ⚠️ RS 7 = D3DRS_ZENABLE (test), PAS D3DRS_ZWRITEENABLE (=14).
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);

    // @0x6a9324 : Gfx_SetLight(g_GfxRenderer, 1, ...) 0x69d5c0 configure le light slot 0 à
    //   partir de champs matériau du renderer (Object A, +1204/+1236...). OMIS ICI : (a) ces
    //   champs appartiennent au renderer GXD non possédé par ce module autonome ; (b) le FVF
    //   du cube (XYZ|TEX1) n'a NI normale NI diffuse => l'éclairage n'a AUCUN effet visuel sur
    //   le cube. L'appel est donc visuellement inerte pour cette géométrie.

    // @0x6a933c : si dword_7FFEA4 (fog actif), SetRenderState(RS 28 = D3DRS_FOGENABLE, FALSE).
    if (fogEnabled) dev->SetRenderState(D3DRS_FOGENABLE, FALSE);

    // @0x6a934f : SetFVF(0x102 = D3DFVF_XYZ | D3DFVF_TEX1) (vtbl+356).
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);

    // @0x6a936e : D3DXMatrixTranslation(&m, g_CameraPos.x/y/z) puis @0x6a9385
    //   SetTransform(D3DTS_WORLD = 256, &m) (vtbl+176) => cube centré sur l'œil caméra
    //   (skybox infinie). Matrice identité + translation posée à la main (équivalent bit-exact,
    //   sans dépendance à la couche D3DX legacy).
    const float cx = camPos ? camPos[0] : 0.0f;
    const float cy = camPos ? camPos[1] : 0.0f;
    const float cz = camPos ? camPos[2] : 0.0f;
    D3DMATRIX world = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        cx,   cy,   cz,   1.0f,
    };
    dev->SetTransform(D3DTS_WORLD, &world);

    // @0x6a9398 / @0x6a93ab : SetSamplerState(0, ADDRESSU/ADDRESSV, D3DTADDRESS_CLAMP = 3)
    //   (vtbl+276) — clamp pour éviter le suintement de bord entre faces.
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    // Boucle 6 faces @0x6a93b1..0x6a93ee : SetTexture(0, faceTex[i]) (@0x6a93cc, vtbl+260) ;
    //   DrawPrimitiveUP(D3DPT_TRIANGLESTRIP = 5, PrimitiveCount = 2, &face, Stride = 20)
    //   (@0x6a93e1, vtbl+332). Dans l'original, le pointeur de texture avance de 52 o (this+13,
    //   `v7 += 13`) et le pointeur de sommets de 80 o (`v6 += 20` dwords = 4 sommets x 20 o).
    for (int i = 0; i < 6; ++i) {
        dev->SetTexture(0, faceTex_[i]);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &verts_[i * 4], sizeof(Vertex));
    }

    // @0x6a93fd / @0x6a9410 : restaurer D3DTADDRESS_WRAP (= 1) sur ADDRESSU/ADDRESSV.
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    // @0x6a942a : si fog, SetRenderState(RS 28 = D3DRS_FOGENABLE, TRUE) — symétrique du disable.
    if (fogEnabled) dev->SetRenderState(D3DRS_FOGENABLE, TRUE);

    // @0x6a9435 : Gfx_SkyboxEndState 0x69d780 = SetLight(0, &g_GfxRenderer+1200) (vtbl+204),
    //   réapplique la lumière de scène du renderer (Object A). OMIS ICI (même raison que le
    //   Gfx_SetLight ci-dessus : état du renderer non possédé, sans effet sur le FVF du cube).

    // @0x6a9446 : SetRenderState(RS 7 = D3DRS_ZENABLE, TRUE) — restaure le test de profondeur.
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
}

} // namespace ts2::gfx

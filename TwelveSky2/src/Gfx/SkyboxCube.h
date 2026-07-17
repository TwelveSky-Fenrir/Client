// Gfx/SkyboxCube.h — reconstruction FIDÈLE et AUTONOME de Env_RenderSkyCube 0x6a8f60
// (le « skybox cube » 6 faces du moteur GXD de TwelveSky2.exe, build EU).
//
// =======================================================================================
// VERDICT D'ENQUÊTE : CODE MORT dans ce build (le cube n'est JAMAIS dessiné).
// ---------------------------------------------------------------------------------------
//   Env_RenderSkyCube 0x6a8f60 a UN SEUL appelant : Gfx_BeginFrame 0x6a2280, qui ne le
//   lance que sous le gate `if (a2 && *a2)` (@0x6a24dc/@0x6a24de -> @0x6a24e3). Or les
//   HUIT sites d'appel de Gfx_BeginFrame poussent TOUS a2 = NULL (`push 0`) :
//     - Scene_IntroRender        @0x5188c1  (push 0) -> call 0x5188c8
//     - Scene_ServerSelectRender @0x5192a1  (push 0) -> call 0x5192a8
//     - Scene_LoginRender        @0x51b071  (push 0) -> call 0x51b078
//     - Scene_CharSelectRender   @0x51cf21  (push 0) -> call 0x51cf28
//     - Scene_EnterWorldRender   @0x52c2b1  (push 0) -> call 0x52c2b8
//     - Scene_InGameRender       @0x52d133  (push 0) -> call 0x52d13a  (sous-état 0/1)
//     - Scene_InGameRender       @0x52d180  (push 0) -> call 0x52d187  (sous-état 2)
//     - Scene_InGameRender       @0x52d1f5  (push 0) -> call 0x52d1fc  (sous-état >=3, frame complète)
//   => a2 est toujours NULL => le gate `a2 && *a2` est toujours faux => Env_RenderSkyCube
//   n'est jamais atteint. L'objet « sky » (le `this` de 0x6a8f60) n'est donc jamais
//   construit, et le rendu du ciel en jeu passe entièrement par SilverLining
//   (Env_UpdateFrame 0x412550 / Env_StepTimeOfDay 0x412590, cf. TS2_DEEP_FRAME_PIPELINE.md
//   §3/§6). Côté ClientSource, la représentation FIDÈLE du ciel de ce build reste donc le
//   gradient de Gfx/SkyRenderer.* (repli .ATM) — PAS ce cube.
//
// SOURCE DES 6 TEXTURES — NON PROUVABLE STATIQUEMENT (honnête, PAS inventé) :
//   Env_RenderSkyCube ne CHARGE aucune texture : il lit un pointeur IDirect3DTexture9*
//   déjà présent dans l'objet sky, par face, à `this+13` (index dword 13 = octet 52), avec
//   un pas de 13 dwords = 52 o entre faces (boucle @0x6a93b1 : `v7 += 13`). Le loader qui
//   remplirait ces 6 slots vit dans le code qui construit l'objet sky — code jamais atteint
//   (voir ci-dessus). Aucun chemin d'asset (.IMG/.DDS de zone) n'est donc démontrable ; on
//   n'en FABRIQUE pas. Ce module reste donc INERTE par défaut : sans SetFaceTextures(), il
//   dessine des faces non texturées, et par convention IsActive()==false tant qu'aucune
//   texture n'est fournie.
//
// POURQUOI CE MODULE EXISTE MALGRÉ TOUT (fidélité) :
//   Le CORPS de Env_RenderSkyCube (génération de géométrie + séquence d'états D3D9 + boucle
//   de dessin 6 faces) est ENTIÈREMENT prouvé par désassemblage (0x6a8f60..0x6a944c). On en
//   fournit une transcription bit-fidèle, AUTONOME (aucune dépendance au renderer GXD, aucun
//   câblage dans la boucle de frame — FLOTTE C/MAIN décidera si/quand l'utiliser). Si une
//   mission future identifiait au runtime (x32dbg) l'objet sky et son loader de textures,
//   ce module serait prêt à être alimenté sans réécriture.
//
// ANCRAGES GLOBAUX (prouvés) :
//   - flt_7FFEA0 0x7FFEA0 = g_GfxRenderer+136 = FAR PLANE, écrit par Cam_SetProjection
//     0x69cbef via `*(float*)(this+136)=a3` @0x69cbd5 (store registre-relatif : d'où
//     l'absence de xref absolue). Image statique = 0.0 (cache runtime). Côté du cube =
//     far/sqrt(3) (@0x6a8f9b : `fdivr flt_7FFEA0`).
//   - dword_7FFEA4 0x7FFEA4 = g_GfxRenderer+140 = flag FOG-ENABLE (lu par Cam_SetProjection
//     @0x69cc06). Gère l'ENCADREMENT FOG du cube (RS 28), PAS le cull (le brief « cull
//     inverse » est réfuté par le décompilé : RS 28 = D3DRS_FOGENABLE).
//   - g_CameraPos 0x800130 (= g_GfxRenderer+792) = œil caméra ; le cube est translaté dessus
//     (@0x6a936e/@0x6a9385) => skybox « infinie » centrée caméra.
// =======================================================================================
#pragma once
#include <d3d9.h>

namespace ts2::gfx {

// Reconstruction autonome du cube ciel de Env_RenderSkyCube 0x6a8f60.
// Ne possède AUCUNE ressource D3DPOOL_DEFAULT : la géométrie est en RAM (DrawPrimitiveUP) et
// les textures de face sont des pointeurs NON possédés injectés par le propriétaire (l'objet
// sky de zone). Sûr vis-à-vis d'un device-lost : rien à libérer/recréer ; le propriétaire
// doit simplement re-fournir ses textures après un reset.
class SkyboxCube {
public:
    // Sommet du cube : 20 o, FVF D3DFVF_XYZ | D3DFVF_TEX1 (0x102) @0x6a934f.
    struct Vertex { float x, y, z, u, v; };

    // Rayon = far plane du renderer (flt_7FFEA0 = g_GfxRenderer+136). Le côté du cube vaut
    // radius/sqrt(3) (@0x6a8f9b). Invalide le cache de géométrie si la valeur change
    // (miroir du test @0x6a8f88 sur this+210).
    void  SetRadius(float radius);
    float Radius() const { return radius_; }

    // Injecte les 6 textures de face (NON possédées, cycle de vie au propriétaire). Ordre =
    // ordre de dessin de la boucle @0x6a93b1 (this+13, pas 52 o) : voir le tableau dans le
    // .cpp pour l'orientation de chaque face. `faces == nullptr` remet les 6 slots à NULL.
    void               SetFaceTextures(IDirect3DTexture9* const faces[6]);
    IDirect3DTexture9* FaceTexture(int i) const;

    // Réplique du gate d'origine (Gfx_BeginFrame `a2 && *a2` + rayon posé par Cam_SetProjection) :
    // true si le rayon est > 0 ET au moins une texture de face est fournie. Sans ça, le cube
    // resterait inerte (le vrai build ne l'atteint jamais — cf. bandeau). Aide l'appelant à
    // décider s'il vaut la peine d'émettre le dessin.
    bool IsActive() const;

    // Dessine le cube centré sur camPos avec la séquence d'états EXACTE d'Env_RenderSkyCube.
    // fogEnabled = miroir de dword_7FFEA4 (g_GfxRenderer+140) : encadre le dessin d'un
    // disable/enable de D3DRS_FOGENABLE. No-op si dev == nullptr ou radius <= 0.
    // NB : reproduit UNIQUEMENT les états que le binaire modifie (voir .cpp). Les états non
    // touchés (cull, texture-stage) sont hérités du pipeline appelant, comme dans l'original.
    void Render(IDirect3DDevice9* dev, const float camPos[3], bool fogEnabled);

    // Accès géométrie (inspection / tests). 24 sommets = 6 faces x 4.
    const Vertex* Vertices() const { return verts_; }

private:
    // Reconstruit les 24 sommets si le rayon a changé. Env_RenderSkyCube @0x6a8f9b..0x6a92c6.
    void rebuild();

    float radius_      = 0.0f;   // flt_7FFEA0 (far plane) ; 0 = inactif (image statique = 0)
    float cacheRadius_ = -1.0f;  // this+210 (octet 0x348) : dernier rayon construit (@0x6a92c6)
    IDirect3DTexture9* faceTex_[6] = { nullptr }; // this+13, pas 52 o (1er dword de chaque face)
    Vertex verts_[24] = {};      // this+211 (6 faces x 4 sommets), 20 o/sommet
};

} // namespace ts2::gfx

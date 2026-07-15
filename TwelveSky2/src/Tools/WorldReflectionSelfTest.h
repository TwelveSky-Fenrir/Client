// Tools/WorldReflectionSelfTest.h — outil TEMPORAIRE de verification empirique (mission
// "EXTENSION OMBRE/REFLET", 2026-07-14).
//
// A SUPPRIMER apres verification : ne fait PAS partie du client livre. Objectif : prouver,
// via un VRAI device D3D9 + le VRAI chemin WorldRenderer::Render() (Scene/WorldRenderer.h,
// NON MODIFIE dans sa logique, seulement dans la garde reflectionEligible ajoutee par cette
// meme mission), que :
//   1. Char_DrawReflection (drawReflectionOverlay) se declenche REELLEMENT pour un monstre
//      actif a portee (<=300 u du joueur local, hors near-cull camera) ;
//   2. NE se declenche PAS pour un joueur actif dans les MEMES conditions de distance
//      (confirmant la restriction reflectionEligible=monstre-seulement, fidele au fait que
//      Char_DrawReflection 0x581090 n'a qu'un seul appelant reel dans le binaire, a
//      l'interieur de la boucle monstre de Scene_InGameRender -- cf. bandeau
//      Scene/WorldRenderer.h).
// N'invente AUCUNE entite reseau : peuple directement game::g_World (le meme mecanisme
// que g_World est deja rempli par EntityManager en production, juste sans passer par
// Net_RecvDispatch) avec un joueur local + un joueur distant + un monstre, tous actifs et
// proches, pour que la difference de comportement soit observable dans UNE SEULE capture.
#pragma once

namespace ts2::tools {

// `seconds` = duree reelle (secondes) pendant laquelle la fenetre reste affichee apres mise
// en place de la scene, pour laisser le temps a une capture d'ecran externe.
// `width`/`height` = resolution reelle de la fenetre de test (defaut 1024x768 si <=0).
int RunWorldReflectionSelfTest(int seconds, int width = 0, int height = 0);

} // namespace ts2::tools

// Tools/WorldSelfTest.h — harnais `-worldtest` : VOIR le personnage DANS le monde 3D
// (terrain d'une zone reelle + self), sans serveur.
//
// Reproduit la RECETTE A du recon in-world (via SceneManager, la plus fidele) : charge une
// vraie zone (Z%03d.* — terrain .WG, collision .WM, objets .WO/.WP, atmosphere .ATM) par le
// flux EnterWorld (World_LoadZoneResource 0x4DCB60), injecte un self a l'indice 0 de
// game::g_World.players (apparence body+68/72/76/80), FORCE la scene InGame (ce que fait
// normalement Pkt_EnterWorld 0x464160 cote serveur), puis rend + capture le back-buffer en PNG.
// Meme methode de verification visuelle que -charselecttest (cf. memoire ts2-3d-character-visible).
//
// AUCUNE cle DRM requise (l'atmosphere SilverLining n'est pas liee). AUCUN socket ouvert.
#pragma once

namespace ts2::tools {

// -worldtest [seconds] [zoneId] [selfX] [selfY] [selfZ]
//   seconds : duree de maintien de la fenetre (0/def -> borne par frames internes).
//   zoneId  : zone a charger (def 1 = Z001, presente sur disque).
//   selfX/Y/Z : position du self dans le monde (def 0,0,0 ; a ajuster sur la bbox du terrain).
// width/height : def kRefWidth/kRefHeight.
int RunWorldSelfTest(int seconds, int zoneId, float selfX, float selfY, float selfZ,
                     int width, int height);

} // namespace ts2::tools

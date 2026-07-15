// Game/ZoneGroup.h — table de groupement zoneId -> groupe régional/faction.
//
// Réécriture C++ PROPRE (byte-exact sur la table) de :
//   World_ZoneIdToGroup   EA 0x4DC260 (int __cdecl/__stdcall(int zoneId) -> int groupe, -1 si inconnu)
//
// Aucun appelant statique n'a été retrouvé dans l'IDB (mcp__idaTs2__xrefs_to sur 0x4DC260 et
// sur le symbole "World_ZoneIdToGroup" renvoient tous deux 0 xref). La fonction est nommée et
// commentée dans l'IDB ("[asset] map zone/map id to group/category code") par une passe de
// renommage antérieure au projet — nom et commentaire conservés tels quels. L'absence de
// référence statique suggère soit un appelant via pointeur de fonction/table non résolu par
// Hex-Rays, soit du code mort conservé pour compatibilité de build ; à confirmer en dynamique
// si besoin (breakpoint sur 0x4DC260 via x32dbg).
//
// RÔLE OBSERVÉ DU GROUPE (déduction indirecte, à falsifier si un appelant est retrouvé) :
// Les identifiants de ville de faction retournés par Game/MapWarp.h::FactionTownNpcId(element)
// tombent chacun dans un groupe DISTINCT de cette table :
//   element 0 (faction A) -> ville NPC   1 -> ZoneIdToGroup(1)   == groupe 1 (zones 1..4)
//   element 1 (faction B) -> ville NPC   6 -> ZoneIdToGroup(6)   == groupe 2 (zones 6..9)
//   element 2 (faction C) -> ville NPC  11 -> ZoneIdToGroup(11)  == groupe 3 (zones 11..14)
//   element 3 (observateur) -> ville NPC 140 -> ZoneIdToGroup(140) == groupe 6 (zone 140 seule)
// Cette coïncidence (les 4 "capitales" de faction ouvrent chacune un groupe séparé, et les
// zones satellites de chaque capitale partagent son groupe) est cohérente avec un usage de
// GROUPEMENT RÉGIONAL PAR CONTINENT/FACTION DE DÉPART (zones 1-4/6-9/11-14 = 3 continents
// natals + leurs zones attenantes). Les autres groupes (4,5,7,8,9) rassemblent des zones sans
// capitale de faction associée (zones "communes"/donjons/événementielles — ex. groupe 5 = très
// grand nombre de zones 49..174, plausiblement "monde ouvert commun" ; groupe 7 = zones
// 200/201/297..299, plausiblement zones de guerre de guilde/nation ; groupe 9 = 319..323,
// plausiblement un pack de donjons instanciés). Usage précis (restriction PvP ? canal de chat
// régional ? résolution de warp ? filtrage anti-triche GameGuard, hors périmètre ?) NON
// confirmé faute d'appelant retrouvé — // TODO(reverse) si un xref apparaît après re-analyse.
//
// PÉRIMÈTRE : pure table de données (LOGIQUE, pas de rendu) — aucun écart avec le binaire.
#pragma once
#include <cstdint>

namespace ts2::game {

// World_ZoneIdToGroup 0x4DC260 — table figée (switch/case fidèle, ordre d'origine non
// significatif car simple lookup). Retourne -1 (comportement du `default` d'origine) pour
// tout zoneId non listé dans la table.
int32_t ZoneIdToGroup(int32_t zoneId);

} // namespace ts2::game

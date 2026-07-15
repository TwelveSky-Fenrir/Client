// UI/PanelSkin.h — fond de panneau texturé "best effort" pour les fenêtres UI
// (ts2::ui), avec repli automatique sur UiContext::FillRect si la texture est
// indisponible. Ne modifie NI Dialog NI UiContext (UI/UIManager.h) : c'est un
// petit utilitaire additif, partagé par les 11 fenêtres listées dans la mission
// « fonds de panneau réels » pour éviter de dupliquer 11 fois la même logique
// charge-une-fois + repli.
//
// -----------------------------------------------------------------------------
// PROVENANCE DES TEXTURES — MÉTHODOLOGIE D'ÉLIMINATION (session sans accès IDA) :
// -----------------------------------------------------------------------------
// Aucun outil MCP `idaTs2` (decompile/xrefs_to/find_regex) n'était disponible
// dans cette session pour retrouver par xref les chemins de fichiers .IMG
// utilisés par les fonctions UI_*_Init/Open du binaire. Le mapping fichier ->
// fenêtre ci-dessous n'est donc PAS confirmé par désassemblage : il résulte
// d'une analyse STATISTIQUE des fichiers .IMG réels (script d'inspection ad hoc,
// cf. Docs/TS2_IMG_FORMAT.md pour le format d'enveloppe), documentée pour
// permettre une vérification/correction future via IDA.
//
// Étape 1 — identifier le dossier atlas UI parmi G03_GDATA/D01_GIMAGE2D/00N
// (005 = tables de données, déjà connu, cf. TS2_IMG_FORMAT.md §4) :
//   001 (3446 fichiers) : tailles TRÈS hétérogènes (10x10 .. 1024x168) — profil
//                         d'un atlas UI (boutons/cadres/bandeaux/panneaux de
//                         tailles variées), PAS un jeu d'icônes uniforme.
//   002 (3103 fichiers) : quasi tout 51x51 ou 25x25 -> icônes d'objets (grille).
//   003 (755 fichiers)  : quasi tout 25x25 ou 50x50 -> icônes (compétences ?).
//   004 (150 fichiers)  : UNE seule taille (150x79) répétée 150/150 fois.
//   006 (126 fichiers)  : UNE seule taille (782x494) répétée 126/126 fois.
//   007 (79 fichiers)   : tuiles 25x25 indexées x,y (nommage 007_XXXYYY.IMG) ->
//                         tuiles de minimap/terrain, pas des panneaux.
//   008 (339 fichiers)  : UNE seule taille (725x459) répétée 337/339 fois.
//   -> 004/006/008 sont chacun un plan d'images PLEIN ÉCRAN à taille UNIQUE
//      (splash art / portraits / cinématiques probables), pas des fonds de
//      fenêtre redimensionnables. 001 est retenu par élimination : c'est le
//      SEUL dossier dont la distribution de tailles est compatible avec un
//      atlas UI multi-éléments (preuve renforcée ci-dessous).
//
// Étape 2 — repérer dans 001 les tailles répétées à INDICES NON CONSÉCUTIFS
// (signature d'un gabarit de fond de fenêtre réutilisé par plusieurs dialogues
// distincts, PAR OPPOSITION à une taille répétée à indices consécutifs, qui
// trahit plutôt une SÉQUENCE D'ANIMATION empaquetée par le même outil) :
//   (252,440) : 63 occurrences, indices étalés sur tout le dossier (300..4345)
//               -> gabarit de fenêtre ÉTROITE/HAUTE dominant (le plus probable
//                  pour la majorité des ~38-63 dialogues du client, cf. le
//                  commentaire "registre d'environ 38 dialogues singletons"
//                  de UI/UIManager.h).
//   (400,440) : 12 occurrences, indices adjacents (3636..3647) — probablement
//               une famille de dialogues exportés consécutivement (PAS une
//               animation : 400x440 est une taille bien trop grande pour des
//               frames d'effet).
//   (446,440) : 5 occurrences non consécutives (1165,1338,1341,4112,4142).
//   (702,488) : 3 occurrences non consécutives (1491,1672,3263) — gabarit large.
//   (468,128) : 6 occurrences non consécutives — gabarit LARGE/COURT (bandeau
//               de dialogue, cohérent avec une boîte de texte PNJ).
//   (668,229)x33 consécutifs et (250,250)x26 consécutifs sont EXCLUS (très
//   probablement des séquences d'animation, pas des fonds de panneau).
//
// Étape 3 — chaque fenêtre listée reçoit le gabarit dont le RATIO largeur/
// hauteur se rapproche le plus du sien (cf. constantes kPanelW/kPanelH de
// chaque .h) ; l'indice précis dans le cluster retenu est choisi arbitrairement
// (non confirmé individuellement) pour éviter que deux fenêtres partagent
// exactement le même fichier quand le pool le permet. Repli AUTOMATIQUE et
// SILENCIEUX (juste un TS2_WARN) sur le rectangle plein coloré existant si :
//   - le fichier n'existe pas / ne se décode pas comme texture DXT ;
//   - le device D3D9 n'est pas encore prêt ;
//   - la création de la texture GPU échoue (device perdu, format non supporté...).
// Aucune de ces fenêtres ne fait planter le rendu dans ces cas : PanelSkin::Draw
// renvoie false et le rectangle plein d'origine (déjà présent dans chaque .cpp)
// continue de s'afficher exactement comme avant cette mission.
#pragma once
#include "UI/UIManager.h"
#include "Gfx/GpuTexture.h"

namespace ts2::ui {

// Fond de panneau texturé paresseux (1 tentative de chargement, mémorisée).
// Usage typique dans Render(), phase Panels, à la place du FillRect de fond :
//   static const PanelSkin s_bg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01165.IMG");
//   s_bg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColPanelBg);
// (le `static` local mémorise la texture GPU une seule fois par process, comme
// pour toute ressource paresseuse côté fenêtres singleton de ce projet).
class PanelSkin {
public:
    explicit PanelSkin(const char* imgRelPath) : path_(imgRelPath) {}

    // Dessine un blit étiré plein cadre (comme UiContext::FillRect, même
    // compensatePos=true) si la texture a pu être chargée ; sinon appelle
    // ctx.FillRect(x,y,w,h,fallbackColor) et renvoie false. Phase Panels
    // uniquement (no-op hors cette phase, comme FillRect/DrawFrame).
    bool Draw(const UiContext& ctx, int x, int y, int w, int h, D3DCOLOR fallbackColor) const;

private:
    void EnsureLoaded(const UiContext& ctx) const;

    const char*             path_;
    mutable gfx::GpuTexture  tex_;
    mutable bool             tried_ = false; // chargement tenté (succès ou échec) une seule fois
};

} // namespace ts2::ui

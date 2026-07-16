// UI/GameHud.cpp — implémentation du HUD en jeu (cGameHud), squelette fonctionnel.
// Ancres : cGameHud_InitLayout 0x62A5B0, cGameHud_Render 0x64A900,
//          cGameHud_OnMouseDown 0x62B080. Voir Docs/TS2_CLIENT_SHELL.md §2.3.
//
// ⚠️ CORRECTION DE PÉRIMÈTRE (décompilation du 2026-07-14, via idaTs2 direct
// JSON-RPC http://127.0.0.1:13337/mcp — tools/call "decompile") :
// `cGameHud_Render 0x64A900` (16465 o, taille conforme à ce qui était attendu)
// s'avère être le rendu de la GRANDE FENÊTRE « personnage » (onglets
// équipement/carquois/sac 8x8/arbre de compétences/éléments/entrepôt-loot,
// commutés sur this[226]=1..10) — cf. déjà Docs/TS2_CLIENT_SHELL.md L.286 et
// L.349. Cette fonction NE CONTIENT AUCUNE barre HP/MP NI AUCUNE barre de
// quickslots : elle ne se déclenche que si this[175] (fenêtre ouverte) est
// vrai, et son overlay de vitales/action-bar n'existe pas dans ce code.
//
// Les VRAIES barres HP/MP + la VRAIE barre d'action toujours affichées (ce que
// ce fichier modélise sous le nom `GameHud`) sont rendues par une fonction
// différente, plus grosse et non documentée jusqu'ici :
// `UI_GameHud_Render 0x67A3C0` (thiscall, ~50,6 Ko décompilés, guardée par
// `g_SceneMgr==6 && g_SceneSubState==4`, commentaire IDA « render full in-game
// HUD (bars, buttons, chat, minimap) »). C'est CETTE fonction qui a servi à
// extraire les constantes ci-dessous ; chaque valeur cite son EA. Les deux
// fonctions ont probablement été confondues au moment d'écrire ce squelette
// (même préfixe conceptuel « HUD principal » mais deux singletons UI
// distincts : le classeur cGameHud 0x1839568 vs. le HUD toujours-visible).
// ⚠️ TEXTURE RÉELLE DU CADRE VITALES (session du 2026-07-14, idaTs2 JSON-RPC direct
// http://127.0.0.1:13337/mcp — le MCP idaTs2 n'était pas exposé comme outil Claude
// dans cette session, mais restait joignable en HTTP direct ; tous les appels
// ci-dessous sont donc de la RE statique vérifiée, pas une déduction) :
//
// Méthode d'identification pas à pas :
//  1. decompile(0x67A3C0) [UI_GameHud_Render] confirme l'appel cité en tête de
//     fichier : `Sprite2D_Draw((int)&unk_8EC114, 0, 0)` @0x67A43D.
//  2. decompile(Sprite2D_Draw 0x4D6B20) -> appelle Sprite2D_EnsureLoaded(this).
//     decompile(Sprite2D_EnsureLoaded 0x4D6A90) -> appelle Tex_LoadCompressedDDS
//     avec ecx=this+0x68 (objet texture GPU) et UN SEUL argument stack = this+4
//     (vérifié par disasm() : `add ecx,4 / push ecx` puis `add ecx,68h`). Donc le
//     champ +4 de Sprite2D EST le buffer de chemin fichier (pas un pointeur vers
//     un chemin ailleurs) — layout confirmé : loaded@0, filename@4 (0x64 o),
//     SpriteTexture@0x68 (44 o) => taille structure = 4+100+44 = 148 o. C'est
//     EXACTEMENT le layout déjà modélisé par gfx::Sprite2D (Gfx/SpriteBatch.h).
//  3. xrefs_to(0x8EC114) : seulement 2 fonctions le LISENT (Render/OnMouseDown),
//     aucune écriture directe -> le champ filename est rempli dynamiquement au
//     démarrage, pas par un littéral statique dans l'image.
//  4. xrefs_to(0x8E8B50) : >100 xrefs dans presque tout le module UI/Scene (HUD,
//     inventaire, boutique, intro, login...) -> confirme que 0x8E8B50 est la BASE
//     d'un immense tableau de Sprite2D partagé par toute l'UI. Calcul :
//     (0x8EC114 - 0x8E8B50) / 148 = 93 EXACTEMENT -> unk_8EC114 = élément #93 de
//     ce tableau.
//  5. find_regex("GIMAGE2D") révèle les gabarits de chemin embarqués, dont
//     "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG" @0x7A7540.
//  6. xrefs_to(0x7A7540) -> une seule fonction : Sprite2D_BuildPath 0x4D68E0.
//     decompile() : `case 1: Crt_Vsnprintf(this+4, "...\\001\\001_%05d.IMG", a3+1)`
//     -> categorie 1 = dossier 001, index fichier = a3+1 (a3 = index du slot).
//  7. xrefs_to(Sprite2D_BuildPath) -> AssetMgr_InitAllSlots 0x4DEB50, seule
//     fonction appelante. decompile() donne la boucle exacte :
//       `for (i = 0; i < 4500; ++i) Sprite2D_BuildPath(this + 148*i + 32, 1, i, 0);`
//     Donc l'élément i du tableau catégorie-1 est à l'adresse `this + 148*i + 32`.
//     En posant élément(93) == unk_8EC114 == 0x8EC114 : this = 0x8EC114 - 148*93
//     - 32 = 0x8E8B30 (cohérent : this+32 = 0x8E8B50, base EXACTE du tableau
//     trouvée en (4) — les deux méthodes convergent).
//  8. Numéro de fichier = a3+1 = 93+1 = 94 -> chemin final :
//       G03_GDATA\D01_GIMAGE2D\001\001_00094.IMG
//     Fichier PRÉSENT sur disque (5453 o) ; décodé avec RE/img_extract.py :
//     DXT3, dimensions embarquées 214x69 (dw0=0xD6, dw1=0x45 dans le payload
//     inflaté) — cohérent en ordre de grandeur avec le rect `layout_.frame`
//     { 230, 80 } déjà estimé par un agent précédent (marge pour les 4 barres).
//     En-tête vérifié bit-à-bit conforme au layout documenté dans
//     Gfx/GpuTexture.h (width@+0, height@+4, FourCC@+28="DXT3", tailleImage@+32,
//     "DDS "@+36) : GpuTexture::CreateFromImgFile s'applique donc SANS
//     adaptation.
//
// Chargement dans GameHud::Init (voir plus bas) : asset::ImgFile::Load(chemin)
// -> gfx::GpuTexture::CreateFromImgFile. Échec (fichier manquant/format
// inattendu) => vitalsFrameTex_ reste invalide => DrawVitalsFrame() retombe sur
// l'ancien rect plein coloré (kFrameBg/kFrameBorder), comportement inchangé.
// ⚠️ AUDIT DE COMPLÉTUDE vs Docs/TS2_UI_GAMEHUD_RENDER.md (mission du 2026-07-14) :
//
// Comparaison exhaustive des ~17 sections du doc contre l'état RÉEL de ce fichier
// avant cette passe (voir le rapport de mission pour le tableau complet section par
// section). Deux constats majeurs ont motivé les changements ci-dessous :
//
//  1. UI/ChatWindow.h (§13, fenêtre de chat/système bas-gauche) était un widget
//     COMPLET (deux anneaux, onglets, saisie, SyncFromMessageLog) mais n'était
//     instancié NULLE PART dans ClientSource — ni ici, ni dans UI/GameWindows.h, ni
//     dans Scene/SceneManager.cpp. Le pipeline de données (game::g_Client.msg,
//     alimenté par ~20 handlers réseau) tournait donc dans le vide : aucune ligne de
//     chat/système n'était jamais affichée. Câblé ci-dessous (rendu + sync +
//     routage souris des onglets) — la saisie clavier RESTE non câblée, cf. TODO
//     précis plus bas (changement requis dans SceneManager.cpp, hors périmètre de
//     cette mission).
//
//  2. UI/ConsumableBarWindow.h (§14, contrepartie pixel de
//     Game/ConsumableBarLogic.h) était ÉGALEMENT écrit et complet (rendu 2 passes
//     via UiContext, teinte « objet manquant », badge de quantité possédée via
//     game::InventoryCount) mais lui aussi jamais instancié : DrawQuickSlotFrames()
//     ci-dessous ne dessinait que des rects placeholder sans lien avec
//     l'inventaire réel. Câblé ci-dessous en remplacement (DrawQuickSlotFrames
//     conservée comme repli si l'allocation échoue, cf. Render()).
//
// Éléments du doc déjà couverts AILLEURS (pas de changement ici, juste confirmé
// lors de l'audit — voir rapport de mission pour le détail) :
//   §9  (grille de buffs)           -> UI/BuffStatusPanel.h (mission précédente)
//   §12 (mini-carte)                -> UI/MinimapWidget.h (mission précédente)
//   §16 (panneau statut bas-droite) -> UI/BuffStatusPanel.h (mission précédente)
//   §17 (panneau permanent)         -> UI/QuestTrackerWindow.h, via UI/GameWindows.h
//                                      (le CALLOUT transitoire Quest_DrawTracker 0x510FC0 est
//                                      DISTINCT et a été câblé lors de CETTE mission, cf.
//                                      bandeau « CÂBLAGE CALLOUT MARQUEUR DE QUÊTE » plus bas)
//
// Éléments du doc SANS câblage possible cette passe faute de DONNÉES modélisées
// côté Game/GameState.h ou Game/ClientRuntime.h (pas un problème de rendu, un
// problème de source de vérité manquante — TODO précis dans le rapport de mission) :
//   §1  barre EXP + barre Maîtrise élémentaire (le cadre HP/MP reste inchangé)
//   §3  panneau stats objet sélectionné / §4 panneau talisman (pas de concept de
//       « sélection d'inventaire » exposé globalement)
//   §5  badge buff spécial / §6 badge bonus devise ligne 2 (globals non modélisés)
//   §7  plaques de cible verrouillée ×2 (pas de « cible courante résolue » exposée :
//       Game/CombatSystem.h ne porte que des offsets bruts +288/+292 sur un blob
//       entité opaque, pas un accesseur utilisable pour l'UI)
//   §10 bouton détail + liste de 76 compteurs de bonus cumulés
//   §11 panneaux de morph spécial (montures/véhicules narratifs)
//   §15 rangée de ~20 boutons de menu (icônes/actions non identifiées)
//
// Seul gain « gratuit » ajouté (donnée déjà disponible, juste jamais affichée) :
// le montant de devise (§2, game::g_World.self.currency, déjà tenu à jour par
// Net/GameVarDispatch.cpp cas 3 et Net/CharStatDeltaDispatch.cpp) — voir
// DrawTextPass() plus bas.
//
// ⚠️ CORRECTION D'AUDIT (mission « CADRES ALLIANCE/GROUPE », 2026-07-14) : la ligne
// ci-dessus (audit précédent, même journée) affirmait que §8 (plaques alliance/groupe,
// EA 0x67B891-0x67BD54) était déjà couvert par UI/PartyWindow.h — C'ÉTAIT FAUX. Relecture
// intégrale de UI/PartyWindow.h/.cpp : ce widget est un panneau « Groupe » qui lit
// game::g_World.partyRoster (PAS allianceRoster) via un mécanisme de données ENTIÈREMENT
// différent (adresses Var dword_1687458/168745C écrites par PartyMemberHpSet/Update, PAS
// une résolution par nom dans g_World.players), et son propre bandeau documente que sa
// position haut-gauche est une INVENTION non prouvée par le désassemblage (aucune fonction
// UI_PartyWin_*/UI_PartyHud_* n'existe dans l'IDB). Le §8 réel, lui, est PROUVÉ par EA dans
// UI_GameHud_Render lui-même (condition `Crt_Strcmp(g_AllianceRosterNames, &String) != 0`,
// position exacte (0, 155+50*i)) et n'a donc AUCUN équivalent câblé avant cette mission.
// Câblé ci-dessous (DrawAllianceFramePanels/DrawAllianceFrameText, voir Render()) à partir
// de game::g_World.allianceRoster (déjà peuplé par Net/GameHandlers_PartyGuild.cpp, cf.
// Game/GameState.h::AllianceRoster) recoupé par NOM avec game::g_World.players[] (même
// méthode que la plaque de cible §7, non câblée) pour en tirer PV/PM RÉELS. LIMITE FIDÈLE
// (pas d'invention) : PlayerEntity ne porte aucun maxHp/maxMp pour une entité DISTANTE
// (seul game::g_World.self les a, via StatEngine) — la jauge d'un membre non-self connu est
// donc dessinée grisée « sans donnée de maximum » plutôt qu'avec un ratio halluciné (même
// politique de dégradation honnête que UI/PartyWindow.cpp pour la PM des coéquipiers).
//
// ⚠️ CÂBLAGE « OVERLAY DEBUG TEMPS GM » (§17, mission 2026-07-14) : élément identifié mais
// non câblé par les audits précédents (cf. ligne "§17 overlay debug GM (trivial ... non
// fait ici)" retirée ci-dessus). Vérifié par désassemblage frais via idaTs2 direct JSON-RPC
// (http://127.0.0.1:13337/mcp, tools/call "disasm" sur 0x6868c0-0x686955, confirmé par
// find_regex("NowTime") -> aNowtimeDDDDS @0x7baf78, un seul xref, DANS UI_GameHud_Render
// elle-même, pas dans Quest_DrawTracker 0x6868AB comme le doc le laissait entendre) :
// condition binaire EXACTE `dword_1676108 > 0 && g_GmAuthLevel > 0` (0x6868e8-0x6868f8),
// format littéral "NowTime : %d / %d %d:%d %s", ancre fixe (10,150) (push 0Ah/push 96h
// @0x686934/0x68692f, UI_DrawNumberValue 0x53FCC0 -> UI_DrawText(a1=texte, x, y, ...)).
// Les 4 entiers (dword_167610C/1676110/1676114/1676118) sont DÉJÀ décodés fidèlement par
// ApplySetGameVar cas 98 (Net/GameVarDispatch.cpp, depuis dword_1676108) via le même
// mécanisme g_Client.Var(adresse) que le reste de la longue traîne — rien à ajouter côté
// dispatch réseau, uniquement l'affichage manquait. Le champ %s (byte_167611C) reste une
// chaîne vide fidèle : Crt_StringInit (0x75CAB0) qui le remplit dans le binaire est un
// stub TODO(ui) non porté (cf. GameVarDispatch.cpp), donc g_Client.Blob(0x167611C, 64) n'est
// jamais écrit ici non plus — comportement dégradé honnête, pas une invention de texte.
// Voir DrawDebugTimeOverlay() plus bas (appelée depuis Render(), gate sur net::g_GmAuthLevel
// != 0, même convention que Scene/SceneManager.cpp::host.IsGm).
//
// ⚠️ CÂBLAGE « CALLOUT MARQUEUR DE QUÊTE » (§17, mission 2026-07-14, vérification demandée
// explicitement — le §17 ci-dessus mentionnait UNIQUEMENT UI/QuestTrackerWindow.h, ce qui
// conflait DEUX fonctions binaires distinctes) : Docs/TS2_UI_GAMEHUD_RENDER.md §17 documente
// `Quest_DrawTracker((int)&g_PlayerCmdController)` (EA 0x6868AB, décompilé pour cette
// vérification) comme « hors périmètre ». Or ce n'est PAS le panneau permanent haut-droite
// (celui-là est UI_CharListWnd_*/QuestTbl, câblé par UI/QuestTrackerWindow.h via
// UI/GameWindows.h) : c'est une PASTILLE TRANSITOIRE (icône NPC + 3 lignes de texte) qui ne
// s'affiche QUE tant que `*(this+51576)` (QuestMarkerState::active) est vrai — armée par
// game::Quest_UpdateMarkerTimer (Game/ComboPickupTick.h, DÉJÀ câblé dans le tick de jeu par
// Scene/SceneManager.cpp::host.UpdateQuestMarkerTimer, cf. rapport de mission précédent) sur
// détection d'un nouvel objectif ou d'un objectif rempli, et qui retombe après ~30s (ou à la
// fermeture de l'entrepôt cible). AUDIT : questMarker.active passait à `true` mais AUCUN code
// ne le LISAIT jamais pour du rendu -> la pastille ne s'affichait donc JAMAIS, quel que soit
// l'état réel de la quête (le tick tournait dans le vide, même symptôme que ChatWindow/
// ConsumableBarWindow documenté plus haut). Câblé ci-dessous : DrawQuestMarkerPanel() (passe
// sprites) + DrawQuestMarkerText() (passe police autonome, même politique que
// DrawDebugTimeOverlay()) — voir GameHud.h::questMarker_ pour la réserve sur l'instance
// d'état PROPRE à GameHud (Scene/SceneManager.cpp non modifié, cf. bandeau du membre).
// LIMITES FIDÈLES (pas d'invention) : l'icône réelle (SkillDefTbl_GetRecord(mNPC, ...)+1320
// -> g_AssetMgr_UiAtlasSlots) et le texte de variante (StrTable003_Get(mZONENPCINFO, ...))
// dépendent de tables NPC/chaînes HORS PÉRIMÈTRE (mêmes tables que celles déjà documentées
// « non modélisées » par Game/QuestSystem.h et UI/QuestTrackerWindow.h) : remplacés par une
// pastille colorée (même politique que §8 alliance ci-dessus) et par les IDENTIFIANTS BRUTS
// déjà exposés par game::QuestProgressState/QuestStepRecord (zoneId/npcQuestId/targetId/
// required), exactement comme UI/QuestTrackerWindow.cpp::BuildLayout. Position réelle
// (`this+24 - 352/196` selon `dword_184C648`, ni l'un ni l'autre modélisés ailleurs dans
// ClientSource) non plus prouvable statiquement : ancrée haut-centre, sous la mini-carte/
// devise, même politique de « simplification assumée, jamais bloquante » que §2 ci-dessus.
#include "UI/GameHud.h"
#include "Game/GameState.h"      // game::g_World (self.hp/maxHp/mp/maxMp/level/currency)
#include "Game/GameDatabase.h"   // game::GetLevelInfo/LevelInfo (§1 barre EXP, mission W4-F2)
#include "Game/ActionStateMachine.h" // game::CharActionState (CastSlot0-2/Channel -> indicateur de cast §16, mission 2026-07-14)
#include "Game/ClientRuntime.h"  // game::g_Client.msg (MessageLog -> ChatWindow, mission 2026-07-14)
#include "Game/StringTables.h"   // game::g_Strings.zoneNames (§17 callout marqueur de quête, mission 2026-07-14)
#include "Net/NetClient.h"       // net::g_GmAuthLevel (§17 overlay debug GM, mission 2026-07-14)
#include "Core/Log.h"
#include "Asset/ImgFile.h"    // chargement .IMG (enveloppe zlib + FourCC DXT)
#include "UI/BuffStatusPanel.h" // §9 grille de buffs + §16 panneau bas-droite (mission 2026-07-14)
#include "UI/ConsumableBarWindow.h" // §14 quickbar réelle (mission 2026-07-14, voir bandeau ci-dessus)

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace ts2::ui {

// --- Palette du HUD (ARGB) ---------------------------------------------------
namespace {
constexpr D3DCOLOR kFrameBg      = 0xC0101018u; // fond du cadre (translucide)
constexpr D3DCOLOR kFrameBorder  = 0xFF3A3A48u; // liseré du cadre
constexpr D3DCOLOR kPortraitBg   = 0xFF181820u; // fond du mini-cadre portrait
constexpr D3DCOLOR kBarBorder    = 0xFF000000u; // contour des barres
constexpr D3DCOLOR kHpBg         = 0xFF240808u; // fond barre de vie
constexpr D3DCOLOR kHpFill       = 0xFFC42828u; // remplissage vie (rouge)
constexpr D3DCOLOR kMpBg         = 0xFF08081Eu; // fond barre de mana
constexpr D3DCOLOR kMpFill       = 0xFF2A56C6u; // remplissage mana (bleu)
constexpr D3DCOLOR kSlotBg       = 0xB0000000u; // fond d'un quickslot
constexpr D3DCOLOR kSlotBorder   = 0xFF404050u; // contour d'un quickslot
constexpr D3DCOLOR kSlotFilled   = 0xFF2E7D46u; // marqueur « slot assigné » (placeholder icône)
constexpr D3DCOLOR kTextColor    = 0xFFFFFFFFu; // texte principal (blanc)
constexpr D3DCOLOR kTextDim      = 0xFFBFBFBFu; // texte secondaire (numéros de slot)

// --- Barre EXP + Maîtrise élémentaire (§1, UI_GameHud_Render 0x67A59E-0x67A782) ----
// Style libre (ARGB non prouvé — aucune table de couleurs modélisée côté client).
constexpr D3DCOLOR kExpBg       = 0xFF201028u; // fond barre d'expérience (violet sombre)
constexpr D3DCOLOR kExpFill     = 0xFF9A46E0u; // remplissage EXP (violet)
constexpr D3DCOLOR kMasteryBg   = 0xFF08201Cu; // fond barre de maîtrise (vert sombre)
constexpr D3DCOLOR kMasteryFill = 0xFF2CC888u; // remplissage maîtrise (vert)
constexpr int      kMasteryMax  = 3000;        // push 0BB8h @0x67A737 ; dbl_7EDAE8 = 3000.0

// §1 EXP : progression/span exacts extraits de UI_GameHud_Render 0x67A59E (disasm
// vérifié cette mission). Getters LevelTable_GetId 0x4C2930 -> LevelInfo::expCumul (+4),
// LevelTable_GetMinExp 0x4C2960 -> LevelInfo::expNext (+8) (décompilés : record = 11
// dwords, GetId=record[+1], GetMinExp=record[+2]). Lecture via g_Client.VarGet — AUCUN
// champ SelfState ajouté (Game/GameState.h en lecture seule, cf. tâche W4-F2).
void ComputeExpProgress(int level, int levelBonus, int& outProgress, int& outSpan) {
    if (levelBonus >= 1) {
        // Renaissance (branche jge @0x67A59E->0x67A618) : span =
        // maybe_GameHud_GetQuickSlotItemId(mLEVEL, levelBonus) = sous-table mLEVEL+1594
        // (dwords), NON portée dans LevelInfo -> barre vide en renaissance.
        outProgress = game::g_Client.VarGet(0x16731B4); // dword_16731B4 @0x67A62F
        outSpan     = 0; // TODO [ancre 0x67A624] : sous-table span renaissance non modélisée
    } else if (const game::LevelInfo* li = game::GetLevelInfo(level)) {
        const int expCumul = li->expCumul;                          // GetId(level) 0x4C2930 = +4
        outProgress = game::g_Client.VarGet(0x16731B0) - expCumul;  // dword_16731B0 - GetId @0x67A608
        // level<145 (0x91) : span = GetMinExp - GetId ; sinon : 2e9 (0x77359400) - GetId.
        outSpan = (level < 145 ? li->expNext : 2000000000) - expCumul; // @0x67A5B8 / 0x67A5EA
    } else {
        outProgress = 0;
        outSpan     = 0;
    }
}

// --- Cadres alliance/groupe (§8, EA 0x67B891-0x67BD54) -----------------------
constexpr D3DCOLOR kAllyFrameBg    = 0xA0141420u; // fond translucide d'une ligne
constexpr D3DCOLOR kAllyFrameBrd   = 0xFF3A3A48u; // liseré (même teinte que le cadre vitales)
constexpr D3DCOLOR kAllyIconOnline = 0xFF3A8A46u; // pastille présence (résolu, vert)
constexpr D3DCOLOR kAllyIconOffline= 0xFF6A6A70u; // pastille présence (introuvable, gris)
constexpr D3DCOLOR kAllyNoData     = 0xA0505058u; // jauge « pas de maximum connu » (grisée)
constexpr D3DCOLOR kAllyNameCol    = 0xFFE8E8F0u;

constexpr int kAllianceMaxRows  = 5;   // g_AllianceRosterNames : slot 0 = leader, 1..4 = membres
constexpr int kAllianceRowY0    = 155; // EA 0x67B891 : ancre (0,155+50*i)
constexpr int kAllianceRowStep  = 50;
constexpr int kAllianceRowW     = 204; // reste à gauche de la grille de buffs §9 (x=220)
constexpr int kAllianceIconSize = 24;
constexpr int kAllianceBarH     = 9;
// Offsets verticaux RELATIFS au haut de ligne, fidèles aux EA du doc (§8) :
// nom @ y=162 (162-155=7), barre PV @ y=180 (180-155=25), barre PM @ y=191 (191-155=36).
constexpr int kAllianceNameDy = 7;
constexpr int kAllianceHpDy   = 25;
constexpr int kAllianceMpDy   = 36;

// --- Callout de marqueur de quête (§17, Quest_DrawTracker 0x510FC0) ----------
constexpr D3DCOLOR kQuestMarkerBg     = 0xD0202028u; // fond translucide (même teinte que §17 tracker)
constexpr D3DCOLOR kQuestMarkerBorder = 0xFF3A3A48u;
constexpr D3DCOLOR kQuestMarkerDone   = 0xFF60FF60u; // icône : objectif rempli (vert)
constexpr D3DCOLOR kQuestMarkerGoing  = 0xFFFFDD66u; // icône : objectif en cours (or)
constexpr D3DCOLOR kQuestMarkerTitle  = 0xFFFFDD66u; // titre (même teinte que QuestTrackerWindow::kColTitle)

constexpr int kQuestMarkerW      = 260; // largeur = même gabarit que QuestTrackerWindow (kPanelW)
constexpr int kQuestMarkerH      = 74;  // titre + 3 lignes
constexpr int kQuestMarkerY0     = 90;  // sous la mini-carte/devise (§2, y<=12), au-dessus des buffs (§9)
constexpr int kQuestMarkerIconSz = 24;
constexpr int kQuestMarkerPadX   = 10;
constexpr int kQuestMarkerPadY   = 8;
constexpr int kQuestMarkerLineH  = 16;

// Fond réel du cadre vitales (élément #93 du tableau Sprite2D catégorie 1,
// identifié par RE statique — voir bandeau de tête du fichier). Chemin littéral
// identique au gabarit binaire "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG" (index
// %05d = 94), pas de préfixe "GameData\" : même convention que
// UI/InventoryWindow.cpp::ResolveItemIconPath (chemins relatifs à la racine
// GameData de lancement du client).
constexpr const char* kVitalsFrameImgPath = "G03_GDATA\\D01_GIMAGE2D\\001\\001_00094.IMG";

// Crée une texture 1x1 opaque blanche (D3DPOOL_MANAGED : survit au device reset).
IDirect3DTexture9* CreateWhiteTexture(IDirect3DDevice9* dev) {
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)) || !tex) {
        return nullptr;
    }
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits) {
        *static_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu; // ARGB blanc opaque
        tex->UnlockRect(0);
    }
    return tex;
}
} // namespace

// =============================================================================
// Init / Shutdown
// =============================================================================
// Définitions HORS LIGNE (déclarées dans GameHud.h) : quickBarWindow_ est un
// std::unique_ptr<ConsumableBarWindow> sur un type seulement forward-déclaré
// dans le header (cycle d'inclusion, cf. bandeau de GameHud.h) — CONSTRUCTEUR et
// destructeur doivent tous deux être instanciés dans UN traducteur qui voit le
// type complet (ici, après #include "UI/ConsumableBarWindow.h" ci-dessus) : un
// constructeur `= default` inline dans le header a aussi besoin du type complet
// (ménage d'exception implicite pour détruire quickBarWindow_ si un membre
// suivant lève à la construction).
GameHud::GameHud()  = default;
GameHud::~GameHud() { Shutdown(); }

bool GameHud::Init(gfx::Renderer& renderer, int screenW, int screenH) {
    Shutdown();

    device_      = renderer.Device();
    rendererPtr_ = &renderer; // cf. UI/GameHud.h::rendererPtr_ (audit UiContext.renderer 2026-07-14)
    screenW_ = screenW;
    screenH_ = screenH;
    if (!device_) {
        TS2_ERR("GameHud::Init : device nul");
        return false;
    }

    // Sprite 2D (D3DXCreateSprite) pour les rects pleins des barres/cadres.
    if (!sprite_.Create(device_)) {
        TS2_ERR("GameHud::Init : SpriteBatch::Create a echoue");
        Shutdown();
        return false;
    }

    // Police par défaut du client (GIGASSOFT_12 ; cf. Font::MakeDefaultDesc).
    gfx::Font::AddTtfResource(false); // best-effort : ignore l'échec si TTF absente
    if (!font_.Init(device_, screenW_, screenH_, /*trVariant=*/false)) {
        TS2_ERR("GameHud::Init : Font::Init a echoue");
        Shutdown();
        return false;
    }

    // Texture blanche teintable (barres/cadres dessinés à plat).
    white_ = CreateWhiteTexture(device_);
    if (!white_) {
        TS2_ERR("GameHud::Init : creation de la texture blanche 1x1 a echoue");
        Shutdown();
        return false;
    }

    // Fond réel du cadre vitales (best-effort ; repli sur le rect plein coloré
    // si le fichier est absent/illisible — voir DrawVitalsFrame). Le reste du
    // HUD (barres, quickslots) n'a pas de sprite .IMG identifié à ce stade et
    // continue d'utiliser les rects pleins (cf. bandeau de tête du fichier).
    {
        asset::ImgFile img;
        if (img.Load(kVitalsFrameImgPath) && img.Kind() == asset::ImgKind::TextureDxt) {
            if (vitalsFrameTex_.CreateFromImgFile(device_, img)) {
                TS2_LOG("GameHud : texture cadre vitales chargee (%s, %ux%u)",
                        kVitalsFrameImgPath, vitalsFrameTex_.Width(), vitalsFrameTex_.Height());
            } else {
                TS2_ERR("GameHud : upload GPU de %s echoue, repli sur rect colore",
                        kVitalsFrameImgPath);
            }
        } else {
            TS2_ERR("GameHud : chargement de %s echoue (kind=%d), repli sur rect colore",
                    kVitalsFrameImgPath, static_cast<int>(img.Kind()));
        }
    }

    InitLayout();

    // Mini-carte (§12) — cf. UI/MinimapWidget.h. Pas de ressource GPU propre à
    // créer (réutilise sprite_/font_/white_ ci-dessus) : Init() ne fait que
    // calculer son layout écran.
    minimap_.Init(screenW_, screenH_);

    // Grille de buffs (§9) + panneau bas-droite (§16) — widget AUTONOME (son
    // propre SpriteBatch + cache de textures, cf. UI/BuffStatusPanel.h) ; on lui
    // passe le device via `renderer` et notre police déjà initialisée ci-dessus
    // (partagée, non possédée par BuffStatusPanel). Best-effort : un échec ne
    // bloque pas l'init du reste du HUD (repli sur pastilles colorées de toute
    // façon si les ressources GPU échouent, cf. bandeau de tête de
    // BuffStatusPanel.h).
    if (!buffPanel_.Init(renderer, &font_)) {
        TS2_ERR("GameHud : BuffStatusPanel::Init a echoue, grille de buffs desactivee");
    } else {
        buffPanel_.SetScreenSize(screenW_, screenH_);
    }

    // Fenêtre de chat (§13, mission 2026-07-14) — widget léger, pas de ressource
    // GPU à créer ici (texture blanche paresseuse propre, cf. ChatWindow.cpp). Ne
    // peut jamais échouer (pas de handle D3D9 à cet appel).
    chatWindow_.SetScreenSize(screenW_, screenH_);

    // Quickbar réelle (§14, mission 2026-07-14) — remplace le rendu placeholder de
    // DrawQuickSlotFrames() ci-dessous. Construction triviale (pas de ressource
    // GPU propre, cf. UI/ConsumableBarWindow.h) : ne peut échouer que par bad_alloc,
    // repli sur nullptr -> Render()/OnMouseDown() retombent sur l'ancien chemin.
    quickBarWindow_ = std::make_unique<ConsumableBarWindow>();

    TS2_LOG("GameHud initialise (%dx%d)", screenW_, screenH_);
    return true;
}

void GameHud::Shutdown() {
    rendererPtr_ = nullptr;
    quickBarWindow_.reset();
    buffPanel_.Shutdown();
    vitalsFrameTex_.Release();
    if (white_) { white_->Release(); white_ = nullptr; }
    font_.Shutdown();
    sprite_.Destroy();
    device_ = nullptr;
}

// =============================================================================
// InitLayout — cGameHud_InitLayout 0x62A5B0 (nom conservé pour l'ancrage
// documentaire), mais VALEURS extraites de UI_GameHud_Render 0x67A3C0, la
// vraie fonction du HUD toujours-affiché (voir bandeau en tête de fichier).
// Chaque rect ci-dessous cite l'EA de son calcul dans le binaire. Décompilé
// via idaTs2 (JSON-RPC direct, tools/call "decompile" sur 0x67A3C0 puis
// 0x64A900) le 2026-07-14.
// =============================================================================
void GameHud::InitLayout() {
    // --- Cadre vitales (portrait + barres HP/MP), ANCRÉ EN HAUT-GAUCHE -------
    // RÉEL : le fond du panneau est dessiné à l'origine écran EXACTE, sans
    // offset — Sprite2D_Draw(&unk_8EC114, 0, 0) @0x67A43D (hit-test juste
    // avant @0x67A41F). Ancien placeholder : bas-gauche { 12, screenH_-100,
    // 232, 88 } — FAUX, le vrai panneau est en haut-gauche, pas en bas.
    // Dimensions w/h non lisibles statiquement (Sprite2D charge sa largeur/
    // hauteur au runtime depuis le .npk, champs +108/+112 nuls dans l'IDB —
    // vérifié via get_int) : estimées à partir de l'étendue logique des 4
    // barres réelles (HP/MP/EXP/Maîtrise, voir plus bas) + marge.
    layout_.frame = HudRect{ 0, 0, 230, 80 };
    const HudRect& f = layout_.frame;

    // Mini-cadre portrait : AUCUN sprite de portrait séparé identifié dans le
    // décompilé (probablement incrusté dans le fond &unk_8EC114 lui-même,
    // hors périmètre — pas de sprite .IMG distinct à charger). Replacé à
    // gauche des barres réelles (celles-ci démarrent à x=57, cf. ci-dessous),
    // hauteur calée sur les 2 lignes HP+MP réelles (14 px de pas chacune).
    // Ancien placeholder : { f.x+8, f.y+12, 64, 64 } (dans un cadre bas-gauche
    // qui n'existe pas côté binaire).
    layout_.portrait = HudRect{ f.x + 4, f.y + 4, 48, 48 };

    // Barre HP : fill dessiné à (57, 8) @0x67A48B ; valeur "%d/%d" alignée à
    // droite, bord droit x=207 @0x67A4F0 (mesuré par UI_MeasureNumberText puis
    // 207-largeur). Sélection de frame = ftol(cur*41.0/max)+95, clampée à 136
    // (donc 41 paliers visuels, dword_1687370[0]/dword_168736C[0]) @0x67A465.
    // Largeur logique retenue = 207-57 = 150 (bord droit du texte = bord droit
    // visuel de la barre) ; hauteur 12 (le pas réel entre lignes est 14 px).
    // Ancien placeholder : { barX dynamique après portrait 64px, f.y+14,
    // barW dynamique, 20 } — mauvaise ancre (bas-gauche) et mauvaise largeur.
    layout_.hpBar = HudRect{ f.x + 57, f.y + 8, 150, 12 };

    // Barre MP : fill à (57, 22) @0x67A540 ; texte droite x=207, y=21
    // @0x67A576-0x67A592. Frame = ftol(cur*41.0/max)+137, clampée à 178
    // (dword_1687378[0]/dword_1687374[0]) @0x67A51A. Ancien placeholder :
    // { barX, f.y+44, barW, 20 }.
    layout_.mpBar = HudRect{ f.x + 57, f.y + 22, 150, 12 };

    // NOTE (constatée mais NON câblée — TODO précis, cf. bandeau de tête du
    // fichier §1 : donnée manquante côté Game/GameState.h, pas une contrainte
    // d'édition de ce header) : le vrai panneau dessine aussi, à la suite des
    // barres HP/MP, une barre d'EXP à (57,36)
    // @0x67A685 (41 paliers 179-220, calcul d'exp restante vs g_SelfLevel/
    // g_SelfLevelBonus @0x67A59E) et une barre de Maîtrise élémentaire à
    // (57,50) @0x67A732 (échelle FIXE /3000 sur dword_168746C — remise à 0 par
    // WE_ResetElementMastery, cf. Docs/TS2_DISPATCHERS.md L.260). Même pas de
    // 14 px, même bord droit texte x=207. À intégrer si Layout est étendu.

    // --- Barre de quickslots, ANCRÉE EN BAS D'ÉCRAN --------------------------
    // RÉEL (toujours dans UI_GameHud_Render 0x67A3C0, PAS dans
    // cGameHud_Render) : boucle de 14 slots — pas 10 — mappant les touches
    // 1..0 (10) PLUS les 4 slots étendus Q/W/E/R (cf. Docs/TS2_CLIENT_SHELL.md
    // §4, DIK 0x10-0x13) : `for (i=0;i<14;++i)` @0x684DC9+, icône dessinée à
    // (frameX + 30*i + 24, frameY + 7) — ex. cas objet @0x6850CB, cas
    // compétence @0x684F3D/0x685042 ; compteur d'empilement (le cas échéant)
    // à (frameX + 30*i + 38, frameY + 21) @0x685150. Pas de label « touche »
    // séparé trouvé : vraisemblablement incrusté dans le fond. kQuickSlotCount
    // RESTE 10 ici — TODO précis (pas une contrainte d'édition, cf. audit
    // 2026-07-14) : passer à 14 pour couvrir les slots étendus Q/W/E/R (DIK
    // 0x10-0x13) demanderait d'étendre `kQuickSlotCount`, `Layout::slots` ET
    // `slots_` ci-dessus, PLUS `Game/ConsumableBarLogic.h::ConsumableSlots`
    // (alias sur `std::array<QuickSlot, kQuickSlotCount>`) et
    // `UI/ConsumableBarWindow.h` (mêmes constantes) — changement transverse
    // à plusieurs fichiers, non fait dans cette passe (priorité donnée au
    // câblage des widgets déjà écrits, cf. bandeau de tête du fichier).
    // Seuls les 10 slots numériques (1..0) sont donc représentés ici.
    //
    // Ancrage réel du panneau de fond @0x684CA8-0x684D80 :
    //   si nWidth > 1024 : centré horizontalement, collé au bas de l'écran
    //                      (nWidth/2 - largeur/2 @0x684CEB,
    //                       nHeight - hauteur @0x684D08)
    //   sinon (résolution de lancement standard 1024x768, cf. CLAUDE.md
    //          "/0/0/2/1024/768") : ANCRE FIXE (391, 728) @0x684CB0/0x684CBC.
    // Largeur/hauteur du sprite de fond non lisibles statiquement (mêmes
    // limites que le cadre vitales) -> conservé le calcul dérivé du pitch réel
    // (30 px) au lieu de charger la texture.
    constexpr int slotSize = 26; // ancien 40 ; dérivé du pitch réel 30 (ci-dessous)
    constexpr int slotGap  = 4;  // pitch réel = slotSize+slotGap = 30 (ancien pitch 44)
    const int totalW = kQuickSlotCount * slotSize + (kQuickSlotCount - 1) * slotGap;

    int anchorX = 0;
    int anchorY = 0;
    if (screenW_ > 1024) {
        // Branche centrée @0x684CCF-0x684D08 (nWidth/2 - largeur/2 ; bas d'écran).
        anchorX = screenW_ / 2 - totalW / 2;
        anchorY = screenH_ - (slotSize + 8);
    } else {
        // Branche fixe @0x684CB0/0x684CBC — valeurs LITTÉRALES du binaire pour
        // la résolution de lancement 1024x768 (ancien placeholder : centré
        // dynamiquement, ancre inexistante côté binaire pour ce cas).
        anchorX = 391;
        anchorY = 728;
    }

    // ⚠️ CORRECTION DE COORDONNÉES (audit "fenêtres mal ajustées", 2026-07-14, RE-VÉRIFIÉ
    // par désassemblage FRAIS via idaTs2 direct HTTP JSON-RPC, http://127.0.0.1:13337/mcp,
    // tools/call "disasm" sur 0x684CA8-0x685177) : les icônes ne sont PAS dessinées à
    // (anchorX+i*pitch, anchorY) comme ci-dessous auparavant — elles sont décalées d'un
    // offset FIXE (+24,+7) par rapport à l'ancre du fond de panneau, vérifié identique sur
    // les 3 branches de contenu (compétence @0x684F11-0x684F3D/0x685010-0x685042, objet
    // @0x685099-0x6850CB : this.x+i*0x1E+0x18, this.y+7 dans les trois cas). anchorX/anchorY
    // ci-dessus restent le coin du FOND de panneau (this[0]/this[1], EA 0x684CB0/0x684CBC
    // confirmées correctes) ; ce n'est que la position de la 1ère case qui manquait cet
    // offset. Ce chemin n'est qu'un REPLI (quickBarWindow_ nul) + hit-test de secours — le
    // rendu normal passe par ConsumableBarWindow::ComputeLayout (même correction appliquée
    // là, cf. son bandeau de tête).
    constexpr int iconOffsetX = 24; // +0x18, vérifié 0x684F29/0x684FB3/0x6850B7
    constexpr int iconOffsetY = 7;  // +7,    vérifié 0x684F14/0x684F9A/0x6850A2
    const int iconX0 = anchorX + iconOffsetX;
    const int iconY0 = anchorY + iconOffsetY;

    layout_.quickBar = HudRect{ anchorX - 4, anchorY - 4, iconOffsetX + totalW + 8, slotSize + iconOffsetY + 8 };
    for (int i = 0; i < kQuickSlotCount; ++i) {
        layout_.slots[static_cast<size_t>(i)] =
            HudRect{ iconX0 + i * (slotSize + slotGap), iconY0, slotSize, slotSize };
    }

    // --- Callout de marqueur de quête (§17, Quest_DrawTracker 0x510FC0) ------
    // Position réelle non prouvable statiquement (`this+24`, champ hors
    // QuestMarkerState, cf. bandeau de tête du fichier) -> ancré haut-centre,
    // largeur alignée sur QuestTrackerWindow::kPanelW pour une cohérence visuelle
    // avec le panneau permanent (voir bandeau de tête pour la distinction).
    layout_.questMarker = HudRect{ screenW_ / 2 - kQuestMarkerW / 2, kQuestMarkerY0,
                                    kQuestMarkerW, kQuestMarkerH };
}

// =============================================================================
// Primitives de dessin (entre sprite_.Begin()/End())
// =============================================================================

// Rect plein : texture 1x1 blanche mise à l'échelle (w,h) et teintée.
// Utilise le chemin compensatePos=true (UI_DrawSpriteScaledAlpha 0x457D70) :
// pos = {x/w, y/h} puis matrice d'échelle -> quad w×h ancré exactement en (x,y).
void GameHud::DrawFilledRect(const HudRect& r, D3DCOLOR color) {
    if (!white_ || r.w <= 0 || r.h <= 0) return;
    RECT src{ 0, 0, 1, 1 };
    sprite_.DrawSpriteScaled(white_, &src, r.x, r.y,
                             static_cast<float>(r.w), static_cast<float>(r.h),
                             color, /*compensatePos=*/true);
}

// Contour = 4 rects pleins (haut / bas / gauche / droite).
void GameHud::DrawBorder(const HudRect& r, int t, D3DCOLOR color) {
    if (t <= 0) return;
    DrawFilledRect(HudRect{ r.x,             r.y,             r.w, t         }, color);
    DrawFilledRect(HudRect{ r.x,             r.y + r.h - t,   r.w, t         }, color);
    DrawFilledRect(HudRect{ r.x,             r.y,             t,   r.h       }, color);
    DrawFilledRect(HudRect{ r.x + r.w - t,   r.y,             t,   r.h       }, color);
}

// Barre de jauge : fond + portion remplie (ratio cur/max) + contour 1px.
void GameHud::DrawBarFill(const HudRect& r, int cur, int max,
                          D3DCOLOR bg, D3DCOLOR fill) {
    DrawFilledRect(r, bg);

    float ratio = 0.0f;
    if (max > 0) {
        ratio = static_cast<float>(cur) / static_cast<float>(max);
        ratio = std::clamp(ratio, 0.0f, 1.0f);
    }
    const int inner   = 1; // marge pour laisser voir le contour
    const int availW  = r.w - 2 * inner;
    const int fillW   = static_cast<int>(static_cast<float>(availW) * ratio);
    if (fillW > 0) {
        DrawFilledRect(HudRect{ r.x + inner, r.y + inner, fillW, r.h - 2 * inner },
                       fill);
    }
    DrawBorder(r, 1, kBarBorder);
}

// =============================================================================
// Sous-passes
// =============================================================================
void GameHud::DrawVitalsFrame() {
    // Cadre : vrai sprite .IMG si chargé (blit plein-cadre non mis à l'échelle,
    // fidèle à Sprite2D_Draw(&unk_8EC114, 0, 0) qui blitte à sa taille native —
    // cf. bandeau de tête). Repli sur l'ancien rect plein + liseré sinon : le
    // fonctionnement précédent n'est jamais cassé par un échec de chargement.
    if (vitalsFrameTex_.Valid()) {
        sprite_.DrawSprite(vitalsFrameTex_.Handle(), nullptr, layout_.frame.x, layout_.frame.y);
    } else {
        DrawFilledRect(layout_.frame, kFrameBg);
        DrawBorder(layout_.frame, 1, kFrameBorder);
    }

    // Mini-cadre portrait (placeholder : fond uni + liseré ; l'icône du
    // personnage viendra du skin .IMG une fois branché).
    DrawFilledRect(layout_.portrait, kPortraitBg);
    DrawBorder(layout_.portrait, 1, kFrameBorder);

    // Remplissage des barres depuis l'état local du joueur.
    const game::SelfState& self = game::g_World.self;
    DrawBarFill(layout_.hpBar, self.hp, self.maxHp, kHpBg, kHpFill);
    DrawBarFill(layout_.mpBar, self.mp, self.maxMp, kMpBg, kMpFill);

    // §1 barre EXP + barre Maîtrise élémentaire (mission W4-F2, UI_GameHud_Render
    // 0x67A59E-0x67A782). Rects LOCAUX dérivés de layout_.frame — le header GameHud.h
    // étant en lecture seule, aucun champ Layout ne peut être ajouté : on réutilise le
    // même x=57 / largeur 150 / pas de 14 px que hpBar (y=8) / mpBar (y=22), fidèle aux
    // ancres de fill (57,36) @0x67A672 et (57,50) @0x67A71F.
    const HudRect& fr = layout_.frame;
    const HudRect expBar{ fr.x + 57, fr.y + 36, 150, 12 };     // fill (0x39,0x24) @0x67A672
    const HudRect masteryBar{ fr.x + 57, fr.y + 50, 150, 12 }; // fill (0x39,0x32) @0x67A71F

    int expProgress = 0, expSpan = 0;
    ComputeExpProgress(self.level, self.levelBonus, expProgress, expSpan);
    // Gate fidèle `cmp var_84C,0 / jle` @0x67A641 : dessiné seulement si progress > 0
    // (DrawBarFill clampe déjà le ratio [0,1], équivalent du clamp de frame 220 @0x67A66B).
    if (expProgress > 0 && expSpan > 0)
        DrawBarFill(expBar, expProgress, expSpan, kExpBg, kExpFill);

    // Maîtrise élémentaire : échelle FIXE /3000 (dbl_7EDAE8 @0x67A6FC), dessinée si
    // val > 0 (cmp dword_168746C,0 / jle @0x67A6EE).
    const int chi = game::g_Client.VarGet(0x168746C); // dword_168746C @0x67A6E7
    if (chi > 0)
        DrawBarFill(masteryBar, chi, kMasteryMax, kMasteryBg, kMasteryFill);
}

// Repli si quickBarWindow_ n'a pas pu être alloué (bad_alloc, cf. Init()) — ancien
// rendu placeholder, inchangé. Le chemin normal passe désormais par
// quickBarWindow_->Render() (voir Render() ci-dessous, mission 2026-07-14).
void GameHud::DrawQuickSlotFrames() {
    // Panneau de fond de la barre.
    DrawFilledRect(layout_.quickBar, kFrameBg);
    DrawBorder(layout_.quickBar, 1, kFrameBorder);

    for (int i = 0; i < kQuickSlotCount; ++i) {
        const HudRect& s = layout_.slots[static_cast<size_t>(i)];
        DrawFilledRect(s, kSlotBg);
        DrawBorder(s, 1, kSlotBorder);

        // Marqueur d'assignation (placeholder de l'icône d'objet/compétence).
        if (!slots_[static_cast<size_t>(i)].empty()) {
            DrawFilledRect(HudRect{ s.x + 4, s.y + 4, s.w - 8, s.h - 8 },
                           kSlotFilled);
        }
    }
}

// =============================================================================
// Cadres alliance/groupe (§8, EA 0x67B891-0x67BD54) — mission 2026-07-14.
// =============================================================================

// Recoupe game::g_World.allianceRoster (noms, EA-vérifié : g_AllianceRosterNames) avec
// game::g_World.players[] par NOM (même méthode que la plaque de cible §7 dans le
// binaire d'origine — les deux tableaux n'ont aucune clé de jointure directe, cf.
// commentaire de Game/GameState.h::PartyRoster pour la même mise en garde côté groupe).
// Boucle arrêtée au premier slot vide (fidèle à la condition EA `Crt_Strcmp(...) != 0`
// testée séquentiellement dans le binaire, PAS un simple filtre "skip les vides").
std::vector<GameHud::AllianceFrameRow> GameHud::BuildAllianceFrames() const {
    std::vector<AllianceFrameRow> rows;
    const auto& alliance = game::g_World.allianceRoster;
    const auto& players  = game::g_World.players;

    for (int i = 0; i < kAllianceMaxRows; ++i) {
        const std::string& nm = alliance.memberNames[static_cast<size_t>(i)];
        if (nm.empty()) break; // EA 0x67B891 : boucle tant que le slot i est non vide

        AllianceFrameRow row;
        row.name = nm;

        for (size_t pi = 0; pi < players.size(); ++pi) {
            const game::PlayerEntity& p = players[pi];
            if (!p.active || p.name != nm) continue;
            row.resolved = true;
            if (pi == 0) {
                // Slot 0 du tableau d'entités = joueur local (convention EntityManager) :
                // source la plus fiable (StatEngine), PV/PM ET maxima réels disponibles.
                const game::SelfState& self = game::g_World.self;
                row.hp = self.hp; row.hpMax = self.maxHp; row.hpMaxKnown = true;
                row.mp = self.mp; row.mpMax = self.maxMp; row.mpMaxKnown = true;
            } else {
                // Autre membre : PV/PM COURANTS réels (Pkt_CharStatDelta écrit e->hp/e->mp
                // pour TOUTE entité résolue par identité réseau, pas seulement self, cf.
                // Game/EntityManager.cpp::OnCharStatDelta) — mais AUCUN maximum n'est
                // modélisé pour une entité distante (PlayerEntity n'a pas de maxHp/maxMp,
                // cf. Game/GameState.h) : hpMaxKnown/mpMaxKnown restent false, la jauge est
                // dessinée grisée plutôt qu'avec un ratio inventé.
                row.hp = p.hp;
                row.mp = p.mp;
            }
            break;
        }

        rows.push_back(std::move(row));
    }
    return rows;
}

// Passe 1 (sprites pleins) : fond de ligne + pastille de présence + jauges PV/PM.
void GameHud::DrawAllianceFramePanels(const std::vector<AllianceFrameRow>& rows) {
    for (size_t i = 0; i < rows.size(); ++i) {
        const AllianceFrameRow& r = rows[i];
        const int rowY = kAllianceRowY0 + kAllianceRowStep * static_cast<int>(i);

        const HudRect frame{ 0, rowY, kAllianceRowW, kAllianceRowStep - 4 };
        DrawFilledRect(frame, kAllyFrameBg);
        DrawBorder(frame, 1, kAllyFrameBrd);

        // Pastille de présence (repli sans asset .IMG identifié, cf. bandeau de tête —
        // le binaire distingue icône niveau-max/normal/introuvable, ici réduit à
        // présent/absent, seule information réellement disponible côté client réécrit).
        const HudRect icon{ 4, rowY + 4, kAllianceIconSize, kAllianceIconSize };
        DrawFilledRect(icon, r.resolved ? kAllyIconOnline : kAllyIconOffline);
        DrawBorder(icon, 1, kAllyFrameBrd);

        const HudRect hpBar{ 5, rowY + kAllianceHpDy, kAllianceRowW - 10, kAllianceBarH };
        const HudRect mpBar{ 5, rowY + kAllianceMpDy, kAllianceRowW - 10, kAllianceBarH };

        if (r.resolved && r.hpMaxKnown) {
            DrawBarFill(hpBar, r.hp, r.hpMax, kHpBg, kHpFill);
        } else {
            DrawFilledRect(hpBar, kAllyNoData);
            DrawBorder(hpBar, 1, kBarBorder);
        }
        if (r.resolved && r.mpMaxKnown) {
            DrawBarFill(mpBar, r.mp, r.mpMax, kMpBg, kMpFill);
        } else {
            DrawFilledRect(mpBar, kAllyNoData);
            DrawBorder(mpBar, 1, kBarBorder);
        }
    }
}

// Passe texte : nom du membre + valeurs numériques réelles quand disponibles.
void GameHud::DrawAllianceFrameText(const std::vector<AllianceFrameRow>& rows) {
    if (!font_.Ready()) return;
    char buf[64];

    for (size_t i = 0; i < rows.size(); ++i) {
        const AllianceFrameRow& r = rows[i];
        const int rowY = kAllianceRowY0 + kAllianceRowStep * static_cast<int>(i);

        font_.DrawTextStyled(r.name.c_str(), kAllianceIconSize + 12, rowY + kAllianceNameDy,
                             r.resolved ? kAllyNameCol : kTextDim, gfx::kStyleShadow);

        if (r.resolved) {
            if (r.hpMaxKnown) {
                std::snprintf(buf, sizeof(buf), "%d/%d", r.hp, r.hpMax);
            } else {
                std::snprintf(buf, sizeof(buf), "%d", r.hp); // PV réel, max non modélisé
            }
            font_.DrawTextStyled(buf, 8, rowY + kAllianceHpDy - 1, kTextColor, gfx::kStyleShadow);

            if (r.mpMaxKnown) {
                std::snprintf(buf, sizeof(buf), "%d/%d", r.mp, r.mpMax);
            } else {
                std::snprintf(buf, sizeof(buf), "%d", r.mp);
            }
            font_.DrawTextStyled(buf, 8, rowY + kAllianceMpDy - 1, kTextColor, gfx::kStyleShadow);
        }
    }
}

// Hit-test grossier : zone couvrant les lignes actuellement peuplées du roster.
bool GameHud::AllianceFramesContains(int x, int y) const {
    int count = 0;
    const auto& alliance = game::g_World.allianceRoster;
    for (int i = 0; i < kAllianceMaxRows; ++i) {
        if (alliance.memberNames[static_cast<size_t>(i)].empty()) break;
        ++count;
    }
    if (count == 0) return false;
    const HudRect area{ 0, kAllianceRowY0, kAllianceRowW, kAllianceRowStep * count };
    return area.Contains(x, y);
}

// Passe texte (police via son propre ID3DXSprite -> lot séparé).
void GameHud::DrawTextPass(int hp, int maxHp, int mp, int maxMp, int level, int currency) {
    if (!font_.Ready()) return;
    char buf[64];

    font_.BeginBatch(D3DXSPRITE_ALPHABLEND);

    // Libellé « niveau » dans le portrait.
    std::snprintf(buf, sizeof(buf), "Lv %d", level);
    font_.DrawTextStyled(buf, layout_.portrait.x + 4, layout_.portrait.y + 4,
                         kTextColor, gfx::kStyleShadow);

    // Valeurs "%d/%d" HP/MP ALIGNÉES À DROITE sur le bord x=207, comme le binaire
    // (et comme EXP/Maîtrise plus bas dans ce fichier). HP : x=0xCF-largeur, y=7
    // (UI_GameHud_Render 0x67A3C0 @0x67A4DC 'mov ecx,0CFh; sub ecx,eax' / push 7 @0x67A4C6).
    // MP : x=0xCF-largeur, y=21 (@0x67A57E 'mov edx,0CFh; sub edx,ecx' / push 15h @0x67A568).
    // L'ancien rendu centré sur la barre (centeredLabel) était infidèle.
    auto rightAlignedLabel = [&](int cur, int mx, int y) {
        std::snprintf(buf, sizeof(buf), "%d/%d", cur, mx);
        const int tw = font_.MeasureText(buf);
        font_.DrawTextStyled(buf, layout_.frame.x + 207 - tw, layout_.frame.y + y,
                             kTextColor, gfx::kStyleShadow);
    };
    rightAlignedLabel(hp, maxHp, 7);  // HP : y=7  @0x67A4C6
    rightAlignedLabel(mp, maxMp, 21); // MP : y=21 @0x67A568

    // §1 texte EXP + Maîtrise (mission W4-F2, UI_GameHud_Render 0x67A690-0x67A782).
    // Recalcul local (idempotent, coût nul) : DrawTextPass ne reçoit pas progress/span.
    // EXP = POURCENTAGE "%.3f" de progress*100/span (a3f @0x67A6A2, dbl_7EDAF0=100.0),
    // aligné à droite bord x=207 (0xCF-largeur @0x67A6CE), y=35 (0x23 @0x67A6B8).
    // Maîtrise = "%d/%d" (val, 3000) aligné droite x=207, y=49 (0x31 @0x67A758).
    {
        const game::SelfState& s = game::g_World.self;
        int p = 0, span = 0;
        ComputeExpProgress(s.level, s.levelBonus, p, span);
        if (span > 0) {
            std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(p) * 100.0 / span);
            const int tw = font_.MeasureText(buf);
            font_.DrawTextStyled(buf, layout_.frame.x + 207 - tw, layout_.frame.y + 35,
                                 kTextColor, gfx::kStyleShadow);
        }
        const int chi = game::g_Client.VarGet(0x168746C);
        if (chi > 0) {
            std::snprintf(buf, sizeof(buf), "%d/%d", chi, kMasteryMax);
            const int tw = font_.MeasureText(buf);
            font_.DrawTextStyled(buf, layout_.frame.x + 207 - tw, layout_.frame.y + 49,
                                 kTextColor, gfx::kStyleShadow);
        }
    }

    // §2 devise (haut-droite, EA 0x67A839-0x67A8FA) — donnée déjà disponible
    // (game::g_World.self.currency, tenue à jour par les handlers réseau) mais
    // jamais affichée avant cette passe (mission 2026-07-14). Ancrage réel =
    // juste sous le cadre mini-carte ; ici, à défaut de connaître la largeur du
    // panneau mini-carte non chargée (cf. bandeau MinimapWidget.h), ancré au bord
    // d'écran avec une marge fixe — simplification assumée, jamais bloquante.
    std::snprintf(buf, sizeof(buf), "Or : %d", currency);
    {
        const int tw = font_.MeasureText(buf);
        font_.DrawTextStyled(buf, screenW_ - tw - 8, 4, kTextColor, gfx::kStyleShadow);
    }

    // Numéro de touche dans chaque quickslot (1..9 puis 0) — UNIQUEMENT en repli
    // (quickBarWindow_ dessine ses propres numéros de touche dans le chemin
    // normal, cf. Render() ; double-dessin évité).
    if (!quickBarWindow_) {
        for (int i = 0; i < kQuickSlotCount; ++i) {
            const HudRect& s = layout_.slots[static_cast<size_t>(i)];
            const int key = (i + 1) % 10; // slot 10 -> touche '0'
            std::snprintf(buf, sizeof(buf), "%d", key);
            font_.DrawTextStyled(buf, s.x + 3, s.y + 2, kTextDim, gfx::kStyleShadow);
        }
    }

    font_.EndBatch();
}

// §17 overlay debug temps réservé aux GM — EA 0x686942 (dans UI_GameHud_Render, PAS dans
// Quest_DrawTracker 0x6868AB comme le doc le laissait supposer, cf. bandeau de tête).
// Lot de police AUTONOME (BeginBatch/EndBatch propres) : n'ouvre le batch que si le
// contenu sera réellement dessiné, pour rester un no-op silencieux hors GM (même politique
// que buffPanel_/chatWindow_, widgets autonomes ci-dessous).
void GameHud::DrawDebugTimeOverlay() {
    // Condition binaire EXACTE @0x6868e8-0x6868f8 : `dword_1676108 > 0 && g_GmAuthLevel > 0`.
    // dword_1676108 déjà tenu à jour par ApplySetGameVar cas 98 (Net/GameVarDispatch.cpp).
    if (game::g_Client.VarGet(0x1676108) <= 0) return;
    if (net::g_GmAuthLevel == 0) return;
    if (!font_.Ready()) return;

    // Format littéral "NowTime : %d / %d %d:%d %s" (aNowtimeDDDDS @0x7baf78). Les 4 entiers
    // = mois+1/jour/heure/minute décodés depuis dword_1676108 par ApplySetGameVar cas 98
    // (dword_167610C/1676110/1676114/1676118). Le %s (byte_167611C) reste vide tant que
    // Crt_StringInit (0x75CAB0) n'est pas porté (TODO(ui) déjà documenté dans
    // GameVarDispatch.cpp) — dégradation fidèle, pas de texte inventé.
    char buf[128];
    const auto& dayStr = game::g_Client.Blob(0x167611C, 64);
    std::snprintf(buf, sizeof(buf), "NowTime : %d / %d %d:%d %s",
                  game::g_Client.VarGet(0x167610C),
                  game::g_Client.VarGet(0x1676110),
                  game::g_Client.VarGet(0x1676114),
                  game::g_Client.VarGet(0x1676118),
                  reinterpret_cast<const char*>(dayStr.data()));

    // Ancre fixe (10,150) @0x686934/0x68692f (push 0Ah/push 96h avant UI_DrawNumberValue
    // 0x53FCC0, qui relaie vers UI_DrawText(a1=texte, x, y, couleur, style)). Couleur
    // d'origine = ColorTable_GetColor(dword_84DF20, 3) (table de couleurs non modélisée
    // côté client réécrit) ; kTextColor + ombre = même convention que le reste du HUD
    // (DrawTextPass ci-dessus), pas une invention visuelle bloquante.
    font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
    font_.DrawTextStyled(buf, 10, 150, kTextColor, gfx::kStyleShadow);
    font_.EndBatch();
}

// =============================================================================
// §17 callout de marqueur de quête — Quest_DrawTracker 0x510FC0 (mission 2026-07-14,
// voir bandeau de tête « CÂBLAGE CALLOUT MARQUEUR DE QUÊTE »). Gate UNIQUE et fidèle au
// binaire : `questMarker_.active` (== *(this+51576) != 0 dans Quest_DrawTracker).
// =============================================================================

// Passe sprites : cadre + pastille d'icône (repli coloré, cf. bandeau de tête pour la
// réserve sur l'icône réelle mNPC/g_AssetMgr_UiAtlasSlots, non modélisée).
void GameHud::DrawQuestMarkerPanel() {
    if (!questMarker_.active) return; // EA 0x510fd9 : garde de tête de Quest_DrawTracker

    const HudRect& r = layout_.questMarker;
    DrawFilledRect(r, kQuestMarkerBg);
    DrawBorder(r, 1, kQuestMarkerBorder);

    // lastObjectiveState==1 <=> branche "objectif rempli" de Quest_UpdateMarkerTimer
    // (EA 0x510e13, joue Snd3D_PlayScaledVolume) ; toute autre valeur non nulle <=>
    // branche "nouvel objectif en cours" (EA 0x510ecc, markerVariant = Rng_Next()%3+1).
    const bool complete = questMarker_.lastObjectiveState == 1;
    const HudRect icon{ r.x + 4, r.y + kQuestMarkerPadY, kQuestMarkerIconSz, kQuestMarkerIconSz };
    DrawFilledRect(icon, complete ? kQuestMarkerDone : kQuestMarkerGoing);
    DrawBorder(icon, 1, kQuestMarkerBorder);
}

// Passe texte AUTONOME (même politique que DrawDebugTimeOverlay ci-dessus) : titre +
// identifiants bruts zone/PNJ/cible (game::QuestProgressState + game::QuestStepRecord via
// LookupQuestStep, résolveur injectable — nullptr par défaut, cf. Game/QuestSystem.h),
// PAS de texte de variante inventé (la table StrTable003/mZONENPCINFO source du vrai
// texte "%s (%d,%d)"/"%s!" de Quest_DrawTracker est hors périmètre, cf. bandeau de tête).
void GameHud::DrawQuestMarkerText() {
    if (!questMarker_.active) return;
    if (!font_.Ready()) return;

    const game::QuestProgressState& progress = game::g_QuestProgress;
    const bool complete = questMarker_.lastObjectiveState == 1;
    // Même sélection de record que le binaire (EA 0x511048/0x5110a8/0x5110d1) :
    // npcQuestId+1 si "objectif rempli" (étape SUIVANTE déjà résolue), npcQuestId sinon.
    const int npcQuestId = complete ? progress.npcQuestId + 1 : progress.npcQuestId;
    const game::QuestStepRecord* step = game::LookupQuestStep(progress.zoneId, npcQuestId);

    const HudRect& r = layout_.questMarker;
    char buf[128];

    font_.BeginBatch(D3DXSPRITE_ALPHABLEND);

    int ty = r.y + kQuestMarkerPadY;
    const char* title = complete ? "Quete : objectif rempli !" : "Quete : nouvel objectif";
    font_.DrawTextStyled(title, r.x + kQuestMarkerIconSz + kQuestMarkerPadX + 4, ty,
                         kQuestMarkerTitle, gfx::kStyleShadow);
    ty += kQuestMarkerLineH;

    // Zone/PNJ — même résolution de nom que UI/QuestTrackerWindow.cpp::BuildLayout
    // (StrTable003_Get via game::g_Strings.zoneNames, repli sur l'id numérique).
    const char* zoneName = game::g_Strings.zoneNames.Get(progress.zoneId);
    if (zoneName && zoneName[0] != '\0')
        std::snprintf(buf, sizeof(buf), "%s - Quete NPC #%d", zoneName, progress.npcQuestId);
    else
        std::snprintf(buf, sizeof(buf), "Zone #%d - Quete NPC #%d", progress.zoneId, progress.npcQuestId);
    font_.DrawTextStyled(buf, r.x + kQuestMarkerPadX, ty, kTextColor, gfx::kStyleShadow);
    ty += kQuestMarkerLineH;

    if (step) {
        std::snprintf(buf, sizeof(buf), "Cible #%u (%d/%u)",
                      step->targetId, progress.objectiveProgress, step->required);
    } else {
        // QuestStepLookup non branché (résolveur injectable par défaut, cf. bandeau de
        // tête) : repli honnête sur l'identifiant brut, pas de texte inventé.
        std::snprintf(buf, sizeof(buf), "Cible : PNJ mQUEST #%d", npcQuestId);
    }
    font_.DrawTextStyled(buf, r.x + kQuestMarkerPadX, ty, kTextColor, gfx::kStyleShadow);

    font_.EndBatch();
}

// =============================================================================
// §7 plaques de cible verrouillée + §15 rangée de boutons de menu (mission W4-F2)
// — helpers file-local (GameHud.h en lecture seule : aucune méthode/champ ajouté).
// =============================================================================
namespace {

// --- §7 plaques de cible (UI_GameHud_Render 0x67B0D3 / 0x67B436) ---------------
// Registres de nom = blobs 13 o 0x167468A (plaque A) / 0x1674697 (plaque B), écrits
// par Pkt SC 0x44 (Net/GameHandlers_ChatSocial.cpp) et déjà lus par
// Scene/SceneManager.cpp::readReqName (même motif répliqué ici). Affichage gaté par
// nom non vide (`Crt_Strcmp(&name,"") != 0` @0x67B0D3). Résolution par NOM dans
// g_World.players (le binaire balaie g_EntityArray 0x1687234, stride 0x38C, nom à
// entity+72 @0x67B115-0x67B171 ; ici on recoupe le miroir players[], même méthode que
// BuildAllianceFrames). LIMITE FIDÈLE : maxHp d'une entité DISTANTE non modélisé
// (PlayerEntity n'a pas de maxHp) -> jauge grisée plutôt qu'un ratio inventé.
constexpr int kTargetPlateY0   = 105; // panneau à (0,105), au-dessus des lignes alliance §8 (y=155)
constexpr int kTargetPlateW    = 204;
constexpr int kTargetPlateH    = 48;
constexpr int kTargetPlateStep = 50;  // plaques A/B empilées quand les deux sont actives

struct TargetPlate {
    bool        active   = false; // registre de nom non vide (gate 0x67B0D3)
    bool        resolved = false; // entité trouvée par NOM dans g_World.players
    std::string name;
    int         hp = 0, hpMax = 0;
    bool        hpMaxKnown = false; // maxHp connu (self uniquement, cf. §8 alliance)
};

// Réplique de Scene/SceneManager.cpp::readReqName (blob 13 o, lu jusqu'au NUL).
std::string ReadTargetName(uint32_t addr) {
    const auto& blob = game::g_Client.Blob(addr, 13);
    size_t len = 0;
    while (len < blob.size() && blob[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(blob.data()), len);
}

// Accesseur de « cible courante résolue » (demandé par la tâche W4-F2).
TargetPlate ResolveTargetPlate(uint32_t nameAddr) {
    TargetPlate tp;
    tp.name   = ReadTargetName(nameAddr);
    tp.active = !tp.name.empty();
    if (!tp.active) return tp;

    const auto& players = game::g_World.players;
    for (size_t pi = 0; pi < players.size(); ++pi) {
        const game::PlayerEntity& p = players[pi];
        if (!p.active || p.name != tp.name) continue;
        tp.resolved = true;
        if (pi == 0) {
            // players[0] = self (convention EntityManager) : PV/maxPV réels via StatEngine.
            const game::SelfState& self = game::g_World.self;
            tp.hp = self.hp; tp.hpMax = self.maxHp; tp.hpMaxKnown = true;
        } else {
            tp.hp = p.hp; // PV courant réel ; maxHp distant non modélisé -> hpMaxKnown=false
        }
        break;
    }
    return tp;
}

// --- §15 rangée de boutons de menu (UI_GameHud_Render 0x685177) ----------------
// Chaque icône Sprite2D est dessinée à (ancre quickbar + dx, + dy), où l'ancre =
// (this[0],this[1]) = coin du fond de quickbar (EA 0x684CB0/0x684CBC, = layout_.quickBar
// +4). Le clic appelle sub_4C1110(0) (= Util_SetClampedU8Field, toggle d'un flag U8
// this+flagOff = ouverture/fermeture d'une fenêtre, cible NON identifiée statiquement).
// Offsets dx/dy/flag relevés du doc §15. Tailles de boutons non lisibles (icônes skin
// .npk non chargées) -> estimées 24x16 (même limite que §1/§14).
struct MenuBtn { int dx, dy, flagOff; };
const MenuBtn kMenuButtons[] = {
    {   0, -17, 124 }, {  25, -17, 396 }, {  59, -16, 452 }, {  84, -16, 448 },
    { 109, -16, 444 }, { 134, -16, 440 }, { 159, -16, 436 }, { 184, -16, 428 },
    { 234, -17, 420 }, { 284, -17, 412 }, { 334, -17, 384 }, { 359, -17, 388 },
    { 384, -17, 392 }, { 309, -17, 408 }, { 409, -17, 400 }, { 434, -17, 404 },
    { 458,   2, 432 },
};
constexpr int kMenuBtnW = 24;
constexpr int kMenuBtnH = 16;

} // namespace

// =============================================================================
// Render — cGameHud_Render 0x64A900
// =============================================================================
void GameHud::Render() {
    if (!visible_ || !device_ || !sprite_.Ready()) return;

    const game::SelfState& self = game::g_World.self;

    // Contexte partagé UiContext (cf. UI/UIManager.h) — construit localement, PAS
    // de dépendance à un UIManager::Instance() enregistré (GameHud n'est pas un
    // Dialog). Consommé par quickBarWindow_ ci-dessous (mission 2026-07-14).
    UiContext ctx;
    ctx.renderer    = rendererPtr_; // cf. UI/GameHud.h::rendererPtr_ (audit 2026-07-14)
    ctx.sprites     = &sprite_;
    ctx.font        = &font_;
    ctx.whiteTex    = white_;
    ctx.screenW     = screenW_;
    ctx.screenH     = screenH_;
    ctx.gameTimeSec = game::g_World.gameTimeSec;

    // §17 callout de marqueur de quête (mission 2026-07-14, voir bandeau de tête
    // « CÂBLAGE CALLOUT MARQUEUR DE QUÊTE ») : tique l'état AVANT les passes de rendu
    // (même entrées que Scene/SceneManager.cpp::host.UpdateQuestMarkerTimer -
    // game::g_QuestProgress + game::g_World.gameTimeSec, isArenaZone=false faute de
    // Map_IsArenaZone modélisé ici comme là-bas), cf. GameHud.h::questMarker_ pour la
    // réserve sur cette instance PROPRE à GameHud (pas de partage possible avec la
    // statique locale de SceneManager.cpp, fichier volontairement non modifié).
    game::Quest_UpdateMarkerTimer(questMarker_, game::g_QuestProgress, game::g_World.gameTimeSec,
                                  /*isArenaZone=*/false, /*warehouseTargetMatches=*/nullptr,
                                  /*playMarkerSound=*/nullptr);

    // Passe 1 : sprites pleins (cadre + barres + quickbar §14 + mini-carte §12
    // + cadres alliance/groupe §8 + callout de marqueur de quête §17).
    if (SUCCEEDED(sprite_.Begin(D3DXSPRITE_ALPHABLEND))) {
        DrawVitalsFrame();
        if (quickBarWindow_) {
            ctx.phase = UiPhase::Panels;
            // cursorX/cursorY par défaut (-1,-1) : SceneManager ne route aucun
            // événement WM_MOUSEMOVE vers GameHud aujourd'hui (cf. TODO précis
            // dans le rapport de mission) -> pas de surbrillance de survol.
            quickBarWindow_->Render(ctx, slots_);
        } else {
            DrawQuickSlotFrames(); // repli si l'allocation a échoué (cf. Init())
        }
        minimap_.DrawPanels(sprite_, white_);
        // §7 plaques de cible verrouillée x2 (mission W4-F2, UI_GameHud_Render 0x67B0D3 /
        // 0x67B436) — panneaux d'information à (0,105+), dessinés AVANT les lignes alliance
        // §8 (elles à y=155). Même politique de dégradation que §8 : maxHp d'une cible
        // distante non modélisé -> jauge grisée, jamais un ratio inventé.
        {
            const TargetPlate plates[2] = {
                ResolveTargetPlate(0x167468A), // plaque A (0x67B0D3)
                ResolveTargetPlate(0x1674697), // plaque B (0x67B436)
            };
            int prow = 0;
            for (int pi = 0; pi < 2; ++pi) {
                const TargetPlate& tp = plates[pi];
                if (!tp.active) continue; // gate `Crt_Strcmp(name,"") != 0` @0x67B0D3
                const int py = kTargetPlateY0 + prow * kTargetPlateStep;
                const HudRect frame{ 0, py, kTargetPlateW, kTargetPlateH };
                DrawFilledRect(frame, kAllyFrameBg);
                DrawBorder(frame, 1, kAllyFrameBrd);
                // Pastille de présence (le binaire distingue icône offline unk_923758 /
                // max-level unk_9464A8 / normal unk_9236C4 ; ici réduit à résolu/introuvable).
                const HudRect icon{ 4, py + 4, kAllianceIconSize, kAllianceIconSize };
                DrawFilledRect(icon, tp.resolved ? kAllyIconOnline : kAllyIconOffline);
                DrawBorder(icon, 1, kAllyFrameBrd);
                // Mini-barre HP uniquement (36 paliers frames 520-556 @ (5,129) local
                // dans le binaire) — PAS de barre MP (contraste avec l'alliance §8).
                const HudRect hpBar{ 5, py + 24, kTargetPlateW - 10, kAllianceBarH };
                if (tp.resolved && tp.hpMaxKnown) {
                    DrawBarFill(hpBar, tp.hp, tp.hpMax, kHpBg, kHpFill);
                } else {
                    DrawFilledRect(hpBar, kAllyNoData);
                    DrawBorder(hpBar, 1, kBarBorder);
                }
                ++prow;
            }
        }
        // §8 cadres alliance/groupe (mission 2026-07-14, voir bandeau de tête) —
        // reconstruit à chaque frame (coût négligeable : au plus 5 slots x recherche
        // linéaire dans g_World.players), même politique que UI/PartyWindow.cpp.
        DrawAllianceFramePanels(BuildAllianceFrames());
        DrawQuestMarkerPanel(); // §17 callout — no-op si questMarker_.active est faux
        sprite_.End();
    }

    // Passe 2 : texte (lot de sprites distinct de la police) — barres HP/MP/niveau
    // + §2 devise.
    DrawTextPass(self.hp, self.maxHp, self.mp, self.maxMp, self.level, self.currency);

    // Passe 2bis : texte de la quickbar réelle (numéros de touche, quantités
    // possédées, message de retour du dernier clic) — deuxième sous-passe
    // UiContext du même widget que la passe 1 ci-dessus.
    if (quickBarWindow_ && font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        ctx.phase = UiPhase::Text;
        quickBarWindow_->Render(ctx, slots_);
        font_.EndBatch();
    }

    // Passe 3 : texte de la mini-carte (bouton bascule + nom de zone/coordonnées
    // en mode grand) — lot séparé pour ne pas toucher à la signature de
    // DrawTextPass ci-dessus.
    if (font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        minimap_.DrawText(font_);
        font_.EndBatch();
    }

    // Passe 3bis : texte des cadres alliance/groupe (§8, mission 2026-07-14) — noms +
    // valeurs PV/PM réelles. Rebuild indépendant de la passe 1 (même politique que
    // UI/PartyWindow.cpp : résultat identique dans la même frame, pas de dépendance
    // d'ordre entre les deux phases).
    if (font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        DrawAllianceFrameText(BuildAllianceFrames());
        font_.EndBatch();
    }

    // Passe 3quinquies : texte des plaques de cible (§7, mission W4-F2) — nom (centré
    // x=75 @0x67B294) + PV réel (max si connu) + compteur de la plaque B (VarGet
    // 0x16746A4 @0x67B... ). Rebuild indépendant de la passe 1 (résultat identique).
    if (font_.Ready()) {
        font_.BeginBatch(D3DXSPRITE_ALPHABLEND);
        const TargetPlate plates[2] = {
            ResolveTargetPlate(0x167468A),
            ResolveTargetPlate(0x1674697),
        };
        int prow = 0;
        char b[64];
        for (int pi = 0; pi < 2; ++pi) {
            const TargetPlate& tp = plates[pi];
            if (!tp.active) continue;
            const int py = kTargetPlateY0 + prow * kTargetPlateStep;
            font_.DrawTextStyled(tp.name.c_str(), 75, py + 6,
                                 tp.resolved ? kAllyNameCol : kTextDim, gfx::kStyleShadow);
            if (tp.resolved) {
                if (tp.hpMaxKnown) std::snprintf(b, sizeof(b), "%d/%d", tp.hp, tp.hpMax);
                else               std::snprintf(b, sizeof(b), "%d", tp.hp); // max distant non modélisé
                font_.DrawTextStyled(b, 8, py + 24, kTextColor, gfx::kStyleShadow);
            }
            if (pi == 1) {
                // Plaque B : compteur bas-droite, affiché même cible absente (@0x67B436).
                std::snprintf(b, sizeof(b), "%d", game::g_Client.VarGet(0x16746A4));
                const int tw = font_.MeasureText(b);
                font_.DrawTextStyled(b, kTargetPlateW - tw - 8, py + kTargetPlateH - 14,
                                     kTextDim, gfx::kStyleShadow);
            }
            ++prow;
        }
        font_.EndBatch();
    }

    // Passe 3ter : overlay debug temps réservé aux GM (§17, EA 0x686942, mission
    // 2026-07-14, voir DrawDebugTimeOverlay()). No-op silencieux hors compte GM.
    DrawDebugTimeOverlay();

    // Passe 3quater : texte du callout de marqueur de quête (§17, Quest_DrawTracker
    // 0x510FC0, mission 2026-07-14). No-op silencieux si questMarker_.active est faux.
    DrawQuestMarkerText();

    // Indicateur de cast (§16, EA 0x6865BF+, `dword_1685E74[g_LocalElement] != 0`) —
    // câblé mission 2026-07-14 : BuffStatusPanel::casting_ existait déjà (icône
    // pulsante 8 frames, cycle Crt_ftol(g_GameTimeSec*16)%8 fidèle à l'EA) mais
    // SetCasting() n'était JAMAIS appelé nulle part -> l'indicateur restait
    // définitivement éteint. La donnée d'état EST déjà modélisée fidèlement côté
    // Game/ActionStateMachine.h (CharActionState, switch terminal de
    // Char_UpdateAnimationFrame 0x571880/0x5727BF, offsets EXACTS du binaire) et
    // tenue à jour chaque frame pour SOI par
    // SceneManager::host.UpdateEntityAnimFrame (idx==0) -> game::g_World.Self().anim.state
    // (entity+244, MÊME mot mémoire que CharAnimState::state, cf. bandeau de tête
    // d'ActionStateMachine.h). Sémantique fidèle au commentaire IDA du doc
    // (« compétence élémentaire en cours de préparation/canalisation ») :
    // CastSlot0/1/2 (0x05-0x07, Char_CastAnimTick_57*, windup compétence) = préparation,
    // Channel (0x28, Char_TickChannelState 0x57A700) = canalisation/maintien.
    const int32_t selfActionState = game::g_World.Self().anim.state;
    const bool isCasting =
        selfActionState == static_cast<int32_t>(game::CharActionState::CastSlot0) ||
        selfActionState == static_cast<int32_t>(game::CharActionState::CastSlot1) ||
        selfActionState == static_cast<int32_t>(game::CharActionState::CastSlot2) ||
        selfActionState == static_cast<int32_t>(game::CharActionState::Channel);
    buffPanel_.SetCasting(isCasting);

    // Passe 4 : grille de buffs (§9) + panneau de statut bas-droite (§16). Widget
    // AUTONOME : gère lui-même son sprite_.Begin()/End() et son propre batch de
    // police (cf. BuffStatusPanel::Render), donc appelé hors des passes 1-3.
    buffPanel_.Render();

    // Passe 5 : fenêtre de chat & messages système (§13, mission 2026-07-14).
    // Widget AUTONOME (gère son propre ID3DXSprite interne paresseux + son propre
    // batch de police, cf. ChatWindow::Render) : partage font_ (paramètre, pas
    // ressource possédée) mais PAS sprite_/white_ (cf. bandeau GameHud.h). Se
    // resynchronise depuis le journal partagé game::g_Client.msg à chaque frame —
    // idempotent, ne republie que les lignes nouvelles (cf.
    // ChatWindow::SyncFromMessageLog).
    chatWindow_.Tick(game::g_World.gameTimeSec);
    chatWindow_.SyncFromMessageLog(game::g_Client.msg);
    chatWindow_.Render(sprite_, font_);
}

// =============================================================================
// OnMouseDown — cGameHud_OnMouseDown 0x62B080
// =============================================================================
bool GameHud::OnMouseDown(int x, int y) {
    lastClickedSlot_ = -1;
    if (!visible_) return false;

    // Fenêtre de chat (§13, mission 2026-07-14) : onglets de canal cliquables
    // (§13b). Testé en premier (« premier consommateur gagne ») ; pas de
    // recouvrement réel avec la quickbar (bas-gauche vs bas-centre).
    if (chatWindow_.OnMouseDown(x, y)) {
        TS2_LOG("GameHud : clic onglet de chat (%d,%d), canal=%d",
                x, y, static_cast<int>(chatWindow_.Channel()));
        return true;
    }

    // Quickbar réelle (§14, mission 2026-07-14). ÉCART DE FIDÉLITÉ ASSUMÉ : le
    // binaire distingue mouse-down (arme un latch, cf. UI_ConsumableBar_OnClick
    // 0x68E3C0) et mouse-up (déclenche l'action, cf. UI_ConsumableBar_ProcInput
    // 0x68E5A0) ; SceneManager::OnMouseUp existant NE ROUTE PAS vers hud_ en scène
    // InGame aujourd'hui (seul OnMouseDown l'est, cf. TODO précis du rapport de
    // mission) — on enchaîne donc OnMouseDown()+OnClick() du widget dans le même
    // appel pour rester utilisable sans ce câblage, au prix de perdre la
    // distinction clic/début-de-drag.
    if (quickBarWindow_) {
        if (quickBarWindow_->OnMouseDown(x, y, slots_)) {
            quickBarWindow_->OnClick(x, y, slots_);
            lastClickedSlot_ = quickBarWindow_->LastDecision().slotIndex;
            TS2_LOG("GameHud : clic quickslot %d (%s)",
                    lastClickedSlot_, quickBarWindow_->LastMessage().c_str());
            return true;
        }
    } else {
        // Repli historique (quickBarWindow_ non alloué, cf. Init()) : ancien
        // hit-test brut sans logique de déclenchement.
        for (int i = 0; i < kQuickSlotCount; ++i) {
            if (layout_.slots[static_cast<size_t>(i)].Contains(x, y)) {
                lastClickedSlot_ = i;
                const QuickSlot& s = slots_[static_cast<size_t>(i)];
                TS2_LOG("GameHud : clic quickslot %d (type=%u ref=%u)",
                        i, static_cast<unsigned>(s.type), s.refId);
                return true;
            }
        }
    }

    // Mini-carte (§12) : bouton bascule taille + clic générique sur le panneau.
    if (minimap_.OnMouseDown(x, y)) {
        TS2_LOG("GameHud : clic mini-carte (%d,%d), grande=%d", x, y, minimap_.BigMode());
        return true;
    }

    // Grille de buffs (§9) + panneau bas-droite (§16) : consommé si le clic tombe
    // dans l'une de ces deux zones (tooltip générique non modélisé, cf.
    // BuffStatusPanel::OnMouseDown).
    if (buffPanel_.OnMouseDown(x, y))
        return true;

    // §7 plaques de cible (mission W4-F2) : panneau d'information pur (pas de sous-hit-test),
    // consommé si le clic tombe dans la zone des plaques actives (premier consommateur gagne,
    // même politique que §8 alliance).
    {
        const TargetPlate tpA = ResolveTargetPlate(0x167468A);
        const TargetPlate tpB = ResolveTargetPlate(0x1674697);
        const int nActive = (tpA.active ? 1 : 0) + (tpB.active ? 1 : 0);
        if (nActive > 0) {
            const HudRect area{ 0, kTargetPlateY0, kTargetPlateW, kTargetPlateStep * nActive };
            if (area.Contains(x, y))
                return true;
        }
    }

    // Cadres alliance/groupe (§8, mission 2026-07-14) : consommé si le clic tombe dans
    // la zone actuellement peuplée (pas de sous-hit-test par ligne, panneau
    // d'information pure — même politique que UI/PartyWindow.cpp/UI/QuestTrackerWindow.h).
    if (AllianceFramesContains(x, y))
        return true;

    // §15 rangée de boutons de menu (mission W4-F2, UI_GameHud_Render 0x685177) : ~17
    // icônes autour de la quickbar, ancrées sur le fond de quickbar (this[0]/this[1],
    // EA 0x684CB0/0x684CBC = coin layout_.quickBar + 4). Action réelle = sub_4C1110(0)
    // (toggle d'un flag U8 this+flagOff -> ouvre une fenêtre non identifiée) : ici on
    // CONSOMME + log, sans ouverture de GameWindows (cible non prouvée statiquement).
    {
        const int anchorX = layout_.quickBar.x + 4; // this[0] (0x684CB0)
        const int anchorY = layout_.quickBar.y + 4; // this[1] (0x684CBC)
        for (const MenuBtn& mb : kMenuButtons) {
            const HudRect r{ anchorX + mb.dx, anchorY + mb.dy, kMenuBtnW, kMenuBtnH };
            if (r.Contains(x, y)) {
                lastClickedSlot_ = -1;
                TS2_LOG("GameHud : clic bouton de menu (flag+%d) a (%d,%d)", mb.flagOff, x, y);
                return true; // sub_4C1110(0) : toggle flag (fenetre cible non identifiee)
            }
        }
    }

    // Clic sur le cadre vitales ou le panneau de quickslots : consommé (bloque
    // le passage à la scène monde derrière le HUD).
    if (layout_.frame.Contains(x, y) || layout_.quickBar.Contains(x, y))
        return true;

    return false;
}

// =============================================================================
// Perte / restauration du device
// =============================================================================
void GameHud::OnDeviceLost() {
    sprite_.OnLostDevice();
    font_.OnDeviceLost();
    // white_ est en D3DPOOL_MANAGED : rien à faire.
    buffPanel_.OnDeviceLost(); // ID3DXSprite propre à BuffStatusPanel
}

void GameHud::OnDeviceReset() {
    sprite_.OnResetDevice();
    font_.OnDeviceReset();
    buffPanel_.OnDeviceReset();
}

} // namespace ts2::ui

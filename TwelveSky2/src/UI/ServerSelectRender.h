// UI/ServerSelectRender.h — RENDU VISUEL de l'écran ServerSelect (ts2::ui).
//
// Réécriture fidèle de la GÉOMÉTRIE réelle (positions/dimensions) de :
//   Scene_ServerSelectRender      0x519250 (~1,3 Ko)  — corps principal
//   ServerSelect_GetButtonX       0x519F40             — colonne X d'un bouton serveur
//   ServerSelect_GetButtonY       0x51A0A0             — rangée Y d'un bouton serveur
//   ServerSelect_GetButtonImageId 0x51A220             — id sprite (atlas unk_8E8B50) d'un bouton
//   ServerSelect_DrawLoadBar      0x51A440             — barre de charge + pastille « plein »
//   UI_ProjectSpriteToScreen      0x50F5D0             — ancrage résolution-indépendant du bouton retour
// décompilées via idaTs2 (serveur HTTP JSON-RPC http://127.0.0.1:13337/mcp, méthode
// "decompile", le MCP `idaTs2` n'étant pas exposé en outil différé dans cette session —
// MÊME IDB que le MCP, aucune donnée inventée).
//
// CÂBLAGE ASSETS RÉELS (2026-07-14, cf. Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md, preuve
// EA par EA) — ce module charge désormais RÉELLEMENT les sprites `.IMG` de l'atlas
// partagé `g_AssetMgr_UiAtlasSlots` (mêmes fichiers `G03_GDATA\D01_GIMAGE2D\001\
// 001_%05d.IMG` que Sprite2D_BuildPath 0x4D68E0, DÉCALAGE +1 slot->fichier confirmé) via
// GetSprite() (cache paresseux asset::ImgFile::Load + gfx::GpuTexture::CreateFromImgFile,
// MÊME pattern que UI/InventoryWindow.cpp::GetIconTex / UI/PanelSkin.h) :
//   - fond plein écran (aléatoire 2380/2381, this[168])            : §1.2 du doc
//   - panneau central (slot 1785 -> 001_01786.IMG, unk_929344)     : §1.3
//   - barre de charge, 8 paliers + pastille "inconnu" (slots 1899..1907 -> 001_01900..
//     001_01908.IMG, unk_92D52C..unk_92D9CC)                       : §1.4/1.5
//   - pastille "serveur plein" (slot 2599 -> 001_02600.IMG, unk_9469DC) : §1.4/1.5
//   - bouton d'action bas-droite, 3 états (slots 4/5/6 -> 001_00005/6/7.IMG,
//     unk_8E8DA0/unk_8E8E34/unk_8E8EC8)                            : §1.5
// AUCUN repli visuel : si un fichier `.IMG` ne peut être chargé au runtime (device D3D9
// pas prêt ou fichier réellement absent), le sprite correspondant n'est tout simplement
// PAS dessiné — comportement FIDÈLE de Sprite2D_Draw/Sprite2D_DrawScaled (qui n'affichent
// rien si EnsureLoaded échoue). Le binaire ne dessine QUE des sprites `.IMG` réels : aucun
// FillRect coloré, aucun cadre, aucun texte de titre/label inventé. Tous les fichiers
// ci-dessus sont confirmés présents sur disque
// (ClientSource/TwelveSky2/GameData/G03_GDATA/D01_GIMAGE2D/001/). GetSprite() a besoin d'un
// device D3D9 valide : l'appelant DOIT appeler SetDevice() une fois le device créé
// (LoginScene::Init, même pattern que InventoryWindow::Init(gfx::Renderer&)).
//
// MODE RÉELLEMENT ACTIF (2026-07-14, réaudit complet du doc ci-dessus, §2) : avec la
// commande de lancement documentée (`/0/0/2/1024/768`), g_ServerModeFlag vaut 0 —
// Scene_ServerSelectUpdate ne construit alors QU'UN SEUL serveur, hôte
// "12sky2-login.geniusorc.com", port 8088 (0x1F98) — ET Scene_ServerSelectRender prend
// la branche `else` (EA 0x5194DD, singleServerMode=false ci-dessous, PAS la branche
// "gros nombre" UI_DrawNumberValue qui, elle, appartient à g_ServerModeFlag!=0). Cette
// branche `else` est une boucle sur les boutons serveur, mais avec UNE SEULE entrée
// construite par Update, elle ne dessine jamais qu'UN SEUL bouton — ce n'est donc PLUS
// une grille multi-canaux : l'appelant (UI/LoginScene.cpp::BuildServerList) construit
// désormais exactement CETTE entrée unique (plus les 6 canaux `MultiChannel` d'antan),
// ce qui fait de la boucle ci-dessous une reproduction FIDÈLE (et non plus un
// "compromis assumé") du chemin réellement emprunté par le binaire pour cette commande
// de lancement. Le mode `MultiChannel` (6 canaux, `g_ServerModeFlag` != 0/1/2) reste
// documenté et modélisé dans Game/ServerSelectFlow.h::ServerListMode/BuildServerList()
// comme référence/option future — ce n'est plus le chemin par défaut de LoginScene.
//
// === GÉOMÉTRIE RÉELLE EXTRAITE (0x519250) ===
//
// Résolution de référence = ts2::kRefWidth/kRefHeight (flt_1669178/flt_166917C =
// 1024x768). Le fond plein écran (this[168] = ServerSelectState::backgroundImageId,
// indexe l'atlas partagé unk_8E8B50, pas de stride 148 o/entrée — MÊME atlas que les
// logos Intro et les boutons serveur) est dessiné À L'ÉCHELLE (0,0) avec :
//   scaleX = nWidth  / kRefWidth   (v13, EA 0x519435)
//   scaleY = nHeight / kRefHeight  (v14, EA 0x519419)
//
// Le panneau central (sprite unk_929344, slot 1785 -> 001_01786.IMG, chargé RÉELLEMENT
// via GetSprite() ; dimensions RÉELLES de la texture utilisées pour le centrage quand
// elle est disponible — EXACTEMENT Sprite2D_GetWidth/Height(unk_929344) de l'original —,
// repli sur les dimensions réelles connues kPanelW/kPanelH (737x755, en-tête IMG) pour
// le seul CENTRAGE si le chargement échoue — le panneau n'est alors pas dessiné,
// jamais remplacé par un aplat)
// est CENTRÉ à l'écran :
//   baseX = nWidth/2  - panelW/2   (v24, EA 0x519486)
//   baseY = nHeight/2 - panelH/2   (v17, EA 0x5194A9)
// C'est l'origine (baseX, baseY) à laquelle s'ajoutent TOUS les offsets ci-dessous
// (boutons serveur ET barres de charge partagent cette même origine).
//
// Boutons serveur (ServerSelect_GetButtonX/Y 0x519F40/0x51A0A0) : offsets EXACTS
// relevés dans les switch(id) du désassemblage, id = index dans la boucle
// [selectedGroupBtnLo..selectedGroupBtnHi] (this[15372]/this[15373], EXACTEMENT les
// bornes de ServerSelectState::selectedGroupBtnLo/Hi). Les ids observés dans les 3
// fonctions de layout sont épars : 0..9 (rangées d'un groupe, jusqu'à 10 serveurs,
// colonne UNIQUE X=+291) et 60 (bouton spécial, colonne X=+539) ; 40 et 50 n'ont PAS de
// case dans GetButtonX (défaut -> offset 0, fidèle à l'original — comportement bord
// reproduit tel quel). Les ids 6..9 réutilisent LES MÊMES rangées Y que 0..3 (196/278ish/
// 378/469 vs 196/287/378/469 — légère divergence 278 vs 287 au cas 7, EXACTE au
// désassemblage, pas une coquille) : ce sont vraisemblablement les boutons d'un AUTRE
// groupe occupant le MÊME espace écran (un seul groupe visible à la fois, cf.
// ServerSelectFlow.h::selectedGroup) ; 40/50/60 sont des slots spéciaux (TODO sémantique
// exacte non confirmée — cf. kSpecialSlotXxx ci-dessous) non modélisés dans
// ServerSelectState (pas de champ dédié), exposés ici pour complétude/documentation mais
// NON dessinés par la boucle principale (qui n'itère que sur des indices de
// ServerSelectState::servers, 0-based, résolument dans la plage 0..9 en pratique — 6
// entrées en mode MultiChannel, 1 en SingleServer, cf. Game::BuildServerList).
//
// Barres de charge (ServerSelect_DrawLoadBar 0x51A440) : 2 sprites par serveur, à la
// MÊME origine (baseX,baseY) —
//   barre de charge (pastille de niveau, 8 sprites empilés unk_92D52C..unk_92D938, pas
//     148 o) à (baseX+barOffX, baseY+barOffY) ;
//   pastille "complet" (unk_9469DC, dessinée SEULEMENT si population >= maxPopulation)
//     à (baseX+fullOffX, baseY+fullOffY).
// Niveau de la barre : 8 paliers, seuil = loadStep * k (k=1..7), la valeur loadStep
// provenant de this[id+13371] dans le binaire — modélisée FIDÈLEMENT par le champ
// ServerEntry::loadStep (ajouté 2026-07-15), alimenté comme maxPopulation par le record de
// statut du serveur (Net_QueryServerStatus 0x519CC0, octets 9-12). LoadLevel() reproduit
// EXACTEMENT la chaîne de comparaisons pop >= k*loadStep (population "pending", <0, affiche
// la pastille dédiée unk_92D9CC).
//
// Bouton retour (bas de l'écran, UI_ProjectSpriteToScreen 0x50F5D0, appelé avec
// (imgId=4, refX=891, refY=701)) : ancrage RÉSOLUTION-INDÉPENDANT distinct du centrage
// panneau, réutilisant le même facteur d'échelle que le fond (nWidth/kRefWidth,
// nHeight/kRefHeight) mais SANS centrage écran — c'est un ancrage de coin/HUD :
//   outX = Crt_ftol(scaleX * (891 + w/2)) - w/2
//   outY = Crt_ftol(scaleY * (701 + h/2)) - h/2
// où w/h = dimensions du sprite atlas[4] (PAS le sprite réellement dessiné ensuite —
// unk_8E8DA0/unk_8E8E34/unk_8E8EC8, normal/survolé/enfoncé — fidèle au binaire qui
// calcule la position sur un sprite et blitte un AUTRE sprite à la position obtenue,
// en supposant implicitement qu'ils ont la même taille). CONFIRMÉ (Docs/TS2_LOGIN_BUTTON_
// ASSETS.md §"Exception notée") : slot 4/5/6 -> 001_00005/6/7.IMG, chargés RÉELLEMENT ici
// (normal/survolé/enfoncé), rien dessiné si absent. État : this[3] du
// SceneMgr, PAS modélisé dans ServerSelectState (scène ServerSelect réutilise this[3] pour
// le latch du bouton retour, alors qu'Intro utilise this[3..152] pour logoFade — même
// mémoire brute, interprétation différente par scène) : latch géré ICI localement
// (cf. ServerSelectRender::OnActionButtonMouseDown/Up) : le latch this[3] est armé au down
// et VALIDÉ au up (OnActionButtonMouseUp renvoie true si le curseur est toujours sur le
// sprite), ce qui déclenche la confirmation de sortie modale dans LoginScene
// (Scene_ServerSelectOnMouseUp 0x519AC0 -> UI_MsgBox_Open dword_1822438 action_id=1 ->
// g_QuitFlag=1).
//
// Mode d'affichage (g_ServerModeFlag == dword_166918C, EXACTEMENT le même global que
// ServerSelectFlow.h::ServerListMode, adresse 0x166918C) : si non nul -> panneau "un
// seul gros nombre" (UI_DrawNumberValue, EA 0x53FCC0, sur le serveur 0 uniquement) ; si
// nul -> boucle boutons (singleServerMode=false ci-dessous).
//
// RÉAUDIT (2026-07-14, re-décompilation intégrale de Scene_ServerSelectRender 0x519250,
// EA 0x5194CB `if (g_ServerModeFlag)`, puis re-vérification complète via
// Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md) : le test réel est un simple `if
// (g_ServerModeFlag)` en C. Pour LA COMMANDE DE LANCEMENT DOCUMENTÉE PAR CE PROJET
// (`/0/0/2/1024/768` -> dword_166918C = 0, cf. WinMain EA 0x4609F1/0x460BAE) :
//   - dword_166918C == 0 (SEUL CAS ACTIF ICI) -> Update construit 1 SEUL serveur (hôte
//     "12sky2-login.geniusorc.com", port 8088) ; Render prend la branche `else`
//     (EA 0x5194DD, singleServerMode=false) -> dessine exactement CE seul bouton, sa
//     barre de charge (9 sprites réels) et le bouton d'action (3 états réels). AUCUNE
//     grille multi-boutons n'est jamais produite par le binaire pour cette commande.
//   - dword_166918C == 1 ou 2 -> 1 seul serveur (port 8088, variante GameGuard "EUTest") ;
//     Render prend la branche gros nombre (EA 0x519664, singleServerMode=true) —
//     conservée dans ce module pour fidélité de l'autre chemin, mais N'EST PAS le chemin
//     emprunté par la commande de lancement documentée.
//   - dword_166918C == autre (!=0,1,2) -> Update construit 6 canaux (EA 0x518F6E) mais
//     Render prend AUSSI la branche gros nombre (même test) -> un seul nombre, serveur 0 ;
//     les 5 autres canaux ne sont JAMAIS dessinés comme boutons. Ce mode `MultiChannel`
//     reste modélisé dans Game/ServerSelectFlow.h::ServerListMode comme référence/option
//     future ; ce n'est PLUS ce que UI/LoginScene.cpp construit par défaut.
// CÂBLAGE ACTUEL (UI/LoginScene.cpp::BuildServerList) : construit désormais EXACTEMENT
// l'entrée SingleServer unique ci-dessus (hôte/port confirmés, cf. Net/Login.h::
// kLoginHostCom) et appelle Render(..., singleServerMode=false) avec cette unique entrée
// bornée en 0..0 — la boucle `else` ci-dessous n'est donc plus un "compromis" mais une
// reproduction FIDÈLE du chemin réellement actif du binaire pour la commande de
// lancement documentée par ce projet.
#pragma once
#include "UI/UIManager.h"          // ts2::ui::UiContext
#include "Game/ServerSelectFlow.h" // ts2::game::ServerSelectState
#include "Gfx/GpuTexture.h"        // gfx::GpuTexture (fond/boutons réels, atlas unk_8E8B50)
#include <cstdint>
#include <unordered_map>

namespace ts2::ui {

// ---------------------------------------------------------------------------
// Constantes/tables de layout — valeurs EXACTES extraites des switch(id) du
// désassemblage (voir commentaire d'en-tête). Regroupées en espace de noms pour
// pouvoir être réutilisées/testées indépendamment de la classe de rendu.
// ---------------------------------------------------------------------------
namespace serverselect_layout {

// Dimensions EXACTES des sprites consultés par Sprite2D_GetWidth/Height dans l'IDB :
// - panneau unk_929344 : GetWidth 0x519486 / GetHeight 0x5194A9 -> 001_01786.IMG = 737x755.
// - boutons serveur : Sprite2D_HitTest 0x51958C/0x5199E6 sur ButtonImageId(i) -> 153x23.
// - bouton retour atlas[4] : UI_ProjectSpriteToScreen 0x5196D1/0x519A79/0x519AED -> 96x31.
// Elles servent au positionnement/hit-test de repli quand la texture n'a pas encore été
// chargée ; Render() préfère les dimensions runtime réelles quand GetSprite() a réussi.
constexpr int kPanelW    = 737;
constexpr int kPanelH    = 755;
constexpr int kButtonW   = 153;
constexpr int kButtonH   = 23;
constexpr int kBackBtnW  = 96;
constexpr int kBackBtnH  = 31;

// ---------------------------------------------------------------------------
// Slots d'atlas CONFIRMÉS par désassemblage (Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md +
// Docs/TS2_LOGIN_BUTTON_ASSETS.md, session 2026-07-14). Index 0-based passés à
// ServerSelectRender::GetSprite() (qui applique lui-même le décalage +1 vers le nom de
// fichier réel `001_%05d.IMG`, cf. Sprite2D_BuildPath 0x4D68E0). Tous les fichiers
// correspondants sont confirmés présents sur disque
// (ClientSource/TwelveSky2/GameData/G03_GDATA/D01_GIMAGE2D/001/).
// ---------------------------------------------------------------------------

// Panneau central (unk_929344) : slot 1785 -> fichier 001_01786.IMG.
constexpr int kPanelImgSlot = 1785;

// Barre de charge (ServerSelect_DrawLoadBar 0x51A440) : 8 paliers croissants
// (unk_92D52C..unk_92D938) -> fichiers 001_01900.IMG..001_01907.IMG, PUIS la pastille
// "population inconnue" (unk_92D9CC, pop < 0) -> fichier 001_01908.IMG.
constexpr int kLoadBarStepSlot[8] = {1899, 1900, 1901, 1902, 1903, 1904, 1905, 1906};
constexpr int kLoadBarPendingSlot = 1907; // pop < 0 (interrogation en cours)

// Pastille "serveur plein" (unk_9469DC, pop >= maxPop) -> fichier 001_02600.IMG.
constexpr int kLoadBarFullSlot = 2599;

// Bouton d'action bas-droite (unk_8E8DA0/unk_8E8E34/unk_8E8EC8), 3 états ->
// fichiers 001_00005.IMG / 001_00006.IMG / 001_00007.IMG (Docs/TS2_LOGIN_BUTTON_ASSETS.md
// §"Exception notée : ServerSelect n'utilise PAS" la paire OK/Quitter du Login).
constexpr int kActionBtnNormalSlot   = 4;
constexpr int kActionBtnHoverSlot    = 5;
constexpr int kActionBtnPressedSlot  = 6;

// Ids "spéciaux" observés dans les 3 tables de layout mais absents de
// ServerSelectState (pas de champ dédié, sémantique non confirmée) — exposés pour
// documentation/complétude, NON dessinés par ServerSelectRender::Render.
constexpr int kSpecialSlotA = 40; // GetButtonImageId -> 3452, GetButtonX -> défaut (0)
constexpr int kSpecialSlotB = 50; // GetButtonImageId -> 3099, GetButtonX -> défaut (0)
constexpr int kSpecialSlotC = 60; // GetButtonImageId -> 2630, colonne X propre (+539)

// ServerSelect_GetButtonX 0x519F40 : offset X (relatif à baseX) d'un bouton serveur par
// id. Défaut = 0, fidèle au binaire (pas de case default explicite -> résultat non
// initialisé réellement, mais la fonction déclare `result` non initialisé SEULEMENT
// pour les ids hors table ; on documente 0 comme reproduction sûre du chemin "défaut"
// qui existe, lui, explicitement dans GetButtonX/Y/ImageId — DrawLoadBar n'a PAS de tel
// défaut, cf. GetLoadBarOffsets ci-dessous).
int ButtonOffsetX(int id);

// ServerSelect_GetButtonY 0x51A0A0 : offset Y (relatif à baseY) d'un bouton serveur.
int ButtonOffsetY(int id);

// ServerSelect_GetButtonImageId 0x51A220 : id sprite (atlas unk_8E8B50, pas 148 o) du
// bouton "relâché" ; le bouton "survolé/actif" est l'entrée SUIVANTE (+148 o = +1 index)
// dans l'atlas, cf. l'appel Sprite2D_Draw(...+ v23 + 148, ...) à l'EA 0x5195F6.
int ButtonImageId(int id);

// Offsets des 2 pastilles dessinées par ServerSelect_DrawLoadBar 0x51A440 (relatifs à
// baseX/baseY). `valid=false` pour un id hors des cases 0..9/60 explicitement gérées
// (le binaire lit alors des variables locales NON INITIALISÉES — comportement non
// reproductible fidèlement ; on ne dessine rien, plus sûr que d'inventer une position).
struct LoadBarOffsets {
    int  barOffX = 0, barOffY = 0;   // pastille de niveau (unk_92D52C..unk_92D938 / pending unk_92D9CC)
    int  fullOffX = 0, fullOffY = 0; // pastille "complet" (unk_9469DC)
    bool valid = false;
};
LoadBarOffsets GetLoadBarOffsets(int id);

} // namespace serverselect_layout

// ---------------------------------------------------------------------------
// ServerSelectRender — dessine l'écran ServerSelect à partir d'un
// game::ServerSelectState en lecture seule. Ne modifie JAMAIS l'état passé.
// Conserve un unique bit d'état PUREMENT visuel (latch du bouton retour, this[3]
// dans le binaire pour cette scène) — la logique de clic réelle reste dans
// Game::ServerSelectFlow.h (OnServerClicked/OnGroupClicked/OnActionButtonReleased).
// ---------------------------------------------------------------------------
class ServerSelectRender {
public:
    // Doit être appelé UNE FOIS après création du device D3D9 (UI/LoginScene.cpp::Init,
    // même pattern que InventoryWindow::Init(gfx::Renderer&)) : sans device, GetSprite()
    // ne peut charger aucune texture réelle et Render() ne dessinera RIEN (aucun sprite —
    // et jamais d'aplat de repli). `ctx.renderer` (UI/UIManager.h) n'est
    // volontairement PAS utilisé ici : LoginScene ne possède qu'un IDirect3DDevice9* brut
    // (pas de gfx::Renderer), exactement comme UI/InventoryWindow.cpp/UI/PanelSkin.cpp.
    void SetDevice(IDirect3DDevice9* device) { device_ = device; }

    // Rendu complet (fond + panneau + boutons/barres de charge + bouton retour), TOUS
    // dessinés avec les sprites RÉELS de l'atlas quand SetDevice() a été appelé et que les
    // fichiers sont présents (cf. constantes de slots ci-dessus) ; un sprite dont le
    // fichier `.IMG` ne peut être chargé n'est simplement PAS dessiné (aucun aplat coloré,
    // aucun cadre, aucun texte inventé — fidèle à Sprite2D_Draw).
    // `cursorX/cursorY` : position curseur CLIENT (comme Dialog::Render — le binaire
    // fait lui-même GetPhysicalCursorPos+ScreenToClient à l'EA 0x5193E7/0x5193F8 ; on
    // suit ici le pattern UIManager::Render qui calcule le curseur UNE fois pour tous
    // les éléments et le repasse en paramètre, cf. UI/UIManager.h::Dialog::Render).
    // `singleServerMode` : reflète g_ServerModeFlag (voir note de tête de fichier) ; false
    // par défaut = branche boucle (EA 0x5194DD), CAS RÉELLEMENT ACTIF pour la commande de
    // lancement documentée par ce projet, désormais bornée par l'appelant à l'unique
    // entrée SingleServer (plus de grille multi-canaux).
    // Appelé deux fois par frame par le pilote de scène (une fois par UiPhase, comme
    // Dialog::Render / MsgBoxDialog::Render) ; le dessin des sprites se filtre en interne
    // sur la phase Panels.
    void Render(const UiContext& ctx, const game::ServerSelectState& state,
                int cursorX, int cursorY, bool singleServerMode = false);

    // Latch du bouton d'action/sortie (mirroir this[3] pour CETTE scène uniquement,
    // Scene_ServerSelectOnMouseDown/Up 0x519780/0x519AC0). À appeler depuis les routeurs
    // souris de la scène ServerSelect. OnActionButtonMouseDown arme le latch si le curseur
    // est sur le sprite (EA 0x519AAF : this[3]=1). OnActionButtonMouseUp désarme le latch
    // (EA 0x519AFE : this[3]=0) et renvoie true si le clic est CONFIRMÉ (latch armé ET
    // curseur toujours sur le sprite, Sprite2D_HitTest EA 0x519B1A) — l'appelant ouvre alors
    // la confirmation de sortie (UI_MsgBox_Open, EA 0x519B3E).
    void OnActionButtonMouseDown(int cursorX, int cursorY, const UiContext& ctx);
    bool OnActionButtonMouseUp(int cursorX, int cursorY, const UiContext& ctx);

private:
    // Résout un slot de l'atlas unk_8E8B50 (AssetMgr_InitAllSlots 0x4deb50, catégorie 1 ->
    // "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG") vers sa texture GPU, cache paresseux
    // (MÊME pattern que UI/InventoryWindow.cpp::GetIconTex). DÉCALAGE +1 CONFIRMÉ par
    // décompilation directe (Sprite2D_BuildPath 0x4d68e0 formate le fichier avec
    // `slot+1`) : le fichier réel d'un slot `id` est 001_<id+1>.IMG, PAS 001_<id>.IMG —
    // vérifié par le contenu réel (ButtonImageId(i)+1 = bouton 153x23 DXT3 cohérent, alors
    // que ButtonImageId(i) lui-même pointe vers un asset sans rapport). Nécessite
    // device_ (cf. SetDevice ci-dessus) ; renvoie nullptr (rien dessiné côté
    // appelant, jamais d'aplat de repli) si le device n'est pas prêt ou si le fichier est
    // absent/illisible.
    gfx::GpuTexture* GetSprite(int slotIndex);

    IDirect3DDevice9* device_ = nullptr; // cf. SetDevice() — requis pour charger les .IMG réels
    bool actionButtonPressed_ = false;
    std::unordered_map<int, gfx::GpuTexture> spriteCache_; // slot -> texture (lazy, category 1)
};

} // namespace ts2::ui

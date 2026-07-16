// UI/BuffStatusPanel.h — grille de buffs/debuffs (7 colonnes) + panneau de statut
// bas-droite (4 icônes + indicateur de cast). Sous-ensemble de UI_GameHud_Render
// 0x67A3C0 documenté intégralement dans Docs/TS2_UI_GAMEHUD_RENDER.md §9 (grille,
// EA 0x67BD54-0x67D9DA) et §16 (panneau bas-droite, EA 0x6865BF-0x6868AB).
//
// ---------------------------------------------------------------------------------
// PÉRIMÈTRE ET SIMPLIFICATIONS (mission du 2026-07-14) :
//
//  §9 — la grille d'origine a ~50 CONDITIONS DE DÉCLENCHEMENT DISTINCTES, chacune
//  lisant une variable globale d'un système de jeu différent (combos élémentaires,
//  synergie de paire, comparaison de loadout de compétences, rang/grade, gemme d'arme
//  incrustée, harmonie/mésentente élémentaire, 36 debuffs à durée, bonus temporel
//  serveur, buff nourriture morph, maîtrise élémentaire, drapeaux divers — cf. doc §9
//  points 1-14). Cette classe consomme un modèle GÉNÉRIQUE — le vecteur
//  `game::PlayerEntity::buffs` de `{id, expiryTime}` (voir Game/GameState.h, struct
//  ActiveBuff) — qui vaut pour n'importe laquelle des ~50 sources d'origine une fois
//  qu'un système amont y poussera un état, sans jamais bloquer le rendu du widget en
//  attendant.
//
//  CÂBLAGE PARTIEL (mission "CABLAGE GRILLE DE BUFFS", 2026-07-14) : sur ces ~50
//  conditions, 8 lisent une adresse qui a un écrivain RÉEL déjà câblé côté
//  Net/GameVarDispatch.cpp ou Net/CharStatDeltaDispatch.cpp (donc une valeur qui peut
//  effectivement devenir non nulle en jeu, pas juste un champ qui existe sur le
//  papier) : §9.5 (rang/grade), §9.7 (2 des 3 drapeaux simples), §9.11 (bonus temporel
//  serveur), §9.13 (maîtrise élémentaire), §9.14 (3 des 4 drapeaux additionnels).
//  `CollectWiredConditionBuffs` (.cpp) les lit chaque frame via `game::g_Client.VarGet`
//  et les fusionne avec `self.buffs` (toujours le point d'ancrage pour les ~40 autres
//  sources, non câblées ici faute d'écrivain confirmé OU par conflit avéré avec un
//  autre système déjà modélisé — liste exhaustive et justification par adresse dans le
//  bandeau de tête de CollectWiredConditionBuffs, .cpp). Le widget ne reste donc plus
//  vide par défaut : dès qu'un des 8 globals ci-dessus prend une valeur (handler réseau
//  déjà câblé), l'icône correspondante apparaît sans action supplémentaire.
//
//  §16 — le cadre bas-droite et les 4 icônes on/off sont des fichiers .IMG RÉELLEMENT
//  résolus (voir kStatusFrameFile / kStatusIcons ci-dessous). L'indicateur de cast
//  animé (8 frames, cycle `Crt_ftol(g_GameTimeSec*16)%8` @0x6865BF+) n'a PAS de
//  séquence d'icônes identifiée dans le désassemblage — seules la formule de
//  déclenchement (`dword_1685E74[g_LocalElement]`) et le cycle de frame sont connus —
//  d'où un repli PERMANENT sur pastille pulsante + texte de frame (ce n'est pas un
//  échec de chargement, la ressource n'a simplement pas pu être identifiée cette
//  session). CÂBLAGE DU DÉCLENCHEMENT (mission 2026-07-14, cf. bandeau
//  UI/GameHud.cpp::Render()) : `SetCasting()` était écrit mais jamais appelé -> pastille
//  éteinte en permanence. Désormais piloté chaque frame depuis
//  `game::g_World.Self().anim.state` (CharAnimState/ActionFsm, DÉJÀ modélisé et tenu à
//  jour par SceneManager::host.UpdateEntityAnimFrame) : vrai quand l'état vaut
//  CastSlot0/1/2 (windup compétence, « préparation ») ou Channel (« canalisation »),
//  fidèle à la sémantique du commentaire IDA d'origine.
//
// ---------------------------------------------------------------------------------
// MÉTHODE DE RÉSOLUTION DES ICÔNES « RÉELLES » (kKnownIcons / kStatusIcons) :
//
// Toutes les icônes non marquées « pastille » ci-dessous sont dérivées par LA MÊME
// méthode de RE statique déjà appliquée à unk_8EC114 dans GameHud.cpp (voir son
// bandeau de tête, étapes 1-8) : une adresse symbolique `unk_XXXXXX` citée par le
// décompilé de UI_GameHud_Render est un élément de la table Sprite2D PARTAGÉE de
// catégorie 1 (base `unk_8E8B50`, pas 148 octets, remplie par
// `AssetMgr_InitAllSlots 0x4DEB50` : `for(i=0;i<4500;++i) Sprite2D_BuildPath(this+148*i+32, 1, i, 0)`,
// template `"G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"`, a3+1 = i+1). Donc pour une
// adresse A appartenant à cette table : `i = (A - 0x8E8B50) / 148` (DOIT diviser
// exactement, reste 0) et le fichier correspondant est `001_%05d.IMG` avec
// `%05d = i + 1`. Vérifié : DIVISION EXACTE (reste 0) pour les 42 adresses utilisées
// dans ce fichier (33 dans kKnownIcons + 9 dans kStatusIcons/kStatusFrameFile), ce
// qui confirme statistiquement leur appartenance à cette table plutôt qu'une
// coïncidence numérique.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/GpuTexture.h"
#include "Game/GameState.h" // game::ActiveBuff (CollectWiredConditionBuffs, mission cablage 2026-07-14)

namespace ts2::ui {

// Identifiants de buff/debuff « catalogue » pour le modèle générique
// `game::PlayerEntity::buffs` (Game/GameState.h, struct ActiveBuff::id). Les valeurs
// 0..kBuffKnownIconCount-1 pointent vers une icône .IMG réellement résolue (table
// kKnownIcons, §9 points 1-9 et 11.max/14) ; toute AUTRE valeur (id négatif, ou
// id >= kBuffKnownIconCount) retombe sur une pastille colorée générique dérivée de
// l'id (voir PillColorForId, .cpp).
//
// MISE À JOUR (vague W9, 2026-07-16) : la banque de 36 debuffs à durée §9.10 n'utilise
// PAS cet espace d'ids — elle a sa propre table d'atlas `unk_A60D04`, désormais
// RÉSOLUE (elle ne l'était pas lors des passes précédentes) : c'est la base de la
// CATÉGORIE 6 d'AssetMgr_InitAllSlots 0x4DEB50 (@0x4DECEA :
// `Sprite2D_BuildPath(this + 5180*ii + 148*jj + 1540564, 6, ii, jj)`, avec
// this = 0x8E8B30 -> 0x8E8B30 + 1540564 = 0xA60D04, EXACT), soit le gabarit
// "G03_GDATA\D01_GIMAGE2D\007\007_%03d%03d.IMG" (Sprite2D_BuildPath 0x4D68E0 case 6,
// arguments a3+1 et a4+1). Voir GetBankIconTex (.cpp) et le type GridEntry ci-dessous.
enum BuffIconId : int {
    kBuffComboA = 0, kBuffComboB, kBuffComboC,                    // §9.1 combos élémentaires (dword_184C218)
    kBuffElemState1, kBuffElemState2, kBuffElemState3, kBuffElemState4, // §9.2 état élémentaire local
    kBuffElemPair,                                                 // §9.3 synergie de paire élémentaire
    kBuffLoadoutGood, kBuffLoadoutBad,                             // §9.4 comparaison loadout de compétences
    kBuffRankDefault, kBuffRank1, kBuffRank2, kBuffRank3,          // §9.5 bonus de rang/grade
    kBuffGem1, kBuffGem2, kBuffGem3, kBuffGem4,                    // §9.6 gemme d'arme incrustée
    kBuffFlagA, kBuffFlagB, kBuffFlagC,                            // §9.7 drapeaux simples de statut
    kBuffHarmony, kBuffMismatch,                                   // §9.8 harmonie/mésentente élémentaire
    kBuffMisc1, kBuffMisc2, kBuffMisc3, kBuffMisc4, kBuffMisc5,    // §9.9 bonus divers par élément
    kBuffServerBonusMax,                                           // §9.11 bonus temporel serveur (cas ==360)
    kBuffFlagAdd1, kBuffFlagAdd2, kBuffFlagAdd3, kBuffFlagAdd4, kBuffFlagAdd5, // §9.14 drapeaux additionnels

    // --- Ajouts mission "CABLAGE GRILLE DE BUFFS" (2026-07-14) : sources dynamiques (la
    // valeur du global, pas seulement son signe, sélectionne l'icône) -- cf. bandeau de
    // tête du .cpp (CollectWiredConditionBuffs) pour la méthode de vérification qui a
    // confirmé ces deux sources comme réellement modélisées côté ClientRuntime.
    kBuffElemMastery1, kBuffElemMastery2, kBuffElemMastery3, kBuffElemMastery4, // §9.13
    kBuffElemMastery5, kBuffElemMastery6, kBuffElemMastery7,                   // g_ElementMastery 1..7
    // confirmé par IDA (data_refs 0x1675680) : commentaire IDB d'origine "élément
    // maîtrisé 1..7 -> +1000 stat correspondante" -- borne HAUTE réelle, pas une
    // supposition.
    kBuffServerBonusMin2, kBuffServerBonusMin3, kBuffServerBonusMin4, kBuffServerBonusMin5, // §9.11
    // dword_1674AB0 en minutes pleines (2..5) ; 360s (=6 min) reste couvert par
    // kBuffServerBonusMax ci-dessus (icône FIXE différente dans le binaire, pas la même
    // table indexée par minute).
    kBuffKnownIconCount
};

// ---------------------------------------------------------------------------------
// BuffStatusPanel — dessine §9 (grille 7 colonnes) et §16 (panneau bas-droite).
// Classe AUTONOME (comme InventoryWindow) : possède son propre SpriteBatch et un
// cache paresseux de GpuTexture par icône ; reçoit le device via Init() et une
// police PARTAGÉE (non possédée, comme InventoryWindow::font_) pour le texte.
// N'implémente PAS l'interface Dialog/UIManager : câblée directement dans
// GameHud.cpp (instance file-local), comme documenté dans son bandeau de tête.
class BuffStatusPanel {
public:
    BuffStatusPanel() = default;
    ~BuffStatusPanel() { Shutdown(); }
    BuffStatusPanel(const BuffStatusPanel&)            = delete;
    BuffStatusPanel& operator=(const BuffStatusPanel&) = delete;

    // Prend le device du renderer + une police partagée (non possédée, peut être
    // nullptr — repli sur icônes/pastilles sans texte).
    bool Init(gfx::Renderer& renderer, gfx::Font* font);
    void Shutdown();

    void SetScreenSize(int width, int height);

    // Dessine §9 (grille de buffs) puis §16 (panneau bas-droite + cast). Gère son
    // propre sprite_.Begin()/End() et sa propre passe police (font_ partagée, batch
    // dédié) : peut être appelée indépendamment de tout autre pass UI.
    void Render();

    // Hit-test minimal (grille + panneau bas-droite) : renvoie true si le clic tombe
    // dans une zone gérée par ce widget (règle « premier consommateur gagne », cf.
    // doc §9 « Toutes les icônes de la grille sont cliquables » — Sprite2D_HitTest ->
    // sub_4C1110(0), ouverture de tooltip/fenêtre détail générique). Aucune action
    // n'est déclenchée ici (pas de système de tooltip modélisé) : seule la
    // consommation de l'événement est reproduite.
    bool OnMouseDown(int x, int y);

    // Autour d'un Reset() de device D3D9.
    void OnDeviceLost();
    void OnDeviceReset();

    // --- Panneau bas-droite (§16) : hooks TODO --------------------------------------
    // `this+176/+180/+184/+188` dans le désassemblage : 4 booléens d'état dont la
    // sémantique (quels systèmes de jeu les arment) n'a pas pu être identifiée cette
    // session (pas de xref nommée). Exposés en écriture pour qu'un futur système de
    // jeu puisse les piloter sans reprendre ce fichier ; false par défaut (icône
    // « normal/off », jamais bloquant).
    void SetStatusFlag(int index, bool active); // index 0..3
    // dword_1685E74[g_LocalElement] : compétence élémentaire en cours de préparation/
    // canalisation (icône de cast animée superposée à la 4e position, cf. doc §16).
    void SetCasting(bool casting) { casting_ = casting; }

private:
    struct TextItem { int x, y; std::string text; D3DCOLOR color; };

    // Une case effectivement dessinée dans la grille §9. Deux familles d'icônes
    // COEXISTENT dans le binaire, avec des tables d'atlas DIFFÉRENTES :
    //   - conditions §9.1-9.9/9.11-9.14 : icône FIXE de la table cat.1 (base
    //     unk_8E8B50), sélectionnée par un id catalogue (BuffIconId) ;
    //   - banque de 36 debuffs à durée §9.10 : icône de la table cat.6 (base
    //     unk_A60D04), indexée par (élément local, INDEX DE BANQUE) -- cf.
    //     GetBankIconTex (.cpp). C'est la SEULE famille qui clignote/expire.
    // `game::ActiveBuff` (Game/GameState.h) ne peut pas porter l'index de banque
    // (pas de champ, et ce header n'est pas modifiable depuis ce front), d'où ce
    // type local qui unifie les deux familles pour un cursor de position unique.
    struct GridEntry {
        int   catalogId = -1;    // BuffIconId (icône cat.1) ; -1 si entrée de banque
        int   bankIndex = -1;    // index 0..35 dans la banque (icône cat.6) ; -1 sinon
        float remaining = -1.0f; // secondes restantes (banque uniquement ; -1 sinon)
    };

    void RenderGrid();         // §9  EA 0x67BD54-0x67D9DA
    void RenderStatusPanel();  // §16 EA 0x6865BF-0x6868AB

    // Reconstruit CHAQUE FRAME (comme le binaire d'origine, qui ne stocke jamais ces
    // icônes -- il les recalcule à chaque Render) la liste effective à dessiner :
    // `self.buffs` (modèle générique pour de futures sources réseau/expiry) + les
    // conditions §9 dont une source de données est RÉELLEMENT modélisée côté
    // game::ClientRuntime (CollectWiredConditionBuffs) + la banque §9.10
    // (CollectZoneStateBuffs). L'INDEX dans le vecteur retourné EST la position de
    // grille (var_424 du binaire) : y figurer = occuper une case, même si l'icône est
    // masquée par le clignotement (cf. RenderGrid). Appelée par RenderGrid ET
    // OnMouseDown pour rester cohérentes sur le nombre d'icônes affichées/cliquables.
    std::vector<GridEntry> BuildGridEntries() const;

    // §9.10 -- banque de 36 debuffs à durée (EA 0x67D560-0x67D77F). Pousse une entrée
    // par emplacement PRÉSENT et hors de la plage réservée [19,28]. Voir le bandeau de
    // l'implémentation (.cpp) pour le layout (id/durée/horodatage) et les gardes.
    void CollectZoneStateBuffs(std::vector<GridEntry>& out) const;
    // Lit les globals `game::g_Client.VarGet(...)` déjà peuplés par les handlers réseau
    // existants (Net/GameVarDispatch.cpp, Net/CharStatDeltaDispatch.cpp) et pousse dans
    // `out` un ActiveBuff par condition §9 confirmée câblable cette mission. Voir le
    // bandeau de tête de l'implémentation (.cpp) pour la liste exhaustive câblée vs.
    // écartée (avec justification par adresse).
    void CollectWiredConditionBuffs(std::vector<game::ActiveBuff>& out) const;

    // Rect plein teinté (texture 1x1 blanche mise à l'échelle) : repli « pastille
    // colorée » quand une icône .IMG n'est pas résolue (même principe que
    // WarehouseWindow::Render, qui retombe sur `ctx.FillRect` coloré pour ses
    // cellules — ici sans UiContext, d'où la primitive locale).
    void DrawFilledRect(int x, int y, int w, int h, D3DCOLOR color);
    void DrawBorder(int x, int y, int w, int h, int thickness, D3DCOLOR color);

    // Icône de la grille (id catalogue -> texture, cache par id) ; nullptr si hors
    // table connue ou si le chargement échoue (=> pastille dans DrawGridIcon).
    gfx::GpuTexture* GetGridIconTex(int buffId);
    // Icône du panneau bas-droite / cadre (fileNo direct -> texture, cache séparé
    // pour ne pas collisionner avec les ids de la grille qui partagent la plage
    // [0, kBuffKnownIconCount)).
    gfx::GpuTexture* GetPanelIconTex(int fileNo);
    // Icône d'un emplacement de la banque §9.10 : table cat.6 indexée par
    // (élément local, index de banque) -- RÉSOLUE en fichier .IMG, voir .cpp.
    // Cache dédié (bankIconCache_) : la clé est un couple, elle ne peut pas
    // partager l'espace d'ids de gridIconCache_/panelIconCache_.
    gfx::GpuTexture* GetBankIconTex(int element, int bankIndex);

    // Dessine une case de la grille : icône réelle si résolue, sinon pastille
    // colorée dérivée de l'id (PillColorForId, .cpp).
    void DrawGridIcon(int buffId, int x, int y, int size);
    // Dessine une case du modèle unifié (catalogue OU banque), avec le même repli
    // « pastille » que DrawGridIcon quand l'icône n'est pas résolue.
    void DrawEntryIcon(const GridEntry& e, int x, int y, int size);

    IDirect3DDevice9*  device_ = nullptr;
    gfx::Font*          font_  = nullptr; // partagée, non possédée
    gfx::SpriteBatch     sprite_;
    IDirect3DTexture9*    white_ = nullptr; // 1x1 blanc teintable (pastilles/cadres)
    gfx::GpuTexture        statusFrameTex_; // cadre §16 (fichier réel, D3DPOOL_MANAGED)

    std::unordered_map<int, gfx::GpuTexture> gridIconCache_;  // clé = BuffIconId
    std::unordered_map<int, gfx::GpuTexture> panelIconCache_; // clé = numéro de fichier .IMG
    std::unordered_map<int, gfx::GpuTexture> bankIconCache_;  // clé = element*100 + bankIndex (cf. GetBankIconTex)

    std::vector<TextItem> pendingText_; // passe texte différée (hors batch sprite)

    int  screenW_ = 0;
    int  screenH_ = 0;

    bool statusFlags_[4] = { false, false, false, false }; // this+176..+188
    bool casting_        = false;                          // dword_1685E74[elem]

    // --- Géométrie grille (§9, EA 0x67BD54 : position `(220+28*(j%7), 28*(j/7)+5)`) ---
    static constexpr int kGridX      = 220;
    static constexpr int kGridY      = 5;
    static constexpr int kGridCols   = 7;
    static constexpr int kIconPitch  = 28;
    static constexpr int kIconSize   = 24;
    // Borne défensive (l'original n'a pas de plafond dur, mais un pare-fou évite un
    // vecteur de buffs démesuré de saturer l'écran) : 12 lignes = 84 icônes.
    static constexpr int kGridMaxIcons = kGridCols * 12;

    // --- Géométrie panneau bas-droite (§16, cadre `unk_94041C` ancré
    // `(nWidth-largeur, nHeight-hauteur)`) ---
    static constexpr int kStatusFrameFile   = 2424; // unk_94041C
    static constexpr int kStatusFallbackW   = 150;
    static constexpr int kStatusFallbackH   = 40;
    static constexpr int kStatusIconSize    = 24;
    struct StatusIconOffset { int dx, dy; };
    static constexpr StatusIconOffset kStatusOffsets[4] = {
        { 2, 6 }, { 59, 6 }, { 87, 6 }, { 115, 6 },
    };
};

} // namespace ts2::ui

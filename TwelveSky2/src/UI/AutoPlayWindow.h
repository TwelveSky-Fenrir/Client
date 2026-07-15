// UI/AutoPlayWindow.h — panneau « AutoPlay » (farming automatique) (ts2::ui).
//
// Contrepartie UI de Game/AutoPlaySystem.h, EXPLICITEMENT différée par ce module
// logique (cf. AutoPlaySystem.h §PÉRIMÈTRE : « PAS le rendu des panneaux UI
// autoplay »). Cette fenêtre se contente d'AFFICHER/PILOTER un
// ts2::game::AutoPlaySystem déjà construit ailleurs (une instance par joueur
// local, cf. AutoPlaySystem.h — pas de singleton imposé) : elle ne réimplémente
// AUCUNE logique de ciblage, elle lit `Targets()`/`CurrentTargetIndex()` et
// appelle `ResetTargetList()`.
//
// ONGLET RÉGLAGES (showSettings_) : contrepartie fidèle du panneau de réglages
// AutoPlay du binaire d'origine, désassemblé en complément de la mission (cluster
// AutoPlay_DrawSettingsPanel 0x4593C0 / AutoPlay_OnClickSettings 0x45A0D0, DISTINCT
// du cluster AutoPlay_* 0x457EA0..0x45D080 couvert par Game/AutoPlaySystem.h). Expose
// les 8 champs RÉELS de AutoPlayConfig avec leurs VRAIES plages, confirmées en lisant
// les hit-tests/écritures de ces deux fonctions (adresses globales g_AutoHuntMode
// 0x16755F4, g_AutoHuntSkillSingle 0x16755F8, g_AutoHuntSkillAoE 0x1675600,
// g_AutoHuntAoEThreshold 0x1675608, g_AutoHuntPkFactionMask 0x167560C,
// g_AutoHuntBagFullReturn 0x1675610, g_AutoHuntUseReturnScroll 0x1675618,
// g_AutoHuntUseTownItem 0x167561C) :
//   - mode              : bascule 2 états (0/1) — le panneau d'origine ne permet de
//                          cliquer QUE sur 0 ou 1 (AutoPlay_OnClickSettings force
//                          g_AutoHuntMode=1 ou =0 selon le sprite cliqué), bien que le
//                          champ 3e valeur (2) existe ailleurs dans le binaire (non
//                          réglable depuis ce panneau) — cf. AutoPlaySystem.h.
//   - aoeThreshold      : boutons -/+ bornés à [1,5] (boucle `for i=1;i<6` de
//                          AutoPlay_OnClickSettings — PAS [1,15] comme la taille de la
//                          liste de cibles).
//   - pkFactionMask     : 4 cases à cocher indépendantes (bits 1/2/4/8 = factions 1..4,
//                          XOR au clic dans AutoPlay_OnClickSettings).
//   - warpOnStuck       : case à cocher « retour ville si sac plein » (vrai nom IDB
//                          g_AutoHuntBagFullReturn, cf. commentaire AutoPlaySystem.h).
//   - useReturnScroll / useTownItem : cases à cocher, bornes [0,1] triviales.
//   - skillSingle / skillAoE : dans l'original, assignés par glisser-déposer d'une icône
//                          de compétence (AutoPlay_DrawItemSlots 0x459140, hors périmètre
//                          pixel-exact ici) ; ce portage expose un champ numérique borné
//                          [0,350] (0 = non configuré), la plage RÉELLE des skillId
//                          confirmée par Game/SkillSystem.h (SkillLevelTable, skillId 1..350).
// Toute modification appelle Net_SendOp99(&g_AutoPlayMgr, g_InvDirtyEnable) côté binaire
// (confirmé fin de AutoPlay_OnClickSettings) : la synchro serveur existe réellement mais
// son point de branchement C++ (Net/SendPackets.h) reste un TODO(send) explicite ci-dessous,
// car hors du périmètre des 2 fichiers audités (OptionsWindow/AutoPlayWindow).
//
// État local propre à CETTE fenêtre (PAS modélisé dans AutoPlaySystem, qui n'a
// aucun drapeau « actif ») :
//   - enabled_ : reflète la case à cocher « AutoPlay actif ». AutoPlaySystem ne
//     sait pas s'arrêter tout seul — c'est à l'appelant (boucle de jeu) de lire
//     IsEnabled() et de conditionner l'appel à AutoPlaySystem::Update(dt)
//     dessus, exactement comme le ferait un flag g_AutoHuntMode côté binaire
//     d'origine (0x16755F4 sert de MODE, pas d'interrupteur — aucun booléen
//     « marche/arrêt » distinct n'a été identifié dans le cluster AutoPlay_*).
//   - showSettings_ : bascule Cibles/Réglages, contrepartie simplifiée des 3 onglets
//     Start/Stop/Réglages du panneau d'origine (unk_9647F8/964920/964A48 à
//     AutoPlay_OnClickSettings+0x2C..+0xD1) — Start/Stop sont déjà couverts par la
//     case « AutoPlay actif » existante, seul l'onglet Réglages manquait.
//
// Aucune donnée de nom de monstre n'est exposée par Game/GameState.h pour
// MonsterEntity (MONSTER_INFO n'a pas d'accesseur typé dans Game/GameDatabase.h,
// contrairement à ITEM_INFO::name) : chaque slot affiche donc l'index monde
// (g_World.monsters), la distance et les PV bruts (MonsterEntity::hp), PAS un
// nom inventé.
#pragma once
#include "UI/UIManager.h"
#include "Game/AutoPlaySystem.h"
#include "Game/GameState.h"

#include <cstdint>
#include <string>

namespace ts2::ui {

// AutoPlayWindow — dialogue modal, bouton de fermeture obligatoire (croix en
// haut à droite). Se recentre chaque frame (cf. Dialog::x_/y_) comme les autres
// dialogues modaux du client d'origine.
class AutoPlayWindow : public Dialog {
public:
    AutoPlayWindow() = default;
    // Constructeur pratique : branche directement le système de farming du
    // joueur local (référence NON possédée — durée de vie gérée par l'appelant).
    explicit AutoPlayWindow(game::AutoPlaySystem& system) : system_(&system) {}

    // Branche/débranche le système piloté (permet un AutoPlayWindow() par défaut
    // construit avant que le AutoPlaySystem du joueur n'existe).
    void SetSystem(game::AutoPlaySystem* system) { system_ = system; }
    game::AutoPlaySystem* System() const { return system_; }

    // État local « AutoPlay actif » — voir bandeau ci-dessus. L'appelant (boucle
    // de jeu) lit IsEnabled() pour décider d'appeler AutoPlaySystem::Update(dt).
    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override; // Échap ferme le panneau

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x = 0, y = 0, w = 0, h = 0;
        bool Contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    // Une ligne de la liste des 15 slots, dérivée de AutoPlayTargetSlot +
    // MonsterEntity (g_World.monsters[monsterIndex]) au moment du Render.
    struct RowView {
        bool     used = false;       // slot occupé (monsterIndex >= 0)
        int32_t  monsterIndex = -1;
        float    distance = 0.0f;
        bool     available = false;  // AutoPlayTargetSlot::available (non pris par un tiers)
        bool     locked = false;     // == AutoPlaySystem::CurrentTargetIndex()
        int      hp = 0;
        bool     hasHp = false;      // false si monsterIndex hors bornes de g_World.monsters
    };

    // Recalcule toute la géométrie (panneau, case à cocher, bouton vider, croix
    // de fermeture, 15 lignes) à partir des dimensions écran courantes. Appelé
    // aux DEUX phases de Render (résultat identique dans la même frame, comme
    // QuestTrackerWindow::BuildLayout) puis mémorisé pour le hit-test différé
    // (OnMouseDown/OnClick routés entre deux frames).
    void RecomputeLayout(int screenW, int screenH);
    RowView BuildRow(int slotIndex) const;

    game::AutoPlaySystem* system_ = nullptr;

    bool enabled_ = false; // case à cocher « AutoPlay actif » (état local, cf. bandeau)

    // Latches boutons (armés au clic-enfoncé, validés au relâchement dedans —
    // même pattern que MsgBoxDialog::btnPressed_).
    bool closeArmed_ = false;
    bool clearArmed_ = false;
    bool checkArmed_ = false;

    // Géométrie mémorisée du dernier Render (recentrée chaque frame).
    Rect panel_;
    Rect closeBtn_;
    Rect checkbox_;
    Rect checkboxLabel_; // zone cliquable étendue (case + libellé)
    Rect clearBtn_;
    Rect rows_[15];

    static constexpr int kPanelW    = 240;
    static constexpr int kPadX      = 10;
    static constexpr int kPadY      = 10;
    static constexpr int kTitleH    = 20;
    static constexpr int kCheckH    = 18;
    static constexpr int kRowH      = 14;
    static constexpr int kRowCount  = 15;
    static constexpr int kButtonH   = 22;
    static constexpr int kCloseSize = 16;
    static constexpr int kCheckSize = 12;

    static constexpr D3DCOLOR kColBg       = Argb(224, 32, 32, 40);    // ~0xE0202028
    static constexpr D3DCOLOR kColBorder   = Argb(255, 128, 128, 128); // ~0xFF808080
    static constexpr D3DCOLOR kColTitle    = Argb(255, 255, 221, 102); // ~0xFFFFDD66
    static constexpr D3DCOLOR kColText     = Argb(255, 255, 255, 255); // ~0xFFFFFFFF
    static constexpr D3DCOLOR kColHover    = Argb(255, 64, 96, 160);   // ~0xFF4060A0
    static constexpr D3DCOLOR kColError    = Argb(255, 255, 96, 96);   // ~0xFFFF6060
    static constexpr D3DCOLOR kColSuccess  = Argb(255, 96, 255, 96);   // ~0xFF60FF60
    static constexpr D3DCOLOR kColDim      = Argb(255, 140, 140, 140); // slot libre / hors ciblage
    static constexpr D3DCOLOR kColButtonBg = Argb(255, 60, 60, 72);
};

} // namespace ts2::ui

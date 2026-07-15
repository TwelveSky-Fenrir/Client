// UI/GameHud.h — HUD principal en jeu (cGameHud).
//
// Réécriture SQUELETTE, fidèle dans l'esprit au binaire TwelveSky2 :
//   cGameHud_InitLayout  0x62A5B0  -> Init()/InitLayout()  (ancre + rects dérivés)
//   cGameHud_Render      0x64A900  -> Render()             (~16 Ko d'origine)
//   cGameHud_OnMouseDown 0x62B080  -> OnMouseDown()         (dispatcher clic-gauche)
//   QuickSlot singleton  dword_18392C0 / UI_QuickSlot_AssignHotkey 0x5bdf00
//
// Le cGameHud d'origine est la grande fenêtre « personnage » (inventaire/équipement).
// Ici on implémente la partie TOUJOURS AFFICHÉE du HUD : barres HP/MP (lues sur
// game::g_World.self), barre de quickslots et un mini-cadre portrait. Dessin via
// gfx::SpriteBatch (rects pleins teintés) + gfx::Font (libellés).
//
// Voir Docs/TS2_CLIENT_SHELL.md §2.3 (HUD) et §4 (quickslots DIK 0x02..0x0B).
//
// ⚠️ AUDIT DE COMPLÉTUDE vs Docs/TS2_UI_GAMEHUD_RENDER.md (mission du 2026-07-14,
// idaTs2 déjà décompilé lors d'une session antérieure — ce fichier ne refait PAS de
// RE, il consomme le doc existant + l'état réel du code) : tableau complet et
// écarts documentés dans le rapport de mission (voir réponse de l'agent). Deux
// widgets déjà écrits mais JAMAIS instanciés nulle part dans ClientSource ont été
// câblés ici lors de cette passe :
//   - UI/ChatWindow.h (§13 fenêtre de chat & messages système, bas-gauche) — le
//     pipeline de données existait déjà (game::g_Client.msg, alimenté par les
//     handlers réseau) mais rien ne l'affichait.
//   - UI/ConsumableBarWindow.h (§14 quickbar, contrepartie pixel de
//     Game/ConsumableBarLogic.h) — remplace l'ancien remplissage de rects placeholder
//     de DrawQuickSlotFrames() par un rendu piloté par l'inventaire réel (compteur
//     de stock, teinte « objet manquant »), conservé en repli si l'instanciation
//     échoue. Voir bandeau de UI/GameHud.cpp pour le détail des accroches et des
//     limites (pas d'événement souris-relâché ni clavier routé depuis SceneManager
//     dans cette passe — TODO précis documenté au .cpp).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/GpuTexture.h"
#include "UI/MinimapWidget.h"
#include "UI/BuffStatusPanel.h"
#include "UI/ChatWindow.h"
#include "Game/ComboPickupTick.h" // game::QuestMarkerState/Quest_UpdateMarkerTimer (§17 callout, mission 2026-07-14)

namespace ts2::ui {

// Défini dans UI/ConsumableBarWindow.h, qui inclut LUI-MÊME UI/GameHud.h (pour
// ts2::ui::QuickSlot/kQuickSlotCount) : une inclusion directe ici créerait un cycle
// d'en-têtes. Forward-declare + std::unique_ptr, implémentation complète dans
// GameHud.cpp (seul traducteur qui a besoin du type complet). Voir bandeau de
// tête ci-dessus et de GameHud.cpp.
class ConsumableBarWindow;

// Nombre de quickslots de la barre principale : touches 1..0 = scancodes DIK
// 0x02..0x0B (Docs/TS2_CLIENT_SHELL.md §4). Les slots étendus Q/W/E/R (DIK
// 0x10..0x13) existent dans l'original mais ne sont pas dessinés ici.
inline constexpr int kQuickSlotCount = 10;

// Type de contenu lié à un quickslot (UI_QuickSlot_AssignHotkey 0x5bdf00).
enum class QuickSlotType : uint8_t {
    Empty = 0,
    Item  = 1,   // objet consommable/autoplay
    Skill = 2,   // compétence
};

// Un quickslot : ce qui lui est assigné (aucune donnée de rendu ici).
struct QuickSlot {
    QuickSlotType type  = QuickSlotType::Empty;
    uint32_t      refId = 0;   // itemId ou skillId lié
    bool empty() const { return type == QuickSlotType::Empty; }
};

// Rectangle écran simple (les rects d'origine sont des quadruplets d'int).
struct HudRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// -----------------------------------------------------------------------------
// GameHud — HUD toujours affiché (barres vitales + quickslots + mini-cadre).
// Possède son propre SpriteBatch et sa propre Font (aucune instance partagée
// n'existe encore dans App). À rendre chaque frame quand la scène == InGame.
class GameHud {
public:
    // Constructeur ET destructeur déclarés ici, DÉFINIS dans GameHud.cpp (pas
    // `= default` inline) : quickBarWindow_ ci-dessous est un
    // std::unique_ptr<ConsumableBarWindow> sur un type SEULEMENT forward-déclaré
    // dans ce header (cf. commentaire de tête ci-dessus, cycle d'inclusion avec
    // UI/ConsumableBarWindow.h). Même un constructeur par défaut trivial a besoin
    // du type complet ici (le ménage d'exception implicite du constructeur généré
    // doit savoir détruire quickBarWindow_ si un membre suivant lève) : le laisser
    // `= default` dans CHAQUE traducteur qui construit un GameHud (ex.
    // Scene/SceneManager.cpp, qui n'inclut pas ConsumableBarWindow.h) donne la
    // même erreur C2338 que pour le destructeur. Un destructeur inline ici
    // instant­cierait ~unique_ptr<ConsumableBarWindow>() avec un type incomplet dans
    // CHAQUE traducteur qui inclut GameHud.h (C2338 « can't delete an incomplete
    // type ») ; seul GameHud.cpp inclut le type complet.
    GameHud();
    ~GameHud();
    GameHud(const GameHud&)            = delete;
    GameHud& operator=(const GameHud&) = delete;

    // cGameHud_InitLayout 0x62A5B0 : crée le sprite/police + la texture blanche,
    // puis pré-remplit les rects du layout à partir des dimensions écran.
    // Renvoie false si le device est nul ou si une ressource GPU échoue.
    bool Init(gfx::Renderer& renderer, int screenW, int screenH);

    // Libère sprite/police/texture (App_Shutdown / teardown UI).
    void Shutdown();

    // cGameHud_Render 0x64A900 : dessine cadre + barres HP/MP (game::g_World.self)
    // + barre de quickslots. Ne fait rien si masqué ou non initialisé.
    void Render();

    // cGameHud_OnMouseDown 0x62B080 : hit-test des quickslots (et du cadre).
    // Renvoie true si l'événement est consommé (« premier consommateur gagne »).
    bool OnMouseDown(int x, int y);

    // Autour d'un Reset() de device D3D9.
    void OnDeviceLost();
    void OnDeviceReset();

    // État visible (this[175] / bVisible dans l'original).
    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const    { return visible_; }

    // Accès aux quickslots (assignation par le système de hotkeys).
    QuickSlot&       Slot(int i)       { return slots_[static_cast<size_t>(i)]; }
    const QuickSlot& Slot(int i) const { return slots_[static_cast<size_t>(i)]; }

    // Dernier slot cliqué (-1 si aucun) — point d'accroche pour l'action d'usage.
    int LastClickedSlot() const { return lastClickedSlot_; }

    // Mini-carte (§12 de Docs/TS2_UI_GAMEHUD_RENDER.md) — accès direct pour un
    // câblage externe futur (ex. touche raccourcie bascule taille, système de
    // quête -> SetQuestHighlightMonster). Câblée dans Init/Render/OnMouseDown
    // ci-dessous ; voir UI/MinimapWidget.h.
    MinimapWidget&       Minimap()       { return minimap_; }
    const MinimapWidget& Minimap() const { return minimap_; }

    // Grille de buffs/debuffs (§9) + panneau de statut bas-droite (§16) — accès
    // direct pour un câblage externe futur (ex. système de buffs poussant dans
    // game::PlayerEntity::buffs, hooks SetStatusFlag/SetCasting). Câblée dans
    // Init/Render/OnMouseDown/OnDeviceLost/OnDeviceReset ci-dessous ; voir
    // UI/BuffStatusPanel.h et Docs/TS2_UI_GAMEHUD_RENDER.md §9/§16.
    BuffStatusPanel&       Buffs()       { return buffPanel_; }
    const BuffStatusPanel& Buffs() const { return buffPanel_; }

    // Fenêtre de chat & messages système (§13) — câblée mission 2026-07-14 (voir
    // bandeau de tête). Accès direct exposé pour un futur câblage externe :
    //   - Chat().Bind(netClient) : requiert que SceneManager possède/expose un
    //     net::NetClient& à passer ici (aucune instance accessible depuis GameHud
    //     aujourd'hui) — TODO précis, cf. bandeau .cpp.
    //   - Chat().OnKey(vk) / Chat().OnChar(c) : requiert que
    //     SceneManager::OnKeyDown/OnChar routent vers hud_ en scène InGame (ces
    //     deux méthodes ne routent aujourd'hui QUE vers `login_`, scène Login) —
    //     TODO précis, cf. bandeau .cpp. Sans ce câblage, la fenêtre de chat
    //     AFFICHE les messages entrants mais ne peut pas recevoir de saisie clavier.
    ChatWindow&       Chat()       { return chatWindow_; }
    const ChatWindow& Chat() const { return chatWindow_; }

    // Cadres alliance/groupe (§8, EA 0x67B891-0x67BD54, cf. Docs/TS2_UI_GAMEHUD_RENDER.md
    // §8) — câblés mission 2026-07-14 (voir bandeau de tête de GameHud.cpp). PAS d'accès
    // externe nécessaire à ce jour (source de données = game::g_World.allianceRoster +
    // game::g_World.players, déjà globales) : pas de méthode publique dédiée.

private:
    // Rects calculés une fois par Init (recalculés si les dims changent).
    struct Layout {
        HudRect frame;     // cadre vitales (fond translucide)
        HudRect portrait;  // mini-cadre portrait
        HudRect hpBar;     // barre de vie
        HudRect mpBar;     // barre de mana
        HudRect quickBar;  // fond de la barre de quickslots
        std::array<HudRect, kQuickSlotCount> slots{}; // cases individuelles
        HudRect questMarker; // §17 callout marqueur de quête (Quest_DrawTracker 0x510FC0)
    };

    void InitLayout();

    // Primitives de dessin (à appeler entre sprite_.Begin()/End()).
    void DrawFilledRect(const HudRect& r, D3DCOLOR color);
    void DrawBorder(const HudRect& r, int thickness, D3DCOLOR color);
    void DrawBarFill(const HudRect& r, int cur, int max,
                     D3DCOLOR bg, D3DCOLOR fill);

    // Sous-passes de rendu.
    void DrawVitalsFrame();     // cadre + portrait + remplissage des 2 barres
    void DrawQuickSlotFrames(); // cases de la barre de quickslots
    void DrawTextPass(int hp, int maxHp, int mp, int maxMp, int level, int currency);

    // §17 overlay debug temps réservé aux GM (EA 0x686942, dans UI_GameHud_Render,
    // condition binaire `dword_1676108 > 0 && g_GmAuthLevel > 0` @0x6868e8-0x6868f8).
    // Lot de police AUTONOME (BeginBatch/EndBatch propres, comme buffPanel_/chatWindow_
    // ci-dessous) : no-op silencieux (aucun coût de rendu) si l'une des deux conditions
    // est fausse. Voir GameHud.cpp pour le détail des 5 globals sources.
    void DrawDebugTimeOverlay();

    // §17 callout de marqueur de quête — Quest_DrawTracker 0x510FC0, appelé par
    // UI_GameHud_Render juste après le panneau bas-droite (EA 0x6868AB, cf. Docs/
    // TS2_UI_GAMEHUD_RENDER.md §17). DISTINCT de UI/QuestTrackerWindow.h (panneau
    // permanent haut-droite, câblé via UI/GameWindows.h) : cette pastille ne
    // s'affiche que le temps où `questMarker_.active` est vrai (armé par
    // game::Quest_UpdateMarkerTimer sur détection d'un nouvel objectif / objectif
    // rempli, retombe après ~30s ou fermeture de l'entrepôt cible, cf.
    // Game/ComboPickupTick.h). Voir GameHud.cpp pour le détail du câblage (état
    // tiqué localement, cf. bandeau — Scene/SceneManager.cpp n'est PAS modifié).
    void DrawQuestMarkerPanel(); // passe sprites (pastille + cadre)
    void DrawQuestMarkerText();  // passe police AUTONOME (comme DrawDebugTimeOverlay)

    // --- Cadres alliance/groupe (§8, mission 2026-07-14) ---------------------
    // Une ligne résolue = un slot non vide de game::g_World.allianceRoster.memberNames
    // (0..4, EA 0x67B891 : `Crt_Strcmp(g_AllianceRosterNames, &String) != 0`) recoupé par
    // NOM avec game::g_World.players[] (même méthode que la plaque de cible §7 : recherche
    // par nom dans le tableau d'entités, PAS par index de roster — les deux tableaux sont
    // indépendants, cf. Game/GameState.h::PartyRoster pour la même mise en garde sur le
    // groupe). Voir GameHud.cpp pour le détail des limites (pas de maxHp/maxMp modélisé
    // pour une entité distante -> jauge grisée "sans donnée" plutôt qu'un ratio inventé).
    struct AllianceFrameRow {
        std::string name;
        bool resolved   = false; // entité trouvée par nom dans game::g_World.players
        int  hp = 0, hpMax = 0; bool hpMaxKnown = false;
        int  mp = 0, mpMax = 0; bool mpMaxKnown = false;
    };
    std::vector<AllianceFrameRow> BuildAllianceFrames() const;
    void DrawAllianceFramePanels(const std::vector<AllianceFrameRow>& rows);
    void DrawAllianceFrameText(const std::vector<AllianceFrameRow>& rows);
    // Hit-test grossier (zone rectangulaire couvrant les lignes actuellement peuplées) —
    // même politique que layout_.frame/quickBar dans OnMouseDown (clic consommé, bloque
    // le passage à la scène 3D derrière le HUD), pas de sous-hit-test par ligne (pas
    // d'action associée, panneau d'information pure comme UI/PartyWindow.cpp).
    bool AllianceFramesContains(int x, int y) const;

    IDirect3DDevice9*  device_ = nullptr;
    // Pointeur NON POSSÉDÉ vers le gfx::Renderer passé à Init() — mémorisé pour peupler
    // UiContext::renderer dans Render() (cf. bandeau .cpp, audit 2026-07-14). AUDIT :
    // aucun consommateur actuel de ce ctx local (quickBarWindow_ -> ConsumableBarWindow::
    // Render) ne déréférence ctx.renderer aujourd'hui (il n'utilise que ctx.FillRect/
    // DrawFrame/Text/MeasureText, qui reposent sur ctx.sprites/whiteTex/font) — donc PAS
    // le même bug silencieux que LoginScene (aucun rendu réel n'était supprimé). Peuplé
    // quand même par cohérence avec UIManager::Init (ctx_.renderer = renderer) et pour
    // ne pas piéger une future extension de ConsumableBarWindow qui chargerait de vraies
    // icônes d'objet via le pattern PanelSkin (EnchantWindow/WarehouseWindow/
    // SkillTreeWindow/VendorShopWindow l'utilisent déjà tous via ctx.renderer).
    gfx::Renderer*     rendererPtr_ = nullptr;
    gfx::SpriteBatch   sprite_;
    gfx::Font          font_;
    IDirect3DTexture9* white_  = nullptr; // 1x1 blanc, teinté pour les rects pleins

    // Vrai sprite de fond du cadre vitales (Sprite2D_Draw &unk_8EC114 @0x67A43D,
    // identifié par IDA comme l'entrée #93 du tableau Sprite2D partagé unk_8E8B50 ->
    // G03_GDATA/D01_GIMAGE2D/001/001_00094.IMG, cf. commentaire de tête de
    // GameHud.cpp et GameHud::Init). D3DPOOL_MANAGED (GpuTexture) : survit au
    // device reset, pas de traitement particulier dans OnDeviceLost/Reset.
    gfx::GpuTexture    vitalsFrameTex_;

    int  screenW_ = 0;
    int  screenH_ = 0;
    bool visible_ = true;
    int  lastClickedSlot_ = -1;

    Layout layout_;
    std::array<QuickSlot, kQuickSlotCount> slots_{};

    // Mini-carte (§12) — widget autonome, dessine à travers sprite_/font_/white_
    // ci-dessus (aucune ressource GPU propre). Voir UI/MinimapWidget.h/.cpp.
    MinimapWidget minimap_;

    // Grille de buffs (§9) + panneau de statut bas-droite (§16) — widget AUTONOME
    // (son propre SpriteBatch/cache de textures, cf. UI/BuffStatusPanel.h), à la
    // différence de la mini-carte ci-dessus qui réutilise sprite_/font_/white_ de
    // GameHud. Nécessite son propre Init(renderer, &font_) : BuffStatusPanel.cpp.
    BuffStatusPanel buffPanel_;

    // Fenêtre de chat (§13) — widget léger (ChatWindow.h n'inclut ni <windows.h>
    // ni <d3d9.h>, pas de cycle avec ce header), dessine à travers son PROPRE
    // ID3DXSprite interne paresseux (pas sprite_/white_ ci-dessus) mais partage
    // font_ (paramètre de Render, pas de ressource propre). Câblée mission
    // 2026-07-14, voir bandeau de tête et GameHud.cpp.
    ChatWindow chatWindow_;

    // §17 callout de marqueur de quête (Quest_DrawTracker 0x510FC0) — état PROPRE à
    // GameHud, tiqué à chaque Render() via game::Quest_UpdateMarkerTimer (Game/
    // ComboPickupTick.h, déjà porté fidèlement). NE PARTAGE PAS l'instance
    // `s_questMarker` locale à la lambda de Scene/SceneManager.cpp (celle-ci reste
    // la source « logique » du jeu — timer 600s d'apparition, son de notification —
    // mais elle est `static` à portée de fonction dans un fichier volontairement non
    // modifié par cette mission, donc invisible d'ici). Les deux instances
    // convergent vers le MÊME état (active/markerVariant) car elles lisent les
    // mêmes entrées déterministes (game::g_QuestProgress + game::g_World.gameTimeSec,
    // isArenaZone=false ici comme là-bas) ; seul écart assumé : la branche
    // "objectif rempli->rien à faire" consomme un tirage Rng_Next() supplémentaire
    // une fois toutes les 600s (net::DefaultRng(), cf. bandeau Net/Rng.h — le
    // serveur ne valide pas ces nonces, donc sans impact protocole ; l'unique effet
    // visible est un choix de variante de pastille éventuellement différent de la
    // copie de SceneManager, cosmétique uniquement).
    game::QuestMarkerState questMarker_;

    // Barre de quickslots réelle (§14, contrepartie pixel de
    // Game/ConsumableBarLogic.h) — remplace le rendu placeholder de
    // DrawQuickSlotFrames() ci-dessus (conservée en repli). Pointeur car
    // ConsumableBarWindow.h ne peut pas être inclus ici (cycle, cf. forward
    // declaration en tête de fichier) ; alloué dans Init(), jamais nul après un
    // Init() réussi. slots_ ci-dessous reste la source de vérité qu'il consomme
    // (aucune donnée dupliquée).
    std::unique_ptr<ConsumableBarWindow> quickBarWindow_;
};

} // namespace ts2::ui

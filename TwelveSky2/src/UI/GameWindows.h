// UI/GameWindows.h — intégration : possède et enregistre les fenêtres de jeu
// (Entrepôt/Guilde/Quête/Compétences/Options/Social/AutoPlay/Marchand/Groupe/
// Échange/Personnage/Inventaire/Dialogue PNJ) dans le UIManager, et route les
// raccourcis clavier.
//
// Écrit lors de l'intégration (pas par un agent de génération) : colle les 14
// classes ts2::ui::*Window (déjà écrites par le workflow ts2-ui-giga-wave/2) sur
// le framework UIManager (déjà écrit, Docs/TS2_CLIENT_SHELL.md §2.2) et sur les
// systèmes Game/* qui les alimentent.
//
// InventoryWindow (sac 8x8 + équipement, cf. UI/InventoryWindow.h) N'HÉRITE PAS
// de Dialog : c'est une classe autonome (Init(renderer,font) propre, Render()
// sans arguments, son propre SpriteBatch) écrite indépendamment du framework
// UIManager. `InventoryDialogAdapter` ci-dessous la fait entrer dans le registre
// UIManager (routage souris « premier consommateur gagne » + rendu en ordre
// inverse, comme les 13 autres) SANS modifier son interface publique.
//
// NpcDialogWindow (cf. UI/NpcDialogWindow.h) était écrite mais N'ÉTAIT ENREGISTRÉE
// NULLE PART (aucune instance, aucun UIManager::Register, aucun appel Open() dans
// tout le dépôt) avant l'audit « chaîne device PanelSkin » (2026-07-14) : sa chaîne
// de rendu était donc cassée au tout premier maillon (Render() jamais invoquée,
// même avec un device valide). Corrigé ici en la possédant/enregistrant EXACTEMENT
// comme les 10 autres fenêtres PanelSkin, pour qu'elle reçoive ctx.renderer valide
// dès qu'un futur appelant invoquera npcDialog_.Open(...). Le site d'appel réel
// (clic sur un PNJ -> Npc_Interact 0x53A660 -> ouverture de CETTE fenêtre, cf.
// bandeau UI/NpcDialogWindow.cpp::HandleAcceptClick) vit côté routage souris 3D,
// hors périmètre de cette mission (Scene/SceneManager.cpp, volontairement non
// modifié) : reste un TODO d'intégration explicite, pas une invention de logique.
#pragma once
#include "UI/UIManager.h"
#include "UI/WarehouseWindow.h"
#include "UI/GuildWindow.h"
#include "UI/QuestTrackerWindow.h"
#include "UI/NpcDialogWindow.h"
#include "UI/SkillTreeWindow.h"
#include "UI/OptionsWindow.h"
#include "UI/SocialWindow.h"
#include "UI/AutoPlayWindow.h"
#include "UI/VendorShopWindow.h"
#include "UI/PartyWindow.h"
#include "UI/PlayerTradeWindow.h"
#include "UI/CharacterStatsWindow.h"
#include "UI/EnchantWindow.h"
#include "UI/InventoryWindow.h"

#include "Game/AutoPlaySystem.h"
#include "Game/SkillSystem.h"
#include "Game/QuestSystem.h"   // game::g_QuestProgress (synchronisé chaque frame)

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/IconTextureCache.h"

namespace ts2::ui {

// Raccourcis clavier (VK Win32) des fenêtres pilotées manuellement. Les fenêtres
// « toujours visibles » (Groupe, Suivi de quête) s'ouvrent une fois à Init() et se
// masquent elles-mêmes (logique interne à leur Render()) — pas de touche dédiée.
namespace hotkeys {
inline constexpr int kInventory      = 'I'; // « Inventaire » (sac 8x8 + équipement) — libre, vérifié contre
                                             // Scene/SceneManager.cpp (C/K/G/O/N/H/V/T/Y/E déjà pris)
inline constexpr int kCharacterStats = 'C';
inline constexpr int kSkillTree      = 'K';
inline constexpr int kGuild          = 'G';
inline constexpr int kOptions        = 'O';
inline constexpr int kSocial         = 'N'; // « réseau social » (F est pris par le déplacement DIK)
inline constexpr int kWarehouse      = 'H'; // « Hangar » (ouverture normale = paquet serveur 0x22)
inline constexpr int kVendor         = 'V'; // (ouverture normale = paquet serveur 0x87)
inline constexpr int kPlayerTrade    = 'T'; // (ouverture normale = paquet serveur 0x31)
inline constexpr int kAutoPlay       = 'Y';
inline constexpr int kEnchant        = 'E';
} // namespace hotkeys

// InventoryDialogAdapter — pont Dialog -> InventoryWindow (cf. commentaire de tête
// de fichier). Pas d'OnKey : InventoryWindow n'a pas de raccourci interne (type
// Échap) ; l'ouverture/fermeture passe par GameWindows::HandleHotkey (kInventory),
// comme les 12 autres fenêtres.
class InventoryDialogAdapter : public Dialog {
public:
    explicit InventoryDialogAdapter(InventoryWindow& w) : win_(w) {}

    void Open()  override { win_.Open();  Dialog::Open();  }
    void Close() override { win_.Close(); Dialog::Close(); }

    bool OnMouseDown(int x, int y) override { return win_.OnMouseDown(x, y); } // cGameHud_OnMouseDown 0x62B080
    bool OnClick(int x, int y)     override { return win_.OnMouseUp(x, y); }   // cGameHud_OnMouseUp   0x62DFA0

    void Render(const UiContext& ctx, int cursorX, int cursorY) override {
        // InventoryWindow::Render() dessine panneaux ET texte en un seul appel
        // (SpriteBatch + Font internes séparés du lot partagé UIManager) — on ne
        // le déclenche que sur la sous-passe Panels pour éviter un double-rendu
        // (UIManager::Render() appelle Dialog::Render deux fois par frame : une
        // fois en phase Panels, une fois en phase Text).
        if (ctx.phase != UiPhase::Panels) return;
        win_.SetCursorPos(cursorX, cursorY);
        win_.Render();
    }

private:
    InventoryWindow& win_;
};

// GameWindows — possède les 14 fenêtres + leur police/sprite dédiés, les
// enregistre dans UIManager (routage/rendu), et convertit les touches ci-dessus
// en Open()/Close(). N'est utile qu'en scène InGame (créé/détruit avec le HUD).
class GameWindows {
public:
    GameWindows();

    // Crée sprite/police dédiés + initialise UIManager (device du renderer) et
    // enregistre toutes les fenêtres. `notifyHwnd` = fenêtre pour ScreenToClient.
    bool Init(gfx::Renderer& renderer, void* notifyHwnd, int screenW, int screenH);
    void Shutdown();

    void OnDeviceLost();
    void OnDeviceReset();
    void SetScreenSize(int w, int h);

    // Rendu (délègue à UIManager::Instance().Render(), qui itère les dialogues
    // enregistrés dans l'ordre inverse — popups au-dessus).
    void Render();

    // cSceneMgr_Update 0x517BF0 (case 6) appelle AutoPlay_Update(g_AutoPlayBot)
    // JUSTE APRÈS Scene_InGameUpdate à CHAQUE frame InGame — confirmé par
    // décompilation directe (Game/InGameTickFlow.h). À appeler depuis
    // SceneManager::Update, case InGame, après le tick du monde.
    void UpdateAutoPlay(float dt) { autoPlaySystem_.Update(dt); }

    // Route un raccourci clavier vers Open()/Close() de la fenêtre concernée.
    // Renvoie true si consommé (à appeler AVANT UIManager::RouteKey côté appelant,
    // pour que les touches globales ne fuient pas vers un dialogue ouvert).
    bool HandleHotkey(int vk);

    // Accès direct (pour brancher les paquets serveur : ouverture contextuelle
    // de l'Entrepôt/Marchand/Échange sur réception d'un paquet réseau — TODO
    // d'intégration ultérieure, ces fenêtres restent accessibles au clavier
    // entre-temps).
    WarehouseWindow&       Warehouse()    { return warehouse_; }
    GuildWindow&            Guild()        { return guild_; }
    QuestTrackerWindow&     QuestTracker() { return questTracker_; }
    SkillTreeWindow&        SkillTree()    { return skillTree_; }
    OptionsWindow&           Options()      { return options_; }
    SocialWindow&            Social()       { return social_; }
    AutoPlayWindow&          AutoPlay()     { return autoPlayWindow_; }
    game::AutoPlaySystem&    AutoPlaySys()  { return autoPlaySystem_; }
    VendorShopWindow&        Vendor()       { return vendor_; }
    PartyWindow&             Party()        { return party_; }
    PlayerTradeWindow&       PlayerTrade()  { return trade_; }
    CharacterStatsWindow&    CharStats()    { return charStats_; }
    EnchantWindow&           Enchant()      { return enchant_; }
    InventoryWindow&         Inventory()    { return inventory_; }
    // Cf. bandeau de tête : enregistrée dans UIManager (chaîne device saine) mais
    // aucun appelant ne déclenche encore Open(npc, questCtx, questProgress, interaction)
    // dans ce portage — accessible ici pour le futur branchement clic-PNJ.
    NpcDialogWindow&         NpcDialog()    { return npcDialog_; }

private:
    gfx::SpriteBatch sprite_;
    gfx::Font        font_;
    bool             inited_ = false;

    // Cache GPU d'icônes PARTAGÉ entre Inventaire/Entrepôt/Enchantement/Marchand (mission
    // « audit mémoire des caches de texture », 2026-07-14, cf. Gfx/IconTextureCache.h) :
    // évite qu'une même icône .IMG soit décodée/uploadée en VRAM une fois par fenêtre qui
    // l'affiche. Injecté dans les 4 fenêtres via SetIconCache() à Init() ci-dessous.
    gfx::IconTextureCache sharedIconCache_;

    // --- Systèmes Game/* possédés ici (pas de home naturel ailleurs) ---
    game::AutoPlaySystem  autoPlaySystem_;
    game::SkillBar        skillBar_;
    // Table de bornes de niveau par compétence : PAS de membre local ici — liée
    // directement à game::GetSkillLevelTable() (Game/SkillCombat.h) au Bind(),
    // qui est la transcription fidèle et complète (350 cas) de
    // Motion_InitFrameTable 0x4F1380, désormais disponible (2026-07-14). Un
    // membre local vide était utilisé auparavant faute d'avoir identifié cette
    // source ; corrigé pour ne plus afficher de niveaux requis à zéro dans
    // SkillTreeWindow.

    // --- Fenêtres ---
    WarehouseWindow       warehouse_;
    GuildWindow            guild_;
    QuestTrackerWindow     questTracker_;
    SkillTreeWindow        skillTree_;
    OptionsWindow           options_;
    SocialWindow            social_;
    AutoPlayWindow           autoPlayWindow_{autoPlaySystem_};
    VendorShopWindow         vendor_;
    PartyWindow               party_;
    PlayerTradeWindow          trade_;
    CharacterStatsWindow        charStats_;
    EnchantWindow                 enchant_;
    InventoryWindow                 inventory_;
    InventoryDialogAdapter            inventoryAdapter_{inventory_};
    NpcDialogWindow                     npcDialog_;
};

} // namespace ts2::ui

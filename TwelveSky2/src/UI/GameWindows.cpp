// UI/GameWindows.cpp — implémentation de l'intégration des fenêtres de jeu.
#include "UI/GameWindows.h"
#include "Game/GameState.h"
#include "Core/Log.h"
#include <windows.h>

namespace ts2::ui {

GameWindows::GameWindows() = default;

bool GameWindows::Init(gfx::Renderer& renderer, void* notifyHwnd, int screenW, int screenH) {
    IDirect3DDevice9* dev = renderer.Device();
    if (!dev) { TS2_WARN("GameWindows::Init : device nul."); return false; }

    if (!sprite_.Create(dev))
        TS2_WARN("GameWindows : SpriteBatch::Create a echoue.");
    if (!font_.Init(dev, screenW, screenH))
        TS2_WARN("GameWindows : Font::Init a echoue.");

    if (!UIManager::Instance().Init(&renderer, &sprite_, &font_,
                                    static_cast<HWND>(notifyHwnd), screenW, screenH)) {
        TS2_ERR("GameWindows : UIManager::Init a echoue.");
        return false;
    }

    // InventoryWindow gère son propre device D3D9 (SpriteBatch dédié, cf.
    // commentaire de tête de UI/GameWindows.h) : Init() séparé de UIManager::Init()
    // ci-dessus, non fatal en cas d'echec (la fenêtre reste juste invisible/non
    // fonctionnelle, comme les autres echecs Create/Init de cette fonction).
    if (!inventory_.Init(renderer, &font_))
        TS2_WARN("GameWindows : InventoryWindow::Init a echoue.");

    // Cache d'icônes PARTAGÉ (cf. bandeau de tête GameWindows.h + Gfx/IconTextureCache.h) :
    // une seule instance injectée dans les 4 fenêtres qui affichent des icônes d'objet,
    // pour ne charger/uploader chaque icône .IMG qu'UNE seule fois en VRAM par session.
    inventory_.SetIconCache(&sharedIconCache_);
    warehouse_.SetIconCache(&sharedIconCache_);
    enchant_.SetIconCache(&sharedIconCache_);
    vendor_.SetIconCache(&sharedIconCache_);

    // Enregistrement (ordre = priorité de routage ; les popups modaux les plus
    // "au-dessus" doivent être en tête). MsgBox (interne à UIManager) est déjà
    // en tête. On place les popups contextuels avant les panneaux de fond.
    auto& mgr = UIManager::Instance();
    mgr.Register(&inventoryAdapter_);
    mgr.Register(&charStats_);
    mgr.Register(&skillTree_);
    mgr.Register(&guild_);
    mgr.Register(&options_);
    mgr.Register(&social_);
    mgr.Register(&warehouse_);
    mgr.Register(&vendor_);
    mgr.Register(&trade_);
    mgr.Register(&enchant_);
    mgr.Register(&autoPlayWindow_);
    // Panneaux "toujours visibles" (auto-masqués par leur propre logique interne)
    // en fin de liste = rendus en premier (fond), routés en dernier.
    mgr.Register(&questTracker_);
    mgr.Register(&party_);
    // NpcDialogWindow (cf. bandeau GameWindows.h) : popup contextuel comme les
    // autres dialogues ci-dessus (fermée par défaut, bOpen_=false tant que rien
    // n'appelle npcDialog_.Open()) — placée avec les popups modaux, pas les
    // panneaux "toujours visibles".
    mgr.Register(&npcDialog_);

    // Lie la fenêtre de compétences aux données runtime disponibles (table SKILL_INFO
    // chargée par LoadGameDatabases, ITEM_INFO idem ; table de bornes de niveau =
    // game::GetSkillLevelTable(), transcription fidèle et complète des 350 cas de
    // Motion_InitFrameTable 0x4F1380 (Game/SkillCombat.h) — remplace l'ancienne
    // table locale vide qui faisait apparaître tous les nœuds « Verrouillé » avec
    // niveau requis 0..0).
    skillTree_.Bind(game::g_World.db.skill, game::g_World.db.item,
                    game::GetSkillLevelTable(), skillBar_, game::g_World.self);

    // Panneaux toujours visibles : ouverts une fois, se masquent eux-mêmes (Render()
    // vérifie leur propre condition d'affichage — groupe non vide / quête active).
    questTracker_.Open();
    party_.Open();

    inited_ = true;
    TS2_LOG("GameWindows initialise (%dx%d, 14 fenetres enregistrees).", screenW, screenH);
    return true;
}

void GameWindows::Shutdown() {
    if (!inited_) return;
    inventory_.Shutdown(); // device/SpriteBatch propres a InventoryWindow (cf. Init ci-dessus)
    UIManager::Instance().Shutdown();
    font_.Shutdown();
    sprite_.Destroy();
    sharedIconCache_.Clear(); // libere explicitement les textures GPU d'icones residentes
    inited_ = false;
}

void GameWindows::OnDeviceLost() {
    sprite_.OnLostDevice();
    font_.OnDeviceLost();
    inventory_.OnDeviceLost(); // ID3DXSprite propre a InventoryWindow, hors du lot partage UIManager
}

void GameWindows::OnDeviceReset() {
    sprite_.OnResetDevice();
    font_.OnDeviceReset();
    inventory_.OnDeviceReset();
}

void GameWindows::SetScreenSize(int w, int h) {
    UIManager::Instance().SetScreenSize(w, h);
    font_.SetClipRect(w, h);
    inventory_.SetScreenSize(w, h);
}

void GameWindows::Render() {
    if (!inited_) return;
    // Synchronise le snapshot de progression de quête consommé par QuestTrackerWindow
    // avec l'état réellement écrit par le réseau (game::g_QuestProgress, Game/QuestSystem.h).
    questTracker_.SetProgressState(game::g_QuestProgress);
    UIManager::Instance().Render();
}

bool GameWindows::HandleHotkey(int vk) {
    if (!inited_) return false;
    // Toggle indépendant par fenêtre (pas de CloseAll : ces panneaux ne se
    // recouvrent pas assez pour justifier l'exclusivité mutuelle de l'original).
    auto toggle = [](Dialog& d) { if (d.IsOpen()) d.Close(); else d.Open(); };
    switch (vk) {
    case hotkeys::kInventory:      toggle(inventoryAdapter_); return true;
    case hotkeys::kCharacterStats: toggle(charStats_);      return true;
    case hotkeys::kSkillTree:      toggle(skillTree_);      return true;
    case hotkeys::kGuild:          toggle(guild_);          return true;
    case hotkeys::kOptions:        toggle(options_);        return true;
    case hotkeys::kSocial:         toggle(social_);         return true;
    case hotkeys::kWarehouse:      toggle(warehouse_);      return true;
    case hotkeys::kVendor:         toggle(vendor_);         return true;
    case hotkeys::kPlayerTrade:    toggle(trade_);          return true;
    case hotkeys::kEnchant:        toggle(enchant_);        return true;
    case hotkeys::kAutoPlay:       toggle(autoPlayWindow_); return true;
    default: return false;
    }
}

} // namespace ts2::ui

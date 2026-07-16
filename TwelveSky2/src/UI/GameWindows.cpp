// UI/GameWindows.cpp — implémentation de l'intégration des fenêtres de jeu.
//
// Ordre d'inclusion : Net/ EN PREMIER (Net/NetClient.h tire <winsock2.h> avant <windows.h>,
// que UI/GameWindows.h tire transitivement via UIManager.h -> <d3d9.h>) — même convention
// que UI/PartyWindow.cpp:6-10 / UI/ClanContextMenu.cpp.
#include "Net/SendPackets.h"    // Net_SendOp45/49/55/61/67/74 (réponses aux prompts)
#include "Net/NetClient.h"      // net::GlobalNetClient()
#include "UI/GameWindows.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h" // game::g_Client.prompt (PromptState)
#include "Core/Log.h"
#include <windows.h>

namespace ts2::ui {

namespace {
// Instance courante (cf. GameWindows::Instance() dans GameWindows.h). Une seule
// GameWindows vit à la fois (créée/détruite avec la scène InGame).
GameWindows* g_GameWindowsInstance = nullptr;
} // namespace

GameWindows* GameWindows::Instance() { return g_GameWindowsInstance; }

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
    // Menu contextuel joueur (UI_ClanWin dword_1822938, gap SGP-1) : popup contextuel
    // enregistré EN TÊTE des fenêtres de GameWindows (juste après le MsgBox interne à
    // UIManager) — le binaire l'ouvre via UI_CloseAllDialogs(dword_1821D4C, 1) @0x5D8E50,
    // c.-à-d. par-dessus tout le reste. Cet enregistrement suffit à le rendre ET à le
    // router ; seul son OUVREUR manque (picking d'entité, G-PICK-05 — cf. ClanContextMenu.h).
    mgr.Register(&clanMenu_);
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
    g_GameWindowsInstance = this; // publie l'accès global (cf. GameWindows::Instance())
    TS2_LOG("GameWindows initialise (%dx%d, 15 fenetres enregistrees).", screenW, screenH);
    return true;
}

void GameWindows::Shutdown() {
    if (!inited_) return;
    if (g_GameWindowsInstance == this) g_GameWindowsInstance = nullptr;
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

// ===========================================================================
// Pont PromptState -> MsgBoxDialog — gaps SCN-01 (aucun lecteur) + SCN-02 (jamais accepté)
// ===========================================================================
namespace {

// Types de prompt qui attendent une RÉPONSE RÉSEAU, et leur builder.
// Table PROUVÉE par les 2 jumptables de UI_MsgBox_OnLButtonUp 0x5C0A90 (cases identiques
// des deux côtés) : 8 -> Op45, 9 -> Op49, 10 -> Op67, 14 -> Op74, 19 -> Op55, 20 -> Op61.
// Ouvreurs correspondants : 8 = Pkt_PartyInvitePrompt 0x48FA70, 9 = Pkt_AllyInvitePrompt
// 0x48FFB0, 19 = Dlg19 0x4906F0, 20 = Dlg20 0x490AF0, 10 = Dlg10 0x490EE0, 14 = Dlg14 0x491C10.
bool IsNetConfirmType(int type) {
    return type == 8 || type == 9 || type == 10 || type == 14 || type == 19 || type == 20;
}

// Émet la réponse au prompt `type` avec la valeur `value`.
void SendPromptReply(int type, int8_t value) {
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return; // hors session : aucune émission (le binaire ne teste pas, mais
                     // g_NetClient y est toujours vivant ; ici le pointeur peut être nul).
    switch (type) {
        case 8:  net::Net_SendOp45(*nc, value); break; // 0x5C1080 (OK) / 0x5C2DDE (Annuler)
        case 9:  net::Net_SendOp49(*nc, value); break; // 0x5C1096 / 0x5C2DF4
        case 10: net::Net_SendOp67(*nc, value); break; // 0x5C10AC / 0x5C2E0A
        case 14: net::Net_SendOp74(*nc, value); break; // 0x5C110C / 0x5C2E1D
        case 19: net::Net_SendOp55(*nc, value); break; // 0x5C11BD / 0x5C2E30
        case 20: net::Net_SendOp61(*nc, value); break; // 0x5C11D3 / 0x5C2E43
        default: break;                                // type non réseau (2 = NoticeDlg)
    }
}

} // namespace

// SCN-01 + SCN-02. Reflète game::g_Client.prompt dans le MsgBoxDialog partagé
// (UIManager::MsgBox(), enregistré EN TÊTE par UIManager::Init -> modal et routé en premier).
//
// ---------------------------------------------------------------------------
// POLARITÉ DES VALEURS — LE DOSSIER DE GAPS AVAIT L'INVERSE ; RÉFUTÉ AU DÉSASSEMBLAGE
// ---------------------------------------------------------------------------
// Le dossier (et Net/Opcodes.h:232/236, marqués à tort « CONFIRMED ») annoncent
// « 1 = accepter / 2 = refuser ». C'est FAUX. Vérité terrain (UI_MsgBox_OnLButtonUp 0x5C0A90) :
//   - latch btn1 [+0Ch] -> sprite unk_8E8FF0 @ (x+0xA5=165, y+0x5A=90) -> jpt_5C0BE5 -> push 0
//   - latch btn2 [+10h] -> sprite unk_8E91AC @ (x+0xF1=241, y+90)      -> jpt_5C2DC3 -> push 1
// btn1 = OK, prouvé : UI_NoticeDlg_OnLButtonUp 0x5C03F0 — dialogue à UN SEUL bouton (l'OK) —
// hit-teste LE MÊME sprite unk_8E8FF0 @0x5C048A, à (x+0xCB=203, y+90) = exactement le CENTRE
// des deux emplacements du MsgBox ((165+241)/2 = 203), dans le même cadre unk_8E8F5C.
// Corroboration : Net_OnConfirmPromptOpen_Dlg10 0x490EE0 contient un auto-ACCEPT (bot
// auto-hunt) qui force dword_1822444 = 1 (= latch btn1) @0x490FFA puis synthétise un clic
// @0x49101F -> il en sort Op67(0). Donc :
//   0 = ACCEPTER (clic OK) · 1 = REFUSER (clic Annuler) · 2 = refus AUTOMATIQUE (filtre off).
// => Le `Net_SendOpXX(2)` des handlers (branche filtre désactivé) est FIDÈLE : ne pas y toucher.
void GameWindows::SyncPrompt() {
    auto& p = game::g_Client.prompt;

    // Front montant : prompt actif et pas encore reflété (ou type changé).
    if (p.active && (!promptShown_ || promptType_ != p.type)) {
        const int  type   = p.type;
        const bool netAsk = IsNetConfirmType(type);
        // Mapping des DEUX lignes de texte. Le binaire dessine ligne1 @y+34 (0x5C31F5) et
        // ligne2 @y+50 (0x5C3266) ; PromptState::Open(t, body, title) est appelée par les
        // handlers avec body = ligne1 et title = ligne2 (p. ex. Net/GameHandlers_PartyGuild.cpp
        // :102 -> Open(8, "[nom]"+Str(305), Str(306))). MsgBoxDialog::Render, lui, dessine
        // title_ AU-DESSUS de body_ (UI/UIManager.cpp:186-191). On croise donc volontairement
        // (p.body -> title_, p.title -> body_) pour préserver l'ORDRE VISUEL d'origine ; les
        // noms de champs de MsgBoxDialog restent trompeurs (signalé au rapport).
        // `withCancel` : les 6 types réseau ont 2 boutons (OK/Annuler) ; les autres (type 2 =
        // UI_NoticeDlg) n'en ont qu'UN (l'OK), cf. UI_NoticeDlg_Render 0x5C0630.
        UIManager::Instance().MsgBox().Open(
            p.body, p.title,
            [type](int button) {
                if (IsNetConfirmType(type)) {
                    // kBtnOk -> 0 (accepter) ; kBtnCancel -> 1 (refuser). Cf. bandeau ci-dessus.
                    SendPromptReply(type,
                        button == MsgBoxDialog::kBtnOk ? static_cast<int8_t>(0)
                                                       : static_cast<int8_t>(1));
                }
                // TODO [ancre 0x5C04DF] : pour le type 2 (UI_NoticeDlg), l'OK d'origine
                //   exécute Net_CloseSocket(&g_NetClient) + g_SceneMgr=2 + g_SceneSubState=0
                //   (retour à la sélection de serveur). C'est le périmètre des gaps SCN-03/04 :
                //   Scene/SceneManager.* n'est PAS détenu par ce front -> on se contente de
                //   fermer le prompt (aucune fausse action, aucune invention).
                game::g_Client.prompt.Close();
                // Désarme l'état miroir IMMÉDIATEMENT (cf. OnPromptDismissed) : un nouveau
                // prompt du même type peut arriver avant le Render suivant. `this` n'est PAS
                // capturé (la lambda survit dans msgBox_.onResult_, possédé par le singleton
                // UIManager, qui peut dépasser la durée de vie de GameWindows) — on repasse
                // par Instance(), nul après Shutdown.
                if (GameWindows* gw = GameWindows::Instance()) gw->OnPromptDismissed();
            },
            /*withCancel=*/netAsk);
        promptShown_ = true;
        promptType_  = type;
        return;
    }

    // Prompt actif, déjà reflété, mais la boîte n'est plus ouverte ET notre callback n'a PAS
    // tourné (il aurait remis promptShown_ à false) : elle a donc été fermée par un TIERS —
    // UIManager::CloseAll/ResetAll, c.-à-d. UI_CloseAllDialogs 0x5AC590 / UI_ResetAllDialogs
    // 0x5AC3F0. Dans le binaire il n'existe QU'UN SEUL stockage (dword_1822440 EST à la fois
    // le flag « boîte visible » et le flag « prompt actif ») : fermer la boîte ÉTEINT donc le
    // prompt. On aligne l'état miroir sur cette sémantique plutôt que de laisser un prompt
    // orphelin (qui serait rouvert en boucle à chaque frame).
    if (p.active && promptShown_ && !UIManager::Instance().MsgBox().IsOpen()) {
        p.Close();
        promptShown_ = false;
        promptType_  = 0;
        return;
    }

    // Front descendant : le prompt a été fermé par le réseau (PromptState::CloseIf des
    // handlers 0x2f/0x35/0x3c/0x42/0x48/0x51) -> referme la boîte.
    if (!p.active && promptShown_) {
        UIManager::Instance().MsgBox().Close();
        promptShown_ = false;
        promptType_  = 0;
    }
}

void GameWindows::Render() {
    if (!inited_) return;
    // Reflète l'état de prompt réseau dans le MsgBox partagé AVANT le rendu (SCN-01/SCN-02) :
    // c'est ce qui donne enfin un LECTEUR à game::g_Client.prompt.
    SyncPrompt();
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

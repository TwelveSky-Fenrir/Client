// UI/SocialWindow.h — Fenêtre « Social » : amis/liste noire (AutoPlay) + succès de tribu.
//
// ===========================================================================
// AUDIT RÉSEAU (Passe 4, vague W6, 2026-07-16) — VERDICT : ZÉRO ÉMISSION = FIDÈLE.
// NE PAS « CORRIGER » CETTE ABSENCE DANS UNE VAGUE FUTURE.
// ===========================================================================
// Cette fenêtre n'émet AUCUN paquet, et c'est CONFORME au binaire (vérifié par
// décompilation intégrale de AutoPlay_OnMouseUpNameList 0x45B000, le vrai code
// amis/liste-noire — onglet de la fenêtre AutoPlay, sélecteur *((_WORD*)this+146) :
// 0 = amis, 1 = ennemis/liste noire) :
//   - Bouton « Ajouter » (x+74, y+303), sprite unk_9664E0, latch testé EA 0x45B5E2 :
//       GetWindowTextA(hWnd, ·, 25) vide            -> message 1949, RETURN
//       !MobDb_FindByName(mITEM, ·)                 -> message 1948, RETURN (EA 0x45B63F)
//       AutoPlay_IsNameListed(·)                    -> message 1947, RETURN (EA 0x45B679)
//       capacité >= 0x30 (48)                       -> message 1980 (EA 0x45B80F/0x45B6F1)
//       sinon List_PushBackNode + AutoPlay_SaveFriendList (EA 0x45B7EC)
//                                 / AutoPlay_SaveEnemyList  (EA 0x45B90A)
//   - Bouton « Retirer » (x+144, y+303), sprite unk_966608, EA 0x45B974 :
//       parcours de liste, List_EraseNode, puis Save*List (EA 0x45BC1C / 0x45BE2F).
// AUCUN `Net_Send*` sur ces chemins : la persistance des listes est un ÉCRITURE
// DISQUE (G02_GINFO\011.BIN / 012.BIN), PAS une synchronisation serveur. Les seuls
// Net_SendOp99 de la fonction (EA 0x45B1A5 arg=1, EA 0x45B272 arg=0) sont le
// démarrage/arrêt du BOT AutoPlay — sans aucun rapport avec le social.
// Les paquets « amis » du protocole (0x7e Net_OnFriendStatusNotice, 0x90
// Net_OnFriendListEvent, 0x79 Net_OnSocialListRemove) sont ENTRANTS UNIQUEMENT :
// aucune UI sociale émettrice n'existe dans ce client (xrefs sur unk_16869C0 /
// 0x1686AC4 / 0x1686BC8 : aucun émetteur).
//
// RÉSIDUEL CONNU (non traité par cette vague — hors mandat « émission réseau », et
// sémantique ambiguë à ne pas deviner) : la validation `MobDb_FindByName(mITEM, nom)`
// (message 1948, ancres 0x45B63F pour l'ajout et 0x45B9D8 pour le retrait) n'est PAS
// portée dans SocialWindow::TryAdd/TryRemoveSelected. Le binaire teste un nom de
// JOUEUR contre la base `mITEM` (0x8E71EC) — l'intention exacte de ce test reste à
// élucider avant tout portage. La capacité (48) et « déjà listé » sont, eux, déjà
// modélisés (Game/SocialSystem.h : SocialListOp::ListFull / AlreadyListed).
//
// AUDIT POSITIONS (RE-VÉRIFICATION PAR DÉCOMPILATION FRAÎCHE, 2026-07-14) : le
// CENTRAGE écran ci-dessous (`(screenW-kPanelW)/2, (screenH-kPanelH)/2`) suit le
// même motif EA-prouvé que UI_ClanWin_Draw 0x5DA210/UI_MemberSelectWnd_Render
// 0x667860 (`nWidth/2 - w/2`, résolution COURANTE, pas de facteur d'échelle figé) —
// AUCUN bug de coordonnées/échelle trouvé dans la formule de centrage elle-même.
// En revanche, décompilation de AutoPlay_OnMouseUpNameList 0x45B000 (le VRAI code
// d'ajout/retrait amis-liste noire) montre que cette UI N'EST PAS un dialogue
// indépendant dans le binaire : c'est un ONGLET intégré à la fenêtre AutoPlay
// (bot de farm, UI/AutoPlayWindow.h) à des offsets FIXES relatifs à l'ancre de
// CETTE fenêtre (`*((_DWORD*)this+7)`=x, `*((_DWORD*)this+6)`=y, PAS une fenêtre
// "Social" séparément centrée) : édition amis à (x+74,y+303), édition liste noire à
// (x+144,y+303), onglets à (x+26,y+103)/(x+153,y+103). SocialWindow (ce fichier) est
// donc une fenêtre à 560x400 centrée écran de manière indépendante — une
// RÉINVENTION pragmatique cohérente en interne, mais SANS position d'ancrage 1:1
// avec le binaire (qui n'a pas de fenêtre "Social" autonome). Onglet « Succès »
// (AchievementState) : aucune UI de rendu identifiée dans le binaire (seul
// Net_OnAchievementDataLoad 0x4AC920 existe côté réseau) — mêmes réserves déjà
// documentées plus bas. Correction appliquée : aucune (le centrage est déjà
// EA-fidèle) ; seule cette note d'audit est nouvelle.
//
// Dialogue modal (ts2::ui::Dialog, cf. UI/UIManager.h) branché sur Game/SocialSystem.h.
// Deux onglets cliquables en haut du panneau :
//   - « Amis »   : les DEUX listes locales prouvées par le désassemblage
//                  (AutoPlaySocialLists::friends / blacklist, EA 0x45d730-0x45e1a3),
//                  avec ajout/retrait via l'API AddFriend/AddToBlacklist/Remove*
//                  déjà écrite dans SocialSystem.h. AUCUN champ « en ligne » n'existe
//                  dans cette structure (voir l'avertissement d'honnêteté en tête de
//                  SocialSystem.h) : on ne l'invente pas, on l'indique explicitement
//                  à l'écran.
//   - « Succès » : AchievementState::flags (24 emplacements int32, dword_184C218),
//                  lu directement depuis game::g_Achievements (Game/SocialSystem.h),
//                  alimenté par Net_OnAchievementDataLoad (opcode 0x98,
//                  Net/GameHandlers_BossWorld.cpp) — un seul état partagé, plus de
//                  copie locale. Tant qu'aucune donnée n'a été reçue du serveur,
//                  tous les succès affichent « verrouillé », état honnête plutôt
//                  qu'inventé.
//
// Saisie de nom SANS EDIT Win32 ni Widgets::EditBox (le contrat Dialog n'expose que
// OnKey(int vk), pas d'OnChar) : on tape directement les touches virtuelles
// alphanumériques (VK_0..VK_9 = '0'..'9', VK_A..VK_Z = 'A'..'Z' en Win32, VK_SPACE),
// à l'image du clavier QWERTY brut — pas de minuscules ni d'accents, limitation
// assumée de cette réécriture UI (le binaire d'origine passe par un vrai EDIT natif).
#pragma once
#include "UI/UIManager.h"
#include "Game/SocialSystem.h"

#include <string>

namespace ts2::ui {

class SocialWindow : public Dialog {
public:
    SocialWindow();

    // Ouverture : recharge les listes amis/liste noire depuis disque
    // (AutoPlaySocialLists::LoadAll -> G02_GINFO\011.BIN / 012.BIN), fidèle au
    // rechargement effectué par l'AutoPlay avant affichage de sa liste de noms.
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Accès à l'état partagé (game::g_Achievements, alimenté par le handler réseau
    // opcode 0x98) — plus de copie locale, cf. commentaire de tête de fichier.
    game::AchievementState&       Achievements()       { return game::g_Achievements; }
    const game::AchievementState& Achievements() const { return game::g_Achievements; }
    game::AutoPlaySocialLists&    Social()             { return social_; }

private:
    enum class Tab { Friends = 0, Achievements = 1 };

    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool Contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    // Géométrie recalculée à chaque Render (dimensions écran variables) ET mise en
    // cache (lastScreenW_/H_) pour que OnMouseDown/OnClick — routés hors Render,
    // avec seulement (x,y) écran en argument — retrouvent le même agencement. Même
    // idiome que MsgBoxDialog::Layout dans UI/UIManager.cpp.
    struct Layout {
        Rect panel;
        Rect closeBtn;
        Rect tabFriends, tabAchievements;
        Rect friendList, blacklistList;   // corps liste sous l'en-tête (hors titre)
        Rect nameInputBox;
        Rect btnAddFriend, btnAddBlacklist, btnRemove;
        Rect achGrid;                      // zone grille succès (cellules calculées à part)
    };
    Layout ComputeLayout(int screenW, int screenH) const;

    // Rangée n (0-based) d'une liste de noms (sous l'en-tête de colonne).
    static Rect RowRect(const Rect& listArea, int index);
    // Cellule n (0-based) de la grille de succès (4 colonnes).
    static Rect AchCellRect(const Rect& gridArea, int index);

    void RenderChrome(const UiContext& ctx, const Layout& lo);
    void RenderFriendsTab(const UiContext& ctx, const Layout& lo);
    void RenderAchievementsTab(const UiContext& ctx, const Layout& lo);

    // Retourne un pointeur vers la liste (amis ou liste noire) contenant `name`,
    // nullptr si absent des deux. Sert au bouton « Retirer » générique.
    game::SocialNameList* FindListContaining(const std::string& name, bool& outIsBlacklist);

    void SetStatus(const std::string& s, D3DCOLOR c);
    void TryAdd(bool toBlacklist);
    void TryRemoveSelected();

    // --- Données ---
    game::AutoPlaySocialLists social_;       // listes amis/liste noire (chargées à Open())
    // (plus de copie locale des succès : Achievements() lit game::g_Achievements partagé)

    Tab  tab_ = Tab::Friends;

    std::string selectedName_;          // nom sélectionné (clic sur une rangée)
    bool        selectedIsBlacklist_ = false;
    bool        hasSelection_ = false;

    std::string nameInput_;             // champ de saisie « nom à ajouter »
    static constexpr size_t kNameMaxLen = 24; // 25 o/slot - 1 (SocialNameList::kSlotBytes)

    std::string statusText_;
    D3DCOLOR    statusColor_ = 0xFFC8C8C8u;

    // Latches souris (armées au press, validées au release DANS le même élément —
    // pattern btnPressed[] des dialogues d'origine, cf. UI_MsgBox_OnLButtonDown/Up).
    bool armClose_ = false, armTabFriends_ = false, armTabAch_ = false;
    bool armAddFriend_ = false, armAddBlacklist_ = false, armRemove_ = false;

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Constantes de mise en page ---
    static constexpr int kPanelW = 560;
    static constexpr int kPanelH = 400;
    static constexpr int kListW  = 250;
    static constexpr int kListH  = 200;
    static constexpr int kListHeaderH = 18;
    static constexpr int kRowH        = 16;

    // --- Palette (D3DCOLOR = 0xAARRGGBB) ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u;
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u;
    static constexpr D3DCOLOR kColFrameDim  = 0xFF505050u;
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu;
    static constexpr D3DCOLOR kColTextDim   = 0xFFAAAAAAu;
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u;
    static constexpr D3DCOLOR kColHover     = 0xFF4060A0u;
    static constexpr D3DCOLOR kColTabActive = 0xFF4060A0u;
    static constexpr D3DCOLOR kColTabIdle   = 0xFF303038u;
    static constexpr D3DCOLOR kColError     = 0xFFFF6060u;
    static constexpr D3DCOLOR kColSuccess   = 0xFF60FF60u;
    static constexpr D3DCOLOR kColRowFriend = 0xFF283828u; // fond rangée liste amis
    static constexpr D3DCOLOR kColRowBlack  = 0xFF382828u; // fond rangée liste noire
    static constexpr D3DCOLOR kColLockedBg  = 0xFF303038u;
    static constexpr D3DCOLOR kColUnlockBg  = 0xFF2E4A2Eu;
};

} // namespace ts2::ui

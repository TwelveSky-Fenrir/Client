// Game/ClientRuntime.h — état runtime CLIENT partagé (hub des handlers réseau + UI).
//
// Socle commun aux modules d'application des paquets entrants (Net/GameHandlers_*.cpp)
// et à l'UI (HUD, inventaire, chat). Réécriture C++ PROPRE des centaines de globals
// éparpillés du binaire (g_InvMain, g_Currency, g_InvWeight, boss bars, journal de
// messages, dialogues actifs…). Voir RE/net_handler_notes.md pour la sémantique des
// handlers d'origine, Docs/TS2_GAMEPLAY_LOGIC.md et Docs/TS2_CLIENT_SHELL.md.
//
// RÈGLE D'OR (parallélisation) : ce fichier est le SOCLE. Les modules de handlers
// ne l'ÉDITENT PAS — ils l'incluent et utilisent son API. Un global de la longue
// traîne (dword_XXXX) non modélisé ici se stocke fidèlement via Var(adresseOrigine)
// sans toucher ce header.
#pragma once
#include "Game/GameState.h"   // InvCell
#include <cstdint>
#include <array>
#include <deque>
#include <string>
#include <unordered_map>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Journal de messages (chat + système + flottant). Regroupe Msg_AppendSystemLine,
// Msg_AppendChatLine et HUD_ShowFloatingMessage du binaire en un tampon lisible
// par l'UI (ChatWindow / HUD). Les couleurs sont des D3DCOLOR ARGB.
// ---------------------------------------------------------------------------
enum class MsgKind : uint8_t { System = 0, Chat = 1, Whisper = 2, Faction = 3, Floating = 4 };

struct MessageLine {
    std::string text;
    uint32_t    color = 0xFFFFFFFFu;
    MsgKind     kind  = MsgKind::System;
    int         floatType = 0;   // HUD_ShowFloatingMessage : type visuel (0..2)
    std::string who;             // émetteur (chat/whisper), vide sinon
};

class MessageLog {
public:
    static constexpr size_t kMaxLines = 256;

    void System (const std::string& t, uint32_t color = 0xFFFFFFFFu);
    void Chat   (const std::string& t, uint32_t color, const char* who = nullptr);
    void Whisper(const std::string& t, const char* who);
    void Faction(const std::string& t, uint32_t color, const char* who = nullptr);
    void Floating(int floatType, int flag, const std::string& t, uint32_t color = 0xFFFFFFFFu);
    // HUD_ShowFloatingMessage(floatType, flag, text) — bannière flottante centrée.
    //
    // Animation — VÉRIFIÉ dans le binaire (2026-07-14) : AUCUNE non plus.
    // HUD_ShowFloatingMessage 0x5AEEC0 se contente d'horodater le slot
    // (this+4*type+1476 = g_GameTimeSec) et de jouer un son ; HUD_RenderFloatingMessages
    // 0x5AF4C0 dessine chaque slot actif à une position FIXE par type (tableau de
    // coords en dur, switch(i)) et à opacité pleine tant que
    // `g_GameTimeSec - horodatage <= 10.0` ; passé ce délai le slot est mis à 0 et
    // disparaît instantanément (pas de fondu de sortie). Aucun lerp/easing/rampe
    // alpha ni glissement de position sur les 10 s d'affichage : apparition et
    // disparition sont des coupures nettes (show/hide par flag + timer), pas des
    // transitions. Si ce rendu HUD est implémenté côté ClientSource, rester fidèle
    // à ce comportement (pas d'ajout de fade/slide qui n'existe pas à l'origine).

    const std::deque<MessageLine>& Lines() const { return lines_; }
    const MessageLine* Last() const { return lines_.empty() ? nullptr : &lines_.back(); }
    size_t Count() const { return lines_.size(); }
    void Clear() { lines_.clear(); }

private:
    void Push(MessageLine&& l);
    std::deque<MessageLine> lines_;
};

// ---------------------------------------------------------------------------
// Grille d'inventaire (SoA d'origine g_InvMain/g_InvGrid_* fusionnée en AoS).
// Adressage fidèle : le binaire indexe [384*row + 6*col] (6 dwords/cellule, stride
// de ligne 384 dwords -> 64 colonnes/ligne). On reproduit (row, col) -> cellule.
// ---------------------------------------------------------------------------
struct InventoryState {
    static constexpr uint32_t kCols = 64;   // 384/6
    static constexpr uint32_t kRows = 32;   // marge (pages/sacs + entrepôt de travail)

    std::array<InvCell, kCols * kRows> cells{};

    int64_t currency = 0;   // g_Currency 0x1673180 (+ miroir dword_1687254[0])
    int64_t weight   = 0;   // g_InvWeight
    // Cellule « aux » de déplacement en attente (g_InvAux + dword_1674ABC/AC0).
    uint32_t aux0 = 0, aux1 = 0, aux2 = 0;

    InvCell& At(uint32_t row, uint32_t col) {
        const uint32_t r = (row < kRows) ? row : (kRows - 1);
        const uint32_t c = (col < kCols) ? col : (kCols - 1);
        return cells[r * kCols + c];
    }
    void Set(uint32_t row, uint32_t col, uint32_t itemId, uint32_t gridX,
             uint32_t gridY, uint32_t count, uint32_t durability, uint32_t serial) {
        InvCell& e = At(row, col);
        e.itemId = itemId; e.gridX = gridX; e.gridY = gridY;
        e.flag = count; e.color = durability; e.durability = serial;
    }
    void ClearCell(uint32_t row, uint32_t col) { At(row, col) = InvCell{}; }
    void Reset() { cells.fill(InvCell{}); currency = weight = 0; aux0 = aux1 = aux2 = 0; }
};

// ---------------------------------------------------------------------------
// Barre de vie de boss (Net_OnBossHpInit / BossHpBarUpdate / BossHpPercent).
// Le serveur envoie le DOUBLE du pourcentage (le client stocke hp/2).
// ---------------------------------------------------------------------------
struct BossBar {
    bool     active   = false;
    int      percent  = 0;    // 0..100 (= valeur reçue / 2)
    uint32_t a = 0, b = 0, c = 0, d = 0;
    std::array<uint8_t, 420> panel{};  // corps de panneau (Net_OnBossPanelLoad)
};

// ---------------------------------------------------------------------------
// État de dialogue/prompt modal actif (registre dword_1822440/1822450 d'origine).
// active=false -> aucun dialogue ; type = identifiant (10/14/19/20…) du prompt.
//
// Animation d'apparition/disparition — VÉRIFIÉ dans le binaire (2026-07-14) :
// AUCUNE. UI_NoticeDlg_Open 0x5C0280 se contente de poser des flags/booléens ;
// UI_NoticeDlg_Render 0x5C0630 dessine le panneau tous les frames à une position
// FIXE (centré via nWidth/nHeight, aucun offset dépendant du temps) et à opacité
// pleine (Sprite2D_Draw sans paramètre alpha) tant que le flag actif est vrai ;
// UI_NoticeDlg_OnLButtonUp 0x5C03F0 appelle UI_NoticeDlg_Close (flag=0) et
// exécute l'action IMMÉDIATEMENT au même frame — pas de fondu de sortie, pas de
// délai. Il n'y a ni lerp, ni easing, ni rampe alpha/position dans ces trois
// fonctions : c'est un show/hide binaire par simple test de flag. Le rendu
// statique de PromptState (LoginScene::RenderNotice) est donc DÉJÀ fidèle ;
// ne pas lui ajouter de fondu/glissement qui n'existe pas dans l'original.
// ---------------------------------------------------------------------------
struct PromptState {
    bool     active = false;
    int      type   = 0;      // dword_1822450
    std::string title;
    std::string body;
    std::string name;         // nom injecté ([%s])
    void Open(int t, std::string b, std::string ttl = {}) {
        active = true; type = t; body = std::move(b); title = std::move(ttl);
    }
    void CloseIf(int t) { if (active && type == t) { active = false; type = 0; } }
    void Close() { active = false; type = 0; }
};

// ---------------------------------------------------------------------------
// ClientRuntime — hub d'état runtime (singleton g_Client). Complète game::g_World
// (entités + self + bases .IMG) par l'état d'UI/inventaire/social/boss piloté par
// les handlers de paquets.
// ---------------------------------------------------------------------------
class ClientRuntime {
public:
    InventoryState        inv;
    MessageLog            msg;
    std::array<BossBar, 2> boss{};
    PromptState           prompt;

    // Curseur de déplacement d'objet en attente (g_PendingMove_SrcRow0 / dword_1822EF0
    // + snapshot item dword_1822F08..). -1 = aucun déplacement en cours.
    int32_t  pendingMoveRow = -1;
    int32_t  pendingMoveCol = -1;
    InvCell  pendingItem{};

    // Échappatoire fidèle pour la longue traîne de globals scalaires (dword_XXXX,
    // word_XXXX, flt_XXXX) non modélisés en champ propre. Clé = adresse d'origine.
    //   Ex : g_Client.Var(0x1675E9C) = pct;  // dword_1675E9C (boss hp %)
    int32_t&  Var(uint32_t addr) { return vars_[addr]; }
    int32_t   VarGet(uint32_t addr) const {
        auto it = vars_.find(addr); return it == vars_.end() ? 0 : it->second;
    }
    float&    VarF(uint32_t addr) {
        // réinterprète le slot int comme float (mêmes 4 octets, comme le binaire).
        return *reinterpret_cast<float*>(&vars_[addr]);
    }

    // Échappatoire complémentaire pour les GROS blobs bruts de la longue traîne
    // (recopies Crt_Memcpy fidèles, structure interne non décodée/non modélisée en
    // champ propre — ex. table UI dword_1686F74, corps de classement dword_1825E70).
    // Clé = adresse d'origine ; la taille est fixée par le premier appelant et
    // vérifiée aux appels suivants (échec silencieux -> renvoie le buffer existant
    // tel quel plutôt que de le retailler, pour ne jamais désynchroniser un lecteur
    // qui aurait déjà pris une référence). Usage : g_Client.Blob(addr, n) = data.
    std::vector<uint8_t>& Blob(uint32_t addr, size_t size) {
        auto& b = blobs_[addr];
        if (b.empty()) b.assign(size, 0);
        return b;
    }

    void Reset() { inv.Reset(); msg.Clear(); boss = {}; prompt.Close();
                   pendingMoveRow = pendingMoveCol = -1; pendingItem = InvCell{};
                   vars_.clear(); blobs_.clear(); }

private:
    std::unordered_map<uint32_t, int32_t> vars_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> blobs_;
};

// Instance globale unique (miroir propre du fouillis de globals du binaire).
inline ClientRuntime g_Client;

// ---------------------------------------------------------------------------
// StrTable005_Get(id) : le client résout des chaînes localisées depuis une table
// (001.DAT / StrTable005). Faute de la table décryptée ici, on renvoie un
// placeholder STABLE « #<id> » pour que les handlers restent fidèles à la logique
// (quel message, avec quels arguments) sans dépendre des assets de langue.
std::string Str(int id);

} // namespace ts2::game

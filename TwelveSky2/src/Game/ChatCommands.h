// Game/ChatCommands.h — Détection de préfixe/canal pour la boîte de chat principale.
//
// Source de vérité : décompilation de UI_Chat_SubmitInput (EA 0x68b330), la fonction
// RÉELLE de routage des commandes de chat JOUEUR (whisper/party/guild/alliance/
// trade/faction). Voir le .cpp pour le détail des écarts avec la commande initiale
// de la mission (qui ciblait Chat_SubmitTypedMessage 0x5c3cf0 et suggérait d'élargir
// vers Chat_ParseGmCommand 0x68bfd0 si besoin).
//
// Ce module ne fait AUCUN envoi réseau : ParseChatInput() se contente de reproduire
// fidèlement la détection de préfixe/canal et l'extraction texte/cible ; c'est à
// l'appelant de mapper ChatCommand::kind vers le bon builder Net_SendOpNN de
// Net/SendPackets.h (déjà écrit, non modifié ici) et d'appliquer les gardes de
// gameplay (zone d'arène, restrictions de morph, etc. — voir TODO dans le .cpp).
#pragma once

#include <string>

namespace ts2::game {

// Canal de discussion résultant du parsing d'une ligne de chat brute.
// Correspond 1:1 aux branches de UI_Chat_SubmitInput (0x68b330) et aux opcodes
// sortants qu'elles appellent (Net/SendPackets.h, déjà écrit) :
//   Whisper  -> Net_SendOp39  (0x4b75d0) — 13o nom + 61o message
//   Party    -> Net_SendOp38  (0x4b7450) — 61o message (« groupe » dans Docs/TS2_CLIENT_SHELL.md §2.5)
//   Alliance -> Net_SendOp68  (0x4ba100) — 61o message
//   Guild    -> Net_SendOp77  (0x4bae60) — 61o message
//   Trade    -> Net_SendOp81  (0x4bb470) — 61o message (« commerce » dans la doc)
//   Faction  -> Net_SendOp40  (0x4b7760) — 61o message
// À ne pas confondre avec le canal "dire" simple (Op80, Net_SendChatNormal_Op80 /
// Net_SendOp80 0x4bb2f0), qui est un chemin totalement séparé — voir Chat_SubmitTypedMessage
// (0x5c3cf0) et le commentaire en tête du .cpp.
enum class ChatCommandKind {
    None,      // Ligne vide, ou préfixe spécial suivi d'un corps vide (rien à envoyer,
               // cf. tests `if (v36[0])` avant tout traitement : 0x68b54e/0x68b677/0x68b7a0/0x68b8ff)
    Whisper,
    Party,
    Alliance,
    Guild,
    Trade,
    Faction,
};

// Onglet de canal actif dans la boîte de chat quand la ligne tapée ne commence par
// AUCUN préfixe spécial. Reflète le `switch (*(this + 161))` de UI_Chat_SubmitInput
// (0x68bb10), valeurs numériques identiques à celles observées dans le désassemblage
// et documentées dans Docs/TS2_CLIENT_SHELL.md §2.5 (champ de mode de canal de
// g_ChatManager, +0x284/644 déc.). ParseChatInput() ne lit pas cet état lui-même
// (il vit dans l'objet UI d'origine, hors périmètre de ce module) : l'appelant le
// fournit explicitement.
enum class ChatChannelMode : int {
    Whisper  = 0,
    Party    = 1,
    Alliance = 2,
    Guild    = 3,
    Trade    = 4,
    Faction  = 5,
};

// Résultat du parsing d'une ligne de chat brute.
struct ChatCommand {
    ChatCommandKind kind = ChatCommandKind::None;

    // Nom du destinataire. UNIQUEMENT pertinent pour kind == Whisper, et NE PROVIENT
    // PAS du texte tapé : dans le client d'origine, la cible du chuchotement est un
    // champ séparé (this+162, 13 octets) fixé par UI_Chat_SetWhisperMode (0x68b260,
    // ex. clic droit sur un joueur), distinct de la ligne éditée. ParseChatInput()
    // ne peut donc pas la reconstruire depuis `raw` seul : ce champ reste vide et
    // c'est à l'appelant de le renseigner à partir de son propre état de chuchotement
    // avant d'appeler Net_SendOp39.
    std::string target;

    // Texte du message, préfixe de canal retiré le cas échéant.
    std::string message;
};

// Reproduit fidèlement la détection de préfixe/canal de la PARTIE JOUEUR de
// UI_Chat_SubmitInput (0x68b330) : premier caractère de préfixe spécial
// ('!'=Guild, '#'=Faction, '@'=Trade, '~'=Alliance), sinon `currentChannelMode`
// (onglet actif, cf. ChatChannelMode ci-dessus). N'effectue ni envoi réseau ni
// filtre anti-injures (maybe_Dict001_MatchWord 0x4c1410, données 001.DAT non
// disponibles statiquement) ni garde de gameplay (arène/morph — voir TODO dans le
// .cpp) : tout cela reste à la charge de l'appelant.
//
// raw               : contenu brut de l'EDIT de chat (équivalent du buffer `String`
//                      rempli par GetWindowTextA en 0x68b368).
// currentChannelMode : onglet actif si `raw` ne commence par aucun préfixe spécial.
ChatCommand ParseChatInput(const std::string& raw, ChatChannelMode currentChannelMode);

} // namespace ts2::game

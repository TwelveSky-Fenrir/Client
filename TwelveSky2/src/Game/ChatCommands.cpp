// Game/ChatCommands.cpp — voir Game/ChatCommands.h pour le contrat du module.
//
// ============================================================================
// ÉCART ASSUMÉ PAR RAPPORT À LA CONSIGNE INITIALE — à lire avant tout le reste
// ============================================================================
// La mission demandait de décompiler Chat_SubmitTypedMessage (EA 0x5c3cf0) pour en
// déduire les préfixes joueur (whisper/party/guild...), et suggérait d'élargir vers
// Chat_ParseGmCommand (0x68bfd0) si des préfixes joueur légitimes s'y trouvaient
// mêlés aux commandes GM.
//
// Décompilation de 0x5c3cf0 (Chat_SubmitTypedMessage) :
//   GetWindowTextA -> filtre mots bannis (maybe_Dict001_MatchWord 0x4c1410) ->
//   Net_SendOp80(&unk_846C08, String) (0x5c3d87).
// C'est effectivement un quasi-trampoline confirmé par la décompilation : AUCUNE
// détection de préfixe, aucun routage de canal. Il envoie tout le texte tapé tel
// quel sur l'opcode 80 (chat « dire » simple). C'est la boîte de saisie utilisée
// par g_hEditChatInput (0x1669000), distincte de la boîte principale.
//
// Décompilation de 0x68bfd0 (Chat_ParseGmCommand) : 36 commandes distinctes,
// TOUTES des commandes GM/triche (/movezone, /hide, /show, /exp, /money, /item,
// /moncall, /die, /max, /tribe, /equip, /unequip, /find, /call, /move, /nchat,
// /ychat, /kick, /block, /tribebank, /pvppoint, /level, /message, /editstr,
// /editdex, /editcon, /editint, /level2, /useitem, /delitem, /monkill,
// /movezonepos, /movepos, /pvpkill, /319Battle, /notice — /item et /nchat/
// /ychat/kick/block ont chacune 2-3 variantes selon le nombre de tokens espacés
// dans la ligne, mais restent la même commande nommée). AUCUN préfixe joueur
// légitime (whisper/party/guild) n'y est mélangé — confirmé exhaustivement par
// la décompilation complète (tokenizer : jusqu'à 5 tokens espacés de 100o max
// chacun, 0x68c043-0x68c129).
// Conformément à la consigne, cette logique reste hors périmètre et n'est PAS
// implémentée ici.
//
// Les VRAIS préfixes/canaux joueur (whisper/party/guild/alliance/trade/faction)
// vivent ailleurs : dans UI_Chat_SubmitInput (EA 0x68b330), la fonction appelée
// par la boîte de chat PRINCIPALE (g_hEditChatMain 0x1668FD4), qui tente d'abord
// Chat_ParseGmCommand (uniquement si g_GmAuthLevel > 0, donc jamais pour un joueur
// normal) puis, si ce n'était pas une commande GM, route selon :
//   - le premier caractère de la ligne s'il vaut '!', '#', '@' ou '~' ;
//   - sinon l'onglet de canal actuellement sélectionné dans la boîte de chat.
// Contrairement à l'hypothèse de la consigne ("/w ", "/p ", "/g "), TS2 N'UTILISE
// PAS de préfixes multi-caractères à la "/lettre " pour les canaux joueur : ce sont
// des SYMBOLES D'UN SEUL CARACTÈRE, collés au texte (pas d'espace après). Les seuls
// préfixes textuels "/xxx" du jeu sont les commandes GM de Chat_ParseGmCommand.
// C'est donc UI_Chat_SubmitInput, et non Chat_SubmitTypedMessage, qui est la
// fonction pertinente pour cette mission ; ce fichier reproduit sa partie JOUEUR.
// Recoupement : Docs/TS2_CLIENT_SHELL.md §2.5 (déjà documenté par une session RE
// antérieure) confirme exactement le même mapping préfixe/canal -> opcode que la
// décompilation ci-dessous.
// ============================================================================

#include "Game/ChatCommands.h"

namespace ts2::game {

namespace {

// Construit une commande à partir du texte suivant un préfixe spécial. Reproduit
// le test `if (v36[0])` présent devant chaque branche '!'/'#'/'@'/'~' dans
// UI_Chat_SubmitInput (0x68b54e, 0x68b677, 0x68b7a0, 0x68b8ff) : si rien ne suit
// le caractère de préfixe, le client n'envoie RIEN (pas d'erreur, pas de paquet).
ChatCommand MakePrefixed(ChatCommandKind kind, const std::string& rest) {
    ChatCommand cmd;
    if (rest.empty()) {
        return cmd; // kind reste None -> l'appelant ne doit rien envoyer
    }
    cmd.kind = kind;
    cmd.message = rest;
    return cmd;
}

} // namespace

ChatCommand ParseChatInput(const std::string& raw, ChatChannelMode currentChannelMode) {
    ChatCommand cmd;

    // GetWindowTextA renvoie 0 sur une boîte vide -> le client d'origine sort sans
    // rien faire (0x68b370/0x68bf2a). Même comportement ici : kind reste None.
    if (raw.empty()) {
        return cmd;
    }

    // NOTE (hors périmètre, non reproduit ici) : avant le switch de préfixe, le
    // client compare le texte ENTIER à 3 chaînes localisées via StrTable005_Get
    // (indices 738/739/740, chargées depuis 005.DAT — non disponibles statiquement)
    // pour déclencher un raccourci d'ouverture d'entrepôt (Net_SendVaultReq_234(1|2|3),
    // EA 0x68b3c5-0x68b4b0). Ce n'est pas une commande de chat au sens de cette
    // mission (whisper/party/guild) : non implémenté, laissé en TODO pour un module
    // ultérieur s'il faut un jour la reproduire.

    // switch(String[0]) — EA 0x68b531/0x68b53e. Le caractère de préfixe n'est PAS
    // suivi d'un espace dans le binaire d'origine (contrairement à l'hypothèse
    // "/w " de la consigne) : le reste du buffer (à partir de l'index 1) est
    // utilisé tel quel comme corps du message.
    const char first = raw[0];
    const std::string rest = raw.substr(1);

    // Les QUATRE préfixes partagent en outre une garde commune non reproduite ici :
    // Map_IsArenaZone() (0x54b690) bloque toujours le canal (message StrTable005[1352],
    // LABEL_86 unique à 0x68be5a) ; '#' et '@' ajoutent en plus g_SelfMorphNpcId == 291
    // au même test (0x68b945/0x68b7e5). Voir aussi le TODO générique en tête de fichier.
    switch (first) {
        case '!':
            // Guild : Net_SendOp77, EA 0x68b733. Gardes non reproduites ici : arène
            // (Map_IsArenaZone, 0x68b683) puis Crt_Strcmp(unk_16746A8, raw) (0x68b6bc)
            // -> message d'erreur StrTable005[371] si égal (rôle exact de unk_16746A8
            // non confirmé statiquement — TODO).
            return MakePrefixed(ChatCommandKind::Guild, rest);
        case '#':
            // Faction : Net_SendOp40, EA 0x68ba50. Gardes non reproduites : arène ou
            // g_SelfMorphNpcId==291 (0x68b945) ; réservé aux joueurs dont
            // g_SelfMorphNpcId ∈ {37,119,124,170,50,52} (0x68b9a4-0x68b9d6), avec une
            // seconde restriction par g_LocalElement (0x68ba02-0x68ba3d).
            return MakePrefixed(ChatCommandKind::Faction, rest);
        case '@':
            // Trade : Net_SendOp81, EA 0x68b894. Gardes non reproduites : arène ou
            // g_SelfMorphNpcId==291 (0x68b7e5) ; bloqué si g_SelfMorphNpcId ==
            // table{138,139,165,166}[g_LocalElement] (0x68b846-0x68b881).
            return MakePrefixed(ChatCommandKind::Trade, rest);
        case '~':
            // Alliance : Net_SendOp68, EA 0x68b60a. Gardes non reproduites : arène
            // (0x68b55a) puis Crt_Strcmp(g_AllianceRosterNames, raw) (0x68b593) ->
            // StrTable005[355] si égal.
            return MakePrefixed(ChatCommandKind::Alliance, rest);
        default:
            break;
    }

    // Pas de préfixe spécial -> route selon l'onglet de canal actif
    // (switch(*(this + 161)), EA 0x68bb10). Le texte est envoyé intégralement
    // (pas de retrait de caractère : les builders sont appelés avec &String en entier,
    // ex. 0x68bba9, 0x68bc22).
    cmd.message = raw;
    switch (currentChannelMode) {
        case ChatChannelMode::Whisper:
            // case 0, EA 0x68bb1c-0x68bba9 : Net_SendOp39. Gardes non reproduites :
            // arène (Map_IsArenaZone, 0x68bb1c) puis Crt_Stricmp(cible, byte_1673184)
            // (0x68bb5d) -> StrTable005[303] si la cible est soi-même. La cible
            // elle-même (this+162) n'est PAS dans `raw` : voir ChatCommand::target
            // dans le .h. L'appelant doit la renseigner.
            cmd.kind = ChatCommandKind::Whisper;
            break;
        case ChatChannelMode::Party:
            // case 1, EA 0x68bb10-0x68bc22 : Net_SendOp38. Pas de garde d'arène ici
            // (seul canal joueur non bloqué en zone arène) : bloqué uniquement si
            // g_SelfMorphNpcId == table{138,139,165,166}[g_LocalElement]
            // (0x68bbb3-0x68bbed, StrTable005[2040] si bloqué).
            cmd.kind = ChatCommandKind::Party;
            break;
        case ChatChannelMode::Alliance:
            // case 2, EA 0x68bc31-0x68bca9 : Net_SendOp68. Gardes non reproduites :
            // arène (0x68bc31) puis même garde sentinelle g_AllianceRosterNames que
            // le préfixe '~' (0x68bc74).
            cmd.kind = ChatCommandKind::Alliance;
            break;
        case ChatChannelMode::Guild:
            // case 3, EA 0x68bcb8-0x68bd30 : Net_SendOp77. Gardes non reproduites :
            // arène (0x68bcb8) puis même garde sentinelle unk_16746A8 que le préfixe
            // '!' (0x68bcfb).
            cmd.kind = ChatCommandKind::Guild;
            break;
        case ChatChannelMode::Trade:
            // case 4, EA 0x68bd78-0x68be10 : Net_SendOp81. Même garde que le préfixe
            // '@' : arène ou g_SelfMorphNpcId==291 (0x68bd78), puis
            // table{138,139,165,166}[g_LocalElement] (0x68bda1-0x68bddb, StrTable005[2041]).
            cmd.kind = ChatCommandKind::Trade;
            break;
        case ChatChannelMode::Faction:
            // case 5, EA 0x68be58-0x68bf25 : Net_SendOp40. Même garde que le préfixe
            // '#' : arène ou g_SelfMorphNpcId==291 (0x68be58), puis liste blanche de
            // morphs + table par g_LocalElement.
            cmd.kind = ChatCommandKind::Faction;
            break;
    }

    return cmd;
}

} // namespace ts2::game

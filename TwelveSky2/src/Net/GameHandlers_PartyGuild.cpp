// Net/GameHandlers_PartyGuild.cpp — routage des paquets GROUPE / GUILDE / ALLIANCE / ÉQUIPE.
//
// Domaine « party_guild » (RE/handler_domains.json) : invitations (groupe/alliance),
// roster (membres, ajout/retrait/kick), chat de guilde/faction, valeurs/positions/HP
// des membres. Sémantique d'origine : RE/net_handler_notes.md.
//   0x2e PartyInvitePrompt   0x2f PartyInviteDecline  0x30 PartyJoinResult
//   0x34 AllyInvitePrompt    0x35 AllyInviteDecline   0x36 AllyJoinResult
//   0x37 GuildMemberInfo     0x38 GuildInfoUpdate     0x3d PartyResultDialog
//   0x3e PartyMemberNameSet  0x3f PartyMemberValueSet  0x40 PartyMemberClear
//   0x4a GuildRosterReset    0x4b GuildMemberJoin     0x4c GuildChatMessage
//   0x4d GuildMemberLeave    0x4e GuildMemberKick     0x4f GuildRosterUpdate
//   0x53 TeamFormationDispatch 0x54 GuildNoticeChat   0x56 TeamSlotAssign
//   0x5c GuildActionResult   0x5d PartyInviteResult   0x7b PartyMemberTargetSet
//   0x7f PartyMemberHpSet    0x80 PartyMemberUpdate   0x81 PartyItemResult
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // net::g_GmCmdCooldownLatch
#include "Net/SendPackets.h"
#include "Config/GameOptions.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"    // game::g_World (résolution d'entité par identité réseau)
#include "Game/GuildSystem.h"  // game::g_Guild (Guild_SelectNextMember)
#include "Game/StatEngine.h"   // game::StatEngine::CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0)
#include "Game/MapWarp.h"      // game::BeginWarpToFactionTown (résolution warp, pas l'envoi)
#include "Game/MotionPoolsCoordResolver.h" // game::g_CoordResolver (coordonnées réelles 003.BIN)
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

namespace {

// Couleurs de canal de chat (placeholders fidèles pour g_ChatColor_Guild 0x84DFE8 /
// g_ChatColor_Faction 0x84DFEC du binaire ; valeurs D3DCOLOR exactes à réextraire).
// Vérifié par decompilation+xrefs idaTs2 (2026-07-14, cf. GameHandlers_ChatSocial.cpp) :
// ces globals sont à 0 dans .data et n'ont aucun site d'écriture dans le binaire —
// valeur réelle posée hors binaire statique (config/skin runtime), pas un oubli.
constexpr uint32_t kChatColorGuild   = 0xFF66FF66u; // g_ChatColor_Guild
constexpr uint32_t kChatColorFaction = 0xFFFFCC33u; // g_ChatColor_Faction

// Lit un champ char[N] (13 o) terminé NUL en std::string propre.
template <size_t N>
inline std::string Name(const char (&s)[N]) {
    return std::string(s, ::strnlen(s, N));
}

// Résout l'index d'une entité joueur par identité réseau (idHi,idLo) SANS l'ajouter
// (le handler d'origine ne fait qu'un scan linéaire : dword_1687238/168723C[227*e]).
// index 0 = self. Retourne -1 si absent.
int FindPlayerIndex(uint32_t hi, uint32_t lo) {
    auto& players = ts2::game::g_World.players;
    for (size_t i = 0; i < players.size(); ++i)
        if (players[i].active && players[i].id.hi == hi && players[i].id.lo == lo)
            return static_cast<int>(i);
    return -1;
}

// Bloc de remise à zéro d'état de guilde partagé À L'IDENTIQUE par Net_OnTeamFormationDispatch
// (0x491E70) case 4 (0x492442-0x4924F3) et case 6 (0x492668-0x492719) — mêmes écritures, mêmes
// ordres, seul le message final diffère (478 vs 470). Factorisé ici car le binaire duplique
// littéralement la séquence (ce n'est pas un refactor : les deux sites sont identiques).
// Les 4 Crt_StringInit de tête (0x492442/0x49245E/0x492470/0x49248C) visent des buffers de
// CHAÎNE non modélisés (0x16746A8/0x16746BC/0x168725C/0x1687270) -> TODO, pas de Var(int32).
void ResetGuildStateBlock() {
    auto& c = ts2::game::g_Client;
    c.Var(0x16746B8) = 0;   // 0x49244A / 0x492670
    c.Var(0x168726C) = 0;   // 0x492478 / 0x49269E
    c.Var(0x1687450) = 0;   // 0x492494 / 0x4926BA
    c.Var(0x168744C) = 0;   // 0x49249E / 0x4926C4
    c.Var(0x1675664) = 0;   // 0x4924A8 / 0x4926CE — dword_1675664[0] (index 0, PAS [slot])
    c.Var(0x1675660) = 0;   // 0x4924B2 / 0x4926D8
    c.Var(0x1675668) = 0;   // 0x4924BC / 0x4926E2
    c.Var(0x167566C) = 0;   // 0x4924C6 / 0x4926EC
    // Char_CalcAttackRatingMin/Max(g_EquipSnapshotScratch) : g_EquipSnapshotScratch (0x8E719C)
    // = snapshot du self -> façade StatEngine sur g_World.self/db (précédent établi et
    // documenté : Net/CharStatDeltaDispatch.cpp:144, Net/GameHandlers_Misc.cpp:79).
    c.Var(0x168736C) = ts2::game::StatEngine::CalcAttackRatingMin(   // 0x4924DA / 0x492700
        ts2::game::g_World.self, ts2::game::g_World.db);
    c.Var(0x1687374) = ts2::game::StatEngine::CalcAttackRatingMax(   // 0x4924E9 / 0x49270F
        ts2::game::g_World.self, ts2::game::g_World.db);
    // TODO(ui) [0x4924F3 / 0x492719] UI_RemoveActiveBuffSlot (0x55D5B0) — UI non détenue ici.
}

// Ferme la notice modale (registre dword_18225D0/18225D8 d'origine, distinct du
// msgbox g_Client.prompt = dword_1822440/1822450). Modélisée via Var pour fidélité.
void CloseNotice(int type) {
    auto& c = ts2::game::g_Client;
    if (c.VarGet(0x18225D0) && c.VarGet(0x18225D8) == type)
        c.Var(0x18225D0) = 0;
}

} // namespace

void RegisterPartyGuildHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, g_World, Str()

    // 0x2e PartyInvitePrompt — invitation de groupe : ouvre la boîte oui/non (type 8) si le
    // filtre est actif ; sinon refus automatique (Net_SendOp45(2), fidèle à Pkt_PartyInvitePrompt).
    OnPacket<PartyInvitePrompt>(sys, 0x2e, [&sys](const PartyInvitePrompt& p) {
        const std::string nm = Name(p.inviterName);
        if (config::g_Options.FilterPartyInvite) {
            g_Client.prompt.Open(8, "[" + nm + "]" + Str(p.flag == 1 ? 305 : 426), Str(306));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp45(sys.Client(), 2);
            g_Client.msg.System(Str(304));
        }
    });

    // 0x2f PartyInviteDecline — invitation de groupe refusée : ferme le prompt 8 + str307.
    OnTrigger(sys, 0x2f, []() {
        g_Client.prompt.CloseIf(8);
        g_Client.msg.System(Str(307));
    });

    // 0x30 PartyJoinResult — résultat d'adhésion au groupe : ferme la notice 4, str308..313.
    // La jumptable jpt_48FC18 (Pkt_PartyJoinResult 0x48FBD0) couvre EXACTEMENT les cas 0..5
    // (case 0 = str 0x134 = 308 à 0x48FC25) et son `default` retourne sans rien afficher :
    // un code > 5 doit donc être MUET, pas afficher un Str() arbitraire du module voisin.
    OnPacket<PartyJoinResult>(sys, 0x30, [&sys](const PartyJoinResult& p) {
        CloseNotice(4);
        if (p.resultCode <= 5) // 0x48FC18 (jumptable 6 cas) ; default -> aucun message
            g_Client.msg.System(Str(308 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0)  // 0x48FC44 : adhésion confirmée -> demande d'infos de groupe (opcode 0x2E, sans payload).
            Net_SendOp46(sys.Client());
    });

    // 0x34 AllyInvitePrompt — invitation d'alliance : mémorise l'invitant + prompt (type 9)
    // si le filtre est actif ; sinon refus automatique (Net_SendOp49(2)).
    OnPacket<AllyInvitePrompt>(sys, 0x34, [&sys](const AllyInvitePrompt& p) {
        const std::string nm = Name(p.name);
        if (config::g_Options.FilterAllyInvite) {
            g_Client.Var(0x1822838) = static_cast<int32_t>(p.inviterId); // dword_1822838 = inviterId
            g_Client.prompt.Open(9, "[" + nm + "]" + Str(325), Str(326));
            g_Client.prompt.name = nm;
        } else {
            Net_SendOp49(sys.Client(), 2);
            g_Client.msg.System(Str(324));
        }
    });

    // 0x35 AllyInviteDecline — invitation d'alliance refusée : ferme le prompt 9 + str327.
    OnTrigger(sys, 0x35, []() {
        g_Client.prompt.CloseIf(9);
        g_Client.msg.System(Str(327));
    });

    // 0x36 AllyJoinResult — résultat d'adhésion alliance : ferme la notice 5, str328..333/2237.
    // La jumptable jpt_490138 (Pkt_AllyJoinResult 0x4900F0) couvre EXACTEMENT les cas 0..6 :
    // str 328..333 pour 0..5 (case 0 = 0x148 = 328 à 0x490145) et str 2237 (0x8BD à 0x490252)
    // pour le SEUL cas 6 ; le `default` est muet. L'ancienne forme `code <= 5 ? ... : Str(2237)`
    // affichait 2237 pour TOUT code >= 6, et — `code` étant un int issu d'un uint32_t —
    // resultCode=0xFFFFFFFF donnait code=-1, passait `code <= 5` et lisait Str(327).
    // Switch explicite sur la valeur NON SIGNÉE, calqué sur la jumptable.
    OnPacket<AllyJoinResult>(sys, 0x36, [&sys](const AllyJoinResult& p) {
        CloseNotice(5);
        switch (p.resultCode) { // 0x490138 (jumptable 7 cas)
            case 0: case 1: case 2: case 3: case 4: case 5:
                g_Client.msg.System(Str(328 + static_cast<int>(p.resultCode)));
                break;
            case 6: g_Client.msg.System(Str(2237)); break; // 0x49024B (0x8BD)
            default: break;                                // muet, fidèle au default
        }
        if (p.resultCode == 0) {
            // 0x49015F-0x490169 : `push offset unk_182296C` (src) / `push offset unk_1822828`
            // (dest) / `call Crt_StringInit` = COPIE 0x182296C -> 0x1822828, et NON une remise
            // à vide (le pseudocode affiche « Crt_StringInit() » sans argument, d'où l'erreur
            // de lecture précédente : `Blob(0x1822828,1).assign(1,0)`).
            // NON REPRODUIT DÉLIBÉRÉMENT : 0x1822828 est une globale WRITE-ONLY dans tout le
            // binaire — xrefs_to = 2, toutes deux en ÉCRITURE (0x49002B Pkt_AllyInvitePrompt,
            // 0x490164 ici), AUCUN lecteur. Modéliser la copie n'aurait aucun effet observable
            // et 0x182296C n'est pas modélisé côté C++ ; on documente plutôt que d'inventer.
            g_Client.Var(0x1822838) = g_Client.VarGet(0x1822984) + g_Client.VarGet(0x1822980) +
                                       g_Client.VarGet(0x182297C); // 0x490171-0x490183
            Net_SendOp50(sys.Client());  // demande du roster d'alliance (opcode 0x32, sans payload).
        }
    });

    // 0x37 GuildMemberInfo — bloc d'infos/liste de membres de guilde : str334.
    OnPacket<GuildMemberInfo>(sys, 0x37, [](const GuildMemberInfo&) {
        g_Client.msg.System(Str(334));
        // TODO(ui): UI_ItemListWin_Open(field0, blockA[128], blockB[96], field228)
        //   — décoder noms/stats des membres.
    });

    // 0x38 GuildInfoUpdate — maj info + roster guilde : header/footer + 8 membres {id,val1,val2}.
    OnPacket<GuildInfoUpdate>(sys, 0x38, [](const GuildInfoUpdate& p) {
        g_Client.Var(0x1822848) = static_cast<int32_t>(p.header); // dword_1822848
        g_Client.Var(0x1822934) = static_cast<int32_t>(p.footer); // dword_1822934
        for (int i = 0; i < 8; ++i) {                             // 8 membres x 12 o
            uint32_t id = 0, v1 = 0, v2 = 0;
            std::memcpy(&id, p.members + 12 * i + 0, 4);
            std::memcpy(&v1, p.members + 12 * i + 4, 4);
            std::memcpy(&v2, p.members + 12 * i + 8, 4);
            g_Client.Var(0x18228CC + 12 * i) = static_cast<int32_t>(id); // dword_18228CC[3*i]
            g_Client.Var(0x18228D0 + 12 * i) = static_cast<int32_t>(v1); // dword_18228D0[3*i]
            g_Client.Var(0x18228D4 + 12 * i) = static_cast<int32_t>(v2); // dword_18228D4[3*i]
        }
        // Crt_Memcpy(dword_182284C, &v5, 0x80u) — 0x490417 (Pkt_GuildInfoUpdate 0x490360).
        // Ce bloc de 128 o n'est PAS un « nom/notice de guilde » (lecture erronée d'origine,
        // cf. commentaire à corriger RecvPackets.h:1111) : c'est un STAGING structuré, prouvé
        // par son unique consommateur. xrefs_to(0x182284C) = 2 exactement : l'écriture ici
        // (0x490412) et la lecture 0x4905D2 dans Net_OnFactionBoardSync (0x490560, opcode
        // 0x3a), qui le relit champ par champ (0x4905D8-0x490620) via dword_182284C[4*i],
        // dword_1822850[4*i], dword_1822854[4*i], dword_1822858[4*i] — soit 8 enregistrements
        // de 4 dwords, stride 16 o = exactement les 128 octets. Même motif que la boucle des
        // 8 membres ci-dessus, qui stage déjà 0x18228CC/D0/D4 pour ce même consommateur.
        for (int i = 0; i < 8; ++i) { // 8 x {4 dwords}, stride 16 o
            uint32_t a = 0, b = 0, c = 0, d = 0;
            std::memcpy(&a, p.block128 + 16 * i + 0,  4);
            std::memcpy(&b, p.block128 + 16 * i + 4,  4);
            std::memcpy(&c, p.block128 + 16 * i + 8,  4);
            std::memcpy(&d, p.block128 + 16 * i + 12, 4);
            g_Client.Var(0x182284C + 16 * i) = static_cast<int32_t>(a); // dword_182284C[4*i]
            g_Client.Var(0x1822850 + 16 * i) = static_cast<int32_t>(b); // dword_1822850[4*i]
            g_Client.Var(0x1822854 + 16 * i) = static_cast<int32_t>(c); // dword_1822854[4*i]
            g_Client.Var(0x1822858 + 16 * i) = static_cast<int32_t>(d); // dword_1822858[4*i]
        }
        // NOTE (câblage hors périmètre) : ce staging n'est OBSERVABLE que si le handler 0x3a
        // le commite (Net_OnFactionBoardSync 0x490560 : 0x49059A/0x4905A5 + boucle
        // 0x4905AA-0x490668). Ce handler vit dans Net/GameHandlers_ChatSocial.cpp, non détenu
        // par ce front -> signalé au rapport pour routage (anti-pattern « produit-mais-non-
        // consommé » assumé et tracé, pas ignoré).
    });

    // 0x3d PartyResultDialog (Net_OnPartyResultDialog 0x490800) — résultat d'action de groupe :
    // ferme la notice 6, str492..497. Comme 0x30/0x36, le switch est BORNÉ : 0x49083B
    // `cmp [ebp+var_C],5` / 0x49083F `ja def_490848` (« switch 6 cases », case 0 = 0x1EC = 492
    // à 0x490855) -> un code > 5 est MUET. (Défaut non relevé par le recon, trouvé en
    // re-prouvant le scan ci-dessous ; même nature et même correctif que le cas 0x30.)
    OnPacket<PartyResultDialog>(sys, 0x3d, [&sys](const PartyResultDialog& p) {
        CloseNotice(6);
        if (p.resultCode <= 5) // 0x49083B/0x49083F ; default -> aucun message
            g_Client.msg.System(Str(492 + static_cast<int>(p.resultCode)));
        if (p.resultCode == 0) {
            // Premier slot VIDE de g_PartyRosterNames -> demande ses infos (Net_SendOp56).
            // Polarité re-prouvée au DÉSASSEMBLAGE (2026-07-16) — elle était INVERSÉE ici :
            //   0x490887 `push offset String` (src = "" @0x7EC95F, 1er octet NUL vérifié)
            //   0x490892 `add edx, offset g_PartyRosterNames` (dest = names + 13*i)
            //   0x490899 `call Crt_Strcmp` / 0x4908A1 `test eax,eax`
            //   0x4908A3 `jnz short loc_4908A7` -> 0x4908A7 `jmp loc_490878` = INCRÉMENT
            // => strcmp != 0 (nom NON vide) CONTINUE la boucle ; la sortie se fait sur
            //    strcmp == 0, c.-à-d. au 1er slot VIDE.
            // Contre-preuve interne (les deux polarités sont RÉELLEMENT opposées dans le
            // binaire, le C++ les avait uniformisées à tort) : le handler frère 0x3f
            // (Net_OnPartyMemberValueSet 0x490A10) teste `jz loc_490A9B` (0x490A89) et
            // 0x490A9B = `jmp loc_490A5F` = incrément -> là c'est bien le 1er NON vide qui
            // déclenche Net_SendOp57 ; le 0x3f ci-dessous est correct, ne pas l'aligner.
            auto& names = g_World.partyRoster.names;
            for (size_t i = 0; i < names.size(); ++i) { // 0x490881 `cmp var_4,0Ah` / jge
                if (names[i].empty()) {
                    Net_SendOp56(sys.Client(), static_cast<int8_t>(i));
                    break;
                }
            }
            // Roster plein (i == 10) : 0x4908A9 `cmp var_4,0Ah` / 0x4908AD `jnz loc_4908B4`
            // -> 0x4908AF `jmp def_490848` = AUCUN envoi (le `break` ci-dessus n'est jamais
            // pris, donc aucun Net_SendOp56 — fidèle sans garde supplémentaire).
        }
    });

    // 0x3e PartyMemberNameSet (Net_OnPartyMemberNameSet 0x4909A0) — pose le nom d'un membre
    // dans un slot du roster de groupe (g_PartyRosterNames, cf. Game/GameState.h::PartyRoster).
    OnPacket<PartyMemberNameSet>(sys, 0x3e, [](const PartyMemberNameSet& p) {
        const std::string nm = Name(p.name);
        if (p.slotIndex < g_World.partyRoster.names.size())
            g_World.partyRoster.names[p.slotIndex] = nm;
        // TODO(ui): UI_MemberSelectWnd_Open (fenêtre de sélection de membre à inviter).
        //   Aucun message système côté handler d'origine.
    });

    // 0x3f PartyMemberValueSet (Net_OnPartyMemberValueSet 0x490A10) — fixe une valeur de
    // membre si le groupe est actif, puis rescanne le roster de noms pour paginer.
    OnPacket<PartyMemberValueSet>(sys, 0x3f, [&sys](const PartyMemberValueSet& p) {
        if (g_Client.VarGet(0x184BE40)) { // dword_184BE40 = groupe actif
            g_Client.Var(0x184BE50 + 4 * static_cast<int>(p.index)) = static_cast<int32_t>(p.value);
            // Balaye g_PartyRosterNames[index+1..9] ; au premier nom non vide, redemande le
            // suivant (Net_SendOp57) — pagination du roster (RE/net_handler_notes.md).
            auto& names = g_World.partyRoster.names;
            for (size_t i = static_cast<size_t>(p.index) + 1; i < names.size(); ++i) {
                if (!names[i].empty()) {
                    Net_SendOp57(sys.Client(), static_cast<int8_t>(i));
                    break;
                }
            }
        }
    });

    // 0x40 PartyMemberClear (Net_OnPartyMemberClear 0x490AB0) — vide le nom d'un slot du
    // roster de groupe. Comportement CORRECT, mais l'ancienne justification (« Crt_StringInit
    // 1 argument sur le binaire = vrai clear-à-vide ») était FAUSSE : Crt_StringInit est un
    // strcpy et prend TOUJOURS 2 arguments (le pseudocode Hex-Rays masque simplement les
    // arguments). Ce clear est réel parce que la SOURCE poussée est `offset String`
    // (0x490AC7 = "" @0x7EC95F), le dest étant g_PartyRosterNames + 13*idx (0x490AD2/0x490AD8).
    // C'est ce critère — et non un nombre d'arguments — qui distingue un clear d'une copie.
    OnPacket<PartyMemberClear>(sys, 0x40, [](const PartyMemberClear& p) {
        if (p.slotIndex < g_World.partyRoster.names.size())
            g_World.partyRoster.names[p.slotIndex].clear();
    });

    // 0x4a GuildRosterReset (Net_OnGuildRosterReset 0x4911D0) — malgré son nom, ce handler
    // ne réinitialise RIEN : il CHARGE le roster d'alliance avec les 5 noms du payload.
    // Prouvé au désassemblage (2026-07-16) : Crt_StringInit(arg1=dest, arg2=src) est un
    // strcpy — le pseudocode Hex-Rays l'affiche « Crt_StringInit() » sans argument, ce qui
    // avait induit un `Reset()` erroné ici. Un vrai clear-à-vide se reconnaît à son
    // `push offset String` (0x7EC95F = chaîne vide, vérifié : 1er octet NUL) ; AUCUN des 6
    // appels de 0x4a n'en pousse — ce sont 6 copies :
    //   0x49125B slot0 = name1 (payload+4)    0x49126C slot1 = name2 (payload+17)
    //   0x49127D slot2 = name3 (payload+30)   0x49128E slot3 = name4 (payload+43)
    //   0x49129F slot4 = name5 (payload+56)
    //   0x4912B1 g_LocalGuildName (0x168740C) = g_AllianceRosterNames[0] (0x16749B8)
    //            -> le nom du fondateur/leader SERT de nom de guilde.
    // PLACEMENT : les 6 copies (0x491252-0x4912B6) précèdent le test de mode
    // (0x4912B9 `mov edx,[ebp+var_58]` / 0x4912BF `cmp [ebp+var_5C],1`) : elles sont donc
    // INCONDITIONNELLES et ont lieu pour TOUS les modes, y compris le mode 3 et les modes
    // inconnus (0x4912CF `jz loc_491316` / 0x4912D1 `jmp loc_491316` : sortie muette).
    OnPacket<GuildRosterReset>(sys, 0x4a, [](const GuildRosterReset& p) {
        auto& ar = g_World.allianceRoster;
        ar.memberNames[0] = Name(p.name1);  // 0x49125B
        ar.memberNames[1] = Name(p.name2);  // 0x49126C
        ar.memberNames[2] = Name(p.name3);  // 0x49127D
        ar.memberNames[3] = Name(p.name4);  // 0x49128E
        ar.memberNames[4] = Name(p.name5);  // 0x49129F
        ar.guildName      = ar.memberNames[0]; // 0x4912B1 (copie du slot 0, PAS un vidage)
        // Slot par slot, sans compactage : un nom vide du payload laisse le slot vide.
        if (p.mode == 1)      g_Client.msg.System(Str(349)); // 0x4912D3 (0x15D)
        else if (p.mode == 2) g_Client.msg.System(Str(881)); // 0x4912F5 (0x371)
        // mode 3 / défaut -> loc_491316 : aucun message (fidèle).
    });

    // 0x4b GuildMemberJoin (Net_OnGuildMemberJoin 0x491330) — nouveau membre : inscrit dans
    // la 1re case libre du roster alliance (slots 1..4, slot 0 = leader jamais réattribué),
    // puis annonce "<str350> [nom]<str351>".
    // La garde 0x491397 `if (i != 5)` englobe À LA FOIS l'écriture du slot (0x4913AF) ET le
    // message (0x4913C6-0x491405) : roster plein (i==5, boucle 0x491359 `for i=1;i<5` sortie
    // sur premier slot vide via 0x491383 `Crt_Strcmp(&g_AllianceRosterNames[13*i], "")`) ->
    // AUCUNE mutation ET AUCUN message. AddMember() renvoie précisément ce booléen.
    OnPacket<GuildMemberJoin>(sys, 0x4b, [](const GuildMemberJoin& p) {
        const std::string nm = Name(p.name);
        if (g_World.allianceRoster.AddMember(nm)) // 0x491397 : garde commune écriture+message
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(351));
    });

    // 0x4c GuildChatMessage — chat de guilde : "<str350> [expéditeur] message".
    OnPacket<GuildChatMessage>(sys, 0x4c, [](const GuildChatMessage& p) {
        const std::string sender = Name(p.senderName);
        const std::string body   = std::string(p.message, ::strnlen(p.message, sizeof p.message));
        // Original : posté seulement si g_ChatShow_Guild==1 (flag non modélisé -> toujours posté).
        g_Client.msg.Chat(Str(350) + " [" + sender + "] " + body, kChatColorGuild, sender.c_str());
    });

    // 0x4d GuildMemberLeave (Net_OnGuildMemberLeave 0x4914D0) — un membre quitte l'alliance :
    // si `nm` == nom local -> vide tout le roster (départ de MOI-même) + Str(882) ; sinon
    // retire `nm` du roster et compacte (cf. AllianceRoster::RemoveMember ci-dessus, note de
    // fidélité sur l'algorithme de compactage). SelfState::localPlayerName vide par défaut
    // (non peuplé par aucun handler à ce jour) -> tant qu'il ne l'est pas, cette branche est
    // simplement jamais prise (dégradation honnête, cf. commentaire GameState.h).
    OnPacket<GuildMemberLeave>(sys, 0x4d, [](const GuildMemberLeave& p) {
        const std::string nm = Name(p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName) {
            g_World.allianceRoster.Reset();
            g_Client.msg.System(Str(882));
        } else if (g_World.allianceRoster.RemoveMember(nm)) { // 0x4915E7 : garde commune
            // 0x4915E7 `if (i != 5)` englobe le compactage (0x4915FF + boucle 0x49160D) ET le
            // message (0x491670-0x4916AF) : nom absent du roster -> aucun message.
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(352));
        }
    });

    // 0x4e GuildMemberKick (Net_OnGuildMemberKick 0x4916D0) — un membre est expulsé : même
    // logique que GuildMemberLeave ci-dessus (self -> reset + Str(883) ; sinon retire+compacte).
    OnPacket<GuildMemberKick>(sys, 0x4e, [](const GuildMemberKick& p) {
        const std::string nm = Name(p.name);
        if (!g_World.self.localPlayerName.empty() && nm == g_World.self.localPlayerName) {
            g_World.allianceRoster.Reset();
            g_Client.msg.System(Str(883));
        } else if (g_World.allianceRoster.RemoveMember(nm)) { // 0x4917E7 : garde commune
            // Idem 0x4d : 0x4917E7 `if (i != 5)` englobe compactage ET message str353.
            g_Client.msg.System(Str(350) + " [" + nm + "]" + Str(353));
        }
    });

    // 0x4f GuildRosterUpdate (Net_OnGuildRosterUpdate 0x4918D0) — dispatcher à 3 cas.
    //   cas 1 (0x491943) : reset intégral (5 slots + guildName, tous en `push offset String`),
    //     str350+" "+str354. Vérifié au désassemblage — fidèle.
    //   cas 2 (0x491A00) : message seul str350+" "+str884 (0x374), AUCUNE mutation.
    //   cas 3 (0x491A51) : test leader `Crt_Strcmp(g_AllianceRosterNames[0], byte_1673184)`
    //     (0x491A5B) puis `test eax,eax` / 0x491A65 `jnz loc_491B28`. Sémantique RÉELLE,
    //     re-prouvée au désasm (2026-07-16) — l'implémentation précédente l'avait INVERSÉE :
    //       strcmp==0  (slot 0 == mon nom -> JE SUIS le leader) : chute en 0x491A6B =
    //         reset intégral + str350+" "+str354 (0x162).  <- 354, PAS 700
    //       strcmp!=0  (je ne suis PAS le leader) : loc_491B28 = PAS de reset, mais
    //         0x491B48 g_LocalGuildName = payloadName, puis boucle i=1..4
    //         (0x491B50-0x491BA6) : 0x491B84 slot[i-1] = slot[i] ; 0x491B9E slot[i] = ""
    //         => DÉCALAGE d'un cran vers le bas, slot4 vidé ; str350+" "+str700 (0x2BC).
    //     L'écriture 0x491B34 (slot0 = payloadName) est MORTE — réécrasée dès i=1 par
    //     slot0 = slot1 — donc non reproduite ici.
    //   NUANCE (état dégénéré, documentée et NON « corrigée ») : le binaire fait un strcmp
    //     brut, donc deux chaînes vides s'égalent -> branche reset. IsLeader() renvoie false
    //     sur nom vide -> branche décalage. Comme SelfState::localPlayerName n'est peuplé par
    //     aucun handler à ce jour, le cas 3 prendra TOUJOURS la branche non-leader. C'est la
    //     dégradation la plus fidèle disponible (dans le binaire byte_1673184 est toujours
    //     renseigné, et un joueur réel non-leader prend bien la branche décalage) ; on garde
    //     IsLeader() par convention « jamais de faux c'est moi ».
    OnPacket<GuildRosterUpdate>(sys, 0x4f, [](const GuildRosterUpdate& p) {
        switch (p.code) {
            case 1: // 0x491943 — dissolution : réinitialise tout le roster
                g_World.allianceRoster.Reset();
                g_Client.msg.System(Str(350) + " " + Str(354));
                break;
            case 2: // 0x491A00 — message informatif seul, aucune mutation
                g_Client.msg.System(Str(350) + " " + Str(884));
                break;
            case 3: {
                auto& ar = g_World.allianceRoster;
                if (ar.IsLeader(g_World.self.localPlayerName)) { // 0x491A5B strcmp==0 -> 0x491A6B
                    ar.Reset();                                  // 6x `push offset String`
                    g_Client.msg.System(Str(350) + " " + Str(354)); // 0x491AD7 (0x162)
                } else {                                         // 0x491A65 jnz -> loc_491B28
                    ar.guildName = Name(p.name);                 // 0x491B48
                    for (int i = 1; i < game::AllianceRoster::kMaxSlots; ++i) { // 0x491B50-0x491BA6
                        ar.memberNames[static_cast<size_t>(i - 1)] =
                            ar.memberNames[static_cast<size_t>(i)];             // 0x491B84
                        ar.memberNames[static_cast<size_t>(i)].clear();         // 0x491B9E
                    }
                    g_Client.msg.System(Str(350) + " " + Str(700)); // 0x491BA8 (0x2BC)
                }
                break;
            }
            default: // 0x4918D0 : aucun autre cas -> sortie muette
                break;
        }
    });

    // 0x53 TeamFormationDispatch (Net_OnTeamFormationDispatch 0x491E70) — MÉGA-DISPATCHER
    // guilde. statusCode (v88 @0x8156C1) : 0 = succès, >0 = codes d'erreur ; subOpcode
    // (v86 @0x8156C5) sélectionne le sous-traitement ; guildBlob 0x56C o @0x8156C9.
    // Le switch maître (0x491EED) traite EXACTEMENT 14 sous-opcodes :
    //   {1,2,3,4,5,6,7,8,9,0xA,0xC,0xD,0xE,0x11}. 0xB/0xF/0x10 tombent en `default` (no-op) :
    //   leur ABSENCE est donc FIDÈLE, ce ne sont pas des oublis.
    // g_GmCmdCooldownLatch = 0 est posé DANS CHACUN des 14 cas (0x491EF4, 0x49200F, 0x492334,
    //   0x4923FD, 0x49256E, 0x492631, 0x4927BA, 0x49283C, 0x492979, 0x492AC0, 0x492BD6,
    //   0x492C20, 0x492C6A, 0x492D12) et JAMAIS sur le `default` — il est donc déplacé dans
    //   les cas (l'ancienne pose inconditionnelle divergeait sur 0xB/0xF/0x10).
    // Le `TODO(msg)` global est résolu : les ids StrTable005 ci-dessous sont relevés un par un
    //   au décompilé (EA cités). Restent en TODO ancré les écritures vers des BUFFERS DE
    //   CHAÎNE non modélisés (0x16746A8/0x16746BC/0x168725C/0x1687270/0x1839D00/0x183A101) :
    //   les poser en Var(int32) serait activement FAUX (ce sont des chaînes), et la grille de
    //   sélection dword_183A014/018 + le rang dword_1839C38[] sont déclarés HORS PÉRIMÈTRE
    //   par Game/GuildSystem.h (fichier non détenu par ce front).
    OnPacket<TeamFormationDispatch>(sys, 0x53, [&sys](const TeamFormationDispatch& p) {
        switch (p.subOpcode) { // 0x491EED
            case 1: // création de guilde — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x491EF4
                if (p.statusCode == 0) {                     // 0x491F0E
                    // TODO(state) [0x491F36] dword_16746A8 = <chaîne du blob> (COPIE, pas un
                    //   clear) ; [0x491F57] unk_16746BC = "" ; [0x491F6B] byte_168725C =
                    //   <chaîne du blob, src = v85/guildBlob> ; [0x491F87] unk_1687270 = "".
                    //   Buffers de chaîne non modélisés côté C++ — ne pas forcer en Var.
                    g_Client.Var(0x16746B8) = 0;             // 0x491F43
                    g_Client.Var(0x168726C) = 0;             // 0x491F73
                    g_Client.msg.System(Str(392));           // 0x491F9F (0x188)
                    // TODO(ui) [0x491FB4] UI_GuildCreate_Open (0x667DA0).
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(393));           // 0x491FCF
                } else if (p.statusCode == 2) {
                    g_Client.msg.System(Str(394));           // 0x491FF5
                }
                break;
            case 2: // montée de palier de guilde — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x49200F
                if (p.statusCode == 0) {                     // 0x492029
                    // Sous-dispatch sur dword_1675B10 (0x492050) — déjà modélisé en
                    // Net/ClientState.h (posé par SendPackets.cpp:2545 = ctxId de la requête).
                    // Le binaire n'a PAS de `case 0` : valeur 0 (défaut) -> aucun effet,
                    // aucun message parasite. Fidèle sans garde supplémentaire.
                    switch (dword_1675B10) {
                        case 1: // 0x49207A : ouvre simplement le gestionnaire
                            // TODO(state) [0x49207A] Crt_Memcpy(unk_1839970, blob, 0x56C).
                            // TODO(ui)    [0x492087] UI_GuildMgrWnd_Open(g_Guild) (0x667E20).
                            break;
                        case 2: { // 0x4920C0 : montée de palier réelle
                            // TODO(state) [0x4920C0] Crt_Memcpy(unk_1839970, blob, 0x56C) :
                            //   le blob recouvre l'état DÉJÀ modélisé par g_Guild (roster 50
                            //   membres) — le poser en Blob brut dupliquerait l'état, donc
                            //   non forcé (cf. GuildSystem.h, layout partiellement déduit).
                            g_Guild.CountMembers();          // 0x4920CD (Guild_CountMembers)
                            // dword_1839980 = PALIER DE GUILDE courant : absent de GuildRoster
                            // (Game/GuildSystem.h non détenu par ce front) et son paquet
                            // d'origine n'est pas identifié dans ce module -> Var de longue
                            // traîne plutôt qu'un champ deviné.
                            const int32_t tier = g_Client.VarGet(0x1839980);
                            if (g_Guild.memberCount >= 10 * tier) { // 0x4920E1
                                // Seuils {niveau, poids} par palier — 0x492136.
                                // NB : le global comparé est g_InvWeight (0x16732AC), que le
                                // dossier appelait « gold » ; on cite l'adresse, pas l'étiquette.
                                switch (tier) {
                                    case 1: // 0x492144 / 0x492176 (0x1312D00 = 20 000 000)
                                        if (g_World.self.level < 50)               g_Client.msg.System(Str(570)); // 0x492157
                                        else if (g_Client.inv.weight < 20000000)   g_Client.msg.System(Str(571)); // 0x492189
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5 (LABEL_42)
                                        break;
                                    case 2: // 0x4921AA / 0x4921DB
                                        if (g_World.self.level < 70)               g_Client.msg.System(Str(572)); // 0x4921BC
                                        else if (g_Client.inv.weight < 30000000)   g_Client.msg.System(Str(573)); // 0x4921EE
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5
                                        break;
                                    case 3: // 0x49220F / 0x492241
                                        if (g_World.self.level < 90)               g_Client.msg.System(Str(574)); // 0x492222
                                        else if (g_Client.inv.weight < 40000000)   g_Client.msg.System(Str(575)); // 0x492253
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5
                                        break;
                                    case 4: // 0x492274 / 0x4922A6
                                        if (g_World.self.level < 113)              g_Client.msg.System(Str(576)); // 0x492287
                                        else if (g_Client.inv.weight < 50000000)   g_Client.msg.System(Str(577)); // 0x4922B9
                                        else Net_SendGuarded_7(sys.Client());                                     // 0x4922F5
                                        break;
                                    default:
                                        g_Client.msg.System(Str(569)); // 0x4922E0
                                        break;
                                }
                            } else {
                                g_Client.msg.System(Str(568)); // 0x4920F3 (membres insuffisants)
                            }
                            break;
                        }
                        case 3: // 0x4920A2 : simple resynchronisation
                            // TODO(state) [0x4920A2] Crt_Memcpy(unk_1839970, blob, 0x56C) seul.
                            break;
                        default: break; // pas de case 0 dans le binaire
                    }
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(391));           // 0x49231A
                }
                break;
            case 3: // 0x491EED (absent du switch C++ précédent)
                g_GmCmdCooldownLatch = 0;                    // 0x492334
                switch (p.statusCode) {                      // 0x49235A
                    case 0: g_Client.msg.System(Str(412));  break; // 0x492372
                    case 1: g_Client.msg.System(Str(413));  break; // 0x492398
                    case 2: g_Client.msg.System(Str(414));  break; // 0x4923BD
                    case 3: g_Client.msg.System(Str(2044)); break; // 0x4923E3
                    default: break;                                // muet
                }
                break;
            case 4: // sortie/annulation — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x4923FD
                if (p.statusCode == 0) {                     // 0x492417
                    ResetGuildStateBlock();                  // 0x492442-0x4924F3
                    g_Client.msg.System(Str(478));           // 0x492508
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(479));           // 0x49252E
                } else if (p.statusCode == 2) {
                    g_Client.msg.System(Str(2046));          // 0x492554
                }
                break;
            case 5: // 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x49256E
                if (p.statusCode == 0) {                     // 0x492588
                    // TODO(state) [0x4925A2/0x4925B4/0x4925C6/0x4925D8] 4x Crt_StringInit sur
                    //   des buffers de chaîne non modélisés (mêmes cibles que le cas 1).
                    g_Client.msg.System(Str(544));           // 0x4925F1
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(545));           // 0x492617
                }
                break;
            case 6: // dissolution — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x492631
                switch (p.statusCode) {                      // 0x492657
                    case 0:
                        ResetGuildStateBlock();              // 0x492668-0x492719
                        g_Client.msg.System(Str(470));       // 0x49272F
                        break;
                    case 1: g_Client.msg.System(Str(471));  break; // 0x492754
                    case 2: g_Client.msg.System(Str(468));  break; // 0x49277A
                    case 3: g_Client.msg.System(Str(2047)); break; // 0x4927A0
                    default: break;                                // muet
                }
                break;
            case 7: // invitation — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x4927BA
                if (p.statusCode == 0) {                     // 0x4927D4
                    g_Client.msg.System(Str(578));           // 0x4927F2
                    // TODO(ui) [0x492807] UI_GuildCreate_Open (0x667DA0).
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(579));           // 0x492822
                }
                break;
            case 8: // exclusion (kick) — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x49283C
                if (p.statusCode == 0) {                     // 0x492856
                    // TODO(state) [0x492874-0x492916] comparaisons dword_1839991/183999E vs
                    //   byte_183A0F4 + 4x Crt_StringInit, et [0x4928EF]
                    //   dword_1839C38[10*dword_183A014 + dword_183A018] = 0 : la grille de
                    //   sélection (183A014/018) et rank[] relèvent de GuildRoster
                    //   (Game/GuildSystem.h), déclarés HORS PÉRIMÈTRE — fichier non détenu.
                    g_Guild.CountMembers();                  // 0x492923
                    g_Client.msg.System(Str(475));           // 0x492939
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(476));           // 0x49295F
                }
                break;
            case 9: // candidature / acceptation — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x492979
                if (p.statusCode == 0) {                     // 0x492993
                    // dword_183A0F0 = sélecteur 1/2 (non modélisé -> Var de longue traîne).
                    const int32_t sel = g_Client.VarGet(0x183A0F0);
                    if (sel == 1) {                          // 0x4929BA
                        // TODO(state) [0x4929D6] dword_1839C38[10*183A014+183A018] = 1 (rank,
                        //   hors périmètre GuildSystem.h).
                        g_Client.msg.System(Str(550));       // 0x4929F1
                    } else if (sel == 2) {                   // 0x4929C3
                        // TODO(state) [0x492A15] idem = 2.
                        g_Client.msg.System(Str(551));       // 0x492A31
                    }
                } else if (p.statusCode == 1) {              // 0x49299C
                    const int32_t sel = g_Client.VarGet(0x183A0F0); // 0x492A4B
                    if (sel == 1)      g_Client.msg.System(Str(552)); // 0x492A7B
                    else if (sel == 2) g_Client.msg.System(Str(553)); // 0x492AA1
                }
                break;
            case 10: // 0xA — 0x491EED
                g_GmCmdCooldownLatch = 0;                    // 0x492AC0
                // (1) RÉFUTATION du dossier de gaps, qui affirmait « le cas 0xA n'écrit AUCUN
                //     état ». FAUX — désasm 0x492AEE-0x492B0C :
                //       Crt_StringInit(dest = unk_1839D00 + 5*(10*dword_183A014 + dword_183A018),
                //                      src  = dword_183A101)
                //     C'est bien une écriture. TODO(state) : unk_1839D00 = « bloc 5 o/membre »
                //     de g_Guild (+920), explicitement HORS PÉRIMÈTRE dans Game/GuildSystem.h
                //     (fichier non détenu), indexé par la grille 183A014/018 elle aussi hors
                //     périmètre. Voir (2) : la source étant toujours vide, c'est en pratique un
                //     effacement du bloc de 5 o.
                // (2) dword_183A101 est PROUVÉ CONSTAMMENT VIDE, ce qui fixe les messages sans
                //     rien deviner : xrefs_to(0x183A101) = 3, et les 3 sont des LECTURES, toutes
                //     dans cette même fonction (0x492AEE = src du StringInit ci-dessus,
                //     0x492B19 et 0x492B77 = arg1 des Crt_Strcmp) — AUCUN site d'écriture dans
                //     tout le binaire ; get_bytes(0x183A101) = 0x0 0x0 ... = chaîne vide.
                //     Le voisin byte_183A0F4 (écrit, lui, par UI_MsgBox_OnLButtonUp 0x5C0A90) ne
                //     peut pas déborder dessus : 0x183A101 == 0x183A0F4 + 13, soit exactement la
                //     taille d'un nom NUL-terminé (12 car. + NUL) — les deux buffers sont
                //     adjacents mais disjoints.
                //     => Crt_Strcmp(dword_183A101, "") vaut TOUJOURS 0 (branche « vide »), donc
                //        les branches 557 (0x492B3B) et 559 (0x492B99) sont du CODE MORT dans le
                //        binaire livré, et seuls 558/560 sont atteignables. Reproduit tel quel.
                if (p.statusCode == 0) {                     // 0x492ADA
                    g_Client.msg.System(Str(558));           // 0x492B5D (branche `strcmp == 0`)
                } else if (p.statusCode == 1) {              // 0x492AE3
                    g_Client.msg.System(Str(560));           // 0x492BBC (branche `strcmp == 0`)
                }
                break;
            case 12: // 0xC — bascule ON (absent du switch C++ précédent)
                g_GmCmdCooldownLatch = 0;                    // 0x492BD6
                if (p.statusCode == 0) {                     // 0x492BF0
                    // dword_16746C8 : 4 xrefs = 2 écritures ici (0xC/0xD) + 2 VRAIES lectures
                    //   (cGameHud_Render 0x64A9C4, cGameHud_OnMouseDown 0x62B6CD) — bascule de
                    //   variante du sprite HUD unk_9465D0 + son hit-test. Ce sprite n'est pas
                    //   modélisé côté C++ : poser le Var est nécessaire mais pas suffisant pour
                    //   un effet visible (câblage HUD = gap distinct, à tracer, pas à forcer ici).
                    g_Client.Var(0x16746C8) = 1;             // 0x492BFD
                    // dword_1687278 : conservé par fidélité d'écriture, mais WRITE-ONLY dans le
                    //   binaire (xrefs_to = 2, les 2 sont ces écritures — aucun lecteur).
                    g_Client.Var(0x1687278) = 1;             // 0x492C07
                }
                break;
            case 13: // 0xD — bascule OFF, symétrique de 0xC (absent du switch C++ précédent)
                g_GmCmdCooldownLatch = 0;                    // 0x492C20
                if (p.statusCode == 0) {                     // 0x492C3A
                    g_Client.Var(0x16746C8) = 0;             // 0x492C47
                    g_Client.Var(0x1687278) = 0;             // 0x492C51
                }
                break;
            case 14: // 0xE — (absent du switch C++ précédent)
                g_GmCmdCooldownLatch = 0;                    // 0x492C6A
                switch (p.statusCode) {                      // 0x492C90
                    case 0: break;                                 // no-op explicite
                    case 1: case 5: g_Client.msg.System(Str(223));  break; // 0x492CAD
                    case 2:         g_Client.msg.System(Str(1717)); break; // 0x492CD3
                    case 4:         g_Client.msg.System(Str(1718)); break; // 0x492CF8
                    default: break;                                // muet
                }
                break;
            case 17: // 0x11 — départ volontaire / promotion
                g_GmCmdCooldownLatch = 0;                    // 0x492D12
                if (p.statusCode == 0) {                     // 0x492D2C
                    // TODO(state) [0x492D57-0x492DE5] 4x Crt_StringInit + [0x492DBE]
                    //   dword_1839C38[10*183A014+183A018] = 0, puis [0x492DED] scan des 50
                    //   membres `Crt_Strcmp(unk_18399AB + 13*i, byte_1673184)` et [0x492E2A]
                    //   dword_1839C38[i] = 2 : rank[]/grille = HORS PÉRIMÈTRE (GuildSystem.h,
                    //   fichier non détenu). GuildRoster::RemoveMember(name) existe déjà mais
                    //   ne pose PAS rank=2 -> ne pas l'appeler à la place (sémantique ≠).
                    g_Client.Var(0x16746B8) = 2;             // 0x492E35
                    g_Client.msg.System(Str(2108));          // 0x492E50
                } else if (p.statusCode == 1) {
                    g_Client.msg.System(Str(2127));          // 0x492E73
                } else if (p.statusCode == 2) {
                    g_Client.msg.System(Str(2109));          // 0x492E95
                }
                break;
            default: // 0xB / 0xF / 0x10 et au-delà : no-op TOTAL, latch NON remis à 0 (fidèle)
                break;
        }
    });

    // 0x54 GuildNoticeChat — message de chat de faction/guilde : "<str543> [nom] message".
    OnPacket<GuildNoticeChat>(sys, 0x54, [](const GuildNoticeChat& p) {
        const std::string nm   = Name(p.name);
        const std::string body = std::string(p.message, ::strnlen(p.message, sizeof p.message));
        g_Client.msg.Faction(Str(543) + " [" + nm + "] " + body, kChatColorFaction, nm.c_str());
    });

    // 0x56 TeamSlotAssign — écrit une valeur dans le slot d'équipe courant (curseur global) puis
    // enchaîne le balayage du roster (Guild_SelectNextMember) ; un balayage réussi (prochain slot
    // non vide trouvé) envoie Net_SendOp78(name[cursor]) — câblé ici (cf. TODO(send) résolu dans
    // Game/GuildSystem.h : « à brancher depuis Net/GameHandlers_PartyGuild.cpp »).
    OnPacket<TeamSlotAssign>(sys, 0x56, [&sys](const TeamSlotAssign& p) {
        const int idx = g_Client.VarGet(0x183A020);                     // dword_183A020 = curseur
        g_Client.Var(0x1839EDC + 4 * idx) = static_cast<int32_t>(p.value); // dword_1839EDC[idx]
        if (g_Guild.SelectNextMember()) {
            char name13[13] = {};
            const std::string& nm = g_Guild.members[g_Guild.cursor].name;
            const size_t n = nm.size() < sizeof name13 ? nm.size() : sizeof name13;
            std::memcpy(name13, nm.data(), n);
            Net_SendOp78(sys.Client(), name13);
        }
    });

    // 0x5c GuildActionResult — résultat d'action de guilde (3 cas ; extra lu mais inutilisé).
    OnPacket<GuildActionResult>(sys, 0x5c, [](const GuildActionResult& p) {
        (void)p.extra;
        switch (p.action) {
            case 1: if (p.flag == 0) g_Client.msg.System(Str(758)); break;
            case 2: if (p.flag == 0) g_Client.msg.System(Str(759)); break;
            case 3:
                if (p.flag == 0)      g_Client.msg.System(Str(760));
                else if (p.flag == 1) g_Client.msg.System(Str(761));
                break;
            default: break;
        }
    });

    // 0x5d PartyInviteResult — résultat d'invitation de groupe (5 cas ; certains avec "[param]").
    OnPacket<PartyInviteResult>(sys, 0x5d, [](const PartyInviteResult& p) {
        const std::string param = std::to_string(p.param);
        switch (p.code) {
            case 1: g_Client.msg.System(Str(372)); break;
            case 2: g_Client.msg.System("[" + param + "]" + Str(373)); break; // couleur 3
            case 3: g_Client.msg.System(Str(374)); break;
            case 4: g_Client.msg.System("[" + param + "]" + Str(375)); break; // couleur 2
            case 5: g_Client.msg.System(Str(376)); break;
            default: break;
        }
    });

    // 0x7b PartyMemberTargetSet — fixe la cible d'un membre (résolu par identité réseau).
    OnPacket<PartyMemberTargetSet>(sys, 0x7b, [](const PartyMemberTargetSet& p) {
        const int e = FindPlayerIndex(p.idHi, p.idLo);
        if (e >= 0) // word_1687454[454*e] = (u16)targetVal ; stride entité = 908 o
            g_Client.Var(0x1687454 + 908 * e) =
                static_cast<int32_t>(static_cast<uint16_t>(p.targetVal));
    });

    // 0x7f PartyMemberHpSet — pose HP/MP courant+max d'un membre (kind 1/2), message si self.
    OnPacket<PartyMemberHpSet>(sys, 0x7f, [](const PartyMemberHpSet& p) {
        const int e = FindPlayerIndex(p.entityIdHi, p.entityIdLo);
        if (e >= 0 && (p.kind == 1 || p.kind == 2)) {
            g_Client.Var(0x1687458 + 908 * e) = p.curValue; // dword_1687458[227*e]
            g_Client.Var(0x168745C + 908 * e) = p.maxValue; // dword_168745C[227*e]
            if (e == 0) {                                   // self : message + action
                g_Client.msg.System(p.kind == 1 ? Str(2012) : Str(2013));
                // TODO(send): Player_QueueAction_op91 (EA 0x517490) enfile une action sur
                //   g_PlayerCmdController (dword_1669170, gros struct de commandes joueur — throttling/
                //   coalescing par tick) qui finit par émettre Net_SendOp91 (opcode 0x5B, sans payload).
                //   NE PAS FORCER un appel direct à Net_SendOp91 ici : g_PlayerCmdController n'est PAS
                //   modélisé dans ce module (cf. Net/CombatResultApply.cpp lignes 108/196/240/321/347 et
                //   Game/MapWarp.cpp — mêmes TODO(net) non résolus, tous en attente de ce même système
                //   de file de commandes joueur ; précédent délibérément respecté pour rester cohérent).
            }
        }
    });

    // 0x80 PartyMemberUpdate — maj d'un champ de membre + gestion de départ (selector 4 = self).
    OnPacket<PartyMemberUpdate>(sys, 0x80, [](const PartyMemberUpdate& p) {
        const int e = FindPlayerIndex(p.idHi, p.idLo);
        if (e < 0) return;
        switch (p.selector) {
            case 1: case 2: case 3:
                g_Client.Var(0x1687458 + 908 * e) = static_cast<int32_t>(p.value1);
                g_Client.Var(0x168745C + 908 * e) = static_cast<int32_t>(p.value2);
                break;
            case 4:
                if (e == 0) { // self : sortie de groupe -> reset HP/MP + warp ville
                    g_Client.Var(0x1687458) = 0;
                    g_Client.Var(0x168745C) = 0;
                    g_Client.msg.System(Str(117));
                    // Résout la cible de warp (garde + coords) ; l'envoi réseau reste un
                    // TODO(send) interne à MapWarp.cpp (Net_SendPacket_Op20, EA 0x55c66f).
                    BeginWarpToFactionTown(static_cast<int32_t>(net::g_LocalElement), false, 0, &g_CoordResolver);
                }
                break;
            default: break;
        }
    });

    // 0x81 PartyItemResult — pose une cellule d'inventaire (status 1) ou quitte le groupe (2).
    OnPacket<PartyItemResult>(sys, 0x81, [](const PartyItemResult& p) {
        if (p.status == 1) {
            g_Client.inv.Set(p.invRow, p.invCol, p.itemId,
                             p.gridPos % 8, p.gridPos / 8, 0, 0, 0);
            g_Client.msg.System(Str(1977));
        } else if (p.status == 2) {
            g_Client.msg.System(Str(117));
            g_Client.Var(0x1687458) = 0; // dword_1687458[0] (self)
            g_Client.Var(0x168745C) = 0; // dword_168745C[0]
            BeginWarpToFactionTown(static_cast<int32_t>(net::g_LocalElement));
        }
    });
}

} // namespace ts2::net

// Game/GuildSystem.cpp — implémentation de GuildRoster (voir GuildSystem.h pour le
// mapping complet des offsets de g_Guild 0x1839968 et les EAs d'origine).
#include "Game/GuildSystem.h"

namespace ts2::game {

// Guild_CountMembers 0x66BBC0 :
//   *(this+429) = 0;
//   for (i = 0; i < 50; ++i)
//     if (Crt_Strcmp((char*)this + 13*i + 67, &String))   // &String = "" @0x7EC95F
//       ++*(this+429);
//   return *(this+429);
int GuildRoster::CountMembers() {
    memberCount = 0;
    for (const auto& m : members) {
        if (!m.Empty())
            ++memberCount;
    }
    return memberCount;
}

// Guild_SelectNextMember 0x66BC30 :
//   if (!*(this+399)) return <inchangé>;
//   for (i = *(this+430) + 1; i < 50; ++i) {
//     if (Crt_Strcmp((char*)this + 13*i + 67, &String)) break;  // slot non vide trouvé
//   }
//   if (i == 50) { *(this+430) = 49; }
//   else { *(this+430) = i; return Net_SendOp78(&unk_846C08, name[i]); }
bool GuildRoster::SelectNextMember() {
    if (!active)
        return false;

    int i = cursor + 1;
    for (; i < kMaxMembers; ++i) {
        if (!members[i].Empty())
            break;
    }

    if (i == kMaxMembers) {
        cursor = kMaxMembers - 1; // *(this+430) = 49
        return false;
    }

    cursor = i;
    return true; // Net_SendOp78(members[cursor].name) envoyé par l'appelant, cf. GameHandlers_PartyGuild.cpp (0x56)
}

// Guild_AddMemberFromInput 0x66BCD0 :
//   result = GetWindowTextA(edit, buf, 1000);           // UI, hors périmètre
//   if (!result) return result;                          // boîte vide -> rien
//   SetWindowTextA(edit, "");                             // UI, hors périmètre
//   if (maybe_Dict001_MatchWord(dict, buf))               // nom banni
//     return Msg_AppendSystemLine(StrTable005_Get(112), color);
//   else
//     return Net_SendOp76(&unk_846C08, buf);              // envoi de la demande d'ajout
bool GuildRoster::AddMember(const std::string& name, bool banned) const {
    if (name.empty())
        return false; // GetWindowTextA aurait retourné 0 : rien à envoyer
    if (banned)
        return false; // maybe_Dict001_MatchWord != 0 : message système, pas d'envoi
    return true;       // envoi Net_SendOp76(name) fait par l'appelant, cf. UI/GuildWindow.cpp::ConfirmAdd
}

// Net_OnTeamFormationDispatch 0x491E70 case 8 (kick, @0x492874-0x492923) :
//   sub_75CAB0((char*)&unk_18399AB + 130*row + 13*col);          // name[index] = ""
//   dword_1839C38[10*row + col] = 0;                              // rank[index] = 0
//   sub_75CAB0((char*)&unk_1839D00 + 50*row + 5*col);             // champ annexe (HORS PÉRIMÈTRE)
//   Guild_CountMembers(&unk_1839968);
// (index = 10*row + col dans l'original, ici directement le paramètre `index`)
void GuildRoster::RemoveMember(int index) {
    if (index < 0 || index >= kMaxMembers)
        return;
    members[index].name.clear();
    members[index].rank = 0;
    CountMembers();
}

// Net_OnTeamFormationDispatch 0x491E70 case 17 (self-leave, @0x492ded-0x492e2a) :
//   for (i = 0; i < 50 && Crt_Strcmp((char*)&unk_18399AB + 13*i, byte_1673184); ++i);
//   dword_1839C38[i] = 2;   // <- écrit MÊME si i==50 (introuvable) dans l'original ;
//                              écart documenté : ici on ne fait rien si introuvable.
void GuildRoster::RemoveMember(const std::string& name) {
    for (int i = 0; i < kMaxMembers; ++i) {
        if (members[i].name == name) {
            members[i].name.clear();
            members[i].rank = 0;
            CountMembers();
            return;
        }
    }
    // Non trouvé : écart de fidélité volontaire, voir commentaire en en-tête (GuildSystem.h).
}

// UI_GuildMgrWnd_Open 0x667E20 (@0x667E29-0x667EB6) :
//   for (i = 0; i < 50; ++i) *(this+i+349) = -2;   // slotValue[i] = -2
//   *(this+399) = 1;                                // active = true
//   for (j = 0; j < 26; ++j) *(this+j+400) = 0;      // HORS PÉRIMÈTRE (état UI non modélisé)
//   *(this+426) = 1; *(this+427) = 0; *(this+428) = -1; // HORS PÉRIMÈTRE (sélection UI)
//   *(this+430) = -1;                               // cursor = -1
//   Guild_CountMembers(this);
//   return Guild_SelectNextMember(this);
void GuildRoster::Reset() {
    for (auto& m : members)
        m.slotValue = -2;
    active = true;
    cursor = -1;
    // NOTE: l'appelant reproduit l'enchaînement d'origine : CountMembers() puis
    // SelectNextMember() (non appelés ici pour rester au plus près du découpage
    // "un membre = une fonction" de l'original).
}

} // namespace ts2::game

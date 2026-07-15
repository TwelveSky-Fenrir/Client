// Game/StatBonusContributors.cpp — implémentation fidèle au désassemblage (voir en-tête .h).
#include "Game/StatBonusContributors.h"
#include <cstring>

namespace ts2::game {

namespace {

// ---------------------------------------------------------------------
// Item_GemStatBonusLookup 0x4C3D90 — table (groupe, clé, valeur) -> bonus plat.
//   groupe 1 : clés 1,2,4,6,8 — plage de valeur -> bonus linéaire (les autres clés = 0).
//   groupe 2 : clé 1 -> paliers de 5 (400/800/1200/1600/2000 sur deux bandes 1-25/26-50) ;
//              clés 4 et 8 -> paliers modulo 5 (200/400/600/800/1000) sur deux bandes
//              disjointes (1-25 / 26-50) ; clés 2 et 6 -> toujours 0.
// Transcription exacte du désassemblage (aucune supposition sur le rôle des clés).
// ---------------------------------------------------------------------
int Item_GemStatBonusLookup(int group, int key, int value) {
    if (group == 1) {
        switch (key) {
            case 1: return (value >= 41 && value <= 60)  ? 100 * (value - 40) : 0;
            case 2: return (value >= 61 && value <= 80)  ? 125 * (value - 60) : 0;
            case 4: return (value >= 1  && value <= 20)  ? 50  * value        : 0;
            case 6: return (value >= 21 && value <= 40)  ? 20  * (value - 20) : 0;
            case 8: return (value >= 81 && value <= 100) ? 50  * (value - 80) : 0;
            default: return 0;
        }
    }
    if (group == 2) {
        switch (key) {
            case 1: {
                if ((value >= 1  && value <= 5)  || (value >= 26 && value <= 30)) return 400;
                if ((value >= 6  && value <= 10) || (value >= 31 && value <= 35)) return 800;
                if ((value >= 11 && value <= 15) || (value >= 36 && value <= 40)) return 1200;
                if ((value >= 16 && value <= 20) || (value >= 41 && value <= 45)) return 1600;
                if ((value >= 21 && value <= 25) || (value >= 46 && value <= 50)) return 2000;
                return 0;
            }
            case 2: return 0;
            case 4: {
                if (value < 1 || value > 25) return 0;
                switch (value % 5) {
                    case 1: return 200;
                    case 2: return 400;
                    case 3: return 600;
                    case 4: return 800;
                    default: return (value % 5) ? 0 : 1000; // reste 0 (multiple de 5)
                }
            }
            case 6: return 0;
            case 8: {
                if (value < 26 || value > 50) return 0;
                switch (value % 5) {
                    case 1: return 200;
                    case 2: return 400;
                    case 3: return 600;
                    case 4: return 800;
                    default: return (value % 5) ? 0 : 1000;
                }
            }
            default: return 0;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// AnchorTbl_FindByKey 0x4C7630 — scan linéaire de GameDatabases::socketT (SOCKET_INFO,
// 20 o/enreg.) cherchant key1 @+0 et key2 @+8. L'original boucle en dur sur 3500 lignes
// (alloc fixe) ; on clampe à socketT.count par sécurité mémoire (comportement identique
// pour toute table réellement chargée, count <= 3031 d'après le chargeur 005_00010.IMG).
// ---------------------------------------------------------------------
const uint8_t* SocketAnchor_FindByKey(const DataTable& socketT, int key1, int key2) {
    if (key1 < 1 || key2 < 1) return nullptr;
    const uint32_t n = (socketT.count < 3500u) ? socketT.count : 3500u;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* rec = socketT.record(i);
        if (!rec) break;
        int32_t k1 = 0, k2 = 0;
        std::memcpy(&k1, rec + 0, 4);
        std::memcpy(&k2, rec + 8, 4);
        if (k1 == key1 && k2 == key2) return rec;
    }
    return nullptr;
}

// Lit le champ +12 (sel==1) ou +16 (sel==2) d'un enregistrement SOCKET_INFO. 0 si absent.
int SocketAnchor_ReadField(const uint8_t* rec, int sel) {
    if (!rec || (sel != 1 && sel != 2)) return 0;
    int32_t v = 0;
    std::memcpy(&v, rec + (sel == 1 ? 12 : 16), 4);
    return v;
}

// ---------------------------------------------------------------------
// SkillTree_GetNodeValue 0x54B830 — sélectionne sel∈{0,1,2} par (category,id,val) via une
// grande table de cas (transcription exacte), puis lit SOCKET_INFO[id,val].champ(sel) via
// AnchorTbl_FindByKey. Simplification FONCTIONNELLEMENT NEUTRE par rapport à l'original :
// l'original fait toujours l'appel AnchorTbl_FindByKey (goto LABEL_220) même quand sel reste
// à 0 (résultat alors forcé à 0 quoi qu'il arrive, car AnchorTbl_FindByKey est sans effet de
// bord) ; on saute directement cet appel inutile quand sel==0 — résultat identique dans tous
// les cas, juste sans le scan de table superflu.
// ---------------------------------------------------------------------
int SkillTree_GetNodeValue(const GameDatabases& db, int category, int id, int val) {
    int sel = 0;
    switch (category) {
        case 1:
            if (id == 1) { if (val == 1 || val == 6 || val == 11) sel = 1; }
            else if (id >= 2 && id <= 8) sel = 1;
            break;
        case 2:
            switch (id) {
                case 1: if (val == 2 || val == 7 || val == 12) sel = 1; break;
                case 9: case 10: case 11: case 12: case 13: case 14: sel = 1; break;
                case 2: sel = 2; break;
                default: break;
            }
            break;
        case 3:
            switch (id) {
                case 1: if (val == 3 || val == 8 || val == 13) sel = 1; break;
                case 15: case 16: case 17: case 18: case 19: sel = 1; break;
                case 3: case 9: sel = 2; break;
                default: break;
            }
            break;
        case 4:
            switch (id) {
                case 1: if (val == 3 || val == 8 || val == 13) sel = 2; break;
                case 20: case 21: case 22: case 23: sel = 1; break;
                case 4: case 10: case 15: sel = 2; break;
                default: break;
            }
            break;
        case 5:
            switch (id) {
                case 1: if (val == 5 || val == 10 || val == 15) sel = 1; break;
                case 24: case 25: case 26: sel = 1; break;
                case 5: case 11: case 16: case 20: sel = 2; break;
                default: break;
            }
            break;
        case 6:
            switch (id) {
                case 1: if (val == 5 || val == 10 || val == 15) sel = 2; break;
                case 27: case 28: sel = 1; break;
                case 6: case 12: case 17: case 21: case 24: sel = 2; break;
                default: break;
            }
            break;
        case 7:
            switch (id) {
                case 1: if (val == 4 || val == 9 || val == 14) sel = 1; break;
                case 29: sel = 1; break;
                case 7: case 13: case 18: case 22: case 25: case 27: sel = 2; break;
                default: break;
            }
            break;
        case 8:
            if (id == 1) { if (val == 4 || val == 9 || val == 14) sel = 2; }
            else if (id == 8 || id == 14 || id == 19 || id == 23 || id == 26 || id == 28 || id == 29) sel = 2;
            break;
        case 11:
            if (id == 1) { if (val == 16 || val == 25) sel = 1; }
            else if (id == 30) sel = 1;
            break;
        case 12:
            if (id == 1) { if (val == 17 || val == 26) sel = 1; }
            else if (id == 31) sel = 1;
            break;
        case 13:
            if (id == 1) { if (val == 18 || val == 27) sel = 1; }
            else if (id == 32) sel = 1;
            break;
        case 14:
            if (id == 1) { if (val == 19 || val == 28) sel = 1; }
            else if (id == 33) sel = 1;
            break;
        case 15:
            if (id == 1) { if (val == 20 || val == 29) sel = 1; }
            else if (id == 34) sel = 1;
            break;
        case 16:
            if (id == 1) { if (val == 21 || val == 30) sel = 1; }
            else if (id == 35) sel = 1;
            break;
        case 17:
            if (id == 1) { if (val == 22 || val == 31) sel = 1; }
            else if (id == 36) sel = 1;
            break;
        case 18:
            if (id == 1) { if (val == 23 || val == 32) sel = 1; }
            else if (id == 37) sel = 1;
            break;
        case 19:
            if (id == 1) { if (val == 24 || val == 33) sel = 1; }
            else if (id == 38) sel = 1;
            break;
        case 20:
            if (id == 39 || id == 43) sel = 1;
            break;
        case 21:
            if (id == 40 || id == 44) sel = 1;
            break;
        case 22:
            if (id == 41 || id == 45) sel = 1;
            break;
        case 23:
            if (id == 42 || id == 46) sel = 1;
            break;
        default:
            break; // category 9,10, <1 ou >23 : sel reste 0 (résultat 0, cf. original).
    }

    if (sel == 0 || id == 0) return 0;
    const uint8_t* rec = SocketAnchor_FindByKey(db.socketT, id, val);
    return SocketAnchor_ReadField(rec, sel);
}

// Bonus de raffinage/gemme "plat" lu sur l'octet2 d'un mot socket (Item_GetAttribByte2
// = catégorie socket float / nb gemmes) : 5 points par palier, +5 si le palier == 25.
inline int gemRefineTerm(uint32_t socketWord) {
    const int cat = Item_GetAttribByte2(socketWord);
    int v = 5 * cat;
    if (cat == 25) v += 5;
    return v;
}

} // namespace

// ---------------------------------------------------------------------
// Item_SumGemStatBonus 0x4C3CC0.
// ---------------------------------------------------------------------
int Item_SumGemStatBonus(int key, uint32_t socketWord) {
    if (socketWord == 0) return 0;
    const int b0 = Item_GetAttribByte0(socketWord);
    const int b1 = Item_GetAttribByte1(socketWord);
    const int b2 = Item_GetAttribByte2(socketWord);
    const int b3 = Item_GetAttribByte3(socketWord);

    int sum = Item_GemStatBonusLookup(1, key, b0);
    sum += Item_GemStatBonusLookup(1, key, b1);
    sum += Item_GemStatBonusLookup(1, key, b2);
    sum += Item_GemStatBonusLookup(2, key, b3);
    return sum;
}

// ---------------------------------------------------------------------
// Char_SumGemStatA/B/C/D 0x54CB00/0x54CB80/0x54CC40/0x54CC90.
// ---------------------------------------------------------------------
int Char_SumGemStatA(const SelfState& s) {
    return gemRefineTerm(s.equip[7].socket) + gemRefineTerm(s.equip[2].socket);
}
int Char_SumGemStatB(const SelfState& s) {
    return gemRefineTerm(s.equip[3].socket) + gemRefineTerm(s.equip[5].socket) + gemRefineTerm(s.equip[1].socket);
}
int Char_SumGemStatC(const SelfState& s) {
    return gemRefineTerm(s.equip[4].socket);
}
int Char_SumGemStatD(const SelfState& s) {
    return gemRefineTerm(s.equip[0].socket);
}

// ---------------------------------------------------------------------
// SkillTree_SumBonuses 0x54B700 — jusqu'à 5 paires (id,valeur) bit-packées sur 3 dwords.
// ---------------------------------------------------------------------
int SkillTree_SumBonuses(int category, uint32_t block0, uint32_t block1, uint32_t block2,
                          const GameDatabases& db) {
    // Octet1 de block0 = nombre de paires actives, char SIGNÉ dans l'original (0/négatif -> 0).
    const int count = static_cast<int>(static_cast<int8_t>((block0 >> 8) & 0xFFu));
    if (count <= 0) return 0;
    const int n = (count < 5) ? count : 5; // borne défensive : seulement 5 paires disponibles
                                            // (l'original n'a pas ce clamp -> lecture hors-tableau
                                            // sur la pile si octet1 > 5, jamais observé en pratique).

    const int ids[5] = {
        static_cast<int8_t>((block0 >> 16) & 0xFFu),
        static_cast<int8_t>( block1        & 0xFFu),
        static_cast<int8_t>((block1 >> 16) & 0xFFu),
        static_cast<int8_t>( block2        & 0xFFu),
        static_cast<int8_t>((block2 >> 16) & 0xFFu),
    };
    const int vals[5] = {
        static_cast<int8_t>((block0 >> 24) & 0xFFu),
        static_cast<int8_t>((block1 >> 8)  & 0xFFu),
        static_cast<int8_t>((block1 >> 24) & 0xFFu),
        static_cast<int8_t>((block2 >> 8)  & 0xFFu),
        static_cast<int8_t>((block2 >> 24) & 0xFFu),
    };

    int sum = 0;
    for (int i = 0; i < n; ++i) {
        if (ids[i] != 0)
            sum += SkillTree_GetNodeValue(db, category, ids[i], vals[i]);
    }
    return sum;
}

} // namespace ts2::game

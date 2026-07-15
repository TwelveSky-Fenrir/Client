// Game/BitPacking.h — utilitaires de manipulation octet-par-octet d'un mot 32 bits
// « packé » (durabilité/attributs d'objet, stats combinées talisman, etc.).
//
// Module UTILITAIRE PUR : aucun état global, fonctions libres, byte-exact au
// désassemblage de TwelveSky2.exe. Toutes les fonctions d'origine partagent le
// même idiome : le mot 32 bits est passé par valeur sur la pile (poussé sur
// 4 octets même quand IDA type l'argument en `char`, car __stdcall aligne
// chaque argument sur un dword), copié via memcpy(4) dans un buffer local,
// modifié octet à octet, puis re-copié dans le dword de retour. Cet idiome
// est reproduit ici par de simples opérations de masquage/décalage,
// numériquement identiques (l'aller-retour memcpy ne fait qu'émuler un accès
// non-aligné, sans incidence sur le résultat). Fonctions `constexpr` (triviales,
// sans état) : voir Game/BitPacking.cpp pour les static_assert de non-régression.
//
// Correspondance fonction <-> adresse d'origine :
//   Bits_AddByte0     0x5456D0   Bits_AddByte1    0x545720   Bits_AddByte2  0x545870
//   Bits_ClearByte0   0x545770   Bits_ClearByte1  0x5457B0   Bits_ClearByte2 0x5457F0
//   Bits_ClearByte3   0x545940
//   Bits_SetByte2     0x545830   Bits_SetByte3    0x545900
//   Bits_PackByte012  0x5458C0
//   Bits_SetByteN     0x54BF30
//   Bits_Unpack8Bytes 0x54C5D0
//   Stat_PackCombined 0x54CEB0   Stat_UnpackCombined 0x54CE40
//
// Cohérence avec Game/ItemSystem.h (Item_GetAttribByte0..3, 0x545610/40/70/A0) :
// CE SONT DES PRIMITIVES SŒURS mais PAS DES DOUBLONS. Item_GetAttribByte0..3
// sont de purs LECTEURS (extraction d'un octet du mot, sans modification,
// immédiatement en amont dans le binaire : 0x545610..0x5456A0). Les fonctions
// de ce fichier (0x5456D0 et suivantes) sont les ÉCRIVAINS de la même famille
// (add/clear/set/pack un octet du mot) — même convention d'encodage, rôle
// disjoint. Aucune duplication : on réutilise ici la même idée d'octet N du
// mot 32 bits, sans réimplémenter les lecteurs (cf. Item_GetAttribByteN).
//
// NB : plusieurs sites d'appel (Net/GameHandlers_InvDispatch.cpp,
// Net/GameHandlers_Misc.cpp, Game/AutoPlaySystem.cpp) contiennent déjà des
// réimplémentations locales ad hoc de certaines de ces primitives (imposées
// par la règle « n'éditer aucun fichier existant »). Elles ont été vérifiées
// bit-exactes avec les fonctions ci-dessous lors de l'écriture de ce module ;
// ce fichier constitue la version canonique/réutilisable pour tout nouveau code.
#pragma once
#include <cstdint>

namespace ts2::game {

// =====================================================================
// Bits_AddByteN — ajoute (modulo 256) un delta signé à l'octet N du mot.
// Le binaire d'origine sign-étend l'octet lu ET le delta avant l'addition
// 32 bits, puis tronque le résultat sur 8 bits (mov [..], al) : le résultat
// tronqué est strictement identique à une addition non signée mod 256,
// quelle que soit l'extension de signe utilisée en amont.
//   Bits_AddByte0 0x5456D0 (octet 0) · Bits_AddByte1 0x545720 (octet 1)
//   Bits_AddByte2 0x545870 (octet 2)
// =====================================================================
constexpr uint32_t Bits_AddByte0(uint32_t packed, int8_t delta) {
    const uint8_t b0 = static_cast<uint8_t>((packed & 0xFFu) + static_cast<uint8_t>(delta));
    return (packed & 0xFFFFFF00u) | b0;
}
constexpr uint32_t Bits_AddByte1(uint32_t packed, int8_t delta) {
    const uint8_t b1 = static_cast<uint8_t>(((packed >> 8) & 0xFFu) + static_cast<uint8_t>(delta));
    return (packed & 0xFFFF00FFu) | (static_cast<uint32_t>(b1) << 8);
}
constexpr uint32_t Bits_AddByte2(uint32_t packed, int8_t delta) {
    const uint8_t b2 = static_cast<uint8_t>(((packed >> 16) & 0xFFu) + static_cast<uint8_t>(delta));
    return (packed & 0xFF00FFFFu) | (static_cast<uint32_t>(b2) << 16);
}

// =====================================================================
// Bits_ClearByteN — met l'octet N du mot à zéro, les 3 autres inchangés.
//   Bits_ClearByte0 0x545770 · Bits_ClearByte1 0x5457B0
//   Bits_ClearByte2 0x5457F0 · Bits_ClearByte3 0x545940
// =====================================================================
constexpr uint32_t Bits_ClearByte0(uint32_t packed) { return packed & 0xFFFFFF00u; }
constexpr uint32_t Bits_ClearByte1(uint32_t packed) { return packed & 0xFFFF00FFu; }
constexpr uint32_t Bits_ClearByte2(uint32_t packed) { return packed & 0xFF00FFFFu; }
constexpr uint32_t Bits_ClearByte3(uint32_t packed) { return packed & 0x00FFFFFFu; }

// =====================================================================
// Bits_SetByteN — écrase l'octet N du mot avec `value` (8 bits), les 3
// autres octets inchangés.
//   Bits_SetByte2 0x545830 (octet 2) · Bits_SetByte3 0x545900 (octet 3)
// =====================================================================
constexpr uint32_t Bits_SetByte2(uint32_t packed, int8_t value) {
    return (packed & 0xFF00FFFFu) | (static_cast<uint32_t>(static_cast<uint8_t>(value)) << 16);
}
constexpr uint32_t Bits_SetByte3(uint32_t packed, int8_t value) {
    return (packed & 0x00FFFFFFu) | (static_cast<uint32_t>(static_cast<uint8_t>(value)) << 24);
}

// =====================================================================
// Bits_PackByte012 0x5458C0 — construit un mot 32 bits à partir de 3 octets
// (octet0=a1, octet1=a2, octet2=a3), octet3 explicitement mis à 0.
// =====================================================================
constexpr uint32_t Bits_PackByte012(int8_t b0, int8_t b1, int8_t b2) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(b0)))
         | (static_cast<uint32_t>(static_cast<uint8_t>(b1)) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b2)) << 16);
    // octet3 = 0 implicitement (bits 24..31 non positionnés).
}

// =====================================================================
// Bits_SetByteN 0x54BF30 — écrase l'octet d'INDICE VARIABLE `byteIndex`
// (0..3) du mot avec `value`.
// ⚠ FIDÉLITÉ : le binaire d'origine n'effectue AUCUNE validation de
// `byteIndex` (écriture directe `[ebp+edx+var_4], al` dans un buffer pile de
// 4 octets) — un index hors [0..3] y corromprait la pile. Reproduire cette
// UB en C++ est impossible/dangereux ; cette implémentation suppose
// byteIndex ∈ [0..3] (contrat respecté par tous les appelants identifiés
// dans le désassemblage) et n'écrit que dans ce cas, sans effet si hors
// bornes (défense minimale, ne change pas le comportement pour les appels
// valides — seule différence avec l'original : pas de corruption silencieuse
// hors bornes).
// =====================================================================
constexpr uint32_t Bits_SetByteN(int32_t byteIndex, int8_t value, uint32_t packed) {
    if (byteIndex < 0 || byteIndex > 3)
        return packed; // hors contrat d'origine (UB pile) — no-op défensif.
    const uint32_t shift = static_cast<uint32_t>(byteIndex) * 8u;
    const uint32_t mask = ~(0xFFu << shift);
    return (packed & mask) | (static_cast<uint32_t>(static_cast<uint8_t>(value)) << shift);
}

// =====================================================================
// Bits_Unpack8Bytes 0x54C5D0 — extrait 8 octets (sign-étendus en int32)
// depuis 3 mots packés :
//   packed1 -> octet2 (out.p1b2), octet3 (out.p1b3)
//   packed2 -> octet0 (out.p2b0), octet1 (out.p2b1), octet2 (out.p2b2), octet3 (out.p2b3)
//   packed3 -> octet0 (out.p3b0), octet1 (out.p3b1)
// (2 + 4 + 2 = 8 octets, d'où le nom d'origine). Chaque sortie reproduit le
// `movsx` du binaire : l'octet est interprété comme int8_t puis étendu en
// int32_t (et non simplement zéro-étendu).
// =====================================================================
struct Bits_Unpack8BytesResult {
    int32_t p1b2 = 0, p1b3 = 0;
    int32_t p2b0 = 0, p2b1 = 0, p2b2 = 0, p2b3 = 0;
    int32_t p3b0 = 0, p3b1 = 0;
};

constexpr Bits_Unpack8BytesResult Bits_Unpack8Bytes(uint32_t packed1, uint32_t packed2, uint32_t packed3) {
    Bits_Unpack8BytesResult r;
    r.p1b2 = static_cast<int8_t>((packed1 >> 16) & 0xFFu);
    r.p1b3 = static_cast<int8_t>((packed1 >> 24) & 0xFFu);
    r.p2b0 = static_cast<int8_t>(packed2 & 0xFFu);
    r.p2b1 = static_cast<int8_t>((packed2 >> 8) & 0xFFu);
    r.p2b2 = static_cast<int8_t>((packed2 >> 16) & 0xFFu);
    r.p2b3 = static_cast<int8_t>((packed2 >> 24) & 0xFFu);
    r.p3b0 = static_cast<int8_t>(packed3 & 0xFFu);
    r.p3b1 = static_cast<int8_t>((packed3 >> 8) & 0xFFu);
    return r;
}

// =====================================================================
// Stat_PackCombined 0x54CEB0 — encode (hi, lo) en un entier combiné
// hi*1000000 + lo, utilisé pour les stats/talismans « valeur combinée ».
//   hi (a1) doit être dans [0, 100]      (sinon échec)
//   lo (a2) doit être dans [0, 100000]   (sinon échec)
// Retourne true et écrit *out = lo + 1000000*hi en cas de succès ; sinon
// écrit *out = 0 et retourne false (le binaire d'origine écrit aussi 0 dans
// ce cas — cf. disasm 0x54ced2/0x54ced5).
// =====================================================================
constexpr bool Stat_PackCombined(int32_t hi, int32_t lo, int32_t& out) {
    if (hi < 0 || hi > 100 || lo < 0 || lo > 100000) {
        out = 0;
        return false;
    }
    out = lo + 1000000 * hi;
    return true;
}

// =====================================================================
// Stat_UnpackCombined 0x54CE40 — inverse de Stat_PackCombined : découpe une
// valeur combinée `v` en (hi, lo) = (v/1000000, v%1000000) [division/reste
// SIGNÉS, idiv], puis plafonne : hi>100 -> 0, lo>100000 -> 0.
// Si v<0, hi=lo=0 directement (pas de division).
// =====================================================================
constexpr void Stat_UnpackCombined(int32_t v, int32_t& hi, int32_t& lo) {
    if (v < 0) {
        hi = 0;
        lo = 0;
        return;
    }
    hi = v / 1000000;
    lo = v % 1000000;
    if (hi > 100)
        hi = 0;
    if (lo > 100000)
        lo = 0;
}

} // namespace ts2::game

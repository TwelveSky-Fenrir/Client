// Net/Framing.h — construction (C->S) et lecture (S->C) des trames réseau TS2.
// Réécriture fidèle du framing observé dans le désassemblage :
//   - builders sortants : Net_SendPacket_Op12 0x4B43C0, Net_SendOp110 0x4B6F80
//   - boucle entrante   : Net_RecvDispatch 0x463040
//   - table des tailles  : dword_846808 (remplie par Net_InitPacketHandlers 0x463270)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Net/Rng.h"
#include "Asset/ByteReader.h"

namespace ts2::net {

// --- En-tête sortant (9 octets) --------------------------------------------
// Disposition EXACTE relevée dans les builders :
//   [0..3]  nonce1        u32 LE = (rng%10000) * (rng%10000)
//   [4..6]  nonce2_lo     3 octets LE de nonce2 = (rng%10000) * (rng%10000)
//   [7]     seq           u8  (byte_8156A5) — écrase le 4e octet de nonce2
//   [8]     opcode        u8
//   [9..]   payload
// Puis XOR mono-octet (byte_8156A4) sur TOUT le paquet, en-tête compris.
inline constexpr size_t kOutHeaderSize   = 9;
inline constexpr size_t kOffNonce1       = 0;
inline constexpr size_t kOffNonce2       = 4;
inline constexpr size_t kOffSeq          = 7;
inline constexpr size_t kOffOpcode       = 8;

// Borne des deux tirages composant chaque nonce (Rng_Next() % 10000).
inline constexpr int kNonceMod = 10000;

// Taille du buffer de réception côté client (this+32, 200000 o) : voir
// Net/PacketDispatch.h::kRecvBufferSize (seule définition — celle-ci était un
// doublon inutilisé, jamais référencé dans ce fichier, source d'un conflit de
// redéfinition dès qu'un même .cpp inclut les deux headers).

// ---------------------------------------------------------------------------
// PacketWriter — assemble un paquet sortant : réserve les 9 octets d'en-tête,
// écrit le payload via les helpers, puis Finalize() pose l'en-tête et chiffre.
//
// Piège de promotion « thiscall » : dans le binaire tout argument 'char' d'un
// builder est recopié sur 4 octets (Crt_Memcpy(this+off, &arg, 4)) — le serveur
// lit donc un int32. Utiliser WriteChar4LE() pour reproduire ce comportement.
// ---------------------------------------------------------------------------
class PacketWriter {
public:
    PacketWriter();

    // --- Helpers payload (little-endian, comme les Crt_Memcpy du binaire) ---
    void WriteU8(uint8_t v);
    void WriteU16(uint16_t v);
    void WriteU32(uint32_t v);
    void WriteI32(int32_t v);
    void WriteFloat(float v);

    // 'char' sortant émis sur 4 octets LE : extension de signe char->int32
    // (char signé sous MSVC) puis 4 octets. cf. Net_SendOp110 0x4B6F80.
    void WriteChar4LE(int8_t v);

    // Copie brute de 'n' octets (memcpy de structs/tableaux du payload).
    void WriteBytes(const void* src, size_t n);

    // Pose l'en-tête [nonce1][nonce2_lo:3][seq][opcode], chiffre XOR tout le
    // buffer avec 'xorKey' (byte_8156A4), puis incrémente 'seq' (byte_8156A5).
    // Ordre des tirages RNG (4 appels) : d1,d2 -> nonce1 ; d3,d4 -> nonce2.
    //
    // NB : dans le binaire ++byte_8156A5 survient après un send() réussi ; on
    // le couple ici à Finalize() (le NetClient envoie juste après et ferme la
    // socket en cas d'échec, rendant tout désync de séquence sans objet). Le
    // NetClient ne doit donc PAS ré-incrémenter la séquence de son côté.
    void Finalize(uint8_t opcode, Rng& rng, uint8_t xorKey, uint8_t& seq);

    // Accès au paquet finalisé (à passer à send()).
    const uint8_t*              Data()   const { return buf_.data(); }
    size_t                      Size()   const { return buf_.size(); }
    const std::vector<uint8_t>& Buffer() const { return buf_; }

    // Taille du payload seul (hors en-tête de 9 octets).
    size_t PayloadSize() const { return buf_.size() - kOutHeaderSize; }

private:
    void Append(const void* p, size_t n);
    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Trame entrante décodée — pointe DANS le buffer de réception (aucune copie).
// ---------------------------------------------------------------------------
struct InboundFrame {
    uint8_t        opcode     = 0;        // buf[0]
    const uint8_t* payload    = nullptr;  // début du payload
    uint32_t       payloadLen = 0;        // taille du payload
    uint32_t       frameLen   = 0;        // octets totaux consommés du buffer
};

// ---------------------------------------------------------------------------
// PacketReader — décode les trames S->C. Le flux entrant n'est PAS chiffré :
// Net_RecvDispatch 0x463040 lit l'opcode brut. Framing : [opcode:u8][payload].
// La taille est fixe par opcode via la table dword_846808 (256 u32, remplie au
// runtime par Net_InitPacketHandlers 0x463270 ; sizeTable[op] inclut l'octet
// d'opcode, donc payload = sizeTable[op] - 1). Exception : opcode 0x63 (99)
// variable = [opcode:u8][len:u32][payload:len], consommant len+5 octets.
// ---------------------------------------------------------------------------
class PacketReader {
public:
    static constexpr uint8_t kVariableOpcode = 0x63; // 99

    explicit PacketReader(const uint32_t* sizeTable) : sizeTable_(sizeTable) {}

    // Tente de décoder une trame depuis 'data' (avail octets disponibles).
    // Renvoie true et remplit 'out' si une trame complète est présente ; false
    // si le buffer est incomplet (attendre plus de recv()) ou si l'opcode n'est
    // pas mappé (taille 0 — handler nul dans le binaire, flux invalide).
    bool TryParse(const uint8_t* data, size_t avail, InboundFrame& out) const;

    // Curseur de lecture borné sur le payload d'une trame décodée (LE).
    static ts2::asset::ByteReader Payload(const InboundFrame& f) {
        return ts2::asset::ByteReader(f.payload, f.payloadLen);
    }

private:
    const uint32_t* sizeTable_;
};

} // namespace ts2::net

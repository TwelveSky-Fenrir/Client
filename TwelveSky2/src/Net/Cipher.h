// Net/Cipher.h — chiffrement réseau TwelveSky2 : XOR mono-octet (clé + séquence).
//
// Vérité : désassemblage de Net_ConnectLoginServer 0x462870, Net_ConnectGameServer
// 0x462A70 et des 234 builders Net_Send* (boucle « for j: buf[j] ^= v18 »).
//
// Deux octets sont négociés au handshake et vivent dans l'objet réseau
// g_NetClient (0x8156A0) :
//   - key : byte_8156A4 (g_NetClient+4) — clé XOR appliquée à TOUT le buffer.
//   - seq : byte_8156A5 (g_NetClient+5) — compteur de séquence, estampillé à
//           l'offset 7 de chaque paquet sortant puis post-incrémenté (mod 256).
//
// IMPORTANT (fidélité) : le chiffrement est UNIDIRECTIONNEL (sortant C->S).
// Le flux entrant S->C est consommé EN CLAIR par Net_RecvDispatch 0x463040
// (aucun XOR n'est appliqué en réception ni sur les bannières de handshake).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::net {

class Cipher {
public:
    Cipher() = default;
    Cipher(uint8_t key, uint8_t seq) : key_(key), seq_(seq) {}

    // Accès direct aux deux octets négociés (byte_8156A4 / byte_8156A5).
    uint8_t Key() const { return key_; }
    uint8_t Seq() const { return seq_; }
    void    SetKey(uint8_t k) { key_ = k; }
    void    SetSeq(uint8_t s) { seq_ = s; }

    // XOR mono-octet sans état (helper direct sur une clé). Employé quand la clé
    // vit ailleurs (p. ex. NetClient::xorKey pendant le handshake de Login.cpp).
    static void Xor(void* data, size_t len, uint8_t key) {
        uint8_t* p = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < len; ++i)
            p[i] = static_cast<uint8_t>(p[i] ^ key);
    }

    // Handshake : la clé est le 2e octet de la bannière serveur (buffer[1], soit
    // g_NetClient+33), la séquence initiale = clé + 127 (cf. *(this+5) += 127).
    void NegotiateFromBanner(uint8_t bannerKeyByte) {
        key_ = bannerKeyByte;
        seq_ = static_cast<uint8_t>(bannerKeyByte + 127);
    }

    // Estampille de séquence sortante : renvoie la valeur courante puis
    // incrémente (« buf[7] = seq++ » ; enveloppe mod 256). Utilisée par les
    // builders Net_Send* après un envoi réussi.
    uint8_t NextSeq() { return seq_++; }

    // XOR mono-octet sur 'len' octets. Symétrique (chiffrer == déchiffrer) ;
    // n'est utilisé que sur les buffers SORTANTS.
    void Apply(void* data, size_t len) const {
        uint8_t* p = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < len; ++i)
            p[i] = static_cast<uint8_t>(p[i] ^ key_);
    }
    void Encrypt(void* data, size_t len) const { Apply(data, len); }
    void Decrypt(void* data, size_t len) const { Apply(data, len); }

private:
    uint8_t key_ = 0;  // byte_8156A4
    uint8_t seq_ = 0;  // byte_8156A5
};

} // namespace ts2::net

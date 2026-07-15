// Net/Rng.cpp — instance globale du PRNG réseau (voir Rng.h).
#include "Net/Rng.h"

namespace ts2::net {

Rng& DefaultRng() {
    // Un seul état, comme le _holdrand de la CRT côté client : les envois
    // réseau (Net_Send*) se font tous sur le même thread, ils partagent donc
    // une unique séquence rand().
    static Rng g_rng;
    return g_rng;
}

} // namespace ts2::net

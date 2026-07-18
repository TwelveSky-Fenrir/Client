// Net/Rng.cpp — global instance of the network PRNG (see Rng.h).
#include "Net/Rng.h"

namespace ts2::net {

Rng& DefaultRng() {
    // Single state, like the client-side CRT's _holdrand: network sends
    // (Net_Send*) all happen on the same thread, so they share a single
    // rand() sequence.
    static Rng g_rng;
    return g_rng;
}

} // namespace ts2::net

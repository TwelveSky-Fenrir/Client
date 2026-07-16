// Game/MapWarp.cpp — implémentation du système de warp de faction (voir MapWarp.h).
//
// Traduction fidèle des 4 fonctions désassemblées (EA en commentaire de chaque bloc).
// Rappel de périmètre : ce module pose l'INTENTION de warp (guardes + globals de mise en
// scène) sans effectuer l'envoi réseau ni le chargement de carte réels (TODO cités).
#include "Game/MapWarp.h"
#include "Net/SendPackets.h"   // tire Net/NetClient.h + Net/Rng.h (DefaultRng, Net_SendWarpRequest)
#include <cstring>

namespace ts2::game {
namespace {

// Lecture LE d'un int32 non aligné (même motif que Game/EntityManager.cpp RdI32 local).
int32_t Rd32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

// *(g_LocalPlayerSheet + 1784) — état d'action du joueur local, en réalité
// players[0]+244 (cf. dérivation en tête de MapWarp.h). g_World.Self() crée le slot 0
// s'il n'existe pas encore (comportement de GameState.h, pas modifié ici).
int32_t SelfActionState() {
    PlayerEntity& self = g_World.Self();
    if (kSelfActionStateOffset + 4 > self.body.size()) return 0;
    return Rd32(self.body.data() + kSelfActionStateOffset);
}

// flt_1675AC4 = (float)(Rng_Next() % 360) — Rng_Next 0x7603FD (net::DefaultRng, MÊME flux
// rand() que les nonces des builders : 1 tirage facing PUIS 4 tirages nonces par warp, cf.
// ordre partagé dans ComboPickupTick.cpp:120-124). Sites : 0x55c64e/0x55c85e/0x55cba8 +
// 0x5f5daf (Warp_SendTeleport). L'angle de façade est cosmétique mais l'ORDRE des tirages
// doit rester fidèle (le facing consomme un tirage avant les nonces du paquet émis ensuite).
float RandomFacingDeg() {
    return static_cast<float>(ts2::net::DefaultRng().NextMod(360));
}

// Bloc commun d'armement du warp complet (dword_1675A8C.. flt_1675AC8), fidèle aux 3
// fonctions qui l'exécutent (Town/Default/Ex : mêmes 10 écritures ; Map37 : même forme
// avec des coordonnées fixes, cf. BeginWarpToMap37). Correspond à :
//   Crt_Memset(&dword_1675AA0, 0, 72); dword_1675AA0=0; dword_1675AA4=flagA4;
//   flt_1675AA8=0.0; flt_1675AAC/AB0/AB4=x/y/z; flt_1675AC4=flt_1675AC8=rand()%360;
// (le memset de 72 o est équivalent ici à "tout Var() non explicitement réécrit reste à sa
// valeur par défaut 0" — g_Client.Var() renvoie 0 pour une clé jamais posée.)
void ArmFullWarp(FactionWarpResolution& r, int32_t warpModeCode, int32_t flagA4,
                  int32_t townNpcId, float x, float y, float z,
                  ts2::net::NetClient* nc = nullptr) {
    g_Client.Var(WarpAddr::MorphInProgress) = 1;          // /*g_MorphInProgress = 1*/
    g_Client.Var(WarpAddr::WarpModeCode)    = warpModeCode;
    g_Client.Var(WarpAddr::WarpSub)         = 0;
    g_Client.Var(WarpAddr::WarpTargetNpc)   = townNpcId;
    // M8 — g_TargetZoneId = townNpcId (0x1675A9C) : EA 0x55c69a/0x55c5ee (non-Ex),
    // homologues Default/Map37/Ex et 0x5f5d46 (teleport). WarpTargetNpc ci-dessus EST déjà
    // g_TargetZoneId (cf. MapWarp.h WarpAddr::WarpTargetNpc == 0x1675A9C) ; on en pose ici le
    // MIROIR consommé par SceneManager pour recharger la BONNE zone (sans lui le rechargement
    // retombe sur la zone courante). GameState.h:545 documente pendingWarpZoneId == 0x1675A9C.
    g_World.pendingWarpZoneId = townNpcId;                 // g_TargetZoneId 0x1675A9C
    g_Client.Var(WarpAddr::WarpFlagA0)      = 0;
    g_Client.Var(WarpAddr::WarpFlagA4)      = flagA4;
    g_Client.VarF(WarpAddr::WarpDelay)      = 0.0f;
    g_Client.VarF(WarpAddr::WarpPosX)       = x;
    g_Client.VarF(WarpAddr::WarpPosY)       = y;
    g_Client.VarF(WarpAddr::WarpPosZ)       = z;
    const float facing = RandomFacingDeg();
    g_Client.VarF(WarpAddr::WarpFacingA)    = facing;
    g_Client.VarF(WarpAddr::WarpFacingB)    = facing;

    r.action       = WarpAction::ArmFullWarp;
    r.warpModeCode = warpModeCode;
    r.facingDeg    = facing;
    r.townNpcId    = townNpcId;
    r.x = x; r.y = y; r.z = z;

    // Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C, v4) — INCONDITIONNEL dans le binaire :
    //   EA 0x55C66F / 0x55C87F / 0x55C993 / 0x55CBC9 (Town/Default/Map37/Ex) et 0x5F5DD6
    //   (Warp_SendTeleport). Émis via l'alias i32 Net_SendWarpRequest : townNpcId
    //   (140/138/139/165/166) >= 128 est ZÉRO-étendu sur 32 bits (cf. Net/SendPackets.cpp — le
    //   builder int8_t partagé sign-étendrait à tort). H1 : le param `nc` valant nullptr chez TOUS
    //   les appelants actuels, on émet via le singleton global net::g_NetClient 0x8156A0
    //   (GlobalNetClient()) — c'est EXACTEMENT ce que fait le binaire (Op20 adresse g_NetClient
    //   directement). Le garde `if (client)` reste une sécurité de réécriture (pointeur null tant
    //   qu'aucune connexion n'est amorcée ; un warp n'arrive qu'en jeu, post-handshake) — pas un
    //   écart de fidélité. Les 4 tirages de nonces d'Op20 suivent le tirage de facing ci-dessus :
    //   l'émission désormais RÉELLE rétablit le flux RNG fidèle (auparavant ces 4 tirages étaient
    //   sautés -> désync vs binaire).
    ts2::net::NetClient* client = nc ? nc : ts2::net::GlobalNetClient();
    if (client) ts2::net::Net_SendWarpRequest(*client, warpModeCode, townNpcId);
    // TODO(monde) au retour serveur (op 0x0d ZoneChangeInfo, cf. World/WorldMap.*/World_LoadMap
    //   déjà écrit ailleurs) : charger effectivement la carte cible et positionner l'acteur à
    //   (x,y,z)/facing ci-dessus, puis remettre g_MorphInProgress à 0 (observé côté
    //   Net_OnGameServerConnectResult, codes d'erreur 1..12).
}

// Op99 (0x63) — Net_SendOp99/Net_SendAutoHuntSync 0x4BD140. Le binaire lit les blobs en GLOBAL
// (Crt_Memcpy(this+13, byte_16755B0, 0x44) @0x4bd1f5 ; Crt_Memcpy(this+81, &g_AutoHuntMode 0x16755F4,
// 0x2C) @0x4bd20b) — ce ne sont PAS des paramètres. Dans l'état modélisé actuel ces blobs sont
// INTÉGRALEMENT à ZÉRO : byte_16755B0 est BSS jamais écrit (xrefs_to 0x16755B0 = 1, sa seule
// lecture 0x4bd1e9) et la grille quick-skills 0x16755B4 (64 o) comme la config auto-hunt 0x16755F4
// (44 o) n'ont AUCUN writer en ClientSource (grep 0 hit). Ce qui importe pour la fidélité protocole
// est INDÉPENDANT du contenu des blobs : paquet présent, seq++ (@0x4bd2cf) et 4 tirages RNG
// (@0x4bd157..18b) — une désync de séquence corromprait TOUTE la session. Émission via le
// singleton (le binaire adresse g_NetClient directement), même motif qu'ArmFullWarp.
// TODO(state) ancré : brancher les vrais blobs (byte_16755B0 68 o + g_AutoHuntMode 0x16755F4 44 o,
//   possédés par AutoPlaySystem, hors périmètre) quand la config auto-hunt sera modélisée.
void EmitAutoHuntSync(ts2::net::NetClient* nc) {
    static const uint8_t kBlob68[68] = {0}; // byte_16755B0 (this+13, 68 o) — zéro prouvé/modélisé
    static const uint8_t kBlob44[44] = {0}; // g_AutoHuntMode 0x16755F4 (this+81, 44 o) — zéro modélisé
    if (auto* c = nc ? nc : ts2::net::GlobalNetClient())
        ts2::net::Net_SendAutoHuntSync(*c, 0, kBlob68, kBlob44); // a2=0 (@0x55ca9c/0x55cad9/0x55cb21)
}

// Résout x/y/z via le resolver optionnel (sinon 0/0/0, cf. IFactionTownCoordResolver).
void ResolveCoords(int32_t element, int32_t townNpcId, const IFactionTownCoordResolver* resolver,
                    FactionWarpResolution& r) {
    if (resolver && resolver->ResolveTownCoords(element, townNpcId, r.x, r.y, r.z)) {
        r.coordsResolved = true;
    }
    // TODO(asset) sans resolver : Motion_GetComboOffsetTable 0x5025E0(element, townNpcId) puis,
    //   si échec, GInfo2_GetVec3 0x4FD4C0(flt_1555D08, townNpcId) — tables NPC/motion (.IMG),
    //   hors périmètre de ce module (cf. IFactionTownCoordResolver dans MapWarp.h).
}

} // namespace

// ---------------------------------------------------------------------------
// Map_BeginWarpToFactionTown 0x55C510 / Map_BeginWarpToFactionTownEx 0x55C9A0.
// ---------------------------------------------------------------------------
FactionWarpResolution BeginWarpToFactionTown(int32_t element, bool ex, int32_t mode,
                                              const IFactionTownCoordResolver* resolver,
                                              ts2::net::NetClient* nc) {
    FactionWarpResolution r{};
    r.element   = element;
    r.townNpcId = FactionTownNpcId(element);
    if (r.townNpcId == 0) {
        // switch(g_LocalElement) default -> v4 reste 0 -> aucune ville, abandon silencieux.
        r.valid = false;
        return r;
    }
    r.valid = true;

    const bool    selfDead         = (SelfActionState() == kDeathRespawnState);
    const int32_t currentTownNpcId = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));

    if (!ex) {
        // 0x55C529 : if (*(this+1784) != 12 || a2) { ... } else return;
        //   -> bloqué SEULEMENT si mort ET mode non forcé (mode==0).
        if (selfDead && mode == 0) {
            r.blockedByDeath = true;
            return r;
        }
        // 0x55C581 : if (v4 != g_SelfMorphNpcId) { ... } — sinon no-op (déjà sur place).
        if (r.townNpcId == currentTownNpcId) {
            r.action = WarpAction::None; // déjà sur place : le binaire ne fait RIEN ici
            return r;
        }
        ResolveCoords(element, r.townNpcId, resolver, r);
        // 0x55C5BD : if (!g_MorphInProgress) { arme + envoie } sinon no-op.
        if (g_Client.VarGet(WarpAddr::MorphInProgress) != 0) {
            r.blockedByMorphInProgress = true;
            return r;
        }
        // 0x55C5C7 : a2 (mode) != 0 -> code 3 / flagA4=0 ; a2==0 -> code 7 / flagA4=1.
        const int32_t warpModeCode = (mode != 0) ? 3 : 7;
        const int32_t flagA4       = (mode != 0) ? 0 : 1;
        ArmFullWarp(r, warpModeCode, flagA4, r.townNpcId, r.x, r.y, r.z, nc);
        return r;
    }

    // --- Ex (0x55C9A0) ---------------------------------------------------
    // 0x55C9BA..0x55C9E1 : garde par mode.
    //   mode==0 : bloqué si mort (state==12).
    //   mode==1 : bloqué SAUF si mort (état "mort" requis — c'est l'appel fait par
    //             Char_TickDeathRespawn 0x577AEE lors de la mort effective du joueur).
    //   autre mode (2, 3, ...) : aucune garde ici (comportement d'origine, jamais observé
    //   sur un site d'appel réel — les 6 xrefs connues n'utilisent que 0 ou 1).
    if (mode == 0) {
        if (selfDead) { r.blockedByDeath = true; return r; }
    } else if (mode == 1) {
        if (!selfDead) { r.blockedByDeath = true; return r; }
    }

    // 0x55CA41 : la résolution de coordonnées a lieu ICI, inconditionnellement (contraste
    // avec la variante non-Ex où elle est repoussée après le test "déjà sur place").
    ResolveCoords(element, r.townNpcId, resolver, r);

    if (r.townNpcId == currentTownNpcId) {
        // 0x55CA65 : déjà sur la bonne ville/morph.
        if (mode == 0 || mode == 1) {
            // 0x55CA82 (mode0) / 0x55CAA7 (mode1) : pas de warp de carte, juste un
            // repositionnement local + confirmation réseau.
            r.action = WarpAction::MoveInPlace;
            g_Client.Var(WarpAddr::InvDirtyEnable) = 0; // g_InvDirtyEnable=0  EA 0x55CA87 (mode0) / 0x55CAC3 (mode1)
            // TODO(net) mode0 : Net_QueueMoveTo(&g_PlayerCmdController, {x,y,z}, 0,-1,0,0,0,0)
            //   EA 0x55CA82 (Net_QueueMoveTo 0x5119B0) ; mode1 : Net_QueueRespawnMove(...)
            //   EA 0x55CABE (Net_QueueRespawnMove 0x5117A0) -> émetteurs op1/op0 (via Op15) du
            //   g_PlayerCmdController : module NON implémenté / NON possédé par ce front.
            // Op99 émis APRÈS g_InvDirtyEnable=0 — EA 0x55CA9C (mode0) / 0x55CAD9 (mode1). Les deux
            //   branches émettent un Op99 IDENTIQUE (Net_SendOp99(&g_AutoPlayMgr, 0)) -> un seul
            //   appel ici couvre le mode réellement pris. Cf. EmitAutoHuntSync (blobs à zéro prouvés).
            EmitAutoHuntSync(nc);
        } else {
            r.action = WarpAction::None; // mode inconnu : le binaire ne fait rien non plus
        }
        return r;
    }

    // 0x55CAF9..0x55CB06 : g_MorphInProgress != 1 && !g_GmCmdCooldownLatch && !g_MorphInProgress
    // (double test équivalent à !g_MorphInProgress tant que ce flag ne vaut que 0/1, cf.
    // commentaire dans MapWarp.h) — bloqué si un morph est déjà armé OU si le verrou de
    // cooldown est posé (verrou anti-renvoi générique, réutilisé ailleurs par des commandes
    // d'administration hors périmètre — non pertinent ici, simple valeur de garde lue).
    const bool morphInProgress = g_Client.VarGet(WarpAddr::MorphInProgress) != 0;
    const bool cooldownLatched = g_Client.VarGet(WarpAddr::CooldownLatch) != 0;
    if (morphInProgress || cooldownLatched) {
        r.blockedByMorphInProgress = morphInProgress;
        r.blockedByCooldown        = cooldownLatched;
        return r;
    }

    g_Client.Var(WarpAddr::InvDirtyEnable) = 0; // g_InvDirtyEnable=0  EA 0x55CB0C
    // Op99 émis AVANT l'Op20 d'ArmFullWarp — EA 0x55CB21. L'ordre binaire (Op99 PUIS facing PUIS
    //   Op20) est ainsi restitué mécaniquement : Op99 tire 4 nonces (seq++), ArmFullWarp tire
    //   ensuite le facing (1 tirage) puis Op20 (4 nonces, seq++). Cf. EmitAutoHuntSync ci-dessus.
    EmitAutoHuntSync(nc);
    ArmFullWarp(r, /*warpModeCode=*/7, /*flagA4=*/1, r.townNpcId, r.x, r.y, r.z, nc); // 0x55CB26..0x55CBC9
    return r;
}

// ---------------------------------------------------------------------------
// Map_BeginWarpToFactionTownDefault 0x55C740 — identique à la branche non-Ex ci-dessus,
// SANS la garde "mort" (aucun `this`/état lu en tête de fonction) et toujours en mode
// "normal" (code 7, flagA4=1 — mêmes constantes que le a2==0 de la variante non-Ex).
// ---------------------------------------------------------------------------
FactionWarpResolution BeginWarpToFactionTownDefault(int32_t element,
                                                     const IFactionTownCoordResolver* resolver,
                                                     ts2::net::NetClient* nc) {
    FactionWarpResolution r{};
    r.element   = element;
    r.townNpcId = FactionTownNpcId(element);
    if (r.townNpcId == 0) { // 0x55C761 default -> v2 reste 0
        r.valid = false;
        return r;
    }
    r.valid = true;

    const int32_t currentTownNpcId = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));
    if (r.townNpcId == currentTownNpcId) { // 0x55C799
        r.action = WarpAction::None;
        return r;
    }
    ResolveCoords(element, r.townNpcId, resolver, r); // 0x55C7B4/0x55C7CA

    if (g_Client.VarGet(WarpAddr::MorphInProgress) != 0) { // 0x55C7D6
        r.blockedByMorphInProgress = true;
        return r;
    }
    ArmFullWarp(r, /*warpModeCode=*/7, /*flagA4=*/1, r.townNpcId, r.x, r.y, r.z, nc); // 0x55C7E6..0x55C87F
    return r;
}

// ---------------------------------------------------------------------------
// Map_BeginWarpToMap37 0x55C8A0 — cible fixe (id 37), coordonnées EXACTES codées en dur.
// ---------------------------------------------------------------------------
FactionWarpResolution BeginWarpToMap37(ts2::net::NetClient* nc) {
    FactionWarpResolution r{};
    r.element   = -1;   // sans objet : cette variante ne dépend pas de g_LocalElement
    r.townNpcId = 37;
    r.valid     = true;

    const bool    selfDead         = (SelfActionState() == kDeathRespawnState); // *(this+1784)!=12
    const int32_t currentTownNpcId = static_cast<int32_t>(g_Client.VarGet(WarpAddr::SelfMorphNpcId));

    // 0x55C8EA : *(this+1784)!=12 && g_SelfMorphNpcId!=37 && !g_MorphInProgress.
    if (selfDead) { r.blockedByDeath = true; return r; }
    if (currentTownNpcId == 37) { r.action = WarpAction::None; return r; } // déjà là
    if (g_Client.VarGet(WarpAddr::MorphInProgress) != 0) {
        r.blockedByMorphInProgress = true;
        return r;
    }

    // Coordonnées codées en dur dans le binaire — AUCUN résolveur nécessaire ici.
    r.coordsResolved = true;
    ArmFullWarp(r, /*warpModeCode=*/11, /*flagA4=*/1, /*townNpcId=*/37,
                /*x=*/6.0f, /*y=*/97.0f, /*z=*/-3259.0f, nc); // 0x55C8FA..0x55C993
    return r;
}

// ---------------------------------------------------------------------------
// Warp_SendTeleport 0x5F5CE0 — __stdcall(u16 a1, float* a2). Corps = strictement
// ArmFullWarp(mode=6, flagA4=1, townNpcId=v3[a1], x/y/z=a2[0..2]) gardé par
// (a1<=3 && !g_MorphInProgress), suivi de Op20(nc, 6, v3[a1]) (EA 0x5f5dd6).
// ---------------------------------------------------------------------------
bool Warp_SendTeleport(uint16_t zoneSel, const float* pos, ts2::net::NetClient* nc) {
    // v3[4] = {138,139,165,166} — EA 0x5f5ce9 / f0 / f7 / fe.
    static constexpr int32_t kTeleportZoneIds[4] = {138, 139, 165, 166};
    // 0x5f5d1a : if (a1 <= 3u && !g_MorphInProgress) — sinon renvoie a1 sans rien faire.
    if (zoneSel > 3u || g_Client.VarGet(WarpAddr::MorphInProgress) != 0) return false;

    FactionWarpResolution r{};
    r.element        = -1;                        // sans objet : téléportation par zone directe
    r.townNpcId      = kTeleportZoneIds[zoneSel]; // g_TargetZoneId = v3[a1]  EA 0x5f5d46
    r.valid          = true;
    r.coordsResolved = true;
    r.x = pos[0]; r.y = pos[1]; r.z = pos[2];     // flt_1675AAC/AB0/AB4  EA 0x5f5d7e/8a/96
    // ArmFullWarp reproduit 0x5f5d20..0x5f5dbb (MorphInProgress=1, mode=6, sub=0, memset72,
    // flagA0=0, flagA4=1, delay=0, x/y/z, facing=Rng%360 x2) + Op20(nc,6,zone) EA 0x5f5dd6.
    ArmFullWarp(r, /*warpModeCode=*/6, /*flagA4=*/1, r.townNpcId, r.x, r.y, r.z, nc); // 0x5f5d20..0x5f5dd6
    return true;
}

} // namespace ts2::game

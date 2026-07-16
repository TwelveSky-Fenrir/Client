// World/TerrainPicker.cpp — picking écran -> monde (cf. bandeau de World/TerrainPicker.h
// pour la carte complète des ancres IDA et les preuves).

#include "World/TerrainPicker.h"

// ⚠ ORDRE D'INCLUSION LOAD-BEARING (convention Winsock du projet : <winsock2.h> AVANT
// <windows.h>). Game/AutoTargetCombatGate.h tire transitivement Game/ComboPickupTick.h ->
// Net/NetClient.h, qui fait `#include <winsock2.h>` PUIS `#include <windows.h>` (NetClient.h
// :19-20). Gfx/Camera.h, lui, inclut <windows.h> nu (Camera.h:29). Ce bloc DOIT donc rester
// AVANT l'include de Gfx/Camera.h, sinon windows.h serait vu en premier et les redéfinitions
// Winsock casseraient la compilation.
#include "Game/AutoTargetCombatGate.h" // Combat_IsTargetablePlayerState/MonsterState,
                                       // AutoTarget_PlayerRecordPopulated/MonsterActionState

#include "Gfx/Camera.h"          // gfx::Camera + d3dx9 (BuildScreenPickCamera)
#include "Game/GameDatabase.h"   // game::MonsterInfo (def+232 / +248..+260)
#include "Game/ExtraDatabases.h" // game::NpcDefRecord (def+1328..+1336)

#include <cmath>
#include <cstring>

namespace ts2::world {
namespace {

// Math_Dist3D 0x53FAA0 — distance euclidienne 3D pleine entre deux vec3.
float Dist3D(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Cam_ScreenRayVsAABB 0x6A0670 — construit le rayon écran depuis les paramètres caméra puis
// teste le segment contre l'AABB. PROUVÉ strictement équivalent à la composition ci-dessous
// (cf. §0 du bandeau .h) : @0x6A0682..0x6A0725 == collision::BuildScreenRay (formule et
// offsets identiques), @0x6A0746 == Collide_SegmentAABB 0x69FB20 == collision::SegmentAABB.
bool ScreenRayVsAABB(const collision::ScreenPickCamera& cam, int sx, int sy,
                     const float bmin[3], const float bmax[3]) {
    float origin[3], dir[3];
    collision::BuildScreenRay(cam, sx, sy, origin, dir); // 0x6A0682..0x6A0725
    return collision::SegmentAABB(origin, dir, bmin, bmax); // 0x6A0746
}

// Scene_RayHitPlayerBox 0x5415E0 — AABB [x±4.5, y..y+20, z±4.5] autour de a1[63..65]
// (== record+252/256/260 == PlayerEntity::x/y/z).
bool RayHitPlayerBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                     const game::PlayerEntity& p) {
    const float bmin[3] = { p.x - 4.5f, p.y,         p.z - 4.5f }; // 0x5415FE/0x54160A/0x54161C
    const float bmax[3] = { p.x + 4.5f, p.y + 20.0f, p.z + 4.5f }; // 0x54162E/0x541640/0x541652
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);               // 0x54166F
}

// Scene_RayHitNodeBox 0x541920 — AABB [x±4.5, y..y+11, z±4.5] autour de a1[6..8]
// (== record+24/28/32 == les 3 premiers floats de ZoneObjectEntity::body).
bool RayHitNodeBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                   const float pos[3]) {
    const float bmin[3] = { pos[0] - 4.5f, pos[1],         pos[2] - 4.5f }; // 0x54193B/0x541944/0x541953
    const float bmax[3] = { pos[0] + 4.5f, pos[1] + 11.0f, pos[2] + 4.5f }; // 0x541962/0x541971/0x541980
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);                        // 0x54199D
}

// Scene_RayHitNpcBox 0x541680 — AABB dimensionnée par le record mNPC : w = def+1328,
// h = def+1332, d = def+1336 (== NpcDefRecord::fieldF[0..2]). Le binaire lit ces champs en
// `*(int*)` puis les promeut en double avant multiplication -> reproduit tel quel.
bool RayHitNpcBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                  const game::NpcRenderEntry& n) {
    if (!n.def) return false; // le binaire déréférence *(_DWORD*)a1 sans garde ; ici le slot
                              // peut porter un def nul (cf. GameState.h:303-306) -> pas de hit.
    const double w = static_cast<double>(static_cast<int32_t>(n.def->fieldF[0])); // +1328
    const double h = static_cast<double>(static_cast<int32_t>(n.def->fieldF[1])); // +1332
    const double d = static_cast<double>(static_cast<int32_t>(n.def->fieldF[2])); // +1336
    const float bmin[3] = { static_cast<float>(n.x - w * 0.5), // 0x5416A6
                            n.y,                                // 0x5416AF
                            static_cast<float>(n.z - d * 0.5) };// 0x5416C9
    const float bmax[3] = { static_cast<float>(w * 0.5 + n.x),  // 0x5416E3
                            static_cast<float>(h + n.y),        // 0x5416F7
                            static_cast<float>(d * 0.5 + n.z) };// 0x541711
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);            // 0x54172E
}

// Scene_RayHitMonsterBox 0x541780 — AABB dimensionnée par le record MONSTER_INFO :
// w = def+248, h = def+252, d = def+256, yOff = def+260 (== MonsterInfo::collDim[0..2] et
// ::field260). `MonsterEntity::def` EST un MonsterInfo* : Game/EntityManager.cpp:148
// (`ResolveMobDef` -> `reinterpret_cast<const uint8_t*>(GetMonsterInfo(id))`) puis :523
// (`m->def = def`). Même promotion int -> double que le binaire.
bool RayHitMonsterBox(const collision::ScreenPickCamera& cam, int sx, int sy,
                      const game::MonsterEntity& m) {
    if (!m.def) return false; // def nul -> pas de boîte (le spawn est normalement rejeté sans def)
    const game::MonsterInfo* def = static_cast<const game::MonsterInfo*>(m.def);
    const double w    = static_cast<double>(def->collDim[0]); // +248
    const double h    = static_cast<double>(def->collDim[1]); // +252
    const double d    = static_cast<double>(def->collDim[2]); // +256
    const double yOff = static_cast<double>(static_cast<int32_t>(def->field260)); // +260
    const float bmin[3] = { static_cast<float>(m.x - w * 0.5),    // 0x5417A7
                            static_cast<float>(yOff + m.y),       // 0x5417BC
                            static_cast<float>(m.z - d * 0.5) };  // 0x5417D7
    const float bmax[3] = { static_cast<float>(w * 0.5 + m.x),    // 0x5417F2
                            static_cast<float>(h + m.y + yOff),   // 0x541813 (ordre d'origine)
                            static_cast<float>(d * 0.5 + m.z) };  // 0x54182E
    return ScreenRayVsAABB(cam, sx, sy, bmin, bmax);              // 0x54184B
}

// --- Offsets RECORD (== base du slot) des champs joueur lus par 0x538AB0 ------------------
// PlayerEntity::body démarre à record+0x18 (24) -> offset body = offset record - 24. Les
// offsets record ci-dessous sont ceux de Game/NameplateLogic.h:123/134/135, qui modélise le
// MÊME enregistrement (mêmes adresses dérivées).
constexpr std::size_t kBodyAffiliation = 40  - 24; // byte_168725C  (record+40)  : nom d'affiliation
constexpr std::size_t kBodyElement     = 88  - 24; // dword_168728C (record+88)  : élément
constexpr std::size_t kBodyPkLevel     = 236 - 24; // dword_1687320 (record+236) : rang PK
// Bornes du nom d'affiliation : record+40 -> record+60 (champ suivant, NameplateLogic.h:124).
constexpr std::size_t kAffiliationMaxLen = 60 - 40;
// Triplet de la catégorie 2 (partenaire d'échange) — noms IDA g_TradePartnerIdLo /
// dword_1687420 / dword_1687424, records +488/+492/+496.
// ⚠ AMBIGUÏTÉ SIGNALÉE (non tranchée, sans effet ici) : Game/NameplateLogic.h:141 interprète
// ce MÊME record+488 comme `isAdminTitle ((+122)==1)`. Les deux lectures sont incompatibles,
// mais la condition est reproduite LITTÉRALEMENT (comparaisons brutes de dwords) — donc le
// comportement est correct quelle que soit la sémantique réelle du champ.
constexpr std::size_t kBodyTradeFlag = 488 - 24;
constexpr std::size_t kBodyTradeA    = 492 - 24;
constexpr std::size_t kBodyTradeB    = 496 - 24;

int32_t ReadI32(const game::PlayerEntity& p, std::size_t bodyOff) {
    int32_t v = 0;
    std::memcpy(&v, p.body.data() + bodyOff, sizeof(v));
    return v;
}

// Chaîne C bornée lue dans le corps (les champs du binaire ne sont pas garantis NUL-terminés).
std::string ReadCString(const game::PlayerEntity& p, std::size_t bodyOff, std::size_t maxLen) {
    const char* s = reinterpret_cast<const char*>(p.body.data()) + bodyOff;
    std::size_t n = 0;
    while (n < maxLen && s[n] != '\0') ++n;
    return std::string(s, n);
}

// Filtre élémentaire des monstres @0x53905E..0x539083 — cf. §2 du bandeau .h.
// def+232 ∈ {10,11,12,13} -> K = def+232 - 10 ; ciblable ssi element != K && element != Paired(K).
bool MonsterPassesElementGate(const game::MonsterInfo& def, int localElement,
                              const game::ElementPairTable& pairs) {
    const int32_t cls = def.field232; // +232
    if (cls < 10 || cls > 13) return true; // hors {10..13} : aucune restriction
    const int k = static_cast<int>(cls) - 10;
    return localElement != k && localElement != pairs.Paired(k); // Char_GetPairedElement 0x557C00
}

// Zone courante interdisant la catégorie 1 (@0x538CFD).
bool ZoneBlocksNeutralPlayer(int zoneId) {
    for (int z : kZonesBlockingNeutralPlayer)
        if (zoneId == z) return true;
    return false;
}

} // namespace

// ===========================================================================
// TerrainPicker — implémenteur de game::ITerrainPicker (G-PICK-06).
// ===========================================================================

bool TerrainPicker::IsPointBlocked(const float pos[3]) {
    return assets_->IsPointBlocked(pos); // World_IsPointBlocked 0x540DA0
}

bool TerrainPicker::PickRayScreen(int screenX, int screenY, CollisionSlot slot, bool twoSide,
                                   float outPos[3]) {
    uint32_t faceIndex = 0; // *a4 d'origine (index de face touchée) : non consommé par
                            // Skill_CanCastAtCursor, qui ne lit que le point 3D (v7).
    return assets_->PickRayScreen(slot, cam_, screenX, screenY, faceIndex, outPos, twoSide);
}                                                                   // Terrain_PickRayScreen 0x699A80

// ===========================================================================
// BuildScreenPickCamera — cf. §0 du bandeau .h.
// ===========================================================================

collision::ScreenPickCamera BuildScreenPickCamera(const gfx::Camera& camera,
                                                   int screenW, int screenH) {
    collision::ScreenPickCamera cam{};

    const D3DXVECTOR3 eye = camera.Eye();   // g_CameraPos 0x800130 (= g_GfxRenderer+792)
    cam.eye[0] = eye.x;
    cam.eye[1] = eye.y;
    cam.eye[2] = eye.z;

    D3DXMATRIX view, invView;
    camera.BuildViewMatrix(view);
    D3DXMatrixInverse(&invView, nullptr, &view); // unk_800194 (= g_GfxRenderer+892)
    std::memcpy(cam.invView, &invView, sizeof(cam.invView)); // 16 floats row-major

    // MÊME calcul d'aspect que Gfx_InitDevice 0x69BFC6 et Scene/WorldRenderer.cpp:556-559.
    const float aspect = (screenH > 0)
        ? static_cast<float>(screenW) / static_cast<float>(screenH)
        : 1.0f;
    D3DXMATRIX proj;
    camera.BuildProjMatrix(proj, aspect);
    cam.proj11 = proj._11;   // flt_8000F0 (= g_GfxRenderer+728)
    cam.proj22 = proj._22;   // flt_800104 (= g_GfxRenderer+748)

    cam.screenW = screenW;   // dword_8000A0 (= g_GfxRenderer+648)
    cam.screenH = screenH;   // dword_8000A4 (= g_GfxRenderer+652)
    return cam;
}

// ===========================================================================
// World_PickEntityAtCursor 0x538AB0 — cf. §1..§4 du bandeau .h.
// ===========================================================================

bool World_PickEntityAtCursor(const game::GameWorld& world,
                               const collision::ScreenPickCamera& cam,
                               int screenX, int screenY,
                               bool allowModifierTargets, bool modifierKeyDown,
                               const EntityPickHost& host,
                               int& outKind, int& outIndex) {
    outKind  = 0;   // *a3 = 0  @0x538ABC
    outIndex = -1;  // *a4 = -1 @0x538AC5

    // v14 : distance de la meilleure candidate. Non initialisée dans le binaire — jamais lue
    // avant d'avoir été posée, car chaque test est gardé par `*a3` (== outKind != 0).
    float best = 0.0f;

    // Position du joueur local (flt_1687330 == g_EntityArray[0]+252 == players[0].x/y/z).
    // Même repli que Scene/WorldRenderer.cpp:567-570 si le pool est vide.
    float selfPos[3] = { 0.0f, 0.0f, 0.0f };
    if (!world.players.empty()) {
        selfPos[0] = world.players[0].x;
        selfPos[1] = world.players[0].y;
        selfPos[2] = world.players[0].z;
    }

    // « Retenir la candidate » : comparaison STRICTE `v14 > dist` -> à égalité, le PREMIER
    // trouvé gagne. Forme unique des 8 sites (les variantes if/else imbriqué des catégories
    // 1/2/3/7 et `!*a3 || v14 > d` des catégories 4/5/6 sont sémantiquement identiques).
    const auto take = [&](int kind, int index, float dist) {
        if (outKind == 0 || best > dist) {
            outKind  = kind;
            outIndex = index;
            best     = dist;
        }
    };

    // ----------------------------------------------------------------------
    // Catégories 1/2/3 — joueurs distants. Boucle @0x538ACB : `for (i = 1; i < g_EntityCount;
    // ++i)` -> l'index 0 (self) est EXCLU. Même convention d'itération que
    // Game/AutoTargetCombatGate.cpp:69.
    // ----------------------------------------------------------------------
    const int  localElement = world.self.element;              // g_LocalElement 0x1673194
    // g_SelfMorphNpcId 0x1675A98 == id de zone courant (cf. Game/SkillCombat.h:33 pour la
    // preuve). On lit g_World.zoneId et NON g_Client.VarGet(0x1675A98) : cette clé
    // d'échappatoire n'a AUCUN écrivain dans tout ClientSource (vérifié) et renverrait donc
    // toujours 0, rendant la garde morte. Scene/SceneManager.cpp:374/462 établit
    // l'équivalence (`worldMap_->SetCurrentZoneId(zoneId); // g_SelfMorphNpcId 0x1675A98`).
    const bool zoneBlocksNeutral = ZoneBlocksNeutralPlayer(world.zoneId);

    for (std::size_t i = 1; i < world.players.size(); ++i) {
        const game::PlayerEntity& p = world.players[i];
        if (!p.active) continue;                                         // g_EntityArray[227i]
        if (!game::AutoTarget_PlayerRecordPopulated(p)) continue;        // dword_168724C[227i]
        if (!game::Combat_IsTargetablePlayerState(p.anim.state)) continue; // 0x558AE0 (state != 12)
        if (!RayHitPlayerBox(cam, screenX, screenY, p)) continue;        // 0x5415E0

        const float pos[3] = { p.x, p.y, p.z };
        const float dist   = Dist3D(pos, cam.eye);                       // 0x538B7E (vs g_CameraPos)
        const int   idx    = static_cast<int>(i);

        // --- Catégorie 2 : partenaire d'échange (@0x538BCC) ---
        const game::PlayerEntity& self0 = world.players[0];
        const bool tradePair =
            ReadI32(self0, kBodyTradeFlag) == 1 &&
            ReadI32(p,     kBodyTradeFlag) == 1 &&
            ReadI32(self0, kBodyTradeA) == ReadI32(p, kBodyTradeA) &&
            ReadI32(self0, kBodyTradeB) != ReadI32(p, kBodyTradeB);
        if (tradePair) {
            take(2, idx, dist);                                          // @0x538BD9 / @0x538C01
            continue;
        }

        // --- Catégorie 3 : joueur attaquable (@0x538C4E) ---
        // Combat_CanTargetOnMap 0x558740 — non portée, déléguée à l'hôte (défaut : false).
        const bool canTarget = host.CanTargetOnMap
            ? host.CanTargetOnMap(static_cast<int>(ReadI32(p, kBodyElement)),
                                   static_cast<int>(ReadI32(p, kBodyPkLevel)),
                                   ReadCString(p, kBodyAffiliation, kAffiliationMaxLen))
            : false;
        if (canTarget) {
            take(3, idx, dist);                                          // @0x538C62 / @0x538C8A
            continue;
        }

        // --- Catégorie 1 : joueur neutre (@0x538CFD garde de zone, @0x538D07 gating) ---
        if (zoneBlocksNeutral) continue;
        // Gating modificateur : allowModifierTargets==false -> TOUJOURS éligible ;
        //                       ==true -> exige byte_8013FE < 0 (DIK_LSHIFT enfoncée).
        if (allowModifierTargets && !modifierKeyDown) continue;          // @0x538D15
        take(1, idx, dist);                                              // @0x538D22 / @0x538D4A
    }

    // ----------------------------------------------------------------------
    // Catégorie 4 — PNJ (pool de rendu g_NpcRenderArray 0x1764D14, stride 88). Boucle
    // @0x538DAC. SEULE boucle à porter le filtre de portée 500.0, mesuré contre la position
    // du JOUEUR (flt_1687330) et non de la caméra.
    // ----------------------------------------------------------------------
    for (std::size_t j = 0; j < world.npcRenderEntries.size(); ++j) {
        const game::NpcRenderEntry& n = world.npcRenderEntries[j];
        if (!n.active) continue;                                         // dword_1764D18[22j]
        const float pos[3] = { n.x, n.y, n.z };                          // unk_1764D28 + 22j
        if (Dist3D(pos, selfPos) > 500.0f) continue;                     // @0x538E04 (`<= 500.0`)
        if (!RayHitNpcBox(cam, screenX, screenY, n)) continue;           // 0x541680 @0x538E21
        take(4, static_cast<int>(j), Dist3D(pos, cam.eye));              // @0x538E48 / @0x538E83
    }

    // ----------------------------------------------------------------------
    // Catégorie 5 — monstres (dword_1766F74, stride 280). Boucle @0x538E9C.
    // ----------------------------------------------------------------------
    const game::ElementPairTable pairs = game::Combat_ReadLocalElementPairs(); // Char_GetPairedElement 0x557C00
    for (std::size_t k = 0; k < world.monsters.size(); ++k) {
        const game::MonsterEntity& m = world.monsters[k];
        if (!m.active) continue;                                         // dword_1766F74[70k]
        if (!game::Combat_IsTargetableMonsterState(game::AutoTarget_MonsterActionState(m)))
            continue;                                                    // 0x558B10 (!= 12 && != 19)
        if (!RayHitMonsterBox(cam, screenX, screenY, m)) continue;       // 0x541780
        if (!m.def) continue;
        if (!MonsterPassesElementGate(*static_cast<const game::MonsterInfo*>(m.def),
                                       localElement, pairs))
            continue;                                                    // @0x53905E..0x539083
        const float pos[3] = { m.x, m.y, m.z };                          // unk_1766F94 + 70k
        take(5, static_cast<int>(k), Dist3D(pos, cam.eye));              // @0x539083 / @0x5390BE
    }

    // ----------------------------------------------------------------------
    // Catégorie 6 — objets au sol (dword_17AB534, stride 152, Scene_RayHitItemModel 0x5418B0).
    // TODO [0x5418B0 / 0x6A0750 / 0x4D7130] : NON PORTÉE — triple blocage, AUCUN effet
    // observable aujourd'hui (cf. §4 du bandeau .h) : (a) game::g_World.groundItems est vide
    // par conception (structure 152 o non modélisée, Game/GameState.h:605-613) ; (b) ce
    // hit-test est un test d'OBB (ModelObj_GetBoneMatrix 0x4D7130 + Collide_SegmentOBB
    // 0x6A0750), aucune des deux briques n'étant portée. Quand (a) tombera, rétablir la
    // boucle ICI (l'ordre compte pour la règle « premier trouvé gagne » à égalité).
    // ----------------------------------------------------------------------

    // ----------------------------------------------------------------------
    // Catégorie 7 — objets de zone (g_ZoneObjectArray 0x180EEF4, stride 76). Boucle @0x5391A7.
    // Position = g_ZoneObjectPayload 0x180EF0C == record+24 == les 3 premiers floats du body
    // (ZoneObjectEntity::body est à record+0x18 == +24). Même gating modificateur que la
    // catégorie 1 (@0x539220), et AUCUN autre filtre.
    // ----------------------------------------------------------------------
    for (std::size_t n = 0; n < world.zoneObjects.size(); ++n) {
        const game::ZoneObjectEntity& z = world.zoneObjects[n];
        if (!z.active) continue;                                         // g_ZoneObjectArray[19n]
        float pos[3];
        std::memcpy(pos, z.body.data(), sizeof(pos));
        if (!RayHitNodeBox(cam, screenX, screenY, pos)) continue;        // 0x541920 @0x5391F2
        if (allowModifierTargets && !modifierKeyDown) continue;          // @0x539220 / @0x53922F
        take(7, static_cast<int>(n), Dist3D(pos, cam.eye));              // @0x539219 / @0x53923C
    }

    return outKind != 0;
}

// ===========================================================================
// CursorSlotForPickCategory — table du switch @0x530FC7..0x53120B.
// ===========================================================================

int CursorSlotForPickCategory(int kind, float gameTimeSec, bool canCastAtCursor) {
    if (kind < 0 || kind > 7) return -1;               // `cmp 7 / ja default` @0x530FB4

    // Catégorie 0 : PAS d'animation — 7 si castable, 8 sinon (Skill_CanCastAtCursor 0x540E60
    // appelé @0x530FE1, son UNIQUE appelant).
    if (kind == 0) return canCastAtCursor ? 7 : 8;     // @0x530FEA / @0x530FF8

    // blink = ((int)(2.0f * g_GameTimeSec)) % 2 — idiome SIGNÉ du binaire (`fadd st,st` +
    // Crt_ftol + `and eax,80000001h` / `jns` / `dec` / `or eax,0FFFFFFFEh` / `inc`), qui
    // correspond exactement au `%` de C++ (division tronquée). Alternance à 2 Hz.
    const int blink = static_cast<int>(2.0f * gameTimeSec) % 2;

    switch (kind) {
        case 1: return blink + 1; // @0x531022 — joueur neutre
        case 2: return blink + 3; // @0x531075 — partenaire d'échange
        case 3: return blink + 3; // @0x5310C8 — joueur attaquable
        case 4: return blink + 1; // @0x53111B — PNJ
        case 5: return blink + 3; // @0x53116B — monstre
        case 6: return blink + 5; // @0x5311B9 — objet au sol
        case 7: return blink + 1; // @0x5311E2 — objet de zone
        default: return -1;
    }
}

} // namespace ts2::world

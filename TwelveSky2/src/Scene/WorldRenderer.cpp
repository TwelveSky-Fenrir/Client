// Scene/WorldRenderer.cpp — voir WorldRenderer.h pour le périmètre exact du câblage.
#include "Scene/WorldRenderer.h"
#include "Gfx/Renderer.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/NameplateLogic.h"
#include "Game/NpcInteraction.h"  // game::Npc_GetNameplateColor (0x540790) — couleur des libelles MONSTRE (W9)
#include "Game/ClientRuntime.h"   // game::Str (StrTable005_Get 0x4C1D10) — NameplateHost::ResolveString (W9)
#include "Game/ExtraDatabases.h"  // game::NpcDefRecord (libelle PNJ : name@+4, fieldF[1]@+1332) — W9
// world::World_PickEntityAtCursor (0x538AB0) + world::BuildScreenPickCamera — W9.
// ⚠️⚠️ DÉPENDANCE DE LIEN À CÂBLER PAR L'ORCHESTRATEUR ⚠️⚠️
// src\World\TerrainPicker.cpp et src\World\TerrainPicker.h existent sur disque (créés par le
// front terrain-picker) mais NE SONT PAS listés dans TwelveSky2.vcxproj / .vcxproj.filters
// (vérifié : 0 occurrence de « TerrainPicker » dans les deux fichiers, et le projet n'utilise
// aucun glob — 138 <ClCompile Include=...> explicites, dont World\WorldMap.cpp et
// World\WorldIntegration.cpp mais PAS World\TerrainPicker.cpp).
// -> Tant que TerrainPicker.cpp n'est pas ajouté au projet, drawNameplatePass() ne LIERA PAS
//    (unresolved external : world::World_PickEntityAtCursor, world::BuildScreenPickCamera).
// Ce front ne touche pas au .vcxproj (règle de périmètre) : action remontée à
// l'orchestrateur. AUCUNE autre solution n'était fidèle — réimplémenter le hit-test ici
// aurait dupliqué les 5 Scene_RayHit* (0x5415E0/0x541680/0x541780/0x5418B0/0x541920) déjà
// portés, ou fait inventer un picking (interdit).
#include "World/TerrainPicker.h"
#include "World/WorldIntegration.h" // F_ENTITY3D (B8) : world::WorldAssets + collision::GroundPlane (plan-sol ombre)
#include "Config/GameOptions.h"   // config::g_Options (g_Opt_ShowHitMarkers/ShowNameplates 0x84DED0/D4) — W9
#include "Game/StaticNpcLoader.h" // PNJ de decor (mission "PNJ DECOR VISIBLES A L'ECRAN", cf. Render())
#include "Game/AnimationTick.h"       // ZoneNpc_AnimTickIsWired() / Monster_MotionTickIsWired() / IMotionFrameCountOracle — W7
#include "Game/PlayerAnimCursorTick.h" // Player_AnimCursorTickIsWired() (curseur joueur) — front F_PLAYERANIM
#include "Game/EntityLifecycleTick.h" // g_MonsterTickExt (motionState/animFrame par monstre) — W7
#include "Gfx/MotionCache.h"      // palette d'os animee (miroir g_ModelMotionArray 0x8E8B30) — W3-F1
#include "Gfx/PlayerPaperdoll.h"  // paperdoll joueur (calque Char_RenderModel 0x527020) — W3-F1
#include "Game/WeaponTrailResolver.h" // trainee d'arme : switch id->v6 + stems (front F_WEAPONTRAIL)
#include "Core/Log.h"
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2 {

namespace {

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Cache CPU des palettes d'os animees (miroir de g_ModelMotionArray 0x8E8B30). WorldRenderer.h
// n'etant PAS editable (aucun membre ajoutable), on l'instancie en statique de fichier — duree de
// vie process, DONNEES 100 % CPU (aucun device D3D requis, cf. MotionCache). gameDataDir="." :
// meme convention et meme raison que modelCache_ (le CWD process est deja bascule sur gameDataDir
// des App::Init, bien avant tout rendu — cf. WorldRenderer::Init).
gfx::MotionCache& Motions() {
    static gfx::MotionCache m(".");
    return m;
}

// Implementation de game::IMotionFrameCountOracle (Passe 4 / W7, front motion-anim) adossee au
// MEME MotionCache que le dessin -- c'est le point CRUCIAL de fidelite : dans le binaire, le
// tick (Model_GetMotionFrameCount 0x4E5A70 @0x582d6a et consorts) et le dessin (Char_Draw
// @0x580770) resolvent le slot par le MEME accesseur sur le MEME g_ModelMotionArray, donc
// frameCount du tick == frameCount de la palette dessinee. Faire diverger ces deux sources
// (p.ex. une table de durees separee) desynchroniserait wrap et echantillonnage.
class MotionFrameCountOracle final : public game::IMotionFrameCountOracle {
public:
    // Model_GetMotionFrameCount 0x4E5A70 (monstre).
    int GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType) const override {
        return Motions().GetMonsterMotionFrameCount(monsterDefId, animType);
    }
    // Model_GetWeaponEffectFrameCount 0x4E5A40 (PNJ de decor), indexe comme game::ZoneNpcs().
    int GetZoneNpcMotionFrameCount(int zoneNpcIndex, int animType) const override {
        const std::vector<game::StaticNpcSlot>& slots = game::ZoneNpcs();
        if (zoneNpcIndex < 0 || static_cast<size_t>(zoneNpcIndex) >= slots.size()) return 0;
        const game::NpcDefRecord* def = slots[static_cast<size_t>(zoneNpcIndex)].def;
        if (!def) return 0; // jamais nul en pratique (garde du chargeur), defensif
        return Motions().GetNpcMotionFrameCount(*def, animType);
    }
};

// Palette approximative des codes couleur de NameplateLogic (game::kNameColor*).
// HORS PÉRIMÈTRE : la vraie palette vit côté assets UI (littéraux passés à
// UI_DrawNumberValue 0x53FCC0, jamais des RVB directs) — non reversée ici. On
// choisit des teintes lisibles par rôle (neutre/hostile/admin/affilié/GM...) pour
// que la nameplate reste exploitable visuellement, sans prétendre à la fidélité
// pixel de la vraie palette.
D3DCOLOR ColorFromNameplateCode(int code) {
    switch (code) {
        case game::kNameColorHostileHidden:     return 0xFFFF4040u; // rouge
        case game::kNameColorAdminPrimary:
        case game::kNameColorAdminSecondary:    return 0xFFFFC040u; // or
        case game::kNameColorWhisper:           return 0xFF60D0FFu; // cyan
        case game::kNameColorAffiliate:         return 0xFF40FF80u; // vert
        case game::kNameColorAllianceAffiliate: return 0xFF40D0D0u; // sarcelle
        case game::kNameColorGmAccount:         return 0xFFFF60FFu; // magenta
        case game::kNameColorVipOrMarketCase0:
        case game::kNameColorMarketCase1:
        case game::kNameColorMarketCase2:
        case game::kNameColorMarketCase3:
        case game::kNameColorMarketGroupA_Std:
        case game::kNameColorMarketGroupA_Alt:  return 0xFFFFA040u; // orange
        case game::kNameColorNeutral:
        default:                                return 0xFFFFFFFFu; // blanc
    }
}

// Npc_DrawMesh 0x57FF00 (cf. Docs/TS2_NPC_MESH_DRAW.md) : garde de culling PROPRE
// aux PNJ, absente de Char_Draw 0x5805C0 (vérifié par décompilation intégrale des
// deux fonctions le 2026-07-14 : Char_Draw ne contient AUCUN appel Math_Dist3D,
// seul le near-cull caméra IsBeyondCameraNearCull s'applique aux joueurs/monstres).
// Npc_DrawMesh, elle, bloque le dessin dès l'entrée si
// Math_Dist3D(pos_pnj, flt_1687330 /* = position du JOUEUR LOCAL, this+5 de
// g_EntityArray[0] */) > 1000.0 -- AVANT même le test near-cull caméra. game::
// ComputeEntityDrawFlags (utilisée uniformément players/monsters/npcs ici) ne
// modélise que le pipeline Char_Draw (aucun far-cull) : sans ce garde-fou
// supplémentaire, un PNJ à >1000 unités du joueur local serait dessiné (cube
// placeholder) alors que le client d'origine ne le ferait jamais.
constexpr float kNpcFarCullDistanceSq = 1000.0f * 1000.0f;

// F_ENTITY3D (B8) — hauteur de modèle (a2 de Model_RenderPlanarShadow 0x40F720) servant au
// segment de pick du plan-sol (start = pos.y + h @0x40F995) et à sa portée (maxDist = h·2.5
// @0x40FA39). GAP ASSUMÉ, PAS une constante prouvée : la vraie hauteur (bound du SObject) n'est
// pas portée côté ClientSource (EntityRenderInfo::drawSize reste 0 — cf. WorldRenderer.h §LOD).
// Le PLAN trouvé (donc la projection D3DXMatrixShadow) ne dépend PAS de h ; h ne borne que la
// recherche : cette valeur (× scale entité) suffit à trouver le sol sous une entité au sol, sans
// rien inventer de la géométrie. Cf. rapport de front.
constexpr float kShadowModelHeight = 12.0f;

// Offset (o) du champ "id d'objet de l'arme équipée" (u32 LE) à l'intérieur de
// PlayerEntity::body (600 o, payload brut Pkt_SpawnCharacter 0x0f / 0x4646c0). Valable
// pour TOUT joueur du tableau (self inclus), câblé ici pour les joueurs DISTANTS
// uniquement (le joueur local utilise SelfState::equip[7].itemId, déjà à jour en continu
// — cf. bandeau WorldRenderer.h pour la preuve de décompilation complète, paire de
// fonctions jumelles Weapon_ClassFromEquip 0x4cc9f0 (self, dword_1673248) /
// Weapon_ClassFromField56 0x4cc930 (générique, *(entity+172)) : entity+172 = body+148
// car le body démarre à entity+0x18).
constexpr size_t kPlayerBodyWeaponItemIdOffset = 148;

// Offsets (o) race/genre/costume dans PlayerEntity::body (mission "câblage corps de base
// joueur", 2026-07-14, cf. Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5) : PROUVÉS par
// décompilation directe (3 sites d'appel qui relisent entity+92/+96/+100/+104 sur le
// tableau runtime g_EntityArray, self ET distants -- entity+92 = body+68 car le body
// démarre à entity+0x18). Valables SANS distinction pour p.body de n'importe quel index
// (contrairement à l'arme, aucun global self séparé connu -- le doc §4 montre au
// contraire que gender/costumeSlot0/costumeSlot1 sont mutés EN PLACE dans entity[0], donc
// dans ce même body, par Pkt_ItemActionDispatch).
constexpr size_t kPlayerBodyRaceOffset         = 68; // [0,3)
constexpr size_t kPlayerBodyGenderOffset       = 72; // [0,2)
constexpr size_t kPlayerBodyCostumeSlot0Offset = 76; // [0,7) -- catalogue flt_F59A7C
constexpr size_t kPlayerBodyCostumeSlot1Offset = 80; // [0,3) -- catalogue flt_F5B21C

// Lecture u32 little-endian dans PlayerEntity::body à l'offset donné ; 0 si l'offset
// (+4 o) dépasse la taille du tableau (garde défensive, ne devrait jamais arriver avec
// un offset constant 148 < 600, mais évite tout UB si body venait à changer de taille).
uint32_t ReadBodyU32LE(const std::array<uint8_t, 600>& body, size_t offset) {
    if (offset + sizeof(uint32_t) > body.size()) return 0;
    uint32_t v = 0;
    std::memcpy(&v, body.data() + offset, sizeof(v)); // hôte x86 LE : pas de reorder
    return v;
}

// ===========================================================================
// Passe 4 / vague W9 — front nameplate-entity : champs lus par Char_DrawNameplate 0x56EF40
// sur l'ENTITÉ (g_EntityArray 0x1687234, stride 908), traduits en offsets dans
// PlayerEntity::body. RÈGLE DE CONVERSION : le body démarre à entity+0x18 (Pkt_SpawnCharacter
// 0x4646C0 fait `Crt_Memcpy(&dword_168724C[227*i], v8, 600)` avec dword_168724C ==
// g_EntityArray + 0x18) => bodyOffset = entityOffset - 24. Même convention que
// kPlayerBodyWeaponItemIdOffset ci-dessus et que World/TerrainPicker.cpp:107-120.
//
// ⚠️ BORNE DURE : le body ne fait que 600 o, soit entity+24..entity+623. Les champs
// d'entité au-delà de +623 (p. ex. altWhisperName +756) ne sont PAS dans le body — ce sont
// des champs runtime écrits ailleurs. Ils ne sont donc PAS peuplés ici (et n'ont de toute
// façon d'usage que dans le bloc « détaillé », mort — cf. §DRAWMODE de NameplateLogic.h).
//
// TRIPLE CORROBORATION des 3 offsets les plus structurants — World_PickEntityAtCursor
// 0x538AB0 lit LES MÊMES champs sur LA MÊME base et les passe à Combat_CanTargetOnMap avec
// EXACTEMENT la signature de NameplateHost::CanTargetOnMap(element, pkLevel, affiliation) :
//     byte_168725C  = 0x1687234 + 0x28  -> entity+40  -> body+16  (affiliation)
//     dword_168728C = 0x1687234 + 0x58  -> entity+88  -> body+64  (element)
//     dword_1687320 = 0x1687234 + 0xEC  -> entity+236 -> body+212 (pkLevel)
// -> valeurs identiques à celles déjà portées dans World/TerrainPicker.cpp (kBodyAffiliation
// = 40-24, kBodyElement = 88-24, kBodyPkLevel = 236-24). Aucune divergence entre les deux
// fronts.
// ===========================================================================
constexpr size_t kNpBodyHasIdentity   = 24  - 24; // entity+24  : identité résolue — MÊME champ que
                                                   //   la garde `dword_168724C[227*i]` de 0x538AB0
constexpr size_t kNpBodyGmAccount     = 28  - 24; // entity+28  : (this+7)==1  -> couleur 35 @0x56FFED
constexpr size_t kNpBodyLevel         = 32  - 24; // entity+32  : (this+8)     @0x56F229
constexpr size_t kNpBodyAffiliation   = 40  - 24; // entity+40  : (this+10)    @0x56F8B6
constexpr size_t kNpBodyElement       = 88  - 24; // entity+88  : (this+22)    @0x56FD3F
constexpr size_t kNpBodyTitleBarExtra = 220 - 24; // entity+220 : `cmp [ecx+0DCh],0` @0x56F115
constexpr size_t kNpBodyEnchantRaw    = 224 - 24; // entity+224 : (this+56)    @0x56F6DC
constexpr size_t kNpBodyPkLevel       = 236 - 24; // entity+236 : (this+59)    @0x56F29B
constexpr size_t kNpBodySpecialPk     = 244 - 24; // entity+244 : (this+61)==12 -> couleur 44 @0x570006
                                                   //   (== EntityManager kPActionState, même champ)
constexpr size_t kNpBodyAlliance      = 472 - 24; // entity+472 : (this+118)   @0x56F870
constexpr size_t kNpBodyAdminTitle    = 488 - 24; // entity+488 : (this+122)==1 @0x56F6C9 (cf. note
                                                   //   « trade » de NameplateLogic.h)
constexpr size_t kNpBodyAdminTitleAlt = 496 - 24; // entity+496 : (this+124)==1
constexpr size_t kNpBodyVipWord       = 544 - 24; // entity+544 : MOT (this+272)==1 -> couleur 5 @0x56FFD7
constexpr size_t kNpBodyRankTier      = 568 - 24; // entity+568 : (this+142)   @0x57002B
constexpr size_t kNpBodySuppressExtra = 576 - 24; // entity+576 : `cmp [edx+240h],0` @0x56F124

// Borne de lecture des chaînes d'affiliation/alliance. VALEUR REPRISE TELLE QUELLE de
// World/TerrainPicker.cpp:111 (`kAffiliationMaxLen = 60 - 40`) : l'écart jusqu'au champ
// connu suivant (subAffiliationName entity+60, cf. NameplateActor). NON PROUVÉE comme étant
// la capacité réelle du buffer — c'est une borne DÉFENSIVE (jamais d'overrun, au pire un nom
// tronqué plutôt qu'inventé), même convention que EntityManager::kPNameBufLen.
constexpr size_t kNpBodyStringMaxLen = 60 - 40;

// Lecture d'un mot 16 bits LE (VIP : `*((_WORD*)this + 272)` @0x56FFD7).
uint16_t ReadBodyU16LE(const std::array<uint8_t, 600>& body, size_t offset) {
    if (offset + sizeof(uint16_t) > body.size()) return 0;
    uint16_t v = 0;
    std::memcpy(&v, body.data() + offset, sizeof(v));
    return v;
}

// Lecture d'une C-string bornée dans le body (même motif que
// EntityManager::ReadPlayerName / TerrainPicker::ReadCString : aucun octet hors bornes).
std::string ReadBodyCString(const std::array<uint8_t, 600>& body, size_t offset, size_t maxLen) {
    if (offset >= body.size()) return std::string();
    const size_t avail = body.size() - offset;
    const size_t cap   = (maxLen < avail) ? maxLen : avail;
    const uint8_t* p = body.data() + offset;
    size_t len = 0;
    while (len < cap && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

} // namespace

// Accesseur public de l'oracle (cf. Scene/WorldRenderer.h pour la justification du placement).
// Singleton a duree de vie process, adosse au MotionCache de dessin.
const game::IMotionFrameCountOracle& WorldMotionFrameCountOracle() {
    static const MotionFrameCountOracle s_oracle;
    return s_oracle;
}

// FrameCount du clip courant d'un JOUEUR (front F_PLAYERANIM) — cf. Scene/WorldRenderer.h. Adosse
// au MEME MotionCache (Motions()) que le dessin : Motion_GetFrameCount 0x4D7830 renvoye par
// PcModel_ResolveSlotAndApply 0x4E5A00, qui borne le wrap du curseur (Char_TickMoveState @0x574922).
int WorldPlayerMotionFrameCount(int race, int gender, int weaponType, int animState) {
    if (const gfx::MotionPalette* mp = Motions().GetForPlayer(race, gender, weaponType, animState))
        return mp->frameCount; // MotionPalette+4 = Motion_GetData 0x4D78C0 (slot+140), cf. MotionCache.h
    return 0;                  // slot non resolu -> Player_AdvanceAnimCursor avance sans wrap
}

// ===========================================================================
//  Init / Shutdown
// ===========================================================================

bool WorldRenderer::Init(gfx::Renderer& renderer, int screenW, int screenH) {
    device_  = renderer.Device();
    screenW_ = screenW;
    screenH_ = screenH;
    if (!device_) { TS2_ERR("WorldRenderer::Init : device nul"); return false; }

    // hWndParent 0x815184 pour le ScreenToClient de drawNameplatePass (@0x52FB6C) — W9.
    // Récupéré depuis le device plutôt que via un nouveau paramètre d'Init() (dont le seul
    // appelant, Scene/SceneManager.cpp, n'est pas un fichier de ce front) : hFocusWindow EST
    // le HWND passé à CreateDevice par gfx::Renderer::Init(HWND, ...). Repli sur le
    // hDeviceWindow de la swap-chain si le device a été créé sans fenêtre de focus explicite.
    D3DDEVICE_CREATION_PARAMETERS cp{};
    if (SUCCEEDED(device_->GetCreationParameters(&cp)) && cp.hFocusWindow) {
        hwnd_ = cp.hFocusWindow;
    } else {
        IDirect3DSwapChain9* sc = nullptr;
        if (SUCCEEDED(device_->GetSwapChain(0, &sc)) && sc) {
            D3DPRESENT_PARAMETERS pp{};
            if (SUCCEEDED(sc->GetPresentParameters(&pp))) hwnd_ = pp.hDeviceWindow;
            sc->Release();
        }
    }
    if (!hwnd_)
        TS2_WARN("WorldRenderer::Init : HWND introuvable -> survol nameplate en coordonnees ecran brutes.");

    if (!meshRenderer_.Init(renderer)) {
        TS2_ERR("WorldRenderer::Init : MeshRenderer::Init a echoue");
        return false;
    }

    // ModelCache (mission "cabler ResolveModel() sur Gfx/ModelCache", 2026-07-14).
    // gameDataDir="." plutot qu'un chemin explicite : WorldRenderer::Init() ne recoit
    // PAS le gameDataDir resolu par App (SceneManager::Init a bien ce parametre, mais
    // SceneManager::Change() n'en passe AUCUN a world_->Init() -- Scene/SceneManager.cpp
    // est explicitement HORS PERIMETRE de cette mission, a ne pas modifier). Ceci reste
    // correct malgre tout : App::ResolveGameDataDir() (App/App.cpp) bascule le CWD du
    // process SUR gameDataDir des App::Init(), donc bien AVANT que SceneManager::Change
    // (InGame) construise/Init ce WorldRenderer -- au moment ou ModelCache en a besoin,
    // "." == gameDataDir (meme hypothese que les chemins codes en dur d'origine
    // "G01_GFONT\..." deja consommes ailleurs dans ClientSource sans prefixe).
    modelCache_ = std::make_unique<gfx::ModelCache>(meshRenderer_, std::string("."));

    // FRONT FX-F4 (M1) : charge les shaders REELS du npk et les cable sur meshRenderer_ AVANT la
    // 1ere frame. Best-effort comme le cube/police : un echec (npk absent) laisse meshRenderer_ sur
    // son fallback HLSL reconstruit, sans bloquer l'init. Ancre IDA : GXD_DeviceCreate 0x401610
    // charge les 12 Shader_LoadVSxx/PSxx en sequence ; ShaderSet::LoadFromFile reproduit ce chemin
    // (defaut "./GXDEFFECT/GXDEffect.npk", cle {1,4,4,1} -- cf. Shader_LoadVS03 0x409AB0 : Npk_OpenFile
    // + Npk_FindEntryByName("Shader03.fx")). AttachShaderSet avec un ShaderSet valide (VS03/PS04)
    // suffit a basculer DrawSkinnedSubset sur les vrais shaders (cf. MeshRenderer.cpp:510).
    if (shaderSet_.LoadFromFile(device_)) {
        meshRenderer_.AttachShaderSet(&shaderSet_); // slots VS03/PS04 reels (0x409AB0/0x409CC0)
        TS2_LOG("WorldRenderer : ShaderSet npk cable (Shader03/04 reels).");
    } else {
        TS2_WARN("WorldRenderer : GXDEffect.npk indisponible -> shaders HLSL reconstruits (fallback).");
    }

    if (!buildPlaceholderCube(device_))
        TS2_WARN("WorldRenderer::Init : placeholder cube indisponible (D3DXCreateBox).");
    if (!font_.Init(device_, screenW, screenH))
        TS2_WARN("WorldRenderer::Init : Font::Init a echoue (nameplates muettes).");

    ready_ = true;
    TS2_LOG("WorldRenderer pret (%dx%d).", screenW, screenH);
    return true;
}

void WorldRenderer::Shutdown() {
    font_.Shutdown();
    modelCache_.reset(); // ~ModelCache -> Clear() -> libere VB/IB/textures GPU residents
    SafeRelease(cubeMesh_);
    meshRenderer_.Shutdown();
    // FRONT FX-F4 (M1) : ORDRE IMPERATIF -- meshRenderer_.Shutdown() a deja lache sa reference
    // (shaderSet_ interne = nullptr, cf. MeshRenderer.cpp:195), on peut donc liberer VS/PS/CT/decl
    // du npk sans qu'aucun draw ne reference des shaders deja liberes.
    shaderSet_.Release();
    device_ = nullptr;
    hwnd_   = nullptr; // W9 : symétrie avec Init() (re-résolu depuis le device au prochain Init)
    ready_  = false;
}

void WorldRenderer::OnDeviceLost() {
    font_.OnDeviceLost();
}

void WorldRenderer::OnDeviceReset() {
    font_.OnDeviceReset();
}

// ===========================================================================
//  Résolution modèle — câblée sur Gfx/ModelCache (cf. bandeau WorldRenderer.h)
// ===========================================================================

const gfx::SkinnedModel* WorldRenderer::ResolveMonsterModel(uint32_t monsterDefId) {
    if (!modelCache_ || monsterDefId == 0) return nullptr;
    // ModelCache::GetForMonster fait tout le travail (lecture g_World.db.monster,
    // field244 -> kindIndex, formule stem M*.SOBJECT) — cf. Gfx/ModelCache.cpp.
    return modelCache_->GetForMonster(monsterDefId);
}

const gfx::SkinnedModel* WorldRenderer::ResolveWeaponModel(uint32_t weaponItemId) {
    if (!modelCache_ || weaponItemId == 0) return nullptr;
    const game::ItemInfo* item = game::GetItemInfo(weaponItemId);
    if (!item) return nullptr; // id hors bornes ou slot vide (cf. GetItemInfo)
    return modelCache_->GetForItem(*item, /*slot=*/0); // slot 0 = modele principal
}

const gfx::SkinnedModel* WorldRenderer::ResolveNpcModel(const game::NpcDefRecord* npcDef) {
    if (!modelCache_ || !npcDef) return nullptr;
    // ModelCache::GetForNpc lit npcDef->fieldE (+1324, kindIndex+1) -> formule
    // "N%03d%03d001.SOBJECT" ; nullptr si hors bornes [1,66] (cf. Gfx/ModelCache.cpp).
    return modelCache_->GetForNpc(*npcDef);
}

gfx::PlayerBodyModel WorldRenderer::ResolvePlayerBodyModel(int race, int gender,
                                                            int costumeSlot0, int costumeSlot1) {
    if (!modelCache_) return {};
    // ModelCache::GetForPlayerBody fait tout le travail (formule kindIndex=race+3*gender,
    // stems SLOT0/SLOT1, bornes) -- cf. Gfx/ModelCache.cpp.
    return modelCache_->GetForPlayerBody(race, gender, costumeSlot0, costumeSlot1);
}

// ===========================================================================
//  Placeholder cube (D3DXCreateBox + pipeline fixe, couleur plate)
// ===========================================================================

bool WorldRenderer::buildPlaceholderCube(IDirect3DDevice9* dev) {
    HRESULT hr = D3DXCreateBox(dev, 1.0f, 1.0f, 1.0f, &cubeMesh_, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldRenderer: D3DXCreateBox a echoue (0x%08lX)", hr);
        return false;
    }
    return true;
}

void WorldRenderer::drawPlaceholderCube(const D3DXVECTOR3& pos, float scale, D3DCOLOR color,
                                        float rotYDeg, const D3DXMATRIX& view, const D3DXMATRIX& proj) {
    if (!cubeMesh_ || !device_) return;

    const float sz = (scale > 0.05f) ? scale : 1.0f;
    D3DXMATRIX s, r, t, world, tmp;
    D3DXMatrixScaling(&s, sz, sz, sz);
    // Rotation Y (mission ROTATION/ORIENTATION, 2026-07-14) : même convention degrés que
    // MeshRenderer::DrawModel / Model_Render 0x40EBB0 (S*Rz*Ry*Rx*T ; ici seul Ry est
    // non-identité, comme dans le binaire pour ce canal).
    D3DXMatrixRotationY(&r, D3DXToRadian(rotYDeg));
    // Pose le cube au sol (base à pos.y), pas centré sur pos.y, pour rester lisible
    // à côté d'un futur vrai modèle dont l'origine est aussi au sol.
    D3DXMatrixTranslation(&t, pos.x, pos.y + sz * 0.5f, pos.z);
    D3DXMatrixMultiply(&tmp, &s, &r);
    D3DXMatrixMultiply(&world, &tmp, &t);

    // Repasse en pipeline fixe (MeshRenderer laisse ses shaders skinnés bindés).
    device_->SetVertexShader(nullptr);
    device_->SetPixelShader(nullptr);
    device_->SetVertexDeclaration(nullptr);
    device_->SetFVF(cubeMesh_->GetFVF());

    device_->SetTransform(D3DTS_WORLD, &world);
    device_->SetTransform(D3DTS_VIEW, &view);
    device_->SetTransform(D3DTS_PROJECTION, &proj);

    // Couleur plate via TFACTOR (pas de dépendance lumière/matériau) : robuste et
    // lisible quel que soit l'état de la lumière du renderer.
    device_->SetRenderState(D3DRS_LIGHTING, FALSE);
    device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device_->SetRenderState(D3DRS_TEXTUREFACTOR, color);
    device_->SetTexture(0, nullptr);
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
    device_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);

    // Filet de debug (bullet 4, tâche W3-F1) : le cube n'est plus qu'un REPLI de traçabilité
    // (jamais dessiné quand un modèle/palette résout) -> rendu en fil de fer pour signaler
    // visuellement « modèle non résolu » sans masquer la scène. AUCUNE ancre IDA : le cube
    // n'existe pas dans le binaire d'origine — repli de debug, pas de fidélité.
    device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    cubeMesh_->DrawSubset(0);
    device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

    // Restaure l'état de texturage standard (modulation texture*diffuse) attendu
    // par les prochains blits sprite/mesh de la frame.
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    // CORRECTIF AUDIT (même classe que le crash CharSelect, cf. MeshRenderer.h:369-380) : ce cube
    // a posé SetVertexShader/PixelShader(nullptr) sur le device PARTAGÉ (L401-402) sans que
    // MeshRenderer le sache -> son cache currentPass_ est désormais périmé. Sans cette invalidation,
    // le PROCHAIN modèle skinné de la MÊME passe (currentPass_ inchangé) saute le re-bind VS/PS dans
    // DrawSkinnedSubset (MeshRenderer.cpp `if (currentPass_ != usePass)`) et se dessine avec le VS
    // NULL du cube (pipeline fixe, mal placé/invisible) — dès qu'un asset manquant côtoie un modèle
    // résolu de même passe dans la boucle de corps.
    meshRenderer_.InvalidateShaderBindingCache();
}

// ===========================================================================
//  Ombre PLANAIRE projetée — Vague B / branchement B8 (front F_ENTITY3D)
//  Bracket Scene_InGameRender 0x52D0B0 (0x52D9DC..0x52DB15) + Model_RenderPlanarShadow 0x40F720.
// ===========================================================================
//
// REMPLACE l'ancienne approximation drawReflectionOverlay() (monstres seuls, corps redessiné à la
// même transformée SANS aplatissement ni bracket) par la VRAIE ombre : on interroge le plan-sol
// (collisionSource_->GetGroundPlaneForShadow 0x40F720) puis on aplatit le corps skinné via
// meshRenderer_.DrawModelPlanarShadow (D3DXMatrixShadow @0x40FB28, PASSE 5 = VS09 + PS NULL),
// pour les 3 catégories que le binaire ombre dans le bracket : JOUEURS (Char_DrawWeaponEffectVariantB
// @0x52DA41), MONSTRES (Char_DrawReflection @0x52DB09) et PNJ DE DÉCOR (Npc_DrawMeshGlow @0x52DAA2).
// Les PNJ gameplay (bodyMeshEligible=false) n'ont pas de corps -> pas d'ombre, comme dans l'original.

// GXD_SetupStencilShadowState 0x404F20 — états D3D du DÉBUT du bracket (byte-exact, disasm relu
// cette session : device=[esi+20Ch]=+524, SetRenderState=+228, SetTextureStageState=+268).
void WorldRenderer::beginPlanarShadowBracket() {
    if (!device_) return;
    device_->SetRenderState(D3DRS_LIGHTING, FALSE);                    // (137,0) @0x404F7D
    device_->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_FLAT);           // (9,1)   @0x404F92
    device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);               // (14,0)  @0x404FA7
    device_->SetRenderState(D3DRS_STENCILENABLE, TRUE);              // (52,1)  @0x404FBC
    device_->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);        // (56,3)  @0x404FD1
    device_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);   // (55,7)  @0x404FE6 masque anti-double-blend
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);          // (27,1)  @0x404FFB
    device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);     // (19,5)  @0x405010
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); // (20,6)  @0x405025
    // TEXTUREFACTOR (60) : RGB=0, A = (u8)((diffuse.r+diffuse.g+diffuse.b)/3 · 128) << 24 — couleur
    // = light diffuse (this+1124/1128/1132) ; /3.0 (dbl_7EDA38) · 128.0 (dbl_7EDA88) @0x405053..0x40507F.
    // -> ombre NOIRE semi-transparente. STENCILREF jamais posé par 0x404F20 -> hérité (non inventé).
    const D3DXVECTOR3& d = meshRenderer_.LightDiffuse();
    int a = static_cast<int>(((d.x + d.y + d.z) / 3.0f) * 128.0f);
    if (a < 0)   a = 0;
    if (a > 255) a = 255;
    device_->SetRenderState(D3DRS_TEXTUREFACTOR, static_cast<D3DCOLOR>(a) << 24); // (60,A<<24) @0x40507F
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);        // (0,1,2)  @0x405096
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);            // (0,2,3)  @0x4050AD
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);            // (0,5,3)  @0x4050C6
}

// GXD_EndStencilShadowState 0x4050D0 — restauration (byte-exact, MÊME ordre, Docs §5).
void WorldRenderer::endPlanarShadowBracket() {
    if (!device_) return;
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);   // (0,5,2) @0x4050E8
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);   // (0,2,2) @0x4050FF
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE); // (0,1,4) @0x405116
    device_->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);            // (60,-1) @0x40512B
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);             // (20,1)  @0x405140
    device_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_ONE);              // (19,2)  @0x405155
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);            // (27,0)  @0x40516A
    device_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);      // (55,1)  @0x40517F
    device_->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);          // (56,8)  @0x405194
    device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);              // (52,0)  @0x4051A9
    device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);                // (14,1)  @0x4051BE
    device_->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);         // (9,2)   @0x4051D3
    device_->SetRenderState(D3DRS_LIGHTING, TRUE);                     // (137,1) @0x4051ED
}

void WorldRenderer::renderPlanarShadows(const std::vector<DrawableEntity>& drawables,
                                        const game::DrawCullContext& cull) {
    // Prérequis : source du plan-sol posée (SetCollisionSource, par MAIN) + device. Repli propre
    // sinon — AUCUN dessin, AUCUN plan y=constante inventé (règle « IDA = unique vérité »).
    if (!collisionSource_ || !device_ || !modelCache_) return;

    // Direction de projection d'ombre = celle que DrawModelPlanarShadow injecte (négée) dans
    // D3DXMatrixShadow ; la MÊME sert au pick du plan-sol -> cohérence pick/projection.
    const D3DXVECTOR3& shadowDir = meshRenderer_.ShadowLightDir();
    const float lightDir[3] = { shadowDir.x, shadowDir.y, shadowDir.z };

    // Bracket d'états ouvert UNE fois autour de toutes les entités (0x52D9DC).
    beginPlanarShadowBracket();

    for (const DrawableEntity& ent : drawables) {
        if (!ent.bodyMeshEligible) continue; // PNJ gameplay : aucun corps -> aucune ombre

        // Visibilité = gate de Char_DrawReflection/Char_DrawShadow (showReflection = active &&
        // dist(pos, self) <= 300 && near-cull caméra) — PROUVÉ pour l'ombre MONSTRE ; réutilisé
        // pour joueurs/PNJ (leur gate exact vit dans le méga-switch this+0xDC non décompilé,
        // cf. WorldRenderer.h §Ombre — approximation documentée, pas une invention de seuil).
        const game::EntityDrawFlags flags =
            game::ComputeEntityDrawFlags(ent.renderState, cull, /*drawPass=*/1);
        if (!flags.showReflection) continue;

        const game::BodyMeshPlacement placement = game::ComputeBodyMeshPlacement(ent.renderState);
        const D3DXVECTOR3 pos(placement.pos.x, placement.pos.y, placement.pos.z);
        const float scale = (placement.scale > 0.0f) ? placement.scale : 1.0f;
        const D3DXVECTOR3 rotDeg(0.0f, placement.angle, 0.0f);
        const D3DXVECTOR3 scaleVec(scale, scale, scale);

        // Plan-sol via segment [pieds+h -> +shadowDir] (Model_RenderPlanarShadow 0x40F720 :
        // Collision_SegPickA 0x420D60 + maxDist = h·2.5 @0x40FA39). h = kShadowModelHeight·scale
        // (GAP documenté en tête de fichier). Repli propre si le sol ne résout pas.
        const float posArr[3]     = { pos.x, pos.y, pos.z };
        const float modelHeight   = kShadowModelHeight * scale;
        world::collision::GroundPlane gp;
        if (!collisionSource_->GetGroundPlaneForShadow(posArr, modelHeight, lightDir,
                                                       modelHeight * 2.5f, gp) || !gp.valid)
            continue;

        // TRAINEE D'ARME — passe OMBRE PLANAIRE (Char_DrawWeaponEffectVariantB 0x56BF90 ->
        // SObject_DrawAnimated2 0x4D91C0 -> Model_RenderPlanarShadow 0x40F720). Dessinée AVANT le
        // corps, comme le bloc traînée en tête de 0x56BF90 précède les pièces de corps. Même
        // SObject/palette/transformée que la traînée opaque de renderOne -> silhouette aplatie
        // cohérente. resolveWeaponTrail() self-gate sur hasBody (no-op monstres/PNJ) et indépendante
        // de la résolution du corps ci-dessous (le `continue` du paperdoll ne l'annule pas).
        {
            const gfx::SkinnedModel* trailModel = nullptr;
            gfx::BonePalette trailPalette;
            if (resolveWeaponTrail(ent, trailModel, trailPalette))
                meshRenderer_.DrawModelPlanarShadow(*trailModel, pos, rotDeg, scaleVec,
                                                    trailPalette, gp.shadowPlane);
        }

        // Résolution du/des modèle(s) + palette — MÊME source que renderOne (le corps et son ombre
        // partagent géométrie ET transformée), pour que la silhouette aplatie corresponde au corps.
        if (ent.hasBody) {
            // JOUEUR : paperdoll (pièces corps + arme, palette d'os partagée). Chaque pièce est
            // aplatie séparément -> l'ombre est la composite (cf. Model_RenderPlanarShadow : un
            // SObject_DrawAnimated2 par pièce, chaque appel une silhouette).
            // front F_PLAYERANIM : animType (etat FSM entity+244) + curseur (entity+248, garde
            // par hasAnimCursor) -> clip ET cadence reels du joueur (cf. header PlayerPaperdoll).
            // Passe aussi a la passe d'ombre planaire (silhouette aplatie = MEME anim que le corps).
            gfx::PaperdollResult pd = gfx::PlayerPaperdoll::Resolve(
                *modelCache_, Motions(), ent.bodyRace, ent.bodyGender,
                ent.bodyCostumeSlot0, ent.bodyCostumeSlot1, ent.weaponItemId,
                ent.animType, ent.animCursor, ent.hasAnimCursor,
                game::g_World.gameTimeSec);
            if (!pd.valid) continue; // corps non résolu -> pas d'ombre (pas de cube d'ombre inventé)
            for (const gfx::SkinnedModel* piece : pd.pieces)
                meshRenderer_.DrawModelPlanarShadow(*piece, pos, rotDeg, scaleVec,
                                                    pd.palette, gp.shadowPlane);
        } else {
            // MONSTRE / PNJ DE DÉCOR : un bodyModel + sa palette animée (même résolution que
            // renderOne : ResolveMonsterModel/ResolveNpcModel + Motions().GetFor*).
            const gfx::SkinnedModel* bodyModel = (ent.monsterDefId != 0)
                ? ResolveMonsterModel(ent.monsterDefId)
                : ResolveNpcModel(ent.npcDef);
            if (!bodyModel || bodyModel->Empty()) continue; // modèle non résolu -> pas d'ombre

            gfx::BonePalette palette; // repli identité si aucune MOTION ne résout (idem renderOne)
            if (ent.monsterDefId != 0) {
                if (const gfx::MotionPalette* mp = Motions().GetForMonster(ent.monsterDefId, ent.animType))
                    palette = ent.hasAnimCursor
                                ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)
                                : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
            } else if (ent.npcDef) {
                if (const gfx::MotionPalette* mp = Motions().GetForNpc(*ent.npcDef, ent.animType))
                    palette = ent.hasAnimCursor
                                ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)
                                : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
            }
            meshRenderer_.DrawModelPlanarShadow(*bodyModel, pos, rotDeg, scaleVec,
                                                palette, gp.shadowPlane);
        }
    }

    // Ferme le bracket : restaure les états (0x52DB15).
    endPlanarShadowBracket();
}

// ===========================================================================
//  Nameplates (projection écran + gfx::Font)
// ===========================================================================

bool WorldRenderer::worldToScreen(const D3DXVECTOR3& world, const D3DXMATRIX& viewProj,
                                  int& sx, int& sy) const {
    D3DXVECTOR4 clip;
    D3DXVec3Transform(&clip, &world, &viewProj);
    if (clip.w <= 0.001f) return false; // derriere la camera (ou au niveau de l'oeil)

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    // Marge généreuse : on laisse passer un peu hors écran plutôt que de couper
    // trop tôt un nom dont seule la moitié dépasserait du cadre.
    if (ndcX < -1.5f || ndcX > 1.5f || ndcY < -1.5f || ndcY > 1.5f) return false;

    sx = static_cast<int>((ndcX * 0.5f + 0.5f) * static_cast<float>(screenW_));
    sy = static_cast<int>((1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(screenH_));
    return true;
}

void WorldRenderer::drawEntityLabel(const std::string& text, const D3DXVECTOR3& worldPos,
                                    D3DCOLOR color, const D3DXMATRIX& viewProj) {
    if (text.empty() || !font_.Ready()) return;
    int sx = 0, sy = 0;
    if (!worldToScreen(worldPos, viewProj, sx, sy)) return;
    const int w = font_.MeasureText(text.c_str());
    font_.DrawTextStyled(text.c_str(), sx - w / 2, sy, color, gfx::kStyleOutline);
}

// ===========================================================================
//  Traînée d'arme — résolution partagée opaque/ombre (front F_WEAPONTRAIL)
//  Char_DrawWeaponEffectVariantB 0x56BF90 (switch id->v6 @0x56c001, gate état @0x56c411) /
//  Char_DrawWeaponTrailEffect 0x55E9D0 (même switch, primitive opaque). Cf. WorldRenderer.h.
// ===========================================================================

bool WorldRenderer::resolveWeaponTrail(const DrawableEntity& ent,
                                       const gfx::SkinnedModel*& outModel,
                                       gfx::BonePalette& outPalette) {
    outModel   = nullptr;
    outPalette = gfx::BonePalette{}; // repli identité (invalide) par défaut

    // (1) Traînée = effet JOUEUR uniquement (0x55E9D0/0x56BF90 bouclent sur g_EntityArray).
    if (!ent.hasBody || !modelCache_) return false;
    // (2) Gate maître @0x56c01b : weaponAnimSlot != 0 ET !altWeaponSet (this+55 / this+144).
    if (ent.weaponAnimSlot == 0 || ent.altWeaponSet) return false;
    // (3) Switch id d'anim -> index d'effet v6 ∈ [0,41] (transcrit de 0x56BF90).
    const int v6 = game::ResolveWeaponTrailIndex(ent.weaponAnimSlot);
    if (v6 < 0) return false;
    // (4) Gate d'état d'action (this+61 = entity+244 = CharAnimState::state, porté dans animType)
    //     -> sous-bloc de motion 0/1/2, ou -1 (aucun dessin).
    const int motionSub = game::ResolveWeaponTrailMotionSub(ent.animType);
    if (motionSub < 0) return false;

    // Motion F/cat.5 de l'effet (unk_F54DB4/E50/EEC + 468*v6). Le sous-bloc 2 n'est dessiné QUE
    // si frameCount>=1 (Motion_GetFrameCount @0x56c43e) ; les sous-blocs 0/1 sont inconditionnels.
    const gfx::MotionPalette* mp = Motions().GetForWeaponTrail(v6, motionSub);
    if (game::WeaponTrailMotionSubIsFrameGated(motionSub) && (!mp || mp->frameCount < 1))
        return false;

    // (5) SObject de l'effet (stem "Y%03d001" cat.9, résolu par ModelCache::Get). Sans modèle
    //     sur disque -> pas de traînée (SObject_Load échouerait pareillement dans le binaire).
    const gfx::SkinnedModel* model = modelCache_->Get(game::BuildWeaponTrailStem(v6));
    if (!model || model->Empty()) return false;

    // Palette d'os : même curseur (entity+248 = this+62 = animCursor) que le corps. Repli identité
    // si aucune motion ne résout (sous-blocs 0/1) — SampleByCursor/SampleByGameTime rendent une
    // BonePalette invalide dans ce cas, DrawModel retombe alors sur identityPalette_.
    if (mp) {
        outPalette = ent.hasAnimCursor
                        ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)
                        : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    }
    outModel = model;
    return true;
}

// ===========================================================================
//  Rendu d'une entité (EntityDrawLogic pour la décision, NameplateLogic pour le nom)
// ===========================================================================

void WorldRenderer::renderOne(const DrawableEntity& ent, const game::DrawCullContext& cull,
                              const D3DXMATRIX& view, const D3DXMATRIX& proj,
                              const D3DXMATRIX& viewProj) {
    // Char_Draw 0x5805C0 : a2=1 (passe principale, cf. Scene_InGameRender). renderOne ne dessine
    // QUE le corps : l'ombre planaire (ex-showReflection) est désormais une passe DÉDIÉE, dessinée
    // AVANT les corps (renderPlanarShadows, front F_ENTITY3D / B8) ; le libellé vit dans
    // drawNameplatePass. showShadow (volume 0x580CE0) reste code mort, jamais dessiné.
    const game::EntityDrawFlags flags = game::ComputeEntityDrawFlags(ent.renderState, cull, /*drawPass=*/1);
    if (!flags.showBody) return; // inactif ou hors garde near-cull (IsBeyondCameraNearCull)

    const game::BodyMeshPlacement placement = game::ComputeBodyMeshPlacement(ent.renderState);
    const D3DXVECTOR3 pos(placement.pos.x, placement.pos.y, placement.pos.z);
    const float scale = (placement.scale > 0.0f) ? placement.scale : 1.0f; // repli placeholder

    // Corps : monstre -> modèle réel si résolu (remplace le cube) ; joueur -> corps de
    // base réel (SLOT0+SLOT1, race/genre/costume, cf. bandeau WorldRenderer.h "JOUEURS")
    // si résolu (remplace le cube) ; PNJ DE DÉCOR (ent.npcDef non-nul, cf. bandeau
    // WorldRenderer.h §"PNJ") -> modèle réel via ResolveNpcModel si résolu (remplace le
    // cube) ; PNJ GAMEPLAY (ent.npcDef nul) -> pas de modèle de corps connu -> cube
    // systématique. monsterDefId et npcDef ne sont jamais renseignés simultanément
    // (cf. DrawableEntity), monsterDefId a priorité par construction ici.
    // bodyModel : MONSTRE / PNJ DE DÉCOR uniquement (les joueurs passent par le paperdoll
    // ci-dessous). Conservé aussi pour la passe reflet plus bas (Char_DrawReflection 0x581090,
    // monstres seulement). Pour un joueur (monsterDefId==0 && npcDef==nul) -> nullptr.
    const gfx::SkinnedModel* bodyModel = (ent.monsterDefId != 0)
        ? ResolveMonsterModel(ent.monsterDefId)
        : ResolveNpcModel(ent.npcDef);

    const D3DXVECTOR3 rotDeg(0.0f, placement.angle, 0.0f);
    const D3DXVECTOR3 scaleVec(scale, scale, scale);

    // Palette d'os ANIMÉE — Char_Draw 0x5805C0 / Npc_DrawMesh 0x57FF00 -> SObject_DrawEx 0x4D9330
    // (Motion_GetData 0x4D78C0 = motionSlot+136) -> Model_Render 0x40EBB0 (frame = ftol(animTime),
    // borné 0..frameCount-1).
    //
    // ///// CORRIGÉ — Passe 4 / vague W7, front motion-anim (gaps as-motion-01 + as-motion-02) /////
    // AVANT : `GetFor*(defId, /*anim idle*/0)` + `SampleByGameTime(g_World.gameTimeSec)`, avec un
    // `TODO [ancre 0x571880]`. DEUX défauts, tous deux corrigés ici :
    //   1. animType FIGÉ à 0 -> monstres et PNJ ne changeaient JAMAIS d'animation. Le binaire lit
    //      un animType PAR ENTITÉ : slot monstre +24 (@0x580770, arg 3 de Model_GetNpcMotionSlot)
    //      / slot PNJ +12 (@0x57ffa0, arg 3 de Model_GetNpcMeshSlot).
    //   2. horloge GLOBALE -> toutes les entités animées EN PHASE. Le binaire lit un curseur PAR
    //      ENTITÉ : slot monstre +28 (@0x580828) / slot PNJ +16 (@0x57fff1), accumulé par le tick
    //      de l'entité (`+= dt*30`), jamais par g_GameTimeSec.
    // L'ancien `TODO [ancre 0x571880]` pointait de surcroît la MAUVAISE fonction : aucun monstre ne
    // passe par Char_UpdateAnimationFrame 0x571880 (réservée à g_EntityArray = les joueurs). Le
    // désassemblage de Scene_InGameUpdate 0x52C600 prouve 4 familles disjointes — @0x52c96d/
    // @0x52c9fd joueurs (0x571880), @0x52ca4c PNJ décor (Npc_RenderSlotTick 0x5803A0), @0x52cad6
    // MONSTRES (Char_Update 0x581E10). Portages : Game/AnimationTick.h §5 (monstres) / §6 (PNJ).
    //
    // hasAnimCursor=false (JOUEURS) -> repli SampleByGameTime, inchangé : leur curseur réel dépend
    // du switch terminal 0x5727BF (55 handlers) non porté, le brancher le figerait à 0.
    gfx::BonePalette palette; // repli identité si aucune MOTION ne résout
    if (ent.monsterDefId != 0) {
        // Model_GetNpcMotionSlot 0x4E5960 (monstre, stride 3276) — arg 3 = animType par entité.
        if (const gfx::MotionPalette* mp = Motions().GetForMonster(ent.monsterDefId, ent.animType))
            palette = ent.hasAnimCursor
                        ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)      // @0x580828
                        : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    } else if (ent.npcDef) {
        // Model_GetNpcMeshSlot 0x4E5910 (PNJ de décor, stride 468) — arg 3 = animType par entité.
        if (const gfx::MotionPalette* mp = Motions().GetForNpc(*ent.npcDef, ent.animType))
            palette = ent.hasAnimCursor
                        ? gfx::MotionCache::SampleByCursor(*mp, ent.animCursor)      // @0x57fff1
                        : gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    }
    // TODO [ancre Char_Draw 0x5805C0 @0x580776] : cas `*((_DWORD*)this + 53)` (slot+212 =
    // MonsterTickExt::fallActive) NON implémenté — quand un monstre est en chute/knockback, le
    // binaire dessine avec animTime = 0.0 (pose figée) et lit pos/rot en slot+240/+252
    // (fallOffX/Y/Z, @0x5807d1) au lieu de slot+32/+56. Hors des 2 gaps de ce front, et la
    // physique de recul elle-même n'est pas portée (cf. Game/AnimationTick.cpp, TODO knockback).

    // PNJ GAMEPLAY : bodyMeshEligible=false -> AUCUN corps ni cube (l'original ne dessine
    // jamais de mesh pour dword_17AB534, cf. DrawableEntity::bodyMeshEligible).
    // MISE À JOUR W9 : « seule la nameplate est émise pour ces entités » — ANCIENNE MENTION
    // DEVENUE FAUSSE, corrigée. Ces entités n'ont AUCUN libellé dans le client d'origine
    // (Char_DrawNameTag 0x583470 = code mort, catégorie de clic 6 = aucun dessin — cf.
    // drawNameplatePass, case 6). Elles ne produisent donc plus rien du tout ici.

    // TRAINEE D'ARME — passe OPAQUE (Char_DrawWeaponTrailEffect 0x55E9D0 -> SObject_DrawEx 0x4D9330
    // -> Model_Render 0x40EBB0), dessinée AVANT le corps : dans 0x55E9D0 le bloc traînée (switch
    // @0x55EAxx) précède le dessin du corps (flt_F59A7C/F5B21C @0x561750/0x561949) — même ordre que
    // dans 0x56BF90 (traînée en tête). Même transformée que le corps (pos=this+63, cap=this+69=rotDeg.y,
    // scaleVec ; animTime=this+62=animCursor via la palette). resolveWeaponTrail() self-gate sur
    // hasBody -> no-op pour monstres/PNJ ; gate strict (weaponAnimSlot/altWeaponSet/v6/état) => pas de
    // traînée permanente. Indépendante de la résolution du corps (émise même si le corps retombe sur
    // le cube). ⚠ weaponAnimSlot non alimenté réseau à ce jour -> aucune traînée en pratique (cf. header).
    {
        const gfx::SkinnedModel* trailModel = nullptr;
        gfx::BonePalette trailPalette;
        if (resolveWeaponTrail(ent, trailModel, trailPalette))
            meshRenderer_.DrawModel(*trailModel, pos, rotDeg, scaleVec, trailPalette);
    }

    if (ent.bodyMeshEligible) {
        if (ent.hasBody) {
            // JOUEUR — PlayerPaperdoll (calque Char_RenderModel 0x527020) : UNE palette d'os
            // animée PARTAGÉE (PcModel_ResolveEquipSlot 0x4E46A0) + liste ordonnée de pièces
            // (corps SLOT0 flt_F59A7C / SLOT1 flt_F5B21C + arme). Remplace l'ancien corps
            // 2-pièces inline ET l'ancien hack d'arme (wpos = pos.y + scale*0.6). L'arme est
            // désormais une pièce dessinée à la MÊME transformée + MÊME palette que le corps
            // (Char_RenderModel 0x527bfe : arme skinnée au bone de main via v37), pas un offset.
            // front F_PLAYERANIM : animType (etat FSM entity+244) + curseur (entity+248, garde
            // par hasAnimCursor) -> clip ET cadence reels du joueur (cf. header PlayerPaperdoll).
            // Passe aussi a la passe d'ombre planaire (silhouette aplatie = MEME anim que le corps).
            gfx::PaperdollResult pd = gfx::PlayerPaperdoll::Resolve(
                *modelCache_, Motions(), ent.bodyRace, ent.bodyGender,
                ent.bodyCostumeSlot0, ent.bodyCostumeSlot1, ent.weaponItemId,
                ent.animType, ent.animCursor, ent.hasAnimCursor,
                game::g_World.gameTimeSec);
            if (pd.valid) {
                for (const gfx::SkinnedModel* piece : pd.pieces)
                    meshRenderer_.DrawModel(*piece, pos, rotDeg, scaleVec, pd.palette);
            } else {
                // Repli : ni corps ni arme résolus -> filet de debug (cf. drawPlaceholderCube).
                drawPlaceholderCube(pos, scale, ent.placeholderColor, rotDeg.y, view, proj);
            }
        } else if (bodyModel && !bodyModel->Empty()) {
            // MONSTRE / PNJ DE DÉCOR — modèle réel + palette animée résolue ci-dessus.
            meshRenderer_.DrawModel(*bodyModel, pos, rotDeg, scaleVec, palette);
        } else {
            // Traçabilité visuelle même sans le vrai modèle (cf. WorldRenderer.h) : filet de
            // dernier recours si le modèle monstre/PNJ n'a pas résolu. N'atteint JAMAIS un PNJ
            // gameplay (bodyMeshEligible=false ci-dessus).
            drawPlaceholderCube(pos, scale, ent.placeholderColor, rotDeg.y, view, proj);
        }
    }

    // NOTE FIDÉLITÉ : monstre = chemin le mieux ancré (Char_Draw 0x5805C0 EST le dessin monstre
    // en jeu). Joueur = extrapolation de Char_RenderModel 0x527020 (dessin corps joueur en jeu non
    // localisé statiquement) — palette animée appliquée en jeu comme choix honnête, supérieure à
    // l'identité. L'ancienne surimpression d'arme séparée (offset pos.y+0.6, aucun bone reversé)
    // est SUPPRIMÉE au profit de l'attache main par skinning (paperdoll).

    // OMBRE PLANAIRE — DÉPLACÉE hors de renderOne (front F_ENTITY3D / B8). L'ancienne
    // approximation « reflet » (drawReflectionOverlay, monstres seuls, corps redessiné sans
    // aplatissement ni bracket) est REMPLACÉE par la vraie ombre projetée : Model_RenderPlanarShadow
    // 0x40F720 (aplatissement D3DXMatrixShadow @0x40FB28, PASSE 5 = VS09 + PS NULL), dessinée pour
    // JOUEURS + MONSTRES + PNJ DE DÉCOR dans une passe DÉDIÉE (renderPlanarShadows), bracketée UNE
    // fois par les états d'ombre (GXD_Setup/EndStencilShadowState 0x404F20/0x4050D0) et exécutée
    // AVANT les corps opaques — exactement comme le bracket 0x52D9DC..0x52DB15 de Scene_InGameRender
    // (les ombres AVANT l'opaque). renderOne() ne dessine donc plus QUE le corps (fidèle : Char_Draw
    // 0x5805C0 ne dessine ni ombre ni libellé). ent.reflectionEligible n'est plus lu ici (la garde
    // de visibilité showReflection est ré-évaluée dans la passe d'ombre).

    // ///// SUPPRIMÉ — Passe 4 / vague W9, front nameplate-entity (gaps HUD-NP-01/02/05) /////
    // Ce site émettait `ComputeNameplateInfo(actor, /*drawMode=*/1, ent.notSelf, vctx, host)`
    // pour CHAQUE joueur/monstre/PNJ à CHAQUE frame. TROIS défauts cumulés, tous corrigés en
    // déplaçant le libellé dans drawNameplatePass() (cf. bandeau WorldRenderer.h) :
    //   1. drawMode=1 reproduisait un CHEMIN MORT du binaire : `xrefs_to(0x56EF40)` = 4 sites,
    //      et le seul en a2=1 (@0x52FC02) est gardé par `dword_1668F64 ∈ {1,2}`, global JAMAIS
    //      écrit (find_bytes('64 8F 66 01') = 4 occurrences, toutes opérandes de `cmp` ;
    //      xrefs_to(0x1668F64) = 4 lectures, 0 écriture). Les 3 sites VIVANTS
    //      (@0x531052/@0x5310A5/@0x5310F8) passent a2=2, sur l'UNIQUE entité sous le curseur.
    //   2. `vctx` n'était peuplé que de selfX/Y/Z -> optShowHitMarkers=false -> la garde
    //      @0x56F679 (`a2 != 1 || g_Opt_ShowHitMarkers && (...)`) était FAUSSE -> mainLine.text
    //      vide -> `drawEntityLabel()` n'était JAMAIS appelée. AUCUNE plaque de nom n'était
    //      rendue, pour aucune entité, aucune frame.
    //   3. Joueurs, monstres, PNJ gameplay ET PNJ de décor passaient tous par le MÊME
    //      ComputeNameplateInfo (logique JOUEUR : enchantement/PK/guilde/marché), alors que le
    //      binaire a 4 fonctions de libellé DISJOINTES avec des sources de couleur distinctes.
    // Fidélité structurelle au passage : Char_Draw 0x5805C0 (le corps, cette fonction) ne
    // dessine AUCUN libellé — les libellés vivent dans le bloc 2D (Gfx_Begin2D @0x52FB89) de
    // Scene_InGameRender, pas dans le dispatcher de corps.
    (void)viewProj; // conservé dans la signature : renderOne reste le point unique des 4 boucles
}

// ===========================================================================
//  Passe « libellé de l'entité survolée » — bloc 2D de Scene_InGameRender 0x52D0B0
//  (@0x52FB58..0x53120B). Voir le bandeau de déclaration dans WorldRenderer.h.
// ===========================================================================

void WorldRenderer::drawNameplatePass(const gfx::Camera& camera, const game::DrawCullContext& cull,
                                      const D3DXMATRIX& viewProj) {
    if (!font_.Ready()) return;

    // ---- 1. Position curseur -> coordonnées client ------------------------
    // @0x52FB5C `call ds:off_7A6364` (GetPhysicalCursorPos) puis @0x52FB6C
    // `call ds:off_7A6368` (ScreenToClient) avec hWnd = ds:hWndParent (0x815184) ;
    // Point.x -> var_94, Point.y -> var_4B4 = les 2 args écran de World_PickEntityAtCursor.
    // ÉCART ASSUMÉ (1 appel) : on utilise GetCursorPos et non GetPhysicalCursorPos — même
    // convention que UI/UIManager.cpp:293 (déjà en place pour UI_RenderAllDialogs 0x5AE2D0,
    // qui fait pourtant lui aussi GetPhysicalCursorPos). Les deux ne diffèrent que sous
    // virtualisation DPI ; s'aligner sur le reste de ClientSource évite deux conventions de
    // curseur contradictoires dans le même process.
    POINT pt{};
    GetCursorPos(&pt);
    if (hwnd_) ScreenToClient(hwnd_, &pt);

    // ---- 2. Hit-test -> catégorie + index ---------------------------------
    // World_PickEntityAtCursor 0x538AB0 (porté : World/TerrainPicker.cpp:206). Le binaire
    // appelle DEUX fois la même fonction, la seule différence étant le 5e argument :
    //   @0x530F7E `push 0` si g_Opt_ShowHitMarkers == 0   (cmp/jnz @0x530F54)
    //   @0x530FA6 `push 1` sinon
    // -> a5 == (g_Opt_ShowHitMarkers ? 1 : 0). C'est le SEUL effet VIVANT de cette option
    // dans tout le domaine « plaques de nom » (cf. §DRAWMODE de Game/NameplateLogic.h).
    const bool allowModifierTargets = (config::g_Options.ShowHitMarkers != 0);

    // byte_8013FE < 0 (octet SIGNÉ = bit 7 posé = touche enfoncée). World/TerrainPicker.h:136-147
    // établit que byte_8013FE == DirectInput state[0x2A] == DIK_LSHIFT (base du tableau
    // DirectInput = g_GfxRenderer+5564 = 0x8013D4, doublement corroborée par byte_8013F2 ==
    // DIK_A et byte_8013E5 == DIK_W, déjà portés).
    // ÉCART ASSUMÉ ET DOCUMENTÉ : on lit l'état Win32 (GetAsyncKeyState) et non le tableau
    // DirectInput, car ts2::input::InputSystem n'a AUCUNE instance globale (elle appartient à
    // App et est passée par référence) et WorldRenderer::Render(camera) — dont l'appelant
    // Scene/SceneManager.cpp n'est PAS un fichier de ce front — ne la reçoit pas. MÊME touche
    // physique, MÊME sémantique 2 états ; la seule divergence théorique est le décalage d'une
    // frame entre le snapshot DirectInput et l'état async. Préféré à un hook non assigné
    // (qui laisserait toute cette passe morte). Cf. rapport : wiringTodoForOrchestrator.
    const bool modifierKeyDown = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;

    const world::collision::ScreenPickCamera pickCam =
        world::BuildScreenPickCamera(camera, screenW_, screenH_);
    // EntityPickHost::CanTargetOnMap non branché (Combat_CanTargetOnMap 0x558740 non portée —
    // dépend de Map_GetPvpMode 0x4FAB90 + du système de zones PVP) -> défaut false -> la
    // catégorie 3 (joueur attaquable) ne sort jamais et retombe en catégorie 1. Dégradation
    // déjà assumée par World/TerrainPicker.h:201-203 ; les catégories 1/2/3 convergeant TOUTES
    // vers Char_DrawNameplate(a2=2), cela n'a aucun effet sur CE site d'appel-ci.
    const world::EntityPickHost pickHost{};

    int pickKind = 0, pickIndex = -1;
    world::World_PickEntityAtCursor(game::g_World, pickCam, pt.x, pt.y,
                                     allowModifierTargets, modifierKeyDown, pickHost,
                                     pickKind, pickIndex);
    if (pickKind == 0 || pickIndex < 0) return; // rien sous le curseur -> aucun libellé

    // ---- 3. Contexte commun ----------------------------------------------
    // g_SelfMorphNpcId 0x1675A98 == id de zone courant : équivalence établie par
    // Scene/SceneManager.cpp:462 (`game::g_World.zoneId = zoneId; // g_SelfMorphNpcId`) et
    // réutilisée telle quelle par World/TerrainPicker.cpp:250. C'est ce champ qui pilote les
    // groupes « marché » 270..274 / 291 de Char_DrawNameplate.
    game::NameplateViewerContext vctx{};
    vctx.localMorphNpcId = game::g_World.zoneId;
    vctx.selfX = cull.localPlayerPos.x;
    vctx.selfY = cull.localPlayerPos.y;
    vctx.selfZ = cull.localPlayerPos.z;
    vctx.optShowNameplates = (config::g_Options.ShowNameplates != 0); // g_Opt_ShowNameplates 0x84DED4
    vctx.optShowHitMarkers = (config::g_Options.ShowHitMarkers != 0); // g_Opt_ShowHitMarkers 0x84DED0
    // optDetailedNameplates RESTE false — dword_1668F64 n'est JAMAIS écrit dans le binaire
    // (4 lectures, 0 écriture) : le bloc titre/guilde/chuchoté/icônes/debug-GM qu'il garde
    // (@0x57008D) est du CODE MORT. Le peupler afficherait ce que le client d'origine
    // n'affiche jamais. NE PAS « allumer ». Cf. §DRAWMODE de Game/NameplateLogic.h.
    //
    // localGmAuthLevel / localFactionSubMode / localFactionFlag / chatColorWhisper /
    // partyFlagIndicatorCount restent à 0 VOLONTAIREMENT : re-vérifié cette vague, aucun
    // d'eux n'a d'effet observable sur le chemin vivant (drawMode=2) —
    //   · localGmAuthLevel : lu par la garde @0x56F679 (court-circuitée par `a2 != 1`) et par
    //     `hostile = gm <= 0 && CanTargetOnMap(...)` (CanTargetOnMap non branchée -> false
    //     quel que soit gm) et par le bloc debug GM (mort). Le brancher exigerait
    //     Net/NetClient.h (winsock2.h APRÈS le windows.h de WorldRenderer.h -> erreur
    //     d'inclusion garantie) pour ZÉRO effet : refusé.
    //   · localFactionSubMode / localFactionFlag : lus uniquement dans la branche `hostile`
    //     (jamais prise, cf. ci-dessus).
    //   · chatColorWhisper / partyFlagIndicatorCount : bloc « détaillé », mort.
    game::NameplateHost host{};
    // Near-cull CAMÉRA (Target_IsBeyondClickRange 0x5410D0, appel @0x56EF96 avec a2=20.0).
    // BRANCHÉ (gap HUD-NP-09) sur l'implémentation DÉJÀ existante et identique
    // game::IsBeyondCameraNearCull (Game/EntityDrawLogic.cpp:22) — jusqu'ici la callback
    // était nulle et NameplateLogic retombait sur `true`, sautant purement le cull.
    // Le callback reçoit y DÉJÀ décalé de +10 (cf. NameplateHost) : on repasse `y - 10` avec
    // radius=20 pour reconstituer littéralement les arguments d'origine
    // (`Target_IsBeyondClickRange((float*)this+63, 20.0)` -> dy = camY - (pos.y + 20*0.5)).
    host.IsBeyondCameraNearCull = [&cull](float x, float y, float z) {
        return game::IsBeyondCameraNearCull(game::Vec3{ x, y - 10.0f, z }, 20.0f, cull.cameraPos);
    };
    // StrTable005_Get(g_LangId, id) 0x4C1D10 -> game::Str (placeholder « #<id> » stable tant
    // que 001.DAT n'est pas déchiffré, cf. Game/ClientRuntime.h:198-202). Branché : c'est la
    // convention déjà retenue par tous les handlers réseau du projet.
    host.ResolveString = [](int stringId) { return game::Str(stringId); };
    // ResolveValueTierPrefix (UI_GetValueTierString 0x54CD40), ResolveEnchantName
    // (Item_GetEnchantNameString 0x548330), CanTargetOnMap (Combat_CanTargetOnMap 0x558740),
    // IsSameAffiliationAsLocal / IsSameAllianceAsLocal (sentinels sociaux) : AUCUNE n'est
    // portée dans ClientSource (grep exhaustif) -> laissées nulles, replis documentés dans
    // NameplateLogic.h ("" / false). TODO [ancres 0x54CD40, 0x548330, 0x558740] : à brancher
    // ici même dès que ces fonctions existeront — ce site est leur consommateur.
    // IsOnScreen (Cam_ProjectToScreen 0x6A24F0) : laissée nulle -> repli `true`. Le vrai
    // gate écran est appliqué juste après par drawEntityLabel -> worldToScreen (qui rejette
    // clip.w <= 0 et |ndc| > 1.5), donc le brancher ici ferait le travail deux fois.

    // ---- 4. Switch 8 cas @0x530FC7 — AU PLUS UN libellé par frame ---------
    switch (pickKind) {
    // -------------------------------------------------------------------
    // Catégories 1/2/3 — JOUEURS (g_EntityArray). Les 3 cas appellent le MÊME
    // Char_DrawNameplate(&g_EntityArray[0x38C*idx], /*a2=*/2, /*a3=*/idx, arg_0) :
    //   @0x531052 (cat 1, neutre) / @0x5310A5 (cat 2, échange) / @0x5310F8 (cat 3, attaquable)
    // `a3` = l'indice de boucle du picker, dont la boucle joueurs démarre à `i = 1`
    // (@0x538ACB) : l'index 0 (le joueur local) n'est JAMAIS retourné -> notSelf est
    // TOUJOURS vrai sur ce chemin. Le client d'origine ne dessine donc jamais de plaque de
    // nom au-dessus de SA PROPRE tête. La garde `pickIndex >= 1` ci-dessous matérialise
    // cette invariante (défensive : le picker la garantit déjà).
    // -------------------------------------------------------------------
    case 1:
    case 2:
    case 3: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.players.size()) break;
        if (pickIndex < 1) break; // self exclu par construction (@0x538ACB `for (i = 1; ...)`)
        const game::PlayerEntity& p = game::g_World.players[static_cast<size_t>(pickIndex)];

        game::NameplateActor actor{};
        actor.active      = p.active;                                                  // entity+0
        // hasIdentity (entity+24) : MÊME champ que la garde `dword_168724C[227*i]` du picker
        // (0x168724C == g_EntityArray + 0x18 == entity+24 == body+0). Remplace l'ancien
        // `actor.hasIdentity = true; // TODO(fidélité)` qui court-circuitait le test.
        actor.hasIdentity = (ReadBodyU32LE(p.body, kNpBodyHasIdentity) != 0);
        actor.x = p.x; actor.y = p.y; actor.z = p.z;   // entity+252/+256/+260, cf. @0x56F133..0x56F15D
        actor.name = p.name;                            // entity+72 = body+48 (EntityManager::ReadPlayerName)

        actor.level        = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyLevel));
        actor.element      = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyElement));
        actor.enchantRaw   = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyEnchantRaw));
        actor.pkLevel      = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyPkLevel));
        actor.rankTierValue = static_cast<int>(ReadBodyU32LE(p.body, kNpBodyRankTier));
        actor.isGmAccount   = (ReadBodyU32LE(p.body, kNpBodyGmAccount) == 1);          // @0x56FFED -> 35
        actor.isVipOrHighlighted = (ReadBodyU16LE(p.body, kNpBodyVipWord) == 1);       // @0x56FFD7 -> 5
        actor.isSpecialPkState   = (ReadBodyU32LE(p.body, kNpBodySpecialPk) == 12);    // @0x570006 -> 44
        actor.isAdminTitle       = (ReadBodyU32LE(p.body, kNpBodyAdminTitle) == 1);    // @0x56F6C9
        actor.isAdminTitleAlt    = (ReadBodyU32LE(p.body, kNpBodyAdminTitleAlt) == 1);
        actor.hasTitleBarExtraHeight  = (ReadBodyU32LE(p.body, kNpBodyTitleBarExtra) != 0); // @0x56F115
        actor.suppressExtraNameHeight = (ReadBodyU32LE(p.body, kNpBodySuppressExtra) != 0); // @0x56F124
        actor.affiliationName = ReadBodyCString(p.body, kNpBodyAffiliation, kNpBodyStringMaxLen);
        actor.allianceName    = ReadBodyCString(p.body, kNpBodyAlliance,    kNpBodyStringMaxLen);
        // hpCur/hpMax/mpCur/mpMax (entity+316/+312/+324/+320) NON peuplés : leur unique
        // consommateur est le bloc barres PV/PM, gardé par `a2 == 1` (@0x56EFBF) donc MORT.
        // titleKind / subAffiliationName / whisperName / statusIconA..F : idem, bloc
        // « détaillé » (dword_1668F64 == 1) mort. Ne pas peupler ce qui ne peut être lu.

        const game::NameplateInfo info =
            game::ComputeNameplateInfo(actor, /*drawMode=*/2, /*notSelf=*/true, vctx, host);
        if (info.visible && info.nameBlockVisible && !info.mainLine.text.empty()) {
            const D3DXVECTOR3 labelPos(actor.x, actor.y + info.labelAnchorYOffset, actor.z);
            drawEntityLabel(info.mainLine.text, labelPos,
                            ColorFromNameplateCode(info.mainLine.color), viewProj);
        }
        break;
    }

    // -------------------------------------------------------------------
    // Catégorie 4 — PNJ (pool de rendu g_NpcRenderArray, == g_World.npcRenderEntries après
    // l'unification W7). Fx_MeleeSwingDrawMarker(&g_NpcRenderArray[0x58*idx], /*a2=*/2, idx)
    // @0x531148. Texte = def->name (def+4), couleur = Quest_GetMarkerSpriteBase -> 10.
    // -------------------------------------------------------------------
    case 4: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.npcRenderEntries.size()) break;
        const game::NpcRenderEntry& n = game::g_World.npcRenderEntries[static_cast<size_t>(pickIndex)];
        if (!n.def) break; // `*(_DWORD*)this` déréférencé sans garde par le binaire ; on protège

        game::ZoneNpcLabelRenderState st{};
        st.active      = n.active;
        st.pos         = { n.x, n.y, n.z };
        st.clickRange  = static_cast<int>(n.def->fieldF[1]); // def+1332 (portée/clic, cf. ExtraDatabases.h)
        st.markerDefId = static_cast<int>(n.def->id);        // def+0 (jamais lu par le stub 0x540770)

        const game::ZoneNpcLabelContent lc =
            game::ComputeZoneNpcLabelContent(st, /*drawMode=*/2, vctx.optShowHitMarkers, cull);
        if (lc.visible) {
            const std::string text(n.def->name, ::strnlen(n.def->name, sizeof(n.def->name)));
            if (!text.empty()) {
                drawEntityLabel(text, D3DXVECTOR3(lc.worldPos.x, lc.worldPos.y, lc.worldPos.z),
                                ColorFromNameplateCode(lc.colorCode), viewProj);
            }
        }
        break;
    }

    // -------------------------------------------------------------------
    // Catégorie 5 — MONSTRES (dword_1766F74, stride 280).
    // Char_DrawOverheadName(&dword_1766F74[0x118*idx], idx) @0x531199. Décompilation :
    //   v5 = (float)*(int*)(*((_DWORD*)this + 24) + 252);          // def+252 = drawSize
    //   if (Target_IsBeyondClickRange(this + 8, v5)) {             // near-cull caméra SEUL
    //     v7[1] = (double)(*(def+252) + *(def+260) + 1) + *(this+9);        /*0x5814AE*/
    //     NameplateColor = Npc_GetNameplateColor((int)this);                /*0x5814C3*/
    //     UI_DrawNumberCentered((const char*)(def + 4), v7, NameplateColor);/*0x5814DC*/ }
    // `this+24` (dword) = record+96 = MonsterEntity::def (documenté « +0x60 » dans
    // GameState.h:211). Aucun cull des 300 unités, aucune garde ShowHitMarkers ici.
    // -------------------------------------------------------------------
    case 5: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.monsters.size()) break;
        const game::MonsterEntity& m = game::g_World.monsters[static_cast<size_t>(pickIndex)];
        if (!m.def) break;
        const game::MonsterInfo& mi = *static_cast<const game::MonsterInfo*>(m.def);

        // EntityRenderInfo : drawSize = def+252 = MonsterInfo::collDim[1] (collDim est à +248) ;
        // nameplateExtraOffset = def+260 = MonsterInfo::field260. Ce bloc `info` n'était
        // JAMAIS peuplé par WorldRenderer (écart relevé dans le bandeau WorldRenderer.h) :
        // ComputeOverheadNameContent recevait donc radius=0 et une hauteur de libellé nulle.
        game::EntityRenderInfo rinfo{};
        rinfo.drawSize             = mi.collDim[1];
        rinfo.nameplateExtraOffset = static_cast<int>(mi.field260);

        game::EntityRenderState st{};
        st.active = m.active;
        st.pos    = { m.x, m.y, m.z }; // record+32/36/40 (this+8/9/10)
        st.info   = &rinfo;

        const game::OverheadNameContent oc = game::ComputeOverheadNameContent(st, cull);
        if (oc.visible) {
            const std::string text(mi.name, ::strnlen(mi.name, sizeof(mi.name))); // def+4
            if (!text.empty()) {
                // Npc_GetNameplateColor 0x540790 — DÉJÀ portée (Game/NpcInteraction.cpp:90) et
                // jusqu'ici JAMAIS appelée pour les monstres : c'est LA source de couleur
                // propre à ce libellé (10 = allié/élément apparié, 2 = hostile, 22/33 = écart
                // de puissance), distincte de la palette PK/guilde/GM de Char_DrawNameplate.
                game::NpcQuestContext qctx{};
                qctx.localElement = static_cast<int>(game::g_World.self.element); // g_LocalElement 0x1673194
                // dword_1687320[0] = entity[0]+236 = players[0].body+212 — MÊME champ que le
                // pkLevel lu par le picker sur les autres entités (cf. bandeau des offsets).
                if (!game::g_World.players.empty()) {
                    qctx.factionFlag = static_cast<int>(
                        ReadBodyU32LE(game::g_World.players[0].body, kNpBodyPkLevel));
                }
                // elementLoadout (g_ElementLoadout 0x1685E14) et pairedElement
                // (Char_GetPairedElement 0x557C00) NON portés -> défauts documentés dans
                // Game/NpcInteraction.h ({} et -1). TODO [ancres 0x1685E14 / 0x557C00].
                const int colorCode = game::Npc_GetNameplateColor(
                    m.def, qctx, game::g_World.self.level, game::g_World.self.levelBonus);
                drawEntityLabel(text, D3DXVECTOR3(oc.worldPos.x, oc.worldPos.y, oc.worldPos.z),
                                ColorFromNameplateCode(colorCode), viewProj);
            }
        }
        break;
    }

    // -------------------------------------------------------------------
    // Catégorie 6 — dword_17AB534 (stride 152). AUCUN libellé, VOLONTAIREMENT.
    // Le case 6 du switch (@0x5311A0) ne fait QUE poser la paire de curseurs 5/6
    // (`push 5` @0x5311B9) : il n'appelle AUCUNE fonction de dessin. Et la seule fonction
    // qui dessine un libellé sur ce tableau, Char_DrawNameTag 0x583470, a pour UNIQUE xref
    // @0x52FCD9 — à l'intérieur du bloc mort `dword_1668F64 == 1`.
    // => ce tableau n'a JAMAIS de libellé dans le client d'origine. C'est aussi pourquoi la
    // boucle `g_World.npcs` de Render() n'en émet plus aucun (elle en émettait un avant W9).
    // -------------------------------------------------------------------
    case 6:
        break;

    // -------------------------------------------------------------------
    // Catégorie 7 — OBJETS DE ZONE (g_ZoneObjectArray 0x180EEF4, stride 76).
    // Obj_DrawNameLabel(&g_ZoneObjectArray[19*idx]) @0x531206. Décompilation intégrale :
    //   if (*(_DWORD *)this)                                            /*0x5840CF*/
    //     if (Math_Dist3D((float *)this + 6, flt_1687330) <= 300.0) {   /*0x5840FD*/
    //       Crt_Vsnprintf(v3, "%s", (const char *)this + 49);           /*0x584117*/
    //       v4[0] = *((float *)this + 6);                               /*0x584128*/
    //       v4[1] = *((float *)this + 7) + 12.0;                        /*0x58413A*/
    //       v4[2] = *((float *)this + 8);                               /*0x584146*/
    //       UI_DrawNumberCentered(v3, v4, 7); }                         /*0x58415B*/
    // Aucun near-cull caméra, aucune garde d'option : SEULEMENT la distance <= 300 au joueur
    // local, et une couleur LITTÉRALE 7. pos = record+24/28/32 = body+0/4/8 (le body démarre
    // à record+0x18 — même conversion que World/TerrainPicker.cpp:347) ; texte = record+49 =
    // body+25.
    // -------------------------------------------------------------------
    case 7: {
        if (static_cast<size_t>(pickIndex) >= game::g_World.zoneObjects.size()) break;
        const game::ZoneObjectEntity& z = game::g_World.zoneObjects[static_cast<size_t>(pickIndex)];
        if (!z.active) break; // `if (*(_DWORD*)this)` @0x5840CF

        float zp[3] = { 0.0f, 0.0f, 0.0f };
        std::memcpy(zp, z.body.data(), sizeof(zp)); // record+24/28/32 == body+0/4/8

        // `Math_Dist3D(pos, flt_1687330) <= 300.0` @0x5840FD (joueur local, PAS la caméra).
        const game::Vec3 zpos{ zp[0], zp[1], zp[2] };
        if (game::Distance3D(zpos, cull.localPlayerPos) > game::kSelfProximityDrawDistance) break;

        // Texte = record+49 = body+25, C-string. Borne DÉFENSIVE : jusqu'à la fin du body
        // (52 o, cf. GameState.h:359) — la capacité réelle n'est pas prouvée (le binaire
        // Vsnprintf dans un buffer de 1000 o sans borne d'entrée). Jamais d'overrun.
        const uint8_t* nameStart = z.body.data() + 25;
        const size_t   nameCap   = z.body.size() - 25;
        size_t nameLen = 0;
        while (nameLen < nameCap && nameStart[nameLen] != 0) ++nameLen;
        if (nameLen == 0) break;
        const std::string text(reinterpret_cast<const char*>(nameStart), nameLen);

        // v4[1] = pos.y + 12.0 (@0x58413A) ; couleur LITTÉRALE 7 (@0x58415B — un immédiat
        // `push 7`, PAS une palette calculée, contrairement aux 3 autres libellés). Le 7 est
        // écrit tel quel : il se trouve que game::kNameColorWhisper vaut aussi 7, mais c'est
        // une COÏNCIDENCE de valeur, pas de rôle -> ne pas réutiliser cette constante ici.
        // Tous ces codes vivent dans le MÊME espace (celui de l'argument couleur de
        // UI_DrawNumberCentered 0x53FD00 / UI_DrawNumberValue 0x53FCC0), d'où le même
        // ColorFromNameplateCode pour les 4 familles de libellés.
        drawEntityLabel(text, D3DXVECTOR3(zp[0], zp[1] + 12.0f, zp[2]),
                        ColorFromNameplateCode(7), viewProj);
        break;
    }

    // Catégorie 0 (`Skill_CanCastAtCursor` @0x530FCE) : aucun libellé, seulement une forme de
    // curseur (`push 7` / `push 8`). Rien à dessiner ici.
    case 0:
    default:
        break;
    }
}

// ===========================================================================
//  Render — parcourt game::g_World.players/monsters/npcs
// ===========================================================================

void WorldRenderer::Render(const gfx::Camera& camera) {
    if (!ready_ || !device_) return;
    // La passe ciel SilverLining minimale peut être dessinée avant ce rendu ; elle casse le
    // cache d'état des shaders du device partagé. On invalide donc notre propre cache avant
    // de dessiner les entités.
    meshRenderer_.InvalidateShaderBindingCache();

    D3DXMATRIX view, proj, viewProj;
    camera.BuildViewMatrix(view);
    const float aspect = (screenH_ > 0)
        ? static_cast<float>(screenW_) / static_cast<float>(screenH_)
        : 1.0f;
    camera.BuildProjMatrix(proj, aspect);
    D3DXMatrixMultiply(&viewProj, &view, &proj);

    meshRenderer_.SetCamera(view, proj);
    // Lumière : valeurs par défaut posées par MeshRenderer::Init (cf. MeshRenderer.h) ;
    // pas de branchement sur une lumière de zone réelle ici (TODO Gfx futur).

    game::DrawCullContext cull;
    if (!game::g_World.players.empty()) {
        const game::PlayerEntity& self0 = game::g_World.players[0];
        cull.localPlayerPos = { self0.x, self0.y, self0.z };
    }
    const D3DXVECTOR3 eye = camera.Eye();
    cull.cameraPos = { eye.x, eye.y, eye.z };

    font_.BeginBatch();

    // F_ENTITY3D (B8) : on COLLECTE d'abord toutes les entités à dessiner (players/monsters/npcs)
    // dans un seul tampon, afin de dessiner les OMBRES PLANAIRES (renderPlanarShadows) AVANT les
    // corps opaques (renderOne) — comme le bracket d'ombre 0x52D9DC..0x52DB15 précède l'opaque dans
    // Scene_InGameRender 0x52D0B0. Les 4 boucles ci-dessous ne dessinent plus directement : elles
    // remplissent `drawables` (mêmes gardes active/far-cull qu'avant -> mêmes entités).
    std::vector<DrawableEntity> drawables;

    // Joueurs (index 0 = soi-même, cf. Game/GameState.h). Char_Draw s'applique à
    // toute entité active, y compris le joueur local -> pas de saut d'index ici ;
    // seul `notSelf` (a3 d'origine) distingue le joueur local pour la nameplate.
    for (size_t i = 0; i < game::g_World.players.size(); ++i) {
        const game::PlayerEntity& p = game::g_World.players[i];
        if (!p.active) continue;

        DrawableEntity ent{};
        ent.renderState.active = true;
        ent.renderState.pos    = { p.x, p.y, p.z };
        ent.renderState.hp     = p.hp;
        // Rotation Y (mission ROTATION/ORIENTATION, 2026-07-14) : PlayerEntity::heading
        // (body+252 = move-state+36, degrés), cf. Game/GameState.h et
        // Game/EntityManager.cpp::ReadPlayerPos pour la preuve de décompilation complète
        // (Char_Draw 0x5805C0 + CharAnimState::facingCurrentDeg, même offset). Consommé
        // par ComputeBodyMeshPlacement -> rotDeg.y ci-dessous (renderOne).
        ent.renderState.facingOrAnimTimer = p.heading;
        // TODO(fidélité) : scaleY/info (drawSize/modelCategoryId) ne sont pas encore
        // portés par PlayerEntity (payload +0x18 opaque, cf. Game/GameState.h) -> repli
        // neutre (échelle 1).
        ent.notSelf = (i != 0);
        // Nom REEL (mission NAMEPLATES, 2026-07-14) : PlayerEntity::name, extrait par
        // EntityManager::ReadPlayerName depuis le body reseau (body+48, cf. GameState.h
        // et Game/EntityManager.cpp pour la preuve de decompilation Char_DrawNameplate
        // 0x56EF40). Repli "Player#i"/"Self" UNIQUEMENT si le champ est encore vide (ex.
        // frame avant reception du premier Pkt_SpawnCharacter pour ce slot) -- ne devrait
        // plus arriver en pratique des que le spawn a ete traite.
        ent.name = !p.name.empty() ? p.name : ((i == 0) ? "Self" : ("Player#" + std::to_string(i)));
        ent.placeholderColor = (i == 0) ? 0xFF3070FFu : 0xFF60A0FFu; // bleu (soi plus vif)
        // Arme reelle : joueur local (i==0) via SelfState::equip[7] (slot 7 = arme, deja
        // tenu a jour en continu par les systemes d'equipement, cf. bandeau de tete) ;
        // joueurs distants (i!=0) via PlayerEntity::body+148 (u32 LE), offset RESOLU par
        // decompilation (paire de fonctions jumelles Weapon_ClassFromEquip/
        // Weapon_ClassFromField56, cf. bandeau WorldRenderer.h et kPlayerBodyWeaponItemIdOffset
        // ci-dessus) : ce champ est peuple par le memcpy brut du payload reseau
        // Pkt_SpawnCharacter, donc disponible des le spawn de l'entite distante.
        ent.weaponItemId = (i == 0)
            ? game::g_World.self.equip[7].itemId
            : ReadBodyU32LE(p.body, kPlayerBodyWeaponItemIdOffset);

        // Corps de base reel (mission "cablage corps de base joueur", 2026-07-14, cf.
        // bandeau de tete "JOUEURS" + Gfx/ModelCache.h::GetForPlayerBody) : race/genre/
        // costume lus directement depuis p.body, SANS distinction self/distant (aucun
        // global self separe connu contrairement a l'arme -- cf. bandeau : ces offsets
        // sont mutes EN PLACE dans entity[0], donc dans ce meme body, par
        // Pkt_ItemActionDispatch). Peuple des le spawn (Pkt_SpawnCharacter copie le body
        // en entier).
        ent.hasBody          = true;
        ent.bodyRace         = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyRaceOffset));
        ent.bodyGender       = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyGenderOffset));
        ent.bodyCostumeSlot0 = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyCostumeSlot0Offset));
        ent.bodyCostumeSlot1 = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyCostumeSlot1Offset));

        // ANIMATION JOUEUR (front F_PLAYERANIM, 2026-07-17) — MEME patron que monstres/PNJ (cf.
        // la boucle monstre ci-dessous et renderOne) : animType = etat FSM entity+244
        // (CharAnimState::state, peuple depuis le reseau Game/EntityManager.cpp:390 = body+220),
        // qui SELECTIONNE le clip (PcModel_ResolveEquipSlot 0x4E46A0, base + 156*etat). animCursor =
        // curseur entity+248 (CharAnimState::animFrame), avance par game::Player_AdvanceAnimCursor
        // (Game/PlayerAnimCursorTick.h) en phase UPDATE (frame += dt*30, wrap par soustraction —
        // Char_TickMoveState 0x574830 @0x574922). Vaut pour soi (i==0) ET les distants sans
        // distinction (l'etat vient du meme champ entity+244 pour tous, l'index 0 etant l'alias
        // g_SelfActionState 0x1687328).
        //
        // GARDE DE NON-REGRESSION (blocker recon, cf. game::Player_AnimCursorTickIsWired) : tant que
        // MAIN n'appelle pas Player_AdvanceAnimCursor par frame (Scene/SceneManager.cpp,
        // host.UpdateEntityAnimFrame — cf. rapport de front / integrationForMain), le curseur reste 0
        // et hasAnimCursor=false -> repli SampleByGameTime (clip CORRECT via animType, mais horloge
        // globale). On ne consomme SampleByCursor(animFrame) QUE si le curseur est reellement avance,
        // sinon on figerait TOUS les joueurs a la frame 0 (strictement pire que l'idle en phase).
        ent.animType      = p.anim.state;                       // entity+244, selecteur de clip (reseau)
        ent.animCursor    = p.anim.animFrame;                   // entity+248, curseur reel (avance en UPDATE)
        ent.hasAnimCursor = game::Player_AnimCursorTickIsWired();

        // TRAINEE D'ARME (front F_WEAPONTRAIL, 2026-07-17) — gate maître de
        // Char_DrawWeaponTrailEffect 0x55E9D0 (opaque) / Char_DrawWeaponEffectVariantB 0x56BF90
        // (ombre) : weaponAnimSlot (entity+220 = this+55, id d'anim de skill/arme actif) -> switch
        // game::ResolveWeaponTrailIndex ; altWeaponSet (entity+576 = this+144) doit valoir 0. Lus
        // depuis CharAnimState, valables pour soi (i==0) ET les distants sans distinction (même
        // champ entity+220/+576 pour tous). ⚠ NON alimentés depuis le réseau à ce jour côté
        // ClientSource (CharAnimState::weaponAnimSlot/altWeaponSet ne sont écrits nulle part — cf.
        // rapport de front / integrationForMain) -> weaponAnimSlot vaut 0 -> le gate échoue ->
        // AUCUNE traînée n'est émise (dégradation propre). Se résorbe dès que MAIN peuple ces
        // champs (EntityManager, body+196 / body+552 = entity+220 / entity+576).
        ent.weaponAnimSlot = p.anim.weaponAnimSlot;             // entity+220 = this+55
        ent.altWeaponSet   = p.anim.altWeaponSet;               // entity+576 = this+144
        // reflectionEligible reste false (défaut) : Char_DrawReflection 0x581090 n'est JAMAIS
        // appelée sur g_EntityArray dans le binaire d'origine (appelant unique @0x52DB09, dans
        // la boucle monstre) -> câbler drawReflectionOverlay() ICI serait une invention.
        // MAIS ATTENTION (corrigé Passe 4 / W5, front shadow-wiring) : cela ne veut PAS dire que
        // les joueurs n'ont pas d'ombre dans le client d'origine. Ils en ont une, dessinée dans
        // le même bracket de passe ombre (0x52D9DC GXD_SetupStencilShadowState ..
        // 0x52DB15 GXD_EndStencilShadowState) par Char_DrawWeaponEffectVariantB 0x56BF90
        // @0x52DA41 (`&g_EntityArray[908*i]`), qui rejoint Model_RenderPlanarShadow 0x40F720 via
        // SObject_DrawAnimated2 0x4D91C0. Cette ombre joueur n'est PAS câblée ici : elle exige le
        // plan sol de Collision_SegPickA 0x420D60 (absent de ClientSource) et son étendue dépend
        // du gate this+0xDC [NON VÉRIFIÉ, méga-switch de 11 Ko non décompilé].
        // TODO [ancres 0x56BF90 + 0x40F720 + 0x420D60] : cf. bandeau WorldRenderer.h §Ombre [B].
        // (Ombre joueur : DÉSORMAIS câblée via renderPlanarShadows, cf. bas de Render().)
        drawables.push_back(std::move(ent));
    }

    // Monstres.
    for (size_t i = 0; i < game::g_World.monsters.size(); ++i) {
        const game::MonsterEntity& m = game::g_World.monsters[i];
        if (!m.active) continue;

        DrawableEntity ent{};
        ent.renderState.active = true;
        ent.renderState.pos    = { m.x, m.y, m.z };
        ent.renderState.hp     = m.hp;
        // Rotation Y (mission ROTATION/ORIENTATION, 2026-07-14) : MonsterEntity::heading
        // (body+40 = move-state+36, degrés) -- CONFIRMÉ DIRECTEMENT par décompilation de
        // Char_Draw 0x5805C0 (this = &dword_1766F74[i], cf. Game/GameState.h et
        // Game/EntityManager.cpp::ReadMonsterPos pour la preuve complète).
        ent.renderState.facingOrAnimTimer = m.heading;
        ent.notSelf = true;
        ent.name    = "Monster#" + std::to_string(i);
        ent.placeholderColor = 0xFFE04040u; // rouge
        // monsterDefId = body[0] (mob id -> MONSTER_INFO), MEME convention que
        // Game/EntityManager.cpp::ResolveMobDef (id brut recu du reseau, sans -1) --
        // ModelCache::GetForMonster() applique exactement la meme lecture en interne.
        uint32_t defId = 0;
        std::memcpy(&defId, m.body.data(), sizeof(defId));
        ent.monsterDefId = defId;

        // ANIMATION PAR MONSTRE (Passe 4 / W7, gaps as-motion-01 + as-motion-02) — Char_Draw
        // 0x5805C0 : animType = slot+24 (@0x580770), curseur = slot+28 (@0x580828). Source C++
        // autoritaire = game::g_MonsterTickExt[i] (Game/EntityLifecycleTick.h), dont
        // `.motionState`/`.animFrame` portent EXACTEMENT ces deux offsets et sont désormais
        // alimentés par game::Monster_DispatchMotionTick (Game/AnimationTick.h §5, les 9
        // Char_MotionTick_* + leur dispatch @0x5822D3).
        // PAS `m.anim` (GameState.h:225) : ce CharAnimState est calqué sur des offsets JOUEUR
        // (entity+244/+248) et est MORT pour les monstres — cf. Game/AutoTargetCombatGate.h:106-112.
        //
        // GARDE DE NON-RÉGRESSION (cf. game::Monster_MotionTickIsWired) : tant que le hook
        // EntityLifecycleTickHost::DispatchMotionTick n'est pas assigné (Scene/SceneManager.cpp),
        // le tick ne tourne pas et animFrame resterait bloqué à 0 -> monstres TOTALEMENT FIGÉS,
        // strictement pire que l'horloge globale d'avant. On ne consomme donc le curseur par
        // entité QUE s'il est réellement alimenté ; sinon on conserve l'ancien repli animé.
        // Ce garde n'est PAS un comportement du binaire : à retirer une fois le câblage verrouillé.
        if (i < game::g_MonsterTickExt.size()) {
            const game::MonsterTickExt& mext = game::g_MonsterTickExt[i];
            ent.animType      = mext.motionState; // slot+24 @0x580770
            ent.animCursor    = mext.animFrame;   // slot+28 @0x580828
            ent.hasAnimCursor = game::Monster_MotionTickIsWired();
        }
        // Char_DrawReflection 0x581090 (= ombre planaire du MONSTRE, cf. bandeau WorldRenderer.h
        // §Ombre/reflet [B] -- le nom « reflet » de l'IDB est trompeur) : appelant unique dans
        // tout le binaire, à l'intérieur de CETTE boucle (`&dword_1766F74[280*i]` @0x52DB09 dans
        // Scene_InGameRender). C'est donc bien ici, et uniquement ici, que reflectionEligible
        // doit être posé à true -- les ombres joueur/PNJ passent par d'autres fonctions du même
        // bracket (@0x52DA41 / @0x52DAA2), non câblées.
        ent.reflectionEligible = true;
        drawables.push_back(std::move(ent));
    }

    // NPC GAMEPLAY (game::g_World.npcs, alimenté par Pkt_SpawnNpc opcode 0x13) —
    // NpcEntity::x/y/z (body+16/20/24) confirmés par décompilation Hex-Rays
    // de Char_SelectAuraEffect 0x5835B0 (cf. Game/EntityManager.cpp et
    // Game/GameState.h::NpcEntity) et alimentés par EntityManager::OnSpawnNpc.
    // NPC = jamais "self" pour la nameplate.
    //
    // AUCUN CORPS 3D (RE idaTs2 2026-07-15, mission "PNJ GAMEPLAY SANS CORPS") :
    // ent.bodyMeshEligible=false -> renderOne n'émet ni modèle ni cube. PROUVÉ que
    // l'original ne dessine JAMAIS de corps pour dword_17AB534 : (1) data_refs 0x17AB598
    // (le champ `def` du PNJ réseau) = lu UNIQUEMENT par l'interaction/autoplay
    // (Npc_Interact/AutoPlay_*), par AUCUNE fonction de rendu ; (2) les 3 boucles PNJ
    // réseau de Scene_InGameRender 0x52D0B0 (0x52dc84/0x52ec5b/0x52fcae) n'appellent que
    // Char_DrawAura / Fx_DrawZoneAura / ModelObj_Draw(marqueur de quête) / Char_DrawNameTag
    // -- jamais SObject_DrawEx ni Char_Draw. Le corps 3D des PNJ visibles est
    // EXCLUSIVEMENT rendu par Npc_DrawMesh 0x57FF00 sur le tableau SÉPARÉ
    // g_NpcRenderArray 0x1764D14 (peuplé par Pkt_EnterWorld 0x464160), modélisé par la
    // boucle PNJ DE DÉCOR (ZoneNpcs) juste en dessous. Le cube jaune était donc une
    // INFIDÉLITÉ -> supprimé. reflectionEligible reste false (idem : aucun reflet PNJ).
    //
    // ///// ÉTAT W9 (front nameplate-entity) : CETTE BOUCLE N'ÉMET PLUS RIEN /////
    // Avant W9 elle émettait une plaque de nom (via renderOne). C'était une INFIDÉLITÉ de
    // plus : le binaire ne dessine AUCUN libellé pour dword_17AB534 — sa seule fonction de
    // libellé (Char_DrawNameTag 0x583470) a pour unique xref @0x52FCD9, dans le bloc mort
    // `dword_1668F64 == 1`, et la catégorie de clic 6 que World_PickEntityAtCursor attribue à
    // ce tableau n'a aucun dessin associé (case 6 @0x5311A0 : curseur seul).
    // La boucle est CONSERVÉE (et non supprimée) comme point d'ancrage du seul dessin que le
    // binaire fait réellement sur ce tableau, aujourd'hui NON portable :
    //   TODO [ancre Char_DrawAura 0x583400 @0x52DCB1 (passe 1) / @0x52EC88 (passe 2)] — gap
    //   erp-08 : `if (this && pass in [1,2] && this[27] != -1) ModelObj_Draw(unk_B60AB8 +
    //   148*this[27], pass, 0.0, this+32, this+35)`. BLOQUÉ : ModelObj_Draw 0x4D71B0 et la
    //   table de templates unk_B60AB8 ne sont pas modélisées dans ClientSource, et
    //   game::NpcEntity n'expose pas le champ +27 (id de template d'aura). Effet conditionnel
    //   (seulement si cet id est posé) -> ne rien dessiner reste le comportement correct tant
    //   que la source n'existe pas.
    //
    // ///// FLOTTE C / FRONT C3 — BUT2 : AURA DE ZONE MORPH (Fx_DrawZoneAura 0x583F90) — TODO-ancre.
    // Le binaire dessine une aura de morph sur CE tableau (dword_17AB534) dans le bracket de
    // Scene_InGameRender 0x52D0B0, DEUX fois (Fx_DrawZoneAura @0x52dd70 passe 1 / @0x52ed47 passe 2 —
    // xrefs verifiees IDA). Corps de 0x583F90 (decompile relu cette session) :
    //     if (*(a1) && a3 in [1,2]) {
    //       switch (g_SelfMorphNpcId) { 1:v7=0  6:v7=1  11:v7=2  140:v7=3 }   // sinon v7 reste 0
    //       slot = &unk_B60AB8 + 148*v7 + 34040;  // 34040/148 = 230 -> idxC = 230 + v7 (banque MiscC)
    //       if (ModelObj_GetSubObjectCount(slot,0) > 0) {
    //         frame = ftol(g_GameTimeSec*30) % subCount;
    //         ModelObj_Draw(slot, a3 /*pass*/, frame, a1+24 /*pos*/, &orient0 /*=0,0,0*/, 0); } }
    // => idxC in {230,231,232,233} (E{idxC+1}001.MOBJECT), selectionne par g_SelfMorphNpcId (== zone id
    // == game::g_World.zoneId, DISPONIBLE) mais UNIQUEMENT pour les zones {1,6,11,140}.
    // NON CABLE ICI, a dessein (le consommateur n'est pas atteignable depuis WorldRenderer) :
    //   1. Le rendu passe par la banque MiscC de ModelObjectRenderer (Gfx/ModelObjectRenderer.h, front
    //      F_MOBJ : idxC 230..233 y sont documentes « non routes »). WorldRenderer ne POSSEDE aucun
    //      ModelObjectRenderer et ne peut en recevoir un sans changer sa signature publique appelee par
    //      Scene/SceneManager.cpp (interdit par la contrainte de disjonction C3). De plus le shim
    //      ModelObjectRenderer_MeshDrawShim n'admet qu'UN renderer actif (enregistre par FxRenderer) :
    //      instancier un 2e ici casserait le hook FX mesh existant.
    //   2. Le gate par-entite (`*(a1)` actif + position a1+24) porte sur dword_17AB534 (cette boucle),
    //      mais aucune entite n'a d'aura sans etat de morph runtime -> depend d'un etat non porte.
    // -> BUT2 = documente + TODO-ancre [0x583F90 / ModelObjectRenderer MiscC idxC 230..233 /
    //    g_SelfMorphNpcId]. A cabler par MAIN : router Fx_DrawZoneAura vers ModelObjectRenderer::MeshDraw
    //    (bank MiscC, idxC=230+v7) depuis la passe de scene qui possede le renderer d'objets, pas d'ici.
    for (size_t i = 0; i < game::g_World.npcs.size(); ++i) {
        const game::NpcEntity& n = game::g_World.npcs[i];
        if (!n.active) continue;

        // Far-cull PNJ fidèle à Npc_DrawMesh 0x57FF00 (cf. constante ci-dessus) :
        // absent de ComputeEntityDrawFlags (qui ne modélise que Char_Draw, sans
        // far-cull). Calculé AVANT renderOne, comme dans le binaire (return
        // immédiat si > 1000 unités du joueur local, avant le near-cull caméra).
        const float dx = n.x - cull.localPlayerPos.x;
        const float dy = n.y - cull.localPlayerPos.y;
        const float dz = n.z - cull.localPlayerPos.z;
        if ((dx * dx + dy * dy + dz * dz) > kNpcFarCullDistanceSq) continue;

        DrawableEntity ent{};
        ent.renderState.active   = true;
        ent.renderState.pos      = { n.x, n.y, n.z };
        ent.renderState.hp       = 0; // TODO(fidélité) : NpcEntity ne porte pas de PV/barre.
        ent.notSelf              = true;
        ent.bodyMeshEligible     = false; // pas de corps (cf. bandeau) -> nameplate seule
        // Nom RÉEL du PNJ = ITEM_INFO.name (+4, 25 o cstring) via NpcEntity::def, désormais
        // résolu contre la table ITEM_INFO (EntityManager::ResolveNpcDef = MobDb_GetEntry
        // (mITEM), cf. Pkt_SpawnNpc 0x467EC0). Aucune fabrication : si def est nul ou le nom
        // vide, on laisse ent.name vide -> pas de nameplate (au lieu de l'ancien "Npc#i").
        if (n.def) {
            const char* nm = reinterpret_cast<const char*>(n.def) + 4; // ITEM_INFO.name
            ent.name.assign(nm, ::strnlen(nm, 25));
        }
        // PNJ gameplay : bodyMeshEligible=false -> ni corps ni ombre (renderPlanarShadows saute).
        drawables.push_back(std::move(ent));
    }

    // PNJ DE DÉCOR (game::ZoneNpcs(), Game/StaticNpcLoader.h) — SOURCE DISTINCTE de la
    // boucle PNJ gameplay ci-dessus (cf. bandeau de tête WorldRenderer.h §"PNJ" pour la
    // preuve complète) :
    //   - g_World.npcs (boucle précédente) = tableau GAMEPLAY, alimenté par le paquet
    //     réseau Pkt_SpawnNpc (opcode 0x13) -- interaction/ciblage, VIDE en pratique pour
    //     les PNJ de décor (marchands, gardes, donneurs de quête...) qui n'ont AUCUN
    //     paquet réseau dédié.
    //   - game::ZoneNpcs() (cette boucle) = ÉQUIVALENT client-source de g_NpcRenderArray
    //     0x1764D14, repeuplé localement depuis la table statique mZONENPCINFO par
    //     StaticNpcLoader::LoadZoneNpcs() (déclenché par EntityManager::OnSpawnCharacter
    //     sur le spawn du joueur local, cf. bandeau de tête de StaticNpcLoader.h) -- c'est
    //     LA SOURCE RÉELLE des PNJ de décor visibles en jeu dans le binaire d'origine
    //     (Npc_DrawMesh 0x57FF00 ne lit QUE g_NpcRenderArray, jamais dword_17AB534).
    // MÊME pipeline de rendu que toutes les autres entités (renderOne()) : modèle réel via
    // ModelCache::GetForNpc(*def) si le kindIndex du PNJ (NpcDefRecord::fieldE) résout un
    // fichier N*.SOBJECT sur disque (cf. ResolveNpcModel ci-dessus), repli cube JAUNE
    // (même couleur que la boucle gameplay, pour rester visuellement cohérent) sinon --
    // jamais d'écran vide. `def` n'est jamais nul ici : StaticNpcLoader::LoadZoneNpcs()
    // n'ajoute un slot à ZoneNpcs() QUE si GetNpcDefRecord(kindId) a réussi (cf.
    // StaticNpcLoader.cpp).
    for (size_t i = 0; i < game::ZoneNpcs().size(); ++i) {
        const game::StaticNpcSlot& n = game::ZoneNpcs()[i]; // = NpcRenderEntry (pool unique W7)

        // W7 « npc-array-unify » : ZoneNpcs() renvoie 100 slots FIXES, dont des TROUS inactifs
        // (def==nullptr, pos 0,0,0). Le contrat impose de tester `active` avant tout usage (cf.
        // Game/StaticNpcLoader.h / GameState.h) -- fidèle au garde `*(this+1)` de Npc_DrawMesh
        // 0x57FF00 / Npc_RenderSlotTick 0x5803A0. AVANT W7, ZoneNpcs() était compacté (que des
        // slots occupés) : sans cette garde on dessinerait désormais des cubes fantômes à l'origine.
        if (!n.active) continue;

        // Même far-cull PNJ que la boucle gameplay ci-dessus (Npc_DrawMesh 0x57FF00, cf.
        // kNpcFarCullDistanceSq en tête de fichier) : ce garde s'applique à TOUT PNJ
        // dessiné via ce pipeline, quelle que soit sa source de données (gameplay ou décor)
        // -- Npc_DrawMesh est LA fonction de dessin des DEUX (g_NpcRenderArray porte les
        // deux catégories de PNJ côté binaire d'origine, cf. Docs/TS2_NPC_MESH_DRAW.md).
        const float dx = n.x - cull.localPlayerPos.x;
        const float dy = n.y - cull.localPlayerPos.y;
        const float dz = n.z - cull.localPlayerPos.z;
        if ((dx * dx + dy * dy + dz * dz) > kNpcFarCullDistanceSq) continue;

        DrawableEntity ent{};
        ent.renderState.active = true;
        ent.renderState.pos    = { n.x, n.y, n.z };
        // ANIMATION PAR PNJ (gaps as-motion-01 + as-motion-02) — Npc_DrawMesh 0x57FF00 :
        // animType = slot+12 (@0x57ffa0, arg 3 de Model_GetNpcMeshSlot), curseur = slot+16
        // (@0x57fff1, animTime de SObject_DrawEx), angle affiché = slot+44. ADAPTATION W7 : ces
        // trois valeurs sont les champs NATIFS du pool unifié NpcRenderEntry (mode/frameAcc/angle),
        // alimentés par game::ZoneNpc_TickAnim (Game/AnimationTick.h §6, portage de
        // Npc_RenderSlotTick 0x5803A0 / _Loop 0x580400 / _Once 0x5804A0) qui tick DIRECTEMENT le
        // pool -- plus de vecteur parallèle. L'angle (+44) est le SEUL mutable : tour vers le
        // joueur à l'ouverture du dialogue (@0x5dc0a2), reset sur la baseline au-delà de 400 u
        // (@0x58048e). Consommé par ComputeBodyMeshPlacement -> rotDeg.y (renderOne), même canal
        // que PlayerEntity::heading/MonsterEntity::heading.
        ent.renderState.facingOrAnimTimer = n.angle;    // slot+44
        ent.animType   = n.mode;                        // slot+12
        ent.animCursor = n.frameAcc;                    // slot+16
        // MÊME garde de non-régression que la boucle monstre (cf. game::Monster_MotionTickIsWired) :
        // game::ZoneNpc_TickAnim n'est à ce jour appelé NULLE PART (câblage à poser par
        // l'orchestrateur dans Scene/SceneManager.cpp après InGameTickFlow_Update, cf.
        // Game/AnimationTick.h §6). Tant qu'il ne tourne pas, frameAcc reste 0 -> consommer le
        // curseur figerait les PNJ sur la 1re frame, alors que l'ancien repli SampleByGameTime les
        // animait (en phase). On ne consomme donc le curseur par-entité QUE s'il est réellement
        // alimenté. Se résorbe dès le câblage posé.
        ent.hasAnimCursor = game::ZoneNpc_AnimTickIsWired();
        ent.renderState.hp = 0; // TODO(fidélité) : pas de barre de vie pour un PNJ de décor.
        ent.notSelf = true;
        // Nom réel (NpcDefRecord::name, 25 o cstring) si disponible ; repli "ZoneNpc#i"
        // dans le cas (normalement impossible ici, cf. garde LoadZoneNpcs) où def serait nul.
        ent.name = (n.def && n.def->name[0] != '\0')
            ? std::string(n.def->name, ::strnlen(n.def->name, sizeof(n.def->name)))
            : ("ZoneNpc#" + std::to_string(i));
        ent.placeholderColor = 0xFFF0E020u; // jaune -- même couleur que la boucle PNJ gameplay
        // Modèle réel : npcDef non-nul -> ResolveNpcModel()/ModelCache::GetForNpc() dans
        // renderOne (cf. bandeau de tête WorldRenderer.h §"PNJ") ; repli cube si fieldE
        // hors bornes [1,66] ou fichier introuvable sur disque (jamais d'exception).
        ent.npcDef = n.def;
        // reflectionEligible reste false (défaut) : Char_DrawReflection 0x581090 n'est jamais
        // appelée sur un tableau PNJ -- même règle que la boucle PNJ gameplay.
        // Ombre planaire du PNJ DE DÉCOR (Npc_DrawMeshGlow 0x5801D0 @0x52DAA2 dans le bracket
        // 0x52D9DC..0x52DB15 -> SObject_DrawAnimated2 0x4D91C0 -> Model_RenderPlanarShadow 0x40F720) :
        // DÉSORMAIS câblée (front F_ENTITY3D / B8), via renderPlanarShadows (bas de Render()).
        // game::ZoneNpcs() EST l'équivalent client-source de g_NpcRenderArray (stride 88) que lit
        // Npc_DrawMeshGlow -> la bonne source. reflectionEligible reste false (non lu par la passe).
        drawables.push_back(std::move(ent));
    }

    // ///// PASSE OMBRE PLANAIRE — Vague B / branchement B8 (front F_ENTITY3D) /////
    // AVANT les corps opaques (bracket 0x52D9DC..0x52DB15 dessine les ombres avant l'opaque dans
    // Scene_InGameRender 0x52D0B0). No-op propre si collisionSource_ non posée (plan-sol absent).
    renderPlanarShadows(drawables, cull);

    // ///// CORPS OPAQUES — renderOne par entité (Char_Draw 0x5805C0), APRÈS les ombres /////
    for (const DrawableEntity& d : drawables)
        renderOne(d, cull, view, proj, viewProj);

    // ///// PASSE LIBELLÉ — Passe 4 / vague W9, front nameplate-entity /////
    // APRÈS les 4 boucles de corps, comme dans Scene_InGameRender 0x52D0B0 où les libellés
    // sont émis dans le bloc 2D ouvert par Gfx_Begin2D @0x52FB89, une fois toutes les passes
    // 3D terminées — et JAMAIS depuis Char_Draw. Émet AU PLUS UN libellé par frame (celui de
    // l'entité sous le curseur), fidèle au switch @0x530FC7.
    drawNameplatePass(camera, cull, viewProj);

    font_.EndBatch();
}

} // namespace ts2

// Scene/WorldRenderer.cpp — voir WorldRenderer.h pour le périmètre exact du câblage.
#include "Scene/WorldRenderer.h"
#include "Gfx/Renderer.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/NameplateLogic.h"
#include "Game/StaticNpcLoader.h" // PNJ de decor (mission "PNJ DECOR VISIBLES A L'ECRAN", cf. Render())
#include "Gfx/MotionCache.h"      // palette d'os animee (miroir g_ModelMotionArray 0x8E8B30) — W3-F1
#include "Gfx/PlayerPaperdoll.h"  // paperdoll joueur (calque Char_RenderModel 0x527020) — W3-F1
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

} // namespace

// ===========================================================================
//  Init / Shutdown
// ===========================================================================

bool WorldRenderer::Init(gfx::Renderer& renderer, int screenW, int screenH) {
    device_  = renderer.Device();
    screenW_ = screenW;
    screenH_ = screenH;
    if (!device_) { TS2_ERR("WorldRenderer::Init : device nul"); return false; }

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
    device_ = nullptr;
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
}

void WorldRenderer::drawReflectionOverlay(const gfx::SkinnedModel* bodyModel, const D3DXVECTOR3& pos,
                                          float scale, float rotYDeg, const D3DXMATRIX& view,
                                          const D3DXMATRIX& proj) {
    if (!device_ || !bodyModel || bodyModel->Empty()) return;
    (void)view;
    (void)proj;

    // Char_DrawReflection redessine le MÊME modèle que le corps, à la même
    // transformée ; on réutilise donc la géométrie réelle déjà résolue par
    // ModelCache, plutôt qu'un cube placeholder.
    // Écart de fidélité assumé : la vraie recette de blend d'origine dépend
    // d'un setup stencil shadow dédié, non reproduit ici. On laisse le mesh
    // réel apparaître avec sa pipeline skinnée standard, sans cube de repli.
    gfx::BonePalette palette;
    const float sz = (scale > 0.05f) ? scale : 1.0f;
    meshRenderer_.DrawModel(*bodyModel, pos, D3DXVECTOR3(0.0f, rotYDeg, 0.0f),
                            D3DXVECTOR3(sz, sz, sz), palette);
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
//  Rendu d'une entité (EntityDrawLogic pour la décision, NameplateLogic pour le nom)
// ===========================================================================

void WorldRenderer::renderOne(const DrawableEntity& ent, const game::DrawCullContext& cull,
                              const D3DXMATRIX& view, const D3DXMATRIX& proj,
                              const D3DXMATRIX& viewProj) {
    // Char_Draw 0x5805C0 : a2=1 (passe principale, cf. Scene_InGameRender).
    // showShadow/showReflection : voir bandeau WorldRenderer.h — showShadow n'est
    // JAMAIS dessiné (Char_DrawShadow 0x580CE0 = 0 xref, code mort confirmé) ;
    // showReflection l'est, via drawReflectionOverlay ci-dessous.
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

    // Palette d'os ANIMÉE (remplace l'ancienne palette IDENTITÉ) — échantillonnée par
    // g_World.gameTimeSec. Reprend la séquence Char_Draw 0x5805C0 -> SObject_DrawEx 0x4D9330
    // (Motion_GetData 0x4D78C0 = motionSlot+136) -> Model_Render 0x40EBB0 (frame = ftol(animTime),
    // borné 0..frameCount-1). animType idle=0 : pose de base de Char_Draw ; la vraie horloge
    // (entity+7) est pilotée par la FSM Char_UpdateAnimationFrame 0x571880, JAMAIS câblée sur les
    // entités de rendu (cf. GameState.h) -> échantillonnage 30 fps par g_World.gameTimeSec (idiome
    // Char_RenderModel 0x528d38). TODO [ancre 0x571880] : animType réel depuis la FSM d'action.
    gfx::BonePalette palette; // repli identité si aucune MOTION ne résout
    if (ent.monsterDefId != 0) {
        // Model_GetNpcMotionSlot 0x4E5960 (monstre, stride 3276).
        if (const gfx::MotionPalette* mp = Motions().GetForMonster(ent.monsterDefId, /*anim idle*/0))
            palette = gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    } else if (ent.npcDef) {
        // Model_GetNpcMeshSlot 0x4E5910 (PNJ de décor, stride 468).
        if (const gfx::MotionPalette* mp = Motions().GetForNpc(*ent.npcDef, /*anim idle*/0))
            palette = gfx::MotionCache::SampleByGameTime(*mp, game::g_World.gameTimeSec);
    }

    // PNJ GAMEPLAY : bodyMeshEligible=false -> AUCUN corps ni cube (l'original ne dessine
    // jamais de mesh pour dword_17AB534, cf. DrawableEntity::bodyMeshEligible). Seule la
    // nameplate plus bas est émise pour ces entités.
    if (ent.bodyMeshEligible) {
        if (ent.hasBody) {
            // JOUEUR — PlayerPaperdoll (calque Char_RenderModel 0x527020) : UNE palette d'os
            // animée PARTAGÉE (PcModel_ResolveEquipSlot 0x4E46A0) + liste ordonnée de pièces
            // (corps SLOT0 flt_F59A7C / SLOT1 flt_F5B21C + arme). Remplace l'ancien corps
            // 2-pièces inline ET l'ancien hack d'arme (wpos = pos.y + scale*0.6). L'arme est
            // désormais une pièce dessinée à la MÊME transformée + MÊME palette que le corps
            // (Char_RenderModel 0x527bfe : arme skinnée au bone de main via v37), pas un offset.
            gfx::PaperdollResult pd = gfx::PlayerPaperdoll::Resolve(
                *modelCache_, Motions(), ent.bodyRace, ent.bodyGender,
                ent.bodyCostumeSlot0, ent.bodyCostumeSlot1, ent.weaponItemId,
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

    // Reflet (Char_DrawReflection 0x581090, RÉSERVÉ AUX MONSTRES -- vérification
    // approfondie 2026-07-14, mission "EXTENSION OMBRE/REFLET", cf. bandeau
    // WorldRenderer.h § Ombre/reflet). `xrefs_to(0x581090)` = un seul appelant dans
    // tout le binaire, dans la boucle monstre de Scene_InGameRender 0x52D0B0
    // (`&dword_1766F74[70*i]`) -- jamais sur g_EntityArray (joueurs) ni le tableau
    // PNJ : `ent.reflectionEligible` matérialise cette garde de type d'entité, en
    // plus des gardes distance/near-cull déjà dans `flags.showReflection`
    // (Char_DrawReflection les applique TOUTES les deux, dans cet ordre, avant de
    // dessiner). Le décompilé redessine le MÊME modèle à la MÊME position/échelle
    // que le corps (this+7/+8/+14, identiques à ComputeBodyMeshPlacement) -> pas de
    // mirroir aplati au sol ici, juste une 2e passe translucide/teintée au même
    // endroit (cf. bandeau WorldRenderer.h pour la recette de blend d'origine et
    // l'écart assumé de fidélité pixel).
    if (ent.reflectionEligible && flags.showReflection) {
        // Diagnostic one-shot (mission "EXTENSION OMBRE/REFLET", 2026-07-14) : confirme
        // au runtime que la passe reflet se déclenche bel et bien pour au moins une
        // entité (toujours un monstre, cf. garde ci-dessus) -- une seule ligne sur
        // toute la durée de vie du process, jamais de spam par frame.
        static bool s_reflectionOnceLogged = false;
        if (!s_reflectionOnceLogged) {
            s_reflectionOnceLogged = true;
            TS2_LOG("WorldRenderer: reflet dessine (Char_DrawReflection, entite '%s') -- "
                    "premiere occurrence, confirme le declenchement reel.", ent.name.c_str());
        }
        drawReflectionOverlay(bodyModel, pos, scale, rotDeg.y, view, proj);
    }

    // Nameplate — game::ComputeNameplateInfo (Char_DrawNameplate 0x56EF40) réel :
    // NameplateHost par défaut (callbacks non branchés -> replis documentés dans
    // NameplateLogic.h, ex. CanTargetOnMap=false => jamais traité comme hostile).
    game::NameplateActor actor{};
    actor.active      = ent.renderState.active;
    actor.hasIdentity = true; // TODO(fidélité) : identité réseau non testée séparément ici
    actor.x = ent.renderState.pos.x;
    actor.y = ent.renderState.pos.y;
    actor.z = ent.renderState.pos.z;
    actor.name = ent.name;

    game::NameplateViewerContext vctx{};
    vctx.selfX = cull.localPlayerPos.x;
    vctx.selfY = cull.localPlayerPos.y;
    vctx.selfZ = cull.localPlayerPos.z;

    const game::NameplateHost host{}; // tous callbacks nuls -> replis par défaut documentés
    const game::NameplateInfo info = game::ComputeNameplateInfo(actor, /*drawMode=*/1, ent.notSelf, vctx, host);
    if (info.visible && info.nameBlockVisible && !info.mainLine.text.empty()) {
        const D3DXVECTOR3 labelPos(actor.x, actor.y + info.labelAnchorYOffset, actor.z);
        drawEntityLabel(info.mainLine.text, labelPos, ColorFromNameplateCode(info.mainLine.color), viewProj);
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
        // reflectionEligible reste false (défaut) : Char_DrawReflection 0x581090
        // n'est JAMAIS appelée sur g_EntityArray dans le binaire d'origine (un seul
        // appelant au total, dans la boucle monstre -- cf. bandeau WorldRenderer.h
        // "VÉRIFICATION APPROFONDIE 2026-07-14"). Le joueur local et les joueurs
        // distants n'ont donc jamais de reflet dans le client d'origine ; ne pas
        // câbler drawReflectionOverlay() ici serait la version FIDÈLE, pas une
        // lacune.
        renderOne(ent, cull, view, proj, viewProj);
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
        // Reflet (Char_DrawReflection 0x581090) : MONSTRES SEULEMENT, cf. bandeau
        // WorldRenderer.h "VÉRIFICATION APPROFONDIE 2026-07-14" -- appelant unique
        // dans tout le binaire, à l'intérieur de CETTE boucle (`&dword_1766F74[70*i]`
        // dans Scene_InGameRender). C'est donc bien ici, et uniquement ici, que
        // reflectionEligible doit être posé à true.
        ent.reflectionEligible = true;
        renderOne(ent, cull, view, proj, viewProj);
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
        renderOne(ent, cull, view, proj, viewProj);
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
        const game::StaticNpcSlot& n = game::ZoneNpcs()[i];

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
        // Angle affiché initial (mZONENPCINFO+0x644, cf. StaticNpcLoader.h) -- consommé
        // par ComputeBodyMeshPlacement -> rotDeg.y dans renderOne, même canal que
        // PlayerEntity::heading/MonsterEntity::heading.
        ent.renderState.facingOrAnimTimer = n.angle;
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
        // reflectionEligible reste false (défaut) : Char_DrawReflection 0x581090 n'est
        // jamais appelée sur un tableau PNJ (cf. bandeau "VÉRIFICATION APPROFONDIE
        // 2026-07-14" ci-dessus, § Ombre/reflet) -- même règle que la boucle PNJ gameplay.
        renderOne(ent, cull, view, proj, viewProj);
    }

    font_.EndBatch();
}

} // namespace ts2

// Game/EntityDrawLogic.h — logique de décision du dispatcher de rendu d'entité.
//
// Réécriture C++ PROPRE (pas byte-exact — cf. règle de périmètre : le rendu 3D
// pixel-exact est hors fidélité, seules les FORMULES/conditions de gameplay le
// sont) des 5 fonctions relevées dans le désassemblage :
//   Char_Draw             0x5805C0  (~0x6F1 o)  — dispatcher principal par entité
//   Char_DrawShadow        0x580CE0  (~0x3A4 o)  — passe ombre
//   Char_DrawReflection    0x581090  (~0x3A4 o)  — passe reflet (miroir/eau)
//   Char_DrawOverheadName  0x581440  (~0xA7 o)   — nombre/texte flottant "cliquable"
//   Char_DrawNameTag       0x583470  (~0x133 o)  — nom(+niveau) au-dessus de la tête
//
// HORS PÉRIMÈTRE (mentionnées ici pour traçabilité uniquement, PAS décompilées —
// trop volumineuses, effets visuels purs, cf. règle de périmètre) :
//   Char_DrawWeaponTrailEffect      0x55E9D0 (~0x9F7A o, ~40 Ko)
//   Char_DrawWeaponEffectVariantA   0x568FE0 (~0x2AFF o, ~11 Ko)
//   Char_DrawWeaponEffectVariantB   0x56BF90 (~0x2AFF o, ~11 Ko)
//   // TODO(rendu) : traînées d'armes — shaders/particules, à brancher plus tard
//   // dans la couche Gfx sans logique de décision associée ici.
//
// CE FICHIER NE FAIT AUCUN RENDU D3D : pas de VB/IB/shader/texture. Il expose des
// fonctions PURES (déterministes, sans état, sans I/O) qui transforment un
// instantané de l'entité + du contexte caméra en DÉCISIONS typées (quoi dessiner,
// avec quel seuil de distance, dans quel ordre). La couche Gfx (futur
// EntityRenderer côté Gfx/) consommera ces décisions pour émettre les vrais appels
// D3D9 (ModelObj_Draw / SObject_DrawEx / SObject_DrawAnimated[2] d'origine).
//
// IMPORTANT — l'objet `this` des 5 fonctions d'origine n'est PAS game::PlayerEntity
// / MonsterEntity / NpcEntity de GameState.h (qui sont les enregistrements réseau
// des tableaux d'entités). C'est un objet de rendu 3D séparé (style "cCharObj"),
// alloué/rafraîchi par le moteur GXD à partir de ces enregistrements, qui porte en
// plus la position interpolée, l'état d'attache (monture), les échelles d'anim et
// les indicateurs d'effets attachés. Ses champs sont reconstruits ci-dessous en
// EntityRenderState avec les offsets d'origine (en octets, relatifs à `this`) —
// c'est à la couche Gfx de peupler cette vue à partir de son objet interne réel.
//
// Convention : les commentaires "+0xNN" citent l'offset d'origine ; "(this+N)"
// cite la forme telle que vue dans le pseudocode Hex-Rays (this+N en unités de
// 4 octets, qu'il s'agisse de (_DWORD*)this+N ou (float*)this+N).
#pragma once
#include <cstdint>
#include <string>

namespace ts2::game {

// Vecteur 3D minimal, indépendant du moteur de rendu (pas de dépendance D3DX ici
// pour garder ce module testable/pur — la couche Gfx convertira vers D3DXVECTOR3).
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Distance euclidienne 3D pleine (Math_Dist3D 0x53FAA0).
float Distance3D(const Vec3& a, const Vec3& b);

// -----------------------------------------------------------------------------
// Gate de culling "proche caméra" (Target_IsBeyondClickRange 0x5410D0).
// -----------------------------------------------------------------------------
// Nom d'origine trompeur : cette fonction sert À LA FOIS de test de portée pour le
// clic souris (picking de cible) ET, réutilisée telle quelle ici, de garde de
// rendu "pas trop près/derrière la caméra". Formule exacte relevée :
//   dx = cameraPos.x - pos.x
//   dy = cameraPos.y - (pos.y + radius*0.5)   <- décalage vertical au milieu du modèle
//   dz = cameraPos.z - pos.z
//   renvoie sqrt(dx²+dy²+dz²) >= 10.0
// `radius` = info.drawSize (cf. EntityRenderState::Info::drawSize) converti en
// float par l'appelant, comme dans le désassemblage d'origine.
bool IsBeyondCameraNearCull(const Vec3& pos, float radius, const Vec3& cameraPos);

// -----------------------------------------------------------------------------
// Bloc "info" partagé (this+24 en dword*, soit +96 o) décrivant le modèle/costume
// courant et ses métadonnées de rendu. Pointé en commun par les 5 fonctions.
// -----------------------------------------------------------------------------
struct EntityRenderInfo {
    int modelCategoryId     = 0; // +0x00 : id costume/modèle courant.
                                  //   [589..600]  = plage réservée aux morphs de compétence de tribu
                                  //   {1141..1144} = plage "variante arme spéciale" (alive/dead)
    int weaponRenderType    = 0; // +0xE8 (+232) : 2 = arme à paliers d'usure animés selon le ratio PV
    int motionIndex         = 0; // +0xF4 (+244) : index modèle/motion, indexe les tables d'assets
    int drawSize            = 0; // +0xFC (+252) : taille/rayon entier — utilisé (a) converti en float
                                  //   comme rayon de IsBeyondCameraNearCull et (b) sommé en int brut
                                  //   avec nameplateExtraOffset pour la hauteur du nom flottant
    int nameplateExtraOffset = 0; // +0x104 (+260) : décalage vertical additionnel pour le nom flottant
    int maxHp                = 0; // +0x170 (+368) : PV max, dénominateur du ratio d'usure de l'arme
};

// -----------------------------------------------------------------------------
// Objet de rendu d'entité (this des Char_Draw / Char_DrawShadow / Char_DrawReflection
// / Char_DrawOverheadName — les 4 premières fonctions partagent EXACTEMENT ce layout,
// vérifié offset par offset dans le pseudocode). Char_DrawNameTag utilise un objet
// DIFFÉRENT (cf. NameTagRenderState plus bas).
// -----------------------------------------------------------------------------
struct EntityRenderState {
    bool  active = false;    // +0x00 : slot vivant (this[0] != 0) ; garde commune à tout le dispatcher

    Vec3  pos{};              // +0x20..+0x28 (this+8..+10) : position monde courante
    // CORRECTION mission ROTATION/ORIENTATION (2026-07-14, décompilation directe de
    // Char_Draw 0x5805C0 + SObject_DrawEx 0x4D9330 + rôle IDB de Model_Render 0x40EBB0
    // "compose S*Rz*Ry*Rx*T", cf. Game/GameState.h::MonsterEntity::heading pour la preuve
    // complète) : this+7 = animFrame (move-state+8, PAS un angle) ; this+14 = heading
    // (move-state+36, cap horizontal en DEGRÉS, injecté comme rotation Y — le vecteur
    // d'échelle est câblé en DUR à {1,1,1} dans SObject_DrawEx, donc this+14 N'EST PAS
    // une échelle). Les libellés ci-dessous datent d'une passe antérieure sans accès à
    // Model_Render (serveur MCP saturé) et sont donc INVERSÉS par rapport à la réalité ;
    // conservés SANS renommer les champs (Scene/WorldRenderer.cpp les peuple déjà
    // correctement : facingOrAnimTimer <- PlayerEntity/MonsterEntity::heading, PAS un
    // minuteur d'anim). scaleY reste à 0.0f partout (jamais peuplé), ce qui dégrade
    // proprement vers l'échelle de repli 1.0 côté WorldRenderer::renderOne — sans effet
    // visible malgré le libellé erroné.
    float facingOrAnimTimer = 0.0f; // +0x1C (this+7 en réalité) : voir note ci-dessus — utilisé comme heading (degrés), pas comme minuteur
    float scaleY = 0.0f;      // +0x38 (this+14 en réalité) : voir note ci-dessus — PAS l'échelle verticale ; jamais peuplé (repli 1.0)

    int   hp = 0;             // +0x5C (this+23) : PV courants — pilote la variante arme vivante/morte
                               //   et le palier d'usure (voir ComputeWeaponOverlayVariant)
    int   stateCategory = 0;  // +0x18 (this+6) : code de catégorie d'état -> overlay associé (voir
                               //   ClassifyStateOverlay) ; PAS le même champ que weaponRenderType

    bool  attached = false;   // +0xD4 (this+53) : monté/attaché à un parent (mount, effet porté...)
    Vec3  attachPos{};        // +0xF0..+0xF8 (this+60..+62) : position relative si attaché
    float attachScale = 0.0f; // +0xFC (this+63) : échelle/valeur si attaché

    // 3 emplacements d'effets attachés fixes (aura/aile/monture...), chacun avec un
    // drapeau actif + une échelle. Ordre de dessin = ordre déclaré ci-dessous.
    bool  effectSlot1Active = false; float effectSlot1Scale = 0.0f; // +0x100/+0x104 (this+64/65) -> modèle fixe #1
    bool  effectSlot2Active = false; float effectSlot2Scale = 0.0f; // +0x108/+0x10C (this+66/67) -> modèle fixe #2
    bool  effectSlot3Active = false; float effectSlot3Scale = 0.0f; // +0x110/+0x114 (this+68/69) -> modèle fixe #3

    const EntityRenderInfo* info = nullptr; // +0x60 (this+24, dword*) : bloc info partagé (peut être nul)
};

// -----------------------------------------------------------------------------
// Objet "étiquette de nom" (this de Char_DrawNameTag 0x583470) — structure
// DISTINCTE de EntityRenderState (offsets différents : pos à +128 et non +32,
// pointeur "owner" à +100 et non +96, aucun test IsBeyondCameraNearCull ici).
// -----------------------------------------------------------------------------
struct NameTagRenderState {
    bool active = false;  // +0x00
    Vec3 pos{};            // +0x80..+0x88 (this+128/132/136) : position du nom (avant décalage Y)
    int  level = 0;        // +0x14 (this+20) : niveau affiché entre parenthèses

    struct OwnerInfo {
        int nameDisplayMode = 0; // +0xBC (+188) : 1 ou 2 => affiche "nom(niveau)", sinon "nom" seul
        // +0x04 : pointeur chaîne C du nom (résolu par l'appelant, pas modélisé ici)
    };
    const OwnerInfo* owner = nullptr; // +0x64 (this+100)
    std::string ownerName; // nom déjà résolu par l'appelant (owner+4 dans l'original)
};

// -----------------------------------------------------------------------------
// Contexte de culling partagé par la frame courante.
// -----------------------------------------------------------------------------
struct DrawCullContext {
    Vec3 cameraPos{};      // g_CameraPos (0x800130, vecteur x/y/z) : utilisé par IsBeyondCameraNearCull
    Vec3 localPlayerPos{}; // flt_1687330 : position du joueur local — utilisée par le cull "300 unités"
                            //   (Char_DrawShadow / Char_DrawReflection / Char_DrawNameTag)
};

inline constexpr float kSelfProximityDrawDistance = 300.0f; // seuil dur relevé dans les 3 fonctions concernées

// -----------------------------------------------------------------------------
// Décisions de visibilité (une par passe de rendu). `drawPass` = paramètre a2
// d'origine de Char_Draw, forwardé tel quel au futur renderer (1 ou 2 = passes
// valides ; en dehors -> aucun rendu). Les 4 autres fonctions d'origine ignorent
// ce paramètre (a2/a3 présents dans leur signature mais jamais lus).
// -----------------------------------------------------------------------------
struct EntityDrawFlags {
    bool showBody       = false; // Char_Draw : corps + arme + overlays d'état
    bool showShadow     = false; // Char_DrawShadow
    bool showReflection = false; // Char_DrawReflection
    bool showOverheadName = false; // Char_DrawOverheadName (nombre/texte flottant "cliquable")
    bool showNameTag    = false; // Char_DrawNameTag — calculé séparément (objet différent),
                                  // exposé ici seulement pour un agrégat pratique côté appelant.
};

// Calcule showBody / showShadow / showReflection / showOverheadName à partir du
// MÊME objet EntityRenderState (les 4 fonctions d'origine partagent ce layout).
// showNameTag reste à false ici : voir ComputeNameTagContent (objet différent).
//   showBody       = active && drawPass∈[1,2] && IsBeyondCameraNearCull(pos, info.drawSize, cam)
//   showShadow     = active && Distance3D(pos, self) <= 300 && IsBeyondCameraNearCull(...)
//   showReflection = idem showShadow (mêmes gardes, dessin différent)
//   showOverheadName = active && IsBeyondCameraNearCull(pos, info.drawSize, cam)  [PAS de cull 300]
EntityDrawFlags ComputeEntityDrawFlags(const EntityRenderState& state,
                                        const DrawCullContext& cull,
                                        int drawPass);

// -----------------------------------------------------------------------------
// Char_DrawOverheadName 0x581440 — position du texte/nombre flottant.
// v7 = { pos.x, (info.drawSize + info.nameplateExtraOffset + 1) + pos.y, pos.z }
// NOTE fidélité : drawSize et nameplateExtraOffset sont additionnés en ENTIER
// (pas convertis en float individuellement) avant d'être ajoutés à pos.y — cf.
// pseudocode d'origine ligne 0x5814ae.
// -----------------------------------------------------------------------------
struct OverheadNameContent {
    bool visible = false;
    Vec3 worldPos{};
};
OverheadNameContent ComputeOverheadNameContent(const EntityRenderState& state, const DrawCullContext& cull);

// -----------------------------------------------------------------------------
// Char_DrawNameTag 0x583470 — gate + formatage "%s(%d)" vs "%s" + position.
// -----------------------------------------------------------------------------
struct NameTagContent {
    bool visible = false;
    bool showLevelSuffix = false; // true => format "%s(%d)" (nom, niveau) ; false => "%s" (nom seul)
    Vec3 worldPos{};               // pos + (0, 2.5, 0)
};
NameTagContent ComputeNameTagContent(const NameTagRenderState& tag, const Vec3& localPlayerPos);

// -----------------------------------------------------------------------------
// Placement du maillage principal (Char_Draw/Shadow/Reflection, branchement sur
// `attached`) : quand l'entité est attachée (monture/parent), le dessin utilise
// une position/rotation relative fixe (angle=0, pos=attachPos, scale=attachScale)
// au lieu de la position/rotation/échelle normales de l'entité.
// -----------------------------------------------------------------------------
struct BodyMeshPlacement {
    bool  useAttachOffset = false;
    float angle = 0.0f;   // 0.0 si attaché, sinon state.facingOrAnimTimer
    Vec3  pos{};          // attachPos si attaché, sinon state.pos
    float scale = 0.0f;   // attachScale si attaché, sinon state.scaleY
};
BodyMeshPlacement ComputeBodyMeshPlacement(const EntityRenderState& state);

// -----------------------------------------------------------------------------
// Sélection de la variante d'overlay "arme" dessinée juste après le corps
// principal dans Char_Draw (et son équivalent dans Shadow/Reflection).
//   - kAliveDeadSpecial : modelCategoryId ∈ {1141,1142,1143,1144} ET
//     weaponRenderType != 2 -> variante "vivant" (hp>0) ou "mort" (hp<=0)
//   - kWearStage : weaponRenderType == 2 ET détail ombres élevé -> palier d'usure
//     0..3 calculé depuis le ratio PV/PVmax (100%->0 = pristine, 0%->3 = pire état)
//   - kDefaultStage0 : tous les autres cas (palier 0 fixe)
// `highDetailShadows` correspond au global g_Opt_GfxDetailShadows == 1, fourni en
// paramètre pour garder cette fonction pure (option graphique, pas d'accès global).
// -----------------------------------------------------------------------------
struct WeaponOverlayDecision {
    enum class Kind { kAliveDeadSpecial, kWearStage, kDefaultStage0 } kind = Kind::kDefaultStage0;
    bool aliveVariant   = false; // valide seulement si kind == kAliveDeadSpecial
    int  wearStageIndex = 0;     // valide seulement si kind == kWearStage ; 0..3 en usage normal
                                  // (non borné dans l'original : hp/maxHp aberrant -> hors [0,3])
};
WeaponOverlayDecision ComputeWeaponOverlayVariant(const EntityRenderState& state, bool highDetailShadows);

// -----------------------------------------------------------------------------
// Branchement "morph spécial tribu" (Char_Draw, tout début — 0x580617..0x58074A).
// Pris uniquement quand modelCategoryId ∈ [589,600] (costumes NPC réservés aux
// morphs de compétence de tribu) ET que le joueur local est actuellement morphé
// (index de compétence morph valide). Les résolutions de tables externes
// (TribeSkill_SkillIdToIndex, dword_184C218, Item_MapUpgradeIconId) restent du
// ressort du système Skill/Item — elles sont injectées ici en paramètres déjà
// résolus pour garder cette fonction pure.
// -----------------------------------------------------------------------------
struct TribeMorphOverlayDecision {
    bool inMorphIdRange = false; // modelCategoryId ∈ [589,600]
    bool active         = false; // + index de compétence morph valide -> branchement pris

    int  itemVariant  = 0; // v10 = table[idx] % 100
    int  upgradeLevel = 0; // v11 = table[idx] / 100

    enum class Overlay { kNone, kUpgradeIcon, kClassOverlay } overlay = Overlay::kNone;

    // true => Char_Draw retourne IMMÉDIATEMENT après cette passe : le corps normal
    // et l'overlay arme ne sont PAS dessinés du tout pour cette frame.
    bool skipNormalBodyDraw = false;
};
// `selfMorphSkillIndex` = TribeSkill_SkillIdToIndex(g_SelfMorphNpcId), -1 si aucun.
// `morphTableValue` = dword_184C218[selfMorphSkillIndex] si l'index est valide (0 sinon).
TribeMorphOverlayDecision ComputeTribeMorphOverlay(const EntityRenderState& state,
                                                    int selfMorphSkillIndex,
                                                    int morphTableValue);

// Après que la couche Gfx a tenté Item_MapUpgradeIconId(itemVariant, upgradeLevel)
// et l'a effectivement dessiné (icône valide, != -1), appeler ceci pour savoir si
// le minuteur d'anim doit être remis à 0 (comportement exact : reset seulement si
// l'icône a été dessinée ET que facingOrAnimTimer avait atteint >= 41).
bool ShouldResetAnimTimerAfterUpgradeIconDraw(float facingOrAnimTimer);

// Vecteur de décalage utilisé pour toutes les auras/overlays "attachés en hauteur"
// (morph, overlay d'état, 3 emplacements d'effets fixes) : (0, scaleY, 0).
Vec3 ComputeAuraOffset(const EntityRenderState& state);

// -----------------------------------------------------------------------------
// Overlay d'état générique (switch sur stateCategory, fin de Char_Draw,
// 0x580a8f..0x580c0a). La résolution effective de l'id (compétence/classe/objet/
// motion spéciale) appartient aux systèmes correspondants (Skill/Item/Anim) —
// modélisée ici uniquement comme classification + décision "faut-il dessiner".
// -----------------------------------------------------------------------------
enum class StateOverlayCategory {
    kNone         = -1,
    kSkillId      = 0,  // maybe_MapCharToSkillId(info.motionIndex)
    kClassId      = 5,  // maybe_MapClassToId(info.motionIndex)
    kItemVariant  = 7,  // Item_MapToVariantId(info.motionIndex)
    kSpecialMotion = 12, // Anim_MapSpecialMotion(info.motionIndex) (0xC)
};
// Classe stateCategory en catégorie d'overlay (ne résout PAS l'id final).
StateOverlayCategory ClassifyStateOverlay(const EntityRenderState& state);
// `resolvedOverlayId` = résultat du résolveur correspondant à la catégorie
// ci-dessus (déjà appelé par la couche Skill/Item/Anim). Renvoie true si un
// overlay doit être dessiné (id résolu != -1).
bool ShouldDrawStateOverlay(int resolvedOverlayId);

// -----------------------------------------------------------------------------
// 3 emplacements d'effets attachés fixes (fin de Char_Draw, 0x580c12..0x580ca6).
// Simple passthrough des drapeaux, mais documente l'ordre de dessin garanti
// (slot1 puis slot2 puis slot3) et les modèles fixes associés côté Gfx :
//   slot1 -> unk_B63174, slot2 -> unk_B63208, slot3 -> unk_B5A180
// -----------------------------------------------------------------------------
struct AttachedEffectSlots {
    bool slot1 = false; float slot1Scale = 0.0f;
    bool slot2 = false; float slot2Scale = 0.0f;
    bool slot3 = false; float slot3Scale = 0.0f;
};
AttachedEffectSlots ComputeAttachedEffectSlots(const EntityRenderState& state);

} // namespace ts2::game

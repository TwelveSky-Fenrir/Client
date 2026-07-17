// Gfx/MotionCache.h — miroir CPU de g_ModelMotionArray 0x8E8B30 (palettes d'os animees).
//
// PROBLEME resolu ici : Scene/WorldRenderer.cpp dessinait toutes les entites avec une
// gfx::BonePalette IDENTITE (aucune animation). Le vrai client garde en memoire, au boot,
// un enorme tableau d'objets Motion (g_ModelMotionArray 0x8E8B30, entrees de 156 o chacune)
// indexe par (kind, anim). A chaque frame le rendu recupere la palette d'os de l'objet via
// Motion_GetData 0x4D78C0 (renvoie motionSlot+136) et echantillonne la frame courante. Ce
// cache comble ce trou COTE CPU pur (aucun device D3D requis) : cle = stem MOTION sans
// dossier ni extension (ex. "M001001001"), valeur = matrices decodees + une gfx::MotionPalette
// (valid/frameCount/bonesPerFrame/base) identique au retour de Motion_GetData.
//
// CHAINE DE DESSIN PROUVEE (IDA, verite unique) :
//   Char_Draw 0x5805C0 (corps monstre en jeu)
//     -> SObject_DrawEx 0x4D9330(desc, pass, animTime, pos, rotY, scale, motionSlot, 1)
//       -> Motion_GetData 0x4D78C0(motionSlot) = motionSlot+136 (MotionPalette)
//       -> Model_Render 0x40EBB0(mesh, scale, scaleVec, pos, rotVec, pass, palette, animTime,..)
//         -> Model_DrawSkinnedSubset 0x40CA40 (base + ((frame*bonesPerFrame)<<6), count=bonesPerFrame)
//
// STRUCTURE MotionPalette (retour Motion_GetData = motionSlot+136), confirmee par Model_Render
// (a7[0]=valid, a7[1]=frameCount, cf. 0x40ebde/0x40ebfd) et Model_DrawSkinnedSubset
// (*(a4+8)=bonesPerFrame, *(a4+12)=base) : +0 valid, +4 frameCount, +8 bonesPerFrame, +12 base
// (ptr -> matrices 64 o, frame-major). C'est EXACTEMENT gfx::MotionPalette de MeshRenderer.h.
//
// FORMAT .MOTION -> matrices (Motion_ReadFrames 0x40B1A0 + Motion_QuatToMatrix 0x6BB684 =
// thunk D3DXMatrixRotationQuaternion) : le parseur ts2::asset::Motion (Asset/Motion.h) fournit
// deja les keyframes en ordre disque = frame-major, At(frame,bone)=frame*BoneCount()+bone.
// FIDELITE (CLAUDE.md) : .MOTION = zlib PUR via le parseur existant -- NE JAMAIS transposer
// XTEA/GXCW (reserve au .npk).
//
// ACCESSEURS par slot (miroir des 3 accesseurs demandes) :
//   - Monstre : Model_GetNpcMotionSlot 0x4E5960 (kind<=0x14C=332, anim in {0,1,3,4,5,7,8,12,19}).
//   - PNJ     : Model_GetNpcMeshSlot   0x4E5910 (kind<=0x41=65, anim<2).
//   - Joueur  : PcModel_ResolveEquipSlot 0x4E46A0 (race in [0,3), gender in [0,2)).
//
// CHEMINS .MOTION (Motion_BuildPathAndLoad 0x4D7390, printf exacts, verifies disque 001..006) :
//   Joueur  001\C%03d%03d%03d.MOTION % (race+3*gender+1, weaponType+1, animState+1)
//   PNJ     002\N%03d001%03d.MOTION  % (kindIndex+1, animType+1)
//   Monstre 003\M%03d001%03d.MOTION  % (kindIndex+1, animType+1)
#pragma once
#include "Gfx/MeshRenderer.h"      // gfx::MotionPalette / gfx::BonePalette (types de palette d'os)
#include "Asset/Motion.h"          // ts2::asset::Motion (parseur zlib pur)
#include "Game/GameDatabase.h"     // game::GetMonsterInfo / MonsterInfo::kindIndexP1
#include "Game/ExtraDatabases.h"   // game::NpcDefRecord::fieldE
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  MotionCache — cle = stem MOTION (nom SANS dossier ni extension, ex "M001001001").
// ---------------------------------------------------------------------------
class MotionCache {
public:
    // gameDataDir = racine "GameData" (meme convention "." que ModelCache, cf.
    // WorldRenderer::Init). AUCUN device D3D requis : donnees 100 % CPU.
    explicit MotionCache(std::string gameDataDir);

    MotionCache(const MotionCache&) = delete;
    MotionCache& operator=(const MotionCache&) = delete;

    // Palette d'animation d'un monstre (Char_Draw 0x5805C0 + Model_GetNpcMotionSlot 0x4E5960) :
    // kindIndex = GetMonsterInfo(monsterDefId)->kindIndexP1 - 1 ; garde 1<=kindIndexP1<=333 et
    // animType in {0,1,3,4,5,7,8,12,19}. nullptr si hors bornes / fichier absent / parsing KO.
    const gfx::MotionPalette* GetForMonster(uint32_t monsterDefId, int animType);

    // Palette d'animation d'un PNJ (Model_GetNpcMeshSlot 0x4E5910) : kindIndex = npc.fieldE - 1 ;
    // garde 1<=fieldE<=66 et animType in {0,1}. nullptr sinon.
    const gfx::MotionPalette* GetForNpc(const game::NpcDefRecord& npc, int animType);

    // Palette d'animation d'un joueur (PcModel_ResolveEquipSlot 0x4E46A0) : garde race in [0,3),
    // gender in [0,2). weaponType/animState laisses en defaut idle (0,0) par l'appelant, cf.
    // PlayerPaperdoll. nullptr sinon.
    const gfx::MotionPalette* GetForPlayer(int race, int gender, int weaponType, int animState);

    // Palette d'animation de la TRAINEE D'ARME (front F_WEAPONTRAIL, 2026-07-17) — motion cat. 5
    // "F%03d001%03d.MOTION" % (trailIndex+1, motionSub+1), dossier 005 (Motion_BuildPathAndLoad
    // 0x4D7390 case 5, cf. Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §3). trailIndex = v6 in [0,42) issu
    // du switch Game/WeaponTrailResolver.h ; motionSub in [0,3) = sous-bloc choisi par l'etat
    // d'action (ResolveWeaponTrailMotionSub) et indexe unk_F54DB4/E50/EEC dans le binaire. C'est le
    // MEME cache que le corps (fidelite : dans 0x56BF90 la trainee et le corps partagent
    // g_ModelMotionArray via Motion_GetData). nullptr si trailIndex/motionSub hors bornes, fichier
    // absent ou parsing KO. Le frameCount de la palette (MotionPalette+4) alimente le gate
    // Motion_GetFrameCount>=1 du sous-bloc 2 (cf. Scene/WorldRenderer.cpp::resolveWeaponTrail).
    const gfx::MotionPalette* GetForWeaponTrail(int trailIndex, int motionSub);

    // Echantillonne la tranche d'os de la frame courante par g_World.gameTimeSec.
    //
    // ///// MISATTRIBUTION CORRIGEE — Passe 4 / vague W7, front motion-anim (2026-07-16) /////
    // La redaction precedente presentait `ftol(g_GameTimeSec*30.0) % count` comme « l'idiome
    // d'horloge du binaire » pour le dessin de corps, en citant « Char_RenderModel 0x528d38 ».
    // C'est FAUX et c'est corrige ici. Cet idiome existe bien, mais a UN SEUL endroit et pour
    // UN SEUL objet : le MARQUEUR DE QUETE flottant au-dessus d'un PNJ -- Npc_DrawMesh 0x57FF00
    // @0x58005b (`Crt_ftol(g_GameTimeSec * 30.0)` puis `v9 = v5 % SubObjectCount`, injecte dans
    // ModelObj_Draw(unk_B60AB8 + 148*153)). Le CORPS, lui, n'est JAMAIS echantillonne par une
    // horloge globale : ni Char_Draw 0x5805C0 (@0x580828 : animTime = slot monstre +28) ni
    // Npc_DrawMesh (@0x57fff1 : animTime = slot PNJ +16) ne lisent g_GameTimeSec pour le corps.
    // => Cette fonction ne reproduit AUCUN chemin de dessin de corps du binaire. Le chemin
    // fidele est SampleByCursor ci-dessous. SampleByGameTime est CONSERVEE car elle reste
    // consommee par Gfx/PlayerPaperdoll.cpp:23 (JOUEURS) : le curseur joueur reel vit dans
    // CharAnimState::animFrame et n'avance que via le switch terminal de
    // Char_UpdateAnimationFrame 0x5727BF (55 handlers), NON PORTE a ce jour (`stateHandler`
    // nul, cf. Scene/SceneManager.cpp:802) -> brancher le curseur joueur maintenant le figerait
    // a 0. Repli ASSUME et documente en attendant ce portage, PAS une reproduction fidele.
    //
    // count = bonesPerFrame BRUT, AUCUN clamp -- fidele a Model_DrawSkinnedSubset 0x40CA40, qui
    // passe Count = *(a4+8) tel quel a SetMatrixArray (+88) : @0x40d4e8 (Sh03), @0x40dac2 (Sh05),
    // @0x40d7c0 (Sh07), sans aucun cmp/min ni constante 40. La borne d'os appartient au SHADER
    // (D3DX ecrit min(Count, mKeyMatrix.Elements)) et est appliquee par MeshRenderer::
    // DrawSkinnedSubset (boneArraySize_ ; kMaxBones = fallback HLSL uniquement). Identique a
    // MotionPalette::FrameSlice. BonePalette invalide (matrices=nullptr) si mp invalide.
    static gfx::BonePalette SampleByGameTime(const gfx::MotionPalette& mp, float gameTimeSec);

    // Echantillonne la tranche d'os par le CURSEUR PROPRE A L'ENTITE — chemin de dessin REEL du
    // binaire pour les corps monstre/PNJ (Passe 4 / W7, front motion-anim ; corrige le gap
    // as-motion-02 « horloge globale au lieu du curseur par entite »).
    //   Char_Draw 0x5805C0 @0x580828 : SObject_DrawEx(..., animTime = *((float*)this + 7)
    //     = slot monstre +28, ...) -- curseur PAR MONSTRE, accumule par les 9 handlers
    //     Char_MotionTick_* (0x582D40..0x5832E0) dispatches par Char_Update 0x581E10 @0x5822D3.
    //   Npc_DrawMesh 0x57FF00 @0x57fff1 : SObject_DrawEx(..., animTime = *(this + 4)
    //     = slot PNJ +16, ...) -- curseur PAR PNJ, accumule par Npc_RenderSlotTick_Loop
    //     0x580400 @0x58043e / Npc_RenderSlotTick_Once 0x5804A0 @0x5804de.
    // SObject_DrawEx 0x4D9330 @0x4d946d passe a3 TEL QUEL en 8e argument de Model_Render
    // 0x40EBB0 (= animTime) -> frame = ftol(animTime) @0x40ebea, borne [0, frameCount-1].
    //
    // AUCUN MODULO ICI, ET C'EST ESSENTIEL (contrairement a SampleByGameTime) : le curseur
    // arrive DEJA reboucle par le tick, dont le wrap est une SOUSTRACTION UNIQUE (@0x582e10
    // Loop, @0x58045f PNJ), jamais un modulo -- et l'etat Mort (Char_MotionTick_Death 0x5832E0
    // @0x583345) GELE volontairement le curseur a frameCount-1 : un modulo le ferait repartir a
    // 0 et ressusciterait visuellement le cadavre. Le clamp [0, frameCount-1] reproduit
    // exactement la borne de Model_Render, rien de plus.
    // Meme absence de clamp d'os que SampleByGameTime (cf. sa note : count = bonesPerFrame BRUT,
    // la seule borne legitime appartient au shader via MeshRenderer::DrawSkinnedSubset).
    static gfx::BonePalette SampleByCursor(const gfx::MotionPalette& mp, float cursor);

    // Nombre de frames de l'anim (kind, animType) — miroir exact de Model_GetMotionFrameCount
    // 0x4E5A70 (monstre) / Model_GetWeaponEffectFrameCount 0x4E5A40 (PNJ), qui resolvent le MEME
    // slot que le dessin puis renvoient Motion_GetFrameCount 0x4D7830. VERIFIE : 0x4D7830 renvoie
    // TOUJOURS *(slot+140) (@0x4d786d) quel que soit son 2e argument (1 cote monstre, 0 cote PNJ
    // — ce flag ne pilote que le chargement paresseux Motion_Load, pas la valeur rendue) => le
    // frameCount du tick est BIT-POUR-BIT celui de la palette de dessin. 0 si non resolu (borne
    // depassee / fichier absent) -> le tick degrade proprement (cf. Game/AnimationTick.h).
    int GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType);
    int GetNpcMotionFrameCount(const game::NpcDefRecord& npc, int animType);

private:
    struct Entry {
        std::vector<D3DXMATRIX> matrices;      // frameCount*boneCount, ordre frame-major
        gfx::MotionPalette      palette{};     // pointe dans `matrices` (stable : cache persistant)
        bool                    loadFailed = false;
    };

    // Lazy-load + cache (succes ET echec). folder = "001"/"002"/"003" (3 o + NUL).
    const gfx::MotionPalette* Get(const std::string& stem, const char* folder);

    // Stems (Motion_BuildPathAndLoad 0x4D7390) — chaine vide si un parametre est hors bornes.
    static std::string BuildMonsterMotionStem(int kindIndex, int animType); // "M%03d001%03d"
    static std::string BuildNpcMotionStem(int kindIndex, int animType);     // "N%03d001%03d"
    static std::string BuildPlayerMotionStem(int race, int gender, int weaponType, int animState); // "C%03d%03d%03d"
    static std::string BuildWeaponTrailMotionStem(int trailIndex, int motionSub); // "F%03d001%03d" (cat.5, dossier 005)

    // Chemin complet <gameDataDir>\G03_GDATA\D03_GMOTION\NNN\<stem>.MOTION (0x4D7390).
    std::string BuildMotionPath(const std::string& stem, const char* folder) const;

    std::string                            gameDataDir_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace ts2::gfx

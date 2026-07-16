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

    // Echantillonne la tranche d'os de la frame courante par g_World.gameTimeSec.
    // Model_Render 0x40ebea : frame = ftol(animTime), borne 0..frameCount-1. Char_RenderModel
    // 0x528d38 : idiome d'horloge du binaire = ftol(g_GameTimeSec*30.0) % count (boucle 30 fps).
    // count = bonesPerFrame BRUT, AUCUN clamp -- fidele a Model_DrawSkinnedSubset 0x40CA40, qui
    // passe Count = *(a4+8) tel quel a SetMatrixArray (+88) : @0x40d4e8 (Sh03), @0x40dac2 (Sh05),
    // @0x40d7c0 (Sh07), sans aucun cmp/min ni constante 40. La borne d'os appartient au SHADER
    // (D3DX ecrit min(Count, mKeyMatrix.Elements)) et est appliquee par MeshRenderer::
    // DrawSkinnedSubset (boneArraySize_ ; kMaxBones = fallback HLSL uniquement). Identique a
    // MotionPalette::FrameSlice. BonePalette invalide (matrices=nullptr) si mp invalide.
    static gfx::BonePalette SampleByGameTime(const gfx::MotionPalette& mp, float gameTimeSec);

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

    // Chemin complet <gameDataDir>\G03_GDATA\D03_GMOTION\NNN\<stem>.MOTION (0x4D7390).
    std::string BuildMotionPath(const std::string& stem, const char* folder) const;

    std::string                            gameDataDir_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace ts2::gfx

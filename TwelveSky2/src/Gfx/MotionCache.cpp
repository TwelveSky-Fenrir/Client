// Gfx/MotionCache.cpp — implementation. Voir MotionCache.h pour la chaine de dessin prouvee
// (Char_Draw 0x5805C0 -> SObject_DrawEx 0x4D9330 -> Motion_GetData 0x4D78C0 -> Model_Render
// 0x40EBB0) et les EAs des accesseurs de slot / des chemins .MOTION.
#include "Gfx/MotionCache.h"
#include "Core/Log.h"
#include <cstdio>

namespace ts2::gfx {

namespace {

// Meme convention de jointure que Gfx/ModelCache.cpp::JoinPath / Game/GameDatabase.cpp::Join.
std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Model_GetNpcMotionSlot 0x4E5960 : switch des anims monstre valides {0,1,3,4,5,7,8,0xC,0x13}
// (les autres -> off_547298 fallback). 0xC=12, 0x13=19.
bool IsValidMonsterAnim(int animType) {
    switch (animType) {
        case 0: case 1: case 3: case 4: case 5: case 7: case 8: case 12: case 19:
            return true;
        default:
            return false;
    }
}

} // namespace

MotionCache::MotionCache(std::string gameDataDir)
    : gameDataDir_(std::move(gameDataDir)) {}

// Motion_BuildPathAndLoad 0x4D7390 cas 3 : "M%03d001%03d.MOTION" % (kindIndex+1, animType+1).
std::string MotionCache::BuildMonsterMotionStem(int kindIndex, int animType) {
    if (kindIndex < 0 || kindIndex >= 333) return {};   // borne Model_GetNpcMotionSlot 0x4E5960 (a2>0x14C)
    if (!IsValidMonsterAnim(animType)) return {};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "M%03d001%03d", kindIndex + 1, animType + 1);
    return buf;
}

// Motion_BuildPathAndLoad 0x4D7390 cas 2 : "N%03d001%03d.MOTION" % (kindIndex+1, animType+1).
std::string MotionCache::BuildNpcMotionStem(int kindIndex, int animType) {
    if (kindIndex < 0 || kindIndex >= 66) return {};    // borne Model_GetNpcMeshSlot 0x4E5910 (a2<=0x41)
    if (animType < 0 || animType >= 2) return {};       // Model_GetNpcMeshSlot : a3<2
    char buf[16];
    std::snprintf(buf, sizeof(buf), "N%03d001%03d", kindIndex + 1, animType + 1);
    return buf;
}

// Motion_BuildPathAndLoad 0x4D7390 cas 1 : "C%03d%03d%03d.MOTION" %
// (race+3*gender+1, weaponType+1, animState+1).
std::string MotionCache::BuildPlayerMotionStem(int race, int gender, int weaponType, int animState) {
    if (race < 0 || race >= 3) return {};               // PcModel_ResolveEquipSlot 0x4E46A0 : race in [0,3)
    if (gender < 0 || gender >= 2) return {};           //                                     gender in [0,2)
    if (weaponType < 0 || animState < 0) return {};
    const int kindIndex = race + 3 * gender;            // 0-based (meme formule que corps, ModelCache)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "C%03d%03d%03d", kindIndex + 1, weaponType + 1, animState + 1);
    return buf;
}

// Motion_BuildPathAndLoad 0x4D7390 : dossier G03_GDATA\D03_GMOTION\NNN\<stem>.MOTION.
std::string MotionCache::BuildMotionPath(const std::string& stem, const char* folder) const {
    if (stem.empty()) return {};
    std::string path = JoinPath(gameDataDir_, "G03_GDATA\\D03_GMOTION");
    path = JoinPath(path, folder);
    path = JoinPath(path, stem + ".MOTION");
    return path;
}

const gfx::MotionPalette* MotionCache::Get(const std::string& stem, const char* folder) {
    if (stem.empty()) return nullptr;

    auto found = entries_.find(stem);
    if (found != entries_.end())
        return found->second.loadFailed ? nullptr : &found->second.palette;

    Entry entry;
    const std::string path = BuildMotionPath(stem, folder);

    asset::Motion m;
    if (path.empty() || !m.Load(path)) {
        TS2_WARN("MotionCache: echec chargement MOTION '%s' (stem '%s')",
                 path.empty() ? "<chemin vide>" : path.c_str(), stem.c_str());
        entry.loadFailed = true;
    } else {
        // Conversion bit-fidele (Motion_ReadFrames 0x40B1A0 + Motion_QuatToMatrix 0x6BB684 =
        // thunk D3DXMatrixRotationQuaternion) : par keyframe (28 o disque) rotation quaternion
        // puis translation ecrite a +48/+52/+56 de la matrice (= m._41/_42/_43, cf. 0x40b26d..).
        // FIDELITE : .MOTION = zlib pur (parseur existant), PAS de XTEA/GXCW (cf. CLAUDE.md).
        const uint32_t F = m.FrameCount(); // count_A (Motion_ReadFrames @off4)
        const uint32_t B = m.BoneCount();  // count_B (Motion_ReadFrames @off8)
        const std::vector<asset::MotionKeyframe>& kf = m.Keyframes(); // ordre disque = frame-major
        entry.matrices.resize(static_cast<size_t>(F) * B);
        const size_t n = (entry.matrices.size() < kf.size()) ? entry.matrices.size() : kf.size();
        for (size_t i = 0; i < n; ++i) {
            D3DXQUATERNION q(kf[i].qx, kf[i].qy, kf[i].qz, kf[i].qw);
            D3DXMATRIX mtx;
            D3DXMatrixRotationQuaternion(&mtx, &q); // Motion_QuatToMatrix 0x6BB684
            mtx._41 = kf[i].tx;                     // translation @+48
            mtx._42 = kf[i].ty;                     // translation @+52
            mtx._43 = kf[i].tz;                     // translation @+56
            entry.matrices[i] = mtx;
        }
        // gfx::MotionPalette = motionSlot+136 (Motion_GetData 0x4D78C0) : +0 valid, +4 frameCount,
        // +8 bonesPerFrame, +12 base. base pose apres insertion (pointeur stable du vector du cache).
        entry.palette.valid         = entry.matrices.empty() ? 0 : 1;
        entry.palette.frameCount    = static_cast<int>(F);
        entry.palette.bonesPerFrame = static_cast<int>(B);
        entry.palette.base          = nullptr; // rebranche ci-dessous sur l'entree inseree
    }

    auto ins = entries_.emplace(stem, std::move(entry)).first;
    if (!ins->second.loadFailed) {
        // Le buffer du vector a ete transfere par le move (adresse stable) : on rebranche `base`
        // sur l'instance PERSISTANTE du cache pour ne jamais pointer une pile/temporaire.
        ins->second.palette.base = ins->second.matrices.empty() ? nullptr
                                                                 : ins->second.matrices.data();
        return &ins->second.palette;
    }
    return nullptr;
}

const gfx::MotionPalette* MotionCache::GetForMonster(uint32_t monsterDefId, int animType) {
    // Char_Draw 0x5805C0 : kindIndex = *(def+244) - 1 = MonsterInfo::kindIndexP1 - 1.
    const game::MonsterInfo* mi = game::GetMonsterInfo(monsterDefId);
    if (!mi) return nullptr;
    const int k1 = mi->kindIndexP1;                 // 1-based tel que stocke (+244)
    if (k1 < 1 || k1 > 333) return nullptr;          // Model_GetNpcMotionSlot 0x4E5960 : a2>0x14C -> fallback
    const std::string stem = BuildMonsterMotionStem(k1 - 1, animType);
    return Get(stem, "003");
}

const gfx::MotionPalette* MotionCache::GetForNpc(const game::NpcDefRecord& npc, int animType) {
    // Npc_DrawMesh 0x57FF00 / Model_GetNpcMeshSlot 0x4E5910 : kindIndex = fieldE - 1, borne [1,66].
    const int f = static_cast<int>(npc.fieldE);      // +1324, kindIndex+1
    if (f < 1 || f > 66) return nullptr;
    const std::string stem = BuildNpcMotionStem(f - 1, animType);
    return Get(stem, "002");
}

const gfx::MotionPalette* MotionCache::GetForPlayer(int race, int gender, int weaponType, int animState) {
    // PcModel_ResolveEquipSlot 0x4E46A0 (chemin par defaut) : slot joueur par (race,gender,weaponPose,animState).
    const std::string stem = BuildPlayerMotionStem(race, gender, weaponType, animState);
    return Get(stem, "001");
}

gfx::BonePalette MotionCache::SampleByGameTime(const gfx::MotionPalette& mp, float gameTimeSec) {
    if (!mp.valid || mp.frameCount <= 0 || mp.bonesPerFrame <= 0 || !mp.base)
        return {}; // repli identite cote appelant (BonePalette invalide)

    // Idiome `ftol(g_GameTimeSec*30.0) % count` — Npc_DrawMesh 0x57FF00 @0x58005b. MISATTRIBUTION
    // CORRIGEE (Passe 4 / W7) : cet idiome ne dessine PAS un corps, il n'anime QUE le marqueur de
    // quete flottant (ModelObj_Draw(unk_B60AB8 + 148*153)) ; l'ancienne citation « Char_RenderModel
    // 0x528d38 » pour un corps etait erronee. Cette fonction est donc un REPLI (joueurs seuls, cf.
    // MotionCache.h), pas un chemin fidele -- le chemin fidele corps = SampleByCursor.
    int frame = static_cast<int>(gameTimeSec * 30.0f) % mp.frameCount;
    if (frame < 0) frame += mp.frameCount;

    // AUCUN CLAMP D'OS -- corrige le residuel « clamp 40 os vs FrameSlice sans clamp » (Passe 4/W5).
    // Verite binaire : Model_DrawSkinnedSubset 0x40CA40 passe Count = *(a4+8) = bonesPerFrame BRUT a
    // ID3DXConstantTable::SetMatrixArray (methode +88) -- aucun cmp/min, aucune constante 40 sur ce
    // chemin : Sh03 @0x40d4e8 (g_GxdSh03_hKeyMatrix, Count=*(a4+8)), Sh05 @0x40dac2 et Sh07 @0x40d7c0
    // (Count=*(a10+8)). Model_Render 0x40EBB0, son unique appelant, ne clampe pas davantage (sa seule
    // borne porte sur la FRAME, pas sur les os). La SEULE borne reelle est la taille du tableau
    // mKeyMatrix[] declaree par le shader -- D3DX n'ecrit que min(Count, desc.Elements) -- et elle est
    // deja appliquee au bon endroit par MeshRenderer::DrawSkinnedSubset (boneArraySize_ =
    // GetConstantDesc(mKeyMatrix).Elements sur shader npk reel ; clamp kMaxBones reserve au fallback
    // HLSL, dont le mKeyMatrix[40] est, lui, une vraie contrainte de tableau).
    // L'ancien clamp cite 0x40CA40 A CONTRESENS : il tronquait ici, en amont et IRREVERSIBLEMENT,
    // court-circuitant boneArraySize_ (os >= 40 perdus sans recours sur un shader declarant plus),
    // et divergeait de MotionPalette::FrameSlice (MeshRenderer.cpp:151), non clampe. kMaxBones=40
    // reste une HYPOTHESE de reecriture NON prouvee IDA (Docs/TS2_GXD_ROSETTA.md, TODO P-8) : ne pas
    // la faire fuir hors du fallback qui, seul, la justifie. Aucun debordement n'est possible sans ce
    // clamp : la tranche source contient exactement bonesPerFrame matrices (jamais sur-lue).
    //
    // Model_Render : tranche = base + ((frame*bonesPerFrame)<<6) octets = base[frame*bonesPerFrame]
    // en pas de matrices 64 o.
    gfx::BonePalette bp;
    bp.matrices = mp.base + static_cast<size_t>(frame) * mp.bonesPerFrame;
    bp.count    = static_cast<UINT>(mp.bonesPerFrame); // BRUT, comme FrameSlice et comme le binaire
    return bp;
}

// Chemin de dessin REEL des corps monstre/PNJ (cf. MotionCache.h pour la chaine d'ancres
// complete). Char_Draw 0x5805C0 @0x580828 (animTime = slot monstre +28) / Npc_DrawMesh 0x57FF00
// @0x57fff1 (animTime = slot PNJ +16) -> SObject_DrawEx 0x4D9330 @0x4d946d (a3 passe tel quel)
// -> Model_Render 0x40EBB0 @0x40ebea (frame = ftol(animTime), borne [0, frameCount-1]).
gfx::BonePalette MotionCache::SampleByCursor(const gfx::MotionPalette& mp, float cursor) {
    if (!mp.valid || mp.frameCount <= 0 || mp.bonesPerFrame <= 0 || !mp.base)
        return {}; // repli identite cote appelant (BonePalette invalide)

    // Model_Render 0x40ebea : frame = ftol(animTime) puis borne [0, frameCount-1]. PAS de modulo
    // (cf. MotionCache.h) : le curseur arrive deja reboucle par le tick (wrap par soustraction
    // unique), et l'etat Mort 0x5832E0 @0x583345 gele volontairement a frameCount-1 -- un modulo
    // casserait ce gel. Le clamp bas protege en plus d'un curseur negatif (jamais produit par les
    // handlers portes, garde defensive pure).
    int frame = static_cast<int>(cursor); // Crt_ftol = troncature vers zero
    if (frame < 0) frame = 0;
    if (frame > mp.frameCount - 1) frame = mp.frameCount - 1;

    // AUCUN CLAMP D'OS : count = bonesPerFrame BRUT, strictement comme SampleByGameTime ci-dessus
    // et comme Model_DrawSkinnedSubset 0x40CA40 (@0x40d4e8/@0x40dac2/@0x40d7c0 passent Count tel
    // quel a SetMatrixArray). La borne d'os reste celle du shader (MeshRenderer::DrawSkinnedSubset
    // / boneArraySize_) -- coherence chemin shader reel <-> fallback HLSL preservee (regle W5).
    gfx::BonePalette bp;
    bp.matrices = mp.base + static_cast<size_t>(frame) * mp.bonesPerFrame;
    bp.count    = static_cast<UINT>(mp.bonesPerFrame);
    return bp;
}

// Model_GetMotionFrameCount 0x4E5A70 = Motion_GetFrameCount(Model_GetNpcMotionSlot(this,
// kindIdx, animType), 1) — MEME slot que le dessin (Char_Draw @0x580770), donc meme palette :
// on relit simplement son frameCount. Motion_GetFrameCount 0x4D7830 renvoie *(slot+140)
// @0x4d786d quel que soit son 2e argument (cf. MotionCache.h) -> equivalence exacte.
int MotionCache::GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType) {
    const gfx::MotionPalette* mp = GetForMonster(monsterDefId, animType);
    return mp ? mp->frameCount : 0; // 0 = slot non resolu (Motion_GetFrameCount renvoie 0 aussi)
}

// Model_GetWeaponEffectFrameCount 0x4E5A40 = Motion_GetFrameCount(Model_GetNpcMeshSlot(this,
// kindIdx, animType), 0) — MEME slot que le dessin (Npc_DrawMesh @0x57ffa0), meme raisonnement.
int MotionCache::GetNpcMotionFrameCount(const game::NpcDefRecord& npc, int animType) {
    const gfx::MotionPalette* mp = GetForNpc(npc, animType);
    return mp ? mp->frameCount : 0;
}

} // namespace ts2::gfx

// Gfx/CharPreview3D.cpp — implementation. ALL the IDA evidence (catalog table,
// piece order, bone palette, camera, light, scale) is in CharPreview3D.h:
// refer to it before touching a single constant in this file.
#include "Gfx/CharPreview3D.h"
#include "Game/GameDatabase.h"  // game::GetItemInfo / game::ItemInfo (= MobDb_GetEntry(mITEM) 0x4C3C00)
#include <cstdio>
#include <cstring>

namespace ts2::gfx {

namespace {

//  Offsets of the raw record (10088 o) READ BY Char_RenderModel 0x527020.
//  Char_RenderModel indexes `a4` in DWORDs: a4[N] == record + 4*N.
//  Deliberately LOCAL: Gfx/ must not depend on Net/ (layering inversion, and
//  Net/CharSelectPackets.h pulls in winsock2 + Game/CharSelectFlow.h). The 5 shared offsets
//  (36/40/44/48/52) coincide with net::kCharRecField* — NEVER let them diverge.
constexpr int kRecOffField4 = 4;   // a4[1]  — `if (a4[1] != 1)` @0x527670 gates the FACE
                                   //          ⚠ UNKNOWN semantics (spec §13.7). We reproduce
                                   //          the test without claiming to know what it means.
constexpr int kRecOffJob    = 36;  // a4[9]  — CREATE: RACE index (@0x527051)
                                   //          LIST   : sentinel `== 3` (@0x52754A)
constexpr int kRecOffRace   = 40;  // a4[10] — LIST: RACE index (@0x527536)
constexpr int kRecOffGender = 44;  // a4[11] — a3 of PcModel_ResolveEquipSlot (@0x52752F / @0x52704A)
constexpr int kRecOffFace   = 48;  // a4[12] — entry index into catalog a5=0
constexpr int kRecOffHair   = 52;  // a4[13] — entry index into catalog a5=1
constexpr int kRecOffItemEquipA = 136; // a4[34] — v29 = MobDb_GetEntry(mITEM, …) @0x52747A
constexpr int kRecOffItemEquipB  = 184; // a4[46] — v30 = MobDb_GetEntry(mITEM, …) @0x527491
constexpr int kRecOffItemWeapon  = 216; // a4[54] — v31 = MobDb_GetEntry(mITEM, …) = the WEAPON (@0x528263)

// Value of the +36 sentinel (`cmp dword ptr [ecx+24h], 3` @0x52754A). Selects the
// category-7 BODY and enables the ALTERNATE catalogs (kind+7) a5=14/15.
constexpr int32_t kJobSentinel = 3;

// field196 of helmets that HIDE the face and cat7 body when gender == 1:
// `if (*(v29+196) != 37 && *(v29+196) != 39)` @0x5275CB (cat7 body) and @0x5276FB (face).
constexpr uint32_t kHelmetHidesFaceA = 37;
constexpr uint32_t kHelmetHidesFaceB = 39;

int32_t ReadI32(const uint8_t* rec, int off) {
    int32_t v = 0;
    std::memcpy(&v, rec + off, sizeof(v)); // 32-bit LE, same endianness as the target binary
    return v;
}

// File-name kindIndex = race + 3*gender (SObject_BuildPath 0x4D89C0: `a3 + 3*a4`),
// then +1 (common case) or +7 (special cases a5=14/15, @0x4D8A30 / @0x4D8A5E).
int KindNumber(int race, int gender, bool alternate) {
    return race + 3 * gender + (alternate ? 7 : 1);
}

// Description of a category-1 catalog (cf. the table in header banner §2).
struct CatalogDesc {
    int  token;        // the MIDDLE %03d of the file name (frozen for a5=14/15/20)
    int  count;        // loop bound of AssetMgr_InitAllSlots
    bool alternateKind;// true => kindIndex +7 instead of +1
    bool fileNumIsEntry;// true  => a6 = i-1 in the loop => file number == i
                       // false => a6 = i                  => file number == i+1
};

bool DescribeCatalog(CharCatalogSlot slot, CatalogDesc& out) {
    switch (slot) {
        // slot | token | count | kind+7 | fileNum == entryIndex
        case CharCatalogSlot::Face:     out = {  1,  7, false, false }; return true; // @0x4DF3CC
        case CharCatalogSlot::Hair:     out = {  2,  3, false, false }; return true; // @0x4DF426
        case CharCatalogSlot::EquipA:    out = {  3, 51, false, true  }; return true; // @0x4DF483
        case CharCatalogSlot::EquipAAlt: out = {  3, 51, true,  true  }; return true; // @0x4DF8C1
        case CharCatalogSlot::EquipB:    out = {  4, 51, false, true  }; return true; // @0x4DF4E0
        case CharCatalogSlot::EquipBAlt: out = {  4, 51, true,  true  }; return true; // @0x4DF91E
        case CharCatalogSlot::WeaponA:  out = {  5, 57, false, false }; return true; // @0x4DF53A
        case CharCatalogSlot::WeaponB:  out = {  6, 57, false, false }; return true; // @0x4DF594
        case CharCatalogSlot::WeaponC:  out = {  7, 57, false, false }; return true; // @0x4DF5EE
        case CharCatalogSlot::Slot009:  out = {  9,  1, false, false }; return true; // @0x4DF6A2
        default: return false;
    }
}

// Adds piece `stem` if it resolves AND is not empty. Each piece is
// independently optional — SObject_DrawEx 0x4D9330 @0x4D9349 returns without drawing anything
// if `SObject_Load` fails: a missing piece NEVER cancels the others.
void PushPiece(ModelCache& models, std::vector<const SkinnedModel*>& pieces,
               const std::string& stem) {
    if (stem.empty()) return;
    if (const SkinnedModel* m = models.Get(stem))
        if (!m->Empty()) pieces.push_back(m);
}

// Resolves a catalog then pushes the piece (shortcut for the 2 calls above).
void PushCatalogPiece(ModelCache& models, std::vector<const SkinnedModel*>& pieces,
                      CharCatalogSlot slot, int race, int gender, int entryIndex) {
    PushPiece(models, pieces, CharPreview3D::BuildCatalogStem(slot, race, gender, entryIndex));
}

// true if the catalog resolves to an actually loadable model — equivalent of the
// `SObject_Load(&flt_F6685C[…], 1)` tested @0x527906 / @0x527AC3 before choosing the
// ALTERNATE catalog rather than the base catalog.
bool CatalogLoads(ModelCache& models, CharCatalogSlot slot, int race, int gender, int entryIndex) {
    const std::string stem = CharPreview3D::BuildCatalogStem(slot, race, gender, entryIndex);
    if (stem.empty()) return false;
    const SkinnedModel* m = models.Get(stem);
    return m != nullptr && !m->Empty();
}

} // namespace

//  Stems
// SObject_BuildPath 0x4D89C0 case 1: "…\001\C%03d%03d%03d.SOBJECT".
std::string CharPreview3D::BuildCatalogStem(CharCatalogSlot slot, int race, int gender,
                                            int entryIndex) {
    CatalogDesc d{};
    if (!DescribeCatalog(slot, d)) return {};
    // Bounds from PcModel_ResolveEquipSlot 0x4E46A0 @0x4E46CC (a2>2 / a3>1) + catalog loop
    // bounds. The binary has NO index guard here (it indexes blindly): this is a PORTING
    // guard, not an observable behavior divergence — out of bounds, the binary would read a
    // descriptor from ANOTHER catalog (behavior that is neither reproducible nor desired).
    if (race < 0 || race >= kCharPreviewRaceCount)     return {};
    if (gender < 0 || gender >= kCharPreviewGenderCount) return {};
    if (entryIndex < 0 || entryIndex >= d.count)        return {};

    const int kind    = KindNumber(race, gender, d.alternateKind);
    const int fileNum = d.fileNumIsEntry ? entryIndex : entryIndex + 1;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "C%03d%03d%03d", kind, d.token, fileNum);
    return buf;
}

// SObject_BuildPath 0x4D89C0 case 7 @0x4D8CD5: "…\007\H%03d%03d%03d.SOBJECT" with
// a5 = 0 and a6 = 0 in the single writer loop @0x4E01D7 -> "H%03d001001".
std::string CharPreview3D::BuildCat7BodyStem(int race, int gender) {
    if (race < 0 || race >= kCharPreviewRaceCount)       return {};
    if (gender < 0 || gender >= kCharPreviewGenderCount) return {};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "H%03d001001", KindNumber(race, gender, /*alternate=*/false));
    return buf;
}

//  Shared bone palette — PcModel_ResolveEquipSlot 0x4E46A0
BonePalette CharPreview3D::ResolveBonePalette(MotionCache& motions, int race, int gender,
                                              int motion, int animState, float animTime) {
    // Guard @0x4E46CC: `if (a2 > 2 || a3 > 1 || !Motion_IsValidWeaponPose(a4, a5))
    //                       return this + 2644772;`
    // 2644772 - 2624960 (motion table base) = 19812 = 127*156 -> the fallback slot
    // IS (race 0, gender 0, motion 0, animState 127) = "C001001128.MOTION".
    bool valid = (unsigned)race <= 2u && (unsigned)gender <= 1u;
    if (valid) {
        // Motion_IsValidWeaponPose 0x4E3A30 — only the 2 CharSelect animStates:
        //   case 1 (Idle)     : `if (a1 >= 8) -> 0`               @0x4E3A7D
        //   case 3 (Entering) : a1 ∈ {0,2,4,6} otherwise -> 0     @0x4E3ACF
        // Other animStates are never used here (cf. header banner §4).
        if (animState == 1)      valid = (unsigned)motion < 8u;
        else if (animState == 3) valid = (motion == 0 || motion == 2 || motion == 4 || motion == 6);
        else                     valid = false; // outside the 2 CharSelect values -> fallback
    }

    const int useRace   = valid ? race      : 0;
    const int useGender = valid ? gender    : 0;
    const int useMotion = valid ? motion    : 0;
    const int useAnim   = valid ? animState : 127;

    // Motion_BuildPathAndLoad 0x4D7390 case 1 @0x4D741E:
    //   "C%03d%03d%03d.MOTION" % (race+3*gender+1, motion+1, animState+1)
    // == MotionCache::GetForPlayer(race, gender, weaponType=motion, animState).
    const MotionPalette* mp = motions.GetForPlayer(useRace, useGender, useMotion, useAnim);
    // Unresolved palette (missing .MOTION file / parse failure) -> invalid BonePalette.
    // Render() treats this as a HARD BAIL (nothing drawn), which reproduces the
    // null `dword_8E8B40` fallback of SObject_DrawEx + the `jz` of Model_Render — cf. Render().
    if (!mp) return {};

    // animTime is a frame CURSOR (this[15719]), NOT a global clock:
    // SObject_DrawEx 0x4D9330 @0x4D946D passes it AS-IS as the 8th arg of Model_Render
    // 0x40EBB0, which does `ftol(animTime)` @0x40EBEA -> frame. -> SampleByCursor.
    // ⚠ Known, BENIGN divergence: Model_Render EXITS WITHOUT DRAWING if the frame is
    //   out of [0, frameCount-1] (@0x40EBF1 `jl` / @0x40EBFD `jg`), whereas SampleByCursor
    //   CLAMPS. No effect here: the CharSelect flow already clamps this[15719] to
    //   duration-1 (@0x51C6AE), so the cursor stays in range.
    return MotionCache::SampleByCursor(*mp, animTime);
}

//  Camera / light
void CharPreview3D::BuildViewMatrix(D3DXMATRIX& out) {
    // Gfx_BeginFrame 0x6A2280 @0x6A2352: D3DXMatrixLookAtLH(view, eye, at, up=(0,1,0)).
    // eye = g_GfxRenderer+792 (0x800130) and at = +804 (0x80013C), set by
    // Scene_CharSelectUpdate @0x51BDED-0x51BE21 (eye (0,5,-28) / at (0,10,0)).
    D3DXVECTOR3 eye = CameraEye(), at = CameraTarget(), up = CameraUp();
    D3DXMatrixLookAtLH(&out, &eye, &at, &up);
    // DO NOT touch the projection: neither Gfx_BeginFrame nor Scene_CharSelectRender set it
    // (cf. header banner §5). It stays the one from Gfx_InitDevice 0x69B9B0.
}

void CharPreview3D::ApplyLight(MeshRenderer& mesh) {
    // D3DLIGHT9 0x18C5358 (Scene_CharSelectRender @0x51D034-0x51D205), then
    // SetLight(0, &light) vtbl+0xCC @0x51D226:
    //   Direction = normalize(-1,-1,-1)  (flt_7EDA10 = BF800000 = -1.0f, Vec3_Normalize @0x51D1CE)
    //   Ambient   = (0.8, 0.8, 0.8, 1.0) (flt_7A9784 = 3F4CCCCD = 0.8f)  @0x51D11A
    //   Diffuse   = (0.8, 0.8, 0.8, 1.0) — SAME constant                 @0x51D070
    //   Specular  = (0,0,0,1) and Type = 3 (DIRECTIONAL, @0x51D034): MeshRenderer::SetLight
    //   exposes neither specular nor type (its VS is already purely directional) -> not passed.
    D3DXVECTOR3 dir(-1.0f, -1.0f, -1.0f);
    D3DXVec3Normalize(&dir, &dir);
    mesh.SetLight(dir, D3DXVECTOR3(0.8f, 0.8f, 0.8f), D3DXVECTOR3(0.8f, 0.8f, 0.8f));
    // TODO [0x51D226]: the binary emits NEITHER LightEnable NOR D3DRS_LIGHTING in this
    // function; unknown whether this D3DLIGHT9 (fixed-function) affects the skinned path
    // (Model_DrawSkinnedSubset 0x40CA40 rebinds its own shaders). Values reproduced
    // faithfully, actual consumption NOT PROVEN (spec §13.3).
}

//  CREATE branch — Char_RenderModel 0x527020 @0x527033 (isCreate != 0)
CharPreviewResult CharPreview3D::BuildFromChoices(ModelCache& models, MotionCache& motions,
                                                  const CharPreviewChoices& choices,
                                                  const CharPreviewPose&    pose) {
    CharPreviewResult r;

    // ⚠ In THIS branch, `job` (+36) IS the race index: `mov edx, [ecx+24h]`
    // @0x527051 -> a2 of PcModel_ResolveEquipSlot; `mov eax, [edx+2Ch]` @0x52704A -> a3.
    const int race   = choices.job;
    const int gender = choices.gender;

    // v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, +36, +44, motion, animState, 1,0,0) @0x52705F
    r.palette = ResolveBonePalette(motions, race, gender, pose.motion, pose.animState, pose.animTime);

    // --- EXACT DRAW ORDER (do not reorder: hair BEFORE face) ---
    // 1. HAIR — flt_F5B21C[216*race + 108*gender + 36*hair] @0x5270B8 (a5=1, entry[hair])
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::Hair, race, gender, choices.hairColor);
    // 2. FACE  — flt_F59A7C[504*race + 252*gender + 36*face] @0x527113 (a5=0, entry[face])
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::Face, race, gender, choices.face);
    // 3. EQUIP A (default body piece) — flt_F5CFEC[3672*race + 1836*gender] @0x527160
    //    = catalog a5=2 + 5040 o = entry[35] FROZEN -> "C%03d003035"
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipA, race, gender, kCreateBodyEntryIndex);
    // 4. EQUIP B (default body piece) — flt_F7282C[3672*race + 1836*gender] @0x5271AD
    //    = catalog a5=3 + 5040 o = entry[35] FROZEN -> "C%03d004035"
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipB, race, gender, kCreateBodyEntryIndex);

    // 5. WEAPON — driven by this[15716] (SCENE), read @0x5271B8: `mov edx, [ecx+0F590h]`.
    //    ⚠ ESPECIALLY NOT rec+216: +216 is only written at CONFIRMATION (0x52669A..0x52675B),
    //    whereas the preview must update LIVE with the `variant` arrows.
    //    The 3 catalogs are a5=4/5/6, ALL at entry[33] FROZEN (4752/144).
    switch (choices.variant) {
        case 0: // flt_F87F4C @0x527230 -> "C%03d005034"
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::WeaponA, race, gender, kCreateWeaponEntryIndex);
            break;
        case 1: // flt_F93FAC @0x527282 -> "C%03d006034"
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::WeaponB, race, gender, kCreateWeaponEntryIndex);
            break;
        case 2: // flt_FA000C @0x5272D4 -> "C%03d007034"
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::WeaponC, race, gender, kCreateWeaponEntryIndex);
            break;
        default:
            // `jmp loc_52744D` @0x5271DD: NO weapon drawn. Add nothing — and
            // especially do NOT fall back to variant 0 (the binary does not do that).
            break;
    }

    // 6. if variant == 2: extra effect attached to a bone — NOT PORTED.
    // TODO [0x5272DF]: the binary does, AFTER the variant-2 weapon:
    //     race == 0 (@0x527308) : Crt_Memset(v27,0,0xC)
    //                             SObject_Draw(flt_FABB5C[72*race+36*gender], /*bone*/2, …) @0x527350
    //                             ModelObj_Draw(unk_B61A54, pass, 0.0, …)                 @0x52736E
    //                             SObject_Draw(flt_FABB5C[…],                /*bone*/3, …) @0x5273B3
    //                             ModelObj_Draw(unk_B61A54, …)                            @0x5273D1
    //     race == 2 (@0x5273E2) : Crt_Memset(v27,0,0xC)
    //                             SObject_Draw(flt_FABB5C[…], /*bone*/2, …)                 @0x52742A
    //                             ModelObj_Draw(unk_B61AE8, …)   <- DIFFERENT object          @0x527448
    //     race == 1             : NOTHING
    // (⚠ spec §5.2 wrongly merges these two branches into a single « race∈{0,2} :
    //  2x SObject_Draw + ModelObj_Draw(unk_B61A54) ».)
    // The flt_FABB5C catalog IS resolved (a5=8 -> CharCatalogSlot::Slot009, "C%03d009001"),
    // but SObject_Draw 0x4D8F90 (extracts a bone transform) and ModelObj_Draw
    // 0x4D71B0 (non-SObject object unk_B61A54/unk_B61AE8) have NO equivalent in
    // Gfx/MeshRenderer.h or Gfx/ModelCache.h. Not ported rather than invented.

    r.valid = !r.pieces.empty();
    return r;
}

//  LIST branch — Char_RenderModel 0x527020 @0x527452 (isCreate == 0)
CharPreviewResult CharPreview3D::BuildFromRecord(ModelCache& models, MotionCache& motions,
                                                 const uint8_t* rec, const CharPreviewPose& pose) {
    CharPreviewResult r;
    if (!rec) return r; // PORT guard (the binary always receives a valid record)

    // ⚠ In THIS branch, race comes from +40 (`mov eax, [edx+28h]` @0x527536), NOT from
    // +36. +36 is only a SENTINEL tested `== 3` (@0x52754A). The two coexist.
    const int32_t race   = ReadI32(rec, kRecOffRace);
    const int32_t gender = ReadI32(rec, kRecOffGender);
    const int32_t job    = ReadI32(rec, kRecOffJob);
    const int32_t face   = ReadI32(rec, kRecOffFace);
    const int32_t hair   = ReadI32(rec, kRecOffHair);
    const int32_t field4 = ReadI32(rec, kRecOffField4);

    // v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, +40, +44, motion, animState, 1,0,0) @0x527544
    r.palette = ResolveBonePalette(motions, race, gender, pose.motion, pose.animState, pose.animTime);

    // 0. Item resolutions — MobDb_GetEntry(mITEM, id) 0x4C3C00 == game::GetItemInfo
    //    (1-BASED accessor: returns 0 if id < 1, id > count, or empty slot).
    //    The binary resolves NINE of them (@0x527463..0x52751B); only the two below are
    //    consumed by the ported pieces. (NB: the one at +232 (v32) is resolved but
    //    NEVER read back, even in the binary — original dead code, not reproduced.)
    const game::ItemInfo* itemEquipA = game::GetItemInfo(static_cast<uint32_t>(ReadI32(rec, kRecOffItemEquipA)));
    const game::ItemInfo* itemEquipB = game::GetItemInfo(static_cast<uint32_t>(ReadI32(rec, kRecOffItemEquipB)));

    // Helmet masking. The binary writes, IDENTICALLY at the two sites @0x5275B3/0x5275CB
    // (cat7 body) and @0x5276E3/0x5276FB (face):
    //     if (v29 && a4[11] == 1) { if (f196 != 37 && f196 != 39) draw; /* else nothing */ }
    //     else                      draw;
    // Contrapositive: we DO NOT draw  <=>  (item present) AND (gender == 1) AND f196 ∈ {37,39}.
    const bool helmetHidesFace =
        itemEquipA != nullptr && gender == 1 &&
        (itemEquipA->field196 == kHelmetHidesFaceA || itemEquipA->field196 == kHelmetHidesFaceB);

    // --- EXACT DRAW ORDER ---
    // 1. category-7 PIECE ("H..."), ONLY if +36 == 3 (@0x52754A) — @0x527615 / @0x527664.
    //    ⭐ Answers the « TODO [0x52754A]: figure out what the guarded block draws » from
    //    Net/CharSelectPackets.h: it's the "H%03d001001.SOBJECT" body (catalog 0x113DF7C,
    //    category 7, folder 007), indexed 72*race + 36*gender in floats = 288/144 o.
    if (job == kJobSentinel && !helmetHidesFace)
        PushPiece(models, r.pieces, BuildCat7BodyStem(race, gender));

    // 2. FACE — guard `if (a4[1] != 1)` @0x527670 then same helmet masking.
    //    a5=0, entry[+48]. TODO [0x527670]: semantics of +4 UNKNOWN (spec §13.7) — we
    //    reproduce the test as-is without giving it meaning.
    if (field4 != 1 && !helmetHidesFace)
        PushCatalogPiece(models, r.pieces, CharCatalogSlot::Face, race, gender, face);

    // 3. HAIR — UNCONDITIONAL (no guard) @0x52780B. a5=1, entry[+52].
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::Hair, race, gender, hair);

    // 4. EQUIP A (record item +136; clothing role NOT PROVEN) — @0x527814:
    //      if (item136) {
    //          if (job==3 && SObject_Load(a5=14[f196]))  draw a5=14[f196];   @0x527906/0x527968
    //          else                                       draw a5=2 [f196];   @0x5279C8
    //      } else                                         draw a5=2 [0];      @0x52785E
    //    (a5=14 = SAME token "003" but kindIndex +7 = alternate model set.)
    if (itemEquipA) {
        const int entry = static_cast<int>(itemEquipA->field196);
        if (job == kJobSentinel && CatalogLoads(models, CharCatalogSlot::EquipAAlt, race, gender, entry))
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipAAlt, race, gender, entry);
        else
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipA, race, gender, entry);
    } else {
        PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipA, race, gender, 0);
    }

    // 5. EQUIP B (record item +184; role NOT PROVEN) — @0x5279D1: structure STRICTLY
    //    identical to EQUIP A, with a5=15 / a5=3 (token "004").
    if (itemEquipB) {
        const int entry = static_cast<int>(itemEquipB->field196);
        if (job == kJobSentinel && CatalogLoads(models, CharCatalogSlot::EquipBAlt, race, gender, entry))
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipBAlt, race, gender, entry);
        else
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipB, race, gender, entry);
    } else {
        PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipB, race, gender, 0);
    }

    // 6. WEAPON (record item +216, v31) — @0x528263. GUARD `if (v31)` @0x528263, then switch on
    //    typeCode(+188), bound `(unsigned)(typeCode-13) <= 8` (table byte_528F6C=[0,1,2,0,1,2,0,1,2]):
    //      {13,16,19} -> WeaponA (cat5, unk_F86CBC) @0x5282A8
    //      {14,17,20} -> WeaponB (cat6, unk_F92D1C) @0x52830E
    //      {15,18,21} -> WeaponC (cat7, unk_F9ED7C) @0x528374
    //    Entry = field196(+196) - 1 (@0x5282f9); catalog indexing identical to other pieces
    //    (race*0x4020 + gender*0x2010 + entry*0x90). This switch IS WeaponClassFromTypeCode (1/2/3 =
    //    A/B/C, 0 otherwise @0x4cc904). ⚠ NOT PORTED (documented below): the animated weapon TRAIL
    //    (@0x5283d5..0x528B1D: SObject_Draw/ModelObj_Draw with no equivalent) and accessories v33-v36.
    if (const game::ItemInfo* itemWeapon =
            game::GetItemInfo(static_cast<uint32_t>(ReadI32(rec, kRecOffItemWeapon)))) {
        const int weaponClass = game::WeaponClassFromTypeCode(itemWeapon->typeCode); // 1/2/3, 0 = none
        const int entry = static_cast<int>(itemWeapon->field196) - 1;                // field196 - 1
        CharCatalogSlot wslot = CharCatalogSlot::WeaponA;
        bool draw = (entry >= 0);
        switch (weaponClass) {
            case 1: wslot = CharCatalogSlot::WeaponA; break; // {13,16,19}
            case 2: wslot = CharCatalogSlot::WeaponB; break; // {14,17,20}
            case 3: wslot = CharCatalogSlot::WeaponC; break; // {15,18,21}
            default: draw = false; break;                    // typeCode outside [13,21] -> no weapon (def_5282A1)
        }
        if (draw) PushCatalogPiece(models, r.pieces, wslot, race, gender, entry);
    }

    // 7. REMAINING EQUIPMENT SLOTS — NOT PORTED (the WEAPON v31 above IS now ported).
    // TODO [0x527B8E]: the binary continues with, in order —
    //     v33 (item +248) : flt_100EA3C / flt_102901C   @0x527BFE / @0x527C61
    //     v34 (item +264) : flt_101398C / unk_1012DBC / flt_102D39C
    //                       + animated effects unk_104BFCC/104BEAC/104C05C/104BF3C
    //                                                    @0x527C6A..0x5280A2
    //     v35 (item +280) : flt_1020FDC / flt_103B5BC   @0x52811B / @0x52817E
    //     v36 (item +296) : flt_1024FFC / flt_103F5DC   @0x5281F7 / @0x52825A
    //     v31 (item +216, the WEAPON) : the weapon PIECE is PORTED above (step 6). Only the
    //                       animated TRAIL unk_10435FC @0x5283d5..0x528B1D remains NOT PORTED.
    //     Entry (item +120) : flt_FABEBC (a5=9) if typeCode==8 ; flt_FB7BBC (a5=20) if ==29
    //                                                    @0x528B94 / @0x528C03
    //     if a4[1] == 1 : flt_FB0C5C (a5=10)             @0x528C59
    //     set bonus (Equip_GetSetBonusId 0x548CE0 % 10 -> unk_B67C9C..unk_B681D0)
    //                                                    @0x528C71..0x528DC0
    //     if a4[1427] (+5708) > 0 : unk_B682F8..unk_B68B10 (12 cases)  @0x528DCF..0x528F53
    // Catalogs 0x100xxxx..0x104xxxx are OUTSIDE the category-1 region (which ends
    // after 0xFB7BBC): they belong to other SObject_BuildPath categories
    // (5/6/7/10/11) whose AssetMgr_InitAllSlots 0x4DEB50 writer loops have NOT been
    // traced. The 4 already resolved (a5=8/9/10/20) are exposed via CharCatalogSlot
    // but their GUARDS depend on the unported slots -> not wired.
    // Accepted consequence: the list shows body + face + hair + torso + legs + WEAPON,
    // without the v33-v36 accessories or the weapon trail. No coordinates or paths invented.

    r.valid = !r.pieces.empty();
    return r;
}

//  Rendering a single pass
void CharPreview3D::Render(MeshRenderer& mesh, const CharPreviewResult& r,
                           const CharPreviewPose& pose, int pass) {
    // Model_Render 0x40EBB0 @0x40EBD5: `dec eax ; cmp eax, 1 ; ja -> exit` => only
    // passes 1 and 2 draw. We reproduce the filter rather than let kPassBoth through
    // (which the binary NEVER emits here).
    if (pass != MeshRenderer::kDrawPass_Opaque && pass != MeshRenderer::kDrawPass_Blend) return;
    if (!r.valid) return;

    // INVALID PALETTE => DRAW NOTHING (and especially NOT in identity pose).
    // Proven chain: SObject_DrawEx 0x4D9330 @0x4D93DE tests
    //   `Motion_EnsureLoaded(a7,a8) && Motion_GetData(a7,a8)`; if either fails it still calls
    //   Model_Render but with the FALLBACK palette `dword_8E8B40` (@0x4D93AA).
    // But `data_refs(0x8E8B40)` = 8 references, ALL `push offset`, ZERO writes: it's a
    // .bss block (g_ModelMotionArray+16, in the 32-byte header preceding the atlas
    // slots at 0x8E8B50) that stays NULL. Model_Render 0x40EBB0 @0x40EBDE then does
    // `mov esi,[ebp+arg_14] ; cmp dword ptr [esi],0 ; jz loc_40EED2` -> HARD BAIL.
    // => missing motion == model NOT DRAWN. MeshRenderer::DrawModel, by contrast, would
    // fall back to identity (cf. Gfx/PlayerPaperdoll.cpp): that would be a T-pose the
    // binary NEVER shows. We reproduce the hard bail.
    if (!r.palette.Valid()) return;

    const D3DXVECTOR3 position(pose.pos[0], pose.pos[1], pose.pos[2]); // &this[15720] (+0xF5A0)
    // v10 = (0.0, yaw, 0.0) — SObject_DrawEx @0x4D9359-0x4D9364 ; yaw = this[15724], in
    // DEGREES (Model_Render converts via *0.01745329).
    const D3DXVECTOR3 rotationDeg(0.0f, pose.yawDeg, 0.0f);
    // v11 = (1.0, 1.0, 1.0) @0x4D9369-0x4D9373. ⚠ NOT 20.0: cf. header banner §5 (the 20.0f
    // of flt_7ED9F8 is the frustum sphere diameter, not a scale).
    const D3DXVECTOR3 scale(kCharPreviewScale, kCharPreviewScale, kCharPreviewScale);

    // Char_RenderModel draws ALL pieces with the SAME pass, the SAME palette (v37)
    // and the SAME transform: the weapon and accessories are SKINNED to the body
    // palette, never positioned by their own offset.
    for (const SkinnedModel* piece : r.pieces)
        mesh.DrawModel(*piece, position, rotationDeg, scale, r.palette, /*lod=*/0, pass);
}

} // namespace ts2::gfx

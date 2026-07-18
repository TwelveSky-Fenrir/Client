// Gfx/CharPreview3D.h — 3D character preview in CharSelect (scene 4) + appearance
// composition. STRICT overlay on Char_RenderModel 0x527020 and the 4 call sites
// of Scene_CharSelectRender 0x51CED0.
//
// 1. PURPOSE
// Char_RenderModel 0x527020 is called EXACTLY 4 times in the whole binary, all
// from Scene_CharSelectRender, in TWO pairs (pass 1 then pass 2):
//   LIST screen    (this[15714]==1) : (pass=1,isCreate=0) @0x51D361 ; (pass=2,isCreate=0) @0x51D3CC
//                                     record = &unk_1669380 + 0x2768*this[15715]
//   CREATE screen  (this[15714]==2) : (pass=1,isCreate=1) @0x51D429 ; (pass=2,isCreate=1) @0x51D480
//                                     record = &dword_16709B8
// LIST guard: `cmp [eax+0F58Ch], 0FFFFFFFFh ; jnz` @0x51D2ED -> if slot == -1, BOTH
// calls are skipped (no model drawn).
//
// Real signature (IDA frame + push order @0x51D2FB-0x51D361):
//   Char_RenderModel(this=scene@ecx, int pass, int isCreate, DWORD* rec,
//                    int motion    = this[15717] (+0xF594),
//                    int animState = this[15718] (+0xF598),
//                    float animTime= this[15719] (+0xF59C),
//                    float* pos    = &this[15720] (+0xF5A0),
//                    int rotBlock  = &this[15723] (+0xF5AC))
// yaw = *(float*)(rotBlock+4) = this[15724] (+0xF5B0) — `fld dword ptr [ecx+4]` @0x527076.
//
// 2. ⭐ APPEARANCE CATALOG RESOLUTION — GAP §13.5/§13.6 CLOSED
// The consolidated spec stated (§13.6): "No writer found for flt_F59A7C /
// F5B21C / F5CFEC / F7282C / F87F4C / F93FAC / FA000C / FABB5C => face/hair/body/weapon
// asset paths UNKNOWN — main remaining gap", and (§13.5) "the 4 catalogs with 51 variants
// = SLOTs {2,3,14,15} and the 3 with 57 = SLOTs {4,5,6}, but NOT WHICH IS WHICH =>
// C%03d{SLOT+1}%03d.SOBJECT paths not reproducible".
//
// BOTH ARE RESOLVED HERE. `data_refs` did not see the writers because
// AssetMgr_InitAllSlots 0x4DEB50 writes via a COMPUTED POINTER (`lea ecx, disp[base+idx]`),
// never via a direct symbol reference. Direct disassembly of the loops gives the
// complete table. Base: `var_18` = g_ModelMotionArray = 0x8E8B30 (App_Init passes
// ecx=0x8E8B30 @0x46224B; cross-checked 13/13 times below).
//
//   for (race<3) for (gender<2) for (i<count)
//       SObject_BuildPath(g_ModelMotionArray + disp + race*rs + gender*gs + i*144,
//                         /*category*/1, race, gender, /*a5=slot*/S, /*a6*/f(i), 0);
//
// SObject_BuildPath 0x4D89C0 case 1 (default @0x4D8BA7):
//       "G03_GDATA\D04_GSOBJECT\001\C%03d%03d%03d.SOBJECT" % (race+3*gender+1, a5+1, a6+1)
// special cases a5=14 @0x4D8A30 / a5=15 @0x4D8A5E: FROZEN token "003"/"004" and kindIndex
// **+7** instead of +1 (ALTERNATE model set); a5=20 @0x4D8B44: token "015", +1.
//
//  a5 | call EA        | base (=0x8E8B30+disp) | disp     | race/gender | n  | a6    | token
//  ---+----------------+-----------------------+----------+-------------+----+-------+------
//   0 | 0x4DF3CC       | 0xF59A7C flt_F59A7C   | 0x670F4C | 2016 / 1008 |  7 | i     | 001  FACE
//   1 | 0x4DF426       | 0xF5B21C flt_F5B21C   | 0x6726EC |  864 /  432 |  3 | i     | 002  HAIR
//   2 | 0x4DF483       | 0xF5BC3C unk_F5BC3C   | 0x67310C |14688 / 7344 | 51 | i-1   | 003  TORSO
//  14 | 0x4DF8C1       | 0xF6685C flt_F6685C   | 0x67DD2C |14688 / 7344 | 51 | i-1   | 003 (kind+7)
//   3 | 0x4DF4E0       | 0xF7147C unk_F7147C   | 0x68894C |14688 / 7344 | 51 | i-1   | 004  LEGS
//  15 | 0x4DF91E       | 0xF7C09C flt_F7C09C   | 0x69356C |14688 / 7344 | 51 | i-1   | 004 (kind+7)
//   4 | 0x4DF53A       | 0xF86CBC unk_F86CBC   | 0x69E18C |16416 / 8208 | 57 | i     | 005  WEAPON A
//   5 | 0x4DF594       | 0xF92D1C unk_F92D1C   | 0x6AA1EC |16416 / 8208 | 57 | i     | 006  WEAPON B
//   6 | 0x4DF5EE       | 0xF9ED7C unk_F9ED7C   | 0x6B624C |16416 / 8208 | 57 | i     | 007  WEAPON C
//   8 | 0x4DF6A2       | 0xFABB5C flt_FABB5C   | 0x6C302C |  288 /  144 |  1 | i     | 009
//   9 | 0x4DF6FC       | 0xFABEBC flt_FABEBC   | 0x6C338C | 6624 / 3312 | 23 | i     | 010
//  10 | 0x4DF756       | 0xFB0C5C flt_FB0C5C   | 0x6C812C |  288 /  144 |  1 | i     | 011
//  20 | 0x4DFAE0       | 0xFB7BBC flt_FB7BBC   | 0x6CF08C | 2304 / 1152 |  8 | i     | 015
// (« a6 = i-1 » => the printed file NUMBER, a6+1, equals i ; « a6 = i » => it equals i+1.)
// Catalog outside category 1:
//  cat 7 | 0x4E01D7 | 0x113DF7C flt_113DF7C | 0x85544C | 288 / 144 | 1 | a5=0,a6=0 -> "H%03d001001"
//        (SObject_BuildPath case 7 @0x4D8CD5 : "…\007\H%03d%03d%03d.SOBJECT" % (k+1,a5+1,a6+1))
//
// CONTIGUITY PROOF (each catalog ends exactly where the next begins):
//   0xF59A7C +3*2016=0xF5B21C ✓ +3*864=0xF5BC3C ✓ +3*14688=0xF6685C ✓ +…=0xF7147C ✓
//   +…=0xF7C09C ✓ +…=0xF86CBC ✓ +3*16416=0xF92D1C ✓ +…=0xF9ED7C ✓ ; 0xFABB5C+3*288=0xFABEBC ✓
//
// INDEPENDENT DISK VERIFICATION (G03_GDATA\D04_GSOBJECT\001, counts for k=1):
//   token 001 -> 7 files (=n)   002 -> 3 (=n)    009 -> 1 (=n)
//   010 -> 23 (=n)              011 -> 1 (=n)    015 -> 8 (=n)
//   -> the 6 non-trivial counts fall EXACTLY on the loop bounds read in IDA.
//   The kindIndex values present for token 003 are 001..012 = {k+1 : k∈[0,6)} ∪ {k+7}:
//   direct confirmation of the a5=14 special case (C007003011.SOBJECT exists).
//
// 3. DRAWN PIECES — EXACT ORDER
// CREATE BRANCH (isCreate != 0, @0x527033) — indexed by field +36 ("job") as
// RACE, +44 as gender. No equipment resolution (no MobDb_GetEntry).
//   1. HAIR   flt_F5B21C[216*rec36 + 108*rec44 + 36*rec52]  @0x5270B8 -> a5=1, entry[hair]
//   2. FACE   flt_F59A7C[504*rec36 + 252*rec44 + 36*rec48]  @0x527113 -> a5=0, entry[face]
//   3. BODY A flt_F5CFEC[3672*rec36 + 1836*rec44]           @0x527160
//        0xF5CFEC - 0xF5BC3C = 5040 = 35*144 -> a5=2, entry[35] -> "C%03d003035"
//        (this is the « unidentified 5040 o (35x144) gap » of spec §13.5: it is
//         NOT a gap, it's an ENTRY OFFSET into catalog a5=2.)
//   4. BODY B flt_F7282C[3672*rec36 + 1836*rec44]           @0x5271AD
//        0xF7282C - 0xF7147C = 5040 = 35*144 -> a5=3, entry[35] -> "C%03d004035"
//   5. WEAPON, based on this[15716] (SCENE, NOT rec+216) — `mov edx,[ecx+0F590h]` @0x5271B8:
//        variant 0 -> flt_F87F4C @0x527230 : 0xF87F4C-0xF86CBC = 4752 = 33*144 -> a5=4, e[33] -> "C%03d005034"
//        variant 1 -> flt_F93FAC @0x527282 : same -> a5=5, e[33] -> "C%03d006034"
//        variant 2 -> flt_FA000C @0x5272D4 : same -> a5=6, e[33] -> "C%03d007034"
//        default   -> NOTHING (`jmp loc_52744D` — no weapon)
//   6. if variant==2: effect attached to a bone (SObject_Draw + ModelObj_Draw) — NOT PORTED,
//      cf. TODO [0x5272DF] in the .cpp. ⚠ Spec §5.2 describes this block as « if
//      variant==2 AND race∈{0,2}: 2x SObject_Draw (bone 2 then 3) + ModelObj_Draw(unk_B61A54) »;
//      that is WRONG for race==2 — disassembly gives TWO DISTINCT branches:
//        race==0 @0x527308 : bone 2 + ModelObj_Draw(unk_B61A54) THEN bone 3 + ModelObj_Draw(unk_B61A54)
//        race==2 @0x5273E2 : bone 2 + ModelObj_Draw(unk_B61AE8)  <- ONLY ONE, and DIFFERENT object
//        race==1           : NOTHING
//
// LIST BRANCH (isCreate == 0, @0x527452) — indexed by +40 (race) / +44 (gender);
// +36 only serves as a SENTINEL `== 3` (`cmp dword ptr [ecx+24h], 3` @0x52754A).
//   0. 9 MobDb_GetEntry(mITEM, rec+{120,136,184,216,232,248,264,280,296}) resolutions
//      @0x527463..0x52751B. NB: the one at +232 (v32) is computed but NEVER read back.
//   1. if rec+36==3 : cat7 BODY flt_113DF7C[72*rec40 + 36*rec44] @0x527615/0x527664
//      -> "H%03d001001" % (race+3*gender+1).  ⭐ Answers the « TODO [0x52754A]: figure out
//      what the guarded block actually draws » from Net/CharSelectPackets.h.
//   2. if rec+4 != 1 : FACE a5=0 entry[rec+48] @0x527753/0x5277B0
//   3. HAIR a5=1 entry[rec+52] @0x52780B (unconditional)
//   4. TORSO: item(+136) ? (rec36==3 && Load(a5=14[f196]) ? a5=14[f196] : a5=2[f196]) : a5=2[0]
//              @0x527968 / 0x5279C8 / 0x52785E
//   5. LEGS  : item(+184) ? (rec36==3 && Load(a5=15[f196]) ? a5=15[f196] : a5=3[f196]) : a5=3[0]
//              @0x527B25 / 0x527B85 / 0x527A1B
//   6. remaining slots (+216 weapon, +248, +264, +280, +296, +120) -> NOT PORTED, catalogs
//      0x100EA3C/0x102901C/0x1012DBC/0x102D39C/0x1020FDC/0x103B5BC/0x1024FFC/0x103F5DC/
//      0x10435FC/0x104Bxxxx unresolved. TODO [0x527B8E] in the .cpp.
// Helmet masking: `if (item136 && rec+44==1) { if (f196 != 37 && f196 != 39) draw; }
// else draw;` @0x5275B3/0x5275CB (cat7 body) and @0x5276E3/0x5276FB (face) — some
// helmets (f196 37/39) hide face+cat7 body on gender 1.
//
// 4. BONE PALETTE — THE ~500-CASE SWITCH IS DEAD CODE HERE
// A SINGLE palette shared by ALL pieces:
//   v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, race, gender, motion, animState, 1, 0, 0)
//         @0x52705F (create) / @0x527544 (list)
// PcModel_ResolveEquipSlot 0x4E46A0:
//   guard @0x4E46CC : `if (a2 > 2 || a3 > 1 || !Motion_IsValidWeaponPose(a4,a5))
//                        return this + 2644772;`  <- FALLBACK slot
//   a8 = 0 HARDCODED at both sites => the switch on a8 (~500 cases) ALWAYS falls to LABEL_152
//   @0x4E5708 : `if (a4%2 || a5!=1 || a6<=112) return this + 479232*a2 + 159744*a3
//                                                     + 19968*a4 + 156*a5 + 2624960;`
//   a6 = 1 HARDCODED => `a6 <= 112` is ALWAYS TRUE => this return is ALWAYS taken.
//   -> slot = g_ModelMotionArray + 2624960 + 159744*(3*race+gender) + 19968*motion + 156*animState
// Writer loop Motion_BuildPathAndLoad @0x4DEFC6-0x4DF00C : disp **0x280DC0 = 2624960**
// (identical to the constant above, bit-for-bit), strides 0x75000/0x27000/0x4E00/0x9C
// = 479232/159744/19968/156 ✓, 128 animStates.
// Motion_BuildPathAndLoad 0x4D7390 case 1 @0x4D741E :
//   "G03_GDATA\D03_GMOTION\001\C%03d%03d%03d.MOTION" % (race+3*gender+1, motion+1, animState+1)
// => EXACTLY MotionCache::GetForPlayer(race, gender, motion, animState).
// Fallback: 2644772 - 2624960 = 19812 = 127*156 -> (race0, gender0, motion0, animState127)
//   = "C001001128.MOTION" -> GetForPlayer(0,0,0,127).
// Motion_IsValidWeaponPose 0x4E3A30 : animState==1 -> motion<8 (@0x4E3A7D) ;
//                                     animState==3 -> motion ∈ {0,2,4,6} (@0x4E3ACF).
// CharSelect uses ONLY animState 1 (Idle, `this[15718]=1` @0x51C363) and 3 (Entering,
// @0x52516F) — NEVER 0 (contradicts Gfx/PlayerPaperdoll.cpp:21, which passes animState=0).
//
// 5. CAMERA / LIGHT / SCALE
// CAMERA — written ONCE on the 0->1 sub-state transition (`cmp [edx+8], 1Eh` @0x51BDE0,
// i.e. frameCounter >= 30), on BOTH singletons:
//   g_GfxRenderer : 0x800130=0.0 @0x51BDED · 0x800134=flt_7EDA1C @0x51BDF9 ·
//                   0x800138=flt_7A9764 @0x51BE05 · 0x80013C=0.0 @0x51BE0D ·
//                   0x800140=flt_7A8D74 @0x51BE19 · 0x800144=0.0 @0x51BE21
//   identical copy -> g_GxdRenderer 0x18C51C0..0x18C51D4 @0x51BE2C-0x51BE65
//   flt_7EDA1C = 40 A0 00 00 = 5.0f | flt_7A9764 = C1 E0 00 00 = -28.0f
//   flt_7A8D74 = 41 20 00 00 = 10.0f   (get_bytes, verified byte-for-byte)
//   => eye = (0, 5, -28) ; target = (0, 10, 0).
// 0x800130 = g_GfxRenderer(0x7FFE18) + 792 and 0x80013C = +804: these are EXACTLY the
// eye/at arguments of Gfx_BeginFrame @0x6A2352:
//   `up = (0,1,0)` @0x6A233A-0x6A234A ; `D3DXMatrixLookAtLH(this+828, this+792, this+804, up)`
//   then SetTransform(D3DTS_VIEW=2, this+828) @0x6A2363.
//   -> ⭐ the "up" from spec §13.4 ("not proven") IS proven: (0,1,0).
// ⚠ PROJECTION: Gfx_BeginFrame does NOT touch it, and neither does Scene_CharSelectRender
//   (the only matrix memcpys @0x51CF32-0x51CF4D copy the WORLD 0x800244 and VIEW
//   0x800154 to the GXD). The projection is the APPLICATION-level one set at boot by
//   Gfx_InitDevice 0x69B9B0 (D3DXMatrixPerspectiveFovLH @0x69BFC6, fovY = runtime field
//   +0x80 * flt_7BB26C — cf. Gfx/Camera.h: 45 deg). CharPreview3D MUST NOT touch it:
//   it provides ONLY the view (BuildViewMatrix below). FOV remains TODO §13.4.
//
// LIGHT — D3DLIGHT9 @ 0x18C5358, filled EVERY render frame then SetLight(0,&light)
// (vtbl+0xCC) @0x51D226:
//   Type      = 3 (D3DLIGHT_DIRECTIONAL)          `mov ds:dword_18C5358, 3` @0x51D034
//   Diffuse   = (0.8, 0.8, 0.8, 1.0)              @0x51D070-0x51D093
//   Specular  = (0.0, 0.0, 0.0, 1.0)              @0x51D0BF-0x51D0E2
//   Ambient   = (0.8, 0.8, 0.8, 1.0)              @0x51D11A-0x51D13D
//   Position  = (0, 0, 0)                         @0x51D160-0x51D178
//   Direction = normalize(-1, -1, -1)             @0x51D1A7 then Vec3_Normalize @0x51D1CE
//   Range/Falloff/Atten0..2/Theta/Phi = 0         @0x51D1D5-0x51D205
//   flt_7A9784 = CD CC 4C 3F = 0.8f | flt_7EDA10 = BF 80 00 00 = -1.0f (verified byte-for-byte)
// ⚠ TODO [0x51D226]: NO LightEnable or D3DRS_LIGHTING in Scene_CharSelectRender —
//   unknown whether this D3DLIGHT9 (FIXED-FUNCTION state) actually affects the SKINNED
//   path, which rebinds its own shaders in Model_DrawSkinnedSubset 0x40CA40. ApplyLight()
//   pushes the same values into MeshRenderer (closest equivalent), with no certainty
//   that the binary consumes them. Spec §13.3, undecided.
//
// SCALE — ⚠ CORRECTION OF SPEC §5 (« Scale: flt_7ED9F8 = 20.0f »): this 20.0f is
//   NOT a scale. SObject_DrawEx 0x4D9330 @0x4D946D passes it as arg_0 of Model_Render
//   0x40EBB0, which uses it ONLY for the frustum BOUNDING SPHERE:
//   center = pos + (0, 20.0*dbl_7ED9D0, 0), radius = 20.0*dbl_7ED9D0, with
//   dbl_7ED9D0 = 3F E0 00 00 00 00 00 00 = 0.5 -> center.y = pos.y+10, radius 10
//   (`fmul dbl_7ED9D0` @0x40EC19 ; Frustum_IntersectsSphere @0x40EC3A).
//   The REAL scale vector is v11 = (1.0, 1.0, 1.0), set @0x4D9369-0x4D9373 and passed
//   as arg_4. The model is thus at scale 1 — drawing at 20 would make it 20x too big.
//
// PASSES — `pass ∈ {1,2}` mandatory: Model_Render @0x40EBD5 does `dec eax ; cmp eax,1 ;
//   ja -> exit` => any other value draws NOTHING. The binary draws the ENTIRE paperdoll
//   in pass 1, then the ENTIRE paperdoll in pass 2 (the 4 sites are two adjacent
//   pairs): the caller must therefore call Render(...,1) THEN Render(...,2) —
//   especially NOT both passes per piece (cf. Gfx/MeshRenderer.h, kPassBoth note).
#pragma once
#include "Gfx/MeshRenderer.h"   // gfx::MeshRenderer / gfx::SkinnedModel / gfx::BonePalette
#include "Gfx/ModelCache.h"     // gfx::ModelCache
#include "Gfx/MotionCache.h"    // gfx::MotionCache
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::gfx {

//  Choice bounds — ALL derived from AssetMgr_InitAllSlots 0x4DEB50 loop bounds
//  and PcModel_ResolveEquipSlot 0x4E46A0 guards (@0x4E46CC).
inline constexpr int kCharPreviewRaceCount    = 3; // a2>2 -> fallback (0x4E46CC) ; loops race<3
inline constexpr int kCharPreviewGenderCount  = 2; // a3>1 -> fallback (0x4E46CC) ; loops gender<2
inline constexpr int kCharPreviewFaceCount    = 7; // catalog a5=0, `cmp var_4, 7`  @0x4DF38B
inline constexpr int kCharPreviewHairCount    = 3; // catalog a5=1, `cmp var_4, 3`  @0x4DF3E5
inline constexpr int kCharPreviewVariantCount = 3; // this[15716] ∈ [0,2] (0x526490 / 0x52650B)

// Number of entries per catalog (loop bounds) — exposed for port guards.
inline constexpr int kCharCatalogCount_EquipA = 51; // a5=2/14, `cmp var_4, 33h` @0x4DF43F/0x4DF87D
inline constexpr int kCharCatalogCount_EquipB = 51; // a5=3/15, @0x4DF49C / 0x4DF8DA
inline constexpr int kCharCatalogCount_Weapon = 57; // a5=4/5/6, `cmp var_4, 39h` @0x4DF4F9/0x4DF553/0x4DF5AD

// Category-1 catalog slots used by CharSelect (= a5 of SObject_BuildPath).
//
// NAMING — "never guess" rule:
//   Face/Hair are PROVEN by their consumer (a5=0 indexed by record +48 = the "face"
//   choice, 7 entries = the 7 arrows; a5=1 indexed by +52 = "hair color",
//   3 entries). Weapon* is PROVEN by the LIST branch (switch on the +216 item's
//   typeCode = the WEAPON, @0x5282A1) as much as by the creation `variant`.
//   EquipA/EquipB are DELIBERATELY NEUTRAL: we only know that a5=2/14 is indexed
//   by the item at record +136 and a5=3/15 by the one at +184 (@0x527814 / @0x5279D1). Their
//   actual clothing role (torso? legs? robe?) is NOT proven — do not rename them
//   "Torso"/"Legs" without a consumer that establishes it.
enum class CharCatalogSlot : int {
    Face      = 0,  // 0xF59A7C, token "001", 7 entries  — indexed by record +48
    Hair      = 1,  // 0xF5B21C, token "002", 3 entries  — indexed by record +52
    EquipA    = 2,  // 0xF5BC3C, token "003", 51 entries, kind+1 — record item +136
    EquipAAlt = 14, // 0xF6685C, token "003", 51 entries, kind+7 (alternate set, job==3)
    EquipB    = 3,  // 0xF7147C, token "004", 51 entries, kind+1 — record item +184
    EquipBAlt = 15, // 0xF7C09C, token "004", 51 entries, kind+7
    WeaponA   = 4,  // 0xF86CBC, token "005", 57 entries — variant 0 (create)
    WeaponB   = 5,  // 0xF92D1C, token "006", 57 entries — variant 1
    WeaponC   = 6,  // 0xF9ED7C, token "007", 57 entries — variant 2
    Slot009   = 8,  // 0xFABB5C, token "009", 1 entry — role NOT PROVEN (bone effect, cf. .cpp)
};

// FROZEN entries read by the CREATE branch (entry offsets, cf. header banner §3).
inline constexpr int kCreateBodyEntryIndex   = 35; // (0xF5CFEC-0xF5BC3C)/144 = (0xF7282C-0xF7147C)/144
inline constexpr int kCreateWeaponEntryIndex = 33; // (0xF87F4C-0xF86CBC)/144 = … = 4752/144

//  Camera / scale / light — literal constants verified byte-for-byte.
// Frustum bounding sphere diameter (flt_7ED9F8 = 20.0f) — NOT a scale.
inline constexpr float kCharPreviewBoundSize = 20.0f;
// Real scale vector passed to Model_Render (v11, @0x4D9369-0x4D9373).
inline constexpr float kCharPreviewScale     = 1.0f;

//  CREATE form choices (branch isCreate != 0).
//  ⚠ `job` (+36) IS the race index in this branch (`mov edx,[ecx+24h]` @0x527051);
//     the LIST branch, however, reads +40 (@0x527536). Do not confuse them.
struct CharPreviewChoices {
    int32_t job       = 0; // record +36 (dword_16709DC), 0..2 — preview RACE index
    int32_t gender    = 0; // record +44 (dword_16709E4), 0..1
    int32_t face      = 0; // record +48 (dword_16709E8), 0..6
    int32_t hairColor = 0; // record +52 (dword_16709EC), 0..2
    int32_t variant   = 0; // ⚠ SCENE this[15716] (+0xF590), 0..2 — NOT the record (@0x5271B8)
};

//  Preview pose — the last 5 arguments of Char_RenderModel.
struct CharPreviewPose {
    int32_t motion    = 0;    // this[15717] (+0xF594) — 0 at Init ; 2*weapon class when armed
    int32_t animState = 1;    // this[15718] (+0xF598) — 1=Idle, 3=Entering. NEVER 0.
    float   animTime  = 0.0f; // this[15719] (+0xF59C) — frame cursor, not a clock
    float   pos[3]    = {0.0f, 0.0f, 0.0f}; // this[15720..22] (+0xF5A0..A8)
    float   yawDeg    = 0.0f; // this[15724] (+0xF5B0) — degrees (Model_Render 0x40EBB0)
};

//  Result: the SHARED palette + pieces IN THE BINARY'S DRAW ORDER.
struct CharPreviewResult {
    BonePalette                      palette{}; // v37 — shared by all pieces
    std::vector<const SkinnedModel*> pieces;    // exact Char_RenderModel order
    bool                             valid = false;
};

//  CharPreview3D — STATELESS resolver (everything passed via arguments), overlaid on
//  Char_RenderModel 0x527020.
class CharPreview3D {
public:
    // --- Camera (Scene_CharSelectUpdate 0x51BDED-0x51BE65 + Gfx_BeginFrame 0x6A2352) ---
    static D3DXVECTOR3 CameraEye()    { return D3DXVECTOR3(0.0f,  5.0f, -28.0f); } // flt_7EDA1C / flt_7A9764
    static D3DXVECTOR3 CameraTarget() { return D3DXVECTOR3(0.0f, 10.0f,   0.0f); } // flt_7A8D74
    static D3DXVECTOR3 CameraUp()     { return D3DXVECTOR3(0.0f,  1.0f,   0.0f); } // @0x6A233A-0x6A234A

    // View = D3DXMatrixLookAtLH(eye, target, up) — the EXACT operation of Gfx_BeginFrame
    // @0x6A2352. DOES NOT TOUCH the projection (scene 4 never sets it): the caller
    // keeps the application projection and calls mesh.SetCamera(view, projApplicative).
    static void BuildViewMatrix(D3DXMATRIX& out);

    // Pushes the D3DLIGHT9 0x18C5358 light into MeshRenderer (closest equivalent
    // of the shader path). Cf. TODO [0x51D226] in header banner §5.
    static void ApplyLight(MeshRenderer& mesh);

    // --- Composition ---
    // CREATE branch (isCreate != 0, @0x527033): LIVE preview of the form. Recomputed
    // on every choice change (no state cache: just call it again).
    static CharPreviewResult BuildFromChoices(ModelCache& models, MotionCache& motions,
                                              const CharPreviewChoices& choices,
                                              const CharPreviewPose&    pose);

    // LIST branch (isCreate == 0, @0x527452): preview of the selected character.
    // `rec` = RAW 10088-byte record (&unk_1669380 + 0x2768*slot = net::g_CharRecords[slot]).
    // Returns an invalid result if `rec` is null.
    static CharPreviewResult BuildFromRecord(ModelCache& models, MotionCache& motions,
                                             const uint8_t* rec, const CharPreviewPose& pose);

    // Draws ALL pieces of `r` for ONE pass. `pass` MUST be 1 or 2
    // (Model_Render @0x40EBD5); any other value draws nothing, as the binary does.
    // Call TWICE (1 then 2) — never both passes per piece.
    static void Render(MeshRenderer& mesh, const CharPreviewResult& r,
                       const CharPreviewPose& pose, int pass);

    // --- Stems (exposed for diagnostics/testing) ---
    // "C%03d%03d%03d" % (race+3*gender+1 [or +7 if slot∈{14,15}], slot+1 [frozen for the
    // special cases], fileNumber). `entryIndex` = ENTRY INDEX into the catalog; the
    // entry -> file number conversion depends on the catalog (cf. "a6" column).
    // Empty string if race/gender/slot/entryIndex out of bounds.
    static std::string BuildCatalogStem(CharCatalogSlot slot, int race, int gender, int entryIndex);

    // "H%03d001001" % (race+3*gender+1) — category-7 body, guard `rec+36 == 3`.
    static std::string BuildCat7BodyStem(int race, int gender);

    // Shared bone palette (PcModel_ResolveEquipSlot 0x4E46A0), fallback included.
    static BonePalette ResolveBonePalette(MotionCache& motions, int race, int gender,
                                          int motion, int animState, float animTime);
};

} // namespace ts2::gfx

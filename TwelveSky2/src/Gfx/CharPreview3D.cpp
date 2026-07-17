// Gfx/CharPreview3D.cpp — implementation. TOUTE la preuve IDA (table des catalogues,
// ordre des pieces, palette d'os, camera, lumiere, echelle) est dans CharPreview3D.h :
// s'y reporter avant de toucher une seule constante de ce fichier.
#include "Gfx/CharPreview3D.h"
#include "Game/GameDatabase.h"  // game::GetItemInfo / game::ItemInfo (= MobDb_GetEntry(mITEM) 0x4C3C00)
#include <cstdio>
#include <cstring>

namespace ts2::gfx {

namespace {

// ---------------------------------------------------------------------------
//  Offsets de la fiche brute (10088 o) LUS PAR Char_RenderModel 0x527020.
//  Char_RenderModel indexe `a4` en DWORD : a4[N] == fiche + 4*N.
//  Volontairement LOCAUX : Gfx/ ne doit pas dependre de Net/ (inversion de couches, et
//  Net/CharSelectPackets.h tire winsock2 + Game/CharSelectFlow.h). Les 5 offsets communs
//  (36/40/44/48/52) coincident avec net::kCharRecField* — ne les faire diverger JAMAIS.
// ---------------------------------------------------------------------------
constexpr int kRecOffField4 = 4;   // a4[1]  — `if (a4[1] != 1)` @0x527670 conditionne le VISAGE
                                   //          ⚠ semantique INCONNUE (spec §13.7). On reproduit
                                   //          le test, sans pretendre savoir ce qu'il signifie.
constexpr int kRecOffJob    = 36;  // a4[9]  — CREATION : index de RACE (@0x527051)
                                   //          LISTE    : sentinelle `== 3` (@0x52754A)
constexpr int kRecOffRace   = 40;  // a4[10] — LISTE : index de RACE (@0x527536)
constexpr int kRecOffGender = 44;  // a4[11] — a3 de PcModel_ResolveEquipSlot (@0x52752F / @0x52704A)
constexpr int kRecOffFace   = 48;  // a4[12] — index d'entree du catalogue a5=0
constexpr int kRecOffHair   = 52;  // a4[13] — index d'entree du catalogue a5=1
constexpr int kRecOffItemEquipA = 136; // a4[34] — v29 = MobDb_GetEntry(mITEM, …) @0x52747A
constexpr int kRecOffItemEquipB  = 184; // a4[46] — v30 = MobDb_GetEntry(mITEM, …) @0x527491
constexpr int kRecOffItemWeapon  = 216; // a4[54] — v31 = MobDb_GetEntry(mITEM, …) = l'ARME (@0x528263)

// Valeur de la sentinelle +36 (`cmp dword ptr [ecx+24h], 3` @0x52754A). Selectionne le
// CORPS categorie 7 et autorise les catalogues ALTERNATIFS (kind+7) a5=14/15.
constexpr int32_t kJobSentinel = 3;

// field196 des casques qui MASQUENT le visage et le corps cat7 quand gender == 1 :
// `if (*(v29+196) != 37 && *(v29+196) != 39)` @0x5275CB (corps cat7) et @0x5276FB (visage).
constexpr uint32_t kHelmetHidesFaceA = 37;
constexpr uint32_t kHelmetHidesFaceB = 39;

int32_t ReadI32(const uint8_t* rec, int off) {
    int32_t v = 0;
    std::memcpy(&v, rec + off, sizeof(v)); // 32-bit LE, meme boutisme que le binaire cible
    return v;
}

// kindIndex du nom de fichier = race + 3*gender (SObject_BuildPath 0x4D89C0 : `a3 + 3*a4`),
// puis +1 (cas courant) ou +7 (cas speciaux a5=14/15, @0x4D8A30 / @0x4D8A5E).
int KindNumber(int race, int gender, bool alternate) {
    return race + 3 * gender + (alternate ? 7 : 1);
}

// Description d'un catalogue de la categorie 1 (cf. table du bandeau §2 de l'en-tete).
struct CatalogDesc {
    int  token;        // le %03d du MILIEU du nom de fichier (fige pour a5=14/15/20)
    int  count;        // borne de boucle d'AssetMgr_InitAllSlots
    bool alternateKind;// true => kindIndex +7 au lieu de +1
    bool fileNumIsEntry;// true  => a6 = i-1 dans la boucle => numero de fichier == i
                       // false => a6 = i                  => numero de fichier == i+1
};

bool DescribeCatalog(CharCatalogSlot slot, CatalogDesc& out) {
    switch (slot) {
        // slot | token | count | kind+7 | numFichier == indiceEntree
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

// Ajoute la piece `stem` si elle se resout ET n'est pas vide. Chaque piece est
// independamment optionnelle — SObject_DrawEx 0x4D9330 @0x4D9349 sort sans rien dessiner
// si `SObject_Load` echoue : une piece manquante n'annule JAMAIS les autres.
void PushPiece(ModelCache& models, std::vector<const SkinnedModel*>& pieces,
               const std::string& stem) {
    if (stem.empty()) return;
    if (const SkinnedModel* m = models.Get(stem))
        if (!m->Empty()) pieces.push_back(m);
}

// Resout un catalogue puis empile la piece (raccourci des 2 appels ci-dessus).
void PushCatalogPiece(ModelCache& models, std::vector<const SkinnedModel*>& pieces,
                      CharCatalogSlot slot, int race, int gender, int entryIndex) {
    PushPiece(models, pieces, CharPreview3D::BuildCatalogStem(slot, race, gender, entryIndex));
}

// true si le catalogue se resout en un modele reellement chargeable — equivalent du
// `SObject_Load(&flt_F6685C[…], 1)` teste @0x527906 / @0x527AC3 avant de choisir le
// catalogue ALTERNATIF plutot que le catalogue de base.
bool CatalogLoads(ModelCache& models, CharCatalogSlot slot, int race, int gender, int entryIndex) {
    const std::string stem = CharPreview3D::BuildCatalogStem(slot, race, gender, entryIndex);
    if (stem.empty()) return false;
    const SkinnedModel* m = models.Get(stem);
    return m != nullptr && !m->Empty();
}

} // namespace

// ---------------------------------------------------------------------------
//  Stems
// ---------------------------------------------------------------------------
// SObject_BuildPath 0x4D89C0 cas 1 : "…\001\C%03d%03d%03d.SOBJECT".
std::string CharPreview3D::BuildCatalogStem(CharCatalogSlot slot, int race, int gender,
                                            int entryIndex) {
    CatalogDesc d{};
    if (!DescribeCatalog(slot, d)) return {};
    // Bornes de PcModel_ResolveEquipSlot 0x4E46A0 @0x4E46CC (a2>2 / a3>1) + bornes de
    // boucle du catalogue. Le binaire n'a PAS de garde d'indice ici (il indexe a cru) :
    // c'est une garde de PORT, pas une divergence de comportement observable — hors
    // bornes, le binaire lirait un descripteur d'un AUTRE catalogue (comportement non
    // reproductible et non souhaite).
    if (race < 0 || race >= kCharPreviewRaceCount)     return {};
    if (gender < 0 || gender >= kCharPreviewGenderCount) return {};
    if (entryIndex < 0 || entryIndex >= d.count)        return {};

    const int kind    = KindNumber(race, gender, d.alternateKind);
    const int fileNum = d.fileNumIsEntry ? entryIndex : entryIndex + 1;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "C%03d%03d%03d", kind, d.token, fileNum);
    return buf;
}

// SObject_BuildPath 0x4D89C0 cas 7 @0x4D8CD5 : "…\007\H%03d%03d%03d.SOBJECT" avec
// a5 = 0 et a6 = 0 dans l'unique boucle ecrivain @0x4E01D7 -> "H%03d001001".
std::string CharPreview3D::BuildCat7BodyStem(int race, int gender) {
    if (race < 0 || race >= kCharPreviewRaceCount)       return {};
    if (gender < 0 || gender >= kCharPreviewGenderCount) return {};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "H%03d001001", KindNumber(race, gender, /*alternate=*/false));
    return buf;
}

// ---------------------------------------------------------------------------
//  Palette d'os partagee — PcModel_ResolveEquipSlot 0x4E46A0
// ---------------------------------------------------------------------------
BonePalette CharPreview3D::ResolveBonePalette(MotionCache& motions, int race, int gender,
                                              int motion, int animState, float animTime) {
    // Garde @0x4E46CC : `if (a2 > 2 || a3 > 1 || !Motion_IsValidWeaponPose(a4, a5))
    //                       return this + 2644772;`
    // 2644772 - 2624960 (base du tableau de motions) = 19812 = 127*156 -> le slot de repli
    // EST (race 0, gender 0, motion 0, animState 127) = "C001001128.MOTION".
    bool valid = (unsigned)race <= 2u && (unsigned)gender <= 1u;
    if (valid) {
        // Motion_IsValidWeaponPose 0x4E3A30 — seuls les 2 animStates de CharSelect :
        //   case 1 (Idle)     : `if (a1 >= 8) -> 0`               @0x4E3A7D
        //   case 3 (Entering) : a1 ∈ {0,2,4,6} sinon -> 0         @0x4E3ACF
        // Les autres animStates ne sont jamais employes ici (cf. bandeau §4).
        if (animState == 1)      valid = (unsigned)motion < 8u;
        else if (animState == 3) valid = (motion == 0 || motion == 2 || motion == 4 || motion == 6);
        else                     valid = false; // hors des 2 valeurs de CharSelect -> repli
    }

    const int useRace   = valid ? race      : 0;
    const int useGender = valid ? gender    : 0;
    const int useMotion = valid ? motion    : 0;
    const int useAnim   = valid ? animState : 127;

    // Motion_BuildPathAndLoad 0x4D7390 cas 1 @0x4D741E :
    //   "C%03d%03d%03d.MOTION" % (race+3*gender+1, motion+1, animState+1)
    // == MotionCache::GetForPlayer(race, gender, weaponType=motion, animState).
    const MotionPalette* mp = motions.GetForPlayer(useRace, useGender, useMotion, useAnim);
    // Palette non resolue (fichier .MOTION absent / parsing KO) -> BonePalette invalide.
    // Render() la traite en SORTIE SECHE (rien dessine), ce qui reproduit le repli
    // `dword_8E8B40` nul de SObject_DrawEx + le `jz` de Model_Render — cf. Render().
    if (!mp) return {};

    // animTime est un CURSEUR de frame (this[15719]), PAS une horloge globale :
    // SObject_DrawEx 0x4D9330 @0x4D946D le passe TEL QUEL en 8e arg de Model_Render
    // 0x40EBB0, qui fait `ftol(animTime)` @0x40EBEA -> frame. -> SampleByCursor.
    // ⚠ Divergence connue et BENIGNE : Model_Render SORT SANS DESSINER si la frame est
    //   hors [0, frameCount-1] (@0x40EBF1 `jl` / @0x40EBFD `jg`), la ou SampleByCursor
    //   BORNE. Sans effet ici : le flux CharSelect clampe deja this[15719] a duree-1
    //   (@0x51C6AE), donc le curseur reste dans la plage.
    return MotionCache::SampleByCursor(*mp, animTime);
}

// ---------------------------------------------------------------------------
//  Camera / lumiere
// ---------------------------------------------------------------------------
void CharPreview3D::BuildViewMatrix(D3DXMATRIX& out) {
    // Gfx_BeginFrame 0x6A2280 @0x6A2352 : D3DXMatrixLookAtLH(view, eye, at, up=(0,1,0)).
    // eye = g_GfxRenderer+792 (0x800130) et at = +804 (0x80013C), poses par
    // Scene_CharSelectUpdate @0x51BDED-0x51BE21 (eye (0,5,-28) / at (0,10,0)).
    D3DXVECTOR3 eye = CameraEye(), at = CameraTarget(), up = CameraUp();
    D3DXMatrixLookAtLH(&out, &eye, &at, &up);
    // NE PAS toucher a la projection : ni Gfx_BeginFrame ni Scene_CharSelectRender ne la
    // posent (cf. bandeau §5). Elle reste celle de Gfx_InitDevice 0x69B9B0.
}

void CharPreview3D::ApplyLight(MeshRenderer& mesh) {
    // D3DLIGHT9 0x18C5358 (Scene_CharSelectRender @0x51D034-0x51D205), puis
    // SetLight(0, &light) vtbl+0xCC @0x51D226 :
    //   Direction = normalize(-1,-1,-1)  (flt_7EDA10 = BF800000 = -1.0f, Vec3_Normalize @0x51D1CE)
    //   Ambient   = (0.8, 0.8, 0.8, 1.0) (flt_7A9784 = 3F4CCCCD = 0.8f)  @0x51D11A
    //   Diffuse   = (0.8, 0.8, 0.8, 1.0) — MEME constante                @0x51D070
    //   Specular  = (0,0,0,1) et Type = 3 (DIRECTIONAL, @0x51D034) : MeshRenderer::SetLight
    //   n'expose ni speculaire ni type (son VS est deja purement directionnel) -> non passes.
    D3DXVECTOR3 dir(-1.0f, -1.0f, -1.0f);
    D3DXVec3Normalize(&dir, &dir);
    mesh.SetLight(dir, D3DXVECTOR3(0.8f, 0.8f, 0.8f), D3DXVECTOR3(0.8f, 0.8f, 0.8f));
    // TODO [0x51D226] : le binaire n'emet NI LightEnable NI D3DRS_LIGHTING dans cette
    // fonction ; on ignore si ce D3DLIGHT9 (fixed-function) influence le chemin skinne
    // (Model_DrawSkinnedSubset 0x40CA40 rebinde ses propres shaders). Valeurs reproduites
    // fidelement, consommation reelle NON PROUVEE (spec §13.3).
}

// ---------------------------------------------------------------------------
//  Branche CREATION — Char_RenderModel 0x527020 @0x527033 (isCreate != 0)
// ---------------------------------------------------------------------------
CharPreviewResult CharPreview3D::BuildFromChoices(ModelCache& models, MotionCache& motions,
                                                  const CharPreviewChoices& choices,
                                                  const CharPreviewPose&    pose) {
    CharPreviewResult r;

    // ⚠ Dans CETTE branche, `job` (+36) EST l'index de race : `mov edx, [ecx+24h]`
    // @0x527051 -> a2 de PcModel_ResolveEquipSlot ; `mov eax, [edx+2Ch]` @0x52704A -> a3.
    const int race   = choices.job;
    const int gender = choices.gender;

    // v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, +36, +44, motion, animState, 1,0,0) @0x52705F
    r.palette = ResolveBonePalette(motions, race, gender, pose.motion, pose.animState, pose.animTime);

    // --- ORDRE DE DESSIN EXACT (ne pas reordonner : cheveux AVANT visage) ---
    // 1. CHEVEUX — flt_F5B21C[216*race + 108*gender + 36*hair] @0x5270B8 (a5=1, entree[hair])
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::Hair, race, gender, choices.hairColor);
    // 2. VISAGE  — flt_F59A7C[504*race + 252*gender + 36*face] @0x527113 (a5=0, entree[face])
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::Face, race, gender, choices.face);
    // 3. EQUIP A (piece de corps par defaut) — flt_F5CFEC[3672*race + 1836*gender] @0x527160
    //    = catalogue a5=2 + 5040 o = entree[35] FIGEE -> "C%03d003035"
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipA, race, gender, kCreateBodyEntryIndex);
    // 4. EQUIP B (piece de corps par defaut) — flt_F7282C[3672*race + 1836*gender] @0x5271AD
    //    = catalogue a5=3 + 5040 o = entree[35] FIGEE -> "C%03d004035"
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipB, race, gender, kCreateBodyEntryIndex);

    // 5. ARME — pilotee par this[15716] (SCENE), lu @0x5271B8 : `mov edx, [ecx+0F590h]`.
    //    ⚠ SURTOUT PAS rec+216 : +216 n'est ecrit qu'a la CONFIRMATION (0x52669A..0x52675B),
    //    alors que l'apercu doit changer EN DIRECT avec les fleches `variant`.
    //    Les 3 catalogues sont a5=4/5/6, TOUS a l'entree[33] FIGEE (4752/144).
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
            // `jmp loc_52744D` @0x5271DD : AUCUNE arme dessinee. Ne rien ajouter — et
            // surtout NE PAS retomber sur le variant 0 (le binaire ne le fait pas).
            break;
    }

    // 6. si variant == 2 : effet supplementaire attache a un OS — NON PORTE.
    // TODO [0x5272DF] : le binaire fait, APRES l'arme du variant 2 :
    //     race == 0 (@0x527308) : Crt_Memset(v27,0,0xC)
    //                             SObject_Draw(flt_FABB5C[72*race+36*gender], /*os*/2, …) @0x527350
    //                             ModelObj_Draw(unk_B61A54, pass, 0.0, …)                 @0x52736E
    //                             SObject_Draw(flt_FABB5C[…],                /*os*/3, …) @0x5273B3
    //                             ModelObj_Draw(unk_B61A54, …)                            @0x5273D1
    //     race == 2 (@0x5273E2) : Crt_Memset(v27,0,0xC)
    //                             SObject_Draw(flt_FABB5C[…], /*os*/2, …)                 @0x52742A
    //                             ModelObj_Draw(unk_B61AE8, …)   <- objet DIFFERENT       @0x527448
    //     race == 1             : RIEN
    // (⚠ la spec §5.2 fusionne a tort ces deux branches en une seule « race∈{0,2} :
    //  2x SObject_Draw + ModelObj_Draw(unk_B61A54) ».)
    // Le catalogue flt_FABB5C EST resolu (a5=8 -> CharCatalogSlot::Slot009, "C%03d009001"),
    // mais SObject_Draw 0x4D8F90 (extraction d'une transformee d'OS) et ModelObj_Draw
    // 0x4D71B0 (objet non-SObject unk_B61A54/unk_B61AE8) n'ont AUCUN equivalent dans
    // Gfx/MeshRenderer.h ni Gfx/ModelCache.h. Non porte plutot qu'invente.

    r.valid = !r.pieces.empty();
    return r;
}

// ---------------------------------------------------------------------------
//  Branche LISTE — Char_RenderModel 0x527020 @0x527452 (isCreate == 0)
// ---------------------------------------------------------------------------
CharPreviewResult CharPreview3D::BuildFromRecord(ModelCache& models, MotionCache& motions,
                                                 const uint8_t* rec, const CharPreviewPose& pose) {
    CharPreviewResult r;
    if (!rec) return r; // garde de PORT (le binaire recoit toujours une fiche valide)

    // ⚠ Dans CETTE branche, la race vient de +40 (`mov eax, [edx+28h]` @0x527536), PAS de
    // +36. +36 n'est qu'une SENTINELLE testee `== 3` (@0x52754A). Les deux coexistent.
    const int32_t race   = ReadI32(rec, kRecOffRace);
    const int32_t gender = ReadI32(rec, kRecOffGender);
    const int32_t job    = ReadI32(rec, kRecOffJob);
    const int32_t face   = ReadI32(rec, kRecOffFace);
    const int32_t hair   = ReadI32(rec, kRecOffHair);
    const int32_t field4 = ReadI32(rec, kRecOffField4);

    // v37 = PcModel_ResolveEquipSlot(g_ModelMotionArray, +40, +44, motion, animState, 1,0,0) @0x527544
    r.palette = ResolveBonePalette(motions, race, gender, pose.motion, pose.animState, pose.animTime);

    // 0. Resolutions d'items — MobDb_GetEntry(mITEM, id) 0x4C3C00 == game::GetItemInfo
    //    (accesseur 1-BASED : renvoie 0 si id < 1, id > count, ou slot vide).
    //    Le binaire en resout NEUF (@0x527463..0x52751B) ; seuls les deux ci-dessous sont
    //    consommes par les pieces portees. (NB : celui de +232 (v32) est resolu puis
    //    JAMAIS relu, y compris dans le binaire — code mort d'origine, non reproduit.)
    const game::ItemInfo* itemEquipA = game::GetItemInfo(static_cast<uint32_t>(ReadI32(rec, kRecOffItemEquipA)));
    const game::ItemInfo* itemEquipB = game::GetItemInfo(static_cast<uint32_t>(ReadI32(rec, kRecOffItemEquipB)));

    // Masquage par casque. Le binaire ecrit, IDENTIQUEMENT aux deux sites @0x5275B3/0x5275CB
    // (corps cat7) et @0x5276E3/0x5276FB (visage) :
    //     if (v29 && a4[11] == 1) { if (f196 != 37 && f196 != 39) draw; /* sinon rien */ }
    //     else                      draw;
    // Contrapposee : on NE dessine PAS  <=>  (item present) ET (genre == 1) ET f196 ∈ {37,39}.
    const bool helmetHidesFace =
        itemEquipA != nullptr && gender == 1 &&
        (itemEquipA->field196 == kHelmetHidesFaceA || itemEquipA->field196 == kHelmetHidesFaceB);

    // --- ORDRE DE DESSIN EXACT ---
    // 1. PIECE categorie 7 ("H..."), UNIQUEMENT si +36 == 3 (@0x52754A) — @0x527615 / @0x527664.
    //    ⭐ Repond au « TODO [0x52754A] : elucider ce que le bloc garde dessine » de
    //    Net/CharSelectPackets.h : c'est le corps "H%03d001001.SOBJECT" (catalogue 0x113DF7C,
    //    categorie 7, dossier 007), indexe 72*race + 36*gender en flottants = 288/144 o.
    if (job == kJobSentinel && !helmetHidesFace)
        PushPiece(models, r.pieces, BuildCat7BodyStem(race, gender));

    // 2. VISAGE — garde `if (a4[1] != 1)` @0x527670 puis meme masquage par casque.
    //    a5=0, entree[+48]. TODO [0x527670] : semantique de +4 INCONNUE (spec §13.7) — on
    //    reproduit le test tel quel sans lui donner de sens.
    if (field4 != 1 && !helmetHidesFace)
        PushCatalogPiece(models, r.pieces, CharCatalogSlot::Face, race, gender, face);

    // 3. CHEVEUX — INCONDITIONNEL (aucune garde) @0x52780B. a5=1, entree[+52].
    PushCatalogPiece(models, r.pieces, CharCatalogSlot::Hair, race, gender, hair);

    // 4. EQUIP A (item fiche +136 ; role vestimentaire NON PROUVE) — @0x527814 :
    //      if (item136) {
    //          if (job==3 && SObject_Load(a5=14[f196]))  draw a5=14[f196];   @0x527906/0x527968
    //          else                                       draw a5=2 [f196];   @0x5279C8
    //      } else                                         draw a5=2 [0];      @0x52785E
    //    (a5=14 = MEME token "003" mais kindIndex +7 = jeu de modeles alternatif.)
    if (itemEquipA) {
        const int entry = static_cast<int>(itemEquipA->field196);
        if (job == kJobSentinel && CatalogLoads(models, CharCatalogSlot::EquipAAlt, race, gender, entry))
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipAAlt, race, gender, entry);
        else
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipA, race, gender, entry);
    } else {
        PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipA, race, gender, 0);
    }

    // 5. EQUIP B (item fiche +184 ; role NON PROUVE) — @0x5279D1 : structure STRICTEMENT
    //    identique a EQUIP A, avec a5=15 / a5=3 (token "004").
    if (itemEquipB) {
        const int entry = static_cast<int>(itemEquipB->field196);
        if (job == kJobSentinel && CatalogLoads(models, CharCatalogSlot::EquipBAlt, race, gender, entry))
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipBAlt, race, gender, entry);
        else
            PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipB, race, gender, entry);
    } else {
        PushCatalogPiece(models, r.pieces, CharCatalogSlot::EquipB, race, gender, 0);
    }

    // 6. ARME (item fiche +216, v31) — @0x528263. GARDE `if (v31)` @0x528263, puis switch sur
    //    typeCode(+188), borne `(unsigned)(typeCode-13) <= 8` (table byte_528F6C=[0,1,2,0,1,2,0,1,2]) :
    //      {13,16,19} -> WeaponA (cat5, unk_F86CBC) @0x5282A8
    //      {14,17,20} -> WeaponB (cat6, unk_F92D1C) @0x52830E
    //      {15,18,21} -> WeaponC (cat7, unk_F9ED7C) @0x528374
    //    Entree = field196(+196) - 1 (@0x5282f9) ; indexation catalogue identique aux autres pieces
    //    (race*0x4020 + gender*0x2010 + entry*0x90). Ce switch EST WeaponClassFromTypeCode (1/2/3 =
    //    A/B/C, 0 sinon @0x4cc904). ⚠ NON PORTES (documentes plus bas) : la TRAINE animee d'arme
    //    (@0x5283d5..0x528B1D : SObject_Draw/ModelObj_Draw sans equivalent) et les accessoires v33-v36.
    if (const game::ItemInfo* itemWeapon =
            game::GetItemInfo(static_cast<uint32_t>(ReadI32(rec, kRecOffItemWeapon)))) {
        const int weaponClass = game::WeaponClassFromTypeCode(itemWeapon->typeCode); // 1/2/3, 0 = aucune
        const int entry = static_cast<int>(itemWeapon->field196) - 1;                // field196 - 1
        CharCatalogSlot wslot = CharCatalogSlot::WeaponA;
        bool draw = (entry >= 0);
        switch (weaponClass) {
            case 1: wslot = CharCatalogSlot::WeaponA; break; // {13,16,19}
            case 2: wslot = CharCatalogSlot::WeaponB; break; // {14,17,20}
            case 3: wslot = CharCatalogSlot::WeaponC; break; // {15,18,21}
            default: draw = false; break;                    // typeCode hors [13,21] -> aucune arme (def_5282A1)
        }
        if (draw) PushCatalogPiece(models, r.pieces, wslot, race, gender, entry);
    }

    // 7. SLOTS D'EQUIPEMENT RESTANTS — NON PORTES (l'ARME v31 ci-dessus EST desormais portee).
    // TODO [0x527B8E] : le binaire poursuit avec, dans l'ordre —
    //     v33 (item +248) : flt_100EA3C / flt_102901C   @0x527BFE / @0x527C61
    //     v34 (item +264) : flt_101398C / unk_1012DBC / flt_102D39C
    //                       + effets animes unk_104BFCC/104BEAC/104C05C/104BF3C
    //                                                    @0x527C6A..0x5280A2
    //     v35 (item +280) : flt_1020FDC / flt_103B5BC   @0x52811B / @0x52817E
    //     v36 (item +296) : flt_1024FFC / flt_103F5DC   @0x5281F7 / @0x52825A
    //     v31 (item +216, l'ARME) : la PIECE d'arme est PORTEE ci-dessus (etape 6). Reste NON
    //                       PORTEE la seule TRAINE animee unk_10435FC @0x5283d5..0x528B1D.
    //     Entry (item +120) : flt_FABEBC (a5=9) si typeCode==8 ; flt_FB7BBC (a5=20) si ==29
    //                                                    @0x528B94 / @0x528C03
    //     si a4[1] == 1 : flt_FB0C5C (a5=10)             @0x528C59
    //     bonus de set (Equip_GetSetBonusId 0x548CE0 % 10 -> unk_B67C9C..unk_B681D0)
    //                                                    @0x528C71..0x528DC0
    //     si a4[1427] (+5708) > 0 : unk_B682F8..unk_B68B10 (12 cas)  @0x528DCF..0x528F53
    // Les catalogues 0x100xxxx..0x104xxxx sont HORS de la region categorie 1 (qui s'arrete
    // apres 0xFB7BBC) : ils appartiennent a d'autres categories de SObject_BuildPath
    // (5/6/7/10/11) dont les boucles ecrivains d'AssetMgr_InitAllSlots 0x4DEB50 n'ont PAS
    // ete descendues. Les 4 deja resolus (a5=8/9/10/20) sont exposes par CharCatalogSlot
    // mais leurs GARDES dependent des slots non portes -> non cables.
    // Consequence assumee : la liste affiche corps + visage + cheveux + torse + jambes + ARME,
    // sans les accessoires v33-v36 ni la traine d'arme. Aucune coordonnee ni aucun chemin inventes.

    r.valid = !r.pieces.empty();
    return r;
}

// ---------------------------------------------------------------------------
//  Rendu d'une passe
// ---------------------------------------------------------------------------
void CharPreview3D::Render(MeshRenderer& mesh, const CharPreviewResult& r,
                           const CharPreviewPose& pose, int pass) {
    // Model_Render 0x40EBB0 @0x40EBD5 : `dec eax ; cmp eax, 1 ; ja -> sortie` => seules
    // les passes 1 et 2 dessinent. On reproduit le filtre plutot que de laisser passer
    // kPassBoth (que le binaire n'emet JAMAIS ici).
    if (pass != MeshRenderer::kDrawPass_Opaque && pass != MeshRenderer::kDrawPass_Blend) return;
    if (!r.valid) return;

    // PALETTE INVALIDE => NE RIEN DESSINER (et surtout PAS en pose identite).
    // Chaine prouvee : SObject_DrawEx 0x4D9330 @0x4D93DE teste
    //   `Motion_EnsureLoaded(a7,a8) && Motion_GetData(a7,a8)` ; si l'un echoue il appelle
    //   quand meme Model_Render mais avec la palette de REPLI `dword_8E8B40` (@0x4D93AA).
    // Or `data_refs(0x8E8B40)` = 8 references, TOUTES `push offset`, ZERO ecriture : c'est
    // un bloc .bss (g_ModelMotionArray+16, dans l'en-tete de 32 o qui precede les slots
    // d'atlas 0x8E8B50) qui reste NUL. Model_Render 0x40EBB0 @0x40EBDE fait alors
    // `mov esi,[ebp+arg_14] ; cmp dword ptr [esi],0 ; jz loc_40EED2` -> SORTIE SECHE.
    // => motion absente == modele NON DESSINE. MeshRenderer::DrawModel, lui, retomberait
    // sur l'identite (cf. Gfx/PlayerPaperdoll.cpp) : ce serait un T-pose que le binaire
    // n'affiche JAMAIS. On reproduit la sortie seche.
    if (!r.palette.Valid()) return;

    const D3DXVECTOR3 position(pose.pos[0], pose.pos[1], pose.pos[2]); // &this[15720] (+0xF5A0)
    // v10 = (0.0, yaw, 0.0) — SObject_DrawEx @0x4D9359-0x4D9364 ; yaw = this[15724], en
    // DEGRES (Model_Render convertit par *0.01745329).
    const D3DXVECTOR3 rotationDeg(0.0f, pose.yawDeg, 0.0f);
    // v11 = (1.0, 1.0, 1.0) @0x4D9369-0x4D9373. ⚠ PAS 20.0 : cf. bandeau §5 (le 20.0f de
    // flt_7ED9F8 est le diametre de la sphere de frustum, pas une echelle).
    const D3DXVECTOR3 scale(kCharPreviewScale, kCharPreviewScale, kCharPreviewScale);

    // Char_RenderModel dessine TOUTES les pieces avec la MEME passe, la MEME palette (v37)
    // et la MEME transformee : l'arme et les accessoires sont SKINNES a la palette du
    // corps, jamais positionnes par un offset propre.
    for (const SkinnedModel* piece : r.pieces)
        mesh.DrawModel(*piece, position, rotationDeg, scale, r.palette, /*lod=*/0, pass);
}

} // namespace ts2::gfx

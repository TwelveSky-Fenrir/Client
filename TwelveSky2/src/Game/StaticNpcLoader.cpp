// Game/StaticNpcLoader.cpp — voir StaticNpcLoader.h pour le contexte complet (dont les 2 ecarts
// de fidelite corriges par la vague W7) et Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md pour la preuve
// de decompilation.
#include "Game/StaticNpcLoader.h"
#include "Game/MotionPools.h"
#include "Core/Log.h"
#include <cstddef> // std::size_t (indexation du pool)
#include <cstdint> // std::uint32_t (kindId)

namespace ts2::game {

namespace {
int g_currentZoneId = 0;
} // namespace

// ============================================================================================
// LoadZoneNpcs — cGameData_LoadZoneNpcInfo 0x5578E0 (ECRIVAIN UNIQUE de g_NpcRenderArray
// 0x1764D14). Decompilation d'origine (this = g_LocalPlayerSheet 0x1685748, type float*) :
//
//   for (i = 0; i < mZONENPCINFO[501 * g_SelfMorphNpcId - 501]; ++i) {            /*0x5578EA*/
//     *((_DWORD*)this + 22*i + 228723) = SkillDefTbl_GetRecord(mNPC, kindId[i]);  /*0x557946*/
//     if (*((_DWORD*)this + 22*i + 228723)) {                                     /*0x557956*/
//       *((_DWORD*)this + 22*i + 228724) = 1;                                     /*0x55796B*/
//       *(this + 22*i + 228726) = 0.0;                                            /*0x55797F*/
//       *(this + 22*i + 228727) = 0.0;                                            /*0x557995*/
//       *(this + 22*i + 228728/9/30) = pos.x/y/z;               /*0x5579C1/ED/0x557A19*/
//       *(this + 22*i + 228734) = angle;                                          /*0x557A42*/
//       *(this + 22*i + 228743) = *(this + 22*i + 228734);                        /*0x557A62*/
//     }
//   }
//
// Correspondance d'offsets (this typé float*, donc index dw) : 228723 dw = 914892 o = 0xDF5CC,
// et 0x1685748 + 0xDF5CC = 0x1764D14 == g_NpcRenderArray EXACTEMENT. Les index suivants
// (228724/6/7/8/9/30/34/43) donnent les offsets intra-slot +4/+12/+16/+20/+24/+28/+44/+80 pour
// un stride de 22 dw (88 o). C'est pourquoi ce site N'APPARAIT PAS dans les xrefs de 0x1764D14
// (adressage `this + offset immediat`) : la preuve est un calcul d'offset, pas une xref.
//
// NOTA : le binaire n'a AUCUN garde de borne (il ecrirait au-dela du 100e slot si le count le
// demandait) — structurellement impossible, mZONENPCINFO ne portant que 100 entrees par zone.
// ============================================================================================
bool LoadZoneNpcs(int zoneId1Based) {
    g_currentZoneId = 0;

    // Pool UNIQUE (ex-`g_zoneNpcs` prive + ex-`g_World.groundItems`, fusionnes par W7).
    // AUCUN clear() ici : le binaire n'efface rien dans ce chargeur (pas d'`else` sur la garde
    // @0x557956, pas de remise a zero des slots >= count). Le nettoyage appartient
    // EXCLUSIVEMENT a Pkt_EnterWorld (boucle `for i<g_NpcCount: dtor(slot)` @0x464237, dtor
    // 0x57FE70 qui ne remet QUE +4=0, preservant def/pos/angle). Sequence d'origine :
    // EnterWorld (desactive les 100 slots) -> SpawnCharacter(self) @0x4648E6 -> ce chargeur.
    std::vector<NpcRenderEntry>& pool = g_World.npcRenderEntries;

    // Le pool a une capacite FIXE, dont GameData_InitPools (== cGameData_InitPools 0x5575D0,
    // `*((_DWORD*)this + 1718) = 100` @0x5575E9 -> g_NpcCount 0x1687220) est le proprietaire
    // UNIQUE. Ce module ne le dimensionne PAS lui-meme : ce serait re-creer la duplication de
    // representation que cette vague elimine. S'il est vide, c'est un defaut de cablage amont.
    if (pool.empty()) {
        TS2_LOG("StaticNpcLoader : pool g_NpcRenderArray VIDE (GameData_InitPools 0x5575D0 pas "
                "encore appele ?) -- zone %d ignoree, aucun PNJ de decor ne sera rendu.",
                zoneId1Based);
        return false;
    }

    const int count = ZoneNpcCount(zoneId1Based);
    if (count <= 0) {
        // Zone hors bornes [1,350], table mZONENPCINFO non chargee, ou zone sans PNJ statique
        // -- pas une erreur (fidele : la boucle @0x5578EA ne fait simplement aucun tour si
        // mZONENPCINFO[zoneId].count == 0, et n'efface rien pour autant).
        g_currentZoneId = zoneId1Based;
        return true;
    }

    // Borne C++ uniquement (le binaire n'en a pas, cf. NOTA ci-dessus) : mZONENPCINFO porte
    // structurellement 100 entrees max par zone (row = 501 dw = 4 count + 100 kindId + 300 pos
    // + 100 angle = 2004 o) et le pool en a 100 -> ce clamp est un no-op en pratique. Il evite
    // un UB (ecriture hors vector) sans changer le comportement observable.
    const int maxSlots = static_cast<int>(pool.size());
    int activated = 0;
    int holes     = 0;
    for (int i = 0; i < count && i < maxSlots; ++i) {
        NpcRenderEntry& slot = pool[static_cast<std::size_t>(i)];

        // @0x557946 : slot.def = SkillDefTbl_GetRecord(mNPC, kindId) -- ecrit
        // INCONDITIONNELLEMENT, y compris a nullptr (c'est la garde suivante qui tranche).
        const std::uint32_t kindId = ZoneNpcKindId(zoneId1Based, i);
        const NpcDefRecord* record = GetNpcDefRecord(kindId);
        slot.def = record;

        if (!record) {
            // @0x557956 : garde `if (record)` SANS `else` -> le flag +4 reste TEL QUEL et le
            // slot i n'est pas active. Il n'est pas non plus supprime : l'index i reste aligne
            // sur mZONENPCINFO[i] (le binaire ne compacte JAMAIS). Cet index part sur le
            // reseau (Net_QueueRunTo(..., 4, index, ...) @0x539E78, index resolu par
            // World_PickEntityAtCursor `*a4 = j` @0x538E8F) : compacter designerait une AUTRE
            // cible au serveur (cf. ecart de fidelite #1 du .h).
            ++holes;
            continue;
        }

        slot.active   = true;  // @0x55796B — +4 = 1 (flag occupe)
        slot.mode     = 0;     // @0x55797F — +12 = 0 (mode tick : 0 -> Npc_RenderSlotTick_Loop)
        slot.frameAcc = 0.0f;  // @0x557995 — +16 = 0.0 (accumulateur de frame)
        // @0x5579C1 / @0x5579ED / @0x557A19 — +20/+24/+28 = position XYZ (mZONENPCINFO+0x194)
        ZoneNpcPosition(zoneId1Based, i, slot.x, slot.y, slot.z);
        // @0x557A42 — +44 = angle affiche (mZONENPCINFO+0x644)
        slot.angle = ZoneNpcAngle(zoneId1Based, i);
        // @0x557A62 — +80 = *(+44) : baseline relue par Npc_RenderSlotTick_Loop @0x58048E
        // (`*(this+11) = *(this+20)`) des que la distance au joueur local depasse 400.
        slot.angleBase = slot.angle;
        ++activated;
    }

    g_currentZoneId = zoneId1Based;
    TS2_LOG("StaticNpcLoader : zone %d -> %d PNJ statiques actives sur %d entrees "
            "mZONENPCINFO (%d trous : kindId sans NpcDefRecord, slots laisses inactifs a "
            "index stable)", zoneId1Based, activated, count, holes);
    return true;
}

// Accesseur mince sur le pool unique -- cf. contrat (100 slots fixes, tester `active`) dans le .h.
const std::vector<NpcRenderEntry>& ZoneNpcs() {
    return g_World.npcRenderEntries;
}

int CurrentZoneNpcZoneId() {
    return g_currentZoneId;
}

} // namespace ts2::game

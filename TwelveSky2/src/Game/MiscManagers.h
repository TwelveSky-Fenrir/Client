// Game/MiscManagers.h — regroupe 5 managers divers de la séquence App_Init 0x461C20 :
// mTRANSFER, mPOINTER, mUTIL, mMYINFO, mPLAY (dans cet ordre d'appel dans le binaire).
//
// Réécriture FIDÈLE, vérité = désassemblage de TwelveSky2.exe (imagebase 0x400000).
// Correspondance fonction d'origine -> fonction ci-dessous :
//   sub_4B43A0              0x4B43A0  -> Transfer_InitNoOp()         (mTRANSFER)
//   CursorSet_LoadResources 0x4C0FA0  -> CursorSet::LoadResources()  (mPOINTER)
//   sub_53F2B0              0x53F2B0  -> Util_InitNoOp()             (mUTIL)
//   Player_ResetAnimState   0x50F520  -> Player_ResetAnimState()     (mMYINFO)
//   cGameData_InitPools     0x5575D0  -> GameData_InitPools()        (mPLAY)
//
// Séquence exacte dans App_Init (0x461f64..0x462405), un manager après l'autre,
// chacun devant renvoyer vrai pour que l'init continue :
//   ... Net_InitPacketHandlers -> sub_4B43A0(&unk_846C08)
//       -> CursorSet_LoadResources(&unk_8E714C)
//       -> Dict001_Load / Tips002_Load / ...
//       -> sub_53F2B0(&unk_1685740)
//       -> ... Player_ResetAnimState(&g_PlayerCmdController)
//       -> cSceneMgr_Init -> ... -> cGameData_InitPools(&g_LocalPlayerSheet)
//       -> g_GameTimeSec = Terr...
//
// ---------------------------------------------------------------------------
// CE QUE FAIT RÉELLEMENT CHAQUE MANAGER (relevé exact par décompilation)
// ---------------------------------------------------------------------------
// mTRANSFER (sub_4B43A0) et mUTIL (sub_53F2B0) sont des NO-OP PURS dans ce
// build : Hex-Rays a réduit chaque fonction à `return 1;` — le paramètre
// `this` (respectivement &unk_846C08 et &unk_1685740) n'est jamais lu ni
// écrit. Ce sont des points d'extension présents dans la séquence d'init
// mais jamais implémentés dans le binaire livré (ou vidés à la compilation
// finale). Reproduits ici comme no-op documentés — PAS de logique fabriquée.
//
// mPOINTER (CursorSet_LoadResources) charge 9 curseurs Win32 EMBARQUÉS dans
// les RESSOURCES de l'exécutable (RT_GROUP_CURSOR, PAS un fichier .cur/.ani
// externe) via LoadCursorA(hInstance, MAKEINTRESOURCE(id)), id direct dans
// [0x66..0x6C] puis {0x75, 0x77}. Renvoie faux si UN SEUL LoadCursorA échoue.
//
// mMYINFO (Player_ResetAnimState) réinitialise une poignée de champs épars
// (PAS un memset intégral) d'un très gros bloc « contrôleur de commandes
// joueur » (g_PlayerCmdController, 0x1669170) : horodatage courant, 6 floats
// à 0, et un flag NaN — voir le tableau d'offsets dans le .cpp.
//
// mPLAY (cGameData_InitPools) fixe les CAPACITÉS FIXES de 6 pools d'entités
// et zéro-initialise un petit nombre de champs de chaque emplacement. C'est
// l'INITIALISATEUR D'ORIGINE des tableaux d'entités déjà modélisés (en
// std::vector dynamique) dans Game/GameState.h : les adresses absolues des
// 5 premiers pools correspondent EXACTEMENT aux globals déjà documentés là
// (dword_1687234 joueurs, dword_1764D14 objets au sol, dword_1766F74
// monstres, dword_17AB534 PNJ, dword_180EEF4 objets de zone) — vérifié par
// calcul d'adresse (voir .cpp). Le 6e pool (E, 0x17D06F4, compteur
// g_FxAuraCount) est le pool de PROJECTILES D'ATTAQUE déjà catalogué dans
// Docs/TS2_FX_CATALOG.md (Fx_SpawnAttackProjectile/Fx_HomingProjectileUpdate) ;
// intentionnellement NON dupliqué ici (voir .cpp).
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <cstdint>

namespace ts2::game {

// ===========================================================================
// mTRANSFER — sub_4B43A0 0x4B43A0.
// ===========================================================================
// No-op confirmé (le binaire ne touche jamais &unk_846C08). Toujours vrai.
inline bool Transfer_InitNoOp() { return true; }

// ===========================================================================
// mUTIL — sub_53F2B0 0x53F2B0.
// ===========================================================================
// No-op confirmé (le binaire ne touche jamais &unk_1685740). Toujours vrai.
inline bool Util_InitNoOp() { return true; }

// ===========================================================================
// mPOINTER — CursorSet_LoadResources 0x4C0FA0.
// ===========================================================================
// Bloc d'origine : global à unk_8E714C, 10 dwords = { état(=0), 9×HCURSOR }.
// Layout EXACT reproduit ci-dessous (this+0 .. this+9 dans le désassemblage).
struct CursorSet {
    // this+0 : toujours mis à 0 par le chargeur d'origine (curseur "actif" /
    // index courant — jamais relu dans LoadResources elle-même).
    int32_t state = 0;

    // this+1 .. this+9 : les 9 curseurs, dans l'ORDRE EXACT du binaire.
    // Ressources IDs = MAKEINTRESOURCE(id) sur le module .exe lui-même
    // (RT_GROUP_CURSOR intégrées dans la section .rsrc — PAS de fichier
    // .cur/.ani sur disque).
    //
    // RÔLE DE CHAQUE SLOT — établi (vague W10) par le switch 8 cas de
    // Scene_InGameRender 0x52D0B0 (`cmp var_53C, 7 / ja def_530FC7` @0x530FB4,
    // saut @0x530FC7) dont chaque cas pose une frame de curseur selon la
    // catégorie renvoyée par World_PickEntityAtCursor 0x538AB0 :
    //
    //   slot 0     (0x66) défaut — reset d'entrée de scène (Scene_EnterWorldUpdate
    //                    @0x52C044, Scene_InGameUpdate @0x52C637 : `push 0`) ET
    //                    tout hit-test UI réussi (cDrawWin_Draw : Sprite2D_HitTest
    //                    != 0 -> `push 0` @0x6299D8).
    //   slots 1,2  (0x67,0x68) paire clignotante 2 Hz « interaction » : cas 1
    //                    (joueur, +Char_DrawNameplate @0x531052), cas 4 (PNJ
    //                    g_NpcRenderArray stride 88, +Fx_MeleeSwingDrawMarker
    //                    @0x531148), cas 7 (objet de zone, +Obj_DrawNameLabel
    //                    @0x531206).  base = +1 (@0x531022 / @0x53111B / @0x5311E2)
    //   slots 3,4  (0x69,0x6A) paire clignotante 2 Hz « hostile » : cas 2 et 3
    //                    (joueurs, +Char_DrawNameplate @0x5310A5 / @0x5310F8),
    //                    cas 5 (monstre dword_1766F74 stride 280,
    //                    +Char_DrawOverheadName @0x531199).
    //                    base = +3 (@0x531075 / @0x5310C8 / @0x53116B)
    //   slots 5,6  (0x6B,0x6C) paire clignotante 2 Hz : cas 6 (objet au sol ;
    //                    aucun draw associé).  base = +5 (@0x5311B9)
    //   slot 7     (0x75) compétence LANÇABLE — cas 0 :
    //                    Skill_CanCastAtCursor(unk_1685740,…) != 0 -> `push 7`
    //                    @0x530FEA
    //   slot 8     (0x77) compétence NON lançable — cas 0, branche zéro ->
    //                    `push 8` @0x530FFA
    //
    // NB : le RÔLE ci-dessus est prouvé par le contexte d'appel ; l'APPARENCE
    // (épée/main/sablier…) ne l'est PAS — les RT_GROUP_CURSOR n'ont pas été
    // inspectés. On ne nomme donc PAS les slots d'après un dessin supposé.
    HCURSOR slot66 = nullptr; // id 0x66 (102) — this+1 — défaut / hover UI
    HCURSOR slot67 = nullptr; // id 0x67 (103) — this+2 — interaction (phase A)
    HCURSOR slot68 = nullptr; // id 0x68 (104) — this+3 — interaction (phase B)
    HCURSOR slot69 = nullptr; // id 0x69 (105) — this+4 — hostile (phase A)
    HCURSOR slot6A = nullptr; // id 0x6A (106) — this+5 — hostile (phase B)
    HCURSOR slot6B = nullptr; // id 0x6B (107) — this+6 — objet au sol (phase A)
    HCURSOR slot6C = nullptr; // id 0x6C (108) — this+7 — objet au sol (phase B)
    HCURSOR slot75 = nullptr; // id 0x75 (117) — this+8 — compétence lançable
    HCURSOR slot77 = nullptr; // id 0x77 (119) — this+9 — compétence bloquée

    // CursorSet_LoadResources 0x4C0FA0 : LoadCursorA(hInstance, id) pour les
    // 9 ressources ci-dessus, DANS CET ORDRE. Renvoie faux si un seul appel
    // échoue (HCURSOR nul) — fidèle à la boucle de contrôle du binaire.
    // NB : échouera légitimement tant que ClientSource n'embarque pas les
    // mêmes ressources RT_GROUP_CURSOR (ids 0x66..0x6C,0x75,0x77) dans son
    // propre .rc — c'est le comportement honnête, pas une régression.
    bool LoadResources(HINSTANCE hInstance);

    // ===========================================================================
    // mPOINTER (teardown) — CursorSet_DestroyAll 0x4C10B0 (App_Shutdown 0x462480,
    // étape 27/33). DestroyIcon() sur les 9 curseurs chargés par LoadResources(),
    // puis remise à zéro (state + les 9 slots) — fidèle à l'original, y compris
    // le fait qu'un curseur obtenu par LoadCursorA (ressource PARTAGÉE) est quand
    // même passé à DestroyIcon : comportement du binaire reproduit tel quel, sans
    // « correction » (DestroyIcon sur un HCURSOR partagé est habituellement un
    // no-op silencieux côté Win32, pas un crash).
    // ===========================================================================
    void DestroyAll();

    // ===========================================================================
    // mPOINTER (tick boucle msg) — Cursor_AnimateTick 0x4C1140, SEUL appelant :
    // WinMain 0x4609C0 @0x46163b (`mov ecx, offset dword_8E714C ; call
    // Cursor_AnimateTick`). PAS une animation par sprite/timer : `state` est
    // l'index [0..8] du curseur "voulu", écrit ailleurs (~157 sites, TOUT le
    // rendu UI/scènes) via Util_SetClampedU8Field 0x4C1110 sur un hit-test
    // souris (ex. cDrawWin_Draw 0x629960 : hover -> Util_SetClampedU8Field(
    // &unk_8E714C, 0)). AnimateTick se contente de réappliquer SetCursor(
    // slot[state]) à CHAQUE tour de la boucle de messages : la fenêtre n'a
    // PAS de WNDCLASSEXA.hCursor enregistré, donc Windows réinitialise le
    // curseur au curseur par défaut à chaque WM_SETCURSOR/déplacement souris
    // au-dessus du client — le jeu réaffirme donc son curseur "voulu" à
    // chaque itération plutôt qu'une seule fois au clic/hover.
    // Décompilation d'origine (this = &unk_8E714C, tableau de 10 dwords) :
    //   return SetCursor(*(this + *this + 1));   // this[ state + 1 ]
    // SetActiveSlot() ci-dessous est l'équivalent client de
    // Util_SetClampedU8Field(&unk_8E714C, idx) 0x4C1110.
    //
    // CIBLE UNIQUE — prouvé par COMPTAGE (vague W10), pas par échantillon :
    // xrefs_to(0x8E714C) = 161 refs data = les 157 sites Util_SetClampedU8Field
    // + exactement 4 autres (WinMain @0x461636 AnimateTick, App_Init @0x461F8B
    // LoadResources, App_Shutdown @0x462587 DestroyAll, CrtInit_CursorSetThunk
    // @0x7A51F3). 157 + 4 = 161 => CHAQUE site du setter vise bien ce global,
    // sans exception.
    // ===========================================================================
    HCURSOR AnimateTick() const;

    // Équivalent de Util_SetClampedU8Field(&unk_8E714C, idx) 0x4C1110 : fixe le
    // slot actif [0..8], sans effet si hors bornes (fidèle : l'original ne
    // touche pas *this quand a2 > 8). Retourne vrai si la valeur a été acceptée.
    bool SetActiveSlot(uint32_t idx);
};

// ===========================================================================
// Slots de curseur nommés — constantes prouvées (cf. tableau de rôles ci-dessus).
// À passer à CursorSet::SetActiveSlot(). Les trois bases « paire clignotante »
// s'emploient via CursorBlinkSlot() (2 Hz).
// ===========================================================================
constexpr uint32_t kCursorDefault      = 0; // @0x52C044 / @0x52C637 / @0x6299D8
constexpr uint32_t kCursorInteractBase = 1; // @0x531022 / @0x53111B / @0x5311E2
constexpr uint32_t kCursorHostileBase  = 3; // @0x531075 / @0x5310C8 / @0x53116B
constexpr uint32_t kCursorPickupBase   = 5; // @0x5311B9
constexpr uint32_t kCursorCastOk       = 7; // @0x530FEA (Skill_CanCastAtCursor != 0)
constexpr uint32_t kCursorCastBlocked  = 8; // @0x530FF8

// ===========================================================================
// Clignotement 2 Hz des paires {1,2} / {3,4} / {5,6} — transcription EXACTE du
// motif répété aux 7 cas de Scene_InGameRender 0x52D0B0 (ancre : cas 1
// @0x531009..0x531022) :
//     fld ds:g_GameTimeSec        // temps de jeu
//     fadd st, st                 // x + x  (PAS une multiplication par 2.0)
//     call Crt_ftol               // troncature vers zéro -> int
//     and  eax, 80000001h         //  ┐
//     jns  short L                //  │ idiome MSVC de MODULO SIGNÉ % 2
//     dec  eax                    //  │ (et NON un simple `& 1` : le fixup de
//     or   eax, 0FFFFFFFEh        //  │  signe rend -3 % 2 == -1)
//     inc  eax                    //  ┘
//   L: add  eax, <base>           // 1, 3 ou 5 selon la catégorie
//
// Le résultat est passé tel quel à Util_SetClampedU8Field (paramètre NON
// signé) : un `base` + résultat négatif deviendrait un unsigned énorme, donc
// rejeté par le clamp `a2 <= 8` — comportement déjà reproduit par
// SetActiveSlot(uint32_t). On garde donc le type signé ici pour rester fidèle
// (en pratique g_GameTimeSec >= 0, le résultat vaut base ou base+1).
inline int CursorBlinkSlot(int base, float gameTimeSec) {
    return static_cast<int>(gameTimeSec + gameTimeSec) % 2 + base;
}

// ===========================================================================
// Cursors() — instance UNIQUE du jeu de curseurs, miroir de dword_8E714C
// (0x8E714C), qui est un GLOBAL dans le binaire et non un membre d'objet.
// ===========================================================================
// Les 161 références à 0x8E714C (cf. « CIBLE UNIQUE » ci-dessus) proviennent de
// WinMain, App_Init, App_Shutdown ET de tout le rendu UI/scènes : aucun de ces
// sites ne possède l'objet, tous adressent le même global. Exposer l'instance
// ici est donc la transcription FIDÈLE — pas un contournement d'encapsulation.
//
// ⚠️ ATTENTION (câblage indissociable, cf. rapport W10) : tant qu'App conserve
// son membre privé `cursors_` (App/App.h:43), il existerait DEUX CursorSet —
// les scènes/UI écriraient dans ce singleton pendant qu'App ticke le membre,
// et le curseur resterait figé alors même que le code PARAÎT complet.
// App.h:43 / App.cpp:320/406/741 doivent basculer sur Cursors() DANS LE MÊME
// changement (fichiers non possédés par W10 -> wiringTodoForOrchestrator).
CursorSet& Cursors();

// ===========================================================================
// mMYINFO — Player_ResetAnimState 0x50F520.
// ===========================================================================
// Opère sur g_PlayerCmdController (0x1669170 dans le binaire), un très gros
// bloc non encore porté dans ClientSource. Reproduit ici comme fonction sur
// pointeur brut (float*), fidèle offset par offset ; à brancher sur le futur
// struct « contrôleur de commandes joueur » une fois celui-ci modélisé.
// `gameTimeSec` = valeur courante de g_GameTimeSec (0x815180) à l'appel.
void Player_ResetAnimState(float* playerCmdController, float gameTimeSec);

// ===========================================================================
// mPLAY — cGameData_InitPools 0x5575D0.
// ===========================================================================
// Fixe les capacités des pools d'entités (déjà modélisés en std::vector dans
// Game/GameState.h : g_World.players/monsters/npcs/groundItems) à leurs
// tailles fixes d'origine, et les pré-remplit d'emplacements par défaut
// (équivalent des petits constructeurs sub_55D6F0/57FE50/580530/583370
// appelés en boucle par le binaire). Toujours vrai (fidèle : le binaire ne
// peut pas échouer ici — pas d'allocation dynamique testée).
bool GameData_InitPools();

// ===========================================================================
// mPLAY (teardown) — cGameData_DestroyPools 0x557780 (App_Shutdown 0x462480,
// étape 1/33 — TOUT PREMIER appel, image miroir de GameData_InitPools qui est
// le TOUT DERNIER manager d'App_Init).
// ===========================================================================
// L'original parcourt CHAQUE pool actif (bornes = compteurs this+1717..1721 +
// g_ZoneObjectCount) et appelle un petit destructeur par emplacement
// (Fx_AttachSlotClear / maybe_cGameData_NpcListItemDtor / Char_RespawnAfterKnockback /
// maybe_cGameData_ListField1ItemDtor / PlayerArray_SlotDestruct /
// maybe_cGameData_ZoneObjListItemDtor) — même schéma en 1-4 champs par slot
// que les petits constructeurs de GameData_InitPools (pas un teardown profond,
// pas de libération mémoire : les pools sont des tableaux FIXES en .bss dans
// le binaire d'origine, jamais réellement "libérés").
// Ici, les pools sont des std::vector dynamiques (Game/GameState.h) : l'effet
// net équivalent (capacité rendue à zéro / slots réinitialisés) est obtenu en
// vidant les 5 vecteurs modélisés (mêmes 5 pools que GameData_InitPools ;
// pool E "projectiles" 0x17D06F4/g_FxAuraCount non modélisé ici non plus, cf.
// commentaire de GameData_InitPools).
bool GameData_DestroyPools();

} // namespace ts2::game

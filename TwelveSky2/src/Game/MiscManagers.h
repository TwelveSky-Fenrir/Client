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
    // .cur/.ani sur disque). Rôle applicatif de chaque slot NON déterminé
    // avec certitude (>100 sites d'appel dans tout le rendu UI/HUD/scènes ;
    // hors périmètre de ce manager d'INIT) — slots nommés génériquement.
    HCURSOR slot66 = nullptr; // id 0x66 (102) — this+1
    HCURSOR slot67 = nullptr; // id 0x67 (103) — this+2
    HCURSOR slot68 = nullptr; // id 0x68 (104) — this+3
    HCURSOR slot69 = nullptr; // id 0x69 (105) — this+4
    HCURSOR slot6A = nullptr; // id 0x6A (106) — this+5
    HCURSOR slot6B = nullptr; // id 0x6B (107) — this+6
    HCURSOR slot6C = nullptr; // id 0x6C (108) — this+7
    HCURSOR slot75 = nullptr; // id 0x75 (117) — this+8
    HCURSOR slot77 = nullptr; // id 0x77 (119) — this+9

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
    // Util_SetClampedU8Field(&unk_8E714C, idx) : NON câblée à un site d'appel
    // UI pour l'instant (les ~157 sites d'origine sont dans du code UI/scènes
    // pas encore porté) — fournie pour que de futurs portages UI puissent
    // piloter le curseur actif sans dupliquer la logique de clamp.
    // ===========================================================================
    HCURSOR AnimateTick() const;

    // Équivalent de Util_SetClampedU8Field(&unk_8E714C, idx) 0x4C1110 : fixe le
    // slot actif [0..8], sans effet si hors bornes (fidèle : l'original ne
    // touche pas *this quand a2 > 8). Retourne vrai si la valeur a été acceptée.
    bool SetActiveSlot(uint32_t idx);
};

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

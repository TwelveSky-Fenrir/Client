// Game/NameplateLogic.h — Plaques de nom (« nameplates ») de TwelveSky2 (ts2::game).
//
// Réécriture C++ fidèle (traduction réelle du désassemblage, pas d'invention) de la LOGIQUE
// (texte affiché, couleur, conditions de visibilité) des plaques de nom dessinées au-dessus
// des personnages. AUCUN rendu D3D réel ici (pas de sprites, pas de projection écran, pas de
// police) : ce module ne fait que calculer une décision — la couche Gfx (hors périmètre de
// cette mission) consomme `NameplateInfo` pour dessiner.
//
// Fonctions d'origine traduites (EA -> fonction) :
//   Char_DrawNameplate       0x56EF40 -> ComputeNameplateInfo() (logique extraite ; le rendu
//                                        D3D/sprite proprement dit est laissé en // TODO précis)
//   Npc_GetNameplateColor    0x540790 -> DÉJÀ implémentée dans Game/NpcInteraction.h/.cpp
//                                        (Npc_GetNameplateColor) — RE-VÉRIFIÉE lors de cette
//                                        mission, CONFORME au désassemblage actuel à 100 %
//                                        (comparaison exhaustive branche par branche faite le
//                                        2026-07-14, cf. note de conformité en bas de fichier).
//                                        Non dupliquée ici : ce fichier réutilise/appelle
//                                        celle de NpcInteraction.h pour tout code de couleur
//                                        « faction/quête » (branches faction/élément/PK de
//                                        Npc_GetNameplateColor), Char_DrawNameplate ayant SA
//                                        PROPRE logique de couleur distincte (PK/guilde/GM/
//                                        marché) traduite ici.
// Callees indispensables à la fidélité, traduits en interne (pas de dépendance externe) :
//   (aucun calcul lourd — uniquement composition de texte/ratios ; Crt_ftol -> troncature vers
//   zéro, cf. convention établie dans Game/StatFormulas.h)
//
// ---------------------------------------------------------------------------------------
// PROVENANCE DES CHAMPS (important) : Char_DrawNameplate lit ~35 champs sur `this` (pointeur
// d'entité, tableau g_EntityArray 0x1687234, stride 908 — cf. Game/GameState.h::PlayerEntity,
// dont le layout ACTUEL (body[600] opaque) ne nomme pas ces champs individuellement, À UNE
// EXCEPTION PRÈS : `name` (this+72 = body+48) EST décodé côté GameState/EntityManager depuis
// la mission NAMEPLATES du 2026-07-14 — cf. GameState.h::PlayerEntity::name et
// Game/EntityManager.cpp::ReadPlayerName — et câblé par WorldRenderer::Render() dans
// `actor.name` au lieu du placeholder "Player#i" qu'il utilisait avant cette mission). Comme
// pour NpcInteractionExt dans Game/NpcInteraction.h, on introduit ici une vue `NameplateActor`
// avec CHAQUE champ annoté de son offset d'origine (en octets, `this+N`), à peupler par
// l'appelant depuis le (futur) portage complet de l'entité joueur. Aucune invention de valeur :
// chaque champ ci-dessous correspond à une lecture explicite du désassemblage de 0x56EF40.
//
// SÉMANTIQUE INCERTAINE (documentée honnêtement, PAS de simplification silencieuse) :
//  - `this+40` (affiliationName) et `this+472` (allianceName) sont comparés par égalité de
//    chaîne à des globals distincts selon le contexte (byte_1686138 en mode « marché » morph
//    291, unk_16746A8 en branche normale, g_AllianceRosterNames pour l'alliance). RE-VÉRIFIÉ
//    (mission « déblocage 425-428 », 2026-07-14) : byte_1686138 N'EST PAS écrit par un
//    « handler réseau différent » — son unique site d'écriture dans tout le binaire est
//    Net_OnWorldEntityDispatch lui-même (case 422, `Crt_StringInit(&byte_1686138,
//    &byte_1686145)` = copie depuis le buffer jumeau byte_1686145), et byte_1686145 n'est
//    JAMAIS alimenté avec un contenu réel nulle part dans l'image (confirmé exhaustivement
//    par xrefs_to sur les deux symboles — cf. commentaire détaillé au site d'implémentation
//    dans Net/WorldEntityDispatch.cpp, sous-cas 425..428). unk_16746A8 (Net_OnTeamFormationDispatch)
//    et g_AllianceRosterNames (Net_OnGuildRosterReset/Join/Leave) restent, eux, bien écrits par
//    leurs handlers respectifs — seul byte_1686138/byte_1686145 est mort côté client d'origine.
//    Le rôle exact (guilde vs équipe vs alliance) n'est PAS confirmé avec certitude — exposé via 2 prédicats
//    `NameplateHost::IsSameAffiliationAsLocal` (sentinel selon le morph actif) et
//    `IsSameAllianceAsLocal`, à brancher par l'appelant sur les vraies structures sociales
//    (Game/SocialSystem.h/GuildSystem.h) une fois leur sémantique confirmée par un futur RE.
//  - `g_SelfMorphNpcId` (0x1675A98, déjà documenté ailleurs — Game/SkillCombat.h,
//    Game/MapWarp.h — comme « id NPC/ville/posture courant du joueur local ») pilote ICI un
//    mode d'affichage de « marché »/zone spéciale (valeurs 270..274, 291, 324, 342) où les
//    plaques de nom de TOUS les personnages changent de présentation (texte + palette de
//    couleur différents). Confirmé par recoupement avec Combat_CanTargetOnMap 0x558740 (même
//    liste de valeurs spéciales, mode PVP par zone via Map_GetPvpMode).
//  - Paramètres `a2`/`a3` de Char_DrawNameplate (a4 confirmé JAMAIS lu dans le corps de la
//    fonction — supprimé de l'API) : repris ici sous les noms `drawMode` (a2) et `notSelf`
//    (a3). Voir le §DRAWMODE ci-dessous — la rédaction précédente (« a2=1 pour la passe
//    principale ») décrivait un chemin MORT.
//
// =======================================================================================
// §DRAWMODE — QUEL CHEMIN D'APPEL EXISTE RÉELLEMENT (Passe 4 / vague W9, front
// nameplate-entity). LIRE AVANT DE TOUCHER À CE FICHIER.
// =======================================================================================
// `xrefs_to(Char_DrawNameplate 0x56EF40)` = EXACTEMENT 4 sites, TOUS dans
// Scene_InGameRender 0x52D0B0 :
//
//   @0x52FC02  a2=1, a3=i  — boucle `for (i; i < g_EntityCount; ++i)` sur g_EntityArray.
//                            ***CHEMIN MORT***, INATTEIGNABLE : la boucle est gardée par
//                            `cmp ds:dword_1668F64, 1 / jz / cmp ds:dword_1668F64, 2 / jnz`
//                            (@0x52FB8E..@0x52FC09) et dword_1668F64 N'EST JAMAIS ÉCRIT.
//   @0x531052  a2=2, a3=idx — catégorie 1 du switch de survol (joueur neutre)
//   @0x5310A5  a2=2, a3=idx — catégorie 2 (partenaire d'échange)
//   @0x5310F8  a2=2, a3=idx — catégorie 3 (joueur attaquable)
//
// PREUVE que dword_1668F64 n'est jamais écrit (re-prouvée cette vague par DEUX voies
// indépendantes, au niveau octet, sur l'image ENTIÈRE) :
//   · find_bytes('64 8F 66 01', min_ea..max_ea) => 4 occurrences : 0x52FB90, 0x52FB99,
//     0x52FC0B, 0x570088 — les 4 sont les OPÉRANDES des `cmp` ci-dessus. Aucun `mov`.
//   · xrefs_to(0x1668F64) => 4 xrefs, toutes de type LECTURE (0x52FB8E, 0x52FB97,
//     0x52FC09, 0x570086) ; définition `dword_1668F64 dd 0`.
// => dword_1668F64 vaut 0 en permanence. `drawMode == 1` NE SE PRODUIT JAMAIS.
//
// CONSÉQUENCES DIRECTES, toutes vérifiées sur le désassemblage — les blocs suivants de
// ComputeNameplateInfo() sont FIDÈLEMENT INERTES et doivent le RESTER :
//   · Barres PV/PM         : gardées par `a2 == 1 && !a3 && g_Opt_ShowNameplates == 1`
//                            (@0x56EFBF) -> mortes. (Cf. NameplateBar::atlasFrame : la
//                            formule d'atlas est modélisée pour la fidélité, PAS pour être
//                            rendue.)
//   · Garde @0x56F679      : `a2 != 1 || g_Opt_ShowHitMarkers && (g_GmAuthLevel >= 1 ||
//                            Math_Dist3D(...) <= 300.0)` -> avec a2=2 elle passe TOUJOURS
//                            par court-circuit. `optShowHitMarkers`, `localGmAuthLevel` et
//                            `selfX/Y/Z` n'ont donc AUCUN effet observable sur le chemin
//                            vivant ; ils sont peuplés par fidélité, pas par nécessité.
//                            (Le seul effet VIVANT de g_Opt_ShowHitMarkers dans ce domaine
//                            est le 5e argument de World_PickEntityAtCursor 0x538AB0
//                            @0x530F54 — cf. World/TerrainPicker.h.)
//   · Bloc « détaillé »    : `if (dword_1668F64 == 1)` @0x57008D -> mort. Il contient la
//                            ligne guilde/titre, le chuchoté, les 6 icônes de statut
//                            (@0x570448..@0x5706C7) et l'overlay debug GM. D'où
//                            `NameplateViewerContext::optDetailedNameplates` qui DOIT rester
//                            false : le peupler afficherait ce que le binaire n'affiche
//                            jamais.
//
// Autrement dit : le client d'origine ne dessine de plaque de nom QUE sur l'UNIQUE entité
// sous le curseur, et JAMAIS sur le joueur local (la boucle de picking de
// World_PickEntityAtCursor 0x538AB0 démarre à `i = 1` @0x538ACB — l'index 0 est exclu).
// Le site d'appel côté ClientSource est Scene/WorldRenderer.cpp::drawNameplatePass().
//
// RÈGLE : ce fichier n'édite AUCUN header existant. Inclut Game/GameState.h (SelfState,
// GameDatabases, g_World), Game/GameDatabase.h (transitif) et Game/StatFormulas.h
// (CalcEvasion, réutilisé tel quel pour la ligne de debug GM "CRKDEF::[n]").
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <cmath>

#include "Game/GameState.h"
#include "Game/StatFormulas.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Codes couleur fidèles (littéraux entiers passés à UI_DrawNumberValue 0x53FCC0 dans le
// binaire — palette exacte définie côté assets UI, hors périmètre de cette mission). Les noms
// ci-dessous décrivent le RÔLE observé au site d'appel, pas une palette RVB.
// ---------------------------------------------------------------------------
constexpr int kNameColorNeutral           = 1;  // nom « normal » (pas d'affiliation connue, pas GM)
constexpr int kNameColorHostileHidden     = 2;  // cible ennemie ciblable -> nom réel masqué (élément/marché)
constexpr int kNameColorAdminPrimary      = 3;  // (+122)==1 && (+124)==1 : variante admin A ; AUSSI overlay debug GM
constexpr int kNameColorAdminSecondary    = 4;  // (+122)==1 && (+124)!=1 : variante admin B
constexpr int kNameColorVipOrMarketCase0  = 5;  // mot (+272)==1 (VIP/surlignage) ; palette marché 324/342 cas 0
constexpr int kNameColorWhisper           = 7;  // ligne « chuchoté » ((+125)==1), littéral fixe
constexpr int kNameColorAffiliate         = 9;  // affiliation ((+40)) == celle du joueur local
constexpr int kNameColorMarketGroupA_Std  = 13; // groupe marché 270..274, (+59) != 1
constexpr int kNameColorMarketCase2       = 24; // palette marché 324/342 cas 2
constexpr int kNameColorAllianceAffiliate = 29; // alliance ((+472)) == celle du joueur local
constexpr int kNameColorMarketGroupA_Alt  = 32; // groupe marché 270..274, (+59) == 1
constexpr int kNameColorMarketCase1       = 33; // palette marché 324/342 cas 1
constexpr int kNameColorGmAccount         = 35; // (+7)==1 (compte GM) — priorité la plus haute (dernier test)
constexpr int kNameColorMarketCase3       = 44; // palette marché 324/342 cas 3 ; AUSSI (+61)==12 (statut spécial)
constexpr int kNameColorGmDebugLine       = 3;  // overlay debug (LEVEL/RANK/CRKDEF), littéral fixe

// ---------------------------------------------------------------------------
// Vue « champs lus par Char_DrawNameplate sur l'entité dessinée » (this+N, cf. bandeau de
// tête). Parallèle à g_World.players[i]/g_World.Self() (même pattern que NpcInteractionExt) :
// tant que le portage complet de l'entité joueur (908 o) n'existe pas, l'appelant peuple cette
// vue avec les valeurs réelles au moment de dessiner.
// ---------------------------------------------------------------------------
struct NameplateActor {
    // --- portes de visibilité ---
    bool  active       = false; // +0    : entité active/vivante
    bool  hasIdentity  = false; // +24   : identité résolue (nom reçu du serveur)

    // --- position monde (utilisée pour la position d'ancrage écran ; PAS de calcul écran ici) ---
    float x = 0.0f, y = 0.0f, z = 0.0f; // +252/+256/+260

    // --- identité / texte ---
    std::string name;               // +72  : nom du personnage (texte de base)
    std::string affiliationName;    // +40  : nom d'affiliation (guilde ou équipe selon contexte, cf. bandeau)
    std::string subAffiliationName; // +60  : sous-texte (titre secondaire, utilisé si titleKind==2)
    std::string allianceName;       // +472 : nom d'alliance
    std::string whisperName;        // +504 : texte affiché en ligne « chuchoté » ((+125)==1)
    std::string altWhisperName;     // +756 : texte alternatif ((+187)!=0 && (+125)==0)

    // --- valeurs numériques ---
    int level          = 0;   // +32 (dupliqué à +112 pour l'overlay debug, additionné à +108)
    int levelBonus      = 0;   // +108 : bonus de niveau (overlay debug "LEVEL::[level+levelBonus]")
    int rankTierValue   = 0;   // +568 : valeur brute pour le préfixe de « palier » (UI_GetValueTierString)
    int titleKind        = 0;   // +56  : 0 = sans titre, 1 = titre simple, 2 = titre + sous-texte
    int element           = 0;   // +88  : élément/faction du personnage (0..3)
    int pkLevel            = 0;   // +236 : rang PK/statut de « crime » (0..3+)
    int enchantRaw          = 0;   // +224 : valeur brute d'enchantement d'arme (grade = enchantRaw/100)

    // --- drapeaux ---
    bool hasTitleBarExtraHeight   = false; // +220 : influence l'offset Y du libellé (rendu, TODO Gfx)
    bool suppressExtraNameHeight  = false; // +576 : idem
    // ⚠️ NOMS SUSPECTS — « admin » n'est PAS prouvé ; indice fort du contraire (W9).
    // World_PickEntityAtCursor 0x538AB0 lit CES MÊMES OFFSETS pour détecter le PARTENAIRE
    // D'ÉCHANGE (catégorie de clic 2, @0x538BCC) :
    //     g_TradePartnerIdLo[0] == 1 && g_TradePartnerIdLo[227*i] == 1
    //  && dword_1687420[0]     == dword_1687420[227*i]
    //  && dword_1687424[0]     != dword_1687424[227*i]
    // avec g_TradePartnerIdLo = 0x168741C = entity+488, dword_1687420 = entity+492,
    // dword_1687424 = entity+496 (base g_EntityArray 0x1687234). Le portage du picker
    // utilise déjà ces 3 offsets sous les noms kBodyTradeFlag/kBodyTradeA/kBodyTradeB
    // (World/TerrainPicker.cpp:118-120). La branche « admin » de Char_DrawNameplate
    // (`(+122)==1` -> couleur 3 ou 4 selon `(+124)==1`, texte `nom + Str(72)`) est donc
    // très probablement l'affichage « EN COURS D'ÉCHANGE », pas un titre d'administrateur.
    // NON RENOMMÉS ici (règle « ne jamais deviner ») : le rôle exact de +492/+496 (idHi/idLo
    // du partenaire ? compteur de session ?) n'est pas prouvé, et Str(72) n'est pas résolu
    // (table de langue non déchiffrée). À trancher par un futur RE de l'écran d'échange.
    bool isAdminTitle             = false; // +488 : (+122)==1  — == g_TradePartnerIdLo (cf. ci-dessus)
    bool isAdminTitleAlt          = false; // +496 : (+124)==1  — == dword_1687424 (n'a de sens que si isAdminTitle)
    bool isWhisperTarget          = false; // +500 : (+125)==1
    bool hasAltWhisperColor       = false; // +748 : (+187)!=0
    bool isVipOrHighlighted       = false; // +544 (mot) : (+272)==1
    bool isGmAccount              = false; // +28  : (+7)==1
    bool isSpecialPkState         = false; // +244 : (+61)==12

    // --- PV / PM (barres au-dessus de la tête, mode « cible sélectionnée » uniquement) ---
    int hpCur = 0, hpMax = 0; // +316 / +312
    int mpCur = 0, mpMax = 0; // +324 / +320

    // --- icônes de statut (0 = aucune, sinon « actif » ; ordre fidèle, sprite réel = TODO Gfx) ---
    int statusIconA = 0; // +392 (unk_95027C)
    int statusIconB = 0; // +404 (unk_96135C)
    int statusIconC = 0; // +408 (unk_9613F0)
    int statusIconD = 0; // +428 (unk_961484)
    int statusIconE = 0; // +436 (unk_961518)
    int statusIconF = 0; // +440 (réutilise le sprite unk_961484)
};

// ---------------------------------------------------------------------------
// Contexte « joueur local + options » nécessaire à Char_DrawNameplate (globals hors `this`).
// ---------------------------------------------------------------------------
struct NameplateViewerContext {
    // NB : g_LocalElement 0x1673194 n'est PAS lu directement par Char_DrawNameplate (vérifié
    // sur les `refs` du désassemblage — absent) ; il n'apparaît qu'indirectement via
    // Combat_CanTargetOnMap (cf. NameplateHost::CanTargetOnMap, à charge de l'implémentation
    // hôte de le capturer si besoin). Pas de champ ici pour éviter une fausse dépendance.
    int localMorphNpcId    = 0; // g_SelfMorphNpcId 0x1675A98 (pilote le mode « marché »/zone spéciale)
    int localGmAuthLevel   = 0; // g_GmAuthLevel 0x1669294
    int localFactionSubMode = 0; // dword_16760F4 (sous-mode d'affichage quand morph==324)
    int localFactionFlag    = 0; // dword_1687320[0] (même champ que NpcQuestContext::factionFlag)

    float selfX = 0.0f, selfY = 0.0f, selfZ = 0.0f; // g_World.Self() position (Math_Dist3D avec flt_1687330)

    bool  optShowNameplates    = false; // g_Opt_ShowNameplates 0x84DED4 (option UI "showNameplates")
    bool  optShowHitMarkers    = false; // g_Opt_ShowHitMarkers 0x84DED0 (option UI "showHitMarkers")
    bool  optDetailedNameplates = false; // dword_1668F64==1 (bloc titre/affiliation/icônes/debug)

    int   chatColorWhisper      = 0; // g_ChatColor_Whisper 0x84DFDC (couleur littérale de altWhisperName)
    int   partyFlagIndicatorCount = 0; // dword_1675804 (compteur ; icône supplémentaire si >0 && notSelf)
};

// Une ligne de texte + couleur (nom principal, titre/affiliation, chuchoté, debug GM…).
struct NameplateLine {
    std::string text;
    int color = kNameColorNeutral;
};

// Barre PV/PM au-dessus de la tête — Char_DrawNameplate @0x56F029..@0x56F10A.
//
// ⚠️ MORTE DANS LE BINAIRE : ce bloc est gardé par `a2 == 1 && !a3 && g_Opt_ShowNameplates
// == 1` (@0x56EFBF) et `a2 == 1` ne se produit jamais (cf. §DRAWMODE en tête de fichier).
// `atlasFrame` est modélisé pour la FIDÉLITÉ DE LA FORMULE (documenter exactement ce que
// le binaire calculerait), PAS pour être rendu : aucun appelant ne doit dessiner ces
// barres tant que dword_1668F64 reste non écrit — ce serait ajouter à l'écran ce que le
// client d'origine n'affiche jamais.
//
// Formule EXACTE (transcrite instruction par instruction, W9) :
//   PV @0x56F029 : `if (*((int*)this + 79) <= 0) v120 = 1375;`                (frame « vide »)
//                  sinon `Crt_ftol((double)*((int*)this+79) * 48.0 / (double)*((int*)this+78))`
//                  puis `v120 = v4 + 1376; if (v120 > 1424) v120 = 1424;`     @0x56F049..@0x56F05F
//                  dessin `Sprite2D_Draw(&g_AssetMgr_UiAtlasSlots + 148*v120, x - 44, y)` @0x56F08F
//   PM @0x56F0A1 : vide = 1425 ; sinon `ftol(*(this+81) * 48.0 / *(this+80)) + 1426`,
//                  clamp 1474 ; dessin à `(x - 44, y + 9)`                     @0x56F10A
// -> 49 frames par barre (PV 1376..1424, PM 1426..1474), ratio TRONQUÉ VERS ZÉRO
// (Crt_ftol 0x760810, cf. Ftol() dans le .cpp). Stride d'atlas 148 (0x94), cohérent avec
// `imul ecx, 94h` @0x530F43. Offsets : hpCur=idx79 (+316), hpMax=idx78 (+312),
// mpCur=idx81 (+324), mpMax=idx80 (+320) — conformes à NameplateActor ci-dessus.
struct NameplateBar {
    bool visible = false;
    int current = 0;
    int max = 0;
    // Index de frame dans l'atlas UI (g_AssetMgr_UiAtlasSlots 0x8E8B50, stride 148 o).
    // Défaut 1375 = frame « barre PV vide » (le défaut de la barre PM serait 1425 ; la
    // valeur est écrasée par ComputeNameplateInfo dès que la barre est calculée).
    int atlasFrame = 1375;
};

// Bornes d'atlas des barres PV/PM (littéraux du binaire, cf. bandeau de NameplateBar).
constexpr int kHpBarFrameEmpty = 1375; // @0x56F029
constexpr int kHpBarFrameBase  = 1376; // @0x56F049
constexpr int kHpBarFrameMax   = 1424; // @0x56F05F (clamp)
constexpr int kMpBarFrameEmpty = 1425; // @0x56F0A1
constexpr int kMpBarFrameBase  = 1426;
constexpr int kMpBarFrameMax   = 1474; // clamp
constexpr double kBarFrameSpan = 48.0; // `* 48.0` des deux ratios

// Identité d'icône de statut (ordre fidèle au binaire ; sprite réel = TODO Gfx). `A`..`E`/`F`
// correspondent aux champs statusIconA..F de NameplateActor ; `PartyFlag` = condition
// `!notSelf && partyFlagIndicatorCount>0` (pas un champ direct de l'entité).
enum class NameplateIconSlot { A, B, C, D, E, PartyFlag, F };

// Décision complète pour une plaque de nom — consommée par la couche Gfx (hors périmètre).
struct NameplateInfo {
    bool visible = false; // porte globale (actif + identité + hors zone de clic)

    // Offset Y (monde) du point d'ancrage du libellé de nom, fidèle à la sélection 31.0/21.0
    // de l'original (position UNIQUEMENT — pas de projection écran ici, cf. TODO Gfx).
    float labelAnchorYOffset = 21.0f;

    NameplateBar hpBar; // visible seulement si drawMode==1 && !notSelf && option activée
    NameplateBar mpBar;

    bool nameBlockVisible = false; // false si hors du gate a2!=1||(hitmarkers&&(gm||<=300))
    NameplateLine mainLine;        // nom principal (préfixe de palier + nom réel OU nom d'élément masqué)

    // Icône de variante (0/1) utilisée uniquement par le groupe marché 270..274 pour choisir
    // entre 2 sprites (unk_8E9DD0/unk_8E9E64) — donnée de sélection, PAS le sprite lui-même.
    int nameIconVariant = 0;

    // Lignes/données du bloc « détaillé » (dword_1668F64==1) : titre/affiliation, chuchoté,
    // overlay debug GM. Ordre fidèle à l'original.
    std::vector<NameplateLine> extraLines;

    // Icônes de statut ACTIVES uniquement, dans l'ordre fidèle au binaire (sprite réel = TODO Gfx).
    std::vector<NameplateIconSlot> statusIcons;
};

// ---------------------------------------------------------------------------
// Dépendances externes non modélisées dans les headers partagés de cette mission (résolution
// de chaînes localisées StrTable005, click-marker souris, identité sociale). Callbacks
// optionnels ; comportement par défaut documenté au site d'appel — AUCUN de ces callbacks
// n'effectue de rendu D3D.
// ---------------------------------------------------------------------------
struct NameplateHost {
    // Target_IsBeyondClickRange 0x5410D0 — ⚠️ NOM D'ORIGINE TROMPEUR : ce n'est PAS une
    // distance à un « point de dernier clic souris ». C'est le NEAR-CULL CAMÉRA, identique
    // à celui de Char_Draw. Décompilation intégrale (re-vérifiée Passe 4 / W9) :
    //   BOOL __stdcall Target_IsBeyondClickRange(float *a1, float a2)
    //   { v5 = (flt_800138 - a1[2])*(flt_800138 - a1[2])
    //        + (flt_800134 - (a2*0.5 + a1[1]))*(flt_800134 - (a2*0.5 + a1[1]))
    //        + (g_CameraPos - *a1)*(g_CameraPos - *a1);
    //     return Math_Sqrt_0(v5) >= 10.0; }                       /*0x54113B..0x54116B*/
    // Le triplet de référence est (g_CameraPos 0x800130, flt_800134, flt_800138) = la
    // POSITION CAMÉRA — MÊME référence que DrawCullContext::cameraPos
    // (Game/EntityDrawLogic.h:151). SOURCE DE L'ANCIENNE ERREUR IDENTIFIÉE : le commentaire
    // de tête de l'IDB dit lui-même « [game] distance from world click point (flt_800130..) »
    // alors que le SYMBOLE de ce même global est nommé g_CameraPos ; la rédaction précédente
    // de ce bandeau avait recopié le commentaire trompeur plutôt que le symbole.
    //
    // RENOMMÉE (W9) `IsBeyondClickRange` -> `IsBeyondCameraNearCull` : aucun autre fichier
    // ne référençait ce membre (World/TerrainPicker.h:199 ne cite NameplateHost que dans un
    // commentaire).
    //
    // CONVENTION D'APPEL CONSERVÉE : l'appelant passe y DÉJÀ décalé de +10 — fidèle à
    // l'appel d'origine @0x56EF96 `Target_IsBeyondClickRange((float*)this+63 /* = pos */,
    // 20.0)`, soit a1[1] + a2*0.5 = y + 10.
    // DÉSORMAIS BRANCHÉE (W9) sur game::IsBeyondCameraNearCull (Game/EntityDrawLogic.cpp:22,
    // même formule) par Scene/WorldRenderer.cpp::drawNameplatePass(). Défaut (non branché) :
    // true (aucun cull) — conservé pour les appelants qui n'ont pas de caméra sous la main.
    std::function<bool(float x, float y, float z)> IsBeyondCameraNearCull;

    // Cam_ProjectToScreen 0x6A24F0 : le point projette-t-il à l'écran ? Gate la totalité du
    // bloc "nom" (mais PAS les barres PV/PM, qui ont leur propre appel). Hors périmètre (Gfx
    // pur) ; défaut (non branché) : toujours à l'écran (true).
    std::function<bool(float x, float y, float z)> IsOnScreen;

    // StrTable005_Get(g_LangId, id) 0x4C1D10 : résolution d'un id de chaîne localisée. Hors
    // périmètre (table de chaînes, asset). Défaut (non branché) : chaîne vide.
    std::function<std::string(int stringId)> ResolveString;

    // UI_GetValueTierString 0x54CD40 : préfixe de « palier » à partir de rankTierValue (seuils
    // 100/300/600/1000/1500/2100/3000 -> ids de chaîne 2077..2083, "" sinon). Encapsule
    // ResolveString + les seuils (résolution complète en une fois pour simplifier l'appelant).
    // Défaut (non branché) : "" (aucun préfixe).
    std::function<std::string(int rankTierValue)> ResolveValueTierPrefix;

    // Item_GetEnchantNameString 0x548330(grade+1, rawValue) : libellé d'enchantement d'arme.
    // Hors périmètre (table Item). Défaut (non branché) : "".
    std::function<std::string(int grade, int rawValue)> ResolveEnchantName;

    // Combat_CanTargetOnMap 0x558740(g_LocalPlayerSheet, element, pkLevel, affiliationName) :
    // true si l'entité est une cible ennemie légitime sur la carte/zone courante (dépend du
    // mode PVP de zone Map_GetPvpMode + morph spécial, cf. bandeau). Hors périmètre (système de
    // zones PVP). Défaut (non branché) : false (traite tout le monde comme "allié/non-ciblable",
    // conservateur — ne masque jamais un nom par erreur).
    std::function<bool(int element, int pkLevel, const std::string& affiliationName)> CanTargetOnMap;

    // Égalité d'affiliation avec le joueur local (cf. note de sémantique incertaine en tête de
    // fichier — sentinel différent selon le contexte : byte_1686138 en mode marché morph==291,
    // unk_16746A8 en branche normale). Défaut (non branché) : false.
    std::function<bool(const std::string& affiliationName)> IsSameAffiliationAsLocal;

    // Égalité d'alliance avec le joueur local (sentinel g_AllianceRosterNames). Défaut (non
    // branché) : false.
    // NB : Char_GetPairedElement 0x557C00 n'est PAS appelée par Char_DrawNameplate (absente de
    // ses `refs` de désassemblage, contrairement à Npc_GetNameplateColor) -> pas de callback
    // dédié ici ; à capturer par l'appelant dans CanTargetOnMap/IsSameAffiliationAsLocal si un
    // futur RE de la couche sociale en a besoin.
    std::function<bool(const std::string& allianceName)> IsSameAllianceAsLocal;
};

// ===========================================================================
// Char_DrawNameplate 0x56EF40 — logique pure (aucun rendu D3D). `drawMode`/`notSelf` = a2/a3
// d'origine (a4 confirmé jamais lu, supprimé de l'API — cf. bandeau de tête).
// ===========================================================================
NameplateInfo ComputeNameplateInfo(const NameplateActor& actor,
                                    int drawMode,
                                    bool notSelf,
                                    const NameplateViewerContext& ctx,
                                    const NameplateHost& host);

} // namespace ts2::game

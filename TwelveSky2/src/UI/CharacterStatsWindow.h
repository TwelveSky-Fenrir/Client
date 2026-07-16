// UI/CharacterStatsWindow.h — fenêtre « Fiche personnage » (stats calculées).
//
// Affiche l'état DÉJÀ CALCULÉ de game::g_World.self (StatEngine::Recompute, qui
// invoque lui-même les 15 fonctions Char_Calc* réécrites dans Game/StatFormulas.h/
// .cpp — voir ce header pour la table des adresses d'origine). Cette fenêtre ne
// recalcule RIEN : elle lit SelfState telle quelle (niveau, 4 attributs primaires,
// points non dépensés, stats dérivées maxHp/maxMp/extAtk/intAtk/extDef/intDef/
// accuracy/evasion/critRate/atkRatingMin/atkRatingMax/attackSpeed).
//
// GEOMETRIE — CONFIRME_FIDELE (2026-07-14, décompilation fraîche idaTs2, cette
// passe d'audit) : la fenêtre d'origine EST identifiée — cDrawWin_Draw 0x629960
// (rendu) + cDrawWin_OnMouseDown 0x628EA0 (hit-test), toutes deux lisent/écrivent
// dword_16731B8..D0 (attrDefensive/attrExtForce/attrOffensive/attrIntForce/
// g_SelfUnspentAttrPoints), soit EXACTEMENT les champs de PrimaryAttr/unspentAttr
// ci-dessous — ce n'était pas visible lors de la passe précédente (aucun nom
// UI_CharSheet_* dans l'IDB, d'où l'ancienne conclusion « non retrouvée »). Points
// vérifiés qui CONTREDISENT l'implémentation précédente (fenêtre centrée, bouton
// fermeture en haut-droite, 4 boutons "+" empilés en colonne) :
//   1) ANCRAGE : PAS de centrage écran. cDrawWin_Draw calcule son origine via
//      UI_ProjectSpriteToScreen(&g_PlayerCmdController, /*spriteSlot*/297,
//      /*designX*/115, /*designY*/105, &outX, &outY) (0x629 9AA), qui applique
//      exactement (désassemblage 0x50F5D0) :
//        outX = round((115 + w/2) * nWidth  / 1024.0f) - w/2
//        outY = round((105 + h/2) * nHeight / 768.0f)  - h/2
//      où nWidth/nHeight = 0x1669184/0x1669188 (résolution RÉELLE, cf.
//      Core/Types.h) et 1024.0f/768.0f sont les constantes flt_1669178/
//      flt_166917C initialisées depuis flt_7A68C8/flt_7A68C4 dans WinMain
//      (0x4609D3..0x4609E5) — CONFIRME octet-exact (bytes 00 00 80 44 / 00 00 40
//      44 = IEEE754 1024.0f / 768.0f). C'est EXACTEMENT kRefWidth/kRefHeight
//      (Core/Types.h) : à la résolution de référence 1024x768 la formule se
//      réduit à outX=115, outY=105 (le panneau est ancré près du coin HAUT-GAUCHE
//      de l'écran, PAS centré) ; à toute autre résolution, le CENTRE du sprite
//      (dont la taille pixel est fixe, non mise à l'échelle) est repositionné à
//      la même fraction d'écran que sa position de conception — mécanisme
//      "ancre proportionnelle", différent du centrage pur pratiqué par
//      SkillTreeWindow/UI_SkillLearn_Draw. `w`/`h` réels (dimensions du sprite de
//      fond, 4 variantes selon palier : unk_8F3704/94B470/90E774/985AB0) restent
//      NON confirmés statiquement (chargés depuis un .IMG au runtime) ; kBoxW/
//      kBoxH ci-dessous restent donc une approximation (comme avant), mais la
//      FORMULE D'ANCRAGE elle-même est désormais fidèle.
//   2) BOUTON FERMETURE : cDrawWin_OnMouseDown teste Sprite2D_HitTest(unk_8F3798,
//      *this+8, *(this+1)+6, ...) (0x629188) -> le bouton fermeture réel est en
//      HAUT-GAUCHE (offset (8,6) depuis l'origine du panneau), PAS en haut-droite.
//   3) BOUTONS "+1" ATTRIBUT : grille 2x2 réelle (PAS une colonne de 4), lue dans
//      cDrawWin_OnMouseDown (0x628F02..0x629007) et redessinée dans cDrawWin_Draw
//      (0x62A26C..0x62A3C7) : offsets FIXES depuis l'origine du panneau (Sprite2D
//      *this+dx, *(this+1)+dy) : ExtForce (52,109), IntForce (148,109),
//      Defensive (52,131), Offensive (148,131) — colonne gauche/droite = x=52/148,
//      ligne haut/bas = y=109/131. Correspond exactement à l'ordre PrimaryAttr
//      (ExtForce=0,IntForce=1,Defensive=2,Offensive=3) mappé en grille
//      (col=i%2, row=i/2).
//   4) BOUTONS "+5" ATTRIBUT (câblés Passe 4 / W6, ex-« hors périmètre ») : second
//      jeu de 4 boutons, sprite unk_940260, offsets (67,109)/(163,109)/(67,131)/
//      (163,131) — même grille, colonne x=67/163. Armés seulement si
//      g_SelfUnspentAttrPoints >= 5 (cDrawWin_OnMouseDown 0x629027, cDrawWin_Draw
//      0x62A3D3) ; émettent les args 5/6/7/8 et décrémentent de 5
//      (cDrawWin_OnCommit 0x629554/0x629602/0x6296B0/0x629761).
// Résiduel non confirmé (inchangé) : dimensions exactes des sprites de fond
// (kBoxW/kBoxH), taille exacte des boutons "+"/fermeture (kPlusSize/kCloseSize -
// dépendent des sprites unk_8F416C/940260/8F3798, chargés depuis un .IMG au runtime,
// non dumpés). Les VALEURS affichées restent byte-exactes (StatFormulas.h), seule la
// géométrie du panneau a changé.
// TODO [ancre 0x8F416C / 0x940260] kPlusSize=18 est une approximation : l'écart réel
// entre la colonne "+1" (x=52/148) et la colonne "+5" (x=67/163) n'est que de 15 px,
// donc à 18 px les deux rectangles se CHEVAUCHENT de 3 px. Sans conséquence
// fonctionnelle ici : l'ordre de test reproduit celui du binaire (les 4 "+1" AVANT
// les 4 "+5", 0x628EDC puis 0x629027), donc la zone commune est attribuée au "+1"
// exactement comme dans le jeu d'origine. À corriger dès que les dims réelles des
// sprites seront dumpées.
//
// Dépense de point d'attribut ("+") — PROUVÉ (Passe 4 / W6, décompilation fraîche
// cDrawWin_OnCommit 0x6291F0) : ce n'est PAS le dispatcher 0x58 (hypothèse de la
// passe précédente, désormais RÉFUTÉE — Net_OnCultivationDispatch 0x493180 est le
// dispatcher ENTRANT qui applique le résultat). L'émission réelle est :
//   Net_SendVaultReq_206(arg)  // 0x590430
//     -> Net_SendPacket_Op19(&g_AutoPlayMgr, 206, &arg)  // 0x4B4E70
//     -> opcode sortant 0x13 @+8, sous-code 206 u32 LE @+9, payload 100 o @+13
//        dont payload[0..3] = arg int32 LE (Crt_Memcpy(v2, &a1, 4) 0x590454 — le
//        piège « char sortant émis sur 4 octets LE »), payload[4..99] = pile NON
//        initialisée (_BYTE v2[108] sans memset). Taille totale figée à 113 (0x71).
//   arg = 1/2/3/4 -> +1 sur ExtForce/IntForce/Defensive/Offensive ;
//   arg = 5/6/7/8 -> +5 sur les mêmes, dans le même ordre.
#pragma once
#include "UI/UIManager.h"
#include "Game/StatFormulas.h"
#include "Game/GameState.h"

namespace ts2::ui {

// Les 4 attributs primaires dépensables (ordre d'affichage de la fenêtre).
enum class PrimaryAttr : int {
    ExtForce  = 0, // attrExtForce  (offset ITEM_INFO 292)
    IntForce  = 1, // attrIntForce  (offset ITEM_INFO 296)
    Defensive = 2, // attrDefensive (offset ITEM_INFO 300)
    Offensive = 3, // attrOffensive (offset ITEM_INFO 304)
};
inline constexpr int kPrimaryAttrCount = 4;

// -----------------------------------------------------------------------------
// CharacterStatsWindow — fiche personnage modale légère (fermable), non draggable.
// Lit game::g_World.self à chaque Render (aucun état dupliqué côté fenêtre).
class CharacterStatsWindow : public Dialog {
public:
    void Open() override;                       // centre + réarme les latches
    // Close() héritée telle quelle (bOpen_=false).

    bool OnMouseDown(int x, int y) override;     // arme close/+ si survolés
    bool OnClick(int x, int y) override;         // valide close/+ si relâché dessus
    bool OnKey(int vk) override;                 // Échap -> ferme

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };

    // Géométrie recalculée à chaque frame (centrage sur l'écran courant).
    struct Layout {
        Rect box;                                 // panneau complet
        Rect titleBar;                             // bandeau de titre
        Rect closeBtn;                             // bouton fermeture (coin haut-droit)
        Rect plusBtn[kPrimaryAttrCount];            // bouton "+1" par attribut primaire
        Rect plus5Btn[kPrimaryAttrCount];           // bouton "+5" par attribut primaire (0x62904D..)
    };
    void ComputeLayout(int screenW, int screenH, Layout& L) const;

    // Émet la dépense de points d'attribut et applique les effets locaux du binaire.
    // `arg` = valeur envoyée au serveur (1..4 pour "+1", 5..8 pour "+5") ; `cost` =
    // décrément appliqué à unspentAttr (1 ou 5). Reproduit le corps de chaque branche
    // de cDrawWin_OnCommit 0x6291F0 — cf. .cpp pour le détail des ancres.
    void CommitAttrSpend(int arg, int cost);

    static const char* AttrLabel(PrimaryAttr a);
    static int          AttrValue(const game::SelfState& s, PrimaryAttr a);

    // Ancrage proportionnel réel (cf. bandeau CONFIRME_FIDELE ci-dessus) :
    // position de conception (1024x768) du coin de la fenêtre, projetée à la
    // résolution réelle via UI_ProjectSpriteToScreen 0x50F5D0.
    static constexpr int kDesignAnchorX = 115; // a3 de l'appel réel (0x629 9AA)
    static constexpr int kDesignAnchorY = 105; // a4 de l'appel réel
    // Offsets réels des 4 boutons "+1" (grille 2x2), depuis l'origine panneau
    // (cDrawWin_OnMouseDown 0x628F02.., cDrawWin_Draw 0x62A26C..).
    static constexpr int kPlusOffX[2] = { 52, 148 }; // colonne gauche / droite
    static constexpr int kPlusOffY[2] = { 109, 131 }; // ligne haute / basse
    // Offsets réels des 4 boutons "+5" — sprite unk_940260, mêmes lignes que "+1"
    // (kPlusOffY), colonnes décalées (cDrawWin_OnMouseDown 0x62904D/0x62909D/
    // 0x6290EC/0x62913E, cDrawWin_OnCommit 0x629514/0x6295C2/0x629670/0x629721).
    static constexpr int kPlus5OffX[2] = { 67, 163 }; // colonne gauche / droite
    // Bouton fermeture réel : offset (8,6) depuis l'origine panneau (HAUT-GAUCHE,
    // PAS haut-droite) — cDrawWin_OnMouseDown 0x629188.
    static constexpr int kCloseOffX = 8;
    static constexpr int kCloseOffY = 6;

    // Latches "clic-enfoncé -> relâché dessus" (pattern MsgBoxDialog/UI d'origine).
    // Miroirs de *(this+3..this+11) de cDrawWin_OnMouseDown/OnCommit 0x628EA0/0x6291F0.
    bool closeArmed_ = false;
    bool plusArmed_[kPrimaryAttrCount] = {false, false, false, false};   // *(this+3..+6)
    bool plus5Armed_[kPrimaryAttrCount] = {false, false, false, false};  // *(this+7..+10)

    // Dims écran mémorisées au dernier Render, pour aligner le hit-test (routé
    // entre deux frames) sur la géométrie effectivement dessinée.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

} // namespace ts2::ui

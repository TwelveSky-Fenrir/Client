// UI/TeleportWindow.cpp — page 76 de cNpcWin : téléportation payante (service code 76).
// Voir UI/TeleportWindow.h pour l'ancrage complet et la chaîne de vivacité.
//
// Ordre d'inclusion : Net/ EN PREMIER (NetClient.h tire <winsock2.h> avant <windows.h>) —
// même règle que UI/NpcDialogWindow.cpp et UI/GuildWindow.cpp.
#include "Net/SendPackets.h"      // net::Net_SendWarpRequest (alias i32 d'Op20 0x4B5000)
#include "Net/NetClient.h"        // net::GlobalNetClient() == &g_NetClient 0x8156A0
#include "Net/Rng.h"              // net::DefaultRng() (Rng_Next 0x7603FD)
#include "UI/TeleportWindow.h"
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h"   // game::g_Client (Var/VarF/VarGet/msg), game::Str(id)
#include "Game/GameState.h"       // game::g_World.self (level/levelBonus/element)
#include "Game/MapWarp.h"         // game::WarpAddr::*
#include "Game/MotionPools.h"     // game::GetFrameRange (miroir de g_MotionFrameRangeTable 0x14A9350)
#include "Game/StringTables.h"    // game::g_Strings.zoneNames (StrTable003 / mZONENAME)

#include <cstdio>                 // std::snprintf (libellé de ligne, best-effort)
#include <string>

namespace ts2::ui {

namespace {

// Fond de panneau (best-effort) : le vrai fond est le sprite `unk_8FE3E0` de l'atlas UI
// (cTeleportWin_Draw 0x628030 @0x6280b3 `Sprite2D_Draw(&unk_8FE3E0, *this, *(this+1))`) —
// slot d'atlas non résolu côté ClientSource, cf. TODO des dims dans le .h. Repli kColBg.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_02463.IMG");

// g_SysMsgColor 0x84DFD8 — non modélisé en champ propre ; longue traîne via Var(), même
// convention que UI/NpcDialogWindow.cpp:32 et Game/SocialSystem.cpp:69.
constexpr uint32_t kSysMsgColorAddr = 0x84dfd8u;

// dword_167589C — scalaire lu par la garde @0x627f0e (`if (dword_167589C >= 1)`), posé par
// Net/GameVarDispatch.cpp cases 148/149. Longue traîne via Var().
constexpr uint32_t kWarpEnableVarAddr = 0x167589Cu;

// Coordonnées de destination — CONSTANTE unique du binaire. PROUVÉ dans IDA :
// Motion_GetComboOffsetTable 0x5025E0 est un switch(élément 0..3) où chaque branche contient
// les mêmes 4 cases mapId {313,316,331,334} ; les 16 cases (4 éléments × 4 mapIds) écrivent
// TOUTES le même triplet et renvoient 1 :
//   arg2[0] = flt_7A9144 = 0x45262000 = 2658.0   (EA type 0x5037da/0x503807/... , 16 sites)
//   arg2[1] = flt_7ED9FC = 0x40000000 =    2.0
//   arg2[2] = flt_7A9140 = 0x43C18000 =  387.0
// Aucune dépendance à l'élément, au mapId, ni à l'état de clan (contraste : case 291 de la
// même fonction, elle, compare byte_1686138/dword_16746A8). Le résolveur de coordonnées n'est
// donc PAS bloquant pour cette page — la valeur est en dur. Vérifié case par case (get_bytes).
constexpr float kTeleportDestPos[3] = { 2658.0f, 2.0f, 387.0f };

} // namespace

TeleportWindow::TeleportWindow() {
    x_ = (ts2::kRefWidth  - kPanelW) / 2;
    y_ = (ts2::kRefHeight - kPanelH) / 2;
}

// ============================================================================
// cTeleportWin_GetDestMapId 0x6282D0 — slot 0..3 -> mapId, défaut 0.
// ============================================================================
int32_t TeleportWindow::DestMapId(int slot) {
    switch (slot) {
    case 0: return 313; // 0x6282ef
    case 1: return 316; // 0x6282f6
    case 2: return 331; // 0x6282fd
    case 3: return 334; // 0x628304
    default: return 0;  // 0x62830b
    }
}

// ============================================================================
// Cycle de vie — cTeleportWin_Init 0x627BA0 / UI_NpcWin_CloseRestore 0x5DC1F0
// ============================================================================
void TeleportWindow::Open() {
    // 0x627bac : *(this+180) = 76 (id de page). Cette classe EST la page 76 : pas de champ id.
    // 0x627bb6 : for(i<100) *(this+i+70) = 0 — purge des 100 verrous ; seuls fermer (+70) et
    // les 4 lignes (+71..74) sont utilisés ici.
    closeLatch_ = false;
    for (int i = 0; i < kSlotCount; ++i) slotLatch_[i] = false;
    Dialog::Open();          // *(this+3)=1 équivalent (visible)
}

void TeleportWindow::Close() {
    // La fermeture réelle passe par UI_NpcWin_CloseRestore 0x5DC1F0 (n'émet RIEN ; restitue au
    // besoin des slots de matériaux — sans objet pour cette page qui ne dépose rien).
    closeLatch_ = false;
    for (int i = 0; i < kSlotCount; ++i) slotLatch_[i] = false;
    Dialog::Close();
}

// ============================================================================
// Géométrie
// ============================================================================
void TeleportWindow::Recenter(int screenW, int screenH) {
    // *this     = nWidth/2  - Sprite2D_GetWidth(&unk_8FE3E0)/2
    // *(this+1) = nHeight/2 - Sprite2D_GetHeight(&unk_8FE3E0)/2   (dims -> kPanelW/H)
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

bool TeleportWindow::RowHit(int i, int mx, int my) const {
    // Inégalités STRICTES du binaire (OnCommit 0x627e4c..0x627e85, OnMouseDown idem) :
    //   a2 > *this+37 && a2 < *this+217 && a3 > *(this+1)+18*i+26 && a3 < *(this+1)+18*i+38
    return mx > x_ + 37 && mx < x_ + 217
        && my > y_ + 18 * i + 26 && my < y_ + 18 * i + 38;
}

bool TeleportWindow::CloseButtonHit(int mx, int my) const {
    // Sprite2D_HitTest(&unk_8F3798, *this+235, *(this+1)+4, mx, my) — dims du sprite -> kCloseW/H.
    return PointInRect(mx, my, x_ + 235, y_ + 4, kCloseW, kCloseH);
}

// ============================================================================
// Messages système
// ============================================================================
void TeleportWindow::SysMsg(int strId) {
    const uint32_t sysColor = static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
    game::g_Client.msg.System(game::Str(strId), sysColor);
}

// ============================================================================
// Armement + émission Op20 (mode 6) — queue de cTeleportWin_OnCommit 0x627f65..0x628008.
// Corps IDENTIQUE à UI/NpcDialogWindow.cpp::ArmWarpAndSendOp20 (mode 6), transcrit localement
// (le binaire n'a pas de fonction partagée ; ArmFullWarp de MapWarp.cpp n'est pas exporté).
// ============================================================================
void TeleportWindow::ArmWarpAndSendOp20(int32_t zoneId, const float pos[3]) {
    using namespace ts2::game;
    g_Client.Var (WarpAddr::MorphInProgress) = 1;      // 0x627f65 : g_MorphInProgress = 1
    g_Client.Var (WarpAddr::WarpModeCode)    = 6;      // 0x627f6f : dword_1675A8C = 6
    g_Client.Var (WarpAddr::WarpSub)         = 0;      // 0x627f79 : dword_1675A90 = 0
    g_Client.Var (WarpAddr::WarpTargetNpc)   = zoneId; // 0x627f86 : g_TargetZoneId = mapId
    // 0x627f95 : Crt_Memset(&dword_1675AA0, 0, 0x48) — non reproductible sur la map Var (pas
    // une image mémoire contiguë) ; seuls les champs RÉÉCRITS juste après sont posés, aucun
    // lecteur ClientSource ne dépend du reliquat 0x1675ACC..0x1675AE4. Même compromis et même
    // justification que UI/NpcDialogWindow.cpp et Game/MapWarp.cpp::ArmFullWarp.
    g_Client.Var (WarpAddr::WarpFlagA0)      = 0;      // 0x627f9d : dword_1675AA0 = 0
    g_Client.Var (WarpAddr::WarpFlagA4)      = 1;      // 0x627fa7 : dword_1675AA4 = 1
    g_Client.VarF(WarpAddr::WarpDelay)       = 0.0f;   // 0x627fb3 : flt_1675AA8 = 0.0
    g_Client.VarF(WarpAddr::WarpPosX)        = pos[0]; // 0x627fbc : flt_1675AAC = v13[0]
    g_Client.VarF(WarpAddr::WarpPosY)        = pos[1]; // 0x627fc5 : flt_1675AB0 = v13[1]
    g_Client.VarF(WarpAddr::WarpPosZ)        = pos[2]; // 0x627fce : flt_1675AB4 = v13[2]
    // 0x627fe7 : flt_1675AC4 = (float)(Rng_Next() % 360) — UN SEUL tirage, recopié. Tiré AVANT
    // les 4 nonces internes d'Op20 : l'ordre du RNG partagé est préservé.
    const float facing = static_cast<float>(ts2::net::DefaultRng().NextMod(360));
    g_Client.VarF(WarpAddr::WarpFacingA)     = facing; // 0x627fe7 : flt_1675AC4
    g_Client.VarF(WarpAddr::WarpFacingB)     = facing; // 0x627ff3 : flt_1675AC8 = flt_1675AC4

    // 0x628008 : Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C /*=6*/, mapId). Le singleton
    // g_NetClient 0x8156A0 est lu en GLOBAL par 0x4B5000 (aucun socket passé). L'ALIAS i32
    // (Net_SendWarpRequest) est OBLIGATOIRE : les mapIds 313/316/331/334 dépassent 127 et
    // seraient sign-étendus par le builder int8_t (cf. Net/SendPackets.h:257-265).
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendWarpRequest(*client, /*warpModeCode=*/6, zoneId);
}

// ============================================================================
// cTeleportWin_OnMouseDown 0x627BF0 — hit-test, arme un verrou (fermer OU ligne).
// ============================================================================
bool TeleportWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x627c04 : recentrage AVANT le hit-test

    if (CloseButtonHit(x, y)) {
        // 0x627c7c : Snd3D_PlayScaledVolume(flt_1487E3C, ..., 0, 100, 1) — clic (non branché).
        closeLatch_ = true;                 // 0x627c84 : *(this+70) = 1
    } else {
        const int32_t morphNpc = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // g_SelfMorphNpcId
        for (int i = 0; i < kSlotCount; ++i) {
            // 0x627d11 : ligne ignorée si sa destination == la ville/NPC courante du joueur.
            if (DestMapId(i) == morphNpc) continue;
            if (!RowHit(i, x, y)) continue;
            // 0x627d1e : son de clic. 0x627d29 : *(this+i+71) = 1. 0x627d34 : retour immédiat.
            slotLatch_[i] = true;
            return true;
        }
    }
    return true;   // modal : le dispatcher (page 76) consomme toujours le clic
}

// ============================================================================
// cTeleportWin_OnCommit 0x627D50 — relâchement : valide et arme/émet, ou ferme.
// ============================================================================
bool TeleportWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x627d64 : recentrage AVANT le hit-test

    if (closeLatch_) {                       // 0x627da8 : if (*(this+70))
        closeLatch_ = false;                 // 0x627db4 : purge
        if (CloseButtonHit(x, y)) {          // 0x627de0
            Close();                         // 0x627df1 : UI_NpcWin_CloseRestore
            return true;
        }
        return true;                         // sinon : fin -> return result (consommé)
    }

    // 0x627dfb : boucle sur les 4 lignes ; traite la PREMIÈRE verrouillée puis retourne.
    for (int i = 0; i < kSlotCount; ++i) {
        if (!slotLatch_[i]) continue;        // 0x627e24 : if (*(this+i+71))
        slotLatch_[i] = false;               // 0x627e36 : purge INCONDITIONNELLE

        if (RowHit(i, x, y)) {               // 0x627e4c..0x627e85
            const int32_t mapId = DestMapId(i);      // 0x627e93
            if (mapId) {                             // 0x627e9f : mapId 0 -> abandon muet
                // 0x627eac : v6 = g_SelfLevelBonus + g_SelfLevel. Garde de niveau via la table
                // g_MotionFrameRangeTable 0x14A9350 : SkillLevelTable_GetMin/Max(table, mapId) =
                // table[2*mapId-2]/table[2*mapId-1] == GetFrameRange(mapId).start/.end (Motion_
                // InitFrameTable pose {157,157} pour les rangées 312/315/330/333 -> mapId
                // 313/316/331/334 : la page n'est utilisable qu'à niveau EXACTEMENT 157).
                const int level = game::g_World.self.level + game::g_World.self.levelBonus;
                const game::MotionFrameRange* fr = game::GetFrameRange(mapId);
                // nullptr (table non initialisée) => {0,0}, miroir du BSS zéro qu'aurait lu le
                // binaire avant Motion_InitFrameTable (App_Init) — la garde échouerait alors.
                const int lvlMin = fr ? fr->start : 0;
                const int lvlMax = fr ? fr->end   : 0;
                if (level >= lvlMin && level <= lvlMax) {          // 0x627ee0
                    if (game::g_Client.VarGet(kWarpEnableVarAddr) >= 1) {   // 0x627f0e
                        // 0x627f4a : Motion_GetComboOffsetTable(g_LocalElement, mapId, v13). Pour
                        // les 4 mapIds la fonction renvoie TOUJOURS 1 et écrit la CONSTANTE
                        // kTeleportDestPos (prouvé case par case, cf. anonyme ci-dessus) : le
                        // `if (result)` @0x627f51 est donc toujours pris.
                        if (!game::g_Client.VarGet(game::WarpAddr::MorphInProgress))  // 0x627f5f
                            ArmWarpAndSendOp20(mapId, kTeleportDestPos);   // 0x627f65..0x628008
                        // 0x628010 : CloseRestore est HORS du `if (!g_MorphInProgress)` mais
                        // DANS le `if (result)` — la fenêtre se ferme même si le morph a bloqué
                        // l'armement. Reproduit tel quel.
                        Close();
                        return true;
                    }
                    SysMsg(2821);            // 0x627f21 : StrTable005_Get(g_LangId, 2821)
                    return true;
                }
                SysMsg(227);                 // 0x627ef2 : StrTable005_Get(g_LangId, 227)
                return true;
            }
        }
        return true;   // 0x627f02 : verrou consommé -> return result (même si la ligne rate)
    }
    return true;       // 0x62801c : modal
}

bool TeleportWindow::OnKey(int vk) {
    // UI_NpcWin_OnKey 0x5DE030 n'émet RIEN et ne filtre aucune touche pour la page 76 ; Échap =
    // convention du portage (même affordance que UI/NpcDialogWindow.cpp, non binaire).
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { Close(); return true; }
    return false;
}

// ============================================================================
// Rendu — cTeleportWin_Draw 0x628030
// ============================================================================
void TeleportWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;
    lastScreenW_ = ctx.screenW;             // mémorisés pour le recentrage des hit-tests
    lastScreenH_ = ctx.screenH;
    Recenter(ctx.screenW, ctx.screenH);     // 0x628054 : le Draw recentre lui aussi

    const int32_t morphNpc = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // g_SelfMorphNpcId

    if (ctx.phase == UiPhase::Panels) {
        // 0x6280b3 : Sprite2D_Draw(&unk_8FE3E0, *this, *(this+1)) — fond du panneau.
        kPanelBg.Draw(ctx, x_, y_, kPanelW, kPanelH, kColBg);
        ctx.DrawFrame(x_, y_, kPanelW, kPanelH, kColBorder, 2);

        // 0x6280be : if (*(this+70)) Sprite2D_Draw(&unk_8F3798, *this+235, *(this+1)+4). Dans le
        // binaire l'état REPOS du bouton fermer est baké dans le sprite de panneau unk_8FE3E0, et
        // unk_8F3798 n'est qu'un OVERLAY « enfoncé » dessiné quand *(this+70). Le panneau C++
        // étant un aplat sans bouton, on matérialise le bouton en permanence (repli visible),
        // teinté « enfoncé » quand le verrou est armé.
        ctx.FillRect(x_ + 235, y_ + 4, kCloseW, kCloseH, closeLatch_ ? kColClose : kColBorder);

        // 0x6280ed : for(i<4) — chaque ligne, SAUF si sa destination == g_SelfMorphNpcId
        // (0x628128) ; état couleur 2=verrouillé (0x6281a9), 3=survol (0x628191), 1=repos.
        for (int i = 0; i < kSlotCount; ++i) {
            if (DestMapId(i) == morphNpc) continue;
            const int state = slotLatch_[i] ? 2 : (RowHit(i, cursorX, cursorY) ? 3 : 1);
            const D3DCOLOR col = (state == 2) ? kColPressed : (state == 3) ? kColHover : kColRest;
            // La ligne cliquable couvre x in (x+37, x+217), y = y+18i+26..+38. On la matérialise
            // par un aplat (le vrai rendu est un libellé texte, cf. phase Text).
            ctx.FillRect(x_ + 37, y_ + 18 * i + 26, 180, 12, (col & 0x00FFFFFFu) | 0x40000000u);
        }
        return;
    }

    // --- Phase texte ---
    // 0x6281bd : cTeleportWin_FormatEntryLabel(i) = Crt_Vsnprintf("%s %s",
    //   StrTable003_Get(dword_84A6A8, mapId), StrTable005_Get(g_LangId, 225)) ; puis
    // 0x628220 : UI_DrawNumberValue(label, *this+127 - UI_MeasureNumberText(label)/2,
    //   *(this+1)+18*i+26, state). Libellé centré horizontalement sur x+127.
    const std::string suffix = game::Str(225);
    for (int i = 0; i < kSlotCount; ++i) {
        const int32_t mapId = DestMapId(i);
        if (mapId == morphNpc) continue;
        // StrTable003_Get(dword_84A6A8, mapId) == game::g_Strings.zoneNames.Get(mapId) (1-based,
        // "" hors bornes — même repli que le binaire &String 0x7ec95f).
        const char* zoneName = game::g_Strings.zoneNames.Get(mapId);
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "%s %s", zoneName, suffix.c_str());
        const int w = ctx.MeasureText(lbl);
        const D3DCOLOR col = slotLatch_[i] ? kColPressed
                            : (RowHit(i, cursorX, cursorY) ? kColHover : kColRest);
        ctx.Text(lbl, x_ + 127 - w / 2, y_ + 18 * i + 26, col);
    }
}

} // namespace ts2::ui

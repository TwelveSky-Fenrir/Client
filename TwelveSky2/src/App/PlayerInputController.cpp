// App/PlayerInputController.cpp — implementation FIDELE de Camera_UpdateFromInput 0x50B7D0.
//
// Verite = desassemblage TwelveSky2.exe (imagebase 0x400000). Voir PlayerInputController.h
// pour la carte du layout g_CameraCtrl 0x1668F60. Chaque bloc porte son ancre EA d'origine.
#include "App/PlayerInputController.h"
#include "Net/SendPackets.h"   // net::Net_SendCmd_251 0x592870 (builder EXISTANT, 12 o -> opcode 251)
#include "Net/NetClient.h"     // definition complete de net::NetClient (tiree aussi par SendPackets.h)

// D3DXVECTOR3/D3DXMATRIX + D3DXVec3Normalize/D3DXMatrixRotationY/D3DXVec3TransformNormal :
// fournis par <d3dx9.h> (deja tire par Gfx/Camera.h via PlayerInputController.h).

namespace ts2 {

// -----------------------------------------------------------------------------
// Update — corps de Camera_UpdateFromInput 0x50B7D0 (0x50B7EC..).
// -----------------------------------------------------------------------------
void PlayerInputController::Update(const input::InputSystem& in, gfx::Camera& cam,
                                   net::NetClient& nc, Scene scene) {
    // 0x50B7EC : garde d'entree EXACTE de l'original, verifiee au desassemblage :
    //     if ( g_SceneMgr != 6 || g_SceneSubState != 4 ) return;
    // g_SceneMgr 0x1676180 == 6 = InGame ; g_SceneSubState 0x1676184 == 4 = le sous-etat
    // MainTick de Scene_InGameUpdate 0x52C600 (pose @0x52C7F1, en fin de case 3 InitCamera)
    // = « le tick monde est pleinement demarre ». Consequence FIDELE : pendant Setup(0),
    // WaitFirstSpawn(1), Failed(2) et InitCamera(3), ce controleur est INTEGRALEMENT mort --
    // WASD, camera ET touches discretes, car LABEL_73 0x50C726 se trouve APRES cette garde.
    // ts2::g_SceneSubState (Scene/SceneManager.h) est le miroir du champ +4 de cSceneMgr,
    // lu ICI en GLOBAL comme dans le binaire : l'original ne recoit pas le sous-etat en
    // parametre (le parametre `scene` reste, lui, le miroir de g_SceneMgr deja en place).
    // La comparaison litterale a 4 reproduit le `cmp ds:g_SceneSubState, 4` d'origine.
    if (scene != Scene::InGame || g_SceneSubState != 4)
        return;

    // 0x50B7FA : si une saisie texte est active (g_UIEditBoxMgr 0x1668FC0), on saute le
    // bloc mouvement ; sinon on le traite. LABEL_73 (touches discretes) suit dans tous les cas
    // SAUF les chemins a `return` du bloc (orbite fallback A/D, ou morph en cours).
    const bool textInput = textInputActive_ && textInputActive_();
    if (!textInput) {
        if (UpdateWasd(in, cam, nc))
            return;   // chemins `return` de l'original (0x50C6F9/0x50C721 + morph) : skip LABEL_73
    }

    ProcessDiscreteKeys(in, nc);   // LABEL_73 0x50C726
}

// -----------------------------------------------------------------------------
// UpdateWasd — bloc mouvement (0x50B810..0x50C721). Renvoie true si l'original
// aurait fait un `return` global (ce qui saute LABEL_73), false s'il aurait fait
// `goto LABEL_73`.
// -----------------------------------------------------------------------------
bool PlayerInputController::UpdateWasd(const input::InputSystem& in, gfx::Camera& cam,
                                       net::NetClient& nc) {
    const bool blocked = selfBlocked_ && selfBlocked_();   // g_SelfCharInvBlock[0] 0x1673170

    // 0x50B810 : joueur bloque OU mouse-look OFF (this[2]==0) -> fallback orbite +/-6 (0x50C6E3).
    if (blocked || st_.mouseLook == 0) {
        if (in.IsKeyDown(input::dik::kA)) {                 // byte_8013F2 (A) 0x50C6E3
            cam.Orbit(6.0f * gfx::Camera::kDegToRad, 0.0f); // Cam_OrbitYaw(g_GfxRenderer, 6.0) 0x50C6F4
            return true;                                    // return (0x50C6F9)
        }
        if (in.IsKeyDown(input::dik::kD)) {                 // byte_8013F4 (D) 0x50C70B
            cam.Orbit(-6.0f * gfx::Camera::kDegToRad, 0.0f);// Cam_OrbitYaw(g_GfxRenderer, -6.0) 0x50C71C
            return true;                                    // return (0x50C721)
        }
        return false;                                       // goto LABEL_73 (0x50C70B)
    }

    // 0x50B820 : mode camera (this[3]).
    if (st_.mode == 1) {                                    // 0x50B827 : MODE 1 = joueur (envoie 251)
        const bool morph = morphInProgress_ && morphInProgress_();  // g_MorphInProgress==1 0x1675A88
        // Direction avant : l'original lit les globals de direction camera
        // g_CameraDir 0x800148 / flt_80014C / flt_800150 (Gfx-owned). cam.Forward() en est
        // l'equivalent modele (Camera.h : Forward = normalize(cible-oeil)).
        const D3DXVECTOR3 F = cam.Forward();

        if (in.IsKeyDown(input::dik::kW)) {                 // byte_8013E5 (W) 0x50B84E : avance
            if (morph) return true;                         // 0x50B857
            selfPos_[0] += st_.speed[0] * F.x;              // flt_1687330 0x50B870
            selfPos_[1] += st_.speed[0] * F.y;              // flt_1687334 0x50B888
            selfPos_[2] += st_.speed[0] * F.z;              // flt_1687338 0x50B8A0
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50B8B0
        } else if (in.IsKeyDown(input::dik::kS)) {          // byte_8013F3 (S) 0x50B8C7 : recule
            if (morph) return true;                         // 0x50B8D0
            selfPos_[0] -= st_.speed[0] * F.x;              // 0x50B8E9
            selfPos_[1] -= st_.speed[0] * F.y;              // 0x50B901
            selfPos_[2] -= st_.speed[0] * F.z;              // 0x50B919
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50B929
        } else if (in.IsKeyDown(input::dik::kA)) {          // byte_8013F2 (A) 0x50B940 : yaw gauche
            cam.Orbit(st_.speed[1] * -1.0f * gfx::Camera::kDegToRad, 0.0f); // Cam_OrbitYaw(this[10]*-1) 0x50B94E
        } else if (in.IsKeyDown(input::dik::kD)) {          // byte_8013F4 (D) 0x50B974 : yaw droite
            cam.Orbit(st_.speed[1] * 1.0f * gfx::Camera::kDegToRad, 0.0f);  // Cam_OrbitYaw(this[10]*1) 0x50B980
        } else if (in.IsKeyDown(input::dik::kQ)) {          // byte_8013E4 (Q) 0x50B9A5 : strafe gauche
            if (morph) return true;                         // 0x50B9B2
            D3DXVECTOR3 h(F.x, 0.0f, F.z);                  // (g_CameraDir, 0, flt_800150) 0x50B9BF
            D3DXVec3Normalize(&h, &h);                      // 0x50B9D8
            D3DXMATRIX r; D3DXMatrixRotationY(&r, 1.5707964f);  // 0x50B9EB
            D3DXVec3TransformNormal(&h, &h, &r);            // 0x50B9FC
            selfPos_[0] -= st_.speed[2] * h.x;              // 0x50BA10
            selfPos_[1] -= st_.speed[2] * h.y;              // 0x50BA25
            selfPos_[2] -= st_.speed[2] * h.z;              // 0x50BA3A
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BA4A
        } else if (in.IsKeyDown(input::dik::kE)) {          // byte_8013E6 (E) 0x50BA61 : strafe droite
            if (morph) return true;                         // 0x50BA6E
            D3DXVECTOR3 h(F.x, 0.0f, F.z);                  // 0x50BA7B
            D3DXVec3Normalize(&h, &h);                      // 0x50BA94
            D3DXMATRIX r; D3DXMatrixRotationY(&r, 1.5707964f);  // 0x50BAA7
            D3DXVec3TransformNormal(&h, &h, &r);            // 0x50BAB8
            selfPos_[0] += st_.speed[2] * h.x;              // 0x50BACC
            selfPos_[1] += st_.speed[2] * h.y;              // 0x50BAE1
            selfPos_[2] += st_.speed[2] * h.z;              // 0x50BAF6
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BB06
        } else if (in.IsKeyDown(input::dik::kR)) {          // byte_8013E7 (R) 0x50BB1A : monter
            if (morph) return true;                         // 0x50BB23
            selfPos_[1] += st_.speed[3];                    // flt_1687334 (Y) 0x50BB36
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BB46
        } else if (in.IsKeyDown(input::dik::kF)) {          // byte_8013F5 (F) 0x50BB5A : descendre
            if (morph) return true;                         // 0x50BB63
            selfPos_[1] -= st_.speed[3];                    // 0x50BB76
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BB86
        }
        return false;                                       // goto LABEL_73 (0x50B8B5)
    }

    // Modes 2 (0x50BB9C) et 3 (0x50C13A) = camera libre : deplacent g_CameraPos 0x800130 /
    // flt_800134.. via Cam_ClampDistance/Cam_SetLookAt/Camera_SetEyeTarget, SANS envoi reseau.
    // Ces globals (Gfx) ne sont PAS possedes par ce front ; le mode 1 (seul mode atteignable
    // via End -> this[3]=1) couvre le deplacement joueur demande.
    // TODO [ancre 0x800130] : reproduire les modes camera libres 2/3 (bloc camera Gfx).
    return false;                                           // goto LABEL_73
}

// -----------------------------------------------------------------------------
// ProcessDiscreteKeys — LABEL_73 0x50C726 : premier evenement APPUI -> switch scancode.
// -----------------------------------------------------------------------------
void PlayerInputController::ProcessDiscreteKeys(const input::InputSystem& in, net::NetClient& nc) {
    (void)nc;   // reserve : les preregles camera 1-9 (differes) emettent Net_SendCmd_251.

    // 0x50C726 : boucle « for i: si (g_UiCmdQueueFlags[5*i] & 0x80) -> break » = FirstKeyDownDik().
    const int dik = in.FirstKeyDownDik();
    if (dik < 0)
        return;                                            // i == g_UiCmdQueueCount (0x50C763) : rien

    const bool blocked = selfBlocked_ && selfBlocked_();    // g_SelfCharInvBlock[0] 0x1673170

    switch (dik) {
    // ------------------------------------------------------------------------
    // cases 2..10 = DIK_1..DIK_9 : PRESETS camera (Cam_SetLookAt coords fixes + envoi 251),
    // conditionnes par g_SelfMorphNpcId 0x1675A98 (194 / 270-274) et par les globals camera
    // g_CameraPos/flt_800134.. (Gfx). Ces coordonnees et le morph sont hors-front.
    // TODO [ancre 0x50C837] : preregles camera 1-9 (bloc camera Gfx + morph Game).
    // ------------------------------------------------------------------------
    case input::dik::kP:      // case 25 0x50DB80 : bascule mode 1<->2 (mouse-look requis)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // goto LABEL_240 (0x50DB80)
        if (st_.mode == 1)      st_.mode = 2;              // 0x50DBA9
        else if (st_.mode == 2) st_.mode = 1;              // 0x50DBB5
        break;

    case input::dik::kL:      // case 38 0x50DBD2 : mode 1<->3 + verrou cible (lockName)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // goto LABEL_240
        // Original : if(mode==1 && Crt_Strcmp(lockName, &String)) mode=3;
        //            elif(mode==3){ mode=1; Crt_StringInit(); }
        // lockName (this+16) et le global String 0x7EC95F ne sont pas modelises (verrou cible).
        // TODO [ancre 0x7EC95F] : semantique du verrou cible (lockName vs String).
        break;

    case input::dik::kZ:      // case 44 0x50DCB2 : speed[zoomIndex] -= 1 (min 1)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // 0x50DC90
        st_.speed[st_.zoomIndex] -= 1.0f;                  // 0x50DCB2
        if (st_.speed[st_.zoomIndex] < 1.0f) st_.speed[st_.zoomIndex] = 1.0f;  // 0x50DCCE
        break;

    case input::dik::kX:      // case 45 0x50DC52 : if(++zoomIndex == 4) zoomIndex = 0
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // 0x50DC52
        if (++st_.zoomIndex == 4) st_.zoomIndex = 0;       // 0x50DC6E
        break;

    case input::dik::kC:      // case 46 0x50DD14 : speed[zoomIndex] += 1 (max 100)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // 0x50DCF2
        st_.speed[st_.zoomIndex] += 1.0f;                  // 0x50DD14
        if (st_.speed[st_.zoomIndex] > 100.0f) st_.speed[st_.zoomIndex] = 100.0f;  // 0x50DD30
        break;

    case input::dik::kF12:    // case 88 0x50DD51 : Screenshot_SaveNext 0x5481A0 (aucune garde)
        if (screenshotHook_) screenshotHook_();
        break;

    case input::dik::kHome:   // case 199 0x50C7B6 : if(!blocked) toggle this[0]
        if (blocked) { RouteSceneKey(dik); break; }        // 0x50C7B6
        st_.homeToggle = st_.homeToggle ? 0 : 1;           // 0x50C7BB/CE/C3
        break;

    case input::dik::kPrior:  // case 201 0x50C7E5 : if(!blocked){ if(++this[1]==4) this[1]=1 }
        if (blocked) { RouteSceneKey(dik); break; }        // 0x50C7E5
        if (++st_.pgupCycle == 4) st_.pgupCycle = 1;       // 0x50C7FD
        break;

    case input::dik::kEnd:    // case 207 0x50DB25 : if(!blocked) toggle mouse-look + Input_ResetMouseState
        if (blocked) { RouteSceneKey(dik); break; }        // 0x50DB25
        {
            const bool wasMouseLook = (st_.mouseLook != 0);
            // Input_ResetMouseState 0x50E000 : reset mouseLook/mode/zoomIndex/speed[] +
            // Crt_StringInit (lockName) ; NE touche PAS homeToggle/pgupCycle.
            st_.mouseLook = 0;                              // +8
            st_.mode      = 0;                              // +12
            st_.zoomIndex = 0;                              // +32
            st_.speed[0] = 20.0f; st_.speed[1] = 5.0f;      // +36/+40
            st_.speed[2] = 5.0f;  st_.speed[3] = 5.0f;      // +44/+48
            // lockName clear (Crt_StringInit) -- lockName non modelise. TODO [ancre 0x7EC95F].
            if (wasMouseLook) { st_.mouseLook = 0; st_.mode = 0; }  // 0x50DB59/63 : mouse-look OFF
            else              { st_.mouseLook = 1; st_.mode = 1; }  // 0x50DB3B/45 : mouse-look ON (mode joueur)
        }
        break;

    default:                  // LABEL_240 0x50DDE4 : routage clavier vers la scene
        RouteSceneKey(dik);
        break;
    }
}

// -----------------------------------------------------------------------------
// RouteSceneKey — LABEL_240 0x50DDE4 : route la touche vers cSceneMgr_OnKeyDown 0x517F80.
// -----------------------------------------------------------------------------
void PlayerInputController::RouteSceneKey(int dik) {
    // Original : ne route que si (blocked || !mouseLook || aucune touche WASD/QERF tenue) ET
    // g_UIEditBoxMgr==6 (record==1) ET g_SelfActionState[0] 0x1687328 hors {11,12,33,34,35,36,37}
    // ET !UI_RouteKeyInput 0x5ADF50 ET dword_184BF5C/68 inactifs. Ces gardes lisent des globals
    // Game/UI hors-front -> routage simplifie (le hook fait le reste cote scene).
    // TODO [ancre 0x1687328] : garde-fous action-state / UI_RouteKeyInput non modelises.
    if (sceneKeyDown_) sceneKeyDown_(dik);
}

} // namespace ts2

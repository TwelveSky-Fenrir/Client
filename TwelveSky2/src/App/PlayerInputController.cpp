// App/PlayerInputController.cpp — faithful implementation of Camera_UpdateFromInput 0x50B7D0.
//
// Ground truth = TwelveSky2.exe disassembly (imagebase 0x400000). See PlayerInputController.h
// for the g_CameraCtrl 0x1668F60 layout map. Each block carries its original EA anchor.
#include "App/PlayerInputController.h"
#include "Net/SendPackets.h"   // net::Net_SendCmd_251 0x592870 (EXISTING builder, 12 bytes -> opcode 251)
#include "Net/NetClient.h"     // full definition of net::NetClient (also pulled in by SendPackets.h)

// D3DXVECTOR3/D3DXMATRIX + D3DXVec3Normalize/D3DXMatrixRotationY/D3DXVec3TransformNormal :
// provided by <d3dx9.h> (already pulled in by Gfx/Camera.h via PlayerInputController.h).

namespace ts2 {

// Update — body of Camera_UpdateFromInput 0x50B7D0 (0x50B7EC..).
void PlayerInputController::Update(const input::InputSystem& in, gfx::Camera& cam,
                                   net::NetClient& nc, Scene scene) {
    // 0x50B7EC: EXACT entry guard from the original, verified via disassembly:
    //     if ( g_SceneMgr != 6 || g_SceneSubState != 4 ) return;
    // g_SceneMgr 0x1676180 == 6 = InGame; g_SceneSubState 0x1676184 == 4 = the MainTick
    // sub-state of Scene_InGameUpdate 0x52C600 (set @0x52C7F1, at the end of case 3 InitCamera)
    // = "the world tick is fully started". Faithful consequence: during Setup(0),
    // WaitFirstSpawn(1), Failed(2), and InitCamera(3), this controller is ENTIRELY dead --
    // WASD, camera, AND discrete keys, because LABEL_73 0x50C726 sits AFTER this guard.
    // ts2::g_SceneSubState (Scene/SceneManager.h) mirrors the +4 field of cSceneMgr,
    // read HERE as a GLOBAL just like in the binary: the original does not receive the
    // sub-state as a parameter (the `scene` parameter still mirrors g_SceneMgr, already in place).
    // The literal comparison to 4 reproduces the original `cmp ds:g_SceneSubState, 4`.
    if (scene != Scene::InGame || g_SceneSubState != 4)
        return;

    // 0x50B7FA: if text input is active (g_UIEditBoxMgr 0x1668FC0), skip the movement
    // block; otherwise process it. LABEL_73 (discrete keys) follows in all cases EXCEPT
    // the block's `return` paths (A/D orbit fallback, or morph in progress).
    const bool textInput = textInputActive_ && textInputActive_();
    if (!textInput) {
        if (UpdateWasd(in, cam, nc))
            return;   // `return` paths of the original (0x50C6F9/0x50C721 + morph): skip LABEL_73
    }

    ProcessDiscreteKeys(in, nc);   // LABEL_73 0x50C726
}

// UpdateWasd — movement block (0x50B810..0x50C721). Returns true if the original
// would have made a global `return` (which skips LABEL_73), false if it would have
// done `goto LABEL_73`.
bool PlayerInputController::UpdateWasd(const input::InputSystem& in, gfx::Camera& cam,
                                       net::NetClient& nc) {
    const bool blocked = selfBlocked_ && selfBlocked_();   // g_SelfCharInvBlock[0] 0x1673170

    // 0x50B810: player blocked OR mouse-look OFF (this[2]==0) -> fallback orbit +/-6 (0x50C6E3).
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

    // 0x50B820: camera mode (this[3]).
    if (st_.mode == 1) {                                    // 0x50B827: MODE 1 = player (sends 251)
        const bool morph = morphInProgress_ && morphInProgress_();  // g_MorphInProgress==1 0x1675A88
        // Forward direction: the original reads the camera direction globals
        // g_CameraDir 0x800148 / flt_80014C / flt_800150 (Gfx-owned). cam.Forward() is the
        // modeled equivalent (Camera.h: Forward = normalize(target-eye)).
        const D3DXVECTOR3 F = cam.Forward();

        if (in.IsKeyDown(input::dik::kW)) {                 // byte_8013E5 (W) 0x50B84E: move forward
            if (morph) return true;                         // 0x50B857
            selfPos_[0] += st_.speed[0] * F.x;              // flt_1687330 0x50B870
            selfPos_[1] += st_.speed[0] * F.y;              // flt_1687334 0x50B888
            selfPos_[2] += st_.speed[0] * F.z;              // flt_1687338 0x50B8A0
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50B8B0
        } else if (in.IsKeyDown(input::dik::kS)) {          // byte_8013F3 (S) 0x50B8C7: move backward
            if (morph) return true;                         // 0x50B8D0
            selfPos_[0] -= st_.speed[0] * F.x;              // 0x50B8E9
            selfPos_[1] -= st_.speed[0] * F.y;              // 0x50B901
            selfPos_[2] -= st_.speed[0] * F.z;              // 0x50B919
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50B929
        } else if (in.IsKeyDown(input::dik::kA)) {          // byte_8013F2 (A) 0x50B940: yaw left
            cam.Orbit(st_.speed[1] * -1.0f * gfx::Camera::kDegToRad, 0.0f); // Cam_OrbitYaw(this[10]*-1) 0x50B94E
        } else if (in.IsKeyDown(input::dik::kD)) {          // byte_8013F4 (D) 0x50B974: yaw right
            cam.Orbit(st_.speed[1] * 1.0f * gfx::Camera::kDegToRad, 0.0f);  // Cam_OrbitYaw(this[10]*1) 0x50B980
        } else if (in.IsKeyDown(input::dik::kQ)) {          // byte_8013E4 (Q) 0x50B9A5: strafe left
            if (morph) return true;                         // 0x50B9B2
            D3DXVECTOR3 h(F.x, 0.0f, F.z);                  // (g_CameraDir, 0, flt_800150) 0x50B9BF
            D3DXVec3Normalize(&h, &h);                      // 0x50B9D8
            D3DXMATRIX r; D3DXMatrixRotationY(&r, 1.5707964f);  // 0x50B9EB
            D3DXVec3TransformNormal(&h, &h, &r);            // 0x50B9FC
            selfPos_[0] -= st_.speed[2] * h.x;              // 0x50BA10
            selfPos_[1] -= st_.speed[2] * h.y;              // 0x50BA25
            selfPos_[2] -= st_.speed[2] * h.z;              // 0x50BA3A
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BA4A
        } else if (in.IsKeyDown(input::dik::kE)) {          // byte_8013E6 (E) 0x50BA61: strafe right
            if (morph) return true;                         // 0x50BA6E
            D3DXVECTOR3 h(F.x, 0.0f, F.z);                  // 0x50BA7B
            D3DXVec3Normalize(&h, &h);                      // 0x50BA94
            D3DXMATRIX r; D3DXMatrixRotationY(&r, 1.5707964f);  // 0x50BAA7
            D3DXVec3TransformNormal(&h, &h, &r);            // 0x50BAB8
            selfPos_[0] += st_.speed[2] * h.x;              // 0x50BACC
            selfPos_[1] += st_.speed[2] * h.y;              // 0x50BAE1
            selfPos_[2] += st_.speed[2] * h.z;              // 0x50BAF6
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BB06
        } else if (in.IsKeyDown(input::dik::kR)) {          // byte_8013E7 (R) 0x50BB1A: move up
            if (morph) return true;                         // 0x50BB23
            selfPos_[1] += st_.speed[3];                    // flt_1687334 (Y) 0x50BB36
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BB46
        } else if (in.IsKeyDown(input::dik::kF)) {          // byte_8013F5 (F) 0x50BB5A: move down
            if (morph) return true;                         // 0x50BB63
            selfPos_[1] -= st_.speed[3];                    // 0x50BB76
            net::Net_SendCmd_251(nc, selfPos_);             // 0x50BB86
        }
        return false;                                       // goto LABEL_73 (0x50B8B5)
    }

    // Modes 2 (0x50BB9C) and 3 (0x50C13A) = free camera: move g_CameraPos 0x800130 /
    // flt_800134.. via Cam_ClampDistance/Cam_SetLookAt/Camera_SetEyeTarget, WITHOUT network send.
    // These globals (Gfx) are NOT owned by this front; mode 1 (the only mode reachable
    // via End -> this[3]=1) covers the requested player movement.
    // TODO [ancre 0x800130]: reproduce free camera modes 2/3 (Gfx camera block).
    return false;                                           // goto LABEL_73
}

// ProcessDiscreteKeys — LABEL_73 0x50C726: first KEY-DOWN event -> switch scancode.
void PlayerInputController::ProcessDiscreteKeys(const input::InputSystem& in, net::NetClient& nc) {
    (void)nc;   // reserved: camera presets 1-9 (deferred) emit Net_SendCmd_251.

    // 0x50C726: loop "for i: if (g_UiCmdQueueFlags[5*i] & 0x80) -> break" = FirstKeyDownDik().
    const int dik = in.FirstKeyDownDik();
    if (dik < 0)
        return;                                            // i == g_UiCmdQueueCount (0x50C763): nothing

    const bool blocked = selfBlocked_ && selfBlocked_();    // g_SelfCharInvBlock[0] 0x1673170

    switch (dik) {
    // cases 2..10 = DIK_1..DIK_9: camera PRESETS (Cam_SetLookAt fixed coords + send 251),
    // gated by g_SelfMorphNpcId 0x1675A98 (194 / 270-274) and by the camera globals
    // g_CameraPos/flt_800134.. (Gfx). These coordinates and the morph are out of front scope.
    // TODO [ancre 0x50C837]: camera presets 1-9 (Gfx camera block + Game morph).
    case input::dik::kP:      // case 25 0x50DB80: toggle mode 1<->2 (mouse-look required)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // goto LABEL_240 (0x50DB80)
        if (st_.mode == 1)      st_.mode = 2;              // 0x50DBA9
        else if (st_.mode == 2) st_.mode = 1;              // 0x50DBB5
        break;

    case input::dik::kL:      // case 38 0x50DBD2: mode 1<->3 + target lock (lockName)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // goto LABEL_240
        // Original: if(mode==1 && Crt_Strcmp(lockName, &String)) mode=3;
        //           elif(mode==3){ mode=1; Crt_StringInit(); }
        // lockName (this+16) and the global String 0x7EC95F are not modeled (target lock).
        // TODO [ancre 0x7EC95F]: target-lock semantics (lockName vs String).
        break;

    case input::dik::kZ:      // case 44 0x50DCB2: speed[zoomIndex] -= 1 (min 1)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // 0x50DC90
        st_.speed[st_.zoomIndex] -= 1.0f;                  // 0x50DCB2
        if (st_.speed[st_.zoomIndex] < 1.0f) st_.speed[st_.zoomIndex] = 1.0f;  // 0x50DCCE
        break;

    case input::dik::kX:      // case 45 0x50DC52: if(++zoomIndex == 4) zoomIndex = 0
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // 0x50DC52
        if (++st_.zoomIndex == 4) st_.zoomIndex = 0;       // 0x50DC6E
        break;

    case input::dik::kC:      // case 46 0x50DD14: speed[zoomIndex] += 1 (max 100)
        if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }  // 0x50DCF2
        st_.speed[st_.zoomIndex] += 1.0f;                  // 0x50DD14
        if (st_.speed[st_.zoomIndex] > 100.0f) st_.speed[st_.zoomIndex] = 100.0f;  // 0x50DD30
        break;

    case input::dik::kF12:    // case 88 0x50DD51: Screenshot_SaveNext 0x5481A0 (no guard)
        if (screenshotHook_) screenshotHook_();
        break;

    case input::dik::kHome:   // case 199 0x50C7B6: if(!blocked) toggle this[0]
        if (blocked) { RouteSceneKey(dik); break; }        // 0x50C7B6
        st_.homeToggle = st_.homeToggle ? 0 : 1;           // 0x50C7BB/CE/C3
        break;

    case input::dik::kPrior:  // case 201 0x50C7E5: if(!blocked){ if(++this[1]==4) this[1]=1 }
        if (blocked) { RouteSceneKey(dik); break; }        // 0x50C7E5
        if (++st_.pgupCycle == 4) st_.pgupCycle = 1;       // 0x50C7FD
        break;

    case input::dik::kEnd:    // case 207 0x50DB25: if(!blocked) toggle mouse-look + Input_ResetMouseState
        if (blocked) { RouteSceneKey(dik); break; }        // 0x50DB25
        {
            const bool wasMouseLook = (st_.mouseLook != 0);
            // Input_ResetMouseState 0x50E000: reset mouseLook/mode/zoomIndex/speed[] +
            // Crt_StringInit (lockName); does NOT touch homeToggle/pgupCycle.
            st_.mouseLook = 0;                              // +8
            st_.mode      = 0;                              // +12
            st_.zoomIndex = 0;                              // +32
            st_.speed[0] = 20.0f; st_.speed[1] = 5.0f;      // +36/+40
            st_.speed[2] = 5.0f;  st_.speed[3] = 5.0f;      // +44/+48
            // lockName clear (Crt_StringInit) -- lockName not modeled. TODO [ancre 0x7EC95F].
            if (wasMouseLook) { st_.mouseLook = 0; st_.mode = 0; }  // 0x50DB59/63: mouse-look OFF
            else              { st_.mouseLook = 1; st_.mode = 1; }  // 0x50DB3B/45: mouse-look ON (player mode)
        }
        break;

    default:                  // LABEL_240 0x50DDE4: keyboard routing to the scene
        RouteSceneKey(dik);
        break;
    }
}

// RouteSceneKey — LABEL_240 0x50DDE4: routes the key to cSceneMgr_OnKeyDown 0x517F80.
void PlayerInputController::RouteSceneKey(int dik) {
    // Original: only routes if (blocked || !mouseLook || no WASD/QERF key held) AND
    // g_UIEditBoxMgr==6 (record==1) AND g_SelfActionState[0] 0x1687328 outside {11,12,33,34,35,36,37}
    // AND !UI_RouteKeyInput 0x5ADF50 AND dword_184BF5C/68 inactive. These guards read
    // Game/UI globals out of front scope -> simplified routing (the hook does the rest scene-side).
    // TODO [ancre 0x1687328]: action-state guards / UI_RouteKeyInput not modeled.
    if (sceneKeyDown_) sceneKeyDown_(dik);
}

} // namespace ts2

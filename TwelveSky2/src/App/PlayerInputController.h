// App/PlayerInputController.h — controleur clavier in-game par frame.
//
// Miroir FIDELE de Camera_UpdateFromInput 0x50B7D0 (objet g_CameraCtrl 0x1668F60,
// initialise par mINPUT Camera_Init 0x50ABC0). Verite = desassemblage TwelveSky2.exe.
// Appele 1x/frame DANS la garde du pas fixe (App_FrameTick 0x462619), juste apres le
// poll clavier (Input_AcquireKeyboard 0x46260F) et AVANT la boucle cSceneMgr_Update.
//
// Role : lire l'etat clavier immediat (InputSystem::KeyState) + les evenements
// tamponnes (FirstKeyDownDik), en deriver le deplacement WASD du joueur (envoye au
// serveur via le builder EXISTANT Net_SendCmd_251 0x592870 / opcode 251), les
// bascules de camera/quickslots, le F12 screenshot, et router le reliquat clavier
// vers cSceneMgr_OnKeyDown 0x517F80.
//
// Layout g_CameraCtrl 0x1668F60 (offsets prouves — Camera_Init 0x50ABC0 +
// Input_ResetMouseState 0x50E000) :
//   +0  [0] int   homeToggle  = 1     (case 199)          Camera_Init 0x50ABC0
//   +4  [1] int   pgupCycle   = 1     (case 201)          Camera_Init 0x50ABC0
//   +8  [2] int   mouseLook   = 0     (gate mouvement)    Input_ResetMouseState 0x50E000
//   +12 [3] int   mode        = 0     (mode camera 1/2/3) Input_ResetMouseState 0x50E000
//   +16 [4] char[] lockName   = ""    (case 38, verrou cible)
//   +32 [8] int   zoomIndex   = 0     (cases 44/45/46)    Input_ResetMouseState 0x50E000
//   +36 [9]  float speed[0]   = 20.0  (avance/recule W/S) Input_ResetMouseState 0x50E000
//   +40 [10] float speed[1]   = 5.0   (pas yaw A/D mode1) Input_ResetMouseState 0x50E000
//   +44 [11] float speed[2]   = 5.0   (pas strafe Q/E)    Input_ResetMouseState 0x50E000
//   +48 [12] float speed[3]   = 5.0   (pas vertical R/F)  Input_ResetMouseState 0x50E000
//   +52..+88 : etat souris/zoom -- DEJA reifie dans gfx::Camera (Camera.h), non duplique ici.
#pragma once

#include "Input/InputSystem.h"   // input::InputSystem (KeyState/IsKeyDown/FirstKeyDownDik)
#include "Gfx/Camera.h"          // gfx::Camera (Forward/Orbit/kDegToRad)
#include "Scene/SceneManager.h"  // enum class ts2::Scene (gate InGame)
#include <functional>
#include <cstdint>

namespace ts2 {

// Forward-decl (le .h reste leger : la definition complete de NetClient est tiree
// par le .cpp via Net/SendPackets.h -> Net/NetClient.h).
namespace net { struct NetClient; }

// Etat de controle miroir de g_CameraCtrl 0x1668F60 (seuls les champs de CONTROLE ;
// les champs souris/zoom +52..+88 vivent dans gfx::Camera). Defauts = Camera_Init
// 0x50ABC0 + Input_ResetMouseState 0x50E000.
struct CameraCtrlState {
    int32_t homeToggle = 1;                     // +0  [0]  Camera_Init 0x50ABC0
    int32_t pgupCycle  = 1;                     // +4  [1]  Camera_Init 0x50ABC0
    int32_t mouseLook  = 0;                     // +8  [2]  Input_ResetMouseState 0x50E000
    int32_t mode       = 0;                     // +12 [3]  Input_ResetMouseState 0x50E000
    int32_t zoomIndex  = 0;                     // +32 [8]  Input_ResetMouseState 0x50E000
    float   speed[4]   = { 20.0f, 5.0f, 5.0f, 5.0f }; // +36..+48 [9..12] Input_ResetMouseState 0x50E000
    // +16 [4] lockName (case 38) : verrou cible compare a String 0x7EC95F -- non modelise
    // ici (Crt_Strcmp/Crt_StringInit + global String hors-front). TODO [ancre 0x7EC95F].
};

// Controleur clavier in-game. Une seule instance (l'original a un unique g_CameraCtrl).
class PlayerInputController {
public:
    PlayerInputController() = default;

    // Miroir Camera_UpdateFromInput 0x50B7D0. Appele 1x/frame gardee (App_FrameTick 0x462619).
    //  - in    : etat clavier (state[] immediat + tampon d'evenements).
    //  - cam   : camera (gfx::Camera) -- orbite/regard (Cam_OrbitYaw 0x69CEE0, etc.).
    //  - nc    : socket (net::NetClient) -- emission du deplacement (Net_SendCmd_251 0x592870).
    //  - scene : scene courante (gate g_SceneMgr==6 / 0x50B7EC).
    void Update(const input::InputSystem& in, gfx::Camera& cam,
                net::NetClient& nc, Scene scene);

    // --- Hooks vers les etats/fonctions inter-front non possedes (regle #6) ---
    // g_UIEditBoxMgr 0x1668FC0 : saisie texte active -> ignore le bloc mouvement (0x50B7FA).
    void SetTextInputActivePredicate(std::function<bool()> p) { textInputActive_ = std::move(p); }
    // g_SelfCharInvBlock[0] 0x1673170 : joueur bloque (inventaire/menu) -> orbite au lieu du mouvement.
    void SetSelfBlockedPredicate(std::function<bool()> p) { selfBlocked_ = std::move(p); }
    // g_MorphInProgress 0x1675A88 : morph en cours (==1) -> annule le deplacement WASD.
    void SetMorphInProgressPredicate(std::function<bool()> p) { morphInProgress_ = std::move(p); }
    // Screenshot_SaveNext 0x5481A0 (fonction Gfx/file non possedee) : appelee sur F12 (case 88).
    void SetScreenshotHook(std::function<void()> h) { screenshotHook_ = std::move(h); }
    // cSceneMgr_OnKeyDown 0x517F80 (LABEL_240 0x50DDE4) : routage du reliquat clavier.
    void SetSceneKeyDownHook(std::function<void(int)> h) { sceneKeyDown_ = std::move(h); }

    // --- Position self modelisee localement (flt_1687330/34/38 = self+252, Game-owned) ---
    void SetSelfPosition(float x, float y, float z) { selfPos_[0] = x; selfPos_[1] = y; selfPos_[2] = z; }
    const float* SelfPosition() const { return selfPos_; }

    const CameraCtrlState& State() const { return st_; }

private:
    // Bloc mouvement (mode 1 = joueur, envoie 251 ; fallback orbite +/-6 si mouse-look off).
    // Renvoie true si l'original aurait fait un `return` global (qui saute LABEL_73),
    // false s'il aurait fait `goto LABEL_73` (cf. corps 0x50B810..0x50C721).
    bool UpdateWasd(const input::InputSystem& in, gfx::Camera& cam, net::NetClient& nc);
    // LABEL_73 0x50C726 : premier evenement APPUI tamponne -> switch scancode discret.
    void ProcessDiscreteKeys(const input::InputSystem& in, net::NetClient& nc);
    // LABEL_240 0x50DDE4 : route la touche vers cSceneMgr_OnKeyDown (sous garde-fous).
    void RouteSceneKey(int dik);

    CameraCtrlState st_;
    // flt_1687330/34/38 0x1687330 (self entity +252, Game-owned) -- MODELE localement.
    // TODO [ancre 0x1687234+252] : synchroniser depuis l'entite self quand Game l'expose
    // (sinon desync serveur ; acceptable en dev/hors-ligne, la cible W1 = cabler le builder).
    float selfPos_[3] = { 0.0f, 0.0f, 0.0f };

    std::function<bool()>    textInputActive_;   // g_UIEditBoxMgr 0x1668FC0
    std::function<bool()>    selfBlocked_;        // g_SelfCharInvBlock 0x1673170
    std::function<bool()>    morphInProgress_;    // g_MorphInProgress 0x1675A88
    std::function<void()>    screenshotHook_;     // Screenshot_SaveNext 0x5481A0
    std::function<void(int)> sceneKeyDown_;       // cSceneMgr_OnKeyDown 0x517F80
};

} // namespace ts2

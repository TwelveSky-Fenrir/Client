// UI/ConfirmMsgBox.h — boîte de dialogue modale Oui/Non partagée (dword_1822438).
//
// Port FIDÈLE du MsgBox partagé du binaire : UI_MsgBox_Open 0x5C08C0 / _OnLButtonDown 0x5C0980
// / _OnLButtonUp 0x5C0A90 / _Render 0x5C3100, peint en QUEUE de scène par UI_RenderAllDialogs
// 0x5AE2D0 (@0x520EAF pour CharSelect ; l'exit passe par le même objet côté ServerSelect).
// Remplace la géométrie FillRect INVENTÉE de DeleteConfirmRender/ExitConfirmRender.
//
// Sprites (atlas UI g_AssetMgr_UiAtlasSlots 0x8E8B50, prouvés par UI_MsgBox_Render 0x5C3100) :
//   panneau slot 7 ; OK slots 8/9/10 (idle/survol/pressé) ; Annuler slots 11/12/13.
// Géométrie : panneau centré sur SA taille native (0x5C313B/0x5C3160) ; OK @ (panelX+165,
// panelY+90) (0x5C35F5) ; Annuler @ (panelX+241, panelY+90) (0x5C368D) ; titre centré à
// x=panelX+234, y=panelY+42 quand le corps est vide (0x5C31AC/0x5C31B2 — cas delete/exit).
//
// ⚠ L'OK du NoticeDlg (1 bouton) est à panelX+203 (0x5C0830) ; celui du MsgBox à panelX+165
// (car il pousse OK à gauche pour loger Annuler). Ce module est le MsgBox (2 boutons) — le
// NoticeDlg reste porté à part (LoginScene::RenderNotice, déjà fidèle).
//
// Module DÉCOUPLÉ : ne connaît ni l'atlas ni le device — l'appelant fournit un SpriteProvider
// (slot -> gfx::GpuTexture*) + son SpriteBatch/Font. Le panneau est RECENTRÉ à CHAQUE Render
// ET à chaque clic (comme le binaire) : sinon le hit-test dérive au redimensionnement.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <windows.h>   // POINT
#include <d3d9.h>      // D3DCOLOR

#include "Gfx/GpuTexture.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"

namespace ts2::ui {

class ConfirmMsgBox {
public:
    using OkFn = std::function<void()>;                          // action du switch(type) sur OK
    using SpriteProvider = std::function<gfx::GpuTexture*(int slot)>;

    // UI_MsgBox_Open 0x5C08C0 : ouvre avec un titre localisé + un `actionType` (le switch(type)
    // du binaire : 2=suppression, 1=sortie) + le callback OK. Corps toujours "" (delete/exit).
    void Open(std::string title, int32_t actionType, OkFn onOk);
    void Close();                                                // UI_ConfirmPrompt_Close 0x5C0960
    bool IsOpen() const { return open_; }
    int32_t Type() const { return type_; }

    // UI_MsgBox_Render 0x5C3100 : panneau 7 + OK (8/9/10) + Annuler (11/12/13) + titre. Recentre.
    // Fait ses PROPRES batchs (sprite puis font), comme DeleteConfirmRender le faisait.
    void Render(const SpriteProvider& sprite, gfx::SpriteBatch& sb, gfx::Font& font,
                int screenW, int screenH, POINT cursor, D3DCOLOR textColor);

    // UI_MsgBox_OnLButtonDown 0x5C0980 : arme btnPressed_ (OK/Annuler). Toujours modal (true).
    bool OnMouseDown(const SpriteProvider& sprite, int x, int y, int screenW, int screenH);
    // UI_MsgBox_OnLButtonUp 0x5C0A90 : sur OK -> Close puis onOk_() ; sur Annuler -> Close. Modal.
    bool OnMouseUp(const SpriteProvider& sprite, int x, int y, int screenW, int screenH);

private:
    void Recenter(const SpriteProvider& sprite, int screenW, int screenH);
    static bool HitSprite(gfx::GpuTexture* t, int x, int y, int mx, int my);

    bool        open_ = false;
    std::string title_;
    int32_t     type_ = 0;
    OkFn        onOk_;
    bool        btnPressed_[2] = {false, false}; // [0]=OK, [1]=Annuler
    int         panelX_ = 0, panelY_ = 0;        // recentrés à chaque Render/clic
};

} // namespace ts2::ui

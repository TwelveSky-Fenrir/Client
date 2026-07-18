#pragma once
// UI/LoginScene_Internal.h — file-local constants shared by the LoginScene.cpp split family
// (LoginScene.cpp, LoginScene_ServerSelect.cpp, LoginScene_Login.cpp, LoginScene_CharSelect.cpp,
// LoginScene_Notice.cpp). Anonymous-namespace-only content (constexpr), included solely by
// these translation units — mirrors the anonymous namespace of the original monolithic file.
#include <d3d9.h>

namespace ts2::ui {
namespace {

// Login panel (unk_8E9368) hardcoded offsets (Scene_LoginRender 0x51B020): fields at
// +126,+60 / +126,+95; OK button at +298,+126; Quit button at +374,+126; shadow checkbox at
// +21,+130. Button size below is NOMINAL (the real create-form Confirm/Cancel size comes from
// the atlas sprites, cf. LayoutCreateForm/LayoutLogin).
constexpr int kBtnW = 72, kBtnH = 24;

// Palette (ARGB) — restricted to the ONLY color still used after wiring the CharSelect create
// form 1:1 onto its real atlas sprites (2026-07-16, front W4-F1). All prior CharSelect
// placeholder colors (kColBackdrop/kColField/kColFieldFocus/kColSel/kColTitle/kColLabel/
// kColBtn + BtnColor) were REMOVED once the form was wired onto real sprites (panel slot 40
// unk_8EA270 @0x51E4C5; +/- slots 41/42, pressed state only; Confirm/Cancel slots 9/12; caret
// slot 43 g_SprTextInputCaret @0x51E53E — cf. CreateFormRender()). kColText = color of the
// real VALUES the binary draws (Login fields, character levels, create-form values, MsgBox
// title).
//
// NOTE: the former substitute colors kColPanel/kColPanelEdge were REMOVED together with the
// old DeleteConfirmRender/ExitConfirmRender — the Yes/No confirmations are now the real-sprite
// MsgBox (UI/ConfirmMsgBox.h, dword_1822438, UI_MsgBox_* 0x5C08C0): no more invented fills.
constexpr D3DCOLOR kColText = 0xFFE8ECF4; // main text color (real VALUES + MsgBox title)

} // namespace
} // namespace ts2::ui

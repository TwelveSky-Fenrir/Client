// Config/GameOptions.h — CONFIG/OPTIONS subsystem of the TwelveSky2 client.
//
// CLEAN C++ rewrite but BYTE-EXACT to the TwelveSky2.exe disassembly.
// The persistent options block is a 23-element array of 32-bit ints (92 bytes)
// serialized as-is to the file "G02_GINFO\001.BIN".
//
// Function <-> original address correspondence (idaTs2):
//   Options_SetDefaults        0x4C2020  -> GameOptions::SetDefaults()
//   Options_LoadAndNormalize   0x4C2110  -> GameOptions::LoadAndNormalize()
//   Options_LoadBin            0x4C2140  -> GameOptions::Load()
//   Options_Save_STUB          0x4C2130  -> GameOptions::SaveStub()  (App_Shutdown, no-op)
//   Options_SaveBin            0x4C2280  -> GameOptions::Save()
//
// Original global block: g_Options @ 0x84DEC0 (struct base, 23 fields).
// Each field is also exported as a global g_Opt_* in the IDB; the member
// names below reuse those symbols (see the "idx / EA" column).
//
// Runtime producer/normalization of the fields: UI_OptionsWnd_OnClick 0x66D140
// (checkboxes + +/- buttons with clamping), consumers scattered across the code
// (Fx_Attach*, Char_Draw*, Snd3D_*, Game_GetTierRange, Net_On*Invite*, …).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::config {

// Persistence file path (CreateFileA "G02_GINFO\\001.BIN", aG02Ginfo001Bin
// @ 0x7A6FE0). Relative to the client's current directory, like in the binary.
inline constexpr char kOptionsFilePath[] = "G02_GINFO\\001.BIN";

// "Last selected server/group" file path (CreateFileA
// "G02_GINFO\\010.BIN", aG02Ginfo010Bin @ 0x7A9750). Written by Cfg_SaveLastServer
// 0x519C40, called from Scene_ServerSelectOnMouseDown 0x519780 (EA 0x51990D) on
// every group/server click on the ServerSelect screen.
//
// WARNING: Cfg_LoadLastServer 0x519B50 (the READ of this file) has 0 xrefs in the
// whole binary — confirmed dead code, NEVER called (not at startup, not anywhere else). This
// file is therefore NEVER re-read by TwelveSky2.exe: only the WRITE is real
// behavior. Wire up ONLY Cfg_SaveLastServer below; do NOT add a
// startup read (that would be unfaithful to the binary).
inline constexpr char kLastServerFilePath[] = "G02_GINFO\\010.BIN";

// Total size of the persistent block: 23 x int32 = 92 bytes (0x5C).
inline constexpr size_t kOptionsByteSize = 92;

// EXACT serialization split (3 successive disk accesses in the binary):
//   block 1: offset 0x00, 40 bytes (0x28) -> fields idx 0..9
//   block 2: offset 0x28, 12 bytes (0x0C) -> fields idx 10..12
//   block 3: offset 0x34, 40 bytes (0x28) -> fields idx 13..22
// The 3 blocks are contiguous (92 bytes), but Load/Save reproduce the 3 accesses and
// their size checks to stay faithful to Options_LoadBin/Options_SaveBin.
inline constexpr size_t kChunk1Off = 0;   inline constexpr size_t kChunk1Size = 40; // 0x28
inline constexpr size_t kChunk2Off = 40;  inline constexpr size_t kChunk2Size = 12; // 0x0C
inline constexpr size_t kChunk3Off = 52;  inline constexpr size_t kChunk3Size = 40; // 0x28

// Persistent options block — BYTE-EXACT layout (23 x int32, 92 bytes).
// idx = dword index; EA = address of the corresponding g_Opt_* global;
// def = default value (Options_SetDefaults); [range] = range enforced
// by the UI (UI_OptionsWnd_OnClick) / expected by the consumers.
#pragma pack(push, 4)
struct GameOptions {
    // idx0  0x00  g_Options 0x84DEC0           def 1  [0,1]
    //   Master switch for skill/weapon visual effects (gated by
    //   Fx_AttachHitSpark/HitBurst/WeaponGlow*/SkillGlow*/SkillAura*, cmp ==0).
    int32_t ShowSkillEffects;
    // idx1  0x04  g_Opt_DisplayRangeTier 0x84DEC4  def 2  [1,3]
    //   Display-distance tier -> Game_GetTierRange 1000/2000/3000.
    int32_t DisplayRangeTier;
    // idx2  0x08  g_Opt_Toggle2_reserved 0x84DEC8  def 1  [0,1]
    //   Toggle edited by the UI but with no known runtime consumer.
    int32_t Toggle2Reserved;
    // idx3  0x0C  g_Opt_Reserved3 0x84DECC     def 1  (no xref — reserved)
    int32_t Reserved3;
    // idx4  0x10  g_Opt_ShowHitMarkers 0x84DED0  def 1  [0,1]
    //   Hit/damage markers (Char_DrawNameplate, Fx_MeleeSwingDrawMarker).
    int32_t ShowHitMarkers;
    // idx5  0x14  g_Opt_ShowNameplates 0x84DED4  def 1  [0,1]
    //   Nameplates (Char_DrawNameplate).
    int32_t ShowNameplates;
    // idx6  0x18  g_Opt_WeaponTrail 0x84DED8    def 1  [0,1]
    //   Weapon trail (Char_DrawWeaponTrailEffect).
    int32_t WeaponTrail;
    // idx7  0x1C  g_Opt_WeaponGlowLevel 0x84DEDC  def 0  [0,3]
    //   Weapon glow effect level 0/1/2/3 (Char_DrawWeaponEffect*).
    int32_t WeaponGlowLevel;
    // idx8  0x20  g_Opt_Reserved8 0x84DEE0      def 1  (no xref — reserved)
    int32_t Reserved8;
    // idx9  0x24  g_Opt_BrightnessLevel 0x84DEE4  def 7  [1,9]
    //   Brightness/gamma (Scene_InGameRender).
    int32_t BrightnessLevel;
    // ---- end of block 1 (offset 0x28) ----
    // idx10 0x28  g_Opt_MusicVolume 0x84DEE8    def 100  [0,100]
    //   BGM volume (Player_UpdateLocalAnim, UI_OptionsWnd_OnMouseDown: fild).
    int32_t MusicVolume;
    // idx11 0x2C  g_Opt_SoundVolume 0x84DEEC    def 100  [0,100]
    //   Sound effects volume (Snd3D_PlayScaledVolume/FullVolume/Positional).
    int32_t SoundVolume;
    // idx12 0x30  g_BgmEnabled 0x84DEF0         def 1  [0,1]
    //   Background music toggle (UI_OptionsWnd_OnClick: Snd_Stop/Snd_Play3D).
    int32_t BgmEnabled;
    // ---- end of block 2 (offset 0x34) ----
    // idx13 0x34  g_MorphUiMode 0x84DEF4        def 2  [1,2]
    //   Morph hotkey selector (F1..F10 if ==1). Decrement clamped to >=1
    //   (Game_OnHotkey, Skill_IsHotkeyPressed, Game_UseFirstReadySkill).
    int32_t MorphUiMode;
    // idx14 0x38  g_Opt_GfxDetailShadows 0x84DEF8  def 1  [0,1]
    //   Graphics detail: shadows/reflections (Char_DrawShadow/Reflection, also
    //   a toggle on the Scene_Login* login screen).
    int32_t GfxDetailShadows;
    // idx15 0x3C  g_Opt_FilterPartyChat 0x84DEFC  def 1  [0,1]
    //   Party chat filter (Pkt_PartyChatOrInvite).
    int32_t FilterPartyChat;
    // idx16 0x40  g_Opt_FilterPartyInvite 0x84DF00  def 1  [0,1]
    //   Party invite filter (Pkt_PartyInvitePrompt).
    int32_t FilterPartyInvite;
    // idx17 0x44  g_Opt_FilterAllyInvite 0x84DF04  def 1  [0,1]
    //   Alliance/guild invite filter (Pkt_AllyInvitePrompt).
    int32_t FilterAllyInvite;
    // idx18 0x48  g_Opt_FilterPrompt19 0x84DF08  def 1  [0,1]
    //   Confirmation prompt filter dlg19 (Net_OnConfirmPromptOpen_Dlg19).
    int32_t FilterPrompt19;
    // idx19 0x4C  g_Opt_FilterPrompt20 0x84DF0C  def 1  [0,1]
    //   Confirmation prompt filter dlg20 (Net_OnConfirmPromptOpen_Dlg20).
    int32_t FilterPrompt20;
    // idx20 0x50  g_Opt_FilterPrompt10 0x84DF10  def 1  [0,1]
    //   Confirmation prompt filter dlg10 (Net_OnConfirmPromptOpen_Dlg10).
    int32_t FilterPrompt10;
    // idx21 0x54  g_Opt_FilterPrompt14 0x84DF14  def 1  [0,1]
    //   Confirmation prompt filter dlg14 (Net_OnConfirmPromptOpen_Dlg14).
    int32_t FilterPrompt14;
    // idx22 0x58  g_Opt_FilterWorldEntity 0x84DF18  def 1  [0,1]
    //   "World entity" message filter (Net_OnWorldEntityDispatch, cmp ==1).
    int32_t FilterWorldEntity;
    // ---- end of block 3 (offset 0x5C = 92) ----

    // -- API (faithful to the binary's functions) --

    // Options_SetDefaults 0x4C2020: resets the 23 fields to their defaults.
    void SetDefaults();

    // Options_LoadBin 0x4C2140: reads the file in 3 blocks (40/12/40) with
    // exact size checking. On absence/error/partial read,
    // falls back to SetDefaults() (WITHOUT writing the file). Returns true if the
    // values come from the file, false if defaults were applied.
    bool Load(const char* path = kOptionsFilePath);

    // Options_SaveBin 0x4C2280: writes the file in 3 blocks (40/12/40),
    // CREATE_ALWAYS. Returns true if the 3 blocks (52 bytes guaranteed + attempt at the
    // 3rd) were emitted, false if the open failed. (The binary never
    // explicitly fails on the 3rd block; we reproduce that behavior.)
    bool Save(const char* path = kOptionsFilePath) const;

    // Options_LoadAndNormalize 0x4C2110: Load() then Save(). The binary's net
    // "normalization" effect is to MATERIALIZE the file on disk
    // (creating it with defaults if missing). Always returns true (like
    // the original, which returns 1).
    bool LoadAndNormalize(const char* path = kOptionsFilePath);

    // Options_Save_STUB 0x4C2130: empty stub called by App_Shutdown 0x462480.
    // Present for call-graph fidelity; does nothing.
    void SaveStub() {}

    // Clamping ("normalization" in the value-range sense). Does NOT exist as
    // such in the binary's config module: the clamp is done field-by-field
    // in UI_OptionsWnd_OnClick 0x66D140. Reconstructed here to offer a
    // single entry point. Does not touch the reserved fields (idx3/idx8).
    void Normalize();
};
#pragma pack(pop)

// Byte-exact layout checks.
static_assert(sizeof(GameOptions) == kOptionsByteSize, "GameOptions doit faire 92 octets");
static_assert(offsetof(GameOptions, ShowSkillEffects)  == 0x00, "layout idx0");
static_assert(offsetof(GameOptions, DisplayRangeTier)  == 0x04, "layout idx1");
static_assert(offsetof(GameOptions, MusicVolume)       == 0x28, "layout idx10");
static_assert(offsetof(GameOptions, SoundVolume)       == 0x2C, "layout idx11");
static_assert(offsetof(GameOptions, BgmEnabled)        == 0x30, "layout idx12");
static_assert(offsetof(GameOptions, MorphUiMode)       == 0x34, "layout idx13");
static_assert(offsetof(GameOptions, GfxDetailShadows)  == 0x38, "layout idx14");
static_assert(offsetof(GameOptions, FilterWorldEntity) == 0x58, "layout idx22");

// Single global instance — equivalent of g_Options @ 0x84DEC0.
extern GameOptions g_Options;

// Cfg_SaveLastServer 0x519C40 — persists the last group/server chosen
// on the ServerSelect screen (G02_GINFO\010.BIN, kLastServerFilePath).
//
// FREE function (NOT a GameOptions method): in the binary, `this`
// is the ServerSelect scene object (g_SceneMgr, field this+61484 / this[15371]),
// a structure totally different from g_Options @ 0x84DEC0 — the two
// files G02_GINFO\001.BIN and 010.BIN have nothing in common besides the
// folder. Byte-exact: CREATE_ALWAYS, writes a SINGLE int32 (4 bytes), no
// header/block. Returns false if the open fails (like the binary:
// hFile == INVALID_HANDLE_VALUE -> no write), true otherwise (the binary
// checks neither WriteFile nor CloseHandle).
//
// Do NOT add a "Load"/read counterpart at startup: Cfg_LoadLastServer
// 0x519B50 is confirmed dead code (0 xref) in the original binary — see
// the kLastServerFilePath comment above.
bool Cfg_SaveLastServer(int32_t lastServerIndex, const char* path = kLastServerFilePath);

} // namespace ts2::config

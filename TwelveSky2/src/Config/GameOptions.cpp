// Config/GameOptions.cpp — BYTE-EXACT implementation of the options subsystem.
//
// The original binary uses the Win32 API (CreateFileA/ReadFile/WriteFile).
// Under SDLCheck the CRT forbids fopen; we go through fopen_s + fread/fwrite,
// which produces a disk file STRICTLY identical (same 92 bytes,
// same 40/12/40 split). The original Win32 flags are documented next to
// each open call; they do not affect the on-disk bytes.
//
// Truth: Options_SetDefaults 0x4C2020, Options_LoadBin 0x4C2140,
//        Options_SaveBin 0x4C2280, Options_LoadAndNormalize 0x4C2110.
#include "Config/GameOptions.h"

#include <cstdio>

namespace ts2::config {

// Global instance (g_Options @ 0x84DEC0). Zero-initialized; the shell calls
// LoadAndNormalize() at startup (App_Init 0x461C20, ecx=offset g_Options).
GameOptions g_Options{};

// Options_SetDefaults 0x4C2020
// Writes the 23 dwords one by one in the binary's exact order.
void GameOptions::SetDefaults()
{
    ShowSkillEffects  = 1;   // idx0  *this      = 1  @0x4C202A
    DisplayRangeTier  = 2;   // idx1  *(this+1)  = 2  @0x4C2033
    Toggle2Reserved   = 1;   // idx2  *(this+2)  = 1  @0x4C203D
    Reserved3         = 1;   // idx3  *(this+3)  = 1  @0x4C2047
    ShowHitMarkers    = 1;   // idx4  *(this+4)  = 1  @0x4C2051
    ShowNameplates    = 1;   // idx5  *(this+5)  = 1  @0x4C205B
    WeaponTrail       = 1;   // idx6  *(this+6)  = 1  @0x4C2065
    WeaponGlowLevel   = 0;   // idx7  *(this+7)  = 0  @0x4C206F
    Reserved8         = 1;   // idx8  *(this+8)  = 1  @0x4C2079
    BrightnessLevel   = 7;   // idx9  *(this+9)  = 7  @0x4C2083
    MusicVolume       = 100; // idx10 *(this+10) = 100 @0x4C208D
    SoundVolume       = 100; // idx11 *(this+11) = 100 @0x4C2097
    BgmEnabled        = 1;   // idx12 *(this+12) = 1  @0x4C20A1
    MorphUiMode       = 2;   // idx13 *(this+13) = 2  @0x4C20AB
    GfxDetailShadows  = 1;   // idx14 *(this+14) = 1  @0x4C20B5
    FilterPartyChat   = 1;   // idx15 *(this+15) = 1  @0x4C20BF
    FilterPartyInvite = 1;   // idx16 *(this+16) = 1  @0x4C20C9
    FilterAllyInvite  = 1;   // idx17 *(this+17) = 1  @0x4C20D3
    FilterPrompt19    = 1;   // idx18 *(this+18) = 1  @0x4C20DD
    FilterPrompt20    = 1;   // idx19 *(this+19) = 1  @0x4C20E7
    FilterPrompt10    = 1;   // idx20 *(this+20) = 1  @0x4C20F1
    FilterPrompt14    = 1;   // idx21 *(this+21) = 1  @0x4C20FB
    FilterWorldEntity = 1;   // idx22 *(this+22) = 1  @0x4C2105
}

// Options_LoadBin 0x4C2140
// CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
//             FILE_ATTRIBUTE_NORMAL(0x80), NULL)  -> fopen_s(..,"rb")
// Reads in 3 blocks with strict size checking; defaults on failure.
bool GameOptions::Load(const char* path)
{
    std::FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || f == nullptr) // hFile == INVALID_HANDLE_VALUE
    {
        SetDefaults();                                // @0x4C2172
        return false;
    }

    unsigned char* raw = reinterpret_cast<unsigned char*>(this);

    // ReadFile(this,      0x28) && n==40  &&
    // ReadFile(this+10,   0x0C) && n==12  &&
    // ReadFile(this+13,   0x28) && n==40
    const bool ok =
        std::fread(raw + kChunk1Off, 1, kChunk1Size, f) == kChunk1Size &&
        std::fread(raw + kChunk2Off, 1, kChunk2Size, f) == kChunk2Size &&
        std::fread(raw + kChunk3Off, 1, kChunk3Size, f) == kChunk3Size;

    if (!ok)
    {
        std::fclose(f);                               // CloseHandle @0x4C2250
        SetDefaults();                                // @0x4C2259
        return false;
    }

    // The binary also falls back to defaults if CloseHandle fails.
    if (std::fclose(f) != 0)                          // CloseHandle @0x4C2264
    {
        SetDefaults();                                // @0x4C2271
        return false;
    }
    return true;
}

// Options_SaveBin 0x4C2280
// CreateFileA(name, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
//             FILE_ATTRIBUTE_NORMAL(0x80), NULL)  -> fopen_s(..,"wb")
// Writes in 3 blocks (40/12/40). The original does not report any error beyond
// the open failure; we reproduce that contract.
bool GameOptions::Save(const char* path) const
{
    std::FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || f == nullptr) // hFile == INVALID_HANDLE_VALUE
    {
        return false;                                 // @0x4C2372 (returns handle -1)
    }

    const unsigned char* raw = reinterpret_cast<const unsigned char*>(this);

    // WriteFile(this,    0x28) && n==40 &&
    // WriteFile(this+40, 0x0C) && n==12  -> then WriteFile(this+52, 0x28)
    if (std::fwrite(raw + kChunk1Off, 1, kChunk1Size, f) == kChunk1Size &&
        std::fwrite(raw + kChunk2Off, 1, kChunk2Size, f) == kChunk2Size)
    {
        std::fwrite(raw + kChunk3Off, 1, kChunk3Size, f); // @0x4C2340
    }

    std::fclose(f);                                   // CloseHandle @0x4C2360
    return true;
}

// Options_LoadAndNormalize 0x4C2110
//   Options_LoadBin(this); Options_SaveBin(this); return 1;
// Net effect: loads (or defaults) then rewrites -> creates the file if missing.
bool GameOptions::LoadAndNormalize(const char* path)
{
    Load(path);   // @0x4C211A
    Save(path);   // @0x4C2122
    return true;  // @0x4C212C (mov eax, 1)
}

// Normalize() — field clamping (reconstructed from UI_OptionsWnd_OnClick
// 0x66D140 and its consumers). NOT part of the binary's config module;
// provided as a consistency utility.
namespace {
    inline int32_t Clamp(int32_t v, int32_t lo, int32_t hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
} // namespace

// Cfg_SaveLastServer 0x519C40
// CreateFileA(name, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
//             FILE_ATTRIBUTE_NORMAL(0x80), NULL)  -> fopen_s(..,"wb")
// WriteFile(this+61484, 4) -> fwrite(&lastServerIndex, 4, 1, f)
// CloseHandle -> fclose. The binary checks neither the write nor close result
// (unlike Options_LoadBin): reproduced as-is.
bool Cfg_SaveLastServer(int32_t lastServerIndex, const char* path)
{
    std::FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || f == nullptr) // hFile == INVALID_HANDLE_VALUE
    {
        return false; // @0x519cb9: no open -> no write
    }

    std::fwrite(&lastServerIndex, sizeof(lastServerIndex), 1, f); // WriteFile @0x519c87
    std::fclose(f);                                               // CloseHandle @0x519c95
    return true;
}

void GameOptions::Normalize()
{
    ShowSkillEffects  = Clamp(ShowSkillEffects,  0, 1);
    DisplayRangeTier  = Clamp(DisplayRangeTier,  1, 3);   // Game_GetTierRange
    Toggle2Reserved   = Clamp(Toggle2Reserved,   0, 1);
    // Reserved3 (idx3): reserved, left untouched.
    ShowHitMarkers    = Clamp(ShowHitMarkers,    0, 1);
    ShowNameplates    = Clamp(ShowNameplates,    0, 1);
    WeaponTrail       = Clamp(WeaponTrail,        0, 1);
    WeaponGlowLevel   = Clamp(WeaponGlowLevel,    0, 3);
    // Reserved8 (idx8): reserved, left untouched.
    BrightnessLevel   = Clamp(BrightnessLevel,    1, 9);   // UI clamp 1..9
    MusicVolume       = Clamp(MusicVolume,        0, 100); // slider 0..100
    SoundVolume       = Clamp(SoundVolume,        0, 100); // slider 0..100
    BgmEnabled        = Clamp(BgmEnabled,         0, 1);
    MorphUiMode       = Clamp(MorphUiMode,        1, 2);   // clamped decrement >=1
    GfxDetailShadows  = Clamp(GfxDetailShadows,   0, 1);
    FilterPartyChat   = Clamp(FilterPartyChat,    0, 1);
    FilterPartyInvite = Clamp(FilterPartyInvite,  0, 1);
    FilterAllyInvite  = Clamp(FilterAllyInvite,   0, 1);
    FilterPrompt19    = Clamp(FilterPrompt19,     0, 1);
    FilterPrompt20    = Clamp(FilterPrompt20,     0, 1);
    FilterPrompt10    = Clamp(FilterPrompt10,     0, 1);
    FilterPrompt14    = Clamp(FilterPrompt14,     0, 1);
    FilterWorldEntity = Clamp(FilterWorldEntity,  0, 1);
}

} // namespace ts2::config

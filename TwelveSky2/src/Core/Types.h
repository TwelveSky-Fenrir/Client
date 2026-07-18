// Core/Types.h — base types and constants for the TwelveSky2 client (faithful rewrite).
// Constants recovered from the disassembly (see Docs/TS2_CLIENT_SHELL.md).
#pragma once
#include <cstdint>

namespace ts2 {

// Win32 window class name (RegisterClass in WinMain 0x4609C0).
inline constexpr char kWindowClassName[] = "TwelveSky2";
inline constexpr char kWindowTitle[]     = "TwelveSky2";

// Reference resolution (flt_1669178 / flt_166917C).
inline constexpr int kRefWidth  = 1024;
inline constexpr int kRefHeight = 768;

// Game loop: fixed 30 FPS timestep (flt_815188 = 0.033333, App_FrameTick 0x4625D0).
inline constexpr double kFixedTimestep = 1.0 / 30.0;

// Asynchronous socket notification WSAAsyncSelect (WM_USER+1), routed by App_WndProc 0x461930.
inline constexpr unsigned kWM_Socket = 0x0400u + 1u; // 0x401

// Unloading of expired assets: purge every 60 s, TTL 300 s (App_FrameTick).
inline constexpr double kAssetPurgeIntervalSec = 60.0;
inline constexpr double kAssetTtlSec           = 300.0;

} // namespace ts2

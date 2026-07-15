// Core/Types.h — types et constantes de base du client TwelveSky2 (réécriture fidèle).
// Constantes relevées dans le désassemblage (voir Docs/TS2_CLIENT_SHELL.md).
#pragma once
#include <cstdint>

namespace ts2 {

// Nom de la classe fenêtre Win32 (RegisterClass dans WinMain 0x4609C0).
inline constexpr char kWindowClassName[] = "TwelveSky2";
inline constexpr char kWindowTitle[]     = "TwelveSky2";

// Résolution de référence (flt_1669178 / flt_166917C).
inline constexpr int kRefWidth  = 1024;
inline constexpr int kRefHeight = 768;

// Boucle de jeu : pas de temps fixe 30 FPS (flt_815188 = 0.033333, App_FrameTick 0x4625D0).
inline constexpr double kFixedTimestep = 1.0 / 30.0;

// Notification socket asynchrone WSAAsyncSelect (WM_USER+1), routée par App_WndProc 0x461930.
inline constexpr unsigned kWM_Socket = 0x0400u + 1u; // 0x401

// Déchargement des assets expirés : purge toutes les 60 s, TTL 300 s (App_FrameTick).
inline constexpr double kAssetPurgeIntervalSec = 60.0;
inline constexpr double kAssetTtlSec           = 300.0;

} // namespace ts2

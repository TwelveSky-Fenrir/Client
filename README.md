# TwelveSky2 — Client Source

A clean C++ reimplementation of the **TwelveSky2** game client, built from scratch using the original disassembly as the sole source of truth. The goal is a faithful, readable codebase that replicates the original behavior exactly — network protocol, file formats, gameplay formulas — without copying assembly.

## What this project is

This is a **full game client** for TwelveSky2, written in C++. It covers the entire flow from the intro AVI → server select → login → character select → entering the world → **in-game with HUD, windows, network, and combat**.

It is **not** a partial stub or a framework. The client is functional end-to-end:

- ~63,900 lines of code across **268 source files**
- 13 subsystems fully implemented (`App`, `Asset`, `Audio`, `Config`, `Core`, `Game`, `Gfx`, `Input`, `Net`, `Scene`, `Tools`, `UI`, `World`)
- Fixed 30 FPS game loop, single-instance window (`TwelveSky2`), ~32 managers initialized at startup
- Scene state machine: `Intro → ServerSelect → Login → CharSelect → InGame`

## Architecture

| Decision | Detail |
|---|---|
| **Platform** | Win32 32-bit, Direct3D 9, DirectInput 8, DirectSound 8 |
| **Bit-exact fidelity** | Network protocol, file formats (`.IMG` / `.npk` / MOTION / SOBJECT), gameplay formulas |
| **Code style** | Clean C++ — rendering, UI, and internals are written properly, not transcribed from assembly |
| **Build system** | Visual Studio solution (`TwelveSky2.sln`), toolset v143, Win32 platform, MBCS character set |

The 32-bit + D3D9 target is intentional: the client must load the original game assets and speak the original wire protocol byte-for-byte.

## Source layout

```
TwelveSky2/
  src/
	main.cpp          — WinMain entry point, ts2::App::Run
	App/              — App init, frame tick, WndProc, command-line parsing
	Core/             — Shared types, constants, logging
	Scene/            — Scene state machine (Intro, ServerSelect, Login, CharSelect, InGame)
	Asset/            — File format readers: .IMG, .npk, MOTION, Model, WorldChunk, Texture, Sound
	Net/              — TCP socket, XOR handshake, packet framing, opcode dispatch (0x0c-0xb6)
	Gfx/              — D3D9 device, GXD renderer, mesh/skinning, sprites, fonts, sky
	Game/             — Stats, combat, items, skills, quests, entities, auto-play
	UI/               — HUD and ~30 windows (inventory, skills, chat, party, guild, vendor, etc.)
	World/            — World map, world integration
	Audio/            — DirectSound, 3D sound, Ogg Vorbis decoder
	Input/            — DirectInput keyboard/mouse
	Config/           — INI and option file handling
	Tools/            — Self-test utilities
```

## Building

**Requirements:** Visual Studio 2022 or newer with the *Desktop development with C++* workload.

- Open `TwelveSky2.sln`
- Select configuration `Debug` or `Release`, platform **`Win32`**
- Press **F7** to build, **F5** to run

Recommended debug command argument: `/0/0/2/1024/768`
*(Project Properties → Debugging → Command Arguments)*

Command-line build:
```
msbuild TwelveSky2.sln /p:Configuration=Release /p:Platform=Win32
```

## Asset self-test

To validate the asset layer against real game files, place the game data under `TwelveSky2\GameData\` then run:
```
TwelveSky2.exe -assettest TwelveSky2\GameData
```
Opens a console and prints PASS/FAIL for each format. `GXDCompress.dll` must be next to the executable or on the PATH.

## Current state

| Subsystem | Status |
|---|---|
| App skeleton + game loop | Done |
| Asset layer (all formats) | Done |
| Network (socket, framing, opcode dispatch) | Done |
| GXD renderer (D3D9, mesh, skinning, fonts) | Done |
| Gameplay and UI (stats, combat, items, ~30 windows) | Done |
| World / terrain (WM/WJ/WG/SHADOW layers) | In progress |
| Ambient music in-game | In progress |
| Combat FX pools, asset purge | In progress |

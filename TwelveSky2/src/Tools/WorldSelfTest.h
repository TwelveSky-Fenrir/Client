// Tools/WorldSelfTest.h — `-worldtest` harness: SEE the character IN the 3D world
// (real zone terrain + self), without a server.
//
// Reproduces RECIPE A of the in-world recon (via SceneManager, the most faithful): loads a
// real zone (Z%03d.* — terrain .WG, collision .WM, objects .WO/.WP, atmosphere .ATM) via the
// EnterWorld flow (World_LoadZoneResource 0x4DCB60), injects a self at index 0 of
// game::g_World.players (appearance body+68/72/76/80), FORCES the InGame scene (what
// Pkt_EnterWorld 0x464160 normally does server-side), then renders + captures the back buffer as PNG.
// Same visual verification method as -charselecttest (cf. memory ts2-3d-character-visible).
//
// NO DRM key required (the SilverLining atmosphere is not linked). NO socket opened.
#pragma once

namespace ts2::tools {

// -worldtest [seconds] [zoneId] [selfX] [selfY] [selfZ]
//   seconds : window hold duration (0/default -> bounded by internal frames).
//   zoneId  : zone to load (default 1 = Z001, present on disk).
//   selfX/Y/Z : self position in the world (default 0,0,0; adjust to the terrain's bbox).
// width/height : default kRefWidth/kRefHeight.
int RunWorldSelfTest(int seconds, int zoneId, float selfX, float selfY, float selfZ,
                     int width, int height);

} // namespace ts2::tools

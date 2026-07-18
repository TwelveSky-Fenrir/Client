// Asset/AssetSelfTest.h — self-test of the Asset layer against real files.
// Invoked via "TwelveSky2.exe -assettest <GameDataPath>". Opens a console,
// exercises NpkArchive + ImgFile + Zlib + Xtea, and prints PASS/FAIL.
#pragma once
#include <string>

namespace ts2::asset {

int RunSelfTest(const std::string& gameDataDir);

} // namespace ts2::asset

// Asset/AssetSelfTest.h — auto-test de la couche Asset contre les vrais fichiers.
// Invoqué par « TwelveSky2.exe -assettest <cheminGameData> ». Ouvre une console,
// exerce NpkArchive + ImgFile + Zlib + Xtea, et affiche PASS/FAIL.
#pragma once
#include <string>

namespace ts2::asset {

int RunSelfTest(const std::string& gameDataDir);

} // namespace ts2::asset

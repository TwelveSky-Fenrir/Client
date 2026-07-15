# ClientSource — réécriture du client TwelveSky2

Réécriture C++ **propre et fidèle** du client `TwelveSky2.exe`, en prenant le désassemblage
(IDB `RE/TwelveSky2.exe.i64`) et les docs de `Docs/` comme **unique vérité**.

## Décisions d'architecture

| Choix | Décision |
|---|---|
| **Cible** | Fidèle **32-bit** (Win32), **Direct3D9** + DirectInput8 + DirectSound8 |
| **Fidélité bit-exacte** | Protocole réseau, formats de fichiers (`.IMG`/`.npk`/MOTION/SOBJECT), formules de gameplay |
| **C++ propre** | Rendu, UI, structure interne (pas de copie de l'assembleur) |
| **Build** | Solution **Visual Studio** (`TwelveSky2.sln`, toolset v143, `Win32`, jeu de caractères MBCS) |
| **Langue** | Commentaires et docs en français |

> Le 32-bit + D3D9 est délibéré : le client doit charger les **assets existants** (textures DXT
> dans `.IMG`, archives `.npk`, modèles/animations) et parler au **serveur d'origine** avec un
> protocole **octet pour octet** identique. Voir la note « serveur » dans `Docs/TS2_GAMEPLAY_LOGIC.md`.

## Structure

```
ClientSource/
  TwelveSky2.sln
  TwelveSky2/
    TwelveSky2.vcxproj(.filters)
    src/
      main.cpp                 # WinMain -> ts2::App::Run
      Core/   Types.h, Log.h   # constantes (pas fixe 30 FPS, WM_Socket 0x401…), log
      App/    App.*, GameConfig.h   # WinMain/App_Init/App_FrameTick/App_WndProc, parse cmdline
      Scene/  SceneManager.*    # machine de scènes (Intro→ServerSelect→Login→CharSelect→InGame)
```

Ces dossiers **existent et sont substantiellement implémentés** : `Asset/` (.IMG/.npk/MOTION/SOBJECT),
`Net/` (protocole, dispatch d'opcodes 0x0c–0xb6), `Gfx/` (device D3D9, GXD, mesh/skinning, sprites/police,
ciel), `Game/` (stats, combat, items, skills, quêtes, entités), `UI/` (HUD + ~30 fenêtres), plus
`World/`, `Audio/`, `Config/`, `Input/`, `Tools/`.

## Compiler & lancer

1. Ouvrir `ClientSource/TwelveSky2.sln` dans Visual Studio 2022 (charge de travail *Développement Desktop C++*).
2. Configuration `Debug` ou `Release`, plateforme **`Win32`**.
3. Générer (F7), lancer (F5). Argument de débogage recommandé : `/0/0/2/1024/768`
   (Propriétés du projet → Débogage → Arguments de la commande).

En ligne de commande : `msbuild TwelveSky2.sln /p:Configuration=Release /p:Platform=Win32`.

## État (client de bout en bout)

Bien au-delà d'un simple socle : le client va de l'intro AVI → server select → login → char select →
enter world → **monde en jeu avec HUD/fenêtres/réseau/combat**. ~63 700 lignes, 268 fichiers, **5 611
adresses IDA en commentaire** (chaque élément est ancré sur son adresse d'origine — voir la Règle #0 de
`Docs/TS2_CROSSCHECK_METHODOLOGY.md`). Socle fidèle : parse cmdline `/`-délimitée, instance unique,
fenêtre `TwelveSky2`, init des ~32 managers (`App_Init 0x461C20`), boucle **à pas fixe 30 FPS**, routage
`WndProc` (souris/clavier/`WM_ACTIVATEAPP`/message socket `0x401`), machine de scènes `cSceneMgr 0x517AF0`.

**Trous restants** (balisés en TODO dans le code) : terrain/sol de map (couches WM/WJ/WG/SHADOW), purge
d'assets, pools FX combat, quelques états non tracés, musique d'ambiance in-game. Campagne d'enrichissement
fidèle en cours : voir `Docs/TS2_CROSSCHECK_CAMPAIGN.md`.

## Assets & auto-test

Les vrais fichiers du jeu sont sous `TwelveSky2/GameData/`. Les specs de tous les formats
sont **prouvées contre ces fichiers** — voir `Docs/TS2_ASSET_FORMATS.md` et les parseurs de
référence `RE/asset_parsers/*.py`. La couche `src/Asset/` traduit fidèlement ces parseurs.

Auto-test runtime de la couche Asset (ouvre `GXDEffect.npk` + un `.IMG` réels) :
```
TwelveSky2.exe -assettest TwelveSky2\GameData
```
(ouvre une console et affiche PASS/FAIL ; `GXDCompress.dll` doit être à côté de l'exe ou dans le PATH).

## Feuille de route

1. **Socle + squelette app** ✅
2. **Assets** ✅ — couche `Asset/` complète et **validée contre les vrais fichiers** :
   `ByteReader`/`Xtea`/`Zlib` (via GXDCompress.dll), `NpkArchive`, `ImgFile`, `Motion`, `Model`
   (SObject/MObject), `WorldChunk`, `Sound`, `Texture` (TGA/DDS), `ZipArchive`. Lance
   `TwelveSky2.exe -assettest TwelveSky2\GameData` pour tout revalider.
3. **Réseau** ✅ — socket, handshake XOR, framing entrant/sortant, dispatch des opcodes (`Docs/TS2_PROTOCOL_SPEC.md`)
4. **Rendu GXD** ✅ — device D3D9, shaders, sprites/police, mesh/skinning (`Docs/TS2_GXD_ENGINE.md`)
5. **Gameplay & UI** ✅ — stats/combat/items (`Docs/TS2_GAMEPLAY_LOGIC.md`), HUD/fenêtres (`Docs/TS2_CLIENT_SHELL.md`)
6. **Enrichissement fidèle (cross-check IDA × VeryOldClient)** 🔄 — voir `Docs/TS2_CROSSCHECK_CAMPAIGN.md` :
   monde/terrain, crypto GXCW, audio, ciel, noms d'opcodes, layouts d'entités.

## Docs de référence (à la racine du dépôt)

`Docs/TS2_GXD_ENGINE.md`, `TS2_IMG_FORMAT.md`, `TS2_GAMEPLAY_LOGIC.md`, `TS2_CLIENT_SHELL.md`,
`TS2_DISPATCHERS.md`, `TS2_PROTOCOL_SPEC.md`, `TS2_SUBSYSTEM_MAP.md`.

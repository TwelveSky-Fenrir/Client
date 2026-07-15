// Scene/SceneAudit.h — harnais d'AUDIT TEMPORAIRE (mission « chaine UIManager::Init
// -> GameWindows -> SceneManager », 2026-07-14). PAS un composant du client : ne
// fait QUE piloter la VRAIE SceneManager (API publique uniquement, cf.
// Scene/SceneManager.h — Init/Change/OnKeyDown/Render) pour forcer l'entree en
// scene InGame SANS passer par le handshake reseau (aucun serveur de jeu
// disponible dans cet environnement de verification). Contrairement a un test
// isole a la Asset/AssetSelfTest.cpp, ce harnais NE PEUPLE JAMAIS lui-meme un
// UiContext : il ne fait qu'appeler le meme chemin de code que main.cpp/App.cpp
// (SceneManager::Change -> GameWindows::Init -> UIManager::Instance().Init),
// donc une preuve visuelle ici est une preuve du chemin REEL, pas d'un mock.
//
// NOTE : la verification effective de cette mission a finalement reutilise
// Tools/UiWindowSelfTest.h (outil equivalent deja present, etendu avec l'option
// "options"). Ce fichier est conserve (entrees vcxproj deja generees) mais n'est
// plus le chemin appele depuis main.cpp — a retirer avec Tools/UiWindowSelfTest.*
// une fois l'ensemble des audits temporaires clos.
#pragma once
#include <string>

namespace ts2 {

// Cree une fenetre + device D3D9 reels, initialise SceneManager, force
// Change(Scene::InGame), ouvre la fenetre Options (touche 'O' -> HandleHotkey),
// rend quelques frames puis laisse la fenetre a l'ecran `holdSeconds` secondes
// (pour capture d'ecran externe) avant de fermer proprement.
int RunSceneAudit(const std::string& gameDataDir, int holdSeconds);

} // namespace ts2

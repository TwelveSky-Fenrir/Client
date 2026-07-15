// Tools/UiWindowSelfTest.h — outil TEMPORAIRE de verification empirique (audit device
// reel VendorShopWindow/WarehouseWindow/InventoryWindow, mission 2026-07-14).
//
// A SUPPRIMER apres verification : ne fait PAS partie du client livre. Objectif :
// prouver, via une VRAIE fenetre visible + un VRAI device D3D9 + le VRAI chemin de
// production (SceneManager::Init/Change -> GameWindows::Init -> UIManager::Init),
// que ces 3 fenetres affichent de vraies textures et pas un repli permanent. N'invente
// AUCUN objet UiContext/Dialog parallele : reutilise tel quel SceneManager (Scene/
// SceneManager.h, NON MODIFIE) avec un device/fenetre reels, en forcant Scene::InGame
// directement — EXACTEMENT le meme etat que le repli "EnterWorld: flux en echec ->
// bascule InGame de secours" deja present dans SceneManager::Update (Scene::EnterWorld),
// donc un etat atteignable en production, pas un artifice de test isole.
#pragma once

namespace ts2::tools {

// `which` = "vendor" | "warehouse" | "inventory" | "options" (fenetre a ouvrir via le
// hotkey reel ; "options" ajoute au perimetre par l'audit chaine UIManager::Init ->
// GameWindows -> SceneManager, 2026-07-14, pour prouver le fond PanelSkin des 13
// fenetres enregistrees par GameWindows::Init, pas seulement Vendor/Warehouse/Inventory).
// `seconds` = duree reelle (secondes) pendant laquelle la fenetre reste affichee apres
// ouverture, pour laisser le temps a une capture d'ecran externe.
// `width`/`height` = resolution reelle de la fenetre de test (AJOUTE audit systeme de
// coordonnees 2026-07-14) : permet de rejouer EXACTEMENT le meme chemin de production
// (SceneManager::Init -> GameWindows::Init -> UIManager::Init) a une resolution NON
// 1024x768, pour verifier empiriquement le repositionnement (ou son absence) des
// fenetres UI. Defaut 1024x768 (kRefWidth/kRefHeight) si <=0.
int RunUiWindowSelfTest(const char* which, int seconds, int width = 0, int height = 0);

} // namespace ts2::tools

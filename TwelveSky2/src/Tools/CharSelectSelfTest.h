// Tools/CharSelectSelfTest.h — harnais de PREVIEW de l'ecran CharSelect / CreateChar.
//
// RAISON D'ETRE (diagnostic Passe 5) : dans le flux normal, CharSelect (scene 4) est
// INATTEIGNABLE sans un serveur de login/statut qui repond — la porte de population de
// ServerSelect (game::OnServerClicked, currentPopulation>=0 && <max) avale le clic serveur
// hors ligne, et il n'existe aucun repli/mock. Tout le pipeline de rendu (2D atlas + apercu
// 3D personnage via CharPreview3D/ModelCache/MotionCache/MeshRenderer) est pourtant COMPLET et
// fonctionnel — il n'est simplement jamais exerce. Ce harnais force Scene::CharSelect, injecte
// des fiches perso par defaut (net::g_CharRecords) pour peupler la LISTE (+ apercu 3D), et
// route souris/clavier pour tester la CREATION (bouton CRÉER -> formulaire + apercu 3D live).
//
// Analogue a Tools/UiWindowSelfTest (qui force Scene::InGame). Outil de verification, active
// par la ligne de commande `-charselecttest [seconds]` (seconds<=0 => jusqu'a fermeture).
#pragma once

namespace ts2::tools {

// Ouvre une fenetre, initialise renderer/scene, force CharSelect avec 2 persos par defaut,
// et pompe une boucle interactive (Update+Render 30 FPS) pendant `seconds` (<=0 = jusqu'a la
// fermeture de la fenetre). Renvoie 0 en succes. width/height <=0 => kRefWidth/kRefHeight.
int RunCharSelectSelfTest(int seconds, int width, int height);

} // namespace ts2::tools

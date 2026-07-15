// Config/GameOptions.h — sous-système CONFIG/OPTIONS du client TwelveSky2.
//
// Réécriture C++ PROPRE mais BYTE-EXACTE au désassemblage de TwelveSky2.exe.
// Le bloc d'options persistant est un tableau de 23 entiers 32 bits (92 octets)
// sérialisé tel quel dans le fichier « G02_GINFO\001.BIN ».
//
// Correspondance fonction ↔ adresse d'origine (idaTs2) :
//   Options_SetDefaults        0x4C2020  -> GameOptions::SetDefaults()
//   Options_LoadAndNormalize   0x4C2110  -> GameOptions::LoadAndNormalize()
//   Options_LoadBin            0x4C2140  -> GameOptions::Load()
//   Options_Save_STUB          0x4C2130  -> GameOptions::SaveStub()  (App_Shutdown, no-op)
//   Options_SaveBin            0x4C2280  -> GameOptions::Save()
//
// Bloc global d'origine : g_Options @ 0x84DEC0 (base du struct, 23 champs).
// Chaque champ est aussi exporté comme global g_Opt_* dans l'IDB ; les noms de
// membres ci-dessous reprennent ces symboles (voir la colonne « idx / EA »).
//
// Producteur/normalisation runtime des champs : UI_OptionsWnd_OnClick 0x66D140
// (cases à cocher + boutons +/- avec bornage), consommateurs éparpillés
// (Fx_Attach*, Char_Draw*, Snd3D_*, Game_GetTierRange, Net_On*Invite*, …).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::config {

// Chemin du fichier de persistance (CreateFileA "G02_GINFO\\001.BIN", aG02Ginfo001Bin
// @ 0x7A6FE0). Relatif au répertoire courant du client, comme dans le binaire.
inline constexpr char kOptionsFilePath[] = "G02_GINFO\\001.BIN";

// Chemin du fichier « dernier serveur/groupe sélectionné » (CreateFileA
// "G02_GINFO\\010.BIN", aG02Ginfo010Bin @ 0x7A9750). Écrit par Cfg_SaveLastServer
// 0x519C40, appelée depuis Scene_ServerSelectOnMouseDown 0x519780 (EA 0x51990D) à
// chaque clic de groupe/serveur sur l'écran ServerSelect.
//
// ⚠️ Cfg_LoadLastServer 0x519B50 (la LECTURE de ce fichier) a 0 xref dans tout le
// binaire — code mort confirmé, JAMAIS appelé (ni au démarrage, ni ailleurs). Ce
// fichier n'est donc JAMAIS relu par TwelveSky2.exe : seule l'ÉCRITURE est un
// comportement réel. Cable UNIQUEMENT Cfg_SaveLastServer ci-dessous ; n'ajoute PAS
// de lecture au démarrage (ce serait infidèle au binaire).
inline constexpr char kLastServerFilePath[] = "G02_GINFO\\010.BIN";

// Taille totale du bloc persistant : 23 × int32 = 92 octets (0x5C).
inline constexpr size_t kOptionsByteSize = 92;

// Découpage EXACT de la sérialisation (3 accès disque successifs dans le binaire) :
//   bloc 1 : offset 0x00, 40 o (0x28) -> champs idx 0..9
//   bloc 2 : offset 0x28, 12 o (0x0C) -> champs idx 10..12
//   bloc 3 : offset 0x34, 40 o (0x28) -> champs idx 13..22
// Les 3 blocs sont contigus (92 o), mais Load/Save reproduisent les 3 accès et
// leurs contrôles de taille pour rester fidèles à Options_LoadBin/Options_SaveBin.
inline constexpr size_t kChunk1Off = 0;   inline constexpr size_t kChunk1Size = 40; // 0x28
inline constexpr size_t kChunk2Off = 40;  inline constexpr size_t kChunk2Size = 12; // 0x0C
inline constexpr size_t kChunk3Off = 52;  inline constexpr size_t kChunk3Size = 40; // 0x28

// =====================================================================
// Bloc d'options persistant — layout BYTE-EXACT (23 × int32, 92 octets).
// idx = index de dword ; EA = adresse du global g_Opt_* correspondant ;
// def = valeur par défaut (Options_SetDefaults) ; [borne] = plage imposée
// par l'UI (UI_OptionsWnd_OnClick) / attendue par les consommateurs.
// =====================================================================
#pragma pack(push, 4)
struct GameOptions {
    // idx0  0x00  g_Options 0x84DEC0           def 1  [0,1]
    //   Interrupteur maître des effets visuels de skill/arme (gaté par
    //   Fx_AttachHitSpark/HitBurst/WeaponGlow*/SkillGlow*/SkillAura*, cmp ==0).
    int32_t ShowSkillEffects;
    // idx1  0x04  g_Opt_DisplayRangeTier 0x84DEC4  def 2  [1,3]
    //   Palier de distance d'affichage -> Game_GetTierRange 1000/2000/3000.
    int32_t DisplayRangeTier;
    // idx2  0x08  g_Opt_Toggle2_reserved 0x84DEC8  def 1  [0,1]
    //   Bascule éditée par l'UI mais sans consommateur runtime connu.
    int32_t Toggle2Reserved;
    // idx3  0x0C  g_Opt_Reserved3 0x84DECC     def 1  (aucune xref — réservé)
    int32_t Reserved3;
    // idx4  0x10  g_Opt_ShowHitMarkers 0x84DED0  def 1  [0,1]
    //   Marqueurs de coups/dégâts (Char_DrawNameplate, Fx_MeleeSwingDrawMarker).
    int32_t ShowHitMarkers;
    // idx5  0x14  g_Opt_ShowNameplates 0x84DED4  def 1  [0,1]
    //   Plaques de nom (Char_DrawNameplate).
    int32_t ShowNameplates;
    // idx6  0x18  g_Opt_WeaponTrail 0x84DED8    def 1  [0,1]
    //   Traînée d'arme (Char_DrawWeaponTrailEffect).
    int32_t WeaponTrail;
    // idx7  0x1C  g_Opt_WeaponGlowLevel 0x84DEDC  def 0  [0,3]
    //   Niveau d'effet lumineux d'arme 0/1/2/3 (Char_DrawWeaponEffect*).
    int32_t WeaponGlowLevel;
    // idx8  0x20  g_Opt_Reserved8 0x84DEE0      def 1  (aucune xref — réservé)
    int32_t Reserved8;
    // idx9  0x24  g_Opt_BrightnessLevel 0x84DEE4  def 7  [1,9]
    //   Luminosité/gamma (Scene_InGameRender).
    int32_t BrightnessLevel;
    // ---- fin bloc 1 (offset 0x28) ----
    // idx10 0x28  g_Opt_MusicVolume 0x84DEE8    def 100  [0,100]
    //   Volume BGM (Player_UpdateLocalAnim, UI_OptionsWnd_OnMouseDown : fild).
    int32_t MusicVolume;
    // idx11 0x2C  g_Opt_SoundVolume 0x84DEEC    def 100  [0,100]
    //   Volume des effets sonores (Snd3D_PlayScaledVolume/FullVolume/Positional).
    int32_t SoundVolume;
    // idx12 0x30  g_BgmEnabled 0x84DEF0         def 1  [0,1]
    //   Interrupteur musique de fond (UI_OptionsWnd_OnClick : Snd_Stop/Snd_Play3D).
    int32_t BgmEnabled;
    // ---- fin bloc 2 (offset 0x34) ----
    // idx13 0x34  g_MorphUiMode 0x84DEF4        def 2  [1,2]
    //   Sélecteur de hotkeys morph (F1..F10 si ==1). Décrément borné à >=1
    //   (Game_OnHotkey, Skill_IsHotkeyPressed, Game_UseFirstReadySkill).
    int32_t MorphUiMode;
    // idx14 0x38  g_Opt_GfxDetailShadows 0x84DEF8  def 1  [0,1]
    //   Détail graphique : ombres/reflets (Char_DrawShadow/Reflection, aussi
    //   toggle sur l'écran de login Scene_Login*).
    int32_t GfxDetailShadows;
    // idx15 0x3C  g_Opt_FilterPartyChat 0x84DEFC  def 1  [0,1]
    //   Filtre chat de groupe (Pkt_PartyChatOrInvite).
    int32_t FilterPartyChat;
    // idx16 0x40  g_Opt_FilterPartyInvite 0x84DF00  def 1  [0,1]
    //   Filtre invitations de groupe (Pkt_PartyInvitePrompt).
    int32_t FilterPartyInvite;
    // idx17 0x44  g_Opt_FilterAllyInvite 0x84DF04  def 1  [0,1]
    //   Filtre invitations d'alliance/guilde (Pkt_AllyInvitePrompt).
    int32_t FilterAllyInvite;
    // idx18 0x48  g_Opt_FilterPrompt19 0x84DF08  def 1  [0,1]
    //   Filtre prompt confirmation dlg19 (Net_OnConfirmPromptOpen_Dlg19).
    int32_t FilterPrompt19;
    // idx19 0x4C  g_Opt_FilterPrompt20 0x84DF0C  def 1  [0,1]
    //   Filtre prompt confirmation dlg20 (Net_OnConfirmPromptOpen_Dlg20).
    int32_t FilterPrompt20;
    // idx20 0x50  g_Opt_FilterPrompt10 0x84DF10  def 1  [0,1]
    //   Filtre prompt confirmation dlg10 (Net_OnConfirmPromptOpen_Dlg10).
    int32_t FilterPrompt10;
    // idx21 0x54  g_Opt_FilterPrompt14 0x84DF14  def 1  [0,1]
    //   Filtre prompt confirmation dlg14 (Net_OnConfirmPromptOpen_Dlg14).
    int32_t FilterPrompt14;
    // idx22 0x58  g_Opt_FilterWorldEntity 0x84DF18  def 1  [0,1]
    //   Filtre messages « entité monde » (Net_OnWorldEntityDispatch, cmp ==1).
    int32_t FilterWorldEntity;
    // ---- fin bloc 3 (offset 0x5C = 92) ----

    // -- API (fidèle aux fonctions du binaire) --

    // Options_SetDefaults 0x4C2020 : réinitialise les 23 champs à leurs défauts.
    void SetDefaults();

    // Options_LoadBin 0x4C2140 : lit le fichier en 3 blocs (40/12/40) avec
    // contrôle de taille exact. En cas d'absence/erreur/lecture partielle,
    // rebascule sur SetDefaults() (SANS écrire le fichier). Renvoie true si les
    // valeurs proviennent du fichier, false si les défauts ont été appliqués.
    bool Load(const char* path = kOptionsFilePath);

    // Options_SaveBin 0x4C2280 : écrit le fichier en 3 blocs (40/12/40),
    // CREATE_ALWAYS. Renvoie true si les 3 blocs (52 o garantis + tentative du
    // 3e) ont été émis, false si l'ouverture a échoué. (Le binaire n'échoue
    // jamais explicitement sur le 3e bloc ; on reproduit ce comportement.)
    bool Save(const char* path = kOptionsFilePath) const;

    // Options_LoadAndNormalize 0x4C2110 : Load() puis Save(). L'effet net de
    // « normalisation » du binaire est de MATÉRIALISER le fichier sur disque
    // (création avec les défauts s'il manquait). Renvoie toujours true (comme
    // l'original qui renvoie 1).
    bool LoadAndNormalize(const char* path = kOptionsFilePath);

    // Options_Save_STUB 0x4C2130 : stub vide appelé par App_Shutdown 0x462480.
    // Présent pour la fidélité du graphe d'appels ; ne fait rien.
    void SaveStub() {}

    // Bornage (« normalisation » au sens plage de valeurs). N'existe PAS tel
    // quel dans le module config du binaire : le clamp est réalisé champ par
    // champ dans UI_OptionsWnd_OnClick 0x66D140. Reconstitué ici pour offrir un
    // point d'entrée unique. Ne touche pas les champs réservés (idx3/idx8).
    void Normalize();
};
#pragma pack(pop)

// Vérifications de layout byte-exact.
static_assert(sizeof(GameOptions) == kOptionsByteSize, "GameOptions doit faire 92 octets");
static_assert(offsetof(GameOptions, ShowSkillEffects)  == 0x00, "layout idx0");
static_assert(offsetof(GameOptions, DisplayRangeTier)  == 0x04, "layout idx1");
static_assert(offsetof(GameOptions, MusicVolume)       == 0x28, "layout idx10");
static_assert(offsetof(GameOptions, SoundVolume)       == 0x2C, "layout idx11");
static_assert(offsetof(GameOptions, BgmEnabled)        == 0x30, "layout idx12");
static_assert(offsetof(GameOptions, MorphUiMode)       == 0x34, "layout idx13");
static_assert(offsetof(GameOptions, GfxDetailShadows)  == 0x38, "layout idx14");
static_assert(offsetof(GameOptions, FilterWorldEntity) == 0x58, "layout idx22");

// Instance globale unique — équivalent de g_Options @ 0x84DEC0.
extern GameOptions g_Options;

// =====================================================================
// Cfg_SaveLastServer 0x519C40 — persistance du dernier groupe/serveur choisi
// sur l'écran ServerSelect (G02_GINFO\010.BIN, kLastServerFilePath).
//
// Fonction LIBRE (PAS une méthode de GameOptions) : dans le binaire, `this`
// est l'objet scène ServerSelect (g_SceneMgr, champ this+61484 / this[15371]),
// une structure totalement différente de g_Options @ 0x84DEC0 — les deux
// fichiers G02_GINFO\001.BIN et 010.BIN n'ont rien en commun hormis le
// dossier. Byte-exact : CREATE_ALWAYS, écrit UN SEUL int32 (4 octets), pas
// d'entête/bloc. Retourne false si l'ouverture échoue (comme le binaire :
// hFile == INVALID_HANDLE_VALUE -> aucune écriture), true sinon (le binaire
// ne vérifie ni WriteFile ni CloseHandle).
//
// NE PAS ajouter de pendant "Load"/lecture au démarrage : Cfg_LoadLastServer
// 0x519B50 est du code mort avéré (0 xref) dans le binaire d'origine — voir
// le commentaire de kLastServerFilePath ci-dessus.
bool Cfg_SaveLastServer(int32_t lastServerIndex, const char* path = kLastServerFilePath);

} // namespace ts2::config

// Gfx/IconTextureCache.h — cache GPU PARTAGÉ d'icônes .IMG, clé = chemin de fichier.
//
// PROBLÈME résolu (mission « audit mémoire des caches de texture/modèle »,
// 2026-07-14) : UI/InventoryWindow.h, UI/WarehouseWindow.h, UI/EnchantWindow.h et
// UI/VendorShopWindow.h possédaient CHACUNE un `std::unordered_map<uint32_t,
// gfx::GpuTexture> iconCache_` INDÉPENDANT (même pattern GetIconTex copié-collé
// 4 fois, y compris la fonction ResolveItemIconPath quasi identique dans 3 des
// 4 .cpp). Ces 4 fenêtres sont des membres PERSISTANTS de UI/GameWindows.h :
// construites une seule fois au Init() de la scène InGame et jamais détruites
// tant que la session dure — Open()/Close() ne fait que les masquer/afficher.
// Constat vérifié par lecture directe des 4 GetIconTex (tous résolvent
// "G03_GDATA\D01_GIMAGE2D\002\002_%05u.IMG" via le même champ ITEM_INFO::iconId) :
// dès qu'un joueur ouvre son sac PUIS l'entrepôt (déplacer un objet), ou le sac
// PUIS une boutique (comparer avant achat), ou le sac PUIS l'enchantement (objet
// déjà équipé), la MÊME icône .IMG était décodée et uploadée en VRAM comme
// AUTANT d'IDirect3DTexture9 DISTINCTES que de fenêtres l'ayant affichée — sans
// jamais être libérée avant la fin de session (aucune éviction, aucun Clear()
// périodique). Sur une session longue où le joueur finit par croiser dans ces
// 4 fenêtres une bonne partie du pool d'icônes (G03_GDATA/D01_GIMAGE2D/002,
// ~3100 fichiers réels, cf. UI/PanelSkin.h §Étape 1), la duplication ×4
// représente un gaspillage VRAM cumulé non négligeable (chaque doublon coûte
// aussi un décodage D3DX + une allocation D3DPOOL_MANAGED redondants), pour un
// gain nul (les 4 fenêtres affichent des pixels rigoureusement identiques).
//
// Ce cache remplace les 4 unordered_map<itemId,...> par UNE SEULE instance
// partagée (possédée par UI::GameWindows, cf. GameWindows.h/.cpp), clé par
// CHEMIN DE FICHIER (et non itemId) : bénéfice croisé supplémentaire, deux
// itemId différents partageant le même ITEM_INFO::iconId (fréquent : variantes
// de couleur/qualité d'un même objet) ne créent plus qu'UNE texture GPU, y
// compris DÉJÀ à l'intérieur d'une seule fenêtre (l'ancien cache par itemId ne
// dédupliquait pas ce cas non plus).
//
// NB périmètre : Gfx/ModelCache.h (modèles .SOBJECT skinnés du monde 3D) était
// DÉJÀ une instance UNIQUE partagée (un seul ModelCache dans Scene/WorldRenderer,
// clé par stem de fichier, utilisé aussi bien pour le corps du joueur que pour
// les items/monstres) — aucune duplication à corriger de ce côté, cf. audit.
#pragma once
#include "Gfx/GpuTexture.h"
#include <d3d9.h>
#include <string>
#include <unordered_map>

namespace ts2::gfx {

class IconTextureCache {
public:
    IconTextureCache() = default;

    IconTextureCache(const IconTextureCache&)            = delete;
    IconTextureCache& operator=(const IconTextureCache&) = delete;

    // Lazy-load : renvoie la texture résidente pour `path` (chemin relatif GameData,
    // ex. "G03_GDATA\D01_GIMAGE2D\002\002_00042.IMG"), la chargeant/uploadant au 1er
    // accès pour ce chemin PRÉCIS (quel que soit l'itemId/la fenêtre appelante).
    // nullptr si `path` vide, `dev` nul, fichier introuvable, ou décodage/upload GPU
    // en échec — un échec est MIS EN CACHE (ne retente pas de charger le même chemin
    // manquant à chaque appel), même politique que les GetIconTex d'origine qu'il
    // remplace.
    GpuTexture* GetOrLoad(IDirect3DDevice9* dev, const std::string& path);

    // Purge (libère immédiatement toutes les textures GPU résidentes).
    void Clear() { entries_.clear(); }

    size_t Resident() const { return entries_.size(); }

private:
    std::unordered_map<std::string, GpuTexture> entries_;
};

} // namespace ts2::gfx

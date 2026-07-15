// UI/EnchantWindow.h — fenêtre « Enchantement » : sélection d'un des 13 slots
// d'équipement du joueur local et prévisualisation du delta de stat qu'apporterait
// le PROCHAIN niveau d'enchantement de l'objet équipé dans ce slot.
//
// S'appuie sur :
//   - Game/GameState.h  : game::g_World.self.equip[13] (EquipSlot{itemId, socket, ..}),
//     socket = mot bit-packé (octet3 = niveau d'enchant courant 1..59, cf.
//     Item_GetAttribByte3, Game/ItemSystem.h).
//   - Game/ItemSystem.h : Item_GetEnchantStatDelta(itemClass, slot, socketWord, key)
//     0x553D50 — grande table (classe, slot, niveau, clé) → delta signé.
//
// Icônes = icône .IMG réelle de l'objet équipé (résolue par itemId, même pattern que
// UI/InventoryWindow.cpp), avec repli sur le carré coloré générique par index de slot
// (SlotColor) si la texture ne charge pas ou si le slot est vide.
//
// -----------------------------------------------------------------------------
// HYPOTHÈSE DE MAPPING classe/slot (DOCUMENTÉE, pas certifiée par le désassemblage
// pour cette fenêtre précise — Item_GetEnchantStatDelta est directement appelable
// mais son 1er paramètre `itemClass` est normalement résolu par
// Item_ClassifyRecord/Item_ClassifyById (0x5509A0/0x550800), qui sont INTERNES et
// NON EXPORTÉS (statiques dans Game/StatFormulas.cpp — cf. UI/ItemTooltip.h qui
// documente la même limite). On reproduit donc ICI, en local, le sous-ensemble de
// mapping EXPLICITEMENT décrit dans le commentaire de Item_GetEnchantStatDelta
// (Game/ItemSystem.h lignes 155-161) :
//   - slot == 1                      -> itemClass 8 (« cas spécial », UNIQUEMENT ce slot)
//   - slot ∈ {0,2,3,4,5}             -> itemClass 4 (armure)
//   - slot == 7                      -> itemClass 1 (arme)
//   - autres slots (6,8,9,10,11,12)  -> non couverts par la table d'enchant connue
//     (la boucle d'origine des moteurs de stats saute explicitement l'indice 8 ;
//     les slots de la page 2 — bijoux/carquois, cf. Docs TS2_GAMEPLAY_LOGIC.md
//     « onglet 2 = slots 9..12 » — n'ont pas d'entrée confirmée dans cette table).
// Cette fenêtre AFFICHE ce statut (« non pris en charge ») plutôt que d'inventer
// une classe pour ces slots.
// -----------------------------------------------------------------------------
#pragma once
#include "UI/UIManager.h"
#include "Game/ItemSystem.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <unordered_map>

namespace ts2::ui {

// Nombre de slots d'équipement (SelfState::equip).
inline constexpr int kEnchantSlotCount = 13;

// Clés de stat exposées par Item_GetEnchantStatDelta (Game/ItemSystem.h §Delta enchant).
// Valeurs en CENTIÈMES pour 10/20/30/40 (converties /100.0 à l'affichage), en UNITÉS
// pour 50/60/70/80/90 (fidèle au commentaire d'origine).
enum class EnchantStatKey : int {
    AtkExt    = 10,
    AtkInt    = 20,
    DefExt    = 30,
    DefInt    = 40,
    MaxHp     = 50,
    MaxMp     = 60,
    Precision = 70,
    Evasion   = 80,
    Rating    = 90,
};
inline constexpr int kEnchantStatKeyCount = 9;
inline constexpr EnchantStatKey kEnchantStatKeys[kEnchantStatKeyCount] = {
    EnchantStatKey::AtkExt, EnchantStatKey::AtkInt, EnchantStatKey::DefExt,
    EnchantStatKey::DefInt, EnchantStatKey::MaxHp,  EnchantStatKey::MaxMp,
    EnchantStatKey::Precision, EnchantStatKey::Evasion, EnchantStatKey::Rating,
};

// -----------------------------------------------------------------------------
// EnchantWindow — dialogue modal léger (fermable), non draggable. Lit
// game::g_World.self.equip[] à chaque Render (aucun état d'objet dupliqué ;
// seul l'index de slot SÉLECTIONNÉ est un état propre à la fenêtre).
class EnchantWindow : public Dialog {
public:
    void Open() override;                       // centre + réarme les latches + reset sélection
    // Close() héritée telle quelle (bOpen_=false).

    bool OnMouseDown(int x, int y) override;     // arme close/slot/enchanter si survolés
    bool OnClick(int x, int y) override;         // valide close/slot/enchanter si relâché dessus
    bool OnKey(int vk) override;                 // Échap -> ferme

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Index du slot actuellement sélectionné (-1 = aucun). Exposé pour tests/outils.
    int SelectedSlot() const { return selectedSlot_; }

    // Cache GPU d'icônes PARTAGÉ (mutualisation mémoire, cf. Gfx/IconTextureCache.h) :
    // injecté par UI/GameWindows.cpp, même instance que InventoryWindow/WarehouseWindow/
    // VendorShopWindow. nullptr (repli) => ownIconCache_ locale (jamais le cas en
    // production, cf. InventoryWindow::SetIconCache).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

private:
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };

    struct Layout {
        Rect box;                          // panneau complet
        Rect titleBar;                     // bandeau de titre
        Rect closeBtn;                     // bouton fermeture (coin haut-droit)
        Rect slot[kEnchantSlotCount];       // 13 carrés d'équipement
        Rect panel;                        // panneau d'info/prévisualisation (droite)
        Rect enchantBtn;                   // bouton "Enchanter" (bas)
    };
    void ComputeLayout(int screenW, int screenH, Layout& L) const;

    // Mapping DOCUMENTÉ en tête de fichier — renvoie -1 si le slot n'est pas couvert
    // par la table Item_GetEnchantStatDelta.
    static int GuessItemClass(int slot);
    static const char* ItemClassLabel(int itemClass);

    // Libellé + couleur générique d'un slot (indépendant de son contenu).
    static const char* SlotLabel(int slot);
    static D3DCOLOR     SlotColor(int slot);

    // Delta prévisualisé pour la clé `key`, au niveau d'enchant `previewLevel`
    // (remplace l'octet3 du mot socket de l'objet équipé dans `slot`). Renvoie 0
    // si le slot est vide ou hors table (itemClass == -1).
    int PreviewDelta(int slot, int previewLevel, EnchantStatKey key) const;

    // --- Icône d'objet (même pattern que InventoryWindow/WarehouseWindow : résolveur +
    // cache paresseux + repli sur SlotColor si la texture ne charge pas). Device pris en
    // paramètre (pas de device_ membre : Dialog n'a pas d'Init() dédié).
    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;

    // Latches "clic-enfoncé -> relâché dessus" (pattern CharacterStatsWindow/MsgBoxDialog).
    bool closeArmed_    = false;
    bool enchantArmed_  = false;
    bool slotArmed_[kEnchantSlotCount] = {};

    int selectedSlot_ = -1; // -1 = aucune sélection

    // Dims écran mémorisées au dernier Render, pour aligner le hit-test (routé
    // entre deux frames) sur la géométrie effectivement dessinée.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

} // namespace ts2::ui

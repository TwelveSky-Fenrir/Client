// Game/StringTables.h — tables de chaînes/localisation du client (G01_GFONT).
//
// Reproduit fidèlement 5 chargeurs appelés en séquence par App_Init 0x461C20
// (managers [Error::mBADWORD.Init()] .. [Error::mFONTCOLOR.Init()]) :
//
//   G01_GFONT\001.DAT -> mBADWORD    Dict001_Load          0x4C1170  (BannedWordDict)
//   G01_GFONT\002.DAT -> mGAMENOTICE Tips002_Load           0x4C1630  (TipsTable)
//   G01_GFONT\003.DAT -> mZONENAME   StrTable003_Load        0x4C18E0  (StrTable003)
//   G01_GFONT\005.DAT -> mMESSAGE    StrTable005_Load        0x4C1B20  (StrTable005)
//   (code en dur)     -> mFONTCOLOR  ColorTable_InitPalette   0x4C1D60  (ColorPalette)
//
// Instances globales identifiées dans App_Init (this = &unk_XXXXXXXX) :
//   mBADWORD    unk_8CE2D8   mGAMENOTICE unk_8B5840   mZONENAME unk_84A6A8
//   mMESSAGE    unk_84DFF8   mFONTCOLOR  unk_84DF20
//
// Deux familles de FORMAT DISQUE bien distinctes (confirmées par lecture du
// désassemblage — cf. Docs/TS2_ASSET_FORMATS.md §2.11) :
//
//   FORMAT 2 « quote-delimited » (002/003/005.DAT) : fichier ASCII BRUT, PAS
//   compressé. Une machine à états ne réagit qu'à l'octet '"' (0x22) : tout
//   le reste hors guillemets (préfixe numérique "NNN.", CRLF, espaces) est
//   ignoré. Chaque paire de guillemets consécutive produit UN enregistrement.
//   Layout mémoire du manager : u32 count PUIS count enregistrements de
//   STRIDE octets chacun, NUL-terminés dans leur slot.
//     002.DAT (mGAMENOTICE) : STRIDE=101, CAP=1000
//     003.DAT (mZONENAME)   : STRIDE=41,  CAP=350
//     005.DAT (mMESSAGE)    : STRIDE=106, CAP=4000  <- table cible pour game::Str(id)
//   Chemin sélectionné par g_UseTRVariant (0x1669190) : TR => "G01_GFONT\TR\00N.DAT".
//
//   FORMAT 3 « dictionnaire compressé » (001.DAT, mBADWORD SEUL) : MÊME
//   enveloppe que les .IMG -> [u32 rawSize][u32 packedSize][flux zlib],
//   décodée par Asset_DecompressImg 0x53F5E0 (réutilisé ici via asset::ImgFile,
//   qui applique exactement cette même enveloppe). Décompressé = mots CP949
//   (coréen, NON localisés même en build EU) séparés CRLF, records de 51 o.
//   Gardes d'intégrité DURES de l'original : rawSize décompressé DOIT valoir
//   11572 ET le nombre final d'entrées DOIT valoir 1432 (sinon échec total).
//   Pas de variante TR (g_UseTRVariant non testé dans Dict001_Load).
//
//   mFONTCOLOR (ColorTable_InitPalette 0x4C1D60) N'EST PAS un chargeur de
//   fichier : la palette est codée EN DUR dans la fonction (45 couleurs ARGB
//   0xAARRGGBB signées + 8 index de "canaux de chat"). Confirmé croisement
//   d'adresses : unk_84DF20 + 184 o (= this+46 dwords) == 0x84DFD8, qui est
//   exactement la ChannelColorTable documentée dans TS2_CLIENT_SHELL.md
//   (system/whisper/party/shout/guild/faction/trade/gm) : ce ne sont PAS des
//   couleurs directes mais des INDEX (1-based) dans le tableau de 45 couleurs.
//
// NOTE IMPORTANTE (fidélité vs sûreté mémoire) : dans les 3 chargeurs quote-
// delimited ET dans Dict001_Load, la garde de dépassement de slot est vérifiée
// AVANT l'écriture d'un caractère de contenu (`if (col == STRIDE) return false`)
// mais PAS avant l'écriture du terminateur NUL de fin d'enregistrement — un
// enregistrement qui remplit exactement les STRIDE octets déclenche, dans le
// binaire d'origine, une écriture du NUL terminal UN OCTET APRÈS la fin du
// slot (déborde dans le premier octet de l'enregistrement suivant). Aucun
// fichier réel n'atteint cette limite (messages bien plus courts que
// STRIDE-1), mais par prudence le portage ci-dessous CLAMPE cette écriture au
// dernier octet valide du slot plutôt que de reproduire le débordement —
// seule divergence volontaire par rapport au binaire, sans impact observable.
#pragma once
#include "Asset/FileUtil.h"
#include "Asset/ImgFile.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ts2::game {

// ---------------------------------------------------------------------------
// FORMAT 2 : table de chaînes "entre guillemets", stride/cap fixes.
// Accès Get(i) 1-based confirmé par désassemblage pour 003/005
// (StrTable003_Get 0x4C1AD0 : this+41*i-37 ; StrTable005_Get 0x4C1D20 :
// this+106*i-102 — les deux formules se réduisent à `4 + STRIDE*(i-1)`,
// c.-à-d. &rec[i-1][0]). Pour 002 (mGAMENOTICE), aucun accesseur dédié n'a
// été retrouvé dans le désassemblage exploré ; la même formule est reprise
// par analogie (layout mémoire identique, cf. TipsTable ci-dessous).
// ---------------------------------------------------------------------------
template <int STRIDE, int CAP>
class QuoteStringTable {
public:
    // Lit un fichier ENTIER en mémoire (pas de compression) et exécute la
    // machine à états sur les guillemets. Échec si :
    //  - ouverture/lecture impossible ;
    //  - plus de CAP enregistrements rencontrés ;
    //  - un enregistrement dépasse STRIDE-1 caractères utiles (voir note de
    //    clamp ci-dessus pour l'unique divergence volontaire).
    bool Load(const std::string& path) {
        std::vector<uint8_t> data;
        if (!asset::ReadWholeFile(path, data)) return false;

        count_ = 0;
        bool inQuotes = false;
        int col = 0;
        for (size_t i = 0; i < data.size(); ++i) {
            const uint8_t c = data[i];
            if (c == '"') {
                if (inQuotes) {
                    inQuotes = false;
                    if (count_ == CAP) return false;
                    int term = col;
                    if (term >= STRIDE) term = STRIDE - 1; // clamp defensif (voir note de tete de fichier)
                    rec_[count_++][term] = 0;
                } else {
                    inQuotes = true;
                    col = 0;
                }
            } else if (inQuotes) {
                if (col == STRIDE) return false; // garde d'origine : record trop long
                rec_[count_][col++] = static_cast<char>(c);
            }
        }
        return true;
    }

    uint32_t Count() const { return count_; }

    // Index 1-based ; chaîne vide statique hors bornes (comme &String dans l'original).
    const char* Get(int index) const {
        if (index < 1 || static_cast<uint32_t>(index) > count_) return "";
        return rec_[index - 1];
    }

private:
    uint32_t count_ = 0;
    char rec_[CAP][STRIDE] = {};
};

// 003.DAT -> mZONENAME (StrTable003_Load 0x4C18E0 / StrTable003_Get 0x4C1AD0).
using StrTable003 = QuoteStringTable<41, 350>;

// 005.DAT -> mMESSAGE (StrTable005_Load 0x4C1B20 / StrTable005_Get 0x4C1D20).
// C'est CETTE table qui doit remplacer le placeholder game::Str(id)="#id"
// dans Game/ClientRuntime.cpp : StrTable005::Get(id) renvoie déjà
// `const char*`, index 1-based, "" hors bornes — signature compatible telle
// quelle avec un usage `return StrTable005::Get(id);` (à adapter en
// std::string si l'appelant le requiert).
using StrTable005 = QuoteStringTable<106, 4000>;

// ---------------------------------------------------------------------------
// 002.DAT -> mGAMENOTICE (bandeau d'annonces tournant). Tips002_Load 0x4C1630
// charge la table ET initialise un timer de rotation ; Tips002_Rotate 0x4C1840
// (probable nom) avance l'index toutes les 600 secondes de jeu.
//
// Layout mémoire réel confirmé par les offsets exploités dans 0x4C1840 :
// `this` est vu comme `float*`, et les champs de rotation sont à this+25251 /
// this+25252 ; or 25251*4 == 4 (count) + 1000*101 (rec[]) octets EXACTEMENT
// -> struct { u32 count; char rec[1000][101]; float lastRotateTime; i32 currentIndex; }.
// `currentIndex` est initialisé à -1 au chargement (le décompilateur l'affiche
// comme un flottant NaN car il partage le pointeur `float*` : bit pattern
// 0xFFFFFFFF = -1 en i32 = NaN en f32) ; `lastRotateTime` est initialisé à
// l'horloge de jeu au moment du chargement (flt_815180 dans l'original).
// ---------------------------------------------------------------------------
class TipsTable {
public:
    // `nowSeconds` = horloge de jeu au chargement (amorce le timer 600 s).
    // `trVariant` sélectionne G01_GFONT\TR\002.DAT (sinon G01_GFONT\002.DAT),
    // comme le test `if (dword_1669190 == 1)` de l'original.
    bool Load(const std::string& gameDataDir, bool trVariant, float nowSeconds);

    uint32_t Count() const { return table_.Count(); }
    const char* Get(int index /*1-based*/) const { return table_.Get(index); }

    // Reproduit la rotation de 0x4C1840 : si (now - lastRotateTime_) >= 600 s,
    // avance currentIndex_ (wrap à 0 quand il atteint count) et renvoie true
    // (nouveau bandeau prêt à être affiché via Current()).
    bool Advance(float nowSeconds);

    // currentIndex_ est stocké 0-based en interne (comme l'original) ; Get()
    // attend un index 1-based, d'où le +1.
    const char* Current() const { return Get(currentIndex_ + 1); }
    int32_t CurrentIndexZeroBased() const { return currentIndex_; }

private:
    QuoteStringTable<101, 1000> table_;
    float   lastRotateTime_ = 0.0f;
    int32_t currentIndex_ = -1;
};

// ---------------------------------------------------------------------------
// 001.DAT -> mBADWORD (dictionnaire de mots bannis, Dict001_Load 0x4C1170).
// SEULE table de ce fichier à passer par l'enveloppe compressée [rawSize]
// [packedSize][zlib] (même décodeur que les .IMG, Asset_DecompressImg
// 0x53F5E0 -> ici asset::ImgFile). Payload décompressé = mots CP949 coréens
// séparés CRLF (délimiteur = octet 0x0D, l'octet 0x0A suivant est sauté sans
// être inspecté — reproduit le `++i` de l'original), records de 51 o.
// Gardes d'intégrité DURES : payload.size()==11572 ET count final==1432,
// sinon échec total (comme l'original qui refuse le manager entier).
// ---------------------------------------------------------------------------
class BannedWordDict {
public:
    // Pas de variante TR : Dict001_Load ne teste jamais g_UseTRVariant.
    bool Load(const std::string& gameDataDir);

    uint32_t Count() const { return count_; }
    // Index 1-based ; "" hors bornes.
    const char* Word(int index) const {
        if (index < 1 || static_cast<uint32_t>(index) > count_) return "";
        return word_[index - 1];
    }

    // NOTE (écart documenté) : le vrai scanner d'origine (candidat
    // "maybe_Dict001_MatchWord" sub_4C1410, EA 0x4C1410, HORS PERIMETRE de
    // cette mission — non demandé parmi les 5 EA assignées) implémente une
    // recherche par fenêtre glissante : il normalise le texte d'entrée en
    // retirant les espaces (0x20) et en recopiant telles quelles les paires
    // DBCS (octet de tête >= 0x80), puis fait glisser une fenêtre à travers
    // CE texte normalisé en la comparant successivement à chaque mot du
    // dictionnaire — MAIS la position de fenêtre n'est JAMAIS réinitialisée
    // entre deux mots essayés (comportement inhabituel, potentiellement un
    // raccourci/bug d'origine, et ses lectures peuvent déborder légèrement
    // le tampon local de 1004 o sans faire planter le binaire d'origine
    // grâce au padding de pile). Le reproduire à l'identique introduirait un
    // comportement mémoire non sûr en C++ moderne (aucune marge de pile
    // garantie de la même façon). IsBanned() ci-dessous est donc une
    // implémentation SÛRE et équivalente en INTENTION (espaces retirés,
    // recherche de sous-chaîne octet-à-octet insensible aux espaces) mais
    // PAS bit-exacte avec sub_4C1410 — à reconfirmer/reporter fidèlement si
    // une divergence de comportement est un jour observée en jeu.
    bool IsBanned(const std::string& text) const;

private:
    uint32_t count_ = 0;
    char word_[2000][51] = {};
};

// ---------------------------------------------------------------------------
// mFONTCOLOR — palette de couleurs codée EN DUR (ColorTable_InitPalette
// 0x4C1D60, aucune E/S fichier, renvoie toujours vrai). Layout confirmé :
//   +0            u32 count = 45
//   +4..+180      colors[45]   (i32 ARGB signés, réinterprétés uint32 0xAARRGGBB)
//   +184 (=this+46) ChannelIndices (8 x i32) = indices 1-based DANS colors[]
// Le bloc +184 correspond EXACTEMENT à la ChannelColorTable documentée dans
// Docs/TS2_CLIENT_SHELL.md à l'adresse absolue 0x84DFD8..0x84DFF4 (instance
// globale mFONTCOLOR = unk_84DF20 ; 0x84DF20 + 184 = 0x84DFD8) — confirme
// que cette table vit DANS le même objet que la palette, pas séparément.
// ---------------------------------------------------------------------------
class ColorPalette {
public:
    struct ChannelIndices {
        int32_t system = 0, whisper = 0, party = 0, shout = 0;
        int32_t guild = 0, faction = 0, trade = 0, gm = 0; // indices 1-based dans colors_
    };

    // ColorTable_InitPalette 0x4C1D60 : constantes en dur, ne peut pas échouer.
    bool InitPalette();

    uint32_t Count() const { return 45; }

    // Index 1-based -> ARGB 0xAARRGGBB ; 0 hors bornes. NOTE : aucun
    // accesseur Get() dédié n'a été retrouvé dans le désassemblage exploré
    // pour cette table (seule InitPalette 0x4C1D60 a été analysée en détail,
    // conformément aux 5 EA assignées) ; la formule d'indexation 1-based est
    // déduite PAR ANALOGIE avec StrTable003_Get/StrTable005_Get. A confirmer
    // si un accesseur dédié est localisé plus tard dans l'IDB.
    uint32_t Get(int index) const {
        if (index < 1 || index > 45) return 0;
        return static_cast<uint32_t>(colors_[index - 1]);
    }

    const ChannelIndices& Channels() const { return channel_; }
    // Résout directement la couleur ARGB d'un canal de chat nommé.
    uint32_t ChannelColor(int32_t paletteIndex1Based) const { return Get(paletteIndex1Based); }

private:
    int32_t colors_[45] = {};
    ChannelIndices channel_{};
};

// ---------------------------------------------------------------------------
// Façade : charge les 5 tables dans l'ordre EXACT de App_Init 0x461C20
// (Dict001 -> Tips002 -> StrTable003 -> StrTable005 -> ColorTable). `gameDataDir`
// = racine "GameData" ; `nowSeconds` = horloge de jeu au chargement (amorce le
// timer de rotation de TipsTable) ; `trVariant` sélectionne les chemins
// G01_GFONT\TR\ (002/003/005 uniquement — 001.DAT n'a pas de variante TR).
//
// Contrairement à l'original (qui abandonne tout App_Init au premier échec,
// cf. MessageBoxA "[Error::mXXX.Init()]"), cette façade tente de charger
// TOUTES les tables et renvoie false si au moins une a échoué, en laissant
// les autres exploitables (même choix que Game/GameDatabase.cpp::LoadGameDatabases).
// ---------------------------------------------------------------------------
struct StringTables {
    BannedWordDict bannedWords; // 001.DAT -> mBADWORD
    TipsTable      notices;     // 002.DAT -> mGAMENOTICE
    StrTable003    zoneNames;   // 003.DAT -> mZONENAME
    StrTable005    messages;    // 005.DAT -> mMESSAGE  <- game::Str(id) doit lire ICI
    ColorPalette   colors;      // codee en dur -> mFONTCOLOR
};

bool LoadStringTables(StringTables& out, const std::string& gameDataDir,
                      float nowSeconds, bool trVariant = false);

// Instance globale unique (miroir des managers mBADWORD/mGAMENOTICE/mZONENAME/
// mMESSAGE/mFONTCOLOR, à l'image de g_Guild/g_Warehouse/g_QuestProgress). Lue par
// game::Str(id) (Game/ClientRuntime.cpp) une fois LoadStringTables() appelée
// depuis App::Init.
inline StringTables g_Strings;

} // namespace ts2::game

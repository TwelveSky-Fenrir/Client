// Game/StringTables.cpp — implémentations non-template de StringTables.h.
// Voir StringTables.h pour la documentation complète des formats et des EA.
#include "Game/StringTables.h"
#include "Game/ClientRuntime.h"   // g_Client.Var(...) : publication de la table de couleurs mFONTCOLOR dans la longue traine
#include "Core/Log.h"
#include <cstring>

namespace ts2::game {

namespace {

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

} // namespace

// ---------------------------------------------------------------------------
// TipsTable (002.DAT -> mGAMENOTICE ; Tips002_Load 0x4C1630).
// ---------------------------------------------------------------------------
bool TipsTable::Load(const std::string& gameDataDir, bool trVariant, float nowSeconds) {
    const std::string path = JoinPath(gameDataDir,
        trVariant ? "G01_GFONT\\TR\\002.DAT" : "G01_GFONT\\002.DAT");
    if (!table_.Load(path)) {
        TS2_ERR("mGAMENOTICE : chargement echoue : %s", path.c_str());
        return false;
    }
    // Init fidele a la fin de sub_4C1630 : lastRotateTime=horloge courante, currentIndex=-1.
    lastRotateTime_ = nowSeconds;
    currentIndex_ = -1;
    TS2_LOG("mGAMENOTICE : %u annonces chargees (%s)", table_.Count(), path.c_str());
    return true;
}

bool TipsTable::Advance(float nowSeconds) {
    if (nowSeconds - lastRotateTime_ < 600.0f) return false;
    lastRotateTime_ = nowSeconds;
    ++currentIndex_;
    if (currentIndex_ == static_cast<int32_t>(table_.Count())) currentIndex_ = 0;
    return true;
}

// ---------------------------------------------------------------------------
// BannedWordDict (001.DAT -> mBADWORD ; Dict001_Load 0x4C1170).
// ---------------------------------------------------------------------------
bool BannedWordDict::Load(const std::string& gameDataDir) {
    const std::string path = JoinPath(gameDataDir, "G01_GFONT\\001.DAT");

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("mBADWORD : .DAT illisible ou decompression echouee : %s", path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() != 11572) {
        // Garde dure de l'original : `if (Buffer == 11572)` sinon echec total.
        TS2_ERR("mBADWORD : taille decompressee inattendue (%zu, attendu 11572) : %s",
                payload.size(), path.c_str());
        return false;
    }

    count_ = 0;
    int col = 0;
    for (size_t i = 0; i < payload.size(); ++i) {
        const uint8_t c = payload[i];
        if (c == 0x0D) { // '\r' — delimiteur d'enregistrement
            if (col == 0) {
                TS2_ERR("mBADWORD : mot vide / CR isole a l'offset %zu", i);
                return false;
            }
            if (count_ == 2000) {
                TS2_ERR("mBADWORD : plus de 2000 mots (capacite depassee)");
                return false;
            }
            int term = col;
            if (term >= 51) term = 50; // clamp defensif (voir note de tete de StringTables.h)
            word_[count_++][term] = 0;
            col = 0;
            ++i; // saute l'octet suivant (0x0A du CRLF), non inspecte, comme l'original
        } else {
            if (col == 51) {
                TS2_ERR("mBADWORD : mot trop long (>51 o) a l'offset %zu", i);
                return false;
            }
            word_[count_][col++] = static_cast<char>(c);
        }
    }

    if (count_ != 1432) {
        // Garde d'integrite dure de l'original : `if (*this == 1432)` sinon echec total.
        TS2_ERR("mBADWORD : compteur final invalide (%u, attendu 1432)", count_);
        return false;
    }

    TS2_LOG("mBADWORD : %u mots bannis charges (CP949) : %s", count_, path.c_str());
    return true;
}

bool BannedWordDict::IsBanned(const std::string& text) const {
    if (count_ == 0) return false;

    // Normalisation fidele a sub_4C1410 0x4C1410 : retire les espaces (0x20),
    // recopie telles quelles les paires DBCS (octet de tete >= 0x80).
    std::string norm;
    norm.reserve(text.size());
    for (size_t i = 0; i < text.size(); ) {
        const uint8_t c = static_cast<uint8_t>(text[i]);
        if (c >= 0x80) {
            norm.push_back(static_cast<char>(c));
            if (i + 1 < text.size()) norm.push_back(text[i + 1]);
            i += 2;
        } else {
            if (c != ' ') norm.push_back(static_cast<char>(c));
            ++i;
        }
    }

    // Recherche de sous-chaine (equivalente en intention a la fenetre glissante
    // d'origine, mais sans son arithmetique de pointeur potentiellement hors
    // bornes — cf. note IsBanned() dans StringTables.h).
    for (uint32_t i = 0; i < count_; ++i) {
        const size_t wordLen = std::strlen(word_[i]);
        if (wordLen == 0 || wordLen > norm.size()) continue;
        if (norm.find(word_[i], 0, wordLen) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// ColorPalette (mFONTCOLOR ; ColorTable_InitPalette 0x4C1D60).
// Constantes reprises telles quelles du desassemblage (45 int32 ARGB signes
// suivis de 8 index de canal), aucune E/S — la fonction d'origine renvoie
// toujours 1.
// ---------------------------------------------------------------------------
bool ColorPalette::InitPalette() {
    count_ = 45; // EA 0x4C1D6A (`*this = 45`) — champ count consomme par Get()/ColorTable_GetColor 0x4C1FE0.
    static const int32_t kColors[45] = {
        -1,        -65536,    -256,      -16776961, -16736414,
        -12775045, -5000269,  -406134,   -7483404,  -7746915,
        -1635,     -745283,   -2162647,  -11369453, -5725184,
        -16750273, -16749207, -16759689, -14284975, -9633754,
        -3425892,  -13424087, -10799340, -3040213,  -7587697,
        -418196,   -6710836,  -5843555,  -7543151,  -12600337,
        -3933003,  -12864513, -489217,   -8659974,  -354816,
        -7680612,  -16727944, -1124,     -536692,   -13246209,
        -13379431, -1661974,  -2243424,  -5592406,  -32704,
    };
    std::memcpy(colors_, kColors, sizeof(colors_));

    // Indices 1-based dans colors_ (this+46..+53 dans le desassemblage,
    // == ChannelColorTable 0x84DFD8 documentee dans TS2_CLIENT_SHELL.md).
    channel_ = ChannelIndices{
        /*system*/15, /*whisper*/1, /*party*/40, /*shout*/39,
        /*guild*/36, /*faction*/37, /*trade*/38, /*gm*/45,
    };
    return true;
}

// ---------------------------------------------------------------------------
// Façade de chargement (ordre App_Init 0x461C20).
// ---------------------------------------------------------------------------
bool LoadStringTables(StringTables& out, const std::string& gameDataDir,
                      float nowSeconds, bool trVariant) {
    bool allOk = true;

    if (!out.bannedWords.Load(gameDataDir)) allOk = false;
    if (!out.notices.Load(gameDataDir, trVariant, nowSeconds)) allOk = false;

    const std::string zonePath = JoinPath(gameDataDir,
        trVariant ? "G01_GFONT\\TR\\003.DAT" : "G01_GFONT\\003.DAT");
    if (!out.zoneNames.Load(zonePath)) {
        TS2_ERR("mZONENAME : chargement echoue : %s", zonePath.c_str());
        allOk = false;
    } else {
        TS2_LOG("mZONENAME : %u zones chargees (%s)", out.zoneNames.Count(), zonePath.c_str());
    }

    const std::string msgPath = JoinPath(gameDataDir,
        trVariant ? "G01_GFONT\\TR\\005.DAT" : "G01_GFONT\\005.DAT");
    if (!out.messages.Load(msgPath)) {
        TS2_ERR("mMESSAGE : chargement echoue : %s", msgPath.c_str());
        allOk = false;
    } else {
        TS2_LOG("mMESSAGE : %u messages charges (%s)", out.messages.Count(), msgPath.c_str());
    }

    if (!out.colors.InitPalette()) allOk = false; // ne peut pas echouer en pratique

    // -----------------------------------------------------------------------
    // CABLAGE de la palette mFONTCOLOR (ColorTable_InitPalette 0x4C1D60) : le
    // binaire garde les 8 couleurs de canal dans un tableau contigu a
    // 0x84DFD8..0x84DFF4 (= instance mFONTCOLOR 0x84DF20 + 184), qui EST la
    // ChannelColorTable (system/whisper/party/shout/guild/faction/trade/gm).
    // On publie ici les 8 ARGB *deja resolus* (ColorTable_GetColor 0x4C1FE0) dans
    // la longue traine g_Client.Var(<EA d'origine>), miroir fidele de ce tableau.
    //
    // Consommation REELLE (verifiee) :
    //   - 0x84DFD8 (g_SysMsgColor, idx15) est LU par 6 sites deja cables :
    //     Net/CharStatDeltaDispatch.cpp:191, Net/GameVarDispatch.cpp (A_SysMsgColor),
    //     Game/SocialSystem.cpp:69, UI/PartyWindow.cpp:48, UI/NpcDialogWindow.cpp:32,
    //     UI/VendorShopWindow.cpp:364 (tous via g_Client.VarGet(0x84dfd8)). Avant ce
    //     cablage la valeur restait 0 -> lignes systeme sociales invisibles
    //     (Game/SocialSystem.cpp:69, sans repli) : bug corrige.
    //   - 0x84DFDC..0x84DFF4 (couleurs de canal chat) : consommees par
    //     UI/ChatWindow.cpp::ChannelColor() qui lit g_Strings.colors DIRECTEMENT ;
    //     publiees ici aussi comme miroir fidele du tableau du binaire (les
    //     handlers Net/GameHandlers_ChatSocial.cpp:33 / Net/GameHandlers_PartyGuild.cpp:33
    //     documentent ces EA comme "valeurs a reextraire" et pourraient les lire ici).
    g_Client.Var(0x84DFD8u) = static_cast<int32_t>(out.colors.SystemColor());  // g_SysMsgColor       idx15 -> 0xFFA8A400
    g_Client.Var(0x84DFDCu) = static_cast<int32_t>(out.colors.WhisperColor()); // g_ChatColor_Whisper idx1  -> 0xFFFFFFFF
    g_Client.Var(0x84DFE0u) = static_cast<int32_t>(out.colors.PartyColor());   // g_ChatColor_Party   idx40 -> 0xFF35E0FF
    g_Client.Var(0x84DFE4u) = static_cast<int32_t>(out.colors.ShoutColor());   // g_ChatColor_Shout   idx39 -> 0xFFF7CF8C
    g_Client.Var(0x84DFE8u) = static_cast<int32_t>(out.colors.GuildColor());   // g_ChatColor_Guild   idx36 -> 0xFF8ACD9C
    g_Client.Var(0x84DFECu) = static_cast<int32_t>(out.colors.FactionColor()); // g_ChatColor_Faction idx37 -> 0xFF00C078
    g_Client.Var(0x84DFF0u) = static_cast<int32_t>(out.colors.TradeColor());   // g_ChatColor_Trade   idx38 -> 0xFFFFFB9C
    g_Client.Var(0x84DFF4u) = static_cast<int32_t>(out.colors.GmColor());      // g_ChatColor_GM      idx45 -> 0xFFFF8040

    return allOk;
}

} // namespace ts2::game

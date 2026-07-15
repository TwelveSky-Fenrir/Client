// World/WorldMap.cpp — implémentation fidèle des chargeurs de monde/zone TwelveSky2.
// Voir WorldMap.h pour les EA sources. Toutes les branches des quatre fonctions cibles
// sont couvertes ; les chemins de fichiers et la table zoneId->fileId sont byte-exacts.
#include "WorldMap.h"

#include <cstdio>

namespace ts2::world {

namespace {

// snprintf « Z%03d » vers un std::string (reproduit Crt_Vsnprintf 0x75cd5f / sub_75CDF8 0x75cdf8).
std::string FormatZone(const char* fmt, int a) {
    char buf[268] = {0}; // CHAR v7[268] dans les fonctions d'origine
    std::snprintf(buf, sizeof(buf), fmt, a);
    return std::string(buf);
}
std::string FormatZone2(const char* fmt, int a, int b) {
    char buf[268] = {0};
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    return std::string(buf);
}

} // namespace

// ===========================================================================
// World_ZoneIdToFileId 0x4db0f0 — table zoneId -> fileId. Transcription exacte
// du switch (les groupes de `case` qui retombent sur la même valeur sont fusionnés).
// ===========================================================================
int WorldMap::ZoneIdToFileId(int zoneId) {
    switch (zoneId) {
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 175;
        case 6: return 6;
        case 7: return 7;
        case 8: return 8;
        case 9: return 9;
        case 10: return 175;
        case 11: return 11;
        case 12: return 12;
        case 13: return 13;
        case 14: return 14;
        case 15: return 175;
        case 16: return 16;
        case 17: return 17;
        case 18: return 18;
        case 19: case 20: case 21: return 175;
        case 22: return 16;
        case 23: return 17;
        case 24: return 18;
        case 25: case 26: case 27: return 175;
        case 28: return 16;
        case 29: return 17;
        case 30: return 18;
        case 31: case 32: case 33: case 34: case 35: case 36: return 175;
        case 37: return 37;
        case 38: return 38;
        case 39: return 39;
        case 40: case 41: case 42: return 40;
        case 43: case 44: case 45: return 43;
        case 46: case 47: case 48: return 46;
        case 49: return 49;
        case 50: return 170;
        case 51: return 51;
        case 52: return 170;
        case 53: return 53;
        case 54: return 54;
        case 55: return 55;
        case 56: case 57: case 58: return 56;
        case 59: case 60: case 61: return 59;
        case 62: return 16;
        case 63: return 17;
        case 64: return 18;
        case 65: return 16;
        case 66: return 17;
        case 67: return 18;
        case 68: return 16;
        case 69: return 17;
        case 70: return 18;
        case 71: return 71;
        case 72: return 72;
        case 73: return 73;
        case 74: return 74;
        case 75: return 75;
        case 76: case 77: case 78: case 79: return 76;
        case 80: case 81: case 82: case 83: return 80;
        case 84: return 84;
        case 85: case 86: case 87: return 196;
        case 88: return 88;
        case 89: return 89;
        case 90: return 90;
        case 91: case 92: case 93: case 94: return 91;
        case 95: case 96: case 97: case 98: return 95;
        case 99: case 100: return 196;
        case 101: case 102: case 103: return 101;
        case 104: return 104;
        case 105: return 105;
        case 106: return 104;
        case 107: return 105;
        case 108: return 104;
        case 109: return 105;
        case 110: return 104;
        case 111: return 105;
        case 112: return 104;
        case 113: return 105;
        case 114: return 104;
        case 115: return 105;
        case 116: return 104;
        case 117: return 105;
        case 118: return 118;
        // NB : case 119 absent -> défaut (-1)
        case 120: case 121: case 122: return 154;
        case 123: return 175;
        case 124: return 124;
        case 125: return 125;
        case 126: case 127: case 128: case 129: case 130: case 131: case 132: case 133:
        case 134: case 135: case 136: case 137: return 126;
        case 138: case 139: return 138;
        case 140: return 140;
        case 141: return 141;
        case 142: return 142;
        case 143: return 143;
        case 144: case 145: return 39;
        case 146: return 49;
        case 147: return 51;
        case 148: return 53;
        case 149: return 49;
        case 150: return 51;
        case 151: case 152: case 153: return 53;
        case 154: return 154;
        case 155: return 155;
        case 156: return 156;
        case 157: return 154;
        case 158: return 155;
        case 159: return 156;
        case 160: return 154;
        case 161: return 155;
        case 162: case 163: case 164: return 156;
        case 165: case 166: return 138;
        case 167: return 101;
        case 168: return 104;
        case 169: return 105;
        case 170: return 170;
        case 171: case 172: case 173: case 174: return 126;
        case 175: case 176: case 177: case 178: case 179: case 180: case 181: case 182:
        case 183: case 184: case 185: case 186: case 187: case 188: case 189: case 190:
        case 191: case 192: case 193: return 175;
        case 194: return 194;
        case 195: return 195;
        case 196: case 197: case 198: case 199: return 196;
        case 200: return 297;
        case 201: return 201;
        case 202: case 203: case 204: case 205: return 202;
        case 206: case 207: case 208: case 209: return 206;
        case 210: case 211: case 212: case 213: case 214: case 215: case 216: case 217:
        case 218: case 219: case 220: case 221: return 126;
        case 222: case 223: case 224: case 225: case 226: case 227: case 228: case 229:
        case 230: case 231: case 232: case 233: return 222;
        case 234: return 234;
        case 235: return 235;
        case 236: return 236;
        case 237: return 237;
        case 238: return 235;
        case 239: return 236;
        case 240: return 237;
        case 241: case 242: case 243: case 244: case 245: case 246: case 247: case 248:
        case 249: return 241;
        case 250: return 267;
        case 251: return 104;
        case 252: return 105;
        case 253: return 104;
        case 254: return 105;
        case 255: return 104;
        case 256: return 105;
        case 257: return 104;
        case 258: return 105;
        case 259: return 104;
        case 260: return 105;
        case 261: return 104;
        case 262: return 105;
        case 263: return 104;
        case 264: return 105;
        case 265: return 104;
        case 266: return 105;
        case 267: case 268: case 269: return 267;
        case 270: case 271: case 272: case 273: case 274: return 270;
        case 275: case 276: case 277: case 278: return 275;
        case 279: case 280: case 281: case 282: return 279;
        case 283: case 284: case 285: case 286: return 283;
        case 287: case 288: case 289: case 290: return 287;
        case 291: return 291;
        case 292: case 293: case 294: return 241;
        case 295: case 296: return 154;
        case 297: case 298: case 299: return 297;
        case 300: case 301: return 300;
        case 302: return 302;
        case 303: return 303;
        case 304: case 305: case 306: case 307: return 304;
        case 308: return 308;
        // NB : case 309 absent -> défaut (-1)
        case 310: return 310;
        case 311: case 312: return 241;
        case 313: case 314: case 315: case 316: case 317: case 318: return 313;
        case 319: case 320: case 321: case 322: case 323: return 319;
        case 324: return 324;
        case 325: case 326: case 327: case 328: case 329: case 330: return 241;
        case 331: case 332: case 333: case 334: case 335: case 336: return 313;
        // NB : cases 337, 338 absentes -> défaut (-1)
        case 339: return 118;
        case 340: return 340;
        case 341: return 341;
        case 342: return 342;
        default: return -1;
    }
}

// ===========================================================================
// Chemin .WM principal (World_LoadZoneResource case 6, premier switch 0x4dcd9b).
// ===========================================================================
std::string WorldMap::ZoneModelPathWM(int fileId, int z291Variant) {
    switch (fileId) {
        case 34:  return "G03_GDATA\\D07_GWORLD\\Z034_1.WM";
        case 49:  return "G03_GDATA\\D07_GWORLD\\Z049_1.WM";
        case 51:  return "G03_GDATA\\D07_GWORLD\\Z051_1.WM";
        case 53:  return "G03_GDATA\\D07_GWORLD\\Z053_1.WM";
        case 54:  return "G03_GDATA\\D07_GWORLD\\Z054_1.WM";
        case 138: case 139: case 165: case 166:
                  return "G03_GDATA\\D07_GWORLD\\Z138_1.WM";
        case 154: return "G03_GDATA\\D07_GWORLD\\Z154_1.WM";
        case 155: return "G03_GDATA\\D07_GWORLD\\Z155_1.WM";
        case 156: return "G03_GDATA\\D07_GWORLD\\Z156_1.WM";
        case 175: return "G03_GDATA\\D07_GWORLD\\Z175_01.WM";
        case 194: return "G03_GDATA\\D07_GWORLD\\Z194_1.WM";
        // 50/52/170 : le principal est Z170_1.WM (le secondaire Z170_2.WM se charge à part).
        case 50: case 52: case 170:
                  return "G03_GDATA\\D07_GWORLD\\Z170_1.WM";
        case 267: return "G03_GDATA\\D07_GWORLD\\Z267_1.WM";
        case 270: return "G03_GDATA\\D07_GWORLD\\Z270_1.WM";
        case 291: return z291Variant == 0 ? "G03_GDATA\\D07_GWORLD\\Z291_1.WM"   // dword_1686134==0
                                           : "G03_GDATA\\D07_GWORLD\\Z291_2.WM";
        case 297: return "G03_GDATA\\D07_GWORLD\\Z297_1.WM"; // fileId 297 couvre zones 200/298/299
        case 319: return "G03_GDATA\\D07_GWORLD\\Z319_1.WM";
        default:  return FormatZone(kFmtWM, fileId);         // "Z%03d.WM"
    }
}

// ===========================================================================
// Chemin .WJ secondaire (World_LoadZoneResource case 6, second switch 0x4dd0b4).
// ===========================================================================
std::string WorldMap::ZoneModelPathWJ(int fileId) {
    switch (fileId) {
        case 34:  return "G03_GDATA\\D07_GWORLD\\Z034_1.WJ";
        case 49:  return "G03_GDATA\\D07_GWORLD\\Z049_1.WJ";
        case 51:  return "G03_GDATA\\D07_GWORLD\\Z051_1.WJ";
        case 53:  return "G03_GDATA\\D07_GWORLD\\Z053_1.WJ";
        case 154: return "G03_GDATA\\D07_GWORLD\\Z154_1.WJ";
        case 155: return "G03_GDATA\\D07_GWORLD\\Z155_1.WJ";
        case 156: return "G03_GDATA\\D07_GWORLD\\Z156_1.WJ";
        case 175: return "G03_GDATA\\D07_GWORLD\\Z175_01.WJ";
        case 194: return "G03_GDATA\\D07_GWORLD\\Z194_1.WJ";
        case 267: return "G03_GDATA\\D07_GWORLD\\Z267_1.WJ";
        case 270: return "G03_GDATA\\D07_GWORLD\\Z270_1.WJ";
        default:  return FormatZone(kFmtWJ, fileId);         // "Z%03d.WJ"
    }
}

// ===========================================================================
// Chemin .WM de la couche courante (World_LoadCurrentZoneModel 0x4dd6e0).
// Sélection par fileId + mode (a2). Chaîne vide = pas de rechargement.
// ===========================================================================
std::string WorldMap::CurrentZoneModelPath(int fileId, int mode) {
    switch (fileId) {
        case 34:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z034_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z034_2.WM";
        case 49:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z049_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z049_2.WM";
        case 51:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z051_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z051_2.WM";
        case 53:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z053_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z053_2.WM";
        case 54:  return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z054_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z054_2.WM";
        case 88:
            switch (mode) {
                case 1: return "G03_GDATA\\D07_GWORLD\\Z088_1.WM";
                case 2: return "G03_GDATA\\D07_GWORLD\\Z088_2.WM";
                case 3: return "G03_GDATA\\D07_GWORLD\\Z088_3.WM";
                case 4: return "G03_GDATA\\D07_GWORLD\\Z088_4.WM";
                case 5: return "G03_GDATA\\D07_GWORLD\\Z088_5.WM";
                case 6: return "G03_GDATA\\D07_GWORLD\\Z088.WM";
                default: return std::string(); // "" -> saut
            }
        case 138: case 139: case 165: case 166:
            switch (mode) {
                case 1: return "G03_GDATA\\D07_GWORLD\\Z138_1.WM";
                case 2: return "G03_GDATA\\D07_GWORLD\\Z138_2.WM";
                case 3: return "G03_GDATA\\D07_GWORLD\\Z138_3.WM";
                case 4: return "G03_GDATA\\D07_GWORLD\\Z138_4.WM";
                default: return "G03_GDATA\\D07_GWORLD\\Z138_5.WM";
            }
        case 154: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z154_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z154_2.WM";
        case 155: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z155_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z155_2.WM";
        case 156: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z156_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z156_2.WM";
        case 175:
            // mode 1..11 -> Z175_01..Z175_11.WM ; sinon "" (saut).
            switch (mode) {
                case 1:  return "G03_GDATA\\D07_GWORLD\\Z175_01.WM";
                case 2:  return "G03_GDATA\\D07_GWORLD\\Z175_02.WM";
                case 3:  return "G03_GDATA\\D07_GWORLD\\Z175_03.WM";
                case 4:  return "G03_GDATA\\D07_GWORLD\\Z175_04.WM";
                case 5:  return "G03_GDATA\\D07_GWORLD\\Z175_05.WM";
                case 6:  return "G03_GDATA\\D07_GWORLD\\Z175_06.WM";
                case 7:  return "G03_GDATA\\D07_GWORLD\\Z175_07.WM";
                case 8:  return "G03_GDATA\\D07_GWORLD\\Z175_08.WM";
                case 9:  return "G03_GDATA\\D07_GWORLD\\Z175_09.WM";
                case 10: return "G03_GDATA\\D07_GWORLD\\Z175_10.WM";
                case 11: return "G03_GDATA\\D07_GWORLD\\Z175_11.WM";
                default: return std::string();
            }
        case 194: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z194_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z194_2.WM";
        case 267: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z267_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z267_2.WM";
        case 270: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z270_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z270_2.WM";
        case 291: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z291_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z291_2.WM";
        case 297:
            if (mode == 1) return "G03_GDATA\\D07_GWORLD\\Z297_1.WM";
            if (mode == 2) return "G03_GDATA\\D07_GWORLD\\Z297_2.WM";
            return "G03_GDATA\\D07_GWORLD\\Z297_3.WM";
        case 319: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z319_1.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z319_2.WM";
        case 324: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z324.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z324_2.WM";
        case 342: return mode == 1 ? "G03_GDATA\\D07_GWORLD\\Z342.WM"
                                   : "G03_GDATA\\D07_GWORLD\\Z342_1.WM";
        default:  return std::string(); // toutes les autres zones : "" -> aucun rechargement
    }
}

// ===========================================================================
// World_LoadMap 0x4116b0 — porte DRM + init atmosphère + météo Atmosphere.DAT.
// ===========================================================================
bool WorldMap::LoadMap(const std::string& mapName, const std::string& drmKey) {
    // this+12 = device (déjà posé via SetDevice ; a3 dans le binaire).

    // --- Porte DRM « ALT1 » ---------------------------------------------------
    // if (Crt_OperatorNew(648)) atmo = cAtmosphere_ctor("ALT1 License 3", clé) else atmo = 0
    void* atmo = nullptr;
    void* mem = hooks_.allocAtmosphere ? hooks_.allocAtmosphere(hooks_.user, 648) : nullptr;
    if (mem && hooks_.constructAtmosphere) {
        // cAtmosphere_ctor 0x791b40 : valide la licence SilverLining (nom const + clé).
        atmo = hooks_.constructAtmosphere(hooks_.user, mem, kAltLicenseName, drmKey.c_str());
    }
    atmosphere_ = atmo; // this+8

    // (*device)[+164] : encadrement device avant init.
    if (hooks_.deviceBeginMap) hooks_.deviceBeginMap(hooks_.user, device_);

    // cAtmosphere_Initialize(1, mapName, 0, device). Retour != 0 => branche « échec » -> 0.
    int initRc = hooks_.atmosphereInitialize
                     ? hooks_.atmosphereInitialize(hooks_.user, atmosphere_, mapName.c_str(), device_)
                     : 0;

    // (*device)[+168] : encadrement device après init (appelé dans les DEUX branches).
    if (hooks_.deviceEndMap) hooks_.deviceEndMap(hooks_.user, device_);

    if (initRc) {
        // Branche « if (Initialize(...)) { ...; return 0; } » : échec/annulation.
        return false; // renvoie 0
    }

    // --- Branche succès : atmosphere[+644]=1, this+4=1, météo par défaut, Atmosphere.DAT ---
    // *(atmosphere+644) = 1  (marqueur interne de l'objet atmosphère).
    valid_ = true;                       // this+4 = 1
    // Str_Assign(mapName) : mémorise le nom de map (byte_815190 côté binaire) — omis (leaf).
    weather_.fill(0);                    // memcpy(this+180, &dword_18C5358, 0x68) : template 104 o, tout à 0.

    // File_IfstreamOpen("Atmosphere.DAT") : si absent/vide -> géoloc par défaut (Séoul) ;
    // sinon parse ligne à ligne (Istream_GetChar/Ostream_WritePad) puis sub_4135F0 + World_FinishLoad.
    bool weatherOk = hooks_.loadWeatherDat
                         ? hooks_.loadWeatherDat(hooks_.user, kAtmosphereDatFile)
                         : false;
    if (!weatherOk) {
        if (hooks_.setGeoLocation)
            hooks_.setGeoLocation(hooks_.user, kDefaultGeoLat, kDefaultGeoLon, kDefaultGeoAlt);
    } else {
        if (hooks_.finishLoad) hooks_.finishLoad(hooks_.user); // World_FinishLoad 0x411c40
    }
    return true; // renvoie 1
}

// ===========================================================================
// World_LoadZoneResource 0x4dcb60 — aiguillage par type de ressource.
// Renvoie l'octet de retour d'origine (LOBYTE(v3)).
// ===========================================================================
unsigned char WorldMap::LoadZoneResource(int zoneId, ResourceKind kind) {
    // LOBYTE(v3) = a3 (kind) à l'entrée ; conservé pour les cas qui ne le réécrivent pas.
    int v3 = static_cast<int>(kind);

    switch (kind) {
        case ResourceKind::FreeSound: // case 1
            v3 = hooks_.freeZoneSound ? (hooks_.freeZoneSound(hooks_.user) ? 1 : 0) : 0;
            return static_cast<unsigned char>(v3);

        case ResourceKind::MapFileWG: { // case 2  .WG
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtWG, fileId);
                v3 = hooks_.loadMapFileWG ? (hooks_.loadMapFileWG(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::ObjectsWO: { // case 3  .WO
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtWO, fileId);
                v3 = hooks_.loadObjectsWO ? (hooks_.loadObjectsWO(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::ObjectsWP: { // case 4  .WP
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtWP, fileId);
                v3 = hooks_.loadObjectsWP ? (hooks_.loadObjectsWP(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::ShadowTex: { // case 5  .SHADOW
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtShadow, fileId);
                v3 = hooks_.loadShadowTexture ? (hooks_.loadShadowTexture(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::WorldModel: { // case 6  .WM + .WJ
            int fileId = ZoneIdToFileId(zoneId);
            if (fileId == -1) return static_cast<unsigned char>(fileId); // 0xFF

            // Chemin .WM principal.
            std::string wm = ZoneModelPathWM(fileId, flagZ291Variant);
            // Zones 50/52/170 : double chargement (secondaire Z170_2.WM dans le slot Secondary).
            if (fileId == 170 || fileId == 50 || fileId == 52) {
                if (hooks_.loadFaces)
                    hooks_.loadFaces(hooks_.user, CollisionSlot::Secondary,
                                     "G03_GDATA\\D07_GWORLD\\Z170_2.WM");
                // wm reste Z170_1.WM (déjà renvoyé par ZoneModelPathWM).
            }
            // MapColl_LoadFaces(this+0xA8, wm) — collision principale.
            if (hooks_.loadFaces) hooks_.loadFaces(hooks_.user, CollisionSlot::Main, wm.c_str());

            // Chemin .WJ secondaire -> MapColl_LoadFaces(this+0x150, wj).
            std::string wj = ZoneModelPathWJ(fileId);
            v3 = hooks_.loadFaces ? (hooks_.loadFaces(hooks_.user, CollisionSlot::WJ, wj.c_str()) ? 1 : 0) : 0;
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Atmosphere: { // case 7  .ATM
            // a2 == -1 -> saut (renvoie v3 = kind à l'entrée).
            if (zoneId == -1) return static_cast<unsigned char>(v3);
            // if (atmosphereLoaded || World_LoadMap(...)) { charger .ATM }
            bool proceed = atmosphereLoaded_;
            if (!proceed) {
                bool ok = LoadMap(kAtmosphereResourceDir); // -> dword_18C67C4, device g_GfxRenderer_pDevice
                v3 = ok ? 1 : 0;
                proceed = ok;
            }
            if (proceed) {
                // ATM utilise le zoneId BRUT (a2), pas le fileId.
                std::string p = FormatZone(kFmtAtm, zoneId);
                v3 = hooks_.loadDataFile ? (hooks_.loadDataFile(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Minimap01: { // case 8
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtMinimap1, fileId);
                v3 = hooks_.loadMinimap ? (hooks_.loadMinimap(hooks_.user, 1, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Minimap02: { // case 9
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtMinimap2, fileId);
                v3 = hooks_.loadMinimap ? (hooks_.loadMinimap(hooks_.user, 2, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::Minimap03: { // case 10
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtMinimap3, fileId);
                v3 = hooks_.loadMinimap ? (hooks_.loadMinimap(hooks_.user, 3, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::WorldSound: { // case 11  .WSOUND (Z%03d\Z%03d.WSOUND)
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone2(kFmtWSound, fileId, fileId);
                v3 = hooks_.loadWorldSound ? (hooks_.loadWorldSound(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        case ResourceKind::WorldBgm: { // case 12  .BGM
            int fileId = ZoneIdToFileId(zoneId);
            v3 = fileId;
            if (fileId != -1) {
                std::string p = FormatZone(kFmtBgm, fileId);
                v3 = hooks_.loadWorldBgm ? (hooks_.loadWorldBgm(hooks_.user, p.c_str()) ? 1 : 0) : 0;
            }
            return static_cast<unsigned char>(v3);
        }
        default: // switch default : renvoie v3 inchangé (= kind).
            return static_cast<unsigned char>(v3);
    }
}

// ===========================================================================
// World_LoadCurrentZoneModel 0x4dd6e0 — recharge la collision principale (this+0xA8)
// avec le modèle .WM de la couche `mode` de la zone courante (g_SelfMorphNpcId).
// ===========================================================================
int WorldMap::LoadCurrentZoneModel(int mode) {
    int fileId = ZoneIdToFileId(currentZoneId_); // World_ZoneIdToFileId(dword_1675A98)
    if (fileId == -1) return fileId;             // -1

    std::string path = CurrentZoneModelPath(fileId, mode);

    // result = Crt_Strcmp(path, "") : 0 si path vide -> pas de rechargement (renvoie 0).
    if (path.empty()) return 0;

    // MapColl_Free(this+0xA8) puis MapColl_LoadFaces(this+0xA8, path).
    if (hooks_.freeFaces) hooks_.freeFaces(hooks_.user, CollisionSlot::Main);
    return hooks_.loadFaces ? (hooks_.loadFaces(hooks_.user, CollisionSlot::Main, path.c_str()) ? 1 : 0)
                            : 0;
}

} // namespace ts2::world

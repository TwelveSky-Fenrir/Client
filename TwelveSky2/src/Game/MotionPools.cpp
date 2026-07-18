// Game/MotionPools.cpp — see MotionPools.h for the full mapping to
// App_Init 0x461C20 and the disassembly of the 4 original functions.
#include "Game/MotionPools.h"
#include "Asset/FileUtil.h"
#include "Core/Log.h"
#include <cstring>

namespace ts2::game {

namespace {

// Raw table of the 350 rows {start,end,type,flag}, extracted AUTOMATICALLY from the
// Hex-Rays pseudocode of Motion_InitFrameTable 0x4F1380 (350-case switch(i), of which
// 12 fall into the "default: continue;" -> {0,0,-1,0}, initial values set before
// the switch: this[2*i]=0, this[2*i+1]=0, this[2*i+700]=-1, this[2*i+701]=0). Index =
// i (0-based); actual motion = i+1 (see GetFrameRange, 1-based like all the other
// GInfo* accessors in the binary).
constexpr MotionFrameRange kFrameTableData[kMotionFrameTableCount] = {
    {1,157,0,1}, // i=0
    {1,157,0,1}, // i=1
    {1,157,0,1}, // i=2
    {1,157,0,1}, // i=3
    {157,157,4,0}, // i=4
    {1,157,1,1}, // i=5
    {1,157,1,1}, // i=6
    {1,157,1,1}, // i=7
    {1,157,1,1}, // i=8
    {157,157,4,0}, // i=9
    {1,157,2,1}, // i=10
    {1,157,2,1}, // i=11
    {1,157,2,1}, // i=12
    {1,157,2,1}, // i=13
    {157,157,4,0}, // i=14
    {1,157,0,0}, // i=15
    {1,157,0,0}, // i=16
    {1,157,0,0}, // i=17
    {146,157,4,0}, // i=18
    {146,157,5,0}, // i=19
    {146,157,6,0}, // i=20
    {1,157,1,0}, // i=21
    {1,157,1,0}, // i=22
    {1,157,1,0}, // i=23
    {150,153,4,0}, // i=24
    {150,153,5,0}, // i=25
    {150,153,6,0}, // i=26
    {1,157,2,0}, // i=27
    {1,157,2,0}, // i=28
    {1,157,2,0}, // i=29
    {154,157,4,0}, // i=30
    {154,157,5,0}, // i=31
    {154,157,6,0}, // i=32
    {146,157,7,0}, // i=33
    {150,153,7,0}, // i=34
    {154,157,7,0}, // i=35
    {1,157,-1,0}, // i=36
    {90,157,-1,1}, // i=37
    {90,112,-1,0}, // i=38
    {90,157,0,0}, // i=39
    {90,157,1,0}, // i=40
    {90,157,2,0}, // i=41
    {90,157,0,0}, // i=42
    {90,157,1,0}, // i=43
    {90,157,2,0}, // i=44
    {1,157,0,0}, // i=45
    {1,157,1,0}, // i=46
    {1,157,2,0}, // i=47
    {10,112,11,1}, // i=48
    {1,157,-1,0}, // i=49
    {20,29,12,1}, // i=50
    {1,157,-1,0}, // i=51
    {30,39,13,1}, // i=52
    {157,157,-1,1}, // i=53
    {113,157,-1,1}, // i=54
    {90,157,0,0}, // i=55
    {90,157,1,0}, // i=56
    {90,157,2,0}, // i=57
    {90,157,0,0}, // i=58
    {90,157,1,0}, // i=59
    {90,157,2,0}, // i=60
    {90,157,0,0}, // i=61
    {90,157,0,0}, // i=62
    {90,157,0,0}, // i=63
    {90,157,1,0}, // i=64
    {90,157,1,0}, // i=65
    {90,157,1,0}, // i=66
    {90,157,2,0}, // i=67
    {90,157,2,0}, // i=68
    {90,157,2,0}, // i=69
    {1,157,0,0}, // i=70
    {1,157,1,0}, // i=71
    {1,157,2,0}, // i=72
    {113,157,-1,0}, // i=73
    {146,157,-1,1}, // i=74
    {146,157,4,0}, // i=75
    {146,157,5,0}, // i=76
    {146,157,6,0}, // i=77
    {146,157,7,0}, // i=78
    {146,157,4,0}, // i=79
    {146,157,5,0}, // i=80
    {146,157,6,0}, // i=81
    {146,157,7,0}, // i=82
    {157,157,-1,1}, // i=83
    {146,156,-1,2}, // i=84
    {150,153,-1,2}, // i=85
    {154,156,-1,2}, // i=86
    {157,157,16,2}, // i=87
    {113,157,-1,1}, // i=88
    {146,157,-1,1}, // i=89
    {146,157,4,0}, // i=90
    {146,157,5,0}, // i=91
    {146,157,6,0}, // i=92
    {146,157,7,0}, // i=93
    {146,157,4,0}, // i=94
    {146,157,5,0}, // i=95
    {146,157,6,0}, // i=96
    {146,157,7,0}, // i=97
    {154,157,-1,2}, // i=98
    {154,157,-1,2}, // i=99
    {113,157,4,0}, // i=100
    {113,157,5,0}, // i=101
    {113,157,6,0}, // i=102
    {113,157,4,0}, // i=103
    {113,157,4,0}, // i=104
    {113,157,5,0}, // i=105
    {113,157,5,0}, // i=106
    {113,157,6,0}, // i=107
    {113,157,6,0}, // i=108
    {146,157,4,0}, // i=109
    {146,157,4,0}, // i=110
    {146,157,5,0}, // i=111
    {146,157,5,0}, // i=112
    {146,157,6,0}, // i=113
    {146,157,6,0}, // i=114
    {146,157,7,0}, // i=115
    {146,157,7,0}, // i=116
    {157,157,10,0}, // i=117
    {0,0,-1,0}, // i=118
    {146,156,11,1}, // i=119
    {150,153,11,1}, // i=120
    {154,156,11,1}, // i=121
    {157,157,4,0}, // i=122
    {1,157,-1,0}, // i=123
    {145,157,-1,1}, // i=124
    {100,157,4,0}, // i=125
    {100,157,4,0}, // i=126
    {100,157,4,0}, // i=127
    {100,157,4,0}, // i=128
    {100,157,5,0}, // i=129
    {100,157,5,0}, // i=130
    {100,157,5,0}, // i=131
    {100,157,5,0}, // i=132
    {100,157,6,0}, // i=133
    {100,157,6,0}, // i=134
    {100,157,6,0}, // i=135
    {100,157,6,0}, // i=136
    {1,157,-1,1}, // i=137
    {1,157,-1,1}, // i=138
    {113,157,3,1}, // i=139
    {113,157,3,1}, // i=140
    {113,157,3,1}, // i=141
    {113,157,3,1}, // i=142
    {113,145,-1,0}, // i=143
    {146,157,-1,0}, // i=144
    {40,49,11,1}, // i=145
    {50,59,12,1}, // i=146
    {60,69,13,1}, // i=147
    {70,79,11,1}, // i=148
    {80,89,12,1}, // i=149
    {90,99,13,1}, // i=150
    {100,105,13,1}, // i=151
    {106,112,13,1}, // i=152
    {113,145,11,1}, // i=153
    {116,118,12,1}, // i=154
    {119,121,13,1}, // i=155
    {122,124,11,1}, // i=156
    {125,127,12,1}, // i=157
    {128,130,13,1}, // i=158
    {131,133,11,1}, // i=159
    {134,136,12,1}, // i=160
    {137,139,13,1}, // i=161
    {140,142,13,1}, // i=162
    {143,145,13,1}, // i=163
    {1,157,-1,1}, // i=164
    {1,157,-1,1}, // i=165
    {113,157,7,0}, // i=166
    {113,157,7,0}, // i=167
    {113,157,7,0}, // i=168
    {1,157,-1,0}, // i=169
    {100,157,7,0}, // i=170
    {100,157,7,0}, // i=171
    {100,157,7,0}, // i=172
    {100,157,7,0}, // i=173
    {100,112,4,0}, // i=174
    {100,112,5,0}, // i=175
    {100,112,6,0}, // i=176
    {113,122,4,0}, // i=177
    {113,122,5,0}, // i=178
    {113,122,6,0}, // i=179
    {113,122,7,0}, // i=180
    {123,132,4,0}, // i=181
    {123,132,5,0}, // i=182
    {123,132,6,0}, // i=183
    {123,132,7,0}, // i=184
    {133,142,4,0}, // i=185
    {133,142,5,0}, // i=186
    {133,142,6,0}, // i=187
    {133,142,7,0}, // i=188
    {113,145,4,0}, // i=189
    {113,145,5,0}, // i=190
    {113,145,6,0}, // i=191
    {113,145,7,0}, // i=192
    {157,157,14,1}, // i=193
    {100,112,-1,1}, // i=194
    {113,145,-1,2}, // i=195
    {123,132,-1,2}, // i=196
    {133,142,-1,2}, // i=197
    {143,145,-1,2}, // i=198
    {157,157,-1,1}, // i=199
    {146,157,-1,1}, // i=200
    {146,157,4,0}, // i=201
    {146,157,5,0}, // i=202
    {146,157,6,0}, // i=203
    {146,157,7,0}, // i=204
    {146,157,4,0}, // i=205
    {146,157,5,0}, // i=206
    {146,157,6,0}, // i=207
    {146,157,7,0}, // i=208
    {100,157,4,0}, // i=209
    {100,157,5,0}, // i=210
    {100,157,6,0}, // i=211
    {100,157,7,0}, // i=212
    {100,157,4,0}, // i=213
    {100,157,5,0}, // i=214
    {100,157,6,0}, // i=215
    {100,157,7,0}, // i=216
    {100,157,4,0}, // i=217
    {100,157,5,0}, // i=218
    {100,157,6,0}, // i=219
    {100,157,7,0}, // i=220
    {100,157,4,0}, // i=221
    {100,157,5,0}, // i=222
    {100,157,6,0}, // i=223
    {100,157,7,0}, // i=224
    {100,157,4,0}, // i=225
    {100,157,5,0}, // i=226
    {100,157,6,0}, // i=227
    {100,157,7,0}, // i=228
    {100,157,4,0}, // i=229
    {100,157,5,0}, // i=230
    {100,157,6,0}, // i=231
    {100,157,7,0}, // i=232
    {157,157,-1,0}, // i=233
    {157,157,-1,0}, // i=234
    {157,157,-1,0}, // i=235
    {157,157,-1,0}, // i=236
    {157,157,-1,0}, // i=237
    {157,157,-1,0}, // i=238
    {157,157,-1,0}, // i=239
    {157,157,-1,0}, // i=240
    {157,157,-1,0}, // i=241
    {157,157,-1,0}, // i=242
    {157,157,-1,0}, // i=243
    {157,157,-1,0}, // i=244
    {157,157,-1,0}, // i=245
    {157,157,-1,0}, // i=246
    {157,157,-1,0}, // i=247
    {157,157,-1,0}, // i=248
    {146,157,15,1}, // i=249
    {146,157,4,0}, // i=250
    {146,157,4,0}, // i=251
    {146,157,5,0}, // i=252
    {146,157,5,0}, // i=253
    {146,157,6,0}, // i=254
    {146,157,6,0}, // i=255
    {146,157,7,0}, // i=256
    {146,157,7,0}, // i=257
    {146,157,4,0}, // i=258
    {146,157,4,0}, // i=259
    {146,157,5,0}, // i=260
    {146,157,5,0}, // i=261
    {146,157,6,0}, // i=262
    {146,157,6,0}, // i=263
    {146,157,7,0}, // i=264
    {146,157,7,0}, // i=265
    {70,112,15,1}, // i=266
    {113,145,15,1}, // i=267
    {146,157,15,1}, // i=268
    {157,157,-1,1}, // i=269
    {157,157,-1,1}, // i=270
    {157,157,-1,1}, // i=271
    {157,157,-1,1}, // i=272
    {157,157,-1,1}, // i=273
    {157,157,4,0}, // i=274
    {157,157,5,0}, // i=275
    {157,157,6,0}, // i=276
    {157,157,7,0}, // i=277
    {157,157,4,0}, // i=278
    {157,157,5,0}, // i=279
    {157,157,6,0}, // i=280
    {157,157,7,0}, // i=281
    {157,157,4,0}, // i=282
    {157,157,5,0}, // i=283
    {157,157,6,0}, // i=284
    {157,157,7,0}, // i=285
    {157,157,4,0}, // i=286
    {157,157,5,0}, // i=287
    {157,157,6,0}, // i=288
    {157,157,7,0}, // i=289
    {1,157,-1,1}, // i=290
    {157,157,-1,0}, // i=291
    {157,157,-1,0}, // i=292
    {157,157,-1,0}, // i=293
    {157,157,11,1}, // i=294
    {157,157,11,1}, // i=295
    {1,157,-1,1}, // i=296
    {1,157,-1,1}, // i=297
    {1,157,-1,1}, // i=298
    {157,157,11,0}, // i=299
    {157,157,11,0}, // i=300
    {157,157,-1,1}, // i=301
    {157,157,-1,1}, // i=302
    {157,157,-1,0}, // i=303
    {157,157,-1,0}, // i=304
    {157,157,-1,0}, // i=305
    {157,157,-1,0}, // i=306
    {157,157,-1,1}, // i=307
    {0,0,-1,0}, // i=308
    {157,157,-1,0}, // i=309
    {157,157,-1,0}, // i=310
    {157,157,-1,0}, // i=311
    {157,157,11,1}, // i=312
    {157,157,11,1}, // i=313
    {157,157,11,1}, // i=314
    {157,157,11,1}, // i=315
    {157,157,11,1}, // i=316
    {157,157,11,1}, // i=317
    {146,149,11,0}, // i=318
    {150,153,11,0}, // i=319
    {154,156,11,0}, // i=320
    {157,157,11,0}, // i=321
    {157,157,11,0}, // i=322
    {157,157,-1,1}, // i=323
    {157,157,-1,0}, // i=324
    {157,157,-1,0}, // i=325
    {157,157,-1,0}, // i=326
    {157,157,-1,0}, // i=327
    {157,157,-1,0}, // i=328
    {157,157,-1,0}, // i=329
    {157,157,11,1}, // i=330
    {157,157,11,1}, // i=331
    {157,157,11,1}, // i=332
    {157,157,11,1}, // i=333
    {157,157,11,1}, // i=334
    {157,157,11,1}, // i=335
    {0,0,-1,0}, // i=336
    {0,0,-1,0}, // i=337
    {157,157,10,0}, // i=338
    {157,157,16,1}, // i=339
    {157,157,11,1}, // i=340
    {157,157,-1,1}, // i=341
    {0,0,-1,0}, // i=342
    {0,0,-1,0}, // i=343
    {0,0,-1,0}, // i=344
    {0,0,-1,0}, // i=345
    {0,0,-1,0}, // i=346
    {0,0,-1,0}, // i=347
    {0,0,-1,0}, // i=348
    {0,0,-1,0}, // i=349
};

// Runtime state of the 4 pools (exclusively owned by this module — original
// symbolic names noted in comments for cross-auditing against the disassembly).
bool g_frameTableReady = false;                 // g_MotionFrameRangeTable ready
MotionFrameRange g_frameTable[kMotionFrameTableCount]; // runtime copy (faithful to the original: a separate copy, not just kFrameTableData reused as-is, to stay consistent with a possible future mutation need)

std::vector<uint32_t> g_attachTable;  // dword_14AA930 — 350*501 DWORD once loaded
std::vector<float>    g_coordTable;   // flt_1555D08   — 350*805 FLOAT once loaded

bool g_modelMotionPoolReady = false;  // g_ModelMotionArray "initialized" (mGDATA)

// Joins two path segments (identical to Game/GameDatabase.cpp).
std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

} // namespace

// 1) mGDATA
bool InitModelMotionPool() {
    // See header comment in MotionPools.h: building the per-slot paths
    // (Sprite2D_BuildPath/ModelObj_BuildPath/Motion_BuildPathAndLoad/SObject_BuildPath/
    // SObject_BuildPathW/Snd3D_SetISNPath) and starting the 5 asynchronous threads
    // (cThread_Start) are OUT OF SCOPE for this module (separate render/asset/
    // thread subsystems). AssetMgr_InitAllSlots 0x4DEB50 has no failure path
    // in the disassembly: we faithfully reproduce this absence of failure.
    g_modelMotionPoolReady = true;
    TS2_LOG("mGDATA : pool documente (layout ModelMotionPoolLayout) — BuildPath/threads"
            " delegues aux sous-systemes render/asset/thread (hors perimetre Asset/Motion)");
    return true;
}

// 2) mZONEMAININFO
bool InitFrameTable() {
    std::memcpy(g_frameTable, kFrameTableData, sizeof(g_frameTable));
    g_frameTableReady = true;
    TS2_LOG("mZONEMAININFO : table de %d motions initialisee (donnees en dur, sans fichier)",
            kMotionFrameTableCount);
    return true;
}

const MotionFrameRange* GetFrameRange(int motionIndex1Based) {
    if (!g_frameTableReady) return nullptr;
    if (motionIndex1Based < 1 || motionIndex1Based > kMotionFrameTableCount) return nullptr;
    return &g_frameTable[motionIndex1Based - 1];
}

// 3) mZONENPCINFO — G02_GINFO\002.BIN
bool LoadGInfo002Bin(const std::string& gameDataDir) {
    const std::string path = JoinPath(gameDataDir, "G02_GINFO\\002.BIN");

    std::vector<uint8_t> raw;
    if (!asset::ReadWholeFile(path, raw)) {
        TS2_ERR("mZONENPCINFO : G02_GINFO\\002.BIN illisible : %s", path.c_str());
        return false;
    }
    // Faithful to Motion_LoadGInfo002Bin 0x4FCFD0: the original loader fails if the
    // read size differs EXACTLY from 701400 B (guard `NumberOfBytesRead == 701400`).
    if (raw.size() != kGInfo002BinSize) {
        TS2_ERR("mZONENPCINFO : taille inattendue (%zu o, attendu %zu o) : %s",
                raw.size(), kGInfo002BinSize, path.c_str());
        return false;
    }

    g_attachTable.assign(kAttachTableRowCount * kAttachTableRowStride, 0);
    std::memcpy(g_attachTable.data(), raw.data(), kGInfo002BinSize);

    TS2_LOG("mZONENPCINFO : %s charge (%d lignes x %d DWORD)",
            path.c_str(), kAttachTableRowCount, kAttachTableRowStride);
    return true;
}

const uint32_t* LoadedAttachTable() {
    return g_attachTable.empty() ? nullptr : g_attachTable.data();
}

const uint32_t* AttachTableRow(int motionIndex1Based) {
    if (g_attachTable.empty()) return nullptr;
    if (motionIndex1Based < 1 || motionIndex1Based > kAttachTableRowCount) return nullptr;
    return g_attachTable.data() + static_cast<size_t>(motionIndex1Based - 1) * kAttachTableRowStride;
}

int FindMotionByFrameId(int frameId) {
    // Faithful reimplementation of GInfo_FindMotionByFrameId 0x4FD070.
    if (g_attachTable.empty()) return 0;
    for (int i = 0; i < kAttachTableRowCount; ++i) {
        const uint32_t* row = g_attachTable.data() + static_cast<size_t>(i) * kAttachTableRowStride;
        const uint32_t n = row[0];
        for (uint32_t j = 0; j < n; ++j) {
            if (row[j + 1] == static_cast<uint32_t>(frameId))
                return i + 1;
        }
    }
    return 0;
}

bool GetRawAttachPoint(int frameId, float& x, float& y, float& z, int* outMotionIndex1Based) {
    x = y = z = 0.0f;
    if (outMotionIndex1Based) *outMotionIndex1Based = 0;
    if (g_attachTable.empty()) return false;

    // Same internal traversal as GInfo_CalcLeftMargin/GInfo_CalcRightMargin (i,j),
    // but we return the RAW triplet (x,y,z) instead of the margin corrected by
    // Motion_GetAABB (out of scope — see MotionPools.h).
    for (int i = 0; i < kAttachTableRowCount; ++i) {
        const uint32_t* rowU = g_attachTable.data() + static_cast<size_t>(i) * kAttachTableRowStride;
        const float* rowF = reinterpret_cast<const float*>(rowU);
        const uint32_t n = rowU[0];
        for (uint32_t j = 0; j < n; ++j) {
            if (rowU[j + 1] == static_cast<uint32_t>(frameId)) {
                x = rowF[101 + 3 * j];
                y = rowF[102 + 3 * j];
                z = rowF[103 + 3 * j];
                if (outMotionIndex1Based) *outMotionIndex1Based = i + 1;
                return true;
            }
        }
    }
    return false;
}

int ZoneNpcCount(int zoneId1Based) {
    const uint32_t* row = AttachTableRow(zoneId1Based);
    if (!row) return 0;
    return static_cast<int>(row[0]);
}

uint32_t ZoneNpcKindId(int zoneId1Based, int npcIndex0Based) {
    const uint32_t* row = AttachTableRow(zoneId1Based);
    if (!row || npcIndex0Based < 0 || npcIndex0Based >= static_cast<int>(row[0])) return 0;
    return row[1 + npcIndex0Based];
}

bool ZoneNpcPosition(int zoneId1Based, int npcIndex0Based, float& x, float& y, float& z) {
    x = y = z = 0.0f;
    const uint32_t* rowU = AttachTableRow(zoneId1Based);
    if (!rowU || npcIndex0Based < 0 || npcIndex0Based >= static_cast<int>(rowU[0])) return false;
    const float* rowF = reinterpret_cast<const float*>(rowU);
    x = rowF[101 + 3 * npcIndex0Based];
    y = rowF[102 + 3 * npcIndex0Based];
    z = rowF[103 + 3 * npcIndex0Based];
    return true;
}

float ZoneNpcAngle(int zoneId1Based, int npcIndex0Based) {
    const uint32_t* rowU = AttachTableRow(zoneId1Based);
    if (!rowU || npcIndex0Based < 0 || npcIndex0Based >= static_cast<int>(rowU[0])) return 0.0f;
    const float* rowF = reinterpret_cast<const float*>(rowU);
    return rowF[401 + npcIndex0Based];
}

// 4) mZONEMOVEINFO — G02_GINFO\003.BIN
bool LoadGInfo003Bin(const std::string& gameDataDir) {
    const std::string path = JoinPath(gameDataDir, "G02_GINFO\\003.BIN");

    std::vector<uint8_t> raw;
    if (!asset::ReadWholeFile(path, raw)) {
        TS2_ERR("mZONEMOVEINFO : G02_GINFO\\003.BIN illisible : %s", path.c_str());
        return false;
    }
    // Faithful to Motion_LoadGInfo003Bin 0x4FD420: guard `NumberOfBytesRead == 1127000`.
    if (raw.size() != kGInfo003BinSize) {
        TS2_ERR("mZONEMOVEINFO : taille inattendue (%zu o, attendu %zu o) : %s",
                raw.size(), kGInfo003BinSize, path.c_str());
        return false;
    }

    g_coordTable.assign(kCoordTableRowCount * kCoordTableRowStride, 0.0f);
    std::memcpy(g_coordTable.data(), raw.data(), kGInfo003BinSize);

    TS2_LOG("mZONEMOVEINFO : %s charge (%d lignes x %d FLOAT)",
            path.c_str(), kCoordTableRowCount, kCoordTableRowStride);
    return true;
}

const float* LoadedCoordTable() {
    return g_coordTable.empty() ? nullptr : g_coordTable.data();
}

bool GetVec3(int motionIndex1Based, float& x, float& y, float& z) {
    // Faithful reimplementation of GInfo2_GetVec3 0x4FD4C0: out of bounds -> (0,0,0),false.
    x = y = z = 0.0f;
    if (g_coordTable.empty()) return false;
    if (motionIndex1Based < 1 || motionIndex1Based > kCoordTableRowCount) return false;

    const float* row = g_coordTable.data() + static_cast<size_t>(motionIndex1Based - 1) * kCoordTableRowStride;
    x = row[0];
    y = row[1];
    z = row[2];
    return true;
}

} // namespace ts2::game

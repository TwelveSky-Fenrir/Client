// Game/StringTables.h — client string/localization tables (G01_GFONT).
//
// Faithfully reproduces 5 loaders called in sequence by App_Init 0x461C20
// (managers [Error::mBADWORD.Init()] .. [Error::mFONTCOLOR.Init()]):
//
//   G01_GFONT\001.DAT -> mBADWORD    Dict001_Load          0x4C1170  (BannedWordDict)
//   G01_GFONT\002.DAT -> mGAMENOTICE Tips002_Load           0x4C1630  (TipsTable)
//   G01_GFONT\003.DAT -> mZONENAME   StrTable003_Load        0x4C18E0  (StrTable003)
//   G01_GFONT\005.DAT -> mMESSAGE    StrTable005_Load        0x4C1B20  (StrTable005)
//   (hardcoded)        -> mFONTCOLOR  ColorTable_InitPalette   0x4C1D60  (ColorPalette)
//
// Global instances identified in App_Init (this = &unk_XXXXXXXX):
//   mBADWORD    unk_8CE2D8   mGAMENOTICE unk_8B5840   mZONENAME unk_84A6A8
//   mMESSAGE    unk_84DFF8   mFONTCOLOR  unk_84DF20
//
// Two clearly distinct ON-DISK FORMAT families (confirmed by reading the
// disassembly — see Docs/TS2_ASSET_FORMATS.md §2.11):
//
//   FORMAT 2 "quote-delimited" (002/003/005.DAT): RAW ASCII file, NOT
//   compressed. A state machine only reacts to the byte '"' (0x22): everything
//   else outside quotes (numeric prefix "NNN.", CRLF, spaces) is ignored.
//   Each consecutive quote pair produces ONE record. Manager memory layout:
//   u32 count THEN count records of STRIDE bytes each, NUL-terminated within
//   their slot.
//     002.DAT (mGAMENOTICE): STRIDE=101, CAP=1000
//     003.DAT (mZONENAME)  : STRIDE=41,  CAP=350
//     005.DAT (mMESSAGE)   : STRIDE=106, CAP=4000  <- target table for game::Str(id)
//   Path selected by g_UseTRVariant (0x1669190): TR => "G01_GFONT\TR\00N.DAT".
//
//   FORMAT 3 "compressed dictionary" (001.DAT, mBADWORD ONLY): SAME envelope
//   as the .IMG files -> [u32 rawSize][u32 packedSize][zlib stream], decoded
//   by Asset_DecompressImg 0x53F5E0 (reused here via asset::ImgFile, which
//   applies exactly this same envelope). Decompressed = CP949 words (Korean,
//   NOT localized even in the EU build) CRLF-separated, 51-byte records.
//   HARD integrity guards from the original: decompressed rawSize MUST be
//   11572 AND the final entry count MUST be 1432 (otherwise total failure).
//   No TR variant (g_UseTRVariant is never tested in Dict001_Load).
//
//   mFONTCOLOR (ColorTable_InitPalette 0x4C1D60) is NOT a file loader: the
//   palette is HARDCODED in the function (45 signed ARGB colors
//   0xAARRGGBB + 8 "chat channel" indices). Confirmed by cross-checking
//   addresses: unk_84DF20 + 184 bytes (= this+46 dwords) == 0x84DFD8, which is
//   exactly the ChannelColorTable documented in TS2_CLIENT_SHELL.md
//   (system/whisper/party/shout/guild/faction/trade/gm): these are NOT direct
//   colors but 1-based INDICES into the 45-color array.
//
// IMPORTANT NOTE (fidelity vs memory safety): in all 3 quote-delimited
// loaders AND in Dict001_Load, the slot-overflow guard is checked BEFORE
// writing a content character (`if (col == STRIDE) return false`) but NOT
// before writing the end-of-record NUL terminator — a record that fills
// exactly STRIDE bytes triggers, in the original binary, a write of the
// terminal NUL ONE BYTE PAST the end of the slot (overflows into the first
// byte of the next record). No real file reaches this limit (messages are
// far shorter than STRIDE-1), but as a precaution the port below CLAMPS this
// write to the last valid byte of the slot instead of reproducing the
// overflow — the only deliberate divergence from the binary, with no
// observable impact.
#pragma once
#include "Asset/FileUtil.h"
#include "Asset/ImgFile.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ts2::game {

// ---------------------------------------------------------------------------
// FORMAT 2: "quote-delimited" string table, fixed stride/cap.
// 1-based Get(i) access confirmed by disassembly for 003/005
// (StrTable003_Get 0x4C1AD0: this+41*i-37; StrTable005_Get 0x4C1D20:
// this+106*i-102 — both formulas reduce to `4 + STRIDE*(i-1)`, i.e.
// &rec[i-1][0]). For 002 (mGAMENOTICE), no dedicated accessor was found in
// the explored disassembly; the same formula is reused by analogy (identical
// memory layout, see TipsTable below).
// ---------------------------------------------------------------------------
template <int STRIDE, int CAP>
class QuoteStringTable {
public:
    // Reads an ENTIRE file into memory (no compression) and runs the quote
    // state machine over it. Fails if:
    //  - open/read fails;
    //  - more than CAP records are encountered;
    //  - a record exceeds STRIDE-1 useful characters (see clamp note above
    //    for the one deliberate divergence).
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
                    if (term >= STRIDE) term = STRIDE - 1; // defensive clamp (see file-header note)
                    rec_[count_++][term] = 0;
                } else {
                    inQuotes = true;
                    col = 0;
                }
            } else if (inQuotes) {
                if (col == STRIDE) return false; // original guard: record too long
                rec_[count_][col++] = static_cast<char>(c);
            }
        }
        return true;
    }

    uint32_t Count() const { return count_; }

    // 1-based index; static empty string out of bounds (like &String in the original).
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
// THIS is the table that must replace the game::Str(id)="#id" placeholder
// in Game/ClientRuntime.cpp: StrTable005::Get(id) already returns
// `const char*`, 1-based index, "" out of bounds — signature compatible as-is
// with `return StrTable005::Get(id);` usage (adapt to std::string if the
// caller requires it).
using StrTable005 = QuoteStringTable<106, 4000>;

// ---------------------------------------------------------------------------
// 002.DAT -> mGAMENOTICE (rotating announcement banner). Tips002_Load 0x4C1630
// loads the table AND initializes a rotation timer; Tips002_Rotate 0x4C1840
// (probable name) advances the index every 600 in-game seconds.
//
// Real memory layout confirmed by the offsets used in 0x4C1840: `this` is
// viewed as `float*`, and the rotation fields are at this+25251 /
// this+25252; and 25251*4 == 4 (count) + 1000*101 (rec[]) bytes EXACTLY
// -> struct { u32 count; char rec[1000][101]; float lastRotateTime; i32 currentIndex; }.
// `currentIndex` is initialized to -1 on load (the decompiler shows it as a
// NaN float because it shares the `float*` pointer: bit pattern
// 0xFFFFFFFF = -1 as i32 = NaN as f32); `lastRotateTime` is initialized to
// the game clock at load time (flt_815180 in the original).
// ---------------------------------------------------------------------------
class TipsTable {
public:
    // `nowSeconds` = game clock at load time (seeds the 600 s timer).
    // `trVariant` selects G01_GFONT\TR\002.DAT (else G01_GFONT\002.DAT),
    // matching the original's `if (dword_1669190 == 1)` test.
    bool Load(const std::string& gameDataDir, bool trVariant, float nowSeconds);

    uint32_t Count() const { return table_.Count(); }
    const char* Get(int index /*1-based*/) const { return table_.Get(index); }

    // Reproduces the rotation in 0x4C1840: if (now - lastRotateTime_) >= 600 s,
    // advances currentIndex_ (wraps to 0 when it reaches count) and returns true
    // (new banner ready to display via Current()).
    bool Advance(float nowSeconds);

    // currentIndex_ is stored 0-based internally (as in the original); Get()
    // expects a 1-based index, hence the +1.
    const char* Current() const { return Get(currentIndex_ + 1); }
    int32_t CurrentIndexZeroBased() const { return currentIndex_; }

private:
    QuoteStringTable<101, 1000> table_;
    float   lastRotateTime_ = 0.0f;
    int32_t currentIndex_ = -1;
};

// ---------------------------------------------------------------------------
// 001.DAT -> mBADWORD (banned-word dictionary, Dict001_Load 0x4C1170).
// The ONLY table of this set to go through the compressed [rawSize]
// [packedSize][zlib] envelope (same decoder as the .IMG files, Asset_DecompressImg
// 0x53F5E0 -> here asset::ImgFile). Decompressed payload = Korean CP949 words
// CRLF-separated (delimiter = byte 0x0D, the following 0x0A byte is skipped
// without inspection — reproduces the original's `++i`), 51-byte records.
// HARD integrity guards: payload.size()==11572 AND final count==1432,
// otherwise total failure (like the original, which rejects the whole manager).
// ---------------------------------------------------------------------------
class BannedWordDict {
public:
    // No TR variant: Dict001_Load never tests g_UseTRVariant.
    bool Load(const std::string& gameDataDir);

    uint32_t Count() const { return count_; }
    // 1-based index; "" out of bounds.
    const char* Word(int index) const {
        if (index < 1 || static_cast<uint32_t>(index) > count_) return "";
        return word_[index - 1];
    }

    // NOTE (documented gap): the real original scanner (candidate
    // "maybe_Dict001_MatchWord" sub_4C1410, EA 0x4C1410, OUT OF SCOPE for
    // this mission — not among the 5 EAs assigned) implements a
    // sliding-window search: it normalizes the input text by stripping
    // spaces (0x20) and copying DBCS pairs (lead byte >= 0x80) as-is, then
    // slides a window across THIS normalized text comparing it successively
    // against each dictionary word — BUT the window position is NEVER reset
    // between two words tried (unusual behavior, possibly an original
    // shortcut/bug, and its reads can slightly overrun the 1004-byte local
    // buffer without crashing the original binary thanks to stack padding).
    // Reproducing it identically would introduce memory-unsafe behavior in
    // modern C++ (no guaranteed stack margin the same way). IsBanned() below
    // is therefore a SAFE implementation, equivalent in INTENT (spaces
    // stripped, space-insensitive byte-for-byte substring search) but NOT
    // bit-exact with sub_4C1410 — reconfirm/report faithfully if a behavior
    // divergence is ever observed in-game.
    bool IsBanned(const std::string& text) const;

private:
    uint32_t count_ = 0;
    char word_[2000][51] = {};
};

// ---------------------------------------------------------------------------
// mFONTCOLOR — HARDCODED color palette (ColorTable_InitPalette
// 0x4C1D60, no file I/O, always returns true). Confirmed layout:
//   +0            u32 count = 45
//   +4..+180      colors[45]   (signed i32 ARGB, reinterpreted as uint32 0xAARRGGBB)
//   +184 (=this+46) ChannelIndices (8 x i32) = 1-based indices INTO colors[]
// The +184 block corresponds EXACTLY to the ChannelColorTable documented in
// Docs/TS2_CLIENT_SHELL.md at absolute address 0x84DFD8..0x84DFF4 (global
// instance mFONTCOLOR = unk_84DF20; 0x84DF20 + 184 = 0x84DFD8) — confirming
// that this table lives INSIDE the same object as the palette, not separately.
// ---------------------------------------------------------------------------
class ColorPalette {
public:
    struct ChannelIndices {
        int32_t system = 0, whisper = 0, party = 0, shout = 0;
        int32_t guild = 0, faction = 0, trade = 0, gm = 0; // 1-based indices into colors_
    };

    // ColorTable_InitPalette 0x4C1D60: hardcoded constants, cannot fail.
    bool InitPalette();

    // EA 0x4C1D6A (`*this = 45`): the counter is an object FIELD, not a
    // compile-time constant — it is 0 until InitPalette has run,
    // exactly like the binary's zero-initialized .data before App_Init.
    uint32_t Count() const { return count_; }

    // ColorTable_GetColor 0x4C1FE0 — the REAL accessor from the binary (FOUND; the
    // "inferred by analogy" note that used to be here is thus lifted):
    //     if (a2 >= 1 && a2 <= *this) return *(this + a2);  // EA 0x4C1FF5 / 0x4C2004
    //     else                        return -16777216;     // EA 0x4C1FF7
    // It confirms 1-based indexing AND fixes two points that were wrong here:
    //   - the out-of-bounds sentinel is 0xFF000000 (OPAQUE black), NOT 0 (TRANSPARENT
    //     black, which made text invisible instead of black);
    //   - the upper bound is the current count_ field, not the constant 45.
    // Called by UI_DrawNumberValue 0x53FCC0 (@0x53FCD2) and UI_DrawNumberCentered
    // 0x53FD00 (@0x53FD4B), both with this = 0x84DF20 (the mFONTCOLOR instance).
    uint32_t Get(int index) const {
        if (index < 1 || static_cast<uint32_t>(index) > count_) return 0xFF000000u; // EA 0x4C1FF7
        return static_cast<uint32_t>(colors_[index - 1]);                           // EA 0x4C2004
    }

    const ChannelIndices& Channels() const { return channel_; }
    // Directly resolves the ARGB color of a named chat channel.
    uint32_t ChannelColor(int32_t paletteIndex1Based) const { return Get(paletteIndex1Based); }

    // -----------------------------------------------------------------------
    // The 8 channel indices (this+46..+53, EA 0x4C1F5F..0x4C1FBA) RESOLVED to ARGB
    // via Get() — i.e. exactly what the binary does at DRAW time
    // (UI_GameHud_Render @0x68441E -> UI_DrawNumberValue 0x53FCC0 ->
    // ColorTable_GetColor 0x4C1FE0). All 8 absolute EAs are named in the IDB and
    // each one's semantics is proven by ITS consuming handler (xrefs):
    uint32_t SystemColor()  const { return Get(channel_.system);  } // g_SysMsgColor       0x84DFD8 idx15 — Msg_AppendSystemLine (everywhere)
    uint32_t WhisperColor() const { return Get(channel_.whisper); } // g_ChatColor_Whisper 0x84DFDC idx1  — Pkt_WhisperReceive 0x48F210 @0x48F2F1
    uint32_t PartyColor()   const { return Get(channel_.party);   } // g_ChatColor_Party   0x84DFE0 idx40 — Pkt_PartyChatOrInvite 0x48F3C0 @0x48F4D3
    uint32_t ShoutColor()   const { return Get(channel_.shout);   } // g_ChatColor_Shout   0x84DFE4 idx39 — Pkt_ShoutMessage 0x48F640 @0x48F72E
    uint32_t GuildColor()   const { return Get(channel_.guild);   } // g_ChatColor_Guild   0x84DFE8 idx36 — Net_OnGuildChatMessage 0x491420 @0x4914A1
    uint32_t FactionColor() const { return Get(channel_.faction); } // g_ChatColor_Faction 0x84DFEC idx37 — Net_OnFactionChatMessage 0x492FE0 @0x493061
    uint32_t TradeColor()   const { return Get(channel_.trade);   } // g_ChatColor_Trade   0x84DFF0 idx38 — Net_OnTradeChatMessage 0x4943F0 @0x49446E
    uint32_t GmColor()      const { return Get(channel_.gm);      } // g_ChatColor_Gm      0x84DFF4 idx45 — "[GM]" rule @0x48F2D0 / @0x48F70D

private:
    // Field order = binary layout (+0 count, +4 colors[45], +184 index).
    uint32_t count_ = 0;            // EA 0x4C1D6A: *this = 45
    int32_t colors_[45] = {};
    ChannelIndices channel_{};
};

// ---------------------------------------------------------------------------
// Facade: loads the 5 tables in the EXACT order used by App_Init 0x461C20
// (Dict001 -> Tips002 -> StrTable003 -> StrTable005 -> ColorTable). `gameDataDir`
// = "GameData" root; `nowSeconds` = game clock at load time (seeds the
// TipsTable rotation timer); `trVariant` selects the G01_GFONT\TR\ paths
// (002/003/005 only — 001.DAT has no TR variant).
//
// Unlike the original (which abandons all of App_Init on the first failure,
// see MessageBoxA "[Error::mXXX.Init()]"), this facade attempts to load
// ALL tables and returns false if at least one failed, leaving the others
// usable (same choice as Game/GameDatabase.cpp::LoadGameDatabases).
// ---------------------------------------------------------------------------
// VeryOldClient cross-check (class names only): the 5 IDA singletons (error string
// [Error::mXXX.Init()]) match EXACTLY the VeryOld classes (CONFIRMED, manager identity).
struct StringTables {
    BannedWordDict bannedWords; // 001.DAT -> mBADWORD    ; ex-VeryOldClient: BADWORD (GameData/CBADWORD.cpp) (CONFIRMED)
    TipsTable      notices;     // 002.DAT -> mGAMENOTICE ; ex-VeryOldClient: GAMENOTICE (GameData/CGAMENOTICE.cpp) (CONFIRMED)
    StrTable003    zoneNames;   // 003.DAT -> mZONENAME   ; ex-VeryOldClient: ZONENAME (GameData/CZONENAME.cpp) (CONFIRMED)
    StrTable005    messages;    // 005.DAT -> mMESSAGE  <- game::Str(id) must read HERE ; ex-VeryOldClient: MESSAGE (GameData/CGAMEMESSAGE.cpp) (CONFIRMED)
    ColorPalette   colors;      // hardcoded -> mFONTCOLOR ; ex-VeryOldClient: FONTCOLOR (GameConfig/CFONTCOLOR.cpp) (CONFIRMED)
};

bool LoadStringTables(StringTables& out, const std::string& gameDataDir,
                      float nowSeconds, bool trVariant = false);

// Single global instance (mirrors the mBADWORD/mGAMENOTICE/mZONENAME/
// mMESSAGE/mFONTCOLOR managers, same pattern as g_Guild/g_Warehouse/g_QuestProgress).
// Read by game::Str(id) (Game/ClientRuntime.cpp) once LoadStringTables() has
// been called from App::Init.
inline StringTables g_Strings;

} // namespace ts2::game

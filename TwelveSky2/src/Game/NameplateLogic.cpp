// Game/NameplateLogic.cpp — see NameplateLogic.h for the EA -> function table, the field
// provenance (offsets `this+N`), and fidelity notes (uncertain semantics of the
// affiliation/alliance sentinels, semantics of a2/a3/a4).
#include "Game/NameplateLogic.h"
#include <cstdio>

namespace ts2::game {

namespace {

// Crt_ftol 0x760810: truncation toward zero (convention established in Game/StatFormulas.h).
inline int Ftol(double v) { return static_cast<int>(v); }

// Enchant grade (Crt_ftol(enchantRaw * 0.01)) — pattern repeated as-is at 6 distinct
// call sites in Char_DrawNameplate.
inline std::string EnchantName(const NameplateHost& host, int enchantRaw) {
    const int grade = Ftol(enchantRaw * 0.01);
    return host.ResolveEnchantName ? host.ResolveEnchantName(grade + 1, enchantRaw) : std::string();
}

inline std::string Str(const NameplateHost& host, int id) {
    return host.ResolveString ? host.ResolveString(id) : std::string();
}

inline std::string TierPrefix(const NameplateHost& host, int rankTierValue) {
    return host.ResolveValueTierPrefix ? host.ResolveValueTierPrefix(rankTierValue) : std::string();
}

inline bool CanTarget(const NameplateHost& host, int element, int pkLevel, const std::string& affiliation) {
    return host.CanTargetOnMap ? host.CanTargetOnMap(element, pkLevel, affiliation) : false;
}

inline bool SameAffiliation(const NameplateHost& host, const std::string& affiliation) {
    return host.IsSameAffiliationAsLocal ? host.IsSameAffiliationAsLocal(affiliation) : false;
}

inline bool SameAlliance(const NameplateHost& host, const std::string& alliance) {
    return host.IsSameAllianceAsLocal ? host.IsSameAllianceAsLocal(alliance) : false;
}

template <typename... Args>
std::string Fmt(const char* fmt, Args... args) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    return std::string(buf);
}

float Distance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    const float dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Atlas frame index for an HP/MP bar — Char_DrawNameplate @0x56F029..@0x56F05F (HP)
// and @0x56F0A1..@0x56F0D7 (MP). Both bars share EXACTLY the same shape, only
// the 3 literals differ (see kHp*/kMp* in NameplateLogic.h):
//     if (cur <= 0) frame = empty;
//     else { frame = Crt_ftol((double)cur * 48.0 / (double)max) + base;
//            if (frame > maxFrame) frame = maxFrame; }
// Ftol = truncation toward zero (Crt_ftol 0x760810). NO low clamp in the binary (cur>0
// => ratio >= 0 => frame >= base), and NO max==0 guard: we neutralize the division by
// zero by treating max<=0 as the "empty" frame rather than reproducing an
// indeterminate value — same defensive convention as ComputeWeaponOverlayVariant
// (Game/EntityDrawLogic.cpp:126). This block is DEAD code anyway (see §DRAWMODE).
int BarAtlasFrame(int cur, int max, int emptyFrame, int baseFrame, int maxFrame) {
    if (cur <= 0 || max <= 0) return emptyFrame;
    const int frame = Ftol(static_cast<double>(cur) * kBarFrameSpan / static_cast<double>(max)) + baseFrame;
    return (frame > maxFrame) ? maxFrame : frame;
}

// "Market" palette (morph 324/342) — 0x56F9E0..0x56FC80 (case 324) / fixed 2 (case 342).
// pk in {0,1,2,3} -> {5,33,24,44}; also used as-is by the detailed title block
// (0x570212..0x5702F7) with pk replaced by `element` when morph != 324/342.
int MarketPaletteByRank(int rank, int fallbackColor) {
    switch (rank) {
        case 0: return kNameColorVipOrMarketCase0; // 5
        case 1: return kNameColorMarketCase1;       // 33
        case 2: return kNameColorMarketCase2;       // 24
        case 3: return kNameColorMarketCase3;       // 44
        // rank outside {0..3}: not reached in the binary (pkLevel/element bounded); the original
        // leaves v114 (variable SHARED with the main name line) unchanged in this case
        // -> faithfully reproduced by returning `fallbackColor` (color already computed for
        // mainLine) rather than an invented value.
        default: return fallbackColor;
    }
}

// Display sub-mode for morph==324, "hostile" branch (0x56F981..0x56FC94). Faithfully
// reproduces the 3 nested sub-switches on localFactionFlag (dword_1687320[0]).
// FIDELITY NOTE: in the binary, when factionFlag is nonzero but does NOT match ANY
// case of the sub-switch (1/2/3), the color variable stays UNINITIALIZED (no value
// is ever written on this exact path) — dead in practice (factionFlag observed bounded
// to {0,1,2,3} elsewhere, see Game/NpcInteraction.h::NpcQuestContext::factionFlag). Here we
// fall back to the same default as factionFlag==0 (instead of an indeterminate memory
// read): documented defensive choice, NOT a proven binary behavior.
int Morph324HostileColor(int factionSubMode, int factionFlag, int pkLevel) {
    switch (factionSubMode) {
        case 0:
            return kNameColorHostileHidden; // 2
        case 1:
            if (factionFlag) {
                switch (factionFlag) {
                    case 1: return (pkLevel > 1) ? kNameColorHostileHidden : kNameColorNeutral;
                    case 2: return (pkLevel == 2 || pkLevel == 3) ? kNameColorNeutral : kNameColorHostileHidden;
                    case 3: return (pkLevel == 2 || pkLevel == 3) ? kNameColorNeutral : kNameColorHostileHidden;
                    default: break;
                }
            }
            return (pkLevel > 1) ? kNameColorHostileHidden : kNameColorNeutral;
        case 2:
            if (factionFlag) {
                switch (factionFlag) {
                    case 1: return (pkLevel == 1 || pkLevel == 3) ? kNameColorNeutral : kNameColorHostileHidden;
                    case 2: return (pkLevel && pkLevel != 2) ? kNameColorHostileHidden : kNameColorNeutral;
                    case 3: return (pkLevel == 1 || pkLevel == 3) ? kNameColorNeutral : kNameColorHostileHidden;
                    default: break;
                }
            }
            return (pkLevel && pkLevel != 2) ? kNameColorHostileHidden : kNameColorNeutral;
        case 3:
            if (factionFlag) {
                switch (factionFlag) {
                    case 1: return (pkLevel == 1 || pkLevel == 2) ? kNameColorNeutral : kNameColorHostileHidden;
                    case 2: return (pkLevel == 1 || pkLevel == 2) ? kNameColorNeutral : kNameColorHostileHidden;
                    case 3: return (pkLevel && pkLevel != 3) ? kNameColorHostileHidden : kNameColorNeutral;
                    default: break;
                }
            }
            return (pkLevel && pkLevel != 3) ? kNameColorHostileHidden : kNameColorNeutral;
        default:
            // Not reached (dword_16760F4 in {0..3} only); safe default faithful to "hostile".
            return kNameColorHostileHidden;
    }
}

} // namespace

NameplateInfo ComputeNameplateInfo(const NameplateActor& actor,
                                    int drawMode,
                                    bool notSelf,
                                    const NameplateViewerContext& ctx,
                                    const NameplateHost& host) {
    NameplateInfo info;

    // ---- Global gate: 0x56ef96 ----
    // CAMERA near-cull (Target_IsBeyondClickRange 0x5410D0 — misleading original name, see the
    // banner comment for NameplateHost::IsBeyondCameraNearCull). y already offset by +10 = a1[1]+a2*0.5
    // with the a2=20.0 of call site @0x56EF96.
    const bool nearCullGate = host.IsBeyondCameraNearCull
        ? host.IsBeyondCameraNearCull(actor.x, actor.y + 10.0f, actor.z) // (float*)this+63, a2=20.0
        : true;
    if (!(actor.active && actor.hasIdentity && nearCullGate)) {
        return info; // visible=false, everything else default
    }
    info.visible = true;

    // ---- HP/MP bars: 0x56efbf..0x56f10a ----
    // ⚠️ DEAD PATH: `a2 == 1` never happens (dword_1668F64 never written — see
    // §DRAWMODE in the header). Block kept for formula fidelity; `visible` therefore
    // always stays false in practice and NO caller should draw these bars.
    if (drawMode == 1 && !notSelf && ctx.optShowNameplates) {
        const bool onScreen = host.IsOnScreen ? host.IsOnScreen(actor.x, actor.y - 2.0f, actor.z) : true;
        if (onScreen) {
            info.hpBar.visible = true;
            info.hpBar.current = actor.hpCur;
            info.hpBar.max = actor.hpMax;
            info.hpBar.atlasFrame = BarAtlasFrame(actor.hpCur, actor.hpMax,
                                                   kHpBarFrameEmpty, kHpBarFrameBase, kHpBarFrameMax);
            info.mpBar.visible = true;
            info.mpBar.current = actor.mpCur;
            info.mpBar.max = actor.mpMax;
            info.mpBar.atlasFrame = BarAtlasFrame(actor.mpCur, actor.mpMax,
                                                   kMpBarFrameEmpty, kMpBarFrameBase, kMpBarFrameMax);
        }
    }

    // ---- Name label anchor: 0x56f12b..0x56f1ac ----
    info.labelAnchorYOffset = (actor.hasTitleBarExtraHeight && !actor.suppressExtraNameHeight) ? 31.0f : 21.0f;
    const float labelWorldY = actor.y + info.labelAnchorYOffset;
    info.nameBlockVisible = host.IsOnScreen ? host.IsOnScreen(actor.x, labelWorldY, actor.z) : true;
    if (!info.nameBlockVisible) {
        return info;
    }

    std::string text;
    int color = kNameColorNeutral;

    // ---- "Market" group A: morph 270..274 — 0x56f1f4..0x56f41c ----
    if (ctx.localMorphNpcId == 270 || ctx.localMorphNpcId == 271 || ctx.localMorphNpcId == 272 ||
        ctx.localMorphNpcId == 273 || ctx.localMorphNpcId == 274) {
        if (actor.enchantRaw >= 1) {
            text = Fmt("%s(%s) [%d]", actor.name.c_str(), EnchantName(host, actor.enchantRaw).c_str(), actor.level);
        } else {
            text = Fmt("%s [%d]", actor.name.c_str(), actor.level);
        }
        color = (actor.pkLevel == 1) ? kNameColorMarketGroupA_Alt : kNameColorMarketGroupA_Std;
        if (actor.isGmAccount) color = kNameColorGmAccount;
        info.nameIconVariant = (actor.pkLevel == 1) ? 0 : 1; // 0=unk_8E9DD0, 1=unk_8E9E64
        info.mainLine.text = TierPrefix(host, actor.rankTierValue) + text;
        info.mainLine.color = color;
        if (actor.statusIconA > 0) info.statusIcons.push_back(NameplateIconSlot::A);
        return info; // faithful: this group does NOT enter the "detailed" block (dword_1668F64)
    }

    // ---- "Market" group B: morph 291 — 0x56f42c..0x56f62b ----
    if (ctx.localMorphNpcId == 291) {
        text = Fmt("%s[%d]", actor.name.c_str(), actor.level);
        if (!SameAffiliation(host, actor.affiliationName)) {
            color = kNameColorHostileHidden;
        } else if (!actor.allianceName.empty()) {
            color = SameAlliance(host, actor.allianceName) ? kNameColorAllianceAffiliate : kNameColorNeutral;
        } else {
            color = kNameColorNeutral;
        }
        if (actor.isGmAccount) color = kNameColorGmAccount;
        info.mainLine.text = TierPrefix(host, actor.rankTierValue) + text;
        info.mainLine.color = color;
        info.extraLines.push_back({actor.affiliationName, color}); // raw "%s", same color
        if (actor.statusIconA > 0) info.statusIcons.push_back(NameplateIconSlot::A);
        return info; // faithful: same here, no "detailed" block for this group
    }

    // ---- Normal branch: 0x56f679..end — gate a2!=1 || (hitmarkers && (gm || <=300)) ----
    const bool showNormal = (drawMode != 1) ||
        (ctx.optShowHitMarkers &&
         (ctx.localGmAuthLevel >= 1 ||
          Distance3D(actor.x, actor.y, actor.z, ctx.selfX, ctx.selfY, ctx.selfZ) <= 300.0f));
    if (!showNormal) {
        return info; // faithful: nothing else is drawn (a2==1, outside hitmarkers/GM/distance condition)
    }

    const bool hostile = (ctx.localGmAuthLevel <= 0) && CanTarget(host, actor.element, actor.pkLevel, actor.affiliationName);

    if (hostile) {
        // ---- Targetable enemy: real name hidden — 0x56f8e6..0x56ffbd ----
        if (ctx.localMorphNpcId == 324) {
            const std::string enchant = EnchantName(host, actor.enchantRaw);
            const std::string label = Str(host, 2685 + actor.pkLevel); // StrTable005_Get(lang, pkLevel+2685)
            text = Fmt("%s(%s)", label.c_str(), enchant.c_str());
            color = Morph324HostileColor(ctx.localFactionSubMode, ctx.localFactionFlag, actor.pkLevel);
        } else if (ctx.localMorphNpcId == 342) {
            const std::string enchant = EnchantName(host, actor.enchantRaw);
            const std::string label = Str(host, 2685 + actor.pkLevel);
            text = Fmt("%s(%s)", label.c_str(), enchant.c_str());
            color = kNameColorHostileHidden;
        } else {
            // Real name replaced by the element name (75/76/77/78 = elements 0..3) — PVP anonymity.
            int strId = 0;
            switch (actor.element) {
                case 0: strId = 75; break;
                case 1: strId = 76; break;
                case 2: strId = 77; break;
                case 3: strId = 78; break;
                default: strId = 75; break; // not reached in the binary (element in {0..3})
            }
            const std::string base = Str(host, strId);
            if (actor.enchantRaw >= 1) {
                text = Fmt("%s(%s)(%d)", base.c_str(), EnchantName(host, actor.enchantRaw).c_str(), actor.level);
            } else {
                text = Fmt("%s(%d)", base.c_str(), actor.level);
            }
            color = kNameColorHostileHidden;
        }
    } else {
        // ---- Ally / non-targetable: real name — 0x56f6c9..0x56f8cb ----
        if (actor.isAdminTitle) {
            if (actor.enchantRaw >= 1) {
                text = Fmt("%s(%s) %s", actor.name.c_str(), EnchantName(host, actor.enchantRaw).c_str(), Str(host, 72).c_str());
            } else {
                text = Fmt("%s %s", actor.name.c_str(), Str(host, 72).c_str());
            }
            color = actor.isAdminTitleAlt ? kNameColorAdminPrimary : kNameColorAdminSecondary;
        } else {
            if (actor.enchantRaw >= 1) {
                text = Fmt("%s(%s)", actor.name.c_str(), EnchantName(host, actor.enchantRaw).c_str());
            } else {
                text = actor.name;
            }
            color = 0;
            if (!actor.allianceName.empty() && SameAlliance(host, actor.allianceName) && notSelf) {
                color = kNameColorAllianceAffiliate;
            }
            if (!actor.affiliationName.empty() && SameAffiliation(host, actor.affiliationName) && notSelf) {
                color = kNameColorAffiliate; // tested AFTER alliance -> affiliation takes priority if both true
            }
            if (color == 0) color = kNameColorNeutral;
        }
    }

    // ---- Common overrides: 0x56ffd7..0x570008 ----
    if (actor.isVipOrHighlighted) color = kNameColorVipOrMarketCase0;
    if (actor.isGmAccount) color = kNameColorGmAccount;
    if (actor.isSpecialPkState) color = kNameColorMarketCase3;

    info.mainLine.text = TierPrefix(host, actor.rankTierValue) + text;
    info.mainLine.color = color;

    // ---- "Detailed" block (option dword_1668F64==1): 0x57008d..end ----
    if (ctx.optDetailedNameplates) {
        if (!actor.affiliationName.empty()) {
            std::string titleText;
            if (actor.titleKind == 0) {
                titleText = Str(host, 73) + " " + actor.affiliationName;
            } else if (actor.titleKind == 1) {
                titleText = Str(host, 74) + " " + actor.affiliationName;
            } else if (actor.titleKind == 2) {
                titleText = !actor.subAffiliationName.empty()
                    ? Fmt("(%s) %s", actor.subAffiliationName.c_str(), actor.affiliationName.c_str())
                    : actor.affiliationName;
            }
            // Otherwise (titleKind >= 3, not reached in the binary): line omitted (faithful: the
            // original buffer wouldn't be rewritten -> undefined behavior, we emit nothing).
            if (actor.titleKind <= 2) {
                int titleColor;
                if (CanTarget(host, actor.element, actor.pkLevel, actor.affiliationName)) {
                    const int rank = (ctx.localMorphNpcId == 324 || ctx.localMorphNpcId == 342) ? actor.pkLevel : actor.element;
                    titleColor = MarketPaletteByRank(rank, color);
                } else {
                    titleColor = kNameColorAffiliate; // 9
                }
                info.extraLines.push_back({titleText, titleColor});
            }
        }

        if (actor.isWhisperTarget) {
            info.extraLines.push_back({actor.whisperName, kNameColorWhisper}); // 7, literal
        } else if (actor.hasAltWhisperColor) {
            info.extraLines.push_back({actor.altWhisperName, ctx.chatColorWhisper});
        }

        if (actor.statusIconA > 0) info.statusIcons.push_back(NameplateIconSlot::A);
        if (actor.statusIconB > 0) info.statusIcons.push_back(NameplateIconSlot::B);
        if (actor.statusIconC > 0) info.statusIcons.push_back(NameplateIconSlot::C);
        if (actor.statusIconD > 0) info.statusIcons.push_back(NameplateIconSlot::D);
        if (actor.statusIconE > 0) info.statusIcons.push_back(NameplateIconSlot::E);
        if (!notSelf && ctx.partyFlagIndicatorCount > 0) info.statusIcons.push_back(NameplateIconSlot::PartyFlag);
        if (actor.statusIconF > 0) info.statusIcons.push_back(NameplateIconSlot::F);

        if (ctx.localGmAuthLevel > 0) {
            info.extraLines.push_back({Fmt("LEVEL::[%d]", actor.level + actor.levelBonus), kNameColorGmDebugLine});
            info.extraLines.push_back({Fmt("RANK::[%d]", actor.rankTierValue), kNameColorGmDebugLine});
            // TODO fidelity: the original uses g_EquipSnapshotScratch (last built
            // equipment snapshot, not necessarily up to date); here we use g_World.self/db
            // (current state) instead of the scratch — documented nuance, not an invented
            // formula (CalcEvasion itself is byte-exact, see Game/StatFormulas.h).
            const int evasion = CalcEvasion(g_World.self, g_World.db);
            info.extraLines.push_back({Fmt("CRKDEF::[%d]", evasion), kNameColorGmDebugLine});
        }
    }

    return info;
}

} // namespace ts2::game

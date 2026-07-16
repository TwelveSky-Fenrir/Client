// Game/NameplateLogic.cpp — voir NameplateLogic.h pour la table EA -> fonction, la provenance
// des champs (offsets `this+N`) et les notes de fidélité (sémantique incertaine des sentinels
// d'affiliation/alliance, sémantique de a2/a3/a4).
#include "Game/NameplateLogic.h"
#include <cstdio>

namespace ts2::game {

namespace {

// Crt_ftol 0x760810 : troncature vers zéro (convention établie dans Game/StatFormulas.h).
inline int Ftol(double v) { return static_cast<int>(v); }

// Grade d'enchantement (Crt_ftol(enchantRaw * 0.01)) — motif répété tel quel à 6 sites d'appel
// distincts dans Char_DrawNameplate.
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

// Index de frame d'atlas d'une barre PV/PM — Char_DrawNameplate @0x56F029..@0x56F05F (PV)
// et @0x56F0A1..@0x56F0D7 (PM). Les deux barres partagent EXACTEMENT la même forme, seuls
// les 3 littéraux changent (cf. kHp*/kMp* dans NameplateLogic.h) :
//     if (cur <= 0) frame = empty;
//     else { frame = Crt_ftol((double)cur * 48.0 / (double)max) + base;
//            if (frame > maxFrame) frame = maxFrame; }
// Ftol = troncature vers zéro (Crt_ftol 0x760810). AUCUN clamp bas dans le binaire (cur>0
// => ratio >= 0 => frame >= base), et AUCUNE garde max==0 : on neutralise la division par
// zéro en traitant max<=0 comme la frame « vide » plutôt que de reproduire une valeur
// indéterminée — même convention défensive que ComputeWeaponOverlayVariant
// (Game/EntityDrawLogic.cpp:126). Ce bloc est de toute façon MORT (cf. §DRAWMODE).
int BarAtlasFrame(int cur, int max, int emptyFrame, int baseFrame, int maxFrame) {
    if (cur <= 0 || max <= 0) return emptyFrame;
    const int frame = Ftol(static_cast<double>(cur) * kBarFrameSpan / static_cast<double>(max)) + baseFrame;
    return (frame > maxFrame) ? maxFrame : frame;
}

// ---------------------------------------------------------------------------
// Palette « marché » (morph 324/342) — 0x56F9E0..0x56FC80 (case 324) / fixe 2 (case 342).
// pk in {0,1,2,3} -> {5,33,24,44} ; utilisée aussi telle quelle par le bloc titre détaillé
// (0x570212..0x5702F7) avec pk remplacé par `element` quand morph != 324/342.
// ---------------------------------------------------------------------------
int MarketPaletteByRank(int rank, int fallbackColor) {
    switch (rank) {
        case 0: return kNameColorVipOrMarketCase0; // 5
        case 1: return kNameColorMarketCase1;       // 33
        case 2: return kNameColorMarketCase2;       // 24
        case 3: return kNameColorMarketCase3;       // 44
        // rank hors {0..3} : non atteint dans le binaire (pkLevel/element bornés) ; l'original
        // laisse v114 (variable PARTAGÉE avec la ligne de nom principale) inchangé dans ce cas
        // -> on reproduit fidèlement en retournant `fallbackColor` (couleur déjà calculée pour
        // mainLine) plutôt qu'une valeur inventée.
        default: return fallbackColor;
    }
}

// ---------------------------------------------------------------------------
// Sous-mode d'affichage morph==324, branche "hostile" (0x56F981..0x56FC94). Reproduit
// fidèlement les 3 sous-switches imbriqués sur localFactionFlag (dword_1687320[0]).
// NOTE FIDÉLITÉ : dans le binaire, quand factionFlag est non-nul mais NE correspond à AUCUN
// case du sous-switch (1/2/3), la variable de couleur reste NON INITIALISÉE (aucune valeur
// n'est jamais écrite sur ce chemin précis) — cas mort en pratique (factionFlag observé borné
// à {0,1,2,3} ailleurs, cf. Game/NpcInteraction.h::NpcQuestContext::factionFlag). On retombe ici
// sur le même repli que factionFlag==0 (au lieu d'une lecture mémoire indéterminée) : choix
// défensif documenté, PAS un comportement prouvé du binaire.
// ---------------------------------------------------------------------------
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
            // Non atteint (dword_16760F4 in {0..3} seulement) ; défaut sûr fidèle à "hostile".
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

    // ---- Porte globale : 0x56ef96 ----
    // Near-cull CAMÉRA (Target_IsBeyondClickRange 0x5410D0 — nom d'origine trompeur, cf. le
    // bandeau de NameplateHost::IsBeyondCameraNearCull). y déjà décalé de +10 = a1[1]+a2*0.5
    // avec l'a2=20.0 du site d'appel @0x56EF96.
    const bool nearCullGate = host.IsBeyondCameraNearCull
        ? host.IsBeyondCameraNearCull(actor.x, actor.y + 10.0f, actor.z) // (float*)this+63, a2=20.0
        : true;
    if (!(actor.active && actor.hasIdentity && nearCullGate)) {
        return info; // visible=false, tout le reste par défaut
    }
    info.visible = true;

    // ---- Barres PV/PM : 0x56efbf..0x56f10a ----
    // ⚠️ CHEMIN MORT : `a2 == 1` ne se produit jamais (dword_1668F64 jamais écrit — cf.
    // §DRAWMODE du header). Bloc conservé pour la fidélité de la formule ; `visible` reste
    // donc toujours false en pratique et AUCUN appelant ne doit dessiner ces barres.
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

    // ---- Ancre du libellé de nom : 0x56f12b..0x56f1ac ----
    info.labelAnchorYOffset = (actor.hasTitleBarExtraHeight && !actor.suppressExtraNameHeight) ? 31.0f : 21.0f;
    const float labelWorldY = actor.y + info.labelAnchorYOffset;
    info.nameBlockVisible = host.IsOnScreen ? host.IsOnScreen(actor.x, labelWorldY, actor.z) : true;
    if (!info.nameBlockVisible) {
        return info;
    }

    std::string text;
    int color = kNameColorNeutral;

    // ---- Groupe "marché" A : morph 270..274 — 0x56f1f4..0x56f41c ----
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
        return info; // fidèle : ce groupe n'entre PAS dans le bloc "détaillé" (dword_1668F64)
    }

    // ---- Groupe "marché" B : morph 291 — 0x56f42c..0x56f62b ----
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
        info.extraLines.push_back({actor.affiliationName, color}); // "%s" brut, même couleur
        if (actor.statusIconA > 0) info.statusIcons.push_back(NameplateIconSlot::A);
        return info; // fidèle : idem, pas de bloc "détaillé" pour ce groupe
    }

    // ---- Branche normale : 0x56f679..fin — gate a2!=1 || (hitmarkers && (gm || <=300)) ----
    const bool showNormal = (drawMode != 1) ||
        (ctx.optShowHitMarkers &&
         (ctx.localGmAuthLevel >= 1 ||
          Distance3D(actor.x, actor.y, actor.z, ctx.selfX, ctx.selfY, ctx.selfZ) <= 300.0f));
    if (!showNormal) {
        return info; // fidèle : rien d'autre n'est dessiné (a2==1, hors condition hitmarkers/GM/distance)
    }

    const bool hostile = (ctx.localGmAuthLevel <= 0) && CanTarget(host, actor.element, actor.pkLevel, actor.affiliationName);

    if (hostile) {
        // ---- Cible ennemie ciblable : nom réel masqué — 0x56f8e6..0x56ffbd ----
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
            // Nom réel remplacé par le nom d'élément (75/76/77/78 = éléments 0..3) — anonymat PVP.
            int strId = 0;
            switch (actor.element) {
                case 0: strId = 75; break;
                case 1: strId = 76; break;
                case 2: strId = 77; break;
                case 3: strId = 78; break;
                default: strId = 75; break; // non atteint dans le binaire (element in {0..3})
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
        // ---- Allié / non-ciblable : nom réel — 0x56f6c9..0x56f8cb ----
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
                color = kNameColorAffiliate; // teste APRÈS l'alliance -> priorité affiliation si les 2 vrais
            }
            if (color == 0) color = kNameColorNeutral;
        }
    }

    // ---- Overrides communs : 0x56ffd7..0x570008 ----
    if (actor.isVipOrHighlighted) color = kNameColorVipOrMarketCase0;
    if (actor.isGmAccount) color = kNameColorGmAccount;
    if (actor.isSpecialPkState) color = kNameColorMarketCase3;

    info.mainLine.text = TierPrefix(host, actor.rankTierValue) + text;
    info.mainLine.color = color;

    // ---- Bloc "détaillé" (option dword_1668F64==1) : 0x57008d..fin ----
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
            // Sinon (titleKind >= 3, non atteint dans le binaire) : ligne omise (fidèle : le
            // buffer d'origine ne serait pas réécrit -> comportement non défini, on n'émet rien).
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
            info.extraLines.push_back({actor.whisperName, kNameColorWhisper}); // 7, littéral
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
            // TODO fidélité : l'original utilise g_EquipSnapshotScratch (dernier snapshot
            // d'équipement construit, pas forcément à jour) ; on utilise ici g_World.self/db
            // (état courant), au lieu du scratch — nuance documentée, pas une invention de
            // formule (CalcEvasion elle-même est byte-exacte, cf. Game/StatFormulas.h).
            const int evasion = CalcEvasion(g_World.self, g_World.db);
            info.extraLines.push_back({Fmt("CRKDEF::[%d]", evasion), kNameColorGmDebugLine});
        }
    }

    return info;
}

} // namespace ts2::game

// Game/CharSelectFlow.cpp — implémentation. Voir CharSelectFlow.h pour les EA
// d'origine, le détail du flux découvert et les écarts/TODO assumés.
#include "Game/CharSelectFlow.h"
#include <windows.h> // MultiByteToWideChar — ValidateNameCharset() (Str_ValidateNameChars 0x53FD70)

namespace ts2::game {

// Str_ValidateNameChars 0x53FD70 — voir la documentation complète en tête de
// déclaration dans CharSelectFlow.h (bornes/plages EXACTES décompilées).
bool ValidateNameCharset(const std::string& name) {
    // Capacité FIXE de 13 WCHAR (== 12 caractères utiles + NUL), EXACTEMENT comme
    // l'original : `MultiByteToWideChar(0, 0, name, -1, &buf, 13)`.
    wchar_t wbuf[13] = {};
    const int written = MultiByteToWideChar(CP_ACP, 0, name.c_str(), -1, wbuf, 13);
    if (written == 0) return false; // encodage invalide OU nom > 12 caracteres utiles

    for (const wchar_t* p = wbuf; *p; ++p) {
        const unsigned c = static_cast<unsigned>(*p);
        const bool digit = (c >= 0x30 && c <= 0x39);
        const bool upper = (c >= 0x41 && c <= 0x5A);
        const bool lower = (c >= 0x61 && c <= 0x7A);
        const bool thai  = (c >= 0x0E00 && c <= 0x0E7F);
        if (!digit && !upper && !lower && !thai) return false;
    }
    return true; // fidele : un nom VIDE traverse cette boucle et renvoie true ici
}

namespace {

constexpr int32_t kInitWaitFrames          = 30;   // 0x1E — délai avant (re)calcul Init
constexpr int32_t kKeepAliveIntervalFrames = 30;   // 0x1E — Net_AccountKeepAlive
constexpr float   kDefaultEnterPreviewFrames = 30.0f; // TODO fidélité : durée par défaut
                                                       // si host.GetEnterPreviewDurationFrames
                                                       // est absent (le binaire tire la
                                                       // vraie durée de l'animation squelette
                                                       // via PcModel_ResolveSlotAndApply,
                                                       // non modélisée ici, cf. header).

// StrTable005 ids EXACTS relevés au désassemblage.
constexpr int32_t kStrSessionExpired      = 20;
constexpr int32_t kStrEnterInvalidName    = 1856;
constexpr int32_t kStrEnterNoSelection    = 47;
constexpr int32_t kStrCreateListFull      = 22;
constexpr int32_t kStrCreateEmptyName     = 38; // GetWindowTextA==0, EA 0x5265a5
constexpr int32_t kStrCreateInvalidChars  = 39; // !Str_ValidateNameChars, EA 0x5265db
constexpr int32_t kStrCreateBannedWord    = 40; // maybe_Dict001_MatchWord, EA 0x526611
constexpr int32_t kStrCreateSuccess       = 41;
constexpr int32_t kStrCreateFail42        = 42; // -> Locked
constexpr int32_t kStrCreateFail43        = 43;
constexpr int32_t kStrCreateFail701       = 701;
constexpr int32_t kStrCreateFail44        = 44; // -> Locked
constexpr int32_t kStrCreateFail45        = 45; // -> Locked
constexpr int32_t kStrDeleteSuccess       = 50;
constexpr int32_t kStrDeleteFail51        = 51; // -> Locked
constexpr int32_t kStrDeleteFail411       = 411;
constexpr int32_t kStrDeleteFail48        = 48;
constexpr int32_t kStrDeleteFail633       = 633;
constexpr int32_t kStrDeleteFail2091      = 2091;
constexpr int32_t kStrDeleteFail52        = 52; // -> Locked
constexpr int32_t kStrDeleteFail53        = 53; // -> Locked
constexpr int32_t kStrEnterInfoFail55     = 55; // -> retour Idle (PAS Locked)
constexpr int32_t kStrEnterInfoFail1347   = 1347; // -> Locked
constexpr int32_t kStrEnterInfoFail1348   = 1348; // -> Locked
constexpr int32_t kStrEnterInfoFail229    = 229;  // -> Locked
constexpr int32_t kStrEnterInfoFail56     = 56;   // -> Locked (code 101)
constexpr int32_t kStrEnterInfoFail57     = 57;   // -> Locked (code 102)
constexpr int32_t kStrConnectFail59       = 59;   // -> Locked
constexpr int32_t kStrConnectFail60       = 60;   // -> Locked
constexpr int32_t kStrConnectFail61       = 61;   // -> annulable
constexpr int32_t kStrConnectFail62       = 62;   // -> annulable
constexpr int32_t kStrConnectFail63       = 63;   // -> annulable
constexpr int32_t kStrConnectFail64       = 64;   // -> Locked
constexpr int32_t kStrConnectFail65       = 65;   // -> Locked
constexpr int32_t kStrCancelSessionExpired = 20;  // réutilisation de str 20

inline void Notice(const CharSelectHost& host, int32_t strId) {
    if (host.ShowNotice) host.ShowNotice(strId);
}

inline void Lock(CharSelectState& s) {
    s.subState     = CharSelectSubState::Locked;
    s.frameCounter = 0;
}

// Table EXACTE de préréglage d'apparence (job,variant) -> id, relevée à 0x5266CD-
// 0x526760 (Scene_CharSelectOnMouseUp, bouton "Confirmer" du formulaire de création).
// RE-VÉRIFIÉE bit-exacte par décompilation directe (mcp__idaTs2__disasm, session
// 2026-07-14, audit CharSelect UI/flux) : switch externe sur `dword_16709DC` (job,
// EA 0x52663C-0x526666) -> 3 blocs, chacun un switch interne sur `this[15716]`
// (accédé `[this+0xF590]`, EA 0x526671/0x5266C7/0x52671A ; 0xF590/4 = 15716 = le
// champ `variant` d'origine, confirmation de l'identité du champ) écrivant
// `dword_1670A90` = 5/6/7 (job 0, EA 0x52669A/0x5266A6/0x5266B2), 11/12/13 (job 1,
// EA 0x5266F0/0x5266FC/0x526708), 17/18/19 (job 2, EA 0x526743/0x52674F/0x52675B).
// Table EXACTEMENT identique à celle ci-dessous.
int32_t ResolveLookPresetId(int32_t job, int32_t variant) {
    static constexpr int32_t kTable[3][3] = {
        {5, 6, 7},
        {11, 12, 13},
        {17, 18, 19},
    };
    if (job < 0 || job > 2 || variant < 0 || variant > 2) return 0; // hors bornes : non atteint normalement
    return kTable[job][variant];
}

// Réinitialise l'aperçu 3D à Idle (this[15717..15725]=0, this[15718]=1) — motif répété
// à plusieurs points du binaire (Init, annulation de création, annulation d'entrée...).
void ResetPreviewToIdle(CharSelectState& s) {
    s.previewMotion      = PreviewMotion::Idle;
    s.previewElapsed      = 0.0f;
    s.enterSequenceFired = false;
}

// Recalcule allSlotsFull + la sélection par défaut (emplacement occupé de plus haute
// "puissance", premier occupé en cas d'égalité/absence de comparaison), fidèle au
// sous-état Init du binaire (EA 0x51C2CA-0x51C356).
void RecomputeDefaultSelection(CharSelectState& s) {
    int32_t occupiedCount = 0;
    s.selectedSlot = -1;
    for (int32_t i = 0; i < kMaxCharSlots; ++i) {
        if (!s.slots[static_cast<size_t>(i)].occupied) continue;
        ++occupiedCount;
        if (s.selectedSlot == -1) {
            s.selectedSlot = i;
        } else if (s.slots[static_cast<size_t>(i)].power > s.slots[static_cast<size_t>(s.selectedSlot)].power) {
            s.selectedSlot = i;
        }
    }
    s.allSlotsFull = (occupiedCount >= kMaxCharSlots);
}

int32_t FindFirstFreeSlot(const CharSelectState& s) {
    for (int32_t i = 0; i < kMaxCharSlots; ++i) {
        if (!s.slots[static_cast<size_t>(i)].occupied) return i;
    }
    return -1;
}

// Séquence réseau "Entrer en jeu" — cf. header pour le détail complet des codes.
// Appelée UNE SEULE FOIS quand le minuteur d'aperçu "Entrée" arrive à complétion.
void FireEnterSequence(CharSelectState& s, const CharSelectHost& host) {
    s.enterSequenceFired = true;
    const int32_t slot = s.selectedSlot;

    EnterCharInfoResult info{};
    if (host.RequestEnterCharInfo) info = host.RequestEnterCharInfo(slot);

    switch (info.resultCode) {
        case 0: {
            const int32_t connectResult = host.ConnectToGameServer
                ? host.ConnectToGameServer(info.domainId, info.gamePort)
                : 101;
            switch (connectResult) {
                case 0:
                    s.pendingTransition = CharSelectTransition::EnterWorld;
                    s.enterWorldZoneId  = info.zoneId; // AUTHENTIQUE, cf. header
                    s.enterWorldSlot    = slot;
                    return; // this[0]=5 : l'appelant applique la transition, pas de reset ici
                case 1: Notice(host, kStrConnectFail59); Lock(s); return;
                case 2: Notice(host, kStrConnectFail60); Lock(s); return;
                case 3:
                case 4:
                case 5: {
                    const int32_t connStr = (connectResult == 3) ? kStrConnectFail61
                                           : (connectResult == 4) ? kStrConnectFail62
                                                                   : kStrConnectFail63;
                    Notice(host, connStr);
                    const int32_t cancelResult = host.CancelEnter ? host.CancelEnter() : 0;
                    if (cancelResult != 0) {
                        if (cancelResult == 101) {
                            Notice(host, kStrCancelSessionExpired);
                            Lock(s);
                        }
                        // ÉCART D'ORIGINE ASSUMÉ : cancelResult != 0 && != 101 -> AUCUN
                        // changement d'état (le binaire ne fait littéralement rien dans
                        // cette branche, EA 0x51C94B-0x51C955 et analogues). Reproduit
                        // tel quel : l'aperçu reste bloqué en Entering.
                    } else {
                        ResetPreviewToIdle(s); // nouvelle tentative possible
                    }
                    return;
                }
                case 6: Notice(host, kStrConnectFail64); Lock(s); return;
                case 7: Notice(host, kStrConnectFail65); Lock(s); return;
                default: return; // code inconnu : no-op fidèle (`default: return;`)
            }
        }
        case 1:
            // Échec de ReqEnterCharInfo lui-même : PAS verrouillant, nouvelle tentative
            // possible (seul cas d'échec "doux" de toute la séquence d'entrée).
            Notice(host, kStrEnterInfoFail55);
            ResetPreviewToIdle(s);
            return;
        case 2: Notice(host, kStrEnterInfoFail1347); Lock(s); return;
        case 3: Notice(host, kStrEnterInfoFail1348); Lock(s); return;
        case 4: Notice(host, kStrEnterInfoFail229); Lock(s); return;
        case 101: Notice(host, kStrEnterInfoFail56); Lock(s); return;
        case 102: Notice(host, kStrEnterInfoFail57); Lock(s); return;
        default: return; // code inconnu : no-op fidèle
    }
}

// Fait avancer le minuteur d'aperçu "Entrée" ; déclenche FireEnterSequence() une seule
// fois à complétion (garde enterSequenceFired, simplification du double-buffer
// g_MorphInProgress d'origine — cf. TODO header).
void AdvanceEnterSequenceIfDue(CharSelectState& s, const CharSelectHost& host, float dt) {
    if (s.screen != CharSelectScreen::List) return;
    if (s.previewMotion != PreviewMotion::Entering) return;
    if (s.enterSequenceFired) return;

    s.previewElapsed += dt * 30.0f; // fidèle : a2*30.0 (accumulation en "frames")
    const float duration = host.GetEnterPreviewDurationFrames
        ? host.GetEnterPreviewDurationFrames(s.selectedSlot)
        : kDefaultEnterPreviewFrames;
    if (s.previewElapsed >= duration) {
        FireEnterSequence(s, host);
    }
}

} // namespace

CharSelectTransition UpdateCharSelect(CharSelectState& state, const CharSelectHost& host, float dt) {
    switch (state.subState) {
    case CharSelectSubState::Init: {
        ++state.frameCounter;
        if (state.frameCounter < kInitWaitFrames) {
            return CharSelectTransition::None;
        }
        state.frameCounter = 0;

        if (host.LoadCharacterSlots) host.LoadCharacterSlots(state.slots);
        RecomputeDefaultSelection(state);

        state.screen = CharSelectScreen::List;
        ResetPreviewToIdle(state);

        state.subState = CharSelectSubState::Active;
        return CharSelectTransition::None;
    }
    case CharSelectSubState::Active: {
        ++state.frameCounter;
        if (state.frameCounter % kKeepAliveIntervalFrames == 0) {
            const int32_t keepAlive = host.AccountKeepAlive ? host.AccountKeepAlive() : 0;
            if (keepAlive == 101) {
                Notice(host, kStrSessionExpired);
                Lock(state);
                return CharSelectTransition::None;
            }
        }
        // Heartbeat anti-triche (Ac_GameGuard_Heartbeat, /30 frames) IGNORÉ ICI —
        // hors périmètre CLAUDE.md (le binaire ferait g_QuitFlag=1 en cas d'échec).

        AdvanceEnterSequenceIfDue(state, host, dt);
        // Rotation caméra de l'écran CreateForm (this[15]/this[16]) : PUREMENT visuel,
        // non modélisée (TODO rendu, cf. header).

        if (state.pendingTransition != CharSelectTransition::None) {
            const CharSelectTransition t = state.pendingTransition;
            state.pendingTransition = CharSelectTransition::None;
            return t;
        }
        return CharSelectTransition::None;
    }
    case CharSelectSubState::Locked:
    default:
        return CharSelectTransition::None; // terminal, aucun traitement (fidèle)
    }
}

void SelectCharacterSlot(CharSelectState& state, int32_t slotIndex) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.screen != CharSelectScreen::List) return;
    if (slotIndex < 0 || slotIndex >= kMaxCharSlots) return;
    if (!state.slots[static_cast<size_t>(slotIndex)].occupied) return;
    if (slotIndex == state.selectedSlot) return;

    state.selectedSlot   = slotIndex;
    state.previewElapsed = 0.0f; // fidèle : EA 0x5226b9 (reset avant sélection du nouveau)
}

void OnCreateButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.screen != CharSelectScreen::List) return;
    if (state.previewMotion == PreviewMotion::Entering) return;

    if (FindFirstFreeSlot(state) == -1) {
        Notice(host, kStrCreateListFull);
        return;
    }
    // TODO fidélité NON reproduit : garde régionale `this[15374]!=40 || g_ServerModeFlag`
    // (notice[2163] sinon) — variante de build non confirmée, cf. header.

    state.createForm = CharCreateForm{};
    state.createForm.job = host.RandomInitialJob ? (host.RandomInitialJob() % 3) : 0;
    state.screen = CharSelectScreen::CreateForm;
    ResetPreviewToIdle(state);
}

namespace {
inline int32_t Clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

void SetCreateJob(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    const int32_t next = Clamp(state.createForm.job + delta, 0, 2);
    if (next == state.createForm.job) return;
    state.createForm.job        = next;
    state.createForm.faction    = 0; // fidèle : le job réinitialise les sous-options
    state.createForm.face       = 0;
    state.createForm.hairColor = 0;
    state.previewElapsed        = 0.0f;
}

void SetCreateFaction(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    state.createForm.faction = Clamp(state.createForm.faction + delta, 0, 1);
}

void SetCreateFace(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    state.createForm.face = Clamp(state.createForm.face + delta, 0, 6);
}

void SetCreateHairColor(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    state.createForm.hairColor = Clamp(state.createForm.hairColor + delta, 0, 2);
}

void SetCreateVariant(CharSelectState& state, int32_t delta) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    state.createForm.variant = Clamp(state.createForm.variant + delta, 0, 2);
}

bool ConfirmCreateCharacter(CharSelectState& state, const CharSelectHost& host) {
    if (state.screen != CharSelectScreen::CreateForm) return false;

    // Ordre EXACT du binaire (EA 0x52658f-0x526623) : vide -> notice[38] ; caractères
    // refusés -> notice[39] ; mot banni -> notice[40]. Le nom est retenu tel que saisi
    // (non tronqué ici) — la borne dure à 12 caractères utiles fait partie intégrante
    // de ValidateNameCharset()/host.ValidateNameChars (échec de conversion), fidèle au
    // buffer 13 WCHAR de Str_ValidateNameChars 0x53FD70.
    const std::string name = host.GetEditedName ? host.GetEditedName() : std::string{};
    if (name.empty()) {
        Notice(host, kStrCreateEmptyName); // notice[38]
        return false;
    }
    if (host.ValidateNameChars && !host.ValidateNameChars(name)) {
        Notice(host, kStrCreateInvalidChars); // notice[39]
        return false;
    }
    if (host.IsNameBanned && host.IsNameBanned(name)) {
        Notice(host, kStrCreateBannedWord); // notice[40]
        return false;
    }

    state.createForm.name = name;
    const int32_t slot = FindFirstFreeSlot(state);
    if (slot == -1) {
        Notice(host, kStrCreateListFull);
        return false;
    }

    const int32_t presetId = ResolveLookPresetId(state.createForm.job, state.createForm.variant);
    const int32_t code = host.CreateCharacter
        ? host.CreateCharacter(slot, state.createForm, presetId)
        : 101;

    switch (code) {
        case 0:
            Notice(host, kStrCreateSuccess);
            state.slots[static_cast<size_t>(slot)].occupied = true;
            state.screen        = CharSelectScreen::List;
            state.selectedSlot  = slot;
            ResetPreviewToIdle(state);
            return true;
        case 1: Notice(host, kStrCreateFail42); Lock(state); return false;
        case 2: Notice(host, kStrCreateFail43); return false;
        case 3: Notice(host, kStrCreateFail701); return false;
        case 0x65: Notice(host, kStrCreateFail44); Lock(state); return false;
        case 0x66: Notice(host, kStrCreateFail45); Lock(state); return false;
        default: return false; // code inconnu : no-op fidèle
    }
}

void CancelCreateForm(CharSelectState& state) {
    if (state.screen != CharSelectScreen::CreateForm) return;
    state.screen = CharSelectScreen::List; // this[15715] (sélection) INCHANGÉ, fidèle
    ResetPreviewToIdle(state);
}

bool OnDeleteButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false;
    // TODO fidélité NON reproduit : garde régionale this[15374]==60 (bouton absent),
    // cf. header.

    if (state.selectedSlot == -1) {
        Notice(host, kStrEnterNoSelection); // notice[47], partagée avec le bouton Entrer
        return false;
    }

    if (host.ShowDeleteConfirm) host.ShowDeleteConfirm();
    return true;
}

void ConfirmDeleteCharacter(CharSelectState& state, const CharSelectHost& host) {
    if (state.selectedSlot == -1) return;
    const int32_t slot = state.selectedSlot;
    const int32_t code = host.DeleteCharacter ? host.DeleteCharacter(slot) : 101;

    switch (code) {
        case 0:
            Notice(host, kStrDeleteSuccess);
            state.slots[static_cast<size_t>(slot)] = CharSlotInfo{};
            state.selectedSlot = -1;
            state.allSlotsFull = false;
            return;
        case 1: Notice(host, kStrDeleteFail51); Lock(state); return;
        case 2: Notice(host, kStrDeleteFail411); return;
        case 3: Notice(host, kStrDeleteFail48); return;
        case 4: Notice(host, kStrDeleteFail633); return;
        case 5: Notice(host, kStrDeleteFail2091); return;
        case 0x65: Notice(host, kStrDeleteFail52); Lock(state); return;
        case 0x66: Notice(host, kStrDeleteFail53); Lock(state); return;
        default: return; // code inconnu : no-op fidèle
    }
}

bool OnEnterButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return false;
    if (state.screen != CharSelectScreen::List) return false;
    if (state.previewMotion == PreviewMotion::Entering) return false;

    if (state.selectedSlot == -1) {
        Notice(host, kStrEnterNoSelection); // notice[47]
        return false;
    }

    const bool gmAuth = host.HasGmAuthLevel && host.HasGmAuthLevel();
    const bool nameOk  = gmAuth
        || !host.IsCharacterNameValid // nullptr => true (fidèle au host par défaut)
        || host.IsCharacterNameValid(state.selectedSlot);
    if (!nameOk) {
        Notice(host, kStrEnterInvalidName); // notice[1856]
        return false;
    }

    state.previewMotion      = PreviewMotion::Entering;
    state.previewElapsed      = 0.0f;
    state.enterSequenceFired = false;
    return true;
}

void OnQuitButtonClicked(CharSelectState& state, const CharSelectHost& host) {
    if (state.subState != CharSelectSubState::Active) return;
    if (state.previewMotion == PreviewMotion::Entering) return;
    if (host.CloseConnectionAndQuit) host.CloseConnectionAndQuit();
}

} // namespace ts2::game

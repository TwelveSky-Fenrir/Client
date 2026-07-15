// Net/WorldEntityDispatch.h — mega-dispatcher de l'opcode reseau 0x5e
// (Net_OnWorldEntityDispatch, EA 0x494870, ~62337 o de code d'origine).
//
// ===========================================================================
//  CARTE DU DISPATCHER (source : idaTs2, decompilation Hex-Rays de 0x494870)
// ===========================================================================
//
//  EN-TETE (0x49488F..0x4948AD) — le payload est recopie en 2 blocs vers des
//  locales (fidele a la table de tailles reseau : t[0x5e] = 105 = 1(opcode) +
//  4(subOpcode) + 100(bloc brut), cf. Net/PacketDispatch.h) :
//     payload[0..3]   -> v736 : SOUS-OPCODE (switch principal, ~300 valeurs).
//     payload[4..103] -> v702[0..99] : bloc brut, reinterprete DIFFEREMMENT
//                        par sous-cas (le plus souvent : u32 index a +0, u32
//                        parametre secondaire a +4 ; parfois un tag 13 o).
//  default du switch = return (no-op). C'est le comportement pour tout
//  sous-opcode non liste ci-dessous — donc ABSENT de couverture == NO-OP
//  fidele (identique a l'etat "non enregistre" d'avant cet audit).
//
//  ROLE GENERAL : notifications d'ETAT D'ACTIVATION des competences de combo/
//  posture/buff du joueur LOCAL, keyees par une famille d'arme/categorie
//  (argument "weaponType"/"branch" de Skill_GetComboMotionId /
//  Skill_GetSpecialMotionId / Skill_GetBuffMotionId, cf. Game/SkillCombat.h
//  deja cablees), avec un indice de competence dans la famille. Le nom
//  "WorldEntityDispatch" (choisi par le renommage IDA d'origine du projet)
//  reste large : au-dela des familles de combo (couvertes ici), d'autres
//  plages de sous-opcodes non couvertes concernent aussi des notifications
//  de zone/monde (cf. tableau "PLAGES NON COUVERTES" plus bas).
//
//  STRUCTURE MECANIQUE DES FAMILLES DE COMBO (confirmee sur les familles 1, 2
//  ET 3, sous-opcodes 1..30 -- familles 1/2 = 2 x 9 slots identiques ; la
//  famille 3 (19..30) reprend EXACTEMENT les slots 1..6 (19..24) puis 7..9
//  (28..30), mais insere 3 sous-cas SUPPLEMENTAIRES sans analogue (25..27,
//  cf. bloc "Familles CONFIRMEES" plus bas) entre les deux -- 12 sous-opcodes
//  au total pour cette famille, PAS 9 comme 1/2) :
//    slot 1 (n)   : PAS d'ecriture d'etat. Si Skill_IsAvailableByLevel(motion)
//                   -> message "<nom> [<argSecondaire>]<suffixe str231>".
//    slot 2 (n+1) : etat[idx]=1. Si disponible -> message "<nom> <str232>" +
//                   (option FilterWorldEntity) popup "<nom><str242>".
//    slot 3 (n+2) : etat[idx]=2. Si disponible -> message "<nom> <str233>".
//    slot 4 (n+3) : etat[idx]=3, (option) argSecondaire. Si motion==morph
//                   courant -> message "<nom> <str234>" + arme la barre de
//                   charge partagee (dword_1675BA4/flt_1675BA8) et un
//                   drapeau flag0/flag1 PROPRE A LA FAMILLE (1675BAC/1675BB0
//                   famille 1, 1675BCC/1675BD0 famille 2) ; famille 1 ecrit
//                   en plus dword_1675BB4 = argSecondaire/2 (famille 2 ne le
//                   fait pas -- asymetrie confirmee dans le desasm).
//    slot 5 (n+4) : etat[idx]=5, horodatage[idx]=Time_GetMonthDayInt(). Pas
//                   de message (juste "amorce" le cache de disponibilite).
//    slot 6 (n+5) : etat[idx]=5, horodatage[idx]=horodatage. Si motion==morph
//                   courant -> message + retour ville de faction
//                   (Map_BeginWarpToFactionTown). Id de suffixe DIFFERENT par
//                   famille (str860 famille 1, str235 famille 2).
//    slot 7 (n+6) : etat[idx]=4, horodatage[idx]=horodatage, argSecondaire =
//                   id d'element a apparier. Si disponible -> resout
//                   Char_GetPairedElement ; message avec 1 ou 2 etiquettes de
//                   classe selon appariement (str236).
//    slot 8 (n+7) : etat[idx]=5. Si motion==morph courant -> message
//                   "<nom> <str237>" + retour ville de faction.
//    slot 9 (n+8) : etat[idx]=0. Si disponible -> rien (reset silencieux).
//
//  Familles CONFIRMEES par lecture complete du desassemblage :
//    famille 1 (sous-opcodes 1..9)   : Skill_GetComboMotionId(1, idx) ; etat =
//        dword_1685EAC[idx], horodatage = dword_1685EE0[idx].
//    famille 2 (sous-opcodes 10..18) : Skill_GetComboMotionId(2, idx) ; etat =
//        dword_1685F14[idx], horodatage = dword_1685F2C[idx].
//    famille 3 (sous-opcodes 19..30) : Skill_GetComboMotionId(3, idx) ; etat =
//        dword_1685F44[idx], horodatage = dword_1685F6C[idx]. CABLEE
//        INTEGRALEMENT (lecture complete du desassemblage, EA 0x4958a0..
//        0x49636d) :
//          - slots 1..6 (19..24) : forme identique aux familles 1/2 (strSlot6
//            =235 comme famille 2 ; slot4/22 sans demi-duree, comme famille 2 ;
//            flags de charge propres dword_1675BD4/1675BD8).
//          - 25..27 : 3 sous-cas SANS ANALOGUE en familles 1/2, intercales entre
//            slot6 et slot7. Gate = motion==morph courant (CALCULABLE, PAS de
//            SkillLevelTable) -> integralement cables message compris. Aucun
//            etat/horodatage ecrit. 25 lit un TAG texte 13 o (payload+12, nom
//            brut sans table a resoudre) + classe 0..3 (payload+8, str75..78) et
//            arme un timer propre (dword_1675BDC/flt_1675BE0) ; 26 = message seul
//            (str244) ; 27 = message avec le u32 arg2 affiche tel quel (str245).
//          - 28..30 : reprise des slots 7..9 standard (etat=4/5/0). Slot7 (28)
//            lit EN PLUS le meme tag 13 o que 25 -- message/timer PROPRES
//            (dword_1675BE4/flt_1675BE8, str246) DIVERGENTS du slot7 generique,
//            route via ApplyFamily3Slot28 (PAS ApplyComboFamilySlot). Gates par
//            Skill_IsAvailableByLevel+Char_GetPairedElement -> CABLES message
//            compris depuis le 2026-07-14 (SkillLevelTable/ElementPairTable
//            exposees globalement, cf. Game/SkillCombat.h ::
//            GetSkillLevelTable()/Combat_ReadLocalElementPairs() et
//            Docs/TS2_COMBAT_ELEMENT_GATING.md).
//
//  Skill_GetSpecialMotionId (sous-opcodes 402..410, etat dword_16860C0[idx]) --
//  famille "Special", 9 slots, MEME mecanique generale que les familles de combo,
//  CABLEE INTEGRALEMENT (lecture complete du desassemblage, EA 0x49ca89..0x49d1cf) :
//  gate = Skill_IsSpecialUsable, CABLE (meme SkillLevelTable exposee que
//  Skill_IsAvailableByLevel, cf. WorldEntityDispatch.cpp) ; slot4 (405) N'ARME PAS la barre de charge partagee
//  (dword_1675BA4/flt_1675BA8 jamais touchee ici, contrairement aux familles de combo) :
//  drapeau propre dword_1675CB0(int)/flt_1675CB4(FLOAT, asymetrie confirmee) + demi-
//  duree dword_1675CB8=arg2/2 + 2e ligne de chat "[demiduree]str843" + reset 4 champs
//  (dword_1675CBC/CC0[0]/CC4/CC8/CCC). Sous-opcode 401 (juste avant, EA 0x497c66) est
//  une notice HUD AUTONOME sans lien mecanique (pas de Skill_GetSpecialMotionId).
//
//  Skill_GetBuffMotionId (sous-opcodes 411..415, etat dword_16860D0[idx]) :
//  mecanique PLUS SIMPLE (5 sous-cas, PAS 9 -- 412/413 fusionnes par le compilateur en
//  un seul corps ; AUCUN gate Skill_IsAvailableByLevel/Skill_IsSpecialUsable, les
//  messages sont ecrits INCONDITIONNELLEMENT sauf le dernier gate par morph==415) --
//  CABLEE INTEGRALEMENT (lecture complete du desassemblage, EA 0x49d1d9..0x49d565).
//  tag entre crochets = MEME champ brut tag13 (payload+12) que le sous-opcode 25. Sous-
//  opcodes 416/417 (EA 0x49d565/0x49d58d) adjacents mais SANS RAPPORT (tableau distinct
//  dword_1686120, pas de Skill_GetBuffMotionId, pas de message) -- cables par trivialite.
//  VERIFIE contre Game/GameState.h::ActiveBuff / UI/BuffStatusPanel.h::BuffIconId :
//  dword_16860D0[idx] (etat de "cast en cours", motion 241-330) N'EST PAS la source de
//  PlayerEntity::buffs (catalogue 0..33 disjoint, alimente par ~50 autres systemes non
//  lies) -- AUCUN branchement fait vers ActiveBuff ici, ce serait une fabrication non
//  confirmee par le desassemblage.
//
//  Famille de combo 4 (arme 4, Skill_GetComboMotionId(4, idx), sous-opcodes 755..763,
//  etat dword_16862F0[idx]) -- CABLEE INTEGRALEMENT (EA 0x4a2159..0x4a293c). PAS de
//  suite immediate de la famille 3 (aucun sous-opcode entre 31 et 754 n'appelle
//  Skill_GetComboMotionId ; verifie par grep exhaustif du desassemblage complet). 2
//  divergences vs familles 1/2/3 : AUCUN tableau d'horodatage journalier (dword_16862F0
//  = etat seul) ; slot4 (758) plus riche (drapeau propre dword_1675E90/E98 + barre
//  partagee + 2e son flt_1491A3C + demi-duree dword_1675E9C + 2e ligne de chat + reset 5
//  champs dword_1675E94/EA0[0]/EA4/EA8/EAC) ; slot7 (761) NE PASSE PAS par
//  Char_GetPairedElement : bloc brut 80o (payload+8, copie inconditionnelle vers
//  dword_1676054, sans consommateur modelise ici -> non persiste) + entier 4 chiffres
//  decodable en jusqu'a 4 ids de classe, message gate par Skill_IsAvailableByLevel --
//  gate DISPONIBLE depuis le 2026-07-14 (GetSkillLevelTable(), cf. slots 1/2/3 des
//  memes familles desormais cables) MAIS message TOUJOURS OMIS ICI : l'entier 4
//  chiffres est lu a un offset payload non confirme dans cette passe (au-dela du bloc
//  brut de 80o, donc au-dela des offsets idx/arg2/tag13 surs), TODO precis (cf.
//  WorldEntityDispatch.cpp).
//
//  Aucune famille "5" de type Skill_GetComboMotionId(5, ...) n'existe (grep exhaustif :
//  seuls les weaponType 1/2/3/4 sont utilises dans tout le dispatcher).
//
//  Sous-opcodes 31/32 (mission "survol 31..170", 2026-07-14, EA 0x4963b0/0x496583) --
//  CABLES. PREMIER mecanisme du "reste" du dispatcher, verifie sur le desassemblage
//  complet : PAS une suite de la famille 3 (aucun Skill_GetComboMotionId ici). Toggle
//  "exclusion" par (branche 0..3 = payload+4, type 0..3 = payload+8) dans
//  dword_1686014[4*branche+type] = 0 (31) / 1 (32) -- ecriture INCONDITIONNELLE (avant
//  tout controle de plage). Sous-opcodes 31/32 STRUCTURELLEMENT JUMEAUX (code duplique
//  par le compilateur) : libelle branche = str75..78 (comme ApplyFamily3TagSlot),
//  libelle type = str251..254 (31) / str255..258 (32), format " %s" = off_7A6E5C (lu
//  directement en memoire). Notice flottante commune (floatType=3, flag=4). IMPORTANT :
//  dword_1686014 a TROIS CONSOMMATEURS reels cote binaire (xrefs_to verifie) --
//  Combo_CheckTransition (0x4fd650, EA 0x4ff2d6), Player_UpdateMovement (0x534500, EA
//  0x534856), UI_FactionInfoWnd_Render (0x672010, EA 0x67246d) -- AUCUN porte dans
//  ClientSource a ce jour, donc l'ecriture est fidele/persistee mais SANS EFFET
//  observable pour l'instant (TODO(@dword_1686014_consumers)).
//
//  Sous-opcodes 101..115 (mission "duel/defi 101-115", 2026-07-14, RE/
//  dispatch_494870_full.c L.2579-2789) -- CABLES (101..109/112/114 duel/defi ; 111/115
//  "maitrise de branche" ; 110 et 113 EXCLUS, cf. plus bas), DEUX sous-systemes distincts :
//    - 101..109/112/114 : DUEL/DEFI par comparaison de PSEUDO joueur -- le payload
//      transporte des noms bruts 13 o a des offsets PROPRES a cette sous-plage (payload+4,
//      +17, +30, +34, +43 -- verifie esp-a-esp sur le desassemblage, DIFFERENTS de la
//      convention idx/arg2/tag13 standard du fichier). Etat partage dword_16746B8/
//      dword_168726C (0=aucun/2=en cours/code brut selon le cas), dword_1687450 (code
//      opposant/resultat), dword_168744C (102 seul), dword_16747F0 (114), rating d'attaque
//      dword_168736C[0]/dword_1687374[0] (memes adresses que A_RatingBaseMin/Max de
//      Net/GameVarDispatch.cpp). Le gate `byte_1673184==nom_paquet` (102/107/108/114)
//      utilise g_World.self.localPlayerName (jamais peuple par aucun handler a ce jour,
//      meme degradation honnete que documentee dans Game/GameState.h -- le gate echoue
//      proprement tant que le paquet de login ne peuple pas ce champ).
//    - 111/115 : notices "maitrise de branche" (StrTable005 1671..1675/2322), branche 0..3
//      -> label 1672..1675, AUCUN etat ecrit (contrairement a 96..100/110).
//    - 110 EXCLU : appartient STRUCTURELLEMENT au cycle "branche de competence" 76..100
//      CABLE ci-dessus (dword_1685F94[8*a+b], PAS a duel/defi ni maitrise de branche) --
//      RESTE NON CABLE (hors perimetre precis de la mission 76..100 ET de la mission
//      101-115, cf. Net/WorldEntityDispatch.cpp::ApplyDuelBranchFamily pour le `default`).
//    - 113 EXCLU : ENTIEREMENT gate par `!Crt_Strcmp(dword_16746A8, v686)` (nom de
//      guilde/affiliation LOCAL, cf. Game/NameplateLogic.h -- "semantique incertaine, a
//      confirmer par un futur RE"), AUCUNE ecriture d'etat inconditionnelle a preserver --
//      MEME statut que 425..428 : rien de calculable sans fabriquer un champ non confirme.
//
//  Sous-opcodes 201..208 (mission "201-208", 2026-07-14, RE/dispatch_494870_full.c
//  L.2790-2899) -- CABLES INTEGRALEMENT (lecture directe du desassemblage complet,
//  PAS juste un survol). Famille "arene individuelle" : morph FIXE 194 (PAS
//  Skill_GetComboMotionId/SpecialMotionId/BuffMotionId -- comboMotionId=194 est une
//  constante codee en dur dans chaque sous-cas), etat SCALAIRE dword_1686054 (PAS un
//  tableau indexe par idx -- une seule instance d'arene individuelle possible a la
//  fois, contrairement aux familles 1..4/Special/Buff). Forme PROCHE (mais PAS
//  identique) des familles de combo a 9 slots -- DIVERGENCES confirmees par lecture
//  directe du desassemblage (PAS le gabarit "meme forme qu'ApplySpecialFamilySlot"
//  suppose par l'audit sommaire initial) :
//    - AUCUN gate Skill_IsAvailableByLevel/Skill_IsSpecialUsable nulle part dans
//      cette famille (201/202/203 ecrivent leur message INCONDITIONNELLEMENT, sans
//      jamais consulter SkillLevelTable -- contrairement A TOUTES les familles de
//      combo/Special/4).
//    - 201 (slot1) : idx=payload+4, message "<nom194> [<idx>]<str231>", AUCUNE
//      ecriture d'etat (seul sous-cas de la famille sans ecriture d'etat -- les
//      slots 1 des familles combo/Special/4 n'ecrivent pas non plus d'etat, donc
//      coherent).
//    - 202/203 (slots 2/3) : etat=1/2, message inconditionnel (str232/233).
//    - 204 (slot4) : etat=3, gate=morph==194 -> message str234 + drapeau PROPRE
//      (dword_1675C84/flt_1675C88, DISTINCT de la barre partagee dword_1675BA4/
//      flt_1675BA8 des familles de combo/4 -- JAMAIS touchee ici, meme divergence
//      que la famille Special) + demi-duree FIXE=600 (dword_1675C8C=600 code en
//      dur, PAS arg2/2 comme les familles combo 1/Special/4 -- idx n'est meme pas
//      lu par ce sous-cas) + 2e message "[600]str843" + reset 4 champs
//      (dword_1675C90[0]/94/98/9C, PAS de "ResetA=1" prealable contrairement a la
//      famille Special).
//    - 205 (slot5) : etat=5 seul, aucun message (identique aux familles combo/Special).
//    - 206 (slot7) : idx=payload+4 (element a apparier), etat=4 inconditionnel. SI
//      idx==-1 -> message COURT "<nom194> <str845>" (SANS etiquette de classe,
//      branche ABSENTE du slot7 generique des familles combo -- divergence propre a
//      cette famille). SINON -> Char_GetPairedElement (Combat_ReadLocalElementPairs),
//      message avec 1 ou 2 etiquettes de classe (str236, meme forme que le slot7
//      generique). Message TOUJOURS envoye (pas de gate SkillLevelTable). Warp SI
//      morph==194 ET element local != idx ET element local != paire ET
//      g_SelfCharInvBlock[0] -- via Map_BeginWarpToFactionTownDefault (PAS
//      BeginWarpToFactionTown : cette famille n'utilise JAMAIS la garde "mort" des
//      autres warps du fichier). TODO(@idx=-1) : dans la branche idx==-1, la
//      variable locale "PairedElement" d'origine n'est pas recalculee -- elle est
//      PARTAGEE par le compilateur avec ~5 autres sous-cas du meme switch geant
//      (BYREF, cf. RE/dispatch_494870_full.c:735), sa valeur exacte dans cette
//      branche precise n'est pas determinable statiquement depuis le seul
//      pseudocode -- approxime par paired=-1 (repli documente, ne bloque jamais le
//      warp), non verifie bit-a-bit.
//    - 207 (slot8) : etat=5, gate=morph==194 -> message str237 ; warp SEULEMENT SI
//      g_SelfCharInvBlock[0] (contrairement aux slots8 generiques des familles
//      combo/Special/4/Buff qui warpent des que le gate morph passe, sans condition
//      supplementaire -- divergence propre a cette famille).
//    - 208 (slot9) : etat=0 (reset), aucun message (identique aux familles combo/Special).
//  Son (Snd3D_PlayScaledVolume) HORS PERIMETRE audio sur 201..206, meme convention
//  que le reste du fichier. EA precises non capturees (map_pseudocode_line_to_eas
//  instable sur cette fonction, cf. mission "reste de la fonction" plus bas) --
//  localisation fiable = numero de ligne RE/dispatch_494870_full.c cite ci-dessus.
//
//  Sous-opcodes 418..429 (mission "fin du bloc Buff", 2026-07-14, dump L.3215-3364) --
//  PAS la mecanique table-driven Buff/combo/Special (aucun Skill_Get*MotionId ici,
//  verifie par lecture complete du desassemblage) : 6 notifications independantes +
//  1 tail-merge partage (423/429, LABEL_135 du desasm) + 4 sous-cas non cables :
//    418  WE_PlaySound_SysLine_418 (EA 0x49d5b5, confirme par disasm direct) : compte
//         (payload+4) -> "[compte]str1402" si >0 sinon "str1403" seul. Pas d'etat/gate.
//    419  classe 0..3 (payload+4) + TAG 13o a payload+8 (PAS +12 comme le tag13
//         "standard" du fichier -- meme offset qu'ApplyClassTagFamily 671..677) ->
//         "[classe] [tag]str1444". Pas d'etat/gate.
//    420  compte (payload+4) -> "[compte]str1475", HUD flottant (cat.3/type1) + chat.
//    421  arme dword_1686134=1 -- CONFIRME etre WorldMap::flagZ291Variant (World/
//         WorldMap.h, aucune instance WorldMap globale exposee aux handlers reseau ici,
//         meme limite que SkillLevelTable) ; message str1476 (HUD cat.3/type2 + chat)
//         inconditionnel ; si morph==291 (CALCULABLE) : arme dword_1675CD0/flt_1675CD4
//         -- CONFIRME etre la ligne 28 de Game/AnimationTick.cpp::kMorphRows (deja
//         consommee par le moteur de timers generique) + 2e message str1477.
//    422/424  suffixe str1478 vs str1480 (PAS "corps identique" au sens strict, cf.
//         RE-VERIFIE ci-dessous) : dword_1686134=0 ecrit INCONDITIONNELLEMENT (precede
//         le gate, meme convention que tout le fichier). Message "[nom] suffixe" + warp
//         conditionnel omis : dependent de byte_1686138 -- meme limite que leaderName
//         (629..652 ci-dessous).
//    425..428  ENTIEREMENT gates par `!Crt_Strcmp(byte_1686138, dword_16746A8)`, SANS
//         aucune ecriture d'etat avant ce gate (contrairement a 422/424) -> RIEN de
//         calculable ici -- NON CABLES (meme statut que 600/764-770).
//         RE-VERIFIE (mission "425-428 + 500/901-903", 2026-07-14) via xrefs_to idaTs2
//         sur TOUT LE BINAIRE (pas seulement cette fonction) pour byte_1686138 (20 xrefs)
//         ET byte_1686145 (6 xrefs, buffer 13o jumeau immediatement adjacent, utilise par
//         les memes gates ailleurs -- UI_ClanWarp_Commit/UI_ClanDisband_Commit) : 25 des
//         26 sites sont des LECTURES (confirme instruction par instruction) ; le seul site
//         restant est le case 422 lui-meme (EA 0x496E37, `Crt_StringInit(&byte_1686138,
//         &byte_1686145)`) qui ECRIT byte_1686138 -- mais en copiant depuis byte_1686145,
//         qui n'est LUI-MEME jamais alimente avec un contenu reel nulle part dans l'image
//         (confirme exhaustivement, cf. commentaire detaille au site d'implementation dans
//         Net/WorldEntityDispatch.cpp). Donc byte_1686138 se resume a une copie differee
//         d'un buffer perpetuellement vide : le gate reste INSURMONTABLE en statique --
//         pas par absence de site d'ecriture, mais parce que l'unique site d'ecriture ne
//         propage jamais de contenu reel.
//    423/429  tail-merge LABEL_135 (corps STRICTEMENT identique) : compte (payload+4),
//         si morph==291 -> "[compte]str1479" (chat seul).
//
//  Sous-opcodes 601..903 (mission "reste de la fonction", 2026-07-14) -- CABLES
//  (avec TODO precis par endroit) :
//    601..610  famille "bonus de niveau/canal" (dword_1686058[0..2]).
//    611..615  famille "notification d'element" (gate g_World.self.element).
//    620..628  notices autonomes (dword_1686188, etc).
//    629..652  famille "declaration de guerre/palier" (dword_168618C[elt 0..3],
//              villes de siege kSiegeTownNpc={138,139,165,166}) -- TODO : nom de
//              guilde/chef (buffers non recus dans ce paquet, 631/632/637-638/
//              642-643/647-648), warp conditionne par ce nom (637-638/642-643/
//              647-648), recalcul de jauge d'attaque (635/640/645/650).
//    659..669  famille "arene" (morph fixe 200, dword_16862A0).
//    671..677  famille "annonce classe+tag" (notifications pures).
//    700..729  2e famille "siege par element" (dword_16862A4[elt 0..3], villes
//              kSiegeTownNpc2={5,10,15,123}).
//    740..750  3e famille "siege/classement" (morph fixe 54, dword_16862B4) --
//              750 absent du desasm (sparse).
//    752/753   table de titres/rang (dword_184C218/184C248[0..11]).
//    754       annonce verbe+classe+tag (SkillName + Str(284)).
//    771..774  famille "annonce de cast" classe+tag (avec SkillName).
//    780..786  evenements "guerre 324" (dword_1686304/1686308).
//    788..791, 795  notices/annonces simples (SkillName + suffixe[s]).
//    800..807  evenements "guerre 342" (dword_1686310) -- 803 confirme NO-OP
//              (dead store cote binaire d'origine, verifie).
//    901..903  notices finales (banniere + ligne systeme, forme = sous-opcode 401).
//  TODO (non cables, cf. Net/WorldEntityDispatch.cpp en tete de section) : 600
//  (confirme no-op observable), 764..770 (sparse, absent).
//
// ===========================================================================
//  PLAGES NON COUVERTES (TODO -- no-op fidele, cf. audit)
// ===========================================================================
//  33..115 (mission "survol 31..170", 2026-07-14 -- DENSE, lecture complete du
//  desassemblage EA 0x4963ec..0x4979ec, ~83 sous-cas TOUS PRESENTS et TOUS
//  DISTINCTS, contrairement aux familles 1..4/Special/Buff : PAS un pattern
//  mecanique unique repetable, plusieurs sous-systemes bien separes identifies
//  (audit sommaire, PAS une lecture exhaustive case par case au depart -- depuis
//  fait pour 76..100 et 101..115, cf. ci-dessous ; il reste 33..75) :
//    33..45     CABLE (mission "loadout d'element", 2026-07-14, RE/dispatch_494870_full.c
//               L.1328-1675, cf. ApplyElementLoadoutFamily dans le .cpp) -- notices/etat du
//               "loadout" (chargement) d'element du joueur local (dword_1685E10/18/1C/20/
//               24/28, dword_16860B0..BC) -- StrTable005 ids 271..288 ; TOUS ecrivent HUD
//               flottant + ligne systeme (contrairement a 46..50 qui n'ecrivent QUE le
//               HUD) ; warp conditionnel sur g_SelfMorphNpcId (35=38, 38=38/39/74/144-145/
//               310, 40=table {50,52,85,99,100,170,196}). Aucun rapport avec les familles
//               de combo/Special/Buff ni avec Char_GetPairedElement.
//    46..47     CABLE (mission "cablage ElementPairTable", 2026-07-14, cf.
//               ApplyAlliancePairFamily dans le .cpp) -- appariement d'ALLIANCE
//               par element (g_AlliancePairTable, g_PerElementCounter,
//               dword_1685E48/68) ; StrTable005 ids 377/378/379. CORRECTIF d'une
//               note anterieure de cette meme ligne : verifie par resolution
//               d'adresse idaTs2 (list_globals) que g_AlliancePairTable EST
//               EXACTEMENT g_LocalPlayerSheet+0x71C (0x1685E64) et
//               dword_1685E68 EST EXACTEMENT g_LocalPlayerSheet+0x720
//               (0x1685E68) -- CE SONT DONC LES MEMES CHAMPS `a`/`b` que
//               Char_GetPairedElement (slot7 des familles combo), PAS un
//               systeme different comme l'affirmait cette note. Ce sous-opcode
//               EST le seul point d'ecriture (cote client) de
//               Game/SkillCombat.h::ElementPairTable -- cf.
//               Docs/TS2_COMBAT_ELEMENT_GATING.md, addendum "Cablage".
//    48..50     CABLE (mission "loadout d'element", 2026-07-14, RE/dispatch_494870_full.c
//               L.1819-1969, cf. ApplyAllianceLabelFamily dans le .cpp) -- tail-merge de
//               l'appariement d'alliance 46/47 : 48/49 partagent la FORME de libelle
//               "<selector str380..383> [classe] [tag] <suffixe>" avec 41/42 mais restent
//               distincts (HUD floatType=10, PAS de ligne systeme -- verifie : aucun
//               Msg_AppendSystemLine pour 46..50, DIVERGENCE vs 33..45). 49 REPREND
//               integralement l'etat ecrit par 47 (memes adresses g_AlliancePairTable/
//               g_PerElementCounter) avant d'ajouter son propre message (suffixe str285).
//               50 est une notification pure ("<selector> str286"). CORRECTIF (meme
//               mission) : le cablage anterieur de 46/47 (ApplyAlliancePairFamily) ajoutait
//               a tort un Msg_AppendSystemLine absent du binaire pour ces deux sous-cas --
//               retire (cf. commentaire de tete d'ApplyAlliancePairFamily dans le .cpp).
//    51..62     CABLE (mission "sous-plage 51-75", 2026-07-14, RE/dispatch_494870_full.c
//               L.1970-2229) -- systeme de "classement/scoreboard" par classe (0..3) et
//               slot (0..9) : dword_1685E74/78/7C/80 (paliers d'alliance, 52..56, avec
//               dead code confirme sur les HUD_ShowFloatingMessage annexes 52..55 -- MAIS
//               52 SEUL a aussi un effet REEL non-dead : boucle i=0..4/j=0..10 qui remet a 0
//               les TROIS tableaux du scoreboard en entier (dword_168653C/16865DC/168667C,
//               les memes que 57..59), verifiee et cablee lors d'une RE-VERIFICATION
//               2026-07-14, cf. ApplyScoreboardFullReset dans le .cpp -- 53/54/56 n'ont QUE
//               le palier, 55 a une boucle differente et bien dead, elle), dword_168653C/
//               16865DC/168667C + byte_1686334 (score/rang/compteur/nom de slot, 57..59 --
//               58 a message OMIS, nom jamais ecrit par cette fonction) + 4 notices
//               independantes (51/60/61/62). DISTINCT de l'appariement d'alliance 46/47
//               ET de la famille de branche de competence 63+ (dword_1685F94).
//    63..75     CABLE (mission "sous-plage 51-75", 2026-07-14, RE/dispatch_494870_full.c
//               L.2219-2305) -- DEBUT du meme systeme "branche de competence" que 76..100
//               ci-dessous (dword_1685F94/Skill_GetMotionId2) : 63..65 = preambule
//               "poll/arm1/arm2" (SEUL endroit ou Skill_IsAvailableByBranch influence
//               reellement un branchement dans toute la famille) ; 66..75 = 1re/2e
//               iteration du cycle "flagsA/flagsB/soundOnly/probe/L418/L420/L422"
//               documente juste en dessous -- reutilisent TELLES QUELLES les fonctions
//               ApplyBranchFlagsSlot/ApplyBranchSoundOnlySlot/ApplyBranchProbe/
//               ApplyBranchLabel418/420/422 (cf. bloc "Sous-opcodes 63..68" dans le .cpp).
//    76..100    systeme de "branche" de competence (Skill_GetMotionId2(branch,
//               type), dword_1685F94[8*branch+type]) -- DISTINCT de
//               Skill_GetComboMotionId (familles 1..4). CABLE INTEGRALEMENT
//               (mission "sous-plage 76..100", 2026-07-14, EA 0x49a514..
//               0x49b4cb, cf. RE/dispatch_tables.json switch jump_ea=0x494a17
//               et disasm direct sur les cases 76/78/80) : cycle de 7 sous-cas
//               repete (probe/L418/L420/L422/flagsA/flagsB/soundOnly, cf.
//               ApplyBranchProbe et suivantes dans WorldEntityDispatch.cpp) --
//               Skill_IsAvailableByBranch (Game/SkillSystem.h, deja exposee)
//               n'est PAS appelee : son unique appelant dans cette tranche
//               (slot "probe") jette systematiquement le resultat (verifie par
//               disasm, les deux branches convergent vers le meme `return`).
//               110 (hors de cette tranche) reste NON cable, cf. bloc "Sous-opcodes
//               101..115" plus haut pour le detail (appartient a CE cycle 76..100,
//               PAS a duel/defi -- exclu des deux missions par prudence).
//    101..115   CABLES (mission "duel/defi 101-115", 2026-07-14 -- sauf 110/113, cf.
//               bloc "Sous-opcodes 101..115" plus haut pour le detail complet).
//  Verdict : ZONE DENSE, PAS cable-able en bloc (5+ sous-systemes distincts avec
//  semantique/etats propres) -- 33..115 CABLE INTEGRALEMENT (sauf 110/113, exclusions
//  documentees ci-dessus, a coupler avec une future revalidation de 76..100 / du
//  buffer dword_16746A8).
//
//  116..200 (mission "survol 31..170", 2026-07-14) -- CONFIRME VIDE : AUCUN `case`
//  dans le desassemblage complet (grep exhaustif sur RE/dispatch_494870_full.c,
//  0 occurrence pour 116..200 inclus, donc 170 et 196 aussi -- l'annotation IDA du
//  jump table par defaut liste explicitement des sous-plages qui corroborent :
//  "116-119,121-200" -- l'ecart apparent en 120 est un artefact du jump table,
//  PAS un sous-cas reel, verifie par grep direct). PAS un TODO : comportement
//  terminal confirme, no-op fidele au `default:`.
//
//  201..208 -- CABLE INTEGRALEMENT (mission "201-208", 2026-07-14), cf. bloc
//  "Sous-opcodes 201..208" plus haut (famille "arene individuelle", morph fixe 194).
//
//  425..428 (mission "fin du bloc Buff", 2026-07-14 -- AUDITEE :
//  entierement gates par un buffer nom byte_1686138 non ecrit dans cette fonction, cf.
//  bloc "Sous-opcodes 418..429" plus haut -- 418..424/429 CABLES dans la meme mission),
// 600, 764..770 (cf. liste TODO precise ci-dessus -- 601..903 CABLES dans la
// mission "reste de la fonction", 2026-07-14).
//  Carte complete par plage et dump Hex-Rays integral de la fonction : RE/
//  dispatch_494870_full.c (genere via ida_hexrays.decompile ; numeros de ligne cites
//  en commentaire dans WorldEntityDispatch.cpp renvoient a ce fichier).
//
// SOURCE DE VERITE : desassemblage idaTs2 (EA citees en commentaire). Comme
// pour Net/ItemActionDispatch.h, ce module N'A PAS de struct RecvPackets.h
// pleinement exploitee : Net/RecvPackets.h::WorldEntityDispatchHeader existe
// pour la documentation/repere de couverture, mais ApplyWorldEntityDispatch
// reparse le payload brut lui-meme (memes raisons qu'ItemActionDispatch).
#pragma once
#include <cstdint>

namespace ts2::game {

// Applique un paquet 0x5e. `payload` pointe sur le payload recu (104 o
// attendus : subOpcode u32 + 100 o bruts, mirroir de v736/v702 du desasm) ;
// `len` sa taille. Sous-opcodes 1..30 (familles de combo 1, 2 et 3), 31/32 (toggle
// "exclusion" branche/type), 33..45 (loadout d'element), 46/47 (appariement
// d'alliance par element), 48..50 (tail-merge de l'appariement d'alliance, cf.
// bloc "33..115" plus haut pour ces 3 sous-plages), 51..75 (scoreboard de
// classement + debut de la famille "branche de competence", cf. bloc "33..115"
// plus haut), 76..100 (famille "branche de competence" Skill_GetMotionId2, cf.
// bloc "33..115" plus haut), 101..115 (duel/defi par comparaison de pseudo +
// "maitrise de branche", SAUF 110/113 -- cf. bloc "Sous-opcodes 101..115" plus
// haut), 201..208 (famille "arene individuelle", morph fixe 194, cf. bloc
// "Sous-opcodes 201..208" plus haut), 401 (notice HUD autonome), 402..410
// (famille "Special"), 411..417 (famille "Buff" + 2 flags triviaux adjacents),
// 418..424+429 (notifications independantes et tail-merge, cf. bloc
// "Sous-opcodes 418..429" plus haut ; 425..428 PAS cables, entierement gates par
// un buffer absent), 601..807/901..903 (familles de guerre/siege/classement, cf.
// liste detaillee plus haut) et 755..763 (famille de combo 4) integralement
// cables (avec TODO precis par endroit, cf. WorldEntityDispatch.cpp) -- TOUTE la
// plage 33..115 est donc CABLEE (sauf 110/113, exclusions documentees). Tout le
// reste est un no-op fidele (comportement du `default:` du switch d'origine) :
// 425..428 (buffer indisponible), 600/764..770 (TODO precis, cf. "PLAGES NON
// COUVERTES" plus haut), 116..200 est
// CONFIRME VIDE (pas un TODO).
void ApplyWorldEntityDispatch(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

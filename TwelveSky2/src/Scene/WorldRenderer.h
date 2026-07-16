// Scene/WorldRenderer.h — dessin RÉEL des entités du monde (câblage jalon 4->5).
//
// Ce fichier ne reverse AUCUNE nouvelle fonction : il CÂBLE ensemble trois modules
// déjà écrits, jusqu'ici non reliés entre eux :
//   Gfx/MeshRenderer.h    — sait dessiner un gfx::SkinnedModel (VB/IB skinné GPU).
//   Gfx/Camera.h          — matrices vue/projection de la caméra en orbite.
//   Game/EntityDrawLogic.h — décision (dessiner/pas) + placement, PAS de rendu D3D.
//   Game/NameplateLogic.h  — texte + couleur du nom au-dessus de la tête.
//   Game/GameState.h::g_World — source des entités (players/monsters/npcs, tous
//                                avec x/y/z depuis ce jalon — cf. NpcEntity).
//
// PÉRIMÈTRE DE CETTE MISSION (câblage, pas nouvelle RE) :
//   - Corps de l'entité : modèle réel via ResolveMonsterModel()/ResolveWeaponModel()
//     (câblés sur Gfx/ModelCache, cf. bandeau ci-dessous) quand résolvable ; SINON un
//     placeholder cube coloré (D3DXCreateBox + pipeline fixe) — jamais d'écran vide.
//
// CÂBLAGE ModelCache (mission "câbler ResolveModel() sur Gfx/ModelCache", 2026-07-14,
// cf. Gfx/ModelCache.h pour les signatures exactes) — TODO(model_cache) ci-dessous
// RÉSOLU pour deux des trois cas :
//   - MONSTRES : résolu à 100%. MonsterEntity::body[0] = mob id -> MONSTER_INFO, MÊME
//     convention que Game/EntityManager.cpp::ResolveMobDef (id brut, sans -1, passé tel
//     quel à g_World.db.monster.record()) -> ModelCache::GetForMonster(monsterDefId)
//     résout kindIndex=MONSTER_INFO.field244-1 en interne et renvoie le vrai
//     gfx::SkinnedModel (M*.SOBJECT). Remplace ENTIÈREMENT le cube quand ça résout.
//   - JOUEURS : RÉSOLU (mission "câblage corps de base joueur", 2026-07-14, cf.
//     Gfx/ModelCache.h::GetForPlayerBody + Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5) :
//     PlayerEntity::body+68/+72/+76/+80 (race/genre/costumeSlot0/costumeSlot1) ->
//     ModelCache::GetForPlayerBody(race, gender, costumeSlot0, costumeSlot1) renvoie
//     les 2 pièces (SLOT0+SLOT1) réellement dessinées par le pipeline joueur d'origine
//     (Char_DrawWeaponTrailEffect, PAS Char_Draw). Ces 4 offsets sont valables pour
//     TOUT joueur du tableau (self ET distants) SANS distinction : contrairement à
//     l'arme (qui a un global self dédié dword_1673248, plus réactif), race/genre/
//     costume n'ont AUCUN global self séparé connu -- au contraire, le doc §4 montre
//     que Pkt_ItemActionDispatch réécrit gender/costumeSlot0/costumeSlot1 DIRECTEMENT
//     dans entity[0]+96/100/104, donc dans CE MÊME `body` (entity et body partagent la
//     même mémoire, body = entity+0x18) -- p.body est donc déjà la source la plus à
//     jour pour le joueur local aussi. Remplace ENTIÈREMENT le cube-corps quand au
//     moins une des 2 pièces résout ; le cube reste un repère de dernier recours si
//     les DEUX stems échouent (fichier introuvable/hors bornes). Ce qui reste résolvable
//     en plus du corps : l'arme équipée, câblée pour LE JOUEUR LOCAL *ET* LES JOUEURS
//     DISTANTS (mission "câblage arme joueur distant", 2026-07-14) :
//       * Joueur local (index 0) : SelfState::equip[7].itemId (slot 7 = arme,
//         dword_1673248, cf. Docs/TS2_GAMEPLAY_LOGIC.md §"13 slots ... Slot 7 = arme"
//         et Game/SkillSystem.cpp:145 "self.equip[7] = dword_1673248") -> GetItemInfo(itemId)
//         -> ModelCache::GetForItem(item, 0). Source déjà à jour en continu (mise à jour
//         équipement locale), préférée à la relecture du body réseau pour ce cas.
//       * Joueurs DISTANTS (i!=0) : offset RÉSOLU dans PlayerEntity::body (600 o, payload
//         Pkt_SpawnCharacter 0x0f 0x4646c0) -> body+148 (u32 LE) = id d'objet de l'arme
//         équipée, valable pour TOUTE entité du tableau (self inclus, à l'index 0, mais on
//         garde SelfState pour ce cas-là — cf. ci-dessus). PREUVE DE DÉCOMPILATION (paire
//         de fonctions jumelles trouvées dans l'IDB, MCP idaTs2) :
//           - `Weapon_ClassFromEquip` 0x4cc9f0 (SELF UNIQUEMENT) :
//             `*(this+7) = MobDb_GetEntry(mITEM, dword_1673248)` — dword_1673248 EST
//             l'id d'arme self (même global que Game/SkillSystem.cpp:145).
//           - `Weapon_ClassFromField56` 0x4cc930 (GÉNÉRIQUE, self OU distant) :
//             `*(this+7) = MobDb_GetEntry(mITEM, *(a2+56))`, appelée
//             `Weapon_ClassFromField56(g_EquipSnapshotScratch, entity+116)` (vu en clair
//             dans `Char_AnimEndToIdle_5761A0` 0x57629b, et documenté pour CHAQUE entité
//             active via `Char_UpdateAnimationFrame` 0x571880 dans
//             RE/gameplay_findings.json : "WeaponClass = Weapon_ClassFromField56
//             (dword_8E719C, entity+116)") -> a2=entity+116, donc `*(a2+56)` =
//             `*(entity+172)`. Corps identique (même MobDb_GetEntry + même switch sur
//             typeCode@+188) que Weapon_ClassFromEquip => `entity+172` porte
//             SÉMANTIQUEMENT le même champ "id d'arme" que dword_1673248, mais lu
//             depuis L'ENTITÉ elle-même (donc valable pour un index distant, jamais
//             câblé sur un global self). `entity+172` = `body+148` car le body démarre
//             à `entity+0x18` (24 o) : `Pkt_SpawnCharacter` 0x4646c0 fait
//             `Crt_Memcpy(&dword_168724C[227*i], v8, 600)` avec `dword_168724C` =
//             `dword_1687234`(=base)`+0x18`. Donc `entity+172 - 24 = 148`.
//         PlayerEntity::body contient par ailleurs l'apparence/équipement brut sur
//         d'autres offsets (nom @+16, weaponTypeId @+64, race/genre/costumeSlot0/
//         costumeSlot1 @+68/72/76/80 -- RÉSOLUS, cf. paragraphe JOUEURS ci-dessus --,
//         modelC/D @+84/88 encore NON résolus, base "equipSnapshot" @+92 — cf.
//         RE/gameplay_findings.json struct CharEntity) mais SEULS l'arme (@+148) et le
//         corps de base (@+68/72/76/80) ont un résolveur connu côté ClientSource à ce
//         jour (cf. ModelCache.h) ; modelC/D et le reste de l'equipSnapshot restent hors
//         périmètre (pas d'invention). GetItemInfo(itemId) -> ModelCache::GetForItem
//         (item, 0), EXACTEMENT le même chemin que pour le joueur local. Le modèle
//         d'arme réel, quand résolu, est dessiné en SURIMPRESSION du corps (modèle réel
//         si résolu, sinon cube) : il n'y a pas de point d'attache de main reversé, donc
//         pas de vraie transformée relative au squelette -- un simple décalage vertical
//         fixe (cf. WorldRenderer.cpp::renderOne) sert de repère visuel plutôt que de
//         laisser un objet flottant sans lien avec l'entité.
//   - PNJ : DEUX TABLEAUX DISTINCTS, DEUX ÉTATS DE RÉSOLUTION DIFFÉRENTS (mise à jour
//     mission "PNJ DECOR VISIBLES A L'ÉCRAN", 2026-07-14) :
//       * PNJ DE DÉCOR (`game::ZoneNpcs()`, Game/StaticNpcLoader.h — ÉQUIVALENT client-source
//         de `g_NpcRenderArray` 0x1764D14, repeuplé localement depuis la table statique
//         `mZONENPCINFO`, cf. bandeau de tête de StaticNpcLoader.h pour la preuve complète) :
//         RÉSOLU. Chaque `StaticNpcSlot::def` porte un `NpcDefRecord*` non-nul (garde à la
//         source, cf. StaticNpcLoader.cpp) → `ResolveNpcModel()` → `ModelCache::GetForNpc(*def)`
//         (RÉSOLU dans une mission antérieure : `npc.fieldE` +1324 = kindIndex+1 du modèle
//         visuel N*.SOBJECT, cf. Docs/TS2_NPC_MESH_DRAW.md §2-3 et Gfx/ModelCache.cpp) — un vrai
//         modèle remplace le cube dès que `fieldE` résout un fichier sur disque. C'EST LA
//         SOURCE RÉELLE des PNJ de décor (marchands, gardes...) visibles en jeu dans le binaire
//         d'origine : `Npc_DrawMesh 0x57FF00` ne lit QUE `g_NpcRenderArray`.
//       * PNJ GAMEPLAY (`game::g_World.npcs`, alimenté par `Pkt_SpawnNpc` opcode 0x13) :
//         TOUJOURS NON résolu, intentionnellement. `game::NpcEntity` (Game/GameState.h) ne
//         porte aucun `NpcDefRecord*`/kindId exploitable (`def` reste `const void*` non typé,
//         aucune fonction du call-graph de rendu n'appelle `Char_Draw`/`Npc_DrawMesh` sur ce
//         tableau, cf. Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §2) — brancher `ResolveNpcModel`
//         ici exigerait d'inventer un mapping id-réseau → `NpcDefRecord`, hors périmètre (pas
//         d'invention). Cube JAUNE conservé tel quel pour cette boucle, sans changement.
//     Les deux boucles de `Render()` partagent exactement le même pipeline `renderOne()` (donc
//     le même repli cube JAUNE si la résolution échoue) — seule la source de données diffère.
//   - AUDIT DE FIDÉLITÉ 2026-07-14 (re-décompilation intégrale de Char_Draw 0x5805C0
//     et Npc_DrawMesh 0x57FF00, cf. Docs/TS2_NPC_MESH_DRAW.md) : ÉCART trouvé et
//     CORRIGÉ dans WorldRenderer.cpp::Render() (boucle PNJ). Npc_DrawMesh contient un
//     far-cull dur propre aux PNJ -- `Math_Dist3D(pos_pnj, flt_1687330 /* position du
//     JOUEUR LOCAL */) > 1000.0 -> return immédiat`, AVANT même le near-cull caméra --
//     qui n'existe PAS dans Char_Draw (vérifié : aucun appel Math_Dist3D dans son
//     désassemblage/décompilé). game::ComputeEntityDrawFlags ne modélise QUE le
//     pipeline Char_Draw (near-cull caméra seul, jamais de far-cull) et est réutilisée
//     telle quelle pour players/monsters/npcs : sans garde supplémentaire, un PNJ à
//     >1000 unités du joueur local aurait été dessiné (cube placeholder) alors que le
//     client d'origine ne le fait jamais. Corrigé en ajoutant ce garde AVANT renderOne
//     dans la boucle NPC (constante `kNpcFarCullDistanceSq`, ts2 anonymous namespace) --
//     ne touche pas EntityDrawLogic.cpp (hors périmètre d'édition de cette mission,
//     le garde reste donc localisé au site d'appel plutôt que dans la fonction pure
//     partagée). Le near-cull caméra (IsBeyondCameraNearCull) et l'absence de far-cull
//     pour joueurs/monstres sont CONFIRMÉS FIDÈLES tels quels (aucun changement).
//   - AUDIT CULLING DE DISTANCE / LOD 2026-07-14 (session 2, mission "CULLING DE
//     DISTANCE ET LOD") : re-vérification que le near-cull caméra (ci-dessus) et le
//     far-cull PNJ (ci-dessus) s'appliquent bien à TOUTES les entités, pas seulement à
//     celles proches caméra.
//       * Near-cull caméra (`IsBeyondCameraNearCull`) : CONFIRMÉ appliqué de façon
//         UNIFORME aux 3 boucles (joueurs -- self inclus --, monstres, PNJ) via l'appel
//         commun `game::ComputeEntityDrawFlags()` dans `renderOne()` : aucune des 3
//         boucles de `Render()` ne le contourne. Fidèle à `Char_Draw` (players/monsters)
//         ET `Npc_DrawMesh` (PNJ), qui appliquent tous deux ce garde.
//       * Far-cull 1000 u (PNJ seuls) : CONFIRMÉ toujours correctement SCOPÉ à la seule
//         boucle PNJ (`kNpcFarCullDistanceSq`) et absent des boucles joueurs/monstres --
//         c'est la fidélité correcte (`Char_Draw` n'a aucun `Math_Dist3D`, cf. ci-dessus),
//         PAS une lacune : étendre ce far-cull aux joueurs/monstres serait une régression.
//       * ÉCART DE FIDÉLITÉ MINEUR TROUVÉ (non corrigé, faible impact) : `EntityRenderState
//         ::info` n'est JAMAIS peuplé par `WorldRenderer::Render()` (`DrawableEntity` n'a
//         pas de champ `info`) -> `IsBeyondCameraNearCull` reçoit toujours `radius=0.0`
//         au lieu du vrai `info.drawSize` par entité. Le garde reste appliqué à TOUTES les
//         entités de façon identique (donc pas de biais self/distant/monstre/PNJ), mais le
//         décalage vertical `pos.y + radius*0.5` de la formule d'origine dégénère en
//         `pos.y` pour tout le monde -- seuil des 10 unités légèrement moins précis
//         verticalement. Non corrigé ici (nécessiterait de brancher `info.drawSize` par
//         type d'entité depuis les tables `MONSTER_INFO`/`ITEM_INFO`/corps joueur, hors
//         périmètre de cette mission de vérification).
//       * VRAI SYSTÈME DE LOD TROUVÉ DANS LE BINAIRE, AU-DELÀ DU SIMPLE CULLING (cf.
//         Docs/TS2_GXD_ENGINE.md §2.6/§2.7/§3, EAs déjà relevés lors d'une session RE
//         antérieure) -- ENTIÈREMENT NON CÂBLÉ dans ClientSource à ce jour :
//           - `Model_Render 0x40EBB0` (appelé par `ModelObj_Draw 0x4D71B0` pour les
//             modèles skinnés placés/objets, EN AVAL du dispatcher `Char_Draw`) fait un
//             frustum-cull sphérique PUIS un « fondu LOD » (fade) piloté par la
//             distance/le brouillard, avant de choisir le sous-mesh à dessiner
//             (`Model_DrawSkinnedSubset 0x40CA40`) -- c'est un système de LOD RUNTIME
//             séparé et complémentaire du near/far-cull d'entité déjà documenté ci-dessus.
//           - Le format SOBJECT porte RÉELLEMENT plusieurs niveaux de détail géométrique
//             (1 à 4), générés hors-ligne par `cMesh_BuildProgressiveLOD 0x43BB00`
//             (D3DXCleanMesh -> WeldVertices -> ValidMesh -> GeneratePMesh -> SetNumFaces,
//             pipeline "progressive mesh" D3DX classique) et sérialisés par
//             `cMesh_SaveToFileWithLOD 0x43AC10`. Ce ne sont donc pas des cubes/LOD
//             inventés : le fichier `.SOBJECT` d'un modèle a vraiment plusieurs maillages
//             de détail décroissant embarqués.
//           - Côté ClientSource, `Gfx/MeshRenderer.h::SkinnedModel/SkinnedMesh` PORTE déjà
//             la structure de données (`std::vector<SkinnedLod> lods`, un niveau par
//             "subset" du parseur `Mesh_ReadFromFile`) et `MeshRenderer::DrawModel()`
//             accepte un paramètre `int lod` -- MAIS **aucun appelant ne le renseigne**.
//             `WorldRenderer::renderOne()` appelle systématiquement
//             `meshRenderer_.DrawModel(*model, pos, rotDeg, scaleVec, palette)` SANS 5e
//             argument -> `lod` vaut toujours 0 (le niveau le PLUS détaillé), quelle que
//             soit la distance caméra/joueur de l'entité (locale, distante, monstre ou
//             PNJ). Concrètement : la géométrie ne se simplifie JAMAIS à distance dans
//             ClientSource, contrairement au binaire d'origine.
//           - NON câblé intentionnellement dans cette mission (vérification/documentation,
//             pas de nouvelle RE) : la formule exacte du "fondu LOD" de `Model_Render`
//             (seuils de distance par palier, éventuelle dépendance au brouillard/à
//             `g_Opt_GfxDetailShadows`) n'a PAS pu être re-décompilée en direct cette
//             session (serveur MCP `idaTs2` indisponible/saturé -- accès concurrent
//             d'autres agents de la même vague). Câbler une sélection de LOD par distance
//             sans cette formule exacte serait une INVENTION de seuils, contraire à la
//             règle "IDA = unique vérité" -- laissé en TODO explicite pour une session
//             avec accès IDA disponible : décompiler `Model_Render 0x40EBB0` pour extraire
//             la formule de seuil/fondu, puis calculer `lod` dans `renderOne()` à partir
//             de `Distance3D(pos, cull.cameraPos)` avant l'appel à `DrawModel()`.
//           - Distinct : LOD TEXTURE (`g_TexLodLevel` / `dword_18C4EFC`, 0..3, saut de
//             mips dans `Tex_ReadPacked 0x417740`) est un réglage GLOBAL de qualité
//             (menu graphique), pas une bascule par distance/par entité -- hors périmètre
//             de cette mission.
//   - Nameplate : appel réel de game::ComputeNameplateInfo, dessiné via gfx::Font
//     (police propre à WorldRenderer, même pattern que UI/GameHud.h::font_).
//   - Ombre/reflet (EntityDrawLogic::ComputeEntityDrawFlags.showShadow/showReflection).
//
//     ///// RÉÉCRIT — Passe 4 / vague W5, front shadow-wiring (2026-07-16) /////
//     L'analyse précédente s'était arrêtée UN NIVEAU TROP TÔT et concluait « aucune ombre
//     portée n'est réellement dessinée par le client d'origine ». C'est FAUX, et c'est
//     corrigé ici. IDA (re-vérifié de bout en bout cette session) prouve DEUX chaînes
//     jumelles, l'une morte, l'autre vivante :
//
//     [A] VOLUME D'OMBRE STENCIL — MORT, INATTEIGNABLE DANS LE BINAIRE.
//         Char_DrawShadow 0x580CE0 / Npc_DrawMeshShadow 0x5800E0 /
//         Char_DrawWeaponEffectVariantA 0x568FE0 -> SObject_DrawAnimated 0x4D9050 ->
//         Model_RenderWithShadow 0x40EEE0 -> Model_BuildShadowVolume 0x40DC70.
//         Les 3 têtes ont 0 xref CHACUNE, et `find_bytes` de leurs adresses en
//         little-endian rend 0 occurrence dans TOUTE l'image (donc pas d'appel indirect
//         par vtable/table de pointeurs). Preuves détaillées : Gfx/MeshRenderer.cpp,
//         bandeau au-dessus de DrawModelShadow(). -> jamais dessiné : CORRECT de ne pas
//         le câbler (gfx::MeshRenderer::DrawModelShadow reste sans appelant, exprès).
//
//     [B] OMBRE PLANAIRE PROJETÉE — VIVANTE, et c'est la vraie ombre du jeu.
//         Model_RenderPlanarShadow 0x40F720 aplatit le modèle sur le plan du sol via
//         j_D3DXMatrixShadow @0x40FB28 (le commentaire de l'IDB sur la fonction dit
//         lui-même « render projected ground shadow via D3DXMatrixShadow »), en PASSE 5
//         = VS09 (g_GxdSh09_VS) + PS NULL. Plan sol (a,b,c,d) = floats +124/+128/+132/+136
//         de `a8[40] + 156*hitIdx`, issu de Collision_SegPickA 0x420D60.
//         `reaches(Scene_InGameRender 0x52D0B0 -> 0x40F720)` = true, profondeur 3.
//         Les devs ont DUPLIQUÉ la chaîne [A] puis basculé sur le planaire, orphelinant
//         le volume (fonctions jumelles, tailles identiques deux à deux) :
//              MORT [A]                                VIVANT [B]                     taille
//              Char_DrawShadow 0x580CE0                Char_DrawReflection 0x581090   0x3A4
//              Npc_DrawMeshShadow 0x5800E0             Npc_DrawMeshGlow 0x5801D0      0xE2
//              Char_DrawWeaponEffectVariantA 0x568FE0  Char_DrawWeaponEffectVariantB 0x56BF90  0x2AFF
//         SObject_DrawAnimated 0x4D9050 et SObject_DrawAnimated2 0x4D91C0 sont eux-mêmes
//         jumeaux (0x16F chacun) et ne diffèrent QUE par Model_RenderWithShadow vs
//         Model_RenderPlanarShadow (décompilation vérifiée : 0x4D91C0 n'appelle QUE 0x40F720).
//         => CONSÉQUENCE DE NOMMAGE : `Char_DrawReflection` / `Npc_DrawMeshGlow` sont MAL
//         NOMMÉES dans l'IDB. Ce ne sont ni un reflet ni un glow : ce sont les DESSINS
//         D'OMBRE (planaire) respectivement du monstre et du PNJ. (Renommage IDB non fait :
//         IDA est en lecture seule pour ce front.)
//
//     BRACKET DE SCÈNE (Scene_InGameRender 0x52D0B0, désassemblage relu ligne à ligne) —
//     la passe d'ombre est un bracket explicite, AVANT le rendu opaque :
//         0x52D9DC  GXD_SetupStencilShadowState(g_GxdRenderer)   <-- DÉBUT passe ombre
//           boucle i<g_EntityCount  : Char_DrawWeaponEffectVariantB(&g_EntityArray[908*i]) @0x52DA41
//           boucle i<g_NpcCount     : Npc_DrawMeshGlow(&g_NpcRenderArray[88*i])            @0x52DAA2
//           boucle i<g_MonsterCount : Char_DrawReflection(&dword_1766F74[280*i])           @0x52DB09
//         0x52DB15  GXD_EndStencilShadowState(g_GxdRenderer)     <-- FIN, puis l'opaque
//           (Char_DrawWeaponTrailEffect @0x52DB7C, Npc_DrawMesh @0x52DBDF, ...)
//     Donc : ombres planaires pour JOUEURS **et** PNJ **et** MONSTRES — pas seulement les
//     monstres comme le supposait la rédaction précédente.
//     États réels posés par GXD_SetupStencilShadowState 0x404F20 (décompilé, vérifié) :
//       LIGHTING(137)=0, SHADEMODE(9)=FLAT, ZWRITEENABLE(14)=0, STENCILENABLE(52)=1,
//       STENCILFUNC(56)=EQUAL(3), STENCILPASS(55)=INCR(7) (masque anti-double-blend),
//       ALPHABLENDENABLE(27)=1, SRCBLEND(19)=SRCALPHA(5), DESTBLEND(20)=INVSRCALPHA(6),
//       TEXTUREFACTOR(60)=(moyenne diffuse ×128)<<24, TSS0: COLOROP=SELECTARG1,
//       COLORARG1=TFACTOR, ALPHAARG1=TFACTOR.
//     (Rectification au passage : la rédaction précédente donnait « DESTBLEND=INVSRCCOLOR » —
//      c'est INVSRCALPHA(6). Et le fondu v37 de 0x40F720, avec fogNear=999999/fogFar=1000000,
//      sature TOUJOURS à 1.0 -> LOD 0 systématique.)
//
//     ÉTAT DU CÂBLAGE (assumé, et volontairement NON étendu par ce front) :
//       * showShadow -> toujours PAS de volume stencil : [A] est mort, cf. ci-dessus.
//       * showReflection -> drawReflectionOverlay() reste RÉSERVÉ AUX MONSTRES
//         (`reflectionEligible`, posé true seulement dans la boucle monstre de Render()).
//         Le nom « reflet » est conservé côté C++ pour ne pas diverger de l'IDB, mais il
//         désigne en réalité l'ombre planaire du monstre.
//         ÉCART DE FIDÉLITÉ ASSUMÉ ET EXPLICITE : drawReflectionOverlay() redessine le
//         modèle À LA MÊME TRANSFORMÉE, sans l'aplatissement D3DXMatrixShadow et sans le
//         bracket d'états — ce n'est donc PAS encore l'ombre planaire du binaire, c'est
//         une approximation. Reproduire [B] fidèlement exige le PLAN DU SOL issu de
//         Collision_SegPickA 0x420D60 (géométrie de collision du monde), qui n'existe pas
//         encore côté ClientSource et est HORS des fichiers possédés par ce front.
//         -> TODO [ancres 0x40F720 + 0x420D60 + bracket 0x52D9DC/0x52DB15] : implémenter
//            l'ombre planaire réelle (aplatissement + bracket + VS09) quand la source du
//            plan sol existera ; étendre alors aux joueurs et PNJ, pas seulement aux
//            monstres. Ne PAS « allumer » un aplatissement avec un plan sol inventé
//            (y=constante) : ce serait une invention, pas de la fidélité.
//         [DÉCISION ORCHESTRATEUR] Retirer purement drawReflectionOverlay() en attendant
//            (au motif qu'il ne correspond à rien d'exact) est un CHANGEMENT VISUEL :
//            laissé en place tel quel, non tranché unilatéralement par ce front.
//       * PNJ — `Npc_DrawMeshGlow` 0x5801D0 : toujours NON câblé, mais pour la raison
//         déjà en place (pas de source de données de rendu PNJ côté ClientSource :
//         `g_NpcRenderArray` stride 88 est un tableau SÉPARÉ de `dword_17AB534` stride 152
//         qui alimente game::NpcEntity), PAS parce que ce serait « un glow hors périmètre » :
//         c'est en fait l'ombre planaire du PNJ (cf. [B]).
//       * [NON VÉRIFIÉ] Sémantique exacte du champ +0xDC qui garde le méga-switch de
//         Char_DrawWeaponEffectVariantB 0x56BF90 (fonction de 11 Ko, non décompilée ici) :
//         il détermine QUELS SObjects attachés du joueur projettent une ombre. Ne pas
//         supposer que la boucle joueur du bracket ombre le corps entier sans condition.
//       * [NON VÉRIFIÉ] Valeur du STENCILREF pendant le bracket : jamais posée par 0x404F20
//         -> héritée (probablement 0 par défaut D3D9). À dumper en dynamique le jour où [B]
//         sera implémentée.
//
// TODO(fidélité) : PlayerEntity/MonsterEntity (Game/GameState.h) ne portent pas
// encore le nom/niveau/échelle/angle réels (payload réseau pas entièrement décodé
// côté GameState) — NameplateActor/EntityRenderState sont peuplés avec les seuls
// champs disponibles (position, PV) + des valeurs de repli documentées au site
// d'appel (WorldRenderer.cpp). Aucune valeur inventée n'est présentée comme
// certaine : les repères visuels (couleur/texte placeholder) restent identifiables
// comme tels.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

#include "Gfx/MeshRenderer.h"
#include "Gfx/Camera.h"
#include "Gfx/Font.h"
#include "Gfx/ModelCache.h"
#include "Game/EntityDrawLogic.h"
#include "Game/ExtraDatabases.h" // game::NpcDefRecord (PNJ de decor, cf. DrawableEntity::npcDef)

namespace ts2 {

namespace gfx { class Renderer; }

// WorldRenderer — dessine chaque frame InGame le contenu de game::g_World
// (joueurs + monstres + npcs, cf. bandeau) : corps (modèle ou placeholder)
// + nameplate. Un seul MeshRenderer/Font est partagé entre toutes les entités de
// la frame (même pattern que ts2::ui::GameHud : une police par composant, pas de
// singleton global de police).
class WorldRenderer {
public:
    ~WorldRenderer() { Shutdown(); }
    WorldRenderer() = default;
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Construit MeshRenderer (décl. vertex + shaders skinnés), le placeholder cube
    // (D3DXCreateBox) et la police des nameplates. Renvoie false si le device est nul
    // ou si MeshRenderer::Init échoue (le placeholder cube/police restent best-effort :
    // leur échec dégrade le rendu mais ne bloque pas l'init globale).
    bool Init(gfx::Renderer& renderer, int screenW, int screenH);
    void Shutdown();

    void OnDeviceLost();
    void OnDeviceReset();

    // Dessine toutes les entités actives de game::g_World (players puis monsters
    // puis npcs) + leurs nameplates, avec les matrices de `camera`. Ne fait rien
    // si non prêt.
    void Render(const gfx::Camera& camera);

private:
    // -----------------------------------------------------------------------
    // Point d'extension modèle réel — CÂBLÉ sur Gfx/ModelCache (mission
    // "câbler ResolveModel()", 2026-07-14, cf. bandeau de tête pour le détail
    // complet par type d'entité). L'ancienne signature unique
    // ResolveModel(modelCategoryId, motionIndex) ne correspondait à AUCUNE API
    // réelle de ModelCache (qui résout par itemId ITEM_INFO pour un item, ou par
    // monsterDefId/MONSTER_INFO pour un monstre — jamais par un couple générique
    // catégorie/motion) : remplacée par deux points d'entrée alignés sur les
    // signatures RÉELLES du cache (cf. Gfx/ModelCache.h::GetForItem/GetForMonster).
    // -----------------------------------------------------------------------
    const gfx::SkinnedModel* ResolveMonsterModel(uint32_t monsterDefId); // corps monstre
    const gfx::SkinnedModel* ResolveWeaponModel(uint32_t weaponItemId);  // arme joueur (self + distant, cf. bandeau)
    // Corps PNJ DE DECOR (mission "PNJ DECOR VISIBLES A L'ECRAN", 2026-07-14, cf. bandeau
    // de tete §"PNJ" pour la distinction avec le tableau gameplay) : delegue a
    // Gfx/ModelCache.h::GetForNpc(NpcDefRecord&), RESOLU depuis une mission anterieure
    // (npc.fieldE +1324 = kindIndex+1, cf. ModelCache.cpp). nullptr si npcDef est nul ou si
    // fieldE est hors bornes [1,66] (repli cube dans renderOne, jamais d'exception).
    const gfx::SkinnedModel* ResolveNpcModel(const game::NpcDefRecord* npcDef);
    // Corps de base joueur (SLOT0+SLOT1, cf. bandeau "JOUEURS" + Gfx/ModelCache.h::
    // GetForPlayerBody). race/gender/costumeSlot0/costumeSlot1 = PlayerEntity::body+68/72/76/80,
    // valables pour self ET distants sans distinction (cf. bandeau).
    gfx::PlayerBodyModel ResolvePlayerBodyModel(int race, int gender, int costumeSlot0, int costumeSlot1);

    bool buildPlaceholderCube(IDirect3DDevice9* dev);
    // rotYDeg (mission ROTATION/ORIENTATION, 2026-07-14) : cap horizontal en degrés,
    // MÊME convention que MeshRenderer::DrawModel (rotationDeg.y, matrice
    // S*Rz*Ry*Rx*T) -- appliqué même au cube placeholder pour que le repère visuel
    // reste cohérent en orientation avec le vrai modèle qui pourra le remplacer plus
    // tard (pas d'effet visuel notable sur un cube symétrique en Y, mais évite un
    // écart de matrice monde entre les deux chemins de dessin).
    void drawPlaceholderCube(const D3DXVECTOR3& pos, float scale, D3DCOLOR color,
                             float rotYDeg, const D3DXMATRIX& view, const D3DXMATRIX& proj);

    // Passe reflet (Char_DrawReflection 0x581090) : MÊME transformée que le corps
    // (cf. bandeau ci-dessus), redessinée sur le vrai mesh monstre quand il est
    // résolu. N'est appelée par renderOne() QUE si DrawableEntity::reflectionEligible
    // est vrai (monstres uniquement, cf. bandeau "VÉRIFICATION APPROFONDIE 2026-07-14") :
    // cette fonction elle-même ne fait aucune hypothèse de type d'entité, la garde
    // est posée au site d'appel comme kNpcFarCullDistanceSq.
    void drawReflectionOverlay(const gfx::SkinnedModel* bodyModel, const D3DXVECTOR3& pos,
                               float scale, float rotYDeg, const D3DXMATRIX& view,
                               const D3DXMATRIX& proj);

    bool worldToScreen(const D3DXVECTOR3& world, const D3DXMATRIX& viewProj,
                       int& sx, int& sy) const;
    void drawEntityLabel(const std::string& text, const D3DXVECTOR3& worldPos,
                         D3DCOLOR color, const D3DXMATRIX& viewProj);

    // Une entité déjà normalisée pour la boucle de rendu (players/monsters/npcs
    // convergent tous ici, cf. WorldRenderer.cpp).
    struct DrawableEntity {
        game::EntityRenderState renderState; // vue EntityDrawLogic (pos/hp/scaleY/...)
        bool        notSelf = true;          // a3 d'origine (index de boucle != 0)
        std::string name;                    // nom REEL pour les joueurs (PlayerEntity::name,
                                              // cf. WorldRenderer.cpp::Render) ; placeholder
                                              // "Monster#i"/"Npc#i" pour monstres/PNJ (noms
                                              // reels hors perimetre, cf. bandeau)
        D3DCOLOR    placeholderColor = 0xFFFFFFFFu;

        // Entrées de résolution modèle réel (mission ModelCache, cf. bandeau de tête) :
        // 0 = non résolu -> cube placeholder pour la partie correspondante. Un monstre
        // avec monsterDefId!=0 remplace ENTIÈREMENT le cube ; un joueur avec hasBody=true
        // remplace le cube-corps par les pièces SLOT0/SLOT1 résolues (cf. bandeau
        // "JOUEURS") ; weaponItemId!=0 AJOUTE l'arme par-dessus le corps (modèle réel ou
        // cube, cf. renderOne). Jamais monsterDefId ET hasBody à la fois en pratique (un
        // monstre n'a pas de race/genre/costume, un joueur n'a pas de monsterDefId).
        // weaponItemId est peuplé pour TOUT joueur actif (self ET distant, cf. bandeau
        // "câblage arme joueur distant" -> PlayerEntity::body+148 pour les distants).
        uint32_t    monsterDefId = 0;
        uint32_t    weaponItemId = 0;

        // PNJ DE DÉCOR (mission "PNJ DECOR VISIBLES A L'ÉCRAN", 2026-07-14, cf. bandeau de
        // tête §"PNJ") : non-nul UNIQUEMENT pour les entrées de `game::ZoneNpcs()`
        // (StaticNpcLoader) — jamais pour joueurs/monstres/PNJ gameplay. Consommé par
        // `ResolveNpcModel()` dans `renderOne()`, EN PLACE de `monsterDefId` (les deux ne
        // sont jamais renseignés simultanément) : `monsterDefId != 0` a priorité, sinon on
        // tente `npcDef`, sinon repli cube.
        const game::NpcDefRecord* npcDef = nullptr;

        // Corps de base joueur (mission "câblage corps de base joueur", 2026-07-14, cf.
        // bandeau "JOUEURS") : hasBody=true pour toute entité PlayerEntity active (self
        // ET distants, jamais pour monstres/PNJ) ; race/gender/costumeSlot0/costumeSlot1
        // = lecture brute de PlayerEntity::body+68/72/76/80 (cf. WorldRenderer.cpp::Render).
        bool hasBody         = false;
        int  bodyRace        = 0;
        int  bodyGender      = 0;
        int  bodyCostumeSlot0 = 0;
        int  bodyCostumeSlot1 = 0;

        // Éligibilité au reflet (mission "EXTENSION OMBRE/REFLET", 2026-07-14, cf.
        // bandeau de tête § Ombre/reflet) : true UNIQUEMENT pour les monstres.
        // `Char_DrawReflection` 0x581090 n'a, dans tout le binaire, qu'un seul
        // appelant (`xrefs_to` confirmé), lui-même dans la boucle MONSTRE de
        // `Scene_InGameRender` -- jamais sur le tableau joueurs ni PNJ. Poser ce
        // drapeau à true pour un joueur ou un PNJ serait une invention (aucun appel
        // correspondant dans le désassemblage) : NE PAS l'étendre sans nouvelle
        // preuve de décompilation contraire.
        bool reflectionEligible = false;

        // Éligibilité au CORPS 3D (mission "PNJ GAMEPLAY SANS CORPS", RE 2026-07-15) :
        // true par défaut. Posé à false UNIQUEMENT pour les PNJ GAMEPLAY (g_World.npcs,
        // tableau réseau dword_17AB534) — PROUVÉ par RE idaTs2 : leur champ `def`
        // (dword_17AB598 = record ITEM_INFO) n'est lu par AUCUNE fonction de rendu
        // (data_refs 0x17AB598 = interaction/autoplay uniquement), et les 3 boucles PNJ
        // réseau de Scene_InGameRender 0x52D0B0 (0x52dc84/0x52ec5b/0x52fcae) n'appellent
        // que Char_DrawAura / Fx_DrawZoneAura / ModelObj_Draw(marqueur de quête) /
        // Char_DrawNameTag — JAMAIS SObject_DrawEx ni Char_Draw (aucun corps de mesh).
        // Le corps 3D des PNJ est EXCLUSIVEMENT dessiné par Npc_DrawMesh 0x57FF00 sur le
        // tableau SÉPARÉ g_NpcRenderArray 0x1764D14 (peuplé par Pkt_EnterWorld depuis les
        // PNJ de décor de zone => boucle ZoneNpcs ci-dessous). Dessiner un corps (a
        // fortiori un cube) pour un PNJ gameplay serait donc une INFIDÉLITÉ : quand ce
        // drapeau est false, renderOne() n'émet ni modèle ni cube (ni reflet), seulement
        // la nameplate.
        bool bodyMeshEligible = true;
    };
    void renderOne(const DrawableEntity& ent, const game::DrawCullContext& cull,
                  const D3DXMATRIX& view, const D3DXMATRIX& proj, const D3DXMATRIX& viewProj);

    // FRONT FX-F4 (M1) : slots shaders REELS du npk GXDEffect (Shader03 VS03_SkinnedLit 0x409AB0
    // + Shader04 PS04_Tex 0x409CC0 + VS15 volume d'ombre 0x40ACB0), charges depuis
    // "./GXDEFFECT/GXDEffect.npk" (cle XTEA {1,4,4,1}, cf. Shader_LoadVS03 0x409AB0 : Npk_OpenFile
    // + Npk_FindEntryByName("Shader03.fx") + j_D3DXCompileShader "Main"/"vs_2_0" + GetConstantByName
    // mKeyMatrix/mWorldViewProjMatrix/mLightDirection/mLightAmbient/mLightDiffuse). POSSEDE ici
    // (duree de vie = WorldRenderer) ; meshRenderer_.AttachShaderSet(&shaderSet_) n'en prend qu'une
    // reference NON possedante. Sans ce cablage, DrawSkinnedSubset retombe sur le HLSL reconstruit
    // (fallback) et les vrais Shader03/04 du npk ne sont jamais utilises (cf. MeshRenderer.cpp:510).
    // DECLARE AVANT meshRenderer_ : les membres etant detruits en ordre inverse de declaration,
    // ceci garantit que ~MeshRenderer (qui lache sa reference) s'execute AVANT ~ShaderSet (qui
    // libere VS/PS/CT/decl) meme sans Shutdown() explicite.
    gfx::ShaderSet    shaderSet_;
    gfx::MeshRenderer meshRenderer_;
    gfx::Font         font_;
    std::unique_ptr<gfx::ModelCache> modelCache_; // cf. Init() pour le choix de gameDataDir="."
    IDirect3DDevice9* device_    = nullptr;
    ID3DXMesh*        cubeMesh_  = nullptr; // placeholder (D3DXCreateBox 1x1x1)
    int  screenW_ = 0, screenH_ = 0;
    bool ready_   = false;
};

} // namespace ts2

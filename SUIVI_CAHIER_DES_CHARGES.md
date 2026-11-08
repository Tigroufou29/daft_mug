# Suivi du cahier des charges Daft Mug

Légende : ✅ fait et testé · 🟡 partiel · ❌ pas commencé. « Testé » = testé sous Linux (tests moteur + test de fumée) ; rien n'a encore été essayé sous Windows ni dans un DAW.

## Phase 1 — fondations (jalon 1)

| Exigence (§) | État | Détail |
|---|---|---|
| CMake + JUCE + VST3 + Standalone, moteur commun (§2) | ✅ / 🟡 | VST3 compile (GCC/Linux). Standalone et MSVC non compilés ici. |
| Fenêtre Liquid Glass sombre, redimensionnable (§3) | ✅ / 🟡 | Rendu vérifié par capture. Pas d'animations autres que le flash des pads ; couleurs d'accent non configurables ; pas de réglage d'échelle dédié. |
| 16 pads 4×4, numéro, nom, couleur, voyant d'état, flash, note MIDI, menu contextuel (§4) | ✅ | Couleur choisie dans une palette de 16, pas de sélecteur libre. |
| Rouage en bas à gauche, zone de clic distincte (§4) | ✅ | Test géométrique ; clic souris réel non essayé. |
| États vide / chargé / lecture / sélectionné / chargement / erreur (§4) | ✅ | |
| Retour visuel MIDI, souris, DAW (§4) | ✅ | Piloté par l'activité réelle du moteur (compteur de déclenchements). |
| Sélection ≠ déclenchement (§4) | 🟡 | Un clic sur un pad chargé le sélectionne ET le joue (visuels distincts). Pas de moyen de sélectionner sans jouer. |
| Note On/Off, vélocité, notes simultanées, polyphonie, décalage dans le bloc (§5) | ✅ | |
| MIDI Learn + saisie manuelle de la note (§5) | ✅ | |
| Modes One-shot / Gate / Retrigger (§5) | ✅ | |
| Courbe de vélocité **configurable** (§5) | ✅ | Sensibilité + courbe (étape 2a). |
| Politique de voix **configurable** (limite, vol) (§5, §13) | ✅ | Voix max, voix par pad, vol Oldest/Quietest/IgnoreNew, dans Options (étape 2a). |
| Changements de programme / banque (§5) | ❌ | Prévu « lorsque implémenté » (banques : phase 3). |
| Paramètres automatisables VST3 par pad (§5) | 🟡 | 8 par pad (volume, pitch, pan, mode, note, sensibilité, début, fin). Attack, fondus, EQ, réverbe, ADSR : n'existent pas encore. |
| Automatisation sans clic en cours de lecture (§5, §15) | ✅ | Volume/pitch/pan lissés en direct, EQ lissé et fondu (étapes 2a/2b). |
| Aucun blocage / E-S / réseau sur le thread audio (§13) | ✅ | Files sans attente, libération des samples côté interface. |
| Import fichier + glisser-déposer, WAV/AIFF/FLAC/MP3 (§6) | ✅ | MP3 testé avec un vrai fichier. Glisser-déposer sur un pad ET sur l'éditeur d'un pad ; la logique de dépôt est testée par appel direct, pas avec un vrai glisser de l'explorateur Windows. Un seul fichier par dépôt (le premier fichier audio). |
| Erreurs : absent, format, endommagé, mémoire, accès refusé (§6) | ✅ | Mémoire insuffisante et accès refusé corrigés pendant cet audit. |
| Mémoire : un même fichier utilisé par plusieurs pads n'est décodé et stocké qu'une fois | ✅ Cache partagé (16 tranches d'une boucle = une seule copie) ; testé |
| Retrouver un fichier manquant (§14) | 🟡 | Erreur affichée ; pas de fonction « localiser le fichier » dédiée (on recharge via le menu du pad). |
| Préécoute + indicateur de progression (§6) | 🟡 | Préécoute par « Jouer » dans l'éditeur ; voyant « chargement » animé, pas de barre de progression. |
| Éditeur indépendant par pad, réglages conservés (§4, §7) | ✅ | Vue plein cadre ouverte par le rouage (pas une fenêtre séparée). |
| Volume, pitch (demi-tons à 0,01 = 1 cent), pan, saisie clavier (§7 B, C) | ✅ | Pitch = transposition avec changement de durée, interpolation linéaire. |
| Forme d'onde, début/fin non destructifs, retour de lecture (§7 A) | ✅ | Zoom, défilement, règle, marqueurs (2a). Préécoute de sélection seule : absente. |
| Sauvegarde/restauration (pads, chemins, noms, couleurs, paramètres), schéma versionné (§14) | ✅ | Pas de secret dans l'état (vérifié). Migration = point d'accroche prêt, aucune migration réelle encore. |
| Export WAV d'un pad (§10 E) | ✅ | 16/24/32 bits, 44,1/48/96 kHz, normalisation. Effets inclus quand ils existeront. |
| Badge « MIDI IN » | ✅ | Était décoratif ; il s'allume maintenant sur les vrais Note On/Off. |

## Phase 2 — éditeur audio (jalon 2, en cours : 2a, 2b, effets et réverbe de 2c faits ; phase 2 terminée)

| Exigence | État |
|---|---|
| Attack, Fade-in, Fade-out distincts (+ graphe d'enveloppe), ADSR (§7 D) | ✅ |
| Reverse (§7 E) | ✅ |
| Palette d'outils de la maquette : couper, inverser, réinitialiser | ❌ |
| EQ 4 bandes par pad + courbe de réponse (§8) | ✅ Bell/shelf/HP/LP, nœuds glissables, molette = Q, double-clic = reset, tout automatisable. Testé (réponse mesurée vs théorique, sans clic). Non écouté dans un DAW. |
| Écoute isolée d'une bande de l'EQ (solo) — demandé après la phase 2 | ✅ Bouton « Écouter la bande » : passe-bande à la fréquence et au Q de la bande, suit la bande sélectionnée, fondu sans clic, testé. Aide d'écoute : non automatable, non sauvegardée, ignorée par l'export. Seul le signal à l'entrée de l'EQ est écouté (les effets placés après, chorus/delay/réverbe, restent actifs). |
| EQ dynamique par bande (seuil, ratio, attack, release, knee, range, Compress/Expand) — décidé pendant la maquette | ✅ Détecteur passe-bande dans la bande, gain de la bande modulé de 0 à 100 %. Testé (courbe statique, compress/expand, sélectivité fréquentielle, sans clic). Définition précise : voir Eq.h. Non écouté dans un DAW ; la courbe de l'interface montre le gain statique, pas la modulation en direct. |
| Réverbération à convolution : liste déroulante Aucune / Plate / Hall / Stadium / SFX / Reverb perso, un seul moteur par pad, Wet 0–100 % (§9) | ✅ Moteur FFT par partitions (validé contre une convolution directe), chargement hors temps réel, rééchantillonnage, IR perso sauvegardée dans le projet, incluse dans l'export WAV. **Testée uniquement avec une IR synthétique** : aucune de vos IR n'a encore été essayée. Limites : IR tronquée à 4 s, latence du signal réverbéré 512 échantillons (~10 ms), coût CPU proportionnel à la longueur de l'IR et au nombre de pads actifs avec réverbe. |
| Dossier d'IR + consignes pour ajouter vos fichiers (§9, §18.8) | ✅ Dossier `IR/` (vide) : Plate.wav, Hall.wav, Stadium.wav, SFX.wav. Recherche dans cet ordre : variable DAFTMUG_IR_DIR, dossier utilisateur DaftMug/IR, dossier `IR/` du projet. |
| Saturation (Douce/Franche/Tube), bitcrusher, chorus, delay — décidés pendant la maquette | ✅ Par pad, automatisables, testés (moteur + ASan). Limites : saturation sans sur-échantillonnage (un peu d'aliasing à fort drive), chorus à 1 voix, delay max 1 s. Non écoutés dans un DAW. |
| Automatisation hôte de tous ces paramètres | ✅ EQ (dont dynamique), effets et réverbe |
| Profils / presets de pad et de kit, catégories (§10 D) ; « profils utilisateur » de la maquette | ✅ Presets de pad (.dmpad, par catégorie, réglages seuls ou avec chemin du sample, sans la note MIDI) et kits (.dmkit, 16 pads + notes + noms + couleurs + voix). Dossier `DaftMug/Presets` (ou variable DAFTMUG_PRESET_DIR). Fichiers XML sans secret, fichiers corrompus/futurs refusés. Testé. Limite : un kit stocke des chemins absolus de samples (collection portable : phase 4). |

## Phases 3 à 5 (à faire)

**Slice to Pads ✅** (détection de transitoires ou parts égales, sensibilité / longueur min. / nombre de tranches configurables, repères déplaçables, ajout/suppression, calage sur passage par zéro, écoute de chaque tranche, répartition sur les pads à partir d'un pad de départ, annulation ; non destructif : les pads partagent le même audio décodé et ne diffèrent que par début/fin ; testé, détection jugée sur signaux synthétiques seulement, pas sur vos boucles réelles) · **Mutate ✅** (variantes de réglages : pitch, pan, volume, attack, gains EQ, dosage de réverbe ; variante 0 = original ; intensité et nombre réglables ; écoute sur le pad sans changer les paramètres ; appliquer / annuler ; sample jamais touché ; testé, pas écouté dans un DAW) · banques, collecte de samples / projets portables avec chemins relatifs (§10) ❌ ·
Module Gemini : page Options → IA et confidentialité, clé dans le coffre de l'OS, test de connexion, recettes validées par liste blanche, préécoute/annulation (§11, §12) ❌ ·
Page de conditions d'utilisation (§12) ❌ · Stabilité, optimisation CPU, documentation utilisateur, installateur Windows, version macOS (§17 phase 5) ❌.

## Tests (§16)

| Fait | Manquant |
|---|---|
| Chargement/lecture, souris, MIDI, notes simultanées, Note On/Off, vélocité, MIDI Learn, indépendance des pads, sauvegarde/restauration, remplacement de sample, fichiers manquants, export | Automatisation par la STAN, réouverture répétée des éditeurs, changement de fréquence d'échantillonnage en cours de route, IR, tout ce qui touche Gemini, annulation de recette |
| Aucun test sous Windows, dans un DAW réel (Cubase, Studio One…), ni du Standalone | À faire de votre côté après compilation |

## Écarts avec le cahier des charges initial décidés avec vous

- Usage **personnel uniquement** : la contrainte « dépendances compatibles avec une distribution commerciale » (§2) est levée. MP3 activé ; ASIO/AAX disponibles via options CMake si vous fournissez les SDK (non testés).
- La maquette a des éléments **pas encore dans le vrai logiciel** : menu PROFIL, bouton Options, bascule IA, liste déroulante de réverbe. Ils arriveront avec leurs phases.

## Logo
- ✅ Logo néon intégré : Source/Resources/logo.png (embarqué via juce_add_binary_data), affiché dans l'en-tête, icône du plugin (Source/Resources/icon.png). Vérifié sur capture Linux uniquement.

## Kit Morph
- ⛔ Retiré à la demande de l'utilisateur (bouton, panneau, état MORPH supprimés).

## Banks
- ✅ Plugin/Banks.* + UI/BankPanel.* : une bank (.dmbank) est une liste ordonnée de kits (chemins relatifs, 128 max) ; panneau Banks (créer, supprimer, ajouter/retirer/réordonner, charger, précédent/suivant) ; MIDI : Bank Select (CC0/CC32) + Program Change n chargent le kit n de la bank ; bank et programme actifs sauvegardés dans l'état. Chemins protégés contre « .. ». Testé (smoke test Linux), non testé avec un vrai clavier/DAW.
- Remarque : l'export WAV par pad existe ; un export audio global du kit reste à faire.

## Collecte portable
- ✅ Plugin/Collector.* : « Kits ▾ › Collecter les fichiers du kit dans un dossier… » copie samples (Samples/) et IR perso (IR/) dans un dossier, gère les collisions de noms (_2, _3), réutilise les fichiers identiques, signale les fichiers manquants, ne touche jamais aux originaux, rattache les pads aux copies et écrit un .dmkit à chemins RELATIFS. « Charger un kit depuis un fichier… » le recharge (même après déplacement du dossier). Testé (smoke test Linux). Limites : IR de bibliothèque (Plate, Hall…) non copiées ; copie synchrone (bloque l'interface le temps de la copie).

## Gemini (Daft Mug AI) — Phase 4
- ✅ Source/AI : catalogue en liste blanche (plages lues sur les vrais paramètres), validation stricte (champ inconnu = réponse refusée ; paramètre inconnu / hors plage / non numérique = étape refusée, jamais écrêtée), service HTTPS (clé uniquement en en-tête x-goog-api-key, hôte verrouillé, erreurs expurgées), test de connexion qui liste les modèles generateContent, stockage de clé : Gestionnaire d'informations d'identification Windows (code écrit, NON compilé ici), mémoire de session ailleurs.
- ✅ UI : Options › IA et confidentialité (activation, clé masquée/afficher, enregistrer/effacer, test, modèle, infos données Google et coûts), Conditions d'utilisation (PROJET de texte à valider/compléter), panneau Assistant IA par pad (3 modes : réglages, recette, explication ; prévisualiser, comparer, accepter, restaurer ; fermeture sans accepter = restauration).
- ✅ Tests (smoke, faux serveur Gemini) : opt-in, validation de clé, liste de modèles, erreurs sans fuite de clé, aucune clé dans l'état/presets/kits/fichier de réglages, aucun chemin de fichier envoyé, validation stricte, flux complet de l'assistant.
- ⚠ Bug corrigé dans le code AI existant : var JSON temporaire détruite avant usage (pointeur invalide) dans testConnection et ask.
- ❌ NON testé : appel réel à l'API Gemini (aucun accès réseau ici ; format de requête/réponse écrit d'après la documentation que je connais, à valider avec votre clé), Credential Manager Windows, noms de modèles réels, transport HTTPS JUCE sous Windows (WinINet).
- ❌ Reste : analyse audio directe (non prévue en v1), classification de samples, textes légaux définitifs.

## Export audio du kit
- ✅ Plugin/KitExporter.* : « Kits ▾ › Exporter tous les pads en WAV (24 bits)… » : un WAV par pad chargé (nom « 01_Nom.wav », noms rendus sûrs, jamais d'écrasement), rendu hors thread audio avec EQ/effets/reverbe. Testé (smoke test).
- README mis à jour.

## Distribution (non testé)
- ✅ écrit : installer/DaftMug.iss (Inno Setup, VST3 + autonome), .github/workflows/build.yml (MSVC + macOS), stockage de clé dans le Trousseau macOS (Source/AI/SecretStore.cpp). AUCUN des trois n'a été exécuté/compilé ici.
- ❌ Signature de code, notarisation macOS, AAX signé.

## Vérification mémoire
- ✅ Test de fumée complet du plug-in sous AddressSanitizer + UBSan : PASSE. Il a révélé un vrai bug (pointeur invalide dans BankPanel::saveCurrent après rechargement de la liste), corrigé ; avec la correction du module IA vu plus haut. Fuites mémoire non analysées (detect_leaks=0).

## Fenêtre autonome
- ✅ Bouton agrandir (carré) ajouté à la barre de titre de l'application autonome + F11 plein écran (Échap pour quitter). Code écrit, vérifié seulement qu'il compile sous Linux ; effet réel à confirmer sous Windows.

## Ajustements interface (demande du 31/10)
- ✅ Éditeur de pad : « SAMPLE ET EXPORT » en première position, réduit à Charger / Vider / Normaliser / Exporter en WAV (24 bits, fréquence d'origine). Audition, Découper, Mutate, bits/fréquence retirés de ce module (Slice et Mutate restent au clic droit sur un pad). Chevauchement du haut droite corrigé (le libellé « ÉDITEUR — PAD » supprimé).
- ✅ Pads : un clic sur un pad vide ne fait plus rien ; un pad chargé joue au clic ; icône dossier (à côté du rouage) pour importer ; le pad change nettement de couleur à chaque déclenchement (souris ou MIDI). Testé par captures sous Linux ; ressenti à valider sous Windows.
- ✅ Logo plus grand (en-tête 112 px).

## Ajustements (demande du 31/10, après-midi)
- ✅ Éditeur de pad : barre Espace = joue le pad (maintenue pour Gate) + bouton « ▶ Écouter (Espace) » ; les boutons ne volent plus le focus clavier. Testé (smoke test : l'Espace déclenche le pad) ; non testé sous Windows.
- ✅ « Tester la connexion » renommé « Authentifier la clé API ».
- ✅ Assistant IA : bouton « Jouer (maintenir) » retiré ; Prévisualiser / Écouter l'original / Écouter la proposition jouent maintenant le pad automatiquement (le pad doit contenir un sample). Retour après Accepter = éditeur du pad avec les réglages acceptés.

## Ajustements (demande du 31/10, soir)
- ✅ Module EQ : forme d'onde du sample affichée au-dessus de la courbe + spectre (contenu fréquentiel) tracé derrière la courbe. Testé par capture ; le spectre est une estimation visuelle (Goertzel sur le passage le plus fort du début du sample).
- ✅ Réverbération à convolution : liste déroulante (vide au départ : « Choisir une IR… ») + bouton « Importer des IR… » (copie vos WAV/AIFF/FLAC dans le dossier IR de Daft Mug, %APPDATA%\DaftMug\IR) ; les IR importées apparaissent dans la liste et s'appliquent à la sélection ; « Aucune réverbération » désactive l'effet. Testé (import, sélection, paramètre) ; non testé avec de vraies IR sous Windows. Anciens projets avec Plate/Hall/Stadium/SFX : les fichiers du dossier IR s'affichent et la sélection est conservée.
- ✅ Thème inspiré du logo : cyan néon, magenta/violet, rose, fond indigo profond ; liseré néon des vitres ; nouvelle palette par défaut des pads (les kits déjà sauvegardés gardent leurs couleurs).

## Ajustements (demande du 31/10, 17 h)
- ✅ Égaliseur : analyseur de spectre EN TEMPS RÉEL (comme les égaliseurs du commerce) : le moteur copie le signal du pad (après EQ) dans un tampon, l'interface calcule le spectre 30 fois par seconde (montée rapide, descente lente, pente +4,5 dB/oct) ; il bouge avec le son joué et retombe au silence. Le profil moyen du sample reste en trait discret. Testé (le tampon reçoit le signal, capture avec spectre d'un sinus) ; non écouté / non mesuré sous Windows.
- ✅ Réverbe : bouton « Ouvrir le dossier IR » retiré, il ne reste que « Importer des IR… ».

## Ajustements (demande du 01/11)
- ✅ Égaliseur : profil fixe du sample retiré, seul l'analyseur temps réel reste.
- ✅ Champs de texte (nom de pad, assistant IA, clé API, conditions) : Ctrl+A (Windows/Linux) et Cmd+A (macOS) sélectionnent tout ; un clic dans le champ sélectionne tout le texte. Testé en simulation de touches (Linux) ; non testé au clavier réel Windows / Mac.
- ✅ Image de fond (Source/Resources/background.jpg) sur l'interface principale, ajustée à la fenêtre (recadrage centré), voile sombre pour la lisibilité des pads. Nouveau fichier de ressource : relancer `cmake -S . -B build`.

## Ajustements (demande du 01/11, midi)
- ✅ Image de fond remplacée (nouvelle image avec « Oxy Gen Prod. »), voile sombre allégé. Recadrage centré : le logo Oxy Gen en bas à droite n'est entièrement visible qu'en format 16:9.
- ✅ Conditions d'utilisation remplacées par le texte fourni (sections 1 à 9). Accessibles par Options › IA et confidentialité › Conditions d'utilisation, et en cliquant sur le logo Daft Mug.
- ⛔ Banks supprimées à la demande de l'utilisateur : bouton, panneau, gestion des listes de kits, Bank Select / Program Change MIDI, propriétés « bank » / « program » de l'état (les anciens projets restent lisibles). Les presets de kit (menu Kits) restent.
- ✅ Clic sur le logo Daft Mug : ouvre les conditions d'utilisation (Retour ramène à l'interface principale).

## Installeur Windows (demande du 01/11)
- ✅ installer/DaftMug.iss (Inno Setup 6) : choix VST3 seul / application autonome seule / les deux (+ personnalisée), raccourci Bureau facultatif, désinstallation, dossier IR conservé. **Non testé** (pas d'Inno Setup ni de Windows chez l'auteur) : à tester sur un PC propre.
- ✅ CMakeLists : runtime C++ lié statiquement sous MSVC (installeur utilisable sans Visual C++ Redistributable). Impose une recompilation complète.
- ❌ Pas de signature de code : Windows SmartScreen affichera un avertissement « éditeur inconnu ».

## VU-mètre de sortie (demande du 01/11)
- ✅ UI/MasterMeter.* : niveau de sortie stéréo (L/R) à gauche des pads, sous le logo : barre = RMS (intégration 300 ms), trait blanc = crête (descente 24 dB/s) + maintien 1,5 s, échelle −60 à 0 dB, lecture numérique PIC / RMS / PIC−RMS (dynamique), voyant de saturation (crête ≥ −0,2 dBFS) ; un clic efface maintien et voyant. Mesure faite dans le processeur sur la sortie du plug-in (avant le volume de la piste de l'hôte). Testé (smoke test : valeurs cohérentes, capture) ; crêtes = crêtes d'échantillons, pas du « true peak » inter-échantillons ; ni LUFS ni calibrage mesuré avec un appareil.

## Tempo par sample + ressources (demande du 01/11, soir)
- ✅ Engine/Tempo.* : détection du tempo (force d'attaque + autocorrélation, repli 75–165 BPM, boutons ÷2 / ×2 pour corriger) à chaque chargement ; les sons isolés (< 1,5 s) ne donnent pas de tempo. Testé sur boucles synthétiques (90 / 120 / 140 BPM exacts) ; **non testé sur vos vraies boucles** (la détection peut se tromper sur des rythmes complexes ou sans percussions : le tempo d'origine est modifiable à la main).
- ✅ Étirement temporel sans changer la hauteur (WSOLA), de ×0,5 à ×2 du tempo d'origine : le pad joue une copie étirée calculée en arrière-plan, le fichier d'origine n'est jamais modifié. Carte « TEMPO ET ÉTIREMENT » dans l'éditeur du pad (tempo d'origine, tempo cible, Détecter, ÷2, ×2, Tempo de l'hôte, Appliquer, Rétablir). Tempo d'origine / cible sauvegardés dans le projet (recalculés au rechargement). Testé : durée, hauteur conservée (440 Hz), niveau, sauvegarde / restauration. Qualité sonore à juger à l'oreille (WSOLA : bon sur boucles et percussions, rugosité possible sur sons tenus avec de forts écarts).
- ⚠ L'export WAV du pad exporte la version étirée si un tempo cible est appliqué. Les presets de kit ne mémorisent pas le tempo.
- ✅ Tempo de l'hôte : le bouton règle le tempo cible sur celui du logiciel de musique (plug-in ; grisé en application autonome).
- ✅ Ligne d'état : CPU audio (part du temps audio utilisée par Daft Mug, lissée), RAM samples (audio décodé en mémoire), RAM processus (mémoire du processus : dans un logiciel de musique, c'est celle du logiciel entier). Lien psapi sous Windows.

## Performance de l'interface (retour utilisateur 01/11 : lenteurs sur PC fixe avec carte AMD dédiée, rendu propre sur portable)
- ✅ Options › AFFICHAGE (Windows) : choix du rendu de l'interface (Automatique / Logiciel processeur / Direct2D carte graphique), appliqué immédiatement, mémorisé dans %APPDATA%\DaftMug\ui.settings. Permet de trouver le mode fluide sur chaque machine. **Cause des lenteurs non confirmée** : hypothèse = rendu Direct2D / pilote ; à tester sur la machine concernée.
- ✅ Vue principale (pads, VU-mètre, panneau) masquée tant qu'un panneau plein écran la recouvre (plus de redessins cachés) ; VU-mètre ne redessine que si ses valeurs changent ; corps défilant de l'éditeur de pad mis en cache image (défilement = copie d'image).
- ❌ Pas d'OpenGL (déconseillé dans un plug-in : conflits avec la fenêtre de l'hôte, abandonné sur macOS).

## Lot 7 — Loop, coupure, chromatique, limiteur, annuler/rétablir, validation

| Fonction | État | Détail |
|---|---|---|
| Mode Loop | Fait, testé (moteur + test de fumée) | 4e mode du pad. Lecture depuis le début de la région puis bouclage à partir du « point de boucle » (% de la région) avec fondu enchaîné linéaire réglable (défaut 10 ms). MIDI : tenu = boucle, Note Off = release. Souris : un clic lance, le clic suivant arrête. Marqueur « LOOP » sur la forme d'onde. Paramètres `padNN_loopstart`, `padNN_loopfade`. |
| Groupes de coupure | Fait, testé | `padNN_choke` 0 (aucun) à 8. Un coup sur un pad coupe (fondu 4 ms) les autres pads du même groupe. |
| Mode chromatique | Fait, testé | `padNN_chromatic`. Le pad joue sur toutes les notes MIDI non utilisées par un autre pad **chargé**, transposé de (note jouée − note du pad). Gate ou Loop conseillés. Un clic souris joue la hauteur d'origine. |
| Volume maître + limiteur | Fait, testé | `global_master` (−60…+12 dB), `global_limiter`, `global_ceiling` (−12…0 dB, défaut −0,3). Limiteur à anticipation 1,5 ms (plafond jamais dépassé), latence constante déclarée à l'hôte. Voyant LIM sur le VU-mètre. Carte « SORTIE » dans Options. |
| Annuler / rétablir | Fait, testé | Ctrl+Z, Ctrl+Y, Ctrl+Maj+Z (Cmd sur Mac) + boutons Annuler/Rétablir. Couvre : tout réglage fait dans l'interface (un geste = une étape), renommage, couleur, chargement, vidage, tempo. Les modifications venant de l'hôte (automation) ne sont pas enregistrées. Étapes rapprochées de moins de 350 ms = une seule étape. Historique vidé à la restauration d'un projet. Découpage en tranches : garde son propre « Annuler ». |
| Validation | pluginval niveau 10 : SUCCÈS sous Linux (VST3, avec tests GUI sous xvfb) | `Tests/validate_pluginval.ps1` pour Windows. **Reste à faire par l'utilisateur** : pluginval sous Windows et tests dans plusieurs STAN (Reaper, Ableton Live, FL Studio, Cubase…). |

Limites connues : l'historique conserve les samples remplacés en mémoire (80 étapes max) ; les modes Slice/Mutate gardent aussi leur annulation propre.

## Lot 8 — Pads « gomme » noirs
Pads redessinés façon boîte à rythme matérielle : caoutchouc noir mat à grain fin, biseau éclairé par le haut, ombre de contact, enfoncement au clic. La couleur du pad est conservée sous forme d'une petite bande lumineuse en bas ; à la frappe le pad s'illumine « par en dessous » (le grain reste visible). Le reste de l'interface est inchangé. Fichier : Source/UI/PadComponent.cpp (couches mises en cache, reconstruites au redimensionnement).

## Lot 9 — Style « matériel » du VU-mètre et du panneau de pad
- **VU-mètre** façon console de mixage : panneau métal brossé à vis, deux colonnes de LED segmentées (1,5 dB par segment : vert, jaune, orange, rouge), échelle dB au centre, segment de crête + maintien (rouge à 0 dBFS), voyants ronds CLIP et LIM, afficheur ambre PIC / RMS / Δ. Clic = réinitialise maintien et CLIP.
- **Panneau du pad sélectionné** : métal brossé, potards rotatifs à capuchon coloré (Volume, Pitch, Pan, Vélocité, Note MIDI) avec graduations qui s'allument, valeurs sur afficheurs LCD ambre (clic sur la valeur = saisie au clavier ; double-clic = valeur par défaut ; molette possible), sélecteur de mode LCD, boutons à biseau avec voyant. Fichiers : UI/HardwareStyle.h (nouveau, en-têtes seulement), UI/MasterMeter.cpp, UI/PadControls.cpp, Plugin/PluginEditor.cpp. Les réglages de l'éditeur complet d'un pad gardent leurs curseurs.

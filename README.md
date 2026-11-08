# Daft Mug — sampler 16 pads (C++20 / JUCE 8)

**VST3** + **application autonome** (Windows d'abord, macOS ensuite). Fenêtre Liquid Glass sombre, 16 pads (4×4) avec rouage ouvrant l'éditeur propre au pad.
Le suivi détaillé, fonction par fonction (fait / testé / non testé), est dans `SUIVI_CAHIER_DES_CHARGES.md`.

## Fonctions disponibles

- **Pads / MIDI** : 16 pads, notes configurables, MIDI Learn, modes One-shot / Gate / Retrigger, polyphonie et vol de voix, courbe de vélocité, glisser-déposer d'un fichier sur un pad.
- **Samples** : import WAV / AIFF / FLAC / MP3, forme d'onde, région non destructive, fondus, reverse, enveloppe ADSR, export WAV d'un pad.
- **Son** : EQ 4 bandes (+ dynamique par bande, écoute isolée), saturation, bitcrusher, chorus, delay, réverbération à convolution (vos propres réponses impulsionnelles dans `IR/`, voir `IR/LISEZMOI.txt`).
- **Outils** : Slice to Pads, Mutate (variantes de réglages), presets de pad et de kit, collecte portable des samples, export WAV de tous les pads.
- **Daft Mug AI (facultatif)** : conseiller Gemini avec **votre propre clé API** (Options › IA et confidentialité). Désactivé par défaut, jamais dans le chemin audio, propositions validées par liste blanche et jamais appliquées sans votre action.
- Tous les réglages sonores sont des paramètres automatisables (`AudioProcessorValueTreeState`).

## Architecture

| Couche | Dossier | Rôle | Dépend de JUCE ? |
|---|---|---|---|
| Moteur audio | `Source/Engine` | Voix, polyphonie, pitch, pan, gain, modes One-shot / Gate / Retrigger, vol de voix, région non destructive, files sans verrou | **Non** (C++17 pur, testable partout) |
| Plug-in | `Source/Plugin` | Paramètres automatisables (`AudioProcessorValueTreeState`, 8 par pad), MIDI, chargement asynchrone, sauvegarde/restauration d'état, MIDI Learn | Oui |
| Interface | `Source/UI` + `PluginEditor` | LookAndFeel Liquid Glass, pads, rouage, éditeur de pad, forme d'onde | Oui |
| Gemini | `Source/AI` | Catalogue en liste blanche, validation stricte, client HTTPS, clé dans le coffre de l'OS (Windows : gestionnaire d'identifiants), hors chemin audio | Oui |

Règles appliquées dès maintenant :
- Le thread audio n'alloue pas, ne verrouille pas, ne touche ni disque ni réseau. Les commandes UI → audio passent par une file SPSC sans attente ; les samples remplacés repartent vers l'UI par une file de « mise au rebut » (`collectGarbage`), donc jamais libérés côté audio.
- Chaque pad possède ses propres paramètres (`pad01_gain` … `pad16_end`). **Ces identifiants sont permanents** (automation hôte) : ne jamais les renommer.
- Les samples ne sont jamais modifiés : début/fin sont de simples paramètres de lecture.
- L'état sauvegardé (paramètres + nom/couleur/chemin de fichier des pads) porte un numéro de schéma (`kStateSchemaVersion`) avec point de migration dans `applyStateTree`. Il ne contiendra jamais de secret.

## Arborescence

```
CMakeLists.txt
Source/Engine/    moteur C++17 sans JUCE : voix, EQ, effets, convolution, détection de tranches, Mutate
Source/Plugin/    processeur, paramètres, état, presets, collecte, exports
Source/AI/        Daft Mug AI : catalogue/validation, service Gemini, stockage de clé
Source/UI/        LookAndFeel Liquid Glass et tous les panneaux (pad, EQ, FX, reverb, Slice, Mutate, IA, conditions)
Source/Resources/ logo et icône (embarqués dans le binaire)
Tests/            EngineTests.cpp (moteur, sans JUCE)   PluginSmokeTest.cpp (processeur + éditeur réels, faux serveur Gemini)
IR/               dossier (vide) pour vos réponses impulsionnelles
```

## Compiler sous Windows

Prérequis : Visual Studio 2022 (charge de travail « Développement Desktop en C++ ») ou ses Build Tools, CMake ≥ 3.22, Git.
Au premier `cmake`, JUCE 8.0.16 est téléchargé automatiquement (accès internet requis). Pour utiliser un JUCE local : `-DDAFTMUG_JUCE_DIR=C:/chemin/JUCE`.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target DaftMug_VST3
cmake --build build --config Release --target DaftMug_Standalone
ctest --test-dir build -C Release --output-on-failure
```

Résultats :
- VST3 : `build\DaftMug_artefacts\Release\VST3\Daft Mug.vst3` → à copier dans `C:\Program Files\Common Files\VST3\` (ou `-DDAFTMUG_COPY_AFTER_BUILD=ON`, droits administrateur requis).
- Autonome : `build\DaftMug_artefacts\Release\Standalone\Daft Mug.exe` (réglages audio/MIDI via le bouton d'options de la fenêtre autonome).

Pour essayer : prenez un WAV/AIFF/FLAC de votre choix, glissez-le sur un pad (ou cliquez un pad vide). Notes MIDI par défaut : pad 1 = C1 (36) … pad 16 = D#2 (51), modifiables par pad.

## Ce qui a réellement été vérifié (Linux, GCC 13, JUCE 8.0.16)

- Le test de fumée du plug-in a aussi été exécuté sous AddressSanitizer + UBSan (réussi après correction de deux bugs mémoire trouvés ainsi).
- Tests moteur (`daftmug_engine_tests`, aussi sous AddressSanitizer + UBSan) : voix, polyphonie, EQ, effets, convolution, EQ dynamique, solo de bande, tranches, Mutate. Tous passent.
- Test de fumée du plug-in réel (`DaftMugSmokeTest`, serveur X virtuel, génère des captures PNG) : chargement et état, presets / kits, collecte portable (collisions, déplacement du dossier), export du kit, Slice, Mutate, glisser-déposer, assistant IA contre un **faux serveur Gemini** (aucun appel réseau réel), absence de clé dans l'état / presets / réglages. Tous passent.
- Compilation de nos fichiers sans avertissement avec GCC.

**Import / export audio** : import WAV, AIFF, FLAC, MP3 ; export WAV par pad (16 / 24 / 32 bits flottant, fréquence d'origine / 44,1 / 48 / 96 kHz, normalisation optionnelle) et export de tous les pads (menu Kits ▾, 24 bits, fichiers jamais écrasés). Rendu hors-ligne avec le même moteur que la lecture.

## Ce qui n'a PAS été vérifié — à faire de votre côté

- Compilation **MSVC / Windows** (seulement GCC sous Linux) : des erreurs propres à MSVC restent possibles ; envoyez-les-moi telles quelles. Le code de stockage de clé via le Gestionnaire d'identifiants Windows n'a jamais été compilé.
- **API Gemini réelle** : aucun accès réseau dans mon environnement ; le format de requête/réponse et les noms de modèles sont à valider avec votre clé (« Authentifier la clé API »).
- La cible **Standalone** (pas d'ALSA chez moi), le chargement du VST3 dans un **hôte réel**, `pluginval`, la latence et le son sur carte audio, les **écoutes** (EQ, effets, convolution avec de vraies IR).
- Clic réel à la souris, glisser-déposer depuis l'Explorateur, fenêtres de choix de fichier natives, MIDI matériel.

## Limites connues

Pas encore : installeur, macOS (stockage de clé dans le Trousseau à écrire), classification automatique des samples par l'IA, textes légaux définitifs (la page « Conditions d'utilisation » est un projet à compléter et à faire valider). Sortie stéréo uniquement. Le rééchantillonnage est une interpolation linéaire (à améliorer). La copie de la collecte portable bloque l'interface le temps de la copie. Les réverbérations de bibliothèque (Plate, Hall…) ne sont pas copiées par la collecte.

## Risques techniques principaux

1. **Convolution + effets par pad ×16** : coût CPU et latence ; prévoir un budget CPU et un partage/limite de moteurs dès le jalon FX.
2. **Qualité de transposition** (interpolation linéaire → repliement) : passer à un interpolateur d'ordre supérieur / sur-échantillonnage.
3. **Hôtes différents** : fenêtre redimensionnable, boîtes de dialogue natives et glisser-déposer se comportent différemment selon l'hôte ; à tester sur les hôtes ciblés.
4. **Compatibilité des projets** : identifiants de paramètres figés + migration d'état à chaque évolution.
5. **Licences** : voir ci-dessous.

## Licences (à valider par vos soins)

- **JUCE 8** : AGPLv3 *ou* licence commerciale JUCE. Distribuer Daft Mug en source fermée exige une licence commerciale JUCE ; sous AGPLv3 le code de Daft Mug doit lui aussi être publié sous AGPLv3. Choisissez avant toute distribution ; ceci n'est pas un avis juridique.
- SDK VST3 : fourni avec JUCE sous licence MIT. FLAC/Ogg embarqués : BSD.
- Usage **strictement personnel** (non distribué) : pas de licence commerciale JUCE nécessaire. Si un jour vous partagez ou vendez un binaire, revoyez ce point.
- **MP3 activé** (`JUCE_USE_MP3AUDIOFORMAT=1`) : import `.mp3` testé avec un vrai fichier.
- **ASIO** (application autonome uniquement) : ajoutez `-DDAFTMUG_ASIO_SDK_DIR=C:/chemin/ASIOSDK` (dossier contenant `common/asio.h`, SDK à télécharger chez Steinberg). Inutile pour le VST3 dans votre DAW, qui gère déjà l'audio.
- **AAX** (Pro Tools uniquement) : ajoutez `-DDAFTMUG_AAX_SDK_DIR=C:/chemin/AAX_SDK` (SDK Avid, compte développeur requis). Pro Tools n'ouvre que des plug-ins AAX **signés** (outils PACE/Avid) : sans cela le fichier compile mais ne se charge pas. Inutile si votre DAW n'est pas Pro Tools (le VST3 suffit).
- Aucune clé API dans le code, les presets ou l'état du projet.

## Nouveautés (lot 7)
- **Mode Loop** (boucle + point de boucle + fondu), **groupes de coupure** (1 à 8), **mode chromatique** : réglages dans l'éditeur de pad, carte « Tonalité, niveau et MIDI ».
- **Volume maître et limiteur de sortie** : Options > SORTIE (voyant LIM sur le VU-mètre). Latence du limiteur : 1,5 ms, constante.
- **Annuler / rétablir** : Ctrl+Z / Ctrl+Y (Cmd sur Mac) et boutons dans l'en-tête.
- **Validation** : `Tests/validate_pluginval.ps1` (pluginval).

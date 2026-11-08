; Inno Setup 6 script for Daft Mug (Windows 64 bits).
; Theme sombre : demande Inno Setup 6.5 ou plus recent (sinon remplacer "modern dark" par "modern"). A tester sur un PC vierge.
;
; 1) Build:    cmake --build build --config Release --target DaftMug_VST3 DaftMug_Standalone
; 2) Compile:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\DaftMug.iss
;    -> build\installer\DaftMug-Setup-0.1.0.exe
#define AppName "Daft Mug"
#define AppVersion "0.1.0"
#define AppPublisher "Oxy Gen Prod."
#define BuildDir "..\build\DaftMug_artefacts\Release"

[Setup]
AppId={{B6D3F6D4-5F6B-4B54-9F0B-DA17D4D06001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=..\build\installer
OutputBaseFilename=DaftMug-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern dark
; Visuels (dossier installer\Assets) : icône, images dark à gauche / en haut à droite (1x et 2x pour les écrans haute résolution)
SetupIconFile=Assets\daftmug.ico
WizardImageFile=Assets\wizard_big.bmp,Assets\wizard_big_2x.bmp
WizardSmallImageFile=Assets\wizard_small.bmp,Assets\wizard_small_2x.bmp
; Page « Conditions d'utilisation » avec case « J'accepte » obligatoire
LicenseFile=Assets\conditions.txt
UninstallDisplayIcon={app}\Daft Mug.exe
UninstallDisplayName={#AppName}
DisableProgramGroupPage=yes

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

; The user chooses: VST3 only, standalone only, or both (or a custom mix).
[Types]
Name: "full"; Description: "Installation complète (VST3 + application autonome)"
Name: "vst3only"; Description: "Plug-in VST3 uniquement"
Name: "standaloneonly"; Description: "Application autonome uniquement"
Name: "custom"; Description: "Personnalisée"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plug-in VST3 (pour votre logiciel de musique)"; Types: full vst3only custom
Name: "standalone"; Description: "Application autonome (Daft Mug.exe)"; Types: full standaloneonly custom

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le Bureau"; Components: standalone; Flags: unchecked

[Files]
; VST3 bundle goes to the shared VST3 folder that every host scans
Source: "{#BuildDir}\VST3\Daft Mug.vst3\*"; DestDir: "{commoncf64}\VST3\Daft Mug.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\Daft Mug.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\Daft Mug"; Filename: "{app}\Daft Mug.exe"; Components: standalone
Name: "{group}\Désinstaller Daft Mug"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Daft Mug"; Filename: "{app}\Daft Mug.exe"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{app}\Daft Mug.exe"; Description: "Lancer Daft Mug"; Components: standalone; Flags: nowait postinstall skipifsilent

[Messages]
french.FinishedLabel=Daft Mug est installé. Pour la réverbération, utilisez le bouton « Importer des IR… » dans l'éditeur d'un pad (vos fichiers sont copiés dans %APPDATA%\DaftMug\IR). La clé Gemini (facultative) se saisit dans Options > IA et confidentialité ; elle n'est jamais incluse dans l'installeur. Si vous avez installé le plug-in VST3, relancez le scan des plug-ins de votre logiciel de musique.

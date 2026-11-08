# Validation du plug-in VST3 avec pluginval (https://github.com/Tracktion/pluginval/releases)
# Usage (PowerShell) : .\Tests\validate_pluginval.ps1 -Pluginval "C:\Outils\pluginval.exe"
param(
    [string]$Pluginval = "pluginval.exe",
    [string]$Vst3 = "C:\DaftMug\DaftMug\build\DaftMug_artefacts\Release\VST3\Daft Mug.vst3",
    [int]$Strictness = 10
)
& $Pluginval --strictness-level $Strictness --validate-in-process --output-dir "$PSScriptRoot\pluginval-logs" $Vst3
if ($LASTEXITCODE -eq 0) { Write-Host "pluginval : SUCCES" -ForegroundColor Green } else { Write-Host "pluginval : ECHEC (voir pluginval-logs)" -ForegroundColor Red }
exit $LASTEXITCODE

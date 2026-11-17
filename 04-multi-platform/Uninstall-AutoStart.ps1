$ErrorActionPreference = 'Stop'
$link = Join-Path ([Environment]::GetFolderPath('Startup')) 'Delta Resolution Watcher.lnk'
if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link }
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'watcher-stop.flag') -Value 'stop'
Write-Host 'Autostart removed; watcher has been asked to stop.'
Write-Host 'If needed, run Restore-Original.bat to restore the resolution.'

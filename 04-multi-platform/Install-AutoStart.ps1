$ErrorActionPreference = 'Stop'
$startup = [Environment]::GetFolderPath('Startup')
$link = Join-Path $startup 'Delta Resolution Watcher.lnk'
$vbs = Join-Path $PSScriptRoot 'Watch-Delta.vbs'
Remove-Item -LiteralPath (Join-Path $PSScriptRoot 'watcher-stop.flag') -ErrorAction SilentlyContinue
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($link)
$shortcut.TargetPath = (Join-Path $env:SystemRoot 'System32\wscript.exe')
$shortcut.Arguments = '"' + $vbs + '"'
$shortcut.WorkingDirectory = $PSScriptRoot
$shortcut.Description = 'Watch Delta Force and change display resolution'
$shortcut.Save()
Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\wscript.exe') -ArgumentList ('"' + $vbs + '"')
Write-Host 'Installed for this Windows user and started.'
Write-Host 'Keep this folder in its current location.'

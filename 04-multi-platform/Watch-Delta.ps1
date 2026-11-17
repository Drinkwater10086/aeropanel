$ErrorActionPreference = 'Stop'
$mutex = New-Object System.Threading.Mutex($false, 'Local\DeltaResolutionWatcher')
if (-not $mutex.WaitOne(0, $false)) { exit }
try {
    . (Join-Path $PSScriptRoot 'DeltaResolution.ps1') -Library
    $savedPath = Join-Path $PSScriptRoot 'original-mode.xml'
    $active = $false
    $gameSeen = $false
    $launcherConsumed = $false
    while ($true) {
        if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'watcher-stop.flag')) { break }
        $running = [bool](Get-Process -Name 'DeltaForceClient-Win64-Shipping' -ErrorAction SilentlyContinue)
        $launcher = [bool](Get-Process -Name 'df_launcher', 'delta_force_launcher' -ErrorAction SilentlyContinue)
        if (-not $launcher) { $launcherConsumed = $false }
        if ($active -and $running) { $gameSeen = $true }
        if (-not $active -and ($running -or ($launcher -and -not $launcherConsumed))) {
            $savedByUs = $false
            try {
                $current = [ScreenMode]::Current()
                if (Test-Path -LiteralPath $savedPath) { throw 'A saved mode exists; run Restore-Original.bat first.' }
                $back = $current
                $target = Get-FourThreeMode $current
                $back | Export-Clixml -LiteralPath $savedPath
                $savedByUs = $true
                [ScreenMode]::Apply($target)
                $active = $true
                $gameSeen = $running
            } catch {
                if ($savedByUs) { try { [ScreenMode]::Apply($back); Remove-Item -LiteralPath $savedPath -ErrorAction SilentlyContinue } catch {} }
                (Get-Date -Format s) + ' Switch failed: ' + $_.Exception.Message | Add-Content -LiteralPath (Join-Path $PSScriptRoot 'watcher-error.log')
                Start-Sleep -Seconds 30
            }
        } elseif ($active -and -not $running -and ($gameSeen -or -not $launcher)) {
            try {
                [ScreenMode]::Apply($back)
                Remove-Item -LiteralPath $savedPath -ErrorAction SilentlyContinue
                $active = $false
                $gameSeen = $false
                if ($launcher) { $launcherConsumed = $true }
            } catch {
                (Get-Date -Format s) + ' Restore failed: ' + $_.Exception.Message | Add-Content -LiteralPath (Join-Path $PSScriptRoot 'watcher-error.log')
                Start-Sleep -Seconds 30
            }
        }
        Start-Sleep -Seconds 2
    }
} finally {
    if ($active) { try { [ScreenMode]::Apply($back); Remove-Item -LiteralPath $savedPath -ErrorAction SilentlyContinue } catch {} }
    Remove-Item -LiteralPath (Join-Path $PSScriptRoot 'watcher-stop.flag') -ErrorAction SilentlyContinue
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}

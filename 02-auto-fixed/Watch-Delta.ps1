$ErrorActionPreference = 'Stop'
$mutex = New-Object System.Threading.Mutex($false, 'Local\DeltaResolutionWatcher')
if (-not $mutex.WaitOne(0, $false)) { exit }
try {
    $source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'DeltaResolution.ps1') -Raw
    $end = $source.IndexOf("`ntry {")
    if ($end -lt 0) { throw 'Cannot load display API.' }
    Invoke-Expression $source.Substring(0, $end)
    $active = $false
    $gameSeen = $false
    $launcherConsumed = $false
    while ($true) {
        if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'watcher-stop.flag')) { break }
        $running = [bool](Get-Process -Name 'DeltaForceClient-Win64-Shipping' -ErrorAction SilentlyContinue)
        $launcher = [bool](Get-Process -Name 'df_launcher' -ErrorAction SilentlyContinue)
        if (-not $launcher) { $launcherConsumed = $false }
        if ($active -and $running) { $gameSeen = $true }
        if (-not $active -and ($running -or ($launcher -and -not $launcherConsumed))) {
            try {
                $current = [ScreenMode]::Current()
                $back = [ScreenMode]::Find(3840, 2160, $current.dmDisplayFrequency, $current.dmBitsPerPel)
                $target = [ScreenMode]::Find(2880, 2160, $current.dmDisplayFrequency, $current.dmBitsPerPel)
                [ScreenMode]::Apply($target)
                $active = $true
                $gameSeen = $running
            } catch {
                (Get-Date -Format s) + ' Switch failed: ' + $_.Exception.Message | Add-Content -LiteralPath (Join-Path $PSScriptRoot 'watcher-error.log')
                Start-Sleep -Seconds 30
            }
        } elseif ($active -and -not $running -and ($gameSeen -or -not $launcher)) {
            try {
                [ScreenMode]::Apply($back)
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
    if ($active) { try { [ScreenMode]::Apply($back) } catch {} }
    Remove-Item -LiteralPath (Join-Path $PSScriptRoot 'watcher-stop.flag') -ErrorAction SilentlyContinue
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$log = Join-Path $root 'error.log'
$modePath = Join-Path $root 'original-mode.xml'
$configPath = Join-Path $root 'settings.json'
$stopPath = Join-Path $root 'watcher-stop.flag'
$mutex = $null
$owned = $false
$active = $false
function Write-ErrorLog($stage, $record) {
    try {
        $detail = ($record | Format-List * -Force | Out-String).Trim()
        $message = "`r`n[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] 阶段: $stage`r`n$detail`r`n脚本: $root`r`n"
        Add-Content -LiteralPath $log -Value $message -Encoding UTF8
    } catch { }
}
try {
    $mutex = New-Object System.Threading.Mutex($false, 'Local\DeltaResolutionWatcher')
    try { $owned = $mutex.WaitOne(0, $false) } catch [System.Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { return }
    . (Join-Path $root 'Display.ps1') -Library
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $menu = New-Object Windows.Forms.ContextMenuStrip
    $open = $menu.Items.Add('打开设置')
    $quit = $menu.Items.Add('退出监测')
    $tray = New-Object Windows.Forms.NotifyIcon
    $tray.Icon = [Drawing.SystemIcons]::Application
    $tray.Text = '三角洲分辨率监测'
    $tray.ContextMenuStrip = $menu
    $tray.Visible = $true
    $open.Add_Click({ try { Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -STA -File "' + (Join-Path $root 'Settings.ps1') + '"') -WindowStyle Hidden } catch { Write-ErrorLog '打开设置' $_ } })
    $tray.Add_DoubleClick({ $open.PerformClick() })
    $quit.Add_Click({ try { Set-Content -LiteralPath $stopPath -Value 'stop' -Encoding ASCII } catch { Write-ErrorLog '请求退出监测' $_ } })
    $gameSeen = $false
    $launcherConsumed = $false
    $configStamp = ''
    $settings = $null
    $retryAt = [DateTime]::MinValue
    $nextPoll = [DateTime]::MinValue
    while (-not (Test-Path -LiteralPath $stopPath)) {
        [Windows.Forms.Application]::DoEvents()
        if ([DateTime]::Now -lt $nextPoll) { Start-Sleep -Milliseconds 250; continue }
        $nextPoll = [DateTime]::Now.AddSeconds(2)
        try {
            if (-not $active) {
                $stamp = (Get-Item -LiteralPath $configPath).LastWriteTimeUtc.Ticks
                if ($stamp -ne $configStamp) {
                    $candidate = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
                    if ($candidate.platform -notin @('Steam','Official','WeGame')) { throw '平台配置无效。' }
                    if ($candidate.mode -notin @('auto','custom')) { throw '分辨率模式无效。' }
                    if ($candidate.mode -eq 'custom' -and ([int]$candidate.width -lt 320 -or [int]$candidate.height -lt 200)) { throw '自定义分辨率无效。' }
                    $settings = $candidate
                    $configStamp = $stamp
                }
            }
            $running = [bool](Get-Process -Name 'DeltaForceClient-Win64-Shipping' -ErrorAction SilentlyContinue)
            $launcher = $false
            if ($settings.platform -eq 'Steam') { $launcher = [bool](Get-Process -Name 'df_launcher' -ErrorAction SilentlyContinue) }
            if ($settings.platform -eq 'Official') { $launcher = [bool](Get-Process -Name 'delta_force_launcher','df_launcher' -ErrorAction SilentlyContinue) }
            if (-not $launcher) { $launcherConsumed = $false }
            if ($active -and $running) { $gameSeen = $true }
            if (-not $active -and ($running -or ($launcher -and -not $launcherConsumed)) -and [DateTime]::Now -ge $retryAt) {
                $stage = '切换前检查'
                $savedByUs = $false
                try {
                    if (Test-Path -LiteralPath $modePath) { throw '发现未恢复的原始显示模式。请先使用设置界面中的“恢复原分辨率”。' }
                    $back = [ScreenMode]::Current()
                    $stage = "验证目标分辨率；当前 $($back.dmPelsWidth)x$($back.dmPelsHeight) @ $($back.dmDisplayFrequency)Hz；平台 $($settings.platform)"
                    if ($settings.mode -eq 'auto') { $target = Get-FourThreeMode $back }
                    else { $target = [ScreenMode]::Find([int]$settings.width,[int]$settings.height,$back.dmDisplayFrequency,$back.dmBitsPerPel) }
                    $stage = "保存原模式并切换至 $($target.dmPelsWidth)x$($target.dmPelsHeight) @ $($target.dmDisplayFrequency)Hz"
                    $back | Export-Clixml -LiteralPath $modePath
                    $savedByUs = $true
                    [ScreenMode]::Apply($target)
                    $active = $true
                    $gameSeen = $running
                } catch {
                    Write-ErrorLog $stage $_
                    if ($savedByUs) { try { [ScreenMode]::Apply($back); Remove-Item -LiteralPath $modePath } catch { Write-ErrorLog '切换失败后的恢复' $_ } }
                    $retryAt = [DateTime]::Now.AddSeconds(30)
                }
            } elseif ($active -and -not $running -and ($gameSeen -or -not $launcher) -and [DateTime]::Now -ge $retryAt) {
                try {
                    [ScreenMode]::Apply($back)
                    Remove-Item -LiteralPath $modePath
                    $active = $false
                    $gameSeen = $false
                    if ($launcher) { $launcherConsumed = $true }
                } catch { Write-ErrorLog '恢复原显示模式' $_; $retryAt = [DateTime]::Now.AddSeconds(30) }
            }
        } catch { if ([DateTime]::Now -ge $retryAt) { Write-ErrorLog '读取配置或检测进程' $_; $retryAt = [DateTime]::Now.AddSeconds(30) } }
    }
} catch { Write-ErrorLog '后台监测启动' $_ }
finally {
    if ($active) { try { [ScreenMode]::Apply($back); Remove-Item -LiteralPath $modePath } catch { Write-ErrorLog '停止监测时恢复' $_ } }
    if ($tray) { $tray.Visible = $false; $tray.Dispose() }
    if ($menu) { $menu.Dispose() }
    if ($owned) { Remove-Item -LiteralPath $stopPath -ErrorAction SilentlyContinue; $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$log = Join-Path $root 'error.log'
$modePath = Join-Path $root 'original-mode.xml'
$configPath = Join-Path $root 'settings.json'
$stopPath = Join-Path $root 'watcher-stop.flag'
$mutex = $null
$owned = $false
$script:active = $false
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
    $script:gameSeen = $false
    $script:launcherConsumed = $false
    $script:configStamp = ''
    $script:settings = $null
    $script:retryAt = [DateTime]::MinValue
    $displayLoaded = $false
    $timer = New-Object Windows.Forms.Timer
    $timer.Interval = 2000
    $timer.Add_Tick({
        if (Test-Path -LiteralPath $stopPath) { [Windows.Forms.Application]::ExitThread(); return }
        try {
            if (-not $script:active) {
                $stamp = (Get-Item -LiteralPath $configPath).LastWriteTimeUtc.Ticks
                if ($stamp -ne $script:configStamp) {
                    $candidate = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
                    if ($candidate.platform -notin @('Steam','Official','WeGame')) { throw '平台配置无效。' }
                    if ($candidate.mode -notin @('auto','custom')) { throw '分辨率模式无效。' }
                    if ($candidate.mode -eq 'custom' -and ([int]$candidate.width -lt 320 -or [int]$candidate.height -lt 200)) { throw '自定义分辨率无效。' }
                    $script:settings = $candidate
                    $script:configStamp = $stamp
                }
            }
            $running = [bool](Get-Process -Name 'DeltaForceClient-Win64-Shipping' -ErrorAction SilentlyContinue)
            $launcher = $false
            if ($script:settings.platform -eq 'Steam') { $launcher = [bool](Get-Process -Name 'df_launcher' -ErrorAction SilentlyContinue) }
            if ($script:settings.platform -eq 'Official') { $launcher = [bool](Get-Process -Name 'delta_force_launcher','df_launcher' -ErrorAction SilentlyContinue) }
            if (-not $launcher) { $script:launcherConsumed = $false }
            if ($script:active -and $running) { $script:gameSeen = $true }
            if (-not $script:active -and ($running -or ($launcher -and -not $script:launcherConsumed)) -and [DateTime]::Now -ge $script:retryAt) {
                $stage = '切换前检查'
                $savedByUs = $false
                try {
                    if (-not $script:displayLoaded) { . (Join-Path $root 'Display.ps1') -Library; $script:displayLoaded = $true }
                    if (Test-Path -LiteralPath $modePath) { throw '发现未恢复的原始显示模式。请先使用设置界面中的“恢复原分辨率”。' }
                    $script:back = [ScreenMode]::Current()
                    $stage = "验证目标分辨率；当前 $($script:back.dmPelsWidth)x$($script:back.dmPelsHeight) @ $($script:back.dmDisplayFrequency)Hz；平台 $($script:settings.platform)"
                    if ($script:settings.mode -eq 'auto') {
                        if ($script:back.dmPelsHeight % 3 -ne 0) { throw '屏幕高度不能计算出整数宽度的 4:3 分辨率。' }
                        $target = [ScreenMode]::Find([int]($script:back.dmPelsHeight / 3 * 4),$script:back.dmPelsHeight,$script:back.dmDisplayFrequency,$script:back.dmBitsPerPel)
                    }
                    else { $target = [ScreenMode]::Find([int]$script:settings.width,[int]$script:settings.height,$script:back.dmDisplayFrequency,$script:back.dmBitsPerPel) }
                    $stage = "保存原模式并切换至 $($target.dmPelsWidth)x$($target.dmPelsHeight) @ $($target.dmDisplayFrequency)Hz"
                    $script:back | Export-Clixml -LiteralPath $modePath
                    $savedByUs = $true
                    [ScreenMode]::Apply($target)
                    $script:active = $true
                    $script:gameSeen = $running
                } catch {
                    Write-ErrorLog $stage $_
                    if ($savedByUs) { try { [ScreenMode]::Apply($script:back); Remove-Item -LiteralPath $modePath } catch { Write-ErrorLog '切换失败后的恢复' $_ } }
                    $script:retryAt = [DateTime]::Now.AddSeconds(30)
                }
            } elseif ($script:active -and -not $running -and ($script:gameSeen -or -not $launcher) -and [DateTime]::Now -ge $script:retryAt) {
                try {
                    [ScreenMode]::Apply($script:back)
                    Remove-Item -LiteralPath $modePath
                    $script:active = $false
                    $script:gameSeen = $false
                    if ($launcher) { $script:launcherConsumed = $true }
                } catch { Write-ErrorLog '恢复原显示模式' $_; $script:retryAt = [DateTime]::Now.AddSeconds(30) }
            }
        } catch { if ([DateTime]::Now -ge $script:retryAt) { Write-ErrorLog '读取配置或检测进程' $_; $script:retryAt = [DateTime]::Now.AddSeconds(30) } }
    })
    $timer.Start()
    [Windows.Forms.Application]::Run()
} catch { Write-ErrorLog '后台监测启动' $_ }
finally {
    if ($script:active) { try { [ScreenMode]::Apply($script:back); Remove-Item -LiteralPath $modePath } catch { Write-ErrorLog '停止监测时恢复' $_ } }
    if ($timer) { $timer.Stop(); $timer.Dispose() }
    if ($tray) { $tray.Visible = $false; $tray.Dispose() }
    if ($menu) { $menu.Dispose() }
    if ($owned) { Remove-Item -LiteralPath $stopPath -ErrorAction SilentlyContinue; $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}

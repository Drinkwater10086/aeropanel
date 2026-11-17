param([ValidateSet('switch','restore')][string]$Action)
$ErrorActionPreference='Stop'
$saved=Join-Path $PSScriptRoot 'original-mode.xml'
$stage=$Action
try {
    . (Join-Path $PSScriptRoot 'Display.ps1') -Library
    if($Action -eq 'restore') {
        if (-not (Test-Path -LiteralPath $saved)) { throw '没有可恢复的显示模式记录。' }
        [ScreenMode]::Apply((Read-SavedMode $saved))
        Remove-Item -LiteralPath $saved
    } else {
        if(Test-Path -LiteralPath $saved){throw '存在未恢复的显示模式，请在设置窗口手动恢复。'}
        $config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'settings.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $mode=[ScreenMode]::Current()
        $stage="验证目标；原模式 $($mode.dmPelsWidth)x$($mode.dmPelsHeight) @ $($mode.dmDisplayFrequency) Hz"
        if($config.mode -eq 'auto'){$target=Get-FourThreeMode $mode}
        elseif($config.mode -eq 'custom'){$target=[ScreenMode]::Find([int]$config.width,[int]$config.height,$mode.dmDisplayFrequency,$mode.dmBitsPerPel)}
        else{throw '分辨率模式无效。'}
        $stage="切换至 $($target.dmPelsWidth)x$($target.dmPelsHeight) @ $($target.dmDisplayFrequency) Hz"
        $mode | Export-Clixml -LiteralPath $saved
        try {[ScreenMode]::Apply($target)} catch {try {[ScreenMode]::Apply($mode); Remove-Item -LiteralPath $saved} catch {}; throw}
    }
    exit 0
} catch {
    $detail=($_ | Format-List * -Force | Out-String).Trim()
    try {Add-Content -LiteralPath (Join-Path $PSScriptRoot 'error.log') -Encoding UTF8 -Value "`r`n[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] 阶段: $stage`r`n$detail`r`n脚本: $PSScriptRoot`r`n"} catch {}
    exit 1
}

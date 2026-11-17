param([switch]$SelfTest)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'Display.ps1') -Library
[System.Windows.Forms.Application]::EnableVisualStyles()
$root = $PSScriptRoot
$configPath = Join-Path $root 'settings.json'
$stopPath = Join-Path $root 'watcher-stop.flag'
$modePath = Join-Path $root 'original-mode.xml'
$logPath = Join-Path $root 'error.log'
$linkPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'Delta Resolution Watcher.lnk'
function Alert($text, $title = '三角洲分辨率设置') { [void][Windows.Forms.MessageBox]::Show($text, $title) }
function Log-GuiError($stage, $record) {
    $detail = ($record | Format-List * -Force | Out-String).Trim()
    try { Add-Content -LiteralPath $logPath -Encoding UTF8 -Value "`r`n[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] 阶段: $stage`r`n$detail`r`n脚本: $root`r`n" } catch { }
    Alert "操作：$stage`r`n$($record.Exception.Message)`r`n`r`n详细信息请查看 error.log。"
}
function Running {
    try { $m = [Threading.Mutex]::OpenExisting('Local\DeltaResolutionWatcher'); $m.Dispose(); return $true }
    catch [Threading.WaitHandleCannotBeOpenedException] { return $false }
    catch [System.UnauthorizedAccessException] { return $true }
}
$form = New-Object Windows.Forms.Form
$form.Text = '三角洲分辨率设置'
$form.ClientSize = New-Object Drawing.Size(550, 425)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'; $form.MaximizeBox = $false
$form.Font = New-Object Drawing.Font('Microsoft YaHei UI', 9)
function Label($text, $x, $y, $w, $h = 25) { $c = New-Object Windows.Forms.Label; $c.Text=$text; $c.SetBounds($x,$y,$w,$h); $form.Controls.Add($c); return $c }
function Button($text, $x, $y, $w, $action) { $c=New-Object Windows.Forms.Button; $c.Text=$text; $c.SetBounds($x,$y,$w,32); $c.Add_Click($action); $form.Controls.Add($c); return $c }
$currentLabel = Label '当前主显示器：读取中' 20 18 510
try { $current = [ScreenMode]::Current(); $currentLabel.Text = "当前主显示器：$($current.dmPelsWidth) × $($current.dmPelsHeight)  $($current.dmDisplayFrequency) Hz" } catch { Log-GuiError '读取当前显示模式' $_ }
[void](Label '游戏平台：' 20 56 90)
$platform = New-Object Windows.Forms.ComboBox; $platform.SetBounds(120,52,180,28); $platform.DropDownStyle='DropDownList'
[void]$platform.Items.AddRange(@('Steam','官方启动器','WeGame')); $platform.SelectedIndex=0; $form.Controls.Add($platform)
$auto = New-Object Windows.Forms.RadioButton; $auto.Text='自动计算同高度 4:3'; $auto.SetBounds(20,96,235,26); $auto.Checked=$true; $form.Controls.Add($auto)
$custom = New-Object Windows.Forms.RadioButton; $custom.Text='自定义分辨率'; $custom.SetBounds(20,132,150,26); $form.Controls.Add($custom)
$width = New-Object Windows.Forms.NumericUpDown; $width.Minimum=320; $width.Maximum=16384; $width.Value=2880; $width.SetBounds(175,132,90,26); $form.Controls.Add($width)
[void](Label '×' 275 135 20)
$height = New-Object Windows.Forms.NumericUpDown; $height.Minimum=200; $height.Maximum=16384; $height.Value=2160; $height.SetBounds(300,132,90,26); $form.Controls.Add($height)
$status = Label '' 20 179 510 48
function Refresh-Status { $status.Text = "后台监测：$(if (Running) {'运行中或无法读取状态'} else {'未运行'})    登录自启：$(if (Test-Path -LiteralPath $linkPath) {'已启用'} else {'未启用'})`r`nWeGame 在游戏进程出现后切换；更改将在下次切换时应用。" }
function SelectedSettings {
    $keys=@('Steam','Official','WeGame')
    return @{platform=$keys[$platform.SelectedIndex]; mode=$(if($auto.Checked){'auto'}else{'custom'}); width=[int]$width.Value; height=[int]$height.Value}
}
function SaveSettings { $obj=SelectedSettings; $json=$obj | ConvertTo-Json; Set-Content -LiteralPath $configPath -Value $json -Encoding UTF8 }
function StartWatcher {
    SaveSettings
    Remove-Item -LiteralPath $stopPath -ErrorAction SilentlyContinue
    $vbs=Join-Path $root 'Watch-Delta.vbs'
    Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\wscript.exe') -ArgumentList ('"' + $vbs + '"') -WindowStyle Hidden
}
try {
    if (Test-Path -LiteralPath $configPath) {
        $s=Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $idx=[Array]::IndexOf(@('Steam','Official','WeGame'),[string]$s.platform); if($idx -lt 0){throw '平台配置无效。'}
        $platform.SelectedIndex=$idx
        if($s.mode -eq 'custom'){$custom.Checked=$true; $width.Value=[decimal]$s.width; $height.Value=[decimal]$s.height}
    }
} catch { Log-GuiError '读取已保存设置' $_ }
[void](Button '保存设置' 20 237 110 { try { SaveSettings; Alert '设置已保存。后台监测会在下次切换时使用新设置。' } catch { Log-GuiError '保存设置' $_ } })
[void](Button '检查分辨率' 145 237 110 { try { $c=[ScreenMode]::Current(); $s=SelectedSettings; if($s.mode -eq 'auto'){$t=Get-FourThreeMode $c}else{$t=[ScreenMode]::Find($s.width,$s.height,$c.dmDisplayFrequency,$c.dmBitsPerPel)}; Alert "驱动验证通过：$($t.dmPelsWidth) × $($t.dmPelsHeight)，$($t.dmDisplayFrequency) Hz。未实际切换。" } catch { Log-GuiError '检查目标分辨率' $_ } })
[void](Button '启动后台' 270 237 110 { try { if(Running){Alert '后台监测已在运行。'}else{StartWatcher; Alert '已请求启动后台监测。'; Refresh-Status} } catch { Log-GuiError '启动后台' $_ } })
[void](Button '停止后台' 395 237 130 { try { Set-Content -LiteralPath $stopPath -Value 'stop' -Encoding ASCII; Alert '已请求停止；请等待数秒后刷新状态。'; Refresh-Status } catch { Log-GuiError '停止后台' $_ } })
[void](Button '启用登录自启' 20 282 145 { try { SaveSettings; $shell=New-Object -ComObject WScript.Shell; $shortcut=$shell.CreateShortcut($linkPath); $shortcut.TargetPath=Join-Path $env:SystemRoot 'System32\wscript.exe'; $shortcut.Arguments='"'+(Join-Path $root 'Watch-Delta.vbs')+'"'; $shortcut.WorkingDirectory=$root; $shortcut.Save(); if(-not (Running)){StartWatcher}; Alert '已启用当前用户登录自启，文件夹请勿移动。'; Refresh-Status } catch { Log-GuiError '启用登录自启' $_ } })
[void](Button '关闭登录自启' 175 282 145 { try { Remove-Item -LiteralPath $linkPath -ErrorAction SilentlyContinue; Alert '已关闭登录自启；当前后台进程可用“停止后台”关闭。'; Refresh-Status } catch { Log-GuiError '关闭登录自启' $_ } })
[void](Button '恢复原分辨率' 330 282 145 { try { if(Running){throw '请先停止后台监测，等待数秒后刷新状态。'}; if(-not (Test-Path -LiteralPath $modePath)){throw '没有找到可恢复的原显示模式。'}; $m=Read-SavedMode $modePath; [ScreenMode]::Apply($m); Remove-Item -LiteralPath $modePath; Alert '已恢复原显示模式。' } catch { Log-GuiError '恢复原分辨率' $_ } })
[void](Button '查看错误日志' 20 327 145 { try { if(-not (Test-Path -LiteralPath $logPath)){Alert '目前没有错误日志。'}else{Start-Process -FilePath 'notepad.exe' -ArgumentList ('"'+$logPath+'"')} } catch { Log-GuiError '打开错误日志' $_ } })
[void](Button '刷新状态' 175 327 145 { Refresh-Status })
[void](Label '设置界面关闭后，后台监测仍会继续运行。' 20 385 510)
Refresh-Status
if ($SelfTest) { Write-Output ("UI created: " + $form.Controls.Count + ' controls'); $form.Dispose() }
else { [void]$form.ShowDialog() }

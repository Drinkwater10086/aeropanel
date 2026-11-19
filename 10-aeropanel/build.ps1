param([string]$ZigPath = 'zig', [switch]$Tests)
$ErrorActionPreference = 'Stop'
$compiler = (Get-Command $ZigPath -ErrorAction Stop).Source
$build = Join-Path $PSScriptRoot '.build'
New-Item -ItemType Directory -Force $build | Out-Null
$names = @('ZIG_GLOBAL_CACHE_DIR','ZIG_LOCAL_CACHE_DIR','TEMP','TMP')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
try {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $build 'cache-global'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $build 'cache-local'
    $env:TEMP = $build
    $env:TMP = $build
    $source = Join-Path $PSScriptRoot 'src\AeroPanel.cpp'
    $output = Join-Path $build 'AeroPanel.exe'
    $link = @('-Wl,--subsystem,windows')
    if ($Tests) {
        $source = Join-Path $PSScriptRoot 'tests\SuiteTests.cpp'
        $output = Join-Path $build 'SuiteTests.exe'
        $link = @()
    }
    & $compiler c++ $source -o $output -target x86-windows-gnu -Os -s @link -municode -fno-exceptions -fno-rtti -luser32 -lgdi32 -lpsapi -lshell32 -ldwmapi -lcomdlg32 -lole32 -luuid
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    if ($Tests) {
        & $output
        if ($LASTEXITCODE -ne 0) { throw "Tests failed: $LASTEXITCODE" }
    }
    Write-Output "Built: $output"
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') }
}

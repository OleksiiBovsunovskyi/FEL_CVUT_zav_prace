<#
.SYNOPSIS
    Builds pgr-vk, runs it against a model, fails on any validation error.

.DESCRIPTION
    Covers what a unit test cannot: device feature agreement, pipeline and
    attachment format match, the glTF load path and the draw. Validation layers
    supply the assertions.

.PARAMETER BuildDir
    Existing CMake build directory.

.PARAMETER Model
    .gltf or .glb to load.

.PARAMETER Frames
    Frame budget; the app closes itself afterwards so stdout is flushed.

.PARAMETER TimeoutSeconds
    Hard limit before the run is considered hung.
#>
param(
    [Parameter(Mandatory = $true)][string] $BuildDir,
    [Parameter(Mandatory = $true)][string] $Model,
    [int] $Frames = 240,
    [int] $TimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'

$buildLog = cmake --build $BuildDir --target pgr-vk 2>&1
if ($LASTEXITCODE -ne 0) {
    $buildLog | ForEach-Object { Write-Host $_ }
    Write-Host 'smoke: build failed' -ForegroundColor Red
    exit 1
}

$exe = Join-Path $BuildDir 'vk/pgr-vk.exe'
if (-not (Test-Path $exe)) { Write-Host "smoke: not found: $exe" -ForegroundColor Red; exit 1 }

$log = [System.IO.Path]::GetTempFileName()

# WorkingDirectory, because SHADER_DIR is relative to the executable and
# Start-Process would otherwise inherit the caller's directory.
$app = Start-Process $exe -ArgumentList (Resolve-Path $Model), $Frames -PassThru -NoNewWindow `
                     -WorkingDirectory (Split-Path -Parent $exe) `
                     -RedirectStandardError $log -RedirectStandardOutput "$log.out"

$exited = $app.WaitForExit($TimeoutSeconds * 1000)
if (-not $exited) { $app.Kill(); $app.WaitForExit() }

$output = @(Get-Content $log -ErrorAction SilentlyContinue) +
          @(Get-Content "$log.out" -ErrorAction SilentlyContinue)
Remove-Item $log, "$log.out" -ErrorAction SilentlyContinue

# EOS_Overlay is an unrelated Epic Games layer warning present on some machines.
$bad = $output | Where-Object { $_ -match 'VUID|Validation|Error:' -and
                                $_ -notmatch 'EOS_Overlay' }

if ($bad) {
    $bad | ForEach-Object { Write-Host $_ }
    Write-Host "smoke: $($bad.Count) validation error(s)" -ForegroundColor Red
    exit 1
}

if (-not $exited) {
    Write-Host "smoke: still running after ${TimeoutSeconds}s" -ForegroundColor Red
    exit 1
}

if (-not ($output -match 'GltfLoader:.*objects')) {
    Write-Host 'smoke: model never loaded' -ForegroundColor Red
    exit 1
}

Write-Host 'smoke: ok' -ForegroundColor Green

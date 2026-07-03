param(
    [string]$SteamVRRoot,
    [string]$DriverRoot,
    [string]$SteamConfigPath,
    [string]$BackupPath,
    [switch]$StopSteamVR = $true
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common_steamvr.ps1")

if (-not $DriverRoot) { $DriverRoot = Join-Path (Join-Path $PSScriptRoot "..") "build\out\anyadance" }
if (Test-Path $DriverRoot) { $DriverRoot = (Resolve-Path $DriverRoot).Path }
$steamvr = Resolve-SteamVRRoot -SteamVRRoot $SteamVRRoot
$vrpathreg = $steamvr.VrPathReg

Write-Host "Unregistering AnyaDance at $DriverRoot" -ForegroundColor Cyan
if ($StopSteamVR) {
    Write-Host "Stopping SteamVR processes" -ForegroundColor Yellow
    Get-Process vrserver -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process vrmonitor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process vrcompositor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

# The bundle may have moved since registration, so $DriverRoot no longer matches
# the registered path. Also remove the exact path recorded at register time.
$recordPath = Get-AnyaDanceRegisteredPathRecord
$recordedRoot = $null
if (Test-Path $recordPath) { $recordedRoot = ([System.IO.File]::ReadAllText($recordPath)).Trim() }

function Test-AnyaDanceDriverRoot {
    param([string]$Root)

    if (-not $Root) { return $false }
    $manifestPath = Join-Path $Root "driver.vrdrivermanifest"
    if (-not (Test-Path -LiteralPath $manifestPath)) { return $false }
    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        return $manifest.name -eq "anyadance"
    } catch {
        return $false
    }
}

# A prior unregister attempt may have consumed the recorded path without removing
# the registration. Discover every extant AnyaDance bundle from OpenVR's driver
# list as a fallback, then remove all known candidate roots.
$rootsToRemove = @($DriverRoot)
if ($recordedRoot) { $rootsToRemove += $recordedRoot }
$openvrPaths = Join-Path $env:LOCALAPPDATA "openvr\openvrpaths.vrpath"
if (Test-Path -LiteralPath $openvrPaths) {
    $paths = Get-Content -LiteralPath $openvrPaths -Raw | ConvertFrom-Json
    foreach ($root in @($paths.external_drivers)) {
        if (Test-AnyaDanceDriverRoot -Root ([string]$root)) {
            $rootsToRemove += [string]$root
        }
    }
}

foreach ($root in ($rootsToRemove | Where-Object { $_ } | Select-Object -Unique)) {
    Write-Host "Removing registration at $root" -ForegroundColor Cyan
    & $vrpathreg removedriver $root 2>$null | Out-Null
}
Write-Host "Registration state after removal:" -ForegroundColor Cyan
& $vrpathreg finddriver anyadance 2>$null
if ($LASTEXITCODE -eq 0) {
    throw "AnyaDance is still registered. The settings backup and registered-path record were preserved."
}

# Restore the pristine SteamVR settings captured at first registration, then
# clear the backup so the next registration captures a fresh baseline.
if (-not $SteamConfigPath) { $SteamConfigPath = Resolve-SteamVrSettingsPath }
if (-not $BackupPath) { $BackupPath = Get-AnyaDanceBackupPath }
if (Test-Path $BackupPath) {
    Copy-Item -LiteralPath $BackupPath -Destination $SteamConfigPath -Force
    Remove-Item -LiteralPath $BackupPath -Force
    Write-Host "Restored SteamVR settings from backup and removed the backup" -ForegroundColor Cyan
} else {
    Write-Host "No SteamVR settings backup found; leaving current settings in place" -ForegroundColor Yellow
}
if (Test-Path $recordPath) { Remove-Item -LiteralPath $recordPath -Force }
$global:LASTEXITCODE = 0

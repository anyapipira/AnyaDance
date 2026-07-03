param(
    [string]$SteamVRRoot,
    [string]$DriverRoot,
    [string]$SteamConfigPath,
    [string]$BackupPath,
    [string]$SteamExe,
    [switch]$NoRestart
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common_steamvr.ps1")

if (-not $SteamConfigPath) { $SteamConfigPath = Resolve-SteamVrSettingsPath }
if (-not $BackupPath) { $BackupPath = Get-AnyaDanceBackupPath }
if (-not $DriverRoot) {
    $bundleRoot = Split-Path -Parent $PSScriptRoot
    if (Test-Path -LiteralPath (Join-Path $bundleRoot "driver.vrdrivermanifest")) {
        $DriverRoot = $bundleRoot
    } else {
        $DriverRoot = Join-Path $bundleRoot "build\out\anyadance"
    }
}

function Remove-JsonProperty {
    param([object]$Object, [string]$Name)

    if ($null -ne $Object -and $Object.PSObject.Properties[$Name]) {
        $Object.PSObject.Properties.Remove($Name)
        return $true
    }
    return $false
}

# Preserve the complete pre-uninstall state independently of the registration
# backup, which a successful unregister intentionally consumes.
$openvrPaths = Join-Path $env:LOCALAPPDATA "openvr\openvrpaths.vrpath"
$recoveryRoot = Join-Path $env:LOCALAPPDATA "AnyaDance\uninstall-recovery"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$recoveryPath = Join-Path $recoveryRoot $timestamp
New-Item -ItemType Directory -Path $recoveryPath -Force | Out-Null

$snapshotFiles = @(
    @{ Source = $openvrPaths; Name = "openvrpaths.vrpath.before" },
    @{ Source = $SteamConfigPath; Name = "steamvr.vrsettings.before" },
    @{ Source = $BackupPath; Name = "steamvr.vrsettings.backup.before" },
    @{ Source = Get-AnyaDanceRegisteredPathRecord; Name = "registered_driver_path.txt.before" }
)
foreach ($file in $snapshotFiles) {
    if (Test-Path -LiteralPath $file.Source) {
        Copy-Item -LiteralPath $file.Source -Destination (Join-Path $recoveryPath $file.Name) -Force
    }
}
Write-Host "Saved uninstall recovery snapshot to $recoveryPath" -ForegroundColor Cyan

$hadSettingsBackup = Test-Path -LiteralPath $BackupPath
$hadAnyaDanceSettings = $false
if (Test-Path -LiteralPath $SteamConfigPath) {
    $beforeSettings = Get-Content -LiteralPath $SteamConfigPath -Raw | ConvertFrom-Json
    $hadAnyaDanceSettings =
        [bool]$beforeSettings.PSObject.Properties["driver_anyadance"] -or
        ($beforeSettings.steamvr.forcedDriver -eq "anyadance")
}

$unregisterArgs = @{
    SteamVRRoot = $SteamVRRoot
    DriverRoot = $DriverRoot
    SteamConfigPath = $SteamConfigPath
    BackupPath = $BackupPath
    StopSteamVR = $true
}
& (Join-Path $PSScriptRoot "unregister_driver.ps1") @unregisterArgs

# A missing pristine backup can happen after an older unregister bug consumed it.
# In that case, remove only values that identify AnyaDance or exactly match the
# overrides AnyaDance writes. The timestamped snapshot remains available if any
# of those generic SteamVR values had also been customized by the user.
if (-not $hadSettingsBackup -and $hadAnyaDanceSettings -and (Test-Path -LiteralPath $SteamConfigPath)) {
    $settings = Get-Content -LiteralPath $SteamConfigPath -Raw | ConvertFrom-Json
    $changed = Remove-JsonProperty -Object $settings -Name "driver_anyadance"

    if ($settings.steamvr.forcedDriver -eq "anyadance") {
        $changed = (Remove-JsonProperty -Object $settings.steamvr -Name "forcedDriver") -or $changed
    }
    if ($settings.steamvr.activateMultipleDrivers -eq $true) {
        $changed = (Remove-JsonProperty -Object $settings.steamvr -Name "activateMultipleDrivers") -or $changed
    }
    if ($settings.power.turnOffScreensTimeout -eq 86400) {
        $changed = (Remove-JsonProperty -Object $settings.power -Name "turnOffScreensTimeout") -or $changed
    }
    if ($null -ne $settings.power -and
        $settings.power.PSObject.Properties["pauseCompositorOnStandby"] -and
        $settings.power.pauseCompositorOnStandby -eq $false) {
        $changed = (Remove-JsonProperty -Object $settings.power -Name "pauseCompositorOnStandby") -or $changed
    }

    if ($changed) {
        $settings | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $SteamConfigPath -Encoding UTF8
        Write-Host "Removed stale AnyaDance SteamVR overrides (no pristine settings backup was available)." -ForegroundColor Yellow
    }
}

$steamvr = Resolve-SteamVRRoot -SteamVRRoot $SteamVRRoot
& $steamvr.VrPathReg finddriver anyadance 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) {
    throw "Uninstall verification failed: AnyaDance is still registered. Recovery snapshot: $recoveryPath"
}

if (-not $NoRestart) {
    & (Join-Path $PSScriptRoot "restart_steamvr.ps1") -SteamExe $SteamExe
} else {
    Write-Host "SteamVR remains stopped. Restart it before using physical VR devices." -ForegroundColor Yellow
}

Write-Host "AnyaDance driver uninstall completed. Application files were not deleted." -ForegroundColor Green
$global:LASTEXITCODE = 0

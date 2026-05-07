[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$FrameworkDependent,
    [switch]$SkipSelfTest,
    [switch]$SkipZip
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$projectPath = Join-Path $repoRoot "apps\Gx12.Launcher.Wpf\Gx12.Launcher.Wpf.csproj"
$runtimeExe = Join-Path $repoRoot "runtime\gx12mouse.exe"
$profilesDir = Join-Path $repoRoot "profiles"
$defaultProfileMarker = Join-Path $repoRoot ".gx12-default-profile"
$uiSpritesDir = Join-Path $repoRoot ".gx12-ui\ui-sprites"
$tooltipSpritesDir = Join-Path $repoRoot ".gx12-ui\tooltip-sprites"
$licensePath = Join-Path $repoRoot "LICENSE"
$distRoot = Join-Path $repoRoot "dist"

function ConvertTo-PackageVersionToken {
    param([Parameter(Mandatory = $true)][string]$Version)

    $safe = [regex]::Replace($Version, "[^0-9A-Za-z.-]", "-").Trim([char[]]@('-', '.'))
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "0.0.0"
    }

    $safe = [regex]::Replace($safe, "(?i)(^|[.-])preview[.-]?([0-9]+)", '-p$2').Trim([char[]]@('-', '.'))
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "0.0.0"
    }

    return $safe
}

function Compress-DirectoryContents {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    $items = @(Get-ChildItem -LiteralPath $SourceDirectory -Force)
    if ($items.Count -eq 0) {
        throw "Refusing to create an empty zip from $SourceDirectory"
    }

    Compress-Archive -LiteralPath $items.FullName -DestinationPath $DestinationPath -Force
}

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Missing WPF project: $projectPath"
}

if (-not (Test-Path -LiteralPath $runtimeExe -PathType Leaf)) {
    throw "Missing runtime executable: $runtimeExe"
}

if (-not (Test-Path -LiteralPath $profilesDir -PathType Container)) {
    throw "Missing profile directory: $profilesDir"
}

if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) {
    throw "Missing GPLv2 license file: $licensePath"
}

[xml]$projectXml = Get-Content -LiteralPath $projectPath -Raw
[string]$version = $projectXml.Project.PropertyGroup.Version
if ([string]::IsNullOrWhiteSpace($version)) {
    $version = "0.0.0"
}

$safeVersion = ConvertTo-PackageVersionToken -Version $version

$packageName = "Asukaflight-$safeVersion-$RuntimeIdentifier"
$packageRoot = Join-Path $distRoot $packageName
$releaseExeName = "Asukaflight.exe"
$publishedExeName = "Asukaflight.exe"
$releaseExe = Join-Path $packageRoot $releaseExeName
$zipPath = Join-Path $distRoot "$packageName.zip"

$resolvedRepoRoot = (Resolve-Path -LiteralPath $repoRoot).Path
$resolvedDistRoot = [System.IO.Path]::GetFullPath($distRoot)
$resolvedPackageRoot = [System.IO.Path]::GetFullPath($packageRoot)
$resolvedPackageRootWithSeparator = $resolvedPackageRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar

if (-not $resolvedDistRoot.StartsWith($resolvedRepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write dist outside repo root: $resolvedDistRoot"
}

if (-not $resolvedPackageRoot.StartsWith($resolvedDistRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write package outside dist root: $resolvedPackageRoot"
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

$selfContained = if ($FrameworkDependent.IsPresent) { "false" } else { "true" }

$publishArgs = @(
    "publish",
    $projectPath,
    "-c",
    $Configuration,
    "-r",
    $RuntimeIdentifier,
    "--self-contained",
    $selfContained,
    "-p:PublishSingleFile=false",
    "-p:DebugType=none",
    "-p:DebugSymbols=false",
    "-o",
    $packageRoot
)

& dotnet @publishArgs
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed with exit code $LASTEXITCODE"
}

$publishedExe = Join-Path $packageRoot $publishedExeName
if (-not (Test-Path -LiteralPath $publishedExe -PathType Leaf)) {
    throw "Publish did not produce $publishedExeName"
}

if (-not [string]::Equals(
    [System.IO.Path]::GetFullPath($publishedExe),
    [System.IO.Path]::GetFullPath($releaseExe),
    [System.StringComparison]::OrdinalIgnoreCase)) {
    Move-Item -LiteralPath $publishedExe -Destination $releaseExe -Force
}

$packageRuntimeDir = Join-Path $packageRoot "runtime"
New-Item -ItemType Directory -Path $packageRuntimeDir -Force | Out-Null
Copy-Item -LiteralPath $runtimeExe -Destination (Join-Path $packageRuntimeDir "gx12mouse.exe") -Force

$packageProfilesDir = Join-Path $packageRoot "profiles"
Copy-Item -LiteralPath $profilesDir -Destination $packageProfilesDir -Recurse -Force

if (Test-Path -LiteralPath $defaultProfileMarker -PathType Leaf) {
    Copy-Item -LiteralPath $defaultProfileMarker -Destination (Join-Path $packageRoot ".gx12-default-profile") -Force
}

if (Test-Path -LiteralPath $uiSpritesDir -PathType Container) {
    $packageUiSpritesDir = Join-Path $packageRoot ".gx12-ui\ui-sprites"
    New-Item -ItemType Directory -Path $packageUiSpritesDir -Force | Out-Null
    Get-ChildItem -LiteralPath $uiSpritesDir -Filter "*.png" -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $packageUiSpritesDir $_.Name) -Force
    }
}

if (Test-Path -LiteralPath $tooltipSpritesDir -PathType Container) {
    $packageTooltipSpritesDir = Join-Path $packageRoot ".gx12-ui\tooltip-sprites"
    New-Item -ItemType Directory -Path $packageTooltipSpritesDir -Force | Out-Null
    Get-ChildItem -LiteralPath $tooltipSpritesDir -Filter "*.png" -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $packageTooltipSpritesDir $_.Name) -Force
    }
}

Copy-Item -LiteralPath $licensePath -Destination (Join-Path $packageRoot "LICENSE") -Force

$packageLogsDir = Join-Path $packageRoot "logs"
New-Item -ItemType Directory -Path $packageLogsDir -Force | Out-Null

Get-ChildItem -LiteralPath $packageRoot -Recurse -Filter "*.pdb" -File | ForEach-Object {
    $symbolPath = [System.IO.Path]::GetFullPath($_.FullName)
    if (-not $symbolPath.StartsWith($resolvedPackageRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove symbol outside package root: $symbolPath"
    }

    Remove-Item -LiteralPath $_.FullName -Force
}

$releaseMarker = @"
Asukaflight release
Version: $version
Runtime: $RuntimeIdentifier
Entry point: $releaseExeName

Keep this folder together. The launcher expects profiles\ and runtime\gx12mouse.exe beside this file.

License: GNU General Public License, version 2 only. See LICENSE.
"@
Set-Content -LiteralPath (Join-Path $packageRoot "ASUKAFLIGHT-RELEASE.txt") -Value $releaseMarker -Encoding ASCII

$readme = @"
Asukaflight

Run: $releaseExeName

Keep the extracted folder intact. The launcher needs the profiles folder and runtime\gx12mouse.exe beside it.

The default profile is selected from .gx12-default-profile when present. The current default is whoop-linear.toml in this repo.

Real-flight sanity checklist: verify physical throttle cut, keep the GX12 trainer routing on the tested model, confirm F1 stop and F3 freeze for the selected profile, and do the first run after any package change with props off.

License: GNU General Public License, version 2 only. See LICENSE. Matching source, including the GX12 firmware source and rebuild notes, is provided in the source tree.
"@
Set-Content -LiteralPath (Join-Path $packageRoot "README-FIRST.txt") -Value $readme -Encoding ASCII

if (-not $SkipSelfTest.IsPresent) {
    $selfTestProcess = Start-Process -FilePath $releaseExe -ArgumentList @("--self-test") -WorkingDirectory $packageRoot -Wait -PassThru -WindowStyle Hidden
    if ($selfTestProcess.ExitCode -ne 0) {
        throw "Packaged launcher self-test failed with exit code $($selfTestProcess.ExitCode)"
    }

    $selfTestLog = Join-Path $packageLogsDir "wpf-launcher-self-test.txt"
    if (-not (Test-Path -LiteralPath $selfTestLog -PathType Leaf)) {
        throw "Packaged launcher self-test did not write the expected log: $selfTestLog"
    }

    $selfTestText = Get-Content -LiteralPath $selfTestLog -Raw
    if ($selfTestText.IndexOf("status=ok", [System.StringComparison]::Ordinal) -lt 0) {
        throw "Packaged launcher self-test log did not report status=ok."
    }
}

if (Test-Path -LiteralPath $packageLogsDir) {
    $resolvedPackageLogsDir = [System.IO.Path]::GetFullPath($packageLogsDir)
    if (-not $resolvedPackageLogsDir.StartsWith($resolvedPackageRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove logs outside package root: $resolvedPackageLogsDir"
    }

    Remove-Item -LiteralPath $packageLogsDir -Recurse -Force
}

if (-not $SkipZip.IsPresent) {
    $compressed = $false
    for ($attempt = 1; $attempt -le 5 -and -not $compressed; $attempt++) {
        try {
            if ($attempt -gt 1) {
                Start-Sleep -Seconds 1
            }

            Compress-DirectoryContents -SourceDirectory $packageRoot -DestinationPath $zipPath
            $compressed = $true
        }
        catch {
            if ($attempt -eq 5) {
                throw
            }

            Write-Warning "Compress-Archive failed on attempt $attempt; retrying after file handles settle."
        }
    }
}

Write-Host "Release package: $packageRoot"
if (-not $SkipZip.IsPresent) {
    Write-Host "Release zip: $zipPath"
}
Write-Host "Entry point: $releaseExe"

[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [string]$FirmwarePath = "",
    [string]$FirmwareSdFileName = "R2X-871.BIN",
    [string]$ExperimentalTx16sClassicFirmwarePath = "",
    [string]$ExperimentalTx16sMk3Uf2Path = "",
    [string]$ExperimentalTx16sMk3BinPath = "",
    [switch]$SkipExperimentalTx16s,
    [switch]$SkipReleasePublish,
    [switch]$SkipZip
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$projectPath = Join-Path $repoRoot "apps\Gx12.Launcher.Wpf\Gx12.Launcher.Wpf.csproj"
$releaseScript = Join-Path $repoRoot "tools\publish-gx12-release.ps1"
$readmePath = Join-Path $repoRoot "README.md"
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

$forbiddenReleaseStrings = [System.Collections.Generic.List[string]]::new()

function Add-ForbiddenReleaseString {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return
    }

    if (-not $script:forbiddenReleaseStrings.Contains($Value)) {
        $script:forbiddenReleaseStrings.Add($Value) | Out-Null
    }
}

function Add-ForbiddenPathString {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    Add-ForbiddenReleaseString $fullPath
    Add-ForbiddenReleaseString ($fullPath -replace "\\", "/")
}

Add-ForbiddenPathString $repoRoot
Add-ForbiddenPathString (Split-Path -Parent $repoRoot)
foreach ($privatePath in @($env:USERPROFILE, $env:HOME, $env:LOCALAPPDATA, $env:APPDATA, $env:TEMP, $env:TMP)) {
    Add-ForbiddenPathString $privatePath
}
if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
    Add-ForbiddenPathString (Split-Path -Parent $env:USERPROFILE)
}
foreach ($needle in @(
    "DESKTOP-",
    "CodexSandbox"
)) {
    Add-ForbiddenReleaseString $needle
}
foreach ($needlePart in @(
    @(".local", "-agent-config"),
    @(".assist", "ant"),
    @("HAND", "OFF.md"),
    @("AG", "ENTS.md"),
    @("LOCAL", "_AGENT_NOTES.md"),
    @("security", "_scan.md")
)) {
    Add-ForbiddenReleaseString ($needlePart -join "")
}

function Test-BytesContainAsciiString {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][string]$Needle
    )

    if ([string]::IsNullOrEmpty($Needle)) {
        return $false
    }

    $needleBytes = [System.Text.Encoding]::ASCII.GetBytes($Needle)
    if ($Bytes.Length -lt $needleBytes.Length) {
        return $false
    }

    for ($index = 0; $index -le $Bytes.Length - $needleBytes.Length; $index++) {
        $matched = $true
        for ($needleIndex = 0; $needleIndex -lt $needleBytes.Length; $needleIndex++) {
            if ($Bytes[$index + $needleIndex] -ne $needleBytes[$needleIndex]) {
                $matched = $false
                break
            }
        }

        if ($matched) {
            return $true
        }
    }

    return $false
}

function Find-ForbiddenReleaseStringInBytes {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    foreach ($needle in $script:forbiddenReleaseStrings) {
        if (Test-BytesContainAsciiString -Bytes $Bytes -Needle $needle) {
            return $needle
        }
    }

    return $null
}

function Assert-NoForbiddenReleaseStrings {
    param(
        [Parameter(Mandatory = $true)][string[]]$Paths,
        [Parameter(Mandatory = $true)][string]$Context
    )

    foreach ($path in $Paths) {
        if (Test-Path -LiteralPath $path -PathType Container) {
            $files = @(Get-ChildItem -LiteralPath $path -Recurse -File -Force)
        }
        elseif (Test-Path -LiteralPath $path -PathType Leaf) {
            $files = @(Get-Item -LiteralPath $path)
        }
        else {
            throw "$Context scan path does not exist: $path"
        }

        foreach ($file in $files) {
            $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
            $hit = Find-ForbiddenReleaseStringInBytes -Bytes $bytes
            if ($null -ne $hit) {
                throw "$Context contains forbidden release string '$hit' in $($file.FullName)"
            }
        }
    }
}

function Assert-NoForbiddenReleaseStringsInZip {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$Context
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($entry in $archive.Entries) {
            if ([string]::IsNullOrEmpty($entry.Name)) {
                continue
            }

            $stream = $entry.Open()
            try {
                $memory = [System.IO.MemoryStream]::new()
                try {
                    $stream.CopyTo($memory)
                    $hit = Find-ForbiddenReleaseStringInBytes -Bytes $memory.ToArray()
                    if ($null -ne $hit) {
                        throw "$Context contains forbidden release string '$hit' in zip entry $($entry.FullName)"
                    }
                }
                finally {
                    $memory.Dispose()
                }
            }
            finally {
                $stream.Dispose()
            }
        }
    }
    finally {
        $archive.Dispose()
    }
}

if ([string]::IsNullOrWhiteSpace($FirmwarePath)) {
    $FirmwarePath = Join-Path $repoRoot "firmware\R2X-871.BIN"
}

if ([string]::IsNullOrWhiteSpace($ExperimentalTx16sClassicFirmwarePath)) {
    $ExperimentalTx16sClassicFirmwarePath = Join-Path $repoRoot "firmware\experimental-tx16s\tx16s-asukaflight-composite-cdc-hid-resolution2x-2.11.0-EXPERIMENTAL-UNTESTED-EA8F1086.bin"
}

if ([string]::IsNullOrWhiteSpace($ExperimentalTx16sMk3Uf2Path)) {
    $ExperimentalTx16sMk3Uf2Path = Join-Path $repoRoot "firmware\experimental-tx16s\tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-8CC87AA0.uf2"
}

if ([string]::IsNullOrWhiteSpace($ExperimentalTx16sMk3BinPath)) {
    $ExperimentalTx16sMk3BinPath = Join-Path $repoRoot "firmware\experimental-tx16s\tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-326BF504.bin"
}

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Missing WPF project: $projectPath"
}

if (-not (Test-Path -LiteralPath $releaseScript -PathType Leaf)) {
    throw "Missing release publish script: $releaseScript"
}

if (-not (Test-Path -LiteralPath $FirmwarePath -PathType Leaf)) {
    throw "Missing firmware binary: $FirmwarePath"
}

if (-not (Test-Path -LiteralPath $readmePath -PathType Leaf)) {
    throw "Missing README.md: $readmePath"
}

function Get-ReadmeUserDirections {
    param([Parameter(Mandatory = $true)][string]$Path)

    $lines = @(Get-Content -LiteralPath $Path)
    $startIndex = -1
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index].Trim() -eq "## User Directions") {
            $startIndex = $index
            break
        }
    }

    if ($startIndex -lt 0) {
        throw "README.md does not contain a '## User Directions' section."
    }

    $sectionLines = New-Object System.Collections.Generic.List[string]
    for ($index = $startIndex; $index -lt $lines.Count; $index++) {
        if ($index -gt $startIndex -and $lines[$index].StartsWith("## ")) {
            break
        }

        $sectionLines.Add($lines[$index])
    }

    $section = ($sectionLines -join [Environment]::NewLine).Trim()
    if ([string]::IsNullOrWhiteSpace($section)) {
        throw "README.md User Directions section is empty."
    }

    return $section
}

$expectedFirmwareHash = "8717A5BE0DD1A536AC7F5718CCD3F50F4DA835B889DCCBF56EEB44AA19FED71A"
$firmwareHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $FirmwarePath).Hash
if (-not [string]::Equals($firmwareHash, $expectedFirmwareHash, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Firmware hash mismatch for $FirmwarePath. Expected $expectedFirmwareHash, got $firmwareHash."
}

$experimentalTx16sFirmware = @()
if (-not $SkipExperimentalTx16s.IsPresent) {
    $experimentalTx16sSpecs = @(
        [pscustomobject]@{
            Label = "TX16S / TX16S II EdgeTX 2.11 raw BIN"
            Path = $ExperimentalTx16sClassicFirmwarePath
            ExpectedHash = "EA8F1086A3935672AEB24C6E26A13C3C7A7366F7956580C6DF86B094772E779B"
        },
        [pscustomobject]@{
            Label = "TX16S III / MK3 EdgeTX pre-3.0 UF2"
            Path = $ExperimentalTx16sMk3Uf2Path
            ExpectedHash = "8CC87AA0FAE6970D318EA2531C8A170E74CD0EB390D824855A9BC6CE7697B8E2"
        },
        [pscustomobject]@{
            Label = "TX16S III / MK3 EdgeTX pre-3.0 raw BIN"
            Path = $ExperimentalTx16sMk3BinPath
            ExpectedHash = "326BF504E49DDB0F88922E987DD566FB1F8CB8B42673C20672F091D3021CDCDD"
        }
    )

    foreach ($spec in $experimentalTx16sSpecs) {
        if (-not (Test-Path -LiteralPath $spec.Path -PathType Leaf)) {
            throw "Missing experimental TX16S firmware: $($spec.Path)"
        }

        $specHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $spec.Path).Hash
        if (-not [string]::Equals($specHash, $spec.ExpectedHash, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Experimental TX16S firmware hash mismatch for $($spec.Path). Expected $($spec.ExpectedHash), got $specHash."
        }

        $specItem = Get-Item -LiteralPath $spec.Path
        $experimentalTx16sFirmware += [pscustomobject]@{
            Label = $spec.Label
            Path = $spec.Path
            Leaf = $specItem.Name
            Hash = $specHash
            Size = $specItem.Length
        }
    }
}

$firmwareScanPaths = @($FirmwarePath)
if ($experimentalTx16sFirmware.Count -gt 0) {
    $firmwareScanPaths += @($experimentalTx16sFirmware | ForEach-Object { $_.Path })
}
Assert-NoForbiddenReleaseStrings -Paths $firmwareScanPaths -Context "Firmware input"

[xml]$projectXml = Get-Content -LiteralPath $projectPath -Raw
[string]$version = $projectXml.Project.PropertyGroup.Version
if ([string]::IsNullOrWhiteSpace($version)) {
    $version = "0.0.0"
}

$safeVersion = ConvertTo-PackageVersionToken -Version $version

$packageName = "Asukaflight-$safeVersion-$RuntimeIdentifier"
$packageRoot = Join-Path $distRoot $packageName
$distributionRoot = $packageRoot
$distributionZip = Join-Path $distRoot "$packageName.zip"

$resolvedRepoRoot = [System.IO.Path]::GetFullPath($repoRoot)
$resolvedDistRoot = [System.IO.Path]::GetFullPath($distRoot)
$resolvedDistributionRoot = [System.IO.Path]::GetFullPath($distributionRoot)

if (-not $resolvedDistRoot.StartsWith($resolvedRepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write dist outside repo root: $resolvedDistRoot"
}

if (-not $resolvedDistributionRoot.StartsWith($resolvedDistRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write distribution outside dist root: $resolvedDistributionRoot"
}

if (-not $SkipReleasePublish.IsPresent) {
    & $releaseScript -Configuration $Configuration -RuntimeIdentifier $RuntimeIdentifier -SkipZip
    if ($LASTEXITCODE -ne 0) {
        throw "Release publish failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) {
    throw "Missing release package root: $packageRoot"
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
if (Test-Path -LiteralPath $distributionZip) {
    Remove-Item -LiteralPath $distributionZip -Force
}

$legacyVersion = [regex]::Replace($version, "[^0-9A-Za-z.-]", "-").Trim([char[]]@('-', '.'))
if (-not [string]::IsNullOrWhiteSpace($legacyVersion)) {
    $legacyZipPaths = @(
        (Join-Path $distRoot "Asukaflight-$legacyVersion-$RuntimeIdentifier.zip"),
        (Join-Path $distRoot "Asukaflight-$legacyVersion-$RuntimeIdentifier-gx12-flash-and-run.zip"),
        (Join-Path $distRoot "$packageName-gx12-flash-and-run.zip")
    ) | Select-Object -Unique

    foreach ($legacyZipPath in $legacyZipPaths) {
        if (-not [string]::Equals(
            [System.IO.Path]::GetFullPath($legacyZipPath),
            [System.IO.Path]::GetFullPath($distributionZip),
            [System.StringComparison]::OrdinalIgnoreCase) -and
            (Test-Path -LiteralPath $legacyZipPath -PathType Leaf)) {
            Remove-Item -LiteralPath $legacyZipPath -Force
        }
    }
}

New-Item -ItemType Directory -Path $distributionRoot -Force | Out-Null

$firmwareRoot = Join-Path $distributionRoot "firmware"
if (Test-Path -LiteralPath $firmwareRoot) {
    Remove-Item -LiteralPath $firmwareRoot -Recurse -Force
}
foreach ($staleRootFile in @("README-FLASH-AND-RUN.txt", "MANIFEST-SHA256.txt")) {
    $staleRootFilePath = Join-Path $distributionRoot $staleRootFile
    if (Test-Path -LiteralPath $staleRootFilePath -PathType Leaf) {
        Remove-Item -LiteralPath $staleRootFilePath -Force
    }
}
New-Item -ItemType Directory -Path $firmwareRoot -Force | Out-Null

Copy-Item -LiteralPath $FirmwarePath -Destination (Join-Path $firmwareRoot $FirmwareSdFileName) -Force

if ($experimentalTx16sFirmware.Count -gt 0) {
    $tx16sExperimentalRoot = Join-Path $firmwareRoot "TX16S-EXPERIMENTAL"
    New-Item -ItemType Directory -Path $tx16sExperimentalRoot -Force | Out-Null

    foreach ($tx16sFirmware in $experimentalTx16sFirmware) {
        Copy-Item -LiteralPath $tx16sFirmware.Path -Destination (Join-Path $tx16sExperimentalRoot $tx16sFirmware.Leaf) -Force
    }

    $tx16sFileList = ($experimentalTx16sFirmware | ForEach-Object {
        "- $($_.Leaf)`n  Target: $($_.Label)`n  SHA256: $($_.Hash)`n  Size: $($_.Size) bytes"
    }) -join [Environment]::NewLine

    $tx16sReadme = @"
Experimental TX16S firmware

These files are not the validated GX12 firmware. They are untested experimental
TX16S/TX16S MK3 builds and are not guaranteed to boot, enumerate over USB, or
behave correctly on a real radio.

Do not flash these files to a GX12. Use the validated GX12 firmware files in
the parent firmware directory for GX12 radios.

Files:
$tx16sFileList

Target mapping:
- TX16S / TX16S II: use only the 2.11.0 raw BIN candidate if your radio and
  recovery path match that EdgeTX target.
- TX16S III / MK3: use the MK3 UF2 candidate for the normal MK3 bootloader UF2
  flow. The MK3 raw BIN is included only for workflows that explicitly require
  a raw binary.

Before flashing:
- Back up models, radio settings, and the current stock firmware.
- Know the EdgeTX/RadioMaster recovery path for your exact radio.
- Keep a stock firmware file ready for rollback.
- First power-up and USB tests should be props off, then simulator-only.
- Recheck trainer routing, USB joystick mode, stop/freeze behavior, and RF
  failsafe behavior before any real vehicle test.
"@
    Set-Content -LiteralPath (Join-Path $tx16sExperimentalRoot "README-TX16S-EXPERIMENTAL.txt") -Value $tx16sReadme -Encoding ASCII
}

$firmwareReadmeTx16sSection = if ($experimentalTx16sFirmware.Count -gt 0) {
@"

TX16S experimental firmware:
- firmware\TX16S-EXPERIMENTAL\ contains untested TX16S/TX16S MK3 candidates.
- These files are not guaranteed and are not the GX12 firmware.
- Read firmware\TX16S-EXPERIMENTAL\README-TX16S-EXPERIMENTAL.txt before any flash attempt.
"@
}
else {
    ""
}

$firmwareReadme = @"
Firmware directory

GX12 validated firmware:
- firmware\$FirmwareSdFileName

Use the GX12 files above for the normal Asukaflight GX12 setup path.
$firmwareReadmeTx16sSection
"@
Set-Content -LiteralPath (Join-Path $firmwareRoot "README-FIRMWARE.txt") -Value $firmwareReadme -Encoding ASCII

$pdbFiles = @(Get-ChildItem -LiteralPath $distributionRoot -Recurse -Filter "*.pdb" -File)
if ($pdbFiles.Count -gt 0) {
    throw "Distribution contains PDB files: $($pdbFiles.FullName -join ', ')"
}

$userDirections = Get-ReadmeUserDirections -Path $readmePath
$readmeTx16sContentsLine = if ($experimentalTx16sFirmware.Count -gt 0) {
    "- firmware\TX16S-EXPERIMENTAL\ - untested TX16S/TX16S MK3 firmware candidates, not GX12 firmware"
}
else {
    ""
}

$readmeTx16sSafetyLine = if ($experimentalTx16sFirmware.Count -gt 0) {
    "- TX16S firmware in this package is experimental, untested, and not guaranteed; do not flash TX16S files to a GX12."
}
else {
    ""
}

$readme = @"
Asukaflight GX12 flash-and-run distribution

Contents:
- Asukaflight.exe - control program
- profiles\ - bundled tuning profiles
- runtime\gx12mouse.exe - native trainer runtime
- LICENSE - GNU General Public License, version 2 only
- firmware\$FirmwareSdFileName - validated GX12 composite CDC/HID firmware
$readmeTx16sContentsLine

Setup directions from README.md:

$userDirections

Safety:
- First run after any package or firmware change should be props off.
- Recheck physical throttle cut, trainer routing, stale-trainer timeout, stop/freeze keys, disconnect behavior, and rollback firmware.
- The default profile uses F1 stop and F3 freeze.
$readmeTx16sSafetyLine

Source and license:
- Asukaflight and the EdgeTX-derived GX12 firmware source are distributed under GPLv2 only.
- Matching firmware source and byte-for-byte rebuild notes are in the source tree under firmware\edgetx-gx12-2.11.0 and docs\firmware-source.md.
"@
Set-Content -LiteralPath (Join-Path $distributionRoot "README-FLASH-AND-RUN.txt") -Value $readme -Encoding ASCII

$manifestPath = Join-Path $distributionRoot "MANIFEST-SHA256.txt"
$distributionRootFull = [System.IO.Path]::GetFullPath($distributionRoot)
Get-ChildItem -LiteralPath $distributionRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = [System.IO.Path]::GetFullPath($_.FullName).Substring($distributionRootFull.Length).TrimStart(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
    "$hash  $relative"
} | Set-Content -LiteralPath $manifestPath -Encoding ASCII

Assert-NoForbiddenReleaseStrings -Paths @($distributionRoot) -Context "Distribution folder"

if (-not $SkipZip.IsPresent) {
    $compressed = $false
    for ($attempt = 1; $attempt -le 5 -and -not $compressed; $attempt++) {
        try {
            if ($attempt -gt 1) {
                Start-Sleep -Seconds 1
            }

            Compress-DirectoryContents -SourceDirectory $distributionRoot -DestinationPath $distributionZip
            $compressed = $true
        }
        catch {
            if ($attempt -eq 5) {
                throw
            }

            Write-Warning "Compress-Archive failed on attempt $attempt; retrying after file handles settle."
        }
    }

    Assert-NoForbiddenReleaseStringsInZip -ZipPath $distributionZip -Context "Distribution zip"
}

Write-Host "Distribution package: $distributionRoot"
if (-not $SkipZip.IsPresent) {
    Write-Host "Distribution zip: $distributionZip"
}
Write-Host "Firmware SHA256: $firmwareHash"

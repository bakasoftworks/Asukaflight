# GX12 Firmware Source

The public source snapshot includes the EdgeTX-derived firmware source under:

```text
firmware\edgetx-gx12-2.11.0
```

The firmware tree is the EdgeTX-derived source used to build the validated GX12
firmware artifact. Generated build directories, local toolchains, local Python
shims, and Windows binary byproducts are intentionally not included.

## Validated Artifact

```text
firmware\R2X-7D8.BIN
SHA256 7D8FAE80FDC88E872832DEDAABB0DE3DCAC47125DC35386BE8FE5B9EE0FCE071
Size   545,528 bytes
```

The source workspace may keep longer local build-output filenames, but the
public repo and distribution package expose the validated GX12 image as
`firmware\R2X-7D8.BIN`.

## Experimental TX16S Artifacts

The source tree also carries untested TX16S/TX16S MK3 firmware candidates under
`firmware\experimental-tx16s`. These are experimental tester artifacts, not the
validated GX12 firmware, and are not guaranteed.

```text
tx16s-asukaflight-composite-cdc-hid-resolution2x-2.11.0-EXPERIMENTAL-UNTESTED-EA8F1086.bin
SHA256 EA8F1086A3935672AEB24C6E26A13C3C7A7366F7956580C6DF86B094772E779B
Size   545,528 bytes

tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-8CC87AA0.uf2
SHA256 8CC87AA0FAE6970D318EA2531C8A170E74CD0EB390D824855A9BC6CE7697B8E2
Size   545,528 bytes

tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-326BF504.bin
SHA256 326BF504E49DDB0F88922E987DD566FB1F8CB8B42673C20672F091D3021CDCDD
Size   545,528 bytes
```

## Rebuild Recipe

Install or unpack CMake, Ninja, Python, `grep`, and the ARM GNU toolchain, then
set the variables below for your local toolchain paths. From the repository
root:

```powershell
$repo = (Get-Location).Path
$firmwareSource = Join-Path $repo 'firmware\edgetx-gx12-2.11.0'
$cmake = '<path-to-cmake.exe>'
$ninja = '<path-to-ninja.exe>'
$armToolchainBin = '<path-to-arm-gnu-toolchain-bin-directory>'
$python = '<path-to-python.exe>'
$grepDirectory = '<path-to-directory-containing-grep.exe>'

$env:SOURCE_DATE_EPOCH = '1777414649'
$repoForward = $repo.Replace('\', '/')
$repoForwardLowerDrive = if ($repoForward.Length -gt 1 -and $repoForward[1] -eq ':') {
  $repoForward.Substring(0, 1).ToLowerInvariant() + $repoForward.Substring(1)
} else {
  $repoForward
}
$prefixMaps = @(
  "-ffile-prefix-map=$repoForward=ASUKA_ROOT",
  "-fmacro-prefix-map=$repoForward=ASUKA_ROOT",
  "-ffile-prefix-map=$repoForwardLowerDrive=ASUKA_ROOT",
  "-fmacro-prefix-map=$repoForwardLowerDrive=ASUKA_ROOT"
) -join ' '
& $cmake `
  -S $firmwareSource `
  -B (Join-Path $firmwareSource 'build-fw-gx12-verify') `
  -G Ninja `
  -DCMAKE_MAKE_PROGRAM=$ninja `
  -DCMAKE_TOOLCHAIN_FILE="$firmwareSource\cmake\toolchain\arm-none-eabi.cmake" `
  -DARM_TOOLCHAIN_DIR=$armToolchainBin `
  -DEdgeTX_SUPERBUILD=OFF `
  -DNATIVE_BUILD=OFF `
  -DPCB=X7 `
  -DPCBREV=GX12 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_FLAGS="$prefixMaps" `
  -DCMAKE_CXX_FLAGS="$prefixMaps" `
  -DCMAKE_ASM_FLAGS="$prefixMaps" `
  -DFIRMWARE_C_FLAGS="$prefixMaps" `
  -DFIRMWARE_CXX_FLAGS="$prefixMaps" `
  -DTRANSLATIONS=EN `
  -DPYTHON_EXECUTABLE=$python

$env:Path = $grepDirectory + [System.IO.Path]::PathSeparator + $env:Path
& $ninja `
  -C (Join-Path $firmwareSource 'build-fw-gx12-verify') `
  firmware `
  -j8
```

Then verify:

```powershell
Get-FileHash -Algorithm SHA256 -Path firmware\edgetx-gx12-2.11.0\build-fw-gx12-verify\firmware.bin
```

Expected SHA256:

```text
7D8FAE80FDC88E872832DEDAABB0DE3DCAC47125DC35386BE8FE5B9EE0FCE071
```

`SOURCE_DATE_EPOCH=1777414649` and the prefix-map flags are required for
byte-for-byte reproduction because EdgeTX embeds build date, build time, and
source location strings.

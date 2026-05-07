# Release Packages

This document describes how to build the local Windows release artifacts from a
source checkout.

## Flash-And-Run Package

Build the firmware-included Windows package from the repo root:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\publish-gx12-distribution.ps1
```

The script creates:

- `dist\Asukaflight-<version-token>-win-x64\`
- `dist\Asukaflight-<version-token>-win-x64.zip`

Preview versions use a short package token, for example
`0.9.0-preview.1` is packaged as `0.9.0-p1`.

The zip is flattened: opening it shows `Asukaflight.exe`, `profiles\`,
`runtime\`, `.gx12-ui\`, `firmware\`,
`README-FLASH-AND-RUN.txt`, and `MANIFEST-SHA256.txt` directly at the archive
root. It does not contain a same-name wrapper folder.

The package contains the self-contained WPF launcher, the native trainer
runtime, profiles, fixed UI sprites, random tooltip sprites, release notes, the
GPLv2 license file, the validated GX12 composite CDC/HID firmware under
`firmware\R2X-871.BIN`. It
intentionally omits `.pdb` symbol files; rebuild locally when debugging with
symbols is needed.

The package also includes untested TX16S/TX16S MK3 firmware candidates under
`firmware\TX16S-EXPERIMENTAL\`. These are experimental, not guaranteed, and
not the GX12 firmware. Read the packaged `README-TX16S-EXPERIMENTAL.txt` before
any TX16S flash attempt.

The matching firmware source is included in this source tree under
`firmware\edgetx-gx12-2.11.0`. Rebuild notes and the validated firmware hash
are in `docs\firmware-source.md`.

`tools\publish-gx12-release.ps1` remains as the internal portable-app publisher
used by the distribution script, but it is not the public release artifact.

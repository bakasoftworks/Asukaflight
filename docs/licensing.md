# Licensing

Asukaflight is distributed under the GNU General Public License, version 2
only. The root `LICENSE` file contains the GPLv2 text.

## Project Code

The Asukaflight host program, WPF launcher, scripts, tests, bundled profiles,
and project documentation are released as GPL-2.0-only.

## Firmware Code

The GX12 firmware source in `firmware\edgetx-gx12-2.11.0` is derived from
EdgeTX 2.11.0 and remains GPLv2. Keep the upstream EdgeTX notices and license
files intact when redistributing or modifying that tree.

The validated firmware binary is:

```text
firmware\R2X-871.BIN
SHA256 8717A5BE0DD1A536AC7F5718CCD3F50F4DA835B889DCCBF56EEB44AA19FED71A
```

## Third-Party Code

Third-party code inside the firmware tree keeps its upstream license files.
The native host build downloads additional dependencies through CMake
FetchContent, and the WPF app uses the .NET runtime and Windows APIs. Those
external components are not relicensed by this repository.

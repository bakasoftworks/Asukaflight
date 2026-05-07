# TX16S Experimental Firmware

The distribution package includes experimental TX16S firmware in
`firmware\TX16S-EXPERIMENTAL\`. These artifacts are for tester work only. They
are not the validated GX12 firmware and are not guaranteed.

## Artifacts

- TX16S / TX16S II candidate:
  `tx16s-asukaflight-composite-cdc-hid-resolution2x-2.11.0-EXPERIMENTAL-UNTESTED-EA8F1086.bin`
- TX16S III / MK3 UF2 candidate:
  `tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-8CC87AA0.uf2`
- TX16S III / MK3 raw BIN candidate:
  `tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-326BF504.bin`

The MK3 build was produced from the EdgeTX `TX16SMK3` target at source commit
`7d15aa34`, configured with `USB_SERIAL=ON`, `USBJ_EX=ON`,
`EdgeTX_SUPERBUILD=OFF`, and `CMAKE_BUILD_TYPE=Release`.

## Warnings

- Do not flash TX16S files to a GX12.
- Back up models, settings, and stock firmware before flashing.
- Keep the recovery firmware and process ready for the exact TX16S radio.
- First validation must be props off and simulator-only.
- Treat USB enumeration, trainer routing, failsafe behavior, F1 stop, F3
  freeze, and stale-trainer timeout as required checks before real RF testing.

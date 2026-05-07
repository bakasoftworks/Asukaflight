# Experimental TX16S Firmware

This folder contains untested TX16S/TX16S MK3 Asukaflight firmware candidates.
They are included for tester work only and are not guaranteed to boot,
enumerate over USB, or behave correctly on real hardware.

Do not flash these files to a GX12. Use the validated GX12 firmware artifact
for GX12 radios.

## Files

- `tx16s-asukaflight-composite-cdc-hid-resolution2x-2.11.0-EXPERIMENTAL-UNTESTED-EA8F1086.bin`
  - Target: TX16S / TX16S II EdgeTX 2.11 raw BIN candidate.
  - SHA256: `EA8F1086A3935672AEB24C6E26A13C3C7A7366F7956580C6DF86B094772E779B`
  - Size: 1,453,260 bytes.
- `tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-8CC87AA0.uf2`
  - Target: TX16S III / MK3 EdgeTX pre-3.0 UF2 candidate.
  - SHA256: `8CC87AA0FAE6970D318EA2531C8A170E74CD0EB390D824855A9BC6CE7697B8E2`
  - Size: 3,107,328 bytes.
- `tx16smk3-asukaflight-composite-cdc-hid-resolution2x-pre-3.0.0-EXPERIMENTAL-UNTESTED-326BF504.bin`
  - Target: TX16S III / MK3 EdgeTX pre-3.0 raw BIN candidate.
  - SHA256: `326BF504E49DDB0F88922E987DD566FB1F8CB8B42673C20672F091D3021CDCDD`
  - Size: 1,487,696 bytes.

## Tester Checklist

Before flashing, back up models, radio settings, and stock firmware. Keep a
known-good EdgeTX or RadioMaster recovery path ready for the exact radio model.

Initial validation should be props off:

1. Flash only the file that matches the exact radio target.
2. Confirm the radio boots and the screen, keys, touch, trims, storage, and
   internal module controls still work.
3. Select USB Joystick mode and confirm Windows enumerates both HID joystick and
   CDC serial interfaces.
4. Configure the Asukaflight trainer routing on a disposable test model.
5. Start the Asukaflight launcher and verify SBUS RX/s and mix sample/s move on
   the radio trainer page.
6. Verify F1 stop, F3 freeze, USB disconnect behavior, stale-trainer timeout,
   and physical throttle cut.
7. Test in a simulator before any real vehicle test.
8. Restore stock firmware immediately if boot, controls, USB enumeration, or
   trainer behavior is wrong.

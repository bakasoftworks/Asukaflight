# Architecture

This file summarizes the current dual-path architecture after the
2026-04-26/27 pivot and sim-path work.

## Mission

Mouse-driven control of a physical tinywhoop drone via the Radiomaster GX12.
VelociDrone remains the safe test bed. Real flight is in scope.

## Active Paths

### Real Flight Path

```text
PC GameInput mouse -> gx12mouse mapper -> USB-CDC SBUS -> GX12 EdgeTX trainer
                                                  |
                                                  v
GX12 mixer: trainer channels + local sticks/switches -> ELRS RF
```

The real-flight path uses the custom EdgeTX USB-VCP SBUS trainer patch. The PC
tool sends SBUS trainer frames over the auto-detected GX12 composite CDC port;
EdgeTX exposes those as trainer sources. Model mixes should use `TR1`/`TR2`
for mouse roll/pitch and keep local radio inputs for
throttle/yaw. Reticle-aim profiles also send trainer yaw; configure EdgeTX
trainer Rudder as `ADD` to layer that trainer yaw on top of the physical
left-stick yaw.

### VelociDrone Sim Path

```text
PC GameInput mouse + optional keyboard/Wooting input
        -> SBUS over composite CDC -> GX12 EdgeTX trainer/mixer
                                      |
                                      v
                         GX12 USB HID joystick -> VelociDrone
```

The composite firmware path keeps the GX12 in USB Joystick mode while exposing
both CDC and HID on the same cable. The sender feeds SBUS trainer frames over
CDC; EdgeTX mixes the trainer channels with local sticks according to the
trainer setup; VelociDrone reads the result as the physical
`Radiomaster GX12 Joystick`.

The older PC-side virtual-controller sim path has been removed. Simulator
tuning and real flight now share the same composite trainer path.

## Current Sim Input Sources

Composite firmware plus Microsoft GameInput solved the VelociDrone foreground
fight. `--trainer-profile` uses GameInput background mouse capture and sends the
result over the composite CDC port while VelociDrone keeps foreground and reads
the GX12 HID joystick.

Left-stick trainer channels can come from digital GameInput keys, the Wooting
Analog SDK, a second GameInput mouse, or the right mouse side buttons plus
vertical scroll wheel. Wooting support is
runtime-loaded so `gx12mouse.exe` still starts on machines without the SDK. In
Wootility, use no gamepad output for this project so VelociDrone sees only
`Radiomaster GX12 Joystick`. The second-mouse path uses GameInput per-device
root tokens or `left = "auto"`: one mouse can feed ch1/ch2 roll/pitch while
another mouse feeds ch3/ch4 throttle/yaw.

## GX12 HID Report Layout

Measured on a GX12 in USB Joystick mode:

- VID/PID: `0x1209/0x4F54`
- Report rate: about `500 Hz`
- Report size: `20` bytes
- Bytes `0..2`: 24 button bits
- Bytes `3..18`: 8 little-endian `uint16` analog channels
- Channel center: `1024`; decoded channel value is `raw - 1024`
- Byte `19`: padding/unused
- Observed mapping: ch1 roll, ch2 pitch, ch3 throttle, ch4 rudder, ch5..ch8 switches/aux

## PC Tool

Important commands:

- `--trainer-profile FILE [sec]`: USB-CDC/SBUS trainer sender.
- `--tune FILE [sec]`: guided trainer sender.
- `--mouse-devices-gameinput [sec]`: GameInput per-mouse token/delta diagnostic.
- `--mouse-left-dry-run FILE [sec]`: dry-run right/left mouse bindings without serial.
- `--elastic-preview FILE`: ASCII graph/table for right-stick elastic return modes.
  Launcher V3's Right Stick preview extends this into an integrated stick
  response view covering expo/deadband/smoothing, axis toggles, and enabled
  return mechanisms.
- `--gimbal-preview FILE`: deterministic preview for the experimental right-stick
  shaping stack: dynamic virtual gimbal, speed-adaptive input gain, and radial
  roll/pitch gates.
- `--wooting-rate [sec] [keys...]`: Wooting Analog SDK depth diagnostic.
- `--hid-list` / `--hid-rate`: generic HID diagnostics.
- `--gx12-hid-capture [sec] [csv_path]`: decoded GX12 joystick HID channel
  capture for raw-axis granularity checks.

## Performance Ceilings

Measured or relevant limits:

| Stage | Rate / latency |
|---|---|
| PC mouse capture (foreground Raw Input) | 1-8 kHz, <1 ms |
| PC mapper baseline | 1000 Hz |
| USB-CDC SBUS decode on GX12 | stable at 8000 Hz tested |
| EdgeTX trainer-source/mixer sampling | about 1000 Hz (`RX 8000`, `Mix 1000`) |
| GX12 USB Joystick HID reports | about 500 Hz measured |
| ExpressLRS RF | up to configured RF packet rate, likely 500 Hz max here |

Sending SBUS above 1000 Hz can reduce upstream sample age before the mixer read,
but cannot raise the mixer or ELRS packet rate.

## Safety Notes

For standard roll/pitch real-flight testing, `SYS -> Trainer` must replace
Ail/Ele only. Thr/Rud trainer modes should be `OFF`, with model mixes keeping
local throttle/yaw on the physical GX12 left stick. For reticle-aim profiles that
send yaw, keep Thr=`OFF` and set Rud=`ADD` so yaw is physical stick plus mouse
trainer yaw. Props-off bench checks come before any armed test.

# Magnetometer Heading Test

## What this is

A minimal bench tool: continuously takes single-shot IIS2MDC magnetometer readings and computes a compass heading, to validate the heading formula and axis mapping used in `9_Mag_ble_mioty_tracker` before trusting it on a real mioty uplink.

No radio, no RTC, no Stop mode — the MCU never sleeps, so Live Expressions update continuously in real time with no `HAL_DBGMCU_EnableDBGStopMode()` workaround needed.

## How heading is calculated

```c
int16_t cx = mx - dbg_offset_x;   // hard-iron corrected
int16_t cy = my - dbg_offset_y;
float heading_deg_f = atan2f((float)cy, (float)cx) * 57.2957795f; // 180/pi
if (heading_deg_f < 0) heading_deg_f += 360.0f;
```

A 2D compass bearing from the X/Y field components, corrected for hard-iron offset (see below) — **magnetic north, no tilt compensation** (no accelerometer on this board, so this is only meaningful with the board roughly level). Which physical direction reads as 0° depends on the magnetometer's mounting orientation on the PCB — that's exactly what this tool is for confirming empirically.

## Hard-iron calibration

Without correction, a constant offset in the raw X/Y readings — from nearby ferrous material, magnetized PCB traces, or the sensor's own inherent bias — shifts the circle traced by `(mx, my)` as the board rotates so it's no longer centered on the origin. `atan2()` measures the angle *from the origin*, so if that circle doesn't enclose the origin, the heading gets stuck in a limited arc and can never reach certain angles (e.g. never hitting 0°/360° no matter how you rotate) — this is exactly the symptom that motivated adding this routine.

`RunHardIronCalibration()` runs once at boot, right after the WHO_AM_I check: for 15 seconds (`CALIBRATION_DURATION_MS`), it samples every 100ms and tracks the min/max of `mx` and `my` separately, then computes `offset = (min + max) / 2` per axis. Every heading calculation afterward subtracts these offsets first, re-centering the circle on the origin so the full 0-359° range becomes reachable.

**You must rotate the board through a full 360° turn during this window** (roughly level) — the LED is solid ON the whole time as the "still calibrating, keep rotating" signal, then goes out and blinks 5 times when it's done and switches to normal heading display.

## LED patterns

- **2 blinks** (boot) = magnetometer WHO_AM_I OK
- **8 blinks** (boot) = WHO_AM_I failed — halts in a slow 1Hz toggle, check wiring
- **Solid ON for 15s** (boot, after WHO_AM_I) = hard-iron calibration running — rotate the board through a full turn now
- **5 blinks** (boot) = calibration complete, offsets found, entering normal operation
- **Toggle every 500ms** (main loop) = heartbeat, proves the read loop is alive — this does NOT encode the heading itself (0-359° isn't practical to blink out), use Live Expressions for the actual number

## Live Expressions

| Variable | Meaning |
| :--- | :--- |
| `dbg_heading_deg` | Computed, hard-iron-corrected compass heading, 0-359, magnetic north |
| `dbg_offset_x`, `dbg_offset_y` | Hard-iron offsets found during boot calibration — subtracted from every raw reading before computing heading |
| `dbg_mag_x`, `dbg_mag_y`, `dbg_mag_z` | Last raw (uncorrected) magnetometer sample |
| `dbg_mag_read_ok` | Last `IIS2MDC_ReadXYZ()` result (1 = success) |

No `HAL_DBGMCU_EnableDBGStopMode()` needed — this project never enters Stop mode, so the debug clock never gets cut and Live Expressions always update live.

## Test procedure

1. Flash. Boot: 2 blinks = magnetometer OK.
2. **Calibration**: as soon as the LED goes solid, rotate the board slowly and evenly through a full 360° turn, roughly level, finishing before the 15-second window ends. LED goes out and blinks 5 times when done — check `dbg_offset_x`/`dbg_offset_y` are non-zero (a zero/zero offset most likely means the board didn't get rotated through a full turn, or was rotated too fast/slow relative to the window).
3. Watch `dbg_heading_deg` with the board stationary — should hold roughly steady (small jitter from sensor noise is normal).
4. Rotate the board slowly through another full turn. Confirm `dbg_heading_deg` now reaches the FULL 0-359 range, wrapping cleanly — if it's still stuck in a limited arc, the calibration turn in step 2 likely wasn't a full/clean 360°, so re-flash (or add a reset) and redo it more carefully.
5. Note which physical direction the board points when `dbg_heading_deg` reads 0 — that's your empirical "0° = this way" reference for the mounted sensor, needed to interpret headings sent by `9_Mag_ble_mioty_tracker` on a real deployment.
6. Tilt the board off level and watch `dbg_heading_deg` drift — this demonstrates the no-tilt-compensation limitation directly; the heading is only trustworthy near-level.

## Build and flash

STM32CubeIDE, launch configuration `magnetometer_heading_test.launch`, expected ELF `Debug/magnetometer_heading_test.elf`.

## Project boundary

Use this project for: validating the heading formula/axis mapping in isolation, with instant Live Expressions feedback (no Stop-mode debug-clock complications).

Use `9_Mag_ble_mioty_tracker` for: the production tracker that actually sends this heading in every mioty uplink.

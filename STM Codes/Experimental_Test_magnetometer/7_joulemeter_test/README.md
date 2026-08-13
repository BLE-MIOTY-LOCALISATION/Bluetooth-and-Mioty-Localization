# 7_ble_mioty_FMDN_tracker + Magnetometer Joulemeter Validation

## What this is

An exact copy of `7_ble_mioty_FMDN_tracker` -- the proven, joulemeter-validated ~5uA Stop-mode design (see `power_test_setup.md`, carried over unchanged) -- with one addition: a marker blink + single-shot IIS2MDC magnetometer read inserted into the main loop after each FMDN beacon send.

Nothing about 7_'s BLE TX or Stop-mode routine was changed. The goal is to validate the magnetometer reading works correctly *while riding along on* 7_'s already-proven power profile, instead of re-deriving Stop-mode leakage fixes from scratch (which is what the now-deleted `single_shot_joulemeter_test` attempted -- it got from ~1.15mA down to ~0.033mA through several iterations, but never matched 7_'s ~0.005mA floor).

## What changed from 7_ble_mioty_FMDN_tracker

- **I2C2 added**: `MX_I2C2_Init()`, `HAL_I2C_MspInit`/`MspDeInit` for I2C2 (PB10/PB11), and the full IIS2MDC single-shot driver (`IIS2MDC_ReadRegister`, `IIS2MDC_WriteRegister`, `IIS2MDC_CheckWhoAmI`, `IIS2MDC_ReadXYZ`) -- none of this existed in 7_, which never used the magnetometer.
- **`STM32_EnterStopMode()`**: now calls `HAL_I2C_DeInit(&hi2c2)` before the existing PB10/PB11-to-Analog clamp (that clamp already existed in 7_'s code, written *anticipating* IIS2MDC use, but never actually exercised since I2C wasn't initialized). After waking, `MX_I2C2_Init()` is called alongside the existing `MX_SPI1_Init()` re-init, so the bus is ready for the next read.
- **Main loop**: after `SX1280_SendFMDNBeacon()`, added a fixed 100ms LED blink, a 100ms gap, then `IIS2MDC_ReadXYZ()`. Everything else in the loop (the FMDN beacon send itself, `STM32_EnterStopMode()`) is untouched.
- **Boot**: added a WHO_AM_I check (`IIS2MDC_CheckWhoAmI()`) right after the existing SX1280 diagnostic blinks -- 2 blinks = magnetometer OK, 8 blinks = I2C/wiring problem. Boot continues either way.
- **Boot, I2C bus recovery**: `I2C2_BusRecovery()` runs before `MX_I2C2_Init()` claims PB10/PB11. The magnetometer's VDD is hardwired to 3.3V on this board (no GPIO power-gating available), so it never loses power across a reflash -- only the MCU resets. If any earlier session's I2C transaction got interrupted mid-byte, the sensor can be left holding SDA low indefinitely, and the module's own pull-up sources continuous current through that stuck line -- a candidate explanation for a steady current floor that persisted even after forcing idle mode (see below) and even though WHO_AM_I still passes (a stuck bus *between* transactions doesn't necessarily block a *new* one from succeeding). Standard recovery procedure: manually clock SCL up to 9 times to let a wedged slave finish and release SDA, then issue a STOP condition.
- **Boot, forced idle mode + read-back proof**: `IIS2MDC_WriteRegister(IIS2MDC_CFG_REG_A, IIS2MDC_CFG_IDLE_LP)` is the very first thing done in `main()`, before the SX1280 is even touched -- followed immediately by reading `CFG_REG_A` back and blinking `MD[1:0]+1` so the result is visible even on a fully standalone run with no debugger attached (1 blink = MD=00/continuous -- write did NOT take effect; 3-4 blinks = MD=10/11/idle -- write succeeded).

  **Why this exists**: with the magnetometer entirely disconnected, standby current measured ~0.003mA; with it present, ~0.030mA -- and per the IIS2MDC datasheet's electrical characteristics table, that ~28uA gap matches `Idd_LP` (continuous low-power mode @ 10Hz, typ. 25uA) almost exactly, not `Idd_PD` (power-down/idle, typ. 1.5uA). Critically, the *same* ~0.030mA was also measured running completely unmodified `FMDN_tracker` firmware, which never initializes I2C2 or sends the sensor a single command -- proving the sensor powers up in continuous mode **by default**, with zero software involvement. Since VDD is hardwired (confirmed -- no GPIO power-gating available on this board) and nothing before this project ever explicitly commanded power-down, the sensor may have simply never been told to stop converting. The forced-idle write plus read-back blink is there to prove definitively whether that write actually lands on real hardware, rather than assuming it does.

## Per-cycle sequence

Once every 10 seconds (`RTC_SetAlarm_10s`, unchanged from 7_):

1. **FMDN BLE beacon** on channels 37/38/39 (~35ms TX burst, ~15mA) -- unchanged from 7_.
2. **Marker blink**: 100ms LED on, then 100ms gap (LED off) -- new landmark so the magnetometer read is visually distinguishable from the TX burst and the existing internal 10ms post-sleep LED flash.
3. **Magnetometer single-shot read** (`IIS2MDC_ReadXYZ`) -- new, isolated current draw, no LED overlapping it.
4. **`STM32_EnterStopMode()`** -- unchanged from 7_: puts SX1280 to sleep, flashes LED for 10ms, clamps every unused Port A/B pin (including I2C2's, now actually de-inited first) to Analog/pulldown, sets the RTC alarm, enters Stop mode, and re-inits everything on wake.

## Reading the joulemeter log

Per `power_test_setup.md`'s existing waveform description, now with one more phase inserted between the TX burst's trailing LED flash and the long Stop-mode floor:

1. ~15mA TX burst (~35ms)
2. ~3mA, 10ms LED flash (already part of 7_'s `STM32_EnterStopMode`, happens right before Stop mode is entered -- i.e. at the *end* of the cycle, not right after TX)
3. **New**: 100ms marker blink + 100ms gap + magnetometer read, happening *before* step 2, right after the TX burst
4. Stop-mode floor (~5-15uA per 7_'s validated numbers) for the remainder of the 10s cycle

## Debugging / measurement setup

Unchanged from `power_test_setup.md` -- summary:

- Comment out `HAL_DBGMCU_EnableDBGStopMode()` in `main()` before flashing (already commented out here, matching 7_).
- Power from the joulemeter directly (3.0V, 50mA current limit to simulate a CR2032), **fully disconnect the ST-Link** (3.3V, GND, SWDIO, SWCLK) -- a connected debugger back-powers the board and ruins the reading.
- Sampling rate >= 1 kSps to resolve the TX burst and the new marker blink/read phase.

## Debug-only state (Live Expressions, sanity-check runs only)

| Variable | Meaning |
| :--- | :--- |
| `dbg_mag_read_ok` | Last `IIS2MDC_ReadXYZ()` result (1 = success) |
| `dbg_mag_x`, `dbg_mag_y`, `dbg_mag_z` | Last raw magnetometer sample |

Requires `HAL_DBGMCU_EnableDBGStopMode()` re-enabled to read during Stop mode -- re-disable before the real joulemeter run.

## Build and flash

STM32CubeIDE, launch configuration:

```text
7_joulemeter_test.launch
```

Expected ELF:

```text
Debug/7_joulemeter_test.elf
```

## Project boundary

Use this project for:

- Validating the IIS2MDC magnetometer read works correctly without disturbing 7_'s already-proven ~5uA Stop-mode power profile.

Use `7_ble_mioty_FMDN_tracker` for:

- The unmodified BLE/FMDN-only baseline this project is built from.

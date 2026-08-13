# 9_Mag_ble_mioty_tracker

Motion-gated BLE + mioty asset tracker. Built from `8_ble_mioty_tracker` (real SX1280 FMDN beacon TX, real mioty TS-UNB uplink, proven Stop-mode GPIO-leakage prevention) with a single-shot IIS2MDC magnetometer and an independent per-radio scheduler added on top — the algorithm validated on the bench in `Experimental_Test_magnetometer/ble_single_shot_scheduler_test`.

## Hardware

* **MCU**: STM32F103CBU6 (Cortex-M3, 128 KB flash, 20 KB SRAM)
* **RF transceiver**: Semtech SX1280IMLTRT (2.4 GHz, BLE/mioty over SPI1)
* **Magnetometer**: ST IIS2MDC (I2C2, single-shot low-power mode only)
* **Clocks**: 8 MHz HSE (PLL off — required for mioty's DWT-cycle-counter symbol timing), LSE 32.768 kHz driving the RTC/Stop-mode wakeup

### Pinout

| Signal | Pin | Notes |
| :--- | :--- | :--- |
| SPI1_SCK | PA5 | |
| SPI1_MISO | PA6 | |
| SPI1_MOSI | PA7 | |
| LORA_NSS | PA4 | Active low chip select |
| LORA_NRST | PA3 | Active low hardware reset |
| LORA_BUSY | PB15 | Input; HIGH while the SX1280 is busy or asleep |
| LORA_DIO1 | PB0 | Input; mapped to the radio's TxDone IRQ |
| I2C2_SCL / I2C2_SDA | PB10 / PB11 | IIS2MDC magnetometer |
| LED | PC13 | Status LED |

See `8_ble_mioty_tracker/README.md` for the Stop-mode GPIO-clamping sequence and the RTC/SX1280 bring-up issues resolved before this project was forked — both carry over unchanged here.

## Algorithm

Every wake:

1. **BLE beacon sends, unconditionally.** Motion never gates *whether* BLE transmits, only how *often* it wakes to do so.
2. **Single-shot magnetometer read**, piggybacked on that same wake — no continuous mode, no separate wake source. A delta vs. the previous sample (`|dx|+|dy|+|dz|` past `motion_delta_threshold`) marks a motion event and resets the 60-second active window (`ACTIVE_HOLD_SECONDS`).
3. **BLE's own wake period** is picked from the active/idle state: 2s while active, 30s while idle.
4. **mioty tracks its own elapsed time independently** — accumulated every cycle, checked against its own target (60s active / 900s idle), and reset only by its own send, never by motion. This is what keeps mioty from being starved by sustained motion, and keeps BLE's cadence from being tied to mioty's much slower one.
5. **Full-iteration elapsed-time measurement**: real wall-clock time (including a mioty send's ~5.5s blocking TX) is what drives the active-window decay and mioty's accumulator, not just the RTC sleep duration — so a real uplink never distorts the schedule's sense of time.

## Schedule

| | Active (recent motion) | Idle |
| :--- | :--- | :--- |
| BLE wake | 2s | 30s |
| mioty target | 60s | 900s (15 min) |

Active window: 60 seconds since the last confirmed motion event.

## LED patterns

**Boot:**
- 3 fast blinks → 2s pause → 1 long blink — firmware alive, radio init complete
- 2 blinks / 8 blinks — magnetometer WHO_AM_I OK / fail
- 1-4 blinks — magnetometer idle-mode readback confirmation (`MD[1:0]+1`; 3-4 = idle confirmed)

**Main loop** — same meaning in both active and idle cadence, only the frequency changes. These are the only LED activity per cycle — the leftover 10ms post-TX flash inherited from `8_`'s `STM32_EnterStopMode()` (fired right before every sleep, regardless of anything else) was removed since it showed up as an extra unlabeled blink shortly after the `1`-blink BLE indicator on every single cycle, easy to mistake for a second, distinct event:
- **1 blink** = BLE cycle (every wake)
- **2 blinks** = mioty cycle (uplink sent)
- **3 blinks** = motion detected

## Live Expressions

| Variable | Meaning |
| :--- | :--- |
| `dbg_active_hold_s` | Seconds remaining in the active window; `0` = idle |
| `dbg_ble_period_s` | Current BLE wake period — `2` active, `30` idle |
| `dbg_mioty_target_s` | Current mioty target — `60` active, `900` idle |
| `dbg_mag_delta` | Last motion-delta magnitude vs. `motion_delta_threshold` |
| `dbg_ble_send_count` | Total BLE beacons sent (every wake, unconditional) |
| `dbg_mioty_send_count` | Real mioty sends so far — climbing during sustained motion proves no starvation |
| `dbg_heading_deg` | Computed compass heading, 0-359, magnetic north — see "Magnetometer heading" below |

Requires `HAL_DBGMCU_EnableDBGStopMode()` (commented out in `main()`) temporarily re-enabled to update during Stop mode — re-disable before any power measurement.

## Magnetometer heading in the mioty payload

Every wake, right after the magnetometer read used for motion detection, a 2D compass bearing is computed from the same X/Y sample: `mx`/`my` are first hard-iron corrected using this unit's fixed `MAG_OFFSET_X`/`MAG_OFFSET_Y` constants (see defines near the top of `main.cpp`), then `atan2f(hy, hx)` converted to degrees and normalized to 0-359 (`dbg_heading_deg`). This is **magnetic north, no tilt compensation** — there's no accelerometer on this board, so the reading is only meaningful with the tracker roughly level.

The `MAG_OFFSET_X`/`Y` constants were found using `Experimental_Test_magnetometer/magnetometer_heading_test`'s boot-time rotate-and-record calibration routine (rotate through a full 360° turn, `offset = (min+max)/2` per axis) — without this correction, a hard-iron bias in the raw readings can shift the traced circle off-center enough that heading gets stuck in a limited arc and never reaches certain angles at all (this happened during testing: heading was stuck above 200°, never near 0°/360°, until calibrated). These offsets are **fixed per physical unit** — re-run that calibration tool and update the two constants if this firmware is flashed onto a different board, or if this board's magnetic environment changes (different mounting, nearby ferrous material added).

Which physical direction "0°" corresponds to depends on the magnetometer's mounting orientation — verify empirically by rotating the board and watching `dbg_heading_deg`, not just by reading the datasheet's axis diagram.

### Re-calibrating the hard-iron offsets

Current values on this unit: `MAG_OFFSET_X = -317`, `MAG_OFFSET_Y = -208` (confirmed working — heading sweeps the full 0-359° range after applying them).

To re-derive these (new board, or this board's magnetic environment changed):

1. Flash `Experimental_Test_magnetometer/magnetometer_heading_test` onto the board instead — it's a standalone bench tool with its own boot-time calibration routine, no radio/RTC involved.
2. Boot it: 2 blinks = magnetometer OK, then the LED goes **solid ON for 15 seconds** — rotate the board slowly and evenly through a full 360° turn during that window.
3. LED blinks 5 times when calibration completes. Read `dbg_offset_x` and `dbg_offset_y` from Live Expressions (no `HAL_DBGMCU_EnableDBGStopMode()` needed there — that project never sleeps).
4. Sanity-check: rotate the board again and confirm `dbg_heading_deg` now sweeps the full 0-359° range cleanly. If it's still stuck in a limited arc, the calibration turn in step 2 likely wasn't a clean full 360° — reflash and redo it.
5. Back in `9_Mag_ble_mioty_tracker`, update the two `#define MAG_OFFSET_X` / `MAG_OFFSET_Y` values (near the top of `main.cpp`, alongside the schedule defines) to whatever `dbg_offset_x`/`dbg_offset_y` read — note the sign: a negative value is expected and correct if the axis's raw readings sat mostly below zero, the subtraction (`mx - MAG_OFFSET_X`) handles either sign automatically.

The heading at the time of send is included in every mioty uplink:

| Bytes | Field |
| :--- | :--- |
| 0-3 | `packet_counter`, big-endian uint32 |
| 4-5 | `heading_deg`, big-endian uint16, 0-359, magnetic north, hard-iron corrected |
| 6-19 | reserved, zero-filled |

Google's FMDN/BLE side can't carry this — Google's scanner only recognizes its own fixed frame format — so heading only ever goes out over mioty.

## What changed from `8_ble_mioty_tracker`

- Magnetometer support added: I2C2 HAL driver, `hi2c2` handle, `I2C2_BusRecovery()`, IIS2MDC single-shot driver, boot-time WHO_AM_I + forced-idle diagnostics.
- `STM32_EnterStopMode()`: added `HAL_I2C_DeInit(&hi2c2)`/`MX_I2C2_Init()` around its existing (previously unused) PB10/PB11 Analog clamp. Everything else in this function is untouched.
- Scheduler replaced: the flat `mioty_counter < MIOTY_WAKEUP_COUNT` cycle counter is gone, replaced by the motion-gated independent elapsed-time scheduler above.
- `sendMiotyPacket()`'s internal 3-blink (previously "TX about to fire") removed — it collided with the new "3 = motion detected" meaning; the main loop now blinks 2 after a send completes instead.
- New minimal debug variable set for verifying scheduling correctness (see Live Expressions above) — not carrying forward the sleep-timing-bug-hunting scaffolding from the sandbox project, since `8_`'s Stop-mode code is already production-proven.

## Build and flash

**Prerequisites**: STM32CubeIDE, an ST-Link (or compatible SWD probe) connected to the board.

1. In STM32CubeIDE: `File → Open Projects from File System...`, select this `9_Mag_ble_mioty_tracker/` folder, import it.
2. Build: select the project in the Project Explorer, `Project → Build Project` (or the hammer icon). Output ELF lands at `Debug/9_Mag_ble_mioty_tracker.elf`.
3. Flash: `Run → Debug` (or the bug icon) using the included launch configuration `9_Mag_ble_mioty_tracker.launch`, which targets the ST-Link and this ELF. This also opens a debug session — for a bench test you generally want it running rather than halted at `main`, so let it run once it stops at the entry breakpoint.
4. For Live Expressions during Stop mode, temporarily uncomment `HAL_DBGMCU_EnableDBGStopMode()` in `main()`, reflash, and add the variables from the table above via the Live Expressions view. Re-comment it and reflash before any real power measurement (see `power_test_setup.md`-style guidance in the sibling `Experimental_Test_magnetometer` projects for why).

## Project boundary

Use this project for: the production motion-gated tracker.

Use `8_ble_mioty_tracker` for: the unmodified real-radio baseline this project is built from (reference/fallback, untouched).

Use `Experimental_Test_magnetometer/ble_single_shot_scheduler_test` for: where this scheduling algorithm was originally validated on the bench (with real BLE TX but a simulated mioty send).

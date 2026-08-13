# BLE Single-Shot Magnetometer + Independent mioty-Sim Scheduler

## What this is

The motion-gated scheduler, built on top of `7_joulemeter_test`'s already hardware-proven ~5µA Stop-mode design (real SX1280 FMDN beacon TX, real single-shot IIS2MDC magnetometer read, real I2C bus recovery + forced-idle boot diagnostic) instead of a from-scratch rewrite.

An earlier attempt (`single_shot_independent_scheduler_test`, now deleted) built the same scheduler logic in a from-scratch sandbox with no real radio — it turned out to have a genuine Stop-mode sleep-timing bug (blinking roughly once a second regardless of the configured period) that consumed a lot of debugging time without being conclusively resolved. Rather than keep chasing it, this project starts over on code that's already known to sleep correctly on real hardware, and layers only the scheduler on top.

## What changed from `7_joulemeter_test`

Nothing about the proven BLE TX, magnetometer driver, or `STM32_EnterStopMode()`'s GPIO leakage-prevention was touched. Only:

- **`STM32_EnterStopMode(void)` → `STM32_EnterStopMode(uint32_t period_s)`**, and `RTC_SetAlarm_10s()` → `RTC_SetAlarm_Seconds(uint32_t seconds)`. Was a fixed 10s cycle; now the scheduler picks 2s (active) or 30s (idle) per cycle.
- **New `RTC_ReadCounter()` helper**, used both inside the alarm-setting code and by the main loop for full-iteration elapsed-time measurement.
- **Motion detection**: the magnetometer read (already happening every cycle in `7_joulemeter_test`, just for validation) now feeds a delta check (`|dx|+|dy|+|dz|` vs. the previous sample, threshold = live-tunable `motion_delta_threshold`). Crossing it resets `active_hold_s` to `ACTIVE_HOLD_SECONDS` (60s — deliberately equal to `MIOTY_ACTIVE_S`, not just "long enough": `time_since_mioty_s` is never reset by motion, so a single motion event only *can* produce a mioty send if the active window lasts at least as long as mioty's own target).
- **mioty simulation**: no real mioty hardware exists on this board, so it's an independent elapsed-time accumulator (`time_since_mioty_s`) checked against its own target (`MIOTY_ACTIVE_S`=60 / `MIOTY_IDLE_S`=900) — never reset by motion, only by its own "send". A ~5.5s blocking `HAL_Delay` stands in for the real `sendMiotyPacket()` TX time when it "sends".
- **Full-iteration elapsed-time measurement**: `active_hold_s` decay and `time_since_mioty_s` accumulation both use an RTC-counter delta spanning the *whole* loop iteration (beacon TX + magnetometer read + possible mioty-sim block + sleep), not just the sleep portion — so the simulated ~5.5s mioty block can never silently vanish from the accounting.

FMDN beacon TX is **unconditional every wake** — motion never gates whether BLE sends, only how often it wakes to do so (2s vs 30s). This matches real tracker behavior; it's different from an earlier (also now-deleted) sandbox design that mistakenly gated the BLE-send debug counter on motion state.

## Per-cycle sequence

1. **FMDN BLE beacon** on channels 37/38/39 — every wake, unconditional.
2. **Single-shot magnetometer read**, delta-checked against the previous sample — may set `active_hold_s = 60`.
3. **Pick this cycle's BLE period** (2s if `active_hold_s > 0`, else 30s) **and mioty target** (60s active / 900s idle) from the (possibly just-updated) motion state.
4. **mioty-sim check**: if `time_since_mioty_s >= mioty_target_s`, "send" (2-blink LED, ~5.5s blocking delay), reset to 0.
5. **`STM32_EnterStopMode(ble_period_s)`** — same proven GPIO clamp/Stop-mode routine as `7_joulemeter_test`, now sleeping for a variable period instead of a fixed 10s.
6. **Full-iteration elapsed measurement** — decays `active_hold_s`, accumulates `time_since_mioty_s`, for use next cycle.

## Live Expressions

| Variable | Meaning |
| :--- | :--- |
| `dbg_active_hold_s` | Seconds remaining in the active window; `0` = idle |
| `dbg_ble_period_s` | RTC/BLE wake period in use — should read `2` while active, `30` while idle |
| `dbg_mioty_target_s` | mioty target in use — should read `60` while active, `900` while idle |
| `dbg_mag_delta` | `\|dx\|+\|dy\|+\|dz\|` vs. the previous single-shot sample |
| `dbg_mioty_send_count` | **The proof variable.** Must keep climbing on its own ~60s schedule during sustained motion, never stall |
| `dbg_alarm_fired_count` | Increments only inside `RTC_Alarm_IRQHandler`. Sleep-timing sanity check carried over from the debugging on the deleted sandbox — should track roughly 1 per wake |
| `dbg_iter_elapsed_s` | Raw measured seconds for the last full iteration. Should track `ble_period_s` closely — if it's stuck near `0`, sleep isn't real |

`dbg_ble_send_count`, `dbg_time_since_mioty_s`, `dbg_motion_event_count`, `dbg_mag_read_ok`, `dbg_mag_x/y/z`, `dbg_cfg_reg_a_readback` are also available if more detail is wanted.

Requires `HAL_DBGMCU_EnableDBGStopMode()` (currently commented out, ~line 943) temporarily re-enabled to read Live Expressions during Stop mode — re-disable before any real joulemeter measurement.

## LED patterns

**Boot** (unchanged from `7_joulemeter_test`):
- **9, 6, 3, 1 blinks** = `SX1280_VerifyInit()` diagnostic register readback
- **1 blink** = SX1280 alive
- **2 blinks** = magnetometer WHO_AM_I OK (8 = fail)
- **1-4 blinks** = `CFG_REG_A` idle-mode readback confirmation (`MD[1:0]+1`; 3-4 = idle confirmed)

**Main loop** — the numbers indicate WHICH RADIO/event acted, not the wake speed; same meaning in both idle and active cadence. These are the ONLY LED activity per cycle now — the old internal 10ms post-TX flash inside `STM32_EnterStopMode()` (inherited from `7_joulemeter_test`, fired right before every sleep regardless of anything else) has been removed since it was an extra, uncounted blink sitting right next to the magnetometer-read step and muddying the 1/2/3 scheme:
- **1 blink** = BLE cycle (every wake, unconditional — right after `SX1280_SendFMDNBeacon()`)
- **2 blinks** = mioty cycle (mioty-sim "sent" — every ~60s while active, ~15min while idle)
- **3 blinks** = motion/orientation change detected (the event that resets `active_hold_s` and triggers the switch to the fast 2s/60s cadence)

So a fully stationary board shows: `1` blink every 30s, `2` blinks roughly every 15 minutes. A tilt shows a `3`, then the `1` blinks speed up to every 2s for up to 60 seconds. Because the active window (60s) is exactly as long as mioty's active target (60s), a single tilt is right on the boundary — it *can* produce a `2` blink before the window closes, but isn't guaranteed to (depends on how much time had already built up toward the target beforehand, and on 1-second RTC granularity). Repeated tilts a few seconds apart make it far more reliable, since each one re-arms the full 60s window.

## Test procedure

1. Flash. Boot: `9,6,3,1` (SX1280 verify) → `1` blink (SX1280 alive) → `2` blinks (magnetometer WHO_AM_I OK) → `1-4` blinks (idle-mode readback). If any of this is missing or different, something regressed from `7_joulemeter_test`'s proven boot sequence — stop and compare before going further.
2. Leave the board stationary and just watch the LED (no debugger needed): confirm a single `1` blink every 30s, with no `3`s appearing. This alone proves BLE's own idle cadence — the `2`-blink mioty check is slow to observe this way (~15 min), trust the shorter active-window case in step 4 instead.
3. Tilt the board once. Confirm a `3` appears, then the `1` blinks speed up to roughly every 2s. With `HAL_DBGMCU_EnableDBGStopMode()` temporarily re-enabled, also confirm `dbg_mag_delta` spiked past `motion_delta_threshold`, `dbg_active_hold_s` jumped to 60, `dbg_ble_period_s` dropped to `2`, and `dbg_mioty_target_s` dropped to `60`. A `2` blink may or may not appear within this single window (see the LED patterns note above on why) — don't treat one missing `2` as a failure by itself.
4. **Sustained continuous motion:** tilt/tap at least once every ~5-10s for at least 90 seconds, so each tilt re-arms the 60s active window before the previous one expires. Watch for `2` blinks — must keep appearing roughly every 60s throughout, never stalling just because `3`s (motion) keep appearing and resetting `dbg_active_hold_s`. `dbg_mioty_send_count` climbing in Live Expressions is the same proof, just without needing to watch the LED.
5. Re-disable `HAL_DBGMCU_EnableDBGStopMode()` before any real power measurement, per `power_test_setup.md`.

## Build and flash

STM32CubeIDE, launch configuration:

```text
ble_single_shot_scheduler_test.launch
```

Expected ELF:

```text
Debug/ble_single_shot_scheduler_test.elf
```

## Project boundary

Use this project for:

- Validating the full motion-gated scheduler (single-shot magnetometer detection + independent BLE/mioty scheduling, real 2s/30s/60s/900s targets) on top of already hardware-proven sleep/TX code.

Use `7_joulemeter_test` for:

- The proven single-shot magnetometer + fixed-10s-cycle baseline this project is built from.

Use `7_ble_mioty_FMDN_tracker` for:

- The unmodified BLE/FMDN-only baseline underneath that.

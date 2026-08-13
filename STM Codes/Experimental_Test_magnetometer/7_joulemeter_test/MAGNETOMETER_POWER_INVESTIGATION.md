# Magnetometer Joulemeter Investigation — Findings Log

Working notes from validating the IIS2MDC magnetometer's single-shot power behavior on top of `7_ble_mioty_FMDN_tracker`'s proven ~5µA Stop-mode design. Kept for future reference — covers the full debugging path, root causes found, and what's still open.

## 1. Background and goal

`7_ble_mioty_FMDN_tracker` (referred to here as "7_") is a validated ultra-low-power BLE/FMDN tracker design, joulemeter-confirmed at ~5µA Stop-mode standby current (see `power_test_setup.md`). The goal of this test line was to add the IIS2MDC magnetometer's single-shot read into that proven design — validating the magnetometer reading works correctly while riding on 7_'s already-proven power profile, rather than re-deriving Stop-mode power optimizations from scratch.

Two project attempts:

1. **`single_shot_joulemeter_test`** (scrapped, no longer in the repo) — an initial from-scratch minimal test that tried to build up Stop-mode power optimization independently. Useful lessons learned (see Section 2) but never matched 7_'s proven floor, so it was abandoned in favor of starting from 7_'s known-good code directly.
2. **`7_joulemeter_test`** (current) — an exact copy of `7_ble_mioty_FMDN_tracker`, with I2C2 + IIS2MDC support added, and a magnetometer read inserted into the main loop after each BLE beacon send. This is the project these findings apply to.

## 2. Lessons from the scrapped `single_shot_joulemeter_test` attempt

Before switching to copying 7_'s proven code directly, a series of incremental GPIO/peripheral fixes were tried on a from-scratch minimal test, each closing part of a large current gap:

| Stage | Stop-mode current | Fix applied |
| :--- | :--- | :--- |
| Baseline (no fixes) | ~1.15mA | — |
| + Park unused SX1280/SPI1 radio pins to Analog mode, hold NRST/NSS in known state | ~0.75mA | Radio chip was left un-configured, sitting in an active power-on state |
| + Clamp I2C2 pins to Analog *before* Stop-mode entry (not just after wake) | ~0.25mA | I2C2 pins were sitting in AF open-drain mode for the entire sleep window |
| + Clamp every other unused Port A/B pin to Analog mode | ~0.033mA | Reset-default floating-input pins are the worst case for Stop-mode leakage |
| + Switch SX1280 from held-reset to properly commanded Sleep mode (SPI opcode `0x84,0x01`) | ~0.033mA (no further improvement) | Hardware reset is not documented as SX1280's lowest-power state; Sleep mode is |

This got to ~33µA but never matched 7_'s ~5µA floor. Rather than keep chasing incremental GPIO fixes, the project was scrapped and rebuilt as an exact copy of 7_'s already-proven `STM32_EnterStopMode()`/`MX_GPIO_Init()`, with only the minimum necessary I2C2 additions layered on top (see Section 3). This is `7_joulemeter_test`.

## 3. `7_joulemeter_test`: what was added to 7_'s proven design

- I2C2 peripheral support (`MX_I2C2_Init()`, `HAL_I2C_MspInit`/`MspDeInit` for PB10/PB11) — did not exist in 7_ at all.
- Full IIS2MDC single-shot driver (`IIS2MDC_ReadRegister`, `IIS2MDC_WriteRegister`, `IIS2MDC_CheckWhoAmI`, `IIS2MDC_ReadXYZ`).
- `STM32_EnterStopMode()`: added `HAL_I2C_DeInit(&hi2c2)` before the PB10/PB11-to-Analog clamp (that clamp already existed in 7_'s code, written *anticipating* magnetometer use, but never exercised since I2C2 was never initialized). Added `MX_I2C2_Init()` after wake, alongside the existing SPI1 re-init. These are the *only* changes to `STM32_EnterStopMode()`/`MX_GPIO_Init()` — verified via full line-by-line diff against the original, unmodified 7_ project.
- Main loop: after `SX1280_SendFMDNBeacon()`, added `IIS2MDC_ReadXYZ()`.
- Boot: added `IIS2MDC_CheckWhoAmI()` diagnostic (2 blinks = OK, 8 = fail), an `I2C2_BusRecovery()` call before `MX_I2C2_Init()` claims the pins, and an explicit idle-mode write + register read-back proof (see Section 4).

## 4. Investigating an elevated Stop-mode floor (~26-30µA) — later found to be a hardware fault, not firmware

Initial measurements on `7_joulemeter_test` showed a Stop-mode floor around ~26-30µA, well above 7_'s ~5µA baseline. Several firmware-level hypotheses were tested and systematically ruled out:

1. **Residual continuous-mode configuration** (e.g. left over from `4_1`'s continuous-mode + `MAG_INT` firmware, since VDD is hardwired and never actually loses power across reflashes). Fix attempted: force `CFG_REG_A` to idle mode (`MD[1:0]=11`) as the very first action at boot, before anything else touches the sensor. **No change** in current.
2. **Stuck I2C bus** (a wedged transaction holding SDA low, sourcing continuous current through the module's pull-up resistors). Fix attempted: standard I2C bus-recovery procedure (manually clock SCL, issue a STOP condition) before `MX_I2C2_Init()` claims the pins. **No change** in current. (Also ruled out by magnitude: the schematic's 4.7kΩ pull-ups would produce ~700µA if a line were genuinely stuck low — far more than the ~26-30µA observed.)
3. **Full register-bank reset** (`REBOOT`+`SOFT_RST`, in case some *other* register bit — not just `MD[1:0]` — was left in a state keeping an internal block active). This was **counterproductive**: it made things measurably *worse* (~60µA) rather than better, and was removed. Root cause of that regression: combining `REBOOT`+`SOFT_RST` likely leaves the sensor in an intermediate/undefined state; recovering from it required a full board power-down (not just an MCU reflash), since VDD never actually drops otherwise.
4. **Register read-back proof**: added a diagnostic that reads `CFG_REG_A` back immediately after the idle-mode write and reports the result (blinked out visibly, or via Live Expressions as `dbg_cfg_reg_a_readback`). Confirmed the write **was** landing correctly (`147` decimal = `0x93` = idle mode) — the register-level fix was never actually the problem.

**Decisive two-board comparison**: the same elevated current (~0.030mA) was reproduced by flashing completely **unmodified `FMDN_tracker`** — a firmware that never initializes I2C2, never claims PB10/PB11, and never sends the magnetometer a single command — onto a board with the magnetometer populated, versus ~0.002mA on a board without the magnetometer populated at all, both running the *identical* unmodified firmware. Since `FMDN_tracker` has zero code path that can touch the sensor in any way, this proved conclusively that **no firmware, register state, or I2C activity of any kind could be responsible** — the elevated current was present purely from the magnetometer being physically populated and powered, regardless of what (if anything) was communicating with it. This is what redirected the investigation away from firmware entirely and toward the physical board/part itself.

**Conclusion**: with idle mode confirmed correctly set, bus recovery in place, a full register reset tried (and reverted, since it regressed), and the two-board comparison ruling out firmware entirely, there was nothing left to fix in software. A **KiCad schematic review** additionally ruled out board-level *design* explanations: standard 4.7kΩ I2C pull-ups (too large to explain the observed leakage even if stuck), no onboard regulator or LED on the magnetometer's own VDD net, standard application circuit per the IIS2MDC datasheet.

**Root cause, confirmed**: a **hardware fault specific to that one physical board/part**, not a design or firmware issue. A newly rebuilt board — same design, same firmware — measured **~5µA**, matching 7_'s original baseline and closing the investigation.

## 5. Joulemeter log analysis (HMC8015)

Logs captured via a Rohde & Schwarz HMC8015 power analyzer, `Logger`/`Duration` mode. Column reference: `URMS[V];IRMS[A];P[W];P[W];URange[V];IRange[A];UPPeak[V];UMPeak[V];IPPeak[A];IMPeak[A];Timestamp`. `IRMS[A]` is in **Amps** (not mA) — this was initially misread from a cropped screenshot, corrected once the raw CSV with headers was available.

Key instrument facts (confirmed against R&S documentation):

- Internal ADC sampling rate: 500 kHz.
- **Logging interval floor: 100ms (10Hz) — this is the fastest available in the standard Logger/Duration mode**, not a setting that can be pushed faster. The 500 kHz figure only applies to the RMS/Peak computation *within* each 100ms window, not to how fast samples can be logged to CSV.
- A known **instrument offset of ~0.008mA (8µA)** exists on this specific unit/setup — visible as the steady-state `IRMS` reading before the DUT is even active. Subtract this from raw readings to get the true DUT current.

Per-cycle waveform observed (10s cycle, matching 7_'s design): TX beacon burst (~15mA typical, up to ~42mA instantaneous peak captured in `IPPeak`) → magnetometer read → `STM32_EnterStopMode()`'s existing 10ms post-TX LED flash → Stop-mode floor for the remainder of the cycle.

**Active-window duration is essentially unchanged whether or not the diagnostic marker-blink LED is present** (~400ms either way, ~4 samples at 100ms resolution) — proving the LED was never the dominant contributor to that window's current. What actually dominates is MCU-active current (Cortex-M3 running at 8MHz, not asleep) plus SX1280 TX tail, both mA-scale — three orders of magnitude larger than the magnetometer's own read current (tens of µA per the datasheet), which is therefore **undetectable against that background at this instrument's resolution**. Isolating the magnetometer's own active-mode draw specifically would require either a much faster logging rate (not available on this instrument) or a firmware variant with BLE TX disabled entirely for a dedicated isolation run (not done as of this writing).

**Efficiency finding**: despite being only ~4% of the 10s cycle duration, the active window accounts for **~97% of total charge consumed per cycle** (back-of-envelope: ~2.8 mA·s from the active phase vs. ~0.08 mA·s from 9.6s of Stop-mode floor). This means **the active phase, not the Stop-mode floor, is what actually determines average current/battery life** at this point in the design. A diagnostic-only 200ms quiet delay (added to visually separate TX from the mag read on the low-resolution trace) was itself roughly half of the entire active window and has since been **removed** from the loop, since it was pure measurement-convenience overhead with no place in a real power number.

## 6. Confirmed: the magnetometer is already correctly power-gated by firmware

`IIS2MDC_ReadXYZ()` triggers single-measurement mode, polls for data-ready, reads the result, then explicitly writes idle mode back. Two independent guarantees this is correct:

1. Per the IIS2MDC datasheet, single-measurement mode is self-terminating — the sensor automatically returns itself to idle after exactly one conversion, regardless of whether the MCU successfully reads the result out.
2. The explicit idle-mode write is confirmed to actually land (register read-back = `147`/`0x93`, not just assumed).

The sensor is only genuinely active for its own ~2-40ms conversion window per 10s cycle; idle the rest of the time. This was verified directly rather than assumed, and rules out "sensor left running continuously" as an explanation for any of the current issues investigated.

## 7. Separate issue: Stop-mode baseline drift over time (unrelated to Section 4/6)

After the hardware-fault board was replaced, a **new, separate** phenomenon was observed: the Stop-mode baseline current was not stable over time — it climbed gradually the longer the board sat, independent of the earlier (now-resolved) hardware-fault issue.

Observed progression across sessions: ~1.2µA → 9.8µA, then later (after a cleaning reset) further sessions showed ~26µA → 45µA → 95µA → 180µA before the next cleaning.

**Diagnostic sequence:**

1. Cleaning the board (isopropyl alcohol, or physically rubbing near the magnetometer) reliably and repeatedly reset the current back down to baseline (~3-5µA) every time.
2. Initial hypothesis: passive PCB surface leakage from flux residue absorbing ambient humidity (a known failure mode near tightly-packed LGA pads — the IIS2MDC's SCL/SDA/CS/VDD/GND pins are all within a few mm of each other).
3. A refinement was considered: since the drift initially seemed to correlate with *using* the magnetometer, two bias-dependent mechanisms were considered as alternatives/additions — **self-heating** (repeated conversions causing local temperature rise, and temperature-dependent semiconductor leakage) and **electrochemical migration** (bias + moisture driving dendritic growth between adjacent pads, which is known to *accelerate* over time — consistent with the roughly-doubling 26→45→95→180µA progression).
4. **Decisive test**: the board was left completely **unpowered** and set aside, and the drift **still occurred** (26µA → 45µA → 95µA progression with the board disconnected the entire time). This is conclusive: no power means no bias, which rules out both self-heating and electrochemical migration as active mechanisms (both require current/voltage to progress). It also confirms the earlier "only after use" impression was very likely a **coincidental correlation with elapsed time** (heavier testing sessions span more wall-clock time, during which humidity was also passively accumulating), not a genuine causal link to electrical activity.

**Root cause, confirmed**: passive **hygroscopic moisture absorption into flux residue / surface contamination** on the PCB near the magnetometer's footprint. This is a purely physical/chemical process — needs only time and ambient humidity, not power or activity. It is **reversible** (not corrosion or metal migration, which the unpowered test rules out) — cleaning fully restores baseline every time, with no evidence of permanent degradation.

**Fix validated**: isopropyl alcohol cleaning around the magnetometer's pads reliably restores the baseline (confirmed: 180µA → 3µA after cleaning).

**Fix planned, not yet applied**: conformal coating (acrylic, e.g. MG Chemicals 4220P pen or 419D spray) over the affected area once material is available, to seal the surface against future humidity absorption. Masking required for the antenna/coax connector (RF detuning risk), the SWD header, and the reset button before coating. In the interim, the board is being re-cleaned with alcohol before each test session, and storage in a sealed bag with desiccant is recommended to slow re-accumulation between sessions.

## 8. Open items / next steps

- [ ] Apply conformal coating once available; re-verify baseline holds after cure.
- [ ] Consider a magnified visual inspection of the magnetometer's pads for visible flux residue/discoloration, for documentation completeness (not done yet, but would directly corroborate the diagnosis).
- [ ] Re-run the full end-to-end joulemeter capture (TX + mag read + Stop mode) on a freshly-cleaned board for a clean, final reference dataset now that the diagnostic 200ms gap has been removed.
- [ ] If a genuinely isolated measurement of the magnetometer's own active-mode current is needed, run a firmware variant with BLE TX disabled for one dedicated capture (not yet done — current data can't separate the sensor's read current from MCU-active/TX-tail current at 100ms logging resolution).

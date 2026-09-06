# 8_ble_mioty_tracker

Dual-protocol BLE + mioty asset tracker on a single SX1280 radio — real Google FMDN BLE advertising and real mioty (TS-UNB) uplink, with STM32 Stop-mode GPIO-leakage prevention tuned for coin-cell battery life.

This is the base project `9_Mag_ble_mioty_tracker` (motion-gated magnetometer scheduler) was forked from. Use this project as the reference "no magnetometer" baseline, or as the starting point for a new BLE+mioty variant.

## Hardware

* **MCU**: STM32F103CBU6 (Cortex-M3, 128 KB flash, 20 KB SRAM)
* **RF transceiver**: Semtech SX1280IMLTRT (2.4 GHz, supports BLE, LoRa, FLRC, GFSK/mioty modulation) over SPI1
* **Clocks**: 8 MHz HSE (PLL off — required for mioty's DWT-cycle-counter symbol timing, see below), LSE 32.768 kHz driving the RTC/Stop-mode wakeup

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
| LED | PC13 | Status LED |
| I2C2_SCL / I2C2_SDA | PB10 / PB11 | Present on the board for future sensor use; unused by this project's firmware |

### Why 8 MHz HSE, PLL off

The Fraunhofer `TsUnb` (mioty) C++ library uses the Cortex-M3 DWT cycle counter (`DWT->CYCCNT`) for sub-microsecond symbol timing (480 bps in the Lambda80 profile is exactly 3385.4 cycles/symbol). Any other clock speed, or PLL jitter, breaks mioty's frequency-hopping alignment.

## Schedule

The main loop alternates BLE cycles and a mioty cycle, using a plain cycle counter (`mioty_counter` vs `MIOTY_WAKEUP_COUNT`):

* **BLE cycles** (`mioty_counter < MIOTY_WAKEUP_COUNT`, currently 5 of them): wake every 1 second via RTC alarm, send an FMDN beacon on channels 37/38/39 (~15 ms), blink the LED once, sleep 1s.
* **Mioty cycle** (every `MIOTY_WAKEUP_COUNT`-th wakeup): blink 3x, run `TsUnb_Node.send()` (blocking, ~5.5s telegram-split TX across 24 sub-bursts), reconfigure back to BLE mode, sleep 5s.

At the current `MIOTY_WAKEUP_COUNT=5`, that's ~10 seconds of sleep between mioty sends (5 × 1s BLE + 5s post-mioty), plus mioty's own ~5.5s blocking TX on top. Adjust `MIOTY_WAKEUP_COUNT` (`Core/Src/main.cpp`) to retune — e.g. `30` for a ~5-minute cadence at these sleep durations, `90` for ~15 minutes.

## LED patterns

* **Boot**: 3 fast blinks (100ms) → 2s pause → 1 long blink (500ms) — firmware alive, radio init complete
* **BLE cycle**: 1 short blink (50ms)
* **Mioty cycle**: 3 short blinks (80ms), then the ~5.5s blocking TX

## Stop-mode GPIO clamping

To hit a verified ~30 µA Stop-mode sleep current (vs. >500 µA if GPIO lines are left floating), `STM32_EnterStopMode()` runs this sequence before sleeping:

1. Put the SX1280 into deep sleep (register-retained).
2. Disable SPI1; clamp SCK/MISO/MOSI to Input Pull-Down.
3. Drive NSS and NRST HIGH to lock the radio's interface state.
4. Clamp BUSY (PB15) and DIO1 (PB0) to Analog, no pull — **critical**: the SX1280 drives BUSY HIGH in deep sleep, so a pull-down here would leak ~82 µA.
5. Clamp I2C2 pins (PB10/PB11) to Analog to prevent leakage through external sensor pull-ups, even though I2C2 isn't used by this firmware.
6. Clamp every other unused Port A/B/C pin to Analog (except PC14/PC15, the LSE crystal pins — never touch those).

## Known issues resolved during bring-up

* **STM32F1 RTC alarm flag bug**: clearing the alarm flag with `RTC_CRL_ALRF_Msk` silently fails to clear the bit on this family (continuous fast wakeups, high current draw as a symptom). Fixed by using `CLEAR_BIT(RTC->CRL, RTC_CRL_ALRF)` (no `_Msk` suffix) instead.
* **SX1280 sleep-to-SPI deadlock**: after deep sleep or a mioty TX, the SX1280 drives BUSY high and shuts down its SPI interface; configuring the radio without waking it first hangs `SX1280_WaitBusy()` forever. Fixed with an explicit `SX1280_Wakeup()` (NSS low ~37.5µs + a dummy SPI byte) before every post-sleep radio reconfiguration.
* **BLE↔mioty mode-switch settling**: switching the radio's modulator/filters/hop table between BLE GFSK and mioty TS-UNB without a settling delay caused intermittent PLL/frequency transients. Fixed with ~10ms delays around mode switches.

## Measured power (0 dBm TX)

* Active BLE current: ~7.5 mA (MCU active + radio TX)
* Active mioty current: ~4.5 mA average (duty-cycled across the telegram split)
* Stop-mode sleep current: ~30 µA

On a 1000 mAh CR2477 coin cell: BLE-only at a 10s interval projects to roughly 2.3 years; BLE (10s) + mioty (15 min) projects to roughly 11 months.

## Build and flash

STM32CubeIDE, launch configuration `BLE_MIOTY_Tracker.launch`, expected ELF `Debug/BLE_MIOTY_Tracker.elf`.

## Project boundary

Use this project for: a real-radio BLE+mioty tracker baseline with no magnetometer/motion gating.

Use `9_Mag_ble_mioty_tracker` for: the same radio stack plus a motion-gated magnetometer scheduler and compass heading — the production direction this project was extended into.

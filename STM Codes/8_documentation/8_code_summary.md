# 8_ble_mioty_tracker: Codebase Summary

The `8_ble_mioty_tracker` directory contains the **final, optimized, and fully working dual-protocol firmware** for the low-power asset tracker. It is the culmination of all the debugging and power optimization efforts.

This document serves to explain exactly what the code in this specific folder does and why it is the "Gold Standard" for your thesis.

---

## 1. Dual-Protocol Interleaving (The "5-to-1" Cycle)
The main loop (`main.cpp`) is structured to seamlessly execute both Google Find My Device Network (FMDN) BLE beacons and Mioty uplinks using the same SX1280 transceiver, preventing any RF collisions:
* **BLE Cycle**: It wakes up and transmits a **BLE Advertising beacon** (channels 37, 38, 39). It repeats this **5 times** sequentially, sleeping for 1 second between each transmission.
* **Mioty Cycle**: After the 5th BLE transmission, it reconfigures the transceiver, waits 10ms for the PLL synthesizer to settle, and transmits a **Mioty TS-UNB packet** (24 frequency-hopping sub-bursts over 5.5 seconds). 
* **Recovery Sleep**: After the Mioty transmission, the board enters a **5-second recovery sleep**, and then the entire loop repeats.

---

## 2. Deep Sleep Clamping (~0.03 mA)
To extend the CR2477 coin cell battery life to nearly a year, the code aggressively puts the STM32F103 into **Stop Mode** while the radio sleeps. 
* We customized the `STM32_EnterStopMode()` function to shut down the SPI1 peripheral and clamp all SPI lines (MISO, MOSI, SCK) to pull-downs.
* **The Major Breakthrough**: The SX1280 `BUSY` and `DIO1` pins are explicitly set to **Analog Mode** during Stop Mode. This stops a persistent 82 µA current leak caused by the SX1280 driving the BUSY pin high while sleeping.

---

## 3. Solved Deadlocks & Bugs
This codebase contains the fixes for three major system-breaking bugs:

### A. The RTC Alarm Fix (Continuous Wakeup Bug)
* **The Problem**: The STM32 HAL `RTC_CRL_ALRF_Msk` macro failed to clear the alarm flag on the F1 MCU, causing the board to wake up immediately after entering sleep.
* **The Fix**: We bypassed the broken macro and used `CLEAR_BIT(RTC->CRL, RTC_CRL_ALRF)` so the board actually stays asleep.

### B. The SX1280 SPI Deadlock (Stuck BUSY Pin)
* **The Problem**: The board locked up with a flashing LED at `debug_stage = 7` or `31` because the MCU tried to send SPI commands while the SX1280 was sleeping and driving BUSY high.
* **The Fix**: We added a manual `SX1280_Wakeup()` sequence (toggling the NSS pin low for 37.5µs) to reliably wake the radio out of deep sleep before running `SX1280_InitBLE()`.

### C. Mioty Crystal Calibration
* **The Fix**: The DWT (Data Watchpoint and Trace) cycle timer is tuned with the `1.00025` multiplier. This software compensation perfectly aligns the Mioty sub-burst hops against the drift of the 8 MHz HSE crystal, allowing the gateway to reliably decode the telegrams.

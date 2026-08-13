# FMDN Tracker Power Profiling & Joulemeter Setup Guide

This document outlines the procedure for accurately measuring the ultra-low power profile of the STM32 + SX1280 FMDN Tracker firmware using a Power Analyzer (Joulemeter) or bench power supply.

> [!IMPORTANT]
> To get accurate idle current readings (~5 µA), you **must** comment out `HAL_DBGMCU_EnableDBGStopMode();` in `main.c` (search for it — its line number shifts as the file changes) before flashing. Leaving this enabled keeps the STM32's debug block powered during Stop Mode, which will artificially inflate your idle current by hundreds of microamps.

---

## 1. Power Supply Settings (Simulating a CR2032)

A standard CR2032 lithium coin cell has a nominal voltage of 3.0V and relatively high internal resistance. To simulate this environment with a bench power supply or source-meter:

*   **Voltage (`V`)**: Set to **3.0V** (You can test down to 2.7V to simulate a dying battery).
*   **Current Limit (`I`)**: Set the compliance/limit to **50 mA**. 
    * *Why?* A CR2032 has a low continuous discharge rating (usually ~3 mA), but it can supply short pulsed bursts of 15-20 mA. Setting your supply limit to 50 mA gives the board enough headroom to complete the 15 mA TX burst without triggering overcurrent protection, while still protecting your board if a short circuit occurs.

---

## 2. Hardware Test Setup

There are two primary ways to hook up a power analyzer, depending on whether your analyzer acts as the power *source* (like a Nordic PPK2 or Joulescope) or acts as an *ammeter in series* with a bench supply.

### Option A: Analyzer as the Power Source (Recommended)
Use this if your Joulemeter supplies power directly.

1. Set the Joulemeter output voltage to **3.0V**.
2. Connect **VOUT+** from the analyzer to the **3.3V / VCC** pin on your STM32 board.
3. Connect **GND** from the analyzer to the **GND** pin on your STM32 board.
4. **Disconnect the ST-Link** entirely (unplug 3.3V, GND, SWDIO, and SWCLK). The ST-Link will back-power the board through the SWDIO pins and ruin your measurements.

### Option B: Analyzer in Series with a Bench Supply
Use this if your Joulemeter only measures current and relies on an external bench supply.

1. Set bench supply to **3.0V** and current limit to **50 mA**.
2. Connect Bench Supply **GND** to the STM32 board **GND**.
3. Connect Bench Supply **V+** to the **Joulemeter IN+**.
4. Connect **Joulemeter OUT-** to the STM32 board **3.3V / VCC** pin.
5. **Disconnect the ST-Link** entirely.

---

## 3. Power Analyzer (Joulemeter) Configuration

To capture the brief beacon pulses accurately, configure your analyzer software with the following settings:

*   **Sampling Rate**: Set to at least **1,000 Samples per second (1 kSps)**. 10 kSps or higher is ideal to capture the exact shape of the 35 ms RF burst.
*   **Measurement Window**: Set the display window to **15 seconds** so you can see a full 10-second sleep cycle and two TX bursts on the same screen.
*   **Current Range**: If your analyzer uses manual ranging, set it to dynamic/auto ranging, or a fixed range that comfortably covers **5 µA to 20 mA**.

---

## 4. Expected Power Profile & Waveform

Once powered on, the tracker will execute a ~10-second cycle. You should see a waveform that looks exactly like this:

````mermaid
xychart-beta
    title "Expected Current Waveform (10s Cycle)"
    x-axis "Time" [0s, "0.035s (TX)", "0.045s (LED)", "5s (Sleep)", "10s (Sleep)", "10.045s (Next TX)"]
    y-axis "Current (mA)" 0.0 --> 16.0
    line [1.5, 15.0, 3.0, 0.005, 0.005, 15.0]
````

### Phase Breakdown

1.  **TX Burst (~35 ms)**: You will see a sharp block of current around **~15 mA**. This is the SX1280 transmitting on channels 37, 38, and 39.
2.  **LED Flash (10 ms)**: Immediately following the TX burst, the current will drop to **~3 mA** for exactly 10 milliseconds while the status LED flashes.
3.  **Stop Mode Sleep (10 seconds)**: The current will plummet to the baseline floor. You should see roughly **5 µA to 15 µA** during this phase.

> [!TIP]
> **Averaging**: To calculate your true average power consumption, drag a selection box in your Joulemeter software starting from the *beginning* of one TX burst to the *beginning* of the next TX burst (an exact 10.045-second window). The software will calculate the integral. Expect the overall average to be well under **100 µA**.

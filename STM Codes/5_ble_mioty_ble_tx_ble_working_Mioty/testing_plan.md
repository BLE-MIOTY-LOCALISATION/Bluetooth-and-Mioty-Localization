# Testing & Verification Plan: Standalone Mioty Transmission

This document summarizes the changes implemented in the `3_MIOTY_standalone_working` directory and provides a structured plan to verify the correctness of the code and analyze on-air Mioty packets.

---

## 1. Summary of What Has Been Done So Far

We successfully cloned and reconfigured the codebase for standalone C++ Mioty transmissions:
1. **Directory Setup**: Cloned the STM32 project `3_BLE_tx_battery` into `3_MIOTY_standalone_working`.
2. **Library Dependency Import**: Copied the Fraunhofer Mioty C++ template headers (`Encryption`, `Trx`, `TsUnb`, and `Utils/BitAccess.h`) from the RP Pico reference project to `Core/Inc/`. Removed Pico-specific USB/LED utility files (`utils.cpp`, `utils.h`) to prevent compilation errors.
3. **STM32 Platform Abstraction (`STM32TsUnb.h`)**: Implemented the SPI operations and the high-precision DWT (Data Watchpoint and Trace) CPU cycle counter timing.
4. **Mioty Instantiation Templates (`STM32TsUnbTemplates.h`)**: Mapped the simple node class templates to use the new `STM32TsUnb` hardware layer.
5. **C++ Main Migration (`main.cpp`)**: Renamed `main.c` to `main.cpp` and re-wrote the main execution loop to:
   - Initialize the C++ Mioty simple node (`TsUnb_Node.init()`).
   - Configure EUI-64 addresses and network key values.
   - Build a 20-byte payload containing an incrementing packet counter.
   - Transmit the packet every 5 minutes in active mode (excluding Stop Mode/low-power standby, as requested).

---

## 2. Timing and Code Correctness Verification Plan

Mioty (TS-UNB) relies on a software-defined Frequency Shift Keying (FSK) timing loop. It requires changing the radio carrier frequency for each bit at a rate of **2380.371 symbols per second**.
Each symbol lasts exactly **$420.1 \ \mu\text{s}$**.
If the loop is delayed by even a few microseconds, the receiver will lose symbol synchronization and fail to decode the packet.

### Step 1: C++ Compiler Activation
Before compiling, you must verify that the project is configured as a C++ project in STM32CubeIDE:
1. Right-click the project folder `3_MIOTY_standalone_working` in the Project Explorer.
2. Select **Convert to C++**.
3. Under Project Properties -> C/C++ Build -> Settings, confirm that the compiler is set to C++11 or C++14.
4. Clean the project and verify it compiles with zero errors.

### Step 2: Timing Validation via Logic Analyzer/Oscilloscope
To confirm the DWT cycle counter timing is exactly $420 \ \mu\text{s}$ per symbol:
1. Connect a logic analyzer (e.g., Saleae) or oscilloscope to the **SPI SCK (`PA5`)** and **NSS (`PA4`)** pins.
2. Trigger on a transmission cycle.
3. Observe the NSS pulses:
   - During packet transmission, the system sends 24 bursts of data.
   - Zoom in to a single burst: you should see SPI clock bursts (SCK) representing the frequency register update commands (`0x86` opcode + 3 frequency bytes).
   - **Measure the time gap between consecutive SPI transactions within a burst.**
   - **EXPECTED MEASUREMENT:** Exactly **$420 \ \mu\text{s}$** ($\pm 1 \ \mu\text{s}$) from the start of one SPI frequency update transaction to the start of the next.

---

## 3. On-Air Mioty Packet Verification Plan

Mioty uses **Telegram Splitting Ultra Narrowband (TS-UNB)** technology. An uplink transmission is split into 24 short radio bursts sent on pseudo-random frequencies in the 2.4 GHz band (centered at $2400.2 \text{ MHz}$) over a duration of approximately 1.5 seconds.

### Method A: Spectrum Visualization via SDR (Software Defined Radio)
This is the easiest way to verify that the radio is transmitting correctly in standalone mode without a full Mioty gateway:
1. Connect a 2.4 GHz compatible SDR receiver (e.g. HackRF, Adalm-Pluto, or RTL-SDR with a downconverter) to your PC.
2. Open a spectrum analyzer software such as **GQRX**, **SDR#**, or **CubicSDR**.
3. Tune the center frequency of the software to **$2400.20 \text{ MHz}$** with a bandwidth of at least **$1 \text{ MHz}$**.
4. Set the FFT display to waterfall mode.
5. Trigger a transmission (or wait for the 5-minute interval).
6. **EXPECTED PATTERN:** You should see 24 short, narrow spectral lines appearing in a pseudo-random pattern across the spectrum over a 1.5-second interval.

### Method B: SPI Command Sniffing
If you don't have an SDR, you can verify that the radio is outputting the correct RF signal by analyzing the SPI commands:
1. Connect a logic analyzer to SPI lines: MOSI (`PA7`), SCK (`PA5`), NSS (`PA4`).
2. Capture a transmission.
3. Verify the opcodes sent to the radio transceiver:
   - Expect opcode **`0xC1`** (`SX1280_SETMODE_FREQSYNTH`) and opcode **`0x8E`** (`SX1280_SETTXPARAMS`) at the start of a burst.
   - Expect opcode **`0x86`** (`SX1280_WRITE_FRF`) to write the frequency register at the beginning of each symbol.
   - Expect opcode **`0xD1`** (`SX1280_SET_CW`) to put the radio into Continuous Wave mode.
   - Expect opcode **`0x84`** (`SX1280_SETMODE_SLEEP`) at the end of each burst.
4. If this sequence repeats 24 times per packet with correct frequency registers, the software is functioning correctly.

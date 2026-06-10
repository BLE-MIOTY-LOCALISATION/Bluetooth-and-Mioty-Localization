# mioty-ble_reference_project – High‑level Overview

---
## 1. Project Purpose

The repository contains a **reference implementation** of a **Mioty‑BLE hybrid transmitter** for the **Raspberry Pi Pico** development board. It demonstrates how to:
- Use the **SX1280** radio module for LoRa‑style **Mioty** ultra‑narrow‑band communication.
- Switch between conventional **BLE iBeacon/Eddystone** advertising and Mioty packets.
- Integrate the **RPPicoTsUnb** library (Mioty stack for Pico) and AES‑128 encryption utilities.

---
## 2. Build System (CMake)

```text
CMakeLists.txt                 # Main build script for the Pico SDK
pico_sdk_import.cmake          # Imports the Pico SDK (downloaded separately)
```

The CMake file:
1. Sets the C/C++ standards (`C11`, `C++17`).
2. Pulls in the Pico SDK (`include(pico_sdk_import.cmake)`).
3. Declares an executable `mioty-ble` built from:
   - `mioty-ble.cpp`
   - `SX1280Driver/sx1280.cpp`
   - `Transmitter/trasmitter.cpp`
   - `UTILS/utils.cpp`
4. Links the standard Pico library (`pico_stdlib`) and the hardware SPI driver (`hardware_spi`).
5. Enables UART/USB stdio (UART disabled, USB enabled).

**Typical build steps** (run from the project root):
```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico
make -j$(nproc)
```
The resulting `mioty-ble.uf2` can be flashed to the Pico.

---
## 3. Directory Layout

| Directory | Purpose | Key Files |
|---|---|---|
| `.vscode` | VS Code workspace config (debug, tasks, extensions) | `c_cpp_properties.json`, `launch.json`, `tasks.json` |
| `Encryption` | AES‑128 helper (used by the Mioty stack) | `Aes128.h` |
| `SX1280Driver` | Driver for the SX1280 radio chip | `sx1280.h`, `sx1280.cpp` |
| `Transmitter` | BLE packet preparation & transmission wrappers | `transmitter.h`, `trasmitter.cpp` |
| `Trx` | Low‑level radio abstractions shared with Mioty | `Rfm69hw.h`, `SX1280.h` |
| `TsUnb` | Mioty protocol implementation (RPPicoTsUnb) | `FixedMac.h`, `Phy.h`, `RadioBurst.h`, `SimpleNode.h`, `VariableMac.h` |
| `Utils` | Miscellaneous helper functions used by the project | `utils.cpp` (if present) |
| Root files | Build entry points | `CMakeLists.txt`, `mioty-ble.cpp`, `pico_sdk_import.cmake` |

---
## 4. Core Modules (Brief Description)

### `mioty-ble.cpp`
* **Entry point** – `int main()` initializes stdio, the SX1280 radio, and the LED.
* Sets up a **BLE iBeacon** payload and a **Mioty node** (`TsUnb_EU1_Lambda80_t`).
* Loops forever transmitting BLE packets; after a configurable number (`CALLS`) it switches to Mioty, sends the UUID, then returns to BLE.
* Uses helper functions from `Transmitter/trasmitter.cpp` and `SX1280Driver/sx1280.cpp`.

### `SX1280Driver/sx1280.{h,cpp}`
* Thin wrapper around the SX1280 hardware registers.
* Provides `initializeRadio()`, `prepareForBeacon()`, and `transmitData()` used by the main loop.
* Configures the radio for **BLE channels (37‑39)** and later for Mioty frequencies.

### `Transmitter/trasmitter.{h,cpp}`
* Contains **BLE‑specific utilities** (e.g., `prepareForBeacon()`, `transmitData()`).
* Abstracts the payload handling so the main file stays concise.

### `TsUnb/*`
* Implements the **Mioty ultra‑narrow‑band stack** for the Pico.
* `FixedMac.h`/`VariableMac.h` define MAC address handling.
* `Phy.h` provides PHY‑layer primitives (modulation, coding).
* `RadioBurst.h` and `SimpleNode.h` expose high‑level send/receive APIs used in `prepareForMioty()` and `TsUnb_Node.send()`.

### `Encryption/Aes128.h`
* Simple AES‑128 encrypt/decrypt helpers – required when Mioty payloads need confidentiality.

---
## 5. VS Code Configuration (`.vscode` folder)

| File | Role |
|---|---|
| `c_cpp_properties.json` | IntelliSense paths for the Pico SDK and project headers. |
| `launch.json` | Debug configuration (launches `mioty-ble` on the Pico via OpenOCD). |
| `tasks.json` | Build task that runs the CMake + make commands from VS Code. |
| `settings.json` | Workspace‑specific editor settings (e.g., tab size). |
| `extensions.json` | Recommended extensions (C/C++ for VS Code, Cortex‑Debug). |
| `cmake-kits.json` | Defines CMake kits for different toolchains; the default kit points to the Pico GCC. |

---
## 6. How to Run & Test

1. **Install the Pico SDK** (follow the official guide) and ensure `PICO_SDK_PATH` points to it.
2. **Connect the Pico** via USB while holding the BOOTSEL button to put it in flash mode.
3. **Build** as described in section 2.
4. **Copy** the generated `mioty-ble.uf2` to the mounted Pico drive.
5. The board will start transmitting BLE iBeacon packets; after the defined interval it will switch to a Mioty packet containing the UUID.
6. Use a BLE scanner (e.g., nRF Connect) to see the iBeacon, and a Mioty‑compatible receiver to verify the ultra‑narrow‑band transmission.

---
## 7. High‑Level Architecture (Mermaid)

```mermaid
flowchart TB
    subgraph Pico[RPi Pico]
        direction TB
        Radio[SX1280 Radio] -->|BLE| BLE_Stack[BLE iBeacon/Eddystone]
        Radio -->|Mioty| Mioty_Stack[Mioty (TsUnb)]
        MCU[Main MCU] --> Radio
        MCU --> Encryption[AES‑128]
        MCU --> Utils[Utility Helpers]
    end
    BLE_Stack -- Sends --> BLE_Receiver[BLE Receiver]
    Mioty_Stack -- Sends --> Mioty_Receiver[Mioty Receiver]
```

---
## 8. Glossary

* **Mioty** – A proprietary ultra‑narrow‑band protocol designed for massive IoT deployments.
* **BLE** – Bluetooth Low Energy, commonly used for short‑range advertising (iBeacon, Eddystone).
* **Pico SDK** – Official C/C++ SDK for Raspberry Pi Pico (RP2040 MCU).
* **SF1280** – Semtech radio chip supporting LoRa, BLE, and Mioty waveforms.
* **UF2** – USB Flashing Format used by the Pico to load firmware.

---
## 9. References

* [Raspberry Pi Pico SDK Documentation](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-sdk.pdf)
* [Mioty Specification (public overview)](https://www.sierrawireless.com/technologies/mioty/)
* [SX1280 Datasheet – Semtech](https://www.semtech.com/uploads/documents/sx1280.pdf)

---
*Prepared as a **high‑level overview** per your request. Feel free to ask for deeper dives into particular modules or code snippets.*

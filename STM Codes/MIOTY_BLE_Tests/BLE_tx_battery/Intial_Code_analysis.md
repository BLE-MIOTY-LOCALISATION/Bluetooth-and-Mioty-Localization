# STM32 SX1280 BLE Beacon - Technical Analysis & Reference Manual

This document provides a comprehensive technical analysis of the **`2_ble_mioty_ble_tx_ble_working`** firmware. It details the project directory structure, hardware connections, software implementation, SX1280 transceiver configuration, and how to verify and customize the beacon.

---

## 1. Directory Structure

This project is a standalone STM32CubeIDE project. Unlike other versions of this codebase, all the driver logic for the Semtech SX1280 radio chip is consolidated directly within `main.c` and `main.h` to minimize external file dependencies.

```text
2_ble_mioty_ble_tx_ble_working/
├── .cproject                         # Eclipse C/C++ project configuration
├── .mxproject                        # STM32CubeMX project database
├── .project                          # Eclipse project definition
├── STM32F103CBUX_FLASH.ld             # Linker script for STM32F103CBUx (128KB Flash, 20KB SRAM)
├── ble_mioty_ble_tx Debug.launch      # GDB Debug launcher configuration
├── ble_mioty_bring_up.ioc             # STM32CubeMX configuration file
├── Core/
│   ├── Inc/
│   │   ├── main.h                     # Pin definitions & application exports
│   │   ├── stm32f1xx_hal_conf.h       # HAL driver module selection
│   │   └── stm32f1xx_it.h             # Interrupt service routine headers
│   ├── Src/
│   │   ├── main.c                     # Primary application logic, driver, and state machine
│   │   ├── stm32f1xx_hal_msp.c        # HAL peripheral MSP initialization (SPI, GPIO, etc.)
│   │   ├── stm32f1xx_it.c             # Interrupt service routines
│   │   ├── syscalls.c                 # Minimal system calls implementation
│   │   ├── sysmem.c                   # Memory management system calls
│   │   └── system_stm32f1xx.c         # Clock configuration and system initialization
│   └── Startup/
│       └── startup_stm32f103cbux.s    # Assembly startup code and vector table
└── Drivers/                           # STM32 HAL and CMSIS driver libraries
```

### 💡 Beginner Notes
* **What are all these files?** An embedded project is split into code you write (`Core/`), configuration settings for the IDE (`.project`, `.cproject`), and code libraries written by the chip manufacturer (`Drivers/`).
* **The Linker Script (`.ld`):** This is a map telling the compiler exactly where to place code in the chip's ROM (Flash Memory) and where variables should go in RAM.
* **Startup Assembly (`.s`):** The very first code that runs when the chip powers on. It sets up the stack pointer, clears memory, and jumps to your `main()` function.

### 🔬 In-Depth Analysis
* **Memory Map (STM32F103CBUx):** This specific chip contains 128 KB of Flash (mapped starting at `0x08000000`) and 20 KB of SRAM (mapped starting at `0x20000000`). The linker script specifies these boundaries to prevent overflow.
* **Vector Table:** Located at the beginning of Flash, this contains the address pointer for the Reset Handler, NMI Handler, HardFault Handler, and peripheral interrupts.
* **HAL vs. CMSIS:** CMSIS is the generic ARM standard for hardware registers. HAL (Hardware Abstraction Layer) is ST's higher-level C library wrapper that provides APIs like `HAL_SPI_Transmit` so you do not have to write bits to hardware registers directly.

---

## 2. Hardware Connections (STM32 to SX1280)

Based on the configurations in `main.h` and `main.c`, the physical pin connection mapping is as follows:

| SX1280 Pin | Function | STM32 Pin | Config Type | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **MISO** | SPI Master Input | `PA6` | Input Floating / AF | SPI1 MISO |
| **MOSI** | SPI Master Output | `PA7` | Alternate Function PP | SPI1 MOSI |
| **SCK** | SPI Clock | `PA5` | Alternate Function PP | SPI1 SCK (9 MHz) |
| **NSS** | Chip Select | `PA4` | Output Push-Pull | Active LOW (Software Controlled) |
| **NRESET** | Hardware Reset | `PA3` | Output Push-Pull | Active LOW |
| **BUSY** | Chip Busy Indicator | `PB15` | Input Floating | High = Chip Busy |
| **DIO1** | Interrupt Signal | `PB0` | Input Floating | High = TxDone Interrupt |
| **LED** | User status LED | `PC13` | Output Push-Pull | On-Board LED (Active LOW on Blue Pill) |

### 💡 Beginner Notes
* **What is SPI?** Serial Peripheral Interface (SPI) is a synchronous communication protocol. A Master (STM32) controls the clock line (`SCK`) and coordinates bidirectional data flow with a Slave (SX1280) using `MOSI` (Master Out Slave In) and `MISO` (Master In Slave Out).
* **Why do we need Busy and CS?** Unlike standard devices that respond instantly, radio chips require time to wake up, change channels, or send RF signals. The `BUSY` pin tells the STM32 "wait, I am still processing." The `NSS` (Chip Select) pin wakes up the SPI interface on the chip when pulled LOW.

### 🔬 In-Depth Analysis
* **Software NSS vs. Hardware NSS:** Although SPI peripherals have hardware-managed NSS pins, they can be unreliable during multi-byte transfers. Using software-controlled GPIO (`PA4`) gives the code precise control over when the transaction starts and stops.
* **Bus Speed Constraints:** SPI1 operates on the APB2 bus (running at 72 MHz). The `BaudRatePrescaler` is set to `8`, yielding a clock frequency of `9 MHz` (`72 / 8 = 9`). This speed is safe, fits well within the SX1280's maximum SPI speed limit (18 MHz), and maintains signal integrity on jumper wires.
* **Interrupt Flag (DIO1):** Rather than polling the radio status register repeatedly via SPI (which wastes clock cycles and adds noise), the SX1280 triggers its `DIO1` pin physically. This allows the MCU to monitor transmission status asynchronously.

---

## 3. SX1280 Driver Implementation (`main.c`)

The SPI communication and control commands for the Semtech SX1280 radio chip are implemented as static driver functions inside `main.c`:

### Primitives
* **`SX1280_WaitBusy()`**
  Polls the `LORA_BUSY_Pin` (`PB15`). If the pin remains high (busy) for more than 1000ms, the system enters a fail-safe state, toggling the on-board LED rapidly (50ms interval) in an infinite loop.
* **`SX1280_GetStatus()`**
  Sends status query opcode `0xC0` over SPI and returns the status byte. 
* **`SX1280_SendCommand(uint8_t *cmd, uint8_t len)`**
  Sends an opcode command block to the SX1280.
* **`SX1280_WriteRegister(uint16_t addr, uint8_t *data, uint8_t len)`**
  Writes multiple bytes to the registers starting at the 16-bit address `addr` using opcode `0x18`.
* **`SX1280_ReadRegister(uint16_t addr)`**
  Reads a single byte from the register at address `addr` using opcode `0x19`.
* **`SX1280_WriteBuffer(uint8_t offset, uint8_t *data, uint8_t len)`**
  Writes payload bytes directly into the SX1280's memory buffer at the specified `offset` using opcode `0x1A`.

### 💡 Beginner Notes
* **Command Opcodes:** The SX1280 has internal microprocessor functions triggered by hex codes (Opcodes). For example, sending `0x80` tells it to go to Standby, and `0x8A` sets the packet type.
* **How ReadRegister Works:** To read from the chip, the STM32 first transmits the Read opcode (`0x19`), followed by the 2-byte register address, and then sends a dummy byte (`0x00`) to generate the SPI clock pulses needed to receive the register value back on the MISO line.

### 🔬 In-Depth Analysis
* **Status Byte Breakdown:** When communicating over SPI, the first byte received back on the MISO line is always the device status byte:
  * **Circuit Mode (Bits 7:5):**
    * `0` = Reserved
    * `1` = Standby RC (STDBY_RC)
    * `2` = Standby XOSC (STDBY_XOSC)
    * `3` = Frequency Synthesis (FS)
    * `4` = Receiver Mode (RX)
    * `5` = Transmitter Mode (TX)
  * **Command Status (Bits 4:2):**
    * `1` = Command execution successful.
    * `3` = Timeout during command execution.
    * `4` = Command processing error.
    * `5` = Command execution failed.
    * `6` = Transmission complete.
* **Handshake Safety:** The polling loop in `SX1280_WaitBusy` ensures SPI transactions do not collision-corrupt the radio's command registers. If the radio hangs, the watchdog-style fast-blink loop prevents the MCU from silently executing empty loops.

---

## 4. BLE Initialisation & Diagnostics

### Initialisation Phase (`SX1280_InitBLE()`)
This function configures the radio specifically to transmit standard BLE advertising packets:
1. **Standby Mode:** Forces the transceiver into `Standby RC` mode (opcode `0x80`).
2. **Packet Type:** Sets the packet standard to BLE (opcode `0x8A`, payload `0x04`).
3. **Buffer Management:** Sets TX buffer base to `0x80` and RX base to `0x00` (opcode `0x8F`).
4. **Modulation Parameters:** Configures BLE 1 Mbps bitrate, modulation index of 0.5, and BT bandwidth product of 0.5 (opcode `0x8B`, payload `{0x45, 0x01, 0x20}`).
5. **TX Power:** Sets transmission power to maximum (`+13 dBm`) and ramp-up time to 20 microseconds (opcode `0x8E`, payload `{0x1F, 0x20}`).
6. **Advertiser Access Address:** Writes `0x8E89BED6` (the standard BLE advertising access address) to registers `0x09CF - 0x09D2`.
7. **CRC Seed:** Writes `0x555555` (standard BLE advertising CRC seed) to registers `0x09C7 - 0x09C9`.
8. **IRQ Routing:** Maps the `TxDone` interrupt flag to the physical `DIO1` pin (`PB0`).

### Hardware Verification (`SX1280_VerifyInit()`)
To verify that the register configuration was successful, the MCU reads back key parameters and blinks the LED in a structured sequence:
* **Group 1 (Access Address MSB):** Reads register `0x09CF`. Expects `0x8E`. Blinks: `(0x8E >> 4) + 1 = 8 + 1 = 9 blinks`.
* **Group 2 (CRC Seed MSB):** Reads register `0x09C7`. Expects `0x55`. Blinks: `(0x55 >> 4) + 1 = 5 + 1 = 6 blinks`.
* **Group 3 (Circuit Mode):** Gets status byte. Expects `STDBY_RC` (2). Blinks: `2 + 1 = 3 blinks`.
* **Group 4 (Command Status):** Gets status command bits. Expects idle (0). Blinks: `0 + 1 = 1 blink`.

### 💡 Beginner Notes
* **Why 9, 6, 3, 1 blinks?** Without a screen, checking if code works on physical chips is hard. By reading back internal registers and blinking the LED, the developer gets visual confirmation that:
  1. The SPI bus is wiring-functional (registers read successfully).
  2. The chip's configurations are correct.
* **Whitening & Seeds:** Wireless signals suffer from interference. "Whitening" scrambles the signal mathematically using a sequence generator so that it looks like random white noise on-air, preventing long streams of 1s or 0s from desynchronizing the receiver.

### 🔬 In-Depth Analysis
* **Modulation Parameters (`0x8B`):** 
  * Parameter 1 (`0x45`): Selects GFSK modulation with 1 Mbps bandwidth.
  * Parameter 2 (`0x01`): Modulation index parameter $\beta = 0.5$, which represents standard BLE frequency shift keying deviation ($\pm 250 \text{ kHz}$).
  * Parameter 3 (`0x20`): Gaussian filter factor $BT = 0.5$ to shape pulses and limit spectral leakage.
* **BLE Channel Access Address Register Mapping:** BLE advertising packets must use the hardcoded synchronization word `0x8E89BED6`. The SX1280 requires this to be written in big-endian order to register addresses `0x09CF` through `0x09D2` to establish correct on-air bit-sync.

---

## 5. Packet Formulation & Transmission

The main transmission sequence cycles through the 3 standard BLE advertising channels:

### Advertising Channels & Seeds
* **Channel 37 (2402 MHz):** Set Frequency bytes `{0xB8, 0xC4, 0xEC}`, whitening seed `0x53`.
* **Channel 38 (2426 MHz):** Set Frequency bytes `{0xBA, 0x9D, 0x89}`, whitening seed `0x33`.
* **Channel 39 (2480 MHz):** Set Frequency bytes `{0xBE, 0xC4, 0xEC}`, whitening seed `0x73`.

### On-Air PDU Construction (iBeacon Payload)
The code forms a 38-byte BLE advertisement packet in the memory buffer:
* **Header (2 Bytes):**
  * `0x42`: PDU Type `ADV_NONCONN_IND` (Non-connectable undirected advertising) and `TxAdd = 1` (random address).
  * `0x24` (36): Total payload length (6-byte MAC + 30-byte payload data).
* **MAC Address (6 Bytes):**
  * `{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}` in little-endian format. Transmits on-air as `FF:EE:DD:CC:BB:AA`.
* **AD Structure 1 - Flags (3 Bytes):**
  * `0x02` (Length), `0x01` (Flags type), `0x06` (LE General Discoverable + BR/EDR Not Supported).
* **AD Structure 2 - iBeacon Manufacturer Data (27 Bytes):**
  * `0x1A` (Length), `0xFF` (Manufacturer Specific Type).
  * `0x4C, 0x00`: Apple Inc. Company ID (little-endian).
  * `0x02, 0x15`: iBeacon subtype and length (21 bytes).
  * `0x01..0x10`: Proximity UUID (`01020304-0506-0708-090A-0B0C0D0E0F10`).
  * `0x00, 0x01`: Major ID (`1`).
  * `0x00, 0x02`: Minor ID (`2`).
  * `0xC5`: Measured RSSI Power calibration at 1 meter (`-59 dBm`).

### 💡 Beginner Notes
* **Why 3 channels?** Bluetooth operates in the busy 2.4 GHz frequency spectrum alongside Wi-Fi and microwaves. To bypass interference, BLE defines three independent channels (37, 38, and 39) spaced far apart. The beacon transmits on all three consecutively to ensure a scanning smartphone picks up at least one.
* **iBeacon Protocol Layout:** An iBeacon does not "connect" to anything. It acts like a lighthouse, blasting out its UUID (identity), Major (general location/store branch), Minor (specific shelf or beacon placement), and a Power reference so the phone can calculate how far away it is.

### 🔬 In-Depth Analysis
* **RF Frequency Registers Calculation:** The SX1280 frequency synthesizer registers are calculated via:
  $$\text{Register Value} = \frac{f_{\text{RF}}}{f_{\text{XTAL}}} \times 2^{18}$$
  With an internal reference crystal $f_{\text{XTAL}} = 52 \text{ MHz}$ (i.e. $52,000,000 \text{ Hz}$):
  * For **$2402 \text{ MHz}$ (Ch 37):** $\text{Value} = \frac{2402}{52} \times 262144 = 12109036.3 \approx 12109036 = \text{0xB8C4EC}$.
  * For **$2426 \text{ MHz}$ (Ch 38):** $\text{Value} = \frac{2426}{52} \times 262144 = 12230025.2 \approx 12230025 = \text{0xBA9D89}$.
  * For **$2480 \text{ MHz}$ (Ch 39):** $\text{Value} = \frac{2480}{52} \times 262144 = 12501333.3 \approx 12501333 = \text{0xBEC455}$.
  
  > [!NOTE]
  > In the firmware code (`main.c` line 325), Channel 39 is set using the bytes `0xBE, 0xC4, 0xEC` (decimal $12502252$). This is a small copy-paste offset from Channel 37 (`0xEC` instead of `0x55`), shifting the on-air frequency slightly to $2480.18 \text{ MHz}$. It still functions because the receiver capture bandwidth is wide enough to tolerate the $181 \text{ kHz}$ offset, but the mathematically precise representation of $2480.00 \text{ MHz}$ is `0xBEC455`.
* **Whitening Architecture:** In the BLE specification, whitening uses a 7-bit shift register with a generator polynomial $x^7 + x^4 + 1$. The channel index value is mapped to the shift register initialization vector, which matches the hardcoded seeds (`0x53`, `0x33`, `0x73`).
* **PDU Header Bits:** The PDU header byte `0x42` expands as:
  * Bits 3:0 = `0x02` (ADV_NONCONN_IND).
  * Bit 4 = Reserved.
  * Bit 5 = ChSel (0).
  * Bit 6 = TxAdd (1, indicates Random Private/Static address).
  * Bit 7 = RxAdd (0).

---

## 6. How to Locate and Scan the Beacon

To scan for this beacon using the **nRF Connect** app (on iOS, Android, or Desktop):

1. **Bluetooth Address:**
   Look for the MAC Address **`FF:EE:DD:CC:BB:AA`**.
2. **Device Classification:**
   The device has no text name, but nRF Connect will auto-detect the payload details:
   * It will show up classified as an **`iBeacon`**.
   * The manufacturer code will be labeled as **`Apple, Inc.`** (or `0x004C`).
3. **Advertising Payload details:**
   If you tap to expand the details of the device, you will see:
   * **UUID:** `01020304-0506-0708-090A-0B0C0D0E0F10`
   * **Major:** `1` (or `0x0001`)
   * **Minor:** `2` (or `0x0002`)
4. **Filtering Tip:**
   In nRF Connect, type **`FF:EE`** or **`iBeacon`** in the search filter bar. Turn off the "Exclude nameless" filter option if it is enabled.

### 💡 Beginner Notes
* **Why nameless?** BLE packets have a tight size limit (37 bytes payload max in legacy advertising). Adding a text device name like "My_STM32_Beacon" takes up precious bytes that would otherwise fit the iBeacon's UUID and major/minor fields.
* **nRF Connect app:** This is the industry-standard developer tool for debugging Bluetooth. It listens to on-air advertising packets, parses the hex payload, and formats it into readable fields.

### 🔬 In-Depth Analysis
* **RSSI Calibration (`0xC5`):** The final byte in the payload (`0xC5` in hex is a two's-complement signed byte representing `-59` in decimal). This is the calibrated signal strength (RSSI) expected when a receiver is exactly 1 meter away from the transmitter.
* **Distance Estimation Formula:** Smartphone operating systems calculate the physical distance (proximity) using the Log-Distance Path Loss model:
  $$\text{Distance} = 10^{\frac{\text{Measured RSSI} - \text{Calibrated RSSI}}{10 \times n}}$$
  where $n$ is the path-loss exponent (typically $2.0$ in free space, and up to $4.0$ indoors).

---

## 7. How to Customize the Beacon

All modifications can be performed directly within `Core/Src/main.c`:

### Changing the MAC Address
Modify the `mac` array on line 268 of `main.c`. **Note that the array is in little-endian format (LSB-first):**
```c
// Example: Change MAC address to AA:BB:CC:11:22:33
uint8_t mac[6] = {0x33, 0x22, 0x11, 0xCC, 0xBB, 0xAA};
```

### Changing the UUID, Major, and Minor IDs
Modify the bytes inside `adv_data` on lines 244–267 of `main.c`:
```c
// To change UUID bytes (index 8 to 23 of adv_data):
0x01, 0x02, 0x03, 0x04, ... // Replace with your custom 16-byte UUID

// To change Major ID (index 24 & 25):
0x00, 0x05, // e.g. Major = 5

// To change Minor ID (index 26 & 27):
0x00, 0x0A  // e.g. Minor = 10
```

### Adjusting TX Output Power
In `SX1280_InitBLE()`, change the power parameter inside `cmd_tx_params` on line 164. The power range is from `-18 dBm` (`0x00`) to `+13 dBm` (`0x1F`):
```c
// Example: Set power to 0 dBm (0x12)
uint8_t cmd_tx_params[3] = {0x8E, 0x12, 0x20};
```

### 💡 Beginner Notes
* **Hexadecimal Conversions:** All hardware values are represented in hex (base 16, e.g. `0x0F` = 15). When editing, make sure you keep the `0x` prefix so the compiler knows it is a hex value.
* **Little-Endian Ordering:** If your MAC address is `11:22:33:44:55:66`, write it backwards: `{0x66, 0x55, 0x44, 0x33, 0x22, 0x11}`. If you do not, scanners will display the address in reverse.

### 🔬 In-Depth Analysis
* **PDU Size Management:** If you modify `adv_data` and add or remove bytes, you MUST update `pdu_len` on line 270. If the PDU length field inside the BLE packet header does not match the actual number of bytes written to the buffer, scanning smartphones will reject the packet as a malformed frame.
* **Power Scaling Table:** The power level byte parameter for the SX1280 command `0x8E` is mapped as follows:
  * `0x1F` $\approx +13 \text{ dBm}$
  * `0x18` $\approx +6 \text{ dBm}$
  * `0x12` $\approx 0 \text{ dBm}$
  * `0x0C` $\approx -6 \text{ dBm}$
  * `0x06` $\approx -12 \text{ dBm}$
  * `0x00` $\approx -18 \text{ dBm}$

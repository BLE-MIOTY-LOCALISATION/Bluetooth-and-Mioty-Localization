# FMDN Tracker — Google Find My Device Network on STM32 + SX1280

> **Date**: 2026-06-28  
> **Hardware**: STM32F103CB + Semtech SX1280  
> **Parent project**: `5_ble_mioty_ble_tx_ble_working_Mioty`  
> **Dependency**: `GoogleFindMyTools` (Python registration & location retrieval) — vendored at the repo root, two levels up from this file

---

## 1. What This Project Does

This firmware turns your STM32 + SX1280 board into a **Google Find My Device Network (FMDN) tracker**. Instead of broadcasting Apple iBeacon advertisements, it transmits FMDN service data that nearby Android phones can detect, report to Google's servers, and use to crowdsource the device's GPS location — all **without any GPS hardware on the tag**.

### How Tracking Works

```
Your tag (this firmware)          Nearby Android phones           Google servers
┌───────────────────────┐         ┌──────────────────┐           ┌─────────────┐
│ Broadcasts FMDN       │  BLE    │ "I heard EID X   │  upload   │ Stores      │
│ beacon every 1 second  │───────▶│  at MY GPS coords"│─────────▶│ encrypted   │
│ on CH37, CH38, CH39    │        │                   │          │ location    │
│                        │        │ (phone has GPS)   │          │ reports     │
│ No GPS needed!         │        │                   │          │             │
└───────────────────────┘         └──────────────────┘           └──────┬──────┘
                                                                        │
                                   You (Python script)                  │
                                   ┌──────────────────┐                 │
                                   │ Query + decrypt  │◀────────────────┘
                                   │ → lat, lng, time │
                                   └──────────────────┘
```

---

## 2. Project Structure

```
FMDN_tracker/
├── FMDN_tracker.md              ← This documentation file
├── .gitignore                    ← Build output exclusions
├── .project                      ← STM32CubeIDE project (name: FMDN_tracker)
├── .cproject                     ← Build configuration (ARM GCC, STM32F103CB)
├── .mxproject                    ← CubeMX project metadata
├── FMDN_tracker.ioc             ← CubeMX pin/peripheral configuration
├── STM32F103CBUX_FLASH.ld        ← Linker script (128K FLASH, 20K RAM)
├── Core/
│   ├── Inc/
│   │   ├── main.h                ← Pin definitions (LED, SPI, LORA_*)
│   │   ├── stm32f1xx_hal_conf.h  ← HAL module enables
│   │   └── stm32f1xx_it.h        ← IRQ prototypes
│   ├── Src/
│   │   ├── main.c                ← ★ FMDN beacon firmware (the key file)
│   │   ├── stm32f1xx_hal_msp.c   ← SPI1 pin muxing
│   │   ├── stm32f1xx_it.c        ← Cortex-M3 exception handlers
│   │   ├── syscalls.c            ← Newlib stubs
│   │   ├── sysmem.c              ← _sbrk heap implementation
│   │   └── system_stm32f1xx.c    ← System clock init
│   └── Startup/
│       └── startup_stm32f103cbux.s ← Reset vector + ISR table
└── Drivers/
    ├── CMSIS/                     ← ARM Cortex-M3 core headers
    └── STM32F1xx_HAL_Driver/      ← ST HAL library
```

---

## 3. Hardware Pin Mapping

| Signal | Pin | Port | Direction | Description |
|--------|-----|------|-----------|-------------|
| LED | PC13 | GPIOC | Output | Status LED (active-high) |
| LORA_NSS | PA4 | GPIOA | Output | SX1280 SPI chip select (active-low) |
| LORA_NRST | PA3 | GPIOA | Output | SX1280 hardware reset (active-low) |
| SPI1_SCK | PA5 | GPIOA | AF-PP | SPI clock |
| SPI1_MISO | PA6 | GPIOA | Input | SPI data in |
| SPI1_MOSI | PA7 | GPIOA | AF-PP | SPI data out |
| LORA_DIO1 | PB0 | GPIOB | Input | TxDone interrupt |
| LORA_BUSY | PB15 | GPIOB | Input | SX1280 busy signal |

---

## 4. FMDN Advertisement Format

The BLE advertisement PDU transmitted by this firmware:

```
┌──────────────────────────────────────────────────────────────────┐
│ BLE PDU Header (2 bytes)                                         │
│   [0] 0x42 = ADV_NONCONN_IND + TxAdd=1 (random address)         │
│   [1] 0x23 = PDU length (35 bytes)                               │
├──────────────────────────────────────────────────────────────────┤
│ MAC Address (6 bytes, LSB-first)                                 │
│   AC:BB:CC:DD:EE:FF                                              │
├──────────────────────────────────────────────────────────────────┤
│ AD Flags (3 bytes)                                               │
│   [0] 0x02 = Length                                              │
│   [1] 0x01 = Flags type                                          │
│   [2] 0x06 = LE General Discoverable + BR/EDR Not Supported      │
├──────────────────────────────────────────────────────────────────┤
│ FMDN Service Data (26 bytes)                                     │
│   [0]   0x19 = Length (25 bytes follow)                          │
│   [1]   0x16 = Service Data AD type                              │
│   [2-3] 0xAA 0xFE = FMDN 16-bit Service UUID                    │
│   [4]   0x41 = FMDN frame type (unwanted tracking protection)   │
│   [5-24] 20-byte Ephemeral Identifier (EID)                     │
│   [25]  0x00 = Hashed flags                                      │
└──────────────────────────────────────────────────────────────────┘
Total on-air: 2 + 6 + 3 + 26 = 37 bytes
```

---

## 5. How to Use — Step by Step

### Step 0: One-Time Environment Setup

Everything needed to talk to Google (register a tracker, retrieve locations, and run the `location_api` dashboard) lives in one local virtual environment, set up with a single script:

```bash
cd location_api
.\setup.ps1
```

This creates `location_api\venv\` and installs both `GoogleFindMyTools`'s dependencies and the dashboard's own — nothing is installed outside `location_api\`, and nothing outside it is required except `GoogleFindMyTools` itself, which is already vendored as a sibling folder at the repo root. Safe to re-run any time (skips venv creation if one already exists).

One thing this script *can't* do for you: the first-time Google login. That's an interactive, Chrome-based OAuth flow tied to your own Google account — there's no way to script around a real person logging in. Do that once, from `location_api\`, using the venv `setup.ps1` just created:

```bash
.\venv\Scripts\python.exe ..\..\GoogleFindMyTools\main.py
```

Chrome opens, you log in, and the device list appears. This same command is also how you register a new tracker and retrieve locations later (Steps 1 and 4 below) — just run it again.

### Step 1: Register a Tracker with Google

With the device list showing (from the command above), press **`r`** and hit Enter to register a new tracker. `GoogleFindMyTools` runs a pure cloud API call to Google's Spot backend (no ESP32/Zephyr-specific logic involved, despite the internal function being named `register_esp32` — an STM32 tracker consuming its output works identically) that:

1. Generates a random 32-byte key
2. Derives a 20-byte Ephemeral Identifier (EID) from it
3. Registers both with Google
4. Prints the EID once, in a box like this:

```
+------------------------------------------------------------------------------+
|                   a1b2c3d4e5f6071829304a5b6c7d8e9f0a1b2c3d                   |
|                             Advertisement Key                                |
+------------------------------------------------------------------------------+
```

**Copy that 40-character hex string** — it's shown only once. Note: the tool's terminal output calls this the **"Advertisement Key"** — that's the same thing this doc calls the EID, not a separate value. `ESP32Firmware/` and `ZephyrFirmware/` (in `GoogleFindMyTools`) consume this exact same output for their own trackers; this project just feeds it into STM32 firmware instead.

### Step 2: Paste EID into Firmware

Open `Core/Src/main.c` and find the `fmdn_eid` array:

```c
static const uint8_t fmdn_eid[20] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // ← INSERT
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // ← YOUR EID
};
```

Replace the zeros with your EID bytes. For example, if the tool gave you:
`a1b2c3d4e5f6071829304a5b6c7d8e9f0a1b2c3d`

Then set:
```c
static const uint8_t fmdn_eid[20] = {
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18, 0x29, 0x30,
    0x4A, 0x5B, 0x6C, 0x7D, 0x8E, 0x9F, 0x0A, 0x1B, 0x2C, 0x3D
};
```

This is what makes the tracker traceable: once flashed, the firmware broadcasts this exact EID over BLE. Any nearby Android phone that hears it reports it to Google, tagged with this EID — which Google's backend already associates with your account from the registration step above. That's the whole link between "a byte array in firmware" and "a pin on a map."

### Step 3: Build & Flash

1. Open the `7_ble_mioty_FMDN_tracker` folder in STM32CubeIDE
2. Build the project (Ctrl+B)
3. Flash to your board via ST-Link

### Step 4: Retrieve Locations

```bash
cd location_api
.\venv\Scripts\python.exe ..\..\GoogleFindMyTools\main.py
# Select your tracker number from the list
# -> Decrypted lat/lng/altitude + Google Maps link will be displayed
```

Or, for the visual dashboard instead of the command-line tool, see `location_api\walkthrough.md` — same underlying data, plotted on a live map (run `.\run_dashboard.ps1` after Step 0's setup).

---

## 6. Maintenance: 4-Day EID Refresh

The EID on the device **never changes**. However, the server-side "announcement" that tells Google's network to look for your EID **expires every 4 days** (96 hours).

**Easiest way to refresh**: just launch the dashboard as normal —
```bash
cd location_api
.\run_dashboard.ps1
```
`BLELocationService.__init__()` (`ble_location_service.py:20-21`) calls `refresh_devices()` automatically on startup, which calls `refresh_custom_trackers()` — the same 4-day announcement renewal `main.py` does. Starting the server is enough; you don't need to open the browser or separately run `main.py` just for this.

**Alternative**: re-run `python main.py` directly —
```bash
cd location_api
.\venv\Scripts\python.exe ..\..\GoogleFindMyTools\main.py
```
Also calls `UploadPrecomputedPublicKeyIds` to announce the next 4 days of EID slots. Equivalent to the dashboard method above; use this if you want the CLI device list instead of the map.

**To automate**: Set up a Windows Task Scheduler job or cron job to run either command every 3 days (a day's margin before the 4-day expiry).

---

## 7. Ultra-Low Power Profile & Battery Life

The firmware is heavily optimized for ultra-low power consumption. After rigorous hardware profiling, the device achieves near-perfect theoretical limits by meticulously managing GPIO states during the STM32's Stop Mode.

### Hardware Power Optimizations
1. **SX1280 Sleep Mode & Pin Clamping**: The SX1280 actively holds the `BUSY` pin HIGH while sleeping. To prevent a massive 82 µA leakage through the STM32's internal pull-down resistor, the `BUSY` and `DIO1` pins are dynamically shifted into `GPIO_MODE_ANALOG` just before entering Stop Mode.
2. **I2C Isolation**: The `PB10` and `PB11` pins connected to the IIS2MDCT magnetometer (which powers up into a 3 µA Idle Mode by default) are shifted into Analog mode before sleep to prevent pull-up leakage.
3. **SPI Float Prevention**: Unused SPI lines (`SCK`, `MISO`, `MOSI`) are explicitly clamped to `GPIO_PULLDOWN`.
4. **Debug Block Deactivation**: The firmware temporarily disables the SWD debugger during Stop Mode to prevent parasitic current draw.

### Current Draw

| Phase | Duration | Current Draw |
|-------|----------|-------------|
| TX burst (3 channels) | ~35-40 ms | ~29 mA (Peak), ~7.4 mA (Avg over 100ms) |
| LED flash (optional) | 10 ms | ~3.1 mA |
| **Stop Mode sleep (Base Board)** | Continuous | **~3 µA** |
| **Stop Mode sleep (w/ Magnetometer)** | Continuous | **~6 µA** |

### Estimated Battery Life (CR2032 - 200 mAh Usable)

Because the sleep baseline is a nearly invisible 3 µA, battery life is entirely dependent on the transmission interval (RTC alarm frequency) and whether the LED flash is enabled.

**Base Board (3 µA sleep) without LED flash:**
* **10-second interval**: ~107 days (3.5 months) @ 77.4 µA average
* **30-second interval**: ~300 days (10 months) @ 27.8 µA average
* **60-second interval**: ~541 days (1.5 years) @ 15.4 µA average

*(Note: Upgrading the battery to a CR2450 (600 mAh) will increase the 30-second interval battery life to over 2.5 years).*

---

## 8. Differences from BLE_tx_battery

| Aspect | BLE_tx_battery | FMDN_tracker |
|--------|---------------|--------------|
| **AD payload** | Apple iBeacon (30 bytes) | FMDN Service Data (29 bytes) |
| **Service UUID** | None (Manufacturer Specific) | 0xFEAA (FMDN) |
| **Tracking** | Requires custom BLE scanner | Google Find My Device Network |
| **PDU length** | 38 bytes | 37 bytes |
| **Function name** | `SX1280_SendBLEBeacon()` | `SX1280_SendFMDNBeacon()` |
| **Everything else** | Identical | Identical |

---

## 9. Known Limitations

- **No Google app visibility**: Locations are only accessible via the Python script, not the Google Find My Device mobile app
- **Static MAC address**: No rotating MAC addresses (privacy limitation)
- **No Fast Pair**: The tracker doesn't support Google's Fast Pair protocol
- **Crowd-dependent**: Location accuracy depends on nearby Android phones with Find My Device enabled
- **4-day refresh required**: Server-side EID announcements must be renewed periodically
- **Board 2 (J1 coaxial)**: If using Board 2 without an external antenna, change TX power to +13 dBm (`0x1F`) in `SX1280_InitBLE()` for trace leakage to reach nearby phones

---

## 10. Future Improvements

- [ ] Build Leaflet.js web dashboard for map visualization
- [ ] Automate 4-day EID refresh with scheduled task
- [ ] Add motion detection (requires accelerometer) for power-gated beaconing
- [ ] Implement MIOTY uplink for dual-network tracking (BLE + MIOTY)

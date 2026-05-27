# Bluetooth and Mioty Localization Project

## Overview
This project is a hardware design for a joint Bluetooth + Mioty localization module. It appears to be centered around a KiCad PCB project named **BLE MIOTY MODULE**.

## Repository Structure
- `BOM.xlsx` - component list for the design.
- `Datasheets/` - vendor footprints and datasheets for components such as ICM-20948, SX1280, crystals, batteries, and other parts.
- `Documentations/` - currently empty.
- `KICAD/` - KiCad project repository for the board.
  - `BLE_MIOTY_MODULE/` - main project folder.
    - `BLE MIOTY MODULE.kicad_sch` - schematic file for the module.
    - `BLE MIOTY MODULE.kicad_pcb` - PCB layout file.
    - `BLE MIOTY MODULE.kicad_prl` - project release file.
    - `BLE MIOTY MODULE.kicad_pro` - KiCad project settings.
    - `fp-info-cache` - footprint cache.
    - `sym-lib-table` - symbol library references.
    - `BLE MIOTY MODULE-backups/` - backup files.
- `Obsidian Notes/` - empty folder, now contains this project summary.
- `Reference Designs/` - includes two PDFs:
  - `Development of Joint Bluetooth and mioty Localization Module.pdf`
  - `schematic.pdf`
- `STM Codes/` - currently empty.
- `NOTES.docx` and `~$NOTES.docx` - additional notes in Word format.

## Main Design Components
### Core ICs
- **STM32F103CBUx**
  - Main MCU.
  - 128 KB Flash, 20 KB RAM, 72 MHz Cortex-M3.
  - Footprint: `QFN-48-1EP_7x7mm_P0.5mm_EP5.6x5.6mm`.
  - Datasheet: STMicroelectronics STM32F103CB.

- **SX1280IMLTRT**
  - Semtech 2.4 GHz RF transceiver, LoRa 802.15.4.
  - Footprint: `XCVR_SX1280IMLTRT`.
  - Likely used for Mioty / 2.4 GHz radio functionality.

- **IIS2MDCTR**
  - ST magnetometer sensor.
  - Footprint: `LGA-12_2X2X0P7_STM`.
  - Datasheet: ST IIS2MDCTR.

### Power and Timing
- **CR2032 battery holder / battery**
  - Battery component appears in schematic.
- **32.768 kHz crystal**
  - Component `ABS07-32.768KHZ-7-T` likely for RTC oscillator.
- **52 MHz crystal**
  - Component `NX2016SA-52MHZ-EXS00A-CS06016` likely for RF or MCU clock.

### RF / Antenna
- **Antenna shield / antenna component**
  - The schematic includes an `Antenna_Shield` symbol.
- **Coaxial connector**
  - Interface for external RF connection.
- **U.FL / RF connector**
  - Additional RF connector may exist.

### Connectors and Interfaces
- **Generic 1x4 connector**
  - Likely used for external I/O or power programming.
- **Multiple nets and signals**
  - MCU boot and reset lines: `BOOT0`, `NRST`, `NRESET`.
  - SPI signals for SX1280: `NSS`, `SCK`, `MOSI`, `MISO`, `BUSY`, `DIO1`, `DIO2`, `DIO3`.
  - I2C / sensor lines: `SCL`, `SDA`.
  - Interrupts for sensor and radio: `INT1A`, `INT2A`, `INT1M`.

## PCB Details
- KiCad version: 2025.01.14 schematic, PCB 2024.12.29.
- Board uses a **4-layer stackup**:
  - Top copper, two internal copper layers, bottom copper.
  - FR4 with 1.6 mm thickness.
- Power nets present: `VCC, 3.3V`; `VCC, 3.3V RAW`; `GND`.
- Many unconnected MCU pins remain, indicating a custom subset of MCU I/O is used.

## Notes on Missing or Empty Content
- `Documentations/` is empty in the workspace.
- `STM Codes/` is empty, so firmware sources are not present here.
- There is no Markdown README or prose design notes in the repo.
- The project appears to be hardware-focused, with supplementary notes in `NOTES.docx` and reference PDF files.

## Suggested Next Steps
- Open `Reference Designs/Development of Joint Bluetooth and mioty Localization Module.pdf` for design goals and architecture.
- Review `BOM.xlsx` for exact part numbers and quantities.
- Open `KICAD/BLE_MIOTY_MODULE/BLE MIOTY MODULE.kicad_sch` in KiCad to inspect schematic connectivity and functional blocks.
- Open `KICAD/BLE_MIOTY_MODULE/BLE MIOTY MODULE.kicad_pcb` in KiCad for layout, RF routing, and board constraints.
- Document missing firmware or software interfaces if you add code later.

## Tags
#hardware #KiCad #BLE #Mioty #RF #STM32 #PCB #project-summary

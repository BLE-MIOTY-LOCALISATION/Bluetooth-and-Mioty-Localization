# BLE Mioty Module STM32 Project

This is a complete STM32 firmware project for the BLE Mioty board using an `STM32F103CB` MCU.

## Project structure

- `platformio.ini` - PlatformIO build configuration
- `src/main.c` - Blink LED firmware using STM32 HAL

## How to build

1. Install PlatformIO in VS Code or use the PlatformIO Core CLI.
2. Open this folder in VS Code or run from terminal:
   ```powershell
   cd "STM Codes\BLE_Mioty_Module"
   pio run
   ```

## How to flash

1. Connect your ST-LINK to the board.
2. Run:
   ```powershell
   pio run -t upload
   ```

## LED pin

- The board LED is connected to `PC13`
- `PC13 = LOW` turns the LED on
- `PC13 = HIGH` turns the LED off

## Notes

If your board uses an external crystal, the clock configuration assumes an HSE crystal and a 72 MHz system clock.
If you do not have an external crystal, change `SystemClock_Config()` to use HSI instead.

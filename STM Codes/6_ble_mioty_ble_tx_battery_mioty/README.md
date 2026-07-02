# BLE + mioty Module: Battery BLE + mioty TX Baseline

This STM32CubeIDE project is the project 6 branch of the non-magnetometer low-power BLE transmit baseline for the BLE + mioty localization module.

The firmware currently wakes periodically, transmits one BLE iBeacon advertisement set on the three BLE advertising channels, puts the SX1280 radio to sleep, enters STM32 Stop mode, and wakes again from the RTC alarm. Project 6 is reserved for adding mioty transmission on top of this BLE battery baseline.

Project name:

```text
ble_mioty_ble_tx_battery_mioty
```

CubeMX file:

```text
ble_mioty_ble_tx_battery_mioty.ioc
```

This is the battery-focused BLE + mioty transmission work branch. Project 3 remains the stable BLE-only battery baseline, project 4 contains magnetometer experiments, and project 5 is reserved for the incoming colleague project.

## Hardware

Main devices:

| Device | Purpose |
| :--- | :--- |
| STM32F103CBUx | Main MCU |
| Semtech SX1280 | 2.4 GHz radio used here for BLE advertising |
| LSE 32.768 kHz crystal | RTC timebase during Stop mode |
| LED on PC13 | Visual boot and wake/debug indication |

STM32 to SX1280 pins:

| Signal | STM32 Pin | Direction | Notes |
| :--- | :--- | :--- | :--- |
| SPI1_SCK | PA5 | STM32 to SX1280 | SPI clock |
| SPI1_MISO | PA6 | SX1280 to STM32 | SPI data to MCU |
| SPI1_MOSI | PA7 | STM32 to SX1280 | SPI data to radio |
| LORA_NSS | PA4 | STM32 to SX1280 | Software chip select, active low |
| LORA_NRST | PA3 | STM32 to SX1280 | Radio reset, active low |
| LORA_DIO1 | PB0 | SX1280 to STM32 | TxDone indication |
| LORA_BUSY | PB15 | SX1280 to STM32 | Radio busy indication |
| LED | PC13 | STM32 output | Active high in this board/code |

## Current Firmware Behavior

At boot:

1. Waits briefly so ST-Link can reconnect after flashing.
2. Initializes HAL, GPIO, SPI1, and the 8 MHz HSE system clock.
3. Hardware-resets the SX1280 using `LORA_NRST`.
4. Reads SX1280 status over SPI.
5. Blinks once if the SX1280 responds.
6. Configures SX1280 for BLE advertising.
7. Performs register readback diagnostics.
8. Initializes the RTC using the external LSE clock.
9. Enters the repeating BLE transmit / Stop-mode cycle.

Expected startup LED sequence when the radio path is healthy:

```text
1, 9, 6, 3, 1
```

Meaning:

| Blink Group | Meaning |
| :--- | :--- |
| 1 | SX1280 responded over SPI |
| 9 | Access address register readback starts with `0x8E` |
| 6 | CRC seed register readback starts with `0x55` |
| 3 | Radio is in expected standby mode |
| 1 | Command status is idle/OK |

If the SX1280 status is `0x00` or `0xFF`, the firmware enters a slow 1-second LED blink forever. That usually means SPI wiring, radio reset, radio power, or chip-select is wrong.

If `SX1280_WaitBusy()` times out, the firmware enters a fast 50 ms blink forever. That usually means the radio is stuck busy, reset is wrong, or the SPI/radio state machine is broken.

## BLE Advertisement

Each wake cycle transmits one advertising set: one packet on each BLE advertising channel.

| BLE Channel | Frequency | SX1280 Frequency Bytes | Whitening Seed |
| :--- | :--- | :--- | :--- |
| 37 | 2402 MHz | `0xB8, 0xC4, 0xEC` | `0x53` |
| 38 | 2426 MHz | `0xBA, 0x9D, 0x89` | `0x33` |
| 39 | 2480 MHz | `0xBE, 0xC4, 0x55` | `0x73` |

The Channel 39 value is the corrected exact 2480 MHz value. Older code used `0xBE, 0xC4, 0xEC`, which is slightly offset.

Payload type:

```text
ADV_NONCONN_IND iBeacon
```

Beacon identity:

| Field | Value |
| :--- | :--- |
| BLE address | `FF:EE:DD:CC:BB:AC` |
| UUID | `01020304-0506-0708-090A-0B0C0D0E0F10` |
| Major | `1` |
| Minor | `2` |
| Measured power byte | `0xC5` (`-59 dBm`) |

The radio TX power is currently:

```c
uint8_t cmd_tx_params[3] = {0x8E, 0x12, 0x20};
```

`0x12` corresponds to approximately 0 dBm. This is much friendlier to a CR2032 than the earlier `0x1F` / +13 dBm setting.

## Clocks

This project intentionally avoids the 72 MHz PLL configuration.

| Clock | Current Use |
| :--- | :--- |
| HSE 8 MHz | Main STM32 system clock while awake |
| PLL | Disabled |
| SPI1 clock | 8 MHz APB2 / 2 = 4 MHz SPI |
| LSE 32.768 kHz | RTC clock during Stop mode |
| HSI 8 MHz | Temporary clock immediately after waking from Stop mode, before `SystemClock_Config()` restores HSE |

Why this matters:

- Running the MCU at 8 MHz instead of 72 MHz greatly reduces active current.
- The SX1280 generates the RF carrier and BLE modulation using its own radio clocking, so lowering the STM32 clock does not change the on-air BLE frequency or bitrate.
- The RTC uses LSE so the wake interval is stable and accurate while the main CPU clock is stopped.

## Low-Power Routine

The main loop is:

```c
while (1)
{
  SX1280_SendBLEBeacon();
  STM32_EnterStopMode();
}
```

`STM32_EnterStopMode()` does the low-power handoff:

1. Sends `SX1280_Sleep()` using opcode `0x84, 0x01`.
2. Flashes the LED for 10 ms after RF transmit is complete.
3. Disables SPI1.
4. Reconfigures SPI pins `PA5`, `PA6`, `PA7` as inputs with pulldown.
5. Reconfigures `PB0` and `PB15` as inputs with pulldown.
6. Keeps `PA4` NSS and `PA3` NRST driven high.
7. Programs the RTC alarm for current RTC counter + 1 second.
8. Suspends SysTick.
9. Enters STM32 Stop mode with low-power regulator.
10. Wakes from RTC alarm on EXTI line 17.
11. Restores HSE system clock.
12. Resumes SysTick.
13. Calls `HAL_SPI_DeInit(&hspi1)` and `MX_SPI1_Init()` so SPI pins are restored correctly.
14. Restores radio status pins as inputs with no pull.
15. Wakes the SX1280.
16. Re-runs `SX1280_InitBLE()` before the next transmit.

The `HAL_SPI_DeInit()` before `MX_SPI1_Init()` is important. In Stop mode, RAM is retained, so the HAL SPI handle can still look initialized even though the pins were manually changed for sleep. De-initializing first forces the HAL MSP init path to restore the SPI GPIO alternate functions.

## RTC Wakeup

The RTC is configured manually in `RTC_Init_LowPower()`:

- Backup domain access is enabled.
- LSE oscillator is enabled.
- RTC clock source is set to LSE.
- Prescaler is set to `32767`, producing a 1 Hz RTC tick from 32.768 kHz.
- RTC alarm interrupt is enabled through EXTI line 17.
- `RTC_Alarm_IRQHandler()` is implemented in `main.c`.

The interrupt handler clears:

- RTC alarm interrupt enable.
- RTC alarm flag.
- EXTI pending bit for line 17.

Then it sets:

```c
alarm_fired = 1;
```

At the moment this flag is diagnostic/state information; the Stop-mode wake itself is what resumes the main loop.

## Current Measurement Notes

Two debug features are useful while developing but should be disabled for serious battery measurements:

1. The 10 ms LED flash in `STM32_EnterStopMode()`.
2. `HAL_DBGMCU_EnableDBGStopMode()`.

Leaving debug enabled can keep extra debug circuitry alive in Stop mode. That is helpful when using ST-Link, but it makes the measured sleep current higher than the real production configuration.

For final current tests:

- Disable the 10 ms LED flash.
- Disable debug in Stop mode.
- Use a fresh CR2032 or a current-limited lab supply.
- Add a local low-ESR reservoir capacitor near the battery/radio supply.
- Confirm the antenna path is populated correctly.

## CR2032 Considerations

A CR2032 can have around 220 mAh nominal capacity, but it cannot comfortably provide large current pulses. Its internal resistance causes voltage sag during high-current bursts.

This firmware reduces peak and average current by:

- Lowering the STM32 active clock from 72 MHz to 8 MHz.
- Lowering SX1280 TX power to about 0 dBm.
- Putting the SX1280 into sleep between advertisements.
- Putting the STM32 into Stop mode between advertisements.
- Keeping the LED off except for the short debug flash.

The current 1-second advertising interval is good for bring-up and testing, but it is aggressive for 1-2 year coin-cell life. Longer intervals such as 5, 10, 15, or 20 seconds are more realistic depending on required discovery latency.

Approximate battery-life direction:

| Advertising Interval | Expected Effect |
| :--- | :--- |
| 1 second | Good discovery, poor long-term CR2032 life |
| 5 seconds | Better battery life, still reasonably discoverable |
| 10-20 seconds | More realistic for long-life tags, slower discovery |
| 15 seconds sleep + multi-second TX burst | Can improve discovery windows but must be measured carefully |

Actual lifetime must be measured on hardware. BLE scanner behavior, receiver scan windows, antenna quality, reservoir capacitance, and coin-cell ESR matter a lot.

## Known Hardware/RF Notes

Some board variants may have a coax connector or antenna routing option. If the PCB antenna path is not populated, low-power BLE transmission may appear weak or missing even though the firmware is working.

Useful checks:

- Confirm whether the PCB antenna path or external antenna path is actually connected.
- If no antenna is connected, +13 dBm code may still appear detectable nearby through leakage, while 0 dBm may not.
- Do not use +13 dBm as a battery-life configuration; use it only as a temporary RF path diagnostic.

## How To Build And Flash

Open the folder as an STM32CubeIDE project and use:

```text
ble_mioty_ble_tx_battery_mioty.launch
```

Expected build artifact:

```text
Debug/ble_mioty_ble_tx_battery_mioty.elf
```

The project was not build-tested from this shell because `arm-none-eabi-gcc` and make tools are not available on PATH here. Build once in CubeIDE before pushing.

## Files Of Interest

| File | Purpose |
| :--- | :--- |
| `Core/Src/main.c` | Main application, SX1280 BLE routines, RTC/Stop-mode logic |
| `Core/Inc/main.h` | Pin definitions |
| `Core/Src/stm32f1xx_hal_msp.c` | SPI MSP/GPIO setup |
| `Core/Startup/startup_stm32f103cbux.s` | Vector table, includes `RTC_Alarm_IRQHandler` |
| `ble_mioty_ble_tx_battery_mioty.ioc` | CubeMX project configuration |
| `ble_mioty_ble_tx_battery_mioty.launch` | CubeIDE debug/flash launch configuration |

## Project Lineage

Project 6 starts from project 3, which is intentionally BLE-only and does not include the IIS2MDC magnetometer.

Project 4 adds:

- I2C2 on PB10/PB11.
- MAG_INT on PB12 / EXTI15_10.
- IIS2MDC WHO_AM_I and orientation test code.
- A longer startup blink sequence ending in magnetometer status.

Keep project 3 as the clean default low-power BLE baseline. Use project 6 for BLE battery behavior plus the upcoming mioty transmission work.

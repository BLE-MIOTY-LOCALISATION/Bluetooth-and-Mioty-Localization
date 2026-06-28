# BLE + mioty Battery Beacon With Magnetometer

This STM32CubeIDE project is the magnetometer bring-up and low-power experiment branch for the BLE + mioty localization module.

It starts from the low-power BLE beacon baseline and adds IIS2MDC magnetometer support over I2C2. On every wake cycle, the STM32 takes one low-power magnetometer sample, blinks an orientation result, transmits one BLE iBeacon advertising set, then returns to Stop mode.

Project name:

```text
ble_mioty_ble_tx_battery_mag
```

CubeMX file:

```text
ble_mioty_ble_tx_battery_mag.ioc
```

Build artifact:

```text
Debug/ble_mioty_ble_tx_battery_mag.elf
```

## Purpose

This project is not the final production tracking firmware yet. It is a controlled hardware bring-up application for answering these questions:

- Is the SX1280 BLE transmit path still working after low-power sleep/wake?
- Is the IIS2MDC magnetometer physically present and readable over I2C2?
- Can the STM32 wake, sample the magnetometer, transmit BLE, and sleep repeatedly?
- Does the board orientation reading change when the board is rotated?
- What code structure should later support motion, tamper, orientation, and low-power experiments?

Project 3 remains the clean non-magnetometer BLE battery baseline. Project 4 is where magnetometer and further low-power experiments can evolve.

## Hardware Map

Main devices:

| Device | Role |
| :--- | :--- |
| STM32F103CBUx | Main MCU |
| Semtech SX1280 | 2.4 GHz radio used for BLE advertising |
| ST IIS2MDC | 3-axis magnetometer |
| LSE 32.768 kHz crystal | RTC timebase during Stop mode |
| LED on PC13 | Visual status and test output |

STM32 pin map:

| Signal | STM32 Pin | Direction | Notes |
| :--- | :--- | :--- | :--- |
| LED | PC13 | Output | Active high in this firmware |
| LORA_NSS | PA4 | Output | SX1280 chip select, active low |
| SPI1_SCK | PA5 | AF output | SX1280 SPI clock |
| SPI1_MISO | PA6 | AF input | SX1280 SPI data to MCU |
| SPI1_MOSI | PA7 | AF output | SX1280 SPI data from MCU |
| LORA_DIO1 | PB0 | Input | SX1280 TxDone |
| LORA_NRST | PA3 | Output | SX1280 reset, active low |
| LORA_BUSY | PB15 | Input | SX1280 busy |
| I2C2_SCL | PB10 | AF open-drain | IIS2MDC I2C clock |
| I2C2_SDA | PB11 | AF open-drain | IIS2MDC I2C data |
| MAG_INT | PB12 | EXTI input | IIS2MDC interrupt pin, configured but not used by current logic |
| LSE_IN | PC14 | Oscillator | 32.768 kHz crystal |
| LSE_OUT | PC15 | Oscillator | 32.768 kHz crystal |
| HSE_IN | PD0 | Oscillator | 8 MHz crystal |
| HSE_OUT | PD1 | Oscillator | 8 MHz crystal |

## Current Runtime Behavior

Boot sequence:

1. Initialize HAL, clocks, GPIO, SPI1, and I2C2.
2. Hard-reset the SX1280 using `LORA_NRST`.
3. Read SX1280 status over SPI.
4. Blink once if the radio responds.
5. Configure the SX1280 for BLE advertising at 0 dBm.
6. Read back key SX1280 registers and blink diagnostics.
7. Read IIS2MDC `WHO_AM_I`.
8. Take one magnetometer sample and store it as the boot reference direction.
9. Blink magnetometer result.
10. Initialize RTC alarm wakeup using LSE.
11. Enter the repeating test loop.

Expected healthy startup blink sequence:

```text
1, 9, 6, 3, 1, 2
```

Meaning:

| Blink Group | Meaning |
| :--- | :--- |
| 1 | SX1280 responded over SPI |
| 9 | BLE access address register readback looks correct |
| 6 | BLE CRC seed register readback looks correct |
| 3 | SX1280 circuit mode is expected |
| 1 | SX1280 command status is expected |
| 2 | IIS2MDC `WHO_AM_I` passed and boot reference sample was captured |

If the final group is `8`, the magnetometer check failed. Likely causes are missing I2C pullups, wrong soldering, wrong address, sensor not powered, or I2C bus wiring issues.

Main loop:

```c
while (1)
{
  LED_Blink(IIS2MDC_GetOrientationBlinkCount());
  SX1280_SendBLEBeacon();
  STM32_EnterStopMode();
}
```

Current wake interval:

```text
1 second
```

This short interval is intentional for bench testing. It makes orientation response easy to observe. It is not intended as a final CR2032 lifetime configuration.

## Orientation Test Logic

The magnetometer test is deliberately simple:

1. During boot, hold the board in the direction you want to treat as the reference direction.
2. The firmware reads one IIS2MDC sample and stores the X/Y vector.
3. On each later wake, it reads a new X/Y vector.
4. It compares the new vector with the boot vector using a dot product.
5. It blinks the result.

Blink meanings during the repeating loop:

| Blink Count | Meaning |
| :--- | :--- |
| 1 | Current direction is roughly the same as boot reference |
| 2 | Current direction is roughly opposite the boot reference |
| 3 | Current direction is sideways, unclear, or outside the simple same/opposite sectors |
| 8 | Magnetometer read/reference error |

Important limitations:

- This is a bring-up test, not a calibrated compass.
- It assumes the board is kept approximately flat.
- It does not compensate for hard-iron or soft-iron magnetic distortion.
- It does not know true geographic north unless the boot reference was physically aligned that way.
- Nearby magnets, motors, speakers, steel tables, batteries, and lab tools can strongly disturb the reading.

For package orientation or flip detection, an accelerometer is normally the better sensor. The magnetometer is useful here for magnetic orientation, magnetic tamper, docking/attachment detection, and checking whether the sensor and I2C/low-power flow are alive.

## IIS2MDC Configuration

IIS2MDC constants used by the firmware:

| Item | Value |
| :--- | :--- |
| 7-bit I2C address | `0x1E` |
| HAL I2C address | `(0x1E << 1)` |
| `WHO_AM_I` register | `0x4F` |
| Expected `WHO_AM_I` | `0x40` |
| Data-ready status bit | `STATUS_REG.ZYXDA` / `0x08` |
| Output registers | `0x68` to `0x6D` |

Sampling routine:

1. Enable block data update using `CFG_REG_C`.
2. Start one low-power single measurement using `CFG_REG_A`.
3. Poll status until new XYZ data is ready.
4. Read six bytes from the output registers.
5. Convert them into signed `int16_t` X/Y/Z values.
6. Put the sensor back into idle low-power mode.

The current firmware uses single-shot style reads instead of leaving the magnetometer continuously measuring. This is friendlier to low-power experiments because the sensor only consumes measurement current briefly on each scheduled wake.

## BLE Advertisement

Each loop transmits one non-connectable iBeacon advertisement on all three BLE advertising channels.

| Channel | Frequency | SX1280 Frequency Bytes | Whitening Seed |
| :--- | :--- | :--- | :--- |
| 37 | 2402 MHz | `0xB8, 0xC4, 0xEC` | `0x53` |
| 38 | 2426 MHz | `0xBA, 0x9D, 0x89` | `0x33` |
| 39 | 2480 MHz | `0xBE, 0xC4, 0x55` | `0x73` |

Beacon fields:

| Field | Value |
| :--- | :--- |
| BLE address | `FF:EE:DD:CC:BB:AC` |
| UUID | `01020304-0506-0708-090A-0B0C0D0E0F10` |
| Major | `1` |
| Minor | `2` |
| Measured power byte | `0xC5` (`-59 dBm`) |
| TX power command | `0x8E, 0x12, 0x20` |

`0x12` is approximately 0 dBm output power. This is useful for coin-cell testing and avoids the large current peaks of the earlier +13 dBm setting.

## Clocks And Low Power

Clock choices:

| Clock | Use |
| :--- | :--- |
| HSE 8 MHz | Main system clock while awake |
| PLL | Disabled |
| SPI1 | APB2 8 MHz / 2 = 4 MHz SPI |
| I2C2 | 100 kHz standard-mode I2C |
| LSE 32.768 kHz | RTC alarm wake source during Stop mode |
| HSI 8 MHz | Temporary clock immediately after Stop wake before `SystemClock_Config()` restores HSE |

Low-power cycle:

1. Read magnetometer.
2. Send BLE advertisement on channels 37, 38, and 39.
3. Put SX1280 into sleep using opcode `0x84, 0x01`.
4. Disable SPI1.
5. Clamp SPI and radio status pins to pulldown input to avoid floating/leakage behavior.
6. Keep `LORA_NSS` and `LORA_NRST` driven high.
7. Set RTC alarm for current counter + 1 second.
8. Suspend SysTick.
9. Enter STM32 Stop mode with low-power regulator.
10. Wake from RTC alarm through EXTI line 17.
11. Restore HSE system clock.
12. Resume SysTick.
13. De-init/re-init SPI1.
14. De-init/re-init I2C2 for the next magnetometer read.
15. Wake SX1280 and re-run BLE radio configuration.

The explicit `HAL_SPI_DeInit()` before `MX_SPI1_Init()` and `HAL_I2C_DeInit()` before `MX_I2C2_Init()` are intentional. Stop mode retains RAM, so HAL handles can look initialized even though pins/peripherals were changed for sleep. De-initializing forces the MSP init path to restore the GPIO alternate functions cleanly.

## Interrupts

Currently configured interrupts:

| Interrupt | Purpose |
| :--- | :--- |
| `RTC_Alarm_IRQHandler` | Wakes the STM32 from Stop mode every second |
| `EXTI15_10_IRQHandler` | Handles `MAG_INT` / PB12 if the magnetometer interrupt is later enabled |

Current magnetometer logic does not rely on `MAG_INT` yet. It performs scheduled single-shot reads on the RTC wake cycle. The interrupt pin is reserved for later experiments such as threshold, magnetic event, or wake-on-field-change behavior.

## What To Observe When Flashing

Healthy boot:

```text
1, 9, 6, 3, 1, 2
```

Then every second:

```text
1 blink  = same direction as boot reference
2 blinks = opposite direction
3 blinks = sideways/unclear
8 blinks = magnetometer read/reference problem
```

Expected BLE behavior:

- A BLE scanner such as nRF Connect should see a nameless iBeacon.
- UUID should be `01020304-0506-0708-090A-0B0C0D0E0F10`.
- Major should be `1`.
- Minor should be `2`.

## Development Notes

For easy bench testing, this firmware intentionally keeps:

- 1-second wake interval.
- LED blink feedback.
- Debug support in Stop mode via `HAL_DBGMCU_EnableDBGStopMode()`.

For real battery-current measurement, disable:

- The per-cycle orientation LED blink.
- Debug-in-Stop support.
- Any unnecessary GPIO/peripheral clocks.

Also test with:

- A known-good 2.4 GHz antenna path.
- A reservoir capacitor near the radio/battery supply.
- I2C pullups populated on SCL/SDA.
- A current probe or power analyzer that can capture TX pulses and sleep current.

## Known Limitations

- The orientation logic is intentionally coarse.
- No magnetometer calibration is performed.
- `MAG_INT` is configured but not functionally used yet.
- BLE payload does not yet include magnetometer/orientation data.
- The device still transmits once every second, which is too aggressive for 1-2 year CR2032 life.
- mioty transmission is not integrated in this project yet.

## Recommended Next Steps

Good next experiments:

1. Increase sleep interval to 5, 10, 15, or 20 seconds and measure discovery behavior.
2. Add a compile-time or runtime switch to disable LED feedback for current measurement.
3. Add magnetometer calibration storage or at least min/max field tracking.
4. Encode orientation state into BLE Major/Minor or manufacturer data for receiver-side logging.
5. Try IIS2MDC threshold/interrupt features on `MAG_INT`.
6. Compare scheduled magnetometer reads against interrupt-driven magnetic event wakeups.
7. Later, add mioty transmission as a lower-rate long-range reporting path.

## Build And Flash

Use STM32CubeIDE and the launch configuration:

```text
ble_mioty_ble_tx_battery_mag.launch
```

Expected ELF:

```text
Debug/ble_mioty_ble_tx_battery_mag.elf
```

This README was prepared from the local source tree. Build once in CubeIDE before flashing/pushing, because the ARM GCC toolchain is not available on this shell PATH.

## Project Boundary

Use this project for:

- Magnetometer bring-up.
- Orientation/magnetic experiments.
- Low-power BLE + sensor wake/sleep experiments.

Use project 3 for:

- The clean BLE-only low-power baseline.
- Regression testing when magnetometer code is suspected of causing trouble.

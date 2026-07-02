# Walkthrough: Standalone Mioty Integration

This document walks through the files created and modified to implement standalone C++ Mioty transmissions every 5 minutes on the STM32.

---

## 1. Directory Structure of Completed Project

The project is structured under **`3_MIOTY_standalone_working`**:

```text
3_MIOTY_standalone_working/
├── .project / .cproject            # Eclipse project files (convert to C++ Nature in IDE)
├── Core/
│   ├── Inc/
│   │   ├── main.h                  # Common defines and peripheral exports
│   │   ├── STM32TsUnb.h            # NEW: STM32 platform timing & SPI wrapper
│   │   ├── STM32TsUnbTemplates.h   # NEW: TS-UNB template mapping for STM32
│   │   ├── Encryption/             # Fraunhofer Aes128 C++ engine
│   │   ├── Trx/                    # Fraunhofer SX1280 driver
│   │   ├── TsUnb/                  # Fraunhofer TS-UNB physical layer templates
│   │   └── Utils/                  # Common utilities (contains BitAccess.h)
│   └── Src/
│       └── main.cpp                # MODIFIED: Core application entry point (renamed from main.c)
└── testing_plan.md                 # NEW: Documentation of what was done and testing procedures
```

---

## 2. Technical Implementations

### C++ Hardware Abstraction (`STM32TsUnb.h`)
* Implements SPI transmit/receive wrappers to forward calls to the STM32 HAL `HAL_SPI_Transmit` and `HAL_SPI_TransmitReceive`.
* Uses the **Cortex-M3 DWT (Data Watchpoint and Trace) cycle counter** (`DWT->CYCCNT`) for sub-microsecond symbol timing (420.1 microseconds per symbol duration).
* Employs a state tracker `is_sleeping` to introduce a 40-microsecond wakeup delay on the first SPI transaction after the radio is put to sleep, ensuring clean wakeups during burst iterations.

### Standalone Main Loop (`main.cpp`)
* Migrated to C++ and includes the Mioty templates via `STM32TsUnbTemplates.h`.
* Configures network key and EUI64 parameters.
* Triggers a brief 100ms LED blink at the start of each transmission cycle.
* Transmits an incrementing packet counter in a 20-byte payload using the Mioty simple node (`TsUnb_Node.send(...)`).
* Executes an active wait of 5 minutes (`HAL_Delay(300000)`) between transmission bursts, keeping the MCU active (without low-power Stop Mode states or clamping SPI pins).

---

## 3. Verification & Testing

For complete instructions on building the code, measuring timing intervals, and sniffing on-air packets with SDR (Software Defined Radio), please refer to the newly created [testing_plan.md](file:///c:/Users_windows/Chandu%20B%20Reddy/Projects/LOcalee/sx1280-bring%20up/3_MIOTY_standalone_working/testing_plan.md).

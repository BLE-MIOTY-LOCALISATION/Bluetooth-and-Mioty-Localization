/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Google FMDN Tracker — STM32F103CB + SX1280
 *
 *                   Transmits Google Find My Device Network (FMDN) BLE
 *                   advertisements on channels 37/38/39 using the SX1280
 *                   2.4 GHz transceiver over SPI. Ultra-low-power design
 *                   with RTC-driven 10-second Stop Mode sleep cycle.
 *
 *                   Based on BLE_tx_battery iBeacon firmware, with the
 *                   advertisement payload replaced by FMDN service data.
 *
 * @date           : 2026-06-28
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// -----------------------------------------------------------------------
// Magnetometer joulemeter validation additions -- everything else in this
// file is an unmodified copy of 7_ble_mioty_FMDN_tracker's proven ~5uA
// Stop-mode design. Only the main loop gains a marker blink + single-shot
// IIS2MDC read after each FMDN beacon send; STM32_EnterStopMode's GPIO
// leakage-prevention (already written anticipating IIS2MDC use -- see its
// "Set PB10/PB11 for IIS2MDCT to Analog mode" step) now actually gets used.
// -----------------------------------------------------------------------
#define IIS2MDC_I2C_ADDR        (0x1E << 1)
#define IIS2MDC_WHO_AM_I_REG    0x4F
#define IIS2MDC_WHO_AM_I_VALUE  0x40
#define IIS2MDC_CFG_REG_A       0x60
#define IIS2MDC_CFG_REG_C       0x62
#define IIS2MDC_STATUS_REG      0x67
#define IIS2MDC_OUTX_L_REG      0x68
#define IIS2MDC_STATUS_ZYXDA    0x08
#define IIS2MDC_AUTO_INCREMENT  0x80
#define IIS2MDC_CFG_SINGLE_LP   0x91
#define IIS2MDC_CFG_IDLE_LP     0x93
#define IIS2MDC_CFG_BDU         0x10

// -----------------------------------------------------------------------
// Motion-gated scheduler additions, built on top of this file's proven
// ~5uA Stop-mode sleep (unmodified from 7_ble_mioty_FMDN_tracker below).
// Ditched an earlier from-scratch sandbox rewrite (had a real sleep-timing
// bug) in favor of layering the scheduler onto THIS already hardware-
// validated base instead. FMDN beacons still transmit on every wake,
// unconditionally, matching real tracker behavior -- motion only changes
// how OFTEN that wake happens (2s vs 30s), never whether it happens.
// mioty has no real hardware here, so it's simulated: an independent
// elapsed-time accumulator (never reset by motion, only by its own "send")
// checked against its own target, with a ~5.5s blocking HAL_Delay standing
// in for the real sendMiotyPacket() TX time.
// -----------------------------------------------------------------------
#define ACTIVE_INTERVAL_S    2    // BLE active -- real decided target
#define IDLE_INTERVAL_S      30   // BLE idle -- real decided target
#define ACTIVE_HOLD_SECONDS  60   // how long "recently moved" persists after last confirmed motion.
                                  // Deliberately == MIOTY_ACTIVE_S below, not just "long enough": since
                                  // time_since_mioty_s is never reset by motion, a single motion event
                                  // only guarantees a mioty send within this window if the window is AT
                                  // LEAST as long as the target it needs to reach. Even so, a single tilt
                                  // can still land right on the boundary (RTC is 1-second-granular, and
                                  // mioty's accumulator may not have been sitting at exactly 0) -- repeat
                                  // the tilt a couple of times a few seconds apart if one still isn't enough.

#define MIOTY_ACTIVE_S        60   // 1 min -- real decided target
#define MIOTY_IDLE_S          900  // 15 min -- real decided target
#define MIOTY_SIM_TX_MS       5500 // simulated mioty TX blocking time
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c2;

/* USER CODE BEGIN PV */
volatile uint8_t alarm_fired = 0;

volatile uint8_t  dbg_mag_read_ok = 0; // last IIS2MDC_ReadXYZ() result
volatile int16_t  dbg_mag_x = 0, dbg_mag_y = 0, dbg_mag_z = 0; // last raw sample
volatile uint8_t  dbg_cfg_reg_a_readback = 0; // CFG_REG_A read back right
                                               // after the boot-time idle-
                                               // mode write, to prove it
                                               // actually landed

// Motion-gated scheduler state
uint16_t motion_delta_threshold = 60; // live-tunable: |dx|+|dy|+|dz| to count as motion
uint8_t  last_mag_valid = 0;          // skip the delta check on the very first read

volatile uint32_t dbg_active_hold_s      = 0; // seconds remaining in the active window; 0 = idle
volatile uint32_t dbg_ble_period_s       = 0; // RTC/BLE wake period in use -- should read 2 while active, 30 while idle
volatile uint32_t dbg_mioty_target_s     = 0; // mioty target in use -- should read 60 while active, 900 while idle
volatile uint16_t dbg_mag_delta          = 0; // |dx|+|dy|+|dz| vs previous single-shot sample
volatile uint32_t dbg_motion_event_count = 0;
volatile uint32_t dbg_ble_send_count     = 0; // total FMDN beacons sent (every wake, unconditional)
volatile uint32_t dbg_mioty_send_count   = 0; // THE proof variable for no-starvation: must keep climbing during sustained motion
volatile uint32_t dbg_time_since_mioty_s = 0; // mioty's own elapsed-time accumulator; never reset by motion, only by a mioty send
volatile uint32_t dbg_alarm_fired_count  = 0; // increments only inside RTC_Alarm_IRQHandler -- sleep-timing sanity check
volatile uint32_t dbg_iter_elapsed_s     = 0; // raw measured elapsed seconds for the last full iteration; should track ble_period_s

// -----------------------------------------------------------------------
// FMDN Ephemeral Identifier (EID) — 20 bytes
//
// HOW TO GET YOUR EID:
//   1. Navigate to: MIOTY_BLE_Tests/GoogleFindMyTools/
//   2. Install dependencies: pip install -r requirements.txt
//   3. Run: python main.py
//   4. Authenticate with your Google account (requires Chrome)
//   5. When the device list appears, press 'r' to register a new tracker
//   6. Copy the 40-character hex string that is displayed
//   7. Convert each pair of hex chars to a 0xNN byte and paste below
//
// EXAMPLE: If the tool outputs "a1b2c3d4e5f6071829304a5b6c7d8e9f0a1b2c3d"
//   then set: { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18, 0x29, 0x30,
//               0x4A, 0x5B, 0x6C, 0x7D, 0x8E, 0x9F, 0x0A, 0x1B, 0x2C, 0x3D }
//
// NOTE: You must re-run main.py every ~4 days to refresh the server-side
//       EID announcements. The EID itself does NOT change on the device.
// -----------------------------------------------------------------------
static const uint8_t fmdn_eid[20] = {
    0x01, 0x3C, 0x72, 0xBD, 0xF6, 0x3A, 0x23, 0x0D, 0x02, 0xE1,
    0x46, 0x01, 0x84, 0x7E, 0x45, 0xAA, 0x98, 0xBF, 0x66, 0x62
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C2_Init(void);
/* USER CODE BEGIN PFP */
void I2C2_BusRecovery(void);
uint8_t IIS2MDC_ReadRegister(uint8_t reg, uint8_t *value);
uint8_t IIS2MDC_WriteRegister(uint8_t reg, uint8_t value);
uint8_t IIS2MDC_CheckWhoAmI(void);
uint8_t IIS2MDC_ReadXYZ(int16_t *x, int16_t *y, int16_t *z);
void LED_Blink(uint8_t count);
void SX1280_WaitBusy(void);
uint8_t SX1280_GetStatus(void);
void SX1280_SendCommand(uint8_t *cmd, uint8_t len);
void SX1280_WriteBuffer(uint8_t offset, uint8_t *data, uint8_t len);
void SX1280_WriteRegister(uint16_t addr, uint8_t *data, uint8_t len);
uint8_t SX1280_ReadRegister(uint16_t addr);
void SX1280_SetFrequency(uint8_t b0, uint8_t b1, uint8_t b2);
uint8_t SX1280_SendOnChannel(uint8_t f0, uint8_t f1, uint8_t f2, uint8_t ch,
                             uint8_t white_seed);
void SX1280_InitBLE(void);
void SX1280_VerifyInit(void);
void SX1280_SendFMDNBeacon(void);
void RTC_Init_LowPower(void);
void RTC_SetAlarm_Seconds(uint32_t seconds);
uint32_t RTC_ReadCounter(void);
void STM32_EnterStopMode(uint32_t period_s);
void SX1280_Sleep(void);
void SX1280_Wakeup(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// -----------------------------------------------------------------------
// LED helper: blinks 'count' times (150ms on/off), then 600ms pause
// -----------------------------------------------------------------------
void LED_Blink(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    HAL_Delay(150);
  }
  HAL_Delay(600);
}

// -----------------------------------------------------------------------
// IIS2MDC magnetometer -- single-shot low-power sample: trigger conversion,
// poll for data-ready, read X/Y/Z, return sensor to idle. No continuous
// mode, no interrupt generator -- the sensor is idle/power-down at all
// other times.
// -----------------------------------------------------------------------
// I2C2 bus recovery -- run BEFORE MX_I2C2_Init() claims PB10/PB11 for the
// AF peripheral. The magnetometer's VDD is hardwired to the 3.3V rail (no
// GPIO power-gating possible), so it never actually loses power across a
// reflash -- only the MCU resets. If a previous session's I2C transaction
// ever got interrupted mid-byte (sensor left holding SDA low, waiting for
// clock pulses that never came because the MCU reset), the bus stays
// stuck that way indefinitely, and the module's own pull-up sources
// continuous current through the stuck-low line -- a plausible explanation
// for a steady current floor that persists regardless of the sensor's own
// register/mode state. This is the standard I2C bus-recovery procedure:
// manually clock SCL (open-drain) up to 9 times to let a wedged slave
// finish clocking out its byte and release SDA, then issue a STOP
// condition. Safe to run even if the bus is already fine.
// -----------------------------------------------------------------------
void I2C2_BusRecovery(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Release both lines (open-drain high = let the module's pull-ups win)
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10 | GPIO_PIN_11, GPIO_PIN_SET);
  HAL_Delay(1);

  // If SDA is stuck low, clock SCL up to 9 times -- enough for a slave
  // mid-byte to finish and release SDA.
  for (int i = 0; i < 9; i++) {
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == GPIO_PIN_SET) {
      break; // SDA already released -- bus is free
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
    HAL_Delay(1);
  }

  // Generate a STOP condition (SDA low-to-high while SCL is high) so the
  // bus is in a known idle state before the I2C peripheral claims it.
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
  HAL_Delay(1);
}

uint8_t IIS2MDC_ReadRegister(uint8_t reg, uint8_t *value) {
  return HAL_I2C_Mem_Read(&hi2c2, IIS2MDC_I2C_ADDR, reg,
                          I2C_MEMADD_SIZE_8BIT, value, 1, 100) == HAL_OK;
}

uint8_t IIS2MDC_WriteRegister(uint8_t reg, uint8_t value) {
  return HAL_I2C_Mem_Write(&hi2c2, IIS2MDC_I2C_ADDR, reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1, 100) == HAL_OK;
}

// Verify IIS2MDC magnetometer presence. WHO_AM_I register 0x4F should read 0x40.
uint8_t IIS2MDC_CheckWhoAmI(void) {
  uint8_t whoami = 0;
  if (!IIS2MDC_ReadRegister(IIS2MDC_WHO_AM_I_REG, &whoami)) return 0;
  return (whoami == IIS2MDC_WHO_AM_I_VALUE) ? 1 : 0;
}

uint8_t IIS2MDC_ReadXYZ(int16_t *x, int16_t *y, int16_t *z) {
  uint8_t status = 0;
  uint8_t raw[6] = {0};

  if (!IIS2MDC_WriteRegister(IIS2MDC_CFG_REG_C, IIS2MDC_CFG_BDU)) return 0;
  if (!IIS2MDC_WriteRegister(IIS2MDC_CFG_REG_A, IIS2MDC_CFG_SINGLE_LP)) return 0;

  for (uint8_t i = 0; i < 20; i++) {
    HAL_Delay(2);
    if (!IIS2MDC_ReadRegister(IIS2MDC_STATUS_REG, &status)) return 0;
    if ((status & IIS2MDC_STATUS_ZYXDA) != 0) break;
  }
  if ((status & IIS2MDC_STATUS_ZYXDA) == 0) return 0;

  if (HAL_I2C_Mem_Read(&hi2c2, IIS2MDC_I2C_ADDR,
                       IIS2MDC_OUTX_L_REG | IIS2MDC_AUTO_INCREMENT,
                       I2C_MEMADD_SIZE_8BIT, raw, 6, 100) != HAL_OK) return 0;

  *x = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
  *y = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
  *z = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);
  IIS2MDC_WriteRegister(IIS2MDC_CFG_REG_A, IIS2MDC_CFG_IDLE_LP);
  return 1;
}

// -----------------------------------------------------------------------
// Wait for BUSY pin to go low (chip ready for next command)
// Hangs with 50ms rapid blink if timeout exceeds 1 second
// -----------------------------------------------------------------------
void SX1280_WaitBusy(void) {
  uint32_t timeout = HAL_GetTick();
  while (HAL_GPIO_ReadPin(LORA_BUSY_GPIO_Port, LORA_BUSY_Pin) == GPIO_PIN_SET) {
    if ((HAL_GetTick() - timeout) > 1000) {
      while (1) {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        HAL_Delay(50);
      }
    }
  }
}

// -----------------------------------------------------------------------
// GetStatus: returns the raw status byte
// Bits 7:5 = circuit mode (2=STDBY_RC, 3=STDBY_XOSC, 4=FS, 5=Rx, 6=Tx)
// Bits 4:2 = command status (1=OK, 3=timeout, 4=error, 5=fail, 6=TxDone)
// -----------------------------------------------------------------------
uint8_t SX1280_GetStatus(void) {
  uint8_t tx[2] = {0xC0, 0x00};
  uint8_t rx[2] = {0x00, 0x00};
  SX1280_WaitBusy();
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, HAL_MAX_DELAY);
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  return rx[1];
}

// -----------------------------------------------------------------------
// Send a command over SPI (waits for BUSY first)
// -----------------------------------------------------------------------
void SX1280_SendCommand(uint8_t *cmd, uint8_t len) {
  SX1280_WaitBusy();
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi1, cmd, len, HAL_MAX_DELAY);
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

// -----------------------------------------------------------------------
// Write payload data to TX buffer at given offset
// -----------------------------------------------------------------------
void SX1280_WriteBuffer(uint8_t offset, uint8_t *data, uint8_t len) {
  SX1280_WaitBusy();
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
  uint8_t header[2] = {0x1A, offset};
  HAL_SPI_Transmit(&hspi1, header, 2, HAL_MAX_DELAY);
  HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

// -----------------------------------------------------------------------
// Write registers via opcode 0x18
// -----------------------------------------------------------------------
void SX1280_WriteRegister(uint16_t addr, uint8_t *data, uint8_t len) {
  SX1280_WaitBusy();
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
  uint8_t header[3] = {0x18, (addr >> 8) & 0xFF, addr & 0xFF};
  HAL_SPI_Transmit(&hspi1, header, 3, HAL_MAX_DELAY);
  HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

// -----------------------------------------------------------------------
// Read a single register via opcode 0x19
// Format: [0x19, addrHigh, addrLow, NOP] then receive 1 byte
// -----------------------------------------------------------------------
uint8_t SX1280_ReadRegister(uint16_t addr) {
  uint8_t val = 0;
  SX1280_WaitBusy();
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
  uint8_t header[4] = {0x19, (addr >> 8) & 0xFF, addr & 0xFF, 0x00};
  HAL_SPI_Transmit(&hspi1, header, 4, HAL_MAX_DELAY);
  HAL_SPI_Receive(&hspi1, &val, 1, HAL_MAX_DELAY);
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  return val;
}

// -----------------------------------------------------------------------
// Set RF frequency
// rfFreq = (targetMHz * 1e6 / 52e6) * 2^18, split into 3 bytes MSB first
// CH37=2402MHz: 0xB8,0xC4,0xEC
// CH38=2426MHz: 0xBA,0x9D,0x89
// CH39=2480MHz: 0xBE,0xC4,0xEC
// -----------------------------------------------------------------------
void SX1280_SetFrequency(uint8_t b0, uint8_t b1, uint8_t b2) {
  uint8_t cmd[4] = {0x86, b0, b1, b2};
  SX1280_SendCommand(cmd, 4);
}

// -----------------------------------------------------------------------
// Initialize SX1280 for BLE advertising mode
// Must be called once after power-on/reset, before any TX
// -----------------------------------------------------------------------
void SX1280_InitBLE(void) {
  // 1. Go to Standby RC mode
  uint8_t cmd_standby[2] = {0x80, 0x00};
  SX1280_SendCommand(cmd_standby, 2);
  HAL_Delay(10);

  // 2. Set packet type = BLE (0x04) — per reference: PACKET_TYPE_BLE = 0x04
  uint8_t cmd_pkt_type[2] = {0x8A, 0x04};
  SX1280_SendCommand(cmd_pkt_type, 2);

  // 3. Set TX base address = 0x80, RX base = 0x00 (matches reference
  // SetupAdvPdu offset)
  uint8_t cmd_buf[3] = {0x8F, 0x80, 0x00};
  SX1280_SendCommand(cmd_buf, 3);

  // 4. Set modulation params: 1 Mbps, MOD_IND=0.5, BT=0.5
  uint8_t cmd_mod[4] = {0x8B, 0x45, 0x01, 0x20};
  SX1280_SendCommand(cmd_mod, 4);

  // 5. Set TX output power = 0 dBm (0x12) for low current draw, ramp time =
  // 20us (0x20)
  // For Board 2 with J1 coaxial (no PCB antenna): use +13 dBm (0x1F)
  uint8_t cmd_tx_params[3] = {0x8E, 0x12, 0x20};
  SX1280_SendCommand(cmd_tx_params, 3);

  // 6. Write BLE advertising Access Address = 0x8E89BED6
  //    Registers 0x09CF (MSB) .. 0x09D2 (LSB)
  uint8_t access_addr[4] = {0x8E, 0x89, 0xBE, 0xD6};
  SX1280_WriteRegister(0x09CF, access_addr, 4);

  // 7. Write CRC init seed = 0x555555 (BLE advertising spec requirement)
  //    Registers 0x09C7 (MSB) .. 0x09C9 (LSB)
  uint8_t crc_seed[3] = {0x55, 0x55, 0x55};
  SX1280_WriteRegister(0x09C7, crc_seed, 3);

  // 8. Map TxDone IRQ to DIO1 pin
  //    [opcode, IRQmask_H, IRQmask_L, DIO1_H, DIO1_L, DIO2_H, DIO2_L, DIO3_H,
  //    DIO3_L]
  uint8_t cmd_irq[9] = {0x8D, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  SX1280_SendCommand(cmd_irq, 9);
}

// -----------------------------------------------------------------------
// Read back key registers after InitBLE and blink results as LED groups
//
// All counts shifted by +1 so 0 = 1 blink (never 0 blinks)
//
// EXPECTED SEQUENCE (good init):
//   Group 1:  9 blinks  (Access Address reg 0x09CF = 0x8E, nibble=8, +1=9)
//   Group 2:  6 blinks  (CRC seed reg 0x09C7 = 0x55, nibble=5, +1=6)
//   Group 3:  3 blinks  (Circuit mode = STDBY_RC = 2, +1=3)
//   Group 4:  1 blink   (Command status = idle = 0, +1=1)
//
// 9,6,3,1 = firmware confirmed good → suspect RF/hardware path
// Any other pattern = SPI write issue
// -----------------------------------------------------------------------
void SX1280_VerifyInit(void) {
  HAL_Delay(1000);

  // Group 1: Access Address reg 0x09CF — expect 9 blinks
  uint8_t aa = SX1280_ReadRegister(0x09CF);
  LED_Blink((aa >> 4) + 1);
  HAL_Delay(1000);

  // Group 2: CRC seed reg 0x09C7 — expect 6 blinks
  uint8_t crc = SX1280_ReadRegister(0x09C7);
  LED_Blink((crc >> 4) + 1);
  HAL_Delay(1000);

  // Group 3: Circuit mode (bits 7:5) — expect 3 blinks (STDBY_RC=2, +1=3)
  HAL_Delay(50);
  uint8_t st = SX1280_GetStatus();
  uint8_t circuit_mode = (st >> 5) & 0x07;
  uint8_t cmd_status = (st >> 2) & 0x07;
  LED_Blink(circuit_mode + 1);
  HAL_Delay(1000);

  // Group 4: Command status (bits 4:2) — expect 1 blink (idle=0, +1=1)
  LED_Blink(cmd_status + 1);
  HAL_Delay(1000);
}

// -----------------------------------------------------------------------
// Transmit one FMDN BLE ADV_NONCONN_IND packet on the specified channel
//
// Returns: 1 = TxDone confirmed via DIO1 pin
//          0 = TxDone not seen (possible TX failure)
//
// DIO1 is mapped to TxDone IRQ in InitBLE step 8.
// DIO1 goes HIGH when the chip finishes transmitting.
// IRQ is cleared after each TX so DIO1 resets for the next channel.
//
// FMDN Advertisement Format (Google Find My Device Network):
//   Byte 0-2:   02 01 06        — BLE Flags
//   Byte 3:     19              — Length (25 bytes)
//   Byte 4:     16              — Service Data AD type
//   Byte 5-6:   AA FE           — FMDN 16-bit Service UUID
//   Byte 7:     41              — FMDN frame type
//   Byte 8-27:  [20 bytes]      — Ephemeral Identifier (EID)
//   Byte 28:    00              — Hashed flags
//   Total: 29 bytes of AD data
// -----------------------------------------------------------------------
uint8_t SX1280_SendOnChannel(uint8_t freq0, uint8_t freq1, uint8_t freq2,
                             uint8_t ch_index, uint8_t white_seed) {
  // Set RF frequency for this channel
  SX1280_SetFrequency(freq0, freq1, freq2);
  // Explicitly set modulation parameters (1 Mbps GFSK, index 0.5, BT 0.5)
  uint8_t cmd_mod[4] = {0x8B, 0x45, 0x01, 0x20};
  SX1280_SendCommand(cmd_mod, 4);

  // Build FMDN ADV_NONCONN_IND PDU
  // Google Find My Device Network service data advertisement
  uint8_t adv_data[] = {
      // Flags AD structure (3 bytes)
      // Length=2, Type=0x01, Value=0x06 (LE General Discoverable + BR/EDR Not
      // Supported)
      0x02, 0x01, 0x06,

      // FMDN Service Data AD structure (26 bytes)
      // Length=25, Type=0x16 (Service Data - 16-bit UUID)
      0x19, 0x16,
      // FMDN 16-bit Service UUID (0xFEAA, little-endian)
      0xAA, 0xFE,
      // FMDN frame type: 0x41 = with unwanted tracking protection mode
      0x41,
      // 20-byte Ephemeral Identifier (EID) — filled from fmdn_eid[] array
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // Hashed flags
      0x00};

  // Copy the EID from the global constant into the advertisement payload
  for (int i = 0; i < 20; i++) {
    adv_data[8 + i] = fmdn_eid[i];
  }

  // BLE random static address (6 bytes, LSB-first)
  // On-air (MSB-first): FF:EE:DD:CC:BB:AC
  uint8_t mac[6] = {0xAC, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

  // Payload length = MAC (6) + AD data (29) = 35 bytes
  uint8_t pdu_len = 6 + 29;
  uint8_t payload[37]; // 2-byte header + 35-byte PDU

  payload[0] = 0x42;    // PDU type: ADV_NONCONN_IND (bits[3:0]=0010) + TxAdd=1
                         // (bit6) for random address
  payload[1] = pdu_len;  // length of everything after the 2-byte header
  for (int i = 0; i < 6; i++)
    payload[2 + i] = mac[i];
  for (int i = 0; i < 29; i++)
    payload[8 + i] = adv_data[i];

  // SetPacketParams for BLE — matching reference prepareForBeacon():
  // SetPacketParams(0x20, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00)
  // [0x8C, ConnectionState=0x20, CrcLength=0x10, BleTestPayload=0x04,
  // Whitening=0x00, 0x00, 0x00, 0x00]
  // NOTE: whitening is enabled via SetWhiteningSeed (register 0x09C5),
  // NOT via this byte
  uint8_t cmd_pkt[8] = {0x8C, 0x20, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00};
  SX1280_SendCommand(cmd_pkt, 8);

  // Set whitening seed — reference uses SetWhiteningSeed(reg 0x09C5)
  // Seeds from reference channel_table: CH37=0x53, CH38=0x33, CH39=0x73
  SX1280_WriteRegister(0x09C5, &white_seed, 1);

  // Write full 37-byte PDU to TX buffer at offset 0x80
  // (2-byte header + 6-byte MAC + 29-byte FMDN AD data = 37 bytes total)
  SX1280_WriteBuffer(0x80, payload, 37);

  // SetTx: periodBase=0x00 (15.625us steps), count=0x0064 (~1.56ms timeout)
  uint8_t cmd_tx[4] = {0x83, 0x00, 0x00, 0x64};
  SX1280_SendCommand(cmd_tx, 4);

  // Wait for chip to finish (BUSY goes low after TX completes)
  SX1280_WaitBusy();
  HAL_Delay(2);

  // Read TxDone confirmation from DIO1 pin
  uint8_t txdone = 0;
  if (HAL_GPIO_ReadPin(LORA_DIO1_GPIO_Port, LORA_DIO1_Pin) == GPIO_PIN_SET) {
    txdone = 1;
    // Clear all IRQ flags so DIO1 goes low again for next channel
    uint8_t clr[3] = {0x97, 0xFF, 0xFF};
    SX1280_SendCommand(clr, 3);
  }

  HAL_Delay(3);
  return txdone;
}

// -----------------------------------------------------------------------
// Send FMDN beacon on all 3 BLE advertising channels
// -----------------------------------------------------------------------
void SX1280_SendFMDNBeacon(void) {
  // Whitening seeds match BLE specification:
  // CH37=0x53, CH38=0x33, CH39=0x73
  SX1280_SendOnChannel(0xB8, 0xC4, 0xEC, 37, 0x53); // 2402 MHz, seed=0x53
  HAL_Delay(10);
  SX1280_SendOnChannel(0xBA, 0x9D, 0x89, 38, 0x33); // 2426 MHz, seed=0x33
  HAL_Delay(10);
  SX1280_SendOnChannel(
      0xBE, 0xC4, 0x55, 39,
      0x73); // 2480 MHz, seed=0x73 (Corrected frequency LSB to 0x55)
  HAL_Delay(10);
}

// -----------------------------------------------------------------------
// Initialize the RTC to use the internal LSI clock source (40 kHz)
// -----------------------------------------------------------------------
void RTC_Init_LowPower(void) {
  // 1. Enable Power and Backup Interface Clocks
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();

  // 2. Enable access to Backup Domain
  HAL_PWR_EnableBkUpAccess();

  // Force backup domain reset to clear any locked RTC clock source
  // configurations
  __HAL_RCC_BACKUPRESET_FORCE();
  __HAL_RCC_BACKUPRESET_RELEASE();

  // 3. Enable LSE oscillator
  RCC_OscInitTypeDef osc_init = {0};
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_LSE;
  osc_init.LSEState = RCC_LSE_ON;
  if (HAL_RCC_OscConfig(&osc_init) != HAL_OK) {
    Error_Handler();
  }

  // 4. Select LSE as RTC Clock Source and enable it
  RCC_PeriphCLKInitTypeDef clk_init = {0};
  clk_init.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  clk_init.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&clk_init) != HAL_OK) {
    Error_Handler();
  }

  // 5. Enable RTC Clock
  __HAL_RCC_RTC_ENABLE();

  // 6. Wait for RTC registers synchronization
  WRITE_REG(RTC->CRL, (uint32_t)~RTC_CRL_RSF);
  while ((RTC->CRL & RTC_CRL_RSF) == 0) {
  }
  while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
  }

  // 7. Configure RTC Prescaler (for 32.768kHz LSE: 32768 - 1 = 32767 to get 1 Hz)
  SET_BIT(RTC->CRL, RTC_CRL_CNF);
  WRITE_REG(RTC->PRLH, (32767 >> 16) & 0xFFFF);
  WRITE_REG(RTC->PRLL, 32767 & 0xFFFF);
  CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
  while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
  }

  // 8. Configure EXTI Line 17 (connected to RTC Alarm) for Interrupt Rising
  // Edge
  SET_BIT(EXTI->IMR, EXTI_IMR_MR17);
  SET_BIT(EXTI->RTSR, EXTI_RTSR_TR17);

  // 9. Configure NVIC for RTC Alarm
  HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);
}

uint32_t RTC_ReadCounter(void) {
  return ((uint32_t)RTC->CNTH << 16) | RTC->CNTL;
}

// -----------------------------------------------------------------------
// Set the RTC Alarm to fire 'seconds' from current time. Was fixed at 10s;
// parameterized so the motion-gated scheduler can request 2s (active) or
// 30s (idle) for BLE's own wake.
// -----------------------------------------------------------------------
void RTC_SetAlarm_Seconds(uint32_t seconds) {
  // Clear Register Synchronized Flag (RSF) and wait for synchronization (critical after wakeup)
  WRITE_REG(RTC->CRL, (uint32_t)~RTC_CRL_RSF);
  while ((RTC->CRL & RTC_CRL_RSF) == 0) {
  }

  while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
  }

  uint32_t alarm_val = RTC_ReadCounter() + seconds;

  SET_BIT(RTC->CRL, RTC_CRL_CNF);
  WRITE_REG(RTC->ALRH, (alarm_val >> 16) & 0xFFFF);
  WRITE_REG(RTC->ALRL, alarm_val & 0xFFFF);
  SET_BIT(RTC->CRH, RTC_CRH_ALRIE); // Enable Alarm interrupt
  CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
  while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
  }
}

// -----------------------------------------------------------------------
// RTC Alarm Interrupt Handler - Clears pending bits and sets flag
// -----------------------------------------------------------------------
void RTC_Alarm_IRQHandler(void) {
  while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
  }
  SET_BIT(RTC->CRL, RTC_CRL_CNF);
  CLEAR_BIT(RTC->CRH, RTC_CRH_ALRIE);    // Disable alarm interrupt temporarily
  CLEAR_BIT(RTC->CRL, RTC_CRL_ALRF_Msk); // Clear Alarm flag
  CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
  while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
  }

  WRITE_REG(EXTI->PR, EXTI_PR_PR17); // Clear EXTI Line 17 pending bit

  alarm_fired = 1; // Set global flag
  dbg_alarm_fired_count++;
}

void SX1280_Sleep(void) {
  uint8_t cmd_sleep[2] = {0x84, 0x01}; // Sleep with register retention
  SX1280_SendCommand(cmd_sleep, 2);
}

void SX1280_Wakeup(void) {
  uint8_t tx[2] = {0xC0, 0x00}; // GetStatus opcode + dummy byte
  uint8_t rx[2] = {0x00, 0x00};
  
  // Pull NSS low to start transaction (no WaitBusy before this)
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
  
  // Hold NSS low for at least 30-50 microseconds to allow the radio internal oscillator to start.
  // At 8 MHz, 100 loop iterations is ~37.5 microseconds.
  for (volatile uint32_t i = 0; i < 100; i++) {
    __NOP();
  }
  
  // Transmit GetStatus command to clock the SPI bus and wake up the radio
  HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, HAL_MAX_DELAY);
  
  // Pull NSS high
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  
  // Wait for the radio's busy pin to go low
  SX1280_WaitBusy();
}

// -----------------------------------------------------------------------
// Put STM32 into Stop Mode for period_s seconds and wake up via RTC alarm
// interrupt. period_s was fixed at 10s; now driven by the motion-gated
// scheduler (2s active / 30s idle).
// -----------------------------------------------------------------------
void STM32_EnterStopMode(uint32_t period_s) {
  // 1. Put SX1280 to sleep
  SX1280_Sleep();

  // 2. Disable SPI1 and clamp pins to Input Pull-Down to prevent leaks/floating states
  __HAL_SPI_DISABLE(&hspi1);

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  // SCK (PA5), MISO (PA6), MOSI (PA7) -> Input with Pull-Down (prevents floating SPI bus)
  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // DIO1 (PB0) and BUSY (PB15) -> Analog mode
  // CRITICAL: The SX1280 drives the BUSY pin HIGH in sleep mode! 
  // A pull-down resistor here creates an exact 82uA short circuit (3.3V / 40kOhm).
  GPIO_InitStruct.Pin = LORA_DIO1_Pin | LORA_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // De-init I2C2 before clamping its pins -- HAL_I2C_DeInit's own pin
  // deinit only reverts to floating input, not Analog, so the explicit
  // clamp below still matters. Re-inited after waking (step 8b).
  HAL_I2C_DeInit(&hi2c2);

  // Set PB10 (SCL2) and PB11 (SDA2) for IIS2MDCT to Analog mode to prevent leakage
  GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // OPTIMIZATION: Set all other unused GPIOs on Port A and B to Analog Mode
  // Port A: PA0, PA1, PA2, PA8, PA9, PA10, PA11, PA12, PA15 (Leave PA13/PA14 for SWD)
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_8 | 
                        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Port B: PB1..PB9, PB12, PB13, PB14 (PB0, PB15 are LORA, PB10, PB11 are I2C)
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | 
                        GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | 
                        GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Keep NSS (PA4) and NRST (PA3) HIGH to lock radio state
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);

  // 3. Set the RTC alarm for period_s seconds from now
  RTC_SetAlarm_Seconds(period_s);

  // 4. Suspend SysTick to prevent it from waking the CPU every 1ms
  HAL_SuspendTick();

  // 5. Enter STOP Mode (SRAM retained, wakes up via RTC alarm EXTI 17)
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  // --- SLEEPING HERE ---

  // 6. Wake up - system restarts on internal HSI, restore clock to HSE 8 MHz
  SystemClock_Config();

  // 7. Resume SysTick
  HAL_ResumeTick();

  // 8. Re-initialize SPI1 (handles peripheral state and alternate functions)
  HAL_SPI_DeInit(&hspi1);
  MX_SPI1_Init();

  // 8b. Re-initialize I2C2 for the next magnetometer single-shot read
  MX_I2C2_Init();

  // 9. Re-initialize DIO1 (PB0) and BUSY (PB15) as INPUT, NOPULL
  GPIO_InitStruct.Pin = LORA_DIO1_Pin | LORA_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // 10. Wake up the SX1280
  SX1280_Wakeup();
  
  // Wait 5ms for the radio regulators and internal crystal to fully stabilize
  HAL_Delay(5);

  // 11. Re-initialize BLE configuration registers (lost during sleep)
  SX1280_InitBLE();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  // Reconnect window: gives ST-Link time to attach after flash
  // Increased to 5s to avoid slow-blink-forever on fresh flash
  for (volatile uint32_t i = 0; i < 5000000; i++)
    ;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  I2C2_BusRecovery(); // free a stuck bus (see comment above the function)
                       // before the I2C peripheral claims PB10/PB11
  MX_I2C2_Init();
  /* USER CODE BEGIN 2 */

  // NOTE: a combined REBOOT+SOFT_RST write was tried here and made things
  // WORSE (Stop-mode current went from ~26-30uA up to ~60uA on real
  // hardware) -- removed. Root cause of the original elevated-current
  // readings turned out to be a hardware fault on that specific board/part
  // (a rebuilt board measures ~5uA with identical firmware logic), not a
  // register-state issue, so this reset was never actually needed. Left as
  // a cautionary note: don't re-add REBOOT+SOFT_RST without re-verifying
  // it doesn't regress this again.

  // Force idle mode explicitly -- if this board was previously flashed
  // with a continuous-mode firmware
  // (e.g. 4_1's IIS2MDC_ConfigureMotionInterrupt()) and VDD to the sensor
  // never actually dropped across reflashes (only the MCU itself resets,
  // not necessarily the whole board's power), the IIS2MDC can still be
  // sitting in continuous-conversion mode drawing its ~25uA continuous-mode
  // current the entire time -- silently inflating every Stop-mode reading
  // taken since, regardless of what this firmware's own loop does.
  // CFG_REG_A's power-on-reset default is idle (MD[1:0]=11), but this
  // write makes it explicit instead of trusting whatever state the sensor
  // was actually left in.
  IIS2MDC_WriteRegister(IIS2MDC_CFG_REG_A, IIS2MDC_CFG_IDLE_LP);

  // Read the register back to PROVE the write actually landed, rather than
  // assuming it did. Blinks MD[1:0]+1 so it's visible even on a fully
  // standalone run (no debugger attached): 1 blink = MD=00 (continuous --
  // write did NOT take effect), 2 = MD=01 (single), 3 or 4 = MD=10/11
  // (idle -- write succeeded). Datasheet: continuous low-power @10Hz draws
  // ~25uA typ, idle/power-down draws ~1.5uA typ -- if this blinks 1 and the
  // Stop-mode current is still ~28-30uA, that's the smoking gun.
  uint8_t cfg_reg_a_readback = 0;
  IIS2MDC_ReadRegister(IIS2MDC_CFG_REG_A, &cfg_reg_a_readback);
  dbg_cfg_reg_a_readback = cfg_reg_a_readback;
  HAL_Delay(300);
  LED_Blink((cfg_reg_a_readback & 0x03) + 1);

  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);

  // Hard reset SX1280: pull NRST low 5ms then release, wait 10ms for boot
  HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);
  HAL_Delay(10);

  // Verify SX1280 is alive on SPI (valid status is not 0x00 or 0xFF)
  uint8_t status = SX1280_GetStatus();
  if (status == 0x00 || status == 0xFF) {
    // Not responding — slow 1s blink forever
    while (1) {
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      HAL_Delay(1000);
    }
  }

  LED_Blink(1); // 1 blink = SX1280 alive

  SX1280_InitBLE();

  // Diagnostic register readback — see SX1280_VerifyInit comments
  // Expected: 9 blinks, 6 blinks, 3 blinks, 1 blink
  SX1280_VerifyInit();

  // Turn off LED after verification blinks
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  // Magnetometer presence check -- 2 blinks = WHO_AM_I OK, 8 blinks = fail
  // (I2C wiring/address/power issue). Boot continues either way; the loop
  // below reports read failures per-cycle via dbg_mag_read_ok.
  uint8_t mag_ok = IIS2MDC_CheckWhoAmI();
  LED_Blink(mag_ok ? 2 : 8);
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  // Disabled for the real joulemeter measurement -- keeping debug clocks
  // alive in Stop mode inflates standby current. Re-enable (uncomment)
  // only for a short Live Expressions sanity-check run, then disable again.
  // HAL_DBGMCU_EnableDBGStopMode();

  RTC_Init_LowPower(); // Initialize low-power RTC wakeup

  // Boots "active" so the first cycles run at the fast 2s cadence.
  uint32_t active_hold_s = ACTIVE_HOLD_SECONDS;
  uint32_t time_since_mioty_s = 0;

  // Tracks the start of each iteration's elapsed-time window, stamped once
  // per iteration right after waking. Any processing time within an
  // iteration (beacon TX, magnetometer read, a possible mioty-sim block)
  // is naturally captured by that SAME iteration's own elapsed measurement
  // at the bottom of the loop -- see the full-iteration elapsed comment
  // there for why this matters (mioty's ~5.5s block must not go uncounted).
  uint32_t last_iter_start = RTC_ReadCounter();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // 1. Transmit FMDN beacon on all 3 BLE advertising channels -- every
    //    wake, unconditionally. Motion never gates WHETHER BLE sends, only
    //    how OFTEN it wakes to do so (see step 3).
    SX1280_SendFMDNBeacon();
    dbg_ble_send_count++;
    LED_Blink(1); // 1 blink = BLE cycle

    // 2. Single-shot magnetometer read, piggybacked on this same wake --
    //    no continuous mode, no interrupt, sensor idle/power-down
    //    otherwise. Delta vs. the previous sample is the motion signal.
    int16_t mx = 0, my = 0, mz = 0;
    dbg_mag_read_ok = IIS2MDC_ReadXYZ(&mx, &my, &mz);
    if (dbg_mag_read_ok) {
      if (last_mag_valid) {
        int16_t dx = (int16_t)(mx - dbg_mag_x);
        int16_t dy = (int16_t)(my - dbg_mag_y);
        int16_t dz = (int16_t)(mz - dbg_mag_z);
        uint16_t delta = (uint16_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz));
        dbg_mag_delta = delta;
        if (delta >= motion_delta_threshold) {
          active_hold_s = ACTIVE_HOLD_SECONDS;
          dbg_motion_event_count++;
          LED_Blink(3); // 3 blinks = motion/orientation change detected
        }
      }
      dbg_mag_x = mx;
      dbg_mag_y = my;
      dbg_mag_z = mz;
      last_mag_valid = 1;
    }
    dbg_active_hold_s = active_hold_s;

    // 3. Pick THIS cycle's sleep period + mioty target from the
    //    (possibly just-updated) motion state, so a motion event detected
    //    THIS iteration immediately shortens the upcoming sleep instead of
    //    waiting a whole extra cycle.
    uint8_t motion_recent = (active_hold_s > 0);
    uint32_t ble_period_s = motion_recent ? ACTIVE_INTERVAL_S : IDLE_INTERVAL_S;
    uint32_t mioty_target_s = motion_recent ? MIOTY_ACTIVE_S : MIOTY_IDLE_S;
    dbg_ble_period_s = ble_period_s;
    dbg_mioty_target_s = mioty_target_s;

    // 4. mioty-sim: independent elapsed-time check against ITS OWN target --
    //    never reset by motion, only by its own send. time_since_mioty_s
    //    already reflects everything through the end of the PREVIOUS
    //    iteration (accumulated in step 6 below), so checking it here,
    //    before this iteration's own sleep, is exact -- no double-counting.
    if (time_since_mioty_s >= mioty_target_s) {
      dbg_mioty_send_count++;
      time_since_mioty_s = 0;
      LED_Blink(2); // 2 blinks = mioty cycle
      HAL_Delay(MIOTY_SIM_TX_MS); // simulated blocking TX -- captured by
                                  // THIS iteration's own elapsed measurement
                                  // in step 6, nothing lost
    }
    dbg_time_since_mioty_s = time_since_mioty_s;

    // 5. Shut down peripherals, flash LED, and enter Stop Mode for
    //    ble_period_s seconds (2 while active, 30 while idle).
    STM32_EnterStopMode(ble_period_s);

    // 6. Full-iteration elapsed time -- spans everything since the last
    //    checkpoint (previous iteration's tail + this sleep): beacon TX,
    //    magnetometer read, a possible mioty-sim block, and the sleep
    //    itself. Used to decay active_hold_s and accumulate mioty's own
    //    elapsed timer for the NEXT iteration's check (step 4).
    uint32_t now = RTC_ReadCounter();
    uint32_t iter_elapsed = now - last_iter_start;
    last_iter_start = now;
    dbg_iter_elapsed_s = iter_elapsed;

    active_hold_s = (active_hold_s > iter_elapsed) ? (active_hold_s - iter_elapsed) : 0;
    time_since_mioty_s += iter_elapsed;
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LORA_NSS_Pin|LORA_NRST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LORA_NSS_Pin LORA_NRST_Pin */
  GPIO_InitStruct.Pin = LORA_NSS_Pin|LORA_NRST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LORA_DIO1_Pin LORA_BUSY_Pin */
  GPIO_InitStruct.Pin = LORA_DIO1_Pin|LORA_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

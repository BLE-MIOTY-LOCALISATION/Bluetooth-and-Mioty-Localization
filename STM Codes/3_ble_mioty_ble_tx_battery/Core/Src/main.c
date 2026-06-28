/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
volatile uint8_t alarm_fired = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
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
void SX1280_SendBLEBeacon(void);
void RTC_Init_LowPower(void);
void RTC_SetAlarm_1s(void);
void STM32_EnterStopMode(void);
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
// CH39=2480MHz: 0xBE,0xC4,0x55
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

  // 5. Set TX output power = 0 dBm (0x12) for low current draw,
  // ramp time = 20us (0x20)
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
// Transmit one BLE ADV_NONCONN_IND packet on the specified channel
//
// Returns: 1 = TxDone confirmed via DIO1 pin
//          0 = TxDone not seen (possible TX failure)
//
// DIO1 is mapped to TxDone IRQ in InitBLE step 8.
// DIO1 goes HIGH when the chip finishes transmitting.
// IRQ is cleared after each TX so DIO1 resets for the next channel.
// -----------------------------------------------------------------------
uint8_t SX1280_SendOnChannel(uint8_t freq0, uint8_t freq1, uint8_t freq2,
                             uint8_t ch_index, uint8_t white_seed) {
  // Set RF frequency for this channel
  SX1280_SetFrequency(freq0, freq1, freq2);
  // Explicitly set modulation parameters (1 Mbps GFSK, index 0.5, BT 0.5)
  uint8_t cmd_mod[4] = {0x8B, 0x45, 0x01, 0x20};
  SX1280_SendCommand(cmd_mod, 4);

  // Build BLE ADV_NONCONN_IND PDU — iBeacon format matching reference board
  // Sniffed from reference board: ADV_NONCONN_IND, ManufacturerSpecificData
  // (0xFF) Apple iBeacon: UUID=01020304-0506-0708-090A-0B0C0D0E0F10, Major=1,
  // Minor=2
  uint8_t adv_data[] = {
      // Flags AD structure (3 bytes)
      // Length=2, Type=0x01, Value=0x06 (LE General Discoverable + BR/EDR Not
      // Supported)
      0x02, 0x01, 0x06,

      // iBeacon Manufacturer Specific Data AD structure (27 bytes)
      // Length=26, Type=0xFF (ManufacturerSpecificData)
      0x1A, 0xFF,
      // Apple Inc. company ID — little-endian (0x004C)
      0x4C, 0x00,
      // iBeacon subtype (0x02) + payload length (0x15 = 21 bytes)
      0x02, 0x15,
      // Proximity UUID: 01020304-0506-0708-090A-0B0C0D0E0F10
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
      0x0D, 0x0E, 0x0F, 0x10,
      // Major = 1 (location zone)
      0x00, 0x01,
      // Minor = 2 (sub-zone)
      0x00, 0x02,
      // TX Power at 1 m = -59 dBm (0xC5 signed) — used for ranging
      0xC5};
  uint8_t mac[6] = {0xFF, 0xEE, 0xDD, 0xCC,
                    0xBB, 0xAC}; // BLE address LSB-first, on-air (MSB-first):
                                 // FF:EE:DD:CC:BB:AC
  // Payload length = MAC (6) + AD data (30) = 36 bytes
  uint8_t pdu_len = 6 + 30;
  uint8_t payload[38]; // 2-byte header + 36-byte PDU

  payload[0] = 0x42;    // PDU type: ADV_NONCONN_IND (bits[1:0]=10) + TxAdd=1
                        // (bit6) for random address
  payload[1] = pdu_len; // length of everything after the 2-byte header
  for (int i = 0; i < 6; i++)
    payload[2 + i] = mac[i];
  for (int i = 0; i < 30; i++)
    payload[8 + i] = adv_data[i];

  // SetPacketParams for BLE — exactly matching reference prepareForBeacon():
  // SetPacketParams(0x20, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00)
  // [0x8C, ConnectionState=0x20, CrcLength=0x10, BleTestPayload=0x04,
  // Whitening=0x00, 0x00, 0x00, 0x00] NOTE: whitening is enabled via
  // SetWhiteningSeed (register 0x09C5), NOT via this byte
  uint8_t cmd_pkt[8] = {0x8C, 0x20, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00};
  SX1280_SendCommand(cmd_pkt, 8);

  // Set whitening seed — reference uses SetWhiteningSeed(reg 0x09C5)
  // Seeds from reference channel_table: CH37=0x53, CH38=0x33, CH39=0x73
  SX1280_WriteRegister(0x09C5, &white_seed, 1);

  // Write full 38-byte PDU to TX buffer at offset 0x80 (matches reference
  // SetupAdvPdu/SetBufferBaseAddresses) (2-byte header + 6-byte MAC + 30-byte
  // iBeacon AD data = 38 bytes total)
  SX1280_WriteBuffer(0x80, payload, 38);

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
// Send beacon on all 3 BLE advertising channels
// Passes TxDone result for each channel back to caller
// -----------------------------------------------------------------------
void SX1280_SendBLEBeacon(void) {
  // Whitening seeds match reference project channel_table exactly:
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
// Initialize the RTC to use the external LSE clock source (32.768 kHz)
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

// -----------------------------------------------------------------------
// Set the RTC Alarm to fire 1 second from current time
// -----------------------------------------------------------------------
void RTC_SetAlarm_1s(void) {
  // Clear Register Synchronized Flag (RSF) and wait for synchronization (critical after wakeup)
  WRITE_REG(RTC->CRL, (uint32_t)~RTC_CRL_RSF);
  while ((RTC->CRL & RTC_CRL_RSF) == 0) {
  }

  while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {
  }

  // Read current 32-bit counter value
  uint32_t cnt = ((uint32_t)RTC->CNTH << 16) | RTC->CNTL;
  uint32_t alarm_val = cnt + 1; // 1 second alarm

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
// Put STM32 into Stop Mode and wake up via RTC alarm interrupt
// -----------------------------------------------------------------------
void STM32_EnterStopMode(void) {
  // 1. Put SX1280 to sleep
  SX1280_Sleep();

  // 1b. Brief post-TX LED flash (10ms) - completely safe since RF is sleeping
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);   // LED ON
  HAL_Delay(10);
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // LED OFF

  // 2. Disable SPI1 and clamp pins to Input Pull-Down to prevent leaks/floating states
  __HAL_SPI_DISABLE(&hspi1);

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  // SCK (PA5), MISO (PA6), MOSI (PA7) -> Input with Pull-Down (prevents floating SPI bus)
  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // DIO1 (PB0) and BUSY (PB15) -> Input with Pull-Down
  GPIO_InitStruct.Pin = LORA_DIO1_Pin | LORA_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Keep NSS (PA4) and NRST (PA3) HIGH to lock radio state
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);





  // 3. Set the RTC alarm for 1 second from now
  RTC_SetAlarm_1s();

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
  /* USER CODE BEGIN 2 */
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

  // Enable debug support during low power modes (keeps SWD active in Stop Mode)
  HAL_DBGMCU_EnableDBGStopMode();

  RTC_Init_LowPower(); // Initialize low-power RTC wakeup

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // 1. Transmit BLE beacon on all 3 channels
    SX1280_SendBLEBeacon();

    // 2. Shut down peripherals, flash LED, and enter Stop Mode (wakes up in 1s)
    STM32_EnterStopMode();
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

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

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
uint8_t SX1280_SendOnChannel(uint8_t f0, uint8_t f1, uint8_t f2, uint8_t ch);
void SX1280_InitBLE(void);
void SX1280_VerifyInit(void);
void SX1280_SendBLEBeacon(uint8_t *ch37_ok, uint8_t *ch38_ok, uint8_t *ch39_ok);
/* USER CODE END PFP */

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
            while(1) {
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

    // 2. Set packet type = BLE (0x05)
    uint8_t cmd_pkt_type[2] = {0x8A, 0x05};
    SX1280_SendCommand(cmd_pkt_type, 2);

    // 3. Set TX and RX buffer base addresses both to 0x00
    uint8_t cmd_buf[3] = {0x8F, 0x00, 0x00};
    SX1280_SendCommand(cmd_buf, 3);

    // 4. Set modulation params: 1 Mbps, MOD_IND=0.5, BT=0.5
    uint8_t cmd_mod[4] = {0x8B, 0x45, 0x01, 0x20};
    SX1280_SendCommand(cmd_mod, 4);

    // 5. Set TX output power = +13 dBm (0x1F), ramp time = 20us (0x20)
    uint8_t cmd_tx_params[3] = {0x8E, 0x1F, 0x20};
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
    //    [opcode, IRQmask_H, IRQmask_L, DIO1_H, DIO1_L, DIO2_H, DIO2_L, DIO3_H, DIO3_L]
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
    uint8_t cmd_status   = (st >> 2) & 0x07;
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
                              uint8_t ch_index) {
    // Set RF frequency for this channel
    SX1280_SetFrequency(freq0, freq1, freq2);

    // Build BLE ADV_NONCONN_IND PDU
    uint8_t adv_data[] = {
        0x02, 0x01, 0x06,                    // Flags: LE General Discoverable
        0x07, 0x09, 'L','o','R','a','B','d'  // Complete Local Name: "LoraBd"
    };
    uint8_t mac[6]    = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA};
    uint8_t pdu_len   = 6 + 11;  // 6 bytes MAC + 11 bytes AD data = 17
    uint8_t payload[19];

    payload[0] = 0x02;    // PDU type: ADV_NONCONN_IND
    payload[1] = pdu_len; // length of everything after the 2-byte header
    for (int i = 0; i < 6;  i++) payload[2 + i] = mac[i];
    for (int i = 0; i < 11; i++) payload[8 + i] = adv_data[i];

    // SetPacketParams for BLE:
    // ConnectionState=0x00, CrcLength=0x10 (3-byte CRC),
    // BleTestPayload=0x00, Whitening=0x00 (enabled)
    uint8_t cmd_pkt[8] = {0x8C, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    SX1280_SendCommand(cmd_pkt, 8);

    // Write PDU to TX buffer starting at offset 0
    SX1280_WriteBuffer(0x00, payload, 19);

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
void SX1280_SendBLEBeacon(uint8_t *ch37_ok, uint8_t *ch38_ok, uint8_t *ch39_ok) {
    *ch37_ok = SX1280_SendOnChannel(0xB8, 0xC4, 0xEC, 37); // 2402 MHz
    HAL_Delay(10);
    *ch38_ok = SX1280_SendOnChannel(0xBA, 0x9D, 0x89, 38); // 2426 MHz
    HAL_Delay(10);
    *ch39_ok = SX1280_SendOnChannel(0xBE, 0xC4, 0xEC, 39); // 2480 MHz
    HAL_Delay(10);
}

/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  // Reconnect window: gives ST-Link time to attach after flash
  // Increased to 5s to avoid slow-blink-forever on fresh flash
  for (volatile uint32_t i = 0; i < 5000000; i++);
  /* USER CODE END 1 */

  HAL_Init();
  SystemClock_Config();
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
      while(1) {
          HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
          HAL_Delay(1000);
      }
  }

  LED_Blink(1); // 1 blink = SX1280 alive

  SX1280_InitBLE();

  // Diagnostic register readback — see SX1280_VerifyInit comments
  // Expected: 9 blinks, 6 blinks, 3 blinks, 1 blink
  SX1280_VerifyInit();

  /* USER CODE END 2 */

  // -----------------------------------------------------------------------
  // Main beacon TX loop
  //
  // LED pattern each cycle (~500ms total):
  //   Short blink (150ms) = TX fired on all 3 channels
  //   100ms gap
  //   Short blink (150ms) = ALL 3 TxDone confirmed via DIO1  ✅
  //   Long  blink (500ms) = at least one TxDone NOT confirmed ⚠️
  //   200ms gap
  //
  // short-short = working correctly
  // short-LONG  = TX confirmation failed on one or more channels
  // -----------------------------------------------------------------------
  while (1)
  {
    uint8_t ch37_ok, ch38_ok, ch39_ok;
    SX1280_SendBLEBeacon(&ch37_ok, &ch38_ok, &ch39_ok);

    // First blink: beacon cycle fired
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);

    // Second blink: confirmation result
    if (ch37_ok && ch38_ok && ch39_ok) {
        // All 3 channels confirmed — short blink
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        HAL_Delay(150);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    } else {
        // One or more channels failed — long blink
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        HAL_Delay(500);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }
    // Second blink: confirmation result (SWAPPED)
//	if (ch37_ok && ch38_ok && ch39_ok) {
//		// All 3 channels confirmed — LONG blink ✅
//		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
//		HAL_Delay(500);
//		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
//	} else {
//		// One or more channels failed — short blink ⚠️
//		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
//		HAL_Delay(150);
//		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
//	}
    HAL_Delay(200);
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Pre-load output register before configuring as output (prevents glitch)
  HAL_GPIO_WritePin(LED_GPIO_Port,       LED_Pin,       GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port,  LORA_NSS_Pin,  GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);

  // LED (PC13)
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  // SX1280 NSS (PA4) — chip select, active low
  GPIO_InitStruct.Pin = LORA_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LORA_NSS_GPIO_Port, &GPIO_InitStruct);

  // SX1280 NRESET (PA3) — chip reset, active low
  GPIO_InitStruct.Pin = LORA_NRST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LORA_NRST_GPIO_Port, &GPIO_InitStruct);

  // SX1280 BUSY (PB15) and DIO1 (PB0) — inputs, no pull resistor
  GPIO_InitStruct.Pin = LORA_DIO1_Pin | LORA_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  while (1)
  {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    HAL_Delay(100);
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif

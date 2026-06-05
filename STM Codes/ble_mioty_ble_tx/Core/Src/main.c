/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
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
void SX1280_SetFrequency(uint8_t b0, uint8_t b1, uint8_t b2);
void SX1280_SendOnChannel(uint8_t f0, uint8_t f1, uint8_t f2, uint8_t ch);
void SX1280_InitBLE(void);
void SX1280_SendBLEBeacon(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void LED_Blink(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        HAL_Delay(150);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
        HAL_Delay(150);
    }
    HAL_Delay(600);
}

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

uint8_t SX1280_GetStatus(void) {
    uint8_t tx[2] = {0xC0, 0x00};
    uint8_t rx[2] = {0x00, 0x00};
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
    return rx[1];
}

void SX1280_SendCommand(uint8_t *cmd, uint8_t len) {
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, cmd, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

void SX1280_WriteBuffer(uint8_t offset, uint8_t *data, uint8_t len) {
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    uint8_t header[2] = {0x1A, offset};
    HAL_SPI_Transmit(&hspi1, header, 2, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

void SX1280_SetFrequency(uint8_t b0, uint8_t b1, uint8_t b2) {
    uint8_t cmd[4] = {0x86, b0, b1, b2};
    SX1280_SendCommand(cmd, 4);
}

void SX1280_WriteRegister(uint16_t addr, uint8_t *data, uint8_t len) {
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    uint8_t header[3] = {0x18, (addr >> 8) & 0xFF, addr & 0xFF};
    HAL_SPI_Transmit(&hspi1, header, 3, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

void SX1280_InitBLE(void) {
    // 1. Standby
    uint8_t cmd_standby[2] = {0x80, 0x00};
    SX1280_SendCommand(cmd_standby, 2);
    HAL_Delay(10);

    // 2. Set packet type = BLE (0x05)
    uint8_t cmd_pkt_type[2] = {0x8A, 0x05};
    SX1280_SendCommand(cmd_pkt_type, 2);

    // 3. Set buffer base addresses
    uint8_t cmd_buf[3] = {0x8F, 0x00, 0x00};
    SX1280_SendCommand(cmd_buf, 3);

    // 4. Set modulation params: 1Mbps, MOD_IND=0.5, BT=0.5
    uint8_t cmd_mod[4] = {0x8B, 0x45, 0x01, 0x20};  // FIXED
    SX1280_SendCommand(cmd_mod, 4);

    // 5. Set TX params: +13dBm, ramp 20us
    uint8_t cmd_tx_params[3] = {0x8E, 0x1F, 0x20};
    SX1280_SendCommand(cmd_tx_params, 3);

    // 6. Set BLE advertising Access Address = 0x8E89BED6
    uint8_t access_addr[4] = {0x8E, 0x89, 0xBE, 0xD6};
    SX1280_WriteRegister(0x09CF, access_addr, 4);  // ADDED

    // 7. Set CRC init seed = 0x555555 (BLE advertising)
    uint8_t crc_seed[3] = {0x55, 0x55, 0x55};
    SX1280_WriteRegister(0x09C7, crc_seed, 3);     // ADDED

    // 8. Set IRQ mask: TxDone on DIO1
    uint8_t cmd_irq[9] = {0x8D, 0x00,0x01, 0x00,0x01, 0x00,0x00, 0x00,0x00};
    SX1280_SendCommand(cmd_irq, 9);
}
void SX1280_SendOnChannel(uint8_t freq0, uint8_t freq1, uint8_t freq2,
                           uint8_t ch_index) {
    // Set frequency for this channel
    SX1280_SetFrequency(freq0, freq1, freq2);

    // Set packet params
    uint8_t adv_data[] = {
        0x02, 0x01, 0x06,
        0x07, 0x09, 'L','o','R','a','B','d'
    };
    uint8_t mac[6] = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA};
    uint8_t pdu_length = 6 + 11; // MAC + AD data = 17
    uint8_t payload[19];

    payload[0] = 0x02;        // PDU type: ADV_NONCONN_IND
    payload[1] = pdu_length;  // 17

    for (int i = 0; i < 6; i++)  payload[2 + i] = mac[i];
    for (int i = 0; i < 11; i++) payload[8 + i] = adv_data[i];

    // FIXED SetPacketParams for BLE:
    // [opcode, ConnectionState=0x00, CRC=0x10(3B), TestPayload=0x00,
    //  Whitening=0x00(enabled), 0x00, 0x00, 0x00]
    uint8_t cmd_pkt[8] = {0x8C, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    SX1280_SendCommand(cmd_pkt, 8);
    SX1280_WriteBuffer(0x00, payload, 19);

    uint8_t cmd_tx[4] = {0x83, 0x00, 0x00, 0x64};
    SX1280_SendCommand(cmd_tx, 4);
    SX1280_WaitBusy();
    HAL_Delay(5);
}

void SX1280_SendBLEBeacon(void) {
    // Advertise on all 3 BLE advertising channels
    SX1280_SendOnChannel(0xB8, 0xC4, 0xEC, 37); // 2402 MHz
    HAL_Delay(10);
    SX1280_SendOnChannel(0xBA, 0x9D, 0x89, 38); // 2426 MHz
    HAL_Delay(10);
    SX1280_SendOnChannel(0xBE, 0xC4, 0xEC, 39); // 2480 MHz
    HAL_Delay(10);
}
/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  // ST-Link reconnect window — 2 seconds before code runs
  for (volatile uint32_t i = 0; i < 2000000; i++);
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);

  // Reset SX1280 properly
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);  // assert reset
  HAL_Delay(5);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET);    // release reset
  HAL_Delay(10);                                          // wait for boot


  // Check SX1280 is alive
  uint8_t status = SX1280_GetStatus();

  if (status == 0x00 || status == 0xFF) {
      // SX1280 not found — slow blink forever
      while(1) {
          HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
          HAL_Delay(1000);
      }
  }

  // SX1280 alive
  LED_Blink(1);   // 1 blink = SX1280 alive

  // Initialize BLE mode
  SX1280_InitBLE();
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    SX1280_SendBLEBeacon();

    // Fast blink = BLE beacon firing
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    HAL_Delay(200);
    /* USER CODE END 3 */
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
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_SPI1_Init(void)
{
  /* USER CODE BEGIN SPI1_Init 0 */
  /* USER CODE END SPI1_Init 0 */
  /* USER CODE BEGIN SPI1_Init 1 */
  /* USER CODE END SPI1_Init 1 */

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
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN SPI1_Init 2 */
  /* USER CODE END SPI1_Init 2 */
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Set initial output states before configuring as outputs
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET);  // NRESET high = not in reset

  // LED
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  // NSS
  GPIO_InitStruct.Pin = LORA_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LORA_NSS_GPIO_Port, &GPIO_InitStruct);

  // SX1280 NRESET (PA3)
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // BUSY and DIO1 as inputs
  GPIO_InitStruct.Pin = LORA_DIO1_Pin | LORA_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  while (1)
  {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    HAL_Delay(100);
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif

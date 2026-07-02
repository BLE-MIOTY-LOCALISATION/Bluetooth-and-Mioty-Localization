/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    : main.cpp
 * @brief   : Mioty (TS-UNB) transmitter — sends one packet every 2 minutes.
 *
 * Base: 2_ble_mioty_ble_tx_ble_working (BLE working reference)
 * Added: Fraunhofer TsUnb Mioty library integration
 *
 * --- Clock Configuration ---
 * CPU:  8 MHz (HSE direct, PLL DISABLED)
 *   -> Required by STM32TsUnb.h which hardcodes DWT timing for 8 MHz.
 *      Running at any other clock speed will corrupt Mioty symbol timing.
 * SPI:  4 MHz (BAUDRATEPRESCALER_2 on 8 MHz PCLK2)
 *   -> Sufficient for SX1280 register writes; SX1280 supports up to 18 MHz.
 *
 * --- Startup LED Sequence ---
 * On power-up, before the first Mioty TX, the LED blinks to confirm
 * the code is running correctly:
 *   3 rapid blinks (100ms on/off) = firmware started, SX1280 reset done
 *   2-second pause
 *   1 long blink (500ms)          = Mioty node initialised, about to TX
 *   Then: first transmission fires immediately
 *
 * --- Main Loop (every 2 minutes) ---
 *   1. LED: 3 short blinks = TX in progress
 *   2. Send 20-byte Mioty packet (counter + zero padding)
 *   3. HAL_Delay(120000) = 2 minute wait
 *
 * --- Spectrum Analyser Verification ---
 * Mioty (TS-UNB) via SX1280 transmits in the 2.4 GHz ISM band.
 * On your spectrum analyser:
 *   Center: 2450 MHz (mid-band ISM)
 *   Span:   200 MHz (to see full 2.4 GHz band, 2400-2500 MHz)
 *   RBW:    100 kHz or lower for best visibility
 *   Expected: A short frequency-hopped burst (~2-4 seconds duration)
 *             appearing as a series of narrow spikes across the band.
 *             The burst repeats every 2 minutes.
 *             Signal level: +13 dBm at antenna port.
 ******************************************************************************
 */
/* USER CODE END Header */

#include "main.h"
#include "STM32TsUnbTemplates.h"

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);

/* USER CODE BEGIN PFP */
void LED_Startup_Sequence(void);
void LED_TX_Blink(void);
void prepareForMioty(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
using namespace TsUnbLib::STM32;
TsUnb_EU1_Lambda80_t TsUnb_Node;

// --- Mioty Network Credentials ---
// Change these to match your Mioty gateway's provisioned device.
#define MAC_NETWORK_KEY  0x53, 0xad, 0xf8, 0x02, 0xb1, 0x97, 0xf2, 0x73, \
                         0x8d, 0x5d, 0xdd, 0xa5, 0x77, 0xe1, 0xfa, 0x9c
#define MAC_EUI64        0x70, 0xb3, 0xd5, 0x67, 0x70, 0xff, 0x01, 0x70

// TX power: +13 dBm = maximum range. Reduce to lower value (e.g. 0) to
// limit range or reduce current draw during transmission.
#define TRANSMIT_PWR     13

// Transmission interval: 5,000 ms = 5 seconds
#define TX_INTERVAL_MS   5000

// -----------------------------------------------------------------------
// Startup LED Sequence
// Runs ONCE on boot to confirm firmware is alive before first TX.
//
// Pattern:
//   3x rapid blinks (100ms on/off) → firmware started, SX1280 reset done
//   2-second pause
//   1x long blink (500ms)           → Mioty node initialised, TX imminent
// -----------------------------------------------------------------------
void LED_Startup_Sequence(void)
{
    // 3 rapid blinks: firmware started
    for (int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);   // ON (active high)
        HAL_Delay(100);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // OFF
        HAL_Delay(100);
    }

    HAL_Delay(2000); // 2-second pause

    // 1 long blink: Mioty node ready, about to transmit
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);   // ON
    HAL_Delay(500);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // OFF
    HAL_Delay(200);
}

// -----------------------------------------------------------------------
// TX Heartbeat LED — 3 short blinks before each Mioty transmission
// -----------------------------------------------------------------------
void LED_TX_Blink(void)
{
    for (int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);   // ON
        HAL_Delay(80);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // OFF
        HAL_Delay(80);
    }
}

// -----------------------------------------------------------------------
// Initialise the Mioty (TsUnb) node with credentials and TX power.
// Must be called once after SX1280 hard reset.
// -----------------------------------------------------------------------
void prepareForMioty(void)
{
    if (TsUnb_Node.init() < 0)
    {
        // Initialisation failed (likely SPI connection error or chip not responding)
        // Flash the LED rapidly (150ms) to indicate hardware init failure.
        while (1)
        {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            HAL_Delay(150);
        }
    }
    TsUnb_Node.Tx.setTxPower(TRANSMIT_PWR);
    TsUnb_Node.Mac.setNetworkKey(MAC_NETWORK_KEY);
    TsUnb_Node.Mac.setAddress(MAC_EUI64);
    TsUnb_Node.Mac.extPkgCnt = 0x01; // Start packet counter at 1
}
/* USER CODE END 0 */

/**
 * @brief Application entry point.
 */
int main(void)
{
    /* USER CODE BEGIN 1 */
    // ST-Link reconnect window: gives debugger time to attach after reset/flash.
    // Prevents the MCU racing ahead before the debugger connects.
    for (volatile uint32_t i = 0; i < 5000000; i++);
    /* USER CODE END 1 */

    /* MCU Configuration --------------------------------------------------------*/
    HAL_Init();

    // Clock: 8 MHz (HSE direct, PLL OFF) — required for Mioty symbol timing
    SystemClock_Config();

    MX_GPIO_Init();
    MX_SPI1_Init(); // SPI1 at 4 MHz (BAUDRATEPRESCALER_2 on 8 MHz clock)

    /* USER CODE BEGIN 2 */
    __HAL_RCC_CLEAR_RESET_FLAGS();

    // Hard reset SX1280: drive NRST LOW for 5 ms, release, wait 10 ms for boot
    HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);

    // Startup LED sequence — confirms firmware is alive
    LED_Startup_Sequence();

    // Initialise Mioty node (sets network key, EUI64, TX power)
    prepareForMioty();

    uint32_t packet_counter = 0;
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */
        /* USER CODE BEGIN 3 */

        // 3 short blinks = TX about to fire
        LED_TX_Blink();

        // Build 20-byte payload: first 4 bytes = packet counter, rest = 0x00
        uint8_t payload[20] = {0};
        payload[0] = (packet_counter >> 24) & 0xFF;
        payload[1] = (packet_counter >> 16) & 0xFF;
        payload[2] = (packet_counter >>  8) & 0xFF;
        payload[3] =  packet_counter        & 0xFF;

        // Transmit Mioty packet (frequency-hopped burst, ~2-4 seconds on air)
        TsUnb_Node.send(payload, sizeof(payload));
        packet_counter++;

        // Wait 2 minutes before next transmission
        // (Total cycle = ~2-4s TX burst + 120s delay ≈ 2 minutes)
        HAL_Delay(TX_INTERVAL_MS);
    }
    /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 *
 * Clock: 8 MHz from HSE crystal (PLL DISABLED)
 * Reason: STM32TsUnb.h DWT timing is hardcoded for 8 MHz CPU clock.
 *         PLL is intentionally disabled — do NOT re-enable.
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Use HSE (external 8 MHz crystal) directly — no PLL
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_NONE; // PLL OFF — required for 8 MHz
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    // SYSCLK = HSE = 8 MHz, all bus dividers = 1
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSE;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;  // HCLK  = 8 MHz
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;    // PCLK1 = 8 MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;    // PCLK2 = 8 MHz (SPI1 source)
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) Error_Handler();
}

/**
 * @brief SPI1 Initialisation
 *
 * SPI clock: 4 MHz (8 MHz PCLK2 / BAUDRATEPRESCALER_2)
 * SX1280 maximum SPI rate: 18 MHz — 4 MHz is well within spec.
 */
static void MX_SPI1_Init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; // 8 MHz / 2 = 4 MHz SPI clock
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

/**
 * @brief GPIO Initialisation
 * PC13 = LED (active low)
 * PA4  = LORA_NSS (chip select, active low)
 * PA3  = LORA_NRST (reset, active low)
 * PB0  = LORA_DIO1 (input, TX done interrupt)
 * PB15 = LORA_BUSY (input, chip busy flag)
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Pre-load outputs before configuring as outputs (prevents glitch on startup)
    HAL_GPIO_WritePin(LED_GPIO_Port,       LED_Pin,       GPIO_PIN_RESET); // LED OFF (active high)
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port,  LORA_NSS_Pin,  GPIO_PIN_SET);   // NSS deselected
    HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);   // NRST released

    // LED (PC13) — output push-pull
    GPIO_InitStruct.Pin   = LED_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

    // NSS (PA4) and NRST (PA3) — output push-pull
    GPIO_InitStruct.Pin   = LORA_NSS_Pin | LORA_NRST_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // DIO1 (PB0) and BUSY (PB15) — inputs, no pull
    GPIO_InitStruct.Pin  = LORA_DIO1_Pin | LORA_BUSY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/**
 * @brief Error Handler — rapid LED blink forever (100ms toggle)
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        HAL_Delay(100);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif

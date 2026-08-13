/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Magnetometer Heading Test — STM32F103CB + IIS2MDC
 *
 *                   Bench tool only: continuously takes single-shot
 *                   magnetometer readings and computes a compass heading,
 *                   to validate the heading formula and axis mapping used
 *                   in 9_Mag_ble_mioty_tracker before trusting it there.
 *
 *                   No radio, no RTC, no Stop mode — the MCU never sleeps,
 *                   so Live Expressions update continuously with no
 *                   HAL_DBGMCU_EnableDBGStopMode() workaround needed.
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <math.h>

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
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

#define CALIBRATION_DURATION_MS  15000 // rotate the board through a full turn during this window
#define CALIBRATION_SAMPLE_MS    100
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c2;

/* USER CODE BEGIN PV */
volatile uint8_t  dbg_mag_read_ok  = 0;  // last IIS2MDC_ReadXYZ() result
volatile int16_t  dbg_mag_x = 0, dbg_mag_y = 0, dbg_mag_z = 0; // last raw sample
volatile uint16_t dbg_heading_deg  = 0;  // computed, CALIBRATED compass heading, 0-359, magnetic north
volatile int16_t  dbg_offset_x = 0, dbg_offset_y = 0; // hard-iron offsets found during boot calibration
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C2_Init(void);
/* USER CODE BEGIN PFP */
void LED_Blink(uint8_t count);
void I2C2_BusRecovery(void);
uint8_t IIS2MDC_ReadRegister(uint8_t reg, uint8_t *value);
uint8_t IIS2MDC_WriteRegister(uint8_t reg, uint8_t value);
uint8_t IIS2MDC_CheckWhoAmI(void);
uint8_t IIS2MDC_ReadXYZ(int16_t *x, int16_t *y, int16_t *z);
void RunHardIronCalibration(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// LED helper: blinks 'count' times (150ms on/off), then 600ms pause
void LED_Blink(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    HAL_Delay(150);
  }
  HAL_Delay(600);
}

// I2C2 bus recovery -- run before MX_I2C2_Init() claims PB10/PB11, in case
// a prior session left an I2C transaction stuck mid-byte. Safe to run even
// if the bus is already fine. Ported verbatim from 7_joulemeter_test.
void I2C2_BusRecovery(void) {
  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  g.Mode = GPIO_MODE_OUTPUT_OD;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &g);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10 | GPIO_PIN_11, GPIO_PIN_SET);
  HAL_Delay(1);

  for (int i = 0; i < 9; i++) {
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == GPIO_PIN_SET) break;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
    HAL_Delay(1);
  }

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

// Single-shot low-power sample: trigger conversion, poll for data-ready,
// read X/Y/Z, return sensor to idle. No continuous mode.
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
// Hard-iron calibration: without this, a constant offset in the raw X/Y
// readings (from nearby ferrous material, magnetized PCB traces, or the
// sensor's own inherent bias) shifts the circle traced by (mx, my) as the
// board rotates so it's no longer centered on the origin -- atan2() then
// only ever sees a limited arc of angles instead of the full 0-360 range.
// Fix: rotate the board through a full turn while recording min/max per
// axis, then use offset = (min+max)/2 to re-center future readings before
// computing heading.
//
// Solid LED ON for the whole window -- rotate the board slowly and evenly
// through a full 360 degrees before it goes out.
// -----------------------------------------------------------------------
void RunHardIronCalibration(void) {
  int16_t min_x = INT16_MAX, max_x = INT16_MIN;
  int16_t min_y = INT16_MAX, max_y = INT16_MIN;

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  uint32_t elapsed_ms = 0;
  while (elapsed_ms < CALIBRATION_DURATION_MS) {
    int16_t mx = 0, my = 0, mz = 0;
    if (IIS2MDC_ReadXYZ(&mx, &my, &mz)) {
      if (mx < min_x) min_x = mx;
      if (mx > max_x) max_x = mx;
      if (my < min_y) min_y = my;
      if (my > max_y) max_y = my;
    }
    HAL_Delay(CALIBRATION_SAMPLE_MS);
    elapsed_ms += CALIBRATION_SAMPLE_MS;
  }

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  dbg_offset_x = (int16_t)(((int32_t)min_x + (int32_t)max_x) / 2);
  dbg_offset_y = (int16_t)(((int32_t)min_y + (int32_t)max_y) / 2);

  LED_Blink(5); // 5 blinks = calibration complete
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  for (volatile uint32_t i = 0; i < 5000000; i++)
    ;
  /* USER CODE END 1 */

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  I2C2_BusRecovery();
  MX_I2C2_Init();

  /* USER CODE BEGIN 2 */
  // Magnetometer presence check -- 2 blinks = WHO_AM_I OK, 8 blinks = fail.
  // Retried a few times: unlike 7_joulemeter_test/9_Mag_ble_mioty_tracker
  // (where a forced-idle write+readback happens first, incidentally
  // "warming up" the I2C bus before WHO_AM_I is ever checked), this is the
  // very first I2C2 transaction attempted here -- and STM32F1's I2C
  // peripheral can glitch on the first transaction right after init even
  // when the bus and device are fine. A retry absorbs that instead of
  // mistaking it for a real hardware fault.
  uint8_t mag_ok = 0;
  for (uint8_t attempt = 0; attempt < 3 && !mag_ok; attempt++) {
    mag_ok = IIS2MDC_CheckWhoAmI();
    if (!mag_ok) HAL_Delay(10);
  }
  LED_Blink(mag_ok ? 2 : 8);
  if (!mag_ok) {
    while (1) {
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      HAL_Delay(1000);
    }
  }
  // Boot-time hard-iron calibration -- rotate the board through a full
  // turn while the LED is solid ON; goes out and blinks 5 times when done.
  RunHardIronCalibration();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    int16_t mx = 0, my = 0, mz = 0;
    dbg_mag_read_ok = IIS2MDC_ReadXYZ(&mx, &my, &mz);
    if (dbg_mag_read_ok) {
      dbg_mag_x = mx;
      dbg_mag_y = my;
      dbg_mag_z = mz;

      // 2D compass bearing from the X/Y field components, hard-iron
      // corrected using the offsets found during boot calibration. Still
      // no tilt compensation (no accelerometer here) -- only meaningful
      // with the board roughly level.
      int16_t cx = (int16_t)(mx - dbg_offset_x);
      int16_t cy = (int16_t)(my - dbg_offset_y);
      float heading_deg_f = atan2f((float)cy, (float)cx) * 57.2957795f;
      if (heading_deg_f < 0) heading_deg_f += 360.0f;
      dbg_heading_deg = (uint16_t)heading_deg_f;
    }

    // Heartbeat blink -- proves the loop is alive, independent of the
    // actual heading value (which needs a debugger to read).
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    HAL_Delay(500);
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

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

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
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{
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
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */

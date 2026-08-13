/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    : main.h
 * @brief   : Dual-protocol BLE + Mioty tracker — STM32F103CB + SX1280
 *
 *  GPIO Pin Assignments (matches hardware layout):
 *    PC13 = LED         (active high, output push-pull)
 *    PA3  = LORA_NRST   (reset, active low, output push-pull)
 *    PA4  = LORA_NSS    (chip select, active low, output push-pull)
 *    PA5  = SPI1_SCK    (SPI1 alternate function)
 *    PA6  = SPI1_MISO   (SPI1 alternate function)
 *    PA7  = SPI1_MOSI   (SPI1 alternate function)
 *    PB0  = LORA_DIO1   (TX done IRQ, input no-pull)
 *    PB15 = LORA_BUSY   (radio busy flag, input no-pull)
 ******************************************************************************
 */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* Exported functions prototypes */
void Error_Handler(void);

/* GPIO Pin Definitions */
#define LED_Pin              GPIO_PIN_13
#define LED_GPIO_Port        GPIOC

#define LORA_NSS_Pin         GPIO_PIN_4
#define LORA_NSS_GPIO_Port   GPIOA

#define LORA_NRST_Pin        GPIO_PIN_3
#define LORA_NRST_GPIO_Port  GPIOA

#define LORA_DIO1_Pin        GPIO_PIN_0
#define LORA_DIO1_GPIO_Port  GPIOB

#define LORA_BUSY_Pin        GPIO_PIN_15
#define LORA_BUSY_GPIO_Port  GPIOB

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

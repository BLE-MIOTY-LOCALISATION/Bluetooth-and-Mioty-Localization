#ifndef SX1280_HAL_H
#define SX1280_HAL_H

#include <stdint.h>

/*
 * Thin HAL for the SX1280 radio used in the BLE TX STM32 project.
 * It abstracts the low‑level SPI command construction into readable
 * function calls while keeping the original register‑level behaviour.
 *
 * The HAL expects the global SPI handle `hspi1` and the GPIO definitions
 * from `main.h` (LED, LORA_* pins) to be available – these are provided by
 * the STM32Cube HAL generated code.
 */

void SX1280_HAL_WaitBusy(void);
void SX1280_HAL_SendCommand(const uint8_t *cmd, uint8_t len);
void SX1280_HAL_WriteRegister(uint16_t addr, const uint8_t *data, uint8_t len);
uint8_t SX1280_HAL_ReadRegister(uint16_t addr);
void SX1280_HAL_WriteBuffer(uint8_t offset, const uint8_t *data, uint8_t len);
uint8_t SX1280_HAL_GetStatus(void);

void SX1280_HAL_Init(void);                // basic init (standby, chip reset)
void SX1280_HAL_InitBLE(void);             // configure radio for BLE advertising
void SX1280_HAL_SetFrequency(uint8_t b0, uint8_t b1, uint8_t b2);
void SX1280_HAL_SetTxPower(uint8_t power, uint8_t ramp);
void SX1280_HAL_SetAccessAddress(uint32_t aa);
void SX1280_HAL_SetCrcSeed(uint32_t seed);
void SX1280_HAL_MapTxDoneIRQ(void);

#endif // SX1280_HAL_H

#pragma once

#include "main.h"

// SPI1 handle declared in main
extern SPI_HandleTypeDef hspi1;

template <uint16_t SYMBOL_RATE_MULT = 48>
class STM32TsUnb
{
public:
    uint32_t start_cycle;
    uint64_t target_cycles;
    double cycles_per_symbol;
    double precise_target;

    STM32TsUnb()
    {
        // Enable DWT Cycle Counter
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        // At 8 MHz CPU clock: cycles per symbol = 8,000,000 / (49.591064453125 * SYMBOL_RATE_MULT)
        cycles_per_symbol = 8000000.0 / (49.591064453125 * (double)SYMBOL_RATE_MULT);
    }

    void spiInit(void) {}
    void spiDeinit(void) {}

    void spiSend(const uint8_t *const dataOut, const uint8_t numBytes)
    {
        HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
        HAL_SPI_Transmit(&hspi1, const_cast<uint8_t*>(dataOut), numBytes, HAL_MAX_DELAY);
        HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
    }

    void spiSendReceive(uint8_t *const dataInOut, const uint8_t numBytes)
    {
        uint8_t readData[numBytes];
        HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
        HAL_SPI_TransmitReceive(&hspi1, dataInOut, readData, numBytes, HAL_MAX_DELAY);
        HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
        for (int i = 0; i < numBytes; i++)
        {
            dataInOut[i] = readData[i];
        }
    }

    void initTimer()
    {
        precise_target = 0.0;
        target_cycles = 0;
    }

    void startTimer()
    {
        start_cycle = DWT->CYCCNT;
        precise_target = cycles_per_symbol;
        target_cycles = static_cast<uint64_t>(precise_target);
    }

    void stopTimer() {}

    void addTimerDelay(const int32_t count)
    {
        precise_target += cycles_per_symbol * count;
        target_cycles = static_cast<uint64_t>(precise_target);
    }

    void waitTimer()
    {
        while ((DWT->CYCCNT - start_cycle) < target_cycles)
        {
            // Busy wait for high precision symbol boundaries
        }
        precise_target += cycles_per_symbol;
        target_cycles = static_cast<uint64_t>(precise_target);
    }

    void resetWatchdog() {}
};

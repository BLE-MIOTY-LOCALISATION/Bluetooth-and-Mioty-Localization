#pragma once

#include "main.h"

// SPI1 handle declared in main
extern SPI_HandleTypeDef hspi1;

// -----------------------------------------------------------------------
// STM32TsUnb - Hardware Abstraction Layer for the Fraunhofer TsUnb library
//
// Clock: 8 MHz (HSE direct, PLL OFF)
//   - Required by the Mioty symbol timing below.
//   - The cycles_per_symbol calculation is hardcoded for 8 MHz.
//   - Changing CPU clock WILL break Mioty symbol timing.
//
// SPI: 4 MHz (BAUDRATEPRESCALER_2 on 8 MHz PCLK2)
//   - Fast enough for SX1280 register writes.
//   - Leaves headroom for DWT busy-wait timing accuracy.
//
// DWT (Data Watchpoint and Trace) cycle counter is used for
// sub-microsecond symbol boundary timing. This avoids needing
// a hardware timer and works well at 8 MHz.
// -----------------------------------------------------------------------

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
        // Enable DWT Cycle Counter (required for symbol timing)
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        // Dynamically compute cycles per symbol using the actual CPU core frequency (SystemCoreClock)
        // This supports any HSE crystal frequency dynamically.
        cycles_per_symbol = (double)(SystemCoreClock) / (49.591064453125 * (double)SYMBOL_RATE_MULT);
        //*1.00025
        precise_target = 0.0;
        target_cycles = 0;
    }

    void startTimer()
    {
        start_cycle = DWT->CYCCNT;
        if (precise_target == 0.0)
        {
            precise_target = cycles_per_symbol;
        }
        target_cycles = static_cast<uint64_t>(precise_target);
    }

    void stopTimer() {}

    void addTimerDelay(const int32_t count)
    {
        precise_target += cycles_per_symbol * (count - 1);
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

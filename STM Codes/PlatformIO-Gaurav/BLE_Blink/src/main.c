#include "stm32f1xx.h"

#define LED_PIN 13

/*
 * System Clock Configuration with External Oscillators
 *
 * HSE (High Speed External):  8 MHz crystal
 *   - Pins: PD0 (OSC_IN) and PD1 (OSC_OUT)
 *   - Output: 72 MHz (8 MHz × PLL multiplier 9)
 *
 * LSE (Low Speed External):   32.768 kHz crystal
 *   - Pins: PC14 (OSC32_IN) and PC15 (OSC32_OUT)
 *   - Used for RTC (Real Time Clock)
 */

static void SystemClock_Config(void)
{
    /* Enable HSE oscillator */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
    {
        /* Wait for HSE to stabilize */
    }

    /* Enable LSE oscillator for RTC */
    RCC->BDCR |= RCC_BDCR_LSEON;
    while (!(RCC->BDCR & RCC_BDCR_LSERDY))
    {
        /* Wait for LSE to stabilize */
    }

    /* Select LSE as RTC clock source */
    RCC->BDCR |= RCC_BDCR_RTCSEL_LSE;

    /* Configure PLL: HSE as source, multiply by 9 (8 MHz × 9 = 72 MHz) */
    RCC->CFGR &= ~RCC_CFGR_PLLSRC; /* Clear PLL source */
    RCC->CFGR |= RCC_CFGR_PLLSRC;  /* Set to HSE */
    RCC->CFGR &= ~RCC_CFGR_PLLMUL; /* Clear PLL multiplier */
    RCC->CFGR |= RCC_CFGR_PLLMUL9; /* Multiply by 9 */

    /* Enable PLL */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
    {
        /* Wait for PLL to lock */
    }

    /* Set FLASH latency to 2 wait states (required for 72 MHz) */
    FLASH->ACR = FLASH_ACR_LATENCY_2;

    /* Select PLL as system clock source */
    RCC->CFGR &= ~RCC_CFGR_SW;    /* Clear system clock source */
    RCC->CFGR |= RCC_CFGR_SW_PLL; /* Set to PLL */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
        /* Wait for clock switch to complete */
    }
}

int main(void)
{
    /* Initialize system clock with external oscillators */
    SystemClock_Config();

    /* Enable GPIOC clock */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    __IO uint32_t tmp = RCC->APB2ENR;
    (void)tmp;

    /* Configure PC13 as push-pull output, max speed 2 MHz */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;

    while (1)
    {
        GPIOC->ODR ^= (1U << LED_PIN);
        for (volatile uint32_t i = 0; i < 800000; ++i)
        {
            __NOP();
        }
    }
}
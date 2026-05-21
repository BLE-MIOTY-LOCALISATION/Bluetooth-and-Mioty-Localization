#include "stm32f1xx.h"

#define LED_PIN 13

int main(void)
{
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

/*
 * BLE Beacon via SX1280 — STM32F103CB  (bare-metal, no HAL)
 *
 * Transmits ADV_NONCONN_IND on channels 37/38/39 every 5 seconds.
 * Visible as "BLE-STM32" in any BLE scanner.
 *
 * Wiring:
 *   PA3  — SX1280 RST
 *   PA4  — SX1280 NSS  (chip-select, active low)
 *   PA5  — SPI1 SCK
 *   PA6  — SPI1 MISO
 *   PA7  — SPI1 MOSI
 *   PB15 — SX1280 BUSY (input)
 *   PC13 — LED (active low, Blue Pill on-board)
 *
 * ====================================================================
 * LED BLINK REFERENCE  (PC13, active-LOW — ON means LED lit)
 * ====================================================================
 *
 * The LED turns OFF instantly at power-on before anything else runs,
 * so you always start from a known dark state.
 *
 * STARTUP  (runs once):
 *
 *   ① FIVE rapid pulses  (100 ms ON · 150 ms OFF × 5)
 *      = MCU is alive, clocks running, GPIO ready.
 *      You will always see this first.
 *      If you see NOTHING: board is not flashed or no power.
 *
 *   [LED goes dark — silent init: SPI, LSE, SX1280 — up to ~7 s]
 *   During LSE (Y2 crystal) wait you may see slow pips
 *   (200 ms ON every 600 ms) — this is normal.
 *
 *   ② THREE long pulses  (900 ms ON · 500 ms OFF × 3)
 *      = Everything ready, entering main loop.
 *      If you never see these: SX1280 is not responding
 *      (check PB15 BUSY wiring and SX1280 power).
 *
 * RUNNING  (main loop, repeats forever):
 *
 *   Single short pip  (80 ms ON)  every 2 000 ms
 *      = Heartbeat — MCU is alive and the loop is running.
 *
 *   Double flash  (400 ms ON · 200 ms OFF × 2)  every 5 000 ms
 *      = BLE beacon just transmitted on ch 37 / 38 / 39.
 *
 * ERROR  (fatal, never stops):
 *   Rapid 100 ms toggle = something failed during init.
 *
 * QUICK CHEAT SHEET:
 *   Nothing at all       → not flashed / no power
 *   5 fast + then dark   → booting (normal)
 *   5 fast + rapid flash → error during init
 *   5 fast + 3 long      → ready and in main loop
 *   pip every 2 s        → heartbeat (running)
 *   double flash every 5 s → BLE TX
 * ====================================================================
 */

#include "stm32f1xx.h"
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* SX1280 opcodes / registers                                          */
/* ------------------------------------------------------------------ */
#define CMD_SET_STANDBY        0x80
#define CMD_SET_PACKET_TYPE    0x8A
#define CMD_SET_RF_FREQUENCY   0x86
#define CMD_SET_TX_PARAMS      0x8E
#define CMD_SET_BUF_BASE_ADDR  0x8F
#define CMD_SET_MOD_PARAMS     0x8B
#define CMD_SET_PKT_PARAMS     0x8C
#define CMD_WRITE_BUFFER       0x1A
#define CMD_SET_TX             0x83
#define CMD_WRITE_REGISTER     0x18

#define REG_BLE_WHITENING_INIT 0x093C

#define BLE_WHITENING_CH37 (37 | 0x40)  /* 0x65 */
#define BLE_WHITENING_CH38 (38 | 0x40)  /* 0x66 */
#define BLE_WHITENING_CH39 (39 | 0x40)  /* 0x67 */

/* ------------------------------------------------------------------ */
/* BLE channel frequencies                                             */
/* ------------------------------------------------------------------ */
#define FREQ_CH37_HZ 2402000000UL
#define FREQ_CH38_HZ 2426000000UL
#define FREQ_CH39_HZ 2480000000UL

/* ------------------------------------------------------------------ */
/* Beacon settings                                                     */
/* ------------------------------------------------------------------ */
#define BEACON_NAME        "BLE-STM32"
#define BEACON_INTERVAL_MS 5000UL

static const uint8_t BEACON_MAC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};

/* ------------------------------------------------------------------ */
/* GPIO bit-band macros                                                */
/* ------------------------------------------------------------------ */
#define LED_ON()    (GPIOC->BRR  = (1u << 13))
#define LED_OFF()   (GPIOC->BSRR = (1u << 13))
#define NSS_LOW()   (GPIOA->BRR  = (1u << 4))
#define NSS_HIGH()  (GPIOA->BSRR = (1u << 4))
#define RST_LOW()   (GPIOA->BRR  = (1u << 3))
#define RST_HIGH()  (GPIOA->BSRR = (1u << 3))
#define BUSY_READ() ((GPIOB->IDR >> 15) & 1u)

/* ------------------------------------------------------------------ */
/* SysTick — 1 ms tick counter                                        */
/* ------------------------------------------------------------------ */
static volatile uint32_t g_ms = 0;

void SysTick_Handler(void) { g_ms++; }

static uint32_t get_ms(void)    { return g_ms; }
static void     delay_ms(uint32_t ms)
{
    uint32_t t = get_ms();
    while (get_ms() - t < ms) {}
}

/* ------------------------------------------------------------------ */
/* LED helpers                                                         */
/* short blink : 200 ms OFF · 200 ms ON · 200 ms OFF                 */
/* long  blink : 500 ms OFF · 500 ms ON · 500 ms OFF                 */
/* ------------------------------------------------------------------ */
static void blink_short(uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        LED_OFF(); delay_ms(200);
        LED_ON();  delay_ms(200);
        LED_OFF(); delay_ms(200);
    }
}

static void blink_long(uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        LED_OFF(); delay_ms(500);
        LED_ON();  delay_ms(500);
        LED_OFF(); delay_ms(500);
    }
}

/* ------------------------------------------------------------------ */
/* Clock init                                                          */
/*   Y1 (ECS-80-12-33-JGN-TR, 8 MHz) → STM32 PD0/PD1 (HSE)         */
/*     → PLL × 9 = 72 MHz system clock                               */
/*   Y2 (ABS07-32.768KHZ-7-T, 32.768 kHz) → STM32 PC14/PC15 (LSE)  */
/*     → RTC clock source                                             */
/*   XTAL1 (NX2016SA-52MHZ) → SX1280 XOSC pins (not STM32)         */
/*     → SX1280 frequency synthesiser reference (52 MHz in math)     */
/* ------------------------------------------------------------------ */
static void clock_init(void)
{
    /* ---- HSE + PLL → 72 MHz ---- */

    /* 1. Enable HSE (Y1, 8 MHz) and wait for it to stabilise */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    /* 2. Flash: 2 wait states + prefetch buffer (required at 72 MHz) */
    FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;

    /* 3. Bus prescalers: AHB=/1, APB1=/2 (max 36 MHz), APB2=/1 */
    RCC->CFGR = RCC_CFGR_PPRE1_DIV2;

    /* 4. PLL: source = HSE, multiplier = 9 → 8 × 9 = 72 MHz */
    RCC->CFGR |= RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;

    /* 5. Enable PLL and wait for lock */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    /* 6. Switch system clock to PLL */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

    /* 7. SysTick: 1 ms at 72 MHz */
    SysTick_Config(72000);
}

/* ------------------------------------------------------------------ */
/* LSE init — called after gpio_init() so the LED is available        */
/*                                                                     */
/* While waiting for Y2 (32.768 kHz) to stabilise the LED shows       */
/* short rapid pips (150 ms ON · 700 ms OFF) so you can see it is     */
/* working and not stuck.  Y2 can take up to ~2 s on first power-up.  */
/* If Y2 never starts (5 s timeout) the RTC falls back to LSI.        */
/* ------------------------------------------------------------------ */
static void lse_init(void)
{
    /*
     * BDCR is in the backup domain, which is write-protected by default.
     * PWR_CR_DBP must be set or writes to BDCR are silently dropped and
     * LSERDY never sets — the wait loop would hang forever.
     */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    volatile uint32_t _pwr = RCC->APB1ENR; (void)_pwr;
    PWR->CR |= PWR_CR_DBP;

    RCC->BDCR |= RCC_BDCR_LSEON;

    uint32_t t0       = get_ms();
    uint32_t last_pip = 0;

    while (!(RCC->BDCR & RCC_BDCR_LSERDY))
    {
        /* Short blink while waiting for Y2 crystal to start */
        uint32_t now = get_ms();
        if (now - last_pip >= 600U)
        {
            LED_OFF(); delay_ms(200);
            LED_ON();  delay_ms(200);
            LED_OFF();
            last_pip = get_ms();
        }

        if (get_ms() - t0 > 5000U)
        {
            /* Y2 timed out — fall back to LSI (less accurate, still works) */
            RCC->BDCR &= ~RCC_BDCR_LSEON;
            RCC->CSR  |= RCC_CSR_LSION;
            while (!(RCC->CSR & RCC_CSR_LSIRDY)) {}
            RCC->BDCR  = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_1;
            RCC->BDCR |= RCC_BDCR_RTCEN;
            return;
        }
    }

    RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_0;
    RCC->BDCR |= RCC_BDCR_RTCEN;
}

/* ------------------------------------------------------------------ */
/* GPIO init                                                           */
/* STM32F103 CRL/CRH nibble values:                                   */
/*   0x3 = output push-pull 50 MHz                                    */
/*   0x2 = output push-pull 2 MHz                                     */
/*   0xB = AF push-pull 50 MHz                                        */
/*   0x4 = input floating                                             */
/* ------------------------------------------------------------------ */
static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_IOPCEN;
    /* read-back flush */
    volatile uint32_t tmp = RCC->APB2ENR; (void)tmp;

    /* PC13 — LED, output PP 2 MHz */
    GPIOC->CRH &= ~(0xFu << 20);
    GPIOC->CRH |=  (0x2u << 20);
    LED_OFF();

    /* PA3 — SX1280 RST, output PP 2 MHz */
    GPIOA->CRL &= ~(0xFu << 12);
    GPIOA->CRL |=  (0x2u << 12);
    RST_HIGH();

    /* PA4 — SX1280 NSS, output PP 50 MHz */
    GPIOA->CRL &= ~(0xFu << 16);
    GPIOA->CRL |=  (0x3u << 16);
    NSS_HIGH();

    /* PB15 — SX1280 BUSY, input floating */
    GPIOB->CRH &= ~(0xFu << 28);
    GPIOB->CRH |=  (0x4u << 28);
}

/* ------------------------------------------------------------------ */
/* SPI1 init (PA5=SCK, PA6=MISO, PA7=MOSI)                           */
/* ------------------------------------------------------------------ */
static void spi1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /* PA5 SCK — AF PP 50 MHz */
    GPIOA->CRL &= ~(0xFu << 20);
    GPIOA->CRL |=  (0xBu << 20);

    /* PA6 MISO — input floating */
    GPIOA->CRL &= ~(0xFu << 24);
    GPIOA->CRL |=  (0x4u << 24);

    /* PA7 MOSI — AF PP 50 MHz */
    GPIOA->CRL &= ~(0xFu << 28);
    GPIOA->CRL |=  (0xBu << 28);

    /* SPI1: Master, 8-bit, Mode 0 (CPOL=0 CPHA=0), SW NSS, /8 = 9 MHz */
    SPI1->CR1 = SPI_CR1_MSTR   /* master */
              | SPI_CR1_BR_1   /* BR[2:0]=010 → /8 → 9 MHz */
              | SPI_CR1_SSM    /* software NSS */
              | SPI_CR1_SSI    /* SSI=1 required in master+SSM */
              | SPI_CR1_SPE;   /* enable */
}

/* ------------------------------------------------------------------ */
/* SPI byte transfer                                                   */
/* ------------------------------------------------------------------ */
static uint8_t spi_txrx(uint8_t b)
{
    while (!(SPI1->SR & SPI_SR_TXE)) {}
    *(volatile uint8_t *)&SPI1->DR = b;
    while (!(SPI1->SR & SPI_SR_RXNE)) {}
    return *(volatile uint8_t *)&SPI1->DR;
}

/* ------------------------------------------------------------------ */
/* SX1280 helpers                                                      */
/* ------------------------------------------------------------------ */
static void sx1280_wait_ready(void)
{
    uint32_t t0 = get_ms();
    while (BUSY_READ())
        if (get_ms() - t0 > 1000UL) return;
}

static void sx1280_cmd(uint8_t opcode, const uint8_t *p, uint8_t n)
{
    sx1280_wait_ready();
    NSS_LOW();
    spi_txrx(opcode);
    for (uint8_t i = 0; i < n; i++) spi_txrx(p[i]);
    NSS_HIGH();
}

static void sx1280_write_buffer(uint8_t offset, const uint8_t *data, uint8_t len)
{
    sx1280_wait_ready();
    NSS_LOW();
    spi_txrx(CMD_WRITE_BUFFER);
    spi_txrx(offset);
    for (uint8_t i = 0; i < len; i++) spi_txrx(data[i]);
    NSS_HIGH();
}

static void sx1280_write_register(uint16_t addr, uint8_t value)
{
    sx1280_wait_ready();
    NSS_LOW();
    spi_txrx(CMD_WRITE_REGISTER);
    spi_txrx((addr >> 8) & 0xFF);
    spi_txrx(addr & 0xFF);
    spi_txrx(value);
    NSS_HIGH();
}

static void sx1280_set_freq(uint32_t freq_hz)
{
    uint32_t steps = (uint32_t)((double)freq_hz * 262144.0 / 52000000.0);
    uint8_t p[3] = {(steps >> 16) & 0xFF, (steps >> 8) & 0xFF, steps & 0xFF};
    sx1280_cmd(CMD_SET_RF_FREQUENCY, p, 3);
}

/* ------------------------------------------------------------------ */
/* SX1280 BLE init                                                     */
/* ------------------------------------------------------------------ */
static void sx1280_init_ble(void)
{
    RST_LOW();
    delay_ms(10);
    RST_HIGH();
    delay_ms(20);

    uint8_t p;

    p = 0x00; sx1280_cmd(CMD_SET_STANDBY,     &p, 1); /* standby RC   */
    p = 0x03; sx1280_cmd(CMD_SET_PACKET_TYPE, &p, 1); /* BLE          */

    sx1280_set_freq(FREQ_CH37_HZ);

    uint8_t txp[2] = {0x1F, 0xE0};  /* +13 dBm, 20 µs ramp */
    sx1280_cmd(CMD_SET_TX_PARAMS, txp, 2);

    uint8_t base[2] = {0x00, 0x80}; /* TX base=0, RX base=128 */
    sx1280_cmd(CMD_SET_BUF_BASE_ADDR, base, 2);

    uint8_t mod[3] = {0x04, 0x08, 0x00}; /* 1 Mbps, BT=0.5 */
    sx1280_cmd(CMD_SET_MOD_PARAMS, mod, 3);
}

/* ------------------------------------------------------------------ */
/* Build BLE ADV_NONCONN_IND PDU                                       */
/* ------------------------------------------------------------------ */
static uint8_t build_adv_pdu(uint8_t *buf, const uint8_t mac[6], const char *name)
{
    uint8_t nlen    = (uint8_t)strlen(name);
    uint8_t payload = 6u + 1u + 1u + nlen;

    buf[0] = 0x42;   /* ADV_NONCONN_IND | TxAdd=1 (random) */
    buf[1] = payload;

    for (uint8_t i = 0; i < 6; i++)
        buf[2 + i] = mac[5 - i];   /* MAC LSO first */

    buf[8] = nlen + 1;  /* AD length */
    buf[9] = 0x09;      /* AD type: Complete Local Name */
    memcpy(&buf[10], name, nlen);

    return (uint8_t)(2u + payload);
}

/* ------------------------------------------------------------------ */
/* Send beacon on all 3 BLE advertising channels                      */
/* ------------------------------------------------------------------ */
static void send_ble_beacon(void)
{
    static const uint32_t freq[3]  = {FREQ_CH37_HZ,       FREQ_CH38_HZ,       FREQ_CH39_HZ};
    static const uint8_t  seed[3]  = {BLE_WHITENING_CH37, BLE_WHITENING_CH38, BLE_WHITENING_CH39};

    uint8_t pdu[40];
    uint8_t pdu_len = build_adv_pdu(pdu, BEACON_MAC, BEACON_NAME);

    /* packet params: ADV_NONCONN_IND, 3-byte CRC, whitening on */
    uint8_t pkt[4] = {0x00, 0x01, 0x00, 0x08};

    sx1280_write_buffer(0x00, pdu, pdu_len);
    sx1280_cmd(CMD_SET_PKT_PARAMS, pkt, 4);

    for (uint8_t ch = 0; ch < 3; ch++)
    {
        sx1280_set_freq(freq[ch]);
        sx1280_write_register(REG_BLE_WHITENING_INIT, seed[ch]);

        uint8_t tx[3] = {0x00, 0x00, 0x00};
        sx1280_cmd(CMD_SET_TX, tx, 3);

        sx1280_wait_ready();
        delay_ms(5);
    }

    led_tx_flash();   /* triple flash = beacon transmitted */
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    /*
     * Turn LED OFF immediately using direct register access.
     * PC13 is floating (input) after reset and may appear ON depending
     * on the PCB pull state.  Fix it before clock_init() runs.
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    { volatile uint32_t _ = RCC->APB2ENR; (void)_; }  /* flush */
    GPIOC->CRH  &= ~(0xFu << 20);
    GPIOC->CRH  |=  (0x2u << 20);   /* PC13 output push-pull 2 MHz */
    GPIOC->BSRR  =  (1u   << 13);   /* PC13 HIGH → LED OFF (active-low) */

    clock_init();  /* HSE 8 MHz (Y1) → PLL → 72 MHz, SysTick 1 ms  */
    gpio_init();   /* full GPIO init (re-inits PC13 cleanly)         */

    /* 5 short blinks = booting */
    blink_short(5);

    spi1_init();
    lse_init();
    sx1280_init_ble();

    /* 3 long blinks = ready, entering main loop */
    blink_long(3);

    uint32_t last_tx = 0;

    while (1)
    {
        uint32_t now = get_ms();
        if (now - last_tx >= BEACON_INTERVAL_MS)
        {
            send_ble_beacon();
            last_tx = now;
        }
    }
}

/*
 * BLE Beacon via SX1280 — STM32F103CB
 *
 * Transmits a BLE ADV_NONCONN_IND packet every 5 seconds on all three
 * standard BLE advertising channels (37/38/39).
 * Visible as "BLE-STM32" in any BLE scanner app or tool.
 *
 * !! VERIFY all SX1280 pin assignments against your schematic !!
 */

#include "stm32f1xx_hal.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Pin definitions — adjust to match your PCB schematic               */
/* ------------------------------------------------------------------ */
#define SX1280_NSS_PORT     GPIOA
#define SX1280_NSS_PIN      GPIO_PIN_4   /* SPI chip-select (active low) */
#define SX1280_RST_PORT     GPIOB
#define SX1280_RST_PIN      GPIO_PIN_0   /* Hardware reset               */
#define SX1280_BUSY_PORT    GPIOB
#define SX1280_BUSY_PIN     GPIO_PIN_1   /* Busy / ready indicator       */

#define LED_PORT            GPIOC
#define LED_PIN_N           GPIO_PIN_13  /* Active-LOW on Blue Pill      */

/* ------------------------------------------------------------------ */
/* SX1280 command opcodes                                              */
/* ------------------------------------------------------------------ */
#define CMD_SET_STANDBY         0x80
#define CMD_SET_PACKET_TYPE     0x8A
#define CMD_SET_RF_FREQUENCY    0x86
#define CMD_SET_TX_PARAMS       0x8E
#define CMD_SET_BUF_BASE_ADDR   0x8F
#define CMD_SET_MOD_PARAMS      0x8B
#define CMD_SET_PKT_PARAMS      0x8C
#define CMD_WRITE_BUFFER        0x1A
#define CMD_SET_TX              0x83

/* ------------------------------------------------------------------ */
/* BLE advertising channel frequencies                                 */
/* ------------------------------------------------------------------ */
#define FREQ_CH37_HZ   2402000000UL   /* 2402 MHz */
#define FREQ_CH38_HZ   2426000000UL   /* 2426 MHz */
#define FREQ_CH39_HZ   2480000000UL   /* 2480 MHz */

/* ------------------------------------------------------------------ */
/* Beacon settings                                                     */
/* ------------------------------------------------------------------ */
#define BEACON_NAME          "BLE-STM32"
#define BEACON_INTERVAL_MS   5000U

/* Static random BLE MAC — top 2 bits of byte[0] must be 0b11        */
static const uint8_t BEACON_MAC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};

/* ------------------------------------------------------------------ */

static SPI_HandleTypeDef hspi1;

/* ------------------------------------------------------------------ */
/* SX1280 low-level SPI helpers                                        */
/* ------------------------------------------------------------------ */

static void sx1280_wait_ready(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GPIO_ReadPin(SX1280_BUSY_PORT, SX1280_BUSY_PIN) == GPIO_PIN_SET)
        if (HAL_GetTick() - t0 > 1000U) break;
}

static void sx1280_cmd(uint8_t opcode, const uint8_t *params, uint8_t n)
{
    sx1280_wait_ready();
    HAL_GPIO_WritePin(SX1280_NSS_PORT, SX1280_NSS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &opcode, 1, HAL_MAX_DELAY);
    if (n) HAL_SPI_Transmit(&hspi1, (uint8_t *)params, n, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SX1280_NSS_PORT, SX1280_NSS_PIN, GPIO_PIN_SET);
}

static void sx1280_write_buffer(uint8_t offset, const uint8_t *data, uint8_t len)
{
    uint8_t hdr[2] = {CMD_WRITE_BUFFER, offset};
    sx1280_wait_ready();
    HAL_GPIO_WritePin(SX1280_NSS_PORT, SX1280_NSS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, hdr, 2, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SX1280_NSS_PORT, SX1280_NSS_PIN, GPIO_PIN_SET);
}

/* ------------------------------------------------------------------ */
/* SX1280 initialisation in BLE advertising mode                       */
/* ------------------------------------------------------------------ */

static void sx1280_reset(void)
{
    HAL_GPIO_WritePin(SX1280_RST_PORT, SX1280_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(SX1280_RST_PORT, SX1280_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(20);
}

static void sx1280_set_freq(uint32_t freq_hz)
{
    /* PLL step = 52 MHz / 2^18 ≈ 198.36 Hz */
    uint32_t steps = (uint32_t)((double)freq_hz * 262144.0 / 52000000.0);
    uint8_t p[3] = {(steps >> 16) & 0xFF, (steps >> 8) & 0xFF, steps & 0xFF};
    sx1280_cmd(CMD_SET_RF_FREQUENCY, p, 3);
}

static void sx1280_init_ble(void)
{
    sx1280_reset();

    /* Standby (RC oscillator) */
    uint8_t rc = 0x00;
    sx1280_cmd(CMD_SET_STANDBY, &rc, 1);

    /* BLE packet type */
    uint8_t pt = 0x03;
    sx1280_cmd(CMD_SET_PACKET_TYPE, &pt, 1);

    /* Starting frequency — will be updated per channel in TX */
    sx1280_set_freq(FREQ_CH37_HZ);

    /* TX power = +12 dBm (0x1F), ramp time = 20 µs (0xE0) */
    uint8_t txp[2] = {0x1F, 0xE0};
    sx1280_cmd(CMD_SET_TX_PARAMS, txp, 2);

    /* TX buffer base = 0x00, RX buffer base = 0x80 */
    uint8_t buf[2] = {0x00, 0x80};
    sx1280_cmd(CMD_SET_BUF_BASE_ADDR, buf, 2);

    /*
     * Modulation params for BLE 1 Mbps:
     *   [0] 0x04 = BLE_BR_1_000_BW_1_2  (1 Mbps / 1.2 MHz BW)
     *   [1] 0x08 = BT = 0.5 (Gaussian shaping)
     *   [2] 0x00 reserved
     */
    uint8_t mod[3] = {0x04, 0x08, 0x00};
    sx1280_cmd(CMD_SET_MOD_PARAMS, mod, 3);
}

/* ------------------------------------------------------------------ */
/* Build a BLE ADV_NONCONN_IND PDU with a "Complete Local Name" AD    */
/* Returns total PDU length (header 2 B + payload).                   */
/* ------------------------------------------------------------------ */
static uint8_t build_adv_pdu(uint8_t *buf, const uint8_t mac[6], const char *name)
{
    uint8_t nlen       = (uint8_t)strlen(name);
    uint8_t payload    = 6u + 1u + 1u + nlen;  /* MAC + len-byte + type-byte + name */

    /* Header */
    buf[0] = 0x42;       /* ADV_NONCONN_IND (0x02) | TxAdd=1 random (bit 6) */
    buf[1] = payload;

    /* AdvA: 6-byte MAC, LSO first (BLE over-the-air ordering) */
    for (uint8_t i = 0; i < 6; i++) buf[2 + i] = mac[5 - i];

    /* AD record: Complete Local Name (type 0x09) */
    buf[8] = nlen + 1;   /* length = type(1) + name bytes */
    buf[9] = 0x09;
    memcpy(&buf[10], name, nlen);

    return (uint8_t)(2u + payload);
}

/* ------------------------------------------------------------------ */
/* Transmit one beacon event on all three BLE advertising channels    */
/* ------------------------------------------------------------------ */
static void send_ble_beacon(void)
{
    static const uint32_t channels[3] = {FREQ_CH37_HZ, FREQ_CH38_HZ, FREQ_CH39_HZ};

    uint8_t pdu[40];
    uint8_t pdu_len = build_adv_pdu(pdu, BEACON_MAC, BEACON_NAME);

    /*
     * Packet params for BLE advertising:
     *   [0] 0x00 = non-connectable / non-scannable
     *   [1] 0x01 = 3-byte CRC (BLE standard)
     *   [2] 0x00 = PRBS9 test payload (ignored in normal adv mode)
     *   [3] 0x08 = whitening enabled
     *       SX1280 auto-seeds LFSR correctly for the three adv channel
     *       frequencies (2402 / 2426 / 2480 MHz).
     */
    uint8_t pkt_params[4] = {0x00, 0x01, 0x00, 0x08};

    sx1280_write_buffer(0x00, pdu, pdu_len);
    sx1280_cmd(CMD_SET_PKT_PARAMS, pkt_params, 4);

    for (uint8_t ch = 0; ch < 3; ch++)
    {
        sx1280_set_freq(channels[ch]);

        /* Single-packet TX (timeout = 0 → one packet then standby) */
        uint8_t tx_args[3] = {0x00, 0x00, 0x00};
        sx1280_cmd(CMD_SET_TX, tx_args, 3);

        sx1280_wait_ready();
        HAL_Delay(5);   /* short gap between channel hops */
    }

    /* Blink LED to confirm each beacon burst */
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN_N);
}

/* ------------------------------------------------------------------ */
/* Peripheral initialisation                                           */
/* ------------------------------------------------------------------ */

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL9;   /* 8 MHz × 9 = 72 MHz */
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

static void GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC13 — LED (active low) */
    g.Pin   = LED_PIN_N;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &g);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN_N, GPIO_PIN_SET);   /* off */

    /* PA4 — SX1280 NSS */
    g.Pin   = SX1280_NSS_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SX1280_NSS_PORT, &g);
    HAL_GPIO_WritePin(SX1280_NSS_PORT, SX1280_NSS_PIN, GPIO_PIN_SET);

    /* PB0 — SX1280 RESET */
    g.Pin   = SX1280_RST_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SX1280_RST_PORT, &g);

    /* PB1 — SX1280 BUSY (input, no pull — driven by chip) */
    g.Pin   = SX1280_BUSY_PIN;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(SX1280_BUSY_PORT, &g);
}

static void SPI1_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA5=SCK, PA7=MOSI — alternate function push-pull */
    g.Pin   = GPIO_PIN_5 | GPIO_PIN_7;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA6=MISO — floating input */
    g.Pin  = GPIO_PIN_6;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;   /* CPOL=0 */
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;    /* CPHA=0  → Mode 0 */
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;  /* 72/8 = 9 MHz */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&hspi1);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    SPI1_Init();
    sx1280_init_ble();

    uint32_t last_tx = 0;

    while (1)
    {
        uint32_t now = HAL_GetTick();
        if (now - last_tx >= BEACON_INTERVAL_MS)
        {
            send_ble_beacon();
            last_tx = now;
        }
    }
}

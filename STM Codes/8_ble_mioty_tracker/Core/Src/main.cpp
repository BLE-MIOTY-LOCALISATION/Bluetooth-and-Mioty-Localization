/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    : main.cpp
 * @brief   : Dual-Protocol BLE + Mioty Asset Tracker — STM32F103CB + SX1280
 *
 * --- Architecture ---
 * This firmware integrates:
 *   1. Google FMDN BLE advertising (channels 37/38/39) every 1 second.
 *   2. Mioty (TS-UNB) telegram-split uplink packet every MIOTY_WAKEUP_COUNT
 *      BLE cycles (~10 seconds of sleep at the current setting, plus
 *      Mioty's own ~5.5s blocking TX time on top of that).
 *
 * The STM32 spends >99% of the time in Stop Mode (Low-Power Regulator ON).
 * It wakes up via an RTC alarm every 1 second during BLE cycles (5 seconds
 * after a Mioty cycle), sends the BLE beacon, checks whether it is time for
 * a Mioty uplink (every MIOTY_WAKEUP_COUNT-th wakeup), and re-enters Stop
 * Mode.
 *
 * --- Clock ---
 * CPU: 8 MHz HSE crystal, PLL OFF. Required by Mioty DWT symbol timing.
 * SPI: 4 MHz (BAUDRATEPRESCALER_2 on 8 MHz PCLK2).
 *
 * --- Wakeup Timing ---
 * BLE interval:   1 second (every RTC alarm during BLE cycles)
 * Mioty interval: MIOTY_WAKEUP_COUNT x 1s BLE sleep + 5s post-Mioty sleep
 *   (currently MIOTY_WAKEUP_COUNT=5 -> ~10s of sleep between Mioty sends,
 *   plus Mioty's own ~5.5s blocking TX time; adjust MIOTY_WAKEUP_COUNT
 *   below to retune)
 *
 * --- LED Status Indicators ---
 * Boot:       3 rapid blinks (100ms) = firmware alive, SX1280 reset done
 *             2s pause
 *             1 long blink (500ms)   = Mioty + BLE init complete
 * Each cycle: 1 short blink (50ms)   = BLE TX done
 *             3 short blinks (80ms)  = Mioty TX firing (on Mioty cycles only)
 *
 * --- Mioty Credentials ---
 * Device EUI64:    70:B3:D5:67:70:FF:01:70
 * Network Key:     53ADF802B197F2738D5DDDA577E1FA9C
 * PHY Profile:     Lambda80 (EU1 2.4 GHz)
 *
 * --- FMDN EID ---
 * See FMDN_tracker.md for instructions on generating a fresh EID.
 ******************************************************************************
 */
/* USER CODE END Header */

/* ============================================================
 *  Includes
 * ============================================================ */
#include "main.h"
#include "STM32TsUnbTemplates.h"   // Pulls in all Mioty C++ library headers

/* ============================================================
 *  Timing Configuration
 *  Edit MIOTY_WAKEUP_COUNT to change Mioty transmission interval.
 *  Interval = MIOTY_WAKEUP_COUNT x 10 seconds.
 * ============================================================ */
#define MIOTY_WAKEUP_COUNT  5      // Send Mioty packet every 5 BLE cycles: 5 x 1s BLE sleep + 5s
                                    // post-Mioty sleep = 10s between Mioty sends (plus Mioty's
                                    // own ~5.5s blocking TX time on top of that)
#define TRANSMIT_PWR        0      // Mioty SX1280 TX power in dBm, passed directly to TsUnb_Node.Tx.setTxPower()
#define BLE_TX_POWER_BYTE   0x12   // BLE SX1280 TX power register byte: 0x1F = +13 dBm, 0x12 = 0 dBm

/* ============================================================
 *  Mioty Network Credentials
 * ============================================================ */
#define MAC_NETWORK_KEY  0x53, 0xad, 0xf8, 0x02, 0xb1, 0x97, 0xf2, 0x73, \
                         0x8d, 0x5d, 0xdd, 0xa5, 0x77, 0xe1, 0xfa, 0x9c
#define MAC_EUI64        0x70, 0xb3, 0xd5, 0x67, 0x70, 0xff, 0x01, 0x70

/* ============================================================
 *  FMDN Ephemeral Identifier (EID) — 20 bytes
 *  Replace with your own EID from GoogleFindMyTools/main.py
 * ============================================================ */
static const uint8_t fmdn_eid[20] = {
    0x7D, 0x35, 0x5E, 0xC1, 0x95, 0x1F, 0xB3, 0xC1, 0xCB, 0x37,
    0x9F, 0x3B, 0xED, 0x0E, 0x8D, 0x13, 0x85, 0x51, 0x4B, 0xD7
};

/* ============================================================
 *  Private Variables
 * ============================================================ */
SPI_HandleTypeDef hspi1;
volatile uint8_t  debug_stage    = 0;  // Track startup phase in Live Expressions
uint32_t          mioty_counter  = 0;  // Counts 10s wakeup cycles
uint32_t          packet_counter = 0;  // Mioty uplink sequence number

/* ============================================================
 *  Mioty C++ node (EU1 Lambda80 profile, 2.4 GHz)
 * ============================================================ */
using namespace TsUnbLib::STM32;
TsUnb_EU1_Lambda80_t TsUnb_Node;

/* ============================================================
 *  Private Function Prototypes
 * ============================================================ */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);

void LED_Blink(uint8_t count, uint16_t on_ms, uint16_t off_ms);
void LED_Boot_Sequence(void);

// SX1280 low-level SPI helpers
void    SX1280_WaitBusy(void);
void    SX1280_SendCommand(uint8_t *cmd, uint8_t len);
void    SX1280_WriteBuffer(uint8_t offset, uint8_t *data, uint8_t len);
void    SX1280_WriteRegister(uint16_t addr, uint8_t *data, uint8_t len);
uint8_t SX1280_ReadRegister(uint16_t addr);
uint8_t SX1280_GetStatus(void);

// BLE functions
void    SX1280_InitBLE(void);
void    SX1280_SendFMDNBeacon(void);
uint8_t SX1280_SendOnChannel(uint8_t f0, uint8_t f1, uint8_t f2,
                              uint8_t ch_index, uint8_t white_seed);

// Mioty functions
void    prepareForMioty(void);
void    sendMiotyPacket(void);

// Power management
void    SX1280_Sleep(void);
void    SX1280_Wakeup(void);
void    RTC_Init_LowPower(void);
void    STM32_EnterStopMode(uint32_t seconds);

/* ============================================================
 *  LED Helpers
 * ============================================================ */
// General purpose blinker: 'count' blinks at specified on/off timing
void LED_Blink(uint8_t count, uint16_t on_ms, uint16_t off_ms)
{
    for (uint8_t i = 0; i < count; i++) {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        HAL_Delay(on_ms);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
        HAL_Delay(off_ms);
    }
}

// Power-on LED sequence confirming firmware is alive and initialized
void LED_Boot_Sequence(void)
{
    LED_Blink(3, 100, 100);  // 3 fast blinks: MCU alive, radio reset done
    HAL_Delay(2000);          // 2s pause (radio booting)
    LED_Blink(1, 500, 200);  // 1 long blink: Mioty + BLE init done
}

/* ============================================================
 *  SX1280 Low-Level SPI Primitives
 * ============================================================ */
void SX1280_WaitBusy(void)
{
    uint32_t timeout = HAL_GetTick();
    while (HAL_GPIO_ReadPin(LORA_BUSY_GPIO_Port, LORA_BUSY_Pin) == GPIO_PIN_SET) {
        if ((HAL_GetTick() - timeout) > 1000) {
            // BUSY stuck: rapid blink forever
            while (1) {
                HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
                HAL_Delay(50);
            }
        }
    }
}

uint8_t SX1280_GetStatus(void)
{
    uint8_t tx[2] = {0xC0, 0x00};
    uint8_t rx[2] = {0x00, 0x00};
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
    return rx[1];
}

void SX1280_SendCommand(uint8_t *cmd, uint8_t len)
{
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, cmd, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

void SX1280_WriteBuffer(uint8_t offset, uint8_t *data, uint8_t len)
{
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    uint8_t header[2] = {0x1A, offset};
    HAL_SPI_Transmit(&hspi1, header, 2, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

void SX1280_WriteRegister(uint16_t addr, uint8_t *data, uint8_t len)
{
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    uint8_t header[3] = {0x18, (uint8_t)((addr >> 8) & 0xFF), (uint8_t)(addr & 0xFF)};
    HAL_SPI_Transmit(&hspi1, header, 3, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
}

uint8_t SX1280_ReadRegister(uint16_t addr)
{
    uint8_t val = 0;
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    uint8_t header[4] = {0x19, (uint8_t)((addr >> 8) & 0xFF), (uint8_t)(addr & 0xFF), 0x00};
    HAL_SPI_Transmit(&hspi1, header, 4, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi1, &val, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
    return val;
}

/* ============================================================
 *  BLE — SX1280 Radio Configuration and Transmission
 * ============================================================ */

// Configure SX1280 for BLE advertising mode.
// Must be called once after each wakeup before sending a BLE packet.
void SX1280_InitBLE(void)
{
    // 1. Standby RC mode
    uint8_t cmd_standby[2] = {0x80, 0x00};
    SX1280_SendCommand(cmd_standby, 2);
    HAL_Delay(10);

    // 2. Packet type = BLE (0x04)
    uint8_t cmd_pkt_type[2] = {0x8A, 0x04};
    SX1280_SendCommand(cmd_pkt_type, 2);

    // 3. TX base address = 0x80, RX base = 0x00
    uint8_t cmd_buf[3] = {0x8F, 0x80, 0x00};
    SX1280_SendCommand(cmd_buf, 3);

    // 4. Modulation: 1 Mbps GFSK, MOD_IND=0.5, BT=0.5
    uint8_t cmd_mod[4] = {0x8B, 0x45, 0x01, 0x20};
    SX1280_SendCommand(cmd_mod, 4);

    // 5. TX power = BLE_TX_POWER_BYTE (+13 dBm), ramp 20us
    uint8_t cmd_tx_params[3] = {0x8E, BLE_TX_POWER_BYTE, 0x20};
    SX1280_SendCommand(cmd_tx_params, 3);

    // 6. BLE advertising Access Address = 0x8E89BED6
    uint8_t access_addr[4] = {0x8E, 0x89, 0xBE, 0xD6};
    SX1280_WriteRegister(0x09CF, access_addr, 4);

    // 7. CRC init seed = 0x555555 (BLE advertising spec)
    uint8_t crc_seed[3] = {0x55, 0x55, 0x55};
    SX1280_WriteRegister(0x09C7, crc_seed, 3);

    // 8. Map TxDone IRQ to DIO1 pin
    uint8_t cmd_irq[9] = {0x8D, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    SX1280_SendCommand(cmd_irq, 9);
}

// Send FMDN ADV_NONCONN_IND on one BLE advertising channel.
// Returns 1 if TxDone confirmed via DIO1, 0 if not.
uint8_t SX1280_SendOnChannel(uint8_t freq0, uint8_t freq1, uint8_t freq2,
                              uint8_t ch_index, uint8_t white_seed)
{
    (void)ch_index;

    // Set RF frequency
    uint8_t cmd_freq[4] = {0x86, freq0, freq1, freq2};
    SX1280_SendCommand(cmd_freq, 4);

    // Repeat modulation parameters per-channel (required for correct on-air BLE)
    uint8_t cmd_mod[4] = {0x8B, 0x45, 0x01, 0x20};
    SX1280_SendCommand(cmd_mod, 4);

    // Build FMDN ADV_NONCONN_IND PDU
    uint8_t adv_data[29] = {
        // BLE Flags AD (3 bytes)
        0x02, 0x01, 0x06,
        // FMDN Service Data AD (26 bytes)
        0x19, 0x16,
        // FMDN Service UUID 0xFEAA (little-endian)
        0xAA, 0xFE,
        // FMDN frame type: 0x41 = unwanted tracking protection
        0x41,
        // 20-byte EID (filled below)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Hashed flags
        0x00
    };
    for (int i = 0; i < 20; i++) {
        adv_data[8 + i] = fmdn_eid[i];
    }

    // BLE random static address (on-air MSB-first: FF:EE:DD:CC:BB:AC)
    uint8_t mac[6] = {0xAC, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // Assemble full PDU: [header(2) | MAC(6) | AD data(29)] = 37 bytes
    uint8_t payload[37];
    payload[0] = 0x42;       // ADV_NONCONN_IND + TxAdd=1 (random address)
    payload[1] = 6 + 29;     // PDU length
    for (int i = 0; i < 6; i++)  payload[2 + i] = mac[i];
    for (int i = 0; i < 29; i++) payload[8 + i] = adv_data[i];

    // Set packet params (ConnectionState=0x20, CRC=3B, TestPayload=0x04)
    uint8_t cmd_pkt[8] = {0x8C, 0x20, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00};
    SX1280_SendCommand(cmd_pkt, 8);

    // Set whitening seed per BLE channel
    SX1280_WriteRegister(0x09C5, &white_seed, 1);

    // Write 37-byte PDU to TX buffer at offset 0x80
    SX1280_WriteBuffer(0x80, payload, 37);

    // SetTx: trigger single transmission
    uint8_t cmd_tx[4] = {0x83, 0x00, 0x00, 0x64};
    SX1280_SendCommand(cmd_tx, 4);
    SX1280_WaitBusy();
    HAL_Delay(2);

    // Read TxDone from DIO1, then clear IRQ
    uint8_t txdone = 0;
    if (HAL_GPIO_ReadPin(LORA_DIO1_GPIO_Port, LORA_DIO1_Pin) == GPIO_PIN_SET) {
        txdone = 1;
        uint8_t clr[3] = {0x97, 0xFF, 0xFF};
        SX1280_SendCommand(clr, 3);
    }
    HAL_Delay(3);
    return txdone;
}

// Transmit one FMDN BLE beacon across all 3 advertising channels.
// BLE whitening seeds: CH37=0x53, CH38=0x33, CH39=0x73
void SX1280_SendFMDNBeacon(void)
{
    SX1280_SendOnChannel(0xB8, 0xC4, 0xEC, 37, 0x53); // 2402 MHz
    HAL_Delay(10);
    SX1280_SendOnChannel(0xBA, 0x9D, 0x89, 38, 0x33); // 2426 MHz
    HAL_Delay(10);
    SX1280_SendOnChannel(0xBE, 0xC4, 0x55, 39, 0x73); // 2480 MHz
    HAL_Delay(10);
}

/* ============================================================
 *  Mioty — Node Initialization and Packet Transmission
 * ============================================================ */

// Initialize the Mioty C++ library node.
// Must be called once at boot. Configures network credentials and TX power.
// The radio must have been hard-reset before calling this.
void prepareForMioty(void)
{
    if (TsUnb_Node.init() < 0) {
        while (1) {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            HAL_Delay(150);
        }
    }
    TsUnb_Node.Tx.setTxPower(TRANSMIT_PWR);
    TsUnb_Node.Mac.setNetworkKey(MAC_NETWORK_KEY);
    TsUnb_Node.Mac.setAddress(MAC_EUI64);
    TsUnb_Node.Mac.extPkgCnt = 0x01;  // Start uplink sequence counter at 1
}

// Build and send a 20-byte Mioty uplink payload.
// Payload format: [4-byte big-endian packet counter | 16 zero bytes]
// Note: TsUnb_Node.send() is blocking — takes ~5.5 seconds to return.
void sendMiotyPacket(void)
{
    // 3 short blinks = Mioty TX about to fire
    LED_Blink(3, 80, 80);

    uint8_t payload[20] = {0};
    payload[0] = (packet_counter >> 24) & 0xFF;
    payload[1] = (packet_counter >> 16) & 0xFF;
    payload[2] = (packet_counter >>  8) & 0xFF;
    payload[3] =  packet_counter        & 0xFF;

    // Blocking call: executes 24 telegram-split bursts over ~5.5 seconds
    TsUnb_Node.send(payload, sizeof(payload));
    packet_counter++;
}

/* ============================================================
 *  Power Management — Sleep and Wakeup
 * ============================================================ */

void SX1280_Sleep(void)
{
    // Sleep with register retention (0x01 = retain config)
    uint8_t cmd[2] = {0x84, 0x01};
    SX1280_SendCommand(cmd, 2);
}

void SX1280_Wakeup(void)
{
    uint8_t tx[2] = {0xC0, 0x00};
    uint8_t rx[2] = {0x00, 0x00};

    // Pull NSS low without WaitBusy (chip is asleep)
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    // Hold 37.5 µs at 8 MHz (100 NOPs) for internal oscillator startup
    for (volatile uint32_t i = 0; i < 100; i++) { __NOP(); }
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
    SX1280_WaitBusy();
}

// Enter Stop Mode and restore everything on wakeup.
// The MCU sleeps here until the RTC alarm fires.
void STM32_EnterStopMode(uint32_t seconds)
{
    // 1. Put SX1280 into deep sleep
    SX1280_Sleep();

    // 2. Brief post-TX LED flash (confirms TX done)
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

    // 3. Disable SPI1 peripheral and clamp SPI bus pins to pull-down
    //    (prevents current leakage through the SPI bus during sleep)
    __HAL_SPI_DISABLE(&hspi1);
    GPIO_InitTypeDef g = {0};
    g.Pin  = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7; // SCK, MISO, MOSI
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &g);

    // 4. BUSY and DIO1 to Analog — CRITICAL: SX1280 drives BUSY HIGH in sleep.
    //    A pull-down would create ~82uA drain. Analog mode avoids this.
    g.Pin  = LORA_DIO1_Pin | LORA_BUSY_Pin;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    // 5. PB10 (SCL2) and PB11 (SDA2) — I2C2 pins (accelerometer/IIS2MDCT)
    //    Clamp to Analog to prevent leakage through I2C pull-ups during sleep.
    g.Pin  = GPIO_PIN_10 | GPIO_PIN_11;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    // 6. All unused Port A pins to Analog (leave PA3/PA4=NSS/NRST, PA5-7=SPI, PA13/PA14=SWD)
    //    PA0, PA1, PA2, PA8, PA9, PA10, PA11, PA12, PA15
    g.Pin  = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_8 |
             GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_15;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    // 7. All unused Port B pins to Analog
    //    (PB0=DIO1, PB15=BUSY already done; PB10/PB11=I2C done above)
    //    PB1, PB2, PB3, PB4, PB5, PB6, PB7, PB8, PB9, PB12, PB13, PB14
    g.Pin  = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 |
             GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 |
             GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    // 8. Unused Port C pins to Analog — PC0..PC12 (leave PC13=LED output,
    //    PC14/PC15=LSE crystal oscillator pins — do NOT touch)
    g.Pin  = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
             GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 |
             GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &g);

    // 9. Keep NSS and NRST high to lock radio state lines
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port,  LORA_NSS_Pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);

    // 10. Set the RTC alarm dynamically based on input parameter
    WRITE_REG(RTC->CRL, (uint32_t)~RTC_CRL_RSF);
    while ((RTC->CRL & RTC_CRL_RSF) == 0) {}
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {}

    uint32_t cnt = ((uint32_t)RTC->CNTH << 16) | RTC->CNTL;
    uint32_t alarm_val = cnt + seconds;

    SET_BIT(RTC->CRL, RTC_CRL_CNF);
    WRITE_REG(RTC->ALRH, (alarm_val >> 16) & 0xFFFF);
    WRITE_REG(RTC->ALRL, alarm_val & 0xFFFF);
    SET_BIT(RTC->CRH, RTC_CRH_ALRIE);
    CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {}

    // 11. Suspend SysTick (prevent 1ms tick from waking CPU)
    HAL_SuspendTick();

    // 12. Enter Stop Mode (SRAM retained, CPU clock gated)
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    // ============================================================
    // *** CPU IS ASLEEP HERE — RTC fires in 10 seconds ***
    // ============================================================

    // 10. Woke up on HSI — restore HSE 8 MHz crystal clock
    SystemClock_Config();

    // 11. Give HSE 5ms to stabilize (important for Mioty DWT timing)
    HAL_ResumeTick();

    // 12. Restore SPI1 peripheral and alternate functions
    HAL_SPI_DeInit(&hspi1);
    MX_SPI1_Init();

    // 13. Restore DIO1 and BUSY as inputs
    g.Pin  = LORA_DIO1_Pin | LORA_BUSY_Pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    // 14. Wake the radio and wait for it to be ready
    SX1280_Wakeup();
    HAL_Delay(5); // 5ms HSE/radio stabilisation margin
}

/* ============================================================
 *  RTC Low-Power Timer (LSE 32.768 kHz)
 * ============================================================ */
void RTC_Init_LowPower(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    clk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) Error_Handler();

    __HAL_RCC_RTC_ENABLE();

    WRITE_REG(RTC->CRL, (uint32_t)~RTC_CRL_RSF);
    while ((RTC->CRL & RTC_CRL_RSF) == 0) {}
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {}

    // Prescaler for 32.768 kHz LSE: 32768 - 1 = 32767 → 1 Hz tick
    SET_BIT(RTC->CRL, RTC_CRL_CNF);
    WRITE_REG(RTC->PRLH, (32767 >> 16) & 0xFFFF);
    WRITE_REG(RTC->PRLL, 32767 & 0xFFFF);
    CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {}

    // EXTI Line 17 (RTC Alarm) — rising edge interrupt
    SET_BIT(EXTI->IMR,  EXTI_IMR_MR17);
    SET_BIT(EXTI->RTSR, EXTI_RTSR_TR17);
    HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);
}

// Set RTC alarm dynamically (replaced by parameterised logic inside STM32_EnterStopMode)

// RTC Alarm ISR — clears flags and sets software flag
extern "C" void RTC_Alarm_IRQHandler(void)
{
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {}
    SET_BIT(RTC->CRL, RTC_CRL_CNF);
    CLEAR_BIT(RTC->CRH, RTC_CRH_ALRIE);
    CLEAR_BIT(RTC->CRL, RTC_CRL_ALRF);   // Clear alarm flag (RTC_CRL_ALRF = bit 1)
    CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0) {}

    WRITE_REG(EXTI->PR, EXTI_PR_PR17);  // Clear EXTI Line 17 pending flag
}

/* ============================================================
 *  Peripheral Initialisation
 * ============================================================ */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Use 8 MHz HSE crystal directly — PLL OFF (required for Mioty timing)
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSE;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) Error_Handler();
}

static void MX_SPI1_Init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; // 8 MHz / 2 = 4 MHz
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Pre-set output levels before configuring as outputs (no glitch on startup)
    HAL_GPIO_WritePin(LED_GPIO_Port,       LED_Pin,       GPIO_PIN_RESET); // LED off
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port,  LORA_NSS_Pin,  GPIO_PIN_SET);   // NSS deselected
    HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);   // NRST released

    // LED (PC13) — output push-pull
    g.Pin   = LED_Pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_Port, &g);

    // NSS (PA4) and NRST (PA3) — output push-pull
    g.Pin   = LORA_NSS_Pin | LORA_NRST_Pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);

    // DIO1 (PB0) and BUSY (PB15) — inputs, no pull
    g.Pin  = LORA_DIO1_Pin | LORA_BUSY_Pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);
}

/* ============================================================
 *  Application Entry Point
 * ============================================================ */
int main(void)
{
    debug_stage = 1;
    // ST-Link reconnect window: prevents the MCU racing ahead before
    // the debugger has time to attach after a flash/reset
    for (volatile uint32_t i = 0; i < 5000000; i++);

    HAL_Init();
    debug_stage = 2;
    SystemClock_Config();
    debug_stage = 3;
    MX_GPIO_Init();
    debug_stage = 4;
    MX_SPI1_Init();
    debug_stage = 5;

    __HAL_RCC_CLEAR_RESET_FLAGS();

    // Deselect radio chip select first (critical to keep SPI state machine clean during boot)
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);

    // Hard reset SX1280: drive NRST low 5ms then release, wait 10ms for boot
    HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(LORA_NRST_GPIO_Port, LORA_NRST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
    debug_stage = 6;
    // Initialize Mioty node (internally calls TsUnb_Node.init())
    prepareForMioty();
    debug_stage = 7;

    // Wake the radio up in case the Mioty library init put the radio to sleep
    SX1280_Wakeup();
    HAL_Delay(5);

    // Initialize BLE radio configuration registers
    SX1280_InitBLE();
    debug_stage = 8;
    // Boot LED sequence: confirms MCU + radio initialization complete
    LED_Boot_Sequence();

    // Initialize RTC for 10-second wakeup intervals
    RTC_Init_LowPower();
    debug_stage = 9;
    /* ============================================================
     *  Main Loop
     *
     *  Every 10 seconds:
     *    - Wake up from Stop Mode via RTC alarm
     *    - Reinitialize BLE, send FMDN beacon (takes ~15ms)
     *    - Blink LED 1x to confirm BLE TX
     *    - If mioty_counter reached MIOTY_WAKEUP_COUNT:
     *        - Switch radio to Mioty, send Mioty packet (~5.5s)
     *        - Reinitialize BLE for next cycle
     *        - Reset mioty_counter
     *    - Enter Stop Mode (sleep until next RTC alarm)
     * ============================================================ */
    while (1)
    {
        if (mioty_counter < MIOTY_WAKEUP_COUNT)
        {
            /* --- BLE cycle (fires 5 times) --- */
            debug_stage = 20;
            SX1280_InitBLE();
            debug_stage = 21;
            SX1280_SendFMDNBeacon();
            debug_stage = 22;
            LED_Blink(1, 50, 0); // Exactly 1 short blink per BLE cycle

            mioty_counter++;

            /* --- Sleep for 1 second during BLE cycles --- */
            debug_stage = 40;
            STM32_EnterStopMode(1);
        }
        else
        {
            /* --- Mioty cycle (fires after 5 BLE cycles) --- */
            HAL_Delay(10);

            debug_stage = 30;
            sendMiotyPacket(); // Blinks 3 times internally
            debug_stage = 31;

            SX1280_Wakeup();
            HAL_Delay(5);
            uint8_t clr[3] = {0x97, 0xFF, 0xFF};
            SX1280_SendCommand(clr, 3);
            HAL_Delay(10);

            SX1280_InitBLE();
            debug_stage = 32;

            mioty_counter = 0; // Reset counter

            /* --- Sleep for 5 seconds after Mioty cycle --- */
            debug_stage = 40;
            STM32_EnterStopMode(5);
        }
    }
}

/* ============================================================
 *  Error Handler
 * ============================================================ */
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
void assert_failed(uint8_t *file, uint32_t line) { (void)file; (void)line; }
#endif

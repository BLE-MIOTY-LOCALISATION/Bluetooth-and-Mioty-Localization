#include "../Inc/sx1280_hal.h"
#include "main.h"

/* Forward declarations – reuse the low‑level helpers from main.c */
extern void SX1280_WaitBusy(void);
extern void SX1280_SendCommand(uint8_t *cmd, uint8_t len);
extern void SX1280_WriteRegister(uint16_t addr, uint8_t *data, uint8_t len);
extern uint8_t SX1280_ReadRegister(uint16_t addr);
extern void SX1280_WriteBuffer(uint8_t offset, uint8_t *data, uint8_t len);
extern uint8_t SX1280_GetStatus(void);

void SX1280_HAL_WaitBusy(void) {
    SX1280_WaitBusy();
}

void SX1280_HAL_SendCommand(const uint8_t *cmd, uint8_t len) {
    // The low‑level SendCommand expects a mutable pointer; cast safely.
    SX1280_SendCommand((uint8_t *)cmd, len);
}

void SX1280_HAL_WriteRegister(uint16_t addr, const uint8_t *data, uint8_t len) {
    SX1280_WriteRegister(addr, (uint8_t *)data, len);
}

uint8_t SX1280_HAL_ReadRegister(uint16_t addr) {
    return SX1280_ReadRegister(addr);
}

void SX1280_HAL_WriteBuffer(uint8_t offset, const uint8_t *data, uint8_t len) {
    SX1280_WriteBuffer(offset, (uint8_t *)data, len);
}

uint8_t SX1280_HAL_GetStatus(void) {
    return SX1280_GetStatus();
}

/* High‑level BLE configuration ------------------------------------------------- */
void SX1280_HAL_Init(void) {
    // Standby RC
    uint8_t cmd_standby[2] = {0x80, 0x00};
    SX1280_HAL_SendCommand(cmd_standby, 2);
    HAL_Delay(10);
}

void SX1280_HAL_InitBLE(void) {
    SX1280_HAL_Init();
    // 1. Set packet type = BLE (0x05)
    uint8_t pkt_type[2] = {0x8A, 0x05};
    SX1280_HAL_SendCommand(pkt_type, 2);
    // 2. TX/RX buffer base = 0x00
    uint8_t buf_base[3] = {0x8F, 0x00, 0x00};
    SX1280_HAL_SendCommand(buf_base, 3);
    // 3. Modulation params: 1 Mbps, MOD_IND=0.5, BT=0.5
    uint8_t mod[4] = {0x8B, 0x45, 0x01, 0x20};
    SX1280_HAL_SendCommand(mod, 4);
    // 4. TX output power +13 dBm, ramp 20 µs
    uint8_t tx_params[3] = {0x8E, 0x1F, 0x20};
    SX1280_HAL_SendCommand(tx_params, 3);
    // 5. BLE advertising Access Address = 0x8E89BED6
    uint8_t aa[4] = {0x8E, 0x89, 0xBE, 0xD6};
    SX1280_HAL_WriteRegister(0x09CF, aa, 4);
    // 6. CRC init seed = 0x555555
    uint8_t crc[3] = {0x55, 0x55, 0x55};
    SX1280_HAL_WriteRegister(0x09C7, crc, 3);
    // 7. Map TxDone IRQ to DIO1 (same as original implementation)
    uint8_t irq_map[9] = {0x8D, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    SX1280_HAL_SendCommand(irq_map, 9);
}

void SX1280_HAL_SetFrequency(uint8_t b0, uint8_t b1, uint8_t b2) {
    uint8_t cmd[4] = {0x86, b0, b1, b2};
    SX1280_HAL_SendCommand(cmd, 4);
}

void SX1280_HAL_SetTxPower(uint8_t power, uint8_t ramp) {
    uint8_t cmd[3] = {0x8E, power, ramp};
    SX1280_HAL_SendCommand(cmd, 3);
}

void SX1280_HAL_SetAccessAddress(uint32_t aa) {
    uint8_t buf[4] = {(uint8_t)(aa >> 24), (uint8_t)(aa >> 16), (uint8_t)(aa >> 8), (uint8_t)aa};
    SX1280_HAL_WriteRegister(0x09CF, buf, 4);
}

void SX1280_HAL_SetCrcSeed(uint32_t seed) {
    uint8_t buf[3] = {(uint8_t)(seed >> 16), (uint8_t)(seed >> 8), (uint8_t)seed};
    SX1280_HAL_WriteRegister(0x09C7, buf, 3);
}

void SX1280_HAL_MapTxDoneIRQ(void) {
    uint8_t irq_map[9] = {0x8D, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    SX1280_HAL_SendCommand(irq_map, 9);
}

#include "mioty_project6.h"

extern "C" {
#include "main.h"
}

#include "mioty/TsUnb/FixedMac.h"
#include "mioty/TsUnb/RadioBurst.h"
#include "mioty/TsUnb/Phy.h"
#include "mioty/TsUnb/SimpleNode.h"
#include "mioty/Trx/SX1280Project6.h"

extern SPI_HandleTypeDef hspi1;
extern "C" void SX1280_WaitBusy(void);

namespace {

static const uint8_t kMiotyNetworkKey[16] = {
    0x53, 0xAD, 0xF8, 0x02, 0xB1, 0x97, 0xF2, 0x73,
    0x8D, 0x5D, 0xDD, 0xA5, 0x77, 0xE1, 0xFA, 0x9C};

static const uint8_t kMiotyEui64[8] = {0x70, 0xB3, 0xD5, 0x67,
                                       0x70, 0xFF, 0x01, 0x70};

static const uint8_t kMiotyTestPayload[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

class Project6TsUnbCpu {
public:
  void spiInit(void) {
    __HAL_SPI_ENABLE(&hspi1);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  }

  void spiDeinit(void) {
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  }

  void spiSend(uint8_t *data, uint16_t len) {
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  }

  void spiSendReceive(uint8_t *data, uint16_t len) {
    SX1280_WaitBusy();
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi1, data, data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
  }

  void initTimer(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    symbolCyclesQ16 =
        ((uint64_t)SystemCoreClock * 1048576ULL * 65536ULL) / 2496000000ULL;
    pendingDelayQ16 = 0;
    nextEventQ16 = 0;
    timerRunning = false;
  }

  void addTimerDelay(int16_t symbols) {
    const int64_t delay = (int64_t)symbols * (int64_t)symbolCyclesQ16;
    if (timerRunning) {
      nextEventQ16 = (uint64_t)((int64_t)nextEventQ16 + delay);
    } else {
      pendingDelayQ16 = (uint64_t)((int64_t)pendingDelayQ16 + delay);
    }
  }

  void startTimer(void) {
    nextEventQ16 = ((uint64_t)DWT->CYCCNT << 16) + pendingDelayQ16;
    pendingDelayQ16 = 0;
    timerRunning = true;
  }

  void waitTimer(void) {
    const uint32_t targetCycles = (uint32_t)(nextEventQ16 >> 16);
    while ((int32_t)(DWT->CYCCNT - targetCycles) < 0) {
    }
    nextEventQ16 += symbolCyclesQ16;
  }

  void stopTimer(void) { timerRunning = false; }

  void resetWatchdog(void) {}

private:
  uint64_t symbolCyclesQ16 = 0;
  uint64_t pendingDelayQ16 = 0;
  uint64_t nextEventQ16 = 0;
  bool timerRunning = false;
};

using MiotyNode =
    TsUnbLib::TsUnb::SimpleNode<
        TsUnbLib::TsUnb::FixedUplinkMac,
        TsUnbLib::TsUnb::Phy<12099962, 12099962, 12, 12,
                             TsUnbLib::TsUnb::TsUnb_UPG1, 3,
                             TsUnbLib::TsUnb::RadioBurst<2, 2>>,
        TsUnbLib::Trx::SX1280Project6<Project6TsUnbCpu, 3,
                                       TsUnbLib::TsUnb::RadioBurst<2, 2>>>;

MiotyNode gMiotyNode;
bool gMiotyInitialized = false;

void configureMiotyNode(void) {
  gMiotyNode.Tx.setTxPower(13);
  gMiotyNode.Mac.setNetworkKey(
      kMiotyNetworkKey[0], kMiotyNetworkKey[1], kMiotyNetworkKey[2],
      kMiotyNetworkKey[3], kMiotyNetworkKey[4], kMiotyNetworkKey[5],
      kMiotyNetworkKey[6], kMiotyNetworkKey[7], kMiotyNetworkKey[8],
      kMiotyNetworkKey[9], kMiotyNetworkKey[10], kMiotyNetworkKey[11],
      kMiotyNetworkKey[12], kMiotyNetworkKey[13], kMiotyNetworkKey[14],
      kMiotyNetworkKey[15]);
  gMiotyNode.Mac.setAddress(kMiotyEui64[0], kMiotyEui64[1], kMiotyEui64[2],
                            kMiotyEui64[3], kMiotyEui64[4], kMiotyEui64[5],
                            kMiotyEui64[6], kMiotyEui64[7]);
  gMiotyNode.Mac.extPkgCnt = 0x01;
}

} // namespace

extern "C" int16_t Mioty_Project6_Init(void) {
  const int16_t ret = gMiotyNode.init();
  if (ret < 0) {
    gMiotyInitialized = false;
    return ret;
  }

  configureMiotyNode();
  gMiotyInitialized = true;
  return 0;
}

extern "C" int16_t Mioty_Project6_SendTestUuid(void) {
  if (!gMiotyInitialized) {
    const int16_t ret = Mioty_Project6_Init();
    if (ret < 0) {
      return ret;
    }
  }

  return gMiotyNode.send(kMiotyTestPayload, sizeof(kMiotyTestPayload));
}

#ifndef MIOTY_TRX_SX1280_PROJECT6_H
#define MIOTY_TRX_SX1280_PROJECT6_H

#include <stdint.h>

#include "../TsUnb/RadioBurst.h"
#include "../Utils/BitAccess.h"

namespace TsUnbLib {
namespace Trx {

static const uint8_t SX1280_PROJECT6_SET_STANDBY = 0x80;
static const uint8_t SX1280_PROJECT6_SET_SLEEP = 0x84;
static const uint8_t SX1280_PROJECT6_SET_RF_FREQUENCY = 0x86;
static const uint8_t SX1280_PROJECT6_SET_TX_PARAMS = 0x8E;
static const uint8_t SX1280_PROJECT6_SET_TX_CONTINUOUS_WAVE = 0xD1;

template <class Cpu_T, uint32_t F_DEV = 3, class RadioBurst_T = TsUnb::RadioBurst<>>
class SX1280Project6 {
public:
  SX1280Project6() : txPower(13) {}

  int16_t init(void) {
    Cpu.spiInit();
    Cpu.initTimer();

    setStandbyModeXOSC();
    setSleepMode();

    Cpu.spiDeinit();
    return 0;
  }

  int16_t transmit(const RadioBurst_T *const bursts, const uint16_t numTxBursts,
                   const uint32_t baseFrequencyReg) {
    Cpu.spiInit();
    Cpu.initTimer();
    setStandbyModeXOSC();

    Cpu.addTimerDelay(4);
    Cpu.startTimer();

    for (uint16_t burstIdx = 0; burstIdx < numTxBursts; ++burstIdx) {
      Cpu.resetWatchdog();

      if (bursts[burstIdx].getBurstLength() == 0) {
        Cpu.waitTimer();
        if (burstIdx + 1 < numTxBursts) {
          Cpu.addTimerDelay((int16_t)bursts[burstIdx].get_T_RB() -
                            (int16_t)bursts[burstIdx].getBurstLength());
        }
        continue;
      }

      const uint32_t carrierFrequency =
          baseFrequencyReg + (uint32_t)bursts[burstIdx].getCarrierOffset();

      const uint8_t firstBit = readBit(0, bursts[burstIdx].getBurst());
      Cpu.waitTimer();
      setStandbyModeXOSC();
      Cpu.waitTimer();
      setTxPowerReg(txPower);
      Cpu.waitTimer();
      setFrequencyReg(carrierFrequency + calcFdev(firstBit));
      Cpu.waitTimer();
      setContinuousWave();

      for (uint16_t bitIdx = 1; bitIdx < bursts[burstIdx].getBurstLength();
           ++bitIdx) {
        const uint8_t bit = readBit(bitIdx, bursts[burstIdx].getBurst());
        Cpu.waitTimer();
        setFrequencyReg(carrierFrequency + calcFdev(bit));
      }

      Cpu.waitTimer();
      setSleepMode();

      if (burstIdx + 1 < numTxBursts) {
        Cpu.addTimerDelay((int16_t)bursts[burstIdx].get_T_RB() -
                          (int16_t)bursts[burstIdx].getBurstLength() - 3);
      }
    }

    Cpu.stopTimer();
    Cpu.spiDeinit();
    return 0;
  }

  void setTxPower(const int8_t power) { txPower = power; }

private:
  void setFrequencyReg(const uint32_t frequencyReg) {
    uint8_t data[4] = {
        SX1280_PROJECT6_SET_RF_FREQUENCY,
        (uint8_t)((frequencyReg >> 16) & 0xFF),
        (uint8_t)((frequencyReg >> 8) & 0xFF),
        (uint8_t)(frequencyReg & 0xFF),
    };
    Cpu.spiSend(data, sizeof(data));
  }

  int8_t setTxPowerReg(int8_t power) {
    if (power > 13) {
      power = 13;
    }
    if (power < -18) {
      power = -18;
    }

    uint8_t data[3] = {SX1280_PROJECT6_SET_TX_PARAMS,
                       (uint8_t)(power + 18), 0xE0};
    Cpu.spiSend(data, sizeof(data));
    return power;
  }

  void setSleepMode(void) {
    uint8_t data[2] = {SX1280_PROJECT6_SET_SLEEP, 0x01};
    Cpu.spiSend(data, sizeof(data));
  }

  void setStandbyModeXOSC(void) {
    uint8_t data[2] = {SX1280_PROJECT6_SET_STANDBY, 0x01};
    Cpu.spiSend(data, sizeof(data));
  }

  void setContinuousWave(void) {
    uint8_t data = SX1280_PROJECT6_SET_TX_CONTINUOUS_WAVE;
    Cpu.spiSend(&data, 1);
  }

  int16_t calcFdev(const uint8_t bit) const {
    return bit ? (int16_t)F_DEV : -(int16_t)F_DEV;
  }

  Cpu_T Cpu;
  int8_t txPower;
};

} // namespace Trx
} // namespace TsUnbLib

#endif /* MIOTY_TRX_SX1280_PROJECT6_H */

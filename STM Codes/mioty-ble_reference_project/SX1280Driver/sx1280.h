#ifndef SX1280_H
#define SX1280_H

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <cmath>

/*!
 * \brief GPIO pin number connected to DIO1 on the SX1280 radio module.
 *
 * DIO1 is used by the SX1280 to signal events like payload ready, transmission done, etc.
 * This pin must be configured as an input and, depending on the specific use case,
 * may need to trigger interrupts.
 */
#define PIN_DIO1 2

/*!
 * \brief GPIO pin number connected to DIO2 on the SX1280 radio module.
 *
 * DIO2 is typically used for signaling events such as FHSS frequency change, CAD detection, etc.
 * Similar to DIO1, this pin should be configured based on the intended radio functionalities being used.
 */
#define PIN_DIO2 3

/*!
 * \brief GPIO pin number connected to DIO3 on the SX1280 radio module.
 *
 * DIO3 can be used for additional event signaling from the SX1280, which may include timing or synchronization signals.
 * Configuration and use will depend on the SX1280's operational mode and setup.
 */
#define PIN_DIO3 4

/*!
 * \brief Provides the frequency of the chip running on the radio and the frequency step
 *
 * \remark These defines are used for computing the frequency divider to set the RF frequency
 */
#define XTAL_FREQ 52000000
#define FREQ_STEP ((double)(XTAL_FREQ / pow(2.0, 18.0)))

/*!
 * \brief The address of the register holding the firmware version MSB
 */
#define REG_LR_FIRMWARE_VERSION_MSB 0x0153

/*!
 * \brief The address of the register holding the first byte defining the whitening seed
 *
 * \remark Only used for packet types GFSK, FLRC and BLE
 */
#define REG_LR_WHITSEEDBASEADDR 0x09C5

/*!
 * \brief Register for MSB Access Address (BLE)
 */
#define REG_LR_BLE_ACCESS_ADDRESS 0x09CF
#define BLE_ADVERTIZER_ACCESS_ADDRESS 0x8E89BED6

/*!
 * \brief opecodes
 */
#define RADIO_SET_STANDBY 0x80
#define RADIO_SET_REGULATORMODE 0x96
#define RADIO_GET_STATUS 0xC0
#define RADIO_WRITE_REGISTER 0x18
#define RADIO_READ_REGISTER 0x19
#define RADIO_WRITE_BUFFER 0x1A
#define RADIO_READ_BUFFER 0x1B
#define RADIO_SET_SLEEP 0x84
#define RADIO_SET_PACKETTYPE 0x8A
#define RADIO_SET_RFFREQUENCY 0x86
#define RADIO_SET_BUFFERBASEADDRESS 0x8F
#define RADIO_SET_MODULATIONPARAMS 0x8B
#define RADIO_SET_PACKETPARAMS 0x8C
#define RADIO_SET_TXPARAMS 0x8E
#define RADIO_SET_DIOIRQPARAMS 0x8D
#define RADIO_SET_TX 0x83
#define RADIO_GET_PACKETSTATUS 0x1D
#define RADIO_CLR_IRQSTATUS 0x97

typedef void (*DioIrqHandler)(uint gpio, uint32_t events);

/*!
 * \brief Declares the oscillator in use while in standby mode
 *
 * Using the STDBY_RC standby mode allow to reduce the energy consumption
 * STDBY_XOSC should be used for time critical applications
 */
enum RadioStandbyModes_t
{
    STDBY_RC = 0x00,
    STDBY_XOSC = 0x01,
};

/*!
 * \brief Declares the power regulation used to power the device
 *
 * This command allows the user to specify if DC-DC or LDO is used for power regulation.
 * Using only LDO implies that the Rx or Tx current is doubled
 */

enum RadioRegulatorModes_t
{
    USE_LDO = 0x00,
    USE_DCDC = 0x01,
};

/*!
 * \brief Represents the possible packet type (i.e. modem) used
 */
enum RadioPacketTypes_t
{
    PACKET_TYPE_BLE = 0x04,
};

/*!
 * \brief Represents the operating mode the radio is actually running
 */
enum OperatingModes_t
{
    MODE_TX,
    MODE_RX,
    MODE_CAD,
    MODE_STDBY_RC,
    MODE_STDBY_XOSC,
    MODE_FS,
    MODE_RX_DC,
    MODE_CAD_OK,
    MODE_CAD_DONE,
};

/*!
 * \brief Represents the ramping time for power amplifier
 */
typedef enum
{
    RADIO_RAMP_02_US = 0x00,
    RADIO_RAMP_04_US = 0x20,
    RADIO_RAMP_06_US = 0x40,
    RADIO_RAMP_08_US = 0x60,
    RADIO_RAMP_10_US = 0x80,
    RADIO_RAMP_12_US = 0xA0,
    RADIO_RAMP_16_US = 0xC0,
    RADIO_RAMP_20_US = 0xE0,
} RadioRampTimes_t;

/*!
 * \brief Represents the tick size available for Rx/Tx timeout operations
 */
typedef enum
{
    RADIO_TICK_SIZE_0015_US = 0x00,
    RADIO_TICK_SIZE_0062_US = 0x01,
    RADIO_TICK_SIZE_1000_US = 0x02,
    RADIO_TICK_SIZE_4000_US = 0x03,
} RadioTickSizes_t;

/*!
 * \brief Represents an amount of time measurable by the radio clock
 *
 * @code
 * Time = Step * NbSteps
 * Example:
 * Step = RADIO_TICK_SIZE_4000_US( 4 ms )
 * NbSteps = 1000
 * Time = 4e-3 * 1000 = 4 seconds
 * @endcode
 */
typedef struct TickTime_s
{
    RadioTickSizes_t Step; //!< The step of ticktime
    /*!
     * \brief The number of steps for ticktime
     * Special values are:
     *     - 0x0000 for single mode
     *     - 0xFFFF for continuous mode
     */
    uint16_t NbSteps;
} TickTime_t;

/*!
 * \brief Represents the packet status
 */
typedef struct
{
    RadioPacketTypes_t packetType; //!< Packet to which the packet status are referring to.
    struct
    {
        int8_t RssiSync; //!< The RSSI of the last packet
        struct
        {
            bool SyncError : 1;           //!< SyncWord error on last packet
            bool LengthError : 1;         //!< Length error on last packet
            bool CrcError : 1;            //!< CRC error on last packet
            bool AbortError : 1;          //!< Abort error on last packet
            bool HeaderReceived : 1;      //!< Header received on last packet
            bool PacketReceived : 1;      //!< Packet received
            bool PacketControlerBusy : 1; //!< Packet controller busy
        } ErrorStatus;                    //!< The error status Byte
        struct
        {
            bool PacketSent : 1;    //!< Packet sent, only relevant in Tx mode
        } TxRxStatus;               //!< The Tx/Rx status Byte
        uint8_t SyncAddrStatus : 3; //!< The id of the correlator who found the packet
    } Ble;
} PacketStatus_t;

class SX1280
{
public:
    virtual ~SX1280() {}
    virtual void Reset() = 0;
    virtual void Wakeup() = 0;
    virtual void IoIrqInit(DioIrqHandler irqHandler) = 0;
    virtual void SetStandby(RadioStandbyModes_t standbyConfig) = 0;
    virtual void SetRegulatorMode(RadioRegulatorModes_t mode) = 0;
    virtual void WriteRegister(uint16_t address, const uint8_t *buffer, uint16_t size) = 0;
    virtual void WriteRegister(uint16_t address, uint8_t value) = 0;
    virtual void ReadRegister(uint16_t address, uint8_t *buffer, uint16_t size) = 0;
    virtual uint8_t ReadRegister(uint16_t address) = 0;
    virtual void WriteCommand(uint8_t command, uint8_t *buffer, uint16_t size) = 0;
    virtual void ReadCommand(uint8_t command, uint8_t *buffer, uint16_t size) = 0;
    virtual void SetPacketType(RadioPacketTypes_t packetType) = 0;
    virtual void SetRfFrequency(uint32_t rfFrequency) = 0;
    virtual void SetBufferBaseAddresses(uint8_t txBaseAddress, uint8_t rxBaseAddress) = 0;
    virtual void SetModulationParams(uint8_t param1, uint8_t param2, uint8_t param3) = 0;
    virtual void SetPacketParams(uint8_t param1, uint8_t param2, uint8_t param3, uint8_t param4, uint8_t param5, uint8_t param6, uint8_t param7) = 0;
    virtual void SetBleAccessAddress(uint32_t accessAddress) = 0;
    virtual void SetBleAdvertizerAccessAddress(void) = 0;
    virtual void SetWhiteningSeed(uint8_t seed) = 0;
    virtual uint8_t SetCrcSeed(uint8_t *seed) = 0;
    virtual void SetTxParams(int8_t power, RadioRampTimes_t rampTime) = 0;
    virtual void WriteBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) = 0;
    virtual void ReadBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) = 0;
    virtual void SetupAdvPdu(uint8_t offset, uint8_t header, uint8_t len, const uint8_t *data, const uint8_t *bd_addr) = 0;
    virtual void SetDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask) = 0;
    virtual void SetTx(TickTime_t timeout) = 0;
    virtual uint8_t GetPacketStatus(PacketStatus_t *packetStatus) = 0;
    virtual void ClearIrqStatus(uint16_t irqMask) = 0;

protected:
    RadioPacketTypes_t PacketType;
    OperatingModes_t OperatingMode;
};
class SX1280Hal : public SX1280
{
public:
    SX1280Hal(uint cs_pin, uint reset_pin, uint busy_pin, uint mosi_pin, uint miso_pin, uint sck_pin);
    ~SX1280Hal();
    void Reset() override;
    void Wakeup() override;
    void IoIrqInit(DioIrqHandler irqHandler) override;
    void SetStandby(RadioStandbyModes_t standbyConfig) override;
    void SetRegulatorMode(RadioRegulatorModes_t mode) override;
    void WriteRegister(uint16_t address, const uint8_t *buffer, uint16_t size) override;
    void WriteRegister(uint16_t address, uint8_t value) override;
    void ReadRegister(uint16_t address, uint8_t *buffer, uint16_t size) override;
    uint8_t ReadRegister(uint16_t address) override;
    void WriteCommand(uint8_t command, uint8_t *buffer, uint16_t size) override;
    void ReadCommand(uint8_t command, uint8_t *buffer, uint16_t size) override;
    void SetPacketType(RadioPacketTypes_t packetType) override;
    void SetRfFrequency(uint32_t rfFrequency) override;
    void SetBufferBaseAddresses(uint8_t txBaseAddress, uint8_t rxBaseAddress) override;
    void SetModulationParams(uint8_t param1, uint8_t param2, uint8_t param3) override;
    void SetPacketParams(uint8_t param1, uint8_t param2, uint8_t param3, uint8_t param4, uint8_t param5, uint8_t param6, uint8_t param7) override;
    void SetBleAccessAddress(uint32_t accessAddress) override;
    void SetBleAdvertizerAccessAddress(void) override;
    void SetWhiteningSeed(uint8_t seed) override;
    uint8_t SetCrcSeed(uint8_t *seed) override;
    void SetTxParams(int8_t power, RadioRampTimes_t rampTime) override;
    void WriteBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) override;
    void ReadBuffer(uint8_t offset, uint8_t *buffer, uint8_t size) override;
    void SetDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask) override;
    void SetTx(TickTime_t timeout) override;
    void SetupAdvPdu(uint8_t offset, uint8_t header, uint8_t len, const uint8_t *data, const uint8_t *bd_addr) override;
    uint8_t GetPacketStatus(PacketStatus_t *packetStatus) override;
    void ClearIrqStatus(uint16_t irqMask) override;
    void WaitOnBusy();
    void DioAssignCallback(uint pin, DioIrqHandler irqHandler);

private:
    uint cs_pin_;
    uint reset_pin_;
    uint busy_pin_;
    uint mosi_pin_;
    uint miso_pin_;
    uint sck_pin_;

    void initialize_spi();
};

#endif

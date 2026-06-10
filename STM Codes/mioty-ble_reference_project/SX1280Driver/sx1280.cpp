#include "sx1280.h"
#include "pico/time.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include <string.h>

/*!
 * \brief Constructor for the SX1280Hal class.
 *
 * Initializes a new instance of the SX1280Hal class, setting up the necessary GPIO and SPI configurations
 * for the SX1280 radio communication. This constructor sets the pin modes and initial states for communication.
 *
 * \param cs_pin The chip select (CS) pin used to initiate SPI communication.
 * \param reset_pin The reset pin used to reset the radio hardware.
 * \param busy_pin The busy pin used to check the radio's busy status.
 * \param mosi_pin The Master Out Slave In (MOSI) pin used for SPI communication.
 * \param miso_pin The Master In Slave Out (MISO) pin used for SPI communication.
 * \param sck_pin The Serial Clock (SCK) pin used for SPI communication.
 *
 * \remark This constructor initializes the SPI communication settings and configures the pin directions and
 * initial states for control of the SX1280 radio module. Ensure that the pins provided are correctly configured
 * in the microcontroller's pinout and are not being used elsewhere in your application.
 */
SX1280Hal::SX1280Hal(uint cs_pin, uint reset_pin, uint busy_pin, uint mosi_pin, uint miso_pin, uint sck_pin)
    : cs_pin_(cs_pin), reset_pin_(reset_pin), busy_pin_(busy_pin), mosi_pin_(mosi_pin), miso_pin_(miso_pin), sck_pin_(sck_pin)
{
    initialize_spi();                // Set up SPI communication
    gpio_init(cs_pin_);              // Initialize the chip select pin
    gpio_set_dir(cs_pin_, GPIO_OUT); // Set the direction of the CS pin to output
    gpio_put(cs_pin_, 1);            // Set CS pin high (deselect)

    gpio_init(reset_pin_);              // Initialize the reset pin
    gpio_set_dir(reset_pin_, GPIO_OUT); // Set the direction of the reset pin to output

    gpio_init(busy_pin_);             // Initialize the busy pin
    gpio_set_dir(busy_pin_, GPIO_IN); // Set the direction of the busy pin to input
}

SX1280Hal::~SX1280Hal()
{
}

/*!
 * \brief Soft resets the radio
 */
void SX1280Hal::Reset()
{
    sleep_ms(20);            // Delay to ensure the radio is ready for reset
    gpio_put(reset_pin_, 0); // Set the reset pin low
    sleep_ms(50);            // Wait for the reset to take effect
    gpio_put(reset_pin_, 1); // Set the reset pin high to end the reset
    sleep_ms(20);            // Delay after reset
}
/*!
 * \brief Wakes up the radio
 */
void SX1280Hal::Wakeup()
{
    uint32_t irq_status = save_and_disable_interrupts(); // Disable interrupts to ensure atomic operation
    gpio_put(cs_pin_, 0);                                // Set CS low to start SPI communication
    uint8_t cmd = RADIO_GET_STATUS;                      // Command to get the status of the radio
    spi_write_blocking(spi0, &cmd, 1);                   // Send the command over SPI
    uint8_t dummy = 0;
    spi_write_blocking(spi0, &dummy, 1); // Perform a dummy write to receive data
    gpio_put(cs_pin_, 1);                // Set CS high to end SPI communication
    restore_interrupts(irq_status);      // Restore the interrupt status
    WaitOnBusy();                        // Wait for the radio to be ready
}

// WaitOnBusy: Polls the busy pin and waits until the SX1280 radio is ready for further commands.
void SX1280Hal::WaitOnBusy()
{
    while (gpio_get(busy_pin_)) // Check the status of the busy pin
    {
        tight_loop_contents(); // Keep polling until the pin is low
    }
}

/*!
 * \brief Returns the status of DIOs pins
 *
 * \retval      dioStatus     A byte where each bit represents a DIO state:
 *                            [ DIOx | BUSY ]
 */
void SX1280Hal::IoIrqInit(DioIrqHandler irqHandler)
{
    // Enable interrupts on the DIO pins and assign the handler
    gpio_set_irq_enabled_with_callback(PIN_DIO1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, irqHandler);
    gpio_set_irq_enabled_with_callback(PIN_DIO2, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, irqHandler);
    gpio_set_irq_enabled_with_callback(PIN_DIO3, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, irqHandler);
}

/*!
 * \brief Sets the radio in configuration mode
 *
 * \param [in]  mode          The standby mode to put the radio into
 */
void SX1280Hal::SetStandby(RadioStandbyModes_t standbyConfig)
{
    WriteCommand(RADIO_SET_STANDBY, (uint8_t *)&standbyConfig, 1); // Send the standby configuration command
    if (standbyConfig == STDBY_RC)
    {
        OperatingMode = MODE_STDBY_RC; // Set operating mode to RC standby
    }
    else
    {
        OperatingMode = MODE_STDBY_XOSC; // Set operating mode to crystal oscillator standby
    }
}

/*!
 * \brief Sets the power regulators operating mode
 *
 * \param [in]  mode          [0: LDO, 1:DC_DC]
 */
void SX1280Hal::SetRegulatorMode(RadioRegulatorModes_t mode)
{
    WriteCommand(RADIO_SET_REGULATORMODE, (uint8_t *)&mode, 1); // Send the regulator mode configuration command
}

/*!
 * \brief Write data to the radio memory
 *
 * \param [in]  address       The address of the first byte to write in the radio
 * \param [in]  buffer        The data to be written in radio's memory
 * \param [in]  size          The number of bytes to write in radio's memory
 */
void SX1280Hal::WriteRegister(uint16_t address, const uint8_t *buffer, uint16_t size)
{
    WaitOnBusy();         // Ensure the radio is not busy before starting a new operation
    gpio_put(cs_pin_, 0); // Set CS low to start the SPI communication
    uint8_t header[] = {RADIO_WRITE_REGISTER, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address & 0xFF)};
    spi_write_blocking(spi0, header, sizeof(header)); // Send the register address
    spi_write_blocking(spi0, buffer, size);           // Write the data to the register
    gpio_put(cs_pin_, 1);                             // Set CS high to end the communication
    WaitOnBusy();                                     // Wait for the operation to complete
}

/*!
 * \brief Write a single byte of data to the radio memory
 *
 * \param [in]  address       The address of the first byte to write in the radio
 * \param [in]  value         The data to be written in radio's memory
 */
void SX1280Hal::WriteRegister(uint16_t address, uint8_t value)
{
    WriteRegister(address, &value, 1); // Use the array version of WriteRegister to write a single byte
}

/*!
 * \brief Read data from the radio memory
 *
 * \param [in]  address       The address of the first byte to read from the radio
 * \param [out] buffer        The buffer that holds data read from radio
 * \param [in]  size          The number of bytes to read from radio's memory
 */
void SX1280Hal::ReadRegister(uint16_t address, uint8_t *buffer, uint16_t size)
{
    WaitOnBusy();         // Ensure the radio is not busy before starting the read operation
    gpio_put(cs_pin_, 0); // Set CS low to start the SPI communication
    uint8_t header[] = {RADIO_READ_REGISTER, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address & 0xFF), 0x00};
    spi_write_blocking(spi0, header, sizeof(header)); // Send the register address
    spi_read_blocking(spi0, 0x00, buffer, size);      // Read the data from the register
    gpio_put(cs_pin_, 1);                             // Set CS high to end the communication
    WaitOnBusy();                                     // Wait for the operation to complete
}

/*!
 * \brief Read a single byte of data from the radio memory
 *
 * \param [in]  address       The address of the first byte to write in the
 *                            radio
 *
 * \retval      value         The value of the byte at the given address in
 *                            radio's memory
 */
uint8_t SX1280Hal::ReadRegister(uint16_t address)
{
    uint8_t data;
    ReadRegister(address, &data, 1); // Use the array version of ReadRegister to read a single byte
    return data;                     // Return the read byte
}

/*!
 * \brief Send a command that write data to the radio
 *
 * \param [in]  opcode        Opcode of the command
 * \param [in]  buffer        Buffer to be send to the radio
 * \param [in]  size          Size of the buffer to send
 */
void SX1280Hal::WriteCommand(uint8_t command, uint8_t *buffer, uint16_t size)
{
    WaitOnBusy();                           // Ensure the radio is not busy before sending the command
    gpio_put(cs_pin_, 0);                   // Set CS low to start the SPI communication
    spi_write_blocking(spi0, &command, 1);  // Send the command
    spi_write_blocking(spi0, buffer, size); // Send the associated data
    gpio_put(cs_pin_, 1);                   // Set CS high to end the communication
    if (command != RADIO_SET_SLEEP)
    {
        WaitOnBusy(); // Wait for the command to be processed, unless it's the sleep command
    }
}

/*!
 * \brief Send a command that write data to the radio
 *
 * \param [in]  opcode        Opcode of the command
 * \param [in]  buffer        Buffer to be send to the radio
 * \param [in]  size          Size of the buffer to send
 */
void SX1280Hal::ReadCommand(uint8_t command, uint8_t *buffer, uint16_t size)
{
    WaitOnBusy(); // Ensure the radio is not busy before sending the command

    gpio_put(cs_pin_, 0); // Set CS low to start the SPI communication

    if (command == RADIO_GET_STATUS)
    {
        buffer[0] = spi_write_blocking(spi0, &command, 1); // Send the command and store the first byte of the response
        uint8_t zero = 0;
        spi_write_blocking(spi0, &zero, 1); // Send a dummy byte to keep the clock running
        spi_write_blocking(spi0, &zero, 1); // Another dummy byte for receiving the next part of the response
    }
    else
    {
        spi_write_blocking(spi0, &command, 1); // Send the command
        uint8_t zero = 0;
        spi_write_blocking(spi0, &zero, 1); // Dummy byte to keep the clock running
        for (uint16_t i = 0; i < size; i++)
        {
            buffer[i] = spi_write_blocking(spi0, &zero, 1); // Read the response byte by byte
        }
    }

    gpio_put(cs_pin_, 1); // Set CS high to end the communication
    WaitOnBusy();         // Wait for the operation to complete
}

/*!
 * \brief Sets the radio for the given protocol
 *
 * \param [in]  packetType    [PACKET_TYPE_GFSK, PACKET_TYPE_LORA,
 *                             PACKET_TYPE_RANGING, PACKET_TYPE_FLRC,
 *                             PACKET_TYPE_BLE]
 *
 * \remark This method has to be called before SetRfFrequency,
 *         SetModulationParams and SetPacketParams
 */
void SX1280Hal::SetPacketType(RadioPacketTypes_t packetType)
{
    this->PacketType = packetType; // Store the packet type internally

    WriteCommand(RADIO_SET_PACKETTYPE, (uint8_t *)&packetType, 1); // Send the packet type configuration command
}

/*!
 * \brief Sets the RF frequency
 *
 * \param [in]  frequency     RF frequency [Hz]
 */
void SX1280Hal::SetRfFrequency(uint32_t rfFrequency)
{
    uint8_t buf[3];                                              // Buffer to hold the frequency configuration
    uint32_t freq = (uint32_t)((double)rfFrequency / FREQ_STEP); // Calculate the frequency setting based on the defined step
    buf[0] = (uint8_t)((freq >> 16) & 0xFF);                     // Most significant byte
    buf[1] = (uint8_t)((freq >> 8) & 0xFF);                      // Middle byte
    buf[2] = (uint8_t)(freq & 0xFF);                             // Least significant byte
    WriteCommand(RADIO_SET_RFFREQUENCY, buf, 3);                 // Send the frequency configuration command
}

/*!
 * \brief Sets the data buffer base address for transmission and reception
 *
 * \param [in]  txBaseAddress Transmission base address
 * \param [in]  rxBaseAddress Reception base address
 */
void SX1280Hal::SetBufferBaseAddresses(uint8_t txBaseAddress, uint8_t rxBaseAddress)
{
    uint8_t buf[2];                                    // Buffer to hold the base address configuration
    buf[0] = txBaseAddress;                            // Transmission base address
    buf[1] = rxBaseAddress;                            // Reception base address
    WriteCommand(RADIO_SET_BUFFERBASEADDRESS, buf, 2); // Send the base address configuration command
}

/*!
 * \brief Set the modulation parameters
 *
 * \param [in]  param1  bit rate and bandwidth definition
 *  \param [in]  param2  modulation index definition
 *  \param [in]  param3  pulse shaping definition
 */
void SX1280Hal::SetModulationParams(uint8_t param1, uint8_t param2, uint8_t param3)
{
    uint8_t buf[3];                                   // Buffer to hold the modulation parameters
    buf[0] = param1;                                  // Parameter 1
    buf[1] = param2;                                  // Parameter 2
    buf[2] = param3;                                  // Parameter 3
    WriteCommand(RADIO_SET_MODULATIONPARAMS, buf, 3); // Send the modulation parameters configuration command
}

/*!
 * \brief Sets the packet parameters
 *
 * \param [in]  param1 ConnectionState
 * \param [in]  param2 CrcLength
 * \param [in]  param3 BleTestPayload
 * \param [in]  param4 Whitening
 * \param [in]  param5 Case BLE = 0x00
 * \param [in]  param6 Case BLE = 0x00
 * \param [in]  param7 Case BLE = 0x00
 */
void SX1280Hal::SetPacketParams(uint8_t param1, uint8_t param2, uint8_t param3, uint8_t param4, uint8_t param5, uint8_t param6, uint8_t param7)
{
    uint8_t buf[7];                               // Buffer to hold the packet parameters
    buf[0] = param1;                              // Parameter 1
    buf[1] = param2;                              // Parameter 2
    buf[2] = param3;                              // Parameter 3
    buf[3] = param4;                              // Parameter 4
    buf[4] = param5;                              // Parameter 5
    buf[5] = param6;                              // Parameter 6
    buf[6] = param7;                              // Parameter 7
    WriteCommand(RADIO_SET_PACKETPARAMS, buf, 7); // Send the packet parameters configuration command
}

/*!
 * \brief Set the Access Address field of BLE packet
 *
 * \param [in]  accessAddress The access address to be used for next BLE packet sent
 */
void SX1280Hal::SetBleAccessAddress(uint32_t accessAddress)
{
    WriteRegister(REG_LR_BLE_ACCESS_ADDRESS, (accessAddress >> 24) & 0xFF);     // Write the most significant byte of the access address
    WriteRegister(REG_LR_BLE_ACCESS_ADDRESS + 1, (accessAddress >> 16) & 0xFF); // Write the second byte
    WriteRegister(REG_LR_BLE_ACCESS_ADDRESS + 2, (accessAddress >> 8) & 0xFF);  // Write the third byte
    WriteRegister(REG_LR_BLE_ACCESS_ADDRESS + 3, accessAddress & 0xFF);         // Write the least significant byte
}

/*!
 * \brief Set the Access Address for Advertizer BLE packets
 *
 * All advertizer BLE packets must use a particular value for Access
 * Address field. This method sets it.
 *
 * \see SX1280::SetBleAccessAddress
 */
void SX1280Hal::SetBleAdvertizerAccessAddress(void)
{
    SetBleAccessAddress(BLE_ADVERTIZER_ACCESS_ADDRESS); // Set the predefined BLE advertiser access address
}

/*!
 * \brief Sets the Initial value of the LFSR used for the whitening in GFSK, FLRC and BLE protocols
 *
 * \param [in]  seed          Initial LFSR value
 */
void SX1280Hal::SetWhiteningSeed(uint8_t seed)
{
    WriteRegister(REG_LR_WHITSEEDBASEADDR, seed); // Write the whitening seed to the appropriate register
}

/*!
 * \brief Sets the Initial value for the LFSR used for the CRC calculation
 *
 * \param [in]  seed          Initial LFSR value
 *
 */
uint8_t SX1280Hal::SetCrcSeed(uint8_t *seed)
{
    WriteRegister(0x9C7, seed[2]); // Write the most significant byte of the CRC seed
    WriteRegister(0x9C8, seed[1]); // Write the second byte
    WriteRegister(0x9C9, seed[0]); // Write the least significant byte
    return 1;                      // Return success
}

/*!
 * \brief Sets the transmission parameters
 *
 * \param [in]  power         RF output power [-18..13] dBm
 * \param [in]  rampTime      Transmission ramp up time
 */
void SX1280Hal::SetTxParams(int8_t power, RadioRampTimes_t rampTime)
{
    uint8_t buf[2];                           // Buffer to hold the transmission parameters
    buf[0] = power + 18;                      // Adjust the power parameter
    buf[1] = (uint8_t)rampTime;               // Set the ramp time
    WriteCommand(RADIO_SET_TXPARAMS, buf, 2); // Send the transmission parameters configuration command
}

/*!
 * \brief Write data to the buffer holding the payload in the radio
 *
 * \param [in]  offset        The offset to start writing the payload
 * \param [in]  buffer        The data to be written (the payload)
 * \param [in]  size          The number of byte to be written
 */
void SX1280Hal::WriteBuffer(uint8_t offset, uint8_t *buffer, uint8_t size)
{
    WaitOnBusy();                                     // Ensure the radio is not busy before starting the write operation
    gpio_put(cs_pin_, 0);                             // Set CS low to start the SPI communication
    uint8_t header[] = {RADIO_WRITE_BUFFER, offset};  // Prepare the header with the buffer offset
    spi_write_blocking(spi0, header, sizeof(header)); // Send the header
    spi_write_blocking(spi0, buffer, size);           // Write the data to the buffer
    gpio_put(cs_pin_, 1);                             // Set CS high to end the communication
    WaitOnBusy();                                     // Wait for the operation to complete
}

/*!
 * \brief Read data from the buffer holding the payload in the radio
 *
 * \param [in]  offset        The offset to start reading the payload
 * \param [out] buffer        A pointer to a buffer holding the data from the radio
 * \param [in]  size          The number of byte to be read
 */
void SX1280Hal::ReadBuffer(uint8_t offset, uint8_t *buffer, uint8_t size)
{
    WaitOnBusy();                                       // Ensure the radio is not busy before starting the read operation
    gpio_put(cs_pin_, 0);                               // Set CS low to start the SPI communication
    uint8_t command[] = {RADIO_READ_BUFFER, offset, 0}; // Prepare the command with the buffer offset
    spi_write_blocking(spi0, command, sizeof(command)); // Send the command
    spi_read_blocking(spi0, 0x00, buffer, size);        // Read the data from the buffer
    gpio_put(cs_pin_, 1);                               // Set CS high to end the communication
    WaitOnBusy();                                       // Wait for the operation to complete
}

/*!
 * \brief Sets up an advertising packet data unit (PDU) for BLE transmission.
 *
 * This function prepares and writes a BLE advertising packet to the SX1280's buffer. It is designed to configure
 * the payload, header, and necessary BLE fields for broadcasting over BLE advertising channels.
 *
 * \param [in] offset  The offset in the radio's buffer where the advertising PDU starts.
 * \param [in] header  The header byte of the PDU, typically defining the type and properties of the PDU.
 * \param [in] len     The length of the advertising data to follow the header and BD address.
 * \param [in] data    Pointer to the array containing the advertising payload data.
 * \param [in] bd_addr Pointer to an array containing the Bluetooth device address.
 *
 * The function constructs the advertising packet by setting the PDU type in the header, followed by the length
 * which includes the BD address and the actual data length. It then copies the BD address and the data into the
 * buffer and sends this buffer to the radio's memory for transmission using the WriteBuffer function.
 */
void SX1280Hal::SetupAdvPdu(uint8_t offset, uint8_t header, uint8_t len, const uint8_t *data, const uint8_t *bd_addr)
{
    uint8_t buffer[39];                       // Buffer to hold the PDU
    buffer[0] = header;                       // Set the PDU header
    buffer[1] = 6 + len;                      // Set the length of the PDU
    memcpy(&buffer[2], bd_addr, 6);           // Copy the BD address into the buffer
    memcpy(&buffer[8], data, len);            // Copy the data into the buffer
    uint16_t packet_size = 2 + buffer[1];     // Calculate the total packet size
    WriteBuffer(offset, buffer, packet_size); // Write the PDU to the radio's buffer
}

/*!
 * \brief   Sets the IRQ mask and DIO masks
 *
 * \param [in]  irqMask       General IRQ mask
 * \param [in]  dio1Mask      DIO1 mask
 * \param [in]  dio2Mask      DIO2 mask
 * \param [in]  dio3Mask      DIO3 mask
 */
void SX1280Hal::SetDioIrqParams(uint16_t irqMask, uint16_t dio1Mask, uint16_t dio2Mask, uint16_t dio3Mask)
{
    uint8_t buf[8];                               // Buffer to hold the IRQ and DIO mask settings
    buf[0] = (uint8_t)((irqMask >> 8) & 0xFF);    // High byte of IRQ mask
    buf[1] = (uint8_t)(irqMask & 0xFF);           // Low byte of IRQ mask
    buf[2] = (uint8_t)((dio1Mask >> 8) & 0xFF);   // High byte of DIO1 mask
    buf[3] = (uint8_t)(dio1Mask & 0xFF);          // Low byte of DIO1 mask
    buf[4] = (uint8_t)((dio2Mask >> 8) & 0xFF);   // High byte of DIO2 mask
    buf[5] = (uint8_t)(dio2Mask & 0xFF);          // Low byte of DIO2 mask
    buf[6] = (uint8_t)((dio3Mask >> 8) & 0xFF);   // High byte of DIO3 mask
    buf[7] = (uint8_t)(dio3Mask & 0xFF);          // Low byte of DIO3 mask
    WriteCommand(RADIO_SET_DIOIRQPARAMS, buf, 8); // Send the DIO interrupt parameters configuration command
}

/*!
 * \brief Sets the radio in transmission mode
 *
 * \param [in]  timeout       Structure describing the transmission timeout value
 */
void SX1280Hal::SetTx(TickTime_t timeout)
{
    uint8_t buf[3];                                               // Buffer to hold the timeout configuration
    buf[0] = timeout.Step;                                        // Timeout step size
    buf[1] = static_cast<uint8_t>((timeout.NbSteps >> 8) & 0xFF); // High byte of the number of steps
    buf[2] = static_cast<uint8_t>(timeout.NbSteps & 0xFF);        // Low byte of the number of steps
    WriteCommand(RADIO_SET_TX, buf, 3);                           // Send the transmission configuration command
    OperatingMode = MODE_TX;                                      // Update the operating mode to transmission
}

/*!
 * \brief Gets the last received packet payload length
 *
 * \param [out] pktStatus     A structure of packet status
 */
uint8_t SX1280Hal::GetPacketStatus(PacketStatus_t *packetStatus)
{
    uint8_t status[5];                                                      // Buffer to hold the status response
    ReadCommand(RADIO_GET_PACKETSTATUS, status, 5);                         // Send the command to get the packet status
    packetStatus->packetType = PACKET_TYPE_BLE;                             // Set the packet type to BLE
    packetStatus->Ble.RssiSync = -(status[1] / 2);                          // Calculate the RSSI of the sync
    packetStatus->Ble.ErrorStatus.SyncError = (status[2] >> 6) & 0x01;      // Extract the sync error flag
    packetStatus->Ble.ErrorStatus.LengthError = (status[2] >> 5) & 0x01;    // Extract the length error flag
    packetStatus->Ble.ErrorStatus.CrcError = (status[2] >> 4) & 0x01;       // Extract the CRC error flag
    packetStatus->Ble.ErrorStatus.AbortError = (status[2] >> 3) & 0x01;     // Extract the abort error flag
    packetStatus->Ble.ErrorStatus.HeaderReceived = (status[2] >> 2) & 0x01; // Extract the header received flag
    packetStatus->Ble.ErrorStatus.PacketReceived = (status[2] >> 1) & 0x01; // Extract the packet received flag
    packetStatus->Ble.ErrorStatus.PacketControlerBusy = status[2] & 0x01;   // Extract the packet controller busy flag
    packetStatus->Ble.TxRxStatus.PacketSent = status[3] & 0x01;             // Extract the packet sent flag
    packetStatus->Ble.SyncAddrStatus = status[4] & 0x07;                    // Extract the sync address status
    return 0;                                                               // Return success
}

/*!
 * \brief Clears the IRQs
 *
 * \param [in]  irq           IRQ(s) to be cleared
 */
void SX1280Hal::ClearIrqStatus(uint16_t irqMask)
{
    uint8_t buf[2];                                         // Buffer to hold the IRQ mask
    buf[0] = static_cast<uint8_t>((irqMask >> 8) & 0x00FF); // High byte of the IRQ mask
    buf[1] = static_cast<uint8_t>(irqMask & 0x00FF);        // Low byte of the IRQ mask
    WriteCommand(RADIO_CLR_IRQSTATUS, buf, 2);              // Send the command to clear the IRQ statuses
}

/*!
 * \brief Initializes the SPI communication interface for the SX1280 radio module.
 *
 * This function configures the SPI interface settings necessary for communication between the microcontroller
 * and the SX1280 radio module. It sets up the SPI clock frequency and configures the necessary GPIO pins for
 * SPI functionality, which includes Master Out Slave In (MOSI), Master In Slave Out (MISO), and Serial Clock (SCK).
 *
 * The SPI communication is initialized at 1 MHz, which ensures compatible speed for reliable data transmission
 * and reception with the SX1280 module. This initialization is crucial for the proper operation of the radio module,
 * allowing it to communicate efficiently with the controlling microcontroller.
 *
 * \remarks
 * - The function assumes that spi0 is used for the SPI interface. Ensure that spi0 is not used elsewhere in your application
 *   or conflicts may arise.
 * - The pins used for MOSI, MISO, and SCK should be correctly configured in the microcontroller's pin settings and
 *   should not be used for other peripheral functions.
 */
void SX1280Hal::initialize_spi()
{
    spi_init(spi0, 1000000);                     // Initialize the SPI at 1 MHz
    gpio_set_function(mosi_pin_, GPIO_FUNC_SPI); // Set the MOSI pin to SPI function
    gpio_set_function(miso_pin_, GPIO_FUNC_SPI); // Set the MISO pin to SPI function
    gpio_set_function(sck_pin_, GPIO_FUNC_SPI);  // Set the SCK pin to SPI function
}

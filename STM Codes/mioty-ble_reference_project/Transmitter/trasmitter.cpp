#include "pico/stdlib.h"         // Standard library for Raspberry Pi Pico
#include "hardware/gpio.h"       // GPIO control library for handling input/output
#include <stdio.h>               // Standard I/O library for logging and debugging
#include "SX1280Driver/sx1280.h" // Driver for the SX1280 radio module
#include "UTILS/utils.h"         // Utility functions for BLE operation
#include <string.h>              // String manipulation library

// Global variable to track the transmission status
volatile bool tx_done = false;

// A table to hold BLE advertising frequencies and whitening seeds for different channels
static const struct
{
    uint32_t freq_hz;  // Frequency in Hz for BLE channels 37, 38, and 39
    uint8_t whitening; // Whitening seed for the specific channel
} channel_table[] = {
    {2402000000, 0x53}, // BLE Channel 37
    {2426000000, 0x33}, // BLE Channel 38
    {2480000000, 0x73}  // BLE Channel 39
};

// Enum to define the BLE formats supported, either iBeacon or Eddystone
enum BLEFormat
{
    IBEACON,  ///< iBeacon advertisement format
    EDDYSTONE ///< Eddystone advertisement format
};

// Bluetooth device address in little-endian format, used for BLE advertising
static struct
{
    uint8_t bd_addr_le[6]; // Bluetooth device address (LE format)
} ctx = {
    {0xC1, 0x05, 0x06, 0x03, 0x02, 0x01}}; // Example BLE device address

/**
 * @brief DIO (Digital Input/Output) interrupt handler for handling radio events.
 *
 * This function is triggered when a rising edge is detected on a GPIO pin, signaling
 * that the BLE transmission has completed.
 *
 * @param gpio   The GPIO pin that triggered the interrupt.
 * @param events The events associated with the GPIO pin (e.g., rising edge).
 */
void dio_irq_handler(uint gpio, uint32_t events)
{
    if (events & GPIO_IRQ_EDGE_RISE)
    {
        tx_done = true; // Set the transmission done flag when the rising edge occurs.
    }
}

/**
 * @brief Prepares the SX1280 radio module for BLE beacon transmission.
 *
 * This function sets up the radio for BLE transmission by configuring the packet type,
 * modulation parameters, access address, and transmission power.
 *
 * @param radio The SX1280Hal radio object used for configuring the SX1280 radio module.
 */
void prepareForBeacon(SX1280Hal &radio)
{
    radio.SetPacketType(PACKET_TYPE_BLE);                            // Set the radio to BLE packet type
    radio.SetModulationParams(0x45, 0x01, 0x20);                     // Set modulation parameters for BLE
    radio.SetPacketParams(0x20, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00); // Set BLE packet parameters
    radio.SetBleAdvertizerAccessAddress();                           // Set the BLE advertiser access address
    uint8_t crcSeed[3] = {0x55, 0x55, 0x55};                         // Define CRC seed for packet integrity
    radio.SetCrcSeed(crcSeed);                                       // Set CRC seed
    radio.SetTxParams(10, RADIO_RAMP_02_US);                         // Set transmission power and ramp time
    uint16_t irq_mask = 0x0001;                                      // Enable interrupt for completed transmission
    radio.SetDioIrqParams(irq_mask, 0x0001, 0x0000, 0x0000);         // Configure interrupt for DIO1 pin
}

/**
 * @brief Initializes the SX1280 radio module.
 *
 * This function resets the radio, wakes it up, configures the interrupt handler, and
 * sets the power regulator to DC-DC mode for energy efficiency.
 *
 * @param radio The SX1280Hal radio object used for initializing the SX1280 radio module.
 */
void initializeRadio(SX1280Hal &radio)
{
    radio.Reset();                    // Reset the radio to a known state
    radio.Wakeup();                   // Wake up the radio from sleep mode
    radio.IoIrqInit(dio_irq_handler); // Initialize DIO interrupt handler
    radio.SetRegulatorMode(USE_DCDC); // Set power regulator mode to DC-DC for efficiency
    radio.SetStandby(STDBY_RC);       // Set the radio to standby mode with RC oscillator
}

/**
 * @brief Transmits BLE advertisement data over three BLE channels.
 *
 * This function transmits BLE packets on channels 37, 38, and 39. For each channel, it
 * configures the frequency, whitening seed, sets up the advertisement packet, and waits
 * for transmission completion.
 *
 * @param radio       The SX1280Hal radio object used for transmission.
 * @param data        The BLE packet data to be transmitted.
 * @param header      The BLE packet header.
 * @param len         The length of the BLE packet data.
 * @param interval_ms The interval in milliseconds between transmissions on different channels.
 */
void transmitData(SX1280Hal &radio, const uint8_t *data, uint8_t header, uint8_t len, uint32_t interval_ms)
{
    // Loop through the three BLE advertising channels
    for (int i = 0; i < sizeof(channel_table) / sizeof(channel_table[0]); ++i)
    {
        radio.SetRfFrequency(channel_table[i].freq_hz);     // Set the RF frequency for the current channel
        radio.SetWhiteningSeed(channel_table[i].whitening); // Set the whitening seed for the current channel

        tx_done = false; // Reset the transmission done flag

        // Setup the BLE advertisement packet (header, data, and device address)
        radio.SetupAdvPdu(0x80, header, len, data, ctx.bd_addr_le);
        radio.SetBufferBaseAddresses(0x80, 0x00);  // Set the buffer addresses for Tx and Rx
        radio.SetTx({RADIO_TICK_SIZE_1000_US, 1}); // Start transmission with specified timeout

        // Wait for the transmission to complete
        while (!tx_done)
        {
            tight_loop_contents(); // Keep the processor in a tight loop until transmission completes
        }

        // Print debug message indicating the current channel
        printf("TX BLE in channel %d\n", 37 + i); // Channel numbers 37, 38, and 39

        displayPacketStatus(radio);   // Display the packet status after transmission
        radio.ClearIrqStatus(0x0001); // Clear the interrupt status
        sleep_ms(interval_ms);        // Wait for the specified interval before next transmission
    }
}

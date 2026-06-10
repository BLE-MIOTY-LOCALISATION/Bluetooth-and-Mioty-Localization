#ifndef TRANSMITTER_H
#define TRANSMITTER_H

#include "SX1280Driver/sx1280.h" // Include the SX1280 radio driver for communication

/**
 * @enum BLEFormat
 * Enum for specifying the BLE advertisement format.
 *
 * - IBEACON: Represents the iBeacon format for BLE packets.
 * - EDDYSTONE: Represents the Eddystone format for BLE packets.
 */
enum BLEFormat
{
    IBEACON,  ///< iBeacon format
    EDDYSTONE ///< Eddystone format
};

/**
 * @brief Interrupt Service Routine (ISR) handler for DIO (Digital Input/Output) interrupts.
 *
 * @param gpio The GPIO pin number that triggered the interrupt.
 * @param events The type of events associated with the interrupt (rising edge, falling edge, etc.).
 */
void dio_irq_handler(uint gpio, uint32_t events);

/**
 * @brief Prepares the SX1280 radio for BLE transmission by configuring the necessary packet type
 * and modulation parameters.
 *
 * @param radio The SX1280Hal radio object that will be used for transmission.
 */
void prepareForBeacon(SX1280Hal &radio);

/**
 * @brief Initializes the SX1280 radio, resetting it and configuring settings such as power mode
 * and interrupt handling.
 *
 * @param radio The SX1280Hal radio object to be initialized.
 */
void initializeRadio(SX1280Hal &radio);

/**
 * @brief Transmits a BLE advertisement packet using the SX1280 radio.
 *
 * @param radio The SX1280Hal radio object used for transmission.
 * @param data A pointer to the BLE advertisement packet to be transmitted.
 * @param header The header of the BLE packet.
 * @param len The length of the BLE packet data.
 * @param interval_ms The time interval (in milliseconds) between consecutive transmissions.
 */
void transmitData(SX1280Hal &radio, const uint8_t *data, uint8_t header, uint8_t len, uint32_t interval_ms);

#endif // TRANSMITTER_H

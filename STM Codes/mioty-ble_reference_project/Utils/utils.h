#ifndef UTILS_H
#define UTILS_H

#include "SX1280Driver/sx1280.h"

/**
 * @brief Initializes the GPIO pin for LED control.
 *
 * This function sets up the GPIO pin to be used as an output for controlling an LED.
 * It must be called before using the LED in the program.
 */
void initializeLed();

/**
 * @brief Toggles the LED state to create a blinking effect.
 *
 * This function turns the LED on and off with a delay in between to create a visual indication.
 * It can be used to signal operation status.
 */
void blink();

/**
 * @brief Waits until a USB connection is established.
 *
 * This function keeps the system in an active loop until a USB connection is detected.
 * It ensures that console output is visible before proceeding with the program.
 */
void waitUntilUsbConnected();

/**
 * @brief Displays the status of the last packet transmission.
 *
 * This function retrieves the packet transmission status from the SX1280 radio module and
 * prints whether the BLE packet was successfully sent.
 *
 * @param radio The SX1280Hal object representing the SX1280 radio module.
 */
void displayPacketStatus(SX1280Hal &radio);

#endif // UTILS_H

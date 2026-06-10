#include "utils.h"
#include <stdio.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// Define the GPIO pin number for LED
#define LED_PIN 25

/**
 * @brief Initializes the GPIO pin for LED control.
 *
 * This function sets up the specified GPIO pin to control an LED.
 * The pin is configured as an output, allowing it to control the state of the LED.
 */
void initializeLed()
{
    gpio_init(LED_PIN);              // Initialize the GPIO pin for the LED.
    gpio_set_dir(LED_PIN, GPIO_OUT); // Set the GPIO pin as an output pin.
}

/**
 * @brief Toggles the LED state to create a blinking effect.
 *
 * This function turns the LED on for 500 milliseconds and then off for 500 milliseconds.
 * It can be used to provide a visual indication during operation.
 */
void blink()
{
    gpio_put(LED_PIN, true);  // Turn the LED on.
    sleep_ms(500);            // Keep the LED on for 500 milliseconds.
    gpio_put(LED_PIN, false); // Turn the LED off.
}

/**
 * @brief Waits until a USB connection is established.
 *
 * This function keeps the processor in an active loop until the USB connection
 * is established. It ensures that console output is visible to the user.
 */
void waitUntilUsbConnected()
{
    while (!stdio_usb_connected())
    {
        tight_loop_contents(); // Keeps the processor active during the wait.
    }
}

/**
 * @brief Displays the status of the last packet transmission.
 *
 * This function retrieves the transmission status from the SX1280 radio module
 * and prints the result to the console. It shows whether the BLE packet was successfully sent.
 *
 * @param radio The SX1280Hal radio object used to communicate with the SX1280 module.
 */
void displayPacketStatus(SX1280Hal &radio)
{
    PacketStatus_t packetStatus;
    radio.GetPacketStatus(&packetStatus);                                // Fetch the packet status from the SX1280 radio module.
    printf("Packet Sent: %d\n", packetStatus.Ble.TxRxStatus.PacketSent); // Display whether the BLE packet was successfully sent.
}

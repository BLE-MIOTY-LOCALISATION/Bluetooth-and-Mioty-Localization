#include "pico/stdlib.h"             // Standard library for Raspberry Pi Pico
#include "hardware/gpio.h"           // Library for GPIO control
#include <stdio.h>                   // Standard input/output library
#include "SX1280Driver/sx1280.h"     // Driver for the SX1280 radio module
#include "UTILS/utils.h"             // Additional utilities
#include "Transmitter/transmitter.h" // Functions for transmission
#include "RPPicoTsUnb.h"             // Library for MIOTY and ultra-narrowband communication

// Define the network key and device address for MIOTY communication
#define MAC_NETWORK_KEY 0x53, 0xad, 0xf8, 0x02, 0xb1, 0x97, 0xf2, 0x73, 0x8d, 0x5d, 0xdd, 0xa5, 0x77, 0xe1, 0xfa, 0x9c
#define MAC_EUI64 0x70, 0xb3, 0xd5, 0x67, 0x70, 0xff, 0x01, 0x70

#define TRANSMIT_PWR 20 // Define the transmission power for MIOTY communication
#define INTERVAL 1000   // Define the interval in milliseconds for BLE packet transmission

// Define constants for BLE and MIOTY transmission sequence
#define BLE_PKTS_BTW_BLEMIOTY 6           // Number of BLE packets before switching to MIOTY
#define CALLS (BLE_PKTS_BTW_BLEMIOTY / 3) // Number of times BLE is transmitted before sending a MIOTY packet

using namespace TsUnbLib::RPPico; // Using namespace for RPPico, required for MIOTY communication
TsUnb_EU1_Lambda80_t TsUnb_Node;  // Define the MIOTY node object

/**
 * @brief Prepares the node for MIOTY transmission.
 *
 * This function initializes the MIOTY node and sets up the network key, device address,
 * and transmission power for the communication.
 */
void prepareForMioty()
{
    TsUnb_Node.init();                             // Initialize the MIOTY node.
    TsUnb_Node.Tx.setTxPower(TRANSMIT_PWR);        // Set the transmission power.
    TsUnb_Node.Mac.setNetworkKey(MAC_NETWORK_KEY); // Set the network key for communication.
    TsUnb_Node.Mac.setAddress(MAC_EUI64);          // Set the MAC address.
    TsUnb_Node.Mac.extPkgCnt = 0x01;               // Set the extended package count for MIOTY.
}

// Define a UUID for BLE packets to differentiate devices or organizations
uint8_t uuid[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

/**
 * @brief BLE iBeacon Data Packet.
 *
 * This array contains the data formatted for iBeacon transmission.
 */
uint8_t ibeacon_data[] = {
    0x02, 0x01, 0x06, // BLE advertising flags.
    0x1A, 0xFF,       // Length and manufacturer-specific data.
    0x4C, 0x00,       // Manufacturer ID (Apple's Bluetooth SIG ID).
    0x02, 0x15,       // iBeacon subtype and payload length.
    // UUID (Universally Unique Identifier) to differentiate the beacon source.
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x00, 0x01, // Major value used to group beacons.
    0x00, 0x02, // Minor value used to differentiate beacons in the group.
    0xC5        // Calibrated transmission power (-59 dBm).
};
// Eddystone UID data payload (for reference)
uint8_t eddystone_template[] = {
    0x02, 0x01, 0x06,       // Flags
    0x03, 0x03, 0xAA, 0xFE, // Complete list of 16-bit Service UUIDs (0xFEAA)
    0x17, 0x16, 0xAA, 0xFE, // Length, Service Data, Eddystone UUID
    0x00,                   // Frame type (UID)
    0xF8,                   // Calibrated Tx power at 0 meters
    // Namespace placeholder (10 bytes)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    // Instance placeholder (6 bytes)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x00, 0x00 // RFU (Reserved for Future Use)
};

uint8_t header = 0x42;              // BLE packet header.
uint8_t len = sizeof(ibeacon_data); // Calculate the length of the BLE data array.
// If using Eddystone template, uncomment the following line to set its length
// uint8_t len = sizeof(eddystone_template);

/**
 * @brief Main function of the program.
 *
 * This function initializes the SX1280 radio module, prepares for BLE transmission,
 * and switches between BLE and MIOTY transmission based on a predefined number of BLE packets.
 */
int main()
{
    stdio_init_all(); // Initialize all standard I/O channels for communication.

    // Initialize the SX1280 radio module with specific GPIO pins.
    SX1280Hal radio(17, 7, 6, 19, 16, 18);
    waitUntilUsbConnected(); // Ensure USB is connected before continuing.
    initializeRadio(radio);  // Initialize and configure the SX1280 radio module.

    initializeLed(); // Initialize LED for status indication during transmission.

    int calls_counter = 0; // Counter to track the number of BLE packets transmitted.

    // Prepare the radio for BLE transmission on channels 37, 38, and 39.
    prepareForBeacon(radio);

    while (true) // Main loop for continuous transmission.
    {
        // Transmit BLE packets using the prepared iBeacon data.
        transmitData(radio, ibeacon_data, header, len, INTERVAL);
        // Alternatively, transmit the Eddystone data by uncommenting the following line:
        // transmitData(radio, ibeacon_data, header, len, INTERVAL);
        calls_counter++; // Increment the packet counter.

        // Check if it is time to switch to MIOTY transmission.
        if (calls_counter >= CALLS)
        {
            printf("Switching to Mioty\n"); // Notify that the system is switching to MIOTY transmission.

            prepareForMioty(); // Prepare the MIOTY node for transmission.

            // Convert the UUID from uint8_t array to a char array for MIOTY transmission.
            char uuid_char[16];
            for (int i = 0; i < 16; i++)
            {
                uuid_char[i] = (char)uuid[i];
            }

            // Send the UUID using MIOTY protocol.
            TsUnb_Node.send((uint8_t *)uuid_char, sizeof(uuid_char));
            printf("TX Mioty\n");         // Log that the MIOTY packet has been transmitted.
            printf("Switching to BLE\n"); // Notify that the system is switching back to BLE transmission.
            sleep_ms(1000);               // Pause for one second before resuming BLE transmission.

            calls_counter = 0; // Reset the BLE packet counter after MIOTY transmission.

            // Reconfigure the radio for BLE transmission after MIOTY.
            SX1280Hal radio(17, 7, 6, 19, 16, 18);
            initializeRadio(radio);

            // Re-prepare the radio for BLE transmission on channels 37, 38, and 39.
            prepareForBeacon(radio);
        }
    }

    return 0; // The program will never reach this point since the loop is infinite.
}

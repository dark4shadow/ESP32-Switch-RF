#include <Arduino.h>

// Define UART pins
#define RXD2 21  // GPIO21 (RX pin on the board) - ExpressLRS input
#define TXD2 22  // GPIO22 (TX pin on the board)
#define RXD1 12  // GPIO12 for UART1 (Choose appropriate pins)
#define TXD1 13  // GPIO13 for UART1
// Flight controller UART
#define RXD0 3   // GPIO3 (RX0) for flight controller
#define TXD0 1   // GPIO1 (TX0) for flight controller

// Debug output UART (USB serial)
//#define DEBUG_UART Serial

// CRSF Protocol defines
#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAME_SIZE_MAX 64

// RC channel configuration
#define SWITCH_SC_CHANNEL 8  

// Buffer for incoming data
uint8_t dataBuffer[CRSF_FRAME_SIZE_MAX];
uint8_t crsfData[CRSF_FRAME_SIZE_MAX];
int activeUart = 2; // Default to UART2

void setup() {
  // Initialize USB Serial for debugging
  //DEBUG_UART.begin(115200);
  delay(100);
  //DEBUG_UART.println("Starting ExpressLRS Bridge");
  
  // Initialize UART for Flight Controller (UART0)
  Serial.begin(420000, SERIAL_8N1, RXD0, TXD0);
  
  // Initialize UART for ExpressLRS receiver input
  Serial2.begin(420000, SERIAL_8N1, RXD2, TXD2);
  
  // Initialize another UART for optional device
  Serial1.begin(420000, SERIAL_8N1, RXD1, TXD1);
  
  //DEBUG_UART.println("UART interfaces initialized");
  //DEBUG_UART.println("Waiting for ExpressLRS data...");
}

void loop() {
  // Forward data: ExpressLRS Receiver -> Flight Controller
  while (Serial2.available()) {
    uint8_t byte = Serial2.read();
    Serial.write(byte); // Forward directly to flight controller
  }
  
  // Forward data: Flight Controller -> ExpressLRS Receiver
  while (Serial.available()) {
    uint8_t byte = Serial.read();
    Serial2.write(byte);
  }
}
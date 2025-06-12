#include <Arduino.h>

// Define UART pins
#define RXD2 21  // GPIO21 (RX pin on the board)
#define TXD2 22  // GPIO22 (TX pin on the board)
#define RXD1 12  // GPIO12 for UART1 (Choose appropriate pins)
#define TXD1 13  // GPIO13 for UART1

// CRSF Protocol defines
#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAME_SIZE_MAX 64

// RC channel configuration
#define SWITCH_SC_CHANNEL 6  // Adjust if your switch SC is on a different channel (A button curently set to channel 6) 

// Switch positions (typical ranges for 3-position switch)
#define SWITCH_UP_VALUE 1700     // Threshold for switch UP position
#define SWITCH_MID_VALUE 1500    // Threshold for switch MID position
// Below SWITCH_MID_VALUE is considered DOWN

// Buffer for incoming data
uint8_t dataBuffer[CRSF_FRAME_SIZE_MAX];
uint8_t crsfData[CRSF_FRAME_SIZE_MAX];
bool frameComplete = false;
uint8_t frameLength = 0;
int framePosition = 0;

// RC channel values
uint16_t rcChannels[16];
int activeUart = 2; // Default to UART2

// Function to print buffer in hex
void printHexBuffer(uint8_t* buffer, int length, String source) {
  Serial.print(source + " Data: ");
  for (int i = 0; i < length; i++) {
    if (buffer[i] < 16) Serial.print("0");
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// Function to decode CRSF channel data and check switch position
void decodeChannels(uint8_t* buffer) {
  // Check if we have a valid RC channels packet
  if (buffer[1] < 24 || buffer[2] != CRSF_FRAMETYPE_RC_CHANNELS_PACKED) {
    return; // Not a valid RC channels packet
  }

  // CRSF protocol packs 16 channels in 22 bytes using 11 bits per channel
  uint16_t channels[16];
  
  // Channel 1
  channels[0] = ((buffer[3] | buffer[4] << 8) & 0x07FF);
  // Channel 2
  channels[1] = ((buffer[4] >> 3 | buffer[5] << 5) & 0x07FF);
  // Channel 3
  channels[2] = ((buffer[5] >> 6 | buffer[6] << 2 | buffer[7] << 10) & 0x07FF);
  // Channel 4
  channels[3] = ((buffer[7] >> 1 | buffer[8] << 7) & 0x07FF);
  // Channel 5
  channels[4] = ((buffer[8] >> 4 | buffer[9] << 4) & 0x07FF);
  // Channel 6
  channels[5] = ((buffer[9] >> 7 | buffer[10] << 1 | buffer[11] << 9) & 0x07FF);
  // Channel 7
  channels[6] = ((buffer[11] >> 2 | buffer[12] << 6) & 0x07FF);
  // Channel 8
  channels[7] = ((buffer[12] >> 5 | buffer[13] << 3) & 0x07FF);
  
  // Map raw values to microseconds for easier understanding
  int switchValue = map(channels[SWITCH_SC_CHANNEL-1], 0, 1800, 1000, 2000);
  
  // Determine switch position and select UART
  int newUartSelection = activeUart; // Default to current selection
  
  if (switchValue >= SWITCH_UP_VALUE) {
    // Switch is UP - select UART1
    newUartSelection = 1;
  } else if (switchValue < SWITCH_MID_VALUE) {
    // Switch is DOWN - select UART2
    newUartSelection = 2;
  }
  
  // If the selection changed, update and print message
  if (newUartSelection != activeUart) {
    activeUart = newUartSelection;
    Serial.print("Switch SC Position Changed: UART");
    Serial.print(activeUart);
    Serial.println(" now active");
  }
  
  // Print the channel values
  Serial.println("RC Channels (raw 11-bit values):");
  for (int i = 0; i < 8; i++) {
    Serial.print("CH");
    Serial.print(i+1);
    Serial.print(": ");
    Serial.print(channels[i]);
    Serial.print(" (");
    
    // Convert to μs (typical range 1000-2000μs)
    int us = map(channels[i], 0, 1800, 1000, 2000);
    Serial.print(us);
    Serial.println("μs)");
  }
  
  // Highlight the switch channel
  Serial.print("Switch SC (CH");
  Serial.print(SWITCH_SC_CHANNEL);
  Serial.print("): ");
  Serial.print(switchValue);
  Serial.print("μs - Position: ");
  
  if (switchValue >= SWITCH_UP_VALUE) {
    Serial.println("UP (UART1)");
  } else if (switchValue < SWITCH_MID_VALUE) {
    Serial.println("DOWN (UART2)");
  } else {
    Serial.println("MID");
  }
}

// Function to parse incoming bytes for CRSF frames
void processByte(uint8_t byte) {
  static uint8_t buffer[CRSF_FRAME_SIZE_MAX];
  static uint8_t frameLength = 0;
  static uint8_t framePosition = 0;
  static bool inFrame = false;
  
  // Look for frame start
  if (!inFrame && byte == CRSF_ADDRESS_FLIGHT_CONTROLLER) {
    inFrame = true;
    framePosition = 0;
    buffer[framePosition++] = byte;
    return;
  }
  
  // If we're in a frame, store the byte
  if (inFrame) {
    buffer[framePosition++] = byte;
    
    // If this is the length byte (second byte)
    if (framePosition == 2) {
      frameLength = byte;
    }
    
    // If we've received all bytes in the frame
    if (framePosition >= frameLength + 2) {  // +2 for address and length
      inFrame = false;
      
      // Copy to global buffer and set flag
      memcpy(crsfData, buffer, framePosition);
      frameComplete = true;
    }
  }
}

// Function to send data to the currently active UART
void sendToActiveUart(uint8_t* data, int length) {
  if (activeUart == 1) {
    Serial1.write(data, length);
  } else {
    Serial2.write(data, length);
  }
}

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  Serial.println("ExpressLRS CRSF Decoder with Switch Selection");
  
  // Initialize both UART interfaces
  Serial1.begin(420000, SERIAL_8N1, RXD1, TXD1);
  Serial2.begin(420000, SERIAL_8N1, RXD2, TXD2);
  
  Serial.println("UART interfaces initialized");
  Serial.println("UART2 active by default - Waiting for ExpressLRS data...");
  Serial.println("Switch position will determine active UART");
}

void loop() {
  // Read from UART2 and process each byte for CRSF frames
  while (Serial2.available()) {
    uint8_t byte = Serial2.read();
    processByte(byte);
  }
  
  // If we have a complete frame, process it
  if (frameComplete) {
    printHexBuffer(crsfData, frameLength + 2, "CRSF");
    decodeChannels(crsfData);
    frameComplete = false;
  }
  
  delay(10); // Small delay for stability
}
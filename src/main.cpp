#include <Arduino.h>

// Define UART pins
#define RXD2 21  // GPIO21 (RX pin) - ExpressLRS input #1 (primary)
#define TXD2 22  // GPIO22 (TX pin)
#define RXD1 12  // GPIO12 - ExpressLRS input #2 (secondary)
#define TXD1 13  // GPIO13
// Flight controller UART
#define RXD0 3   // GPIO3 (RX0) for flight controller
#define TXD0 1   // GPIO1 (TX0) for flight controller

// Debug output UART (USB serial)
#define DEBUG_UART Serial
#define DEBUG_ENABLE  // Uncomment to enable debug

// CRSF Protocol defines
#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16
#define CRSF_FRAME_SIZE_MAX 64
#define CRSF_FRAME_START_BYTE 0xC8
#define CRSF_LQ_INDEX 4  // Byte position for Link Quality in CRSF link stats frame

// RC channel configuration
#define SWITCH_SC_CHANNEL 8  // Channel for manual switching
#define SWITCH_THRESHOLD 1500 // Threshold for switch position (adjust based on your radio)

// Signal quality thresholds
#define SIGNAL_QUALITY_THRESHOLD 30  // Minimum signal quality before switching (0-100%)
#define FAILSAFE_TIMEOUT 500  // Time in ms before considering signal lost

// Buffers for incoming data
uint8_t dataBuffer[CRSF_FRAME_SIZE_MAX];
uint8_t crsfData[CRSF_FRAME_SIZE_MAX];
int activeUart = 2; // Default to UART2 (primary)
bool autoSwitchEnabled = true;

// Variables for signal quality monitoring
unsigned long lastValidFrameTime1 = 0;
unsigned long lastValidFrameTime2 = 0;
uint8_t signalQuality1 = 0;  // 0-100%
uint8_t signalQuality2 = 0;  // 0-100%
uint16_t channelValues[16] = {0};  // Store decoded RC channel values

// Function declarations
bool parseCRSFPacket(uint8_t* data, int len, int uartId);
uint16_t unpackChannel(uint8_t* data, int channelIdx);
void switchUart(int uartNum);
bool processCRSFFrame(HardwareSerial &serial, uint8_t &signalQuality, unsigned long &lastFrameTime);

void setup() {
  // Initialize USB Serial for debugging
  #ifdef DEBUG_ENABLE
    DEBUG_UART.begin(115200);
    delay(100);
    DEBUG_UART.println("Starting ExpressLRS Diversity Bridge");
  #endif
  
  // Initialize UART for Flight Controller (UART0)
  Serial.begin(420000, SERIAL_8N1, RXD0, TXD0);
  
  // Initialize UART for primary ExpressLRS receiver
  Serial2.begin(420000, SERIAL_8N1, RXD2, TXD2);
  
  // Initialize UART for secondary ExpressLRS receiver
  Serial1.begin(420000, SERIAL_8N1, RXD1, TXD1);
  
  #ifdef DEBUG_ENABLE
    DEBUG_UART.println("UART interfaces initialized");
    DEBUG_UART.println("Waiting for ExpressLRS data...");
  #endif
}

void loop() {
  // Process data from primary receiver (UART2)
  bool receivedData2 = processCRSFFrame(Serial2, signalQuality1, lastValidFrameTime1);
  
  // Process data from secondary receiver (UART1)
  bool receivedData1 = processCRSFFrame(Serial1, signalQuality2, lastValidFrameTime2);
  
  // Check if we should switch based on signal quality
  unsigned long currentTime = millis();
  
  // Check for signal loss on active UART
  bool signalLost = false;
  if (activeUart == 2 && (currentTime - lastValidFrameTime1 > FAILSAFE_TIMEOUT)) {
    signalLost = true;
  } else if (activeUart == 1 && (currentTime - lastValidFrameTime2 > FAILSAFE_TIMEOUT)) {
    signalLost = true;
  }
  
  // Check manual switch position (if we have valid data)
  if (channelValues[SWITCH_SC_CHANNEL-1] > 0) {
    bool manualSwitchActive = channelValues[SWITCH_SC_CHANNEL-1] > SWITCH_THRESHOLD;
    
    // Manual switch to second receiver
    if (manualSwitchActive && activeUart != 1) {
      switchUart(1);
    } 
    // Manual switch to primary receiver
    else if (!manualSwitchActive && activeUart != 2) {
      switchUart(2);
    }
  }
  
  // Auto-switch on signal loss
  if (autoSwitchEnabled && signalLost) {
    if (activeUart == 2) {
      // Primary signal lost, check if secondary is available
      if (currentTime - lastValidFrameTime2 < FAILSAFE_TIMEOUT) {
        switchUart(1);
      }
    } else {
      // Secondary signal lost, check if primary is available
      if (currentTime - lastValidFrameTime1 < FAILSAFE_TIMEOUT) {
        switchUart(2);
      }
    }
  }
  
  // Forward data from Flight Controller to active receiver
  while (Serial.available()) {
    uint8_t byte = Serial.read();
    if (activeUart == 2) {
      Serial2.write(byte);
    } else {
      Serial1.write(byte);
    }
  }
}

// Enable debug for troubleshooting

// Update buffer handling for UART processing
bool processCRSFFrame(HardwareSerial &serial, uint8_t &signalQuality, unsigned long &lastFrameTime) {
  static int bufPos1 = 0, bufPos2 = 0;
  static uint8_t buffer1[CRSF_FRAME_SIZE_MAX], buffer2[CRSF_FRAME_SIZE_MAX];
  
  int* bufPosPtr;
  uint8_t* bufferPtr;
  
  // Determine which UART this is and select corresponding buffer
  int uartId = (&serial == &Serial2) ? 2 : 1;
  if (uartId == 2) {
    bufPosPtr = &bufPos2;
    bufferPtr = buffer2;
  } else {
    bufPosPtr = &bufPos1;
    bufferPtr = buffer1;
  }
  
  bool frameProcessed = false;
  
  while (serial.available()) {
    uint8_t byte = serial.read();
    
    // Forward data to flight controller if this is the active UART
    if (&serial == (activeUart == 2 ? &Serial2 : &Serial1)) {
      Serial.write(byte);
    }
    
    // Process the byte for CRSF frame parsing
    if (*bufPosPtr == 0 && byte != CRSF_FRAME_START_BYTE) {
      // Not a start byte, skip
      continue;
    }
    
    bufferPtr[*bufPosPtr] = byte;
    (*bufPosPtr)++;
    
    // Check if we have a valid frame length
    if (*bufPosPtr == 2) {
      if (bufferPtr[1] > CRSF_FRAME_SIZE_MAX || bufferPtr[1] < 3) {
        // Invalid length, reset buffer
        *bufPosPtr = 0;
        continue;
      }
    }
    
    // Check if we have a complete frame
    if (*bufPosPtr > 2 && *bufPosPtr >= bufferPtr[1] + 2) {
      // We have a complete frame, process it
      if (parseCRSFPacket(bufferPtr, *bufPosPtr, uartId)) {
        lastFrameTime = millis();
        frameProcessed = true;
        
        #ifdef DEBUG_ENABLE
          if (bufferPtr[2] == CRSF_FRAMETYPE_RC_CHANNELS_PACKED) {
            DEBUG_UART.print("RX");
            DEBUG_UART.print(uartId);
            DEBUG_UART.print(" CH8=");
            DEBUG_UART.println(channelValues[SWITCH_SC_CHANNEL-1]);
          }
          if (bufferPtr[2] == 0x14) {  // Link stats
            DEBUG_UART.print("RX");
            DEBUG_UART.print(uartId);
            DEBUG_UART.print(" LQ=");
            DEBUG_UART.println(uartId == 2 ? signalQuality1 : signalQuality2);
          }
        #endif
      }
      *bufPosPtr = 0;
    }
    
    // Buffer overflow protection
    if (*bufPosPtr >= CRSF_FRAME_SIZE_MAX) {
      *bufPosPtr = 0;
    }
  }
  
  return frameProcessed;
}

// Parse a CRSF packet and extract channel data
bool parseCRSFPacket(uint8_t* data, int len, int uartId) {
  // Basic validation
  if (len < 4) return false;
  
  // Check packet type - RC channels packet
  if (data[2] == CRSF_FRAMETYPE_RC_CHANNELS_PACKED && len >= 26) {
    // Extract channel data (11 bit per channel, packed)
    for (int i = 0; i < 16; i++) {
      channelValues[i] = unpackChannel(data + 3, i);
    }
    return true;
  }
  
  // Link statistics packet - extract signal quality
  if (data[2] == 0x14 && len >= 10) { 
    // This is a link statistics packet, extract LQ
    // Use uartId to determine which signal quality to update
    if (uartId == 2) {
      signalQuality1 = data[CRSF_LQ_INDEX];
    } else {
      signalQuality2 = data[CRSF_LQ_INDEX];
    }
    return true;
  }
  
  return false;
}

// More accurate channel unpacking for CRSF
uint16_t unpackChannel(uint8_t* data, int channelIdx) {
  // CRSF channels use 11 bits per channel, packed
  uint32_t channelData;
  uint8_t* payload = data;
  
  if (channelIdx <= 7) {
    // First 8 channels
    const uint8_t* const payloadPtr = payload;
    // Calculate index within the array
    const uint8_t byteIdx = channelIdx / 4 * 5 + channelIdx % 4;
    uint16_t value;
    
    switch (channelIdx % 4) {
      case 0:
        value = (payloadPtr[byteIdx] >> 0) | ((payloadPtr[byteIdx + 1] & 0x07) << 8);
        break;
      case 1:
        value = (payloadPtr[byteIdx] >> 3) | ((payloadPtr[byteIdx + 1] & 0x3F) << 5);
        break;
      case 2:
        value = (payloadPtr[byteIdx] >> 6) | ((payloadPtr[byteIdx + 1] & 0xFF) << 2) | 
                ((payloadPtr[byteIdx + 2] & 0x01) << 10);
        break;
      case 3:
        value = (payloadPtr[byteIdx] >> 1) | ((payloadPtr[byteIdx + 1] & 0x0F) << 7);
        break;
      default:
        value = 0;
        break;
    }
    
    return value;
    
  } else if (channelIdx <= 15) {
    // Channels 8-16
    channelIdx -= 8;
    const uint8_t* const payloadPtr = payload + 5;
    // Calculate index within the array
    const uint8_t byteIdx = channelIdx / 4 * 5 + channelIdx % 4;
    uint16_t value;
    
    switch (channelIdx % 4) {
      case 0:
        value = (payloadPtr[byteIdx] >> 0) | ((payloadPtr[byteIdx + 1] & 0x07) << 8);
        break;
      case 1:
        value = (payloadPtr[byteIdx] >> 3) | ((payloadPtr[byteIdx + 1] & 0x3F) << 5);
        break;
      case 2:
        value = (payloadPtr[byteIdx] >> 6) | ((payloadPtr[byteIdx + 1] & 0xFF) << 2) | 
                ((payloadPtr[byteIdx + 2] & 0x01) << 10);
        break;
      case 3:
        value = (payloadPtr[byteIdx] >> 1) | ((payloadPtr[byteIdx + 1] & 0x0F) << 7);
        break;
      default:
        value = 0;
        break;
    }
    
    return value;
  }
  
  return 0;
}

// Switch function with more debug information
void switchUart(int uartNum) {
  if (activeUart == uartNum) return;
  
  activeUart = uartNum;
  
  #ifdef DEBUG_ENABLE
    DEBUG_UART.print("\n*** SWITCHING TO UART");
    DEBUG_UART.print(activeUart);
    DEBUG_UART.println(" ***");
    DEBUG_UART.print("Signal Quality 1: ");
    DEBUG_UART.print(signalQuality1);
    DEBUG_UART.print("%, Signal Quality 2: ");
    DEBUG_UART.print(signalQuality2);
    DEBUG_UART.println("%");
    DEBUG_UART.print("CH8 value: ");
    DEBUG_UART.println(channelValues[SWITCH_SC_CHANNEL-1]);
  #endif
}
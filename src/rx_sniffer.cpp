#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// Pin definitions for TTGO LORA32 T3_V1.6.1
#define SCLK     5     
#define MISO    19    
#define MOSI    27    
#define CS      18    
#define RST     23    
#define DIO0    26    
#define DIO1    33

// Built-in LED for activity indication
#define LED_PIN 25   

// Radio configuration
#define FREQUENCY       433.0
#define FSK_BIT_RATE    4.8
#define FSK_FREQ_DEV    5.0
#define RX_BANDWIDTH    125.0

// Initialize SX1278 module
SX1278 radio = new Module(CS, DIO0, RST, DIO1);

// Variables for sniffing
unsigned long packetCount = 0;
unsigned long lastStatusTime = 0;
unsigned long statusInterval = 5000; // Status display interval in ms
bool verboseMode = true;
bool hexDumpMode = false;
bool autoRestartRx = true;

void setup() {
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize serial connection
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n--- TTGO LORA32 T3_V1.6.1 FSK/OOK Packet Sniffer Starting ---");
  
  // Print pin configuration
  Serial.println("Radio module pin configuration:");
  Serial.printf("SCLK: %d, MISO: %d, MOSI: %d, CS: %d, RST: %d, DIO0: %d, DIO1: %d\n", 
                SCLK, MISO, MOSI, CS, RST, DIO0, DIO1);
  
  // Set up SPI
  Serial.println("Setting up SPI...");
  SPI.begin(SCLK, MISO, MOSI, CS);
  
  // Initialize radio in FSK mode
  Serial.printf("Initializing radio module at frequency: %.2f MHz...\n", FREQUENCY);
  
  // Start in FSK mode
  int state = radio.beginFSK(FREQUENCY, FSK_BIT_RATE, FSK_FREQ_DEV, RX_BANDWIDTH);
  if (state == RADIOLIB_ERR_NONE) {  
    Serial.println("Radio module initialized successfully!");
  } else {
    Serial.printf("Radio module initialization failed, error code: %d\n", state);
    while (true) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  // Optional: Enable OOK mode for specific devices that use it
  // state = radio.setOOK(true);
  // if (state == RADIOLIB_ERR_NONE) {
  //   Serial.println("OOK mode enabled!");
  // } else {
  //   Serial.printf("Error enabling OOK mode, error code: %d\n", state);
  // }
  
  // Print configuration
  Serial.println("Configuration:");
  Serial.printf("- Frequency: %.2f MHz\n", FREQUENCY);
  Serial.printf("- Bit rate: %.1f kbps\n", FSK_BIT_RATE);
  Serial.printf("- Frequency deviation: %.1f kHz\n", FSK_FREQ_DEV);
  Serial.printf("- RX bandwidth: %.1f kHz\n", RX_BANDWIDTH);
  
  // Blink LED to indicate ready state
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
  
  Serial.println("\n--- Available Commands ---");
  Serial.println("v - toggle verbose mode");
  Serial.println("x - toggle hex dump mode");
  Serial.println("a - toggle auto restart RX");
  Serial.println("r - restart receiver");
  Serial.println("c - clear packet counter");
  Serial.println("-------------------------\n");
  
  // Start receiver
  Serial.println("Starting receiver...");
  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Receiver started successfully!");
    Serial.println("Listening for packets...");
  } else {
    Serial.printf("Failed to start receiver, error code: %d\n", state);
  }
}

void hexDump(uint8_t* data, size_t length) {
  char temp[16];
  for (size_t i = 0; i < length; i++) {
    sprintf(temp, "%02X ", data[i]);
    Serial.print(temp);
    if ((i + 1) % 16 == 0) {
      Serial.println();
    }
  }
  Serial.println();
}

void processReceivedData() {
  digitalWrite(LED_PIN, HIGH);
  
  // Check if new packet is available
  if (radio.available()) {
    // Get packet size
    size_t packetLength = radio.getPacketLength();
    
    // Create a buffer for the packet
    uint8_t* packet = new uint8_t[packetLength];
    
    // Read the packet
    int state = radio.readData(packet, packetLength);
    
    if (state == RADIOLIB_ERR_NONE) {
      // Increment counter
      packetCount++;
      
      // Display packet data
      Serial.printf("\n[%lu] Packet received (RSSI: %.1f dBm): %d bytes\n", 
                   packetCount, radio.getRSSI(), packetLength);
      
      // Try to display as string (if it's text)
      if (verboseMode) {
        Serial.print("Data (ASCII): ");
        for (size_t i = 0; i < packetLength; i++) {
          if (packet[i] >= 32 && packet[i] <= 126) {
            Serial.write(packet[i]);
          } else {
            Serial.print("·"); // Non-printable character
          }
        }
        Serial.println();
      }
      
      // Hex dump
      if (hexDumpMode) {
        Serial.println("Data (HEX):");
        hexDump(packet, packetLength);
      }
      
      Serial.println("------------------------------");
    } else {
      Serial.printf("Reception error, code: %d\n", state);
    }
    
    // Free memory
    delete[] packet;
    
    // Restart reception if auto restart is enabled
    if (autoRestartRx) {
      radio.startReceive();
    }
  }
  
  digitalWrite(LED_PIN, LOW);
}

void processSerialCommand() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case 'v': {
        verboseMode = !verboseMode;
        Serial.printf("Verbose mode: %s\n", verboseMode ? "ON" : "OFF");
        break;
      }
      case 'x': {
        hexDumpMode = !hexDumpMode;
        Serial.printf("Hex dump mode: %s\n", hexDumpMode ? "ON" : "OFF");
        break;
      }
      case 'a': {
        autoRestartRx = !autoRestartRx;
        Serial.printf("Auto restart RX: %s\n", autoRestartRx ? "ON" : "OFF");
        break;
      }
      case 'r': {
        Serial.println("Restarting receiver...");
        int state = radio.startReceive();
        if (state == RADIOLIB_ERR_NONE) {
          Serial.println("Receiver restarted!");
        } else {
          Serial.printf("Failed to restart receiver, error code: %d\n", state);
        }
        break;
      }
      case 'c': {
        packetCount = 0;
        Serial.println("Packet counter cleared");
        break;
      }
    }
    
    // Clear buffer
    while(Serial.available()) {
      Serial.read();
    }
  }
}

void printStatus() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastStatusTime >= statusInterval) {
    Serial.printf("Status: Listening... [%lu packets received]\n", packetCount);
    lastStatusTime = currentTime;
  }
}

void loop() {
  // Process commands from serial port
  processSerialCommand();
  
  // Check for received data
  processReceivedData();
  
  // Print periodic status
  printStatus();
  
  delay(10);
}
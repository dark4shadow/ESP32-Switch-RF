#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin definitions for TTGO LORA32 T3_V1.6.1
#define SCLK     5     
#define MISO    19    
#define MOSI    27    
#define CS      18    
#define RST     23    
#define DIO0    26    
#define DIO1    33

// OLED display pins and parameters
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin not used on TTGO LoRa32
#define SCREEN_ADDRESS 0x3C  // Common I2C address for SSD1306

// Built-in LED for activity indication
#define LED_PIN 25   

// Radio configuration
#define FREQUENCY       433.0
#define FSK_BIT_RATE    4.8
#define FSK_FREQ_DEV    5.0
#define RX_BANDWIDTH    125.0

// Initialize SX1278 module
SX1278 radio = new Module(CS, DIO0, RST, DIO1);

// Initialize OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Variables for sniffing
unsigned long packetCount = 0;
unsigned long lastStatusTime = 0;
unsigned long statusInterval = 5000; // Status display interval in ms
unsigned long lastDisplayUpdateTime = 0;
unsigned long displayUpdateInterval = 1000; // Display update interval in ms
bool verboseMode = true;
bool hexDumpMode = false;
bool autoRestartRx = true;
float lastRssi = 0;


// Last packet storage
uint8_t lastPacket[64]; // Buffer to store last packet (adjust size as needed)
size_t lastPacketLength = 0;
unsigned long lastPacketTime = 0;
bool displayingLastPacket = false;
const unsigned long LAST_PACKET_DISPLAY_TIMEOUT = 3000; // ms before showing last packet

void displayInit() {
  Wire.begin(OLED_SDA, OLED_SCL);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    return;
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("RF Sniffer Starting..."));
  display.println(F("Freq: 433.0 MHz"));
  display.display();
  delay(2000);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // Header with basic info
  display.println("RF Packet Sniffer");
  display.println("----------------");
  
  // Display frequency
  display.print("Freq: ");
  display.print(FREQUENCY, 1);
  display.println(" MHz");
  
  // Display packet count
  display.print("Packets: ");
  display.println(packetCount);
  
  // Display last RSSI if packets received
  if (packetCount > 0) {
    display.print("Last RSSI: ");
    display.print(lastRssi, 1);
    display.println(" dBm");
  }
  
  // Show current mode
  display.println(autoRestartRx ? "Mode: Listening" : "Mode: Paused");
  
  display.display();
}

void displayPacketInfo(size_t packetLength, float rssi, uint8_t* packet) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // Display packet info header
  display.println("Packet Received!");
  
  // Packet number and size
  display.print("#");
  display.print(packetCount);
  display.print(" (");
  display.print(packetLength);
  display.print("B) RSSI:");
  display.print(rssi, 0);
  display.println("dBm");
  display.println("----------------");
  
  // Display packet data (up to 16 bytes)
  int bytesToShow = min(16, (int)packetLength);
  
  // HEX display of data
  display.print("HEX: ");
  int linePos = 5;
  for (int i = 0; i < bytesToShow; i++) {
    if (linePos > 15) {  // Start a new line if needed
      display.println();
      display.print("     ");
      linePos = 5;
    }
    
    if (packet[i] < 16) {
      display.print("0");  // Add leading zero for values < 0x10
    }
    display.print(packet[i], HEX);
    display.print(" ");
    linePos += 3;
  }
  
  display.println();
  
  // ASCII display of data
  display.print("ASCII: ");
  for (int i = 0; i < bytesToShow; i++) {
    if (packet[i] >= 32 && packet[i] <= 126) {
      display.write(packet[i]);
    } else {
      display.print("·"); // Non-printable character
    }
  }
  
  display.display();
}

void setup() {
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize serial connection
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n--- TTGO LORA32 T3_V1.6.1 FSK/OOK Packet Sniffer Starting ---");
  
  // Initialize OLED display
  displayInit();
  
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
    updateDisplay();
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
      
      // Get RSSI
      lastRssi = radio.getRSSI();
      
      // Store last packet data
      lastPacketLength = min(packetLength, sizeof(lastPacket));
      memcpy(lastPacket, packet, lastPacketLength);
      lastPacketTime = millis();
      displayingLastPacket = false; // Reset display mode
      
      // Display packet data
      Serial.printf("\n[%lu] Packet received (RSSI: %.1f dBm): %d bytes\n", 
                   packetCount, lastRssi, packetLength);
      
      // Update OLED with packet info
      displayPacketInfo(packetLength, lastRssi, packet);
      
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
        updateDisplay(); // Update display to show current mode
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
        updateDisplay(); // Update display to show cleared counter
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
  
  // Display logic with switching
  if (lastPacketLength > 0 && autoRestartRx) {
    // Check if we should switch to last packet display
    if (!displayingLastPacket && 
        (currentTime - lastPacketTime >= LAST_PACKET_DISPLAY_TIMEOUT) &&
        (currentTime - lastDisplayUpdateTime >= displayUpdateInterval)) {
      // Switch to last packet display
      displayPacketInfo(lastPacketLength, lastRssi, lastPacket);
      displayingLastPacket = true;
      lastDisplayUpdateTime = currentTime;
      Serial.println("Showing last packet details on display");
    }
    // Check if we should switch back to scanning display
    else if (displayingLastPacket && 
             (currentTime - lastDisplayUpdateTime >= 3000)) { // Switch back every 10 seconds
      updateDisplay();
      displayingLastPacket = false;
      lastDisplayUpdateTime = currentTime;
    }
    // Regular update when not displaying last packet
    else if (!displayingLastPacket && 
             (currentTime - lastDisplayUpdateTime >= displayUpdateInterval)) {
      updateDisplay();
      lastDisplayUpdateTime = currentTime;
    }
  } else {
    // Default behavior when no packets received or auto-restart is off
    if (currentTime - lastDisplayUpdateTime >= displayUpdateInterval) {
      updateDisplay();
      lastDisplayUpdateTime = currentTime;
    }
  }
}

void loop() {
  processSerialCommand();
  processReceivedData();
  // Print periodic status
  printStatus();
  delay(10);
}
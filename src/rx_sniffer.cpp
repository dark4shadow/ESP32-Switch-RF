#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin definitions for TTGO LORA32 T3_V1.6.1
#define SCLK         5     
#define MISO        19    
#define MOSI        27    
#define CS          18    
#define RAD_RST     23    //radio reset pin
#define DIO0        26    
#define DIO1        33

// Button pin (using the RST pin as a button input)
#define BUTTON_PIN  RST

// OLED display pins and parameters - renamed to avoid conflicts
#define CUSTOM_OLED_SDA 21
#define CUSTOM_OLED_SCL 22
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

// For spectrum analyzer
#define SCAN_START_FREQ 430.0
#define SCAN_STOP_FREQ  435.0
#define SCAN_STEP       0.2
#define NUM_SAMPLES     ((SCAN_STOP_FREQ - SCAN_START_FREQ) / SCAN_STEP + 1)

// Operating modes
enum OperatingMode {
  MODE_SNIFFER = 0,
  MODE_SPECTRUM = 1,
  MODE_DECODER = 2,
  MODE_SETTINGS = 3,
  NUM_MODES = 4
};

// Initialize SX1278 module
SX1278 radio = new Module(CS, DIO0, RAD_RST, DIO1);

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

// Button handling variables
unsigned long buttonPressTime = 0;
bool buttonPressed = false;
#define LONG_PRESS_TIME 1000
#define DEBOUNCE_TIME 200
unsigned long lastButtonTime = 0;

// Mode handling
OperatingMode currentMode = MODE_SNIFFER;
const char* modeNames[] = {
  "Packet Sniffer",
  "Spectrum Analyzer",
  "Protocol Decoder",
  "Settings"
};

// For spectrum analyzer
float spectrumData[int(NUM_SAMPLES)];
float currentFreq = FREQUENCY;
unsigned long lastScanTime = 0;
#define SCAN_INTERVAL 100 // ms between frequency scans

// For protocol decoder
struct DecodedProtocol {
  const char* name;
  bool detected;
  uint32_t code;
};

DecodedProtocol protocols[] = {
  { "PT2262", false, 0 },
  { "HT12E", false, 0 },
  { "SC5262", false, 0 },
  { "EV1527", false, 0 }
};
#define NUM_PROTOCOLS 4

// Settings variables
int settingsIndex = 0;
const char* settingsNames[] = {
  "Frequency",
  "Bit Rate",
  "Bandwidth",
  "Reset All"
};
#define NUM_SETTINGS 4
bool editingSettings = false;

void displayInit() {
  // Use custom pins for OLED
  Wire.begin(CUSTOM_OLED_SDA, CUSTOM_OLED_SCL);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    return;
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("RF Multi-Tool"));
  display.println(F("Starting..."));
  display.println(F("Freq: 433.0 MHz"));
  display.println(F("Press button to"));
  display.println(F("switch modes"));
  display.display();
  delay(2000);
}

void drawModeHeader() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(modeNames[currentMode]);
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    display.drawPixel(i, 10, SSD1306_WHITE);
  }
  display.setCursor(0, 12);
}

void updateSnifferDisplay() {
  drawModeHeader();
  
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
  display.print("Listen mode: ");
  display.println(autoRestartRx ? "ON" : "OFF");
  
  display.display();
}

void updateSpectrumDisplay() {
  drawModeHeader();
  
  // Display scanning range
  display.print("Scanning ");
  display.print(SCAN_START_FREQ);
  display.print("-");
  display.print(SCAN_STOP_FREQ);
  display.println("MHz");
  
  // Draw spectrum graph
  int graphHeight = 30;
  int graphTop = 22;
  int graphWidth = SCREEN_WIDTH;
  
  // Draw axes
  display.drawLine(0, graphTop + graphHeight, graphWidth, graphTop + graphHeight, SSD1306_WHITE);
  display.drawLine(0, graphTop, 0, graphTop + graphHeight, SSD1306_WHITE);
  
  // Draw spectrum data
  for (int i = 0; i < (graphWidth < int(NUM_SAMPLES) ? graphWidth : int(NUM_SAMPLES)); i++) {
    int sampleIndex = i * NUM_SAMPLES / graphWidth;
    // Normalize RSSI (-120 to -40 dBm typical range)
    float normalizedRSSI = (spectrumData[sampleIndex] + 120) / 80.0f;
    if (normalizedRSSI < 0.0f) normalizedRSSI = 0.0f;
    if (normalizedRSSI > 1.0f) normalizedRSSI = 1.0f;
    int barHeight = normalizedRSSI * graphHeight;
    
    if (barHeight > 0) {
      display.drawLine(i, graphTop + graphHeight - barHeight, i, graphTop + graphHeight, SSD1306_WHITE);
    }
  }
  
  // Show current frequency
  display.setCursor(0, 56);
  display.print("Current: ");
  display.print(currentFreq, 1);
  display.print("MHz");
  
  display.display();
}

void updateDecoderDisplay() {
  drawModeHeader();
  
  display.println("Detecting protocols:");
  
  int y = 22;
  for (int i = 0; i < NUM_PROTOCOLS; i++) {
    display.setCursor(0, y);
    display.print(protocols[i].name);
    display.print(": ");
    
    if (protocols[i].detected) {
      display.print("0x");
      display.print(protocols[i].code, HEX);
    } else {
      display.print("Not detected");
    }
    
    y += 10;
  }
  
  display.display();
}

void updateSettingsDisplay() {
  drawModeHeader();
  
  display.println(editingSettings ? "Editing:" : "Select setting:");
  
  for (int i = 0; i < NUM_SETTINGS; i++) {
    // Highlight selected setting
    if (i == settingsIndex) {
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    
    display.print(settingsNames[i]);
    
    // Show current value
    display.print(": ");
    switch (i) {
      case 0: // Frequency
        display.print(FREQUENCY);
        display.print(" MHz");
        break;
      case 1: // Bit Rate
        display.print(FSK_BIT_RATE);
        display.print(" kbps");
        break;
      case 2: // Bandwidth
        display.print(RX_BANDWIDTH);
        display.print(" kHz");
        break;
      case 3: // Reset All
        display.print("[Press to confirm]");
        break;
    }
    
    display.println();
  }
  
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void updateDisplay() {
  switch (currentMode) {
    case MODE_SNIFFER:
      updateSnifferDisplay();
      break;
    case MODE_SPECTRUM:
      updateSpectrumDisplay();
      break;
    case MODE_DECODER:
      updateDecoderDisplay();
      break;
    case MODE_SETTINGS:
      updateSettingsDisplay();
      break;
  }
}

void displayPacketInfo(size_t packetLength, float rssi) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // Display packet info header
  display.println("Packet Received!");
  display.println("----------------");
  
  // Packet number and size
  display.print("# ");
  display.println(packetCount);
  display.print("Size: ");
  display.print(packetLength);
  display.println(" bytes");
  
  // RSSI value
  display.print("RSSI: ");
  display.print(rssi, 1);
  display.println(" dBm");
  
  // Show the first few bytes in hex
  display.print("Data: ");
  // Note: We'll show packet data in processReceivedData
  
  display.display();
}

void switchToMode(OperatingMode newMode) {
  // Clean up previous mode
  switch (currentMode) {
    case MODE_SNIFFER:
      // Nothing special to clean up
      break;
    case MODE_SPECTRUM:
      // Stop scan and return to fixed frequency
      radio.setFrequency(FREQUENCY);
      break;
    case MODE_DECODER:
      // Reset protocol detection flags
      for (int i = 0; i < NUM_PROTOCOLS; i++) {
        protocols[i].detected = false;
        protocols[i].code = 0;
      }
      break;
    case MODE_SETTINGS:
      settingsIndex = 0;
      editingSettings = false;
      break;
  }
  
  // Set up new mode
  currentMode = newMode;
  
  Serial.print("Switched to mode: ");
  Serial.println(modeNames[currentMode]);
  
  // Initialize the new mode
  switch (currentMode) {
    case MODE_SNIFFER:
      radio.setFrequency(FREQUENCY);
      radio.startReceive();
      break;
    case MODE_SPECTRUM:
      // Initialize spectrum analyzer data
      for (int i = 0; i < NUM_SAMPLES; i++) {
        spectrumData[i] = -120.0; // Minimum RSSI
      }
      currentFreq = SCAN_START_FREQ;
      break;
    case MODE_DECODER:
      radio.setFrequency(FREQUENCY);
      radio.startReceive();
      break;
    case MODE_SETTINGS:
      // Nothing special to initialize
      break;
  }
  
  // Update display for new mode
  updateDisplay();
}

void handleButton() {
  // Read button state
  int buttonState = digitalRead(BUTTON_PIN);
  unsigned long currentTime = millis();
  
  // Button press detected
  if (buttonState == LOW) {
    // If button was previously not pressed, record time
    if (!buttonPressed && (currentTime - lastButtonTime > DEBOUNCE_TIME)) {
      buttonPressed = true;
      buttonPressTime = currentTime;
    }
  } 
  // Button release detected
  else if (buttonPressed) {
    buttonPressed = false;
    lastButtonTime = currentTime;
    
    // Determine if it was a short or long press
    unsigned long pressDuration = currentTime - buttonPressTime;
    
    if (pressDuration < LONG_PRESS_TIME) {
      // Short press - switch mode
      OperatingMode newMode = static_cast<OperatingMode>((currentMode + 1) % NUM_MODES);
      switchToMode(newMode);
    } else {
      // Long press - action depends on current mode
      switch (currentMode) {
        case MODE_SNIFFER:
          // Toggle auto restart
          autoRestartRx = !autoRestartRx;
          if (autoRestartRx) {
            radio.startReceive();
          }
          break;
        case MODE_SPECTRUM:
          // Reset spectrum data
          for (int i = 0; i < NUM_SAMPLES; i++) {
            spectrumData[i] = -120.0;
          }
          break;
        case MODE_DECODER:
          // Reset detected protocols
          for (int i = 0; i < NUM_PROTOCOLS; i++) {
            protocols[i].detected = false;
            protocols[i].code = 0;
          }
          break;
        case MODE_SETTINGS:
          if (editingSettings) {
            // Exit edit mode
            editingSettings = false;
          } else {
            // Enter edit mode for selected setting
            editingSettings = true;
          }
          break;
      }
      updateDisplay();
    }
  }
}

void setup() {
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize serial connection
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n--- TTGO LORA32 T3_V1.6.1 RF Multi-Tool Starting ---");
  
  // Initialize OLED display
  displayInit();
  
  // Print pin configuration
  Serial.println("Radio module pin configuration:");
  Serial.printf("SCLK: %d, MISO: %d, MOSI: %d, CS: %d, RST: %d, DIO0: %d, DIO1: %d\n", 
                SCLK, MISO, MOSI, CS, RAD_RST, DIO0, DIO1);
  
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
  Serial.println("m - switch mode");
  Serial.println("-------------------------\n");
  
  // Initialize spectrum data
  for (int i = 0; i < NUM_SAMPLES; i++) {
    spectrumData[i] = -120.0;
  }
  
  // Start receiver for initial sniffer mode
  Serial.println("Starting in Sniffer mode...");
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
  if (currentMode != MODE_SNIFFER && currentMode != MODE_DECODER) {
    return; // Only process received data in sniffer and decoder modes
  }
  
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
      
      if (currentMode == MODE_SNIFFER) {
        // Display packet data in sniffer mode
        Serial.printf("\n[%lu] Packet received (RSSI: %.1f dBm): %d bytes\n", 
                    packetCount, lastRssi, packetLength);
        
        // Update OLED with packet info
        displayPacketInfo(packetLength, lastRssi);
        
        // Show first few bytes on OLED - Fix type mismatch
        display.setCursor(0, 48);
        size_t bytesToShow = (packetLength < 8) ? packetLength : 8;
        for (size_t i = 0; i < bytesToShow; i++) {
          char temp[4];
          sprintf(temp, "%02X ", packet[i]);
          display.print(temp);
        }
        if (packetLength > 8) {
          display.print("...");
        }
        display.display();
        
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
      }
      else if (currentMode == MODE_DECODER) {
        // Simple pattern matching for common protocols
        
        // Check for PT2262/EV1527 pattern (24-bit code)
        if (packetLength == 3) {
          uint32_t code = (packet[0] << 16) | (packet[1] << 8) | packet[2];
          protocols[0].detected = true;
          protocols[0].code = code;
          protocols[3].detected = true; // EV1527 often has similar format
          protocols[3].code = code;
        }
        
        // Check for HT12E pattern (12-bit address + 4-bit data)
        if (packetLength == 2) {
          uint32_t code = (packet[0] << 8) | packet[1];
          protocols[1].detected = true;
          protocols[1].code = code;
        }
        
        // SC5262 pattern check (similar to PT2262 but different encoding)
        if (packetLength >= 3) {
          uint32_t code = 0;
          // Fix min() function with proper type casting
          int bytesToProcess = (packetLength < 4) ? (int)packetLength : 4;
          for (int i = 0; i < bytesToProcess; i++) {
            code = (code << 8) | packet[i];
          }
          protocols[2].detected = true;
          protocols[2].code = code;
        }
        
        updateDisplay(); // Update with detected protocols
      }
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

void runSpectrumScan() {
  if (currentMode != MODE_SPECTRUM) {
    return; // Only run spectrum scan in spectrum analyzer mode
  }
  
  unsigned long currentTime = millis();
  
  // Time to scan next frequency
  if (currentTime - lastScanTime >= SCAN_INTERVAL) {
    lastScanTime = currentTime;
    
    // Set radio to current frequency
    radio.setFrequency(currentFreq);
    
    // Get RSSI at this frequency
    float rssi = radio.getRSSI();
    
    // Store in spectrum data
    int index = round((currentFreq - SCAN_START_FREQ) / SCAN_STEP);
    if (index >= 0 && index < NUM_SAMPLES) {
      // Use running average to smooth readings
      spectrumData[index] = (spectrumData[index] * 0.7) + (rssi * 0.3);
    }
    
    // Move to next frequency
    currentFreq += SCAN_STEP;
    if (currentFreq > SCAN_STOP_FREQ) {
      currentFreq = SCAN_START_FREQ;
      // Update display after full scan
      updateDisplay();
    }
  }
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
      case 'm': {
        OperatingMode newMode = static_cast<OperatingMode>((currentMode + 1) % NUM_MODES);
        switchToMode(newMode);
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
  if (currentMode != MODE_SNIFFER) {
    return; // Only print periodic status in sniffer mode
  }
  
  unsigned long currentTime = millis();
  
  if (currentTime - lastStatusTime >= statusInterval) {
    Serial.printf("Status: Listening... [%lu packets received]\n", packetCount);
    lastStatusTime = currentTime;
  }
  
  // Update display periodically
  if (currentTime - lastDisplayUpdateTime >= displayUpdateInterval) {
    updateDisplay();
    lastDisplayUpdateTime = currentTime;
  }
}

void loop() {
  handleButton(); // Check for button press/release
  processSerialCommand();
  
  switch (currentMode) {
    case MODE_SNIFFER:
      processReceivedData();
      printStatus();
      break;
      
    case MODE_SPECTRUM:
      runSpectrumScan();
      break;
      
    case MODE_DECODER:
      processReceivedData();
      // Update less frequently in decoder mode
      if (millis() - lastDisplayUpdateTime >= displayUpdateInterval * 2) {
        updateDisplay();
        lastDisplayUpdateTime = millis();
      }
      break;
      
    case MODE_SETTINGS:
      // Settings mode is passive, just wait for button input
      break;
  }
  
  delay(10);
}
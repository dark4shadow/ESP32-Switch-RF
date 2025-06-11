#include <Arduino.h>

// Define UART2 pins (existing)
#define RXD2 21  // GPIO21 (RX pin on the board)
#define TXD2 22  // GPIO22 (TX pin on the board)

// Define UART1 pins (new)
#define RXD1 12  // GPIO12 (using a free GPIO pin)
#define TXD1 13  // GPIO13 (using a free GPIO pin)

// Buffer for incoming data
#define MAX_BUFFER 64
uint8_t dataBuffer[MAX_BUFFER];

void printHexBuffer(uint8_t* buffer, int length, String source) {
  Serial.print(source + " Data: ");
  for (int i = 0; i < length; i++) {
    // Print hex value with leading zero if needed
    if (buffer[i] < 16) Serial.print("0");
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Also print as ASCII where possible
  Serial.print(source + " ASCII: ");
  for (int i = 0; i < length; i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) {
      Serial.print((char)buffer[i]);
    } else {
      Serial.print(".");
    }
  }
  Serial.println("\n");
}

void setup() {
  // Initialize Serial Monitor (USB connection to computer)
  Serial.begin(115200);
  Serial.println("UART Reception Test");
  
  // Initialize UART2
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); // Try 115200 for RC controller
  
  // Initialize UART1
  Serial1.begin(9600, SERIAL_8N1, RXD1, TXD1);
  
  Serial.println("UART1 and UART2 initialized");
}

void loop() {
  // Check if data is available on UART2 (RadioMaster TX12)
  if (Serial2.available() > 0) {
    int bytesAvailable = Serial2.available();
    int bytesToRead = min(bytesAvailable, MAX_BUFFER);
    
    // Read the bytes into our buffer
    int bytesRead = Serial2.readBytes(dataBuffer, bytesToRead);
    
    // Print the buffer contents
    printHexBuffer(dataBuffer, bytesRead, "UART2");
    delay(100); // Give some time between readings
  }
  
  // Check if data is available on UART1
  if (Serial1.available() > 0) {
    int bytesAvailable = Serial1.available();
    int bytesToRead = min(bytesAvailable, MAX_BUFFER);
    
    // Read the bytes into our buffer
    int bytesRead = Serial1.readBytes(dataBuffer, bytesToRead);
    
    // Print the buffer contents
    printHexBuffer(dataBuffer, bytesRead, "UART1");
  }
  
  delay(10); // Small delay for stability
}
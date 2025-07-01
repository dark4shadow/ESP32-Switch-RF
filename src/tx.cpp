#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// Визначення пінів для TTGO LORA32 T3_V1.6.1
#define SCLK     5     
#define MISO    19    
#define MOSI    27    
#define CS      18    
#define RST     23    
#define DIO0    26    
#define DIO1    33

// Вбудований світлодіод для індикації активності
#define LED_PIN 25   

// Налаштування радіо
#define FREQUENCY       433.0
#define FSK_BIT_RATE    4.8
#define FSK_FREQ_DEV    5.0
#define RX_BANDWIDTH    125.0

// Ініціалізація модуля SX1278
SX1278 radio = new Module(CS, DIO0, RST, DIO1);

// Змінні для відстеження часу та стану
unsigned long lastTransmitTime = 0;
unsigned long transmitInterval = 2000; // інтервал передачі в мс
int packetCounter = 0;

// Буфер для повідомлення
char message[64];

void setup() {
  // Ініціалізація світлодіода
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Ініціалізація послідовного з'єднання
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n--- TTGO LORA32 T3_V1.6.1 FSK/OOK Transmitter Starting ---");
  
  // Виведення інформації про піни
  Serial.println("Конфігурація пінів радіомодуля:");
  Serial.printf("SCLK: %d, MISO: %d, MOSI: %d, CS: %d, RST: %d, DIO0: %d, DIO1: %d\n", 
                SCLK, MISO, MOSI, CS, RST, DIO0, DIO1);
  
  // Налаштування SPI
  Serial.println("Налаштування SPI...");
  SPI.begin(SCLK, MISO, MOSI, CS);
  
  // Ініціалізація радіо в режимі FSK
  Serial.printf("Ініціалізація радіомодуля на частоті: %.2f МГц...\n", FREQUENCY);
  
  // Початок роботи в режимі FSK
  int state = radio.beginFSK(FREQUENCY, FSK_BIT_RATE, FSK_FREQ_DEV, RX_BANDWIDTH);
  if (state == RADIOLIB_ERR_NONE) {  
    Serial.println("Радіомодуль успішно ініціалізовано!");
  } else {
    Serial.printf("Помилка ініціалізації радіомодуля, код помилки: %d\n", state);
    while (true) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  // Увімкнення режиму OOK для простих сигналів
  state = radio.setOOK(true);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Режим OOK увімкнено!");
  } else {
    Serial.printf("Помилка увімкнення режиму OOK, код помилки: %d\n", state);
  }
  
  // Встановлення потужності передачі
  radio.setOutputPower(10); // 10 дБм (можна змінити в межах від 2 до 17)
  
  // Вивід конфігурації
  Serial.println("Налаштування:");
  Serial.printf("- Частота: %.2f МГц\n", FREQUENCY);
  Serial.printf("- Швидкість передачі: %.1f кбіт/с\n", FSK_BIT_RATE);
  Serial.printf("- Частотне відхилення: %.1f кГц\n", FSK_FREQ_DEV);
  Serial.printf("- Ширина смуги прийому: %.1f кГц\n", RX_BANDWIDTH);
  Serial.printf("- Інтервал передачі: %lu мс\n", transmitInterval);
  
  // Блимання світлодіодом для індикації готовності
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
  
  Serial.println("\n--- Доступні команди ---");
  Serial.println("i XXXXX - встановити інтервал передачі (XXXXX мс)");
  Serial.println("m ТЕКСТ - встановити повідомлення для передачі");
  Serial.println("s - відправити повідомлення негайно");
  Serial.println("----------------------\n");
  
  // Початкове повідомлення
  sprintf(message, "TTGO LORA32 Test Packet");
}

void sendPacket() {
  digitalWrite(LED_PIN, HIGH);
  
  // Підготовка пакету з даними
  char packet[128];
  sprintf(packet, "Packet #%d: %s", packetCounter++, message);
  
  Serial.printf("\n> Відправляю пакет: \"%s\"\n", packet);
  
  // Відправка пакету
  int state = radio.transmit(packet);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("> Пакет успішно відправлено!");
  } else {
    Serial.printf("> Помилка відправки пакету, код помилки: %d\n", state);
  }
  
  digitalWrite(LED_PIN, LOW);
}

void processSerialCommand() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    if (cmd == 'i') {
      // Команда встановлення інтервалу
      delay(10);  // дати час на отримання всіх даних
      unsigned long newInterval = Serial.parseInt();
      if (newInterval > 0) {
        transmitInterval = newInterval;
        Serial.printf("Інтервал передачі змінено на %lu мс\n", transmitInterval);
      }
    } 
    else if (cmd == 'm') {
      // Команда встановлення повідомлення
      delay(10);  // дати час на отримання всіх даних
      String newMessage = Serial.readStringUntil('\n');
      newMessage.trim();
      if (newMessage.length() > 0) {
        strncpy(message, newMessage.c_str(), sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        Serial.printf("Повідомлення змінено на \"%s\"\n", message);
      }
    }
    else if (cmd == 's') {
      // Команда негайної відправки
      Serial.println("Негайна відправка пакету");
      sendPacket();
    }
    
    // Очистити буфер
    while(Serial.available()) {
      Serial.read();
    }
  }
}

void loop() {
  // Обробка команд з послідовного порту
  processSerialCommand();
  sprintf(message, "TTGO LORA32 Test Packet");

  // Перевірка, чи пора відправити пакет
  unsigned long currentTime = millis();
  if (currentTime - lastTransmitTime >= transmitInterval) {
    sendPacket();
    lastTransmitTime = currentTime;
  }
  
  delay(10);
}
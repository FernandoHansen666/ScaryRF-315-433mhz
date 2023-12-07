#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define RX_PIN 4  // Pino de recepção
#define TX_PIN 2  // Pino de transmissão
#define OLED_RESET 22 // Pino de reset do OLED
#define SCREEN_WIDTH 128 // Largura da tela OLED
#define SCREEN_HEIGHT 64 // Altura da tela OLED

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

RCSwitch mySwitch = RCSwitch();

void setup() {
  Serial.begin(9600);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Falha ao iniciar o display OLED"));
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("|--------|");
  display.println("|Scary-RF|");
  display.println("|  TOOL  |");
  display.println("|________|");
  display.display();
  delay(2000);
  display.clearDisplay();

  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.SetRx();

  mySwitch.enableReceive(RX_PIN);
  mySwitch.enableTransmit(TX_PIN);
}

void loop() {
  if (mySwitch.available()) {
    unsigned long receivedValue = mySwitch.getReceivedValue();
    if (receivedValue != 0) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Received Signal:");
      display.setCursor(0, 10);
      display.println(receivedValue);
      display.display();

      int receivedBitLength = mySwitch.getReceivedBitlength();
      unsigned int* rawSignal = mySwitch.getReceivedRawdata();
      int receivedProtocol = mySwitch.getReceivedProtocol();

      mySwitch.resetAvailable();
      mySwitch.disableReceive();
      delay(100);
      mySwitch.enableTransmit(TX_PIN);
      ELECHOUSE_cc1101.SetTx();

      delay(3000);
      display.clearDisplay();
      display.setCursor(0, 20);
      display.println("Sending..");

      mySwitch.setProtocol(receivedProtocol);
      mySwitch.send(receivedValue, receivedBitLength);

      delay(500);
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Received Signal:");
      display.setCursor(0, 10);
      display.println(receivedValue);
      display.setCursor(0, 20);
      display.println("OK");
      display.display();

      ELECHOUSE_cc1101.SetRx();
      mySwitch.disableTransmit();
      delay(100);
      mySwitch.enableReceive(RX_PIN);
    }
  }
}

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define RX_PIN 4         // Pino de recepção
#define TX_PIN 2         // Pino de transmissão
#define BUTTON_PIN 14    // Pino do botão
#define FREQUENCY_SWITCH_PIN 13 // Pino do interruptor para mudar a frequência
#define OLED_RESET 22    // Pino de reset do OLED
#define SCREEN_WIDTH 128 // Largura da tela OLED
#define SCREEN_HEIGHT 32 // Altura da tela OLED

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RCSwitch mySwitch = RCSwitch();

unsigned long receivedValue = 0; // Variável para armazenar o valor recebido
int receivedBitLength = 0;       // Comprimento do sinal recebido
int receivedProtocol = 0;        // Protocolo do sinal recebido

void setup() {
  Serial.begin(9600);

  pinMode(BUTTON_PIN, INPUT_PULLUP); // Configura o pino do botão como entrada com pull-up
  pinMode(FREQUENCY_SWITCH_PIN, INPUT_PULLUP); // Configura o pino do seletor de frequencia

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Inicializa o display OLED
    Serial.println(F("Falha ao iniciar o display OLED"));
    while (1); // Fica preso aqui se houver falha no display
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

  ELECHOUSE_cc1101.Init(); // Inicializa o módulo CC1101
  //ELECHOUSE_cc1101.setMHZ(433.92); // Define a frequência em 433.92MHz
  
  if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) { //O botão escolhe a frequencia
    ELECHOUSE_cc1101.setMHZ(315); // Muda a frequência para 315MHz
  } else {
    ELECHOUSE_cc1101.setMHZ(433.92); // Mantém a frequência em 433.92MHz
  }
  
  ELECHOUSE_cc1101.SetRx(); // Configura o módulo CC1101 para receber


  mySwitch.enableReceive(RX_PIN); // Habilita a recepção no pino RX
  mySwitch.enableTransmit(TX_PIN); // Habilita a transmissão no pino TX
}

void loop() {
  if (mySwitch.available()) { // Se houver dados disponíveis para leitura
    receivedValue = mySwitch.getReceivedValue(); // Lê o valor recebido
    if (receivedValue != 0) { // Se o valor recebido for diferente de 0
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Received Signal:");
      display.setCursor(0, 10);
      display.println(receivedValue);
      display.display();

      receivedBitLength = mySwitch.getReceivedBitlength(); // Obtém o comprimento do sinal recebido
      unsigned int* rawSignal = mySwitch.getReceivedRawdata(); // Obtém os dados brutos do sinal
      receivedProtocol = mySwitch.getReceivedProtocol(); // Obtém o protocolo do sinal recebido

      mySwitch.resetAvailable(); // Reinicia o buffer de dados recebidos
    }
  }

  if (digitalRead(BUTTON_PIN) == LOW && receivedValue != 0) { // Se o botão for pressionado e houver um valor recebido
    mySwitch.disableReceive(); // Desabilita a recepção
    delay(100);
    mySwitch.enableTransmit(TX_PIN); // Habilita a transmissão
    ELECHOUSE_cc1101.SetTx(); // Configura o módulo CC1101 para transmitir

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Received Signal:");
    display.setCursor(0, 10);
    display.println(receivedValue);
    display.setCursor(0, 20);
    display.println("Sending...");
    display.display();

    mySwitch.setProtocol(receivedProtocol); // Configura o protocolo para o valor recebido
    mySwitch.send(receivedValue, receivedBitLength); // Envia o valor recebido com o comprimento do sinal

    delay(500);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Received Signal:");
    display.setCursor(0, 10);
    display.println(receivedValue);
    display.setCursor(0, 20);
    display.println("OK");
    display.display();

    ELECHOUSE_cc1101.SetRx(); // Configura o módulo CC1101 para receber novamente
    mySwitch.disableTransmit(); // Desabilita a transmissão
    delay(100);
    mySwitch.enableReceive(RX_PIN); // Habilita a recepção
  }

  if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) {
    ELECHOUSE_cc1101.setMHZ(315); // Muda a frequência para 315MHz
  } else {
    ELECHOUSE_cc1101.setMHZ(433.92); // Mantém a frequência em 433.92MHz
  }
  
}

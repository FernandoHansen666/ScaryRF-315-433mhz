#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define RX_PIN 4         // Pino de recepção
#define TX_PIN 2         // Pino de transmissão
#define BUTTON_PIN 14    // Pino do botão
#define BUTTON_PIN_DIR 27    // Pino do botão
#define BUTTON_PIN_ESQ 12    // Pino do botão
#define BUTTON_PIN_UP 26    // Pino do botão
#define FREQUENCY_SWITCH_PIN 13 // Pino do interruptor para mudar a frequência
#define OLED_RESET 22    // Pino de reset do OLED
#define SCREEN_WIDTH 128 // Largura da tela OLED
#define SCREEN_HEIGHT 32 // Altura da tela OLED

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RCSwitch mySwitch = RCSwitch();

unsigned long receivedValue = 0; // Variável para armazenar o valor recebido
int receivedBitLength = 0;       // Comprimento do sinal recebido
int receivedProtocol = 0;        // Protocolo do sinal recebido
const int rssi_threshold = -75; // Limiar de RSSI para considerar um sinal como forte o suficiente (Analizador de frequencia)

static const uint32_t subghz_frequency_list[] = {
    /* Lista de frequências em MHz */
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000,  // Faixa 300-348 MHz
    390000000, 418000000, 433075000, 433420000, 433920000, 434420000, 434775000, 438900000,  // Faixa 387-464 MHz
    868350000, 915000000, 925000000  // Faixa 779-928 MHz
};

#define WAVEFORM_SAMPLES 128
int waveform[WAVEFORM_SAMPLES] = {0};
int waveformIndex = 0;

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP); // Configura o pino do botão baixo como entrada com pull-up
  pinMode(FREQUENCY_SWITCH_PIN, INPUT_PULLUP); // Configura o pino do seletor de frequencia
  pinMode(BUTTON_PIN_UP, INPUT_PULLUP); // Configura o pino do botão cima como entrada com pull-up
  pinMode(BUTTON_PIN_DIR, INPUT_PULLUP); // Configura o pino do botão direito como entrada com pull-up
  pinMode(BUTTON_PIN_ESQ, INPUT_PULLUP); // Configura o pino do botão esquerda como entrada com pull-up

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Inicializa o display OLED
    Serial.println(F("Falha ao iniciar o display OLED"));
    while (1); // Fica preso aqui se houver falha no display
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("      |--------|");
  display.println("      |Scary-RF|");
  display.println("      |  TOOL  |");
  display.println("      |________|");
  display.display();
  delay(2000);
  display.clearDisplay();


  ELECHOUSE_cc1101.Init(); // Inicializa o módulo CC1101

  
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
    receivedBitLength = mySwitch.getReceivedBitlength(); // Obtém o comprimento do sinal recebido
    unsigned int* rawSignal = mySwitch.getReceivedRawdata(); // Obtém os dados brutos do sinal
    receivedProtocol = mySwitch.getReceivedProtocol(); // Obtém o protocolo do sinal recebido

    
    if (receivedValue != 0) { // Se o valor recebido for diferente de 0
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Received Signal:");
      display.setCursor(0, 10);
      display.println(receivedValue);
      display.setCursor(0, 20);
      display.printf("Bit:%d", receivedBitLength);
      display.printf("  Ptc:%d", receivedProtocol);
      display.display();


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

  //FUNÇÃO RANDOM (Futuramente Bruteforce eu acho)______________________

   if (digitalRead(BUTTON_PIN_DIR) == LOW){

    display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Sending Random:");
  display.display();

  delay(100);

  // Gera uma sequência aleatória de 9 dígitos
  unsigned long randomValue = 100000000 + random(900000000);


       int randomBitLength = 28; // Ajuste conforme necessário 433
       int randomProtocol = 6;   // Ajuste conforme necessário 433
       
  if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) {
        // Configura o protocolo e comprimento do sinal
       randomBitLength = 24; // Ajuste conforme necessário
       randomProtocol = 1;   // Ajuste conforme necessário
  }

  mySwitch.disableReceive();
  delay(100);
  mySwitch.enableTransmit(TX_PIN);
  ELECHOUSE_cc1101.SetTx();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Sending Random:");
  display.setCursor(0, 10);
  display.println(randomValue);
  display.setCursor(0, 20);
  display.println("Sending...");
  display.display();

  mySwitch.setProtocol(randomProtocol);
  mySwitch.send(randomValue, randomBitLength);

  delay(100);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Sending Random:");
  display.setCursor(0, 10);
  display.println(randomValue);
  display.setCursor(0, 20);
  display.println("OK");
  display.display();

  ELECHOUSE_cc1101.SetRx();
  mySwitch.disableTransmit();
  delay(100);
  mySwitch.enableReceive(RX_PIN);
    
    }

    // Função analizadora de frequencia e potencia _________________________ AOW POTENCIA

   if (digitalRead(BUTTON_PIN_UP) == LOW){

        display.clearDisplay();
        display.setCursor(0, 0);
        display.printf("Analyzing...");
        display.display();
        
    while (digitalRead(BUTTON_PIN_UP) == LOW) {
        // Enquanto o botão de aumento de frequência estiver pressionado       
        int rssi;
        uint32_t detectedFrequency = 0;
        int detectedRssi = -100;

        // Varredura Fina: Varre a lista de frequências definida em subghz_frequency_list
        for (size_t i = 0; i < sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]); i++) {
            uint32_t frequency = subghz_frequency_list[i];

            // Configura a frequência atual no módulo CC1101
            ELECHOUSE_cc1101.setMHZ((float)frequency / 1000000.0);
            ELECHOUSE_cc1101.SetRx();
            delayMicroseconds(3500);
            rssi = ELECHOUSE_cc1101.getRssi(); // Obtém o RSSI do sinal recebido

            // Verifica se o sinal é forte o suficiente e se é mais forte do que os sinais anteriores
            if (rssi >= rssi_threshold && rssi > detectedRssi) {
                detectedRssi = rssi;
                detectedFrequency = frequency;
            }
        }

        // Se uma Frequência FINE foi detectada, exibe as informações no display OLED
        if (detectedFrequency != 0) {
            display.clearDisplay();
            display.setCursor(0, 0);
            display.printf("Signal detected: ");
            display.setCursor(0, 10);
            display.printf("Frequency:%.2fMHz", (float)detectedFrequency / 1000000.0);
            display.setCursor(0, 20);
            display.printf("RSSI:%ddBm", detectedRssi);
            display.display();
        }

        delay(600);
    }
    ELECHOUSE_cc1101.SetRx(); // Configura o módulo CC1101 para receber novamente
    mySwitch.disableTransmit(); // Desabilita a transmissão
    delay(100);
    mySwitch.enableReceive(RX_PIN); // Habilita a recepção
   }


   //RAW WAVEFORM________________________

 while (digitalRead(BUTTON_PIN_ESQ) == LOW)
    {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Recebendo...");

        // Lê continuamente os valores do pino RX e atualiza a forma de onda
        for (int i = 1; i < SCREEN_WIDTH; i++)
        {         
                int rssi = ELECHOUSE_cc1101.getRssi();
                waveform[i] = map(rssi, -100, -40, 0, 1023); // Mapeia o RSSI para valores de forma de onda
               

            // Conecta os pixels consecutivos com uma linha
            int prevY = map(waveform[i - 1], 0, 1023, SCREEN_HEIGHT, 0);
            int currY = map(waveform[i], 0, 1023, SCREEN_HEIGHT, 0);

            display.drawLine(i - 1, prevY, i, currY, SSD1306_WHITE);

            display.display();
            delay(30); //Controle da taxa de atualizaçao da tela

            if (i == SCREEN_WIDTH){
              display.clearDisplay();
              i = 1;
            }
            
            if (digitalRead(BUTTON_PIN_ESQ) != LOW){
              break;
            }
        }

        delay(500); // Aguarda um pouco antes de começar a próxima leitura
    }
    
} //FIM LOOP

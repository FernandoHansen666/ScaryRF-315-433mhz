// ==================== VARIÁVEIS DE RAW CAPTURE ====================
#define MAX_SAMPLES 800

struct RawCapture {
  volatile unsigned long rawData[MAX_SAMPLES];
  volatile unsigned int dataPoints = 0;
  volatile unsigned long lastTime = 0;
  volatile bool signalState = false;
};

RawCapture rawCapture;

// ==================== FUNÇÃO DETECT (REPLAY) ====================
void Detect() {
  // Inicializa receptor
  mySwitch.enableReceive(RX_PIN);
  ELECHOUSE_cc1101.SetRx();

  // Mensagem inicial
  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("Waiting Signal...");
  u8g2.drawHLine(0, 40, 128);
  u8g2.setCursor(0, 50);
  u8g2.print("[OK]Send  [^]Exit");
  u8g2.setCursor(0, 60);
  u8g2.print("[v]Save  [<]Saved");
  u8g2.sendBuffer();

  while (true) {

    // === SAÍDA COM UP ===
    if (btnUp.isPressed()) {
      break; // sai do loop imediatamente
    }

    // Atualiza frequência dinamicamente
    float newMhz = (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) ? 315.00 : 433.92;
    if (newMhz != rfData.mhz) {
      rfData.mhz = newMhz;
      ELECHOUSE_cc1101.setMHZ(rfData.mhz);
      ELECHOUSE_cc1101.SetRx();
    }

    // === DETECÇÃO CONTÍNUA ===
    if (mySwitch.available()) {
      unsigned long value = mySwitch.getReceivedValue();
      int bits = mySwitch.getReceivedBitlength();
      int proto = mySwitch.getReceivedProtocol();

      if (value != 0) {
        rfData.receivedValue = value;
        rfData.receivedBitLength = bits;
        rfData.receivedProtocol = proto;

        u8g2.clearBuffer();
        u8g2.setCursor(0, 10);
        u8g2.print("Received Signal:");
        u8g2.setCursor(0, 20);
        u8g2.print(rfData.receivedValue);
        u8g2.setCursor(0, 30);
        u8g2.printf("Bit:%d Ptc:%d", bits, proto);
        u8g2.setCursor(0, 40);
        u8g2.print("[OK][^][v][<]");
        u8g2.sendBuffer();
      }

      mySwitch.resetAvailable();
    }

    // === REPLAY ===
    if (btnOk.isPressed() && rfData.receivedValue != 0) {
      mySwitch.disableReceive();
      mySwitch.enableTransmit(TX_PIN);
      ELECHOUSE_cc1101.SetTx();

      mySwitch.setProtocol(rfData.receivedProtocol);
      mySwitch.send(rfData.receivedValue, rfData.receivedBitLength);

      delay(200);

      ELECHOUSE_cc1101.SetRx();
      mySwitch.disableTransmit();
      mySwitch.enableReceive(RX_PIN);
    }
    if (btnDown.isPressed()) {
      saveSignal();
    }
    if (btnLeft.isPressed()) {
      Saved();
      u8g2.sendBuffer();//teste
    }

    delay(10);
  }

  // === LIMPEZA FINAL ===
  mySwitch.disableReceive();

  // Limpa a tela antes de voltar ao menu
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

// ================= SAVED =================
void Saved() {
  File dir = SD.open("/Subghz");
  if (!dir || !dir.isDirectory()) {
    showMessage("No Subghz dir");
    delay(800);
    return;
  }

  // Guarda caminhos completos
  String files[30];
  int count = 0;

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      files[count++] = String(entry.name()); // caminho completo
      if (count >= 30) break;
    }
    entry.close();
  }
  dir.close();

  if (count == 0) {
    showMessage("No files");
    delay(800);
    return;
  }

  int sel = 0;

  while (true) {
    u8g2.clearBuffer();

    for (int i = 0; i < 6; i++) {
      int idx = (sel / 6) * 6 + i;
      if (idx >= count) break;

      // Remove pasta do nome
      String name = files[idx];
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);

      if (idx == sel) u8g2.drawStr(0, 12 + i * 10, ">");
      u8g2.drawStr(10, 12 + i * 10, name.c_str());
    }

    u8g2.sendBuffer();

    if (btnLeft.isPressed()) {
      return;
    }

    if (btnRight.isPressed()) {
      String path = files[sel];
      if (deleteFile(path)) {
        return;
      }
    }

    if (btnDown.isPressed()) {
      sel = (sel + 1) % count;
      delay(150);
    }

    if (btnUp.isPressed()) {
      sel = (sel - 1 + count) % count;
      delay(150);
    }

    if (btnOk.isPressed()) {
      File f = SD.open(files[sel], FILE_READ);
      if (!f) {
        showMessage("Open error");
        delay(500);
        continue;
      }

      // Espera formato: name;mhz;protocol;bits;value
      char name[16];
      float mhz;
      int proto, bits;
      unsigned long val;

      String line = f.readStringUntil('\n');
      f.close();

      if (sscanf(line.c_str(), "%[^;];%f;%d;%d;%lu",
                 name, &mhz, &proto, &bits, &val) != 5) {
        showMessage("Parse error");
        delay(500);
        continue;
      }

      // Transmissão
      ELECHOUSE_cc1101.setMHZ(mhz);
      mySwitch.disableReceive();
      mySwitch.enableTransmit(TX_PIN);
      ELECHOUSE_cc1101.SetTx();

      u8g2.clearBuffer();
      u8g2.drawStr(0, 30, "TRANSMITTING...");
      u8g2.sendBuffer();

      mySwitch.setProtocol(proto);
      mySwitch.send(val, bits);

      delay(400);

      mySwitch.disableTransmit();
      ELECHOUSE_cc1101.SetRx();
      mySwitch.enableReceive(RX_PIN);
    }
  }
}

// ==================== FUNÇÃO RAW CAPTURE ====================
void showCaptureInfo(float frequency, int rssi, int sampleCount) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(0, 10, "Signal Captured");
  u8g2.drawHLine(0, 12, 128);

  u8g2.setCursor(0, 25);
  u8g2.print("Freq: ");
  u8g2.print(frequency, 2);
  u8g2.print(" MHz");

  u8g2.setCursor(0, 37);
  u8g2.print("Power: ");
  u8g2.print(rssi);
  u8g2.print(" dBm");

  u8g2.setCursor(0, 49);
  u8g2.print("Samples: ");
  u8g2.print(sampleCount);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 63);
  u8g2.print("DIR:Repeat | DOWN:New");

  u8g2.sendBuffer();
}

void retransmitSignal(float mhz) {
  ELECHOUSE_cc1101.SetTx();
  delay(15);

  bool txState = HIGH;
  digitalWrite(TX_PIN, txState);

  for (unsigned int i = 0; i < rawCapture.dataPoints; i++) {
    unsigned long pulseDuration = rawCapture.rawData[i];
    unsigned long startTime = micros();

    delayMicroseconds(pulseDuration - 12);
    txState = !txState;
    digitalWrite(TX_PIN, txState);

    while (micros() - startTime < pulseDuration) {}
  }

  digitalWrite(TX_PIN, LOW);
  ELECHOUSE_cc1101.SetRx();

  u8g2.clearBuffer();
  u8g2.setCursor(0, 25);
  u8g2.print("Freq: ");
  u8g2.print(mhz, 2);
  u8g2.print(" MHz");

  u8g2.setCursor(0, 37);
  u8g2.print("Signal retransmitted!");

  u8g2.setCursor(0, 63);
  u8g2.print("DIR:Repeat | OK:New");

  delay(300);
}

void Raw() {
  bool exitall = false;
  while (!exitall) {
    float mhz = (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) ? 315.00 : 433.92;
    ELECHOUSE_cc1101.setMHZ(mhz);
    ELECHOUSE_cc1101.SetRx();

    // Aguarda botão de captura
    u8g2.clearBuffer();
    u8g2.setCursor(0, 20);
    u8g2.print("Press [OK] to capture");
    u8g2.sendBuffer();

    while (!btnOk.isPressed()) {
      delay(10);
      if (btnUp.isPressed()) {
        exitall = true;
        break;
      }
    }

    // Prepara nova captura
    rawCapture.dataPoints = 0;
    rawCapture.lastTime = 0;
    rawCapture.signalState = false;

    // Setup interrupt
    attachInterrupt(digitalPinToInterrupt(RX_PIN), [] {
      unsigned long now = micros();
      if (rawCapture.lastTime > 0) {
        unsigned long duration = now - rawCapture.lastTime;
        if (duration > 20 && duration < 100000 && rawCapture.dataPoints < MAX_SAMPLES) {
          unsigned int currentPoints = rawCapture.dataPoints;
          rawCapture.rawData[currentPoints] = duration;
          rawCapture.dataPoints = currentPoints + 1;
          rawCapture.signalState = !rawCapture.signalState;
        }
      }
      rawCapture.lastTime = now;
    }, CHANGE);

    // Loop de captura com waveform
    int i = 1;
    while (btnOk.isHeld() && !btnUp.isPressed()) {
      if (i == 1) {
        u8g2.clearBuffer();
      }
      // Verifica mudança de frequência
      float newMhz = (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) ? 315.00 : 433.92;
      if (newMhz != mhz) {
        mhz = newMhz;
        ELECHOUSE_cc1101.setMHZ(mhz);
        ELECHOUSE_cc1101.SetRx();
      }

      // Atualiza waveform
      int rssi = ELECHOUSE_cc1101.getRssi();
      waveform[i] = map(rssi, -100, -40, 0, 1023);

      // Display info
      u8g2.setCursor(0, 7);
      u8g2.print(mhz, 2);
      u8g2.print("MHz ");
      u8g2.print(rssi);
      u8g2.print("dBm");
      u8g2.drawHLine(0, 12, 128);

      // Desenha waveform
      int prevY = map(waveform[i - 1], 0, 1023, SCREEN_HEIGHT - 20, 8);
      int currY = map(waveform[i], 0, 1023, SCREEN_HEIGHT - 20, 8);
      u8g2.drawLine(i - 1, prevY, i, currY);
      u8g2.sendBuffer();

      i++;
      if (i >= SCREEN_WIDTH) {
        u8g2.clearBuffer();
        i = 1;
      }

      delay(30);
    }

    // Finaliza captura
    detachInterrupt(digitalPinToInterrupt(RX_PIN));

    // Processa resultados
    if (rawCapture.dataPoints > 0) {
      showCaptureInfo(mhz, ELECHOUSE_cc1101.getRssi(), rawCapture.dataPoints);

      // Loop de retransmissão múltipla
      while (!btnUp.isPressed()) {
        if (btnOk.isPressed()) {
          break; // Sai para nova captura
        }

        if (btnRight.isPressed()) {
          retransmitSignal(mhz);
        }

        delay(10);
      }
    } else if (!exitall) {
      u8g2.clearBuffer();
      u8g2.setCursor(0, 30);
      u8g2.print("No signal captured!");
      u8g2.sendBuffer();
      delay(1000);
    }
  }
}

// ==================== FUNÇÃO ANALYSER ====================
void Analyser() {
  u8g2.clearBuffer();
  u8g2.setCursor(0, 20);
  u8g2.print("Analyzing...");
  u8g2.sendBuffer();

  bool exitAnalyser = false;

  while (!exitAnalyser) {
    int rssi;
    uint32_t detectedFrequency = 0;
    int detectedRssi = -100;

    // Sai imediatamente se UP for pressionado
    if (btnUp.isPressed()) {
      exitAnalyser = true;
      break;
    }

    // Varre lista de frequências
    for (size_t i = 0; i < sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]); i++) {
      uint32_t frequency = subghz_frequency_list[i];

      if (btnUp.isPressed()) { // Verificação a cada frequência
        exitAnalyser = true;
        break;
      }

      ELECHOUSE_cc1101.setMHZ((float)frequency / 1000000.0);
      ELECHOUSE_cc1101.SetRx();
      delayMicroseconds(3500);
      rssi = ELECHOUSE_cc1101.getRssi();

      if (rssi >= rfData.rssi_threshold && rssi > detectedRssi) {
        detectedRssi = rssi;
        detectedFrequency = frequency;
      }
    }

    if (exitAnalyser) break; // Sai do loop principal

    // Mostra sinal detectado, se houver
    if (detectedFrequency != 0) {
      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Signal detected:");
      u8g2.setCursor(0, 20);
      u8g2.printf("Frequency:%.2fMHz", (float)detectedFrequency / 1000000.0);
      u8g2.setCursor(0, 30);
      u8g2.printf("RSSI:%ddBm", detectedRssi);
      u8g2.sendBuffer();
      detectedFrequency = 0;
    }

    delay(600);
  }

  // Limpeza final ao sair
  ELECHOUSE_cc1101.SetRx();
  mySwitch.disableTransmit();
  delay(100);
  mySwitch.enableReceive(RX_PIN);

  u8g2.clearBuffer();
  u8g2.sendBuffer();
}


// ==================== FUNÇÃO SEND RANDOM ====================
void SendRandom() {
  showMessage("Press BTN to", "send random");

  bool exitSend = false;

  while (!exitSend) {
    // Sai se UP for pressionado
    if (btnUp.isPressed()) {
      exitSend = true;
      break;
    }

    // Envia enquanto OK estiver pressionado
    while (btnOk.isHeld()) {
      if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) {
        ELECHOUSE_cc1101.setMHZ(315);
        rfData.mhz = 315.00;
      } else {
        ELECHOUSE_cc1101.setMHZ(433.92);
        rfData.mhz = 433.92;
      }

      unsigned long randomValue = 100000000 + random(900000000);
      int randomBitLength = (rfData.mhz == 315.00) ? 24 : 28;
      int randomProtocol = random(12);

      mySwitch.disableReceive();
      delay(50); // pequeno delay para estabilidade
      mySwitch.enableTransmit(TX_PIN);
      ELECHOUSE_cc1101.SetTx();

      // Exibe mensagem de envio
      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Sending Random:");
      u8g2.setCursor(0, 20);
      u8g2.print(randomValue);
      u8g2.setCursor(0, 30);
      u8g2.print(randomProtocol);
      u8g2.setCursor(0, 40);
      u8g2.print("Sending...");
      u8g2.sendBuffer();

      // Envia o valor
      mySwitch.setProtocol(randomProtocol);
      mySwitch.send(randomValue, randomBitLength);

      // Confirma envio
      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Sending Random:");
      u8g2.setCursor(0, 20);
      u8g2.print(randomValue);
      u8g2.setCursor(0, 30);
      u8g2.print("OK");
      u8g2.sendBuffer();

      // Retorna para recepção
      ELECHOUSE_cc1101.SetRx();
      mySwitch.disableTransmit();
      delay(50);
      mySwitch.enableReceive(RX_PIN);

      delay(50); // pequena pausa entre envios para estabilidade, mas quase contínuo
    }

    delay(10); // Verificação de UP ou OK
  }

  // Limpeza final ao sair
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

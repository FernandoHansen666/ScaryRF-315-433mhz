void Detect() {
  while (digitalRead(BUTTON_PIN_UP) != LOW) {
    if (receivedBitLength == 0 ) {
      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Waiting Signal...");
      u8g2.sendBuffer();
    }
    if (mySwitch.available()) {
      receivedValue = mySwitch.getReceivedValue();
      receivedBitLength = mySwitch.getReceivedBitlength();
      unsigned int* rawSignal = mySwitch.getReceivedRawdata();
      receivedProtocol = mySwitch.getReceivedProtocol();

      if (receivedValue != 0) {
        u8g2.clearBuffer();
        u8g2.setCursor(0, 10);
        u8g2.print("Received Signal:");
        u8g2.setCursor(0, 20);
        u8g2.print(receivedValue);
        u8g2.setCursor(0, 30);
        u8g2.printf("Bit:%d  Ptc:%d", receivedBitLength, receivedProtocol);
        u8g2.sendBuffer();

        mySwitch.resetAvailable();
      }
    }

    if (digitalRead(BUTTON_PIN) == LOW && receivedValue != 0) {
      mySwitch.disableReceive();
      delay(100);
      mySwitch.enableTransmit(TX_PIN);
      ELECHOUSE_cc1101.SetTx();

      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Received Signal:");
      u8g2.setCursor(0, 20);
      u8g2.print(receivedValue);
      u8g2.setCursor(0, 30);
      u8g2.print("Sending...");
      u8g2.sendBuffer();

      mySwitch.setProtocol(receivedProtocol);
      mySwitch.send(receivedValue, receivedBitLength);

      delay(500);

      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.print("Received Signal:");
      u8g2.setCursor(0, 20);
      u8g2.print(receivedValue);
      u8g2.setCursor(0, 30);
      u8g2.print("OK");
      u8g2.sendBuffer();

      ELECHOUSE_cc1101.SetRx();
      mySwitch.disableTransmit();
      delay(100);
      mySwitch.enableReceive(RX_PIN);
    }

    if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) {
      ELECHOUSE_cc1101.setMHZ(315);
    } else {
      ELECHOUSE_cc1101.setMHZ(433.92);
    }

  }
}

void Raw() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    u8g2.clearBuffer();
    float mhz = 0;

    for (int i = 1; i < SCREEN_WIDTH; i++) {
      int rssi = ELECHOUSE_cc1101.getRssi();
      waveform[i] = map(rssi, -100, -40, 0, 1023);

      if (rssi < -75) {
        if (i % 2 == 0) {
          ELECHOUSE_cc1101.setMHZ(433.92);
          ELECHOUSE_cc1101.SetRx();
          mhz = 433.92;
        } else {
          ELECHOUSE_cc1101.setMHZ(315);
          ELECHOUSE_cc1101.SetRx();
          mhz = 315.00;
        }
      } else {
        u8g2.setCursor(0, 7);
        u8g2.print(mhz);
      }

      int prevY = map(waveform[i - 1], 0, 1023, SCREEN_HEIGHT - 13, 8);
      int currY = map(waveform[i], 0, 1023, SCREEN_HEIGHT - 13, 8);
      u8g2.drawLine(i - 1, prevY, i, currY);

      u8g2.sendBuffer();
      delay(30);

      if (i == SCREEN_WIDTH && mhz != 0) {
        u8g2.clearBuffer();
        u8g2.setCursor(0, 7);
        u8g2.print(mhz);
        i = 1;
      }

      if (digitalRead(BUTTON_PIN) != LOW) {
        break;
      }
    }

    delay(500);
  }
}

void Analyser() {
  u8g2.clearBuffer();
  u8g2.setCursor(0, 20);
  u8g2.printf("Analyzing...");
  u8g2.sendBuffer();

  while (digitalRead(BUTTON_PIN_UP) != LOW) {
    int rssi;
    uint32_t detectedFrequency = 0;
    int detectedRssi = -100;

    for (size_t i = 0; i < sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]); i++) {
      uint32_t frequency = subghz_frequency_list[i];
      
      if (digitalRead(BUTTON_PIN_UP) == LOW){
        break;
      }

      ELECHOUSE_cc1101.setMHZ((float)frequency / 1000000.0);
      ELECHOUSE_cc1101.SetRx();
      delayMicroseconds(3500);
      rssi = ELECHOUSE_cc1101.getRssi();

      if (rssi >= rssi_threshold && rssi > detectedRssi) {
        detectedRssi = rssi;
        detectedFrequency = frequency;
      }     
    }

    if (detectedFrequency != 0) {
      u8g2.clearBuffer();
      u8g2.setCursor(0, 10);
      u8g2.printf("Signal detected:");
      u8g2.setCursor(0, 20);
      u8g2.printf("Frequency:%.2fMHz", (float)detectedFrequency / 1000000.0);
      u8g2.setCursor(0, 30);
      u8g2.printf("RSSI:%ddBm", detectedRssi);
      u8g2.sendBuffer();
      detectedFrequency = 0;
    }

    delay(600);
  }
  ELECHOUSE_cc1101.SetRx();
  mySwitch.disableTransmit();
  delay(100);
  mySwitch.enableReceive(RX_PIN);

}

void SendRandom() {

  if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) {
    ELECHOUSE_cc1101.setMHZ(315);
  } else {
    ELECHOUSE_cc1101.setMHZ(433.92);
  }

  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("Sending Random:");
  u8g2.sendBuffer();
  delay(100);

  unsigned long randomValue = 100000000 + random(900000000);
  int randomBitLength = 28;
  int randomProtocol = 6;

  if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) {
    randomBitLength = 24;
    randomProtocol = 1;
  }

  mySwitch.disableReceive();
  delay(100);
  mySwitch.enableTransmit(TX_PIN);
  ELECHOUSE_cc1101.SetTx();

  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("Sending Random:");
  u8g2.setCursor(0, 20);
  u8g2.print(randomValue);
  u8g2.setCursor(0, 30);
  u8g2.print("Sending...");
  u8g2.sendBuffer();

  mySwitch.setProtocol(randomProtocol);
  mySwitch.send(randomValue, randomBitLength);

  delay(100);

  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("Sending Random:");
  u8g2.setCursor(0, 20);
  u8g2.print(randomValue);
  u8g2.setCursor(0, 30);
  u8g2.print("OK");
  u8g2.sendBuffer();

  ELECHOUSE_cc1101.SetRx();
  mySwitch.disableTransmit();
  delay(100);
  mySwitch.enableReceive(RX_PIN);
}

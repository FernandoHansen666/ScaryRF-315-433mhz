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
      mhz = 315.00;
    } else {
      ELECHOUSE_cc1101.setMHZ(433.92);
      mhz = 433.92;
    }

  }
}


#define MAX_SAMPLES 600
volatile unsigned long rawData[MAX_SAMPLES];
volatile unsigned int dataPoints = 0;
volatile unsigned long lastTime = 0;
volatile bool signalState = false;

void showCaptureInfo(float frequency, int rssi, int sampleCount) {
  u8g2.clearBuffer();
  
  // Header
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(0, 10, "✅ Signal Captured");
  u8g2.drawHLine(0, 12, 128);

  // Main information
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

  // Footer
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 63);
  u8g2.print("DIR:Repeat | BTN:New Capture");

  u8g2.sendBuffer();
}

void retransmitSignal(float mhz) {
  ELECHOUSE_cc1101.SetTx();
  delay(15); // Stabilization time
  
  bool txState = HIGH; // Initial state always HIGH
  digitalWrite(TX_PIN, txState);
  
  for (unsigned int i = 0; i < dataPoints; i++) {
    unsigned long pulseDuration = rawData[i];
    unsigned long startTime = micros();
    
    // Send pulse with precise timing
    delayMicroseconds(pulseDuration - 12); // Compensate overhead
    txState = !txState;
    digitalWrite(TX_PIN, txState);
    
    // Ensure exact pulse duration
    while (micros() - startTime < pulseDuration) {}
  }
  
  digitalWrite(TX_PIN, LOW);
  ELECHOUSE_cc1101.SetRx();
  
  // Quick feedback
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  u8g2.print("Signal retransmitted!");
  u8g2.sendBuffer();
  delay(300);
}

void Raw() {
  while (true) {
    // Set initial frequency
    float mhz = (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) ? 315.00 : 433.92;
    ELECHOUSE_cc1101.setMHZ(mhz);
    ELECHOUSE_cc1101.SetRx();
    
    // Wait for capture button
    while (digitalRead(BUTTON_PIN) == HIGH) {
      if (digitalRead(BUTTON_PIN_UP) == LOW) return;
      delay(10);
    }
    
    // Prepare new capture
    u8g2.clearBuffer();
    dataPoints = 0;
    lastTime = 0;
    signalState = false;
    
    // Setup interrupt
    attachInterrupt(digitalPinToInterrupt(RX_PIN), []{
      unsigned long now = micros();
      if (lastTime > 0) {
        unsigned long duration = now - lastTime;
        if (duration > 20 && duration < 100000 && dataPoints < MAX_SAMPLES) {
          rawData[dataPoints++] = duration;
          signalState = !signalState;
        }
      }
      lastTime = now;
    }, CHANGE);

    // Capture loop with waveform
    int i = 1;
    while (digitalRead(BUTTON_PIN) == LOW) {
      if (digitalRead(BUTTON_PIN_UP) == LOW) {
        detachInterrupt(digitalPinToInterrupt(RX_PIN));
        return;
      }
      
      // Check frequency change
      float newMhz = (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) ? 315.00 : 433.92;
      if (newMhz != mhz) {
        mhz = newMhz;
        ELECHOUSE_cc1101.setMHZ(mhz);
        ELECHOUSE_cc1101.SetRx();
      }
      
      // Update waveform
      int rssi = ELECHOUSE_cc1101.getRssi();
      waveform[i] = map(rssi, -100, -40, 0, 1023);
      
      // Display information
      u8g2.setCursor(0, 7);
      u8g2.print(mhz, 2);
      u8g2.print("MHz ");
      u8g2.print(rssi);
      u8g2.print("dBm");
      
      // Draw waveform
      int prevY = map(waveform[i - 1], 0, 1023, SCREEN_HEIGHT - 13, 8);
      int currY = map(waveform[i], 0, 1023, SCREEN_HEIGHT - 13, 8);
      u8g2.drawLine(i - 1, prevY, i, currY);
      u8g2.sendBuffer();
      
      i++;
      if (i >= SCREEN_WIDTH) {
        u8g2.clearBuffer();
        i = 1;
      }
      
      delay(30);
    }

    // Finish capture
    detachInterrupt(digitalPinToInterrupt(RX_PIN));

    // Process results
    if (dataPoints > 0) {
      showCaptureInfo(mhz, ELECHOUSE_cc1101.getRssi(), dataPoints);

      // Multiple retransmission loop
      while (true) {
        if (digitalRead(BUTTON_PIN_UP) == LOW) return;
        
        if (digitalRead(BUTTON_PIN) == LOW) {
          delay(50);
          break; // Exit for new capture
        }
        
        if (digitalRead(BUTTON_PIN_DIR) == LOW) {
          delay(50); // Debounce
          retransmitSignal(mhz);
        }
        
        delay(10);
      }
    } else {
      u8g2.clearBuffer();
      u8g2.setCursor(0, 30);
      u8g2.print("No signal captured!");
      u8g2.sendBuffer();
      delay(1000);
    }
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
    mhz = 315.00;
  } else {
    ELECHOUSE_cc1101.setMHZ(433.92);
    mhz = 433.92;
  }

  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("Sending Random:");
  u8g2.sendBuffer();
  delay(100);

  unsigned long randomValue = 100000000 + random(900000000);
  int randomBitLength = 28;
  int randomProtocol = 0 + random(12);

  if (digitalRead(FREQUENCY_SWITCH_PIN) == LOW) {
    randomBitLength = 24;
    randomProtocol = 0 + random(12);
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
  u8g2.print(randomProtocol);
  u8g2.setCursor(0, 40);
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

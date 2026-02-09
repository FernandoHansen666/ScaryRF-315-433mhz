
// ================= SD INIT =================
void initSD() {
  hspi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, hspi)) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 40, "SD FAIL");
    u8g2.sendBuffer();
    while (1);
  }
  u8g2.drawStr(0, 40, "SD CARD OK!..");
  u8g2.sendBuffer();
  delay(500);
  SD.mkdir("/Subghz");
}

// ================= EDITOR DE NOME =================
char signalName[13];
int namePos = 0;
int charIndex = 0;
const char charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";


bool editSignalName() {
  memset(signalName, 0, sizeof(signalName));
  namePos = 0;
  charIndex = 0;


  while (true) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Name:");
    u8g2.drawStr(0, 25, signalName);
    u8g2.drawStr(namePos * 6, 35, "^");
    u8g2.drawStr(0, 55, "UP/DN:Char  RT:Next  LF:Del");
    u8g2.sendBuffer();

    if (btnUp.isPressed()) {
      charIndex = (charIndex - 1 + sizeof(charset)) % sizeof(charset);
      signalName[namePos] = charset[charIndex];
      signalName[namePos + 1] = '\0';
    }
    if (btnDown.isPressed()) {
      charIndex = (charIndex + 1) % sizeof(charset);
      signalName[namePos] = charset[charIndex];
      signalName[namePos + 1] = '\0';
    }

    // === AVANÇA POSIÇÃO ===
    if (btnRight.isPressed() && namePos < 12) {
      namePos++;
      signalName[namePos] = charset[charIndex];
      signalName[namePos + 1] = '\0';
    }
    // === APAGA ===
    if (btnLeft.isPressed() && namePos > 0) {
      signalName[namePos] = '\0';
      namePos--;
    }
    // === CONFIRMA ===
    if (btnOk.isPressed() && namePos >= 0) {
      return true;
    }
    // === CANCELA (UP + LEFT) ===
    if (digitalRead(BTN_UP) == LOW && digitalRead(BTN_LEFT) == LOW) {
      return false;
    }
  }
}

// ================= SALVAR =================
void saveSignal() {
  if (!editSignalName()) return;


  String path = String("/Subghz/") + signalName + ".scrf";
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;


  f.print(signalName);
  f.print(";");
  f.print(rfData.mhz, 2);
  f.print(";");
  f.print(rfData.receivedProtocol);
  f.print(";");
  f.print(rfData.receivedBitLength);
  f.print(";");
  f.println(rfData.receivedValue);
  f.close();


  showMessage("Saved!");
  delay(500);
}

//================ DELETE ===============
bool deleteFile(String path) {
  while (true) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 20, "Delete file?");
    u8g2.drawStr(0, 35, "OK = YES");
    u8g2.drawStr(0, 50, "LEFT = NO");
    u8g2.sendBuffer();

    if (btnOk.isPressed()) {
      SD.remove(path);
      showMessage("Deleted");
      delay(500);
      return true;
    }

    if (btnLeft.isPressed()) {
      return false;
    }
  }
}

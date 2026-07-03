#include "scrbtm.h"

// ================= SD INIT =================
void initSD() {
  hspi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, hspi)) {
    sdAvailable = false;
    u8g2.drawStr(0, 40, "SD not found");
    u8g2.sendBuffer();
    delay(800); // segue sem SD; so Save/Load/Config ficam indisponiveis
    return;
  }
  sdAvailable = true;
  u8g2.drawStr(0, 40, "SD CARD OK!..");
  u8g2.sendBuffer();
  delay(500);
  SD.mkdir("/Subghz");
}

// ================= CONFIG (key=value em /scaryrf.cfg) =================
// Le o arquivo de config para a struct global. Campos ausentes mantem o default.
void loadConfig() {
  if (!sdAvailable) return;
  File f = SD.open("/scaryrf.cfg", FILE_READ);
  if (!f) return; // sem arquivo -> usa defaults
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    if (key == "mainFreq") config.mainFreq = val.toFloat();
    else if (key == "deviceName" && val.length() > 0) {
      val.toCharArray(config.deviceName, sizeof(config.deviceName));
    }
  }
  f.close();
}

// Grava a struct de config no arquivo (sobrescreve).
void saveConfig() {
  if (!sdAvailable) return;
  SD.remove("/scaryrf.cfg"); // garante reescrita limpa
  File f = SD.open("/scaryrf.cfg", FILE_WRITE);
  if (!f) return;
  f.print("mainFreq=");
  f.println(config.mainFreq, 2);
  f.print("deviceName=");
  f.println(config.deviceName);
  f.close();
}

// ================= EDITOR DE NOME (teclado estilo Flipper) =================
char signalName[16];
#define NAME_MAX 12

// 3 linhas de caracteres (13 colunas) + linha de acoes (DEL / SAVE).
static const char kbChars[3][14] = {
  "ABCDEFGHIJKLM",
  "NOPQRSTUVWXYZ",
  "0123456789_- "
};
#define KB_COLS 13

// Navegue com o D-pad pelo teclado; OK digita a letra ou aciona DEL/SAVE.
// Para cancelar sem salvar, segure UP + LEFT juntos.
bool editSignalName() {
  memset(signalName, 0, sizeof(signalName));
  int namePos = 0;
  int kbRow = 0, kbCol = 0;

  const int CELL_W = 9, CELL_H = 12, KB_X0 = 4, KB_Y0 = 13;

  while (true) {
    // ---- Desenho ----
    u8g2.clearBuffer();

    // Nome digitado no topo
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, "Name:");
    u8g2.drawStr(34, 10, signalName);

    // Teclado de caracteres
    u8g2.setFont(u8g2_font_5x7_tf);
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < KB_COLS; c++) {
        int cx = KB_X0 + c * CELL_W;
        int cy = KB_Y0 + r * CELL_H;
        char ch[2] = { kbChars[r][c], 0 };
        bool sel = (kbRow == r && kbCol == c);
        if (sel) { u8g2.drawBox(cx, cy, CELL_W, CELL_H); u8g2.setDrawColor(0); }
        u8g2.drawStr(cx + 2, cy + 9, ch);
        if (sel) u8g2.setDrawColor(1);
      }
    }

    // Linha de acoes: DEL | BACK | SAVE
    int ay = KB_Y0 + 3 * CELL_H;
    const char* actLbl[3]  = { "DEL", "BACK", "SAVE" };
    const int   actX[3]    = { 2, 44, 86 };
    const int   actLblX[3] = { 14, 52, 96 };
    for (int a = 0; a < 3; a++) {
      bool sel = (kbRow == 3 && kbCol == a);
      if (sel) { u8g2.drawBox(actX[a], ay, 40, CELL_H); u8g2.setDrawColor(0); }
      u8g2.drawStr(actLblX[a], ay + 9, actLbl[a]);
      if (sel) u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();

    // ---- Navegacao no teclado ----
    if (btnRight.isPressed()) {
      int maxCol = (kbRow == 3) ? 2 : (KB_COLS - 1);
      kbCol = (kbCol >= maxCol) ? 0 : kbCol + 1;
    }
    if (btnLeft.isPressed()) {
      int maxCol = (kbRow == 3) ? 2 : (KB_COLS - 1);
      kbCol = (kbCol <= 0) ? maxCol : kbCol - 1;
    }
    if (btnDown.isPressed() && kbRow < 3) {
      kbRow++;
      // mapeia a coluna para uma das 3 acoes (DEL / BACK / SAVE)
      if (kbRow == 3) kbCol = (kbCol < 4) ? 0 : (kbCol < 9 ? 1 : 2);
    }
    if (btnUp.isPressed() && kbRow > 0) {
      if (kbRow == 3) kbCol = (kbCol == 0) ? 0 : (kbCol == 1 ? 6 : 12);
      kbRow--;
    }

    // ---- Selecao (OK) ----
    if (btnOk.isPressed()) {
      if (kbRow < 3) {
        if (namePos < NAME_MAX) {
          signalName[namePos++] = kbChars[kbRow][kbCol];
          signalName[namePos] = '\0';
        }
      } else if (kbCol == 0) {       // DEL (backspace)
        if (namePos > 0) { namePos--; signalName[namePos] = '\0'; }
      } else if (kbCol == 1) {       // BACK (cancela sem salvar)
        return false;
      } else {                       // SAVE
        if (namePos > 0) return true;
      }
    }
  }
}

// ================= SALVAR (formato .sub do Flipper) =================
void saveSignal() {
  if (!sdAvailable) { showMessage("No SD card"); delay(800); return; }
  if (rfData.receivedValue == 0) { showMessage("No signal", "to save"); delay(800); return; }
  if (!editSignalName()) return;

  String path = String("/Subghz/") + signalName + ".sub";
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;

  unsigned long freqHz = (unsigned long)(rfData.mhz * 1000000.0 + 0.5);
  int te = rfData.receivedDelay > 0 ? rfData.receivedDelay : 350;
  unsigned long val = rfData.receivedValue;

  // Cabecalho padrao Flipper SubGhz. Salvamos como Princeton (cobre PT2262/
  // EV1527 = a maioria dos controles de codigo fixo). Key = 8 bytes big-endian.
  f.println("Filetype: Flipper SubGhz Key File");
  f.println("Version: 1");
  f.print("Frequency: ");
  f.println(freqHz);
  f.println("Preset: FuriHalSubGhzPresetOok650Async");
  f.println("Protocol: Princeton");
  f.print("Bit: ");
  f.println(rfData.receivedBitLength);

  char keybuf[32];
  snprintf(keybuf, sizeof(keybuf), "Key: 00 00 00 00 %02lX %02lX %02lX %02lX",
           (val >> 24) & 0xFF, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
  f.println(keybuf);
  f.print("TE: ");
  f.println(te);
  // Campo proprio (ignorado pelo Flipper): guarda o protocolo real do RCSwitch
  // para o replay sair fiel mesmo quando nao for Princeton/protocolo 1.
  f.print("RcProtocol: ");
  f.println(rfData.receivedProtocol);
  f.close();

  showMessage("Saved (.sub)!");
  delay(500);
}

//================ DELETE ===============
bool deleteFile(String path) {
  static const CmdEntry delCmds[] = { { CMD_OK, "Yes" }, { CMD_LEFT, "No" } };
  while (true) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 20, "Delete file?");
    drawCommandBar(delCmds, 2);
    u8g2.sendBuffer();

    if (btnOk.isPressed()) {
      bool ok = SD.remove(path);
      if (!ok && SD.exists(path)) {
        // Algumas versoes do SD nao removem com "//" ou caminho relativo:
        // tenta normalizar barras duplicadas antes de desistir.
        String p2 = path;
        p2.replace("//", "/");
        ok = SD.remove(p2);
      }
      showMessage(ok ? "Deleted" : "Delete failed");
      delay(600);
      return ok;
    }

    if (btnLeft.isPressed()) {
      return false;
    }
  }
}

// Apaga uma pasta recursivamente (SD.rmdir so remove pasta vazia). Coleta os
// filhos antes de apagar — remover durante openNextFile corrompe o iterador.
// Limita a 30 itens por nivel; pastas maiores nao garantem limpeza total.
bool removeDirRecursive(String path) {
  String openPath = path;
  if (openPath.length() > 1 && openPath.endsWith("/"))
    openPath = openPath.substring(0, openPath.length() - 1);

  File dir = SD.open(openPath);
  if (!dir) return false;
  if (!dir.isDirectory()) { dir.close(); return SD.remove(openPath); }

  char childNames[24][64];
  bool childIsDir[24];
  int cn = 0;
  while (cn < 24) {
    File e = dir.openNextFile();
    if (!e) break;
    String nm = String(e.name());
    int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
    childIsDir[cn] = e.isDirectory();
    nm.toCharArray(childNames[cn], 64);
    cn++;
    e.close();
  }
  bool overflow = (cn >= 24 && dir.openNextFile()); // ainda havia mais itens?
  dir.close();

  bool ok = !overflow;
  for (int i = 0; i < cn; i++) {
    String child = openPath + "/" + childNames[i];
    if (childIsDir[i]) { if (!removeDirRecursive(child)) ok = false; }
    else { if (!SD.remove(child)) ok = false; }
  }
  return SD.rmdir(openPath) && ok;
}

// Dialogo de confirmacao para apagar a PASTA selecionada (e todo o conteudo).
bool deleteFolder(String path) {
  static const CmdEntry delCmds[] = { { CMD_OK, "Yes" }, { CMD_LEFT, "No" } };
  while (true) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 16, "Delete FOLDER?");
    u8g2.drawStr(0, 30, "(and contents)");
    drawCommandBar(delCmds, 2);
    u8g2.sendBuffer();

    if (btnOk.isPressed()) {
      bool ok = removeDirRecursive(path);
      showMessage(ok ? "Folder deleted" : "Delete failed");
      delay(700);
      return ok;
    }
    if (btnLeft.isPressed()) return false;
  }
}

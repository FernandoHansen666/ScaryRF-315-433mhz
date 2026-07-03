#include "scrbtm.h"
#include <NimBLEHIDDevice.h>

// ====================================================================
//  BAD-BLE — teclado BLE falso que executa scripts estilo DuckyScript
//  (BadUSB do Flipper). Os scripts sao .txt em /badusb/ no cartao SD.
//
//  Fluxo: entra -> anuncia como "ScaryRF KB" -> host pareia -> escolhe um
//  script -> injeta as teclas. Suporta STRING, DELAY, ENTER, GUI r, combos
//  (CTRL ALT DELETE), setas, F1-F12, etc.
// ====================================================================

// Report map de teclado padrao (Report ID 1, relatorio de 8 bytes:
// modificadores + reservado + 6 keycodes).
static const uint8_t HID_REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x06,
  0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
  0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0
};

#define KEY_LCTRL  0x01
#define KEY_LSHIFT 0x02
#define KEY_LALT   0x04
#define KEY_LGUI   0x08

static NimBLEHIDDevice     *badHid = nullptr;
static NimBLECharacteristic *hidInput = nullptr;
static volatile bool badbleConnected = false;
static uint16_t badbleDefaultDelay = 0; // DELAY padrao entre linhas (DEFAULT_DELAY)

class BadbleServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s)    { badbleConnected = true; }
  void onDisconnect(NimBLEServer *s) { badbleConnected = false; NimBLEDevice::startAdvertising(); }
};

// -------- Envio de um relatorio (pressiona e solta) --------
static void hidSend(uint8_t mod, uint8_t key) {
  if (!badbleConnected || !hidInput) return;
  uint8_t rep[8] = { mod, 0, key, 0, 0, 0, 0, 0 };
  hidInput->setValue(rep, 8); hidInput->notify();
  delay(8);
  uint8_t rel[8] = { 0 };
  hidInput->setValue(rel, 8); hidInput->notify();
  delay(8);
}

// -------- ASCII -> keycode HID (layout US). Retorna 0 se nao mapeia. --------
static uint8_t asciiToHid(char c, bool *shift) {
  *shift = false;
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') { *shift = true; return 0x04 + (c - 'A'); }
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  if (c == '0') return 0x27;
  switch (c) {
    case ' ':  return 0x2C;
    case '\n': return 0x28;
    case '\t': return 0x2B;
    case '-':  return 0x2D;  case '_': *shift = true; return 0x2D;
    case '=':  return 0x2E;  case '+': *shift = true; return 0x2E;
    case '[':  return 0x2F;  case '{': *shift = true; return 0x2F;
    case ']':  return 0x30;  case '}': *shift = true; return 0x30;
    case '\\': return 0x31;  case '|': *shift = true; return 0x31;
    case ';':  return 0x33;  case ':': *shift = true; return 0x33;
    case '\'': return 0x34;  case '"': *shift = true; return 0x34;
    case '`':  return 0x35;  case '~': *shift = true; return 0x35;
    case ',':  return 0x36;  case '<': *shift = true; return 0x36;
    case '.':  return 0x37;  case '>': *shift = true; return 0x37;
    case '/':  return 0x38;  case '?': *shift = true; return 0x38;
    case '!': *shift = true; return 0x1E;
    case '@': *shift = true; return 0x1F;
    case '#': *shift = true; return 0x20;
    case '$': *shift = true; return 0x21;
    case '%': *shift = true; return 0x22;
    case '^': *shift = true; return 0x23;
    case '&': *shift = true; return 0x24;
    case '*': *shift = true; return 0x25;
    case '(': *shift = true; return 0x26;
    case ')': *shift = true; return 0x27;
  }
  return 0;
}

static void hidType(const char *s) {
  for (; *s; s++) {
    bool sh; uint8_t k = asciiToHid(*s, &sh);
    if (k) hidSend(sh ? KEY_LSHIFT : 0, k);
  }
}

// Token de tecla nomeada -> keycode. 0 se nao for.
static uint8_t namedKey(const char *t) {
  if (!strcmp(t, "ENTER"))      return 0x28;
  if (!strcmp(t, "ESC") || !strcmp(t, "ESCAPE")) return 0x29;
  if (!strcmp(t, "BACKSPACE"))  return 0x2A;
  if (!strcmp(t, "TAB"))        return 0x2B;
  if (!strcmp(t, "SPACE"))      return 0x2C;
  if (!strcmp(t, "CAPSLOCK"))   return 0x39;
  if (!strcmp(t, "DELETE") || !strcmp(t, "DEL")) return 0x4C;
  if (!strcmp(t, "INSERT"))     return 0x49;
  if (!strcmp(t, "HOME"))       return 0x4A;
  if (!strcmp(t, "END"))        return 0x4D;
  if (!strcmp(t, "PAGEUP"))     return 0x4B;
  if (!strcmp(t, "PAGEDOWN"))   return 0x4E;
  if (!strcmp(t, "RIGHT") || !strcmp(t, "RIGHTARROW")) return 0x4F;
  if (!strcmp(t, "LEFT")  || !strcmp(t, "LEFTARROW"))  return 0x50;
  if (!strcmp(t, "DOWN")  || !strcmp(t, "DOWNARROW"))  return 0x51;
  if (!strcmp(t, "UP")    || !strcmp(t, "UPARROW"))    return 0x52;
  if (!strcmp(t, "PRINTSCREEN")) return 0x46;
  if (!strcmp(t, "MENU") || !strcmp(t, "APP")) return 0x65;
  if (t[0] == 'F' && t[1] >= '1' && t[1] <= '9') { int f = atoi(t + 1); if (f >= 1 && f <= 12) return 0x3A + (f - 1); }
  return 0;
}

static uint8_t modKey(const char *t) {
  if (!strcmp(t, "CTRL") || !strcmp(t, "CONTROL")) return KEY_LCTRL;
  if (!strcmp(t, "SHIFT")) return KEY_LSHIFT;
  if (!strcmp(t, "ALT"))   return KEY_LALT;
  if (!strcmp(t, "GUI") || !strcmp(t, "WINDOWS") || !strcmp(t, "WIN")) return KEY_LGUI;
  return 0;
}

// Executa uma linha de DuckyScript.
static void runDuckyLine(char *line) {
  while (*line == ' ' || *line == '\t') line++;
  int len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' '))
    line[--len] = 0;
  if (len == 0) return;

  if (!strncmp(line, "REM", 3)) return;
  if (!strncmp(line, "STRINGLN ", 9)) { hidType(line + 9); hidSend(0, 0x28); return; }
  if (!strncmp(line, "STRING ", 7))   { hidType(line + 7); return; }
  if (!strncmp(line, "DELAY ", 6)) {
    long d = atol(line + 6);
    while (d > 0 && !btnLeft.isHeld()) { delay(d > 20 ? 20 : d); d -= 20; }
    return;
  }
  if (!strncmp(line, "DEFAULT_DELAY ", 14)) { badbleDefaultDelay = atoi(line + 14); return; }
  if (!strncmp(line, "DEFAULTDELAY ", 13))  { badbleDefaultDelay = atoi(line + 13); return; }

  // Combo/tecla: acumula modificadores + uma tecla final (nomeada ou 1 char).
  uint8_t mod = 0, key = 0;
  char *tok = strtok(line, " ");
  while (tok) {
    uint8_t m = modKey(tok);
    if (m) mod |= m;
    else {
      uint8_t nk = namedKey(tok);
      if (nk) key = nk;
      else if (strlen(tok) == 1) { bool sh; uint8_t k = asciiToHid(tok[0], &sh); if (k) { key = k; if (sh) mod |= KEY_LSHIFT; } }
    }
    tok = strtok(NULL, " ");
  }
  hidSend(mod, key); // key==0 com mod!=0 pressiona so os modificadores (ex.: GUI)
}

// -------- Setup do teclado HID --------
static void badbleSetupHid() {
  // Remove a ISR do RCSwitch na GPIO 4 antes de ligar o radio BT (mesmo motivo
  // do Blesetup: init do BT desliga o cache da flash; ISR em flash = crash).
  mySwitch.disableReceive();
  detachInterrupt(digitalPinToInterrupt(RX_PIN));

  NimBLEDevice::init(config.deviceName); // nome definido em Config
  NimBLEDevice::setSecurityAuth(true, false, true); // bonding, sem MITM, secure connections

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new BadbleServerCB());

  badHid = new NimBLEHIDDevice(server);
  hidInput = badHid->inputReport(1);
  badHid->manufacturer()->setValue("ScaryRF");
  badHid->pnp(0x02, 0xe502, 0xa111, 0x0210);
  badHid->hidInfo(0x00, 0x01);
  badHid->reportMap((uint8_t *)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
  badHid->startServices();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C1); // HID Keyboard
  adv->addServiceUUID(badHid->hidService()->getUUID());
  adv->start();
}

// -------- Executa um script .txt do SD --------
static void runBadbleScript(const char *path) {
  File f = SD.open(path, FILE_READ);
  if (!f) { showMessage("Open error"); delay(800); return; }

  badbleDefaultDelay = 0;
  int lineNo = 0;
  bool aborted = false;

  while (f.available()) {
    if (btnLeft.isHeld()) { aborted = true; break; }
    if (!badbleConnected) break;

    String ln = f.readStringUntil('\n');
    char buf[256];
    ln.toCharArray(buf, sizeof(buf));
    lineNo++;

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 14); u8g2.print("Running script");
    u8g2.setCursor(0, 30); u8g2.printf("Line %d", lineNo);
    u8g2.setCursor(0, 56); u8g2.print("< Stop");
    u8g2.sendBuffer();

    runDuckyLine(buf);
    if (badbleDefaultDelay) delay(badbleDefaultDelay);
  }
  f.close();

  showMessage(!badbleConnected ? "Disconnected" : (aborted ? "Stopped" : "Script done"));
  delay(900);
}

// -------- Seletor de script (/badusb/*.txt) --------
static bool pickBadbleScript(char *outPath, size_t outN) {
  static const CmdEntry cmds[] = { { CMD_OK, "Run" }, { CMD_DOWN, "Nav" }, { CMD_LEFT, "Exit" } };
  static char names[40][40];
  int count = 0;

  File dir = SD.open("/badusb");
  if (dir && dir.isDirectory()) {
    while (count < 40) {
      File e = dir.openNextFile();
      if (!e) break;
      bool d = e.isDirectory();
      String nm = String(e.name());
      e.close();
      if (d) continue;
      int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
      if ((nm.endsWith(".txt") || nm.endsWith(".TXT")) && !nm.startsWith(".")) {
        nm.toCharArray(names[count], 40); count++;
      }
    }
  }
  dir.close();

  if (count == 0) { showMessage("No scripts", "in /badusb"); delay(1200); return false; }

  int sel = 0, top = 0;
  while (true) {
    if (!badbleConnected) return false;
    if (btnLeft.isPressed()) return false;
    if (btnDown.isPressed()) sel = (sel + 1) % count;
    if (btnUp.isPressed())   sel = (sel - 1 + count) % count;
    if (btnOk.isPressed()) { snprintf(outPath, outN, "/badusb/%s", names[sel]); return true; }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 8); u8g2.print("Bad-BLE scripts");
    const int rows = 3;
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    for (int r = 0; r < rows; r++) {
      int idx = top + r;
      if (idx >= count) break;
      int y = 18 + r * 10;
      u8g2.setCursor(0, y); u8g2.print(idx == sel ? ">" : " ");
      u8g2.setCursor(8, y); u8g2.printf("%.18s", names[idx]);
    }
    drawCommandBar(cmds, 3);
    u8g2.sendBuffer();
    delay(20);
  }
}

// -------- Entrada do modo Bad-BLE --------
void BadBle() {
  static const CmdEntry waitCmds[] = { { CMD_LEFT, "Exit" } };

  if (!sdAvailable) { showMessage("No SD card"); delay(1000); return; }
  SD.mkdir("/badusb");

  badbleConnected = false;
  badbleSetupHid();

  // Espera o host parear/conectar.
  while (!badbleConnected) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 12); u8g2.print("Bad-BLE keyboard");
    u8g2.setCursor(0, 26); u8g2.printf("Pair: %.14s", config.deviceName);
    u8g2.setCursor(0, 40); u8g2.print("Waiting host...");
    drawCommandBar(waitCmds, 1);
    u8g2.sendBuffer();
    if (btnLeft.isPressed()) { NimBLEDevice::deinit(true); return; }
    delay(80);
  }

  // Conectado: escolhe e roda scripts ate sair/desconectar.
  while (badbleConnected) {
    char path[64];
    if (!pickBadbleScript(path, sizeof(path))) break;
    runBadbleScript(path);
  }

  NimBLEDevice::deinit(true);
}

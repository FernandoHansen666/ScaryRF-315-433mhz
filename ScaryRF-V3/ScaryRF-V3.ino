/*
 * ScaryRF — ESP32 + CC1101 sub-GHz / BLE / WiFi research multitool
 * Copyright (C) 2026 FernandoHansen666
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <U8g2lib.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <SPI.h>
#include <SD.h>

#include "scrbtm.h"

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

// ==================== CONFIGURAÇÕES DE PINOS ====================
#define RX_PIN 4
#define TX_PIN 2

// SD (HSPI)
#define SD_CS 17
#define SD_MISO 34
#define SD_MOSI 13
#define SD_SCK 14

// Botoes
#define FREQUENCY_SWITCH_PIN 12
#define BTN_UP     27
#define BTN_LEFT   26
#define BTN_DOWN   25
#define BTN_OK     33
#define BTN_RIGHT  32

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ==================== CONFIGURAÇÕES DE TEMPORIZAÇÃO ====================
#define DEBOUNCE_DELAY 10
#define DISPLAY_UPDATE_INTERVAL 50

// ==================== OBJETOS GLOBAIS ====================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
RCSwitch mySwitch = RCSwitch();
SPIClass hspi(HSPI);

// ==================== ESTADOS DO SISTEMA ====================
enum SystemState {
  STATE_MENU,
  STATE_RF_REPLAY,
  STATE_RF_RAW,
  STATE_RF_ANALYSER,
  STATE_RF_BRUTE,
  STATE_RF_SPECTRUM,
  STATE_RF_SENSOR,
  STATE_RF_TESTSUBS,
  STATE_RF_MANUAL,
  STATE_WIFI_DEAUTH,
  STATE_WIFI_EVIL_PORTAL,
  STATE_BLE_SPAM,
  STATE_BLE_SCAN,
  STATE_BLE_BADBLE,
  STATE_PRESENCE_SCAN,
  STATE_PRESENCE_SPOOF,
  STATE_CONFIG,
  STATE_FOLDERS
};

SystemState currentSystemState = STATE_MENU;

// Modo do BLE spam escolhido no submenu Bluetooth.
enum BleMode { BLE_MODE_APPLE, BLE_MODE_WINDOWS, BLE_MODE_ANDROID, BLE_MODE_SAMSUNG, BLE_MODE_ALL };
int bleSpamMode = BLE_MODE_ALL;

// Tipo de tracker a simular no Presence > Spoof (definido no submenu).
enum PresenceSpoofType { PRESENCE_SPOOF_AIRTAG, PRESENCE_SPOOF_SMARTTAG, PRESENCE_SPOOF_TILE, PRESENCE_SPOOF_ALL };
int presenceSpoofType = PRESENCE_SPOOF_AIRTAG;

// ==================== VARIÁVEIS GLOBAIS DE RF ====================
struct RFData {
  unsigned long receivedValue = 0;
  int receivedBitLength = 0;
  int receivedProtocol = 0;
  int receivedDelay = 0; // pulse length (TE) em us, do RCSwitch
  float mhz = 433.92;
  const int rssi_threshold = -75;
};

RFData rfData;

// ==================== FREQUÊNCIAS SUB-GHZ ====================
static const uint32_t subghz_frequency_list[] = {
  300000000, 303875000, 304250000, 310000000, 315000000, 318000000,
  390000000, 418000000, 433075000, 433420000, 433920000, 434420000,
  434775000, 438900000, 868350000, 915000000, 925000000
};
#define FREQ_LIST_LEN (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]))

// ==================== CONFIGURAÇÃO PERSISTENTE ====================
// Carregada de /scaryrf.cfg no boot. Adicione novos campos aqui conforme
// precisar (a persistencia em arquivo ja cobre todos).
struct Config {
  float mainFreq = 433.92;          // frequencia padrao ao abrir a captura
  char  deviceName[16] = "ScaryRF"; // nome BLE (ex.: teclado do Bad-BLE)
};
Config config;

// Frequencia de trabalho em runtime: comeca na config no boot, e atualizada
// pelo UP nas telas de captura e pelo Analyser (sem mexer no arquivo de config).
float currentFreq = 433.92;

// True se o cartao SD foi inicializado com sucesso. Save/Load/Config exigem SD;
// os demais modos (Replay ao vivo, Raw, Brute, BLE, Deauth) funcionam sem.
bool sdAvailable = false;

// ==================== WAVEFORM ====================
#define WAVEFORM_SAMPLES 128
int waveform[WAVEFORM_SAMPLES] = {0};


// ==================== ESTRUTURA DO MENU ====================
#define ICON_WIDTH 32
#define ICON_HEIGHT 32
#define ICON_GAP 4
#define ICON_Y 32 + ICON_GAP

struct menu_entry_type {
  const uint8_t *font;
  uint16_t icon;
  const char *name;
  const menu_entry_type *submenu;
};

struct menu_state {
  uint8_t position;
  const menu_entry_type *menu;
};

// ==================== SUBMENUS ====================
const menu_entry_type radio_freq_submenu[] = {
  { u8g2_font_open_iconic_all_4x_t, 211, "Replay", NULL },
  { u8g2_font_open_iconic_all_4x_t, 238, "Raw", NULL },
  { u8g2_font_open_iconic_all_4x_t, 165, "Analyser", NULL },
  { u8g2_font_open_iconic_all_4x_t, 193, "Brute", NULL },
  { u8g2_font_open_iconic_all_4x_t, 253, "Spectrum", NULL },
  { u8g2_font_open_iconic_all_4x_t, 152, "Sensors", NULL },
  { u8g2_font_open_iconic_all_4x_t, 256, "Test All", NULL },
  { u8g2_font_open_iconic_all_4x_t, 128, "Manual", NULL },
  { NULL, 0, NULL, NULL }
};

const menu_entry_type wifi_submenu[] = {
  { u8g2_font_open_iconic_all_4x_t, 104, "Evil portal", NULL },
  { u8g2_font_open_iconic_all_4x_t, 87, "Deauther", NULL },
  { u8g2_font_open_iconic_all_4x_t, 175, "Internet", NULL },
  { NULL, 0, NULL, NULL }
};

const menu_entry_type blespam_submenu[] = {
  { u8g2_font_open_iconic_all_4x_t, 94, "Apple", NULL },
  { u8g2_font_open_iconic_all_4x_t, 94, "Windows", NULL },
  { u8g2_font_open_iconic_all_4x_t, 94, "Android", NULL },
  { u8g2_font_open_iconic_all_4x_t, 94, "Samsung", NULL },
  { u8g2_font_open_iconic_all_4x_t, 84, "BLE All", NULL },
  { NULL, 0, NULL, NULL }
};

const menu_entry_type presence_submenu[] = {
  { u8g2_font_open_iconic_all_4x_t, 165, "Scan Tags", NULL },
  { u8g2_font_open_iconic_all_4x_t, 263, "Spoof AirTag", NULL },
  { u8g2_font_open_iconic_all_4x_t, 263, "Spoof Samsung", NULL },
  { u8g2_font_open_iconic_all_4x_t, 263, "Spoof Tile", NULL },
  { u8g2_font_open_iconic_all_4x_t, 263, "Spoof All", NULL },
  { NULL, 0, NULL, NULL }
};

const menu_entry_type bluetooth_submenu[] = {
  { u8g2_font_open_iconic_all_4x_t, 230, "BLE Spam", blespam_submenu },
  { u8g2_font_open_iconic_all_4x_t, 165, "Presence Tags", presence_submenu },
  { u8g2_font_open_iconic_all_4x_t, 207, "BLE Scan", NULL },
  { u8g2_font_open_iconic_all_4x_t, 242, "Bad-BLE", NULL },
  { NULL, 0, NULL, NULL }
};

const menu_entry_type menu_entry_list[] = {
  { u8g2_font_open_iconic_all_4x_t, 84, "RF Tools", radio_freq_submenu },
  { u8g2_font_open_iconic_all_4x_t, 247, "Wifi", wifi_submenu },
  { u8g2_font_open_iconic_all_4x_t, 94, "Bluetooth", bluetooth_submenu },
  { u8g2_font_open_iconic_all_4x_t, 167, "Files", NULL },
  { u8g2_font_open_iconic_all_4x_t, 129, "Config", NULL },
  { NULL, 0, NULL, NULL }
};

menu_state current_state = { 0, menu_entry_list };

// Pilha de navegacao para submenus aninhados (ex.: Bluetooth > BLE Spam > Apple).
// Guarda o menu/posicao do nivel anterior ao descer, para o LEFT voltar certo.
struct MenuFrame { const menu_entry_type *menu; uint8_t pos; };
MenuFrame menuStack[4];
int menuTop = 0;

void menuEnter(const menu_entry_type *sub) {
  if (menuTop < 4) {
    menuStack[menuTop].menu = current_state.menu;
    menuStack[menuTop].pos = current_state.position;
    menuTop++;
  }
  current_state.menu = sub;
  current_state.position = 0;
}

void menuBack() {
  if (menuTop > 0) {
    menuTop--;
    current_state.menu = menuStack[menuTop].menu;
    current_state.position = menuStack[menuTop].pos;
  } else {
    current_state.menu = menu_entry_list;
    current_state.position = 0;
  }
}

// ==================== CLASSE DE DEBOUNCE ====================
class Button {
  private:
    uint8_t pin;
    unsigned long lastDebounceTime = 0;
    bool lastState = HIGH;
    bool currentState = HIGH;

  public:
    Button(uint8_t p) : pin(p) {
      pinMode(pin, INPUT_PULLUP);
    }

    bool isPressed() {
      bool reading = digitalRead(pin);

      if (reading != lastState) {
        lastDebounceTime = millis();
      }

      if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != currentState) {
          currentState = reading;
          lastState = reading;
          return (currentState == LOW);
        }
      }

      lastState = reading;
      return false;
    }

    bool isHeld() {
      return digitalRead(pin) == LOW;
    }
};

// ==================== INSTÂNCIAS DOS BOTÕES ====================
Button btnSwitch(FREQUENCY_SWITCH_PIN); // SEM USO no momento (pino reservado p/ uso futuro)
Button btnUp(BTN_UP);
Button btnLeft(BTN_LEFT);
Button btnDown(BTN_DOWN);
Button btnOk(BTN_OK);
Button btnRight(BTN_RIGHT);

// ==================== FUNÇÕES DE DISPLAY ====================
void displayInit() {
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFlipMode(3);
  u8g2.setFont(u8g2_font_6x10_tf);
}

void drawMenu(menu_state *state) {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < DISPLAY_UPDATE_INTERVAL) return;
  lastUpdate = millis();

  // Conta os itens (fora o terminador NULL) para setas e dots de posicao.
  uint8_t count = 0;
  while (state->menu[count].font != NULL) count++;
  uint8_t i = state->position;

  const int16_t W = u8g2.getDisplayWidth();
  const int16_t x = (W - ICON_WIDTH) / 2;   // icone centralizado
  const int boxTop = 2;
  const int iconBaseY = boxTop + ICON_HEIGHT; // baseline do glyph (32px acima)

  u8g2.clearBuffer();

  // --- Icone central ---
  u8g2.setFont(state->menu[i].font);
  u8g2.drawGlyph(x, iconBaseY, state->menu[i].icon);

  // --- Moldura arredondada ---
  u8g2.drawRFrame(x - 3, boxTop - 1, ICON_WIDTH + 6, ICON_HEIGHT + 4, 5);

  // --- Setas de navegacao (so quando ha para onde ir) ---
  int ay = boxTop + ICON_HEIGHT / 2;        // meio vertical do icone
  if (i > 0)         u8g2.drawTriangle(4, ay, 12, ay - 6, 12, ay + 6);         // ◀
  if (i < count - 1) u8g2.drawTriangle(W - 4, ay, W - 12, ay - 6, W - 12, ay + 6); // ▶

  // --- Rotulo centralizado ---
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor((W - u8g2.getStrWidth(state->menu[i].name)) / 2, 49);
  u8g2.print(state->menu[i].name);

  // --- Dots de posicao na base (cheio = atual) ---
  const int dotGap = 8;
  int dx = (W - (count - 1) * dotGap) / 2;
  for (uint8_t k = 0; k < count; k++) {
    if (k == i) u8g2.drawDisc(dx + k * dotGap, 59, 2);
    else        u8g2.drawCircle(dx + k * dotGap, 59, 2);
  }

  u8g2.sendBuffer();
}

// Submenu em formato de lista vertical, com um icone pequeno (16px) em cada item.
void drawListMenu(menu_state *state) {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < DISPLAY_UPDATE_INTERVAL) return;
  lastUpdate = millis();

  uint8_t count = 0;
  while (state->menu[count].font != NULL) count++;

  const uint8_t rowH = 16;
  const uint8_t visible = SCREEN_HEIGHT / rowH; // 4 linhas cabem na tela

  // Janela de rolagem caso haja mais itens que linhas visiveis
  uint8_t top = 0;
  if (state->position >= visible) top = state->position - visible + 1;

  u8g2.clearBuffer();

  for (uint8_t row = 0; row < visible; row++) {
    uint8_t idx = top + row;
    if (idx >= count) break;

    uint8_t y = row * rowH;
    bool selected = (idx == state->position);

    if (selected) {
      // Linha selecionada: caixa branca com icone e texto em "negativo"
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, y, SCREEN_WIDTH, rowH);
      u8g2.setDrawColor(0);
    } else {
      u8g2.setDrawColor(1);
    }

    // Icone pequeno: mesmo codigo do open_iconic, porem na fonte 2x (16px)
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2.drawGlyph(2, y + 14, state->menu[idx].icon);

    // Rotulo do item
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(22, y + 12, state->menu[idx].name);
  }

  u8g2.setDrawColor(1); // restaura cor padrao para o resto da UI
  u8g2.sendBuffer();
}

// Desenha o menu atual no estilo certo:
// camada 1 (menu_entry_list) = carrossel; demais camadas = lista com icones.
void drawCurrentMenu() {
  if (current_state.menu == menu_entry_list) drawMenu(&current_state);
  else drawListMenu(&current_state);
}

void showMessage(const char* line1, const char* line2 = NULL, const char* line3 = NULL) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf); // garante fonte consistente (ex.: apos o teclado)
  u8g2.setCursor(0, 20);
  u8g2.print(line1);
  if (line2) {
    u8g2.setCursor(0, 35);
    u8g2.print(line2);
  }
  if (line3) {
    u8g2.setCursor(0, 50);
    u8g2.print(line3);
  }
  u8g2.sendBuffer();
}

// Desenha uma seta pequena (6x6) apontando na direcao dada, com o canto
// superior-esquerdo em (x, y), usando a cor de desenho atual.
void drawArrowGlyph(int x, int y, uint8_t dir) {
  switch (dir) {
    case CMD_UP:    u8g2.drawTriangle(x, y + 6, x + 6, y + 6, x + 3, y);     break;
    case CMD_DOWN:  u8g2.drawTriangle(x, y,     x + 6, y,     x + 3, y + 6); break;
    case CMD_LEFT:  u8g2.drawTriangle(x + 6, y, x + 6, y + 6, x, y + 3);     break;
    case CMD_RIGHT: u8g2.drawTriangle(x, y,     x, y + 6,     x + 6, y + 3); break;
    default: break;
  }
}

// Barra de comandos padronizada no rodape: ate 4 itens (seta + rotulo),
// dispostos em 2 colunas x 2 linhas, com separador. O item CMD_OK aparece
// como "OK". Nao limpa nem envia o buffer: e composta junto ao conteudo.
void drawCommandBar(const CmdEntry *cmds, uint8_t count) {
  const uint8_t sepY = 44;
  u8g2.setDrawColor(1);
  u8g2.drawHLine(0, sepY, SCREEN_WIDTH);
  u8g2.setFont(u8g2_font_5x7_tf);

  for (uint8_t k = 0; k < count && k < 4; k++) {
    int x = (k % 2) ? 66 : 2;          // coluna esquerda / direita
    int topY = sepY + 3 + (k / 2) * 9; // linha de cima / de baixo
    int baseY = topY + 7;              // baseline do texto

    if (cmds[k].dir == CMD_OK) {
      u8g2.drawStr(x, baseY, "OK");
      u8g2.drawStr(x + 14, baseY, cmds[k].label);
    } else {
      drawArrowGlyph(x, topY, cmds[k].dir);
      u8g2.drawStr(x + 9, baseY, cmds[k].label);
    }
  }
  u8g2.setFont(u8g2_font_6x10_tf); // restaura a fonte padrao
}

// ==================== SETUP DE MÓDULOS ====================
void setupDisplay() {
  Serial.println(F("Init Display..."));
  displayInit();
  u8g2.drawStr(0, 20, "Display OK!");
  u8g2.sendBuffer();
  delay(500);
}

void setupCC1101() {
  Serial.println(F("Init CC1101..."));

  if (!ELECHOUSE_cc1101.getCC1101()) {
    Serial.println(F("ERROR: CC1101 not found!"));
    showMessage("WARNING:", "CC1101 not found!", "RF disabled");
    delay(1500);
    return; // continua o boot; modos RF ficam indisponiveis, mas BLE/WiFi rodam
  }

  ELECHOUSE_cc1101.Init();
  rfData.mhz = 433.92; // valor inicial; a config define a frequencia de trabalho
  ELECHOUSE_cc1101.setMHZ(rfData.mhz);
  ELECHOUSE_cc1101.SetRx();

  mySwitch.enableReceive(RX_PIN);
  mySwitch.enableTransmit(TX_PIN);

  Serial.println(F("CC1101 OK!"));
  u8g2.drawStr(0, 30, "CC1101 OK!");
  u8g2.sendBuffer();
  delay(500);
}

// ==================== MÁQUINA DE ESTADOS ====================
// Aciona o estado correspondente ao item selecionado de um submenu.
void selectMenuItem(const char* name) {
  if (strcmp(name, "Replay") == 0) currentSystemState = STATE_RF_REPLAY;
  else if (strcmp(name, "Raw") == 0) currentSystemState = STATE_RF_RAW;
  else if (strcmp(name, "Analyser") == 0) currentSystemState = STATE_RF_ANALYSER;
  else if (strcmp(name, "Brute") == 0) currentSystemState = STATE_RF_BRUTE;
  else if (strcmp(name, "Spectrum") == 0) currentSystemState = STATE_RF_SPECTRUM;
  else if (strcmp(name, "Sensors") == 0) currentSystemState = STATE_RF_SENSOR;
  else if (strcmp(name, "Test All") == 0) currentSystemState = STATE_RF_TESTSUBS;
  else if (strcmp(name, "Manual") == 0) currentSystemState = STATE_RF_MANUAL;
  else if (strcmp(name, "Apple") == 0)   { bleSpamMode = BLE_MODE_APPLE;   currentSystemState = STATE_BLE_SPAM; }
  else if (strcmp(name, "Windows") == 0) { bleSpamMode = BLE_MODE_WINDOWS; currentSystemState = STATE_BLE_SPAM; }
  else if (strcmp(name, "Android") == 0) { bleSpamMode = BLE_MODE_ANDROID; currentSystemState = STATE_BLE_SPAM; }
  else if (strcmp(name, "Samsung") == 0) { bleSpamMode = BLE_MODE_SAMSUNG; currentSystemState = STATE_BLE_SPAM; }
  else if (strcmp(name, "BLE All") == 0) { bleSpamMode = BLE_MODE_ALL;     currentSystemState = STATE_BLE_SPAM; }
  else if (strcmp(name, "Scan Tags") == 0)     currentSystemState = STATE_PRESENCE_SCAN;
  else if (strcmp(name, "Spoof AirTag") == 0)  { presenceSpoofType = PRESENCE_SPOOF_AIRTAG;   currentSystemState = STATE_PRESENCE_SPOOF; }
  else if (strcmp(name, "Spoof Samsung") == 0) { presenceSpoofType = PRESENCE_SPOOF_SMARTTAG; currentSystemState = STATE_PRESENCE_SPOOF; }
  else if (strcmp(name, "Spoof Tile") == 0)    { presenceSpoofType = PRESENCE_SPOOF_TILE;     currentSystemState = STATE_PRESENCE_SPOOF; }
  else if (strcmp(name, "Spoof All") == 0)     { presenceSpoofType = PRESENCE_SPOOF_ALL;      currentSystemState = STATE_PRESENCE_SPOOF; }
  else if (strcmp(name, "BLE Scan") == 0)      currentSystemState = STATE_BLE_SCAN;
  else if (strcmp(name, "Bad-BLE") == 0)       currentSystemState = STATE_BLE_BADBLE;
  else if (strcmp(name, "Deauther") == 0) currentSystemState = STATE_WIFI_DEAUTH;
  else if (strcmp(name, "Evil portal") == 0) currentSystemState = STATE_WIFI_EVIL_PORTAL;
}

void handleMenuNavigation() {
  bool inMainMenu = (current_state.menu == menu_entry_list);

  if (inMainMenu) {
    // ----- Camada 1: carrossel. LEFT/RIGHT navega, OK entra. -----
    if (btnRight.isPressed() && current_state.menu[current_state.position + 1].font != NULL) {
      current_state.position++;
      drawCurrentMenu();
    }

    if (btnLeft.isPressed() && current_state.position > 0) {
      current_state.position--;
      drawCurrentMenu();
    }

    if (btnOk.isPressed()) {
      if (current_state.menu[current_state.position].submenu != NULL) {
        menuEnter(current_state.menu[current_state.position].submenu);
        drawCurrentMenu();
      } else if (strcmp(current_state.menu[current_state.position].name, "Config") == 0) {
        currentSystemState = STATE_CONFIG;
      } else if (strcmp(current_state.menu[current_state.position].name, "Files") == 0) {
        currentSystemState = STATE_FOLDERS;
      }
    }
  } else {
    // ----- Submenus: lista. UP/DOWN navega, OK seleciona, LEFT volta. -----
    uint8_t count = 0;
    while (current_state.menu[count].font != NULL) count++;

    if (btnDown.isPressed()) {
      current_state.position = (current_state.position + 1) % count;
      drawCurrentMenu();
    }

    if (btnUp.isPressed()) {
      current_state.position = (current_state.position + count - 1) % count;
      drawCurrentMenu();
    }

    if (btnLeft.isPressed()) {
      menuBack(); // volta um nivel (submenu pai ou carrossel)
      drawCurrentMenu();
      return;
    }

    if (btnOk.isPressed()) {
      if (current_state.menu[current_state.position].submenu != NULL) {
        menuEnter(current_state.menu[current_state.position].submenu); // sub-submenu
        drawCurrentMenu();
      } else {
        selectMenuItem(current_state.menu[current_state.position].name);
      }
    }
  }
}

void executeState() {
  switch (currentSystemState) {
    case STATE_RF_REPLAY:
      Detect();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_RF_RAW:
      Raw();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_RF_ANALYSER:
      Analyser();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_RF_BRUTE:
      DeBruijn();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_RF_SPECTRUM:
      Spectrum();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_RF_SENSOR:
      Sensors();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_RF_TESTSUBS:
      TestSubs();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_RF_MANUAL:
      ManualGen();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_BLE_SPAM:
      Blesetup();
      Bleloop();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_BLE_SCAN:
      BleScanner();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_BLE_BADBLE:
      BadBle();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_PRESENCE_SCAN:
      PresenceScan();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_PRESENCE_SPOOF:
      PresenceSpoof();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_WIFI_DEAUTH:
      Deauther();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_WIFI_EVIL_PORTAL:
      showMessage("In progress...");
      delay(2000);
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_CONFIG:
      ConfigMenu();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_FOLDERS:
      Folders();
      currentSystemState = STATE_MENU;
      drawCurrentMenu();
      break;
    case STATE_MENU:
    default: handleMenuNavigation();
      break;
  }
}

// ==================== SETUP PRINCIPAL ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n\n=== ScaryRF Starting ==="));

  pinMode(FREQUENCY_SWITCH_PIN, INPUT_PULLUP);

  setupDisplay();
  setupCC1101();
  initSD();
  loadConfig(); // carrega /scaryrf.cfg (mainFreq, etc.)
  currentFreq = config.mainFreq; // frequencia de trabalho inicial = config

  // Tela inicial
  u8g2.clearBuffer();
  u8g2.firstPage();
  do {
    u8g2.drawXBMP(0, 0, scary_width, scary_height, scary_bits);
  }
  while (u8g2.nextPage());

  // Aguarda qualquer botão
  while (!btnOk.isPressed() && !btnRight.isPressed() &&
         !btnLeft.isPressed() && !btnUp.isPressed() && !btnDown.isPressed()) delay(10);

  // Desenha o menu principal logo apos sair da tela inicial
  drawCurrentMenu();

  Serial.println(F("Setup complete!"));
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  executeState();
  delay(10);
}

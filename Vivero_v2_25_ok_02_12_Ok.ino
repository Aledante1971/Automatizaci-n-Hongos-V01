// --- START OF FILE Vivero_v3_06_LinkerFix_XX_XX.txt --- // (Moved functions to fix linker error)

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TaskScheduler.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Adafruit_AHTX0.h>
#include <AdafruitIO_WiFi.h>
#include <WiFi.h>
#include <ArduinoJson.h> // Needed for /data endpoint

// -------------------------------------------------------
// Credenciales de WiFi y Adafruit IO (Defaults)
#define DEFAULT_WIFI_SSID "TU_RED"
#define DEFAULT_WIFI_PASSWORD "TU_PASSWORD"
#define IO_USERNAME "USUARIO_ADAFRUIT"
#define IO_KEY "TUCLAVEADAFRUIT"

// -------------------------------------------------------
// Adafruit IO Initialization
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);
AdafruitIO_Feed *temperaturaFeed = io.feed("esp32_temperatura");
AdafruitIO_Feed *humedadFeed    = io.feed("esp32_humedad");
AdafruitIO_Feed *logsFeed       = io.feed("esp32_logs");

// -------------------------------------------------------
// Pin Definitions
#define RELE_VENTILACION         13   // Active HIGH
#define RELE_HUMIDIFICADOR       26   // Active LOW
#define RELE_CALEFACCION         14   // Active LOW
#define RELE_VENTILADOR_INTERNO  12   // Active HIGH

#define PIN_SDA                  33
#define PIN_SCL                  32
#define BOTON_BOOT               0    // Button on GPIO 0

#define DIRECCION_LCD            0x3F

// -------------------------------------------------------
// Peripheral Initialization
LiquidCrystal_I2C lcd(DIRECCION_LCD, 16, 2);
Scheduler planificador;
Preferences preferences;
Adafruit_AHTX0 aht;

WiFiUDP ntpUDP;
const long utcOffsetInSeconds = -10800; // UTC-3 Argentina
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds, 60000);

// -------------------------------------------------------
// Web Server
AsyncWebServer server(80);

// -------------------------------------------------------
// Global Sensor & Config Variables
float humedad = 0.0;
float temperatura = 0.0;
float temperaturaMin = 100.0;
float temperaturaMax = -100.0;
float humedadMin = 100.0;
float humedadMax = 0.0;

float temperaturaDeseada = 24.0;
float margenTemperatura = 2.0;
float humedadDeseada = 90.0;
float margenHumedad = 5.0;

float intervaloVentilacion = 3600.0; // seconds
unsigned long duracionVentilacion = 300; // seconds

float intervaloVentiladorInterno = 360.0;  // seconds
unsigned long duracionVentiladorInterno = 180;   // seconds

// -------------------------------------------------------
// Timer Variables - External Ventilation
unsigned long ultimoTiempoVentilacion = 0; // Stores millis() when the timer cycle STARTs
bool enPeriodoVentilacion = false; // Tracks if timer logic wants the fan ON (duration phase)

// Timer Variables - Internal Fan
unsigned long ultimoTiempoVentiladorInterno = 0; // Stores millis() when the timer cycle STARTs
bool enPeriodoVentiladorInterno = false; // Tracks if timer logic wants the fan ON (duration phase)

// -------------------------------------------------------
// Logging Variables
#define INTERVALO_REGISTRO 600000      // 10 min in ms
#define REGISTROS_7_DIAS   (7 * 24 * 6)

struct Registro { float temperatura; float humedad; time_t timestamp; };
Registro registros[REGISTROS_7_DIAS];
int indiceRegistro = 0;

String ultimoLog = "";
String ultimosLogs[4] = { "", "", "", "" };
int indiceLog = 0;

// -------------------------------------------------------
// Display & Button Variables
unsigned long tiempoInicioLCD = 0;
bool lcdEncendido = true;
int displayPage = 0;
const int NUM_DISPLAY_PAGES = 4;

// -------------------------------------------------------
// EMA Variables
float EMA_Calefaccion_Hora = 0, EMA_Calefaccion_Dia = 0;
float EMA_Humidificador_Hora = 0, EMA_Humidificador_Dia = 0;
float EMA_Ventilacion_Hora  = 0, EMA_Ventilacion_Dia  = 0;
float EMA_VentiladorInterno_Hora = 0, EMA_VentiladorInterno_Dia = 0;

const float alphaHora = 2.0 / (360.0 + 1.0);
const float alphaDia  = 2.0 / (8640.0 + 1.0);

// -------------------------------------------------------
// Override Modes
#define MODE_AUTO 0
#define MODE_ON   1
#define MODE_OFF  2

int overrideCalefaccion        = MODE_AUTO;
int overrideHumidificador      = MODE_AUTO;
int overrideVentilacion        = MODE_AUTO;
int overrideVentiladorInterno  = MODE_AUTO;

// -------------------------------------------------------
// Relay State Variables
bool estadoCalefaccion       = false;
bool estadoHumidificador     = false;
bool estadoVentilacion       = false;
bool estadoVentiladorInterno = false;

// -------------------------------------------------------
// WiFi AP Mode Variable
bool enModoAP = false;

// ============================================================
// START: Moved function definitions before Task initializers to fix Linker Error
// ============================================================

//
// Guardar registro periódico
//
void guardarRegistro() {
  if (timeStatus() == timeSet) {
    registros[indiceRegistro].temperatura = temperatura;
    registros[indiceRegistro].humedad = humedad;
    registros[indiceRegistro].timestamp = now();
    indiceRegistro = (indiceRegistro + 1) % REGISTROS_7_DIAS;
  } else {
    Serial.println("Hora no sincronizada, registro omitido.");
  }
}

//
// Enviar datos a Adafruit IO
//
void enviarDatosAdafruit() {
  if (io.status() >= AIO_CONNECTED) {
      temperaturaFeed->save(temperatura);
      humedadFeed->save(humedad);
      logsFeed->save(ultimoLog);
  }
}

// ============================================================
// END: Moved function definitions
// ============================================================


// -------------------------------------------------------
// Function Prototypes
void actualizarPantalla();
void controlarSistema();
void iniciarServidorWeb();
void cargarConfiguraciones();
// void guardarRegistro(); // Prototype no longer strictly needed before Task definition
time_t getNtpTime();
void calcularEMA();
void guardarLog(String log); // Definition needed for lambda task
void manejarBoton();
// void enviarDatosAdafruit(); // Prototype no longer strictly needed before Task definition
void gestionarWiFi();
void printLCD(int line, const char *fmt, ...);


// -------------------------------------------------------
// Tasks
Task tareaActualizarPantalla(1000, TASK_FOREVER, &actualizarPantalla);
Task tareaControlarSistema(5000, TASK_FOREVER, &controlarSistema);
Task tareaGuardarRegistro(INTERVALO_REGISTRO, TASK_FOREVER, &guardarRegistro); // Now finds the definition above
Task tareaImprimirLog(15000, TASK_FOREVER, []() { // Log to Serial/Adafruit IO every 15s
  String log = String(hour()) + ":" + String(minute()) + ":" + String(second()) +
               " | T:" + String(temperatura, 1) + "/" + String(temperaturaDeseada, 1) +
               " H:" + String(humedad, 1) + "/" + String(humedadDeseada, 1) +
               " | R:" +
               (estadoCalefaccion ? "C" : "c") +
               (estadoHumidificador ? "H" : "h") +
               (estadoVentilacion ? "V" : "v") +
               (estadoVentiladorInterno ? "I" : "i") +
               " | O:" +
               (overrideCalefaccion == MODE_AUTO ? 'A' : (overrideCalefaccion == MODE_ON ? '1' : '0')) +
               (overrideHumidificador == MODE_AUTO ? 'A' : (overrideHumidificador == MODE_ON ? '1' : '0')) +
               (overrideVentilacion == MODE_AUTO ? 'A' : (overrideVentilacion == MODE_ON ? '1' : '0')) +
               (overrideVentiladorInterno == MODE_AUTO ? 'A' : (overrideVentiladorInterno == MODE_ON ? '1' : '0')) +
               " | EMA_H:" +
               " C:" + String(EMA_Calefaccion_Hora * 100, 0) + "%" +
               " H:" + String(EMA_Humidificador_Hora * 100, 0) + "%" +
               " V:" + String(EMA_Ventilacion_Hora * 100, 0) + "%" +
               " I:" + String(EMA_VentiladorInterno_Hora * 100, 0) + "%";
  Serial.println(log);
  ultimoLog = log;
  guardarLog(log); // Save to circular buffer for web UI - Finds definition below
});
Task tareaManejarBoton(100, TASK_FOREVER, &manejarBoton);
Task tareaCalcularEMA(10000, TASK_FOREVER, &calcularEMA);
Task tareaEnviarDatosAdafruit(60000, TASK_FOREVER, &enviarDatosAdafruit); // Now finds the definition above
Task tareaGestionWiFi(60000, TASK_FOREVER, &gestionarWiFi);


//
// setup()
//
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("\n\n--- Iniciando Sistema Vivero ---");

  Wire.begin(PIN_SDA, PIN_SCL);
  lcd.init();
  lcd.backlight();
  printLCD(0, "Iniciando...");
  Serial.println("LCD inicializado.");

  printLCD(1, "Buscando AHT...");
  Serial.print("Inicializando sensor AHT... ");
  if (!aht.begin()) {
    Serial.println("¡ERROR! No se encontró AHT10/AHT20.");
    printLCD(0, "ERROR Sensor!");
    printLCD(1, "Revisar conexion");
    while (1) delay(10);
  } else {
    Serial.println("OK.");
    printLCD(1, "Sensor OK.");
    delay(1000);
  }

  pinMode(RELE_VENTILACION, OUTPUT);
  pinMode(RELE_HUMIDIFICADOR, OUTPUT);
  pinMode(RELE_CALEFACCION, OUTPUT);
  pinMode(RELE_VENTILADOR_INTERNO, OUTPUT);
  pinMode(BOTON_BOOT, INPUT_PULLUP);

  digitalWrite(RELE_VENTILACION, LOW);
  digitalWrite(RELE_HUMIDIFICADOR, HIGH);
  digitalWrite(RELE_CALEFACCION, HIGH);
  digitalWrite(RELE_VENTILADOR_INTERNO, LOW);

  preferences.begin("config", false);
  cargarConfiguraciones(); // Load settings first, as they contain interval/duration
  preferences.end();

  String ssid = preferences.getString("wifiSSID", DEFAULT_WIFI_SSID);
  String pass = preferences.getString("wifiPASS", DEFAULT_WIFI_PASSWORD);

  printLCD(0, "Conectando WiFi");
  Serial.print("Conectando a WiFi SSID: " + ssid + "... ");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long wifiStartTime = millis();
  bool connected = false;
  while (millis() - wifiStartTime < 30000) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    Serial.print(".");
    printLCD(1, "%lu s", (millis() - wifiStartTime) / 1000);
    delay(500);
  }

  if (connected) {
    Serial.println(" ¡Conectado!");
    String ip = WiFi.localIP().toString();
    Serial.println("IP: " + ip);
    printLCD(0, "WiFi OK!");
    printLCD(1, "%s", ip.c_str());
    delay(1500);

    printLCD(0, "Conectando AIO...");
    Serial.print("Conectando a Adafruit IO... ");
    io.connect();
    wifiStartTime = millis();
    while(io.status() < AIO_CONNECTED && millis() - wifiStartTime < 15000) {
        Serial.print("*");
        delay(500);
    }
    if(io.status() >= AIO_CONNECTED) {
        Serial.println(" ¡Conectado!");
        printLCD(1, "AIO OK!");
    } else {
        Serial.println(" ¡Fallo AIO!");
        printLCD(1, "AIO Fallo");
    }
  } else {
    Serial.println(" ¡Fallo WiFi!");
    printLCD(0, "Fallo WiFi");
    printLCD(1, "Modo Local/AP");
    gestionarWiFi();
  }
  delay(2000);

  iniciarServidorWeb(); // Call this AFTER WiFi setup
  Serial.println("Servidor Web iniciado.");

  printLCD(0, "Sincronizando...");
  printLCD(1, "Hora NTP");
  Serial.print("Sincronizando hora NTP... ");
  timeClient.begin();
  if (timeClient.forceUpdate()) {
      setTime(timeClient.getEpochTime());
      setSyncProvider(getNtpTime);
      setSyncInterval(3600);
      Serial.println("OK. Hora: " + String(hour()) + ":" + String(minute()));
      printLCD(1, "Hora OK");
  } else {
      Serial.println("¡Fallo NTP!");
      printLCD(1, "Fallo NTP");
  }
   delay(1500);

  EMA_Calefaccion_Hora = 0.0; EMA_Calefaccion_Dia = 0.0;
  EMA_Humidificador_Hora = 0.0; EMA_Humidificador_Dia = 0.0;
  EMA_Ventilacion_Hora = 0.0; EMA_Ventilacion_Dia = 0.0;
  EMA_VentiladorInterno_Hora = 0.0; EMA_VentiladorInterno_Dia = 0.0;

  planificador.addTask(tareaActualizarPantalla);
  planificador.addTask(tareaControlarSistema);
  planificador.addTask(tareaGuardarRegistro);
  planificador.addTask(tareaImprimirLog);
  planificador.addTask(tareaManejarBoton);
  planificador.addTask(tareaCalcularEMA);
  planificador.addTask(tareaEnviarDatosAdafruit);
  planificador.addTask(tareaGestionWiFi);

  tareaActualizarPantalla.enable();
  tareaControlarSistema.enable();
  tareaGuardarRegistro.enable();
  tareaImprimirLog.enable();
  tareaManejarBoton.enable();
  tareaCalcularEMA.enable();
  tareaEnviarDatosAdafruit.enable();
  tareaGestionWiFi.enable();

  printLCD(0, "Sistema Listo");
  printLCD(1, "");
  Serial.println("--- Sistema Iniciado Correctamente ---");

  // Initialize timer reference points
  unsigned long bootMillis = millis();
  ultimoTiempoVentiladorInterno = bootMillis - (duracionVentiladorInterno * 1000UL);
  ultimoTiempoVentilacion = bootMillis - (duracionVentilacion * 1000UL);
  Serial.printf("Timer Init @ %lu ms: VentInt LastStart Ref=%lu, VentExt LastStart Ref=%lu\n",
                 bootMillis, ultimoTiempoVentiladorInterno, ultimoTiempoVentilacion);

  tiempoInicioLCD = millis(); // Start LCD timeout timer AFTER initializing timers
} // End of setup()


//
// loop()
//
void loop() {
  planificador.execute();
  io.run();
}

// Helper function for LCD printing
void printLCD(int line, const char *fmt, ...) {
    char buf[17];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lcd.setCursor(0, line);
    lcd.print(buf);
    for (int i = strlen(buf); i < 16; i++) {
        lcd.print(" ");
    }
}


// ============================================================
// START: Clean Version of actualizarPantalla
// ============================================================
void actualizarPantalla() {
  if (millis() - tiempoInicioLCD >= 30000) {
     if (lcdEncendido) {
        lcdEncendido = false;
        displayPage = 0;
        lcd.noBacklight();
        return;
     } else {
        return;
     }
  }
  if (!lcdEncendido) {
      lcd.noBacklight();
      return;
  }
  lcd.backlight();
  switch(displayPage) {
     case 0: {
      char cState = estadoCalefaccion ? 'C' : 'c'; char vState = estadoVentilacion ? 'V' : 'v'; char hState = estadoHumidificador ? 'H' : 'h'; char iState = estadoVentiladorInterno ? 'I' : 'i';
      printLCD(0, "D%.1f T%.1f %c%c%c%c", temperaturaDeseada, temperatura, cState, vState, hState, iState);
      char cOvr = (overrideCalefaccion == MODE_AUTO) ? 'A' : ((overrideCalefaccion == MODE_ON) ? '1' : '0'); char hOvr = (overrideHumidificador == MODE_AUTO) ? 'A' : ((overrideHumidificador == MODE_ON) ? '1' : '0'); char vOvr = (overrideVentilacion == MODE_AUTO) ? 'A' : ((overrideVentilacion == MODE_ON) ? '1' : '0'); char iOvr = (overrideVentiladorInterno == MODE_AUTO) ? 'A' : ((overrideVentiladorInterno == MODE_ON) ? '1' : '0');
      printLCD(1, "HD%.0f H%.0f %c%c%c%c", humedadDeseada, humedad, cOvr, hOvr, vOvr, iOvr); break;
    }
    case 1: { printLCD(0, "TMax: %.1f C", temperaturaMax); printLCD(1, "TMin: %.1f C", temperaturaMin); break; }
    case 2: { printLCD(0, "HMax: %.1f %%", humedadMax); printLCD(1, "HMin: %.1f %%", humedadMin); break; }
    case 3: { printLCD(0, "C:%.0f%% H:%.0f%%", EMA_Calefaccion_Hora*100, EMA_Humidificador_Hora*100); printLCD(1, "V:%.0f%% I:%.0f%%", EMA_Ventilacion_Hora*100, EMA_VentiladorInterno_Hora*100); break; }
  }
}
// ============================================================
// END: Clean Version of actualizarPantalla
// ============================================================


// ============================================================
// START: Clean Version of manejarBoton (with corrected debounce)
// ============================================================
void manejarBoton() {
  const unsigned long debounceDelay = 50;
  static unsigned long lastDebounceTime = 0;
  static int buttonState = HIGH;
  static int lastButtonState = HIGH;

  int reading = digitalRead(BOTON_BOOT);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        if (!lcdEncendido) {
            lcdEncendido = true;
            lcd.backlight();
            tiempoInicioLCD = millis();
        } else {
             lcdEncendido = true;
             lcd.backlight();
             displayPage = (displayPage + 1) % NUM_DISPLAY_PAGES;
             tiempoInicioLCD = millis();
        }
        tareaActualizarPantalla.forceNextIteration();
      }
    }
  }
  lastButtonState = reading;
}
// ============================================================
// END: Clean Version of manejarBoton
// ============================================================


// ============================================================
// START: controlarSistema function with corrected timer logic
// ============================================================
void controlarSistema() {
  // 1. Leer Sensores
  sensors_event_t humedadEvento, tempEvento;
  if (aht.getEvent(&humedadEvento, &tempEvento)) {
    temperatura = tempEvento.temperature;
    humedad = humedadEvento.relative_humidity;
    if (temperatura > temperaturaMax) temperaturaMax = temperatura;
    if (temperatura < temperaturaMin) temperaturaMin = temperatura;
    if (humedad > humedadMax) humedadMax = humedad;
    if (humedad < humedadMin) humedadMin = humedad;
  } else {
    Serial.println("Error al leer el sensor AHT10/AHT20");
  }

  unsigned long currentMillis = millis();

  // 2. Controlar Relés (con Overrides)

  // --- Control de CALEFACCION ---
  bool calefaccionNecesaria = (temperatura < (temperaturaDeseada - margenTemperatura));
  bool calefaccionApagar = (temperatura >= temperaturaDeseada);
  if (overrideCalefaccion == MODE_ON) estadoCalefaccion = true;
  else if (overrideCalefaccion == MODE_OFF) estadoCalefaccion = false;
  else { if (calefaccionNecesaria) estadoCalefaccion = true; else if (calefaccionApagar) estadoCalefaccion = false; }
  digitalWrite(RELE_CALEFACCION, estadoCalefaccion ? LOW : HIGH);

  // --- Control de HUMIDIFICADOR ---
  bool humidificadorNecesario = (humedad < (humedadDeseada - margenHumedad));
  bool humidificadorApagar = (humedad >= humedadDeseada);
  if (overrideHumidificador == MODE_ON) estadoHumidificador = true;
  else if (overrideHumidificador == MODE_OFF) estadoHumidificador = false;
  else { if (humidificadorNecesario) estadoHumidificador = true; else if (humidificadorApagar) estadoHumidificador = false; }
  digitalWrite(RELE_HUMIDIFICADOR, estadoHumidificador ? LOW : HIGH);

  // --- Control de VENTILADOR INTERNO (Lógica de Intervalo Corregida) ---
  bool ventiladorInternoTimer = false; // Variable to track if the timer *logic* wants the fan ON
  if (!enPeriodoVentiladorInterno) { // If currently in the OFF period
      // Condition to START a NEW cycle: Interval time must have passed since the beginning of the PREVIOUS ON period.
      if (currentMillis - ultimoTiempoVentiladorInterno >= (unsigned long)(intervaloVentiladorInterno * 1000.0f)) {
          enPeriodoVentiladorInterno = true;          // Start the ON period
          ultimoTiempoVentiladorInterno = currentMillis; // Record the NEW start time of this ON period
          ventiladorInternoTimer = true;              // Timer logic requires ON
          Serial.println("VENT_INT: Iniciando NUEVO ciclo por intervalo."); // DEBUG
      } else {
          ventiladorInternoTimer = false;             // Timer logic requires OFF
      }
  } else { // If currently in the ON period
      // Check if the ON DURATION has elapsed since this ON period started
      if (currentMillis - ultimoTiempoVentiladorInterno >= (duracionVentiladorInterno * 1000UL)) {
          enPeriodoVentiladorInterno = false;         // End the ON period
          ventiladorInternoTimer = false;             // Timer logic requires OFF
          Serial.println("VENT_INT: Finalizando ciclo por duración."); // DEBUG
      } else {
          ventiladorInternoTimer = true;              // Timer logic requires ON
      }
  }

  // --- Control de VENTILACION EXTERNA (Lógica de Intervalo Corregida) ---
  bool ventilacionExternaTimer = false; // Variable to track if the timer *logic* wants the fan ON
   if (!enPeriodoVentilacion) { // If currently in the OFF period (from timer's perspective)
      // Condition to START a NEW cycle by timer: Interval time must have passed since the start of the previous timer cycle
      // AND the humidity condition must be met
      if ((currentMillis - ultimoTiempoVentilacion >= (unsigned long)(intervaloVentilacion * 1000.0f)) &&
          (humedad > (humedadDeseada + margenHumedad))) {
          enPeriodoVentilacion = true;            // Start the ON period (timer initiated)
          ultimoTiempoVentilacion = currentMillis; // Record the NEW start time of this timer-initiated ON period
          ventilacionExternaTimer = true;         // Timer logic requires ON
          Serial.println("VENT_EXT: Iniciando NUEVO ciclo por timer/intervalo/humedad."); // DEBUG
      } else {
          ventilacionExternaTimer = false;        // Timer logic requires OFF
      }
  } else { // If currently in the ON period (initiated by timer previously)
       // Check if the ON DURATION for the timer cycle has elapsed since it started
      if (currentMillis - ultimoTiempoVentilacion >= (duracionVentilacion * 1000UL)) {
          enPeriodoVentilacion = false;         // End the timer-initiated ON period
          ventilacionExternaTimer = false;        // Timer logic requires OFF
          Serial.println("VENT_EXT: Timer finalizando ciclo por duración."); // DEBUG
      } else {
          ventilacionExternaTimer = true;         // Timer logic requires ON
      }
  }

  // --- Final state determination for VENTILACION EXTERNA ---
  if (overrideVentilacion == MODE_ON) {
      estadoVentilacion = true;
  } else if (overrideVentilacion == MODE_OFF) {
      estadoVentilacion = false;
  } else { // MODE_AUTO
      bool humidificadorFuerzaVentilacion = (overrideHumidificador == MODE_AUTO && estadoHumidificador);
      estadoVentilacion = ventilacionExternaTimer || humidificadorFuerzaVentilacion;
  }
  digitalWrite(RELE_VENTILACION, estadoVentilacion ? HIGH : LOW);

  // --- Final state determination for VENTILADOR INTERNO ---
  if (overrideVentiladorInterno == MODE_ON) {
    estadoVentiladorInterno = true;
  } else if (overrideVentiladorInterno == MODE_OFF) {
    estadoVentiladorInterno = false;
  } else { // MODE_AUTO
    estadoVentiladorInterno = ventiladorInternoTimer;
  }
  digitalWrite(RELE_VENTILADOR_INTERNO, estadoVentiladorInterno ? HIGH : LOW);

} // End of controlarSistema function
// ============================================================
// END: controlarSistema function with corrected timer logic
// ============================================================


// ============================================================
// START: Updated iniciarServidorWeb function (includes /data and redirects)
// ============================================================
void iniciarServidorWeb() {

  // --- Main Page Route ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
<!DOCTYPE html><html lang="es"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>Panel de Control Vivero</title>
<style>body{background-color:#282c34;color:#abb2bf;font-family:sans-serif;margin:0;padding:15px}h1,h2,h3{color:#61afef;text-align:center}.container,.form-container{max-width:800px;margin:20px auto;padding:15px;background-color:#3b4048;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,.2)}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:15px}.box{text-align:center;padding:15px;background-color:#454c57;border-radius:5px}.large-number{font-size:2.2em;font-weight:700;color:#98c379;margin:5px 0}.label{font-size:.9em;color:#abb2bf}.minmax{font-size:.8em;color:#c678dd}form{margin-top:15px}.form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:15px;margin-bottom:15px}.form-grid div label{display:block;margin-bottom:5px;font-weight:700;color:#e5c07b}input[type=number],input[type=text],select{width:100%;padding:8px;box-sizing:border-box;background-color:#3b4048;color:#abb2bf;border:1px solid #61afef;border-radius:4px}input[type=submit],.button{background-color:#61afef;color:#282c34;padding:10px 15px;border:none;border-radius:5px;cursor:pointer;font-weight:700;text-decoration:none;display:inline-block;margin-top:10px}input[type=submit]:hover,.button:hover{background-color:#5295cc}.logs{margin-top:20px}.log-entry{background-color:#454c57;padding:8px;margin-bottom:5px;border-radius:4px;font-size:.85em;font-family:monospace;word-wrap:break-word}.status{margin-top:15px;text-align:center;font-size:.9em}a{color:#61afef}</style>
</head><body><h1>Control Vivero</h1>
<div class="container"><h2>Estado Actual</h2><div class="grid">
<div class="box"><span class="label">Temperatura (°C)</span><p class="large-number" id="temp_value">%TEMPERATURA%</p><p class="minmax" id="temp_minmax">Min: %TEMP_MIN% / Max: %TEMP_MAX%</p></div>
<div class="box"><span class="label">Humedad (%)</span><p class="large-number" id="hum_value">%HUMEDAD%</p><p class="minmax" id="hum_minmax">Min: %HUM_MIN% / Max: %HUM_MAX%</p></div>
<div class="box"><span class="label">Relés (C H V I)</span><p class="large-number" id="rele_estados">%RELE_ESTADOS%</p><p class="minmax" id="rele_overrides">Overrides: %RELE_OVERRIDES%</p></div>
</div><div style="text-align:center;margin-top:10px"><a href="/resetminmax" class="button" onclick="return confirm('¿Reiniciar valores Mín/Máx?');">Reset Min/Max</a></div></div>
<div class="form-container"><form action="/guardar" method="POST"><h3>Configuración de Clima y Timers</h3><div class="form-grid"><div><label>Temp. Deseada (°C):</label><input type="number" name="tempDeseada" step="0.1" value="%TEMP_DESEADA%"></div><div><label>Margen Temp. (°C):</label><input type="number" name="margenTemp" step="0.1" value="%MARGEN_TEMP%"></div><div><label>Hum. Deseada (%):</label><input type="number" name="humDeseada" step="0.1" value="%HUM_DESEADA%"></div><div><label>Margen Hum. (%):</label><input type="number" name="margenHum" step="0.1" value="%MARGEN_HUM%"></div><div><label>Intervalo Vent. Ext (Horas):</label><input type="number" name="intervaloVent" step="0.1" value="%INTERVALO_VENT%"></div><div><label>Duración Vent. Ext (Minutos):</label><input type="number" name="duracionVent" step="1" value="%DURACION_VENT%"></div><div><label>Intervalo Vent. Int (Horas):</label><input type="number" name="intervaloVentInt" step="0.1" value="%INTERVALO_VENT_INT%"></div><div><label>Duración Vent. Int (Minutos):</label><input type="number" name="duracionVentInt" step="1" value="%DURACION_VENT_INT%"></div></div><input type="submit" value="Guardar Configuración"></form></div>
<div class="form-container"><form action="/override" method="POST"><h3>Control Manual (Overrides)</h3><div class="form-grid"><div><label>Calefacción:</label><select name="ovr_calefaccion"><option value="0" %OVR_CAL_AUTO%>Auto</option><option value="1" %OVR_CAL_ON%>ON (1)</option><option value="2" %OVR_CAL_OFF%>OFF (0)</option></select></div><div><label>Humidificador:</label><select name="ovr_humidificador"><option value="0" %OVR_HUM_AUTO%>Auto</option><option value="1" %OVR_HUM_ON%>ON (1)</option><option value="2" %OVR_HUM_OFF%>OFF (0)</option></select></div><div><label>Ventilación Externa:</label><select name="ovr_ventilacion"><option value="0" %OVR_VEN_AUTO%>Auto</option><option value="1" %OVR_VEN_ON%>ON (1)</option><option value="2" %OVR_VEN_OFF%>OFF (0)</option></select></div><div><label>Ventilador Interno:</label><select name="ovr_ventilador"><option value="0" %OVR_VENINT_AUTO%>Auto</option><option value="1" %OVR_VENINT_ON%>ON (1)</option><option value="2" %OVR_VENINT_OFF%>OFF (0)</option></select></div></div><input type="submit" value="Aplicar Overrides"></form></div>
<div class="container logs"><h3>Últimos Logs del Sistema</h3><div id="log_entries">%ULTIMOS_LOGS%</div></div>
<div class="status"><p>WiFi: <span id="wifi_status">%WIFI_STATUS%</span> | IP: <span id="wifi_ip">%WIFI_IP%</span> | <a href="/configwifi">Configurar WiFi</a></p><p>Adafruit IO: <span id="aio_status">%AIO_STATUS%</span></p></div>

<script>
function updateData() {
  fetch('/data')
    .then(response => { if (!response.ok) { throw new Error('Network response was not ok ' + response.statusText); } return response.json(); })
    .then(data => {
      document.getElementById('temp_value').textContent = data.temp.toFixed(1); document.getElementById('temp_minmax').textContent = 'Min: ' + data.temp_min.toFixed(1) + ' / Max: ' + data.temp_max.toFixed(1);
      document.getElementById('hum_value').textContent = data.hum.toFixed(1); document.getElementById('hum_minmax').textContent = 'Min: ' + data.hum_min.toFixed(1) + ' / Max: ' + data.hum_max.toFixed(1);
      let releEstados = (data.r_c ? 'C' : 'c') + (data.r_h ? 'H' : 'h') + (data.r_v ? 'V' : 'v') + (data.r_i ? 'I' : 'i'); document.getElementById('rele_estados').textContent = releEstados;
      let releOverrides = (data.o_c == 0 ? 'A' : (data.o_c == 1 ? '1' : '0')) + (data.o_h == 0 ? 'A' : (data.o_h == 1 ? '1' : '0')) + (data.o_v == 0 ? 'A' : (data.o_v == 1 ? '1' : '0')) + (data.o_i == 0 ? 'A' : (data.o_i == 1 ? '1' : '0')); document.getElementById('rele_overrides').textContent = 'Overrides: ' + releOverrides;
      document.getElementById('wifi_status').textContent = data.wifi_status; document.getElementById('wifi_ip').textContent = data.wifi_ip; document.getElementById('aio_status').textContent = data.aio_status;
    })
    .catch(error => { console.error('Error fetching data:', error); });
}
setInterval(updateData, 5000); // Update every 5 seconds
</script>

</body></html>
    )rawliteral";

    // Replace placeholders for initial load
    html.replace("%TEMPERATURA%", String(temperatura, 1)); html.replace("%TEMP_MIN%", String(temperaturaMin, 1)); html.replace("%TEMP_MAX%", String(temperaturaMax, 1));
    html.replace("%HUMEDAD%", String(humedad, 1)); html.replace("%HUM_MIN%", String(humedadMin, 1)); html.replace("%HUM_MAX%", String(humedadMax, 1));
    String releEstados = ""; releEstados += estadoCalefaccion ? "C" : "c"; releEstados += estadoHumidificador ? "H" : "h"; releEstados += estadoVentilacion ? "V" : "v"; releEstados += estadoVentiladorInterno ? "I" : "i"; html.replace("%RELE_ESTADOS%", releEstados);
    String releOverrides = ""; releOverrides += (overrideCalefaccion == MODE_AUTO ? 'A' : (overrideCalefaccion == MODE_ON ? '1' : '0')); releOverrides += (overrideHumidificador == MODE_AUTO ? 'A' : (overrideHumidificador == MODE_ON ? '1' : '0')); releOverrides += (overrideVentilacion == MODE_AUTO ? 'A' : (overrideVentilacion == MODE_ON ? '1' : '0')); releOverrides += (overrideVentiladorInterno == MODE_AUTO ? 'A' : (overrideVentiladorInterno == MODE_ON ? '1' : '0')); html.replace("%RELE_OVERRIDES%", releOverrides);
    html.replace("%TEMP_DESEADA%", String(temperaturaDeseada, 1)); html.replace("%MARGEN_TEMP%", String(margenTemperatura, 1));
    html.replace("%HUM_DESEADA%", String(humedadDeseada, 1)); html.replace("%MARGEN_HUM%", String(margenHumedad, 1));
    html.replace("%INTERVALO_VENT%", String(intervaloVentilacion / 3600.0, 1)); html.replace("%DURACION_VENT%", String(duracionVentilacion / 60));
    html.replace("%INTERVALO_VENT_INT%", String(intervaloVentiladorInterno / 3600.0, 1)); html.replace("%DURACION_VENT_INT%", String(duracionVentiladorInterno / 60));
    html.replace("%OVR_CAL_AUTO%", (overrideCalefaccion == MODE_AUTO ? "selected" : "")); html.replace("%OVR_CAL_ON%", (overrideCalefaccion == MODE_ON ? "selected" : "")); html.replace("%OVR_CAL_OFF%", (overrideCalefaccion == MODE_OFF ? "selected" : ""));
    html.replace("%OVR_HUM_AUTO%", (overrideHumidificador == MODE_AUTO ? "selected" : "")); html.replace("%OVR_HUM_ON%", (overrideHumidificador == MODE_ON ? "selected" : "")); html.replace("%OVR_HUM_OFF%", (overrideHumidificador == MODE_OFF ? "selected" : ""));
    html.replace("%OVR_VEN_AUTO%", (overrideVentilacion == MODE_AUTO ? "selected" : "")); html.replace("%OVR_VEN_ON%", (overrideVentilacion == MODE_ON ? "selected" : "")); html.replace("%OVR_VEN_OFF%", (overrideVentilacion == MODE_OFF ? "selected" : ""));
    html.replace("%OVR_VENINT_AUTO%", (overrideVentiladorInterno == MODE_AUTO ? "selected" : "")); html.replace("%OVR_VENINT_ON%", (overrideVentiladorInterno == MODE_ON ? "selected" : "")); html.replace("%OVR_VENINT_OFF%", (overrideVentiladorInterno == MODE_OFF ? "selected" : ""));
    String logsHtml = ""; for (int i = 0; i < 4; i++) { int logIdx = (indiceLog - 1 - i + 4) % 4; if (!ultimosLogs[logIdx].isEmpty()) { logsHtml += "<div class='log-entry'>" + ultimosLogs[logIdx] + "</div>"; } } html.replace("%ULTIMOS_LOGS%", logsHtml);
    html.replace("%WIFI_STATUS%", (WiFi.status() == WL_CONNECTED) ? "Conectado" : (enModoAP ? "Modo AP" : "Desconectado")); html.replace("%WIFI_IP%", (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : (enModoAP ? WiFi.softAPIP().toString() : "N/A")); html.replace("%AIO_STATUS%", io.statusText());
    request->send(200, "text/html", html);
  });

  // --- NEW: Data Endpoint for AJAX updates ---
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<512> doc;
    doc["temp"] = float(int(temperatura * 10 + 0.5)) / 10.0; doc["temp_min"] = float(int(temperaturaMin * 10 + 0.5)) / 10.0; doc["temp_max"] = float(int(temperaturaMax * 10 + 0.5)) / 10.0;
    doc["hum"] = float(int(humedad * 10 + 0.5)) / 10.0; doc["hum_min"] = float(int(humedadMin * 10 + 0.5)) / 10.0; doc["hum_max"] = float(int(humedadMax * 10 + 0.5)) / 10.0;
    doc["r_c"] = estadoCalefaccion; doc["r_h"] = estadoHumidificador; doc["r_v"] = estadoVentilacion; doc["r_i"] = estadoVentiladorInterno;
    doc["o_c"] = overrideCalefaccion; doc["o_h"] = overrideHumidificador; doc["o_v"] = overrideVentilacion; doc["o_i"] = overrideVentiladorInterno;
    doc["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "Conectado" : (enModoAP ? "Modo AP" : "Desconectado");
    doc["wifi_ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : (enModoAP ? WiFi.softAPIP().toString() : "N/A");
    doc["aio_status"] = io.statusText();
    String output; serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  // --- Route to save climate and timer configuration ---
  server.on("/guardar", HTTP_POST, [](AsyncWebServerRequest *request) {
    preferences.begin("config", false);
    if (request->hasParam("tempDeseada", true)) { temperaturaDeseada = request->getParam("tempDeseada", true)->value().toFloat(); preferences.putFloat("tempDeseada", temperaturaDeseada); }
    if (request->hasParam("margenTemp", true)) { margenTemperatura = request->getParam("margenTemp", true)->value().toFloat(); preferences.putFloat("margenTemp", margenTemperatura); }
    if (request->hasParam("humDeseada", true)) { humedadDeseada = request->getParam("humDeseada", true)->value().toFloat(); preferences.putFloat("humDeseada", humedadDeseada); }
    if (request->hasParam("margenHum", true)) { margenHumedad = request->getParam("margenHum", true)->value().toFloat(); preferences.putFloat("margenHum", margenHumedad); }
    if (request->hasParam("intervaloVent", true)) { intervaloVentilacion = request->getParam("intervaloVent", true)->value().toFloat() * 3600.0; preferences.putFloat("intVent", intervaloVentilacion); }
    if (request->hasParam("duracionVent", true)) { duracionVentilacion = request->getParam("duracionVent", true)->value().toInt() * 60; preferences.putUInt("durVent", duracionVentilacion); }
    if (request->hasParam("intervaloVentInt", true)) { intervaloVentiladorInterno = request->getParam("intervaloVentInt", true)->value().toFloat() * 3600.0; preferences.putFloat("intVentI", intervaloVentiladorInterno); }
    if (request->hasParam("duracionVentInt", true)) { duracionVentiladorInterno = request->getParam("duracionVentInt", true)->value().toInt() * 60; preferences.putUInt("durVentI", duracionVentiladorInterno); }
    preferences.end(); Serial.println("Configuraciones de clima/timer guardadas.");
    request->redirect("/"); // Redirect immediately
  });

  // --- Route to set relay overrides ---
  server.on("/override", HTTP_POST, [](AsyncWebServerRequest *request) {
    preferences.begin("config", false);
    if (request->hasParam("ovr_calefaccion", true)) { int newVal = request->getParam("ovr_calefaccion", true)->value().toInt(); if (newVal != overrideCalefaccion) { overrideCalefaccion = newVal; preferences.putInt("ovrCal", overrideCalefaccion); } }
    if (request->hasParam("ovr_humidificador", true)) { int newVal = request->getParam("ovr_humidificador", true)->value().toInt(); if (newVal != overrideHumidificador) { overrideHumidificador = newVal; preferences.putInt("ovrHum", overrideHumidificador); } }
    if (request->hasParam("ovr_ventilacion", true)) { int newVal = request->getParam("ovr_ventilacion", true)->value().toInt(); if (newVal != overrideVentilacion) { overrideVentilacion = newVal; preferences.putInt("ovrVen", overrideVentilacion); } }
    if (request->hasParam("ovr_ventilador", true)) { int newVal = request->getParam("ovr_ventilador", true)->value().toInt(); if (newVal != overrideVentiladorInterno) { overrideVentiladorInterno = newVal; preferences.putInt("ovrVenI", overrideVentiladorInterno); } }
    preferences.end(); Serial.println("Overrides guardados."); controlarSistema();
    request->redirect("/"); // Redirect immediately
  });

   // --- Route to reset Min/Max values ---
   server.on("/resetminmax", HTTP_GET, [](AsyncWebServerRequest *request){
    temperaturaMin = 100.0; temperaturaMax = -100.0; humedadMin = 100.0; humedadMax = 0.0; Serial.println("Valores Min/Max reseteados.");
    request->redirect("/"); // Redirect immediately
   });

  // --- Route to show WiFi configuration page ---
  server.on("/configwifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    String currentSSID = preferences.getString("wifiSSID", DEFAULT_WIFI_SSID); String html = R"rawliteral(
<!DOCTYPE html><html lang="es"><head><meta charset="UTF-8"><title>Config WiFi</title><style>body{background-color:#282c34; color:#abb2bf; font-family: sans-serif; padding: 20px;} h1{color:#61afef;} form{background-color:#3b4048; padding:20px; border-radius:8px;} label{display:block; margin-bottom:5px; color:#e5c07b;} input[type='text'],input[type='password']{width:95%; padding:8px; margin-bottom:10px; background-color:#454c57; color:#abb2bf; border:1px solid #61afef; border-radius:4px;} input[type='submit']{background-color:#61afef; color:#282c34; padding:10px 15px; border:none; border-radius:5px; cursor:pointer;}</style></head><body><h1>Configurar WiFi</h1><p>SSID Actual: %CURRENT_SSID%</p><form method='POST' action='/savewifi'><label>Nuevo SSID:</label><input type='text' name='ssid' placeholder='Dejar vacío para no cambiar'><br><label>Nueva Contraseña:</label><input type='password' name='pass'><br><input type='submit' value='Guardar y Reconectar'></form><br><a href='/'>Volver al Panel</a></body></html>)rawliteral"; html.replace("%CURRENT_SSID%", currentSSID); request->send(200, "text/html", html);
  });

  // --- Route to save WiFi credentials and reconnect ---
  server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    String newSSID = ""; String newPass = ""; bool ssidChanged = false; preferences.begin("config", false);
    if (request->hasParam("ssid", true)) { String reqSSID = request->getParam("ssid", true)->value(); if (!reqSSID.isEmpty()) { newSSID = reqSSID; preferences.putString("wifiSSID", newSSID); ssidChanged = true; Serial.println("Nuevo SSID guardado: " + newSSID); } else { newSSID = preferences.getString("wifiSSID", DEFAULT_WIFI_SSID); } }
    if (request->hasParam("pass", true)) { newPass = request->getParam("pass", true)->value(); preferences.putString("wifiPASS", newPass); Serial.println("Nueva contraseña guardada."); }
    preferences.end();
    if (ssidChanged || request->hasParam("pass", true)) { Serial.println("WiFi Config cambiada. Desconectando e intentando reconectar a: " + newSSID); WiFi.disconnect(true); delay(100); WiFi.mode(WIFI_STA); WiFi.begin(newSSID.c_str(), newPass.c_str()); gestionarWiFi(); }
    request->redirect("/"); // Redirect immediately
  });

  // Start server
  server.begin();
}
// ============================================================
// END: Updated iniciarServidorWeb function
// ============================================================


// --- Remaining functions ---

//
// Cargar configuraciones almacenadas
//
void cargarConfiguraciones() {
  temperaturaDeseada = preferences.getFloat("tempDeseada", 24.0); margenTemperatura = preferences.getFloat("margenTemp", 2.0);
  humedadDeseada = preferences.getFloat("humDeseada", 90.0); margenHumedad = preferences.getFloat("margenHum", 5.0);
  intervaloVentilacion = preferences.getFloat("intVent", 3600.0); duracionVentilacion = preferences.getUInt("durVent", 300);
  intervaloVentiladorInterno = preferences.getFloat("intVentI", 360.0); duracionVentiladorInterno = preferences.getUInt("durVentI", 180);
  overrideCalefaccion = preferences.getInt("ovrCal", MODE_AUTO); overrideHumidificador = preferences.getInt("ovrHum", MODE_AUTO); overrideVentilacion = preferences.getInt("ovrVen", MODE_AUTO); overrideVentiladorInterno = preferences.getInt("ovrVenI", MODE_AUTO);
  Serial.println("Configuraciones cargadas:"); Serial.printf("  Temp: %.1f +/- %.1f C\n", temperaturaDeseada, margenTemperatura); Serial.printf("  Hum:  %.0f +/- %.0f %%\n", humedadDeseada, margenHumedad); Serial.printf("  Vent Ext: Intervalo %.1f h, Duracion %lu min\n", intervaloVentilacion / 3600.0, duracionVentilacion / 60); Serial.printf("  Vent Int: Intervalo %.1f h, Duracion %lu min\n", intervaloVentiladorInterno / 3600.0, duracionVentiladorInterno / 60); Serial.printf("  Overrides (C,H,V,I): %d, %d, %d, %d (0=A, 1=ON, 2=OFF)\n", overrideCalefaccion, overrideHumidificador, overrideVentilacion, overrideVentiladorInterno);
}

//
// Sincronización NTP
//
time_t getNtpTime() {
  return timeClient.getEpochTime();
}

//
// Calcular EMAs para cada relé
//
void calcularEMA() {
  EMA_Calefaccion_Hora = alphaHora * (estadoCalefaccion ? 1.0 : 0.0) + (1.0 - alphaHora) * EMA_Calefaccion_Hora; EMA_Calefaccion_Dia = alphaDia * (estadoCalefaccion ? 1.0 : 0.0) + (1.0 - alphaDia) * EMA_Calefaccion_Dia;
  EMA_Humidificador_Hora = alphaHora * (estadoHumidificador ? 1.0 : 0.0) + (1.0 - alphaHora) * EMA_Humidificador_Hora; EMA_Humidificador_Dia = alphaDia * (estadoHumidificador ? 1.0 : 0.0) + (1.0 - alphaDia) * EMA_Humidificador_Dia;
  EMA_Ventilacion_Hora = alphaHora * (estadoVentilacion ? 1.0 : 0.0) + (1.0 - alphaHora) * EMA_Ventilacion_Hora; EMA_Ventilacion_Dia = alphaDia * (estadoVentilacion ? 1.0 : 0.0) + (1.0 - alphaDia) * EMA_Ventilacion_Dia;
  EMA_VentiladorInterno_Hora = alphaHora * (estadoVentiladorInterno ? 1.0 : 0.0) + (1.0 - alphaHora) * EMA_VentiladorInterno_Hora; EMA_VentiladorInterno_Dia = alphaDia * (estadoVentiladorInterno ? 1.0 : 0.0) + (1.0 - alphaDia) * EMA_VentiladorInterno_Dia;
}

//
// Guardar log en buffer circular para la web UI
//
void guardarLog(String log) {
  ultimosLogs[indiceLog] = log;
  indiceLog = (indiceLog + 1) % 4;
}

//
// Tarea para gestionar la conexión WiFi (modo AP fallback)
//
void gestionarWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!enModoAP) {
      Serial.println("WiFi Desconectado. Activando modo AP para configuración..."); Serial.println("SSID: Vivero_Fallback | Pass: fallback123");
      WiFi.mode(WIFI_AP_STA); WiFi.softAP("Vivero_Fallback", "fallback123"); enModoAP = true; printLCD(0,"Modo AP Activo"); printLCD(1, "%s", WiFi.softAPIP().toString().c_str());
    } else { Serial.println("Modo AP activo. IP: " + WiFi.softAPIP().toString()); }
  } else {
    if (enModoAP) {
      Serial.println("Conectado a WiFi (" + WiFi.localIP().toString() + "). Desactivando modo AP..."); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); enModoAP = false; printLCD(0,"WiFi OK!"); printLCD(1, "%s", WiFi.localIP().toString().c_str());
    }
     if (io.status() < AIO_CONNECTED) { Serial.println("AIO desconectado. Intentando reconectar..."); io.connect(); }
  }
}
// --- END OF FILE ---

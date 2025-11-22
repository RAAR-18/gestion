#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========================================
// PINES ESP32-C3
// ========================================
// LED INTEGRADO (pin 8 - azul)
#define LED_BUILTIN_PIN 8  // LED azul integrado de la placa ✅

// LED RGB EXTERNO (nuevo módulo)
#define LED_RED_PIN 4      // LED rojo del módulo RGB
#define LED_GREEN_PIN 5    // LED verde del módulo RGB
#define LED_BLUE_PIN 6     // LED azul del módulo RGB

// Motor vibrador
#define MOTOR_PIN 7        // Pin del motor vibrador

// ========================================
// CONFIGURACIÓN
// ========================================
const char* ssid = "Rafa";      
const char* password = "12345678";
const char* SERVER_IP = "10.73.183.70";
String SERVER_URL = "http://10.73.183.70:5000";
const String MESERO_ID = "mesero1";
const String MESERO_NOMBRE = "Mesero 1";

// ========================================
// VARIABLES
// ========================================
unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 2000;

bool notificacionActiva = false;
String kioskoAsignado = "";
String timestampAsignacion = "";

unsigned long lastWifiTry = 0;
const unsigned long wifiRetryInterval = 10000;

unsigned long lastStatusCheck = 0;
const unsigned long statusCheckInterval = 30000;

bool wifiConnected = false;
bool serverConnected = false;

int consecutiveErrors = 0;

// Variables para vibración
unsigned long vibracionInicio = 0;
int vibracionCount = 0;
bool vibracionActiva = false;

// ========================================
// FUNCIONES LED INTEGRADO (pin 8)
// ========================================
void ledBuiltinOn() {
  digitalWrite(LED_BUILTIN_PIN, LOW);  // Lógica invertida
}

void ledBuiltinOff() {
  digitalWrite(LED_BUILTIN_PIN, HIGH);  // Lógica invertida
}

// ========================================
// FUNCIONES LED RGB EXTERNO
// ========================================
void ledRgbOff() {
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, LOW);
}

void ledRgbRed() {
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, LOW);
}

void ledRgbGreen() {
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, LOW);
}

void ledRgbBlue() {
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, HIGH);
}

void ledRgbYellow() {
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, LOW);
}

void ledRgbPurple() {
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, HIGH);
}

void ledRgbCyan() {
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, HIGH);
}

void ledRgbWhite() {
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, HIGH);
}

// ========================================
// FUNCIONES MOTOR
// ========================================
void motorOn() {
  digitalWrite(MOTOR_PIN, HIGH);
}

void motorOff() {
  digitalWrite(MOTOR_PIN, LOW);
}

void iniciarVibracion(int pulsos) {
  vibracionCount = pulsos;
  vibracionActiva = true;
  vibracionInicio = millis();
}

void handleVibracion() {
  if (!vibracionActiva) return;
  
  unsigned long tiempoActual = millis();
  unsigned long tiempoTranscurrido = tiempoActual - vibracionInicio;
  
  int ciclo = tiempoTranscurrido / 500;
  
  if (ciclo >= vibracionCount) {
    motorOff();
    vibracionActiva = false;
    return;
  }
  
  int posicionEnCiclo = tiempoTranscurrido % 500;
  
  if (posicionEnCiclo < 300) {
    motorOn();
  } else {
    motorOff();
  }
}

// ========================================
// HELPER
// ========================================
void logln(const String &s){ 
  Serial.println("[" + String(millis()/1000) + "s] " + s); 
}

// ========================================
// WIFI
// ========================================
bool checkWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      logln("✅ WiFi CONECTADO");
      logln("   IP: " + WiFi.localIP().toString());
      logln("   RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    return true;
  }
  
  if (wifiConnected) {
    wifiConnected = false;
    logln("❌ WiFi DESCONECTADO");
  }
  
  if (millis() - lastWifiTry < wifiRetryInterval) return false;
  
  lastWifiTry = millis();
  logln("⚠️ Reconectando WiFi...");
  
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    wifiConnected = true;
    logln("✅ WiFi reconectado");
    return true;
  } else {
    Serial.println();
    logln("❌ Fallo reconexión WiFi");
    return false;
  }
}

// ========================================
// HTTP GET
// ========================================
int httpGET(const String &url, String &response) {
  if (!checkWiFi()) {
    return -99;
  }

  HTTPClient http;
  WiFiClient client;
  
  http.begin(client, url);
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  http.setReuse(false);
  
  int code = http.GET();
  
  if (code > 0) {
    response = http.getString();
    consecutiveErrors = 0;
  } else {
    consecutiveErrors++;
    if (consecutiveErrors >= 5) {
      logln("⚠️ Muchos errores HTTP");
      consecutiveErrors = 0;
    }
  }
  
  http.end();
  return code;
}

// ========================================
// VERIFICAR NOTIFICACIÓN
// ========================================
void checkNotificacion() {
  if (!checkWiFi()) return;
  
  String response;
  String url = SERVER_URL + "/mesero/" + MESERO_ID + "/notificacion";
  
  int code = httpGET(url, response);
  
  if (code == 200) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      bool tieneNotificacion = doc["notificacion"] | false;
      String kiosko = doc["kiosko"] | "";
      String timestamp = doc["timestamp"] | "";
      
      // ========== NUEVA NOTIFICACIÓN ==========
      if (tieneNotificacion && !notificacionActiva && kiosko.length() > 0) {
        notificacionActiva = true;
        kioskoAsignado = kiosko;
        timestampAsignacion = timestamp;
        
        // ENCENDER LED INTEGRADO (pin 8)
        ledBuiltinOn();
        
        // ENCENDER LED RGB EXTERNO EN AZUL
        ledRgbBlue();
        
        // INICIAR VIBRACIÓN (5 pulsos)
        iniciarVibracion(5);
        
        logln("════════════════════════════════════════");
        logln("  🔔 ¡NUEVA ASIGNACIÓN - " + MESERO_NOMBRE + "!");
        logln("════════════════════════════════════════");
        logln("  🏪 Kiosko: " + kioskoAsignado);
        logln("  🕐 Hora: " + timestampAsignacion);
        logln("════════════════════════════════════════");
        logln("  📍 Dirígete al kiosko y usa tu RFID");
        logln("  💡 LED integrado (pin 8): ENCENDIDO");
        logln("  💙 LED RGB externo: AZUL");
        logln("  📳 Vibración: 5 pulsos");
        logln("════════════════════════════════════════");
        
        if (!serverConnected) {
          serverConnected = true;
        }
      }
      
      // ========== NOTIFICACIÓN CONFIRMADA ==========
      if (!tieneNotificacion && notificacionActiva) {
        notificacionActiva = false;
        
        // APAGAR LED INTEGRADO
        ledBuiltinOff();
        
        // CAMBIAR LED RGB A VERDE (confirmación)
        ledRgbGreen();
        
        // VIBRACIÓN CORTA DE CONFIRMACIÓN (2 pulsos)
        iniciarVibracion(2);
        
        logln("════════════════════════════════════════");
        logln("  ✅ ATENCIÓN CONFIRMADA - " + MESERO_NOMBRE);
        logln("════════════════════════════════════════");
        logln("  Kiosko " + kioskoAsignado + " atendido");
        logln("  💡 LED integrado: APAGADO");
        logln("  💚 LED RGB: VERDE (3 segundos)");
        logln("  📳 Vibración: 2 pulsos");
        logln("════════════════════════════════════════");
        
        // Apagar todo después de 3 segundos
        delay(3000);
        ledRgbOff();
        
        kioskoAsignado = "";
        timestampAsignacion = "";
      }
      
      if (!serverConnected) {
        serverConnected = true;
        logln("✅ Servidor conectado");
      }
      
    } else {
      logln("⚠️ Error parseando JSON");
    }
  } else if (code > 0) {
    if (serverConnected) {
      serverConnected = false;
      logln("❌ Error servidor (HTTP " + String(code) + ")");
    }
  }
}

// ========================================
// MANEJAR LEDS
// ========================================
void handleNotification() {
  if (notificacionActiva) {
    ledBuiltinOn();   // LED integrado encendido
    ledRgbBlue();     // LED RGB en azul
  } else if (!vibracionActiva) {
    ledBuiltinOff();  // LED integrado apagado
    ledRgbOff();      // LED RGB apagado
  }
}

// ========================================
// ESTADO DEL SISTEMA
// ========================================
void printStatus() {
  if (millis() - lastStatusCheck < statusCheckInterval) return;
  
  lastStatusCheck = millis();
  
  logln("─────────────────────────────────────");
  logln("📊 ESTADO DEL SISTEMA");
  logln("─────────────────────────────────────");
  logln("  WiFi: " + String(wifiConnected ? "✅ Conectado" : "❌ Desconectado"));
  if (wifiConnected) {
    logln("  IP: " + WiFi.localIP().toString());
    logln("  Señal: " + String(WiFi.RSSI()) + " dBm");
  }
  logln("  Servidor: " + String(serverConnected ? "✅ Conectado" : "❌ Desconectado"));
  logln("  Notificación: " + String(notificacionActiva ? "🔔 Activa" : "🔕 Inactiva"));
  
  String estadoLEDs = "⚫ Apagados";
  if (notificacionActiva) estadoLEDs = "💙 Azul (asignación)";
  else if (vibracionActiva) estadoLEDs = "💚 Verde (confirmación)";
  logln("  LEDs: " + estadoLEDs);
  
  if (notificacionActiva) {
    logln("  Kiosko: " + kioskoAsignado);
  }
  logln("  RAM libre: " + String(ESP.getFreeHeap() / 1024) + " KB");
  logln("─────────────────────────────────────");
}

// ========================================
// SETUP
// ========================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n");
  logln("════════════════════════════════════════");
  logln("   📱 MANILLA INTELIGENTE COMPLETA");
  logln("   🔵 " + MESERO_NOMBRE);
  logln("════════════════════════════════════════");
  
  // Configurar LED integrado (pin 8)
  pinMode(LED_BUILTIN_PIN, OUTPUT);
  ledBuiltinOff();
  logln("✅ LED integrado configurado (PIN " + String(LED_BUILTIN_PIN) + ")");
  
  // Configurar LED RGB externo
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  ledRgbOff();
  logln("✅ LED RGB externo configurado");
  logln("   Rojo: PIN " + String(LED_RED_PIN));
  logln("   Verde: PIN " + String(LED_GREEN_PIN));
  logln("   Azul: PIN " + String(LED_BLUE_PIN));
  
  // Configurar motor
  pinMode(MOTOR_PIN, OUTPUT);
  motorOff();
  logln("✅ Motor vibrador configurado (PIN " + String(MOTOR_PIN) + ")");
  
  logln("");
  logln("🧪 Test de componentes...");
  
  // Test LED integrado
  logln("   LED integrado...");
  ledBuiltinOn();
  delay(500);
  ledBuiltinOff();
  delay(300);
  
  // Test LED RGB - Secuencia de colores
  logln("   LED RGB - Rojo...");
  ledRgbRed();
  delay(500);
  
  logln("   LED RGB - Verde...");
  ledRgbGreen();
  delay(500);
  
  logln("   LED RGB - Azul...");
  ledRgbBlue();
  delay(500);
  
  ledRgbOff();
  logln("   LEDs OK ✅");
  
  // Test motor - 1 vibración = Mesero 1
  logln("   Motor (1 vibración = Mesero 1)...");
  motorOn();
  delay(300);
  motorOff();
  delay(300);
  logln("   Motor OK ✅");
  
  logln("✅ Todo el hardware funcionando");
  
  // WiFi
  logln("");
  logln("📡 Conectando WiFi: " + String(ssid));
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    logln("✅ WiFi conectado!");
    logln("   IP: " + WiFi.localIP().toString());
    
    delay(1000);
    String testResp;
    int testCode = httpGET(SERVER_URL + "/mesero/" + MESERO_ID + "/notificacion", testResp);
    
    if (testCode == 200) {
      serverConnected = true;
      logln("✅ Servidor OK!");
    }
  }
  
  logln("");
  logln("════════════════════════════════════════");
  logln("   ✅ SISTEMA LISTO");
  logln("════════════════════════════════════════");
  logln("   💙 Azul: Nueva asignación");
  logln("   💚 Verde: Confirmación");
  logln("   📳 Vibración: Alertas");
  logln("   🎯 Esperando asignaciones...");
  logln("════════════════════════════════════════");
  logln("");
}

// ========================================
// LOOP
// ========================================
void loop() {
  
  checkWiFi();
  
  if (millis() - lastCheckTime > checkInterval) {
    lastCheckTime = millis();
    checkNotificacion();
  }
  
  handleNotification();
  handleVibracion();
  printStatus();
  
  delay(50);
}
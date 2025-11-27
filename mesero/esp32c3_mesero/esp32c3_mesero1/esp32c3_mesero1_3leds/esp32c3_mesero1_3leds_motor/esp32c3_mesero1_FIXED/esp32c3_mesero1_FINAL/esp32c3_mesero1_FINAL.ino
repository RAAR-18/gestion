#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========================================
// PINES ESP32-C3 - CONFIGURACIÓN FINAL ✅
// ========================================
#define LED1_PIN 3   // LED para Kiosko 1 (Rojo)
#define LED2_PIN 4   // LED para Kiosko 2 (Azul)
#define LED3_PIN 5   // LED para Kiosko 3 (Verde)
#define MOTOR_PIN 7  // Motor vibrador

// ========================================
// CONFIGURACIÓN - ✅ IP CORRECTA
// ========================================
const char* ssid = "Rafa";      
const char* password = "12345678";
const char* SERVER_IP = "10.227.19.70";  // ✅ IP CORRECTA DEL SERVIDOR
String SERVER_URL = "http://10.227.19.70:5000";  // ✅ IP CORRECTA DEL SERVIDOR
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
int numeroKioskoActual = 0;

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
// HELPER
// ========================================
void logln(const String &s){ 
  Serial.println("[" + String(millis()/1000) + "s] " + s); 
}

// ========================================
// OBTENER NÚMERO DE KIOSKO
// ========================================
int getNumeroKiosko(const String &kiosko) {
  if (kiosko == "kiosko-1") return 1;
  if (kiosko == "kiosko-2") return 2;
  if (kiosko == "kiosko-3") return 3;
  return 0;
}

// ========================================
// FUNCIONES MOTOR VIBRADOR
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
  logln("  📳 Iniciando " + String(pulsos) + " pulsos de vibración");
}

void handleVibracion() {
  if (!vibracionActiva) return;
  
  unsigned long tiempoActual = millis();
  unsigned long tiempoTranscurrido = tiempoActual - vibracionInicio;
  
  int ciclo = tiempoTranscurrido / 500;
  
  if (ciclo >= vibracionCount) {
    motorOff();
    vibracionActiva = false;
    logln("  📳 Vibración completada");
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
// APAGAR TODOS LOS LEDs
// ========================================
void apagarTodosLEDs() {
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
}

// ========================================
// ENCENDER LEDs SEGÚN KIOSKO
// ========================================
void encenderLEDsKiosko(int numKiosko) {
  apagarTodosLEDs();
  delay(300);
  
  logln("════════════════════════════════════════");
  logln("  🔔 INDICANDO KIOSKO " + String(numKiosko));
  logln("════════════════════════════════════════");
  
  if (numKiosko == 1) {
    digitalWrite(LED1_PIN, HIGH);
    logln("  🔴 LED 1 ENCENDIDO (PIN 3)");
  } else if (numKiosko == 2) {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    logln("  🔴🔵 LED 1 + LED 2 ENCENDIDOS (PIN 3+4)");
  } else if (numKiosko == 3) {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    digitalWrite(LED3_PIN, HIGH);
    logln("  🔴🔵🟢 LED 1 + LED 2 + LED 3 ENCENDIDOS (PIN 3+4+5)");
  }
  
  logln("════════════════════════════════════════");
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
      logln("   Gateway: " + WiFi.gatewayIP().toString());
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
  logln("📡 Reconectando WiFi...");
  
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
    logln("   IP: " + WiFi.localIP().toString());
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
      logln("⚠️ Muchos errores HTTP consecutivos");
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
        numeroKioskoActual = getNumeroKiosko(kiosko);
        
        logln("");
        logln("════════════════════════════════════════");
        logln("  ⚡ ¡NUEVA ASIGNACIÓN - " + MESERO_NOMBRE + "!");
        logln("════════════════════════════════════════");
        logln("  📍 Kiosko: " + kioskoAsignado);
        logln("  🕐 Hora: " + timestampAsignacion);
        logln("  #️⃣  Número: " + String(numeroKioskoActual));
        logln("════════════════════════════════════════");
        
        encenderLEDsKiosko(numeroKioskoActual);
        iniciarVibracion(5);
        
        if (!serverConnected) {
          serverConnected = true;
        }
      }
      
      // ========== NOTIFICACIÓN CONFIRMADA ==========
      if (!tieneNotificacion && notificacionActiva) {
        notificacionActiva = false;
        apagarTodosLEDs();
        iniciarVibracion(2);
        
        logln("");
        logln("════════════════════════════════════════");
        logln("  ✅ ATENCIÓN CONFIRMADA - " + MESERO_NOMBRE);
        logln("════════════════════════════════════════");
        logln("  💡 LEDs APAGADOS");
        logln("════════════════════════════════════════");
        logln("");
        
        kioskoAsignado = "";
        timestampAsignacion = "";
        numeroKioskoActual = 0;
      }
      
      if (!serverConnected) {
        serverConnected = true;
        logln("✅ Servidor conectado");
      }
      
    } else {
      logln("⚠️ Error parseando JSON: " + String(error.c_str()));
    }
  } else if (code > 0) {
    if (serverConnected) {
      serverConnected = false;
      logln("❌ Error servidor (HTTP " + String(code) + ")");
    }
  }
}

// ========================================
// ESTADO DEL SISTEMA
// ========================================
void printStatus() {
  if (millis() - lastStatusCheck < statusCheckInterval) return;
  
  lastStatusCheck = millis();
  
  logln("");
  logln("─────────────────────────────────────");
  logln("  📊 ESTADO DEL SISTEMA");
  logln("─────────────────────────────────────");
  logln("  WiFi: " + String(wifiConnected ? "✅ Conectado" : "❌ Desconectado"));
  if (wifiConnected) {
    logln("  IP: " + WiFi.localIP().toString());
    logln("  Señal: " + String(WiFi.RSSI()) + " dBm");
  }
  logln("  Servidor: " + String(serverConnected ? "✅ Conectado" : "❌ Desconectado"));
  logln("  Notificación: " + String(notificacionActiva ? "🔔 Activa" : "🔕 Inactiva"));
  
  if (notificacionActiva) {
    logln("  Kiosko asignado: " + kioskoAsignado + " (#" + String(numeroKioskoActual) + ")");
    
    String estadoLEDs = "  LEDs: ";
    if (numeroKioskoActual == 1) estadoLEDs += "🔴⚫⚫";
    else if (numeroKioskoActual == 2) estadoLEDs += "🔴🔵⚫";
    else if (numeroKioskoActual == 3) estadoLEDs += "🔴🔵🟢";
    logln(estadoLEDs);
  } else {
    logln("  LEDs: ⚫⚫⚫");
  }
  
  logln("  Motor: " + String(vibracionActiva ? "📳" : "⚫"));
  logln("  RAM libre: " + String(ESP.getFreeHeap() / 1024) + " KB");
  logln("─────────────────────────────────────");
  logln("");
}

// ========================================
// SETUP
// ========================================
void setup() {
  Serial.begin(115200);
  
  #ifdef ARDUINO_USB_CDC_ON_BOOT
  delay(1000);
  while(!Serial && millis() < 5000) {
    delay(10);
  }
  #endif
  
  delay(1000);
  
  Serial.println("\n\n");
  logln("════════════════════════════════════════");
  logln("    🎫 MANILLA INTELIGENTE v4.0");
  logln("    " + MESERO_NOMBRE);
  logln("════════════════════════════════════════");
  logln("   ESP32-C3 - PINES 3, 4, 5 ✅");
  logln("   ID: " + MESERO_ID);
  logln("   Servidor: " + SERVER_URL);
  logln("════════════════════════════════════════");
  
  // Configurar pines
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  
  apagarTodosLEDs();
  motorOff();
  
  logln("💡 LEDs configurados (PIN 3, 4, 5)");
  logln("📳 Motor configurado (PIN 7)");
  
  // Conectar WiFi
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
    logln("   IP ESP32: " + WiFi.localIP().toString());
    logln("   Gateway: " + WiFi.gatewayIP().toString());
    logln("   RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    // Test servidor
    delay(1000);
    logln("");
    logln("🔍 Probando servidor...");
    
    String testResp;
    int testCode = httpGET(SERVER_URL + "/mesero/" + MESERO_ID + "/notificacion", testResp);
    
    if (testCode == 200) {
      serverConnected = true;
      logln("✅ Servidor OK!");
      logln("   Respuesta: " + testResp);
    } else {
      logln("❌ Servidor no responde (código: " + String(testCode) + ")");
      logln("   Se reintentará automáticamente");
    }
  } else {
    logln("❌ WiFi NO conectado");
  }
  
  logln("");
  logln("════════════════════════════════════════");
  logln("    ✅ SISTEMA LISTO");
  logln("════════════════════════════════════════");
  logln("    ⏱️  Verificando cada 2 segundos");
  logln("    💡 LEDs: PIN 3, 4, 5");
  logln("    📳 Motor: PIN 7");
  logln("    🎯 Esperando asignaciones...");
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
  
  handleVibracion();
  printStatus();
  
  delay(50);
}
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========================================
// PINES ESP32-C3
// ========================================
#define LED_PIN 8 

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

unsigned long lastBlinkTime = 0;
bool ledState = false;

unsigned long lastWifiTry = 0;
const unsigned long wifiRetryInterval = 10000;

unsigned long lastStatusCheck = 0;
const unsigned long statusCheckInterval = 30000;

bool wifiConnected = false;
bool serverConnected = false;

int consecutiveErrors = 0;

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
      logln("   WiFi CONECTADO");
      logln("   IP: " + WiFi.localIP().toString());
      logln("   RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    return true;
  }
  
  if (wifiConnected) {
    wifiConnected = false;
    logln("  WiFi DESCONECTADO");
  }
  
  if (millis() - lastWifiTry < wifiRetryInterval) return false;
  
  lastWifiTry = millis();
  logln("  Reconectando WiFi...");
  
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
    logln("   WiFi reconectado");
    logln("   IP: " + WiFi.localIP().toString());
    return true;
  } else {
    Serial.println();
    logln("   Fallo reconexión WiFi");
    logln("   Reintentando en 10 segundos...");
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
      logln("  Muchos errores HTTP");
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
        
        // ENCENDER LED INMEDIATAMENTE
        digitalWrite(LED_PIN, HIGH);
        
        logln("════════════════════════════════════════");
        logln("    ¡NUEVA ASIGNACIÓN - " + MESERO_NOMBRE + "!");
        logln("════════════════════════════════════════");
        logln("     Kiosko: " + kioskoAsignado);
        logln("     Hora: " + timestampAsignacion);
        logln("════════════════════════════════════════");
        logln("     Dirígete al kiosko y usa tu RFID");
        logln("     LED ENCENDIDO");
        logln("════════════════════════════════════════");
        
        if (!serverConnected) {
          serverConnected = true;
        }
      }
      
      // ========== NOTIFICACIÓN CONFIRMADA ==========
      if (!tieneNotificacion && notificacionActiva) {
        notificacionActiva = false;
        
        // APAGAR LED INMEDIATAMENTE
        digitalWrite(LED_PIN, HIGH);
        
        logln("════════════════════════════════════════");
        logln("    ATENCIÓN CONFIRMADA - " + MESERO_NOMBRE);
        logln("════════════════════════════════════════");
        logln("  Kiosko " + kioskoAsignado + " atendido");
        logln("  Notificación limpiada");
        logln("    LED APAGADO");
        logln("════════════════════════════════════════");
        
        kioskoAsignado = "";
        timestampAsignacion = "";
      }
      
      // Marcar servidor como conectado
      if (!serverConnected) {
        serverConnected = true;
        logln("  Servidor conectado");
      }
      
    } else {
      logln("  Error parseando JSON: " + String(error.c_str()));
    }
  } else if (code > 0) {
    if (serverConnected) {
      serverConnected = false;
      logln("   Error servidor (HTTP " + String(code) + ")");
    }
  }
}

// ========================================
// MANEJAR LED
// ========================================
void handleNotification() {
  if (!notificacionActiva) {
    digitalWrite(LED_PIN, HIGH);  // LED apagado si no hay notificación
    return;
  }
  
  // LED ENCENDIDO FIJO cuando hay notificación activa
  digitalWrite(LED_PIN, LOW);
}

// ========================================
// ESTADO DEL SISTEMA
// ========================================
void printStatus() {
  if (millis() - lastStatusCheck < statusCheckInterval) return;
  
  lastStatusCheck = millis();
  
  logln("─────────────────────────────────────");
  logln("  ESTADO DEL SISTEMA");
  logln("─────────────────────────────────────");
  logln("  WiFi: " + String(wifiConnected ? "✅ Conectado" : "❌ Desconectado"));
  if (wifiConnected) {
    logln("  IP: " + WiFi.localIP().toString());
    logln("  Señal: " + String(WiFi.RSSI()) + " dBm");
  }
  logln("  Servidor: " + String(serverConnected ? "✅ Conectado" : "❌ Desconectado"));
  logln("  Notificación: " + String(notificacionActiva ? "🔔 Activa" : "🔕 Inactiva"));
  logln("  LED: " + String(notificacionActiva ? "💡 Encendido" : "⚫ Apagado"));
  if (notificacionActiva) {
    logln("  Kiosko asignado: " + kioskoAsignado);
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
  logln("    MANILLA INTELIGENTE");
  logln("    " + MESERO_NOMBRE);
  logln("════════════════════════════════════════");
  logln("   ESP32-C3 - Sistema de Notificaciones");
  logln("   ID: " + MESERO_ID);
  logln("   Servidor: " + SERVER_URL);
  logln("════════════════════════════════════════");
  
  // Info del chip
  Serial.print("Chip: ");
  Serial.print(ESP.getChipModel());
  Serial.print(" Rev ");
  Serial.println(ESP.getChipRevision());
  Serial.print("RAM libre: ");
  Serial.print(ESP.getFreeHeap() / 1024);
  Serial.println(" KB");
  
  logln("════════════════════════════════════════");
  
  // Configurar LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  logln("  LED configurado (PIN 2)");
  
  // Test LED - 1 parpadeo = Mesero 1
  logln("  Test de identificación...");
  for (int i = 0; i < 1; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(300);
    digitalWrite(LED_PIN, HIGH);
    delay(300);
  }
  logln("  Identificado como " + MESERO_NOMBRE);
  
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
    logln("   WiFi conectado!");
    logln("   IP: " + WiFi.localIP().toString());
    logln("   RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    // Test servidor
    delay(1000);
    logln("");
    logln("🔍 Probando servidor...");
    
    String testResp;
    int testCode = httpGET(SERVER_URL + "/mesero/" + MESERO_ID + "/notificacion", testResp);
    
    if (testCode == 200) {
      serverConnected = true;
      logln("   Servidor OK!");
      logln("   Respuesta: " + testResp);
    } else {
      logln("   Servidor no responde (código: " + String(testCode) + ")");
      logln("   Se reintentará automáticamente");
    }
  } else {
    logln("   WiFi NO conectado");
    logln("   Se reintentará cada 10 segundos");
  }
  
  logln("");
  logln("════════════════════════════════════════");
  logln("    SISTEMA LISTO");
  logln("════════════════════════════════════════");
  logln("    Verificando notificaciones cada 2s");
  logln("    Reintentos WiFi cada 10s");
  logln("    Reporte de estado cada 30s");
  logln("    LED azul (PIN 8) indica asignación");
  logln("    Esperando asignaciones...");
  logln("════════════════════════════════════════");
  logln("");
}

// ========================================
// LOOP
// ========================================
void loop() {
  
  // Verificar WiFi
  checkWiFi();
  
  // Verificar notificaciones cada 2 segundos
  if (millis() - lastCheckTime > checkInterval) {
    lastCheckTime = millis();
    checkNotificacion();
  }
  
  // Manejar LED (mantener encendido si hay notificación)
  handleNotification();
  
  // Mostrar estado periódico (cada 30 segundos)
  printStatus();
  
  delay(50);
}
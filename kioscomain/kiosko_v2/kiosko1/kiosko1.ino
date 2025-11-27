#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>

// ---------- PINES ----------
#define SS_PIN 5
#define RST_PIN 27
#define BUTTON_PIN 14
#define LED_PIN 32

MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- CONFIG ----------
const char* ssid = "Rafa";      
const char* password = "12345678";

// CAMBIAR ESTA IP POR LA DE TU SERVIDOR
const char* SERVER_IP = "10.73.183.70";  // ACTUALIZAR
String SERVER_URL = "http://10.73.183.70:5000";  // ACTUALIZAR
const String KIOSKO = "kiosko-1";

// ---------- CONTROL ----------
unsigned long lastButtonChange = 0;
const unsigned long debounceMs = 200;
bool lastButtonState = HIGH;

unsigned long lastWifiTry = 0;
const unsigned long wifiRetryInterval = 10000;

unsigned long lastStateCheck = 0;
const unsigned long stateCheckInterval = 5000;
String lastKnownState = "Libre";
String meseroAsignado = "";  // Mesero actualmente asignado

int consecutiveErrors = 0;

// ---------- HELPERS ----------
void logln(const String &s){ 
  Serial.println("[" + String(millis()/1000) + "s] " + s); 
}

// ---------- UID MAPPING ----------
String uidToMesero(const String &uid) {
  String u = uid;
  u.trim();
  u.toUpperCase();

  if (u == "26731C06" || u == "26731C6") return "mesero1";
  if (u == "19EE2C06" || u == "19EE2C6") return "mesero2";
  if (u == "044633C2411595" || u == "44633C2411595") return "mesero3";
  
  return "";
}

String readUIDString() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    char b[3];
    sprintf(b, "%02X", rfid.uid.uidByte[i]);
    uid += b;
  }
  return uid;
}

// ---------- WIFI ----------
bool checkWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  
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
    logln("  WiFi OK: " + WiFi.localIP().toString());
    return true;
  } else {
    Serial.println();
    logln("  WiFi fallo");
    return false;
  }
}

// ---------- HTTP GET ----------
int httpGET(const String &url, String &response) {
  if (!checkWiFi()) {
    logln("  Sin WiFi");
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
    logln("  HTTP OK (code=" + String(code) + ")");
    consecutiveErrors = 0;
  } else {
    logln("  HTTP ERROR (code=" + String(code) + ")");
    consecutiveErrors++;
    
    if (consecutiveErrors >= 5) {
      logln("  Muchos errores. Verifica servidor.");
      consecutiveErrors = 0;
    }
  }
  
  http.end();
  return code;
}

// ---------- ESTADO ----------
void checkKioskoState() {
  if (!checkWiFi()) return;
  
  String response;
  String url = SERVER_URL + "/estado";
  
  int code = httpGET(url, response);
  
  if (code == 200) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error && doc.containsKey(KIOSKO)) {
      String newState = doc[KIOSKO]["estado"].as<String>();
      String mesero = doc[KIOSKO]["mesero"] | "";
      
      if (newState != lastKnownState) {
        logln("🔄 " + lastKnownState + " → " + newState);
        
        if (newState == "Libre") {
          digitalWrite(LED_PIN, LOW);
          logln(" LED OFF");
          meseroAsignado = "";
        } else if (newState == "Pendiente") {
          digitalWrite(LED_PIN, HIGH);
          logln(" LED ON (Pendiente)");
          meseroAsignado = "";
        } else if (newState == "En atención") {
          digitalWrite(LED_PIN, HIGH);
          logln(" LED ON (En atención)");
          meseroAsignado = mesero;
          logln(" Mesero asignado: " + meseroAsignado);
        }
        
        lastKnownState = newState;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  logln("═══════════════════════════════════════");
  logln("     KIOSKO - ESP32");
  logln("═══════════════════════════════════════");
  logln("    ID: " + KIOSKO);
  logln("═══════════════════════════════════════");
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  SPI.begin();
  rfid.PCD_Init();
  logln(" RFID OK");
  
  logln("");
  logln(" Conectando WiFi: " + String(ssid));
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    logln("   WiFi conectado!");
    logln("   IP: " + WiFi.localIP().toString());
    logln("   Servidor: " + SERVER_URL);
    
    // Test
    delay(1000);
    String testResp;
    int testCode = httpGET(SERVER_URL + "/estado", testResp);
    
    if (testCode == 200) {
      logln("  Servidor OK!");
    } else {
      logln("  Servidor no responde");
    }
  } else {
    logln("  WiFi NO conectado");
  }
  
  logln("");
  logln("═══════════════════════════════════════");
  logln("     SISTEMA LISTO");
  logln("═══════════════════════════════════════");
  logln("");
}

void loop() {
  
  // ---------- BOTÓN ----------
  bool reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastButtonChange = millis();
    lastButtonState = reading;
  }
  
  if ((millis() - lastButtonChange > debounceMs) && reading == LOW) {
    lastButtonChange = millis() + 2000;
    
    logln("══════════════════════════════");
    logln("    BOTÓN PRESIONADO");
    logln("══════════════════════════════");
    
    String response;
    int code = httpGET(SERVER_URL + "/solicitar/" + KIOSKO, response);
    
    if (code == 200) {
      logln(" Solicitud enviada");
      digitalWrite(LED_PIN, HIGH);
      lastKnownState = "Pendiente";
    }
  }

  // ---------- RFID ----------
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    
    String uid = readUIDString();
    logln("══════════════════════════════");
    logln("    RFID: " + uid);
    
    String mesero = uidToMesero(uid);
    
    if (mesero.length() > 0) {
      logln("   Mesero: " + mesero);
      logln("══════════════════════════════");
      
      // Si el kiosko está en atención, confirmar
      if (lastKnownState == "En atención" && meseroAsignado == mesero) {
        logln(" CONFIRMANDO ATENCIÓN CON RFID...");
        
        String response;
        String url = SERVER_URL + "/confirmar/" + KIOSKO + "/" + mesero;
        int code = httpGET(url, response);
        
        if (code == 200) {
          logln("   Atención CONFIRMADA");
          logln("   El mesero " + mesero + " llegó al kiosko");
          
          // Parpadeo de confirmación
          for (int i = 0; i < 3; i++) {
            digitalWrite(LED_PIN, LOW);
            delay(200);
            digitalWrite(LED_PIN, HIGH);
            delay(200);
          }
        } else {
          logln("  Error en confirmación");
        }
      } 
      // Si está libre o pendiente, atender normalmente
      else {
        logln("  ATENDIENDO...");
        
        String response;
        String url = SERVER_URL + "/atender/" + KIOSKO + "/" + mesero;
        int code = httpGET(url, response);
        
        if (code == 200) {
          logln("  Atención registrada");
          meseroAsignado = mesero;
          lastKnownState = "En atención";
        } else {
          logln("  Error registrando atención");
        }
      }
    } else {
      logln("     UID desconocido");
      logln("══════════════════════════════");
    }
    
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    delay(1500);
  }

  // ---------- VERIFICAR ESTADO ----------
  if (millis() - lastStateCheck > stateCheckInterval) {
    lastStateCheck = millis();
    checkKioskoState();
  }

  delay(50);
}

/*
  ------------------------------------------------------------
  M5 Atom Matrix Companion v4
  Single Button Satellite
  Author: Adrian Davis
  URL: https://github.com/themusicnerd/M5-AtomMatrix-Companion-v4-Satellite
  Board: M5Atom (ESP32)
  License: MIT

  Features:
    - Companion v4 Satellite API support
    - Single-button surface
    - 5x5 Matrix status icons (WiFi, OK, error, etc.)
    - External RGB LED PWM output (G33 RED / G22 GREEN / G19 BLUE + G23 GND)
    - WiFiManager config portal (hold 5s)
    - OTA firmware updates
    - Full MAC-based deviceID (M5ATOM_<fullmac>)
    - Auto reconnect, ping, and key-release failsafe
  Thanks To:
    Joespeh Adams, Brad De La Rue, m9-999 and all the wonderful people behind Companion!
  ------------------------------------------------------------
*/

#include <M5Atom.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <vector>
#include <esp32-hal-ledc.h>   // core 3.x LEDC helpers
#include <WebServer.h>
#include <ESPmDNS.h>

Preferences preferences;
WiFiManager wifiManager;
WiFiClient client;

// REST API Server for Companion configuration
WebServer restServer(9999);

// -------------------------------------------------------------------
// Companion Server
// -------------------------------------------------------------------
char companion_host[40] = "Companion IP";
char companion_port[6]  = "16622";

// Static IP (0.0.0.0 = DHCP)
IPAddress stationIP   = IPAddress(0, 0, 0, 0);
IPAddress stationGW   = IPAddress(0, 0, 0, 0);
IPAddress stationMask = IPAddress(255, 255, 255, 0);

// Device ID – full MAC will be appended
String deviceID = "";

// WiFi hostname for mDNS
String wifiHostname = "";

// Boot counter for config portal trigger
const uint8_t BOOT_FAIL_LIMIT = 1;
int bootCountCached = 0;

// AP password for config portal (empty = open)
const char* AP_password = "";

// Timing
unsigned long lastPingTime    = 0;
unsigned long lastConnectTry  = 0;
const unsigned long connectRetryMs = 5000;
const unsigned long pingIntervalMs  = 2000;

// Brightness (0–100)
int brightness = 100;

// -------------------------------------------------------------------
// External RGB LED (Jaycar RGB LED)  - ATOM MATRIX PINS
// -------------------------------------------------------------------
#define LEDC_CHANNEL_RED   0
#define LEDC_CHANNEL_GREEN 1
#define LEDC_CHANNEL_BLUE  2

const int LED_PIN_RED   = 33;  // G33
const int LED_PIN_GREEN = 22;  // G22
const int LED_PIN_BLUE  = 19;  // G19
const int LED_PIN_GND   = 23;  // G23 (ground for LED)

const int pwmFreq       = 5000; // 5 kHz
const int pwmResolution = 8;

uint8_t lastColorR = 0;
uint8_t lastColorG = 0;
uint8_t lastColorB = 0;

// -------------------------------------------------------------------
// Matrix number / icon system (ported from TallyArbiter project)
// -------------------------------------------------------------------
int rotatedNumber[25];   // kept for future rotation use

// Default color values
int RGB_COLOR_WHITE        = 0xffffff;
int RGB_COLOR_DIMWHITE     = 0x555555;
int RGB_COLOR_WARMWHITE    = 0xFFEBC8;
int RGB_COLOR_DIMWARMWHITE = 0x877D5F;
int RGB_COLOR_BLACK        = 0x000000;
int RGB_COLOR_RED          = 0xff0000;
int RGB_COLOR_ORANGE       = 0xa5ff00;
int RGB_COLOR_YELLOW       = 0xffff00;
int RGB_COLOR_DIMYELLOW    = 0x555500;
int RGB_COLOR_GREEN        = 0x008800; // toned down
int RGB_COLOR_BLUE         = 0x0000ff;
int RGB_COLOR_PURPLE       = 0x008080;

int numbercolor = RGB_COLOR_WARMWHITE;

int flashcolor[]  = {RGB_COLOR_WHITE, RGB_COLOR_WHITE};
int offcolor[]    = {RGB_COLOR_BLACK, numbercolor};
int badcolor[]    = {RGB_COLOR_BLACK, RGB_COLOR_RED};
int readycolor[]  = {RGB_COLOR_BLACK, RGB_COLOR_GREEN};
int alloffcolor[] = {RGB_COLOR_BLACK, RGB_COLOR_BLACK};
int wificolor[]   = {RGB_COLOR_BLACK, RGB_COLOR_BLUE};
int infocolor[]   = {RGB_COLOR_BLACK, RGB_COLOR_ORANGE};

// Number glyphs (only 0 is used at the moment as a “dot”)
int number[17][25] = {
  {
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 1,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
  }, // Number 0 - (single dot)
  { 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1,
    1, 1, 1, 1, 1,
    0, 1, 0, 0, 1,
    0, 0, 0, 0, 0
  }, // Number 1
  { 0, 0, 0, 0, 0,
    1, 1, 1, 0, 1,
    1, 0, 1, 0, 1,
    1, 0, 1, 1, 1,
    0, 0, 0, 0, 0
  }, // Number 2
  { 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 0, 1, 0, 1,
    0, 0, 0, 0, 0
  }, // Number 3
  { 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    0, 0, 1, 0, 0,
    1, 1, 1, 0, 0,
    0, 0, 0, 0, 0
  }, // Number 4
  { 0, 0, 0, 0, 0,
    1, 0, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 1, 1, 0, 1,
    0, 0, 0, 0, 0
  }, // Number 5
  { 0, 0, 0, 0, 0,
    1, 0, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0
  }, // Number 6
  { 0, 0, 0, 0, 0,
    1, 1, 0, 0, 0,
    1, 0, 1, 0, 0,
    1, 0, 0, 1, 1,
    0, 0, 0, 0, 0
  }, // Number 7
  { 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0
  }, // Number 8
  { 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 1, 1, 0, 1,
    0, 0, 0, 0, 0
  }, // Number 9
  { 1, 1, 1, 1, 1,
    1, 0, 0, 0, 1,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1
  }, // Number 10
  { 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0
  }, // Number 11
  { 1, 1, 1, 0, 1,
    1, 0, 1, 0, 1,
    1, 0, 1, 1, 1,
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1
  }, // Number 12
  { 1, 1, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 0, 1, 0, 1,
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1
  }, // Number 13
  { 1, 1, 1, 1, 1,
    0, 0, 1, 0, 0,
    1, 1, 1, 0, 0,
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1
  }, // Number 14
  { 1, 0, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 1, 1, 0, 1,
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1
  }, // Number 15
  { 1, 0, 1, 1, 1,
    1, 0, 1, 0, 1,
    1, 1, 1, 1, 1,
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1
  }, // Number 16
};

// Icons for WiFi / setup / good / error, etc.
int icons[13][25] = {
  { 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1
  }, // full blank (used as fill)
  { 0, 0, 1, 1, 1,
    0, 1, 0, 0, 0,
    1, 0, 0, 1, 1,
    1, 0, 1, 0, 0,
    1, 0, 1, 0, 1
  }, // wifi 3 rings
  { 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 1, 1,
    0, 0, 1, 0, 0,
    0, 0, 1, 0, 1
  }, // wifi 2 rings
  { 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 1
  }, // wifi 1 ring
  { 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
  }, // reassign 1
  { 0, 0, 0, 0, 0,
    0, 1, 1, 1, 0,
    0, 1, 0, 1, 0,
    0, 1, 1, 1, 0,
    0, 0, 0, 0, 0
  }, // reassign 2
  { 1, 1, 1, 1, 1,
    1, 0, 0, 0, 1,
    1, 0, 0, 0, 1,
    1, 0, 0, 0, 1,
    1, 1, 1, 1, 1
  }, // reassign 3
  { 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
  }, // setup 1
  { 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0,
    1, 0, 0, 0, 1,
    0, 0, 0, 0, 0,
    0, 0, 1, 0, 0
  }, // setup 3 (slight tweak)
  { 1, 0, 0, 0, 1,
    0, 1, 0, 1, 0,
    0, 0, 1, 0, 0,
    0, 1, 0, 1, 0,
    1, 0, 0, 0, 1
  }, // error
  { 0, 1, 0, 0, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 1, 0,
    0, 0, 0, 0, 1,
    0, 0, 0, 1, 0
  }, // good (tick)
  { 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
  }, // no icon
};

// -------------------------------------------------------------------
// WiFiManager Parameters
// -------------------------------------------------------------------
WiFiManagerParameter* custom_companionIP;
WiFiManagerParameter* custom_companionPort;

// Logger
void logger(const String& s, const String& type = "info") {
  Serial.println(s);
}

// -------------------------------------------------------------------
// Matrix drawing helpers (Tally-Arbiter style)
// -------------------------------------------------------------------
void drawNumberArray(int arr[25], int colors[2]) {
  for (int i = 0; i < 25; i++) {
    int colorIndex = arr[i];  // 0 or 1
    int rgb        = colors[colorIndex];
    M5.dis.drawpix(i, rgb);
  }
}

void drawMultiple(int arr[25], int colors[2], int times, int delaysMs) {
  for (int t = 0; t < times; t++) {
    drawNumberArray(arr, colors);
    delay(delaysMs);
  }
}

// Clear Matrix with black
void matrixOff() {
  M5.dis.fillpix(0x000000);
}

// -------------------------------------------------------------------
// Config param helpers
// -------------------------------------------------------------------
String getParam(const String& name) {
  if (wifiManager.server && wifiManager.server->hasArg(name))
    return wifiManager.server->arg(name);
  return "";
}

void saveParamCallback() {
  String str_companionIP   = getParam("companionIP");
  String str_companionPort = getParam("companionPort");

  preferences.begin("companion", false);
  if (str_companionIP.length() > 0)   preferences.putString("companionip", str_companionIP);
  if (str_companionPort.length() > 0) preferences.putString("companionport", str_companionPort);
  preferences.end();
}

// ------------------------------------------------------------
// Boot counter management
// ------------------------------------------------------------
int eepromReadBootCounter() {
  preferences.begin("companion", true);
  int count = preferences.getInt("bootCounter", 0);
  preferences.end();
  return count;
}

void eepromWriteBootCounter(int count) {
  preferences.begin("companion", false);
  preferences.putInt("bootCounter", count);
  preferences.end();
}

// ------------------------------------------------------------
// Config portal functions
// ------------------------------------------------------------
void startConfigPortal() {
  Serial.println("[WiFi] Entering CONFIG PORTAL mode");
  
  // Load Companion config from preferences (for default field values)
  preferences.begin("companion", true);
  String savedHost = preferences.getString("companionip", "Companion IP");
  String savedPort = preferences.getString("companionport", "16622");
  preferences.end();

  // Prepare WiFiManager with params
  custom_companionIP   = new WiFiManagerParameter("companionIP", "Companion IP", savedHost.c_str(), 40);
  custom_companionPort = new WiFiManagerParameter("companionPort", "Satellite Port", savedPort.c_str(), 6);

  wifiManager.addParameter(custom_companionIP);
  wifiManager.addParameter(custom_companionPort);
  wifiManager.setSaveParamsCallback(saveParamCallback);

  std::vector<const char*> menu = { "wifi", "param", "info", "sep", "restart", "exit" };
  wifiManager.setMenu(menu);
  wifiManager.setClass("invert");
  wifiManager.setConfigPortalTimeout(0); // No timeout when we explicitly call config mode

  wifiManager.setAPCallback([](WiFiManager* wm) {
    Serial.println("[WiFi] Config portal started");
    // Draw WiFi icon in orange when portal is active
    int wificolor[] = {RGB_COLOR_BLACK, RGB_COLOR_ORANGE};
    drawNumberArray(icons[1], wificolor);
  });

  // Show setup icons while portal is active
  drawNumberArray(icons[7], infocolor);

  // Start AP + portal, blocks until user saves or exits
  String shortDeviceID = "m5atom-matrix_" + deviceID.substring(deviceID.length() - 5);
  wifiManager.startConfigPortal(shortDeviceID.c_str(), AP_password);
  Serial.printf("[WiFi] Config portal started - SSID: %s\n", shortDeviceID.c_str());

  // After returning, update our Companion host/port and persist
  strncpy(companion_host, custom_companionIP->getValue(), sizeof(companion_host));
  companion_host[sizeof(companion_host) - 1] = '\0';

  strncpy(companion_port, custom_companionPort->getValue(), sizeof(companion_port));
  companion_port[sizeof(companion_port) - 1] = '\0';

  // Save to preferences
  preferences.begin("companion", false);
  preferences.putString("companionip", String(companion_host));
  preferences.putString("companionport", String(companion_port));
  preferences.end();

  Serial.println("[WiFi] Config portal completed");
  Serial.printf("[WiFi] Companion Host: %s\n", companion_host);
  Serial.printf("[WiFi] Companion Port: %s\n", companion_port);
}

// -------------------------------------------------------------------
// External LED + Matrix color handling
// -------------------------------------------------------------------
void setExternalLedColor(uint8_t r, uint8_t g, uint8_t b) {
  lastColorR = r;
  lastColorG = g;
  lastColorB = b;

  uint8_t scaledR = r * max(brightness, 15) / 100;
  uint8_t scaledG = g * max(brightness, 15) / 100;
  uint8_t scaledB = b * max(brightness, 15) / 100;

  Serial.print("[COLOR] raw r/g/b = ");
  Serial.print(r); Serial.print("/");
  Serial.print(g); Serial.print("/");
  Serial.print(b);
  Serial.print("  scaled = ");
  Serial.print(scaledR); Serial.print("/");
  Serial.print(scaledG); Serial.print("/");
  Serial.println(scaledB);

  // External RGB LED using new core 3.x API (pin-based)
  ledcWrite(LED_PIN_RED,   scaledR);
  ledcWrite(LED_PIN_GREEN, scaledG);
  ledcWrite(LED_PIN_BLUE,  scaledB);

  // Also light the whole Matrix in that color (Tally style)
  int rgb = (scaledR << 16) | (scaledG << 8) | scaledB;
  M5.dis.fillpix(rgb);
}

// -------------------------------------------------------------------
// Companion / Satellite API
// -------------------------------------------------------------------
void sendAddDevice() {
  String cmd;
  String companionDeviceID = "m5atom-matrix:" + deviceID.substring(deviceID.length() - 5); // Use last 5 chars like LEDMatrixClock
  
  cmd = "ADD-DEVICE DEVICEID=" + companionDeviceID +
        " PRODUCT_NAME=\"M5 Atom Matrix\" "
        "KEYS_TOTAL=1 KEYS_PER_ROW=1 "
        "COLORS=rgb TEXT=true";
  client.println(cmd);
  Serial.println("[API] Sent: " + cmd);
}

void handleKeyState(const String& line) {
  Serial.println("[API] KEY-STATE line: " + line);

  // COLOR="rgba(r,g,b,a)"
  int colorPos = line.indexOf("COLOR=");
  if (colorPos >= 0) {
    int start = colorPos + 6;
    int end = line.indexOf(' ', start);
    if (end < 0) end = line.length();
    String c = line.substring(start, end);
    c.trim();

    Serial.println("[API] COLOR raw: " + c);

    if (c.startsWith("\"") && c.endsWith("\""))
      c = c.substring(1, c.length() - 1);

    if (c.startsWith("rgba(")) {
      c.replace("rgba(", "");
      c.replace(")", "");
      c.replace(" ", "");

      int p1 = c.indexOf(',');
      int p2 = c.indexOf(',', p1+1);
      int p3 = c.indexOf(',', p2+1);

      int r = c.substring(0, p1).toInt();
      int g = c.substring(p1+1, p2).toInt();
      int b = c.substring(p2+1, p3).toInt();

      Serial.print("[API] Parsed COLOR r/g/b = ");
      Serial.print(r); Serial.print("/");
      Serial.print(g); Serial.print("/");
      Serial.println(b);

      setExternalLedColor(r, g, b);
    } else {
      Serial.println("[API] COLOR is not rgba(), ignoring.");
    }
  } else {
    Serial.println("[API] No COLOR= field in KEY-STATE.");
  }
}

void parseAPI(const String& apiData) {
  if (apiData.length() == 0) return;
  if (apiData.startsWith("PONG"))   return;

  Serial.println("[API] RX: " + apiData);

  if (apiData.startsWith("PING")) {
    String payload = apiData.substring(apiData.indexOf(' ') + 1);
    client.println("PONG " + payload);
    return;
  }

  if (apiData.startsWith("BRIGHTNESS")) {
    int valPos = apiData.indexOf("VALUE=");
    String v = apiData.substring(valPos + 6);
    brightness = v.toInt();
    Serial.println("[API] BRIGHTNESS set to " + String(brightness));
    setExternalLedColor(lastColorR, lastColorG, lastColorB);
    return;
  }

  if (apiData.startsWith("KEYS-CLEAR")) {
    Serial.println("[API] KEYS-CLEAR received");
    matrixOff();
    setExternalLedColor(0,0,0);
    return;
  }

  if (apiData.startsWith("KEY-STATE")) {
    handleKeyState(apiData);
    return;
  }
}

// ------------------------------------------------------------
// REST API Handlers for Companion Configuration
// ------------------------------------------------------------
void handleGetHost() {
  Serial.println("[REST] GET /api/host request received");
  Serial.println("[REST] Current companion_host: '" + String(companion_host) + "'");
  restServer.send(200, "text/plain", companion_host);
  Serial.println("[REST] GET /api/host: " + String(companion_host));
}

void handleGetPort() {
  Serial.println("[REST] GET /api/port request received");
  Serial.println("[REST] Current companion_port: '" + String(companion_port) + "'");
  restServer.send(200, "text/plain", companion_port);
  Serial.println("[REST] GET /api/port: " + String(companion_port));
}

void handleGetConfig() {
  String json = "{\"host\":\"" + String(companion_host) + "\",\"port\":" + String(companion_port) + "}";
  Serial.println("[REST] GET /api/config request received");
  Serial.println("[REST] Response JSON: " + json);
  restServer.send(200, "application/json", json);
  Serial.println("[REST] GET /api/config: " + json);
}

void handlePostHost() {
  String newHost = "";
  
  Serial.println("[REST] POST /api/host request received");
  
  // Try to parse JSON first
  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();
    Serial.println("[REST] Request body: '" + body + "'");
    
    // Check if it's JSON format
    if (body.startsWith("{") && body.endsWith("}")) {
      Serial.println("[REST] Parsing JSON format");
      int hostPos = body.indexOf("\"host\":");
      if (hostPos >= 0) {
        int startQuote = body.indexOf("\"", hostPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newHost = body.substring(startQuote + 1, endQuote);
          Serial.println("[REST] Extracted host from JSON: '" + newHost + "'");
        }
      }
    } else {
      // Plain text format
      newHost = body;
      Serial.println("[REST] Using plain text format: '" + newHost + "'");
    }
  } else {
    Serial.println("[REST] No plain body found in request");
  }
  
  newHost.trim();
  
  if (newHost.length() > 0 && newHost.length() < sizeof(companion_host)) {
    strncpy(companion_host, newHost.c_str(), sizeof(companion_host));
    companion_host[sizeof(companion_host) - 1] = '\0';
    
    Serial.println("[REST] Updating companion_host to: '" + String(companion_host) + "'");
    
    // Save to preferences
    Serial.println("[REST] Saving to preferences...");
    preferences.begin("companion", false);
    preferences.putString("companionip", String(companion_host));
    preferences.end();
    Serial.println("[REST] Preferences saved");
    
    // Update WiFiManager parameter
    if (custom_companionIP) {
      custom_companionIP->setValue(companion_host, sizeof(companion_host));
      Serial.println("[REST] WiFiManager parameter updated");
    }
    
    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/host: Updated to " + String(companion_host));
    
    // Reestablish connection
    if (client.connected()) {
      client.stop();
    }
  } else {
    Serial.println("[REST] Invalid host - length: " + String(newHost.length()) + " content: '" + newHost + "'");
    restServer.send(400, "text/plain", "Invalid host");
    Serial.println("[REST] POST /api/host: Invalid host - " + newHost);
  }
}

void handlePostPort() {
  String newPort = "";
  
  // Try to parse JSON first
  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();
    
    // Check if it's JSON format
    if (body.startsWith("{") && body.endsWith("}")) {
      int portPos = body.indexOf("\"port\":");
      if (portPos >= 0) {
        int startQuote = body.indexOf("\"", portPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newPort = body.substring(startQuote + 1, endQuote);
        }
      }
    } else {
      // Plain text format
      newPort = body;
    }
  }
  
  newPort.trim();
  
  // Validate port number
  int portNum = newPort.toInt();
  if (portNum > 0 && portNum <= 65535) {
    strncpy(companion_port, newPort.c_str(), sizeof(companion_port));
    companion_port[sizeof(companion_port) - 1] = '\0';
    
    // Save to preferences
    preferences.begin("companion", false);
    preferences.putString("companionport", String(companion_port));
    preferences.end();
    
    // Update WiFiManager parameter
    if (custom_companionPort) {
      custom_companionPort->setValue(companion_port, sizeof(companion_port));
    }
    
    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/port: Updated to " + String(companion_port));
    
    // Reestablish connection
    if (client.connected()) {
      client.stop();
    }
  } else {
    restServer.send(400, "text/plain", "Invalid port number");
    Serial.println("[REST] POST /api/port: Invalid port - " + newPort);
  }
}

void handlePostConfig() {
  String newHost = "";
  String newPort = "";
  
  Serial.println("[REST] POST /api/config request received");
  
  if (restServer.hasArg("plain")) {
    String body = restServer.arg("plain");
    body.trim();
    Serial.println("[REST] Request body: '" + body + "'");
    
    if (body.startsWith("{") && body.endsWith("}")) {
      Serial.println("[REST] Parsing JSON format");
      
      // Parse host
      int hostPos = body.indexOf("\"host\":");
      if (hostPos >= 0) {
        int startQuote = body.indexOf("\"", hostPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newHost = body.substring(startQuote + 1, endQuote);
          Serial.println("[REST] Extracted host from JSON: '" + newHost + "'");
        }
      }
      
      // Parse port
      int portPos = body.indexOf("\"port\":");
      if (portPos >= 0) {
        // Try quoted port first
        int startQuote = body.indexOf("\"", portPos + 7);
        int endQuote = body.indexOf("\"", startQuote + 1);
        if (startQuote >= 0 && endQuote > startQuote) {
          newPort = body.substring(startQuote + 1, endQuote);
          Serial.println("[REST] Extracted quoted port from JSON: '" + newPort + "'");
        } else {
          // Try unquoted port number
          int startNum = portPos + 7;
          // Skip whitespace and colon
          while (startNum < body.length() && (body.charAt(startNum) == ' ' || body.charAt(startNum) == ':')) {
            startNum++;
          }
          // Find the end by looking for comma or closing brace
          int endNumComma = body.indexOf(",", startNum);
          int endNumBrace = body.indexOf("}", startNum);
          int endNum = -1;
          
          // Use the closest delimiter
          if (endNumComma >= 0 && endNumBrace >= 0) {
            endNum = (endNumComma < endNumBrace) ? endNumComma : endNumBrace;
          } else if (endNumComma >= 0) {
            endNum = endNumComma;
          } else if (endNumBrace >= 0) {
            endNum = endNumBrace;
          }
          
          if (endNum >= 0) {
            newPort = body.substring(startNum, endNum);
            newPort.trim();
            Serial.println("[REST] Extracted unquoted port from JSON: '" + newPort + "'");
          }
        }
      }
    }
  }
  
  newHost.trim();
  newPort.trim();
  
  // Validate
  bool hostValid = (newHost.length() > 0 && newHost.length() < sizeof(companion_host));
  int portNum = newPort.toInt();
  bool portValid = (portNum > 0 && portNum <= 65535);
  
  if (hostValid && portValid) {
    strncpy(companion_host, newHost.c_str(), sizeof(companion_host));
    companion_host[sizeof(companion_host) - 1] = '\0';
    strncpy(companion_port, newPort.c_str(), sizeof(companion_port));
    companion_port[sizeof(companion_port) - 1] = '\0';
    
    // Save to preferences
    preferences.begin("companion", false);
    preferences.putString("companionip", String(companion_host));
    preferences.putString("companionport", String(companion_port));
    preferences.end();
    
    // Update WiFiManager parameters
    if (custom_companionIP) {
      custom_companionIP->setValue(companion_host, sizeof(companion_host));
    }
    if (custom_companionPort) {
      custom_companionPort->setValue(companion_port, sizeof(companion_port));
    }
    
    restServer.send(200, "text/plain", "OK");
    Serial.println("[REST] POST /api/config: Updated host=" + String(companion_host) + " port=" + String(companion_port));
    
    // Reestablish connection
    if (client.connected()) {
      client.stop();
    }
  } else {
    Serial.println("[REST] Invalid config - host: '" + newHost + "' (valid: " + String(hostValid) + ") port: '" + newPort + "' (valid: " + String(portValid) + ")");
    restServer.send(400, "text/plain", "Invalid config");
  }
}

void setupRestServer() {
  restServer.on("/api/host", HTTP_GET, handleGetHost);
  restServer.on("/api/port", HTTP_GET, handleGetPort);
  restServer.on("/api/config", HTTP_GET, handleGetConfig);
  
  restServer.on("/api/host", HTTP_POST, handlePostHost);
  restServer.on("/api/port", HTTP_POST, handlePostPort);
  restServer.on("/api/config", HTTP_POST, handlePostConfig);
  
  restServer.begin();
  Serial.println("[REST] REST API server started on port 9999");
  Serial.println("[REST] Available endpoints:");
  Serial.println("[REST]   GET  /api/host");
  Serial.println("[REST]   GET  /api/port");
  Serial.println("[REST]   GET  /api/config");
  Serial.println("[REST]   POST /api/host");
  Serial.println("[REST]   POST /api/port");
  Serial.println("[REST]   POST /api/config");
}

// -------------------------------------------------------------------
// WiFi + Config Portal
// -------------------------------------------------------------------
void connectToNetwork() {
  if (stationIP != IPAddress(0,0,0,0))
    wifiManager.setSTAStaticIPConfig(stationIP, stationGW, stationMask);

  WiFi.mode(WIFI_STA);
  logger("Connecting to SSID: " + String(WiFi.SSID()), "info");

  custom_companionIP   = new WiFiManagerParameter("companionIP", "Companion IP", companion_host, 40);
  custom_companionPort = new WiFiManagerParameter("companionPort", "Satellite Port", companion_port, 6);

  wifiManager.addParameter(custom_companionIP);
  wifiManager.addParameter(custom_companionPort);
  wifiManager.setSaveParamsCallback(saveParamCallback);

  std::vector<const char*> menu = { "wifi", "param", "info", "sep", "restart", "exit" };
  wifiManager.setMenu(menu);
  wifiManager.setClass("invert");
  wifiManager.setConfigPortalTimeout(180); // 3 minutes auto portal if WiFi fails

  // Set hostname in WiFiManager to prevent ESP32 default override
  String wifiHostname = "m5atom-matrix_" + deviceID.substring(deviceID.length() - 5);
  wifiManager.setHostname(wifiHostname.c_str());
  Serial.printf("[WiFi] WiFiManager hostname set to: %s\n", wifiHostname.c_str());

  wifiManager.setAPCallback([](WiFiManager* wm) {
    Serial.println("[WiFi] Config portal started");
    // Draw WiFi icon in orange when portal is active
    int wificolor[] = {RGB_COLOR_BLACK, RGB_COLOR_ORANGE};
    drawNumberArray(icons[1], wificolor);
  });

  // Normal autoConnect behaviour (connect to WiFi, or start portal if no WiFi)
  // WiFi connect animation (wifi rings)
  drawNumberArray(icons[3], wificolor);
  delay(300);
  drawNumberArray(icons[2], wificolor);
  delay(300);
  drawNumberArray(icons[1], wificolor);
  delay(300);

  // Use shortened device ID for WiFi portal name (underscore format)
  String shortDeviceID = "m5atom-matrix_" + deviceID.substring(deviceID.length() - 5);  // Use last 5 chars like LEDMatrixClock
  bool res = wifiManager.autoConnect(shortDeviceID.c_str(), AP_password);
  Serial.printf("[WiFi] AutoConnect - SSID: %s\n", shortDeviceID.c_str());

  if (!res) {
    logger("Failed to connect", "error");
    drawNumberArray(icons[9], badcolor); // error icon
    Serial.println("[WiFi] Failed to connect, starting config portal...");
    // WiFiManager will automatically start config portal on failure
    // No need to restart - let WiFiManager handle it
  } else {
    logger("connected...yay :)", "info");
    drawNumberArray(icons[11], readycolor); // good tick
    delay(400);
    
    // Verify and reset hostname after connection to ensure it persists
    String currentHostname = WiFi.getHostname();
    String expectedHostname = "m5atom-matrix_" + deviceID.substring(deviceID.length() - 5);
    if (currentHostname != expectedHostname) {
      Serial.printf("[WiFi] Hostname mismatch, resetting from '%s' to '%s'\n", currentHostname.c_str(), expectedHostname.c_str());
      WiFi.setHostname(expectedHostname.c_str());
      delay(100);
      Serial.printf("[WiFi] Hostname reset to: %s\n", WiFi.getHostname());
    } else {
      Serial.printf("[WiFi] Hostname confirmed: %s\n", currentHostname.c_str());
    }
  }
}

// -------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("Booting M5 Atom Matrix Companion v4…");

  // Make sure WiFi is initialised so MAC is valid
  WiFi.mode(WIFI_STA);
  delay(100);

  // Build deviceID from full MAC
  uint8_t mac[6];
  WiFi.macAddress(mac);

  char macBuf[13];
  sprintf(macBuf, "%02X%02X%02X%02X%02X%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  deviceID = "m5atom-matrix_";
  deviceID += macBuf;
  deviceID.toUpperCase();

  Serial.print("DeviceID: ");
  Serial.println(deviceID);

  // Load preferences (host/port override)
  preferences.begin("companion", false);
  if (preferences.getString("companionip").length() > 0)
    preferences.getString("companionip").toCharArray(companion_host, sizeof(companion_host));

  if (preferences.getString("companionport").length() > 0)
    preferences.getString("companionport").toCharArray(companion_port, sizeof(companion_port));
  preferences.end();

  Serial.print("Companion Host: ");
  Serial.println(companion_host);
  Serial.print("Companion Port: ");
  Serial.println(companion_port);

  // Save battery by turning off Bluetooth
  btStop();

  // Init M5 Atom
  M5.begin(true, false, true);
  delay(50);
  M5.dis.setBrightness(32);  // nice low-ish brightness for both test + runtime
  matrixOff();

  // Boot icon (simple “setup” sequence)
  drawNumberArray(icons[7], infocolor);
  delay(300);
  drawNumberArray(icons[8], infocolor);
  delay(300);
  drawNumberArray(icons[7], infocolor);
  delay(300);
  matrixOff();

  // External LED setup
  pinMode(LED_PIN_GND, OUTPUT);
  digitalWrite(LED_PIN_GND, LOW);

  Serial.println("[LED] Initialising PWM (esp32-hal-ledc, pin-based)...");
  bool okR = ledcAttach(LED_PIN_RED,   pwmFreq, pwmResolution);
  bool okG = ledcAttach(LED_PIN_GREEN, pwmFreq, pwmResolution);
  bool okB = ledcAttach(LED_PIN_BLUE,  pwmFreq, pwmResolution);

  Serial.print("[LED] ledcAttach RED: ");   Serial.println(okR);
  Serial.print("[LED] ledcAttach GREEN: "); Serial.println(okG);
  Serial.print("[LED] ledcAttach BLUE: ");  Serial.println(okB);

  setExternalLedColor(0,0,0);

  Serial.println("[LED] Running power-on colour test (R/G/B)...");
  setExternalLedColor(255, 0, 0);
  delay(250);
  setExternalLedColor(0, 255, 0);
  delay(250);
  setExternalLedColor(0, 0, 255);
  delay(250);
  setExternalLedColor(0,0,0);

  // WiFi connect (with icons)
  
  // Boot counter logic for config portal trigger
  bootCountCached = eepromReadBootCounter();
  Serial.printf("[Boot] Boot counter read: %u\n", bootCountCached);
  
  if (bootCountCached == 1) {
    // Boot counter 1 → trigger config portal (user reset during boot animations)
    Serial.println("[Boot] Boot counter 1 → triggering config portal");
    eepromWriteBootCounter(0);  // Reset immediately
    bootCountCached = 0;
    startConfigPortal();
    // startConfigPortal() will handle setup icons
    return;  // Skip normal boot sequence
  } else {
    // Boot counter 0 (or any other value) → normal boot
    Serial.println("[Boot] Boot counter 0 → normal boot");
    // Set boot counter to 1 during boot animations so user can reset to trigger portal
    eepromWriteBootCounter(1);
  }
  
  connectToNetwork();

  // OTA
  ArduinoOTA.setHostname(deviceID.c_str());
  ArduinoOTA.setPassword("companion-satellite");
  ArduinoOTA.begin();

  // Start REST API server after WiFi is connected
  setupRestServer();

  // Initialize mDNS service after WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[mDNS] Starting mDNS service...");
    
    // Extract short MAC for hostname (first 5 chars like LEDMatrixClock)
    String macShort = deviceID.substring(deviceID.length() - 5); // Use last 5 chars like LEDMatrixClock
    
    // Start mDNS with underscore hostname format
    String mDNSHostname = "m5atom-matrix_" + macShort;
    if (!MDNS.begin(mDNSHostname.c_str())) {
      Serial.println("[mDNS] Error setting up mDNS responder!");
    } else {
      Serial.println("[mDNS] mDNS responder started");
      Serial.printf("[mDNS] Hostname: %s.local\n", mDNSHostname.c_str());
      Serial.printf("[mDNS] IP Address: %s\n", WiFi.localIP().toString().c_str());
      
      // Set instance name for mDNS
      String mDNSInstanceName = "m5atom-matrix:" + macShort;
      MDNS.setInstanceName(mDNSInstanceName);
      Serial.printf("[mDNS] Instance name set to: %s\n", mDNSInstanceName.c_str());
      
      // Add companion-satellite service
      bool serviceAdded = MDNS.addService("companion-satellite", "tcp", 9999);
      if (serviceAdded) {
        Serial.println("[mDNS] SUCCESS: companion-satellite service registered!");
        
        // Add service text records (matching LEDMatrixClock)
        MDNS.addServiceTxt("companion-satellite", "tcp", "restEnabled", "true");
        Serial.println("[mDNS] Added service TXT record: restEnabled=true");
        
        // ESP32 mDNS handles updates automatically, just add small delays
        for (int i = 0; i < 3; i++) {
          delay(100);
          Serial.printf("[mDNS] Setup delay %d/3 completed\n", i+1);
        }
        
        Serial.println("[mDNS] Setup complete - service discoverable");
        Serial.println("[mDNS] Test with: dns-sd -B companion-satellite._tcp");
        Serial.println("[mDNS] SUCCESS: Full companion-satellite service name working!");
      } else {
        Serial.println("[mDNS] ERROR: companion-satellite service registration failed!");
      }
    }
  }

  // Show “waiting for Companion” icon (single dot)
  drawNumberArray(number[0], offcolor);
  
  // Successful boot completed - set boot counter to 0
  eepromWriteBootCounter(0);
  Serial.println("[Boot] Successful boot completed - boot counter reset to 0");
  
  Serial.println("[System] Setup complete, entering main loop.");
}

// -------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------
void loop() {
  M5.update();
  ArduinoOTA.handle();
  restServer.handleClient();

  unsigned long now = millis();

  // Companion connect / reconnect
  if (!client.connected() && (now - lastConnectTry >= connectRetryMs)) {
    lastConnectTry = now;
    Serial.print("[NET] Connecting to Companion ");
    Serial.print(companion_host);
    Serial.print(":");
    Serial.println(companion_port);

    if (client.connect(companion_host, atoi(companion_port))) {
      Serial.println("[NET] Connected to Companion API");
      // Good icon when Companion connects
      drawNumberArray(icons[11], readycolor);
      delay(300);
      drawNumberArray(number[0], offcolor); // back to dot
      sendAddDevice();
      lastPingTime = millis();
    } else {
      Serial.println("[NET] Companion connect failed");
      drawNumberArray(icons[9], badcolor); // error icon briefly
      delay(200);
      drawNumberArray(number[0], offcolor);
    }
  }

  if (client.connected()) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) parseAPI(line);
    }

    if (M5.Btn.wasPressed()) {
      Serial.println("[BTN] Short press -> KEY=0 PRESSED=true");
      String companionDeviceID = "m5atom-matrix:" + deviceID.substring(deviceID.length() - 5);
      client.println("KEY-PRESS DEVICEID=" + companionDeviceID + " KEY=0 PRESSED=true");
    }

    if (M5.Btn.wasReleased()) {
      Serial.println("[BTN] Release -> KEY=0 PRESSED=false");
      String companionDeviceID = "m5atom-matrix:" + deviceID.substring(deviceID.length() - 5);
      client.println("KEY-PRESS DEVICEID=" + companionDeviceID + " KEY=0 PRESSED=false");
    }

    if (now - lastPingTime >= pingIntervalMs) {
      client.println("PING m5atom");
      lastPingTime = now;
    }
  }

  delay(10);
}

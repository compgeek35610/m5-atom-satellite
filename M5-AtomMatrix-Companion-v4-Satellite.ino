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

Preferences preferences;
WiFiManager wifiManager;
WiFiClient client;

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
  String cmd = "ADD-DEVICE DEVICEID=" + deviceID +
               " PRODUCT_NAME=\"M5 Atom Matrix\" KEYS_TOTAL=1 KEYS_PER_ROW=1 COLORS=rgb TEXT=true";
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
  wifiManager.setConfigPortalTimeout(120);

  // WiFi connect animation (wifi rings)
  drawNumberArray(icons[3], wificolor);
  delay(300);
  drawNumberArray(icons[2], wificolor);
  delay(300);
  drawNumberArray(icons[1], wificolor);
  delay(300);

  bool res = wifiManager.autoConnect(deviceID.c_str(), AP_password);

  if (!res) {
    logger("Failed to connect", "error");
    drawNumberArray(icons[9], badcolor); // error icon
  } else {
    logger("connected...yay :)", "info");
    drawNumberArray(icons[11], readycolor); // good tick
    delay(400);
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

  deviceID = "M5ATOM_";
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
  setExternalLedColor(0, 0, 0);

  // Hostname
  WiFi.setHostname(deviceID.c_str());
  wifiManager.setHostname(deviceID.c_str());

  // WiFi connect (with icons)
  connectToNetwork();

  // OTA
  ArduinoOTA.setHostname(deviceID.c_str());
  ArduinoOTA.setPassword("companion-satellite");
  ArduinoOTA.begin();

  // Show “waiting for Companion” icon (single dot)
  drawNumberArray(number[0], offcolor);
  Serial.println("[System] Setup complete, entering main loop.");
}

// -------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------
void loop() {
  M5.update();
  ArduinoOTA.handle();

  // Long press (5s) – open WiFiManager config portal
  if (M5.Btn.pressedFor(5000)) {

    // Force a KEY RELEASE in Companion so it never stays “held”
    if (client.connected()) {
      Serial.println("[BTN] Long press, sending KEY RELEASE before config portal");
      client.println("KEY-PRESS DEVICEID=" + deviceID + " KEY=0 PRESSED=false");
    }

    delay(50);

    // Show setup icons while portal is active
    drawNumberArray(icons[7], infocolor);

    while (wifiManager.startConfigPortal(deviceID.c_str(), AP_password)) {}

    ESP.restart();
  }

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
      client.println("KEY-PRESS DEVICEID=" + deviceID + " KEY=0 PRESSED=true");
    }

    if (M5.Btn.wasReleased()) {
      Serial.println("[BTN] Release -> KEY=0 PRESSED=false");
      client.println("KEY-PRESS DEVICEID=" + deviceID + " KEY=0 PRESSED=false");
    }

    if (now - lastPingTime >= pingIntervalMs) {
      client.println("PING m5atom");
      lastPingTime = now;
    }
  }

  delay(10);
}

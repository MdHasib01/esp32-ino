#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <esp_task_wdt.h>

using namespace websockets;

#define LED_PIN 2

// ==========================
// CONFIG
// ==========================
// 1. Register this device from the dashboard's Settings page first —
//    it gives you a deviceId and a one-time apiKey.
// 2. wsServer points at the deployed backend — the device WebSocket lives
//    under /api/ws/device (TLS, via esp32.hasibdev.online) and takes
//    deviceId as a query param.

const char* wsServer = "wss://esp32.hasibdev.online/api/ws/device?deviceId=esp32-livingroom";
const char* apiKey = "dk_6852d0acbf71d430ee9fb24aa6b618a7aabdd8acb796c28c";

// Must stay well under the backend's HEARTBEAT_TIMEOUT_MS (10s by default) —
// this board has no mains-sensing circuit, so the backend infers a power
// cut purely from heartbeat silence lasting that long.
const unsigned long HEARTBEAT_INTERVAL_MS = 3000;
const unsigned long RECONNECT_INTERVAL_MS = 5000;

// Fixed self-restart so a long-running board doesn't freeze. Not
// configurable from the dashboard — the firmware always enforces it.
const unsigned long RESTART_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 minutes

// Hardware task watchdog — if loop() itself hangs (so the timed restart
// above can never run), the chip resets after this many seconds.
const int WDT_TIMEOUT_S = 30;

// Local fallback PC MAC/broadcast — the server normally sends the MAC to
// wake in its wake_pc command (set in Settings), this is only used if that
// field is ever missing.
byte pcMac[] = { 0xD8, 0x43, 0xAE, 0xB8, 0x63, 0x1A };
IPAddress broadcastIP(192, 168, 0, 255);

// ==========================

WebsocketsClient client;
WiFiUDP udp;

bool authenticated = false;
unsigned long lastHeartbeat = 0;
unsigned long lastReconnectAttempt = 0;

// Blinks the on-board LED on command — a quick "is this specific unit
// actually alive and reachable" check, independent of power-monitor state.
bool ledBlink = false;
bool ledState = false;
unsigned long lastBlink = 0;

// Restart timer — counts from boot.
unsigned long restartTimerStart = 0;

// ==========================
// WAKE ON LAN
// ==========================

void wakePC(const byte* mac) {
  byte packet[102];
  for (int i = 0; i < 6; i++) packet[i] = 0xFF;
  for (int i = 1; i <= 16; i++) memcpy(&packet[i * 6], mac, 6);

  udp.beginPacket(broadcastIP, 9);
  udp.write(packet, 102);
  udp.endPacket();

  Serial.println("Wake-on-LAN packet sent");
}

void parseMac(const char* macStr, byte* out) {
  int values[6];
  if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6) {
    for (int i = 0; i < 6; i++) out[i] = (byte)values[i];
  }
}

// ==========================
// BACKEND PROTOCOL
// ==========================

void sendAuth() {
  StaticJsonDocument<128> doc;
  doc["type"] = "auth";
  doc["apiKey"] = apiKey;
  String json;
  serializeJson(doc, json);
  client.send(json);
}

void sendHeartbeat() {
  if (!authenticated) return;
  StaticJsonDocument<64> doc;
  doc["type"] = "heartbeat";
  doc["led"] = ledBlink;
  String json;
  serializeJson(doc, json);
  client.send(json);
}

void sendLedAck() {
  StaticJsonDocument<64> ack;
  ack["type"] = "led_ack";
  ack["value"] = ledBlink;
  String json;
  serializeJson(ack, json);
  client.send(json);
}

void handleMessage(WebsocketsMessage message) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, message.data())) {
    Serial.println("Bad JSON from backend");
    return;
  }

  const char* type = doc["type"];
  if (!type) return;

  if (strcmp(type, "auth_ok") == 0) {
    authenticated = true;
    Serial.println("Authenticated with backend");
    sendHeartbeat();
    return;
  }

  if (strcmp(type, "wake_pc") == 0) {
    byte mac[6];
    const char* macStr = doc["mac"];
    if (macStr) {
      parseMac(macStr, mac);
    } else {
      memcpy(mac, pcMac, 6);
    }
    wakePC(mac);

    StaticJsonDocument<64> ack;
    ack["type"] = "wake_ack";
    ack["success"] = true;
    String json;
    serializeJson(ack, json);
    client.send(json);
    return;
  }

  if (strcmp(type, "led") == 0) {
    ledBlink = doc["value"];
    if (!ledBlink) {
      ledState = false;
      digitalWrite(LED_PIN, LOW);
    }
    Serial.print("LED blink set to: ");
    Serial.println(ledBlink);
    sendLedAck();
    return;
  }

  if (strcmp(type, "restart_config") == 0) {
    // Ignored — the restart interval is fixed in firmware (RESTART_INTERVAL_MS).
    return;
  }
}

// Anti-freeze restart — reboots every RESTART_INTERVAL_MS regardless of
// connection state, and gives the backend a heads-up first (best-effort)
// so the resulting brief disconnect isn't logged as a power cut.
void maybeScheduledRestart() {
  if (millis() - restartTimerStart < RESTART_INTERVAL_MS) return;

  Serial.println("Scheduled restart interval reached, restarting...");
  if (authenticated && client.available()) {
    StaticJsonDocument<64> doc;
    doc["type"] = "restarting";
    String json;
    serializeJson(doc, json);
    client.send(json);
    delay(200); // best-effort flush before reboot
  }
  ESP.restart();
}

void connectServer() {
  Serial.println("Connecting to backend...");
  authenticated = false;
  client.setInsecure(); // TLS without certificate pinning
  if (client.connect(wsServer)) {
    Serial.println("Socket open, authenticating...");
    sendAuth();
  } else {
    Serial.println("Backend connection failed");
  }
}

// ==========================
// SETUP / LOOP
// ==========================

void startWatchdog() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  // The core may already have the watchdog running — reconfigure it if so.
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("ESP32 booting");

  pinMode(LED_PIN, OUTPUT);

  WiFiManager wm;
  // Don't sit in the setup portal forever — reboot and retry instead.
  wm.setConfigPortalTimeout(180);
  bool connected = wm.autoConnect("Hasib-ESP32-Setup");
  if (!connected) {
    Serial.println("WiFi failed, restarting");
    ESP.restart();
  }

  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(9);
  restartTimerStart = millis();
  startWatchdog();
  client.onMessage(handleMessage);
  connectServer();
}

void loop() {
  esp_task_wdt_reset();
  client.poll();
  maybeScheduledRestart();

  if (!client.available()) {
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
      lastReconnectAttempt = millis();
      connectServer();
    }
    return;
  }

  if (authenticated && millis() - lastHeartbeat > HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }

  if (ledBlink) {
    if (millis() - lastBlink > 500) {
      lastBlink = millis();

      ledState = !ledState;

      digitalWrite(LED_PIN, ledState);
    }
  }
}

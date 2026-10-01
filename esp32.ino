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

// Root CAs for verifying the server over TLS. On ESP32, ArduinoWebsockets'
// setInsecure() never reaches the TLS client, and core 3.x refuses TLS with
// no CA — so wss:// only works with a CA set here. Cloudflare currently
// serves a Google Trust Services chain (WE1 -> GTS Root R4); the other roots
// cover Cloudflare switching CAs on renewal (GTS RSA chain, Let's Encrypt).
const char* rootCACerts = R"PEM(
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQswCQYDVQQG
EwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIGA1UEAxMLR1RT
IFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAwWjBHMQswCQYDVQQGEwJV
UzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIGA1UEAxMLR1RTIFJv
b3QgUjEwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreV
p0A8of2C+X0yBoJx9vaMf/vo27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4
IyfLpLGcY9vXmX7wCl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/S
CYe7zUjwTcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaHszVsrBhQ
f4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8Y/kB+Xj9e1x3+naH
+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmkMiVOKvFlRNACzqrOSbTqn3yD
sEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97
PtbfkSIS5r762DL8EGMUUXLeXdYWk70paDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmx
lbCluQ0WEdrHbEg8QOB+DVrNVjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACY
PTrIrnqYNxgFlQIDAQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAd
BgNVHQ4EFgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEeQkEzCzc9
zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuyh6f88/qBVRRiClmp
IgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM47HLwEXWdyzRSjeZ2axfG34ar
J45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8JZgfIPxz88NtFMN9iiMG1D53Dn0reWVlH
xYciNuaCp+0KueIHoI17eko8cdLiA6EfMgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/b
al8xa5meLMFrUKTX5hgUvYU/Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAb
d03OIozUhfJFfbdT6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLs
QBqvFAnZ0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bbbP6MvPJw
NQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYDVQQGEwJV
UzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIGA1UEAxMLR1RTIFJv
b3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAwWjBHMQswCQYDVQQGEwJVUzEi
MCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3Qg
UjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvV
YRg1rkDdc/eJkTBa6zzuhXyiQHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHc
i8nHc8iMai/lxKvRHYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0G
A1UdDgQWBBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8p/SgguMh
1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAwTzELMAkG
A1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNoIEdyb3VwMRUw
EwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4WhcNMzUwNjA0MTEwNDM4WjBP
MQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3Jv
dXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoC
ggIBAK3oJHP0FDfzm54rVygch77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj
/RQSa78f0uoxmyF+0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7i
S4+3mX6UA5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyHB5T0Y3Hs
LuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UCB5iPNgiV5+I3lg02
dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUvKBds0pjBqAlkd25HN7rOrFle
aJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWnOlFuhjuefXKnEgV4We0+UXgVCwOPjdAv
BbI+e0ocS3MFEvzG6uBQE3xDk3SzynTnjh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymC
zLq9gwQbooMDQaHWBfEbwrbwqHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC
1CLQJ13hef4Y53CIrU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIB
BjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZLubhzEFnT
IZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ3BebYhtF8GaV0nxv
wuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KKNFtY2PwByVS5uCbMiogziUwt
hDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5ORAzI4JMPJ+GslWYHb4phowim57iaztX
OoJwTdwJx4nLCgdNbOhdjsnvzqvHu7UrTkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIu
vtd7u+Nxe5AW0wdeRlN8NwdCjNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1N
bdWhscdCb+ZAJzVcoyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4k
qKOJ2qxq4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57demyPxgcY
xn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQswCQYDVQQG
EwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNV
BAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00MDA5MTcxNjAwMDBaME8xCzAJ
BgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBTZWN1cml0eSBSZXNlYXJjaCBHcm91cDEV
MBMGA1UEAxMMSVNSRyBSb290IFgyMHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJ
SvMWSj5cz3es3mcFDR0HttwW+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvX
RdgKam7mAHf7AlF9ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYw
DwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdWtL4ndQav
Ei51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1/q4AaOeMSQ+2b1tb
FfLn
-----END CERTIFICATE-----
)PEM";

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
  client.setCACert(rootCACerts);
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

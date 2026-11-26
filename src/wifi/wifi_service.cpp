#include "wifi/wifi_service.h"
#include "config.h"
#include "wdt/wdt_service.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>

static Preferences preferences;
static unsigned long activeThingSpeakChannel = THINGSPEAK_CHANNEL_ID;
static String activeThingSpeakApiKey = THINGSPEAK_WRITE_API_KEY;
static bool shouldSaveIotConfig = false;

static void saveConfigCallback() {
  shouldSaveIotConfig = true;
}

static void loadIotConfig() {
  preferences.begin("iot_config", true);
  unsigned long ch = preferences.getULong("ts_chan", THINGSPEAK_CHANNEL_ID);
  String key = preferences.getString("ts_key", THINGSPEAK_WRITE_API_KEY);
  preferences.end();

  activeThingSpeakChannel = ch;
  activeThingSpeakApiKey = key;
  logLine("[IoT] Konfigurasi ThingSpeak dimuat: Channel=%lu, Key=%s",
          activeThingSpeakChannel,
          activeThingSpeakApiKey.length() > 0 ? "***TERPASANG***" : "(KOSONG)");
}

void saveThingSpeakConfig(unsigned long channel, const String &key) {
  preferences.begin("iot_config", false);
  preferences.putULong("ts_chan", channel);
  preferences.putString("ts_key", key);
  preferences.end();

  activeThingSpeakChannel = channel;
  activeThingSpeakApiKey = key;
  logLine("[IoT] Konfigurasi ThingSpeak disimpan ke NVS: Channel=%lu", activeThingSpeakChannel);
}

void initWiFiService() {
  loadIotConfig();
}

bool startWiFiManagerAutoConnect() {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalTimeout(WIFIMANAGER_PORTAL_TIMEOUT_SEC);
  wm.setConnectTimeout(15);
  wm.setShowStaticFields(false);
  wm.setShowDnsFields(false);

  char channelStr[20];
  snprintf(channelStr, sizeof(channelStr), "%lu", activeThingSpeakChannel);
  char keyStr[36];
  strncpy(keyStr, activeThingSpeakApiKey.c_str(), sizeof(keyStr) - 1);
  keyStr[sizeof(keyStr) - 1] = '\0';

  WiFiManagerParameter customTsChannel("ts_chan", "ThingSpeak Channel ID", channelStr, 16);
  WiFiManagerParameter customTsKey("ts_key", "ThingSpeak Write API Key", keyStr, 32);

  wm.addParameter(&customTsChannel);
  wm.addParameter(&customTsKey);

  logLine("[WiFiManager] Memeriksa koneksi Wi-Fi yang tersimpan...");
  logLine("[WiFiManager] Jika gagal, AP '%s' akan aktif selama %u detik (IP 192.168.4.1)",
          WIFIMANAGER_AP_NAME, static_cast<unsigned int>(WIFIMANAGER_PORTAL_TIMEOUT_SEC));

  bool connected = false;
  if (strlen(DEFAULT_WIFI_SSID) > 0) {
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);
    logLine("[WiFi] Mencoba SSID default dari secret.h (%s)...", DEFAULT_WIFI_SSID);
    uint32_t startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < 8000)) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
    }
  }

  if (!connected) {
    connected = wm.autoConnect(WIFIMANAGER_AP_NAME,
                               strlen(WIFIMANAGER_AP_PASSWORD) > 0 ? WIFIMANAGER_AP_PASSWORD : nullptr);
  }

  if (connected) {
    logLine("[WiFi] BERHASIL TERHUBUNG! IP: %s | RSSI: %d dBm",
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    logLine("[WiFi] Tidak tersambung Wi-Fi / portal timeout. Berjalan dalam mode offline.");
  }

  if (shouldSaveIotConfig) {
    const char *newChanStr = customTsChannel.getValue();
    const char *newKeyStr = customTsKey.getValue();
    unsigned long newChan = strtoul(newChanStr, nullptr, 10);
    saveThingSpeakConfig(newChan, String(newKeyStr));
    shouldSaveIotConfig = false;
  }

  return connected;
}

void handleOnDemandWiFiPortal() {
  logLine("[WiFiManager] Membuka Portal Konfigurasi on-demand pada AP '%s'...", WIFIMANAGER_AP_NAME);
  wdtUnregisterCurrentTask("ThingSpeakTask");

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalTimeout(180);

  char channelStr[20];
  snprintf(channelStr, sizeof(channelStr), "%lu", activeThingSpeakChannel);
  char keyStr[36];
  strncpy(keyStr, activeThingSpeakApiKey.c_str(), sizeof(keyStr) - 1);
  keyStr[sizeof(keyStr) - 1] = '\0';

  WiFiManagerParameter customTsChannel("ts_chan", "ThingSpeak Channel ID", channelStr, 16);
  WiFiManagerParameter customTsKey("ts_key", "ThingSpeak Write API Key", keyStr, 32);

  wm.addParameter(&customTsChannel);
  wm.addParameter(&customTsKey);

  bool res = wm.startConfigPortal(WIFIMANAGER_AP_NAME,
                                  strlen(WIFIMANAGER_AP_PASSWORD) > 0 ? WIFIMANAGER_AP_PASSWORD : nullptr);
  if (res) {
    logLine("[WiFiManager] Portal selesai. Terhubung ke: %s (IP: %s)",
            WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    logLine("[WiFiManager] Portal konfigurasi ditutup / timeout.");
  }

  if (shouldSaveIotConfig) {
    const char *newChanStr = customTsChannel.getValue();
    const char *newKeyStr = customTsKey.getValue();
    unsigned long newChan = strtoul(newChanStr, nullptr, 10);
    saveThingSpeakConfig(newChan, String(newKeyStr));
    shouldSaveIotConfig = false;
  }

  requestWifiPortal = false;
  wdtRegisterCurrentTask("ThingSpeakTask");
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String getWiFiLocalIP() {
  return WiFi.localIP().toString();
}

int getWiFiRSSI() {
  return WiFi.RSSI();
}

unsigned long getActiveThingSpeakChannel() {
  return activeThingSpeakChannel;
}

String getActiveThingSpeakApiKey() {
  return activeThingSpeakApiKey;
}

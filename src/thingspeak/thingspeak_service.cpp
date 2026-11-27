#include "thingspeak/thingspeak_service.h"
#include "config.h"
#include "wdt/wdt_service.h"
#include "wifi/wifi_service.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <ThingSpeak.h>

static WiFiClient tsClient;
static uint32_t lastThingSpeakPostAt = 0;
static int lastThingSpeakHttpCode = 0;

void initThingSpeakService() {
  tsClient.setTimeout(5); // Batas timeout TCP/HTTP client 5 detik untuk mencegah hang Task WDT
  ThingSpeak.begin(tsClient);
}

void thingSpeakTask(void *parameter) {
  (void)parameter;
  wdtRegisterCurrentTask("ThingSpeakTask");
  initThingSpeakService();

  // WiFiManager may wait for the captive-portal timeout. Run it in this
  // worker after the ToF and occupancy tasks have started, and unregister this
  // task from the watchdog while the library is blocking.
  wdtUnregisterCurrentTask("ThingSpeakTask");
  startWiFiManagerAutoConnect();
  wdtRegisterCurrentTask("ThingSpeakTask");

  for (;;) {
    wdtFeed();

    if (requestWifiPortal) {
      handleOnDemandWiFiPortal();
      continue;
    }

    uint32_t now = millis();

    // Pastikan koneksi Wi-Fi aktif
    if (!isWiFiConnected()) {
      static uint32_t lastReconnectAttempt = 0;
      if (now - lastReconnectAttempt >= 10000) {
        lastReconnectAttempt = now;
        logLine("[WiFi] Sambungan terputus, mencoba menyambung ulang...");
        WiFi.reconnect();
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    unsigned long chan = getActiveThingSpeakChannel();
    String apiKey = getActiveThingSpeakApiKey();

    if (chan == 0 || apiKey.length() == 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    CloudSyncData localData;
    bool hasData = false;
    bool eventPending = false;

    if (cloudDataMutex != nullptr && xSemaphoreTake(cloudDataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      localData = cloudData;
      hasData = true;
      eventPending = cloudData.pendingUpdate;
      if (eventPending && (now - lastThingSpeakPostAt >= 15000)) {
        cloudData.pendingUpdate = false;
      }
      xSemaphoreGive(cloudDataMutex);
    }

    bool shouldSend = false;
    if (hasData) {
      const bool intervalPassed = (now - lastThingSpeakPostAt >= THINGSPEAK_UPDATE_INTERVAL_MS);
      const bool eventReady = eventPending && (now - lastThingSpeakPostAt >= 15000);
      if (intervalPassed || eventReady) {
        shouldSend = true;
      }
    }

    if (shouldSend) {
      logLine("[ThingSpeak] Mengirim ke Channel %lu: Masuk=%lu, Keluar=%lu, Di Dalam=%u (%s)...",
              chan,
              static_cast<unsigned long>(localData.totalEntry),
              static_cast<unsigned long>(localData.totalExit),
              static_cast<unsigned int>(localData.occupancyCount),
              localData.isOccupied ? "TERISI" : "KOSONG");

      // ========================================================================
      // Parameter Okupansi ThingSpeak:
      // Field 1: Orang Masuk  (Kumulatif orang masuk A->B)
      // Field 2: Orang Keluar (Kumulatif orang keluar B->A)
      // Field 3: Orang di Dalam (Jumlah orang saat ini di dalam ruangan)
      // Field 4: Status Ruangan (1 = Terisi, 0 = Kosong)
      // Field 5: State Lintasan (0 = Idle, 1 = Masuk, 2 = Keluar, 3 = Tunggu Clear)
      // ========================================================================
      ThingSpeak.setField(1, static_cast<long>(localData.totalEntry));
      ThingSpeak.setField(2, static_cast<long>(localData.totalExit));
      ThingSpeak.setField(3, static_cast<long>(localData.occupancyCount));
      ThingSpeak.setField(4, localData.isOccupied ? 1 : 0);
      ThingSpeak.setField(5, static_cast<long>(localData.crossingState));

      char statusMsg[64];
      snprintf(statusMsg, sizeof(statusMsg), "In=%lu Out=%lu Inside=%u",
               static_cast<unsigned long>(localData.totalEntry),
               static_cast<unsigned long>(localData.totalExit),
               static_cast<unsigned int>(localData.occupancyCount));
      ThingSpeak.setStatus(statusMsg);

      wdtFeed();
      int httpCode = ThingSpeak.writeFields(chan, apiKey.c_str());
      wdtFeed();

      lastThingSpeakPostAt = millis();
      lastThingSpeakHttpCode = httpCode;

      if (httpCode == 200) {
        logLine("[ThingSpeak] Sukses mengirim data (HTTP 200 OK)");
      } else {
        logLine("[ThingSpeak] Respons HTTP: %d (gagal/dibatasi rate-limit 15 detik)", httpCode);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

bool startThingSpeakTask() {
  return xTaskCreatePinnedToCore(thingSpeakTask, "ThingSpeakTask", 8192,
                                 nullptr, 1, nullptr, 0) == pdPASS;
}

int getLastThingSpeakHttpCode() {
  return lastThingSpeakHttpCode;
}

uint32_t getLastThingSpeakPostTime() {
  return lastThingSpeakPostAt;
}

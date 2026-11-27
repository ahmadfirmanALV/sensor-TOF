#include "occupancy/occupancy_service.h"
#include "config.h"
#include "wdt/wdt_service.h"
#include "wifi/wifi_service.h"
#include "thingspeak/thingspeak_service.h"
#include <Preferences.h>

static Preferences preferences;
static uint16_t occupancyCount = 0; // Orang di dalam saat ini
static uint32_t totalEntry = 0;     // Orang masuk (kumulatif)
static uint32_t totalExit = 0;      // Orang keluar (kumulatif)
static CrossingState crossingState = CrossingState::Idle;
static uint32_t crossingStartedAt = 0;
static bool previousAActive = false;
static bool previousBActive = false;

// Manajemen umur Flash SPI (NVS wear-leveling):
// Debounce penulisan NVS agar tidak menulis flash setiap ada orang lewat
static bool countsDirty = false;
static uint32_t lastCountChangeAt = 0;
static uint32_t lastNvsSaveAt = 0;

static void saveCounts() {
  preferences.putUShort("count", occupancyCount);
  preferences.putUInt("entry", totalEntry);
  preferences.putUInt("exit", totalExit);
  countsDirty = false;
  lastNvsSaveAt = millis();
}

static void markCountsDirty(uint32_t now) {
  countsDirty = true;
  lastCountChangeAt = now;
}

static void flushCountsIfQuiet(uint32_t now) {
  // Simpan otomatis ke NVS jika data berubah dan jalur kosong/tenang selama 5 detik,
  // atau batas maksimal 60 detik jika lalu lalang terus menerus.
  if (countsDirty && ((static_cast<uint32_t>(now - lastCountChangeAt) >= 5000) ||
                      (static_cast<uint32_t>(now - lastNvsSaveAt) >= 60000))) {
    saveCounts();
    logLine("[NVS] Data okupansi disimpan otomatis (debounced): Di Dalam=%u | Masuk=%u | Keluar=%u",
            static_cast<unsigned int>(occupancyCount),
            static_cast<unsigned int>(totalEntry),
            static_cast<unsigned int>(totalExit));
  }
}

static void logOccupancy(const char *event) {
  logLine("[%s] Di Dalam=%u | Masuk=%u | Keluar=%u | Status=%s",
          event,
          static_cast<unsigned int>(occupancyCount),
          static_cast<unsigned int>(totalEntry),
          static_cast<unsigned int>(totalExit),
          occupancyCount > 0 ? "TERISI" : "KOSONG");
}

static void syncCloudPending() {
  if (cloudDataMutex != nullptr && xSemaphoreTake(cloudDataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    cloudData.totalEntry = totalEntry;
    cloudData.totalExit = totalExit;
    cloudData.occupancyCount = occupancyCount;
    cloudData.isOccupied = (occupancyCount > 0);
    cloudData.crossingState = static_cast<uint8_t>(crossingState);
    cloudData.pendingUpdate = true;
    xSemaphoreGive(cloudDataMutex);
  }
}

static void completeEntry() {
  if (totalEntry < UINT32_MAX) {
    ++totalEntry;
  }
  if (occupancyCount < MAX_OCCUPANCY_COUNT) {
    ++occupancyCount;
  }
  markCountsDirty(millis());
  logOccupancy("MASUK A->B");
  syncCloudPending();
}

static void completeExit() {
  if (totalExit < UINT32_MAX) {
    ++totalExit;
  }
  if (occupancyCount > 0) {
    --occupancyCount;
    markCountsDirty(millis());
    logOccupancy("KELUAR B->A");
    syncCloudPending();
  } else {
    markCountsDirty(millis());
    logLine("[KELUAR B->A] Diabaikan karena hitungan di dalam sudah 0; Total Keluar=%u",
            static_cast<unsigned int>(totalExit));
    syncCloudPending();
  }
}

static void resetCrossingTracker(bool aActive, bool bActive) {
  crossingState = CrossingState::Idle;
  crossingStartedAt = 0;
  previousAActive = aActive;
  previousBActive = bActive;
}

static void updateCrossingTracker(const SensorSnapshot &snapshot, uint32_t now) {
  if (!snapshot.onlineA || !snapshot.onlineB) {
    resetCrossingTracker(snapshot.activeA, snapshot.activeB);
    return;
  }

  const bool aActive = snapshot.activeA;
  const bool bActive = snapshot.activeB;
  const bool aRose = aActive && !previousAActive;
  const bool bRose = bActive && !previousBActive;
  previousAActive = aActive;
  previousBActive = bActive;

  switch (crossingState) {
    case CrossingState::Idle:
      if (aRose && bRose) {
        crossingState = CrossingState::WaitForClear;
        logLine("[ARAH] sensor A dan B aktif bersamaan; lintasan ambigu, tidak dihitung");
      } else if (aRose) {
        crossingState = CrossingState::WaitForB;
        crossingStartedAt = now;
      } else if (bRose) {
        crossingState = CrossingState::WaitForA;
        crossingStartedAt = now;
      }
      break;

    case CrossingState::WaitForB:
      if (bRose) {
        completeEntry();
        crossingState = CrossingState::WaitForClear;
      } else if (static_cast<uint32_t>(now - crossingStartedAt) > CROSSING_TIMEOUT_MS) {
        crossingState = CrossingState::WaitForClear;
        logLine("[ARAH] urutan A->B kedaluwarsa; tidak dihitung");
      }
      break;

    case CrossingState::WaitForA:
      if (aRose) {
        completeExit();
        crossingState = CrossingState::WaitForClear;
      } else if (static_cast<uint32_t>(now - crossingStartedAt) > CROSSING_TIMEOUT_MS) {
        crossingState = CrossingState::WaitForClear;
        logLine("[ARAH] urutan B->A kedaluwarsa; tidak dihitung");
      }
      break;

    case CrossingState::WaitForClear:
      if (!aActive && !bActive) {
        crossingState = CrossingState::Idle;
      }
      break;
  }
}

static const char *crossingStateName() {
  switch (crossingState) {
    case CrossingState::Idle: return "IDLE";
    case CrossingState::WaitForB: return "TUNGGU_B";
    case CrossingState::WaitForA: return "TUNGGU_A";
    case CrossingState::WaitForClear: return "TUNGGU_CLEAR";
  }
  return "?";
}

static void formatRange(char *output, size_t outputSize, bool online, bool timeout,
                         uint16_t rangeMm) {
  if (!online) {
    snprintf(output, outputSize, "OFFLINE");
  } else if (timeout) {
    snprintf(output, outputSize, "TIMEOUT");
  } else if (rangeMm > MAX_REASONABLE_RANGE_MM) {
    snprintf(output, outputSize, "OUT");
  } else {
    snprintf(output, outputSize, "%u mm", static_cast<unsigned int>(rangeMm));
  }
}

void printSystemStatus(const SensorSnapshot *snapshot) {
  char rangeA[20];
  char rangeB[20];

  if (snapshot != nullptr) {
    formatRange(rangeA, sizeof(rangeA), snapshot->onlineA, snapshot->timeoutA,
                snapshot->filteredA);
    formatRange(rangeB, sizeof(rangeB), snapshot->onlineB, snapshot->timeoutB,
                snapshot->filteredB);
  } else {
    snprintf(rangeA, sizeof(rangeA), "WAIT");
    snprintf(rangeB, sizeof(rangeB), "WAIT");
  }

  const char *wifiStatus = isWiFiConnected() ? "CONNECTED" : "DISCONNECTED";
  String ipStr = isWiFiConnected() ? getWiFiLocalIP() : "0.0.0.0";
  unsigned long tsChan = getActiveThingSpeakChannel();
  int tsCode = getLastThingSpeakHttpCode();

  logLine("[STATUS] Di Dalam=%u (Masuk=%u, Keluar=%u, %s) | A=%s | B=%s | State=%s | WiFi=%s (%s) | TS=%lu (HTTP %d) | WDT=%us",
          static_cast<unsigned int>(occupancyCount),
          static_cast<unsigned int>(totalEntry),
          static_cast<unsigned int>(totalExit),
          occupancyCount > 0 ? "TERISI" : "KOSONG",
          rangeA, rangeB,
          crossingStateName(),
          wifiStatus, ipStr.c_str(),
          tsChan, tsCode,
          static_cast<unsigned int>(WDT_TIMEOUT_SECONDS));
}

static void processCommand(Command command, const SensorSnapshot *latestSnapshot) {
  if (command == Command::ResetCount) {
    occupancyCount = 0;
    saveCounts();
    resetCrossingTracker(latestSnapshot != nullptr && latestSnapshot->activeA,
                         latestSnapshot != nullptr && latestSnapshot->activeB);
    logOccupancy("RESET OKUPANSI");
    syncCloudPending();
  } else if (command == Command::ShowStatus) {
    printSystemStatus(latestSnapshot);
  } else if (command == Command::StartWifiPortal) {
    requestWifiPortal = true;
    logLine("[SISTEM] Permintaan aktivasi Captive Portal WiFiManager diterima");
  } else if (command == Command::SyncThingSpeak) {
    syncCloudPending();
    logLine("[SISTEM] Permintaan sinkronisasi manual ThingSpeak dijadwalkan");
  }
}

void initOccupancyService() {
  if (!preferences.begin("roomocc", false)) {
    logLine("[NVS] Gagal membuka namespace 'roomocc'");
  }
  const uint16_t savedCount = preferences.getUShort("count", 0);
  occupancyCount = (savedCount <= MAX_OCCUPANCY_COUNT) ? savedCount : 0;
  totalEntry = preferences.getUInt("entry", 0);
  totalExit = preferences.getUInt("exit", 0);

  logLine("[NVS] Data okupansi dipulihkan: Di Dalam=%u | Masuk=%u | Keluar=%u",
          static_cast<unsigned int>(occupancyCount),
          static_cast<unsigned int>(totalEntry),
          static_cast<unsigned int>(totalExit));

  if (cloudDataMutex != nullptr && xSemaphoreTake(cloudDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    cloudData.totalEntry = totalEntry;
    cloudData.totalExit = totalExit;
    cloudData.occupancyCount = occupancyCount;
    cloudData.isOccupied = (occupancyCount > 0);
    cloudData.pendingUpdate = true;
    xSemaphoreGive(cloudDataMutex);
  }

  logLine("[SISTEM] Logika arah: A->B bertambah (masuk), B->A berkurang (keluar)");
}

void occupancyTask(void *parameter) {
  (void)parameter;
  wdtRegisterCurrentTask("OccupancyTask");

  SensorSnapshot latestSnapshot{};
  bool haveSnapshot = false;
  uint32_t lastDebugAt = 0;

  for (;;) {
    wdtFeed();

    Command command;
    while (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
      processCommand(command, haveSnapshot ? &latestSnapshot : nullptr);
      wdtFeed();
    }

    SensorSnapshot newSnapshot;
    if (xQueueReceive(sensorQueue, &newSnapshot, pdMS_TO_TICKS(20)) == pdTRUE) {
      latestSnapshot = newSnapshot;
      haveSnapshot = true;
      updateCrossingTracker(latestSnapshot, millis());

      if (cloudDataMutex != nullptr && xSemaphoreTake(cloudDataMutex, 0) == pdTRUE) {
        cloudData.totalEntry = totalEntry;
        cloudData.totalExit = totalExit;
        cloudData.occupancyCount = occupancyCount;
        cloudData.isOccupied = (occupancyCount > 0);
        cloudData.rangeA = latestSnapshot.filteredA;
        cloudData.rangeB = latestSnapshot.filteredB;
        cloudData.crossingState = static_cast<uint8_t>(crossingState);
        xSemaphoreGive(cloudDataMutex);
      }
    }

    wdtFeed();

    const uint32_t now = millis();
    flushCountsIfQuiet(now);

    if (static_cast<uint32_t>(now - lastDebugAt) >= DEBUG_PRINT_INTERVAL_MS) {
      lastDebugAt = now;
      printSystemStatus(haveSnapshot ? &latestSnapshot : nullptr);
    }
  }
}

bool startOccupancyTask() {
  return xTaskCreatePinnedToCore(occupancyTask, "OccupancyTask", 6144,
                                 nullptr, 2, nullptr, 1) == pdPASS;
}

uint16_t getOccupancyCount() {
  return occupancyCount;
}

uint32_t getTotalEntry() {
  return totalEntry;
}

uint32_t getTotalExit() {
  return totalExit;
}

void resetOccupancyCount() {
  occupancyCount = 0;
  saveCounts();
  syncCloudPending();
}

void resetAllCounts() {
  occupancyCount = 0;
  totalEntry = 0;
  totalExit = 0;
  saveCounts();
  syncCloudPending();
  logLine("[NVS] Seluruh hitungan direset: Di Dalam=0, Masuk=0, Keluar=0");
}

#include <Arduino.h>
#include "config.h"
#include "wdt/wdt_service.h"
#include "sensor/tof_sensor.h"
#include "occupancy/occupancy_service.h"
#include "wifi/wifi_service.h"
#include "thingspeak/thingspeak_service.h"
#include "serial/serial_service.h"

#include <stdarg.h>
#include <stdio.h>

// ==============================================================================
// Alokasi Objek Global Sinkronisasi FreeRTOS
// ==============================================================================
QueueHandle_t sensorQueue = nullptr;
QueueHandle_t commandQueue = nullptr;
SemaphoreHandle_t serialMutex = nullptr;
SemaphoreHandle_t cloudDataMutex = nullptr;
CloudSyncData cloudData{};
volatile bool requestWifiPortal = false;

// ==============================================================================
// Implementasi Logger Thread-Safe
// ==============================================================================
void logLine(const char *format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  if (serialMutex != nullptr) {
    xSemaphoreTake(serialMutex, portMAX_DELAY);
  }
  Serial.println(message);
  if (serialMutex != nullptr) {
    xSemaphoreGive(serialMutex);
  }
}

// ==============================================================================
// Master Coordinator Task (1 Task yang memanggil & mengawasi semua Worker Task)
// ==============================================================================
static void systemCoordinatorTask(void *parameter) {
  (void)parameter;
  wdtRegisterCurrentTask("SystemCoordinator");

  logLine("[COORDINATOR] Memulai inisialisasi seluruh worker task...");

  // 1. Jalankan ToFReadTask (Core 1, Prio 3)
  if (!startToFTask()) {
    logLine("[FATAL] Gagal memulai ToFReadTask");
  } else {
    logLine("[COORDINATOR] ToFReadTask berhasil berjalan di Core 1 (Prio 3)");
  }

  // 2. Jalankan OccupancyTask (Core 1, Prio 2)
  if (!startOccupancyTask()) {
    logLine("[FATAL] Gagal memulai OccupancyTask");
  } else {
    logLine("[COORDINATOR] OccupancyTask berhasil berjalan di Core 1 (Prio 2)");
  }

  // 3. Jalankan SerialTask (Core 1, Prio 1)
  if (!startSerialTask()) {
    logLine("[FATAL] Gagal memulai SerialTask");
  } else {
    logLine("[COORDINATOR] SerialTask berhasil berjalan di Core 1 (Prio 1)");
  }

  // 4. Jalankan ThingSpeakTask (Core 0, Prio 1 - Network Core)
  if (!startThingSpeakTask()) {
    logLine("[FATAL] Gagal memulai ThingSpeakTask");
  } else {
    logLine("[COORDINATOR] ThingSpeakTask berhasil berjalan di Core 0 (Prio 1)");
  }

  logLine("[COORDINATOR] Seluruh task modular telah aktif & diawasi Watchdog Timer.");

  // Loop pengawasan rutin supervisor / coordinator
  for (;;) {
    wdtFeed();

    // Supervisor memantau ketersediaan free heap memori setiap 10 detik
    static uint32_t lastHealthCheck = 0;
    uint32_t now = millis();
    if (now - lastHealthCheck >= 10000) {
      lastHealthCheck = now;
      logLine("[HEALTH] Free Heap: %u bytes | Min Heap: %u bytes",
              static_cast<unsigned int>(esp_get_free_heap_size()),
              static_cast<unsigned int>(esp_get_minimum_free_heap_size()));
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ==============================================================================
// Setup & Loop Arduino
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  // Inisialisasi Mutex & Antrean FreeRTOS
  serialMutex = xSemaphoreCreateMutex();
  cloudDataMutex = xSemaphoreCreateMutex();
  sensorQueue = xQueueCreate(8, sizeof(SensorSnapshot));
  commandQueue = xQueueCreate(4, sizeof(Command));

  logLine("\n========================================================");
  logLine(" Sistem Okupansi Ruang - Arsitektur Modular FreeRTOS");
  logLine(" Modul: ToF, Occupancy, WiFiManager, ThingSpeak, WDT, Serial");
  logLine("========================================================");

  // Inisialisasi Hardware Sensor I2C & XSHUT
  initToFSensors();

  // Inisialisasi Modul Okupansi (NVS restore)
  initOccupancyService();

  // Inisialisasi Modul Wi-Fi & Parameter IoT (NVS)
  initWiFiService();

  // Inisialisasi Watchdog Timer untuk seluruh alur
  initWatchdog(WDT_TIMEOUT_SECONDS);
  wdtRegisterCurrentTask("setup/loop");

  if (sensorQueue == nullptr || commandQueue == nullptr || cloudDataMutex == nullptr) {
    logLine("[FATAL] Gagal membuat antrean atau mutex FreeRTOS");
    return;
  }

  // Buat 1 Master Task (System Coordinator) yang memanggil task-task lainnya
  if (xTaskCreatePinnedToCore(systemCoordinatorTask, "SystemCoordinator", 4096,
                              nullptr, 2, nullptr, 1) != pdPASS) {
    logLine("[FATAL] Gagal membuat SystemCoordinatorTask");
    return;
  }
}

void loop() {
  wdtFeed();
  vTaskDelay(pdMS_TO_TICKS(1000));
}

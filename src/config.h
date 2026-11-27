#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if defined(__has_include)
#if __has_include("secret.h")
#include "secret.h"
#endif
#endif

// ==============================================================================
// 1. Nilai Default Kredensial & Konfigurasi (Dapat dioverride di include/secret.h)
// ==============================================================================
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif

#ifndef DEFAULT_WIFI_PASSWORD
#define DEFAULT_WIFI_PASSWORD ""
#endif

#ifndef WIFIMANAGER_AP_NAME
#define WIFIMANAGER_AP_NAME "ESP32-RoomCounter-AP"
#endif

#ifndef WIFIMANAGER_AP_PASSWORD
#define WIFIMANAGER_AP_PASSWORD "12345678"
#endif

#ifndef WIFIMANAGER_PORTAL_TIMEOUT_SEC
#define WIFIMANAGER_PORTAL_TIMEOUT_SEC 120
#endif

#ifndef THINGSPEAK_CHANNEL_ID
#define THINGSPEAK_CHANNEL_ID 0UL
#endif

#ifndef THINGSPEAK_WRITE_API_KEY
#define THINGSPEAK_WRITE_API_KEY ""
#endif

#ifndef THINGSPEAK_UPDATE_INTERVAL_MS
#define THINGSPEAK_UPDATE_INTERVAL_MS 20000
#endif

// ==============================================================================
// 2. Hardware Pin DOIT ESP32 DEVKIT V1 & VL53L0X
// ==============================================================================
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_XSHUT_A = 32;
constexpr uint8_t PIN_XSHUT_B = 33;

constexpr uint8_t I2C_ADDRESS_DEFAULT = 0x29;
constexpr uint8_t I2C_ADDRESS_A = 0x30;
constexpr uint8_t I2C_ADDRESS_B = 0x31;

// ==============================================================================
// 3. Ambang Batas Sensor & Konstanta Sistem
// ==============================================================================
constexpr uint16_t SENSOR_A_TRIGGER_MM = 900;
constexpr uint16_t SENSOR_A_RELEASE_MM = 1050;
constexpr uint16_t SENSOR_B_TRIGGER_MM = 900;
constexpr uint16_t SENSOR_B_RELEASE_MM = 1050;

constexpr uint16_t SENSOR_TIMEOUT_MS = 100;
constexpr uint32_t MEASUREMENT_BUDGET_US = 33000;
constexpr uint16_t MAX_REASONABLE_RANGE_MM = 4000;
constexpr uint8_t ENTER_CONFIRM_SAMPLES = 2;
constexpr uint8_t CLEAR_CONFIRM_SAMPLES = 3;
constexpr uint8_t TIMEOUTS_BEFORE_REINIT = 3;
constexpr uint32_t SENSOR_RETRY_MS = 2500;
constexpr uint32_t CROSSING_TIMEOUT_MS = 5000;
constexpr uint32_t DEBUG_PRINT_INTERVAL_MS = 1000;
constexpr uint16_t MAX_OCCUPANCY_COUNT = 9999;

// Watchdog Timer Timeout (detik)
constexpr uint32_t WDT_TIMEOUT_SECONDS = 20;

// ==============================================================================
// 4. Struktur Data & Enumerasi Bersama
// ==============================================================================
enum class Command : uint8_t {
  ResetCount,
  ShowStatus,
  StartWifiPortal,
  SyncThingSpeak,
};

enum class CrossingState : uint8_t {
  Idle = 0,
  WaitForB = 1,  // Sensor A aktif duluan: orang masuk
  WaitForA = 2,  // Sensor B aktif duluan: orang keluar
  WaitForClear = 3,
};

struct SensorSnapshot {
  uint16_t filteredA;
  uint16_t filteredB;
  bool onlineA;
  bool onlineB;
  bool timeoutA;
  bool timeoutB;
  bool activeA;
  bool activeB;
};

struct CloudSyncData {
  uint32_t totalEntry = 0;      // Orang Masuk (kumulatif)
  uint32_t totalExit = 0;       // Orang Keluar (kumulatif)
  uint16_t occupancyCount = 0;  // Orang di Dalam (saat ini)
  bool isOccupied = false;      // Status ruangan (terisi/kosong)
  uint16_t rangeA = 0;
  uint16_t rangeB = 0;
  uint8_t crossingState = 0;
  bool pendingUpdate = false;
};

// ==============================================================================
// 5. Handle Global Sinkronisasi FreeRTOS
// ==============================================================================
extern QueueHandle_t sensorQueue;
extern QueueHandle_t commandQueue;
extern SemaphoreHandle_t serialMutex;
extern SemaphoreHandle_t cloudDataMutex;
extern CloudSyncData cloudData;
extern volatile bool requestWifiPortal;

// Fungsi logging thread-safe bersama
void logLine(const char *format, ...);

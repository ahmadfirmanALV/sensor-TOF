#pragma once

#include <Arduino.h>
#include "config.h"

// Inisialisasi penyimpanan NVS dan nilai hitungan okupansi tersimpan
void initOccupancyService();

// Jalankan FreeRTOS Task pengolah logika okupansi pada Core 1
bool startOccupancyTask();

// Fungsi implementasi task FreeRTOS
void occupancyTask(void *parameter);

// Getter & Setter hitungan okupansi
uint16_t getOccupancyCount();     // Orang di dalam saat ini
uint32_t getTotalEntry();        // Total orang masuk kumulatif
uint32_t getTotalExit();         // Total orang keluar kumulatif
void resetOccupancyCount();      // Reset orang di dalam menjadi 0
void resetAllCounts();           // Reset total masuk, keluar, dan di dalam menjadi 0

// Cetak ringkasan status sistem (Sensor, Wi-Fi, ThingSpeak, WDT)
void printSystemStatus(const SensorSnapshot *snapshot);

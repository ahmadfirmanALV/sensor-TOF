#pragma once

#include <Arduino.h>

// Inisialisasi Hardware Task Watchdog Timer
void initWatchdog(uint32_t timeoutSeconds);

// Daftarkan task pemanggil saat ini ke dalam pengawasan WDT
void wdtRegisterCurrentTask(const char *taskName);

// Lepas task pemanggil sementara dari pengawasan WDT (contoh: sebelum portal web blocking)
void wdtUnregisterCurrentTask(const char *taskName);

// Beri makan (reset) timer WDT untuk task saat ini
void wdtFeed();

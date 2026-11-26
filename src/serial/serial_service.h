#pragma once

#include <Arduino.h>

// Inisialisasi komunikasi Serial
void initSerialService();

// Jalankan FreeRTOS Task pembaca perintah Serial pada Core 1
bool startSerialTask();

// Fungsi implementasi task FreeRTOS
void serialTask(void *parameter);

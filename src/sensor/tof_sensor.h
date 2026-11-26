#pragma once

#include <Arduino.h>

// Inisialisasi pin hardware XSHUT dan I2C untuk kedua sensor VL53L0X
void initToFSensors();

// Jalankan FreeRTOS Task pembacaan sensor pada Core 1
bool startToFTask();

// Fungsi implementasi task FreeRTOS
void tofReadTask(void *parameter);

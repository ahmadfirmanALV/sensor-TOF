#pragma once

#include <Arduino.h>

// Inisialisasi klien ThingSpeak
void initThingSpeakService();

// Jalankan FreeRTOS Task pengirim data ThingSpeak pada Core 0 (Network Core)
bool startThingSpeakTask();

// Fungsi implementasi task FreeRTOS
void thingSpeakTask(void *parameter);

// Getter status ThingSpeak
int getLastThingSpeakHttpCode();
uint32_t getLastThingSpeakPostTime();

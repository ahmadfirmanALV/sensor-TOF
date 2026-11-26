#pragma once

#include <Arduino.h>

// Inisialisasi konfigurasi Wi-Fi dan muat parameter IoT dari NVS
void initWiFiService();

// Jalankan autoConnect WiFiManager (Web Portal jika belum terhubung)
bool startWiFiManagerAutoConnect();

// Tangani pembukaan portal konfigurasi on-demand (dari perintah Serial 'W')
void handleOnDemandWiFiPortal();

// Status dan utilitas Wi-Fi
bool isWiFiConnected();
String getWiFiLocalIP();
int getWiFiRSSI();

// Konfigurasi ThingSpeak yang dikelola melalui Web Portal & NVS
unsigned long getActiveThingSpeakChannel();
String getActiveThingSpeakApiKey();
void saveThingSpeakConfig(unsigned long channel, const String &key);

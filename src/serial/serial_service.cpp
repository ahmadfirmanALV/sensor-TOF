#include "serial/serial_service.h"
#include "config.h"
#include "wdt/wdt_service.h"
#include <ctype.h>
#include <string.h>

static bool enqueueCommand(Command command) {
  return commandQueue != nullptr && xQueueSend(commandQueue, &command, 0) == pdTRUE;
}

static void processSerialInput() {
  static char input[24];
  static uint8_t length = 0;

  while (Serial.available() > 0) {
    const char received = static_cast<char>(Serial.read());
    if (received == '\r') {
      continue;
    }
    if (received != '\n') {
      if (length < sizeof(input) - 1) {
        input[length++] = static_cast<char>(tolower(static_cast<unsigned char>(received)));
      }
      continue;
    }

    input[length] = '\0';
    if (strcmp(input, "r") == 0 || strcmp(input, "reset") == 0) {
      if (!enqueueCommand(Command::ResetCount)) {
        logLine("[SERIAL] Antrean perintah penuh; coba lagi");
      }
    } else if (strcmp(input, "s") == 0 || strcmp(input, "status") == 0) {
      if (!enqueueCommand(Command::ShowStatus)) {
        logLine("[SERIAL] Antrean perintah penuh; coba lagi");
      }
    } else if (strcmp(input, "w") == 0 || strcmp(input, "wifi") == 0) {
      if (!enqueueCommand(Command::StartWifiPortal)) {
        logLine("[SERIAL] Antrean perintah penuh; coba lagi");
      }
    } else if (strcmp(input, "c") == 0 || strcmp(input, "cloud") == 0) {
      if (!enqueueCommand(Command::SyncThingSpeak)) {
        logLine("[SERIAL] Antrean perintah penuh; coba lagi");
      }
    } else if (strcmp(input, "h") == 0 || strcmp(input, "help") == 0) {
      logLine("--- Daftar Perintah Serial ---");
      logLine("  R / RESET : Reset hitungan okupansi menjadi 0");
      logLine("  S / STATUS: Tampilkan status sensor, Wi-Fi, ThingSpeak, & WDT");
      logLine("  W / WIFI  : Buka Web Captive Portal WiFiManager (AP mode)");
      logLine("  C / CLOUD : Sinkronisasi manual data ke ThingSpeak");
      logLine("  H / HELP  : Tampilkan panduan perintah serial ini");
    } else if (length > 0) {
      logLine("Perintah tidak dikenal. Ketik H untuk bantuan.");
    }
    length = 0;
  }
}

void initSerialService() {
  // Serial.begin sudah dipanggil di awal setup
}

void serialTask(void *parameter) {
  (void)parameter;
  wdtRegisterCurrentTask("SerialTask");

  for (;;) {
    wdtFeed();
    processSerialInput();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool startSerialTask() {
  return xTaskCreatePinnedToCore(serialTask, "SerialTask", 4096,
                                 nullptr, 1, nullptr, 1) == pdPASS;
}

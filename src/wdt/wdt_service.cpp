#include "wdt/wdt_service.h"
#include "config.h"
#include <esp_task_wdt.h>
#include <esp_idf_version.h>

void initWatchdog(uint32_t timeoutSeconds) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = timeoutSeconds * 1000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  esp_err_t err = esp_task_wdt_init(&twdt_config);
  if (err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_reconfigure(&twdt_config);
  }
#else
  esp_err_t err = esp_task_wdt_init(timeoutSeconds, true);
#endif

  if (err == ESP_OK) {
    logLine("[WDT] Watchdog Timer aktif (%u detik, Auto-Reboot=ON)",
            static_cast<unsigned int>(timeoutSeconds));
  } else {
    logLine("[WDT] Status konfigurasi Watchdog Timer: %d", err);
  }
}

void wdtRegisterCurrentTask(const char *taskName) {
  esp_err_t err = esp_task_wdt_add(NULL);
  if (err == ESP_OK) {
    logLine("[WDT] Task '%s' terdaftar dalam pengawasan WDT", taskName);
  } else {
    logLine("[WDT] Peringatan pendaftaran task '%s': code %d", taskName, err);
  }
}

void wdtUnregisterCurrentTask(const char *taskName) {
  esp_err_t err = esp_task_wdt_delete(NULL);
  if (err == ESP_OK) {
    logLine("[WDT] Task '%s' sementara dilepas dari pengawasan WDT", taskName);
  }
}

void wdtFeed() {
  esp_task_wdt_reset();
}

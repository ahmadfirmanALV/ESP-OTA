#include "app_watchdog.h"

#include <esp_task_wdt.h>

#include "app_config.h"
#include "app_log.h"

namespace {

bool watchdogReady = false;

}  // namespace

bool appWatchdogInit() {
  esp_task_wdt_config_t config = {};
  config.timeout_ms = AppConfig::TASK_WATCHDOG_TIMEOUT_MS;
  config.idle_core_mask = AppConfig::TASK_WATCHDOG_IDLE_CORE_MASK;
  config.trigger_panic = true;

  // ESP-IDF may initialize the TWDT during startup. Reconfigure that one
  // instance; only initialize it if the framework has not done so.
  esp_err_t result = esp_task_wdt_reconfigure(&config);
  if (result == ESP_ERR_INVALID_STATE) {
    result = esp_task_wdt_init(&config);
  }
  if (result != ESP_OK) {
    appLogf("Watchdog: konfigurasi gagal (ESP error %d).", result);
    return false;
  }

  watchdogReady = true;
  appLogf("Watchdog aktif: timeout %lu ms, idle CPU0/CPU1 dipantau.",
          static_cast<unsigned long>(AppConfig::TASK_WATCHDOG_TIMEOUT_MS));
  return true;
}

bool appWatchdogSubscribeCurrentTask(const char* taskName) {
  if (!watchdogReady) {
    appLogf("Watchdog: task %s tidak didaftarkan; watchdog belum aktif.", taskName);
    return false;
  }

  const esp_err_t status = esp_task_wdt_status(nullptr);
  if (status == ESP_OK) {
    return true;
  }
  if (status != ESP_ERR_NOT_FOUND) {
    appLogf("Watchdog: status task %s gagal (ESP error %d).", taskName, status);
    return false;
  }

  const esp_err_t result = esp_task_wdt_add(nullptr);
  if (result != ESP_OK) {
    appLogf("Watchdog: task %s gagal didaftarkan (ESP error %d).", taskName, result);
    return false;
  }

  appLogf("Watchdog: task %s dipantau.", taskName);
  return true;
}

void appWatchdogFeed() {
  if (watchdogReady) {
    esp_task_wdt_reset();
  }
}

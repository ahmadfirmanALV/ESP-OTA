#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_events.h"
#include "app_log.h"
#include "app_watchdog.h"
#include "tasks/led/led_task.h"
#include "tasks/ota/ota_task.h"
#include "tasks/wifi/wifi_task.h"

void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(500));

  if (!appLogInit()) {
    Serial.println("Gagal membuat mutex logger.");
    return;
  }

  if (!appEventsInit()) {
    appLog("Gagal membuat event group FreeRTOS.");
    return;
  }

  appLog("=== ESP32 PlatformIO + FreeRTOS + WiFiManager + OTA ===");

  if (!appWatchdogInit() || !appWatchdogSubscribeCurrentTask("main_loop")) {
    appLog("Watchdog gagal disiapkan; ESP32 akan restart.");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
    return;
  }

  // LED langsung aktif saat boot; task jaringan tidak menahan task ini.
  if (!ledTaskStart()) {
    appLog("Gagal memulai task LED.");
  }

  if (!wifiTaskStart()) {
    appLog("Gagal memulai task WiFi.");
  }

  // Task OTA menunggu WIFI_CONNECTED_BIT sebelum memulai ArduinoOTA.
  if (!otaTaskStart()) {
    appLog("Gagal memulai task OTA.");
  }
}

void loop() {
  // Seluruh pekerjaan aplikasi berjalan di task masing-masing.
  appWatchdogFeed();
  vTaskDelay(pdMS_TO_TICKS(1000));
}

#include "led_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_config.h"
#include "app_log.h"
#include "app_watchdog.h"

namespace {

void ledTask(void* parameter) {
  (void)parameter;

  if (!appWatchdogSubscribeCurrentTask("led_task")) {
    appLog("Watchdog: task LED gagal didaftarkan; ESP32 akan restart.");
    ESP.restart();
    vTaskDelete(nullptr);
    return;
  }

  pinMode(AppConfig::LED_PIN, OUTPUT);
  digitalWrite(AppConfig::LED_PIN, HIGH);

  if (!AppConfig::BLINK_MODE) {
    for (;;) {
      appWatchdogFeed();
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  bool ledState = false;
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(AppConfig::BLINK_INTERVAL_MS);

  for (;;) {
    appWatchdogFeed();
    vTaskDelayUntil(&lastWakeTime, interval);
    ledState = !ledState;
    digitalWrite(AppConfig::LED_PIN, ledState ? HIGH : LOW);
  }
}

}  // namespace

bool ledTaskStart() {
  return xTaskCreatePinnedToCore(
             ledTask,
             "led_task",
             AppConfig::LED_TASK_STACK_BYTES,
             nullptr,
             1,
             nullptr,
             1) == pdPASS;
}

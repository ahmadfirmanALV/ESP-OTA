#include "wifi_task.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_config.h"
#include "app_events.h"
#include "app_identity.h"
#include "app_log.h"
#include "app_watchdog.h"

namespace {

bool connectWithWifiManager() {
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(AppConfig::WIFI_PORTAL_TIMEOUT_SECONDS);

  if (AppConfig::USE_STATIC_IP) {
    const IPAddress staticIp(
        AppConfig::WIFI_STATIC_IP[0], AppConfig::WIFI_STATIC_IP[1],
        AppConfig::WIFI_STATIC_IP[2], AppConfig::WIFI_STATIC_IP[3]);
    const IPAddress gateway(
        AppConfig::WIFI_GATEWAY[0], AppConfig::WIFI_GATEWAY[1],
        AppConfig::WIFI_GATEWAY[2], AppConfig::WIFI_GATEWAY[3]);
    const IPAddress subnet(
        AppConfig::WIFI_SUBNET[0], AppConfig::WIFI_SUBNET[1],
        AppConfig::WIFI_SUBNET[2], AppConfig::WIFI_SUBNET[3]);
    const IPAddress dns(
        AppConfig::WIFI_DNS[0], AppConfig::WIFI_DNS[1],
        AppConfig::WIFI_DNS[2], AppConfig::WIFI_DNS[3]);
    wifiManager.setSTAStaticIPConfig(staticIp, gateway, subnet, dns);
  } else {
    // Ambil konfigurasi IP dinamis dari router jika DHCP dipilih.
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  }

  wifiManager.setAPCallback([](WiFiManager* manager) {
    (void)manager;
    appLogf("Mode konfigurasi aktif. Sambungkan ke WiFi: %s", AppConfig::WIFI_AP_NAME);
    appLog("Buka http://192.168.4.1 untuk memasukkan kredensial WiFi.");
  });

  if (AppConfig::RESET_WIFI_SETTINGS_ON_BOOT) {
    wifiManager.resetSettings();
    appLog("Kredensial WiFi tersimpan sudah dihapus.");
  }

  appLog("Memulai koneksi WiFi; portal konfigurasi tersedia jika diperlukan.");
  if (!wifiManager.autoConnect(AppConfig::WIFI_AP_NAME, AppConfig::WIFI_AP_PASSWORD)) {
    appLog("Koneksi WiFi gagal atau portal timeout; ESP32 akan restart.");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
    return false;
  }

  WiFi.setAutoReconnect(true);
  setWifiConnected(WiFi.status() == WL_CONNECTED);

  const String ssid = WiFi.SSID();
  const String ipAddress = WiFi.localIP().toString();
  const String gatewayAddress = WiFi.gatewayIP().toString();
  const String subnetMask = WiFi.subnetMask().toString();
  appLogf("Terhubung ke WiFi: %s", ssid.c_str());
  appLogf("IP address: %s", ipAddress.c_str());
  appLogf("Gateway: %s", gatewayAddress.c_str());
  appLogf("Subnet mask: %s", subnetMask.c_str());

  return true;
}

void wifiTask(void* parameter) {
  (void)parameter;

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    (void)info;

    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      setWifiConnected(true);
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      setWifiConnected(false);
    }
  });

  WiFi.mode(WIFI_STA);
  const String hostname = AppIdentity::deviceId();
  WiFi.setHostname(hostname.c_str());
  appLogf("Identitas perangkat: %s", hostname.c_str());

  // WiFiManager may wait for the user portal for up to 180 s, so enroll this
  // task only after the initial connection/portal flow has completed.
  if (!connectWithWifiManager()) {
    vTaskDelete(nullptr);
    return;
  }
  if (!appWatchdogSubscribeCurrentTask("wifi_task")) {
    appLog("Watchdog: task WiFi gagal didaftarkan; ESP32 akan restart.");
    ESP.restart();
    vTaskDelete(nullptr);
    return;
  }

  // Task tetap hidup agar koneksi Wi-Fi dipulihkan jika router sempat terputus.
  for (;;) {
    appWatchdogFeed();
    if (WiFi.status() != WL_CONNECTED) {
      setWifiConnected(false);
      appLog("WiFi terputus; mencoba menyambung kembali.");
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(5000));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

}  // namespace

bool wifiTaskStart() {
  return xTaskCreatePinnedToCore(
             wifiTask,
             "wifi_task",
             AppConfig::WIFI_TASK_STACK_BYTES,
             nullptr,
             2,
             nullptr,
             0) == pdPASS;
}

#pragma once

#include <Arduino.h>

#if __has_include("ota_secrets.h")
#include "ota_secrets.h"
#endif

#ifndef OTA_MQTT_USERNAME
#define OTA_MQTT_USERNAME ""
#endif

#ifndef OTA_MQTT_PASSWORD
#define OTA_MQTT_PASSWORD ""
#endif

#ifndef OTA_MQTT_CA_CERT_PEM
#define OTA_MQTT_CA_CERT_PEM ""
#endif

namespace AppConfig {

constexpr uint8_t LED_PIN = 2; // LED onboard D2 / GPIO2 pada DOIT ESP32 DEVKIT V1

// WiFiManager membuat access point ini saat ESP32 belum memiliki WiFi tersimpan.
constexpr char WIFI_AP_NAME[] = "ESP32-Setup";
constexpr char WIFI_AP_PASSWORD[] = "12345678";
constexpr uint16_t WIFI_PORTAL_TIMEOUT_SECONDS = 180;

// Gunakan DHCP agar ESP32 mengikuti jaringan router yang sedang dipakai.
constexpr bool USE_STATIC_IP = false;

// Konfigurasi IP statis (hanya aktif jika USE_STATIC_IP = true).
constexpr uint8_t WIFI_STATIC_IP[] = {192, 168, 2, 50};
constexpr uint8_t WIFI_GATEWAY[] = {192, 168, 2, 2};
constexpr uint8_t WIFI_SUBNET[] = {255, 255, 255, 0};
constexpr uint8_t WIFI_DNS[] = {192, 168, 2, 2};

// Identitas unik diturunkan dari MAC Wi-Fi agar firmware yang sama aman dipakai
// pada beberapa ESP32.
constexpr char OTA_DEVICE_ID_PREFIX[] = "esp32-ota-";

// Komputer lokal yang menyajikan file firmware lewat HTTP; terpisah dari broker.
constexpr uint8_t OTA_SERVER_IP[] = {192, 168, 211, 251};
// Broker dapat berupa Mosquitto lokal atau hostname layanan cloud.
constexpr char MQTT_BROKER_HOST[] = "192.168.211.251";
constexpr uint16_t MQTT_PORT = 8883;
constexpr uint16_t OTA_HTTP_PORT = 5000;
constexpr char MQTT_USERNAME[] = OTA_MQTT_USERNAME;
constexpr char MQTT_PASSWORD[] = OTA_MQTT_PASSWORD;
constexpr char MQTT_CA_CERT[] = OTA_MQTT_CA_CERT_PEM;
constexpr char OTA_UPDATE_TOPIC[] = "gateway/ota/update";
constexpr char OTA_STATUS_TOPIC_PREFIX[] = "gateway/status";
constexpr char MQTT_DUMMY_TOPIC_PREFIX[] = "gateway/dummy";
constexpr uint32_t MQTT_DUMMY_INTERVAL_MS = 5000;
constexpr uint32_t OTA_BOOT_VALIDATION_TIMEOUT_MS = 180000;

constexpr bool BLINK_MODE = false;
constexpr uint32_t BLINK_INTERVAL_MS = 500;

// Ubah ke true hanya untuk satu kali upload jika ingin menghapus kredensial WiFi
// yang tersimpan. Kembalikan ke false dan upload ulang setelah kredensial terhapus.
constexpr bool RESET_WIFI_SETTINGS_ON_BOOT = false;

constexpr uint32_t WIFI_TASK_STACK_BYTES = 8192;
constexpr uint32_t OTA_TASK_STACK_BYTES = 8192;
constexpr uint32_t LED_TASK_STACK_BYTES = 2048;

}  // namespace AppConfig

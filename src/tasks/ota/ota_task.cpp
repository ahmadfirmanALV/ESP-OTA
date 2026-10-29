#include "ota_task.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <string.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "app_config.h"
#include "app_events.h"
#include "app_identity.h"
#include "app_log.h"
#include "app_watchdog.h"
#include "build_version.h"

namespace {

WiFiClientSecure networkClient;
PubSubClient mqttClient(networkClient);
Preferences otaPreferences;
uint32_t lastDummyPublishMs = 0;
bool runningImagePendingVerify = false;
uint32_t runningImageBootTime = 0;

String firmwareServerIp() {
  const IPAddress address(
      AppConfig::OTA_SERVER_IP[0], AppConfig::OTA_SERVER_IP[1],
      AppConfig::OTA_SERVER_IP[2], AppConfig::OTA_SERVER_IP[3]);
  return address.toString();
}

bool firmwareServerConfigured() {
  return AppConfig::OTA_SERVER_IP[0] != 0 || AppConfig::OTA_SERVER_IP[1] != 0 ||
         AppConfig::OTA_SERVER_IP[2] != 0 || AppConfig::OTA_SERVER_IP[3] != 0;
}

void publishStatus(const char* status) {
  if (!mqttClient.connected()) {
    return;
  }

  StaticJsonDocument<256> document;
  const String deviceId = AppIdentity::deviceId();
  const String statusTopic = AppIdentity::statusTopic();
  document["device"] = deviceId;
  document["version"] = OTA_FIRMWARE_VERSION;
  document["status"] = status;

  char payload[256];
  const size_t length = serializeJson(document, payload, sizeof(payload));
  if (length > 0) {
    mqttClient.publish(
        statusTopic.c_str(),
        reinterpret_cast<const uint8_t*>(payload),
        static_cast<unsigned int>(length),
        false);
  }
}

void publishLatestVersion() {
  const String versionTopic = AppIdentity::latestVersionTopic();
  if (mqttClient.publish(versionTopic.c_str(), OTA_FIRMWARE_VERSION, true)) {
    appLogf("MQTT versi: %s = %s", versionTopic.c_str(), OTA_FIRMWARE_VERSION);
  } else {
    appLog("MQTT: gagal menerbitkan topic latest_version.");
  }
}

bool isValidMd5(const char* md5) {
  if (md5 == nullptr || strlen(md5) != 32) {
    return false;
  }

  for (size_t i = 0; i < 32; ++i) {
    const char value = md5[i];
    const bool isHex = (value >= '0' && value <= '9') ||
                       (value >= 'a' && value <= 'f') ||
                       (value >= 'A' && value <= 'F');
    if (!isHex) {
      return false;
    }
  }
  return true;
}

bool parseReleaseVersion(const char* version, uint32_t components[3]) {
  if (version == nullptr || version[0] != 'v') {
    return false;
  }

  const char* cursor = version + 1;
  for (size_t componentIndex = 0; componentIndex < 3; ++componentIndex) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    uint32_t componentValue = 0;
    while (*cursor >= '0' && *cursor <= '9') {
      const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
      if (componentValue > (0xFFFFFFFFU - digit) / 10U) {
        return false;
      }
      componentValue = componentValue * 10U + digit;
      ++cursor;
    }
    components[componentIndex] = componentValue;

    if (componentIndex < 2) {
      if (*cursor != '.') {
        return false;
      }
      ++cursor;
    }
  }

  if (*cursor != '\0') {
    return false;
  }
  return true;
}

int compareReleaseVersions(const uint32_t left[3], const uint32_t right[3]) {
  for (size_t i = 0; i < 3; ++i) {
    if (left[i] < right[i]) {
      return -1;
    }
    if (left[i] > right[i]) {
      return 1;
    }
  }
  return 0;
}

void rejectRelease(const char* version, const char* reason) {
  otaPreferences.putString("rejected", version);
  otaPreferences.remove("pending");
  appLogf("OTA %s ditolak: %s", version, reason);
  publishStatus("update_failed");
}

String getStoredString(const char* key) {
  if (!otaPreferences.isKey(key)) {
    return String();
  }
  return otaPreferences.getString(key);
}

void verifyRunningImage() {
  const String pendingVersion = getStoredString("pending");
  if (!pendingVersion.isEmpty() && pendingVersion != OTA_FIRMWARE_VERSION) {
    // Versi lama kembali berjalan setelah bootloader membatalkan firmware baru.
    otaPreferences.putString("rejected", pendingVersion);
    otaPreferences.remove("pending");
    appLogf("Rollback terdeteksi; rilis %s tidak akan dicoba ulang.", pendingVersion.c_str());
    publishStatus("rollback");
  }

  const esp_partition_t* runningPartition = esp_ota_get_running_partition();
  if (runningPartition == nullptr) {
    appLog("OTA: partisi firmware aktif tidak ditemukan.");
    return;
  }

  esp_ota_img_states_t imageState;
  const esp_err_t stateResult = esp_ota_get_state_partition(runningPartition, &imageState);
  if (stateResult == ESP_OK && imageState == ESP_OTA_IMG_PENDING_VERIFY) {
    const esp_err_t confirmResult = esp_ota_mark_app_valid_cancel_rollback();
    if (confirmResult == ESP_OK) {
      runningImagePendingVerify = false;
      otaPreferences.remove("pending");
      otaPreferences.remove("rejected");
      appLog("Firmware baru lolos koneksi Wi-Fi dan MQTT; rollback dibatalkan.");
      publishStatus("verified");
    } else {
      appLogf("Gagal mengesahkan firmware baru (ESP error %d).", confirmResult);
    }
    return;
  }

  if (!pendingVersion.isEmpty() && pendingVersion == OTA_FIRMWARE_VERSION) {
    if (stateResult != ESP_OK) {
      appLogf("Status rollback bootloader tidak tersedia (ESP error %d).", stateResult);
    }
    // Bersihkan penanda setelah firmware ini berhasil mencapai koneksi MQTT.
    otaPreferences.remove("pending");
    otaPreferences.remove("rejected");
  }

  runningImagePendingVerify = false;
}

void inspectBootValidationState() {
  const esp_partition_t* runningPartition = esp_ota_get_running_partition();
  if (runningPartition == nullptr) {
    return;
  }

  esp_ota_img_states_t imageState;
  if (esp_ota_get_state_partition(runningPartition, &imageState) == ESP_OK &&
      imageState == ESP_OTA_IMG_PENDING_VERIFY) {
    runningImagePendingVerify = true;
    runningImageBootTime = millis();
    appLog("Firmware OTA menunggu verifikasi Wi-Fi dan MQTT.");
  }
}

void rollbackIfValidationTimesOut() {
  if (!runningImagePendingVerify ||
      millis() - runningImageBootTime < AppConfig::OTA_BOOT_VALIDATION_TIMEOUT_MS) {
    return;
  }

  appLog("Firmware baru tidak lolos koneksi dalam 180 detik; meminta rollback.");
  const esp_err_t rollbackResult = esp_ota_mark_app_invalid_rollback_and_reboot();
  appLogf("Permintaan rollback mengembalikan ESP error %d.", rollbackResult);
  if (rollbackResult != ESP_OK) {
    ESP.restart();
  }
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool executeOta(const char* version, const char* url, const char* expectedMd5) {
  appLogf("OTA: mengunduh firmware %s.", version);

  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(url)) {
    rejectRelease(version, "URL HTTP tidak dapat dibuka");
    return false;
  }

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    http.end();
    rejectRelease(version, "server tidak mengembalikan HTTP 200");
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0) {
    http.end();
    rejectRelease(version, "ukuran firmware tidak valid");
    return false;
  }

  if (!Update.begin(static_cast<size_t>(contentLength))) {
    http.end();
    rejectRelease(version, "slot OTA tidak cukup besar atau tidak dapat dibuka");
    return false;
  }

  if (!Update.setMD5(expectedMd5)) {
    Update.abort();
    http.end();
    rejectRelease(version, "checksum MD5 tidak dapat dipasang");
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const size_t written = Update.writeStream(*stream);
  http.end();

  if (written != static_cast<size_t>(contentLength)) {
    Update.abort();
    rejectRelease(version, "unduhan firmware tidak lengkap");
    return false;
  }

  if (!Update.end() || !Update.isFinished()) {
    rejectRelease(version, "flash gagal atau checksum tidak cocok");
    return false;
  }

  otaPreferences.putString("pending", version);
  publishStatus("updating");
  appLogf("OTA %s selesai ditulis; ESP32 akan restart.", version);
  vTaskDelay(pdMS_TO_TICKS(300));
  ESP.restart();
  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  const String deviceTopic = AppIdentity::otaTopic();
  const String broadcastTopic = AppIdentity::otaBroadcastTopic();
  const bool addressedToDevice = topic != nullptr && deviceTopic == topic;
  const bool addressedToAll = topic != nullptr && broadcastTopic == topic;
  if (!addressedToDevice && !addressedToAll) {
    appLog("MQTT: perintah OTA dari topic yang tidak dikenal diabaikan.");
    return;
  }

  StaticJsonDocument<512> document;
  const DeserializationError parseError = deserializeJson(document, payload, length);
  if (parseError) {
    appLogf("MQTT: payload JSON tidak valid (%s).", parseError.c_str());
    return;
  }

  const char* version = document["version"];
  const char* url = document["url"];
  const char* md5 = document["md5"];
  const char* target = document["target"];
  if (version == nullptr || url == nullptr || md5 == nullptr ||
      strlen(version) == 0 || strlen(version) > 48 ||
      !isValidMd5(md5)) {
    appLog("MQTT: payload OTA tidak memiliki versi, URL, atau MD5 yang valid.");
    return;
  }

  const String deviceId = AppIdentity::deviceId();
  if ((addressedToDevice && target != nullptr && deviceId != target) ||
      (addressedToAll && (target == nullptr || strcmp(target, "all") != 0))) {
    const char* targetLabel = target == nullptr ? "(tidak ada)" : target;
    appLogf("OTA %s diabaikan; target %s, perangkat ini %s.",
            version, targetLabel, deviceId.c_str());
    return;
  }

  const String expectedPrefix = String("http://") + firmwareServerIp() + ":" +
                                String(AppConfig::OTA_HTTP_PORT) + "/download/";
  if (strncmp(url, expectedPrefix.c_str(), expectedPrefix.length()) != 0) {
    appLog("MQTT: URL firmware tidak cocok dengan server lokal yang dikonfigurasi.");
    return;
  }

  uint32_t advertisedVersion[3];
  if (!parseReleaseVersion(version, advertisedVersion)) {
    appLogf("OTA: versi broker '%s' bukan format vMAJOR.MINOR.PATCH.", version);
    return;
  }

  uint32_t runningVersion[3];
  if (parseReleaseVersion(OTA_FIRMWARE_VERSION, runningVersion)) {
    const int versionOrder = compareReleaseVersions(runningVersion, advertisedVersion);
    if (versionOrder == 0) {
      appLogf("OTA: firmware sudah memakai versi broker %s.", version);
      publishStatus("online");
      return;
    }
    if (versionOrder > 0) {
      appLogf("OTA: versi ESP %s lebih baru dari broker %s; downgrade dilewati.",
              OTA_FIRMWARE_VERSION, version);
      publishStatus("broker_release_older");
      return;
    }
    appLogf("OTA: versi ESP %s lebih lama dari broker %s; mulai update.",
            OTA_FIRMWARE_VERSION, version);
  } else {
    appLogf("OTA: firmware lokal %s menerima rilis broker %s untuk migrasi.",
            OTA_FIRMWARE_VERSION, version);
  }

  const String rejectedVersion = getStoredString("rejected");
  if (rejectedVersion == version) {
    appLogf("OTA: rilis %s pernah gagal dan diabaikan.", version);
    publishStatus("release_rejected");
    return;
  }

  executeOta(version, url, md5);
}

bool connectMqtt() {
  const String clientId = AppIdentity::deviceId();
  const bool hasCredentials = AppConfig::MQTT_USERNAME[0] != '\0';
  bool connected = false;

  if (hasCredentials) {
    connected = mqttClient.connect(
        clientId.c_str(), AppConfig::MQTT_USERNAME, AppConfig::MQTT_PASSWORD);
  } else {
    connected = mqttClient.connect(clientId.c_str());
  }

  if (!connected) {
    appLogf("MQTT: koneksi gagal, kode %d.", mqttClient.state());
    return false;
  }

  appLog("MQTT: terhubung ke broker.");
  lastDummyPublishMs = millis();
  verifyRunningImage();
  const String deviceOtaTopic = AppIdentity::otaTopic();
  const String broadcastOtaTopic = AppIdentity::otaBroadcastTopic();
  if (!mqttClient.subscribe(deviceOtaTopic.c_str(), 1) ||
      !mqttClient.subscribe(broadcastOtaTopic.c_str(), 1)) {
    appLog("MQTT: gagal subscribe ke topic OTA perangkat atau broadcast.");
    mqttClient.disconnect();
    return false;
  }
  publishLatestVersion();
  publishStatus("online");
  return true;
}

void otaTask(void* parameter) {
  (void)parameter;
  if (!appWatchdogSubscribeCurrentTask("ota_task")) {
    appLog("Watchdog: task OTA gagal didaftarkan; ESP32 akan restart.");
    ESP.restart();
    vTaskDelete(nullptr);
    return;
  }

  otaPreferences.begin("ota-state", false);
  inspectBootValidationState();
  mqttClient.setServer(AppConfig::MQTT_BROKER_HOST, AppConfig::MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(10);

  if (!firmwareServerConfigured() || AppConfig::MQTT_BROKER_HOST[0] == '\0') {
    appLog("OTA belum aktif: isi OTA_SERVER_IP dan MQTT_BROKER_HOST di app_config.h.");
  }
  if (AppConfig::MQTT_CA_CERT[0] == '\0') {
    appLog("MQTT TLS belum siap: isi OTA_MQTT_CA_CERT_PEM dengan CA certificate broker.");
  } else {
    networkClient.setCACert(AppConfig::MQTT_CA_CERT);
  }
  const bool serverConfigured = firmwareServerConfigured() &&
                                AppConfig::MQTT_BROKER_HOST[0] != '\0' &&
                                AppConfig::MQTT_CA_CERT[0] != '\0';

  uint32_t lastConnectionAttempt = 0;
  for (;;) {
    appWatchdogFeed();
    rollbackIfValidationTimesOut();
    xEventGroupWaitBits(
        gAppEvents,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(1000));

    if (WiFi.status() != WL_CONNECTED ||
        (xEventGroupGetBits(gAppEvents) & WIFI_CONNECTED_BIT) == 0) {
      if (mqttClient.connected()) {
        mqttClient.disconnect();
      }
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    if (!serverConfigured) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!mqttClient.connected()) {
      const uint32_t now = millis();
      if (now - lastConnectionAttempt >= 5000) {
        lastConnectionAttempt = now;
        connectMqtt();
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    mqttClient.loop();

    const uint32_t now = millis();
    if (now - lastDummyPublishMs >= AppConfig::MQTT_DUMMY_INTERVAL_MS) {
      lastDummyPublishMs = now;
      const String value = String(random(0, 101));
      const String topic = AppIdentity::dummyTopic();
      if (mqttClient.publish(topic.c_str(), value.c_str())) {
        appLogf("MQTT dummy: %s = %s", topic.c_str(), value.c_str());
      } else {
        appLog("MQTT dummy: publish gagal.");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

bool otaTaskStart() {
  return xTaskCreatePinnedToCore(
             otaTask,
             "ota_task",
             AppConfig::OTA_TASK_STACK_BYTES,
             nullptr,
             2,
             nullptr,
             1) == pdPASS;
}

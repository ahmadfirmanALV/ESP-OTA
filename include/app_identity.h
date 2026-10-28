#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"

namespace AppIdentity {

inline String deviceId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return String(AppConfig::OTA_DEVICE_ID_PREFIX) + mac;
}

inline String statusTopic() {
  return String(AppConfig::OTA_STATUS_TOPIC_PREFIX) + "/" + deviceId();
}

inline String dummyTopic() {
  return String(AppConfig::MQTT_DUMMY_TOPIC_PREFIX) + "/" + deviceId() + "/random";
}

}  // namespace AppIdentity

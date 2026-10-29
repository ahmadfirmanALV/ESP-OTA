import logging
import os
import random
import signal
import time

import paho.mqtt.client as mqtt


BROKER_HOST = os.environ.get("OTA_MQTT_HOST", "localhost").strip()
BROKER_PORT = int(os.environ.get("OTA_MQTT_PORT", "8883"))
USERNAME = os.environ.get("OTA_MQTT_USERNAME", "")
PASSWORD = os.environ.get("OTA_MQTT_PASSWORD", "")
USE_TLS = os.environ.get("OTA_MQTT_TLS", "1").strip().lower() in {"1", "true", "yes", "on"}
CA_CERT = os.environ.get("OTA_MQTT_CA_CERT", "").strip()
TOPIC = os.environ.get("OTA_DUMMY_TOPIC", "demo/random").strip()
INTERVAL_SECONDS = float(os.environ.get("OTA_DUMMY_INTERVAL_SECONDS", "5"))
MIN_VALUE = int(os.environ.get("OTA_DUMMY_MIN", "0"))
MAX_VALUE = int(os.environ.get("OTA_DUMMY_MAX", "100"))

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("mqtt-dummy-publisher")
stop_requested = False


def request_stop(_signum, _frame):
    global stop_requested
    stop_requested = True


def main():
    if not BROKER_HOST or not TOPIC:
        raise SystemExit("OTA_MQTT_HOST dan OTA_DUMMY_TOPIC tidak boleh kosong.")
    if INTERVAL_SECONDS <= 0 or MIN_VALUE > MAX_VALUE:
        raise SystemExit("Interval harus positif dan OTA_DUMMY_MIN <= OTA_DUMMY_MAX.")

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="ota-dummy-publisher",
        protocol=mqtt.MQTTv311,
    )
    if USERNAME:
        client.username_pw_set(USERNAME, PASSWORD)
    if USE_TLS:
        client.tls_set(ca_certs=CA_CERT or None)
        client.tls_insecure_set(False)

    try:
        client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
        client.loop_start()
        logger.info(
            "Terhubung ke %s:%d%s; publish ke topic '%s' setiap %s detik.",
            BROKER_HOST,
            BROKER_PORT,
            " dengan TLS" if USE_TLS else "",
            TOPIC,
            INTERVAL_SECONDS,
        )

        signal.signal(signal.SIGINT, request_stop)
        signal.signal(signal.SIGTERM, request_stop)
        next_publish = time.monotonic()
        while not stop_requested:
            remaining = next_publish - time.monotonic()
            if remaining > 0:
                time.sleep(min(remaining, 0.25))
                continue

            value = random.randint(MIN_VALUE * 100, MAX_VALUE * 100) / 100.0
            payload = f"{value:.2f}"
            info = client.publish(TOPIC, payload, qos=0, retain=False)
            if info.rc != mqtt.MQTT_ERR_SUCCESS:
                logger.warning("Publish gagal dengan kode %s.", info.rc)
            else:
                info.wait_for_publish(timeout=10)
                logger.info("%s <- %s", TOPIC, payload)
            next_publish += INTERVAL_SECONDS
    finally:
        client.disconnect()
        client.loop_stop()
        logger.info("Publisher dihentikan.")


if __name__ == "__main__":
    main()

import hashlib
import json
import logging
import os
import re
import threading
import time
from pathlib import Path
from urllib.parse import quote

import paho.mqtt.client as mqtt
import requests
from flask import Flask, abort, send_from_directory


GITHUB_REPO = os.environ.get("OTA_GITHUB_REPO", "").strip()
GITHUB_TOKEN = os.environ.get("GITHUB_TOKEN", "").strip()
SERVER_IP = os.environ.get("OTA_SERVER_IP", "192.168.211.251").strip()
MQTT_BROKER = os.environ.get("OTA_MQTT_HOST", "127.0.0.1").strip()
MQTT_PORT = int(os.environ.get("OTA_MQTT_PORT", "1883"))
MQTT_USERNAME = os.environ.get("OTA_MQTT_USERNAME", "")
MQTT_PASSWORD = os.environ.get("OTA_MQTT_PASSWORD", "")
MQTT_TOPIC_OTA = "gateway/ota/update"
MQTT_TOPIC_STATUS = "gateway/status"
SERVER_PORT = int(os.environ.get("OTA_SERVER_PORT", "5000"))
CHECK_INTERVAL_SECONDS = int(os.environ.get("OTA_CHECK_INTERVAL_SECONDS", "300"))
FIRMWARE_DIR = Path(__file__).resolve().parent / "firmware_files"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
logger = logging.getLogger("esp32-ota-server")
app = Flask(__name__)
last_processed_tag = None
last_update_payload = None


def github_headers():
    headers = {
        "User-Agent": "ESP32-OTA-Server",
        "Accept": "application/vnd.github+json",
    }
    if GITHUB_TOKEN:
        headers["Authorization"] = f"Bearer {GITHUB_TOKEN}"
    return headers


def make_mqtt_client():
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="esp32-ota-release-server",
    )
    if MQTT_USERNAME:
        client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    return client


def publish_update(payload):
    client = make_mqtt_client()
    client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_start()
    try:
        result = client.publish(MQTT_TOPIC_OTA, payload, qos=1, retain=True)
        result.wait_for_publish(timeout=15)
        if result.rc != mqtt.MQTT_ERR_SUCCESS or not result.is_published():
            raise RuntimeError(f"MQTT publish gagal, rc={result.rc}")
    finally:
        client.disconnect()
        client.loop_stop()


def get_latest_release():
    api_url = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
    response = requests.get(api_url, headers=github_headers(), timeout=20)
    response.raise_for_status()
    release = response.json()
    tag_name = release.get("tag_name")
    if not tag_name:
        raise RuntimeError("GitHub Release tidak memiliki tag_name.")

    assets = release.get("assets", [])
    firmware_asset = next(
        (item for item in assets if item.get("name") == "firmware.bin"),
        None,
    )
    if firmware_asset is None:
        raise RuntimeError(f"Release {tag_name} tidak memiliki asset firmware.bin.")

    manifest_asset = next(
        (item for item in assets if item.get("name") == "ota_manifest.json"),
        None,
    )
    if manifest_asset is None:
        raise RuntimeError(
            f"Release {tag_name} belum mendukung protokol OTA ini; manifest tidak ada."
        )

    manifest_response = requests.get(
        manifest_asset["browser_download_url"],
        headers=github_headers(),
        timeout=20,
    )
    manifest_response.raise_for_status()
    manifest = manifest_response.json()
    if manifest.get("ota_protocol") != 1 or manifest.get("version") != tag_name:
        raise RuntimeError(f"Manifest Release {tag_name} tidak cocok atau tidak didukung.")
    if manifest.get("board") != "esp32doit-devkit-v1":
        raise RuntimeError(f"Release {tag_name} ditujukan untuk board yang berbeda.")

    return tag_name, firmware_asset


def download_firmware(tag_name, asset):
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    safe_tag = re.sub(r"[^A-Za-z0-9._-]", "_", tag_name)
    filename = f"firmware_{safe_tag}.bin"
    final_path = FIRMWARE_DIR / filename
    temporary_path = FIRMWARE_DIR / f"{filename}.part"
    checksum = hashlib.md5()
    first_byte = None
    downloaded_size = 0

    try:
        with requests.get(
            asset["browser_download_url"],
            headers=github_headers(),
            stream=True,
            timeout=(10, 120),
        ) as response:
            response.raise_for_status()
            with temporary_path.open("wb") as firmware_file:
                for chunk in response.iter_content(chunk_size=64 * 1024):
                    if not chunk:
                        continue
                    if first_byte is None:
                        first_byte = chunk[0]
                    downloaded_size += len(chunk)
                    checksum.update(chunk)
                    firmware_file.write(chunk)

        expected_size = asset.get("size")
        if first_byte != 0xE9:
            raise ValueError("Asset bukan image aplikasi ESP32 yang valid.")
        if expected_size is not None and downloaded_size != expected_size:
            raise ValueError("Ukuran firmware unduhan tidak cocok dengan metadata GitHub.")
        temporary_path.replace(final_path)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise

    local_url = f"http://{SERVER_IP}:{SERVER_PORT}/download/{quote(filename)}"
    payload = {
        "version": tag_name,
        "url": local_url,
        "md5": checksum.hexdigest(),
    }
    return filename, payload


def process_latest_release():
    global last_processed_tag, last_update_payload
    tag_name, firmware_asset = get_latest_release()
    if tag_name != last_processed_tag:
        filename, last_update_payload = download_firmware(tag_name, firmware_asset)
        last_processed_tag = tag_name
        logger.info("Firmware rilis %s disimpan sebagai %s.", tag_name, filename)

    # Publish retained berulang agar broker restart atau ESP offline dapat pulih.
    if last_update_payload is not None:
        publish_update(json.dumps(last_update_payload, separators=(",", ":")))
        logger.info("Pengumuman OTA rilis %s dikirim lewat MQTT.", tag_name)


def github_polling_worker():
    while True:
        try:
            process_latest_release()
        except Exception:
            logger.exception("Gagal memeriksa atau mengumumkan firmware terbaru.")
        time.sleep(CHECK_INTERVAL_SECONDS)


@app.get("/health")
def health():
    return {"status": "ok", "github_repo": GITHUB_REPO}


@app.get("/download/<path:filename>")
def serve_firmware(filename):
    if Path(filename).name != filename or not filename.endswith(".bin"):
        abort(404)
    return send_from_directory(
        FIRMWARE_DIR,
        filename,
        mimetype="application/octet-stream",
    )


def main():
    if not GITHUB_REPO or "/" not in GITHUB_REPO:
        raise SystemExit("Set OTA_GITHUB_REPO=owner/repository.")
    if CHECK_INTERVAL_SECONDS < 30:
        raise SystemExit("OTA_CHECK_INTERVAL_SECONDS harus minimal 30 detik.")

    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    threading.Thread(target=github_polling_worker, daemon=True).start()
    logger.info(
        "OTA server aktif: repo=%s, broker=%s:%s, HTTP=%s:%s",
        GITHUB_REPO,
        MQTT_BROKER,
        MQTT_PORT,
        SERVER_IP,
        SERVER_PORT,
    )
    app.run(host="0.0.0.0", port=SERVER_PORT, debug=False, use_reloader=False)


if __name__ == "__main__":
    main()

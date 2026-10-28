# COBA OTA ESP32

Proyek PlatformIO untuk DOIT ESP32 DEVKIT V1 dengan alur update terpusat:

```text
VS Code/tag -> GitHub Actions -> GitHub Release (firmware.bin + manifest)
                                      |
ESP32 <- MQTT broker <- ota_server.py
  |                         |
  +-- HTTP firmware <-------+
```

**Alamat broker MQTT dan alamat server HTTP adalah dua pengaturan berbeda.** Username/password hanya mengautentikasi ke broker; ESP32 tetap perlu host/IP broker untuk tersambung. ESP32 juga memerlukan IP komputer lokal untuk mengambil file firmware lewat HTTP, meskipun brokernya berada di cloud.

## Isi repo

- `src/` dan `include/`: firmware Arduino + FreeRTOS, WiFiManager, dan klien OTA MQTT.
- `partitions.csv`: dua slot aplikasi 1.5 MB, `otadata`, SPIFFS, dan coredump untuk flash 4 MB.
- `.github/workflows/build-release.yml`: build pada tag `vMAJOR.MINOR.PATCH`; rilis berisi `firmware.bin` dan `ota_manifest.json`.
- `server/ota_server.py`: mengambil rilis terbaru yang kompatibel, menyajikan firmware lewat HTTP lokal, dan menerbitkan perintah MQTT retained.
- `server/dummy_publisher.py`: mengirim angka acak ke topic MQTT untuk uji koneksi, default setiap 5 detik.
- `include/ota_secrets.h.example`: template kredensial broker untuk build lokal. File rahasia sebenarnya diabaikan Git.

PlatformIO dipatok ke platform pioarduino `55.03.311` dan framework Arduino ESP32 3.3.11. SDK platform ini mengaktifkan rollback bootloader. Jangan hapus pin platform tanpa memeriksa ulang dukungan rollback dan hasil build.

## Konfigurasi alamat dan kredensial

1. `OTA_SERVER_IP` di `include/app_config.h` adalah IP LAN komputer yang menyajikan firmware di port HTTP 5000. Berdasarkan konfigurasi Wi-Fi terakhir, nilainya sekarang `192.168.211.251`; samakan dengan environment variable `OTA_SERVER_IP` di server Python.
2. `MQTT_BROKER_HOST` adalah host broker yang terpisah. Nilai awal `192.168.211.251` cocok jika broker berjalan di PC tersebut dan menerima koneksi dari LAN. Untuk broker cloud, ganti dengan hostname broker yang diberikan penyedia layanan. Username/password saja tidak cukup untuk menemukan broker.
3. Port broker sekarang `8883` dengan TLS dan verifikasi sertifikat aktif.
4. ESP32 memakai DHCP (`USE_STATIC_IP = false`) agar mendapat alamat dari router; komputer server memakai IP Wi-Fi tetap `192.168.211.251` agar URL firmware tidak berubah.
5. Ganti `WIFI_AP_PASSWORD` bawaan sebelum deployment.
6. Untuk build lokal, salin `include/ota_secrets.h.example` ke `include/ota_secrets.h`, lalu isi username/password dan CA certificate PEM broker di `OTA_MQTT_CA_CERT_PEM`. File `ota_secrets.h` tidak di-commit. Untuk firmware GitHub, buat Actions Secrets `OTA_MQTT_USERNAME`, `OTA_MQTT_PASSWORD`, dan `OTA_MQTT_CA_CERT_PEM`; workflow menyisipkannya saat build.

Nama host `MQTT_BROKER_HOST` harus tercantum pada SAN sertifikat broker. `server.crt` lokal yang diperiksa mencakup IP `192.168.211.251` dan `localhost`; ESP32 memakai IP LAN, sedangkan proses Python di PC dapat memakai `localhost`.

Jika repo GitHub private, set environment variable `GITHUB_TOKEN` di komputer server menggunakan token dengan akses baca ke repo/releases. Jangan simpan token ini di kode atau workflow. Server Python memakai `OTA_MQTT_HOST`, `OTA_MQTT_PORT`, `OTA_MQTT_USERNAME`, `OTA_MQTT_PASSWORD`, dan `OTA_MQTT_TLS`. Jika CA broker tidak dipercaya Windows, set `OTA_MQTT_CA_CERT` ke path file CA PEM.

Contoh menggunakan HTTP untuk firmware dan TLS untuk MQTT. Batasi akses port ke perangkat/jaringan tepercaya. Kredensial dan CA MQTT yang dimasukkan saat build ikut tertanam di firmware; gunakan akun broker khusus dengan izin topik terbatas, bukan akun admin. MD5 mendeteksi kerusakan saat transfer, bukan tanda tangan keamanan firmware.

## GitHub Actions dan rilis

Push source dan workflow ke repo GitHub. Workflow membangun environment `doit_devkit_v1`, menanam tag serta kredensial broker dari Actions Secrets, lalu membuat Release berisi `firmware.bin` dan `ota_manifest.json`. Token workflow berasal dari `GITHUB_TOKEN` bawaan dengan izin `contents: write`.

Untuk rilis baru, gunakan tag yang belum pernah dipakai:

```powershell
git add .
git commit -m "Update fitur OTA"
git push origin main
git tag v1.0.1
git push origin v1.0.1
```

Ganti `v1.0.1` dengan nomor versi berikutnya. Tag harus berformat `vMAJOR.MINOR.PATCH`. Setelah workflow sukses, pastikan Release memiliki kedua asset dengan nama persis `firmware.bin` dan `ota_manifest.json`.

## Menjalankan server di Windows

Pasang Python 3.10+ dan Mosquitto. Jika memakai broker cloud, Mosquitto lokal tidak diperlukan. Dari PowerShell di root proyek:

```powershell
py -m venv server\.venv
server\.venv\Scripts\python.exe -m pip install -r server\requirements.txt
$env:OTA_GITHUB_REPO = "OWNER/NAMA_REPO"
$env:OTA_SERVER_IP = "192.168.211.251"
$env:OTA_MQTT_HOST = "localhost"
$env:OTA_MQTT_PORT = "8883"
$env:OTA_MQTT_TLS = "1"
# Jika CA lokal tidak dipercaya Windows, isi path ke file CA PEM:
$env:OTA_MQTT_CA_CERT = "C:\Program Files\Mosquitto\certs\ca.crt"
# Jika broker memakai autentikasi, set juga username dan password:
$env:OTA_MQTT_USERNAME = ""
$env:OTA_MQTT_PASSWORD = ""
server\.venv\Scripts\python.exe server\ota_server.py
```

Ganti nilai contoh dengan konfigurasi sebenarnya. `OTA_SERVER_IP` harus IP LAN komputer ini. Untuk broker lokal pada komputer yang sama, `OTA_MQTT_HOST=localhost` cocok bila sertifikatnya mencakup `localhost`; untuk ESP32, host harus nama LAN/DNS yang tercakup di sertifikat. Jika CA broker tidak dipercaya Windows, set `$env:OTA_MQTT_CA_CERT` ke file CA PEM. Jika repo private, set `$env:GITHUB_TOKEN`.

Server memeriksa GitHub setiap lima menit. Server mengunduh rilis kompatibel, memeriksa ukuran serta image firmware, lalu menerbitkan pesan retained. Pesan retained memungkinkan ESP32 yang offline mengambil rilis saat tersambung kembali. Perintah unduh tetap menunjuk ke komputer lokal pada `OTA_SERVER_IP`.

## Uji MQTT dengan angka dummy

Setelah ESP32 tersambung ke broker TLS, firmware mengirim angka bulat acak `0`–`100` ke topic `gateway/dummy/random` setiap 5 detik. Di MQTT Explorer subscribe ke topic tersebut (atau `#`). Host broker firmware diatur lewat `MQTT_BROKER_HOST`, port `8883`, username/password, dan CA PEM.

Publisher Python di bawah ini menguji broker dari PC tanpa ESP32, dan mengirim ke topic `demo/random` setiap 5 detik.

Publisher ini mengirim angka bulat acak `0`–`100` ke `demo/random` setiap 5 detik. Default-nya memakai TLS di `localhost:8883`. Jalankan di PowerShell setelah dependency terpasang:

```powershell
$env:OTA_MQTT_HOST = "localhost"
$env:OTA_MQTT_PORT = "8883"
$env:OTA_MQTT_TLS = "1"
$env:OTA_MQTT_CA_CERT = "C:\Program Files\Mosquitto\certs\ca.crt"
$env:OTA_DUMMY_TOPIC = "demo/random"
# Isi jika broker meminta autentikasi:
$env:OTA_MQTT_USERNAME = "username-broker"
$env:OTA_MQTT_PASSWORD = "password-broker"
server\.venv\Scripts\python.exe server\dummy_publisher.py
```

Untuk koneksi pada MQTT Explorer yang berjalan di komputer yang sama, `localhost` mengarah ke broker lokal. Di MQTT Explorer subscribe ke `demo/random` (atau `#`) untuk melihat nilainya. Tekan `Ctrl+C` untuk berhenti. Untuk ESP32 di jaringan, gunakan alamat LAN/nama DNS broker yang cocok dengan sertifikat, bukan `localhost`.

## Upload awal ESP32

Install PlatformIO IDE di VS Code. Build lokal memakai environment `doit_devkit_v1`:

```powershell
pio run -e doit_devkit_v1
```

**Upload awal dan perpindahan tabel partisi wajib lewat USB.** Firmware yang sudah terpasang belum mengenal MQTT OTA, dan OTA tidak mengganti tabel partisi. Untuk pertama kali, sambungkan ESP32 via USB dan jalankan task **PlatformIO: Upload DOIT ESP32 DEVKIT V1 to COM4** atau perintah berikut dengan port yang benar:

```powershell
pio run -e doit_devkit_v1 -t upload --upload-port COM4
pio device monitor --port COM4
```

Setelah boot, isi portal `ESP32-Setup` bila diminta. Serial Monitor 115200 menampilkan status Wi-Fi dan broker. Pastikan `OTA_SERVER_IP` di firmware/server sama, `MQTT_BROKER_HOST` cocok dengan nama di sertifikat TLS, dan CA PEM, port, username, serta password sesuai broker.

Sesudah upload USB awal dan tersedia Release dengan manifest yang cocok, server akan mengumumkan firmware lewat MQTT. Pantau `gateway/status` atau Serial Monitor.

## Catatan operasional

- Release tanpa `ota_manifest.json` dengan protokol `1` dan board `esp32doit-devkit-v1` akan diabaikan server.
- Jika update gagal atau rollback terjadi, firmware menyimpan tag gagal di NVS dan mengabaikannya. Buat tag versi baru setelah memperbaiki firmware.
- Firmware ESP32 dan server OTA memakai TLS port 8883; keduanya memerlukan CA broker yang sesuai.
- Firmware release publik dapat diunduh siapa pun jika repo/release publik. Gunakan repo private dan `GITHUB_TOKEN` server bila firmware harus tetap privat.
- ArduinoOTA langsung dari PlatformIO tidak aktif di konfigurasi ini; pemulihan dilakukan lewat USB.

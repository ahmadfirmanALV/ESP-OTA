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
3. `MQTT_PORT` harus cocok dengan port broker. Default proyek ini `1883` untuk MQTT TCP. Firmware ESP32 saat ini belum mendukung TLS/port `8883`.
4. ESP32 memakai DHCP (`USE_STATIC_IP = false`) agar mendapat alamat dari router; komputer server memakai IP Wi-Fi tetap `192.168.211.251` agar URL firmware tidak berubah.
5. Ganti `WIFI_AP_PASSWORD` bawaan sebelum deployment.
6. Untuk build lokal memakai broker ber-password, salin `include/ota_secrets.h.example` ke `include/ota_secrets.h`, lalu isi kredensial. File `ota_secrets.h` tidak di-commit. Untuk firmware GitHub, buat Actions Secrets `OTA_MQTT_USERNAME` dan `OTA_MQTT_PASSWORD`; workflow menyisipkannya saat build.

Jika repo GitHub private, set environment variable `GITHUB_TOKEN` di komputer server menggunakan token dengan akses baca ke repo/releases. Jangan simpan token ini di kode atau workflow. Server Python memakai `OTA_MQTT_HOST`, `OTA_MQTT_PORT`, `OTA_MQTT_USERNAME`, dan `OTA_MQTT_PASSWORD`; nilainya harus sama dengan endpoint/kredensial broker pada ESP32.

Contoh sekarang menggunakan HTTP dan MQTT TCP. Batasi akses port ke perangkat/jaringan tepercaya. Kredensial MQTT yang dimasukkan saat build ikut tertanam di firmware dan dapat diekstrak dari file `.bin`; gunakan akun broker khusus dengan izin topik terbatas, bukan akun admin. MD5 mendeteksi kerusakan saat transfer, bukan tanda tangan keamanan firmware.

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
$env:OTA_MQTT_PORT = "1883"
# Jika broker memakai autentikasi, set juga username dan password:
$env:OTA_MQTT_USERNAME = ""
$env:OTA_MQTT_PASSWORD = ""
server\.venv\Scripts\python.exe server\ota_server.py
```

Ganti nilai contoh dengan konfigurasi sebenarnya. `OTA_SERVER_IP` harus IP LAN komputer ini. Untuk Mosquitto di komputer yang sama, `OTA_MQTT_HOST=127.0.0.1` cocok; untuk broker cloud, isi hostname cloud. Jika repo private, set `$env:GITHUB_TOKEN`. Jika broker memakai autentikasi, masukkan kredensial yang sama ke Actions Secrets dan `include/ota_secrets.h` lokal.

Server memeriksa GitHub setiap lima menit. Server mengunduh rilis kompatibel, memeriksa ukuran serta image firmware, lalu menerbitkan pesan retained. Pesan retained memungkinkan ESP32 yang offline mengambil rilis saat tersambung kembali. Perintah unduh tetap menunjuk ke komputer lokal pada `OTA_SERVER_IP`.

## Uji MQTT dengan angka dummy

Setelah ESP32 tersambung ke broker, firmware mengirim angka bulat acak `0`–`100` ke topic `gateway/dummy/random` setiap 5 detik. Di MQTT Explorer subscribe ke topic tersebut (atau `#`). Host broker firmware diatur lewat `MQTT_BROKER_HOST`, port `1883`, dan kredensial opsional dari `include/ota_secrets.h`.

Publisher Python di bawah ini menguji broker dari PC tanpa ESP32, dan mengirim ke topic `demo/random` setiap 5 detik.

Publisher ini mengirim angka bulat acak `0`–`100` ke `demo/random` setiap 5 detik. Default-nya memakai broker lokal tanpa TLS di `127.0.0.1:1883`. Jalankan di PowerShell setelah dependency terpasang:

```powershell
$env:OTA_MQTT_HOST = "localhost"
$env:OTA_MQTT_PORT = "8883"
$env:OTA_MQTT_TLS = "1"
$env:OTA_DUMMY_TOPIC = "demo/random"
# Isi jika broker meminta autentikasi:
$env:OTA_MQTT_USERNAME = "username-broker"
$env:OTA_MQTT_PASSWORD = "password-broker"
server\.venv\Scripts\python.exe server\dummy_publisher.py
```

Untuk koneksi pada MQTT Explorer yang berjalan di komputer yang sama, `localhost` mengarah ke broker lokal. Jika memakai broker lain, ganti host dan port sesuai profilnya; untuk broker tanpa TLS gunakan port yang sesuai dan set `$env:OTA_MQTT_TLS = "0"`. Di MQTT Explorer subscribe ke `demo/random` (atau `#`) untuk melihat nilainya. Tekan `Ctrl+C` untuk berhenti. `OTA_MQTT_CA_CERT` dapat diisi path CA certificate jika broker TLS memakai CA khusus. Untuk ESP32 di jaringan, gunakan alamat LAN broker `192.168.211.251`, bukan `localhost`.

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

Setelah boot, isi portal `ESP32-Setup` bila diminta. Serial Monitor 115200 menampilkan status Wi-Fi dan broker. Pastikan `OTA_SERVER_IP` di firmware/server sama, dan `MQTT_BROKER_HOST`, port, username, serta password cocok dengan broker.

Sesudah upload USB awal dan tersedia Release dengan manifest yang cocok, server akan mengumumkan firmware lewat MQTT. Pantau `gateway/status` atau Serial Monitor.

## Catatan operasional

- Release tanpa `ota_manifest.json` dengan protokol `1` dan board `esp32doit-devkit-v1` akan diabaikan server.
- Jika update gagal atau rollback terjadi, firmware menyimpan tag gagal di NVS dan mengabaikannya. Buat tag versi baru setelah memperbaiki firmware.
- Jika broker mensyaratkan TLS/port 8883, publisher Python dapat memakainya untuk uji dari PC; firmware ESP32 masih perlu dukungan TLS dan CA certificate.
- Firmware release publik dapat diunduh siapa pun jika repo/release publik. Gunakan repo private dan `GITHUB_TOKEN` server bila firmware harus tetap privat.
- ArduinoOTA langsung dari PlatformIO tidak aktif di konfigurasi ini; pemulihan dilakukan lewat USB.

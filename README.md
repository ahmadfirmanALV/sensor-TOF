# Penghitung Okupansi Ruang: DOIT ESP32 + 2x VL53L0X (Arsitektur Modular FreeRTOS)

Firmware PlatformIO berbasis ESP32 menggunakan framework Arduino dan FreeRTOS Dual-Core. Orang dihitung saat melewati pintu: sensor A di sisi luar, sensor B di sisi dalam. Urutan A ke B menambah hitungan; urutan B ke A menguranginya. Hitungan disimpan secara persisten di NVS (Non-Volatile Storage) ESP32 dan dipulihkan secara otomatis setelah reboot.

---

## Struktur Direktori Modular

Proyek ini telah direfaktor menjadi arsitektur modular di mana setiap fitur dipisahkan ke dalam modul/folder tersendiri dengan sepasang berkas `.h` dan `.cpp`:

```text
SENSOR TOF/
├── platformio.ini               # Konfigurasi PlatformIO & dependensi pustaka
├── README.md                    # Dokumentasi lengkap proyek
├── include/
│   └── secret.h.example         # Template konfigurasi rahasia lokal
└── src/
    ├── config.h                 # Definisi pin, konstanta, enum, dan handle bersama
    ├── main.cpp                 # Bootstrapper & Master Coordinator Task
    ├── occupancy/
    │   ├── occupancy_service.h  # Header layanan okupansi & state machine
    │   └── occupancy_service.cpp# Implementasi deteksi arah, hitungan, & NVS
    ├── sensor/
    │   ├── tof_sensor.h         # Header sensor VL53L0X
    │   └── tof_sensor.cpp       # Implementasi I2C, XSHUT, filter median, & ToF task
    ├── serial/
    │   ├── serial_service.h     # Header layanan perintah Serial
    │   └── serial_service.cpp   # Implementasi parser Serial & task
    ├── thingspeak/
    │   ├── thingspeak_service.h # Header layanan IoT ThingSpeak
    │   └── thingspeak_service.cpp # Implementasi pengiriman data 5 field (Core 0)
    ├── wdt/
    │   ├── wdt_service.h        # Header Hardware Task Watchdog Timer
    │   └── wdt_service.cpp      # Implementasi pendaftaran & reset WDT (20 detik)
    └── wifi/
        ├── wifi_service.h       # Header layanan Wi-Fi & Web Captive Portal
        └── wifi_service.cpp     # Implementasi WiFiManager & penyimpanan NVS
```

---

## Pola "1 Master Coordinator Task Memanggil Beberapa Task"

Di dalam berkas [src/main.cpp](file:///C:/Users/ASUS/Documents/PROJECT/SENSOR%20TOF/src/main.cpp), alur inisialisasi dikendalikan oleh sebuah task koordinator utama:
- **`systemCoordinatorTask`** dijalankan sebagai task supervisor.
- Task ini memanggil dan mengawasi setiap worker task di core yang sesuai:
  1. `startToFTask()`: Membaca sensor A & B (Core 1, Prioritas 3).
  2. `startOccupancyTask()`: Memproses state machine arah dan menyimpan ke NVS (Core 1, Prioritas 2).
  3. `startSerialTask()`: Memproses input perintah Serial Monitor (Core 1, Prioritas 1).
  4. `startThingSpeakTask()`: Mengelola Wi-Fi dan pengiriman ThingSpeak (Core 0, Prioritas 1).
- Task koordinator kemudian bertindak sebagai supervisor kesehatan sistem (memantau ketersediaan RAM heap bebas dan memberi makan WDT secara rutin).

---

## Kompartemen Watchdog Timer (WDT)

Watchdog Timer (`src/wdt/`) melindungi sistem dari potensi hang atau kebuntuan:
- Timeout diatur ke **20 detik** dengan opsi *panic/auto-reboot*.
- Setiap task (`SystemCoordinator`, `ToFReadTask`, `OccupancyTask`, `SerialTask`, `ThingSpeakTask`, dan `loopTask`) didaftarkan ke WDT dengan `wdtRegisterCurrentTask()`.
- Setiap alur memanggil `wdtFeed()` di setiap iterasi loop.
- Jika terjadi crash atau hang pada I2C/Wi-Fi lebih dari 20 detik, mikrokontroler restart otomatis dan memulihkan hitungan dari NVS.

---

## Kompartemen WiFiManager

- **Access Point Otomatis**: Jika Wi-Fi belum terkonfigurasi, perangkat mengaktifkan AP:
  - **SSID**: `ESP32-RoomCounter-AP` (Password: `12345678`)
  - **IP Portal**: `192.168.4.1`
- **Konfigurasi Web**: Menyediakan kolom input untuk SSID, Password Wi-Fi, serta **ThingSpeak Channel ID** dan **Write API Key**.
- **Portal dan pembacaan sensor**: WiFiManager berjalan dari `ThingSpeakTask` setelah task sensor dan okupansi aktif. Portal dapat menunggu sampai 120 detik, tetapi pembacaan sensor tetap berjalan.
- **On-Demand Portal**: Ketik `W` atau `wifi` pada Serial Monitor untuk membuka portal kapan saja.

---

## Kompartemen ThingSpeak IoT Cloud

Berjalan pada **Core 0** (Core protokol jaringan ESP32) agar transmisi data tidak mengganggu pembacaan sensor pada Core 1:
- **Field 1**: Orang Masuk (`totalEntry` kumulatif)
- **Field 2**: Orang Keluar (`totalExit` kumulatif)
- **Field 3**: Orang di Dalam (`occupancyCount` saat ini)
- **Field 4**: Status Ruangan (`1` = Terisi, `0` = Kosong)
- **Field 5**: State Lintasan (`0` = IDLE, `1` = Masuk/Tunggu B, `2` = Keluar/Tunggu A, `3` = Tunggu Clear)
- **Rate-Limit**: Minimal 15 detik (sesuai aturan ThingSpeak free tier) dan dikirim berkala setiap 20 detik.

---

## Posisi Sensor dan Kalibrasi

Untuk menghitung arah orang yang melewati satu pintu, pasang sensor pada sisi pintu, bukan berdampingan di atas pintu menghadap ke bawah. A harus terlewati lebih dulu saat orang masuk, lalu B. Pasang keduanya pada ketinggian sama, sekitar 80-100 cm dari lantai sebagai titik awal, dan arahkan melintang ke jalur lewat menuju permukaan tetap di seberangnya. Mulai dengan jarak sekitar 50-60 cm sepanjang arah berjalan jika ruang memungkinkan; ini titik awal, bukan ukuran wajib. Jika A dan B sering aktif bersamaan, tambah jarak atau sesuaikan arah sensor.

Ambang di `src/config.h` bekerja dengan kondisi jarak aktif `<= TRIGGER_MM` dan jarak kosong `>= RELEASE_MM`. Saat kosong, permukaan di seberang harus terbaca lebih jauh dari nilai release. Saat orang lewat, jarak ke tubuh harus turun di bawah nilai trigger. Contoh: jika jarak kosong sekitar 800 mm dan orang terbaca sekitar 350 mm, coba trigger 500 mm dan release 650 mm. Setel sensor A dan B secara terpisah berdasarkan hasil di Serial Monitor.

Hindari sensor melihat daun pintu yang bergerak. Jika tidak ada permukaan tetap dalam jangkauan, pembacaan tanpa target dianggap sebagai kondisi kosong oleh firmware.

## Wiring Pin

| DOIT ESP32 | Sensor A | Sensor B | Keterangan |
| :--- | :--- | :--- | :--- |
| **3V3** | VIN / VCC | VIN / VCC | Daya 3.3V |
| **GND** | GND | GND | Ground bersama |
| **GPIO 21 (SDA)** | SDA | SDA | Bus I2C Data bersama |
| **GPIO 22 (SCL)** | SCL | SCL | Bus I2C Clock bersama |
| **GPIO 32** | XSHUT | - | Kontrol reset Sensor A |
| **GPIO 33** | - | XSHUT | Kontrol reset Sensor B |

---

## Perintah Serial Monitor (115200 Baud)

- `S` / `status` : Menampilkan ringkasan status sensor A/B, hitungan, Wi-Fi, ThingSpeak HTTP code, dan status WDT.
- `R` / `reset`  : Mereset hitungan okupansi menjadi 0 dan menyimpan ke NVS.
- `W` / `wifi`   : Membuka Captive Portal WiFiManager secara on-demand.
- `C` / `cloud`  : Memaksa penjadwalan sinkronisasi data ke ThingSpeak.
- `H` / `help`   : Menampilkan daftar bantuan perintah.

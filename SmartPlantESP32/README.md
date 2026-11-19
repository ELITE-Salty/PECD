# Smart Plant Monitoring System — ESP32 sensor controller

This is the ESP32 half of the two-board assignment. The ESP32 reads soil
moisture and DHT11 data, evaluates rules from `configuration.txt`, drives a
buzzer, stores an `events_log.txt` in its local flash, and light-sleeps between
measurements. Raspberry Pi camera capture and on-device computer vision are
**not implemented in this package**; those remain required to finish the PDF.

## Wiring (classic ESP32, not ESP32-C3/S3)

| Device | ESP32 pin | Notes |
| --- | --- | --- |
| Soil moisture analog output | GPIO34 | ADC1 channel 6; sensor analog voltage must stay <=3.3 V |
| DHT11 data | GPIO26 | 3.3 V DHT11; bare sensors generally need a ~10k pull-up |
| Passive buzzer signal | GPIO27 | 800 Hz LEDC PWM; use transistor/driver if buzzer needs more current |
| All grounds | GND | Shared ground is mandatory |

For a 3.3V capacitive soil sensor, calibrate the two raw values in `main/main.c`:

    #define SOIL_RAW_DRY 3200
    #define SOIL_RAW_WET 1300

Those are **placeholders, not trustworthy measurements**. Take readings with
probe in dry air/dry soil and in wet soil (as the sensor maker recommends).
This firmware maps DRY to 0 and WET to 1000; threshold `< 300` then means
"too dry". If your sensor's output direction is reversed, account for that
in calibration before relying on alarms.

## Rules

Edit `spiffs/configuration.txt`:

    1: air_humidity > 50
    2: air_temperature < 15
    3: soil_humidity < 300

Supported sensors: `air_humidity`, `air_temperature`, `soil_humidity`.
Supported comparisons: `>`, `<`, `>=`, `<=`, `==`, `!=`.
The leading number defines how many times the buzzer beeps (1-10).

A rule beeps *once when it first becomes true*. Persistent violations don't
cause infinite beeping; the alarm re-arms after the reading returns to normal.
A disconnected or failing sensor doesn't falsely clear its alarm state.

## Build and flash

Requires ESP-IDF >=5.3, target `esp32`, a board with >=4 MB flash, and network
access during first build to download the DHT component dependency.

```bash
source ~/esp/esp-idf/export.sh   # adapt this path to your ESP-IDF installation
cd SmartPlantESP32
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor  # replace port with yours
```

The `idf_component.yml` downloads `esp-idf-lib/dht`. The main component
explicitly depends on `esp_driver_ledc`, which supplies `driver/ledc.h` in
modern ESP-IDF releases.

The build also creates a SPIFFS image of `spiffs/` and flashes it together with
the firmware. **Warning: running `idf.py flash` again reflashes the SPIFFS
partition and erases previous `events_log.txt` files.** Export logs before
reflashing. For field usage you would want to separate configuration and logs
into different partitions or update configuration via a command interface.

`partitions.csv` assumes a classic ESP32 with >=4MB flash.

## Logging and time

Each sensor measurement and alarm transition is appended to
`/spiffs/events_log.txt`; a previous log rotates to
`/spiffs/events_prev.txt` after roughly 128 KiB.

The firmware doesn't configure Wi-Fi, so it doesn't sync a real calendar clock.
Until you add NTP, an RTC (e.g. DS3231), or a timestamp sync from the
Raspberry Pi, event timestamps are explicitly marked `uptime=123s` rather
than pretending to be correct dates. For the PDF's full calendar-formatted
log requirement, add actual time synchronization and set the local timezone.

SPIFFS is intended for small experiments, not high-volume permanent logging.
Flash wears with repeated writes. If you plan to log for months, periodically
transfer logs to the Pi or use an SD card and keep only buffered summaries.

## Saving energy

- Monitor sensors and sound alarms entirely on ESP32; Pi need not run 24/7.
- If readings or alerts are changing, read every 60 seconds.
- After three consecutive stable, non-alert readings, read every 5 minutes.
- Enter light sleep between sensor reads; Wi-Fi isn't turned on.
- The DHT11 and soil sensor remain powered. Real battery optimization should
  also switch off the sensors when idle, and consider deep sleep with alarm
  persistence in RTC memory/NVS. A typical ESP32 dev board's USB bridge and
  power regulator can dominate sleep current.

## Requirements still outstanding from the original assignment

1. Raspberry Pi + RGB camera capture and efficient on-device image analysis
   (e.g. leaf yellowing / leaf counting or quantized model).
2. True wall-clock timestamps (NTP or RTC).
3. Greenhouse/plant-specific calibration and threshold research.
4. Test scenarios measuring environmental and visual accuracy and power draw.
5. GitHub publication, a report explaining the design and comparison to
   alternatives, and full practical evaluation.

## Notes

No real ESP32 build or hardware test was possible in the generation environment.
The code needs to be built and tested on your specific ESP32 and sensors.

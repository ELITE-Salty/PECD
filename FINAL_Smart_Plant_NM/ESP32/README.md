# ESP32 firmware

ESP-IDF application responsible for environmental sensing, alarm rules,
buzzer notifications and event logging.

## Responsibilities

- Read soil moisture (analog, ADC) and air temperature/humidity (DHT11).
- Load alarm rules from `configuration.txt` and evaluate them after each
  valid reading.
- Signal alarms with the buzzer (N beeps, as defined by the rule).
- Record measurements and actions in `events_log.txt`.
- *(Planned)* adaptive sampling, sleep modes, communication with the
  Raspberry Pi.

## Hardware configuration

| Signal | GPIO | Peripheral | Configuration |
| --- | --- | --- | --- |
| Soil moisture (analog out) | 34 | ADC1 channel 6, one-shot driver | 12-bit, `ADC_ATTEN_DB_12` |
| DHT11 data | 26 | `esp-idf-lib/dht` (bit-banged) | internal pull-up enabled |
| Buzzer (passive) | 27 | LEDC low-speed, timer 0, channel 0 | 800 Hz, 10-bit, 50 % duty when on |

GPIO34 is input-only on the classic ESP32 and belongs to ADC1, which (unlike
ADC2) remains usable if Wi-Fi is enabled later.

## Sampling and timing

| Parameter | Value | Origin |
| --- | --- | --- |
| `SOIL_INTERVAL_MS` | 1000 ms | soil module |
| `DHT_INTERVAL_MS` | 2500 ms | DHT11 module (DHT11 must not be polled faster than ~1 Hz) |
| `DHT_STARTUP_MS` | 2000 ms | DHT11 settling time after power-up |
| `BEEP_ON_MS` / `BEEP_OFF_MS` | 200 / 250 ms | first integrated version |
| `BEEP_GROUP_GAP_MS` | 600 ms | separates beep groups of different rules |

The two sensors are scheduled independently: the main loop keeps one
next-sample deadline per sensor and blocks (`vTaskDelay`) until the earliest
deadline. No additional FreeRTOS tasks are used. These fixed intervals are the
reference configuration for the adaptive sampling experiment; they are not
claimed to be energy-optimal.

## Alarm behaviour

- A rule triggers the buzzer only on the transition from *false* to *true*;
  a persistent condition does not beep repeatedly.
- A rule is re-armed when a valid reading no longer satisfies it.
- Rules are not evaluated after a failed read, so a sensor fault cannot clear
  an alarm state.
- `soil_humidity` is compared in **raw ADC counts (0..4095)** until the sensor
  is calibrated. The example threshold `300` from the assignment refers to the
  0..1000 scale and is therefore not meaningful yet.

## Storage

A 1 MiB SPIFFS partition (`storage`, see `partitions.csv`) holds
`configuration.txt` and the log. The log is rotated to `events_prev.txt` when
it reaches 256 KiB. Log lines use uptime because no wall-clock source exists:

```
uptime=12s: soil_humidity = 2310 (raw ADC)
uptime=13s: air_temperature = 21.0
uptime=13s: air_humidity = 48.0
uptime=13s: buzzer 1
```

To retrieve the log, type `logs` + Enter in `idf.py monitor`; each line is
printed with an `@@PLANT|` prefix.

## Build and flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p <PORT> storage-flash     # writes configuration.txt; ERASES the stored log
idf.py -p <PORT> flash monitor     # firmware only; keeps the stored log
```

`configuration.txt` is taken from `../Configuration/` at build time.

**Build status:** not yet built with ESP-IDF in this repository. The source
has only passed a host-side syntax check (GCC with stub headers), which does
not validate the ESP-IDF API usage, the CMake configuration or behaviour on
hardware.

## Dependencies

- ESP-IDF ≥ 5.3 (team environment: v6.1.0, per `dependencies.lock`).
- `esp-idf-lib/dht` ^1.2.0 (resolved to 1.3.0 in `dependencies.lock`),
  downloaded automatically by the IDF Component Manager.

## Known limitations

1. Soil readings are uncalibrated raw values; a single ADC sample is used
   (no filtering) and the ESP32 ADC is not linearity-corrected.
2. Timestamps are uptime-based, not the calendar format of the assignment.
3. With 1 s soil sampling every reading is written to flash: high write rate
   (flash wear) and only a few hours of history before rotation.
4. `beep()` blocks the main loop for up to ~5 s (10 beeps).
5. No sleep modes: the CPU idles in FreeRTOS between samples, and the sensors
   are permanently powered.
6. Rules support only `<` and `>`.
7. `storage-flash` overwrites the whole partition, including logs.

## Files

| File | Purpose |
| --- | --- |
| `main/main.c` | Baseline firmware |
| `main/idf_component.yml` | DHT component dependency |
| `partitions.csv`, `sdkconfig.defaults` | Custom partition table, 4 MB flash |
| `dependencies.lock` | Pinned component versions (from the first integrated version) |
| `reference/` | Unmodified original modules, kept for traceability |

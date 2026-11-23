# ESP32 firmware

ESP-IDF application responsible for environmental sensing, alarm rules,
buzzer notifications and event logging.

## Sleep measurements: light and deep

`main/optimised.c` provides two separate test modes. Each cycle reads soil
moisture and the DHT11, performs the same basic Pi UART exchange, saves the
events to ESP32 flash, then sleeps for **2500 ms**. A timer wakes it for the
next cycle. The original `unoptimised.c` is unchanged.

Run these commands from this ESP32 folder (not `ESP32/main`) in an ESP-IDF
terminal. Use your actual serial port: `COM5` is the saved Windows setting;
on Linux it may be `/dev/ttyUSB0`. Build all three firmware images with:

```bash
idf.py -B build-unoptimised -DPLANT_VARIANT=unoptimised build
idf.py -B build-light -DPLANT_VARIANT=light build
idf.py -B build-deep -DPLANT_VARIANT=deep build
```

Then flash and monitor one test at a time (exit the monitor with Ctrl+]):

| Test | Flash and monitor |
| --- | --- |
| Original baseline | `idf.py -B build-unoptimised -p COM5 flash monitor` |
| Light sleep | `idf.py -B build-light -p COM5 flash monitor` |
| Deep sleep | `idf.py -B build-deep -p COM5 flash monitor` |

Each build directory remembers its selected variant; a fresh build defaults
to `unoptimised`. Keep the same `-B` directory for build, flash and monitor
so you run the intended image. CMake prints `Plant firmware variant: ...`
during configuration. Ordinary firmware flashing preserves existing logs.

Light sleep resumes the loop. Deep sleep restarts the ESP32 application,
reinitialises its peripherals, and continues with a cycle counter retained
in RTC memory. Both modes play the original startup beeps and wake the Pi
only after power-on/manual reset, **not after each sleep**. The buzzer is held
off and the Pi wake line is held released during sleep.

Logs still use `/spiffs/events_log.txt` and the same rotation/download format.
Optimised entries additionally identify `mode=light` or `mode=deep` and
`cycle=N`, including sleep-entry and wake events. In deep mode, the
`uptime=` value restarts on each wake; use the cycle number to follow the
sequence. A manual reset starts the cycle count again and writes a new
system-start entry. These are not calendar timestamps or power readings.

### Downloading logs from a sleeping ESP32

After pressing **EN/reset**, either optimised mode stays awake for **15
seconds** to accept the existing `logs` command. This window is skipped
after a deep-sleep timer wake. To download:

1. Close the serial monitor and prepare `python tools/download_logs.py COM5`.
2. Press EN, wait about one second for boot, then run the command within the
   15-second window.
3. Find the result in `downloaded_logs/<date-and-time>/events_log.txt`.

An accepted download finishes before sampling/sleep starts, even if it takes
longer than 15 seconds. With `idf.py monitor`, press EN and type `logs` +
Enter in the same window. USB/Pi input received while sleeping is not
guaranteed to be retained; use this reset window for reliable downloads.
Downloading does not delete the original files.

### Comparing power

Start the power recording after the download window and startup beeps
(allow about 30 seconds after reset). Record light and deep sleep separately,
using the same supply, sensor wiring and Pi response state as the baseline.
The 2500 ms is the **sleep duration**, not the total sampling period: UART
receive time, sensing, flash writes and deep-sleep boot time also count.
Include complete wake/read/communicate/sleep cycles in each recording.
Keep the two-second UART receive timeout for this first comparison.

Sensors and the Pi remain powered; this code does not shut them down.
Board LEDs, regulator and USB interface consumption also remain in a
whole-board measurement. Measure the actual saving with your power monitor;
the firmware's event log does not measure current or energy.
See [the measurement procedure](../Tests/energy_measurements/README.md).
Sleep behaviour follows the
[Espressif sleep-mode documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/sleep_modes.html).

Validation (2026-11-23): restored `main/CMakeLists.txt` after a stray character
had replaced its contents and caused a CMake parse error. Light, deep and
unoptimised variants all build successfully in their separate directories
with ESP-IDF 6.1.1. Verified the selected sources and the light/deep compile
definitions. The CP210x serial adapter is detected on COM5; firmware was not
flashed or hardware-tested in this check. Verify repeated wakes, sensor/Pi
readings and log downloads before collecting final measurements.

## Baseline option: unoptimised.c

`PLANT_VARIANT=unoptimised` builds `main/unoptimised.c`. It reads both
sensors, exchanges a greeting with the Pi, then waits 2500 ms between cycles.
The UART read can add up to 2000 ms, so the cycle can take about 4.5 seconds
plus sensor and flash-write time. It keeps the existing moisture
conversion (`MOISTURE_DRY` / `MOISTURE_WET`) and startup Pi wake pulse.

Logs are saved directly in the ESP32's built-in flash (SPIFFS); no Pi or
computer is needed while recording. This option appends startup, wake, valid
sensor readings and sensor read errors to `/spiffs/events_log.txt`, using
uptime timestamps. Soil entries
include both raw ADC counts and the calculated percentage. Actual calls to
`beep()` are logged too; the current startup demonstration plays three short
beeps and one five-second beep before sampling begins.
The current log rotates at 256 KiB to `events_prev.txt`, retaining one previous
file. Logs survive reset and normal firmware flashing. A storage failure is
reported on the console and does not stop sensing or automatically format flash.

To view the saved files later, connect USB and type `logs` + Enter in
`idf.py monitor`. This prints the stored text without deleting it. Allow about
4.5 seconds plus sensor/flash time for a response; printing large logs pauses
sampling and Pi exchanges until complete.
This option has no Pi log transfer or remote commands. The alarm rules and
daily wake scheduler described for `main.c` below are also separate features.

### Basic Pi communication for the unoptimised measurement

UART2 sends `Hello from ESP32\n` once per sensor cycle, then calls
`uart_read_bytes` with a two-second timeout, as in the supplied lab example.
Any received bytes are displayed and logged; `ACK` is not required and there
is no Pi-ready wait or reconnect loop. The timeout is part of this baseline.
Sent text, received text, and receive timeouts are recorded in `events_log.txt`.
The firmware logs received chunks rather than assembling complete messages.

Wire ESP32 GPIO17/TX2 to Pi pin 10/RX, GPIO16/RX2 to Pi pin 8/TX, and connect
GND. Use 115200 baud, 8N1. The existing `pi_wake/main/Something.py` script can
run on the Pi: it already answers `Hello from ESP32` with `ACK\n` without
requiring a preceding `PING`. Stop other Pi programs using the same UART first.

For comparison measurements, note whether the Pi was connected and replying.
The added UART exchange and its flash log writes are both included in the
measured consumption. Download logs after the measurement, not during it.

To download the files to your PC, close the serial monitor (Ctrl+]) and run
this from the ESP32 folder in an ESP-IDF terminal:

```bash
python tools/download_logs.py COM5
```

Open `downloaded_logs/<date-and-time>/events_log.txt` for the current log.
`events_prev.txt` is downloaded too if present. Each download gets its own
folder, so earlier copies are kept. The originals remain on the ESP32.
Allow up to a couple of minutes for large logs; sampling pauses during the
transfer. Interrupted downloads keep a `.partial` extension so they cannot
be mistaken for complete files. The firmware yields briefly during large
exports so the watchdog stays serviced.

Build with `idf.py build`, then flash firmware with
`idf.py -p <PORT> flash monitor`. If SPIFFS has never been initialised, run
`idf.py -p <PORT> storage-flash` once, then reset the ESP32. **storage-flash
erases existing logs**, so export any retained data before using it. Ordinary
`flash` does not include the storage image.

Validation: the basic UART exchange and its storage image built successfully
with ESP-IDF 6.1.1 on 2026-11-20. This latest UART addition has not been flashed
or tested with the Pi: the ESP32 was not connected over USB during the build.
The earlier logging version was flashed on COM5 with hash verification;
live soil/DHT readings and new saved entries were verified through `logs`.
Existing logs were preserved. Windows required the CP210x USB serial driver;
the ESP32 VS Code folder settings now select COM5.
The USB downloader was verified on COM5 after flashing the export-yield fix;
both stored files downloaded completely, and five transfer/error tests passed.

The remaining sections describe the integrated `main.c` option unless noted.

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
printed with an `@@PLANT|` prefix. The same commands work on UART2, TX=GPIO17,
RX=GPIO16, 115200 8N1, for the Pi. `PING` replies `@@PLANT|READY`;
`BEEP 4` plays four beeps, logs the vision alarm and replies
`@@PLANT|BEEP|4|DONE` after completion. Counts 1..10 are accepted. Replies
return on the requesting port. See [the Pi guide](../RaspberryPi/README.md)
for framing, wiring and configuration.

The daily Pi wake scheduler uses GPIO25 in open-drain mode, connected to Pi
GPIO3 (physical pin 5). It sends a 250 ms low pulse every 24 hours, beginning
24 hours after ESP32 startup. ESP32 reset restarts the countdown; this is an
uptime interval rather than a wall-clock alarm. `WAKE_STATUS` reports seconds
remaining. `WAKE_IN 60` schedules one short test, then normal daily scheduling
resumes. Valid test delays are 30..86400 seconds. The wake wire and updated
firmware must be in place before enabling Pi shutdown. See the Pi guide for
the full daily sequence and limitations.

## Build and flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p <PORT> storage-flash     # writes configuration.txt; ERASES the stored log
idf.py -p <PORT> flash monitor     # firmware only; keeps the stored log
```

`configuration.txt` is taken from `../Configuration/` at build time.

**Build status:** successfully built with the installed ESP-IDF 6.1 toolchain
on 2026-11-19, including the UART2, remote alarm and daily wake changes.
The UART/beep version was tested with the Pi. The latest daily-wake version
was flashed with hash verification, preserving SPIFFS. A 60-second wake test
sent the GPIO25 pulse, restored the daily interval and successfully booted
PECD-NK. The Pi's automatic boot-cycle/shutdown service still needs sudo setup.

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
| `main/unoptimised.c` | Original fixed-delay option with persistent logging |
| `main/optimised.c` | Separate light/deep-sleep tests with the same sensing and UART exchange |
| `main/idf_component.yml` | DHT component dependency |
| `partitions.csv`, `sdkconfig.defaults` | Custom partition table, 4 MB flash |
| `dependencies.lock` | Pinned component versions (from the first integrated version) |
| `reference/` | Unmodified original modules, kept for traceability |

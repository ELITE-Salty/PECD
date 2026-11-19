# Technical decisions

Each entry records the decision, the alternatives considered and the
reasoning. **Status:** *Confirmed* = implemented and agreed by the team;
*Provisional* = implemented in the baseline but awaiting team confirmation or
hardware validation; *Proposed* = not implemented.

---

### D1 - Baseline source for the ESP32 firmware
**Status:** Provisional

Two integrated versions existed: `Smart_Plant_Monitoring_System_NM`
(single 5 s loop, SPIFFS rules and log) and `SmartPlantESP32` (additionally
adaptive sampling and light sleep, generated with AI assistance and not
confirmed to work).
**Decision:** derive the baseline from `Smart_Plant_Monitoring_System_NM`.
**Reason:** a simple, understandable baseline is needed as the reference for
the energy experiments. Adaptive sampling and sleep are the *subjects* of
Experiments 2 and 4 and should be introduced one at a time, so that their
effect can be measured against the baseline.

### D2 - Keep soil moisture as raw ADC counts
**Status:** Provisional

Alternatives: (a) linear mapping to 0..1000 with assumed dry/wet values,
(b) raw counts until calibration.
**Decision:** (b).
**Reason:** the dry/wet constants in v0 (3200/1300) were placeholders. A
mapping based on them would give the impression of a calibrated value. The
soil threshold in `configuration.txt` is consequently in raw counts for now.

### D3 - Single main loop, no additional FreeRTOS tasks
**Status:** Provisional

Alternatives: one task per sensor; FreeRTOS software timers; a single loop.
**Decision:** a single loop with one deadline per sensor.
**Reason:** two sensors with second-scale periods do not need concurrency.
A single loop is easier to explain, has no shared-state synchronisation, and
makes the later transition to sleep modes straightforward (one place decides
how long the system can sleep). Trade-off: a long buzzer sequence delays the
next sample by up to a few seconds.

### D4 - Independent sampling deadlines based on `esp_timer_get_time()`
**Status:** Provisional

**Reason:** the DHT11 has a minimum sampling period (~1 s), while the ADC does
not, so the two intervals must be configurable separately. The next deadline
is computed from the start of the current sample (`now + interval`); missed
deadlines are not "caught up", which avoids bursts after a blocking beep.

### D5 - Buzzer only on transitions into an alarm state
**Status:** Confirmed (already in v0)

**Reason:** beeping at every sample during a persistent condition would be
annoying and would waste energy. Failed reads do not change the rule state.

### D6 - SPIFFS on the ESP32 for `configuration.txt` and `events_log.txt`
**Status:** Provisional (already in v0)

Alternatives: SD card; NVS; storing the log on the Raspberry Pi.
**Reason:** no extra hardware, and the ESP32 can operate without the
Raspberry Pi. Trade-offs: limited size (1 MiB), flash wear at high write
rates, and `storage-flash` erases the log. The final location of the log
depends on the communication decision (P1).

### D7 - Log rotation at 256 KiB
**Status:** Provisional

**Reason:** bounds flash usage to roughly half of the partition. v0 already
exported an `events_prev.txt` file but never created it.

### D8 - Uptime timestamps instead of calendar dates
**Status:** Provisional

**Reason:** the ESP32 has no time source (no Wi-Fi/NTP, no RTC). Writing a
calendar date would be incorrect. To be revisited once P1 is decided (the
Raspberry Pi could provide the time).

### D9 - Single source for `configuration.txt`
**Status:** Provisional

v0 contained two copies with different values (`air_temperature < 15` and
`< 30`), and only one of them was actually flashed.
**Decision:** `Configuration/configuration.txt` is copied into the SPIFFS
image at build time (`configure_file` in `ESP32/main/CMakeLists.txt`).

### D10 - SPIFFS image not flashed with `idf.py flash`
**Status:** Provisional (already in v0)

Alternative: `FLASH_IN_PROJECT`, which flashes the image with every firmware
update. **Reason:** avoids accidentally erasing collected logs during
development; rules are written explicitly with `idf.py storage-flash`.

### D11 - Internal pull-up on the DHT11 data line
**Status:** Provisional

The standalone DHT11 module used `GPIO_FLOATING`; v0 changed this to
`GPIO_PULLUP_ONLY`. The v0 setting is kept: an internal pull-up in parallel
with an external module resistor is harmless. To be confirmed against the
actual DHT11 board (bare sensor or module with resistor).

### D12 - 4 MB flash configuration
**Status:** Provisional

`partitions.csv` requires ~3.1 MB, more than the ESP-IDF default of 2 MB;
`sdkconfig.defaults` selects 4 MB. Must match the actual board.

---

## Proposals (not implemented)

### P1 - ESP32 ↔ Raspberry Pi communication
UART (lab prototype exists), Wi-Fi (MQTT/HTTP) or USB serial. To be decided
by the team on the basis of measured energy cost and reliability.

### P2 - Computer vision approach
Colour-based segmentation/rules vs. a pre-trained classifier vs. both
(segmentation first, classifier only when needed). Must consider model size,
quantisation, inference time and energy on the Raspberry Pi, and species
coverage.

### P3 - Adaptive sampling (Experiment 2)
Lengthen the sampling interval when readings are stable and far from
thresholds; shorten it near thresholds.

### P4 - Sleep modes (Experiment 4)
Light sleep vs. deep sleep between samples. Deep sleep requires preserving
rule state (RTC memory) and re-initialising peripherals after wake-up.

### P5 - Sensor power gating
Powering the soil sensor from a GPIO (or transistor) only during a
measurement. Requires checking sensor current and settling time.

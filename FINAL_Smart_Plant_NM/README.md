# Smart Plant Monitoring System

Course project for *Programming of Energy-Constrained Devices* (FRI, 2026/27).
Manuel Gonçalves and Nik Kopi.

An ESP32 that is always on reads soil moisture, air temperature and humidity,
checks alarm rules from `configuration.txt`, beeps a buzzer and logs every
event in `events_log.txt`. A Raspberry Pi 3B with a camera is booted by the
ESP32 only when a photo is needed (once a day or when an alarm starts),
checks the plant with a two-stage vision pipeline and powers off again.

**Report:** [`Report/Smart_Plant_Report.pdf`](Report/Smart_Plant_Report.pdf) (LNCS, source in `Report/main.tex`) ·
**Presentation:** [`Report/Smart_Plant_Presentation.pptx`](Report/Smart_Plant_Presentation.pptx)

## Main results

| System (energy per day) | ESP32 | Pi | Total |
| --- | ---: | ---: | ---: |
| A. Baseline: ESP32 always awake (`unoptimised.c`), Pi always on | 18.9 kJ | 107 kJ | **126 kJ** |
| B. Fixed 10 s light sleep (measured), Pi photo every hour | 7.3 kJ | 7.2 kJ | 14.5 kJ |
| C. Ours: deep sleep, adaptive 60-900 s interval, Pi daily or on alarm | 3.9 kJ | 0.4 kJ | **4.4 kJ** |

A and the components of B are measured with a Monsoon power monitor; C uses a
model built from those measurements and a simulated 3.5-day scenario (the final
firmware has not been recorded on the Monsoon). The halted Pi's standby draw
was not measured and is excluded from B and C.

## Repository structure

```
FINAL_Smart_Plant_NM/
├── ESP32/            ESP-IDF firmware (4 variants, see ESP32/README.md)
├── RaspberryPi/      photo + vision cycle, UART client, boot/shutdown service
├── Configuration/    configuration.txt (alarm rules for the sunflower)
├── Measurements/     raw Monsoon recordings (.pt5 / .cd5)
├── Tests/            analysis scripts, energy model, unit tests, simulation
├── Results/          numbers computed from the measurements (JSON)
└── Report/           report (LaTeX), figures, presentation
```

## Hardware

| Device | Connection |
| --- | --- |
| ESP32 DevKit (ESP32-D0WD-V3, 4 MB flash) | - |
| Soil moisture sensor, analog | GPIO34 (ADC1 CH6, 12 bit) |
| DHT11 | GPIO26 |
| Passive buzzer | GPIO27 (LEDC PWM, 800 Hz) |
| Raspberry Pi 3 Model B + IMX219 camera | wake: ESP32 GPIO25 (open drain) -> Pi GPIO3 (pin 5); UART: ESP32 GPIO17 TX -> Pi pin 10, GPIO16 RX <- Pi pin 8, 115200 8N1; common GND |

## Firmware variants (ESP32)

| Variant | File | Status |
| --- | --- | --- |
| `unoptimised` (baseline) | `ESP32/main/unoptimised.c` | built, flashed, measured |
| `light` / `deep` (10 s sleep) | `ESP32/main/optimised.c` | built, flashed, measured |
| `adaptive` (final) | `ESP32/main/adaptive.c` + `adaptive_sampling.h` | policy unit-tested on a PC; **not yet built with ESP-IDF or tested on hardware** |
| integrated, no sleep | `ESP32/main/main.c` | built, flashed; rules, log and Pi protocol tested |

```bash
cd ESP32
idf.py -B build-adaptive -DPLANT_VARIANT=adaptive build
idf.py -B build-adaptive -p <PORT> storage-flash   # first time: writes configuration.txt (erases the log)
idf.py -B build-adaptive -p <PORT> flash monitor
```

## Reproducing the results

```bash
# energy summaries from the raw recordings
python Tests/energy_measurements/analyse_pt5.py Measurements Results/energy_consumption/esp32_unoptimised_summary.json
python Tests/energy_measurements/analyse_sleep_and_pi.py Measurements Results/energy_consumption/esp32_sleep_and_pi_summary.json
# figures and model
python Tests/energy_measurements/plot_traces.py Measurements
python Tests/energy_measurements/energy_model.py
# adaptive interval: unit tests + scenario
gcc -std=c11 -O2 -IESP32/main Tests/adaptive_sampling/test_adaptive_sampling.c -o Tests/adaptive_sampling/test_adaptive_sampling
Tests/adaptive_sampling/test_adaptive_sampling
cd Tests/adaptive_sampling && python simulate_scenario.py
```

Other tests: `python -m unittest Tests/computer_vision/test_plant_cycle.py Tests/test_download_logs.py`
and `gcc -std=c11 Tests/computer_vision/test_pi_wake_schedule.c -o w && ./w`.
Python needs `numpy` and `matplotlib`.

## Status and known limitations

- The final firmware `adaptive.c` is not yet measured on the hardware; its
  energy comes from the model (Report, Sect. 4).
- In the last wake test the UART link did not answer after the Pi booted, and
  the automatic power-off service was not installed (needs sudo on the Pi);
  see `Results/pi_wake_test_2026-11-19.json`.
- The vision classifier covers pepper, potato and tomato only (PlantVillage);
  for the sunflower only the small model and the colour screening are used.
  The completed lab code it imports lives on the Pi (`~/Desktop/leafs-starter`,
  `~/Desktop/AI_base`), see `RaspberryPi/README.md`; the original lab handouts
  and models are in `../Labs/leafs-starter` and `../Labs/leafs_advanced`.
- No systematic accuracy study of the alarms or the vision was done.
- Event-log timestamps are time since power-on (no clock on the board).

## Who did what

- **Manuel Gonçalves - ESP32:** firmware for the soil sensor, DHT11 and
  buzzer, alarm rules, event log, soil calibration, adaptive sampling, ESP32
  energy measurements with the Monsoon.
- **Nik Kopi - Raspberry Pi:** Pi and camera set-up, two-stage vision
  pipeline, UART protocol with the ESP32, wake/power-off cycle.
- **Together:** test scenario, measurements, presentation and report.

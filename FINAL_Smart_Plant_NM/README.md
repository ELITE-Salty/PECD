# Smart Plant Monitoring System

Course project for *Programming of Energy-Constrained Devices* (PECD).

An energy-efficient plant monitoring system that combines soil-moisture,
temperature and humidity sensing on an ESP32 with camera-based plant health
analysis on a Raspberry Pi. Unfavourable conditions are signalled to the user
with a buzzer, using rules defined in `configuration.txt`, and all
measurements and actions are recorded in `events_log.txt`.

## Objectives

1. Monitor soil moisture, air temperature and air humidity, and detect
   unfavourable conditions defined by configurable rules.
2. Capture RGB images and detect visual signs of plant distress (e.g. leaf
   yellowing) with a resource-efficient on-device computer vision model.
3. Minimise energy consumption through sampling-rate optimisation, selective
   camera activation, sleep modes and efficient model execution.
4. Evaluate detection accuracy (environmental and visual) and the energy
   consumption of the system and of its individual components.

## Hardware

| Component | Role | Connection |
| --- | --- | --- |
| ESP32 (classic, dual-core) | Sensor acquisition, alarm rules, buzzer, logging | - |
| Capacitive/resistive soil moisture sensor *(exact model to be confirmed)* | Soil moisture (analog) | ESP32 GPIO34 (ADC1 CH6) |
| DHT11 | Air temperature and relative humidity | ESP32 GPIO26 |
| Passive buzzer | Audio alerts | ESP32 GPIO27 (LEDC PWM, 800 Hz) |
| Raspberry Pi *(model to be confirmed)* | Camera acquisition, computer vision | - |
| RGB camera *(model to be confirmed)* | Plant images | Raspberry Pi |

Wiring details: [Documentation/hardware/](Documentation/hardware/README.md).

## Repository structure

```
FINAL_Smart_Plant_NM/
├── ESP32/            ESP-IDF firmware (sensors, buzzer, rules, event log)
│   └── reference/    unmodified snapshots of the original modules
├── RaspberryPi/      camera and computer vision (in development)
├── Configuration/    configuration.txt (alarm rules) - single source
├── Tests/            scripts and procedures for each experiment
├── Results/          raw measurements, energy data, graphs
├── Documentation/    architecture, hardware, software, experiments
└── Report/           report outline and figures
```

## Implementation status

Status labels: **Implemented** (code exists), **Tested** (verified on
hardware, with evidence in `Results/`), **Proposed**, **Not started**.

| Feature | Status |
| --- | --- |
| Buzzer, soil ADC and DHT11 as individual modules | Implemented; tested individually during the labs |
| Integrated ESP32 baseline (single loop, fixed intervals) | Implemented, **not yet built or tested in this repository** |
| `configuration.txt` rule parsing and buzzer alarms | Implemented (baseline), not yet tested |
| `events_log.txt` on SPIFFS (uptime timestamps) | Implemented (baseline), not yet tested |
| Calendar timestamps in the event log | Not started (needs a time source) |
| Soil sensor calibration to the 0..1000 scale | Not started (Experiment 1) |
| Adaptive sampling | Proposed (Experiment 2) |
| ESP32 sleep modes | Proposed (Experiment 4) |
| ESP32 ↔ Raspberry Pi communication | Not decided; a UART prototype exists from the labs |
| Camera capture and computer vision | In development by the Raspberry Pi team member |
| Energy measurements | Not started (Experiment 5) |

## Build (ESP32)

Requirements: ESP-IDF ≥ 5.3 (the team uses v6.1), target `esp32`, a board
with 4 MB flash, and internet access on the first build (the DHT component is
downloaded from the ESP Component Registry).

```bash
cd ESP32
idf.py set-target esp32
idf.py build
idf.py -p <PORT> storage-flash   # first time and after editing configuration.txt
idf.py -p <PORT> flash monitor
```

> These are the standard ESP-IDF commands for this project layout. The build
> has **not yet been verified** in this repository structure; see
> [ESP32/README.md](ESP32/README.md).

## Team and contributions

| Member | Main responsibility |
| --- | --- |
| *(name)* | ESP32: sensing, buzzer, calibration, adaptive sampling, ESP32 energy measurements |
| *(name)* | Raspberry Pi: setup, ESP32 communication, camera, computer vision |

## Documentation

- [System architecture](Documentation/architecture/system_architecture.md)
- [Technical decisions](Documentation/architecture/technical_decisions.md)
- [Experimental plan](Documentation/experiments/experimental_plan.md)
- [Results template](Documentation/experiments/results_template.md)
- [Report outline](Report/report_outline.md)

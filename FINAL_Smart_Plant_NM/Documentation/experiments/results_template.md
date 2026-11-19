# Results template

Copy this template for each experiment into `Results/` (e.g.
`Results/energy_consumption/E5_idle_<date>.md`). Leave fields empty rather
than estimating them. Raw data files (CSV, logs) are stored next to the
summary and referenced by name.

---

## Experiment: <ID and name>

**Date:** YYYY-MM-DD **Performed by:** <name>
**Firmware / software version:** <git commit hash>
**Configuration:** <sampling intervals, filter, sleep mode, model, ...>

### Measurement conditions

| Item | Value |
| --- | --- |
| Measurement instrument (model) | |
| Instrument sampling rate / resolution | |
| Measurement point (USB 5 V input / 3.3 V rail / sensor supply) | |
| Supply voltage (V) | |
| Board (ESP32 module / DevKit model; Raspberry Pi model) | |
| Ambient temperature (°C) / humidity (%RH) | |
| Soil / plant / lighting conditions (if relevant) | |
| Duration of each run | |
| Number of repetitions | |

### Methodology

<Steps followed, so that another person can reproduce the measurement.>

### Results - energy

| Condition | Mean current (mA) | Std. dev. (mA) | Voltage (V) | Mean power (mW) | Duration (s) | Energy (mJ) | n |
| --- | --- | --- | --- | --- | --- | --- | --- |
| | | | | | | | |

### Results - sensing / detection

| Condition | Readings per hour | Detection latency (s) | Missed alarms | False alarms | n |
| --- | --- | --- | --- | --- | --- |
| | | | | | |

### Results - calibration / filtering (Experiment 1)

| Method | Mean raw | Std. dev. raw | Min | Max | Acquisition time (ms) | n |
| --- | --- | --- | --- | --- | --- | --- |
| Single reading | | | | | | |
| Mean of N | | | | | | |
| Median of N | | | | | | |

### Results - computer vision

| Model / variant | Size (MB) | Inference time (ms) | Energy per inference (mJ) | Accuracy | Precision | Recall | n images |
| --- | --- | --- | --- | --- | --- | --- | --- |
| | | | | | | | |

### Raw data files

- `<file name>` - <description>

### Interpretation

<What the results show, compared with the baseline or alternative.>

### Limitations and sources of error

<Instrument resolution, development-board overhead, small sample size,
environmental variation, anything that did not go as planned.>

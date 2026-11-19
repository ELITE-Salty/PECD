# Experimental plan

This plan prepares the experiments required by the assignment and the
presentation. No experiment has been performed yet. Results go into
`Results/` using `results_template.md`; scripts and procedures go into
`Tests/`.

## 0. Common methodology

**Current, power and energy.** Measured *current* I (A) at a known supply
voltage V gives *power* P = V·I (W). *Energy* is power integrated over time,
E = ∫ V·I dt (J), or E ≈ V·Ī·t for an approximately constant current. Energy
per sensing cycle and average power over a representative period (e.g. one
hour) are the main comparison metrics, because a faster but higher-current
operation can cost less energy overall.

**Instrumentation (to be confirmed):** the measurement device, its sampling
rate and resolution determine which events can be resolved. A multimeter can
only measure steady states; short events (a DHT11 read of a few ms, a beep)
need a sampling instrument (e.g. a USB power meter with logging, an INA219 /
INA226, or a power profiler). The device used must be recorded in every
result.

**Development board overhead.** An ESP32 DevKit includes a voltage regulator,
a USB-UART bridge and a power LED that consume current independently of the
firmware, and may dominate in sleep modes. Each result must state where
current was measured (USB input, 3.3 V rail, or sensor supply) and this
limitation must be discussed.

**Repetitions.** Each condition is measured at least 3 times (more for short
events); mean and standard deviation are reported.

---

## Experiment 1 - Soil sensor calibration and filtering

**Goal:** map raw ADC counts to the assignment's 0..1000 scale and choose a
reading method.

1. *Calibration:* record raw values with the probe in air, in dry soil, in
   saturated soil, and at several known water additions to a fixed soil mass.
   Determine the direction (higher = drier or wetter) and the usable range.
2. *Filtering:* compare
   - a single ADC reading,
   - the mean of N readings,
   - the median of N readings (N to be chosen, e.g. 5 and 15).

**Metrics:** standard deviation over a static condition, outliers, acquisition
time per measurement, and (if resolvable) energy per measurement.

**Expected trade-off:** more samples reduce noise but lengthen the active
time; the benefit must be measured, not assumed.

## Experiment 2 - Fixed vs. adaptive sampling

**Baseline:** fixed intervals of the current firmware (soil 1 s, DHT11 2.5 s),
plus a more realistic fixed interval (e.g. 60 s) for a fair comparison.

**Adaptive policy (to be designed):** interval increases while readings are
stable and far from thresholds, decreases when values change quickly or
approach a threshold.

**Metrics:** number of readings per hour, energy per hour, detection latency
(time from the real threshold crossing to the buzzer), detection reliability
(missed or false alarms).

**Procedure:** apply the same controlled change to each configuration (e.g.
watering a pot, or warming/cooling the DHT11) and log the time of the change
manually or with a reference instrument.

## Experiment 3 - Hierarchical sensing (camera activation)

**Compare:** periodic image capture (fixed period) vs. event-triggered
capture (triggered by a significant change in ESP32 measurements).

**Metrics:** number of captures, Raspberry Pi energy (including boot and idle
if it is powered on demand), and how many relevant visual changes are
missed.

**Dependency:** requires the ESP32 ↔ Raspberry Pi link (not yet defined).

## Experiment 4 - ESP32 sleep modes

**Compare:** active idle (current baseline: FreeRTOS delay), light sleep,
deep sleep.

**Metrics:** current in each state, wake-up latency, energy per sensing cycle,
peripheral re-initialisation needs (ADC, LEDC, DHT11 settling time after
power-up), effect on communication with the Raspberry Pi, effect on the
serial console.

**Note:** sleep is not assumed to be beneficial. At short intervals the
wake-up and re-initialisation costs (e.g. the DHT11 2 s settling time after
deep sleep, if the sensor is unpowered) may outweigh the savings. The
break-even sampling interval should be determined.

## Experiment 5 - Energy measurements

Measure current/power and compute energy for:

| Component / state | What is measured |
| --- | --- |
| ESP32 idle (baseline loop between samples) | steady current |
| Soil sensing | energy per reading (sensor supply + ESP32 active) |
| DHT11 sensing | energy per reading |
| Buzzer | current while sounding, energy per beep |
| Camera capture | energy per image (Raspberry Pi) |
| Computer vision | energy and time per inference (Raspberry Pi) |
| Raspberry Pi idle / boot | idle power, energy per boot |
| Complete system | average power over a representative period |

From these, an energy budget per hour (or per day) can be estimated for each
configuration and compared with direct measurements.

## Evaluation scenarios (assignment requirements)

1. **Environmental change detection:** change conditions slowly (e.g. let a
   pot dry out; warm the sensor area) and compare the time and value at which
   the system reacts against a reference reading.
2. **Visual distress detection:** present a set of plant images (healthy and
   distressed, including real photos of our own plants) to the camera and
   compute accuracy, precision and recall. Record lighting conditions.
3. **Practical end-to-end scenario (to be designed):** e.g. a pot that is not
   watered for several days, with the system running in its energy-saving
   configuration, logging events and alerting with the buzzer.

## Comparative analysis (presentation requirement)

At least one comparison against an alternative is required. Candidates:
fixed vs. adaptive sampling; active idle vs. light/deep sleep; periodic vs.
event-triggered camera; float vs. quantised CV model; on-device vs. server
inference.
